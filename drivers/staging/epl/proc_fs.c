/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  proc fs entry with diagnostic information under Linux

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

                $RCSfile: proc_fs.c,v $

                $Author: D.Krueger $

                $Revision: 1.13 $  $Date: 2008/11/07 13:55:56 $

                $State: Exp $

                Build Environment:
                    GNU

  -------------------------------------------------------------------------

  Revision History:

  2006/07/31 d.k.:   start of implementation

****************************************************************************/

#include "kernel/EplNmtk.h"
#include "user/EplNmtu.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
#include "user/EplNmtMnu.h"
#endif

#include "kernel/EplDllkCal.h"

//#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/version.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>

#ifdef CONFIG_COLDFIRE
#include <asm/coldfire.h>
#include "fec.h"
#endif

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

#ifndef EPL_PROC_DEV_NAME
#define EPL_PROC_DEV_NAME    "epl"
#endif

#ifndef DBG_TRACE_POINTS
#define DBG_TRACE_POINTS    23	// # of supported debug trace points
#endif

#ifndef DBG_TRACE_VALUES
#define DBG_TRACE_VALUES    24	// # of supported debug trace values (size of circular buffer)
#endif

//---------------------------------------------------------------------------
// modul global types
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

#ifdef _DBG_TRACE_POINTS_
atomic_t aatmDbgTracePoint_l[DBG_TRACE_POINTS];
DWORD adwDbgTraceValue_l[DBG_TRACE_VALUES];
DWORD dwDbgTraceValueOld_l;
unsigned int uiDbgTraceValuePos_l;
spinlock_t spinlockDbgTraceValue_l;
unsigned long ulDbTraceValueFlags_l;
#endif

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static int EplLinProcRead(char *pcBuffer_p, char **ppcStart_p, off_t Offset_p,
			  int nBufferSize_p, int *pEof_p, void *pData_p);
static int EplLinProcWrite(struct file *file, const char __user * buffer,
			   unsigned long count, void *data);

void PUBLIC TgtDbgSignalTracePoint(BYTE bTracePointNumber_p);
void PUBLIC TgtDbgPostTraceValue(DWORD dwTraceValue_p);

EPLDLLEXPORT DWORD PUBLIC EplIdentuGetRunningRequests(void);

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

tEplKernel EplLinProcInit(void)
{
	struct proc_dir_entry *pProcDirEntry;
	pProcDirEntry = create_proc_entry(EPL_PROC_DEV_NAME, S_IRUGO, NULL);
	if (pProcDirEntry != NULL) {
		pProcDirEntry->read_proc = EplLinProcRead;
		pProcDirEntry->write_proc = EplLinProcWrite;
		pProcDirEntry->data = NULL;	// device number or something else

	} else {
		return kEplNoResource;
	}

#ifdef _DBG_TRACE_POINTS_
	// initialize spinlock and circular buffer position
	spin_lock_init(&spinlockDbgTraceValue_l);
	uiDbgTraceValuePos_l = 0;
	dwDbgTraceValueOld_l = 0;
#endif

	return kEplSuccessful;
}

tEplKernel EplLinProcFree(void)
{
	remove_proc_entry(EPL_PROC_DEV_NAME, NULL);

	return kEplSuccessful;
}

//---------------------------------------------------------------------------
//  Target specific event signaling (FEC Tx-/Rx-Interrupt, used by Edrv)
//---------------------------------------------------------------------------

#ifdef _DBG_TRACE_POINTS_
void PUBLIC TgtDbgSignalTracePoint(BYTE bTracePointNumber_p)
{

	if (bTracePointNumber_p >=
	    (sizeof(aatmDbgTracePoint_l) / sizeof(aatmDbgTracePoint_l[0]))) {
		goto Exit;
	}

	atomic_inc(&aatmDbgTracePoint_l[bTracePointNumber_p]);

      Exit:

	return;

}

void PUBLIC TgtDbgPostTraceValue(DWORD dwTraceValue_p)
{

	spin_lock_irqsave(&spinlockDbgTraceValue_l, ulDbTraceValueFlags_l);
	if (dwDbgTraceValueOld_l != dwTraceValue_p) {
		adwDbgTraceValue_l[uiDbgTraceValuePos_l] = dwTraceValue_p;
		uiDbgTraceValuePos_l =
		    (uiDbgTraceValuePos_l + 1) % DBG_TRACE_VALUES;
		dwDbgTraceValueOld_l = dwTraceValue_p;
	}
	spin_unlock_irqrestore(&spinlockDbgTraceValue_l, ulDbTraceValueFlags_l);

	return;

}
#endif

//---------------------------------------------------------------------------
//  Read function for PROC-FS read access
//---------------------------------------------------------------------------

static int EplLinProcRead(char *pcBuffer_p,
			  char **ppcStart_p,
			  off_t Offset_p,
			  int nBufferSize_p, int *pEof_p, void *pData_p)
{

	int nSize;
	int Eof;
	tEplDllkCalStatistics *pDllkCalStats;

	nSize = 0;
	Eof = 0;

	// count calls of this function
#ifdef _DBG_TRACE_POINTS_
	TgtDbgSignalTracePoint(0);
#endif

	//---------------------------------------------------------------
	// generate static information
	//---------------------------------------------------------------

	// ---- Driver information ----
	nSize += snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
			  "%s    %s    (c) 2006 %s\n",
			  EPL_PRODUCT_NAME, EPL_PRODUCT_VERSION,
			  EPL_PRODUCT_MANUFACTURER);

	//---------------------------------------------------------------
	// generate process information
	//---------------------------------------------------------------

	// ---- EPL state ----
	nSize += snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
			  "NMT state:                  0x%04X\n",
			  (WORD) EplNmtkGetNmtState());

	EplDllkCalGetStatistics(&pDllkCalStats);

	nSize += snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
			  "CurAsyncTxGen=%lu CurAsyncTxNmt=%lu CurAsyncRx=%lu\nMaxAsyncTxGen=%lu MaxAsyncTxNmt=%lu MaxAsyncRx=%lu\n",
			  pDllkCalStats->m_ulCurTxFrameCountGen,
			  pDllkCalStats->m_ulCurTxFrameCountNmt,
			  pDllkCalStats->m_ulCurRxFrameCount,
			  pDllkCalStats->m_ulMaxTxFrameCountGen,
			  pDllkCalStats->m_ulMaxTxFrameCountNmt,
			  pDllkCalStats->m_ulMaxRxFrameCount);

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	// fetch running IdentRequests
	nSize += snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
			  "running IdentRequests:      0x%08lX\n",
			  EplIdentuGetRunningRequests());

	// fetch state of NmtMnu module
	{
		unsigned int uiMandatorySlaveCount;
		unsigned int uiSignalSlaveCount;
		WORD wFlags;

		EplNmtMnuGetDiagnosticInfo(&uiMandatorySlaveCount,
					   &uiSignalSlaveCount, &wFlags);

		nSize += snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
				  "MN  MandSlaveCount: %u  SigSlaveCount: %u  Flags: 0x%X\n",
				  uiMandatorySlaveCount, uiSignalSlaveCount,
				  wFlags);

	}
#endif

	// ---- FEC state ----
#ifdef CONFIG_COLDFIRE
	{
		// Receive the base address
		unsigned long base_addr;
#if (EDRV_USED_ETH_CTRL == 0)
		// Set the base address of FEC0
		base_addr = FEC_BASE_ADDR_FEC0;
#else
		// Set the base address of FEC1
		base_addr = FEC_BASE_ADDR_FEC1;
#endif

		nSize += snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
				  "FEC_ECR = 0x%08X FEC_EIR = 0x%08X FEC_EIMR = 0x%08X\nFEC_TCR = 0x%08X FECTFSR = 0x%08X FECRFSR = 0x%08X\n",
				  FEC_ECR(base_addr), FEC_EIR(base_addr),
				  FEC_EIMR(base_addr), FEC_TCR(base_addr),
				  FEC_FECTFSR(base_addr),
				  FEC_FECRFSR(base_addr));
	}
#endif

	// ---- DBG: TracePoints ----
#ifdef _DBG_TRACE_POINTS_
	{
		int nNum;

		nSize += snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
				  "DbgTracePoints:\n");
		for (nNum = 0;
		     nNum < (sizeof(aatmDbgTracePoint_l) / sizeof(atomic_t));
		     nNum++) {
			nSize +=
			    snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
				     " TracePoint[%2d]: %d\n", (int)nNum,
				     atomic_read(&aatmDbgTracePoint_l[nNum]));
		}

		nSize += snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
				  "DbgTraceValues:\n");
		for (nNum = 0; nNum < DBG_TRACE_VALUES; nNum++) {
			if (nNum == uiDbgTraceValuePos_l) {	// next value will be stored at that position
				nSize +=
				    snprintf(pcBuffer_p + nSize,
					     nBufferSize_p - nSize, "*%08lX",
					     adwDbgTraceValue_l[nNum]);
			} else {
				nSize +=
				    snprintf(pcBuffer_p + nSize,
					     nBufferSize_p - nSize, " %08lX",
					     adwDbgTraceValue_l[nNum]);
			}
			if ((nNum & 0x00000007) == 0x00000007) {	// 8 values printed -> end of line reached
				nSize +=
				    snprintf(pcBuffer_p + nSize,
					     nBufferSize_p - nSize, "\n");
			}
		}
		if ((nNum & 0x00000007) != 0x00000007) {	// number of values printed is not a multiple of 8 -> print new line
			nSize +=
			    snprintf(pcBuffer_p + nSize, nBufferSize_p - nSize,
				     "\n");
		}
	}
#endif

	Eof = 1;
	goto Exit;

      Exit:

	*pEof_p = Eof;

	return (nSize);

}

//---------------------------------------------------------------------------
//  Write function for PROC-FS write access
//---------------------------------------------------------------------------

static int EplLinProcWrite(struct file *file, const char __user * buffer,
			   unsigned long count, void *data)
{
	char abBuffer[count + 1];
	int iErr;
	int iVal = 0;
	tEplNmtEvent NmtEvent;

	if (count > 0) {
		iErr = copy_from_user(abBuffer, buffer, count);
		if (iErr != 0) {
			return count;
		}
		abBuffer[count] = '\0';

		iErr = sscanf(abBuffer, "%i", &iVal);
	}
	if ((iVal <= 0) || (iVal > 0x2F)) {
		NmtEvent = kEplNmtEventSwReset;
	} else {
		NmtEvent = (tEplNmtEvent) iVal;
	}
	// execute specified NMT command on write access of /proc/epl
	EplNmtuNmtEvent(NmtEvent);

	return count;
}
