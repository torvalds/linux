/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for api function of the sdo module

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

                $RCSfile: EplSdo.h,v $

                $Author: D.Krueger $

                $Revision: 1.6 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

 2006/06/26 k.t.:   start of the implementation

****************************************************************************/

#include "EplInc.h"
#include "EplFrame.h"
#include "EplSdoAc.h"

#ifndef _EPLSDO_H_
#define _EPLSDO_H_

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------
// global defines
#ifndef EPL_SDO_MAX_PAYLOAD
#define EPL_SDO_MAX_PAYLOAD     256
#endif

// handle between Protocol Abstraction Layer and asynchronous SDO Sequence Layer
#define EPL_SDO_UDP_HANDLE      0x8000
#define EPL_SDO_ASND_HANDLE     0x4000
#define EPL_SDO_ASY_HANDLE_MASK 0xC000
#define EPL_SDO_ASY_INVALID_HDL 0x3FFF

// handle between  SDO Sequence Layer and sdo command layer
#define EPL_SDO_ASY_HANDLE      0x8000
#define EPL_SDO_PDO_HANDLE      0x4000
#define EPL_SDO_SEQ_HANDLE_MASK 0xC000
#define EPL_SDO_SEQ_INVALID_HDL 0x3FFF

#define EPL_ASND_HEADER_SIZE        4
//#define EPL_SEQ_HEADER_SIZE         4
#define EPL_ETHERNET_HEADER_SIZE    14

#define EPL_SEQ_NUM_MASK            0xFC

// size for send buffer and history
#define EPL_MAX_SDO_FRAME_SIZE      EPL_C_IP_MIN_MTU
// size for receive frame
// -> needed because SND-Kit sends up to 1518 Byte
//    without Sdo-Command: Maximum Segment Size
#define EPL_MAX_SDO_REC_FRAME_SIZE  EPL_C_IP_MAX_MTU

//---------------------------------------------------------------------------
// typedef
//---------------------------------------------------------------------------
// handle between Protocol Abstraction Layer and asynchronuus SDO Sequence Layer
typedef unsigned int tEplSdoConHdl;

// callback function pointer for Protocol Abstraction Layer to call
// asynchronuus SDO Sequence Layer
typedef tEplKernel(*tEplSequLayerReceiveCb) (tEplSdoConHdl ConHdl_p,
					     tEplAsySdoSeq *pSdoSeqData_p,
					     unsigned int uiDataSize_p);

// handle between asynchronuus SDO Sequence Layer and SDO Command layer
typedef unsigned int tEplSdoSeqConHdl;

// callback function pointer for asynchronuus SDO Sequence Layer to call
// SDO Command layer for received data
typedef tEplKernel(* tEplSdoComReceiveCb) (tEplSdoSeqConHdl SdoSeqConHdl_p,
					   tEplAsySdoCom *pAsySdoCom_p,
					   unsigned int uiDataSize_p);

// status of connection
typedef enum {
	kAsySdoConStateConnected = 0x00,
	kAsySdoConStateInitError = 0x01,
	kAsySdoConStateConClosed = 0x02,
	kAsySdoConStateAckReceived = 0x03,
	kAsySdoConStateFrameSended = 0x04,
	kAsySdoConStateTimeout = 0x05
} tEplAsySdoConState;

// callback function pointer for asynchronuus SDO Sequence Layer to call
// SDO Command layer for connection status
typedef tEplKernel(* tEplSdoComConCb) (tEplSdoSeqConHdl SdoSeqConHdl_p,
				       tEplAsySdoConState AsySdoConState_p);

// handle between  SDO Command layer and application
typedef unsigned int tEplSdoComConHdl;

// status of connection
typedef enum {
	kEplSdoComTransferNotActive = 0x00,
	kEplSdoComTransferRunning = 0x01,
	kEplSdoComTransferTxAborted = 0x02,
	kEplSdoComTransferRxAborted = 0x03,
	kEplSdoComTransferFinished = 0x04,
	kEplSdoComTransferLowerLayerAbort = 0x05
} tEplSdoComConState;

// SDO Services and Command-Ids from DS 1.0.0 p.152
typedef enum {
	kEplSdoServiceNIL = 0x00,
	kEplSdoServiceWriteByIndex = 0x01,
	kEplSdoServiceReadByIndex = 0x02
	    //--------------------------------
	    // the following services are optional and
	    // not supported now
/*
    kEplSdoServiceWriteAllByIndex   = 0x03,
    kEplSdoServiceReadAllByIndex    = 0x04,
    kEplSdoServiceWriteByName       = 0x05,
    kEplSdoServiceReadByName        = 0x06,

    kEplSdoServiceFileWrite         = 0x20,
    kEplSdoServiceFileRead          = 0x21,

    kEplSdoServiceWriteMultiByIndex = 0x31,
    kEplSdoServiceReadMultiByIndex  = 0x32,

    kEplSdoServiceMaxSegSize        = 0x70

    // 0x80 - 0xFF manufacturer specific

 */
} tEplSdoServiceType;

// describes if read or write access
typedef enum {
	kEplSdoAccessTypeRead = 0x00,
	kEplSdoAccessTypeWrite = 0x01
} tEplSdoAccessType;

typedef enum {
	kEplSdoTypeAuto = 0x00,
	kEplSdoTypeUdp = 0x01,
	kEplSdoTypeAsnd = 0x02,
	kEplSdoTypePdo = 0x03
} tEplSdoType;

typedef enum {
	kEplSdoTransAuto = 0x00,
	kEplSdoTransExpedited = 0x01,
	kEplSdoTransSegmented = 0x02
} tEplSdoTransType;

// structure to inform application about finish of SDO transfer
typedef struct {
	tEplSdoComConHdl m_SdoComConHdl;
	tEplSdoComConState m_SdoComConState;
	u32 m_dwAbortCode;
	tEplSdoAccessType m_SdoAccessType;
	unsigned int m_uiNodeId;	// NodeId of the target
	unsigned int m_uiTargetIndex;	// index which was accessed
	unsigned int m_uiTargetSubIndex;	// subindex which was accessed
	unsigned int m_uiTransferredByte;	// number of bytes transferred
	void *m_pUserArg;	// user definable argument pointer

} tEplSdoComFinished;

// callback function pointer to inform application about connection
typedef tEplKernel(* tEplSdoFinishedCb) (tEplSdoComFinished *pSdoComFinished_p);

// structure to init SDO transfer to Read or Write by Index
typedef struct {
	tEplSdoComConHdl m_SdoComConHdl;
	unsigned int m_uiIndex;
	unsigned int m_uiSubindex;
	void *m_pData;
	unsigned int m_uiDataSize;
	unsigned int m_uiTimeout;	// not used in this version
	tEplSdoAccessType m_SdoAccessType;
	tEplSdoFinishedCb m_pfnSdoFinishedCb;
	void *m_pUserArg;	// user definable argument pointer

} tEplSdoComTransParamByIndex;

//---------------------------------------------------------------------------
// function prototypes
//---------------------------------------------------------------------------

#endif // #ifndef _EPLSDO_H_
