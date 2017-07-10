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

#ifndef _PDUMP_KM_H_
#define _PDUMP_KM_H_

#if defined(PDUMP)
#include <stdarg.h>
#endif

/* services/srvkm/include/ */
#include "device.h"

/* include/ */
#include "pvrsrv_error.h"


#if defined(__KERNEL__) && defined(LINUX) && !defined(__GENKSYMS__)
#define __pvrsrv_defined_struct_enum__
#include <services_kernel_client.h>
#endif

#include "connection_server.h"
#include "sync_server.h"
/*
 *	Pull in pdump flags from services include
 */
#include "pdump.h"
#include "pdumpdefs.h"

/* Define this to enable the PDUMP_HERE trace in the server */
#undef PDUMP_TRACE

#if defined(PDUMP_TRACE)
#define PDUMP_HERE(a)	if (ui32Flags & PDUMP_FLAGS_DEBUG) PVR_DPF((PVR_DBG_WARNING, "HERE %d", (a)))
#define PDUMP_HEREA(a)	PVR_DPF((PVR_DBG_WARNING, "HERE ALWAYS %d", (a)))
#else
#define PDUMP_HERE(a)	(void)(a);
#define PDUMP_HEREA(a)	(void)(a);
#endif

#define PDUMP_PD_UNIQUETAG	(IMG_HANDLE)0
#define PDUMP_PT_UNIQUETAG	(IMG_HANDLE)0


#if defined(PDUMP_DEBUG_OUTFILES)
/* counter increments each time debug write is called */
extern IMG_UINT32 g_ui32EveryLineCounter;
#endif

typedef struct _PDUMP_CONNECTION_DATA_ PDUMP_CONNECTION_DATA;
typedef PVRSRV_ERROR (*PFN_PDUMP_TRANSITION)(void **pvData, IMG_BOOL bInto, IMG_UINT32 ui32PDumpFlags);

#ifdef PDUMP

/*! Macro used to record a panic in the PDump script stream */
#define PDUMP_PANIC(_id, _msg) do \
		{ PVRSRV_ERROR _eE;\
			_eE = PDumpPanic(((RGX_PDUMP_PANIC_ ## _id) & 0xFFFF), _msg, __FUNCTION__, __LINE__);	\
			PVR_LOG_IF_ERROR(_eE, "PDumpPanic");\
		MSC_SUPPRESS_4127\
		} while (0)

/*! Macro used to record a driver error in the PDump script stream to invalidate the capture */
#define PDUMP_ERROR(_err, _msg) do \
		{   (void) PDumpCaptureError((_err), (_msg), __FUNCTION__, __LINE__);\
		MSC_SUPPRESS_4127\
		} while (0)

	/* Shared across pdump_x files */
	PVRSRV_ERROR PDumpInitCommon(void);
	void PDumpDeInitCommon(void);
	IMG_BOOL PDumpReady(void);
	void PDumpGetParameterZeroPageInfo(PDUMP_FILEOFFSET_T *puiZeroPageOffset,
									size_t *puiZeroPageSize,
									const IMG_CHAR **ppszZeroPageFilename);

	void PDumpConnectionNotify(void);
	void PDumpDisconnectionNotify(void);

	void PDumpStopInitPhase(IMG_BOOL bPDumpClient, IMG_BOOL bInitClient);
	PVRSRV_ERROR PDumpSetFrameKM(CONNECTION_DATA *psConnection,
                             PVRSRV_DEVICE_NODE * psDeviceNode,
                             IMG_UINT32 ui32Frame);
	PVRSRV_ERROR PDumpGetFrameKM(CONNECTION_DATA *psConnection,
                             PVRSRV_DEVICE_NODE * psDeviceNode,
                             IMG_UINT32* pui32Frame);
	PVRSRV_ERROR PDumpCommentKM(IMG_CHAR *pszComment, IMG_UINT32 ui32Flags);

	PVRSRV_ERROR PDumpSetDefaultCaptureParamsKM(IMG_UINT32 ui32Mode,
	                                           IMG_UINT32 ui32Start,
	                                           IMG_UINT32 ui32End,
	                                           IMG_UINT32 ui32Interval,
	                                           IMG_UINT32 ui32MaxParamFileSize);


	PVRSRV_ERROR PDumpReg32(IMG_CHAR	*pszPDumpRegName,
							IMG_UINT32	ui32RegAddr,
							IMG_UINT32	ui32RegValue,
							IMG_UINT32	ui32Flags);

	PVRSRV_ERROR PDumpReg64(IMG_CHAR	*pszPDumpRegName,
							IMG_UINT32	ui32RegAddr,
							IMG_UINT64	ui64RegValue,
							IMG_UINT32	ui32Flags);

	PVRSRV_ERROR PDumpRegLabelToReg64(IMG_CHAR *pszPDumpRegName,
                                          IMG_UINT32 ui32RegDst,
                                          IMG_UINT32 ui32RegSrc,
                                          IMG_UINT32 ui32Flags);

	PVRSRV_ERROR PDumpMemLabelToInternalVar(IMG_CHAR *pszInternalVar,
                                                PMR *psPMR,
                                                IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                                                IMG_UINT32 ui32Flags);

	PVRSRV_ERROR PDumpWriteVarORValueOp (const IMG_CHAR *pszInternalVariable,
                                             const IMG_UINT64 ui64Value,
                                             const IMG_UINT32 ui32PDumpFlags);

	PVRSRV_ERROR PDumpWriteVarANDValueOp (const IMG_CHAR *pszInternalVariable,
                                              const IMG_UINT64 ui64Value,
                                              const IMG_UINT32 ui32PDumpFlags);

	PVRSRV_ERROR PDumpWriteVarSHRValueOp (const IMG_CHAR *pszInternalVariable,
                                              const IMG_UINT64 ui64Value,
                                              const IMG_UINT32 ui32PDumpFlags);

	PVRSRV_ERROR PDumpInternalVarToReg32(IMG_CHAR *pszPDumpRegName,
                                             IMG_UINT32	ui32Reg,
                                             IMG_CHAR *pszInternalVar,
                                             IMG_UINT32	ui32Flags);

	PVRSRV_ERROR PDumpInternalVarToReg64(IMG_CHAR *pszPDumpRegName,
                                             IMG_UINT32	ui32Reg,
                                             IMG_CHAR *pszInternalVar,
                                             IMG_UINT32	ui32Flags);

	PVRSRV_ERROR PDumpMemLabelToMem32(PMR *psPMRSource,
                                          PMR *psPMRDest,
                                          IMG_DEVMEM_OFFSET_T uiLogicalOffsetSource,
                                          IMG_DEVMEM_OFFSET_T uiLogicalOffsetDest,
                                          IMG_UINT32 ui32Flags);

	PVRSRV_ERROR PDumpMemLabelToMem64(PMR *psPMRSource,
                                          PMR *psPMRDest,
                                          IMG_DEVMEM_OFFSET_T uiLogicalOffsetSource,
                                          IMG_DEVMEM_OFFSET_T uiLogicalOffsetDest,
                                          IMG_UINT32	ui32Flags);

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

	PVRSRV_ERROR PDumpRegLabelToInternalVar(IMG_CHAR *pszPDumpRegName,
                                                IMG_UINT32 ui32Reg,
                                                IMG_CHAR *pszInternalVar,
                                                IMG_UINT32 ui32Flags);

	PVRSRV_ERROR PDumpSAW(IMG_CHAR      *pszDevSpaceName,
	                      IMG_UINT32    ui32HPOffsetBytes,
	                      IMG_UINT32    ui32NumSaveBytes,
	                      IMG_CHAR      *pszOutfileName,
	                      IMG_UINT32    ui32OutfileOffsetByte,
	                      PDUMP_FLAGS_T uiPDumpFlags);

	PVRSRV_ERROR PDumpRegPolKM(IMG_CHAR				*pszPDumpRegName,
							   IMG_UINT32			ui32RegAddr,
							   IMG_UINT32			ui32RegValue,
							   IMG_UINT32			ui32Mask,
							   IMG_UINT32			ui32Flags,
							   PDUMP_POLL_OPERATOR	eOperator);

	IMG_IMPORT PVRSRV_ERROR PDumpBitmapKM(PVRSRV_DEVICE_NODE *psDeviceNode,
										  IMG_CHAR *pszFileName,
										  IMG_UINT32 ui32FileOffset,
										  IMG_UINT32 ui32Width,
										  IMG_UINT32 ui32Height,
										  IMG_UINT32 ui32StrideInBytes,
										  IMG_DEV_VIRTADDR sDevBaseAddr,
										  IMG_UINT32 ui32MMUContextID,
										  IMG_UINT32 ui32Size,
										  PDUMP_PIXEL_FORMAT ePixelFormat,
										  IMG_UINT32 ui32AddrMode,
										  IMG_UINT32 ui32PDumpFlags);

	IMG_IMPORT PVRSRV_ERROR PDumpReadRegKM(IMG_CHAR *pszPDumpRegName,
										   IMG_CHAR *pszFileName,
										   IMG_UINT32 ui32FileOffset,
										   IMG_UINT32 ui32Address,
										   IMG_UINT32 ui32Size,
										   IMG_UINT32 ui32PDumpFlags);

	PVRSRV_ERROR PDumpCommentWithFlags(IMG_UINT32	ui32Flags,
									   IMG_CHAR*	pszFormat,
									   ...) __printf(2, 3);

	PVRSRV_ERROR PDumpCommentWithFlagsVA(IMG_UINT32 ui32Flags,
									    const IMG_CHAR * pszFormat,
										va_list args);

	PVRSRV_ERROR PDumpPanic(IMG_UINT32      ui32PanicNo,
							IMG_CHAR*       pszPanicMsg,
							const IMG_CHAR* pszPPFunc,
							IMG_UINT32      ui32PPline);

	PVRSRV_ERROR PDumpCaptureError(PVRSRV_ERROR    ui32ErrorNo,
	                               IMG_CHAR*       pszErrorMsg,
	                               const IMG_CHAR* pszPPFunc,
	                               IMG_UINT32      ui32PPline);

	PVRSRV_ERROR PDumpPDReg(PDUMP_MMU_ATTRIB *psMMUAttrib,
							IMG_UINT32	ui32Reg,
							IMG_UINT32	ui32dwData,
							IMG_HANDLE	hUniqueTag);
	PVRSRV_ERROR PDumpPDRegWithFlags(PDUMP_MMU_ATTRIB *psMMUAttrib,
									 IMG_UINT32		ui32Reg,
									 IMG_UINT32		ui32Data,
									 IMG_UINT32		ui32Flags,
									 IMG_HANDLE		hUniqueTag);

	PVRSRV_ERROR PDumpIsLastCaptureFrameKM(IMG_BOOL *pbIsLastCaptureFrame);

	PVRSRV_ERROR PDumpIsCaptureFrameKM(IMG_BOOL *bIsCapturing);

	PVRSRV_ERROR PDumpRegRead32(IMG_CHAR *pszPDumpRegName,
								const IMG_UINT32 dwRegOffset,
								IMG_UINT32	ui32Flags);
	PVRSRV_ERROR PDumpRegRead64(IMG_CHAR *pszPDumpRegName,
								const IMG_UINT32 dwRegOffset,
								IMG_UINT32	ui32Flags);

	PVRSRV_ERROR PDumpIDLWithFlags(IMG_UINT32 ui32Clocks, IMG_UINT32 ui32Flags);
	PVRSRV_ERROR PDumpIDL(IMG_UINT32 ui32Clocks);

	PVRSRV_ERROR PDumpRegBasedCBP(IMG_CHAR		*pszPDumpRegName,
								  IMG_UINT32	ui32RegOffset,
								  IMG_UINT32	ui32WPosVal,
								  IMG_UINT32	ui32PacketSize,
								  IMG_UINT32	ui32BufferSize,
								  IMG_UINT32	ui32Flags);

	PVRSRV_ERROR PDumpTRG(IMG_CHAR *pszMemSpace,
	                      IMG_UINT32 ui32MMUCtxID,
	                      IMG_UINT32 ui32RegionID,
	                      IMG_BOOL bEnable,
	                      IMG_UINT64 ui64VAddr,
	                      IMG_UINT64 ui64LenBytes,
	                      IMG_UINT32 ui32XStride,
	                      IMG_UINT32 ui32Flags);

	PVRSRV_ERROR PDumpCreateLockKM(void);
	void PDumpDestroyLockKM(void);
	void PDumpLock(void);
	void PDumpUnlock(void);

	PVRSRV_ERROR PDumpIfKM(IMG_CHAR		*pszPDumpCond);
	PVRSRV_ERROR PDumpElseKM(IMG_CHAR	*pszPDumpCond);
	PVRSRV_ERROR PDumpFiKM(IMG_CHAR		*pszPDumpCond);

	void PDumpPowerTransitionStart(void);
	void PDumpPowerTransitionEnd(void);
	IMG_BOOL PDumpInPowerTransition(void);
	IMG_BOOL PDumpIsDumpSuspended(void);

	/*!
	 * @name	PDumpWriteParameter
	 * @brief	General function for writing to PDump stream. Used
	 *          mainly for memory dumps to parameter stream.
	 * 			Usually more convenient to use PDumpWriteScript below
	 * 			for the script stream.
	 * @param	psui8Data - data to write
	 * @param	ui32Size - size of write
	 * @param	ui32Flags - PDump flags
	 * @param   pui32FileOffset - on return contains the file offset to
	 *                            the start of the parameter data
	 * @param   aszFilenameStr - pointer to at least a 20 char buffer to
	 *                           return the parameter filename
	 * @return	error
	 */
	PVRSRV_ERROR PDumpWriteParameter(IMG_UINT8 *psui8Data, IMG_UINT32 ui32Size,
			IMG_UINT32 ui32Flags, IMG_UINT32* pui32FileOffset,
			IMG_CHAR* aszFilenameStr);

	/*!
	 * @name	PDumpWriteScript
	 * @brief	Write an PDumpOS created string to the "script" output stream
	 * @param	hString - PDump OS layer handle of string buffer to write
	 * @param	ui32Flags - PDump flags
	 * @return	IMG_TRUE on success.
	 */
	IMG_BOOL PDumpWriteScript(IMG_HANDLE hString, IMG_UINT32 ui32Flags);

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
extern PVRSRV_ERROR
PDumpWriteShiftedMaskedValue(const IMG_CHAR *pszDestRegspaceName,
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
extern PVRSRV_ERROR
PDumpWriteSymbAddress(const IMG_CHAR *pszDestSpaceName,
                      IMG_DEVMEM_OFFSET_T uiDestOffset,
                      const IMG_CHAR *pszRefSymbolicName,
                      IMG_DEVMEM_OFFSET_T uiRefOffset,
                      const IMG_CHAR *pszPDumpDevName,
                      IMG_UINT32 ui32WordSize,
                      IMG_UINT32 ui32AlignShift,
                      IMG_UINT32 ui32Shift,
                      IMG_UINT32 uiPDumpFlags);

/* Register the connection with the PDump subsystem */
extern PVRSRV_ERROR PDumpRegisterConnection(SYNC_CONNECTION_DATA *psSyncConnectionData,
											PDUMP_CONNECTION_DATA **ppsPDumpConnectionData);

/* Unregister the connection with the PDump subsystem */
extern void PDumpUnregisterConnection(PDUMP_CONNECTION_DATA *psPDumpConnectionData);

/* Register for notification of PDump Transition into/out of capture range */
extern PVRSRV_ERROR PDumpRegisterTransitionCallback(PDUMP_CONNECTION_DATA *psPDumpConnectionData,
													 PFN_PDUMP_TRANSITION pfnCallback,
													 void *hPrivData,
													 void **ppvHandle);

/* Unregister notification of PDump Transition */
extern void PDumpUnregisterTransitionCallback(void *pvHandle);

/* Notify PDump of a Transition into/out of capture range */
extern PVRSRV_ERROR PDumpTransition(PDUMP_CONNECTION_DATA *psPDumpConnectionData, IMG_BOOL bInto, IMG_UINT32 ui32PDumpFlags);

/* Wires-up a MIPS TLB in the page table*/
extern PVRSRV_ERROR PdumpWireUpMipsTLB(PMR *psPMRSource,
						PMR *psPMRDest,
						IMG_DEVMEM_OFFSET_T uiLogicalOffsetSource,
						IMG_DEVMEM_OFFSET_T uiLogicalOffsetDest,
						IMG_UINT32 ui32AllocationFlags,
						IMG_UINT32 ui32Flags);

/*Invalidate a MIPS TLB in the page table */
PVRSRV_ERROR PdumpInvalidateMipsTLB(PMR *psPMRDest,
									IMG_DEVMEM_OFFSET_T uiLogicalOffsetDest,
									IMG_UINT32 ui32MipsTLBValidClearMask,
									IMG_UINT32 ui32Flags);



	#define PDUMP_LOCK				PDumpLock
	#define PDUMP_UNLOCK			PDumpUnlock

	#define PDUMPINIT				PDumpInitCommon
	#define PDUMPDEINIT				PDumpDeInitCommon
	#define PDUMPREG32				PDumpReg32
	#define PDUMPREG64				PDumpReg64
	#define PDUMPREGREAD32			PDumpRegRead32
	#define PDUMPREGREAD64			PDumpRegRead64
	#define PDUMPCOMMENT(...)		PDumpCommentWithFlags(PDUMP_FLAGS_CONTINUOUS, __VA_ARGS__)
	#define PDUMPCOMMENTWITHFLAGS	PDumpCommentWithFlags
	#define PDUMPREGPOL				PDumpRegPolKM
	#define PDUMPPDREG				PDumpPDReg
	#define PDUMPPDREGWITHFLAGS		PDumpPDRegWithFlags
	#define PDUMPREGBASEDCBP		PDumpRegBasedCBP
	#define PDUMPENDINITPHASE		PDumpStopInitPhase
	#define PDUMPIDLWITHFLAGS		PDumpIDLWithFlags
	#define PDUMPIDL				PDumpIDL
	#define PDUMPPOWCMDSTART		PDumpPowerTransitionStart
	#define PDUMPPOWCMDEND			PDumpPowerTransitionEnd
	#define PDUMPPOWCMDINTRANS		PDumpInPowerTransition
	#define PDUMPIF					PDumpIfKM
	#define PDUMPELSE				PDumpElseKM
	#define PDUMPFI					PDumpFiKM
#else
	/*
		We should be clearer about which functions can be called
		across the bridge as this looks rather unbalanced
	*/

/*! Macro used to record a panic in the PDump script stream */
#define PDUMP_PANIC(_id, _msg)  ((void)0);

/*! Macro used to record a driver error in the PDump script stream to invalidate the capture */
#define PDUMP_ERROR(_err, _msg) ((void)0);

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
PDumpConnectionNotify(void)
{
	return;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpDisconnectionNotify)
#endif
static INLINE void
PDumpDisconnectionNotify(void)
{
	return;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpCreateLockKM)
#endif
static INLINE PVRSRV_ERROR
PDumpCreateLockKM(void)
{
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpDestroyLockKM)
#endif
static INLINE void
PDumpDestroyLockKM(void)
{
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
PDumpStopInitPhase(IMG_BOOL bPDumpClient, IMG_BOOL bInitClient)
{
	PVR_UNREFERENCED_PARAMETER(bPDumpClient);
	PVR_UNREFERENCED_PARAMETER(bInitClient);
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
PDumpCommentKM(IMG_CHAR *pszComment, IMG_UINT32 ui32Flags)
{
	PVR_UNREFERENCED_PARAMETER(pszComment);
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	return PVRSRV_OK;
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpCommentKM)
#endif
static INLINE PVRSRV_ERROR
PDumpSetDefaultCaptureParamsKM(IMG_UINT32 ui32Mode,
                              IMG_UINT32 ui32Start,
                              IMG_UINT32 ui32End,
                              IMG_UINT32 ui32Interval,
                              IMG_UINT32 ui32MaxParamFileSize)
{
	PVR_UNREFERENCED_PARAMETER(ui32Mode);
	PVR_UNREFERENCED_PARAMETER(ui32Start);
	PVR_UNREFERENCED_PARAMETER(ui32End);
	PVR_UNREFERENCED_PARAMETER(ui32Interval);
	PVR_UNREFERENCED_PARAMETER(ui32MaxParamFileSize);

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
#pragma inline(PDumpIsCaptureFrameKM)
#endif
static INLINE PVRSRV_ERROR
PDumpIsCaptureFrameKM(IMG_BOOL *bIsCapturing)
{
	*bIsCapturing = IMG_FALSE;
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpBitmapKM)
#endif
static INLINE PVRSRV_ERROR
PDumpBitmapKM(PVRSRV_DEVICE_NODE *psDeviceNode,
										  IMG_CHAR *pszFileName,
										  IMG_UINT32 ui32FileOffset,
										  IMG_UINT32 ui32Width,
										  IMG_UINT32 ui32Height,
										  IMG_UINT32 ui32StrideInBytes,
										  IMG_DEV_VIRTADDR sDevBaseAddr,
										  IMG_UINT32 ui32MMUContextID,
										  IMG_UINT32 ui32Size,
										  PDUMP_PIXEL_FORMAT ePixelFormat,
										  IMG_UINT32 ui32AddrMode,
										  IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	PVR_UNREFERENCED_PARAMETER(pszFileName);
	PVR_UNREFERENCED_PARAMETER(ui32FileOffset);
	PVR_UNREFERENCED_PARAMETER(ui32Width);
	PVR_UNREFERENCED_PARAMETER(ui32Height);
	PVR_UNREFERENCED_PARAMETER(ui32StrideInBytes);
	PVR_UNREFERENCED_PARAMETER(sDevBaseAddr);
	PVR_UNREFERENCED_PARAMETER(ui32MMUContextID);
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(ePixelFormat);
	PVR_UNREFERENCED_PARAMETER(ui32AddrMode);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpRegisterConnection)
#endif
static INLINE PVRSRV_ERROR
PDumpRegisterConnection(SYNC_CONNECTION_DATA *psSyncConnectionData,
						PDUMP_CONNECTION_DATA **ppsPDumpConnectionData)
{
	PVR_UNREFERENCED_PARAMETER(psSyncConnectionData);
	PVR_UNREFERENCED_PARAMETER(ppsPDumpConnectionData);

	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpUnregisterConnection)
#endif
static INLINE
void PDumpUnregisterConnection(PDUMP_CONNECTION_DATA *psPDumpConnectionData)
{
	PVR_UNREFERENCED_PARAMETER(psPDumpConnectionData);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpRegisterTransitionCallback)
#endif
static INLINE
PVRSRV_ERROR PDumpRegisterTransitionCallback(PDUMP_CONNECTION_DATA *psPDumpConnectionData,
											  PFN_PDUMP_TRANSITION pfnCallback,
											  void *hPrivData,
											  void **ppvHandle)
{
	PVR_UNREFERENCED_PARAMETER(psPDumpConnectionData);
	PVR_UNREFERENCED_PARAMETER(pfnCallback);
	PVR_UNREFERENCED_PARAMETER(hPrivData);
	PVR_UNREFERENCED_PARAMETER(ppvHandle);

	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpUnregisterTransitionCallback)
#endif
static INLINE
void PDumpUnregisterTransitionCallback(void *pvHandle)
{
	PVR_UNREFERENCED_PARAMETER(pvHandle);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PDumpTransition)
#endif
static INLINE
PVRSRV_ERROR PDumpTransition(PDUMP_CONNECTION_DATA *psPDumpConnectionData, IMG_BOOL bInto, IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psPDumpConnectionData);
	PVR_UNREFERENCED_PARAMETER(bInto);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
	return PVRSRV_OK;
}

	#if defined WIN32
		#define PDUMPINIT			PDumpInitCommon
		#define PDUMPDEINIT(...)		/ ## * PDUMPDEINIT(__VA_ARGS__) * ## /
		#define PDUMPREG32(...)			/ ## * PDUMPREG32(__VA_ARGS__) * ## /
		#define PDUMPREG64(...)			/ ## * PDUMPREG64(__VA_ARGS__) * ## /
		#define PDUMPREGREAD32(...)			/ ## * PDUMPREGREAD32(__VA_ARGS__) * ## /
		#define PDUMPREGREAD64(...)			/ ## * PDUMPREGREAD64(__VA_ARGS__) * ## /
		#define PDUMPCOMMENT(...)		/ ## * PDUMPCOMMENT(__VA_ARGS__) * ## /
		#define PDUMPREGPOL(...)		/ ## * PDUMPREGPOL(__VA_ARGS__) * ## /
		#define PDUMPPDREG(...)			/ ## * PDUMPPDREG(__VA_ARGS__) * ## /
		#define PDUMPPDREGWITHFLAGS(...)	/ ## * PDUMPPDREGWITHFLAGS(__VA_ARGS__) * ## /
		#define PDUMPSYNC(...)			/ ## * PDUMPSYNC(__VA_ARGS__) * ## /
		#define PDUMPCOPYTOMEM(...)		/ ## * PDUMPCOPYTOMEM(__VA_ARGS__) * ## /
		#define PDUMPWRITE(...)			/ ## * PDUMPWRITE(__VA_ARGS__) * ## /
		#define PDUMPCBP(...)			/ ## * PDUMPCBP(__VA_ARGS__) * ## /
		#define	PDUMPREGBASEDCBP(...)		/ ## * PDUMPREGBASEDCBP(__VA_ARGS__) * ## /
		#define PDUMPCOMMENTWITHFLAGS(...)	/ ## * PDUMPCOMMENTWITHFLAGS(__VA_ARGS__) * ## /
		#define PDUMPMALLOCPAGESPHYS(...)	/ ## * PDUMPMALLOCPAGESPHYS(__VA_ARGS__) * ## /
		#define PDUMPENDINITPHASE(...)		/ ## * PDUMPENDINITPHASE(__VA_ARGS__) * ## /
		#define PDUMPMSVDXREG(...)		/ ## * PDUMPMSVDXREG(__VA_ARGS__) * ## /
		#define PDUMPMSVDXREGWRITE(...)		/ ## * PDUMPMSVDXREGWRITE(__VA_ARGS__) * ## /
		#define PDUMPMSVDXREGREAD(...)		/ ## * PDUMPMSVDXREGREAD(__VA_ARGS__) * ## /
		#define PDUMPMSVDXPOLEQ(...)		/ ## * PDUMPMSVDXPOLEQ(__VA_ARGS__) * ## /
		#define PDUMPMSVDXPOL(...)		/ ## * PDUMPMSVDXPOL(__VA_ARGS__) * ## /
		#define PDUMPIDLWITHFLAGS(...)		/ ## * PDUMPIDLWITHFLAGS(__VA_ARGS__) * ## /
		#define PDUMPIDL(...)			/ ## * PDUMPIDL(__VA_ARGS__) * ## /
		#define PDUMPPOWCMDSTART(...)		/ ## * PDUMPPOWCMDSTART(__VA_ARGS__) * ## /
		#define PDUMPPOWCMDEND(...)		/ ## * PDUMPPOWCMDEND(__VA_ARGS__) * ## /
		#define PDUMP_LOCK			/ ## * PDUMP_LOCK(__VA_ARGS__) * ## /
		#define PDUMP_UNLOCK			/ ## * PDUMP_UNLOCK(__VA_ARGS__) * ## /
	#else
		#if defined LINUX || defined GCC_IA32 || defined GCC_ARM || defined __QNXNTO__ || defined(INTEGRITY_OS)
			#define PDUMPINIT	PDumpInitCommon
			#define PDUMPDEINIT(args...)
			#define PDUMPREG32(args...)
			#define PDUMPREG64(args...)
			#define PDUMPREGREAD32(args...)
			#define PDUMPREGREAD64(args...)
			#define PDUMPCOMMENT(args...)
			#define PDUMPREGPOL(args...)
			#define PDUMPPDREG(args...)
			#define PDUMPPDREGWITHFLAGS(args...)
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

		#else
			#error Compiler not specified
		#endif
	#endif
#endif


#endif /* _PDUMP_KM_H_ */

/******************************************************************************
 End of file (pdump_km.h)
******************************************************************************/
