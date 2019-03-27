/*
 * svndumpfilter.c: Subversion dump stream filtering tool main file.
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

#include <apr_file_io.h>

#include "svn_private_config.h"
#include "svn_cmdline.h"
#include "svn_error.h"
#include "svn_string.h"
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_repos.h"
#include "svn_fs.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_version.h"

#include "private/svn_repos_private.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_cmdline_private.h"
#include "private/svn_sorts_private.h"

/*** Code. ***/

/* Writes a property in dumpfile format to given stringbuf. */
static void
write_prop_to_stringbuf(svn_stringbuf_t *strbuf,
                        const char *name,
                        const svn_string_t *value)
{
  int bytes_used;
  size_t namelen;
  char buf[SVN_KEYLINE_MAXLEN];

  /* Output name length, then name. */
  namelen = strlen(name);
  svn_stringbuf_appendbytes(strbuf, "K ", 2);

  bytes_used = apr_snprintf(buf, sizeof(buf), "%" APR_SIZE_T_FMT, namelen);
  svn_stringbuf_appendbytes(strbuf, buf, bytes_used);
  svn_stringbuf_appendbyte(strbuf, '\n');

  svn_stringbuf_appendbytes(strbuf, name, namelen);
  svn_stringbuf_appendbyte(strbuf, '\n');

  /* Output value length, then value. */
  svn_stringbuf_appendbytes(strbuf, "V ", 2);

  bytes_used = apr_snprintf(buf, sizeof(buf), "%" APR_SIZE_T_FMT, value->len);
  svn_stringbuf_appendbytes(strbuf, buf, bytes_used);
  svn_stringbuf_appendbyte(strbuf, '\n');

  svn_stringbuf_appendbytes(strbuf, value->data, value->len);
  svn_stringbuf_appendbyte(strbuf, '\n');
}


/* Writes a property deletion in dumpfile format to given stringbuf. */
static void
write_propdel_to_stringbuf(svn_stringbuf_t **strbuf,
                           const char *name)
{
  int bytes_used;
  size_t namelen;
  char buf[SVN_KEYLINE_MAXLEN];

  /* Output name length, then name. */
  namelen = strlen(name);
  svn_stringbuf_appendbytes(*strbuf, "D ", 2);

  bytes_used = apr_snprintf(buf, sizeof(buf), "%" APR_SIZE_T_FMT, namelen);
  svn_stringbuf_appendbytes(*strbuf, buf, bytes_used);
  svn_stringbuf_appendbyte(*strbuf, '\n');

  svn_stringbuf_appendbytes(*strbuf, name, namelen);
  svn_stringbuf_appendbyte(*strbuf, '\n');
}


/* Compare the node-path PATH with the (const char *) prefixes in PFXLIST.
 * Return TRUE if any prefix is a prefix of PATH (matching whole path
 * components); FALSE otherwise.
 * PATH starts with a '/', as do the (const char *) paths in PREFIXES. */
/* This function is a duplicate of svnadmin.c:ary_prefix_match(). */
static svn_boolean_t
ary_prefix_match(const apr_array_header_t *pfxlist, const char *path)
{
  int i;
  size_t path_len = strlen(path);

  for (i = 0; i < pfxlist->nelts; i++)
    {
      const char *pfx = APR_ARRAY_IDX(pfxlist, i, const char *);
      size_t pfx_len = strlen(pfx);

      if (path_len < pfx_len)
        continue;
      if (strncmp(path, pfx, pfx_len) == 0
          && (pfx_len == 1 || path[pfx_len] == '\0' || path[pfx_len] == '/'))
        return TRUE;
    }

  return FALSE;
}


/* Check whether we need to skip this PATH based on its presence in
   the PREFIXES list, and the DO_EXCLUDE option.
   PATH starts with a '/', as do the (const char *) paths in PREFIXES. */
static APR_INLINE svn_boolean_t
skip_path(const char *path, const apr_array_header_t *prefixes,
          svn_boolean_t do_exclude, svn_boolean_t glob)
{
  const svn_boolean_t matches =
    (glob
     ? svn_cstring_match_glob_list(path, prefixes)
     : ary_prefix_match(prefixes, path));

  /* NXOR */
  return (matches ? do_exclude : !do_exclude);
}



/* Note: the input stream parser calls us with events.
   Output of the filtered dump occurs for the most part streamily with the
   event callbacks, to avoid caching large quantities of data in memory.
   The exceptions this are:
   - All revision data (headers and props) must be cached until a non-skipped
     node within the revision is found, or the revision is closed.
   - Node headers and props must be cached until all props have been received
     (to allow the Prop-content-length to be found). This is signalled either
     by the node text arriving, or the node being closed.
   The writing_begun members of the associated object batons track the state.
   output_revision() and output_node() are called to cause this flushing of
   cached data to occur.
*/


/* Filtering batons */

struct revmap_t
{
  svn_revnum_t rev; /* Last non-dropped revision to which this maps. */
  svn_boolean_t was_dropped; /* Was this revision dropped? */
};

struct parse_baton_t
{
  /* Command-line options values. */
  svn_boolean_t do_exclude;
  svn_boolean_t quiet;
  svn_boolean_t glob;
  svn_boolean_t drop_empty_revs;
  svn_boolean_t drop_all_empty_revs;
  svn_boolean_t do_renumber_revs;
  svn_boolean_t preserve_revprops;
  svn_boolean_t skip_missing_merge_sources;
  svn_boolean_t allow_deltas;
  apr_array_header_t *prefixes;

  /* Input and output streams. */
  svn_stream_t *in_stream;
  svn_stream_t *out_stream;

  /* State for the filtering process. */
  apr_int32_t rev_drop_count;
  apr_hash_t *dropped_nodes;
  apr_hash_t *renumber_history;  /* svn_revnum_t -> struct revmap_t */
  svn_revnum_t last_live_revision;
  /* The oldest original revision, greater than r0, in the input
     stream which was not filtered. */
  svn_revnum_t oldest_original_rev;
};

struct revision_baton_t
{
  /* Reference to the global parse baton. */
  struct parse_baton_t *pb;

  /* Does this revision have node or prop changes? */
  svn_boolean_t has_nodes;

  /* Did we drop any nodes? */
  svn_boolean_t had_dropped_nodes;

  /* Written to output stream? */
  svn_boolean_t writing_begun;

  /* The original and new (re-mapped) revision numbers. */
  svn_revnum_t rev_orig;
  svn_revnum_t rev_actual;

  /* Pointers to dumpfile data. */
  apr_hash_t *original_headers;
  apr_hash_t *props;
};

struct node_baton_t
{
  /* Reference to the current revision baton. */
  struct revision_baton_t *rb;

  /* Are we skipping this node? */
  svn_boolean_t do_skip;

  /* Have we been instructed to change or remove props on, or change
     the text of, this node? */
  svn_boolean_t has_props;
  svn_boolean_t has_text;

  /* Written to output stream? */
  svn_boolean_t writing_begun;

  /* The text content length according to the dumpfile headers, because we
     need the length before we have the actual text. */
  svn_filesize_t tcl;

  /* Pointers to dumpfile data. */
  svn_repos__dumpfile_headers_t *headers;
  svn_stringbuf_t *props;

  /* Expect deltas? */
  svn_boolean_t has_prop_delta;
  svn_boolean_t has_text_delta;

  /* We might need the node path in a parse error message. */
  char *node_path;

  apr_pool_t *node_pool;
};



/* Filtering vtable members */

/* File-format stamp. */
static svn_error_t *
magic_header_record(int version, void *parse_baton, apr_pool_t *pool)
{
  struct parse_baton_t *pb = parse_baton;

  if (version >= SVN_REPOS_DUMPFILE_FORMAT_VERSION_DELTAS)
    pb->allow_deltas = TRUE;

  SVN_ERR(svn_stream_printf(pb->out_stream, pool,
                            SVN_REPOS_DUMPFILE_MAGIC_HEADER ": %d\n\n",
                            version));

  return SVN_NO_ERROR;
}


/* Return a deep copy of a (char * -> char *) hash. */
static apr_hash_t *
headers_dup(apr_hash_t *headers,
            apr_pool_t *pool)
{
  apr_hash_t *new_hash = apr_hash_make(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, headers); hi; hi = apr_hash_next(hi))
    {
      const char *key = apr_hash_this_key(hi);
      const char *val = apr_hash_this_val(hi);

      svn_hash_sets(new_hash, apr_pstrdup(pool, key), apr_pstrdup(pool, val));
    }
  return new_hash;
}

/* New revision: set up revision_baton, decide if we skip it. */
static svn_error_t *
new_revision_record(void **revision_baton,
                    apr_hash_t *headers,
                    void *parse_baton,
                    apr_pool_t *pool)
{
  struct revision_baton_t *rb;
  const char *rev_orig;

  *revision_baton = apr_palloc(pool, sizeof(struct revision_baton_t));
  rb = *revision_baton;
  rb->pb = parse_baton;
  rb->has_nodes = FALSE;
  rb->had_dropped_nodes = FALSE;
  rb->writing_begun = FALSE;
  rb->props = apr_hash_make(pool);
  rb->original_headers = headers_dup(headers, pool);

  rev_orig = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER);
  rb->rev_orig = SVN_STR_TO_REV(rev_orig);

  if (rb->pb->do_renumber_revs)
    rb->rev_actual = rb->rev_orig - rb->pb->rev_drop_count;
  else
    rb->rev_actual = rb->rev_orig;

  return SVN_NO_ERROR;
}


/* Output revision to dumpstream
   This may be called by new_node_record(), iff rb->has_nodes has been set
   to TRUE, or by close_revision() otherwise. This must only be called
   if rb->writing_begun is FALSE. */
static svn_error_t *
output_revision(struct revision_baton_t *rb)
{
  svn_boolean_t write_out_rev = FALSE;
  apr_pool_t *hash_pool = apr_hash_pool_get(rb->props);
  apr_pool_t *subpool = svn_pool_create(hash_pool);

  rb->writing_begun = TRUE;

  /* If this revision has no nodes left because the ones it had were
     dropped, and we are not dropping empty revisions, and we were not
     told to preserve revision props, then we want to fixup the
     revision props to only contain:
       - the date
       - a log message that reports that this revision is just stuffing. */
  if ((! rb->pb->preserve_revprops)
      && (! rb->has_nodes)
      && rb->had_dropped_nodes
      && (! rb->pb->drop_empty_revs)
      && (! rb->pb->drop_all_empty_revs))
    {
      apr_hash_t *old_props = rb->props;
      rb->props = apr_hash_make(hash_pool);
      svn_hash_sets(rb->props, SVN_PROP_REVISION_DATE,
                    svn_hash_gets(old_props, SVN_PROP_REVISION_DATE));
      svn_hash_sets(rb->props, SVN_PROP_REVISION_LOG,
                    svn_string_create(_("This is an empty revision for "
                                        "padding."), hash_pool));
    }

  /* write out the revision */
  /* Revision is written out in the following cases:
     1. If the revision has nodes or
     it is revision 0 (Special case: To preserve the props on r0).
     2. --drop-empty-revs has been supplied,
     but revision has not all nodes dropped.
     3. If no --drop-empty-revs or --drop-all-empty-revs have been supplied,
     write out the revision which has no nodes to begin with.
  */
  if (rb->has_nodes || (rb->rev_orig == 0))
    write_out_rev = TRUE;
  else if (rb->pb->drop_empty_revs)
    write_out_rev = ! rb->had_dropped_nodes;
  else if (! rb->pb->drop_all_empty_revs)
    write_out_rev = TRUE;

  if (write_out_rev)
    {
      /* This revision is a keeper. */
      SVN_ERR(svn_repos__dump_revision_record(rb->pb->out_stream,
                                              rb->rev_actual,
                                              rb->original_headers,
                                              rb->props,
                                              FALSE /*props_section_always*/,
                                              subpool));

      /* Stash the oldest original rev not dropped. */
      if (rb->rev_orig > 0
          && !SVN_IS_VALID_REVNUM(rb->pb->oldest_original_rev))
        rb->pb->oldest_original_rev = rb->rev_orig;

      if (rb->pb->do_renumber_revs)
        {
          svn_revnum_t *rr_key;
          struct revmap_t *rr_val;
          apr_pool_t *rr_pool = apr_hash_pool_get(rb->pb->renumber_history);
          rr_key = apr_palloc(rr_pool, sizeof(*rr_key));
          rr_val = apr_palloc(rr_pool, sizeof(*rr_val));
          *rr_key = rb->rev_orig;
          rr_val->rev = rb->rev_actual;
          rr_val->was_dropped = FALSE;
          apr_hash_set(rb->pb->renumber_history, rr_key,
                       sizeof(*rr_key), rr_val);
          rb->pb->last_live_revision = rb->rev_actual;
        }

      if (! rb->pb->quiet)
        SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                    _("Revision %ld committed as %ld.\n"),
                                    rb->rev_orig, rb->rev_actual));
    }
  else
    {
      /* We're dropping this revision. */
      rb->pb->rev_drop_count++;
      if (rb->pb->do_renumber_revs)
        {
          svn_revnum_t *rr_key;
          struct revmap_t *rr_val;
          apr_pool_t *rr_pool = apr_hash_pool_get(rb->pb->renumber_history);
          rr_key = apr_palloc(rr_pool, sizeof(*rr_key));
          rr_val = apr_palloc(rr_pool, sizeof(*rr_val));
          *rr_key = rb->rev_orig;
          rr_val->rev = rb->pb->last_live_revision;
          rr_val->was_dropped = TRUE;
          apr_hash_set(rb->pb->renumber_history, rr_key,
                       sizeof(*rr_key), rr_val);
        }

      if (! rb->pb->quiet)
        SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                    _("Revision %ld skipped.\n"),
                                    rb->rev_orig));
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


/* UUID record here: dump it, as we do not filter them. */
static svn_error_t *
uuid_record(const char *uuid, void *parse_baton, apr_pool_t *pool)
{
  struct parse_baton_t *pb = parse_baton;
  SVN_ERR(svn_stream_printf(pb->out_stream, pool,
                            SVN_REPOS_DUMPFILE_UUID ": %s\n\n", uuid));
  return SVN_NO_ERROR;
}


/* New node here. Set up node_baton by copying headers. */
static svn_error_t *
new_node_record(void **node_baton,
                apr_hash_t *headers,
                void *rev_baton,
                apr_pool_t *pool)
{
  struct parse_baton_t *pb;
  struct node_baton_t *nb;
  char *node_path, *copyfrom_path;
  apr_hash_index_t *hi;
  const char *tcl;

  *node_baton = apr_palloc(pool, sizeof(struct node_baton_t));
  nb          = *node_baton;
  nb->rb      = rev_baton;
  nb->node_pool = pool;
  pb          = nb->rb->pb;

  node_path = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_PATH);
  copyfrom_path = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH);

  /* Ensure that paths start with a leading '/'. */
  if (node_path[0] != '/')
    node_path = apr_pstrcat(pool, "/", node_path, SVN_VA_NULL);
  if (copyfrom_path && copyfrom_path[0] != '/')
    copyfrom_path = apr_pstrcat(pool, "/", copyfrom_path, SVN_VA_NULL);

  nb->do_skip = skip_path(node_path, pb->prefixes,
                          pb->do_exclude, pb->glob);

  /* If we're skipping the node, take note of path, discarding the
     rest.  */
  if (nb->do_skip)
    {
      svn_hash_sets(pb->dropped_nodes,
                    apr_pstrdup(apr_hash_pool_get(pb->dropped_nodes),
                                node_path),
                    (void *)1);
      nb->rb->had_dropped_nodes = TRUE;
    }
  else
    {
      const char *kind;
      const char *action;

      tcl = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH);

      /* Test if this node was copied from dropped source. */
      if (copyfrom_path &&
          skip_path(copyfrom_path, pb->prefixes, pb->do_exclude, pb->glob))
        {
          /* This node was copied from a dropped source.
             We have a problem, since we did not want to drop this node too.

             However, there is one special case we'll handle.  If the node is
             a file, and this was a copy-and-modify operation, then the
             dumpfile should contain the new contents of the file.  In this
             scenario, we'll just do an add without history using the new
             contents.  */
          kind = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_KIND);

          /* If there is a Text-content-length header, and the kind is
             "file", we just fallback to an add without history. */
          if (tcl && (strcmp(kind, "file") == 0))
            {
              svn_hash_sets(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH,
                            NULL);
              svn_hash_sets(headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV,
                            NULL);
              copyfrom_path = NULL;
            }
          /* Else, this is either a directory or a file whose contents we
             don't have readily available.  */
          else
            {
              return svn_error_createf
                (SVN_ERR_INCOMPLETE_DATA, 0,
                 _("Invalid copy source path '%s'"), copyfrom_path);
            }
        }

      nb->has_props = FALSE;
      nb->has_text = FALSE;
      nb->has_prop_delta = FALSE;
      nb->has_text_delta = FALSE;
      nb->writing_begun = FALSE;
      nb->tcl = tcl ? svn__atoui64(tcl) : 0;
      nb->headers = svn_repos__dumpfile_headers_create(pool);
      nb->props = svn_stringbuf_create_empty(pool);
      nb->node_path = apr_pstrdup(pool, node_path);

      /* Now we know for sure that we have a node that will not be
         skipped, flush the revision if it has not already been done. */
      nb->rb->has_nodes = TRUE;
      if (! nb->rb->writing_begun)
        SVN_ERR(output_revision(nb->rb));

      /* A node record is required to begin with 'Node-path', skip the
         leading '/' to match the form used by 'svnadmin dump'. */
      svn_repos__dumpfile_header_push(
        nb->headers, SVN_REPOS_DUMPFILE_NODE_PATH, node_path + 1);

      /* Node-kind is next and is optional. */
      kind = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_KIND);
      if (kind)
        svn_repos__dumpfile_header_push(
          nb->headers, SVN_REPOS_DUMPFILE_NODE_KIND, kind);

      /* Node-action is next and required. */
      action = svn_hash_gets(headers, SVN_REPOS_DUMPFILE_NODE_ACTION);
      if (action)
        svn_repos__dumpfile_header_push(
          nb->headers, SVN_REPOS_DUMPFILE_NODE_ACTION, action);
      else
        return svn_error_createf(SVN_ERR_INCOMPLETE_DATA, 0,
                                 _("Missing Node-action for path '%s'"),
                                 node_path);

      for (hi = apr_hash_first(pool, headers); hi; hi = apr_hash_next(hi))
        {
          const char *key = apr_hash_this_key(hi);
          const char *val = apr_hash_this_val(hi);

          if ((!strcmp(key, SVN_REPOS_DUMPFILE_PROP_DELTA))
              && (!strcmp(val, "true")))
            nb->has_prop_delta = TRUE;

          if ((!strcmp(key, SVN_REPOS_DUMPFILE_TEXT_DELTA))
              && (!strcmp(val, "true")))
            nb->has_text_delta = TRUE;

          if ((!strcmp(key, SVN_REPOS_DUMPFILE_CONTENT_LENGTH))
              || (!strcmp(key, SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH))
              || (!strcmp(key, SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH))
              || (!strcmp(key, SVN_REPOS_DUMPFILE_NODE_PATH))
              || (!strcmp(key, SVN_REPOS_DUMPFILE_NODE_KIND))
              || (!strcmp(key, SVN_REPOS_DUMPFILE_NODE_ACTION)))
            continue;

          /* Rewrite Node-Copyfrom-Rev if we are renumbering revisions.
             The number points to some revision in the past. We keep track
             of revision renumbering in an apr_hash, which maps original
             revisions to new ones. Dropped revision are mapped to -1.
             This should never happen here.
          */
          if (pb->do_renumber_revs
              && (!strcmp(key, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV)))
            {
              svn_revnum_t cf_orig_rev;
              struct revmap_t *cf_renum_val;

              cf_orig_rev = SVN_STR_TO_REV(val);
              cf_renum_val = apr_hash_get(pb->renumber_history,
                                          &cf_orig_rev,
                                          sizeof(cf_orig_rev));
              if (! (cf_renum_val && SVN_IS_VALID_REVNUM(cf_renum_val->rev)))
                return svn_error_createf
                  (SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                   _("No valid copyfrom revision in filtered stream"));
              svn_repos__dumpfile_header_pushf(
                nb->headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV,
                "%ld", cf_renum_val->rev);
              continue;
            }

          /* passthru: put header straight to output */
          svn_repos__dumpfile_header_push(nb->headers, key, val);
        }
    }

  return SVN_NO_ERROR;
}


/* Examine the mergeinfo in INITIAL_VAL, omitting missing merge
   sources or renumbering revisions in rangelists as appropriate, and
   return the (possibly new) mergeinfo in *FINAL_VAL (allocated from
   POOL). */
static svn_error_t *
adjust_mergeinfo(svn_string_t **final_val, const svn_string_t *initial_val,
                 struct revision_baton_t *rb, apr_pool_t *pool)
{
  apr_hash_t *mergeinfo;
  apr_hash_t *final_mergeinfo = apr_hash_make(pool);
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_mergeinfo_parse(&mergeinfo, initial_val->data, subpool));

  /* Issue #3020: If we are skipping missing merge sources, then also
     filter mergeinfo ranges as old or older than the oldest revision in the
     dump stream.  Those older than the oldest obviously refer to history
     outside of the dump stream.  The oldest rev itself is present in the
     dump, but cannot be a valid merge source revision since it is the
     start of all history.  E.g. if we dump -r100:400 then dumpfilter the
     result with --skip-missing-merge-sources, any mergeinfo with revision
     100 implies a change of -r99:100, but r99 is part of the history we
     want filtered.

     If the oldest rev is r0 then there is nothing to filter. */

  /* ### This seems to cater only for use cases where the revisions being
         processed are not following on from revisions that will already
         exist in the destination repository. If the revisions being
         processed do follow on, then we might want to keep the mergeinfo
         that refers to those older revisions. */

  if (rb->pb->skip_missing_merge_sources && rb->pb->oldest_original_rev > 0)
    SVN_ERR(svn_mergeinfo__filter_mergeinfo_by_ranges(
      &mergeinfo, mergeinfo,
      rb->pb->oldest_original_rev, 0,
      FALSE, subpool, subpool));

  for (hi = apr_hash_first(subpool, mergeinfo); hi; hi = apr_hash_next(hi))
    {
      const char *merge_source = apr_hash_this_key(hi);
      svn_rangelist_t *rangelist = apr_hash_this_val(hi);
      struct parse_baton_t *pb = rb->pb;

      /* Determine whether the merge_source is a part of the prefix. */
      if (skip_path(merge_source, pb->prefixes, pb->do_exclude, pb->glob))
        {
          if (pb->skip_missing_merge_sources)
            continue;
          else
            return svn_error_createf(SVN_ERR_INCOMPLETE_DATA, 0,
                                     _("Missing merge source path '%s'; try "
                                       "with --skip-missing-merge-sources"),
                                     merge_source);
        }

      /* Possibly renumber revisions in merge source's rangelist. */
      if (pb->do_renumber_revs)
        {
          int i;

          for (i = 0; i < rangelist->nelts; i++)
            {
              struct revmap_t *revmap_start;
              struct revmap_t *revmap_end;
              svn_merge_range_t *range = APR_ARRAY_IDX(rangelist, i,
                                                       svn_merge_range_t *);

              revmap_start = apr_hash_get(pb->renumber_history,
                                          &range->start, sizeof(range->start));
              if (! (revmap_start && SVN_IS_VALID_REVNUM(revmap_start->rev)))
                return svn_error_createf
                  (SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                   _("No valid revision range 'start' in filtered stream"));

              revmap_end = apr_hash_get(pb->renumber_history,
                                        &range->end, sizeof(range->end));
              if (! (revmap_end && SVN_IS_VALID_REVNUM(revmap_end->rev)))
                return svn_error_createf
                  (SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                   _("No valid revision range 'end' in filtered stream"));

              range->start = revmap_start->rev;
              range->end = revmap_end->rev;
            }
        }
      svn_hash_sets(final_mergeinfo, merge_source, rangelist);
    }

  SVN_ERR(svn_mergeinfo__canonicalize_ranges(final_mergeinfo, subpool));
  SVN_ERR(svn_mergeinfo_to_string(final_val, final_mergeinfo, pool));
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
set_revision_property(void *revision_baton,
                      const char *name,
                      const svn_string_t *value)
{
  struct revision_baton_t *rb = revision_baton;
  apr_pool_t *hash_pool = apr_hash_pool_get(rb->props);

  svn_hash_sets(rb->props,
                apr_pstrdup(hash_pool, name),
                svn_string_dup(value, hash_pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
set_node_property(void *node_baton,
                  const char *name,
                  const svn_string_t *value)
{
  struct node_baton_t *nb = node_baton;
  struct revision_baton_t *rb = nb->rb;

  if (nb->do_skip)
    return SVN_NO_ERROR;

  /* Try to detect if a delta-mode property occurs unexpectedly. HAS_PROPS
     can be false here only if the parser didn't call remove_node_props(),
     so this may indicate a bug rather than bad data. */
  if (! (nb->has_props || nb->has_prop_delta))
    return svn_error_createf(SVN_ERR_STREAM_MALFORMED_DATA, NULL,
                             _("Delta property block detected, but deltas "
                               "are not enabled for node '%s' in original "
                               "revision %ld"),
                             nb->node_path, rb->rev_orig);

  if (strcmp(name, SVN_PROP_MERGEINFO) == 0)
    {
      svn_string_t *filtered_mergeinfo;  /* Avoid compiler warning. */
      apr_pool_t *pool = apr_hash_pool_get(rb->props);
      SVN_ERR(adjust_mergeinfo(&filtered_mergeinfo, value, rb, pool));
      value = filtered_mergeinfo;
    }

  nb->has_props = TRUE;
  write_prop_to_stringbuf(nb->props, name, value);

  return SVN_NO_ERROR;
}


static svn_error_t *
delete_node_property(void *node_baton, const char *name)
{
  struct node_baton_t *nb = node_baton;
  struct revision_baton_t *rb = nb->rb;

  if (nb->do_skip)
    return SVN_NO_ERROR;

  if (!nb->has_prop_delta)
    return svn_error_createf(SVN_ERR_STREAM_MALFORMED_DATA, NULL,
                             _("Delta property block detected, but deltas "
                               "are not enabled for node '%s' in original "
                               "revision %ld"),
                             nb->node_path, rb->rev_orig);

  nb->has_props = TRUE;
  write_propdel_to_stringbuf(&(nb->props), name);

  return SVN_NO_ERROR;
}


/* The parser calls this method if the node record has a non-delta
 * property content section, before any calls to set_node_property().
 * If the node record uses property deltas, this is not called.
 */
static svn_error_t *
remove_node_props(void *node_baton)
{
  struct node_baton_t *nb = node_baton;

  /* In this case, not actually indicating that the node *has* props,
     rather that it has a property content section. */
  nb->has_props = TRUE;

  return SVN_NO_ERROR;
}


static svn_error_t *
set_fulltext(svn_stream_t **stream, void *node_baton)
{
  struct node_baton_t *nb = node_baton;

  if (!nb->do_skip)
    {
      nb->has_text = TRUE;
      if (! nb->writing_begun)
        {
          nb->writing_begun = TRUE;
          if (nb->has_props)
            {
              svn_stringbuf_appendcstr(nb->props, "PROPS-END\n");
            }
          SVN_ERR(svn_repos__dump_node_record(nb->rb->pb->out_stream,
                                              nb->headers,
                                              nb->has_props ? nb->props : NULL,
                                              nb->has_text,
                                              nb->tcl,
                                              TRUE /*content_length_always*/,
                                              nb->node_pool));
        }
      *stream = nb->rb->pb->out_stream;
    }

  return SVN_NO_ERROR;
}


/* Finalize node */
static svn_error_t *
close_node(void *node_baton)
{
  struct node_baton_t *nb = node_baton;
  apr_size_t len = 2;

  /* Get out of here if we can. */
  if (nb->do_skip)
    return SVN_NO_ERROR;

  /* If the node was not flushed already to output its text, do it now. */
  if (! nb->writing_begun)
    {
      nb->writing_begun = TRUE;
      if (nb->has_props)
        {
          svn_stringbuf_appendcstr(nb->props, "PROPS-END\n");
        }
      SVN_ERR(svn_repos__dump_node_record(nb->rb->pb->out_stream,
                                          nb->headers,
                                          nb->has_props ? nb->props : NULL,
                                          nb->has_text,
                                          nb->tcl,
                                          TRUE /*content_length_always*/,
                                          nb->node_pool));
    }

  /* put an end to node. */
  SVN_ERR(svn_stream_write(nb->rb->pb->out_stream, "\n\n", &len));

  return SVN_NO_ERROR;
}


/* Finalize revision */
static svn_error_t *
close_revision(void *revision_baton)
{
  struct revision_baton_t *rb = revision_baton;

  /* If no node has yet flushed the revision, do it now. */
  if (! rb->writing_begun)
    return output_revision(rb);
  else
    return SVN_NO_ERROR;
}


/* Filtering vtable */
static svn_repos_parse_fns3_t filtering_vtable =
  {
    magic_header_record,
    uuid_record,
    new_revision_record,
    new_node_record,
    set_revision_property,
    set_node_property,
    delete_node_property,
    remove_node_props,
    set_fulltext,
    NULL,
    close_node,
    close_revision
  };



/** Subcommands. **/

static svn_opt_subcommand_t
  subcommand_help,
  subcommand_exclude,
  subcommand_include;

enum
  {
    svndumpfilter__drop_empty_revs = SVN_OPT_FIRST_LONGOPT_ID,
    svndumpfilter__drop_all_empty_revs,
    svndumpfilter__renumber_revs,
    svndumpfilter__preserve_revprops,
    svndumpfilter__skip_missing_merge_sources,
    svndumpfilter__targets,
    svndumpfilter__quiet,
    svndumpfilter__glob,
    svndumpfilter__version
  };

/* Option codes and descriptions.
 *
 * The entire list must be terminated with an entry of nulls.
 */
static const apr_getopt_option_t options_table[] =
  {
    {"help",          'h', 0,
     N_("show help on a subcommand")},

    {NULL,            '?', 0,
     N_("show help on a subcommand")},

    {"version",            svndumpfilter__version, 0,
     N_("show program version information") },
    {"quiet",              svndumpfilter__quiet, 0,
     N_("Do not display filtering statistics.") },
    {"pattern",            svndumpfilter__glob, 0,
     N_("Treat the path prefixes as file glob patterns.\n"
        "                             Glob special characters are '*' '?' '[]' and '\\'.\n"
        "                             Character '/' is not treated specially, so\n"
        "                             pattern /*/foo matches paths /a/foo and /a/b/foo.") },
    {"drop-empty-revs",    svndumpfilter__drop_empty_revs, 0,
     N_("Remove revisions emptied by filtering.")},
    {"drop-all-empty-revs",    svndumpfilter__drop_all_empty_revs, 0,
     N_("Remove all empty revisions found in dumpstream\n"
        "                             except revision 0.")},
    {"renumber-revs",      svndumpfilter__renumber_revs, 0,
     N_("Renumber revisions left after filtering.") },
    {"skip-missing-merge-sources",
     svndumpfilter__skip_missing_merge_sources, 0,
     N_("Skip missing merge sources.") },
    {"preserve-revprops",  svndumpfilter__preserve_revprops, 0,
     N_("Don't filter revision properties.") },
    {"targets", svndumpfilter__targets, 1,
     N_("Read additional prefixes, one per line, from\n"
        "                             file ARG.")},
    {NULL}
  };


/* Array of available subcommands.
 * The entire list must be terminated with an entry of nulls.
 */
static const svn_opt_subcommand_desc2_t cmd_table[] =
  {
    {"exclude", subcommand_exclude, {0},
     N_("Filter out nodes with given prefixes from dumpstream.\n"
        "usage: svndumpfilter exclude PATH_PREFIX...\n"),
     {svndumpfilter__drop_empty_revs, svndumpfilter__drop_all_empty_revs,
      svndumpfilter__renumber_revs,
      svndumpfilter__skip_missing_merge_sources, svndumpfilter__targets,
      svndumpfilter__preserve_revprops, svndumpfilter__quiet,
      svndumpfilter__glob} },

    {"include", subcommand_include, {0},
     N_("Filter out nodes without given prefixes from dumpstream.\n"
        "usage: svndumpfilter include PATH_PREFIX...\n"),
     {svndumpfilter__drop_empty_revs, svndumpfilter__drop_all_empty_revs,
      svndumpfilter__renumber_revs,
      svndumpfilter__skip_missing_merge_sources, svndumpfilter__targets,
      svndumpfilter__preserve_revprops, svndumpfilter__quiet,
      svndumpfilter__glob} },

    {"help", subcommand_help, {"?", "h"},
     N_("Describe the usage of this program or its subcommands.\n"
        "usage: svndumpfilter help [SUBCOMMAND...]\n"),
     {0} },

    { NULL, NULL, {0}, NULL, {0} }
  };


/* Baton for passing option/argument state to a subcommand function. */
struct svndumpfilter_opt_state
{
  svn_opt_revision_t start_revision;     /* -r X[:Y] is         */
  svn_opt_revision_t end_revision;       /* not implemented.    */
  svn_boolean_t quiet;                   /* --quiet             */
  svn_boolean_t glob;                    /* --pattern           */
  svn_boolean_t version;                 /* --version           */
  svn_boolean_t drop_empty_revs;         /* --drop-empty-revs   */
  svn_boolean_t drop_all_empty_revs;     /* --drop-all-empty-revs */
  svn_boolean_t help;                    /* --help or -?        */
  svn_boolean_t renumber_revs;           /* --renumber-revs     */
  svn_boolean_t preserve_revprops;       /* --preserve-revprops */
  svn_boolean_t skip_missing_merge_sources;
                                         /* --skip-missing-merge-sources */
  const char *targets_file;              /* --targets-file       */
  apr_array_header_t *prefixes;          /* mainargs.           */
};


static svn_error_t *
parse_baton_initialize(struct parse_baton_t **pb,
                       struct svndumpfilter_opt_state *opt_state,
                       svn_boolean_t do_exclude,
                       apr_pool_t *pool)
{
  struct parse_baton_t *baton = apr_palloc(pool, sizeof(*baton));

  /* Read the stream from STDIN.  Users can redirect a file. */
  SVN_ERR(svn_stream_for_stdin2(&baton->in_stream, TRUE, pool));

  /* Have the parser dump results to STDOUT. Users can redirect a file. */
  SVN_ERR(svn_stream_for_stdout(&baton->out_stream, pool));

  baton->do_exclude = do_exclude;

  /* Ignore --renumber-revs if there can't possibly be
     anything to renumber. */
  baton->do_renumber_revs =
    (opt_state->renumber_revs && (opt_state->drop_empty_revs
                                  || opt_state->drop_all_empty_revs));

  baton->drop_empty_revs = opt_state->drop_empty_revs;
  baton->drop_all_empty_revs = opt_state->drop_all_empty_revs;
  baton->preserve_revprops = opt_state->preserve_revprops;
  baton->quiet = opt_state->quiet;
  baton->glob = opt_state->glob;
  baton->prefixes = opt_state->prefixes;
  baton->skip_missing_merge_sources = opt_state->skip_missing_merge_sources;
  baton->rev_drop_count = 0; /* used to shift revnums while filtering */
  baton->dropped_nodes = apr_hash_make(pool);
  baton->renumber_history = apr_hash_make(pool);
  baton->last_live_revision = SVN_INVALID_REVNUM;
  baton->oldest_original_rev = SVN_INVALID_REVNUM;
  baton->allow_deltas = FALSE;

  *pb = baton;
  return SVN_NO_ERROR;
}

/* This implements `help` subcommand. */
static svn_error_t *
subcommand_help(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svndumpfilter_opt_state *opt_state = baton;
  const char *header =
    _("general usage: svndumpfilter SUBCOMMAND [ARGS & OPTIONS ...]\n"
      "Subversion repository dump filtering tool.\n"
      "Type 'svndumpfilter help <subcommand>' for help on a "
      "specific subcommand.\n"
      "Type 'svndumpfilter --version' to see the program version.\n"
      "\n"
      "Available subcommands:\n");

  SVN_ERR(svn_opt_print_help4(os, "svndumpfilter",
                              opt_state ? opt_state->version : FALSE,
                              opt_state ? opt_state->quiet : FALSE,
                              /*###opt_state ? opt_state->verbose :*/ FALSE,
                              NULL, header, cmd_table, options_table,
                              NULL, NULL, pool));

  return SVN_NO_ERROR;
}


/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_repos", svn_repos_version },
      { "svn_delta", svn_delta_version },
      { NULL, NULL }
    };
  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list2(&my_version, checklist, svn_ver_equal);
}


/* Do the real work of filtering. */
static svn_error_t *
do_filter(apr_getopt_t *os,
          void *baton,
          svn_boolean_t do_exclude,
          apr_pool_t *pool)
{
  struct svndumpfilter_opt_state *opt_state = baton;
  struct parse_baton_t *pb;
  apr_hash_index_t *hi;
  apr_array_header_t *keys;
  int i, num_keys;

  if (! opt_state->quiet)
    {
      apr_pool_t *subpool = svn_pool_create(pool);

      if (opt_state->glob)
        {
          SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                      do_exclude
                                      ? (opt_state->drop_empty_revs
                                         || opt_state->drop_all_empty_revs)
                                        ? _("Excluding (and dropping empty "
                                            "revisions for) prefix patterns:\n")
                                        : _("Excluding prefix patterns:\n")
                                      : (opt_state->drop_empty_revs
                                         || opt_state->drop_all_empty_revs)
                                        ? _("Including (and dropping empty "
                                            "revisions for) prefix patterns:\n")
                                        : _("Including prefix patterns:\n")));
        }
      else
        {
          SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                      do_exclude
                                      ? (opt_state->drop_empty_revs
                                         || opt_state->drop_all_empty_revs)
                                        ? _("Excluding (and dropping empty "
                                            "revisions for) prefixes:\n")
                                        : _("Excluding prefixes:\n")
                                      : (opt_state->drop_empty_revs
                                         || opt_state->drop_all_empty_revs)
                                        ? _("Including (and dropping empty "
                                            "revisions for) prefixes:\n")
                                        : _("Including prefixes:\n")));
        }

      for (i = 0; i < opt_state->prefixes->nelts; i++)
        {
          svn_pool_clear(subpool);
          SVN_ERR(svn_cmdline_fprintf
                  (stderr, subpool, "   '%s'\n",
                   APR_ARRAY_IDX(opt_state->prefixes, i, const char *)));
        }

      SVN_ERR(svn_cmdline_fputs("\n", stderr, subpool));
      svn_pool_destroy(subpool);
    }

  SVN_ERR(parse_baton_initialize(&pb, opt_state, do_exclude, pool));
  SVN_ERR(svn_repos_parse_dumpstream3(pb->in_stream, &filtering_vtable, pb,
                                      TRUE, NULL, NULL, pool));

  /* The rest of this is just reporting.  If we aren't reporting, get
     outta here. */
  if (opt_state->quiet)
    return SVN_NO_ERROR;

  SVN_ERR(svn_cmdline_fputs("\n", stderr, pool));

  if (pb->rev_drop_count)
    SVN_ERR(svn_cmdline_fprintf(stderr, pool,
                                Q_("Dropped %d revision.\n\n",
                                   "Dropped %d revisions.\n\n",
                                   pb->rev_drop_count),
                                pb->rev_drop_count));

  if (pb->do_renumber_revs)
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      SVN_ERR(svn_cmdline_fputs(_("Revisions renumbered as follows:\n"),
                                stderr, subpool));

      /* Get the keys of the hash, sort them, then print the hash keys
         and values, sorted by keys. */
      num_keys = apr_hash_count(pb->renumber_history);
      keys = apr_array_make(pool, num_keys + 1, sizeof(svn_revnum_t));
      for (hi = apr_hash_first(pool, pb->renumber_history);
           hi;
           hi = apr_hash_next(hi))
        {
          const svn_revnum_t *revnum = apr_hash_this_key(hi);

          APR_ARRAY_PUSH(keys, svn_revnum_t) = *revnum;
        }
      svn_sort__array(keys, svn_sort_compare_revisions);
      for (i = 0; i < keys->nelts; i++)
        {
          svn_revnum_t this_key;
          struct revmap_t *this_val;

          svn_pool_clear(subpool);
          this_key = APR_ARRAY_IDX(keys, i, svn_revnum_t);
          this_val = apr_hash_get(pb->renumber_history, &this_key,
                                  sizeof(this_key));
          if (this_val->was_dropped)
            SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                        _("   %ld => (dropped)\n"),
                                        this_key));
          else
            SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                        "   %ld => %ld\n",
                                        this_key, this_val->rev));
        }
      SVN_ERR(svn_cmdline_fputs("\n", stderr, subpool));
      svn_pool_destroy(subpool);
    }

  if ((num_keys = apr_hash_count(pb->dropped_nodes)))
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      SVN_ERR(svn_cmdline_fprintf(stderr, subpool,
                                  Q_("Dropped %d node:\n",
                                     "Dropped %d nodes:\n",
                                     num_keys),
                                  num_keys));

      /* Get the keys of the hash, sort them, then print the hash keys
         and values, sorted by keys. */
      keys = apr_array_make(pool, num_keys + 1, sizeof(const char *));
      for (hi = apr_hash_first(pool, pb->dropped_nodes);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *path = apr_hash_this_key(hi);

          APR_ARRAY_PUSH(keys, const char *) = path;
        }
      svn_sort__array(keys, svn_sort_compare_paths);
      for (i = 0; i < keys->nelts; i++)
        {
          svn_pool_clear(subpool);
          SVN_ERR(svn_cmdline_fprintf
                  (stderr, subpool, "   '%s'\n",
                   (const char *)APR_ARRAY_IDX(keys, i, const char *)));
        }
      SVN_ERR(svn_cmdline_fputs("\n", stderr, subpool));
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}

/* This implements `exclude' subcommand. */
static svn_error_t *
subcommand_exclude(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  return do_filter(os, baton, TRUE, pool);
}


/* This implements `include` subcommand. */
static svn_error_t *
subcommand_include(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  return do_filter(os, baton, FALSE, pool);
}



/** Main. **/

/*
 * On success, leave *EXIT_CODE untouched and return SVN_NO_ERROR. On error,
 * either return an error to be displayed, or set *EXIT_CODE to non-zero and
 * return SVN_NO_ERROR.
 */
static svn_error_t *
sub_main(int *exit_code, int argc, const char *argv[], apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;

  const svn_opt_subcommand_desc2_t *subcommand = NULL;
  struct svndumpfilter_opt_state opt_state;
  apr_getopt_t *os;
  int opt_id;
  apr_array_header_t *received_opts;
  int i;

  /* Check library versions */
  SVN_ERR(check_lib_versions());

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  /* Initialize the FS library. */
  SVN_ERR(svn_fs_initialize(pool));

  if (argc <= 1)
    {
      SVN_ERR(subcommand_help(NULL, NULL, pool));
      *exit_code = EXIT_FAILURE;
      return SVN_NO_ERROR;
    }

  /* Initialize opt_state. */
  memset(&opt_state, 0, sizeof(opt_state));
  opt_state.start_revision.kind = svn_opt_revision_unspecified;
  opt_state.end_revision.kind = svn_opt_revision_unspecified;

  /* Parse options. */
  SVN_ERR(svn_cmdline__getopt_init(&os, argc, argv, pool));

  os->interleave = 1;
  while (1)
    {
      const char *opt_arg;

      /* Parse the next option. */
      apr_err = apr_getopt_long(os, options_table, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          SVN_ERR(subcommand_help(NULL, NULL, pool));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }

      /* Stash the option code in an array before parsing it. */
      APR_ARRAY_PUSH(received_opts, int) = opt_id;

      switch (opt_id)
        {
        case 'h':
        case '?':
          opt_state.help = TRUE;
          break;
        case svndumpfilter__version:
          opt_state.version = TRUE;
          break;
        case svndumpfilter__quiet:
          opt_state.quiet = TRUE;
          break;
        case svndumpfilter__glob:
          opt_state.glob = TRUE;
          break;
        case svndumpfilter__drop_empty_revs:
          opt_state.drop_empty_revs = TRUE;
          break;
        case svndumpfilter__drop_all_empty_revs:
          opt_state.drop_all_empty_revs = TRUE;
          break;
        case svndumpfilter__renumber_revs:
          opt_state.renumber_revs = TRUE;
          break;
        case svndumpfilter__preserve_revprops:
          opt_state.preserve_revprops = TRUE;
          break;
        case svndumpfilter__skip_missing_merge_sources:
          opt_state.skip_missing_merge_sources = TRUE;
          break;
        case svndumpfilter__targets:
          SVN_ERR(svn_utf_cstring_to_utf8(&opt_state.targets_file,
                                          opt_arg, pool));
          break;
        default:
          {
            SVN_ERR(subcommand_help(NULL, NULL, pool));
            *exit_code = EXIT_FAILURE;
            return SVN_NO_ERROR;
          }
        }  /* close `switch' */
    }  /* close `while' */

  /* Disallow simultaneous use of both --drop-empty-revs and
     --drop-all-empty-revs. */
  if (opt_state.drop_empty_revs && opt_state.drop_all_empty_revs)
    {
      return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS,
                              NULL,
                              _("--drop-empty-revs cannot be used with "
                                "--drop-all-empty-revs"));
    }

  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is subcommand_help(). */
  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand2(cmd_table, "help");

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_state.version)
            {
              /* Use the "help" subcommand to handle the "--version" option. */
              static const svn_opt_subcommand_desc2_t pseudo_cmd =
                { "--version", subcommand_help, {0}, "",
                  {svndumpfilter__version,  /* must accept its own option */
                   svndumpfilter__quiet,
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              svn_error_clear(svn_cmdline_fprintf
                              (stderr, pool,
                               _("Subcommand argument required\n")));
              SVN_ERR(subcommand_help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
      else
        {
          const char *first_arg;

          SVN_ERR(svn_utf_cstring_to_utf8(&first_arg, os->argv[os->ind++],
                                          pool));
          subcommand = svn_opt_get_canonical_subcommand2(cmd_table, first_arg);
          if (subcommand == NULL)
            {
              svn_error_clear(
                svn_cmdline_fprintf(stderr, pool,
                                    _("Unknown subcommand: '%s'\n"),
                                    first_arg));
              SVN_ERR(subcommand_help(NULL, NULL, pool));
              *exit_code = EXIT_FAILURE;
              return SVN_NO_ERROR;
            }
        }
    }

  /* If there's a second argument, it's probably [one of] prefixes.
     Every subcommand except `help' requires at least one, so we parse
     them out here and store in opt_state. */

  if (subcommand->cmd_func != subcommand_help)
    {

      opt_state.prefixes = apr_array_make(pool, os->argc - os->ind,
                                          sizeof(const char *));
      for (i = os->ind ; i< os->argc; i++)
        {
          const char *prefix;

          /* Ensure that each prefix is UTF8-encoded, in internal
             style, and absolute. */
          SVN_ERR(svn_utf_cstring_to_utf8(&prefix, os->argv[i], pool));
          prefix = svn_relpath__internal_style(prefix, pool);
          if (prefix[0] != '/')
            prefix = apr_pstrcat(pool, "/", prefix, SVN_VA_NULL);
          APR_ARRAY_PUSH(opt_state.prefixes, const char *) = prefix;
        }

      if (opt_state.targets_file)
        {
          svn_stringbuf_t *buffer, *buffer_utf8;
          apr_array_header_t *targets = apr_array_make(pool, 0,
                                                       sizeof(const char *));

          /* We need to convert to UTF-8 now, even before we divide
             the targets into an array, because otherwise we wouldn't
             know what delimiter to use for svn_cstring_split().  */
          SVN_ERR(svn_stringbuf_from_file2(&buffer, opt_state.targets_file,
                                           pool));
          SVN_ERR(svn_utf_stringbuf_to_utf8(&buffer_utf8, buffer, pool));

          targets = apr_array_append(pool,
                         svn_cstring_split(buffer_utf8->data, "\n\r",
                                           TRUE, pool),
                         targets);

          for (i = 0; i < targets->nelts; i++)
            {
              const char *prefix = APR_ARRAY_IDX(targets, i, const char *);
              if (prefix[0] != '/')
                prefix = apr_pstrcat(pool, "/", prefix, SVN_VA_NULL);
              APR_ARRAY_PUSH(opt_state.prefixes, const char *) = prefix;
            }
        }

      if (apr_is_empty_array(opt_state.prefixes))
        {
          svn_error_clear(svn_cmdline_fprintf
                          (stderr, pool,
                           _("\nError: no prefixes supplied.\n")));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }


  /* Check that the subcommand wasn't passed any inappropriate options. */
  for (i = 0; i < received_opts->nelts; i++)
    {
      opt_id = APR_ARRAY_IDX(received_opts, i, int);

      /* All commands implicitly accept --help, so just skip over this
         when we see it. Note that we don't want to include this option
         in their "accepted options" list because it would be awfully
         redundant to display it in every commands' help text. */
      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id, NULL))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, options_table, subcommand,
                                          pool);
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            SVN_ERR(subcommand_help(NULL, NULL, pool));
          else
            svn_error_clear(svn_cmdline_fprintf
                            (stderr, pool,
                             _("Subcommand '%s' doesn't accept option '%s'\n"
                               "Type 'svndumpfilter help %s' for usage.\n"),
                             subcommand->name, optstr, subcommand->name));
          *exit_code = EXIT_FAILURE;
          return SVN_NO_ERROR;
        }
    }

  /* Run the subcommand. */
  err = (*subcommand->cmd_func)(os, &opt_state, pool);
  if (err)
    {
      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrap(err,
                                     _("Try 'svndumpfilter help' for more "
                                       "info"));
        }
      return err;
    }

  return SVN_NO_ERROR;
}

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  int exit_code = EXIT_SUCCESS;
  svn_error_t *err;

  /* Initialize the app. */
  if (svn_cmdline_init("svndumpfilter", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a separate mutexless allocator,
   * given this application is single threaded.
   */
  pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  err = sub_main(&exit_code, argc, argv, pool);

  /* Flush stdout and report if it fails. It would be flushed on exit anyway
     but this makes sure that output is not silently lost if it fails. */
  err = svn_error_compose_create(err, svn_cmdline_fflush(stdout));

  if (err)
    {
      exit_code = EXIT_FAILURE;
      svn_cmdline_handle_exit_error(err, NULL, "svndumpfilter: ");
    }

  svn_pool_destroy(pool);
  return exit_code;
}
