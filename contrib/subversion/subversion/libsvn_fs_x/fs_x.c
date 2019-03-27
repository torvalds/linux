/* fs_x.c --- filesystem operations specific to fs_x
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

#include "fs_x.h"

#include <apr_uuid.h>

#include "svn_hash.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"
#include "svn_sorts.h"
#include "svn_version.h"

#include "cached_data.h"
#include "id.h"
#include "low_level.h"
#include "rep-cache.h"
#include "revprops.h"
#include "transaction.h"
#include "tree.h"
#include "util.h"
#include "index.h"

#include "private/svn_fs_util.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* The default maximum number of files per directory to store in the
   rev and revprops directory.  The number below is somewhat arbitrary,
   and can be overridden by defining the macro while compiling; the
   figure of 1000 is reasonable for VFAT filesystems, which are by far
   the worst performers in this area. */
#ifndef SVN_FS_X_DEFAULT_MAX_FILES_PER_DIR
#define SVN_FS_X_DEFAULT_MAX_FILES_PER_DIR 1000
#endif

/* Begin deltification after a node history exceeded this this limit.
   Useful values are 4 to 64 with 16 being a good compromise between
   computational overhead and repository size savings.
   Should be a power of 2.
   Values < 2 will result in standard skip-delta behavior. */
#define SVN_FS_X_MAX_LINEAR_DELTIFICATION 16

/* Finding a deltification base takes operations proportional to the
   number of changes being skipped. To prevent exploding runtime
   during commits, limit the deltification range to this value.
   Should be a power of 2 minus one.
   Values < 1 disable deltification. */
#define SVN_FS_X_MAX_DELTIFICATION_WALK 1023




/* Check that BUF, a nul-terminated buffer of text from format file PATH,
   contains only digits at OFFSET and beyond, raising an error if not.

   Uses SCRATCH_POOL for temporary allocation. */
static svn_error_t *
check_format_file_buffer_numeric(const char *buf,
                                 apr_off_t offset,
                                 const char *path,
                                 apr_pool_t *scratch_pool)
{
  return svn_fs_x__check_file_buffer_numeric(buf, offset, path, "Format",
                                             scratch_pool);
}

/* Return the error SVN_ERR_FS_UNSUPPORTED_FORMAT if FS's format
   number is not the same as a format number supported by this
   Subversion. */
static svn_error_t *
check_format(int format)
{
  /* Put blacklisted versions here. */

  /* We support any format if it matches the current format. */
  if (format == SVN_FS_X__FORMAT_NUMBER)
    return SVN_NO_ERROR;

  /* Experimental formats are only supported if they match the current, but
   * that case has already been handled. So, reject any experimental format.
   */
  if (SVN_FS_X__EXPERIMENTAL_FORMAT_NUMBER >= format)
    return svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
      _("Unsupported experimental FSX format '%d' found; current format is '%d'"),
      format, SVN_FS_X__FORMAT_NUMBER);

  /* By default, we will support any non-experimental format released so far.
   */
  if (format <= SVN_FS_X__FORMAT_NUMBER)
    return SVN_NO_ERROR;

  return svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
     _("Expected FSX format between '%d' and '%d'; found format '%d'"),
     SVN_FS_X__EXPERIMENTAL_FORMAT_NUMBER + 1, SVN_FS_X__FORMAT_NUMBER,
     format);
}

/* Read the format file at PATH and set *PFORMAT to the format version found
 * and *MAX_FILES_PER_DIR to the shard size.  Use SCRATCH_POOL for temporary
 * allocations. */
static svn_error_t *
read_format(int *pformat,
            int *max_files_per_dir,
            const char *path,
            apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  svn_stringbuf_t *content;
  svn_stringbuf_t *buf;
  svn_boolean_t eos = FALSE;

  SVN_ERR(svn_stringbuf_from_file2(&content, path, scratch_pool));
  stream = svn_stream_from_stringbuf(content, scratch_pool);
  SVN_ERR(svn_stream_readline(stream, &buf, "\n", &eos, scratch_pool));
  if (buf->len == 0 && eos)
    {
      /* Return a more useful error message. */
      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                               _("Can't read first line of format file '%s'"),
                               svn_dirent_local_style(path, scratch_pool));
    }

  /* Check that the first line contains only digits. */
  SVN_ERR(check_format_file_buffer_numeric(buf->data, 0, path, scratch_pool));
  SVN_ERR(svn_cstring_atoi(pformat, buf->data));

  /* Check that we support this format at all */
  SVN_ERR(check_format(*pformat));

  /* Read any options. */
  SVN_ERR(svn_stream_readline(stream, &buf, "\n", &eos, scratch_pool));
  if (!eos && strncmp(buf->data, "layout sharded ", 15) == 0)
    {
      /* Check that the argument is numeric. */
      SVN_ERR(check_format_file_buffer_numeric(buf->data, 15, path,
                                               scratch_pool));
      SVN_ERR(svn_cstring_atoi(max_files_per_dir, buf->data + 15));
    }
  else
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                  _("'%s' contains invalid filesystem format option '%s'"),
                  svn_dirent_local_style(path, scratch_pool), buf->data);

  return SVN_NO_ERROR;
}

/* Write the format number and maximum number of files per directory
   to a new format file in PATH, possibly expecting to overwrite a
   previously existing file.

   Use SCRATCH_POOL for temporary allocation. */
svn_error_t *
svn_fs_x__write_format(svn_fs_t *fs,
                       svn_boolean_t overwrite,
                       apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *sb;
  const char *path = svn_fs_x__path_format(fs, scratch_pool);
  svn_fs_x__data_t *ffd = fs->fsap_data;

  SVN_ERR_ASSERT(1 <= ffd->format && ffd->format <= SVN_FS_X__FORMAT_NUMBER);

  sb = svn_stringbuf_createf(scratch_pool, "%d\n", ffd->format);
  svn_stringbuf_appendcstr(sb, apr_psprintf(scratch_pool,
                                            "layout sharded %d\n",
                                            ffd->max_files_per_dir));

  /* svn_io_write_version_file() does a load of magic to allow it to
     replace version files that already exist.  We only need to do
     that when we're allowed to overwrite an existing file. */
  if (! overwrite)
    {
      /* Create the file */
      SVN_ERR(svn_io_file_create(path, sb->data, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_io_write_atomic2(path, sb->data, sb->len,
                                   NULL /* copy_perms_path */,
                                   ffd->flush_to_disk, scratch_pool));
    }

  /* And set the perms to make it read only */
  return svn_io_set_file_read_only(path, FALSE, scratch_pool);
}

/* Check that BLOCK_SIZE is a valid block / page size, i.e. it is within
 * the range of what the current system may address in RAM and it is a
 * power of 2.  Assume that the element size within the block is ITEM_SIZE.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
verify_block_size(apr_int64_t block_size,
                  apr_size_t item_size,
                  const char *name,
                  apr_pool_t *scratch_pool)
{
  /* Limit range. */
  if (block_size <= 0)
    return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                             _("%s is too small for fsfs.conf setting '%s'."),
                             apr_psprintf(scratch_pool,
                                          "%" APR_INT64_T_FMT,
                                          block_size),
                             name);

  if (block_size > SVN_MAX_OBJECT_SIZE / item_size)
    return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                             _("%s is too large for fsfs.conf setting '%s'."),
                             apr_psprintf(scratch_pool,
                                          "%" APR_INT64_T_FMT,
                                          block_size),
                             name);

  /* Ensure it is a power of two.
   * For positive X,  X & (X-1) will reset the lowest bit set.
   * If the result is 0, at most one bit has been set. */
  if (0 != (block_size & (block_size - 1)))
    return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                             _("%s is invalid for fsfs.conf setting '%s' "
                               "because it is not a power of 2."),
                             apr_psprintf(scratch_pool,
                                          "%" APR_INT64_T_FMT,
                                          block_size),
                             name);

  return SVN_NO_ERROR;
}

/* Read the configuration information of the file system at FS_PATH
 * and set the respective values in FFD.  Use pools as usual.
 */
static svn_error_t *
read_config(svn_fs_x__data_t *ffd,
            const char *fs_path,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_config_t *config;
  apr_int64_t compression_level;

  SVN_ERR(svn_config_read3(&config,
                           svn_dirent_join(fs_path, PATH_CONFIG, scratch_pool),
                           FALSE, FALSE, FALSE, scratch_pool));

  /* Initialize ffd->rep_sharing_allowed. */
  SVN_ERR(svn_config_get_bool(config, &ffd->rep_sharing_allowed,
                              CONFIG_SECTION_REP_SHARING,
                              CONFIG_OPTION_ENABLE_REP_SHARING, TRUE));

  /* Initialize deltification settings in ffd. */
  SVN_ERR(svn_config_get_int64(config, &ffd->max_deltification_walk,
                               CONFIG_SECTION_DELTIFICATION,
                               CONFIG_OPTION_MAX_DELTIFICATION_WALK,
                               SVN_FS_X_MAX_DELTIFICATION_WALK));
  SVN_ERR(svn_config_get_int64(config, &ffd->max_linear_deltification,
                               CONFIG_SECTION_DELTIFICATION,
                               CONFIG_OPTION_MAX_LINEAR_DELTIFICATION,
                               SVN_FS_X_MAX_LINEAR_DELTIFICATION));
  SVN_ERR(svn_config_get_int64(config, &compression_level,
                               CONFIG_SECTION_DELTIFICATION,
                               CONFIG_OPTION_COMPRESSION_LEVEL,
                               SVN_DELTA_COMPRESSION_LEVEL_DEFAULT));
  ffd->delta_compression_level
    = (int)MIN(MAX(SVN_DELTA_COMPRESSION_LEVEL_NONE, compression_level),
                SVN_DELTA_COMPRESSION_LEVEL_MAX);

  /* Initialize revprop packing settings in ffd. */
  SVN_ERR(svn_config_get_bool(config, &ffd->compress_packed_revprops,
                              CONFIG_SECTION_PACKED_REVPROPS,
                              CONFIG_OPTION_COMPRESS_PACKED_REVPROPS,
                              TRUE));
  SVN_ERR(svn_config_get_int64(config, &ffd->revprop_pack_size,
                               CONFIG_SECTION_PACKED_REVPROPS,
                               CONFIG_OPTION_REVPROP_PACK_SIZE,
                               ffd->compress_packed_revprops
                                   ? 0x100
                                   : 0x40));

  ffd->revprop_pack_size *= 1024;

  /* I/O settings in ffd. */
  SVN_ERR(svn_config_get_int64(config, &ffd->block_size,
                               CONFIG_SECTION_IO,
                               CONFIG_OPTION_BLOCK_SIZE,
                               64));
  SVN_ERR(svn_config_get_int64(config, &ffd->l2p_page_size,
                               CONFIG_SECTION_IO,
                               CONFIG_OPTION_L2P_PAGE_SIZE,
                               0x2000));
  SVN_ERR(svn_config_get_int64(config, &ffd->p2l_page_size,
                               CONFIG_SECTION_IO,
                               CONFIG_OPTION_P2L_PAGE_SIZE,
                               0x400));

  /* Don't accept unreasonable or illegal values.
   * Block size and P2L page size are in kbytes;
   * L2P blocks are arrays of apr_off_t. */
  SVN_ERR(verify_block_size(ffd->block_size, 0x400,
                            CONFIG_OPTION_BLOCK_SIZE, scratch_pool));
  SVN_ERR(verify_block_size(ffd->p2l_page_size, 0x400,
                            CONFIG_OPTION_P2L_PAGE_SIZE, scratch_pool));
  SVN_ERR(verify_block_size(ffd->l2p_page_size, sizeof(apr_off_t),
                            CONFIG_OPTION_L2P_PAGE_SIZE, scratch_pool));

  /* convert kBytes to bytes */
  ffd->block_size *= 0x400;
  ffd->p2l_page_size *= 0x400;
  /* L2P pages are in entries - not in (k)Bytes */

  /* Debug options. */
  SVN_ERR(svn_config_get_bool(config, &ffd->pack_after_commit,
                              CONFIG_SECTION_DEBUG,
                              CONFIG_OPTION_PACK_AFTER_COMMIT,
                              FALSE));

  /* memcached configuration */
  SVN_ERR(svn_cache__make_memcache_from_config(&ffd->memcache, config,
                                               result_pool, scratch_pool));

  SVN_ERR(svn_config_get_bool(config, &ffd->fail_stop,
                              CONFIG_SECTION_CACHES, CONFIG_OPTION_FAIL_STOP,
                              FALSE));

  return SVN_NO_ERROR;
}

/* Write FS' initial configuration file.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
write_config(svn_fs_t *fs,
             apr_pool_t *scratch_pool)
{
#define NL APR_EOL_STR
  static const char * const fsx_conf_contents =
"### This file controls the configuration of the FSX filesystem."            NL
""                                                                           NL
"[" SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS "]"                          NL
"### These options name memcached servers used to cache internal FSX"        NL
"### data.  See http://www.danga.com/memcached/ for more information on"     NL
"### memcached.  To use memcached with FSX, run one or more memcached"       NL
"### servers, and specify each of them as an option like so:"                NL
"# first-server = 127.0.0.1:11211"                                           NL
"# remote-memcached = mymemcached.corp.example.com:11212"                    NL
"### The option name is ignored; the value is of the form HOST:PORT."        NL
"### memcached servers can be shared between multiple repositories;"         NL
"### however, if you do this, you *must* ensure that repositories have"      NL
"### distinct UUIDs and paths, or else cached data from one repository"      NL
"### might be used by another accidentally.  Note also that memcached has"   NL
"### no authentication for reads or writes, so you must ensure that your"    NL
"### memcached servers are only accessible by trusted users."                NL
""                                                                           NL
"[" CONFIG_SECTION_CACHES "]"                                                NL
"### When a cache-related error occurs, normally Subversion ignores it"      NL
"### and continues, logging an error if the server is appropriately"         NL
"### configured (and ignoring it with file:// access).  To make"             NL
"### Subversion never ignore cache errors, uncomment this line."             NL
"# " CONFIG_OPTION_FAIL_STOP " = true"                                       NL
""                                                                           NL
"[" CONFIG_SECTION_REP_SHARING "]"                                           NL
"### To conserve space, the filesystem can optionally avoid storing"         NL
"### duplicate representations.  This comes at a slight cost in"             NL
"### performance, as maintaining a database of shared representations can"   NL
"### increase commit times.  The space savings are dependent upon the size"  NL
"### of the repository, the number of objects it contains and the amount of" NL
"### duplication between them, usually a function of the branching and"      NL
"### merging process."                                                       NL
"###"                                                                        NL
"### The following parameter enables rep-sharing in the repository.  It can" NL
"### be switched on and off at will, but for best space-saving results"      NL
"### should be enabled consistently over the life of the repository."        NL
"### 'svnadmin verify' will check the rep-cache regardless of this setting." NL
"### rep-sharing is enabled by default."                                     NL
"# " CONFIG_OPTION_ENABLE_REP_SHARING " = true"                              NL
""                                                                           NL
"[" CONFIG_SECTION_DELTIFICATION "]"                                         NL
"### To conserve space, the filesystem stores data as differences against"   NL
"### existing representations.  This comes at a slight cost in performance," NL
"### as calculating differences can increase commit times.  Reading data"    NL
"### will also create higher CPU load and the data will be fragmented."      NL
"### Since deltification tends to save significant amounts of disk space,"   NL
"### the overall I/O load can actually be lower."                            NL
"###"                                                                        NL
"### The options in this section allow for tuning the deltification"         NL
"### strategy.  Their effects on data size and server performance may vary"  NL
"### from one repository to another."                                        NL
"###"                                                                        NL
"### During commit, the server may need to walk the whole change history of" NL
"### of a given node to find a suitable deltification base.  This linear"    NL
"### process can impact commit times, svnadmin load and similar operations." NL
"### This setting limits the depth of the deltification history.  If the"    NL
"### threshold has been reached, the node will be stored as fulltext and a"  NL
"### new deltification history begins."                                      NL
"### Note, this is unrelated to svn log."                                    NL
"### Very large values rarely provide significant additional savings but"    NL
"### can impact performance greatly - in particular if directory"            NL
"### deltification has been activated.  Very small values may be useful in"  NL
"### repositories that are dominated by large, changing binaries."           NL
"### Should be a power of two minus 1.  A value of 0 will effectively"       NL
"### disable deltification."                                                 NL
"### For 1.9, the default value is 1023."                                    NL
"# " CONFIG_OPTION_MAX_DELTIFICATION_WALK " = 1023"                          NL
"###"                                                                        NL
"### The skip-delta scheme used by FSX tends to repeatably store redundant"  NL
"### delta information where a simple delta against the latest version is"   NL
"### often smaller.  By default, 1.9+ will therefore use skip deltas only"   NL
"### after the linear chain of deltas has grown beyond the threshold"        NL
"### specified by this setting."                                             NL
"### Values up to 64 can result in some reduction in repository size for"    NL
"### the cost of quickly increasing I/O and CPU costs. Similarly, smaller"   NL
"### numbers can reduce those costs at the cost of more disk space.  For"    NL
"### rarely read repositories or those containing larger binaries, this may" NL
"### present a better trade-off."                                            NL
"### Should be a power of two.  A value of 1 or smaller will cause the"      NL
"### exclusive use of skip-deltas."                                          NL
"### For 1.8, the default value is 16."                                      NL
"# " CONFIG_OPTION_MAX_LINEAR_DELTIFICATION " = 16"                          NL
"###"                                                                        NL
"### After deltification, we compress the data through zlib to minimize on-" NL
"### disk size.  That can be an expensive and ineffective process.  This"    NL
"### setting controls the usage of zlib in future revisions."                NL
"### Revisions with highly compressible data in them may shrink in size"     NL
"### if the setting is increased but may take much longer to commit.  The"   NL
"### time taken to uncompress that data again is widely independent of the"  NL
"### compression level."                                                     NL
"### Compression will be ineffective if the incoming content is already"     NL
"### highly compressed.  In that case, disabling the compression entirely"   NL
"### will speed up commits as well as reading the data.  Repositories with"  NL
"### many small compressible files (source code) but also a high percentage" NL
"### of large incompressible ones (artwork) may benefit from compression"    NL
"### levels lowered to e.g. 1."                                              NL
"### Valid values are 0 to 9 with 9 providing the highest compression ratio" NL
"### and 0 disabling it altogether."                                         NL
"### The default value is 5."                                                NL
"# " CONFIG_OPTION_COMPRESSION_LEVEL " = 5"                                  NL
""                                                                           NL
"[" CONFIG_SECTION_PACKED_REVPROPS "]"                                       NL
"### This parameter controls the size (in kBytes) of packed revprop files."  NL
"### Revprops of consecutive revisions will be concatenated into a single"   NL
"### file up to but not exceeding the threshold given here.  However, each"  NL
"### pack file may be much smaller and revprops of a single revision may be" NL
"### much larger than the limit set here.  The threshold will be applied"    NL
"### before optional compression takes place."                               NL
"### Large values will reduce disk space usage at the expense of increased"  NL
"### latency and CPU usage reading and changing individual revprops.  They"  NL
"### become an advantage when revprop caching has been enabled because a"    NL
"### lot of data can be read in one go.  Values smaller than 4 kByte will"   NL
"### not improve latency any further and quickly render revprop packing"     NL
"### ineffective."                                                           NL
"### revprop-pack-size is 64 kBytes by default for non-compressed revprop"   NL
"### pack files and 256 kBytes when compression has been enabled."           NL
"# " CONFIG_OPTION_REVPROP_PACK_SIZE " = 64"                                 NL
"###"                                                                        NL
"### To save disk space, packed revprop files may be compressed.  Standard"  NL
"### revprops tend to allow for very effective compression.  Reading and"    NL
"### even more so writing, become significantly more CPU intensive.  With"   NL
"### revprop caching enabled, the overhead can be offset by reduced I/O"     NL
"### unless you often modify revprops after packing."                        NL
"### Compressing packed revprops is enabled by default."                     NL
"# " CONFIG_OPTION_COMPRESS_PACKED_REVPROPS " = true"                        NL
""                                                                           NL
"[" CONFIG_SECTION_IO "]"                                                    NL
"### Parameters in this section control the data access granularity in"      NL
"### format 7 repositories and later.  The defaults should translate into"   NL
"### decent performance over a wide range of setups."                        NL
"###"                                                                        NL
"### When a specific piece of information needs to be read from disk,  a"    NL
"### data block is being read at once and its contents are being cached."    NL
"### If the repository is being stored on a RAID, the block size should be"  NL
"### either 50% or 100% of RAID block size / granularity.  Also, your file"  NL
"### system blocks/clusters should be properly aligned and sized.  In that"  NL
"### setup, each access will hit only one disk (minimizes I/O load) but"     NL
"### uses all the data provided by the disk in a single access."             NL
"### For SSD-based storage systems, slightly lower values around 16 kB"      NL
"### may improve latency while still maximizing throughput."                 NL
"### Can be changed at any time but must be a power of 2."                   NL
"### block-size is given in kBytes and with a default of 64 kBytes."         NL
"# " CONFIG_OPTION_BLOCK_SIZE " = 64"                                        NL
"###"                                                                        NL
"### The log-to-phys index maps data item numbers to offsets within the"     NL
"### rev or pack file.  This index is organized in pages of a fixed maximum" NL
"### capacity.  To access an item, the page table and the respective page"   NL
"### must be read."                                                          NL
"### This parameter only affects revisions with thousands of changed paths." NL
"### If you have several extremely large revisions (~1 mio changes), think"  NL
"### about increasing this setting.  Reducing the value will rarely result"  NL
"### in a net speedup."                                                      NL
"### This is an expert setting.  Must be a power of 2."                      NL
"### l2p-page-size is 8192 entries by default."                              NL
"# " CONFIG_OPTION_L2P_PAGE_SIZE " = 8192"                                   NL
"###"                                                                        NL
"### The phys-to-log index maps positions within the rev or pack file to"    NL
"### to data items,  i.e. describes what piece of information is being"      NL
"### stored at any particular offset.  The index describes the rev file"     NL
"### in chunks (pages) and keeps a global list of all those pages.  Large"   NL
"### pages mean a shorter page table but a larger per-page description of"   NL
"### data items in it.  The latency sweet spot depends on the change size"   NL
"### distribution but covers a relatively wide range."                       NL
"### If the repository contains very large files,  i.e. individual changes"  NL
"### of tens of MB each,  increasing the page size will shorten the index"   NL
"### file at the expense of a slightly increased latency in sections with"   NL
"### smaller changes."                                                       NL
"### For source code repositories, this should be about 16x the block-size." NL
"### Must be a power of 2."                                                  NL
"### p2l-page-size is given in kBytes and with a default of 1024 kBytes."    NL
"# " CONFIG_OPTION_P2L_PAGE_SIZE " = 1024"                                   NL
;
#undef NL
  return svn_io_file_create(svn_dirent_join(fs->path, PATH_CONFIG,
                                            scratch_pool),
                            fsx_conf_contents, scratch_pool);
}

/* Read / Evaluate the global configuration in FS->CONFIG to set up
 * parameters in FS. */
static svn_error_t *
read_global_config(svn_fs_t *fs)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;

  ffd->flush_to_disk = !svn_hash__get_bool(fs->config,
                                           SVN_FS_CONFIG_NO_FLUSH_TO_DISK,
                                           FALSE);

  return SVN_NO_ERROR;
}

/* Read FS's UUID file and store the data in the FS struct. */
static svn_error_t *
read_uuid(svn_fs_t *fs,
          apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  apr_file_t *uuid_file;
  char buf[APR_UUID_FORMATTED_LENGTH + 2];
  apr_size_t limit;

  /* Read the repository uuid. */
  SVN_ERR(svn_io_file_open(&uuid_file, svn_fs_x__path_uuid(fs, scratch_pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           scratch_pool));

  limit = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(uuid_file, buf, &limit, scratch_pool));
  fs->uuid = apr_pstrdup(fs->pool, buf);

  /* Read the instance ID. */
  limit = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(uuid_file, buf, &limit,
                                  scratch_pool));
  ffd->instance_id = apr_pstrdup(fs->pool, buf);

  SVN_ERR(svn_io_file_close(uuid_file, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__read_format_file(svn_fs_t *fs,
                           apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  int format, max_files_per_dir;

  /* Read info from format file. */
  SVN_ERR(read_format(&format, &max_files_per_dir,
                      svn_fs_x__path_format(fs, scratch_pool), scratch_pool));

  /* Now that we've got *all* info, store / update values in FFD. */
  ffd->format = format;
  ffd->max_files_per_dir = max_files_per_dir;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__open(svn_fs_t *fs,
               const char *path,
               apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  fs->path = apr_pstrdup(fs->pool, path);

  /* Read the FS format file. */
  SVN_ERR(svn_fs_x__read_format_file(fs, scratch_pool));

  /* Read in and cache the repository uuid. */
  SVN_ERR(read_uuid(fs, scratch_pool));

  /* Read the min unpacked revision. */
  SVN_ERR(svn_fs_x__update_min_unpacked_rev(fs, scratch_pool));

  /* Read the configuration file. */
  SVN_ERR(read_config(ffd, fs->path, fs->pool, scratch_pool));

  /* Global configuration options. */
  SVN_ERR(read_global_config(fs));

  ffd->youngest_rev_cache = 0;

  return SVN_NO_ERROR;
}

/* Baton type bridging svn_fs_x__upgrade and upgrade_body carrying
 * parameters over between them. */
typedef struct upgrade_baton_t
{
  svn_fs_t *fs;
  svn_fs_upgrade_notify_t notify_func;
  void *notify_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
} upgrade_baton_t;

/* Upgrade the FS given in upgrade_baton_t *)BATON to the latest format
 * version.  Apply options an invoke callback from that BATON.
 * Temporary allocations are to be made from SCRATCH_POOL.
 *
 * At the moment, this is a simple placeholder as we don't support upgrades
 * from experimental FSX versions.
 */
static svn_error_t *
upgrade_body(void *baton,
             apr_pool_t *scratch_pool)
{
  upgrade_baton_t *upgrade_baton = baton;
  svn_fs_t *fs = upgrade_baton->fs;
  int format, max_files_per_dir;
  const char *format_path = svn_fs_x__path_format(fs, scratch_pool);

  /* Read the FS format number and max-files-per-dir setting. */
  SVN_ERR(read_format(&format, &max_files_per_dir, format_path,
                      scratch_pool));

  /* If we're already up-to-date, there's nothing else to be done here. */
  if (format == SVN_FS_X__FORMAT_NUMBER)
    return SVN_NO_ERROR;

  /* Done */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__upgrade(svn_fs_t *fs,
                  svn_fs_upgrade_notify_t notify_func,
                  void *notify_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *scratch_pool)
{
  upgrade_baton_t baton;
  baton.fs = fs;
  baton.notify_func = notify_func;
  baton.notify_baton = notify_baton;
  baton.cancel_func = cancel_func;
  baton.cancel_baton = cancel_baton;

  return svn_fs_x__with_all_locks(fs, upgrade_body, (void *)&baton,
                                  scratch_pool);
}


svn_error_t *
svn_fs_x__youngest_rev(svn_revnum_t *youngest_p,
                       svn_fs_t *fs,
                       apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  SVN_ERR(svn_fs_x__read_current(youngest_p, fs, scratch_pool));
  ffd->youngest_rev_cache = *youngest_p;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__ensure_revision_exists(svn_revnum_t rev,
                                 svn_fs_t *fs,
                                 apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;

  if (! SVN_IS_VALID_REVNUM(rev))
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("Invalid revision number '%ld'"), rev);


  /* Did the revision exist the last time we checked the current
     file? */
  if (rev <= ffd->youngest_rev_cache)
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_x__read_current(&ffd->youngest_rev_cache, fs, scratch_pool));

  /* Check again. */
  if (rev <= ffd->youngest_rev_cache)
    return SVN_NO_ERROR;

  return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                           _("No such revision %ld"), rev);
}


svn_error_t *
svn_fs_x__file_length(svn_filesize_t *length,
                      svn_fs_x__noderev_t *noderev)
{
  if (noderev->data_rep)
    *length = noderev->data_rep->expanded_size;
  else
    *length = 0;

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_fs_x__file_text_rep_equal(svn_fs_x__representation_t *a,
                              svn_fs_x__representation_t *b)
{
  svn_boolean_t a_empty = a == NULL || a->expanded_size == 0;
  svn_boolean_t b_empty = b == NULL || b->expanded_size == 0;

  /* This makes sure that neither rep will be NULL later on */
  if (a_empty && b_empty)
    return TRUE;

  if (a_empty != b_empty)
    return FALSE;

  /* Same physical representation?  Note that these IDs are always up-to-date
     instead of e.g. being set lazily. */
  if (svn_fs_x__id_eq(&a->id, &b->id))
    return TRUE;

  /* Contents are equal if the checksums match.  These are also always known.
   */
  return memcmp(a->md5_digest, b->md5_digest, sizeof(a->md5_digest)) == 0
      && memcmp(a->sha1_digest, b->sha1_digest, sizeof(a->sha1_digest)) == 0;
}

svn_error_t *
svn_fs_x__prop_rep_equal(svn_boolean_t *equal,
                         svn_fs_t *fs,
                         svn_fs_x__noderev_t *a,
                         svn_fs_x__noderev_t *b,
                         svn_boolean_t strict,
                         apr_pool_t *scratch_pool)
{
  svn_fs_x__representation_t *rep_a = a->prop_rep;
  svn_fs_x__representation_t *rep_b = b->prop_rep;
  apr_hash_t *proplist_a;
  apr_hash_t *proplist_b;

  /* Mainly for a==b==NULL */
  if (rep_a == rep_b)
    {
      *equal = TRUE;
      return SVN_NO_ERROR;
    }

  /* Committed property lists can be compared quickly */
  if (   rep_a && rep_b
      && svn_fs_x__is_revision(rep_a->id.change_set)
      && svn_fs_x__is_revision(rep_b->id.change_set))
    {
      /* MD5 must be given. Having the same checksum is good enough for
         accepting the prop lists as equal. */
      *equal = memcmp(rep_a->md5_digest, rep_b->md5_digest,
                      sizeof(rep_a->md5_digest)) == 0;
      return SVN_NO_ERROR;
    }

  /* Same path in same txn? */
  if (svn_fs_x__id_eq(&a->noderev_id, &b->noderev_id))
    {
      *equal = TRUE;
      return SVN_NO_ERROR;
    }

  /* Skip the expensive bits unless we are in strict mode.
     Simply assume that there is a different. */
  if (!strict)
    {
      *equal = FALSE;
      return SVN_NO_ERROR;
    }

  /* At least one of the reps has been modified in a txn.
     Fetch and compare them. */
  SVN_ERR(svn_fs_x__get_proplist(&proplist_a, fs, a, scratch_pool,
                                 scratch_pool));
  SVN_ERR(svn_fs_x__get_proplist(&proplist_b, fs, b, scratch_pool,
                                 scratch_pool));

  *equal = svn_fs__prop_lists_equal(proplist_a, proplist_b, scratch_pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__file_checksum(svn_checksum_t **checksum,
                        svn_fs_x__noderev_t *noderev,
                        svn_checksum_kind_t kind,
                        apr_pool_t *result_pool)
{
  *checksum = NULL;

  if (noderev->data_rep)
    {
      svn_checksum_t temp;
      temp.kind = kind;

      switch(kind)
        {
          case svn_checksum_md5:
            temp.digest = noderev->data_rep->md5_digest;
            break;

          case svn_checksum_sha1:
            if (! noderev->data_rep->has_sha1)
              return SVN_NO_ERROR;

            temp.digest = noderev->data_rep->sha1_digest;
            break;

          default:
            return SVN_NO_ERROR;
        }

      *checksum = svn_checksum_dup(&temp, result_pool);
    }

  return SVN_NO_ERROR;
}

svn_fs_x__representation_t *
svn_fs_x__rep_copy(svn_fs_x__representation_t *rep,
                   apr_pool_t *result_pool)
{
  if (rep == NULL)
    return NULL;

  return apr_pmemdup(result_pool, rep, sizeof(*rep));
}


/* Write out the zeroth revision for filesystem FS.
   Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
write_revision_zero(svn_fs_t *fs,
                    apr_pool_t *scratch_pool)
{
  const char *path_revision_zero = svn_fs_x__path_rev(fs, 0, scratch_pool);
  apr_hash_t *proplist;
  svn_string_t date;

  apr_array_header_t *index_entries;
  svn_fs_x__p2l_entry_t *entry;
  svn_fs_x__revision_file_t *rev_file;
  apr_file_t *apr_file;
  const char *l2p_proto_index, *p2l_proto_index;

  /* Construct a skeleton r0 with no indexes. */
  svn_string_t *noderev_str = svn_string_create("id: 2+0\n"
                                                "node: 0+0\n"
                                                "copy: 0+0\n"
                                                "type: dir\n"
                                                "count: 0\n"
                                                "cpath: /\n"
                                                "\n",
                                                scratch_pool);
  svn_string_t *changes_str = svn_string_create("\n",
                                                scratch_pool);
  svn_string_t *r0 = svn_string_createf(scratch_pool, "%s%s",
                                        noderev_str->data,
                                        changes_str->data);

  /* Write skeleton r0 to disk. */
  SVN_ERR(svn_io_file_create(path_revision_zero, r0->data, scratch_pool));

  /* Construct the index P2L contents: describe the 2 items we have.
     Be sure to create them in on-disk order. */
  index_entries = apr_array_make(scratch_pool, 2, sizeof(entry));

  entry = apr_pcalloc(scratch_pool, sizeof(*entry));
  entry->offset = 0;
  entry->size = (apr_off_t)noderev_str->len;
  entry->type = SVN_FS_X__ITEM_TYPE_NODEREV;
  entry->item_count = 1;
  entry->items = apr_pcalloc(scratch_pool, sizeof(*entry->items));
  entry->items[0].change_set = 0;
  entry->items[0].number = SVN_FS_X__ITEM_INDEX_ROOT_NODE;
  APR_ARRAY_PUSH(index_entries, svn_fs_x__p2l_entry_t *) = entry;

  entry = apr_pcalloc(scratch_pool, sizeof(*entry));
  entry->offset = (apr_off_t)noderev_str->len;
  entry->size = (apr_off_t)changes_str->len;
  entry->type = SVN_FS_X__ITEM_TYPE_CHANGES;
  entry->item_count = 1;
  entry->items = apr_pcalloc(scratch_pool, sizeof(*entry->items));
  entry->items[0].change_set = 0;
  entry->items[0].number = SVN_FS_X__ITEM_INDEX_CHANGES;
  APR_ARRAY_PUSH(index_entries, svn_fs_x__p2l_entry_t *) = entry;

  /* Now re-open r0, create proto-index files from our entries and
     rewrite the index section of r0. */
  SVN_ERR(svn_fs_x__rev_file_open_writable(&rev_file, fs, 0,
                                           scratch_pool, scratch_pool));
  SVN_ERR(svn_fs_x__p2l_index_from_p2l_entries(&p2l_proto_index, fs,
                                               rev_file, index_entries,
                                               scratch_pool, scratch_pool));
  SVN_ERR(svn_fs_x__l2p_index_from_p2l_entries(&l2p_proto_index, fs,
                                               index_entries,
                                               scratch_pool, scratch_pool));
  SVN_ERR(svn_fs_x__rev_file_get(&apr_file, rev_file));
  SVN_ERR(svn_fs_x__add_index_data(fs, apr_file, l2p_proto_index,
                                   p2l_proto_index, 0, scratch_pool));
  SVN_ERR(svn_fs_x__close_revision_file(rev_file));

  SVN_ERR(svn_io_set_file_read_only(path_revision_zero, FALSE, scratch_pool));

  /* Set a date on revision 0. */
  date.data = svn_time_to_cstring(apr_time_now(), scratch_pool);
  date.len = strlen(date.data);
  proplist = apr_hash_make(scratch_pool);
  svn_hash_sets(proplist, SVN_PROP_REVISION_DATE, &date);

  SVN_ERR(svn_io_file_open(&apr_file,
                           svn_fs_x__path_revprops(fs, 0, scratch_pool),
                           APR_WRITE | APR_CREATE, APR_OS_DEFAULT, 
                           scratch_pool));
  SVN_ERR(svn_fs_x__write_non_packed_revprops(apr_file, proplist,
                                              scratch_pool));
  SVN_ERR(svn_io_file_close(apr_file, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__create_file_tree(svn_fs_t *fs,
                           const char *path,
                           int format,
                           int shard_size,
                           apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;

  fs->path = apr_pstrdup(fs->pool, path);
  ffd->format = format;

  /* Use an appropriate sharding mode if supported by the format. */
  ffd->max_files_per_dir = shard_size;

  /* Create the revision data directories. */
  SVN_ERR(svn_io_make_dir_recursively(
                              svn_fs_x__path_shard(fs, 0, scratch_pool),
                              scratch_pool));

  /* Create the transaction directory. */
  SVN_ERR(svn_io_make_dir_recursively(
                                  svn_fs_x__path_txns_dir(fs, scratch_pool),
                                  scratch_pool));

  /* Create the protorevs directory. */
  SVN_ERR(svn_io_make_dir_recursively(
                            svn_fs_x__path_txn_proto_revs(fs, scratch_pool),
                            scratch_pool));

  /* Create the 'current' file. */
  SVN_ERR(svn_io_file_create(svn_fs_x__path_current(fs, scratch_pool),
                             "0\n", scratch_pool));

  /* Create the 'uuid' file. */
  SVN_ERR(svn_io_file_create_empty(svn_fs_x__path_lock(fs, scratch_pool),
                                   scratch_pool));
  SVN_ERR(svn_fs_x__set_uuid(fs, NULL, NULL, FALSE, scratch_pool));

  /* Create the fsfs.conf file. */
  SVN_ERR(write_config(fs, scratch_pool));
  SVN_ERR(read_config(ffd, fs->path, fs->pool, scratch_pool));

  /* Global configuration options. */
  SVN_ERR(read_global_config(fs));

  /* Add revision 0. */
  SVN_ERR(write_revision_zero(fs, scratch_pool));

  /* Create the min unpacked rev file. */
  SVN_ERR(svn_io_file_create(
                          svn_fs_x__path_min_unpacked_rev(fs, scratch_pool),
                          "0\n", scratch_pool));

  /* Create the txn-current file if the repository supports
     the transaction sequence file. */
  SVN_ERR(svn_io_file_create(svn_fs_x__path_txn_current(fs, scratch_pool),
                             "0\n", scratch_pool));
  SVN_ERR(svn_io_file_create_empty(
                          svn_fs_x__path_txn_current_lock(fs, scratch_pool),
                          scratch_pool));

  /* Initialize the revprop caching info. */
  SVN_ERR(svn_io_file_create_empty(
                        svn_fs_x__path_revprop_generation(fs, scratch_pool),
                        scratch_pool));
  SVN_ERR(svn_fs_x__reset_revprop_generation_file(fs, scratch_pool));

  ffd->youngest_rev_cache = 0;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__create(svn_fs_t *fs,
                 const char *path,
                 apr_pool_t *scratch_pool)
{
  int format = SVN_FS_X__FORMAT_NUMBER;
  svn_fs_x__data_t *ffd = fs->fsap_data;

  fs->path = apr_pstrdup(fs->pool, path);
  /* See if compatibility with older versions was explicitly requested. */
  if (fs->config)
    {
      svn_version_t *compatible_version;
      SVN_ERR(svn_fs__compatible_version(&compatible_version, fs->config,
                                         scratch_pool));

      /* select format number */
      switch(compatible_version->minor)
        {
          case 0:
          case 1:
          case 2:
          case 3:
          case 4:
          case 5:
          case 6:
          case 7:
          case 8: return svn_error_create(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                  _("FSX is not compatible with Subversion prior to 1.9"));

          default:format = SVN_FS_X__FORMAT_NUMBER;
        }
    }

  /* Actual FS creation. */
  SVN_ERR(svn_fs_x__create_file_tree(fs, path, format,
                                     SVN_FS_X_DEFAULT_MAX_FILES_PER_DIR,
                                     scratch_pool));

  /* This filesystem is ready.  Stamp it with a format number. */
  SVN_ERR(svn_fs_x__write_format(fs, FALSE, scratch_pool));

  ffd->youngest_rev_cache = 0;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__set_uuid(svn_fs_t *fs,
                   const char *uuid,
                   const char *instance_id,
                   svn_boolean_t overwrite,
                   apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  const char *uuid_path = svn_fs_x__path_uuid(fs, scratch_pool);
  svn_stringbuf_t *contents = svn_stringbuf_create_empty(scratch_pool);

  if (! uuid)
    uuid = svn_uuid_generate(scratch_pool);

  if (! instance_id)
    instance_id = svn_uuid_generate(scratch_pool);

  svn_stringbuf_appendcstr(contents, uuid);
  svn_stringbuf_appendcstr(contents, "\n");
  svn_stringbuf_appendcstr(contents, instance_id);
  svn_stringbuf_appendcstr(contents, "\n");

  /* We use the permissions of the 'current' file, because the 'uuid'
     file does not exist during repository creation.

     svn_io_write_atomic2() does a load of magic to allow it to
     replace version files that already exist.  We only need to do
     that when we're allowed to overwrite an existing file. */
  if (! overwrite)
    {
      /* Create the file */
      SVN_ERR(svn_io_file_create(uuid_path, contents->data, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_io_write_atomic2(uuid_path, contents->data, contents->len,
                                   /* perms */
                                   svn_fs_x__path_current(fs, scratch_pool),
                                   ffd->flush_to_disk, scratch_pool));
    }

  fs->uuid = apr_pstrdup(fs->pool, uuid);
  ffd->instance_id = apr_pstrdup(fs->pool, instance_id);

  return SVN_NO_ERROR;
}

/** Node origin lazy cache. */

/* If directory PATH does not exist, create it and give it the same
   permissions as FS_path.*/
svn_error_t *
svn_fs_x__ensure_dir_exists(const char *path,
                            const char *fs_path,
                            apr_pool_t *scratch_pool)
{
  svn_error_t *err = svn_io_dir_make(path, APR_OS_DEFAULT, scratch_pool);
  if (err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  /* We successfully created a new directory.  Dup the permissions
     from FS->path. */
  return svn_io_copy_perms(fs_path, path, scratch_pool);
}


/*** Revisions ***/

svn_error_t *
svn_fs_x__revision_prop(svn_string_t **value_p,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        const char *propname,
                        svn_boolean_t refresh,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  apr_hash_t *table;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR(svn_fs_x__get_revision_proplist(&table, fs, rev, FALSE, refresh,
                                          scratch_pool, scratch_pool));

  *value_p = svn_string_dup(svn_hash_gets(table, propname), result_pool);

  return SVN_NO_ERROR;
}


/* Baton used for change_rev_prop_body below. */
typedef struct change_rev_prop_baton_t {
  svn_fs_t *fs;
  svn_revnum_t rev;
  const char *name;
  const svn_string_t *const *old_value_p;
  const svn_string_t *value;
} change_rev_prop_baton_t;

/* The work-horse for svn_fs_x__change_rev_prop, called with the FS
   write lock.  This implements the svn_fs_x__with_write_lock()
   'body' callback type.  BATON is a 'change_rev_prop_baton_t *'. */
static svn_error_t *
change_rev_prop_body(void *baton,
                     apr_pool_t *scratch_pool)
{
  change_rev_prop_baton_t *cb = baton;
  apr_hash_t *table;
  const svn_string_t *present_value;

  /* Read current revprop values from disk (never from cache).
     Even if somehow the cache got out of sync, we want to make sure that
     we read, update and write up-to-date data. */
  SVN_ERR(svn_fs_x__get_revision_proplist(&table, cb->fs, cb->rev, TRUE,
                                          TRUE, scratch_pool, scratch_pool));
  present_value = svn_hash_gets(table, cb->name);

  if (cb->old_value_p)
    {
      const svn_string_t *wanted_value = *cb->old_value_p;
      if ((!wanted_value != !present_value)
          || (wanted_value && present_value
              && !svn_string_compare(wanted_value, present_value)))
        {
          /* What we expected isn't what we found. */
          return svn_error_createf(SVN_ERR_FS_PROP_BASEVALUE_MISMATCH, NULL,
                                   _("revprop '%s' has unexpected value in "
                                     "filesystem"),
                                   cb->name);
        }
      /* Fall through. */
    }

  /* If the prop-set is a no-op, skip the actual write. */
  if ((!present_value && !cb->value)
      || (present_value && cb->value
          && svn_string_compare(present_value, cb->value)))
    return SVN_NO_ERROR;

  svn_hash_sets(table, cb->name, cb->value);

  return svn_fs_x__set_revision_proplist(cb->fs, cb->rev, table,
                                         scratch_pool);
}

svn_error_t *
svn_fs_x__change_rev_prop(svn_fs_t *fs,
                          svn_revnum_t rev,
                          const char *name,
                          const svn_string_t *const *old_value_p,
                          const svn_string_t *value,
                          apr_pool_t *scratch_pool)
{
  change_rev_prop_baton_t cb;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  cb.fs = fs;
  cb.rev = rev;
  cb.name = name;
  cb.old_value_p = old_value_p;
  cb.value = value;

  return svn_fs_x__with_write_lock(fs, change_rev_prop_body, &cb,
                                   scratch_pool);
}


svn_error_t *
svn_fs_x__info_format(int *fs_format,
                      svn_version_t **supports_version,
                      svn_fs_t *fs,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  *fs_format = ffd->format;
  *supports_version = apr_palloc(result_pool, sizeof(svn_version_t));

  (*supports_version)->major = SVN_VER_MAJOR;
  (*supports_version)->minor = 9;
  (*supports_version)->patch = 0;
  (*supports_version)->tag = "";

  switch (ffd->format)
    {
    case 1:
      break;
    case 2:
      (*supports_version)->minor = 10;
      break;
#ifdef SVN_DEBUG
# if SVN_FS_X__FORMAT_NUMBER != 2
#  error "Need to add a 'case' statement here"
# endif
#endif
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__info_config_files(apr_array_header_t **files,
                            svn_fs_t *fs,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  *files = apr_array_make(result_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(*files, const char *) = svn_dirent_join(fs->path, PATH_CONFIG,
                                                         result_pool);
  return SVN_NO_ERROR;
}
