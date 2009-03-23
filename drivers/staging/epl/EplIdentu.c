/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for Identu-Module

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

                $RCSfile: EplIdentu.c,v $

                $Author: D.Krueger $

                $Revision: 1.8 $  $Date: 2008/11/21 09:00:38 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/11/15 d.k.:   start of the implementation

****************************************************************************/

#include "user/EplIdentu.h"
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
	tEplIdentResponse *m_apIdentResponse[254];	// the IdentResponse are managed dynamically
	tEplIdentuCbResponse m_apfnCbResponse[254];

} tEplIdentuInstance;

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

static tEplIdentuInstance EplIdentuInstance_g;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static tEplKernel EplIdentuCbIdentResponse(tEplFrameInfo *pFrameInfo_p);

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplIdentuInit
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

tEplKernel EplIdentuInit(void)
{
	tEplKernel Ret;

	Ret = EplIdentuAddInstance();

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplIdentuAddInstance
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

tEplKernel EplIdentuAddInstance(void)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// reset instance structure
	EPL_MEMSET(&EplIdentuInstance_g, 0, sizeof(EplIdentuInstance_g));

	// register IdentResponse callback function
	Ret =
	    EplDlluCalRegAsndService(kEplDllAsndIdentResponse,
				     EplIdentuCbIdentResponse,
				     kEplDllAsndFilterAny);

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplIdentuDelInstance
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

tEplKernel EplIdentuDelInstance(void)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// deregister IdentResponse callback function
	Ret =
	    EplDlluCalRegAsndService(kEplDllAsndIdentResponse, NULL,
				     kEplDllAsndFilterNone);

	Ret = EplIdentuReset();

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplIdentuReset
//
// Description: resets this instance
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

tEplKernel EplIdentuReset(void)
{
	tEplKernel Ret;
	int iIndex;

	Ret = kEplSuccessful;

	for (iIndex = 0;
	     iIndex < tabentries(EplIdentuInstance_g.m_apIdentResponse);
	     iIndex++) {
		if (EplIdentuInstance_g.m_apIdentResponse[iIndex] != NULL) {	// free memory
			EPL_FREE(EplIdentuInstance_g.m_apIdentResponse[iIndex]);
		}
	}

	EPL_MEMSET(&EplIdentuInstance_g, 0, sizeof(EplIdentuInstance_g));

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplIdentuGetIdentResponse
//
// Description: returns the IdentResponse for the specified node.
//
// Parameters:  uiNodeId_p                  = IN: node ID
//              ppIdentResponse_p           = OUT: pointer to pointer of IdentResponse
//                                            equals NULL, if no IdentResponse available
//
// Return:      tEplKernel                  = error code
//
// State:       not tested
//
//---------------------------------------------------------------------------

tEplKernel EplIdentuGetIdentResponse(unsigned int uiNodeId_p,
				     tEplIdentResponse **ppIdentResponse_p)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// decrement node ID, because array is zero based
	uiNodeId_p--;
	if (uiNodeId_p < tabentries(EplIdentuInstance_g.m_apIdentResponse)) {
		*ppIdentResponse_p =
		    EplIdentuInstance_g.m_apIdentResponse[uiNodeId_p];
	} else {		// invalid node ID specified
		*ppIdentResponse_p = NULL;
		Ret = kEplInvalidNodeId;
	}

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplIdentuRequestIdentResponse
//
// Description: returns the IdentResponse for the specified node.
//
// Parameters:  uiNodeId_p                  = IN: node ID
//              pfnCbResponse_p             = IN: function pointer to callback function
//                                            which will be called if IdentResponse is received
//
// Return:      tEplKernel                  = error code
//
// State:       not tested
//
//---------------------------------------------------------------------------

tEplKernel EplIdentuRequestIdentResponse(unsigned int uiNodeId_p,
					 tEplIdentuCbResponse pfnCbResponse_p)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// decrement node ID, because array is zero based
	uiNodeId_p--;
	if (uiNodeId_p < tabentries(EplIdentuInstance_g.m_apfnCbResponse)) {
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
		if (EplIdentuInstance_g.m_apfnCbResponse[uiNodeId_p] != NULL) {	// request already issued (maybe by someone else)
			Ret = kEplInvalidOperation;
		} else {
			EplIdentuInstance_g.m_apfnCbResponse[uiNodeId_p] =
			    pfnCbResponse_p;
			Ret =
			    EplDlluCalIssueRequest(kEplDllReqServiceIdent,
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

//---------------------------------------------------------------------------
//
// Function:    EplIdentuGetRunningRequests
//
// Description: returns a bit field with the running requests for node-ID 1-32
//              just for debugging purposes
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

u32 EplIdentuGetRunningRequests(void)
{
	u32 dwReqs = 0;
	unsigned int uiIndex;

	for (uiIndex = 0; uiIndex < 32; uiIndex++) {
		if (EplIdentuInstance_g.m_apfnCbResponse[uiIndex] != NULL) {
			dwReqs |= (1 << uiIndex);
		}
	}

	return dwReqs;
}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplIdentuCbIdentResponse
//
// Description: callback funktion for IdentResponse
//
//
//
// Parameters:  pFrameInfo_p            = Frame with the IdentResponse
//
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplIdentuCbIdentResponse(tEplFrameInfo *pFrameInfo_p)
{
	tEplKernel Ret = kEplSuccessful;
	unsigned int uiNodeId;
	unsigned int uiIndex;
	tEplIdentuCbResponse pfnCbResponse;

	uiNodeId = AmiGetByteFromLe(&pFrameInfo_p->m_pFrame->m_le_bSrcNodeId);

	uiIndex = uiNodeId - 1;

	if (uiIndex < tabentries(EplIdentuInstance_g.m_apfnCbResponse)) {
		// memorize pointer to callback function
		pfnCbResponse = EplIdentuInstance_g.m_apfnCbResponse[uiIndex];
		// reset callback function pointer so that caller may issue next request immediately
		EplIdentuInstance_g.m_apfnCbResponse[uiIndex] = NULL;

		if (pFrameInfo_p->m_uiFrameSize < EPL_C_DLL_MINSIZE_IDENTRES) {	// IdentResponse not received or it has invalid size
			if (pfnCbResponse == NULL) {	// response was not requested
				goto Exit;
			}
			Ret = pfnCbResponse(uiNodeId, NULL);
		} else {	// IdentResponse received
			if (EplIdentuInstance_g.m_apIdentResponse[uiIndex] == NULL) {	// memory for IdentResponse must be allocated
				EplIdentuInstance_g.m_apIdentResponse[uiIndex] =
				    EPL_MALLOC(sizeof(tEplIdentResponse));
				if (EplIdentuInstance_g.m_apIdentResponse[uiIndex] == NULL) {	// malloc failed
					if (pfnCbResponse == NULL) {	// response was not requested
						goto Exit;
					}
					Ret =
					    pfnCbResponse(uiNodeId,
							  &pFrameInfo_p->
							  m_pFrame->m_Data.
							  m_Asnd.m_Payload.
							  m_IdentResponse);
					goto Exit;
				}
			}
			// copy IdentResponse to instance structure
			EPL_MEMCPY(EplIdentuInstance_g.
				   m_apIdentResponse[uiIndex],
				   &pFrameInfo_p->m_pFrame->m_Data.m_Asnd.
				   m_Payload.m_IdentResponse,
				   sizeof(tEplIdentResponse));
			if (pfnCbResponse == NULL) {	// response was not requested
				goto Exit;
			}
			Ret =
			    pfnCbResponse(uiNodeId,
					  EplIdentuInstance_g.
					  m_apIdentResponse[uiIndex]);
		}
	}

      Exit:
	return Ret;
}

// EOF
