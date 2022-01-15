.. SPDX-License-Identifier: GPL-2.0

Special inodes
--------------

ext4 reserves some inode for special features, as follows:

.. list-table::
   :widths: 6 70
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

Note that there are also some inodes allocated from non-reserved inode numbers
for other filesystem features which are not referenced from standard directory
hierarchy. These are generally reference from the superblock. They are:

.. list-table::
   :widths: 20 50
   :header-rows: 1

   * - Superblock field
     - Description

   * - s\_lpf\_ino
     - Inode number of lost+found directory.
   * - s\_prj\_quota\_inum
     - Inode number of quota file tracking project quotas
   * - s\_orphan\_file\_inum
     - Inode number of file tracking orphan inodes.
