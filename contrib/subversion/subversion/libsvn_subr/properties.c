/*
 * properties.c:  stuff related to Subversion properties
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



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <string.h>       /* for strncmp() */
#include "svn_hash.h"
#include "svn_string.h"
#include "svn_props.h"
#include "svn_error.h"
#include "svn_ctype.h"
#include "private/svn_subr_private.h"


/* All Subversion-specific versioned node properties
 * known to this client, that are applicable to both a file and a dir.
 */
#define SVN_PROP__NODE_COMMON_PROPS SVN_PROP_MERGEINFO, \
                                    SVN_PROP_TEXT_TIME, \
                                    SVN_PROP_OWNER, \
                                    SVN_PROP_GROUP, \
                                    SVN_PROP_UNIX_MODE,

/* All Subversion-specific versioned node properties
 * known to this client, that are applicable to a dir only.
 */
#define SVN_PROP__NODE_DIR_ONLY_PROPS SVN_PROP_IGNORE, \
                                      SVN_PROP_INHERITABLE_IGNORES, \
                                      SVN_PROP_INHERITABLE_AUTO_PROPS, \
                                      SVN_PROP_EXTERNALS,

/* All Subversion-specific versioned node properties
 * known to this client, that are applicable to a file only.
 */
#define SVN_PROP__NODE_FILE_ONLY_PROPS SVN_PROP_MIME_TYPE, \
                                       SVN_PROP_EOL_STYLE, \
                                       SVN_PROP_KEYWORDS, \
                                       SVN_PROP_EXECUTABLE, \
                                       SVN_PROP_NEEDS_LOCK, \
                                       SVN_PROP_SPECIAL,

static const char *const known_rev_props[]
 = { SVN_PROP_REVISION_ALL_PROPS
     NULL };

static const char *const known_node_props[]
 = { SVN_PROP__NODE_COMMON_PROPS
     SVN_PROP__NODE_DIR_ONLY_PROPS
     SVN_PROP__NODE_FILE_ONLY_PROPS
     NULL };

static const char *const known_dir_props[]
 = { SVN_PROP__NODE_COMMON_PROPS
     SVN_PROP__NODE_DIR_ONLY_PROPS
     NULL };

static const char *const known_file_props[]
 = { SVN_PROP__NODE_COMMON_PROPS
     SVN_PROP__NODE_FILE_ONLY_PROPS
     NULL };

static svn_boolean_t
is_known_prop(const char *prop_name,
              const char *const *known_props)
{
  while (*known_props)
    {
      if (strcmp(prop_name, *known_props++) == 0)
        return TRUE;
    }
  return FALSE;
}

svn_boolean_t
svn_prop_is_known_svn_rev_prop(const char *prop_name)
{
  return is_known_prop(prop_name, known_rev_props);
}

svn_boolean_t
svn_prop_is_known_svn_node_prop(const char *prop_name)
{
  return is_known_prop(prop_name, known_node_props);
}

svn_boolean_t
svn_prop_is_known_svn_file_prop(const char *prop_name)
{
  return is_known_prop(prop_name, known_file_props);
}

svn_boolean_t
svn_prop_is_known_svn_dir_prop(const char *prop_name)
{
  return is_known_prop(prop_name, known_dir_props);
}


svn_boolean_t
svn_prop_is_svn_prop(const char *prop_name)
{
  return strncmp(prop_name, SVN_PROP_PREFIX, (sizeof(SVN_PROP_PREFIX) - 1))
         == 0;
}


svn_boolean_t
svn_prop_has_svn_prop(const apr_hash_t *props, apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  if (! props)
    return FALSE;

  for (hi = apr_hash_first(pool, (apr_hash_t *)props); hi;
       hi = apr_hash_next(hi))
    {
      const char *prop_name = apr_hash_this_key(hi);

      if (svn_prop_is_svn_prop(prop_name))
        return TRUE;
    }

  return FALSE;
}


#define SIZEOF_WC_PREFIX (sizeof(SVN_PROP_WC_PREFIX) - 1)
#define SIZEOF_ENTRY_PREFIX (sizeof(SVN_PROP_ENTRY_PREFIX) - 1)

svn_prop_kind_t
svn_property_kind2(const char *prop_name)
{

  if (strncmp(prop_name, SVN_PROP_WC_PREFIX, SIZEOF_WC_PREFIX) == 0)
    return svn_prop_wc_kind;

  if (strncmp(prop_name, SVN_PROP_ENTRY_PREFIX, SIZEOF_ENTRY_PREFIX) == 0)
    return svn_prop_entry_kind;

  return svn_prop_regular_kind;
}


/* NOTE: this function is deprecated, but we cannot move it to deprecated.c
   because we need the SIZEOF_*_PREFIX constant symbols defined above.  */
svn_prop_kind_t
svn_property_kind(int *prefix_len,
                  const char *prop_name)
{
  svn_prop_kind_t kind = svn_property_kind2(prop_name);

  if (prefix_len)
    {
      if (kind == svn_prop_wc_kind)
        *prefix_len = SIZEOF_WC_PREFIX;
      else if (kind == svn_prop_entry_kind)
        *prefix_len = SIZEOF_ENTRY_PREFIX;
      else
        *prefix_len = 0;
    }

  return kind;
}


svn_error_t *
svn_categorize_props(const apr_array_header_t *proplist,
                     apr_array_header_t **entry_props,
                     apr_array_header_t **wc_props,
                     apr_array_header_t **regular_props,
                     apr_pool_t *pool)
{
  int i;
  if (entry_props)
    *entry_props = apr_array_make(pool, 1, sizeof(svn_prop_t));
  if (wc_props)
    *wc_props = apr_array_make(pool, 1, sizeof(svn_prop_t));
  if (regular_props)
    *regular_props = apr_array_make(pool, 1, sizeof(svn_prop_t));

  for (i = 0; i < proplist->nelts; i++)
    {
      svn_prop_t *prop, *newprop;
      enum svn_prop_kind kind;

      prop = &APR_ARRAY_IDX(proplist, i, svn_prop_t);
      kind = svn_property_kind2(prop->name);
      newprop = NULL;

      if (kind == svn_prop_regular_kind)
        {
          if (regular_props)
            newprop = apr_array_push(*regular_props);
        }
      else if (kind == svn_prop_wc_kind)
        {
          if (wc_props)
            newprop = apr_array_push(*wc_props);
        }
      else if (kind == svn_prop_entry_kind)
        {
          if (entry_props)
            newprop = apr_array_push(*entry_props);
        }
      else
        /* Technically this can't happen, but might as well have the
           code ready in case that ever changes. */
        return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                                 "Bad property kind for property '%s'",
                                 prop->name);

      if (newprop)
        {
          newprop->name = prop->name;
          newprop->value = prop->value;
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_prop_diffs(apr_array_header_t **propdiffs,
               const apr_hash_t *target_props,
               const apr_hash_t *source_props,
               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *ary = apr_array_make(pool, 1, sizeof(svn_prop_t));

  /* Note: we will be storing the pointers to the keys (from the hashes)
     into the propdiffs array.  It is acceptable for us to
     reference the same memory as the base/target_props hash. */

  /* Loop over SOURCE_PROPS and examine each key.  This will allow us to
     detect any `deletion' events or `set-modification' events.  */
  for (hi = apr_hash_first(pool, (apr_hash_t *)source_props); hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_string_t *propval1, *propval2;

      /* Get next property */
      apr_hash_this(hi, &key, &klen, &val);
      propval1 = val;

      /* Does property name exist in TARGET_PROPS? */
      propval2 = apr_hash_get((apr_hash_t *)target_props, key, klen);

      if (propval2 == NULL)
        {
          /* Add a delete event to the array */
          svn_prop_t *p = apr_array_push(ary);
          p->name = key;
          p->value = NULL;
        }
      else if (! svn_string_compare(propval1, propval2))
        {
          /* Add a set (modification) event to the array */
          svn_prop_t *p = apr_array_push(ary);
          p->name = key;
          p->value = svn_string_dup(propval2, pool);
        }
    }

  /* Loop over TARGET_PROPS and examine each key.  This allows us to
     detect `set-creation' events */
  for (hi = apr_hash_first(pool, (apr_hash_t *)target_props); hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_string_t *propval;

      /* Get next property */
      apr_hash_this(hi, &key, &klen, &val);
      propval = val;

      /* Does property name exist in SOURCE_PROPS? */
      if (NULL == apr_hash_get((apr_hash_t *)source_props, key, klen))
        {
          /* Add a set (creation) event to the array */
          svn_prop_t *p = apr_array_push(ary);
          p->name = key;
          p->value = svn_string_dup(propval, pool);
        }
    }

  /* Done building our array of user events. */
  *propdiffs = ary;

  return SVN_NO_ERROR;
}

apr_hash_t *
svn_prop__patch(const apr_hash_t *original_props,
                const apr_array_header_t *prop_changes,
                apr_pool_t *pool)
{
  apr_hash_t *props = apr_hash_copy(pool, original_props);
  int i;

  for (i = 0; i < prop_changes->nelts; i++)
    {
      const svn_prop_t *p = &APR_ARRAY_IDX(prop_changes, i, svn_prop_t);

      svn_hash_sets(props, p->name, p->value);
    }
  return props;
}

/**
 * Reallocate the members of PROP using POOL.
 */
static void
svn_prop__members_dup(svn_prop_t *prop, apr_pool_t *pool)
{
  if (prop->name)
    prop->name = apr_pstrdup(pool, prop->name);
  if (prop->value)
    prop->value = svn_string_dup(prop->value, pool);
}

svn_prop_t *
svn_prop_dup(const svn_prop_t *prop, apr_pool_t *pool)
{
  svn_prop_t *new_prop = apr_palloc(pool, sizeof(*new_prop));

  *new_prop = *prop;

  svn_prop__members_dup(new_prop, pool);

  return new_prop;
}

apr_array_header_t *
svn_prop_array_dup(const apr_array_header_t *array, apr_pool_t *pool)
{
  int i;
  apr_array_header_t *new_array = apr_array_copy(pool, array);
  for (i = 0; i < new_array->nelts; ++i)
    {
      svn_prop_t *elt = &APR_ARRAY_IDX(new_array, i, svn_prop_t);
      svn_prop__members_dup(elt, pool);
    }
  return new_array;
}

apr_array_header_t *
svn_prop_hash_to_array(const apr_hash_t *hash,
                       apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *array = apr_array_make(pool,
                                             apr_hash_count((apr_hash_t *)hash),
                                             sizeof(svn_prop_t));

  for (hi = apr_hash_first(pool, (apr_hash_t *)hash); hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      svn_prop_t prop;

      apr_hash_this(hi, &key, NULL, &val);
      prop.name = key;
      prop.value = val;
      APR_ARRAY_PUSH(array, svn_prop_t) = prop;
    }

  return array;
}

apr_hash_t *
svn_prop_hash_dup(const apr_hash_t *hash,
                  apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *new_hash = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, (apr_hash_t *)hash); hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *prop;

      apr_hash_this(hi, &key, &klen, &prop);
      apr_hash_set(new_hash, apr_pstrmemdup(pool, key, klen), klen,
                   svn_string_dup(prop, pool));
    }
  return new_hash;
}

apr_hash_t *
svn_prop_array_to_hash(const apr_array_header_t *properties,
                       apr_pool_t *pool)
{
  int i;
  apr_hash_t *prop_hash = apr_hash_make(pool);

  for (i = 0; i < properties->nelts; i++)
    {
      const svn_prop_t *prop = &APR_ARRAY_IDX(properties, i, svn_prop_t);
      svn_hash_sets(prop_hash, prop->name, prop->value);
    }

  return prop_hash;
}

svn_boolean_t
svn_prop_is_boolean(const char *prop_name)
{
  /* If we end up with more than 3 of these, we should probably put
     them in a table and use bsearch.  With only three, it doesn't
     make any speed difference.  */
  if (strcmp(prop_name, SVN_PROP_EXECUTABLE) == 0
      || strcmp(prop_name, SVN_PROP_NEEDS_LOCK) == 0
      || strcmp(prop_name, SVN_PROP_SPECIAL) == 0)
    return TRUE;
  return FALSE;
}


svn_boolean_t
svn_prop_needs_translation(const char *propname)
{
  /* ### Someday, we may want to be picky and choosy about which
     properties require UTF8 and EOL conversion.  For now, all "svn:"
     props need it.  */

  return svn_prop_is_svn_prop(propname);
}


svn_boolean_t
svn_prop_name_is_valid(const char *prop_name)
{
  const char *p = prop_name;

  /* The characters we allow use identical representations in UTF8
     and ASCII, so we can just test for the appropriate ASCII codes.
     But we can't use standard C character notation ('A', 'B', etc)
     because there's no guarantee that this C environment is using
     ASCII. */

  if (!(svn_ctype_isalpha(*p)
        || *p == SVN_CTYPE_ASCII_COLON
        || *p == SVN_CTYPE_ASCII_UNDERSCORE))
    return FALSE;
  p++;
  for (; *p; p++)
    {
      if (!(svn_ctype_isalnum(*p)
            || *p == SVN_CTYPE_ASCII_MINUS
            || *p == SVN_CTYPE_ASCII_DOT
            || *p == SVN_CTYPE_ASCII_COLON
            || *p == SVN_CTYPE_ASCII_UNDERSCORE))
        return FALSE;
    }
  return TRUE;
}

const char *
svn_prop_get_value(const apr_hash_t *props,
                   const char *prop_name)
{
  svn_string_t *str;

  if (!props)
    return NULL;

  str = svn_hash_gets((apr_hash_t *)props, prop_name);

  if (str)
    return str->data;

  return NULL;
}
