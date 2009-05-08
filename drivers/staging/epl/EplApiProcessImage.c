/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for EPL API module (process image)

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

                $RCSfile: EplApiProcessImage.c,v $

                $Author: D.Krueger $

                $Revision: 1.7 $  $Date: 2008/11/13 17:13:09 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/10/10 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#include "Epl.h"

#include <linux/uaccess.h>

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

#if ((EPL_API_PROCESS_IMAGE_SIZE_IN > 0) || (EPL_API_PROCESS_IMAGE_SIZE_OUT > 0))
typedef struct {
#if EPL_API_PROCESS_IMAGE_SIZE_IN > 0
	u8 m_abProcessImageInput[EPL_API_PROCESS_IMAGE_SIZE_IN];
#endif
#if EPL_API_PROCESS_IMAGE_SIZE_OUT > 0
	u8 m_abProcessImageOutput[EPL_API_PROCESS_IMAGE_SIZE_OUT];
#endif

} tEplApiProcessImageInstance;

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

static tEplApiProcessImageInstance EplApiProcessImageInstance_g;
#endif

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
// Function:    EplApiProcessImageSetup()
//
// Description: sets up static process image
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplApiProcessImageSetup(void)
{
	tEplKernel Ret = kEplSuccessful;
#if ((EPL_API_PROCESS_IMAGE_SIZE_IN > 0) || (EPL_API_PROCESS_IMAGE_SIZE_OUT > 0))
	unsigned int uiVarEntries;
	tEplObdSize ObdSize;
#endif

#if EPL_API_PROCESS_IMAGE_SIZE_IN > 0
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_IN;
	ObdSize = 1;
	Ret = EplApiLinkObject(0x2000,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageInput, &uiVarEntries, &ObdSize,
			       1);

	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_IN;
	ObdSize = 1;
	Ret = EplApiLinkObject(0x2001,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageInput, &uiVarEntries, &ObdSize,
			       1);

	ObdSize = 2;
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_IN / ObdSize;
	Ret = EplApiLinkObject(0x2010,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageInput, &uiVarEntries, &ObdSize,
			       1);

	ObdSize = 2;
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_IN / ObdSize;
	Ret = EplApiLinkObject(0x2011,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageInput, &uiVarEntries, &ObdSize,
			       1);

	ObdSize = 4;
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_IN / ObdSize;
	Ret = EplApiLinkObject(0x2020,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageInput, &uiVarEntries, &ObdSize,
			       1);

	ObdSize = 4;
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_IN / ObdSize;
	Ret = EplApiLinkObject(0x2021,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageInput, &uiVarEntries, &ObdSize,
			       1);
#endif

#if EPL_API_PROCESS_IMAGE_SIZE_OUT > 0
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_OUT;
	ObdSize = 1;
	Ret = EplApiLinkObject(0x2030,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageOutput, &uiVarEntries, &ObdSize,
			       1);

	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_OUT;
	ObdSize = 1;
	Ret = EplApiLinkObject(0x2031,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageOutput, &uiVarEntries, &ObdSize,
			       1);

	ObdSize = 2;
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_OUT / ObdSize;
	Ret = EplApiLinkObject(0x2040,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageOutput, &uiVarEntries, &ObdSize,
			       1);

	ObdSize = 2;
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_OUT / ObdSize;
	Ret = EplApiLinkObject(0x2041,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageOutput, &uiVarEntries, &ObdSize,
			       1);

	ObdSize = 4;
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_OUT / ObdSize;
	Ret = EplApiLinkObject(0x2050,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageOutput, &uiVarEntries, &ObdSize,
			       1);

	ObdSize = 4;
	uiVarEntries = EPL_API_PROCESS_IMAGE_SIZE_OUT / ObdSize;
	Ret = EplApiLinkObject(0x2051,
			       EplApiProcessImageInstance_g.
			       m_abProcessImageOutput, &uiVarEntries, &ObdSize,
			       1);
#endif

	return Ret;
}

//----------------------------------------------------------------------------
// Function:    EplApiProcessImageExchangeIn()
//
// Description: replaces passed input process image with the one of EPL stack
//
// Parameters:  pPI_p                   = input process image
//
// Returns:     tEplKernel              = error code
//
// State:
//----------------------------------------------------------------------------

tEplKernel EplApiProcessImageExchangeIn(tEplApiProcessImage *pPI_p)
{
	tEplKernel Ret = kEplSuccessful;

#if EPL_API_PROCESS_IMAGE_SIZE_IN > 0
	copy_to_user(pPI_p->m_pImage,
		     EplApiProcessImageInstance_g.m_abProcessImageInput,
		     min(pPI_p->m_uiSize,
			 sizeof(EplApiProcessImageInstance_g.
				m_abProcessImageInput)));
#endif

	return Ret;
}

//----------------------------------------------------------------------------
// Function:    EplApiProcessImageExchangeOut()
//
// Description: copies passed output process image to EPL stack.
//
// Parameters:  pPI_p                   = output process image
//
// Returns:     tEplKernel              = error code
//
// State:
//----------------------------------------------------------------------------

tEplKernel EplApiProcessImageExchangeOut(tEplApiProcessImage *pPI_p)
{
	tEplKernel Ret = kEplSuccessful;

#if EPL_API_PROCESS_IMAGE_SIZE_OUT > 0
	copy_from_user(EplApiProcessImageInstance_g.m_abProcessImageOutput,
		       pPI_p->m_pImage,
		       min(pPI_p->m_uiSize,
			   sizeof(EplApiProcessImageInstance_g.
				  m_abProcessImageOutput)));
#endif

	return Ret;
}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

// EOF
