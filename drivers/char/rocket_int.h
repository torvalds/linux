/*
 * rocket_int.h --- internal header file for rocket.c
 *
 * Written by Theodore Ts'o, Copyright 1997.
 * Copyright 1997 Comtrol Corporation.  
 * 
 */

/*
 * Definition of the types in rcktpt_type
 */
#define ROCKET_TYPE_NORMAL	0
#define ROCKET_TYPE_MODEM	1
#define ROCKET_TYPE_MODEMII	2
#define ROCKET_TYPE_MODEMIII	3
#define ROCKET_TYPE_PC104       4

#include <linux/mutex.h>

#include <asm/io.h>
#include <asm/byteorder.h>

typedef unsigned char Byte_t;
typedef unsigned int ByteIO_t;

typedef unsigned int Word_t;
typedef unsigned int WordIO_t;

typedef unsigned long DWord_t;
typedef unsigned int DWordIO_t;

/*
 * Note!  Normally the Linux I/O macros already take care of
 * byte-swapping the I/O instructions.  However, all accesses using
 * sOutDW aren't really 32-bit accesses, but should be handled in byte
 * order.  Hence the use of the cpu_to_le32() macro to byte-swap
 * things to no-op the byte swapping done by the big-endian outl()
 * instruction.
 */

#ifdef ROCKET_DEBUG_IO
static inline void sOutB(unsigned short port, unsigned char value)
{
#ifdef ROCKET_DEBUG_IO
	printk("sOutB(%x, %x)...", port, value);
#endif
	outb_p(value, port);
}

static inline void sOutW(unsigned short port, unsigned short value)
{
#ifdef ROCKET_DEBUG_IO
	printk("sOutW(%x, %x)...", port, value);
#endif
	outw_p(value, port);
}

static inline void sOutDW(unsigned short port, unsigned long value)
{
#ifdef ROCKET_DEBUG_IO
	printk("sOutDW(%x, %lx)...", port, value);
#endif
	outl_p(cpu_to_le32(value), port);
}

static inline unsigned char sInB(unsigned short port)
{
	return inb_p(port);
}

static inline unsigned short sInW(unsigned short port)
{
	return inw_p(port);
}

#else				/* !ROCKET_DEBUG_IO */
#define sOutB(a, b) outb_p(b, a)
#define sOutW(a, b) outw_p(b, a)
#define sOutDW(port, value) outl_p(cpu_to_le32(value), port)
#define sInB(a) (inb_p(a))
#define sInW(a) (inw_p(a))
#endif				/* ROCKET_DEBUG_IO */

/* This is used to move arrays of bytes so byte swapping isn't appropriate. */
#define sOutStrW(port, addr, count) if (count) outsw(port, addr, count)
#define sInStrW(port, addr, count) if (count) insw(port, addr, count)

#define CTL_SIZE 8
#define AIOP_CTL_SIZE 4
#define CHAN_AIOP_SIZE 8
#define MAX_PORTS_PER_AIOP 8
#define MAX_AIOPS_PER_BOARD 4
#define MAX_PORTS_PER_BOARD 32

/* Bus type ID */
#define	isISA	0
#define	isPCI	1
#define	isMC	2

/* Controller ID numbers */
#define CTLID_NULL  -1		/* no controller exists */
#define CTLID_0001  0x0001	/* controller release 1 */

/* AIOP ID numbers, identifies AIOP type implementing channel */
#define AIOPID_NULL -1		/* no AIOP or channel exists */
#define AIOPID_0001 0x0001	/* AIOP release 1 */

#define NULLDEV -1		/* identifies non-existant device */
#define NULLCTL -1		/* identifies non-existant controller */
#define NULLCTLPTR (CONTROLLER_T *)0	/* identifies non-existant controller */
#define NULLAIOP -1		/* identifies non-existant AIOP */
#define NULLCHAN -1		/* identifies non-existant channel */

/************************************************************************
 Global Register Offsets - Direct Access - Fixed values
************************************************************************/

#define _CMD_REG   0x38		/* Command Register            8    Write */
#define _INT_CHAN  0x39		/* Interrupt Channel Register  8    Read */
#define _INT_MASK  0x3A		/* Interrupt Mask Register     8    Read / Write */
#define _UNUSED    0x3B		/* Unused                      8 */
#define _INDX_ADDR 0x3C		/* Index Register Address      16   Write */
#define _INDX_DATA 0x3E		/* Index Register Data         8/16 Read / Write */

/************************************************************************
 Channel Register Offsets for 1st channel in AIOP - Direct Access
************************************************************************/
#define _TD0       0x00		/* Transmit Data               16   Write */
#define _RD0       0x00		/* Receive Data                16   Read */
#define _CHN_STAT0 0x20		/* Channel Status              8/16 Read / Write */
#define _FIFO_CNT0 0x10		/* Transmit/Receive FIFO Count 16   Read */
#define _INT_ID0   0x30		/* Interrupt Identification    8    Read */

/************************************************************************
 Tx Control Register Offsets - Indexed - External - Fixed
************************************************************************/
#define _TX_ENBLS  0x980	/* Tx Processor Enables Register 8 Read / Write */
#define _TXCMP1    0x988	/* Transmit Compare Value #1     8 Read / Write */
#define _TXCMP2    0x989	/* Transmit Compare Value #2     8 Read / Write */
#define _TXREP1B1  0x98A	/* Tx Replace Value #1 - Byte 1  8 Read / Write */
#define _TXREP1B2  0x98B	/* Tx Replace Value #1 - Byte 2  8 Read / Write */
#define _TXREP2    0x98C	/* Transmit Replace Value #2     8 Read / Write */

/************************************************************************
Memory Controller Register Offsets - Indexed - External - Fixed
************************************************************************/
#define _RX_FIFO    0x000	/* Rx FIFO */
#define _TX_FIFO    0x800	/* Tx FIFO */
#define _RXF_OUTP   0x990	/* Rx FIFO OUT pointer        16 Read / Write */
#define _RXF_INP    0x992	/* Rx FIFO IN pointer         16 Read / Write */
#define _TXF_OUTP   0x994	/* Tx FIFO OUT pointer        8  Read / Write */
#define _TXF_INP    0x995	/* Tx FIFO IN pointer         8  Read / Write */
#define _TXP_CNT    0x996	/* Tx Priority Count          8  Read / Write */
#define _TXP_PNTR   0x997	/* Tx Priority Pointer        8  Read / Write */

#define PRI_PEND    0x80	/* Priority data pending (bit7, Tx pri cnt) */
#define TXFIFO_SIZE 255		/* size of Tx FIFO */
#define RXFIFO_SIZE 1023	/* size of Rx FIFO */

/************************************************************************
Tx Priority Buffer - Indexed - External - Fixed
************************************************************************/
#define _TXP_BUF    0x9C0	/* Tx Priority Buffer  32  Bytes   Read / Write */
#define TXP_SIZE    0x20	/* 32 bytes */

/************************************************************************
Channel Register Offsets - Indexed - Internal - Fixed
************************************************************************/

#define _TX_CTRL    0xFF0	/* Transmit Control               16  Write */
#define _RX_CTRL    0xFF2	/* Receive Control                 8  Write */
#define _BAUD       0xFF4	/* Baud Rate                      16  Write */
#define _CLK_PRE    0xFF6	/* Clock Prescaler                 8  Write */

#define STMBREAK   0x08		/* BREAK */
#define STMFRAME   0x04		/* framing error */
#define STMRCVROVR 0x02		/* receiver over run error */
#define STMPARITY  0x01		/* parity error */
#define STMERROR   (STMBREAK | STMFRAME | STMPARITY)
#define STMBREAKH   0x800	/* BREAK */
#define STMFRAMEH   0x400	/* framing error */
#define STMRCVROVRH 0x200	/* receiver over run error */
#define STMPARITYH  0x100	/* parity error */
#define STMERRORH   (STMBREAKH | STMFRAMEH | STMPARITYH)

#define CTS_ACT   0x20		/* CTS input asserted */
#define DSR_ACT   0x10		/* DSR input asserted */
#define CD_ACT    0x08		/* CD input asserted */
#define TXFIFOMT  0x04		/* Tx FIFO is empty */
#define TXSHRMT   0x02		/* Tx shift register is empty */
#define RDA       0x01		/* Rx data available */
#define DRAINED (TXFIFOMT | TXSHRMT)	/* indicates Tx is drained */

#define STATMODE  0x8000	/* status mode enable bit */
#define RXFOVERFL 0x2000	/* receive FIFO overflow */
#define RX2MATCH  0x1000	/* receive compare byte 2 match */
#define RX1MATCH  0x0800	/* receive compare byte 1 match */
#define RXBREAK   0x0400	/* received BREAK */
#define RXFRAME   0x0200	/* received framing error */
#define RXPARITY  0x0100	/* received parity error */
#define STATERROR (RXBREAK | RXFRAME | RXPARITY)

#define CTSFC_EN  0x80		/* CTS flow control enable bit */
#define RTSTOG_EN 0x40		/* RTS toggle enable bit */
#define TXINT_EN  0x10		/* transmit interrupt enable */
#define STOP2     0x08		/* enable 2 stop bits (0 = 1 stop) */
#define PARITY_EN 0x04		/* enable parity (0 = no parity) */
#define EVEN_PAR  0x02		/* even parity (0 = odd parity) */
#define DATA8BIT  0x01		/* 8 bit data (0 = 7 bit data) */

#define SETBREAK  0x10		/* send break condition (must clear) */
#define LOCALLOOP 0x08		/* local loopback set for test */
#define SET_DTR   0x04		/* assert DTR */
#define SET_RTS   0x02		/* assert RTS */
#define TX_ENABLE 0x01		/* enable transmitter */

#define RTSFC_EN  0x40		/* RTS flow control enable */
#define RXPROC_EN 0x20		/* receive processor enable */
#define TRIG_NO   0x00		/* Rx FIFO trigger level 0 (no trigger) */
#define TRIG_1    0x08		/* trigger level 1 char */
#define TRIG_1_2  0x10		/* trigger level 1/2 */
#define TRIG_7_8  0x18		/* trigger level 7/8 */
#define TRIG_MASK 0x18		/* trigger level mask */
#define SRCINT_EN 0x04		/* special Rx condition interrupt enable */
#define RXINT_EN  0x02		/* Rx interrupt enable */
#define MCINT_EN  0x01		/* modem change interrupt enable */

#define RXF_TRIG  0x20		/* Rx FIFO trigger level interrupt */
#define TXFIFO_MT 0x10		/* Tx FIFO empty interrupt */
#define SRC_INT   0x08		/* special receive condition interrupt */
#define DELTA_CD  0x04		/* CD change interrupt */
#define DELTA_CTS 0x02		/* CTS change interrupt */
#define DELTA_DSR 0x01		/* DSR change interrupt */

#define REP1W2_EN 0x10		/* replace byte 1 with 2 bytes enable */
#define IGN2_EN   0x08		/* ignore byte 2 enable */
#define IGN1_EN   0x04		/* ignore byte 1 enable */
#define COMP2_EN  0x02		/* compare byte 2 enable */
#define COMP1_EN  0x01		/* compare byte 1 enable */

#define RESET_ALL 0x80		/* reset AIOP (all channels) */
#define TXOVERIDE 0x40		/* Transmit software off override */
#define RESETUART 0x20		/* reset channel's UART */
#define RESTXFCNT 0x10		/* reset channel's Tx FIFO count register */
#define RESRXFCNT 0x08		/* reset channel's Rx FIFO count register */

#define INTSTAT0  0x01		/* AIOP 0 interrupt status */
#define INTSTAT1  0x02		/* AIOP 1 interrupt status */
#define INTSTAT2  0x04		/* AIOP 2 interrupt status */
#define INTSTAT3  0x08		/* AIOP 3 interrupt status */

#define INTR_EN   0x08		/* allow interrupts to host */
#define INT_STROB 0x04		/* strobe and clear interrupt line (EOI) */

/**************************************************************************
 MUDBAC remapped for PCI
**************************************************************************/

#define _CFG_INT_PCI  0x40
#define _PCI_INT_FUNC 0x3A

#define PCI_STROB 0x2000	/* bit 13 of int aiop register */
#define INTR_EN_PCI   0x0010	/* allow interrupts to host */

/*
 * Definitions for Universal PCI board registers
 */
#define _PCI_9030_INT_CTRL	0x4c          /* Offsets from BAR1 */
#define _PCI_9030_GPIO_CTRL	0x54
#define PCI_INT_CTRL_AIOP	0x0001
#define PCI_GPIO_CTRL_8PORT	0x4000
#define _PCI_9030_RING_IND	0xc0          /* Offsets from BAR1 */

#define CHAN3_EN  0x08		/* enable AIOP 3 */
#define CHAN2_EN  0x04		/* enable AIOP 2 */
#define CHAN1_EN  0x02		/* enable AIOP 1 */
#define CHAN0_EN  0x01		/* enable AIOP 0 */
#define FREQ_DIS  0x00
#define FREQ_274HZ 0x60
#define FREQ_137HZ 0x50
#define FREQ_69HZ  0x40
#define FREQ_34HZ  0x30
#define FREQ_17HZ  0x20
#define FREQ_9HZ   0x10
#define PERIODIC_ONLY 0x80	/* only PERIODIC interrupt */

#define CHANINT_EN 0x0100	/* flags to enable/disable channel ints */

#define RDATASIZE 72
#define RREGDATASIZE 52

/*
 * AIOP interrupt bits for ISA/PCI boards and UPCI boards.
 */
#define AIOP_INTR_BIT_0		0x0001
#define AIOP_INTR_BIT_1		0x0002
#define AIOP_INTR_BIT_2		0x0004
#define AIOP_INTR_BIT_3		0x0008

#define AIOP_INTR_BITS ( \
	AIOP_INTR_BIT_0 \
	| AIOP_INTR_BIT_1 \
	| AIOP_INTR_BIT_2 \
	| AIOP_INTR_BIT_3)

#define UPCI_AIOP_INTR_BIT_0	0x0004
#define UPCI_AIOP_INTR_BIT_1	0x0020
#define UPCI_AIOP_INTR_BIT_2	0x0100
#define UPCI_AIOP_INTR_BIT_3	0x0800

#define UPCI_AIOP_INTR_BITS ( \
	UPCI_AIOP_INTR_BIT_0 \
	| UPCI_AIOP_INTR_BIT_1 \
	| UPCI_AIOP_INTR_BIT_2 \
	| UPCI_AIOP_INTR_BIT_3)

/* Controller level information structure */
typedef struct {
	int CtlID;
	int CtlNum;
	int BusType;
	int boardType;
	int isUPCI;
	WordIO_t PCIIO;
	WordIO_t PCIIO2;
	ByteIO_t MBaseIO;
	ByteIO_t MReg1IO;
	ByteIO_t MReg2IO;
	ByteIO_t MReg3IO;
	Byte_t MReg2;
	Byte_t MReg3;
	int NumAiop;
	int AltChanRingIndicator;
	ByteIO_t UPCIRingInd;
	WordIO_t AiopIO[AIOP_CTL_SIZE];
	ByteIO_t AiopIntChanIO[AIOP_CTL_SIZE];
	int AiopID[AIOP_CTL_SIZE];
	int AiopNumChan[AIOP_CTL_SIZE];
	Word_t *AiopIntrBits;
} CONTROLLER_T;

typedef CONTROLLER_T CONTROLLER_t;

/* Channel level information structure */
typedef struct {
	CONTROLLER_T *CtlP;
	int AiopNum;
	int ChanID;
	int ChanNum;
	int rtsToggle;

	ByteIO_t Cmd;
	ByteIO_t IntChan;
	ByteIO_t IntMask;
	DWordIO_t IndexAddr;
	WordIO_t IndexData;

	WordIO_t TxRxData;
	WordIO_t ChanStat;
	WordIO_t TxRxCount;
	ByteIO_t IntID;

	Word_t TxFIFO;
	Word_t TxFIFOPtrs;
	Word_t RxFIFO;
	Word_t RxFIFOPtrs;
	Word_t TxPrioCnt;
	Word_t TxPrioPtr;
	Word_t TxPrioBuf;

	Byte_t R[RREGDATASIZE];

	Byte_t BaudDiv[4];
	Byte_t TxControl[4];
	Byte_t RxControl[4];
	Byte_t TxEnables[4];
	Byte_t TxCompare[4];
	Byte_t TxReplace1[4];
	Byte_t TxReplace2[4];
} CHANNEL_T;

typedef CHANNEL_T CHANNEL_t;
typedef CHANNEL_T *CHANPTR_T;

#define InterfaceModeRS232  0x00
#define InterfaceModeRS422  0x08
#define InterfaceModeRS485  0x10
#define InterfaceModeRS232T 0x18

/***************************************************************************
Function: sClrBreak
Purpose:  Stop sending a transmit BREAK signal
Call:     sClrBreak(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sClrBreak(ChP) \
do { \
   (ChP)->TxControl[3] &= ~SETBREAK; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sClrDTR
Purpose:  Clr the DTR output
Call:     sClrDTR(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sClrDTR(ChP) \
do { \
   (ChP)->TxControl[3] &= ~SET_DTR; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sClrRTS
Purpose:  Clr the RTS output
Call:     sClrRTS(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sClrRTS(ChP) \
do { \
   if ((ChP)->rtsToggle) break; \
   (ChP)->TxControl[3] &= ~SET_RTS; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sClrTxXOFF
Purpose:  Clear any existing transmit software flow control off condition
Call:     sClrTxXOFF(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sClrTxXOFF(ChP) \
do { \
   sOutB((ChP)->Cmd,TXOVERIDE | (Byte_t)(ChP)->ChanNum); \
   sOutB((ChP)->Cmd,(Byte_t)(ChP)->ChanNum); \
} while (0)

/***************************************************************************
Function: sCtlNumToCtlPtr
Purpose:  Convert a controller number to controller structure pointer
Call:     sCtlNumToCtlPtr(CtlNum)
          int CtlNum; Controller number
Return:   CONTROLLER_T *: Ptr to controller structure
*/
#define sCtlNumToCtlPtr(CTLNUM) &sController[CTLNUM]

/***************************************************************************
Function: sControllerEOI
Purpose:  Strobe the MUDBAC's End Of Interrupt bit.
Call:     sControllerEOI(CtlP)
          CONTROLLER_T *CtlP; Ptr to controller structure
*/
#define sControllerEOI(CTLP) sOutB((CTLP)->MReg2IO,(CTLP)->MReg2 | INT_STROB)

/***************************************************************************
Function: sPCIControllerEOI
Purpose:  Strobe the PCI End Of Interrupt bit.
          For the UPCI boards, toggle the AIOP interrupt enable bit
	  (this was taken from the Windows driver).
Call:     sPCIControllerEOI(CtlP)
          CONTROLLER_T *CtlP; Ptr to controller structure
*/
#define sPCIControllerEOI(CTLP) \
do { \
    if ((CTLP)->isUPCI) { \
	Word_t w = sInW((CTLP)->PCIIO); \
	sOutW((CTLP)->PCIIO, (w ^ PCI_INT_CTRL_AIOP)); \
	sOutW((CTLP)->PCIIO, w); \
    } \
    else { \
	sOutW((CTLP)->PCIIO, PCI_STROB); \
    } \
} while (0)

/***************************************************************************
Function: sDisAiop
Purpose:  Disable I/O access to an AIOP
Call:     sDisAiop(CltP)
          CONTROLLER_T *CtlP; Ptr to controller structure
          int AiopNum; Number of AIOP on controller
*/
#define sDisAiop(CTLP,AIOPNUM) \
do { \
   (CTLP)->MReg3 &= sBitMapClrTbl[AIOPNUM]; \
   sOutB((CTLP)->MReg3IO,(CTLP)->MReg3); \
} while (0)

/***************************************************************************
Function: sDisCTSFlowCtl
Purpose:  Disable output flow control using CTS
Call:     sDisCTSFlowCtl(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sDisCTSFlowCtl(ChP) \
do { \
   (ChP)->TxControl[2] &= ~CTSFC_EN; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sDisIXANY
Purpose:  Disable IXANY Software Flow Control
Call:     sDisIXANY(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sDisIXANY(ChP) \
do { \
   (ChP)->R[0x0e] = 0x86; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->R[0x0c]); \
} while (0)

/***************************************************************************
Function: DisParity
Purpose:  Disable parity
Call:     sDisParity(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Comments: Function sSetParity() can be used in place of functions sEnParity(),
          sDisParity(), sSetOddParity(), and sSetEvenParity().
*/
#define sDisParity(ChP) \
do { \
   (ChP)->TxControl[2] &= ~PARITY_EN; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sDisRTSToggle
Purpose:  Disable RTS toggle
Call:     sDisRTSToggle(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sDisRTSToggle(ChP) \
do { \
   (ChP)->TxControl[2] &= ~RTSTOG_EN; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
   (ChP)->rtsToggle = 0; \
} while (0)

/***************************************************************************
Function: sDisRxFIFO
Purpose:  Disable Rx FIFO
Call:     sDisRxFIFO(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sDisRxFIFO(ChP) \
do { \
   (ChP)->R[0x32] = 0x0a; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->R[0x30]); \
} while (0)

/***************************************************************************
Function: sDisRxStatusMode
Purpose:  Disable the Rx status mode
Call:     sDisRxStatusMode(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Comments: This takes the channel out of the receive status mode.  All
          subsequent reads of receive data using sReadRxWord() will return
          two data bytes.
*/
#define sDisRxStatusMode(ChP) sOutW((ChP)->ChanStat,0)

/***************************************************************************
Function: sDisTransmit
Purpose:  Disable transmit
Call:     sDisTransmit(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
          This disables movement of Tx data from the Tx FIFO into the 1 byte
          Tx buffer.  Therefore there could be up to a 2 byte latency
          between the time sDisTransmit() is called and the transmit buffer
          and transmit shift register going completely empty.
*/
#define sDisTransmit(ChP) \
do { \
   (ChP)->TxControl[3] &= ~TX_ENABLE; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sDisTxSoftFlowCtl
Purpose:  Disable Tx Software Flow Control
Call:     sDisTxSoftFlowCtl(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sDisTxSoftFlowCtl(ChP) \
do { \
   (ChP)->R[0x06] = 0x8a; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->R[0x04]); \
} while (0)

/***************************************************************************
Function: sEnAiop
Purpose:  Enable I/O access to an AIOP
Call:     sEnAiop(CltP)
          CONTROLLER_T *CtlP; Ptr to controller structure
          int AiopNum; Number of AIOP on controller
*/
#define sEnAiop(CTLP,AIOPNUM) \
do { \
   (CTLP)->MReg3 |= sBitMapSetTbl[AIOPNUM]; \
   sOutB((CTLP)->MReg3IO,(CTLP)->MReg3); \
} while (0)

/***************************************************************************
Function: sEnCTSFlowCtl
Purpose:  Enable output flow control using CTS
Call:     sEnCTSFlowCtl(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sEnCTSFlowCtl(ChP) \
do { \
   (ChP)->TxControl[2] |= CTSFC_EN; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sEnIXANY
Purpose:  Enable IXANY Software Flow Control
Call:     sEnIXANY(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sEnIXANY(ChP) \
do { \
   (ChP)->R[0x0e] = 0x21; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->R[0x0c]); \
} while (0)

/***************************************************************************
Function: EnParity
Purpose:  Enable parity
Call:     sEnParity(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Comments: Function sSetParity() can be used in place of functions sEnParity(),
          sDisParity(), sSetOddParity(), and sSetEvenParity().

Warnings: Before enabling parity odd or even parity should be chosen using
          functions sSetOddParity() or sSetEvenParity().
*/
#define sEnParity(ChP) \
do { \
   (ChP)->TxControl[2] |= PARITY_EN; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sEnRTSToggle
Purpose:  Enable RTS toggle
Call:     sEnRTSToggle(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Comments: This function will disable RTS flow control and clear the RTS
          line to allow operation of RTS toggle.
*/
#define sEnRTSToggle(ChP) \
do { \
   (ChP)->RxControl[2] &= ~RTSFC_EN; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->RxControl[0]); \
   (ChP)->TxControl[2] |= RTSTOG_EN; \
   (ChP)->TxControl[3] &= ~SET_RTS; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
   (ChP)->rtsToggle = 1; \
} while (0)

/***************************************************************************
Function: sEnRxFIFO
Purpose:  Enable Rx FIFO
Call:     sEnRxFIFO(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sEnRxFIFO(ChP) \
do { \
   (ChP)->R[0x32] = 0x08; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->R[0x30]); \
} while (0)

/***************************************************************************
Function: sEnRxProcessor
Purpose:  Enable the receive processor
Call:     sEnRxProcessor(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Comments: This function is used to start the receive processor.  When
          the channel is in the reset state the receive processor is not
          running.  This is done to prevent the receive processor from
          executing invalid microcode instructions prior to the
          downloading of the microcode.

Warnings: This function must be called after valid microcode has been
          downloaded to the AIOP, and it must not be called before the
          microcode has been downloaded.
*/
#define sEnRxProcessor(ChP) \
do { \
   (ChP)->RxControl[2] |= RXPROC_EN; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->RxControl[0]); \
} while (0)

/***************************************************************************
Function: sEnRxStatusMode
Purpose:  Enable the Rx status mode
Call:     sEnRxStatusMode(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Comments: This places the channel in the receive status mode.  All subsequent
          reads of receive data using sReadRxWord() will return a data byte
          in the low word and a status byte in the high word.

*/
#define sEnRxStatusMode(ChP) sOutW((ChP)->ChanStat,STATMODE)

/***************************************************************************
Function: sEnTransmit
Purpose:  Enable transmit
Call:     sEnTransmit(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sEnTransmit(ChP) \
do { \
   (ChP)->TxControl[3] |= TX_ENABLE; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sEnTxSoftFlowCtl
Purpose:  Enable Tx Software Flow Control
Call:     sEnTxSoftFlowCtl(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sEnTxSoftFlowCtl(ChP) \
do { \
   (ChP)->R[0x06] = 0xc5; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->R[0x04]); \
} while (0)

/***************************************************************************
Function: sGetAiopIntStatus
Purpose:  Get the AIOP interrupt status
Call:     sGetAiopIntStatus(CtlP,AiopNum)
          CONTROLLER_T *CtlP; Ptr to controller structure
          int AiopNum; AIOP number
Return:   Byte_t: The AIOP interrupt status.  Bits 0 through 7
                         represent channels 0 through 7 respectively.  If a
                         bit is set that channel is interrupting.
*/
#define sGetAiopIntStatus(CTLP,AIOPNUM) sInB((CTLP)->AiopIntChanIO[AIOPNUM])

/***************************************************************************
Function: sGetAiopNumChan
Purpose:  Get the number of channels supported by an AIOP
Call:     sGetAiopNumChan(CtlP,AiopNum)
          CONTROLLER_T *CtlP; Ptr to controller structure
          int AiopNum; AIOP number
Return:   int: The number of channels supported by the AIOP
*/
#define sGetAiopNumChan(CTLP,AIOPNUM) (CTLP)->AiopNumChan[AIOPNUM]

/***************************************************************************
Function: sGetChanIntID
Purpose:  Get a channel's interrupt identification byte
Call:     sGetChanIntID(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Return:   Byte_t: The channel interrupt ID.  Can be any
             combination of the following flags:
                RXF_TRIG:     Rx FIFO trigger level interrupt
                TXFIFO_MT:    Tx FIFO empty interrupt
                SRC_INT:      Special receive condition interrupt
                DELTA_CD:     CD change interrupt
                DELTA_CTS:    CTS change interrupt
                DELTA_DSR:    DSR change interrupt
*/
#define sGetChanIntID(ChP) (sInB((ChP)->IntID) & (RXF_TRIG | TXFIFO_MT | SRC_INT | DELTA_CD | DELTA_CTS | DELTA_DSR))

/***************************************************************************
Function: sGetChanNum
Purpose:  Get the number of a channel within an AIOP
Call:     sGetChanNum(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Return:   int: Channel number within AIOP, or NULLCHAN if channel does
               not exist.
*/
#define sGetChanNum(ChP) (ChP)->ChanNum

/***************************************************************************
Function: sGetChanStatus
Purpose:  Get the channel status
Call:     sGetChanStatus(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Return:   Word_t: The channel status.  Can be any combination of
             the following flags:
                LOW BYTE FLAGS
                CTS_ACT:      CTS input asserted
                DSR_ACT:      DSR input asserted
                CD_ACT:       CD input asserted
                TXFIFOMT:     Tx FIFO is empty
                TXSHRMT:      Tx shift register is empty
                RDA:          Rx data available

                HIGH BYTE FLAGS
                STATMODE:     status mode enable bit
                RXFOVERFL:    receive FIFO overflow
                RX2MATCH:     receive compare byte 2 match
                RX1MATCH:     receive compare byte 1 match
                RXBREAK:      received BREAK
                RXFRAME:      received framing error
                RXPARITY:     received parity error
Warnings: This function will clear the high byte flags in the Channel
          Status Register.
*/
#define sGetChanStatus(ChP) sInW((ChP)->ChanStat)

/***************************************************************************
Function: sGetChanStatusLo
Purpose:  Get the low byte only of the channel status
Call:     sGetChanStatusLo(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Return:   Byte_t: The channel status low byte.  Can be any combination
             of the following flags:
                CTS_ACT:      CTS input asserted
                DSR_ACT:      DSR input asserted
                CD_ACT:       CD input asserted
                TXFIFOMT:     Tx FIFO is empty
                TXSHRMT:      Tx shift register is empty
                RDA:          Rx data available
*/
#define sGetChanStatusLo(ChP) sInB((ByteIO_t)(ChP)->ChanStat)

/**********************************************************************
 * Get RI status of channel
 * Defined as a function in rocket.c   -aes
 */
#if 0
#define sGetChanRI(ChP) ((ChP)->CtlP->AltChanRingIndicator ? \
                          (sInB((ByteIO_t)((ChP)->ChanStat+8)) & DSR_ACT) : \
                            (((ChP)->CtlP->boardType == ROCKET_TYPE_PC104) ? \
                               (!(sInB((ChP)->CtlP->AiopIO[3]) & sBitMapSetTbl[(ChP)->ChanNum])) : \
                             0))
#endif

/***************************************************************************
Function: sGetControllerIntStatus
Purpose:  Get the controller interrupt status
Call:     sGetControllerIntStatus(CtlP)
          CONTROLLER_T *CtlP; Ptr to controller structure
Return:   Byte_t: The controller interrupt status in the lower 4
                         bits.  Bits 0 through 3 represent AIOP's 0
                         through 3 respectively.  If a bit is set that
                         AIOP is interrupting.  Bits 4 through 7 will
                         always be cleared.
*/
#define sGetControllerIntStatus(CTLP) (sInB((CTLP)->MReg1IO) & 0x0f)

/***************************************************************************
Function: sPCIGetControllerIntStatus
Purpose:  Get the controller interrupt status
Call:     sPCIGetControllerIntStatus(CtlP)
          CONTROLLER_T *CtlP; Ptr to controller structure
Return:   unsigned char: The controller interrupt status in the lower 4
                         bits and bit 4.  Bits 0 through 3 represent AIOP's 0
                         through 3 respectively. Bit 4 is set if the int 
			 was generated from periodic. If a bit is set the
			 AIOP is interrupting.
*/
#define sPCIGetControllerIntStatus(CTLP) \
	((CTLP)->isUPCI ? \
	  (sInW((CTLP)->PCIIO2) & UPCI_AIOP_INTR_BITS) : \
	  ((sInW((CTLP)->PCIIO) >> 8) & AIOP_INTR_BITS))

/***************************************************************************

Function: sGetRxCnt
Purpose:  Get the number of data bytes in the Rx FIFO
Call:     sGetRxCnt(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Return:   int: The number of data bytes in the Rx FIFO.
Comments: Byte read of count register is required to obtain Rx count.

*/
#define sGetRxCnt(ChP) sInW((ChP)->TxRxCount)

/***************************************************************************
Function: sGetTxCnt
Purpose:  Get the number of data bytes in the Tx FIFO
Call:     sGetTxCnt(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Return:   Byte_t: The number of data bytes in the Tx FIFO.
Comments: Byte read of count register is required to obtain Tx count.

*/
#define sGetTxCnt(ChP) sInB((ByteIO_t)(ChP)->TxRxCount)

/*****************************************************************************
Function: sGetTxRxDataIO
Purpose:  Get the I/O address of a channel's TxRx Data register
Call:     sGetTxRxDataIO(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Return:   WordIO_t: I/O address of a channel's TxRx Data register
*/
#define sGetTxRxDataIO(ChP) (ChP)->TxRxData

/***************************************************************************
Function: sInitChanDefaults
Purpose:  Initialize a channel structure to it's default state.
Call:     sInitChanDefaults(ChP)
          CHANNEL_T *ChP; Ptr to the channel structure
Comments: This function must be called once for every channel structure
          that exists before any other SSCI calls can be made.

*/
#define sInitChanDefaults(ChP) \
do { \
   (ChP)->CtlP = NULLCTLPTR; \
   (ChP)->AiopNum = NULLAIOP; \
   (ChP)->ChanID = AIOPID_NULL; \
   (ChP)->ChanNum = NULLCHAN; \
} while (0)

/***************************************************************************
Function: sResetAiopByNum
Purpose:  Reset the AIOP by number
Call:     sResetAiopByNum(CTLP,AIOPNUM)
	CONTROLLER_T CTLP; Ptr to controller structure
	AIOPNUM; AIOP index 
*/
#define sResetAiopByNum(CTLP,AIOPNUM) \
do { \
   sOutB((CTLP)->AiopIO[(AIOPNUM)]+_CMD_REG,RESET_ALL); \
   sOutB((CTLP)->AiopIO[(AIOPNUM)]+_CMD_REG,0x0); \
} while (0)

/***************************************************************************
Function: sSendBreak
Purpose:  Send a transmit BREAK signal
Call:     sSendBreak(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSendBreak(ChP) \
do { \
   (ChP)->TxControl[3] |= SETBREAK; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetBaud
Purpose:  Set baud rate
Call:     sSetBaud(ChP,Divisor)
          CHANNEL_T *ChP; Ptr to channel structure
          Word_t Divisor; 16 bit baud rate divisor for channel
*/
#define sSetBaud(ChP,DIVISOR) \
do { \
   (ChP)->BaudDiv[2] = (Byte_t)(DIVISOR); \
   (ChP)->BaudDiv[3] = (Byte_t)((DIVISOR) >> 8); \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->BaudDiv[0]); \
} while (0)

/***************************************************************************
Function: sSetData7
Purpose:  Set data bits to 7
Call:     sSetData7(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetData7(ChP) \
do { \
   (ChP)->TxControl[2] &= ~DATA8BIT; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetData8
Purpose:  Set data bits to 8
Call:     sSetData8(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetData8(ChP) \
do { \
   (ChP)->TxControl[2] |= DATA8BIT; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetDTR
Purpose:  Set the DTR output
Call:     sSetDTR(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetDTR(ChP) \
do { \
   (ChP)->TxControl[3] |= SET_DTR; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetEvenParity
Purpose:  Set even parity
Call:     sSetEvenParity(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Comments: Function sSetParity() can be used in place of functions sEnParity(),
          sDisParity(), sSetOddParity(), and sSetEvenParity().

Warnings: This function has no effect unless parity is enabled with function
          sEnParity().
*/
#define sSetEvenParity(ChP) \
do { \
   (ChP)->TxControl[2] |= EVEN_PAR; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetOddParity
Purpose:  Set odd parity
Call:     sSetOddParity(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Comments: Function sSetParity() can be used in place of functions sEnParity(),
          sDisParity(), sSetOddParity(), and sSetEvenParity().

Warnings: This function has no effect unless parity is enabled with function
          sEnParity().
*/
#define sSetOddParity(ChP) \
do { \
   (ChP)->TxControl[2] &= ~EVEN_PAR; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetRTS
Purpose:  Set the RTS output
Call:     sSetRTS(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetRTS(ChP) \
do { \
   if ((ChP)->rtsToggle) break; \
   (ChP)->TxControl[3] |= SET_RTS; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetRxTrigger
Purpose:  Set the Rx FIFO trigger level
Call:     sSetRxProcessor(ChP,Level)
          CHANNEL_T *ChP; Ptr to channel structure
          Byte_t Level; Number of characters in Rx FIFO at which the
             interrupt will be generated.  Can be any of the following flags:

             TRIG_NO:   no trigger
             TRIG_1:    1 character in FIFO
             TRIG_1_2:  FIFO 1/2 full
             TRIG_7_8:  FIFO 7/8 full
Comments: An interrupt will be generated when the trigger level is reached
          only if function sEnInterrupt() has been called with flag
          RXINT_EN set.  The RXF_TRIG flag in the Interrupt Idenfification
          register will be set whenever the trigger level is reached
          regardless of the setting of RXINT_EN.

*/
#define sSetRxTrigger(ChP,LEVEL) \
do { \
   (ChP)->RxControl[2] &= ~TRIG_MASK; \
   (ChP)->RxControl[2] |= LEVEL; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->RxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetStop1
Purpose:  Set stop bits to 1
Call:     sSetStop1(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetStop1(ChP) \
do { \
   (ChP)->TxControl[2] &= ~STOP2; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetStop2
Purpose:  Set stop bits to 2
Call:     sSetStop2(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
*/
#define sSetStop2(ChP) \
do { \
   (ChP)->TxControl[2] |= STOP2; \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->TxControl[0]); \
} while (0)

/***************************************************************************
Function: sSetTxXOFFChar
Purpose:  Set the Tx XOFF flow control character
Call:     sSetTxXOFFChar(ChP,Ch)
          CHANNEL_T *ChP; Ptr to channel structure
          Byte_t Ch; The value to set the Tx XOFF character to
*/
#define sSetTxXOFFChar(ChP,CH) \
do { \
   (ChP)->R[0x07] = (CH); \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->R[0x04]); \
} while (0)

/***************************************************************************
Function: sSetTxXONChar
Purpose:  Set the Tx XON flow control character
Call:     sSetTxXONChar(ChP,Ch)
          CHANNEL_T *ChP; Ptr to channel structure
          Byte_t Ch; The value to set the Tx XON character to
*/
#define sSetTxXONChar(ChP,CH) \
do { \
   (ChP)->R[0x0b] = (CH); \
   sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->R[0x08]); \
} while (0)

/***************************************************************************
Function: sStartRxProcessor
Purpose:  Start a channel's receive processor
Call:     sStartRxProcessor(ChP)
          CHANNEL_T *ChP; Ptr to channel structure
Comments: This function is used to start a Rx processor after it was
          stopped with sStopRxProcessor() or sStopSWInFlowCtl().  It
          will restart both the Rx processor and software input flow control.

*/
#define sStartRxProcessor(ChP) sOutDW((ChP)->IndexAddr,*(DWord_t *)&(ChP)->R[0])

/***************************************************************************
Function: sWriteTxByte
Purpose:  Write a transmit data byte to a channel.
          ByteIO_t io: Channel transmit register I/O address.  This can
                           be obtained with sGetTxRxDataIO().
          Byte_t Data; The transmit data byte.
Warnings: This function writes the data byte without checking to see if
          sMaxTxSize is exceeded in the Tx FIFO.
*/
#define sWriteTxByte(IO,DATA) sOutB(IO,DATA)

/*
 * Begin Linux specific definitions for the Rocketport driver
 *
 * This code is Copyright Theodore Ts'o, 1995-1997
 */

struct r_port {
	int magic;
	int line;
	int flags;
	int count;
	int blocked_open;
	struct tty_struct *tty;
	unsigned int board:3;
	unsigned int aiop:2;
	unsigned int chan:3;
	CONTROLLER_t *ctlp;
	CHANNEL_t channel;
	int closing_wait;
	int close_delay;
	int intmask;
	int xmit_fifo_room;	/* room in xmit fifo */
	unsigned char *xmit_buf;
	int xmit_head;
	int xmit_tail;
	int xmit_cnt;
	int cd_status;
	int ignore_status_mask;
	int read_status_mask;
	int cps;

#ifdef DECLARE_WAITQUEUE
	wait_queue_head_t open_wait;
	wait_queue_head_t close_wait;
#else
	struct wait_queue *open_wait;
	struct wait_queue *close_wait;
#endif
	spinlock_t slock;
	struct mutex write_mtx;
};

#define RPORT_MAGIC 0x525001

#define NUM_BOARDS 8
#define MAX_RP_PORTS (32*NUM_BOARDS)

/*
 * The size of the xmit buffer is 1 page, or 4096 bytes
 */
#define XMIT_BUF_SIZE 4096

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/* Internal flags used only by the rocketport driver */
#define ROCKET_INITIALIZED	0x80000000	/* Port is active */
#define ROCKET_CLOSING		0x40000000	/* Serial port is closing */
#define ROCKET_NORMAL_ACTIVE	0x20000000	/* Normal port is active */

/* tty subtypes */
#define SERIAL_TYPE_NORMAL 1

/*
 * Assigned major numbers for the Comtrol Rocketport
 */
#define TTY_ROCKET_MAJOR	46
#define CUA_ROCKET_MAJOR	47

#ifdef PCI_VENDOR_ID_RP
#undef PCI_VENDOR_ID_RP
#undef PCI_DEVICE_ID_RP8OCTA
#undef PCI_DEVICE_ID_RP8INTF
#undef PCI_DEVICE_ID_RP16INTF
#undef PCI_DEVICE_ID_RP32INTF
#undef PCI_DEVICE_ID_URP8OCTA
#undef PCI_DEVICE_ID_URP8INTF
#undef PCI_DEVICE_ID_URP16INTF
#undef PCI_DEVICE_ID_CRP16INTF
#undef PCI_DEVICE_ID_URP32INTF
#endif

/*  Comtrol PCI Vendor ID */
#define PCI_VENDOR_ID_RP		0x11fe

/*  Comtrol Device ID's */
#define PCI_DEVICE_ID_RP32INTF		0x0001	/* Rocketport 32 port w/external I/F     */
#define PCI_DEVICE_ID_RP8INTF		0x0002	/* Rocketport 8 port w/external I/F      */
#define PCI_DEVICE_ID_RP16INTF		0x0003	/* Rocketport 16 port w/external I/F     */
#define PCI_DEVICE_ID_RP4QUAD		0x0004	/* Rocketport 4 port w/quad cable        */
#define PCI_DEVICE_ID_RP8OCTA		0x0005	/* Rocketport 8 port w/octa cable        */
#define PCI_DEVICE_ID_RP8J		0x0006	/* Rocketport 8 port w/RJ11 connectors   */
#define PCI_DEVICE_ID_RP4J		0x0007	/* Rocketport 4 port w/RJ11 connectors   */
#define PCI_DEVICE_ID_RP8SNI		0x0008	/* Rocketport 8 port w/ DB78 SNI (Siemens) connector */
#define PCI_DEVICE_ID_RP16SNI		0x0009	/* Rocketport 16 port w/ DB78 SNI (Siemens) connector   */
#define PCI_DEVICE_ID_RPP4		0x000A	/* Rocketport Plus 4 port                */
#define PCI_DEVICE_ID_RPP8		0x000B	/* Rocketport Plus 8 port                */
#define PCI_DEVICE_ID_RP6M		0x000C	/* RocketModem 6 port                    */
#define PCI_DEVICE_ID_RP4M		0x000D	/* RocketModem 4 port                    */
#define PCI_DEVICE_ID_RP2_232           0x000E	/* Rocketport Plus 2 port RS232          */
#define PCI_DEVICE_ID_RP2_422           0x000F	/* Rocketport Plus 2 port RS422          */ 

/* Universal PCI boards  */
#define PCI_DEVICE_ID_URP32INTF		0x0801	/* Rocketport UPCI 32 port w/external I/F */ 
#define PCI_DEVICE_ID_URP8INTF		0x0802	/* Rocketport UPCI 8 port w/external I/F  */
#define PCI_DEVICE_ID_URP16INTF		0x0803	/* Rocketport UPCI 16 port w/external I/F */
#define PCI_DEVICE_ID_URP8OCTA		0x0805	/* Rocketport UPCI 8 port w/octa cable    */
#define PCI_DEVICE_ID_UPCI_RM3_8PORT    0x080C	/* Rocketmodem III 8 port                 */
#define PCI_DEVICE_ID_UPCI_RM3_4PORT    0x080D	/* Rocketmodem III 4 port                 */

/* Compact PCI device */ 
#define PCI_DEVICE_ID_CRP16INTF		0x0903	/* Rocketport Compact PCI 16 port w/external I/F */

#define TTY_GET_LINE(t) t->index
#define TTY_DRIVER_MINOR_START(t) t->driver->minor_start
#define TTY_DRIVER_SUBTYPE(t) t->driver->subtype
#define TTY_DRIVER_NAME(t) t->driver->name
#define TTY_DRIVER_NAME_BASE(t) t->driver->name_base
#define TTY_DRIVER_FLUSH_BUFFER_EXISTS(t) t->driver->flush_buffer
#define TTY_DRIVER_FLUSH_BUFFER(t) t->driver->flush_buffer(t)


