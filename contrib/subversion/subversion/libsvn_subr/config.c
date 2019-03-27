/*
 * config.c :  reading configuration information
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



#include <assert.h>

#define APR_WANT_STRFUNC
#define APR_WANT_MEMFUNC
#include <apr_want.h>

#include <apr_general.h>
#include <apr_lib.h>
#include "svn_hash.h"
#include "svn_error.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "config_impl.h"

#include "private/svn_dep_compat.h"
#include "private/svn_subr_private.h"
#include "private/svn_config_private.h"

#include "svn_private_config.h"




/* Section table entries. */
typedef struct cfg_section_t cfg_section_t;
struct cfg_section_t
{
  /* The section name. */
  const char *name;

  /* Table of cfg_option_t's. */
  apr_hash_t *options;
};


/* States that a config option value can assume. */
typedef enum option_state_t
{
  /* Value still needs to be expanded.
     This is the initial state for *all* values. */
  option_state_needs_expanding,

  /* Value is currently being expanded.
     This transitional state allows for detecting cyclic dependencies. */
  option_state_expanding,

  /* Expanded value is available.
     Values that never needed expanding directly go into that state
     skipping option_state_expanding. */
  option_state_expanded,

  /* The value expansion is cyclic which results in "undefined" behavior.
     This is to return a defined value ("") in that case. */
  option_state_cyclic
} option_state_t;

/* Option table entries. */
typedef struct cfg_option_t cfg_option_t;
struct cfg_option_t
{
  /* The option name. */
  const char *name;

  /* The option name, converted into a hash key. */
  const char *hash_key;

  /* The unexpanded option value. */
  const char *value;

  /* The expanded option value. */
  const char *x_value;

  /* Expansion state. If this is option_state_expanded, VALUE has already
     been expanded.  In this case, if x_value is NULL, no expansions were
     necessary, and value should be used directly. */
  option_state_t state;
};



svn_error_t *
svn_config_create2(svn_config_t **cfgp,
                   svn_boolean_t section_names_case_sensitive,
                   svn_boolean_t option_names_case_sensitive,
                   apr_pool_t *result_pool)
{
  svn_config_t *cfg = apr_palloc(result_pool, sizeof(*cfg));

  cfg->sections = svn_hash__make(result_pool);
  cfg->pool = result_pool;
  cfg->x_pool = svn_pool_create(result_pool);
  cfg->x_values = FALSE;
  cfg->tmp_key = svn_stringbuf_create_empty(result_pool);
  cfg->tmp_value = svn_stringbuf_create_empty(result_pool);
  cfg->section_names_case_sensitive = section_names_case_sensitive;
  cfg->option_names_case_sensitive = option_names_case_sensitive;
  cfg->read_only = FALSE;

  *cfgp = cfg;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_config_read3(svn_config_t **cfgp, const char *file,
                 svn_boolean_t must_exist,
                 svn_boolean_t section_names_case_sensitive,
                 svn_boolean_t option_names_case_sensitive,
                 apr_pool_t *result_pool)
{
  svn_config_t *cfg;
  svn_error_t *err;

  SVN_ERR(svn_config_create2(&cfg,
                             section_names_case_sensitive,
                             option_names_case_sensitive,
                             result_pool));

  /* Yes, this is platform-specific code in Subversion, but there's no
     practical way to migrate it into APR, as it's simultaneously
     Subversion-specific and Windows-specific.  Even if we eventually
     want to have APR offer a generic config-reading interface, it
     makes sense to test it here first and migrate it later. */
#ifdef WIN32
  if (0 == strncmp(file, SVN_REGISTRY_PREFIX, SVN_REGISTRY_PREFIX_LEN))
    err = svn_config__parse_registry(cfg, file + SVN_REGISTRY_PREFIX_LEN,
                                     must_exist, result_pool);
  else
#endif /* WIN32 */
    err = svn_config__parse_file(cfg, file, must_exist, result_pool);

  if (err != SVN_NO_ERROR)
    return err;
  else
    *cfgp = cfg;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_config__default_add_value_fn(void *baton,
                                 svn_stringbuf_t *section,
                                 svn_stringbuf_t *option,
                                 svn_stringbuf_t *value)
{
  /* FIXME: We may as well propagate the known string sizes here. */
  svn_config_set((svn_config_t *)baton, section->data,
                 option->data, value->data);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_config_parse(svn_config_t **cfgp, svn_stream_t *stream,
                 svn_boolean_t section_names_case_sensitive,
                 svn_boolean_t option_names_case_sensitive,
                 apr_pool_t *result_pool)
{
  svn_config_t *cfg;
  svn_error_t *err;
  apr_pool_t *scratch_pool = svn_pool_create(result_pool);

  err = svn_config_create2(&cfg,
                           section_names_case_sensitive,
                           option_names_case_sensitive,
                           result_pool);

  if (err == SVN_NO_ERROR)
    err = svn_config__parse_stream(stream,
                                   svn_config__constructor_create(
                                       NULL, NULL,
                                       svn_config__default_add_value_fn,
                                       scratch_pool),
                                   cfg, scratch_pool);

  if (err == SVN_NO_ERROR)
    *cfgp = cfg;

  svn_pool_destroy(scratch_pool);

  return err;
}

/* Read various configuration sources into *CFGP, in this order, with
 * later reads overriding the results of earlier ones:
 *
 *    1. SYS_REGISTRY_PATH   (only on Win32, but ignored if NULL)
 *
 *    2. SYS_FILE_PATH       (everywhere, but ignored if NULL)
 *
 *    3. USR_REGISTRY_PATH   (only on Win32, but ignored if NULL)
 *
 *    4. USR_FILE_PATH       (everywhere, but ignored if NULL)
 *
 * Allocate *CFGP in POOL.  Even if no configurations are read,
 * allocate an empty *CFGP.
 */
static svn_error_t *
read_all(svn_config_t **cfgp,
         const char *sys_registry_path,
         const char *usr_registry_path,
         const char *sys_file_path,
         const char *usr_file_path,
         apr_pool_t *pool)
{
  svn_boolean_t red_config = FALSE;  /* "red" is the past tense of "read" */

  /*** Read system-wide configurations first... ***/

#ifdef WIN32
  if (sys_registry_path)
    {
      SVN_ERR(svn_config_read3(cfgp, sys_registry_path, FALSE, FALSE, FALSE,
                               pool));
      red_config = TRUE;
    }
#endif /* WIN32 */

  if (sys_file_path)
    {
      if (red_config)
        SVN_ERR(svn_config_merge(*cfgp, sys_file_path, FALSE));
      else
        {
          SVN_ERR(svn_config_read3(cfgp, sys_file_path,
                                   FALSE, FALSE, FALSE, pool));
          red_config = TRUE;
        }
    }

  /*** ...followed by per-user configurations. ***/

#ifdef WIN32
  if (usr_registry_path)
    {
      if (red_config)
        SVN_ERR(svn_config_merge(*cfgp, usr_registry_path, FALSE));
      else
        {
          SVN_ERR(svn_config_read3(cfgp, usr_registry_path,
                                   FALSE, FALSE, FALSE, pool));
          red_config = TRUE;
        }
    }
#endif /* WIN32 */

  if (usr_file_path)
    {
      if (red_config)
        SVN_ERR(svn_config_merge(*cfgp, usr_file_path, FALSE));
      else
        {
          SVN_ERR(svn_config_read3(cfgp, usr_file_path,
                                   FALSE, FALSE, FALSE, pool));
          red_config = TRUE;
        }
    }

  if (! red_config)
    SVN_ERR(svn_config_create2(cfgp, FALSE, FALSE, pool));

  return SVN_NO_ERROR;
}


/* CONFIG_DIR provides an override for the default behavior of reading
   the default set of overlay files described by read_all()'s doc
   string.  Returns non-NULL *CFG or an error. */
static svn_error_t *
get_category_config(svn_config_t **cfg,
                    const char *config_dir,
                    const char *category,
                    apr_pool_t *pool)
{
  const char *usr_reg_path = NULL, *sys_reg_path = NULL;
  const char *usr_cfg_path, *sys_cfg_path;
  svn_error_t *err = NULL;

  *cfg = NULL;

  if (! config_dir)
    {
#ifdef WIN32
      sys_reg_path = apr_pstrcat(pool, SVN_REGISTRY_SYS_CONFIG_PATH,
                                 category, SVN_VA_NULL);
      usr_reg_path = apr_pstrcat(pool, SVN_REGISTRY_USR_CONFIG_PATH,
                                 category, SVN_VA_NULL);
#endif /* WIN32 */

      err = svn_config__sys_config_path(&sys_cfg_path, category, pool);
      if ((err) && (err->apr_err == SVN_ERR_BAD_FILENAME))
        {
          sys_cfg_path = NULL;
          svn_error_clear(err);
        }
      else if (err)
        return err;
    }
  else
    sys_cfg_path = NULL;

  SVN_ERR(svn_config_get_user_config_path(&usr_cfg_path, config_dir, category,
                                          pool));
  return read_all(cfg, sys_reg_path, usr_reg_path,
                  sys_cfg_path, usr_cfg_path, pool);
}


svn_error_t *
svn_config_get_config(apr_hash_t **cfg_hash,
                      const char *config_dir,
                      apr_pool_t *pool)
{
  svn_config_t *cfg;
  *cfg_hash = svn_hash__make(pool);

  SVN_ERR(get_category_config(&cfg, config_dir, SVN_CONFIG_CATEGORY_SERVERS,
                              pool));
  svn_hash_sets(*cfg_hash, SVN_CONFIG_CATEGORY_SERVERS, cfg);

  SVN_ERR(get_category_config(&cfg, config_dir, SVN_CONFIG_CATEGORY_CONFIG,
                              pool));
  svn_hash_sets(*cfg_hash, SVN_CONFIG_CATEGORY_CONFIG, cfg);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_config__get_default_config(apr_hash_t **cfg_hash,
                               apr_pool_t *pool)
{
  svn_config_t *empty_cfg;
  *cfg_hash = svn_hash__make(pool);

  SVN_ERR(svn_config_create2(&empty_cfg, FALSE, FALSE, pool));
  svn_hash_sets(*cfg_hash, SVN_CONFIG_CATEGORY_CONFIG, empty_cfg);

  SVN_ERR(svn_config_create2(&empty_cfg, FALSE, FALSE, pool));
  svn_hash_sets(*cfg_hash, SVN_CONFIG_CATEGORY_SERVERS, empty_cfg);

  return SVN_NO_ERROR;
}



/* Iterate through CFG, passing BATON to CALLBACK for every (SECTION, OPTION)
   pair.  Stop if CALLBACK returns TRUE.  Allocate from POOL. */
static void
for_each_option(svn_config_t *cfg, void *baton, apr_pool_t *pool,
                svn_boolean_t callback(void *same_baton,
                                       cfg_section_t *section,
                                       cfg_option_t *option))
{
  apr_hash_index_t *sec_ndx;
  for (sec_ndx = apr_hash_first(pool, cfg->sections);
       sec_ndx != NULL;
       sec_ndx = apr_hash_next(sec_ndx))
    {
      cfg_section_t *sec = apr_hash_this_val(sec_ndx);
      apr_hash_index_t *opt_ndx;

      for (opt_ndx = apr_hash_first(pool, sec->options);
           opt_ndx != NULL;
           opt_ndx = apr_hash_next(opt_ndx))
        {
          cfg_option_t *opt = apr_hash_this_val(opt_ndx);

          if (callback(baton, sec, opt))
            return;
        }
    }
}



static svn_boolean_t
merge_callback(void *baton, cfg_section_t *section, cfg_option_t *option)
{
  svn_config_set(baton, section->name, option->name, option->value);
  return FALSE;
}

svn_error_t *
svn_config_merge(svn_config_t *cfg, const char *file,
                 svn_boolean_t must_exist)
{
  /* The original config hash shouldn't change if there's an error
     while reading the confguration, so read into a temporary table.
     ### We could use a tmp subpool for this, since merge_cfg is going
     to be tossed afterwards.  Premature optimization, though? */
  svn_config_t *merge_cfg;
  SVN_ERR(svn_config_read3(&merge_cfg, file, must_exist,
                           cfg->section_names_case_sensitive,
                           cfg->option_names_case_sensitive,
                           cfg->pool));

  /* Now copy the new options into the original table. */
  for_each_option(merge_cfg, cfg, merge_cfg->pool, merge_callback);
  return SVN_NO_ERROR;
}



/* Remove variable expansions from CFG.  Walk through the options tree,
   killing all expanded values, then clear the expanded value pool. */
static svn_boolean_t
rmex_callback(void *baton, cfg_section_t *section, cfg_option_t *option)
{
  /* Only reset the expansion state if the value actually contains
     variable expansions. */
  if (   (option->state == option_state_expanded && option->x_value != NULL)
      || option->state == option_state_cyclic)
    {
      option->x_value = NULL;
      option->state = option_state_needs_expanding;
    }

  return FALSE;
}

static void
remove_expansions(svn_config_t *cfg)
{
  if (!cfg->x_values)
    return;

  for_each_option(cfg, NULL, cfg->x_pool, rmex_callback);
  svn_pool_clear(cfg->x_pool);
  cfg->x_values = FALSE;
}



/* Canonicalize a string for hashing.  Modifies KEY in place. */
static APR_INLINE char *
make_hash_key(char *key)
{
  register char *p;
  for (p = key; *p != 0; ++p)
    *p = (char)apr_tolower(*p);
  return key;
}

/* Return the value for KEY in HASH.  If CASE_SENSITIVE is FALSE,
   BUFFER will be used to construct the normalized hash key. */
static void *
get_hash_value(apr_hash_t *hash,
               svn_stringbuf_t *buffer,
               const char *key,
               svn_boolean_t case_sensitive)
{
  apr_size_t i;
  apr_size_t len = strlen(key);

  if (case_sensitive)
    return apr_hash_get(hash, key, len);

  svn_stringbuf_ensure(buffer, len);
  for (i = 0; i < len; ++i)
    buffer->data[i] = (char)apr_tolower(key[i]);

  return apr_hash_get(hash, buffer->data, len);
}

/* Return a pointer to an option in CFG, or NULL if it doesn't exist.
   if SECTIONP is non-null, return a pointer to the option's section.
   OPTION may be NULL. */
static cfg_option_t *
find_option(svn_config_t *cfg, const char *section, const char *option,
            cfg_section_t **sectionp)
{
  void *sec_ptr = get_hash_value(cfg->sections, cfg->tmp_key, section,
                                 cfg->section_names_case_sensitive);
  if (sectionp != NULL)
    *sectionp = sec_ptr;

  if (sec_ptr != NULL && option != NULL)
    {
      cfg_section_t *sec = sec_ptr;
      cfg_option_t *opt = get_hash_value(sec->options, cfg->tmp_key, option,
                                         cfg->option_names_case_sensitive);
      /* NOTE: ConfigParser's sections are case sensitive. */
      if (opt == NULL
          && apr_strnatcasecmp(section, SVN_CONFIG__DEFAULT_SECTION) != 0)
        /* Options which aren't found in the requested section are
           also sought after in the default section. */
        opt = find_option(cfg, SVN_CONFIG__DEFAULT_SECTION, option, &sec);
      return opt;
    }

  return NULL;
}


/* Has a bi-directional dependency with make_string_from_option(). */
static svn_boolean_t
expand_option_value(svn_config_t *cfg, cfg_section_t *section,
                    const char *opt_value, const char **opt_x_valuep,
                    apr_pool_t *x_pool);


/* Set *VALUEP according to the OPT's value.  A value for X_POOL must
   only ever be passed into this function by expand_option_value(). */
static void
make_string_from_option(const char **valuep, svn_config_t *cfg,
                        cfg_section_t *section, cfg_option_t *opt,
                        apr_pool_t* x_pool)
{
  /* Expand the option value if necessary. */
  if (   opt->state == option_state_expanding
      || opt->state == option_state_cyclic)
    {
      /* Recursion is not supported.  Since we can't produce an error
       * nor should we abort the process, the next best thing is to
       * report the recursive part as an empty string. */
      *valuep = "";

      /* Go into "value undefined" state. */
      opt->state = option_state_cyclic;

      return;
    }
  else if (opt->state == option_state_needs_expanding)
    {
      /* before attempting to expand an option, check for the placeholder.
       * If none is there, there is no point in calling expand_option_value.
       */
      if (opt->value && strchr(opt->value, '%'))
        {
          apr_pool_t *tmp_pool;

          /* setting read-only mode should have expanded all values
           * automatically. */
          assert(!cfg->read_only);

          tmp_pool = (x_pool ? x_pool : svn_pool_create(cfg->x_pool));

          /* Expand the value. During that process, have the option marked
           * as "expanding" to detect cycles. */
          opt->state = option_state_expanding;
          if (expand_option_value(cfg, section, opt->value, &opt->x_value,
                                  tmp_pool))
            opt->state = option_state_expanded;
          else
            opt->state = option_state_cyclic;

          /* Ensure the expanded value is allocated in a permanent pool. */
          if (x_pool != cfg->x_pool)
            {
              /* Grab the fully expanded value from tmp_pool before its
                 disappearing act. */
              if (opt->x_value)
                opt->x_value = apr_pstrmemdup(cfg->x_pool, opt->x_value,
                                              strlen(opt->x_value));
              if (!x_pool)
                svn_pool_destroy(tmp_pool);
            }
        }
      else
        {
          opt->state = option_state_expanded;
        }
    }

  if (opt->x_value)
    *valuep = opt->x_value;
  else
    *valuep = opt->value;
}


/* Start of variable-replacement placeholder */
#define FMT_START     "%("
#define FMT_START_LEN (sizeof(FMT_START) - 1)

/* End of variable-replacement placeholder */
#define FMT_END       ")s"
#define FMT_END_LEN   (sizeof(FMT_END) - 1)


/* Expand OPT_VALUE (which may be NULL) in SECTION into *OPT_X_VALUEP.
   If no variable replacements are done, set *OPT_X_VALUEP to
   NULL.  Return TRUE if the expanded value is defined and FALSE
   for recursive definitions.  Allocate from X_POOL. */
static svn_boolean_t
expand_option_value(svn_config_t *cfg, cfg_section_t *section,
                    const char *opt_value, const char **opt_x_valuep,
                    apr_pool_t *x_pool)
{
  svn_stringbuf_t *buf = NULL;
  const char *parse_from = opt_value;
  const char *copy_from = parse_from;
  const char *name_start, *name_end;

  while (parse_from != NULL
         && *parse_from != '\0'
         && (name_start = strstr(parse_from, FMT_START)) != NULL)
    {
      name_start += FMT_START_LEN;
      if (*name_start == '\0')
        /* FMT_START at end of opt_value. */
        break;

      name_end = strstr(name_start, FMT_END);
      if (name_end != NULL)
        {
          cfg_option_t *x_opt;
          apr_size_t len = name_end - name_start;
          char *name = apr_pstrmemdup(x_pool, name_start, len);

          x_opt = find_option(cfg, section->name, name, NULL);

          if (x_opt != NULL)
            {
              const char *cstring;

              /* Pass back the sub-pool originally provided by
                 make_string_from_option() as an indication of when it
                 should terminate. */
              make_string_from_option(&cstring, cfg, section, x_opt, x_pool);

              /* Values depending on cyclic values must also be marked as
               * "undefined" because they might themselves form cycles with
               * the one cycle we just detected.  Due to the early abort of
               * the recursion, we won't follow and thus detect dependent
               * cycles anymore.
               */
              if (x_opt->state == option_state_cyclic)
                {
                  *opt_x_valuep = "";
                  return FALSE;
                }

              /* Append the plain text preceding the expansion. */
              len = name_start - FMT_START_LEN - copy_from;
              if (buf == NULL)
                {
                  buf = svn_stringbuf_ncreate(copy_from, len, x_pool);
                  cfg->x_values = TRUE;
                }
              else
                svn_stringbuf_appendbytes(buf, copy_from, len);

              /* Append the expansion and adjust parse pointers. */
              svn_stringbuf_appendcstr(buf, cstring);
              parse_from = name_end + FMT_END_LEN;
              copy_from = parse_from;
            }
          else
            /* Though ConfigParser considers the failure to resolve
               the requested expansion an exception condition, we
               consider it to be plain text, and look for the start of
               the next one. */
            parse_from = name_end + FMT_END_LEN;
        }
      else
        /* Though ConfigParser treats unterminated format specifiers
           as an exception condition, we consider them to be plain
           text.  The fact that there are no more format specifier
           endings means we're done parsing. */
        parse_from = NULL;
    }

  if (buf != NULL)
    {
      /* Copy the remainder of the plain text. */
      svn_stringbuf_appendcstr(buf, copy_from);
      *opt_x_valuep = buf->data;
    }
  else
    *opt_x_valuep = NULL;

  /* Expansion has a well-defined answer. */
  return TRUE;
}

static cfg_section_t *
svn_config_addsection(svn_config_t *cfg,
                      const char *section)
{
  cfg_section_t *s;
  const char *hash_key;

  s = apr_palloc(cfg->pool, sizeof(cfg_section_t));
  s->name = apr_pstrdup(cfg->pool, section);
  if(cfg->section_names_case_sensitive)
    hash_key = s->name;
  else
    hash_key = make_hash_key(apr_pstrdup(cfg->pool, section));
  s->options = svn_hash__make(cfg->pool);

  svn_hash_sets(cfg->sections, hash_key, s);

  return s;
}

static void
svn_config_create_option(cfg_option_t **opt,
                         const char *option,
                         const char *value,
                         svn_boolean_t option_names_case_sensitive,
                         apr_pool_t *pool)
{
  cfg_option_t *o;

  o = apr_palloc(pool, sizeof(cfg_option_t));
  o->name = apr_pstrdup(pool, option);
  if(option_names_case_sensitive)
    o->hash_key = o->name;
  else
    o->hash_key = make_hash_key(apr_pstrdup(pool, option));

  o->value = apr_pstrdup(pool, value);
  o->x_value = NULL;
  o->state = option_state_needs_expanding;

  *opt = o;
}

svn_boolean_t
svn_config__is_expanded(svn_config_t *cfg,
                        const char *section,
                        const char *option)
{
  cfg_option_t *opt;

  if (cfg == NULL)
    return FALSE;

  /* does the option even exist? */
  opt = find_option(cfg, section, option, NULL);
  if (opt == NULL)
    return FALSE;

  /* already expanded? */
  if (   opt->state == option_state_expanded
      || opt->state == option_state_cyclic)
    return TRUE;

  /* needs expansion? */
  if (opt->value && strchr(opt->value, '%'))
    return FALSE;

  /* no expansion necessary */
  return TRUE;
}


void
svn_config_get(svn_config_t *cfg, const char **valuep,
               const char *section, const char *option,
               const char *default_value)
{
  *valuep = default_value;
  if (cfg)
    {
      cfg_section_t *sec;
      cfg_option_t *opt = find_option(cfg, section, option, &sec);
      if (opt != NULL)
        {
          make_string_from_option(valuep, cfg, sec, opt, NULL);
        }
      else
        /* before attempting to expand an option, check for the placeholder.
         * If there is none, there is no point in calling expand_option_value.
         */
        if (default_value && strchr(default_value, '%'))
          {
            apr_pool_t *tmp_pool = svn_pool_create(cfg->pool);
            const char *x_default;
            if (!expand_option_value(cfg, sec, default_value, &x_default,
                                     tmp_pool))
              {
                /* Recursive definitions are not supported.
                   Normalize the answer in that case. */
                *valuep = "";
              }
            else if (x_default)
              {
                svn_stringbuf_set(cfg->tmp_value, x_default);
                *valuep = cfg->tmp_value->data;
              }
            svn_pool_destroy(tmp_pool);
          }
    }
}



void
svn_config_set(svn_config_t *cfg,
               const char *section, const char *option,
               const char *value)
{
  cfg_section_t *sec;
  cfg_option_t *opt;

  /* Ignore write attempts to r/o configurations.
   *
   * Since we should never try to modify r/o data, trigger an assertion
   * in debug mode.
   */
#ifdef SVN_DEBUG
  SVN_ERR_ASSERT_NO_RETURN(!cfg->read_only);
#endif
  if (cfg->read_only)
    return;

  remove_expansions(cfg);

  opt = find_option(cfg, section, option, &sec);
  if (opt != NULL)
    {
      /* Replace the option's value. */
      opt->value = apr_pstrdup(cfg->pool, value);
      opt->state = option_state_needs_expanding;
      return;
    }

  /* Create a new option */
  svn_config_create_option(&opt, option, value,
                           cfg->option_names_case_sensitive,
                           cfg->pool);

  if (sec == NULL)
    {
      /* Even the section doesn't exist. Create it. */
      sec = svn_config_addsection(cfg, section);
    }

  svn_hash_sets(sec->options, opt->hash_key, opt);
}



/* Set *BOOLP to true or false depending (case-insensitively) on INPUT.
   If INPUT is null, set *BOOLP to DEFAULT_VALUE.

   INPUT is a string indicating truth or falsehood in any of the usual
   ways: "true"/"yes"/"on"/etc, "false"/"no"/"off"/etc.

   If INPUT is neither NULL nor a recognized string, return an error
   with code SVN_ERR_BAD_CONFIG_VALUE; use SECTION and OPTION in
   constructing the error string. */
static svn_error_t *
get_bool(svn_boolean_t *boolp, const char *input, svn_boolean_t default_value,
         const char *section, const char *option)
{
  svn_tristate_t value = svn_tristate__from_word(input);

  if (value == svn_tristate_true)
    *boolp = TRUE;
  else if (value == svn_tristate_false)
    *boolp = FALSE;
  else if (input == NULL) /* no value provided */
    *boolp = default_value;

  else if (section) /* unrecognized value */
    return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                             _("Config error: invalid boolean "
                               "value '%s' for '[%s] %s'"),
                             input, section, option);
  else
    return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                             _("Config error: invalid boolean "
                               "value '%s' for '%s'"),
                             input, option);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_config_get_bool(svn_config_t *cfg, svn_boolean_t *valuep,
                    const char *section, const char *option,
                    svn_boolean_t default_value)
{
  const char *tmp_value;
  svn_config_get(cfg, &tmp_value, section, option, NULL);
  return get_bool(valuep, tmp_value, default_value, section, option);
}



void
svn_config_set_bool(svn_config_t *cfg,
                    const char *section, const char *option,
                    svn_boolean_t value)
{
  svn_config_set(cfg, section, option,
                 (value ? SVN_CONFIG_TRUE : SVN_CONFIG_FALSE));
}

svn_error_t *
svn_config_get_int64(svn_config_t *cfg,
                     apr_int64_t *valuep,
                     const char *section,
                     const char *option,
                     apr_int64_t default_value)
{
  const char *tmp_value;
  svn_config_get(cfg, &tmp_value, section, option, NULL);
  if (tmp_value)
    return svn_cstring_strtoi64(valuep, tmp_value,
                                APR_INT64_MIN, APR_INT64_MAX, 10);

  *valuep = default_value;
  return SVN_NO_ERROR;
}

void
svn_config_set_int64(svn_config_t *cfg,
                     const char *section,
                     const char *option,
                     apr_int64_t value)
{
  svn_config_set(cfg, section, option,
                 apr_psprintf(cfg->pool, "%" APR_INT64_T_FMT, value));
}

svn_error_t *
svn_config_get_yes_no_ask(svn_config_t *cfg, const char **valuep,
                          const char *section, const char *option,
                          const char* default_value)
{
  const char *tmp_value;

  svn_config_get(cfg, &tmp_value, section, option, NULL);

  if (! tmp_value)
    tmp_value = default_value;

  if (tmp_value && (0 == svn_cstring_casecmp(tmp_value, SVN_CONFIG_ASK)))
    {
      *valuep = SVN_CONFIG_ASK;
    }
  else
    {
      svn_boolean_t bool_val;
      /* We already incorporated default_value into tmp_value if
         necessary, so the FALSE below will be ignored unless the
         caller is doing something it shouldn't be doing. */
      SVN_ERR(get_bool(&bool_val, tmp_value, FALSE, section, option));
      *valuep = bool_val ? SVN_CONFIG_TRUE : SVN_CONFIG_FALSE;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_config_get_tristate(svn_config_t *cfg, svn_tristate_t *valuep,
                        const char *section, const char *option,
                        const char *unknown_value,
                        svn_tristate_t default_value)
{
  const char *tmp_value;

  svn_config_get(cfg, &tmp_value, section, option, NULL);

  if (! tmp_value)
    {
      *valuep = default_value;
    }
  else if (0 == svn_cstring_casecmp(tmp_value, unknown_value))
    {
      *valuep = svn_tristate_unknown;
    }
  else
    {
      svn_boolean_t bool_val;
      /* We already incorporated default_value into tmp_value if
         necessary, so the FALSE below will be ignored unless the
         caller is doing something it shouldn't be doing. */
      SVN_ERR(get_bool(&bool_val, tmp_value, FALSE, section, option));
      *valuep = bool_val ? svn_tristate_true : svn_tristate_false;
    }

  return SVN_NO_ERROR;
}

int
svn_config_enumerate_sections(svn_config_t *cfg,
                              svn_config_section_enumerator_t callback,
                              void *baton)
{
  apr_hash_index_t *sec_ndx;
  int count = 0;
  apr_pool_t *subpool = svn_pool_create(cfg->x_pool);

  for (sec_ndx = apr_hash_first(subpool, cfg->sections);
       sec_ndx != NULL;
       sec_ndx = apr_hash_next(sec_ndx))
    {
      void *sec_ptr;
      cfg_section_t *sec;

      apr_hash_this(sec_ndx, NULL, NULL, &sec_ptr);
      sec = sec_ptr;
      ++count;
      if (!callback(sec->name, baton))
        break;
    }

  svn_pool_destroy(subpool);
  return count;
}


int
svn_config_enumerate_sections2(svn_config_t *cfg,
                               svn_config_section_enumerator2_t callback,
                               void *baton, apr_pool_t *pool)
{
  apr_hash_index_t *sec_ndx;
  apr_pool_t *iteration_pool;
  int count = 0;

  iteration_pool = svn_pool_create(pool);
  for (sec_ndx = apr_hash_first(pool, cfg->sections);
       sec_ndx != NULL;
       sec_ndx = apr_hash_next(sec_ndx))
    {
      cfg_section_t *sec = apr_hash_this_val(sec_ndx);

      ++count;
      svn_pool_clear(iteration_pool);
      if (!callback(sec->name, baton, iteration_pool))
        break;
    }
  svn_pool_destroy(iteration_pool);

  return count;
}



int
svn_config_enumerate(svn_config_t *cfg, const char *section,
                     svn_config_enumerator_t callback, void *baton)
{
  cfg_section_t *sec;
  apr_hash_index_t *opt_ndx;
  int count;
  apr_pool_t *subpool;

  find_option(cfg, section, NULL, &sec);
  if (sec == NULL)
    return 0;

  subpool = svn_pool_create(cfg->pool);
  count = 0;
  for (opt_ndx = apr_hash_first(subpool, sec->options);
       opt_ndx != NULL;
       opt_ndx = apr_hash_next(opt_ndx))
    {
      cfg_option_t *opt = apr_hash_this_val(opt_ndx);
      const char *temp_value;

      ++count;
      make_string_from_option(&temp_value, cfg, sec, opt, NULL);
      if (!callback(opt->name, temp_value, baton))
        break;
    }

  svn_pool_destroy(subpool);
  return count;
}


int
svn_config_enumerate2(svn_config_t *cfg, const char *section,
                      svn_config_enumerator2_t callback, void *baton,
                      apr_pool_t *pool)
{
  cfg_section_t *sec;
  apr_hash_index_t *opt_ndx;
  apr_pool_t *iteration_pool;
  int count;

  find_option(cfg, section, NULL, &sec);
  if (sec == NULL)
    return 0;

  iteration_pool = svn_pool_create(pool);
  count = 0;
  for (opt_ndx = apr_hash_first(pool, sec->options);
       opt_ndx != NULL;
       opt_ndx = apr_hash_next(opt_ndx))
    {
      cfg_option_t *opt = apr_hash_this_val(opt_ndx);
      const char *temp_value;

      ++count;
      make_string_from_option(&temp_value, cfg, sec, opt, NULL);
      svn_pool_clear(iteration_pool);
      if (!callback(opt->name, temp_value, baton, iteration_pool))
        break;
    }
  svn_pool_destroy(iteration_pool);

  return count;
}



/* Baton for search_groups() */
struct search_groups_baton
{
  const char *key;          /* Provided by caller of svn_config_find_group */
  const char *match;        /* Filled in by search_groups */
  apr_pool_t *pool;
};


/* This is an `svn_config_enumerator_t' function, and BATON is a
 * `struct search_groups_baton *'.
 */
static svn_boolean_t search_groups(const char *name,
                                   const char *value,
                                   void *baton,
                                   apr_pool_t *pool)
{
  struct search_groups_baton *b = baton;
  apr_array_header_t *list;

  list = svn_cstring_split(value, ",", TRUE, pool);
  if (svn_cstring_match_glob_list(b->key, list))
    {
      /* Fill in the match and return false, to stop enumerating. */
      b->match = apr_pstrdup(b->pool, name);
      return FALSE;
    }
  else
    return TRUE;
}


const char *svn_config_find_group(svn_config_t *cfg, const char *key,
                                  const char *master_section,
                                  apr_pool_t *pool)
{
  struct search_groups_baton gb;

  gb.key = key;
  gb.match = NULL;
  gb.pool = pool;
  (void) svn_config_enumerate2(cfg, master_section, search_groups, &gb, pool);
  return gb.match;
}


const char*
svn_config_get_server_setting(svn_config_t *cfg,
                              const char* server_group,
                              const char* option_name,
                              const char* default_value)
{
  const char *retval;
  svn_config_get(cfg, &retval, SVN_CONFIG_SECTION_GLOBAL,
                 option_name, default_value);
  if (server_group)
    {
      svn_config_get(cfg, &retval, server_group, option_name, retval);
    }
  return retval;
}


svn_error_t *
svn_config_dup(svn_config_t **cfgp,
               const svn_config_t *src,
               apr_pool_t *pool)
{
  apr_hash_index_t *sectidx;
  apr_hash_index_t *optidx;

  *cfgp = 0;
  SVN_ERR(svn_config_create2(cfgp, FALSE, FALSE, pool));

  (*cfgp)->x_values = src->x_values;
  (*cfgp)->section_names_case_sensitive = src->section_names_case_sensitive;
  (*cfgp)->option_names_case_sensitive = src->option_names_case_sensitive;

  for (sectidx = apr_hash_first(pool, src->sections);
       sectidx != NULL;
       sectidx = apr_hash_next(sectidx))
  {
    const void *sectkey;
    void *sectval;
    apr_ssize_t sectkeyLength;
    cfg_section_t * srcsect;
    cfg_section_t * destsec;

    apr_hash_this(sectidx, &sectkey, &sectkeyLength, &sectval);
    srcsect = sectval;

    destsec = svn_config_addsection(*cfgp, srcsect->name);

    for (optidx = apr_hash_first(pool, srcsect->options);
         optidx != NULL;
         optidx = apr_hash_next(optidx))
    {
      const void *optkey;
      void *optval;
      apr_ssize_t optkeyLength;
      cfg_option_t *srcopt;
      cfg_option_t *destopt;

      apr_hash_this(optidx, &optkey, &optkeyLength, &optval);
      srcopt = optval;

      svn_config_create_option(&destopt, srcopt->name, srcopt->value,
                               (*cfgp)->option_names_case_sensitive,
                               pool);

      destopt->value = apr_pstrdup(pool, srcopt->value);
      destopt->x_value = apr_pstrdup(pool, srcopt->x_value);
      destopt->state = srcopt->state;
      apr_hash_set(destsec->options,
                   apr_pstrdup(pool, (const char*)optkey),
                   optkeyLength, destopt);
    }
  }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_config_copy_config(apr_hash_t **cfg_hash,
                       apr_hash_t *src_hash,
                       apr_pool_t *pool)
{
  apr_hash_index_t *cidx;

  *cfg_hash = svn_hash__make(pool);
  for (cidx = apr_hash_first(pool, src_hash);
       cidx != NULL;
       cidx = apr_hash_next(cidx))
  {
    const void *ckey;
    void *cval;
    apr_ssize_t ckeyLength;
    svn_config_t * srcconfig;
    svn_config_t * destconfig;

    apr_hash_this(cidx, &ckey, &ckeyLength, &cval);
    srcconfig = cval;

    SVN_ERR(svn_config_dup(&destconfig, srcconfig, pool));

    apr_hash_set(*cfg_hash,
                 apr_pstrdup(pool, (const char*)ckey),
                 ckeyLength, destconfig);
  }

  return SVN_NO_ERROR;
}

svn_error_t*
svn_config_get_server_setting_int(svn_config_t *cfg,
                                  const char *server_group,
                                  const char *option_name,
                                  apr_int64_t default_value,
                                  apr_int64_t *result_value,
                                  apr_pool_t *pool)
{
  const char* tmp_value;
  char *end_pos;

  tmp_value = svn_config_get_server_setting(cfg, server_group,
                                            option_name, NULL);
  if (tmp_value == NULL)
    *result_value = default_value;
  else
    {
      /* read tmp_value as an int now */
      *result_value = apr_strtoi64(tmp_value, &end_pos, 0);

      if (*end_pos != 0)
        {
          return svn_error_createf
            (SVN_ERR_BAD_CONFIG_VALUE, NULL,
             _("Config error: invalid integer value '%s'"),
             tmp_value);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_config_get_server_setting_bool(svn_config_t *cfg,
                                   svn_boolean_t *valuep,
                                   const char *server_group,
                                   const char *option_name,
                                   svn_boolean_t default_value)
{
  const char* tmp_value;
  tmp_value = svn_config_get_server_setting(cfg, server_group,
                                            option_name, NULL);
  return get_bool(valuep, tmp_value, default_value,
                  server_group, option_name);
}


svn_boolean_t
svn_config_has_section(svn_config_t *cfg, const char *section)
{
  return NULL != get_hash_value(cfg->sections, cfg->tmp_key, section,
                                cfg->section_names_case_sensitive);
}

svn_error_t *
svn_config__write(svn_stream_t *stream,
                  const struct svn_config_t *cfg,
                  apr_pool_t *scratch_pool)
{
  apr_hash_index_t *section_i;
  apr_hash_index_t *options_i;
  apr_pool_t *section_pool = svn_pool_create(scratch_pool);
  apr_pool_t *options_pool = svn_pool_create(scratch_pool);

  for (section_i = apr_hash_first(scratch_pool, cfg->sections);
       section_i != NULL;
       section_i = apr_hash_next(section_i))
    {
      cfg_section_t *section = apr_hash_this_val(section_i);
      svn_pool_clear(section_pool);
      SVN_ERR(svn_stream_printf(stream, section_pool, "\n[%s]\n",
                                section->name));

      for (options_i = apr_hash_first(section_pool, section->options);
           options_i != NULL;
           options_i = apr_hash_next(options_i))
        {
          cfg_option_t *option = apr_hash_this_val(options_i);
          svn_pool_clear(options_pool);
          SVN_ERR(svn_stream_printf(stream, options_pool, "%s=%s\n",
                                    option->name, option->value));
        }
    }

  svn_pool_destroy(section_pool);
  svn_pool_destroy(options_pool);

  return SVN_NO_ERROR;
}

