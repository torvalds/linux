/*************************************************************************/ /*!
@File
@Title          ProcFS implementation of Debug Info interface.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements osdi_impl.h API to provide access to driver's
                debug data via ProcFS.

                Note about locking in ProcFS module.

                Access to ProcFS is protected against the race where any
                file could be removed while being accessed or accessed while
                being removed. Any calls to proc_remove() will block until all
                operations are finished.

                See implementation of file operations (proc_reg_*()) and
                implementation of (un)use_pde() and proc_entry_rundown() in
                source/fs/proc/inode.c in Linux kernel sources for more details.

                Not about locking for sequential files.

                The seq_file objects have a mutex that protects access
                to all of the file operations hence all of the sequential
                *read* operations are protected.
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

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "img_types.h"
#include "img_defs.h"
#include "pvr_debug.h"
#include "pvr_procfs.h"
#include "osfunc.h"
#include "allocmem.h"
#include "pvr_bridge_k.h"
#include "pvr_uaccess.h"
#include "osdi_impl.h"

#define _DRIVER_THREAD_ENTER() \
	do { \
		PVRSRV_ERROR eLocalError = PVRSRVDriverThreadEnter(); \
		if (eLocalError != PVRSRV_OK) \
		{ \
			PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRVDriverThreadEnter failed: %s", \
				__func__, PVRSRVGetErrorString(eLocalError))); \
			return OSPVRSRVToNativeError(eLocalError); \
		} \
	} while (0)

#define _DRIVER_THREAD_EXIT() \
	PVRSRVDriverThreadExit()

#define PVR_DEBUGFS_PVR_DPF_LEVEL PVR_DBG_ERROR

typedef struct DFS_DIR
{
	struct proc_dir_entry *psDirEntry;
	struct DFS_DIR *psParentDir;
} DFS_DIR;

typedef struct DFS_ENTRY
{
	OSDI_IMPL_ENTRY sImplEntry;
	DI_ITERATOR_CB sIterCb;
} DFS_ENTRY;

typedef struct DFS_FILE
{
	struct proc_dir_entry *psFileEntry;
	struct DFS_DIR *psParentDir;
	const struct seq_operations *psSeqOps;
	struct DFS_ENTRY sEntry;
	DI_ENTRY_TYPE eType;
} DFS_FILE;

/* ----- native callbacks interface ----------------------------------------- */

static void _VPrintf(void *pvNativeHandle, const IMG_CHAR *pszFmt,
                     va_list pArgs)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	seq_vprintf(pvNativeHandle, pszFmt, pArgs);
#else
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];

	vsnprintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN, pszFmt, pArgs);
	seq_printf(pvNativeHandle, "%s", szBuffer);
#endif
}

static void _Puts(void *pvNativeHandle, const IMG_CHAR *pszStr)
{
	seq_puts(pvNativeHandle, pszStr);
}

static IMG_BOOL _HasOverflowed(void *pvNativeHandle)
{
	struct seq_file *psSeqFile = pvNativeHandle;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	return seq_has_overflowed(psSeqFile);
#else
	return psSeqFile->count == psSeqFile->size;
#endif
}

static OSDI_IMPL_ENTRY_CB _g_sEntryCallbacks = {
	.pfnVPrintf = _VPrintf,
	.pfnPuts = _Puts,
	.pfnHasOverflowed = _HasOverflowed,
};

/* ----- sequential file operations ----------------------------------------- */

static void *_Start(struct seq_file *psSeqFile, loff_t *puiPos)
{
	DFS_ENTRY *psEntry = psSeqFile->private;

	void *pvRet = psEntry->sIterCb.pfnStart(&psEntry->sImplEntry, puiPos);

	if (pvRet == DI_START_TOKEN)
	{
		return SEQ_START_TOKEN;
	}

	return pvRet;
}

static void _Stop(struct seq_file *psSeqFile, void *pvPriv)
{
	DFS_ENTRY *psEntry = psSeqFile->private;

	psEntry->sIterCb.pfnStop(&psEntry->sImplEntry, pvPriv);
}

static void *_Next(struct seq_file *psSeqFile, void *pvPriv, loff_t *puiPos)
{
	DFS_ENTRY *psEntry = psSeqFile->private;

	return psEntry->sIterCb.pfnNext(&psEntry->sImplEntry, pvPriv, puiPos);
}

static int _Show(struct seq_file *psSeqFile, void *pvPriv)
{
	DFS_ENTRY *psEntry = psSeqFile->private;

	if (pvPriv == SEQ_START_TOKEN)
	{
		pvPriv = DI_START_TOKEN;
	}

	return psEntry->sIterCb.pfnShow(&psEntry->sImplEntry, pvPriv);
}

static struct seq_operations _g_sSeqOps = {
	.start = _Start,
	.stop = _Stop,
	.next = _Next,
	.show = _Show
};

/* ----- file operations ---------------------------------------------------- */

static int _Open(struct inode *psINode, struct file *psFile)
{
	DFS_FILE *psDFSFile = PDE_DATA(psINode);
	int iRes;

	PVR_LOG_RETURN_IF_FALSE(psDFSFile != NULL, "psDFSFile is NULL", -EIO);

	_DRIVER_THREAD_ENTER();

	if (psDFSFile->sEntry.sIterCb.pfnStart != NULL)
	{
		iRes = seq_open(psFile, psDFSFile->psSeqOps);
	}
	else
	{
		/* private data is NULL as it's going to be set below */
		iRes = single_open(psFile, _Show, NULL);
	}

	if (iRes == 0)
	{
		struct seq_file *psSeqFile = psFile->private_data;

		DFS_ENTRY *psEntry = OSAllocMem(sizeof(*psEntry));
		if (psEntry == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: OSAllocMem() failed", __func__));
			iRes = -ENOMEM;
			goto return_;
		}

		*psEntry = psDFSFile->sEntry;
		psSeqFile->private = psEntry;
		psEntry->sImplEntry.pvNative = psSeqFile;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to seq_open psFile, returning %d",
		        __func__, iRes));
	}

return_:
	_DRIVER_THREAD_EXIT();

	return iRes;
}

static int _Close(struct inode *psINode, struct file *psFile)
{
	DFS_FILE *psDFSFile = PDE_DATA(psINode);
	DFS_ENTRY *psEntry;
	int iRes;

	PVR_LOG_RETURN_IF_FALSE(psDFSFile != NULL, "psDFSFile is NULL",
	                        -EIO);

	_DRIVER_THREAD_ENTER();

	/* save pointer to DFS_ENTRY */
	psEntry = ((struct seq_file *) psFile->private_data)->private;

	if (psDFSFile->sEntry.sIterCb.pfnStart != NULL)
	{
		iRes = seq_release(psINode, psFile);
	}
	else
	{
		iRes = single_release(psINode, psFile);
	}

	/* free DFS_ENTRY allocated in _Open */
	OSFreeMem(psEntry);

	/* Validation check as seq_release (and single_release which calls it)
	 * never fail */
	if (iRes != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to release psFile, returning %d",
		        __func__, iRes));
	}

	_DRIVER_THREAD_EXIT();

	return iRes;
}

static ssize_t _Read(struct file *psFile, char __user *pcBuffer,
                     size_t uiCount, loff_t *puiPos)
{
	DFS_FILE *psDFSFile = PDE_DATA(psFile->f_path.dentry->d_inode);
	ssize_t iRes = -1;

	_DRIVER_THREAD_ENTER();

	if (psDFSFile->eType == DI_ENTRY_TYPE_GENERIC)
	{
		iRes = seq_read(psFile, pcBuffer, uiCount, puiPos);
		if (iRes < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: filed to read from file pfnRead() "
			        "returned %zd", __func__, iRes));
			goto return_;
		}
	}
	else if (psDFSFile->eType == DI_ENTRY_TYPE_RANDOM_ACCESS)
	{
		DFS_ENTRY *psEntry = &psDFSFile->sEntry;
		IMG_UINT64 ui64Count = uiCount, ui64Pos;

		IMG_CHAR *pcLocalBuffer = OSAllocMem(uiCount);
		PVR_GOTO_IF_FALSE(pcLocalBuffer != NULL, return_);

		iRes = psEntry->sIterCb.pfnRead(pcLocalBuffer, ui64Count, &ui64Pos,
		                                psEntry->sImplEntry.pvPrivData);
		if (iRes < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: filed to read from file pfnRead() "
			        "returned %zd", __func__, iRes));
			OSFreeMem(pcLocalBuffer);
			goto return_;
		}

		if (pvr_copy_to_user(pcBuffer, pcLocalBuffer, iRes) != 0)
		{
			iRes = -1;
		}

		OSFreeMem(pcLocalBuffer);

		*puiPos = ui64Pos;
	}

return_:
	_DRIVER_THREAD_EXIT();

	return iRes;
}

static loff_t _LSeek(struct file *psFile, loff_t iOffset, int iOrigin)
{
	DFS_FILE *psDFSFile = PDE_DATA(psFile->f_path.dentry->d_inode);
	loff_t iRes = -1;

	_DRIVER_THREAD_ENTER();

	if (psDFSFile->eType == DI_ENTRY_TYPE_GENERIC)
	{
		iRes = seq_lseek(psFile, iOffset, iOrigin);
		if (iRes < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: filed to set file position to "
			        "offset %lld, pfnSeek() returned %lld", __func__,
			        iOffset, iRes));
			goto return_;
		}
	}
	else if (psDFSFile->eType == DI_ENTRY_TYPE_RANDOM_ACCESS)
	{
		DFS_ENTRY *psEntry = &psDFSFile->sEntry;
		IMG_UINT64 ui64Pos;

		switch (iOrigin)
		{
			case SEEK_SET:
				ui64Pos = psFile->f_pos + iOffset;
				break;
			case SEEK_CUR:
				ui64Pos = iOffset;
				break;
			case SEEK_END:
				/* not supported as we don't know the file size here */
				/* fall through */
			default:
				return -1;
		}

		/* only pass the absolute position to the callback, it's up to the
		 * implementer to determine if the position is valid */

		iRes = psEntry->sIterCb.pfnSeek(ui64Pos,
		                                psEntry->sImplEntry.pvPrivData);
		if (iRes < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: filed to set file position to "
			        "offset %lld, pfnSeek() returned %lld", __func__,
			        iOffset, iRes));
			goto return_;
		}

		psFile->f_pos = ui64Pos;
	}

return_:
	_DRIVER_THREAD_EXIT();

	return iRes;
}

static ssize_t _Write(struct file *psFile, const char __user *pszBuffer,
                      size_t uiCount, loff_t *puiPos)
{
	struct inode *psINode = psFile->f_path.dentry->d_inode;
	DFS_FILE *psDFSFile = PDE_DATA(psINode);
	DI_ITERATOR_CB *psIter = &psDFSFile->sEntry.sIterCb;
	IMG_CHAR *pcLocalBuffer;
	IMG_UINT64 ui64Count = uiCount + 1, ui64Pos = *puiPos;
	IMG_INT64 i64Res = -EIO;

	PVR_LOG_RETURN_IF_FALSE(psDFSFile != NULL, "psDFSFile is NULL",
	                        -EIO);
	PVR_LOG_RETURN_IF_FALSE(psIter->pfnWrite != NULL, "pfnWrite is NULL",
	                        -EIO);

	_DRIVER_THREAD_ENTER();

	pcLocalBuffer = OSAllocMem(ui64Count);
	PVR_LOG_GOTO_IF_FALSE(pcLocalBuffer != NULL, "OSAllocMem() failed",
	                      return_);

	i64Res = pvr_copy_from_user(pcLocalBuffer, pszBuffer, uiCount);
	PVR_LOG_GOTO_IF_FALSE(i64Res == 0, "pvr_copy_from_user() failed",
	                      free_local_buffer_);

	/* ensure that the framework user gets a NUL terminated buffer */
	pcLocalBuffer[ui64Count - 1] = '\0';

	i64Res = psIter->pfnWrite(pcLocalBuffer, ui64Count, &ui64Pos,
	                          psDFSFile->sEntry.sImplEntry.pvPrivData);
	PVR_LOG_GOTO_IF_FALSE(i64Res >= 0, "pfnWrite failed", free_local_buffer_);

	*puiPos = ui64Pos;

free_local_buffer_:
	OSFreeMem(pcLocalBuffer);

return_:
	_DRIVER_THREAD_EXIT();

	return i64Res;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))

static const struct file_operations _g_psFileOpsGen = {
	.owner = THIS_MODULE,
	.open = _Open,
	.release = _Close,
	.read = _Read,
	.llseek = _LSeek,
	.write = _Write,
};

static const struct file_operations _g_psFileOpsRndAcc = {
	.owner = THIS_MODULE,
	.read = _Read,
	.llseek = _LSeek,
	.write = _Write,
};

#else

static const struct proc_ops _g_psFileOpsGen = {
	.proc_open    = _Open,
	.proc_read    = _Read,
	.proc_write   = _Write,
	.proc_lseek   = _LSeek,
	.proc_release = _Close,
};

static const struct proc_ops _g_psFileOpsRndAcc = {
	.proc_read  = _Read,
	.proc_write = _Write,
	.proc_lseek = _LSeek,
};

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)) */

/* ----- DI implementation interface ---------------------------------------- */

static PVRSRV_ERROR _Init(void)
{
	return PVRSRV_OK;
}

static void _DeInit(void)
{
}

static PVRSRV_ERROR _CreateFile(const IMG_CHAR *pszName,
                                DI_ENTRY_TYPE eType,
                                const DI_ITERATOR_CB *psIterCb,
                                void *pvPrivData,
                                void *pvParentDir,
                                void **pvFile)
{
	DFS_DIR *psParentDir = pvParentDir;
	DFS_FILE *psFile;
	umode_t uiMode = S_IFREG;
	struct proc_dir_entry *psEntry;
	PVRSRV_ERROR eError;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
	const struct file_operations *psProcOps = NULL;
#else
	const struct proc_ops *psProcOps = NULL;
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)) */

	PVR_LOG_RETURN_IF_INVALID_PARAM(pvFile != NULL, "pvFile");
	PVR_LOG_RETURN_IF_INVALID_PARAM(pvParentDir != NULL, "pvParentDir");

	switch (eType)
	{
		case DI_ENTRY_TYPE_GENERIC:
			psProcOps = &_g_psFileOpsGen;
			break;
		case DI_ENTRY_TYPE_RANDOM_ACCESS:
			psProcOps = &_g_psFileOpsRndAcc;
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR, "eType invalid in %s()", __func__));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto return_;
	}

	psFile = OSAllocMem(sizeof(*psFile));
	PVR_LOG_GOTO_IF_NOMEM(psFile, eError, return_);

	uiMode |= psIterCb->pfnShow != NULL || psIterCb->pfnRead != NULL ?
	        S_IRUGO : 0;
	uiMode |= psIterCb->pfnWrite != NULL ? S_IWUSR : 0;

	psEntry = proc_create_data(pszName, uiMode, psParentDir->psDirEntry,
	                           psProcOps, psFile);
	if (IS_ERR_OR_NULL(psEntry))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Cannot create debugfs '%s' file",
		        __func__, pszName));

		eError = psEntry == NULL ?
		        PVRSRV_ERROR_OUT_OF_MEMORY : PVRSRV_ERROR_INVALID_DEVICE;
		goto free_file_;
	}

	psFile->eType = eType;
	psFile->psSeqOps = &_g_sSeqOps;
	psFile->sEntry.sIterCb = *psIterCb;
	psFile->sEntry.sImplEntry.pvPrivData = pvPrivData;
	psFile->sEntry.sImplEntry.pvNative = NULL;
	psFile->sEntry.sImplEntry.psCb = &_g_sEntryCallbacks;
	psFile->psParentDir = psParentDir;
	psFile->psFileEntry = psEntry;

	*pvFile = psFile;

	return PVRSRV_OK;

free_file_:
	OSFreeMem(psFile);

return_:
	return eError;
}

static void _DestroyFile(void *pvFile)
{
	DFS_FILE *psFile = pvFile;

	PVR_ASSERT(psFile != NULL);

	proc_remove(psFile->psFileEntry);
	OSFreeMem(psFile);
}

static PVRSRV_ERROR _CreateDir(const IMG_CHAR *pszName,
                               void *pvParentDir,
                               void **ppvDir)
{
	DFS_DIR *psNewDir;
	struct proc_dir_entry *psDirEntry, *psParentDir = NULL;

	PVR_LOG_RETURN_IF_INVALID_PARAM(pszName != NULL, "pszName");
	PVR_LOG_RETURN_IF_INVALID_PARAM(ppvDir != NULL, "ppvDir");

	psNewDir = OSAllocMem(sizeof(*psNewDir));
	PVR_LOG_RETURN_IF_NOMEM(psNewDir, "OSAllocMem");

	psNewDir->psParentDir = pvParentDir;

	if (pvParentDir != NULL)
	{
		psParentDir = psNewDir->psParentDir->psDirEntry;
	}

	psDirEntry = proc_mkdir(pszName, psParentDir);
	if (IS_ERR_OR_NULL(psDirEntry))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Cannot create '%s' debugfs directory",
		        __func__, pszName));
		OSFreeMem(psNewDir);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psNewDir->psDirEntry = psDirEntry;
	*ppvDir = psNewDir;

	return PVRSRV_OK;
}

static void _DestroyDir(void *pvDir)
{
	DFS_DIR *psDir = pvDir;

	PVR_ASSERT(psDir != NULL);

	proc_remove(psDir->psDirEntry);
	OSFreeMem(psDir);
}

PVRSRV_ERROR PVRProcFsRegister(void)
{
	OSDI_IMPL_CB sImplCb = {
		.pfnInit = _Init,
		.pfnDeInit = _DeInit,
		.pfnCreateEntry = _CreateFile,
		.pfnDestroyEntry = _DestroyFile,
		.pfnCreateGroup = _CreateDir,
		.pfnDestroyGroup = _DestroyDir
	};

	return DIRegisterImplementation("procfs", &sImplCb);
}
