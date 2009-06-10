/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for NMT-Userspace-Module

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

                $RCSfile: EplNmtu.h,v $

                $Author: D.Krueger $

                $Revision: 1.5 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/09 k.t.:   start of the implementation

****************************************************************************/

#ifndef _EPLNMTU_H_
#define _EPLNMTU_H_

#include "../EplNmt.h"
#include "EplEventu.h"

// nmt commands
typedef enum {
	// requestable ASnd ServiceIds    0x01..0x1F
	kEplNmtCmdIdentResponse = 0x01,
	kEplNmtCmdStatusResponse = 0x02,
	// plain NMT state commands       0x20..0x3F
	kEplNmtCmdStartNode = 0x21,
	kEplNmtCmdStopNode = 0x22,
	kEplNmtCmdEnterPreOperational2 = 0x23,
	kEplNmtCmdEnableReadyToOperate = 0x24,
	kEplNmtCmdResetNode = 0x28,
	kEplNmtCmdResetCommunication = 0x29,
	kEplNmtCmdResetConfiguration = 0x2A,
	kEplNmtCmdSwReset = 0x2B,
	// extended NMT state commands    0x40..0x5F
	kEplNmtCmdStartNodeEx = 0x41,
	kEplNmtCmdStopNodeEx = 0x42,
	kEplNmtCmdEnterPreOperational2Ex = 0x43,
	kEplNmtCmdEnableReadyToOperateEx = 0x44,
	kEplNmtCmdResetNodeEx = 0x48,
	kEplNmtCmdResetCommunicationEx = 0x49,
	kEplNmtCmdResetConfigurationEx = 0x4A,
	kEplNmtCmdSwResetEx = 0x4B,
	// NMT managing commands          0x60..0x7F
	kEplNmtCmdNetHostNameSet = 0x62,
	kEplNmtCmdFlushArpEntry = 0x63,
	// NMT info services              0x80..0xBF
	kEplNmtCmdPublishConfiguredCN = 0x80,
	kEplNmtCmdPublishActiveCN = 0x90,
	kEplNmtCmdPublishPreOperational1 = 0x91,
	kEplNmtCmdPublishPreOperational2 = 0x92,
	kEplNmtCmdPublishReadyToOperate = 0x93,
	kEplNmtCmdPublishOperational = 0x94,
	kEplNmtCmdPublishStopped = 0x95,
	kEplNmtCmdPublishEmergencyNew = 0xA0,
	kEplNmtCmdPublishTime = 0xB0,

	kEplNmtCmdInvalidService = 0xFF
} tEplNmtCommand;

typedef tEplKernel(* tEplNmtuStateChangeCallback) (tEplEventNmtStateChange NmtStateChange_p);

typedef tEplKernel(* tEplNmtuCheckEventCallback) (tEplNmtEvent NmtEvent_p);

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)

tEplKernel EplNmtuInit(void);

tEplKernel EplNmtuAddInstance(void);

tEplKernel EplNmtuDelInstance(void);

tEplKernel EplNmtuNmtEvent(tEplNmtEvent NmtEvent_p);

tEplNmtState EplNmtuGetNmtState(void);

tEplKernel EplNmtuProcessEvent(tEplEvent *pEplEvent_p);

tEplKernel EplNmtuRegisterStateChangeCb(tEplNmtuStateChangeCallback pfnEplNmtStateChangeCb_p);

#endif // #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)

#endif // #ifndef _EPLNMTU_H_
