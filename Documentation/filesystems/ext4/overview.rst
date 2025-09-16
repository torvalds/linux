.. SPDX-License-Identifier: GPL-2.0

High Level Design
=================

An ext4 file system is split into a series of block groups. To reduce
performance difficulties due to fragmentation, the block allocator tries
very hard to keep each file's blocks within the same group, thereby
reducing seek times. The size of a block group is specified in
``sb.s_blocks_per_group`` blocks, though it can also calculated as 8 *
``block_size_in_bytes``. With the default block size of 4KiB, each group
will contain 32,768 blocks, for a length of 128MiB. The number of block
groups is the size of the device divided by the size of a block group.

All fields in ext4 are written to disk in little-endian order. HOWEVER,
all fields in jbd2 (the journal) are written to disk in big-endian
order.

.. toctree::

   blocks
   blockgroup
   special_inodes
   allocators
   checksums
   bigalloc
   inlinedata
   eainode
   verity
   atomic_writes
