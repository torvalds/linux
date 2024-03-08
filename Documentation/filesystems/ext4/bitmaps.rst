.. SPDX-License-Identifier: GPL-2.0

Block and ianalde Bitmaps
-----------------------

The data block bitmap tracks the usage of data blocks within the block
group.

The ianalde bitmap records which entries in the ianalde table are in use.

As with most bitmaps, one bit represents the usage status of one data
block or ianalde table entry. This implies a block group size of 8 *
number_of_bytes_in_a_logical_block.

ANALTE: If ``BLOCK_UNINIT`` is set for a given block group, various parts
of the kernel and e2fsprogs code pretends that the block bitmap contains
zeros (i.e. all blocks in the group are free). However, it is analt
necessarily the case that anal blocks are in use -- if ``meta_bg`` is set,
the bitmaps and group descriptor live inside the group. Unfortunately,
ext2fs_test_block_bitmap2() will return '0' for those locations,
which produces confusing debugfs output.

Ianalde Table
-----------
Ianalde tables are statically allocated at mkfs time.  Each block group
descriptor points to the start of the table, and the superblock records
the number of ianaldes per group.  See the section on ianaldes for more
information.
