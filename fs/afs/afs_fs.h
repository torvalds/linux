/* AFS File Service definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef AFS_FS_H
#define AFS_FS_H

#define AFS_FS_PORT		7000	/* AFS file server port */
#define FS_SERVICE		1	/* AFS File Service ID */

enum AFS_FS_Operations {
	FSFETCHDATA		= 130,	/* AFS Fetch file data */
	FSFETCHSTATUS		= 132,	/* AFS Fetch file status */
	FSREMOVEFILE		= 136,	/* AFS Remove a file */
	FSCREATEFILE		= 137,	/* AFS Create a file */
	FSRENAME		= 138,	/* AFS Rename or move a file or directory */
	FSSYMLINK		= 139,	/* AFS Create a symbolic link */
	FSLINK			= 140,	/* AFS Create a hard link */
	FSMAKEDIR		= 141,	/* AFS Create a directory */
	FSREMOVEDIR		= 142,	/* AFS Remove a directory */
	FSGIVEUPCALLBACKS	= 147,	/* AFS Discard callback promises */
	FSGETVOLUMEINFO		= 148,	/* AFS Get root volume information */
	FSGETROOTVOLUME		= 151,	/* AFS Get root volume name */
	FSLOOKUP		= 161,	/* AFS lookup file in directory */
};

enum AFS_FS_Errors {
	VSALVAGE	= 101,	/* volume needs salvaging */
	VNOVNODE	= 102,	/* no such file/dir (vnode) */
	VNOVOL		= 103,	/* no such volume or volume unavailable */
	VVOLEXISTS	= 104,	/* volume name already exists */
	VNOSERVICE	= 105,	/* volume not currently in service */
	VOFFLINE	= 106,	/* volume is currently offline (more info available [VVL-spec]) */
	VONLINE		= 107,	/* volume is already online */
	VDISKFULL	= 108,	/* disk partition is full */
	VOVERQUOTA	= 109,	/* volume's maximum quota exceeded */
	VBUSY		= 110,	/* volume is temporarily unavailable */
	VMOVED		= 111,	/* volume moved to new server - ask this FS where */
};

#endif /* AFS_FS_H */
