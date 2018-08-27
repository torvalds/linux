.. SPDX-License-Identifier: GPL-2.0

Multiple Mount Protection
-------------------------

Multiple mount protection (MMP) is a feature that protects the
filesystem against multiple hosts trying to use the filesystem
simultaneously. When a filesystem is opened (for mounting, or fsck,
etc.), the MMP code running on the node (call it node A) checks a
sequence number. If the sequence number is EXT4\_MMP\_SEQ\_CLEAN, the
open continues. If the sequence number is EXT4\_MMP\_SEQ\_FSCK, then
fsck is (hopefully) running, and open fails immediately. Otherwise, the
open code will wait for twice the specified MMP check interval and check
the sequence number again. If the sequence number has changed, then the
filesystem is active on another machine and the open fails. If the MMP
code passes all of those checks, a new MMP sequence number is generated
and written to the MMP block, and the mount proceeds.

While the filesystem is live, the kernel sets up a timer to re-check the
MMP block at the specified MMP check interval. To perform the re-check,
the MMP sequence number is re-read; if it does not match the in-memory
MMP sequence number, then another node (node B) has mounted the
filesystem, and node A remounts the filesystem read-only. If the
sequence numbers match, the sequence number is incremented both in
memory and on disk, and the re-check is complete.

The hostname and device filename are written into the MMP block whenever
an open operation succeeds. The MMP code does not use these values; they
are provided purely for informational purposes.

The checksum is calculated against the FS UUID and the MMP structure.
The MMP structure (``struct mmp_struct``) is as follows:

.. list-table::
   :widths: 1 1 1 77
   :header-rows: 1

   * - Offset
     - Type
     - Name
     - Description
   * - 0x0
     - \_\_le32
     - mmp\_magic
     - Magic number for MMP, 0x004D4D50 (“MMP”).
   * - 0x4
     - \_\_le32
     - mmp\_seq
     - Sequence number, updated periodically.
   * - 0x8
     - \_\_le64
     - mmp\_time
     - Time that the MMP block was last updated.
   * - 0x10
     - char[64]
     - mmp\_nodename
     - Hostname of the node that opened the filesystem.
   * - 0x50
     - char[32]
     - mmp\_bdevname
     - Block device name of the filesystem.
   * - 0x70
     - \_\_le16
     - mmp\_check\_interval
     - The MMP re-check interval, in seconds.
   * - 0x72
     - \_\_le16
     - mmp\_pad1
     - Zero.
   * - 0x74
     - \_\_le32[226]
     - mmp\_pad2
     - Zero.
   * - 0x3FC
     - \_\_le32
     - mmp\_checksum
     - Checksum of the MMP block.
