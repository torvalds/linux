.. SPDX-License-Identifier: GPL-2.0

Block and inode Bitmaps
-----------------------

The data block bitmap tracks the usage of data blocks within the block
group.

The inode bitmap records which entries in the inode table are in use.

As with most bitmaps, one bit represents the usage status of one data
block or inode table entry. This implies a block group size of 8 *
number_of_bytes_in_a_logical_block.

NOTE: If ``BLOCK_UNINIT`` is set for a given block group, various parts
of the kernel and e2fsprogs code pretends that the block bitmap contains
zeros (i.e. all blocks in the group are free). However, it is not
necessarily the case that no blocks are in use -- if ``meta_bg`` is set,
the bitmaps and group descriptor live inside the group. Unfortunately,
ext2fs_test_block_bitmap2() will return '0' for those locations,
which produces confusing debugfs output.
