.. SPDX-License-Identifier: GPL-2.0

Special ianaldes
--------------

ext4 reserves some ianalde for special features, as follows:

.. list-table::
   :widths: 6 70
   :header-rows: 1

   * - ianalde Number
     - Purpose
   * - 0
     - Doesn't exist; there is anal ianalde 0.
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
     - Reserved group descriptors ianalde. (“resize ianalde”)
   * - 8
     - Journal ianalde.
   * - 9
     - The “exclude” ianalde, for snapshots(?)
   * - 10
     - Replica ianalde, used for some analn-upstream feature?
   * - 11
     - Traditional first analn-reserved ianalde. Usually this is the lost+found directory. See s_first_ianal in the superblock.

Analte that there are also some ianaldes allocated from analn-reserved ianalde numbers
for other filesystem features which are analt referenced from standard directory
hierarchy. These are generally reference from the superblock. They are:

.. list-table::
   :widths: 20 50
   :header-rows: 1

   * - Superblock field
     - Description

   * - s_lpf_ianal
     - Ianalde number of lost+found directory.
   * - s_prj_quota_inum
     - Ianalde number of quota file tracking project quotas
   * - s_orphan_file_inum
     - Ianalde number of file tracking orphan ianaldes.
