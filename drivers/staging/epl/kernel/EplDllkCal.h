/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for kernelspace DLL Communication Abstraction Layer module

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

                $RCSfile: EplDllkCal.h,v $

                $Author: D.Krueger $

                $Revision: 1.6 $  $Date: 2008/11/13 17:13:09 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/13 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#ifndef _EPL_DLLKCAL_H_
#define _EPL_DLLKCAL_H_

#include "../EplDll.h"
#include "../EplEvent.h"

typedef struct {
	unsigned long m_ulCurTxFrameCountGen;
	unsigned long m_ulCurTxFrameCountNmt;
	unsigned long m_ulCurRxFrameCount;
	unsigned long m_ulMaxTxFrameCountGen;
	unsigned long m_ulMaxTxFrameCountNmt;
	unsigned long m_ulMaxRxFrameCount;

} tEplDllkCalStatistics;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)

tEplKernel EplDllkCalAddInstance(void);

tEplKernel EplDllkCalDelInstance(void);

tEplKernel EplDllkCalAsyncGetTxCount(tEplDllAsyncReqPriority * pPriority_p,
				     unsigned int *puiCount_p);
tEplKernel EplDllkCalAsyncGetTxFrame(void *pFrame_p,
				     unsigned int *puiFrameSize_p,
				     tEplDllAsyncReqPriority Priority_p);
// only frames with registered AsndServiceIds are passed to CAL
tEplKernel EplDllkCalAsyncFrameReceived(tEplFrameInfo * pFrameInfo_p);

tEplKernel EplDllkCalAsyncSend(tEplFrameInfo * pFrameInfo_p,
			       tEplDllAsyncReqPriority Priority_p);

tEplKernel EplDllkCalAsyncClearBuffer(void);

tEplKernel EplDllkCalGetStatistics(tEplDllkCalStatistics ** ppStatistics);

tEplKernel EplDllkCalProcess(tEplEvent * pEvent_p);

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

tEplKernel EplDllkCalAsyncClearQueues(void);

tEplKernel EplDllkCalIssueRequest(tEplDllReqServiceId Service_p,
				  unsigned int uiNodeId_p, u8 bSoaFlag1_p);

tEplKernel EplDllkCalAsyncGetSoaRequest(tEplDllReqServiceId * pReqServiceId_p,
					unsigned int *puiNodeId_p);

tEplKernel EplDllkCalAsyncSetPendingRequests(unsigned int uiNodeId_p,
					     tEplDllAsyncReqPriority
					     AsyncReqPrio_p,
					     unsigned int uiCount_p);

#endif //(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

#endif // #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)

#endif // #ifndef _EPL_DLLKCAL_H_
