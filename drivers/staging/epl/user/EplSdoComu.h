/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for SDO Command Layer module

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

                $RCSfile: EplSdoComu.h,v $

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
#include "../EplObd.h"
#include "../EplSdoAc.h"
#include "EplObdu.h"
#include "EplSdoAsySequ.h"

#ifndef _EPLSDOCOMU_H_
#define _EPLSDOCOMU_H_

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// typedef
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// function prototypes
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoComInit(void);

tEplKernel PUBLIC EplSdoComAddInstance(void);

tEplKernel PUBLIC EplSdoComDelInstance(void);

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)

tEplKernel PUBLIC EplSdoComDefineCon(tEplSdoComConHdl * pSdoComConHdl_p,
				     unsigned int uiTargetNodeId_p,
				     tEplSdoType ProtType_p);

tEplKernel PUBLIC EplSdoComInitTransferByIndex(tEplSdoComTransParamByIndex *
					       pSdoComTransParam_p);

tEplKernel PUBLIC EplSdoComUndefineCon(tEplSdoComConHdl SdoComConHdl_p);

tEplKernel PUBLIC EplSdoComGetState(tEplSdoComConHdl SdoComConHdl_p,
				    tEplSdoComFinished * pSdoComFinished_p);

tEplKernel PUBLIC EplSdoComSdoAbort(tEplSdoComConHdl SdoComConHdl_p,
				    DWORD dwAbortCode_p);

#endif

// for future extention
/*
tEplKernel PUBLIC EplSdoComInitTransferAllByIndex(tEplSdoComTransParamAllByIndex* pSdoComTransParam_p);

tEplKernel PUBLIC EplSdoComInitTransferByName(tEplSdoComTransParamByName* pSdoComTransParam_p);

tEplKernel PUBLIC EplSdoComInitTransferFile(tEplSdoComTransParamFile* pSdoComTransParam_p);

*/

#endif // #ifndef _EPLSDOCOMU_H_
