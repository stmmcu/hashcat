/**
 * Author......: See docs/credits.txt
 * License.....: MIT
 */

#include "common.h"
#include "types.h"
#include "memory.h"
#include "event.h"
#include "convert.h"
#include "restore.h"
#include "thread.h"
#include "timer.h"
#include "interface.h"
#include "hwmon.h"
#include "outfile.h"
#include "monitor.h"
#include "status.h"

static const char ST_0000[] = "Initializing";
static const char ST_0001[] = "Autotuning";
static const char ST_0002[] = "Running";
static const char ST_0003[] = "Paused";
static const char ST_0004[] = "Exhausted";
static const char ST_0005[] = "Cracked";
static const char ST_0006[] = "Aborted";
static const char ST_0007[] = "Quit";
static const char ST_0008[] = "Bypass";
static const char ST_9999[] = "Unknown! Bug!";

static char *status_get_rules_file (const hashcat_ctx_t *hashcat_ctx)
{
  const user_options_t *user_options = hashcat_ctx->user_options;

  if (user_options->rp_files_cnt > 0)
  {
    char *tmp_buf = (char *) malloc (HCBUFSIZ_TINY);

    int tmp_len = 0;

    u32 i;

    for (i = 0; i < user_options->rp_files_cnt - 1; i++)
    {
      tmp_len += snprintf (tmp_buf + tmp_len, HCBUFSIZ_TINY - tmp_len - 1, "%s, ", user_options->rp_files[i]);
    }

    tmp_len += snprintf (tmp_buf + tmp_len, HCBUFSIZ_TINY - tmp_len - 1, "%s", user_options->rp_files[i]);

    return tmp_buf; // yes, user need to free()
  }

  return NULL;
}

void format_timer_display (struct tm *tm, char *buf, size_t len)
{
  const char *time_entities_s[] = { "year",  "day",  "hour",  "min",  "sec"  };
  const char *time_entities_m[] = { "years", "days", "hours", "mins", "secs" };

  if (tm->tm_year - 70)
  {
    char *time_entity1 = ((tm->tm_year - 70) == 1) ? (char *) time_entities_s[0] : (char *) time_entities_m[0];
    char *time_entity2 = ( tm->tm_yday       == 1) ? (char *) time_entities_s[1] : (char *) time_entities_m[1];

    snprintf (buf, len - 1, "%d %s, %d %s", tm->tm_year - 70, time_entity1, tm->tm_yday, time_entity2);
  }
  else if (tm->tm_yday)
  {
    char *time_entity1 = (tm->tm_yday == 1) ? (char *) time_entities_s[1] : (char *) time_entities_m[1];
    char *time_entity2 = (tm->tm_hour == 1) ? (char *) time_entities_s[2] : (char *) time_entities_m[2];

    snprintf (buf, len - 1, "%d %s, %d %s", tm->tm_yday, time_entity1, tm->tm_hour, time_entity2);
  }
  else if (tm->tm_hour)
  {
    char *time_entity1 = (tm->tm_hour == 1) ? (char *) time_entities_s[2] : (char *) time_entities_m[2];
    char *time_entity2 = (tm->tm_min  == 1) ? (char *) time_entities_s[3] : (char *) time_entities_m[3];

    snprintf (buf, len - 1, "%d %s, %d %s", tm->tm_hour, time_entity1, tm->tm_min, time_entity2);
  }
  else if (tm->tm_min)
  {
    char *time_entity1 = (tm->tm_min == 1) ? (char *) time_entities_s[3] : (char *) time_entities_m[3];
    char *time_entity2 = (tm->tm_sec == 1) ? (char *) time_entities_s[4] : (char *) time_entities_m[4];

    snprintf (buf, len - 1, "%d %s, %d %s", tm->tm_min, time_entity1, tm->tm_sec, time_entity2);
  }
  else
  {
    char *time_entity1 = (tm->tm_sec == 1) ? (char *) time_entities_s[4] : (char *) time_entities_m[4];

    snprintf (buf, len - 1, "%d %s", tm->tm_sec, time_entity1);
  }
}

void format_speed_display (double val, char *buf, size_t len)
{
  if (val <= 0)
  {
    buf[0] = '0';
    buf[1] = ' ';
    buf[2] = 0;

    return;
  }

  char units[7] = { ' ', 'k', 'M', 'G', 'T', 'P', 'E' };

  u32 level = 0;

  while (val > 99999)
  {
    val /= 1000;

    level++;
  }

  /* generate output */

  if (level == 0)
  {
    snprintf (buf, len - 1, "%.0f ", val);
  }
  else
  {
    snprintf (buf, len - 1, "%.1f %c", val, units[level]);
  }
}

double get_avg_exec_time (hc_device_param_t *device_param, const int last_num_entries)
{
  int exec_pos = (int) device_param->exec_pos - last_num_entries;

  if (exec_pos < 0) exec_pos += EXEC_CACHE;

  double exec_msec_sum = 0;

  int exec_msec_cnt = 0;

  for (int i = 0; i < last_num_entries; i++)
  {
    double exec_msec = device_param->exec_msec[(exec_pos + i) % EXEC_CACHE];

    if (exec_msec > 0)
    {
      exec_msec_sum += exec_msec;

      exec_msec_cnt++;
    }
  }

  if (exec_msec_cnt == 0) return 0;

  return exec_msec_sum / exec_msec_cnt;
}

int status_get_device_info_cnt (const hashcat_ctx_t *hashcat_ctx)
{
  const opencl_ctx_t *opencl_ctx = hashcat_ctx->opencl_ctx;

  return opencl_ctx->devices_cnt;
}

int status_get_device_info_active (const hashcat_ctx_t *hashcat_ctx)
{
  const opencl_ctx_t *opencl_ctx = hashcat_ctx->opencl_ctx;

  return opencl_ctx->devices_active;
}

bool status_get_skipped_dev (const hashcat_ctx_t *hashcat_ctx, const int device_id)
{
  const opencl_ctx_t *opencl_ctx = hashcat_ctx->opencl_ctx;

  hc_device_param_t *device_param = &opencl_ctx->devices_param[device_id];

  return device_param->skipped;
}

char *status_get_session (const hashcat_ctx_t *hashcat_ctx)
{
  const user_options_t *user_options = hashcat_ctx->user_options;

  return user_options->session;
}

char *status_get_status_string (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  const int devices_status = status_ctx->devices_status;

  switch (devices_status)
  {
    case STATUS_INIT:      return ((char *) ST_0000);
    case STATUS_AUTOTUNE:  return ((char *) ST_0001);
    case STATUS_RUNNING:   return ((char *) ST_0002);
    case STATUS_PAUSED:    return ((char *) ST_0003);
    case STATUS_EXHAUSTED: return ((char *) ST_0004);
    case STATUS_CRACKED:   return ((char *) ST_0005);
    case STATUS_ABORTED:   return ((char *) ST_0006);
    case STATUS_QUIT:      return ((char *) ST_0007);
    case STATUS_BYPASS:    return ((char *) ST_0008);
  }

  return ((char *) ST_9999);
}

int status_get_status_number (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  return status_ctx->devices_status;
}

char *status_get_hash_type (const hashcat_ctx_t *hashcat_ctx)
{
  const hashconfig_t *hashconfig = hashcat_ctx->hashconfig;

  return strhashtype (hashconfig->hash_mode);
}

char *status_get_hash_target (const hashcat_ctx_t *hashcat_ctx)
{
  const hashconfig_t *hashconfig = hashcat_ctx->hashconfig;
  const hashes_t     *hashes     = hashcat_ctx->hashes;

  if (hashes->digests_cnt == 1)
  {
    if (hashconfig->hash_mode == 2500)
    {
      char *tmp_buf = (char *) malloc (HCBUFSIZ_TINY);

      wpa_t *wpa = (wpa_t *) hashes->esalts_buf;

      snprintf (tmp_buf, HCBUFSIZ_TINY - 1, "%s (%02x:%02x:%02x:%02x:%02x:%02x <-> %02x:%02x:%02x:%02x:%02x:%02x)",
        (char *) hashes->salts_buf[0].salt_buf,
        wpa->orig_mac1[0],
        wpa->orig_mac1[1],
        wpa->orig_mac1[2],
        wpa->orig_mac1[3],
        wpa->orig_mac1[4],
        wpa->orig_mac1[5],
        wpa->orig_mac2[0],
        wpa->orig_mac2[1],
        wpa->orig_mac2[2],
        wpa->orig_mac2[3],
        wpa->orig_mac2[4],
        wpa->orig_mac2[5]);

      return tmp_buf;
    }
    else if (hashconfig->hash_mode == 5200)
    {
      return hashes->hashfile;
    }
    else if (hashconfig->hash_mode == 9000)
    {
      return hashes->hashfile;
    }
    else if ((hashconfig->hash_mode >= 6200) && (hashconfig->hash_mode <= 6299))
    {
      return hashes->hashfile;
    }
    else if ((hashconfig->hash_mode >= 13700) && (hashconfig->hash_mode <= 13799))
    {
      return hashes->hashfile;
    }
    else
    {
      char *tmp_buf = (char *) malloc (HCBUFSIZ_LARGE);

      tmp_buf[0] = 0;

      ascii_digest ((hashcat_ctx_t *) hashcat_ctx, tmp_buf, 0, 0);

      char *tmp_buf2 = strdup (tmp_buf);

      free (tmp_buf);

      return tmp_buf2;
    }
  }
  else
  {
    if (hashconfig->hash_mode == 3000)
    {
      char *tmp_buf = (char *) malloc (HCBUFSIZ_TINY);

      char out_buf1[32] = { 0 };
      char out_buf2[32] = { 0 };

      ascii_digest ((hashcat_ctx_t *) hashcat_ctx, out_buf1, 0, 0);
      ascii_digest ((hashcat_ctx_t *) hashcat_ctx, out_buf2, 0, 1);

      snprintf (tmp_buf, HCBUFSIZ_TINY - 1, "%s, %s", out_buf1, out_buf2);

      return tmp_buf;
    }
    else
    {
      return hashes->hashfile;
    }
  }

  return NULL;
}

int status_get_input_mode (const hashcat_ctx_t *hashcat_ctx)
{
  const combinator_ctx_t     *combinator_ctx     = hashcat_ctx->combinator_ctx;
  const user_options_t       *user_options       = hashcat_ctx->user_options;
  const user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  bool has_wordlist   = false;
  bool has_rule_file  = false;
  bool has_rule_gen   = false;
  bool has_base_left  = false;
  bool has_mask_cs    = false;

  if (user_options_extra->wordlist_mode == WL_MODE_FILE) has_wordlist = true;

  if (user_options->rp_files_cnt > 0) has_rule_file = true;
  if (user_options->rp_gen       > 0) has_rule_gen  = true;

  if (combinator_ctx->combs_mode == COMBINATOR_MODE_BASE_LEFT) has_base_left = true;

  if (user_options->custom_charset_1) has_mask_cs = true;
  if (user_options->custom_charset_2) has_mask_cs = true;
  if (user_options->custom_charset_3) has_mask_cs = true;
  if (user_options->custom_charset_4) has_mask_cs = true;

  if (user_options->attack_mode == ATTACK_MODE_STRAIGHT)
  {
    if (has_wordlist == true)
    {
      if (has_rule_file == true)
      {
        return INPUT_MODE_STRAIGHT_FILE_RULES_FILE;
      }
      else if (has_rule_gen == true)
      {
        return INPUT_MODE_STRAIGHT_FILE_RULES_GEN;
      }
      else
      {
        return INPUT_MODE_STRAIGHT_FILE;
      }
    }
    else
    {
      if (has_rule_file == true)
      {
        return INPUT_MODE_STRAIGHT_STDIN_RULES_FILE;
      }
      else if (has_rule_gen == true)
      {
        return INPUT_MODE_STRAIGHT_STDIN_RULES_GEN;
      }
      else
      {
        return INPUT_MODE_STRAIGHT_STDIN;
      }
    }
  }
  else if (user_options->attack_mode == ATTACK_MODE_COMBI)
  {
    if (has_base_left == true)
    {
      return INPUT_MODE_COMBINATOR_BASE_LEFT;
    }
    else
    {
      return INPUT_MODE_COMBINATOR_BASE_RIGHT;
    }
  }
  else if (user_options->attack_mode == ATTACK_MODE_BF)
  {
    if (has_mask_cs == true)
    {
      return INPUT_MODE_MASK_CS;
    }
    else
    {
      return INPUT_MODE_MASK;
    }
  }
  else if (user_options->attack_mode == ATTACK_MODE_HYBRID1)
  {
    if (has_mask_cs == true)
    {
      return INPUT_MODE_HYBRID1_CS;
    }
    else
    {
      return INPUT_MODE_HYBRID1;
    }
  }
  else if (user_options->attack_mode == ATTACK_MODE_HYBRID2)
  {
    if (has_mask_cs == true)
    {
      return INPUT_MODE_HYBRID2_CS;
    }
    else
    {
      return INPUT_MODE_HYBRID2;
    }
  }

  return INPUT_MODE_NONE;
}

char *status_get_input_base (const hashcat_ctx_t *hashcat_ctx)
{
  const user_options_t *user_options = hashcat_ctx->user_options;

  if (user_options->attack_mode == ATTACK_MODE_STRAIGHT)
  {
    const straight_ctx_t *straight_ctx = hashcat_ctx->straight_ctx;

    return straight_ctx->dict;
  }
  else if (user_options->attack_mode == ATTACK_MODE_COMBI)
  {
    const combinator_ctx_t *combinator_ctx = hashcat_ctx->combinator_ctx;

    if (combinator_ctx->combs_mode == INPUT_MODE_COMBINATOR_BASE_LEFT)
    {
      return combinator_ctx->dict1;
    }
    else
    {
      return combinator_ctx->dict2;
    }
  }
  else if (user_options->attack_mode == ATTACK_MODE_BF)
  {
    const mask_ctx_t *mask_ctx = hashcat_ctx->mask_ctx;

    return mask_ctx->mask;
  }
  else if (user_options->attack_mode == ATTACK_MODE_HYBRID1)
  {
    const straight_ctx_t *straight_ctx = hashcat_ctx->straight_ctx;

    return straight_ctx->dict;
  }
  else if (user_options->attack_mode == ATTACK_MODE_HYBRID2)
  {
    const straight_ctx_t *straight_ctx = hashcat_ctx->straight_ctx;

    return straight_ctx->dict;
  }

  return NULL;
}

char *status_get_input_mod (const hashcat_ctx_t *hashcat_ctx)
{
  const user_options_t *user_options = hashcat_ctx->user_options;

  if (user_options->attack_mode == ATTACK_MODE_STRAIGHT)
  {
    return status_get_rules_file (hashcat_ctx);
  }
  else if (user_options->attack_mode == ATTACK_MODE_COMBI)
  {
    const combinator_ctx_t *combinator_ctx = hashcat_ctx->combinator_ctx;

    if (combinator_ctx->combs_mode == INPUT_MODE_COMBINATOR_BASE_LEFT)
    {
      return combinator_ctx->dict2;
    }
    else
    {
      return combinator_ctx->dict1;
    }
  }
  else if (user_options->attack_mode == ATTACK_MODE_BF)
  {

  }
  else if (user_options->attack_mode == ATTACK_MODE_HYBRID1)
  {
    const mask_ctx_t *mask_ctx = hashcat_ctx->mask_ctx;

    return mask_ctx->mask;
  }
  else if (user_options->attack_mode == ATTACK_MODE_HYBRID2)
  {
    const mask_ctx_t *mask_ctx = hashcat_ctx->mask_ctx;

    return mask_ctx->mask;
  }

  return NULL;
}

char *status_get_input_charset (const hashcat_ctx_t *hashcat_ctx)
{
  const user_options_t *user_options = hashcat_ctx->user_options;

  const char *custom_charset_1 = user_options->custom_charset_1;
  const char *custom_charset_2 = user_options->custom_charset_2;
  const char *custom_charset_3 = user_options->custom_charset_3;
  const char *custom_charset_4 = user_options->custom_charset_4;

  if ((custom_charset_1 != NULL) || (custom_charset_2 != NULL) || (custom_charset_3 != NULL) || (custom_charset_4 != NULL))
  {
    char *tmp_buf = (char *) malloc (HCBUFSIZ_TINY);

    if (custom_charset_1 == NULL) custom_charset_1 = "Undefined";
    if (custom_charset_2 == NULL) custom_charset_2 = "Undefined";
    if (custom_charset_3 == NULL) custom_charset_3 = "Undefined";
    if (custom_charset_4 == NULL) custom_charset_4 = "Undefined";

    snprintf (tmp_buf, HCBUFSIZ_TINY - 1, "-1 %s, -2 %s, -3 %s, -4 %s", custom_charset_1, custom_charset_2, custom_charset_3, custom_charset_4);

    return tmp_buf;
  }

  return NULL;
}

char *status_get_input_candidates_dev (const hashcat_ctx_t *hashcat_ctx, const int device_id)
{
  const opencl_ctx_t         *opencl_ctx         = hashcat_ctx->opencl_ctx;
  const status_ctx_t         *status_ctx         = hashcat_ctx->status_ctx;
  const user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  if (status_ctx->devices_status == STATUS_INIT)     return NULL;
  if (status_ctx->devices_status == STATUS_AUTOTUNE) return NULL;
  if (status_ctx->devices_status == STATUS_AUTOTUNE) return NULL;

  hc_device_param_t *device_param = &opencl_ctx->devices_param[device_id];

  char *display = (char *) malloc (HCBUFSIZ_TINY);

  if (user_options_extra->attack_kern == ATTACK_KERN_BF)
  {
    snprintf (display, HCBUFSIZ_TINY - 1, "[Generating]");
  }
  else
  {
    snprintf (display, HCBUFSIZ_TINY - 1, "[Copying]");
  }

  if (device_param->skipped == true) return display;

  if ((device_param->outerloop_left == 0) || (device_param->innerloop_left == 0)) return display;

  const u32 outerloop_first = 0;
  const u32 outerloop_last  = device_param->outerloop_left - 1;

  const u32 innerloop_first = 0;
  const u32 innerloop_last  = device_param->innerloop_left - 1;

  plain_t plain1 = { 0, 0, 0, outerloop_first, innerloop_first };
  plain_t plain2 = { 0, 0, 0, outerloop_last,  innerloop_last  };

  u32 plain_buf1[16] = { 0 };
  u32 plain_buf2[16] = { 0 };

  u8 *plain_ptr1 = (u8 *) plain_buf1;
  u8 *plain_ptr2 = (u8 *) plain_buf2;

  int plain_len1 = 0;
  int plain_len2 = 0;

  build_plain ((hashcat_ctx_t *) hashcat_ctx, device_param, &plain1, plain_buf1, &plain_len1);
  build_plain ((hashcat_ctx_t *) hashcat_ctx, device_param, &plain2, plain_buf2, &plain_len2);

  const bool need_hex1 = need_hexify (plain_ptr1, plain_len1);
  const bool need_hex2 = need_hexify (plain_ptr2, plain_len2);

  if ((need_hex1 == true) || (need_hex2 == true))
  {
    exec_hexify (plain_ptr1, plain_len1, plain_ptr1);
    exec_hexify (plain_ptr2, plain_len2, plain_ptr2);

    plain_ptr1[plain_len1 * 2] = 0;
    plain_ptr2[plain_len2 * 2] = 0;

    snprintf (display, HCBUFSIZ_TINY - 1, "$HEX[%s] -> $HEX[%s]", plain_ptr1, plain_ptr2);
  }
  else
  {
    snprintf (display, HCBUFSIZ_TINY - 1, "%s -> %s", plain_ptr1, plain_ptr2);
  }

  return display;
}

int status_get_digests_done (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t *hashes = hashcat_ctx->hashes;

  return hashes->digests_done;
}

int status_get_digests_cnt (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t *hashes = hashcat_ctx->hashes;

  return hashes->digests_cnt;
}

double status_get_digests_percent (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t *hashes = hashcat_ctx->hashes;

  return ((double) hashes->digests_done / (double) hashes->digests_cnt) * 100;
}

int status_get_salts_done (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t *hashes = hashcat_ctx->hashes;

  return hashes->salts_done;
}

int status_get_salts_cnt (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t *hashes = hashcat_ctx->hashes;

  return hashes->salts_cnt;
}

double status_get_salts_percent (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t *hashes = hashcat_ctx->hashes;

  return ((double) hashes->salts_done / (double) hashes->salts_cnt) * 100;
}

double status_get_msec_running (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  double msec_running = hc_timer_get (status_ctx->timer_running);

  return msec_running;
}

double status_get_msec_paused (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  double msec_paused = status_ctx->msec_paused;

  if (status_ctx->devices_status == STATUS_PAUSED)
  {
    double msec_paused_tmp = hc_timer_get (status_ctx->timer_paused);

    msec_paused += msec_paused_tmp;
  }

  return msec_paused;
}

double status_get_msec_real (const hashcat_ctx_t *hashcat_ctx)
{
  const double msec_running = status_get_msec_running (hashcat_ctx);
  const double msec_paused  = status_get_msec_paused  (hashcat_ctx);

  const double msec_real = msec_running - msec_paused;

  return msec_real;
}

char *status_get_time_started_absolute (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  const time_t time_start = status_ctx->runtime_start;

  char *start = ctime (&time_start);

  const size_t start_len = strlen (start);

  if (start[start_len - 1] == '\n') start[start_len - 1] = 0;
  if (start[start_len - 2] == '\r') start[start_len - 2] = 0;

  return start;
}

char *status_get_time_started_relative (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  time_t time_now;

  time (&time_now);

  const time_t time_start = status_ctx->runtime_start;

  #if defined (_WIN)

  __time64_t sec_run = time_now - time_start;

  #else

  time_t sec_run = time_now - time_start;

  #endif

  struct tm *tmp;

  #if defined (_WIN)

  tmp = _gmtime64 (&sec_run);

  #else

  tmp = gmtime (&sec_run);

  #endif

  char *display_run = (char *) malloc (HCBUFSIZ_TINY);

  format_timer_display (tmp, display_run, HCBUFSIZ_TINY);

  return display_run;
}

char *status_get_time_estimated_absolute (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t         *status_ctx         = hashcat_ctx->status_ctx;
  const user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  #if defined (_WIN)
  __time64_t sec_etc = 0;
  #else
  time_t sec_etc = 0;
  #endif

  if ((user_options_extra->wordlist_mode == WL_MODE_FILE) || (user_options_extra->wordlist_mode == WL_MODE_MASK))
  {
    if (status_ctx->devices_status != STATUS_CRACKED)
    {
      const u64 progress_cur_relative_skip = status_get_progress_cur_relative_skip (hashcat_ctx);
      const u64 progress_end_relative_skip = status_get_progress_end_relative_skip (hashcat_ctx);

      const u64 progress_ignore = status_get_progress_ignore (hashcat_ctx);

      const double hashes_msec_all = status_get_hashes_msec_all (hashcat_ctx);

      if (hashes_msec_all > 0)
      {
        const u64 progress_left_relative_skip = progress_end_relative_skip - progress_cur_relative_skip;

        u64 msec_left = (u64) ((progress_left_relative_skip - progress_ignore) / hashes_msec_all);

        sec_etc = msec_left / 1000;
      }
    }
  }

  // we need this check to avoid integer overflow
  #if defined (_WIN)
  if (sec_etc > 100000000)
  {
    sec_etc = 100000000;
  }
  #endif

  time_t now;

  time (&now);

  now += sec_etc;

  char *etc = ctime (&now);

  const size_t etc_len = strlen (etc);

  if (etc[etc_len - 1] == '\n') etc[etc_len - 1] = 0;
  if (etc[etc_len - 2] == '\r') etc[etc_len - 2] = 0;

  return etc;
}

char *status_get_time_estimated_relative (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t         *status_ctx         = hashcat_ctx->status_ctx;
  const user_options_t       *user_options       = hashcat_ctx->user_options;
  const user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  #if defined (_WIN)
  __time64_t sec_etc = 0;
  #else
  time_t sec_etc = 0;
  #endif

  if ((user_options_extra->wordlist_mode == WL_MODE_FILE) || (user_options_extra->wordlist_mode == WL_MODE_MASK))
  {
    if (status_ctx->devices_status != STATUS_CRACKED)
    {
      const u64 progress_cur_relative_skip = status_get_progress_cur_relative_skip (hashcat_ctx);
      const u64 progress_end_relative_skip = status_get_progress_end_relative_skip (hashcat_ctx);

      const u64 progress_ignore = status_get_progress_ignore (hashcat_ctx);

      const double hashes_msec_all = status_get_hashes_msec_all (hashcat_ctx);

      if (hashes_msec_all > 0)
      {
        const u64 progress_left_relative_skip = progress_end_relative_skip - progress_cur_relative_skip;

        u64 msec_left = (u64) ((progress_left_relative_skip - progress_ignore) / hashes_msec_all);

        sec_etc = msec_left / 1000;
      }
    }
  }

  // we need this check to avoid integer overflow
  #if defined (_WIN)
  if (sec_etc > 100000000)
  {
    sec_etc = 100000000;
  }
  #endif

  struct tm *tmp;

  #if defined (_WIN)
  tmp = _gmtime64 (&sec_etc);
  #else
  tmp = gmtime (&sec_etc);
  #endif

  char *display = (char *) malloc (HCBUFSIZ_TINY);

  format_timer_display (tmp, display, HCBUFSIZ_TINY);

  if (user_options->runtime > 0)
  {
    const int runtime_left = get_runtime_left (hashcat_ctx);

    char *tmp = strdup (display);

    if (runtime_left > 0)
    {
      #if defined (_WIN)
      __time64_t sec_left = runtime_left;
      #else
      time_t sec_left = runtime_left;
      #endif

      struct tm *tmp_left;

      #if defined (_WIN)
      tmp_left = _gmtime64 (&sec_left);
      #else
      tmp_left = gmtime (&sec_left);
      #endif

      char *display_left = (char *) malloc (HCBUFSIZ_TINY);

      format_timer_display (tmp_left, display_left, HCBUFSIZ_TINY);

      snprintf (display, HCBUFSIZ_TINY - 1, "%s; Runtime limited: %s", tmp, display_left);

      free (display_left);
    }
    else
    {
      snprintf (display, HCBUFSIZ_TINY - 1, "%s; Runtime limit exceeded", tmp);
    }

    free (tmp);
  }

  return display;
}

u64 status_get_restore_point (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  const u64 restore_point = status_ctx->words_cur;

  return restore_point;
}

u64 status_get_restore_total (const hashcat_ctx_t *hashcat_ctx)
{
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  const u64 restore_total = status_ctx->words_base;

  return restore_total;
}

double status_get_restore_percent (const hashcat_ctx_t *hashcat_ctx)
{
  double restore_percent = 0;

  const u64 restore_point = status_get_restore_point (hashcat_ctx);
  const u64 restore_total = status_get_restore_total (hashcat_ctx);

  if (restore_total > 0)
  {
    restore_percent = ((double) restore_point / (double) restore_total) * 100;
  }

  return restore_percent;
}

int status_get_progress_mode (const hashcat_ctx_t *hashcat_ctx)
{
  const u64 progress_end_relative_skip = status_get_progress_end_relative_skip (hashcat_ctx);

  if (progress_end_relative_skip > 0)
  {
    return PROGRESS_MODE_KEYSPACE_KNOWN;
  }
  else
  {
    return PROGRESS_MODE_KEYSPACE_UNKNOWN;
  }

  return PROGRESS_MODE_NONE;
}

double status_get_progress_finished_percent (const hashcat_ctx_t *hashcat_ctx)
{
  const u64 progress_cur_relative_skip = status_get_progress_cur_relative_skip (hashcat_ctx);
  const u64 progress_end_relative_skip = status_get_progress_end_relative_skip (hashcat_ctx);

  double progress_finished_percent = 0;

  if (progress_end_relative_skip > 0)
  {
    progress_finished_percent = ((double) progress_cur_relative_skip / (double) progress_end_relative_skip) * 100;
  }

  return progress_finished_percent;
}

u64 status_get_progress_done (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t     *hashes     = hashcat_ctx->hashes;
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  u64 progress_done = 0;

  for (u32 salt_pos = 0; salt_pos < hashes->salts_cnt; salt_pos++)
  {
    progress_done += status_ctx->words_progress_done[salt_pos];
  }

  return progress_done;
}

u64 status_get_progress_rejected (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t     *hashes     = hashcat_ctx->hashes;
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  u64 progress_rejected = 0;

  for (u32 salt_pos = 0; salt_pos < hashes->salts_cnt; salt_pos++)
  {
    progress_rejected += status_ctx->words_progress_rejected[salt_pos];
  }

  return progress_rejected;
}

double status_get_progress_rejected_percent (const hashcat_ctx_t *hashcat_ctx)
{
  const u64 progress_cur      = status_get_progress_cur      (hashcat_ctx);
  const u64 progress_rejected = status_get_progress_rejected (hashcat_ctx);

  double percent_rejected = 0;

  if (progress_cur)
  {
    percent_rejected = ((double) (progress_rejected) / (double) progress_cur) * 100;
  }

  return percent_rejected;
}

u64 status_get_progress_restored (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t     *hashes     = hashcat_ctx->hashes;
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  u64 progress_restored = 0;

  for (u32 salt_pos = 0; salt_pos < hashes->salts_cnt; salt_pos++)
  {
    progress_restored += status_ctx->words_progress_restored[salt_pos];
  }

  return progress_restored;
}

u64 status_get_progress_cur (const hashcat_ctx_t *hashcat_ctx)
{
  const u64 progress_done     = status_get_progress_done     (hashcat_ctx);
  const u64 progress_rejected = status_get_progress_rejected (hashcat_ctx);
  const u64 progress_restored = status_get_progress_restored (hashcat_ctx);

  const u64 progress_cur = progress_done + progress_rejected + progress_restored;

  return progress_cur;
}

u64 status_get_progress_ignore (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t     *hashes     = hashcat_ctx->hashes;
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  // Important for ETA only

  u64 progress_ignore = 0;

  for (u32 salt_pos = 0; salt_pos < hashes->salts_cnt; salt_pos++)
  {
    if (hashes->salts_shown[salt_pos] == 1)
    {
      const u64 all = status_ctx->words_progress_done[salt_pos]
                    + status_ctx->words_progress_rejected[salt_pos]
                    + status_ctx->words_progress_restored[salt_pos];

      const u64 left = status_ctx->words_cnt - all;

      progress_ignore += left;
    }
  }

  return progress_ignore;
}

u64 status_get_progress_end (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t             *hashes             = hashcat_ctx->hashes;
  const status_ctx_t         *status_ctx         = hashcat_ctx->status_ctx;
  const user_options_t       *user_options       = hashcat_ctx->user_options;
  const user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  u64 progress_end = status_ctx->words_cnt * hashes->salts_cnt;

  if (user_options->limit)
  {
    const combinator_ctx_t *combinator_ctx = hashcat_ctx->combinator_ctx;
    const mask_ctx_t       *mask_ctx       = hashcat_ctx->mask_ctx;
    const straight_ctx_t   *straight_ctx   = hashcat_ctx->straight_ctx;

    progress_end = MIN (user_options->limit, status_ctx->words_base) * hashes->salts_cnt;

    if      (user_options_extra->attack_kern == ATTACK_KERN_STRAIGHT) progress_end  *= straight_ctx->kernel_rules_cnt;
    else if (user_options_extra->attack_kern == ATTACK_KERN_COMBI)    progress_end  *= combinator_ctx->combs_cnt;
    else if (user_options_extra->attack_kern == ATTACK_KERN_BF)       progress_end  *= mask_ctx->bfs_cnt;
  }

  return progress_end;
}

u64 status_get_progress_skip (const hashcat_ctx_t *hashcat_ctx)
{
  const hashes_t             *hashes             = hashcat_ctx->hashes;
  const status_ctx_t         *status_ctx         = hashcat_ctx->status_ctx;
  const user_options_t       *user_options       = hashcat_ctx->user_options;
  const user_options_extra_t *user_options_extra = hashcat_ctx->user_options_extra;

  u64 progress_skip = 0;

  if (user_options->skip)
  {
    const combinator_ctx_t *combinator_ctx = hashcat_ctx->combinator_ctx;
    const mask_ctx_t       *mask_ctx       = hashcat_ctx->mask_ctx;
    const straight_ctx_t   *straight_ctx   = hashcat_ctx->straight_ctx;

    progress_skip = MIN (user_options->skip, status_ctx->words_base) * hashes->salts_cnt;

    if      (user_options_extra->attack_kern == ATTACK_KERN_STRAIGHT) progress_skip *= straight_ctx->kernel_rules_cnt;
    else if (user_options_extra->attack_kern == ATTACK_KERN_COMBI)    progress_skip *= combinator_ctx->combs_cnt;
    else if (user_options_extra->attack_kern == ATTACK_KERN_BF)       progress_skip *= mask_ctx->bfs_cnt;
  }

  return progress_skip;
}

u64 status_get_progress_cur_relative_skip (const hashcat_ctx_t *hashcat_ctx)
{
  const u64 progress_skip = status_get_progress_skip (hashcat_ctx);
  const u64 progress_cur  = status_get_progress_cur  (hashcat_ctx);

  u64 progress_cur_relative_skip = 0;

  if (progress_cur > 0)
  {
    progress_cur_relative_skip = progress_cur - progress_skip;
  }

  return progress_cur_relative_skip;
}

u64 status_get_progress_end_relative_skip (const hashcat_ctx_t *hashcat_ctx)
{
  const u64 progress_skip = status_get_progress_skip (hashcat_ctx);
  const u64 progress_end  = status_get_progress_end  (hashcat_ctx);

  u64 progress_end_relative_skip = 0;

  if (progress_end > 0)
  {
    progress_end_relative_skip = progress_end - progress_skip;
  }

  return progress_end_relative_skip;
}

double status_get_hashes_msec_all (const hashcat_ctx_t *hashcat_ctx)
{
  const opencl_ctx_t *opencl_ctx = hashcat_ctx->opencl_ctx;

  double hashes_all_msec = 0;

  for (u32 device_id = 0; device_id < opencl_ctx->devices_cnt; device_id++)
  {
    hashes_all_msec += status_get_hashes_msec_dev (hashcat_ctx, device_id);
  }

  return hashes_all_msec;
}

double status_get_hashes_msec_dev (const hashcat_ctx_t *hashcat_ctx, const int device_id)
{
  const opencl_ctx_t *opencl_ctx = hashcat_ctx->opencl_ctx;

  u64    speed_cnt  = 0;
  double speed_msec = 0;

  hc_device_param_t *device_param = &opencl_ctx->devices_param[device_id];

  if (device_param->skipped == false)
  {
    for (int i = 0; i < SPEED_CACHE; i++)
    {
      speed_cnt  += device_param->speed_cnt[i];
      speed_msec += device_param->speed_msec[i];
    }
  }

  speed_cnt  /= SPEED_CACHE;
  speed_msec /= SPEED_CACHE;

  double hashes_dev_msec = 0;

  if (speed_msec > 0)
  {
    hashes_dev_msec = (double) speed_cnt / speed_msec;
  }

  return hashes_dev_msec;
}

double status_get_hashes_msec_dev_benchmark (const hashcat_ctx_t *hashcat_ctx, const int device_id)
{
  // this function increases accuracy for benchmark modes

  const opencl_ctx_t *opencl_ctx = hashcat_ctx->opencl_ctx;

  u64    speed_cnt  = 0;
  double speed_msec = 0;

  hc_device_param_t *device_param = &opencl_ctx->devices_param[device_id];

  if (device_param->skipped == false)
  {
    speed_cnt  += device_param->speed_cnt[0];
    speed_msec += device_param->speed_msec[0];
  }

  double hashes_dev_msec = 0;

  if (speed_msec > 0)
  {
    hashes_dev_msec = (double) speed_cnt / speed_msec;
  }

  return hashes_dev_msec;
}

double status_get_exec_msec_all (const hashcat_ctx_t *hashcat_ctx)
{
  const opencl_ctx_t *opencl_ctx = hashcat_ctx->opencl_ctx;

  double exec_all_msec = 0;

  for (u32 device_id = 0; device_id < opencl_ctx->devices_cnt; device_id++)
  {
    exec_all_msec += status_get_exec_msec_dev (hashcat_ctx, device_id);
  }

  return exec_all_msec;
}

double status_get_exec_msec_dev (const hashcat_ctx_t *hashcat_ctx, const int device_id)
{
  const opencl_ctx_t *opencl_ctx = hashcat_ctx->opencl_ctx;

  hc_device_param_t *device_param = &opencl_ctx->devices_param[device_id];

  double exec_dev_msec = 0;

  if (device_param->skipped == false)
  {
    exec_dev_msec = get_avg_exec_time (device_param, EXEC_CACHE);
  }

  return exec_dev_msec;
}

char *status_get_speed_sec_all (const hashcat_ctx_t *hashcat_ctx)
{
  const double hashes_msec_all = status_get_hashes_msec_all (hashcat_ctx);

  char *display = (char *) malloc (HCBUFSIZ_TINY);

  format_speed_display (hashes_msec_all * 1000, display, HCBUFSIZ_TINY);

  return display;
}

char *status_get_speed_sec_dev (const hashcat_ctx_t *hashcat_ctx, const int device_id)
{
  const double hashes_msec_dev = status_get_hashes_msec_dev (hashcat_ctx, device_id);

  char *display = (char *) malloc (HCBUFSIZ_TINY);

  format_speed_display (hashes_msec_dev * 1000, display, HCBUFSIZ_TINY);

  return display;
}

int status_get_cpt_cur_min (const hashcat_ctx_t *hashcat_ctx)
{
  const cpt_ctx_t    *cpt_ctx    = hashcat_ctx->cpt_ctx;
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  if (status_ctx->devices_status != STATUS_RUNNING) return 0;

  const time_t now = time (NULL);

  int cpt_cur_min = 0;

  for (int i = 0; i < CPT_BUF; i++)
  {
    const u32    cracked   = cpt_ctx->cpt_buf[i].cracked;
    const time_t timestamp = cpt_ctx->cpt_buf[i].timestamp;

    if ((timestamp + 60) > now)
    {
      cpt_cur_min += cracked;
    }
  }

  return cpt_cur_min;
}

int status_get_cpt_cur_hour (const hashcat_ctx_t *hashcat_ctx)
{
  const cpt_ctx_t    *cpt_ctx    = hashcat_ctx->cpt_ctx;
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  if (status_ctx->devices_status != STATUS_RUNNING) return 0;

  const time_t now = time (NULL);

  int cpt_cur_hour = 0;

  for (int i = 0; i < CPT_BUF; i++)
  {
    const u32    cracked   = cpt_ctx->cpt_buf[i].cracked;
    const time_t timestamp = cpt_ctx->cpt_buf[i].timestamp;

    if ((timestamp + 3600) > now)
    {
      cpt_cur_hour += cracked;
    }
  }

  return cpt_cur_hour;
}

int status_get_cpt_cur_day (const hashcat_ctx_t *hashcat_ctx)
{
  const cpt_ctx_t    *cpt_ctx    = hashcat_ctx->cpt_ctx;
  const status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  if (status_ctx->devices_status != STATUS_RUNNING) return 0;

  const time_t now = time (NULL);

  int cpt_cur_day = 0;

  for (int i = 0; i < CPT_BUF; i++)
  {
    const u32    cracked   = cpt_ctx->cpt_buf[i].cracked;
    const time_t timestamp = cpt_ctx->cpt_buf[i].timestamp;

    if ((timestamp + 86400) > now)
    {
      cpt_cur_day += cracked;
    }
  }

  return cpt_cur_day;
}

double status_get_cpt_avg_min (const hashcat_ctx_t *hashcat_ctx)
{
  const cpt_ctx_t *cpt_ctx = hashcat_ctx->cpt_ctx;

  const double msec_real = status_get_msec_real (hashcat_ctx);

  const double cpt_avg_min = (double) cpt_ctx->cpt_total / ((msec_real / 1000) / 60);

  return cpt_avg_min;
}

double status_get_cpt_avg_hour (const hashcat_ctx_t *hashcat_ctx)
{
  const cpt_ctx_t *cpt_ctx = hashcat_ctx->cpt_ctx;

  const double msec_real = status_get_msec_real (hashcat_ctx);

  const double cpt_avg_hour = (double) cpt_ctx->cpt_total / ((msec_real / 1000) / 3600);

  return cpt_avg_hour;
}

double status_get_cpt_avg_day (const hashcat_ctx_t *hashcat_ctx)
{
  const cpt_ctx_t *cpt_ctx = hashcat_ctx->cpt_ctx;

  const double msec_real = status_get_msec_real (hashcat_ctx);

  const double cpt_avg_day = (double) cpt_ctx->cpt_total / ((msec_real / 1000) / 86400);

  return cpt_avg_day;
}

char *status_get_cpt (const hashcat_ctx_t *hashcat_ctx)
{
  const cpt_ctx_t *cpt_ctx = hashcat_ctx->cpt_ctx;

  const time_t now = time (NULL);

  char *cpt = (char *) malloc (HCBUFSIZ_TINY);

  const int cpt_cur_min  = status_get_cpt_cur_min  (hashcat_ctx);
  const int cpt_cur_hour = status_get_cpt_cur_hour (hashcat_ctx);
  const int cpt_cur_day  = status_get_cpt_cur_day  (hashcat_ctx);

  const double cpt_avg_min  = status_get_cpt_avg_min  (hashcat_ctx);
  const double cpt_avg_hour = status_get_cpt_avg_hour (hashcat_ctx);
  const double cpt_avg_day  = status_get_cpt_avg_day  (hashcat_ctx);

  if ((cpt_ctx->cpt_start + 86400) < now)
  {
    snprintf (cpt, HCBUFSIZ_TINY - 1, "CUR:%u,%u,%u AVG:%0.2f,%0.2f,%0.2f (Min,Hour,Day)",
      cpt_cur_min,
      cpt_cur_hour,
      cpt_cur_day,
      cpt_avg_min,
      cpt_avg_hour,
      cpt_avg_day);
  }
  else if ((cpt_ctx->cpt_start + 3600) < now)
  {
    snprintf (cpt, HCBUFSIZ_TINY - 1, "CUR:%u,%u,N/A AVG:%0.2f,%0.2f,%0.2f (Min,Hour,Day)",
      cpt_cur_min,
      cpt_cur_hour,
      cpt_avg_min,
      cpt_avg_hour,
      cpt_avg_day);
  }
  else if ((cpt_ctx->cpt_start + 60) < now)
  {
    snprintf (cpt, HCBUFSIZ_TINY - 1, "CUR:%u,N/A,N/A AVG:%0.2f,%0.2f,%0.2f (Min,Hour,Day)",
      cpt_cur_min,
      cpt_avg_min,
      cpt_avg_hour,
      cpt_avg_day);
  }
  else
  {
    snprintf (cpt, HCBUFSIZ_TINY - 1, "CUR:N/A,N/A,N/A AVG:%0.2f,%0.2f,%0.2f (Min,Hour,Day)",
      cpt_avg_min,
      cpt_avg_hour,
      cpt_avg_day);
  }

  return cpt;
}

char *status_get_hwmon_dev (const hashcat_ctx_t *hashcat_ctx, const int device_id)
{
  const opencl_ctx_t *opencl_ctx = hashcat_ctx->opencl_ctx;

  hc_device_param_t *device_param = &opencl_ctx->devices_param[device_id];

  char *output_buf = (char *) malloc (HCBUFSIZ_TINY);

  snprintf (output_buf, HCBUFSIZ_TINY - 1, "N/A");

  if (device_param->skipped == true) return output_buf;

  status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  hc_thread_mutex_lock (status_ctx->mux_hwmon);

  const int num_temperature = hm_get_temperature_with_device_id ((hashcat_ctx_t *) hashcat_ctx, device_id);
  const int num_fanspeed    = hm_get_fanspeed_with_device_id    ((hashcat_ctx_t *) hashcat_ctx, device_id);
  const int num_utilization = hm_get_utilization_with_device_id ((hashcat_ctx_t *) hashcat_ctx, device_id);
  const int num_corespeed   = hm_get_corespeed_with_device_id   ((hashcat_ctx_t *) hashcat_ctx, device_id);
  const int num_memoryspeed = hm_get_memoryspeed_with_device_id ((hashcat_ctx_t *) hashcat_ctx, device_id);
  const int num_buslanes    = hm_get_buslanes_with_device_id    ((hashcat_ctx_t *) hashcat_ctx, device_id);
  const int num_throttle    = hm_get_throttle_with_device_id    ((hashcat_ctx_t *) hashcat_ctx, device_id);

  int output_len = 0;

  if (num_temperature >= 0)
  {
    snprintf (output_buf + output_len, HCBUFSIZ_TINY - output_len, "Temp:%3uc ", num_temperature);

    output_len = strlen (output_buf);
  }

  if (num_fanspeed >= 0)
  {
    snprintf (output_buf + output_len, HCBUFSIZ_TINY - output_len, "Fan:%3u%% ", num_fanspeed);

    output_len = strlen (output_buf);
  }

  if (num_utilization >= 0)
  {
    snprintf (output_buf + output_len, HCBUFSIZ_TINY - output_len, "Util:%3u%% ", num_utilization);

    output_len = strlen (output_buf);
  }

  if (num_corespeed >= 0)
  {
    snprintf (output_buf + output_len, HCBUFSIZ_TINY - output_len, "Core:%4uMhz ", num_corespeed);

    output_len = strlen (output_buf);
  }

  if (num_memoryspeed >= 0)
  {
    snprintf (output_buf + output_len, HCBUFSIZ_TINY - output_len, "Mem:%4uMhz ", num_memoryspeed);

    output_len = strlen (output_buf);
  }

  if (num_buslanes >= 0)
  {
    snprintf (output_buf + output_len, HCBUFSIZ_TINY - output_len, "Lanes:%u ", num_buslanes);

    output_len = strlen (output_buf);
  }

  if (num_throttle >= 0)
  {
    snprintf (output_buf + output_len, HCBUFSIZ_TINY - output_len, "*Throttled* ");

    output_len = strlen (output_buf);
  }

  if (output_len > 0)
  {
    output_buf[output_len - 1] = 0;
  }

  hc_thread_mutex_unlock (status_ctx->mux_hwmon);

  return output_buf;
}

int status_progress_init (hashcat_ctx_t *hashcat_ctx)
{
  status_ctx_t *status_ctx = hashcat_ctx->status_ctx;
  hashes_t     *hashes     = hashcat_ctx->hashes;

  status_ctx->words_progress_done     = (u64 *) hccalloc (hashcat_ctx, hashes->salts_cnt, sizeof (u64)); VERIFY_PTR (status_ctx->words_progress_done);
  status_ctx->words_progress_rejected = (u64 *) hccalloc (hashcat_ctx, hashes->salts_cnt, sizeof (u64)); VERIFY_PTR (status_ctx->words_progress_rejected);
  status_ctx->words_progress_restored = (u64 *) hccalloc (hashcat_ctx, hashes->salts_cnt, sizeof (u64)); VERIFY_PTR (status_ctx->words_progress_restored);

  return 0;
}

void status_progress_destroy (hashcat_ctx_t *hashcat_ctx)
{
  status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  hcfree (status_ctx->words_progress_done);
  hcfree (status_ctx->words_progress_rejected);
  hcfree (status_ctx->words_progress_restored);

  status_ctx->words_progress_done     = NULL;
  status_ctx->words_progress_rejected = NULL;
  status_ctx->words_progress_restored = NULL;
}

void status_progress_reset (hashcat_ctx_t *hashcat_ctx)
{
  status_ctx_t *status_ctx = hashcat_ctx->status_ctx;
  hashes_t     *hashes     = hashcat_ctx->hashes;

  memset (status_ctx->words_progress_done,     0, hashes->salts_cnt * sizeof (u64));
  memset (status_ctx->words_progress_rejected, 0, hashes->salts_cnt * sizeof (u64));
  memset (status_ctx->words_progress_restored, 0, hashes->salts_cnt * sizeof (u64));
}

int status_ctx_init (hashcat_ctx_t *hashcat_ctx)
{
  status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  status_ctx->devices_status = STATUS_INIT;

  status_ctx->run_main_level1   = true;
  status_ctx->run_main_level2   = true;
  status_ctx->run_main_level3   = true;
  status_ctx->run_thread_level1 = true;
  status_ctx->run_thread_level2 = true;

  status_ctx->checkpoint_shutdown = false;

  hc_thread_mutex_init (status_ctx->mux_dispatcher);
  hc_thread_mutex_init (status_ctx->mux_counter);
  hc_thread_mutex_init (status_ctx->mux_display);
  hc_thread_mutex_init (status_ctx->mux_hwmon);

  return 0;
}

void status_ctx_destroy (hashcat_ctx_t *hashcat_ctx)
{
  status_ctx_t *status_ctx = hashcat_ctx->status_ctx;

  hc_thread_mutex_delete (status_ctx->mux_dispatcher);
  hc_thread_mutex_delete (status_ctx->mux_counter);
  hc_thread_mutex_delete (status_ctx->mux_display);
  hc_thread_mutex_delete (status_ctx->mux_hwmon);

  memset (status_ctx, 0, sizeof (status_ctx_t));
}
