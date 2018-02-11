/* SPDX-License-Identifier: GPL-2.0 */
/************************************************************************/
/*									*/
/*	dc395x.h							*/
/*									*/
/*	Device Driver for Tekram DC395(U/UW/F), DC315(U)		*/
/*	PCI SCSI Bus Master Host Adapter				*/
/*	(SCSI chip set used Tekram ASIC TRM-S1040)			*/
/*									*/
/************************************************************************/
#ifndef DC395x_H
#define DC395x_H

/************************************************************************/
/*									*/
/*	Initial values							*/
/*									*/
/************************************************************************/
#define DC395x_MAX_CMD_QUEUE		32
/* #define DC395x_MAX_QTAGS		32 */
#define DC395x_MAX_QTAGS		16
#define DC395x_MAX_SCSI_ID		16
#define DC395x_MAX_CMD_PER_LUN		DC395x_MAX_QTAGS
#define DC395x_MAX_SG_TABLESIZE		64	/* HW limitation			*/
#define DC395x_MAX_SG_LISTENTRY		64	/* Must be equal or lower to previous	*/
						/* item					*/
#define DC395x_MAX_SRB_CNT		63
/* #define DC395x_MAX_CAN_QUEUE		7 * DC395x_MAX_QTAGS */
#define DC395x_MAX_CAN_QUEUE		DC395x_MAX_SRB_CNT
#define DC395x_END_SCAN			2
#define DC395x_SEL_TIMEOUT		153	/* 250 ms selection timeout (@ 40 MHz)	*/
#define DC395x_MAX_RETRIES		3

#if 0
#define SYNC_FIRST
#endif

#define NORM_REC_LVL			0

/************************************************************************/
/*									*/
/*	Various definitions						*/
/*									*/
/************************************************************************/
#define BIT31				0x80000000
#define BIT30				0x40000000
#define BIT29				0x20000000
#define BIT28				0x10000000
#define BIT27				0x08000000
#define BIT26				0x04000000
#define BIT25				0x02000000
#define BIT24				0x01000000
#define BIT23				0x00800000
#define BIT22				0x00400000
#define BIT21				0x00200000
#define BIT20				0x00100000
#define BIT19				0x00080000
#define BIT18				0x00040000
#define BIT17				0x00020000
#define BIT16				0x00010000
#define BIT15				0x00008000
#define BIT14				0x00004000
#define BIT13				0x00002000
#define BIT12				0x00001000
#define BIT11				0x00000800
#define BIT10				0x00000400
#define BIT9				0x00000200
#define BIT8				0x00000100
#define BIT7				0x00000080
#define BIT6				0x00000040
#define BIT5				0x00000020
#define BIT4				0x00000010
#define BIT3				0x00000008
#define BIT2				0x00000004
#define BIT1				0x00000002
#define BIT0				0x00000001

/* UnitCtrlFlag */
#define UNIT_ALLOCATED			BIT0
#define UNIT_INFO_CHANGED		BIT1
#define FORMATING_MEDIA			BIT2
#define UNIT_RETRY			BIT3

/* UnitFlags */
#define DASD_SUPPORT			BIT0
#define SCSI_SUPPORT			BIT1
#define ASPI_SUPPORT			BIT2

/* SRBState machine definition */
#define SRB_FREE			0x0000
#define SRB_WAIT			0x0001
#define SRB_READY			0x0002
#define SRB_MSGOUT			0x0004	/* arbitration+msg_out 1st byte		*/
#define SRB_MSGIN			0x0008
#define SRB_EXTEND_MSGIN		0x0010
#define SRB_COMMAND			0x0020
#define SRB_START_			0x0040	/* arbitration+msg_out+command_out	*/
#define SRB_DISCONNECT			0x0080
#define SRB_DATA_XFER			0x0100
#define SRB_XFERPAD			0x0200
#define SRB_STATUS			0x0400
#define SRB_COMPLETED			0x0800
#define SRB_ABORT_SENT			0x1000
#define SRB_DO_SYNC_NEGO		0x2000
#define SRB_DO_WIDE_NEGO		0x4000
#define SRB_UNEXPECT_RESEL		0x8000

/************************************************************************/
/*									*/
/*	ACB Config							*/
/*									*/
/************************************************************************/
#define HCC_WIDE_CARD			0x20
#define HCC_SCSI_RESET			0x10
#define HCC_PARITY			0x08
#define HCC_AUTOTERM			0x04
#define HCC_LOW8TERM			0x02
#define HCC_UP8TERM			0x01

/* ACBFlag */
#define RESET_DEV			BIT0
#define RESET_DETECT			BIT1
#define RESET_DONE			BIT2

/* DCBFlag */
#define ABORT_DEV_			BIT0

/* SRBstatus */
#define SRB_OK				BIT0
#define ABORTION			BIT1
#define OVER_RUN			BIT2
#define UNDER_RUN			BIT3
#define PARITY_ERROR			BIT4
#define SRB_ERROR			BIT5

/* SRBFlag */
#define DATAOUT				BIT7
#define DATAIN				BIT6
#define RESIDUAL_VALID			BIT5
#define ENABLE_TIMER			BIT4
#define RESET_DEV0			BIT2
#define ABORT_DEV			BIT1
#define AUTO_REQSENSE			BIT0

/* Adapter status */
#define H_STATUS_GOOD			0
#define H_SEL_TIMEOUT			0x11
#define H_OVER_UNDER_RUN		0x12
#define H_UNEXP_BUS_FREE		0x13
#define H_TARGET_PHASE_F		0x14
#define H_INVALID_CCB_OP		0x16
#define H_LINK_CCB_BAD			0x17
#define H_BAD_TARGET_DIR		0x18
#define H_DUPLICATE_CCB			0x19
#define H_BAD_CCB_OR_SG			0x1A
#define H_ABORT				0x0FF

/* SCSI BUS Status byte codes */
#define SCSI_STAT_GOOD			0x0	/* Good status				*/
#define SCSI_STAT_CHECKCOND		0x02	/* SCSI Check Condition			*/
#define SCSI_STAT_CONDMET		0x04	/* Condition Met			*/
#define SCSI_STAT_BUSY			0x08	/* Target busy status			*/
#define SCSI_STAT_INTER			0x10	/* Intermediate status			*/
#define SCSI_STAT_INTERCONDMET		0x14	/* Intermediate condition met		*/
#define SCSI_STAT_RESCONFLICT		0x18	/* Reservation conflict			*/
#define SCSI_STAT_CMDTERM		0x22	/* Command Terminated			*/
#define SCSI_STAT_QUEUEFULL		0x28	/* Queue Full				*/
#define SCSI_STAT_UNEXP_BUS_F		0xFD	/* Unexpect Bus Free			*/
#define SCSI_STAT_BUS_RST_DETECT	0xFE	/* Scsi Bus Reset detected		*/
#define SCSI_STAT_SEL_TIMEOUT		0xFF	/* Selection Time out			*/

/* Sync_Mode */
#define SYNC_WIDE_TAG_ATNT_DISABLE	0
#define SYNC_NEGO_ENABLE		BIT0
#define SYNC_NEGO_DONE			BIT1
#define WIDE_NEGO_ENABLE		BIT2
#define WIDE_NEGO_DONE			BIT3
#define WIDE_NEGO_STATE			BIT4
#define EN_TAG_QUEUEING			BIT5
#define EN_ATN_STOP			BIT6

#define SYNC_NEGO_OFFSET		15

/* SCSI MSG BYTE */
#define MSG_COMPLETE			0x00
#define MSG_EXTENDED			0x01
#define MSG_SAVE_PTR			0x02
#define MSG_RESTORE_PTR			0x03
#define MSG_DISCONNECT			0x04
#define MSG_INITIATOR_ERROR		0x05
#define MSG_ABORT			0x06
#define MSG_REJECT_			0x07
#define MSG_NOP				0x08
#define MSG_PARITY_ERROR		0x09
#define MSG_LINK_CMD_COMPL		0x0A
#define MSG_LINK_CMD_COMPL_FLG		0x0B
#define MSG_BUS_RESET			0x0C
#define MSG_ABORT_TAG			0x0D
#define MSG_SIMPLE_QTAG			0x20
#define MSG_HEAD_QTAG			0x21
#define MSG_ORDER_QTAG			0x22
#define MSG_IGNOREWIDE			0x23
#define MSG_IDENTIFY			0x80
#define MSG_HOST_ID			0xC0

/* SCSI STATUS BYTE */
#define STATUS_GOOD			0x00
#define CHECK_CONDITION_		0x02
#define STATUS_BUSY			0x08
#define STATUS_INTERMEDIATE		0x10
#define RESERVE_CONFLICT		0x18

/* cmd->result */
#define STATUS_MASK_			0xFF
#define MSG_MASK			0xFF00
#define RETURN_MASK			0xFF0000

/************************************************************************/
/*									*/
/*	Inquiry Data format						*/
/*									*/
/************************************************************************/
struct ScsiInqData
{						/* INQ					*/
	u8 DevType;				/* Periph Qualifier & Periph Dev Type	*/
	u8 RMB_TypeMod;				/* rem media bit & Dev Type Modifier	*/
	u8 Vers;				/* ISO, ECMA, & ANSI versions		*/
	u8 RDF;					/* AEN, TRMIOP, & response data format	*/
	u8 AddLen;				/* length of additional data		*/
	u8 Res1;				/* reserved				*/
	u8 Res2;				/* reserved				*/
	u8 Flags;				/* RelADr, Wbus32, Wbus16, Sync, etc.	*/
	u8 VendorID[8];				/* Vendor Identification		*/
	u8 ProductID[16];			/* Product Identification		*/
	u8 ProductRev[4];			/* Product Revision			*/
};

						/* Inquiry byte 0 masks			*/
#define SCSI_DEVTYPE			0x1F	/* Peripheral Device Type		*/
#define SCSI_PERIPHQUAL			0xE0	/* Peripheral Qualifier			*/
						/* Inquiry byte 1 mask			*/
#define SCSI_REMOVABLE_MEDIA		0x80	/* Removable Media bit (1=removable)	*/
						/* Peripheral Device Type definitions	*/
						/* See include/scsi/scsi.h		*/
#define TYPE_NODEV		SCSI_DEVTYPE	/* Unknown or no device type		*/
#ifndef TYPE_PRINTER				/*					*/
# define TYPE_PRINTER			0x02	/* Printer device			*/
#endif						/*					*/
#ifndef TYPE_COMM				/*					*/
# define TYPE_COMM			0x09	/* Communications device		*/
#endif

/************************************************************************/
/*									*/
/*	Inquiry flag definitions (Inq data byte 7)			*/
/*									*/
/************************************************************************/
#define SCSI_INQ_RELADR			0x80	/* device supports relative addressing	*/
#define SCSI_INQ_WBUS32			0x40	/* device supports 32 bit data xfers	*/
#define SCSI_INQ_WBUS16			0x20	/* device supports 16 bit data xfers	*/
#define SCSI_INQ_SYNC			0x10	/* device supports synchronous xfer	*/
#define SCSI_INQ_LINKED			0x08	/* device supports linked commands	*/
#define SCSI_INQ_CMDQUEUE		0x02	/* device supports command queueing	*/
#define SCSI_INQ_SFTRE			0x01	/* device supports soft resets		*/

#define ENABLE_CE			1
#define DISABLE_CE			0
#define EEPROM_READ			0x80

/************************************************************************/
/*									*/
/*	The PCI configuration register offset for TRM_S1040		*/
/*									*/
/************************************************************************/
#define TRM_S1040_ID			0x00	/* Vendor and Device ID			*/
#define TRM_S1040_COMMAND		0x04	/* PCI command register			*/
#define TRM_S1040_IOBASE		0x10	/* I/O Space base address		*/
#define TRM_S1040_ROMBASE		0x30	/* Expansion ROM Base Address		*/
#define TRM_S1040_INTLINE		0x3C	/* Interrupt line			*/

/************************************************************************/
/*									*/
/*	The SCSI register offset for TRM_S1040				*/
/*									*/
/************************************************************************/
#define TRM_S1040_SCSI_STATUS		0x80	/* SCSI Status (R)			*/
#define COMMANDPHASEDONE		0x2000	/* SCSI command phase done		*/
#define SCSIXFERDONE			0x0800	/* SCSI SCSI transfer done		*/
#define SCSIXFERCNT_2_ZERO		0x0100	/* SCSI SCSI transfer count to zero	*/
#define SCSIINTERRUPT			0x0080	/* SCSI interrupt pending		*/
#define COMMANDABORT			0x0040	/* SCSI command abort			*/
#define SEQUENCERACTIVE			0x0020	/* SCSI sequencer active		*/
#define PHASEMISMATCH			0x0010	/* SCSI phase mismatch			*/
#define PARITYERROR			0x0008	/* SCSI parity error			*/

#define PHASEMASK			0x0007	/* Phase MSG/CD/IO			*/
#define PH_DATA_OUT			0x00	/* Data out phase			*/
#define PH_DATA_IN			0x01	/* Data in phase			*/
#define PH_COMMAND			0x02	/* Command phase			*/
#define PH_STATUS			0x03	/* Status phase				*/
#define PH_BUS_FREE			0x05	/* Invalid phase used as bus free	*/
#define PH_MSG_OUT			0x06	/* Message out phase			*/
#define PH_MSG_IN			0x07	/* Message in phase			*/

#define TRM_S1040_SCSI_CONTROL		0x80	/* SCSI Control (W)			*/
#define DO_CLRATN			0x0400	/* Clear ATN				*/
#define DO_SETATN			0x0200	/* Set ATN				*/
#define DO_CMDABORT			0x0100	/* Abort SCSI command			*/
#define DO_RSTMODULE			0x0010	/* Reset SCSI chip			*/
#define DO_RSTSCSI			0x0008	/* Reset SCSI bus			*/
#define DO_CLRFIFO			0x0004	/* Clear SCSI transfer FIFO		*/
#define DO_DATALATCH			0x0002	/* Enable SCSI bus data input (latched)	*/
/* #define DO_DATALATCH			0x0000 */	/* KG: DISable SCSI bus data latch	*/
#define DO_HWRESELECT			0x0001	/* Enable hardware reselection		*/

#define TRM_S1040_SCSI_FIFOCNT		0x82	/* SCSI FIFO Counter 5bits(R)		*/
#define TRM_S1040_SCSI_SIGNAL		0x83	/* SCSI low level signal (R/W)		*/

#define TRM_S1040_SCSI_INTSTATUS	0x84	/* SCSI Interrupt Status (R)		*/
#define INT_SCAM			0x80	/* SCAM selection interrupt		*/
#define INT_SELECT			0x40	/* Selection interrupt			*/
#define INT_SELTIMEOUT			0x20	/* Selection timeout interrupt		*/
#define INT_DISCONNECT			0x10	/* Bus disconnected interrupt		*/
#define INT_RESELECTED			0x08	/* Reselected interrupt			*/
#define INT_SCSIRESET			0x04	/* SCSI reset detected interrupt	*/
#define INT_BUSSERVICE			0x02	/* Bus service interrupt		*/
#define INT_CMDDONE			0x01	/* SCSI command done interrupt		*/

#define TRM_S1040_SCSI_OFFSET		0x84	/* SCSI Offset Count (W)		*/

/************************************************************************/
/*									*/
/*	Bit		Name		Definition			*/
/*	---------	-------------	----------------------------	*/
/*	07-05	0	RSVD		Reversed. Always 0.		*/
/*	04	0	OFFSET4		Reversed for LVDS. Always 0.	*/
/*	03-00	0	OFFSET[03:00]	Offset number from 0 to 15	*/
/*									*/
/************************************************************************/

#define TRM_S1040_SCSI_SYNC		0x85	/* SCSI Synchronous Control (R/W)	*/
#define LVDS_SYNC			0x20	/* Enable LVDS synchronous		*/
#define WIDE_SYNC			0x10	/* Enable WIDE synchronous		*/
#define ALT_SYNC			0x08	/* Enable Fast-20 alternate synchronous	*/

/************************************************************************/
/*									*/
/*	SYNCM	7    6    5    4    3       2       1       0		*/
/*	Name	RSVD RSVD LVDS WIDE ALTPERD PERIOD2 PERIOD1 PERIOD0	*/
/*	Default	0    0    0    0    0       0       0       0		*/
/*									*/
/*	Bit		Name		Definition			*/
/*	---------	-------------	---------------------------	*/
/*	07-06	0	RSVD		Reversed. Always read 0		*/
/*	05	0	LVDS		Reversed. Always read 0		*/
/*	04	0	WIDE/WSCSI	Enable wide (16-bits) SCSI	*/
/*					transfer.			*/
/*	03	0	ALTPERD/ALTPD	Alternate (Sync./Period) mode.	*/
/*									*/
/*			@@ When this bit is set,			*/
/*			   the synchronous period bits 2:0		*/
/*			   in the Synchronous Mode register		*/
/*			   are used to transfer data			*/
/*			   at the Fast-20 rate.				*/
/*			@@ When this bit is unset,			*/
/*			   the synchronous period bits 2:0		*/
/*			   in the Synchronous Mode Register		*/
/*			   are used to transfer data			*/
/*			   at the Fast-10 rate (or Fast-40 w/ LVDS).	*/
/*									*/
/*	02-00	0	PERIOD[2:0]/	Synchronous SCSI Transfer Rate.	*/
/*			SXPD[02:00]	These 3 bits specify		*/
/*					the Synchronous SCSI Transfer	*/
/*					Rate for Fast-20 and Fast-10.	*/
/*					These bits are also reset	*/
/*					by a SCSI Bus reset.		*/
/*									*/
/*	For Fast-10 bit ALTPD = 0 and LVDS = 0				*/
/*	and bit2,bit1,bit0 is defined as follows :			*/
/*									*/
/*	000	100ns, 10.0 MHz						*/
/*	001	150ns,  6.6 MHz						*/
/*	010	200ns,  5.0 MHz						*/
/*	011	250ns,  4.0 MHz						*/
/*	100	300ns,  3.3 MHz						*/
/*	101	350ns,  2.8 MHz						*/
/*	110	400ns,  2.5 MHz						*/
/*	111	450ns,  2.2 MHz						*/
/*									*/
/*	For Fast-20 bit ALTPD = 1 and LVDS = 0				*/
/*	and bit2,bit1,bit0 is defined as follows :			*/
/*									*/
/*	000	 50ns, 20.0 MHz						*/
/*	001	 75ns, 13.3 MHz						*/
/*	010	100ns, 10.0 MHz						*/
/*	011	125ns,  8.0 MHz						*/
/*	100	150ns,  6.6 MHz						*/
/*	101	175ns,  5.7 MHz						*/
/*	110	200ns,  5.0 MHz						*/
/*	111	250ns,  4.0 MHz   KG: Maybe 225ns, 4.4 MHz		*/
/*									*/
/*	For Fast-40 bit ALTPD = 0 and LVDS = 1				*/
/*	and bit2,bit1,bit0 is defined as follows :			*/
/*									*/
/*	000	 25ns, 40.0 MHz						*/
/*	001	 50ns, 20.0 MHz						*/
/*	010	 75ns, 13.3 MHz						*/
/*	011	100ns, 10.0 MHz						*/
/*	100	125ns,  8.0 MHz						*/
/*	101	150ns,  6.6 MHz						*/
/*	110	175ns,  5.7 MHz						*/
/*	111	200ns,  5.0 MHz						*/
/*									*/
/************************************************************************/

#define TRM_S1040_SCSI_TARGETID		0x86	/* SCSI Target ID (R/W)			*/
#define TRM_S1040_SCSI_IDMSG		0x87	/* SCSI Identify Message (R)		*/
#define TRM_S1040_SCSI_HOSTID		0x87	/* SCSI Host ID (W)			*/
#define TRM_S1040_SCSI_COUNTER		0x88	/* SCSI Transfer Counter 24bits(R/W)	*/

#define TRM_S1040_SCSI_INTEN		0x8C	/* SCSI Interrupt Enable (R/W)		*/
#define EN_SCAM				0x80	/* Enable SCAM selection interrupt	*/
#define EN_SELECT			0x40	/* Enable selection interrupt		*/
#define EN_SELTIMEOUT			0x20	/* Enable selection timeout interrupt	*/
#define EN_DISCONNECT			0x10	/* Enable bus disconnected interrupt	*/
#define EN_RESELECTED			0x08	/* Enable reselected interrupt		*/
#define EN_SCSIRESET			0x04	/* Enable SCSI reset detected interrupt	*/
#define EN_BUSSERVICE			0x02	/* Enable bus service interrupt		*/
#define EN_CMDDONE			0x01	/* Enable SCSI command done interrupt	*/

#define TRM_S1040_SCSI_CONFIG0		0x8D	/* SCSI Configuration 0 (R/W)		*/
#define PHASELATCH			0x40	/* Enable phase latch			*/
#define INITIATOR			0x20	/* Enable initiator mode		*/
#define PARITYCHECK			0x10	/* Enable parity check			*/
#define BLOCKRST			0x01	/* Disable SCSI reset1			*/

#define TRM_S1040_SCSI_CONFIG1		0x8E	/* SCSI Configuration 1 (R/W)		*/
#define ACTIVE_NEGPLUS			0x10	/* Enhance active negation		*/
#define FILTER_DISABLE			0x08	/* Disable SCSI data filter		*/
#define FAST_FILTER			0x04	/* ?					*/
#define ACTIVE_NEG			0x02	/* Enable active negation		*/

#define TRM_S1040_SCSI_CONFIG2		0x8F	/* SCSI Configuration 2 (R/W)		*/
#define CFG2_WIDEFIFO			0x02	/*					*/

#define TRM_S1040_SCSI_COMMAND		0x90	/* SCSI Command (R/W)			*/
#define SCMD_COMP			0x12	/* Command complete			*/
#define SCMD_SEL_ATN			0x60	/* Selection with ATN			*/
#define SCMD_SEL_ATN3			0x64	/* Selection with ATN3			*/
#define SCMD_SEL_ATNSTOP		0xB8	/* Selection with ATN and Stop		*/
#define SCMD_FIFO_OUT			0xC0	/* SCSI FIFO transfer out		*/
#define SCMD_DMA_OUT			0xC1	/* SCSI DMA transfer out		*/
#define SCMD_FIFO_IN			0xC2	/* SCSI FIFO transfer in		*/
#define SCMD_DMA_IN			0xC3	/* SCSI DMA transfer in			*/
#define SCMD_MSGACCEPT			0xD8	/* Message accept			*/

/************************************************************************/
/*									*/
/*	Code	Command Description					*/
/*	----	----------------------------------------		*/
/*	02	Enable reselection with FIFO				*/
/*	40	Select without ATN with FIFO				*/
/*	60	Select with ATN with FIFO				*/
/*	64	Select with ATN3 with FIFO				*/
/*	A0	Select with ATN and stop with FIFO			*/
/*	C0	Transfer information out with FIFO			*/
/*	C1	Transfer information out with DMA			*/
/*	C2	Transfer information in with FIFO			*/
/*	C3	Transfer information in with DMA			*/
/*	12	Initiator command complete with FIFO			*/
/*	50	Initiator transfer information out sequence without ATN	*/
/*		with FIFO						*/
/*	70	Initiator transfer information out sequence with ATN	*/
/*		with FIFO						*/
/*	74	Initiator transfer information out sequence with ATN3	*/
/*		with FIFO						*/
/*	52	Initiator transfer information in sequence without ATN	*/
/*		with FIFO						*/
/*	72	Initiator transfer information in sequence with ATN	*/
/*		with FIFO						*/
/*	76	Initiator transfer information in sequence with ATN3	*/
/*		with FIFO						*/
/*	90	Initiator transfer information out command complete	*/
/*		with FIFO						*/
/*	92	Initiator transfer information in command complete	*/
/*		with FIFO						*/
/*	D2	Enable selection					*/
/*	08	Reselection						*/
/*	48	Disconnect command with FIFO				*/
/*	88	Terminate command with FIFO				*/
/*	C8	Target command complete with FIFO			*/
/*	18	SCAM Arbitration/ Selection				*/
/*	5A	Enable reselection					*/
/*	98	Select without ATN with FIFO				*/
/*	B8	Select with ATN with FIFO				*/
/*	D8	Message Accepted					*/
/*	58	NOP							*/
/*									*/
/************************************************************************/

#define TRM_S1040_SCSI_TIMEOUT		0x91	/* SCSI Time Out Value (R/W)		*/
#define TRM_S1040_SCSI_FIFO		0x98	/* SCSI FIFO (R/W)			*/

#define TRM_S1040_SCSI_TCR0		0x9C	/* SCSI Target Control 0 (R/W)		*/
#define TCR0_WIDE_NEGO_DONE		0x8000	/* Wide nego done			*/
#define TCR0_SYNC_NEGO_DONE		0x4000	/* Synchronous nego done		*/
#define TCR0_ENABLE_LVDS		0x2000	/* Enable LVDS synchronous		*/
#define TCR0_ENABLE_WIDE		0x1000	/* Enable WIDE synchronous		*/
#define TCR0_ENABLE_ALT			0x0800	/* Enable alternate synchronous		*/
#define TCR0_PERIOD_MASK		0x0700	/* Transfer rate			*/

#define TCR0_DO_WIDE_NEGO		0x0080	/* Do wide NEGO				*/
#define TCR0_DO_SYNC_NEGO		0x0040	/* Do sync NEGO				*/
#define TCR0_DISCONNECT_EN		0x0020	/* Disconnection enable			*/
#define TCR0_OFFSET_MASK		0x001F	/* Offset number			*/

#define TRM_S1040_SCSI_TCR1		0x9E	/* SCSI Target Control 1 (R/W)		*/
#define MAXTAG_MASK			0x7F00	/* Maximum tags (127)			*/
#define NON_TAG_BUSY			0x0080	/* Non tag command active		*/
#define ACTTAG_MASK			0x007F	/* Active tags				*/

/************************************************************************/
/*									*/
/*	The DMA register offset for TRM_S1040				*/
/*									*/
/************************************************************************/
#define TRM_S1040_DMA_COMMAND		0xA0	/* DMA Command (R/W)			*/
#define DMACMD_SG			0x02	/* Enable HW S/G support		*/
#define DMACMD_DIR			0x01	/* 1 = read from SCSI write to Host	*/
#define XFERDATAIN_SG			0x0103	/* Transfer data in  w/  SG		*/
#define XFERDATAOUT_SG			0x0102	/* Transfer data out w/  SG		*/
#define XFERDATAIN			0x0101	/* Transfer data in  w/o SG		*/
#define XFERDATAOUT			0x0100	/* Transfer data out w/o SG		*/

#define TRM_S1040_DMA_FIFOCNT		0xA1	/* DMA FIFO Counter (R)			*/

#define TRM_S1040_DMA_CONTROL		0xA1	/* DMA Control (W)			*/
#define DMARESETMODULE			0x10	/* Reset PCI/DMA module			*/
#define STOPDMAXFER			0x08	/* Stop  DMA transfer			*/
#define ABORTXFER			0x04	/* Abort DMA transfer			*/
#define CLRXFIFO			0x02	/* Clear DMA transfer FIFO		*/
#define STARTDMAXFER			0x01	/* Start DMA transfer			*/

#define TRM_S1040_DMA_FIFOSTAT		0xA2	/* DMA FIFO Status (R)			*/

#define TRM_S1040_DMA_STATUS		0xA3	/* DMA Interrupt Status (R/W)		*/
#define XFERPENDING			0x80	/* Transfer pending			*/
#define SCSIBUSY			0x40	/* SCSI busy				*/
#define GLOBALINT			0x20	/* DMA_INTEN bit 0-4 set		*/
#define FORCEDMACOMP			0x10	/* Force DMA transfer complete		*/
#define DMAXFERERROR			0x08	/* DMA transfer error			*/
#define DMAXFERABORT			0x04	/* DMA transfer abort			*/
#define DMAXFERCOMP			0x02	/* Bus Master XFER Complete status	*/
#define SCSICOMP			0x01	/* SCSI complete interrupt		*/

#define TRM_S1040_DMA_INTEN		0xA4	/* DMA Interrupt Enable (R/W)		*/
#define EN_FORCEDMACOMP			0x10	/* Force DMA transfer complete		*/
#define EN_DMAXFERERROR			0x08	/* DMA transfer error			*/
#define EN_DMAXFERABORT			0x04	/* DMA transfer abort			*/
#define EN_DMAXFERCOMP			0x02	/* Bus Master XFER Complete status	*/
#define EN_SCSIINTR			0x01	/* Enable SCSI complete interrupt	*/

#define TRM_S1040_DMA_CONFIG		0xA6	/* DMA Configuration (R/W)		*/
#define DMA_ENHANCE			0x8000	/* Enable DMA enhance feature (SG?)	*/
#define DMA_PCI_DUAL_ADDR		0x4000	/*					*/
#define DMA_CFG_RES			0x2000	/* Always 1				*/
#define DMA_AUTO_CLR_FIFO		0x1000	/* DISable DMA auto clear FIFO		*/
#define DMA_MEM_MULTI_READ		0x0800	/*					*/
#define DMA_MEM_WRITE_INVAL		0x0400	/* Memory write and invalidate		*/
#define DMA_FIFO_CTRL			0x0300	/* Control FIFO operation with DMA	*/
#define DMA_FIFO_HALF_HALF		0x0200	/* Keep half filled on both read/write	*/

#define TRM_S1040_DMA_XCNT		0xA8	/* DMA Transfer Counter (R/W), 24bits	*/
#define TRM_S1040_DMA_CXCNT		0xAC	/* DMA Current Transfer Counter (R)	*/
#define TRM_S1040_DMA_XLOWADDR		0xB0	/* DMA Transfer Physical Low Address	*/
#define TRM_S1040_DMA_XHIGHADDR		0xB4	/* DMA Transfer Physical High Address	*/

/************************************************************************/
/*									*/
/*	The general register offset for TRM_S1040			*/
/*									*/
/************************************************************************/
#define TRM_S1040_GEN_CONTROL		0xD4	/* Global Control			*/
#define CTRL_LED			0x80	/* Control onboard LED			*/
#define EN_EEPROM			0x10	/* Enable EEPROM programming		*/
#define DIS_TERM			0x08	/* Disable onboard termination		*/
#define AUTOTERM			0x04	/* Enable Auto SCSI terminator		*/
#define LOW8TERM			0x02	/* Enable Lower 8 bit SCSI terminator	*/
#define UP8TERM				0x01	/* Enable Upper 8 bit SCSI terminator	*/

#define TRM_S1040_GEN_STATUS		0xD5	/* Global Status			*/
#define GTIMEOUT			0x80	/* Global timer reach 0			*/
#define EXT68HIGH			0x40	/* Higher 8 bit connected externally	*/
#define INT68HIGH			0x20	/* Higher 8 bit connected internally	*/
#define CON5068				0x10	/* External 50/68 pin connected (low)	*/
#define CON68				0x08	/* Internal 68 pin connected (low)	*/
#define CON50				0x04	/* Internal 50 pin connected (low!)	*/
#define WIDESCSI			0x02	/* Wide SCSI card			*/
#define STATUS_LOAD_DEFAULT		0x01	/*					*/

#define TRM_S1040_GEN_NVRAM		0xD6	/* Serial NON-VOLATILE RAM port		*/
#define NVR_BITOUT			0x08	/* Serial data out			*/
#define NVR_BITIN			0x04	/* Serial data in			*/
#define NVR_CLOCK			0x02	/* Serial clock				*/
#define NVR_SELECT			0x01	/* Serial select			*/

#define TRM_S1040_GEN_EDATA		0xD7	/* Parallel EEPROM data port		*/
#define TRM_S1040_GEN_EADDRESS		0xD8	/* Parallel EEPROM address		*/
#define TRM_S1040_GEN_TIMER		0xDB	/* Global timer				*/

/************************************************************************/
/*									*/
/*	NvmTarCfg0: Target configuration byte 0 :..pDCB->DevMode	*/
/*									*/
/************************************************************************/
#define NTC_DO_WIDE_NEGO		0x20	/* Wide negotiate			*/
#define NTC_DO_TAG_QUEUEING		0x10	/* Enable SCSI tag queuing		*/
#define NTC_DO_SEND_START		0x08	/* Send start command SPINUP		*/
#define NTC_DO_DISCONNECT		0x04	/* Enable SCSI disconnect		*/
#define NTC_DO_SYNC_NEGO		0x02	/* Sync negotiation			*/
#define NTC_DO_PARITY_CHK		0x01	/* (it should define at NAC)		*/
						/* Parity check enable			*/

/************************************************************************/
/*									*/
/*	Nvram Initiater bits definition					*/
/*									*/
/************************************************************************/
#if 0
#define MORE2_DRV			BIT0
#define GREATER_1G			BIT1
#define RST_SCSI_BUS			BIT2
#define ACTIVE_NEGATION			BIT3
#define NO_SEEK				BIT4
#define LUN_CHECK			BIT5
#endif

/************************************************************************/
/*									*/
/*	Nvram Adapter Cfg bits definition				*/
/*									*/
/************************************************************************/
#define NAC_SCANLUN			0x20	/* Include LUN as BIOS device		*/
#define NAC_POWERON_SCSI_RESET		0x04	/* Power on reset enable		*/
#define NAC_GREATER_1G			0x02	/* > 1G support enable			*/
#define NAC_GT2DRIVES			0x01	/* Support more than 2 drives		*/
/* #define NAC_DO_PARITY_CHK		0x08 */	/* Parity check enable			*/

#endif
