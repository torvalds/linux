/*
 * mergeinfo.c:  Mergeinfo parsing and handling
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
#include <ctype.h>

#include "svn_path.h"
#include "svn_types.h"
#include "svn_ctype.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_string.h"
#include "svn_mergeinfo.h"
#include "private/svn_fspath.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "svn_private_config.h"
#include "svn_hash.h"
#include "private/svn_dep_compat.h"

/* Return TRUE iff the forward revision range FIRST wholly contains the
 * forward revision range SECOND and (if CONSIDER_INHERITANCE is TRUE) has
 * the same inheritability. */
static svn_boolean_t
range_contains(const svn_merge_range_t *first, const svn_merge_range_t *second,
               svn_boolean_t consider_inheritance);


/* Attempt to combine two ranges, IN1 and IN2. If they are adjacent or
   overlapping, and their inheritability allows them to be combined, put
   the result in OUTPUT and return TRUE, otherwise return FALSE.

   CONSIDER_INHERITANCE determines how to account for the inheritability
   of IN1 and IN2 when trying to combine ranges.  If ranges with different
   inheritability are combined (CONSIDER_INHERITANCE must be FALSE for this
   to happen) the result is inheritable.  If both ranges are inheritable the
   result is inheritable.  And only if both ranges are non-inheritable
   the result is non-inheritable.

   Range overlapping detection algorithm from
   http://c2.com/cgi-bin/wiki/fullSearch?TestIfDateRangesOverlap
*/
static svn_boolean_t
combine_ranges(svn_merge_range_t *output,
               const svn_merge_range_t *in1,
               const svn_merge_range_t *in2,
               svn_boolean_t consider_inheritance)
{
  if (in1->start <= in2->end && in2->start <= in1->end)
    {
      if (!consider_inheritance
          || (consider_inheritance
              && (in1->inheritable == in2->inheritable)))
        {
          output->start = MIN(in1->start, in2->start);
          output->end = MAX(in1->end, in2->end);
          output->inheritable = (in1->inheritable || in2->inheritable);
          return TRUE;
        }
    }
  return FALSE;
}

/* pathname -> PATHNAME */
static svn_error_t *
parse_pathname(const char **input,
               const char *end,
               const char **pathname,
               apr_pool_t *pool)
{
  const char *curr = *input;
  const char *last_colon = NULL;

  /* A pathname may contain colons, so find the last colon before END
     or newline.  We'll consider this the divider between the pathname
     and the revisionlist. */
  while (curr < end && *curr != '\n')
    {
      if (*curr == ':')
        last_colon = curr;
      curr++;
    }

  if (!last_colon)
    return svn_error_create(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                            _("Pathname not terminated by ':'"));

  /* Tolerate relative repository paths, but convert them to absolute.
     ### Efficiency?  1 string duplication here, 2 in canonicalize. */
  *pathname = svn_fspath__canonicalize(apr_pstrndup(pool, *input,
                                                    last_colon - *input),
                                       pool);

  *input = last_colon;

  return SVN_NO_ERROR;
}

/* Return TRUE iff (svn_merge_range_t *) RANGE describes a valid, forward
 * revision range.
 *
 * Note: The smallest valid value of RANGE->start is 0 because it is an
 * exclusive endpoint, being one less than the revision number of the first
 * change described by the range, and the oldest possible change is "r1" as
 * there cannot be a change "r0". */
#define IS_VALID_FORWARD_RANGE(range) \
  (SVN_IS_VALID_REVNUM((range)->start) && ((range)->start < (range)->end))

/* Ways in which two svn_merge_range_t can intersect or adjoin, if at all. */
typedef enum intersection_type_t
{
  /* Ranges don't intersect and don't adjoin. */
  svn__no_intersection,

  /* Ranges are equal. */
  svn__equal_intersection,

  /* Ranges adjoin but don't overlap. */
  svn__adjoining_intersection,

  /* Ranges overlap but neither is a subset of the other. */
  svn__overlapping_intersection,

  /* One range is a proper subset of the other. */
  svn__proper_subset_intersection
} intersection_type_t;

/* Given ranges R1 and R2, both of which must be forward merge ranges,
   set *INTERSECTION_TYPE to describe how the ranges intersect, if they
   do at all.  The inheritance type of the ranges is not considered. */
static svn_error_t *
get_type_of_intersection(const svn_merge_range_t *r1,
                         const svn_merge_range_t *r2,
                         intersection_type_t *intersection_type)
{
  SVN_ERR_ASSERT(r1);
  SVN_ERR_ASSERT(r2);
  SVN_ERR_ASSERT(IS_VALID_FORWARD_RANGE(r1));
  SVN_ERR_ASSERT(IS_VALID_FORWARD_RANGE(r2));

  if (!(r1->start <= r2->end && r2->start <= r1->end))
    *intersection_type = svn__no_intersection;
  else if (r1->start == r2->start && r1->end == r2->end)
    *intersection_type = svn__equal_intersection;
  else if (r1->end == r2->start || r2->end == r1->start)
    *intersection_type = svn__adjoining_intersection;
  else if (r1->start <= r2->start && r1->end >= r2->end)
    *intersection_type = svn__proper_subset_intersection;
  else if (r2->start <= r1->start && r2->end >= r1->end)
    *intersection_type = svn__proper_subset_intersection;
  else
    *intersection_type = svn__overlapping_intersection;

  return SVN_NO_ERROR;
}

/* Modify or extend RANGELIST (a list of merge ranges) to incorporate
   NEW_RANGE. RANGELIST is a "rangelist" as defined in svn_mergeinfo.h.

   OVERVIEW

   Determine the minimal set of non-overlapping merge ranges required to
   represent the combination of RANGELIST and NEW_RANGE. The result depends
   on whether and how NEW_RANGE overlaps any merge range[*] in RANGELIST,
   and also on any differences in the inheritability of each range,
   according to the rules described below. Modify RANGELIST to represent
   this result, by adjusting the last range in it and/or appending one or
   two more ranges.

   ([*] Due to the simplifying assumption below, only the last range in
   RANGELIST is considered.)

   DETAILS

   If RANGELIST is not empty assume NEW_RANGE does not intersect with any
   range before the last one in RANGELIST.

   If RANGELIST is empty or NEW_RANGE does not intersect with the lastrange
   in RANGELIST, then append a copy of NEW_RANGE, allocated in RESULT_POOL,
   to RANGELIST.

   If NEW_RANGE intersects with the last range in RANGELIST then combine
   these two ranges as described below:

   If the intersecting ranges have the same inheritability then simply
   combine the ranges in place.  Otherwise, if the ranges intersect but
   differ in inheritability, then merge the ranges as dictated by
   CONSIDER_INHERITANCE:

   If CONSIDER_INHERITANCE is false then intersecting ranges are combined
   into a single range.  The inheritability of the resulting range is
   non-inheritable *only* if both ranges are non-inheritable, otherwise the
   combined range is inheritable, e.g.:

     Last range in        NEW_RANGE        RESULTING RANGES
     RANGELIST
     -------------        ---------        ----------------
     4-10*                6-13             4-13
     4-10                 6-13*            4-13
     4-10*                6-13*            4-13*

   If CONSIDER_INHERITANCE is true, then only the intersection between the
   two ranges is combined, with the inheritability of the resulting range
   non-inheritable only if both ranges were non-inheritable.  The
   non-intersecting portions are added as separate ranges allocated in
   RESULT_POOL, e.g.:

     Last range in        NEW_RANGE        RESULTING RANGES
     RANGELIST
     -------------        ---------        ----------------
     4-10*                6                4-5*, 6, 7-10*
     4-10*                6-12             4-5*, 6-12

   Note that the standard rules for rangelists still apply and overlapping
   ranges are not allowed.  So if the above would result in overlapping
   ranges of the same inheritance, the overlapping ranges are merged into a
   single range, e.g.:

     Last range in        NEW_RANGE        RESULTING RANGES
     RANGELIST
     -------------        ---------        ----------------
     4-10                 6*               4-10 (Not 4-5, 6, 7-10)

   When replacing the last range in RANGELIST, either allocate a new range in
   RESULT_POOL or modify the existing range in place.  Any new ranges added
   to RANGELIST are allocated in RESULT_POOL.
*/
static svn_error_t *
combine_with_lastrange(const svn_merge_range_t *new_range,
                       svn_rangelist_t *rangelist,
                       svn_boolean_t consider_inheritance,
                       apr_pool_t *result_pool)
{
  svn_merge_range_t *lastrange;
  svn_merge_range_t combined_range;

  /* We don't accept a NULL RANGELIST. */
  SVN_ERR_ASSERT(rangelist);

  if (rangelist->nelts > 0)
    lastrange = APR_ARRAY_IDX(rangelist, rangelist->nelts - 1, svn_merge_range_t *);
  else
    lastrange = NULL;

  if (!lastrange)
    {
      /* No *LASTRANGE so push NEW_RANGE onto RANGELIST and we are done. */
      APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) =
        svn_merge_range_dup(new_range, result_pool);
    }
  else if (combine_ranges(&combined_range, lastrange, new_range,
                     consider_inheritance))
    {
      *lastrange = combined_range;
    }
  else if (!consider_inheritance)
    {
      /* We are not considering inheritance so we can merge intersecting
         ranges of different inheritability.  Of course if the ranges
         don't intersect at all we simply push NEW_RANGE onto RANGELIST. */
      APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) =
            svn_merge_range_dup(new_range, result_pool);
    }
  else /* Considering inheritance */
    {
      /* If we are here then the ranges either don't intersect or do
          intersect but have differing inheritability.  Check for the
          first case as that is easy to handle. */
      intersection_type_t intersection_type;
      svn_boolean_t sorted = FALSE;

      SVN_ERR(get_type_of_intersection(new_range, lastrange,
                                        &intersection_type));

      switch (intersection_type)
        {
          case svn__no_intersection:
            /* NEW_RANGE and *LASTRANGE *really* don't intersect so
                just push NEW_RANGE onto RANGELIST. */
            APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) =
              svn_merge_range_dup(new_range, result_pool);
            sorted = (svn_sort_compare_ranges(&lastrange,
                                              &new_range) < 0);
            break;

          case svn__equal_intersection:
            /* They range are equal so all we do is force the
                inheritability of lastrange to true. */
            lastrange->inheritable = TRUE;
            sorted = TRUE;
            break;

          case svn__adjoining_intersection:
            /* They adjoin but don't overlap so just push NEW_RANGE
                onto RANGELIST. */
            APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) =
              svn_merge_range_dup(new_range, result_pool);
            sorted = (svn_sort_compare_ranges(&lastrange,
                                              &new_range) < 0);
            break;

          case svn__overlapping_intersection:
            /* They ranges overlap but neither is a proper subset of
                the other.  We'll end up pusing two new ranges onto
                RANGELIST, the intersecting part and the part unique to
                NEW_RANGE.*/
            {
              svn_merge_range_t *r1 = svn_merge_range_dup(lastrange,
                                                          result_pool);
              svn_merge_range_t *r2 = svn_merge_range_dup(new_range,
                                                          result_pool);

              /* Pop off *LASTRANGE to make our manipulations
                  easier. */
              apr_array_pop(rangelist);

              /* Ensure R1 is the older range. */
              if (r2->start < r1->start)
                {
                  /* Swap R1 and R2. */
                  *r2 = *r1;
                  *r1 = *new_range;
                }

              /* Absorb the intersecting ranges into the
                  inheritable range. */
              if (r1->inheritable)
                r2->start = r1->end;
              else
                r1->end = r2->start;

              /* Push everything back onto RANGELIST. */
              APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = r1;
              sorted = (svn_sort_compare_ranges(&lastrange,
                                                &r1) < 0);
              APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = r2;
              if (sorted)
                sorted = (svn_sort_compare_ranges(&r1, &r2) < 0);
              break;
            }

          default: /* svn__proper_subset_intersection */
            {
              /* One range is a proper subset of the other. */
              svn_merge_range_t *r1 = svn_merge_range_dup(lastrange,
                                                          result_pool);
              svn_merge_range_t *r2 = svn_merge_range_dup(new_range,
                                                          result_pool);
              svn_merge_range_t *r3 = NULL;

              /* Pop off *LASTRANGE to make our manipulations
                  easier. */
              apr_array_pop(rangelist);

              /* Ensure R1 is the superset. */
              if (r2->start < r1->start || r2->end > r1->end)
                {
                  /* Swap R1 and R2. */
                  *r2 = *r1;
                  *r1 = *new_range;
                }

              if (r1->inheritable)
                {
                  /* The simple case: The superset is inheritable, so
                      just combine r1 and r2. */
                  r1->start = MIN(r1->start, r2->start);
                  r1->end = MAX(r1->end, r2->end);
                  r2 = NULL;
                }
              else if (r1->start == r2->start)
                {
                  svn_revnum_t tmp_revnum;

                  /* *LASTRANGE and NEW_RANGE share an end point. */
                  tmp_revnum = r1->end;
                  r1->end = r2->end;
                  r2->inheritable = r1->inheritable;
                  r1->inheritable = TRUE;
                  r2->start = r1->end;
                  r2->end = tmp_revnum;
                }
              else if (r1->end == r2->end)
                {
                  /* *LASTRANGE and NEW_RANGE share an end point. */
                  r1->end = r2->start;
                  r2->inheritable = TRUE;
                }
              else
                {
                  /* NEW_RANGE and *LASTRANGE share neither start
                      nor end points. */
                  r3 = apr_pcalloc(result_pool, sizeof(*r3));
                  r3->start = r2->end;
                  r3->end = r1->end;
                  r3->inheritable = r1->inheritable;
                  r2->inheritable = TRUE;
                  r1->end = r2->start;
                }

              /* Push everything back onto RANGELIST. */
              APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = r1;
              sorted = (svn_sort_compare_ranges(&lastrange, &r1) < 0);
              if (r2)
                {
                  APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = r2;
                  if (sorted)
                    sorted = (svn_sort_compare_ranges(&r1, &r2) < 0);
                }
              if (r3)
                {
                  APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = r3;
                  if (sorted)
                    {
                      if (r2)
                        sorted = (svn_sort_compare_ranges(&r2,
                                                          &r3) < 0);
                      else
                        sorted = (svn_sort_compare_ranges(&r1,
                                                          &r3) < 0);
                    }
                }
              break;
            }
        }

      /* Some of the above cases might have put *RANGELIST out of
          order, so re-sort.*/
      if (!sorted)
        svn_sort__array(rangelist, svn_sort_compare_ranges);
    }

  return SVN_NO_ERROR;
}

/* Convert a single svn_merge_range_t *RANGE back into a string.  */
static char *
range_to_string(const svn_merge_range_t *range,
                apr_pool_t *pool)
{
  const char *mark
    = range->inheritable ? "" : SVN_MERGEINFO_NONINHERITABLE_STR;

  if (range->start == range->end - 1)
    return apr_psprintf(pool, "%ld%s", range->end, mark);
  else if (range->start - 1 == range->end)
    return apr_psprintf(pool, "-%ld%s", range->start, mark);
  else if (range->start < range->end)
    return apr_psprintf(pool, "%ld-%ld%s", range->start + 1, range->end, mark);
  else
    return apr_psprintf(pool, "%ld-%ld%s", range->start, range->end + 1, mark);
}

/* Helper for svn_mergeinfo_parse()
   Append revision ranges onto the array RANGELIST to represent the range
   descriptions found in the string *INPUT.  Read only as far as a newline
   or the position END, whichever comes first.  Set *INPUT to the position
   after the last character of INPUT that was used.

   revisionlist -> (revisionelement)(COMMA revisionelement)*
   revisionrange -> REVISION "-" REVISION("*")
   revisionelement -> revisionrange | REVISION("*")
*/
static svn_error_t *
parse_rangelist(const char **input, const char *end,
                svn_rangelist_t *rangelist,
                apr_pool_t *pool)
{
  const char *curr = *input;

  /* Eat any leading horizontal white-space before the rangelist. */
  while (curr < end && *curr != '\n' && isspace(*curr))
    curr++;

  if (*curr == '\n' || curr == end)
    {
      /* Empty range list. */
      *input = curr;
      return SVN_NO_ERROR;
    }

  while (curr < end && *curr != '\n')
    {
      /* Parse individual revisions or revision ranges. */
      svn_merge_range_t *mrange = apr_pcalloc(pool, sizeof(*mrange));
      svn_revnum_t firstrev;

      SVN_ERR(svn_revnum_parse(&firstrev, curr, &curr));
      if (*curr != '-' && *curr != '\n' && *curr != ',' && *curr != '*'
          && curr != end)
        return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                 _("Invalid character '%c' found in revision "
                                   "list"), *curr);
      mrange->start = firstrev - 1;
      mrange->end = firstrev;
      mrange->inheritable = TRUE;

      if (firstrev == 0)
        return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                 _("Invalid revision number '0' found in "
                                   "range list"));

      if (*curr == '-')
        {
          svn_revnum_t secondrev;

          curr++;
          SVN_ERR(svn_revnum_parse(&secondrev, curr, &curr));
          if (firstrev > secondrev)
            return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                     _("Unable to parse reversed revision "
                                       "range '%ld-%ld'"),
                                       firstrev, secondrev);
          else if (firstrev == secondrev)
            return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                     _("Unable to parse revision range "
                                       "'%ld-%ld' with same start and end "
                                       "revisions"), firstrev, secondrev);
          mrange->end = secondrev;
        }

      if (*curr == '\n' || curr == end)
        {
          APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = mrange;
          *input = curr;
          return SVN_NO_ERROR;
        }
      else if (*curr == ',')
        {
          APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = mrange;
          curr++;
        }
      else if (*curr == '*')
        {
          mrange->inheritable = FALSE;
          curr++;
          if (*curr == ',' || *curr == '\n' || curr == end)
            {
              APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = mrange;
              if (*curr == ',')
                {
                  curr++;
                }
              else
                {
                  *input = curr;
                  return SVN_NO_ERROR;
                }
            }
          else
            {
              return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                       _("Invalid character '%c' found in "
                                         "range list"), *curr);
            }
        }
      else
        {
          return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                   _("Invalid character '%c' found in "
                                     "range list"), *curr);
        }

    }
  if (*curr != '\n')
    return svn_error_create(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                            _("Range list parsing ended before hitting "
                              "newline"));
  *input = curr;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_rangelist__parse(svn_rangelist_t **rangelist,
                     const char *str,
                     apr_pool_t *result_pool)
{
  const char *s = str;

  *rangelist = apr_array_make(result_pool, 1, sizeof(svn_merge_range_t *));
  SVN_ERR(parse_rangelist(&s, s + strlen(s), *rangelist, result_pool));
  return SVN_NO_ERROR;
}

svn_boolean_t
svn_rangelist__is_canonical(const svn_rangelist_t *rangelist)
{
  int i;
  svn_merge_range_t **ranges = (svn_merge_range_t **)rangelist->elts;

  /* Check for reversed and empty ranges */
  for (i = 0; i < rangelist->nelts; ++i)
    {
      if (ranges[i]->start >= ranges[i]->end)
        return FALSE;
    }

  /* Check for overlapping ranges */
  for (i = 0; i < rangelist->nelts - 1; ++i)
    {
      if (ranges[i]->end > ranges[i + 1]->start)
        return FALSE; /* Overlapping range */
      else if (ranges[i]->end == ranges[i+1]->start
               && ranges[i]->inheritable == ranges[i + 1]->inheritable)
        {
          return FALSE; /* Ranges should have been combined */
        }
    }

  return TRUE;
}

/* In-place combines adjacent ranges in a rangelist.
   SCRATCH_POOL is just used for providing error messages. */
svn_error_t *
svn_rangelist__canonicalize(svn_rangelist_t *rangelist,
                            apr_pool_t *scratch_pool)
{
  int i;
  svn_merge_range_t *range, *lastrange;

  if (svn_rangelist__is_canonical(rangelist))
    return SVN_NO_ERROR; /* Nothing to do */

  svn_sort__array(rangelist, svn_sort_compare_ranges);

  lastrange = APR_ARRAY_IDX(rangelist, 0, svn_merge_range_t *);

  for (i = 1; i < rangelist->nelts; i++)
    {
      range = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
      if (lastrange->start <= range->end
          && range->start <= lastrange->end)
        {
          /* The ranges are adjacent or intersect. */

          /* svn_mergeinfo_parse promises to combine overlapping
             ranges as long as their inheritability is the same. */
          if (range->start < lastrange->end
              && range->inheritable != lastrange->inheritable)
            {
              return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                       _("Unable to parse overlapping "
                                         "revision ranges '%s' and '%s' "
                                         "with different inheritance "
                                         "types"),
                                       range_to_string(lastrange,
                                                       scratch_pool),
                                       range_to_string(range,
                                                       scratch_pool));
            }

          /* Combine overlapping or adjacent ranges with the
             same inheritability. */
          if (lastrange->inheritable == range->inheritable)
            {
              lastrange->end = MAX(range->end, lastrange->end);
              svn_sort__array_delete(rangelist, i, 1);
              i--;
            }
        }
      lastrange = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
    }

  return SVN_NO_ERROR;
}

/* revisionline -> PATHNAME COLON revisionlist
 *
 * Parse one line of mergeinfo starting at INPUT, not reading beyond END,
 * into HASH. Allocate the new entry in HASH deeply from HASH's pool.
 */
static svn_error_t *
parse_revision_line(const char **input, const char *end, svn_mergeinfo_t hash,
                    apr_pool_t *scratch_pool)
{
  const char *pathname = "";
  apr_ssize_t klen;
  svn_rangelist_t *existing_rangelist;
  svn_rangelist_t *rangelist = apr_array_make(scratch_pool, 1,
                                              sizeof(svn_merge_range_t *));

  SVN_ERR(parse_pathname(input, end, &pathname, scratch_pool));

  if (*(*input) != ':')
    return svn_error_create(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                            _("Pathname not terminated by ':'"));

  *input = *input + 1;

  SVN_ERR(parse_rangelist(input, end, rangelist, scratch_pool));

  if (rangelist->nelts == 0)
      return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                               _("Mergeinfo for '%s' maps to an "
                                 "empty revision range"), pathname);
  if (*input != end && *(*input) != '\n')
    return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                             _("Could not find end of line in range list line "
                               "in '%s'"), *input);

  if (*input != end)
    *input = *input + 1;

  /* Sort the rangelist, combine adjacent ranges into single ranges, and
     make sure there are no overlapping ranges.  Luckily, most data in
     svn:mergeinfo will already be in normalized form and this will be quick.
   */
  SVN_ERR(svn_rangelist__canonicalize(rangelist, scratch_pool));

  /* Handle any funky mergeinfo with relative merge source paths that
     might exist due to issue #3547.  It's possible that this issue allowed
     the creation of mergeinfo with path keys that differ only by a
     leading slash, e.g. "trunk:4033\n/trunk:4039-4995".  In the event
     we encounter this we merge the rangelists together under a single
     absolute path key. */
  klen = strlen(pathname);
  existing_rangelist = apr_hash_get(hash, pathname, klen);
  if (existing_rangelist)
    SVN_ERR(svn_rangelist_merge2(rangelist, existing_rangelist,
                                 scratch_pool, scratch_pool));

  apr_hash_set(hash, apr_pstrmemdup(apr_hash_pool_get(hash), pathname, klen),
               klen, svn_rangelist_dup(rangelist, apr_hash_pool_get(hash)));

  return SVN_NO_ERROR;
}

/* top -> revisionline (NEWLINE revisionline)*
 *
 * Parse mergeinfo starting at INPUT, not reading beyond END, into HASH.
 * Allocate all the new entries in HASH deeply from HASH's pool.
 */
static svn_error_t *
parse_top(const char **input, const char *end, svn_mergeinfo_t hash,
          apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  while (*input < end)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(parse_revision_line(input, end, hash, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_parse(svn_mergeinfo_t *mergeinfo,
                    const char *input,
                    apr_pool_t *pool)
{
  svn_error_t *err;

  *mergeinfo = svn_hash__make(pool);
  err = parse_top(&input, input + strlen(input), *mergeinfo, pool);

  /* Always return SVN_ERR_MERGEINFO_PARSE_ERROR as the topmost error. */
  if (err && err->apr_err != SVN_ERR_MERGEINFO_PARSE_ERROR)
    err = svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, err,
                            _("Could not parse mergeinfo string '%s'"),
                            input);
  return err;
}

/* Cleanup after svn_rangelist_merge2 when it modifies the ending range of
   a single rangelist element in-place.

   If *RANGE_INDEX is not a valid element in RANGELIST do nothing.  Otherwise
   ensure that RANGELIST[*RANGE_INDEX]->END does not adjoin or overlap any
   subsequent ranges in RANGELIST.

   If overlap is found, then remove, modify, and/or add elements to RANGELIST
   as per the invariants for rangelists documented in svn_mergeinfo.h.  If
   RANGELIST[*RANGE_INDEX]->END adjoins a subsequent element then combine the
   elements if their inheritability permits -- The inheritance of intersecting
   and adjoining ranges is handled as per svn_mergeinfo_merge2.  Upon return
   set *RANGE_INDEX to the index of the youngest element modified, added, or
   adjoined to RANGELIST[*RANGE_INDEX].

   Note: Adjoining rangelist elements are those where the end rev of the older
   element is equal to the start rev of the younger element.

   Any new elements inserted into RANGELIST are allocated in  RESULT_POOL.*/
static void
adjust_remaining_ranges(svn_rangelist_t *rangelist,
                        int *range_index,
                        apr_pool_t *result_pool)
{
  int i;
  int starting_index;
  int elements_to_delete = 0;
  svn_merge_range_t *modified_range;

  if (*range_index >= rangelist->nelts)
    return;

  starting_index = *range_index + 1;
  modified_range = APR_ARRAY_IDX(rangelist, *range_index, svn_merge_range_t *);

  for (i = *range_index + 1; i < rangelist->nelts; i++)
    {
      svn_merge_range_t *next_range = APR_ARRAY_IDX(rangelist, i,
                                                    svn_merge_range_t *);

      /* If MODIFIED_RANGE doesn't adjoin or overlap the next range in
         RANGELIST then we are finished. */
      if (modified_range->end < next_range->start)
        break;

      /* Does MODIFIED_RANGE adjoin NEXT_RANGE? */
      if (modified_range->end == next_range->start)
        {
          if (modified_range->inheritable == next_range->inheritable)
            {
              /* Combine adjoining ranges with the same inheritability. */
              modified_range->end = next_range->end;
              elements_to_delete++;
            }
          else
            {
              /* Cannot join because inheritance differs. */
              (*range_index)++;
            }
          break;
        }

      /* Alright, we know MODIFIED_RANGE overlaps NEXT_RANGE, but how? */
      if (modified_range->end > next_range->end)
        {
          /* NEXT_RANGE is a proper subset of MODIFIED_RANGE and the two
             don't share the same end range. */
          if (modified_range->inheritable
              || (modified_range->inheritable == next_range->inheritable))
            {
              /* MODIFIED_RANGE absorbs NEXT_RANGE. */
              elements_to_delete++;
            }
          else
            {
              /* NEXT_RANGE is a proper subset MODIFIED_RANGE but
                 MODIFIED_RANGE is non-inheritable and NEXT_RANGE is
                 inheritable.  This means MODIFIED_RANGE is truncated,
                 NEXT_RANGE remains, and the portion of MODIFIED_RANGE
                 younger than NEXT_RANGE is added as a separate range:
                  ______________________________________________
                 |                                              |
                 M                 MODIFIED_RANGE               N
                 |                 (!inheritable)               |
                 |______________________________________________|
                                  |              |
                                  O  NEXT_RANGE  P
                                  | (inheritable)|
                                  |______________|
                                         |
                                         V
                  _______________________________________________
                 |                |              |               |
                 M MODIFIED_RANGE O  NEXT_RANGE  P   NEW_RANGE   N
                 | (!inheritable) | (inheritable)| (!inheritable)|
                 |________________|______________|_______________|
              */
              svn_merge_range_t *new_modified_range =
                apr_palloc(result_pool, sizeof(*new_modified_range));
              new_modified_range->start = next_range->end;
              new_modified_range->end = modified_range->end;
              new_modified_range->inheritable = FALSE;
              modified_range->end = next_range->start;
              (*range_index) += 2 + elements_to_delete;
              svn_sort__array_insert(rangelist, &new_modified_range,
                                     *range_index);
              /* Recurse with the new range. */
              adjust_remaining_ranges(rangelist, range_index, result_pool);
              break;
            }
        }
      else if (modified_range->end == next_range->end)
        {
          /* NEXT_RANGE is a proper subset MODIFIED_RANGE and share
             the same end range. */
          if (modified_range->inheritable
              || (modified_range->inheritable == next_range->inheritable))
            {
              /* MODIFIED_RANGE absorbs NEXT_RANGE. */
              elements_to_delete++;
            }
          else
            {
              /* The intersection between MODIFIED_RANGE and NEXT_RANGE is
                 absorbed by the latter. */
              modified_range->end = next_range->start;
              (*range_index)++;
            }
          break;
        }
      else
        {
          /* NEXT_RANGE and MODIFIED_RANGE intersect but NEXT_RANGE is not
             a proper subset of MODIFIED_RANGE, nor do the two share the
             same end revision, i.e. they overlap. */
          if (modified_range->inheritable == next_range->inheritable)
            {
              /* Combine overlapping ranges with the same inheritability. */
              modified_range->end = next_range->end;
              elements_to_delete++;
            }
          else if (modified_range->inheritable)
            {
              /* MODIFIED_RANGE absorbs the portion of NEXT_RANGE it overlaps
                 and NEXT_RANGE is truncated. */
              next_range->start = modified_range->end;
              (*range_index)++;
            }
          else
            {
              /* NEXT_RANGE absorbs the portion of MODIFIED_RANGE it overlaps
                 and MODIFIED_RANGE is truncated. */
              modified_range->end = next_range->start;
              (*range_index)++;
            }
          break;
        }
    }

  if (elements_to_delete)
    svn_sort__array_delete(rangelist, starting_index, elements_to_delete);
}

#if 0 /* Temporary debug helper code */
static svn_error_t *
dual_dump(const char *prefix,
  const svn_rangelist_t *rangelist,
  const svn_rangelist_t *changes,
  apr_pool_t *scratch_pool)
{
  svn_string_t *rls, *chg;

  SVN_ERR(svn_rangelist_to_string(&rls, rangelist, scratch_pool));
  SVN_ERR(svn_rangelist_to_string(&chg, changes, scratch_pool));

  SVN_DBG(("%s: %s / %s", prefix, rls->data, chg->data));
  return SVN_NO_ERROR;
}
#endif

svn_error_t *
svn_rangelist_merge2(svn_rangelist_t *rangelist,
                     const svn_rangelist_t *chg,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_rangelist_t *changes;
  int i = 0;
  int j;

  SVN_ERR(svn_rangelist__canonicalize(rangelist, scratch_pool));

  /* We may modify CHANGES, so make a copy in SCRATCH_POOL. */
  changes = svn_rangelist_dup(chg, scratch_pool);
  SVN_ERR(svn_rangelist__canonicalize(changes, scratch_pool));

  for (j = 0; j < changes->nelts; j++)
    {
      svn_merge_range_t *range;
      svn_merge_range_t *change =
        APR_ARRAY_IDX(changes, j, svn_merge_range_t *);
      int res;

      range = (i < rangelist->nelts)
              ? APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *)
              : NULL;

      if (!range || change->end < range->start)
        {
          /* No overlap, nor adjoin, copy change to result range */
          svn_merge_range_t *chg_copy = svn_merge_range_dup(change,
                                                            result_pool);
          svn_sort__array_insert(rangelist, &chg_copy, i++);
          continue;
        }
      else if ((change->start > range->end)
               || (change->start == range->end
                   && change->inheritable != range->inheritable))
        {
          /* No overlap, nor adjoin. Check next range item against change */
          i++;
          j--;
          continue;
        }

      if (change->start < range->start
          && range->inheritable != change->inheritable
          && ! (change->inheritable && range_contains(change, range, FALSE))
          && ! (range->inheritable && range_contains(range, change, FALSE)))
        {
          /* Can't fold change into existing range.
             Insert new range before range */

          svn_merge_range_t *chg_copy = svn_merge_range_dup(change,
                                                            result_pool);

          chg_copy->start = MIN(change->start, range->start);
          if (! change->inheritable)
            chg_copy->end = range->start;
          else
            range->start = change->end;

          svn_sort__array_insert(rangelist, &chg_copy, i++);

          change->start = chg_copy->end;
          if (change->start >= change->end)
            continue; /* No overlap with range left */
        }
      else
        {
          range->start = MIN(range->start, change->start);
        }

      SVN_ERR_ASSERT(change->start >= range->start);

      res = svn_sort_compare_ranges(&range, &change);

      if (res == 0)
        {
          /* Only when merging two non-inheritable ranges is the result also
             non-inheritable.  In all other cases ensure an inheritable
             result. */
          if (range->inheritable || change->inheritable)
            range->inheritable = TRUE;
          i++;
          continue;
        }
      else if (res < 0) /* CHANGE is younger than RANGE */
        {
          if (range->end == change->start)
            {
              /* RANGE and CHANGE adjoin */
              if (range->inheritable == change->inheritable)
                {
                  /* RANGE and CHANGE have the same inheritability so
                     RANGE expands to absord CHANGE. */
                  range->end = change->end;
                  adjust_remaining_ranges(rangelist, &i, result_pool);
                  continue;
                }
              else
                {
                  /* RANGE and CHANGE adjoin, but have different
                     inheritability.  Since RANGE is older, just
                     move on to the next RANGE. */
                  SVN_ERR_MALFUNCTION();
                }
            }
          else
            {
              /* RANGE and CHANGE overlap, but how? */
              if ((range->inheritable == change->inheritable)
                  || range->inheritable)
                {
                  /* If CHANGE is a proper subset of RANGE, it absorbs RANGE
                      with no adjustment otherwise only the intersection is
                      absorbed and CHANGE is truncated. */
                  if (range->end >= change->end)
                    continue;
                  else
                    {
                      change->start = range->end;
                      j--;
                      continue;
                    }
                }
              else
                {
                  /* RANGE is non-inheritable and CHANGE is inheritable. */
                  if (range->start < change->start)
                    {
                      /* CHANGE absorbs intersection with RANGE and RANGE
                         is truncated. */
                      svn_merge_range_t *range_copy =
                        svn_merge_range_dup(range, result_pool);
                      range_copy->end = change->start;
                      range->start = change->start;
                      svn_sort__array_insert(rangelist, &range_copy, i++);
                      j--;
                      continue;
                    }
                  else
                    {
                      /* CHANGE and RANGE share the same start rev, but
                         RANGE is considered older because its end rev
                         is older. */
                      range->inheritable = TRUE;
                      change->start = range->end;
                      j--;
                      continue;
                    }
                }
            }
        }
      else /* res > 0, CHANGE is older than RANGE */
        {
          if (change->end == range->start)
            {
              /* RANGE and CHANGE adjoin */
              if (range->inheritable == change->inheritable)
                {
                  /* RANGE and CHANGE have the same inheritability so we
                     can simply combine the two in place. */
                  range->start = change->start;
                  continue;
                }
              else
                {
                  /* RANGE and CHANGE have different inheritability so insert
                     a copy of CHANGE into RANGELIST. */
                  SVN_ERR_MALFUNCTION(); /* Already handled */
                }
            }
          else
            {
              /* RANGE and CHANGE overlap. */
              if (range->inheritable == change->inheritable)
                {
                  /* RANGE and CHANGE have the same inheritability so we
                     can simply combine the two in place... */
                  range->start = change->start;
                  if (range->end < change->end)
                    {
                      /* ...but if RANGE is expanded ensure that we don't
                         violate any rangelist invariants. */
                      range->end = change->end;
                      adjust_remaining_ranges(rangelist, &i, result_pool);
                    }
                  continue;
                }
              else if (range->inheritable)
                {
                  if (change->start < range->start)
                    {
                      /* RANGE is inheritable so absorbs any part of CHANGE
                         it overlaps.  CHANGE is truncated and the remainder
                         inserted into RANGELIST. */
                      SVN_ERR_MALFUNCTION(); /* Already handled */
                    }
                  else
                    {
                      /* CHANGE and RANGE share the same start rev, but
                         CHANGE is considered older because CHANGE->END is
                         older than RANGE->END. */
                      continue;
                    }
                }
              else
                {
                  /* RANGE is non-inheritable and CHANGE is inheritable. */
                  if (change->start < range->start)
                    {
                      if (change->end == range->end)
                        {
                          /* RANGE is a proper subset of CHANGE and share the
                             same end revision, so set RANGE equal to CHANGE. */
                          range->start = change->start;
                          range->inheritable = TRUE;
                          continue;
                        }
                      else if (change->end > range->end)
                        {
                          /* RANGE is a proper subset of CHANGE and CHANGE has
                             a younger end revision, so set RANGE equal to its
                             intersection with CHANGE and truncate CHANGE. */
                          range->start = change->start;
                          range->inheritable = TRUE;
                          change->start = range->end;
                          j--;
                          continue;
                        }
                      else
                        {
                          /* CHANGE and RANGE overlap. Set RANGE equal to its
                             intersection with CHANGE and take the remainder
                             of RANGE and insert it into RANGELIST. */
                          svn_merge_range_t *range_copy =
                            svn_merge_range_dup(range, result_pool);
                          range_copy->start = change->end;
                          range->start = change->start;
                          range->end = change->end;
                          range->inheritable = TRUE;
                          svn_sort__array_insert(rangelist, &range_copy, ++i);
                          continue;
                        }
                    }
                  else
                    {
                      /* CHANGE and RANGE share the same start rev, but
                         CHANGE is considered older because its end rev
                         is older.

                         Insert the intersection of RANGE and CHANGE into
                         RANGELIST and then set RANGE to the non-intersecting
                         portion of RANGE. */
                      svn_merge_range_t *range_copy =
                        svn_merge_range_dup(range, result_pool);
                      range_copy->end = change->end;
                      range_copy->inheritable = TRUE;
                      range->start = change->end;
                      svn_sort__array_insert(rangelist, &range_copy, i++);
                      continue;
                    }
                }
            }
        }
      SVN_ERR_MALFUNCTION(); /* Unreachable */
    }

#ifdef SVN_DEBUG
  SVN_ERR_ASSERT(svn_rangelist__is_canonical(rangelist));
#endif

  return SVN_NO_ERROR;
}

/* Return TRUE iff the forward revision ranges FIRST and SECOND overlap and
 * (if CONSIDER_INHERITANCE is TRUE) have the same inheritability. */
static svn_boolean_t
range_intersect(const svn_merge_range_t *first, const svn_merge_range_t *second,
                svn_boolean_t consider_inheritance)
{
  SVN_ERR_ASSERT_NO_RETURN(IS_VALID_FORWARD_RANGE(first));
  SVN_ERR_ASSERT_NO_RETURN(IS_VALID_FORWARD_RANGE(second));

  return (first->start + 1 <= second->end)
    && (second->start + 1 <= first->end)
    && (!consider_inheritance
        || (!(first->inheritable) == !(second->inheritable)));
}

/* Return TRUE iff the forward revision range FIRST wholly contains the
 * forward revision range SECOND and (if CONSIDER_INHERITANCE is TRUE) has
 * the same inheritability. */
static svn_boolean_t
range_contains(const svn_merge_range_t *first, const svn_merge_range_t *second,
               svn_boolean_t consider_inheritance)
{
  SVN_ERR_ASSERT_NO_RETURN(IS_VALID_FORWARD_RANGE(first));
  SVN_ERR_ASSERT_NO_RETURN(IS_VALID_FORWARD_RANGE(second));

  return (first->start <= second->start) && (second->end <= first->end)
    && (!consider_inheritance
        || (!(first->inheritable) == !(second->inheritable)));
}

/* Swap start and end fields of RANGE. */
static void
range_swap_endpoints(svn_merge_range_t *range)
{
  svn_revnum_t swap = range->start;
  range->start = range->end;
  range->end = swap;
}

svn_error_t *
svn_rangelist_reverse(svn_rangelist_t *rangelist, apr_pool_t *pool)
{
  int i;

  svn_sort__array_reverse(rangelist, pool);

  for (i = 0; i < rangelist->nelts; i++)
    {
      range_swap_endpoints(APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *));
    }

  return SVN_NO_ERROR;
}

void
svn_rangelist__set_inheritance(svn_rangelist_t *rangelist,
                               svn_boolean_t inheritable)
{
  if (rangelist)
    {
      int i;
      svn_merge_range_t *range;

      for (i = 0; i < rangelist->nelts; i++)
        {
          range = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
          range->inheritable = inheritable;
        }
    }
  return;
}

void
svn_mergeinfo__set_inheritance(svn_mergeinfo_t mergeinfo,
                               svn_boolean_t inheritable,
                               apr_pool_t *scratch_pool)
{
  if (mergeinfo)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        {
          svn_rangelist_t *rangelist = apr_hash_this_val(hi);

          if (rangelist)
            svn_rangelist__set_inheritance(rangelist, inheritable);
        }
    }
  return;
}

/* If DO_REMOVE is true, then remove any overlapping ranges described by
   RANGELIST1 from RANGELIST2 and place the results in *OUTPUT.  When
   DO_REMOVE is true, RANGELIST1 is effectively the "eraser" and RANGELIST2
   the "whiteboard".

   If DO_REMOVE is false, then capture the intersection between RANGELIST1
   and RANGELIST2 and place the results in *OUTPUT.  The ordering of
   RANGELIST1 and RANGELIST2 doesn't matter when DO_REMOVE is false.

   If CONSIDER_INHERITANCE is true, then take the inheritance of the
   ranges in RANGELIST1 and RANGELIST2 into account when comparing them
   for intersection, see the doc string for svn_rangelist_intersect().

   If CONSIDER_INHERITANCE is false, then ranges with differing inheritance
   may intersect, but the resulting intersection is non-inheritable only
   if both ranges were non-inheritable, e.g.:

   RANGELIST1  RANGELIST2  CONSIDER     DO_REMOVE  *OUTPUT
                           INHERITANCE
   ----------  ------      -----------  ---------  -------

   90-420*     1-100       TRUE         FALSE      Empty Rangelist
   90-420      1-100*      TRUE         FALSE      Empty Rangelist
   90-420      1-100       TRUE         FALSE      90-100
   90-420*     1-100*      TRUE         FALSE      90-100*

   90-420*     1-100       FALSE        FALSE      90-100
   90-420      1-100*      FALSE        FALSE      90-100
   90-420      1-100       FALSE        FALSE      90-100
   90-420*     1-100*      FALSE        FALSE      90-100*

   Allocate the contents of *OUTPUT in POOL. */
static svn_error_t *
rangelist_intersect_or_remove(svn_rangelist_t **output,
                              const svn_rangelist_t *rangelist1,
                              const svn_rangelist_t *rangelist2,
                              svn_boolean_t do_remove,
                              svn_boolean_t consider_inheritance,
                              apr_pool_t *pool)
{
  int i1, i2, lasti2;
  svn_merge_range_t working_elt2;

  *output = apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

  i1 = 0;
  i2 = 0;
  lasti2 = -1;  /* Initialized to a value that "i2" will never be. */

  while (i1 < rangelist1->nelts && i2 < rangelist2->nelts)
    {
      svn_merge_range_t *elt1, *elt2;

      elt1 = APR_ARRAY_IDX(rangelist1, i1, svn_merge_range_t *);

      /* Instead of making a copy of the entire array of rangelist2
         elements, we just keep a copy of the current rangelist2 element
         that needs to be used, and modify our copy if necessary. */
      if (i2 != lasti2)
        {
          working_elt2 =
            *(APR_ARRAY_IDX(rangelist2, i2, svn_merge_range_t *));
          lasti2 = i2;
        }

      elt2 = &working_elt2;

      /* If the rangelist2 range is contained completely in the
         rangelist1, we increment the rangelist2.
         If the ranges intersect, and match exactly, we increment both
         rangelist1 and rangelist2.
         Otherwise, we have to generate a range for the left part of
         the removal of rangelist1 from rangelist2, and possibly change
         the rangelist2 to the remaining portion of the right part of
         the removal, to test against. */
      if (range_contains(elt1, elt2, consider_inheritance))
        {
          if (!do_remove)
            {
              svn_merge_range_t tmp_range;
              tmp_range.start = elt2->start;
              tmp_range.end = elt2->end;
              /* The intersection of two ranges is non-inheritable only
                 if both ranges are non-inheritable. */
              tmp_range.inheritable =
                (elt2->inheritable || elt1->inheritable);
              SVN_ERR(combine_with_lastrange(&tmp_range, *output,
                                             consider_inheritance,
                                             pool));
            }

          i2++;

          if (elt2->start == elt1->start && elt2->end == elt1->end)
            i1++;
        }
      else if (range_intersect(elt1, elt2, consider_inheritance))
        {
          if (elt2->start < elt1->start)
            {
              /* The rangelist2 range starts before the rangelist1 range. */
              svn_merge_range_t tmp_range;
              if (do_remove)
                {
                  /* Retain the range that falls before the rangelist1
                     start. */
                  tmp_range.start = elt2->start;
                  tmp_range.end = elt1->start;
                  tmp_range.inheritable = elt2->inheritable;
                }
              else
                {
                  /* Retain the range that falls between the rangelist1
                     start and rangelist2 end. */
                  tmp_range.start = elt1->start;
                  tmp_range.end = MIN(elt2->end, elt1->end);
                  /* The intersection of two ranges is non-inheritable only
                     if both ranges are non-inheritable. */
                  tmp_range.inheritable =
                    (elt2->inheritable || elt1->inheritable);
                }

              SVN_ERR(combine_with_lastrange(&tmp_range,
                                             *output, consider_inheritance,
                                             pool));
            }

          /* Set up the rest of the rangelist2 range for further
             processing.  */
          if (elt2->end > elt1->end)
            {
              /* The rangelist2 range ends after the rangelist1 range. */
              if (!do_remove)
                {
                  /* Partial overlap. */
                  svn_merge_range_t tmp_range;
                  tmp_range.start = MAX(elt2->start, elt1->start);
                  tmp_range.end = elt1->end;
                  /* The intersection of two ranges is non-inheritable only
                     if both ranges are non-inheritable. */
                  tmp_range.inheritable =
                    (elt2->inheritable || elt1->inheritable);
                  SVN_ERR(combine_with_lastrange(&tmp_range,
                                                 *output,
                                                 consider_inheritance,
                                                 pool));
                }

              working_elt2.start = elt1->end;
              working_elt2.end = elt2->end;
            }
          else
            i2++;
        }
      else  /* ranges don't intersect */
        {
          /* See which side of the rangelist2 the rangelist1 is on.  If it
             is on the left side, we need to move the rangelist1.

             If it is on past the rangelist2 on the right side, we
             need to output the rangelist2 and increment the
             rangelist2.  */
          if (svn_sort_compare_ranges(&elt1, &elt2) < 0)
            i1++;
          else
            {
              svn_merge_range_t *lastrange;

              if ((*output)->nelts > 0)
                lastrange = APR_ARRAY_IDX(*output, (*output)->nelts - 1,
                                          svn_merge_range_t *);
              else
                lastrange = NULL;

              if (do_remove && !(lastrange &&
                                 combine_ranges(lastrange, lastrange, elt2,
                                                consider_inheritance)))
                {
                  lastrange = svn_merge_range_dup(elt2, pool);
                  APR_ARRAY_PUSH(*output, svn_merge_range_t *) = lastrange;
                }
              i2++;
            }
        }
    }

  if (do_remove)
    {
      /* Copy the current rangelist2 element if we didn't hit the end
         of the rangelist2, and we still had it around.  This element
         may have been touched, so we can't just walk the rangelist2
         array, we have to use our copy.  This case only happens when
         we ran out of rangelist1 before rangelist2, *and* we had changed
         the rangelist2 element. */
      if (i2 == lasti2 && i2 < rangelist2->nelts)
        {
          SVN_ERR(combine_with_lastrange(&working_elt2, *output,
                                         consider_inheritance, pool));
          i2++;
        }

      /* Copy any other remaining untouched rangelist2 elements.  */
      for (; i2 < rangelist2->nelts; i2++)
        {
          svn_merge_range_t *elt = APR_ARRAY_IDX(rangelist2, i2,
                                                 svn_merge_range_t *);

          SVN_ERR(combine_with_lastrange(elt, *output,
                                         consider_inheritance, pool));
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_rangelist_intersect(svn_rangelist_t **output,
                        const svn_rangelist_t *rangelist1,
                        const svn_rangelist_t *rangelist2,
                        svn_boolean_t consider_inheritance,
                        apr_pool_t *pool)
{
  return rangelist_intersect_or_remove(output, rangelist1, rangelist2, FALSE,
                                       consider_inheritance, pool);
}

svn_error_t *
svn_rangelist_remove(svn_rangelist_t **output,
                     const svn_rangelist_t *eraser,
                     const svn_rangelist_t *whiteboard,
                     svn_boolean_t consider_inheritance,
                     apr_pool_t *pool)
{
  return rangelist_intersect_or_remove(output, eraser, whiteboard, TRUE,
                                       consider_inheritance, pool);
}

svn_error_t *
svn_rangelist_diff(svn_rangelist_t **deleted, svn_rangelist_t **added,
                   const svn_rangelist_t *from, const svn_rangelist_t *to,
                   svn_boolean_t consider_inheritance,
                   apr_pool_t *pool)
{
  /* The following diagrams illustrate some common range delta scenarios:

     (from)           deleted
     r0 <===========(=========)============[=========]===========> rHEAD
     [to]                                    added

     (from)           deleted                deleted
     r0 <===========(=========[============]=========)===========> rHEAD
     [to]

     (from)           deleted
     r0 <===========(=========[============)=========]===========> rHEAD
     [to]                                    added

     (from)                                  deleted
     r0 <===========[=========(============]=========)===========> rHEAD
     [to]             added

     (from)
     r0 <===========[=========(============)=========]===========> rHEAD
     [to]             added                  added

     (from)  d                                  d             d
     r0 <===(=[=)=]=[==]=[=(=)=]=[=]=[=(===|===(=)==|=|==[=(=]=)=> rHEAD
     [to]        a   a    a   a   a   a                   a
  */

  /* The items that are present in from, but not in to, must have been
     deleted. */
  SVN_ERR(svn_rangelist_remove(deleted, to, from, consider_inheritance,
                               pool));
  /* The items that are present in to, but not in from, must have been
     added.  */
  return svn_rangelist_remove(added, from, to, consider_inheritance, pool);
}

struct mergeinfo_diff_baton
{
  svn_mergeinfo_t from;
  svn_mergeinfo_t to;
  svn_mergeinfo_t deleted;
  svn_mergeinfo_t added;
  svn_boolean_t consider_inheritance;
  apr_pool_t *pool;
};

/* This implements the 'svn_hash_diff_func_t' interface.
   BATON is of type 'struct mergeinfo_diff_baton *'.
*/
static svn_error_t *
mergeinfo_hash_diff_cb(const void *key, apr_ssize_t klen,
                       enum svn_hash_diff_key_status status,
                       void *baton)
{
  /* hash_a is FROM mergeinfo,
     hash_b is TO mergeinfo. */
  struct mergeinfo_diff_baton *cb = baton;
  svn_rangelist_t *from_rangelist, *to_rangelist;
  const char *path = key;
  if (status == svn_hash_diff_key_both)
    {
      /* Record any deltas (additions or deletions). */
      svn_rangelist_t *deleted_rangelist, *added_rangelist;
      from_rangelist = apr_hash_get(cb->from, path, klen);
      to_rangelist = apr_hash_get(cb->to, path, klen);
      SVN_ERR(svn_rangelist_diff(&deleted_rangelist, &added_rangelist,
                                 from_rangelist, to_rangelist,
                                 cb->consider_inheritance, cb->pool));
      if (cb->deleted && deleted_rangelist->nelts > 0)
        apr_hash_set(cb->deleted, apr_pstrmemdup(cb->pool, path, klen),
                     klen, deleted_rangelist);
      if (cb->added && added_rangelist->nelts > 0)
        apr_hash_set(cb->added, apr_pstrmemdup(cb->pool, path, klen),
                     klen, added_rangelist);
    }
  else if ((status == svn_hash_diff_key_a) && cb->deleted)
    {
      from_rangelist = apr_hash_get(cb->from, path, klen);
      apr_hash_set(cb->deleted, apr_pstrmemdup(cb->pool, path, klen), klen,
                   svn_rangelist_dup(from_rangelist, cb->pool));
    }
  else if ((status == svn_hash_diff_key_b) && cb->added)
    {
      to_rangelist = apr_hash_get(cb->to, path, klen);
      apr_hash_set(cb->added, apr_pstrmemdup(cb->pool, path, klen), klen,
                   svn_rangelist_dup(to_rangelist, cb->pool));
    }
  return SVN_NO_ERROR;
}

/* Record deletions and additions of entire range lists (by path
   presence), and delegate to svn_rangelist_diff() for delta
   calculations on a specific path.  */
static svn_error_t *
walk_mergeinfo_hash_for_diff(svn_mergeinfo_t from, svn_mergeinfo_t to,
                             svn_mergeinfo_t deleted, svn_mergeinfo_t added,
                             svn_boolean_t consider_inheritance,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  struct mergeinfo_diff_baton mdb;
  mdb.from = from;
  mdb.to = to;
  mdb.deleted = deleted;
  mdb.added = added;
  mdb.consider_inheritance = consider_inheritance;
  mdb.pool = result_pool;

  return svn_hash_diff(from, to, mergeinfo_hash_diff_cb, &mdb, scratch_pool);
}

svn_error_t *
svn_mergeinfo_diff2(svn_mergeinfo_t *deleted, svn_mergeinfo_t *added,
                    svn_mergeinfo_t from, svn_mergeinfo_t to,
                    svn_boolean_t consider_inheritance,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  if (from && to == NULL)
    {
      *deleted = svn_mergeinfo_dup(from, result_pool);
      *added = svn_hash__make(result_pool);
    }
  else if (from == NULL && to)
    {
      *deleted = svn_hash__make(result_pool);
      *added = svn_mergeinfo_dup(to, result_pool);
    }
  else
    {
      *deleted = svn_hash__make(result_pool);
      *added = svn_hash__make(result_pool);

      if (from && to)
        {
          SVN_ERR(walk_mergeinfo_hash_for_diff(from, to, *deleted, *added,
                                               consider_inheritance,
                                               result_pool, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__equals(svn_boolean_t *is_equal,
                      svn_mergeinfo_t info1,
                      svn_mergeinfo_t info2,
                      svn_boolean_t consider_inheritance,
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *is_equal = FALSE;

  /* special cases: at least one side has no merge info */
  if (info1 == NULL && info2 == NULL)
    {
      *is_equal = TRUE;
      return SVN_NO_ERROR;
    }

  if (info1 == NULL ||  info2 == NULL)
    return SVN_NO_ERROR;

  /* trivial case: different number of paths -> unequal */
  if (apr_hash_count(info1) != apr_hash_count(info2))
    return SVN_NO_ERROR;

  /* compare range lists for all paths */
  for (hi = apr_hash_first(pool, info1); hi; hi = apr_hash_next(hi))
    {
      const char *key;
      apr_ssize_t key_length;
      svn_rangelist_t *lhs, *rhs;
      int i;
      svn_rangelist_t *deleted, *added;

      /* get both path lists */
      apr_hash_this(hi, (const void**)&key, &key_length, (void **)&lhs);
      rhs = apr_hash_get(info2, key, key_length);

      /* missing on one side? */
      if (rhs == NULL)
        return SVN_NO_ERROR;

      /* quick compare: the range lists will often be a perfect match */
      if (lhs->nelts == rhs->nelts)
        {
          for (i = 0; i < lhs->nelts; ++i)
            {
              svn_merge_range_t *lrange
                = APR_ARRAY_IDX(lhs, i, svn_merge_range_t *);
              svn_merge_range_t *rrange
                = APR_ARRAY_IDX(rhs, i, svn_merge_range_t *);

              /* range mismatch? -> needs detailed comparison */
              if (   lrange->start != rrange->start
                  || lrange->end != rrange->end)
                break;

              /* inheritance mismatch? -> merge info differs */
              if (   consider_inheritance
                  && lrange->inheritable != rrange->inheritable)
                return SVN_NO_ERROR;
            }

          /* all ranges found to match -> next path */
          if (i == lhs->nelts)
            continue;
        }

      /* range lists differ but there are many ways to sort and aggregate
         revisions into ranges. Do a full diff on them. */
      SVN_ERR(svn_rangelist_diff(&deleted, &added, lhs, rhs,
                                  consider_inheritance, pool));
      if (deleted->nelts || added->nelts)
        return SVN_NO_ERROR;
    }

  /* no mismatch found */
  *is_equal = TRUE;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_merge2(svn_mergeinfo_t mergeinfo,
                     svn_mergeinfo_t changes,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  if (!apr_hash_count(changes))
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, changes); hi; hi = apr_hash_next(hi))
    {
      const char *key;
      apr_ssize_t klen;
      svn_rangelist_t *to_insert;
      svn_rangelist_t *target;

      /* get ranges to insert and the target ranges list of that insertion */
      apr_hash_this(hi, (const void**)&key, &klen, (void*)&to_insert);
      target = apr_hash_get(mergeinfo, key, klen);

      /* if range list exists, just expand on it.
       * Otherwise, add new hash entry. */
      if (target)
        {
          SVN_ERR(svn_rangelist_merge2(target, to_insert, result_pool,
                                       iterpool));
          svn_pool_clear(iterpool);
        }
      else
        apr_hash_set(mergeinfo, key, klen, to_insert);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_catalog_merge(svn_mergeinfo_catalog_t mergeinfo_cat,
                            svn_mergeinfo_catalog_t changes_cat,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  int i = 0;
  int j = 0;
  apr_array_header_t *sorted_cat =
    svn_sort__hash(mergeinfo_cat, svn_sort_compare_items_as_paths,
                   scratch_pool);
  apr_array_header_t *sorted_changes =
    svn_sort__hash(changes_cat, svn_sort_compare_items_as_paths,
                   scratch_pool);

  while (i < sorted_cat->nelts && j < sorted_changes->nelts)
    {
      svn_sort__item_t cat_elt, change_elt;
      int res;

      cat_elt = APR_ARRAY_IDX(sorted_cat, i, svn_sort__item_t);
      change_elt = APR_ARRAY_IDX(sorted_changes, j, svn_sort__item_t);
      res = svn_sort_compare_items_as_paths(&cat_elt, &change_elt);

      if (res == 0) /* Both catalogs have mergeinfo for a given path. */
        {
          svn_mergeinfo_t mergeinfo = cat_elt.value;
          svn_mergeinfo_t changes_mergeinfo = change_elt.value;

          SVN_ERR(svn_mergeinfo_merge2(mergeinfo, changes_mergeinfo,
                                       result_pool, scratch_pool));
          apr_hash_set(mergeinfo_cat, cat_elt.key, cat_elt.klen, mergeinfo);
          i++;
          j++;
        }
      else if (res < 0) /* Only MERGEINFO_CAT has mergeinfo for this path. */
        {
          i++;
        }
      else /* Only CHANGES_CAT has mergeinfo for this path. */
        {
          apr_hash_set(mergeinfo_cat,
                       apr_pstrdup(result_pool, change_elt.key),
                       change_elt.klen,
                       svn_mergeinfo_dup(change_elt.value, result_pool));
          j++;
        }
    }

  /* Copy back any remaining elements from the CHANGES_CAT catalog. */
  for (; j < sorted_changes->nelts; j++)
    {
      svn_sort__item_t elt = APR_ARRAY_IDX(sorted_changes, j,
                                           svn_sort__item_t);
      apr_hash_set(mergeinfo_cat,
                   apr_pstrdup(result_pool, elt.key),
                   elt.klen,
                   svn_mergeinfo_dup(elt.value, result_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_intersect2(svn_mergeinfo_t *mergeinfo,
                         svn_mergeinfo_t mergeinfo1,
                         svn_mergeinfo_t mergeinfo2,
                         svn_boolean_t consider_inheritance,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  *mergeinfo = apr_hash_make(result_pool);
  iterpool = svn_pool_create(scratch_pool);

  /* ### TODO(reint): Do we care about the case when a path in one
     ### mergeinfo hash has inheritable mergeinfo, and in the other
     ### has non-inheritable mergeinfo?  It seems like that path
     ### itself should really be an intersection, while child paths
     ### should not be... */
  for (hi = apr_hash_first(scratch_pool, mergeinfo1);
       hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_rangelist_t *rangelist1 = apr_hash_this_val(hi);
      svn_rangelist_t *rangelist2;

      svn_pool_clear(iterpool);
      rangelist2 = svn_hash_gets(mergeinfo2, path);
      if (rangelist2)
        {
          SVN_ERR(svn_rangelist_intersect(&rangelist2, rangelist1, rangelist2,
                                          consider_inheritance, iterpool));
          if (rangelist2->nelts > 0)
            svn_hash_sets(*mergeinfo, apr_pstrdup(result_pool, path),
                          svn_rangelist_dup(rangelist2, result_pool));
        }
    }
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_remove2(svn_mergeinfo_t *mergeinfo,
                      svn_mergeinfo_t eraser,
                      svn_mergeinfo_t whiteboard,
                      svn_boolean_t consider_inheritance,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  *mergeinfo = apr_hash_make(result_pool);
  return walk_mergeinfo_hash_for_diff(whiteboard, eraser, *mergeinfo, NULL,
                                      consider_inheritance, result_pool,
                                      scratch_pool);
}

svn_error_t *
svn_rangelist_to_string(svn_string_t **output,
                        const svn_rangelist_t *rangelist,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create_empty(pool);

  if (rangelist->nelts > 0)
    {
      int i;
      svn_merge_range_t *range;

      /* Handle the elements that need commas at the end.  */
      for (i = 0; i < rangelist->nelts - 1; i++)
        {
          range = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
          svn_stringbuf_appendcstr(buf, range_to_string(range, pool));
          svn_stringbuf_appendcstr(buf, ",");
        }

      /* Now handle the last element, which needs no comma.  */
      range = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
      svn_stringbuf_appendcstr(buf, range_to_string(range, pool));
    }

  *output = svn_stringbuf__morph_into_string(buf);

  return SVN_NO_ERROR;
}

/* Converts a mergeinfo INPUT to an unparsed mergeinfo in OUTPUT.  If PREFIX
   is not NULL then prepend PREFIX to each line in OUTPUT.  If INPUT contains
   no elements, return the empty string.  If INPUT contains any merge source
   path keys that are relative then convert these to absolute paths in
   *OUTPUT.
 */
static svn_error_t *
mergeinfo_to_stringbuf(svn_stringbuf_t **output,
                       svn_mergeinfo_t input,
                       const char *prefix,
                       apr_pool_t *pool)
{
  *output = svn_stringbuf_create_empty(pool);

  if (apr_hash_count(input) > 0)
    {
      apr_array_header_t *sorted =
        svn_sort__hash(input, svn_sort_compare_items_as_paths, pool);
      int i;

      for (i = 0; i < sorted->nelts; i++)
        {
          svn_sort__item_t elt = APR_ARRAY_IDX(sorted, i, svn_sort__item_t);
          svn_string_t *revlist;

          SVN_ERR(svn_rangelist_to_string(&revlist, elt.value, pool));
          svn_stringbuf_appendcstr(
            *output,
            apr_psprintf(pool, "%s%s%s:%s",
                         prefix ? prefix : "",
                         *((const char *) elt.key) == '/' ? "" : "/",
                         (const char *) elt.key,
                         revlist->data));
          if (i < sorted->nelts - 1)
            svn_stringbuf_appendcstr(*output, "\n");
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_to_string(svn_string_t **output, svn_mergeinfo_t input,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *mergeinfo_buf;

  SVN_ERR(mergeinfo_to_stringbuf(&mergeinfo_buf, input, NULL, pool));
  *output = svn_stringbuf__morph_into_string(mergeinfo_buf);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_sort(svn_mergeinfo_t input, apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, input); hi; hi = apr_hash_next(hi))
    {
      apr_array_header_t *rl = apr_hash_this_val(hi);

      svn_sort__array(rl, svn_sort_compare_ranges);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__canonicalize_ranges(svn_mergeinfo_t mergeinfo,
                                   apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, mergeinfo); hi; hi = apr_hash_next(hi))
    {
      apr_array_header_t *rl = apr_hash_this_val(hi);

      SVN_ERR(svn_rangelist__canonicalize(rl, scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_mergeinfo_catalog_t
svn_mergeinfo_catalog_dup(svn_mergeinfo_catalog_t mergeinfo_catalog,
                          apr_pool_t *pool)
{
  svn_mergeinfo_t new_mergeinfo_catalog = apr_hash_make(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, mergeinfo_catalog);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *key = apr_hash_this_key(hi);
      svn_mergeinfo_t val = apr_hash_this_val(hi);

      svn_hash_sets(new_mergeinfo_catalog, apr_pstrdup(pool, key),
                    svn_mergeinfo_dup(val, pool));
    }

  return new_mergeinfo_catalog;
}

svn_mergeinfo_t
svn_mergeinfo_dup(svn_mergeinfo_t mergeinfo, apr_pool_t *pool)
{
  svn_mergeinfo_t new_mergeinfo = svn_hash__make(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      apr_ssize_t pathlen = apr_hash_this_key_len(hi);
      svn_rangelist_t *rangelist = apr_hash_this_val(hi);

      apr_hash_set(new_mergeinfo, apr_pstrmemdup(pool, path, pathlen), pathlen,
                   svn_rangelist_dup(rangelist, pool));
    }

  return new_mergeinfo;
}

svn_error_t *
svn_mergeinfo_inheritable2(svn_mergeinfo_t *output,
                           svn_mergeinfo_t mergeinfo,
                           const char *path,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t inheritable,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_mergeinfo_t inheritable_mergeinfo = apr_hash_make(result_pool);

  for (hi = apr_hash_first(scratch_pool, mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *key = apr_hash_this_key(hi);
      apr_ssize_t keylen = apr_hash_this_key_len(hi);
      svn_rangelist_t *rangelist = apr_hash_this_val(hi);
      svn_rangelist_t *inheritable_rangelist;

      if (!path || svn_path_compare_paths(path, key) == 0)
        SVN_ERR(svn_rangelist_inheritable2(&inheritable_rangelist, rangelist,
                                           start, end, inheritable,
                                           result_pool, scratch_pool));
      else
        inheritable_rangelist = svn_rangelist_dup(rangelist, result_pool);

      /* Only add this rangelist if some ranges remain.  A rangelist with
         a path mapped to an empty rangelist is not syntactically valid */
      if (inheritable_rangelist->nelts)
        apr_hash_set(inheritable_mergeinfo,
                     apr_pstrmemdup(result_pool, key, keylen), keylen,
                     inheritable_rangelist);
    }
  *output = inheritable_mergeinfo;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_rangelist_inheritable2(svn_rangelist_t **inheritable_rangelist,
                           const svn_rangelist_t *rangelist,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t inheritable,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  *inheritable_rangelist = apr_array_make(result_pool, 1,
                                          sizeof(svn_merge_range_t *));
  if (rangelist->nelts)
    {
      if (!SVN_IS_VALID_REVNUM(start)
          || !SVN_IS_VALID_REVNUM(end)
          || end < start)
        {
          /* We want all (non-inheritable or inheritable) ranges removed. */
          int i;

          for (i = 0; i < rangelist->nelts; i++)
            {
              svn_merge_range_t *range = APR_ARRAY_IDX(rangelist, i,
                                                       svn_merge_range_t *);
              if (range->inheritable == inheritable)
                {
                  APR_ARRAY_PUSH(*inheritable_rangelist, svn_merge_range_t *)
                    = svn_merge_range_dup(range, result_pool);
                }
            }
        }
      else
        {
          /* We want only the (non-inheritable or inheritable) ranges
             bound by START and END removed. */
          svn_rangelist_t *ranges_inheritable =
            svn_rangelist__initialize(start, end, inheritable, scratch_pool);

          if (rangelist->nelts)
            SVN_ERR(svn_rangelist_remove(inheritable_rangelist,
                                         ranges_inheritable,
                                         rangelist,
                                         TRUE,
                                         result_pool));
        }
    }
  return SVN_NO_ERROR;
}

svn_boolean_t
svn_mergeinfo__remove_empty_rangelists(svn_mergeinfo_t mergeinfo,
                                       apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_boolean_t removed_some_ranges = FALSE;

  if (mergeinfo)
    {
      for (hi = apr_hash_first(scratch_pool, mergeinfo); hi;
           hi = apr_hash_next(hi))
        {
          const char *path = apr_hash_this_key(hi);
          svn_rangelist_t *rangelist = apr_hash_this_val(hi);

          if (rangelist->nelts == 0)
            {
              svn_hash_sets(mergeinfo, path, NULL);
              removed_some_ranges = TRUE;
            }
        }
    }
  return removed_some_ranges;
}

svn_error_t *
svn_mergeinfo__remove_prefix_from_catalog(svn_mergeinfo_catalog_t *out_catalog,
                                          svn_mergeinfo_catalog_t in_catalog,
                                          const char *prefix_path,
                                          apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(prefix_path[0] == '/');

  *out_catalog = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, in_catalog); hi; hi = apr_hash_next(hi))
    {
      const char *original_path = apr_hash_this_key(hi);
      svn_mergeinfo_t value = apr_hash_this_val(hi);
      const char *new_path;

      new_path = svn_fspath__skip_ancestor(prefix_path, original_path);
      SVN_ERR_ASSERT(new_path);

      svn_hash_sets(*out_catalog, new_path, value);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__add_prefix_to_catalog(svn_mergeinfo_catalog_t *out_catalog,
                                     svn_mergeinfo_catalog_t in_catalog,
                                     const char *prefix_path,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  *out_catalog = apr_hash_make(result_pool);

  for (hi = apr_hash_first(scratch_pool, in_catalog);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *original_path = apr_hash_this_key(hi);
      svn_mergeinfo_t value = apr_hash_this_val(hi);

      if (original_path[0] == '/')
        original_path++;

      svn_hash_sets(*out_catalog,
                    svn_dirent_join(prefix_path, original_path, result_pool),
                    value);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__add_suffix_to_mergeinfo(svn_mergeinfo_t *out_mergeinfo,
                                       svn_mergeinfo_t mergeinfo,
                                       const char *suffix_relpath,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(suffix_relpath && svn_relpath_is_canonical(suffix_relpath));

  *out_mergeinfo = apr_hash_make(result_pool);

  for (hi = apr_hash_first(scratch_pool, mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *fspath = apr_hash_this_key(hi);
      svn_rangelist_t *rangelist = apr_hash_this_val(hi);

      svn_hash_sets(*out_mergeinfo,
                    svn_fspath__join(fspath, suffix_relpath, result_pool),
                    rangelist);
    }

  return SVN_NO_ERROR;
}

/* Deep-copy an array of pointers to simple objects.
 *
 * Return a duplicate in POOL of the array ARRAY of pointers to objects
 * of size OBJECT_SIZE bytes. Duplicate each object bytewise.
 */
static apr_array_header_t *
ptr_array_dup(const apr_array_header_t *array,
              size_t object_size,
              apr_pool_t *pool)
{
  apr_array_header_t *new_array = apr_array_make(pool, array->nelts,
                                                 sizeof(void *));

  /* allocate target range buffer with a single operation */
  char *copy = apr_palloc(pool, object_size * array->nelts);

  /* for efficiency, directly address source and target reference buffers */
  void **source = (void **)(array->elts);
  void **target = (void **)(new_array->elts);
  int i;

  /* copy ranges iteratively and link them into the target range list */
  for (i = 0; i < array->nelts; i++)
    {
      target[i] = &copy[i * object_size];
      memcpy(target[i], source[i], object_size);
    }
  new_array->nelts = array->nelts;

  return new_array;
}

svn_rangelist_t *
svn_rangelist_dup(const svn_rangelist_t *rangelist, apr_pool_t *pool)
{
  return ptr_array_dup(rangelist, sizeof(svn_merge_range_t), pool);
}

svn_merge_range_t *
svn_merge_range_dup(const svn_merge_range_t *range, apr_pool_t *pool)
{
  svn_merge_range_t *new_range = apr_pmemdup(pool, range, sizeof(*new_range));
  return new_range;
}

svn_boolean_t
svn_merge_range_contains_rev(const svn_merge_range_t *range, svn_revnum_t rev)
{
  assert(SVN_IS_VALID_REVNUM(range->start));
  assert(SVN_IS_VALID_REVNUM(range->end));
  assert(range->start != range->end);

  if (range->start < range->end)
    return rev > range->start && rev <= range->end;
  else
    return rev > range->end && rev <= range->start;
}

svn_error_t *
svn_mergeinfo__catalog_to_formatted_string(svn_string_t **output,
                                           svn_mergeinfo_catalog_t catalog,
                                           const char *key_prefix,
                                           const char *val_prefix,
                                           apr_pool_t *pool)
{
  svn_stringbuf_t *output_buf = NULL;

  if (catalog && apr_hash_count(catalog))
    {
      int i;
      apr_array_header_t *sorted_catalog =
        svn_sort__hash(catalog, svn_sort_compare_items_as_paths, pool);

      output_buf = svn_stringbuf_create_empty(pool);
      for (i = 0; i < sorted_catalog->nelts; i++)
        {
          svn_sort__item_t elt =
            APR_ARRAY_IDX(sorted_catalog, i, svn_sort__item_t);
          const char *path1;
          svn_mergeinfo_t mergeinfo;
          svn_stringbuf_t *mergeinfo_output_buf;

          path1 = elt.key;
          mergeinfo = elt.value;
          if (key_prefix)
            svn_stringbuf_appendcstr(output_buf, key_prefix);
          svn_stringbuf_appendcstr(output_buf, path1);
          svn_stringbuf_appendcstr(output_buf, "\n");
          SVN_ERR(mergeinfo_to_stringbuf(&mergeinfo_output_buf, mergeinfo,
                                         val_prefix ? val_prefix : "", pool));
          svn_stringbuf_appendstr(output_buf, mergeinfo_output_buf);
          svn_stringbuf_appendcstr(output_buf, "\n");
        }
    }
#ifdef SVN_DEBUG
  else if (!catalog)
    {
      output_buf = svn_stringbuf_create(key_prefix ? key_prefix : "", pool);
      svn_stringbuf_appendcstr(output_buf, _("NULL mergeinfo catalog\n"));
    }
  else if (apr_hash_count(catalog) == 0)
    {
      output_buf = svn_stringbuf_create(key_prefix ? key_prefix : "", pool);
      svn_stringbuf_appendcstr(output_buf, _("empty mergeinfo catalog\n"));
    }
#endif

  /* If we have an output_buf, convert it to an svn_string_t;
     otherwise, return a new string containing only a newline
     character.  */
  if (output_buf)
    *output = svn_stringbuf__morph_into_string(output_buf);
  else
    *output = svn_string_create("\n", pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__get_range_endpoints(svn_revnum_t *youngest_rev,
                                   svn_revnum_t *oldest_rev,
                                   svn_mergeinfo_t mergeinfo,
                                   apr_pool_t *pool)
{
  *youngest_rev = *oldest_rev = SVN_INVALID_REVNUM;
  if (mergeinfo)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi))
        {
          svn_rangelist_t *rangelist = apr_hash_this_val(hi);

          if (rangelist->nelts)
            {
              svn_merge_range_t *range = APR_ARRAY_IDX(rangelist,
                                                       rangelist->nelts - 1,
                                                       svn_merge_range_t *);
              if (!SVN_IS_VALID_REVNUM(*youngest_rev)
                  || (range->end > *youngest_rev))
                *youngest_rev = range->end;

              range = APR_ARRAY_IDX(rangelist, 0, svn_merge_range_t *);
              if (!SVN_IS_VALID_REVNUM(*oldest_rev)
                  || (range->start < *oldest_rev))
                *oldest_rev = range->start;
            }
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__filter_catalog_by_ranges(svn_mergeinfo_catalog_t *filtered_cat,
                                        svn_mergeinfo_catalog_t catalog,
                                        svn_revnum_t youngest_rev,
                                        svn_revnum_t oldest_rev,
                                        svn_boolean_t include_range,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  *filtered_cat = apr_hash_make(result_pool);
  for (hi = apr_hash_first(scratch_pool, catalog);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_mergeinfo_t mergeinfo = apr_hash_this_val(hi);
      svn_mergeinfo_t filtered_mergeinfo;

      SVN_ERR(svn_mergeinfo__filter_mergeinfo_by_ranges(&filtered_mergeinfo,
                                                        mergeinfo,
                                                        youngest_rev,
                                                        oldest_rev,
                                                        include_range,
                                                        result_pool,
                                                        scratch_pool));
      if (apr_hash_count(filtered_mergeinfo))
        svn_hash_sets(*filtered_cat,
                      apr_pstrdup(result_pool, path), filtered_mergeinfo);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__filter_mergeinfo_by_ranges(svn_mergeinfo_t *filtered_mergeinfo,
                                          svn_mergeinfo_t mergeinfo,
                                          svn_revnum_t youngest_rev,
                                          svn_revnum_t oldest_rev,
                                          svn_boolean_t include_range,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(oldest_rev));
  SVN_ERR_ASSERT(oldest_rev < youngest_rev);

  *filtered_mergeinfo = apr_hash_make(result_pool);

  if (mergeinfo)
    {
      apr_hash_index_t *hi;
      svn_rangelist_t *filter_rangelist =
        svn_rangelist__initialize(oldest_rev, youngest_rev, TRUE,
                                  scratch_pool);

      for (hi = apr_hash_first(scratch_pool, mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *path = apr_hash_this_key(hi);
          svn_rangelist_t *rangelist = apr_hash_this_val(hi);

          if (rangelist->nelts)
            {
              svn_rangelist_t *new_rangelist;

              SVN_ERR(rangelist_intersect_or_remove(
                        &new_rangelist, filter_rangelist, rangelist,
                        ! include_range, FALSE, result_pool));

              if (new_rangelist->nelts)
                svn_hash_sets(*filtered_mergeinfo,
                              apr_pstrdup(result_pool, path), new_rangelist);
            }
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__adjust_mergeinfo_rangelists(svn_mergeinfo_t *adjusted_mergeinfo,
                                           svn_mergeinfo_t mergeinfo,
                                           svn_revnum_t offset,
                                           apr_pool_t *result_pool,
                                           apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  *adjusted_mergeinfo = apr_hash_make(result_pool);

  if (mergeinfo)
    {
      for (hi = apr_hash_first(scratch_pool, mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        {
          int i;
          const char *path = apr_hash_this_key(hi);
          svn_rangelist_t *rangelist = apr_hash_this_val(hi);
          svn_rangelist_t *adjusted_rangelist =
            apr_array_make(result_pool, rangelist->nelts,
                           sizeof(svn_merge_range_t *));

          for (i = 0; i < rangelist->nelts; i++)
            {
              svn_merge_range_t *range =
                APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);

              if (range->start + offset > 0 && range->end + offset > 0)
                {
                  range->start = range->start + offset;
                  range->end = range->end + offset;
                  APR_ARRAY_PUSH(adjusted_rangelist, svn_merge_range_t *) =
                    range;
                }
            }

          if (adjusted_rangelist->nelts)
            svn_hash_sets(*adjusted_mergeinfo, apr_pstrdup(result_pool, path),
                          adjusted_rangelist);
        }
    }
  return SVN_NO_ERROR;
}

svn_boolean_t
svn_mergeinfo__is_noninheritable(svn_mergeinfo_t mergeinfo,
                                 apr_pool_t *scratch_pool)
{
  if (mergeinfo)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        {
          svn_rangelist_t *rangelist = apr_hash_this_val(hi);
          int i;

          for (i = 0; i < rangelist->nelts; i++)
            {
              svn_merge_range_t *range = APR_ARRAY_IDX(rangelist, i,
                                                       svn_merge_range_t *);
              if (!range->inheritable)
                return TRUE;
            }
        }
    }
  return FALSE;
}

svn_rangelist_t *
svn_rangelist__initialize(svn_revnum_t start,
                          svn_revnum_t end,
                          svn_boolean_t inheritable,
                          apr_pool_t *result_pool)
{
  svn_rangelist_t *rangelist =
    apr_array_make(result_pool, 1, sizeof(svn_merge_range_t *));
  svn_merge_range_t *range = apr_pcalloc(result_pool, sizeof(*range));

  range->start = start;
  range->end = end;
  range->inheritable = inheritable;
  APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = range;
  return rangelist;
}

svn_error_t *
svn_mergeinfo__mergeinfo_from_segments(svn_mergeinfo_t *mergeinfo_p,
                                       const apr_array_header_t *segments,
                                       apr_pool_t *pool)
{
  svn_mergeinfo_t mergeinfo = apr_hash_make(pool);
  int i;

  /* Translate location segments into merge sources and ranges. */
  for (i = 0; i < segments->nelts; i++)
    {
      svn_location_segment_t *segment =
        APR_ARRAY_IDX(segments, i, svn_location_segment_t *);
      svn_rangelist_t *path_ranges;
      svn_merge_range_t *range;
      const char *source_path;

      /* No path segment?  Skip it. */
      if (! segment->path)
        continue;

      /* Prepend a leading slash to our path. */
      source_path = apr_pstrcat(pool, "/", segment->path, SVN_VA_NULL);

      /* See if we already stored ranges for this path.  If not, make
         a new list.  */
      path_ranges = svn_hash_gets(mergeinfo, source_path);
      if (! path_ranges)
        path_ranges = apr_array_make(pool, 1, sizeof(range));

      /* A svn_location_segment_t may have legitimately describe only
         revision 0, but there is no corresponding representation for
         this in a svn_merge_range_t. */
      if (segment->range_start == 0 && segment->range_end == 0)
        continue;

      /* Build a merge range, push it onto the list of ranges, and for
         good measure, (re)store it in the hash. */
      range = apr_pcalloc(pool, sizeof(*range));
      range->start = MAX(segment->range_start - 1, 0);
      range->end = segment->range_end;
      range->inheritable = TRUE;
      APR_ARRAY_PUSH(path_ranges, svn_merge_range_t *) = range;
      svn_hash_sets(mergeinfo, source_path, path_ranges);
    }

  *mergeinfo_p = mergeinfo;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_rangelist__merge_many(svn_rangelist_t *merged_rangelist,
                          svn_mergeinfo_t merge_history,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  if (apr_hash_count(merge_history))
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, merge_history);
           hi;
           hi = apr_hash_next(hi))
        {
          svn_rangelist_t *subtree_rangelist = apr_hash_this_val(hi);

          svn_pool_clear(iterpool);
          SVN_ERR(svn_rangelist_merge2(merged_rangelist, subtree_rangelist,
                                       result_pool, iterpool));
        }
      svn_pool_destroy(iterpool);
    }
  return SVN_NO_ERROR;
}


const char *
svn_inheritance_to_word(svn_mergeinfo_inheritance_t inherit)
{
  switch (inherit)
    {
    case svn_mergeinfo_inherited:
      return "inherited";
    case svn_mergeinfo_nearest_ancestor:
      return "nearest-ancestor";
    default:
      return "explicit";
    }
}

svn_mergeinfo_inheritance_t
svn_inheritance_from_word(const char *word)
{
  if (strcmp(word, "inherited") == 0)
    return svn_mergeinfo_inherited;
  if (strcmp(word, "nearest-ancestor") == 0)
    return svn_mergeinfo_nearest_ancestor;
  return svn_mergeinfo_explicit;
}
