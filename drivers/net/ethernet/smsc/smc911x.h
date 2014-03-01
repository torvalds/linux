/*------------------------------------------------------------------------
 . smc911x.h - macros for SMSC's LAN911{5,6,7,8} single-chip Ethernet device.
 .
 . Copyright (C) 2005 Sensoria Corp.
 . Derived from the unified SMC91x driver by Nicolas Pitre
 .
 . This program is free software; you can redistribute it and/or modify
 . it under the terms of the GNU General Public License as published by
 . the Free Software Foundation; either version 2 of the License, or
 . (at your option) any later version.
 .
 . This program is distributed in the hope that it will be useful,
 . but WITHOUT ANY WARRANTY; without even the implied warranty of
 . MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 . GNU General Public License for more details.
 .
 . You should have received a copy of the GNU General Public License
 . along with this program; if not, see <http://www.gnu.org/licenses/>.
 .
 . Information contained in this file was obtained from the LAN9118
 . manual from SMC.  To get a copy, if you really want one, you can find
 . information under www.smsc.com.
 .
 . Authors
 .	 Dustin McIntire		 <dustin@sensoria.com>
 .
 ---------------------------------------------------------------------------*/
#ifndef _SMC911X_H_
#define _SMC911X_H_

#include <linux/smc911x.h>
/*
 * Use the DMA feature on PXA chips
 */
#ifdef CONFIG_ARCH_PXA
  #define SMC_USE_PXA_DMA	1
  #define SMC_USE_16BIT		0
  #define SMC_USE_32BIT		1
  #define SMC_IRQ_SENSE		IRQF_TRIGGER_FALLING
#elif defined(CONFIG_SH_MAGIC_PANEL_R2)
  #define SMC_USE_16BIT		0
  #define SMC_USE_32BIT		1
  #define SMC_IRQ_SENSE		IRQF_TRIGGER_LOW
#elif defined(CONFIG_ARCH_OMAP3)
  #define SMC_USE_16BIT		0
  #define SMC_USE_32BIT		1
  #define SMC_IRQ_SENSE		IRQF_TRIGGER_LOW
  #define SMC_MEM_RESERVED	1
#elif defined(CONFIG_ARCH_OMAP2)
  #define SMC_USE_16BIT		0
  #define SMC_USE_32BIT		1
  #define SMC_IRQ_SENSE		IRQF_TRIGGER_LOW
  #define SMC_MEM_RESERVED	1
#else
/*
 * Default configuration
 */

#define SMC_DYNAMIC_BUS_CONFIG
#endif

#ifdef SMC_USE_PXA_DMA
#define SMC_USE_DMA
#endif

/* store this information for the driver.. */
struct smc911x_local {
	/*
	 * If I have to wait until the DMA is finished and ready to reload a
	 * packet, I will store the skbuff here. Then, the DMA will send it
	 * out and free it.
	 */
	struct sk_buff *pending_tx_skb;

	/* version/revision of the SMC911x chip */
	u16 version;
	u16 revision;

	/* FIFO sizes */
	int tx_fifo_kb;
	int tx_fifo_size;
	int rx_fifo_size;
	int afc_cfg;

	/* Contains the current active receive/phy mode */
	int ctl_rfduplx;
	int ctl_rspeed;

	u32 msg_enable;
	u32 phy_type;
	struct mii_if_info mii;

	/* work queue */
	struct work_struct phy_configure;

	int tx_throttle;
	spinlock_t lock;

	struct net_device *netdev;

#ifdef SMC_USE_DMA
	/* DMA needs the physical address of the chip */
	u_long physaddr;
	int rxdma;
	int txdma;
	int rxdma_active;
	int txdma_active;
	struct sk_buff *current_rx_skb;
	struct sk_buff *current_tx_skb;
	struct device *dev;
#endif
	void __iomem *base;
#ifdef SMC_DYNAMIC_BUS_CONFIG
	struct smc911x_platdata cfg;
#endif
};

/*
 * Define the bus width specific IO macros
 */

#ifdef SMC_DYNAMIC_BUS_CONFIG
static inline unsigned int SMC_inl(struct smc911x_local *lp, int reg)
{
	void __iomem *ioaddr = lp->base + reg;

	if (lp->cfg.flags & SMC911X_USE_32BIT)
		return readl(ioaddr);

	if (lp->cfg.flags & SMC911X_USE_16BIT)
		return readw(ioaddr) | (readw(ioaddr + 2) << 16);

	BUG();
}

static inline void SMC_outl(unsigned int value, struct smc911x_local *lp,
			    int reg)
{
	void __iomem *ioaddr = lp->base + reg;

	if (lp->cfg.flags & SMC911X_USE_32BIT) {
		writel(value, ioaddr);
		return;
	}

	if (lp->cfg.flags & SMC911X_USE_16BIT) {
		writew(value & 0xffff, ioaddr);
		writew(value >> 16, ioaddr + 2);
		return;
	}

	BUG();
}

static inline void SMC_insl(struct smc911x_local *lp, int reg,
			      void *addr, unsigned int count)
{
	void __iomem *ioaddr = lp->base + reg;

	if (lp->cfg.flags & SMC911X_USE_32BIT) {
		ioread32_rep(ioaddr, addr, count);
		return;
	}

	if (lp->cfg.flags & SMC911X_USE_16BIT) {
		ioread16_rep(ioaddr, addr, count * 2);
		return;
	}

	BUG();
}

static inline void SMC_outsl(struct smc911x_local *lp, int reg,
			     void *addr, unsigned int count)
{
	void __iomem *ioaddr = lp->base + reg;

	if (lp->cfg.flags & SMC911X_USE_32BIT) {
		iowrite32_rep(ioaddr, addr, count);
		return;
	}

	if (lp->cfg.flags & SMC911X_USE_16BIT) {
		iowrite16_rep(ioaddr, addr, count * 2);
		return;
	}

	BUG();
}
#else
#if	SMC_USE_16BIT
#define SMC_inl(lp, r)		 ((readw((lp)->base + (r)) & 0xFFFF) + (readw((lp)->base + (r) + 2) << 16))
#define SMC_outl(v, lp, r) 			 \
	do{					 \
		 writew(v & 0xFFFF, (lp)->base + (r));	 \
		 writew(v >> 16, (lp)->base + (r) + 2); \
	 } while (0)
#define SMC_insl(lp, r, p, l)	 ioread16_rep((short*)((lp)->base + (r)), p, l*2)
#define SMC_outsl(lp, r, p, l)	 iowrite16_rep((short*)((lp)->base + (r)), p, l*2)

#elif	SMC_USE_32BIT
#define SMC_inl(lp, r)		 readl((lp)->base + (r))
#define SMC_outl(v, lp, r)	 writel(v, (lp)->base + (r))
#define SMC_insl(lp, r, p, l)	 ioread32_rep((int*)((lp)->base + (r)), p, l)
#define SMC_outsl(lp, r, p, l)	 iowrite32_rep((int*)((lp)->base + (r)), p, l)

#endif /* SMC_USE_16BIT */
#endif /* SMC_DYNAMIC_BUS_CONFIG */


#ifdef SMC_USE_PXA_DMA

#include <mach/dma.h>

/*
 * Define the request and free functions
 * These are unfortunately architecture specific as no generic allocation
 * mechanism exits
 */
#define SMC_DMA_REQUEST(dev, handler) \
	 pxa_request_dma(dev->name, DMA_PRIO_LOW, handler, dev)

#define SMC_DMA_FREE(dev, dma) \
	 pxa_free_dma(dma)

#define SMC_DMA_ACK_IRQ(dev, dma)					\
{									\
	if (DCSR(dma) & DCSR_BUSERR) {					\
		netdev_err(dev, "DMA %d bus error!\n", dma);		\
	}								\
	DCSR(dma) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;		\
}

/*
 * Use a DMA for RX and TX packets.
 */
#include <linux/dma-mapping.h>

static dma_addr_t rx_dmabuf, tx_dmabuf;
static int rx_dmalen, tx_dmalen;

#ifdef SMC_insl
#undef SMC_insl
#define SMC_insl(lp, r, p, l) \
	smc_pxa_dma_insl(lp, lp->physaddr, r, lp->rxdma, p, l)

static inline void
smc_pxa_dma_insl(struct smc911x_local *lp, u_long physaddr,
		int reg, int dma, u_char *buf, int len)
{
	/* 64 bit alignment is required for memory to memory DMA */
	if ((long)buf & 4) {
		*((u32 *)buf) = SMC_inl(lp, reg);
		buf += 4;
		len--;
	}

	len *= 4;
	rx_dmabuf = dma_map_single(lp->dev, buf, len, DMA_FROM_DEVICE);
	rx_dmalen = len;
	DCSR(dma) = DCSR_NODESC;
	DTADR(dma) = rx_dmabuf;
	DSADR(dma) = physaddr + reg;
	DCMD(dma) = (DCMD_INCTRGADDR | DCMD_BURST32 |
		DCMD_WIDTH4 | DCMD_ENDIRQEN | (DCMD_LENGTH & rx_dmalen));
	DCSR(dma) = DCSR_NODESC | DCSR_RUN;
}
#endif

#ifdef SMC_outsl
#undef SMC_outsl
#define SMC_outsl(lp, r, p, l) \
	 smc_pxa_dma_outsl(lp, lp->physaddr, r, lp->txdma, p, l)

static inline void
smc_pxa_dma_outsl(struct smc911x_local *lp, u_long physaddr,
		int reg, int dma, u_char *buf, int len)
{
	/* 64 bit alignment is required for memory to memory DMA */
	if ((long)buf & 4) {
		SMC_outl(*((u32 *)buf), lp, reg);
		buf += 4;
		len--;
	}

	len *= 4;
	tx_dmabuf = dma_map_single(lp->dev, buf, len, DMA_TO_DEVICE);
	tx_dmalen = len;
	DCSR(dma) = DCSR_NODESC;
	DSADR(dma) = tx_dmabuf;
	DTADR(dma) = physaddr + reg;
	DCMD(dma) = (DCMD_INCSRCADDR | DCMD_BURST32 |
		DCMD_WIDTH4 | DCMD_ENDIRQEN | (DCMD_LENGTH & tx_dmalen));
	DCSR(dma) = DCSR_NODESC | DCSR_RUN;
}
#endif
#endif	 /* SMC_USE_PXA_DMA */


/* Chip Parameters and Register Definitions */

#define SMC911X_TX_FIFO_LOW_THRESHOLD	(1536*2)

#define SMC911X_IO_EXTENT	 0x100

#define SMC911X_EEPROM_LEN	 7

/* Below are the register offsets and bit definitions
 * of the Lan911x memory space
 */
#define RX_DATA_FIFO		 (0x00)

#define TX_DATA_FIFO		 (0x20)
#define	TX_CMD_A_INT_ON_COMP_		(0x80000000)
#define	TX_CMD_A_INT_BUF_END_ALGN_	(0x03000000)
#define	TX_CMD_A_INT_4_BYTE_ALGN_	(0x00000000)
#define	TX_CMD_A_INT_16_BYTE_ALGN_	(0x01000000)
#define	TX_CMD_A_INT_32_BYTE_ALGN_	(0x02000000)
#define	TX_CMD_A_INT_DATA_OFFSET_	(0x001F0000)
#define	TX_CMD_A_INT_FIRST_SEG_		(0x00002000)
#define	TX_CMD_A_INT_LAST_SEG_		(0x00001000)
#define	TX_CMD_A_BUF_SIZE_		(0x000007FF)
#define	TX_CMD_B_PKT_TAG_		(0xFFFF0000)
#define	TX_CMD_B_ADD_CRC_DISABLE_	(0x00002000)
#define	TX_CMD_B_DISABLE_PADDING_	(0x00001000)
#define	TX_CMD_B_PKT_BYTE_LENGTH_	(0x000007FF)

#define RX_STATUS_FIFO		(0x40)
#define	RX_STS_PKT_LEN_			(0x3FFF0000)
#define	RX_STS_ES_			(0x00008000)
#define	RX_STS_BCST_			(0x00002000)
#define	RX_STS_LEN_ERR_			(0x00001000)
#define	RX_STS_RUNT_ERR_		(0x00000800)
#define	RX_STS_MCAST_			(0x00000400)
#define	RX_STS_TOO_LONG_		(0x00000080)
#define	RX_STS_COLL_			(0x00000040)
#define	RX_STS_ETH_TYPE_		(0x00000020)
#define	RX_STS_WDOG_TMT_		(0x00000010)
#define	RX_STS_MII_ERR_			(0x00000008)
#define	RX_STS_DRIBBLING_		(0x00000004)
#define	RX_STS_CRC_ERR_			(0x00000002)
#define RX_STATUS_FIFO_PEEK 	(0x44)
#define TX_STATUS_FIFO		(0x48)
#define	TX_STS_TAG_			(0xFFFF0000)
#define	TX_STS_ES_			(0x00008000)
#define	TX_STS_LOC_			(0x00000800)
#define	TX_STS_NO_CARR_			(0x00000400)
#define	TX_STS_LATE_COLL_		(0x00000200)
#define	TX_STS_MANY_COLL_		(0x00000100)
#define	TX_STS_COLL_CNT_		(0x00000078)
#define	TX_STS_MANY_DEFER_		(0x00000004)
#define	TX_STS_UNDERRUN_		(0x00000002)
#define	TX_STS_DEFERRED_		(0x00000001)
#define TX_STATUS_FIFO_PEEK	(0x4C)
#define ID_REV			(0x50)
#define	ID_REV_CHIP_ID_			(0xFFFF0000)  /* RO */
#define	ID_REV_REV_ID_			(0x0000FFFF)  /* RO */

#define INT_CFG			(0x54)
#define	INT_CFG_INT_DEAS_		(0xFF000000)  /* R/W */
#define	INT_CFG_INT_DEAS_CLR_		(0x00004000)
#define	INT_CFG_INT_DEAS_STS_		(0x00002000)
#define	INT_CFG_IRQ_INT_		(0x00001000)  /* RO */
#define	INT_CFG_IRQ_EN_			(0x00000100)  /* R/W */
#define	INT_CFG_IRQ_POL_		(0x00000010)  /* R/W Not Affected by SW Reset */
#define	INT_CFG_IRQ_TYPE_		(0x00000001)  /* R/W Not Affected by SW Reset */

#define INT_STS			(0x58)
#define	INT_STS_SW_INT_			(0x80000000)  /* R/WC */
#define	INT_STS_TXSTOP_INT_		(0x02000000)  /* R/WC */
#define	INT_STS_RXSTOP_INT_		(0x01000000)  /* R/WC */
#define	INT_STS_RXDFH_INT_		(0x00800000)  /* R/WC */
#define	INT_STS_RXDF_INT_		(0x00400000)  /* R/WC */
#define	INT_STS_TX_IOC_			(0x00200000)  /* R/WC */
#define	INT_STS_RXD_INT_		(0x00100000)  /* R/WC */
#define	INT_STS_GPT_INT_		(0x00080000)  /* R/WC */
#define	INT_STS_PHY_INT_		(0x00040000)  /* RO */
#define	INT_STS_PME_INT_		(0x00020000)  /* R/WC */
#define	INT_STS_TXSO_			(0x00010000)  /* R/WC */
#define	INT_STS_RWT_			(0x00008000)  /* R/WC */
#define	INT_STS_RXE_			(0x00004000)  /* R/WC */
#define	INT_STS_TXE_			(0x00002000)  /* R/WC */
//#define	INT_STS_ERX_		(0x00001000)  /* R/WC */
#define	INT_STS_TDFU_			(0x00000800)  /* R/WC */
#define	INT_STS_TDFO_			(0x00000400)  /* R/WC */
#define	INT_STS_TDFA_			(0x00000200)  /* R/WC */
#define	INT_STS_TSFF_			(0x00000100)  /* R/WC */
#define	INT_STS_TSFL_			(0x00000080)  /* R/WC */
//#define	INT_STS_RXDF_		(0x00000040)  /* R/WC */
#define	INT_STS_RDFO_			(0x00000040)  /* R/WC */
#define	INT_STS_RDFL_			(0x00000020)  /* R/WC */
#define	INT_STS_RSFF_			(0x00000010)  /* R/WC */
#define	INT_STS_RSFL_			(0x00000008)  /* R/WC */
#define	INT_STS_GPIO2_INT_		(0x00000004)  /* R/WC */
#define	INT_STS_GPIO1_INT_		(0x00000002)  /* R/WC */
#define	INT_STS_GPIO0_INT_		(0x00000001)  /* R/WC */

#define INT_EN			(0x5C)
#define	INT_EN_SW_INT_EN_		(0x80000000)  /* R/W */
#define	INT_EN_TXSTOP_INT_EN_		(0x02000000)  /* R/W */
#define	INT_EN_RXSTOP_INT_EN_		(0x01000000)  /* R/W */
#define	INT_EN_RXDFH_INT_EN_		(0x00800000)  /* R/W */
//#define	INT_EN_RXDF_INT_EN_		(0x00400000)  /* R/W */
#define	INT_EN_TIOC_INT_EN_		(0x00200000)  /* R/W */
#define	INT_EN_RXD_INT_EN_		(0x00100000)  /* R/W */
#define	INT_EN_GPT_INT_EN_		(0x00080000)  /* R/W */
#define	INT_EN_PHY_INT_EN_		(0x00040000)  /* R/W */
#define	INT_EN_PME_INT_EN_		(0x00020000)  /* R/W */
#define	INT_EN_TXSO_EN_			(0x00010000)  /* R/W */
#define	INT_EN_RWT_EN_			(0x00008000)  /* R/W */
#define	INT_EN_RXE_EN_			(0x00004000)  /* R/W */
#define	INT_EN_TXE_EN_			(0x00002000)  /* R/W */
//#define	INT_EN_ERX_EN_			(0x00001000)  /* R/W */
#define	INT_EN_TDFU_EN_			(0x00000800)  /* R/W */
#define	INT_EN_TDFO_EN_			(0x00000400)  /* R/W */
#define	INT_EN_TDFA_EN_			(0x00000200)  /* R/W */
#define	INT_EN_TSFF_EN_			(0x00000100)  /* R/W */
#define	INT_EN_TSFL_EN_			(0x00000080)  /* R/W */
//#define	INT_EN_RXDF_EN_			(0x00000040)  /* R/W */
#define	INT_EN_RDFO_EN_			(0x00000040)  /* R/W */
#define	INT_EN_RDFL_EN_			(0x00000020)  /* R/W */
#define	INT_EN_RSFF_EN_			(0x00000010)  /* R/W */
#define	INT_EN_RSFL_EN_			(0x00000008)  /* R/W */
#define	INT_EN_GPIO2_INT_		(0x00000004)  /* R/W */
#define	INT_EN_GPIO1_INT_		(0x00000002)  /* R/W */
#define	INT_EN_GPIO0_INT_		(0x00000001)  /* R/W */

#define BYTE_TEST		(0x64)
#define FIFO_INT		(0x68)
#define	FIFO_INT_TX_AVAIL_LEVEL_	(0xFF000000)  /* R/W */
#define	FIFO_INT_TX_STS_LEVEL_		(0x00FF0000)  /* R/W */
#define	FIFO_INT_RX_AVAIL_LEVEL_	(0x0000FF00)  /* R/W */
#define	FIFO_INT_RX_STS_LEVEL_		(0x000000FF)  /* R/W */

#define RX_CFG			(0x6C)
#define	RX_CFG_RX_END_ALGN_		(0xC0000000)  /* R/W */
#define		RX_CFG_RX_END_ALGN4_		(0x00000000)  /* R/W */
#define		RX_CFG_RX_END_ALGN16_		(0x40000000)  /* R/W */
#define		RX_CFG_RX_END_ALGN32_		(0x80000000)  /* R/W */
#define	RX_CFG_RX_DMA_CNT_		(0x0FFF0000)  /* R/W */
#define	RX_CFG_RX_DUMP_			(0x00008000)  /* R/W */
#define	RX_CFG_RXDOFF_			(0x00001F00)  /* R/W */
//#define	RX_CFG_RXBAD_			(0x00000001)  /* R/W */

#define TX_CFG			(0x70)
//#define	TX_CFG_TX_DMA_LVL_		(0xE0000000)	 /* R/W */
//#define	TX_CFG_TX_DMA_CNT_		(0x0FFF0000)	 /* R/W Self Clearing */
#define	TX_CFG_TXS_DUMP_		(0x00008000)  /* Self Clearing */
#define	TX_CFG_TXD_DUMP_		(0x00004000)  /* Self Clearing */
#define	TX_CFG_TXSAO_			(0x00000004)  /* R/W */
#define	TX_CFG_TX_ON_			(0x00000002)  /* R/W */
#define	TX_CFG_STOP_TX_			(0x00000001)  /* Self Clearing */

#define HW_CFG			(0x74)
#define	HW_CFG_TTM_			(0x00200000)  /* R/W */
#define	HW_CFG_SF_			(0x00100000)  /* R/W */
#define	HW_CFG_TX_FIF_SZ_		(0x000F0000)  /* R/W */
#define	HW_CFG_TR_			(0x00003000)  /* R/W */
#define	HW_CFG_PHY_CLK_SEL_		(0x00000060)  /* R/W */
#define		 HW_CFG_PHY_CLK_SEL_INT_PHY_ 	(0x00000000) /* R/W */
#define		 HW_CFG_PHY_CLK_SEL_EXT_PHY_ 	(0x00000020) /* R/W */
#define		 HW_CFG_PHY_CLK_SEL_CLK_DIS_ 	(0x00000040) /* R/W */
#define	HW_CFG_SMI_SEL_			(0x00000010)  /* R/W */
#define	HW_CFG_EXT_PHY_DET_		(0x00000008)  /* RO */
#define	HW_CFG_EXT_PHY_EN_		(0x00000004)  /* R/W */
#define	HW_CFG_32_16_BIT_MODE_		(0x00000004)  /* RO */
#define	HW_CFG_SRST_TO_			(0x00000002)  /* RO */
#define	HW_CFG_SRST_			(0x00000001)  /* Self Clearing */

#define RX_DP_CTRL		(0x78)
#define	RX_DP_CTRL_RX_FFWD_		(0x80000000)  /* R/W */
#define	RX_DP_CTRL_FFWD_BUSY_		(0x80000000)  /* RO */

#define RX_FIFO_INF		(0x7C)
#define	 RX_FIFO_INF_RXSUSED_		(0x00FF0000)  /* RO */
#define	 RX_FIFO_INF_RXDUSED_		(0x0000FFFF)  /* RO */

#define TX_FIFO_INF		(0x80)
#define	TX_FIFO_INF_TSUSED_		(0x00FF0000)  /* RO */
#define	TX_FIFO_INF_TDFREE_		(0x0000FFFF)  /* RO */

#define PMT_CTRL		(0x84)
#define	PMT_CTRL_PM_MODE_		(0x00003000)  /* Self Clearing */
#define	PMT_CTRL_PHY_RST_		(0x00000400)  /* Self Clearing */
#define	PMT_CTRL_WOL_EN_		(0x00000200)  /* R/W */
#define	PMT_CTRL_ED_EN_			(0x00000100)  /* R/W */
#define	PMT_CTRL_PME_TYPE_		(0x00000040)  /* R/W Not Affected by SW Reset */
#define	PMT_CTRL_WUPS_			(0x00000030)  /* R/WC */
#define		PMT_CTRL_WUPS_NOWAKE_		(0x00000000)  /* R/WC */
#define		PMT_CTRL_WUPS_ED_		(0x00000010)  /* R/WC */
#define		PMT_CTRL_WUPS_WOL_		(0x00000020)  /* R/WC */
#define		PMT_CTRL_WUPS_MULTI_		(0x00000030)  /* R/WC */
#define	PMT_CTRL_PME_IND_		(0x00000008)  /* R/W */
#define	PMT_CTRL_PME_POL_		(0x00000004)  /* R/W */
#define	PMT_CTRL_PME_EN_		(0x00000002)  /* R/W Not Affected by SW Reset */
#define	PMT_CTRL_READY_			(0x00000001)  /* RO */

#define GPIO_CFG		(0x88)
#define	GPIO_CFG_LED3_EN_		(0x40000000)  /* R/W */
#define	GPIO_CFG_LED2_EN_		(0x20000000)  /* R/W */
#define	GPIO_CFG_LED1_EN_		(0x10000000)  /* R/W */
#define	GPIO_CFG_GPIO2_INT_POL_		(0x04000000)  /* R/W */
#define	GPIO_CFG_GPIO1_INT_POL_		(0x02000000)  /* R/W */
#define	GPIO_CFG_GPIO0_INT_POL_		(0x01000000)  /* R/W */
#define	GPIO_CFG_EEPR_EN_		(0x00700000)  /* R/W */
#define	GPIO_CFG_GPIOBUF2_		(0x00040000)  /* R/W */
#define	GPIO_CFG_GPIOBUF1_		(0x00020000)  /* R/W */
#define	GPIO_CFG_GPIOBUF0_		(0x00010000)  /* R/W */
#define	GPIO_CFG_GPIODIR2_		(0x00000400)  /* R/W */
#define	GPIO_CFG_GPIODIR1_		(0x00000200)  /* R/W */
#define	GPIO_CFG_GPIODIR0_		(0x00000100)  /* R/W */
#define	GPIO_CFG_GPIOD4_		(0x00000010)  /* R/W */
#define	GPIO_CFG_GPIOD3_		(0x00000008)  /* R/W */
#define	GPIO_CFG_GPIOD2_		(0x00000004)  /* R/W */
#define	GPIO_CFG_GPIOD1_		(0x00000002)  /* R/W */
#define	GPIO_CFG_GPIOD0_		(0x00000001)  /* R/W */

#define GPT_CFG			(0x8C)
#define	GPT_CFG_TIMER_EN_		(0x20000000)  /* R/W */
#define	GPT_CFG_GPT_LOAD_		(0x0000FFFF)  /* R/W */

#define GPT_CNT			(0x90)
#define	GPT_CNT_GPT_CNT_		(0x0000FFFF)  /* RO */

#define ENDIAN			(0x98)
#define FREE_RUN		(0x9C)
#define RX_DROP			(0xA0)
#define MAC_CSR_CMD		(0xA4)
#define	 MAC_CSR_CMD_CSR_BUSY_		(0x80000000)  /* Self Clearing */
#define	 MAC_CSR_CMD_R_NOT_W_		(0x40000000)  /* R/W */
#define	 MAC_CSR_CMD_CSR_ADDR_		(0x000000FF)  /* R/W */

#define MAC_CSR_DATA		(0xA8)
#define AFC_CFG			(0xAC)
#define		AFC_CFG_AFC_HI_			(0x00FF0000)  /* R/W */
#define		AFC_CFG_AFC_LO_			(0x0000FF00)  /* R/W */
#define		AFC_CFG_BACK_DUR_		(0x000000F0)  /* R/W */
#define		AFC_CFG_FCMULT_			(0x00000008)  /* R/W */
#define		AFC_CFG_FCBRD_			(0x00000004)  /* R/W */
#define		AFC_CFG_FCADD_			(0x00000002)  /* R/W */
#define		AFC_CFG_FCANY_			(0x00000001)  /* R/W */

#define E2P_CMD			(0xB0)
#define	E2P_CMD_EPC_BUSY_		(0x80000000)  /* Self Clearing */
#define	E2P_CMD_EPC_CMD_			(0x70000000)  /* R/W */
#define		E2P_CMD_EPC_CMD_READ_		(0x00000000)  /* R/W */
#define		E2P_CMD_EPC_CMD_EWDS_		(0x10000000)  /* R/W */
#define		E2P_CMD_EPC_CMD_EWEN_		(0x20000000)  /* R/W */
#define		E2P_CMD_EPC_CMD_WRITE_		(0x30000000)  /* R/W */
#define		E2P_CMD_EPC_CMD_WRAL_		(0x40000000)  /* R/W */
#define		E2P_CMD_EPC_CMD_ERASE_		(0x50000000)  /* R/W */
#define		E2P_CMD_EPC_CMD_ERAL_		(0x60000000)  /* R/W */
#define		E2P_CMD_EPC_CMD_RELOAD_		(0x70000000)  /* R/W */
#define	E2P_CMD_EPC_TIMEOUT_		(0x00000200)  /* RO */
#define	E2P_CMD_MAC_ADDR_LOADED_	(0x00000100)  /* RO */
#define	E2P_CMD_EPC_ADDR_		(0x000000FF)  /* R/W */

#define E2P_DATA		(0xB4)
#define	E2P_DATA_EEPROM_DATA_		(0x000000FF)  /* R/W */
/* end of LAN register offsets and bit definitions */

/*
 ****************************************************************************
 ****************************************************************************
 * MAC Control and Status Register (Indirect Address)
 * Offset (through the MAC_CSR CMD and DATA port)
 ****************************************************************************
 ****************************************************************************
 *
 */
#define MAC_CR			(0x01)  /* R/W */

/* MAC_CR - MAC Control Register */
#define MAC_CR_RXALL_			(0x80000000)
// TODO: delete this bit? It is not described in the data sheet.
#define MAC_CR_HBDIS_			(0x10000000)
#define MAC_CR_RCVOWN_			(0x00800000)
#define MAC_CR_LOOPBK_			(0x00200000)
#define MAC_CR_FDPX_			(0x00100000)
#define MAC_CR_MCPAS_			(0x00080000)
#define MAC_CR_PRMS_			(0x00040000)
#define MAC_CR_INVFILT_			(0x00020000)
#define MAC_CR_PASSBAD_			(0x00010000)
#define MAC_CR_HFILT_			(0x00008000)
#define MAC_CR_HPFILT_			(0x00002000)
#define MAC_CR_LCOLL_			(0x00001000)
#define MAC_CR_BCAST_			(0x00000800)
#define MAC_CR_DISRTY_			(0x00000400)
#define MAC_CR_PADSTR_			(0x00000100)
#define MAC_CR_BOLMT_MASK_		(0x000000C0)
#define MAC_CR_DFCHK_			(0x00000020)
#define MAC_CR_TXEN_			(0x00000008)
#define MAC_CR_RXEN_			(0x00000004)

#define ADDRH			(0x02)	  /* R/W mask 0x0000FFFFUL */
#define ADDRL			(0x03)	  /* R/W mask 0xFFFFFFFFUL */
#define HASHH			(0x04)	  /* R/W */
#define HASHL			(0x05)	  /* R/W */

#define MII_ACC			(0x06)	  /* R/W */
#define MII_ACC_PHY_ADDR_		(0x0000F800)
#define MII_ACC_MIIRINDA_		(0x000007C0)
#define MII_ACC_MII_WRITE_		(0x00000002)
#define MII_ACC_MII_BUSY_		(0x00000001)

#define MII_DATA		(0x07)	  /* R/W mask 0x0000FFFFUL */

#define FLOW			(0x08)	  /* R/W */
#define FLOW_FCPT_			(0xFFFF0000)
#define FLOW_FCPASS_			(0x00000004)
#define FLOW_FCEN_			(0x00000002)
#define FLOW_FCBSY_			(0x00000001)

#define VLAN1			(0x09)	  /* R/W mask 0x0000FFFFUL */
#define VLAN1_VTI1_			(0x0000ffff)

#define VLAN2			(0x0A)	  /* R/W mask 0x0000FFFFUL */
#define VLAN2_VTI2_			(0x0000ffff)

#define WUFF			(0x0B)	  /* WO */

#define WUCSR			(0x0C)	  /* R/W */
#define WUCSR_GUE_			(0x00000200)
#define WUCSR_WUFR_			(0x00000040)
#define WUCSR_MPR_			(0x00000020)
#define WUCSR_WAKE_EN_			(0x00000004)
#define WUCSR_MPEN_			(0x00000002)

/*
 ****************************************************************************
 * Chip Specific MII Defines
 ****************************************************************************
 *
 * Phy register offsets and bit definitions
 *
 */

#define PHY_MODE_CTRL_STS	((u32)17)	/* Mode Control/Status Register */
//#define MODE_CTRL_STS_FASTRIP_	  ((u16)0x4000)
#define MODE_CTRL_STS_EDPWRDOWN_	 ((u16)0x2000)
//#define MODE_CTRL_STS_LOWSQEN_	   ((u16)0x0800)
//#define MODE_CTRL_STS_MDPREBP_	   ((u16)0x0400)
//#define MODE_CTRL_STS_FARLOOPBACK_  ((u16)0x0200)
//#define MODE_CTRL_STS_FASTEST_	   ((u16)0x0100)
//#define MODE_CTRL_STS_REFCLKEN_	   ((u16)0x0010)
//#define MODE_CTRL_STS_PHYADBP_	   ((u16)0x0008)
//#define MODE_CTRL_STS_FORCE_G_LINK_ ((u16)0x0004)
#define MODE_CTRL_STS_ENERGYON_	 	((u16)0x0002)

#define PHY_INT_SRC			((u32)29)
#define PHY_INT_SRC_ENERGY_ON_			((u16)0x0080)
#define PHY_INT_SRC_ANEG_COMP_			((u16)0x0040)
#define PHY_INT_SRC_REMOTE_FAULT_		((u16)0x0020)
#define PHY_INT_SRC_LINK_DOWN_			((u16)0x0010)
#define PHY_INT_SRC_ANEG_LP_ACK_		((u16)0x0008)
#define PHY_INT_SRC_PAR_DET_FAULT_		((u16)0x0004)
#define PHY_INT_SRC_ANEG_PGRX_			((u16)0x0002)

#define PHY_INT_MASK			((u32)30)
#define PHY_INT_MASK_ENERGY_ON_			((u16)0x0080)
#define PHY_INT_MASK_ANEG_COMP_			((u16)0x0040)
#define PHY_INT_MASK_REMOTE_FAULT_		((u16)0x0020)
#define PHY_INT_MASK_LINK_DOWN_			((u16)0x0010)
#define PHY_INT_MASK_ANEG_LP_ACK_		((u16)0x0008)
#define PHY_INT_MASK_PAR_DET_FAULT_		((u16)0x0004)
#define PHY_INT_MASK_ANEG_PGRX_			((u16)0x0002)

#define PHY_SPECIAL			((u32)31)
#define PHY_SPECIAL_ANEG_DONE_			((u16)0x1000)
#define PHY_SPECIAL_RES_			((u16)0x0040)
#define PHY_SPECIAL_RES_MASK_			((u16)0x0FE1)
#define PHY_SPECIAL_SPD_			((u16)0x001C)
#define PHY_SPECIAL_SPD_10HALF_			((u16)0x0004)
#define PHY_SPECIAL_SPD_10FULL_			((u16)0x0014)
#define PHY_SPECIAL_SPD_100HALF_		((u16)0x0008)
#define PHY_SPECIAL_SPD_100FULL_		((u16)0x0018)

#define LAN911X_INTERNAL_PHY_ID		(0x0007C000)

/* Chip ID values */
#define CHIP_9115	0x0115
#define CHIP_9116	0x0116
#define CHIP_9117	0x0117
#define CHIP_9118	0x0118
#define CHIP_9211	0x9211
#define CHIP_9215	0x115A
#define CHIP_9217	0x117A
#define CHIP_9218	0x118A

struct chip_id {
	u16 id;
	char *name;
};

static const struct chip_id chip_ids[] =  {
	{ CHIP_9115, "LAN9115" },
	{ CHIP_9116, "LAN9116" },
	{ CHIP_9117, "LAN9117" },
	{ CHIP_9118, "LAN9118" },
	{ CHIP_9211, "LAN9211" },
	{ CHIP_9215, "LAN9215" },
	{ CHIP_9217, "LAN9217" },
	{ CHIP_9218, "LAN9218" },
	{ 0, NULL },
};

#define IS_REV_A(x)	((x & 0xFFFF)==0)

/*
 * Macros to abstract register access according to the data bus
 * capabilities.  Please use those and not the in/out primitives.
 */
/* FIFO read/write macros */
#define SMC_PUSH_DATA(lp, p, l)	SMC_outsl( lp, TX_DATA_FIFO, p, (l) >> 2 )
#define SMC_PULL_DATA(lp, p, l)	SMC_insl ( lp, RX_DATA_FIFO, p, (l) >> 2 )
#define SMC_SET_TX_FIFO(lp, x) 	SMC_outl( x, lp, TX_DATA_FIFO )
#define SMC_GET_RX_FIFO(lp)	SMC_inl( lp, RX_DATA_FIFO )


/* I/O mapped register read/write macros */
#define SMC_GET_TX_STS_FIFO(lp)		SMC_inl( lp, TX_STATUS_FIFO )
#define SMC_GET_RX_STS_FIFO(lp)		SMC_inl( lp, RX_STATUS_FIFO )
#define SMC_GET_RX_STS_FIFO_PEEK(lp)	SMC_inl( lp, RX_STATUS_FIFO_PEEK )
#define SMC_GET_PN(lp)			(SMC_inl( lp, ID_REV ) >> 16)
#define SMC_GET_REV(lp)			(SMC_inl( lp, ID_REV ) & 0xFFFF)
#define SMC_GET_IRQ_CFG(lp)		SMC_inl( lp, INT_CFG )
#define SMC_SET_IRQ_CFG(lp, x)		SMC_outl( x, lp, INT_CFG )
#define SMC_GET_INT(lp)			SMC_inl( lp, INT_STS )
#define SMC_ACK_INT(lp, x)			SMC_outl( x, lp, INT_STS )
#define SMC_GET_INT_EN(lp)		SMC_inl( lp, INT_EN )
#define SMC_SET_INT_EN(lp, x)		SMC_outl( x, lp, INT_EN )
#define SMC_GET_BYTE_TEST(lp)		SMC_inl( lp, BYTE_TEST )
#define SMC_SET_BYTE_TEST(lp, x)		SMC_outl( x, lp, BYTE_TEST )
#define SMC_GET_FIFO_INT(lp)		SMC_inl( lp, FIFO_INT )
#define SMC_SET_FIFO_INT(lp, x)		SMC_outl( x, lp, FIFO_INT )
#define SMC_SET_FIFO_TDA(lp, x)					\
	do {							\
		unsigned long __flags;				\
		int __mask;					\
		local_irq_save(__flags);			\
		__mask = SMC_GET_FIFO_INT((lp)) & ~(0xFF<<24);	\
		SMC_SET_FIFO_INT( (lp), __mask | (x)<<24 );	\
		local_irq_restore(__flags);			\
	} while (0)
#define SMC_SET_FIFO_TSL(lp, x)					\
	do {							\
		unsigned long __flags;				\
		int __mask;					\
		local_irq_save(__flags);			\
		__mask = SMC_GET_FIFO_INT((lp)) & ~(0xFF<<16);	\
		SMC_SET_FIFO_INT( (lp), __mask | (((x) & 0xFF)<<16));	\
		local_irq_restore(__flags);			\
	} while (0)
#define SMC_SET_FIFO_RSA(lp, x)					\
	do {							\
		unsigned long __flags;				\
		int __mask;					\
		local_irq_save(__flags);			\
		__mask = SMC_GET_FIFO_INT((lp)) & ~(0xFF<<8);	\
		SMC_SET_FIFO_INT( (lp), __mask | (((x) & 0xFF)<<8));	\
		local_irq_restore(__flags);			\
	} while (0)
#define SMC_SET_FIFO_RSL(lp, x)					\
	do {							\
		unsigned long __flags;				\
		int __mask;					\
		local_irq_save(__flags);			\
		__mask = SMC_GET_FIFO_INT((lp)) & ~0xFF;	\
		SMC_SET_FIFO_INT( (lp),__mask | ((x) & 0xFF));	\
		local_irq_restore(__flags);			\
	} while (0)
#define SMC_GET_RX_CFG(lp)		SMC_inl( lp, RX_CFG )
#define SMC_SET_RX_CFG(lp, x)		SMC_outl( x, lp, RX_CFG )
#define SMC_GET_TX_CFG(lp)		SMC_inl( lp, TX_CFG )
#define SMC_SET_TX_CFG(lp, x)		SMC_outl( x, lp, TX_CFG )
#define SMC_GET_HW_CFG(lp)		SMC_inl( lp, HW_CFG )
#define SMC_SET_HW_CFG(lp, x)		SMC_outl( x, lp, HW_CFG )
#define SMC_GET_RX_DP_CTRL(lp)		SMC_inl( lp, RX_DP_CTRL )
#define SMC_SET_RX_DP_CTRL(lp, x)		SMC_outl( x, lp, RX_DP_CTRL )
#define SMC_GET_PMT_CTRL(lp)		SMC_inl( lp, PMT_CTRL )
#define SMC_SET_PMT_CTRL(lp, x)		SMC_outl( x, lp, PMT_CTRL )
#define SMC_GET_GPIO_CFG(lp)		SMC_inl( lp, GPIO_CFG )
#define SMC_SET_GPIO_CFG(lp, x)		SMC_outl( x, lp, GPIO_CFG )
#define SMC_GET_RX_FIFO_INF(lp)		SMC_inl( lp, RX_FIFO_INF )
#define SMC_SET_RX_FIFO_INF(lp, x)		SMC_outl( x, lp, RX_FIFO_INF )
#define SMC_GET_TX_FIFO_INF(lp)		SMC_inl( lp, TX_FIFO_INF )
#define SMC_SET_TX_FIFO_INF(lp, x)		SMC_outl( x, lp, TX_FIFO_INF )
#define SMC_GET_GPT_CFG(lp)		SMC_inl( lp, GPT_CFG )
#define SMC_SET_GPT_CFG(lp, x)		SMC_outl( x, lp, GPT_CFG )
#define SMC_GET_RX_DROP(lp)		SMC_inl( lp, RX_DROP )
#define SMC_SET_RX_DROP(lp, x)		SMC_outl( x, lp, RX_DROP )
#define SMC_GET_MAC_CMD(lp)		SMC_inl( lp, MAC_CSR_CMD )
#define SMC_SET_MAC_CMD(lp, x)		SMC_outl( x, lp, MAC_CSR_CMD )
#define SMC_GET_MAC_DATA(lp)		SMC_inl( lp, MAC_CSR_DATA )
#define SMC_SET_MAC_DATA(lp, x)		SMC_outl( x, lp, MAC_CSR_DATA )
#define SMC_GET_AFC_CFG(lp)		SMC_inl( lp, AFC_CFG )
#define SMC_SET_AFC_CFG(lp, x)		SMC_outl( x, lp, AFC_CFG )
#define SMC_GET_E2P_CMD(lp)		SMC_inl( lp, E2P_CMD )
#define SMC_SET_E2P_CMD(lp, x)		SMC_outl( x, lp, E2P_CMD )
#define SMC_GET_E2P_DATA(lp)		SMC_inl( lp, E2P_DATA )
#define SMC_SET_E2P_DATA(lp, x)		SMC_outl( x, lp, E2P_DATA )

/* MAC register read/write macros */
#define SMC_GET_MAC_CSR(lp,a,v)						\
	do {								\
		while (SMC_GET_MAC_CMD((lp)) & MAC_CSR_CMD_CSR_BUSY_);	\
		SMC_SET_MAC_CMD((lp),MAC_CSR_CMD_CSR_BUSY_ |		\
			MAC_CSR_CMD_R_NOT_W_ | (a) );			\
		while (SMC_GET_MAC_CMD((lp)) & MAC_CSR_CMD_CSR_BUSY_);	\
		v = SMC_GET_MAC_DATA((lp));			       	\
	} while (0)
#define SMC_SET_MAC_CSR(lp,a,v)						\
	do {								\
		while (SMC_GET_MAC_CMD((lp)) & MAC_CSR_CMD_CSR_BUSY_);	\
		SMC_SET_MAC_DATA((lp), v);				\
		SMC_SET_MAC_CMD((lp), MAC_CSR_CMD_CSR_BUSY_ | (a) );	\
		while (SMC_GET_MAC_CMD((lp)) & MAC_CSR_CMD_CSR_BUSY_);	\
	} while (0)
#define SMC_GET_MAC_CR(lp, x)	SMC_GET_MAC_CSR( (lp), MAC_CR, x )
#define SMC_SET_MAC_CR(lp, x)	SMC_SET_MAC_CSR( (lp), MAC_CR, x )
#define SMC_GET_ADDRH(lp, x)	SMC_GET_MAC_CSR( (lp), ADDRH, x )
#define SMC_SET_ADDRH(lp, x)	SMC_SET_MAC_CSR( (lp), ADDRH, x )
#define SMC_GET_ADDRL(lp, x)	SMC_GET_MAC_CSR( (lp), ADDRL, x )
#define SMC_SET_ADDRL(lp, x)	SMC_SET_MAC_CSR( (lp), ADDRL, x )
#define SMC_GET_HASHH(lp, x)	SMC_GET_MAC_CSR( (lp), HASHH, x )
#define SMC_SET_HASHH(lp, x)	SMC_SET_MAC_CSR( (lp), HASHH, x )
#define SMC_GET_HASHL(lp, x)	SMC_GET_MAC_CSR( (lp), HASHL, x )
#define SMC_SET_HASHL(lp, x)	SMC_SET_MAC_CSR( (lp), HASHL, x )
#define SMC_GET_MII_ACC(lp, x)	SMC_GET_MAC_CSR( (lp), MII_ACC, x )
#define SMC_SET_MII_ACC(lp, x)	SMC_SET_MAC_CSR( (lp), MII_ACC, x )
#define SMC_GET_MII_DATA(lp, x)	SMC_GET_MAC_CSR( (lp), MII_DATA, x )
#define SMC_SET_MII_DATA(lp, x)	SMC_SET_MAC_CSR( (lp), MII_DATA, x )
#define SMC_GET_FLOW(lp, x)		SMC_GET_MAC_CSR( (lp), FLOW, x )
#define SMC_SET_FLOW(lp, x)		SMC_SET_MAC_CSR( (lp), FLOW, x )
#define SMC_GET_VLAN1(lp, x)	SMC_GET_MAC_CSR( (lp), VLAN1, x )
#define SMC_SET_VLAN1(lp, x)	SMC_SET_MAC_CSR( (lp), VLAN1, x )
#define SMC_GET_VLAN2(lp, x)	SMC_GET_MAC_CSR( (lp), VLAN2, x )
#define SMC_SET_VLAN2(lp, x)	SMC_SET_MAC_CSR( (lp), VLAN2, x )
#define SMC_SET_WUFF(lp, x)		SMC_SET_MAC_CSR( (lp), WUFF, x )
#define SMC_GET_WUCSR(lp, x)	SMC_GET_MAC_CSR( (lp), WUCSR, x )
#define SMC_SET_WUCSR(lp, x)	SMC_SET_MAC_CSR( (lp), WUCSR, x )

/* PHY register read/write macros */
#define SMC_GET_MII(lp,a,phy,v)					\
	do {							\
		u32 __v;					\
		do {						\
			SMC_GET_MII_ACC((lp), __v);			\
		} while ( __v & MII_ACC_MII_BUSY_ );		\
		SMC_SET_MII_ACC( (lp), ((phy)<<11) | ((a)<<6) |	\
			MII_ACC_MII_BUSY_);			\
		do {						\
			SMC_GET_MII_ACC( (lp), __v);			\
		} while ( __v & MII_ACC_MII_BUSY_ );		\
		SMC_GET_MII_DATA((lp), v);				\
	} while (0)
#define SMC_SET_MII(lp,a,phy,v)					\
	do {							\
		u32 __v;					\
		do {						\
			SMC_GET_MII_ACC((lp), __v);			\
		} while ( __v & MII_ACC_MII_BUSY_ );		\
		SMC_SET_MII_DATA((lp), v);				\
		SMC_SET_MII_ACC( (lp), ((phy)<<11) | ((a)<<6) |	\
			MII_ACC_MII_BUSY_	 |		\
			MII_ACC_MII_WRITE_  );			\
		do {						\
			SMC_GET_MII_ACC((lp), __v);			\
		} while ( __v & MII_ACC_MII_BUSY_ );		\
	} while (0)
#define SMC_GET_PHY_BMCR(lp,phy,x)		SMC_GET_MII( (lp), MII_BMCR, phy, x )
#define SMC_SET_PHY_BMCR(lp,phy,x)		SMC_SET_MII( (lp), MII_BMCR, phy, x )
#define SMC_GET_PHY_BMSR(lp,phy,x)		SMC_GET_MII( (lp), MII_BMSR, phy, x )
#define SMC_GET_PHY_ID1(lp,phy,x)		SMC_GET_MII( (lp), MII_PHYSID1, phy, x )
#define SMC_GET_PHY_ID2(lp,phy,x)		SMC_GET_MII( (lp), MII_PHYSID2, phy, x )
#define SMC_GET_PHY_MII_ADV(lp,phy,x)	SMC_GET_MII( (lp), MII_ADVERTISE, phy, x )
#define SMC_SET_PHY_MII_ADV(lp,phy,x)	SMC_SET_MII( (lp), MII_ADVERTISE, phy, x )
#define SMC_GET_PHY_MII_LPA(lp,phy,x)	SMC_GET_MII( (lp), MII_LPA, phy, x )
#define SMC_SET_PHY_MII_LPA(lp,phy,x)	SMC_SET_MII( (lp), MII_LPA, phy, x )
#define SMC_GET_PHY_CTRL_STS(lp,phy,x)	SMC_GET_MII( (lp), PHY_MODE_CTRL_STS, phy, x )
#define SMC_SET_PHY_CTRL_STS(lp,phy,x)	SMC_SET_MII( (lp), PHY_MODE_CTRL_STS, phy, x )
#define SMC_GET_PHY_INT_SRC(lp,phy,x)	SMC_GET_MII( (lp), PHY_INT_SRC, phy, x )
#define SMC_SET_PHY_INT_SRC(lp,phy,x)	SMC_SET_MII( (lp), PHY_INT_SRC, phy, x )
#define SMC_GET_PHY_INT_MASK(lp,phy,x)	SMC_GET_MII( (lp), PHY_INT_MASK, phy, x )
#define SMC_SET_PHY_INT_MASK(lp,phy,x)	SMC_SET_MII( (lp), PHY_INT_MASK, phy, x )
#define SMC_GET_PHY_SPECIAL(lp,phy,x)	SMC_GET_MII( (lp), PHY_SPECIAL, phy, x )



/* Misc read/write macros */

#ifndef SMC_GET_MAC_ADDR
#define SMC_GET_MAC_ADDR(lp, addr)				\
	do {							\
		unsigned int __v;				\
								\
		SMC_GET_MAC_CSR((lp), ADDRL, __v);			\
		addr[0] = __v; addr[1] = __v >> 8;		\
		addr[2] = __v >> 16; addr[3] = __v >> 24;	\
		SMC_GET_MAC_CSR((lp), ADDRH, __v);			\
		addr[4] = __v; addr[5] = __v >> 8;		\
	} while (0)
#endif

#define SMC_SET_MAC_ADDR(lp, addr)				\
	do {							\
		 SMC_SET_MAC_CSR((lp), ADDRL,				\
				 addr[0] |			\
				(addr[1] << 8) |		\
				(addr[2] << 16) |		\
				(addr[3] << 24));		\
		 SMC_SET_MAC_CSR((lp), ADDRH, addr[4]|(addr[5] << 8));\
	} while (0)


#define SMC_WRITE_EEPROM_CMD(lp, cmd, addr)				\
	do {								\
		while (SMC_GET_E2P_CMD((lp)) & MAC_CSR_CMD_CSR_BUSY_);	\
		SMC_SET_MAC_CMD((lp), MAC_CSR_CMD_R_NOT_W_ | a );		\
		while (SMC_GET_MAC_CMD((lp)) & MAC_CSR_CMD_CSR_BUSY_);	\
	} while (0)

#endif	 /* _SMC911X_H_ */
