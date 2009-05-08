/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  Abstract Memory Interface for x86 compatible

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

                $RCSfile: amix86.c,v $

                $Author: D.Krueger $

                $Revision: 1.3 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                    ...

  -------------------------------------------------------------------------

  Revision History:

  r.s.: first implemetation

  2006-06-13  d.k.: duplicate functions for little endian and big endian

****************************************************************************/

//#include "global.h"
//#include "EplAmi.h"
#include "EplInc.h"

//---------------------------------------------------------------------------
// typedef
//---------------------------------------------------------------------------

typedef struct {
	u16 m_wWord;

} twStruct;

typedef struct {
	u32 m_dwDword;

} tdwStruct;

typedef struct {
	u64 m_qwQword;

} tqwStruct;

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    AmiSetXXXToBe()
//
// Description: writes the specified value to the absolute address in
//              big endian
//
// Parameters:  pAddr_p                 = absolute address
//              xXXXVal_p               = value
//
// Returns:     (none)
//
// State:
//
//---------------------------------------------------------------------------

//------------< write u8 in big endian >--------------------------
/*
void AmiSetByteToBe (void *pAddr_p, u8 bByteVal_p)
{

	*(u8 *)pAddr_p = bByteVal_p;

}
*/

//------------< write u16 in big endian >--------------------------

void AmiSetWordToBe(void * pAddr_p, u16 wWordVal_p)
{
	twStruct *pwStruct;
	twStruct wValue;

	wValue.m_wWord = (u16) ((wWordVal_p & 0x00FF) << 8);	//LSB to MSB
	wValue.m_wWord |= (u16) ((wWordVal_p & 0xFF00) >> 8);	//MSB to LSB

	pwStruct = (twStruct *) pAddr_p;
	pwStruct->m_wWord = wValue.m_wWord;

}

//------------< write u32 in big endian >-------------------------

void AmiSetDwordToBe(void *pAddr_p, u32 dwDwordVal_p)
{
	tdwStruct *pdwStruct;
	tdwStruct dwValue;

	dwValue.m_dwDword = ((dwDwordVal_p & 0x000000FF) << 24);	//LSB to MSB
	dwValue.m_dwDword |= ((dwDwordVal_p & 0x0000FF00) << 8);
	dwValue.m_dwDword |= ((dwDwordVal_p & 0x00FF0000) >> 8);
	dwValue.m_dwDword |= ((dwDwordVal_p & 0xFF000000) >> 24);	//MSB to LSB

	pdwStruct = (tdwStruct *) pAddr_p;
	pdwStruct->m_dwDword = dwValue.m_dwDword;

}

//---------------------------------------------------------------------------
//
// Function:    AmiSetXXXToLe()
//
// Description: writes the specified value to the absolute address in
//              little endian
//
// Parameters:  pAddr_p                 = absolute address
//              xXXXVal_p               = value
//
// Returns:     (none)
//
// State:
//
//---------------------------------------------------------------------------

//------------< write u8 in little endian >--------------------------
/*
void AmiSetByteToLe (void *pAddr_p, u8 bByteVal_p)
{

	*(u8 *)pAddr_p = bByteVal_p;

}
*/

//------------< write u16 in little endian >--------------------------

void AmiSetWordToLe(void *pAddr_p, u16 wWordVal_p)
{
	twStruct *pwStruct;

	pwStruct = (twStruct *) pAddr_p;
	pwStruct->m_wWord = wWordVal_p;

}

//------------< write u32 in little endian >-------------------------

void AmiSetDwordToLe(void *pAddr_p, u32 dwDwordVal_p)
{
	tdwStruct *pdwStruct;

	pdwStruct = (tdwStruct *) pAddr_p;
	pdwStruct->m_dwDword = dwDwordVal_p;

}

//---------------------------------------------------------------------------
//
// Function:    AmiGetXXXFromBe()
//
// Description: reads the specified value from the absolute address in
//              big endian
//
// Parameters:  pAddr_p                 = absolute address
//
// Returns:     XXX                     = value
//
// State:
//
//---------------------------------------------------------------------------

//------------< read u8 in big endian >---------------------------
/*
u8 AmiGetByteFromBe (void *pAddr_p)
{

	return ( *(u8 *)pAddr_p );

}
*/

//------------< read u16 in big endian >---------------------------

u16 AmiGetWordFromBe(void *pAddr_p)
{
	twStruct *pwStruct;
	twStruct wValue;

	pwStruct = (twStruct *) pAddr_p;

	wValue.m_wWord = (u16) ((pwStruct->m_wWord & 0x00FF) << 8);	//LSB to MSB
	wValue.m_wWord |= (u16) ((pwStruct->m_wWord & 0xFF00) >> 8);	//MSB to LSB

	return (wValue.m_wWord);

}

//------------< read u32 in big endian >--------------------------

u32 AmiGetDwordFromBe(void *pAddr_p)
{
	tdwStruct *pdwStruct;
	tdwStruct dwValue;

	pdwStruct = (tdwStruct *) pAddr_p;

	dwValue.m_dwDword = ((pdwStruct->m_dwDword & 0x000000FF) << 24);	//LSB to MSB
	dwValue.m_dwDword |= ((pdwStruct->m_dwDword & 0x0000FF00) << 8);
	dwValue.m_dwDword |= ((pdwStruct->m_dwDword & 0x00FF0000) >> 8);
	dwValue.m_dwDword |= ((pdwStruct->m_dwDword & 0xFF000000) >> 24);	//MSB to LSB

	return (dwValue.m_dwDword);

}

//---------------------------------------------------------------------------
//
// Function:    AmiGetXXXFromLe()
//
// Description: reads the specified value from the absolute address in
//              little endian
//
// Parameters:  pAddr_p                 = absolute address
//
// Returns:     XXX                     = value
//
// State:
//
//---------------------------------------------------------------------------

//------------< read u8 in little endian >---------------------------
/*
u8 AmiGetByteFromLe (void *pAddr_p)
{

	return ( *(u8 *)pAddr_p );

}
*/

//------------< read u16 in little endian >---------------------------

u16 AmiGetWordFromLe(void *pAddr_p)
{
	twStruct *pwStruct;

	pwStruct = (twStruct *) pAddr_p;
	return (pwStruct->m_wWord);
}

//------------< read u32 in little endian >--------------------------

u32 AmiGetDwordFromLe(void *pAddr_p)
{
	tdwStruct *pdwStruct;

	pdwStruct = (tdwStruct *) pAddr_p;
	return (pdwStruct->m_dwDword);
}

//---------------------------------------------------------------------------
//
// Function:    AmiSetDword24ToBe()
//
// Description: sets a 24 bit value to a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              dwDwordVal_p    = value to set
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiSetDword24ToBe(void *pAddr_p, u32 dwDwordVal_p)
{
	((u8 *) pAddr_p)[0] = ((u8 *) & dwDwordVal_p)[2];
	((u8 *) pAddr_p)[1] = ((u8 *) & dwDwordVal_p)[1];
	((u8 *) pAddr_p)[2] = ((u8 *) & dwDwordVal_p)[0];
}

//---------------------------------------------------------------------------
//
// Function:    AmiSetDword24ToLe()
//
// Description: sets a 24 bit value to a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              dwDwordVal_p    = value to set
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiSetDword24ToLe(void *pAddr_p, u32 dwDwordVal_p)
{
	((u8 *) pAddr_p)[0] = ((u8 *) & dwDwordVal_p)[0];
	((u8 *) pAddr_p)[1] = ((u8 *) & dwDwordVal_p)[1];
	((u8 *) pAddr_p)[2] = ((u8 *) & dwDwordVal_p)[2];
}

//---------------------------------------------------------------------------
//
// Function:    AmiGetDword24FromBe()
//
// Description: reads a 24 bit value from a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      u32           = read value
//
// State:       not tested
//
//---------------------------------------------------------------------------
u32 AmiGetDword24FromBe(void *pAddr_p)
{
	tdwStruct dwStruct;

	dwStruct.m_dwDword = AmiGetDwordFromBe(pAddr_p);
	dwStruct.m_dwDword >>= 8;

	return (dwStruct.m_dwDword);
}

//---------------------------------------------------------------------------
//
// Function:    AmiGetDword24FromLe()
//
// Description: reads a 24 bit value from a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      u32           = read value
//
// State:       not tested
//
//---------------------------------------------------------------------------
u32 AmiGetDword24FromLe(void *pAddr_p)
{
	tdwStruct dwStruct;

	dwStruct.m_dwDword = AmiGetDwordFromLe(pAddr_p);
	dwStruct.m_dwDword &= 0x00FFFFFF;

	return (dwStruct.m_dwDword);
}

//#ifdef USE_VAR64

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword64ToBe()
//
// Description: sets a 64 bit value to a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------
void AmiSetQword64ToBe(void *pAddr_p, u64 qwQwordVal_p)
{
	((u8 *) pAddr_p)[0] = ((u8 *) & qwQwordVal_p)[7];
	((u8 *) pAddr_p)[1] = ((u8 *) & qwQwordVal_p)[6];
	((u8 *) pAddr_p)[2] = ((u8 *) & qwQwordVal_p)[5];
	((u8 *) pAddr_p)[3] = ((u8 *) & qwQwordVal_p)[4];
	((u8 *) pAddr_p)[4] = ((u8 *) & qwQwordVal_p)[3];
	((u8 *) pAddr_p)[5] = ((u8 *) & qwQwordVal_p)[2];
	((u8 *) pAddr_p)[6] = ((u8 *) & qwQwordVal_p)[1];
	((u8 *) pAddr_p)[7] = ((u8 *) & qwQwordVal_p)[0];
}

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword64ToLe()
//
// Description: sets a 64 bit value to a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------
void AmiSetQword64ToLe(void *pAddr_p, u64 qwQwordVal_p)
{
	u64 *pqwDst;

	pqwDst = (u64 *) pAddr_p;
	*pqwDst = qwQwordVal_p;
}

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword64FromBe()
//
// Description: reads a 64 bit value from a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------
u64 AmiGetQword64FromBe(void *pAddr_p)
{
	tqwStruct qwStruct;

	((u8 *) & qwStruct.m_qwQword)[0] = ((u8 *) pAddr_p)[7];
	((u8 *) & qwStruct.m_qwQword)[1] = ((u8 *) pAddr_p)[6];
	((u8 *) & qwStruct.m_qwQword)[2] = ((u8 *) pAddr_p)[5];
	((u8 *) & qwStruct.m_qwQword)[3] = ((u8 *) pAddr_p)[4];
	((u8 *) & qwStruct.m_qwQword)[4] = ((u8 *) pAddr_p)[3];
	((u8 *) & qwStruct.m_qwQword)[5] = ((u8 *) pAddr_p)[2];
	((u8 *) & qwStruct.m_qwQword)[6] = ((u8 *) pAddr_p)[1];
	((u8 *) & qwStruct.m_qwQword)[7] = ((u8 *) pAddr_p)[0];

	return (qwStruct.m_qwQword);
}

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword64FromLe()
//
// Description: reads a 64 bit value from a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------
u64 AmiGetQword64FromLe(void *pAddr_p)
{
	tqwStruct *pqwStruct;
	tqwStruct qwStruct;

	pqwStruct = (tqwStruct *) pAddr_p;
	qwStruct.m_qwQword = pqwStruct->m_qwQword;

	return (qwStruct.m_qwQword);
}

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword40ToBe()
//
// Description: sets a 40 bit value to a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiSetQword40ToBe(void *pAddr_p, u64 qwQwordVal_p)
{

	((u8 *) pAddr_p)[0] = ((u8 *) & qwQwordVal_p)[4];
	((u8 *) pAddr_p)[1] = ((u8 *) & qwQwordVal_p)[3];
	((u8 *) pAddr_p)[2] = ((u8 *) & qwQwordVal_p)[2];
	((u8 *) pAddr_p)[3] = ((u8 *) & qwQwordVal_p)[1];
	((u8 *) pAddr_p)[4] = ((u8 *) & qwQwordVal_p)[0];

}

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword40ToLe()
//
// Description: sets a 40 bit value to a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiSetQword40ToLe(void *pAddr_p, u64 qwQwordVal_p)
{

	((u32 *) pAddr_p)[0] = ((u32 *) & qwQwordVal_p)[0];
	((u8 *) pAddr_p)[4] = ((u8 *) & qwQwordVal_p)[4];

}

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword40FromBe()
//
// Description: reads a 40 bit value from a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      u64
//
// State:       not tested
//
//---------------------------------------------------------------------------

u64 AmiGetQword40FromBe(void *pAddr_p)
{

	tqwStruct qwStruct;

	qwStruct.m_qwQword = AmiGetQword64FromBe(pAddr_p);
	qwStruct.m_qwQword >>= 24;

	return (qwStruct.m_qwQword);

}

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword40FromLe()
//
// Description: reads a 40 bit value from a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      u64
//
// State:       not tested
//
//---------------------------------------------------------------------------

u64 AmiGetQword40FromLe(void *pAddr_p)
{

	tqwStruct qwStruct;

	qwStruct.m_qwQword = AmiGetQword64FromLe(pAddr_p);
	qwStruct.m_qwQword &= 0x000000FFFFFFFFFFLL;

	return (qwStruct.m_qwQword);

}

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword48ToBe()
//
// Description: sets a 48 bit value to a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiSetQword48ToBe(void *pAddr_p, u64 qwQwordVal_p)
{

	((u8 *) pAddr_p)[0] = ((u8 *) & qwQwordVal_p)[5];
	((u8 *) pAddr_p)[1] = ((u8 *) & qwQwordVal_p)[4];
	((u8 *) pAddr_p)[2] = ((u8 *) & qwQwordVal_p)[3];
	((u8 *) pAddr_p)[3] = ((u8 *) & qwQwordVal_p)[2];
	((u8 *) pAddr_p)[4] = ((u8 *) & qwQwordVal_p)[1];
	((u8 *) pAddr_p)[5] = ((u8 *) & qwQwordVal_p)[0];

}

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword48ToLe()
//
// Description: sets a 48 bit value to a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiSetQword48ToLe(void *pAddr_p, u64 qwQwordVal_p)
{

	((u32 *) pAddr_p)[0] = ((u32 *) & qwQwordVal_p)[0];
	((u16 *) pAddr_p)[2] = ((u16 *) & qwQwordVal_p)[2];

}

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword48FromBe()
//
// Description: reads a 48 bit value from a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      u64
//
// State:       not tested
//
//---------------------------------------------------------------------------

u64 AmiGetQword48FromBe(void *pAddr_p)
{

	tqwStruct qwStruct;

	qwStruct.m_qwQword = AmiGetQword64FromBe(pAddr_p);
	qwStruct.m_qwQword >>= 16;

	return (qwStruct.m_qwQword);

}

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword48FromLe()
//
// Description: reads a 48 bit value from a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      u64
//
// State:       not tested
//
//---------------------------------------------------------------------------

u64 AmiGetQword48FromLe(void *pAddr_p)
{

	tqwStruct qwStruct;

	qwStruct.m_qwQword = AmiGetQword64FromLe(pAddr_p);
	qwStruct.m_qwQword &= 0x0000FFFFFFFFFFFFLL;

	return (qwStruct.m_qwQword);

}

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword56ToBe()
//
// Description: sets a 56 bit value to a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiSetQword56ToBe(void *pAddr_p, u64 qwQwordVal_p)
{

	((u8 *) pAddr_p)[0] = ((u8 *) & qwQwordVal_p)[6];
	((u8 *) pAddr_p)[1] = ((u8 *) & qwQwordVal_p)[5];
	((u8 *) pAddr_p)[2] = ((u8 *) & qwQwordVal_p)[4];
	((u8 *) pAddr_p)[3] = ((u8 *) & qwQwordVal_p)[3];
	((u8 *) pAddr_p)[4] = ((u8 *) & qwQwordVal_p)[2];
	((u8 *) pAddr_p)[5] = ((u8 *) & qwQwordVal_p)[1];
	((u8 *) pAddr_p)[6] = ((u8 *) & qwQwordVal_p)[0];

}

//---------------------------------------------------------------------------
//
// Function:    AmiSetQword56ToLe()
//
// Description: sets a 56 bit value to a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to destination buffer
//              qwQwordVal_p    = quadruple word value
//
// Return:      void
//
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiSetQword56ToLe(void *pAddr_p, u64 qwQwordVal_p)
{

	((u32 *) pAddr_p)[0] = ((u32 *) & qwQwordVal_p)[0];
	((u16 *) pAddr_p)[2] = ((u16 *) & qwQwordVal_p)[2];
	((u8 *) pAddr_p)[6] = ((u8 *) & qwQwordVal_p)[6];

}

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword56FromBe()
//
// Description: reads a 56 bit value from a buffer in big endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      u64
//
// State:       not tested
//
//---------------------------------------------------------------------------

u64 AmiGetQword56FromBe(void *pAddr_p)
{

	tqwStruct qwStruct;

	qwStruct.m_qwQword = AmiGetQword64FromBe(pAddr_p);
	qwStruct.m_qwQword >>= 8;

	return (qwStruct.m_qwQword);

}

//---------------------------------------------------------------------------
//
// Function:    AmiGetQword56FromLe()
//
// Description: reads a 56 bit value from a buffer in little endian
//
// Parameters:  pAddr_p         = pointer to source buffer
//
// Return:      u64
//
// State:       not tested
//
//---------------------------------------------------------------------------

u64 AmiGetQword56FromLe(void *pAddr_p)
{

	tqwStruct qwStruct;

	qwStruct.m_qwQword = AmiGetQword64FromLe(pAddr_p);
	qwStruct.m_qwQword &= 0x00FFFFFFFFFFFFFFLL;

	return (qwStruct.m_qwQword);

}

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
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiSetTimeOfDay(void *pAddr_p, tTimeOfDay *pTimeOfDay_p)
{

	AmiSetDwordToLe(((u8 *) pAddr_p), pTimeOfDay_p->m_dwMs & 0x0FFFFFFF);
	AmiSetWordToLe(((u8 *) pAddr_p) + 4, pTimeOfDay_p->m_wDays);

}

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
// State:       not tested
//
//---------------------------------------------------------------------------

void AmiGetTimeOfDay(void *pAddr_p, tTimeOfDay *pTimeOfDay_p)
{

	pTimeOfDay_p->m_dwMs = AmiGetDwordFromLe(((u8 *) pAddr_p)) & 0x0FFFFFFF;
	pTimeOfDay_p->m_wDays = AmiGetWordFromLe(((u8 *) pAddr_p) + 4);

}

// EOF

// Die letzte Zeile muﬂ unbedingt eine leere Zeile sein, weil manche Compiler
// damit ein Problem haben, wenn das nicht so ist (z.B. GNU oder Borland C++ Builder).
