/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  Ethernet driver for Realtek RTL8139 chips
                except the RTL8139C+, because it has a different
                Tx descriptor handling.

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

                $RCSfile: Edrv8139.c,v $

                $Author: D.Krueger $

                $Revision: 1.10 $  $Date: 2008/11/21 09:00:38 $

                $State: Exp $

                Build Environment:
                Dev C++ and GNU-Compiler for m68k

  -------------------------------------------------------------------------

  Revision History:

  2008/02/05 d.k.:   start of implementation

****************************************************************************/

#include "global.h"
#include "EplInc.h"
#include "edrv.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/version.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <linux/sched.h>
#include <linux/delay.h>

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

// Buffer handling:
// All buffers are created statically (i.e. at compile time resp. at
// initialisation via kmalloc() ) and not dynamically on request (i.e. via
// EdrvAllocTxMsgBuffer().
// EdrvAllocTxMsgBuffer() searches for an unused buffer which is large enough.
// EdrvInit() may allocate some buffers with sizes less than maximum frame
// size (i.e. 1514 bytes), e.g. for SoC, SoA, StatusResponse, IdentResponse,
// NMT requests / commands. The less the size of the buffer the less the
// number of the buffer.

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

#ifndef EDRV_MAX_TX_BUFFERS
#define EDRV_MAX_TX_BUFFERS     20
#endif

#define EDRV_MAX_FRAME_SIZE     0x600

#define EDRV_RX_BUFFER_SIZE     0x8610	// 32 kB + 16 Byte + 1,5 kB (WRAP is enabled)
#define EDRV_RX_BUFFER_LENGTH   (EDRV_RX_BUFFER_SIZE & 0xF800)	// buffer size cut down to 2 kB alignment

#define EDRV_TX_BUFFER_SIZE     (EDRV_MAX_TX_BUFFERS * EDRV_MAX_FRAME_SIZE)	// n * (MTU + 14 + 4)

#define DRV_NAME                "epl"

#define EDRV_REGW_INT_MASK      0x3C	// interrupt mask register
#define EDRV_REGW_INT_STATUS    0x3E	// interrupt status register
#define EDRV_REGW_INT_ROK       0x0001	// Receive OK interrupt
#define EDRV_REGW_INT_RER       0x0002	// Receive error interrupt
#define EDRV_REGW_INT_TOK       0x0004	// Transmit OK interrupt
#define EDRV_REGW_INT_TER       0x0008	// Transmit error interrupt
#define EDRV_REGW_INT_RXOVW     0x0010	// Rx buffer overflow interrupt
#define EDRV_REGW_INT_PUN       0x0020	// Packet underrun/ link change interrupt
#define EDRV_REGW_INT_FOVW      0x0040	// Rx FIFO overflow interrupt
#define EDRV_REGW_INT_LENCHG    0x2000	// Cable length change interrupt
#define EDRV_REGW_INT_TIMEOUT   0x4000	// Time out interrupt
#define EDRV_REGW_INT_SERR      0x8000	// System error interrupt
#define EDRV_REGW_INT_MASK_DEF  (EDRV_REGW_INT_ROK \
                                 | EDRV_REGW_INT_RER \
                                 | EDRV_REGW_INT_TOK \
                                 | EDRV_REGW_INT_TER \
                                 | EDRV_REGW_INT_RXOVW \
                                 | EDRV_REGW_INT_FOVW \
                                 | EDRV_REGW_INT_PUN \
                                 | EDRV_REGW_INT_TIMEOUT \
                                 | EDRV_REGW_INT_SERR)	// default interrupt mask

#define EDRV_REGB_COMMAND       0x37	// command register
#define EDRV_REGB_COMMAND_RST   0x10
#define EDRV_REGB_COMMAND_RE    0x08
#define EDRV_REGB_COMMAND_TE    0x04
#define EDRV_REGB_COMMAND_BUFE  0x01

#define EDRV_REGB_CMD9346       0x50	// 93C46 command register
#define EDRV_REGB_CMD9346_LOCK  0x00	// lock configuration registers
#define EDRV_REGB_CMD9346_UNLOCK 0xC0	// unlock configuration registers

#define EDRV_REGDW_RCR          0x44	// Rx configuration register
#define EDRV_REGDW_RCR_NO_FTH   0x0000E000	// no receive FIFO threshold
#define EDRV_REGDW_RCR_RBLEN32K 0x00001000	// 32 kB receive buffer
#define EDRV_REGDW_RCR_MXDMAUNL 0x00000700	// unlimited maximum DMA burst size
#define EDRV_REGDW_RCR_NOWRAP   0x00000080	// do not wrap frame at end of buffer
#define EDRV_REGDW_RCR_AER      0x00000020	// accept error frames (CRC, alignment, collided)
#define EDRV_REGDW_RCR_AR       0x00000010	// accept runt
#define EDRV_REGDW_RCR_AB       0x00000008	// accept broadcast frames
#define EDRV_REGDW_RCR_AM       0x00000004	// accept multicast frames
#define EDRV_REGDW_RCR_APM      0x00000002	// accept physical match frames
#define EDRV_REGDW_RCR_AAP      0x00000001	// accept all frames
#define EDRV_REGDW_RCR_DEF      (EDRV_REGDW_RCR_NO_FTH \
                                 | EDRV_REGDW_RCR_RBLEN32K \
                                 | EDRV_REGDW_RCR_MXDMAUNL \
                                 | EDRV_REGDW_RCR_NOWRAP \
                                 | EDRV_REGDW_RCR_AB \
                                 | EDRV_REGDW_RCR_AM \
                                 | EDRV_REGDW_RCR_APM)	// default value

#define EDRV_REGDW_TCR          0x40	// Tx configuration register
#define EDRV_REGDW_TCR_VER_MASK 0x7CC00000	// mask for hardware version
#define EDRV_REGDW_TCR_VER_C    0x74000000	// RTL8139C
#define EDRV_REGDW_TCR_VER_D    0x74400000	// RTL8139D
#define EDRV_REGDW_TCR_IFG96    0x03000000	// default interframe gap (960 ns)
#define EDRV_REGDW_TCR_CRC      0x00010000	// disable appending of CRC by the controller
#define EDRV_REGDW_TCR_MXDMAUNL 0x00000700	// maximum DMA burst size of 2048 b
#define EDRV_REGDW_TCR_TXRETRY  0x00000000	// 16 retries
#define EDRV_REGDW_TCR_DEF      (EDRV_REGDW_TCR_IFG96 \
                                 | EDRV_REGDW_TCR_MXDMAUNL \
                                 | EDRV_REGDW_TCR_TXRETRY)

#define EDRV_REGW_MULINT        0x5C	// multiple interrupt select register

#define EDRV_REGDW_MPC          0x4C	// missed packet counter register

#define EDRV_REGDW_TSAD0        0x20	// Transmit start address of descriptor 0
#define EDRV_REGDW_TSAD1        0x24	// Transmit start address of descriptor 1
#define EDRV_REGDW_TSAD2        0x28	// Transmit start address of descriptor 2
#define EDRV_REGDW_TSAD3        0x2C	// Transmit start address of descriptor 3
#define EDRV_REGDW_TSD0         0x10	// Transmit status of descriptor 0
#define EDRV_REGDW_TSD_CRS      0x80000000	// Carrier sense lost
#define EDRV_REGDW_TSD_TABT     0x40000000	// Transmit Abort
#define EDRV_REGDW_TSD_OWC      0x20000000	// Out of window collision
#define EDRV_REGDW_TSD_TXTH_DEF 0x00020000	// Transmit FIFO threshold of 64 bytes
#define EDRV_REGDW_TSD_TOK      0x00008000	// Transmit OK
#define EDRV_REGDW_TSD_TUN      0x00004000	// Transmit FIFO underrun
#define EDRV_REGDW_TSD_OWN      0x00002000	// Owner

#define EDRV_REGDW_RBSTART      0x30	// Receive buffer start address

#define EDRV_REGW_CAPR          0x38	// Current address of packet read

#define EDRV_REGDW_IDR0         0x00	// ID register 0
#define EDRV_REGDW_IDR4         0x04	// ID register 4

#define EDRV_REGDW_MAR0         0x08	// Multicast address register 0
#define EDRV_REGDW_MAR4         0x0C	// Multicast address register 4

// defines for the status word in the receive buffer
#define EDRV_RXSTAT_MAR         0x8000	// Multicast address received
#define EDRV_RXSTAT_PAM         0x4000	// Physical address matched
#define EDRV_RXSTAT_BAR         0x2000	// Broadcast address received
#define EDRV_RXSTAT_ISE         0x0020	// Invalid symbol error
#define EDRV_RXSTAT_RUNT        0x0010	// Runt packet received
#define EDRV_RXSTAT_LONG        0x0008	// Long packet
#define EDRV_RXSTAT_CRC         0x0004	// CRC error
#define EDRV_RXSTAT_FAE         0x0002	// Frame alignment error
#define EDRV_RXSTAT_ROK         0x0001	// Receive OK

#define EDRV_REGDW_WRITE(dwReg, dwVal)  writel(dwVal, EdrvInstance_l.m_pIoAddr + dwReg)
#define EDRV_REGW_WRITE(dwReg, wVal)    writew(wVal, EdrvInstance_l.m_pIoAddr + dwReg)
#define EDRV_REGB_WRITE(dwReg, bVal)    writeb(bVal, EdrvInstance_l.m_pIoAddr + dwReg)
#define EDRV_REGDW_READ(dwReg)          readl(EdrvInstance_l.m_pIoAddr + dwReg)
#define EDRV_REGW_READ(dwReg)           readw(EdrvInstance_l.m_pIoAddr + dwReg)
#define EDRV_REGB_READ(dwReg)           readb(EdrvInstance_l.m_pIoAddr + dwReg)

// TracePoint support for realtime-debugging
#ifdef _DBG_TRACE_POINTS_
void PUBLIC TgtDbgSignalTracePoint(BYTE bTracePointNumber_p);
void PUBLIC TgtDbgPostTraceValue(DWORD dwTraceValue_p);
#define TGT_DBG_SIGNAL_TRACE_POINT(p)   TgtDbgSignalTracePoint(p)
#define TGT_DBG_POST_TRACE_VALUE(v)     TgtDbgPostTraceValue(v)
#else
#define TGT_DBG_SIGNAL_TRACE_POINT(p)
#define TGT_DBG_POST_TRACE_VALUE(v)
#endif

#define EDRV_COUNT_SEND                 TGT_DBG_SIGNAL_TRACE_POINT(2)
#define EDRV_COUNT_TIMEOUT              TGT_DBG_SIGNAL_TRACE_POINT(3)
#define EDRV_COUNT_PCI_ERR              TGT_DBG_SIGNAL_TRACE_POINT(4)
#define EDRV_COUNT_TX                   TGT_DBG_SIGNAL_TRACE_POINT(5)
#define EDRV_COUNT_RX                   TGT_DBG_SIGNAL_TRACE_POINT(6)
#define EDRV_COUNT_LATECOLLISION        TGT_DBG_SIGNAL_TRACE_POINT(10)
#define EDRV_COUNT_TX_COL_RL            TGT_DBG_SIGNAL_TRACE_POINT(11)
#define EDRV_COUNT_TX_FUN               TGT_DBG_SIGNAL_TRACE_POINT(12)
#define EDRV_COUNT_TX_ERR               TGT_DBG_SIGNAL_TRACE_POINT(13)
#define EDRV_COUNT_RX_CRC               TGT_DBG_SIGNAL_TRACE_POINT(14)
#define EDRV_COUNT_RX_ERR               TGT_DBG_SIGNAL_TRACE_POINT(15)
#define EDRV_COUNT_RX_FOVW              TGT_DBG_SIGNAL_TRACE_POINT(16)
#define EDRV_COUNT_RX_PUN               TGT_DBG_SIGNAL_TRACE_POINT(17)
#define EDRV_COUNT_RX_FAE               TGT_DBG_SIGNAL_TRACE_POINT(18)
#define EDRV_COUNT_RX_OVW               TGT_DBG_SIGNAL_TRACE_POINT(19)

#define EDRV_TRACE_CAPR(x)              TGT_DBG_POST_TRACE_VALUE(((x) & 0xFFFF) | 0x06000000)
#define EDRV_TRACE_RX_CRC(x)            TGT_DBG_POST_TRACE_VALUE(((x) & 0xFFFF) | 0x0E000000)
#define EDRV_TRACE_RX_ERR(x)            TGT_DBG_POST_TRACE_VALUE(((x) & 0xFFFF) | 0x0F000000)
#define EDRV_TRACE_RX_PUN(x)            TGT_DBG_POST_TRACE_VALUE(((x) & 0xFFFF) | 0x11000000)
#define EDRV_TRACE(x)                   TGT_DBG_POST_TRACE_VALUE(((x) & 0xFFFF0000) | 0x0000FEC0)

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------
/*
typedef struct
{
    BOOL            m_fUsed;
    unsigned int    m_uiSize;
    MCD_bufDescFec *m_pBufDescr;

} tEdrvTxBufferIntern;
*/

// Private structure
typedef struct {
	struct pci_dev *m_pPciDev;	// pointer to PCI device structure
	void *m_pIoAddr;	// pointer to register space of Ethernet controller
	BYTE *m_pbRxBuf;	// pointer to Rx buffer
	dma_addr_t m_pRxBufDma;
	BYTE *m_pbTxBuf;	// pointer to Tx buffer
	dma_addr_t m_pTxBufDma;
	BOOL m_afTxBufUsed[EDRV_MAX_TX_BUFFERS];
	unsigned int m_uiCurTxDesc;

	tEdrvInitParam m_InitParam;
	tEdrvTxBuffer *m_pLastTransmittedTxBuffer;

} tEdrvInstance;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static int EdrvInitOne(struct pci_dev *pPciDev,
		       const struct pci_device_id *pId);

static void EdrvRemoveOne(struct pci_dev *pPciDev);

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------
// buffers and buffer descriptors and pointers

static struct pci_device_id aEdrvPciTbl[] = {
	{0x10ec, 0x8139, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, aEdrvPciTbl);

static tEdrvInstance EdrvInstance_l;

static struct pci_driver EdrvDriver = {
	.name = DRV_NAME,
	.id_table = aEdrvPciTbl,
	.probe = EdrvInitOne,
	.remove = EdrvRemoveOne,
};

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  <edrv>                                              */
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

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static BYTE EdrvCalcHash(BYTE * pbMAC_p);

//---------------------------------------------------------------------------
//
// Function:    EdrvInit
//
// Description: function for init of the Ethernet controller
//
// Parameters:  pEdrvInitParam_p    = pointer to struct including the init-parameters
//
// Returns:     Errorcode           = kEplSuccessful
//                                  = kEplNoResource
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EdrvInit(tEdrvInitParam * pEdrvInitParam_p)
{
	tEplKernel Ret;
	int iResult;

	Ret = kEplSuccessful;

	// clear instance structure
	EPL_MEMSET(&EdrvInstance_l, 0, sizeof(EdrvInstance_l));

	// save the init data
	EdrvInstance_l.m_InitParam = *pEdrvInitParam_p;

	// register PCI driver
	iResult = pci_register_driver(&EdrvDriver);
	if (iResult != 0) {
		printk("%s pci_register_driver failed with %d\n", __func__,
		       iResult);
		Ret = kEplNoResource;
		goto Exit;
	}

	if (EdrvInstance_l.m_pPciDev == NULL) {
		printk("%s m_pPciDev=NULL\n", __func__);
		Ret = kEplNoResource;
		goto Exit;
	}
	// read MAC address from controller
	printk("%s local MAC = ", __func__);
	for (iResult = 0; iResult < 6; iResult++) {
		pEdrvInitParam_p->m_abMyMacAddr[iResult] =
		    EDRV_REGB_READ((EDRV_REGDW_IDR0 + iResult));
		printk("%02X ",
		       (unsigned int)pEdrvInitParam_p->m_abMyMacAddr[iResult]);
	}
	printk("\n");

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EdrvShutdown
//
// Description: Shutdown the Ethernet controller
//
// Parameters:  void
//
// Returns:     Errorcode   = kEplSuccessful
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EdrvShutdown(void)
{

	// unregister PCI driver
	printk("%s calling pci_unregister_driver()\n", __func__);
	pci_unregister_driver(&EdrvDriver);

	return kEplSuccessful;
}

//---------------------------------------------------------------------------
//
// Function:    EdrvDefineRxMacAddrEntry
//
// Description: Set a multicast entry into the Ethernet controller
//
// Parameters:  pbMacAddr_p     = pointer to multicast entry to set
//
// Returns:     Errorcode       = kEplSuccessful
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EdrvDefineRxMacAddrEntry(BYTE * pbMacAddr_p)
{
	tEplKernel Ret = kEplSuccessful;
	DWORD dwData;
	BYTE bHash;

	bHash = EdrvCalcHash(pbMacAddr_p);
/*
    dwData = ether_crc(6, pbMacAddr_p);

    printk("EdrvDefineRxMacAddrEntry('%02X:%02X:%02X:%02X:%02X:%02X') hash = %u / %u  ether_crc = 0x%08lX\n",
        (WORD) pbMacAddr_p[0], (WORD) pbMacAddr_p[1], (WORD) pbMacAddr_p[2],
        (WORD) pbMacAddr_p[3], (WORD) pbMacAddr_p[4], (WORD) pbMacAddr_p[5],
        (WORD) bHash, (WORD) (dwData >> 26), dwData);
*/
	if (bHash > 31) {
		dwData = EDRV_REGDW_READ(EDRV_REGDW_MAR4);
		dwData |= 1 << (bHash - 32);
		EDRV_REGDW_WRITE(EDRV_REGDW_MAR4, dwData);
	} else {
		dwData = EDRV_REGDW_READ(EDRV_REGDW_MAR0);
		dwData |= 1 << bHash;
		EDRV_REGDW_WRITE(EDRV_REGDW_MAR0, dwData);
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EdrvUndefineRxMacAddrEntry
//
// Description: Reset a multicast entry in the Ethernet controller
//
// Parameters:  pbMacAddr_p     = pointer to multicast entry to reset
//
// Returns:     Errorcode       = kEplSuccessful
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EdrvUndefineRxMacAddrEntry(BYTE * pbMacAddr_p)
{
	tEplKernel Ret = kEplSuccessful;
	DWORD dwData;
	BYTE bHash;

	bHash = EdrvCalcHash(pbMacAddr_p);

	if (bHash > 31) {
		dwData = EDRV_REGDW_READ(EDRV_REGDW_MAR4);
		dwData &= ~(1 << (bHash - 32));
		EDRV_REGDW_WRITE(EDRV_REGDW_MAR4, dwData);
	} else {
		dwData = EDRV_REGDW_READ(EDRV_REGDW_MAR0);
		dwData &= ~(1 << bHash);
		EDRV_REGDW_WRITE(EDRV_REGDW_MAR0, dwData);
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EdrvAllocTxMsgBuffer
//
// Description: Register a Tx-Buffer
//
// Parameters:  pBuffer_p   = pointer to Buffer structure
//
// Returns:     Errorcode   = kEplSuccessful
//                          = kEplEdrvNoFreeBufEntry
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EdrvAllocTxMsgBuffer(tEdrvTxBuffer * pBuffer_p)
{
	tEplKernel Ret = kEplSuccessful;
	DWORD i;

	if (pBuffer_p->m_uiMaxBufferLen > EDRV_MAX_FRAME_SIZE) {
		Ret = kEplEdrvNoFreeBufEntry;
		goto Exit;
	}
	// search a free Tx buffer with appropriate size
	for (i = 0; i < EDRV_MAX_TX_BUFFERS; i++) {
		if (EdrvInstance_l.m_afTxBufUsed[i] == FALSE) {
			// free channel found
			EdrvInstance_l.m_afTxBufUsed[i] = TRUE;
			pBuffer_p->m_uiBufferNumber = i;
			pBuffer_p->m_pbBuffer =
			    EdrvInstance_l.m_pbTxBuf +
			    (i * EDRV_MAX_FRAME_SIZE);
			pBuffer_p->m_uiMaxBufferLen = EDRV_MAX_FRAME_SIZE;
			break;
		}
	}
	if (i >= EDRV_MAX_TX_BUFFERS) {
		Ret = kEplEdrvNoFreeBufEntry;
		goto Exit;
	}

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EdrvReleaseTxMsgBuffer
//
// Description: Register a Tx-Buffer
//
// Parameters:  pBuffer_p   = pointer to Buffer structure
//
// Returns:     Errorcode   = kEplSuccessful
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EdrvReleaseTxMsgBuffer(tEdrvTxBuffer * pBuffer_p)
{
	unsigned int uiBufferNumber;

	uiBufferNumber = pBuffer_p->m_uiBufferNumber;

	if (uiBufferNumber < EDRV_MAX_TX_BUFFERS) {
		EdrvInstance_l.m_afTxBufUsed[uiBufferNumber] = FALSE;
	}

	return kEplSuccessful;

}

//---------------------------------------------------------------------------
//
// Function:    EdrvSendTxMsg
//
// Description: immediately starts the transmission of the buffer
//
// Parameters:  pBuffer_p   = buffer descriptor to transmit
//
// Returns:     Errorcode   = kEplSuccessful
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EdrvSendTxMsg(tEdrvTxBuffer * pBuffer_p)
{
	tEplKernel Ret = kEplSuccessful;
	unsigned int uiBufferNumber;
	DWORD dwTemp;

	uiBufferNumber = pBuffer_p->m_uiBufferNumber;

	if ((uiBufferNumber >= EDRV_MAX_TX_BUFFERS)
	    || (EdrvInstance_l.m_afTxBufUsed[uiBufferNumber] == FALSE)) {
		Ret = kEplEdrvBufNotExisting;
		goto Exit;
	}

	if (EdrvInstance_l.m_pLastTransmittedTxBuffer != NULL) {	// transmission is already active
		Ret = kEplInvalidOperation;
		dwTemp =
		    EDRV_REGDW_READ((EDRV_REGDW_TSD0 +
				     (EdrvInstance_l.m_uiCurTxDesc *
				      sizeof(DWORD))));
		printk("%s InvOp TSD%u = 0x%08lX", __func__,
		       EdrvInstance_l.m_uiCurTxDesc, dwTemp);
		printk("  Cmd = 0x%02X\n",
		       (WORD) EDRV_REGB_READ(EDRV_REGB_COMMAND));
		goto Exit;
	}
	// save pointer to buffer structure for TxHandler
	EdrvInstance_l.m_pLastTransmittedTxBuffer = pBuffer_p;

	EDRV_COUNT_SEND;

	// pad with zeros if necessary, because controller does not do it
	if (pBuffer_p->m_uiTxMsgLen < MIN_ETH_SIZE) {
		EPL_MEMSET(pBuffer_p->m_pbBuffer + pBuffer_p->m_uiTxMsgLen, 0,
			   MIN_ETH_SIZE - pBuffer_p->m_uiTxMsgLen);
		pBuffer_p->m_uiTxMsgLen = MIN_ETH_SIZE;
	}
	// set DMA address of buffer
	EDRV_REGDW_WRITE((EDRV_REGDW_TSAD0 +
			  (EdrvInstance_l.m_uiCurTxDesc * sizeof(DWORD))),
			 (EdrvInstance_l.m_pTxBufDma +
			  (uiBufferNumber * EDRV_MAX_FRAME_SIZE)));
	dwTemp =
	    EDRV_REGDW_READ((EDRV_REGDW_TSAD0 +
			     (EdrvInstance_l.m_uiCurTxDesc * sizeof(DWORD))));
//    printk("%s TSAD%u = 0x%08lX", __func__, EdrvInstance_l.m_uiCurTxDesc, dwTemp);

	// start transmission
	EDRV_REGDW_WRITE((EDRV_REGDW_TSD0 +
			  (EdrvInstance_l.m_uiCurTxDesc * sizeof(DWORD))),
			 (EDRV_REGDW_TSD_TXTH_DEF | pBuffer_p->m_uiTxMsgLen));
	dwTemp =
	    EDRV_REGDW_READ((EDRV_REGDW_TSD0 +
			     (EdrvInstance_l.m_uiCurTxDesc * sizeof(DWORD))));
//    printk(" TSD%u = 0x%08lX / 0x%08lX\n", EdrvInstance_l.m_uiCurTxDesc, dwTemp, (DWORD)(EDRV_REGDW_TSD_TXTH_DEF | pBuffer_p->m_uiTxMsgLen));

      Exit:
	return Ret;
}

#if 0
//---------------------------------------------------------------------------
//
// Function:    EdrvTxMsgReady
//
// Description: starts copying the buffer to the ethernet controller's FIFO
//
// Parameters:  pbBuffer_p - bufferdescriptor to transmit
//
// Returns:     Errorcode - kEplSuccessful
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EdrvTxMsgReady(tEdrvTxBuffer * pBuffer_p)
{
	tEplKernel Ret = kEplSuccessful;
	unsigned int uiBufferNumber;

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EdrvTxMsgStart
//
// Description: starts transmission of the ethernet controller's FIFO
//
// Parameters:  pbBuffer_p - bufferdescriptor to transmit
//
// Returns:     Errorcode - kEplSuccessful
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EdrvTxMsgStart(tEdrvTxBuffer * pBuffer_p)
{
	tEplKernel Ret = kEplSuccessful;

	return Ret;
}
#endif

//---------------------------------------------------------------------------
//
// Function:    EdrvReinitRx
//
// Description: reinitialize the Rx process, because of error
//
// Parameters:  void
//
// Returns:     void
//
// State:
//
//---------------------------------------------------------------------------
static void EdrvReinitRx(void)
{
	BYTE bCmd;

	// simply switch off and on the receiver
	// this will reset the CAPR register
	bCmd = EDRV_REGB_READ(EDRV_REGB_COMMAND);
	EDRV_REGB_WRITE(EDRV_REGB_COMMAND, (bCmd & ~EDRV_REGB_COMMAND_RE));
	EDRV_REGB_WRITE(EDRV_REGB_COMMAND, bCmd);

	// set receive configuration register
	EDRV_REGDW_WRITE(EDRV_REGDW_RCR, EDRV_REGDW_RCR_DEF);
}

//---------------------------------------------------------------------------
//
// Function:     EdrvInterruptHandler
//
// Description:  interrupt handler
//
// Parameters:   void
//
// Returns:      void
//
// State:
//
//---------------------------------------------------------------------------
#if 0
void EdrvInterruptHandler(void)
{
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
static int TgtEthIsr(int nIrqNum_p, void *ppDevInstData_p)
#else
static int TgtEthIsr(int nIrqNum_p, void *ppDevInstData_p,
		     struct pt_regs *ptRegs_p)
#endif
{
//    EdrvInterruptHandler();
	tEdrvRxBuffer RxBuffer;
	tEdrvTxBuffer *pTxBuffer;
	WORD wStatus;
	DWORD dwTxStatus;
	DWORD dwRxStatus;
	WORD wCurRx;
	BYTE *pbRxBuf;
	unsigned int uiLength;
	int iHandled = IRQ_HANDLED;

//    printk("¤");

	// read the interrupt status
	wStatus = EDRV_REGW_READ(EDRV_REGW_INT_STATUS);

	// acknowledge the interrupts
	EDRV_REGW_WRITE(EDRV_REGW_INT_STATUS, wStatus);

	if (wStatus == 0) {
		iHandled = IRQ_NONE;
		goto Exit;
	}
	// process tasks
	if ((wStatus & (EDRV_REGW_INT_TER | EDRV_REGW_INT_TOK)) != 0) {	// transmit interrupt

		if (EdrvInstance_l.m_pbTxBuf == NULL) {
			printk("%s Tx buffers currently not allocated\n",
			       __func__);
			goto Exit;
		}
		// read transmit status
		dwTxStatus =
		    EDRV_REGDW_READ((EDRV_REGDW_TSD0 +
				     (EdrvInstance_l.m_uiCurTxDesc *
				      sizeof(DWORD))));
		if ((dwTxStatus & (EDRV_REGDW_TSD_TOK | EDRV_REGDW_TSD_TABT | EDRV_REGDW_TSD_TUN)) != 0) {	// transmit finished
			EdrvInstance_l.m_uiCurTxDesc =
			    (EdrvInstance_l.m_uiCurTxDesc + 1) & 0x03;
			pTxBuffer = EdrvInstance_l.m_pLastTransmittedTxBuffer;
			EdrvInstance_l.m_pLastTransmittedTxBuffer = NULL;

			if ((dwTxStatus & EDRV_REGDW_TSD_TOK) != 0) {
				EDRV_COUNT_TX;
			} else if ((dwTxStatus & EDRV_REGDW_TSD_TUN) != 0) {
				EDRV_COUNT_TX_FUN;
			} else {	// assume EDRV_REGDW_TSD_TABT
				EDRV_COUNT_TX_COL_RL;
			}

//            printk("T");
			if (pTxBuffer != NULL) {
				// call Tx handler of Data link layer
				EdrvInstance_l.m_InitParam.
				    m_pfnTxHandler(pTxBuffer);
			}
		} else {
			EDRV_COUNT_TX_ERR;
		}
	}

	if ((wStatus & (EDRV_REGW_INT_RER | EDRV_REGW_INT_FOVW | EDRV_REGW_INT_RXOVW | EDRV_REGW_INT_PUN)) != 0) {	// receive error interrupt

		if ((wStatus & EDRV_REGW_INT_FOVW) != 0) {
			EDRV_COUNT_RX_FOVW;
		} else if ((wStatus & EDRV_REGW_INT_RXOVW) != 0) {
			EDRV_COUNT_RX_OVW;
		} else if ((wStatus & EDRV_REGW_INT_PUN) != 0) {	// Packet underrun
			EDRV_TRACE_RX_PUN(wStatus);
			EDRV_COUNT_RX_PUN;
		} else {	/*if ((wStatus & EDRV_REGW_INT_RER) != 0) */

			EDRV_TRACE_RX_ERR(wStatus);
			EDRV_COUNT_RX_ERR;
		}

		// reinitialize Rx process
		EdrvReinitRx();
	}

	if ((wStatus & EDRV_REGW_INT_ROK) != 0) {	// receive interrupt

		if (EdrvInstance_l.m_pbRxBuf == NULL) {
			printk("%s Rx buffers currently not allocated\n",
			       __func__);
			goto Exit;
		}
		// read current offset in receive buffer
		wCurRx =
		    (EDRV_REGW_READ(EDRV_REGW_CAPR) +
		     0x10) % EDRV_RX_BUFFER_LENGTH;

		while ((EDRV_REGB_READ(EDRV_REGB_COMMAND) & EDRV_REGB_COMMAND_BUFE) == 0) {	// frame available

			// calculate pointer to current frame in receive buffer
			pbRxBuf = EdrvInstance_l.m_pbRxBuf + wCurRx;

			// read receive status DWORD
			dwRxStatus = le32_to_cpu(*((DWORD *) pbRxBuf));

			// calculate length of received frame
			uiLength = dwRxStatus >> 16;

			if (uiLength == 0xFFF0) {	// frame is unfinished (maybe early Rx interrupt is active)
				break;
			}

			if ((dwRxStatus & EDRV_RXSTAT_ROK) == 0) {	// error occured while receiving this frame
				// ignore it
				if ((dwRxStatus & EDRV_RXSTAT_FAE) != 0) {
					EDRV_COUNT_RX_FAE;
				} else if ((dwRxStatus & EDRV_RXSTAT_CRC) != 0) {
					EDRV_TRACE_RX_CRC(dwRxStatus);
					EDRV_COUNT_RX_CRC;
				} else {
					EDRV_TRACE_RX_ERR(dwRxStatus);
					EDRV_COUNT_RX_ERR;
				}

				// reinitialize Rx process
				EdrvReinitRx();

				break;
			} else {	// frame is OK
				RxBuffer.m_BufferInFrame =
				    kEdrvBufferLastInFrame;
				RxBuffer.m_uiRxMsgLen = uiLength - ETH_CRC_SIZE;
				RxBuffer.m_pbBuffer =
				    pbRxBuf + sizeof(dwRxStatus);

//                printk("R");
				EDRV_COUNT_RX;

				// call Rx handler of Data link layer
				EdrvInstance_l.m_InitParam.
				    m_pfnRxHandler(&RxBuffer);
			}

			// calulate new offset (DWORD aligned)
			wCurRx =
			    (WORD) ((wCurRx + uiLength + sizeof(dwRxStatus) +
				     3) & ~0x3);
			EDRV_TRACE_CAPR(wCurRx - 0x10);
			EDRV_REGW_WRITE(EDRV_REGW_CAPR, wCurRx - 0x10);

			// reread current offset in receive buffer
			wCurRx =
			    (EDRV_REGW_READ(EDRV_REGW_CAPR) +
			     0x10) % EDRV_RX_BUFFER_LENGTH;

		}
	}

	if ((wStatus & EDRV_REGW_INT_SERR) != 0) {	// PCI error
		EDRV_COUNT_PCI_ERR;
	}

	if ((wStatus & EDRV_REGW_INT_TIMEOUT) != 0) {	// Timeout
		EDRV_COUNT_TIMEOUT;
	}

      Exit:
	return iHandled;
}

//---------------------------------------------------------------------------
//
// Function:    EdrvInitOne
//
// Description: initializes one PCI device
//
// Parameters:  pPciDev             = pointer to corresponding PCI device structure
//              pId                 = PCI device ID
//
// Returns:     (int)               = error code
//
// State:
//
//---------------------------------------------------------------------------

static int EdrvInitOne(struct pci_dev *pPciDev, const struct pci_device_id *pId)
{
	int iResult = 0;
	DWORD dwTemp;

	if (EdrvInstance_l.m_pPciDev != NULL) {	// Edrv is already connected to a PCI device
		printk("%s device %s discarded\n", __func__,
		       pci_name(pPciDev));
		iResult = -ENODEV;
		goto Exit;
	}

	if (pPciDev->revision >= 0x20) {
		printk
		    ("%s device %s is an enhanced 8139C+ version, which is not supported\n",
		     __func__, pci_name(pPciDev));
		iResult = -ENODEV;
		goto Exit;
	}

	EdrvInstance_l.m_pPciDev = pPciDev;

	// enable device
	printk("%s enable device\n", __func__);
	iResult = pci_enable_device(pPciDev);
	if (iResult != 0) {
		goto Exit;
	}

	if ((pci_resource_flags(pPciDev, 1) & IORESOURCE_MEM) == 0) {
		iResult = -ENODEV;
		goto Exit;
	}

	printk("%s request regions\n", __func__);
	iResult = pci_request_regions(pPciDev, DRV_NAME);
	if (iResult != 0) {
		goto Exit;
	}

	printk("%s ioremap\n", __func__);
	EdrvInstance_l.m_pIoAddr =
	    ioremap(pci_resource_start(pPciDev, 1),
		    pci_resource_len(pPciDev, 1));
	if (EdrvInstance_l.m_pIoAddr == NULL) {	// remap of controller's register space failed
		iResult = -EIO;
		goto Exit;
	}
	// enable PCI busmaster
	printk("%s enable busmaster\n", __func__);
	pci_set_master(pPciDev);

	// reset controller
	printk("%s reset controller\n", __func__);
	EDRV_REGB_WRITE(EDRV_REGB_COMMAND, EDRV_REGB_COMMAND_RST);

	// wait until reset has finished
	for (iResult = 500; iResult > 0; iResult--) {
		if ((EDRV_REGB_READ(EDRV_REGB_COMMAND) & EDRV_REGB_COMMAND_RST)
		    == 0) {
			break;
		}

		schedule_timeout(10);
	}

	// check hardware version, i.e. chip ID
	dwTemp = EDRV_REGDW_READ(EDRV_REGDW_TCR);
	if (((dwTemp & EDRV_REGDW_TCR_VER_MASK) != EDRV_REGDW_TCR_VER_C)
	    && ((dwTemp & EDRV_REGDW_TCR_VER_MASK) != EDRV_REGDW_TCR_VER_D)) {	// unsupported chip
		printk("%s Unsupported chip! TCR = 0x%08lX\n", __func__,
		       dwTemp);
		iResult = -ENODEV;
		goto Exit;
	}
	// disable interrupts
	printk("%s disable interrupts\n", __func__);
	EDRV_REGW_WRITE(EDRV_REGW_INT_MASK, 0);
	// acknowledge all pending interrupts
	EDRV_REGW_WRITE(EDRV_REGW_INT_STATUS,
			EDRV_REGW_READ(EDRV_REGW_INT_STATUS));

	// install interrupt handler
	printk("%s install interrupt handler\n", __func__);
	iResult =
	    request_irq(pPciDev->irq, TgtEthIsr, IRQF_SHARED,
			DRV_NAME /*pPciDev->dev.name */ , pPciDev);
	if (iResult != 0) {
		goto Exit;
	}

/*
    // unlock configuration registers
    printk("%s unlock configuration registers\n", __func__);
    EDRV_REGB_WRITE(EDRV_REGB_CMD9346, EDRV_REGB_CMD9346_UNLOCK);

    // check if user specified a MAC address
    printk("%s check specified MAC address\n", __func__);
    for (iResult = 0; iResult < 6; iResult++)
    {
        if (EdrvInstance_l.m_InitParam.m_abMyMacAddr[iResult] != 0)
        {
            printk("%s set local MAC address\n", __func__);
            // write this MAC address to controller
            EDRV_REGDW_WRITE(EDRV_REGDW_IDR0,
                le32_to_cpu(*((DWORD*)&EdrvInstance_l.m_InitParam.m_abMyMacAddr[0])));
            dwTemp = EDRV_REGDW_READ(EDRV_REGDW_IDR0);

            EDRV_REGDW_WRITE(EDRV_REGDW_IDR4,
                le32_to_cpu(*((DWORD*)&EdrvInstance_l.m_InitParam.m_abMyMacAddr[4])));
            dwTemp = EDRV_REGDW_READ(EDRV_REGDW_IDR4);
            break;
        }
    }
    iResult = 0;

    // lock configuration registers
    EDRV_REGB_WRITE(EDRV_REGB_CMD9346, EDRV_REGB_CMD9346_LOCK);
*/

	// allocate buffers
	printk("%s allocate buffers\n", __func__);
	EdrvInstance_l.m_pbTxBuf =
	    pci_alloc_consistent(pPciDev, EDRV_TX_BUFFER_SIZE,
				 &EdrvInstance_l.m_pTxBufDma);
	if (EdrvInstance_l.m_pbTxBuf == NULL) {
		iResult = -ENOMEM;
		goto Exit;
	}

	EdrvInstance_l.m_pbRxBuf =
	    pci_alloc_consistent(pPciDev, EDRV_RX_BUFFER_SIZE,
				 &EdrvInstance_l.m_pRxBufDma);
	if (EdrvInstance_l.m_pbRxBuf == NULL) {
		iResult = -ENOMEM;
		goto Exit;
	}
	// reset pointers for Tx buffers
	printk("%s reset pointers fo Tx buffers\n", __func__);
	EDRV_REGDW_WRITE(EDRV_REGDW_TSAD0, 0);
	dwTemp = EDRV_REGDW_READ(EDRV_REGDW_TSAD0);
	EDRV_REGDW_WRITE(EDRV_REGDW_TSAD1, 0);
	dwTemp = EDRV_REGDW_READ(EDRV_REGDW_TSAD1);
	EDRV_REGDW_WRITE(EDRV_REGDW_TSAD2, 0);
	dwTemp = EDRV_REGDW_READ(EDRV_REGDW_TSAD2);
	EDRV_REGDW_WRITE(EDRV_REGDW_TSAD3, 0);
	dwTemp = EDRV_REGDW_READ(EDRV_REGDW_TSAD3);

	printk("    Command = 0x%02X\n",
	       (WORD) EDRV_REGB_READ(EDRV_REGB_COMMAND));

	// set pointer for receive buffer in controller
	printk("%s set pointer to Rx buffer\n", __func__);
	EDRV_REGDW_WRITE(EDRV_REGDW_RBSTART, EdrvInstance_l.m_pRxBufDma);

	// enable transmitter and receiver
	printk("%s enable Tx and Rx", __func__);
	EDRV_REGB_WRITE(EDRV_REGB_COMMAND,
			(EDRV_REGB_COMMAND_RE | EDRV_REGB_COMMAND_TE));
	printk("  Command = 0x%02X\n",
	       (WORD) EDRV_REGB_READ(EDRV_REGB_COMMAND));

	// clear missed packet counter to enable Rx/Tx process
	EDRV_REGDW_WRITE(EDRV_REGDW_MPC, 0);

	// set transmit configuration register
	printk("%s set Tx conf register", __func__);
	EDRV_REGDW_WRITE(EDRV_REGDW_TCR, EDRV_REGDW_TCR_DEF);
	printk(" = 0x%08X\n", EDRV_REGDW_READ(EDRV_REGDW_TCR));

	// set receive configuration register
	printk("%s set Rx conf register", __func__);
	EDRV_REGDW_WRITE(EDRV_REGDW_RCR, EDRV_REGDW_RCR_DEF);
	printk(" = 0x%08X\n", EDRV_REGDW_READ(EDRV_REGDW_RCR));

	// reset multicast MAC address filter
	EDRV_REGDW_WRITE(EDRV_REGDW_MAR0, 0);
	dwTemp = EDRV_REGDW_READ(EDRV_REGDW_MAR0);
	EDRV_REGDW_WRITE(EDRV_REGDW_MAR4, 0);
	dwTemp = EDRV_REGDW_READ(EDRV_REGDW_MAR4);

/*
    // enable transmitter and receiver
    printk("%s enable Tx and Rx", __func__);
    EDRV_REGB_WRITE(EDRV_REGB_COMMAND, (EDRV_REGB_COMMAND_RE | EDRV_REGB_COMMAND_TE));
    printk("  Command = 0x%02X\n", (WORD) EDRV_REGB_READ(EDRV_REGB_COMMAND));
*/
	// disable early interrupts
	EDRV_REGW_WRITE(EDRV_REGW_MULINT, 0);

	// enable interrupts
	printk("%s enable interrupts\n", __func__);
	EDRV_REGW_WRITE(EDRV_REGW_INT_MASK, EDRV_REGW_INT_MASK_DEF);

      Exit:
	printk("%s finished with %d\n", __func__, iResult);
	return iResult;
}

//---------------------------------------------------------------------------
//
// Function:    EdrvRemoveOne
//
// Description: shuts down one PCI device
//
// Parameters:  pPciDev             = pointer to corresponding PCI device structure
//
// Returns:     (void)
//
// State:
//
//---------------------------------------------------------------------------

static void EdrvRemoveOne(struct pci_dev *pPciDev)
{

	if (EdrvInstance_l.m_pPciDev != pPciDev) {	// trying to remove unknown device
		BUG_ON(EdrvInstance_l.m_pPciDev != pPciDev);
		goto Exit;
	}
	// disable transmitter and receiver
	EDRV_REGB_WRITE(EDRV_REGB_COMMAND, 0);

	// disable interrupts
	EDRV_REGW_WRITE(EDRV_REGW_INT_MASK, 0);

	// remove interrupt handler
	free_irq(pPciDev->irq, pPciDev);

	// free buffers
	if (EdrvInstance_l.m_pbTxBuf != NULL) {
		pci_free_consistent(pPciDev, EDRV_TX_BUFFER_SIZE,
				    EdrvInstance_l.m_pbTxBuf,
				    EdrvInstance_l.m_pTxBufDma);
		EdrvInstance_l.m_pbTxBuf = NULL;
	}

	if (EdrvInstance_l.m_pbRxBuf != NULL) {
		pci_free_consistent(pPciDev, EDRV_RX_BUFFER_SIZE,
				    EdrvInstance_l.m_pbRxBuf,
				    EdrvInstance_l.m_pRxBufDma);
		EdrvInstance_l.m_pbRxBuf = NULL;
	}
	// unmap controller's register space
	if (EdrvInstance_l.m_pIoAddr != NULL) {
		iounmap(EdrvInstance_l.m_pIoAddr);
	}
	// disable the PCI device
	pci_disable_device(pPciDev);

	// release memory regions
	pci_release_regions(pPciDev);

	EdrvInstance_l.m_pPciDev = NULL;

      Exit:;
}

//---------------------------------------------------------------------------
//
// Function:    EdrvCalcHash
//
// Description: function calculates the entry for the hash-table from MAC
//              address
//
// Parameters:  pbMAC_p - pointer to MAC address
//
// Returns:     hash value
//
// State:
//
//---------------------------------------------------------------------------
#define HASH_BITS              6	// used bits in hash
#define CRC32_POLY    0x04C11DB6	//
//#define CRC32_POLY    0xEDB88320  //
// G(x) = x32 + x26 + x23 + x22 + x16 + x12 + x11 + x10 + x8 + x7 + x5 + x4 + x2 + x + 1

static BYTE EdrvCalcHash(BYTE * pbMAC_p)
{
	DWORD dwByteCounter;
	DWORD dwBitCounter;
	DWORD dwData;
	DWORD dwCrc;
	DWORD dwCarry;
	BYTE *pbData;
	BYTE bHash;

	pbData = pbMAC_p;

	// calculate crc32 value of mac address
	dwCrc = 0xFFFFFFFF;

	for (dwByteCounter = 0; dwByteCounter < 6; dwByteCounter++) {
		dwData = *pbData;
		pbData++;
		for (dwBitCounter = 0; dwBitCounter < 8;
		     dwBitCounter++, dwData >>= 1) {
			dwCarry = (((dwCrc >> 31) ^ dwData) & 1);
			dwCrc = dwCrc << 1;
			if (dwCarry != 0) {
				dwCrc = (dwCrc ^ CRC32_POLY) | dwCarry;
			}
		}
	}

//    printk("MyCRC = 0x%08lX\n", dwCrc);
	// only upper 6 bits (HASH_BITS) are used
	// which point to specific bit in the hash registers
	bHash = (BYTE) ((dwCrc >> (32 - HASH_BITS)) & 0x3f);

	return bHash;
}
