/*
 * old-and-busted.c:  routines for reading pre-1.7 working copies.
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
#include "svn_xml.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_ctype.h"
#include "svn_pools.h"

#include "wc.h"
#include "adm_files.h"
#include "entries.h"
#include "lock.h"

#include "private/svn_wc_private.h"
#include "svn_private_config.h"


/* Within the (old) entries file, boolean values have a specific string
   value (thus, TRUE), or they are missing (for FALSE). Below are the
   values for each of the booleans stored.  */
#define ENTRIES_BOOL_COPIED     "copied"
#define ENTRIES_BOOL_DELETED    "deleted"
#define ENTRIES_BOOL_ABSENT     "absent"
#define ENTRIES_BOOL_INCOMPLETE "incomplete"
#define ENTRIES_BOOL_KEEP_LOCAL "keep-local"

/* Tag names used in our old XML entries file.  */
#define ENTRIES_TAG_ENTRY "entry"

/* Attribute names used in our old XML entries file.  */
#define ENTRIES_ATTR_NAME               "name"
#define ENTRIES_ATTR_REPOS              "repos"
#define ENTRIES_ATTR_UUID               "uuid"
#define ENTRIES_ATTR_INCOMPLETE         "incomplete"
#define ENTRIES_ATTR_LOCK_TOKEN         "lock-token"
#define ENTRIES_ATTR_LOCK_OWNER         "lock-owner"
#define ENTRIES_ATTR_LOCK_COMMENT       "lock-comment"
#define ENTRIES_ATTR_LOCK_CREATION_DATE "lock-creation-date"
#define ENTRIES_ATTR_DELETED            "deleted"
#define ENTRIES_ATTR_ABSENT             "absent"
#define ENTRIES_ATTR_CMT_REV            "committed-rev"
#define ENTRIES_ATTR_CMT_DATE           "committed-date"
#define ENTRIES_ATTR_CMT_AUTHOR         "last-author"
#define ENTRIES_ATTR_REVISION           "revision"
#define ENTRIES_ATTR_URL                "url"
#define ENTRIES_ATTR_KIND               "kind"
#define ENTRIES_ATTR_SCHEDULE           "schedule"
#define ENTRIES_ATTR_COPIED             "copied"
#define ENTRIES_ATTR_COPYFROM_URL       "copyfrom-url"
#define ENTRIES_ATTR_COPYFROM_REV       "copyfrom-rev"
#define ENTRIES_ATTR_CHECKSUM           "checksum"
#define ENTRIES_ATTR_WORKING_SIZE       "working-size"
#define ENTRIES_ATTR_TEXT_TIME          "text-time"
#define ENTRIES_ATTR_CONFLICT_OLD       "conflict-old" /* saved old file */
#define ENTRIES_ATTR_CONFLICT_NEW       "conflict-new" /* saved new file */
#define ENTRIES_ATTR_CONFLICT_WRK       "conflict-wrk" /* saved wrk file */
#define ENTRIES_ATTR_PREJFILE           "prop-reject-file"

/* Attribute values used in our old XML entries file.  */
#define ENTRIES_VALUE_FILE     "file"
#define ENTRIES_VALUE_DIR      "dir"
#define ENTRIES_VALUE_ADD      "add"
#define ENTRIES_VALUE_DELETE   "delete"
#define ENTRIES_VALUE_REPLACE  "replace"


/* */
static svn_wc_entry_t *
alloc_entry(apr_pool_t *pool)
{
  svn_wc_entry_t *entry = apr_pcalloc(pool, sizeof(*entry));
  entry->revision = SVN_INVALID_REVNUM;
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  entry->cmt_rev = SVN_INVALID_REVNUM;
  entry->kind = svn_node_none;
  entry->working_size = SVN_WC_ENTRY_WORKING_SIZE_UNKNOWN;
  entry->depth = svn_depth_infinity;
  entry->file_external_path = NULL;
  entry->file_external_peg_rev.kind = svn_opt_revision_unspecified;
  entry->file_external_rev.kind = svn_opt_revision_unspecified;
  return entry;
}



/* Read an escaped byte on the form 'xHH' from [*BUF, END), placing
   the byte in *RESULT.  Advance *BUF to point after the escape
   sequence. */
static svn_error_t *
read_escaped(char *result, char **buf, const char *end)
{
  apr_uint64_t val;
  char digits[3];

  if (end - *buf < 3 || **buf != 'x' || ! svn_ctype_isxdigit((*buf)[1])
      || ! svn_ctype_isxdigit((*buf)[2]))
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Invalid escape sequence"));
  (*buf)++;
  digits[0] = *((*buf)++);
  digits[1] = *((*buf)++);
  digits[2] = 0;
  if ((val = apr_strtoi64(digits, NULL, 16)) == 0)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Invalid escaped character"));
  *result = (char) val;
  return SVN_NO_ERROR;
}

/* Read a field, possibly with escaped bytes, from [*BUF, END),
   stopping at the terminator.  Place the read string in *RESULT, or set
   *RESULT to NULL if it is the empty string.  Allocate the returned string
   in POOL.  Advance *BUF to point after the terminator. */
static svn_error_t *
read_str(const char **result,
         char **buf, const char *end,
         apr_pool_t *pool)
{
  svn_stringbuf_t *s = NULL;
  const char *start;
  if (*buf == end)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Unexpected end of entry"));
  if (**buf == '\n')
    {
      *result = NULL;
      (*buf)++;
      return SVN_NO_ERROR;
    }

  start = *buf;
  while (*buf != end && **buf != '\n')
    {
      if (**buf == '\\')
        {
          char c;
          if (! s)
            s = svn_stringbuf_ncreate(start, *buf - start, pool);
          else
            svn_stringbuf_appendbytes(s, start, *buf - start);
          (*buf)++;
          SVN_ERR(read_escaped(&c, buf, end));
          svn_stringbuf_appendbyte(s, c);
          start = *buf;
        }
      else
        (*buf)++;
    }

  if (*buf == end)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Unexpected end of entry"));

  if (s)
    {
      svn_stringbuf_appendbytes(s, start, *buf - start);
      *result = s->data;
    }
  else
    *result = apr_pstrndup(pool, start, *buf - start);
  (*buf)++;
  return SVN_NO_ERROR;
}

/* This is wrapper around read_str() (which see for details); it
   simply asks svn_path_is_canonical() of the string it reads,
   returning an error if the test fails.
   ### It seems this is only called for entrynames now
   */
static svn_error_t *
read_path(const char **result,
          char **buf, const char *end,
          apr_pool_t *pool)
{
  SVN_ERR(read_str(result, buf, end, pool));
  if (*result && **result && !svn_relpath_is_canonical(*result))
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("Entry contains non-canonical path '%s'"),
                             *result);
  return SVN_NO_ERROR;
}

/* This is read_path() for urls. This function does not do the is_canonical
   test for entries from working copies older than version 10, as since that
   version the canonicalization of urls has been changed. See issue #2475.
   If the test is done and fails, read_url returs an error. */
static svn_error_t *
read_url(const char **result,
         char **buf, const char *end,
         int wc_format,
         apr_pool_t *pool)
{
  SVN_ERR(read_str(result, buf, end, pool));

  /* Always canonicalize the url, as we have stricter canonicalization rules
     in 1.7+ then before */
  if (*result && **result)
    *result = svn_uri_canonicalize(*result, pool);

  return SVN_NO_ERROR;
}

/* Read a field from [*BUF, END), terminated by a newline character.
   The field may not contain escape sequences.  The field is not
   copied and the buffer is modified in place, by replacing the
   terminator with a NUL byte.  Make *BUF point after the original
   terminator. */
static svn_error_t *
read_val(const char **result,
          char **buf, const char *end)
{
  const char *start = *buf;

  if (*buf == end)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Unexpected end of entry"));
  if (**buf == '\n')
    {
      (*buf)++;
      *result = NULL;
      return SVN_NO_ERROR;
    }

  while (*buf != end && **buf != '\n')
    (*buf)++;
  if (*buf == end)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Unexpected end of entry"));
  **buf = '\0';
  *result = start;
  (*buf)++;
  return SVN_NO_ERROR;
}

/* Read a boolean field from [*BUF, END), placing the result in
   *RESULT.  If there is no boolean value (just a terminator), it
   defaults to false.  Else, the value must match FIELD_NAME, in which
   case *RESULT will be set to true.  Advance *BUF to point after the
   terminator. */
static svn_error_t *
read_bool(svn_boolean_t *result, const char *field_name,
          char **buf, const char *end)
{
  const char *val;
  SVN_ERR(read_val(&val, buf, end));
  if (val)
    {
      if (strcmp(val, field_name) != 0)
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                 _("Invalid value for field '%s'"),
                                 field_name);
      *result = TRUE;
    }
  else
    *result = FALSE;
  return SVN_NO_ERROR;
}

/* Read a revision number from [*BUF, END) stopping at the
   terminator.  Set *RESULT to the revision number, or
   SVN_INVALID_REVNUM if there is none.  Use POOL for temporary
   allocations.  Make *BUF point after the terminator.  */
static svn_error_t *
read_revnum(svn_revnum_t *result,
            char **buf,
            const char *end,
            apr_pool_t *pool)
{
  const char *val;

  SVN_ERR(read_val(&val, buf, end));

  if (val)
    *result = SVN_STR_TO_REV(val);
  else
    *result = SVN_INVALID_REVNUM;

  return SVN_NO_ERROR;
}

/* Read a timestamp from [*BUF, END) stopping at the terminator.
   Set *RESULT to the resulting timestamp, or 0 if there is none.  Use
   POOL for temporary allocations.  Make *BUF point after the
   terminator. */
static svn_error_t *
read_time(apr_time_t *result,
          char **buf, const char *end,
          apr_pool_t *pool)
{
  const char *val;

  SVN_ERR(read_val(&val, buf, end));
  if (val)
    SVN_ERR(svn_time_from_cstring(result, val, pool));
  else
    *result = 0;

  return SVN_NO_ERROR;
}

/**
 * Parse the string at *STR as an revision and save the result in
 * *OPT_REV.  After returning successfully, *STR points at next
 * character in *STR where further parsing can be done.
 */
static svn_error_t *
string_to_opt_revision(svn_opt_revision_t *opt_rev,
                       const char **str,
                       apr_pool_t *pool)
{
  const char *s = *str;

  SVN_ERR_ASSERT(opt_rev);

  while (*s && *s != ':')
    ++s;

  /* Should not find a \0. */
  if (!*s)
    return svn_error_createf
      (SVN_ERR_INCORRECT_PARAMS, NULL,
       _("Found an unexpected \\0 in the file external '%s'"), *str);

  if (0 == strncmp(*str, "HEAD:", 5))
    {
      opt_rev->kind = svn_opt_revision_head;
    }
  else
    {
      svn_revnum_t rev;
      const char *endptr;

      SVN_ERR(svn_revnum_parse(&rev, *str, &endptr));
      SVN_ERR_ASSERT(endptr == s);
      opt_rev->kind = svn_opt_revision_number;
      opt_rev->value.number = rev;
    }

  *str = s + 1;

  return SVN_NO_ERROR;
}

/**
 * Given a revision, return a string for the revision, either "HEAD"
 * or a string representation of the revision value.  All other
 * revision kinds return an error.
 */
static svn_error_t *
opt_revision_to_string(const char **str,
                       const char *path,
                       const svn_opt_revision_t *rev,
                       apr_pool_t *pool)
{
  switch (rev->kind)
    {
    case svn_opt_revision_head:
      *str = apr_pstrmemdup(pool, "HEAD", 4);
      break;
    case svn_opt_revision_number:
      *str = apr_ltoa(pool, rev->value.number);
      break;
    default:
      return svn_error_createf
        (SVN_ERR_INCORRECT_PARAMS, NULL,
         _("Illegal file external revision kind %d for path '%s'"),
         rev->kind, path);
      break;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__unserialize_file_external(const char **path_result,
                                  svn_opt_revision_t *peg_rev_result,
                                  svn_opt_revision_t *rev_result,
                                  const char *str,
                                  apr_pool_t *pool)
{
  if (str)
    {
      svn_opt_revision_t peg_rev;
      svn_opt_revision_t op_rev;
      const char *s = str;

      SVN_ERR(string_to_opt_revision(&peg_rev, &s, pool));
      SVN_ERR(string_to_opt_revision(&op_rev, &s, pool));

      *path_result = apr_pstrdup(pool, s);
      *peg_rev_result = peg_rev;
      *rev_result = op_rev;
    }
  else
    {
      *path_result = NULL;
      peg_rev_result->kind = svn_opt_revision_unspecified;
      rev_result->kind = svn_opt_revision_unspecified;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__serialize_file_external(const char **str,
                                const char *path,
                                const svn_opt_revision_t *peg_rev,
                                const svn_opt_revision_t *rev,
                                apr_pool_t *pool)
{
  const char *s;

  if (path)
    {
      const char *s1;
      const char *s2;

      SVN_ERR(opt_revision_to_string(&s1, path, peg_rev, pool));
      SVN_ERR(opt_revision_to_string(&s2, path, rev, pool));

      s = apr_pstrcat(pool, s1, ":", s2, ":", path, SVN_VA_NULL);
    }
  else
    s = NULL;

  *str = s;

  return SVN_NO_ERROR;
}

/* Allocate an entry from POOL and read it from [*BUF, END).  The
   buffer may be modified in place while parsing.  Return the new
   entry in *NEW_ENTRY.  Advance *BUF to point at the end of the entry
   record.
   The entries file format should be provided in ENTRIES_FORMAT. */
static svn_error_t *
read_entry(svn_wc_entry_t **new_entry,
           char **buf, const char *end,
           int entries_format,
           apr_pool_t *pool)
{
  svn_wc_entry_t *entry = alloc_entry(pool);
  const char *name;

#define MAYBE_DONE if (**buf == '\f') goto done

  /* Find the name and set up the entry under that name. */
  SVN_ERR(read_path(&name, buf, end, pool));
  entry->name = name ? name : SVN_WC_ENTRY_THIS_DIR;

  /* Set up kind. */
  {
    const char *kindstr;
    SVN_ERR(read_val(&kindstr, buf, end));
    if (kindstr)
      {
        if (strcmp(kindstr, ENTRIES_VALUE_FILE) == 0)
          entry->kind = svn_node_file;
        else if (strcmp(kindstr, ENTRIES_VALUE_DIR) == 0)
          entry->kind = svn_node_dir;
        else
          return svn_error_createf
            (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
             _("Entry '%s' has invalid node kind"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR));
      }
    else
      entry->kind = svn_node_none;
  }
  MAYBE_DONE;

  /* Attempt to set revision (resolve_to_defaults may do it later, too) */
  SVN_ERR(read_revnum(&entry->revision, buf, end, pool));
  MAYBE_DONE;

  /* Attempt to set up url path (again, see resolve_to_defaults). */
  SVN_ERR(read_url(&entry->url, buf, end, entries_format, pool));
  MAYBE_DONE;

  /* Set up repository root.  Make sure it is a prefix of url. */
  SVN_ERR(read_url(&entry->repos, buf, end, entries_format, pool));
  if (entry->repos && entry->url
      && ! svn_uri__is_ancestor(entry->repos, entry->url))
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("Entry for '%s' has invalid repository "
                               "root"),
                             name ? name : SVN_WC_ENTRY_THIS_DIR);
  MAYBE_DONE;

  /* Look for a schedule attribute on this entry. */
  {
    const char *schedulestr;
    SVN_ERR(read_val(&schedulestr, buf, end));
    entry->schedule = svn_wc_schedule_normal;
    if (schedulestr)
      {
        if (strcmp(schedulestr, ENTRIES_VALUE_ADD) == 0)
          entry->schedule = svn_wc_schedule_add;
        else if (strcmp(schedulestr, ENTRIES_VALUE_DELETE) == 0)
          entry->schedule = svn_wc_schedule_delete;
        else if (strcmp(schedulestr, ENTRIES_VALUE_REPLACE) == 0)
          entry->schedule = svn_wc_schedule_replace;
        else
          return svn_error_createf(
            SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
            _("Entry '%s' has invalid 'schedule' value"),
            name ? name : SVN_WC_ENTRY_THIS_DIR);
      }
  }
  MAYBE_DONE;

  /* Attempt to set up text timestamp. */
  SVN_ERR(read_time(&entry->text_time, buf, end, pool));
  MAYBE_DONE;

  /* Checksum. */
  SVN_ERR(read_str(&entry->checksum, buf, end, pool));
  MAYBE_DONE;

  /* Setup last-committed values. */
  SVN_ERR(read_time(&entry->cmt_date, buf, end, pool));
  MAYBE_DONE;

  SVN_ERR(read_revnum(&entry->cmt_rev, buf, end, pool));
  MAYBE_DONE;

  SVN_ERR(read_str(&entry->cmt_author, buf, end, pool));
  MAYBE_DONE;

  /* has-props, has-prop-mods, cachable-props, present-props are all
     deprecated. Read any values that may be in the 'entries' file, but
     discard them, and just put default values into the entry. */
  {
    const char *unused_value;

    /* has-props flag. */
    SVN_ERR(read_val(&unused_value, buf, end));
    entry->has_props = FALSE;
    MAYBE_DONE;

    /* has-prop-mods flag. */
    SVN_ERR(read_val(&unused_value, buf, end));
    entry->has_prop_mods = FALSE;
    MAYBE_DONE;

    /* Use the empty string for cachable_props, indicating that we no
       longer attempt to cache any properties. An empty string for
       present_props means that no cachable props are present. */

    /* cachable-props string. */
    SVN_ERR(read_val(&unused_value, buf, end));
    entry->cachable_props = "";
    MAYBE_DONE;

    /* present-props string. */
    SVN_ERR(read_val(&unused_value, buf, end));
    entry->present_props = "";
    MAYBE_DONE;
  }

  /* Is this entry in a state of mental torment (conflict)? */
  {
    SVN_ERR(read_path(&entry->prejfile, buf, end, pool));
    MAYBE_DONE;
    SVN_ERR(read_path(&entry->conflict_old, buf, end, pool));
    MAYBE_DONE;
    SVN_ERR(read_path(&entry->conflict_new, buf, end, pool));
    MAYBE_DONE;
    SVN_ERR(read_path(&entry->conflict_wrk, buf, end, pool));
    MAYBE_DONE;
  }

  /* Is this entry copied? */
  SVN_ERR(read_bool(&entry->copied, ENTRIES_BOOL_COPIED, buf, end));
  MAYBE_DONE;

  SVN_ERR(read_url(&entry->copyfrom_url, buf, end, entries_format, pool));
  MAYBE_DONE;
  SVN_ERR(read_revnum(&entry->copyfrom_rev, buf, end, pool));
  MAYBE_DONE;

  /* Is this entry deleted? */
  SVN_ERR(read_bool(&entry->deleted, ENTRIES_BOOL_DELETED, buf, end));
  MAYBE_DONE;

  /* Is this entry absent? */
  SVN_ERR(read_bool(&entry->absent, ENTRIES_BOOL_ABSENT, buf, end));
  MAYBE_DONE;

  /* Is this entry incomplete? */
  SVN_ERR(read_bool(&entry->incomplete, ENTRIES_BOOL_INCOMPLETE, buf, end));
  MAYBE_DONE;

  /* UUID. */
  SVN_ERR(read_str(&entry->uuid, buf, end, pool));
  MAYBE_DONE;

  /* Lock token. */
  SVN_ERR(read_str(&entry->lock_token, buf, end, pool));
  MAYBE_DONE;

  /* Lock owner. */
  SVN_ERR(read_str(&entry->lock_owner, buf, end, pool));
  MAYBE_DONE;

  /* Lock comment. */
  SVN_ERR(read_str(&entry->lock_comment, buf, end, pool));
  MAYBE_DONE;

  /* Lock creation date. */
  SVN_ERR(read_time(&entry->lock_creation_date, buf, end, pool));
  MAYBE_DONE;

  /* Changelist. */
  SVN_ERR(read_str(&entry->changelist, buf, end, pool));
  MAYBE_DONE;

  /* Keep entry in working copy after deletion? */
  SVN_ERR(read_bool(&entry->keep_local, ENTRIES_BOOL_KEEP_LOCAL, buf, end));
  MAYBE_DONE;

  /* Translated size */
  {
    const char *val;

    /* read_val() returns NULL on an empty (e.g. default) entry line,
       and entry has already been initialized accordingly already */
    SVN_ERR(read_val(&val, buf, end));
    if (val)
      entry->working_size = (apr_off_t)apr_strtoi64(val, NULL, 0);
  }
  MAYBE_DONE;

  /* Depth. */
  {
    const char *result;
    SVN_ERR(read_val(&result, buf, end));
    if (result)
      {
        svn_boolean_t invalid;
        svn_boolean_t is_this_dir;

        entry->depth = svn_depth_from_word(result);

        /* Verify the depth value:
           THIS_DIR should not have an excluded value and SUB_DIR should only
           have excluded value. Remember that infinity value is not stored and
           should not show up here. Otherwise, something bad may have
           happened. However, infinity value itself will always be okay. */
        is_this_dir = !name;
        /* '!=': XOR */
        invalid = is_this_dir != (entry->depth != svn_depth_exclude);
        if (entry->depth != svn_depth_infinity && invalid)
          return svn_error_createf(
            SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
            _("Entry '%s' has invalid 'depth' value"),
            name ? name : SVN_WC_ENTRY_THIS_DIR);
      }
    else
      entry->depth = svn_depth_infinity;

  }
  MAYBE_DONE;

  /* Tree conflict data. */
  SVN_ERR(read_str(&entry->tree_conflict_data, buf, end, pool));
  MAYBE_DONE;

  /* File external URL and revision. */
  {
    const char *str;
    SVN_ERR(read_str(&str, buf, end, pool));
    SVN_ERR(svn_wc__unserialize_file_external(&entry->file_external_path,
                                              &entry->file_external_peg_rev,
                                              &entry->file_external_rev,
                                              str,
                                              pool));
  }
  MAYBE_DONE;

 done:
  *new_entry = entry;
  return SVN_NO_ERROR;
}


/* If attribute ATTR_NAME appears in hash ATTS, set *ENTRY_FLAG to its
   boolean value, else set *ENTRY_FLAG false.  ENTRY_NAME is the name
   of the WC-entry. */
static svn_error_t *
do_bool_attr(svn_boolean_t *entry_flag,
             apr_hash_t *atts, const char *attr_name,
             const char *entry_name)
{
  const char *str = svn_hash_gets(atts, attr_name);

  *entry_flag = FALSE;
  if (str)
    {
      if (strcmp(str, "true") == 0)
        *entry_flag = TRUE;
      else if (strcmp(str, "false") == 0 || strcmp(str, "") == 0)
        *entry_flag = FALSE;
      else
        return svn_error_createf
          (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
           _("Entry '%s' has invalid '%s' value"),
           (entry_name ? entry_name : SVN_WC_ENTRY_THIS_DIR), attr_name);
    }
  return SVN_NO_ERROR;
}


/* */
static const char *
extract_string(apr_hash_t *atts,
               const char *att_name,
               apr_pool_t *result_pool)
{
  const char *value = svn_hash_gets(atts, att_name);

  if (value == NULL)
    return NULL;

  return apr_pstrdup(result_pool, value);
}


/* Like extract_string(), but normalizes empty strings to NULL.  */
static const char *
extract_string_normalize(apr_hash_t *atts,
                         const char *att_name,
                         apr_pool_t *result_pool)
{
  const char *value = svn_hash_gets(atts, att_name);

  if (value == NULL)
    return NULL;

  if (*value == '\0')
    return NULL;

  return apr_pstrdup(result_pool, value);
}


/* NOTE: this is used for upgrading old XML-based entries file. Be wary of
         removing items.

   ### many attributes are no longer used within the old-style log files.
   ### These attrs need to be recognized for old entries, however. For these
   ### cases, the code will parse the attribute, but not set *MODIFY_FLAGS
   ### for that particular field. MODIFY_FLAGS is *only* used by the
   ### log-based entry modification system, and will go way once we
   ### completely move away from loggy.

   Set *NEW_ENTRY to a new entry, taking attributes from ATTS, whose
   keys and values are both char *.  Allocate the entry and copy
   attributes into POOL as needed. */
static svn_error_t *
atts_to_entry(svn_wc_entry_t **new_entry,
              apr_hash_t *atts,
              apr_pool_t *pool)
{
  svn_wc_entry_t *entry = alloc_entry(pool);
  const char *name;

  /* Find the name and set up the entry under that name. */
  name = svn_hash_gets(atts, ENTRIES_ATTR_NAME);
  entry->name = name ? apr_pstrdup(pool, name) : SVN_WC_ENTRY_THIS_DIR;

  /* Attempt to set revision (resolve_to_defaults may do it later, too)

     ### not used by loggy; no need to set MODIFY_FLAGS  */
  {
    const char *revision_str
      = svn_hash_gets(atts, ENTRIES_ATTR_REVISION);

    if (revision_str)
      entry->revision = SVN_STR_TO_REV(revision_str);
    else
      entry->revision = SVN_INVALID_REVNUM;
  }

  /* Attempt to set up url path (again, see resolve_to_defaults).

     ### not used by loggy; no need to set MODIFY_FLAGS  */
  entry->url = extract_string(atts, ENTRIES_ATTR_URL, pool);
  if (entry->url)
    entry->url = svn_uri_canonicalize(entry->url, pool);

  /* Set up repository root.  Make sure it is a prefix of url.

     ### not used by loggy; no need to set MODIFY_FLAGS  */
  entry->repos = extract_string(atts, ENTRIES_ATTR_REPOS, pool);
  if (entry->repos)
    entry->repos = svn_uri_canonicalize(entry->repos, pool);

  if (entry->url && entry->repos
      && !svn_uri__is_ancestor(entry->repos, entry->url))
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("Entry for '%s' has invalid repository "
                               "root"),
                             name ? name : SVN_WC_ENTRY_THIS_DIR);

  /* Set up kind. */
  /* ### not used by loggy; no need to set MODIFY_FLAGS  */
  {
    const char *kindstr
      = svn_hash_gets(atts, ENTRIES_ATTR_KIND);

    entry->kind = svn_node_none;
    if (kindstr)
      {
        if (strcmp(kindstr, ENTRIES_VALUE_FILE) == 0)
          entry->kind = svn_node_file;
        else if (strcmp(kindstr, ENTRIES_VALUE_DIR) == 0)
          entry->kind = svn_node_dir;
        else
          return svn_error_createf
            (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
             _("Entry '%s' has invalid node kind"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR));
      }
  }

  /* Look for a schedule attribute on this entry. */
  /* ### not used by loggy; no need to set MODIFY_FLAGS  */
  {
    const char *schedulestr
      = svn_hash_gets(atts, ENTRIES_ATTR_SCHEDULE);

    entry->schedule = svn_wc_schedule_normal;
    if (schedulestr)
      {
        if (strcmp(schedulestr, ENTRIES_VALUE_ADD) == 0)
          entry->schedule = svn_wc_schedule_add;
        else if (strcmp(schedulestr, ENTRIES_VALUE_DELETE) == 0)
          entry->schedule = svn_wc_schedule_delete;
        else if (strcmp(schedulestr, ENTRIES_VALUE_REPLACE) == 0)
          entry->schedule = svn_wc_schedule_replace;
        else if (strcmp(schedulestr, "") == 0)
          entry->schedule = svn_wc_schedule_normal;
        else
          return svn_error_createf(
                   SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
                   _("Entry '%s' has invalid 'schedule' value"),
                   (name ? name : SVN_WC_ENTRY_THIS_DIR));
      }
  }

  /* Is this entry in a state of mental torment (conflict)? */
  entry->prejfile = extract_string_normalize(atts,
                                             ENTRIES_ATTR_PREJFILE,
                                             pool);
  entry->conflict_old = extract_string_normalize(atts,
                                                 ENTRIES_ATTR_CONFLICT_OLD,
                                                 pool);
  entry->conflict_new = extract_string_normalize(atts,
                                                 ENTRIES_ATTR_CONFLICT_NEW,
                                                 pool);
  entry->conflict_wrk = extract_string_normalize(atts,
                                                 ENTRIES_ATTR_CONFLICT_WRK,
                                                 pool);

  /* Is this entry copied? */
  /* ### not used by loggy; no need to set MODIFY_FLAGS  */
  SVN_ERR(do_bool_attr(&entry->copied, atts, ENTRIES_ATTR_COPIED, name));

  /* ### not used by loggy; no need to set MODIFY_FLAGS  */
  entry->copyfrom_url = extract_string(atts, ENTRIES_ATTR_COPYFROM_URL, pool);

  /* ### not used by loggy; no need to set MODIFY_FLAGS  */
  {
    const char *revstr;

    revstr = svn_hash_gets(atts, ENTRIES_ATTR_COPYFROM_REV);
    if (revstr)
      entry->copyfrom_rev = SVN_STR_TO_REV(revstr);
  }

  /* Is this entry deleted?

     ### not used by loggy; no need to set MODIFY_FLAGS  */
  SVN_ERR(do_bool_attr(&entry->deleted, atts, ENTRIES_ATTR_DELETED, name));

  /* Is this entry absent?

     ### not used by loggy; no need to set MODIFY_FLAGS  */
  SVN_ERR(do_bool_attr(&entry->absent, atts, ENTRIES_ATTR_ABSENT, name));

  /* Is this entry incomplete?

     ### not used by loggy; no need to set MODIFY_FLAGS  */
  SVN_ERR(do_bool_attr(&entry->incomplete, atts, ENTRIES_ATTR_INCOMPLETE,
                       name));

  /* Attempt to set up timestamps. */
  /* ### not used by loggy; no need to set MODIFY_FLAGS  */
  {
    const char *text_timestr;

    text_timestr = svn_hash_gets(atts, ENTRIES_ATTR_TEXT_TIME);
    if (text_timestr)
      SVN_ERR(svn_time_from_cstring(&entry->text_time, text_timestr, pool));

    /* Note: we do not persist prop_time, so there is no need to attempt
       to parse a new prop_time value from the log. Certainly, on any
       recent working copy, there will not be a log record to alter
       the prop_time value. */
  }

  /* Checksum. */
  /* ### not used by loggy; no need to set MODIFY_FLAGS  */
  entry->checksum = extract_string(atts, ENTRIES_ATTR_CHECKSUM, pool);

  /* UUID.

     ### not used by loggy; no need to set MODIFY_FLAGS  */
  entry->uuid = extract_string(atts, ENTRIES_ATTR_UUID, pool);

  /* Setup last-committed values. */
  {
    const char *cmt_datestr, *cmt_revstr;

    cmt_datestr = svn_hash_gets(atts, ENTRIES_ATTR_CMT_DATE);
    if (cmt_datestr)
      {
        SVN_ERR(svn_time_from_cstring(&entry->cmt_date, cmt_datestr, pool));
      }
    else
      entry->cmt_date = 0;

    cmt_revstr = svn_hash_gets(atts, ENTRIES_ATTR_CMT_REV);
    if (cmt_revstr)
      {
        entry->cmt_rev = SVN_STR_TO_REV(cmt_revstr);
      }
    else
      entry->cmt_rev = SVN_INVALID_REVNUM;

    entry->cmt_author = extract_string(atts, ENTRIES_ATTR_CMT_AUTHOR, pool);
  }

  /* ### not used by loggy; no need to set MODIFY_FLAGS  */
  entry->lock_token = extract_string(atts, ENTRIES_ATTR_LOCK_TOKEN, pool);
  entry->lock_owner = extract_string(atts, ENTRIES_ATTR_LOCK_OWNER, pool);
  entry->lock_comment = extract_string(atts, ENTRIES_ATTR_LOCK_COMMENT, pool);
  {
    const char *cdate_str =
      svn_hash_gets(atts, ENTRIES_ATTR_LOCK_CREATION_DATE);
    if (cdate_str)
      {
        SVN_ERR(svn_time_from_cstring(&entry->lock_creation_date,
                                      cdate_str, pool));
      }
  }
  /* ----- end of lock handling.  */

  /* Note: if there are attributes for the (deprecated) has_props,
     has_prop_mods, cachable_props, or present_props, then we're just
     going to ignore them. */

  /* Translated size */
  /* ### not used by loggy; no need to set MODIFY_FLAGS  */
  {
    const char *val = svn_hash_gets(atts, ENTRIES_ATTR_WORKING_SIZE);
    if (val)
      {
        /* Cast to off_t; it's safe: we put in an off_t to start with... */
        entry->working_size = (apr_off_t)apr_strtoi64(val, NULL, 0);
      }
  }

  *new_entry = entry;
  return SVN_NO_ERROR;
}

/* Used when reading an entries file in XML format. */
struct entries_accumulator
{
  /* Keys are entry names, vals are (struct svn_wc_entry_t *)'s. */
  apr_hash_t *entries;

  /* The parser that's parsing it, for signal_expat_bailout(). */
  svn_xml_parser_t *parser;

  /* Don't leave home without one. */
  apr_pool_t *pool;

  /* Cleared before handling each entry. */
  apr_pool_t *scratch_pool;
};



/* Called whenever we find an <open> tag of some kind. */
static void
handle_start_tag(void *userData, const char *tagname, const char **atts)
{
  struct entries_accumulator *accum = userData;
  apr_hash_t *attributes;
  svn_wc_entry_t *entry;
  svn_error_t *err;

  /* We only care about the `entry' tag; all other tags, such as `xml'
     and `wc-entries', are ignored. */
  if (strcmp(tagname, ENTRIES_TAG_ENTRY))
    return;

  svn_pool_clear(accum->scratch_pool);
  /* Make an entry from the attributes. */
  attributes = svn_xml_make_att_hash(atts, accum->scratch_pool);
  err = atts_to_entry(&entry, attributes, accum->pool);
  if (err)
    {
      svn_xml_signal_bailout(err, accum->parser);
      return;
    }

  /* Find the name and set up the entry under that name.  This
     should *NOT* be NULL, since svn_wc__atts_to_entry() should
     have made it into SVN_WC_ENTRY_THIS_DIR. */
  svn_hash_sets(accum->entries, entry->name, entry);
}

/* Parse BUF of size SIZE as an entries file in XML format, storing the parsed
   entries in ENTRIES.  Use SCRATCH_POOL for temporary allocations and
   RESULT_POOL for the returned entries.  */
static svn_error_t *
parse_entries_xml(const char *dir_abspath,
                  apr_hash_t *entries,
                  const char *buf,
                  apr_size_t size,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_xml_parser_t *svn_parser;
  struct entries_accumulator accum;

  /* Set up userData for the XML parser. */
  accum.entries = entries;
  accum.pool = result_pool;
  accum.scratch_pool = svn_pool_create(scratch_pool);

  /* Create the XML parser */
  svn_parser = svn_xml_make_parser(&accum,
                                   handle_start_tag,
                                   NULL,
                                   NULL,
                                   scratch_pool);

  /* Store parser in its own userdata, so callbacks can call
     svn_xml_signal_bailout() */
  accum.parser = svn_parser;

  /* Parse. */
  SVN_ERR_W(svn_xml_parse(svn_parser, buf, size, TRUE),
            apr_psprintf(scratch_pool,
                         _("XML parser failed in '%s'"),
                         svn_dirent_local_style(dir_abspath, scratch_pool)));

  svn_pool_destroy(accum.scratch_pool);

  /* Clean up the XML parser */
  svn_xml_free_parser(svn_parser);

  return SVN_NO_ERROR;
}



/* Use entry SRC to fill in blank portions of entry DST.  SRC itself
   may not have any blanks, of course.
   Typically, SRC is a parent directory's own entry, and DST is some
   child in that directory. */
static void
take_from_entry(const svn_wc_entry_t *src,
                svn_wc_entry_t *dst,
                apr_pool_t *pool)
{
  /* Inherits parent's revision if doesn't have a revision of one's
     own, unless this is a subdirectory. */
  if ((dst->revision == SVN_INVALID_REVNUM) && (dst->kind != svn_node_dir))
    dst->revision = src->revision;

  /* Inherits parent's url if doesn't have a url of one's own. */
  if (! dst->url)
    dst->url = svn_path_url_add_component2(src->url, dst->name, pool);

  if (! dst->repos)
    dst->repos = src->repos;

  if ((! dst->uuid)
      && (! ((dst->schedule == svn_wc_schedule_add)
             || (dst->schedule == svn_wc_schedule_replace))))
    {
      dst->uuid = src->uuid;
    }
}

/* Resolve any missing information in ENTRIES by deducing from the
   directory's own entry (which must already be present in ENTRIES). */
static svn_error_t *
resolve_to_defaults(apr_hash_t *entries,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_wc_entry_t *default_entry
    = svn_hash_gets(entries, SVN_WC_ENTRY_THIS_DIR);

  /* First check the dir's own entry for consistency. */
  if (! default_entry)
    return svn_error_create(SVN_ERR_ENTRY_NOT_FOUND,
                            NULL,
                            _("Missing default entry"));

  if (default_entry->revision == SVN_INVALID_REVNUM)
    return svn_error_create(SVN_ERR_ENTRY_MISSING_REVISION,
                            NULL,
                            _("Default entry has no revision number"));

  if (! default_entry->url)
    return svn_error_create(SVN_ERR_ENTRY_MISSING_URL,
                            NULL,
                            _("Default entry is missing URL"));


  /* Then use it to fill in missing information in other entries. */
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      svn_wc_entry_t *this_entry = apr_hash_this_val(hi);

      if (this_entry == default_entry)
        /* THIS_DIR already has all the information it can possibly
           have.  */
        continue;

      if (this_entry->kind == svn_node_dir)
        /* Entries that are directories have everything but their
           name, kind, and state stored in the THIS_DIR entry of the
           directory itself.  However, we are disallowing the perusing
           of any entries outside of the current entries file.  If a
           caller wants more info about a directory, it should look in
           the entries file in the directory.  */
        continue;

      if (this_entry->kind == svn_node_file)
        /* For file nodes that do not explicitly have their ancestry
           stated, this can be derived from the default entry of the
           directory in which those files reside.  */
        take_from_entry(default_entry, this_entry, pool);
    }

  return SVN_NO_ERROR;
}



/* Read and parse an old-style 'entries' file in the administrative area
   of PATH, filling in ENTRIES with the contents. The results will be
   allocated in RESULT_POOL, and temporary allocations will be made in
   SCRATCH_POOL.  */
svn_error_t *
svn_wc__read_entries_old(apr_hash_t **entries,
                         const char *dir_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  char *curp;
  const char *endp;
  svn_wc_entry_t *entry;
  svn_stream_t *stream;
  svn_string_t *buf;

  *entries = apr_hash_make(result_pool);

  /* Open the entries file. */
  SVN_ERR(svn_wc__open_adm_stream(&stream, dir_abspath, SVN_WC__ADM_ENTRIES,
                                  scratch_pool, scratch_pool));
  SVN_ERR(svn_string_from_stream2(&buf, stream, SVN__STREAM_CHUNK_SIZE,
                                  scratch_pool));

  /* We own the returned data; it is modifiable, so cast away... */
  curp = (char *)buf->data;
  endp = buf->data + buf->len;

  /* If the first byte of the file is not a digit, then it is probably in XML
     format. */
  if (curp != endp && !svn_ctype_isdigit(*curp))
    SVN_ERR(parse_entries_xml(dir_abspath, *entries, buf->data, buf->len,
                              result_pool, scratch_pool));
  else
    {
      int entryno, entries_format;
      const char *val;

      /* Read the format line from the entries file. In case we're in the
         middle of upgrading a working copy, this line will contain the
         original format pre-upgrade. */
      SVN_ERR(read_val(&val, &curp, endp));
      if (val)
        entries_format = (int)apr_strtoi64(val, NULL, 0);
      else
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                 _("Invalid version line in entries file "
                                   "of '%s'"),
                                 svn_dirent_local_style(dir_abspath,
                                                        scratch_pool));
      entryno = 1;

      while (curp != endp)
        {
          svn_error_t *err = read_entry(&entry, &curp, endp,
                                        entries_format, result_pool);
          if (! err)
            {
              /* We allow extra fields at the end of the line, for
                 extensibility. */
              curp = memchr(curp, '\f', endp - curp);
              if (! curp)
                err = svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                                       _("Missing entry terminator"));
              if (! err && (curp == endp || *(++curp) != '\n'))
                err = svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                                       _("Invalid entry terminator"));
            }
          if (err)
            return svn_error_createf(err->apr_err, err,
                                     _("Error at entry %d in entries file for "
                                       "'%s':"),
                                     entryno,
                                     svn_dirent_local_style(dir_abspath,
                                                            scratch_pool));

          ++curp;
          ++entryno;

          svn_hash_sets(*entries, entry->name, entry);
        }
    }

  /* Fill in any implied fields. */
  return svn_error_trace(resolve_to_defaults(*entries, result_pool));
}


/* For non-directory PATHs full entry information is obtained by reading
 * the entries for the parent directory of PATH and then extracting PATH's
 * entry.  If PATH is a directory then only abrieviated information is
 * available in the parent directory, more complete information is
 * available by reading the entries for PATH itself.
 *
 * Note: There is one bit of information about directories that is only
 * available in the parent directory, that is the "deleted" state.  If PATH
 * is a versioned directory then the "deleted" state information will not
 * be returned in ENTRY.  This means some bits of the code (e.g. revert)
 * need to obtain it by directly extracting the directory entry from the
 * parent directory's entries.  I wonder if this function should handle
 * that?
 */
svn_error_t *
svn_wc_entry(const svn_wc_entry_t **entry,
             const char *path,
             svn_wc_adm_access_t *adm_access,
             svn_boolean_t show_hidden,
             apr_pool_t *pool)
{
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const char *local_abspath;
  svn_wc_adm_access_t *dir_access;
  const char *entry_name;
  apr_hash_t *entries;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  /* Does the provided path refer to a directory with an associated
     access baton?  */
  dir_access = svn_wc__adm_retrieve_internal2(db, local_abspath, pool);
  if (dir_access == NULL)
    {
      /* Damn. Okay. Assume the path is to a child, and let's look for
         a baton associated with its parent.  */

      const char *dir_abspath;

      svn_dirent_split(&dir_abspath, &entry_name, local_abspath, pool);

      dir_access = svn_wc__adm_retrieve_internal2(db, dir_abspath, pool);
    }
  else
    {
      /* Woo! Got one. Look for "this dir" in the entries hash.  */
      entry_name = "";
    }

  if (dir_access == NULL)
    {
      /* Early exit.  */
      *entry = NULL;
      return SVN_NO_ERROR;
    }

  /* Load an entries hash, and cache it into DIR_ACCESS. Go ahead and
     fetch all entries here (optimization) since we know how to filter
     out a "hidden" node.  */
  SVN_ERR(svn_wc__entries_read_internal(&entries, dir_access, TRUE, pool));
  *entry = svn_hash_gets(entries, entry_name);

  if (!show_hidden && *entry != NULL)
    {
      svn_boolean_t hidden;

      SVN_ERR(svn_wc__entry_is_hidden(&hidden, *entry));
      if (hidden)
        *entry = NULL;
    }

  return SVN_NO_ERROR;
}
