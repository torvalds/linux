/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Advanced  Micro Devices Inc. AMD8111E Linux Network Driver
 * Copyright (C) 2003 Advanced Micro Devices
 *

Module Name:

    amd8111e.h

Abstract:

	 AMD8111 based 10/100 Ethernet Controller driver definitions.

Environment:

	Kernel Mode

Revision History:
	3.0.0
	   Initial Revision.
	3.0.1
*/

#ifndef _AMD811E_H
#define _AMD811E_H

/* Command style register access

Registers CMD0, CMD2, CMD3,CMD7 and INTEN0 uses a write access technique called command style access. It allows the write to selected bits of this register without altering the bits that are not selected. Command style registers are divided into 4 bytes that can be written independently. Higher order bit of each byte is the  value bit that specifies the value that will be written into the selected bits of register.

eg., if the value 10011010b is written into the least significant byte of a command style register, bits 1,3 and 4 of the register will be set to 1, and the other bits will not be altered. If the value 00011010b is written into the same byte, bits 1,3 and 4 will be cleared to 0 and the other bits will not be altered.

*/

/*  Offset for Memory Mapped Registers. */
/* 32 bit registers */

#define  ASF_STAT		0x00	/* ASF status register */
#define CHIPID			0x04	/* Chip ID register */
#define	MIB_DATA		0x10	/* MIB data register */
#define MIB_ADDR		0x14	/* MIB address register */
#define STAT0			0x30	/* Status0 register */
#define INT0			0x38	/* Interrupt0 register */
#define INTEN0			0x40	/* Interrupt0  enable register*/
#define CMD0			0x48	/* Command0 register */
#define CMD2			0x50	/* Command2 register */
#define CMD3			0x54	/* Command3 resiter */
#define CMD7			0x64	/* Command7 register */

#define CTRL1 			0x6C	/* Control1 register */
#define CTRL2 			0x70	/* Control2 register */

#define XMT_RING_LIMIT		0x7C	/* Transmit ring limit register */

#define AUTOPOLL0		0x88	/* Auto-poll0 register */
#define AUTOPOLL1		0x8A	/* Auto-poll1 register */
#define AUTOPOLL2		0x8C	/* Auto-poll2 register */
#define AUTOPOLL3		0x8E	/* Auto-poll3 register */
#define AUTOPOLL4		0x90	/* Auto-poll4 register */
#define	AUTOPOLL5		0x92	/* Auto-poll5 register */

#define AP_VALUE		0x98	/* Auto-poll value register */
#define DLY_INT_A		0xA8	/* Group A delayed interrupt register */
#define DLY_INT_B		0xAC	/* Group B delayed interrupt register */

#define FLOW_CONTROL		0xC8	/* Flow control register */
#define PHY_ACCESS		0xD0	/* PHY access register */

#define STVAL			0xD8	/* Software timer value register */

#define XMT_RING_BASE_ADDR0	0x100	/* Transmit ring0 base addr register */
#define XMT_RING_BASE_ADDR1	0x108	/* Transmit ring1 base addr register */
#define XMT_RING_BASE_ADDR2	0x110	/* Transmit ring2 base addr register */
#define XMT_RING_BASE_ADDR3	0x118	/* Transmit ring2 base addr register */

#define RCV_RING_BASE_ADDR0	0x120	/* Transmit ring0 base addr register */

#define PMAT0			0x190	/* OnNow pattern register0 */
#define PMAT1			0x194	/* OnNow pattern register1 */

/* 16bit registers */

#define XMT_RING_LEN0		0x140	/* Transmit Ring0 length register */
#define XMT_RING_LEN1		0x144	/* Transmit Ring1 length register */
#define XMT_RING_LEN2		0x148 	/* Transmit Ring2 length register */
#define XMT_RING_LEN3		0x14C	/* Transmit Ring3 length register */

#define RCV_RING_LEN0		0x150	/* Receive Ring0 length register */

#define SRAM_SIZE		0x178	/* SRAM size register */
#define SRAM_BOUNDARY		0x17A	/* SRAM boundary register */

/* 48bit register */

#define PADR			0x160	/* Physical address register */

#define IFS1			0x18C	/* Inter-frame spacing Part1 register */
#define IFS			0x18D	/* Inter-frame spacing register */
#define IPG			0x18E	/* Inter-frame gap register */
/* 64bit register */

#define LADRF			0x168	/* Logical address filter register */


/* Register Bit Definitions */
typedef enum {

	ASF_INIT_DONE		= (1 << 1),
	ASF_INIT_PRESENT	= (1 << 0),

}STAT_ASF_BITS;

typedef enum {

	MIB_CMD_ACTIVE		= (1 << 15 ),
	MIB_RD_CMD		= (1 << 13 ),
	MIB_CLEAR		= (1 << 12 ),
	MIB_ADDRESS		= (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3)|
					(1 << 4) | (1 << 5),
}MIB_ADDR_BITS;


typedef enum {

	PMAT_DET		= (1 << 12),
	MP_DET		        = (1 << 11),
	LC_DET			= (1 << 10),
	SPEED_MASK		= (1 << 9)|(1 << 8)|(1 << 7),
	FULL_DPLX		= (1 << 6),
	LINK_STATS		= (1 << 5),
	AUTONEG_COMPLETE	= (1 << 4),
	MIIPD			= (1 << 3),
	RX_SUSPENDED		= (1 << 2),
	TX_SUSPENDED		= (1 << 1),
	RUNNING			= (1 << 0),

}STAT0_BITS;

#define PHY_SPEED_10		0x2
#define PHY_SPEED_100		0x3

/* INT0				0x38, 32bit register */
typedef enum {

	INTR			= (1 << 31),
	PCSINT			= (1 << 28),
	LCINT			= (1 << 27),
	APINT5			= (1 << 26),
	APINT4			= (1 << 25),
	APINT3			= (1 << 24),
	TINT_SUM		= (1 << 23),
	APINT2			= (1 << 22),
	APINT1			= (1 << 21),
	APINT0			= (1 << 20),
	MIIPDTINT		= (1 << 19),
	MCCINT			= (1 << 17),
	MREINT			= (1 << 16),
	RINT_SUM		= (1 << 15),
	SPNDINT			= (1 << 14),
	MPINT			= (1 << 13),
	SINT			= (1 << 12),
	TINT3			= (1 << 11),
	TINT2			= (1 << 10),
	TINT1			= (1 << 9),
	TINT0			= (1 << 8),
	UINT			= (1 << 7),
	STINT			= (1 << 4),
	RINT0			= (1 << 0),

}INT0_BITS;

typedef enum {

	VAL3			= (1 << 31),   /* VAL bit for byte 3 */
	VAL2			= (1 << 23),   /* VAL bit for byte 2 */
	VAL1			= (1 << 15),   /* VAL bit for byte 1 */
	VAL0			= (1 << 7),    /* VAL bit for byte 0 */

}VAL_BITS;

typedef enum {

	/* VAL3 */
	LCINTEN			= (1 << 27),
	APINT5EN		= (1 << 26),
	APINT4EN		= (1 << 25),
	APINT3EN		= (1 << 24),
	/* VAL2 */
	APINT2EN		= (1 << 22),
	APINT1EN		= (1 << 21),
	APINT0EN		= (1 << 20),
	MIIPDTINTEN		= (1 << 19),
	MCCIINTEN		= (1 << 18),
	MCCINTEN		= (1 << 17),
	MREINTEN		= (1 << 16),
	/* VAL1 */
	SPNDINTEN		= (1 << 14),
	MPINTEN			= (1 << 13),
	TINTEN3			= (1 << 11),
	SINTEN			= (1 << 12),
	TINTEN2			= (1 << 10),
	TINTEN1			= (1 << 9),
	TINTEN0			= (1 << 8),
	/* VAL0 */
	STINTEN			= (1 << 4),
	RINTEN0			= (1 << 0),

	INTEN0_CLEAR 		= 0x1F7F7F1F, /* Command style register */

}INTEN0_BITS;

typedef enum {
	/* VAL2 */
	RDMD0			= (1 << 16),
	/* VAL1 */
	TDMD3			= (1 << 11),
	TDMD2			= (1 << 10),
	TDMD1			= (1 << 9),
	TDMD0			= (1 << 8),
	/* VAL0 */
	UINTCMD			= (1 << 6),
	RX_FAST_SPND		= (1 << 5),
	TX_FAST_SPND		= (1 << 4),
	RX_SPND			= (1 << 3),
	TX_SPND			= (1 << 2),
	INTREN			= (1 << 1),
	RUN			= (1 << 0),

	CMD0_CLEAR 		= 0x000F0F7F,   /* Command style register */

}CMD0_BITS;

typedef enum {

	/* VAL3 */
	CONDUIT_MODE		= (1 << 29),
	/* VAL2 */
	RPA			= (1 << 19),
	DRCVPA			= (1 << 18),
	DRCVBC			= (1 << 17),
	PROM			= (1 << 16),
	/* VAL1 */
	ASTRP_RCV		= (1 << 13),
	RCV_DROP0	  	= (1 << 12),
	EMBA			= (1 << 11),
	DXMT2PD			= (1 << 10),
	LTINTEN			= (1 << 9),
	DXMTFCS			= (1 << 8),
	/* VAL0 */
	APAD_XMT		= (1 << 6),
	DRTY			= (1 << 5),
	INLOOP			= (1 << 4),
	EXLOOP			= (1 << 3),
	REX_RTRY		= (1 << 2),
	REX_UFLO		= (1 << 1),
	REX_LCOL		= (1 << 0),

	CMD2_CLEAR 		= 0x3F7F3F7F,   /* Command style register */

}CMD2_BITS;

typedef enum {

	/* VAL3 */
	ASF_INIT_DONE_ALIAS	= (1 << 29),
	/* VAL2 */
	JUMBO			= (1 << 21),
	VSIZE			= (1 << 20),
	VLONLY			= (1 << 19),
	VL_TAG_DEL		= (1 << 18),
	/* VAL1 */
	EN_PMGR			= (1 << 14),
	INTLEVEL		= (1 << 13),
	FORCE_FULL_DUPLEX	= (1 << 12),
	FORCE_LINK_STATUS	= (1 << 11),
	APEP			= (1 << 10),
	MPPLBA			= (1 << 9),
	/* VAL0 */
	RESET_PHY_PULSE		= (1 << 2),
	RESET_PHY		= (1 << 1),
	PHY_RST_POL		= (1 << 0),

}CMD3_BITS;


typedef enum {

	/* VAL0 */
	PMAT_SAVE_MATCH		= (1 << 4),
	PMAT_MODE		= (1 << 3),
	MPEN_SW			= (1 << 1),
	LCMODE_SW		= (1 << 0),

	CMD7_CLEAR  		= 0x0000001B	/* Command style register */

}CMD7_BITS;


typedef enum {

	RESET_PHY_WIDTH		= (0xF << 16) | (0xF<< 20), /* 0x00FF0000 */
	XMTSP_MASK		= (1 << 9) | (1 << 8),	/* 9:8 */
	XMTSP_128		= (1 << 9),	/* 9 */
	XMTSP_64		= (1 << 8),
	CACHE_ALIGN		= (1 << 4),
	BURST_LIMIT_MASK	= (0xF << 0 ),
	CTRL1_DEFAULT		= 0x00010111,

}CTRL1_BITS;

typedef enum {

	FMDC_MASK		= (1 << 9)|(1 << 8),	/* 9:8 */
	XPHYRST			= (1 << 7),
	XPHYANE			= (1 << 6),
	XPHYFD			= (1 << 5),
	XPHYSP			= (1 << 4) | (1 << 3),	/* 4:3 */
	APDW_MASK		= (1 <<	2) | (1 << 1) | (1 << 0), /* 2:0 */

}CTRL2_BITS;

/* XMT_RING_LIMIT		0x7C, 32bit register */
typedef enum {

	XMT_RING2_LIMIT		= (0xFF << 16),	/* 23:16 */
	XMT_RING1_LIMIT		= (0xFF << 8),	/* 15:8 */
	XMT_RING0_LIMIT		= (0xFF << 0), 	/* 7:0 */

}XMT_RING_LIMIT_BITS;

typedef enum {

	AP_REG0_EN		= (1 << 15),
	AP_REG0_ADDR_MASK	= (0xF << 8) |(1 << 12),/* 12:8 */
	AP_PHY0_ADDR_MASK	= (0xF << 0) |(1 << 4),/* 4:0 */

}AUTOPOLL0_BITS;

/* AUTOPOLL1			0x8A, 16bit register */
typedef enum {

	AP_REG1_EN		= (1 << 15),
	AP_REG1_ADDR_MASK	= (0xF << 8) |(1 << 12),/* 12:8 */
	AP_PRE_SUP1		= (1 << 6),
	AP_PHY1_DFLT		= (1 << 5),
	AP_PHY1_ADDR_MASK	= (0xF << 0) |(1 << 4),/* 4:0 */

}AUTOPOLL1_BITS;


typedef enum {

	AP_REG2_EN		= (1 << 15),
	AP_REG2_ADDR_MASK	= (0xF << 8) |(1 << 12),/* 12:8 */
	AP_PRE_SUP2		= (1 << 6),
	AP_PHY2_DFLT		= (1 << 5),
	AP_PHY2_ADDR_MASK	= (0xF << 0) |(1 << 4),/* 4:0 */

}AUTOPOLL2_BITS;

typedef enum {

	AP_REG3_EN		= (1 << 15),
	AP_REG3_ADDR_MASK	= (0xF << 8) |(1 << 12),/* 12:8 */
	AP_PRE_SUP3		= (1 << 6),
	AP_PHY3_DFLT		= (1 << 5),
	AP_PHY3_ADDR_MASK	= (0xF << 0) |(1 << 4),/* 4:0 */

}AUTOPOLL3_BITS;


typedef enum {

	AP_REG4_EN		= (1 << 15),
	AP_REG4_ADDR_MASK	= (0xF << 8) |(1 << 12),/* 12:8 */
	AP_PRE_SUP4		= (1 << 6),
	AP_PHY4_DFLT		= (1 << 5),
	AP_PHY4_ADDR_MASK	= (0xF << 0) |(1 << 4),/* 4:0 */

}AUTOPOLL4_BITS;


typedef enum {

	AP_REG5_EN		= (1 << 15),
	AP_REG5_ADDR_MASK	= (0xF << 8) |(1 << 12),/* 12:8 */
	AP_PRE_SUP5		= (1 << 6),
	AP_PHY5_DFLT		= (1 << 5),
	AP_PHY5_ADDR_MASK	= (0xF << 0) |(1 << 4),/* 4:0 */

}AUTOPOLL5_BITS;




/* AP_VALUE 			0x98, 32bit ragister */
typedef enum {

	AP_VAL_ACTIVE		= (1 << 31),
	AP_VAL_RD_CMD		= ( 1 << 29),
	AP_ADDR			= (1 << 18)|(1 << 17)|(1 << 16), /* 18:16 */
	AP_VAL			= (0xF << 0) | (0xF << 4) |( 0xF << 8) |
				  (0xF << 12),	/* 15:0 */

}AP_VALUE_BITS;

typedef enum {

	DLY_INT_A_R3		= (1 << 31),
	DLY_INT_A_R2		= (1 << 30),
	DLY_INT_A_R1		= (1 << 29),
	DLY_INT_A_R0		= (1 << 28),
	DLY_INT_A_T3		= (1 << 27),
	DLY_INT_A_T2		= (1 << 26),
	DLY_INT_A_T1		= (1 << 25),
	DLY_INT_A_T0		= ( 1 << 24),
	EVENT_COUNT_A		= (0xF << 16) | (0x1 << 20),/* 20:16 */
	MAX_DELAY_TIME_A	= (0xF << 0) | (0xF << 4) | (1 << 8)|
				  (1 << 9) | (1 << 10),	/* 10:0 */

}DLY_INT_A_BITS;

typedef enum {

	DLY_INT_B_R3		= (1 << 31),
	DLY_INT_B_R2		= (1 << 30),
	DLY_INT_B_R1		= (1 << 29),
	DLY_INT_B_R0		= (1 << 28),
	DLY_INT_B_T3		= (1 << 27),
	DLY_INT_B_T2		= (1 << 26),
	DLY_INT_B_T1		= (1 << 25),
	DLY_INT_B_T0		= ( 1 << 24),
	EVENT_COUNT_B		= (0xF << 16) | (0x1 << 20),/* 20:16 */
	MAX_DELAY_TIME_B	= (0xF << 0) | (0xF << 4) | (1 << 8)|
				  (1 << 9) | (1 << 10),	/* 10:0 */
}DLY_INT_B_BITS;


/* FLOW_CONTROL 		0xC8, 32bit register */
typedef enum {

	PAUSE_LEN_CHG		= (1 << 30),
	FTPE			= (1 << 22),
	FRPE			= (1 << 21),
	NAPA			= (1 << 20),
	NPA			= (1 << 19),
	FIXP			= ( 1 << 18),
	FCCMD			= ( 1 << 16),
	PAUSE_LEN		= (0xF << 0) | (0xF << 4) |( 0xF << 8) |	 				  (0xF << 12),	/* 15:0 */

}FLOW_CONTROL_BITS;

/* PHY_ ACCESS			0xD0, 32bit register */
typedef enum {

	PHY_CMD_ACTIVE		= (1 << 31),
	PHY_WR_CMD		= (1 << 30),
	PHY_RD_CMD		= (1 << 29),
	PHY_RD_ERR		= (1 << 28),
	PHY_PRE_SUP		= (1 << 27),
	PHY_ADDR		= (1 << 21) | (1 << 22) | (1 << 23)|
				  	(1 << 24) |(1 << 25),/* 25:21 */
	PHY_REG_ADDR		= (1 << 16) | (1 << 17) | (1 << 18)|	 			  	   	  	(1 << 19) | (1 << 20),/* 20:16 */
	PHY_DATA		= (0xF << 0)|(0xF << 4) |(0xF << 8)|
					(0xF << 12),/* 15:0 */

}PHY_ACCESS_BITS;


/* PMAT0			0x190,	 32bit register */
typedef enum {
	PMR_ACTIVE		= (1 << 31),
	PMR_WR_CMD		= (1 << 30),
	PMR_RD_CMD		= (1 << 29),
	PMR_BANK		= (1 <<28),
	PMR_ADDR		= (0xF << 16)|(1 << 20)|(1 << 21)|
				  	(1 << 22),/* 22:16 */
	PMR_B4			= (0xF << 0) | (0xF << 4),/* 15:0 */
}PMAT0_BITS;


/* PMAT1			0x194,	 32bit register */
typedef enum {
	PMR_B3			= (0xF << 24) | (0xF <<28),/* 31:24 */
	PMR_B2			= (0xF << 16) |(0xF << 20),/* 23:16 */
	PMR_B1			= (0xF << 8) | (0xF <<12), /* 15:8 */
	PMR_B0			= (0xF << 0)|(0xF << 4),/* 7:0 */
}PMAT1_BITS;

/************************************************************************/
/*                                                                      */
/*                      MIB counter definitions                         */
/*                                                                      */
/************************************************************************/

#define rcv_miss_pkts				0x00
#define rcv_octets				0x01
#define rcv_broadcast_pkts			0x02
#define rcv_multicast_pkts			0x03
#define rcv_undersize_pkts			0x04
#define rcv_oversize_pkts			0x05
#define rcv_fragments				0x06
#define rcv_jabbers				0x07
#define rcv_unicast_pkts			0x08
#define rcv_alignment_errors			0x09
#define rcv_fcs_errors				0x0A
#define rcv_good_octets				0x0B
#define rcv_mac_ctrl				0x0C
#define rcv_flow_ctrl				0x0D
#define rcv_pkts_64_octets			0x0E
#define rcv_pkts_65to127_octets			0x0F
#define rcv_pkts_128to255_octets		0x10
#define rcv_pkts_256to511_octets		0x11
#define rcv_pkts_512to1023_octets		0x12
#define rcv_pkts_1024to1518_octets		0x13
#define rcv_unsupported_opcode			0x14
#define rcv_symbol_errors			0x15
#define rcv_drop_pkts_ring1			0x16
#define rcv_drop_pkts_ring2			0x17
#define rcv_drop_pkts_ring3			0x18
#define rcv_drop_pkts_ring4			0x19
#define rcv_jumbo_pkts				0x1A

#define xmt_underrun_pkts			0x20
#define xmt_octets				0x21
#define xmt_packets				0x22
#define xmt_broadcast_pkts			0x23
#define xmt_multicast_pkts			0x24
#define xmt_collisions				0x25
#define xmt_unicast_pkts			0x26
#define xmt_one_collision			0x27
#define xmt_multiple_collision			0x28
#define xmt_deferred_transmit			0x29
#define xmt_late_collision			0x2A
#define xmt_excessive_defer			0x2B
#define xmt_loss_carrier			0x2C
#define xmt_excessive_collision			0x2D
#define xmt_back_pressure			0x2E
#define xmt_flow_ctrl				0x2F
#define xmt_pkts_64_octets			0x30
#define xmt_pkts_65to127_octets			0x31
#define xmt_pkts_128to255_octets		0x32
#define xmt_pkts_256to511_octets		0x33
#define xmt_pkts_512to1023_octets		0x34
#define xmt_pkts_1024to1518_octet		0x35
#define xmt_oversize_pkts			0x36
#define xmt_jumbo_pkts				0x37


/* Driver definitions */

#define	 PCI_VENDOR_ID_AMD		0x1022
#define  PCI_DEVICE_ID_AMD8111E_7462	0x7462

#define MAX_UNITS			8 /* Maximum number of devices possible */

#define NUM_TX_BUFFERS			32 /* Number of transmit buffers */
#define NUM_RX_BUFFERS			32 /* Number of receive buffers */

#define TX_BUFF_MOD_MASK         	31 /* (NUM_TX_BUFFERS -1) */
#define RX_BUFF_MOD_MASK         	31 /* (NUM_RX_BUFFERS -1) */

#define NUM_TX_RING_DR			32
#define NUM_RX_RING_DR			32

#define TX_RING_DR_MOD_MASK         	31 /* (NUM_TX_RING_DR -1) */
#define RX_RING_DR_MOD_MASK         	31 /* (NUM_RX_RING_DR -1) */

#define MAX_FILTER_SIZE			64 /* Maximum multicast address */
#define AMD8111E_MIN_MTU	 	60
#define AMD8111E_MAX_MTU		9000

#define PKT_BUFF_SZ			1536
#define MIN_PKT_LEN			60

#define  AMD8111E_TX_TIMEOUT		(3 * HZ)/* 3 sec */
#define SOFT_TIMER_FREQ 		0xBEBC  /* 0.5 sec */
#define DELAY_TIMER_CONV		50    /* msec to 10 usec conversion.
						 Only 500 usec resolution */
#define OPTION_VLAN_ENABLE		0x0001
#define OPTION_JUMBO_ENABLE		0x0002
#define OPTION_MULTICAST_ENABLE		0x0004
#define OPTION_WOL_ENABLE		0x0008
#define OPTION_WAKE_MAGIC_ENABLE	0x0010
#define OPTION_WAKE_PHY_ENABLE		0x0020
#define OPTION_INTR_COAL_ENABLE		0x0040
#define OPTION_DYN_IPG_ENABLE	        0x0080

#define PHY_REG_ADDR_MASK		0x1f

/* ipg parameters */
#define DEFAULT_IPG			0x60
#define IFS1_DELTA			36
#define	IPG_CONVERGE_JIFFIES (HZ/2)
#define	IPG_STABLE_TIME	5
#define	MIN_IPG	96
#define	MAX_IPG	255
#define IPG_STEP	16
#define CSTATE  1
#define SSTATE  2

/* Assume controller gets data 10 times the maximum processing time */
#define  REPEAT_CNT			10

/* amd8111e descriptor flag definitions */
typedef enum {

	OWN_BIT		=	(1 << 15),
	ADD_FCS_BIT	=	(1 << 13),
	LTINT_BIT	=	(1 << 12),
	STP_BIT		=	(1 << 9),
	ENP_BIT		=	(1 << 8),
	KILL_BIT	= 	(1 << 6),
	TCC_VLAN_INSERT	=	(1 << 1),
	TCC_VLAN_REPLACE =	(1 << 1) |( 1<< 0),

}TX_FLAG_BITS;

typedef enum {
	ERR_BIT 	=	(1 << 14),
	FRAM_BIT	=  	(1 << 13),
	OFLO_BIT	=       (1 << 12),
	CRC_BIT		=	(1 << 11),
	PAM_BIT		=	(1 << 6),
	LAFM_BIT	= 	(1 << 5),
	BAM_BIT		=	(1 << 4),
	TT_VLAN_TAGGED	= 	(1 << 3) |(1 << 2),/* 0x000 */
	TT_PRTY_TAGGED	=	(1 << 3),/* 0x0008 */

}RX_FLAG_BITS;

#define RESET_RX_FLAGS		0x0000
#define TT_MASK			0x000c
#define TCC_MASK		0x0003

/* driver ioctl parameters */
#define AMD8111E_REG_DUMP_LEN	 13*sizeof(u32)

/* amd8111e descriptor format */

struct amd8111e_tx_dr{

	__le16 buff_count; /* Size of the buffer pointed by this descriptor */

	__le16 tx_flags;

	__le16 tag_ctrl_info;

	__le16 tag_ctrl_cmd;

	__le32 buff_phy_addr;

	__le32 reserved;
};

struct amd8111e_rx_dr{

	__le32 reserved;

	__le16 msg_count; /* Received message len */

	__le16 tag_ctrl_info;

	__le16 buff_count;  /* Len of the buffer pointed by descriptor. */

	__le16 rx_flags;

	__le32 buff_phy_addr;

};
struct amd8111e_link_config{

#define SPEED_INVALID		0xffff
#define DUPLEX_INVALID		0xff
#define AUTONEG_INVALID		0xff

	unsigned long			orig_phy_option;
	u16				speed;
	u8				duplex;
	u8				autoneg;
	u8				reserved;  /* 32bit alignment */
};

enum coal_type{

	NO_COALESCE,
	LOW_COALESCE,
	MEDIUM_COALESCE,
	HIGH_COALESCE,

};

enum coal_mode{
	RX_INTR_COAL,
	TX_INTR_COAL,
	DISABLE_COAL,
	ENABLE_COAL,

};
#define MAX_TIMEOUT	40
#define MAX_EVENT_COUNT 31
struct amd8111e_coalesce_conf{

	unsigned int rx_timeout;
	unsigned int rx_event_count;
	unsigned long rx_packets;
	unsigned long rx_prev_packets;
	unsigned long rx_bytes;
	unsigned long rx_prev_bytes;
	unsigned int rx_coal_type;

	unsigned int tx_timeout;
	unsigned int tx_event_count;
	unsigned long tx_packets;
	unsigned long tx_prev_packets;
	unsigned long tx_bytes;
	unsigned long tx_prev_bytes;
	unsigned int tx_coal_type;

};
struct ipg_info{

	unsigned int ipg_state;
	unsigned int ipg;
	unsigned int current_ipg;
	unsigned int col_cnt;
	unsigned int diff_col_cnt;
	unsigned int timer_tick;
	unsigned int prev_ipg;
	struct timer_list ipg_timer;
};

struct amd8111e_priv{

	struct amd8111e_tx_dr*  tx_ring;
	struct amd8111e_rx_dr* rx_ring;
	dma_addr_t tx_ring_dma_addr;	/* tx descriptor ring base address */
	dma_addr_t rx_ring_dma_addr;	/* rx descriptor ring base address */
	const char *name;
	struct pci_dev *pci_dev;	/* Ptr to the associated pci_dev */
	struct net_device* amd8111e_net_dev; 	/* ptr to associated net_device */
	/* Transmit and receive skbs */
	struct sk_buff *tx_skbuff[NUM_TX_BUFFERS];
	struct sk_buff *rx_skbuff[NUM_RX_BUFFERS];
	/* Transmit and receive dma mapped addr */
	dma_addr_t tx_dma_addr[NUM_TX_BUFFERS];
	dma_addr_t rx_dma_addr[NUM_RX_BUFFERS];
	/* Reg memory mapped address */
	void __iomem *mmio;

	struct napi_struct napi;

	spinlock_t lock;	/* Guard lock */
	unsigned long rx_idx, tx_idx;	/* The next free ring entry */
	unsigned long tx_complete_idx;
	unsigned long tx_ring_complete_idx;
	unsigned long tx_ring_idx;
	unsigned int rx_buff_len;	/* Buffer length of rx buffers */
	int options;		/* Options enabled/disabled for the device */

	unsigned long ext_phy_option;
	int ext_phy_addr;
	u32 ext_phy_id;

	struct amd8111e_link_config link_config;

	struct net_device *next;
	int mii;
	struct mii_if_info mii_if;
	char opened;
	unsigned int drv_rx_errors;
	struct amd8111e_coalesce_conf coal_conf;

	struct ipg_info  ipg_data;

};

/* kernel provided writeq does not write 64 bits into the amd8111e device register instead writes only higher 32bits data into lower 32bits of the register.
BUG? */
#define  amd8111e_writeq(_UlData,_memMap)   \
		writel(*(u32*)(&_UlData), _memMap);	\
		writel(*(u32*)((u8*)(&_UlData)+4), _memMap+4)

/* maps the external speed options to internal value */
typedef enum {
	SPEED_AUTONEG,
	SPEED10_HALF,
	SPEED10_FULL,
	SPEED100_HALF,
	SPEED100_FULL,
}EXT_PHY_OPTION;

static int card_idx;
static int speed_duplex[MAX_UNITS] = { 0, };
static bool coalesce[MAX_UNITS] = { [ 0 ... MAX_UNITS-1] = true };
static bool dynamic_ipg[MAX_UNITS] = { [ 0 ... MAX_UNITS-1] = false };
static unsigned int chip_version;

#endif /* _AMD8111E_H */

