/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for asychrionus SDO Sequence Layer module

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

                $RCSfile: EplSdoAsySequ.h,v $

                $Author: D.Krueger $

                $Revision: 1.4 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/26 k.t.:   start of the implementation

****************************************************************************/

#include "../EplSdo.h"
#include "EplSdoUdpu.h"
#include "EplSdoAsndu.h"
#include "../EplEvent.h"
#include "EplTimeru.h"

#ifndef _EPLSDOASYSEQU_H_
#define _EPLSDOASYSEQU_H_

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// typedef
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// function prototypes
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoAsySeqInit(tEplSdoComReceiveCb fpSdoComCb_p,
				   tEplSdoComConCb fpSdoComConCb_p);

tEplKernel PUBLIC EplSdoAsySeqAddInstance(tEplSdoComReceiveCb fpSdoComCb_p,
					  tEplSdoComConCb fpSdoComConCb_p);

tEplKernel PUBLIC EplSdoAsySeqDelInstance(void);

tEplKernel PUBLIC EplSdoAsySeqInitCon(tEplSdoSeqConHdl * pSdoSeqConHdl_p,
				      unsigned int uiNodeId_p,
				      tEplSdoType SdoType);

tEplKernel PUBLIC EplSdoAsySeqSendData(tEplSdoSeqConHdl SdoSeqConHdl_p,
				       unsigned int uiDataSize_p,
				       tEplFrame * pData_p);

tEplKernel PUBLIC EplSdoAsySeqProcessEvent(tEplEvent * pEvent_p);

tEplKernel PUBLIC EplSdoAsySeqDelCon(tEplSdoSeqConHdl SdoSeqConHdl_p);

#endif // #ifndef _EPLSDOASYSEQU_H_
