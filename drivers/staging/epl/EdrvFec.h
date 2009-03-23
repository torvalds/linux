/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  interface for ethernetdriver
                "fast ethernet controller" (FEC)
                freescale coldfire MCF528x and compatible FEC

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

                $RCSfile: EdrvFec.h,v $

                $Author: D.Krueger $

                $Revision: 1.3 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                Dev C++ and GNU-Compiler for m68k

  -------------------------------------------------------------------------

  Revision History:

  2005/08/01 m.b.:   start of implementation

****************************************************************************/

#ifndef _EDRVFEC_H_
#define _EDRVFEC_H_

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------
// do this in config header
#define TARGET_HARDWARE TGTHW_SPLC_CF54

// base addresses
#if ((TARGET_HARDWARE & TGT_CPU_MASK_) == TGT_CPU_5282)

#elif ((TARGET_HARDWARE & TGT_CPU_MASK_) == TGT_CPU_5485)

#else

#error 'ERROR: Target was never implemented!'

#endif

//---------------------------------------------------------------------------
// types
//---------------------------------------------------------------------------

// Rx and Tx buffer descriptor format
typedef struct {
	u16 m_wStatus;		// control / status  ---  used by edrv, do not change in application
	u16 m_wLength;		// transfer length
	u8 *m_pbData;		// buffer address
} tBufferDescr;

#if ((TARGET_HARDWARE & TGT_CPU_MASK_) == TGT_CPU_5282)

#elif ((TARGET_HARDWARE & TGT_CPU_MASK_) == TGT_CPU_5485)

#endif

//---------------------------------------------------------------------------
// function prototypes
//---------------------------------------------------------------------------

#endif // #ifndef _EDRV_FEC_H_
