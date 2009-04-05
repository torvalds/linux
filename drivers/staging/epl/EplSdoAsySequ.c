/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for asychronous SDO Sequence Layer module

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

                $RCSfile: EplSdoAsySequ.c,v $

                $Author: D.Krueger $

                $Revision: 1.10 $  $Date: 2008/11/13 17:13:09 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/26 k.t.:   start of the implementation

****************************************************************************/

#include "user/EplSdoAsySequ.h"

#if ((((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) == 0) &&\
     (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_ASND)) == 0)   )

#error 'ERROR: At least UDP or Asnd module needed!'

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

#define EPL_SDO_HISTORY_SIZE        5

#ifndef EPL_MAX_SDO_SEQ_CON
#define EPL_MAX_SDO_SEQ_CON         10
#endif

#define EPL_SEQ_DEFAULT_TIMEOUT     5000	// in [ms] => 5 sec

#define EPL_SEQ_RETRY_COUNT         5	// => max. Timeout 30 sec

#define EPL_SEQ_NUM_THRESHOLD       100	// threshold which distinguishes between old and new sequence numbers

// define frame with size of Asnd-Header-, SDO Sequenze Header size, SDO Command header
// and Ethernet-Header size
#define EPL_SEQ_FRAME_SIZE          24
// size of the header of the asynchronus SDO Sequence layer
#define EPL_SEQ_HEADER_SIZE         4

// buffersize for one frame in history
#define EPL_SEQ_HISTROY_FRAME_SIZE  EPL_MAX_SDO_FRAME_SIZE

// mask to get scon and rcon
#define EPL_ASY_SDO_CON_MASK        0x03

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

// events for processfunction
typedef enum {
	kAsySdoSeqEventNoEvent = 0x00,	// no Event
	kAsySdoSeqEventInitCon = 0x01,	// init connection
	kAsySdoSeqEventFrameRec = 0x02,	// frame received
	kAsySdoSeqEventFrameSend = 0x03,	// frame to send
	kAsySdoSeqEventTimeout = 0x04,	// Timeout for connection
	kAsySdoSeqEventCloseCon = 0x05	// higher layer close connection
} tEplAsySdoSeqEvent;

// structure for History-Buffer
typedef struct {
	u8 m_bFreeEntries;
	u8 m_bWrite;		// index of the next free buffer entry
	u8 m_bAck;		// index of the next message which should become acknowledged
	u8 m_bRead;		// index between m_bAck and m_bWrite to the next message for retransmission
	u8 m_aabHistoryFrame[EPL_SDO_HISTORY_SIZE]
	    [EPL_SEQ_HISTROY_FRAME_SIZE];
	unsigned int m_auiFrameSize[EPL_SDO_HISTORY_SIZE];

} tEplAsySdoConHistory;

// state of the statemaschine
typedef enum {
	kEplAsySdoStateIdle = 0x00,
	kEplAsySdoStateInit1 = 0x01,
	kEplAsySdoStateInit2 = 0x02,
	kEplAsySdoStateInit3 = 0x03,
	kEplAsySdoStateConnected = 0x04,
	kEplAsySdoStateWaitAck = 0x05
} tEplAsySdoState;

// connection control structure
typedef struct {
	tEplSdoConHdl m_ConHandle;
	tEplAsySdoState m_SdoState;
	u8 m_bRecSeqNum;	// name from view of the communication partner
	u8 m_bSendSeqNum;	// name from view of the communication partner
	tEplAsySdoConHistory m_SdoConHistory;
	tEplTimerHdl m_EplTimerHdl;
	unsigned int m_uiRetryCount;	// retry counter
	unsigned int m_uiUseCount;	// one sequence layer connection may be used by
	// multiple command layer connections

} tEplAsySdoSeqCon;

// instance structure
typedef struct {
	tEplAsySdoSeqCon m_AsySdoConnection[EPL_MAX_SDO_SEQ_CON];
	tEplSdoComReceiveCb m_fpSdoComReceiveCb;
	tEplSdoComConCb m_fpSdoComConCb;

#if defined(WIN32) || defined(_WIN32)
	LPCRITICAL_SECTION m_pCriticalSection;
	CRITICAL_SECTION m_CriticalSection;

	LPCRITICAL_SECTION m_pCriticalSectionReceive;
	CRITICAL_SECTION m_CriticalSectionReceive;
#endif

} tEplAsySdoSequInstance;

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------

static tEplAsySdoSequInstance AsySdoSequInstance_g;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static tEplKernel EplSdoAsySeqProcess(unsigned int uiHandle_p,
				      unsigned int uiDataSize_p,
				      tEplFrame * pData_p,
				      tEplAsySdoSeq * pRecFrame_p,
				      tEplAsySdoSeqEvent Event_p);

static tEplKernel EplSdoAsySeqSendIntern(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					 unsigned int uiDataSize_p,
					 tEplFrame * pData_p,
					 BOOL fFrameInHistory);

static tEplKernel EplSdoAsySeqSendLowerLayer(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					     unsigned int uiDataSize_p,
					     tEplFrame * pEplFrame_p);

tEplKernel EplSdoAsyReceiveCb(tEplSdoConHdl ConHdl_p,
			      tEplAsySdoSeq *pSdoSeqData_p,
			      unsigned int uiDataSize_p);

static tEplKernel EplSdoAsyInitHistory(void);

static tEplKernel EplSdoAsyAddFrameToHistory(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					     tEplFrame * pFrame_p,
					     unsigned int uiSize_p);

static tEplKernel EplSdoAsyAckFrameToHistory(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					     u8 bRecSeqNumber_p);

static tEplKernel EplSdoAsyReadFromHistory(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					   tEplFrame ** ppFrame_p,
					   unsigned int *puiSize_p,
					   BOOL fInitRead);

static unsigned int EplSdoAsyGetFreeEntriesFromHistory(tEplAsySdoSeqCon *
						       pAsySdoSeqCon_p);

static tEplKernel EplSdoAsySeqSetTimer(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
				       unsigned long ulTimeout);

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  <EPL asychronus SDO Sequence layer>                 */
/*                                                                         */
/*                                                                         */
/***************************************************************************/
//
// Description: this module contains the asynchronus SDO Sequence Layer for
//              the EPL SDO service
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
// Function:    EplSdoAsySeqInit
//
// Description: init first instance
//
//
//
// Parameters:  fpSdoComCb_p    = callback function to inform Command layer
//                                about new frames
//              fpSdoComConCb_p = callback function to inform command layer
//                                about connection state
//
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplSdoAsySeqInit(tEplSdoComReceiveCb fpSdoComCb_p,
			    tEplSdoComConCb fpSdoComConCb_p)
{
	tEplKernel Ret;

	Ret = EplSdoAsySeqAddInstance(fpSdoComCb_p, fpSdoComConCb_p);

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplSdoAsySeqAddInstance
//
// Description: init following instances
//
//
//
// Parameters:  fpSdoComCb_p    = callback function to inform Command layer
//                                about new frames
//              fpSdoComConCb_p = callback function to inform command layer
//                                about connection state
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplSdoAsySeqAddInstance(tEplSdoComReceiveCb fpSdoComCb_p,
				   tEplSdoComConCb fpSdoComConCb_p)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// check functionpointer
	if (fpSdoComCb_p == NULL) {
		Ret = kEplSdoSeqMissCb;
		goto Exit;
	} else {
		AsySdoSequInstance_g.m_fpSdoComReceiveCb = fpSdoComCb_p;
	}

	// check functionpointer
	if (fpSdoComConCb_p == NULL) {
		Ret = kEplSdoSeqMissCb;
		goto Exit;
	} else {
		AsySdoSequInstance_g.m_fpSdoComConCb = fpSdoComConCb_p;
	}

	// set controllstructure to 0
	EPL_MEMSET(&AsySdoSequInstance_g.m_AsySdoConnection[0], 0x00,
		   sizeof(AsySdoSequInstance_g.m_AsySdoConnection));

	// init History
	Ret = EplSdoAsyInitHistory();
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#if defined(WIN32) || defined(_WIN32)
	// create critical section for process function
	AsySdoSequInstance_g.m_pCriticalSection =
	    &AsySdoSequInstance_g.m_CriticalSection;
	InitializeCriticalSection(AsySdoSequInstance_g.m_pCriticalSection);

	// init critical section for receive cb function
	AsySdoSequInstance_g.m_pCriticalSectionReceive =
	    &AsySdoSequInstance_g.m_CriticalSectionReceive;
	InitializeCriticalSection(AsySdoSequInstance_g.
				  m_pCriticalSectionReceive);
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)
	// init lower layer
	Ret = EplSdoUdpuAddInstance(EplSdoAsyReceiveCb);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_ASND)) != 0)
	// init lower layer
	Ret = EplSdoAsnduAddInstance(EplSdoAsyReceiveCb);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#endif

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplSdoAsySeqDelInstance
//
// Description: delete instances
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplSdoAsySeqDelInstance(void)
{
	tEplKernel Ret;
	unsigned int uiCount;
	tEplAsySdoSeqCon *pAsySdoSeqCon;

	Ret = kEplSuccessful;

	// delete timer of open connections
	uiCount = 0;
	pAsySdoSeqCon = &AsySdoSequInstance_g.m_AsySdoConnection[0];
	while (uiCount < EPL_MAX_SDO_SEQ_CON) {
		if (pAsySdoSeqCon->m_ConHandle != 0) {
			EplTimeruDeleteTimer(&pAsySdoSeqCon->m_EplTimerHdl);
		}
		uiCount++;
		pAsySdoSeqCon++;
	}

#if defined(WIN32) || defined(_WIN32)
	// delete critical section for process function
	DeleteCriticalSection(AsySdoSequInstance_g.m_pCriticalSection);
#endif

	// set instance-table to 0
	EPL_MEMSET(&AsySdoSequInstance_g, 0x00, sizeof(AsySdoSequInstance_g));

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)
	// delete lower layer
	Ret = EplSdoUdpuDelInstance();
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_ASND)) != 0)
	// delete lower layer
	Ret = EplSdoAsnduDelInstance();
#endif

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplSdoAsySeqInitCon
//
// Description: start initialization of a sequence layer connection.
//              It tries to reuse an existing connection to the same node.
//
//
// Parameters:  pSdoSeqConHdl_p = pointer to the variable for the connection handle
//              uiNodeId_p      = Node Id of the target
//              SdoType          = Type of the SDO connection
//
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplSdoAsySeqInitCon(tEplSdoSeqConHdl *pSdoSeqConHdl_p,
			       unsigned int uiNodeId_p,
			       tEplSdoType SdoType)
{
	tEplKernel Ret;
	unsigned int uiCount;
	unsigned int uiFreeCon;
	tEplSdoConHdl ConHandle;
	tEplAsySdoSeqCon *pAsySdoSeqCon;
	Ret = kEplSuccessful;

	// check SdoType
	// call init function of the protcol abstraction layer
	// which tries to find an existing connection to the same node
	switch (SdoType) {
		// SDO over UDP
	case kEplSdoTypeUdp:
		{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)
			Ret = EplSdoUdpuInitCon(&ConHandle, uiNodeId_p);
			if (Ret != kEplSuccessful) {
				goto Exit;
			}
#else
			Ret = kEplSdoSeqUnsupportedProt;
#endif
			break;
		}

		// SDO over Asnd
	case kEplSdoTypeAsnd:
		{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_ASND)) != 0)
			Ret = EplSdoAsnduInitCon(&ConHandle, uiNodeId_p);
			if (Ret != kEplSuccessful) {
				goto Exit;
			}
#else
			Ret = kEplSdoSeqUnsupportedProt;
#endif
			break;
		}

		// unsupported protocols
		// -> auto should be replaced by command layer
	case kEplSdoTypeAuto:
	case kEplSdoTypePdo:
	default:
		{
			Ret = kEplSdoSeqUnsupportedProt;
			goto Exit;
		}

	}			// end of switch(SdoType)

	// find existing connection to the same node or find empty entry for connection
	uiCount = 0;
	uiFreeCon = EPL_MAX_SDO_SEQ_CON;
	pAsySdoSeqCon = &AsySdoSequInstance_g.m_AsySdoConnection[0];

	while (uiCount < EPL_MAX_SDO_SEQ_CON) {
		if (pAsySdoSeqCon->m_ConHandle == ConHandle) {	// existing connection found
			break;
		}
		if (pAsySdoSeqCon->m_ConHandle == 0) {
			uiFreeCon = uiCount;
		}
		uiCount++;
		pAsySdoSeqCon++;
	}

	if (uiCount == EPL_MAX_SDO_SEQ_CON) {
		if (uiFreeCon == EPL_MAX_SDO_SEQ_CON) {	// no free entry found
			switch (SdoType) {
				// SDO over UDP
			case kEplSdoTypeUdp:
				{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)
					Ret = EplSdoUdpuDelCon(ConHandle);
					if (Ret != kEplSuccessful) {
						goto Exit;
					}
#endif
					break;
				}

				// SDO over Asnd
			case kEplSdoTypeAsnd:
				{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_ASND)) != 0)
					Ret = EplSdoAsnduDelCon(ConHandle);
					if (Ret != kEplSuccessful) {
						goto Exit;
					}
#endif
					break;
				}

				// unsupported protocols
				// -> auto should be replaced by command layer
			case kEplSdoTypeAuto:
			case kEplSdoTypePdo:
			default:
				{
					Ret = kEplSdoSeqUnsupportedProt;
					goto Exit;
				}

			}	// end of switch(SdoType)

			Ret = kEplSdoSeqNoFreeHandle;
			goto Exit;
		} else {	// free entry found
			pAsySdoSeqCon =
			    &AsySdoSequInstance_g.m_AsySdoConnection[uiFreeCon];
			pAsySdoSeqCon->m_ConHandle = ConHandle;
			uiCount = uiFreeCon;
		}
	}
	// set handle
	*pSdoSeqConHdl_p = (uiCount | EPL_SDO_ASY_HANDLE);

	// increment use counter
	pAsySdoSeqCon->m_uiUseCount++;

	// call intern process function
	Ret = EplSdoAsySeqProcess(uiCount,
				  0, NULL, NULL, kAsySdoSeqEventInitCon);

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplSdoAsySeqSendData
//
// Description: send sata unsing a established connection
//
//
//
// Parameters:  pSdoSeqConHdl_p = connection handle
//              uiDataSize_p    = Size of Frame to send
//                                  -> wihtout SDO sequence layer header, Asnd header
//                                     and ethernetnet
//                                  ==> SDO Sequence layer payload
//              SdoType          = Type of the SDO connection
//
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplSdoAsySeqSendData(tEplSdoSeqConHdl SdoSeqConHdl_p,
				unsigned int uiDataSize_p,
				tEplFrame *pabData_p)
{
	tEplKernel Ret;
	unsigned int uiHandle;

	uiHandle = (SdoSeqConHdl_p & ~EPL_SDO_SEQ_HANDLE_MASK);

	// check if connection ready
	if (AsySdoSequInstance_g.m_AsySdoConnection[uiHandle].m_SdoState ==
	    kEplAsySdoStateIdle) {
		// no connection with this handle
		Ret = kEplSdoSeqInvalidHdl;
		goto Exit;
	} else if (AsySdoSequInstance_g.m_AsySdoConnection[uiHandle].
		   m_SdoState != kEplAsySdoStateConnected) {
		Ret = kEplSdoSeqConnectionBusy;
		goto Exit;
	}

	Ret = EplSdoAsySeqProcess(uiHandle,
				  uiDataSize_p,
				  pabData_p, NULL, kAsySdoSeqEventFrameSend);
      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplSdoAsySeqProcessEvent
//
// Description: function processes extern events
//              -> later needed for timeout controll with timer-module
//
//
//
// Parameters:  pEvent_p = pointer to event
//
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplSdoAsySeqProcessEvent(tEplEvent *pEvent_p)
{
	tEplKernel Ret;
	tEplTimerEventArg *pTimerEventArg;
	tEplAsySdoSeqCon *pAsySdoSeqCon;
	tEplTimerHdl EplTimerHdl;
	unsigned int uiCount;

	Ret = kEplSuccessful;
	// check parameter
	if (pEvent_p == NULL) {
		Ret = kEplSdoSeqInvalidEvent;
		goto Exit;
	}

	if (pEvent_p->m_EventType != kEplEventTypeTimer) {
		Ret = kEplSdoSeqInvalidEvent;
		goto Exit;
	}
	// get timerhdl
	pTimerEventArg = (tEplTimerEventArg *) pEvent_p->m_pArg;
	EplTimerHdl = pTimerEventArg->m_TimerHdl;

	// get pointer to intern control structure of connection
	if (pTimerEventArg->m_ulArg == 0) {
		goto Exit;
	}
	pAsySdoSeqCon = (tEplAsySdoSeqCon *) pTimerEventArg->m_ulArg;

	// check if time is current
	if (EplTimerHdl != pAsySdoSeqCon->m_EplTimerHdl) {
		// delete timer
		EplTimeruDeleteTimer(&EplTimerHdl);
		goto Exit;
	}
	// delete timer
	EplTimeruDeleteTimer(&pAsySdoSeqCon->m_EplTimerHdl);

	// get indexnumber of control structure
	uiCount = 0;
	while ((&AsySdoSequInstance_g.m_AsySdoConnection[uiCount]) !=
	       pAsySdoSeqCon) {
		uiCount++;
		if (uiCount > EPL_MAX_SDO_SEQ_CON) {
			goto Exit;
		}
	}

	// process event and call processfunction if needed
	Ret = EplSdoAsySeqProcess(uiCount,
				  0, NULL, NULL, kAsySdoSeqEventTimeout);

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplSdoAsySeqDelCon
//
// Description: del and close one connection
//
//
//
// Parameters:  SdoSeqConHdl_p = handle of connection
//
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplSdoAsySeqDelCon(tEplSdoSeqConHdl SdoSeqConHdl_p)
{
	tEplKernel Ret = kEplSuccessful;
	unsigned int uiHandle;
	tEplAsySdoSeqCon *pAsySdoSeqCon;

	uiHandle = (SdoSeqConHdl_p & ~EPL_SDO_SEQ_HANDLE_MASK);

	// check if handle invalid
	if (uiHandle >= EPL_MAX_SDO_SEQ_CON) {
		Ret = kEplSdoSeqInvalidHdl;
		goto Exit;
	}
	// get pointer to connection
	pAsySdoSeqCon = &AsySdoSequInstance_g.m_AsySdoConnection[uiHandle];

	// decrement use counter
	pAsySdoSeqCon->m_uiUseCount--;

	if (pAsySdoSeqCon->m_uiUseCount == 0) {
		// process close in processfunction
		Ret = EplSdoAsySeqProcess(uiHandle,
					  0,
					  NULL, NULL, kAsySdoSeqEventCloseCon);

		//check protocol
		if ((pAsySdoSeqCon->m_ConHandle & EPL_SDO_ASY_HANDLE_MASK) ==
		    EPL_SDO_UDP_HANDLE) {
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)
			// call close function of lower layer
			EplSdoUdpuDelCon(pAsySdoSeqCon->m_ConHandle);
#endif // end of #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)
		} else {
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_ASND)) != 0)
			// call close function of lower layer
			EplSdoAsnduDelCon(pAsySdoSeqCon->m_ConHandle);
#endif // end of #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_ASND)) != 0)
		}

		// delete timer
		EplTimeruDeleteTimer(&pAsySdoSeqCon->m_EplTimerHdl);

		// clean controllstructure
		EPL_MEMSET(pAsySdoSeqCon, 0x00, sizeof(tEplAsySdoSeqCon));
		pAsySdoSeqCon->m_SdoConHistory.m_bFreeEntries =
		    EPL_SDO_HISTORY_SIZE;
	}

      Exit:
	return Ret;

}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplEplSdoAsySeqProcess
//
// Description: intern function to process the asynchronus SDO Sequence Layer
//              state maschine
//
//
//
// Parameters:  uiHandle_p      = index of the control structure of the connection
//              uiDataSize_p    = size of data frame to process (can be 0)
//                                  -> without size of sequence header and Asnd header!!!
//
//              pData_p         = pointer to frame to send (can be NULL)
//              pRecFrame_p     = pointer to received frame (can be NULL)
//              Event_p         = Event to process
//
//
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoAsySeqProcess(unsigned int uiHandle_p,
				      unsigned int uiDataSize_p,
				      tEplFrame * pData_p,
				      tEplAsySdoSeq * pRecFrame_p,
				      tEplAsySdoSeqEvent Event_p)
{
	tEplKernel Ret;
	unsigned int uiFrameSize;
	tEplFrame *pEplFrame;
	tEplAsySdoSeqCon *pAsySdoSeqCon;
	tEplSdoSeqConHdl SdoSeqConHdl;
	unsigned int uiFreeEntries;

#if defined(WIN32) || defined(_WIN32)
	// enter  critical section for process function
	EnterCriticalSection(AsySdoSequInstance_g.m_pCriticalSection);
#endif

	Ret = kEplSuccessful;

	// get handle for hinger layer
	SdoSeqConHdl = uiHandle_p | EPL_SDO_ASY_HANDLE;

	// check if handle invalid
	if ((SdoSeqConHdl & ~EPL_SDO_SEQ_HANDLE_MASK) ==
	    EPL_SDO_SEQ_INVALID_HDL) {
		Ret = kEplSdoSeqInvalidHdl;
		goto Exit;
	}
	// get pointer to connection
	pAsySdoSeqCon = &AsySdoSequInstance_g.m_AsySdoConnection[uiHandle_p];

	// check size
	if ((pData_p == NULL) && (pRecFrame_p == NULL) && (uiDataSize_p != 0)) {
		Ret = kEplSdoSeqInvalidFrame;
		goto Exit;
	}
	// check state
	switch (pAsySdoSeqCon->m_SdoState) {
		// idle state
	case kEplAsySdoStateIdle:
		{
			// check event
			switch (Event_p) {
				// new connection
				// -> send init frame and change to
				// kEplAsySdoStateInit1
			case kAsySdoSeqEventInitCon:
				{
					// set sending scon to 1
					pAsySdoSeqCon->m_bRecSeqNum = 0x01;
					// set set send rcon to 0
					pAsySdoSeqCon->m_bSendSeqNum = 0x00;
					Ret =
					    EplSdoAsySeqSendIntern
					    (pAsySdoSeqCon, 0, NULL, FALSE);
					if (Ret != kEplSuccessful) {
						goto Exit;
					}
					// change state
					pAsySdoSeqCon->m_SdoState =
					    kEplAsySdoStateInit1;

					// set timer
					Ret =
					    EplSdoAsySeqSetTimer(pAsySdoSeqCon,
								 EPL_SEQ_DEFAULT_TIMEOUT);

					break;
				}

				// init con from extern
				// check rcon and scon
				// -> send answer
			case kAsySdoSeqEventFrameRec:
				{
/*
                    PRINTF3("%s scon=%u rcon=%u\n",
                            __func__,
                            pRecFrame_p->m_le_bSendSeqNumCon,
                            pRecFrame_p->m_le_bRecSeqNumCon);
*/
					// check if scon == 1 and rcon == 0
					if (((pRecFrame_p->
					      m_le_bRecSeqNumCon &
					      EPL_ASY_SDO_CON_MASK) == 0x00)
					    &&
					    ((pRecFrame_p->
					      m_le_bSendSeqNumCon &
					      EPL_ASY_SDO_CON_MASK) == 0x01)) {
						// save sequence numbers
						pAsySdoSeqCon->m_bRecSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bRecSeqNumCon);
						pAsySdoSeqCon->m_bSendSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bSendSeqNumCon);
						// create answer and send answer
						// set rcon to 1 (in send direction own scon)
						pAsySdoSeqCon->m_bRecSeqNum++;
						Ret =
						    EplSdoAsySeqSendIntern
						    (pAsySdoSeqCon, 0, NULL,
						     FALSE);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						// change state to kEplAsySdoStateInit2
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateInit2;

						// set timer
						Ret =
						    EplSdoAsySeqSetTimer
						    (pAsySdoSeqCon,
						     EPL_SEQ_DEFAULT_TIMEOUT);
					} else {	// error -> close
						// delete timer
						EplTimeruDeleteTimer
						    (&pAsySdoSeqCon->
						     m_EplTimerHdl);
						if (((pRecFrame_p->
						      m_le_bRecSeqNumCon &
						      EPL_ASY_SDO_CON_MASK) !=
						     0x00)
						    || ((pRecFrame_p->m_le_bSendSeqNumCon & EPL_ASY_SDO_CON_MASK) != 0x00)) {	// d.k. only answer with close message if the message sent was not a close message
							// save sequence numbers
							pAsySdoSeqCon->
							    m_bRecSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bRecSeqNumCon);
							pAsySdoSeqCon->
							    m_bSendSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bSendSeqNumCon);
							// set rcon and scon to 0
							pAsySdoSeqCon->
							    m_bSendSeqNum &=
							    EPL_SEQ_NUM_MASK;
							pAsySdoSeqCon->
							    m_bRecSeqNum &=
							    EPL_SEQ_NUM_MASK;
							// send frame
							EplSdoAsySeqSendIntern
							    (pAsySdoSeqCon, 0,
							     NULL, FALSE);
						}
						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateInitError);
					}
					break;
				}

			default:
				// d.k. do nothing
				break;

			}	// end of switch(Event_p)
			break;
		}

		// init connection step 1
		// wait for frame with scon = 1
		// and rcon = 1
	case kEplAsySdoStateInit1:
		{
//            PRINTF0("EplSdoAsySequ: StateInit1\n");

			// check event
			switch (Event_p) {
				// frame received
			case kAsySdoSeqEventFrameRec:
				{
					// check scon == 1 and rcon == 1
					if (((pRecFrame_p->
					      m_le_bRecSeqNumCon &
					      EPL_ASY_SDO_CON_MASK) == 0x01)
					    && ((pRecFrame_p->m_le_bSendSeqNumCon & EPL_ASY_SDO_CON_MASK) == 0x01)) {	// create answer own scon = 2
						// save sequence numbers
						pAsySdoSeqCon->m_bRecSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bRecSeqNumCon);
						pAsySdoSeqCon->m_bSendSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bSendSeqNumCon);

						pAsySdoSeqCon->m_bRecSeqNum++;
						Ret =
						    EplSdoAsySeqSendIntern
						    (pAsySdoSeqCon, 0, NULL,
						     FALSE);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						// change state to kEplAsySdoStateInit3
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateInit3;

						// set timer
						Ret =
						    EplSdoAsySeqSetTimer
						    (pAsySdoSeqCon,
						     EPL_SEQ_DEFAULT_TIMEOUT);

					}
					// check if scon == 1 and rcon == 0, i.e. other side wants me to be server
					else if (((pRecFrame_p->
						   m_le_bRecSeqNumCon &
						   EPL_ASY_SDO_CON_MASK) ==
						  0x00)
						 &&
						 ((pRecFrame_p->
						   m_le_bSendSeqNumCon &
						   EPL_ASY_SDO_CON_MASK) ==
						  0x01)) {
						// save sequence numbers
						pAsySdoSeqCon->m_bRecSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bRecSeqNumCon);
						pAsySdoSeqCon->m_bSendSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bSendSeqNumCon);
						// create answer and send answer
						// set rcon to 1 (in send direction own scon)
						pAsySdoSeqCon->m_bRecSeqNum++;
						Ret =
						    EplSdoAsySeqSendIntern
						    (pAsySdoSeqCon, 0, NULL,
						     FALSE);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						// change state to kEplAsySdoStateInit2
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateInit2;

						// set timer
						Ret =
						    EplSdoAsySeqSetTimer
						    (pAsySdoSeqCon,
						     EPL_SEQ_DEFAULT_TIMEOUT);
					} else {	// error -> Close
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateIdle;
						// delete timer
						EplTimeruDeleteTimer
						    (&pAsySdoSeqCon->
						     m_EplTimerHdl);
						if (((pRecFrame_p->
						      m_le_bRecSeqNumCon &
						      EPL_ASY_SDO_CON_MASK) !=
						     0x00)
						    || ((pRecFrame_p->m_le_bSendSeqNumCon & EPL_ASY_SDO_CON_MASK) != 0x00)) {	// d.k. only answer with close message if the message sent was not a close message
							// save sequence numbers
							pAsySdoSeqCon->
							    m_bRecSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bRecSeqNumCon);
							pAsySdoSeqCon->
							    m_bSendSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bSendSeqNumCon);

							// set rcon and scon to 0
							pAsySdoSeqCon->
							    m_bSendSeqNum &=
							    EPL_SEQ_NUM_MASK;
							pAsySdoSeqCon->
							    m_bRecSeqNum &=
							    EPL_SEQ_NUM_MASK;
							// send frame
							EplSdoAsySeqSendIntern
							    (pAsySdoSeqCon, 0,
							     NULL, FALSE);
						}
						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateInitError);
					}
					break;
				}

				// timeout
			case kAsySdoSeqEventTimeout:
				{	// error -> Close
					pAsySdoSeqCon->m_SdoState =
					    kEplAsySdoStateIdle;

					// set rcon and scon to 0
					pAsySdoSeqCon->m_bSendSeqNum &=
					    EPL_SEQ_NUM_MASK;
					pAsySdoSeqCon->m_bRecSeqNum &=
					    EPL_SEQ_NUM_MASK;
					// send frame
					EplSdoAsySeqSendIntern(pAsySdoSeqCon,
							       0, NULL, FALSE);
					// call Command Layer Cb
					AsySdoSequInstance_g.
					    m_fpSdoComConCb(SdoSeqConHdl,
							    kAsySdoConStateInitError);
					break;
				}

			default:
				// d.k. do nothing
				break;

			}	// end of switch(Event_p)
			break;
		}

		// init connection step 2
	case kEplAsySdoStateInit2:
		{
//            PRINTF0("EplSdoAsySequ: StateInit2\n");

			// check event
			switch (Event_p) {
				// frame received
			case kAsySdoSeqEventFrameRec:
				{
					// check scon == 2 and rcon == 1
					if (((pRecFrame_p->
					      m_le_bRecSeqNumCon &
					      EPL_ASY_SDO_CON_MASK) == 0x01)
					    && ((pRecFrame_p->m_le_bSendSeqNumCon & EPL_ASY_SDO_CON_MASK) == 0x02)) {	// create answer own rcon = 2
						// save sequence numbers
						pAsySdoSeqCon->m_bRecSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bRecSeqNumCon);
						pAsySdoSeqCon->m_bSendSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bSendSeqNumCon);

						pAsySdoSeqCon->m_bRecSeqNum++;
						Ret =
						    EplSdoAsySeqSendIntern
						    (pAsySdoSeqCon, 0, NULL,
						     FALSE);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						// change state to kEplAsySdoStateConnected
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateConnected;

						// set timer
						Ret =
						    EplSdoAsySeqSetTimer
						    (pAsySdoSeqCon,
						     EPL_SEQ_DEFAULT_TIMEOUT);

						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateConnected);

					}
					// check scon == 1 and rcon == 1, i.e. other side wants me to initiate the connection
					else if (((pRecFrame_p->
						   m_le_bRecSeqNumCon &
						   EPL_ASY_SDO_CON_MASK) ==
						  0x01)
						 &&
						 ((pRecFrame_p->
						   m_le_bSendSeqNumCon &
						   EPL_ASY_SDO_CON_MASK) ==
						  0x01)) {
						// save sequence numbers
						pAsySdoSeqCon->m_bRecSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bRecSeqNumCon);
						pAsySdoSeqCon->m_bSendSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bSendSeqNumCon);
						// create answer and send answer
						// set rcon to 1 (in send direction own scon)
						pAsySdoSeqCon->m_bRecSeqNum++;
						Ret =
						    EplSdoAsySeqSendIntern
						    (pAsySdoSeqCon, 0, NULL,
						     FALSE);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						// set timer
						Ret =
						    EplSdoAsySeqSetTimer
						    (pAsySdoSeqCon,
						     EPL_SEQ_DEFAULT_TIMEOUT);
						// change state to kEplAsySdoStateInit3
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateInit3;

					} else {	// error -> Close
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateIdle;
						// delete timer
						EplTimeruDeleteTimer
						    (&pAsySdoSeqCon->
						     m_EplTimerHdl);
						if (((pRecFrame_p->
						      m_le_bRecSeqNumCon &
						      EPL_ASY_SDO_CON_MASK) !=
						     0x00)
						    || ((pRecFrame_p->m_le_bSendSeqNumCon & EPL_ASY_SDO_CON_MASK) != 0x00)) {	// d.k. only answer with close message if the message sent was not a close message
							// save sequence numbers
							pAsySdoSeqCon->
							    m_bRecSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bRecSeqNumCon);
							pAsySdoSeqCon->
							    m_bSendSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bSendSeqNumCon);
							// set rcon and scon to 0
							pAsySdoSeqCon->
							    m_bSendSeqNum &=
							    EPL_SEQ_NUM_MASK;
							pAsySdoSeqCon->
							    m_bRecSeqNum &=
							    EPL_SEQ_NUM_MASK;
							// send frame
							EplSdoAsySeqSendIntern
							    (pAsySdoSeqCon, 0,
							     NULL, FALSE);
						}
						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateInitError);
					}
					break;
				}

				// timeout
			case kAsySdoSeqEventTimeout:
				{	// error -> Close
					pAsySdoSeqCon->m_SdoState =
					    kEplAsySdoStateIdle;
					// set rcon and scon to 0
					pAsySdoSeqCon->m_bSendSeqNum &=
					    EPL_SEQ_NUM_MASK;
					pAsySdoSeqCon->m_bRecSeqNum &=
					    EPL_SEQ_NUM_MASK;
					// send frame
					EplSdoAsySeqSendIntern(pAsySdoSeqCon,
							       0, NULL, FALSE);

					// call Command Layer Cb
					AsySdoSequInstance_g.
					    m_fpSdoComConCb(SdoSeqConHdl,
							    kAsySdoConStateInitError);
					break;
				}

			default:
				// d.k. do nothing
				break;

			}	// end of switch(Event_p)
			break;
		}

		// init connection step 3
	case kEplAsySdoStateInit3:
		{
			// check event
			switch (Event_p) {
				// frame received
			case kAsySdoSeqEventFrameRec:
				{
					// check scon == 2 and rcon == 2
					if (((pRecFrame_p->
					      m_le_bRecSeqNumCon &
					      EPL_ASY_SDO_CON_MASK) == 0x02)
					    &&
					    ((pRecFrame_p->
					      m_le_bSendSeqNumCon &
					      EPL_ASY_SDO_CON_MASK) == 0x02)) {
						// save sequence numbers
						pAsySdoSeqCon->m_bRecSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bRecSeqNumCon);
						pAsySdoSeqCon->m_bSendSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bSendSeqNumCon);
						// change state to kEplAsySdoStateConnected
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateConnected;

						// set timer
						Ret =
						    EplSdoAsySeqSetTimer
						    (pAsySdoSeqCon,
						     EPL_SEQ_DEFAULT_TIMEOUT);
						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateConnected);

					}
					// check scon == 2 and rcon == 1
					else if (((pRecFrame_p->
						   m_le_bRecSeqNumCon &
						   EPL_ASY_SDO_CON_MASK) ==
						  0x01)
						 && ((pRecFrame_p->m_le_bSendSeqNumCon & EPL_ASY_SDO_CON_MASK) == 0x02)) {	// create answer own rcon = 2
						// save sequence numbers
						pAsySdoSeqCon->m_bRecSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bRecSeqNumCon);
						pAsySdoSeqCon->m_bSendSeqNum =
						    AmiGetByteFromLe
						    (&pRecFrame_p->
						     m_le_bSendSeqNumCon);

						pAsySdoSeqCon->m_bRecSeqNum++;
						Ret =
						    EplSdoAsySeqSendIntern
						    (pAsySdoSeqCon, 0, NULL,
						     FALSE);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						// change state to kEplAsySdoStateConnected
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateConnected;

						// set timer
						Ret =
						    EplSdoAsySeqSetTimer
						    (pAsySdoSeqCon,
						     EPL_SEQ_DEFAULT_TIMEOUT);

						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateConnected);

					} else {	// error -> Close
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateIdle;
						// delete timer
						EplTimeruDeleteTimer
						    (&pAsySdoSeqCon->
						     m_EplTimerHdl);
						if (((pRecFrame_p->
						      m_le_bRecSeqNumCon &
						      EPL_ASY_SDO_CON_MASK) !=
						     0x00)
						    || ((pRecFrame_p->m_le_bSendSeqNumCon & EPL_ASY_SDO_CON_MASK) != 0x00)) {	// d.k. only answer with close message if the message sent was not a close message
							// save sequence numbers
							pAsySdoSeqCon->
							    m_bRecSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bRecSeqNumCon);
							pAsySdoSeqCon->
							    m_bSendSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bSendSeqNumCon);
							// set rcon and scon to 0
							pAsySdoSeqCon->
							    m_bSendSeqNum &=
							    EPL_SEQ_NUM_MASK;
							pAsySdoSeqCon->
							    m_bRecSeqNum &=
							    EPL_SEQ_NUM_MASK;
							// send frame
							EplSdoAsySeqSendIntern
							    (pAsySdoSeqCon, 0,
							     NULL, FALSE);
						}
						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateInitError);
					}
					break;
				}

				// timeout
			case kAsySdoSeqEventTimeout:
				{	// error -> Close
					pAsySdoSeqCon->m_SdoState =
					    kEplAsySdoStateIdle;
					// set rcon and scon to 0
					pAsySdoSeqCon->m_bSendSeqNum &=
					    EPL_SEQ_NUM_MASK;
					pAsySdoSeqCon->m_bRecSeqNum &=
					    EPL_SEQ_NUM_MASK;
					// send frame
					EplSdoAsySeqSendIntern(pAsySdoSeqCon,
							       0, NULL, FALSE);

					// call Command Layer Cb
					AsySdoSequInstance_g.
					    m_fpSdoComConCb(SdoSeqConHdl,
							    kAsySdoConStateInitError);
					break;
				}

			default:
				// d.k. do nothing
				break;

			}	// end of switch(Event_p)
			break;
		}

		// connection established
	case kEplAsySdoStateConnected:
		{
			// check event
			switch (Event_p) {

				// frame to send
			case kAsySdoSeqEventFrameSend:
				{
					// set timer
					Ret =
					    EplSdoAsySeqSetTimer(pAsySdoSeqCon,
								 EPL_SEQ_DEFAULT_TIMEOUT);
					// check if data frame or ack
					if (pData_p == NULL) {	// send ack
						// inc scon
						//pAsySdoSeqCon->m_bRecSeqNum += 4;
						Ret =
						    EplSdoAsySeqSendIntern
						    (pAsySdoSeqCon, 0, NULL,
						     FALSE);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
					} else {	// send dataframe
						// increment send sequence number
						pAsySdoSeqCon->m_bRecSeqNum +=
						    4;
						Ret =
						    EplSdoAsySeqSendIntern
						    (pAsySdoSeqCon,
						     uiDataSize_p, pData_p,
						     TRUE);
						if (Ret == kEplSdoSeqRequestAckNeeded) {	// request ack
							// change state to wait ack
							pAsySdoSeqCon->
							    m_SdoState =
							    kEplAsySdoStateWaitAck;
							// set Ret to kEplSuccessful, because no error
							// for higher layer
							Ret = kEplSuccessful;

						} else if (Ret !=
							   kEplSuccessful) {
							goto Exit;
						} else {
							// call Command Layer Cb
							AsySdoSequInstance_g.
							    m_fpSdoComConCb
							    (SdoSeqConHdl,
							     kAsySdoConStateFrameSended);
						}
					}
					break;
				}	// end of case kAsySdoSeqEventFrameSend

				// frame received
			case kAsySdoSeqEventFrameRec:
				{
					u8 bSendSeqNumCon =
					    AmiGetByteFromLe(&pRecFrame_p->
							     m_le_bSendSeqNumCon);

					// set timer
					Ret =
					    EplSdoAsySeqSetTimer(pAsySdoSeqCon,
								 EPL_SEQ_DEFAULT_TIMEOUT);
					// check scon
					switch (bSendSeqNumCon &
						EPL_ASY_SDO_CON_MASK) {
						// close from other node
					case 0:
					case 1:
						{
							// return to idle
							pAsySdoSeqCon->
							    m_SdoState =
							    kEplAsySdoStateIdle;
							// delete timer
							EplTimeruDeleteTimer
							    (&pAsySdoSeqCon->
							     m_EplTimerHdl);
							// call Command Layer Cb
							AsySdoSequInstance_g.
							    m_fpSdoComConCb
							    (SdoSeqConHdl,
							     kAsySdoConStateConClosed);

							break;
						}

						// Request Ack or Error Ack
						// possible contain data
					case 3:
						// normal frame
					case 2:
						{
							if ((AmiGetByteFromLe
							     (&pRecFrame_p->
							      m_le_bRecSeqNumCon)
							     &
							     EPL_ASY_SDO_CON_MASK)
							    == 3) {
//                                PRINTF0("EplSdoAsySequ: error response received\n");

								// error response (retransmission request)
								// resend frames from history

								// read frame from history
								Ret =
								    EplSdoAsyReadFromHistory
								    (pAsySdoSeqCon,
								     &pEplFrame,
								     &uiFrameSize,
								     TRUE);
								if (Ret !=
								    kEplSuccessful)
								{
									goto Exit;
								}

								while ((pEplFrame != NULL)
								       &&
								       (uiFrameSize
									!= 0)) {
									// send frame
									Ret =
									    EplSdoAsySeqSendLowerLayer
									    (pAsySdoSeqCon,
									     uiFrameSize,
									     pEplFrame);
									if (Ret
									    !=
									    kEplSuccessful)
									{
										goto Exit;
									}
									// read next frame from history
									Ret =
									    EplSdoAsyReadFromHistory
									    (pAsySdoSeqCon,
									     &pEplFrame,
									     &uiFrameSize,
									     FALSE);
									if (Ret
									    !=
									    kEplSuccessful)
									{
										goto Exit;
									}
								}	// end of while((pabFrame != NULL)
							}	// end of if (error response)

							if (((pAsySdoSeqCon->m_bSendSeqNum + 4) & EPL_SEQ_NUM_MASK) == (bSendSeqNumCon & EPL_SEQ_NUM_MASK)) {	// next frame of sequence received
								// save send sequence number (without ack request)
								pAsySdoSeqCon->
								    m_bSendSeqNum
								    =
								    bSendSeqNumCon
								    & ~0x01;

								// check if ack or data-frame
								//ignore ack -> already processed
								if (uiDataSize_p
								    >
								    EPL_SEQ_HEADER_SIZE)
								{
									AsySdoSequInstance_g.
									    m_fpSdoComReceiveCb
									    (SdoSeqConHdl,
									     ((tEplAsySdoCom *) & pRecFrame_p->m_le_abSdoSeqPayload), (uiDataSize_p - EPL_SEQ_HEADER_SIZE));
									// call Command Layer Cb
									AsySdoSequInstance_g.
									    m_fpSdoComConCb
									    (SdoSeqConHdl,
									     kAsySdoConStateFrameSended);

								} else {
									// call Command Layer Cb
									AsySdoSequInstance_g.
									    m_fpSdoComConCb
									    (SdoSeqConHdl,
									     kAsySdoConStateAckReceived);
								}
							} else if (((bSendSeqNumCon - pAsySdoSeqCon->m_bSendSeqNum - 4) & EPL_SEQ_NUM_MASK) < EPL_SEQ_NUM_THRESHOLD) {	// frame of sequence was lost,
								// because difference of received and old value
								// is less then halve of the values range.

								// send error frame with own rcon = 3
								pAsySdoSeqCon->
								    m_bSendSeqNum
								    |= 0x03;
								Ret =
								    EplSdoAsySeqSendIntern
								    (pAsySdoSeqCon,
								     0, NULL,
								     FALSE);
								// restore send sequence number
								pAsySdoSeqCon->
								    m_bSendSeqNum
								    =
								    (pAsySdoSeqCon->
								     m_bSendSeqNum
								     &
								     EPL_SEQ_NUM_MASK)
								    | 0x02;
								if (Ret !=
								    kEplSuccessful)
								{
									goto Exit;
								}
								// break here, because a requested acknowledge
								// was sent implicitly above
								break;
							}
							// else, ignore repeated frame

							if ((bSendSeqNumCon & EPL_ASY_SDO_CON_MASK) == 3) {	// ack request received

								// create ack with own scon = 2
								Ret =
								    EplSdoAsySeqSendIntern
								    (pAsySdoSeqCon,
								     0, NULL,
								     FALSE);
								if (Ret !=
								    kEplSuccessful)
								{
									goto Exit;
								}
							}

							break;
						}

					}	// switch(pAsySdoSeqCon->m_bSendSeqNum & EPL_ASY_SDO_CON_MASK)
					break;
				}	// end of case kAsySdoSeqEventFrameRec:

				//close event from higher layer
			case kAsySdoSeqEventCloseCon:
				{
					pAsySdoSeqCon->m_SdoState =
					    kEplAsySdoStateIdle;
					// set rcon and scon to 0
					pAsySdoSeqCon->m_bSendSeqNum &=
					    EPL_SEQ_NUM_MASK;
					pAsySdoSeqCon->m_bRecSeqNum &=
					    EPL_SEQ_NUM_MASK;
					// send frame
					EplSdoAsySeqSendIntern(pAsySdoSeqCon,
							       0, NULL, FALSE);

					// delete timer
					EplTimeruDeleteTimer(&pAsySdoSeqCon->
							     m_EplTimerHdl);
					// call Command Layer Cb is not necessary, because the event came from there
//                    AsySdoSequInstance_g.m_fpSdoComConCb(SdoSeqConHdl,
//                                                            kAsySdoConStateInitError);
					break;
				}

				// timeout
			case kAsySdoSeqEventTimeout:
				{

					uiFreeEntries =
					    EplSdoAsyGetFreeEntriesFromHistory
					    (pAsySdoSeqCon);
					if ((uiFreeEntries <
					     EPL_SDO_HISTORY_SIZE)
					    && (pAsySdoSeqCon->m_uiRetryCount < EPL_SEQ_RETRY_COUNT)) {	// unacknowlegded frames in history
						// and retry counter not exceeded

						// resend data with acknowledge request

						// increment retry counter
						pAsySdoSeqCon->m_uiRetryCount++;

						// set timer
						Ret =
						    EplSdoAsySeqSetTimer
						    (pAsySdoSeqCon,
						     EPL_SEQ_DEFAULT_TIMEOUT);

						// read first frame from history
						Ret =
						    EplSdoAsyReadFromHistory
						    (pAsySdoSeqCon, &pEplFrame,
						     &uiFrameSize, TRUE);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}

						if ((pEplFrame != NULL)
						    && (uiFrameSize != 0)) {

							// set ack request in scon
							AmiSetByteToLe
							    (&pEplFrame->m_Data.
							     m_Asnd.m_Payload.
							     m_SdoSequenceFrame.
							     m_le_bSendSeqNumCon,
							     AmiGetByteFromLe
							     (&pEplFrame->
							      m_Data.m_Asnd.
							      m_Payload.
							      m_SdoSequenceFrame.
							      m_le_bSendSeqNumCon)
							     | 0x03);

							// send frame
							Ret =
							    EplSdoAsySeqSendLowerLayer
							    (pAsySdoSeqCon,
							     uiFrameSize,
							     pEplFrame);
							if (Ret !=
							    kEplSuccessful) {
								goto Exit;
							}

						}
					} else {
						// timeout, because of no traffic -> Close
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateIdle;
						// set rcon and scon to 0
						pAsySdoSeqCon->m_bSendSeqNum &=
						    EPL_SEQ_NUM_MASK;
						pAsySdoSeqCon->m_bRecSeqNum &=
						    EPL_SEQ_NUM_MASK;
						// send frame
						EplSdoAsySeqSendIntern
						    (pAsySdoSeqCon, 0, NULL,
						     FALSE);

						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateTimeout);
					}

					break;
				}

			default:
				// d.k. do nothing
				break;

			}	// end of switch(Event_p)
			break;
		}

		// wait for Acknowledge (history buffer full)
	case kEplAsySdoStateWaitAck:
		{
			PRINTF0("EplSdoAsySequ: StateWaitAck\n");

			// set timer
			Ret = EplSdoAsySeqSetTimer(pAsySdoSeqCon,
						   EPL_SEQ_DEFAULT_TIMEOUT);

			//TODO: retry of acknowledge
			if (Event_p == kAsySdoSeqEventFrameRec) {
				// check rcon
				switch (pRecFrame_p->
					m_le_bRecSeqNumCon &
					EPL_ASY_SDO_CON_MASK) {
					// close-frome other node
				case 0:
					{
						// return to idle
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateIdle;
						// delete timer
						EplTimeruDeleteTimer
						    (&pAsySdoSeqCon->
						     m_EplTimerHdl);
						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateConClosed);

						break;
					}

					// normal frame
				case 2:
					{
						// should be ack
						// -> change to state kEplAsySdoStateConnected
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateConnected;
						// call Command Layer Cb
						AsySdoSequInstance_g.
						    m_fpSdoComConCb
						    (SdoSeqConHdl,
						     kAsySdoConStateAckReceived);
						// send data to higher layer if needed
						if (uiDataSize_p >
						    EPL_SEQ_HEADER_SIZE) {
							AsySdoSequInstance_g.
							    m_fpSdoComReceiveCb
							    (SdoSeqConHdl,
							     ((tEplAsySdoCom *)
							      & pRecFrame_p->
							      m_le_abSdoSeqPayload),
							     (uiDataSize_p -
							      EPL_SEQ_HEADER_SIZE));
						}
						break;
					}

					// Request Ack or Error Ack
				case 3:
					{
						// -> change to state kEplAsySdoStateConnected
						pAsySdoSeqCon->m_SdoState =
						    kEplAsySdoStateConnected;

						if (pRecFrame_p->m_le_bRecSeqNumCon == pAsySdoSeqCon->m_bRecSeqNum) {	// ack request
							// -> send ack
							// save sequence numbers
							pAsySdoSeqCon->
							    m_bRecSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bRecSeqNumCon);
							pAsySdoSeqCon->
							    m_bSendSeqNum =
							    AmiGetByteFromLe
							    (&pRecFrame_p->
							     m_le_bSendSeqNumCon);

							// create answer own rcon = 2
							pAsySdoSeqCon->
							    m_bRecSeqNum--;

							// check if ack or data-frame
							if (uiDataSize_p >
							    EPL_SEQ_HEADER_SIZE)
							{
								AsySdoSequInstance_g.
								    m_fpSdoComReceiveCb
								    (SdoSeqConHdl,
								     ((tEplAsySdoCom *) & pRecFrame_p->m_le_abSdoSeqPayload), (uiDataSize_p - EPL_SEQ_HEADER_SIZE));
								// call Command Layer Cb
								AsySdoSequInstance_g.
								    m_fpSdoComConCb
								    (SdoSeqConHdl,
								     kAsySdoConStateFrameSended);

							} else {
								Ret =
								    EplSdoAsySeqSendIntern
								    (pAsySdoSeqCon,
								     0, NULL,
								     FALSE);
								if (Ret !=
								    kEplSuccessful)
								{
									goto Exit;
								}
							}

						} else {
							// error ack
							// resend frames from history

							// read frame from history
							Ret =
							    EplSdoAsyReadFromHistory
							    (pAsySdoSeqCon,
							     &pEplFrame,
							     &uiFrameSize,
							     TRUE);
							while ((pEplFrame !=
								NULL)
							       && (uiFrameSize
								   != 0)) {
								// send frame
								Ret =
								    EplSdoAsySeqSendLowerLayer
								    (pAsySdoSeqCon,
								     uiFrameSize,
								     pEplFrame);
								if (Ret !=
								    kEplSuccessful)
								{
									goto Exit;
								}
								// read next frame

								// read frame from history
								Ret =
								    EplSdoAsyReadFromHistory
								    (pAsySdoSeqCon,
								     &pEplFrame,
								     &uiFrameSize,
								     FALSE);
							}	// end of while((pabFrame != NULL)
						}
						break;
					}
				}	// end of switch(pRecFrame_p->m_le_bRecSeqNumCon & EPL_ASY_SDO_CON_MASK)

			} else if (Event_p == kAsySdoSeqEventTimeout) {	// error -> Close
				pAsySdoSeqCon->m_SdoState = kEplAsySdoStateIdle;
				// set rcon and scon to 0
				pAsySdoSeqCon->m_bSendSeqNum &=
				    EPL_SEQ_NUM_MASK;
				pAsySdoSeqCon->m_bRecSeqNum &= EPL_SEQ_NUM_MASK;
				// send frame
				EplSdoAsySeqSendIntern(pAsySdoSeqCon,
						       0, NULL, FALSE);

				// call Command Layer Cb
				AsySdoSequInstance_g.
				    m_fpSdoComConCb(SdoSeqConHdl,
						    kAsySdoConStateTimeout);
			}

			break;
		}

		// unknown state
	default:
		{
			EPL_DBGLVL_SDO_TRACE0
			    ("Error: Unknown State in EplSdoAsySeqProcess\n");

		}
	}			// end of switch(pAsySdoSeqCon->m_SdoState)

      Exit:

#if defined(WIN32) || defined(_WIN32)
	// leave critical section for process function
	LeaveCriticalSection(AsySdoSequInstance_g.m_pCriticalSection);
#endif
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplSdoAsySeqSendIntern
//
// Description: intern function to create and send a frame
//              -> if uiDataSize_p == 0 create a frame with infos from
//                 pAsySdoSeqCon_p
//
//
//
// Parameters:  pAsySdoSeqCon_p = pointer to control structure of the connection
//              uiDataSize_p    = size of data frame to process (can be 0)
//                                  -> without size of sequence header and Asnd header!!!
//              pData_p         = pointer to frame to process (can be NULL)
//              fFrameInHistory = if TRUE frame is saved to history else not
//
//
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoAsySeqSendIntern(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					 unsigned int uiDataSize_p,
					 tEplFrame * pData_p,
					 BOOL fFrameInHistory_p)
{
	tEplKernel Ret;
	u8 abFrame[EPL_SEQ_FRAME_SIZE];
	tEplFrame *pEplFrame;
	unsigned int uiFreeEntries;

	if (pData_p == NULL) {	// set pointer to own frame
		EPL_MEMSET(&abFrame[0], 0x00, sizeof(abFrame));
		pEplFrame = (tEplFrame *) & abFrame[0];
	} else {		// set pointer to frame from calling function
		pEplFrame = pData_p;
	}

	if (fFrameInHistory_p != FALSE) {
		// check if only one free entry in history buffer
		uiFreeEntries =
		    EplSdoAsyGetFreeEntriesFromHistory(pAsySdoSeqCon_p);
		if (uiFreeEntries == 1) {	// request an acknowledge in dataframe
			// own scon = 3
			pAsySdoSeqCon_p->m_bRecSeqNum |= 0x03;
		}
	}
	// fillin header informations
	// set service id sdo
	AmiSetByteToLe(&pEplFrame->m_Data.m_Asnd.m_le_bServiceId, 0x05);
	AmiSetByteToLe(&pEplFrame->m_Data.m_Asnd.m_Payload.m_SdoSequenceFrame.
		       m_le_abReserved, 0x00);
	// set receive sequence number and rcon
	AmiSetByteToLe(&pEplFrame->m_Data.m_Asnd.m_Payload.m_SdoSequenceFrame.
		       m_le_bRecSeqNumCon, pAsySdoSeqCon_p->m_bSendSeqNum);
	// set send sequence number and scon
	AmiSetByteToLe(&pEplFrame->m_Data.m_Asnd.m_Payload.m_SdoSequenceFrame.
		       m_le_bSendSeqNumCon, pAsySdoSeqCon_p->m_bRecSeqNum);

	// add size
	uiDataSize_p += EPL_SEQ_HEADER_SIZE;

	// forward frame to appropriate lower layer
	Ret = EplSdoAsySeqSendLowerLayer(pAsySdoSeqCon_p, uiDataSize_p, pEplFrame);	// pointer to frame

	// check if all allright
	if ((Ret == kEplSuccessful)
	    && (fFrameInHistory_p != FALSE)) {
		// set own scon to 2 if needed
		if ((pAsySdoSeqCon_p->m_bRecSeqNum & 0x03) == 0x03) {
			pAsySdoSeqCon_p->m_bRecSeqNum--;
		}
		// save frame to history
		Ret = EplSdoAsyAddFrameToHistory(pAsySdoSeqCon_p,
						 pEplFrame, uiDataSize_p);
		if (Ret == kEplSdoSeqNoFreeHistory) {	// request Ack needed
			Ret = kEplSdoSeqRequestAckNeeded;
		}

	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplSdoAsySeqSendLowerLayer
//
// Description: intern function to send a previously created frame to lower layer
//
// Parameters:  pAsySdoSeqCon_p = pointer to control structure of the connection
//              uiDataSize_p    = size of data frame to process (can be 0)
//                                  -> without size of Asnd header!!!
//              pData_p         = pointer to frame to process (can be NULL)
//
// Returns:     tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoAsySeqSendLowerLayer(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					     unsigned int uiDataSize_p,
					     tEplFrame * pEplFrame_p)
{
	tEplKernel Ret;

	// call send-function
	// check handle for UDP or Asnd
	if ((pAsySdoSeqCon_p->m_ConHandle & EPL_SDO_ASY_HANDLE_MASK) == EPL_SDO_UDP_HANDLE) {	// send over UDP
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_UDP)) != 0)
		Ret = EplSdoUdpuSendData(pAsySdoSeqCon_p->m_ConHandle, pEplFrame_p,	// pointer to frame
					 uiDataSize_p);
#else
		Ret = kEplSdoSeqUnsupportedProt;
#endif

	} else if ((pAsySdoSeqCon_p->m_ConHandle & EPL_SDO_ASY_HANDLE_MASK) == EPL_SDO_ASND_HANDLE) {	// ASND
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_SDO_ASND)) != 0)
		Ret = EplSdoAsnduSendData(pAsySdoSeqCon_p->m_ConHandle, pEplFrame_p,	// pointer to frame
					  uiDataSize_p);
#else
		Ret = kEplSdoSeqUnsupportedProt;
#endif
	} else {		// error
		Ret = kEplSdoSeqInvalidHdl;
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:        EplSdoAsyReceiveCb
//
// Description:     callback-function for received frames from lower layer
//
//
//
// Parameters:      ConHdl_p        = handle of the connection
//                  pSdoSeqData_p   = pointer to frame
//                  uiDataSize_p    = size of frame
//
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplSdoAsyReceiveCb(tEplSdoConHdl ConHdl_p,
			      tEplAsySdoSeq *pSdoSeqData_p,
			      unsigned int uiDataSize_p)
{
	tEplKernel Ret;
	unsigned int uiCount = 0;
	unsigned int uiFreeEntry = EPL_MAX_SDO_SEQ_CON;
	tEplAsySdoSeqCon *pAsySdoSeqCon;

#if defined(WIN32) || defined(_WIN32)
	// enter  critical section
	EnterCriticalSection(AsySdoSequInstance_g.m_pCriticalSectionReceive);
#endif

	EPL_DBGLVL_SDO_TRACE2("Handle: 0x%x , First Databyte 0x%x\n", ConHdl_p,
			      ((u8 *) pSdoSeqData_p)[0]);

	// search controll structure for this connection
	pAsySdoSeqCon = &AsySdoSequInstance_g.m_AsySdoConnection[uiCount];
	while (uiCount < EPL_MAX_SDO_SEQ_CON) {
		if (pAsySdoSeqCon->m_ConHandle == ConHdl_p) {
			break;
		} else if ((pAsySdoSeqCon->m_ConHandle == 0)
			   && (uiFreeEntry == EPL_MAX_SDO_SEQ_CON)) {
			// free entry
			uiFreeEntry = uiCount;
		}
		uiCount++;
		pAsySdoSeqCon++;
	}

	if (uiCount == EPL_MAX_SDO_SEQ_CON) {	// new connection
		if (uiFreeEntry == EPL_MAX_SDO_SEQ_CON) {
			Ret = kEplSdoSeqNoFreeHandle;
			goto Exit;
		} else {
			pAsySdoSeqCon =
			    &AsySdoSequInstance_g.
			    m_AsySdoConnection[uiFreeEntry];
			// save handle from lower layer
			pAsySdoSeqCon->m_ConHandle = ConHdl_p;
			// increment use counter
			pAsySdoSeqCon->m_uiUseCount++;
			uiCount = uiFreeEntry;
		}
	}
	// call history ack function
	Ret = EplSdoAsyAckFrameToHistory(pAsySdoSeqCon,
					 (AmiGetByteFromLe
					  (&pSdoSeqData_p->
					   m_le_bRecSeqNumCon) &
					  EPL_SEQ_NUM_MASK));
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
#if defined(WIN32) || defined(_WIN32)
	// leave critical section
	LeaveCriticalSection(AsySdoSequInstance_g.m_pCriticalSectionReceive);
#endif

	// call process function with pointer of frame and event kAsySdoSeqEventFrameRec
	Ret = EplSdoAsySeqProcess(uiCount,
				  uiDataSize_p,
				  NULL, pSdoSeqData_p, kAsySdoSeqEventFrameRec);

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:        EplSdoAsyInitHistory
//
// Description:     inti function for history buffer
//
//
//
// Parameters:
//
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoAsyInitHistory(void)
{
	tEplKernel Ret;
	unsigned int uiCount;

	Ret = kEplSuccessful;
	// init m_bFreeEntries in history-buffer
	for (uiCount = 0; uiCount < EPL_MAX_SDO_SEQ_CON; uiCount++) {
		AsySdoSequInstance_g.m_AsySdoConnection[uiCount].
		    m_SdoConHistory.m_bFreeEntries = EPL_SDO_HISTORY_SIZE;
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:        EplSdoAsyAddFrameToHistory
//
// Description:     function to add a frame to the history buffer
//
//
//
// Parameters:      pAsySdoSeqCon_p = pointer to control structure of this connection
//                  pFrame_p        = pointer to frame
//                  uiSize_p        = size of the frame
//                                     -> without size of the ethernet header
//                                        and the asnd header
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoAsyAddFrameToHistory(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					     tEplFrame * pFrame_p,
					     unsigned int uiSize_p)
{
	tEplKernel Ret;
	tEplAsySdoConHistory *pHistory;

	Ret = kEplSuccessful;

	// add frame to history buffer

	// check size
	// $$$ d.k. EPL_SEQ_HISTORY_FRAME_SIZE includes the header size, but uiSize_p does not!!!
	if (uiSize_p > EPL_SEQ_HISTROY_FRAME_SIZE) {
		Ret = kEplSdoSeqFrameSizeError;
		goto Exit;
	}
	// save pointer to history
	pHistory = &pAsySdoSeqCon_p->m_SdoConHistory;

	// check if a free entry is available
	if (pHistory->m_bFreeEntries > 0) {	// write message in free entry
		EPL_MEMCPY(&
			   ((tEplFrame *) pHistory->
			    m_aabHistoryFrame[pHistory->m_bWrite])->
			   m_le_bMessageType, &pFrame_p->m_le_bMessageType,
			   uiSize_p + EPL_ASND_HEADER_SIZE);
		// store size
		pHistory->m_auiFrameSize[pHistory->m_bWrite] = uiSize_p;

		// decremend number of free bufferentries
		pHistory->m_bFreeEntries--;

		// increment writeindex
		pHistory->m_bWrite++;

		// check if write-index run over array-boarder
		if (pHistory->m_bWrite == EPL_SDO_HISTORY_SIZE) {
			pHistory->m_bWrite = 0;
		}

	} else {		// no free entry
		Ret = kEplSdoSeqNoFreeHistory;
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:        EplSdoAsyAckFrameToHistory
//
// Description:     function to delete acknowledged frames fron history buffer
//
//
//
// Parameters:      pAsySdoSeqCon_p = pointer to control structure of this connection
//                  bRecSeqNumber_p = receive sequence number of the received frame
//
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoAsyAckFrameToHistory(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					     u8 bRecSeqNumber_p)
{
	tEplKernel Ret;
	tEplAsySdoConHistory *pHistory;
	u8 bAckIndex;
	u8 bCurrentSeqNum;

	Ret = kEplSuccessful;

	// get pointer to history buffer
	pHistory = &pAsySdoSeqCon_p->m_SdoConHistory;

	// release all acknowledged frames from history buffer

	// check if there are entries in history
	if (pHistory->m_bFreeEntries < EPL_SDO_HISTORY_SIZE) {
		bAckIndex = pHistory->m_bAck;
		do {
			bCurrentSeqNum =
			    (((tEplFrame *) pHistory->
			      m_aabHistoryFrame[bAckIndex])->m_Data.m_Asnd.
			     m_Payload.m_SdoSequenceFrame.
			     m_le_bSendSeqNumCon & EPL_SEQ_NUM_MASK);
			if (((bRecSeqNumber_p -
			      bCurrentSeqNum) & EPL_SEQ_NUM_MASK)
			    < EPL_SEQ_NUM_THRESHOLD) {
				pHistory->m_auiFrameSize[bAckIndex] = 0;
				bAckIndex++;
				pHistory->m_bFreeEntries++;
				if (bAckIndex == EPL_SDO_HISTORY_SIZE) {	// read index run over array-boarder
					bAckIndex = 0;
				}
			} else {	// nothing to do anymore,
				// because any further frame in history has larger sequence
				// number than the acknowledge
				goto Exit;
			}
		}
		while ((((bRecSeqNumber_p - 1 -
			  bCurrentSeqNum) & EPL_SEQ_NUM_MASK)
			< EPL_SEQ_NUM_THRESHOLD)
		       && (pHistory->m_bWrite != bAckIndex));

		// store local read-index to global var
		pHistory->m_bAck = bAckIndex;
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:        EplSdoAsyReadFromHistory
//
// Description:     function to one frame from history
//
//
//
// Parameters:      pAsySdoSeqCon_p = pointer to control structure of this connection
//                  ppFrame_p       = pointer to pointer to the buffer of the stored frame
//                  puiSize_p       = OUT: size of the frame
//                  fInitRead       = bool which indicate a start of retransmission
//                                      -> return last not acknowledged message if TRUE
//
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoAsyReadFromHistory(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
					   tEplFrame ** ppFrame_p,
					   unsigned int *puiSize_p,
					   BOOL fInitRead_p)
{
	tEplKernel Ret;
	tEplAsySdoConHistory *pHistory;

	Ret = kEplSuccessful;

	// read one message from History

	// get pointer to history buffer
	pHistory = &pAsySdoSeqCon_p->m_SdoConHistory;

	// check if init
	if (fInitRead_p != FALSE) {	// initialize read index to the index which shall be acknowledged next
		pHistory->m_bRead = pHistory->m_bAck;
	}
	// check if entries are available for reading
	if ((pHistory->m_bFreeEntries < EPL_SDO_HISTORY_SIZE)
	    && (pHistory->m_bWrite != pHistory->m_bRead)) {
//        PRINTF4("EplSdoAsyReadFromHistory(): init = %d, read = %u, write = %u, ack = %u", (int) fInitRead_p, (u16)pHistory->m_bRead, (u16)pHistory->m_bWrite, (u16)pHistory->m_bAck);
//        PRINTF2(", free entries = %u, next frame size = %u\n", (u16)pHistory->m_bFreeEntries, pHistory->m_auiFrameSize[pHistory->m_bRead]);

		// return pointer to stored frame
		*ppFrame_p =
		    (tEplFrame *) pHistory->m_aabHistoryFrame[pHistory->
							      m_bRead];

		// save size
		*puiSize_p = pHistory->m_auiFrameSize[pHistory->m_bRead];

		pHistory->m_bRead++;
		if (pHistory->m_bRead == EPL_SDO_HISTORY_SIZE) {
			pHistory->m_bRead = 0;
		}

	} else {
//        PRINTF3("EplSdoAsyReadFromHistory(): read = %u, ack = %u, free entries = %u, no frame\n", (u16)pHistory->m_bRead, (u16)pHistory->m_bAck, (u16)pHistory->m_bFreeEntries);

		// no more frames to send
		// return null pointer
		*ppFrame_p = NULL;

		*puiSize_p = 0;
	}

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:        EplSdoAsyGetFreeEntriesFromHistory
//
// Description:     function returns the number of free histroy entries
//
//
//
// Parameters:      pAsySdoSeqCon_p = pointer to control structure of this connection
//
//
// Returns:         unsigned int    = number of free entries
//
//
// State:
//
//---------------------------------------------------------------------------
static unsigned int EplSdoAsyGetFreeEntriesFromHistory(tEplAsySdoSeqCon *
						       pAsySdoSeqCon_p)
{
	unsigned int uiFreeEntries;

	uiFreeEntries =
	    (unsigned int)pAsySdoSeqCon_p->m_SdoConHistory.m_bFreeEntries;

	return uiFreeEntries;
}

//---------------------------------------------------------------------------
//
// Function:        EplSdoAsySeqSetTimer
//
// Description:     function sets or modify timer in timermosule
//
//
//
// Parameters:      pAsySdoSeqCon_p = pointer to control structure of this connection
//                  ulTimeout       = timeout in ms
//
//
// Returns:         unsigned int    = number of free entries
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplSdoAsySeqSetTimer(tEplAsySdoSeqCon * pAsySdoSeqCon_p,
				       unsigned long ulTimeout)
{
	tEplKernel Ret;
	tEplTimerArg TimerArg;

	TimerArg.m_EventSink = kEplEventSinkSdoAsySeq;
	TimerArg.m_ulArg = (unsigned long)pAsySdoSeqCon_p;

	if (pAsySdoSeqCon_p->m_EplTimerHdl == 0) {	// create new timer
		Ret = EplTimeruSetTimerMs(&pAsySdoSeqCon_p->m_EplTimerHdl,
					  ulTimeout, TimerArg);
	} else {		// modify exisiting timer
		Ret = EplTimeruModifyTimerMs(&pAsySdoSeqCon_p->m_EplTimerHdl,
					     ulTimeout, TimerArg);

	}

	return Ret;
}

// EOF
