/*************************************************************************/ /*!
@Title          Command Queue API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Internal structures and definitions for command queues
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

#ifndef QUEUE_H
#define QUEUE_H


#if defined(__cplusplus)
extern "C" {
#endif

/*!
 * Macro to Read Offset in given command queue
 */
#define UPDATE_QUEUE_ROFF(psQueue, uSize)						\
	(psQueue)->uReadOffset = ((psQueue)->uReadOffset + (uSize))	\
	& ((psQueue)->uQueueSize - 1);

/*!
	generic cmd complete structure.
	This structure represents the storage required between starting and finishing
	a given cmd and is required to hold the generic sync object update data.
	note: for any given system we know what command types we support and
	therefore how much storage is required for any number of commands in progress
 */
 typedef struct _COMMAND_COMPLETE_DATA_
 {
	IMG_BOOL			bInUse;
	/* <arg(s) to PVRSRVProcessQueues>;	*/	/*!< TBD */
	IMG_UINT32			ui32DstSyncCount;	/*!< number of dst sync objects */
	IMG_UINT32			ui32SrcSyncCount;	/*!< number of src sync objects */
	PVRSRV_SYNC_OBJECT	*psDstSync;			/*!< dst sync ptr list, 
                                        	allocated on back of this structure */
	PVRSRV_SYNC_OBJECT	*psSrcSync;			/*!< src sync ptr list, 
                                       		allocated on back of this structure */
	IMG_UINT32			ui32AllocSize;		/*!< allocated size*/
	PFN_QUEUE_COMMAND_COMPLETE	pfnCommandComplete;	/*!< Command complete callback */
	IMG_HANDLE					hCallbackData;		/*!< Command complete callback data */
 }COMMAND_COMPLETE_DATA, *PCOMMAND_COMPLETE_DATA;

#if !defined(USE_CODE)
IMG_VOID QueueDumpDebugInfo(IMG_VOID);

IMG_IMPORT
PVRSRV_ERROR PVRSRVProcessQueues (IMG_BOOL		bFlush);

#if defined(__linux__) && defined(__KERNEL__) 
#include <linux/types.h>
#include <linux/seq_file.h>
void* ProcSeqOff2ElementQueue(struct seq_file * sfile, loff_t off);
void ProcSeqShowQueue(struct seq_file *sfile,void* el);
#endif


IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateCommandQueueKM(IMG_SIZE_T uQueueSize,
													 PVRSRV_QUEUE_INFO **ppsQueueInfo);
IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyCommandQueueKM(PVRSRV_QUEUE_INFO *psQueueInfo);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVInsertCommandKM(PVRSRV_QUEUE_INFO	*psQueue,
												PVRSRV_COMMAND		**ppsCommand,
												IMG_UINT32			ui32DevIndex,
												IMG_UINT16			CommandType,
												IMG_UINT32			ui32DstSyncCount,
												PVRSRV_KERNEL_SYNC_INFO	*apsDstSync[],
												IMG_UINT32			ui32SrcSyncCount,
												PVRSRV_KERNEL_SYNC_INFO	*apsSrcSync[],
												IMG_SIZE_T			ui32DataByteSize,
												PFN_QUEUE_COMMAND_COMPLETE pfnCommandComplete,
												IMG_HANDLE			hCallbackData);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetQueueSpaceKM(PVRSRV_QUEUE_INFO *psQueue,
												IMG_SIZE_T uParamSize,
												IMG_VOID **ppvSpace);

IMG_IMPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSubmitCommandKM(PVRSRV_QUEUE_INFO *psQueue,
												PVRSRV_COMMAND *psCommand);

IMG_IMPORT
IMG_VOID PVRSRVCommandCompleteKM(IMG_HANDLE hCmdCookie, IMG_BOOL bScheduleMISR);

IMG_IMPORT
PVRSRV_ERROR PVRSRVRegisterCmdProcListKM(IMG_UINT32		ui32DevIndex,
										 PFN_CMD_PROC	*ppfnCmdProcList,
										 IMG_UINT32		ui32MaxSyncsPerCmd[][2],
										 IMG_UINT32		ui32CmdCount);
IMG_IMPORT
PVRSRV_ERROR PVRSRVRemoveCmdProcListKM(IMG_UINT32	ui32DevIndex,
									   IMG_UINT32	ui32CmdCount);

#endif /* !defined(USE_CODE) */


#if defined (__cplusplus)
}
#endif

#endif /* QUEUE_H */

/******************************************************************************
 End of file (queue.h)
******************************************************************************/
