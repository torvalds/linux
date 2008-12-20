/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for communication abstraction layer
                for the Epl-Obd-Userspace-Modul

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

                $RCSfile: EplObduCal.c,v $

                $Author: D.Krueger $

                $Revision: 1.6 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/19 k.t.:   start of the implementation

****************************************************************************/
#include "EplInc.h"
#include "user/EplObduCal.h"
#include "kernel/EplObdk.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0) && (EPL_OBD_USE_KERNEL != FALSE)

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplObduCalWriteEntry()
//
// Description: Function encapsulate access of function EplObdWriteEntry
//
// Parameters:  uiIndex_p       =   Index of the OD entry
//              uiSubIndex_p    =   Subindex of the OD Entry
//              pSrcData_p      =   Pointer to the data to write
//              Size_p          =   Size of the data in Byte
//
// Return:      tEplKernel      =   Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplObduCalWriteEntry(unsigned int uiIndex_p,
						    unsigned int uiSubIndex_p,
						    void *pSrcData_p,
						    tEplObdSize Size_p)
{
	tEplKernel Ret;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Ret = EplObdWriteEntry(uiIndex_p, uiSubIndex_p, pSrcData_p, Size_p);
#else
	Ret = kEplSuccessful;
#endif

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalReadEntry()
//
// Description: Function encapsulate access of function EplObdReadEntry
//
// Parameters:  uiIndex_p       = Index oof the OD entry to read
//              uiSubIndex_p    = Subindex to read
//              pDstData_p      = pointer to the buffer for data
//              Offset_p        = offset in data for read access
//              pSize_p         = IN: Size of the buffer
//                                OUT: number of readed Bytes
//
// Return:      tEplKernel      =   errorcode
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplObduCalReadEntry(unsigned int uiIndex_p,
						   unsigned int uiSubIndex_p,
						   void *pDstData_p,
						   tEplObdSize * pSize_p)
{
	tEplKernel Ret;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Ret = EplObdReadEntry(uiIndex_p, uiSubIndex_p, pDstData_p, pSize_p);
#else
	Ret = kEplSuccessful;
#endif

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalAccessOdPart()
//
// Description: Function encapsulate access of function EplObdAccessOdPart
//
// Parameters:  ObdPart_p       = od-part to reset
//              Direction_p     = directory flag for
//
// Return:      tEplKernel  = errorcode
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplObduCalAccessOdPart(tEplObdPart ObdPart_p,
						      tEplObdDir Direction_p)
{
	tEplKernel Ret;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Ret = EplObdAccessOdPart(ObdPart_p, Direction_p);
#else
	Ret = kEplSuccessful;
#endif

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalDefineVar()
//
// Description: Function encapsulate access of function EplObdDefineVar
//
// Parameters:  pEplVarParam_p = varentry
//
// Return:      tEplKernel  =   errorcode
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplObduCalDefineVar(tEplVarParam MEM *
						   pVarParam_p)
{
	tEplKernel Ret;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Ret = EplObdDefineVar(pVarParam_p);
#else
	Ret = kEplSuccessful;
#endif

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalGetObjectDataPtr()
//
// Description: Function encapsulate access of function EplObdGetObjectDataPtr
//
// Parameters:  uiIndex_p    =   Index of the entry
//              uiSubindex_p =   Subindex of the entry
//
// Return:      void *    = pointer to object data
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT void *PUBLIC EplObduCalGetObjectDataPtr(unsigned int uiIndex_p,
						     unsigned int uiSubIndex_p)
{
	void *pData;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	pData = EplObdGetObjectDataPtr(uiIndex_p, uiSubIndex_p);
#else
	pData = NULL;
#endif

	return pData;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalRegisterUserOd()
//
// Description: Function encapsulate access of function EplObdRegisterUserOd
//
// Parameters:  pUserOd_p   = pointer to user OD
//
// Return:     tEplKernel = errorcode
//
// State:
//
//---------------------------------------------------------------------------
#if (defined (EPL_OBD_USER_OD) && (EPL_OBD_USER_OD != FALSE))
EPLDLLEXPORT tEplKernel PUBLIC EplObduCalRegisterUserOd(tEplObdEntryPtr
							pUserOd_p)
{
	tEplKernel Ret;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Ret = EplObdRegisterUserOd(pUserOd_p);
#else
	Ret = kEplSuccessful;
#endif

	return Ret;

}
#endif
//---------------------------------------------------------------------------
//
// Function:    EplObduCalInitVarEntry()
//
// Description: Function encapsulate access of function EplObdInitVarEntry
//
// Parameters:  pVarEntry_p = pointer to var entry structure
//              bType_p     = object type
//              ObdSize_p   = size of object data
//
// Returns:     none
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT void PUBLIC EplObduCalInitVarEntry(tEplObdVarEntry MEM *
						pVarEntry_p, BYTE bType_p,
						tEplObdSize ObdSize_p)
{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	EplObdInitVarEntry(pVarEntry_p, bType_p, ObdSize_p);
#endif
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalGetDataSize()
//
// Description: Function encapsulate access of function EplObdGetDataSize
//
//              gets the data size of an object
//              for string objects it returnes the string length
//
// Parameters:  uiIndex_p   =   Index
//              uiSubIndex_p=   Subindex
//
// Return:      tEplObdSize
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplObdSize PUBLIC EplObduCalGetDataSize(unsigned int uiIndex_p,
						      unsigned int uiSubIndex_p)
{
	tEplObdSize Size;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Size = EplObdGetDataSize(uiIndex_p, uiSubIndex_p);
#else
	Size = 0;
#endif

	return Size;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalGetNodeId()
//
// Description: Function encapsulate access of function EplObdGetNodeId
//
//
// Parameters:
//
// Return:      unsigned int = Node Id
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT unsigned int PUBLIC EplObduCalGetNodeId()
{
	unsigned int uiNodeId;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	uiNodeId = EplObdGetNodeId();
#else
	uiNodeId = 0;
#endif

	return uiNodeId;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalSetNodeId()
//
// Description: Function encapsulate access of function EplObdSetNodeId
//
//
// Parameters:  uiNodeId_p  =   Node Id to set
//              NodeIdType_p=   Type on which way the Node Id was set
//
// Return:      tEplKernel = Errorcode
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplObduCalSetNodeId(unsigned int uiNodeId_p,
						   tEplObdNodeIdType
						   NodeIdType_p)
{
	tEplKernel Ret;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Ret = EplObdSetNodeId(uiNodeId_p, NodeIdType_p);
#else
	Ret = kEplSuccessful;
#endif

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalGetAccessType()
//
// Description: Function encapsulate access of function EplObdGetAccessType
//
// Parameters:  uiIndex_p       =   Index of the OD entry
//              uiSubIndex_p    =   Subindex of the OD Entry
//              pAccessTyp_p    =   pointer to buffer to store accesstype
//
// Return:      tEplKernel      =   errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplObduCalGetAccessType(unsigned int uiIndex_p,
						       unsigned int
						       uiSubIndex_p,
						       tEplObdAccess *
						       pAccessTyp_p)
{
	tEplObdAccess AccesType;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	AccesType = EplObdGetAccessType(uiIndex_p, uiSubIndex_p, pAccessTyp_p);
#else
	AccesType = 0;
#endif

	return AccesType;

}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalReadEntryToLe()
//
// Description: Function encapsulate access of function EplObdReadEntryToLe
//
// Parameters:  uiIndex_p       = Index of the OD entry to read
//              uiSubIndex_p    = Subindex to read
//              pDstData_p      = pointer to the buffer for data
//              Offset_p        = offset in data for read access
//              pSize_p         = IN: Size of the buffer
//                                OUT: number of readed Bytes
//
// Return:      tEplKernel
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplObduCalReadEntryToLe(unsigned int uiIndex_p,
						       unsigned int
						       uiSubIndex_p,
						       void *pDstData_p,
						       tEplObdSize * pSize_p)
{
	tEplKernel Ret;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Ret = EplObdReadEntryToLe(uiIndex_p, uiSubIndex_p, pDstData_p, pSize_p);
#else
	Ret = kEplSuccessful;
#endif

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalWriteEntryFromLe()
//
// Description: Function encapsulate access of function EplObdWriteEntryFromLe
//
// Parameters:  uiIndex_p       =   Index of the OD entry
//              uiSubIndex_p    =   Subindex of the OD Entry
//              pSrcData_p      =   Pointer to the data to write
//              Size_p          =   Size of the data in Byte
//
// Return:      tEplKernel      =   Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplObduCalWriteEntryFromLe(unsigned int
							  uiIndex_p,
							  unsigned int
							  uiSubIndex_p,
							  void *pSrcData_p,
							  tEplObdSize Size_p)
{
	tEplKernel Ret;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Ret =
	    EplObdWriteEntryFromLe(uiIndex_p, uiSubIndex_p, pSrcData_p, Size_p);
#else
	Ret = kEplSuccessful;
#endif
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduCalSearchVarEntry()
//
// Description: gets variable from OD
//
// Parameters:  uiIndex_p       =   index of the var entry to search
//              uiSubindex_p    =   subindex of var entry to search
//              ppVarEntry_p    =   pointer to the pointer to the varentry
//
// Return:      tEplKernel
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC
EplObduCalSearchVarEntry(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
			 unsigned int uiSubindex_p,
			 tEplObdVarEntry MEM ** ppVarEntry_p)
{
	tEplKernel Ret;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	Ret = EplObdSearchVarEntry(uiIndex_p, uiSubindex_p, ppVarEntry_p);
#else
	Ret = kEplSuccessful;
#endif
	return Ret;
}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:
//
// Description:
//
//
//
// Parameters:
//
//
// Returns:
//
//
// State:
//
//---------------------------------------------------------------------------

#endif //(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)

// EOF
