.. SPDX-License-Identifier: GPL-2.0

=================================
The Linux NTFS filesystem driver
=================================


.. Table of contents

   - Overview
   - Utilities support
   - Supported mount options


Overview
========

NTFS is a Linux kernel filesystem driver that provides full read and write
support for NTFS volumes. It is designed for high performance, modern
kernel infrastructure (iomap, folio), and stable long-term maintenance.


Utilities support
=================

The NTFS utilities project, called ntfsprogs-plus, provides mkfs.ntfs,
fsck.ntfs, and other related tools (e.g., ntfsinfo, ntfsclone, etc.) for
creating, checking, and managing NTFS volumes. These utilities can be used
for filesystem testing with xfstests as well as for recovering corrupted
NTFS devices.

The project is available at:

  https://github.com/ntfsprogs-plus/ntfsprogs-plus


Supported mount options
=======================

The NTFS driver supports the following mount options:

======================= ====================================================
iocharset=name          Character set to use for converting between
                        the encoding is used for user visible filename and
                        16 bit Unicode characters.

nls=name                Deprecated option.  Still supported but please use
                        iocharset=name in the future.

uid=
gid=
umask=                  Provide default owner, group, and access mode mask.
                        These options work as documented in mount(8).  By
                        default, the files/directories are owned by root
                        and he/she has read and write permissions, as well
                        as browse permission for directories.  No one else
                        has any access permissions.  I.e. the mode on all
                        files is by default rw------- and
                        for directories rwx------, a consequence of
                        the default fmask=0177 and dmask=0077.
                        Using a umask of zero will grant all permissions to
                        everyone, i.e. all files and directories will have
                        mode rwxrwxrwx.

fmask=
dmask=                  Instead of specifying umask which applies both to
                        files and directories, fmask applies only to files
                        and dmask only to directories.

showmeta=<BOOL>
show_sys_files=<BOOL>   If show_sys_files is specified, show the system
                        files in directory listings.  Otherwise the default
                        behaviour is to hide the system files.
                        Note that even when show_sys_files is specified,
                        "$MFT" will not be visible due to bugs/mis-features
                        in glibc. Further, note that irrespective of
                        show_sys_files, all files are accessible by name,
                        i.e. you can always do "ls -l \$UpCase" for example
                        to specifically show the system file containing
                        the Unicode upcase table.

case_sensitive=<BOOL>   If case_sensitive is specified, treat all filenames
                        as case sensitive and create file names in
                        the POSIX namespace (default behavior). Note,
                        the Linux NTFS driver will never create short
                        filenames and will remove them on rename/delete of
                        the corresponding long file name. Note that files
                        remain accessible via their short file name, if it
                        exists.

nocase=<BOOL>           If nocase is specified, treat filenames
                        case-insensitively.

disable_sparse=<BOOL>   If disable_sparse is specified, creation of sparse
                        regions, i.e. holes, inside files is disabled for
                        the volume (for the duration of this mount only).
                        By default, creation of sparse regions is enabled,
                        which is consistent with the behaviour of
                        traditional Unix filesystems.

errors=opt              Specify NTFS behavior on critical errors: panic,
                        remount the partition in read-only mode or
                        continue without doing anything (default behavior).

mft_zone_multiplier=    Set the MFT zone multiplier for the volume (this
                        setting is not persistent across mounts and can be
                        changed from mount to mount but cannot be changed
                        on remount).  Values of 1 to 4 are allowed, 1 being
                        the default.  The MFT zone multiplier determines
                        how much space is reserved for the MFT on the
                        volume.  If all other space is used up, then the
                        MFT zone will be shrunk dynamically, so this has no
                        impact on the amount of free space.  However, it
                        can have an impact on performance by affecting
                        fragmentation of the MFT. In general use the
                        default.  If you have a lot of small files then use
                        a higher value.  The values have the following
                        meaning:

                        =====   =================================
                        Value   MFT zone size (% of volume size)
                        =====   =================================
                          1             12.5%
                          2             25%
                          3             37.5%
                          4             50%
                        =====   =================================

                        Note this option is irrelevant for read-only mount.

preallocated_size=      Set preallocated size to optimize runlist merge
                        overhead with small chunck size.(64KB size by
                        default)

acl=<BOOL>              Enable POSIX ACL support. When specified, POSIX
                        ACLs stored in extended attributes are enforced.
                        Default is off. Requires kernel config
                        NTFS_FS_POSIX_ACL enabled.

sys_immutable=<BOOL>    Make NTFS system files (e.g. $MFT, $LogFile,
                        $Bitmap, $UpCase, etc.) immutable to user initiated
                        modifications for extra safety. Default is off.

nohidden=<BOOL>         Hide files and directories marked with the Windows
                        "hidden" attribute. By default hidden items are
                        shown.

hide_dot_files=<BOOL>   Hide names beginning with a dot ("."). By default
                        dot files are shown. When enabled, files and
                        directories created with a leading '.' will be
                        hidden from directory listings.

windows_names=<BOOL>    Refuse creation/rename of files with characters or
                        reserved device names disallowed on Windows (e.g.
                        CON, NUL, AUX, COM1, LPT1, etc.). Default is off.
discard=<BOOL>          Issue block device discard for clusters freed on
                        file deletion/truncation to inform underlying
                        storage.
======================= ====================================================
