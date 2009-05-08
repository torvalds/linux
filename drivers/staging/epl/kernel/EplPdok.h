/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for kernel PDO module

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

                $RCSfile: EplPdok.h,v $

                $Author: D.Krueger $

                $Revision: 1.5 $  $Date: 2008/06/23 14:56:33 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/05/22 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#ifndef _EPL_PDOK_H_
#define _EPL_PDOK_H_

#include "../EplPdo.h"
#include "../EplEvent.h"
#include "../EplDll.h"

// process events from queue (PDOs/frames and SoA for synchronization)
tEplKernel EplPdokProcess(tEplEvent * pEvent_p);

// copies RPDO to event queue for processing
// is called by DLL in NMT_CS_READY_TO_OPERATE and NMT_CS_OPERATIONAL
// PDO needs not to be valid
tEplKernel EplPdokCbPdoReceived(tEplFrameInfo * pFrameInfo_p);

// posts pointer and size of TPDO to event queue
// is called by DLL in NMT_CS_PRE_OPERATIONAL_2,
//     NMT_CS_READY_TO_OPERATE and NMT_CS_OPERATIONAL
tEplKernel EplPdokCbPdoTransmitted(tEplFrameInfo * pFrameInfo_p);

// posts SoA event to queue
tEplKernel EplPdokCbSoa(tEplFrameInfo * pFrameInfo_p);

tEplKernel EplPdokAddInstance(void);

tEplKernel EplPdokDelInstance(void);

#endif // #ifndef _EPL_PDOK_H_
