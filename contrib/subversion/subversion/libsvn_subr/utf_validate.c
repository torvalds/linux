/*
 * utf_validate.c:  Validate a UTF-8 string
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

/* Validate a UTF-8 string according to the rules in
 *
 *    Table 3-6. Well-Formed UTF-8 Bytes Sequences
 *
 * in
 *
 *    The Unicode Standard, Version 4.0
 *
 * which is available at
 *
 *    http://www.unicode.org/
 *
 * UTF-8 was originally defined in RFC-2279, Unicode's "well-formed UTF-8"
 * is a subset of that enconding.  The Unicode enconding prohibits things
 * like non-shortest encodings (some characters can be represented by more
 * than one multi-byte encoding) and the encodings for the surrogate code
 * points.  RFC-3629 superceeds RFC-2279 and adopts the same well-formed
 * rules as Unicode.  This is the ABNF in RFC-3629 that describes
 * well-formed UTF-8 rules:
 *
 *   UTF8-octets = *( UTF8-char )
 *   UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
 *   UTF8-1      = %x00-7F
 *   UTF8-2      = %xC2-DF UTF8-tail
 *   UTF8-3      = %xE0 %xA0-BF UTF8-tail /
 *                 %xE1-EC 2( UTF8-tail ) /
 *                 %xED %x80-9F UTF8-tail /
 *                 %xEE-EF 2( UTF8-tail )
 *   UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) /
 *                 %xF1-F3 3( UTF8-tail ) /
 *                 %xF4 %x80-8F 2( UTF8-tail )
 *   UTF8-tail   = %x80-BF
 *
 */

#include "private/svn_utf_private.h"
#include "private/svn_eol_private.h"
#include "private/svn_dep_compat.h"

/* Lookup table to categorise each octet in the string. */
static const char octet_category[256] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, /* 0x00-0x7f */
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, /* 0x80-0x8f */
  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2, /* 0x90-0x9f */
  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, /* 0xa0-0xbf */
  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,
  4,  4,                                                         /* 0xc0-0xc1 */
          5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5, /* 0xc2-0xdf */
  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
  6,                                                             /* 0xe0 */
      7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,             /* 0xe1-0xec */
                                                      8,         /* 0xed */
                                                          9,  9, /* 0xee-0xef */
  10,                                                            /* 0xf0 */
      11, 11, 11,                                                /* 0xf1-0xf3 */
                  12,                                            /* 0xf4 */
                      13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13 /* 0xf5-0xff */
};

/* Machine states */
#define FSM_START         0
#define FSM_80BF          1
#define FSM_A0BF          2
#define FSM_80BF80BF      3
#define FSM_809F          4
#define FSM_90BF          5
#define FSM_80BF80BF80BF  6
#define FSM_808F          7
#define FSM_ERROR         8

/* In the FSM it appears that categories 0xc0-0xc1 and 0xf5-0xff make the
   same transitions, as do categories 0xe1-0xec and 0xee-0xef.  I wonder if
   there is any great benefit in combining categories?  It would reduce the
   memory footprint of the transition table by 16 bytes, but might it be
   harder to understand?  */

/* Machine transition table */
static const char machine [9][14] = {
  /* FSM_START */
  {FSM_START,         /* 0x00-0x7f */
   FSM_ERROR,         /* 0x80-0x8f */
   FSM_ERROR,         /* 0x90-0x9f */
   FSM_ERROR,         /* 0xa0-0xbf */
   FSM_ERROR,         /* 0xc0-0xc1 */
   FSM_80BF,          /* 0xc2-0xdf */
   FSM_A0BF,          /* 0xe0 */
   FSM_80BF80BF,      /* 0xe1-0xec */
   FSM_809F,          /* 0xed */
   FSM_80BF80BF,      /* 0xee-0xef */
   FSM_90BF,          /* 0xf0 */
   FSM_80BF80BF80BF,  /* 0xf1-0xf3 */
   FSM_808F,          /* 0xf4 */
   FSM_ERROR},        /* 0xf5-0xff */

  /* FSM_80BF */
  {FSM_ERROR,         /* 0x00-0x7f */
   FSM_START,         /* 0x80-0x8f */
   FSM_START,         /* 0x90-0x9f */
   FSM_START,         /* 0xa0-0xbf */
   FSM_ERROR,         /* 0xc0-0xc1 */
   FSM_ERROR,         /* 0xc2-0xdf */
   FSM_ERROR,         /* 0xe0 */
   FSM_ERROR,         /* 0xe1-0xec */
   FSM_ERROR,         /* 0xed */
   FSM_ERROR,         /* 0xee-0xef */
   FSM_ERROR,         /* 0xf0 */
   FSM_ERROR,         /* 0xf1-0xf3 */
   FSM_ERROR,         /* 0xf4 */
   FSM_ERROR},        /* 0xf5-0xff */

  /* FSM_A0BF */
  {FSM_ERROR,         /* 0x00-0x7f */
   FSM_ERROR,         /* 0x80-0x8f */
   FSM_ERROR,         /* 0x90-0x9f */
   FSM_80BF,          /* 0xa0-0xbf */
   FSM_ERROR,         /* 0xc0-0xc1 */
   FSM_ERROR,         /* 0xc2-0xdf */
   FSM_ERROR,         /* 0xe0 */
   FSM_ERROR,         /* 0xe1-0xec */
   FSM_ERROR,         /* 0xed */
   FSM_ERROR,         /* 0xee-0xef */
   FSM_ERROR,         /* 0xf0 */
   FSM_ERROR,         /* 0xf1-0xf3 */
   FSM_ERROR,         /* 0xf4 */
   FSM_ERROR},        /* 0xf5-0xff */

  /* FSM_80BF80BF */
  {FSM_ERROR,         /* 0x00-0x7f */
   FSM_80BF,          /* 0x80-0x8f */
   FSM_80BF,          /* 0x90-0x9f */
   FSM_80BF,          /* 0xa0-0xbf */
   FSM_ERROR,         /* 0xc0-0xc1 */
   FSM_ERROR,         /* 0xc2-0xdf */
   FSM_ERROR,         /* 0xe0 */
   FSM_ERROR,         /* 0xe1-0xec */
   FSM_ERROR,         /* 0xed */
   FSM_ERROR,         /* 0xee-0xef */
   FSM_ERROR,         /* 0xf0 */
   FSM_ERROR,         /* 0xf1-0xf3 */
   FSM_ERROR,         /* 0xf4 */
   FSM_ERROR},        /* 0xf5-0xff */

  /* FSM_809F */
  {FSM_ERROR,         /* 0x00-0x7f */
   FSM_80BF,          /* 0x80-0x8f */
   FSM_80BF,          /* 0x90-0x9f */
   FSM_ERROR,         /* 0xa0-0xbf */
   FSM_ERROR,         /* 0xc0-0xc1 */
   FSM_ERROR,         /* 0xc2-0xdf */
   FSM_ERROR,         /* 0xe0 */
   FSM_ERROR,         /* 0xe1-0xec */
   FSM_ERROR,         /* 0xed */
   FSM_ERROR,         /* 0xee-0xef */
   FSM_ERROR,         /* 0xf0 */
   FSM_ERROR,         /* 0xf1-0xf3 */
   FSM_ERROR,         /* 0xf4 */
   FSM_ERROR},        /* 0xf5-0xff */

  /* FSM_90BF */
  {FSM_ERROR,         /* 0x00-0x7f */
   FSM_ERROR,         /* 0x80-0x8f */
   FSM_80BF80BF,      /* 0x90-0x9f */
   FSM_80BF80BF,      /* 0xa0-0xbf */
   FSM_ERROR,         /* 0xc0-0xc1 */
   FSM_ERROR,         /* 0xc2-0xdf */
   FSM_ERROR,         /* 0xe0 */
   FSM_ERROR,         /* 0xe1-0xec */
   FSM_ERROR,         /* 0xed */
   FSM_ERROR,         /* 0xee-0xef */
   FSM_ERROR,         /* 0xf0 */
   FSM_ERROR,         /* 0xf1-0xf3 */
   FSM_ERROR,         /* 0xf4 */
   FSM_ERROR},        /* 0xf5-0xff */

  /* FSM_80BF80BF80BF */
  {FSM_ERROR,         /* 0x00-0x7f */
   FSM_80BF80BF,      /* 0x80-0x8f */
   FSM_80BF80BF,      /* 0x90-0x9f */
   FSM_80BF80BF,      /* 0xa0-0xbf */
   FSM_ERROR,         /* 0xc0-0xc1 */
   FSM_ERROR,         /* 0xc2-0xdf */
   FSM_ERROR,         /* 0xe0 */
   FSM_ERROR,         /* 0xe1-0xec */
   FSM_ERROR,         /* 0xed */
   FSM_ERROR,         /* 0xee-0xef */
   FSM_ERROR,         /* 0xf0 */
   FSM_ERROR,         /* 0xf1-0xf3 */
   FSM_ERROR,         /* 0xf4 */
   FSM_ERROR},        /* 0xf5-0xff */

  /* FSM_808F */
  {FSM_ERROR,         /* 0x00-0x7f */
   FSM_80BF80BF,      /* 0x80-0x8f */
   FSM_ERROR,         /* 0x90-0x9f */
   FSM_ERROR,         /* 0xa0-0xbf */
   FSM_ERROR,         /* 0xc0-0xc1 */
   FSM_ERROR,         /* 0xc2-0xdf */
   FSM_ERROR,         /* 0xe0 */
   FSM_ERROR,         /* 0xe1-0xec */
   FSM_ERROR,         /* 0xed */
   FSM_ERROR,         /* 0xee-0xef */
   FSM_ERROR,         /* 0xf0 */
   FSM_ERROR,         /* 0xf1-0xf3 */
   FSM_ERROR,         /* 0xf4 */
   FSM_ERROR},        /* 0xf5-0xff */

  /* FSM_ERROR */
  {FSM_ERROR,         /* 0x00-0x7f */
   FSM_ERROR,         /* 0x80-0x8f */
   FSM_ERROR,         /* 0x90-0x9f */
   FSM_ERROR,         /* 0xa0-0xbf */
   FSM_ERROR,         /* 0xc0-0xc1 */
   FSM_ERROR,         /* 0xc2-0xdf */
   FSM_ERROR,         /* 0xe0 */
   FSM_ERROR,         /* 0xe1-0xec */
   FSM_ERROR,         /* 0xed */
   FSM_ERROR,         /* 0xee-0xef */
   FSM_ERROR,         /* 0xf0 */
   FSM_ERROR,         /* 0xf1-0xf3 */
   FSM_ERROR,         /* 0xf4 */
   FSM_ERROR},        /* 0xf5-0xff */
};

/* Scan MAX_LEN bytes in *DATA for chars that are not in the octet
 * category 0 (FSM_START).  Return the position of the first such char
 * or DATA + MAX_LEN if all were cat 0.
 */
static const char *
first_non_fsm_start_char(const char *data, apr_size_t max_len)
{
#if SVN_UNALIGNED_ACCESS_IS_OK

  /* Scan the input one machine word at a time. */
  for (; max_len > sizeof(apr_uintptr_t)
       ; data += sizeof(apr_uintptr_t), max_len -= sizeof(apr_uintptr_t))
    if (*(const apr_uintptr_t *)data & SVN__BIT_7_SET)
      break;

#endif

  /* The remaining odd bytes will be examined the naive way: */
  for (; max_len > 0; ++data, --max_len)
    if ((unsigned char)*data >= 0x80)
      break;

  return data;
}

const char *
svn_utf__last_valid(const char *data, apr_size_t len)
{
  const char *start = first_non_fsm_start_char(data, len);
  const char *end = data + len;
  int state = FSM_START;

  data = start;
  while (data < end)
    {
      unsigned char octet = *data++;
      int category = octet_category[octet];
      state = machine[state][category];
      if (state == FSM_START)
        start = data;
    }
  return start;
}

svn_boolean_t
svn_utf__cstring_is_valid(const char *data)
{
  if (!data)
    return FALSE;

  return svn_utf__is_valid(data, strlen(data));
}

svn_boolean_t
svn_utf__is_valid(const char *data, apr_size_t len)
{
  const char *end = data + len;
  int state = FSM_START;

  if (!data)
    return FALSE;

  data = first_non_fsm_start_char(data, len);

  while (data < end)
    {
      unsigned char octet = *data++;
      int category = octet_category[octet];
      state = machine[state][category];
    }
  return state == FSM_START;
}

const char *
svn_utf__last_valid2(const char *data, apr_size_t len)
{
  const char *start = first_non_fsm_start_char(data, len);
  const char *end = data + len;
  int state = FSM_START;

  data = start;
  while (data < end)
    {
      unsigned char octet = *data++;
      switch (state)
        {
        case FSM_START:
          if (octet <= 0x7F)
            break;
          else if (octet <= 0xC1)
            state = FSM_ERROR;
          else if (octet <= 0xDF)
            state = FSM_80BF;
          else if (octet == 0xE0)
            state = FSM_A0BF;
          else if (octet <= 0xEC)
            state = FSM_80BF80BF;
          else if (octet == 0xED)
            state = FSM_809F;
          else if (octet <= 0xEF)
            state = FSM_80BF80BF;
          else if (octet == 0xF0)
            state = FSM_90BF;
          else if (octet <= 0xF3)
            state = FSM_80BF80BF80BF;
          else if (octet <= 0xF4)
            state = FSM_808F;
          else
            state = FSM_ERROR;
          break;
        case FSM_80BF:
          if (octet >= 0x80 && octet <= 0xBF)
            state = FSM_START;
          else
            state = FSM_ERROR;
          break;
        case FSM_A0BF:
          if (octet >= 0xA0 && octet <= 0xBF)
            state = FSM_80BF;
          else
            state = FSM_ERROR;
          break;
        case FSM_80BF80BF:
          if (octet >= 0x80 && octet <= 0xBF)
            state = FSM_80BF;
          else
            state = FSM_ERROR;
          break;
        case FSM_809F:
          if (octet >= 0x80 && octet <= 0x9F)
            state = FSM_80BF;
          else
            state = FSM_ERROR;
          break;
        case FSM_90BF:
          if (octet >= 0x90 && octet <= 0xBF)
            state = FSM_80BF80BF;
          else
            state = FSM_ERROR;
          break;
        case FSM_80BF80BF80BF:
          if (octet >= 0x80 && octet <= 0xBF)
            state = FSM_80BF80BF;
          else
            state = FSM_ERROR;
          break;
        case FSM_808F:
          if (octet >= 0x80 && octet <= 0x8F)
            state = FSM_80BF80BF;
          else
            state = FSM_ERROR;
          break;
        default:
        case FSM_ERROR:
          return start;
        }
      if (state == FSM_START)
        start = data;
    }
  return start;
}
