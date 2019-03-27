/*
 * xml.c :  standard XML parsing routines for ra_serf
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



#include <apr_uri.h>
#include <serf.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_path.h"

#include "svn_private_config.h"
#include "private/svn_string_private.h"

#include "ra_serf.h"


/* Read/write chunks of this size into the spillbuf.  */
#define PARSE_CHUNK_SIZE 8000


struct svn_ra_serf__xml_context_t {
  /* Current state information.  */
  svn_ra_serf__xml_estate_t *current;

  /* If WAITING >= then we are waiting for an element to close before
     resuming events. The number stored here is the amount of nested
     elements open. The Xml parser will make sure the document is well
     formed. */
  int waiting;

  /* The transition table.  */
  const svn_ra_serf__xml_transition_t *ttable;

  /* The callback information.  */
  svn_ra_serf__xml_opened_t opened_cb;
  svn_ra_serf__xml_closed_t closed_cb;
  svn_ra_serf__xml_cdata_t cdata_cb;
  void *baton;

  /* Linked list of free states.  */
  svn_ra_serf__xml_estate_t *free_states;

#ifdef SVN_DEBUG
  /* Used to verify we are not re-entering a callback, specifically to
     ensure SCRATCH_POOL is not cleared while an outer callback is
     trying to use it.  */
  svn_boolean_t within_callback;
#define START_CALLBACK(xmlctx) \
  do {                                                    \
    svn_ra_serf__xml_context_t *xmlctx__tmp = (xmlctx);   \
    SVN_ERR_ASSERT(!xmlctx__tmp->within_callback);        \
    xmlctx__tmp->within_callback = TRUE;                  \
  } while (0)
#define END_CALLBACK(xmlctx) ((xmlctx)->within_callback = FALSE)
#else
#define START_CALLBACK(xmlctx)  /* empty */
#define END_CALLBACK(xmlctx)  /* empty */
#endif /* SVN_DEBUG  */

  apr_pool_t *scratch_pool;

};

/* Structure which represents an XML namespace. */
typedef struct svn_ra_serf__ns_t {
  /* The assigned name. */
  const char *xmlns;
  /* The full URL for this namespace. */
  const char *url;
  /* The next namespace in our list. */
  struct svn_ra_serf__ns_t *next;
} svn_ra_serf__ns_t;

struct svn_ra_serf__xml_estate_t {
  /* The current state value.  */
  int state;

  /* The xml tag that opened this state. Waiting for the tag to close.  */
  svn_ra_serf__dav_props_t tag;

  /* Should the CLOSED_CB function be called for custom processing when
     this tag is closed?  */
  svn_boolean_t custom_close;

  /* A pool may be constructed for this state.  */
  apr_pool_t *state_pool;

  /* The namespaces extent for this state/element. This will start with
     the parent's NS_LIST, and we will push new namespaces into our
     local list. The parent will be unaffected by our locally-scoped data. */
  svn_ra_serf__ns_t *ns_list;

  /* Any collected attribute values. char * -> svn_string_t *. May be NULL
     if no attributes have been collected.  */
  apr_hash_t *attrs;

  /* Any collected cdata. May be NULL if no cdata is being collected.  */
  svn_stringbuf_t *cdata;

  /* Previous/outer state.  */
  svn_ra_serf__xml_estate_t *prev;

};

struct expat_ctx_t {
  svn_ra_serf__xml_context_t *xmlctx;
  svn_xml_parser_t *parser;
  svn_ra_serf__handler_t *handler;
  const int *expected_status;

  /* Do not use this pool for allocation. It is merely recorded for running
     the cleanup handler.  */
  apr_pool_t *cleanup_pool;
};


static void
define_namespaces(svn_ra_serf__ns_t **ns_list,
                  const char *const *attrs,
                  apr_pool_t *(*get_pool)(void *baton),
                  void *baton)
{
  const char *const *tmp_attrs = attrs;

  for (tmp_attrs = attrs; *tmp_attrs != NULL; tmp_attrs += 2)
    {
      if (strncmp(*tmp_attrs, "xmlns", 5) == 0)
        {
          const svn_ra_serf__ns_t *cur_ns;
          svn_boolean_t found = FALSE;
          const char *prefix;

          /* The empty prefix, or a named-prefix.  */
          if (tmp_attrs[0][5] == ':')
            prefix = &tmp_attrs[0][6];
          else
            prefix = "";

          /* Have we already defined this ns previously? */
          for (cur_ns = *ns_list; cur_ns; cur_ns = cur_ns->next)
            {
              if (strcmp(cur_ns->xmlns, prefix) == 0)
                {
                  found = TRUE;
                  break;
                }
            }

          if (!found)
            {
              apr_pool_t *pool;
              svn_ra_serf__ns_t *new_ns;

              if (get_pool)
                pool = get_pool(baton);
              else
                pool = baton;
              new_ns = apr_palloc(pool, sizeof(*new_ns));
              new_ns->xmlns = apr_pstrdup(pool, prefix);
              new_ns->url = apr_pstrdup(pool, tmp_attrs[1]);

              /* Push into the front of NS_LIST. Parent states will point
                 to later in the chain, so will be unaffected by
                 shadowing/other namespaces pushed onto NS_LIST.  */
              new_ns->next = *ns_list;
              *ns_list = new_ns;
            }
        }
    }
}

/*
 * Look up @a name in the @a ns_list list for previously declared namespace
 * definitions.
 *
 * Return (in @a *returned_prop_name) a #svn_ra_serf__dav_props_t tuple
 * representing the expanded name.
 */
static void
expand_ns(svn_ra_serf__dav_props_t *returned_prop_name,
                       const svn_ra_serf__ns_t *ns_list,
                       const char *name)
{
  const char *colon;

  colon = strchr(name, ':');
  if (colon)
    {
      const svn_ra_serf__ns_t *ns;

      for (ns = ns_list; ns; ns = ns->next)
        {
          if (strncmp(ns->xmlns, name, colon - name) == 0)
            {
              returned_prop_name->xmlns = ns->url;
              returned_prop_name->name = colon + 1;
              return;
            }
        }
    }
  else
    {
      const svn_ra_serf__ns_t *ns;

      for (ns = ns_list; ns; ns = ns->next)
        {
          if (! ns->xmlns[0])
            {
              returned_prop_name->xmlns = ns->url;
              returned_prop_name->name = name;
              return;
            }
        }
    }

  /* If the prefix is not found, then the name is NOT within a
     namespace.  */
  returned_prop_name->xmlns = "";
  returned_prop_name->name = name;
}


#define XML_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?>"

void
svn_ra_serf__add_xml_header_buckets(serf_bucket_t *agg_bucket,
                                    serf_bucket_alloc_t *bkt_alloc)
{
  serf_bucket_t *tmp;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(XML_HEADER, sizeof(XML_HEADER) - 1,
                                      bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);
}

void
svn_ra_serf__add_open_tag_buckets(serf_bucket_t *agg_bucket,
                                  serf_bucket_alloc_t *bkt_alloc,
                                  const char *tag, ...)
{
  va_list ap;
  const char *key;
  serf_bucket_t *tmp;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(tag, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  va_start(ap, tag);
  while ((key = va_arg(ap, char *)) != NULL)
    {
      const char *val = va_arg(ap, const char *);
      if (val)
        {
          tmp = SERF_BUCKET_SIMPLE_STRING_LEN(" ", 1, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING(key, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING_LEN("=\"", 2, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING(val, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\"", 1, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);
        }
    }
  va_end(ap);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(">", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);
}

void
svn_ra_serf__add_empty_tag_buckets(serf_bucket_t *agg_bucket,
                                   serf_bucket_alloc_t *bkt_alloc,
                                   const char *tag, ...)
{
  va_list ap;
  const char *key;
  serf_bucket_t *tmp;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(tag, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  va_start(ap, tag);
  while ((key = va_arg(ap, char *)) != NULL)
    {
      const char *val = va_arg(ap, const char *);
      if (val)
        {
          tmp = SERF_BUCKET_SIMPLE_STRING_LEN(" ", 1, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING(key, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING_LEN("=\"", 2, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING(val, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);

          tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\"", 1, bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp);
        }
    }
  va_end(ap);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("/>", 2, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);
}

void
svn_ra_serf__add_close_tag_buckets(serf_bucket_t *agg_bucket,
                                   serf_bucket_alloc_t *bkt_alloc,
                                   const char *tag)
{
  serf_bucket_t *tmp;

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</", 2, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING(tag, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(">", 1, bkt_alloc);
  serf_bucket_aggregate_append(agg_bucket, tmp);
}

void
svn_ra_serf__add_cdata_len_buckets(serf_bucket_t *agg_bucket,
                                   serf_bucket_alloc_t *bkt_alloc,
                                   const char *data, apr_size_t len)
{
  const char *end = data + len;
  const char *p = data, *q;
  serf_bucket_t *tmp_bkt;

  while (1)
    {
      /* Find a character which needs to be quoted and append bytes up
         to that point.  Strictly speaking, '>' only needs to be
         quoted if it follows "]]", but it's easier to quote it all
         the time.

         So, why are we escaping '\r' here?  Well, according to the
         XML spec, '\r\n' gets converted to '\n' during XML parsing.
         Also, any '\r' not followed by '\n' is converted to '\n'.  By
         golly, if we say we want to escape a '\r', we want to make
         sure it remains a '\r'!  */
      q = p;
      while (q < end && *q != '&' && *q != '<' && *q != '>' && *q != '\r')
        q++;


      tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN(p, q - p, bkt_alloc);
      serf_bucket_aggregate_append(agg_bucket, tmp_bkt);

      /* We may already be a winner.  */
      if (q == end)
        break;

      /* Append the entity reference for the character.  */
      if (*q == '&')
        {
          tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN("&amp;", sizeof("&amp;") - 1,
                                                  bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp_bkt);
        }
      else if (*q == '<')
        {
          tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN("&lt;", sizeof("&lt;") - 1,
                                                  bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp_bkt);
        }
      else if (*q == '>')
        {
          tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN("&gt;", sizeof("&gt;") - 1,
                                                  bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp_bkt);
        }
      else if (*q == '\r')
        {
          tmp_bkt = SERF_BUCKET_SIMPLE_STRING_LEN("&#13;", sizeof("&#13;") - 1,
                                                  bkt_alloc);
          serf_bucket_aggregate_append(agg_bucket, tmp_bkt);
        }

      p = q + 1;
    }
}

void svn_ra_serf__add_tag_buckets(serf_bucket_t *agg_bucket, const char *tag,
                                  const char *value,
                                  serf_bucket_alloc_t *bkt_alloc)
{
  svn_ra_serf__add_open_tag_buckets(agg_bucket, bkt_alloc, tag, SVN_VA_NULL);

  if (value)
    {
      svn_ra_serf__add_cdata_len_buckets(agg_bucket, bkt_alloc,
                                         value, strlen(value));
    }

  svn_ra_serf__add_close_tag_buckets(agg_bucket, bkt_alloc, tag);
}

/* Return a pool for XES to use for self-alloc (and other specifics).  */
static apr_pool_t *
xes_pool(const svn_ra_serf__xml_estate_t *xes)
{
  /* Move up through parent states looking for one with a pool. This
     will always terminate since the initial state has a pool.  */
  while (xes->state_pool == NULL)
    xes = xes->prev;
  return xes->state_pool;
}


static void
ensure_pool(svn_ra_serf__xml_estate_t *xes)
{
  if (xes->state_pool == NULL)
    xes->state_pool = svn_pool_create(xes_pool(xes));
}


/* This callback is used by define_namespaces() to wait until a pool is
   required before constructing it.  */
static apr_pool_t *
lazy_create_pool(void *baton)
{
  svn_ra_serf__xml_estate_t *xes = baton;

  ensure_pool(xes);
  return xes->state_pool;
}

svn_error_t *
svn_ra_serf__xml_context_done(svn_ra_serf__xml_context_t *xmlctx)
{
  if (xmlctx->current->prev)
    {
      /* Probably unreachable as this would be an xml parser error */
      return svn_error_createf(SVN_ERR_XML_MALFORMED, NULL,
                               _("XML stream truncated: closing '%s' missing"),
                               xmlctx->current->tag.name);
    }
  else if (! xmlctx->free_states)
    {
      /* If we have no items on the free_states list, we didn't push anything,
         which tells us that we found an empty xml body */
      const svn_ra_serf__xml_transition_t *scan;
      const svn_ra_serf__xml_transition_t *document = NULL;
      const char *msg;

      for (scan = xmlctx->ttable; scan->ns != NULL; ++scan)
        {
          if (scan->from_state == XML_STATE_INITIAL)
            {
              if (document != NULL)
                {
                  document = NULL; /* Multiple document elements defined */
                  break;
                }
              document = scan;
            }
        }

      if (document)
        msg = apr_psprintf(xmlctx->scratch_pool, "'%s' element not found",
                            document->name);
      else
        msg = _("document element not found");

      return svn_error_createf(SVN_ERR_XML_MALFORMED, NULL,
                               _("XML stream truncated: %s"),
                               msg);
    }

  svn_pool_destroy(xmlctx->scratch_pool);
  return SVN_NO_ERROR;
}

svn_ra_serf__xml_context_t *
svn_ra_serf__xml_context_create(
  const svn_ra_serf__xml_transition_t *ttable,
  svn_ra_serf__xml_opened_t opened_cb,
  svn_ra_serf__xml_closed_t closed_cb,
  svn_ra_serf__xml_cdata_t cdata_cb,
  void *baton,
  apr_pool_t *result_pool)
{
  svn_ra_serf__xml_context_t *xmlctx;
  svn_ra_serf__xml_estate_t *xes;

  xmlctx = apr_pcalloc(result_pool, sizeof(*xmlctx));
  xmlctx->ttable = ttable;
  xmlctx->opened_cb = opened_cb;
  xmlctx->closed_cb = closed_cb;
  xmlctx->cdata_cb = cdata_cb;
  xmlctx->baton = baton;
  xmlctx->scratch_pool = svn_pool_create(result_pool);

  xes = apr_pcalloc(result_pool, sizeof(*xes));
  /* XES->STATE == 0  */

  /* Child states may use this pool to allocate themselves. If a child
     needs to collect information, then it will construct a subpool and
     will use that to allocate itself and its collected data.  */
  xes->state_pool = result_pool;

  xmlctx->current = xes;

  return xmlctx;
}


apr_hash_t *
svn_ra_serf__xml_gather_since(svn_ra_serf__xml_estate_t *xes,
                              int stop_state)
{
  apr_hash_t *data;
  apr_pool_t *pool;

  ensure_pool(xes);
  pool = xes->state_pool;

  data = apr_hash_make(pool);

  for (; xes != NULL; xes = xes->prev)
    {
      if (xes->attrs != NULL)
        {
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(pool, xes->attrs); hi;
               hi = apr_hash_next(hi))
            {
              const void *key;
              apr_ssize_t klen;
              void *val;

              /* Parent name/value lifetimes are at least as long as POOL.  */
              apr_hash_this(hi, &key, &klen, &val);
              apr_hash_set(data, key, klen, val);
            }
        }

      if (xes->state == stop_state)
        break;
    }

  return data;
}


void
svn_ra_serf__xml_note(svn_ra_serf__xml_estate_t *xes,
                      int state,
                      const char *name,
                      const char *value)
{
  svn_ra_serf__xml_estate_t *scan;

  for (scan = xes; scan != NULL && scan->state != state; scan = scan->prev)
    /* pass */ ;

  SVN_ERR_ASSERT_NO_RETURN(scan != NULL);

  /* Make sure the target state has a pool.  */
  ensure_pool(scan);

  /* ... and attribute storage.  */
  if (scan->attrs == NULL)
    scan->attrs = apr_hash_make(scan->state_pool);

  /* In all likelihood, NAME is a string constant. But we can't really
     be sure. And it isn't like we're storing a billion of these into
     the state pool.  */
  svn_hash_sets(scan->attrs,
                apr_pstrdup(scan->state_pool, name),
                apr_pstrdup(scan->state_pool, value));
}


apr_pool_t *
svn_ra_serf__xml_state_pool(svn_ra_serf__xml_estate_t *xes)
{
  /* If they asked for a pool, then ensure that we have one to provide.  */
  ensure_pool(xes);

  return xes->state_pool;
}


static svn_error_t *
xml_cb_start(svn_ra_serf__xml_context_t *xmlctx,
             const char *raw_name,
             const char *const *attrs)
{
  svn_ra_serf__xml_estate_t *current = xmlctx->current;
  svn_ra_serf__dav_props_t elemname;
  const svn_ra_serf__xml_transition_t *scan;
  apr_pool_t *new_pool;
  svn_ra_serf__xml_estate_t *new_xes;

  /* If we're waiting for an element to close, then just ignore all
     other element-opens.  */
  if (xmlctx->waiting > 0)
    {
      xmlctx->waiting++;
      return SVN_NO_ERROR;
    }

  /* Look for xmlns: attributes. Lazily create the state pool if any
     were found.  */
  define_namespaces(&current->ns_list, attrs, lazy_create_pool, current);

  expand_ns(&elemname, current->ns_list, raw_name);

  for (scan = xmlctx->ttable; scan->ns != NULL; ++scan)
    {
      if (scan->from_state != current->state)
        continue;

      /* Wildcard tag match.  */
      if (*scan->name == '*')
        break;

      /* Found a specific transition.  */
      if (strcmp(elemname.name, scan->name) == 0
          && strcmp(elemname.xmlns, scan->ns) == 0)
        break;
    }
  if (scan->ns == NULL)
    {
      if (current->state == XML_STATE_INITIAL)
        {
          return svn_error_createf(
                        SVN_ERR_XML_UNEXPECTED_ELEMENT, NULL,
                        _("XML Parsing failed: Unexpected root element '%s'"),
                        elemname.name);
        }

      xmlctx->waiting++; /* Start waiting for the close tag */
      return SVN_NO_ERROR;
    }

  /* We should not be told to collect cdata if the closed_cb will not
     be called.  */
  SVN_ERR_ASSERT(!scan->collect_cdata || scan->custom_close);

  /* Found a transition. Make it happen.  */

  /* ### todo. push state  */

  /* ### how to use free states?  */
  /* This state should be allocated in the extent pool. If we will be
     collecting information for this state, then construct a subpool.

     ### potentially optimize away the subpool if none of the
     ### attributes are present. subpools are cheap, tho...  */
  new_pool = xes_pool(current);
  if (scan->collect_cdata || scan->collect_attrs[0])
    {
      new_pool = svn_pool_create(new_pool);

      /* Prep the new state.  */
      new_xes = apr_pcalloc(new_pool, sizeof(*new_xes));
      new_xes->state_pool = new_pool;

      /* If we're supposed to collect cdata, then set up a buffer for
         this. The existence of this buffer will instruct our cdata
         callback to collect the cdata.  */
      if (scan->collect_cdata)
        new_xes->cdata = svn_stringbuf_create_empty(new_pool);

      if (scan->collect_attrs[0] != NULL)
        {
          const char *const *saveattr = &scan->collect_attrs[0];

          new_xes->attrs = apr_hash_make(new_pool);
          for (; *saveattr != NULL; ++saveattr)
            {
              const char *name;
              const char *value;

              if (**saveattr == '?')
                {
                  name = *saveattr + 1;
                  value = svn_xml_get_attr_value(name, attrs);
                }
              else
                {
                  name = *saveattr;
                  value = svn_xml_get_attr_value(name, attrs);
                  if (value == NULL)
                    return svn_error_createf(
                                SVN_ERR_XML_ATTRIB_NOT_FOUND,
                                NULL,
                                _("Missing XML attribute '%s' on '%s' element"),
                                name, scan->name);
                }

              if (value)
                svn_hash_sets(new_xes->attrs, name,
                              apr_pstrdup(new_pool, value));
            }
        }
    }
  else
    {
      /* Prep the new state.  */
      new_xes = apr_pcalloc(new_pool, sizeof(*new_xes));
      /* STATE_POOL remains NULL.  */
    }

  /* Some basic copies to set up the new estate.  */
  new_xes->state = scan->to_state;
  new_xes->tag.name = apr_pstrdup(new_pool, elemname.name);
  new_xes->tag.xmlns = apr_pstrdup(new_pool, elemname.xmlns);
  new_xes->custom_close = scan->custom_close;

  /* Start with the parent's namespace set.  */
  new_xes->ns_list = current->ns_list;

  /* The new state is prepared. Make it current.  */
  new_xes->prev = current;
  xmlctx->current = new_xes;

  if (xmlctx->opened_cb)
    {
      START_CALLBACK(xmlctx);
      SVN_ERR(xmlctx->opened_cb(new_xes, xmlctx->baton,
                                new_xes->state, &new_xes->tag,
                                xmlctx->scratch_pool));
      END_CALLBACK(xmlctx);
      svn_pool_clear(xmlctx->scratch_pool);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
xml_cb_end(svn_ra_serf__xml_context_t *xmlctx,
           const char *raw_name)
{
  svn_ra_serf__xml_estate_t *xes = xmlctx->current;

  if (xmlctx->waiting > 0)
    {
      xmlctx->waiting--;
      return SVN_NO_ERROR;
    }

  if (xes->custom_close)
    {
      const svn_string_t *cdata;

      if (xes->cdata)
        {
          cdata = svn_stringbuf__morph_into_string(xes->cdata);
#ifdef SVN_DEBUG
          /* We might toss the pool holding this structure, but it could also
             be within a parent pool. In any case, for safety's sake, disable
             the stringbuf against future Badness.  */
          xes->cdata->pool = NULL;
#endif
        }
      else
        cdata = NULL;

      START_CALLBACK(xmlctx);
      SVN_ERR(xmlctx->closed_cb(xes, xmlctx->baton, xes->state,
                                cdata, xes->attrs,
                                xmlctx->scratch_pool));
      END_CALLBACK(xmlctx);
      svn_pool_clear(xmlctx->scratch_pool);
    }

  /* Pop the state.  */
  xmlctx->current = xes->prev;

  /* ### not everything should go on the free state list. XES may go
     ### away with the state pool.  */
  xes->prev = xmlctx->free_states;
  xmlctx->free_states = xes;

  /* If there is a STATE_POOL, then toss it. This will get rid of as much
     memory as possible. Potentially the XES (if we didn't create a pool
     right away, then XES may be in a parent pool).  */
  if (xes->state_pool)
    svn_pool_destroy(xes->state_pool);

  return SVN_NO_ERROR;
}


static svn_error_t *
xml_cb_cdata(svn_ra_serf__xml_context_t *xmlctx,
             const char *data,
             apr_size_t len)
{
  /* If we are waiting for a closing tag, then we are uninterested in
     the cdata. Just return.  */
  if (xmlctx->waiting > 0)
    return SVN_NO_ERROR;

  /* If the current state is collecting cdata, then copy the cdata.  */
  if (xmlctx->current->cdata != NULL)
    {
      svn_stringbuf_appendbytes(xmlctx->current->cdata, data, len);
    }
  /* ... else if a CDATA_CB has been supplied, then invoke it for
     all states.  */
  else if (xmlctx->cdata_cb != NULL)
    {
      START_CALLBACK(xmlctx);
      SVN_ERR(xmlctx->cdata_cb(xmlctx->current,
                               xmlctx->baton,
                               xmlctx->current->state,
                               data, len,
                               xmlctx->scratch_pool));
      END_CALLBACK(xmlctx);
      svn_pool_clear(xmlctx->scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Wrapper around svn_xml_parse */
static APR_INLINE svn_error_t *
parse_xml(struct expat_ctx_t *ectx, const char *data, apr_size_t len, svn_boolean_t is_final)
{
  svn_error_t *err = svn_xml_parse(ectx->parser, data, len, is_final);

  if (err && err->apr_err == SVN_ERR_XML_MALFORMED)
    err = svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, err,
                           _("The XML response contains invalid XML"));

  return err;
}

/* Implements svn_xml_start_elem callback */
static void
expat_start(void *baton, const char *raw_name, const char **attrs)
{
  struct expat_ctx_t *ectx = baton;
  svn_error_t *err;

  err = svn_error_trace(xml_cb_start(ectx->xmlctx, raw_name, attrs));

  if (err)
    svn_xml_signal_bailout(err, ectx->parser);
}


/* Implements svn_xml_end_elem callback */
static void
expat_end(void *baton, const char *raw_name)
{
  struct expat_ctx_t *ectx = baton;
  svn_error_t *err;

  err = svn_error_trace(xml_cb_end(ectx->xmlctx, raw_name));

  if (err)
    svn_xml_signal_bailout(err, ectx->parser);
}


/* Implements svn_xml_char_data callback */
static void
expat_cdata(void *baton, const char *data, apr_size_t len)
{
  struct expat_ctx_t *ectx = baton;
  svn_error_t *err;

  err = svn_error_trace(xml_cb_cdata(ectx->xmlctx, data, len));

  if (err)
    svn_xml_signal_bailout(err, ectx->parser);
}


/* Implements svn_ra_serf__response_handler_t */
static svn_error_t *
expat_response_handler(serf_request_t *request,
                       serf_bucket_t *response,
                       void *baton,
                       apr_pool_t *scratch_pool)
{
  struct expat_ctx_t *ectx = baton;
  svn_boolean_t got_expected_status;

  if (ectx->expected_status)
    {
      const int *status = ectx->expected_status;
      got_expected_status = FALSE;

      while (*status && ectx->handler->sline.code != *status)
        status++;

      got_expected_status = (*status) != 0;
    }
  else
    got_expected_status = (ectx->handler->sline.code == 200);

  if (!ectx->handler->server_error
      && ((ectx->handler->sline.code < 200) || (ectx->handler->sline.code >= 300)
          || ! got_expected_status))
    {
      /* By deferring to expect_empty_body(), it will make a choice on
         how to handle the body. Whatever the decision, the core handler
         will take over, and we will not be called again.  */

      /* ### This handles xml bodies as svn-errors (returned via serf context
         ### loop), but ignores non-xml errors.

         Current code depends on this behavior and checks itself while other
         continues, and then verifies if work has been performed.

         ### TODO: Make error checking consistent */

      /* ### If !GOT_EXPECTED_STATUS, this should always produce an error */
      return svn_error_trace(svn_ra_serf__expect_empty_body(
                               request, response, ectx->handler,
                               scratch_pool));
    }

  if (!ectx->parser)
    {
      ectx->parser = svn_xml_make_parser(ectx, expat_start, expat_end,
                                         expat_cdata, ectx->cleanup_pool);
    }

  while (1)
    {
      apr_status_t status;
      const char *data;
      apr_size_t len;
      svn_boolean_t at_eof = FALSE;

      status = serf_bucket_read(response, PARSE_CHUNK_SIZE, &data, &len);
      if (SERF_BUCKET_READ_ERROR(status))
        return svn_ra_serf__wrap_err(status, NULL);
      else if (APR_STATUS_IS_EOF(status))
        at_eof = TRUE;

      SVN_ERR(parse_xml(ectx, data, len, at_eof /* isFinal */));

      /* The parsing went fine. What has the bucket told us?  */
      if (at_eof)
        {
          /* Make sure we actually got xml and clean up after parsing */
          SVN_ERR(svn_ra_serf__xml_context_done(ectx->xmlctx));
        }

      if (status && !SERF_BUCKET_READ_ERROR(status))
        {
          return svn_ra_serf__wrap_err(status, NULL);
        }
    }

  /* NOTREACHED */
}


svn_ra_serf__handler_t *
svn_ra_serf__create_expat_handler(svn_ra_serf__session_t *session,
                                  svn_ra_serf__xml_context_t *xmlctx,
                                  const int *expected_status,
                                  apr_pool_t *result_pool)
{
  svn_ra_serf__handler_t *handler;
  struct expat_ctx_t *ectx;

  ectx = apr_pcalloc(result_pool, sizeof(*ectx));
  ectx->xmlctx = xmlctx;
  ectx->parser = NULL;
  ectx->expected_status = expected_status;
  ectx->cleanup_pool = result_pool;

  handler = svn_ra_serf__create_handler(session, result_pool);
  handler->response_handler = expat_response_handler;
  handler->response_baton = ectx;

  ectx->handler = handler;

  return handler;
}
