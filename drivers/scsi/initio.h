/**************************************************************************
 * Initio 9100 device driver for Linux.
 *
 * Copyright (c) 1994-1998 Initio Corporation
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * --------------------------------------------------------------------------
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU General Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 **************************************************************************/


#include <linux/config.h>
#include <linux/types.h>

#define ULONG   unsigned long
#define USHORT  unsigned short
#define UCHAR   unsigned char
#define BYTE    unsigned char
#define WORD    unsigned short
#define DWORD   unsigned long
#define UBYTE   unsigned char
#define UWORD   unsigned short
#define UDWORD  unsigned long
#define U32     u32

#define TOTAL_SG_ENTRY		32
#define MAX_SUPPORTED_ADAPTERS  8
#define MAX_OFFSET		15
#define MAX_TARGETS		16

typedef struct {
	unsigned short base;
	unsigned short vec;
} i91u_config;

/***************************************/
/*  Tulip Configuration Register Set */
/***************************************/
#define TUL_PVID        0x00	/* Vendor ID                    */
#define TUL_PDID        0x02	/* Device ID                    */
#define TUL_PCMD        0x04	/* Command                      */
#define TUL_PSTUS       0x06	/* Status                       */
#define TUL_PRID        0x08	/* Revision number              */
#define TUL_PPI         0x09	/* Programming interface        */
#define TUL_PSC         0x0A	/* Sub Class                    */
#define TUL_PBC         0x0B	/* Base Class                   */
#define TUL_PCLS        0x0C	/* Cache line size              */
#define TUL_PLTR        0x0D	/* Latency timer                */
#define TUL_PHDT        0x0E	/* Header type                  */
#define TUL_PBIST       0x0F	/* BIST                         */
#define TUL_PBAD        0x10	/* Base address                 */
#define TUL_PBAD1       0x14	/* Base address                 */
#define TUL_PBAD2       0x18	/* Base address                 */
#define TUL_PBAD3       0x1C	/* Base address                 */
#define TUL_PBAD4       0x20	/* Base address                 */
#define TUL_PBAD5       0x24	/* Base address                 */
#define TUL_PRSVD       0x28	/* Reserved                     */
#define TUL_PRSVD1      0x2C	/* Reserved                     */
#define TUL_PRAD        0x30	/* Expansion ROM base address   */
#define TUL_PRSVD2      0x34	/* Reserved                     */
#define TUL_PRSVD3      0x38	/* Reserved                     */
#define TUL_PINTL       0x3C	/* Interrupt line               */
#define TUL_PINTP       0x3D	/* Interrupt pin                */
#define TUL_PIGNT       0x3E	/* MIN_GNT                      */
#define TUL_PMGNT       0x3F	/* MAX_GNT                      */

/************************/
/*  Jasmin Register Set */
/************************/
#define TUL_HACFG0      0x40	/* H/A Configuration Register 0         */
#define TUL_HACFG1      0x41	/* H/A Configuration Register 1         */
#define TUL_HACFG2      0x42	/* H/A Configuration Register 2         */

#define TUL_SDCFG0      0x44	/* SCSI Device Configuration 0          */
#define TUL_SDCFG1      0x45	/* SCSI Device Configuration 1          */
#define TUL_SDCFG2      0x46	/* SCSI Device Configuration 2          */
#define TUL_SDCFG3      0x47	/* SCSI Device Configuration 3          */

#define TUL_GINTS       0x50	/* Global Interrupt Status Register     */
#define TUL_GIMSK       0x52	/* Global Interrupt MASK Register       */
#define TUL_GCTRL       0x54	/* Global Control Register              */
#define TUL_GCTRL_EEPROM_BIT    0x04
#define TUL_GCTRL1      0x55	/* Global Control Register              */
#define TUL_DMACFG      0x5B	/* DMA configuration                    */
#define TUL_NVRAM       0x5D	/* Non-volatile RAM port                */

#define TUL_SCnt0       0x80	/* 00 R/W Transfer Counter Low          */
#define TUL_SCnt1       0x81	/* 01 R/W Transfer Counter Mid          */
#define TUL_SCnt2       0x82	/* 02 R/W Transfer Count High           */
#define TUL_SFifoCnt    0x83	/* 03 R   FIFO counter                  */
#define TUL_SIntEnable  0x84	/* 03 W   Interrupt enble               */
#define TUL_SInt        0x84	/* 04 R   Interrupt Register            */
#define TUL_SCtrl0      0x85	/* 05 W   Control 0                     */
#define TUL_SStatus0    0x85	/* 05 R   Status 0                      */
#define TUL_SCtrl1      0x86	/* 06 W   Control 1                     */
#define TUL_SStatus1    0x86	/* 06 R   Status 1                      */
#define TUL_SConfig     0x87	/* 07 W   Configuration                 */
#define TUL_SStatus2    0x87	/* 07 R   Status 2                      */
#define TUL_SPeriod     0x88	/* 08 W   Sync. Transfer Period & Offset */
#define TUL_SOffset     0x88	/* 08 R   Offset                        */
#define TUL_SScsiId     0x89	/* 09 W   SCSI ID                       */
#define TUL_SBusId      0x89	/* 09 R   SCSI BUS ID                   */
#define TUL_STimeOut    0x8A	/* 0A W   Sel/Resel Time Out Register   */
#define TUL_SIdent      0x8A	/* 0A R   Identify Message Register     */
#define TUL_SAvail      0x8A	/* 0A R   Availiable Counter Register   */
#define TUL_SData       0x8B	/* 0B R/W SCSI data in/out              */
#define TUL_SFifo       0x8C	/* 0C R/W FIFO                          */
#define TUL_SSignal     0x90	/* 10 R/W SCSI signal in/out            */
#define TUL_SCmd        0x91	/* 11 R/W Command                       */
#define TUL_STest0      0x92	/* 12 R/W Test0                         */
#define TUL_STest1      0x93	/* 13 R/W Test1                         */
#define TUL_SCFG1	0x94	/* 14 R/W Configuration                 */

#define TUL_XAddH       0xC0	/*DMA Transfer Physical Address         */
#define TUL_XAddW       0xC8	/*DMA Current Transfer Physical Address */
#define TUL_XCntH       0xD0	/*DMA Transfer Counter                  */
#define TUL_XCntW       0xD4	/*DMA Current Transfer Counter          */
#define TUL_XCmd        0xD8	/*DMA Command Register                  */
#define TUL_Int         0xDC	/*Interrupt Register                    */
#define TUL_XStatus     0xDD	/*DMA status Register                   */
#define TUL_Mask        0xE0	/*Interrupt Mask Register               */
#define TUL_XCtrl       0xE4	/*DMA Control Register                  */
#define TUL_XCtrl1      0xE5	/*DMA Control Register 1                */
#define TUL_XFifo       0xE8	/*DMA FIFO                              */

#define TUL_WCtrl       0xF7	/*Bus master wait state control         */
#define TUL_DCtrl       0xFB	/*DMA delay control                     */

/*----------------------------------------------------------------------*/
/*   bit definition for Command register of Configuration Space Header  */
/*----------------------------------------------------------------------*/
#define BUSMS           0x04	/* BUS MASTER Enable                    */
#define IOSPA           0x01	/* IO Space Enable                      */

/*----------------------------------------------------------------------*/
/* Command Codes of Tulip SCSI Command register                         */
/*----------------------------------------------------------------------*/
#define TSC_EN_RESEL    0x80	/* Enable Reselection                   */
#define TSC_CMD_COMP    0x84	/* Command Complete Sequence            */
#define TSC_SEL         0x01	/* Select Without ATN Sequence          */
#define TSC_SEL_ATN     0x11	/* Select With ATN Sequence             */
#define TSC_SEL_ATN_DMA 0x51	/* Select With ATN Sequence with DMA    */
#define TSC_SEL_ATN3    0x31	/* Select With ATN3 Sequence            */
#define TSC_SEL_ATNSTOP 0x12	/* Select With ATN and Stop Sequence    */
#define TSC_SELATNSTOP  0x1E	/* Select With ATN and Stop Sequence    */

#define TSC_SEL_ATN_DIRECT_IN   0x95	/* Select With ATN Sequence     */
#define TSC_SEL_ATN_DIRECT_OUT  0x15	/* Select With ATN Sequence     */
#define TSC_SEL_ATN3_DIRECT_IN  0xB5	/* Select With ATN3 Sequence    */
#define TSC_SEL_ATN3_DIRECT_OUT 0x35	/* Select With ATN3 Sequence    */
#define TSC_XF_DMA_OUT_DIRECT   0x06	/* DMA Xfer Infomation out      */
#define TSC_XF_DMA_IN_DIRECT    0x86	/* DMA Xfer Infomation in       */

#define TSC_XF_DMA_OUT  0x43	/* DMA Xfer Infomation out              */
#define TSC_XF_DMA_IN   0xC3	/* DMA Xfer Infomation in               */
#define TSC_XF_FIFO_OUT 0x03	/* FIFO Xfer Infomation out             */
#define TSC_XF_FIFO_IN  0x83	/* FIFO Xfer Infomation in              */

#define TSC_MSG_ACCEPT  0x0F	/* Message Accept                       */

/*----------------------------------------------------------------------*/
/* bit definition for Tulip SCSI Control 0 Register                     */
/*----------------------------------------------------------------------*/
#define TSC_RST_SEQ     0x20	/* Reset sequence counter               */
#define TSC_FLUSH_FIFO  0x10	/* Flush FIFO                           */
#define TSC_ABT_CMD     0x04	/* Abort command (sequence)             */
#define TSC_RST_CHIP    0x02	/* Reset SCSI Chip                      */
#define TSC_RST_BUS     0x01	/* Reset SCSI Bus                       */

/*----------------------------------------------------------------------*/
/* bit definition for Tulip SCSI Control 1 Register                     */
/*----------------------------------------------------------------------*/
#define TSC_EN_SCAM     0x80	/* Enable SCAM                          */
#define TSC_TIMER       0x40	/* Select timeout unit                  */
#define TSC_EN_SCSI2    0x20	/* SCSI-2 mode                          */
#define TSC_PWDN        0x10	/* Power down mode                      */
#define TSC_WIDE_CPU    0x08	/* Wide CPU                             */
#define TSC_HW_RESELECT 0x04	/* Enable HW reselect                   */
#define TSC_EN_BUS_OUT  0x02	/* Enable SCSI data bus out latch       */
#define TSC_EN_BUS_IN   0x01	/* Enable SCSI data bus in latch        */

/*----------------------------------------------------------------------*/
/* bit definition for Tulip SCSI Configuration Register                 */
/*----------------------------------------------------------------------*/
#define TSC_EN_LATCH    0x80	/* Enable phase latch                   */
#define TSC_INITIATOR   0x40	/* Initiator mode                       */
#define TSC_EN_SCSI_PAR 0x20	/* Enable SCSI parity                   */
#define TSC_DMA_8BIT    0x10	/* Alternate dma 8-bits mode            */
#define TSC_DMA_16BIT   0x08	/* Alternate dma 16-bits mode           */
#define TSC_EN_WDACK    0x04	/* Enable DACK while wide SCSI xfer     */
#define TSC_ALT_PERIOD  0x02	/* Alternate sync period mode           */
#define TSC_DIS_SCSIRST 0x01	/* Disable SCSI bus reset us            */

#define TSC_INITDEFAULT (TSC_INITIATOR | TSC_EN_LATCH | TSC_ALT_PERIOD | TSC_DIS_SCSIRST)

#define TSC_WIDE_SCSI   0x80	/* Enable Wide SCSI                     */

/*----------------------------------------------------------------------*/
/* bit definition for Tulip SCSI signal Register                        */
/*----------------------------------------------------------------------*/
#define TSC_RST_ACK     0x00	/* Release ACK signal                   */
#define TSC_RST_ATN     0x00	/* Release ATN signal                   */
#define TSC_RST_BSY     0x00	/* Release BSY signal                   */

#define TSC_SET_ACK     0x40	/* ACK signal                           */
#define TSC_SET_ATN     0x08	/* ATN signal                           */

#define TSC_REQI        0x80	/* REQ signal                           */
#define TSC_ACKI        0x40	/* ACK signal                           */
#define TSC_BSYI        0x20	/* BSY signal                           */
#define TSC_SELI        0x10	/* SEL signal                           */
#define TSC_ATNI        0x08	/* ATN signal                           */
#define TSC_MSGI        0x04	/* MSG signal                           */
#define TSC_CDI         0x02	/* C/D signal                           */
#define TSC_IOI         0x01	/* I/O signal                           */


/*----------------------------------------------------------------------*/
/* bit definition for Tulip SCSI Status 0 Register                      */
/*----------------------------------------------------------------------*/
#define TSS_INT_PENDING 0x80	/* Interrupt pending            */
#define TSS_SEQ_ACTIVE  0x40	/* Sequencer active             */
#define TSS_XFER_CNT    0x20	/* Transfer counter zero        */
#define TSS_FIFO_EMPTY  0x10	/* FIFO empty                   */
#define TSS_PAR_ERROR   0x08	/* SCSI parity error            */
#define TSS_PH_MASK     0x07	/* SCSI phase mask              */

/*----------------------------------------------------------------------*/
/* bit definition for Tulip SCSI Status 1 Register                      */
/*----------------------------------------------------------------------*/
#define TSS_STATUS_RCV  0x08	/* Status received              */
#define TSS_MSG_SEND    0x40	/* Message sent                 */
#define TSS_CMD_PH_CMP  0x20	/* command phase done              */
#define TSS_DATA_PH_CMP 0x10	/* Data phase done              */
#define TSS_STATUS_SEND 0x08	/* Status sent                  */
#define TSS_XFER_CMP    0x04	/* Transfer completed           */
#define TSS_SEL_CMP     0x02	/* Selection completed          */
#define TSS_ARB_CMP     0x01	/* Arbitration completed        */

/*----------------------------------------------------------------------*/
/* bit definition for Tulip SCSI Status 2 Register                      */
/*----------------------------------------------------------------------*/
#define TSS_CMD_ABTED   0x80	/* Command aborted              */
#define TSS_OFFSET_0    0x40	/* Offset counter zero          */
#define TSS_FIFO_FULL   0x20	/* FIFO full                    */
#define TSS_TIMEOUT_0   0x10	/* Timeout counter zero         */
#define TSS_BUSY_RLS    0x08	/* Busy release                 */
#define TSS_PH_MISMATCH 0x04	/* Phase mismatch               */
#define TSS_SCSI_BUS_EN 0x02	/* SCSI data bus enable         */
#define TSS_SCSIRST     0x01	/* SCSI bus reset in progress   */

/*----------------------------------------------------------------------*/
/* bit definition for Tulip SCSI Interrupt Register                     */
/*----------------------------------------------------------------------*/
#define TSS_RESEL_INT   0x80	/* Reselected interrupt         */
#define TSS_SEL_TIMEOUT 0x40	/* Selected/reselected timeout  */
#define TSS_BUS_SERV    0x20
#define TSS_SCSIRST_INT 0x10	/* SCSI bus reset detected      */
#define TSS_DISC_INT    0x08	/* Disconnected interrupt       */
#define TSS_SEL_INT     0x04	/* Select interrupt             */
#define TSS_SCAM_SEL    0x02	/* SCAM selected                */
#define TSS_FUNC_COMP   0x01

/*----------------------------------------------------------------------*/
/* SCSI Phase Codes.                                                    */
/*----------------------------------------------------------------------*/
#define DATA_OUT        0
#define DATA_IN         1	/* 4                            */
#define CMD_OUT         2
#define STATUS_IN       3	/* 6                            */
#define MSG_OUT         6	/* 3                            */
#define MSG_IN          7



/*----------------------------------------------------------------------*/
/* Command Codes of Tulip xfer Command register                         */
/*----------------------------------------------------------------------*/
#define TAX_X_FORC      0x02
#define TAX_X_ABT       0x04
#define TAX_X_CLR_FIFO  0x08

#define TAX_X_IN        0x21
#define TAX_X_OUT       0x01
#define TAX_SG_IN       0xA1
#define TAX_SG_OUT      0x81

/*----------------------------------------------------------------------*/
/* Tulip Interrupt Register                                             */
/*----------------------------------------------------------------------*/
#define XCMP            0x01
#define FCMP            0x02
#define XABT            0x04
#define XERR            0x08
#define SCMP            0x10
#define IPEND           0x80

/*----------------------------------------------------------------------*/
/* Tulip DMA Status Register                                            */
/*----------------------------------------------------------------------*/
#define XPEND           0x01	/* Transfer pending             */
#define FEMPTY          0x02	/* FIFO empty                   */



/*----------------------------------------------------------------------*/
/* bit definition for TUL_GCTRL                                         */
/*----------------------------------------------------------------------*/
#define EXTSG           0x80
#define EXTAD           0x60
#define SEG4K           0x08
#define EEPRG           0x04
#define MRMUL           0x02

/*----------------------------------------------------------------------*/
/* bit definition for TUL_NVRAM                                         */
/*----------------------------------------------------------------------*/
#define SE2CS           0x08
#define SE2CLK          0x04
#define SE2DO           0x02
#define SE2DI           0x01


/************************************************************************/
/*              Scatter-Gather Element Structure                        */
/************************************************************************/
typedef struct SG_Struc {
	U32 SG_Ptr;		/* Data Pointer */
	U32 SG_Len;		/* Data Length */
} SG;

/***********************************************************************
		SCSI Control Block
************************************************************************/
typedef struct Scsi_Ctrl_Blk {
	struct Scsi_Ctrl_Blk *SCB_NxtScb;
	UBYTE SCB_Status;	/*4 */
	UBYTE SCB_NxtStat;	/*5 */
	UBYTE SCB_Mode;		/*6 */
	UBYTE SCB_Msgin;	/*7 SCB_Res0 */
	UWORD SCB_SGIdx;	/*8 */
	UWORD SCB_SGMax;	/*A */
#ifdef ALPHA
	U32 SCB_Reserved[2];	/*C */
#else
	U32 SCB_Reserved[3];	/*C */
#endif

	U32 SCB_XferLen;	/*18 Current xfer len           */
	U32 SCB_TotXLen;	/*1C Total xfer len             */
	U32 SCB_PAddr;		/*20 SCB phy. Addr. */

	UBYTE SCB_Opcode;	/*24 SCB command code */
	UBYTE SCB_Flags;	/*25 SCB Flags */
	UBYTE SCB_Target;	/*26 Target Id */
	UBYTE SCB_Lun;		/*27 Lun */
	U32 SCB_BufPtr;		/*28 Data Buffer Pointer */
	U32 SCB_BufLen;		/*2C Data Allocation Length */
	UBYTE SCB_SGLen;	/*30 SG list # */
	UBYTE SCB_SenseLen;	/*31 Sense Allocation Length */
	UBYTE SCB_HaStat;	/*32 */
	UBYTE SCB_TaStat;	/*33 */
	UBYTE SCB_CDBLen;	/*34 CDB Length */
	UBYTE SCB_Ident;	/*35 Identify */
	UBYTE SCB_TagMsg;	/*36 Tag Message */
	UBYTE SCB_TagId;	/*37 Queue Tag */
	UBYTE SCB_CDB[12];	/*38 */
	U32 SCB_SGPAddr;	/*44 SG List/Sense Buf phy. Addr. */
	U32 SCB_SensePtr;	/*48 Sense data pointer */
	void (*SCB_Post) (BYTE *, BYTE *);	/*4C POST routine */
	struct scsi_cmnd *SCB_Srb;	/*50 SRB Pointer */
	SG SCB_SGList[TOTAL_SG_ENTRY];	/*54 Start of SG list */
} SCB;

/* Bit Definition for SCB_Status */
#define SCB_RENT        0x01
#define SCB_PEND        0x02
#define SCB_CONTIG      0x04	/* Contigent Allegiance */
#define SCB_SELECT      0x08
#define SCB_BUSY        0x10
#define SCB_DONE        0x20


/* Opcodes of SCB_Opcode */
#define ExecSCSI        0x1
#define BusDevRst       0x2
#define AbortCmd        0x3


/* Bit Definition for SCB_Mode */
#define SCM_RSENS       0x01	/* request sense mode */


/* Bit Definition for SCB_Flags */
#define SCF_DONE        0x01
#define SCF_POST        0x02
#define SCF_SENSE       0x04
#define SCF_DIR         0x18
#define SCF_NO_DCHK     0x00
#define SCF_DIN         0x08
#define SCF_DOUT        0x10
#define SCF_NO_XF       0x18
#define SCF_WR_VF       0x20	/* Write verify turn on         */
#define SCF_POLL        0x40
#define SCF_SG          0x80

/* Error Codes for SCB_HaStat */
#define HOST_SEL_TOUT   0x11
#define HOST_DO_DU      0x12
#define HOST_BUS_FREE   0x13
#define HOST_BAD_PHAS   0x14
#define HOST_INV_CMD    0x16
#define HOST_ABORTED    0x1A	/* 07/21/98 */
#define HOST_SCSI_RST   0x1B
#define HOST_DEV_RST    0x1C

/* Error Codes for SCB_TaStat */
#define TARGET_CHKCOND  0x02
#define TARGET_BUSY     0x08
#define INI_QUEUE_FULL	0x28

/* SCSI MESSAGE */
#define MSG_COMP        0x00
#define MSG_EXTEND      0x01
#define MSG_SDP         0x02
#define MSG_RESTORE     0x03
#define MSG_DISC        0x04
#define MSG_IDE         0x05
#define MSG_ABORT       0x06
#define MSG_REJ         0x07
#define MSG_NOP         0x08
#define MSG_PARITY      0x09
#define MSG_LINK_COMP   0x0A
#define MSG_LINK_FLAG   0x0B
#define MSG_DEVRST      0x0C
#define MSG_ABORT_TAG   0x0D

/* Queue tag msg: Simple_quque_tag, Head_of_queue_tag, Ordered_queue_tag */
#define MSG_STAG        0x20
#define MSG_HTAG        0x21
#define MSG_OTAG        0x22

#define MSG_IGNOREWIDE  0x23

#define MSG_IDENT   0x80

/***********************************************************************
		Target Device Control Structure
**********************************************************************/

typedef struct Tar_Ctrl_Struc {
	UWORD TCS_Flags;	/* 0 */
	UBYTE TCS_JS_Period;	/* 2 */
	UBYTE TCS_SConfig0;	/* 3 */

	UWORD TCS_DrvFlags;	/* 4 */
	UBYTE TCS_DrvHead;	/* 6 */
	UBYTE TCS_DrvSector;	/* 7 */
} TCS;

/***********************************************************************
		Target Device Control Structure
**********************************************************************/

/* Bit Definition for TCF_Flags */
#define TCF_SCSI_RATE           0x0007
#define TCF_EN_DISC             0x0008
#define TCF_NO_SYNC_NEGO        0x0010
#define TCF_NO_WDTR             0x0020
#define TCF_EN_255              0x0040
#define TCF_EN_START            0x0080
#define TCF_WDTR_DONE           0x0100
#define TCF_SYNC_DONE           0x0200
#define TCF_BUSY                0x0400


/* Bit Definition for TCF_DrvFlags */
#define TCF_DRV_BUSY            0x01	/* Indicate target busy(driver) */
#define TCF_DRV_EN_TAG          0x0800
#define TCF_DRV_255_63          0x0400

typedef struct I91u_Adpt_Struc {
	UWORD ADPT_BIOS;	/* 0 */
	UWORD ADPT_BASE;	/* 1 */
	UBYTE ADPT_Bus;		/* 2 */
	UBYTE ADPT_Device;	/* 3 */
	UBYTE ADPT_INTR;	/* 4 */
} INI_ADPT_STRUCT;


/***********************************************************************
	      Host Adapter Control Structure
************************************************************************/
typedef struct Ha_Ctrl_Struc {
	UWORD HCS_Base;		/* 00 */
	UWORD HCS_BIOS;		/* 02 */
	UBYTE HCS_Intr;		/* 04 */
	UBYTE HCS_SCSI_ID;	/* 05 */
	UBYTE HCS_MaxTar;	/* 06 */
	UBYTE HCS_NumScbs;	/* 07 */

	UBYTE HCS_Flags;	/* 08 */
	UBYTE HCS_Index;	/* 09 */
	UBYTE HCS_HaId;		/* 0A */
	UBYTE HCS_Config;	/* 0B */
	UWORD HCS_IdMask;	/* 0C */
	UBYTE HCS_Semaph;	/* 0E */
	UBYTE HCS_Phase;	/* 0F */
	UBYTE HCS_JSStatus0;	/* 10 */
	UBYTE HCS_JSInt;	/* 11 */
	UBYTE HCS_JSStatus1;	/* 12 */
	UBYTE HCS_SConf1;	/* 13 */

	UBYTE HCS_Msg[8];	/* 14 */
	SCB *HCS_NxtAvail;	/* 1C */
	SCB *HCS_Scb;		/* 20 */
	SCB *HCS_ScbEnd;	/* 24 */
	SCB *HCS_NxtPend;	/* 28 */
	SCB *HCS_NxtContig;	/* 2C */
	SCB *HCS_ActScb;	/* 30 */
	TCS *HCS_ActTcs;	/* 34 */

	SCB *HCS_FirstAvail;	/* 38 */
	SCB *HCS_LastAvail;	/* 3C */
	SCB *HCS_FirstPend;	/* 40 */
	SCB *HCS_LastPend;	/* 44 */
	SCB *HCS_FirstBusy;	/* 48 */
	SCB *HCS_LastBusy;	/* 4C */
	SCB *HCS_FirstDone;	/* 50 */
	SCB *HCS_LastDone;	/* 54 */
	UBYTE HCS_MaxTags[16];	/* 58 */
	UBYTE HCS_ActTags[16];	/* 68 */
	TCS HCS_Tcs[MAX_TARGETS];	/* 78 */
	spinlock_t HCS_AvailLock;
	spinlock_t HCS_SemaphLock;
	struct pci_dev *pci_dev;
} HCS;

/* Bit Definition for HCB_Config */
#define HCC_SCSI_RESET          0x01
#define HCC_EN_PAR              0x02
#define HCC_ACT_TERM1           0x04
#define HCC_ACT_TERM2           0x08
#define HCC_AUTO_TERM           0x10
#define HCC_EN_PWR              0x80

/* Bit Definition for HCB_Flags */
#define HCF_EXPECT_DISC         0x01
#define HCF_EXPECT_SELECT       0x02
#define HCF_EXPECT_RESET        0x10
#define HCF_EXPECT_DONE_DISC    0x20

/******************************************************************
	Serial EEProm
*******************************************************************/

typedef struct _NVRAM_SCSI {	/* SCSI channel configuration   */
	UCHAR NVM_ChSCSIID;	/* 0Ch -> Channel SCSI ID       */
	UCHAR NVM_ChConfig1;	/* 0Dh -> Channel config 1      */
	UCHAR NVM_ChConfig2;	/* 0Eh -> Channel config 2      */
	UCHAR NVM_NumOfTarg;	/* 0Fh -> Number of SCSI target */
	/* SCSI target configuration    */
	UCHAR NVM_Targ0Config;	/* 10h -> Target 0 configuration */
	UCHAR NVM_Targ1Config;	/* 11h -> Target 1 configuration */
	UCHAR NVM_Targ2Config;	/* 12h -> Target 2 configuration */
	UCHAR NVM_Targ3Config;	/* 13h -> Target 3 configuration */
	UCHAR NVM_Targ4Config;	/* 14h -> Target 4 configuration */
	UCHAR NVM_Targ5Config;	/* 15h -> Target 5 configuration */
	UCHAR NVM_Targ6Config;	/* 16h -> Target 6 configuration */
	UCHAR NVM_Targ7Config;	/* 17h -> Target 7 configuration */
	UCHAR NVM_Targ8Config;	/* 18h -> Target 8 configuration */
	UCHAR NVM_Targ9Config;	/* 19h -> Target 9 configuration */
	UCHAR NVM_TargAConfig;	/* 1Ah -> Target A configuration */
	UCHAR NVM_TargBConfig;	/* 1Bh -> Target B configuration */
	UCHAR NVM_TargCConfig;	/* 1Ch -> Target C configuration */
	UCHAR NVM_TargDConfig;	/* 1Dh -> Target D configuration */
	UCHAR NVM_TargEConfig;	/* 1Eh -> Target E configuration */
	UCHAR NVM_TargFConfig;	/* 1Fh -> Target F configuration */
} NVRAM_SCSI;

typedef struct _NVRAM {
/*----------header ---------------*/
	USHORT NVM_Signature;	/* 0,1: Signature */
	UCHAR NVM_Size;		/* 2:   Size of data structure */
	UCHAR NVM_Revision;	/* 3:   Revision of data structure */
	/* ----Host Adapter Structure ---- */
	UCHAR NVM_ModelByte0;	/* 4:   Model number (byte 0) */
	UCHAR NVM_ModelByte1;	/* 5:   Model number (byte 1) */
	UCHAR NVM_ModelInfo;	/* 6:   Model information         */
	UCHAR NVM_NumOfCh;	/* 7:   Number of SCSI channel */
	UCHAR NVM_BIOSConfig1;	/* 8:   BIOS configuration 1  */
	UCHAR NVM_BIOSConfig2;	/* 9:   BIOS configuration 2  */
	UCHAR NVM_HAConfig1;	/* A:   Hoat adapter configuration 1 */
	UCHAR NVM_HAConfig2;	/* B:   Hoat adapter configuration 2 */
	NVRAM_SCSI NVM_SCSIInfo[2];
	UCHAR NVM_reserved[10];
	/* ---------- CheckSum ----------       */
	USHORT NVM_CheckSum;	/* 0x3E, 0x3F: Checksum of NVRam        */
} NVRAM, *PNVRAM;

/* Bios Configuration for nvram->BIOSConfig1                            */
#define NBC1_ENABLE             0x01	/* BIOS enable                  */
#define NBC1_8DRIVE             0x02	/* Support more than 2 drives   */
#define NBC1_REMOVABLE          0x04	/* Support removable drive      */
#define NBC1_INT19              0x08	/* Intercept int 19h            */
#define NBC1_BIOSSCAN           0x10	/* Dynamic BIOS scan            */
#define NBC1_LUNSUPPORT         0x40	/* Support LUN                  */

/* HA Configuration Byte 1                                              */
#define NHC1_BOOTIDMASK 0x0F	/* Boot ID number               */
#define NHC1_LUNMASK    0x70	/* Boot LUN number              */
#define NHC1_CHANMASK   0x80	/* Boot Channel number          */

/* Bit definition for nvram->SCSIconfig1                                */
#define NCC1_BUSRESET           0x01	/* Reset SCSI bus at power up   */
#define NCC1_PARITYCHK          0x02	/* SCSI parity enable           */
#define NCC1_ACTTERM1           0x04	/* Enable active terminator 1   */
#define NCC1_ACTTERM2           0x08	/* Enable active terminator 2   */
#define NCC1_AUTOTERM           0x10	/* Enable auto terminator       */
#define NCC1_PWRMGR             0x80	/* Enable power management      */

/* Bit definition for SCSI Target configuration byte                    */
#define NTC_DISCONNECT          0x08	/* Enable SCSI disconnect       */
#define NTC_SYNC                0x10	/* SYNC_NEGO                    */
#define NTC_NO_WDTR             0x20	/* SYNC_NEGO                    */
#define NTC_1GIGA               0x40	/* 255 head / 63 sectors (64/32) */
#define NTC_SPINUP              0x80	/* Start disk drive             */

/*      Default NVRam values                                            */
#define INI_SIGNATURE           0xC925
#define NBC1_DEFAULT            (NBC1_ENABLE)
#define NCC1_DEFAULT            (NCC1_BUSRESET | NCC1_AUTOTERM | NCC1_PARITYCHK)
#define NTC_DEFAULT             (NTC_NO_WDTR | NTC_1GIGA | NTC_DISCONNECT)

/* SCSI related definition                                              */
#define DISC_NOT_ALLOW          0x80	/* Disconnect is not allowed    */
#define DISC_ALLOW              0xC0	/* Disconnect is allowed        */
#define SCSICMD_RequestSense    0x03

typedef struct _HCSinfo {
	ULONG base;
	UCHAR vec;
	UCHAR bios;		/* High byte of BIOS address */
	USHORT BaseAndBios;	/* high byte: pHcsInfo->bios,low byte:pHcsInfo->base */
} HCSINFO;

#define TUL_RD(x,y)             (UCHAR)(inb(  (int)((ULONG)(x+y)) ))
#define TUL_RDLONG(x,y)         (ULONG)(inl((int)((ULONG)(x+y)) ))
#define TUL_WR(     adr,data)   outb( (UCHAR)(data), (int)(adr))
#define TUL_WRSHORT(adr,data)   outw( (UWORD)(data), (int)(adr))
#define TUL_WRLONG( adr,data)   outl( (ULONG)(data), (int)(adr))

#define SCSI_ABORT_SNOOZE 0
#define SCSI_ABORT_SUCCESS 1
#define SCSI_ABORT_PENDING 2
#define SCSI_ABORT_BUSY 3
#define SCSI_ABORT_NOT_RUNNING 4
#define SCSI_ABORT_ERROR 5

#define SCSI_RESET_SNOOZE 0
#define SCSI_RESET_PUNT 1
#define SCSI_RESET_SUCCESS 2
#define SCSI_RESET_PENDING 3
#define SCSI_RESET_WAKEUP 4
#define SCSI_RESET_NOT_RUNNING 5
#define SCSI_RESET_ERROR 6

#define SCSI_RESET_SYNCHRONOUS		0x01
#define SCSI_RESET_ASYNCHRONOUS		0x02
#define SCSI_RESET_SUGGEST_BUS_RESET	0x04
#define SCSI_RESET_SUGGEST_HOST_RESET	0x08

#define SCSI_RESET_BUS_RESET 0x100
#define SCSI_RESET_HOST_RESET 0x200
#define SCSI_RESET_ACTION   0xff

extern void init_i91uAdapter_table(void);
extern int Addi91u_into_Adapter_table(WORD, WORD, BYTE, BYTE, BYTE);
extern int tul_ReturnNumberOfAdapters(void);
extern void get_tulipPCIConfig(HCS * pHCB, int iChannel_index);
extern int init_tulip(HCS * pHCB, SCB * pSCB, int tul_num_scb, BYTE * pbBiosAdr, int reset_time);
extern SCB *tul_alloc_scb(HCS * pHCB);
extern int tul_abort_srb(HCS * pHCB, struct scsi_cmnd * pSRB);
extern void tul_exec_scb(HCS * pHCB, SCB * pSCB);
extern void tul_release_scb(HCS * pHCB, SCB * pSCB);
extern void tul_stop_bm(HCS * pHCB);
extern int tul_reset_scsi(HCS * pCurHcb, int seconds);
extern int tul_isr(HCS * pHCB);
extern int tul_reset(HCS * pHCB, struct scsi_cmnd * pSRB, unsigned char target);
extern int tul_reset_scsi_bus(HCS * pCurHcb);
extern int tul_device_reset(HCS * pCurHcb, struct scsi_cmnd *pSrb,
		unsigned int target, unsigned int ResetFlags);
				/* ---- EXTERNAL VARIABLES ---- */
extern HCS tul_hcs[];
