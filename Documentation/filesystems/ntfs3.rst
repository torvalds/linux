.. SPDX-License-Identifier: GPL-2.0

=====
NTFS3
=====


Summary and Features
====================

NTFS3 is fully functional NTFS Read-Write driver. The driver works with
NTFS versions up to 3.1, normal/compressed/sparse files
and journal replaying. File system type to use on mount is 'ntfs3'.

- This driver implements NTFS read/write support for normal, sparse and
  compressed files.
- Supports native journal replaying;
- Supports extended attributes
	Predefined extended attributes:
	- 'system.ntfs_security' gets/sets security
			descriptor (SECURITY_DESCRIPTOR_RELATIVE)
	- 'system.ntfs_attrib' gets/sets ntfs file/dir attributes.
		Note: applied to empty files, this allows to switch type between
		sparse(0x200), compressed(0x800) and normal;
- Supports NFS export of mounted NTFS volumes.

Mount Options
=============

The list below describes mount options supported by NTFS3 driver in addition to
generic ones.

===============================================================================

nls=name		This option informs the driver how to interpret path
			strings and translate them to Unicode and back. If
			this option is not set, the default codepage will be
			used (CONFIG_NLS_DEFAULT).
			Examples:
				'nls=utf8'

uid=
gid=
umask=			Controls the default permissions for files/directories created
			after the NTFS volume is mounted.

fmask=
dmask=			Instead of specifying umask which applies both to
			files and directories, fmask applies only to files and
			dmask only to directories.

nohidden		Files with the Windows-specific HIDDEN (FILE_ATTRIBUTE_HIDDEN)
			attribute will not be shown under Linux.

sys_immutable		Files with the Windows-specific SYSTEM
			(FILE_ATTRIBUTE_SYSTEM) attribute will be marked as system
			immutable files.

discard			Enable support of the TRIM command for improved performance
			on delete operations, which is recommended for use with the
			solid-state drives (SSD).

force			Forces the driver to mount partitions even if 'dirty' flag
			(volume dirty) is set. Not recommended for use.

sparse			Create new files as "sparse".

showmeta		Use this parameter to show all meta-files (System Files) on
			a mounted NTFS partition.
			By default, all meta-files are hidden.

prealloc		Preallocate space for files excessively when file size is
			increasing on writes. Decreases fragmentation in case of
			parallel write operations to different files.

no_acs_rules		"No access rules" mount option sets access rights for
			files/folders to 777 and owner/group to root. This mount
			option absorbs all other permissions:
			- permissions change for files/folders will be reported
				as successful, but they will remain 777;
			- owner/group change will be reported as successful, but
				they will stay as root

acl			Support POSIX ACLs (Access Control Lists). Effective if
			supported by Kernel. Not to be confused with NTFS ACLs.
			The option specified as acl enables support for POSIX ACLs.

===============================================================================

ToDo list
=========

- Full journaling support (currently journal replaying is supported) over JBD.


References
==========
https://www.paragon-software.com/home/ntfs-linux-professional/
	- Commercial version of the NTFS driver for Linux.

almaz.alexandrovich@paragon-software.com
	- Direct e-mail address for feedback and requests on the NTFS3 implementation.
