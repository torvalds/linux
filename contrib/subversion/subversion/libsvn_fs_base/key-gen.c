/* key-gen.c --- manufacturing sequential keys for some db tables
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
#include <string.h>

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <stdlib.h>
#include <apr.h>

#include "key-gen.h"



/*** Keys for reps and strings. ***/


void
svn_fs_base__next_key(const char *this, apr_size_t *len, char *next)
{
  apr_size_t olen = *len;     /* remember the first length */
  apr_size_t i;               /* current index */
  char c;                     /* current char */
  svn_boolean_t carry = TRUE; /* boolean: do we have a carry or not?
                                 We start with a carry, because we're
                                 incrementing the number, after all. */

  /* Empty strings and leading zeros (except for the string "0") are not
   * allowed.  Run our malfunction handler to prevent possible db corruption
   * from being propagated further. */
  SVN_ERR_ASSERT_NO_RETURN(olen != 0 && (olen == 1 || this[0] != '0'));

  i = olen - 1; /* initial index: we work backwords */
  while (1729)
    {
      c = this[i];

      /* Validate as we go. */
      if (! (((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z'))))
        {
          *len = 0;
          return;
        }

      if (carry)
        {
          if (c == 'z')
            next[i] = '0';
          else
            {
              carry = FALSE;

              if (c == '9')
                next[i] = 'a';
              else
                next[i] = c + 1;
            }
        }
      else
        next[i] = c;

      if (i == 0)
        break;

      i--;
    }

  /* The new length is OLEN, plus 1 if there's a carry out of the
     leftmost digit. */
  *len = olen + (carry ? 1 : 0);

  /* Ensure that we haven't overrun the (ludicrous) bound on key length.
     Note that MAX_KEY_SIZE is a bound on the size *including*
     the trailing null byte. */
  assert(*len < MAX_KEY_SIZE);

  /* Now we know it's safe to add the null terminator. */
  next[*len] = '\0';

  /* Handle any leftover carry. */
  if (carry)
    {
      memmove(next+1, next, olen);
      next[0] = '1';
    }
}


svn_boolean_t
svn_fs_base__same_keys(const char *a, const char *b)
{
  if (! (a || b))
    return TRUE;
  if (a && (! b))
    return FALSE;
  if ((! a) && b)
    return FALSE;
  return (strcmp(a, b) == 0);
}
