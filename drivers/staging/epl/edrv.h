/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  interface for ethernetdriver

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

                $RCSfile: edrv.h,v $

                $Author: D.Krueger $

                $Revision: 1.6 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                Dev C++ and GNU-Compiler for m68k

  -------------------------------------------------------------------------

  Revision History:

  2005/08/01 m.b.:   start of implementation

****************************************************************************/

#ifndef _EDRV_H_
#define _EDRV_H_

#include "EplInc.h"
#include "EplFrame.h"

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------
// --------------------------------------------------------------------------
#define MAX_ETH_DATA_SIZE       1500
#define MIN_ETH_DATA_SIZE         46

#define ETH_HDR_OFFSET 	 0	// Ethernet header at the top of the frame
#define ETH_HDR_SIZE	14	// size of Ethernet header
#define MIN_ETH_SIZE     (MIN_ETH_DATA_SIZE + ETH_HDR_SIZE)	// without CRC

#define ETH_CRC_SIZE	 4	// size of Ethernet CRC, i.e. FCS

//---------------------------------------------------------------------------
// types
//---------------------------------------------------------------------------

// position of a buffer in an ethernet-frame
typedef enum {
	kEdrvBufferFirstInFrame = 0x01,	// first data buffer in an ethernet frame
	kEdrvBufferMiddleInFrame = 0x02,	// a middle data buffer in an ethernet frame
	kEdrvBufferLastInFrame = 0x04	// last data buffer in an ethernet frame
} tEdrvBufferInFrame;

// format of a tx-buffer
typedef struct _tEdrvTxBuffer {
	tEplMsgType m_EplMsgType;	// IN: type of EPL message, set by calling function
	unsigned int m_uiTxMsgLen;	// IN: length of message to be send (set for each transmit call)
	// ----------------------
	unsigned int m_uiBufferNumber;	// OUT: number of the buffer, set by ethernetdriver
	u8 *m_pbBuffer;	// OUT: pointer to the buffer, set by ethernetdriver
	tEplNetTime m_NetTime;	// OUT: Timestamp of end of transmission, set by ethernetdriver
	// ----------------------
	unsigned int m_uiMaxBufferLen;	// IN/OUT: maximum length of the buffer
} tEdrvTxBuffer;

// format of a rx-buffer
typedef struct _tEdrvRxBuffer {
	tEdrvBufferInFrame m_BufferInFrame;	// OUT position of received buffer in an ethernet-frame
	unsigned int m_uiRxMsgLen;	// OUT: length of received buffer (without CRC)
	u8 *m_pbBuffer;	// OUT: pointer to the buffer, set by ethernetdriver
	tEplNetTime m_NetTime;	// OUT: Timestamp of end of receiption

} tEdrvRxBuffer;

//typedef void (*tEdrvRxHandler) (u8 bBufferInFrame_p, tBufferDescr * pbBuffer_p);
//typedef void (*tEdrvRxHandler) (u8 bBufferInFrame_p, u8 * pbEthernetData_p, u16 wDataLen_p);
typedef void (*tEdrvRxHandler) (tEdrvRxBuffer * pRxBuffer_p);
typedef void (*tEdrvTxHandler) (tEdrvTxBuffer * pTxBuffer_p);

// format of init structure
typedef struct {
	u8 m_abMyMacAddr[6];	// the own MAC address

//    u8            m_bNoOfRxBuffDescr;     // number of entries in rx bufferdescriptor table
//    tBufferDescr *  m_pRxBuffDescrTable;    // rx bufferdescriptor table
//    u16            m_wRxBufferSize;        // size of the whole rx buffer

	tEdrvRxHandler m_pfnRxHandler;
	tEdrvTxHandler m_pfnTxHandler;

} tEdrvInitParam;

//---------------------------------------------------------------------------
// function prototypes
//---------------------------------------------------------------------------

tEplKernel EdrvInit(tEdrvInitParam * pEdrvInitParam_p);

tEplKernel EdrvShutdown(void);

tEplKernel EdrvDefineRxMacAddrEntry(u8 * pbMacAddr_p);
tEplKernel EdrvUndefineRxMacAddrEntry(u8 * pbMacAddr_p);

//tEplKernel EdrvDefineUnicastEntry     (u8 * pbUCEntry_p);
//tEplKernel EdrvUndfineUnicastEntry    (u8 * pbUCEntry_p);

tEplKernel EdrvAllocTxMsgBuffer(tEdrvTxBuffer * pBuffer_p);
tEplKernel EdrvReleaseTxMsgBuffer(tEdrvTxBuffer * pBuffer_p);

//tEplKernel EdrvWriteMsg               (tBufferDescr * pbBuffer_p);
tEplKernel EdrvSendTxMsg(tEdrvTxBuffer * pBuffer_p);
tEplKernel EdrvTxMsgReady(tEdrvTxBuffer * pBuffer_p);
tEplKernel EdrvTxMsgStart(tEdrvTxBuffer * pBuffer_p);

//tEplKernel EdrvReadMsg                (void);

// interrupt handler called by target specific interrupt handler
void EdrvInterruptHandler(void);

#endif // #ifndef _EDRV_H_
