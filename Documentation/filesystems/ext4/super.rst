.. SPDX-License-Identifier: GPL-2.0

Super Block
-----------

The superblock records various information about the enclosing
filesystem, such as block counts, inode counts, supported features,
maintenance information, and more.

If the sparse\_super feature flag is set, redundant copies of the
superblock and group descriptors are kept only in the groups whose group
number is either 0 or a power of 3, 5, or 7. If the flag is not set,
redundant copies are kept in all groups.

The superblock checksum is calculated against the superblock structure,
which includes the FS UUID.

The ext4 superblock is laid out as follows in
``struct ext4_super_block``:

.. list-table::
   :widths: 8 8 24 40
   :header-rows: 1

   * - Offset
     - Size
     - Name
     - Description
   * - 0x0
     - \_\_le32
     - s\_inodes\_count
     - Total inode count.
   * - 0x4
     - \_\_le32
     - s\_blocks\_count\_lo
     - Total block count.
   * - 0x8
     - \_\_le32
     - s\_r\_blocks\_count\_lo
     - This number of blocks can only be allocated by the super-user.
   * - 0xC
     - \_\_le32
     - s\_free\_blocks\_count\_lo
     - Free block count.
   * - 0x10
     - \_\_le32
     - s\_free\_inodes\_count
     - Free inode count.
   * - 0x14
     - \_\_le32
     - s\_first\_data\_block
     - First data block. This must be at least 1 for 1k-block filesystems and
       is typically 0 for all other block sizes.
   * - 0x18
     - \_\_le32
     - s\_log\_block\_size
     - Block size is 2 ^ (10 + s\_log\_block\_size).
   * - 0x1C
     - \_\_le32
     - s\_log\_cluster\_size
     - Cluster size is 2 ^ (10 + s\_log\_cluster\_size) blocks if bigalloc is
       enabled. Otherwise s\_log\_cluster\_size must equal s\_log\_block\_size.
   * - 0x20
     - \_\_le32
     - s\_blocks\_per\_group
     - Blocks per group.
   * - 0x24
     - \_\_le32
     - s\_clusters\_per\_group
     - Clusters per group, if bigalloc is enabled. Otherwise
       s\_clusters\_per\_group must equal s\_blocks\_per\_group.
   * - 0x28
     - \_\_le32
     - s\_inodes\_per\_group
     - Inodes per group.
   * - 0x2C
     - \_\_le32
     - s\_mtime
     - Mount time, in seconds since the epoch.
   * - 0x30
     - \_\_le32
     - s\_wtime
     - Write time, in seconds since the epoch.
   * - 0x34
     - \_\_le16
     - s\_mnt\_count
     - Number of mounts since the last fsck.
   * - 0x36
     - \_\_le16
     - s\_max\_mnt\_count
     - Number of mounts beyond which a fsck is needed.
   * - 0x38
     - \_\_le16
     - s\_magic
     - Magic signature, 0xEF53
   * - 0x3A
     - \_\_le16
     - s\_state
     - File system state. See super_state_ for more info.
   * - 0x3C
     - \_\_le16
     - s\_errors
     - Behaviour when detecting errors. See super_errors_ for more info.
   * - 0x3E
     - \_\_le16
     - s\_minor\_rev\_level
     - Minor revision level.
   * - 0x40
     - \_\_le32
     - s\_lastcheck
     - Time of last check, in seconds since the epoch.
   * - 0x44
     - \_\_le32
     - s\_checkinterval
     - Maximum time between checks, in seconds.
   * - 0x48
     - \_\_le32
     - s\_creator\_os
     - Creator OS. See the table super_creator_ for more info.
   * - 0x4C
     - \_\_le32
     - s\_rev\_level
     - Revision level. See the table super_revision_ for more info.
   * - 0x50
     - \_\_le16
     - s\_def\_resuid
     - Default uid for reserved blocks.
   * - 0x52
     - \_\_le16
     - s\_def\_resgid
     - Default gid for reserved blocks.
   * -
     -
     -
     - These fields are for EXT4_DYNAMIC_REV superblocks only.
       
       Note: the difference between the compatible feature set and the
       incompatible feature set is that if there is a bit set in the
       incompatible feature set that the kernel doesn't know about, it should
       refuse to mount the filesystem.
       
       e2fsck's requirements are more strict; if it doesn't know
       about a feature in either the compatible or incompatible feature set, it
       must abort and not try to meddle with things it doesn't understand...
   * - 0x54
     - \_\_le32
     - s\_first\_ino
     - First non-reserved inode.
   * - 0x58
     - \_\_le16
     - s\_inode\_size
     - Size of inode structure, in bytes.
   * - 0x5A
     - \_\_le16
     - s\_block\_group\_nr
     - Block group # of this superblock.
   * - 0x5C
     - \_\_le32
     - s\_feature\_compat
     - Compatible feature set flags. Kernel can still read/write this fs even
       if it doesn't understand a flag; fsck should not do that. See the
       super_compat_ table for more info.
   * - 0x60
     - \_\_le32
     - s\_feature\_incompat
     - Incompatible feature set. If the kernel or fsck doesn't understand one
       of these bits, it should stop. See the super_incompat_ table for more
       info.
   * - 0x64
     - \_\_le32
     - s\_feature\_ro\_compat
     - Readonly-compatible feature set. If the kernel doesn't understand one of
       these bits, it can still mount read-only. See the super_rocompat_ table
       for more info.
   * - 0x68
     - \_\_u8
     - s\_uuid[16]
     - 128-bit UUID for volume.
   * - 0x78
     - char
     - s\_volume\_name[16]
     - Volume label.
   * - 0x88
     - char
     - s\_last\_mounted[64]
     - Directory where filesystem was last mounted.
   * - 0xC8
     - \_\_le32
     - s\_algorithm\_usage\_bitmap
     - For compression (Not used in e2fsprogs/Linux)
   * -
     -
     -
     - Performance hints.  Directory preallocation should only happen if the
       EXT4_FEATURE_COMPAT_DIR_PREALLOC flag is on.
   * - 0xCC
     - \_\_u8
     - s\_prealloc\_blocks
     - #. of blocks to try to preallocate for ... files? (Not used in
       e2fsprogs/Linux)
   * - 0xCD
     - \_\_u8
     - s\_prealloc\_dir\_blocks
     - #. of blocks to preallocate for directories. (Not used in
       e2fsprogs/Linux)
   * - 0xCE
     - \_\_le16
     - s\_reserved\_gdt\_blocks
     - Number of reserved GDT entries for future filesystem expansion.
   * -
     -
     -
     - Journalling support is valid only if EXT4_FEATURE_COMPAT_HAS_JOURNAL is
       set.
   * - 0xD0
     - \_\_u8
     - s\_journal\_uuid[16]
     - UUID of journal superblock
   * - 0xE0
     - \_\_le32
     - s\_journal\_inum
     - inode number of journal file.
   * - 0xE4
     - \_\_le32
     - s\_journal\_dev
     - Device number of journal file, if the external journal feature flag is
       set.
   * - 0xE8
     - \_\_le32
     - s\_last\_orphan
     - Start of list of orphaned inodes to delete.
   * - 0xEC
     - \_\_le32
     - s\_hash\_seed[4]
     - HTREE hash seed.
   * - 0xFC
     - \_\_u8
     - s\_def\_hash\_version
     - Default hash algorithm to use for directory hashes. See super_def_hash_
       for more info.
   * - 0xFD
     - \_\_u8
     - s\_jnl\_backup\_type
     - If this value is 0 or EXT3\_JNL\_BACKUP\_BLOCKS (1), then the
       ``s_jnl_blocks`` field contains a duplicate copy of the inode's
       ``i_block[]`` array and ``i_size``.
   * - 0xFE
     - \_\_le16
     - s\_desc\_size
     - Size of group descriptors, in bytes, if the 64bit incompat feature flag
       is set.
   * - 0x100
     - \_\_le32
     - s\_default\_mount\_opts
     - Default mount options. See the super_mountopts_ table for more info.
   * - 0x104
     - \_\_le32
     - s\_first\_meta\_bg
     - First metablock block group, if the meta\_bg feature is enabled.
   * - 0x108
     - \_\_le32
     - s\_mkfs\_time
     - When the filesystem was created, in seconds since the epoch.
   * - 0x10C
     - \_\_le32
     - s\_jnl\_blocks[17]
     - Backup copy of the journal inode's ``i_block[]`` array in the first 15
       elements and i\_size\_high and i\_size in the 16th and 17th elements,
       respectively.
   * -
     -
     -
     - 64bit support is valid only if EXT4_FEATURE_COMPAT_64BIT is set.
   * - 0x150
     - \_\_le32
     - s\_blocks\_count\_hi
     - High 32-bits of the block count.
   * - 0x154
     - \_\_le32
     - s\_r\_blocks\_count\_hi
     - High 32-bits of the reserved block count.
   * - 0x158
     - \_\_le32
     - s\_free\_blocks\_count\_hi
     - High 32-bits of the free block count.
   * - 0x15C
     - \_\_le16
     - s\_min\_extra\_isize
     - All inodes have at least # bytes.
   * - 0x15E
     - \_\_le16
     - s\_want\_extra\_isize
     - New inodes should reserve # bytes.
   * - 0x160
     - \_\_le32
     - s\_flags
     - Miscellaneous flags. See the super_flags_ table for more info.
   * - 0x164
     - \_\_le16
     - s\_raid\_stride
     - RAID stride. This is the number of logical blocks read from or written
       to the disk before moving to the next disk. This affects the placement
       of filesystem metadata, which will hopefully make RAID storage faster.
   * - 0x166
     - \_\_le16
     - s\_mmp\_interval
     - #. seconds to wait in multi-mount prevention (MMP) checking. In theory,
       MMP is a mechanism to record in the superblock which host and device
       have mounted the filesystem, in order to prevent multiple mounts. This
       feature does not seem to be implemented...
   * - 0x168
     - \_\_le64
     - s\_mmp\_block
     - Block # for multi-mount protection data.
   * - 0x170
     - \_\_le32
     - s\_raid\_stripe\_width
     - RAID stripe width. This is the number of logical blocks read from or
       written to the disk before coming back to the current disk. This is used
       by the block allocator to try to reduce the number of read-modify-write
       operations in a RAID5/6.
   * - 0x174
     - \_\_u8
     - s\_log\_groups\_per\_flex
     - Size of a flexible block group is 2 ^ ``s_log_groups_per_flex``.
   * - 0x175
     - \_\_u8
     - s\_checksum\_type
     - Metadata checksum algorithm type. The only valid value is 1 (crc32c).
   * - 0x176
     - \_\_le16
     - s\_reserved\_pad
     -
   * - 0x178
     - \_\_le64
     - s\_kbytes\_written
     - Number of KiB written to this filesystem over its lifetime.
   * - 0x180
     - \_\_le32
     - s\_snapshot\_inum
     - inode number of active snapshot. (Not used in e2fsprogs/Linux.)
   * - 0x184
     - \_\_le32
     - s\_snapshot\_id
     - Sequential ID of active snapshot. (Not used in e2fsprogs/Linux.)
   * - 0x188
     - \_\_le64
     - s\_snapshot\_r\_blocks\_count
     - Number of blocks reserved for active snapshot's future use. (Not used in
       e2fsprogs/Linux.)
   * - 0x190
     - \_\_le32
     - s\_snapshot\_list
     - inode number of the head of the on-disk snapshot list. (Not used in
       e2fsprogs/Linux.)
   * - 0x194
     - \_\_le32
     - s\_error\_count
     - Number of errors seen.
   * - 0x198
     - \_\_le32
     - s\_first\_error\_time
     - First time an error happened, in seconds since the epoch.
   * - 0x19C
     - \_\_le32
     - s\_first\_error\_ino
     - inode involved in first error.
   * - 0x1A0
     - \_\_le64
     - s\_first\_error\_block
     - Number of block involved of first error.
   * - 0x1A8
     - \_\_u8
     - s\_first\_error\_func[32]
     - Name of function where the error happened.
   * - 0x1C8
     - \_\_le32
     - s\_first\_error\_line
     - Line number where error happened.
   * - 0x1CC
     - \_\_le32
     - s\_last\_error\_time
     - Time of most recent error, in seconds since the epoch.
   * - 0x1D0
     - \_\_le32
     - s\_last\_error\_ino
     - inode involved in most recent error.
   * - 0x1D4
     - \_\_le32
     - s\_last\_error\_line
     - Line number where most recent error happened.
   * - 0x1D8
     - \_\_le64
     - s\_last\_error\_block
     - Number of block involved in most recent error.
   * - 0x1E0
     - \_\_u8
     - s\_last\_error\_func[32]
     - Name of function where the most recent error happened.
   * - 0x200
     - \_\_u8
     - s\_mount\_opts[64]
     - ASCIIZ string of mount options.
   * - 0x240
     - \_\_le32
     - s\_usr\_quota\_inum
     - Inode number of user `quota <quota>`__ file.
   * - 0x244
     - \_\_le32
     - s\_grp\_quota\_inum
     - Inode number of group `quota <quota>`__ file.
   * - 0x248
     - \_\_le32
     - s\_overhead\_blocks
     - Overhead blocks/clusters in fs. (Huh? This field is always zero, which
       means that the kernel calculates it dynamically.)
   * - 0x24C
     - \_\_le32
     - s\_backup\_bgs[2]
     - Block groups containing superblock backups (if sparse\_super2)
   * - 0x254
     - \_\_u8
     - s\_encrypt\_algos[4]
     - Encryption algorithms in use. There can be up to four algorithms in use
       at any time; valid algorithm codes are given in the super_encrypt_ table
       below.
   * - 0x258
     - \_\_u8
     - s\_encrypt\_pw\_salt[16]
     - Salt for the string2key algorithm for encryption.
   * - 0x268
     - \_\_le32
     - s\_lpf\_ino
     - Inode number of lost+found
   * - 0x26C
     - \_\_le32
     - s\_prj\_quota\_inum
     - Inode that tracks project quotas.
   * - 0x270
     - \_\_le32
     - s\_checksum\_seed
     - Checksum seed used for metadata\_csum calculations. This value is
       crc32c(~0, $orig\_fs\_uuid).
   * - 0x274
     - \_\_u8
     - s\_wtime_hi
     - Upper 8 bits of the s_wtime field.
   * - 0x275
     - \_\_u8
     - s\_mtime_hi
     - Upper 8 bits of the s_mtime field.
   * - 0x276
     - \_\_u8
     - s\_mkfs_time_hi
     - Upper 8 bits of the s_mkfs_time field.
   * - 0x277
     - \_\_u8
     - s\_lastcheck_hi
     - Upper 8 bits of the s_lastcheck_hi field.
   * - 0x278
     - \_\_u8
     - s\_first_error_time_hi
     - Upper 8 bits of the s_first_error_time_hi field.
   * - 0x279
     - \_\_u8
     - s\_last_error_time_hi
     - Upper 8 bits of the s_last_error_time_hi field.
   * - 0x27A
     - \_\_u8
     - s\_pad[2]
     - Zero padding.
   * - 0x27C
     - \_\_le16
     - s\_encoding
     - Filename charset encoding.
   * - 0x27E
     - \_\_le16
     - s\_encoding_flags
     - Filename charset encoding flags.
   * - 0x280
     - \_\_le32
     - s\_orphan\_file\_inum
     - Orphan file inode number.
   * - 0x284
     - \_\_le32
     - s\_reserved[94]
     - Padding to the end of the block.
   * - 0x3FC
     - \_\_le32
     - s\_checksum
     - Superblock checksum.

.. _super_state:

The superblock state is some combination of the following:

.. list-table::
   :widths: 8 72
   :header-rows: 1

   * - Value
     - Description
   * - 0x0001
     - Cleanly umounted
   * - 0x0002
     - Errors detected
   * - 0x0004
     - Orphans being recovered

.. _super_errors:

The superblock error policy is one of the following:

.. list-table::
   :widths: 8 72
   :header-rows: 1

   * - Value
     - Description
   * - 1
     - Continue
   * - 2
     - Remount read-only
   * - 3
     - Panic

.. _super_creator:

The filesystem creator is one of the following:

.. list-table::
   :widths: 8 72
   :header-rows: 1

   * - Value
     - Description
   * - 0
     - Linux
   * - 1
     - Hurd
   * - 2
     - Masix
   * - 3
     - FreeBSD
   * - 4
     - Lites

.. _super_revision:

The superblock revision is one of the following:

.. list-table::
   :widths: 8 72
   :header-rows: 1

   * - Value
     - Description
   * - 0
     - Original format
   * - 1
     - v2 format w/ dynamic inode sizes

Note that ``EXT4_DYNAMIC_REV`` refers to a revision 1 or newer filesystem.

.. _super_compat:

The superblock compatible features field is a combination of any of the
following:

.. list-table::
   :widths: 16 64
   :header-rows: 1

   * - Value
     - Description
   * - 0x1
     - Directory preallocation (COMPAT\_DIR\_PREALLOC).
   * - 0x2
     - “imagic inodes”. Not clear from the code what this does
       (COMPAT\_IMAGIC\_INODES).
   * - 0x4
     - Has a journal (COMPAT\_HAS\_JOURNAL).
   * - 0x8
     - Supports extended attributes (COMPAT\_EXT\_ATTR).
   * - 0x10
     - Has reserved GDT blocks for filesystem expansion
       (COMPAT\_RESIZE\_INODE). Requires RO\_COMPAT\_SPARSE\_SUPER.
   * - 0x20
     - Has directory indices (COMPAT\_DIR\_INDEX).
   * - 0x40
     - “Lazy BG”. Not in Linux kernel, seems to have been for uninitialized
       block groups? (COMPAT\_LAZY\_BG)
   * - 0x80
     - “Exclude inode”. Not used. (COMPAT\_EXCLUDE\_INODE).
   * - 0x100
     - “Exclude bitmap”. Seems to be used to indicate the presence of
       snapshot-related exclude bitmaps? Not defined in kernel or used in
       e2fsprogs (COMPAT\_EXCLUDE\_BITMAP).
   * - 0x200
     - Sparse Super Block, v2. If this flag is set, the SB field s\_backup\_bgs
       points to the two block groups that contain backup superblocks
       (COMPAT\_SPARSE\_SUPER2).
   * - 0x400
     - Fast commits supported. Although fast commits blocks are
       backward incompatible, fast commit blocks are not always
       present in the journal. If fast commit blocks are present in
       the journal, JBD2 incompat feature
       (JBD2\_FEATURE\_INCOMPAT\_FAST\_COMMIT) gets
       set (COMPAT\_FAST\_COMMIT).
   * - 0x1000
     - Orphan file allocated. This is the special file for more efficient
       tracking of unlinked but still open inodes. When there may be any
       entries in the file, we additionally set proper rocompat feature
       (RO\_COMPAT\_ORPHAN\_PRESENT).

.. _super_incompat:

The superblock incompatible features field is a combination of any of the
following:

.. list-table::
   :widths: 16 64
   :header-rows: 1

   * - Value
     - Description
   * - 0x1
     - Compression (INCOMPAT\_COMPRESSION).
   * - 0x2
     - Directory entries record the file type. See ext4\_dir\_entry\_2 below
       (INCOMPAT\_FILETYPE).
   * - 0x4
     - Filesystem needs recovery (INCOMPAT\_RECOVER).
   * - 0x8
     - Filesystem has a separate journal device (INCOMPAT\_JOURNAL\_DEV).
   * - 0x10
     - Meta block groups. See the earlier discussion of this feature
       (INCOMPAT\_META\_BG).
   * - 0x40
     - Files in this filesystem use extents (INCOMPAT\_EXTENTS).
   * - 0x80
     - Enable a filesystem size of 2^64 blocks (INCOMPAT\_64BIT).
   * - 0x100
     - Multiple mount protection (INCOMPAT\_MMP).
   * - 0x200
     - Flexible block groups. See the earlier discussion of this feature
       (INCOMPAT\_FLEX\_BG).
   * - 0x400
     - Inodes can be used to store large extended attribute values
       (INCOMPAT\_EA\_INODE).
   * - 0x1000
     - Data in directory entry (INCOMPAT\_DIRDATA). (Not implemented?)
   * - 0x2000
     - Metadata checksum seed is stored in the superblock. This feature enables
       the administrator to change the UUID of a metadata\_csum filesystem
       while the filesystem is mounted; without it, the checksum definition
       requires all metadata blocks to be rewritten (INCOMPAT\_CSUM\_SEED).
   * - 0x4000
     - Large directory >2GB or 3-level htree (INCOMPAT\_LARGEDIR). Prior to
       this feature, directories could not be larger than 4GiB and could not
       have an htree more than 2 levels deep. If this feature is enabled,
       directories can be larger than 4GiB and have a maximum htree depth of 3.
   * - 0x8000
     - Data in inode (INCOMPAT\_INLINE\_DATA).
   * - 0x10000
     - Encrypted inodes are present on the filesystem. (INCOMPAT\_ENCRYPT).

.. _super_rocompat:

The superblock read-only compatible features field is a combination of any of
the following:

.. list-table::
   :widths: 16 64
   :header-rows: 1

   * - Value
     - Description
   * - 0x1
     - Sparse superblocks. See the earlier discussion of this feature
       (RO\_COMPAT\_SPARSE\_SUPER).
   * - 0x2
     - This filesystem has been used to store a file greater than 2GiB
       (RO\_COMPAT\_LARGE\_FILE).
   * - 0x4
     - Not used in kernel or e2fsprogs (RO\_COMPAT\_BTREE\_DIR).
   * - 0x8
     - This filesystem has files whose sizes are represented in units of
       logical blocks, not 512-byte sectors. This implies a very large file
       indeed! (RO\_COMPAT\_HUGE\_FILE)
   * - 0x10
     - Group descriptors have checksums. In addition to detecting corruption,
       this is useful for lazy formatting with uninitialized groups
       (RO\_COMPAT\_GDT\_CSUM).
   * - 0x20
     - Indicates that the old ext3 32,000 subdirectory limit no longer applies
       (RO\_COMPAT\_DIR\_NLINK). A directory's i\_links\_count will be set to 1
       if it is incremented past 64,999.
   * - 0x40
     - Indicates that large inodes exist on this filesystem
       (RO\_COMPAT\_EXTRA\_ISIZE).
   * - 0x80
     - This filesystem has a snapshot (RO\_COMPAT\_HAS\_SNAPSHOT).
   * - 0x100
     - `Quota <Quota>`__ (RO\_COMPAT\_QUOTA).
   * - 0x200
     - This filesystem supports “bigalloc”, which means that file extents are
       tracked in units of clusters (of blocks) instead of blocks
       (RO\_COMPAT\_BIGALLOC).
   * - 0x400
     - This filesystem supports metadata checksumming.
       (RO\_COMPAT\_METADATA\_CSUM; implies RO\_COMPAT\_GDT\_CSUM, though
       GDT\_CSUM must not be set)
   * - 0x800
     - Filesystem supports replicas. This feature is neither in the kernel nor
       e2fsprogs. (RO\_COMPAT\_REPLICA)
   * - 0x1000
     - Read-only filesystem image; the kernel will not mount this image
       read-write and most tools will refuse to write to the image.
       (RO\_COMPAT\_READONLY)
   * - 0x2000
     - Filesystem tracks project quotas. (RO\_COMPAT\_PROJECT)
   * - 0x8000
     - Verity inodes may be present on the filesystem. (RO\_COMPAT\_VERITY)
   * - 0x10000
     - Indicates orphan file may have valid orphan entries and thus we need
       to clean them up when mounting the filesystem
       (RO\_COMPAT\_ORPHAN\_PRESENT).

.. _super_def_hash:

The ``s_def_hash_version`` field is one of the following:

.. list-table::
   :widths: 8 72
   :header-rows: 1

   * - Value
     - Description
   * - 0x0
     - Legacy.
   * - 0x1
     - Half MD4.
   * - 0x2
     - Tea.
   * - 0x3
     - Legacy, unsigned.
   * - 0x4
     - Half MD4, unsigned.
   * - 0x5
     - Tea, unsigned.

.. _super_mountopts:

The ``s_default_mount_opts`` field is any combination of the following:

.. list-table::
   :widths: 8 72
   :header-rows: 1

   * - Value
     - Description
   * - 0x0001
     - Print debugging info upon (re)mount. (EXT4\_DEFM\_DEBUG)
   * - 0x0002
     - New files take the gid of the containing directory (instead of the fsgid
       of the current process). (EXT4\_DEFM\_BSDGROUPS)
   * - 0x0004
     - Support userspace-provided extended attributes. (EXT4\_DEFM\_XATTR\_USER)
   * - 0x0008
     - Support POSIX access control lists (ACLs). (EXT4\_DEFM\_ACL)
   * - 0x0010
     - Do not support 32-bit UIDs. (EXT4\_DEFM\_UID16)
   * - 0x0020
     - All data and metadata are commited to the journal.
       (EXT4\_DEFM\_JMODE\_DATA)
   * - 0x0040
     - All data are flushed to the disk before metadata are committed to the
       journal. (EXT4\_DEFM\_JMODE\_ORDERED)
   * - 0x0060
     - Data ordering is not preserved; data may be written after the metadata
       has been written. (EXT4\_DEFM\_JMODE\_WBACK)
   * - 0x0100
     - Disable write flushes. (EXT4\_DEFM\_NOBARRIER)
   * - 0x0200
     - Track which blocks in a filesystem are metadata and therefore should not
       be used as data blocks. This option will be enabled by default on 3.18,
       hopefully. (EXT4\_DEFM\_BLOCK\_VALIDITY)
   * - 0x0400
     - Enable DISCARD support, where the storage device is told about blocks
       becoming unused. (EXT4\_DEFM\_DISCARD)
   * - 0x0800
     - Disable delayed allocation. (EXT4\_DEFM\_NODELALLOC)

.. _super_flags:

The ``s_flags`` field is any combination of the following:

.. list-table::
   :widths: 8 72
   :header-rows: 1

   * - Value
     - Description
   * - 0x0001
     - Signed directory hash in use.
   * - 0x0002
     - Unsigned directory hash in use.
   * - 0x0004
     - To test development code.

.. _super_encrypt:

The ``s_encrypt_algos`` list can contain any of the following:

.. list-table::
   :widths: 8 72
   :header-rows: 1

   * - Value
     - Description
   * - 0
     - Invalid algorithm (ENCRYPTION\_MODE\_INVALID).
   * - 1
     - 256-bit AES in XTS mode (ENCRYPTION\_MODE\_AES\_256\_XTS).
   * - 2
     - 256-bit AES in GCM mode (ENCRYPTION\_MODE\_AES\_256\_GCM).
   * - 3
     - 256-bit AES in CBC mode (ENCRYPTION\_MODE\_AES\_256\_CBC).

Total size of the superblock is 1024 bytes.
