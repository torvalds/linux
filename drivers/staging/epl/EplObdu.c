/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for Epl-Obd-Userspace-module

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

                $RCSfile: EplObdu.c,v $

                $Author: D.Krueger $

                $Revision: 1.5 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/19 k.t.:   start of the implementation

****************************************************************************/

#include "EplInc.h"
#include "user/EplObdu.h"
#include "user/EplObduCal.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)
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
// Function:    EplObduWriteEntry()
//
// Description: Function writes data to an OBD entry. Strings
//              are stored with added '\0' character.
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
tEplKernel EplObduWriteEntry(unsigned int uiIndex_p,
			     unsigned int uiSubIndex_p,
			     void *pSrcData_p, tEplObdSize Size_p)
{
	tEplKernel Ret;

	Ret = EplObduCalWriteEntry(uiIndex_p, uiSubIndex_p, pSrcData_p, Size_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduReadEntry()
//
// Description: The function reads an object entry. The application
//              can always read the data even if attrib kEplObdAccRead
//              is not set. The attrib is only checked up for SDO transfer.
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
tEplKernel EplObduReadEntry(unsigned int uiIndex_p,
			    unsigned int uiSubIndex_p,
			    void *pDstData_p,
			    tEplObdSize *pSize_p)
{
	tEplKernel Ret;

	Ret = EplObduCalReadEntry(uiIndex_p, uiSubIndex_p, pDstData_p, pSize_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObdAccessOdPart()
//
// Description: restores default values of one part of OD
//
// Parameters:  ObdPart_p       = od-part to reset
//              Direction_p     = directory flag for
//
// Return:      tEplKernel  = errorcode
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplObduAccessOdPart(tEplObdPart ObdPart_p, tEplObdDir Direction_p)
{
	tEplKernel Ret;

	Ret = EplObduCalAccessOdPart(ObdPart_p, Direction_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduDefineVar()
//
// Description: defines a variable in OD
//
// Parameters:  pEplVarParam_p = varentry
//
// Return:      tEplKernel  =   errorcode
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplObduDefineVar(tEplVarParam *pVarParam_p)
{
	tEplKernel Ret;

	Ret = EplObduCalDefineVar(pVarParam_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduGetObjectDataPtr()
//
// Description: It returnes the current data pointer. But if object is an
//              constant object it returnes the default pointer.
//
// Parameters:  uiIndex_p    =   Index of the entry
//              uiSubindex_p =   Subindex of the entry
//
// Return:      void *    = pointer to object data
//
// State:
//
//---------------------------------------------------------------------------
void *EplObduGetObjectDataPtr(unsigned int uiIndex_p, unsigned int uiSubIndex_p)
{
	void *pData;

	pData = EplObduCalGetObjectDataPtr(uiIndex_p, uiSubIndex_p);

	return pData;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduRegisterUserOd()
//
// Description: function registers the user OD
//
// Parameters:  pUserOd_p   =pointer to user ODd
//
// Return:     tEplKernel = errorcode
//
// State:
//
//---------------------------------------------------------------------------
#if (defined (EPL_OBD_USER_OD) && (EPL_OBD_USER_OD != FALSE))
tEplKernel EplObduRegisterUserOd(tEplObdEntryPtr pUserOd_p)
{
	tEplKernel Ret;

	Ret = EplObduCalRegisterUserOd(pUserOd_p);

	return Ret;

}
#endif
//---------------------------------------------------------------------------
//
// Function:    EplObduInitVarEntry()
//
// Description: function to initialize VarEntry dependened on object type
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
void EplObduInitVarEntry(tEplObdVarEntry *pVarEntry_p, u8 bType_p, tEplObdSize ObdSize_p)
{
	EplObduCalInitVarEntry(pVarEntry_p, bType_p, ObdSize_p);
}

//---------------------------------------------------------------------------
//
// Function:    EplObduGetDataSize()
//
// Description: function to initialize VarEntry dependened on object type
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
tEplObdSize EplObduGetDataSize(unsigned int uiIndex_p, unsigned int uiSubIndex_p)
{
	tEplObdSize Size;

	Size = EplObduCalGetDataSize(uiIndex_p, uiSubIndex_p);

	return Size;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduGetNodeId()
//
// Description: function returns nodeid from entry 0x1F93
//
//
// Parameters:
//
// Return:      unsigned int = Node Id
//
// State:
//
//---------------------------------------------------------------------------
unsigned int EplObduGetNodeId(void)
{
	unsigned int uiNodeId;

	uiNodeId = EplObduCalGetNodeId();

	return uiNodeId;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduSetNodeId()
//
// Description: function sets nodeid in entry 0x1F93
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
tEplKernel EplObduSetNodeId(unsigned int uiNodeId_p, tEplObdNodeIdType NodeIdType_p)
{
	tEplKernel Ret;

	Ret = EplObduCalSetNodeId(uiNodeId_p, NodeIdType_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduGetAccessType()
//
// Description: Function returns accesstype of the entry
//
// Parameters:  uiIndex_p       =   Index of the OD entry
//              uiSubIndex_p    =   Subindex of the OD Entry
//              pAccessTyp_p    =   pointer to buffer to store accesstyp
//
// Return:      tEplKernel      =   Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplObduGetAccessType(unsigned int uiIndex_p,
				unsigned int uiSubIndex_p,
				tEplObdAccess *pAccessTyp_p)
{
	tEplObdAccess AccessType;

	AccessType =
	    EplObduCalGetAccessType(uiIndex_p, uiSubIndex_p, pAccessTyp_p);

	return AccessType;
}

//---------------------------------------------------------------------------
//
// Function:    EplObdReaduEntryToLe()
//
// Description: The function reads an object entry from the byteoder
//              of the system to the little endian byteorder for numeric values.
//              For other types a normal read will be processed. This is usefull for
//              the PDO and SDO module. The application
//              can always read the data even if attrib kEplObdAccRead
//              is not set. The attrib is only checked up for SDO transfer.
//
// Parameters:  EPL_MCO_DECL_INSTANCE_PTR_
//              uiIndex_p       = Index of the OD entry to read
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
tEplKernel EplObduReadEntryToLe(unsigned int uiIndex_p,
				unsigned int uiSubIndex_p,
				void *pDstData_p,
				tEplObdSize *pSize_p)
{
	tEplKernel Ret;

	Ret =
	    EplObduCalReadEntryToLe(uiIndex_p, uiSubIndex_p, pDstData_p,
				    pSize_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduWriteEntryFromLe()
//
// Description: Function writes data to an OBD entry from a source with
//              little endian byteorder to the od with system specuific
//              byteorder. Not numeric values will only by copied. Strings
//              are stored with added '\0' character.
//
// Parameters:  EPL_MCO_DECL_INSTANCE_PTR_
//              uiIndex_p       =   Index of the OD entry
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
tEplKernel EplObduWriteEntryFromLe(unsigned int uiIndex_p,
				   unsigned int uiSubIndex_p,
				   void *pSrcData_p,
				   tEplObdSize Size_p)
{
	tEplKernel Ret;

	Ret =
	    EplObduCalWriteEntryFromLe(uiIndex_p, uiSubIndex_p, pSrcData_p,
				       Size_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplObduSearchVarEntry()
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
tEplKernel EplObduSearchVarEntry(EPL_MCO_DECL_INSTANCE_PTR_ unsigned int uiIndex_p,
				 unsigned int uiSubindex_p,
				 tEplObdVarEntry **ppVarEntry_p)
{
	tEplKernel Ret;

	Ret = EplObduCalSearchVarEntry(uiIndex_p, uiSubindex_p, ppVarEntry_p);

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

#endif // #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)

// EOF
