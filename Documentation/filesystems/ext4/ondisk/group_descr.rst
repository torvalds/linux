.. SPDX-License-Identifier: GPL-2.0

Block Group Descriptors
-----------------------

Each block group on the filesystem has one of these descriptors
associated with it. As noted in the Layout section above, the group
descriptors (if present) are the second item in the block group. The
standard configuration is for each block group to contain a full copy of
the block group descriptor table unless the sparse\_super feature flag
is set.

Notice how the group descriptor records the location of both bitmaps and
the inode table (i.e. they can float). This means that within a block
group, the only data structures with fixed locations are the superblock
and the group descriptor table. The flex\_bg mechanism uses this
property to group several block groups into a flex group and lay out all
of the groups' bitmaps and inode tables into one long run in the first
group of the flex group.

If the meta\_bg feature flag is set, then several block groups are
grouped together into a meta group. Note that in the meta\_bg case,
however, the first and last two block groups within the larger meta
group contain only group descriptors for the groups inside the meta
group.

flex\_bg and meta\_bg do not appear to be mutually exclusive features.

In ext2, ext3, and ext4 (when the 64bit feature is not enabled), the
block group descriptor was only 32 bytes long and therefore ends at
bg\_checksum. On an ext4 filesystem with the 64bit feature enabled, the
block group descriptor expands to at least the 64 bytes described below;
the size is stored in the superblock.

If gdt\_csum is set and metadata\_csum is not set, the block group
checksum is the crc16 of the FS UUID, the group number, and the group
descriptor structure. If metadata\_csum is set, then the block group
checksum is the lower 16 bits of the checksum of the FS UUID, the group
number, and the group descriptor structure. Both block and inode bitmap
checksums are calculated against the FS UUID, the group number, and the
entire bitmap.

The block group descriptor is laid out in ``struct ext4_group_desc``.

.. list-table::
   :widths: 1 1 1 77
   :header-rows: 1

   * - Offset
     - Size
     - Name
     - Description
   * - 0x0
     - \_\_le32
     - bg\_block\_bitmap\_lo
     - Lower 32-bits of location of block bitmap.
   * - 0x4
     - \_\_le32
     - bg\_inode\_bitmap\_lo
     - Lower 32-bits of location of inode bitmap.
   * - 0x8
     - \_\_le32
     - bg\_inode\_table\_lo
     - Lower 32-bits of location of inode table.
   * - 0xC
     - \_\_le16
     - bg\_free\_blocks\_count\_lo
     - Lower 16-bits of free block count.
   * - 0xE
     - \_\_le16
     - bg\_free\_inodes\_count\_lo
     - Lower 16-bits of free inode count.
   * - 0x10
     - \_\_le16
     - bg\_used\_dirs\_count\_lo
     - Lower 16-bits of directory count.
   * - 0x12
     - \_\_le16
     - bg\_flags
     - Block group flags. See the bgflags_ table below.
   * - 0x14
     - \_\_le32
     - bg\_exclude\_bitmap\_lo
     - Lower 32-bits of location of snapshot exclusion bitmap.
   * - 0x18
     - \_\_le16
     - bg\_block\_bitmap\_csum\_lo
     - Lower 16-bits of the block bitmap checksum.
   * - 0x1A
     - \_\_le16
     - bg\_inode\_bitmap\_csum\_lo
     - Lower 16-bits of the inode bitmap checksum.
   * - 0x1C
     - \_\_le16
     - bg\_itable\_unused\_lo
     - Lower 16-bits of unused inode count. If set, we needn't scan past the
       ``(sb.s_inodes_per_group - gdt.bg_itable_unused)``\ th entry in the
       inode table for this group.
   * - 0x1E
     - \_\_le16
     - bg\_checksum
     - Group descriptor checksum; crc16(sb\_uuid+group+desc) if the
       RO\_COMPAT\_GDT\_CSUM feature is set, or crc32c(sb\_uuid+group\_desc) &
       0xFFFF if the RO\_COMPAT\_METADATA\_CSUM feature is set.
   * -
     -
     -
     - These fields only exist if the 64bit feature is enabled and s_desc_size
       > 32.
   * - 0x20
     - \_\_le32
     - bg\_block\_bitmap\_hi
     - Upper 32-bits of location of block bitmap.
   * - 0x24
     - \_\_le32
     - bg\_inode\_bitmap\_hi
     - Upper 32-bits of location of inodes bitmap.
   * - 0x28
     - \_\_le32
     - bg\_inode\_table\_hi
     - Upper 32-bits of location of inodes table.
   * - 0x2C
     - \_\_le16
     - bg\_free\_blocks\_count\_hi
     - Upper 16-bits of free block count.
   * - 0x2E
     - \_\_le16
     - bg\_free\_inodes\_count\_hi
     - Upper 16-bits of free inode count.
   * - 0x30
     - \_\_le16
     - bg\_used\_dirs\_count\_hi
     - Upper 16-bits of directory count.
   * - 0x32
     - \_\_le16
     - bg\_itable\_unused\_hi
     - Upper 16-bits of unused inode count.
   * - 0x34
     - \_\_le32
     - bg\_exclude\_bitmap\_hi
     - Upper 32-bits of location of snapshot exclusion bitmap.
   * - 0x38
     - \_\_le16
     - bg\_block\_bitmap\_csum\_hi
     - Upper 16-bits of the block bitmap checksum.
   * - 0x3A
     - \_\_le16
     - bg\_inode\_bitmap\_csum\_hi
     - Upper 16-bits of the inode bitmap checksum.
   * - 0x3C
     - \_\_u32
     - bg\_reserved
     - Padding to 64 bytes.

.. _bgflags:

Block group flags can be any combination of the following:

.. list-table::
   :widths: 1 79
   :header-rows: 1

   * - Value
     - Description
   * - 0x1
     - inode table and bitmap are not initialized (EXT4\_BG\_INODE\_UNINIT).
   * - 0x2
     - block bitmap is not initialized (EXT4\_BG\_BLOCK\_UNINIT).
   * - 0x4
     - inode table is zeroed (EXT4\_BG\_INODE\_ZEROED).
