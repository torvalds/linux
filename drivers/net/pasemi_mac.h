/*
 * Copyright (C) 2006 PA Semi, Inc
 *
 * Driver for the PA6T-1682M onchip 1G/10G Ethernet MACs, soft state and
 * hardware register layouts.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef PASEMI_MAC_H
#define PASEMI_MAC_H

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/phy.h>

struct pasemi_mac_txring {
	spinlock_t	 lock;
	struct pas_dma_xct_descr	*desc;
	dma_addr_t	 dma;
	unsigned int	 size;
	unsigned int	 next_to_use;
	unsigned int	 next_to_clean;
	struct pasemi_mac_buffer *desc_info;
	char		 irq_name[10];  /* "eth%d tx" */
};

struct pasemi_mac_rxring {
	spinlock_t	 lock;
	struct pas_dma_xct_descr	*desc;	/* RX channel descriptor ring */
	dma_addr_t	 dma;
	u64		*buffers;	/* RX interface buffer ring */
	dma_addr_t	 buf_dma;
	unsigned int	 size;
	unsigned int	 next_to_fill;
	unsigned int	 next_to_clean;
	struct pasemi_mac_buffer *desc_info;
	char		 irq_name[10];  /* "eth%d rx" */
};

struct pasemi_mac {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct pci_dev *dma_pdev;
	struct pci_dev *iob_pdev;
	struct phy_device *phydev;
	struct net_device_stats stats;

	/* Pointer to the cacheable per-channel status registers */
	u64	*rx_status;
	u64	*tx_status;

	u8		type;
#define MAC_TYPE_GMAC	1
#define MAC_TYPE_XAUI	2
	u32	dma_txch;
	u32	dma_if;
	u32	dma_rxch;

	u8		mac_addr[6];

	struct timer_list	rxtimer;

	struct pasemi_mac_txring *tx;
	struct pasemi_mac_rxring *rx;
	unsigned long	tx_irq;
	unsigned long	rx_irq;
	int	link;
	int	speed;
	int	duplex;

	unsigned int	msg_enable;
	char	phy_id[BUS_ID_SIZE];
};

/* Software status descriptor (desc_info) */
struct pasemi_mac_buffer {
	struct sk_buff *skb;
	dma_addr_t	dma;
};


/* status register layout in IOB region, at 0xfb800000 */
struct pasdma_status {
	u64 rx_sta[64];
	u64 tx_sta[20];
};

/* descriptor structure */
struct pas_dma_xct_descr {
	union {
		u64	mactx;
		u64	macrx;
	};
	union {
		u64	ptr;
		u64	rxb;
	};
};

/* MAC CFG register offsets */

enum {
	PAS_MAC_CFG_PCFG = 0x80,
	PAS_MAC_CFG_TXP = 0x98,
	PAS_MAC_IPC_CHNL = 0x208,
};

/* MAC CFG register fields */
#define PAS_MAC_CFG_PCFG_PE		0x80000000
#define PAS_MAC_CFG_PCFG_CE		0x40000000
#define PAS_MAC_CFG_PCFG_BU		0x20000000
#define PAS_MAC_CFG_PCFG_TT		0x10000000
#define PAS_MAC_CFG_PCFG_TSR_M		0x0c000000
#define PAS_MAC_CFG_PCFG_TSR_10M	0x00000000
#define PAS_MAC_CFG_PCFG_TSR_100M	0x04000000
#define PAS_MAC_CFG_PCFG_TSR_1G		0x08000000
#define PAS_MAC_CFG_PCFG_TSR_10G	0x0c000000
#define PAS_MAC_CFG_PCFG_T24		0x02000000
#define PAS_MAC_CFG_PCFG_PR		0x01000000
#define PAS_MAC_CFG_PCFG_CRO_M		0x00ff0000
#define PAS_MAC_CFG_PCFG_CRO_S	16
#define PAS_MAC_CFG_PCFG_IPO_M		0x0000ff00
#define PAS_MAC_CFG_PCFG_IPO_S	8
#define PAS_MAC_CFG_PCFG_S1		0x00000080
#define PAS_MAC_CFG_PCFG_IO_M		0x00000060
#define PAS_MAC_CFG_PCFG_IO_MAC		0x00000000
#define PAS_MAC_CFG_PCFG_IO_OFF		0x00000020
#define PAS_MAC_CFG_PCFG_IO_IND_ETH	0x00000040
#define PAS_MAC_CFG_PCFG_IO_IND_IP	0x00000060
#define PAS_MAC_CFG_PCFG_LP		0x00000010
#define PAS_MAC_CFG_PCFG_TS		0x00000008
#define PAS_MAC_CFG_PCFG_HD		0x00000004
#define PAS_MAC_CFG_PCFG_SPD_M		0x00000003
#define PAS_MAC_CFG_PCFG_SPD_10M	0x00000000
#define PAS_MAC_CFG_PCFG_SPD_100M	0x00000001
#define PAS_MAC_CFG_PCFG_SPD_1G		0x00000002
#define PAS_MAC_CFG_PCFG_SPD_10G	0x00000003
#define PAS_MAC_CFG_TXP_FCF		0x01000000
#define PAS_MAC_CFG_TXP_FCE		0x00800000
#define PAS_MAC_CFG_TXP_FC		0x00400000
#define PAS_MAC_CFG_TXP_FPC_M		0x00300000
#define PAS_MAC_CFG_TXP_FPC_S		20
#define PAS_MAC_CFG_TXP_FPC(x)		(((x) << PAS_MAC_CFG_TXP_FPC_S) & \
					 PAS_MAC_CFG_TXP_FPC_M)
#define PAS_MAC_CFG_TXP_RT		0x00080000
#define PAS_MAC_CFG_TXP_BL		0x00040000
#define PAS_MAC_CFG_TXP_SL_M		0x00030000
#define PAS_MAC_CFG_TXP_SL_S		16
#define PAS_MAC_CFG_TXP_SL(x)		(((x) << PAS_MAC_CFG_TXP_SL_S) & \
					 PAS_MAC_CFG_TXP_SL_M)
#define PAS_MAC_CFG_TXP_COB_M		0x0000f000
#define PAS_MAC_CFG_TXP_COB_S		12
#define PAS_MAC_CFG_TXP_COB(x)		(((x) << PAS_MAC_CFG_TXP_COB_S) & \
					 PAS_MAC_CFG_TXP_COB_M)
#define PAS_MAC_CFG_TXP_TIFT_M		0x00000f00
#define PAS_MAC_CFG_TXP_TIFT_S		8
#define PAS_MAC_CFG_TXP_TIFT(x)		(((x) << PAS_MAC_CFG_TXP_TIFT_S) & \
					 PAS_MAC_CFG_TXP_TIFT_M)
#define PAS_MAC_CFG_TXP_TIFG_M		0x000000ff
#define PAS_MAC_CFG_TXP_TIFG_S		0
#define PAS_MAC_CFG_TXP_TIFG(x)		(((x) << PAS_MAC_CFG_TXP_TIFG_S) & \
					 PAS_MAC_CFG_TXP_TIFG_M)

#define PAS_MAC_IPC_CHNL_DCHNO_M	0x003f0000
#define PAS_MAC_IPC_CHNL_DCHNO_S	16
#define PAS_MAC_IPC_CHNL_DCHNO(x)	(((x) << PAS_MAC_IPC_CHNL_DCHNO_S) & \
					 PAS_MAC_IPC_CHNL_DCHNO_M)
#define PAS_MAC_IPC_CHNL_BCH_M		0x0000003f
#define PAS_MAC_IPC_CHNL_BCH_S		0
#define PAS_MAC_IPC_CHNL_BCH(x)		(((x) << PAS_MAC_IPC_CHNL_BCH_S) & \
					 PAS_MAC_IPC_CHNL_BCH_M)

/* All these registers live in the PCI configuration space for the DMA PCI
 * device. Use the normal PCI config access functions for them.
 */
enum {
	PAS_DMA_COM_TXCMD = 0x100,	/* Transmit Command Register  */
	PAS_DMA_COM_TXSTA = 0x104,	/* Transmit Status Register   */
	PAS_DMA_COM_RXCMD = 0x108,	/* Receive Command Register   */
	PAS_DMA_COM_RXSTA = 0x10c,	/* Receive Status Register    */
};
#define PAS_DMA_COM_TXCMD_EN	0x00000001 /* enable */
#define PAS_DMA_COM_TXSTA_ACT	0x00000001 /* active */
#define PAS_DMA_COM_RXCMD_EN	0x00000001 /* enable */
#define PAS_DMA_COM_RXSTA_ACT	0x00000001 /* active */


/* Per-interface and per-channel registers */
#define _PAS_DMA_RXINT_STRIDE		0x20
#define PAS_DMA_RXINT_RCMDSTA(i)	(0x200+(i)*_PAS_DMA_RXINT_STRIDE)
#define    PAS_DMA_RXINT_RCMDSTA_EN	0x00000001
#define    PAS_DMA_RXINT_RCMDSTA_ST	0x00000002
#define    PAS_DMA_RXINT_RCMDSTA_MBT	0x00000008
#define    PAS_DMA_RXINT_RCMDSTA_MDR	0x00000010
#define    PAS_DMA_RXINT_RCMDSTA_MOO	0x00000020
#define    PAS_DMA_RXINT_RCMDSTA_MBP	0x00000040
#define    PAS_DMA_RXINT_RCMDSTA_BT	0x00000800
#define    PAS_DMA_RXINT_RCMDSTA_DR	0x00001000
#define    PAS_DMA_RXINT_RCMDSTA_OO	0x00002000
#define    PAS_DMA_RXINT_RCMDSTA_BP	0x00004000
#define    PAS_DMA_RXINT_RCMDSTA_TB	0x00008000
#define    PAS_DMA_RXINT_RCMDSTA_ACT	0x00010000
#define    PAS_DMA_RXINT_RCMDSTA_DROPS_M	0xfffe0000
#define    PAS_DMA_RXINT_RCMDSTA_DROPS_S	17
#define PAS_DMA_RXINT_INCR(i)		(0x210+(i)*_PAS_DMA_RXINT_STRIDE)
#define    PAS_DMA_RXINT_INCR_INCR_M	0x0000ffff
#define    PAS_DMA_RXINT_INCR_INCR_S	0
#define    PAS_DMA_RXINT_INCR_INCR(x)	((x) & 0x0000ffff)
#define PAS_DMA_RXINT_BASEL(i)		(0x218+(i)*_PAS_DMA_RXINT_STRIDE)
#define    PAS_DMA_RXINT_BASEL_BRBL(x)	((x) & ~0x3f)
#define PAS_DMA_RXINT_BASEU(i)		(0x21c+(i)*_PAS_DMA_RXINT_STRIDE)
#define    PAS_DMA_RXINT_BASEU_BRBH(x)	((x) & 0xfff)
#define    PAS_DMA_RXINT_BASEU_SIZ_M	0x3fff0000	/* # of cache lines worth of buffer ring */
#define    PAS_DMA_RXINT_BASEU_SIZ_S	16		/* 0 = 16K */
#define    PAS_DMA_RXINT_BASEU_SIZ(x)	(((x) << PAS_DMA_RXINT_BASEU_SIZ_S) & \
					 PAS_DMA_RXINT_BASEU_SIZ_M)


#define _PAS_DMA_TXCHAN_STRIDE	0x20    /* Size per channel		*/
#define _PAS_DMA_TXCHAN_TCMDSTA	0x300	/* Command / Status		*/
#define _PAS_DMA_TXCHAN_CFG	0x304	/* Configuration		*/
#define _PAS_DMA_TXCHAN_DSCRBU	0x308	/* Descriptor BU Allocation	*/
#define _PAS_DMA_TXCHAN_INCR	0x310	/* Descriptor increment		*/
#define _PAS_DMA_TXCHAN_CNT	0x314	/* Descriptor count/offset	*/
#define _PAS_DMA_TXCHAN_BASEL	0x318	/* Descriptor ring base (low)	*/
#define _PAS_DMA_TXCHAN_BASEU	0x31c	/*			(high)	*/
#define PAS_DMA_TXCHAN_TCMDSTA(c) (0x300+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define    PAS_DMA_TXCHAN_TCMDSTA_EN	0x00000001	/* Enabled */
#define    PAS_DMA_TXCHAN_TCMDSTA_ST	0x00000002	/* Stop interface */
#define    PAS_DMA_TXCHAN_TCMDSTA_ACT	0x00010000	/* Active */
#define PAS_DMA_TXCHAN_CFG(c)     (0x304+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define    PAS_DMA_TXCHAN_CFG_TY_IFACE	0x00000000	/* Type = interface */
#define    PAS_DMA_TXCHAN_CFG_TATTR_M	0x0000003c
#define    PAS_DMA_TXCHAN_CFG_TATTR_S	2
#define    PAS_DMA_TXCHAN_CFG_TATTR(x)	(((x) << PAS_DMA_TXCHAN_CFG_TATTR_S) & \
					 PAS_DMA_TXCHAN_CFG_TATTR_M)
#define    PAS_DMA_TXCHAN_CFG_WT_M	0x000001c0
#define    PAS_DMA_TXCHAN_CFG_WT_S	6
#define    PAS_DMA_TXCHAN_CFG_WT(x)	(((x) << PAS_DMA_TXCHAN_CFG_WT_S) & \
					 PAS_DMA_TXCHAN_CFG_WT_M)
#define    PAS_DMA_TXCHAN_CFG_CF	0x00001000	/* Clean first line */
#define    PAS_DMA_TXCHAN_CFG_CL	0x00002000	/* Clean last line */
#define    PAS_DMA_TXCHAN_CFG_UP	0x00004000	/* update tx descr when sent */
#define PAS_DMA_TXCHAN_INCR(c)    (0x310+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define PAS_DMA_TXCHAN_BASEL(c)   (0x318+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define    PAS_DMA_TXCHAN_BASEL_BRBL_M	0xffffffc0
#define    PAS_DMA_TXCHAN_BASEL_BRBL_S	0
#define    PAS_DMA_TXCHAN_BASEL_BRBL(x)	(((x) << PAS_DMA_TXCHAN_BASEL_BRBL_S) & \
					 PAS_DMA_TXCHAN_BASEL_BRBL_M)
#define PAS_DMA_TXCHAN_BASEU(c)   (0x31c+(c)*_PAS_DMA_TXCHAN_STRIDE)
#define    PAS_DMA_TXCHAN_BASEU_BRBH_M	0x00000fff
#define    PAS_DMA_TXCHAN_BASEU_BRBH_S	0
#define    PAS_DMA_TXCHAN_BASEU_BRBH(x)	(((x) << PAS_DMA_TXCHAN_BASEU_BRBH_S) & \
					 PAS_DMA_TXCHAN_BASEU_BRBH_M)
/* # of cache lines worth of buffer ring */
#define    PAS_DMA_TXCHAN_BASEU_SIZ_M	0x3fff0000
#define    PAS_DMA_TXCHAN_BASEU_SIZ_S	16		/* 0 = 16K */
#define    PAS_DMA_TXCHAN_BASEU_SIZ(x)	(((x) << PAS_DMA_TXCHAN_BASEU_SIZ_S) & \
					 PAS_DMA_TXCHAN_BASEU_SIZ_M)

#define _PAS_DMA_RXCHAN_STRIDE	0x20    /* Size per channel		*/
#define _PAS_DMA_RXCHAN_CCMDSTA	0x800	/* Command / Status		*/
#define _PAS_DMA_RXCHAN_CFG	0x804	/* Configuration		*/
#define _PAS_DMA_RXCHAN_INCR	0x810	/* Descriptor increment		*/
#define _PAS_DMA_RXCHAN_CNT	0x814	/* Descriptor count/offset	*/
#define _PAS_DMA_RXCHAN_BASEL	0x818	/* Descriptor ring base (low)	*/
#define _PAS_DMA_RXCHAN_BASEU	0x81c	/*			(high)	*/
#define PAS_DMA_RXCHAN_CCMDSTA(c) (0x800+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define    PAS_DMA_RXCHAN_CCMDSTA_EN	0x00000001	/* Enabled */
#define    PAS_DMA_RXCHAN_CCMDSTA_ST	0x00000002	/* Stop interface */
#define    PAS_DMA_RXCHAN_CCMDSTA_ACT	0x00010000	/* Active */
#define    PAS_DMA_RXCHAN_CCMDSTA_DU	0x00020000
#define PAS_DMA_RXCHAN_CFG(c)     (0x804+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define    PAS_DMA_RXCHAN_CFG_HBU_M	0x00000380
#define    PAS_DMA_RXCHAN_CFG_HBU_S	7
#define    PAS_DMA_RXCHAN_CFG_HBU(x)	(((x) << PAS_DMA_RXCHAN_CFG_HBU_S) & \
					 PAS_DMA_RXCHAN_CFG_HBU_M)
#define PAS_DMA_RXCHAN_INCR(c)    (0x810+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define PAS_DMA_RXCHAN_BASEL(c)   (0x818+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define    PAS_DMA_RXCHAN_BASEL_BRBL_M	0xffffffc0
#define    PAS_DMA_RXCHAN_BASEL_BRBL_S	0
#define    PAS_DMA_RXCHAN_BASEL_BRBL(x)	(((x) << PAS_DMA_RXCHAN_BASEL_BRBL_S) & \
					 PAS_DMA_RXCHAN_BASEL_BRBL_M)
#define PAS_DMA_RXCHAN_BASEU(c)   (0x81c+(c)*_PAS_DMA_RXCHAN_STRIDE)
#define    PAS_DMA_RXCHAN_BASEU_BRBH_M	0x00000fff
#define    PAS_DMA_RXCHAN_BASEU_BRBH_S	0
#define    PAS_DMA_RXCHAN_BASEU_BRBH(x)	(((x) << PAS_DMA_RXCHAN_BASEU_BRBH_S) & \
					 PAS_DMA_RXCHAN_BASEU_BRBH_M)
/* # of cache lines worth of buffer ring */
#define    PAS_DMA_RXCHAN_BASEU_SIZ_M	0x3fff0000
#define    PAS_DMA_RXCHAN_BASEU_SIZ_S	16		/* 0 = 16K */
#define    PAS_DMA_RXCHAN_BASEU_SIZ(x)	(((x) << PAS_DMA_RXCHAN_BASEU_SIZ_S) & \
					 PAS_DMA_RXCHAN_BASEU_SIZ_M)

#define    PAS_STATUS_PCNT_M		0x000000000000ffffull
#define    PAS_STATUS_PCNT_S		0
#define    PAS_STATUS_DCNT_M		0x00000000ffff0000ull
#define    PAS_STATUS_DCNT_S		16
#define    PAS_STATUS_BPCNT_M		0x0000ffff00000000ull
#define    PAS_STATUS_BPCNT_S		32
#define    PAS_STATUS_CAUSE_M		0xf000000000000000ull
#define    PAS_STATUS_TIMER		0x1000000000000000ull
#define    PAS_STATUS_ERROR		0x2000000000000000ull
#define    PAS_STATUS_SOFT		0x4000000000000000ull
#define    PAS_STATUS_INT		0x8000000000000000ull

#define PAS_IOB_DMA_RXCH_CFG(i)		(0x1100 + (i)*4)
#define    PAS_IOB_DMA_RXCH_CFG_CNTTH_M		0x00000fff
#define    PAS_IOB_DMA_RXCH_CFG_CNTTH_S		0
#define    PAS_IOB_DMA_RXCH_CFG_CNTTH(x)	(((x) << PAS_IOB_DMA_RXCH_CFG_CNTTH_S) & \
						 PAS_IOB_DMA_RXCH_CFG_CNTTH_M)
#define PAS_IOB_DMA_TXCH_CFG(i)		(0x1200 + (i)*4)
#define    PAS_IOB_DMA_TXCH_CFG_CNTTH_M		0x00000fff
#define    PAS_IOB_DMA_TXCH_CFG_CNTTH_S		0
#define    PAS_IOB_DMA_TXCH_CFG_CNTTH(x)	(((x) << PAS_IOB_DMA_TXCH_CFG_CNTTH_S) & \
						 PAS_IOB_DMA_TXCH_CFG_CNTTH_M)
#define PAS_IOB_DMA_RXCH_STAT(i)	(0x1300 + (i)*4)
#define    PAS_IOB_DMA_RXCH_STAT_INTGEN	0x00001000
#define    PAS_IOB_DMA_RXCH_STAT_CNTDEL_M	0x00000fff
#define    PAS_IOB_DMA_RXCH_STAT_CNTDEL_S	0
#define    PAS_IOB_DMA_RXCH_STAT_CNTDEL(x)	(((x) << PAS_IOB_DMA_RXCH_STAT_CNTDEL_S) &\
						 PAS_IOB_DMA_RXCH_STAT_CNTDEL_M)
#define PAS_IOB_DMA_TXCH_STAT(i)	(0x1400 + (i)*4)
#define    PAS_IOB_DMA_TXCH_STAT_INTGEN	0x00001000
#define    PAS_IOB_DMA_TXCH_STAT_CNTDEL_M	0x00000fff
#define    PAS_IOB_DMA_TXCH_STAT_CNTDEL_S	0
#define    PAS_IOB_DMA_TXCH_STAT_CNTDEL(x)	(((x) << PAS_IOB_DMA_TXCH_STAT_CNTDEL_S) &\
						 PAS_IOB_DMA_TXCH_STAT_CNTDEL_M)
#define PAS_IOB_DMA_RXCH_RESET(i)	(0x1500 + (i)*4)
#define    PAS_IOB_DMA_RXCH_RESET_PCNT_M	0xffff0000
#define    PAS_IOB_DMA_RXCH_RESET_PCNT_S	0
#define    PAS_IOB_DMA_RXCH_RESET_PCNT(x)	(((x) << PAS_IOB_DMA_RXCH_RESET_PCNT_S) & \
						 PAS_IOB_DMA_RXCH_RESET_PCNT_M)
#define    PAS_IOB_DMA_RXCH_RESET_PCNTRST	0x00000020
#define    PAS_IOB_DMA_RXCH_RESET_DCNTRST	0x00000010
#define    PAS_IOB_DMA_RXCH_RESET_TINTC		0x00000008
#define    PAS_IOB_DMA_RXCH_RESET_DINTC		0x00000004
#define    PAS_IOB_DMA_RXCH_RESET_SINTC		0x00000002
#define    PAS_IOB_DMA_RXCH_RESET_PINTC		0x00000001
#define PAS_IOB_DMA_TXCH_RESET(i)	(0x1600 + (i)*4)
#define    PAS_IOB_DMA_TXCH_RESET_PCNT_M	0xffff0000
#define    PAS_IOB_DMA_TXCH_RESET_PCNT_S	0
#define    PAS_IOB_DMA_TXCH_RESET_PCNT(x)	(((x) << PAS_IOB_DMA_TXCH_RESET_PCNT_S) & \
						 PAS_IOB_DMA_TXCH_RESET_PCNT_M)
#define    PAS_IOB_DMA_TXCH_RESET_PCNTRST	0x00000020
#define    PAS_IOB_DMA_TXCH_RESET_DCNTRST	0x00000010
#define    PAS_IOB_DMA_TXCH_RESET_TINTC		0x00000008
#define    PAS_IOB_DMA_TXCH_RESET_DINTC		0x00000004
#define    PAS_IOB_DMA_TXCH_RESET_SINTC		0x00000002
#define    PAS_IOB_DMA_TXCH_RESET_PINTC		0x00000001

#define PAS_IOB_DMA_COM_TIMEOUTCFG		0x1700
#define    PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT_M	0x00ffffff
#define    PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT_S	0
#define    PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT(x)	(((x) << PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT_S) & \
						 PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT_M)

/* Transmit descriptor fields */
#define	XCT_MACTX_T		0x8000000000000000ull
#define	XCT_MACTX_ST		0x4000000000000000ull
#define XCT_MACTX_NORES		0x0000000000000000ull
#define XCT_MACTX_8BRES		0x1000000000000000ull
#define XCT_MACTX_24BRES	0x2000000000000000ull
#define XCT_MACTX_40BRES	0x3000000000000000ull
#define XCT_MACTX_I		0x0800000000000000ull
#define XCT_MACTX_O		0x0400000000000000ull
#define XCT_MACTX_E		0x0200000000000000ull
#define XCT_MACTX_VLAN_M	0x0180000000000000ull
#define XCT_MACTX_VLAN_NOP	0x0000000000000000ull
#define XCT_MACTX_VLAN_REMOVE	0x0080000000000000ull
#define XCT_MACTX_VLAN_INSERT   0x0100000000000000ull
#define XCT_MACTX_VLAN_REPLACE  0x0180000000000000ull
#define XCT_MACTX_CRC_M		0x0060000000000000ull
#define XCT_MACTX_CRC_NOP	0x0000000000000000ull
#define XCT_MACTX_CRC_INSERT	0x0020000000000000ull
#define XCT_MACTX_CRC_PAD	0x0040000000000000ull
#define XCT_MACTX_CRC_REPLACE	0x0060000000000000ull
#define XCT_MACTX_SS		0x0010000000000000ull
#define XCT_MACTX_LLEN_M	0x00007fff00000000ull
#define XCT_MACTX_LLEN_S	32ull
#define XCT_MACTX_LLEN(x)	((((long)(x)) << XCT_MACTX_LLEN_S) & \
				 XCT_MACTX_LLEN_M)
#define XCT_MACTX_IPH_M		0x00000000f8000000ull
#define XCT_MACTX_IPH_S		27ull
#define XCT_MACTX_IPH(x)	((((long)(x)) << XCT_MACTX_IPH_S) & \
				 XCT_MACTX_IPH_M)
#define XCT_MACTX_IPO_M		0x0000000007c00000ull
#define XCT_MACTX_IPO_S		22ull
#define XCT_MACTX_IPO(x)	((((long)(x)) << XCT_MACTX_IPO_S) & \
				 XCT_MACTX_IPO_M)
#define XCT_MACTX_CSUM_M	0x0000000000000060ull
#define XCT_MACTX_CSUM_NOP	0x0000000000000000ull
#define XCT_MACTX_CSUM_TCP	0x0000000000000040ull
#define XCT_MACTX_CSUM_UDP	0x0000000000000060ull
#define XCT_MACTX_V6		0x0000000000000010ull
#define XCT_MACTX_C		0x0000000000000004ull
#define XCT_MACTX_AL2		0x0000000000000002ull

/* Receive descriptor fields */
#define	XCT_MACRX_T		0x8000000000000000ull
#define	XCT_MACRX_ST		0x4000000000000000ull
#define XCT_MACRX_NORES		0x0000000000000000ull
#define XCT_MACRX_8BRES		0x1000000000000000ull
#define XCT_MACRX_24BRES	0x2000000000000000ull
#define XCT_MACRX_40BRES	0x3000000000000000ull
#define XCT_MACRX_O		0x0400000000000000ull
#define XCT_MACRX_E		0x0200000000000000ull
#define XCT_MACRX_FF		0x0100000000000000ull
#define XCT_MACRX_PF		0x0080000000000000ull
#define XCT_MACRX_OB		0x0040000000000000ull
#define XCT_MACRX_OD		0x0020000000000000ull
#define XCT_MACRX_FS		0x0010000000000000ull
#define XCT_MACRX_NB_M		0x000fc00000000000ull
#define XCT_MACRX_NB_S		46ULL
#define XCT_MACRX_NB(x)		((((long)(x)) << XCT_MACRX_NB_S) & \
				 XCT_MACRX_NB_M)
#define XCT_MACRX_LLEN_M	0x00003fff00000000ull
#define XCT_MACRX_LLEN_S	32ULL
#define XCT_MACRX_LLEN(x)	((((long)(x)) << XCT_MACRX_LLEN_S) & \
				 XCT_MACRX_LLEN_M)
#define XCT_MACRX_CRC		0x0000000080000000ull
#define XCT_MACRX_LEN_M		0x0000000060000000ull
#define XCT_MACRX_LEN_TOOSHORT	0x0000000020000000ull
#define XCT_MACRX_LEN_BELOWMIN	0x0000000040000000ull
#define XCT_MACRX_LEN_TRUNC	0x0000000060000000ull
#define XCT_MACRX_CAST_M	0x0000000018000000ull
#define XCT_MACRX_CAST_UNI	0x0000000000000000ull
#define XCT_MACRX_CAST_MULTI	0x0000000008000000ull
#define XCT_MACRX_CAST_BROAD	0x0000000010000000ull
#define XCT_MACRX_CAST_PAUSE	0x0000000018000000ull
#define XCT_MACRX_VLC_M		0x0000000006000000ull
#define XCT_MACRX_FM		0x0000000001000000ull
#define XCT_MACRX_HTY_M		0x0000000000c00000ull
#define XCT_MACRX_HTY_IPV4_OK	0x0000000000000000ull
#define XCT_MACRX_HTY_IPV6 	0x0000000000400000ull
#define XCT_MACRX_HTY_IPV4_BAD	0x0000000000800000ull
#define XCT_MACRX_HTY_NONIP	0x0000000000c00000ull
#define XCT_MACRX_IPP_M		0x00000000003f0000ull
#define XCT_MACRX_IPP_S		16
#define XCT_MACRX_CSUM_M	0x000000000000ffffull
#define XCT_MACRX_CSUM_S	0

#define XCT_PTR_T		0x8000000000000000ull
#define XCT_PTR_LEN_M		0x7ffff00000000000ull
#define XCT_PTR_LEN_S		44
#define XCT_PTR_LEN(x)		((((long)(x)) << XCT_PTR_LEN_S) & \
				 XCT_PTR_LEN_M)
#define XCT_PTR_ADDR_M		0x00000fffffffffffull
#define XCT_PTR_ADDR_S		0
#define XCT_PTR_ADDR(x)		((((long)(x)) << XCT_PTR_ADDR_S) & \
				 XCT_PTR_ADDR_M)

/* Receive interface buffer fields */
#define XCT_RXB_LEN_M		0x0ffff00000000000ull
#define XCT_RXB_LEN_S		44
#define XCT_RXB_LEN(x)		((((long)(x)) << XCT_PTR_LEN_S) & XCT_PTR_LEN_M)
#define XCT_RXB_ADDR_M		0x00000fffffffffffull
#define XCT_RXB_ADDR_S		0
#define XCT_RXB_ADDR(x)		((((long)(x)) << XCT_PTR_ADDR_S) & XCT_PTR_ADDR_M)


#endif /* PASEMI_MAC_H */
