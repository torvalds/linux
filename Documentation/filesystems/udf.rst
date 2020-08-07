.. SPDX-License-Identifier: GPL-2.0

===============
UDF file system
===============

If you encounter problems with reading UDF discs using this driver,
please report them according to MAINTAINERS file.

Write support requires a block driver which supports writing.  Currently
dvd+rw drives and media support true random sector writes, and so a udf
filesystem on such devices can be directly mounted read/write.  CD-RW
media however, does not support this.  Instead the media can be formatted
for packet mode using the utility cdrwtool, then the pktcdvd driver can
be bound to the underlying cd device to provide the required buffering
and read-modify-write cycles to allow the filesystem random sector writes
while providing the hardware with only full packet writes.  While not
required for dvd+rw media, use of the pktcdvd driver often enhances
performance due to very poor read-modify-write support supplied internally
by drive firmware.

-------------------------------------------------------------------------------

The following mount options are supported:

	===========	======================================
	gid=		Set the default group.
	umask=		Set the default umask.
	mode=		Set the default file permissions.
	dmode=		Set the default directory permissions.
	uid=		Set the default user.
	bs=		Set the block size.
	unhide		Show otherwise hidden files.
	undelete	Show deleted files in lists.
	adinicb		Embed data in the inode (default)
	noadinicb	Don't embed data in the inode
	shortad		Use short ad's
	longad		Use long ad's (default)
	nostrict	Unset strict conformance
	iocharset=	Set the NLS character set
	===========	======================================

The uid= and gid= options need a bit more explaining.  They will accept a
decimal numeric value and all inodes on that mount will then appear as
belonging to that uid and gid.  Mount options also accept the string "forget".
The forget option causes all IDs to be written to disk as -1 which is a way
of UDF standard to indicate that IDs are not supported for these files .

For typical desktop use of removable media, you should set the ID to that of
the interactively logged on user, and also specify the forget option.  This way
the interactive user will always see the files on the disk as belonging to him.

The remaining are for debugging and disaster recovery:

	=====		================================
	novrs		Skip volume sequence recognition
	=====		================================

The following expect a offset from 0.

	==========	=================================================
	session=	Set the CDROM session (default= last session)
	anchor=		Override standard anchor location. (default= 256)
	lastblock=	Set the last block of the filesystem/
	==========	=================================================

-------------------------------------------------------------------------------


For the latest version and toolset see:
	https://github.com/pali/udftools

Documentation on UDF and ECMA 167 is available FREE from:
	- http://www.osta.org/
	- http://www.ecma-international.org/
