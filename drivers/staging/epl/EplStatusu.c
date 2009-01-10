/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for Statusu-Module

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

                $RCSfile: EplStatusu.c,v $

                $Author: D.Krueger $

                $Revision: 1.5 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/11/15 d.k.:   start of the implementation

****************************************************************************/

#include "user/EplStatusu.h"
#include "user/EplDlluCal.h"

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
/*          C L A S S  <xxxxx>                                             */
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
	tEplStatusuCbResponse m_apfnCbResponse[254];

} tEplStatusuInstance;

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

static tEplStatusuInstance EplStatusuInstance_g;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static tEplKernel PUBLIC EplStatusuCbStatusResponse(tEplFrameInfo *
						    pFrameInfo_p);

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplStatusuInit
//
// Description: init first instance of the module
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------

EPLDLLEXPORT tEplKernel PUBLIC EplStatusuInit()
{
	tEplKernel Ret;

	Ret = EplStatusuAddInstance();

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplStatusuAddInstance
//
// Description: init other instances of the module
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------

EPLDLLEXPORT tEplKernel PUBLIC EplStatusuAddInstance()
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// reset instance structure
	EPL_MEMSET(&EplStatusuInstance_g, 0, sizeof(EplStatusuInstance_g));

	// register StatusResponse callback function
	Ret =
	    EplDlluCalRegAsndService(kEplDllAsndStatusResponse,
				     EplStatusuCbStatusResponse,
				     kEplDllAsndFilterAny);

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplStatusuDelInstance
//
// Description: delete instance
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------

EPLDLLEXPORT tEplKernel PUBLIC EplStatusuDelInstance()
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// deregister StatusResponse callback function
	Ret =
	    EplDlluCalRegAsndService(kEplDllAsndStatusResponse, NULL,
				     kEplDllAsndFilterNone);

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplStatusuReset
//
// Description: resets this instance
//
// Parameters:
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------

EPLDLLEXPORT tEplKernel PUBLIC EplStatusuReset()
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// reset instance structure
	EPL_MEMSET(&EplStatusuInstance_g, 0, sizeof(EplStatusuInstance_g));

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplStatusuRequestStatusResponse
//
// Description: returns the StatusResponse for the specified node.
//
// Parameters:  uiNodeId_p                  = IN: node ID
//              pfnCbResponse_p             = IN: function pointer to callback function
//                                            which will be called if StatusResponse is received
//
// Return:      tEplKernel                  = error code
//
// State:       not tested
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplStatusuRequestStatusResponse(unsigned int uiNodeId_p,
						  tEplStatusuCbResponse
						  pfnCbResponse_p)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// decrement node ID, because array is zero based
	uiNodeId_p--;
	if (uiNodeId_p < tabentries(EplStatusuInstance_g.m_apfnCbResponse)) {
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
		if (EplStatusuInstance_g.m_apfnCbResponse[uiNodeId_p] != NULL) {	// request already issued (maybe by someone else)
			Ret = kEplInvalidOperation;
		} else {
			EplStatusuInstance_g.m_apfnCbResponse[uiNodeId_p] =
			    pfnCbResponse_p;
			Ret =
			    EplDlluCalIssueRequest(kEplDllReqServiceStatus,
						   (uiNodeId_p + 1), 0xFF);
		}
#else
		Ret = kEplInvalidOperation;
#endif
	} else {		// invalid node ID specified
		Ret = kEplInvalidNodeId;
	}

	return Ret;

}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplStatusuCbStatusResponse
//
// Description: callback funktion for StatusResponse
//
//
//
// Parameters:  pFrameInfo_p            = Frame with the StatusResponse
//
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel PUBLIC EplStatusuCbStatusResponse(tEplFrameInfo *
						    pFrameInfo_p)
{
	tEplKernel Ret = kEplSuccessful;
	unsigned int uiNodeId;
	unsigned int uiIndex;
	tEplStatusuCbResponse pfnCbResponse;

	uiNodeId = AmiGetByteFromLe(&pFrameInfo_p->m_pFrame->m_le_bSrcNodeId);

	uiIndex = uiNodeId - 1;

	if (uiIndex < tabentries(EplStatusuInstance_g.m_apfnCbResponse)) {
		// memorize pointer to callback function
		pfnCbResponse = EplStatusuInstance_g.m_apfnCbResponse[uiIndex];
		if (pfnCbResponse == NULL) {	// response was not requested
			goto Exit;
		}
		// reset callback function pointer so that caller may issue next request
		EplStatusuInstance_g.m_apfnCbResponse[uiIndex] = NULL;

		if (pFrameInfo_p->m_uiFrameSize < EPL_C_DLL_MINSIZE_STATUSRES) {	// StatusResponse not received or it has invalid size
			Ret = pfnCbResponse(uiNodeId, NULL);
		} else {	// StatusResponse received
			Ret =
			    pfnCbResponse(uiNodeId,
					  &pFrameInfo_p->m_pFrame->m_Data.
					  m_Asnd.m_Payload.m_StatusResponse);
		}
	}

      Exit:
	return Ret;
}

// EOF
