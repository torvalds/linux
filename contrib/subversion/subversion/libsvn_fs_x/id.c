/* id.c : implements FSX-internal ID functions
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

#include "id.h"
#include "index.h"
#include "util.h"

#include "private/svn_string_private.h"



svn_boolean_t
svn_fs_x__is_txn(svn_fs_x__change_set_t change_set)
{
  return change_set < SVN_FS_X__INVALID_CHANGE_SET;
}

svn_boolean_t
svn_fs_x__is_revision(svn_fs_x__change_set_t change_set)
{
  return change_set > SVN_FS_X__INVALID_CHANGE_SET;
}

svn_revnum_t
svn_fs_x__get_revnum(svn_fs_x__change_set_t change_set)
{
  return svn_fs_x__is_revision(change_set)
       ? (svn_revnum_t)change_set
       : SVN_INVALID_REVNUM;
}

apr_int64_t
svn_fs_x__get_txn_id(svn_fs_x__change_set_t change_set)
{
  return svn_fs_x__is_txn(change_set)
       ? -change_set + SVN_FS_X__INVALID_CHANGE_SET -1
       : SVN_FS_X__INVALID_TXN_ID;
}


svn_fs_x__change_set_t
svn_fs_x__change_set_by_rev(svn_revnum_t revnum)
{
  assert(revnum >= SVN_FS_X__INVALID_CHANGE_SET);
  return revnum;
}

svn_fs_x__change_set_t
svn_fs_x__change_set_by_txn(apr_int64_t txn_id)
{
  assert(txn_id >= SVN_FS_X__INVALID_CHANGE_SET);
  return -txn_id + SVN_FS_X__INVALID_CHANGE_SET -1;
}


/* Parse the NUL-terminated ID part at DATA and write the result into *PART.
 * Return TRUE if no errors were detected. */
static svn_boolean_t
part_parse(svn_fs_x__id_t *part,
           const char *data)
{
  part->number = svn__base36toui64(&data, data);
  switch (data[0])
    {
      /* txn number? */
      case '-': part->change_set = -svn__base36toui64(&data, data + 1);
                return TRUE;

      /* revision number? */
      case '+': part->change_set = svn__base36toui64(&data, data + 1);
                return TRUE;

      /* everything else is forbidden */
      default:  return FALSE;
    }
}

/* Write the textual representation of *PART into P and return a pointer
 * to the first position behind that string.
 */
static char *
part_unparse(char *p,
             const svn_fs_x__id_t *part)
{
  p += svn__ui64tobase36(p, part->number);
  if (part->change_set >= 0)
    {
      *(p++) = '+';
      p += svn__ui64tobase36(p, part->change_set);
    }
  else
    {
      *(p++) = '-';
      p += svn__ui64tobase36(p, -part->change_set);
    }

  return p;
}



/* Operations on ID parts */

svn_boolean_t
svn_fs_x__id_is_root(const svn_fs_x__id_t* part)
{
  return part->change_set == 0 && part->number == 0;
}

svn_boolean_t
svn_fs_x__id_eq(const svn_fs_x__id_t *lhs,
                const svn_fs_x__id_t *rhs)
{
  return lhs->change_set == rhs->change_set && lhs->number == rhs->number;
}

svn_error_t *
svn_fs_x__id_parse(svn_fs_x__id_t *part,
                   const char *data)
{
  if (!part_parse(part, data))
    return svn_error_createf(SVN_ERR_FS_MALFORMED_NODEREV_ID, NULL,
                             "Malformed ID string");

  return SVN_NO_ERROR;
}

svn_string_t *
svn_fs_x__id_unparse(const svn_fs_x__id_t *id,
                     apr_pool_t *result_pool)
{
  char string[2 * SVN_INT64_BUFFER_SIZE + 1];
  char *p = part_unparse(string, id);

  return svn_string_ncreate(string, p - string, result_pool);
}

void
svn_fs_x__id_reset(svn_fs_x__id_t *part)
{
  part->change_set = SVN_FS_X__INVALID_CHANGE_SET;
  part->number = 0;
}

svn_boolean_t
svn_fs_x__id_used(const svn_fs_x__id_t *part)
{
  return part->change_set != SVN_FS_X__INVALID_CHANGE_SET;
}

void
svn_fs_x__init_txn_root(svn_fs_x__id_t *noderev_id,
                        svn_fs_x__txn_id_t txn_id)
{
  noderev_id->change_set = svn_fs_x__change_set_by_txn(txn_id);
  noderev_id->number = SVN_FS_X__ITEM_INDEX_ROOT_NODE;
}

void
svn_fs_x__init_rev_root(svn_fs_x__id_t *noderev_id,
                        svn_revnum_t rev)
{
  noderev_id->change_set = svn_fs_x__change_set_by_rev(rev);
  noderev_id->number = SVN_FS_X__ITEM_INDEX_ROOT_NODE;
}

int
svn_fs_x__id_compare(const svn_fs_x__id_t *a,
                     const svn_fs_x__id_t *b)
{
  if (a->change_set < b->change_set)
    return -1;
  if (a->change_set > b->change_set)
    return 1;

  return a->number < b->number ? -1 : a->number == b->number ? 0 : 1;
}
