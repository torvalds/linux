/* id.c : operations on node-revision IDs
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
#include <stdlib.h>

#include "id.h"
#include "index.h"

#include "../libsvn_fs/fs-loader.h"
#include "private/svn_temp_serializer.h"
#include "private/svn_string_private.h"


typedef struct fs_fs__id_t
{
  /* API visible part */
  svn_fs_id_t generic_id;

  /* private members */
  struct
    {
      svn_fs_fs__id_part_t node_id;
      svn_fs_fs__id_part_t copy_id;
      svn_fs_fs__id_part_t txn_id;
      svn_fs_fs__id_part_t rev_item;
    } private_id;
} fs_fs__id_t;



/** Like strtol but with a fixed base of 10, locale independent and limited
 * to non-negative values.  Overflows are indicated by a FALSE return value
 * in which case *RESULT_P will not be modified.
 *
 * This allows the compiler to generate massively faster code.
 * (E.g. Avoiding locale specific processing).  ID parsing is one of the
 * most CPU consuming parts of FSFS data access.  Better be quick.
 */
static svn_boolean_t
locale_independent_strtol(long *result_p,
                          const char* buffer,
                          const char** end)
{
  /* We allow positive values only.  We use unsigned arithmetics to get
   * well-defined overflow behavior.  It also happens to allow for a wider
   * range of compiler-side optimizations. */
  unsigned long result = 0;
  while (1)
    {
      unsigned long c = (unsigned char)*buffer - (unsigned char)'0';
      unsigned long next;

      /* This implies the NUL check. */
      if (c > 9)
        break;

      /* Overflow check.  Passing this, NEXT can be no more than ULONG_MAX+9
       * before being truncated to ULONG but it still covers 0 .. ULONG_MAX.
       */
      if (result > ULONG_MAX / 10)
        return FALSE;

      next = result * 10 + c;

      /* Overflow check.  In case of an overflow, NEXT is 0..9 and RESULT
       * is much larger than 10.  We will then return FALSE.
       *
       * In the non-overflow case, NEXT is >= 10 * RESULT but never smaller.
       * We will continue the loop in that case. */
      if (next < result)
        return FALSE;

      result = next;
      ++buffer;
    }

  *end = buffer;
  if (result > LONG_MAX)
    return FALSE;

  *result_p = (long)result;

  return TRUE;
}

/* Parse the NUL-terminated ID part at DATA and write the result into *PART.
 * Return TRUE if no errors were detected. */
static svn_boolean_t
part_parse(svn_fs_fs__id_part_t *part,
           const char *data)
{
  const char *end;

  /* special case: ID inside some transaction */
  if (data[0] == '_')
    {
      part->revision = SVN_INVALID_REVNUM;
      part->number = svn__base36toui64(&data, data + 1);
      return *data == '\0';
    }

  /* special case: 0 / default ID */
  if (data[0] == '0' && data[1] == '\0')
    {
      part->revision = 0;
      part->number = 0;
      return TRUE;
    }

  /* read old style / new style ID */
  part->number = svn__base36toui64(&data, data);
  if (data[0] != '-')
    {
      part->revision = 0;
      return *data == '\0';
    }

  return locale_independent_strtol(&part->revision, data+1, &end);
}

/* Parse the transaction id in DATA and store the result in *TXN_ID.
 * Return FALSE if there was some problem.
 */
static svn_boolean_t
txn_id_parse(svn_fs_fs__id_part_t *txn_id,
             const char *data)
{
  const char *end;
  if (!locale_independent_strtol(&txn_id->revision, data, &end))
    return FALSE;

  data = end;
  if (*data != '-')
    return FALSE;

  ++data;
  txn_id->number = svn__base36toui64(&data, data);
  return *data == '\0';
}

/* Write the textual representation of *PART into P and return a pointer
 * to the first position behind that string.
 */
static char *
unparse_id_part(char *p,
                const svn_fs_fs__id_part_t *part)
{
  if (SVN_IS_VALID_REVNUM(part->revision))
    {
      /* ordinary old style / new style ID */
      p += svn__ui64tobase36(p, part->number);
      if (part->revision > 0)
        {
          *(p++) = '-';
          p += svn__i64toa(p, part->revision);
        }
    }
  else
    {
      /* in txn: mark with "_" prefix */
      *(p++) = '_';
      p += svn__ui64tobase36(p, part->number);
    }

  *(p++) = '.';

  return p;
}



/* Operations on ID parts */

svn_boolean_t
svn_fs_fs__id_part_is_root(const svn_fs_fs__id_part_t* part)
{
  return part->revision == 0 && part->number == 0;
}

svn_boolean_t
svn_fs_fs__id_part_eq(const svn_fs_fs__id_part_t *lhs,
                      const svn_fs_fs__id_part_t *rhs)
{
  return lhs->revision == rhs->revision && lhs->number == rhs->number;
}

svn_boolean_t
svn_fs_fs__id_txn_used(const svn_fs_fs__id_part_t *txn_id)
{
  return SVN_IS_VALID_REVNUM(txn_id->revision) || (txn_id->number != 0);
}

void
svn_fs_fs__id_txn_reset(svn_fs_fs__id_part_t *txn_id)
{
  txn_id->revision = SVN_INVALID_REVNUM;
  txn_id->number = 0;
}

svn_error_t *
svn_fs_fs__id_txn_parse(svn_fs_fs__id_part_t *txn_id,
                        const char *data)
{
  if (! txn_id_parse(txn_id, data))
    return svn_error_createf(SVN_ERR_FS_MALFORMED_TXN_ID, NULL,
                             "malformed txn id '%s'", data);

  return SVN_NO_ERROR;
}

const char *
svn_fs_fs__id_txn_unparse(const svn_fs_fs__id_part_t *txn_id,
                          apr_pool_t *pool)
{
  char string[2 * SVN_INT64_BUFFER_SIZE + 1];
  char *p = string;

  p += svn__i64toa(p, txn_id->revision);
  *(p++) = '-';
  p += svn__ui64tobase36(p, txn_id->number);

  return apr_pstrmemdup(pool, string, p - string);
}



/* Accessing ID Pieces.  */

const svn_fs_fs__id_part_t *
svn_fs_fs__id_node_id(const svn_fs_id_t *fs_id)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)fs_id;

  return &id->private_id.node_id;
}


const svn_fs_fs__id_part_t *
svn_fs_fs__id_copy_id(const svn_fs_id_t *fs_id)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)fs_id;

  return &id->private_id.copy_id;
}


const svn_fs_fs__id_part_t *
svn_fs_fs__id_txn_id(const svn_fs_id_t *fs_id)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)fs_id;

  return &id->private_id.txn_id;
}


const svn_fs_fs__id_part_t *
svn_fs_fs__id_rev_item(const svn_fs_id_t *fs_id)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)fs_id;

  return &id->private_id.rev_item;
}

svn_revnum_t
svn_fs_fs__id_rev(const svn_fs_id_t *fs_id)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)fs_id;

  return id->private_id.rev_item.revision;
}

apr_uint64_t
svn_fs_fs__id_item(const svn_fs_id_t *fs_id)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)fs_id;

  return id->private_id.rev_item.number;
}

svn_boolean_t
svn_fs_fs__id_is_txn(const svn_fs_id_t *fs_id)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)fs_id;

  return svn_fs_fs__id_txn_used(&id->private_id.txn_id);
}

svn_string_t *
svn_fs_fs__id_unparse(const svn_fs_id_t *fs_id,
                      apr_pool_t *pool)
{
  char string[6 * SVN_INT64_BUFFER_SIZE + 10];
  const fs_fs__id_t *id = (const fs_fs__id_t *)fs_id;

  char *p = unparse_id_part(string, &id->private_id.node_id);
  p = unparse_id_part(p, &id->private_id.copy_id);

  if (svn_fs_fs__id_txn_used(&id->private_id.txn_id))
    {
      *(p++) = 't';
      p += svn__i64toa(p, id->private_id.txn_id.revision);
      *(p++) = '-';
      p += svn__ui64tobase36(p, id->private_id.txn_id.number);
    }
  else
    {
      *(p++) = 'r';
      p += svn__i64toa(p, id->private_id.rev_item.revision);
      *(p++) = '/';
      p += svn__i64toa(p, id->private_id.rev_item.number);
    }

  return svn_string_ncreate(string, p - string, pool);
}


/*** Comparing node IDs ***/

svn_boolean_t
svn_fs_fs__id_eq(const svn_fs_id_t *a,
                 const svn_fs_id_t *b)
{
  const fs_fs__id_t *id_a = (const fs_fs__id_t *)a;
  const fs_fs__id_t *id_b = (const fs_fs__id_t *)b;

  if (a == b)
    return TRUE;

  return svn_fs_fs__id_part_eq(&id_a->private_id.node_id,
                               &id_b->private_id.node_id)
      && svn_fs_fs__id_part_eq(&id_a->private_id.copy_id,
                               &id_b->private_id.copy_id)
      && svn_fs_fs__id_part_eq(&id_a->private_id.txn_id,
                               &id_b->private_id.txn_id)
      && svn_fs_fs__id_part_eq(&id_a->private_id.rev_item,
                               &id_b->private_id.rev_item);
}


svn_boolean_t
svn_fs_fs__id_check_related(const svn_fs_id_t *a,
                            const svn_fs_id_t *b)
{
  const fs_fs__id_t *id_a = (const fs_fs__id_t *)a;
  const fs_fs__id_t *id_b = (const fs_fs__id_t *)b;

  if (a == b)
    return TRUE;

  /* If both node_ids have been created within _different_ transactions
     (and are still uncommitted), then it is impossible for them to be
     related.

     Due to our txn-local temporary IDs, however, they might have been
     given the same temporary node ID.  We need to detect that case.
   */
  if (   id_a->private_id.node_id.revision == SVN_INVALID_REVNUM
      && id_b->private_id.node_id.revision == SVN_INVALID_REVNUM)
    {
      if (!svn_fs_fs__id_part_eq(&id_a->private_id.txn_id,
                                 &id_b->private_id.txn_id))
        return FALSE;

      /* At this point, matching node_ids implies relatedness. */
    }

  return svn_fs_fs__id_part_eq(&id_a->private_id.node_id,
                               &id_b->private_id.node_id);
}


svn_fs_node_relation_t
svn_fs_fs__id_compare(const svn_fs_id_t *a,
                      const svn_fs_id_t *b)
{
  if (svn_fs_fs__id_eq(a, b))
    return svn_fs_node_unchanged;
  return (svn_fs_fs__id_check_related(a, b) ? svn_fs_node_common_ancestor
                                            : svn_fs_node_unrelated);
}

int
svn_fs_fs__id_part_compare(const svn_fs_fs__id_part_t *a,
                           const svn_fs_fs__id_part_t *b)
{
  if (a->revision < b->revision)
    return -1;
  if (a->revision > b->revision)
    return 1;

  return a->number < b->number ? -1 : a->number == b->number ? 0 : 1;
}



/* Creating ID's.  */

static id_vtable_t id_vtable = {
  svn_fs_fs__id_unparse,
  svn_fs_fs__id_compare
};

svn_fs_id_t *
svn_fs_fs__id_txn_create_root(const svn_fs_fs__id_part_t *txn_id,
                              apr_pool_t *pool)
{
  fs_fs__id_t *id = apr_pcalloc(pool, sizeof(*id));

  /* node ID and copy ID are "0" */

  id->private_id.txn_id = *txn_id;
  id->private_id.rev_item.revision = SVN_INVALID_REVNUM;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;

  return (svn_fs_id_t *)id;
}

svn_fs_id_t *svn_fs_fs__id_create_root(const svn_revnum_t revision,
                                       apr_pool_t *pool)
{
  fs_fs__id_t *id = apr_pcalloc(pool, sizeof(*id));

  id->private_id.txn_id.revision = SVN_INVALID_REVNUM;
  id->private_id.rev_item.revision = revision;
  id->private_id.rev_item.number = SVN_FS_FS__ITEM_INDEX_ROOT_NODE;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;

  return (svn_fs_id_t *)id;
}

svn_fs_id_t *
svn_fs_fs__id_txn_create(const svn_fs_fs__id_part_t *node_id,
                         const svn_fs_fs__id_part_t *copy_id,
                         const svn_fs_fs__id_part_t *txn_id,
                         apr_pool_t *pool)
{
  fs_fs__id_t *id = apr_pcalloc(pool, sizeof(*id));

  id->private_id.node_id = *node_id;
  id->private_id.copy_id = *copy_id;
  id->private_id.txn_id = *txn_id;
  id->private_id.rev_item.revision = SVN_INVALID_REVNUM;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;

  return (svn_fs_id_t *)id;
}


svn_fs_id_t *
svn_fs_fs__id_rev_create(const svn_fs_fs__id_part_t *node_id,
                         const svn_fs_fs__id_part_t *copy_id,
                         const svn_fs_fs__id_part_t *rev_item,
                         apr_pool_t *pool)
{
  fs_fs__id_t *id = apr_pcalloc(pool, sizeof(*id));

  id->private_id.node_id = *node_id;
  id->private_id.copy_id = *copy_id;
  id->private_id.txn_id.revision = SVN_INVALID_REVNUM;
  id->private_id.rev_item = *rev_item;

  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;

  return (svn_fs_id_t *)id;
}


svn_fs_id_t *
svn_fs_fs__id_copy(const svn_fs_id_t *source, apr_pool_t *pool)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)source;
  fs_fs__id_t *new_id = apr_pmemdup(pool, id, sizeof(*new_id));

  new_id->generic_id.fsap_data = new_id;

  return (svn_fs_id_t *)new_id;
}

/* Return an ID resulting from parsing the string DATA, or NULL if DATA is
   an invalid ID string. *DATA will be modified / invalidated by this call. */
static svn_fs_id_t *
id_parse(char *data,
         apr_pool_t *pool)
{
  fs_fs__id_t *id;
  char *str;

  /* Alloc a new svn_fs_id_t structure. */
  id = apr_pcalloc(pool, sizeof(*id));
  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;

  /* Now, we basically just need to "split" this data on `.'
     characters.  We will use svn_cstring_tokenize, which will put
     terminators where each of the '.'s used to be.  Then our new
     id field will reference string locations inside our duplicate
     string.*/

  /* Node Id */
  str = svn_cstring_tokenize(".", &data);
  if (str == NULL)
    return NULL;
  if (! part_parse(&id->private_id.node_id, str))
    return NULL;

  /* Copy Id */
  str = svn_cstring_tokenize(".", &data);
  if (str == NULL)
    return NULL;
  if (! part_parse(&id->private_id.copy_id, str))
    return NULL;

  /* Txn/Rev Id */
  str = svn_cstring_tokenize(".", &data);
  if (str == NULL)
    return NULL;

  if (str[0] == 'r')
    {
      apr_int64_t val;
      const char *tmp;
      svn_error_t *err;

      /* This is a revision type ID */
      id->private_id.txn_id.revision = SVN_INVALID_REVNUM;
      id->private_id.txn_id.number = 0;

      data = str + 1;
      str = svn_cstring_tokenize("/", &data);
      if (str == NULL)
        return NULL;
      if (!locale_independent_strtol(&id->private_id.rev_item.revision,
                                     str, &tmp))
        return NULL;

      err = svn_cstring_atoi64(&val, data);
      if (err)
        {
          svn_error_clear(err);
          return NULL;
        }
      id->private_id.rev_item.number = (apr_uint64_t)val;
    }
  else if (str[0] == 't')
    {
      /* This is a transaction type ID */
      id->private_id.rev_item.revision = SVN_INVALID_REVNUM;
      id->private_id.rev_item.number = 0;

      if (! txn_id_parse(&id->private_id.txn_id, str + 1))
        return NULL;
    }
  else
    return NULL;

  return (svn_fs_id_t *)id;
}

svn_error_t *
svn_fs_fs__id_parse(const svn_fs_id_t **id_p,
                    char *data,
                    apr_pool_t *pool)
{
  svn_fs_id_t *id = id_parse(data, pool);
  if (id == NULL)
    return svn_error_createf(SVN_ERR_FS_MALFORMED_NODEREV_ID, NULL,
                             "Malformed node revision ID string");

  *id_p = id;

  return SVN_NO_ERROR;
}

/* (de-)serialization support */

/* Serialize an ID within the serialization CONTEXT.
 */
void
svn_fs_fs__id_serialize(svn_temp_serializer__context_t *context,
                        const svn_fs_id_t * const *in)
{
  const fs_fs__id_t *id = (const fs_fs__id_t *)*in;

  /* nothing to do for NULL ids */
  if (id == NULL)
    return;

  /* Serialize the id data struct itself.
   * Note that the structure behind IN is actually larger than a mere
   * svn_fs_id_t . */
  svn_temp_serializer__add_leaf(context,
                                (const void * const *)in,
                                sizeof(fs_fs__id_t));
}

/* Deserialize an ID inside the BUFFER.
 */
void
svn_fs_fs__id_deserialize(void *buffer, svn_fs_id_t **in_out)
{
  fs_fs__id_t *id;

  /* The id maybe all what is in the whole buffer.
   * Don't try to fixup the pointer in that case*/
  if (*in_out != buffer)
    svn_temp_deserializer__resolve(buffer, (void**)in_out);

  id = (fs_fs__id_t *)*in_out;

  /* no id, no sub-structure fixup necessary */
  if (id == NULL)
    return;

  /* the stored vtable is bogus at best -> set the right one */
  id->generic_id.vtable = &id_vtable;
  id->generic_id.fsap_data = id;
}

