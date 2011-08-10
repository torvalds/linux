/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
  $
 */

#ifndef _MLOS_H
#define _MLOS_H

#ifndef __KERNEL__
#include <stdio.h>
#endif

#include "mltypes.h"

#ifdef __cplusplus
extern "C" {
#endif

	/* ------------ */
	/* - Defines. - */
	/* ------------ */

	/* - MLOSCreateFile defines. - */

#define MLOS_GENERIC_READ         ((unsigned int)0x80000000)
#define MLOS_GENERIC_WRITE        ((unsigned int)0x40000000)
#define MLOS_FILE_SHARE_READ      ((unsigned int)0x00000001)
#define MLOS_FILE_SHARE_WRITE     ((unsigned int)0x00000002)
#define MLOS_OPEN_EXISTING        ((unsigned int)0x00000003)

	/* ---------- */
	/* - Enums. - */
	/* ---------- */

	/* --------------- */
	/* - Structures. - */
	/* --------------- */

	/* --------------------- */
	/* - Function p-types. - */
	/* --------------------- */

	void *MLOSMalloc(unsigned int numBytes);
	tMLError MLOSFree(void *ptr);
	tMLError MLOSCreateMutex(HANDLE *mutex);
	tMLError MLOSLockMutex(HANDLE mutex);
	tMLError MLOSUnlockMutex(HANDLE mutex);
	FILE *MLOSFOpen(char *filename);
	void MLOSFClose(FILE *fp);

	tMLError MLOSDestroyMutex(HANDLE handle);

	void MLOSSleep(int mSecs);
	unsigned long MLOSGetTickCount(void);

#ifdef __cplusplus
}
#endif
#endif				/* _MLOS_H */
