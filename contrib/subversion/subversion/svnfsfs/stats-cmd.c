/* stats-cmd.c -- implements the size stats sub-command.
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

#include "svn_fs.h"
#include "svn_pools.h"
#include "svn_sorts.h"

#include "private/svn_sorts_private.h"
#include "private/svn_string_private.h"
#include "private/svn_fs_fs_private.h"

#include "svn_private_config.h"
#include "svnfsfs.h"

/* Return the string, allocated in RESULT_POOL, describing the value 2**I.
 */
static const char *
print_two_power(int i,
                apr_pool_t *result_pool)
{
  /* These are the SI prefixes for base-1000, the binary ones with base-1024
     are too clumsy and require appending B for "byte" to be intelligible,
     e.g. "MiB".

     Therefore, we ignore the official standard and revert to the traditional
     contextual use were the base-1000 prefixes are understood as base-1024
     when it came to data sizes.
   */
  const char *si_prefixes = " kMGTPEZY";

  int number = (i >= 0) ? (1 << (i % 10)) : 0;
  int thousands = (i >= 0) ? (i / 10) : 0;

  char si_prefix = (thousands < strlen(si_prefixes))
                 ? si_prefixes[thousands]
                 : '?';

  if (si_prefix == ' ')
    return apr_psprintf(result_pool, "%d", number);

  return apr_psprintf(result_pool, "%d%c", number, si_prefix);
}

/* Print statistics for the given group of representations to console.
 * Use POOL for allocations.
 */
static void
print_rep_stats(svn_fs_fs__representation_stats_t *stats,
                apr_pool_t *pool)
{
  printf(_("%20s bytes in %12s reps\n"
           "%20s bytes in %12s shared reps\n"
           "%20s bytes expanded size\n"
           "%20s bytes expanded shared size\n"
           "%20s bytes with rep-sharing off\n"
           "%20s shared references\n"
           "%20.3f average delta chain length\n"),
         svn__ui64toa_sep(stats->total.packed_size, ',', pool),
         svn__ui64toa_sep(stats->total.count, ',', pool),
         svn__ui64toa_sep(stats->shared.packed_size, ',', pool),
         svn__ui64toa_sep(stats->shared.count, ',', pool),
         svn__ui64toa_sep(stats->total.expanded_size, ',', pool),
         svn__ui64toa_sep(stats->shared.expanded_size, ',', pool),
         svn__ui64toa_sep(stats->expanded_size, ',', pool),
         svn__ui64toa_sep(stats->references - stats->total.count, ',', pool),
         stats->chain_len / MAX(1.0, (double)stats->total.count));
}

/* Print the (used) contents of CHANGES.  Use POOL for allocations.
 */
static void
print_largest_reps(svn_fs_fs__largest_changes_t *changes,
                   apr_pool_t *pool)
{
  apr_size_t i;
  for (i = 0; i < changes->count && changes->changes[i]->size; ++i)
    printf(_("%12s r%-8ld %s\n"),
           svn__ui64toa_sep(changes->changes[i]->size, ',', pool),
           changes->changes[i]->revision,
           changes->changes[i]->path->data);
}

/* Print the non-zero section of HISTOGRAM to console.
 * Use POOL for allocations.
 */
static void
print_histogram(svn_fs_fs__histogram_t *histogram,
                apr_pool_t *pool)
{
  int first = 0;
  int last = 63;
  int i;

  /* identify non-zero range */
  while (last > 0 && histogram->lines[last].count == 0)
    --last;

  while (first <= last && histogram->lines[first].count == 0)
    ++first;

  /* display histogram lines */
  for (i = last; i >= first; --i)
    printf(_("  %4s .. < %-4s %19s (%2d%%) bytes in %12s (%2d%%) items\n"),
           print_two_power(i-1, pool), print_two_power(i, pool),
           svn__ui64toa_sep(histogram->lines[i].sum, ',', pool),
           (int)(histogram->lines[i].sum * 100 / histogram->total.sum),
           svn__ui64toa_sep(histogram->lines[i].count, ',', pool),
           (int)(histogram->lines[i].count * 100 / histogram->total.count));
}

/* COMPARISON_FUNC for svn_sort__hash.
 * Sort extension_info_t values by total count in descending order.
 */
static int
compare_count(const svn_sort__item_t *a,
              const svn_sort__item_t *b)
{
  const svn_fs_fs__extension_info_t *lhs = a->value;
  const svn_fs_fs__extension_info_t *rhs = b->value;
  apr_int64_t diff = lhs->node_histogram.total.count
                   - rhs->node_histogram.total.count;

  return diff > 0 ? -1 : (diff < 0 ? 1 : 0);
}

/* COMPARISON_FUNC for svn_sort__hash.
 * Sort extension_info_t values by total uncompressed size in descending order.
 */
static int
compare_node_size(const svn_sort__item_t *a,
                  const svn_sort__item_t *b)
{
  const svn_fs_fs__extension_info_t *lhs = a->value;
  const svn_fs_fs__extension_info_t *rhs = b->value;
  apr_int64_t diff = lhs->node_histogram.total.sum
                   - rhs->node_histogram.total.sum;

  return diff > 0 ? -1 : (diff < 0 ? 1 : 0);
}

/* COMPARISON_FUNC for svn_sort__hash.
 * Sort extension_info_t values by total prep count in descending order.
 */
static int
compare_rep_size(const svn_sort__item_t *a,
                 const svn_sort__item_t *b)
{
  const svn_fs_fs__extension_info_t *lhs = a->value;
  const svn_fs_fs__extension_info_t *rhs = b->value;
  apr_int64_t diff = lhs->rep_histogram.total.sum
                   - rhs->rep_histogram.total.sum;

  return diff > 0 ? -1 : (diff < 0 ? 1 : 0);
}

/* Return an array of extension_info_t* for the (up to) 16 most prominent
 * extensions in STATS according to the sort criterion COMPARISON_FUNC.
 * Allocate results in POOL.
 */
static apr_array_header_t *
get_by_extensions(svn_fs_fs__stats_t *stats,
                  int (*comparison_func)(const svn_sort__item_t *,
                                         const svn_sort__item_t *),
                  apr_pool_t *pool)
{
  /* sort all data by extension */
  apr_array_header_t *sorted
    = svn_sort__hash(stats->by_extension, comparison_func, pool);

  /* select the top (first) 16 entries */
  int count = MIN(sorted->nelts, 16);
  apr_array_header_t *result
    = apr_array_make(pool, count, sizeof(svn_fs_fs__extension_info_t*));
  int i;

  for (i = 0; i < count; ++i)
    APR_ARRAY_PUSH(result, svn_fs_fs__extension_info_t*)
     = APR_ARRAY_IDX(sorted, i, svn_sort__item_t).value;

  return result;
}

/* Add all extension_info_t* entries of TO_ADD not already in TARGET to
 * TARGET.
 */
static void
merge_by_extension(apr_array_header_t *target,
                   apr_array_header_t *to_add)
{
  int i, k, count;

  count = target->nelts;
  for (i = 0; i < to_add->nelts; ++i)
    {
      svn_fs_fs__extension_info_t *info
        = APR_ARRAY_IDX(to_add, i, svn_fs_fs__extension_info_t *);
      for (k = 0; k < count; ++k)
        if (info == APR_ARRAY_IDX(target, k, svn_fs_fs__extension_info_t *))
          break;

      if (k == count)
        APR_ARRAY_PUSH(target, svn_fs_fs__extension_info_t*) = info;
    }
}

/* Print the (up to) 16 extensions in STATS with the most changes.
 * Use POOL for allocations.
 */
static void
print_extensions_by_changes(svn_fs_fs__stats_t *stats,
                            apr_pool_t *pool)
{
  apr_array_header_t *data = get_by_extensions(stats, compare_count, pool);
  apr_int64_t sum = 0;
  int i;

  for (i = 0; i < data->nelts; ++i)
    {
      svn_fs_fs__extension_info_t *info
        = APR_ARRAY_IDX(data, i, svn_fs_fs__extension_info_t *);

      /* If there are elements, then their count cannot be 0. */
      assert(stats->file_histogram.total.count);

      sum += info->node_histogram.total.count;
      printf(_("%11s %20s (%2d%%) representations\n"),
             info->extension,
             svn__ui64toa_sep(info->node_histogram.total.count, ',', pool),
             (int)(info->node_histogram.total.count * 100 /
                   stats->file_histogram.total.count));
    }

  if (stats->file_histogram.total.count)
    {
      printf(_("%11s %20s (%2d%%) representations\n"),
             "(others)",
             svn__ui64toa_sep(stats->file_histogram.total.count - sum, ',',
                              pool),
             (int)((stats->file_histogram.total.count - sum) * 100 /
                   stats->file_histogram.total.count));
    }
}

/* Calculate a percentage, handling edge cases. */
static int
get_percentage(apr_uint64_t part,
               apr_uint64_t total)
{
  /* This include total == 0. */
  if (part >= total)
    return 100;

  /* Standard case. */
  return (int)(part * 100.0 / total);
}

/* Print the (up to) 16 extensions in STATS with the largest total size of
 * changed file content.  Use POOL for allocations.
 */
static void
print_extensions_by_nodes(svn_fs_fs__stats_t *stats,
                          apr_pool_t *pool)
{
  apr_array_header_t *data = get_by_extensions(stats, compare_node_size, pool);
  apr_int64_t sum = 0;
  int i;

  for (i = 0; i < data->nelts; ++i)
    {
      svn_fs_fs__extension_info_t *info
        = APR_ARRAY_IDX(data, i, svn_fs_fs__extension_info_t *);
      sum += info->node_histogram.total.sum;
      printf(_("%11s %20s (%2d%%) bytes\n"),
             info->extension,
             svn__ui64toa_sep(info->node_histogram.total.sum, ',', pool),
             get_percentage(info->node_histogram.total.sum,
                            stats->file_histogram.total.sum));
    }

  if (stats->file_histogram.total.sum > sum)
    {
      /* Total sum can't be zero here. */
      printf(_("%11s %20s (%2d%%) bytes\n"),
             "(others)",
             svn__ui64toa_sep(stats->file_histogram.total.sum - sum, ',',
                              pool),
             get_percentage(stats->file_histogram.total.sum - sum,
                            stats->file_histogram.total.sum));
    }
}

/* Print the (up to) 16 extensions in STATS with the largest total size of
 * changed file content.  Use POOL for allocations.
 */
static void
print_extensions_by_reps(svn_fs_fs__stats_t *stats,
                         apr_pool_t *pool)
{
  apr_array_header_t *data = get_by_extensions(stats, compare_rep_size, pool);
  apr_int64_t sum = 0;
  int i;

  for (i = 0; i < data->nelts; ++i)
    {
      svn_fs_fs__extension_info_t *info
        = APR_ARRAY_IDX(data, i, svn_fs_fs__extension_info_t *);
      sum += info->rep_histogram.total.sum;
      printf(_("%11s %20s (%2d%%) bytes\n"),
             info->extension,
             svn__ui64toa_sep(info->rep_histogram.total.sum, ',', pool),
             get_percentage(info->rep_histogram.total.sum,
                            stats->rep_size_histogram.total.sum));
    }

  if (stats->rep_size_histogram.total.sum > sum)
    {
      /* Total sum can't be zero here. */
      printf(_("%11s %20s (%2d%%) bytes\n"),
             "(others)",
             svn__ui64toa_sep(stats->rep_size_histogram.total.sum - sum, ',',
                              pool),
             get_percentage(stats->rep_size_histogram.total.sum - sum,
                            stats->rep_size_histogram.total.sum));
    }
}

/* Print per-extension histograms for the most frequent extensions in STATS.
 * Use POOL for allocations. */
static void
print_histograms_by_extension(svn_fs_fs__stats_t *stats,
                              apr_pool_t *pool)
{
  apr_array_header_t *data = get_by_extensions(stats, compare_count, pool);
  int i;

  merge_by_extension(data, get_by_extensions(stats, compare_node_size, pool));
  merge_by_extension(data, get_by_extensions(stats, compare_rep_size, pool));

  for (i = 0; i < data->nelts; ++i)
    {
      svn_fs_fs__extension_info_t *info
        = APR_ARRAY_IDX(data, i, svn_fs_fs__extension_info_t *);
      printf("\nHistogram of '%s' file sizes:\n", info->extension);
      print_histogram(&info->node_histogram, pool);
      printf("\nHistogram of '%s' file representation sizes:\n",
             info->extension);
      print_histogram(&info->rep_histogram, pool);
    }
}

/* Print the contents of STATS to the console.
 * Use POOL for allocations.
 */
static void
print_stats(svn_fs_fs__stats_t *stats,
            apr_pool_t *pool)
{
  /* print results */
  printf("\n\nGlobal statistics:\n");
  printf(_("%20s bytes in %12s revisions\n"
           "%20s bytes in %12s changes\n"
           "%20s bytes in %12s node revision records\n"
           "%20s bytes in %12s representations\n"
           "%20s bytes expanded representation size\n"
           "%20s bytes with rep-sharing off\n"),
         svn__ui64toa_sep(stats->total_size, ',', pool),
         svn__ui64toa_sep(stats->revision_count, ',', pool),
         svn__ui64toa_sep(stats->change_len, ',', pool),
         svn__ui64toa_sep(stats->change_count, ',', pool),
         svn__ui64toa_sep(stats->total_node_stats.size, ',', pool),
         svn__ui64toa_sep(stats->total_node_stats.count, ',', pool),
         svn__ui64toa_sep(stats->total_rep_stats.total.packed_size, ',',
                         pool),
         svn__ui64toa_sep(stats->total_rep_stats.total.count, ',', pool),
         svn__ui64toa_sep(stats->total_rep_stats.total.expanded_size, ',',
                         pool),
         svn__ui64toa_sep(stats->total_rep_stats.expanded_size, ',', pool));

  printf("\nNoderev statistics:\n");
  printf(_("%20s bytes in %12s nodes total\n"
           "%20s bytes in %12s directory noderevs\n"
           "%20s bytes in %12s file noderevs\n"),
         svn__ui64toa_sep(stats->total_node_stats.size, ',', pool),
         svn__ui64toa_sep(stats->total_node_stats.count, ',', pool),
         svn__ui64toa_sep(stats->dir_node_stats.size, ',', pool),
         svn__ui64toa_sep(stats->dir_node_stats.count, ',', pool),
         svn__ui64toa_sep(stats->file_node_stats.size, ',', pool),
         svn__ui64toa_sep(stats->file_node_stats.count, ',', pool));

  printf("\nRepresentation statistics:\n");
  printf(_("%20s bytes in %12s representations total\n"
           "%20s bytes in %12s directory representations\n"
           "%20s bytes in %12s file representations\n"
           "%20s bytes in %12s representations of added file nodes\n"
           "%20s bytes in %12s directory property representations\n"
           "%20s bytes in %12s file property representations\n"
           "                         with %12.3f average delta chain length\n"
           "%20s bytes in header & footer overhead\n"),
         svn__ui64toa_sep(stats->total_rep_stats.total.packed_size, ',',
                         pool),
         svn__ui64toa_sep(stats->total_rep_stats.total.count, ',', pool),
         svn__ui64toa_sep(stats->dir_rep_stats.total.packed_size, ',',
                         pool),
         svn__ui64toa_sep(stats->dir_rep_stats.total.count, ',', pool),
         svn__ui64toa_sep(stats->file_rep_stats.total.packed_size, ',',
                         pool),
         svn__ui64toa_sep(stats->file_rep_stats.total.count, ',', pool),
         svn__ui64toa_sep(stats->added_rep_size_histogram.total.sum, ',',
                         pool),
         svn__ui64toa_sep(stats->added_rep_size_histogram.total.count, ',',
                         pool),
         svn__ui64toa_sep(stats->dir_prop_rep_stats.total.packed_size, ',',
                         pool),
         svn__ui64toa_sep(stats->dir_prop_rep_stats.total.count, ',', pool),
         svn__ui64toa_sep(stats->file_prop_rep_stats.total.packed_size, ',',
                         pool),
         svn__ui64toa_sep(stats->file_prop_rep_stats.total.count, ',', pool),
         stats->total_rep_stats.chain_len
            / (double)stats->total_rep_stats.total.count,
         svn__ui64toa_sep(stats->total_rep_stats.total.overhead_size, ',',
                         pool));

  printf("\nDirectory representation statistics:\n");
  print_rep_stats(&stats->dir_rep_stats, pool);
  printf("\nFile representation statistics:\n");
  print_rep_stats(&stats->file_rep_stats, pool);
  printf("\nDirectory property representation statistics:\n");
  print_rep_stats(&stats->dir_prop_rep_stats, pool);
  printf("\nFile property representation statistics:\n");
  print_rep_stats(&stats->file_prop_rep_stats, pool);

  printf("\nLargest representations:\n");
  print_largest_reps(stats->largest_changes, pool);
  printf("\nExtensions by number of representations:\n");
  print_extensions_by_changes(stats, pool);
  printf("\nExtensions by size of changed files:\n");
  print_extensions_by_nodes(stats, pool);
  printf("\nExtensions by size of representations:\n");
  print_extensions_by_reps(stats, pool);

  printf("\nHistogram of expanded node sizes:\n");
  print_histogram(&stats->node_size_histogram, pool);
  printf("\nHistogram of representation sizes:\n");
  print_histogram(&stats->rep_size_histogram, pool);
  printf("\nHistogram of file sizes:\n");
  print_histogram(&stats->file_histogram, pool);
  printf("\nHistogram of file representation sizes:\n");
  print_histogram(&stats->file_rep_histogram, pool);
  printf("\nHistogram of file property sizes:\n");
  print_histogram(&stats->file_prop_histogram, pool);
  printf("\nHistogram of file property representation sizes:\n");
  print_histogram(&stats->file_prop_rep_histogram, pool);
  printf("\nHistogram of directory sizes:\n");
  print_histogram(&stats->dir_histogram, pool);
  printf("\nHistogram of directory representation sizes:\n");
  print_histogram(&stats->dir_rep_histogram, pool);
  printf("\nHistogram of directory property sizes:\n");
  print_histogram(&stats->dir_prop_histogram, pool);
  printf("\nHistogram of directory property representation sizes:\n");
  print_histogram(&stats->dir_prop_rep_histogram, pool);

  print_histograms_by_extension(stats, pool);
}

/* Our progress function simply prints the REVISION number and makes it
 * appear immediately.
 */
static void
print_progress(svn_revnum_t revision,
               void *baton,
               apr_pool_t *pool)
{
  printf("%8ld", revision);
  fflush(stdout);
}

/* This implements `svn_opt_subcommand_t'. */
svn_error_t *
subcommand__stats(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnfsfs__opt_state *opt_state = baton;
  svn_fs_fs__stats_t *stats;
  svn_fs_t *fs;

  printf("Reading revisions\n");
  SVN_ERR(open_fs(&fs, opt_state->repository_path, pool));
  SVN_ERR(svn_fs_fs__get_stats(&stats, fs, print_progress, NULL,
                               check_cancel, NULL, pool, pool));

  print_stats(stats, pool);

  return SVN_NO_ERROR;
}
