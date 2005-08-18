/*  Copyright, 1988-1992, Russell Nelson, Crynwr Software

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 1.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
   */

#include <linux/config.h>

#if defined(CONFIG_ARCH_IXDP2X01) || defined(CONFIG_ARCH_PNX0105)
/* IXDP2401/IXDP2801 uses dword-aligned register addressing */
#define CS89x0_PORT(reg) ((reg) * 2)
#else
#define CS89x0_PORT(reg) (reg)
#endif

#define PP_ChipID 0x0000	/* offset   0h -> Corp -ID              */
				/* offset   2h -> Model/Product Number  */
				/* offset   3h -> Chip Revision Number  */

#define PP_ISAIOB 0x0020	/*  IO base address */
#define PP_CS8900_ISAINT 0x0022	/*  ISA interrupt select */
#define PP_CS8920_ISAINT 0x0370	/*  ISA interrupt select */
#define PP_CS8900_ISADMA 0x0024	/*  ISA Rec DMA channel */
#define PP_CS8920_ISADMA 0x0374	/*  ISA Rec DMA channel */
#define PP_ISASOF 0x0026	/*  ISA DMA offset */
#define PP_DmaFrameCnt 0x0028	/*  ISA DMA Frame count */
#define PP_DmaByteCnt 0x002A	/*  ISA DMA Byte count */
#define PP_CS8900_ISAMemB 0x002C	/*  Memory base */
#define PP_CS8920_ISAMemB 0x0348 /*  */

#define PP_ISABootBase 0x0030	/*  Boot Prom base  */
#define PP_ISABootMask 0x0034	/*  Boot Prom Mask */

/* EEPROM data and command registers */
#define PP_EECMD 0x0040		/*  NVR Interface Command register */
#define PP_EEData 0x0042	/*  NVR Interface Data Register */
#define PP_DebugReg 0x0044	/*  Debug Register */

#define PP_RxCFG 0x0102		/*  Rx Bus config */
#define PP_RxCTL 0x0104		/*  Receive Control Register */
#define PP_TxCFG 0x0106		/*  Transmit Config Register */
#define PP_TxCMD 0x0108		/*  Transmit Command Register */
#define PP_BufCFG 0x010A	/*  Bus configuration Register */
#define PP_LineCTL 0x0112	/*  Line Config Register */
#define PP_SelfCTL 0x0114	/*  Self Command Register */
#define PP_BusCTL 0x0116	/*  ISA bus control Register */
#define PP_TestCTL 0x0118	/*  Test Register */
#define PP_AutoNegCTL 0x011C	/*  Auto Negotiation Ctrl */

#define PP_ISQ 0x0120		/*  Interrupt Status */
#define PP_RxEvent 0x0124	/*  Rx Event Register */
#define PP_TxEvent 0x0128	/*  Tx Event Register */
#define PP_BufEvent 0x012C	/*  Bus Event Register */
#define PP_RxMiss 0x0130	/*  Receive Miss Count */
#define PP_TxCol 0x0132		/*  Transmit Collision Count */
#define PP_LineST 0x0134	/*  Line State Register */
#define PP_SelfST 0x0136	/*  Self State register */
#define PP_BusST 0x0138		/*  Bus Status */
#define PP_TDR 0x013C		/*  Time Domain Reflectometry */
#define PP_AutoNegST 0x013E	/*  Auto Neg Status */
#define PP_TxCommand 0x0144	/*  Tx Command */
#define PP_TxLength 0x0146	/*  Tx Length */
#define PP_LAF 0x0150		/*  Hash Table */
#define PP_IA 0x0158		/*  Physical Address Register */

#define PP_RxStatus 0x0400	/*  Receive start of frame */
#define PP_RxLength 0x0402	/*  Receive Length of frame */
#define PP_RxFrame 0x0404	/*  Receive frame pointer */
#define PP_TxFrame 0x0A00	/*  Transmit frame pointer */

/*  Primary I/O Base Address. If no I/O base is supplied by the user, then this */
/*  can be used as the default I/O base to access the PacketPage Area. */
#define DEFAULTIOBASE 0x0300
#define FIRST_IO 0x020C		/*  First I/O port to check */
#define LAST_IO 0x037C		/*  Last I/O port to check (+10h) */
#define ADD_MASK 0x3000		/*  Mask it use of the ADD_PORT register */
#define ADD_SIG 0x3000		/*  Expected ID signature */

/* On Macs, we only need use the ISA I/O stuff until we do MEMORY_ON */
#ifdef CONFIG_MAC
#define LCSLOTBASE 0xfee00000
#define MMIOBASE 0x40000
#endif

#define CHIP_EISA_ID_SIG 0x630E   /*  Product ID Code for Crystal Chip (CS8900 spec 4.3) */
#define CHIP_EISA_ID_SIG_STR "0x630E"

#ifdef IBMEIPKT
#define EISA_ID_SIG 0x4D24	/*  IBM */
#define PART_NO_SIG 0x1010	/*  IBM */
#define MONGOOSE_BIT 0x0000	/*  IBM */
#else
#define EISA_ID_SIG 0x630E	/*  PnP Vendor ID (same as chip id for Crystal board) */
#define PART_NO_SIG 0x4000	/*  ID code CS8920 board (PnP Vendor Product code) */
#define MONGOOSE_BIT 0x2000	/*  PART_NO_SIG + MONGOOSE_BUT => ID of mongoose */
#endif

#define PRODUCT_ID_ADD 0x0002   /*  Address of product ID */

/*  Mask to find out the types of  registers */
#define REG_TYPE_MASK 0x001F

/*  Eeprom Commands */
#define ERSE_WR_ENBL 0x00F0
#define ERSE_WR_DISABLE 0x0000

/*  Defines Control/Config register quintuplet numbers */
#define RX_BUF_CFG 0x0003
#define RX_CONTROL 0x0005
#define TX_CFG 0x0007
#define TX_COMMAND 0x0009
#define BUF_CFG 0x000B
#define LINE_CONTROL 0x0013
#define SELF_CONTROL 0x0015
#define BUS_CONTROL 0x0017
#define TEST_CONTROL 0x0019

/*  Defines Status/Count registers quintuplet numbers */
#define RX_EVENT 0x0004
#define TX_EVENT 0x0008
#define BUF_EVENT 0x000C
#define RX_MISS_COUNT 0x0010
#define TX_COL_COUNT 0x0012
#define LINE_STATUS 0x0014
#define SELF_STATUS 0x0016
#define BUS_STATUS 0x0018
#define TDR 0x001C

/* PP_RxCFG - Receive  Configuration and Interrupt Mask bit definition -  Read/write */
#define SKIP_1 0x0040
#define RX_STREAM_ENBL 0x0080
#define RX_OK_ENBL 0x0100
#define RX_DMA_ONLY 0x0200
#define AUTO_RX_DMA 0x0400
#define BUFFER_CRC 0x0800
#define RX_CRC_ERROR_ENBL 0x1000
#define RX_RUNT_ENBL 0x2000
#define RX_EXTRA_DATA_ENBL 0x4000

/* PP_RxCTL - Receive Control bit definition - Read/write */
#define RX_IA_HASH_ACCEPT 0x0040
#define RX_PROM_ACCEPT 0x0080
#define RX_OK_ACCEPT 0x0100
#define RX_MULTCAST_ACCEPT 0x0200
#define RX_IA_ACCEPT 0x0400
#define RX_BROADCAST_ACCEPT 0x0800
#define RX_BAD_CRC_ACCEPT 0x1000
#define RX_RUNT_ACCEPT 0x2000
#define RX_EXTRA_DATA_ACCEPT 0x4000
#define RX_ALL_ACCEPT (RX_PROM_ACCEPT|RX_BAD_CRC_ACCEPT|RX_RUNT_ACCEPT|RX_EXTRA_DATA_ACCEPT)
/*  Default receive mode - individually addressed, broadcast, and error free */
#define DEF_RX_ACCEPT (RX_IA_ACCEPT | RX_BROADCAST_ACCEPT | RX_OK_ACCEPT)

/* PP_TxCFG - Transmit Configuration Interrupt Mask bit definition - Read/write */
#define TX_LOST_CRS_ENBL 0x0040
#define TX_SQE_ERROR_ENBL 0x0080
#define TX_OK_ENBL 0x0100
#define TX_LATE_COL_ENBL 0x0200
#define TX_JBR_ENBL 0x0400
#define TX_ANY_COL_ENBL 0x0800
#define TX_16_COL_ENBL 0x8000

/* PP_TxCMD - Transmit Command bit definition - Read-only */
#define TX_START_4_BYTES 0x0000
#define TX_START_64_BYTES 0x0040
#define TX_START_128_BYTES 0x0080
#define TX_START_ALL_BYTES 0x00C0
#define TX_FORCE 0x0100
#define TX_ONE_COL 0x0200
#define TX_TWO_PART_DEFF_DISABLE 0x0400
#define TX_NO_CRC 0x1000
#define TX_RUNT 0x2000

/* PP_BufCFG - Buffer Configuration Interrupt Mask bit definition - Read/write */
#define GENERATE_SW_INTERRUPT 0x0040
#define RX_DMA_ENBL 0x0080
#define READY_FOR_TX_ENBL 0x0100
#define TX_UNDERRUN_ENBL 0x0200
#define RX_MISS_ENBL 0x0400
#define RX_128_BYTE_ENBL 0x0800
#define TX_COL_COUNT_OVRFLOW_ENBL 0x1000
#define RX_MISS_COUNT_OVRFLOW_ENBL 0x2000
#define RX_DEST_MATCH_ENBL 0x8000

/* PP_LineCTL - Line Control bit definition - Read/write */
#define SERIAL_RX_ON 0x0040
#define SERIAL_TX_ON 0x0080
#define AUI_ONLY 0x0100
#define AUTO_AUI_10BASET 0x0200
#define MODIFIED_BACKOFF 0x0800
#define NO_AUTO_POLARITY 0x1000
#define TWO_PART_DEFDIS 0x2000
#define LOW_RX_SQUELCH 0x4000

/* PP_SelfCTL - Software Self Control bit definition - Read/write */
#define POWER_ON_RESET 0x0040
#define SW_STOP 0x0100
#define SLEEP_ON 0x0200
#define AUTO_WAKEUP 0x0400
#define HCB0_ENBL 0x1000
#define HCB1_ENBL 0x2000
#define HCB0 0x4000
#define HCB1 0x8000

/* PP_BusCTL - ISA Bus Control bit definition - Read/write */
#define RESET_RX_DMA 0x0040
#define MEMORY_ON 0x0400
#define DMA_BURST_MODE 0x0800
#define IO_CHANNEL_READY_ON 0x1000
#define RX_DMA_SIZE_64K 0x2000
#define ENABLE_IRQ 0x8000

/* PP_TestCTL - Test Control bit definition - Read/write */
#define LINK_OFF 0x0080
#define ENDEC_LOOPBACK 0x0200
#define AUI_LOOPBACK 0x0400
#define BACKOFF_OFF 0x0800
#define FDX_8900 0x4000
#define FAST_TEST 0x8000

/* PP_RxEvent - Receive Event Bit definition - Read-only */
#define RX_IA_HASHED 0x0040
#define RX_DRIBBLE 0x0080
#define RX_OK 0x0100
#define RX_HASHED 0x0200
#define RX_IA 0x0400
#define RX_BROADCAST 0x0800
#define RX_CRC_ERROR 0x1000
#define RX_RUNT 0x2000
#define RX_EXTRA_DATA 0x4000

#define HASH_INDEX_MASK 0x0FC00

/* PP_TxEvent - Transmit Event Bit definition - Read-only */
#define TX_LOST_CRS 0x0040
#define TX_SQE_ERROR 0x0080
#define TX_OK 0x0100
#define TX_LATE_COL 0x0200
#define TX_JBR 0x0400
#define TX_16_COL 0x8000
#define TX_SEND_OK_BITS (TX_OK|TX_LOST_CRS)
#define TX_COL_COUNT_MASK 0x7800

/* PP_BufEvent - Buffer Event Bit definition - Read-only */
#define SW_INTERRUPT 0x0040
#define RX_DMA 0x0080
#define READY_FOR_TX 0x0100
#define TX_UNDERRUN 0x0200
#define RX_MISS 0x0400
#define RX_128_BYTE 0x0800
#define TX_COL_OVRFLW 0x1000
#define RX_MISS_OVRFLW 0x2000
#define RX_DEST_MATCH 0x8000

/* PP_LineST - Ethernet Line Status bit definition - Read-only */
#define LINK_OK 0x0080
#define AUI_ON 0x0100
#define TENBASET_ON 0x0200
#define POLARITY_OK 0x1000
#define CRS_OK 0x4000

/* PP_SelfST - Chip Software Status bit definition */
#define ACTIVE_33V 0x0040
#define INIT_DONE 0x0080
#define SI_BUSY 0x0100
#define EEPROM_PRESENT 0x0200
#define EEPROM_OK 0x0400
#define EL_PRESENT 0x0800
#define EE_SIZE_64 0x1000

/* PP_BusST - ISA Bus Status bit definition */
#define TX_BID_ERROR 0x0080
#define READY_FOR_TX_NOW 0x0100

/* PP_AutoNegCTL - Auto Negotiation Control bit definition */
#define RE_NEG_NOW 0x0040
#define ALLOW_FDX 0x0080
#define AUTO_NEG_ENABLE 0x0100
#define NLP_ENABLE 0x0200
#define FORCE_FDX 0x8000
#define AUTO_NEG_BITS (FORCE_FDX|NLP_ENABLE|AUTO_NEG_ENABLE)
#define AUTO_NEG_MASK (FORCE_FDX|NLP_ENABLE|AUTO_NEG_ENABLE|ALLOW_FDX|RE_NEG_NOW)

/* PP_AutoNegST - Auto Negotiation Status bit definition */
#define AUTO_NEG_BUSY 0x0080
#define FLP_LINK 0x0100
#define FLP_LINK_GOOD 0x0800
#define LINK_FAULT 0x1000
#define HDX_ACTIVE 0x4000
#define FDX_ACTIVE 0x8000

/*  The following block defines the ISQ event types */
#define ISQ_RECEIVER_EVENT 0x04
#define ISQ_TRANSMITTER_EVENT 0x08
#define ISQ_BUFFER_EVENT 0x0c
#define ISQ_RX_MISS_EVENT 0x10
#define ISQ_TX_COL_EVENT 0x12

#define ISQ_EVENT_MASK 0x003F   /*  ISQ mask to find out type of event */
#define ISQ_HIST 16		/*  small history buffer */
#define AUTOINCREMENT 0x8000	/*  Bit mask to set bit-15 for autoincrement */

#define TXRXBUFSIZE 0x0600
#define RXDMABUFSIZE 0x8000
#define RXDMASIZE 0x4000
#define TXRX_LENGTH_MASK 0x07FF

/*  rx options bits */
#define RCV_WITH_RXON	1       /*  Set SerRx ON */
#define RCV_COUNTS	2       /*  Use Framecnt1 */
#define RCV_PONG	4       /*  Pong respondent */
#define RCV_DONG	8       /*  Dong operation */
#define RCV_POLLING	0x10	/*  Poll RxEvent */
#define RCV_ISQ		0x20	/*  Use ISQ, int */
#define RCV_AUTO_DMA	0x100	/*  Set AutoRxDMAE */
#define RCV_DMA		0x200	/*  Set RxDMA only */
#define RCV_DMA_ALL	0x400	/*  Copy all DMA'ed */
#define RCV_FIXED_DATA	0x800	/*  Every frame same */
#define RCV_IO		0x1000	/*  Use ISA IO only */
#define RCV_MEMORY	0x2000	/*  Use ISA Memory */

#define RAM_SIZE	0x1000       /*  The card has 4k bytes or RAM */
#define PKT_START PP_TxFrame  /*  Start of packet RAM */

#define RX_FRAME_PORT	CS89x0_PORT(0x0000)
#define TX_FRAME_PORT RX_FRAME_PORT
#define TX_CMD_PORT	CS89x0_PORT(0x0004)
#define TX_NOW		0x0000       /*  Tx packet after   5 bytes copied */
#define TX_AFTER_381	0x0040       /*  Tx packet after 381 bytes copied */
#define TX_AFTER_ALL	0x00c0       /*  Tx packet after all bytes copied */
#define TX_LEN_PORT	CS89x0_PORT(0x0006)
#define ISQ_PORT	CS89x0_PORT(0x0008)
#define ADD_PORT	CS89x0_PORT(0x000A)
#define DATA_PORT	CS89x0_PORT(0x000C)

#define EEPROM_WRITE_EN		0x00F0
#define EEPROM_WRITE_DIS	0x0000
#define EEPROM_WRITE_CMD	0x0100
#define EEPROM_READ_CMD		0x0200

/*  Receive Header */
/*  Description of header of each packet in receive area of memory */
#define RBUF_EVENT_LOW	0   /*  Low byte of RxEvent - status of received frame */
#define RBUF_EVENT_HIGH	1   /*  High byte of RxEvent - status of received frame */
#define RBUF_LEN_LOW	2   /*  Length of received data - low byte */
#define RBUF_LEN_HI	3   /*  Length of received data - high byte */
#define RBUF_HEAD_LEN	4   /*  Length of this header */

#define CHIP_READ 0x1   /*  Used to mark state of the repins code (chip or dma) */
#define DMA_READ 0x2   /*  Used to mark state of the repins code (chip or dma) */

/*  for bios scan */
/*  */
#ifdef CSDEBUG
/*  use these values for debugging bios scan */
#define BIOS_START_SEG 0x00000
#define BIOS_OFFSET_INC 0x0010
#else
#define BIOS_START_SEG 0x0c000
#define BIOS_OFFSET_INC 0x0200
#endif

#define BIOS_LAST_OFFSET 0x0fc00

/*  Byte offsets into the EEPROM configuration buffer */
#define ISA_CNF_OFFSET 0x6
#define TX_CTL_OFFSET (ISA_CNF_OFFSET + 8)			/*  8900 eeprom */
#define AUTO_NEG_CNF_OFFSET (ISA_CNF_OFFSET + 8)		/*  8920 eeprom */

  /*  the assumption here is that the bits in the eeprom are generally  */
  /*  in the same position as those in the autonegctl register. */
  /*  Of course the IMM bit is not in that register so it must be  */
  /*  masked out */
#define EE_FORCE_FDX  0x8000
#define EE_NLP_ENABLE 0x0200
#define EE_AUTO_NEG_ENABLE 0x0100
#define EE_ALLOW_FDX 0x0080
#define EE_AUTO_NEG_CNF_MASK (EE_FORCE_FDX|EE_NLP_ENABLE|EE_AUTO_NEG_ENABLE|EE_ALLOW_FDX)

#define IMM_BIT 0x0040		/*  ignore missing media	 */

#define ADAPTER_CNF_OFFSET (AUTO_NEG_CNF_OFFSET + 2)
#define A_CNF_10B_T 0x0001
#define A_CNF_AUI 0x0002
#define A_CNF_10B_2 0x0004
#define A_CNF_MEDIA_TYPE 0x0070
#define A_CNF_MEDIA_AUTO 0x0070
#define A_CNF_MEDIA_10B_T 0x0020
#define A_CNF_MEDIA_AUI 0x0040
#define A_CNF_MEDIA_10B_2 0x0010
#define A_CNF_DC_DC_POLARITY 0x0080
#define A_CNF_NO_AUTO_POLARITY 0x2000
#define A_CNF_LOW_RX_SQUELCH 0x4000
#define A_CNF_EXTND_10B_2 0x8000

#define PACKET_PAGE_OFFSET 0x8

/*  Bit definitions for the ISA configuration word from the EEPROM */
#define INT_NO_MASK 0x000F
#define DMA_NO_MASK 0x0070
#define ISA_DMA_SIZE 0x0200
#define ISA_AUTO_RxDMA 0x0400
#define ISA_RxDMA 0x0800
#define DMA_BURST 0x1000
#define STREAM_TRANSFER 0x2000
#define ANY_ISA_DMA (ISA_AUTO_RxDMA | ISA_RxDMA)

/*  DMA controller registers */
#define DMA_BASE 0x00     /*  DMA controller base */
#define DMA_BASE_2 0x0C0    /*  DMA controller base */

#define DMA_STAT 0x0D0    /*  DMA controller status register */
#define DMA_MASK 0x0D4    /*  DMA controller mask register */
#define DMA_MODE 0x0D6    /*  DMA controller mode register */
#define DMA_RESETFF 0x0D8    /*  DMA controller first/last flip flop */

/*  DMA data */
#define DMA_DISABLE 0x04     /*  Disable channel n */
#define DMA_ENABLE 0x00     /*  Enable channel n */
/*  Demand transfers, incr. address, auto init, writes, ch. n */
#define DMA_RX_MODE 0x14
/*  Demand transfers, incr. address, auto init, reads, ch. n */
#define DMA_TX_MODE 0x18

#define DMA_SIZE (16*1024) /*  Size of dma buffer - 16k */

#define CS8900 0x0000
#define CS8920 0x4000   
#define CS8920M 0x6000   
#define REVISON_BITS 0x1F00
#define EEVER_NUMBER 0x12
#define CHKSUM_LEN 0x14
#define CHKSUM_VAL 0x0000
#define START_EEPROM_DATA 0x001c /*  Offset into eeprom for start of data */
#define IRQ_MAP_EEPROM_DATA 0x0046 /*  Offset into eeprom for the IRQ map */
#define IRQ_MAP_LEN 0x0004 /*  No of bytes to read for the IRQ map */
#define PNP_IRQ_FRMT 0x0022 /*  PNP small item IRQ format */
#ifdef CONFIG_SH_HICOSH4
#define CS8900_IRQ_MAP 0x0002 /* HiCO-SH4 board has its IRQ on #1 */
#else
#define CS8900_IRQ_MAP 0x1c20 /*  This IRQ map is fixed */
#endif

#define CS8920_NO_INTS 0x0F   /*  Max CS8920 interrupt select # */

#define PNP_ADD_PORT 0x0279
#define PNP_WRITE_PORT 0x0A79

#define GET_PNP_ISA_STRUCT 0x40
#define PNP_ISA_STRUCT_LEN 0x06
#define PNP_CSN_CNT_OFF 0x01
#define PNP_RD_PORT_OFF 0x02
#define PNP_FUNCTION_OK 0x00
#define PNP_WAKE 0x03
#define PNP_RSRC_DATA 0x04
#define PNP_RSRC_READY 0x01
#define PNP_STATUS 0x05
#define PNP_ACTIVATE 0x30
#define PNP_CNF_IO_H 0x60
#define PNP_CNF_IO_L 0x61
#define PNP_CNF_INT 0x70
#define PNP_CNF_DMA 0x74
#define PNP_CNF_MEM 0x48

#define BIT0 1
#define BIT15 0x8000

