/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for DLL Communication Abstraction Layer module in EPL user part

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

                $RCSfile: EplDlluCal.c,v $

                $Author: D.Krueger $

                $Revision: 1.7 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/20 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#include "user/EplDlluCal.h"
#include "user/EplEventu.h"

#include "EplDllCal.h"

// include only if direct call between user- and kernelspace is enabled
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
#include "kernel/EplDllkCal.h"
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLU)) != 0)

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
/*          C L A S S  EplDlluCal                                          */
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
	tEplDlluCbAsnd m_apfnDlluCbAsnd[EPL_DLL_MAX_ASND_SERVICE_ID];

} tEplDlluCalInstance;

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

// if no dynamic memory allocation shall be used
// define structures statically
static tEplDlluCalInstance EplDlluCalInstance_g;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static tEplKernel EplDlluCalSetAsndServiceIdFilter(tEplDllAsndServiceId
						   ServiceId_p,
						   tEplDllAsndFilter Filter_p);

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalAddInstance()
//
// Description: add and initialize new instance of DLL CAL module
//
// Parameters:  none
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDlluCalAddInstance()
{
	tEplKernel Ret = kEplSuccessful;

	// reset instance structure
	EPL_MEMSET(&EplDlluCalInstance_g, 0, sizeof(EplDlluCalInstance_g));

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalDelInstance()
//
// Description: deletes an instance of DLL CAL module
//
// Parameters:  none
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDlluCalDelInstance()
{
	tEplKernel Ret = kEplSuccessful;

	// reset instance structure
	EPL_MEMSET(&EplDlluCalInstance_g, 0, sizeof(EplDlluCalInstance_g));

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalProcess
//
// Description: process the passed asynch frame
//
// Parameters:  pEvent_p                = event containing frame to be processed
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDlluCalProcess(tEplEvent * pEvent_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplMsgType MsgType;
	unsigned int uiAsndServiceId;
	tEplFrameInfo FrameInfo;

	if (pEvent_p->m_EventType == kEplEventTypeAsndRx) {
		FrameInfo.m_pFrame = (tEplFrame *) pEvent_p->m_pArg;
		FrameInfo.m_uiFrameSize = pEvent_p->m_uiSize;
		// extract NetTime
		FrameInfo.m_NetTime = pEvent_p->m_NetTime;

		MsgType =
		    (tEplMsgType) AmiGetByteFromLe(&FrameInfo.m_pFrame->
						   m_le_bMessageType);
		if (MsgType != kEplMsgTypeAsnd) {
			Ret = kEplInvalidOperation;
			goto Exit;
		}

		uiAsndServiceId =
		    (unsigned int)AmiGetByteFromLe(&FrameInfo.m_pFrame->m_Data.
						   m_Asnd.m_le_bServiceId);
		if (uiAsndServiceId < EPL_DLL_MAX_ASND_SERVICE_ID) {	// ASnd service ID is valid
			if (EplDlluCalInstance_g.m_apfnDlluCbAsnd[uiAsndServiceId] != NULL) {	// handler was registered
				Ret =
				    EplDlluCalInstance_g.
				    m_apfnDlluCbAsnd[uiAsndServiceId]
				    (&FrameInfo);
			}
		}
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalRegAsndService()
//
// Description: registers the specified handler for the specified
//              AsndServiceId with the specified node ID filter.
//
// Parameters:  ServiceId_p             = ASnd Service ID
//              pfnDlluCbAsnd_p         = callback function
//              Filter_p                = node ID filter
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDlluCalRegAsndService(tEplDllAsndServiceId ServiceId_p,
				    tEplDlluCbAsnd pfnDlluCbAsnd_p,
				    tEplDllAsndFilter Filter_p)
{
	tEplKernel Ret = kEplSuccessful;

	if (ServiceId_p < tabentries(EplDlluCalInstance_g.m_apfnDlluCbAsnd)) {
		// memorize function pointer
		EplDlluCalInstance_g.m_apfnDlluCbAsnd[ServiceId_p] =
		    pfnDlluCbAsnd_p;

		if (pfnDlluCbAsnd_p == NULL) {	// close filter
			Filter_p = kEplDllAsndFilterNone;
		}
		// set filter in DLL module in kernel part
		Ret = EplDlluCalSetAsndServiceIdFilter(ServiceId_p, Filter_p);

	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalAsyncSend()
//
// Description: sends the frame with the specified priority.
//
// Parameters:  pFrameInfo_p            = frame
//                                        m_uiFrameSize does not include the
//                                        ethernet header (14 bytes)
//              Priority_p              = priority
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDlluCalAsyncSend(tEplFrameInfo * pFrameInfo_p,
			       tEplDllAsyncReqPriority Priority_p)
{
	tEplKernel Ret = kEplSuccessful;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
	pFrameInfo_p->m_uiFrameSize += 14;	// add size of ethernet header
	Ret = EplDllkCalAsyncSend(pFrameInfo_p, Priority_p);
#else
	Ret = kEplSuccessful;
#endif

	return Ret;
}

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalIssueRequest()
//
// Description: issues a StatusRequest or a IdentRequest to the specified node.
//
// Parameters:  Service_p               = request service ID
//              uiNodeId_p              = node ID
//              bSoaFlag1_p             = flag1 for this node (transmit in SoA and PReq)
//                                        If 0xFF this flag is ignored.
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDlluCalIssueRequest(tEplDllReqServiceId Service_p,
				  unsigned int uiNodeId_p, BYTE bSoaFlag1_p)
{
	tEplKernel Ret = kEplSuccessful;

	// add node to appropriate request queue
	switch (Service_p) {
	case kEplDllReqServiceIdent:
	case kEplDllReqServiceStatus:
		{
			tEplEvent Event;
			tEplDllCalIssueRequest IssueReq;

			Event.m_EventSink = kEplEventSinkDllkCal;
			Event.m_EventType = kEplEventTypeDllkIssueReq;
			IssueReq.m_Service = Service_p;
			IssueReq.m_uiNodeId = uiNodeId_p;
			IssueReq.m_bSoaFlag1 = bSoaFlag1_p;
			Event.m_pArg = &IssueReq;
			Event.m_uiSize = sizeof(IssueReq);

			Ret = EplEventuPost(&Event);
			break;
		}

	default:
		{
			Ret = kEplDllInvalidParam;
			goto Exit;
		}
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalAddNode()
//
// Description: adds the specified node to the isochronous phase.
//
// Parameters:  pNodeInfo_p             = pointer of node info structure
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDlluCalAddNode(tEplDllNodeInfo * pNodeInfo_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplEvent Event;

	Event.m_EventSink = kEplEventSinkDllkCal;
	Event.m_EventType = kEplEventTypeDllkAddNode;
	Event.m_pArg = pNodeInfo_p;
	Event.m_uiSize = sizeof(tEplDllNodeInfo);

	Ret = EplEventuPost(&Event);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalDeleteNode()
//
// Description: removes the specified node from the isochronous phase.
//
// Parameters:  uiNodeId_p              = node ID
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDlluCalDeleteNode(unsigned int uiNodeId_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplEvent Event;

	Event.m_EventSink = kEplEventSinkDllkCal;
	Event.m_EventType = kEplEventTypeDllkDelNode;
	Event.m_pArg = &uiNodeId_p;
	Event.m_uiSize = sizeof(uiNodeId_p);

	Ret = EplEventuPost(&Event);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalSoftDeleteNode()
//
// Description: removes the specified node softly from the isochronous phase.
//
// Parameters:  uiNodeId_p              = node ID
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDlluCalSoftDeleteNode(unsigned int uiNodeId_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplEvent Event;

	Event.m_EventSink = kEplEventSinkDllkCal;
	Event.m_EventType = kEplEventTypeDllkSoftDelNode;
	Event.m_pArg = &uiNodeId_p;
	Event.m_uiSize = sizeof(uiNodeId_p);

	Ret = EplEventuPost(&Event);

	return Ret;
}

#endif // (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplDlluCalSetAsndServiceIdFilter()
//
// Description: forwards call to EplDllkSetAsndServiceIdFilter() in kernel part
//
// Parameters:  ServiceId_p             = ASnd Service ID
//              Filter_p                = node ID filter
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplDlluCalSetAsndServiceIdFilter(tEplDllAsndServiceId
						   ServiceId_p,
						   tEplDllAsndFilter Filter_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplEvent Event;
	tEplDllCalAsndServiceIdFilter ServFilter;

	Event.m_EventSink = kEplEventSinkDllkCal;
	Event.m_EventType = kEplEventTypeDllkServFilter;
	ServFilter.m_ServiceId = ServiceId_p;
	ServFilter.m_Filter = Filter_p;
	Event.m_pArg = &ServFilter;
	Event.m_uiSize = sizeof(ServFilter);

	Ret = EplEventuPost(&Event);

	return Ret;
}

#endif // (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLU)) != 0)

// EOF
