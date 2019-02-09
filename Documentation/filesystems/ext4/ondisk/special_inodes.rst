.. SPDX-License-Identifier: GPL-2.0

Special inodes
--------------

ext4 reserves some inode for special features, as follows:

.. list-table::
   :widths: 1 79
   :header-rows: 1

   * - inode Number
     - Purpose
   * - 0
     - Doesn't exist; there is no inode 0.
   * - 1
     - List of defective blocks.
   * - 2
     - Root directory.
   * - 3
     - User quota.
   * - 4
     - Group quota.
   * - 5
     - Boot loader.
   * - 6
     - Undelete directory.
   * - 7
     - Reserved group descriptors inode. (“resize inode”)
   * - 8
     - Journal inode.
   * - 9
     - The “exclude” inode, for snapshots(?)
   * - 10
     - Replica inode, used for some non-upstream feature?
   * - 11
     - Traditional first non-reserved inode. Usually this is the lost+found directory. See s\_first\_ino in the superblock.

