.. SPDX-License-Identifier: GPL-2.0

Blocks
------

ext4 allocates storage space in units of “blocks”. A block is a group of
sectors between 1KiB and 64KiB, and the number of sectors must be an
integral power of 2. Blocks are in turn grouped into larger units called
block groups. Block size is specified at mkfs time and typically is
4KiB. You may experience mounting problems if block size is greater than
page size (i.e. 64KiB blocks on a i386 which only has 4KiB memory
pages). By default a filesystem can contain 2^32 blocks; if the '64bit'
feature is enabled, then a filesystem can have 2^64 blocks.

For 32-bit filesystems, limits are as follows:

.. list-table::
   :widths: 1 1 1 1 1
   :header-rows: 1

   * - Item
     - 1KiB
     - 2KiB
     - 4KiB
     - 64KiB
   * - Blocks
     - 2^32
     - 2^32
     - 2^32
     - 2^32
   * - Inodes
     - 2^32
     - 2^32
     - 2^32
     - 2^32
   * - File System Size
     - 4TiB
     - 8TiB
     - 16TiB
     - 256PiB
   * - Blocks Per Block Group
     - 8,192
     - 16,384
     - 32,768
     - 524,288
   * - Inodes Per Block Group
     - 8,192
     - 16,384
     - 32,768
     - 524,288
   * - Block Group Size
     - 8MiB
     - 32MiB
     - 128MiB
     - 32GiB
   * - Blocks Per File, Extents
     - 2^32
     - 2^32
     - 2^32
     - 2^32
   * - Blocks Per File, Block Maps
     - 16,843,020
     - 134,480,396
     - 1,074,791,436
     - 4,398,314,962,956 (really 2^32 due to field size limitations)
   * - File Size, Extents
     - 4TiB
     - 8TiB
     - 16TiB
     - 256TiB
   * - File Size, Block Maps
     - 16GiB
     - 256GiB
     - 4TiB
     - 256TiB

For 64-bit filesystems, limits are as follows:

.. list-table::
   :widths: 1 1 1 1 1
   :header-rows: 1

   * - Item
     - 1KiB
     - 2KiB
     - 4KiB
     - 64KiB
   * - Blocks
     - 2^64
     - 2^64
     - 2^64
     - 2^64
   * - Inodes
     - 2^32
     - 2^32
     - 2^32
     - 2^32
   * - File System Size
     - 16ZiB
     - 32ZiB
     - 64ZiB
     - 1YiB
   * - Blocks Per Block Group
     - 8,192
     - 16,384
     - 32,768
     - 524,288
   * - Inodes Per Block Group
     - 8,192
     - 16,384
     - 32,768
     - 524,288
   * - Block Group Size
     - 8MiB
     - 32MiB
     - 128MiB
     - 32GiB
   * - Blocks Per File, Extents
     - 2^32
     - 2^32
     - 2^32
     - 2^32
   * - Blocks Per File, Block Maps
     - 16,843,020
     - 134,480,396
     - 1,074,791,436
     - 4,398,314,962,956 (really 2^32 due to field size limitations)
   * - File Size, Extents
     - 4TiB
     - 8TiB
     - 16TiB
     - 256TiB
   * - File Size, Block Maps
     - 16GiB
     - 256GiB
     - 4TiB
     - 256TiB

Note: Files not using extents (i.e. files using block maps) must be
placed within the first 2^32 blocks of a filesystem. Files with extents
must be placed within the first 2^48 blocks of a filesystem. It's not
clear what happens with larger filesystems.
