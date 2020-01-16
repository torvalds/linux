.. SPDX-License-Identifier: GPL-2.0

Special iyesdes
--------------

ext4 reserves some iyesde for special features, as follows:

.. list-table::
   :widths: 6 70
   :header-rows: 1

   * - iyesde Number
     - Purpose
   * - 0
     - Doesn't exist; there is yes iyesde 0.
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
     - Reserved group descriptors iyesde. (“resize iyesde”)
   * - 8
     - Journal iyesde.
   * - 9
     - The “exclude” iyesde, for snapshots(?)
   * - 10
     - Replica iyesde, used for some yesn-upstream feature?
   * - 11
     - Traditional first yesn-reserved iyesde. Usually this is the lost+found directory. See s\_first\_iyes in the superblock.

