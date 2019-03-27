/* fs_skels.c --- conversion between fs native types and skeletons
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

#include <string.h>

#include <apr_md5.h>
#include <apr_sha1.h>

#include "svn_error.h"
#include "svn_string.h"
#include "svn_types.h"
#include "svn_time.h"

#include "private/svn_skel.h"
#include "private/svn_dep_compat.h"
#include "private/svn_subr_private.h"

#include "svn_checksum.h"
#include "fs_skels.h"
#include "../id.h"


static svn_error_t *
skel_err(const char *skel_type)
{
  return svn_error_createf(SVN_ERR_FS_MALFORMED_SKEL, NULL,
                           "Malformed%s%s skeleton",
                           skel_type ? " " : "",
                           skel_type ? skel_type : "");
}



/*** Validity Checking ***/

static svn_boolean_t
is_valid_checksum_skel(svn_skel_t *skel)
{
  if (svn_skel__list_length(skel) != 2)
    return FALSE;

  if (svn_skel__matches_atom(skel->children, "md5")
      && skel->children->next->is_atom)
    return TRUE;

  if (svn_skel__matches_atom(skel->children, "sha1")
      && skel->children->next->is_atom)
    return TRUE;

  return FALSE;
}


static svn_boolean_t
is_valid_revision_skel(svn_skel_t *skel)
{
  int len = svn_skel__list_length(skel);

  if ((len == 2)
      && svn_skel__matches_atom(skel->children, "revision")
      && skel->children->next->is_atom)
    return TRUE;

  return FALSE;
}


static svn_boolean_t
is_valid_transaction_skel(svn_skel_t *skel, transaction_kind_t *kind)
{
  int len = svn_skel__list_length(skel);

  if (len != 5)
    return FALSE;

  /* Determine (and verify) the kind. */
  if (svn_skel__matches_atom(skel->children, "transaction"))
    *kind = transaction_kind_normal;
  else if (svn_skel__matches_atom(skel->children, "committed"))
    *kind = transaction_kind_committed;
  else if (svn_skel__matches_atom(skel->children, "dead"))
    *kind = transaction_kind_dead;
  else
    return FALSE;

  if (skel->children->next->is_atom
      && skel->children->next->next->is_atom
      && (! skel->children->next->next->next->is_atom)
      && (! skel->children->next->next->next->next->is_atom))
    return TRUE;

  return FALSE;
}


static svn_boolean_t
is_valid_rep_delta_chunk_skel(svn_skel_t *skel)
{
  int len;
  svn_skel_t *window;
  svn_skel_t *diff;

  /* check the delta skel. */
  if ((svn_skel__list_length(skel) != 2)
      || (! skel->children->is_atom))
    return FALSE;

  /* check the window. */
  window = skel->children->next;
  len = svn_skel__list_length(window);
  if ((len < 3) || (len > 4))
    return FALSE;
  if (! ((! window->children->is_atom)
         && (window->children->next->is_atom)
         && (window->children->next->next->is_atom)))
    return FALSE;
  if ((len == 4)
      && (! window->children->next->next->next->is_atom))
    return FALSE;

  /* check the diff. ### currently we support only svndiff version
     0 delta data. */
  diff = window->children;
  if ((svn_skel__list_length(diff) == 3)
      && (svn_skel__matches_atom(diff->children, "svndiff"))
      && ((svn_skel__matches_atom(diff->children->next, "0"))
          || (svn_skel__matches_atom(diff->children->next, "1")))
      && (diff->children->next->next->is_atom))
    return TRUE;

  return FALSE;
}


static svn_boolean_t
is_valid_representation_skel(svn_skel_t *skel)
{
  int len = svn_skel__list_length(skel);
  svn_skel_t *header;
  int header_len;

  /* the rep has at least two items in it, a HEADER list, and at least
     one piece of kind-specific data. */
  if (len < 2)
    return FALSE;

  /* check the header.  it must have KIND and TXN atoms, and
     optionally 1 or 2 checksums (which is a list form). */
  header = skel->children;
  header_len = svn_skel__list_length(header);
  if (! (((header_len == 2)     /* 2 means old repository, checksum absent */
          && (header->children->is_atom)
          && (header->children->next->is_atom))
         || ((header_len == 3)  /* 3 means md5 checksum present */
             && (header->children->is_atom)
             && (header->children->next->is_atom)
             && (is_valid_checksum_skel(header->children->next->next)))
         || ((header_len == 4)  /* 3 means md5 and sha1 checksums present */
             && (header->children->is_atom)
             && (header->children->next->is_atom)
             && (is_valid_checksum_skel(header->children->next->next))
             && (is_valid_checksum_skel(header->children->next->next->next)))))
    return FALSE;

  /* check for fulltext rep. */
  if ((len == 2)
      && (svn_skel__matches_atom(header->children, "fulltext")))
    return TRUE;

  /* check for delta rep. */
  if ((len >= 2)
      && (svn_skel__matches_atom(header->children, "delta")))
    {
      /* it's a delta rep.  check the validity.  */
      svn_skel_t *chunk = skel->children->next;

      /* loop over chunks, checking each one. */
      while (chunk)
        {
          if (! is_valid_rep_delta_chunk_skel(chunk))
            return FALSE;
          chunk = chunk->next;
        }

      /* all good on this delta rep. */
      return TRUE;
    }

  return FALSE;
}


static svn_boolean_t
is_valid_node_revision_header_skel(svn_skel_t *skel, svn_skel_t **kind_p)
{
  int len = svn_skel__list_length(skel);

  if (len < 2)
    return FALSE;

  /* set the *KIND_P pointer. */
  *kind_p = skel->children;

  /* check for valid lengths. */
  if (! ((len == 2) || (len == 3) || (len == 4) || (len == 6)))
    return FALSE;

  /* got mergeinfo stuff? */
  if ((len > 4)
      && (! (skel->children->next->next->next->next->is_atom
             && skel->children->next->next->next->next->next->is_atom)))
    return FALSE;

  /* got predecessor count? */
  if ((len > 3)
      && (! skel->children->next->next->next->is_atom))
    return FALSE;

  /* got predecessor? */
  if ((len > 2)
      && (! skel->children->next->next->is_atom))
    return FALSE;

  /* got the basics? */
  if (! (skel->children->is_atom
         && skel->children->next->is_atom
         && (skel->children->next->data[0] == '/')))
    return FALSE;

  return TRUE;
}


static svn_boolean_t
is_valid_node_revision_skel(svn_skel_t *skel)
{
  int len = svn_skel__list_length(skel);
  svn_skel_t *header = skel->children;
  svn_skel_t *kind;

  if (len < 1)
    return FALSE;

  if (! is_valid_node_revision_header_skel(header, &kind))
    return FALSE;

  if (svn_skel__matches_atom(kind, "dir"))
    {
      if (! ((len == 3)
             && header->next->is_atom
             && header->next->next->is_atom))
        return FALSE;
    }
  else if (svn_skel__matches_atom(kind, "file"))
    {
      if (len < 3)
        return FALSE;

      if (! header->next->is_atom)
        return FALSE;

      /* As of SVN_FS_BASE__MIN_REP_SHARING_FORMAT version, the
         DATA-KEY slot can be a 2-tuple. */
      if (! header->next->next->is_atom)
        {
          if (! ((svn_skel__list_length(header->next->next) == 2)
                 && header->next->next->children->is_atom
                 && header->next->next->children->len
                 && header->next->next->children->next->is_atom
                 && header->next->next->children->next->len))
            return FALSE;
        }

      if ((len > 3) && (! header->next->next->next->is_atom))
        return FALSE;

      if (len > 4)
        return FALSE;
    }

  return TRUE;
}


static svn_boolean_t
is_valid_copy_skel(svn_skel_t *skel)
{
  return ((svn_skel__list_length(skel) == 4)
          && (svn_skel__matches_atom(skel->children, "copy")
              || svn_skel__matches_atom(skel->children, "soft-copy"))
          && skel->children->next->is_atom
          && skel->children->next->next->is_atom
          && skel->children->next->next->next->is_atom);
}


static svn_boolean_t
is_valid_change_skel(svn_skel_t *skel, svn_fs_path_change_kind_t *kind)
{
  if ((svn_skel__list_length(skel) == 6)
      && svn_skel__matches_atom(skel->children, "change")
      && skel->children->next->is_atom
      && skel->children->next->next->is_atom
      && skel->children->next->next->next->is_atom
      && skel->children->next->next->next->next->is_atom
      && skel->children->next->next->next->next->next->is_atom)
    {
      svn_skel_t *kind_skel = skel->children->next->next->next;

      /* check the kind (and return it) */
      if (svn_skel__matches_atom(kind_skel, "reset"))
        {
          if (kind)
            *kind = svn_fs_path_change_reset;
          return TRUE;
        }
      if (svn_skel__matches_atom(kind_skel, "add"))
        {
          if (kind)
            *kind = svn_fs_path_change_add;
          return TRUE;
        }
      if (svn_skel__matches_atom(kind_skel, "delete"))
        {
          if (kind)
            *kind = svn_fs_path_change_delete;
          return TRUE;
        }
      if (svn_skel__matches_atom(kind_skel, "replace"))
        {
          if (kind)
            *kind = svn_fs_path_change_replace;
          return TRUE;
        }
      if (svn_skel__matches_atom(kind_skel, "modify"))
        {
          if (kind)
            *kind = svn_fs_path_change_modify;
          return TRUE;
        }
    }
  return FALSE;
}


static svn_boolean_t
is_valid_lock_skel(svn_skel_t *skel)
{
  if ((svn_skel__list_length(skel) == 8)
      && svn_skel__matches_atom(skel->children, "lock")
      && skel->children->next->is_atom
      && skel->children->next->next->is_atom
      && skel->children->next->next->next->is_atom
      && skel->children->next->next->next->next->is_atom
      && skel->children->next->next->next->next->next->is_atom
      && skel->children->next->next->next->next->next->next->is_atom
      && skel->children->next->next->next->next->next->next->next->is_atom)
    return TRUE;

  return FALSE;
}



/*** Parsing (conversion from skeleton to native FS type) ***/

svn_error_t *
svn_fs_base__parse_revision_skel(revision_t **revision_p,
                                 svn_skel_t *skel,
                                 apr_pool_t *pool)
{
  revision_t *revision;

  /* Validate the skel. */
  if (! is_valid_revision_skel(skel))
    return skel_err("revision");

  /* Create the returned structure */
  revision = apr_pcalloc(pool, sizeof(*revision));
  revision->txn_id = apr_pstrmemdup(pool, skel->children->next->data,
                                    skel->children->next->len);

  /* Return the structure. */
  *revision_p = revision;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__parse_transaction_skel(transaction_t **transaction_p,
                                    svn_skel_t *skel,
                                    apr_pool_t *pool)
{
  transaction_t *transaction;
  transaction_kind_t kind;
  svn_skel_t *root_id, *base_id_or_rev, *proplist, *copies;
  int len;

  /* Validate the skel. */
  if (! is_valid_transaction_skel(skel, &kind))
    return skel_err("transaction");

  root_id = skel->children->next;
  base_id_or_rev = skel->children->next->next;
  proplist = skel->children->next->next->next;
  copies = skel->children->next->next->next->next;

  /* Create the returned structure */
  transaction = apr_pcalloc(pool, sizeof(*transaction));

  /* KIND */
  transaction->kind = kind;

  /* REVISION or BASE-ID */
  if (kind == transaction_kind_committed)
    {
      /* Committed transactions have a revision number... */
      transaction->base_id = NULL;
      transaction->revision =
        SVN_STR_TO_REV(apr_pstrmemdup(pool, base_id_or_rev->data,
                                      base_id_or_rev->len));
      if (! SVN_IS_VALID_REVNUM(transaction->revision))
        return skel_err("transaction");

    }
  else
    {
      /* ...where unfinished transactions have a base node-revision-id. */
      transaction->revision = SVN_INVALID_REVNUM;
      transaction->base_id = svn_fs_base__id_parse(base_id_or_rev->data,
                                                   base_id_or_rev->len, pool);
    }

  /* ROOT-ID */
  transaction->root_id = svn_fs_base__id_parse(root_id->data,
                                               root_id->len, pool);

  /* PROPLIST */
  SVN_ERR(svn_skel__parse_proplist(&(transaction->proplist),
                                   proplist, pool));

  /* COPIES */
  if ((len = svn_skel__list_length(copies)))
    {
      const char *copy_id;
      apr_array_header_t *txncopies;
      svn_skel_t *cpy = copies->children;

      txncopies = apr_array_make(pool, len, sizeof(copy_id));
      while (cpy)
        {
          copy_id = apr_pstrmemdup(pool, cpy->data, cpy->len);
          APR_ARRAY_PUSH(txncopies, const char *) = copy_id;
          cpy = cpy->next;
        }
      transaction->copies = txncopies;
    }

  /* Return the structure. */
  *transaction_p = transaction;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__parse_representation_skel(representation_t **rep_p,
                                       svn_skel_t *skel,
                                       apr_pool_t *pool)
{
  representation_t *rep;
  svn_skel_t *header_skel;

  /* Validate the skel. */
  if (! is_valid_representation_skel(skel))
    return skel_err("representation");
  header_skel = skel->children;

  /* Create the returned structure */
  rep = apr_pcalloc(pool, sizeof(*rep));

  /* KIND */
  if (svn_skel__matches_atom(header_skel->children, "fulltext"))
    rep->kind = rep_kind_fulltext;
  else
    rep->kind = rep_kind_delta;

  /* TXN */
  rep->txn_id = apr_pstrmemdup(pool, header_skel->children->next->data,
                               header_skel->children->next->len);

  /* MD5 */
  if (header_skel->children->next->next)
    {
      svn_skel_t *checksum_skel = header_skel->children->next->next;
      rep->md5_checksum =
        svn_checksum__from_digest_md5((const unsigned char *)
                                      (checksum_skel->children->next->data),
                                      pool);

      /* SHA1 */
      if (header_skel->children->next->next->next)
        {
          checksum_skel = header_skel->children->next->next->next;
          rep->sha1_checksum =
            svn_checksum__from_digest_sha1(
              (const unsigned char *)(checksum_skel->children->next->data),
              pool);
        }
    }

  /* KIND-SPECIFIC stuff */
  if (rep->kind == rep_kind_fulltext)
    {
      /* "fulltext"-specific. */
      rep->contents.fulltext.string_key
        = apr_pstrmemdup(pool,
                         skel->children->next->data,
                         skel->children->next->len);
    }
  else
    {
      /* "delta"-specific. */
      svn_skel_t *chunk_skel = skel->children->next;
      rep_delta_chunk_t *chunk;
      apr_array_header_t *chunks;

      /* Alloc the chunk array. */
      chunks = apr_array_make(pool, svn_skel__list_length(skel) - 1,
                              sizeof(chunk));

      /* Process the chunks. */
      while (chunk_skel)
        {
          svn_skel_t *window_skel = chunk_skel->children->next;
          svn_skel_t *diff_skel = window_skel->children;
          apr_int64_t val;
          apr_uint64_t uval;
          const char *str;

          /* Allocate a chunk and its window */
          chunk = apr_palloc(pool, sizeof(*chunk));

          /* Populate the window */
          str = apr_pstrmemdup(pool, diff_skel->children->next->data,
                               diff_skel->children->next->len);
          SVN_ERR(svn_cstring_strtoui64(&uval, str, 0, 255, 10));
          chunk->version = (apr_byte_t)uval;

          chunk->string_key
            = apr_pstrmemdup(pool,
                             diff_skel->children->next->next->data,
                             diff_skel->children->next->next->len);

          str = apr_pstrmemdup(pool, window_skel->children->next->data,
                               window_skel->children->next->len);
          SVN_ERR(svn_cstring_strtoui64(&uval, str, 0, APR_SIZE_MAX, 10));
          chunk->size = (apr_size_t)uval;

          chunk->rep_key
            = apr_pstrmemdup(pool,
                             window_skel->children->next->next->data,
                             window_skel->children->next->next->len);

          str = apr_pstrmemdup(pool, chunk_skel->children->data,
                               chunk_skel->children->len);
          SVN_ERR(svn_cstring_strtoi64(&val, str, 0, APR_INT64_MAX, 10));
          chunk->offset = (svn_filesize_t)val;

          /* Add this chunk to the array. */
          APR_ARRAY_PUSH(chunks, rep_delta_chunk_t *) = chunk;

          /* Next... */
          chunk_skel = chunk_skel->next;
        }

      /* Add the chunks array to the representation. */
      rep->contents.delta.chunks = chunks;
    }

  /* Return the structure. */
  *rep_p = rep;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__parse_node_revision_skel(node_revision_t **noderev_p,
                                      svn_skel_t *skel,
                                      apr_pool_t *pool)
{
  node_revision_t *noderev;
  svn_skel_t *header_skel, *cur_skel;

  /* Validate the skel. */
  if (! is_valid_node_revision_skel(skel))
    return skel_err("node-revision");
  header_skel = skel->children;

  /* Create the returned structure */
  noderev = apr_pcalloc(pool, sizeof(*noderev));

  /* KIND */
  if (svn_skel__matches_atom(header_skel->children, "dir"))
    noderev->kind = svn_node_dir;
  else
    noderev->kind = svn_node_file;

  /* CREATED-PATH */
  noderev->created_path = apr_pstrmemdup(pool,
                                         header_skel->children->next->data,
                                         header_skel->children->next->len);

  /* PREDECESSOR-ID */
  if (header_skel->children->next->next)
    {
      cur_skel = header_skel->children->next->next;
      if (cur_skel->len)
        noderev->predecessor_id = svn_fs_base__id_parse(cur_skel->data,
                                                        cur_skel->len, pool);

      /* PREDECESSOR-COUNT */
      noderev->predecessor_count = -1;
      if (cur_skel->next)
        {
          const char *str;

          cur_skel = cur_skel->next;
          if (cur_skel->len)
            {
              str = apr_pstrmemdup(pool, cur_skel->data, cur_skel->len);
              SVN_ERR(svn_cstring_atoi(&noderev->predecessor_count, str));
            }

          /* HAS-MERGEINFO and MERGEINFO-COUNT */
          if (cur_skel->next)
            {
              int val;

              cur_skel = cur_skel->next;
              str = apr_pstrmemdup(pool, cur_skel->data, cur_skel->len);
              SVN_ERR(svn_cstring_atoi(&val, str));
              noderev->has_mergeinfo = (val != 0);

              str = apr_pstrmemdup(pool, cur_skel->next->data,
                                   cur_skel->next->len);
              SVN_ERR(svn_cstring_atoi64(&noderev->mergeinfo_count, str));
            }
        }
    }

  /* PROP-KEY */
  if (skel->children->next->len)
    noderev->prop_key = apr_pstrmemdup(pool, skel->children->next->data,
                                       skel->children->next->len);

  /* DATA-KEY */
  if (skel->children->next->next->is_atom)
    {
      /* This is a real data rep key. */
      if (skel->children->next->next->len)
        noderev->data_key = apr_pstrmemdup(pool,
                                           skel->children->next->next->data,
                                           skel->children->next->next->len);
      noderev->data_key_uniquifier = NULL;
    }
  else
    {
      /* This is a 2-tuple with a data rep key and a uniquifier. */
      noderev->data_key =
        apr_pstrmemdup(pool,
                       skel->children->next->next->children->data,
                       skel->children->next->next->children->len);
      noderev->data_key_uniquifier =
        apr_pstrmemdup(pool,
                       skel->children->next->next->children->next->data,
                       skel->children->next->next->children->next->len);
    }

  /* EDIT-DATA-KEY (optional, files only) */
  if ((noderev->kind == svn_node_file)
      && skel->children->next->next->next
      && skel->children->next->next->next->len)
    noderev->edit_key
      = apr_pstrmemdup(pool, skel->children->next->next->next->data,
                       skel->children->next->next->next->len);

  /* Return the structure. */
  *noderev_p = noderev;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__parse_copy_skel(copy_t **copy_p,
                             svn_skel_t *skel,
                             apr_pool_t *pool)
{
  copy_t *copy;

  /* Validate the skel. */
  if (! is_valid_copy_skel(skel))
    return skel_err("copy");

  /* Create the returned structure */
  copy = apr_pcalloc(pool, sizeof(*copy));

  /* KIND */
  if (svn_skel__matches_atom(skel->children, "soft-copy"))
    copy->kind = copy_kind_soft;
  else
    copy->kind = copy_kind_real;

  /* SRC-PATH */
  copy->src_path = apr_pstrmemdup(pool,
                                  skel->children->next->data,
                                  skel->children->next->len);

  /* SRC-TXN-ID */
  copy->src_txn_id = apr_pstrmemdup(pool,
                                    skel->children->next->next->data,
                                    skel->children->next->next->len);

  /* DST-NODE-ID */
  copy->dst_noderev_id
    = svn_fs_base__id_parse(skel->children->next->next->next->data,
                            skel->children->next->next->next->len, pool);

  /* Return the structure. */
  *copy_p = copy;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__parse_entries_skel(apr_hash_t **entries_p,
                                svn_skel_t *skel,
                                apr_pool_t *pool)
{
  apr_hash_t *entries = NULL;
  int len = svn_skel__list_length(skel);
  svn_skel_t *elt;

  if (! (len >= 0))
    return skel_err("entries");

  if (len > 0)
    {
      /* Else, allocate a hash and populate it. */
      entries = apr_hash_make(pool);

      /* Check entries are well-formed as we go along. */
      for (elt = skel->children; elt; elt = elt->next)
        {
          const char *name;
          svn_fs_id_t *id;

          /* ENTRY must be a list of two elements. */
          if (svn_skel__list_length(elt) != 2)
            return skel_err("entries");

          /* Get the entry's name and ID. */
          name = apr_pstrmemdup(pool, elt->children->data,
                                elt->children->len);
          id = svn_fs_base__id_parse(elt->children->next->data,
                                     elt->children->next->len, pool);

          /* Add the entry to the hash. */
          apr_hash_set(entries, name, elt->children->len, id);
        }
    }

  /* Return the structure. */
  *entries_p = entries;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__parse_change_skel(change_t **change_p,
                               svn_skel_t *skel,
                               apr_pool_t *pool)
{
  change_t *change;
  svn_fs_path_change_kind_t kind;

  /* Validate the skel. */
  if (! is_valid_change_skel(skel, &kind))
    return skel_err("change");

  /* Create the returned structure */
  change = apr_pcalloc(pool, sizeof(*change));

  /* PATH */
  change->path = apr_pstrmemdup(pool, skel->children->next->data,
                                skel->children->next->len);

  /* NODE-REV-ID */
  if (skel->children->next->next->len)
    change->noderev_id = svn_fs_base__id_parse
      (skel->children->next->next->data, skel->children->next->next->len,
       pool);

  /* KIND */
  change->kind = kind;

  /* TEXT-MOD */
  if (skel->children->next->next->next->next->len)
    change->text_mod = TRUE;

  /* PROP-MOD */
  if (skel->children->next->next->next->next->next->len)
    change->prop_mod = TRUE;

  /* Return the structure. */
  *change_p = change;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__parse_lock_skel(svn_lock_t **lock_p,
                             svn_skel_t *skel,
                             apr_pool_t *pool)
{
  svn_lock_t *lock;
  const char *timestr;

  /* Validate the skel. */
  if (! is_valid_lock_skel(skel))
    return skel_err("lock");

  /* Create the returned structure */
  lock = apr_pcalloc(pool, sizeof(*lock));

  /* PATH */
  lock->path = apr_pstrmemdup(pool, skel->children->next->data,
                              skel->children->next->len);

  /* LOCK-TOKEN */
  lock->token = apr_pstrmemdup(pool,
                               skel->children->next->next->data,
                               skel->children->next->next->len);

  /* OWNER */
  lock->owner = apr_pstrmemdup(pool,
                               skel->children->next->next->next->data,
                               skel->children->next->next->next->len);

  /* COMMENT  (could be just an empty atom) */
  if (skel->children->next->next->next->next->len)
    lock->comment =
      apr_pstrmemdup(pool,
                     skel->children->next->next->next->next->data,
                     skel->children->next->next->next->next->len);

  /* XML_P */
  if (svn_skel__matches_atom
      (skel->children->next->next->next->next->next, "1"))
    lock->is_dav_comment = TRUE;
  else
    lock->is_dav_comment = FALSE;

  /* CREATION-DATE */
  timestr = apr_pstrmemdup
    (pool,
     skel->children->next->next->next->next->next->next->data,
     skel->children->next->next->next->next->next->next->len);
  SVN_ERR(svn_time_from_cstring(&(lock->creation_date),
                                timestr, pool));

  /* EXPIRATION-DATE  (could be just an empty atom) */
  if (skel->children->next->next->next->next->next->next->next->len)
    {
      timestr =
        apr_pstrmemdup
        (pool,
         skel->children->next->next->next->next->next->next->next->data,
         skel->children->next->next->next->next->next->next->next->len);
      SVN_ERR(svn_time_from_cstring(&(lock->expiration_date),
                                    timestr, pool));
    }

  /* Return the structure. */
  *lock_p = lock;
  return SVN_NO_ERROR;
}



/*** Unparsing (conversion from native FS type to skeleton) ***/

svn_error_t *
svn_fs_base__unparse_revision_skel(svn_skel_t **skel_p,
                                   const revision_t *revision,
                                   apr_pool_t *pool)
{
  svn_skel_t *skel;

  /* Create the skel. */
  skel = svn_skel__make_empty_list(pool);

  /* TXN_ID */
  svn_skel__prepend(svn_skel__str_atom(revision->txn_id, pool), skel);

  /* "revision" */
  svn_skel__prepend(svn_skel__str_atom("revision", pool), skel);

  /* Validate and return the skel. */
  if (! is_valid_revision_skel(skel))
    return skel_err("revision");
  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__unparse_transaction_skel(svn_skel_t **skel_p,
                                      const transaction_t *transaction,
                                      apr_pool_t *pool)
{
  svn_skel_t *skel;
  svn_skel_t *proplist_skel, *copies_skel, *header_skel;
  svn_string_t *id_str;
  transaction_kind_t kind;

  /* Create the skel. */
  skel = svn_skel__make_empty_list(pool);

  switch (transaction->kind)
    {
    case transaction_kind_committed:
      header_skel = svn_skel__str_atom("committed", pool);
      if ((transaction->base_id)
          || (! SVN_IS_VALID_REVNUM(transaction->revision)))
        return skel_err("transaction");
      break;
    case transaction_kind_dead:
      header_skel = svn_skel__str_atom("dead", pool);
      if ((! transaction->base_id)
          || (SVN_IS_VALID_REVNUM(transaction->revision)))
        return skel_err("transaction");
      break;
    case transaction_kind_normal:
      header_skel = svn_skel__str_atom("transaction", pool);
      if ((! transaction->base_id)
          || (SVN_IS_VALID_REVNUM(transaction->revision)))
        return skel_err("transaction");
      break;
    default:
      return skel_err("transaction");
    }


  /* COPIES */
  copies_skel = svn_skel__make_empty_list(pool);
  if (transaction->copies && transaction->copies->nelts)
    {
      int i;
      for (i = transaction->copies->nelts - 1; i >= 0; i--)
        {
          svn_skel__prepend(svn_skel__str_atom(
                                APR_ARRAY_IDX(transaction->copies, i,
                                              const char *),
                                pool),
                            copies_skel);
        }
    }
  svn_skel__prepend(copies_skel, skel);

  /* PROPLIST */
  SVN_ERR(svn_skel__unparse_proplist(&proplist_skel,
                                     transaction->proplist, pool));
  svn_skel__prepend(proplist_skel, skel);

  /* REVISION or BASE-ID */
  if (transaction->kind == transaction_kind_committed)
    {
      /* Committed transactions have a revision number... */
      svn_skel__prepend(svn_skel__str_atom(apr_psprintf(pool, "%ld",
                                                        transaction->revision),
                                           pool), skel);
    }
  else
    {
      /* ...where other transactions have a base node revision ID. */
      id_str = svn_fs_base__id_unparse(transaction->base_id, pool);
      svn_skel__prepend(svn_skel__mem_atom(id_str->data, id_str->len, pool),
                        skel);
    }

  /* ROOT-ID */
  id_str = svn_fs_base__id_unparse(transaction->root_id, pool);
  svn_skel__prepend(svn_skel__mem_atom(id_str->data, id_str->len, pool), skel);

  /* KIND (see above) */
  svn_skel__prepend(header_skel, skel);

  /* Validate and return the skel. */
  if (! is_valid_transaction_skel(skel, &kind))
    return skel_err("transaction");
  if (kind != transaction->kind)
    return skel_err("transaction");
  *skel_p = skel;
  return SVN_NO_ERROR;
}


/* Construct a skel representing CHECKSUM, allocated in POOL, and prepend
 * it onto the existing skel SKEL. */
static svn_error_t *
prepend_checksum(svn_skel_t *skel,
                 svn_checksum_t *checksum,
                 apr_pool_t *pool)
{
  svn_skel_t *checksum_skel = svn_skel__make_empty_list(pool);

  switch (checksum->kind)
    {
    case svn_checksum_md5:
      svn_skel__prepend(svn_skel__mem_atom(checksum->digest,
                                           APR_MD5_DIGESTSIZE, pool),
                        checksum_skel);
      svn_skel__prepend(svn_skel__str_atom("md5", pool), checksum_skel);
      break;

    case svn_checksum_sha1:
      svn_skel__prepend(svn_skel__mem_atom(checksum->digest,
                                           APR_SHA1_DIGESTSIZE, pool),
                        checksum_skel);
      svn_skel__prepend(svn_skel__str_atom("sha1", pool), checksum_skel);
      break;

    default:
      return skel_err("checksum");
    }
  svn_skel__prepend(checksum_skel, skel);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__unparse_representation_skel(svn_skel_t **skel_p,
                                         const representation_t *rep,
                                         int format,
                                         apr_pool_t *pool)
{
  svn_skel_t *skel = svn_skel__make_empty_list(pool);
  svn_skel_t *header_skel = svn_skel__make_empty_list(pool);

  /** Some parts of the header are common to all representations; do
      those parts first. **/

  /* SHA1 */
  if ((format >= SVN_FS_BASE__MIN_REP_SHARING_FORMAT) && rep->sha1_checksum)
    SVN_ERR(prepend_checksum(header_skel, rep->sha1_checksum, pool));

  /* MD5 */
  {
    svn_checksum_t *md5_checksum = rep->md5_checksum;
    if (! md5_checksum)
      md5_checksum = svn_checksum_create(svn_checksum_md5, pool);
    SVN_ERR(prepend_checksum(header_skel, md5_checksum, pool));
  }

  /* TXN */
  if (rep->txn_id)
    svn_skel__prepend(svn_skel__str_atom(rep->txn_id, pool), header_skel);
  else
    svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), header_skel);

  /** Do the kind-specific stuff. **/

  if (rep->kind == rep_kind_fulltext)
    {
      /*** Fulltext Representation. ***/

      /* STRING-KEY */
      if ((! rep->contents.fulltext.string_key)
          || (! *rep->contents.fulltext.string_key))
        svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);
      else
        svn_skel__prepend(svn_skel__str_atom(rep->contents.fulltext.string_key,
                                             pool), skel);

      /* "fulltext" */
      svn_skel__prepend(svn_skel__str_atom("fulltext", pool), header_skel);

      /* header */
      svn_skel__prepend(header_skel, skel);
    }
  else if (rep->kind == rep_kind_delta)
    {
      /*** Delta Representation. ***/
      int i;
      apr_array_header_t *chunks = rep->contents.delta.chunks;

      /* Loop backwards through the windows, creating and prepending skels. */
      for (i = chunks->nelts; i > 0; i--)
        {
          svn_skel_t *window_skel = svn_skel__make_empty_list(pool);
          svn_skel_t *chunk_skel = svn_skel__make_empty_list(pool);
          svn_skel_t *diff_skel = svn_skel__make_empty_list(pool);
          const char *size_str, *offset_str, *version_str;
          rep_delta_chunk_t *chunk = APR_ARRAY_IDX(chunks, i - 1,
                                                   rep_delta_chunk_t *);

          /* OFFSET */
          offset_str = apr_psprintf(pool, "%" SVN_FILESIZE_T_FMT,
                                    chunk->offset);

          /* SIZE */
          size_str = apr_psprintf(pool, "%" APR_SIZE_T_FMT, chunk->size);

          /* VERSION */
          version_str = apr_psprintf(pool, "%d", chunk->version);

          /* DIFF */
          if ((! chunk->string_key) || (! *chunk->string_key))
            svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), diff_skel);
          else
            svn_skel__prepend(svn_skel__str_atom(chunk->string_key, pool),
                              diff_skel);
          svn_skel__prepend(svn_skel__str_atom(version_str, pool), diff_skel);
          svn_skel__prepend(svn_skel__str_atom("svndiff", pool), diff_skel);

          /* REP-KEY */
          if ((! chunk->rep_key) || (! *(chunk->rep_key)))
            svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool),
                              window_skel);
          else
            svn_skel__prepend(svn_skel__str_atom(chunk->rep_key, pool),
                              window_skel);
          svn_skel__prepend(svn_skel__str_atom(size_str, pool), window_skel);
          svn_skel__prepend(diff_skel, window_skel);

          /* window header. */
          svn_skel__prepend(window_skel, chunk_skel);
          svn_skel__prepend(svn_skel__str_atom(offset_str, pool),
                               chunk_skel);

          /* Add this window item to the main skel. */
          svn_skel__prepend(chunk_skel, skel);
        }

      /* "delta" */
      svn_skel__prepend(svn_skel__str_atom("delta", pool), header_skel);

      /* header */
      svn_skel__prepend(header_skel, skel);
    }
  else /* unknown kind */
    SVN_ERR_MALFUNCTION();

  /* Validate and return the skel. */
  if (! is_valid_representation_skel(skel))
    return skel_err("representation");
  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__unparse_node_revision_skel(svn_skel_t **skel_p,
                                        const node_revision_t *noderev,
                                        int format,
                                        apr_pool_t *pool)
{
  svn_skel_t *skel;
  svn_skel_t *header_skel;
  const char *num_str;

  /* Create the skel. */
  skel = svn_skel__make_empty_list(pool);
  header_skel = svn_skel__make_empty_list(pool);

  /* Store mergeinfo stuffs only if the schema level supports it. */
  if (format >= SVN_FS_BASE__MIN_MERGEINFO_FORMAT)
    {
      /* MERGEINFO-COUNT */
      num_str = apr_psprintf(pool, "%" APR_INT64_T_FMT,
                             noderev->mergeinfo_count);
      svn_skel__prepend(svn_skel__str_atom(num_str, pool), header_skel);

      /* HAS-MERGEINFO */
      svn_skel__prepend(svn_skel__mem_atom(noderev->has_mergeinfo ? "1" : "0",
                                           1, pool), header_skel);

      /* PREDECESSOR-COUNT padding (only if we *don't* have a valid
         value; if we do, we'll pick that up below) */
      if (noderev->predecessor_count == -1)
        {
          svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), header_skel);
        }
    }

  /* PREDECESSOR-COUNT */
  if (noderev->predecessor_count != -1)
    {
      const char *count_str = apr_psprintf(pool, "%d",
                                           noderev->predecessor_count);
      svn_skel__prepend(svn_skel__str_atom(count_str, pool), header_skel);
    }

  /* PREDECESSOR-ID */
  if (noderev->predecessor_id)
    {
      svn_string_t *id_str = svn_fs_base__id_unparse(noderev->predecessor_id,
                                                     pool);
      svn_skel__prepend(svn_skel__mem_atom(id_str->data, id_str->len, pool),
                        header_skel);
    }
  else
    {
      svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), header_skel);
    }

  /* CREATED-PATH */
  svn_skel__prepend(svn_skel__str_atom(noderev->created_path, pool),
                    header_skel);

  /* KIND */
  if (noderev->kind == svn_node_file)
    svn_skel__prepend(svn_skel__str_atom("file", pool), header_skel);
  else if (noderev->kind == svn_node_dir)
    svn_skel__prepend(svn_skel__str_atom("dir", pool), header_skel);
  else
    SVN_ERR_MALFUNCTION();

  /* ### do we really need to check *node->FOO_key ? if a key doesn't
     ### exist, then the field should be NULL ...  */

  /* EDIT-DATA-KEY (optional) */
  if ((noderev->edit_key) && (*noderev->edit_key))
    svn_skel__prepend(svn_skel__str_atom(noderev->edit_key, pool), skel);

  /* DATA-KEY | (DATA-KEY DATA-KEY-UNIQID) */
  if ((noderev->data_key_uniquifier) && (*noderev->data_key_uniquifier))
    {
      /* Build a 2-tuple with a rep key and uniquifier. */
      svn_skel_t *data_key_skel = svn_skel__make_empty_list(pool);

      /* DATA-KEY-UNIQID */
      svn_skel__prepend(svn_skel__str_atom(noderev->data_key_uniquifier,
                                           pool),
                        data_key_skel);

      /* DATA-KEY */
      if ((noderev->data_key) && (*noderev->data_key))
        svn_skel__prepend(svn_skel__str_atom(noderev->data_key, pool),
                          data_key_skel);
      else
        svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), data_key_skel);

      /* Add our 2-tuple to the main skel. */
      svn_skel__prepend(data_key_skel, skel);
    }
  else
    {
      /* Just store the rep key (or empty placeholder) in the main skel. */
      if ((noderev->data_key) && (*noderev->data_key))
        svn_skel__prepend(svn_skel__str_atom(noderev->data_key, pool), skel);
      else
        svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);
    }

  /* PROP-KEY */
  if ((noderev->prop_key) && (*noderev->prop_key))
    svn_skel__prepend(svn_skel__str_atom(noderev->prop_key, pool), skel);
  else
    svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);

  /* HEADER */
  svn_skel__prepend(header_skel, skel);

  /* Validate and return the skel. */
  if (! is_valid_node_revision_skel(skel))
    return skel_err("node-revision");
  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__unparse_copy_skel(svn_skel_t **skel_p,
                               const copy_t *copy,
                               apr_pool_t *pool)
{
  svn_skel_t *skel;
  svn_string_t *tmp_str;

  /* Create the skel. */
  skel = svn_skel__make_empty_list(pool);

  /* DST-NODE-ID */
  tmp_str = svn_fs_base__id_unparse(copy->dst_noderev_id, pool);
  svn_skel__prepend(svn_skel__mem_atom(tmp_str->data, tmp_str->len, pool),
                    skel);

  /* SRC-TXN-ID */
  if ((copy->src_txn_id) && (*copy->src_txn_id))
    svn_skel__prepend(svn_skel__str_atom(copy->src_txn_id, pool), skel);
  else
    svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);

  /* SRC-PATH */
  if ((copy->src_path) && (*copy->src_path))
    svn_skel__prepend(svn_skel__str_atom(copy->src_path, pool), skel);
  else
    svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);

  /* "copy" */
  if (copy->kind == copy_kind_real)
    svn_skel__prepend(svn_skel__str_atom("copy", pool), skel);
  else
    svn_skel__prepend(svn_skel__str_atom("soft-copy", pool), skel);

  /* Validate and return the skel. */
  if (! is_valid_copy_skel(skel))
    return skel_err("copy");
  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__unparse_entries_skel(svn_skel_t **skel_p,
                                  apr_hash_t *entries,
                                  apr_pool_t *pool)
{
  svn_skel_t *skel = svn_skel__make_empty_list(pool);
  apr_hash_index_t *hi;

  /* Create the skel. */
  if (entries)
    {
      /* Loop over hash entries */
      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          apr_ssize_t klen;
          svn_fs_id_t *value;
          svn_string_t *id_str;
          svn_skel_t *entry_skel = svn_skel__make_empty_list(pool);

          apr_hash_this(hi, &key, &klen, &val);
          value = val;

          /* VALUE */
          id_str = svn_fs_base__id_unparse(value, pool);
          svn_skel__prepend(svn_skel__mem_atom(id_str->data, id_str->len,
                                               pool),
                            entry_skel);

          /* NAME */
          svn_skel__prepend(svn_skel__mem_atom(key, klen, pool), entry_skel);

          /* Add entry to the entries skel. */
          svn_skel__prepend(entry_skel, skel);
        }
    }

  /* Return the skel. */
  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__unparse_change_skel(svn_skel_t **skel_p,
                                 const change_t *change,
                                 apr_pool_t *pool)
{
  svn_skel_t *skel;
  svn_string_t *tmp_str;
  svn_fs_path_change_kind_t kind;

  /* Create the skel. */
  skel = svn_skel__make_empty_list(pool);

  /* PROP-MOD */
  if (change->prop_mod)
    svn_skel__prepend(svn_skel__str_atom("1", pool), skel);
  else
    svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);

  /* TEXT-MOD */
  if (change->text_mod)
    svn_skel__prepend(svn_skel__str_atom("1", pool), skel);
  else
    svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);

  /* KIND */
  switch (change->kind)
    {
    case svn_fs_path_change_reset:
      svn_skel__prepend(svn_skel__str_atom("reset", pool), skel);
      break;
    case svn_fs_path_change_add:
      svn_skel__prepend(svn_skel__str_atom("add", pool), skel);
      break;
    case svn_fs_path_change_delete:
      svn_skel__prepend(svn_skel__str_atom("delete", pool), skel);
      break;
    case svn_fs_path_change_replace:
      svn_skel__prepend(svn_skel__str_atom("replace", pool), skel);
      break;
    case svn_fs_path_change_modify:
    default:
      svn_skel__prepend(svn_skel__str_atom("modify", pool), skel);
      break;
    }

  /* NODE-REV-ID */
  if (change->noderev_id)
    {
      tmp_str = svn_fs_base__id_unparse(change->noderev_id, pool);
      svn_skel__prepend(svn_skel__mem_atom(tmp_str->data, tmp_str->len, pool),
                        skel);
    }
  else
    {
      svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);
    }

  /* PATH */
  svn_skel__prepend(svn_skel__str_atom(change->path, pool), skel);

  /* "change" */
  svn_skel__prepend(svn_skel__str_atom("change", pool), skel);

  /* Validate and return the skel. */
  if (! is_valid_change_skel(skel, &kind))
    return skel_err("change");
  if (kind != change->kind)
    return skel_err("change");
  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__unparse_lock_skel(svn_skel_t **skel_p,
                               const svn_lock_t *lock,
                               apr_pool_t *pool)
{
  svn_skel_t *skel;

  /* Create the skel. */
  skel = svn_skel__make_empty_list(pool);

  /* EXP-DATE is optional.  If not present, just use an empty atom. */
  if (lock->expiration_date)
    svn_skel__prepend(svn_skel__str_atom(
                          svn_time_to_cstring(lock->expiration_date, pool),
                          pool), skel);
  else
    svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);

  /* CREATION-DATE */
  svn_skel__prepend(svn_skel__str_atom(
                        svn_time_to_cstring(lock->creation_date, pool),
                        pool), skel);

  /* XML_P */
  if (lock->is_dav_comment)
    svn_skel__prepend(svn_skel__str_atom("1", pool), skel);
  else
    svn_skel__prepend(svn_skel__str_atom("0", pool), skel);

  /* COMMENT */
  if (lock->comment)
    svn_skel__prepend(svn_skel__str_atom(lock->comment, pool), skel);
  else
    svn_skel__prepend(svn_skel__mem_atom(NULL, 0, pool), skel);

  /* OWNER */
  svn_skel__prepend(svn_skel__str_atom(lock->owner, pool), skel);

  /* LOCK-TOKEN */
  svn_skel__prepend(svn_skel__str_atom(lock->token, pool), skel);

  /* PATH */
  svn_skel__prepend(svn_skel__str_atom(lock->path, pool), skel);

  /* "lock" */
  svn_skel__prepend(svn_skel__str_atom("lock", pool), skel);

  /* Validate and return the skel. */
  if (! is_valid_lock_skel(skel))
    return skel_err("lock");

  *skel_p = skel;
  return SVN_NO_ERROR;
}
