.. SPDX-License-Identifier: GPL-2.0

Bigalloc
--------

At the moment, the default size of a block is 4KiB, which is a commonly
supported page size on most MMU-capable hardware. This is fortunate, as
ext4 code is not prepared to handle the case where the block size
exceeds the page size. However, for a filesystem of mostly huge files,
it is desirable to be able to allocate disk blocks in units of multiple
blocks to reduce both fragmentation and metadata overhead. The
`bigalloc <Bigalloc>`__ feature provides exactly this ability. The
administrator can set a block cluster size at mkfs time (which is stored
in the s\_log\_cluster\_size field in the superblock); from then on, the
block bitmaps track clusters, not individual blocks. This means that
block groups can be several gigabytes in size (instead of just 128MiB);
however, the minimum allocation unit becomes a cluster, not a block,
even for directories. TaoBao had a patchset to extend the “use units of
clusters instead of blocks” to the extent tree, though it is not clear
where those patches went-- they eventually morphed into “extent tree v2”
but that code has not landed as of May 2015.

