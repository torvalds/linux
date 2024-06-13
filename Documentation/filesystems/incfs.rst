.. SPDX-License-Identifier: GPL-2.0

=================================================
incfs: A stacked incremental filesystem for Linux
=================================================

/sys/fs interface
=================

Please update Documentation/ABI/testing/sysfs-fs-incfs if you update this
section.

incfs creates the following files in /sys/fs.

Features
--------

/sys/fs/incremental-fs/features/corefs
  Reads 'supported'. Always present.

/sys/fs/incremental-fs/features/v2
  Reads 'supported'. Present if all v2 features of incfs are supported. These
  are:
    fs-verity support
    inotify support
    ioclts:
      INCFS_IOC_SET_READ_TIMEOUTS
      INCFS_IOC_GET_READ_TIMEOUTS
      INCFS_IOC_GET_BLOCK_COUNT
      INCFS_IOC_CREATE_MAPPED_FILE
    .incomplete folder
    .blocks_written pseudo file
    report_uid mount option

/sys/fs/incremental-fs/features/zstd
  Reads 'supported'. Present if zstd compression is supported for data blocks.

/sys/fs/incremental-fs/features/bugfix_throttling
  Reads 'supported'. Present if the throttling lock bug is fixed

Optional per mount
------------------

For each incfs mount, the mount option sysfs_name=[name] creates a /sys/fs
node called:

/sys/fs/incremental-fs/instances/[name]

This will contain the following files:

/sys/fs/incremental-fs/instances/[name]/reads_delayed_min
  Returns a count of the number of reads that were delayed as a result of the
  per UID read timeouts min time setting.

/sys/fs/incremental-fs/instances/[name]/reads_delayed_min_us
  Returns total delay time for all files since first mount as a result of the
  per UID read timeouts min time setting.

/sys/fs/incremental-fs/instances/[name]/reads_delayed_pending
  Returns a count of the number of reads that were delayed as a result of
  waiting for a pending read.

/sys/fs/incremental-fs/instances/[name]/reads_delayed_pending_us
  Returns total delay time for all files since first mount as a result of
  waiting for a pending read.

/sys/fs/incremental-fs/instances/[name]/reads_failed_hash_verification
  Returns number of reads that failed because of hash verification failures.

/sys/fs/incremental-fs/instances/[name]/reads_failed_other
  Returns number of reads that failed for reasons other than timing out or
  hash failures.

/sys/fs/incremental-fs/instances/[name]/reads_failed_timed_out
  Returns number of reads that timed out.

For reads_delayed_*** settings, note that a file can count for both
reads_delayed_min and reads_delayed_pending if incfs first waits for a pending
read then has to wait further for the min time. In that case, the time spent
waiting is split between reads_delayed_pending_us, which is increased by the
time spent waiting for the pending read, and reads_delayed_min_us, which is
increased by the remainder of the time spent waiting.

Reads that timed out are not added to the reads_delayed_pending or the
reads_delayed_pending_us counters.
