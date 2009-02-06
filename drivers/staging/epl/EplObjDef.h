/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  defines objdict dictionary

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

                $RCSfile: EplObjDef.h,v $

                $Author: D.Krueger $

                $Revision: 1.4 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/06    k.t.:   take ObjDef.h from CANopen and modify for EPL

****************************************************************************/

#ifndef _EPLOBJDEF_H_
#define _EPLOBJDEF_H_

//---------------------------------------------------------------------------
// security checks
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// macros to help building OD
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#if (defined (EPL_OBD_USE_VARIABLE_SUBINDEX_TAB) && (EPL_OBD_USE_VARIABLE_SUBINDEX_TAB != FALSE))

#define CCM_SUBINDEX_RAM_ONLY(a)    a;
#define CCM_SUBINDEX_RAM_ONEOF(a,b) a

#else

#define CCM_SUBINDEX_RAM_ONLY(a)
#define CCM_SUBINDEX_RAM_ONEOF(a,b) b

#endif

//---------------------------------------------------------------------------
// To prevent unused memory in subindex tables we need this macro.
// But not all compilers support to preset the last struct value followed by a comma.
// Compilers which does not support a comma after last struct value has to place in a dummy subindex.
#if ((DEV_SYSTEM & _DEV_COMMA_EXT_) != 0)

#define EPL_OBD_END_SUBINDEX()
#define EPL_OBD_MAX_ARRAY_SUBENTRIES    2

#else

#define EPL_OBD_END_SUBINDEX()          {0,0,0,NULL,NULL}
#define EPL_OBD_MAX_ARRAY_SUBENTRIES    3

#endif

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// globale vars
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

// -------------------------------------------------------------------------
// creation of data in ROM memory
// -------------------------------------------------------------------------
#define EPL_OBD_CREATE_ROM_DATA
#include "objdict.h"
#undef EPL_OBD_CREATE_ROM_DATA

// -------------------------------------------------------------------------
// creation of data in RAM memory
// -------------------------------------------------------------------------

#define EPL_OBD_CREATE_RAM_DATA
#include "objdict.h"
#undef EPL_OBD_CREATE_RAM_DATA

// -------------------------------------------------------------------------
// creation of subindex tables in ROM and RAM
// -------------------------------------------------------------------------

#define EPL_OBD_CREATE_SUBINDEX_TAB
#include "objdict.h"
#undef EPL_OBD_CREATE_SUBINDEX_TAB

// -------------------------------------------------------------------------
// creation of index tables for generic, manufacturer and device part
// -------------------------------------------------------------------------

#define EPL_OBD_CREATE_INDEX_TAB
#include "objdict.h"
#undef EPL_OBD_CREATE_INDEX_TAB

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

// ----------------------------------------------------------------------------
//
// Function:    EPL_OBD_INIT_RAM_NAME()
//
// Description: function to initialize object dictionary
//
// Parameters:  pInitParam_p    = pointer to init param struct of Epl
//
// Returns:     tEplKernel      = error code
//
// State:
//
// ----------------------------------------------------------------------------

EPLDLLEXPORT tEplKernel PUBLIC EPL_OBD_INIT_RAM_NAME(tEplObdInitParam MEM *
						     pInitParam_p)
{

	tEplObdInitParam MEM *pInitParam = pInitParam_p;

	// check if pointer to parameter structure is valid
	// if not then only copy subindex tables below
	if (pInitParam != NULL) {
		// at first delete all parameters (all pointers will be set zu NULL)
		EPL_MEMSET(pInitParam, 0, sizeof(tEplObdInitParam));

#define EPL_OBD_CREATE_INIT_FUNCTION
		{
			// inserts code to init pointer to index tables
#include "objdict.h"
		}
#undef EPL_OBD_CREATE_INIT_FUNCTION

#if (defined (EPL_OBD_USER_OD) && (EPL_OBD_USER_OD != FALSE))
		{
			// to begin no user OD is defined
			pInitParam_p->m_pUserPart = NULL;
		}
#endif
	}
#define EPL_OBD_CREATE_INIT_SUBINDEX
	{
		// inserts code to copy subindex tables
#include "objdict.h"
	}
#undef EPL_OBD_CREATE_INIT_SUBINDEX

	return kEplSuccessful;

}

#endif // _EPLOBJDEF_H_

// Die letzte Zeile muﬂ unbedingt eine leere Zeile sein, weil manche Compiler
// damit ein Problem haben, wenn das nicht so ist (z.B. GNU oder Borland C++ Builder).
