/*
 * hash.c :  dumping and reading hash tables to/from files.
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



#include <stdlib.h>
#include <limits.h>

#include <apr_version.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>

#ifndef SVN_HASH__GETS_SETS
#define SVN_HASH__GETS_SETS
#endif
#include "svn_hash.h"

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_sorts.h"
#include "svn_io.h"
#include "svn_pools.h"

#include "private/svn_dep_compat.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"

#include "svn_private_config.h"



/*
 * The format of a dumped hash table is:
 *
 *   K <nlength>
 *   name (a string of <nlength> bytes, followed by a newline)
 *   V <vlength>
 *   val (a string of <vlength> bytes, followed by a newline)
 *   [... etc, etc ...]
 *   END
 *
 *
 * (Yes, there is a newline after END.)
 *
 * For example:
 *
 *   K 5
 *   color
 *   V 3
 *   red
 *   K 11
 *   wine review
 *   V 376
 *   A forthright entrance, yet coquettish on the tongue, its deceptively
 *   fruity exterior hides the warm mahagony undercurrent that is the
 *   hallmark of Chateau Fraisant-Pitre.  Connoisseurs of the region will
 *   be pleased to note the familiar, subtle hints of mulberries and
 *   carburator fluid.  Its confident finish is marred only by a barely
 *   detectable suggestion of rancid squid ink.
 *   K 5
 *   price
 *   V 8
 *   US $6.50
 *   END
 *
 */




/*** Dumping and loading hash files. */

/* Implements svn_hash_read2 and svn_hash_read_incremental. */
svn_error_t *
svn_hash__read_entry(svn_hash__entry_t *entry,
                     svn_stream_t *stream,
                     const char *terminator,
                     svn_boolean_t incremental,
                     apr_pool_t *pool)
{
  svn_stringbuf_t *buf;
  svn_boolean_t eof;
  apr_size_t len;
  char c;

  svn_error_t *err;
  apr_uint64_t ui64;

  /* Read a key length line.  Might be END, though. */
  SVN_ERR(svn_stream_readline(stream, &buf, "\n", &eof, pool));

  /* Check for the end of the hash. */
  if ((!terminator && eof && buf->len == 0)
      || (terminator && (strcmp(buf->data, terminator) == 0)))
  {
    entry->key = NULL;
    entry->keylen = 0;
    entry->val = NULL;
    entry->vallen = 0;

    return SVN_NO_ERROR;
  }

  /* Check for unexpected end of stream */
  if (eof)
    return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                            _("Serialized hash missing terminator"));

  if ((buf->len >= 3) && (buf->data[0] == 'K') && (buf->data[1] == ' '))
    {
      /* Get the length of the key */
      err = svn_cstring_strtoui64(&ui64, buf->data + 2,
                                  0, APR_SIZE_MAX, 10);
      if (err)
        return svn_error_create(SVN_ERR_MALFORMED_FILE, err,
                                _("Serialized hash malformed key length"));
      entry->keylen = (apr_size_t)ui64;

      /* Now read that much into a buffer. */
      entry->key = apr_palloc(pool, entry->keylen + 1);
      SVN_ERR(svn_stream_read_full(stream, entry->key, &entry->keylen));
      entry->key[entry->keylen] = '\0';

      /* Suck up extra newline after key data */
      len = 1;
      SVN_ERR(svn_stream_read_full(stream, &c, &len));
      if (c != '\n')
        return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                                _("Serialized hash malformed key data"));

      /* Read a val length line */
      SVN_ERR(svn_stream_readline(stream, &buf, "\n", &eof, pool));

      if ((buf->data[0] == 'V') && (buf->data[1] == ' '))
        {
          /* Get the length of the val */
          err = svn_cstring_strtoui64(&ui64, buf->data + 2,
                                      0, APR_SIZE_MAX, 10);
          if (err)
            return svn_error_create(SVN_ERR_MALFORMED_FILE, err,
                                    _("Serialized hash malformed value length"));
          entry->vallen = (apr_size_t)ui64;

          entry->val = apr_palloc(pool, entry->vallen + 1);
          SVN_ERR(svn_stream_read_full(stream, entry->val, &entry->vallen));
          entry->val[entry->vallen] = '\0';

          /* Suck up extra newline after val data */
          len = 1;
          SVN_ERR(svn_stream_read_full(stream, &c, &len));
          if (c != '\n')
            return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                                    _("Serialized hash malformed value data"));
        }
      else
        return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                                _("Serialized hash malformed"));
    }
  else if (incremental && (buf->len >= 3)
           && (buf->data[0] == 'D') && (buf->data[1] == ' '))
    {
      /* Get the length of the key */
      err = svn_cstring_strtoui64(&ui64, buf->data + 2,
                                  0, APR_SIZE_MAX, 10);
      if (err)
        return svn_error_create(SVN_ERR_MALFORMED_FILE, err,
                                _("Serialized hash malformed key length"));
      entry->keylen = (apr_size_t)ui64;

      /* Now read that much into a buffer. */
      entry->key = apr_palloc(pool, entry->keylen + 1);
      SVN_ERR(svn_stream_read_full(stream, entry->key, &entry->keylen));
      entry->key[entry->keylen] = '\0';

      /* Suck up extra newline after key data */
      len = 1;
      SVN_ERR(svn_stream_read_full(stream, &c, &len));
      if (c != '\n')
        return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                                _("Serialized hash malformed key data"));

      /* Remove this hash entry. */
      entry->vallen = 0;
      entry->val = NULL;
    }
  else
    {
      return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                              _("Serialized hash malformed"));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
hash_read(apr_hash_t *hash, svn_stream_t *stream, const char *terminator,
          svn_boolean_t incremental, apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);

  while (1)
    {
      svn_hash__entry_t entry;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_hash__read_entry(&entry, stream, terminator,
                                   incremental, iterpool));

      /* end of hash? */
      if (entry.key == NULL)
        break;

      if (entry.val)
        {
          /* Add a new hash entry. */
          apr_hash_set(hash, apr_pstrmemdup(pool, entry.key, entry.keylen),
                       entry.keylen,
                       svn_string_ncreate(entry.val, entry.vallen, pool));
        }
      else
        {
          /* Remove this hash entry. */
          apr_hash_set(hash, entry.key, entry.keylen, NULL);
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/* Implements svn_hash_write2 and svn_hash_write_incremental. */
static svn_error_t *
hash_write(apr_hash_t *hash, apr_hash_t *oldhash, svn_stream_t *stream,
           const char *terminator, apr_pool_t *pool)
{
  apr_pool_t *subpool;
  apr_size_t len;
  apr_array_header_t *list;
  int i;

  subpool = svn_pool_create(pool);

  list = svn_sort__hash(hash, svn_sort_compare_items_lexically, pool);
  for (i = 0; i < list->nelts; i++)
    {
      svn_sort__item_t *item = &APR_ARRAY_IDX(list, i, svn_sort__item_t);
      svn_string_t *valstr = item->value;

      svn_pool_clear(subpool);

      /* Don't output entries equal to the ones in oldhash, if present. */
      if (oldhash)
        {
          svn_string_t *oldstr = apr_hash_get(oldhash, item->key, item->klen);

          if (oldstr && svn_string_compare(valstr, oldstr))
            continue;
        }

      if (item->klen < 0)
        return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                                _("Cannot serialize negative length"));

      /* Write it out. */
      SVN_ERR(svn_stream_printf(stream, subpool,
                                "K %" APR_SIZE_T_FMT "\n%s\n"
                                "V %" APR_SIZE_T_FMT "\n",
                                (apr_size_t) item->klen,
                                (const char *) item->key,
                                valstr->len));
      len = valstr->len;
      SVN_ERR(svn_stream_write(stream, valstr->data, &len));
      SVN_ERR(svn_stream_puts(stream, "\n"));
    }

  if (oldhash)
    {
      /* Output a deletion entry for each property in oldhash but not hash. */
      list = svn_sort__hash(oldhash, svn_sort_compare_items_lexically,
                            pool);
      for (i = 0; i < list->nelts; i++)
        {
          svn_sort__item_t *item = &APR_ARRAY_IDX(list, i, svn_sort__item_t);

          svn_pool_clear(subpool);

          /* If it's not present in the new hash, write out a D entry. */
          if (! apr_hash_get(hash, item->key, item->klen))
            SVN_ERR(svn_stream_printf(stream, subpool,
                                      "D %" APR_SSIZE_T_FMT "\n%s\n",
                                      item->klen, (const char *) item->key));
        }
    }

  if (terminator)
    SVN_ERR(svn_stream_printf(stream, subpool, "%s\n", terminator));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


svn_error_t *svn_hash_read2(apr_hash_t *hash, svn_stream_t *stream,
                            const char *terminator, apr_pool_t *pool)
{
  return hash_read(hash, stream, terminator, FALSE, pool);
}


svn_error_t *svn_hash_read_incremental(apr_hash_t *hash,
                                       svn_stream_t *stream,
                                       const char *terminator,
                                       apr_pool_t *pool)
{
  return hash_read(hash, stream, terminator, TRUE, pool);
}


svn_error_t *
svn_hash_write2(apr_hash_t *hash, svn_stream_t *stream,
                const char *terminator, apr_pool_t *pool)
{
  return hash_write(hash, NULL, stream, terminator, pool);
}


svn_error_t *
svn_hash_write_incremental(apr_hash_t *hash, apr_hash_t *oldhash,
                           svn_stream_t *stream, const char *terminator,
                           apr_pool_t *pool)
{
  SVN_ERR_ASSERT(oldhash != NULL);
  return hash_write(hash, oldhash, stream, terminator, pool);
}


svn_error_t *
svn_hash_write(apr_hash_t *hash, apr_file_t *destfile, apr_pool_t *pool)
{
  return hash_write(hash, NULL, svn_stream_from_aprfile2(destfile, TRUE, pool),
                    SVN_HASH_TERMINATOR, pool);
}


/* There are enough quirks in the deprecated svn_hash_read that we
   should just preserve its implementation. */
svn_error_t *
svn_hash_read(apr_hash_t *hash,
              apr_file_t *srcfile,
              apr_pool_t *pool)
{
  svn_error_t *err;
  char buf[SVN_KEYLINE_MAXLEN];
  apr_size_t num_read;
  char c;
  int first_time = 1;


  while (1)
    {
      /* Read a key length line.  Might be END, though. */
      apr_size_t len = sizeof(buf);

      err = svn_io_read_length_line(srcfile, buf, &len, pool);
      if (err && APR_STATUS_IS_EOF(err->apr_err) && first_time)
        {
          /* We got an EOF on our very first attempt to read, which
             means it's a zero-byte file.  No problem, just go home. */
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      else if (err)
        /* Any other circumstance is a genuine error. */
        return err;

      first_time = 0;

      if (((len == 3) && (buf[0] == 'E') && (buf[1] == 'N') && (buf[2] == 'D'))
          || ((len == 9)
              && (buf[0] == 'P')
              && (buf[1] == 'R')       /* We formerly used just "END" to */
              && (buf[2] == 'O')       /* end a property hash, but later */
              && (buf[3] == 'P')       /* we added "PROPS-END", so that  */
              && (buf[4] == 'S')       /* the fs dump format would be    */
              && (buf[5] == '-')       /* more human-readable.  That's   */
              && (buf[6] == 'E')       /* why we accept either way here. */
              && (buf[7] == 'N')
              && (buf[8] == 'D')))
        {
          /* We've reached the end of the dumped hash table, so leave. */
          return SVN_NO_ERROR;
        }
      else if ((buf[0] == 'K') && (buf[1] == ' '))
        {
          size_t keylen;
          int parsed_len;
          void *keybuf;

          /* Get the length of the key */
          SVN_ERR(svn_cstring_atoi(&parsed_len, buf + 2));
          keylen = parsed_len;

          /* Now read that much into a buffer, + 1 byte for null terminator */
          keybuf = apr_palloc(pool, keylen + 1);
          SVN_ERR(svn_io_file_read_full2(srcfile,
                                         keybuf, keylen,
                                         &num_read, NULL, pool));
          ((char *) keybuf)[keylen] = '\0';

          /* Suck up extra newline after key data */
          SVN_ERR(svn_io_file_getc(&c, srcfile, pool));
          if (c != '\n')
            return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL, NULL);

          /* Read a val length line */
          len = sizeof(buf);
          SVN_ERR(svn_io_read_length_line(srcfile, buf, &len, pool));

          if ((buf[0] == 'V') && (buf[1] == ' '))
            {
              svn_string_t *value = apr_palloc(pool, sizeof(*value));
              apr_size_t vallen;
              void *valbuf;

              /* Get the length of the value */
              SVN_ERR(svn_cstring_atoi(&parsed_len, buf + 2));
              vallen = parsed_len;

              /* Again, 1 extra byte for the null termination. */
              valbuf = apr_palloc(pool, vallen + 1);
              SVN_ERR(svn_io_file_read_full2(srcfile,
                                             valbuf, vallen,
                                             &num_read, NULL, pool));
              ((char *) valbuf)[vallen] = '\0';

              /* Suck up extra newline after val data */
              SVN_ERR(svn_io_file_getc(&c, srcfile, pool));
              if (c != '\n')
                return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL, NULL);

              value->data = valbuf;
              value->len = vallen;

              /* The Grand Moment:  add a new hash entry! */
              apr_hash_set(hash, keybuf, keylen, value);
            }
          else
            {
              return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL, NULL);
            }
        }
      else
        {
          return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL, NULL);
        }
    } /* while (1) */
}



/*** Diffing hashes ***/

svn_error_t *
svn_hash_diff(apr_hash_t *hash_a,
              apr_hash_t *hash_b,
              svn_hash_diff_func_t diff_func,
              void *diff_func_baton,
              apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  if (hash_a)
    for (hi = apr_hash_first(pool, hash_a); hi; hi = apr_hash_next(hi))
      {
        const void *key;
        apr_ssize_t klen;

        apr_hash_this(hi, &key, &klen, NULL);

        if (hash_b && (apr_hash_get(hash_b, key, klen)))
          SVN_ERR((*diff_func)(key, klen, svn_hash_diff_key_both,
                               diff_func_baton));
        else
          SVN_ERR((*diff_func)(key, klen, svn_hash_diff_key_a,
                               diff_func_baton));
      }

  if (hash_b)
    for (hi = apr_hash_first(pool, hash_b); hi; hi = apr_hash_next(hi))
      {
        const void *key;
        apr_ssize_t klen;

        apr_hash_this(hi, &key, &klen, NULL);

        if (! (hash_a && apr_hash_get(hash_a, key, klen)))
          SVN_ERR((*diff_func)(key, klen, svn_hash_diff_key_b,
                               diff_func_baton));
      }

  return SVN_NO_ERROR;
}


/*** Misc. hash APIs ***/

svn_error_t *
svn_hash_keys(apr_array_header_t **array,
              apr_hash_t *hash,
              apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *array = apr_array_make(pool, apr_hash_count(hash), sizeof(const char *));

  for (hi = apr_hash_first(pool, hash); hi; hi = apr_hash_next(hi))
    {
      APR_ARRAY_PUSH(*array, const char *) = apr_hash_this_key(hi);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_hash_from_cstring_keys(apr_hash_t **hash_p,
                           const apr_array_header_t *keys,
                           apr_pool_t *pool)
{
  int i;
  apr_hash_t *hash = svn_hash__make(pool);
  for (i = 0; i < keys->nelts; i++)
    {
      const char *key =
        apr_pstrdup(pool, APR_ARRAY_IDX(keys, i, const char *));
      svn_hash_sets(hash, key, key);
    }
  *hash_p = hash;
  return SVN_NO_ERROR;
}


void *
svn_hash__gets_debug(apr_hash_t *ht, const char *key)
{
  return apr_hash_get(ht, key, APR_HASH_KEY_STRING);
}


void
svn_hash__sets_debug(apr_hash_t *ht, const char *key, const void *val)
{
  apr_hash_set(ht, key, APR_HASH_KEY_STRING, val);
}



/*** Specialized getter APIs ***/

const char *
svn_hash__get_cstring(apr_hash_t *hash,
                      const char *key,
                      const char *default_value)
{
  if (hash)
    {
      const char *value = svn_hash_gets(hash, key);
      return value ? value : default_value;
    }

  return default_value;
}


svn_boolean_t
svn_hash__get_bool(apr_hash_t *hash, const char *key,
                   svn_boolean_t default_value)
{
  const char *tmp_value = svn_hash__get_cstring(hash, key, NULL);
  svn_tristate_t value = svn_tristate__from_word(tmp_value);

  if (value == svn_tristate_true)
    return TRUE;
  else if (value == svn_tristate_false)
    return FALSE;

  return default_value;
}



/*** Optimized hash function ***/

/* apr_hashfunc_t optimized for the key that we use in SVN: paths and
 * property names.  Its primary goal is speed for keys of known length.
 *
 * Since strings tend to spawn large value spaces (usually differ in many
 * bits with differences spanning a larger section of the key), we can be
 * quite sloppy extracting a hash value.  The more keys there are in a
 * hash container, the more bits of the value returned by this function
 * will be used.  For a small number of string keys, choosing bits from any
 * any fix location close to the tail of those keys would usually be good
 * enough to prevent high collision rates.
 */
static unsigned int
hashfunc_compatible(const char *char_key, apr_ssize_t *klen)
{
    unsigned int hash = 0;
    const unsigned char *key = (const unsigned char *)char_key;
    const unsigned char *p;
    apr_ssize_t i;

    if (*klen == APR_HASH_KEY_STRING)
      *klen = strlen(char_key);

#if SVN_UNALIGNED_ACCESS_IS_OK
    for (p = key, i = *klen; i >= 4; i-=4, p+=4)
      {
        apr_uint32_t chunk = *(const apr_uint32_t *)p;

        /* the ">> 17" part gives upper bits in the chunk a chance to make
           some impact as well */
        hash = hash * 33 * 33 * 33 * 33 + chunk + (chunk >> 17);
      }
#else
    for (p = key, i = *klen; i >= 4; i-=4, p+=4)
      {
        hash = hash * 33 * 33 * 33 * 33
              + p[0] * 33 * 33 * 33
              + p[1] * 33 * 33
              + p[2] * 33
              + p[3];
      }
#endif
    for (; i; i--, p++)
        hash = hash * 33 + *p;

    return hash;
}

apr_hash_t *
svn_hash__make(apr_pool_t *pool)
{
  return apr_hash_make_custom(pool, hashfunc_compatible);
}
