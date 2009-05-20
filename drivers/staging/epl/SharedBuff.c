/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      Project independend shared buffer (linear + circular)

  Description:  Implementation of platform independend part for the
                shared buffer

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

  2006/06/27 -rs:   V 1.00 (initial version)

****************************************************************************/

#if defined(WIN32) || defined(_WIN32)

#ifdef UNDER_RTSS
	// RTX header
#include <windows.h>
#include <process.h>
#include <rtapi.h>

#elif __BORLANDC__
	// borland C header
#include <windows.h>
#include <process.h>

#elif WINCE
#include <windows.h>

#else
	// MSVC needs to include windows.h at first
	// the following defines ar necessary for function prototypes for waitable timers
#define _WIN32_WINDOWS 0x0401
#define _WIN32_WINNT   0x0400
#include <windows.h>
#include <process.h>
#endif

#endif

#include "global.h"
#include "SharedBuff.h"
#include "ShbIpc.h"

#include <linux/string.h>
#include <linux/kernel.h>

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

//---------------------------------------------------------------------------
//  Configuration
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Constant definitions
//---------------------------------------------------------------------------

#define SBC_MAGIC_ID    0x53424323	// magic ID ("SBC#")
#define SBL_MAGIC_ID    0x53424C23	// magic ID ("SBL#")

//---------------------------------------------------------------------------
//  Local types
//---------------------------------------------------------------------------

// structure to administrate circular shared buffer head
typedef struct {
	unsigned long m_ShbCirMagicID;	// magic ID ("SBC#")
	unsigned long m_ulBufferTotalSize;	// over-all size of complete buffer
	unsigned long m_ulBufferDataSize;	// size of complete data area
	unsigned long m_ulWrIndex;	// current write index (set bevore write)
	unsigned long m_ulRdIndex;	// current read index (set after read)
	unsigned long m_ulNumOfWriteJobs;	// number of currently (parallel running) write operations
	unsigned long m_ulDataInUse;	// currently used buffer size (incl. uncompleted write operations)
	unsigned long m_ulDataApended;	// buffer size of complete new written but not yet readable data (in case of m_ulNumOfWriteJobs>1)
	unsigned long m_ulBlocksApended;	// number of complete new written but not yet readable data blocks (in case of m_ulNumOfWriteJobs>1)
	unsigned long m_ulDataReadable;	// buffer size with readable (complete written) data
	unsigned long m_ulBlocksReadable;	// number of readable (complete written) data blocks
	tShbCirSigHndlrNewData m_pfnSigHndlrNewData;	// application handler to signal new data
	unsigned int m_fBufferLocked;	// TRUE if buffer is locked (because of pending reset request)
	tShbCirSigHndlrReset m_pfnSigHndlrReset;	// application handler to signal buffer reset is done
	unsigned char m_Data;	// start of data area (the real data size is unknown at this time)

} tShbCirBuff;

// structure to administrate linear shared buffer head
typedef struct {
	unsigned int m_ShbLinMagicID;	// magic ID ("SBL#")
	unsigned long m_ulBufferTotalSize;	// over-all size of complete buffer
	unsigned long m_ulBufferDataSize;	// size of complete data area
	unsigned char m_Data;	// start of data area (the real data size is unknown at this time)

} tShbLinBuff;

// type to save size of a single data block inside the circular shared buffer
typedef struct {
	unsigned int m_uiFullBlockSize:28;	// a single block must not exceed a length of 256MByte :-)
	unsigned int m_uiAlignFillBytes:4;

} tShbCirBlockSize;

#define SBC_BLOCK_ALIGNMENT                  4	// alignment must *not* be lower than sizeof(tShbCirBlockSize)!
#define SBC_MAX_BLOCK_SIZE         ((1<<28)-1)	// = (2^28 - 1) = (256MByte - 1) -> should be enought for real life :-)

#define SBL_BLOCK_ALIGNMENT                  4
#define SBL_MAX_BLOCK_SIZE         ((1<<28)-1)	// = (2^28 - 1) = (256MByte - 1) -> should be enought for real life :-)

//---------------------------------------------------------------------------
//  Global variables
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Local variables
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Prototypes of internal functions
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Get pointer to Circular Shared Buffer
//---------------------------------------------------------------------------

tShbCirBuff *ShbCirGetBuffer(tShbInstance pShbInstance_p)
{

	tShbCirBuff *pShbCirBuff;

	pShbCirBuff = (tShbCirBuff *) ShbIpcGetShMemPtr(pShbInstance_p);
	ASSERT(pShbCirBuff->m_ShbCirMagicID == SBC_MAGIC_ID);

	return (pShbCirBuff);

}

//---------------------------------------------------------------------------
//  Get pointer to Linear Shared Buffer
//---------------------------------------------------------------------------

tShbLinBuff *ShbLinGetBuffer(tShbInstance pShbInstance_p)
{

	tShbLinBuff *pShbLinBuff;

	pShbLinBuff = (tShbLinBuff *) ShbIpcGetShMemPtr(pShbInstance_p);
	ASSERT(pShbLinBuff->m_ShbLinMagicID == SBL_MAGIC_ID);

	return (pShbLinBuff);

}

// not inlined internal functions
int ShbCirSignalHandlerNewData(tShbInstance pShbInstance_p);
void ShbCirSignalHandlerReset(tShbInstance pShbInstance_p,
			      unsigned int fTimeOut_p);


//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

// not inlined external functions

//---------------------------------------------------------------------------
//  Initialize Shared Buffer Module
//---------------------------------------------------------------------------

tShbError ShbInit(void)
{

	tShbError ShbError;

	ShbError = ShbIpcInit();

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Deinitialize Shared Buffer Module
//---------------------------------------------------------------------------

tShbError ShbExit(void)
{

	tShbError ShbError;

	ShbError = ShbIpcExit();

	return (ShbError);

}

//-------------------------------------------------------------------------//
//                                                                         //
//          C i r c u l a r   S h a r e d   B u f f e r                    //
//                                                                         //
//-------------------------------------------------------------------------//

//---------------------------------------------------------------------------
//  Allocate Circular Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbCirAllocBuffer(unsigned long ulBufferSize_p,
			    const char *pszBufferID_p,
			    tShbInstance * ppShbInstance_p,
			    unsigned int *pfShbNewCreated_p)
{

	tShbInstance pShbInstance;
	tShbCirBuff *pShbCirBuff;
	unsigned int fShbNewCreated;
	unsigned long ulBufferDataSize;
	unsigned long ulBufferTotalSize;
	tShbError ShbError;

	// check arguments
	if ((ulBufferSize_p == 0) || (ppShbInstance_p == NULL)) {
		return (kShbInvalidArg);
	}

	// calculate length of memory to allocate
	ulBufferDataSize =
	    (ulBufferSize_p +
	     (SBC_BLOCK_ALIGNMENT - 1)) & ~(SBC_BLOCK_ALIGNMENT - 1);
	ulBufferTotalSize = ulBufferDataSize + sizeof(tShbCirBuff);

	// allocate a new or open an existing shared buffer
	ShbError = ShbIpcAllocBuffer(ulBufferTotalSize, pszBufferID_p,
				     &pShbInstance, &fShbNewCreated);
	if (ShbError != kShbOk) {
		goto Exit;
	}

	if (pShbInstance == NULL) {
		ShbError = kShbOutOfMem;
		goto Exit;
	}

	// get pointer to shared buffer
	pShbCirBuff = (tShbCirBuff *) ShbIpcGetShMemPtr(pShbInstance);

	// if the shared buffer was new created, than this process has
	// to initialize it, otherwise the buffer is already in use
	// and *must not* be reseted
	if (fShbNewCreated) {
#ifndef NDEBUG
		{
			memset(pShbCirBuff, 0xCC, ulBufferTotalSize);
		}
#endif

		pShbCirBuff->m_ShbCirMagicID = SBC_MAGIC_ID;
		pShbCirBuff->m_ulBufferTotalSize = ulBufferTotalSize;
		pShbCirBuff->m_ulBufferDataSize = ulBufferDataSize;
		pShbCirBuff->m_ulWrIndex = 0;
		pShbCirBuff->m_ulRdIndex = 0;
		pShbCirBuff->m_ulNumOfWriteJobs = 0;
		pShbCirBuff->m_ulDataInUse = 0;
		pShbCirBuff->m_ulDataApended = 0;
		pShbCirBuff->m_ulBlocksApended = 0;
		pShbCirBuff->m_ulDataReadable = 0;
		pShbCirBuff->m_ulBlocksReadable = 0;
		pShbCirBuff->m_pfnSigHndlrNewData = NULL;
		pShbCirBuff->m_fBufferLocked = FALSE;
		pShbCirBuff->m_pfnSigHndlrReset = NULL;
	} else {
		if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
			ShbError = kShbInvalidBufferType;
			goto Exit;
		}
	}

      Exit:

	*ppShbInstance_p = pShbInstance;
	*pfShbNewCreated_p = fShbNewCreated;

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Release Circular Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbCirReleaseBuffer(tShbInstance pShbInstance_p)
{

	tShbError ShbError;

	// check arguments
	if (pShbInstance_p == NULL) {
		ShbError = kShbOk;
		goto Exit;
	}

	ShbError = ShbIpcReleaseBuffer(pShbInstance_p);

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Reset Circular Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbCirResetBuffer(tShbInstance pShbInstance_p,
			    unsigned long ulTimeOut_p,
			    tShbCirSigHndlrReset pfnSignalHandlerReset_p)
{

	tShbCirBuff *pShbCirBuff;
	unsigned long ulNumOfWriteJobs = 0;	// d.k. GCC complains about uninitialized variable otherwise
	tShbError ShbError;

	// check arguments
	if (pShbInstance_p == NULL) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	ShbError = kShbOk;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	// start reset job by setting request request in buffer header
	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		if (!pShbCirBuff->m_fBufferLocked) {
			ulNumOfWriteJobs = pShbCirBuff->m_ulNumOfWriteJobs;

			pShbCirBuff->m_fBufferLocked = TRUE;
			pShbCirBuff->m_pfnSigHndlrReset =
			    pfnSignalHandlerReset_p;
		} else {
			ShbError = kShbAlreadyReseting;
		}
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

	if (ShbError != kShbOk) {
		goto Exit;
	}

	// if there is currently no running write operation then reset buffer
	// immediately, otherwise wait until the last write job is ready by
	// starting a signal process
	if (ulNumOfWriteJobs == 0) {
		// there is currently no running write operation
		// -> reset buffer immediately
		ShbCirSignalHandlerReset(pShbInstance_p, FALSE);
		ShbError = kShbOk;
	} else {
		// there is currently at least one running write operation
		// -> starting signal process to wait until the last write job is ready
		ShbError =
		    ShbIpcStartSignalingJobReady(pShbInstance_p, ulTimeOut_p,
						 ShbCirSignalHandlerReset);
	}

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Write data block to Circular Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbCirWriteDataBlock(tShbInstance pShbInstance_p,
			       const void *pSrcDataBlock_p,
			       unsigned long ulDataBlockSize_p)
{

	tShbCirBuff *pShbCirBuff;
	tShbCirBlockSize ShbCirBlockSize;
	unsigned int uiFullBlockSize;
	unsigned int uiAlignFillBytes;
	unsigned char *pShbCirDataPtr;
	unsigned char *pScrDataPtr;
	unsigned long ulDataSize;
	unsigned long ulChunkSize;
	unsigned long ulWrIndex = 0;	// d.k. GCC complains about uninitialized variable otherwise
	unsigned int fSignalNewData;
	unsigned int fSignalReset;
	tShbError ShbError;
	tShbError ShbError2;
	int fRes;

	// check arguments
	if (pShbInstance_p == NULL) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	if ((pSrcDataBlock_p == NULL) || (ulDataBlockSize_p == 0)) {
		// nothing to do here
		ShbError = kShbOk;
		goto Exit;
	}

	if (ulDataBlockSize_p > SBC_MAX_BLOCK_SIZE) {
		ShbError = kShbExceedDataSizeLimit;
		goto Exit;
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	pScrDataPtr = (unsigned char *)pSrcDataBlock_p;
	fSignalNewData = FALSE;
	fSignalReset = FALSE;
	ShbError = kShbOk;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	// calculate data block size in circular buffer
	ulDataSize =
	    (ulDataBlockSize_p +
	     (SBC_BLOCK_ALIGNMENT - 1)) & ~(SBC_BLOCK_ALIGNMENT - 1);
	uiFullBlockSize = ulDataSize + sizeof(tShbCirBlockSize);	// data size + header
	uiAlignFillBytes = ulDataSize - ulDataBlockSize_p;

	ShbCirBlockSize.m_uiFullBlockSize = uiFullBlockSize;
	ShbCirBlockSize.m_uiAlignFillBytes = uiAlignFillBytes;

	// reserve the needed memory for the write operation to do now
	// and make necessary adjustments in the circular buffer header
	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		// check if there is sufficient memory available to store
		// the new data
		fRes =
		    uiFullBlockSize <=
		    (pShbCirBuff->m_ulBufferDataSize -
		     pShbCirBuff->m_ulDataInUse);
		if (fRes) {
			// set write pointer for the write operation to do now
			// to the current write pointer of the circular buffer
			ulWrIndex = pShbCirBuff->m_ulWrIndex;

			// reserve the needed memory for the write operation to do now
			pShbCirBuff->m_ulDataInUse += uiFullBlockSize;

			// set new write pointer behind the reserved memory
			// for the write operation to do now
			pShbCirBuff->m_ulWrIndex += uiFullBlockSize;
			pShbCirBuff->m_ulWrIndex %=
			    pShbCirBuff->m_ulBufferDataSize;

			// increment number of currently (parallel running)
			// write operations
			pShbCirBuff->m_ulNumOfWriteJobs++;
		}
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

	if (!fRes) {
		ShbError = kShbBufferFull;
		goto Exit;
	}

	// copy the data to the circular buffer
	// (the copy process itself will be done outside of any
	// critical/locked section)
	pShbCirDataPtr = &pShbCirBuff->m_Data;	// ptr to start of data area

	// write real size of current block (incl. alignment fill bytes)
	*(tShbCirBlockSize *) (pShbCirDataPtr + ulWrIndex) = ShbCirBlockSize;
	ulWrIndex += sizeof(tShbCirBlockSize);
	ulWrIndex %= pShbCirBuff->m_ulBufferDataSize;

	if (ulWrIndex + ulDataBlockSize_p <= pShbCirBuff->m_ulBufferDataSize) {
		// linear write operation
		memcpy(pShbCirDataPtr + ulWrIndex, pScrDataPtr,
		       ulDataBlockSize_p);
	} else {
		// wrap-around write operation
		ulChunkSize = pShbCirBuff->m_ulBufferDataSize - ulWrIndex;
		memcpy(pShbCirDataPtr + ulWrIndex, pScrDataPtr, ulChunkSize);
		memcpy(pShbCirDataPtr, pScrDataPtr + ulChunkSize,
		       ulDataBlockSize_p - ulChunkSize);
	}

	// adjust header information for circular buffer with properties
	// of the wiritten data block
	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		pShbCirBuff->m_ulDataApended += uiFullBlockSize;
		pShbCirBuff->m_ulBlocksApended++;

		// decrement number of currently (parallel running) write operations
		if (!--pShbCirBuff->m_ulNumOfWriteJobs) {
			// if there is no other write process running then
			// set new size of readable (complete written) data and
			// adjust number of readable blocks
			pShbCirBuff->m_ulDataReadable +=
			    pShbCirBuff->m_ulDataApended;
			pShbCirBuff->m_ulBlocksReadable +=
			    pShbCirBuff->m_ulBlocksApended;

			pShbCirBuff->m_ulDataApended = 0;
			pShbCirBuff->m_ulBlocksApended = 0;

			fSignalNewData = TRUE;
			fSignalReset = pShbCirBuff->m_fBufferLocked;
		}
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

	// signal new data event to a potentially reading application
	if (fSignalNewData) {
		ShbError2 = ShbIpcSignalNewData(pShbInstance_p);
		if (ShbError == kShbOk) {
			ShbError = ShbError2;
		}
	}
	// signal that the last write job has been finished to allow
	// a waiting application to reset the buffer now
	if (fSignalReset) {
		ShbError2 = ShbIpcSignalJobReady(pShbInstance_p);
		if (ShbError == kShbOk) {
			ShbError = ShbError2;
		}
	}

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Allocate block within the Circular Shared Buffer for chunk writing
//---------------------------------------------------------------------------

tShbError ShbCirAllocDataBlock(tShbInstance pShbInstance_p,
			       tShbCirChunk * pShbCirChunk_p,
			       unsigned long ulDataBufferSize_p)
{

	tShbCirBuff *pShbCirBuff;
	tShbCirBlockSize ShbCirBlockSize;
	unsigned int uiFullBlockSize;
	unsigned int uiAlignFillBytes;
	unsigned char *pShbCirDataPtr;
	unsigned long ulDataSize;
	unsigned long ulWrIndex = 0;	// d.k. GCC complains about uninitialized variable otherwise
	tShbError ShbError;
	int fRes;

	// check arguments
	if ((pShbInstance_p == NULL) || (pShbCirChunk_p == NULL)) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	if (ulDataBufferSize_p == 0) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	if (ulDataBufferSize_p > SBC_MAX_BLOCK_SIZE) {
		ShbError = kShbExceedDataSizeLimit;
		goto Exit;
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	ShbError = kShbOk;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	// calculate data block size in circular buffer
	ulDataSize =
	    (ulDataBufferSize_p +
	     (SBC_BLOCK_ALIGNMENT - 1)) & ~(SBC_BLOCK_ALIGNMENT - 1);
	uiFullBlockSize = ulDataSize + sizeof(tShbCirBlockSize);	// data size + header
	uiAlignFillBytes = ulDataSize - ulDataBufferSize_p;

	ShbCirBlockSize.m_uiFullBlockSize = uiFullBlockSize;
	ShbCirBlockSize.m_uiAlignFillBytes = uiAlignFillBytes;

	// reserve the needed memory for the write operation to do now
	// and make necessary adjustments in the circular buffer header
	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		// check if there is sufficient memory available to store
		// the new data
		fRes =
		    (uiFullBlockSize <=
		     (pShbCirBuff->m_ulBufferDataSize -
		      pShbCirBuff->m_ulDataInUse));
		if (fRes) {
			// set write pointer for the write operation to do now
			// to the current write pointer of the circular buffer
			ulWrIndex = pShbCirBuff->m_ulWrIndex;

			// reserve the needed memory for the write operation to do now
			pShbCirBuff->m_ulDataInUse += uiFullBlockSize;

			// set new write pointer behind the reserved memory
			// for the write operation to do now
			pShbCirBuff->m_ulWrIndex += uiFullBlockSize;
			pShbCirBuff->m_ulWrIndex %=
			    pShbCirBuff->m_ulBufferDataSize;

			// increment number of currently (parallel running)
			// write operations
			pShbCirBuff->m_ulNumOfWriteJobs++;
		}
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

	if (!fRes) {
		ShbError = kShbBufferFull;
		goto Exit;
	}

	// setup header information for allocated buffer
	pShbCirDataPtr = &pShbCirBuff->m_Data;	// ptr to start of data area

	// write real size of current block (incl. alignment fill bytes)
	*(tShbCirBlockSize *) (pShbCirDataPtr + ulWrIndex) = ShbCirBlockSize;
	ulWrIndex += sizeof(tShbCirBlockSize);
	ulWrIndex %= pShbCirBuff->m_ulBufferDataSize;

	// setup chunk descriptor
	pShbCirChunk_p->m_uiFullBlockSize = uiFullBlockSize;
	pShbCirChunk_p->m_ulAvailableSize = ulDataBufferSize_p;
	pShbCirChunk_p->m_ulWrIndex = ulWrIndex;
	pShbCirChunk_p->m_fBufferCompleted = FALSE;

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Write data chunk into an allocated buffer of the Circular Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbCirWriteDataChunk(tShbInstance pShbInstance_p,
			       tShbCirChunk *pShbCirChunk_p,
			       const void *pSrcDataChunk_p,
			       unsigned long ulDataChunkSize_p,
			       unsigned int *pfBufferCompleted_p)
{

	tShbCirBuff *pShbCirBuff;
	unsigned char *pShbCirDataPtr;
	unsigned char *pScrDataPtr;
	unsigned long ulSubChunkSize;
	unsigned long ulWrIndex;
	unsigned int fBufferCompleted;
	unsigned int fSignalNewData;
	unsigned int fSignalReset;
	tShbError ShbError;
	tShbError ShbError2;

	// check arguments
	if ((pShbInstance_p == NULL) || (pShbCirChunk_p == NULL)
	    || (pfBufferCompleted_p == NULL)) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	if ((pSrcDataChunk_p == NULL) || (ulDataChunkSize_p == 0)) {
		// nothing to do here
		ShbError = kShbOk;
		goto Exit;
	}

	if (pShbCirChunk_p->m_fBufferCompleted) {
		ShbError = kShbBufferAlreadyCompleted;
		goto Exit;
	}

	if (ulDataChunkSize_p > pShbCirChunk_p->m_ulAvailableSize) {
		ShbError = kShbExceedDataSizeLimit;
		goto Exit;
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	pScrDataPtr = (unsigned char *)pSrcDataChunk_p;
	fSignalNewData = FALSE;
	fSignalReset = FALSE;
	ShbError = kShbOk;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	ulWrIndex = pShbCirChunk_p->m_ulWrIndex;

	// copy the data to the circular buffer
	// (the copy process itself will be done outside of any
	// critical/locked section)
	pShbCirDataPtr = &pShbCirBuff->m_Data;	// ptr to start of data area

	if (ulWrIndex + ulDataChunkSize_p <= pShbCirBuff->m_ulBufferDataSize) {
		// linear write operation
		memcpy(pShbCirDataPtr + ulWrIndex, pScrDataPtr,
		       ulDataChunkSize_p);
	} else {
		// wrap-around write operation
		ulSubChunkSize = pShbCirBuff->m_ulBufferDataSize - ulWrIndex;
		memcpy(pShbCirDataPtr + ulWrIndex, pScrDataPtr, ulSubChunkSize);
		memcpy(pShbCirDataPtr, pScrDataPtr + ulSubChunkSize,
		       ulDataChunkSize_p - ulSubChunkSize);
	}

	// adjust chunk descriptor
	ulWrIndex += ulDataChunkSize_p;
	ulWrIndex %= pShbCirBuff->m_ulBufferDataSize;

	pShbCirChunk_p->m_ulAvailableSize -= ulDataChunkSize_p;
	pShbCirChunk_p->m_ulWrIndex = ulWrIndex;

	fBufferCompleted = (pShbCirChunk_p->m_ulAvailableSize == 0);
	pShbCirChunk_p->m_fBufferCompleted = fBufferCompleted;

	// if the complete allocated buffer is filled with data then
	// adjust header information for circular buffer with properties
	// of the wiritten data block
	if (fBufferCompleted) {
		ShbIpcEnterAtomicSection(pShbInstance_p);
		{
			pShbCirBuff->m_ulDataApended +=
			    pShbCirChunk_p->m_uiFullBlockSize;
			pShbCirBuff->m_ulBlocksApended++;

			// decrement number of currently (parallel running) write operations
			if (!--pShbCirBuff->m_ulNumOfWriteJobs) {
				// if there is no other write process running then
				// set new size of readable (complete written) data and
				// adjust number of readable blocks
				pShbCirBuff->m_ulDataReadable +=
				    pShbCirBuff->m_ulDataApended;
				pShbCirBuff->m_ulBlocksReadable +=
				    pShbCirBuff->m_ulBlocksApended;

				pShbCirBuff->m_ulDataApended = 0;
				pShbCirBuff->m_ulBlocksApended = 0;

				fSignalNewData = TRUE;
				fSignalReset = pShbCirBuff->m_fBufferLocked;
			}
		}
		ShbIpcLeaveAtomicSection(pShbInstance_p);
	}

	// signal new data event to a potentially reading application
	if (fSignalNewData) {
		ShbError2 = ShbIpcSignalNewData(pShbInstance_p);
		if (ShbError == kShbOk) {
			ShbError = ShbError2;
		}
	}
	// signal that the last write job has been finished to allow
	// a waiting application to reset the buffer now
	if (fSignalReset) {
		ShbError2 = ShbIpcSignalJobReady(pShbInstance_p);
		if (ShbError == kShbOk) {
			ShbError = ShbError2;
		}
	}

	*pfBufferCompleted_p = fBufferCompleted;

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Read data block from Circular Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbCirReadDataBlock(tShbInstance pShbInstance_p,
			      void *pDstDataBlock_p,
			      unsigned long ulRdBuffSize_p,
			      unsigned long *pulDataBlockSize_p)
{

	tShbCirBuff *pShbCirBuff;
	tShbCirBlockSize ShbCirBlockSize;
	unsigned long ulDataReadable;
	unsigned char *pShbCirDataPtr;
	unsigned char *pDstDataPtr;
	unsigned long ulDataSize = 0;	// d.k. GCC complains about uninitialized variable otherwise
	unsigned long ulChunkSize;
	unsigned long ulRdIndex;
	tShbError ShbError;

	// check arguments
	if ((pShbInstance_p == NULL) || (pulDataBlockSize_p == NULL)) {
		return (kShbInvalidArg);
	}

	if ((pDstDataBlock_p == NULL) || (ulRdBuffSize_p == 0)) {
		// nothing to do here
		ShbError = kShbOk;
		goto Exit;
	}

	ShbError = kShbOk;
	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	pDstDataPtr = (unsigned char *)pDstDataBlock_p;
	ulDataSize = 0;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	// get total number of readable bytes for the whole circular buffer
	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		ulDataReadable = pShbCirBuff->m_ulDataReadable;
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

	// if there are readable data available, then there must be at least
	// one complete readable data block
	if (ulDataReadable > 0) {
		// get pointer to start of data area and current read index
		pShbCirDataPtr = &pShbCirBuff->m_Data;	// ptr to start of data area
		ulRdIndex = pShbCirBuff->m_ulRdIndex;

		// get real size of current block (incl. alignment fill bytes)
		ShbCirBlockSize =
		    *(tShbCirBlockSize *) (pShbCirDataPtr + ulRdIndex);
		ulRdIndex += sizeof(tShbCirBlockSize);
		ulRdIndex %= pShbCirBuff->m_ulBufferDataSize;

		// get size of user data inside the current block
		ulDataSize =
		    ShbCirBlockSize.m_uiFullBlockSize -
		    ShbCirBlockSize.m_uiAlignFillBytes;
		ulDataSize -= sizeof(tShbCirBlockSize);
	}

	// ulDataSize = MIN(ulDataSize, ulRdBuffSize_p);
	if (ulDataSize > ulRdBuffSize_p) {
		ulDataSize = ulRdBuffSize_p;
		ShbError = kShbDataTruncated;
	}

	if (ulDataSize == 0) {
		// nothing to do here
		ShbError = kShbNoReadableData;
		goto Exit;
	}

	// copy the data from the circular buffer
	// (the copy process itself will be done outside of any
	// critical/locked section)
	if (ulRdIndex + ulDataSize <= pShbCirBuff->m_ulBufferDataSize) {
		// linear read operation
		memcpy(pDstDataPtr, pShbCirDataPtr + ulRdIndex, ulDataSize);
	} else {
		// wrap-around read operation
		ulChunkSize = pShbCirBuff->m_ulBufferDataSize - ulRdIndex;
		memcpy(pDstDataPtr, pShbCirDataPtr + ulRdIndex, ulChunkSize);
		memcpy(pDstDataPtr + ulChunkSize, pShbCirDataPtr,
		       ulDataSize - ulChunkSize);
	}

#ifndef NDEBUG
	{
		tShbCirBlockSize ClrShbCirBlockSize;

		if (ulRdIndex + ulDataSize <= pShbCirBuff->m_ulBufferDataSize) {
			// linear buffer
			memset(pShbCirDataPtr + ulRdIndex, 0xDD, ulDataSize);
		} else {
			// wrap-around read operation
			ulChunkSize =
			    pShbCirBuff->m_ulBufferDataSize - ulRdIndex;
			memset(pShbCirDataPtr + ulRdIndex, 0xDD, ulChunkSize);
			memset(pShbCirDataPtr, 0xDD, ulDataSize - ulChunkSize);
		}

		ClrShbCirBlockSize.m_uiFullBlockSize = /*(unsigned int) */ -1;	// -1 = xFFFFFFF
		ClrShbCirBlockSize.m_uiAlignFillBytes = /*(unsigned int) */ -1;	// -1 = Fxxxxxxx
		*(tShbCirBlockSize *) (pShbCirDataPtr +
				       pShbCirBuff->m_ulRdIndex) =
		    ClrShbCirBlockSize;
	}
#endif // #ifndef NDEBUG

	// set new size of readable data, data in use, new read index
	// and adjust number of readable blocks
	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		pShbCirBuff->m_ulDataInUse -= ShbCirBlockSize.m_uiFullBlockSize;
		pShbCirBuff->m_ulDataReadable -=
		    ShbCirBlockSize.m_uiFullBlockSize;
		pShbCirBuff->m_ulBlocksReadable--;

		//$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
		if ((pShbCirBuff->m_ulDataInUse == 0)
		    && (pShbCirBuff->m_ulDataReadable == 0)) {
			ASSERT(pShbCirBuff->m_ulBlocksReadable == 0);

			pShbCirBuff->m_ulWrIndex = 0;
			pShbCirBuff->m_ulRdIndex = 0;
		} else
			//$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
		{
			pShbCirBuff->m_ulRdIndex +=
			    ShbCirBlockSize.m_uiFullBlockSize;
			pShbCirBuff->m_ulRdIndex %=
			    pShbCirBuff->m_ulBufferDataSize;
		}
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

      Exit:

	*pulDataBlockSize_p = ulDataSize;

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Get data size of next readable block from Circular Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbCirGetReadDataSize(tShbInstance pShbInstance_p,
				unsigned long *pulDataBlockSize_p)
{

	tShbCirBuff *pShbCirBuff;
	unsigned long ulDataReadable;
	unsigned char *pShbCirDataPtr;
	tShbCirBlockSize ShbCirBlockSize;
	unsigned long ulDataSize;
	tShbError ShbError;

	// check arguments
	if ((pShbInstance_p == NULL) || (pulDataBlockSize_p == NULL)) {
		return (kShbInvalidArg);
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	ulDataSize = 0;
	ShbError = kShbOk;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	// get total number of readable bytes for the whole circular buffer
	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		ulDataReadable = pShbCirBuff->m_ulDataReadable;
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

	// if there are readable data available, then there must be at least
	// one complete readable data block
	if (ulDataReadable > 0) {
		pShbCirDataPtr =
		    &pShbCirBuff->m_Data + pShbCirBuff->m_ulRdIndex;

		// get real size of current block (incl. alignment fill bytes)
		ShbCirBlockSize = *(tShbCirBlockSize *) pShbCirDataPtr;

		// get size of user data inside the current block
		ulDataSize =
		    ShbCirBlockSize.m_uiFullBlockSize -
		    ShbCirBlockSize.m_uiAlignFillBytes;
		ulDataSize -= sizeof(tShbCirBlockSize);
	}

      Exit:

	*pulDataBlockSize_p = ulDataSize;

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Get number of readable blocks from Circular Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbCirGetReadBlockCount(tShbInstance pShbInstance_p,
				  unsigned long *pulDataBlockCount_p)
{

	tShbCirBuff *pShbCirBuff;
	unsigned long ulBlockCount;
	tShbError ShbError;

	// check arguments
	if ((pShbInstance_p == NULL) || (pulDataBlockCount_p == NULL)) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	ulBlockCount = 0;
	ShbError = kShbOk;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		ulBlockCount = pShbCirBuff->m_ulBlocksReadable;
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

	*pulDataBlockCount_p = ulBlockCount;

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Set application handler to signal new data for Circular Shared Buffer
//  d.k.: new parameter priority as enum
//---------------------------------------------------------------------------

tShbError ShbCirSetSignalHandlerNewData(tShbInstance pShbInstance_p,
					tShbCirSigHndlrNewData pfnSignalHandlerNewData_p,
					tShbPriority ShbPriority_p)
{

	tShbCirBuff *pShbCirBuff;
	tShbError ShbError;

	// check arguments
	if (pShbInstance_p == NULL) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	ShbError = kShbOk;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	if (pfnSignalHandlerNewData_p != NULL) {
		// set a new signal handler
		if (pShbCirBuff->m_pfnSigHndlrNewData != NULL) {
			ShbError = kShbAlreadySignaling;
			goto Exit;
		}

		pShbCirBuff->m_pfnSigHndlrNewData = pfnSignalHandlerNewData_p;
		ShbError =
		    ShbIpcStartSignalingNewData(pShbInstance_p,
						ShbCirSignalHandlerNewData,
						ShbPriority_p);
	} else {
		// remove existing signal handler
		ShbError = ShbIpcStopSignalingNewData(pShbInstance_p);
		if (pShbCirBuff->m_pfnSigHndlrNewData != NULL) {
			pShbCirBuff->m_pfnSigHndlrNewData(pShbInstance_p, 0);
		}
		pShbCirBuff->m_pfnSigHndlrNewData = NULL;
	}

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  DEBUG: Trace Circular Shared Buffer
//---------------------------------------------------------------------------

#ifndef NDEBUG
tShbError ShbCirTraceBuffer(tShbInstance pShbInstance_p)
{

	tShbCirBuff *pShbCirBuff;
	char szMagigID[sizeof(SBC_MAGIC_ID) + 1];
	tShbCirBlockSize ShbCirBlockSize;
	unsigned long ulDataReadable;
	unsigned char *pShbCirDataPtr;
	unsigned long ulBlockIndex;
	unsigned int nBlockCount;
	unsigned long ulDataSize;
	unsigned long ulChunkSize;
	unsigned long ulRdIndex;
	tShbError ShbError;

	TRACE0("\n\n##### Circular Shared Buffer #####\n");

	// check arguments
	if (pShbInstance_p == NULL) {
		TRACE1("\nERROR: invalid buffer address (0x%08lX)\n",
		       (unsigned long)pShbInstance_p);
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	ShbError = kShbOk;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	*(unsigned long *)&szMagigID[0] = pShbCirBuff->m_ShbCirMagicID;
	szMagigID[sizeof(SBC_MAGIC_ID)] = '\0';

	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		TRACE1("\nBuffer Address:   0x%08lX\n",
		       (unsigned long)pShbCirBuff);

		TRACE0("\nHeader Info:");
		TRACE2("\nMagigID:          '%s' (%08lX)", szMagigID,
		       pShbCirBuff->m_ShbCirMagicID);
		TRACE1("\nBufferTotalSize:  %4lu [Bytes]",
		       pShbCirBuff->m_ulBufferTotalSize);
		TRACE1("\nBufferDataSize:   %4lu [Bytes]",
		       pShbCirBuff->m_ulBufferDataSize);
		TRACE1("\nWrIndex:          %4lu", pShbCirBuff->m_ulWrIndex);
		TRACE1("\nRdIndex:          %4lu", pShbCirBuff->m_ulRdIndex);
		TRACE1("\nNumOfWriteJobs:   %4lu",
		       pShbCirBuff->m_ulNumOfWriteJobs);
		TRACE1("\nDataInUse:        %4lu [Bytes]",
		       pShbCirBuff->m_ulDataInUse);
		TRACE1("\nDataApended:      %4lu [Bytes]",
		       pShbCirBuff->m_ulDataApended);
		TRACE1("\nBlocksApended:    %4lu",
		       pShbCirBuff->m_ulBlocksApended);
		TRACE1("\nDataReadable:     %4lu [Bytes]",
		       pShbCirBuff->m_ulDataReadable);
		TRACE1("\nBlocksReadable:   %4lu",
		       pShbCirBuff->m_ulBlocksReadable);
		TRACE1("\nSigHndlrNewData:  %08lX",
		       (unsigned long)pShbCirBuff->m_pfnSigHndlrNewData);
		TRACE1("\nBufferLocked:     %d", pShbCirBuff->m_fBufferLocked);
		TRACE1("\nSigHndlrReset:    %08lX",
		       (unsigned long)pShbCirBuff->m_pfnSigHndlrReset);

		ShbTraceDump(&pShbCirBuff->m_Data,
			     pShbCirBuff->m_ulBufferDataSize, 0x00000000L,
			     "\nData Area:");

		ulDataReadable = pShbCirBuff->m_ulDataReadable;
		nBlockCount = 1;
		ulBlockIndex = pShbCirBuff->m_ulRdIndex;

		while (ulDataReadable > 0) {
			TRACE1("\n\n--- Block #%u ---", nBlockCount);

			// get pointer to start of data area and current read index
			pShbCirDataPtr = &pShbCirBuff->m_Data;	// ptr to start of data area
			ulRdIndex = ulBlockIndex;

			// get real size of current block (incl. alignment fill bytes)
			ShbCirBlockSize =
			    *(tShbCirBlockSize *) (pShbCirDataPtr + ulRdIndex);
			ulRdIndex += sizeof(tShbCirBlockSize);
			ulRdIndex %= pShbCirBuff->m_ulBufferDataSize;

			// get size of user data inside the current block
			ulDataSize =
			    ShbCirBlockSize.m_uiFullBlockSize -
			    ShbCirBlockSize.m_uiAlignFillBytes;
			ulDataSize -= sizeof(tShbCirBlockSize);

			TRACE1
			    ("\nFull Data Size:       %4u [Bytes] (incl. header and alignment fill bytes)",
			     ShbCirBlockSize.m_uiFullBlockSize);
			TRACE1("\nUser Data Size:       %4lu [Bytes]",
			       ulDataSize);
			TRACE1("\nAlignment Fill Bytes: %4u [Bytes]",
			       ShbCirBlockSize.m_uiAlignFillBytes);

			if (ulRdIndex + ulDataSize <=
			    pShbCirBuff->m_ulBufferDataSize) {
				// linear data buffer
				ShbTraceDump(pShbCirDataPtr + ulRdIndex,
					     ulDataSize, 0x00000000L, NULL);
			} else {
				// wrap-around data buffer
				ulChunkSize =
				    pShbCirBuff->m_ulBufferDataSize - ulRdIndex;
				ShbTraceDump(pShbCirDataPtr + ulRdIndex,
					     ulChunkSize, 0x00000000L, NULL);
				ShbTraceDump(pShbCirDataPtr,
					     ulDataSize - ulChunkSize,
					     ulChunkSize, NULL);
			}

			nBlockCount++;

			ulBlockIndex += ShbCirBlockSize.m_uiFullBlockSize;
			ulBlockIndex %= pShbCirBuff->m_ulBufferDataSize;

			ulDataReadable -= ShbCirBlockSize.m_uiFullBlockSize;
		}

		ASSERT(pShbCirBuff->m_ulBlocksReadable == nBlockCount - 1);
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

      Exit:

	return (ShbError);

}
#endif

//-------------------------------------------------------------------------//
//                                                                         //
//          L i n e a r   S h a r e d   B u f f e r                        //
//                                                                         //
//-------------------------------------------------------------------------//

//---------------------------------------------------------------------------
//  Allocate Linear Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbLinAllocBuffer(unsigned long ulBufferSize_p,
			    const char *pszBufferID_p,
			    tShbInstance * ppShbInstance_p,
			    unsigned int *pfShbNewCreated_p)
{

	tShbInstance pShbInstance;
	tShbLinBuff *pShbLinBuff;
	unsigned int fShbNewCreated;
	unsigned long ulBufferDataSize;
	unsigned long ulBufferTotalSize;
	tShbError ShbError;

	// check arguments
	if ((ulBufferSize_p == 0) || (ppShbInstance_p == NULL)) {
		return (kShbInvalidArg);
	}

	// calculate length of memory to allocate
	ulBufferDataSize =
	    (ulBufferSize_p +
	     (SBL_BLOCK_ALIGNMENT - 1)) & ~(SBL_BLOCK_ALIGNMENT - 1);
	ulBufferTotalSize = ulBufferDataSize + sizeof(tShbLinBuff);

	// allocate a new or open an existing shared buffer
	ShbError = ShbIpcAllocBuffer(ulBufferTotalSize, pszBufferID_p,
				     &pShbInstance, &fShbNewCreated);
	if (ShbError != kShbOk) {
		goto Exit;
	}

	if (pShbInstance == NULL) {
		ShbError = kShbOutOfMem;
		goto Exit;
	}

	// get pointer to shared buffer
	pShbLinBuff = (tShbLinBuff *) ShbIpcGetShMemPtr(pShbInstance);

	// if the shared buffer was new created, than this process has
	// to initialize it, otherwise the buffer is already in use
	// and *must not* be reseted
	if (fShbNewCreated) {
#ifndef NDEBUG
		{
			memset(pShbLinBuff, 0xCC, ulBufferTotalSize);
		}
#endif

		pShbLinBuff->m_ShbLinMagicID = SBL_MAGIC_ID;
		pShbLinBuff->m_ulBufferTotalSize = ulBufferTotalSize;
		pShbLinBuff->m_ulBufferDataSize = ulBufferDataSize;
	} else {
		if (pShbLinBuff->m_ShbLinMagicID != SBL_MAGIC_ID) {
			ShbError = kShbInvalidBufferType;
			goto Exit;
		}
	}

      Exit:

	*ppShbInstance_p = pShbInstance;
	*pfShbNewCreated_p = fShbNewCreated;

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Release Linear Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbLinReleaseBuffer(tShbInstance pShbInstance_p)
{

	tShbError ShbError;

	// check arguments
	if (pShbInstance_p == NULL) {
		ShbError = kShbOk;
		goto Exit;
	}

	ShbError = ShbIpcReleaseBuffer(pShbInstance_p);

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Write data block to Linear Shared Buffer
//---------------------------------------------------------------------------
tShbError ShbLinWriteDataBlock(tShbInstance pShbInstance_p,
			       unsigned long ulDstBufferOffs_p,
			       const void *pSrcDataBlock_p,
			       unsigned long ulDataBlockSize_p)
{

	tShbLinBuff *pShbLinBuff;
	unsigned char *pShbLinDataPtr;
	unsigned char *pScrDataPtr;
	unsigned long ulBufferDataSize;
	tShbError ShbError;

	// check arguments
	if (pShbInstance_p == NULL) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	if ((pSrcDataBlock_p == NULL) || (ulDataBlockSize_p == 0)) {
		// nothing to do here
		ShbError = kShbOk;
		goto Exit;
	}

	if (ulDataBlockSize_p > SBL_MAX_BLOCK_SIZE) {
		ShbError = kShbExceedDataSizeLimit;
		goto Exit;
	}

	pShbLinBuff = ShbLinGetBuffer(pShbInstance_p);
	pScrDataPtr = (unsigned char *)pSrcDataBlock_p;
	ShbError = kShbOk;

	if (pShbLinBuff->m_ShbLinMagicID != SBL_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	// check if offeset and size for the write operation matches with
	// the size of the shared buffer
	ulBufferDataSize = pShbLinBuff->m_ulBufferDataSize;
	if ((ulDstBufferOffs_p > ulBufferDataSize) ||
	    (ulDataBlockSize_p > ulBufferDataSize) ||
	    ((ulDstBufferOffs_p + ulDataBlockSize_p) > ulBufferDataSize)) {
		ShbError = kShbDataOutsideBufferArea;
		goto Exit;
	}

	// copy the data to the linear buffer
	// (the copy process will be done inside of any critical/locked section)
	pShbLinDataPtr = &pShbLinBuff->m_Data;	// ptr to start of data area
	pShbLinDataPtr += ulDstBufferOffs_p;

	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		memcpy(pShbLinDataPtr, pScrDataPtr, ulDataBlockSize_p);
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Read data block from Linear Shared Buffer
//---------------------------------------------------------------------------
tShbError ShbLinReadDataBlock(tShbInstance pShbInstance_p,
			      void *pDstDataBlock_p,
			      unsigned long ulSrcBufferOffs_p,
			      unsigned long ulDataBlockSize_p)
{

	tShbLinBuff *pShbLinBuff;
	unsigned char *pShbLinDataPtr;
	unsigned char *pDstDataPtr;
	unsigned long ulBufferDataSize;
	tShbError ShbError;

	// check arguments
	if (pShbInstance_p == NULL) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	if ((pDstDataBlock_p == NULL) || (ulDataBlockSize_p == 0)) {
		// nothing to do here
		ShbError = kShbOk;
		goto Exit;
	}

	if (ulDataBlockSize_p > SBL_MAX_BLOCK_SIZE) {
		ShbError = kShbExceedDataSizeLimit;
		goto Exit;
	}

	pShbLinBuff = ShbLinGetBuffer(pShbInstance_p);
	pDstDataPtr = (unsigned char *)pDstDataBlock_p;
	ShbError = kShbOk;

	if (pShbLinBuff->m_ShbLinMagicID != SBL_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	// check if offeset and size for the read operation matches with
	// the size of the shared buffer
	ulBufferDataSize = pShbLinBuff->m_ulBufferDataSize;
	if ((ulSrcBufferOffs_p > ulBufferDataSize) ||
	    (ulDataBlockSize_p > ulBufferDataSize) ||
	    ((ulSrcBufferOffs_p + ulDataBlockSize_p) > ulBufferDataSize)) {
		ShbError = kShbDataOutsideBufferArea;
		goto Exit;
	}

	// copy the data to the linear buffer
	// (the copy process will be done inside of any critical/locked section)
	pShbLinDataPtr = &pShbLinBuff->m_Data;	// ptr to start of data area
	pShbLinDataPtr += ulSrcBufferOffs_p;

	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		memcpy(pDstDataPtr, pShbLinDataPtr, ulDataBlockSize_p);
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  DEBUG: Trace Linear Shared Buffer
//---------------------------------------------------------------------------

#ifndef NDEBUG
tShbError ShbLinTraceBuffer(tShbInstance pShbInstance_p)
{

	tShbLinBuff *pShbLinBuff;
	char szMagigID[sizeof(SBL_MAGIC_ID) + 1];
	tShbError ShbError;

	TRACE0("\n\n##### Linear Shared Buffer #####\n");

	// check arguments
	if (pShbInstance_p == NULL) {
		TRACE1("\nERROR: invalid buffer address (0x%08lX)\n",
		       (unsigned long)pShbInstance_p);
		ShbError = kShbInvalidArg;
		goto Exit;
	}

	pShbLinBuff = ShbLinGetBuffer(pShbInstance_p);
	ShbError = kShbOk;

	if (pShbLinBuff->m_ShbLinMagicID != SBL_MAGIC_ID) {
		ShbError = kShbInvalidBufferType;
		goto Exit;
	}

	*(unsigned int *)&szMagigID[0] = pShbLinBuff->m_ShbLinMagicID;
	szMagigID[sizeof(SBL_MAGIC_ID)] = '\0';

	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		TRACE1("\nBuffer Address:   0x%08lX\n",
		       (unsigned long)pShbLinBuff);

		TRACE0("\nHeader Info:");
		TRACE2("\nMagigID:          '%s' (%08X)", szMagigID,
		       pShbLinBuff->m_ShbLinMagicID);
		TRACE1("\nBufferTotalSize:  %4lu [Bytes]",
		       pShbLinBuff->m_ulBufferTotalSize);
		TRACE1("\nBufferDataSize:   %4lu [Bytes]",
		       pShbLinBuff->m_ulBufferDataSize);

		ShbTraceDump(&pShbLinBuff->m_Data,
			     pShbLinBuff->m_ulBufferDataSize, 0x00000000L,
			     "\nData Area:");
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

      Exit:

	return (ShbError);

}
#endif

//---------------------------------------------------------------------------
//  Dump buffer contents
//---------------------------------------------------------------------------

#ifndef NDEBUG
tShbError ShbTraceDump(const unsigned char *pabStartAddr_p,
		       unsigned long ulDataSize_p,
		       unsigned long ulAddrOffset_p, const char *pszInfoText_p)
{

	const unsigned char *pabBuffData;
	unsigned long ulBuffSize;
	unsigned char bData;
	int nRow;
	int nCol;

	// get pointer to buffer and length of buffer
	pabBuffData = pabStartAddr_p;
	ulBuffSize = ulDataSize_p;

	if (pszInfoText_p != NULL) {
		TRACE1("%s", pszInfoText_p);
	}
	// dump buffer contents
	for (nRow = 0;; nRow++) {
		TRACE1("\n%08lX:   ",
		       (unsigned long)(nRow * 0x10) + ulAddrOffset_p);

		for (nCol = 0; nCol < 16; nCol++) {
			if ((unsigned long)nCol < ulBuffSize) {
				TRACE1("%02X ",
				       (unsigned int)*(pabBuffData + nCol));
			} else {
				TRACE0("   ");
			}
		}

		TRACE0(" ");

		for (nCol = 0; nCol < 16; nCol++) {
			bData = *pabBuffData++;
			if ((unsigned long)nCol < ulBuffSize) {
				if ((bData >= 0x20) && (bData < 0x7F)) {
					TRACE1("%c", bData);
				} else {
					TRACE0(".");
				}
			} else {
				TRACE0(" ");
			}
		}

		if (ulBuffSize > 16) {
			ulBuffSize -= 16;
		} else {
			break;
		}
	}

	return (kShbOk);

}
#endif // #ifndef NDEBUG

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  Handler to signal new data event for Circular Shared Buffer
//---------------------------------------------------------------------------

int ShbCirSignalHandlerNewData(tShbInstance pShbInstance_p)
{

	tShbCirBuff *pShbCirBuff;
	unsigned long ulDataSize;
	unsigned long ulBlockCount;
	tShbError ShbError;

	// check arguments
	if (pShbInstance_p == NULL) {
		return FALSE;
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	ShbError = kShbOk;

	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		return FALSE;
	}

	// call application handler
	if (pShbCirBuff->m_pfnSigHndlrNewData != NULL) {
/*        do
        {*/
		ShbError = ShbCirGetReadDataSize(pShbInstance_p, &ulDataSize);
		if ((ulDataSize > 0) && (ShbError == kShbOk)) {
			pShbCirBuff->m_pfnSigHndlrNewData(pShbInstance_p,
							  ulDataSize);
		}

		ShbError =
		    ShbCirGetReadBlockCount(pShbInstance_p, &ulBlockCount);
/*        }
        while ((ulBlockCount > 0) && (ShbError == kShbOk));*/
	}
	// Return TRUE if there are pending blocks.
	// In that case ShbIpc tries to call this function again immediately if there
	// is no other filled shared buffer with higher priority.
	return ((ulBlockCount > 0) && (ShbError == kShbOk));

}

//---------------------------------------------------------------------------
//  Handler to reset Circular Shared Buffer
//---------------------------------------------------------------------------

void ShbCirSignalHandlerReset(tShbInstance pShbInstance_p,
			      unsigned int fTimeOut_p)
{

	tShbCirBuff *pShbCirBuff;

	// check arguments
	if (pShbInstance_p == NULL) {
		return;
	}

	pShbCirBuff = ShbCirGetBuffer(pShbInstance_p);
	if (pShbCirBuff->m_ShbCirMagicID != SBC_MAGIC_ID) {
		return;
	}

	// reset buffer header
	if (!fTimeOut_p) {
		ShbIpcEnterAtomicSection(pShbInstance_p);
		{
			pShbCirBuff->m_ulWrIndex = 0;
			pShbCirBuff->m_ulRdIndex = 0;
			pShbCirBuff->m_ulNumOfWriteJobs = 0;
			pShbCirBuff->m_ulDataInUse = 0;
			pShbCirBuff->m_ulDataApended = 0;
			pShbCirBuff->m_ulBlocksApended = 0;
			pShbCirBuff->m_ulDataReadable = 0;
			pShbCirBuff->m_ulBlocksReadable = 0;
		}
		ShbIpcLeaveAtomicSection(pShbInstance_p);

#ifndef NDEBUG
		{
			memset(&pShbCirBuff->m_Data, 0xCC,
			       pShbCirBuff->m_ulBufferDataSize);
		}
#endif
	}

	// call application handler
	if (pShbCirBuff->m_pfnSigHndlrReset != NULL) {
		pShbCirBuff->m_pfnSigHndlrReset(pShbInstance_p, fTimeOut_p);
	}

	// unlock buffer
	ShbIpcEnterAtomicSection(pShbInstance_p);
	{
		pShbCirBuff->m_fBufferLocked = FALSE;
		pShbCirBuff->m_pfnSigHndlrReset = NULL;
	}
	ShbIpcLeaveAtomicSection(pShbInstance_p);

	return;

}

// EOF
