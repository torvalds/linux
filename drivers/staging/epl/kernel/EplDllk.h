/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for kernelspace DLL module

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

                $RCSfile: EplDllk.h,v $

                $Author: D.Krueger $

                $Revision: 1.6 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/08 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#ifndef _EPL_DLLK_H_
#define _EPL_DLLK_H_

#include "../EplDll.h"
#include "../EplEvent.h"

typedef tEplKernel(*tEplDllkCbAsync) (tEplFrameInfo * pFrameInfo_p);

typedef struct {
	u8 m_be_abSrcMac[6];

} tEplDllkInitParam;

// forward declaration
struct _tEdrvTxBuffer;

struct _tEplDllkNodeInfo {
	struct _tEplDllkNodeInfo *m_pNextNodeInfo;
	struct _tEdrvTxBuffer *m_pPreqTxBuffer;
	unsigned int m_uiNodeId;
	u32 m_dwPresTimeout;
	unsigned long m_ulDllErrorEvents;
	tEplNmtState m_NmtState;
	u16 m_wPresPayloadLimit;
	u8 m_be_abMacAddr[6];
	u8 m_bSoaFlag1;
	BOOL m_fSoftDelete;	// delete node after error and ignore error

};

typedef struct _tEplDllkNodeInfo tEplDllkNodeInfo;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)

tEplKernel EplDllkAddInstance(tEplDllkInitParam * pInitParam_p);

tEplKernel EplDllkDelInstance(void);

// called before NMT_GS_COMMUNICATING will be entered to configure fixed parameters
tEplKernel EplDllkConfig(tEplDllConfigParam * pDllConfigParam_p);

// set identity of local node (may be at any time, e.g. in case of hostname change)
tEplKernel EplDllkSetIdentity(tEplDllIdentParam * pDllIdentParam_p);

// process internal events and do work that cannot be done in interrupt-context
tEplKernel EplDllkProcess(tEplEvent * pEvent_p);

// registers handler for non-EPL frames
tEplKernel EplDllkRegAsyncHandler(tEplDllkCbAsync pfnDllkCbAsync_p);

// deregisters handler for non-EPL frames
tEplKernel EplDllkDeregAsyncHandler(tEplDllkCbAsync pfnDllkCbAsync_p);

// register C_DLL_MULTICAST_ASND in ethernet driver if any AsndServiceId is registered
tEplKernel EplDllkSetAsndServiceIdFilter(tEplDllAsndServiceId ServiceId_p,
					 tEplDllAsndFilter Filter_p);

// creates the buffer for a Tx frame and registers it to the ethernet driver
tEplKernel EplDllkCreateTxFrame(unsigned int *puiHandle_p,
				tEplFrame ** ppFrame_p,
				unsigned int *puiFrameSize_p,
				tEplMsgType MsgType_p,
				tEplDllAsndServiceId ServiceId_p);

tEplKernel EplDllkDeleteTxFrame(unsigned int uiHandle_p);

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

tEplKernel EplDllkAddNode(tEplDllNodeInfo * pNodeInfo_p);

tEplKernel EplDllkDeleteNode(unsigned int uiNodeId_p);

tEplKernel EplDllkSoftDeleteNode(unsigned int uiNodeId_p);

tEplKernel EplDllkSetFlag1OfNode(unsigned int uiNodeId_p, u8 bSoaFlag1_p);

tEplKernel EplDllkGetFirstNodeInfo(tEplDllkNodeInfo ** ppNodeInfo_p);

#endif //(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

#endif // #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)

#endif // #ifndef _EPL_DLLK_H_
