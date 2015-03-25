/*************************************************************************/ /*!
@File
@Title          Device Memory Management PDump internal
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Services internal interface to PDump device memory management
                functions that are shared between client and server code.
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

#ifndef _DEVICEMEM_PDUMP_H_
#define _DEVICEMEM_PDUMP_H_

#include "devicemem.h"
#include "pdumpdefs.h"
#include "pdump.h"

#if defined(PDUMP)
/*
 * DevmemPDumpMem()
 *
 * takes a memory descriptor, offset, and size, and takes the current
 * contents of the memory at that location and writes it to the prm
 * pdump file, and emits a pdump LDB to load the data from that file.
 * The intention here is that the contents of the simulated buffer
 * upon pdump playback will be made to be the same as they are when
 * this command is run, enabling pdump of cases where the memory has
 * been modified externally, i.e. by the host cpu or by a third
 * party.
 */
extern IMG_VOID
DevmemPDumpLoadMem(DEVMEM_MEMDESC *psMemDesc,
                   IMG_DEVMEM_OFFSET_T uiOffset,
                   IMG_DEVMEM_SIZE_T uiSize,
                   PDUMP_FLAGS_T uiPDumpFlags);

/*
 * DevmemPDumpZeroMem()
 *
 * as DevmemPDumpMem() but the PDump allocation will be populated with zeros from
 * the zero page in the parameter stream
 */
extern IMG_VOID
DevmemPDumpLoadZeroMem(DEVMEM_MEMDESC *psMemDesc,
                   IMG_DEVMEM_OFFSET_T uiOffset,
                   IMG_DEVMEM_SIZE_T uiSize,
                   PDUMP_FLAGS_T uiPDumpFlags);

/*
 * DevmemPDumpMemValue()
 * 
 * As above but dumps the value at a dword-aligned address in plain
 * text to the pdump script2 file. Useful for patching a buffer at
 * pdump playback by simply editing the script output file.
 * 
 * (The same functionality can be achieved by the above function but
 *  the binary PARAM file must be patched in that case.)
 */
IMG_INTERNAL IMG_VOID
DevmemPDumpLoadMemValue32(DEVMEM_MEMDESC *psMemDesc,
                        IMG_DEVMEM_OFFSET_T uiOffset,
                        IMG_UINT32 ui32Value,
                        PDUMP_FLAGS_T uiPDumpFlags);

/*
 * DevmemPDumpMemValue64()
 *
 * As above but dumps the 64bit-value at a dword-aligned address in plain
 * text to the pdump script2 file. Useful for patching a buffer at
 * pdump playback by simply editing the script output file.
 *
 * (The same functionality can be achieved by the above function but
 *  the binary PARAM file must be patched in that case.)
 */
IMG_INTERNAL IMG_VOID
DevmemPDumpLoadMemValue64(DEVMEM_MEMDESC *psMemDesc,
                        IMG_DEVMEM_OFFSET_T uiOffset,
                        IMG_UINT64 ui64Value,
                        PDUMP_FLAGS_T uiPDumpFlags);

/*
 * DevmemPDumpPageCatBaseToSAddr()
 *
 * Returns the symbolic address of a piece of memory represented
 * by an offset into the mem descriptor.
 */
extern PVRSRV_ERROR
DevmemPDumpPageCatBaseToSAddr(DEVMEM_MEMDESC		*psMemDesc,
							  IMG_DEVMEM_OFFSET_T	*puiMemOffset,
							  IMG_CHAR				*pszName,
							  IMG_UINT32			ui32Size);

/*
 * DevmemPDumpSaveToFile()
 *
 * emits a pdump SAB to cause the current contents of the memory to be
 * written to the given file during playback
 */
extern IMG_VOID
DevmemPDumpSaveToFile(DEVMEM_MEMDESC *psMemDesc,
                      IMG_DEVMEM_OFFSET_T uiOffset,
                      IMG_DEVMEM_SIZE_T uiSize,
                      const IMG_CHAR *pszFilename);

/*
 * DevmemPDumpSaveToFileVirtual()
 *
 * emits a pdump SAB, just like DevmemPDumpSaveToFile(), but uses the
 * virtual address and device MMU context to cause the pdump player to
 * traverse the MMU page tables itself.
 */
extern IMG_VOID
DevmemPDumpSaveToFileVirtual(DEVMEM_MEMDESC *psMemDesc,
                             IMG_DEVMEM_OFFSET_T uiOffset,
                             IMG_DEVMEM_SIZE_T uiSize,
                             const IMG_CHAR *pszFilename,
							 IMG_UINT32 ui32FileOffset,
							 IMG_UINT32 ui32PdumpFlags);


/*
 *
 * Devmem_PDumpDevmemPol32()
 *
 * writes a PDump 'POL' command to wait for a masked 32-bit memory
 * location to become the specified value
 */
extern PVRSRV_ERROR
DevmemPDumpDevmemPol32(const DEVMEM_MEMDESC *psMemDesc,
                           IMG_DEVMEM_OFFSET_T uiOffset,
                           IMG_UINT32 ui32Value,
                           IMG_UINT32 ui32Mask,
                           PDUMP_POLL_OPERATOR eOperator,
                           PDUMP_FLAGS_T ui32PDumpFlags);

/*
 * DevmemPDumpCBP()
 *
 * Polls for space in circular buffer. Reads the read offset
 * from memory and waits until there is enough space to write
 * the packet.
 *
 * hMemDesc      - MemDesc which contains the read offset
 * uiReadOffset  - Offset into MemDesc to the read offset
 * uiWriteOffset - Current write offset
 * uiPacketSize  - Size of packet to write
 * uiBufferSize  - Size of circular buffer
 */
extern PVRSRV_ERROR
DevmemPDumpCBP(const DEVMEM_MEMDESC *psMemDesc,
				IMG_DEVMEM_OFFSET_T uiReadOffset,
				IMG_DEVMEM_OFFSET_T uiWriteOffset,
				IMG_DEVMEM_SIZE_T uiPacketSize,
				IMG_DEVMEM_SIZE_T uiBufferSize);

#else	/* PDUMP */

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemPDumpLoadMem)
#endif
static INLINE IMG_VOID
DevmemPDumpLoadMem(DEVMEM_MEMDESC *psMemDesc,
                   IMG_DEVMEM_OFFSET_T uiOffset,
                   IMG_DEVMEM_SIZE_T uiSize,
                   PDUMP_FLAGS_T uiPDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psMemDesc);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiPDumpFlags);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemPDumpLoadMemValue32)
#endif
static INLINE IMG_VOID
DevmemPDumpLoadMemValue32(DEVMEM_MEMDESC *psMemDesc,
                        IMG_DEVMEM_OFFSET_T uiOffset,
                        IMG_UINT32 ui32Value,
                        PDUMP_FLAGS_T uiPDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psMemDesc);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(uiPDumpFlags);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemPDumpLoadMemValue64)
#endif
static INLINE IMG_VOID
DevmemPDumpLoadMemValue64(DEVMEM_MEMDESC *psMemDesc,
                        IMG_DEVMEM_OFFSET_T uiOffset,
                        IMG_UINT64 ui64Value,
                        PDUMP_FLAGS_T uiPDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psMemDesc);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(ui64Value);
	PVR_UNREFERENCED_PARAMETER(uiPDumpFlags);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemPDumpLoadMemValue)
#endif
static INLINE IMG_VOID
DevmemPDumpLoadMemValue(DEVMEM_MEMDESC *psMemDesc,
                        IMG_DEVMEM_OFFSET_T uiOffset,
                        IMG_UINT32 ui32Value,
                        PDUMP_FLAGS_T uiPDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psMemDesc);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(uiPDumpFlags);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemPDumpPageCatBaseToSAddr)
#endif
static INLINE PVRSRV_ERROR
DevmemPDumpPageCatBaseToSAddr(DEVMEM_MEMDESC		*psMemDesc,
							  IMG_DEVMEM_OFFSET_T	*puiMemOffset,
							  IMG_CHAR				*pszName,
							  IMG_UINT32			ui32Size)
{
	PVR_UNREFERENCED_PARAMETER(psMemDesc);
	PVR_UNREFERENCED_PARAMETER(puiMemOffset);
	PVR_UNREFERENCED_PARAMETER(pszName);
	PVR_UNREFERENCED_PARAMETER(ui32Size);

	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemPDumpSaveToFile)
#endif
static INLINE IMG_VOID
DevmemPDumpSaveToFile(DEVMEM_MEMDESC *psMemDesc,
                      IMG_DEVMEM_OFFSET_T uiOffset,
                      IMG_DEVMEM_SIZE_T uiSize,
                      const IMG_CHAR *pszFilename)
{
	PVR_UNREFERENCED_PARAMETER(psMemDesc);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(pszFilename);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemPDumpSaveToFileVirtual)
#endif
static INLINE IMG_VOID
DevmemPDumpSaveToFileVirtual(DEVMEM_MEMDESC *psMemDesc,
                             IMG_DEVMEM_OFFSET_T uiOffset,
                             IMG_DEVMEM_SIZE_T uiSize,
                             const IMG_CHAR *pszFilename,
							 IMG_UINT32 ui32FileOffset,
							 IMG_UINT32 ui32PdumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psMemDesc);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(pszFilename);
	PVR_UNREFERENCED_PARAMETER(ui32FileOffset);
	PVR_UNREFERENCED_PARAMETER(ui32PdumpFlags);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemPDumpDevmemPol32)
#endif
static INLINE PVRSRV_ERROR
DevmemPDumpDevmemPol32(const DEVMEM_MEMDESC *psMemDesc,
                           IMG_DEVMEM_OFFSET_T uiOffset,
                           IMG_UINT32 ui32Value,
                           IMG_UINT32 ui32Mask,
                           PDUMP_POLL_OPERATOR eOperator,
                           PDUMP_FLAGS_T ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psMemDesc);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(ui32Mask);
	PVR_UNREFERENCED_PARAMETER(eOperator);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);

	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(DevmemPDumpCBP)
#endif
static INLINE PVRSRV_ERROR
DevmemPDumpCBP(const DEVMEM_MEMDESC *psMemDesc,
				IMG_DEVMEM_OFFSET_T uiReadOffset,
				IMG_DEVMEM_OFFSET_T uiWriteOffset,
				IMG_DEVMEM_SIZE_T uiPacketSize,
				IMG_DEVMEM_SIZE_T uiBufferSize)
{
	PVR_UNREFERENCED_PARAMETER(psMemDesc);
	PVR_UNREFERENCED_PARAMETER(uiReadOffset);
	PVR_UNREFERENCED_PARAMETER(uiWriteOffset);
	PVR_UNREFERENCED_PARAMETER(uiPacketSize);
	PVR_UNREFERENCED_PARAMETER(uiBufferSize);

	return PVRSRV_OK;
}
#endif	/* PDUMP */
#endif	/* _DEVICEMEM_PDUMP_H_ */
