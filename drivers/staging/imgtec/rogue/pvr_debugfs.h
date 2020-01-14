/*************************************************************************/ /*!
@File
@Title          Functions for creating debugfs directories and entries.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined(__PVR_DEBUGFS_H__)
#define __PVR_DEBUGFS_H__

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "img_types.h"
#include "osfunc.h"

typedef ssize_t (PVRSRV_ENTRY_WRITE_FUNC)(const char __user *pszBuffer,
					  size_t uiCount,
					  loff_t uiPosition,
					  void *pvData);


typedef IMG_UINT32 (PVRSRV_INC_STAT_MEM_REFCOUNT_FUNC)(void *pvStatPtr);
typedef IMG_UINT32 (PVRSRV_DEC_STAT_MEM_REFCOUNT_FUNC)(void *pvStatPtr);

typedef IMG_UINT32 (PVRSRV_INC_FSENTRY_PVDATA_REFCNT_FN)(void *pvData);
typedef IMG_UINT32 (PVRSRV_DEC_FSENTRY_PVDATA_REFCNT_FN)(void *pvData);

typedef struct _PVR_DEBUGFS_DIR_DATA_ {
	struct dentry *psDir;
	struct _PVR_DEBUGFS_DIR_DATA_ *psParentDir;
	IMG_UINT32	ui32RefCount;
} PVR_DEBUGFS_DIR_DATA;

struct _PVR_DEBUGFS_ENTRY_DATA_;

typedef struct _PVR_DEBUGFS_DRIVER_STAT_ {
	void				 *pvData;
	OS_STATS_PRINT_FUNC  *pfnStatsPrint;
	PVRSRV_INC_STAT_MEM_REFCOUNT_FUNC	*pfnIncStatMemRefCount;
	PVRSRV_DEC_STAT_MEM_REFCOUNT_FUNC	*pfnDecStatMemRefCount;
	IMG_UINT32				ui32RefCount;
	struct _PVR_DEBUGFS_ENTRY_DATA_	*pvDebugFSEntry;
} PVR_DEBUGFS_DRIVER_STAT;

typedef struct _PVR_DEBUGFS_ENTRY_DATA_ {
	struct dentry *psEntry;
	PVR_DEBUGFS_DIR_DATA *psParentDir;
	IMG_UINT32	ui32RefCount;
	PVR_DEBUGFS_DRIVER_STAT *psStatData;
} PVR_DEBUGFS_ENTRY_DATA;

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
typedef struct _PVR_DEBUGFS_RAW_DRIVER_STAT_ {
	OS_STATS_PRINT_FUNC *pfStatsPrint;
	PVR_DEBUGFS_ENTRY_DATA *pvDebugFsEntry;
} PVR_DEBUGFS_RAW_DRIVER_STAT;
#endif

int PVRDebugFSInit(void);
void PVRDebugFSDeInit(void);

int PVRDebugFSCreateEntryDir(IMG_CHAR *pszName,
 	 	 	     PVR_DEBUGFS_DIR_DATA *psParentDir,
			     PVR_DEBUGFS_DIR_DATA **ppsNewDir);

void PVRDebugFSRemoveEntryDir(PVR_DEBUGFS_DIR_DATA **ppsDir);

int PVRDebugFSCreateEntry(const char *pszName,
			  PVR_DEBUGFS_DIR_DATA *psParentDir,
			  const struct seq_operations *psReadOps,
			  PVRSRV_ENTRY_WRITE_FUNC *pfnWrite,
			  PVRSRV_INC_FSENTRY_PVDATA_REFCNT_FN *pfnIncPvDataRefCnt,
			  PVRSRV_DEC_FSENTRY_PVDATA_REFCNT_FN *pfnDecPvDataRefCnt,
			  void *pvData,
			  PVR_DEBUGFS_ENTRY_DATA **ppsNewEntry);

void PVRDebugFSRemoveEntry(PVR_DEBUGFS_ENTRY_DATA **ppsDebugFSEntry);

PVR_DEBUGFS_DRIVER_STAT *PVRDebugFSCreateStatisticEntry(const char *pszName,
		       PVR_DEBUGFS_DIR_DATA *psDir,
		       OS_STATS_PRINT_FUNC *pfnStatsPrint,
		       PVRSRV_INC_STAT_MEM_REFCOUNT_FUNC *pfnIncStatMemRefCount,
		       PVRSRV_INC_STAT_MEM_REFCOUNT_FUNC *pfnDecStatMemRefCount,
		       void *pvData);

void PVRDebugFSRemoveStatisticEntry(PVR_DEBUGFS_DRIVER_STAT *psStatEntry);

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
PVR_DEBUGFS_RAW_DRIVER_STAT *PVRDebugFSCreateRawStatisticEntry(
                                             const IMG_CHAR *pszFileName,
                                             void *pvParentDir,
                                             OS_STATS_PRINT_FUNC* pfnStatsPrint);

void PVRDebugFSRemoveRawStatisticEntry(PVR_DEBUGFS_RAW_DRIVER_STAT *psStatEntry);
#endif

#endif /* !defined(__PVR_DEBUGFS_H__) */
