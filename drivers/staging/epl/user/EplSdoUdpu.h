/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for SDO/UDP-Protocollabstractionlayer module

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

                $RCSfile: EplSdoUdpu.h,v $

                $Author: D.Krueger $

                $Revision: 1.5 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/26 k.t.:   start of the implementation

****************************************************************************/

#include "../EplSdo.h"

#ifndef _EPLSDOUDPU_H_
#define _EPLSDOUDPU_H_

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// typedef
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// function prototypes
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)

tEplKernel PUBLIC EplSdoUdpuInit(tEplSequLayerReceiveCb fpReceiveCb_p);

tEplKernel PUBLIC EplSdoUdpuAddInstance(tEplSequLayerReceiveCb fpReceiveCb_p);

tEplKernel PUBLIC EplSdoUdpuDelInstance(void);

tEplKernel PUBLIC EplSdoUdpuConfig(unsigned long ulIpAddr_p,
				   unsigned int uiPort_p);

tEplKernel PUBLIC EplSdoUdpuInitCon(tEplSdoConHdl * pSdoConHandle_p,
				    unsigned int uiTargetNodeId_p);

tEplKernel PUBLIC EplSdoUdpuSendData(tEplSdoConHdl SdoConHandle_p,
				     tEplFrame * pSrcData_p,
				     DWORD dwDataSize_p);

tEplKernel PUBLIC EplSdoUdpuDelCon(tEplSdoConHdl SdoConHandle_p);

#endif // end of #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)

#endif // #ifndef _EPLSDOUDPU_H_
