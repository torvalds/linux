/* drivers/atm/midway.h - Efficient Networks Midway (SAR) description */
 
/* Written 1995-1999 by Werner Almesberger, EPFL LRC/ICA */
 

#ifndef DRIVERS_ATM_MIDWAY_H
#define DRIVERS_ATM_MIDWAY_H


#define NR_VCI		1024		/* number of VCIs */
#define NR_VCI_LD	10		/* log2(NR_VCI) */
#define NR_DMA_RX	512		/* RX DMA queue entries */
#define NR_DMA_TX	512		/* TX DMA queue entries */
#define NR_SERVICE	NR_VCI		/* service list size */
#define NR_CHAN		8		/* number of TX channels */
#define TS_CLOCK	25000000	/* traffic shaper clock (cell/sec) */

#define MAP_MAX_SIZE	0x00400000	/* memory window for max config */
#define EPROM_SIZE	0x00010000
#define	MEM_VALID	0xffc00000	/* mask base address with this */
#define PHY_BASE	0x00020000	/* offset of PHY register are */
#define REG_BASE	0x00040000	/* offset of Midway register area */
#define RAM_BASE	0x00200000	/* offset of RAM area */
#define RAM_INCREMENT	0x00020000	/* probe for RAM every 128kB */

#define MID_VCI_BASE	RAM_BASE
#define MID_DMA_RX_BASE	(MID_VCI_BASE+NR_VCI*16)
#define MID_DMA_TX_BASE	(MID_DMA_RX_BASE+NR_DMA_RX*8)
#define MID_SERVICE_BASE (MID_DMA_TX_BASE+NR_DMA_TX*8)
#define MID_FREE_BASE	(MID_SERVICE_BASE+NR_SERVICE*4)

#define MAC_LEN 6 /* atm.h */

#define MID_MIN_BUF_SIZE (1024)		/*   1 kB is minimum */
#define MID_MAX_BUF_SIZE (128*1024)	/* 128 kB is maximum */

#define RX_DESCR_SIZE	1		/* RX PDU descr is 1 longword */
#define TX_DESCR_SIZE	2		/* TX PDU descr is 2 longwords */
#define AAL5_TRAILER	(ATM_AAL5_TRAILER/4) /* AAL5 trailer is 2 longwords */

#define TX_GAP		8		/* TX buffer gap (words) */

/*
 * Midway Reset/ID
 *
 * All values read-only. Writing to this register resets Midway chip.
 */

#define MID_RES_ID_MCON	0x00		/* Midway Reset/ID */

#define MID_ID		0xf0000000	/* Midway version */
#define MID_SHIFT	24
#define MID_MOTHER_ID	0x00000700	/* mother board id */
#define MID_MOTHER_SHIFT 8
#define MID_CON_TI	0x00000080	/* 0: normal ctrl; 1: SABRE */
#define MID_CON_SUNI	0x00000040	/* 0: UTOPIA; 1: SUNI */
#define MID_CON_V6	0x00000020	/* 0: non-pipel UTOPIA (required iff
					   !CON_SUNI; 1: UTOPIA */
#define DAUGTHER_ID	0x0000001f	/* daugther board id */

/*
 * Interrupt Status Acknowledge, Interrupt Status & Interrupt Enable
 */

#define MID_ISA		0x01		/* Interrupt Status Acknowledge */
#define MID_IS		0x02		/* Interrupt Status */
#define MID_IE		0x03		/* Interrupt Enable */

#define MID_TX_COMPLETE_7 0x00010000	/* channel N completed a PDU */
#define MID_TX_COMPLETE_6 0x00008000	/* transmission */
#define MID_TX_COMPLETE_5 0x00004000
#define MID_TX_COMPLETE_4 0x00002000
#define MID_TX_COMPLETE_3 0x00001000
#define MID_TX_COMPLETE_2 0x00000800
#define MID_TX_COMPLETE_1 0x00000400
#define MID_TX_COMPLETE_0 0x00000200
#define MID_TX_COMPLETE	0x0001fe00	/* any TX */
#define MID_TX_DMA_OVFL	0x00000100	/* DMA to adapter overflow */
#define MID_TX_IDENT_MISM 0x00000080	/* TX: ident mismatch => halted */
#define MID_DMA_LERR_ACK 0x00000040	/* LERR - SBus ? */
#define MID_DMA_ERR_ACK	0x00000020	/* DMA error */
#define	MID_RX_DMA_COMPLETE 0x00000010	/* DMA to host done */
#define MID_TX_DMA_COMPLETE 0x00000008	/* DMA from host done */
#define MID_SERVICE	0x00000004	/* something in service list */
#define MID_SUNI_INT	0x00000002	/* interrupt from SUNI */
#define MID_STAT_OVFL	0x00000001	/* statistics overflow */

/*
 * Master Control/Status
 */

#define MID_MC_S	0x04

#define MID_INT_SELECT	0x000001C0	/* Interrupt level (000: off) */
#define MID_INT_SEL_SHIFT 6
#define	MID_TX_LOCK_MODE 0x00000020	/* 0: streaming; 1: TX ovfl->lock */
#define MID_DMA_ENABLE	0x00000010	/* R: 0: disable; 1: enable
					   W: 0: no change; 1: enable */
#define MID_TX_ENABLE	0x00000008	/* R: 0: TX disabled; 1: enabled
					   W: 0: no change; 1: enable */
#define MID_RX_ENABLE	0x00000004	/* like TX */
#define MID_WAIT_1MS	0x00000002	/* R: 0: timer not running; 1: running
					   W: 0: no change; 1: no interrupts
							       for 1 ms */
#define MID_WAIT_500US	0x00000001	/* like WAIT_1MS, but 0.5 ms */

/*
 * Statistics
 *
 * Cleared when reading.
 */

#define MID_STAT		0x05

#define MID_VCI_TRASH	0xFFFF0000	/* trashed cells because of VCI mode */
#define MID_VCI_TRASH_SHIFT 16
#define MID_OVFL_TRASH	0x0000FFFF	/* trashed cells because of overflow */

/*
 * Address registers
 */

#define MID_SERV_WRITE	0x06	/* free pos in service area (R, 10 bits) */
#define MID_DMA_ADDR	0x07	/* virtual DMA address (R, 32 bits) */
#define MID_DMA_WR_RX	0x08	/* (RW, 9 bits) */
#define MID_DMA_RD_RX	0x09
#define MID_DMA_WR_TX	0x0A
#define MID_DMA_RD_TX	0x0B

/*
 * Transmit Place Registers (0x10+4*channel)
 */

#define MID_TX_PLACE(c)	(0x10+4*(c))

#define MID_SIZE	0x00003800	/* size, N*256 x 32 bit */
#define MID_SIZE_SHIFT	11
#define MID_LOCATION	0x000007FF	/* location in adapter memory (word) */

#define MID_LOC_SKIP	8		/* 8 bits of location are always zero
					   (applies to all uses of location) */

/*
 * Transmit ReadPtr Registers (0x11+4*channel)
 */

#define MID_TX_RDPTR(c)	(0x11+4*(c))

#define MID_READ_PTR	0x00007FFF	/* next word for PHY */

/*
 * Transmit DescrStart Registers (0x12+4*channel)
 */

#define MID_TX_DESCRSTART(c) (0x12+4*(c))

#define MID_DESCR_START	0x00007FFF	/* seg buffer being DMAed */

#define ENI155_MAGIC	0xa54b872d

struct midway_eprom {
	unsigned char mac[MAC_LEN],inv_mac[MAC_LEN];
	unsigned char pad[36];
	u32 serial,inv_serial;
	u32 magic,inv_magic;
};


/*
 * VCI table entry
 */

#define MID_VCI_IN_SERVICE	0x00000001	/* set if VCI is currently in
						   service list */
#define MID_VCI_SIZE		0x00038000	/* reassembly buffer size,
						   2*<size> kB */
#define MID_VCI_SIZE_SHIFT	15
#define MID_VCI_LOCATION	0x1ffc0000	/* buffer location */
#define MID_VCI_LOCATION_SHIFT	18
#define MID_VCI_PTI_MODE	0x20000000	/* 0: trash, 1: preserve */
#define MID_VCI_MODE		0xc0000000
#define MID_VCI_MODE_SHIFT	30
#define MID_VCI_READ		0x00007fff
#define MID_VCI_READ_SHIFT	0
#define MID_VCI_DESCR		0x7fff0000
#define MID_VCI_DESCR_SHIFT	16
#define MID_VCI_COUNT		0x000007ff
#define MID_VCI_COUNT_SHIFT	0
#define MID_VCI_STATE		0x0000c000
#define MID_VCI_STATE_SHIFT	14
#define MID_VCI_WRITE		0x7fff0000
#define MID_VCI_WRITE_SHIFT	16

#define MID_MODE_TRASH	0
#define MID_MODE_RAW	1
#define MID_MODE_AAL5	2

/*
 * Reassembly buffer descriptor
 */

#define MID_RED_COUNT		0x000007ff
#define MID_RED_CRC_ERR		0x00000800
#define MID_RED_T		0x00001000
#define MID_RED_CE		0x00010000
#define MID_RED_CLP		0x01000000
#define MID_RED_IDEN		0xfe000000
#define MID_RED_SHIFT		25

#define MID_RED_RX_ID		0x1b		/* constant identifier */

/*
 * Segmentation buffer descriptor
 */

#define MID_SEG_COUNT		MID_RED_COUNT
#define MID_SEG_RATE		0x01f80000
#define MID_SEG_RATE_SHIFT	19
#define MID_SEG_PR		0x06000000
#define MID_SEG_PR_SHIFT	25
#define MID_SEG_AAL5		0x08000000
#define MID_SEG_ID		0xf0000000
#define MID_SEG_ID_SHIFT	28
#define MID_SEG_MAX_RATE	63

#define MID_SEG_CLP		0x00000001
#define MID_SEG_PTI		0x0000000e
#define MID_SEG_PTI_SHIFT	1
#define MID_SEG_VCI		0x00003ff0
#define MID_SEG_VCI_SHIFT	4

#define MID_SEG_TX_ID		0xb		/* constant identifier */

/*
 * DMA entry
 */

#define MID_DMA_COUNT		0xffff0000
#define MID_DMA_COUNT_SHIFT	16
#define MID_DMA_END		0x00000020
#define MID_DMA_TYPE		0x0000000f

#define MID_DT_JK	0x3
#define MID_DT_WORD	0x0
#define MID_DT_2W	0x7
#define MID_DT_4W	0x4
#define MID_DT_8W	0x5
#define MID_DT_16W	0x6
#define MID_DT_2WM	0xf
#define MID_DT_4WM	0xc
#define MID_DT_8WM	0xd
#define MID_DT_16WM	0xe

/* only for RX*/
#define MID_DMA_VCI		0x0000ffc0
#define	MID_DMA_VCI_SHIFT	6

/* only for TX */
#define MID_DMA_CHAN		0x000001c0
#define MID_DMA_CHAN_SHIFT	6

#define MID_DT_BYTE	0x1
#define MID_DT_HWORD	0x2

#endif
