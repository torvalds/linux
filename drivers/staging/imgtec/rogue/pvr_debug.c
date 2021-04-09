/*************************************************************************/ /*!
@File
@Title          Debug Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Provides kernel side Debug Functionality.
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
#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <stdarg.h>

#include "allocmem.h"
#include "pvrversion.h"
#include "img_types.h"
#include "servicesext.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "pvr_debugfs.h"
#include "linkage.h"
#include "pvr_uaccess.h"
#include "pvrsrv.h"
#include "rgxdevice.h"
#include "rgxdebug.h"
#include "rgxinit.h"
#include "lists.h"
#include "osfunc.h"

/* Handle used by DebugFS to get GPU utilisation stats */
static IMG_HANDLE ghGpuUtilUserDebugFS = NULL;

#if defined(PVRSRV_NEED_PVR_DPF)

/******** BUFFERED LOG MESSAGES ********/

/* Because we don't want to have to handle CCB wrapping, each buffered
 * message is rounded up to PVRSRV_DEBUG_CCB_MESG_MAX bytes. This means
 * there is the same fixed number of messages that can be stored,
 * regardless of message length.
 */

#if defined(PVRSRV_DEBUG_CCB_MAX)

#define PVRSRV_DEBUG_CCB_MESG_MAX	PVR_MAX_DEBUG_MESSAGE_LEN

#include <linux/syscalls.h>
#include <linux/time.h>

typedef struct
{
	const IMG_CHAR *pszFile;
	IMG_INT iLine;
	IMG_UINT32 ui32TID;
	IMG_UINT32 ui32PID;
	IMG_CHAR pcMesg[PVRSRV_DEBUG_CCB_MESG_MAX];
	struct timeval sTimeVal;
}
PVRSRV_DEBUG_CCB;

static PVRSRV_DEBUG_CCB gsDebugCCB[PVRSRV_DEBUG_CCB_MAX] = { { 0 } };

static IMG_UINT giOffset = 0;

static DEFINE_MUTEX(gsDebugCCBMutex);

static void
AddToBufferCCB(const IMG_CHAR *pszFileName, IMG_UINT32 ui32Line,
			   const IMG_CHAR *szBuffer)
{
	mutex_lock(&gsDebugCCBMutex);

	gsDebugCCB[giOffset].pszFile = pszFileName;
	gsDebugCCB[giOffset].iLine   = ui32Line;
	gsDebugCCB[giOffset].ui32TID = current->pid;
	gsDebugCCB[giOffset].ui32PID = current->tgid;

	do_gettimeofday(&gsDebugCCB[giOffset].sTimeVal);

	strncpy(gsDebugCCB[giOffset].pcMesg, szBuffer, PVRSRV_DEBUG_CCB_MESG_MAX - 1);
	gsDebugCCB[giOffset].pcMesg[PVRSRV_DEBUG_CCB_MESG_MAX - 1] = 0;

	giOffset = (giOffset + 1) % PVRSRV_DEBUG_CCB_MAX;

	mutex_unlock(&gsDebugCCBMutex);
}

IMG_EXPORT void PVRSRVDebugPrintfDumpCCB(void)
{
	int i;

	mutex_lock(&gsDebugCCBMutex);
	
	for (i = 0; i < PVRSRV_DEBUG_CCB_MAX; i++)
	{
		PVRSRV_DEBUG_CCB *psDebugCCBEntry =
			&gsDebugCCB[(giOffset + i) % PVRSRV_DEBUG_CCB_MAX];

		/* Early on, we won't have PVRSRV_DEBUG_CCB_MAX messages */
		if (!psDebugCCBEntry->pszFile)
		{
			continue;
		}

		printk(KERN_ERR "%s:%d: (%ld.%ld, tid=%u, pid=%u) %s\n",
			   psDebugCCBEntry->pszFile,
			   psDebugCCBEntry->iLine,
			   (long)psDebugCCBEntry->sTimeVal.tv_sec,
			   (long)psDebugCCBEntry->sTimeVal.tv_usec,
			   psDebugCCBEntry->ui32TID,
			   psDebugCCBEntry->ui32PID,
			   psDebugCCBEntry->pcMesg);

		/* Clear this entry so it doesn't get printed the next time again. */
		psDebugCCBEntry->pszFile = NULL;
	}

	mutex_unlock(&gsDebugCCBMutex);
}

#else /* defined(PVRSRV_DEBUG_CCB_MAX) */
static INLINE void
AddToBufferCCB(const IMG_CHAR *pszFileName, IMG_UINT32 ui32Line,
			   const IMG_CHAR *szBuffer)
{
	(void)pszFileName;
	(void)szBuffer;
	(void)ui32Line;
}

IMG_EXPORT void PVRSRVDebugPrintfDumpCCB(void)
{
	/* Not available */
}

#endif /* defined(PVRSRV_DEBUG_CCB_MAX) */

#endif /* defined(PVRSRV_NEED_PVR_DPF) */

static IMG_BOOL VBAppend(IMG_CHAR *pszBuf, IMG_UINT32 ui32BufSiz,
						 const IMG_CHAR *pszFormat, va_list VArgs)
						 __printf(3, 0);


#if defined(PVRSRV_NEED_PVR_DPF)

#define PVR_MAX_FILEPATH_LEN 256

static IMG_BOOL BAppend(IMG_CHAR *pszBuf, IMG_UINT32 ui32BufSiz,
						const IMG_CHAR *pszFormat, ...)
						__printf(3, 4);

/* NOTE: Must NOT be static! Used in module.c.. */
IMG_UINT32 gPVRDebugLevel =
	(
	 DBGPRIV_FATAL | DBGPRIV_ERROR | DBGPRIV_WARNING

#if defined(PVRSRV_DEBUG_CCB_MAX)
	 | DBGPRIV_BUFFERED
#endif /* defined(PVRSRV_DEBUG_CCB_MAX) */

#if defined(PVR_DPF_ADHOC_DEBUG_ON)
	 | DBGPRIV_DEBUG
#endif /* defined(PVR_DPF_ADHOC_DEBUG_ON) */
	);

#endif /* defined(PVRSRV_NEED_PVR_DPF) || defined(PVRSRV_NEED_PVR_TRACE) */

#define	PVR_MAX_MSG_LEN PVR_MAX_DEBUG_MESSAGE_LEN

/* Message buffer for non-IRQ messages */
static IMG_CHAR gszBufferNonIRQ[PVR_MAX_MSG_LEN + 1];

/* Message buffer for IRQ messages */
static IMG_CHAR gszBufferIRQ[PVR_MAX_MSG_LEN + 1];

/* The lock is used to control access to gszBufferNonIRQ */
static DEFINE_MUTEX(gsDebugMutexNonIRQ);

/* The lock is used to control access to gszBufferIRQ */
static DEFINE_SPINLOCK(gsDebugLockIRQ);

#define	USE_SPIN_LOCK (in_interrupt() || !preemptible())

static inline void GetBufferLock(unsigned long *pulLockFlags)
{
	if (USE_SPIN_LOCK)
	{
		spin_lock_irqsave(&gsDebugLockIRQ, *pulLockFlags);
	}
	else
	{
		mutex_lock(&gsDebugMutexNonIRQ);
	}
}

static inline void ReleaseBufferLock(unsigned long ulLockFlags)
{
	if (USE_SPIN_LOCK)
	{
		spin_unlock_irqrestore(&gsDebugLockIRQ, ulLockFlags);
	}
	else
	{
		mutex_unlock(&gsDebugMutexNonIRQ);
	}
}

static inline void SelectBuffer(IMG_CHAR **ppszBuf, IMG_UINT32 *pui32BufSiz)
{
	if (USE_SPIN_LOCK)
	{
		*ppszBuf = gszBufferIRQ;
		*pui32BufSiz = sizeof(gszBufferIRQ);
	}
	else
	{
		*ppszBuf = gszBufferNonIRQ;
		*pui32BufSiz = sizeof(gszBufferNonIRQ);
	}
}

/*
 * Append a string to a buffer using formatted conversion.
 * The function takes a variable number of arguments, pointed
 * to by the var args list.
 */
static IMG_BOOL VBAppend(IMG_CHAR *pszBuf, IMG_UINT32 ui32BufSiz, const IMG_CHAR *pszFormat, va_list VArgs)
{
	IMG_UINT32 ui32Used;
	IMG_UINT32 ui32Space;
	IMG_INT32 i32Len;

	ui32Used = strlen(pszBuf);
	BUG_ON(ui32Used >= ui32BufSiz);
	ui32Space = ui32BufSiz - ui32Used;

	i32Len = vsnprintf(&pszBuf[ui32Used], ui32Space, pszFormat, VArgs);
	pszBuf[ui32BufSiz - 1] = 0;

	/* Return true if string was truncated */
	return i32Len < 0 || i32Len >= (IMG_INT32)ui32Space;
}

/*************************************************************************/ /*!
@Function       PVRSRVReleasePrintf
@Description    To output an important message to the user in release builds
@Input          pszFormat   The message format string
@Input          ...         Zero or more arguments for use by the format string
*/ /**************************************************************************/
void PVRSRVReleasePrintf(const IMG_CHAR *pszFormat, ...)
{
	va_list vaArgs;
	unsigned long ulLockFlags = 0;
	IMG_CHAR *pszBuf;
	IMG_UINT32 ui32BufSiz;
	IMG_INT32  result;

	SelectBuffer(&pszBuf, &ui32BufSiz);

	va_start(vaArgs, pszFormat);

	GetBufferLock(&ulLockFlags);

	result = snprintf(pszBuf, (ui32BufSiz - 2), "PVR_K: %u: ", current->pid);
	PVR_ASSERT(result>0);
	ui32BufSiz -= result;

	if (VBAppend(pszBuf, ui32BufSiz, pszFormat, vaArgs))
	{
		printk(KERN_ERR "PVR_K:(Message Truncated): %s\n", pszBuf);
	}
	else
	{
		printk(KERN_ERR "%s\n", pszBuf);
	}

	ReleaseBufferLock(ulLockFlags);
	va_end(vaArgs);
}

#if defined(PVRSRV_NEED_PVR_TRACE)

/*************************************************************************/ /*!
@Function       PVRTrace
@Description    To output a debug message to the user
@Input          pszFormat   The message format string
@Input          ...         Zero or more arguments for use by the format string
*/ /**************************************************************************/
void PVRSRVTrace(const IMG_CHAR *pszFormat, ...)
{
	va_list VArgs;
	unsigned long ulLockFlags = 0;
	IMG_CHAR *pszBuf;
	IMG_UINT32 ui32BufSiz;
	IMG_INT32  result;

	SelectBuffer(&pszBuf, &ui32BufSiz);

	va_start(VArgs, pszFormat);

	GetBufferLock(&ulLockFlags);

	result = snprintf(pszBuf, (ui32BufSiz - 2), "PVR: %u: ", current->pid);
	PVR_ASSERT(result>0);
	ui32BufSiz -= result;

	if (VBAppend(pszBuf, ui32BufSiz, pszFormat, VArgs))
	{
		printk(KERN_ERR "PVR_K:(Message Truncated): %s\n", pszBuf);
	}
	else
	{
		printk(KERN_ERR "%s\n", pszBuf);
	}

	ReleaseBufferLock(ulLockFlags);

	va_end(VArgs);
}

#endif /* defined(PVRSRV_NEED_PVR_TRACE) */

#if defined(PVRSRV_NEED_PVR_DPF)

/*
 * Append a string to a buffer using formatted conversion.
 * The function takes a variable number of arguments, calling
 * VBAppend to do the actual work.
 */
static IMG_BOOL BAppend(IMG_CHAR *pszBuf, IMG_UINT32 ui32BufSiz, const IMG_CHAR *pszFormat, ...)
{
	va_list VArgs;
	IMG_BOOL bTrunc;

	va_start (VArgs, pszFormat);

	bTrunc = VBAppend(pszBuf, ui32BufSiz, pszFormat, VArgs);

	va_end (VArgs);

	return bTrunc;
}

/*************************************************************************/ /*!
@Function       PVRSRVDebugPrintf
@Description    To output a debug message to the user
@Input          uDebugLevel The current debug level
@Input          pszFile     The source file generating the message
@Input          uLine       The line of the source file
@Input          pszFormat   The message format string
@Input          ...         Zero or more arguments for use by the format string
*/ /**************************************************************************/
void PVRSRVDebugPrintf(IMG_UINT32 ui32DebugLevel,
			   const IMG_CHAR *pszFullFileName,
			   IMG_UINT32 ui32Line,
			   const IMG_CHAR *pszFormat,
			   ...)
{
	IMG_BOOL bNoLoc;
	const IMG_CHAR *pszFileName = pszFullFileName;
	IMG_CHAR *pszLeafName;

	bNoLoc = (IMG_BOOL)((ui32DebugLevel & DBGPRIV_CALLTRACE) |
						(ui32DebugLevel & DBGPRIV_BUFFERED)) ? IMG_TRUE : IMG_FALSE;

	if (gPVRDebugLevel & ui32DebugLevel)
	{
		va_list vaArgs;
		unsigned long ulLockFlags = 0;
		IMG_CHAR *pszBuf;
		IMG_UINT32 ui32BufSiz;

		SelectBuffer(&pszBuf, &ui32BufSiz);

		va_start(vaArgs, pszFormat);

		GetBufferLock(&ulLockFlags);

		switch (ui32DebugLevel)
		{
			case DBGPRIV_FATAL:
			{
				strncpy(pszBuf, "PVR_K:(Fatal): ", (ui32BufSiz - 2));
				break;
			}
			case DBGPRIV_ERROR:
			{
				strncpy(pszBuf, "PVR_K:(Error): ", (ui32BufSiz - 2));
				break;
			}
			case DBGPRIV_WARNING:
			{
				strncpy(pszBuf, "PVR_K:(Warn):  ", (ui32BufSiz - 2));
				break;
			}
			case DBGPRIV_MESSAGE:
			{
				strncpy(pszBuf, "PVR_K:(Mesg):  ", (ui32BufSiz - 2));
				break;
			}
			case DBGPRIV_VERBOSE:
			{
				strncpy(pszBuf, "PVR_K:(Verb):  ", (ui32BufSiz - 2));
				break;
			}
			case DBGPRIV_DEBUG:
			{
				strncpy(pszBuf, "PVR_K:(Debug): ", (ui32BufSiz - 2));
				break;
			}
			case DBGPRIV_CALLTRACE:
			case DBGPRIV_ALLOC:
			case DBGPRIV_BUFFERED:
			default:
			{
				strncpy(pszBuf, "PVR_K:  ", (ui32BufSiz - 2));
				break;
			}
		}
		pszBuf[ui32BufSiz - 1] = '\0';

		if (current->pid == task_tgid_nr(current))
		{
			(void) BAppend(pszBuf, ui32BufSiz, "%5u: ", current->pid);
		}
		else
		{
			(void) BAppend(pszBuf, ui32BufSiz, "%5u-%5u: ", task_tgid_nr(current) /* pid id of group*/, current->pid /* task id */);
		}

		if (VBAppend(pszBuf, ui32BufSiz, pszFormat, vaArgs))
		{
			printk(KERN_ERR "PVR_K:(Message Truncated): %s\n", pszBuf);
		}
		else
		{
			IMG_BOOL bTruncated = IMG_FALSE;

#if !defined(__sh__)
			pszLeafName = (IMG_CHAR *)strrchr (pszFileName, '/');

			if (pszLeafName)
			{
				pszFileName = pszLeafName+1;
			}
#endif /* __sh__ */

#if defined(DEBUG)
			{
				static const IMG_CHAR *lastFile = NULL;

				if (lastFile == pszFileName)
				{
					bTruncated = BAppend(pszBuf, ui32BufSiz, " [%u]", ui32Line);
				}
				else
				{
					bTruncated = BAppend(pszBuf, ui32BufSiz, " [%s:%u]", pszFileName, ui32Line);
					lastFile = pszFileName;
				}
			}
#endif

			if (bTruncated)
			{
				printk(KERN_ERR "PVR_K:(Message Truncated): %s\n", pszBuf);
			}
			else
			{
				if (ui32DebugLevel & DBGPRIV_BUFFERED)
				{
					AddToBufferCCB(pszFileName, ui32Line, pszBuf);
				}
				else
				{
					printk(KERN_ERR "%s\n", pszBuf);
				}
			}
		}

		ReleaseBufferLock(ulLockFlags);

		va_end (vaArgs);
	}
}

#endif /* PVRSRV_NEED_PVR_DPF */


/*************************************************************************/ /*!
 Version DebugFS entry
*/ /**************************************************************************/

static void *_DebugVersionCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode,
					  va_list va)
{
	loff_t *puiCurrentPosition = va_arg(va, loff_t *);
	loff_t uiPosition = va_arg(va, loff_t);
	loff_t uiCurrentPosition = *puiCurrentPosition;

	(*puiCurrentPosition)++;

	return (uiCurrentPosition == uiPosition) ? psDevNode : NULL;
}

static void *_DebugVersionSeqStart(struct seq_file *psSeqFile,
				   loff_t *puiPosition)
{
	PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA *)psSeqFile->private;
	loff_t uiCurrentPosition = 1;

	if (*puiPosition == 0)
	{
		return SEQ_START_TOKEN;
	}

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugVersionCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *puiPosition);
}

static void _DebugVersionSeqStop(struct seq_file *psSeqFile, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_DebugVersionSeqNext(struct seq_file *psSeqFile,
				  void *pvData,
				  loff_t *puiPosition)
{
	PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA *)psSeqFile->private;
	loff_t uiCurrentPosition = 1;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*puiPosition)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugVersionCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *puiPosition);
}

static int _DebugVersionSeqShow(struct seq_file *psSeqFile, void *pvData)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if (pvData == SEQ_START_TOKEN)
	{
		if(psPVRSRVData->sDriverInfo.bIsNoMatch)
		{
			seq_printf(psSeqFile, "Driver UM Version: %d (%s) %s\n",
					psPVRSRVData->sDriverInfo.sUMBuildInfo.ui32BuildRevision,
				    (psPVRSRVData->sDriverInfo.sUMBuildInfo.ui32BuildType)?"release":"debug",
				    PVR_BUILD_DIR);
			seq_printf(psSeqFile, "Driver KM Version: %d (%s) %s\n",
								psPVRSRVData->sDriverInfo.sKMBuildInfo.ui32BuildRevision,
							    (BUILD_TYPE_RELEASE == psPVRSRVData->sDriverInfo.sKMBuildInfo.ui32BuildType)?"release":"debug",
							    PVR_BUILD_DIR);
		}else
		{
			seq_printf(psSeqFile, "Driver Version: %s (%s) %s\n",
						   PVRVERSION_STRING,
						   PVR_BUILD_TYPE, PVR_BUILD_DIR);
		}
	}
	else if (pvData != NULL)
	{
		PVRSRV_DEVICE_NODE *psDevNode = (PVRSRV_DEVICE_NODE *)pvData;

		seq_printf(psSeqFile, "\nDevice Name: %s\n", psDevNode->psDevConfig->pszName);

		if (psDevNode->psDevConfig->pszVersion)
		{
			seq_printf(psSeqFile, "Device Version: %s\n", psDevNode->psDevConfig->pszVersion);
		}

		if (psDevNode->pfnDeviceVersionString)
		{
			IMG_CHAR *pszDeviceVersionString;

			if (psDevNode->pfnDeviceVersionString(psDevNode, &pszDeviceVersionString) == PVRSRV_OK)
			{
				seq_printf(psSeqFile, "%s\n", pszDeviceVersionString);

				OSFreeMem(pszDeviceVersionString);
			}
		}
	}

	return 0;
}

static struct seq_operations gsDebugVersionReadOps =
{
	.start = _DebugVersionSeqStart,
	.stop = _DebugVersionSeqStop,
	.next = _DebugVersionSeqNext,
	.show = _DebugVersionSeqShow,
};

/*************************************************************************/ /*!
 Status DebugFS entry
*/ /**************************************************************************/

static void *_DebugStatusCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode,
										 va_list va)
{
	loff_t *puiCurrentPosition = va_arg(va, loff_t *);
	loff_t uiPosition = va_arg(va, loff_t);
	loff_t uiCurrentPosition = *puiCurrentPosition;

	(*puiCurrentPosition)++;

	return (uiCurrentPosition == uiPosition) ? psDevNode : NULL;
}

static void *_DebugStatusSeqStart(struct seq_file *psSeqFile,
								  loff_t *puiPosition)
{
	PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA *)psSeqFile->private;
	loff_t uiCurrentPosition = 1;

	if (*puiPosition == 0)
	{
		return SEQ_START_TOKEN;
	}

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugStatusCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *puiPosition);
}

static void _DebugStatusSeqStop(struct seq_file *psSeqFile, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_DebugStatusSeqNext(struct seq_file *psSeqFile,
								 void *pvData,
								 loff_t *puiPosition)
{
	PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA *)psSeqFile->private;
	loff_t uiCurrentPosition = 1;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*puiPosition)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugStatusCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *puiPosition);
}

static int _DebugStatusSeqShow(struct seq_file *psSeqFile, void *pvData)
{
	if (pvData == SEQ_START_TOKEN)
	{
		PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA *)psSeqFile->private;

		if (psPVRSRVData != NULL)
		{
			switch (psPVRSRVData->eServicesState)
			{
				case PVRSRV_SERVICES_STATE_OK:
					seq_printf(psSeqFile, "Driver Status:   OK\n");
					break;
				case PVRSRV_SERVICES_STATE_BAD:
					seq_printf(psSeqFile, "Driver Status:   BAD\n");
					break;
				default:
					seq_printf(psSeqFile, "Driver Status:   %d\n", psPVRSRVData->eServicesState);
					break;
			}
		}
	}
	else if (pvData != NULL)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;
		IMG_CHAR           *pszStatus = "";
		IMG_CHAR           *pszReason = "";
		PVRSRV_DEVICE_HEALTH_STATUS eHealthStatus;
		PVRSRV_DEVICE_HEALTH_REASON eHealthReason;
		
		/* Update the health status now if possible... */
		if (psDeviceNode->pfnUpdateHealthStatus)
		{
			psDeviceNode->pfnUpdateHealthStatus(psDeviceNode, IMG_FALSE);
		}
		eHealthStatus = OSAtomicRead(&psDeviceNode->eHealthStatus);
		eHealthReason = OSAtomicRead(&psDeviceNode->eHealthReason);
		
		switch (eHealthStatus)
		{
			case PVRSRV_DEVICE_HEALTH_STATUS_OK:  pszStatus = "OK";  break;
			case PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING:  pszStatus = "NOT RESPONDING";  break;
			case PVRSRV_DEVICE_HEALTH_STATUS_DEAD:  pszStatus = "DEAD";  break;
			default:  pszStatus = "UNKNOWN";  break;
		}

		switch (eHealthReason)
		{
			case PVRSRV_DEVICE_HEALTH_REASON_NONE:  pszReason = "";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_ASSERTED:  pszReason = " (FW Assert)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_POLL_FAILING:  pszReason = " (Poll failure)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_TIMEOUTS:  pszReason = " (Global Event Object timeouts rising)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_CORRUPT:  pszReason = " (KCCB offset invalid)";  break;
			case PVRSRV_DEVICE_HEALTH_REASON_QUEUE_STALLED:  pszReason = " (KCCB stalled)";  break;
			default:  pszReason = " (Unknown reason)";  break;
		}

		seq_printf(psSeqFile, "Firmware Status: %s%s\n", pszStatus, pszReason);

#if defined(PVRSRV_GPUVIRT_GUESTDRV)
		/*
		 * Guest drivers do not support the following functionality:
		 *	- Perform actual on-chip fw tracing
		 *	- Collect actual on-chip GPU utilization stats
		 *	- Perform actual on-chip GPU power/dvfs management
		 */
		PVR_UNREFERENCED_PARAMETER(ghGpuUtilUserDebugFS);
#else
		/* Write other useful stats to aid the test cycle... */
		if (psDeviceNode->pvDevice != NULL)
		{
			PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
			RGXFWIF_TRACEBUF *psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBuf;

			/* Calculate the number of HWR events in total across all the DMs... */
			if (psRGXFWIfTraceBufCtl != NULL)
			{
				IMG_UINT32 ui32HWREventCount = 0;
				IMG_UINT32 ui32CRREventCount = 0;
				IMG_UINT32 ui32DMIndex;

				for (ui32DMIndex = 0; ui32DMIndex < psDevInfo->sDevFeatureCfg.ui32MAXDMCount; ui32DMIndex++)
				{
					ui32HWREventCount += psRGXFWIfTraceBufCtl->aui32HwrDmLockedUpCount[ui32DMIndex];
					ui32CRREventCount += psRGXFWIfTraceBufCtl->aui32HwrDmOverranCount[ui32DMIndex];
				}

				seq_printf(psSeqFile, "HWR Event Count: %d\n", ui32HWREventCount);
				seq_printf(psSeqFile, "CRR Event Count: %d\n", ui32CRREventCount);
			}

			/* Write the number of APM events... */
			seq_printf(psSeqFile, "APM Event Count: %d\n", psDevInfo->ui32ActivePMReqTotal);

			/* Write the current GPU Utilisation values... */
			if (psDevInfo->pfnGetGpuUtilStats &&
				eHealthStatus == PVRSRV_DEVICE_HEALTH_STATUS_OK)
			{
				RGXFWIF_GPU_UTIL_STATS sGpuUtilStats;
				PVRSRV_ERROR eError = PVRSRV_OK;

				eError = psDevInfo->pfnGetGpuUtilStats(psDeviceNode,
													   ghGpuUtilUserDebugFS,
													   &sGpuUtilStats);

				if ((eError == PVRSRV_OK) &&
					((IMG_UINT32)sGpuUtilStats.ui64GpuStatCumulative))
				{
					IMG_UINT64 util;
					IMG_UINT32 rem;

					util = 100 * (sGpuUtilStats.ui64GpuStatActiveHigh +
								  sGpuUtilStats.ui64GpuStatActiveLow);
					util = OSDivide64(util, (IMG_UINT32)sGpuUtilStats.ui64GpuStatCumulative, &rem);

					seq_printf(psSeqFile, "GPU Utilisation: %u%%\n", (IMG_UINT32)util);
				}
				else
				{
					seq_printf(psSeqFile, "GPU Utilisation: -\n");
				}
			}
		}
#endif
	}

	return 0;
}

static IMG_INT DebugStatusSet(const char __user *pcBuffer,
							  size_t uiCount,
							  loff_t uiPosition,
							  void *pvData)
{
	IMG_CHAR acDataBuffer[6];

	if (uiPosition != 0)
	{
		return -EIO;
	}

	if (uiCount > (sizeof(acDataBuffer) / sizeof(acDataBuffer[0])))
	{
		return -EINVAL;
	}

	if (pvr_copy_from_user(acDataBuffer, pcBuffer, uiCount))
	{
		return -EINVAL;
	}

	if (acDataBuffer[uiCount - 1] != '\n')
	{
		return -EINVAL;
	}

	if (((acDataBuffer[0] == 'k') || ((acDataBuffer[0] == 'K'))) && uiCount == 2)
	{
		PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
		psPVRSRVData->eServicesState = PVRSRV_SERVICES_STATE_BAD;
	}
	else
	{
		return -EINVAL;
	}

	return uiCount;
}

static struct seq_operations gsDebugStatusReadOps =
{
	.start = _DebugStatusSeqStart,
	.stop = _DebugStatusSeqStop,
	.next = _DebugStatusSeqNext,
	.show = _DebugStatusSeqShow,
};

/*************************************************************************/ /*!
 Dump Debug DebugFS entry
*/ /**************************************************************************/

static void *_DebugDumpDebugCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode, va_list va)
{
	loff_t *puiCurrentPosition = va_arg(va, loff_t *);
	loff_t uiPosition = va_arg(va, loff_t);
	loff_t uiCurrentPosition = *puiCurrentPosition;

	(*puiCurrentPosition)++;

	return (uiCurrentPosition == uiPosition) ? psDevNode : NULL;
}

static void *_DebugDumpDebugSeqStart(struct seq_file *psSeqFile, loff_t *puiPosition)
{
	PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA *)psSeqFile->private;
	loff_t uiCurrentPosition = 1;

	if (*puiPosition == 0)
	{
		return SEQ_START_TOKEN;
	}

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugDumpDebugCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *puiPosition);
}

static void _DebugDumpDebugSeqStop(struct seq_file *psSeqFile, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_DebugDumpDebugSeqNext(struct seq_file *psSeqFile,
									void *pvData,
									loff_t *puiPosition)
{
	PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA *)psSeqFile->private;
	loff_t uiCurrentPosition = 1;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*puiPosition)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugDumpDebugCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *puiPosition);
}

static void _DumpDebugSeqPrintf(void *pvDumpDebugFile,
				const IMG_CHAR *pszFormat, ...)
{
	struct seq_file *psSeqFile = (struct seq_file *)pvDumpDebugFile;
	IMG_CHAR  szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	va_list  ArgList;

	va_start(ArgList, pszFormat);
	vsnprintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN, pszFormat, ArgList);
	va_end(ArgList);
	seq_printf(psSeqFile, "%s\n", szBuffer);
}

static int _DebugDumpDebugSeqShow(struct seq_file *psSeqFile, void *pvData)
{
	if (pvData != NULL  &&  pvData != SEQ_START_TOKEN)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;

		if (psDeviceNode->pvDevice != NULL)
		{
			PVRSRVDebugRequest(psDeviceNode, DEBUG_REQUEST_VERBOSITY_MAX,
						_DumpDebugSeqPrintf, psSeqFile);
		}
	}

	return 0;
}

static struct seq_operations gsDumpDebugReadOps =
{
	.start = _DebugDumpDebugSeqStart,
	.stop  = _DebugDumpDebugSeqStop,
	.next  = _DebugDumpDebugSeqNext,
	.show  = _DebugDumpDebugSeqShow,
};
/*************************************************************************/ /*!
 Firmware Trace DebugFS entry
*/ /**************************************************************************/
#if !defined(PVRSRV_GPUVIRT_GUESTDRV)
static void *_DebugFWTraceCompare_AnyVaCb(PVRSRV_DEVICE_NODE *psDevNode, va_list va)
{
	loff_t *puiCurrentPosition = va_arg(va, loff_t *);
	loff_t uiPosition = va_arg(va, loff_t);
	loff_t uiCurrentPosition = *puiCurrentPosition;

	(*puiCurrentPosition)++;

	return (uiCurrentPosition == uiPosition) ? psDevNode : NULL;
}

static void *_DebugFWTraceSeqStart(struct seq_file *psSeqFile, loff_t *puiPosition)
{
	PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA *)psSeqFile->private;
	loff_t uiCurrentPosition = 1;

	if (*puiPosition == 0)
	{
		return SEQ_START_TOKEN;
	}

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugFWTraceCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *puiPosition);
}

static void _DebugFWTraceSeqStop(struct seq_file *psSeqFile, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_DebugFWTraceSeqNext(struct seq_file *psSeqFile,
								  void *pvData,
								  loff_t *puiPosition)
{
	PVRSRV_DATA *psPVRSRVData = (PVRSRV_DATA *)psSeqFile->private;
	loff_t uiCurrentPosition = 1;

	PVR_UNREFERENCED_PARAMETER(pvData);

	(*puiPosition)++;

	return List_PVRSRV_DEVICE_NODE_Any_va(psPVRSRVData->psDeviceNodeList,
										  _DebugFWTraceCompare_AnyVaCb,
										  &uiCurrentPosition,
										  *puiPosition);
}

static void _FWTraceSeqPrintf(void *pvDumpDebugFile,
				const IMG_CHAR *pszFormat, ...)
{
	struct seq_file *psSeqFile = (struct seq_file *)pvDumpDebugFile;
	IMG_CHAR  szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	va_list  ArgList;

	va_start(ArgList, pszFormat);
	vsnprintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN, pszFormat, ArgList);
	va_end(ArgList);
	seq_printf(psSeqFile, "%s\n", szBuffer);
}

static int _DebugFWTraceSeqShow(struct seq_file *psSeqFile, void *pvData)
{
	if (pvData != NULL  &&  pvData != SEQ_START_TOKEN)
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;

		if (psDeviceNode->pvDevice != NULL)
		{
			PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

			RGXDumpFirmwareTrace(_FWTraceSeqPrintf, psSeqFile, psDevInfo);
		}
	}

	return 0;
}

static struct seq_operations gsFWTraceReadOps =
{
	.start = _DebugFWTraceSeqStart,
	.stop  = _DebugFWTraceSeqStop,
	.next  = _DebugFWTraceSeqNext,
	.show  = _DebugFWTraceSeqShow,
};
#endif
/*************************************************************************/ /*!
 Debug level DebugFS entry
*/ /**************************************************************************/

#if defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON)
static void *DebugLevelSeqStart(struct seq_file *psSeqFile, loff_t *puiPosition)
{
	if (*puiPosition == 0)
	{
		return psSeqFile->private;
	}

	return NULL;
}

static void DebugLevelSeqStop(struct seq_file *psSeqFile, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *DebugLevelSeqNext(struct seq_file *psSeqFile,
							   void *pvData,
							   loff_t *puiPosition)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);
	PVR_UNREFERENCED_PARAMETER(puiPosition);

	return NULL;
}

static int DebugLevelSeqShow(struct seq_file *psSeqFile, void *pvData)
{
	if (pvData != NULL)
	{
		IMG_UINT32 uiDebugLevel = *((IMG_UINT32 *)pvData);

		seq_printf(psSeqFile, "%u\n", uiDebugLevel);

		return 0;
	}

	return -EINVAL;
}

static struct seq_operations gsDebugLevelReadOps =
{
	.start = DebugLevelSeqStart,
	.stop = DebugLevelSeqStop,
	.next = DebugLevelSeqNext,
	.show = DebugLevelSeqShow,
};


static IMG_INT DebugLevelSet(const char __user *pcBuffer,
							 size_t uiCount,
							 loff_t uiPosition,
							 void *pvData)
{
	IMG_UINT32 *uiDebugLevel = (IMG_UINT32 *)pvData;
	IMG_CHAR acDataBuffer[6];

	if (uiPosition != 0)
	{
		return -EIO;
	}

	if (uiCount > (sizeof(acDataBuffer) / sizeof(acDataBuffer[0])))
	{
		return -EINVAL;
	}

	if (pvr_copy_from_user(acDataBuffer, pcBuffer, uiCount))
	{
		return -EINVAL;
	}

	if (acDataBuffer[uiCount - 1] != '\n')
	{
		return -EINVAL;
	}

	if (sscanf(acDataBuffer, "%u", &gPVRDebugLevel) == 0)
	{
		return -EINVAL;
	}

	/* As this is Linux the next line uses a GCC builtin function */
	(*uiDebugLevel) &= (1 << __builtin_ffsl(DBGPRIV_LAST)) - 1;

	return uiCount;
}
#endif /* defined(DEBUG) */

static PVR_DEBUGFS_ENTRY_DATA *gpsVersionDebugFSEntry;

static PVR_DEBUGFS_ENTRY_DATA *gpsStatusDebugFSEntry;
static PVR_DEBUGFS_ENTRY_DATA *gpsDumpDebugDebugFSEntry;

static PVR_DEBUGFS_ENTRY_DATA *gpsFWTraceDebugFSEntry;

#if defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON)
static PVR_DEBUGFS_ENTRY_DATA *gpsDebugLevelDebugFSEntry;
#endif

int PVRDebugCreateDebugFSEntries(void)
{
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	int iResult;

	PVR_ASSERT(psPVRSRVData != NULL);

	/*
	 * The DebugFS entries are designed to work in a single device system but
	 * this function will be called multiple times in a multi-device system.
	 * Return an error in this case.
	 */
	if (gpsVersionDebugFSEntry)
	{
		return -EEXIST;
	}

#if !defined(NO_HARDWARE)
	if (RGXRegisterGpuUtilStats(&ghGpuUtilUserDebugFS) != PVRSRV_OK)
	{
		return -ENOMEM;
	}
#endif

	iResult = PVRDebugFSCreateEntry("version",
									NULL,
									&gsDebugVersionReadOps,
									NULL,
									NULL,
									NULL,
									psPVRSRVData,
									&gpsVersionDebugFSEntry);
	if (iResult != 0)
	{
		return iResult;
	}

	iResult = PVRDebugFSCreateEntry("status",
									NULL,
									&gsDebugStatusReadOps,
									(PVRSRV_ENTRY_WRITE_FUNC *)DebugStatusSet,
									NULL,
									NULL,
									psPVRSRVData,
									&gpsStatusDebugFSEntry);
	if (iResult != 0)
	{
		goto ErrorRemoveVersionEntry;
	}

	iResult = PVRDebugFSCreateEntry("debug_dump",
									NULL,
									&gsDumpDebugReadOps,
									NULL,
									NULL,
									NULL,
									psPVRSRVData,
									&gpsDumpDebugDebugFSEntry);
	if (iResult != 0)
	{
		goto ErrorRemoveStatusEntry;
	}
#if !defined(PVRSRV_GPUVIRT_GUESTDRV)
	iResult = PVRDebugFSCreateEntry("firmware_trace",
									NULL,
									&gsFWTraceReadOps,
									NULL,
									NULL,
									NULL,
									psPVRSRVData,
									&gpsFWTraceDebugFSEntry);
	if (iResult != 0)
	{
		goto ErrorRemoveDumpDebugEntry;
	}
#endif
#if defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON)
	iResult = PVRDebugFSCreateEntry("debug_level",
									NULL,
									&gsDebugLevelReadOps,
									(PVRSRV_ENTRY_WRITE_FUNC *)DebugLevelSet,
									NULL,
									NULL,
									&gPVRDebugLevel,
									&gpsDebugLevelDebugFSEntry);
	if (iResult != 0)
	{
		goto ErrorRemoveFWTraceLogEntry;
	}
#endif

	return 0;

#if defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON)
ErrorRemoveFWTraceLogEntry:
	PVRDebugFSRemoveEntry(&gpsFWTraceDebugFSEntry);
#endif
#if !defined(PVRSRV_GPUVIRT_GUESTDRV)
ErrorRemoveDumpDebugEntry:
	PVRDebugFSRemoveEntry(&gpsDumpDebugDebugFSEntry);
#endif
ErrorRemoveStatusEntry:
	PVRDebugFSRemoveEntry(&gpsStatusDebugFSEntry);

ErrorRemoveVersionEntry:
	PVRDebugFSRemoveEntry(&gpsVersionDebugFSEntry);

	return iResult;
}

void PVRDebugRemoveDebugFSEntries(void)
{
#if !defined(NO_HARDWARE)
	if (ghGpuUtilUserDebugFS != NULL)
	{
		RGXUnregisterGpuUtilStats(ghGpuUtilUserDebugFS);
		ghGpuUtilUserDebugFS = NULL;
	}
#endif

#if defined(DEBUG) || defined(PVR_DPF_ADHOC_DEBUG_ON)
	if (gpsDebugLevelDebugFSEntry != NULL)
	{
		PVRDebugFSRemoveEntry(&gpsDebugLevelDebugFSEntry);
	}
#endif

	if (gpsFWTraceDebugFSEntry != NULL)
	{
		PVRDebugFSRemoveEntry(&gpsFWTraceDebugFSEntry);
	}

	if (gpsDumpDebugDebugFSEntry != NULL)
	{
		PVRDebugFSRemoveEntry(&gpsDumpDebugDebugFSEntry);
	}

	if (gpsStatusDebugFSEntry != NULL)
	{
		PVRDebugFSRemoveEntry(&gpsStatusDebugFSEntry);
	}

	if (gpsVersionDebugFSEntry != NULL)
	{
		PVRDebugFSRemoveEntry(&gpsVersionDebugFSEntry);
	}
}

