/*

  he.h

  ForeRunnerHE ATM Adapter driver for ATM on Linux
  Copyright (C) 1999-2001  Naval Research Laboratory

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/*

  he.h

  ForeRunnerHE ATM Adapter driver for ATM on Linux
  Copyright (C) 1999-2000  Naval Research Laboratory

  Permission to use, copy, modify and distribute this software and its
  documentation is hereby granted, provided that both the copyright
  notice and this permission notice appear in all copies of the software,
  derivative works or modified versions, and any portions thereof, and
  that both notices appear in supporting documentation.

  NRL ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION AND
  DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
  RESULTING FROM THE USE OF THIS SOFTWARE.

 */

#ifndef _HE_H_
#define _HE_H_

#define DEV_LABEL       "he"

#define CONFIG_DEFAULT_VCIBITS	12
#define CONFIG_DEFAULT_VPIBITS	0

#define CONFIG_IRQ_SIZE		128
#define CONFIG_IRQ_THRESH	(CONFIG_IRQ_SIZE/2)

#define CONFIG_TPDRQ_SIZE	512
#define TPDRQ_MASK(x)		(((unsigned long)(x))&((CONFIG_TPDRQ_SIZE<<3)-1))

#define CONFIG_RBRQ_SIZE	512
#define CONFIG_RBRQ_THRESH	400
#define RBRQ_MASK(x)		(((unsigned long)(x))&((CONFIG_RBRQ_SIZE<<3)-1))

#define CONFIG_TBRQ_SIZE	512
#define CONFIG_TBRQ_THRESH	400
#define TBRQ_MASK(x)		(((unsigned long)(x))&((CONFIG_TBRQ_SIZE<<2)-1))

#define CONFIG_RBPL_SIZE	512
#define CONFIG_RBPL_THRESH	64
#define CONFIG_RBPL_BUFSIZE	4096
#define RBPL_MASK(x)		(((unsigned long)(x))&((CONFIG_RBPL_SIZE<<3)-1))

/* 5.1.3 initialize connection memory */

#define CONFIG_RSRA		0x00000
#define CONFIG_RCMLBM		0x08000
#define CONFIG_RCMABR		0x0d800
#define CONFIG_RSRB		0x0e000

#define CONFIG_TSRA		0x00000
#define CONFIG_TSRB		0x08000
#define CONFIG_TSRC		0x0c000
#define CONFIG_TSRD		0x0e000
#define CONFIG_TMABR		0x0f000
#define CONFIG_TPDBA		0x10000

#define HE_MAXCIDBITS		12

/* 2.9.3.3 interrupt encodings */

struct he_irq {
	volatile u32 isw;
};

#define IRQ_ALIGNMENT		0x1000

#define NEXT_ENTRY(base, tail, mask) \
				(((unsigned long)base)|(((unsigned long)(tail+1))&mask))

#define ITYPE_INVALID		0xffffffff
#define ITYPE_TBRQ_THRESH	(0<<3)
#define ITYPE_TPD_COMPLETE	(1<<3)
#define ITYPE_RBPS_THRESH	(2<<3)
#define ITYPE_RBPL_THRESH	(3<<3)
#define ITYPE_RBRQ_THRESH	(4<<3)
#define ITYPE_RBRQ_TIMER	(5<<3)
#define ITYPE_PHY		(6<<3)
#define ITYPE_OTHER		0x80
#define ITYPE_PARITY		0x81
#define ITYPE_ABORT		0x82

#define ITYPE_GROUP(x)		(x & 0x7)
#define ITYPE_TYPE(x)		(x & 0xf8)

#define HE_NUM_GROUPS 8

/* 2.1.4 transmit packet descriptor */

struct he_tpd {

	/* read by the adapter */

	volatile u32 status;
	volatile u32 reserved;

#define TPD_MAXIOV	3
	struct {
		u32 addr, len;
	} iovec[TPD_MAXIOV];

#define address0 iovec[0].addr
#define length0 iovec[0].len

	/* linux-atm extensions */

	struct sk_buff *skb;
	struct atm_vcc *vcc;

	struct list_head entry;
};

#define TPD_ALIGNMENT	64
#define TPD_LEN_MASK	0xffff

#define TPD_ADDR_SHIFT  6
#define TPD_MASK	0xffffffc0
#define TPD_ADDR(x)	((x) & TPD_MASK)
#define TPD_INDEX(x)	(TPD_ADDR(x) >> TPD_ADDR_SHIFT)


/* table 2.3 transmit buffer return elements */

struct he_tbrq {
	volatile u32 tbre;
};

#define TBRQ_ALIGNMENT	CONFIG_TBRQ_SIZE

#define TBRQ_TPD(tbrq)		((tbrq)->tbre & 0xffffffc0)
#define TBRQ_EOS(tbrq)		((tbrq)->tbre & (1<<3))
#define TBRQ_MULTIPLE(tbrq)	((tbrq)->tbre & (1))

/* table 2.21 receive buffer return queue element field organization */

struct he_rbrq {
	volatile u32 addr;
	volatile u32 cidlen;
};

#define RBRQ_ALIGNMENT	CONFIG_RBRQ_SIZE

#define RBRQ_ADDR(rbrq)		((rbrq)->addr & 0xffffffc0)
#define RBRQ_CRC_ERR(rbrq)	((rbrq)->addr & (1<<5))
#define RBRQ_LEN_ERR(rbrq)	((rbrq)->addr & (1<<4))
#define RBRQ_END_PDU(rbrq)	((rbrq)->addr & (1<<3))
#define RBRQ_AAL5_PROT(rbrq)	((rbrq)->addr & (1<<2))
#define RBRQ_CON_CLOSED(rbrq)	((rbrq)->addr & (1<<1))
#define RBRQ_HBUF_ERR(rbrq)	((rbrq)->addr & 1)
#define RBRQ_CID(rbrq)		(((rbrq)->cidlen >> 16) & 0x1fff)
#define RBRQ_BUFLEN(rbrq)	((rbrq)->cidlen & 0xffff)

/* figure 2.3 transmit packet descriptor ready queue */

struct he_tpdrq {
	volatile u32 tpd;
	volatile u32 cid;
};

#define TPDRQ_ALIGNMENT CONFIG_TPDRQ_SIZE

/* table 2.30 host status page detail */

#define HSP_ALIGNMENT	0x400		/* must align on 1k boundary */

struct he_hsp {
	struct he_hsp_entry {
		volatile u32 tbrq_tail; 
		volatile u32 reserved1[15];
		volatile u32 rbrq_tail; 
		volatile u32 reserved2[15];
	} group[HE_NUM_GROUPS];
};

/*
 * figure 2.9 receive buffer pools
 *
 * since a virtual address might be more than 32 bits, we store an index
 * in the virt member of he_rbp.  NOTE: the lower six bits in the  rbrq
 * addr member are used for buffer status further limiting us to 26 bits.
 */

struct he_rbp {
	volatile u32 phys;
	volatile u32 idx;	/* virt */
};

#define RBP_IDX_OFFSET 6

/*
 * the he dma engine will try to hold an extra 16 buffers in its local
 * caches.  and add a couple buffers for safety.
 */

#define RBPL_TABLE_SIZE (CONFIG_RBPL_SIZE + 16 + 2)

struct he_buff {
	struct list_head entry;
	dma_addr_t mapping;
	unsigned long len;
	u8 data[];
};

#ifdef notyet
struct he_group {
	u32 rpbl_size, rpbl_qsize;
	struct he_rpb_entry *rbpl_ba;
};
#endif

#define HE_LOOKUP_VCC(dev, cid) ((dev)->he_vcc_table[(cid)].vcc)

struct he_vcc_table 
{
	struct atm_vcc *vcc;
};

struct he_cs_stper
{
	long pcr;
	int inuse;
};

#define HE_NUM_CS_STPER		16

struct he_dev {
	unsigned int number;
	unsigned int irq;
	void __iomem *membase;

	char prod_id[30];
	char mac_addr[6];
	int media;

	unsigned int vcibits, vpibits;
	unsigned int cells_per_row;
	unsigned int bytes_per_row;
	unsigned int cells_per_lbuf;
	unsigned int r0_numrows, r0_startrow, r0_numbuffs;
	unsigned int r1_numrows, r1_startrow, r1_numbuffs;
	unsigned int tx_numrows, tx_startrow, tx_numbuffs;
	unsigned int buffer_limit;

	struct he_vcc_table *he_vcc_table;

#ifdef notyet
	struct he_group group[HE_NUM_GROUPS];
#endif
	struct he_cs_stper cs_stper[HE_NUM_CS_STPER];
	unsigned total_bw;

	dma_addr_t irq_phys;
	struct he_irq *irq_base, *irq_head, *irq_tail;
	volatile unsigned *irq_tailoffset;
	int irq_peak;

	struct tasklet_struct tasklet;
	struct pci_pool *tpd_pool;
	struct list_head outstanding_tpds;

	dma_addr_t tpdrq_phys;
	struct he_tpdrq *tpdrq_base, *tpdrq_tail, *tpdrq_head;

	spinlock_t global_lock;		/* 8.1.5 pci transaction ordering
					  error problem */
	dma_addr_t rbrq_phys;
	struct he_rbrq *rbrq_base, *rbrq_head;
	int rbrq_peak;

	struct he_buff **rbpl_virt;
	unsigned long *rbpl_table;
	unsigned long rbpl_hint;
	struct pci_pool *rbpl_pool;
	dma_addr_t rbpl_phys;
	struct he_rbp *rbpl_base, *rbpl_tail;
	struct list_head rbpl_outstanding;
	int rbpl_peak;

	dma_addr_t tbrq_phys;
	struct he_tbrq *tbrq_base, *tbrq_head;
	int tbrq_peak;

	dma_addr_t hsp_phys;
	struct he_hsp *hsp;

	struct pci_dev *pci_dev;
	struct atm_dev *atm_dev;
	struct he_dev *next;
};

#define HE_MAXIOV 20

struct he_vcc
{
	struct list_head buffers;
	int pdu_len;
	int rc_index;

	wait_queue_head_t rx_waitq;
	wait_queue_head_t tx_waitq;
};

#define HE_VCC(vcc)	((struct he_vcc *)(vcc->dev_data))

#define PCI_VENDOR_ID_FORE	0x1127
#define PCI_DEVICE_ID_FORE_HE	0x400

#define GEN_CNTL_0				0x40
#define  INT_PROC_ENBL		(1<<25)
#define  SLAVE_ENDIAN_MODE	(1<<16)
#define  MRL_ENB		(1<<5)
#define  MRM_ENB		(1<<4)
#define  INIT_ENB		(1<<2)
#define  IGNORE_TIMEOUT		(1<<1)
#define  ENBL_64		(1<<0)

#define MIN_PCI_LATENCY		32	/* errata 8.1.3 */

#define HE_DEV(dev) ((struct he_dev *) (dev)->dev_data)

#define he_is622(dev)	((dev)->media & 0x1)
#define he_isMM(dev)	((dev)->media & 0x20)

#define HE_REGMAP_SIZE	0x100000

#define RESET_CNTL	0x80000
#define  BOARD_RST_STATUS	(1<<6)

#define HOST_CNTL	0x80004
#define  PCI_BUS_SIZE64			(1<<27)
#define  DESC_RD_STATIC_64		(1<<26)
#define  DATA_RD_STATIC_64		(1<<25)
#define  DATA_WR_STATIC_64		(1<<24)
#define  ID_CS				(1<<12)
#define  ID_WREN			(1<<11)
#define  ID_DOUT			(1<<10)
#define   ID_DOFFSET			10
#define  ID_DIN				(1<<9)
#define  ID_CLOCK			(1<<8)
#define  QUICK_RD_RETRY			(1<<7)
#define  QUICK_WR_RETRY			(1<<6)
#define  OUTFF_ENB			(1<<5)
#define  CMDFF_ENB			(1<<4)
#define  PERR_INT_ENB			(1<<2)
#define  IGNORE_INTR			(1<<0)

#define LB_SWAP		0x80008
#define  SWAP_RNUM_MAX(x)	(x<<27)
#define  DATA_WR_SWAP		(1<<20)
#define  DESC_RD_SWAP		(1<<19)
#define  DATA_RD_SWAP		(1<<18)
#define  INTR_SWAP		(1<<17)
#define  DESC_WR_SWAP		(1<<16)
#define  SDRAM_INIT		(1<<15)
#define  BIG_ENDIAN_HOST	(1<<14)
#define  XFER_SIZE		(1<<7)

#define LB_MEM_ADDR	0x8000c
#define LB_MEM_DATA	0x80010

#define LB_MEM_ACCESS	0x80014
#define  LB_MEM_HNDSHK		(1<<30)
#define  LM_MEM_WRITE		(0x7)
#define  LM_MEM_READ		(0x3)

#define SDRAM_CTL	0x80018
#define  LB_64_ENB		(1<<3)
#define  LB_TWR			(1<<2)
#define  LB_TRP			(1<<1)
#define  LB_TRAS		(1<<0)

#define INT_FIFO	0x8001c
#define  INT_MASK_D		(1<<15)
#define  INT_MASK_C		(1<<14)
#define  INT_MASK_B		(1<<13)
#define  INT_MASK_A		(1<<12)
#define  INT_CLEAR_D		(1<<11)
#define  INT_CLEAR_C		(1<<10)
#define  INT_CLEAR_B		(1<<9)
#define  INT_CLEAR_A		(1<<8)

#define ABORT_ADDR	0x80020

#define IRQ0_BASE	0x80080
#define  IRQ_BASE(x)		(x<<12)
#define  IRQ_MASK		((CONFIG_IRQ_SIZE<<2)-1)	/* was 0x3ff */
#define  IRQ_TAIL(x)		(((unsigned long)(x)) & IRQ_MASK)
#define IRQ0_HEAD	0x80084
#define  IRQ_SIZE(x)		(x<<22)
#define  IRQ_THRESH(x)		(x<<12)
#define  IRQ_HEAD(x)		(x<<2)
/* #define  IRQ_PENDING		(1) 		conflict with linux/irq.h */
#define IRQ0_CNTL	0x80088
#define  IRQ_ADDRSEL(x)		(x<<2)
#define  IRQ_INT_A		(0<<2)
#define  IRQ_INT_B		(1<<2)
#define  IRQ_INT_C		(2<<2)
#define  IRQ_INT_D		(3<<2)
#define  IRQ_TYPE_ADDR		0x1
#define  IRQ_TYPE_LINE		0x0
#define IRQ0_DATA	0x8008c

#define IRQ1_BASE	0x80090
#define IRQ1_HEAD	0x80094
#define IRQ1_CNTL	0x80098
#define IRQ1_DATA	0x8009c

#define IRQ2_BASE	0x800a0
#define IRQ2_HEAD	0x800a4
#define IRQ2_CNTL	0x800a8
#define IRQ2_DATA	0x800ac

#define IRQ3_BASE	0x800b0
#define IRQ3_HEAD	0x800b4
#define IRQ3_CNTL	0x800b8
#define IRQ3_DATA	0x800bc

#define GRP_10_MAP	0x800c0
#define GRP_32_MAP	0x800c4
#define GRP_54_MAP	0x800c8
#define GRP_76_MAP	0x800cc

#define	G0_RBPS_S	0x80400
#define G0_RBPS_T	0x80404
#define  RBP_TAIL(x)		((x)<<3)
#define  RBP_MASK(x)		((x)|0x1fff)
#define G0_RBPS_QI	0x80408
#define  RBP_QSIZE(x)		((x)<<14)
#define  RBP_INT_ENB		(1<<13)
#define  RBP_THRESH(x)		(x)
#define G0_RBPS_BS	0x8040c
#define G0_RBPL_S	0x80410
#define G0_RBPL_T	0x80414
#define G0_RBPL_QI	0x80418 
#define G0_RBPL_BS	0x8041c

#define	G1_RBPS_S	0x80420
#define G1_RBPS_T	0x80424
#define G1_RBPS_QI	0x80428
#define G1_RBPS_BS	0x8042c
#define G1_RBPL_S	0x80430
#define G1_RBPL_T	0x80434
#define G1_RBPL_QI	0x80438
#define G1_RBPL_BS	0x8043c

#define	G2_RBPS_S	0x80440
#define G2_RBPS_T	0x80444
#define G2_RBPS_QI	0x80448
#define G2_RBPS_BS	0x8044c
#define G2_RBPL_S	0x80450
#define G2_RBPL_T	0x80454
#define G2_RBPL_QI	0x80458
#define G2_RBPL_BS	0x8045c

#define	G3_RBPS_S	0x80460
#define G3_RBPS_T	0x80464
#define G3_RBPS_QI	0x80468
#define G3_RBPS_BS	0x8046c
#define G3_RBPL_S	0x80470
#define G3_RBPL_T	0x80474
#define G3_RBPL_QI	0x80478
#define G3_RBPL_BS	0x8047c

#define	G4_RBPS_S	0x80480
#define G4_RBPS_T	0x80484
#define G4_RBPS_QI	0x80488
#define G4_RBPS_BS	0x8048c
#define G4_RBPL_S	0x80490
#define G4_RBPL_T	0x80494
#define G4_RBPL_QI	0x80498
#define G4_RBPL_BS	0x8049c

#define	G5_RBPS_S	0x804a0
#define G5_RBPS_T	0x804a4
#define G5_RBPS_QI	0x804a8
#define G5_RBPS_BS	0x804ac
#define G5_RBPL_S	0x804b0
#define G5_RBPL_T	0x804b4
#define G5_RBPL_QI	0x804b8
#define G5_RBPL_BS	0x804bc

#define	G6_RBPS_S	0x804c0
#define G6_RBPS_T	0x804c4
#define G6_RBPS_QI	0x804c8
#define G6_RBPS_BS	0x804cc
#define G6_RBPL_S	0x804d0
#define G6_RBPL_T	0x804d4
#define G6_RBPL_QI	0x804d8
#define G6_RBPL_BS	0x804dc

#define	G7_RBPS_S	0x804e0
#define G7_RBPS_T	0x804e4
#define G7_RBPS_QI	0x804e8
#define G7_RBPS_BS	0x804ec

#define G7_RBPL_S	0x804f0
#define G7_RBPL_T	0x804f4
#define G7_RBPL_QI	0x804f8
#define G7_RBPL_BS	0x804fc

#define G0_RBRQ_ST	0x80500
#define G0_RBRQ_H	0x80504
#define G0_RBRQ_Q	0x80508
#define  RBRQ_THRESH(x)		((x)<<13)
#define  RBRQ_SIZE(x)		(x)
#define G0_RBRQ_I	0x8050c
#define  RBRQ_TIME(x)		((x)<<8)
#define  RBRQ_COUNT(x)		(x)

/* fill in 1 ... 7 later */

#define G0_TBRQ_B_T	0x80600
#define G0_TBRQ_H	0x80604
#define G0_TBRQ_S	0x80608
#define G0_TBRQ_THRESH	0x8060c
#define  TBRQ_THRESH(x)		(x)

/* fill in 1 ... 7 later */

#define RH_CONFIG	0x805c0
#define  PHY_INT_ENB	(1<<10)
#define  OAM_GID(x)	(x<<7)
#define  PTMR_PRE(x)	(x)

#define G0_INMQ_S	0x80580
#define G0_INMQ_L	0x80584
#define G1_INMQ_S	0x80588
#define G1_INMQ_L	0x8058c
#define G2_INMQ_S	0x80590
#define G2_INMQ_L	0x80594
#define G3_INMQ_S	0x80598
#define G3_INMQ_L	0x8059c
#define G4_INMQ_S	0x805a0
#define G4_INMQ_L	0x805a4
#define G5_INMQ_S	0x805a8
#define G5_INMQ_L	0x805ac
#define G6_INMQ_S	0x805b0
#define G6_INMQ_L	0x805b4
#define G7_INMQ_S	0x805b8
#define G7_INMQ_L	0x805bc

#define TPDRQ_B_H	0x80680
#define TPDRQ_T		0x80684
#define TPDRQ_S		0x80688

#define UBUFF_BA	0x8068c

#define RLBF0_H		0x806c0
#define RLBF0_T		0x806c4
#define RLBF1_H		0x806c8
#define RLBF1_T		0x806cc
#define RLBC_H		0x806d0
#define RLBC_T		0x806d4
#define RLBC_H2		0x806d8
#define TLBF_H		0x806e0
#define TLBF_T		0x806e4
#define RLBF0_C		0x806e8
#define RLBF1_C		0x806ec
#define RXTHRSH		0x806f0
#define LITHRSH		0x806f4

#define LBARB		0x80700
#define  SLICE_X(x)		 (x<<28)
#define  ARB_RNUM_MAX(x)	 (x<<23)
#define  TH_PRTY(x)		 (x<<21)
#define  RH_PRTY(x)		 (x<<19)
#define  TL_PRTY(x)		 (x<<17)
#define  RL_PRTY(x)		 (x<<15)
#define  BUS_MULTI(x)		 (x<<8)
#define  NET_PREF(x)		 (x)

#define SDRAMCON	0x80704
#define	 BANK_ON		(1<<14)
#define	 WIDE_DATA		(1<<13)
#define	 TWR_WAIT		(1<<12)
#define	 TRP_WAIT		(1<<11)
#define	 TRAS_WAIT		(1<<10)
#define	 REF_RATE(x)		(x)

#define LBSTAT		0x80708

#define RCC_STAT	0x8070c
#define  RCC_BUSY		(1)

#define TCMCONFIG	0x80740
#define  TM_DESL2		(1<<10)
#define	 TM_BANK_WAIT(x)	(x<<6)
#define	 TM_ADD_BANK4(x)	(x<<4)
#define  TM_PAR_CHECK(x)	(x<<3)
#define  TM_RW_WAIT(x)		(x<<2)
#define  TM_SRAM_TYPE(x)	(x)

#define TSRB_BA		0x80744	
#define TSRC_BA		0x80748	
#define TMABR_BA	0x8074c	
#define TPD_BA		0x80750	
#define TSRD_BA		0x80758	

#define TX_CONFIG	0x80760
#define  DRF_THRESH(x)		(x<<22)
#define  TX_UT_MODE(x)		(x<<21)
#define  TX_VCI_MASK(x)		(x<<17)
#define  LBFREE_CNT(x)		(x)

#define TXAAL5_PROTO	0x80764
#define  CPCS_UU(x)		(x<<8)
#define  CPI(x)			(x)

#define RCMCONFIG	0x80780
#define  RM_DESL2(x)		(x<<10)
#define  RM_BANK_WAIT(x)	(x<<6)
#define  RM_ADD_BANK(x)		(x<<4)
#define  RM_PAR_CHECK(x)	(x<<3)
#define  RM_RW_WAIT(x)		(x<<2)
#define  RM_SRAM_TYPE(x)	(x)

#define RCMRSRB_BA	0x80784
#define RCMLBM_BA	0x80788
#define RCMABR_BA	0x8078c

#define RC_CONFIG	0x807c0
#define  UT_RD_DELAY(x)		(x<<11)
#define  WRAP_MODE(x)		(x<<10)
#define  RC_UT_MODE(x)		(x<<9)
#define  RX_ENABLE		(1<<8)
#define  RX_VALVP(x)		(x<<4)
#define  RX_VALVC(x)		(x)

#define MCC		0x807c4
#define OEC		0x807c8
#define DCC		0x807cc
#define CEC		0x807d0

#define HSP_BA		0x807f0

#define LB_CONFIG	0x807f4
#define  LB_SIZE(x)		(x)

#define CON_DAT		0x807f8
#define CON_CTL		0x807fc
#define  CON_CTL_MBOX		(2<<30)
#define  CON_CTL_TCM		(1<<30)
#define  CON_CTL_RCM		(0<<30)
#define  CON_CTL_WRITE		(1<<29)
#define  CON_CTL_READ		(0<<29)
#define  CON_CTL_BUSY		(1<<28)
#define  CON_BYTE_DISABLE_3	(1<<22)		/* 24..31 */
#define  CON_BYTE_DISABLE_2	(1<<21)		/* 16..23 */
#define  CON_BYTE_DISABLE_1	(1<<20)		/* 8..15 */
#define  CON_BYTE_DISABLE_0	(1<<19)		/* 0..7 */
#define  CON_CTL_ADDR(x)	(x)

#define FRAMER		0x80800		/* to 0x80bfc */

/* 3.3 network controller (internal) mailbox registers */

#define CS_STPER0	0x0
	/* ... */
#define CS_STPER31	0x01f

#define CS_STTIM0	0x020
	/* ... */
#define CS_STTIM31	0x03f

#define CS_TGRLD0	0x040
	/* ... */
#define CS_TGRLD15	0x04f

#define CS_ERTHR0	0x050
#define CS_ERTHR1	0x051
#define CS_ERTHR2	0x052
#define CS_ERTHR3	0x053
#define CS_ERTHR4	0x054
#define CS_ERCTL0	0x055
#define  TX_ENABLE		(1<<28)
#define  ER_ENABLE		(1<<27)
#define CS_ERCTL1	0x056
#define CS_ERCTL2	0x057
#define CS_ERSTAT0	0x058
#define CS_ERSTAT1	0x059

#define CS_RTCCT	0x060
#define CS_RTFWC	0x061
#define CS_RTFWR	0x062
#define CS_RTFTC	0x063
#define CS_RTATR	0x064

#define CS_TFBSET	0x070
#define CS_TFBADD	0x071
#define CS_TFBSUB	0x072
#define CS_WCRMAX	0x073
#define CS_WCRMIN	0x074
#define CS_WCRINC	0x075
#define CS_WCRDEC	0x076
#define CS_WCRCEIL	0x077
#define CS_BWDCNT	0x078

#define CS_OTPPER	0x080
#define CS_OTWPER	0x081
#define CS_OTTLIM	0x082
#define CS_OTTCNT	0x083

#define CS_HGRRT0	0x090
	/* ... */
#define CS_HGRRT7	0x097

#define CS_ORPTRS	0x0a0

#define RXCON_CLOSE	0x100


#define RCM_MEM_SIZE	0x10000		/* 1M of 32-bit registers */
#define TCM_MEM_SIZE	0x20000		/* 2M of 32-bit registers */

/* 2.5 transmit connection memory registers */

#define TSR0_CONN_STATE(x)	((x>>28) & 0x7)
#define TSR0_USE_WMIN		(1<<23)
#define TSR0_GROUP(x)		((x & 0x7)<<18)
#define TSR0_ABR		(2<<16)
#define TSR0_UBR		(1<<16)
#define TSR0_CBR		(0<<16)
#define TSR0_PROT		(1<<15)
#define TSR0_AAL0_SDU		(2<<12)
#define TSR0_AAL0		(1<<12)
#define TSR0_AAL5		(0<<12)
#define TSR0_HALT_ER		(1<<11)
#define TSR0_MARK_CI		(1<<10)
#define TSR0_MARK_ER		(1<<9)
#define TSR0_UPDATE_GER		(1<<8)
#define TSR0_RC_INDEX(x)	(x & 0x1F)

#define TSR1_PCR(x)		((x & 0x7FFF)<<16)
#define TSR1_MCR(x)		(x & 0x7FFF)

#define TSR2_ACR(x)		((x & 0x7FFF)<<16)

#define TSR3_NRM_CNT(x)		((x & 0xFF)<<24)
#define TSR3_CRM_CNT(x)		(x & 0xFFFF)

#define TSR4_FLUSH_CONN		(1<<31)
#define TSR4_SESSION_ENDED	(1<<30)
#define TSR4_CRC10		(1<<28)
#define TSR4_NULL_CRC10		(1<<27)
#define TSR4_PROT		(1<<26)
#define TSR4_AAL0_SDU		(2<<23)
#define TSR4_AAL0		(1<<23)
#define TSR4_AAL5		(0<<23)

#define TSR9_OPEN_CONN		(1<<20)

#define TSR11_ICR(x)		((x & 0x7FFF)<<16)
#define TSR11_TRM(x)		((x & 0x7)<<13)
#define TSR11_NRM(x)		((x & 0x7)<<10)
#define TSR11_ADTF(x)		(x & 0x3FF)

#define TSR13_RDF(x)		((x & 0xF)<<23)
#define TSR13_RIF(x)		((x & 0xF)<<19)
#define TSR13_CDF(x)		((x & 0x7)<<16)
#define TSR13_CRM(x)		(x & 0xFFFF)

#define TSR14_DELETE		(1<<31)
#define TSR14_ABR_CLOSE		(1<<16)

/* 2.7.1 per connection receieve state registers */

#define RSR0_START_PDU	(1<<10)
#define RSR0_OPEN_CONN	(1<<6)
#define RSR0_CLOSE_CONN	(0<<6)
#define RSR0_PPD_ENABLE	(1<<5)
#define RSR0_EPD_ENABLE	(1<<4)
#define RSR0_TCP_CKSUM	(1<<3)
#define RSR0_AAL5		(0)
#define RSR0_AAL0		(1)
#define RSR0_AAL0_SDU		(2)
#define RSR0_RAWCELL		(3)
#define RSR0_RAWCELL_CRC10	(4)

#define RSR1_AQI_ENABLE	(1<<20)
#define RSR1_RBPL_ONLY	(1<<19)
#define RSR1_GROUP(x)	((x)<<16)

#define RSR4_AQI_ENABLE (1<<30)
#define RSR4_GROUP(x)	((x)<<27)
#define RSR4_RBPL_ONLY	(1<<26)

/* 2.1.4 transmit packet descriptor */

#define	TPD_USERCELL		0x0
#define	TPD_SEGMENT_OAMF5	0x4
#define	TPD_END2END_OAMF5	0x5
#define	TPD_RMCELL		0x6
#define TPD_CELLTYPE(x)		(x<<3)
#define TPD_EOS			(1<<2)
#define TPD_CLP			(1<<1)
#define TPD_INT			(1<<0)
#define TPD_LST		(1<<31)

/* table 4.3 serial eeprom information */

#define PROD_ID		0x08	/* char[] */
#define  PROD_ID_LEN	30
#define HW_REV		0x26	/* char[] */
#define M_SN		0x3a	/* integer */
#define MEDIA		0x3e	/* integer */
#define  HE155MM	0x26
#define  HE622MM	0x27
#define  HE155SM	0x46
#define  HE622SM	0x47
#define MAC_ADDR	0x42	/* char[] */

#define CS_LOW		0x0
#define CS_HIGH		ID_CS /* HOST_CNTL_ID_PROM_SEL */
#define CLK_LOW		0x0
#define CLK_HIGH	ID_CLOCK /* HOST_CNTL_ID_PROM_CLOCK */
#define SI_HIGH		ID_DIN /* HOST_CNTL_ID_PROM_DATA_IN */
#define EEPROM_DELAY	400 /* microseconds */

#endif /* _HE_H_ */
