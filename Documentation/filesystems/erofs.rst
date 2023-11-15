.. SPDX-License-Identifier: GPL-2.0

======================================
EROFS - Enhanced Read-Only File System
======================================

Overview
========

EROFS filesystem stands for Enhanced Read-Only File System.  It aims to form a
generic read-only filesystem solution for various read-only use cases instead
of just focusing on storage space saving without considering any side effects
of runtime performance.

It is designed to meet the needs of flexibility, feature extendability and user
payload friendly, etc.  Apart from those, it is still kept as a simple
random-access friendly high-performance filesystem to get rid of unneeded I/O
amplification and memory-resident overhead compared to similar approaches.

It is implemented to be a better choice for the following scenarios:

 - read-only storage media or

 - part of a fully trusted read-only solution, which means it needs to be
   immutable and bit-for-bit identical to the official golden image for
   their releases due to security or other considerations and

 - hope to minimize extra storage space with guaranteed end-to-end performance
   by using compact layout, transparent file compression and direct access,
   especially for those embedded devices with limited memory and high-density
   hosts with numerous containers.

Here are the main features of EROFS:

 - Little endian on-disk design;

 - Block-based distribution and file-based distribution over fscache are
   supported;

 - Support multiple devices to refer to external blobs, which can be used
   for container images;

 - 32-bit block addresses for each device, therefore 16TiB address space at
   most with 4KiB block size for now;

 - Two inode layouts for different requirements:

   =====================  ============  ======================================
                          compact (v1)  extended (v2)
   =====================  ============  ======================================
   Inode metadata size    32 bytes      64 bytes
   Max file size          4 GiB         16 EiB (also limited by max. vol size)
   Max uids/gids          65536         4294967296
   Per-inode timestamp    no            yes (64 + 32-bit timestamp)
   Max hardlinks          65536         4294967296
   Metadata reserved      8 bytes       18 bytes
   =====================  ============  ======================================

 - Support extended attributes as an option;

 - Support a bloom filter that speeds up negative extended attribute lookups;

 - Support POSIX.1e ACLs by using extended attributes;

 - Support transparent data compression as an option:
   LZ4, MicroLZMA and DEFLATE algorithms can be used on a per-file basis; In
   addition, inplace decompression is also supported to avoid bounce compressed
   buffers and unnecessary page cache thrashing.

 - Support chunk-based data deduplication and rolling-hash compressed data
   deduplication;

 - Support tailpacking inline compared to byte-addressed unaligned metadata
   or smaller block size alternatives;

 - Support merging tail-end data into a special inode as fragments.

 - Support large folios for uncompressed files.

 - Support direct I/O on uncompressed files to avoid double caching for loop
   devices;

 - Support FSDAX on uncompressed images for secure containers and ramdisks in
   order to get rid of unnecessary page cache.

 - Support file-based on-demand loading with the Fscache infrastructure.

The following git tree provides the file system user-space tools under
development, such as a formatting tool (mkfs.erofs), an on-disk consistency &
compatibility checking tool (fsck.erofs), and a debugging tool (dump.erofs):

- git://git.kernel.org/pub/scm/linux/kernel/git/xiang/erofs-utils.git

Bugs and patches are welcome, please kindly help us and send to the following
linux-erofs mailing list:

- linux-erofs mailing list   <linux-erofs@lists.ozlabs.org>

Mount options
=============

===================    =========================================================
(no)user_xattr         Setup Extended User Attributes. Note: xattr is enabled
                       by default if CONFIG_EROFS_FS_XATTR is selected.
(no)acl                Setup POSIX Access Control List. Note: acl is enabled
                       by default if CONFIG_EROFS_FS_POSIX_ACL is selected.
cache_strategy=%s      Select a strategy for cached decompression from now on:

		       ==========  =============================================
                         disabled  In-place I/O decompression only;
                        readahead  Cache the last incomplete compressed physical
                                   cluster for further reading. It still does
                                   in-place I/O decompression for the rest
                                   compressed physical clusters;
                       readaround  Cache the both ends of incomplete compressed
                                   physical clusters for further reading.
                                   It still does in-place I/O decompression
                                   for the rest compressed physical clusters.
		       ==========  =============================================
dax={always,never}     Use direct access (no page cache).  See
                       Documentation/filesystems/dax.rst.
dax                    A legacy option which is an alias for ``dax=always``.
device=%s              Specify a path to an extra device to be used together.
fsid=%s                Specify a filesystem image ID for Fscache back-end.
domain_id=%s           Specify a domain ID in fscache mode so that different images
                       with the same blobs under a given domain ID can share storage.
===================    =========================================================

Sysfs Entries
=============

Information about mounted erofs file systems can be found in /sys/fs/erofs.
Each mounted filesystem will have a directory in /sys/fs/erofs based on its
device name (i.e., /sys/fs/erofs/sda).
(see also Documentation/ABI/testing/sysfs-fs-erofs)

On-disk details
===============

Summary
-------
Different from other read-only file systems, an EROFS volume is designed
to be as simple as possible::

                                |-> aligned with the block size
   ____________________________________________________________
  | |SB| | ... | Metadata | ... | Data | Metadata | ... | Data |
  |_|__|_|_____|__________|_____|______|__________|_____|______|
  0 +1K

All data areas should be aligned with the block size, but metadata areas
may not. All metadatas can be now observed in two different spaces (views):

 1. Inode metadata space

    Each valid inode should be aligned with an inode slot, which is a fixed
    value (32 bytes) and designed to be kept in line with compact inode size.

    Each inode can be directly found with the following formula:
         inode offset = meta_blkaddr * block_size + 32 * nid

    ::

                                 |-> aligned with 8B
                                            |-> followed closely
     + meta_blkaddr blocks                                      |-> another slot
       _____________________________________________________________________
     |  ...   | inode |  xattrs  | extents  | data inline | ... | inode ...
     |________|_______|(optional)|(optional)|__(optional)_|_____|__________
              |-> aligned with the inode slot size
                   .                   .
                 .                         .
               .                              .
             .                                    .
           .                                         .
         .                                              .
       .____________________________________________________|-> aligned with 4B
       | xattr_ibody_header | shared xattrs | inline xattrs |
       |____________________|_______________|_______________|
       |->    12 bytes    <-|->x * 4 bytes<-|               .
                           .                .                 .
                     .                      .                   .
                .                           .                     .
            ._______________________________.______________________.
            | id | id | id | id |  ... | id | ent | ... | ent| ... |
            |____|____|____|____|______|____|_____|_____|____|_____|
                                            |-> aligned with 4B
                                                        |-> aligned with 4B

    Inode could be 32 or 64 bytes, which can be distinguished from a common
    field which all inode versions have -- i_format::

        __________________               __________________
       |     i_format     |             |     i_format     |
       |__________________|             |__________________|
       |        ...       |             |        ...       |
       |                  |             |                  |
       |__________________| 32 bytes    |                  |
                                        |                  |
                                        |__________________| 64 bytes

    Xattrs, extents, data inline are placed after the corresponding inode with
    proper alignment, and they could be optional for different data mappings.
    _currently_ total 5 data layouts are supported:

    ==  ====================================================================
     0  flat file data without data inline (no extent);
     1  fixed-sized output data compression (with non-compacted indexes);
     2  flat file data with tail packing data inline (no extent);
     3  fixed-sized output data compression (with compacted indexes, v5.3+);
     4  chunk-based file (v5.15+).
    ==  ====================================================================

    The size of the optional xattrs is indicated by i_xattr_count in inode
    header. Large xattrs or xattrs shared by many different files can be
    stored in shared xattrs metadata rather than inlined right after inode.

 2. Shared xattrs metadata space

    Shared xattrs space is similar to the above inode space, started with
    a specific block indicated by xattr_blkaddr, organized one by one with
    proper align.

    Each share xattr can also be directly found by the following formula:
         xattr offset = xattr_blkaddr * block_size + 4 * xattr_id

::

                           |-> aligned by  4 bytes
    + xattr_blkaddr blocks                     |-> aligned with 4 bytes
     _________________________________________________________________________
    |  ...   | xattr_entry |  xattr data | ... |  xattr_entry | xattr data  ...
    |________|_____________|_____________|_____|______________|_______________

Directories
-----------
All directories are now organized in a compact on-disk format. Note that
each directory block is divided into index and name areas in order to support
random file lookup, and all directory entries are _strictly_ recorded in
alphabetical order in order to support improved prefix binary search
algorithm (could refer to the related source code).

::

                  ___________________________
                 /                           |
                /              ______________|________________
               /              /              | nameoff1       | nameoffN-1
  ____________.______________._______________v________________v__________
 | dirent | dirent | ... | dirent | filename | filename | ... | filename |
 |___.0___|____1___|_____|___N-1__|____0_____|____1_____|_____|___N-1____|
      \                           ^
       \                          |                           * could have
        \                         |                             trailing '\0'
         \________________________| nameoff0
                             Directory block

Note that apart from the offset of the first filename, nameoff0 also indicates
the total number of directory entries in this block since it is no need to
introduce another on-disk field at all.

Chunk-based files
-----------------
In order to support chunk-based data deduplication, a new inode data layout has
been supported since Linux v5.15: Files are split in equal-sized data chunks
with ``extents`` area of the inode metadata indicating how to get the chunk
data: these can be simply as a 4-byte block address array or in the 8-byte
chunk index form (see struct erofs_inode_chunk_index in erofs_fs.h for more
details.)

By the way, chunk-based files are all uncompressed for now.

Long extended attribute name prefixes
-------------------------------------
There are use cases where extended attributes with different values can have
only a few common prefixes (such as overlayfs xattrs).  The predefined prefixes
work inefficiently in both image size and runtime performance in such cases.

The long xattr name prefixes feature is introduced to address this issue.  The
overall idea is that, apart from the existing predefined prefixes, the xattr
entry could also refer to user-specified long xattr name prefixes, e.g.
"trusted.overlay.".

When referring to a long xattr name prefix, the highest bit (bit 7) of
erofs_xattr_entry.e_name_index is set, while the lower bits (bit 0-6) as a whole
represent the index of the referred long name prefix among all long name
prefixes.  Therefore, only the trailing part of the name apart from the long
xattr name prefix is stored in erofs_xattr_entry.e_name, which could be empty if
the full xattr name matches exactly as its long xattr name prefix.

All long xattr prefixes are stored one by one in the packed inode as long as
the packed inode is valid, or in the meta inode otherwise.  The
xattr_prefix_count (of the on-disk superblock) indicates the total number of
long xattr name prefixes, while (xattr_prefix_start * 4) indicates the start
offset of long name prefixes in the packed/meta inode.  Note that, long extended
attribute name prefixes are disabled if xattr_prefix_count is 0.

Each long name prefix is stored in the format: ALIGN({__le16 len, data}, 4),
where len represents the total size of the data part.  The data part is actually
represented by 'struct erofs_xattr_long_prefix', where base_index represents the
index of the predefined xattr name prefix, e.g. EROFS_XATTR_INDEX_TRUSTED for
"trusted.overlay." long name prefix, while the infix string keeps the string
after stripping the short prefix, e.g. "overlay." for the example above.

Data compression
----------------
EROFS implements fixed-sized output compression which generates fixed-sized
compressed data blocks from variable-sized input in contrast to other existing
fixed-sized input solutions. Relatively higher compression ratios can be gotten
by using fixed-sized output compression since nowadays popular data compression
algorithms are mostly LZ77-based and such fixed-sized output approach can be
benefited from the historical dictionary (aka. sliding window).

In details, original (uncompressed) data is turned into several variable-sized
extents and in the meanwhile, compressed into physical clusters (pclusters).
In order to record each variable-sized extent, logical clusters (lclusters) are
introduced as the basic unit of compress indexes to indicate whether a new
extent is generated within the range (HEAD) or not (NONHEAD). Lclusters are now
fixed in block size, as illustrated below::

          |<-    variable-sized extent    ->|<-       VLE         ->|
        clusterofs                        clusterofs              clusterofs
          |                                 |                       |
 _________v_________________________________v_______________________v________
 ... |    .         |              |        .     |              |  .   ...
 ____|____._________|______________|________.___ _|______________|__.________
     |-> lcluster <-|-> lcluster <-|-> lcluster <-|-> lcluster <-|
          (HEAD)        (NONHEAD)       (HEAD)        (NONHEAD)    .
           .             CBLKCNT            .                    .
            .                               .                  .
             .                              .                .
       _______._____________________________.______________._________________
          ... |              |              |              | ...
       _______|______________|______________|______________|_________________
              |->      big pcluster       <-|-> pcluster <-|

A physical cluster can be seen as a container of physical compressed blocks
which contains compressed data. Previously, only lcluster-sized (4KB) pclusters
were supported. After big pcluster feature is introduced (available since
Linux v5.13), pcluster can be a multiple of lcluster size.

For each HEAD lcluster, clusterofs is recorded to indicate where a new extent
starts and blkaddr is used to seek the compressed data. For each NONHEAD
lcluster, delta0 and delta1 are available instead of blkaddr to indicate the
distance to its HEAD lcluster and the next HEAD lcluster. A PLAIN lcluster is
also a HEAD lcluster except that its data is uncompressed. See the comments
around "struct z_erofs_vle_decompressed_index" in erofs_fs.h for more details.

If big pcluster is enabled, pcluster size in lclusters needs to be recorded as
well. Let the delta0 of the first NONHEAD lcluster store the compressed block
count with a special flag as a new called CBLKCNT NONHEAD lcluster. It's easy
to understand its delta0 is constantly 1, as illustrated below::

   __________________________________________________________
  | HEAD |  NONHEAD  | NONHEAD | ... | NONHEAD | HEAD | HEAD |
  |__:___|_(CBLKCNT)_|_________|_____|_________|__:___|____:_|
     |<----- a big pcluster (with CBLKCNT) ------>|<--  -->|
           a lcluster-sized pcluster (without CBLKCNT) ^

If another HEAD follows a HEAD lcluster, there is no room to record CBLKCNT,
but it's easy to know the size of such pcluster is 1 lcluster as well.

Since Linux v6.1, each pcluster can be used for multiple variable-sized extents,
therefore it can be used for compressed data deduplication.
