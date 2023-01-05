.. SPDX-License-Identifier: GPL-2.0

=====
NTFS3
=====

Summary and Features
====================

NTFS3 is fully functional NTFS Read-Write driver. The driver works with NTFS
versions up to 3.1. File system type to use on mount is *ntfs3*.

- This driver implements NTFS read/write support for normal, sparse and
  compressed files.
- Supports native journal replaying.
- Supports NFS export of mounted NTFS volumes.
- Supports extended attributes. Predefined extended attributes:

	- *system.ntfs_security* gets/sets security

		Descriptor: SECURITY_DESCRIPTOR_RELATIVE

	- *system.ntfs_attrib* gets/sets ntfs file/dir attributes.

	  Note: Applied to empty files, this allows to switch type between
	  sparse(0x200), compressed(0x800) and normal.

	- *system.ntfs_attrib_be* gets/sets ntfs file/dir attributes.

	  Same value as system.ntfs_attrib but always represent as big-endian
	  (endianness of system.ntfs_attrib is the same as of the CPU).

Mount Options
=============

The list below describes mount options supported by NTFS3 driver in addition to
generic ones. You can use every mount option with **no** option. If it is in
this table marked with no it means default is without **no**.

.. flat-table::
   :widths: 1 5
   :fill-cells:

   * - iocharset=name
     - This option informs the driver how to interpret path strings and
       translate them to Unicode and back. If this option is not set, the
       default codepage will be used (CONFIG_NLS_DEFAULT).

       Example: iocharset=utf8

   * - uid=
     - :rspan:`1`
   * - gid=

   * - umask=
     - Controls the default permissions for files/directories created after
       the NTFS volume is mounted.

   * - dmask=
     - :rspan:`1` Instead of specifying umask which applies both to files and
       directories, fmask applies only to files and dmask only to directories.
   * - fmask=

   * - noacsrules
     - "No access rules" mount option sets access rights for files/folders to
       777 and owner/group to root. This mount option absorbs all other
       permissions.

       - Permissions change for files/folders will be reported as successful,
	 but they will remain 777.

       - Owner/group change will be reported as successful, butthey will stay
	 as root.

   * - nohidden
     - Files with the Windows-specific HIDDEN (FILE_ATTRIBUTE_HIDDEN) attribute
       will not be shown under Linux.

   * - sys_immutable
     - Files with the Windows-specific SYSTEM (FILE_ATTRIBUTE_SYSTEM) attribute
       will be marked as system immutable files.

   * - hide_dot_files
     - Updates the Windows-specific HIDDEN (FILE_ATTRIBUTE_HIDDEN) attribute
       when creating and moving or renaming files. Files whose names start
       with a dot will have the HIDDEN attribute set and files whose names
       do not start with a dot will have it unset.

   * - windows_names
     - Prevents the creation of files and directories with a name not allowed
       by Windows, either because it contains some not allowed character (which
       are the characters " * / : < > ? \\ | and those whose code is less than
       0x20), because the name (with or without extension) is a reserved file
       name (CON, AUX, NUL, PRN, LPT1-9, COM1-9) or because the last character
       is a space or a dot. Existing such files can still be read and renamed.

   * - discard
     - Enable support of the TRIM command for improved performance on delete
       operations, which is recommended for use with the solid-state drives
       (SSD).

   * - force
     - Forces the driver to mount partitions even if volume is marked dirty.
       Not recommended for use.

   * - sparse
     - Create new files as sparse.

   * - showmeta
     - Use this parameter to show all meta-files (System Files) on a mounted
       NTFS partition. By default, all meta-files are hidden.

   * - prealloc
     - Preallocate space for files excessively when file size is increasing on
       writes. Decreases fragmentation in case of parallel write operations to
       different files.

   * - acl
     - Support POSIX ACLs (Access Control Lists). Effective if supported by
       Kernel. Not to be confused with NTFS ACLs. The option specified as acl
       enables support for POSIX ACLs.

Todo list
=========
- Full journaling support over JBD. Currently journal replaying is supported
  which is not necessarily as effectice as JBD would be.

References
==========
- Commercial version of the NTFS driver for Linux.
	https://www.paragon-software.com/home/ntfs-linux-professional/

- Direct e-mail address for feedback and requests on the NTFS3 implementation.
	almaz.alexandrovich@paragon-software.com
