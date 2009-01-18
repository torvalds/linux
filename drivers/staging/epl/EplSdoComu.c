/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for SDO Command Layer module

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

                $RCSfile: EplSdoComu.c,v $

                $Author: D.Krueger $

                $Revision: 1.14 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/26 k.t.:   start of the implementation

****************************************************************************/

#include "user/EplSdoComu.h"

#if ((((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) == 0) &&\
     (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) == 0)   )

#error 'ERROR: At least SDO Server or SDO Client should be activate!'

#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) == 0) && (EPL_OBD_USE_KERNEL == FALSE)

#error 'ERROR: SDO Server needs OBDu module!'

#endif

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

#ifndef EPL_MAX_SDO_COM_CON
#define EPL_MAX_SDO_COM_CON         5
#endif

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

// intern events
typedef enum {
	kEplSdoComConEventSendFirst = 0x00,	// first frame to send
	kEplSdoComConEventRec = 0x01,	// frame received
	kEplSdoComConEventConEstablished = 0x02,	// connection established
	kEplSdoComConEventConClosed = 0x03,	// connection closed
	kEplSdoComConEventAckReceived = 0x04,	// acknowledge received by lower layer
	// -> continue sending
	kEplSdoComConEventFrameSended = 0x05,	// lower has send a frame
	kEplSdoComConEventInitError = 0x06,	// error duringinitialisiation
	// of the connection
	kEplSdoComConEventTimeout = 0x07	// timeout in lower layer
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
	    ,

	kEplSdoComConEventInitCon = 0x08,	// init connection (only client)
	kEplSdoComConEventAbort = 0x09	// abort sdo transfer (only client)
#endif
} tEplSdoComConEvent;

typedef enum {
	kEplSdoComSendTypeReq = 0x00,	// send a request
	kEplSdoComSendTypeAckRes = 0x01,	// send a resonse without data
	kEplSdoComSendTypeRes = 0x02,	// send response with data
	kEplSdoComSendTypeAbort = 0x03	// send abort
} tEplSdoComSendType;

// state of the state maschine
typedef enum {
	// General State
	kEplSdoComStateIdle = 0x00,	// idle state

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)
	// Server States
	kEplSdoComStateServerSegmTrans = 0x01,	// send following frames
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
	// Client States
	kEplSdoComStateClientWaitInit = 0x10,	// wait for init connection
	// on lower layer
	kEplSdoComStateClientConnected = 0x11,	// connection established
	kEplSdoComStateClientSegmTrans = 0x12	// send following frames
#endif
} tEplSdoComState;

// control structure for transaction
typedef struct {
	tEplSdoSeqConHdl m_SdoSeqConHdl;	// if != 0 -> entry used
	tEplSdoComState m_SdoComState;
	BYTE m_bTransactionId;
	unsigned int m_uiNodeId;	// NodeId of the target
	// -> needed to reinit connection
	//    after timeout
	tEplSdoTransType m_SdoTransType;	// Auto, Expedited, Segmented
	tEplSdoServiceType m_SdoServiceType;	// WriteByIndex, ReadByIndex
	tEplSdoType m_SdoProtType;	// protocol layer: Auto, Udp, Asnd, Pdo
	BYTE *m_pData;		// pointer to data
	unsigned int m_uiTransSize;	// number of bytes
	// to transfer
	unsigned int m_uiTransferredByte;	// number of bytes
	// already transferred
	tEplSdoFinishedCb m_pfnTransferFinished;	// callback function of the
	// application
	// -> called in the end of
	//    the SDO transfer
	void *m_pUserArg;	// user definable argument pointer

	DWORD m_dwLastAbortCode;	// save the last abort code
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
	// only for client
	unsigned int m_uiTargetIndex;	// index to access
	unsigned int m_uiTargetSubIndex;	// subiondex to access

	// for future use
	unsigned int m_uiTimeout;	// timeout for this connection

#endif

} tEplSdoComCon;

// instance table
typedef struct {
	tEplSdoComCon m_SdoComCon[EPL_MAX_SDO_COM_CON];

#if defined(WIN32) || defined(_WIN32)
	LPCRITICAL_SECTION m_pCriticalSection;
	CRITICAL_SECTION m_CriticalSection;
#endif

} tEplSdoComInstance;

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------
static tEplSdoComInstance SdoComInstance_g;
//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoComReceiveCb(tEplSdoSeqConHdl SdoSeqConHdl_p,
				     tEplAsySdoCom * pAsySdoCom_p,
				     unsigned int uiDataSize_p);

tEplKernel PUBLIC EplSdoComConCb(tEplSdoSeqConHdl SdoSeqConHdl_p,
				 tEplAsySdoConState AsySdoConState_p);

static tEplKernel EplSdoComSearchConIntern(tEplSdoSeqConHdl SdoSeqConHdl_p,
					   tEplSdoComConEvent SdoComConEvent_p,
					   tEplAsySdoCom * pAsySdoCom_p);

static tEplKernel EplSdoComProcessIntern(tEplSdoComConHdl SdoComCon_p,
					 tEplSdoComConEvent SdoComConEvent_p,
					 tEplAsySdoCom * pAsySdoCom_p);

static tEplKernel EplSdoComTransferFinished(tEplSdoComConHdl SdoComCon_p,
					    tEplSdoComCon * pSdoComCon_p,
					    tEplSdoComConState
					    SdoComConState_p);

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)
static tEplKernel EplSdoComServerInitReadByIndex(tEplSdoComCon * pSdoComCon_p,
						 tEplAsySdoCom * pAsySdoCom_p);

static tEplKernel EplSdoComServerSendFrameIntern(tEplSdoComCon * pSdoComCon_p,
						 unsigned int uiIndex_p,
						 unsigned int uiSubIndex_p,
						 tEplSdoComSendType SendType_p);

static tEplKernel EplSdoComServerInitWriteByIndex(tEplSdoComCon * pSdoComCon_p,
						  tEplAsySdoCom * pAsySdoCom_p);
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)

static tEplKernel EplSdoComClientSend(tEplSdoComCon * pSdoComCon_p);

static tEplKernel EplSdoComClientProcessFrame(tEplSdoComConHdl SdoComCon_p,
					      tEplAsySdoCom * pAsySdoCom_p);

static tEplKernel EplSdoComClientSendAbort(tEplSdoComCon * pSdoComCon_p,
					   DWORD dwAbortCode_p);
#endif

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  <SDO Command Layer>                                 */
/*                                                                         */
/*                                                                         */
/***************************************************************************/
//
// Description: SDO Command layer Modul
//
//
/***************************************************************************/

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplSdoComInit
//
// Description: Init first instance of the module
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
tEplKernel PUBLIC EplSdoComInit(void)
{
	tEplKernel Ret;

	Ret = EplSdoComAddInstance();

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplSdoComAddInstance
//
// Description: Init additional instance of the module
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
tEplKernel PUBLIC EplSdoComAddInstance(void)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// init controll structure
	EPL_MEMSET(&SdoComInstance_g, 0x00, sizeof(SdoComInstance_g));

	// init instance of lower layer
	Ret = EplSdoAsySeqAddInstance(EplSdoComReceiveCb, EplSdoComConCb);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#if defined(WIN32) || defined(_WIN32)
	// create critical section for process function
	SdoComInstance_g.m_pCriticalSection =
	    &SdoComInstance_g.m_CriticalSection;
	InitializeCriticalSection(SdoComInstance_g.m_pCriticalSection);
#endif

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplSdoComDelInstance
//
// Description: delete instance of the module
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
tEplKernel PUBLIC EplSdoComDelInstance(void)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

#if defined(WIN32) || defined(_WIN32)
	// delete critical section for process function
	DeleteCriticalSection(SdoComInstance_g.m_pCriticalSection);
#endif

	Ret = EplSdoAsySeqDelInstance();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplSdoComDefineCon
//
// Description: function defines a SDO connection to another node
//              -> init lower layer and returns a handle for the connection.
//              Two client connections to the same node via the same protocol
//              are not allowed. If this function detects such a situation
//              it will return kEplSdoComHandleExists and the handle of
//              the existing connection in pSdoComConHdl_p.
//              Using of existing server connections is possible.
//
// Parameters:  pSdoComConHdl_p     = pointer to the buffer of the handle
//              uiTargetNodeId_p    = NodeId of the targetnode
//              ProtType_p          = type of protocol to use for connection
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
tEplKernel PUBLIC EplSdoComDefineCon(tEplSdoComConHdl * pSdoComConHdl_p,
				     unsigned int uiTargetNodeId_p,
				     tEplSdoType ProtType_p)
{
	tEplKernel Ret;
	unsigned int uiCount;
	unsigned int uiFreeHdl;
	tEplSdoComCon *pSdoComCon;

	// check Parameter
	ASSERT(pSdoComConHdl_p != NULL);

	// check NodeId
	if ((uiTargetNodeId_p == EPL_C_ADR_INVALID)
	    || (uiTargetNodeId_p >= EPL_C_ADR_BROADCAST)) {
		Ret = kEplInvalidNodeId;

	}
	// search free control structure
	pSdoComCon = &SdoComInstance_g.m_SdoComCon[0];
	uiCount = 0;
	uiFreeHdl = EPL_MAX_SDO_COM_CON;
	while (uiCount < EPL_MAX_SDO_COM_CON) {
		if (pSdoComCon->m_SdoSeqConHdl == 0) {	// free entry
			uiFreeHdl = uiCount;
		} else if ((pSdoComCon->m_uiNodeId == uiTargetNodeId_p)
			   && (pSdoComCon->m_SdoProtType == ProtType_p)) {	// existing client connection with same node ID and same protocol type
			*pSdoComConHdl_p = uiCount;
			Ret = kEplSdoComHandleExists;
			goto Exit;
		}
		uiCount++;
		pSdoComCon++;
	}

	if (uiFreeHdl == EPL_MAX_SDO_COM_CON) {
		Ret = kEplSdoComNoFreeHandle;
		goto Exit;
	}

	pSdoComCon = &SdoComInstance_g.m_SdoComCon[uiFreeHdl];
	// save handle for application
	*pSdoComConHdl_p = uiFreeHdl;
	// save parameters
	pSdoComCon->m_SdoProtType = ProtType_p;
	pSdoComCon->m_uiNodeId = uiTargetNodeId_p;

	// set Transaction Id
	pSdoComCon->m_bTransactionId = 0;

	// check protocol
	switch (ProtType_p) {
		// udp
	case kEplSdoTypeUdp:
		{
			// call connection int function of lower layer
			Ret = EplSdoAsySeqInitCon(&pSdoComCon->m_SdoSeqConHdl,
						  pSdoComCon->m_uiNodeId,
						  kEplSdoTypeUdp);
			if (Ret != kEplSuccessful) {
				goto Exit;
			}
			break;
		}

		// Asend
	case kEplSdoTypeAsnd:
		{
			// call connection int function of lower layer
			Ret = EplSdoAsySeqInitCon(&pSdoComCon->m_SdoSeqConHdl,
						  pSdoComCon->m_uiNodeId,
						  kEplSdoTypeAsnd);
			if (Ret != kEplSuccessful) {
				goto Exit;
			}
			break;
		}

		// Pdo -> not supported
	case kEplSdoTypePdo:
	default:
		{
			Ret = kEplSdoComUnsupportedProt;
			goto Exit;
		}
	}			// end of switch(m_ProtType_p)

	// call process function
	Ret = EplSdoComProcessIntern(uiFreeHdl,
				     kEplSdoComConEventInitCon, NULL);

      Exit:
	return Ret;
}
#endif
//---------------------------------------------------------------------------
//
// Function:    EplSdoComInitTransferByIndex
//
// Description: function init SDO Transfer for a defined connection
//
//
//
// Parameters:  SdoComTransParam_p    = Structure with parameters for connection
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
tEplKernel PUBLIC EplSdoComInitTransferByIndex(tEplSdoComTransParamByIndex *
					       pSdoComTransParam_p)
{
	tEplKernel Ret;
	tEplSdoComCon *pSdoComCon;

	// check parameter
	if ((pSdoComTransParam_p->m_uiSubindex >= 0xFF)
	    || (pSdoComTransParam_p->m_uiIndex == 0)
	    || (pSdoComTransParam_p->m_uiIndex > 0xFFFF)
	    || (pSdoComTransParam_p->m_pData == NULL)
	    || (pSdoComTransParam_p->m_uiDataSize == 0)) {
		Ret = kEplSdoComInvalidParam;
		goto Exit;
	}

	if (pSdoComTransParam_p->m_SdoComConHdl >= EPL_MAX_SDO_COM_CON) {
		Ret = kEplSdoComInvalidHandle;
		goto Exit;
	}
	// get pointer to control structure of connection
	pSdoComCon =
	    &SdoComInstance_g.m_SdoComCon[pSdoComTransParam_p->m_SdoComConHdl];

	// check if handle ok
	if (pSdoComCon->m_SdoSeqConHdl == 0) {
		Ret = kEplSdoComInvalidHandle;
		goto Exit;
	}
	// check if command layer is idle
	if ((pSdoComCon->m_uiTransferredByte + pSdoComCon->m_uiTransSize) > 0) {	// handle is not idle
		Ret = kEplSdoComHandleBusy;
		goto Exit;
	}
	// save parameter
	// callback function for end of transfer
	pSdoComCon->m_pfnTransferFinished =
	    pSdoComTransParam_p->m_pfnSdoFinishedCb;
	pSdoComCon->m_pUserArg = pSdoComTransParam_p->m_pUserArg;

	// set type of SDO command
	if (pSdoComTransParam_p->m_SdoAccessType == kEplSdoAccessTypeRead) {
		pSdoComCon->m_SdoServiceType = kEplSdoServiceReadByIndex;
	} else {
		pSdoComCon->m_SdoServiceType = kEplSdoServiceWriteByIndex;

	}
	// save pointer to data
	pSdoComCon->m_pData = pSdoComTransParam_p->m_pData;
	// maximal bytes to transfer
	pSdoComCon->m_uiTransSize = pSdoComTransParam_p->m_uiDataSize;
	// bytes already transfered
	pSdoComCon->m_uiTransferredByte = 0;

	// reset parts of control structure
	pSdoComCon->m_dwLastAbortCode = 0;
	pSdoComCon->m_SdoTransType = kEplSdoTransAuto;
	// save timeout
	//pSdoComCon->m_uiTimeout = SdoComTransParam_p.m_uiTimeout;

	// save index and subindex
	pSdoComCon->m_uiTargetIndex = pSdoComTransParam_p->m_uiIndex;
	pSdoComCon->m_uiTargetSubIndex = pSdoComTransParam_p->m_uiSubindex;

	// call process function
	Ret = EplSdoComProcessIntern(pSdoComTransParam_p->m_SdoComConHdl, kEplSdoComConEventSendFirst,	// event to start transfer
				     NULL);

      Exit:
	return Ret;

}
#endif

//---------------------------------------------------------------------------
//
// Function:    EplSdoComUndefineCon
//
// Description: function undefine a SDO connection
//
//
//
// Parameters:  SdoComConHdl_p    = handle for the connection
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
tEplKernel PUBLIC EplSdoComUndefineCon(tEplSdoComConHdl SdoComConHdl_p)
{
	tEplKernel Ret;
	tEplSdoComCon *pSdoComCon;

	Ret = kEplSuccessful;

	if (SdoComConHdl_p >= EPL_MAX_SDO_COM_CON) {
		Ret = kEplSdoComInvalidHandle;
		goto Exit;
	}
	// get pointer to control structure
	pSdoComCon = &SdoComInstance_g.m_SdoComCon[SdoComConHdl_p];

	// $$$ d.k. abort a running transfer before closing the sequence layer

	if (((pSdoComCon->m_SdoSeqConHdl & ~EPL_SDO_SEQ_HANDLE_MASK) !=
	     EPL_SDO_SEQ_INVALID_HDL)
	    && (pSdoComCon->m_SdoSeqConHdl != 0)) {
		// close connection in lower layer
		switch (pSdoComCon->m_SdoProtType) {
		case kEplSdoTypeAsnd:
		case kEplSdoTypeUdp:
			{
				Ret =
				    EplSdoAsySeqDelCon(pSdoComCon->
						       m_SdoSeqConHdl);
				break;
			}

		case kEplSdoTypePdo:
		case kEplSdoTypeAuto:
		default:
			{
				Ret = kEplSdoComUnsupportedProt;
				goto Exit;
			}

		}		// end of switch(pSdoComCon->m_SdoProtType)
	}

	// clean controll structure
	EPL_MEMSET(pSdoComCon, 0x00, sizeof(tEplSdoComCon));
      Exit:
	return Ret;
}
#endif
//---------------------------------------------------------------------------
//
// Function:    EplSdoComGetState
//
// Description: function returns the state fo the connection
//
//
//
// Parameters:  SdoComConHdl_p    = handle for the connection
//              pSdoComFinished_p = pointer to structur for sdo state
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
tEplKernel PUBLIC EplSdoComGetState(tEplSdoComConHdl SdoComConHdl_p,
				    tEplSdoComFinished * pSdoComFinished_p)
{
	tEplKernel Ret;
	tEplSdoComCon *pSdoComCon;

	Ret = kEplSuccessful;

	if (SdoComConHdl_p >= EPL_MAX_SDO_COM_CON) {
		Ret = kEplSdoComInvalidHandle;
		goto Exit;
	}
	// get pointer to control structure
	pSdoComCon = &SdoComInstance_g.m_SdoComCon[SdoComConHdl_p];

	// check if handle ok
	if (pSdoComCon->m_SdoSeqConHdl == 0) {
		Ret = kEplSdoComInvalidHandle;
		goto Exit;
	}

	pSdoComFinished_p->m_pUserArg = pSdoComCon->m_pUserArg;
	pSdoComFinished_p->m_uiNodeId = pSdoComCon->m_uiNodeId;
	pSdoComFinished_p->m_uiTargetIndex = pSdoComCon->m_uiTargetIndex;
	pSdoComFinished_p->m_uiTargetSubIndex = pSdoComCon->m_uiTargetSubIndex;
	pSdoComFinished_p->m_uiTransferredByte =
	    pSdoComCon->m_uiTransferredByte;
	pSdoComFinished_p->m_dwAbortCode = pSdoComCon->m_dwLastAbortCode;
	pSdoComFinished_p->m_SdoComConHdl = SdoComConHdl_p;
	if (pSdoComCon->m_SdoServiceType == kEplSdoServiceWriteByIndex) {
		pSdoComFinished_p->m_SdoAccessType = kEplSdoAccessTypeWrite;
	} else {
		pSdoComFinished_p->m_SdoAccessType = kEplSdoAccessTypeRead;
	}

	if (pSdoComCon->m_dwLastAbortCode != 0) {	// sdo abort
		pSdoComFinished_p->m_SdoComConState =
		    kEplSdoComTransferRxAborted;

		// delete abort code
		pSdoComCon->m_dwLastAbortCode = 0;

	} else if ((pSdoComCon->m_SdoSeqConHdl & ~EPL_SDO_SEQ_HANDLE_MASK) == EPL_SDO_SEQ_INVALID_HDL) {	// check state
		pSdoComFinished_p->m_SdoComConState =
		    kEplSdoComTransferLowerLayerAbort;
	} else if (pSdoComCon->m_SdoComState == kEplSdoComStateClientWaitInit) {
		// finished
		pSdoComFinished_p->m_SdoComConState =
		    kEplSdoComTransferNotActive;
	} else if (pSdoComCon->m_uiTransSize == 0) {	// finished
		pSdoComFinished_p->m_SdoComConState =
		    kEplSdoComTransferFinished;
	}

      Exit:
	return Ret;

}
#endif
//---------------------------------------------------------------------------
//
// Function:    EplSdoComSdoAbort
//
// Description: function abort a sdo transfer
//
//
//
// Parameters:  SdoComConHdl_p    = handle for the connection
//              dwAbortCode_p     = abort code
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
tEplKernel PUBLIC EplSdoComSdoAbort(tEplSdoComConHdl SdoComConHdl_p,
				    DWORD dwAbortCode_p)
{
	tEplKernel Ret;
	tEplSdoComCon *pSdoComCon;

	if (SdoComConHdl_p >= EPL_MAX_SDO_COM_CON) {
		Ret = kEplSdoComInvalidHandle;
		goto Exit;
	}
	// get pointer to control structure of connection
	pSdoComCon = &SdoComInstance_g.m_SdoComCon[SdoComConHdl_p];

	// check if handle ok
	if (pSdoComCon->m_SdoSeqConHdl == 0) {
		Ret = kEplSdoComInvalidHandle;
		goto Exit;
	}
	// save pointer to abort code
	pSdoComCon->m_pData = (BYTE *) & dwAbortCode_p;

	Ret = EplSdoComProcessIntern(SdoComConHdl_p,
				     kEplSdoComConEventAbort,
				     (tEplAsySdoCom *) NULL);

      Exit:
	return Ret;
}
#endif

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:        EplSdoComReceiveCb
//
// Description:     callback function for SDO Sequence Layer
//                  -> indicates new data
//
//
//
// Parameters:      SdoSeqConHdl_p = Handle for connection
//                  pAsySdoCom_p   = pointer to data
//                  uiDataSize_p   = size of data ($$$ not used yet, but it should)
//
//
// Returns:
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoComReceiveCb(tEplSdoSeqConHdl SdoSeqConHdl_p,
				     tEplAsySdoCom * pAsySdoCom_p,
				     unsigned int uiDataSize_p)
{
	tEplKernel Ret;

	// search connection internally
	Ret = EplSdoComSearchConIntern(SdoSeqConHdl_p,
				       kEplSdoComConEventRec, pAsySdoCom_p);

	EPL_DBGLVL_SDO_TRACE3
	    ("EplSdoComReceiveCb SdoSeqConHdl: 0x%X, First Byte of pAsySdoCom_p: 0x%02X, uiDataSize_p: 0x%04X\n",
	     SdoSeqConHdl_p, (WORD) pAsySdoCom_p->m_le_abCommandData[0],
	     uiDataSize_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:        EplSdoComConCb
//
// Description:     callback function called by SDO Sequence Layer to inform
//                  command layer about state change of connection
//
//
//
// Parameters:      SdoSeqConHdl_p      = Handle of the connection
//                  AsySdoConState_p    = Event of the connection
//
//
// Returns:         tEplKernel  = Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplSdoComConCb(tEplSdoSeqConHdl SdoSeqConHdl_p,
				 tEplAsySdoConState AsySdoConState_p)
{
	tEplKernel Ret;
	tEplSdoComConEvent SdoComConEvent = kEplSdoComConEventSendFirst;

	Ret = kEplSuccessful;

	// check state
	switch (AsySdoConState_p) {
	case kAsySdoConStateConnected:
		{
			EPL_DBGLVL_SDO_TRACE0("Connection established\n");
			SdoComConEvent = kEplSdoComConEventConEstablished;
			// start transmission if needed
			break;
		}

	case kAsySdoConStateInitError:
		{
			EPL_DBGLVL_SDO_TRACE0("Error during initialisation\n");
			SdoComConEvent = kEplSdoComConEventInitError;
			// inform app about error and close sequence layer handle
			break;
		}

	case kAsySdoConStateConClosed:
		{
			EPL_DBGLVL_SDO_TRACE0("Connection closed\n");
			SdoComConEvent = kEplSdoComConEventConClosed;
			// close sequence layer handle
			break;
		}

	case kAsySdoConStateAckReceived:
		{
			EPL_DBGLVL_SDO_TRACE0("Acknowlage received\n");
			SdoComConEvent = kEplSdoComConEventAckReceived;
			// continue transmission
			break;
		}

	case kAsySdoConStateFrameSended:
		{
			EPL_DBGLVL_SDO_TRACE0("One Frame sent\n");
			SdoComConEvent = kEplSdoComConEventFrameSended;
			// to continue transmission
			break;

		}

	case kAsySdoConStateTimeout:
		{
			EPL_DBGLVL_SDO_TRACE0("Timeout\n");
			SdoComConEvent = kEplSdoComConEventTimeout;
			// close sequence layer handle
			break;

		}
	}			// end of switch(AsySdoConState_p)

	Ret = EplSdoComSearchConIntern(SdoSeqConHdl_p,
				       SdoComConEvent, (tEplAsySdoCom *) NULL);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:        EplSdoComSearchConIntern
//
// Description:     search a Sdo Sequence Layer connection handle in the
//                  control structure of the Command Layer
//
// Parameters:      SdoSeqConHdl_p     = Handle to search
//                  SdoComConEvent_p = event to process
//                  pAsySdoCom_p     = pointer to received frame
//
// Returns:         tEplKernel
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoComSearchConIntern(tEplSdoSeqConHdl SdoSeqConHdl_p,
					   tEplSdoComConEvent SdoComConEvent_p,
					   tEplAsySdoCom * pAsySdoCom_p)
{
	tEplKernel Ret;
	tEplSdoComCon *pSdoComCon;
	tEplSdoComConHdl HdlCount;
	tEplSdoComConHdl HdlFree;

	Ret = kEplSdoComNotResponsible;

	// get pointer to first element of the array
	pSdoComCon = &SdoComInstance_g.m_SdoComCon[0];
	HdlCount = 0;
	HdlFree = 0xFFFF;
	while (HdlCount < EPL_MAX_SDO_COM_CON) {
		if (pSdoComCon->m_SdoSeqConHdl == SdoSeqConHdl_p) {	// matching command layer handle found
			Ret = EplSdoComProcessIntern(HdlCount,
						     SdoComConEvent_p,
						     pAsySdoCom_p);
		} else if ((pSdoComCon->m_SdoSeqConHdl == 0)
			   && (HdlFree == 0xFFFF)) {
			HdlFree = HdlCount;
		}

		pSdoComCon++;
		HdlCount++;
	}

	if (Ret == kEplSdoComNotResponsible) {	// no responsible command layer handle found
		if (HdlFree == 0xFFFF) {	// no free handle
			// delete connection immediately
			// 2008/04/14 m.u./d.k. This connection actually does not exist.
			//                      pSdoComCon is invalid.
			// Ret = EplSdoAsySeqDelCon(pSdoComCon->m_SdoSeqConHdl);
			Ret = kEplSdoComNoFreeHandle;
		} else {	// create new handle
			HdlCount = HdlFree;
			pSdoComCon = &SdoComInstance_g.m_SdoComCon[HdlCount];
			pSdoComCon->m_SdoSeqConHdl = SdoSeqConHdl_p;
			Ret = EplSdoComProcessIntern(HdlCount,
						     SdoComConEvent_p,
						     pAsySdoCom_p);
		}
	}

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:        EplSdoComProcessIntern
//
// Description:     search a Sdo Sequence Layer connection handle in the
//                  control structer of the Command Layer
//
//
//
// Parameters:      SdoComCon_p     = index of control structure of connection
//                  SdoComConEvent_p = event to process
//                  pAsySdoCom_p     = pointer to received frame
//
// Returns:         tEplKernel  =  errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoComProcessIntern(tEplSdoComConHdl SdoComCon_p,
					 tEplSdoComConEvent SdoComConEvent_p,
					 tEplAsySdoCom * pAsySdoCom_p)
{
	tEplKernel Ret;
	tEplSdoComCon *pSdoComCon;
	BYTE bFlag;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)
	DWORD dwAbortCode;
	unsigned int uiSize;
#endif

#if defined(WIN32) || defined(_WIN32)
	// enter  critical section for process function
	EnterCriticalSection(SdoComInstance_g.m_pCriticalSection);
	EPL_DBGLVL_SDO_TRACE0
	    ("\n\tEnterCiticalSection EplSdoComProcessIntern\n\n");
#endif

	Ret = kEplSuccessful;

	// get pointer to control structure
	pSdoComCon = &SdoComInstance_g.m_SdoComCon[SdoComCon_p];

	// process state maschine
	switch (pSdoComCon->m_SdoComState) {
		// idle state
	case kEplSdoComStateIdle:
		{
			// check events
			switch (SdoComConEvent_p) {
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
				// init con for client
			case kEplSdoComConEventInitCon:
				{

					// call of the init function already
					// processed in EplSdoComDefineCon()
					// only change state to kEplSdoComStateClientWaitInit
					pSdoComCon->m_SdoComState =
					    kEplSdoComStateClientWaitInit;
					break;
				}
#endif

				// int con for server
			case kEplSdoComConEventRec:
				{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)
					// check if init of an transfer and no SDO abort
					if ((pAsySdoCom_p->m_le_bFlags & 0x80) == 0) {	// SDO request
						if ((pAsySdoCom_p->m_le_bFlags & 0x40) == 0) {	// no SDO abort
							// save tansaction id
							pSdoComCon->
							    m_bTransactionId =
							    AmiGetByteFromLe
							    (&pAsySdoCom_p->
							     m_le_bTransactionId);
							// check command
							switch (pAsySdoCom_p->
								m_le_bCommandId)
							{
							case kEplSdoServiceNIL:
								{	// simply acknowlegde NIL command on sequence layer

									Ret =
									    EplSdoAsySeqSendData
									    (pSdoComCon->
									     m_SdoSeqConHdl,
									     0,
									     (tEplFrame
									      *)
									     NULL);

									break;
								}

							case kEplSdoServiceReadByIndex:
								{	// read by index

									// search entry an start transfer
									EplSdoComServerInitReadByIndex
									    (pSdoComCon,
									     pAsySdoCom_p);
									// check next state
									if (pSdoComCon->m_uiTransSize == 0) {	// ready -> stay idle
										pSdoComCon->
										    m_SdoComState
										    =
										    kEplSdoComStateIdle;
										// reset abort code
										pSdoComCon->
										    m_dwLastAbortCode
										    =
										    0;
									} else {	// segmented transfer
										pSdoComCon->
										    m_SdoComState
										    =
										    kEplSdoComStateServerSegmTrans;
									}

									break;
								}

							case kEplSdoServiceWriteByIndex:
								{

									// search entry an start write
									EplSdoComServerInitWriteByIndex
									    (pSdoComCon,
									     pAsySdoCom_p);
									// check next state
									if (pSdoComCon->m_uiTransSize == 0) {	// already -> stay idle
										pSdoComCon->
										    m_SdoComState
										    =
										    kEplSdoComStateIdle;
										// reset abort code
										pSdoComCon->
										    m_dwLastAbortCode
										    =
										    0;
									} else {	// segmented transfer
										pSdoComCon->
										    m_SdoComState
										    =
										    kEplSdoComStateServerSegmTrans;
									}

									break;
								}

							default:
								{
									//  unsupported command
									//       -> abort senden
									dwAbortCode
									    =
									    EPL_SDOAC_UNKNOWN_COMMAND_SPECIFIER;
									// send abort
									pSdoComCon->
									    m_pData
									    =
									    (BYTE
									     *)
									    &
									    dwAbortCode;
									Ret =
									    EplSdoComServerSendFrameIntern
									    (pSdoComCon,
									     0,
									     0,
									     kEplSdoComSendTypeAbort);

								}

							}	// end of switch(pAsySdoCom_p->m_le_bCommandId)
						}
					} else {	// this command layer handle is not responsible
						// (wrong direction or wrong transaction ID)
						Ret = kEplSdoComNotResponsible;
						goto Exit;
					}
#endif // end of #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)

					break;
				}

				// connection closed
			case kEplSdoComConEventInitError:
			case kEplSdoComConEventTimeout:
			case kEplSdoComConEventConClosed:
				{
					Ret =
					    EplSdoAsySeqDelCon(pSdoComCon->
							       m_SdoSeqConHdl);
					// clean control structure
					EPL_MEMSET(pSdoComCon, 0x00,
						   sizeof(tEplSdoComCon));
					break;
				}

			default:
				// d.k. do nothing
				break;
			}	// end of switch(SdoComConEvent_p)
			break;
		}

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)
		//-------------------------------------------------------------------------
		// SDO Server part
		// segmented transfer
	case kEplSdoComStateServerSegmTrans:
		{
			// check events
			switch (SdoComConEvent_p) {
				// send next frame
			case kEplSdoComConEventAckReceived:
			case kEplSdoComConEventFrameSended:
				{
					// check if it is a read
					if (pSdoComCon->m_SdoServiceType ==
					    kEplSdoServiceReadByIndex) {
						// send next frame
						EplSdoComServerSendFrameIntern
						    (pSdoComCon, 0, 0,
						     kEplSdoComSendTypeRes);
						// if all send -> back to idle
						if (pSdoComCon->m_uiTransSize == 0) {	// back to idle
							pSdoComCon->
							    m_SdoComState =
							    kEplSdoComStateIdle;
							// reset abort code
							pSdoComCon->
							    m_dwLastAbortCode =
							    0;
						}

					}
					break;
				}

				// process next frame
			case kEplSdoComConEventRec:
				{
					// check if the frame is a SDO response and has the right transaction ID
					bFlag =
					    AmiGetByteFromLe(&pAsySdoCom_p->
							     m_le_bFlags);
					if (((bFlag & 0x80) != 0)
					    &&
					    (AmiGetByteFromLe
					     (&pAsySdoCom_p->
					      m_le_bTransactionId) ==
					     pSdoComCon->m_bTransactionId)) {
						// check if it is a abort
						if ((bFlag & 0x40) != 0) {	// SDO abort
							// clear control structure
							pSdoComCon->
							    m_uiTransSize = 0;
							pSdoComCon->
							    m_uiTransferredByte
							    = 0;
							// change state
							pSdoComCon->
							    m_SdoComState =
							    kEplSdoComStateIdle;
							// reset abort code
							pSdoComCon->
							    m_dwLastAbortCode =
							    0;
							// d.k.: do not execute anything further on this command
							break;
						}
						// check if it is a write
						if (pSdoComCon->
						    m_SdoServiceType ==
						    kEplSdoServiceWriteByIndex)
						{
							// write data to OD
							uiSize =
							    AmiGetWordFromLe
							    (&pAsySdoCom_p->
							     m_le_wSegmentSize);
							if (pSdoComCon->
							    m_dwLastAbortCode ==
							    0) {
								EPL_MEMCPY
								    (pSdoComCon->
								     m_pData,
								     &pAsySdoCom_p->
								     m_le_abCommandData
								     [0],
								     uiSize);
							}
							// update counter
							pSdoComCon->
							    m_uiTransferredByte
							    += uiSize;
							pSdoComCon->
							    m_uiTransSize -=
							    uiSize;

							// update pointer
							if (pSdoComCon->
							    m_dwLastAbortCode ==
							    0) {
								( /*(BYTE*) */
								 pSdoComCon->
								 m_pData) +=
						      uiSize;
							}
							// check end of transfer
							if ((pAsySdoCom_p->m_le_bFlags & 0x30) == 0x30) {	// transfer ready
								pSdoComCon->
								    m_uiTransSize
								    = 0;

								if (pSdoComCon->
								    m_dwLastAbortCode
								    == 0) {
									// send response
									// send next frame
									EplSdoComServerSendFrameIntern
									    (pSdoComCon,
									     0,
									     0,
									     kEplSdoComSendTypeRes);
									// if all send -> back to idle
									if (pSdoComCon->m_uiTransSize == 0) {	// back to idle
										pSdoComCon->
										    m_SdoComState
										    =
										    kEplSdoComStateIdle;
										// reset abort code
										pSdoComCon->
										    m_dwLastAbortCode
										    =
										    0;
									}
								} else {	// send dabort code
									// send abort
									pSdoComCon->
									    m_pData
									    =
									    (BYTE
									     *)
									    &
									    pSdoComCon->
									    m_dwLastAbortCode;
									Ret =
									    EplSdoComServerSendFrameIntern
									    (pSdoComCon,
									     0,
									     0,
									     kEplSdoComSendTypeAbort);

									// reset abort code
									pSdoComCon->
									    m_dwLastAbortCode
									    = 0;

								}
							} else {
								// send acknowledge without any Command layer data
								Ret =
								    EplSdoAsySeqSendData
								    (pSdoComCon->
								     m_SdoSeqConHdl,
								     0,
								     (tEplFrame
								      *) NULL);
							}
						}
					} else {	// this command layer handle is not responsible
						// (wrong direction or wrong transaction ID)
						Ret = kEplSdoComNotResponsible;
						goto Exit;
					}
					break;
				}

				// connection closed
			case kEplSdoComConEventInitError:
			case kEplSdoComConEventTimeout:
			case kEplSdoComConEventConClosed:
				{
					Ret =
					    EplSdoAsySeqDelCon(pSdoComCon->
							       m_SdoSeqConHdl);
					// clean control structure
					EPL_MEMSET(pSdoComCon, 0x00,
						   sizeof(tEplSdoComCon));
					break;
				}

			default:
				// d.k. do nothing
				break;
			}	// end of switch(SdoComConEvent_p)

			break;
		}
#endif // endif of #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
		//-------------------------------------------------------------------------
		// SDO Client part
		// wait for finish of establishing connection
	case kEplSdoComStateClientWaitInit:
		{

			// if connection handle is invalid reinit connection
			// d.k.: this will be done only on new events (i.e. InitTransfer)
			if ((pSdoComCon->
			     m_SdoSeqConHdl & ~EPL_SDO_SEQ_HANDLE_MASK) ==
			    EPL_SDO_SEQ_INVALID_HDL) {
				// check kind of connection to reinit
				// check protocol
				switch (pSdoComCon->m_SdoProtType) {
					// udp
				case kEplSdoTypeUdp:
					{
						// call connection int function of lower layer
						Ret =
						    EplSdoAsySeqInitCon
						    (&pSdoComCon->
						     m_SdoSeqConHdl,
						     pSdoComCon->m_uiNodeId,
						     kEplSdoTypeUdp);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						break;
					}

					// Asend -> not supported
				case kEplSdoTypeAsnd:
					{
						// call connection int function of lower layer
						Ret =
						    EplSdoAsySeqInitCon
						    (&pSdoComCon->
						     m_SdoSeqConHdl,
						     pSdoComCon->m_uiNodeId,
						     kEplSdoTypeAsnd);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						break;
					}

					// Pdo -> not supported
				case kEplSdoTypePdo:
				default:
					{
						Ret = kEplSdoComUnsupportedProt;
						goto Exit;
					}
				}	// end of switch(m_ProtType_p)
				// d.k.: reset transaction ID, because new sequence layer connection was initialized
				// $$$ d.k. is this really necessary?
				//pSdoComCon->m_bTransactionId = 0;
			}
			// check events
			switch (SdoComConEvent_p) {
				// connection established
			case kEplSdoComConEventConEstablished:
				{
					//send first frame if needed
					if ((pSdoComCon->m_uiTransSize > 0)
					    && (pSdoComCon->m_uiTargetIndex != 0)) {	// start SDO transfer
						Ret =
						    EplSdoComClientSend
						    (pSdoComCon);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						// check if segemted transfer
						if (pSdoComCon->
						    m_SdoTransType ==
						    kEplSdoTransSegmented) {
							pSdoComCon->
							    m_SdoComState =
							    kEplSdoComStateClientSegmTrans;
							goto Exit;
						}
					}
					// goto state kEplSdoComStateClientConnected
					pSdoComCon->m_SdoComState =
					    kEplSdoComStateClientConnected;
					goto Exit;
				}

			case kEplSdoComConEventSendFirst:
				{
					// infos for transfer already saved by function EplSdoComInitTransferByIndex
					break;
				}

			case kEplSdoComConEventConClosed:
			case kEplSdoComConEventInitError:
			case kEplSdoComConEventTimeout:
				{
					// close sequence layer handle
					Ret =
					    EplSdoAsySeqDelCon(pSdoComCon->
							       m_SdoSeqConHdl);
					pSdoComCon->m_SdoSeqConHdl |=
					    EPL_SDO_SEQ_INVALID_HDL;
					// call callback function
					if (SdoComConEvent_p ==
					    kEplSdoComConEventTimeout) {
						pSdoComCon->m_dwLastAbortCode =
						    EPL_SDOAC_TIME_OUT;
					} else {
						pSdoComCon->m_dwLastAbortCode =
						    0;
					}
					Ret =
					    EplSdoComTransferFinished
					    (SdoComCon_p, pSdoComCon,
					     kEplSdoComTransferLowerLayerAbort);
					// d.k.: do not clean control structure
					break;
				}

			default:
				// d.k. do nothing
				break;

			}	// end of  switch(SdoComConEvent_p)
			break;
		}

		// connected
	case kEplSdoComStateClientConnected:
		{
			// check events
			switch (SdoComConEvent_p) {
				// send a frame
			case kEplSdoComConEventSendFirst:
			case kEplSdoComConEventAckReceived:
			case kEplSdoComConEventFrameSended:
				{
					Ret = EplSdoComClientSend(pSdoComCon);
					if (Ret != kEplSuccessful) {
						goto Exit;
					}
					// check if read transfer finished
					if ((pSdoComCon->m_uiTransSize == 0)
					    && (pSdoComCon->
						m_uiTransferredByte != 0)
					    && (pSdoComCon->m_SdoServiceType ==
						kEplSdoServiceReadByIndex)) {
						// inc transaction id
						pSdoComCon->m_bTransactionId++;
						// call callback of application
						pSdoComCon->m_dwLastAbortCode =
						    0;
						Ret =
						    EplSdoComTransferFinished
						    (SdoComCon_p, pSdoComCon,
						     kEplSdoComTransferFinished);

						goto Exit;
					}
					// check if segemted transfer
					if (pSdoComCon->m_SdoTransType ==
					    kEplSdoTransSegmented) {
						pSdoComCon->m_SdoComState =
						    kEplSdoComStateClientSegmTrans;
						goto Exit;
					}
					break;
				}

				// frame received
			case kEplSdoComConEventRec:
				{
					// check if the frame is a SDO response and has the right transaction ID
					bFlag =
					    AmiGetByteFromLe(&pAsySdoCom_p->
							     m_le_bFlags);
					if (((bFlag & 0x80) != 0)
					    &&
					    (AmiGetByteFromLe
					     (&pAsySdoCom_p->
					      m_le_bTransactionId) ==
					     pSdoComCon->m_bTransactionId)) {
						// check if abort or not
						if ((bFlag & 0x40) != 0) {
							// send acknowledge without any Command layer data
							Ret =
							    EplSdoAsySeqSendData
							    (pSdoComCon->
							     m_SdoSeqConHdl, 0,
							     (tEplFrame *)
							     NULL);
							// inc transaction id
							pSdoComCon->
							    m_bTransactionId++;
							// save abort code
							pSdoComCon->
							    m_dwLastAbortCode =
							    AmiGetDwordFromLe
							    (&pAsySdoCom_p->
							     m_le_abCommandData
							     [0]);
							// call callback of application
							Ret =
							    EplSdoComTransferFinished
							    (SdoComCon_p,
							     pSdoComCon,
							     kEplSdoComTransferRxAborted);

							goto Exit;
						} else {	// normal frame received
							// check frame
							Ret =
							    EplSdoComClientProcessFrame
							    (SdoComCon_p,
							     pAsySdoCom_p);

							// check if transfer ready
							if (pSdoComCon->
							    m_uiTransSize ==
							    0) {
								// send acknowledge without any Command layer data
								Ret =
								    EplSdoAsySeqSendData
								    (pSdoComCon->
								     m_SdoSeqConHdl,
								     0,
								     (tEplFrame
								      *) NULL);
								// inc transaction id
								pSdoComCon->
								    m_bTransactionId++;
								// call callback of application
								pSdoComCon->
								    m_dwLastAbortCode
								    = 0;
								Ret =
								    EplSdoComTransferFinished
								    (SdoComCon_p,
								     pSdoComCon,
								     kEplSdoComTransferFinished);

								goto Exit;
							}

						}
					} else {	// this command layer handle is not responsible
						// (wrong direction or wrong transaction ID)
						Ret = kEplSdoComNotResponsible;
						goto Exit;
					}
					break;
				}

				// connection closed event go back to kEplSdoComStateClientWaitInit
			case kEplSdoComConEventConClosed:
				{	// connection closed by communication partner
					// close sequence layer handle
					Ret =
					    EplSdoAsySeqDelCon(pSdoComCon->
							       m_SdoSeqConHdl);
					// set handle to invalid and enter kEplSdoComStateClientWaitInit
					pSdoComCon->m_SdoSeqConHdl |=
					    EPL_SDO_SEQ_INVALID_HDL;
					// change state
					pSdoComCon->m_SdoComState =
					    kEplSdoComStateClientWaitInit;

					// call callback of application
					pSdoComCon->m_dwLastAbortCode = 0;
					Ret =
					    EplSdoComTransferFinished
					    (SdoComCon_p, pSdoComCon,
					     kEplSdoComTransferLowerLayerAbort);

					goto Exit;

					break;
				}

				// abort to send from higher layer
			case kEplSdoComConEventAbort:
				{
					EplSdoComClientSendAbort(pSdoComCon,
								 *((DWORD *)
								   pSdoComCon->
								   m_pData));

					// inc transaction id
					pSdoComCon->m_bTransactionId++;
					// call callback of application
					pSdoComCon->m_dwLastAbortCode =
					    *((DWORD *) pSdoComCon->m_pData);
					Ret =
					    EplSdoComTransferFinished
					    (SdoComCon_p, pSdoComCon,
					     kEplSdoComTransferTxAborted);

					break;
				}

			case kEplSdoComConEventInitError:
			case kEplSdoComConEventTimeout:
				{
					// close sequence layer handle
					Ret =
					    EplSdoAsySeqDelCon(pSdoComCon->
							       m_SdoSeqConHdl);
					pSdoComCon->m_SdoSeqConHdl |=
					    EPL_SDO_SEQ_INVALID_HDL;
					// change state
					pSdoComCon->m_SdoComState =
					    kEplSdoComStateClientWaitInit;
					// call callback of application
					pSdoComCon->m_dwLastAbortCode =
					    EPL_SDOAC_TIME_OUT;
					Ret =
					    EplSdoComTransferFinished
					    (SdoComCon_p, pSdoComCon,
					     kEplSdoComTransferLowerLayerAbort);

				}

			default:
				// d.k. do nothing
				break;

			}	// end of switch(SdoComConEvent_p)

			break;
		}

		// process segmented transfer
	case kEplSdoComStateClientSegmTrans:
		{
			// check events
			switch (SdoComConEvent_p) {
				// sned a frame
			case kEplSdoComConEventSendFirst:
			case kEplSdoComConEventAckReceived:
			case kEplSdoComConEventFrameSended:
				{
					Ret = EplSdoComClientSend(pSdoComCon);
					if (Ret != kEplSuccessful) {
						goto Exit;
					}
					// check if read transfer finished
					if ((pSdoComCon->m_uiTransSize == 0)
					    && (pSdoComCon->m_SdoServiceType ==
						kEplSdoServiceReadByIndex)) {
						// inc transaction id
						pSdoComCon->m_bTransactionId++;
						// change state
						pSdoComCon->m_SdoComState =
						    kEplSdoComStateClientConnected;
						// call callback of application
						pSdoComCon->m_dwLastAbortCode =
						    0;
						Ret =
						    EplSdoComTransferFinished
						    (SdoComCon_p, pSdoComCon,
						     kEplSdoComTransferFinished);

						goto Exit;
					}

					break;
				}

				// frame received
			case kEplSdoComConEventRec:
				{
					// check if the frame is a response
					bFlag =
					    AmiGetByteFromLe(&pAsySdoCom_p->
							     m_le_bFlags);
					if (((bFlag & 0x80) != 0)
					    &&
					    (AmiGetByteFromLe
					     (&pAsySdoCom_p->
					      m_le_bTransactionId) ==
					     pSdoComCon->m_bTransactionId)) {
						// check if abort or not
						if ((bFlag & 0x40) != 0) {
							// send acknowledge without any Command layer data
							Ret =
							    EplSdoAsySeqSendData
							    (pSdoComCon->
							     m_SdoSeqConHdl, 0,
							     (tEplFrame *)
							     NULL);
							// inc transaction id
							pSdoComCon->
							    m_bTransactionId++;
							// change state
							pSdoComCon->
							    m_SdoComState =
							    kEplSdoComStateClientConnected;
							// save abort code
							pSdoComCon->
							    m_dwLastAbortCode =
							    AmiGetDwordFromLe
							    (&pAsySdoCom_p->
							     m_le_abCommandData
							     [0]);
							// call callback of application
							Ret =
							    EplSdoComTransferFinished
							    (SdoComCon_p,
							     pSdoComCon,
							     kEplSdoComTransferRxAborted);

							goto Exit;
						} else {	// normal frame received
							// check frame
							Ret =
							    EplSdoComClientProcessFrame
							    (SdoComCon_p,
							     pAsySdoCom_p);

							// check if transfer ready
							if (pSdoComCon->
							    m_uiTransSize ==
							    0) {
								// send acknowledge without any Command layer data
								Ret =
								    EplSdoAsySeqSendData
								    (pSdoComCon->
								     m_SdoSeqConHdl,
								     0,
								     (tEplFrame
								      *) NULL);
								// inc transaction id
								pSdoComCon->
								    m_bTransactionId++;
								// change state
								pSdoComCon->
								    m_SdoComState
								    =
								    kEplSdoComStateClientConnected;
								// call callback of application
								pSdoComCon->
								    m_dwLastAbortCode
								    = 0;
								Ret =
								    EplSdoComTransferFinished
								    (SdoComCon_p,
								     pSdoComCon,
								     kEplSdoComTransferFinished);

							}

						}
					}
					break;
				}

				// connection closed event go back to kEplSdoComStateClientWaitInit
			case kEplSdoComConEventConClosed:
				{	// connection closed by communication partner
					// close sequence layer handle
					Ret =
					    EplSdoAsySeqDelCon(pSdoComCon->
							       m_SdoSeqConHdl);
					// set handle to invalid and enter kEplSdoComStateClientWaitInit
					pSdoComCon->m_SdoSeqConHdl |=
					    EPL_SDO_SEQ_INVALID_HDL;
					// change state
					pSdoComCon->m_SdoComState =
					    kEplSdoComStateClientWaitInit;
					// inc transaction id
					pSdoComCon->m_bTransactionId++;
					// call callback of application
					pSdoComCon->m_dwLastAbortCode = 0;
					Ret =
					    EplSdoComTransferFinished
					    (SdoComCon_p, pSdoComCon,
					     kEplSdoComTransferFinished);

					break;
				}

				// abort to send from higher layer
			case kEplSdoComConEventAbort:
				{
					EplSdoComClientSendAbort(pSdoComCon,
								 *((DWORD *)
								   pSdoComCon->
								   m_pData));

					// inc transaction id
					pSdoComCon->m_bTransactionId++;
					// change state
					pSdoComCon->m_SdoComState =
					    kEplSdoComStateClientConnected;
					// call callback of application
					pSdoComCon->m_dwLastAbortCode =
					    *((DWORD *) pSdoComCon->m_pData);
					Ret =
					    EplSdoComTransferFinished
					    (SdoComCon_p, pSdoComCon,
					     kEplSdoComTransferTxAborted);

					break;
				}

			case kEplSdoComConEventInitError:
			case kEplSdoComConEventTimeout:
				{
					// close sequence layer handle
					Ret =
					    EplSdoAsySeqDelCon(pSdoComCon->
							       m_SdoSeqConHdl);
					pSdoComCon->m_SdoSeqConHdl |=
					    EPL_SDO_SEQ_INVALID_HDL;
					// change state
					pSdoComCon->m_SdoComState =
					    kEplSdoComStateClientWaitInit;
					// call callback of application
					pSdoComCon->m_dwLastAbortCode =
					    EPL_SDOAC_TIME_OUT;
					Ret =
					    EplSdoComTransferFinished
					    (SdoComCon_p, pSdoComCon,
					     kEplSdoComTransferLowerLayerAbort);

				}

			default:
				// d.k. do nothing
				break;

			}	// end of switch(SdoComConEvent_p)

			break;
		}
#endif // endo of #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)

	}			// end of switch(pSdoComCon->m_SdoComState)

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
      Exit:
#endif

#if defined(WIN32) || defined(_WIN32)
	// leave critical section for process function
	EPL_DBGLVL_SDO_TRACE0
	    ("\n\tLeaveCriticalSection EplSdoComProcessIntern\n\n");
	LeaveCriticalSection(SdoComInstance_g.m_pCriticalSection);

#endif

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:        EplSdoComServerInitReadByIndex
//
// Description:    function start the processing of an read by index command
//
//
//
// Parameters:      pSdoComCon_p     = pointer to control structure of connection
//                  pAsySdoCom_p     = pointer to received frame
//
// Returns:         tEplKernel  =  errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)
static tEplKernel EplSdoComServerInitReadByIndex(tEplSdoComCon * pSdoComCon_p,
						 tEplAsySdoCom * pAsySdoCom_p)
{
	tEplKernel Ret;
	unsigned int uiIndex;
	unsigned int uiSubindex;
	tEplObdSize EntrySize;
	tEplObdAccess AccessType;
	DWORD dwAbortCode;

	dwAbortCode = 0;

	// a init of a read could not be a segmented transfer
	// -> no variable part of header

	// get index and subindex
	uiIndex = AmiGetWordFromLe(&pAsySdoCom_p->m_le_abCommandData[0]);
	uiSubindex = AmiGetByteFromLe(&pAsySdoCom_p->m_le_abCommandData[2]);

	// check accesstype of entry
	// existens of entry
//#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)
	Ret = EplObduGetAccessType(uiIndex, uiSubindex, &AccessType);
/*#else
    Ret = kEplObdSubindexNotExist;
    AccessType = 0;
#endif*/
	if (Ret == kEplObdSubindexNotExist) {	// subentry doesn't exist
		dwAbortCode = EPL_SDOAC_SUB_INDEX_NOT_EXIST;
		// send abort
		pSdoComCon_p->m_pData = (BYTE *) & dwAbortCode;
		Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
						     uiIndex,
						     uiSubindex,
						     kEplSdoComSendTypeAbort);
		goto Exit;
	} else if (Ret != kEplSuccessful) {	// entry doesn't exist
		dwAbortCode = EPL_SDOAC_OBJECT_NOT_EXIST;
		// send abort
		pSdoComCon_p->m_pData = (BYTE *) & dwAbortCode;
		Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
						     uiIndex,
						     uiSubindex,
						     kEplSdoComSendTypeAbort);
		goto Exit;
	}
	// compare accesstype must be read or const
	if (((AccessType & kEplObdAccRead) == 0)
	    && ((AccessType & kEplObdAccConst) == 0)) {

		if ((AccessType & kEplObdAccWrite) != 0) {
			// entry read a write only object
			dwAbortCode = EPL_SDOAC_READ_TO_WRITE_ONLY_OBJ;
		} else {
			dwAbortCode = EPL_SDOAC_UNSUPPORTED_ACCESS;
		}
		// send abort
		pSdoComCon_p->m_pData = (BYTE *) & dwAbortCode;
		Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
						     uiIndex,
						     uiSubindex,
						     kEplSdoComSendTypeAbort);
		goto Exit;
	}
	// save service
	pSdoComCon_p->m_SdoServiceType = kEplSdoServiceReadByIndex;

	// get size of object to see iof segmented or expedited transfer
//#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)
	EntrySize = EplObduGetDataSize(uiIndex, uiSubindex);
/*#else
    EntrySize = 0;
#endif*/
	if (EntrySize > EPL_SDO_MAX_PAYLOAD) {	// segmented transfer
		pSdoComCon_p->m_SdoTransType = kEplSdoTransSegmented;
		// get pointer to object-entry data
//#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)
		pSdoComCon_p->m_pData =
		    EplObduGetObjectDataPtr(uiIndex, uiSubindex);
//#endif
	} else {		// expedited transfer
		pSdoComCon_p->m_SdoTransType = kEplSdoTransExpedited;
	}

	pSdoComCon_p->m_uiTransSize = EntrySize;
	pSdoComCon_p->m_uiTransferredByte = 0;

	Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
					     uiIndex,
					     uiSubindex, kEplSdoComSendTypeRes);
	if (Ret != kEplSuccessful) {
		// error -> abort
		dwAbortCode = EPL_SDOAC_GENERAL_ERROR;
		// send abort
		pSdoComCon_p->m_pData = (BYTE *) & dwAbortCode;
		Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
						     uiIndex,
						     uiSubindex,
						     kEplSdoComSendTypeAbort);
		goto Exit;
	}

      Exit:
	return Ret;
}
#endif

//---------------------------------------------------------------------------
//
// Function:        EplSdoComServerSendFrameIntern();
//
// Description:    function creats and send a frame for server
//
//
//
// Parameters:      pSdoComCon_p     = pointer to control structure of connection
//                  uiIndex_p        = index to send if expedited transfer else 0
//                  uiSubIndex_p     = subindex to send if expedited transfer else 0
//                  SendType_p       = to of frame to send
//
// Returns:         tEplKernel  =  errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)
static tEplKernel EplSdoComServerSendFrameIntern(tEplSdoComCon * pSdoComCon_p,
						 unsigned int uiIndex_p,
						 unsigned int uiSubIndex_p,
						 tEplSdoComSendType SendType_p)
{
	tEplKernel Ret;
	BYTE abFrame[EPL_MAX_SDO_FRAME_SIZE];
	tEplFrame *pFrame;
	tEplAsySdoCom *pCommandFrame;
	unsigned int uiSizeOfFrame;
	BYTE bFlag;

	Ret = kEplSuccessful;

	pFrame = (tEplFrame *) & abFrame[0];

	EPL_MEMSET(&abFrame[0], 0x00, sizeof(abFrame));

	// build generic part of frame
	// get pointer to command layerpart of frame
	pCommandFrame =
	    &pFrame->m_Data.m_Asnd.m_Payload.m_SdoSequenceFrame.
	    m_le_abSdoSeqPayload;
	AmiSetByteToLe(&pCommandFrame->m_le_bCommandId,
		       pSdoComCon_p->m_SdoServiceType);
	AmiSetByteToLe(&pCommandFrame->m_le_bTransactionId,
		       pSdoComCon_p->m_bTransactionId);

	// set size to header size
	uiSizeOfFrame = 8;

	// check SendType
	switch (SendType_p) {
		// requestframe to send
	case kEplSdoComSendTypeReq:
		{
			// nothing to do for server
			//-> error
			Ret = kEplSdoComInvalidSendType;
			break;
		}

		// response without data to send
	case kEplSdoComSendTypeAckRes:
		{
			// set response flag
			AmiSetByteToLe(&pCommandFrame->m_le_bFlags, 0x80);

			// send frame
			Ret = EplSdoAsySeqSendData(pSdoComCon_p->m_SdoSeqConHdl,
						   uiSizeOfFrame, pFrame);

			break;
		}

		// responsframe to send
	case kEplSdoComSendTypeRes:
		{
			// set response flag
			bFlag = AmiGetByteFromLe(&pCommandFrame->m_le_bFlags);
			bFlag |= 0x80;
			AmiSetByteToLe(&pCommandFrame->m_le_bFlags, bFlag);

			// check type of resonse
			if (pSdoComCon_p->m_SdoTransType == kEplSdoTransExpedited) {	// Expedited transfer
				// copy data in frame
//#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)
				Ret = EplObduReadEntryToLe(uiIndex_p,
							   uiSubIndex_p,
							   &pCommandFrame->
							   m_le_abCommandData
							   [0],
							   (tEplObdSize *) &
							   pSdoComCon_p->
							   m_uiTransSize);
				if (Ret != kEplSuccessful) {
					goto Exit;
				}
//#endif

				// set size of frame
				AmiSetWordToLe(&pCommandFrame->
					       m_le_wSegmentSize,
					       (WORD) pSdoComCon_p->
					       m_uiTransSize);

				// correct byte-counter
				uiSizeOfFrame += pSdoComCon_p->m_uiTransSize;
				pSdoComCon_p->m_uiTransferredByte +=
				    pSdoComCon_p->m_uiTransSize;
				pSdoComCon_p->m_uiTransSize = 0;

				// send frame
				uiSizeOfFrame += pSdoComCon_p->m_uiTransSize;
				Ret =
				    EplSdoAsySeqSendData(pSdoComCon_p->
							 m_SdoSeqConHdl,
							 uiSizeOfFrame, pFrame);
			} else if (pSdoComCon_p->m_SdoTransType == kEplSdoTransSegmented) {	// segmented transfer
				// distinguish between init, segment and complete
				if (pSdoComCon_p->m_uiTransferredByte == 0) {	// init
					// set init flag
					bFlag =
					    AmiGetByteFromLe(&pCommandFrame->
							     m_le_bFlags);
					bFlag |= 0x10;
					AmiSetByteToLe(&pCommandFrame->
						       m_le_bFlags, bFlag);
					// init variable header
					AmiSetDwordToLe(&pCommandFrame->
							m_le_abCommandData[0],
							pSdoComCon_p->
							m_uiTransSize);
					// copy data in frame
					EPL_MEMCPY(&pCommandFrame->
						   m_le_abCommandData[4],
						   pSdoComCon_p->m_pData,
						   (EPL_SDO_MAX_PAYLOAD - 4));

					// correct byte-counter
					pSdoComCon_p->m_uiTransSize -=
					    (EPL_SDO_MAX_PAYLOAD - 4);
					pSdoComCon_p->m_uiTransferredByte +=
					    (EPL_SDO_MAX_PAYLOAD - 4);
					// move data pointer
					pSdoComCon_p->m_pData +=
					    (EPL_SDO_MAX_PAYLOAD - 4);

					// set segment size
					AmiSetWordToLe(&pCommandFrame->
						       m_le_wSegmentSize,
						       (EPL_SDO_MAX_PAYLOAD -
							4));

					// send frame
					uiSizeOfFrame += EPL_SDO_MAX_PAYLOAD;
					Ret =
					    EplSdoAsySeqSendData(pSdoComCon_p->
								 m_SdoSeqConHdl,
								 uiSizeOfFrame,
								 pFrame);

				} else
				    if ((pSdoComCon_p->m_uiTransferredByte > 0)
					&& (pSdoComCon_p->m_uiTransSize > EPL_SDO_MAX_PAYLOAD)) {	// segment
					// set segment flag
					bFlag =
					    AmiGetByteFromLe(&pCommandFrame->
							     m_le_bFlags);
					bFlag |= 0x20;
					AmiSetByteToLe(&pCommandFrame->
						       m_le_bFlags, bFlag);

					// copy data in frame
					EPL_MEMCPY(&pCommandFrame->
						   m_le_abCommandData[0],
						   pSdoComCon_p->m_pData,
						   EPL_SDO_MAX_PAYLOAD);

					// correct byte-counter
					pSdoComCon_p->m_uiTransSize -=
					    EPL_SDO_MAX_PAYLOAD;
					pSdoComCon_p->m_uiTransferredByte +=
					    EPL_SDO_MAX_PAYLOAD;
					// move data pointer
					pSdoComCon_p->m_pData +=
					    EPL_SDO_MAX_PAYLOAD;

					// set segment size
					AmiSetWordToLe(&pCommandFrame->
						       m_le_wSegmentSize,
						       EPL_SDO_MAX_PAYLOAD);

					// send frame
					uiSizeOfFrame += EPL_SDO_MAX_PAYLOAD;
					Ret =
					    EplSdoAsySeqSendData(pSdoComCon_p->
								 m_SdoSeqConHdl,
								 uiSizeOfFrame,
								 pFrame);
				} else {
					if ((pSdoComCon_p->m_uiTransSize == 0)
					    && (pSdoComCon_p->
						m_SdoServiceType !=
						kEplSdoServiceWriteByIndex)) {
						goto Exit;
					}
					// complete
					// set segment complete flag
					bFlag =
					    AmiGetByteFromLe(&pCommandFrame->
							     m_le_bFlags);
					bFlag |= 0x30;
					AmiSetByteToLe(&pCommandFrame->
						       m_le_bFlags, bFlag);

					// copy data in frame
					EPL_MEMCPY(&pCommandFrame->
						   m_le_abCommandData[0],
						   pSdoComCon_p->m_pData,
						   pSdoComCon_p->m_uiTransSize);

					// correct byte-counter
					pSdoComCon_p->m_uiTransferredByte +=
					    pSdoComCon_p->m_uiTransSize;

					// move data pointer
					pSdoComCon_p->m_pData +=
					    pSdoComCon_p->m_uiTransSize;

					// set segment size
					AmiSetWordToLe(&pCommandFrame->
						       m_le_wSegmentSize,
						       (WORD) pSdoComCon_p->
						       m_uiTransSize);

					// send frame
					uiSizeOfFrame +=
					    pSdoComCon_p->m_uiTransSize;
					pSdoComCon_p->m_uiTransSize = 0;
					Ret =
					    EplSdoAsySeqSendData(pSdoComCon_p->
								 m_SdoSeqConHdl,
								 uiSizeOfFrame,
								 pFrame);
				}

			}
			break;
		}
		// abort to send
	case kEplSdoComSendTypeAbort:
		{
			// set response and abort flag
			bFlag = AmiGetByteFromLe(&pCommandFrame->m_le_bFlags);
			bFlag |= 0xC0;
			AmiSetByteToLe(&pCommandFrame->m_le_bFlags, bFlag);

			// copy abortcode to frame
			AmiSetDwordToLe(&pCommandFrame->m_le_abCommandData[0],
					*((DWORD *) pSdoComCon_p->m_pData));

			// set size of segment
			AmiSetWordToLe(&pCommandFrame->m_le_wSegmentSize,
				       sizeof(DWORD));

			// update counter
			pSdoComCon_p->m_uiTransferredByte = sizeof(DWORD);
			pSdoComCon_p->m_uiTransSize = 0;

			// calc framesize
			uiSizeOfFrame += sizeof(DWORD);
			Ret = EplSdoAsySeqSendData(pSdoComCon_p->m_SdoSeqConHdl,
						   uiSizeOfFrame, pFrame);
			break;
		}
	}			// end of switch(SendType_p)

      Exit:
	return Ret;
}
#endif
//---------------------------------------------------------------------------
//
// Function:        EplSdoComServerInitWriteByIndex
//
// Description:    function start the processing of an write by index command
//
//
//
// Parameters:      pSdoComCon_p     = pointer to control structure of connection
//                  pAsySdoCom_p     = pointer to received frame
//
// Returns:         tEplKernel  =  errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOS)) != 0)
static tEplKernel EplSdoComServerInitWriteByIndex(tEplSdoComCon * pSdoComCon_p,
						  tEplAsySdoCom * pAsySdoCom_p)
{
	tEplKernel Ret = kEplSuccessful;
	unsigned int uiIndex;
	unsigned int uiSubindex;
	unsigned int uiBytesToTransfer;
	tEplObdSize EntrySize;
	tEplObdAccess AccessType;
	DWORD dwAbortCode;
	BYTE *pbSrcData;

	dwAbortCode = 0;

	// a init of a write
	// -> variable part of header possible

	// check if expedited or segmented transfer
	if ((pAsySdoCom_p->m_le_bFlags & 0x30) == 0x10) {	// initiate segmented transfer
		pSdoComCon_p->m_SdoTransType = kEplSdoTransSegmented;
		// get index and subindex
		uiIndex =
		    AmiGetWordFromLe(&pAsySdoCom_p->m_le_abCommandData[4]);
		uiSubindex =
		    AmiGetByteFromLe(&pAsySdoCom_p->m_le_abCommandData[6]);
		// get source-pointer for copy
		pbSrcData = &pAsySdoCom_p->m_le_abCommandData[8];
		// save size
		pSdoComCon_p->m_uiTransSize =
		    AmiGetDwordFromLe(&pAsySdoCom_p->m_le_abCommandData[0]);

	} else if ((pAsySdoCom_p->m_le_bFlags & 0x30) == 0x00) {	// expedited transfer
		pSdoComCon_p->m_SdoTransType = kEplSdoTransExpedited;
		// get index and subindex
		uiIndex =
		    AmiGetWordFromLe(&pAsySdoCom_p->m_le_abCommandData[0]);
		uiSubindex =
		    AmiGetByteFromLe(&pAsySdoCom_p->m_le_abCommandData[2]);
		// get source-pointer for copy
		pbSrcData = &pAsySdoCom_p->m_le_abCommandData[4];
		// save size
		pSdoComCon_p->m_uiTransSize =
		    AmiGetWordFromLe(&pAsySdoCom_p->m_le_wSegmentSize);
		// subtract header
		pSdoComCon_p->m_uiTransSize -= 4;

	} else {
		// just ignore any other transfer type
		goto Exit;
	}

	// check accesstype of entry
	// existens of entry
//#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)
	Ret = EplObduGetAccessType(uiIndex, uiSubindex, &AccessType);
/*#else
    Ret = kEplObdSubindexNotExist;
    AccessType = 0;
#endif*/
	if (Ret == kEplObdSubindexNotExist) {	// subentry doesn't exist
		pSdoComCon_p->m_dwLastAbortCode = EPL_SDOAC_SUB_INDEX_NOT_EXIST;
		// send abort
		// d.k. This is wrong: k.t. not needed send abort on end of write
		/*pSdoComCon_p->m_pData = (BYTE*)pSdoComCon_p->m_dwLastAbortCode;
		   Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
		   uiIndex,
		   uiSubindex,
		   kEplSdoComSendTypeAbort); */
		goto Abort;
	} else if (Ret != kEplSuccessful) {	// entry doesn't exist
		pSdoComCon_p->m_dwLastAbortCode = EPL_SDOAC_OBJECT_NOT_EXIST;
		// send abort
		// d.k. This is wrong: k.t. not needed send abort on end of write
		/*
		   pSdoComCon_p->m_pData = (BYTE*)&dwAbortCode;
		   Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
		   uiIndex,
		   uiSubindex,
		   kEplSdoComSendTypeAbort); */
		goto Abort;
	}
	// compare accesstype must be read
	if ((AccessType & kEplObdAccWrite) == 0) {

		if ((AccessType & kEplObdAccRead) != 0) {
			// entry write a read only object
			pSdoComCon_p->m_dwLastAbortCode =
			    EPL_SDOAC_WRITE_TO_READ_ONLY_OBJ;
		} else {
			pSdoComCon_p->m_dwLastAbortCode =
			    EPL_SDOAC_UNSUPPORTED_ACCESS;
		}
		// send abort
		// d.k. This is wrong: k.t. not needed send abort on end of write
		/*pSdoComCon_p->m_pData = (BYTE*)&dwAbortCode;
		   Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
		   uiIndex,
		   uiSubindex,
		   kEplSdoComSendTypeAbort); */
		goto Abort;
	}
	// save service
	pSdoComCon_p->m_SdoServiceType = kEplSdoServiceWriteByIndex;

	pSdoComCon_p->m_uiTransferredByte = 0;

	// write data to OD
	if (pSdoComCon_p->m_SdoTransType == kEplSdoTransExpedited) {	// expedited transfer
		// size checking is done by EplObduWriteEntryFromLe()

//#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)
		Ret = EplObduWriteEntryFromLe(uiIndex,
					      uiSubindex,
					      pbSrcData,
					      pSdoComCon_p->m_uiTransSize);
		switch (Ret) {
		case kEplSuccessful:
			{
				break;
			}

		case kEplObdAccessViolation:
			{
				pSdoComCon_p->m_dwLastAbortCode =
				    EPL_SDOAC_UNSUPPORTED_ACCESS;
				// send abort
				goto Abort;
			}

		case kEplObdValueLengthError:
			{
				pSdoComCon_p->m_dwLastAbortCode =
				    EPL_SDOAC_DATA_TYPE_LENGTH_NOT_MATCH;
				// send abort
				goto Abort;
			}

		case kEplObdValueTooHigh:
			{
				pSdoComCon_p->m_dwLastAbortCode =
				    EPL_SDOAC_VALUE_RANGE_TOO_HIGH;
				// send abort
				goto Abort;
			}

		case kEplObdValueTooLow:
			{
				pSdoComCon_p->m_dwLastAbortCode =
				    EPL_SDOAC_VALUE_RANGE_TOO_LOW;
				// send abort
				goto Abort;
			}

		default:
			{
				pSdoComCon_p->m_dwLastAbortCode =
				    EPL_SDOAC_GENERAL_ERROR;
				// send abort
				goto Abort;
			}
		}
//#endif
		// send command acknowledge
		Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
						     0,
						     0,
						     kEplSdoComSendTypeAckRes);

		pSdoComCon_p->m_uiTransSize = 0;
		goto Exit;
	} else {
		// get size of the object to check if it fits
		// because we directly write to the destination memory
		// d.k. no one calls the user OD callback function

		//#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)
		EntrySize = EplObduGetDataSize(uiIndex, uiSubindex);
		/*#else
		   EntrySize = 0;
		   #endif */
		if (EntrySize < pSdoComCon_p->m_uiTransSize) {	// parameter too big
			pSdoComCon_p->m_dwLastAbortCode =
			    EPL_SDOAC_DATA_TYPE_LENGTH_TOO_HIGH;
			// send abort
			// d.k. This is wrong: k.t. not needed send abort on end of write
			/*pSdoComCon_p->m_pData = (BYTE*)&dwAbortCode;
			   Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
			   uiIndex,
			   uiSubindex,
			   kEplSdoComSendTypeAbort); */
			goto Abort;
		}

		uiBytesToTransfer =
		    AmiGetWordFromLe(&pAsySdoCom_p->m_le_wSegmentSize);
		// eleminate header (Command header (8) + variable part (4) + Command header (4))
		uiBytesToTransfer -= 16;
		// get pointer to object entry
//#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0)
		pSdoComCon_p->m_pData = EplObduGetObjectDataPtr(uiIndex,
								uiSubindex);
//#endif
		if (pSdoComCon_p->m_pData == NULL) {
			pSdoComCon_p->m_dwLastAbortCode =
			    EPL_SDOAC_GENERAL_ERROR;
			// send abort
			// d.k. This is wrong: k.t. not needed send abort on end of write
/*            pSdoComCon_p->m_pData = (BYTE*)&pSdoComCon_p->m_dwLastAbortCode;
            Ret = EplSdoComServerSendFrameIntern(pSdoComCon_p,
                                        uiIndex,
                                        uiSubindex,
                                        kEplSdoComSendTypeAbort);*/
			goto Abort;
		}
		// copy data
		EPL_MEMCPY(pSdoComCon_p->m_pData, pbSrcData, uiBytesToTransfer);

		// update internal counter
		pSdoComCon_p->m_uiTransferredByte = uiBytesToTransfer;
		pSdoComCon_p->m_uiTransSize -= uiBytesToTransfer;

		// update target pointer
		( /*(BYTE*) */ pSdoComCon_p->m_pData) += uiBytesToTransfer;

		// send acknowledge without any Command layer data
		Ret = EplSdoAsySeqSendData(pSdoComCon_p->m_SdoSeqConHdl,
					   0, (tEplFrame *) NULL);
		goto Exit;
	}

      Abort:
	if (pSdoComCon_p->m_dwLastAbortCode != 0) {
		// send abort
		pSdoComCon_p->m_pData =
		    (BYTE *) & pSdoComCon_p->m_dwLastAbortCode;
		Ret =
		    EplSdoComServerSendFrameIntern(pSdoComCon_p, uiIndex,
						   uiSubindex,
						   kEplSdoComSendTypeAbort);

		// reset abort code
		pSdoComCon_p->m_dwLastAbortCode = 0;
		pSdoComCon_p->m_uiTransSize = 0;
		goto Exit;
	}

      Exit:
	return Ret;
}
#endif

//---------------------------------------------------------------------------
//
// Function:        EplSdoComClientSend
//
// Description:    function starts an sdo transfer an send all further frames
//
//
//
// Parameters:      pSdoComCon_p     = pointer to control structure of connection
//
// Returns:         tEplKernel  =  errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
static tEplKernel EplSdoComClientSend(tEplSdoComCon * pSdoComCon_p)
{
	tEplKernel Ret;
	BYTE abFrame[EPL_MAX_SDO_FRAME_SIZE];
	tEplFrame *pFrame;
	tEplAsySdoCom *pCommandFrame;
	unsigned int uiSizeOfFrame;
	BYTE bFlags;
	BYTE *pbPayload;

	Ret = kEplSuccessful;

	pFrame = (tEplFrame *) & abFrame[0];

	EPL_MEMSET(&abFrame[0], 0x00, sizeof(abFrame));

	// build generic part of frame
	// get pointer to command layerpart of frame
	pCommandFrame =
	    &pFrame->m_Data.m_Asnd.m_Payload.m_SdoSequenceFrame.
	    m_le_abSdoSeqPayload;
	AmiSetByteToLe(&pCommandFrame->m_le_bCommandId,
		       pSdoComCon_p->m_SdoServiceType);
	AmiSetByteToLe(&pCommandFrame->m_le_bTransactionId,
		       pSdoComCon_p->m_bTransactionId);

	// set size constant part of header
	uiSizeOfFrame = 8;

	// check if first frame to send -> command header needed
	if (pSdoComCon_p->m_uiTransSize > 0) {
		if (pSdoComCon_p->m_uiTransferredByte == 0) {	// start SDO transfer
			// check if segmented or expedited transfer
			// only for write commands
			switch (pSdoComCon_p->m_SdoServiceType) {
			case kEplSdoServiceReadByIndex:
				{	// first frame of read access always expedited
					pSdoComCon_p->m_SdoTransType =
					    kEplSdoTransExpedited;
					pbPayload =
					    &pCommandFrame->
					    m_le_abCommandData[0];
					// fill rest of header
					AmiSetWordToLe(&pCommandFrame->
						       m_le_wSegmentSize, 4);

					// create command header
					AmiSetWordToLe(pbPayload,
						       (WORD) pSdoComCon_p->
						       m_uiTargetIndex);
					pbPayload += 2;
					AmiSetByteToLe(pbPayload,
						       (BYTE) pSdoComCon_p->
						       m_uiTargetSubIndex);
					// calc size
					uiSizeOfFrame += 4;

					// set pSdoComCon_p->m_uiTransferredByte to one
					pSdoComCon_p->m_uiTransferredByte = 1;
					break;
				}

			case kEplSdoServiceWriteByIndex:
				{
					if (pSdoComCon_p->m_uiTransSize > EPL_SDO_MAX_PAYLOAD) {	// segmented transfer
						// -> variable part of header needed
						// save that transfer is segmented
						pSdoComCon_p->m_SdoTransType =
						    kEplSdoTransSegmented;
						// fill variable part of header
						AmiSetDwordToLe(&pCommandFrame->
								m_le_abCommandData
								[0],
								pSdoComCon_p->
								m_uiTransSize);
						// set pointer to real payload
						pbPayload =
						    &pCommandFrame->
						    m_le_abCommandData[4];
						// fill rest of header
						AmiSetWordToLe(&pCommandFrame->
							       m_le_wSegmentSize,
							       EPL_SDO_MAX_PAYLOAD);
						bFlags = 0x10;
						AmiSetByteToLe(&pCommandFrame->
							       m_le_bFlags,
							       bFlags);
						// create command header
						AmiSetWordToLe(pbPayload,
							       (WORD)
							       pSdoComCon_p->
							       m_uiTargetIndex);
						pbPayload += 2;
						AmiSetByteToLe(pbPayload,
							       (BYTE)
							       pSdoComCon_p->
							       m_uiTargetSubIndex);
						// on byte for reserved
						pbPayload += 2;
						// calc size
						uiSizeOfFrame +=
						    EPL_SDO_MAX_PAYLOAD;

						// copy payload
						EPL_MEMCPY(pbPayload,
							   pSdoComCon_p->
							   m_pData,
							   (EPL_SDO_MAX_PAYLOAD
							    - 8));
						pSdoComCon_p->m_pData +=
						    (EPL_SDO_MAX_PAYLOAD - 8);
						// correct intern counter
						pSdoComCon_p->m_uiTransSize -=
						    (EPL_SDO_MAX_PAYLOAD - 8);
						pSdoComCon_p->
						    m_uiTransferredByte =
						    (EPL_SDO_MAX_PAYLOAD - 8);

					} else {	// expedited trandsfer
						// save that transfer is expedited
						pSdoComCon_p->m_SdoTransType =
						    kEplSdoTransExpedited;
						pbPayload =
						    &pCommandFrame->
						    m_le_abCommandData[0];

						// create command header
						AmiSetWordToLe(pbPayload,
							       (WORD)
							       pSdoComCon_p->
							       m_uiTargetIndex);
						pbPayload += 2;
						AmiSetByteToLe(pbPayload,
							       (BYTE)
							       pSdoComCon_p->
							       m_uiTargetSubIndex);
						// + 2 -> one byte for subindex and one byte reserved
						pbPayload += 2;
						// copy data
						EPL_MEMCPY(pbPayload,
							   pSdoComCon_p->
							   m_pData,
							   pSdoComCon_p->
							   m_uiTransSize);
						// calc size
						uiSizeOfFrame +=
						    (4 +
						     pSdoComCon_p->
						     m_uiTransSize);
						// fill rest of header
						AmiSetWordToLe(&pCommandFrame->
							       m_le_wSegmentSize,
							       (WORD) (4 +
								       pSdoComCon_p->
								       m_uiTransSize));

						pSdoComCon_p->
						    m_uiTransferredByte =
						    pSdoComCon_p->m_uiTransSize;
						pSdoComCon_p->m_uiTransSize = 0;
					}
					break;
				}

			case kEplSdoServiceNIL:
			default:
				// invalid service requested
				Ret = kEplSdoComInvalidServiceType;
				goto Exit;
			}	// end of switch(pSdoComCon_p->m_SdoServiceType)
		} else		// (pSdoComCon_p->m_uiTransferredByte > 0)
		{		// continue SDO transfer
			switch (pSdoComCon_p->m_SdoServiceType) {
				// for expedited read is nothing to do
				// -> server sends data

			case kEplSdoServiceWriteByIndex:
				{	// send next frame
					if (pSdoComCon_p->m_SdoTransType ==
					    kEplSdoTransSegmented) {
						if (pSdoComCon_p->m_uiTransSize > EPL_SDO_MAX_PAYLOAD) {	// next segment
							pbPayload =
							    &pCommandFrame->
							    m_le_abCommandData
							    [0];
							// fill rest of header
							AmiSetWordToLe
							    (&pCommandFrame->
							     m_le_wSegmentSize,
							     EPL_SDO_MAX_PAYLOAD);
							bFlags = 0x20;
							AmiSetByteToLe
							    (&pCommandFrame->
							     m_le_bFlags,
							     bFlags);
							// copy data
							EPL_MEMCPY(pbPayload,
								   pSdoComCon_p->
								   m_pData,
								   EPL_SDO_MAX_PAYLOAD);
							pSdoComCon_p->m_pData +=
							    EPL_SDO_MAX_PAYLOAD;
							// correct intern counter
							pSdoComCon_p->
							    m_uiTransSize -=
							    EPL_SDO_MAX_PAYLOAD;
							pSdoComCon_p->
							    m_uiTransferredByte
							    =
							    EPL_SDO_MAX_PAYLOAD;
							// calc size
							uiSizeOfFrame +=
							    EPL_SDO_MAX_PAYLOAD;

						} else {	// end of transfer
							pbPayload =
							    &pCommandFrame->
							    m_le_abCommandData
							    [0];
							// fill rest of header
							AmiSetWordToLe
							    (&pCommandFrame->
							     m_le_wSegmentSize,
							     (WORD)
							     pSdoComCon_p->
							     m_uiTransSize);
							bFlags = 0x30;
							AmiSetByteToLe
							    (&pCommandFrame->
							     m_le_bFlags,
							     bFlags);
							// copy data
							EPL_MEMCPY(pbPayload,
								   pSdoComCon_p->
								   m_pData,
								   pSdoComCon_p->
								   m_uiTransSize);
							pSdoComCon_p->m_pData +=
							    pSdoComCon_p->
							    m_uiTransSize;
							// calc size
							uiSizeOfFrame +=
							    pSdoComCon_p->
							    m_uiTransSize;
							// correct intern counter
							pSdoComCon_p->
							    m_uiTransSize = 0;
							pSdoComCon_p->
							    m_uiTransferredByte
							    =
							    pSdoComCon_p->
							    m_uiTransSize;

						}
					} else {
						goto Exit;
					}
					break;
				}
			default:
				{
					goto Exit;
				}
			}	// end of switch(pSdoComCon_p->m_SdoServiceType)
		}
	} else {
		goto Exit;
	}

	// call send function of lower layer
	switch (pSdoComCon_p->m_SdoProtType) {
	case kEplSdoTypeAsnd:
	case kEplSdoTypeUdp:
		{
			Ret = EplSdoAsySeqSendData(pSdoComCon_p->m_SdoSeqConHdl,
						   uiSizeOfFrame, pFrame);
			break;
		}

	default:
		{
			Ret = kEplSdoComUnsupportedProt;
		}
	}			// end of switch(pSdoComCon_p->m_SdoProtType)

      Exit:
	return Ret;

}
#endif
//---------------------------------------------------------------------------
//
// Function:        EplSdoComClientProcessFrame
//
// Description:    function process a received frame
//
//
//
// Parameters:      SdoComCon_p      = connection handle
//                  pAsySdoCom_p     = pointer to frame to process
//
// Returns:         tEplKernel  =  errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
static tEplKernel EplSdoComClientProcessFrame(tEplSdoComConHdl SdoComCon_p,
					      tEplAsySdoCom * pAsySdoCom_p)
{
	tEplKernel Ret;
	BYTE bBuffer;
	unsigned int uiBuffer;
	unsigned int uiDataSize;
	unsigned long ulBuffer;
	tEplSdoComCon *pSdoComCon;

	Ret = kEplSuccessful;

	// get pointer to control structure
	pSdoComCon = &SdoComInstance_g.m_SdoComCon[SdoComCon_p];

	// check if transaction Id fit
	bBuffer = AmiGetByteFromLe(&pAsySdoCom_p->m_le_bTransactionId);
	if (pSdoComCon->m_bTransactionId != bBuffer) {
		// incorrect transaction id

		// if running transfer
		if ((pSdoComCon->m_uiTransferredByte != 0)
		    && (pSdoComCon->m_uiTransSize != 0)) {
			pSdoComCon->m_dwLastAbortCode = EPL_SDOAC_GENERAL_ERROR;
			// -> send abort
			EplSdoComClientSendAbort(pSdoComCon,
						 pSdoComCon->m_dwLastAbortCode);
			// call callback of application
			Ret =
			    EplSdoComTransferFinished(SdoComCon_p, pSdoComCon,
						      kEplSdoComTransferTxAborted);
		}

	} else {		// check if correct command
		bBuffer = AmiGetByteFromLe(&pAsySdoCom_p->m_le_bCommandId);
		if (pSdoComCon->m_SdoServiceType != bBuffer) {
			// incorrect command
			// if running transfer
			if ((pSdoComCon->m_uiTransferredByte != 0)
			    && (pSdoComCon->m_uiTransSize != 0)) {
				pSdoComCon->m_dwLastAbortCode =
				    EPL_SDOAC_GENERAL_ERROR;
				// -> send abort
				EplSdoComClientSendAbort(pSdoComCon,
							 pSdoComCon->
							 m_dwLastAbortCode);
				// call callback of application
				Ret =
				    EplSdoComTransferFinished(SdoComCon_p,
							      pSdoComCon,
							      kEplSdoComTransferTxAborted);
			}

		} else {	// switch on command
			switch (pSdoComCon->m_SdoServiceType) {
			case kEplSdoServiceWriteByIndex:
				{	// check if confirmation from server
					// nothing more to do
					break;
				}

			case kEplSdoServiceReadByIndex:
				{	// check if it is an segmented or an expedited transfer
					bBuffer =
					    AmiGetByteFromLe(&pAsySdoCom_p->
							     m_le_bFlags);
					// mask uninteressting bits
					bBuffer &= 0x30;
					switch (bBuffer) {
						// expedited transfer
					case 0x00:
						{
							// check size of buffer
							uiBuffer =
							    AmiGetWordFromLe
							    (&pAsySdoCom_p->
							     m_le_wSegmentSize);
							if (uiBuffer > pSdoComCon->m_uiTransSize) {	// buffer provided by the application is to small
								// copy only a part
								uiDataSize =
								    pSdoComCon->
								    m_uiTransSize;
							} else {	// buffer fits
								uiDataSize =
								    uiBuffer;
							}

							// copy data
							EPL_MEMCPY(pSdoComCon->
								   m_pData,
								   &pAsySdoCom_p->
								   m_le_abCommandData
								   [0],
								   uiDataSize);

							// correct counter
							pSdoComCon->
							    m_uiTransSize = 0;
							pSdoComCon->
							    m_uiTransferredByte
							    = uiDataSize;
							break;
						}

						// start of a segmented transfer
					case 0x10:
						{	// get total size of transfer
							ulBuffer =
							    AmiGetDwordFromLe
							    (&pAsySdoCom_p->
							     m_le_abCommandData
							     [0]);
							if (ulBuffer <= pSdoComCon->m_uiTransSize) {	// buffer fit
								pSdoComCon->
								    m_uiTransSize
								    =
								    (unsigned
								     int)
								    ulBuffer;
							} else {	// buffer to small
								// send abort
								pSdoComCon->
								    m_dwLastAbortCode
								    =
								    EPL_SDOAC_DATA_TYPE_LENGTH_TOO_HIGH;
								// -> send abort
								EplSdoComClientSendAbort
								    (pSdoComCon,
								     pSdoComCon->
								     m_dwLastAbortCode);
								// call callback of application
								Ret =
								    EplSdoComTransferFinished
								    (SdoComCon_p,
								     pSdoComCon,
								     kEplSdoComTransferRxAborted);
								goto Exit;
							}

							// get segment size
							// check size of buffer
							uiBuffer =
							    AmiGetWordFromLe
							    (&pAsySdoCom_p->
							     m_le_wSegmentSize);
							// subtract size of vaiable header from datasize
							uiBuffer -= 4;
							// copy data
							EPL_MEMCPY(pSdoComCon->
								   m_pData,
								   &pAsySdoCom_p->
								   m_le_abCommandData
								   [4],
								   uiBuffer);

							// correct counter an pointer
							pSdoComCon->m_pData +=
							    uiBuffer;
							pSdoComCon->
							    m_uiTransferredByte
							    += uiBuffer;
							pSdoComCon->
							    m_uiTransSize -=
							    uiBuffer;

							break;
						}

						// segment
					case 0x20:
						{
							// get segment size
							// check size of buffer
							uiBuffer =
							    AmiGetWordFromLe
							    (&pAsySdoCom_p->
							     m_le_wSegmentSize);
							// check if data to copy fit to buffer
							if (uiBuffer >= pSdoComCon->m_uiTransSize) {	// to much data
								uiBuffer =
								    (pSdoComCon->
								     m_uiTransSize
								     - 1);
							}
							// copy data
							EPL_MEMCPY(pSdoComCon->
								   m_pData,
								   &pAsySdoCom_p->
								   m_le_abCommandData
								   [0],
								   uiBuffer);

							// correct counter an pointer
							pSdoComCon->m_pData +=
							    uiBuffer;
							pSdoComCon->
							    m_uiTransferredByte
							    += uiBuffer;
							pSdoComCon->
							    m_uiTransSize -=
							    uiBuffer;
							break;
						}

						// last segment
					case 0x30:
						{
							// get segment size
							// check size of buffer
							uiBuffer =
							    AmiGetWordFromLe
							    (&pAsySdoCom_p->
							     m_le_wSegmentSize);
							// check if data to copy fit to buffer
							if (uiBuffer > pSdoComCon->m_uiTransSize) {	// to much data
								uiBuffer =
								    (pSdoComCon->
								     m_uiTransSize
								     - 1);
							}
							// copy data
							EPL_MEMCPY(pSdoComCon->
								   m_pData,
								   &pAsySdoCom_p->
								   m_le_abCommandData
								   [0],
								   uiBuffer);

							// correct counter an pointer
							pSdoComCon->m_pData +=
							    uiBuffer;
							pSdoComCon->
							    m_uiTransferredByte
							    += uiBuffer;
							pSdoComCon->
							    m_uiTransSize = 0;

							break;
						}
					}	// end of switch(bBuffer & 0x30)

					break;
				}

			case kEplSdoServiceNIL:
			default:
				// invalid service requested
				// $$$ d.k. What should we do?
				break;
			}	// end of switch(pSdoComCon->m_SdoServiceType)
		}
	}

      Exit:
	return Ret;
}
#endif

//---------------------------------------------------------------------------
//
// Function:    EplSdoComClientSendAbort
//
// Description: function send a abort message
//
//
//
// Parameters:  pSdoComCon_p     = pointer to control structure of connection
//              dwAbortCode_p    = Sdo abort code
//
// Returns:     tEplKernel  =  errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDOC)) != 0)
static tEplKernel EplSdoComClientSendAbort(tEplSdoComCon * pSdoComCon_p,
					   DWORD dwAbortCode_p)
{
	tEplKernel Ret;
	BYTE abFrame[EPL_MAX_SDO_FRAME_SIZE];
	tEplFrame *pFrame;
	tEplAsySdoCom *pCommandFrame;
	unsigned int uiSizeOfFrame;

	Ret = kEplSuccessful;

	pFrame = (tEplFrame *) & abFrame[0];

	EPL_MEMSET(&abFrame[0], 0x00, sizeof(abFrame));

	// build generic part of frame
	// get pointer to command layerpart of frame
	pCommandFrame =
	    &pFrame->m_Data.m_Asnd.m_Payload.m_SdoSequenceFrame.
	    m_le_abSdoSeqPayload;
	AmiSetByteToLe(&pCommandFrame->m_le_bCommandId,
		       pSdoComCon_p->m_SdoServiceType);
	AmiSetByteToLe(&pCommandFrame->m_le_bTransactionId,
		       pSdoComCon_p->m_bTransactionId);

	uiSizeOfFrame = 8;

	// set response and abort flag
	pCommandFrame->m_le_bFlags |= 0x40;

	// copy abortcode to frame
	AmiSetDwordToLe(&pCommandFrame->m_le_abCommandData[0], dwAbortCode_p);

	// set size of segment
	AmiSetWordToLe(&pCommandFrame->m_le_wSegmentSize, sizeof(DWORD));

	// update counter
	pSdoComCon_p->m_uiTransferredByte = sizeof(DWORD);
	pSdoComCon_p->m_uiTransSize = 0;

	// calc framesize
	uiSizeOfFrame += sizeof(DWORD);

	// save abort code
	pSdoComCon_p->m_dwLastAbortCode = dwAbortCode_p;

	// call send function of lower layer
	switch (pSdoComCon_p->m_SdoProtType) {
	case kEplSdoTypeAsnd:
	case kEplSdoTypeUdp:
		{
			Ret = EplSdoAsySeqSendData(pSdoComCon_p->m_SdoSeqConHdl,
						   uiSizeOfFrame, pFrame);
			break;
		}

	default:
		{
			Ret = kEplSdoComUnsupportedProt;
		}
	}			// end of switch(pSdoComCon_p->m_SdoProtType)

	return Ret;
}
#endif

//---------------------------------------------------------------------------
//
// Function:    EplSdoComTransferFinished
//
// Description: calls callback function of application if available
//              and clears entry in control structure
//
// Parameters:  pSdoComCon_p     = pointer to control structure of connection
//              SdoComConState_p = state of SDO transfer
//
// Returns:     tEplKernel  =  errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoComTransferFinished(tEplSdoComConHdl SdoComCon_p,
					    tEplSdoComCon * pSdoComCon_p,
					    tEplSdoComConState SdoComConState_p)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	if (pSdoComCon_p->m_pfnTransferFinished != NULL) {
		tEplSdoFinishedCb pfnTransferFinished;
		tEplSdoComFinished SdoComFinished;

		SdoComFinished.m_pUserArg = pSdoComCon_p->m_pUserArg;
		SdoComFinished.m_uiNodeId = pSdoComCon_p->m_uiNodeId;
		SdoComFinished.m_uiTargetIndex = pSdoComCon_p->m_uiTargetIndex;
		SdoComFinished.m_uiTargetSubIndex =
		    pSdoComCon_p->m_uiTargetSubIndex;
		SdoComFinished.m_uiTransferredByte =
		    pSdoComCon_p->m_uiTransferredByte;
		SdoComFinished.m_dwAbortCode = pSdoComCon_p->m_dwLastAbortCode;
		SdoComFinished.m_SdoComConHdl = SdoComCon_p;
		SdoComFinished.m_SdoComConState = SdoComConState_p;
		if (pSdoComCon_p->m_SdoServiceType ==
		    kEplSdoServiceWriteByIndex) {
			SdoComFinished.m_SdoAccessType = kEplSdoAccessTypeWrite;
		} else {
			SdoComFinished.m_SdoAccessType = kEplSdoAccessTypeRead;
		}

		// reset transfer state so this handle is not busy anymore
		pSdoComCon_p->m_uiTransferredByte = 0;
		pSdoComCon_p->m_uiTransSize = 0;

		pfnTransferFinished = pSdoComCon_p->m_pfnTransferFinished;
		// delete function pointer to inform application only once for each transfer
		pSdoComCon_p->m_pfnTransferFinished = NULL;

		// call application's callback function
		pfnTransferFinished(&SdoComFinished);

	}

	return Ret;
}

// EOF
