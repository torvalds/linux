/*
 * eol.c :  generic eol/keyword routines
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



#define APR_WANT_STRFUNC

#include <apr_file_io.h>
#include "svn_io.h"
#include "private/svn_eol_private.h"
#include "private/svn_dep_compat.h"

char *
svn_eol__find_eol_start(char *buf, apr_size_t len)
{
#if SVN_UNALIGNED_ACCESS_IS_OK

  /* Scan the input one machine word at a time. */
  for (; len > sizeof(apr_uintptr_t)
       ; buf += sizeof(apr_uintptr_t), len -= sizeof(apr_uintptr_t))
    {
      /* This is a variant of the well-known strlen test: */
      apr_uintptr_t chunk = *(const apr_uintptr_t *)buf;

      /* A byte in SVN__R_TEST is \0, iff it was \r in *BUF.
       * Similarly, SVN__N_TEST is an indicator for \n. */
      apr_uintptr_t r_test = chunk ^ SVN__R_MASK;
      apr_uintptr_t n_test = chunk ^ SVN__N_MASK;

      /* A byte in SVN__R_TEST can only be < 0x80, iff it has been \0 before
       * (i.e. \r in *BUF). Ditto for SVN__N_TEST. */
      r_test |= (r_test & SVN__LOWER_7BITS_SET) + SVN__LOWER_7BITS_SET;
      n_test |= (n_test & SVN__LOWER_7BITS_SET) + SVN__LOWER_7BITS_SET;

      /* Check whether at least one of the words contains a byte <0x80
       * (if one is detected, there was a \r or \n in CHUNK). */
      if ((r_test & n_test & SVN__BIT_7_SET) != SVN__BIT_7_SET)
        break;
    }

#endif

  /* The remaining odd bytes will be examined the naive way: */
  for (; len > 0; ++buf, --len)
    {
      if (*buf == '\n' || *buf == '\r')
        return buf;
    }

  return NULL;
}

const char *
svn_eol__detect_eol(char *buf, apr_size_t len, char **eolp)
{
  char *eol;

  eol = svn_eol__find_eol_start(buf, len);
  if (eol)
    {
      if (eolp)
        *eolp = eol;

      if (*eol == '\n')
        return "\n";

      /* We found a CR. */
      ++eol;
      if (eol == buf + len || *eol != '\n')
        return "\r";
      return "\r\n";
    }

  return NULL;
}
