.. SPDX-License-Identifier: GPL-2.0

==================================
The Linux exFAT filesystem driver
==================================


.. Table of contents

   - Overview
   - Utilities support
   - Supported mount options


Overview
========

exFAT is a filesystem designed for removable storage and other devices that
need to store large files.  The Linux exFAT filesystem driver provides read
and write support for exFAT volumes.

To mount an exFAT volume, use the ``exfat`` filesystem type::

  mount -t exfat /dev/sdX1 /mnt


Utilities support
=================

The exfatprogs project provides userspace utilities for creating, checking,
repairing, inspecting, and tuning exFAT filesystems.  Use exfatprogs when
creating or checking an exFAT filesystem.  For example, use ``mkfs.exfat``
to create a filesystem and ``fsck.exfat`` to check or repair one.

The project is available at:

  https://github.com/exfatprogs/exfatprogs


Supported mount options
=======================

The exFAT driver supports the following mount options:

======================= ====================================================
uid=
gid=                    Set the owner and group of all files and
                        directories.  The default is the uid and gid of
                        the process mounting the filesystem.

umask=                  Set the permission mask for files and directories.
                        The default is the umask of the process mounting
                        the filesystem.

dmask=                  Set the permission mask for directories.

fmask=                  Set the permission mask for files.

allow_utime=            Control the permission check for changing file
                        timestamps.  Only permission bits 0022 are used.
                        Permission bit 0020 allows members of the file's
                        group to change timestamps, and permission bit 0002
                        allows other users to change timestamps.  The
                        default is derived from dmask (``~dmask & 0022``).

iocharset=name          Character set used to convert between user-visible
                        filenames and the UTF-16 character encoding used by
                        exFAT.  The default is
                        CONFIG_EXFAT_DEFAULT_IOCHARSET, which is ``utf8``
                        unless changed at kernel configuration time.  Use
                        ``iocharset=utf8`` for UTF-8 filename handling.

errors=                 Specify exFAT behavior on filesystem errors.  The
                        value must be ``panic``, ``continue``, or
                        ``remount-ro``.  These respectively panic, continue
                        without changing the filesystem, or remount the
                        filesystem read-only.  The default is
                        ``remount-ro``.

discard                 Issue discard/TRIM requests to the block device
                        when clusters are freed.  This is disabled by
                        default.  ``nodiscard`` disables it explicitly.

keep_last_dots          Keep trailing periods in path components during
                        lookup.  Without this option, trailing periods are
                        stripped.  Existing entries with trailing periods
                        can be accessed when this option is enabled, but
                        creating new entries with trailing periods is
                        rejected.

sys_tz                  Use the system timezone as the UTC offset when an
                        exFAT timestamp does not contain a valid timezone
                        offset.  This takes precedence over time_offset.

time_offset=minutes     Set the UTC offset, in minutes, used when an exFAT
                        timestamp does not contain a valid timezone offset.
                        Values from -1440 to 1440 are accepted.  The default
                        is 0.  This option is ignored when sys_tz is set.

zero_size_dir           Create directories with zero size and without
                        allocating a cluster.  This is disabled by default;
                        the default behavior allocates a cluster for a new
                        directory.  ``nozero_size_dir`` disables it
                        explicitly.
======================= ====================================================


Deprecated mount options
------------------------

The following options are accepted for compatibility but should not be used:

``utf8``
  Deprecated.  Use ``iocharset=utf8`` instead.

``debug``, ``namecase=``, ``codepage=``
  Deprecated and ignored by the exFAT driver.
