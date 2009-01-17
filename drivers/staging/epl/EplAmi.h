/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  Definitions for Abstract Memory Interface

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

                $RCSfile: EplAmi.h,v $

                $Author: D.Krueger $

                $Revision: 1.2 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                GCC

  -------------------------------------------------------------------------

  Revision History:

   06.03.2000  -rs
               Implementation

   16.09.2002  -as
               To save code space the functions AmiSetByte and AmiGetByte
               are replaced by macros. For targets which assign BYTE by
               an 16Bit type, the definition of macros must changed to
               functions.

   23.02.2005  r.d.:
               Functions included for extended data types such as UNSIGNED24,
               UNSIGNED40, ...

   13.06.2006  d.k.:
               Extended the interface for EPL with the different functions
               for little endian and big endian

****************************************************************************/

#ifndef _EPLAMI_H_
#define _EPLAMI_H_

#if ((DEV_SYSTEM & _DEV_64BIT_SUPPORT_) == 0)
//    #ifdef USE_VAR64
#error 'ERROR: development system does not support 64 bit operations!'
//    #endif
#endif

//---------------------------------------------------------------------------
//  types
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Prototypen
//---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

#if (TARGET_SYSTEM == _WIN32_)
#if defined(INLINE_FUNCTION_DEF)
#undef  INLINE_FUNCTION
#define INLINE_FUNCTION     INLINE_FUNCTION_DEF
#define INLINE_ENABLED      TRUE
#define EPL_AMI_INLINED
#include "../EplStack/amix86.c"
#endif

#elif (TARGET_SYSTEM == _LINUX_)
#if defined(__m68k__)		// it is an big endian machine
#if defined(INLINE_FUNCTION_DEF)
#undef  INLINE_FUNCTION
#define INLINE_FUNCTION     INLINE_FUNCTION_DEF
#define INLINE_ENABLED      TRUE
#define EPL_AMI_INLINED
#include "../EplStack/amibe.c"
#endif
#endif
#endif

//---------------------------------------------------------------------------
//
// write functions
//
// To save code space the function AmiSetByte is replaced by
// an macro.
// void   PUBLIC  AmiSetByte  (void FAR* pAddr_p, BYTE bByteVal_p);

#define AmiSetByteToBe(pAddr_p, bByteVal_p)  {*(BYTE FAR*)(pAddr_p) = (bByteVal_p);}
#define AmiSetByteToLe(pAddr_p, bByteVal_p)  {*(BYTE FAR*)(pAddr_p) = (bByteVal_p);}

#if !defined(INLINE_ENABLED)
	void PUBLIC AmiSetWordToBe(void FAR * pAddr_p, WORD wWordVal_p);
	void PUBLIC AmiSetDwordToBe(void FAR * pAddr_p, DWORD dwDwordVal_p);
	void PUBLIC AmiSetWordToLe(void FAR * pAddr_p, WORD wWordVal_p);
	void PUBLIC AmiSetDwordToLe(void FAR * pAddr_p, DWORD dwDwordVal_p);
#endif

//---------------------------------------------------------------------------
//
// read functions
//
// To save code space the function AmiGetByte is replaced by
// an macro.
// BYTE   PUBLIC  AmiGetByte  (void FAR* pAddr_p);

#define AmiGetByteFromBe(pAddr_p)  (*(BYTE FAR*)(pAddr_p))
#define AmiGetByteFromLe(pAddr_p)  (*(BYTE FAR*)(pAddr_p))

#if !defined(INLINE_ENABLED)

	WORD PUBLIC AmiGetWordFromBe(void FAR * pAddr_p);
	DWORD PUBLIC AmiGetDwordFromBe(void FAR * pAddr_p);
	WORD PUBLIC AmiGetWordFromLe(void FAR * pAddr_p);
	DWORD PUBLIC AmiGetDwordFromLe(void FAR * pAddr_p);

//---------------------------------------------------------------------------
//
// Function:    AmiSetDword24()
//
// Description: sets a 24 bit value to a buffer
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              dwDwordVal_p    = value to set
//
// Return:      void
//
//---------------------------------------------------------------------------

	void PUBLIC AmiSetDword24ToBe(void FAR * pAddr_p, DWORD dwDwordVal_p);
	void PUBLIC AmiSetDword24ToLe(void FAR * pAddr_p, DWORD dwDwordVal_p);

//---------------------------------------------------------------------------
//
// Function:    AmiGetDword24()
//
// Description: reads a 24 bit value from a buffer
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      DWORD           = read value
//
//---------------------------------------------------------------------------

	DWORD PUBLIC AmiGetDword24FromBe(void FAR * pAddr_p);
	DWORD PUBLIC AmiGetDword24FromLe(void FAR * pAddr_p);

//#ifdef USE_VAR64

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword40()
//
// Description: sets a 40 bit value to a buffer
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
//---------------------------------------------------------------------------

	void PUBLIC AmiSetQword40ToBe(void FAR * pAddr_p, QWORD qwQwordVal_p);
	void PUBLIC AmiSetQword40ToLe(void FAR * pAddr_p, QWORD qwQwordVal_p);

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword40()
//
// Description: reads a 40 bit value from a buffer
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      QWORD
//
//---------------------------------------------------------------------------

	QWORD PUBLIC AmiGetQword40FromBe(void FAR * pAddr_p);
	QWORD PUBLIC AmiGetQword40FromLe(void FAR * pAddr_p);

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword48()
//
// Description: sets a 48 bit value to a buffer
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
//---------------------------------------------------------------------------

	void PUBLIC AmiSetQword48ToBe(void FAR * pAddr_p, QWORD qwQwordVal_p);
	void PUBLIC AmiSetQword48ToLe(void FAR * pAddr_p, QWORD qwQwordVal_p);

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword48()
//
// Description: reads a 48 bit value from a buffer
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      QWORD
//
//---------------------------------------------------------------------------

	QWORD PUBLIC AmiGetQword48FromBe(void FAR * pAddr_p);
	QWORD PUBLIC AmiGetQword48FromLe(void FAR * pAddr_p);

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword56()
//
// Description: sets a 56 bit value to a buffer
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
//---------------------------------------------------------------------------

	void PUBLIC AmiSetQword56ToBe(void FAR * pAddr_p, QWORD qwQwordVal_p);
	void PUBLIC AmiSetQword56ToLe(void FAR * pAddr_p, QWORD qwQwordVal_p);

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword56()
//
// Description: reads a 56 bit value from a buffer
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      QWORD
//
//---------------------------------------------------------------------------

	QWORD PUBLIC AmiGetQword56FromBe(void FAR * pAddr_p);
	QWORD PUBLIC AmiGetQword56FromLe(void FAR * pAddr_p);

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword64()
//
// Description: sets a 64 bit value to a buffer
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
//---------------------------------------------------------------------------

	void PUBLIC AmiSetQword64ToBe(void FAR * pAddr_p, QWORD qwQwordVal_p);
	void PUBLIC AmiSetQword64ToLe(void FAR * pAddr_p, QWORD qwQwordVal_p);

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword64()
//
// Description: reads a 64 bit value from a buffer
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      void
//
//---------------------------------------------------------------------------

	QWORD PUBLIC AmiGetQword64FromBe(void FAR * pAddr_p);
	QWORD PUBLIC AmiGetQword64FromLe(void FAR * pAddr_p);

//---------------------------------------------------------------------------
//
// Function:    AmiSetTimeOfDay()
//
// Description: sets a TIME_OF_DAY (CANopen) value to a buffer
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              pTimeOfDay_p    = pointer to struct TIME_OF_DAY
//
// Return:      void
//
//---------------------------------------------------------------------------

	void PUBLIC AmiSetTimeOfDay(void FAR * pAddr_p,
				    tTimeOfDay FAR * pTimeOfDay_p);

//---------------------------------------------------------------------------
//
// Function:    AmiGetTimeOfDay()
//
// Description: reads a TIME_OF_DAY (CANopen) value from a buffer
//
// Parameters:  pAddr_p         = pointer to source buffer
//              pTimeOfDay_p    = pointer to struct TIME_OF_DAY
//
// Return:      void
//
//---------------------------------------------------------------------------

	void PUBLIC AmiGetTimeOfDay(void FAR * pAddr_p,
				    tTimeOfDay FAR * pTimeOfDay_p);

#endif

#undef  INLINE_ENABLED		// disable actual inlining of functions
#define EPL_AMI_INCLUDED

#ifdef __cplusplus
}
#endif
#endif				// ifndef _EPLAMI_H_
// Die letzte Zeile muﬂ unbedingt eine leere Zeile sein, weil manche Compiler// damit ein Problem haben, wenn das nicht so ist (z.B. GNU oder Borland C++ Builder).
