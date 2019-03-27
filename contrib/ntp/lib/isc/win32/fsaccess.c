/*
 * Copyright (C) 2004, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000-2002  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: fsaccess.c,v 1.15 2007/06/19 23:47:19 tbox Exp $ */

/*
 * Note that Win32 does not have the concept of files having access
 * and ownership bits.  The FAT File system only has a readonly flag
 * for everyone and that's all. NTFS uses ACL's which is a totally
 * different concept of controlling access.
 *
 * This code needs to be revisited to set up proper access control for
 * NTFS file systems.  Nothing can be done for FAT file systems.
 */

#include <config.h>

#include <aclapi.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <errno.h>

#include <isc/file.h>
#include <isc/stat.h>

#include "errno2result.h"

/*
 * The OS-independent part of the API is in lib/isc.
 */
#include "../fsaccess.c"

/* Store the user account name locally */
static char username[255] = "\0";
static DWORD namelen = 0;

/*
 * In order to set or retrieve access information, we need to obtain
 * the File System type.  These could be UNC-type shares.
 */

BOOL
is_ntfs(const char * file) {

	char drive[255];
	char FSType[20];
	char tmpbuf[256];
	char *machinename;
	char *sharename;
	char filename[1024];

	REQUIRE(filename != NULL);

	if (isc_file_absolutepath(file, filename,
		sizeof(filename)) != ISC_R_SUCCESS) {
		return (FALSE);
	}

	/*
	 * Look for c:\path\... style, c:/path/... or \\computer\shar\path...
	 * the UNC style file specs
	 */
	if (isalpha(filename[0]) && filename[1] == ':' && 
		(filename[2] == '\\' || filename[2] == '/')) {
		strncpy(drive, filename, 3);
		drive[3] = '\0';
	}

	else if ((filename[0] == '\\') && (filename[1] == '\\')) {
		/* Find the machine and share name and rebuild the UNC */
		strcpy(tmpbuf, filename);
		machinename = strtok(tmpbuf, "\\");
		sharename = strtok(NULL, "\\");
		strcpy(drive, "\\\\");
		strcat(drive, machinename);
		strcat(drive, "\\");
		strcat(drive, sharename);
		strcat(drive, "\\");

	}
	else /* Not determinable */
		return (FALSE);
		
	GetVolumeInformation(drive, NULL, 0, NULL, 0, NULL, FSType,
			     sizeof(FSType));
	if(strcmp(FSType,"NTFS") == 0)
		return (TRUE);
	else
		return (FALSE);
}

/*
 * If it's not NTFS, we assume that it is FAT and proceed
 * with almost nothing to do. Only the write flag can be set or
 * cleared.
 */
isc_result_t
FAT_fsaccess_set(const char *path, isc_fsaccess_t access) {
	int mode;
	isc_fsaccess_t bits;

	/*
	 * Done with checking bad bits.  Set mode_t.
	 */
	mode = 0;

#define SET_AND_CLEAR1(modebit) \
	if ((access & bits) != 0) { \
		mode |= modebit; \
		access &= ~bits; \
	}
#define SET_AND_CLEAR(user, group, other) \
	SET_AND_CLEAR1(user); \
	bits <<= STEP; \
	SET_AND_CLEAR1(group); \
	bits <<= STEP; \
	SET_AND_CLEAR1(other);

	bits = ISC_FSACCESS_READ | ISC_FSACCESS_LISTDIRECTORY;

	SET_AND_CLEAR(S_IRUSR, S_IRGRP, S_IROTH);

	bits = ISC_FSACCESS_WRITE |
	       ISC_FSACCESS_CREATECHILD |
	       ISC_FSACCESS_DELETECHILD;

	SET_AND_CLEAR(S_IWUSR, S_IWGRP, S_IWOTH);

	INSIST(access == 0);

	if (_chmod(path, mode) < 0)
		return (isc__errno2result(errno));

	return (ISC_R_SUCCESS);
}

isc_result_t
NTFS_Access_Control(const char *filename, const char *user, int access,
		    isc_boolean_t isdir) {
	SECURITY_DESCRIPTOR sd;
	BYTE aclBuffer[1024];
	PACL pacl=(PACL)&aclBuffer;
	BYTE sidBuffer[100];
	PSID psid=(PSID) &sidBuffer;
	DWORD sidBufferSize = sizeof(sidBuffer);
	BYTE adminSidBuffer[100];
	PSID padminsid=(PSID) &adminSidBuffer;
	DWORD adminSidBufferSize = sizeof(adminSidBuffer);
	BYTE otherSidBuffer[100];
	PSID pothersid=(PSID) &otherSidBuffer;
	DWORD otherSidBufferSize = sizeof(otherSidBuffer);
	char domainBuffer[100];
	DWORD domainBufferSize = sizeof(domainBuffer);
	SID_NAME_USE snu;
	int errval;
	DWORD NTFSbits;
	int caccess;


	/* Initialize an ACL */
	if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
		return (ISC_R_NOPERM);
	if (!InitializeAcl(pacl, sizeof(aclBuffer), ACL_REVISION))
		return (ISC_R_NOPERM);
	if (!LookupAccountName(0, user, psid, &sidBufferSize, domainBuffer,
			  &domainBufferSize, &snu))
		return (ISC_R_NOPERM);
	domainBufferSize = sizeof(domainBuffer);
	if (!LookupAccountName(0, "Administrators", padminsid,
		&adminSidBufferSize, domainBuffer, &domainBufferSize, &snu)) {
		errval = GetLastError();
		return (ISC_R_NOPERM);
	}
	domainBufferSize = sizeof(domainBuffer);
	if (!LookupAccountName(0, "Everyone", pothersid,
		&otherSidBufferSize, domainBuffer, &domainBufferSize, &snu)) {
		errval = GetLastError();
		return (ISC_R_NOPERM);
	}

	caccess = access;
	/* Owner check */

	NTFSbits = 0;
	if (caccess & ISC_FSACCESS_READ)
		NTFSbits |= FILE_GENERIC_READ;
	if (caccess & ISC_FSACCESS_WRITE)
		NTFSbits |= FILE_GENERIC_WRITE;
	if (caccess & ISC_FSACCESS_EXECUTE)
		NTFSbits |= FILE_GENERIC_EXECUTE;

	/* For directories check the directory-specific bits */
	if (isdir == ISC_TRUE) {
		if (caccess & ISC_FSACCESS_CREATECHILD)
			NTFSbits |= FILE_ADD_SUBDIRECTORY | FILE_ADD_FILE;
		if (caccess & ISC_FSACCESS_DELETECHILD)
			NTFSbits |= FILE_DELETE_CHILD;
		if (caccess & ISC_FSACCESS_LISTDIRECTORY)
			NTFSbits |= FILE_LIST_DIRECTORY;
		if (caccess & ISC_FSACCESS_ACCESSCHILD)
			NTFSbits |= FILE_TRAVERSE;
	}

	if (NTFSbits == (FILE_GENERIC_READ | FILE_GENERIC_WRITE
		     | FILE_GENERIC_EXECUTE))
		     NTFSbits |= FILE_ALL_ACCESS;
	/*
	 * Owner and Administrator also get STANDARD_RIGHTS_ALL
	 * to ensure that they have full control
	 */

	NTFSbits |= STANDARD_RIGHTS_ALL;

	/* Add the ACE to the ACL */
	if (!AddAccessAllowedAce(pacl, ACL_REVISION, NTFSbits, psid))
		return (ISC_R_NOPERM);
	if (!AddAccessAllowedAce(pacl, ACL_REVISION, NTFSbits, padminsid))
		return (ISC_R_NOPERM);

	/*
	 * Group is ignored since we can be in multiple groups or no group
	 * and its meaning is not clear on Win32
	 */

	caccess = caccess >> STEP;

	/*
	 * Other check.  We translate this to be the same as Everyone
	 */

	caccess = caccess >> STEP;

	NTFSbits = 0;
	if (caccess & ISC_FSACCESS_READ)
		NTFSbits |= FILE_GENERIC_READ;
	if (caccess & ISC_FSACCESS_WRITE)
		NTFSbits |= FILE_GENERIC_WRITE;
	if (caccess & ISC_FSACCESS_EXECUTE)
		NTFSbits |= FILE_GENERIC_EXECUTE;

	/* For directories check the directory-specific bits */
	if (isdir == TRUE) {
		if (caccess & ISC_FSACCESS_CREATECHILD)
			NTFSbits |= FILE_ADD_SUBDIRECTORY | FILE_ADD_FILE;
		if (caccess & ISC_FSACCESS_DELETECHILD)
			NTFSbits |= FILE_DELETE_CHILD;
		if (caccess & ISC_FSACCESS_LISTDIRECTORY)
			NTFSbits |= FILE_LIST_DIRECTORY;
		if (caccess & ISC_FSACCESS_ACCESSCHILD)
			NTFSbits |= FILE_TRAVERSE;
	}
	/* Add the ACE to the ACL */
	if (!AddAccessAllowedAce(pacl, ACL_REVISION, NTFSbits,
				 pothersid))
		return (ISC_R_NOPERM);

	if (!SetSecurityDescriptorDacl(&sd, TRUE, pacl, FALSE))
		return (ISC_R_NOPERM);
	if (!SetFileSecurity(filename, DACL_SECURITY_INFORMATION, &sd)) {
		return (ISC_R_NOPERM);
	}

	return(ISC_R_SUCCESS);
}

isc_result_t
NTFS_fsaccess_set(const char *path, isc_fsaccess_t access,
		  isc_boolean_t isdir){

	/*
	 * For NTFS we first need to get the name of the account under
	 * which BIND is running
	 */
	if (namelen <= 0) {
		namelen = sizeof(username);
		if (GetUserName(username, &namelen) == 0)
			return (ISC_R_FAILURE);
	}
	return (NTFS_Access_Control(path, username, access, isdir));
}

isc_result_t
isc_fsaccess_set(const char *path, isc_fsaccess_t access) {
	struct stat statb;
	isc_boolean_t is_dir = ISC_FALSE;
	isc_result_t result;

	if (stat(path, &statb) != 0)
		return (isc__errno2result(errno));

	if ((statb.st_mode & S_IFDIR) != 0)
		is_dir = ISC_TRUE;
	else if ((statb.st_mode & S_IFREG) == 0)
		return (ISC_R_INVALIDFILE);

	result = check_bad_bits(access, is_dir);
	if (result != ISC_R_SUCCESS)
		return (result);

	/*
	 * Determine if this is a FAT or NTFS disk and
	 * call the appropriate function to set the permissions
	 */
	if (is_ntfs(path))
		return (NTFS_fsaccess_set(path, access, is_dir));
	else
		return (FAT_fsaccess_set(path, access));
}

isc_result_t
isc_fsaccess_changeowner(const char *filename, const char *user) {
	SECURITY_DESCRIPTOR psd;
	BYTE sidBuffer[500];
	BYTE groupBuffer[500];
	PSID psid=(PSID) &sidBuffer;
	DWORD sidBufferSize = sizeof(sidBuffer);
	char domainBuffer[100];
	DWORD domainBufferSize = sizeof(domainBuffer);
	SID_NAME_USE snu;
	PSID pSidGroup = (PSID) &groupBuffer;
	DWORD groupBufferSize = sizeof(groupBuffer);


	/*
	 * Determine if this is a FAT or NTFS disk and
	 * call the appropriate function to set the ownership
	 * FAT disks do not have ownership attributes so it's
	 * a noop.
	 */
	if (is_ntfs(filename) == FALSE)
		return (ISC_R_SUCCESS);

	if (!InitializeSecurityDescriptor(&psd, SECURITY_DESCRIPTOR_REVISION))
		return (ISC_R_NOPERM);

	if (!LookupAccountName(0, user, psid, &sidBufferSize, domainBuffer,
		&domainBufferSize, &snu))
		return (ISC_R_NOPERM);

	/* Make sure administrators can get to it */
	domainBufferSize = sizeof(domainBuffer);
	if (!LookupAccountName(0, "Administrators", pSidGroup,
		&groupBufferSize, domainBuffer, &domainBufferSize, &snu))
		return (ISC_R_NOPERM);

	if (!SetSecurityDescriptorOwner(&psd, psid, FALSE))
		return (ISC_R_NOPERM);

	if (!SetSecurityDescriptorGroup(&psd, pSidGroup, FALSE))
		return (ISC_R_NOPERM);

	if (!SetFileSecurity(filename,
		OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
		&psd))
		return (ISC_R_NOPERM);

	return (ISC_R_SUCCESS);
}

