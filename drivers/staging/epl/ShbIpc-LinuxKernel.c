/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      Project independend shared buffer (linear + circular)

  Description:  Implementation of platform specific part for the
                shared buffer
                (Implementation for Linux KernelSpace)

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

  2006/06/28 -rs:   V 1.00 (initial version)

****************************************************************************/

#include "global.h"
#include "SharedBuff.h"
#include "ShbIpc.h"
#include "ShbLinuxKernel.h"
#include "Debug.h"

#include <linux/string.h>
#include <linux/module.h>
#include <asm/processor.h>
//#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/completion.h>

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

#define MAX_LEN_BUFFER_ID       256

#define TIMEOUT_ENTER_ATOMIC    1000	// (ms) for debgging: INFINITE
#define TIMEOUT_TERM_THREAD     1000
#define INFINITE                3600

#define SBI_MAGIC_ID            0x5342492B	// magic ID ("SBI+")
#define SBH_MAGIC_ID            0x5342482A	// magic ID ("SBH*")

#define INVALID_ID              -1

#define TABLE_SIZE              10

//---------------------------------------------------------------------------
//  Local types
//---------------------------------------------------------------------------

// This structure is the common header for the shared memory region used
// by all processes attached this shared memory. It includes common
// information to administrate/manage the shared buffer from a couple of
// separated processes (e.g. the refernce counter). This structure is
// located at the start of the shared memory region itself and exists
// consequently only one times per shared memory instance.
typedef struct {

	unsigned long m_ulShMemSize;
	unsigned long m_ulRefCount;
	int m_iBufferId;
//    int                 m_iUserSpaceMem;           //0 for userspace mem   !=0 kernelspace mem
	spinlock_t m_SpinlockBuffAccess;
	BOOL m_fNewData;
	BOOL m_fJobReady;
	wait_queue_head_t m_WaitQueueNewData;
	wait_queue_head_t m_WaitQueueJobReady;

#ifndef NDEBUG
	unsigned long m_ulOwnerProcID;
#endif

} tShbMemHeader;

// This structure is the "external entry point" from a separate process
// to get access to a shared buffer. This structure includes all platform
// resp. target specific information to administrate/manage the shared
// buffer from a separate process. Every process attached to the shared
// buffer has its own runtime instance of this structure with its individual
// runtime data (e.g. the scope of an event handle is limitted to the
// owner process only). The structure member <m_pShbMemHeader> points
// to the (process specific) start address of the shared memory region
// itself.
typedef struct {
	unsigned long m_SbiMagicID;	// magic ID ("SBI+")
//    void*               m_pSharedMem;
	int m_tThreadNewDataId;
	long m_lThreadNewDataNice;	// nice value of the new data thread
	int m_tThreadJobReadyId;
	unsigned long m_ulFlagsBuffAccess;	// d.k. moved from tShbMemHeader, because each
	// process needs to store the interrupt flags separately
	tSigHndlrNewData m_pfnSigHndlrNewData;
	unsigned long m_ulTimeOutJobReady;
	tSigHndlrJobReady m_pfnSigHndlrJobReady;
	tShbMemHeader *m_pShbMemHeader;
	int m_iThreadTermFlag;
	struct completion m_CompletionNewData;
/*
    struct semaphore    *m_pSemBuffAccess;
    struct semaphore    *m_pSemNewData;
    struct semaphore    *m_pSemStopSignalingNewData;
    struct semaphore    *m_pSemJobReady;
*/
#ifndef NDEBUG
	unsigned long m_ulThreadIDNewData;
	unsigned long m_ulThreadIDJobReady;
#endif
} tShbMemInst;

//---------------------------------------------------------------------------
//  Prototypes of internal functions
//---------------------------------------------------------------------------

//tShbMemInst*            ShbIpcGetShbMemInst         (tShbInstance pShbInstance_p);
//tShbMemHeader*          ShbIpcGetShbMemHeader       (tShbMemInst* pShbMemInst_p);

//---------------------------------------------------------------------------
//  Get pointer to process local information structure
//---------------------------------------------------------------------------

static inline tShbMemInst *ShbIpcGetShbMemInst(tShbInstance pShbInstance_p)
{

	tShbMemInst *pShbMemInst;

	pShbMemInst = (tShbMemInst *) pShbInstance_p;

	return (pShbMemInst);

}

//---------------------------------------------------------------------------
//  Get pointer to shared memory header
//---------------------------------------------------------------------------

static inline tShbMemHeader *ShbIpcGetShbMemHeader(tShbMemInst * pShbMemInst_p)
{

	tShbMemHeader *pShbMemHeader;

	pShbMemHeader = pShbMemInst_p->m_pShbMemHeader;

	return (pShbMemHeader);

}

//  Get pointer to process local information structure
//#define ShbIpcGetShbMemInst(pShbInstance_p) ((tShbMemInst*)pShbInstance_p)

//  Get pointer to shared memory header
//#define ShbIpcGetShbMemHeader(pShbMemInst_p) (pShbMemInst_p->m_pShbMemHeader)

// not inlined internal functions
int ShbIpcThreadSignalNewData(void *pvThreadParam_p);
int ShbIpcThreadSignalJobReady(void *pvThreadParam_p);

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------

struct sShbMemTable *psMemTableElementFirst_g;

static void *ShbIpcAllocPrivateMem(unsigned long ulMemSize_p);
static int ShbIpcFindListElement(int iBufferId,
				 struct sShbMemTable
				 **ppsReturnMemTableElement);
static void ShbIpcAppendListElement(struct sShbMemTable *sNewMemTableElement);
static void ShbIpcDeleteListElement(int iBufferId);
static void ShbIpcCrc32GenTable(unsigned long aulCrcTable[256]);
static unsigned long ShbIpcCrc32GetCrc(const char *pcString,
				       unsigned long aulCrcTable[256]);


//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

// not inlined external functions

//---------------------------------------------------------------------------
//  Initialize IPC for Shared Buffer Module
//---------------------------------------------------------------------------

tShbError ShbIpcInit(void)
{
	psMemTableElementFirst_g = NULL;
	return (kShbOk);

}

//---------------------------------------------------------------------------
//  Deinitialize IPC for Shared Buffer Module
//---------------------------------------------------------------------------

tShbError ShbIpcExit(void)
{

	return (kShbOk);

}

//---------------------------------------------------------------------------
//  Allocate Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbIpcAllocBuffer(unsigned long ulBufferSize_p,
			    const char *pszBufferID_p,
			    tShbInstance * ppShbInstance_p,
			    unsigned int *pfShbNewCreated_p)
{
	tShbError ShbError;
	int iBufferId = 0;
	unsigned long ulCrc32 = 0;
	unsigned int uiFirstProcess = 0;
	unsigned long ulShMemSize;
	tShbMemHeader *pShbMemHeader;
	tShbMemInst *pShbMemInst = NULL;
	tShbInstance pShbInstance;
	unsigned int fShMemNewCreated = FALSE;
	void *pSharedMem = NULL;
	unsigned long aulCrcTable[256];
	struct sShbMemTable *psMemTableElement;

	DEBUG_LVL_29_TRACE0("ShbIpcAllocBuffer \n");
	ulShMemSize = ulBufferSize_p + sizeof(tShbMemHeader);

	//create Buffer ID
	ShbIpcCrc32GenTable(aulCrcTable);
	ulCrc32 = ShbIpcCrc32GetCrc(pszBufferID_p, aulCrcTable);
	iBufferId = ulCrc32;
	DEBUG_LVL_29_TRACE2
	    ("ShbIpcAllocBuffer BufferSize:%d sizeof(tShb..):%d\n",
	     ulBufferSize_p, sizeof(tShbMemHeader));
	DEBUG_LVL_29_TRACE2("ShbIpcAllocBuffer BufferId:%d MemSize:%d\n",
			    iBufferId, ulShMemSize);
	//---------------------------------------------------------------
	// (1) open an existing or create a new shared memory
	//---------------------------------------------------------------
	//test if buffer already exists
	if (ShbIpcFindListElement(iBufferId, &psMemTableElement) == 0) {
		//Buffer already exists
		fShMemNewCreated = FALSE;
		pSharedMem = psMemTableElement->m_pBuffer;
		DEBUG_LVL_29_TRACE1
		    ("ShbIpcAllocBuffer attach Buffer at:%p Id:%d\n",
		     pSharedMem);
		uiFirstProcess = 1;
	} else {
		//create new Buffer
		fShMemNewCreated = TRUE;
		uiFirstProcess = 0;
		pSharedMem = kmalloc(ulShMemSize, GFP_KERNEL);
		DEBUG_LVL_29_TRACE2
		    ("ShbIpcAllocBuffer Create New Buffer at:%p Id:%d\n",
		     pSharedMem, iBufferId);
		if (pSharedMem == NULL) {
			//unable to create mem
			ShbError = kShbOutOfMem;
			goto Exit;
		}
		DEBUG_LVL_29_TRACE0("ShbIpcAllocBuffer create semas\n");
		// append Element to Mem Table
		psMemTableElement =
		    kmalloc(sizeof(struct sShbMemTable), GFP_KERNEL);
		psMemTableElement->m_iBufferId = iBufferId;
		psMemTableElement->m_pBuffer = pSharedMem;
		psMemTableElement->m_psNextMemTableElement = NULL;
		ShbIpcAppendListElement(psMemTableElement);
	}

	DEBUG_LVL_29_TRACE0("ShbIpcAllocBuffer update header\n");
	//update header
	pShbMemHeader = (tShbMemHeader *) pSharedMem;
	DEBUG_LVL_29_TRACE1
	    ("ShbIpcAllocBuffer 0 pShbMemHeader->m_ulShMemSize: %d\n",
	     pShbMemHeader->m_ulShMemSize);
	// allocate a memory block from process specific mempool to save
	// process local information to administrate/manage the shared buffer
	DEBUG_LVL_29_TRACE0("ShbIpcAllocBuffer alloc private mem\n");
	pShbMemInst =
	    (tShbMemInst *) ShbIpcAllocPrivateMem(sizeof(tShbMemInst));
	if (pShbMemInst == NULL) {
		ShbError = kShbOutOfMem;
		goto Exit;
	}
	// reset complete header to default values
	//pShbMemInst->m_SbiMagicID                             = SBI_MAGIC_ID;
//    pShbMemInst->m_pSharedMem                               = pSharedMem;
	pShbMemInst->m_tThreadNewDataId = INVALID_ID;
	pShbMemInst->m_tThreadJobReadyId = INVALID_ID;
	pShbMemInst->m_pfnSigHndlrNewData = NULL;
	pShbMemInst->m_ulTimeOutJobReady = 0;
	pShbMemInst->m_pfnSigHndlrJobReady = NULL;
	pShbMemInst->m_pShbMemHeader = pShbMemHeader;
	pShbMemInst->m_iThreadTermFlag = 0;

	// initialize completion etc.
	init_completion(&pShbMemInst->m_CompletionNewData);

	ShbError = kShbOk;
	if (fShMemNewCreated) {
		// this process was the first who wanted to use the shared memory,
		// so a new shared memory was created
		// -> setup new header information inside the shared memory region
		//    itself
		pShbMemHeader->m_ulShMemSize = ulShMemSize;
		pShbMemHeader->m_ulRefCount = 1;
		pShbMemHeader->m_iBufferId = iBufferId;
		// initialize spinlock
		spin_lock_init(&pShbMemHeader->m_SpinlockBuffAccess);
		// initialize wait queues
		init_waitqueue_head(&pShbMemHeader->m_WaitQueueNewData);
		init_waitqueue_head(&pShbMemHeader->m_WaitQueueJobReady);
	} else {
		// any other process has created the shared memory and this
		// process only has to attach to it
		// -> check and update existing header information inside the
		//    shared memory region itself
		if (pShbMemHeader->m_ulShMemSize != ulShMemSize) {
			ShbError = kShbOpenMismatch;
			goto Exit;
		}
		pShbMemHeader->m_ulRefCount++;
	}

      Exit:
	pShbInstance = (tShbInstance *) pShbMemInst;
	*pfShbNewCreated_p = fShMemNewCreated;
	*ppShbInstance_p = pShbInstance;
	return (ShbError);

}

//---------------------------------------------------------------------------
//  Release Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbIpcReleaseBuffer(tShbInstance pShbInstance_p)
{
	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	tShbError ShbError;
	tShbError ShbError2;

	DEBUG_LVL_26_TRACE1("ShbIpcReleaseBuffer(%p)\n", pShbInstance_p);
	if (pShbInstance_p == NULL) {
		return (kShbOk);
	}
	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbMemInst);

	// stop threads in any case, because they are bound to that specific instance
	ShbError2 = ShbIpcStopSignalingNewData(pShbInstance_p);
	// d.k.: Whats up with JobReady thread?
	//       Just wake it up, but without setting the semaphore variable
	wake_up_interruptible(&pShbMemHeader->m_WaitQueueJobReady);

	if (!--pShbMemHeader->m_ulRefCount) {
		ShbError = kShbOk;
		// delete mem table element
		ShbIpcDeleteListElement(pShbMemHeader->m_iBufferId);
		// delete shared mem
		kfree(pShbMemInst->m_pShbMemHeader);
	} else {
		ShbError = kShbMemUsedByOtherProcs;
	}
	//delete privat mem
	kfree(pShbMemInst);
	return (ShbError);
}

//---------------------------------------------------------------------------
//  Enter atomic section for Shared Buffer access
//---------------------------------------------------------------------------

tShbError ShbIpcEnterAtomicSection(tShbInstance pShbInstance_p)
{

	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	tShbError ShbError = kShbOk;

	if (pShbInstance_p == NULL) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}
	DEBUG_LVL_29_TRACE0("enter atomic\n");
	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbMemInst);

	// lock interrupts
	spin_lock_irqsave(&pShbMemHeader->m_SpinlockBuffAccess,
			  pShbMemInst->m_ulFlagsBuffAccess);

      Exit:
	return ShbError;

}

//---------------------------------------------------------------------------
//  Leave atomic section for Shared Buffer access
//---------------------------------------------------------------------------

tShbError ShbIpcLeaveAtomicSection(tShbInstance pShbInstance_p)
{

	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	tShbError ShbError = kShbOk;

	if (pShbInstance_p == NULL) {
		ShbError = kShbInvalidArg;
		goto Exit;
	}
	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbMemInst);
	// unlock interrupts
	spin_unlock_irqrestore(&pShbMemHeader->m_SpinlockBuffAccess,
			       pShbMemInst->m_ulFlagsBuffAccess);

      Exit:
	DEBUG_LVL_29_TRACE0("Leave Atomic \n");
	return ShbError;

}

//---------------------------------------------------------------------------
//  Start signaling of new data (called from reading process)
//---------------------------------------------------------------------------

tShbError ShbIpcStartSignalingNewData(tShbInstance pShbInstance_p,
				      tSigHndlrNewData pfnSignalHandlerNewData_p,
				      tShbPriority ShbPriority_p)
{
	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	tShbError ShbError;

	DEBUG_LVL_29_TRACE0("------->ShbIpcStartSignalingNewData\n");
	if ((pShbInstance_p == NULL) || (pfnSignalHandlerNewData_p == NULL)) {
		return (kShbInvalidArg);
	}

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbMemInst);
	ShbError = kShbOk;

	if ((pShbMemInst->m_tThreadNewDataId != INVALID_ID)
	    || (pShbMemInst->m_pfnSigHndlrNewData != NULL)) {
		ShbError = kShbAlreadySignaling;
		goto Exit;
	}
	DEBUG_LVL_26_TRACE2
	    ("ShbIpcStartSignalingNewData(%p) m_pfnSigHndlrNewData = %p\n",
	     pShbInstance_p, pfnSignalHandlerNewData_p);
	pShbMemInst->m_pfnSigHndlrNewData = pfnSignalHandlerNewData_p;
	pShbMemHeader->m_fNewData = FALSE;
	pShbMemInst->m_iThreadTermFlag = 0;

	switch (ShbPriority_p) {
	case kShbPriorityLow:
		pShbMemInst->m_lThreadNewDataNice = -2;
		break;

	case kShbPriorityNormal:
		pShbMemInst->m_lThreadNewDataNice = -9;
		break;

	case kshbPriorityHigh:
		pShbMemInst->m_lThreadNewDataNice = -20;
		break;

	}

	//create thread for signalling new data
	pShbMemInst->m_tThreadNewDataId =
	    kernel_thread(ShbIpcThreadSignalNewData, pShbInstance_p,
			  CLONE_KERNEL);

      Exit:
	return ShbError;

}

//---------------------------------------------------------------------------
//  Stop signaling of new data (called from reading process)
//---------------------------------------------------------------------------

tShbError ShbIpcStopSignalingNewData(tShbInstance pShbInstance_p)
{
	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	tShbError ShbError;

	DEBUG_LVL_29_TRACE0("------->ShbIpcStopSignalingNewData\n");
	if (pShbInstance_p == NULL) {
		return (kShbInvalidArg);
	}
	ShbError = kShbOk;
	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbMemInst);

	DEBUG_LVL_26_TRACE2
	    ("ShbIpcStopSignalingNewData(%p) pfnSignHndlrNewData=%p\n",
	     pShbInstance_p, pShbMemInst->m_pfnSigHndlrNewData);
	if (pShbMemInst->m_pfnSigHndlrNewData != NULL) {	// signal handler was set before
		int iErr;
		//set termination flag in mem header
		pShbMemInst->m_iThreadTermFlag = 1;

		// check if thread is still running at all by sending the null-signal to this thread
		/* iErr = kill_proc(pShbMemInst->m_tThreadNewDataId, 0, 1); */
		iErr = send_sig(0, pShbMemInst->m_tThreadNewDataId, 1);
		if (iErr == 0) {
			// wake up thread, because it is still running
			wake_up_interruptible(&pShbMemHeader->
					      m_WaitQueueNewData);

			//wait for termination of thread
			wait_for_completion(&pShbMemInst->m_CompletionNewData);
		}

		pShbMemInst->m_pfnSigHndlrNewData = NULL;
		pShbMemInst->m_tThreadNewDataId = INVALID_ID;
	}

	return ShbError;

}

//---------------------------------------------------------------------------
//  Signal new data (called from writing process)
//---------------------------------------------------------------------------

tShbError ShbIpcSignalNewData(tShbInstance pShbInstance_p)
{
	tShbMemHeader *pShbMemHeader;

	if (pShbInstance_p == NULL) {
		return (kShbInvalidArg);
	}
	pShbMemHeader =
	    ShbIpcGetShbMemHeader(ShbIpcGetShbMemInst(pShbInstance_p));
	//set semaphore
	pShbMemHeader->m_fNewData = TRUE;
	DEBUG_LVL_29_TRACE0("ShbIpcSignalNewData set Sem -> New Data\n");

	wake_up_interruptible(&pShbMemHeader->m_WaitQueueNewData);
	return (kShbOk);
}

//---------------------------------------------------------------------------
//  Start signaling for job ready (called from waiting process)
//---------------------------------------------------------------------------

tShbError ShbIpcStartSignalingJobReady(tShbInstance pShbInstance_p,
				       unsigned long ulTimeOut_p,
				       tSigHndlrJobReady pfnSignalHandlerJobReady_p)
{
	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	tShbError ShbError;

	if ((pShbInstance_p == NULL) || (pfnSignalHandlerJobReady_p == NULL)) {
		return (kShbInvalidArg);
	}
	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbMemInst);

	ShbError = kShbOk;
	if ((pShbMemInst->m_tThreadJobReadyId != INVALID_ID)
	    || (pShbMemInst->m_pfnSigHndlrJobReady != NULL)) {
		ShbError = kShbAlreadySignaling;
		goto Exit;
	}
	pShbMemInst->m_ulTimeOutJobReady = ulTimeOut_p;
	pShbMemInst->m_pfnSigHndlrJobReady = pfnSignalHandlerJobReady_p;
	pShbMemHeader->m_fJobReady = FALSE;
	//create thread for signalling new data
	pShbMemInst->m_tThreadJobReadyId =
	    kernel_thread(ShbIpcThreadSignalJobReady, pShbInstance_p,
			  CLONE_KERNEL);
      Exit:
	return ShbError;
}

//---------------------------------------------------------------------------
//  Signal job ready (called from executing process)
//---------------------------------------------------------------------------

tShbError ShbIpcSignalJobReady(tShbInstance pShbInstance_p)
{
	tShbMemHeader *pShbMemHeader;

	DEBUG_LVL_29_TRACE0("ShbIpcSignalJobReady\n");
	if (pShbInstance_p == NULL) {
		return (kShbInvalidArg);
	}
	pShbMemHeader =
	    ShbIpcGetShbMemHeader(ShbIpcGetShbMemInst(pShbInstance_p));
	//set semaphore
	pShbMemHeader->m_fJobReady = TRUE;
	DEBUG_LVL_29_TRACE0("ShbIpcSignalJobReady set Sem -> Job Ready \n");

	wake_up_interruptible(&pShbMemHeader->m_WaitQueueJobReady);
	return (kShbOk);
}

//---------------------------------------------------------------------------
//  Get pointer to common used share memory area
//---------------------------------------------------------------------------

void *ShbIpcGetShMemPtr(tShbInstance pShbInstance_p)
{

	tShbMemHeader *pShbMemHeader;
	void *pShbShMemPtr;

	pShbMemHeader =
	    ShbIpcGetShbMemHeader(ShbIpcGetShbMemInst(pShbInstance_p));
	if (pShbMemHeader != NULL) {
		pShbShMemPtr = (u8 *) pShbMemHeader + sizeof(tShbMemHeader);
	} else {
		pShbShMemPtr = NULL;
	}

	return (pShbShMemPtr);

}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//  Get pointer to process local information structure
//---------------------------------------------------------------------------

/*tShbMemInst*  ShbIpcGetShbMemInst (
    tShbInstance pShbInstance_p)
{

tShbMemInst*  pShbMemInst;

    pShbMemInst = (tShbMemInst*)pShbInstance_p;

    return (pShbMemInst);

}
*/

//---------------------------------------------------------------------------
//  Get pointer to shared memory header
//---------------------------------------------------------------------------

/*tShbMemHeader*  ShbIpcGetShbMemHeader (
    tShbMemInst* pShbMemInst_p)
{

tShbMemHeader*  pShbMemHeader;

    pShbMemHeader = pShbMemInst_p->m_pShbMemHeader;

    return (pShbMemHeader);

}
*/

//---------------------------------------------------------------------------
//  Allocate a memory block from process specific mempool
//---------------------------------------------------------------------------

static void *ShbIpcAllocPrivateMem(unsigned long ulMemSize_p)
{
	tShbError ShbError;
	void *pMem;

	DEBUG_LVL_29_TRACE0("ShbIpcAllocPrivateMem \n");
	//get private mem
	pMem = kmalloc(ulMemSize_p, GFP_KERNEL);
	if (pMem == NULL) {
		//unable to create mem
		ShbError = kShbOutOfMem;
		goto Exit;
	}
      Exit:
	return (pMem);

}

//---------------------------------------------------------------------------
//  Thread for new data signaling
//---------------------------------------------------------------------------

int ShbIpcThreadSignalNewData(void *pvThreadParam_p)
{
	tShbInstance pShbInstance;
	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	int iRetVal = -1;
	int fCallAgain;

	daemonize("ShbND%p", pvThreadParam_p);
	allow_signal(SIGTERM);
	pShbInstance = (tShbMemInst *) pvThreadParam_p;
	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbMemInst);

	DEBUG_LVL_26_TRACE1("ShbIpcThreadSignalNewData(%p)\n", pvThreadParam_p);

	set_user_nice(current, pShbMemInst->m_lThreadNewDataNice);

//            DEBUG_LVL_29_TRACE1("ShbIpcThreadSignalNewData wait for New Data Sem %p\n",pShbMemInst->m_pSemNewData);
	do {
		iRetVal =
		    wait_event_interruptible(pShbMemHeader->m_WaitQueueNewData,
					     (pShbMemInst->m_iThreadTermFlag !=
					      0)
					     || (pShbMemHeader->m_fNewData !=
						 FALSE));

		if (iRetVal != 0) {	// signal pending
			break;
		}

		if (pShbMemHeader->m_fNewData != FALSE) {
			pShbMemHeader->m_fNewData = FALSE;
			do {
				fCallAgain =
				    pShbMemInst->
				    m_pfnSigHndlrNewData(pShbInstance);
				// call scheduler, which will execute any task with higher priority
				schedule();
			} while (fCallAgain != FALSE);
		}
	} while (pShbMemInst->m_iThreadTermFlag == 0);
	DEBUG_LVL_29_TRACE0("ShbIpcThreadSignalNewData terminated \n");
	//set thread completed
	complete_and_exit(&pShbMemInst->m_CompletionNewData, 0);
	return 0;
}

//---------------------------------------------------------------------------
//  Thread for new data Job Ready signaling
//---------------------------------------------------------------------------

int ShbIpcThreadSignalJobReady(void *pvThreadParam_p)
{
	tShbInstance pShbInstance;
	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	long lTimeOut;
	int iRetVal = -1;

	daemonize("ShbJR%p", pvThreadParam_p);
	allow_signal(SIGTERM);
	pShbInstance = (tShbMemInst *) pvThreadParam_p;
	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbMemInst);

	DEBUG_LVL_29_TRACE0
	    ("ShbIpcThreadSignalJobReady wait for job ready Sem\n");
	if (pShbMemInst->m_ulTimeOutJobReady != 0) {
		lTimeOut = (long)pShbMemInst->m_ulTimeOutJobReady;
		//wait for job ready semaphore
		iRetVal =
		    wait_event_interruptible_timeout(pShbMemHeader->
						     m_WaitQueueJobReady,
						     (pShbMemHeader->
						      m_fJobReady != FALSE),
						     lTimeOut);
	} else {
		//wait for job ready semaphore
		iRetVal =
		    wait_event_interruptible(pShbMemHeader->m_WaitQueueJobReady,
					     (pShbMemHeader->m_fJobReady !=
					      FALSE));
	}

	if (pShbMemInst->m_pfnSigHndlrJobReady != NULL) {
		//call Handler
		pShbMemInst->m_pfnSigHndlrJobReady(pShbInstance,
						   !pShbMemHeader->m_fJobReady);
	}

	pShbMemInst->m_pfnSigHndlrJobReady = NULL;
	return 0;
}

//Build the crc table
static void ShbIpcCrc32GenTable(unsigned long aulCrcTable[256])
{
	unsigned long ulCrc, ulPoly;
	int iIndexI, iIndexJ;

	ulPoly = 0xEDB88320L;
	for (iIndexI = 0; iIndexI < 256; iIndexI++) {
		ulCrc = iIndexI;
		for (iIndexJ = 8; iIndexJ > 0; iIndexJ--) {
			if (ulCrc & 1) {
				ulCrc = (ulCrc >> 1) ^ ulPoly;
			} else {
				ulCrc >>= 1;
			}
		}
		aulCrcTable[iIndexI] = ulCrc;
	}
}

//Calculate the crc value
static unsigned long ShbIpcCrc32GetCrc(const char *pcString,
				       unsigned long aulCrcTable[256])
{
	unsigned long ulCrc;
	int iIndex;

	ulCrc = 0xFFFFFFFF;
	for (iIndex = 0; iIndex < strlen(pcString); iIndex++) {
		ulCrc =
		    ((ulCrc >> 8) & 0x00FFFFFF) ^
		    aulCrcTable[(ulCrc ^ pcString[iIndex]) & 0xFF];
	}
	return (ulCrc ^ 0xFFFFFFFF);

}

static void ShbIpcAppendListElement(struct sShbMemTable *psNewMemTableElement)
{
	struct sShbMemTable *psMemTableElement = psMemTableElementFirst_g;
	psNewMemTableElement->m_psNextMemTableElement = NULL;

	if (psMemTableElementFirst_g != NULL) {	/* sind Elemente vorhanden */
		while (psMemTableElement->m_psNextMemTableElement != NULL) {	/* suche das letzte Element */
			psMemTableElement =
			    psMemTableElement->m_psNextMemTableElement;
		}
		psMemTableElement->m_psNextMemTableElement = psNewMemTableElement;	/*  Haenge das Element hinten an */
	} else {		/* wenn die liste leer ist, bin ich das erste Element */
		psMemTableElementFirst_g = psNewMemTableElement;
	}
}

static int ShbIpcFindListElement(int iBufferId,
				 struct sShbMemTable **ppsReturnMemTableElement)
{
	struct sShbMemTable *psMemTableElement = psMemTableElementFirst_g;
	while (psMemTableElement != NULL) {
		if (psMemTableElement->m_iBufferId == iBufferId) {
//printk("ShbIpcFindListElement Buffer at:%p Id:%d\n",psMemTableElement->m_pBuffer,psMemTableElement->m_iBufferId);
			*ppsReturnMemTableElement = psMemTableElement;
//printk("ShbIpcFindListElement Buffer at:%p Id:%d\n",(*ppsReturnMemTableElement)->m_pBuffer,(*ppsReturnMemTableElement)->m_iBufferId);
			return 0;
		}
		psMemTableElement = psMemTableElement->m_psNextMemTableElement;
	}
	return -1;
}

static void ShbIpcDeleteListElement(int iBufferId)
{
	struct sShbMemTable *psMemTableElement = psMemTableElementFirst_g;
	struct sShbMemTable *psMemTableElementOld = psMemTableElementFirst_g;
	if (psMemTableElement != NULL) {
		while ((psMemTableElement != NULL)
		       && (psMemTableElement->m_iBufferId != iBufferId)) {
			psMemTableElementOld = psMemTableElement;
			psMemTableElement =
			    psMemTableElement->m_psNextMemTableElement;
		}
		if (psMemTableElement != NULL) {
			if (psMemTableElement != psMemTableElementFirst_g) {
				psMemTableElementOld->m_psNextMemTableElement =
				    psMemTableElement->m_psNextMemTableElement;
				kfree(psMemTableElement);
			} else {
				kfree(psMemTableElement);
				psMemTableElementFirst_g = NULL;
			}

		}
	}

}

