/* skel.c --- parsing and unparsing skeletons
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
#include "svn_string.h"
#include "svn_error.h"
#include "svn_props.h"
#include "svn_pools.h"
#include "private/svn_skel.h"
#include "private/svn_string_private.h"


/* Parsing skeletons.  */

enum char_type {
  type_nothing = 0,
  type_space = 1,
  type_digit = 2,
  type_paren = 3,
  type_name = 4
};


/* We can't use the <ctype.h> macros here, because they are locale-
   dependent.  The syntax of a skel is specified directly in terms of
   byte values, and is independent of locale.  */

static const enum char_type skel_char_type[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,   0, 1, 1, 0, 1, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  1, 0, 0, 0, 0, 0, 0, 0,   3, 3, 0, 0, 0, 0, 0, 0,
  2, 2, 2, 2, 2, 2, 2, 2,   2, 2, 0, 0, 0, 0, 0, 0,

  /* 64 */
  0, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 3, 0, 3, 0, 0,
  0, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 0, 0, 0, 0, 0,

  /* 128 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,

  /* 192 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};



/* ### WTF? since when is number conversion LOCALE DEPENDENT? */
/* stsp: In C99, various numerical string properties such as decimal point,
 * thousands separator, and the plus/minus sign are locale dependent. */

/* Converting text to numbers.  */

/* Return the value of the string of digits at DATA as an ASCII
   decimal number.  The string is at most LEN bytes long.  The value
   of the number is at most MAX.  Set *END to the address of the first
   byte after the number, or zero if an error occurred while
   converting the number (overflow, for example).

   We would like to use strtoul, but that family of functions is
   locale-dependent, whereas we're trying to parse data in a
   locale-independent format.  */
static apr_size_t
getsize(const char *data, apr_size_t len,
        const char **endptr, apr_size_t max)
{
  /* We can't detect overflow by simply comparing value against max,
     since multiplying value by ten can overflow in strange ways if
     max is close to the limits of apr_size_t.  For example, suppose
     that max is 54, and apr_size_t is six bits long; its range is
     0..63.  If we're parsing the number "502", then value will be 50
     after parsing the first two digits.  50 * 10 = 500.  But 500
     doesn't fit in an apr_size_t, so it'll be truncated to 500 mod 64
     = 52, which is less than max, so we'd fail to recognize the
     overflow.  Furthermore, it *is* greater than 50, so you can't
     detect overflow by checking whether value actually increased
     after each multiplication --- sometimes it does increase, but
     it's still wrong.

     So we do the check for overflow before we multiply value and add
     in the new digit.  */
  apr_size_t max_prefix = max / 10;
  apr_size_t max_digit = max % 10;
  apr_size_t i;
  apr_size_t value = 0;

  for (i = 0; i < len && '0' <= data[i] && data[i] <= '9'; i++)
    {
      apr_size_t digit = data[i] - '0';

      /* Check for overflow.  */
      if (value > max_prefix
          || (value == max_prefix && digit > max_digit))
        {
          *endptr = 0;
          return 0;
        }

      value = (value * 10) + digit;
    }

  /* There must be at least one digit there.  */
  if (i == 0)
    {
      *endptr = 0;
      return 0;
    }
  else
    {
      *endptr = data + i;
      return value;
    }
}


/* Checking validity of skels. */
static svn_error_t *
skel_err(const char *skel_type)
{
  return svn_error_createf(SVN_ERR_FS_MALFORMED_SKEL, NULL,
                           "Malformed%s%s skeleton",
                           skel_type ? " " : "",
                           skel_type ? skel_type : "");
}


static svn_boolean_t
is_valid_proplist_skel(const svn_skel_t *skel)
{
  int len = svn_skel__list_length(skel);

  if ((len >= 0) && (len & 1) == 0)
    {
      svn_skel_t *elt;

      for (elt = skel->children; elt; elt = elt->next)
        if (! elt->is_atom)
          return FALSE;

      return TRUE;
    }

  return FALSE;
}

static svn_boolean_t
is_valid_iproplist_skel(const svn_skel_t *skel)
{
  int len = svn_skel__list_length(skel);

  if ((len >= 0) && (len & 1) == 0)
    {
      svn_skel_t *elt;

      for (elt = skel->children; elt; elt = elt->next)
        {
          if (!elt->is_atom)
            return FALSE;

          if (elt->next == NULL)
            return FALSE;

          elt = elt->next;

          if (! is_valid_proplist_skel(elt))
            return FALSE;
        }

      return TRUE;
    }

  return FALSE;
}


static svn_skel_t *parse(const char *data, apr_size_t len,
                         apr_pool_t *pool);
static svn_skel_t *list(const char *data, apr_size_t len,
                        apr_pool_t *pool);
static svn_skel_t *implicit_atom(const char *data, apr_size_t len,
                                 apr_pool_t *pool);
static svn_skel_t *explicit_atom(const char *data, apr_size_t len,
                                 apr_pool_t *pool);


svn_skel_t *
svn_skel__parse(const char *data,
                apr_size_t len,
                apr_pool_t *pool)
{
  return parse(data, len, pool);
}


/* Parse any kind of skel object --- atom, or list.  */
static svn_skel_t *
parse(const char *data,
      apr_size_t len,
      apr_pool_t *pool)
{
  char c;

  /* The empty string isn't a valid skel.  */
  if (len <= 0)
    return NULL;

  c = *data;

  /* Is it a list, or an atom?  */
  if (c == '(')
    return list(data, len, pool);

  /* Is it a string with an implicit length?  */
  if (skel_char_type[(unsigned char) c] == type_name)
    return implicit_atom(data, len, pool);

  /* Otherwise, we assume it's a string with an explicit length;
     svn_skel__getsize will catch the error.  */
  else
    return explicit_atom(data, len, pool);
}


static svn_skel_t *
list(const char *data,
     apr_size_t len,
     apr_pool_t *pool)
{
  const char *end = data + len;
  const char *list_start;

  /* Verify that the list starts with an opening paren.  At the
     moment, all callers have checked this already, but it's more
     robust this way.  */
  if (data >= end || *data != '(')
    return NULL;

  /* Mark where the list starts.  */
  list_start = data;

  /* Skip the opening paren.  */
  data++;

  /* Parse the children.  */
  {
    svn_skel_t *children = NULL;
    svn_skel_t **tail = &children;

    for (;;)
      {
        svn_skel_t *element;

        /* Skip any whitespace.  */
        while (data < end
               && skel_char_type[(unsigned char) *data] == type_space)
          data++;

        /* End of data, but no closing paren?  */
        if (data >= end)
          return NULL;

        /* End of list?  */
        if (*data == ')')
          {
            data++;
            break;
          }

        /* Parse the next element in the list.  */
        element = parse(data, end - data, pool);
        if (! element)
          return NULL;

        /* Link that element into our list.  */
        element->next = NULL;
        *tail = element;
        tail = &element->next;

        /* Advance past that element.  */
        data = element->data + element->len;
      }

    /* Construct the return value.  */
    {
      svn_skel_t *s = apr_pcalloc(pool, sizeof(*s));

      s->is_atom = FALSE;
      s->data = list_start;
      s->len = data - list_start;
      s->children = children;

      return s;
    }
  }
}


/* Parse an atom with implicit length --- one that starts with a name
   character, terminated by whitespace, '(', ')', or end-of-data.  */
static svn_skel_t *
implicit_atom(const char *data,
              apr_size_t len,
              apr_pool_t *pool)
{
  const char *start = data;
  const char *end = data + len;
  svn_skel_t *s;

  /* Verify that the atom starts with a name character.  At the
     moment, all callers have checked this already, but it's more
     robust this way.  */
  if (data >= end || skel_char_type[(unsigned char) *data] != type_name)
    return NULL;

  /* Find the end of the string.  */
  while (++data < end
         && skel_char_type[(unsigned char) *data] != type_space
         && skel_char_type[(unsigned char) *data] != type_paren)
    ;

  /* Allocate the skel representing this string.  */
  s = apr_pcalloc(pool, sizeof(*s));
  s->is_atom = TRUE;
  s->data = start;
  s->len = data - start;

  return s;
}


/* Parse an atom with explicit length --- one that starts with a byte
   length, as a decimal ASCII number.  */
static svn_skel_t *
explicit_atom(const char *data,
              apr_size_t len,
              apr_pool_t *pool)
{
  const char *end = data + len;
  const char *next;
  apr_size_t size;
  svn_skel_t *s;

  /* Parse the length.  */
  size = getsize(data, end - data, &next, end - data);
  data = next;

  /* Exit if we overflowed, or there wasn't a valid number there.  */
  if (! data)
    return NULL;

  /* Skip the whitespace character after the length.  */
  if (data >= end || skel_char_type[(unsigned char) *data] != type_space)
    return NULL;
  data++;

  /* Check the length.  */
  if (end - data < size)
    return NULL;

  /* Allocate the skel representing this string.  */
  s = apr_pcalloc(pool, sizeof(*s));
  s->is_atom = TRUE;
  s->data = data;
  s->len = size;

  return s;
}



/* Unparsing skeletons.  */

static apr_size_t estimate_unparsed_size(const svn_skel_t *skel);
static svn_stringbuf_t *unparse(const svn_skel_t *skel,
                                svn_stringbuf_t *str);


svn_stringbuf_t *
svn_skel__unparse(const svn_skel_t *skel, apr_pool_t *pool)
{
  svn_stringbuf_t *str
    = svn_stringbuf_create_ensure(estimate_unparsed_size(skel) + 200, pool);

  return unparse(skel, str);
}


/* Return an estimate of the number of bytes that the external
   representation of SKEL will occupy.  Since reallocing is expensive
   in pools, it's worth trying to get the buffer size right the first
   time.  */
static apr_size_t
estimate_unparsed_size(const svn_skel_t *skel)
{
  if (skel->is_atom)
    {
      if (skel->len < 100)
        /* If we have to use the explicit-length form, that'll be
           two bytes for the length, one byte for the space, and
           the contents.  */
        return skel->len + 3;
      else
        return skel->len + 30;
    }
  else
    {
      apr_size_t total_len;
      svn_skel_t *child;

      /* Allow space for opening and closing parens, and a space
         between each pair of elements.  */
      total_len = 2;
      for (child = skel->children; child; child = child->next)
        total_len += estimate_unparsed_size(child) + 1;

      return total_len;
    }
}


/* Return non-zero iff we should use the implicit-length form for SKEL.
   Assume that SKEL is an atom.  */
static svn_boolean_t
use_implicit(const svn_skel_t *skel)
{
  /* If it's null, or long, we should use explicit-length form.  */
  if (skel->len == 0
      || skel->len >= 100)
    return FALSE;

  /* If it doesn't start with a name character, we must use
     explicit-length form.  */
  if (skel_char_type[(unsigned char) skel->data[0]] != type_name)
    return FALSE;

  /* If it contains any whitespace or parens, then we must use
     explicit-length form.  */
  {
    apr_size_t i;

    for (i = 1; i < skel->len; i++)
      if (skel_char_type[(unsigned char) skel->data[i]] == type_space
          || skel_char_type[(unsigned char) skel->data[i]] == type_paren)
        return FALSE;
  }

  /* If we can't reject it for any of the above reasons, then we can
     use implicit-length form.  */
  return TRUE;
}


/* Append the concrete representation of SKEL to the string STR. */
static svn_stringbuf_t *
unparse(const svn_skel_t *skel, svn_stringbuf_t *str)
{
  if (skel->is_atom)
    {
      /* Append an atom to STR.  */
      if (use_implicit(skel))
        svn_stringbuf_appendbytes(str, skel->data, skel->len);
      else
        {
          /* Append the length to STR.  Ensure enough space for at least
           * one 64 bit int. */
          char buf[200 + SVN_INT64_BUFFER_SIZE];
          apr_size_t length_len;

          length_len = svn__ui64toa(buf, skel->len);

          SVN_ERR_ASSERT_NO_RETURN(length_len > 0);

          /* Make sure we have room for the length, the space, and the
             atom's contents.  */
          svn_stringbuf_ensure(str, str->len + length_len + 1 + skel->len);
          svn_stringbuf_appendbytes(str, buf, length_len);
          svn_stringbuf_appendbyte(str, ' ');
          svn_stringbuf_appendbytes(str, skel->data, skel->len);
        }
    }
  else
    {
      /* Append a list to STR: an opening parenthesis, the list elements
       * separated by a space, and a closing parenthesis.  */
      svn_skel_t *child;

      svn_stringbuf_appendbyte(str, '(');

      for (child = skel->children; child; child = child->next)
        {
          unparse(child, str);
          if (child->next)
            svn_stringbuf_appendbyte(str, ' ');
        }

      svn_stringbuf_appendbyte(str, ')');
    }

  return str;
}



/* Building skels.  */


svn_skel_t *
svn_skel__str_atom(const char *str, apr_pool_t *pool)
{
  svn_skel_t *skel = apr_pcalloc(pool, sizeof(*skel));
  skel->is_atom = TRUE;
  skel->data = str;
  skel->len = strlen(str);

  return skel;
}


svn_skel_t *
svn_skel__mem_atom(const void *addr,
                   apr_size_t len,
                   apr_pool_t *pool)
{
  svn_skel_t *skel = apr_pcalloc(pool, sizeof(*skel));
  skel->is_atom = TRUE;
  skel->data = addr;
  skel->len = len;

  return skel;
}


svn_skel_t *
svn_skel__make_empty_list(apr_pool_t *pool)
{
  svn_skel_t *skel = apr_pcalloc(pool, sizeof(*skel));
  return skel;
}

svn_skel_t *svn_skel__dup(const svn_skel_t *src_skel, svn_boolean_t dup_data,
                          apr_pool_t *result_pool)
{
  svn_skel_t *skel = apr_pmemdup(result_pool, src_skel, sizeof(svn_skel_t));

  if (dup_data && skel->data)
    {
      if (skel->is_atom)
        skel->data = apr_pmemdup(result_pool, skel->data, skel->len);
      else
        {
          /* When creating a skel this would be NULL, 0 for a list.
             When parsing a string to a skel this might point to real data
             delimiting the sublist. We don't copy that from here. */
          skel->data = NULL;
          skel->len = 0;
        }
    }

  if (skel->children)
    skel->children = svn_skel__dup(skel->children, dup_data, result_pool);

  if (skel->next)
    skel->next = svn_skel__dup(skel->next, dup_data, result_pool);

  return skel;
}

void
svn_skel__prepend(svn_skel_t *skel, svn_skel_t *list_skel)
{
  /* If list_skel isn't even a list, somebody's not using this
     function properly. */
  SVN_ERR_ASSERT_NO_RETURN(! list_skel->is_atom);

  skel->next = list_skel->children;
  list_skel->children = skel;
}


void svn_skel__prepend_int(apr_int64_t value,
                           svn_skel_t *skel,
                           apr_pool_t *result_pool)
{
  char *val_string = apr_palloc(result_pool, SVN_INT64_BUFFER_SIZE);
  svn__i64toa(val_string, value);

  svn_skel__prepend_str(val_string, skel, result_pool);
}


void svn_skel__prepend_str(const char *value,
                           svn_skel_t *skel,
                           apr_pool_t *result_pool)
{
  svn_skel_t *atom = svn_skel__str_atom(value, result_pool);

  svn_skel__prepend(atom, skel);
}


void svn_skel__append(svn_skel_t *list_skel, svn_skel_t *skel)
{
  SVN_ERR_ASSERT_NO_RETURN(list_skel != NULL && !list_skel->is_atom);

  if (list_skel->children == NULL)
    {
      list_skel->children = skel;
    }
  else
    {
      list_skel = list_skel->children;
      while (list_skel->next != NULL)
        list_skel = list_skel->next;
      list_skel->next = skel;
    }
}


/* Examining skels.  */


svn_boolean_t
svn_skel__matches_atom(const svn_skel_t *skel, const char *str)
{
  if (skel && skel->is_atom)
    {
      apr_size_t len = strlen(str);

      return (skel->len == len
              && ! memcmp(skel->data, str, len));
    }
  return FALSE;
}


int
svn_skel__list_length(const svn_skel_t *skel)
{
  int len = 0;
  const svn_skel_t *child;

  if ((! skel) || skel->is_atom)
    return -1;

  for (child = skel->children; child; child = child->next)
    len++;

  return len;
}



/* Parsing and unparsing into high-level types. */

svn_error_t *
svn_skel__parse_int(apr_int64_t *n, const svn_skel_t *skel,
                    apr_pool_t *scratch_pool)
{
  const char *str;

  /* We need to duplicate the SKEL contents in order to get a NUL-terminated
     version of it. The SKEL may not have valid memory at DATA[LEN].  */
  str = apr_pstrmemdup(scratch_pool, skel->data, skel->len);
  return svn_error_trace(svn_cstring_atoi64(n, str));
}


svn_error_t *
svn_skel__parse_proplist(apr_hash_t **proplist_p,
                         const svn_skel_t *skel,
                         apr_pool_t *pool /* result_pool */)
{
  apr_hash_t *proplist = NULL;
  svn_skel_t *elt;

  /* Validate the skel. */
  if (! is_valid_proplist_skel(skel))
    return skel_err("proplist");

  /* Create the returned structure */
  proplist = apr_hash_make(pool);
  for (elt = skel->children; elt; elt = elt->next->next)
    {
      svn_string_t *value = svn_string_ncreate(elt->next->data,
                                               elt->next->len, pool);
      apr_hash_set(proplist,
                   apr_pstrmemdup(pool, elt->data, elt->len),
                   elt->len,
                   value);
    }

  /* Return the structure. */
  *proplist_p = proplist;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_skel__parse_iprops(apr_array_header_t **iprops,
                       const svn_skel_t *skel,
                       apr_pool_t *result_pool)
{
  svn_skel_t *elt;

  /* Validate the skel. */
  if (! is_valid_iproplist_skel(skel))
    return skel_err("iprops");

  /* Create the returned structure */
  *iprops = apr_array_make(result_pool, 1,
                           sizeof(svn_prop_inherited_item_t *));

  for (elt = skel->children; elt; elt = elt->next->next)
    {
      svn_prop_inherited_item_t *new_iprop = apr_palloc(result_pool,
                                                        sizeof(*new_iprop));
      svn_string_t *repos_parent = svn_string_ncreate(elt->data, elt->len,
                                                      result_pool);
      SVN_ERR(svn_skel__parse_proplist(&(new_iprop->prop_hash), elt->next,
                                       result_pool));
      new_iprop->path_or_url = repos_parent->data;
      APR_ARRAY_PUSH(*iprops, svn_prop_inherited_item_t *) = new_iprop;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_skel__parse_prop(svn_string_t **propval,
                     const svn_skel_t *skel,
                     const char *propname,
                     apr_pool_t *pool /* result_pool */)
{
  svn_skel_t *elt;

  *propval = NULL;

  /* Validate the skel. */
  if (! is_valid_proplist_skel(skel))
    return skel_err("proplist");

  /* Look for PROPNAME in SKEL. */
  for (elt = skel->children; elt; elt = elt->next->next)
    {
      if (elt->len == strlen(propname)
          && strncmp(propname, elt->data, elt->len) == 0)
        {
          *propval = svn_string_ncreate(elt->next->data, elt->next->len,
                                        pool);
          break;
        }
      else
        {
          continue;
        }
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_skel__unparse_proplist(svn_skel_t **skel_p,
                           const apr_hash_t *proplist,
                           apr_pool_t *pool)
{
  svn_skel_t *skel = svn_skel__make_empty_list(pool);
  apr_hash_index_t *hi;

  /* Create the skel. */
  if (proplist)
    {
      /* Loop over hash entries */
      for (hi = apr_hash_first(pool, (apr_hash_t *)proplist); hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          apr_ssize_t klen;
          svn_string_t *value;

          apr_hash_this(hi, &key, &klen, &val);
          value = val;

          /* VALUE */
          svn_skel__prepend(svn_skel__mem_atom(value->data, value->len, pool),
                            skel);

          /* NAME */
          svn_skel__prepend(svn_skel__mem_atom(key, klen, pool), skel);
        }
    }

  /* Validate and return the skel. */
  if (! is_valid_proplist_skel(skel))
    return skel_err("proplist");
  *skel_p = skel;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_skel__unparse_iproplist(svn_skel_t **skel_p,
                            const apr_array_header_t *inherited_props,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_skel_t *skel = svn_skel__make_empty_list(result_pool);

  /* Create the skel. */
  if (inherited_props)
    {
      int i;
      apr_hash_index_t *hi;

      for (i = 0; i < inherited_props->nelts; i++)
        {
          svn_prop_inherited_item_t *iprop =
            APR_ARRAY_IDX(inherited_props, i, svn_prop_inherited_item_t *);

          svn_skel_t *skel_list = svn_skel__make_empty_list(result_pool);
          svn_skel_t *skel_atom;

          /* Loop over hash entries */
          for (hi = apr_hash_first(scratch_pool, iprop->prop_hash);
               hi;
               hi = apr_hash_next(hi))
            {
              const void *key;
              void *val;
              apr_ssize_t klen;
              svn_string_t *value;

              apr_hash_this(hi, &key, &klen, &val);
              value = val;

              /* VALUE */
              svn_skel__prepend(svn_skel__mem_atom(value->data, value->len,
                                                   result_pool), skel_list);

              /* NAME */
              svn_skel__prepend(svn_skel__mem_atom(key, klen, result_pool),
                                skel_list);
            }

          skel_atom = svn_skel__str_atom(
            apr_pstrdup(result_pool, iprop->path_or_url), result_pool);
          svn_skel__append(skel, skel_atom);
          svn_skel__append(skel, skel_list);
        }
    }

  /* Validate and return the skel. */
  if (! is_valid_iproplist_skel(skel))
    return skel_err("iproplist");

  *skel_p = skel;
  return SVN_NO_ERROR;
}
