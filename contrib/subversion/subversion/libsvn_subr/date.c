/* date.c:  date parsing for Subversion
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "svn_time.h"
#include "svn_error.h"
#include "svn_string.h"

#include "svn_private_config.h"
#include "private/svn_token.h"

/* Valid rule actions */
enum rule_action {
  ACCUM,    /* Accumulate a decimal value */
  MICRO,    /* Accumulate microseconds */
  TZIND,    /* Handle +, -, Z */
  NOOP,     /* Do nothing */
  SKIPFROM, /* If at end-of-value, accept the match.  Otherwise,
               if the next template character matches the current
               value character, continue processing as normal.
               Otherwise, attempt to complete matching starting
               immediately after the first subsequent occurrance of
               ']' in the template. */
  SKIP,     /* Ignore this template character */
  ACCEPT    /* Accept the value */
};

/* How to handle a particular character in a template */
typedef struct rule
{
  char key;                /* The template char that this rule matches */
  const char *valid;       /* String of valid chars for this rule */
  enum rule_action action; /* What action to take when the rule is matched */
  int offset;              /* Where to store the any results of the action,
                              expressed in terms of bytes relative to the
                              base of a match_state object. */
} rule;

/* The parsed values, before localtime/gmt processing */
typedef struct match_state
{
  apr_time_exp_t base;
  apr_int32_t offhours;
  apr_int32_t offminutes;
} match_state;

#define DIGITS "0123456789"

/* A declarative specification of how each template character
   should be processed, using a rule for each valid symbol. */
static const rule
rules[] =
{
  { 'Y', DIGITS, ACCUM, APR_OFFSETOF(match_state, base.tm_year) },
  { 'M', DIGITS, ACCUM, APR_OFFSETOF(match_state, base.tm_mon) },
  { 'D', DIGITS, ACCUM, APR_OFFSETOF(match_state, base.tm_mday) },
  { 'h', DIGITS, ACCUM, APR_OFFSETOF(match_state, base.tm_hour) },
  { 'm', DIGITS, ACCUM, APR_OFFSETOF(match_state, base.tm_min) },
  { 's', DIGITS, ACCUM, APR_OFFSETOF(match_state, base.tm_sec) },
  { 'u', DIGITS, MICRO, APR_OFFSETOF(match_state, base.tm_usec) },
  { 'O', DIGITS, ACCUM, APR_OFFSETOF(match_state, offhours) },
  { 'o', DIGITS, ACCUM, APR_OFFSETOF(match_state, offminutes) },
  { '+', "-+", TZIND, 0 },
  { 'Z', "Z", TZIND, 0 },
  { ':', ":", NOOP, 0 },
  { '-', "-", NOOP, 0 },
  { 'T', "T", NOOP, 0 },
  { ' ', " ", NOOP, 0 },
  { '.', ".,", NOOP, 0 },
  { '[', NULL, SKIPFROM, 0 },
  { ']', NULL, SKIP, 0 },
  { '\0', NULL, ACCEPT, 0 },
};

/* Return the rule associated with TCHAR, or NULL if there
   is no such rule. */
static const rule *
find_rule(char tchar)
{
  int i = sizeof(rules)/sizeof(rules[0]);
  while (i--)
    if (rules[i].key == tchar)
      return &rules[i];
  return NULL;
}

/* Attempt to match the date-string in VALUE to the provided TEMPLATE,
   using the rules defined above.  Return TRUE on successful match,
   FALSE otherwise.  On successful match, fill in *EXP with the
   matched values and set *LOCALTZ to TRUE if the local time zone
   should be used to interpret the match (i.e. if no time zone
   information was provided), or FALSE if not. */
static svn_boolean_t
template_match(apr_time_exp_t *expt, svn_boolean_t *localtz,
               const char *template, const char *value)
{
  int multiplier = 100000;
  int tzind = 0;
  match_state ms;
  char *base = (char *)&ms;

  memset(&ms, 0, sizeof(ms));

  for (;;)
    {
      const rule *match = find_rule(*template++);
      char vchar = *value++;
      apr_int32_t *place;

      if (!match || (match->valid
                     && (!vchar || !strchr(match->valid, vchar))))
        return FALSE;

      /* Compute the address of memory location affected by this
         rule by adding match->offset bytes to the address of ms.
         Because this is a byte-quantity, it is necessary to cast
         &ms to char *. */
      place = (apr_int32_t *)(base + match->offset);
      switch (match->action)
        {
        case ACCUM:
          *place = *place * 10 + vchar - '0';
          continue;
        case MICRO:
          *place += (vchar - '0') * multiplier;
          multiplier /= 10;
          continue;
        case TZIND:
          tzind = vchar;
          continue;
        case SKIP:
          value--;
          continue;
        case NOOP:
          continue;
        case SKIPFROM:
          if (!vchar)
            break;
          match = find_rule(*template);
          if (!strchr(match->valid, vchar))
            template = strchr(template, ']') + 1;
          value--;
          continue;
        case ACCEPT:
          if (vchar)
            return FALSE;
          break;
        }

      break;
    }

  /* Validate gmt offset here, since we can't reliably do it later. */
  if (ms.offhours > 23 || ms.offminutes > 59)
    return FALSE;

  /* tzind will be '+' or '-' for an explicit time zone, 'Z' to
     indicate UTC, or 0 to indicate local time. */
  switch (tzind)
    {
    case '+':
      ms.base.tm_gmtoff = ms.offhours * 3600 + ms.offminutes * 60;
      break;
    case '-':
      ms.base.tm_gmtoff = -(ms.offhours * 3600 + ms.offminutes * 60);
      break;
    }

  *expt = ms.base;
  *localtz = (tzind == 0);
  return TRUE;
}

static struct unit_words_table {
  const char *word;
  apr_time_t value;
} unit_words_table[] = {
  /* Word matching does not concern itself with exact days of the month
   * or leap years so these amounts are always fixed. */
  { "years",    apr_time_from_sec(60 * 60 * 24 * 365) },
  { "months",   apr_time_from_sec(60 * 60 * 24 * 30) },
  { "weeks",    apr_time_from_sec(60 * 60 * 24 * 7) },
  { "days",     apr_time_from_sec(60 * 60 * 24) },
  { "hours",    apr_time_from_sec(60 * 60) },
  { "minutes",  apr_time_from_sec(60) },
  { "mins",     apr_time_from_sec(60) },
  { NULL ,      0 }
};

static svn_token_map_t number_words_map[] = {
  { "zero", 0 }, { "one", 1 }, { "two", 2 }, { "three", 3 }, { "four", 4 },
  { "five", 5 }, { "six", 6 }, { "seven", 7 }, { "eight", 8 }, { "nine", 9 },
  { "ten", 10 }, { "eleven", 11 }, { "twelve", 12 }, { NULL, 0 }
};

/* Attempt to match the date-string in TEXT according to the following rules:
 *
 * "N years|months|weeks|days|hours|minutes ago" resolve to the most recent
 * revision prior to the specified time. N may either be a word from
 * NUMBER_WORDS_TABLE defined above, or a non-negative digit.
 *
 * Return TRUE on successful match, FALSE otherwise. On successful match,
 * fill in *EXP with the matched value and set *LOCALTZ to TRUE (this
 * function always uses local time). Use POOL for temporary allocations. */
static svn_boolean_t
words_match(apr_time_exp_t *expt, svn_boolean_t *localtz,
            apr_time_t now, const char *text, apr_pool_t *pool)
{
  apr_time_t t = -1;
  const char *word;
  apr_array_header_t *words;
  int i;
  int n = -1;
  const char *unit_str;

  words = svn_cstring_split(text, " ", TRUE /* chop_whitespace */, pool);

  if (words->nelts != 3)
    return FALSE;

  word = APR_ARRAY_IDX(words, 0, const char *);

  /* Try to parse a number word. */
  n = svn_token__from_word(number_words_map, word);

  if (n == SVN_TOKEN_UNKNOWN)
    {
      svn_error_t *err;

      /* Try to parse a digit. */
      err = svn_cstring_atoi(&n, word);
      if (err)
        {
          svn_error_clear(err);
          return FALSE;
        }
      if (n < 0)
        return FALSE;
    }

  /* Try to parse a unit. */
  word = APR_ARRAY_IDX(words, 1, const char *);
  for (i = 0, unit_str = unit_words_table[i].word;
       unit_str = unit_words_table[i].word, unit_str != NULL; i++)
    {
      /* Tolerate missing trailing 's' from unit. */
      if (!strcmp(word, unit_str) ||
          !strncmp(word, unit_str, strlen(unit_str) - 1))
        {
          t = now - (n * unit_words_table[i].value);
          break;
        }
    }

  if (t < 0)
    return FALSE;

  /* Require trailing "ago". */
  word = APR_ARRAY_IDX(words, 2, const char *);
  if (strcmp(word, "ago"))
    return FALSE;

  if (apr_time_exp_lt(expt, t) != APR_SUCCESS)
    return FALSE;

  *localtz = TRUE;
  return TRUE;
}

static int
valid_days_by_month[] = {
  31, 29, 31, 30,
  31, 30, 31, 31,
  30, 31, 30, 31
};

svn_error_t *
svn_parse_date(svn_boolean_t *matched, apr_time_t *result, const char *text,
               apr_time_t now, apr_pool_t *pool)
{
  apr_time_exp_t expt, expnow;
  apr_status_t apr_err;
  svn_boolean_t localtz;

  *matched = FALSE;

  apr_err = apr_time_exp_lt(&expnow, now);
  if (apr_err != APR_SUCCESS)
    return svn_error_wrap_apr(apr_err, _("Can't manipulate current date"));

  if (template_match(&expt, &localtz, /* ISO-8601 extended, date only */
                     "YYYY-M[M]-D[D]",
                     text)
      || template_match(&expt, &localtz, /* ISO-8601 extended, UTC */
                        "YYYY-M[M]-D[D]Th[h]:mm[:ss[.u[u[u[u[u[u][Z]",
                        text)
      || template_match(&expt, &localtz, /* ISO-8601 extended, with offset */
                        "YYYY-M[M]-D[D]Th[h]:mm[:ss[.u[u[u[u[u[u]+OO[:oo]",
                        text)
      || template_match(&expt, &localtz, /* ISO-8601 basic, date only */
                        "YYYYMMDD",
                        text)
      || template_match(&expt, &localtz, /* ISO-8601 basic, UTC */
                        "YYYYMMDDThhmm[ss[.u[u[u[u[u[u][Z]",
                        text)
      || template_match(&expt, &localtz, /* ISO-8601 basic, with offset */
                        "YYYYMMDDThhmm[ss[.u[u[u[u[u[u]+OO[oo]",
                        text)
      || template_match(&expt, &localtz, /* "svn log" format */
                        "YYYY-M[M]-D[D] h[h]:mm[:ss[.u[u[u[u[u[u][ +OO[oo]",
                        text)
      || template_match(&expt, &localtz, /* GNU date's iso-8601 */
                        "YYYY-M[M]-D[D]Th[h]:mm[:ss[.u[u[u[u[u[u]+OO[oo]",
                        text))
    {
      expt.tm_year -= 1900;
      expt.tm_mon -= 1;
    }
  else if (template_match(&expt, &localtz, /* Just a time */
                          "h[h]:mm[:ss[.u[u[u[u[u[u]",
                          text))
    {
      expt.tm_year = expnow.tm_year;
      expt.tm_mon = expnow.tm_mon;
      expt.tm_mday = expnow.tm_mday;
    }
  else if (!words_match(&expt, &localtz, now, text, pool))
    return SVN_NO_ERROR;

  /* Range validation, allowing for leap seconds */
  if (expt.tm_mon < 0 || expt.tm_mon > 11
      || expt.tm_mday > valid_days_by_month[expt.tm_mon]
      || expt.tm_mday < 1
      || expt.tm_hour > 23
      || expt.tm_min > 59
      || expt.tm_sec > 60)
    return SVN_NO_ERROR;

  /* february/leap-year day checking.  tm_year is bias-1900, so centuries
     that equal 100 (mod 400) are multiples of 400. */
  if (expt.tm_mon == 1
      && expt.tm_mday == 29
      && (expt.tm_year % 4 != 0
          || (expt.tm_year % 100 == 0 && expt.tm_year % 400 != 100)))
    return SVN_NO_ERROR;

  if (localtz)
    {
      apr_time_t candidate;
      apr_time_exp_t expthen;

      /* We need to know the GMT offset of the requested time, not the
         current time.  In some cases, that quantity is ambiguous,
         since at the end of daylight saving's time, an hour's worth
         of local time happens twice.  For those cases, we should
         prefer DST if we are currently in DST, and standard time if
         not.  So, calculate the time value using the current time's
         GMT offset and use the GMT offset of the resulting time. */
      expt.tm_gmtoff = expnow.tm_gmtoff;
      apr_err = apr_time_exp_gmt_get(&candidate, &expt);
      if (apr_err != APR_SUCCESS)
        return svn_error_wrap_apr(apr_err,
                                  _("Can't calculate requested date"));
      apr_err = apr_time_exp_lt(&expthen, candidate);
      if (apr_err != APR_SUCCESS)
        return svn_error_wrap_apr(apr_err, _("Can't expand time"));
      expt.tm_gmtoff = expthen.tm_gmtoff;
    }
  apr_err = apr_time_exp_gmt_get(result, &expt);
  if (apr_err != APR_SUCCESS)
    return svn_error_wrap_apr(apr_err, _("Can't calculate requested date"));

  *matched = TRUE;
  return SVN_NO_ERROR;
}
