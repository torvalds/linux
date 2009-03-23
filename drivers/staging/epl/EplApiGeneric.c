/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for generic EPL API module

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

                $RCSfile: EplApiGeneric.c,v $

                $Author: D.Krueger $

                $Revision: 1.21 $  $Date: 2008/11/21 09:00:38 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/09/05 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#include "Epl.h"
#include "kernel/EplDllk.h"
#include "kernel/EplErrorHandlerk.h"
#include "kernel/EplEventk.h"
#include "kernel/EplNmtk.h"
#include "kernel/EplObdk.h"
#include "kernel/EplTimerk.h"
#include "kernel/EplDllkCal.h"
#include "kernel/EplPdokCal.h"
#include "user/EplDlluCal.h"
#include "user/EplLedu.h"
#include "user/EplNmtCnu.h"
#include "user/EplNmtMnu.h"
#include "user/EplSdoComu.h"
#include "user/EplIdentu.h"
#include "user/EplStatusu.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
#include "kernel/EplPdok.h"
#endif

#include "SharedBuff.h"

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) == 0)
#error "EPL API layer needs EPL module OBDK!"
#endif

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

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  EplApi                                              */
/*                                                                         */
/*                                                                         */
/***************************************************************************/
//
// Description:
//
//
/***************************************************************************/

//=========================================================================//
//                                                                         //
//          P R I V A T E   D E F I N I T I O N S                          //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

typedef struct {
	tEplApiInitParam m_InitParam;

} tEplApiInstance;

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

static tEplApiInstance EplApiInstance_g;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

// NMT state change event callback function
static tEplKernel EplApiCbNmtStateChange(tEplEventNmtStateChange NmtStateChange_p);

// update DLL configuration from OD
static tEplKernel EplApiUpdateDllConfig(BOOL fUpdateIdentity_p);

// update OD from init param
static tEplKernel EplApiUpdateObd(void);

// process events from user event queue
static tEplKernel EplApiProcessEvent(tEplEvent *pEplEvent_p);

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
// callback function of SDO module
static tEplKernel EplApiCbSdoCon(tEplSdoComFinished *pSdoComFinished_p);
#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
// callback functions of NmtMnu module
static tEplKernel EplApiCbNodeEvent(unsigned int uiNodeId_p,
				    tEplNmtNodeEvent NodeEvent_p,
				    tEplNmtState NmtState_p,
				    u16 wErrorCode_p, BOOL fMandatory_p);

static tEplKernel EplApiCbBootEvent(tEplNmtBootEvent BootEvent_p,
				    tEplNmtState NmtState_p,
				    u16 wErrorCode_p);
#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_LEDU)) != 0)
// callback function of Ledu module
static tEplKernel EplApiCbLedStateChange(tEplLedType LedType_p, BOOL fOn_p);
#endif

// OD initialization function (implemented in Objdict.c)
tEplKernel EplObdInitRam(tEplObdInitParam *pInitParam_p);

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplApiInitialize()
//
// Description: add and initialize new instance of EPL stack.
//              After return from this function the application must start
//              the NMT state machine via
//              EplApiExecNmtCommand(kEplNmtEventSwReset)
//              and thereby the whole EPL stack :-)
//
// Parameters:  pInitParam_p            = initialisation parameters
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplApiInitialize(tEplApiInitParam *pInitParam_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplObdInitParam ObdInitParam;
	tEplDllkInitParam DllkInitParam;
#ifndef EPL_NO_FIFO
	tShbError ShbError;
#endif

	// reset instance structure
	EPL_MEMSET(&EplApiInstance_g, 0, sizeof(EplApiInstance_g));

	EPL_MEMCPY(&EplApiInstance_g.m_InitParam, pInitParam_p,
		   min(sizeof(tEplApiInitParam),
		       pInitParam_p->m_uiSizeOfStruct));

	// check event callback function pointer
	if (EplApiInstance_g.m_InitParam.m_pfnCbEvent == NULL) {	// application must always have an event callback function
		Ret = kEplApiInvalidParam;
		goto Exit;
	}
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) != 0)
	// init OD
// FIXME
//    Ret = EplObdInitRam(&ObdInitParam);
//    if (Ret != kEplSuccessful)
//    {
//        goto Exit;
//    }

	// initialize EplObd module
	Ret = EplObdInit(&ObdInitParam);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

#ifndef EPL_NO_FIFO
	ShbError = ShbInit();
	if (ShbError != kShbOk) {
		Ret = kEplNoResource;
		goto Exit;
	}
#endif

	// initialize EplEventk module
	Ret = EplEventkInit(EplApiInstance_g.m_InitParam.m_pfnCbSync);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// initialize EplEventu module
	Ret = EplEventuInit(EplApiProcessEvent);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// init EplTimerk module
	Ret = EplTimerkInit();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// initialize EplNmtk module before DLL
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) != 0)
	Ret = EplNmtkInit();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

	// initialize EplDllk module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
	EPL_MEMCPY(DllkInitParam.m_be_abSrcMac,
		   EplApiInstance_g.m_InitParam.m_abMacAddress, 6);
	Ret = EplDllkAddInstance(&DllkInitParam);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// initialize EplErrorHandlerk module
	Ret = EplErrorHandlerkInit();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// initialize EplDllkCal module
	Ret = EplDllkCalAddInstance();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

	// initialize EplDlluCal module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLU)) != 0)
	Ret = EplDlluCalAddInstance();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

	// initialize EplPdok module
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
	Ret = EplPdokAddInstance();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}

	Ret = EplPdokCalAddInstance();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

	// initialize EplNmtCnu module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_CN)) != 0)
	Ret = EplNmtCnuAddInstance(EplApiInstance_g.m_InitParam.m_uiNodeId);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

	// initialize EplNmtu module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)
	Ret = EplNmtuInit();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// register NMT event callback function
	Ret = EplNmtuRegisterStateChangeCb(EplApiCbNmtStateChange);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	// initialize EplNmtMnu module
	Ret = EplNmtMnuInit(EplApiCbNodeEvent, EplApiCbBootEvent);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// initialize EplIdentu module
	Ret = EplIdentuInit();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// initialize EplStatusu module
	Ret = EplStatusuInit();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

	// initialize EplLedu module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_LEDU)) != 0)
	Ret = EplLeduInit(EplApiCbLedStateChange);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

	// init SDO module
#if ((((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0) || \
     (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0))
	// init sdo command layer
	Ret = EplSdoComInit();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

	// the application must start NMT state machine
	// via EplApiExecNmtCommand(kEplNmtEventSwReset)
	// and thereby the whole EPL stack

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplApiShutdown()
//
// Description: deletes an instance of EPL stack
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplApiShutdown(void)
{
	tEplKernel Ret = kEplSuccessful;

	// $$$ d.k.: check if NMT state is NMT_GS_OFF

	// $$$ d.k.: maybe delete event queues at first, but this implies that
	//           no other module must not use the event queues for communication
	//           during shutdown.

	// delete instance for all modules

	// deinitialize EplSdoCom module
#if ((((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0) || \
     (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0))
	Ret = EplSdoComDelInstance();
//    PRINTF1("EplSdoComDelInstance():  0x%X\n", Ret);
#endif

	// deinitialize EplLedu module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_LEDU)) != 0)
	Ret = EplLeduDelInstance();
//    PRINTF1("EplLeduDelInstance():    0x%X\n", Ret);
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	// deinitialize EplNmtMnu module
	Ret = EplNmtMnuDelInstance();
//    PRINTF1("EplNmtMnuDelInstance():  0x%X\n", Ret);

	// deinitialize EplIdentu module
	Ret = EplIdentuDelInstance();
//    PRINTF1("EplIdentuDelInstance():  0x%X\n", Ret);

	// deinitialize EplStatusu module
	Ret = EplStatusuDelInstance();
//    PRINTF1("EplStatusuDelInstance():  0x%X\n", Ret);
#endif

	// deinitialize EplNmtCnu module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_CN)) != 0)
	Ret = EplNmtCnuDelInstance();
//    PRINTF1("EplNmtCnuDelInstance():  0x%X\n", Ret);
#endif

	// deinitialize EplNmtu module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)
	Ret = EplNmtuDelInstance();
//    PRINTF1("EplNmtuDelInstance():    0x%X\n", Ret);
#endif

	// deinitialize EplDlluCal module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLU)) != 0)
	Ret = EplDlluCalDelInstance();
//    PRINTF1("EplDlluCalDelInstance(): 0x%X\n", Ret);

#endif

	// deinitialize EplEventu module
	Ret = EplEventuDelInstance();
//    PRINTF1("EplEventuDelInstance():  0x%X\n", Ret);

	// deinitialize EplNmtk module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) != 0)
	Ret = EplNmtkDelInstance();
//    PRINTF1("EplNmtkDelInstance():    0x%X\n", Ret);
#endif

	// deinitialize EplDllk module
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
	Ret = EplDllkDelInstance();
//    PRINTF1("EplDllkDelInstance():    0x%X\n", Ret);

	// deinitialize EplDllkCal module
	Ret = EplDllkCalDelInstance();
//    PRINTF1("EplDllkCalDelInstance(): 0x%X\n", Ret);
#endif

	// deinitialize EplEventk module
	Ret = EplEventkDelInstance();
//    PRINTF1("EplEventkDelInstance():  0x%X\n", Ret);

	// deinitialize EplTimerk module
	Ret = EplTimerkDelInstance();
//    PRINTF1("EplTimerkDelInstance():  0x%X\n", Ret);

#ifndef EPL_NO_FIFO
	ShbExit();
#endif

	return Ret;
}

//----------------------------------------------------------------------------
// Function:    EplApiExecNmtCommand()
//
// Description: executes a NMT command, i.e. post the NMT command/event to the
//              NMTk module. NMT commands which are not appropriate in the current
//              NMT state are silently ignored. Please keep in mind that the
//              NMT state may change until the NMT command is actually executed.
//
// Parameters:  NmtEvent_p              = NMT command/event
//
// Returns:     tEplKernel              = error code
//
// State:
//----------------------------------------------------------------------------

tEplKernel EplApiExecNmtCommand(tEplNmtEvent NmtEvent_p)
{
	tEplKernel Ret = kEplSuccessful;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)
	Ret = EplNmtuNmtEvent(NmtEvent_p);
#endif

	return Ret;
}

//----------------------------------------------------------------------------
// Function:    EplApiLinkObject()
//
// Description: Function maps array of application variables onto specified object in OD
//
// Parameters:  uiObjIndex_p            = Function maps variables for this object index
//              pVar_p                  = Pointer to data memory area for the specified object
//              puiVarEntries_p         = IN: pointer to number of entries to map
//                                        OUT: pointer to number of actually used entries
//              pEntrySize_p            = IN: pointer to size of one entry;
//                                            if size is zero, the actual size will be read from OD
//                                        OUT: pointer to entire size of all entries mapped
//              uiFirstSubindex_p       = This is the first subindex to be mapped.
//
// Returns:     tEplKernel              = error code
//
// State:
//----------------------------------------------------------------------------

tEplKernel EplApiLinkObject(unsigned int uiObjIndex_p,
			    void *pVar_p,
			    unsigned int *puiVarEntries_p,
			    tEplObdSize *pEntrySize_p,
			    unsigned int uiFirstSubindex_p)
{
	u8 bVarEntries;
	u8 bIndexEntries;
	u8 *pbData;
	unsigned int uiSubindex;
	tEplVarParam VarParam;
	tEplObdSize EntrySize;
	tEplObdSize UsedSize;

	tEplKernel RetCode = kEplSuccessful;

	if ((pVar_p == NULL)
	    || (puiVarEntries_p == NULL)
	    || (*puiVarEntries_p == 0)
	    || (pEntrySize_p == NULL)) {
		RetCode = kEplApiInvalidParam;
		goto Exit;
	}

	pbData = (u8 *)pVar_p;
	bVarEntries = (u8) * puiVarEntries_p;
	UsedSize = 0;

	// init VarParam structure with default values
	VarParam.m_uiIndex = uiObjIndex_p;
	VarParam.m_ValidFlag = kVarValidAll;

	if (uiFirstSubindex_p != 0) {	// check if object exists by reading subindex 0x00,
		// because user wants to link a variable to a subindex unequal 0x00
		// read number of entries
		EntrySize = (tEplObdSize) sizeof(bIndexEntries);
		RetCode = EplObdReadEntry(uiObjIndex_p,
					  0x00,
					  (void *)&bIndexEntries,
					  &EntrySize);

		if ((RetCode != kEplSuccessful) || (bIndexEntries == 0x00)) {
			// Object doesn't exist or invalid entry number
			RetCode = kEplObdIndexNotExist;
			goto Exit;
		}
	} else {		// user wants to link a variable to subindex 0x00
		// that's OK
		bIndexEntries = 0;
	}

	// Correct number of entries if number read from OD is greater
	// than the specified number.
	// This is done, so that we do not set more entries than subindexes the
	// object actually has.
	if ((bIndexEntries > (bVarEntries + uiFirstSubindex_p - 1)) &&
	    (bVarEntries != 0x00)) {
		bIndexEntries = (u8) (bVarEntries + uiFirstSubindex_p - 1);
	}
	// map entries
	for (uiSubindex = uiFirstSubindex_p; uiSubindex <= bIndexEntries;
	     uiSubindex++) {
		// if passed entry size is 0, then get size from OD
		if (*pEntrySize_p == 0x00) {
			// read entry size
			EntrySize = EplObdGetDataSize(uiObjIndex_p, uiSubindex);

			if (EntrySize == 0x00) {
				// invalid entry size (maybe object doesn't exist or entry of type DOMAIN is empty)
				RetCode = kEplObdSubindexNotExist;
				break;
			}
		} else {	// use passed entry size
			EntrySize = *pEntrySize_p;
		}

		VarParam.m_uiSubindex = uiSubindex;

		// set pointer to user var
		VarParam.m_Size = EntrySize;
		VarParam.m_pData = pbData;

		UsedSize += EntrySize;
		pbData += EntrySize;

		RetCode = EplObdDefineVar(&VarParam);
		if (RetCode != kEplSuccessful) {
			break;
		}
	}

	// set number of mapped entries and entry size
	*puiVarEntries_p = ((bIndexEntries - uiFirstSubindex_p) + 1);
	*pEntrySize_p = UsedSize;

      Exit:

	return (RetCode);

}

// ----------------------------------------------------------------------------
//
// Function:    EplApiReadObject()
//
// Description: reads the specified entry from the OD of the specified node.
//              If this node is a remote node, it performs a SDO transfer, which
//              means this function returns kEplApiTaskDeferred and the application
//              is informed via the event callback function when the task is completed.
//
// Parameters:  pSdoComConHdl_p         = INOUT: pointer to SDO connection handle (may be NULL in case of local OD access)
//              uiNodeId_p              = IN: node ID (0 = itself)
//              uiIndex_p               = IN: index of object in OD
//              uiSubindex_p            = IN: sub-index of object in OD
//              pDstData_le_p           = OUT: pointer to data in little endian
//              puiSize_p               = INOUT: pointer to size of data
//              SdoType_p               = IN: type of SDO transfer
//              pUserArg_p              = IN: user-definable argument pointer,
//                                            which will be passed to the event callback function
//
// Return:      tEplKernel              = error code
//
// ----------------------------------------------------------------------------

tEplKernel EplApiReadObject(tEplSdoComConHdl *pSdoComConHdl_p,
			    unsigned int uiNodeId_p,
			    unsigned int uiIndex_p,
			    unsigned int uiSubindex_p,
			    void *pDstData_le_p,
			    unsigned int *puiSize_p,
			    tEplSdoType SdoType_p, void *pUserArg_p)
{
	tEplKernel Ret = kEplSuccessful;

	if ((uiIndex_p == 0) || (pDstData_le_p == NULL) || (puiSize_p == NULL)
	    || (*puiSize_p == 0)) {
		Ret = kEplApiInvalidParam;
		goto Exit;
	}

	if (uiNodeId_p == 0 || uiNodeId_p == EplObdGetNodeId()) {	// local OD access can be performed
		tEplObdSize ObdSize;

		ObdSize = (tEplObdSize) * puiSize_p;
		Ret =
		    EplObdReadEntryToLe(uiIndex_p, uiSubindex_p, pDstData_le_p,
					&ObdSize);
		*puiSize_p = (unsigned int)ObdSize;
	} else {		// perform SDO transfer
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
		tEplSdoComTransParamByIndex TransParamByIndex;
//    tEplSdoComConHdl            SdoComConHdl;

		// check if application provides space for handle
		if (pSdoComConHdl_p == NULL) {
			Ret = kEplApiInvalidParam;
			goto Exit;
//            pSdoComConHdl_p = &SdoComConHdl;
		}
		// init command layer connection
		Ret = EplSdoComDefineCon(pSdoComConHdl_p, uiNodeId_p,	// target node id
					 SdoType_p);	// SDO type
		if ((Ret != kEplSuccessful) && (Ret != kEplSdoComHandleExists)) {
			goto Exit;
		}
		TransParamByIndex.m_pData = pDstData_le_p;
		TransParamByIndex.m_SdoAccessType = kEplSdoAccessTypeRead;
		TransParamByIndex.m_SdoComConHdl = *pSdoComConHdl_p;
		TransParamByIndex.m_uiDataSize = *puiSize_p;
		TransParamByIndex.m_uiIndex = uiIndex_p;
		TransParamByIndex.m_uiSubindex = uiSubindex_p;
		TransParamByIndex.m_pfnSdoFinishedCb = EplApiCbSdoCon;
		TransParamByIndex.m_pUserArg = pUserArg_p;

		Ret = EplSdoComInitTransferByIndex(&TransParamByIndex);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
		Ret = kEplApiTaskDeferred;

#else
		Ret = kEplApiInvalidParam;
#endif
	}

      Exit:
	return Ret;
}

// ----------------------------------------------------------------------------
//
// Function:    EplApiWriteObject()
//
// Description: writes the specified entry to the OD of the specified node.
//              If this node is a remote node, it performs a SDO transfer, which
//              means this function returns kEplApiTaskDeferred and the application
//              is informed via the event callback function when the task is completed.
//
// Parameters:  pSdoComConHdl_p         = INOUT: pointer to SDO connection handle (may be NULL in case of local OD access)
//              uiNodeId_p              = IN: node ID (0 = itself)
//              uiIndex_p               = IN: index of object in OD
//              uiSubindex_p            = IN: sub-index of object in OD
//              pSrcData_le_p           = IN: pointer to data in little endian
//              uiSize_p                = IN: size of data in bytes
//              SdoType_p               = IN: type of SDO transfer
//              pUserArg_p              = IN: user-definable argument pointer,
//                                            which will be passed to the event callback function
//
// Return:      tEplKernel              = error code
//
// ----------------------------------------------------------------------------

tEplKernel EplApiWriteObject(tEplSdoComConHdl *pSdoComConHdl_p,
			     unsigned int uiNodeId_p,
			     unsigned int uiIndex_p,
			     unsigned int uiSubindex_p,
			     void *pSrcData_le_p,
			     unsigned int uiSize_p,
			     tEplSdoType SdoType_p, void *pUserArg_p)
{
	tEplKernel Ret = kEplSuccessful;

	if ((uiIndex_p == 0) || (pSrcData_le_p == NULL) || (uiSize_p == 0)) {
		Ret = kEplApiInvalidParam;
		goto Exit;
	}

	if (uiNodeId_p == 0 || uiNodeId_p == EplObdGetNodeId()) {	// local OD access can be performed

		Ret =
		    EplObdWriteEntryFromLe(uiIndex_p, uiSubindex_p,
					   pSrcData_le_p, uiSize_p);
	} else {		// perform SDO transfer
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
		tEplSdoComTransParamByIndex TransParamByIndex;
//    tEplSdoComConHdl            SdoComConHdl;

		// check if application provides space for handle
		if (pSdoComConHdl_p == NULL) {
			Ret = kEplApiInvalidParam;
			goto Exit;
//            pSdoComConHdl_p = &SdoComConHdl;
		}
		// d.k.: How to recycle command layer connection?
		//       Try to redefine it, which will return kEplSdoComHandleExists
		//       and the existing command layer handle.
		//       If the returned handle is busy, EplSdoComInitTransferByIndex()
		//       will return with error.
		// $$$ d.k.: Collisions may occur with Configuration Manager, if both the application and
		//           Configuration Manager, are trying to communicate with the very same node.
		//     possible solution: disallow communication by application if Configuration Manager is busy

		// init command layer connection
		Ret = EplSdoComDefineCon(pSdoComConHdl_p, uiNodeId_p,	// target node id
					 SdoType_p);	// SDO type
		if ((Ret != kEplSuccessful) && (Ret != kEplSdoComHandleExists)) {
			goto Exit;
		}
		TransParamByIndex.m_pData = pSrcData_le_p;
		TransParamByIndex.m_SdoAccessType = kEplSdoAccessTypeWrite;
		TransParamByIndex.m_SdoComConHdl = *pSdoComConHdl_p;
		TransParamByIndex.m_uiDataSize = uiSize_p;
		TransParamByIndex.m_uiIndex = uiIndex_p;
		TransParamByIndex.m_uiSubindex = uiSubindex_p;
		TransParamByIndex.m_pfnSdoFinishedCb = EplApiCbSdoCon;
		TransParamByIndex.m_pUserArg = pUserArg_p;

		Ret = EplSdoComInitTransferByIndex(&TransParamByIndex);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
		Ret = kEplApiTaskDeferred;

#else
		Ret = kEplApiInvalidParam;
#endif
	}

      Exit:
	return Ret;
}

// ----------------------------------------------------------------------------
//
// Function:    EplApiFreeSdoChannel()
//
// Description: frees the specified SDO channel.
//              This function must be called after each call to EplApiReadObject()/EplApiWriteObject()
//              which returns kEplApiTaskDeferred and the application
//              is informed via the event callback function when the task is completed.
//
// Parameters:  SdoComConHdl_p          = IN: SDO connection handle
//
// Return:      tEplKernel              = error code
//
// ----------------------------------------------------------------------------

tEplKernel EplApiFreeSdoChannel(tEplSdoComConHdl SdoComConHdl_p)
{
	tEplKernel Ret = kEplSuccessful;

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)

	// init command layer connection
	Ret = EplSdoComUndefineCon(SdoComConHdl_p);

#else
	Ret = kEplApiInvalidParam;
#endif

	return Ret;
}

// ----------------------------------------------------------------------------
//
// Function:    EplApiReadLocalObject()
//
// Description: reads the specified entry from the local OD.
//
// Parameters:  uiIndex_p               = IN: index of object in OD
//              uiSubindex_p            = IN: sub-index of object in OD
//              pDstData_p              = OUT: pointer to data in platform byte order
//              puiSize_p               = INOUT: pointer to size of data
//
// Return:      tEplKernel              = error code
//
// ----------------------------------------------------------------------------

tEplKernel EplApiReadLocalObject(unsigned int uiIndex_p,
				 unsigned int uiSubindex_p,
				 void *pDstData_p, unsigned int *puiSize_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplObdSize ObdSize;

	ObdSize = (tEplObdSize) * puiSize_p;
	Ret = EplObdReadEntry(uiIndex_p, uiSubindex_p, pDstData_p, &ObdSize);
	*puiSize_p = (unsigned int)ObdSize;

	return Ret;
}

// ----------------------------------------------------------------------------
//
// Function:    EplApiWriteLocalObject()
//
// Description: writes the specified entry to the local OD.
//
// Parameters:  uiIndex_p               = IN: index of object in OD
//              uiSubindex_p            = IN: sub-index of object in OD
//              pSrcData_p              = IN: pointer to data in platform byte order
//              uiSize_p                = IN: size of data in bytes
//
// Return:      tEplKernel              = error code
//
// ----------------------------------------------------------------------------

tEplKernel EplApiWriteLocalObject(unsigned int uiIndex_p,
				  unsigned int uiSubindex_p,
				  void *pSrcData_p,
				  unsigned int uiSize_p)
{
	tEplKernel Ret = kEplSuccessful;

	Ret =
	    EplObdWriteEntry(uiIndex_p, uiSubindex_p, pSrcData_p,
			     (tEplObdSize) uiSize_p);

	return Ret;
}

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
// ----------------------------------------------------------------------------
//
// Function:    EplApiMnTriggerStateChange()
//
// Description: triggers the specified node command for the specified node.
//
// Parameters:  uiNodeId_p              = node ID for which the node command will be executed
//              NodeCommand_p           = node command
//
// Return:      tEplKernel              = error code
//
// ----------------------------------------------------------------------------

tEplKernel EplApiMnTriggerStateChange(unsigned int uiNodeId_p,
				      tEplNmtNodeCommand NodeCommand_p)
{
	tEplKernel Ret = kEplSuccessful;

	Ret = EplNmtMnuTriggerStateChange(uiNodeId_p, NodeCommand_p);

	return Ret;
}

#endif // (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

//---------------------------------------------------------------------------
//
// Function:    EplApiCbObdAccess
//
// Description: callback function for OD accesses
//
// Parameters:  pParam_p                = OBD parameter
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplApiCbObdAccess(tEplObdCbParam *pParam_p)
{
	tEplKernel Ret = kEplSuccessful;

#if (EPL_API_OBD_FORWARD_EVENT != FALSE)
	tEplApiEventArg EventArg;

	// call user callback
	// must be disabled for EplApiLinuxKernel.c, because of reentrancy problem
	// for local OD access. This is not so bad as user callback function in
	// application does not use OD callbacks at the moment.
	EventArg.m_ObdCbParam = *pParam_p;
	Ret = EplApiInstance_g.m_InitParam.m_pfnCbEvent(kEplApiEventObdAccess,
							&EventArg,
							EplApiInstance_g.
							m_InitParam.
							m_pEventUserArg);
#endif

	switch (pParam_p->m_uiIndex) {
		//case 0x1006:    // NMT_CycleLen_U32 (valid on reset)
	case 0x1C14:		// DLL_LossOfFrameTolerance_U32
		//case 0x1F98:    // NMT_CycleTiming_REC (valid on reset)
		{
			if (pParam_p->m_ObdEvent == kEplObdEvPostWrite) {
				// update DLL configuration
				Ret = EplApiUpdateDllConfig(FALSE);
			}
			break;
		}

	case 0x1020:		// CFM_VerifyConfiguration_REC.ConfId_U32 != 0
		{
			if ((pParam_p->m_ObdEvent == kEplObdEvPostWrite)
			    && (pParam_p->m_uiSubIndex == 3)
			    && (*((u32 *) pParam_p->m_pArg) != 0)) {
				u32 dwVerifyConfInvalid = 0;
				// set CFM_VerifyConfiguration_REC.VerifyConfInvalid_U32 to 0
				Ret =
				    EplObdWriteEntry(0x1020, 4,
						     &dwVerifyConfInvalid, 4);
				// ignore any error because this objekt is optional
				Ret = kEplSuccessful;
			}
			break;
		}

	case 0x1F9E:		// NMT_ResetCmd_U8
		{
			if (pParam_p->m_ObdEvent == kEplObdEvPreWrite) {
				u8 bNmtCommand;

				bNmtCommand = *((u8 *) pParam_p->m_pArg);
				// check value range
				switch ((tEplNmtCommand) bNmtCommand) {
				case kEplNmtCmdResetNode:
				case kEplNmtCmdResetCommunication:
				case kEplNmtCmdResetConfiguration:
				case kEplNmtCmdSwReset:
				case kEplNmtCmdInvalidService:
					// valid command identifier specified
					break;

				default:
					pParam_p->m_dwAbortCode =
					    EPL_SDOAC_VALUE_RANGE_EXCEEDED;
					Ret = kEplObdAccessViolation;
					break;
				}
			} else if (pParam_p->m_ObdEvent == kEplObdEvPostWrite) {
				u8 bNmtCommand;

				bNmtCommand = *((u8 *) pParam_p->m_pArg);
				// check value range
				switch ((tEplNmtCommand) bNmtCommand) {
				case kEplNmtCmdResetNode:
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)
					Ret =
					    EplNmtuNmtEvent
					    (kEplNmtEventResetNode);
#endif
					break;

				case kEplNmtCmdResetCommunication:
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)
					Ret =
					    EplNmtuNmtEvent
					    (kEplNmtEventResetCom);
#endif
					break;

				case kEplNmtCmdResetConfiguration:
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)
					Ret =
					    EplNmtuNmtEvent
					    (kEplNmtEventResetConfig);
#endif
					break;

				case kEplNmtCmdSwReset:
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)
					Ret =
					    EplNmtuNmtEvent
					    (kEplNmtEventSwReset);
#endif
					break;

				case kEplNmtCmdInvalidService:
					break;

				default:
					pParam_p->m_dwAbortCode =
					    EPL_SDOAC_VALUE_RANGE_EXCEEDED;
					Ret = kEplObdAccessViolation;
					break;
				}
			}
			break;
		}

	default:
		break;
	}

//Exit:
	return Ret;
}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplApiProcessEvent
//
// Description: processes events from event queue and forwards these to
//              the application's event callback function
//
// Parameters:  pEplEvent_p =   pointer to event
//
// Returns:     tEplKernel  = errorcode
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplApiProcessEvent(tEplEvent *pEplEvent_p)
{
	tEplKernel Ret;
	tEplEventError *pEventError;
	tEplApiEventType EventType;

	Ret = kEplSuccessful;

	// process event
	switch (pEplEvent_p->m_EventType) {
		// error event
	case kEplEventTypeError:
		{
			pEventError = (tEplEventError *) pEplEvent_p->m_pArg;
			switch (pEventError->m_EventSource) {
				// treat the errors from the following sources as critical
			case kEplEventSourceEventk:
			case kEplEventSourceEventu:
			case kEplEventSourceDllk:
				{
					EventType = kEplApiEventCriticalError;
					// halt the stack by entering NMT state Off
					Ret =
					    EplNmtuNmtEvent
					    (kEplNmtEventCriticalError);
					break;
				}

				// the other errors are just warnings
			default:
				{
					EventType = kEplApiEventWarning;
					break;
				}
			}

			// call user callback
			Ret =
			    EplApiInstance_g.m_InitParam.m_pfnCbEvent(EventType,
								      (tEplApiEventArg
								       *)
								      pEventError,
								      EplApiInstance_g.
								      m_InitParam.
								      m_pEventUserArg);
			// discard error from callback function, because this could generate an endless loop
			Ret = kEplSuccessful;
			break;
		}

		// at present, there are no other events for this module
	default:
		break;
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplApiCbNmtStateChange
//
// Description: callback function for NMT state changes
//
// Parameters:  NmtStateChange_p        = NMT state change event
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplApiCbNmtStateChange(tEplEventNmtStateChange NmtStateChange_p)
{
	tEplKernel Ret = kEplSuccessful;
	u8 bNmtState;
	tEplApiEventArg EventArg;

	// save NMT state in OD
	bNmtState = (u8) NmtStateChange_p.m_NewNmtState;
	Ret = EplObdWriteEntry(0x1F8C, 0, &bNmtState, 1);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// do work which must be done in that state
	switch (NmtStateChange_p.m_NewNmtState) {
		// EPL stack is not running
	case kEplNmtGsOff:
		break;

		// first init of the hardware
	case kEplNmtGsInitialising:
#if 0
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)
		// configure SDO via UDP (i.e. bind it to the EPL ethernet interface)
		Ret =
		    EplSdoUdpuConfig(EplApiInstance_g.m_InitParam.m_dwIpAddress,
				     EPL_C_SDO_EPL_PORT);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
#endif
#endif

		break;

		// init of the manufacturer-specific profile area and the
		// standardised device profile area
	case kEplNmtGsResetApplication:
		{
			// reset application part of OD
			Ret = EplObdAccessOdPart(kEplObdPartApp,
						 kEplObdDirLoad);
			if (Ret != kEplSuccessful) {
				goto Exit;
			}

			break;
		}

		// init of the communication profile area
	case kEplNmtGsResetCommunication:
		{
			// reset communication part of OD
			Ret = EplObdAccessOdPart(kEplObdPartGen,
						 kEplObdDirLoad);

			if (Ret != kEplSuccessful) {
				goto Exit;
			}
			// $$$ d.k.: update OD only if OD was not loaded from non-volatile memory
			Ret = EplApiUpdateObd();
			if (Ret != kEplSuccessful) {
				goto Exit;
			}

			break;
		}

		// build the configuration with infos from OD
	case kEplNmtGsResetConfiguration:
		{

			Ret = EplApiUpdateDllConfig(TRUE);
			if (Ret != kEplSuccessful) {
				goto Exit;
			}

			break;
		}

		//-----------------------------------------------------------
		// CN part of the state machine

		// node liste for EPL-Frames and check timeout
	case kEplNmtCsNotActive:
		{
			// indicate completion of reset in NMT_ResetCmd_U8
			bNmtState = (u8) kEplNmtCmdInvalidService;
			Ret = EplObdWriteEntry(0x1F9E, 0, &bNmtState, 1);
			if (Ret != kEplSuccessful) {
				goto Exit;
			}

			break;
		}

		// node process only async frames
	case kEplNmtCsPreOperational1:
		{
			break;
		}

		// node process isochronus and asynchronus frames
	case kEplNmtCsPreOperational2:
		{
			break;
		}

		// node should be configured und application is ready
	case kEplNmtCsReadyToOperate:
		{
			break;
		}

		// normal work state
	case kEplNmtCsOperational:
		{
			break;
		}

		// node stopped by MN
		// -> only process asynchronus frames
	case kEplNmtCsStopped:
		{
			break;
		}

		// no EPL cycle
		// -> normal ethernet communication
	case kEplNmtCsBasicEthernet:
		{
			break;
		}

		//-----------------------------------------------------------
		// MN part of the state machine

		// node listens for EPL-Frames and check timeout
	case kEplNmtMsNotActive:
		{
			break;
		}

		// node processes only async frames
	case kEplNmtMsPreOperational1:
		{
			break;
		}

		// node processes isochronous and asynchronous frames
	case kEplNmtMsPreOperational2:
		{
			break;
		}

		// node should be configured und application is ready
	case kEplNmtMsReadyToOperate:
		{
			break;
		}

		// normal work state
	case kEplNmtMsOperational:
		{
			break;
		}

		// no EPL cycle
		// -> normal ethernet communication
	case kEplNmtMsBasicEthernet:
		{
			break;
		}

	default:
		{
			TRACE0
			    ("EplApiCbNmtStateChange(): unhandled NMT state\n");
		}
	}

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_LEDU)) != 0)
	// forward event to Led module
	Ret = EplLeduCbNmtStateChange(NmtStateChange_p);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	// forward event to NmtMn module
	Ret = EplNmtMnuCbNmtStateChange(NmtStateChange_p);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

	// call user callback
	EventArg.m_NmtStateChange = NmtStateChange_p;
	Ret =
	    EplApiInstance_g.m_InitParam.
	    m_pfnCbEvent(kEplApiEventNmtStateChange, &EventArg,
			 EplApiInstance_g.m_InitParam.m_pEventUserArg);

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplApiUpdateDllConfig
//
// Description: update configuration of DLL
//
// Parameters:  fUpdateIdentity_p       = TRUE, if identity must be updated
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplApiUpdateDllConfig(BOOL fUpdateIdentity_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplDllConfigParam DllConfigParam;
	tEplDllIdentParam DllIdentParam;
	tEplObdSize ObdSize;
	u16 wTemp;
	u8 bTemp;

	// configure Dll
	EPL_MEMSET(&DllConfigParam, 0, sizeof(DllConfigParam));
	DllConfigParam.m_uiNodeId = EplObdGetNodeId();

	// Cycle Length (0x1006: NMT_CycleLen_U32) in [us]
	ObdSize = 4;
	Ret =
	    EplObdReadEntry(0x1006, 0, &DllConfigParam.m_dwCycleLen, &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// 0x1F82: NMT_FeatureFlags_U32
	ObdSize = 4;
	Ret =
	    EplObdReadEntry(0x1F82, 0, &DllConfigParam.m_dwFeatureFlags,
			    &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// d.k. There is no dependance between FeatureFlags and async-only CN
	DllConfigParam.m_fAsyncOnly = EplApiInstance_g.m_InitParam.m_fAsyncOnly;

	// 0x1C14: DLL_LossOfFrameTolerance_U32 in [ns]
	ObdSize = 4;
	Ret =
	    EplObdReadEntry(0x1C14, 0, &DllConfigParam.m_dwLossOfFrameTolerance,
			    &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// 0x1F98: NMT_CycleTiming_REC
	// 0x1F98.1: IsochrTxMaxPayload_U16
	ObdSize = 2;
	Ret = EplObdReadEntry(0x1F98, 1, &wTemp, &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	DllConfigParam.m_uiIsochrTxMaxPayload = wTemp;

	// 0x1F98.2: IsochrRxMaxPayload_U16
	ObdSize = 2;
	Ret = EplObdReadEntry(0x1F98, 2, &wTemp, &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	DllConfigParam.m_uiIsochrRxMaxPayload = wTemp;

	// 0x1F98.3: PResMaxLatency_U32
	ObdSize = 4;
	Ret =
	    EplObdReadEntry(0x1F98, 3, &DllConfigParam.m_dwPresMaxLatency,
			    &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// 0x1F98.4: PReqActPayloadLimit_U16
	ObdSize = 2;
	Ret = EplObdReadEntry(0x1F98, 4, &wTemp, &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	DllConfigParam.m_uiPreqActPayloadLimit = wTemp;

	// 0x1F98.5: PResActPayloadLimit_U16
	ObdSize = 2;
	Ret = EplObdReadEntry(0x1F98, 5, &wTemp, &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	DllConfigParam.m_uiPresActPayloadLimit = wTemp;

	// 0x1F98.6: ASndMaxLatency_U32
	ObdSize = 4;
	Ret =
	    EplObdReadEntry(0x1F98, 6, &DllConfigParam.m_dwAsndMaxLatency,
			    &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// 0x1F98.7: MultiplCycleCnt_U8
	ObdSize = 1;
	Ret = EplObdReadEntry(0x1F98, 7, &bTemp, &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	DllConfigParam.m_uiMultiplCycleCnt = bTemp;

	// 0x1F98.8: AsyncMTU_U16
	ObdSize = 2;
	Ret = EplObdReadEntry(0x1F98, 8, &wTemp, &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	DllConfigParam.m_uiAsyncMtu = wTemp;

	// $$$ Prescaler

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	// 0x1F8A.1: WaitSoCPReq_U32 in [ns]
	ObdSize = 4;
	Ret =
	    EplObdReadEntry(0x1F8A, 1, &DllConfigParam.m_dwWaitSocPreq,
			    &ObdSize);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// 0x1F8A.2: AsyncSlotTimeout_U32 in [ns] (optional)
	ObdSize = 4;
	Ret =
	    EplObdReadEntry(0x1F8A, 2, &DllConfigParam.m_dwAsyncSlotTimeout,
			    &ObdSize);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/
#endif

	DllConfigParam.m_uiSizeOfStruct = sizeof(DllConfigParam);
	Ret = EplDllkConfig(&DllConfigParam);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}

	if (fUpdateIdentity_p != FALSE) {
		// configure Identity
		EPL_MEMSET(&DllIdentParam, 0, sizeof(DllIdentParam));
		ObdSize = 4;
		Ret =
		    EplObdReadEntry(0x1000, 0, &DllIdentParam.m_dwDeviceType,
				    &ObdSize);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}

		ObdSize = 4;
		Ret =
		    EplObdReadEntry(0x1018, 1, &DllIdentParam.m_dwVendorId,
				    &ObdSize);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
		ObdSize = 4;
		Ret =
		    EplObdReadEntry(0x1018, 2, &DllIdentParam.m_dwProductCode,
				    &ObdSize);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
		ObdSize = 4;
		Ret =
		    EplObdReadEntry(0x1018, 3,
				    &DllIdentParam.m_dwRevisionNumber,
				    &ObdSize);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
		ObdSize = 4;
		Ret =
		    EplObdReadEntry(0x1018, 4, &DllIdentParam.m_dwSerialNumber,
				    &ObdSize);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}

		DllIdentParam.m_dwIpAddress =
		    EplApiInstance_g.m_InitParam.m_dwIpAddress;
		DllIdentParam.m_dwSubnetMask =
		    EplApiInstance_g.m_InitParam.m_dwSubnetMask;
		EPL_MEMCPY(DllIdentParam.m_sHostname,
			   EplApiInstance_g.m_InitParam.m_sHostname,
			   sizeof(DllIdentParam.m_sHostname));

		ObdSize = 4;
		Ret =
		    EplObdReadEntry(0x1020, 1,
				    &DllIdentParam.m_dwVerifyConfigurationDate,
				    &ObdSize);
		// ignore any error, because this object is optional

		ObdSize = 4;
		Ret =
		    EplObdReadEntry(0x1020, 2,
				    &DllIdentParam.m_dwVerifyConfigurationTime,
				    &ObdSize);
		// ignore any error, because this object is optional

		// $$$ d.k.: fill rest of ident structure

		DllIdentParam.m_uiSizeOfStruct = sizeof(DllIdentParam);
		Ret = EplDllkSetIdentity(&DllIdentParam);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplApiUpdateObd
//
// Description: update OD from init param
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplApiUpdateObd(void)
{
	tEplKernel Ret = kEplSuccessful;
	u16 wTemp;
	u8 bTemp;

	// set node id in OD
	Ret = EplObdSetNodeId(EplApiInstance_g.m_InitParam.m_uiNodeId,	// node id
			      kEplObdNodeIdHardware);	// set by hardware
	if (Ret != kEplSuccessful) {
		goto Exit;
	}

	if (EplApiInstance_g.m_InitParam.m_dwCycleLen != -1) {
		Ret =
		    EplObdWriteEntry(0x1006, 0,
				     &EplApiInstance_g.m_InitParam.m_dwCycleLen,
				     4);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/
	}

	if (EplApiInstance_g.m_InitParam.m_dwLossOfFrameTolerance != -1) {
		Ret =
		    EplObdWriteEntry(0x1C14, 0,
				     &EplApiInstance_g.m_InitParam.
				     m_dwLossOfFrameTolerance, 4);
		/*        if(Ret != kEplSuccessful)
		   {
		   goto Exit;
		   } */
	}
	// d.k. There is no dependance between FeatureFlags and async-only CN.
	if (EplApiInstance_g.m_InitParam.m_dwFeatureFlags != -1) {
		Ret =
		    EplObdWriteEntry(0x1F82, 0,
				     &EplApiInstance_g.m_InitParam.
				     m_dwFeatureFlags, 4);
		/*    if(Ret != kEplSuccessful)
		   {
		   goto Exit;
		   } */
	}

	wTemp = (u16) EplApiInstance_g.m_InitParam.m_uiIsochrTxMaxPayload;
	Ret = EplObdWriteEntry(0x1F98, 1, &wTemp, 2);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/

	wTemp = (u16) EplApiInstance_g.m_InitParam.m_uiIsochrRxMaxPayload;
	Ret = EplObdWriteEntry(0x1F98, 2, &wTemp, 2);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/

	Ret =
	    EplObdWriteEntry(0x1F98, 3,
			     &EplApiInstance_g.m_InitParam.m_dwPresMaxLatency,
			     4);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/

	if (EplApiInstance_g.m_InitParam.m_uiPreqActPayloadLimit <=
	    EPL_C_DLL_ISOCHR_MAX_PAYL) {
		wTemp =
		    (u16) EplApiInstance_g.m_InitParam.m_uiPreqActPayloadLimit;
		Ret = EplObdWriteEntry(0x1F98, 4, &wTemp, 2);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/
	}

	if (EplApiInstance_g.m_InitParam.m_uiPresActPayloadLimit <=
	    EPL_C_DLL_ISOCHR_MAX_PAYL) {
		wTemp =
		    (u16) EplApiInstance_g.m_InitParam.m_uiPresActPayloadLimit;
		Ret = EplObdWriteEntry(0x1F98, 5, &wTemp, 2);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/
	}

	Ret =
	    EplObdWriteEntry(0x1F98, 6,
			     &EplApiInstance_g.m_InitParam.m_dwAsndMaxLatency,
			     4);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/

	if (EplApiInstance_g.m_InitParam.m_uiMultiplCycleCnt <= 0xFF) {
		bTemp = (u8) EplApiInstance_g.m_InitParam.m_uiMultiplCycleCnt;
		Ret = EplObdWriteEntry(0x1F98, 7, &bTemp, 1);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/
	}

	if (EplApiInstance_g.m_InitParam.m_uiAsyncMtu <=
	    EPL_C_DLL_MAX_ASYNC_MTU) {
		wTemp = (u16) EplApiInstance_g.m_InitParam.m_uiAsyncMtu;
		Ret = EplObdWriteEntry(0x1F98, 8, &wTemp, 2);
/*    if(Ret != kEplSuccessful)
    {
        goto Exit;
    }*/
	}

	if (EplApiInstance_g.m_InitParam.m_uiPrescaler <= 1000) {
		wTemp = (u16) EplApiInstance_g.m_InitParam.m_uiPrescaler;
		Ret = EplObdWriteEntry(0x1F98, 9, &wTemp, 2);
		// ignore return code
		Ret = kEplSuccessful;
	}
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	if (EplApiInstance_g.m_InitParam.m_dwWaitSocPreq != -1) {
		Ret =
		    EplObdWriteEntry(0x1F8A, 1,
				     &EplApiInstance_g.m_InitParam.
				     m_dwWaitSocPreq, 4);
		/*        if(Ret != kEplSuccessful)
		   {
		   goto Exit;
		   } */
	}

	if ((EplApiInstance_g.m_InitParam.m_dwAsyncSlotTimeout != 0)
	    && (EplApiInstance_g.m_InitParam.m_dwAsyncSlotTimeout != -1)) {
		Ret =
		    EplObdWriteEntry(0x1F8A, 2,
				     &EplApiInstance_g.m_InitParam.
				     m_dwAsyncSlotTimeout, 4);
		/*        if(Ret != kEplSuccessful)
		   {
		   goto Exit;
		   } */
	}
#endif

	// configure Identity
	if (EplApiInstance_g.m_InitParam.m_dwDeviceType != -1) {
		Ret =
		    EplObdWriteEntry(0x1000, 0,
				     &EplApiInstance_g.m_InitParam.
				     m_dwDeviceType, 4);
/*        if(Ret != kEplSuccessful)
        {
            goto Exit;
        }*/
	}

	if (EplApiInstance_g.m_InitParam.m_dwVendorId != -1) {
		Ret =
		    EplObdWriteEntry(0x1018, 1,
				     &EplApiInstance_g.m_InitParam.m_dwVendorId,
				     4);
/*        if(Ret != kEplSuccessful)
        {
            goto Exit;
        }*/
	}

	if (EplApiInstance_g.m_InitParam.m_dwProductCode != -1) {
		Ret =
		    EplObdWriteEntry(0x1018, 2,
				     &EplApiInstance_g.m_InitParam.
				     m_dwProductCode, 4);
/*        if(Ret != kEplSuccessful)
        {
            goto Exit;
        }*/
	}

	if (EplApiInstance_g.m_InitParam.m_dwRevisionNumber != -1) {
		Ret =
		    EplObdWriteEntry(0x1018, 3,
				     &EplApiInstance_g.m_InitParam.
				     m_dwRevisionNumber, 4);
/*        if(Ret != kEplSuccessful)
        {
            goto Exit;
        }*/
	}

	if (EplApiInstance_g.m_InitParam.m_dwSerialNumber != -1) {
		Ret =
		    EplObdWriteEntry(0x1018, 4,
				     &EplApiInstance_g.m_InitParam.
				     m_dwSerialNumber, 4);
/*        if(Ret != kEplSuccessful)
        {
            goto Exit;
        }*/
	}

	if (EplApiInstance_g.m_InitParam.m_pszDevName != NULL) {
		// write Device Name (0x1008)
		Ret =
		    EplObdWriteEntry(0x1008, 0,
				     (void *)EplApiInstance_g.
				     m_InitParam.m_pszDevName,
				     (tEplObdSize) strlen(EplApiInstance_g.
							  m_InitParam.
							  m_pszDevName));
/*        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }*/
	}

	if (EplApiInstance_g.m_InitParam.m_pszHwVersion != NULL) {
		// write Hardware version (0x1009)
		Ret =
		    EplObdWriteEntry(0x1009, 0,
				     (void *)EplApiInstance_g.
				     m_InitParam.m_pszHwVersion,
				     (tEplObdSize) strlen(EplApiInstance_g.
							  m_InitParam.
							  m_pszHwVersion));
/*        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }*/
	}

	if (EplApiInstance_g.m_InitParam.m_pszSwVersion != NULL) {
		// write Software version (0x100A)
		Ret =
		    EplObdWriteEntry(0x100A, 0,
				     (void *)EplApiInstance_g.
				     m_InitParam.m_pszSwVersion,
				     (tEplObdSize) strlen(EplApiInstance_g.
							  m_InitParam.
							  m_pszSwVersion));
/*        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }*/
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplApiCbSdoCon
//
// Description: callback function for SDO transfers
//
// Parameters:  pSdoComFinished_p       = SDO parameter
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
static tEplKernel EplApiCbSdoCon(tEplSdoComFinished *pSdoComFinished_p)
{
	tEplKernel Ret;
	tEplApiEventArg EventArg;

	Ret = kEplSuccessful;

	// call user callback
	EventArg.m_Sdo = *pSdoComFinished_p;
	Ret = EplApiInstance_g.m_InitParam.m_pfnCbEvent(kEplApiEventSdo,
							&EventArg,
							EplApiInstance_g.
							m_InitParam.
							m_pEventUserArg);

	return Ret;

}
#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

//---------------------------------------------------------------------------
//
// Function:    EplApiCbNodeEvent
//
// Description: callback function for node events
//
// Parameters:  uiNodeId_p              = node ID of the CN
//              NodeEvent_p             = event from the specified CN
//              NmtState_p              = current NMT state of the CN
//              wErrorCode_p            = EPL error code if NodeEvent_p==kEplNmtNodeEventError
//              fMandatory_p            = flag if CN is mandatory
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplApiCbNodeEvent(unsigned int uiNodeId_p,
				    tEplNmtNodeEvent NodeEvent_p,
				    tEplNmtState NmtState_p,
				    u16 wErrorCode_p, BOOL fMandatory_p)
{
	tEplKernel Ret;
	tEplApiEventArg EventArg;

	Ret = kEplSuccessful;

	// call user callback
	EventArg.m_Node.m_uiNodeId = uiNodeId_p;
	EventArg.m_Node.m_NodeEvent = NodeEvent_p;
	EventArg.m_Node.m_NmtState = NmtState_p;
	EventArg.m_Node.m_wErrorCode = wErrorCode_p;
	EventArg.m_Node.m_fMandatory = fMandatory_p;

	Ret = EplApiInstance_g.m_InitParam.m_pfnCbEvent(kEplApiEventNode,
							&EventArg,
							EplApiInstance_g.
							m_InitParam.
							m_pEventUserArg);

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplApiCbBootEvent
//
// Description: callback function for boot events
//
// Parameters:  BootEvent_p             = event from the boot-up process
//              NmtState_p              = current local NMT state
//              wErrorCode_p            = EPL error code if BootEvent_p==kEplNmtBootEventError
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplApiCbBootEvent(tEplNmtBootEvent BootEvent_p,
				    tEplNmtState NmtState_p,
				    u16 wErrorCode_p)
{
	tEplKernel Ret;
	tEplApiEventArg EventArg;

	Ret = kEplSuccessful;

	// call user callback
	EventArg.m_Boot.m_BootEvent = BootEvent_p;
	EventArg.m_Boot.m_NmtState = NmtState_p;
	EventArg.m_Boot.m_wErrorCode = wErrorCode_p;

	Ret = EplApiInstance_g.m_InitParam.m_pfnCbEvent(kEplApiEventBoot,
							&EventArg,
							EplApiInstance_g.
							m_InitParam.
							m_pEventUserArg);

	return Ret;

}

#endif // (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_LEDU)) != 0)

//---------------------------------------------------------------------------
//
// Function:    EplApiCbLedStateChange
//
// Description: callback function for LED change events.
//
// Parameters:  LedType_p       = type of LED
//              fOn_p           = state of LED
//
// Returns:     tEplKernel      = errorcode
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplApiCbLedStateChange(tEplLedType LedType_p, BOOL fOn_p)
{
	tEplKernel Ret;
	tEplApiEventArg EventArg;

	Ret = kEplSuccessful;

	// call user callback
	EventArg.m_Led.m_LedType = LedType_p;
	EventArg.m_Led.m_fOn = fOn_p;

	Ret = EplApiInstance_g.m_InitParam.m_pfnCbEvent(kEplApiEventLed,
							&EventArg,
							EplApiInstance_g.
							m_InitParam.
							m_pEventUserArg);

	return Ret;

}

#endif

// EOF
