/* reps-strings.c : intepreting representations with respect to strings
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

#include "svn_fs.h"
#include "svn_pools.h"

#include "fs.h"
#include "err.h"
#include "trail.h"
#include "reps-strings.h"

#include "bdb/reps-table.h"
#include "bdb/strings-table.h"

#include "../libsvn_fs/fs-loader.h"
#define SVN_WANT_BDB
#include "svn_private_config.h"


/*** Helper Functions ***/


/* Return non-zero iff REP is mutable under transaction TXN_ID. */
static svn_boolean_t rep_is_mutable(representation_t *rep,
                                    const char *txn_id)
{
  if ((! rep->txn_id) || (strcmp(rep->txn_id, txn_id) != 0))
    return FALSE;
  return TRUE;
}

/* Helper macro that evaluates to an error message indicating that
   the representation referred to by X has an unknown node kind. */
#define UNKNOWN_NODE_KIND(x)                                   \
  svn_error_createf                                            \
    (SVN_ERR_FS_CORRUPT, NULL,                                 \
     _("Unknown node kind for representation '%s'"), x)

/* Return a `fulltext' representation, allocated in POOL, which
 * references the string STR_KEY.
 *
 * If TXN_ID is non-zero and non-NULL, make the representation mutable
 * under that TXN_ID.
 *
 * If STR_KEY is non-null, copy it into an allocation from POOL.
 *
 * If MD5_CHECKSUM is non-null, use it as the MD5 checksum for the new
 * rep; else initialize the rep with an all-zero (i.e., always
 * successful) MD5 checksum.
 *
 * If SHA1_CHECKSUM is non-null, use it as the SHA1 checksum for the new
 * rep; else initialize the rep with an all-zero (i.e., always
 * successful) SHA1 checksum.
 */
static representation_t *
make_fulltext_rep(const char *str_key,
                  const char *txn_id,
                  svn_checksum_t *md5_checksum,
                  svn_checksum_t *sha1_checksum,
                  apr_pool_t *pool)

{
  representation_t *rep = apr_pcalloc(pool, sizeof(*rep));
  if (txn_id && *txn_id)
    rep->txn_id = apr_pstrdup(pool, txn_id);
  rep->kind = rep_kind_fulltext;
  rep->md5_checksum = svn_checksum_dup(md5_checksum, pool);
  rep->sha1_checksum = svn_checksum_dup(sha1_checksum, pool);
  rep->contents.fulltext.string_key
    = str_key ? apr_pstrdup(pool, str_key) : NULL;
  return rep;
}


/* Set *KEYS to an array of string keys gleaned from `delta'
   representation REP.  Allocate *KEYS in POOL. */
static svn_error_t *
delta_string_keys(apr_array_header_t **keys,
                  const representation_t *rep,
                  apr_pool_t *pool)
{
  const char *key;
  int i;
  apr_array_header_t *chunks;

  if (rep->kind != rep_kind_delta)
    return svn_error_create
      (SVN_ERR_FS_GENERAL, NULL,
       _("Representation is not of type 'delta'"));

  /* Set up a convenience variable. */
  chunks = rep->contents.delta.chunks;

  /* Initialize *KEYS to an empty array. */
  *keys = apr_array_make(pool, chunks->nelts, sizeof(key));
  if (! chunks->nelts)
    return SVN_NO_ERROR;

  /* Now, push the string keys for each window into *KEYS */
  for (i = 0; i < chunks->nelts; i++)
    {
      rep_delta_chunk_t *chunk = APR_ARRAY_IDX(chunks, i, rep_delta_chunk_t *);

      key = apr_pstrdup(pool, chunk->string_key);
      APR_ARRAY_PUSH(*keys, const char *) = key;
    }

  return SVN_NO_ERROR;
}


/* Delete the strings associated with array KEYS in FS as part of TRAIL.  */
static svn_error_t *
delete_strings(const apr_array_header_t *keys,
               svn_fs_t *fs,
               trail_t *trail,
               apr_pool_t *pool)
{
  int i;
  const char *str_key;
  apr_pool_t *subpool = svn_pool_create(pool);

  for (i = 0; i < keys->nelts; i++)
    {
      svn_pool_clear(subpool);
      str_key = APR_ARRAY_IDX(keys, i, const char *);
      SVN_ERR(svn_fs_bdb__string_delete(fs, str_key, trail, subpool));
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}



/*** Reading the contents from a representation. ***/

struct compose_handler_baton
{
  /* The combined window, and the pool it's allocated from. */
  svn_txdelta_window_t *window;
  apr_pool_t *window_pool;

  /* If the incoming window was self-compressed, and the combined WINDOW
     exists from previous iterations, SOURCE_BUF will point to the
     expanded self-compressed window. */
  char *source_buf;

  /* The trail for this operation. WINDOW_POOL will be a child of
     TRAIL->pool. No allocations will be made from TRAIL->pool itself. */
  trail_t *trail;

  /* TRUE when no more windows have to be read/combined. */
  svn_boolean_t done;

  /* TRUE if we've just started reading a new window. We need this
     because the svndiff handler will push a NULL window at the end of
     the stream, and we have to ignore that; but we must also know
     when it's appropriate to push a NULL window at the combiner. */
  svn_boolean_t init;
};


/* Handle one window. If BATON is emtpy, copy the WINDOW into it;
   otherwise, combine WINDOW with the one in BATON, unless WINDOW
   is self-compressed (i.e., does not copy from the source view),
   in which case expand. */

static svn_error_t *
compose_handler(svn_txdelta_window_t *window, void *baton)
{
  struct compose_handler_baton *cb = baton;
  SVN_ERR_ASSERT(!cb->done || window == NULL);
  SVN_ERR_ASSERT(cb->trail && cb->trail->pool);

  if (!cb->init && !window)
    return SVN_NO_ERROR;

  /* We should never get here if we've already expanded a
     self-compressed window. */
  SVN_ERR_ASSERT(!cb->source_buf);

  if (cb->window)
    {
      if (window && (window->sview_len == 0 || window->src_ops == 0))
        {
          /* This is a self-compressed window. Don't combine it with
             the others, because the combiner may go quadratic. Instead,
             expand it here and signal that the combination has
             ended. */
          apr_size_t source_len = window->tview_len;
          SVN_ERR_ASSERT(cb->window->sview_len == source_len);
          cb->source_buf = apr_palloc(cb->window_pool, source_len);
          svn_txdelta_apply_instructions(window, NULL,
                                         cb->source_buf, &source_len);
          cb->done = TRUE;
        }
      else
        {
          /* Combine the incoming window with whatever's in the baton. */
          apr_pool_t *composite_pool = svn_pool_create(cb->trail->pool);
          svn_txdelta_window_t *composite;

          composite = svn_txdelta_compose_windows(window, cb->window,
                                                  composite_pool);
          svn_pool_destroy(cb->window_pool);
          cb->window = composite;
          cb->window_pool = composite_pool;
          cb->done = (composite->sview_len == 0 || composite->src_ops == 0);
        }
    }
  else if (window)
    {
      /* Copy the (first) window into the baton. */
      apr_pool_t *window_pool = svn_pool_create(cb->trail->pool);
      SVN_ERR_ASSERT(cb->window_pool == NULL);
      cb->window = svn_txdelta_window_dup(window, window_pool);
      cb->window_pool = window_pool;
      cb->done = (window->sview_len == 0 || window->src_ops == 0);
    }
  else
    cb->done = TRUE;

  cb->init = FALSE;
  return SVN_NO_ERROR;
}



/* Read one delta window from REP[CUR_CHUNK] and push it at the
   composition handler. */

static svn_error_t *
get_one_window(struct compose_handler_baton *cb,
               svn_fs_t *fs,
               representation_t *rep,
               int cur_chunk)
{
  svn_stream_t *wstream;
  char diffdata[4096];   /* hunk of svndiff data */
  svn_filesize_t off;    /* offset into svndiff data */
  apr_size_t amt;        /* how much svndiff data to/was read */
  const char *str_key;

  apr_array_header_t *chunks = rep->contents.delta.chunks;
  rep_delta_chunk_t *this_chunk, *first_chunk;

  cb->init = TRUE;
  if (chunks->nelts <= cur_chunk)
    return compose_handler(NULL, cb);

  /* Set up a window handling stream for the svndiff data. */
  wstream = svn_txdelta_parse_svndiff(compose_handler, cb, TRUE,
                                      cb->trail->pool);

  /* First things first:  send the "SVN"{version} header through the
     stream.  ### For now, we will just use the version specified
     in the first chunk, and then verify that no chunks have a
     different version number than the one used.  In the future,
     we might simply convert chunks that use a different version
     of the diff format -- or, heck, a different format
     altogether -- to the format/version of the first chunk.  */
  first_chunk = APR_ARRAY_IDX(chunks, 0, rep_delta_chunk_t*);
  diffdata[0] = 'S';
  diffdata[1] = 'V';
  diffdata[2] = 'N';
  diffdata[3] = (char) (first_chunk->version);
  amt = 4;
  SVN_ERR(svn_stream_write(wstream, diffdata, &amt));
  /* FIXME: The stream write handler is borked; assert (amt == 4); */

  /* Get this string key which holds this window's data.
     ### todo: make sure this is an `svndiff' DIFF skel here. */
  this_chunk = APR_ARRAY_IDX(chunks, cur_chunk, rep_delta_chunk_t*);
  str_key = this_chunk->string_key;

  /* Run through the svndiff data, at least as far as necessary. */
  off = 0;
  do
    {
      amt = sizeof(diffdata);
      SVN_ERR(svn_fs_bdb__string_read(fs, str_key, diffdata,
                                      off, &amt, cb->trail,
                                      cb->trail->pool));
      off += amt;
      SVN_ERR(svn_stream_write(wstream, diffdata, &amt));
    }
  while (amt != 0);
  SVN_ERR(svn_stream_close(wstream));

  SVN_ERR_ASSERT(!cb->init);
  SVN_ERR_ASSERT(cb->window != NULL);
  SVN_ERR_ASSERT(cb->window_pool != NULL);
  return SVN_NO_ERROR;
}


/* Undeltify a range of data. DELTAS is the set of delta windows to
   combine, FULLTEXT is the source text, CUR_CHUNK is the index of the
   delta chunk we're starting from. OFFSET is the relative offset of
   the requested data within the chunk; BUF and LEN are what we're
   undeltifying to. */

static svn_error_t *
rep_undeltify_range(svn_fs_t *fs,
                    const apr_array_header_t *deltas,
                    representation_t *fulltext,
                    int cur_chunk,
                    char *buf,
                    apr_size_t offset,
                    apr_size_t *len,
                    trail_t *trail,
                    apr_pool_t *pool)
{
  apr_size_t len_read = 0;

  do
    {
      struct compose_handler_baton cb = { 0 };
      char *source_buf, *target_buf;
      apr_size_t target_len;
      int cur_rep;

      cb.trail = trail;
      cb.done = FALSE;
      for (cur_rep = 0; !cb.done && cur_rep < deltas->nelts; ++cur_rep)
        {
          representation_t *const rep =
            APR_ARRAY_IDX(deltas, cur_rep, representation_t*);
          SVN_ERR(get_one_window(&cb, fs, rep, cur_chunk));
        }

      if (!cb.window)
          /* That's it, no more source data is available. */
          break;

      /* The source view length should not be 0 if there are source
         copy ops in the window. */
      SVN_ERR_ASSERT(cb.window->sview_len > 0 || cb.window->src_ops == 0);

      /* cb.window is the combined delta window. Read the source text
         into a buffer. */
      if (cb.source_buf)
        {
          /* The combiner already created the source text from a
             self-compressed window. */
          source_buf = cb.source_buf;
        }
      else if (fulltext && cb.window->sview_len > 0 && cb.window->src_ops > 0)
        {
          apr_size_t source_len = cb.window->sview_len;
          source_buf = apr_palloc(cb.window_pool, source_len);
          SVN_ERR(svn_fs_bdb__string_read
                  (fs, fulltext->contents.fulltext.string_key,
                   source_buf, cb.window->sview_offset, &source_len,
                   trail, pool));
          if (source_len != cb.window->sview_len)
            return svn_error_create
                (SVN_ERR_FS_CORRUPT, NULL,
                 _("Svndiff source length inconsistency"));
        }
      else
        {
          source_buf = NULL;    /* Won't read anything from here. */
        }

      if (offset > 0)
        {
          target_len = *len - len_read + offset;
          target_buf = apr_palloc(cb.window_pool, target_len);
        }
      else
        {
          target_len = *len - len_read;
          target_buf = buf;
        }

      svn_txdelta_apply_instructions(cb.window, source_buf,
                                     target_buf, &target_len);
      if (offset > 0)
        {
          SVN_ERR_ASSERT(target_len > offset);
          target_len -= offset;
          memcpy(buf, target_buf + offset, target_len);
          offset = 0; /* Read from the beginning of the next chunk. */
        }
      /* Don't need this window any more. */
      svn_pool_destroy(cb.window_pool);

      len_read += target_len;
      buf += target_len;
      ++cur_chunk;
    }
  while (len_read < *len);

  *len = len_read;
  return SVN_NO_ERROR;
}



/* Calculate the index of the chunk in REP that contains REP_OFFSET,
   and find the relative CHUNK_OFFSET within the chunk.
   Return -1 if offset is beyond the end of the represented data.
   ### The basic assumption is that all delta windows are the same size
   and aligned at the same offset, so this number is the same in all
   dependent deltas.  Oh, and the chunks in REP must be ordered. */

static int
get_chunk_offset(representation_t *rep,
                 svn_filesize_t rep_offset,
                 apr_size_t *chunk_offset)
{
  const apr_array_header_t *chunks = rep->contents.delta.chunks;
  int cur_chunk;
  assert(chunks->nelts);

  /* ### Yes, this is a linear search.  I'll change this to bisection
     the very second we notice it's slowing us down. */
  for (cur_chunk = 0; cur_chunk < chunks->nelts; ++cur_chunk)
  {
    const rep_delta_chunk_t *const this_chunk
      = APR_ARRAY_IDX(chunks, cur_chunk, rep_delta_chunk_t*);

    if ((this_chunk->offset + this_chunk->size) > rep_offset)
      {
        assert(this_chunk->offset <= rep_offset);
        assert(rep_offset - this_chunk->offset < SVN_MAX_OBJECT_SIZE);
        *chunk_offset = (apr_size_t) (rep_offset - this_chunk->offset);
        return cur_chunk;
      }
  }

  return -1;
}

/* Copy into BUF *LEN bytes starting at OFFSET from the string
   represented via REP_KEY in FS, as part of TRAIL.
   The number of bytes actually copied is stored in *LEN.  */
static svn_error_t *
rep_read_range(svn_fs_t *fs,
               const char *rep_key,
               svn_filesize_t offset,
               char *buf,
               apr_size_t *len,
               trail_t *trail,
               apr_pool_t *pool)
{
  representation_t *rep;
  apr_size_t chunk_offset;

  /* Read in our REP. */
  SVN_ERR(svn_fs_bdb__read_rep(&rep, fs, rep_key, trail, pool));
  if (rep->kind == rep_kind_fulltext)
    {
      SVN_ERR(svn_fs_bdb__string_read(fs, rep->contents.fulltext.string_key,
                                      buf, offset, len, trail, pool));
    }
  else if (rep->kind == rep_kind_delta)
    {
      const int cur_chunk = get_chunk_offset(rep, offset, &chunk_offset);
      if (cur_chunk < 0)
        *len = 0;
      else
        {
          svn_error_t *err;
          /* Preserve for potential use in error message. */
          const char *first_rep_key = rep_key;
          /* Make a list of all the rep's we need to undeltify this range.
             We'll have to read them within this trail anyway, so we might
             as well do it once and up front. */
          apr_array_header_t *reps = apr_array_make(pool, 30, sizeof(rep));
          do
            {
              const rep_delta_chunk_t *const first_chunk
                = APR_ARRAY_IDX(rep->contents.delta.chunks,
                                0, rep_delta_chunk_t*);
              const rep_delta_chunk_t *const chunk
                = APR_ARRAY_IDX(rep->contents.delta.chunks,
                                cur_chunk, rep_delta_chunk_t*);

              /* Verify that this chunk is of the same version as the first. */
              if (first_chunk->version != chunk->version)
                return svn_error_createf
                  (SVN_ERR_FS_CORRUPT, NULL,
                   _("Diff version inconsistencies in representation '%s'"),
                   rep_key);

              rep_key = chunk->rep_key;
              APR_ARRAY_PUSH(reps, representation_t *) = rep;
              SVN_ERR(svn_fs_bdb__read_rep(&rep, fs, rep_key,
                                           trail, pool));
            }
          while (rep->kind == rep_kind_delta
                 && rep->contents.delta.chunks->nelts > cur_chunk);

          /* Right. We've either just read the fulltext rep, or a rep that's
             too short, in which case we'll undeltify without source data.*/
          if (rep->kind != rep_kind_delta && rep->kind != rep_kind_fulltext)
            return UNKNOWN_NODE_KIND(rep_key);

          if (rep->kind == rep_kind_delta)
            rep = NULL;         /* Don't use source data */

          err = rep_undeltify_range(fs, reps, rep, cur_chunk, buf,
                                    chunk_offset, len, trail, pool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_FS_CORRUPT)
                return svn_error_createf
                  (SVN_ERR_FS_CORRUPT, err,
                   _("Corruption detected whilst reading delta chain from "
                     "representation '%s' to '%s'"), first_rep_key, rep_key);
              else
                return svn_error_trace(err);
            }
        }
    }
  else /* unknown kind */
    return UNKNOWN_NODE_KIND(rep_key);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__get_mutable_rep(const char **new_rep_key,
                             const char *rep_key,
                             svn_fs_t *fs,
                             const char *txn_id,
                             trail_t *trail,
                             apr_pool_t *pool)
{
  representation_t *rep = NULL;
  const char *new_str = NULL;

  /* We were passed an existing REP_KEY, so examine it.  If it is
     mutable already, then just return REP_KEY as the mutable result
     key.  */
  if (rep_key && (rep_key[0] != '\0'))
    {
      SVN_ERR(svn_fs_bdb__read_rep(&rep, fs, rep_key, trail, pool));
      if (rep_is_mutable(rep, txn_id))
        {
          *new_rep_key = rep_key;
          return SVN_NO_ERROR;
        }
    }

  /* Either we weren't provided a base key to examine, or the base key
     we were provided was not mutable.  So, let's make a new
     representation and return its key to the caller. */
  SVN_ERR(svn_fs_bdb__string_append(fs, &new_str, 0, NULL, trail, pool));
  rep = make_fulltext_rep(new_str, txn_id,
                          svn_checksum_empty_checksum(svn_checksum_md5,
                                                      pool),
                          svn_checksum_empty_checksum(svn_checksum_sha1,
                                                      pool),
                          pool);
  return svn_fs_bdb__write_new_rep(new_rep_key, fs, rep, trail, pool);
}


svn_error_t *
svn_fs_base__delete_rep_if_mutable(svn_fs_t *fs,
                                   const char *rep_key,
                                   const char *txn_id,
                                   trail_t *trail,
                                   apr_pool_t *pool)
{
  representation_t *rep;

  SVN_ERR(svn_fs_bdb__read_rep(&rep, fs, rep_key, trail, pool));
  if (! rep_is_mutable(rep, txn_id))
    return SVN_NO_ERROR;

  if (rep->kind == rep_kind_fulltext)
    {
      SVN_ERR(svn_fs_bdb__string_delete(fs,
                                        rep->contents.fulltext.string_key,
                                        trail, pool));
    }
  else if (rep->kind == rep_kind_delta)
    {
      apr_array_header_t *keys;
      SVN_ERR(delta_string_keys(&keys, rep, pool));
      SVN_ERR(delete_strings(keys, fs, trail, pool));
    }
  else /* unknown kind */
    return UNKNOWN_NODE_KIND(rep_key);

  return svn_fs_bdb__delete_rep(fs, rep_key, trail, pool);
}



/*** Reading and writing data via representations. ***/

/** Reading. **/

struct rep_read_baton
{
  /* The FS from which we're reading. */
  svn_fs_t *fs;

  /* The representation skel whose contents we want to read.  If this
     is NULL, the rep has never had any contents, so all reads fetch 0
     bytes.

     Formerly, we cached the entire rep skel here, not just the key.
     That way we didn't have to fetch the rep from the db every time
     we want to read a little bit more of the file.  Unfortunately,
     this has a problem: if, say, a file's representation changes
     while we're reading (changes from fulltext to delta, for
     example), we'll never know it.  So for correctness, we now
     refetch the representation skel every time we want to read
     another chunk.  */
  const char *rep_key;

  /* How many bytes have been read already. */
  svn_filesize_t offset;

  /* If present, the read will be done as part of this trail, and the
     trail's pool will be used.  Otherwise, see `pool' below.  */
  trail_t *trail;

  /* MD5 checksum context.  Initialized when the baton is created, updated as
     we read data, and finalized when the stream is closed. */
  svn_checksum_ctx_t *md5_checksum_ctx;

  /* Final resting place of the checksum created by md5_checksum_cxt. */
  svn_checksum_t *md5_checksum;

  /* SHA1 checksum context.  Initialized when the baton is created, updated as
     we read data, and finalized when the stream is closed. */
  svn_checksum_ctx_t *sha1_checksum_ctx;

  /* Final resting place of the checksum created by sha1_checksum_cxt. */
  svn_checksum_t *sha1_checksum;

  /* The length of the rep's contents (as fulltext, that is,
     independent of how the rep actually stores the data.)  This is
     retrieved when the baton is created, and used to determine when
     we have read the last byte, at which point we compare checksums.

     Getting this at baton creation time makes interleaved reads and
     writes on the same rep in the same trail impossible.  But we're
     not doing that, and probably no one ever should.  And anyway if
     they do, they should see problems immediately. */
  svn_filesize_t size;

  /* Set to FALSE when the baton is created, TRUE when the checksum_ctx
     is digestified. */
  svn_boolean_t checksum_finalized;

  /* Used for temporary allocations.  This pool is cleared at the
     start of each invocation of the relevant stream read function --
     see rep_read_contents().  */
  apr_pool_t *scratch_pool;

};


static svn_error_t *
rep_read_get_baton(struct rep_read_baton **rb_p,
                   svn_fs_t *fs,
                   const char *rep_key,
                   svn_boolean_t use_trail_for_reads,
                   trail_t *trail,
                   apr_pool_t *pool)
{
  struct rep_read_baton *b;

  b = apr_pcalloc(pool, sizeof(*b));
  b->md5_checksum_ctx = svn_checksum_ctx_create(svn_checksum_md5, pool);
  b->sha1_checksum_ctx = svn_checksum_ctx_create(svn_checksum_sha1, pool);

  if (rep_key)
    SVN_ERR(svn_fs_base__rep_contents_size(&(b->size), fs, rep_key,
                                           trail, pool));
  else
    b->size = 0;

  b->checksum_finalized = FALSE;
  b->fs = fs;
  b->trail = use_trail_for_reads ? trail : NULL;
  b->scratch_pool = svn_pool_create(pool);
  b->rep_key = rep_key;
  b->offset = 0;

  *rb_p = b;

  return SVN_NO_ERROR;
}



/*** Retrieving data. ***/

svn_error_t *
svn_fs_base__rep_contents_size(svn_filesize_t *size_p,
                               svn_fs_t *fs,
                               const char *rep_key,
                               trail_t *trail,
                               apr_pool_t *pool)
{
  representation_t *rep;

  SVN_ERR(svn_fs_bdb__read_rep(&rep, fs, rep_key, trail, pool));

  if (rep->kind == rep_kind_fulltext)
    {
      /* Get the size by asking Berkeley for the string's length. */
      SVN_ERR(svn_fs_bdb__string_size(size_p, fs,
                                      rep->contents.fulltext.string_key,
                                      trail, pool));
    }
  else if (rep->kind == rep_kind_delta)
    {
      /* Get the size by finding the last window pkg in the delta and
         adding its offset to its size.  This way, we won't even be
         messed up by overlapping windows, as long as the window pkgs
         are still ordered. */
      apr_array_header_t *chunks = rep->contents.delta.chunks;
      rep_delta_chunk_t *last_chunk;

      SVN_ERR_ASSERT(chunks->nelts);

      last_chunk = APR_ARRAY_IDX(chunks, chunks->nelts - 1,
                                 rep_delta_chunk_t *);
      *size_p = last_chunk->offset + last_chunk->size;
    }
  else /* unknown kind */
    return UNKNOWN_NODE_KIND(rep_key);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__rep_contents_checksums(svn_checksum_t **md5_checksum,
                                    svn_checksum_t **sha1_checksum,
                                    svn_fs_t *fs,
                                    const char *rep_key,
                                    trail_t *trail,
                                    apr_pool_t *pool)
{
  representation_t *rep;

  SVN_ERR(svn_fs_bdb__read_rep(&rep, fs, rep_key, trail, pool));
  if (md5_checksum)
    *md5_checksum = svn_checksum_dup(rep->md5_checksum, pool);
  if (sha1_checksum)
    *sha1_checksum = svn_checksum_dup(rep->sha1_checksum, pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__rep_contents(svn_string_t *str,
                          svn_fs_t *fs,
                          const char *rep_key,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  svn_filesize_t contents_size;
  apr_size_t len;
  char *data;

  SVN_ERR(svn_fs_base__rep_contents_size(&contents_size, fs, rep_key,
                                         trail, pool));

  /* What if the contents are larger than we can handle? */
  if (contents_size > SVN_MAX_OBJECT_SIZE)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       _("Rep contents are too large: "
         "got %s, limit is %s"),
       apr_psprintf(pool, "%" SVN_FILESIZE_T_FMT, contents_size),
       apr_psprintf(pool, "%" APR_SIZE_T_FMT, SVN_MAX_OBJECT_SIZE));
  else
    str->len = (apr_size_t) contents_size;

  data = apr_palloc(pool, str->len);
  str->data = data;
  len = str->len;
  SVN_ERR(rep_read_range(fs, rep_key, 0, data, &len, trail, pool));

  /* Paranoia. */
  if (len != str->len)
    return svn_error_createf
      (SVN_ERR_FS_CORRUPT, NULL,
       _("Failure reading representation '%s'"), rep_key);

  /* Just the standard paranoia. */
  {
    representation_t *rep;
    svn_checksum_t *checksum, *rep_checksum;

    SVN_ERR(svn_fs_bdb__read_rep(&rep, fs, rep_key, trail, pool));
    rep_checksum = rep->sha1_checksum ? rep->sha1_checksum : rep->md5_checksum;
    SVN_ERR(svn_checksum(&checksum, rep_checksum->kind, str->data, str->len,
                         pool));

    if (! svn_checksum_match(checksum, rep_checksum))
      return svn_error_create(SVN_ERR_FS_CORRUPT,
                svn_checksum_mismatch_err(rep_checksum, checksum, pool,
                            _("Checksum mismatch on representation '%s'"),
                            rep_key),
                NULL);
  }

  return SVN_NO_ERROR;
}


struct read_rep_args
{
  struct rep_read_baton *rb;   /* The data source.             */
  char *buf;                   /* Where to put what we read.   */
  apr_size_t *len;             /* How much to read / was read. */
};


/* BATON is of type `read_rep_args':

   Read into BATON->rb->buf the *(BATON->len) bytes starting at
   BATON->rb->offset from the data represented at BATON->rb->rep_key
   in BATON->rb->fs, as part of TRAIL.

   Afterwards, *(BATON->len) is the number of bytes actually read, and
   BATON->rb->offset is incremented by that amount.

   If BATON->rb->rep_key is null, this is assumed to mean the file's
   contents have no representation, i.e., the file has no contents.
   In that case, if BATON->rb->offset > 0, return the error
   SVN_ERR_FS_FILE_CONTENTS_CHANGED, else just set *(BATON->len) to
   zero and return.  */
static svn_error_t *
txn_body_read_rep(void *baton, trail_t *trail)
{
  struct read_rep_args *args = baton;

  if (args->rb->rep_key)
    {
      SVN_ERR(rep_read_range(args->rb->fs,
                             args->rb->rep_key,
                             args->rb->offset,
                             args->buf,
                             args->len,
                             trail,
                             args->rb->scratch_pool));

      args->rb->offset += *(args->len);

      /* We calculate the checksum just once, the moment we see the
       * last byte of data.  But we can't assume there was a short
       * read.  The caller may have known the length of the data and
       * requested exactly that amount, so there would never be a
       * short read.  (That's why the read baton has to know the
       * length of the data in advance.)
       *
       * On the other hand, some callers invoke the stream reader in a
       * loop whose termination condition is that the read returned
       * zero bytes of data -- which usually results in the read
       * function being called one more time *after* the call that got
       * a short read (indicating end-of-stream).
       *
       * The conditions below ensure that we compare checksums even
       * when there is no short read associated with the last byte of
       * data, while also ensuring that it's harmless to repeatedly
       * read 0 bytes from the stream.
       */
      if (! args->rb->checksum_finalized)
        {
          SVN_ERR(svn_checksum_update(args->rb->md5_checksum_ctx, args->buf,
                                      *(args->len)));
          SVN_ERR(svn_checksum_update(args->rb->sha1_checksum_ctx, args->buf,
                                      *(args->len)));

          if (args->rb->offset == args->rb->size)
            {
              representation_t *rep;

              SVN_ERR(svn_checksum_final(&args->rb->md5_checksum,
                                         args->rb->md5_checksum_ctx,
                                         trail->pool));
              SVN_ERR(svn_checksum_final(&args->rb->sha1_checksum,
                                         args->rb->sha1_checksum_ctx,
                                         trail->pool));
              args->rb->checksum_finalized = TRUE;

              SVN_ERR(svn_fs_bdb__read_rep(&rep, args->rb->fs,
                                           args->rb->rep_key,
                                           trail, trail->pool));

              if (rep->md5_checksum
                  && (! svn_checksum_match(rep->md5_checksum,
                                           args->rb->md5_checksum)))
                return svn_error_create(SVN_ERR_FS_CORRUPT,
                        svn_checksum_mismatch_err(rep->md5_checksum,
                             args->rb->md5_checksum, trail->pool,
                             _("MD5 checksum mismatch on representation '%s'"),
                             args->rb->rep_key),
                        NULL);

              if (rep->sha1_checksum
                  && (! svn_checksum_match(rep->sha1_checksum,
                                           args->rb->sha1_checksum)))
                return svn_error_createf(SVN_ERR_FS_CORRUPT,
                        svn_checksum_mismatch_err(rep->sha1_checksum,
                            args->rb->sha1_checksum, trail->pool,
                            _("SHA1 checksum mismatch on representation '%s'"),
                            args->rb->rep_key),
                        NULL);
            }
        }
    }
  else if (args->rb->offset > 0)
    {
      return
        svn_error_create
        (SVN_ERR_FS_REP_CHANGED, NULL,
         _("Null rep, but offset past zero already"));
    }
  else
    *(args->len) = 0;

  return SVN_NO_ERROR;
}


static svn_error_t *
rep_read_contents(void *baton, char *buf, apr_size_t *len)
{
  struct rep_read_baton *rb = baton;
  struct read_rep_args args;

  /* Clear the scratch pool of the results of previous invocations. */
  svn_pool_clear(rb->scratch_pool);

  args.rb = rb;
  args.buf = buf;
  args.len = len;

  /* If we got a trail, use it; else make one. */
  if (rb->trail)
    SVN_ERR(txn_body_read_rep(&args, rb->trail));
  else
    {
      /* In the case of reading from the db, any returned data should
         live in our pre-allocated buffer, so the whole operation can
         happen within a single malloc/free cycle.  This prevents us
         from creating millions of unnecessary trail subpools when
         reading a big file.  */
      SVN_ERR(svn_fs_base__retry_txn(rb->fs,
                                     txn_body_read_rep,
                                     &args,
                                     TRUE,
                                     rb->scratch_pool));
    }
  return SVN_NO_ERROR;
}


/** Writing. **/


struct rep_write_baton
{
  /* The FS in which we're writing. */
  svn_fs_t *fs;

  /* The representation skel whose contents we want to write. */
  const char *rep_key;

  /* The transaction id under which this write action will take
     place. */
  const char *txn_id;

  /* If present, do the write as part of this trail, and use trail's
     pool.  Otherwise, see `pool' below.  */
  trail_t *trail;

  /* SHA1 and MD5 checksums.  Initialized when the baton is created,
     updated as we write data, and finalized and stored when the
     stream is closed. */
  svn_checksum_ctx_t *md5_checksum_ctx;
  svn_checksum_t *md5_checksum;
  svn_checksum_ctx_t *sha1_checksum_ctx;
  svn_checksum_t *sha1_checksum;
  svn_boolean_t finalized;

  /* Used for temporary allocations, iff `trail' (above) is null.  */
  apr_pool_t *pool;

};


static struct rep_write_baton *
rep_write_get_baton(svn_fs_t *fs,
                    const char *rep_key,
                    const char *txn_id,
                    trail_t *trail,
                    apr_pool_t *pool)
{
  struct rep_write_baton *b;

  b = apr_pcalloc(pool, sizeof(*b));
  b->md5_checksum_ctx = svn_checksum_ctx_create(svn_checksum_md5, pool);
  b->sha1_checksum_ctx = svn_checksum_ctx_create(svn_checksum_sha1, pool);
  b->fs = fs;
  b->trail = trail;
  b->pool = pool;
  b->rep_key = rep_key;
  b->txn_id = txn_id;
  return b;
}



/* Write LEN bytes from BUF into the end of the string represented via
   REP_KEY in FS, as part of TRAIL.  If the representation is not
   mutable, return the error SVN_FS_REP_NOT_MUTABLE. */
static svn_error_t *
rep_write(svn_fs_t *fs,
          const char *rep_key,
          const char *buf,
          apr_size_t len,
          const char *txn_id,
          trail_t *trail,
          apr_pool_t *pool)
{
  representation_t *rep;

  SVN_ERR(svn_fs_bdb__read_rep(&rep, fs, rep_key, trail, pool));

  if (! rep_is_mutable(rep, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_REP_NOT_MUTABLE, NULL,
       _("Rep '%s' is not mutable"), rep_key);

  if (rep->kind == rep_kind_fulltext)
    {
      SVN_ERR(svn_fs_bdb__string_append
              (fs, &(rep->contents.fulltext.string_key), len, buf,
               trail, pool));
    }
  else if (rep->kind == rep_kind_delta)
    {
      /* There should never be a case when we have a mutable
         non-fulltext rep.  The only code that creates mutable reps is
         in this file, and it creates them fulltext. */
      return svn_error_createf
        (SVN_ERR_FS_CORRUPT, NULL,
         _("Rep '%s' both mutable and non-fulltext"), rep_key);
    }
  else /* unknown kind */
    return UNKNOWN_NODE_KIND(rep_key);

  return SVN_NO_ERROR;
}


struct write_rep_args
{
  struct rep_write_baton *wb;   /* Destination.       */
  const char *buf;              /* Data.              */
  apr_size_t len;               /* How much to write. */
};


/* BATON is of type `write_rep_args':
   Append onto BATON->wb->rep_key's contents BATON->len bytes of
   data from BATON->wb->buf, in BATON->rb->fs, as part of TRAIL.

   If the representation is not mutable, return the error
   SVN_FS_REP_NOT_MUTABLE.  */
static svn_error_t *
txn_body_write_rep(void *baton, trail_t *trail)
{
  struct write_rep_args *args = baton;

  SVN_ERR(rep_write(args->wb->fs,
                    args->wb->rep_key,
                    args->buf,
                    args->len,
                    args->wb->txn_id,
                    trail,
                    trail->pool));
  SVN_ERR(svn_checksum_update(args->wb->md5_checksum_ctx,
                              args->buf, args->len));
  SVN_ERR(svn_checksum_update(args->wb->sha1_checksum_ctx,
                              args->buf, args->len));
  return SVN_NO_ERROR;
}


static svn_error_t *
rep_write_contents(void *baton,
                   const char *buf,
                   apr_size_t *len)
{
  struct rep_write_baton *wb = baton;
  struct write_rep_args args;

  /* We toss LEN's indirectness because if not all the bytes are
     written, it's an error, so we wouldn't be reporting anything back
     through *LEN anyway. */
  args.wb = wb;
  args.buf = buf;
  args.len = *len;

  /* If we got a trail, use it; else make one. */
  if (wb->trail)
    SVN_ERR(txn_body_write_rep(&args, wb->trail));
  else
    {
      /* In the case of simply writing the rep to the db, we're
         *certain* that there's no data coming back to us that needs
         to be preserved... so the whole operation can happen within a
         single malloc/free cycle.  This prevents us from creating
         millions of unnecessary trail subpools when writing a big
         file. */
      SVN_ERR(svn_fs_base__retry_txn(wb->fs,
                                     txn_body_write_rep,
                                     &args,
                                     TRUE,
                                     wb->pool));
    }

  return SVN_NO_ERROR;
}


/* Helper for rep_write_close_contents(); see that doc string for
   more.  BATON is of type `struct rep_write_baton'. */
static svn_error_t *
txn_body_write_close_rep(void *baton, trail_t *trail)
{
  struct rep_write_baton *wb = baton;
  representation_t *rep;

  SVN_ERR(svn_fs_bdb__read_rep(&rep, wb->fs, wb->rep_key,
                               trail, trail->pool));
  rep->md5_checksum = svn_checksum_dup(wb->md5_checksum, trail->pool);
  rep->sha1_checksum = svn_checksum_dup(wb->sha1_checksum, trail->pool);
  return svn_fs_bdb__write_rep(wb->fs, wb->rep_key, rep,
                               trail, trail->pool);
}


/* BATON is of type `struct rep_write_baton'.
 *
 * Finalize BATON->md5_context and store the resulting digest under
 * BATON->rep_key.
 */
static svn_error_t *
rep_write_close_contents(void *baton)
{
  struct rep_write_baton *wb = baton;

  /* ### Thought: if we fixed apr-util MD5 contexts to allow repeated
     digestification, then we wouldn't need a stream close function at
     all -- instead, we could update the stored checksum each time a
     write occurred, which would have the added advantage of making
     interleaving reads and writes work.  Currently, they'd fail with
     a checksum mismatch, it just happens that our code never tries to
     do that anyway. */

  if (! wb->finalized)
    {
      SVN_ERR(svn_checksum_final(&wb->md5_checksum, wb->md5_checksum_ctx,
                                 wb->pool));
      SVN_ERR(svn_checksum_final(&wb->sha1_checksum, wb->sha1_checksum_ctx,
                                 wb->pool));
      wb->finalized = TRUE;
    }

  /* If we got a trail, use it; else make one. */
  if (wb->trail)
    return txn_body_write_close_rep(wb, wb->trail);
  else
    /* We need to keep our trail pool around this time so the
       checksums we've calculated survive. */
    return svn_fs_base__retry_txn(wb->fs, txn_body_write_close_rep,
                                  wb, FALSE, wb->pool);
}


/** Public read and write stream constructors. **/

svn_error_t *
svn_fs_base__rep_contents_read_stream(svn_stream_t **rs_p,
                                      svn_fs_t *fs,
                                      const char *rep_key,
                                      svn_boolean_t use_trail_for_reads,
                                      trail_t *trail,
                                      apr_pool_t *pool)
{
  struct rep_read_baton *rb;

  SVN_ERR(rep_read_get_baton(&rb, fs, rep_key, use_trail_for_reads,
                             trail, pool));
  *rs_p = svn_stream_create(rb, pool);
  svn_stream_set_read2(*rs_p, NULL /* only full read support */,
                       rep_read_contents);

  return SVN_NO_ERROR;
}


/* Clear the contents of REP_KEY, so that it represents the empty
   string, as part of TRAIL.  TXN_ID is the id of the Subversion
   transaction under which this occurs.  If REP_KEY is not mutable,
   return the error SVN_ERR_FS_REP_NOT_MUTABLE.  */
static svn_error_t *
rep_contents_clear(svn_fs_t *fs,
                   const char *rep_key,
                   const char *txn_id,
                   trail_t *trail,
                   apr_pool_t *pool)
{
  representation_t *rep;
  const char *str_key;

  SVN_ERR(svn_fs_bdb__read_rep(&rep, fs, rep_key, trail, pool));

  /* Make sure it's mutable. */
  if (! rep_is_mutable(rep, txn_id))
    return svn_error_createf
      (SVN_ERR_FS_REP_NOT_MUTABLE, NULL,
       _("Rep '%s' is not mutable"), rep_key);

  SVN_ERR_ASSERT(rep->kind == rep_kind_fulltext);

  /* If rep has no string, just return success.  Else, clear the
     underlying string.  */
  str_key = rep->contents.fulltext.string_key;
  if (str_key && *str_key)
    {
      SVN_ERR(svn_fs_bdb__string_clear(fs, str_key, trail, pool));
      rep->md5_checksum = NULL;
      rep->sha1_checksum = NULL;
      SVN_ERR(svn_fs_bdb__write_rep(fs, rep_key, rep, trail, pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__rep_contents_write_stream(svn_stream_t **ws_p,
                                       svn_fs_t *fs,
                                       const char *rep_key,
                                       const char *txn_id,
                                       svn_boolean_t use_trail_for_writes,
                                       trail_t *trail,
                                       apr_pool_t *pool)
{
  struct rep_write_baton *wb;

  /* Clear the current rep contents (free mutability check!). */
  SVN_ERR(rep_contents_clear(fs, rep_key, txn_id, trail, pool));

  /* Now, generate the write baton and stream. */
  wb = rep_write_get_baton(fs, rep_key, txn_id,
                           use_trail_for_writes ? trail : NULL, pool);
  *ws_p = svn_stream_create(wb, pool);
  svn_stream_set_write(*ws_p, rep_write_contents);
  svn_stream_set_close(*ws_p, rep_write_close_contents);

  return SVN_NO_ERROR;
}



/*** Deltified storage. ***/

/* Baton for svn_write_fn_t write_string_set(). */
struct write_svndiff_strings_baton
{
  /* The fs where lives the string we're writing. */
  svn_fs_t *fs;

  /* The key of the string we're writing to.  Typically this is
     initialized to NULL, so svn_fs_base__string_append() can fill in a
     value. */
  const char *key;

  /* The amount of txdelta data written to the current
     string-in-progress. */
  apr_size_t size;

  /* The amount of svndiff header information we've written thus far
     to the strings table. */
  apr_size_t header_read;

  /* The version number of the svndiff data written.  ### You'd better
     not count on this being populated after the first chunk is sent
     through the interface, since it lives at the 4th byte of the
     stream. */
  apr_byte_t version;

  /* The trail we're writing in. */
  trail_t *trail;

};


/* Function of type `svn_write_fn_t', for writing to a collection of
   strings; BATON is `struct write_svndiff_strings_baton *'.

   On the first call, BATON->key is null.  A new string key in
   BATON->fs is chosen and stored in BATON->key; each call appends
   *LEN bytes from DATA onto the string.  *LEN is never changed; if
   the write fails to write all *LEN bytes, an error is returned.
   BATON->size is used to track the total amount of data written via
   this handler, and must be reset by the caller to 0 when appropriate.  */
static svn_error_t *
write_svndiff_strings(void *baton, const char *data, apr_size_t *len)
{
  struct write_svndiff_strings_baton *wb = baton;
  const char *buf = data;
  apr_size_t nheader = 0;

  /* If we haven't stripped all the header information from this
     stream yet, keep stripping.  If someone sends a first window
     through here that's shorter than 4 bytes long, this will probably
     cause a nuclear reactor meltdown somewhere in the American
     midwest.  */
  if (wb->header_read < 4)
    {
      nheader = 4 - wb->header_read;
      *len -= nheader;
      buf += nheader;
      wb->header_read += nheader;

      /* If we have *now* read the full 4-byte header, check that
         least byte for the version number of the svndiff format. */
      if (wb->header_read == 4)
        wb->version = *(buf - 1);
    }

  /* Append to the current string we're writing (or create a new one
     if WB->key is NULL). */
  SVN_ERR(svn_fs_bdb__string_append(wb->fs, &(wb->key), *len,
                                    buf, wb->trail, wb->trail->pool));

  /* Make sure we (still) have a key. */
  if (wb->key == NULL)
    return svn_error_create(SVN_ERR_FS_GENERAL, NULL,
                            _("Failed to get new string key"));

  /* Restore *LEN to the value it *would* have been were it not for
     header stripping. */
  *len += nheader;

  /* Increment our running total of bytes written to this string. */
  wb->size += *len;

  return SVN_NO_ERROR;
}


typedef struct window_write_t
{
  const char *key; /* string key for this window */
  apr_size_t svndiff_len; /* amount of svndiff data written to the string */
  svn_filesize_t text_off; /* offset of fulltext represented by this window */
  apr_size_t text_len; /* amount of fulltext data represented by this window */

} window_write_t;


svn_error_t *
svn_fs_base__rep_deltify(svn_fs_t *fs,
                         const char *target,
                         const char *source,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  svn_stream_t *source_stream; /* stream to read the source */
  svn_stream_t *target_stream; /* stream to read the target */
  svn_txdelta_stream_t *txdelta_stream; /* stream to read delta windows  */

  /* window-y things, and an array to track them */
  window_write_t *ww;
  apr_array_header_t *windows;

  /* stream to write new (deltified) target data and its baton */
  svn_stream_t *new_target_stream;
  struct write_svndiff_strings_baton new_target_baton;

  /* window handler/baton for writing to above stream */
  svn_txdelta_window_handler_t new_target_handler;
  void *new_target_handler_baton;

  /* yes, we do windows */
  svn_txdelta_window_t *window;

  /* The current offset into the fulltext that our window is about to
     write.  This doubles, after all windows are written, as the
     total size of the svndiff data for the deltification process. */
  svn_filesize_t tview_off = 0;

  /* The total amount of diff data written while deltifying. */
  svn_filesize_t diffsize = 0;

  /* TARGET's original string keys */
  apr_array_header_t *orig_str_keys;

  /* The checksums for the representation's fulltext contents. */
  svn_checksum_t *rep_md5_checksum;
  svn_checksum_t *rep_sha1_checksum;

  /* MD5 digest */
  const unsigned char *digest;

  /* pool for holding the windows */
  apr_pool_t *wpool;

  /* Paranoia: never allow a rep to be deltified against itself,
     because then there would be no fulltext reachable in the delta
     chain, and badness would ensue.  */
  if (strcmp(target, source) == 0)
    return svn_error_createf
      (SVN_ERR_FS_CORRUPT, NULL,
       _("Attempt to deltify '%s' against itself"),
       target);

  /* Set up a handler for the svndiff data, which will write each
     window to its own string in the `strings' table. */
  new_target_baton.fs = fs;
  new_target_baton.trail = trail;
  new_target_baton.header_read = FALSE;
  new_target_stream = svn_stream_create(&new_target_baton, pool);
  svn_stream_set_write(new_target_stream, write_svndiff_strings);

  /* Get streams to our source and target text data. */
  SVN_ERR(svn_fs_base__rep_contents_read_stream(&source_stream, fs, source,
                                                TRUE, trail, pool));
  SVN_ERR(svn_fs_base__rep_contents_read_stream(&target_stream, fs, target,
                                                TRUE, trail, pool));

  /* Setup a stream to convert the textdelta data into svndiff windows. */
  svn_txdelta2(&txdelta_stream, source_stream, target_stream, TRUE, pool);

  if (bfd->format >= SVN_FS_BASE__MIN_SVNDIFF1_FORMAT)
    svn_txdelta_to_svndiff3(&new_target_handler, &new_target_handler_baton,
                            new_target_stream, 1,
                            SVN_DELTA_COMPRESSION_LEVEL_DEFAULT, pool);
  else
    svn_txdelta_to_svndiff3(&new_target_handler, &new_target_handler_baton,
                            new_target_stream, 0,
                            SVN_DELTA_COMPRESSION_LEVEL_DEFAULT, pool);

  /* subpool for the windows */
  wpool = svn_pool_create(pool);

  /* Now, loop, manufacturing and dispatching windows of svndiff data. */
  windows = apr_array_make(pool, 1, sizeof(ww));
  do
    {
      /* Reset some baton variables. */
      new_target_baton.size = 0;
      new_target_baton.key = NULL;

      /* Free the window. */
      svn_pool_clear(wpool);

      /* Fetch the next window of txdelta data. */
      SVN_ERR(svn_txdelta_next_window(&window, txdelta_stream, wpool));

      /* Send off this package to be written as svndiff data. */
      SVN_ERR(new_target_handler(window, new_target_handler_baton));
      if (window)
        {
          /* Add a new window description to our array. */
          ww = apr_pcalloc(pool, sizeof(*ww));
          ww->key = new_target_baton.key;
          ww->svndiff_len = new_target_baton.size;
          ww->text_off = tview_off;
          ww->text_len = window->tview_len;
          APR_ARRAY_PUSH(windows, window_write_t *) = ww;

          /* Update our recordkeeping variables. */
          tview_off += window->tview_len;
          diffsize += ww->svndiff_len;
        }

    } while (window);

  svn_pool_destroy(wpool);

  /* Having processed all the windows, we can query the MD5 digest
     from the stream.  */
  digest = svn_txdelta_md5_digest(txdelta_stream);
  if (! digest)
    return svn_error_createf
      (SVN_ERR_DELTA_MD5_CHECKSUM_ABSENT, NULL,
       _("Failed to calculate MD5 digest for '%s'"),
       source);

  /* Construct a list of the strings used by the old representation so
     that we can delete them later.  While we are here, if the old
     representation was a fulltext, check to make sure the delta we're
     replacing it with is actually smaller.  (Don't perform this check
     if we're replacing a delta; in that case, we're going for a time
     optimization, not a space optimization.)  */
  {
    representation_t *old_rep;
    const char *str_key;

    SVN_ERR(svn_fs_bdb__read_rep(&old_rep, fs, target, trail, pool));
    if (old_rep->kind == rep_kind_fulltext)
      {
        svn_filesize_t old_size = 0;

        str_key = old_rep->contents.fulltext.string_key;
        SVN_ERR(svn_fs_bdb__string_size(&old_size, fs, str_key,
                                        trail, pool));
        orig_str_keys = apr_array_make(pool, 1, sizeof(str_key));
        APR_ARRAY_PUSH(orig_str_keys, const char *) = str_key;

        /* If the new data is NOT an space optimization, destroy the
           string(s) we created, and get outta here. */
        if (diffsize >= old_size)
          {
            int i;
            for (i = 0; i < windows->nelts; i++)
              {
                ww = APR_ARRAY_IDX(windows, i, window_write_t *);
                SVN_ERR(svn_fs_bdb__string_delete(fs, ww->key, trail, pool));
              }
            return SVN_NO_ERROR;
          }
      }
    else if (old_rep->kind == rep_kind_delta)
      SVN_ERR(delta_string_keys(&orig_str_keys, old_rep, pool));
    else /* unknown kind */
      return UNKNOWN_NODE_KIND(target);

    /* Save the checksums, since the new rep needs them. */
    rep_md5_checksum = svn_checksum_dup(old_rep->md5_checksum, pool);
    rep_sha1_checksum = svn_checksum_dup(old_rep->sha1_checksum, pool);
  }

  /* Hook the new strings we wrote into the rest of the filesystem by
     building a new representation to replace our old one. */
  {
    representation_t new_rep;
    rep_delta_chunk_t *chunk;
    apr_array_header_t *chunks;
    int i;

    new_rep.kind = rep_kind_delta;
    new_rep.txn_id = NULL;

    /* Migrate the old rep's checksums to the new rep. */
    new_rep.md5_checksum = svn_checksum_dup(rep_md5_checksum, pool);
    new_rep.sha1_checksum = svn_checksum_dup(rep_sha1_checksum, pool);

    chunks = apr_array_make(pool, windows->nelts, sizeof(chunk));

    /* Loop through the windows we wrote, creating and adding new
       chunks to the representation. */
    for (i = 0; i < windows->nelts; i++)
      {
        ww = APR_ARRAY_IDX(windows, i, window_write_t *);

        /* Allocate a chunk and its window */
        chunk = apr_palloc(pool, sizeof(*chunk));
        chunk->offset = ww->text_off;

        /* Populate the window */
        chunk->version = new_target_baton.version;
        chunk->string_key = ww->key;
        chunk->size = ww->text_len;
        chunk->rep_key = source;

        /* Add this chunk to the array. */
        APR_ARRAY_PUSH(chunks, rep_delta_chunk_t *) = chunk;
      }

    /* Put the chunks array into the representation. */
    new_rep.contents.delta.chunks = chunks;

    /* Write out the new representation. */
    SVN_ERR(svn_fs_bdb__write_rep(fs, target, &new_rep, trail, pool));

    /* Delete the original pre-deltified strings. */
    SVN_ERR(delete_strings(orig_str_keys, fs, trail, pool));
  }

  return SVN_NO_ERROR;
}
