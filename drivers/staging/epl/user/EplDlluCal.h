/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for DLL Communication Abstraction Layer module in EPL user part

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

                $RCSfile: EplDlluCal.h,v $

                $Author: D.Krueger $

                $Revision: 1.5 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/20 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#ifndef _EPL_DLLUCAL_H_
#define _EPL_DLLUCAL_H_

#include "../EplDll.h"
#include "../EplEvent.h"


typedef tEplKernel(* tEplDlluCbAsnd) (tEplFrameInfo * pFrameInfo_p);

tEplKernel EplDlluCalAddInstance(void);

tEplKernel EplDlluCalDelInstance(void);

tEplKernel EplDlluCalRegAsndService(tEplDllAsndServiceId ServiceId_p,
				    tEplDlluCbAsnd pfnDlluCbAsnd_p,
				    tEplDllAsndFilter Filter_p);

tEplKernel EplDlluCalAsyncSend(tEplFrameInfo * pFrameInfo,
			       tEplDllAsyncReqPriority Priority_p);

tEplKernel EplDlluCalProcess(tEplEvent * pEvent_p);

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

tEplKernel EplDlluCalAddNode(tEplDllNodeInfo * pNodeInfo_p);

tEplKernel EplDlluCalDeleteNode(unsigned int uiNodeId_p);

tEplKernel EplDlluCalSoftDeleteNode(unsigned int uiNodeId_p);

tEplKernel EplDlluCalIssueRequest(tEplDllReqServiceId Service_p,
				  unsigned int uiNodeId_p, u8 bSoaFlag1_p);

#endif

#endif // #ifndef _EPL_DLLUCAL_H_
