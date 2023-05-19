/*************************************************************************/ /*!
@File
@Title          pdump functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Main APIs for pdump functions
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

#ifndef PDUMP_KM_H
#define PDUMP_KM_H

#if defined(PDUMP)
 #if defined(__linux__)
  #include <linux/version.h>
  #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
   #include <linux/stdarg.h>
  #else
   #include <stdarg.h>
  #endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) */
 #else
  #include <stdarg.h>
 #endif /* __linux__ */
#endif /* PDUMP */

/* services/srvkm/include/ */
#include "device.h"

/* include/ */
#include "pvrsrv_error.h"


#if defined(__KERNEL__) && defined(__linux__) && !defined(__GENKSYMS__)
#define __pvrsrv_defined_struct_enum__
#include <services_kernel_client.h>
#endif

#include "connection_server.h"
/* Pull in pdump flags from services include */
#include "pdump.h"
#include "pdumpdefs.h"

/* Define this to enable the PDUMP_HERE trace in the server */
#undef PDUMP_TRACE

#if defined(PDUMP_TRACE)
#define PDUMP_HERE_VAR  __maybe_unused IMG_UINT32 here = 0;
#define PDUMP_HERE(a)	{ here = (a); if (ui32Flags & PDUMP_FLAGS_DEBUG) PVR_DPF((PVR_DBG_WARNING, "HERE %d", (a))); }
#define PDUMP_HEREA(a)	{ here = (a); PVR_DPF((PVR_DBG_WARNING, "HERE ALWAYS %d", (a))); }
#else
#define PDUMP_HERE_VAR  __maybe_unused IMG_UINT32 here = 0;
#define PDUMP_HERE(a)	here = (a);
#define PDUMP_HEREA(a)	here = (a);
#endif

#define PDUMP_PD_UNIQUETAG	(IMG_HANDLE)0
#define PDUMP_PT_UNIQUETAG	(IMG_HANDLE)0

/* Invalid value for PDump block number */
#define PDUMP_BLOCKNUM_INVALID      IMG_UINT32_MAX

typedef struct _PDUMP_CONNECTION_DATA_ PDUMP_CONNECTION_DATA;

/* PDump transition events */
typedef enum _PDUMP_TRANSITION_EVENT_
{
	PDUMP_TRANSITION_EVENT_NONE,              /* No event */
	PDUMP_TRANSITION_EVENT_BLOCK_FINISHED,    /* Block mode event, current PDump-block has finished */
	PDUMP_TRANSITION_EVENT_BLOCK_STARTED,     /* Block mode event, new PDump-block has started */
	PDUMP_TRANSITION_EVENT_RANGE_ENTERED,     /* Transition into capture range */
	PDUMP_TRANSITION_EVENT_RANGE_EXITED,      /* Transition out of capture range */
} PDUMP_TRANSITION_EVENT;

typedef PVRSRV_ERROR (*PFN_PDUMP_TRANSITION)(void *pvData, void *pvDevice, PDUMP_TRANSITION_EVENT eEvent, IMG_UINT32 ui32PDumpFlags);
typedef void (*PFN_PDUMP_SYNCBLOCKS)(PVRSRV_DEVICE_NODE *psDevNode, void *pvData, PDUMP_TRANSITION_EVENT eEvent);

typedef PVRSRV_ERROR (*PFN_PDUMP_TRANSITION_FENCE_SYNC)(void *pvData, PDUMP_TRANSITION_EVENT eEvent);

#ifdef PDUMP

/*! Macro used to record a panic in the PDump script stream */
#define PDUMP_PANIC(_dev, _id, _msg) do \
		{ PVRSRV_ERROR _eE;\
			_eE = PDumpPanic((_dev), ((RGX_PDUMP_PANIC_ ## _id) & 0xFFFF), _msg, __func__, __LINE__);	\
			PVR_LOG_IF_ERROR(_eE, "PDumpPanic");\
		MSC_SUPPRESS_4127\
		} while (0)

/*! Macro used to record a driver error in the PDump script stream to invalidate the capture */
#define PDUMP_ERROR(_dev, _err, _msg) \
	(void)PDumpCaptureError((_dev), _err, _msg, __func__, __LINE__)

#define SZ_MSG_SIZE_MAX			PVRSRV_PDUMP_MAX_COMMENT_SIZE
#define SZ_SCRIPT_SIZE_MAX		PVRSRV_PDUMP_MAX_COMMENT_SIZE
#define SZ_FILENAME_SIZE_MAX	(PVRSRV_PDUMP_MAX_FILENAME_SIZE+sizeof(PDUMP_PARAM_N_FILE_NAME))

#define PDUMP_GET_SCRIPT_STRING()																			\
	IMG_HANDLE hScript;																						\
	void *pvScriptAlloc;																					\
	IMG_UINT32 ui32MaxLen = SZ_SCRIPT_SIZE_MAX-1;															\
	pvScriptAlloc = OSAllocMem( SZ_SCRIPT_SIZE_MAX );														\
	if (!pvScriptAlloc)																						\
	{																										\
		PVR_DPF((PVR_DBG_ERROR, "PDUMP_GET_SCRIPT_STRING() failed to allocate memory for script buffer"));	\
		return PVRSRV_ERROR_OUT_OF_MEMORY;																	\
	}																										\
																											\
	hScript = (IMG_HANDLE) pvScriptAlloc;

#define PDUMP_GET_MSG_STRING()																				\
	IMG_CHAR *pszMsg;																						\
	void *pvMsgAlloc;																						\
	IMG_UINT32 ui32MaxLen = SZ_MSG_SIZE_MAX-1;																\
	pvMsgAlloc = OSAllocMem( SZ_MSG_SIZE_MAX );																\
	if (!pvMsgAlloc)																						\
	{																										\
		PVR_DPF((PVR_DBG_ERROR, "PDUMP_GET_MSG_STRING() failed to allocate memory for message buffer"));	\
		return PVRSRV_ERROR_OUT_OF_MEMORY;																	\
	}																										\
	pszMsg = (IMG_CHAR *)pvMsgAlloc;

#define PDUMP_GET_SCRIPT_AND_FILE_STRING()																	\
	IMG_HANDLE hScript;																						\
	IMG_CHAR *pszFileName;																					\
	IMG_UINT32 ui32MaxLenScript = SZ_SCRIPT_SIZE_MAX-1;														\
	void *pvScriptAlloc;																					\
	void *pvFileAlloc;																						\
	pvScriptAlloc = OSAllocMem( SZ_SCRIPT_SIZE_MAX );														\
	if (!pvScriptAlloc)																						\
	{																										\
		PVR_DPF((PVR_DBG_ERROR, "PDUMP_GET_SCRIPT_AND_FILE_STRING() failed to allocate memory for script buffer"));		\
		return PVRSRV_ERROR_OUT_OF_MEMORY;																	\
	}																										\
																											\
	hScript = (IMG_HANDLE) pvScriptAlloc;																	\
	pvFileAlloc = OSAllocMem( SZ_FILENAME_SIZE_MAX );														\
	if (!pvFileAlloc)																						\
	{																										\
		PVR_DPF((PVR_DBG_ERROR, "PDUMP_GET_SCRIPT_AND_FILE_STRING() failed to allocate memory for filename buffer"));	\
		OSFreeMem(pvScriptAlloc);																			\
		return PVRSRV_ERROR_OUT_OF_MEMORY;																	\
	}																										\
	pszFileName = (IMG_CHAR *)pvFileAlloc;

#define PDUMP_RELEASE_SCRIPT_STRING()																		\
	if (pvScriptAlloc)																						\
	{																										\
		OSFreeMem(pvScriptAlloc);																			\
		pvScriptAlloc = NULL;																				\
	}

#define PDUMP_RELEASE_MSG_STRING()																			\
	if (pvMsgAlloc)																							\
	{																										\
		OSFreeMem(pvMsgAlloc);																				\
		pvMsgAlloc = NULL;																					\
	}

#define PDUMP_RELEASE_FILE_STRING()																			\
	if (pvFileAlloc)																						\
	{																										\
		OSFreeMem(pvFileAlloc);																				\
		pvFileAlloc = NULL;																					\
	}

#define PDUMP_RELEASE_SCRIPT_AND_FILE_STRING()																\
	if (pvScriptAlloc)																						\
	{																										\
		OSFreeMem(pvScriptAlloc);																			\
		pvScriptAlloc = NULL;																				\
	}																										\
	if (pvFileAlloc)																						\
	{																										\
		OSFreeMem(pvFileAlloc);																				\
		pvFileAlloc = NULL;																					\
	}


/* Shared across pdump_x files */
PVRSRV_ERROR PDumpInitCommon(void);
void PDumpDeInitCommon(void);
PVRSRV_ERROR PDumpValidateUMFlags(PDUMP_FLAGS_T uiFlags);
PVRSRV_ERROR PDumpReady(void);
void PDumpGetParameterZeroPageInfo(PDUMP_FILEOFFSET_T *puiZeroPageOffset,
                                   size_t *puiZeroPageSize,
                                   const IMG_CHAR **ppszZeroPageFilename);

void PDumpConnectionNotify(PVRSRV_DEVICE_NODE *psDeviceNode);
void PDumpDisconnectionNotify(PVRSRV_DEVICE_NODE *psDeviceNode);

void PDumpStopInitPhase(PVRSRV_DEVICE_NODE *psDeviceNode);
PVRSRV_ERROR PDumpSetFrameKM(CONNECTION_DATA *psConnection,
                             PVRSRV_DEVICE_NODE *psDeviceNode,
                             IMG_UINT32 ui32Frame);
PVRSRV_ERROR PDumpGetFrameKM(CONNECTION_DATA *psConnection,
                             PVRSRV_DEVICE_NODE * psDeviceNode,
                             IMG_UINT32* pui32Frame);
PVRSRV_ERROR PDumpCommentKM(CONNECTION_DATA *psConnection,
                            PVRSRV_DEVICE_NODE *psDeviceNode,
                            IMG_UINT32 ui32CommentSize,
                            IMG_CHAR *pszComment,
                            IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpSetDefaultCaptureParamsKM(CONNECTION_DATA *psConnection,
                                            PVRSRV_DEVICE_NODE *psDeviceNode,
                                            IMG_UINT32 ui32Mode,
                                            IMG_UINT32 ui32Start,
                                            IMG_UINT32 ui32End,
                                            IMG_UINT32 ui32Interval,
                                            IMG_UINT32 ui32MaxParamFileSize,
                                            IMG_UINT32 ui32AutoTermTimeout);


PVRSRV_ERROR PDumpReg32(PVRSRV_DEVICE_NODE *psDeviceNode,
                        IMG_CHAR *pszPDumpRegName,
                        IMG_UINT32 ui32RegAddr,
                        IMG_UINT32 ui32RegValue,
                        IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpReg64(PVRSRV_DEVICE_NODE *psDeviceNode,
                        IMG_CHAR *pszPDumpRegName,
                        IMG_UINT32 ui32RegAddr,
                        IMG_UINT64 ui64RegValue,
                        IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpRegLabelToReg64(PVRSRV_DEVICE_NODE *psDeviceNode,
                                  IMG_CHAR *pszPDumpRegName,
                                  IMG_UINT32 ui32RegDst,
                                  IMG_UINT32 ui32RegSrc,
                                  IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpPhysHandleToInternalVar64(PVRSRV_DEVICE_NODE *psDeviceNode,
                                            IMG_CHAR *pszInternalVar,
                                            IMG_HANDLE hPdumpPages,
                                            IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpMemLabelToInternalVar64(IMG_CHAR *pszInternalVar,
                                          PMR *psPMR,
                                          IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                                          IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpInternalVarToMemLabel(PMR *psPMR,
                                        IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                                        IMG_CHAR *pszInternalVar,
                                        IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpWriteVarORValueOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                    const IMG_CHAR *pszInternalVariable,
                                    const IMG_UINT64 ui64Value,
                                    const IMG_UINT32 ui32PDumpFlags);

PVRSRV_ERROR PDumpWriteVarANDValueOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     const IMG_CHAR *pszInternalVariable,
                                     const IMG_UINT64 ui64Value,
                                     const IMG_UINT32 ui32PDumpFlags);

PVRSRV_ERROR PDumpWriteVarSHRValueOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     const IMG_CHAR *pszInternalVariable,
                                     const IMG_UINT64 ui64Value,
                                     const IMG_UINT32 ui32PDumpFlags);

PVRSRV_ERROR PDumpWriteVarORVarOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                  const IMG_CHAR *pszInternalVar,
                                  const IMG_CHAR *pszInternalVar2,
                                  const IMG_UINT32 ui32PDumpFlags);

PVRSRV_ERROR PDumpWriteVarANDVarOp(PVRSRV_DEVICE_NODE *psDeviceNode,
                                   const IMG_CHAR *pszInternalVar,
                                   const IMG_CHAR *pszInternalVar2,
                                   const IMG_UINT32 ui32PDumpFlags);

PVRSRV_ERROR PDumpInternalVarToReg32(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_CHAR *pszPDumpRegName,
                                     IMG_UINT32 ui32Reg,
                                     IMG_CHAR *pszInternalVar,
                                     IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpInternalVarToReg64(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_CHAR *pszPDumpRegName,
                                     IMG_UINT32 ui32Reg,
                                     IMG_CHAR *pszInternalVar,
                                     IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpMemLabelToMem32(PMR *psPMRSource,
                                  PMR *psPMRDest,
                                  IMG_DEVMEM_OFFSET_T uiLogicalOffsetSource,
                                  IMG_DEVMEM_OFFSET_T uiLogicalOffsetDest,
                                  IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpMemLabelToMem64(PMR *psPMRSource,
                                  PMR *psPMRDest,
                                  IMG_DEVMEM_OFFSET_T uiLogicalOffsetSource,
                                  IMG_DEVMEM_OFFSET_T uiLogicalOffsetDest,
                                  IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpRegLabelToMem32(IMG_CHAR *pszPDumpRegName,
                                  IMG_UINT32 ui32Reg,
                                  PMR *psPMR,
                                  IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                                  IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpRegLabelToMem64(IMG_CHAR *pszPDumpRegName,
                                  IMG_UINT32 ui32Reg,
                                  PMR *psPMR,
                                  IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                                  IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpRegLabelToInternalVar(PVRSRV_DEVICE_NODE *psDeviceNode,
                                        IMG_CHAR *pszPDumpRegName,
                                        IMG_UINT32 ui32Reg,
                                        IMG_CHAR *pszInternalVar,
                                        IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpSAW(PVRSRV_DEVICE_NODE *psDeviceNode,
                      IMG_CHAR      *pszDevSpaceName,
                      IMG_UINT32    ui32HPOffsetBytes,
                      IMG_UINT32    ui32NumSaveBytes,
                      IMG_CHAR      *pszOutfileName,
                      IMG_UINT32    ui32OutfileOffsetByte,
                      PDUMP_FLAGS_T uiPDumpFlags);

PVRSRV_ERROR PDumpRegPolKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                           IMG_CHAR            *pszPDumpRegName,
                           IMG_UINT32          ui32RegAddr,
                           IMG_UINT32          ui32RegValue,
                           IMG_UINT32          ui32Mask,
                           IMG_UINT32          ui32Flags,
                           PDUMP_POLL_OPERATOR eOperator);


/**************************************************************************/ /*!
@Function       PDumpImageDescriptor
@Description    PDumps image data out as an IMGBv2 data section
@Input          psDeviceNode         Pointer to device node.
@Input          ui32MMUContextID     PDUMP MMU context ID.
@Input          pszSABFileName       Pointer to string containing file name of
                                     Image being SABed
@Input          sData                GPU virtual address of this surface.
@Input          ui32DataSize         Image data size
@Input          ui32LogicalWidth     Image logical width
@Input          ui32LogicalHeight    Image logical height
@Input          ui32PhysicalWidth    Image physical width
@Input          ui32PhysicalHeight   Image physical height
@Input          ePixFmt              Image pixel format
@Input          eFBCompression       FB compression mode
@Input          paui32FBCClearColour FB clear colour (Only applicable to FBC surfaces)
@Input          eFBCSwizzle          FBC channel swizzle (Only applicable to FBC surfaces)
@Input          sHeader              GPU virtual address of the headers of this
                                     surface (Only applicable to FBC surfaces)
@Input          ui32HeaderSize       Header size (Only applicable to FBC surfaces)
@Input          ui32PDumpFlags       PDUMP flags
@Return         PVRSRV_ERROR:        PVRSRV_OK on success. Otherwise, a PVRSRV_
                                     error code
*/ /***************************************************************************/
PVRSRV_ERROR PDumpImageDescriptor(PVRSRV_DEVICE_NODE *psDeviceNode,
                                  IMG_UINT32 ui32MMUContextID,
                                  IMG_CHAR *pszSABFileName,
                                  IMG_DEV_VIRTADDR sData,
                                  IMG_UINT32 ui32DataSize,
                                  IMG_UINT32 ui32LogicalWidth,
                                  IMG_UINT32 ui32LogicalHeight,
                                  IMG_UINT32 ui32PhysicalWidth,
                                  IMG_UINT32 ui32PhysicalHeight,
                                  PDUMP_PIXEL_FORMAT ePixFmt,
                                  IMG_MEMLAYOUT eMemLayout,
                                  IMG_FB_COMPRESSION eFBCompression,
                                  const IMG_UINT32 *paui32FBCClearColour,
                                  PDUMP_FBC_SWIZZLE eFBCSwizzle,
                                  IMG_DEV_VIRTADDR sHeader,
                                  IMG_UINT32 ui32HeaderSize,
                                  IMG_UINT32 ui32PDumpFlags);

/**************************************************************************/ /*!
@Function       PDumpDataDescriptor
@Description    PDumps non-image data out as an IMGCv1 data section
@Input          psDeviceNode         Pointer to device node.
@Input          ui32MMUContextID     PDUMP MMU context ID.
@Input          pszSABFileName       Pointer to string containing file name of
                                     Data being SABed
@Input          sData                GPU virtual address of this data.
@Input          ui32DataSize         Data size
@Input          ui32HeaderType       Header type
@Input          ui32ElementType      Data element type
@Input          ui32ElementCount     Number of data elements
@Input          ui32PDumpFlags       PDUMP flags
@Return         PVRSRV_ERROR:        PVRSRV_OK on success. Otherwise, a PVRSRV_
                                     error code
*/ /***************************************************************************/
PVRSRV_ERROR PDumpDataDescriptor(PVRSRV_DEVICE_NODE *psDeviceNode,
                                 IMG_UINT32 ui32MMUContextID,
                                 IMG_CHAR *pszSABFileName,
                                 IMG_DEV_VIRTADDR sData,
                                 IMG_UINT32 ui32DataSize,
                                 IMG_UINT32 ui32HeaderType,
                                 IMG_UINT32 ui32ElementType,
                                 IMG_UINT32 ui32ElementCount,
                                 IMG_UINT32 ui32PDumpFlags);


PVRSRV_ERROR PDumpReadRegKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                            IMG_CHAR *pszPDumpRegName,
                            IMG_CHAR *pszFileName,
                            IMG_UINT32 ui32FileOffset,
                            IMG_UINT32 ui32Address,
                            IMG_UINT32 ui32Size,
                            IMG_UINT32 ui32PDumpFlags);

__printf(3, 4)
PVRSRV_ERROR PDumpCommentWithFlags(PVRSRV_DEVICE_NODE *psDeviceNode,
                                   IMG_UINT32 ui32Flags,
                                   IMG_CHAR* pszFormat,
                                   ...);

PVRSRV_ERROR PDumpCommentWithFlagsVA(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_UINT32 ui32Flags,
                                     const IMG_CHAR * pszFormat,
                                     va_list args);

PVRSRV_ERROR PDumpPanic(PVRSRV_DEVICE_NODE *psDeviceNode,
                        IMG_UINT32      ui32PanicNo,
                        IMG_CHAR*       pszPanicMsg,
                        const IMG_CHAR* pszPPFunc,
                        IMG_UINT32      ui32PPline);

PVRSRV_ERROR PDumpCaptureError(PVRSRV_DEVICE_NODE *psDeviceNode,
                               PVRSRV_ERROR    ui32ErrorNo,
                               IMG_CHAR*       pszErrorMsg,
                               const IMG_CHAR* pszPPFunc,
                               IMG_UINT32      ui32PPline);

PVRSRV_ERROR PDumpIsLastCaptureFrameKM(IMG_BOOL *pbIsLastCaptureFrame);

PVRSRV_ERROR PDumpGetStateKM(IMG_UINT64 *ui64State);

PVRSRV_ERROR PDumpForceCaptureStopKM(CONNECTION_DATA *psConnection,
                                     PVRSRV_DEVICE_NODE *psDeviceNode);

PVRSRV_ERROR PDumpRegRead32ToInternalVar(PVRSRV_DEVICE_NODE *psDeviceNode,
                                         IMG_CHAR *pszPDumpRegName,
                                         IMG_UINT32 ui32RegOffset,
                                         IMG_CHAR *pszInternalVar,
                                         IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpRegRead32(PVRSRV_DEVICE_NODE *psDeviceNode,
                            IMG_CHAR *pszPDumpRegName,
                            const IMG_UINT32 dwRegOffset,
                            IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpRegRead64(PVRSRV_DEVICE_NODE *psDeviceNode,
                            IMG_CHAR *pszPDumpRegName,
                            const IMG_UINT32 dwRegOffset,
                            IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpRegRead64ToInternalVar(PVRSRV_DEVICE_NODE *psDeviceNode,
                                         IMG_CHAR *pszPDumpRegName,
                                         IMG_CHAR *pszInternalVar,
                                         const IMG_UINT32 dwRegOffset,
                                         IMG_UINT32	ui32Flags);

PVRSRV_ERROR PDumpIDLWithFlags(PVRSRV_DEVICE_NODE *psDeviceNode,
                               IMG_UINT32 ui32Clocks,
                               IMG_UINT32 ui32Flags);
PVRSRV_ERROR PDumpIDL(PVRSRV_DEVICE_NODE *psDeviceNode,
                      IMG_UINT32 ui32Clocks);

PVRSRV_ERROR PDumpRegBasedCBP(PVRSRV_DEVICE_NODE *psDeviceNode,
                              IMG_CHAR   *pszPDumpRegName,
                              IMG_UINT32 ui32RegOffset,
                              IMG_UINT32 ui32WPosVal,
                              IMG_UINT32 ui32PacketSize,
                              IMG_UINT32 ui32BufferSize,
                              IMG_UINT32 ui32Flags);

PVRSRV_ERROR PDumpTRG(PVRSRV_DEVICE_NODE *psDeviceNode,
                      IMG_CHAR *pszMemSpace,
                      IMG_UINT32 ui32MMUCtxID,
                      IMG_UINT32 ui32RegionID,
                      IMG_BOOL bEnable,
                      IMG_UINT64 ui64VAddr,
                      IMG_UINT64 ui64LenBytes,
                      IMG_UINT32 ui32XStride,
                      IMG_UINT32 ui32Flags);

void PDumpLock(void);
void PDumpUnlock(void);

PVRSRV_ERROR PDumpRegCondStr(IMG_CHAR            **ppszPDumpCond,
                             IMG_CHAR            *pszPDumpRegName,
                             IMG_UINT32          ui32RegAddr,
                             IMG_UINT32          ui32RegValue,
                             IMG_UINT32          ui32Mask,
                             IMG_UINT32          ui32Flags,
                             PDUMP_POLL_OPERATOR eOperator);

PVRSRV_ERROR PDumpInternalValCondStr(IMG_CHAR            **ppszPDumpCond,
                                     IMG_CHAR            *pszInternalVar,
                                     IMG_UINT32          ui32RegValue,
                                     IMG_UINT32          ui32Mask,
                                     IMG_UINT32          ui32Flags,
                                     PDUMP_POLL_OPERATOR eOperator);

PVRSRV_ERROR PDumpIfKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                       IMG_CHAR *pszPDumpCond, IMG_UINT32 ui32PDumpFlags);
PVRSRV_ERROR PDumpElseKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                         IMG_CHAR *pszPDumpCond, IMG_UINT32 ui32PDumpFlags);
PVRSRV_ERROR PDumpFiKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                       IMG_CHAR *pszPDumpCond, IMG_UINT32 ui32PDumpFlags);
PVRSRV_ERROR PDumpStartDoLoopKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                                IMG_UINT32 ui32PDumpFlags);
PVRSRV_ERROR PDumpEndDoWhileLoopKM(PVRSRV_DEVICE_NODE *psDeviceNode,
                                   IMG_CHAR *pszPDumpWhileCond,
                                   IMG_UINT32 ui32PDumpFlags);
PVRSRV_ERROR PDumpCOMCommand(PVRSRV_DEVICE_NODE *psDeviceNode,
                             IMG_UINT32 ui32PDumpFlags,
                             const IMG_CHAR *pszPDump);

void PDumpPowerTransitionStart(PVRSRV_DEVICE_NODE *psDeviceNode);
void PDumpPowerTransitionEnd(PVRSRV_DEVICE_NODE *psDeviceNode);
IMG_BOOL PDumpCheckFlagsWrite(PVRSRV_DEVICE_NODE *psDeviceNode,
                              IMG_UINT32 ui32Flags);

/*!
 * @name	PDumpWriteParameter
 * @brief	General function for writing to PDump stream. Used
 *          mainly for memory dumps to parameter stream.
 *          Usually more convenient to use PDumpWriteScript below
 *          for the script stream.
 * @param	psDeviceNode - device PDump pertains to
 * @param	psui8Data - data to write
 * @param	ui32Size - size of write
 * @param	ui32Flags - PDump flags
 * @param   pui32FileOffset - on return contains the file offset to
 *                            the start of the parameter data
 * @param   aszFilenameStr - pointer to at least a 20 char buffer to
 *                           return the parameter filename
 * @return	error
 */
PVRSRV_ERROR PDumpWriteParameter(PVRSRV_DEVICE_NODE *psDeviceNode,
                                 IMG_UINT8 *psui8Data, IMG_UINT32 ui32Size,
                                 IMG_UINT32 ui32Flags, IMG_UINT32* pui32FileOffset,
                                 IMG_CHAR* aszFilenameStr);

/*!
 * @name	PDumpWriteScript
 * @brief	Write an PDumpOS created string to the "script" output stream
 * @param	psDeviceNode - device PDump pertains to
 * @param	hString - PDump OS layer handle of string buffer to write
 * @param	ui32Flags - PDump flags
 * @return	IMG_TRUE on success.
 */
IMG_BOOL PDumpWriteScript(PVRSRV_DEVICE_NODE *psDeviceNode,
                          IMG_HANDLE hString, IMG_UINT32 ui32Flags);

/**************************************************************************/ /*!
@Function       PDumpSNPrintf
@Description    Printf to OS-specific PDump state buffer. This function is
                only called if PDUMP is defined.
@Input          hBuf               handle of buffer to write into
@Input          ui32ScriptSizeMax  maximum size of data to write (chars)
@Input          pszFormat          format string
@Return         None
*/ /**************************************************************************/
__printf(3, 4)
PVRSRV_ERROR PDumpSNPrintf(IMG_HANDLE hBuf, IMG_UINT32 ui32ScriptSizeMax, IMG_CHAR* pszFormat, ...);


/*
	PDumpWriteShiftedMaskedValue():

	loads the "reference" address into an internal PDump register,
	optionally shifts it right,
	optionally shifts it left,
	optionally masks it
	then finally writes the computed value to the given destination address

	i.e. it emits pdump language equivalent to this expression:

	 dest = ((&ref) >> SHRamount << SHLamount) & MASK
*/
PVRSRV_ERROR
PDumpWriteShiftedMaskedValue(PVRSRV_DEVICE_NODE *psDeviceNode,
                             const IMG_CHAR *pszDestRegspaceName,
                             const IMG_CHAR *pszDestSymbolicName,
                             IMG_DEVMEM_OFFSET_T uiDestOffset,
                             const IMG_CHAR *pszRefRegspaceName,
                             const IMG_CHAR *pszRefSymbolicName,
                             IMG_DEVMEM_OFFSET_T uiRefOffset,
                             IMG_UINT32 uiSHRAmount,
                             IMG_UINT32 uiSHLAmount,
                             IMG_UINT32 uiMask,
                             IMG_DEVMEM_SIZE_T uiWordSize,
                             IMG_UINT32 uiPDumpFlags);

/*
	PDumpWriteSymbAddress():
	writes the address of the "reference" to the offset given
*/
PVRSRV_ERROR
PDumpWriteSymbAddress(PVRSRV_DEVICE_NODE *psDeviceNode,
                      const IMG_CHAR *pszDestSpaceName,
                      IMG_DEVMEM_OFFSET_T uiDestOffset,
                      const IMG_CHAR *pszRefSymbolicName,
                      IMG_DEVMEM_OFFSET_T uiRefOffset,
                      const IMG_CHAR *pszPDumpDevName,
                      IMG_UINT32 ui32WordSize,
                      IMG_UINT32 ui32AlignShift,
                      IMG_UINT32 ui32Shift,
                      IMG_UINT32 uiPDumpFlags);

/* Register the connection with the PDump subsystem */
PVRSRV_ERROR
PDumpRegisterConnection(PVRSRV_DEVICE_NODE *psDeviceNode,
                        void *hSyncPrivData,
                        PFN_PDUMP_SYNCBLOCKS pfnPDumpSyncBlocks,
                        PDUMP_CONNECTION_DATA **ppsPDumpConnectionData);

/* Unregister the connection with the PDump subsystem */
void
PDumpUnregisterConnection(PVRSRV_DEVICE_NODE *psDeviceNode,
                          PDUMP_CONNECTION_DATA *psPDumpConnectionData);

/* Register for notification of PDump Transition into/out of capture range */
PVRSRV_ERROR
PDumpRegisterTransitionCallback(PDUMP_CONNECTION_DATA *psPDumpConnectionData,
                                PFN_PDUMP_TRANSITION pfnCallback,
                                void *hPrivData,
                                void *pvDevice,
                                void **ppvHandle);

/* Unregister notification of PDump Transition */
void
PDumpUnregisterTransitionCallback(void *pvHandle);

PVRSRV_ERROR
PDumpRegisterTransitionCallbackFenceSync(void *hPrivData,
                                         PFN_PDUMP_TRANSITION_FENCE_SYNC pfnCallback,
                                         void **ppvHandle);

void
PDumpUnregisterTransitionCallbackFenceSync(void *pvHandle);

/* Notify PDump of a Transition into/out of capture range */
PVRSRV_ERROR
PDumpTransition(PVRSRV_DEVICE_NODE *psDeviceNode,
                PDUMP_CONNECTION_DATA *psPDumpConnectionData,
                PDUMP_TRANSITION_EVENT eEvent,
                IMG_UINT32 ui32PDumpFlags);

/* Check if writing to a PDump file is permitted for the given device */
IMG_BOOL PDumpIsDevicePermitted(PVRSRV_DEVICE_NODE *psDeviceNode);

/* _ui32PDumpFlags must be a variable in the local scope */
#define PDUMP_LOCK(_ui32PDumpFlags) do \
	{ if ((_ui32PDumpFlags & PDUMP_FLAGS_PDUMP_LOCK_HELD) == 0)\
		{\
			PDumpLock();\
		}\
	MSC_SUPPRESS_4127\
	} while (0)

/* _ui32PDumpFlags must be a variable in the local scope */
#define PDUMP_UNLOCK(_ui32PDumpFlags) do \
	{ if ((_ui32PDumpFlags & PDUMP_FLAGS_PDUMP_LOCK_HELD) == 0)\
		{\
			PDumpUnlock();\
		}\
	MSC_SUPPRESS_4127\
	} while (0)

#define PDUMPINIT				PDumpInitCommon
#define PDUMPDEINIT				PDumpDeInitCommon
#define PDUMPREG32				PDumpReg32
#define PDUMPREG64				PDumpReg64
#define PDUMPREGREAD32			PDumpRegRead32
#define PDUMPREGREAD64			PDumpRegRead64
#define PDUMPCOMMENT(d, ...)	PDumpCommentWithFlags(d, PDUMP_FLAGS_CONTINUOUS, __VA_ARGS__)
#define PDUMPCOMMENTWITHFLAGS	PDumpCommentWithFlags
#define PDUMPREGPOL				PDumpRegPolKM
#define PDUMPREGBASEDCBP		PDumpRegBasedCBP
#define PDUMPENDINITPHASE		PDumpStopInitPhase
#define PDUMPIDLWITHFLAGS		PDumpIDLWithFlags
#define PDUMPIDL				PDumpIDL
#define PDUMPPOWCMDSTART		PDumpPowerTransitionStart
#define PDUMPPOWCMDEND			PDumpPowerTransitionEnd
#define PDUMPCOM				PDumpCOMCommand

/* _ui32PDumpFlags must be a variable in the local scope */
#define PDUMP_BLKSTART(_ui32PDumpFlags) do \
	{ PDUMP_LOCK(_ui32PDumpFlags);\
	_ui32PDumpFlags |= PDUMP_FLAGS_PDUMP_LOCK_HELD;\
	MSC_SUPPRESS_4127\
	} while (0)

/* _ui32PDumpFlags must be a variable in the local scope */
#define PDUMP_BLKEND(_ui32PDumpFlags) do \
	{ _ui32PDumpFlags &= ~PDUMP_FLAGS_PDUMP_LOCK_HELD;\
	PDUMP_UNLOCK(_ui32PDumpFlags);\
	MSC_SUPPRESS_4127\
	} while (0)

/* _ui32PDumpFlags must be a variable in the local scope */
#define PDUMPIF(_dev,_msg,_ui32PDumpFlags) do \
	{PDUMP_BLKSTART(_ui32PDumpFlags);\
	PDumpIfKM(_dev,_msg,_ui32PDumpFlags);\
	MSC_SUPPRESS_4127\
	} while (0)

#define PDUMPELSE				PDumpElseKM

/* _ui32PDumpFlags must be a variable in the local scope */
#define PDUMPFI(_dev,_msg,_ui32PDumpFlags) do \
	{ PDumpFiKM(_dev,_msg,_ui32PDumpFlags);\
	PDUMP_BLKEND(_ui32PDumpFlags);\
	MSC_SUPPRESS_4127\
	} while (0)

#else
/*
	We should be clearer about which functions can be called
	across the bridge as this looks rather unbalanced
*/

/*! Macro used to record a panic in the PDump script stream */
#define PDUMP_PANIC(_dev, _id, _msg)  ((void)0)

/*! Macro used to record a driver error in the PDump script stream to invalidate the capture */
#define PDUMP_ERROR(_dev, _err, _msg) ((void)0)

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpInitCommon)
#endif
static INLINE PVRSRV_ERROR
PDumpInitCommon(void)
{
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpConnectionNotify)
#endif
static INLINE void
PDumpConnectionNotify(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpDisconnectionNotify)
#endif
static INLINE void
PDumpDisconnectionNotify(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpLock)
#endif
static INLINE void
PDumpLock(void)
{
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpUnlock)
#endif
static INLINE void
PDumpUnlock(void)
{
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpStopInitPhase)
#endif
static INLINE void
PDumpStopInitPhase(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpSetFrameKM)
#endif
static INLINE PVRSRV_ERROR
PDumpSetFrameKM(CONNECTION_DATA *psConnection,
                PVRSRV_DEVICE_NODE *psDevNode,
                IMG_UINT32 ui32Frame)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(ui32Frame);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpGetFrameKM)
#endif
static INLINE PVRSRV_ERROR
PDumpGetFrameKM(CONNECTION_DATA *psConnection,
                PVRSRV_DEVICE_NODE *psDeviceNode,
                IMG_UINT32* pui32Frame)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(pui32Frame);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpCommentKM)
#endif
static INLINE PVRSRV_ERROR
PDumpCommentKM(CONNECTION_DATA *psConnection,
               PVRSRV_DEVICE_NODE *psDeviceNode,
               IMG_UINT32 ui32CommentSize,
               IMG_CHAR *pszComment,
               IMG_UINT32 ui32Flags)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
    PVR_UNREFERENCED_PARAMETER(ui32CommentSize);
	PVR_UNREFERENCED_PARAMETER(pszComment);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	return PVRSRV_OK;
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpSetDefaultCaptureParamsKM)
#endif
static INLINE PVRSRV_ERROR
PDumpSetDefaultCaptureParamsKM(CONNECTION_DATA *psConnection,
                               PVRSRV_DEVICE_NODE *psDeviceNode,
                               IMG_UINT32 ui32Mode,
                               IMG_UINT32 ui32Start,
                               IMG_UINT32 ui32End,
                               IMG_UINT32 ui32Interval,
                               IMG_UINT32 ui32MaxParamFileSize,
                               IMG_UINT32 ui32AutoTermTimeout)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32Mode);
	PVR_UNREFERENCED_PARAMETER(ui32Start);
	PVR_UNREFERENCED_PARAMETER(ui32End);
	PVR_UNREFERENCED_PARAMETER(ui32Interval);
	PVR_UNREFERENCED_PARAMETER(ui32MaxParamFileSize);
	PVR_UNREFERENCED_PARAMETER(ui32AutoTermTimeout);

	return PVRSRV_OK;
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpPanic)
#endif
static INLINE PVRSRV_ERROR
PDumpPanic(IMG_UINT32      ui32PanicNo,
           IMG_CHAR*       pszPanicMsg,
           const IMG_CHAR* pszPPFunc,
           IMG_UINT32      ui32PPline)
{
	PVR_UNREFERENCED_PARAMETER(ui32PanicNo);
	PVR_UNREFERENCED_PARAMETER(pszPanicMsg);
	PVR_UNREFERENCED_PARAMETER(pszPPFunc);
	PVR_UNREFERENCED_PARAMETER(ui32PPline);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpCaptureError)
#endif
static INLINE PVRSRV_ERROR
PDumpCaptureError(PVRSRV_ERROR    ui32ErrorNo,
                  IMG_CHAR*       pszErrorMsg,
                  const IMG_CHAR* pszPPFunc,
                  IMG_UINT32      ui32PPline)
{
	PVR_UNREFERENCED_PARAMETER(ui32ErrorNo);
	PVR_UNREFERENCED_PARAMETER(pszErrorMsg);
	PVR_UNREFERENCED_PARAMETER(pszPPFunc);
	PVR_UNREFERENCED_PARAMETER(ui32PPline);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpIsLastCaptureFrameKM)
#endif
static INLINE PVRSRV_ERROR
PDumpIsLastCaptureFrameKM(IMG_BOOL *pbIsLastCaptureFrame)
{
	*pbIsLastCaptureFrame = IMG_FALSE;
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpGetStateKM)
#endif
static INLINE PVRSRV_ERROR
PDumpGetStateKM(IMG_UINT64 *ui64State)
{
	*ui64State = 0;
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpForceCaptureStopKM)
#endif
static INLINE PVRSRV_ERROR
PDumpForceCaptureStopKM(CONNECTION_DATA *psConnection,
                        PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpImageDescriptor)
#endif
static INLINE PVRSRV_ERROR
PDumpImageDescriptor(PVRSRV_DEVICE_NODE *psDeviceNode,
                     IMG_UINT32 ui32MMUContextID,
                     IMG_CHAR *pszSABFileName,
                     IMG_DEV_VIRTADDR sData,
                     IMG_UINT32 ui32DataSize,
                     IMG_UINT32 ui32LogicalWidth,
                     IMG_UINT32 ui32LogicalHeight,
                     IMG_UINT32 ui32PhysicalWidth,
                     IMG_UINT32 ui32PhysicalHeight,
                     PDUMP_PIXEL_FORMAT ePixFmt,
                     IMG_MEMLAYOUT eMemLayout,
                     IMG_FB_COMPRESSION eFBCompression,
                     const IMG_UINT32 *paui32FBCClearColour,
                     PDUMP_FBC_SWIZZLE eFBCSwizzle,
                     IMG_DEV_VIRTADDR sHeader,
                     IMG_UINT32 ui32HeaderSize,
                     IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32MMUContextID);
	PVR_UNREFERENCED_PARAMETER(pszSABFileName);
	PVR_UNREFERENCED_PARAMETER(sData);
	PVR_UNREFERENCED_PARAMETER(ui32DataSize);
	PVR_UNREFERENCED_PARAMETER(ui32LogicalWidth);
	PVR_UNREFERENCED_PARAMETER(ui32LogicalHeight);
	PVR_UNREFERENCED_PARAMETER(ui32PhysicalWidth);
	PVR_UNREFERENCED_PARAMETER(ui32PhysicalHeight);
	PVR_UNREFERENCED_PARAMETER(ePixFmt);
	PVR_UNREFERENCED_PARAMETER(eMemLayout);
	PVR_UNREFERENCED_PARAMETER(eFBCompression);
	PVR_UNREFERENCED_PARAMETER(paui32FBCClearColour);
	PVR_UNREFERENCED_PARAMETER(eFBCSwizzle);
	PVR_UNREFERENCED_PARAMETER(sHeader);
	PVR_UNREFERENCED_PARAMETER(ui32HeaderSize);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpDataDescriptor)
#endif
static INLINE PVRSRV_ERROR
PDumpDataDescriptor(PVRSRV_DEVICE_NODE *psDeviceNode,
                    IMG_UINT32 ui32MMUContextID,
                    IMG_CHAR *pszSABFileName,
                    IMG_DEV_VIRTADDR sData,
                    IMG_UINT32 ui32DataSize,
                    IMG_UINT32 ui32ElementType,
                    IMG_UINT32 ui32ElementCount,
                    IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(ui32MMUContextID);
	PVR_UNREFERENCED_PARAMETER(pszSABFileName);
	PVR_UNREFERENCED_PARAMETER(sData);
	PVR_UNREFERENCED_PARAMETER(ui32DataSize);
	PVR_UNREFERENCED_PARAMETER(ui32ElementType);
	PVR_UNREFERENCED_PARAMETER(ui32ElementCount);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpRegisterConnection)
#endif
static INLINE PVRSRV_ERROR
PDumpRegisterConnection(PVRSRV_DEVICE_NODE *psDeviceNode,
                        void *hSyncPrivData,
                        PFN_PDUMP_SYNCBLOCKS pfnPDumpSyncBlocks,
                        PDUMP_CONNECTION_DATA **ppsPDumpConnectionData)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(hSyncPrivData);
	PVR_UNREFERENCED_PARAMETER(pfnPDumpSyncBlocks);
	PVR_UNREFERENCED_PARAMETER(ppsPDumpConnectionData);

	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpUnregisterConnection)
#endif
static INLINE void
PDumpUnregisterConnection(PVRSRV_DEVICE_NODE *psDeviceNode,
                          PDUMP_CONNECTION_DATA *psPDumpConnectionData)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psPDumpConnectionData);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpRegisterTransitionCallback)
#endif
static INLINE PVRSRV_ERROR
PDumpRegisterTransitionCallback(PDUMP_CONNECTION_DATA *psPDumpConnectionData,
                                PFN_PDUMP_TRANSITION pfnCallback,
                                void *hPrivData,
                                void *pvDevice,
                                void **ppvHandle)
{
	PVR_UNREFERENCED_PARAMETER(psPDumpConnectionData);
	PVR_UNREFERENCED_PARAMETER(pfnCallback);
	PVR_UNREFERENCED_PARAMETER(hPrivData);
	PVR_UNREFERENCED_PARAMETER(pvDevice);
	PVR_UNREFERENCED_PARAMETER(ppvHandle);

	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpUnregisterTransitionCallback)
#endif
static INLINE void
PDumpUnregisterTransitionCallback(void *pvHandle)
{
	PVR_UNREFERENCED_PARAMETER(pvHandle);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpRegisterTransitionCallback)
#endif
static INLINE PVRSRV_ERROR
PDumpRegisterTransitionCallbackFenceSync(void *hPrivData,
                                         PFN_PDUMP_TRANSITION_FENCE_SYNC pfnCallback,
                                         void **ppvHandle)
{
	PVR_UNREFERENCED_PARAMETER(pfnCallback);
	PVR_UNREFERENCED_PARAMETER(hPrivData);
	PVR_UNREFERENCED_PARAMETER(ppvHandle);

	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpUnregisterTransitionCallbackFenceSync)
#endif
static INLINE void
PDumpUnregisterTransitionCallbackFenceSync(void *pvHandle)
{
	PVR_UNREFERENCED_PARAMETER(pvHandle);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpTransition)
#endif
static INLINE PVRSRV_ERROR
PDumpTransition(PVRSRV_DEVICE_NODE *psDeviceNode,
                PDUMP_CONNECTION_DATA *psPDumpConnectionData,
                PDUMP_TRANSITION_EVENT eEvent,
                IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(psPDumpConnectionData);
	PVR_UNREFERENCED_PARAMETER(eEvent);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	return PVRSRV_OK;
}

#if defined(__linux__) || defined(GCC_IA32) || defined(GCC_ARM) || defined(__QNXNTO__) || defined(INTEGRITY_OS)
	#define PDUMPINIT	PDumpInitCommon
	#define PDUMPDEINIT(args...)
	#define PDUMPREG32(args...)
	#define PDUMPREG64(args...)
	#define PDUMPREGREAD32(args...)
	#define PDUMPREGREAD64(args...)
	#define PDUMPCOMMENT(args...)
	#define PDUMPREGPOL(args...)
	#define PDUMPSYNC(args...)
	#define PDUMPCOPYTOMEM(args...)
	#define PDUMPWRITE(args...)
	#define PDUMPREGBASEDCBP(args...)
	#define PDUMPCOMMENTWITHFLAGS(args...)
	#define PDUMPENDINITPHASE(args...)
	#define PDUMPIDLWITHFLAGS(args...)
	#define PDUMPIDL(args...)
	#define PDUMPPOWCMDSTART(args...)
	#define PDUMPPOWCMDEND(args...)
	#define PDUMP_LOCK(args...)
	#define PDUMP_UNLOCK(args...)
	#define PDUMPIF(args...)
	#define PDUMPFI(args...)
	#define PDUMPCOM(args...)
#else
	#error Compiler not specified
#endif

#endif /* PDUMP */

#endif /* PDUMP_KM_H */

/******************************************************************************
 End of file (pdump_km.h)
******************************************************************************/
