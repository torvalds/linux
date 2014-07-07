/* drivers/atm/firestream.h - FireStream 155 (MB86697) and
 *                            FireStream  50 (MB86695) device driver 
 */
 
/* Written & (C) 2000 by R.E.Wolff@BitWizard.nl 
 * Copied snippets from zatm.c by Werner Almesberger, EPFL LRC/ICA 
 * and ambassador.c Copyright (C) 1995-1999  Madge Networks Ltd 
 */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  The GNU GPL is contained in /usr/doc/copyright/GPL on a Debian
  system and in the file COPYING in the Linux kernel source.
*/


/***********************************************************************
 *                  first the defines for the chip.                    *
 ***********************************************************************/


/********************* General chip parameters. ************************/

#define FS_NR_FREE_POOLS   8
#define FS_NR_RX_QUEUES    4


/********************* queues and queue access macros ******************/


/* A queue entry. */
struct FS_QENTRY {
	u32 cmd;
	u32 p0, p1, p2;
};


/* A freepool entry. */
struct FS_BPENTRY {
	u32 flags;
	u32 next;
	u32 bsa;
	u32 aal_bufsize;

	/* The hardware doesn't look at this, but we need the SKB somewhere... */
	struct sk_buff *skb;
	struct freepool *fp;
	struct fs_dev *dev;
};


#define STATUS_CODE(qe)  ((qe->cmd >> 22) & 0x3f)


/* OFFSETS against the base of a QUEUE... */
#define QSA     0x00
#define QEA     0x04
#define QRP     0x08
#define QWP     0x0c
#define QCNF    0x10   /* Only for Release queues! */
/* Not for the transmit pending queue. */


/* OFFSETS against the base of a FREE POOL... */
#define FPCNF   0x00
#define FPSA    0x04
#define FPEA    0x08
#define FPCNT   0x0c
#define FPCTU   0x10

#define Q_SA(b)     (b + QSA )
#define Q_EA(b)     (b + QEA )
#define Q_RP(b)     (b + QRP )
#define Q_WP(b)     (b + QWP )
#define Q_CNF(b)    (b + QCNF)

#define FP_CNF(b)   (b + FPCNF)
#define FP_SA(b)    (b + FPSA)
#define FP_EA(b)    (b + FPEA)
#define FP_CNT(b)   (b + FPCNT)
#define FP_CTU(b)   (b + FPCTU)

/* bits in a queue register. */
#define Q_FULL      0x1
#define Q_EMPTY     0x2
#define Q_INCWRAP   0x4
#define Q_ADDR_MASK 0xfffffff0

/* bits in a FreePool config register */
#define RBFP_RBS    (0x1 << 16)
#define RBFP_RBSVAL (0x1 << 15)
#define RBFP_CME    (0x1 << 12)
#define RBFP_DLP    (0x1 << 11)
#define RBFP_BFPWT  (0x1 <<  0)




/* FireStream commands. */
#define QE_CMD_NULL             (0x00 << 22)
#define QE_CMD_REG_RD           (0x01 << 22)
#define QE_CMD_REG_RDM          (0x02 << 22)
#define QE_CMD_REG_WR           (0x03 << 22)
#define QE_CMD_REG_WRM          (0x04 << 22)
#define QE_CMD_CONFIG_TX        (0x05 << 22)
#define QE_CMD_CONFIG_RX        (0x06 << 22)
#define QE_CMD_PRP_RD           (0x07 << 22)
#define QE_CMD_PRP_RDM          (0x2a << 22)
#define QE_CMD_PRP_WR           (0x09 << 22)
#define QE_CMD_PRP_WRM          (0x2b << 22)
#define QE_CMD_RX_EN            (0x0a << 22)
#define QE_CMD_RX_PURGE         (0x0b << 22)
#define QE_CMD_RX_PURGE_INH     (0x0c << 22)
#define QE_CMD_TX_EN            (0x0d << 22)
#define QE_CMD_TX_PURGE         (0x0e << 22)
#define QE_CMD_TX_PURGE_INH     (0x0f << 22)
#define QE_CMD_RST_CG           (0x10 << 22)
#define QE_CMD_SET_CG           (0x11 << 22)
#define QE_CMD_RST_CLP          (0x12 << 22)
#define QE_CMD_SET_CLP          (0x13 << 22)
#define QE_CMD_OVERRIDE         (0x14 << 22)
#define QE_CMD_ADD_BFP          (0x15 << 22)
#define QE_CMD_DUMP_TX          (0x16 << 22)
#define QE_CMD_DUMP_RX          (0x17 << 22)
#define QE_CMD_LRAM_RD          (0x18 << 22)
#define QE_CMD_LRAM_RDM         (0x28 << 22)
#define QE_CMD_LRAM_WR          (0x19 << 22)
#define QE_CMD_LRAM_WRM         (0x29 << 22)
#define QE_CMD_LRAM_BSET        (0x1a << 22)
#define QE_CMD_LRAM_BCLR        (0x1b << 22)
#define QE_CMD_CONFIG_SEGM      (0x1c << 22)
#define QE_CMD_READ_SEGM        (0x1d << 22)
#define QE_CMD_CONFIG_ROUT      (0x1e << 22)
#define QE_CMD_READ_ROUT        (0x1f << 22)
#define QE_CMD_CONFIG_TM        (0x20 << 22)
#define QE_CMD_READ_TM          (0x21 << 22)
#define QE_CMD_CONFIG_TXBM      (0x22 << 22)
#define QE_CMD_READ_TXBM        (0x23 << 22)
#define QE_CMD_CONFIG_RXBM      (0x24 << 22)
#define QE_CMD_READ_RXBM        (0x25 << 22)
#define QE_CMD_CONFIG_REAS      (0x26 << 22)
#define QE_CMD_READ_REAS        (0x27 << 22)

#define QE_TRANSMIT_DE          (0x0 << 30)
#define QE_CMD_LINKED           (0x1 << 30)
#define QE_CMD_IMM              (0x2 << 30)
#define QE_CMD_IMM_INQ          (0x3 << 30)

#define TD_EPI                  (0x1 << 27)
#define TD_COMMAND              (0x1 << 28)

#define TD_DATA                 (0x0 << 29)
#define TD_RM_CELL              (0x1 << 29)
#define TD_OAM_CELL             (0x2 << 29)
#define TD_OAM_CELL_SEGMENT     (0x3 << 29)

#define TD_BPI                  (0x1 << 20)

#define FP_FLAGS_EPI            (0x1 << 27)


#define TX_PQ(i)  (0x00  + (i) * 0x10)
#define TXB_RQ    (0x20)
#define ST_Q      (0x48)
#define RXB_FP(i) (0x90  + (i) * 0x14)
#define RXB_RQ(i) (0x134 + (i) * 0x14)


#define TXQ_HP 0
#define TXQ_LP 1

/* Phew. You don't want to know how many revisions these simple queue
 * address macros went through before I got them nice and compact as
 * they are now. -- REW
 */


/* And now for something completely different: 
 * The rest of the registers... */


#define CMDR0 0x34
#define CMDR1 0x38
#define CMDR2 0x3c
#define CMDR3 0x40


#define SARMODE0     0x5c

#define SARMODE0_TXVCS_0    (0x0 << 0)
#define SARMODE0_TXVCS_1k   (0x1 << 0)
#define SARMODE0_TXVCS_2k   (0x2 << 0)
#define SARMODE0_TXVCS_4k   (0x3 << 0)
#define SARMODE0_TXVCS_8k   (0x4 << 0)
#define SARMODE0_TXVCS_16k  (0x5 << 0)
#define SARMODE0_TXVCS_32k  (0x6 << 0)
#define SARMODE0_TXVCS_64k  (0x7 << 0)
#define SARMODE0_TXVCS_32   (0x8 << 0)

#define SARMODE0_ABRVCS_0   (0x0 << 4)
#define SARMODE0_ABRVCS_512 (0x1 << 4)
#define SARMODE0_ABRVCS_1k  (0x2 << 4)
#define SARMODE0_ABRVCS_2k  (0x3 << 4)
#define SARMODE0_ABRVCS_4k  (0x4 << 4)
#define SARMODE0_ABRVCS_8k  (0x5 << 4)
#define SARMODE0_ABRVCS_16k (0x6 << 4)
#define SARMODE0_ABRVCS_32k (0x7 << 4)
#define SARMODE0_ABRVCS_32  (0x9 << 4) /* The others are "8", this one really has to 
					  be 9. Tell me you don't believe me. -- REW */

#define SARMODE0_RXVCS_0    (0x0 << 8)
#define SARMODE0_RXVCS_1k   (0x1 << 8)
#define SARMODE0_RXVCS_2k   (0x2 << 8)
#define SARMODE0_RXVCS_4k   (0x3 << 8)
#define SARMODE0_RXVCS_8k   (0x4 << 8)
#define SARMODE0_RXVCS_16k  (0x5 << 8)
#define SARMODE0_RXVCS_32k  (0x6 << 8)
#define SARMODE0_RXVCS_64k  (0x7 << 8)
#define SARMODE0_RXVCS_32   (0x8 << 8) 

#define SARMODE0_CALSUP_1  (0x0 << 12)
#define SARMODE0_CALSUP_2  (0x1 << 12)
#define SARMODE0_CALSUP_3  (0x2 << 12)
#define SARMODE0_CALSUP_4  (0x3 << 12)

#define SARMODE0_PRPWT_FS50_0  (0x0 << 14)
#define SARMODE0_PRPWT_FS50_2  (0x1 << 14)
#define SARMODE0_PRPWT_FS50_5  (0x2 << 14)
#define SARMODE0_PRPWT_FS50_11 (0x3 << 14)

#define SARMODE0_PRPWT_FS155_0 (0x0 << 14)
#define SARMODE0_PRPWT_FS155_1 (0x1 << 14)
#define SARMODE0_PRPWT_FS155_2 (0x2 << 14)
#define SARMODE0_PRPWT_FS155_3 (0x3 << 14)

#define SARMODE0_SRTS0     (0x1 << 23)
#define SARMODE0_SRTS1     (0x1 << 24)

#define SARMODE0_RUN       (0x1 << 25)

#define SARMODE0_UNLOCK    (0x1 << 26)
#define SARMODE0_CWRE      (0x1 << 27)


#define SARMODE0_INTMODE_READCLEAR          (0x0 << 28)
#define SARMODE0_INTMODE_READNOCLEAR        (0x1 << 28)
#define SARMODE0_INTMODE_READNOCLEARINHIBIT (0x2 << 28)
#define SARMODE0_INTMODE_READCLEARINHIBIT   (0x3 << 28)  /* Tell me you don't believe me. */

#define SARMODE0_GINT      (0x1 << 30)
#define SARMODE0_SHADEN    (0x1 << 31)


#define SARMODE1     0x60


#define SARMODE1_TRTL_SHIFT 0   /* Program to 0 */
#define SARMODE1_RRTL_SHIFT 4   /* Program to 0 */

#define SARMODE1_TAGM       (0x1 <<  8)  /* Program to 0 */

#define SARMODE1_HECM0      (0x1 <<  9)
#define SARMODE1_HECM1      (0x1 << 10)
#define SARMODE1_HECM2      (0x1 << 11)

#define SARMODE1_GFCE       (0x1 << 14)
#define SARMODE1_GFCR       (0x1 << 15)
#define SARMODE1_PMS        (0x1 << 18)
#define SARMODE1_GPRI       (0x1 << 19)
#define SARMODE1_GPAS       (0x1 << 20)
#define SARMODE1_GVAS       (0x1 << 21)
#define SARMODE1_GNAM       (0x1 << 22)
#define SARMODE1_GPLEN      (0x1 << 23)
#define SARMODE1_DUMPE      (0x1 << 24)
#define SARMODE1_OAMCRC     (0x1 << 25)
#define SARMODE1_DCOAM      (0x1 << 26)
#define SARMODE1_DCRM       (0x1 << 27)
#define SARMODE1_TSTLP      (0x1 << 28)
#define SARMODE1_DEFHEC     (0x1 << 29)


#define ISR      0x64
#define IUSR     0x68
#define IMR      0x6c

#define ISR_LPCO          (0x1 <<  0)
#define ISR_DPCO          (0x1 <<  1)
#define ISR_RBRQ0_W       (0x1 <<  2)
#define ISR_RBRQ1_W       (0x1 <<  3)
#define ISR_RBRQ2_W       (0x1 <<  4)
#define ISR_RBRQ3_W       (0x1 <<  5)
#define ISR_RBRQ0_NF      (0x1 <<  6)
#define ISR_RBRQ1_NF      (0x1 <<  7)
#define ISR_RBRQ2_NF      (0x1 <<  8)
#define ISR_RBRQ3_NF      (0x1 <<  9)
#define ISR_BFP_SC        (0x1 << 10)
#define ISR_INIT          (0x1 << 11)
#define ISR_INIT_ERR      (0x1 << 12) /* Documented as "reserved" */
#define ISR_USCEO         (0x1 << 13)
#define ISR_UPEC0         (0x1 << 14)
#define ISR_VPFCO         (0x1 << 15)
#define ISR_CRCCO         (0x1 << 16)
#define ISR_HECO          (0x1 << 17)
#define ISR_TBRQ_W        (0x1 << 18)
#define ISR_TBRQ_NF       (0x1 << 19)
#define ISR_CTPQ_E        (0x1 << 20)
#define ISR_GFC_C0        (0x1 << 21)
#define ISR_PCI_FTL       (0x1 << 22)
#define ISR_CSQ_W         (0x1 << 23)
#define ISR_CSQ_NF        (0x1 << 24)
#define ISR_EXT_INT       (0x1 << 25)
#define ISR_RXDMA_S       (0x1 << 26)


#define TMCONF 0x78
/* Bits? */


#define CALPRESCALE 0x7c
/* Bits? */

#define CELLOSCONF 0x84
#define CELLOSCONF_COTS   (0x1 << 28)
#define CELLOSCONF_CEN    (0x1 << 27)
#define CELLOSCONF_SC8    (0x3 << 24)
#define CELLOSCONF_SC4    (0x2 << 24)
#define CELLOSCONF_SC2    (0x1 << 24)
#define CELLOSCONF_SC1    (0x0 << 24)

#define CELLOSCONF_COBS   (0x1 << 16)
#define CELLOSCONF_COPK   (0x1 <<  8)
#define CELLOSCONF_COST   (0x1 <<  0)
/* Bits? */

#define RAS0 0x1bc
#define RAS0_DCD_XHLT (0x1 << 31)

#define RAS0_VPSEL    (0x1 << 16)
#define RAS0_VCSEL    (0x1 <<  0)

#define RAS1 0x1c0
#define RAS1_UTREG    (0x1 << 5)


#define DMAMR 0x1cc
#define DMAMR_TX_MODE_FULL (0x0 << 0)
#define DMAMR_TX_MODE_PART (0x1 << 0)
#define DMAMR_TX_MODE_NONE (0x2 << 0) /* And 3 */



#define RAS2 0x280

#define RAS2_NNI  (0x1 << 0)
#define RAS2_USEL (0x1 << 1)
#define RAS2_UBS  (0x1 << 2)



struct fs_transmit_config {
	u32 flags;
	u32 atm_hdr;
	u32 TMC[4];
	u32 spec;
	u32 rtag[3];
};

#define TC_FLAGS_AAL5      (0x0 << 29)
#define TC_FLAGS_TRANSPARENT_PAYLOAD (0x1 << 29)
#define TC_FLAGS_TRANSPARENT_CELL    (0x2 << 29)
#define TC_FLAGS_STREAMING (0x1 << 28)
#define TC_FLAGS_PACKET    (0x0) 
#define TC_FLAGS_TYPE_ABR  (0x0 << 22)
#define TC_FLAGS_TYPE_CBR  (0x1 << 22)
#define TC_FLAGS_TYPE_VBR  (0x2 << 22)
#define TC_FLAGS_TYPE_UBR  (0x3 << 22)
#define TC_FLAGS_CAL0      (0x0 << 20)
#define TC_FLAGS_CAL1      (0x1 << 20)
#define TC_FLAGS_CAL2      (0x2 << 20)
#define TC_FLAGS_CAL3      (0x3 << 20)


#define RC_FLAGS_NAM        (0x1 << 13)
#define RC_FLAGS_RXBM_PSB   (0x0 << 14)
#define RC_FLAGS_RXBM_CIF   (0x1 << 14)
#define RC_FLAGS_RXBM_PMB   (0x2 << 14)
#define RC_FLAGS_RXBM_STR   (0x4 << 14)
#define RC_FLAGS_RXBM_SAF   (0x6 << 14)
#define RC_FLAGS_RXBM_POS   (0x6 << 14)
#define RC_FLAGS_BFPS       (0x1 << 17)

#define RC_FLAGS_BFPS_BFP   (0x1 << 17)

#define RC_FLAGS_BFPS_BFP0  (0x0 << 17)
#define RC_FLAGS_BFPS_BFP1  (0x1 << 17)
#define RC_FLAGS_BFPS_BFP2  (0x2 << 17)
#define RC_FLAGS_BFPS_BFP3  (0x3 << 17)
#define RC_FLAGS_BFPS_BFP4  (0x4 << 17)
#define RC_FLAGS_BFPS_BFP5  (0x5 << 17)
#define RC_FLAGS_BFPS_BFP6  (0x6 << 17)
#define RC_FLAGS_BFPS_BFP7  (0x7 << 17)
#define RC_FLAGS_BFPS_BFP01 (0x8 << 17)
#define RC_FLAGS_BFPS_BFP23 (0x9 << 17)
#define RC_FLAGS_BFPS_BFP45 (0xa << 17)
#define RC_FLAGS_BFPS_BFP67 (0xb << 17)
#define RC_FLAGS_BFPS_BFP07 (0xc << 17)
#define RC_FLAGS_BFPS_BFP27 (0xd << 17)
#define RC_FLAGS_BFPS_BFP47 (0xe << 17)

#define RC_FLAGS_BFPP       (0x1 << 21)
#define RC_FLAGS_TEVC       (0x1 << 22)
#define RC_FLAGS_TEP        (0x1 << 23)
#define RC_FLAGS_AAL5       (0x0 << 24)
#define RC_FLAGS_TRANSP     (0x1 << 24)
#define RC_FLAGS_TRANSC     (0x2 << 24)
#define RC_FLAGS_ML         (0x1 << 27)
#define RC_FLAGS_TRBRM      (0x1 << 28)
#define RC_FLAGS_PRI        (0x1 << 29)
#define RC_FLAGS_HOAM       (0x1 << 30)
#define RC_FLAGS_CRC10      (0x1 << 31)


#define RAC 0x1c8
#define RAM 0x1c4



/************************************************************************
 *         Then the datastructures that the DRIVER uses.                *
 ************************************************************************/

#define TXQ_NENTRIES  32
#define RXRQ_NENTRIES 1024


struct fs_vcc {
	int channo;
	wait_queue_head_t close_wait;
	struct sk_buff *last_skb;
};


struct queue {
	struct FS_QENTRY *sa, *ea;  
	int offset;
};

struct freepool {
	int offset;
	int bufsize;
	int nr_buffers;
	int n;
};


struct fs_dev {
	struct fs_dev *next;		/* other FS devices */
	int flags;

	unsigned char irq;		/* IRQ */
	struct pci_dev *pci_dev;	/* PCI stuff */
	struct atm_dev *atm_dev;
	struct timer_list timer;

	unsigned long hw_base;		/* mem base address */
	void __iomem *base;             /* Mapping of base address */
	int channo;
	unsigned long channel_mask;

	struct queue    hp_txq, lp_txq, tx_relq, st_q;
	struct freepool rx_fp[FS_NR_FREE_POOLS];
	struct queue    rx_rq[FS_NR_RX_QUEUES];

	int nchannels;
	struct atm_vcc **atm_vccs;
	void *tx_inuse;
	int ntxpckts;
};




/* Number of channesl that the FS50 supports. */
#define FS50_CHANNEL_BITS  5
#define FS50_NR_CHANNELS      (1 << FS50_CHANNEL_BITS)

         
#define FS_DEV(atm_dev) ((struct fs_dev *) (atm_dev)->dev_data)
#define FS_VCC(atm_vcc) ((struct fs_vcc *) (atm_vcc)->dev_data)


#define FS_IS50  0x1
#define FS_IS155 0x2

#define IS_FS50(dev)  (dev->flags & FS_IS50)
#define IS_FS155(dev) (dev->flags & FS_IS155)
 
/* Within limits this is user-configurable. */
/* Note: Currently the sum (10 -> 1k channels) is hardcoded in the driver. */
#define FS155_VPI_BITS 4
#define FS155_VCI_BITS 6

#define FS155_CHANNEL_BITS  (FS155_VPI_BITS + FS155_VCI_BITS)
#define FS155_NR_CHANNELS   (1 << FS155_CHANNEL_BITS)
