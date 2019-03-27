/*
 * Copyright (C) 2004, 2006, 2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2001  Internet Software Consortium.
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

/* $Id: ntgroups.c,v 1.12 2009/09/29 23:48:04 tbox Exp $ */

/*
 * The NT Groups have two groups that are not well documented and are
 * not normally seen: None and Everyone.  A user account belongs to
 * any number of groups, but if it is not a member of any group then
 * it is a member of the None Group. The None group is not listed
 * anywhere. You cannot remove an account from the none group except
 * by making it a member of some other group, The second group is the
 * Everyone group.  All accounts, no matter how many groups that they
 * belong to, also belong to the Everyone group. You cannot remove an
 * account from the Everyone group.
 */

#ifndef UNICODE
#define UNICODE
#endif /* UNICODE */

/*
 * Silence warnings.
 */
#define _CRT_SECURE_NO_DEPRECATE 1

#include <windows.h>
#include <assert.h>
#include <lm.h>

#include <isc/ntgroups.h>
#include <isc/result.h>

#define MAX_NAME_LENGTH 256

isc_result_t
isc_ntsecurity_getaccountgroups(char *username, char **GroupList,
				unsigned int maxgroups,
				unsigned int *totalGroups) {
	LPGROUP_USERS_INFO_0 pTmpBuf;
	LPLOCALGROUP_USERS_INFO_0 pTmpLBuf;
	DWORD i;
	LPLOCALGROUP_USERS_INFO_0 pBuf = NULL;
	LPGROUP_USERS_INFO_0 pgrpBuf = NULL;
	DWORD dwLevel = 0;
	DWORD dwFlags = LG_INCLUDE_INDIRECT;
	DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
	DWORD dwEntriesRead = 0;
	DWORD dwTotalEntries = 0;
	NET_API_STATUS nStatus;
	DWORD dwTotalCount = 0;
	size_t retlen;
	wchar_t user[MAX_NAME_LENGTH];

	retlen = mbstowcs(user, username, MAX_NAME_LENGTH);

	*totalGroups = 0;
	/*
	 * Call the NetUserGetLocalGroups function
	 * specifying information level 0.
	 *
	 * The LG_INCLUDE_INDIRECT flag specifies that the
	 * function should also return the names of the local
	 * groups in which the user is indirectly a member.
	 */
	nStatus = NetUserGetLocalGroups(NULL,
				   user,
				   dwLevel,
				   dwFlags,
				   (LPBYTE *) &pBuf,
				   dwPrefMaxLen,
				   &dwEntriesRead,
				   &dwTotalEntries);
	/*
	 * See if the call succeeds,
	 */
	if (nStatus != NERR_Success) {
		if (nStatus == ERROR_ACCESS_DENIED)
			return (ISC_R_NOPERM);
		if (nStatus == ERROR_MORE_DATA)
			return (ISC_R_NOSPACE);
		if (nStatus == NERR_UserNotFound)
			dwEntriesRead = 0;
	}

	dwTotalCount = 0;
	if (pBuf != NULL) {
		pTmpLBuf = pBuf;
		/*
		 * Loop through the entries
		 */
		 for (i = 0;
		     (i < dwEntriesRead && *totalGroups < maxgroups); i++) {
			assert(pTmpLBuf != NULL);
			if (pTmpLBuf == NULL)
				break;
			retlen = wcslen(pTmpLBuf->lgrui0_name);
			GroupList[*totalGroups] = (char *) malloc(retlen +1);
			if (GroupList[*totalGroups] == NULL)
				return (ISC_R_NOMEMORY);

			retlen = wcstombs(GroupList[*totalGroups],
				 pTmpLBuf->lgrui0_name, retlen);
			GroupList[*totalGroups][retlen] = '\0';
			if (strcmp(GroupList[*totalGroups], "None") == 0)
				free(GroupList[*totalGroups]);
			else
				(*totalGroups)++;
			pTmpLBuf++;
		}
	}
	/* Free the allocated memory. */
	if (pBuf != NULL)
		NetApiBufferFree(pBuf);


	/*
	 * Call the NetUserGetGroups function, specifying level 0.
	 */
	nStatus = NetUserGetGroups(NULL,
			      user,
			      dwLevel,
			      (LPBYTE*)&pgrpBuf,
			      dwPrefMaxLen,
			      &dwEntriesRead,
			      &dwTotalEntries);
	/*
	 * See if the call succeeds,
	 */
	if (nStatus != NERR_Success) {
		if (nStatus == ERROR_ACCESS_DENIED)
			return (ISC_R_NOPERM);
		if (nStatus == ERROR_MORE_DATA)
			return (ISC_R_NOSPACE);
		if (nStatus == NERR_UserNotFound)
			dwEntriesRead = 0;
	}

	if (pgrpBuf != NULL) {
		pTmpBuf = pgrpBuf;
		/*
		 * Loop through the entries
		 */
		 for (i = 0;
		     (i < dwEntriesRead && *totalGroups < maxgroups); i++) {
			assert(pTmpBuf != NULL);

			if (pTmpBuf == NULL)
				break;
			retlen = wcslen(pTmpBuf->grui0_name);
			GroupList[*totalGroups] = (char *) malloc(retlen +1);
			if (GroupList[*totalGroups] == NULL)
				return (ISC_R_NOMEMORY);

			retlen = wcstombs(GroupList[*totalGroups],
				 pTmpBuf->grui0_name, retlen);
			GroupList[*totalGroups][retlen] = '\0';
			if (strcmp(GroupList[*totalGroups], "None") == 0)
				free(GroupList[*totalGroups]);
			else
				(*totalGroups)++;
			pTmpBuf++;
		}
	}
	/*
	 * Free the allocated memory.
	 */
	if (pgrpBuf != NULL)
		NetApiBufferFree(pgrpBuf);

	return (ISC_R_SUCCESS);
}
