/* drivers/spi/rk29xx_spim.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __DRIVERS_SPIM_RK29XX_HEADER_H
#define __DRIVERS_SPIM_RK29XX_HEADER_H
#include <linux/io.h>
#ifdef CONFIG_ARCH_RK30
#include <plat/dma-pl330.h>
#else
#include <mach/dma-pl330.h>
#endif

/* SPI register offsets */
#define SPIM_CTRLR0				0x0000
#define SPIM_CTRLR1				0x0004
#define SPIM_ENR				0x0008
#define SPIM_SER				0x000c
#define SPIM_BAUDR				0x0010
#define SPIM_TXFTLR				0x0014
#define SPIM_RXFTLR				0x0018
#define SPIM_TXFLR				0x001c
#define SPIM_RXFLR				0x0020
#define SPIM_SR					0x0024
#define SPIM_IPR                0x0028
#define SPIM_IMR				0x002c
#define SPIM_ISR				0x0030
#define SPIM_RISR				0x0034
#define SPIM_ICR				0x0038
#define SPIM_DMACR				0x003c
#define SPIM_DMATDLR			0x0040
#define SPIM_DMARDLR			0x0044
#define SPIM_TXDR				0x0400
#define SPIM_RXDR               0x0800

/* --------Bit fields in CTRLR0--------begin */

#define SPI_DFS_OFFSET			0                  /* Data Frame Size */
#define SPI_DFS_4BIT            0x00
#define SPI_DFS_8BIT            0x01
#define SPI_DFS_16BIT           0x02
#define SPI_DFS_RESV            0x03

#define SPI_FRF_OFFSET			16                 /* Frame Format */
#define SPI_FRF_SPI			    0x00               /* motorola spi */
#define SPI_FRF_SSP			    0x01               /* Texas Instruments SSP*/
#define SPI_FRF_MICROWIRE		0x02               /*  National Semiconductors Microwire */
#define SPI_FRF_RESV			0x03

#define SPI_MODE_OFFSET		    6                 /* SCPH & SCOL */

#define SPI_SCPH_OFFSET			6                  /* Serial Clock Phase */
#define SPI_SCPH_TOGMID         0                  /* Serial clock toggles in middle of first data bit */
#define SPI_SCPH_TOGSTA         1                  /* Serial clock toggles at start of first data bit */

#define SPI_SCOL_OFFSET			7                  /* Serial Clock Polarity */

#define SPI_OPMOD_OFFSET	    20
#define SPI_OPMOD_MASTER        0
#define SPI_OPMOD_SLAVE         1

#define SPI_TMOD_OFFSET			18                 /* Transfer Mode */
#define	SPI_TMOD_TR			    0x00		       /* xmit & recv */
#define SPI_TMOD_TO			    0x01		       /* xmit only */
#define SPI_TMOD_RO			    0x02		       /* recv only */
#define SPI_TMOD_RESV		    0x03

#define SPI_CFS_OFFSET			2                  /* Control Frame Size */

#define SPI_CSM_OFFSET          8                  /* Chip Select Mode */
#define SPI_CSM_KEEP            0x00               /* ss_n keep low after every frame data is transferred */
#define SPI_CSM_HALF            0x01               /* ss_n be high for half sclk_out cycles after every frame data is transferred */
#define SPI_CSM_ONE             0x02               /* ss_n be high for one sclk_out cycle after every frame data is transferred */

#define SPI_SSN_DELAY_OFFSET    10
#define SPI_SSN_DELAY_HALF      0x00
#define SPI_SSN_DELAY_ONE       0x01

#define SPI_HALF_WORLD_TX_OFFSET       13
#define SPI_HALF_WORLD_ON       0x00
#define SPI_HALF_WORLD_OFF      0x01


/* --------Bit fields in CTRLR0--------end */


/* Bit fields in SR, 7 bits */
#define SR_MASK				0x7f		/* cover 7 bits */
#define SR_BUSY				(1 << 0)
#define SR_TF_FULL		    (1 << 1)
#define SR_TF_EMPT			(1 << 2)
#define SR_RF_EMPT		    (1 << 3)
#define SR_RF_FULL			(1 << 4)

/* Bit fields in ISR, IMR, RISR, 7 bits */
#define SPI_INT_TXEI			(1 << 0)
#define SPI_INT_TXOI			(1 << 1)
#define SPI_INT_RXUI			(1 << 2)
#define SPI_INT_RXOI			(1 << 3)
#define SPI_INT_RXFI			(1 << 4)

/* Bit fields in DMACR */
#define SPI_DMACR_TX_ENABLE     (1 << 1)
#define SPI_DMACR_RX_ENABLE     (1 << 0)

/* Bit fields in ICR */
#define SPI_CLEAR_INT_ALL       (1<< 0)
#define SPI_CLEAR_INT_RXUI      (1 << 1)
#define SPI_CLEAR_INT_RXOI      (1 << 2)
#define SPI_CLEAR_INT_TXOI      (1 << 3)

enum rk29xx_ssi_type {
	SSI_MOTO_SPI = 0,
	SSI_TI_SSP,
	SSI_NS_MICROWIRE,
};

struct rk29xx_spi {
	struct spi_master	*master;
	struct spi_device	*cur_dev;
	enum rk29xx_ssi_type	type;

	void __iomem		*regs;
	unsigned long		paddr;
	u32			iolen;
	int			irq;
	u32         irq_polarity;
	u32			fifo_len;	/* depth of the FIFO buffer */
	struct clk		*clock_spim;	/* clk apb */
	struct clk		*pclk;
	struct platform_device	*pdev;
	
	/* Driver message queue */
	struct workqueue_struct	*workqueue;
	struct work_struct	pump_messages;
	spinlock_t		lock;	
	struct mutex 		dma_lock;
	struct list_head	queue;
	int			busy;
	int			run;

	/* Message Transfer pump */
	struct tasklet_struct	pump_transfers;	
	struct tasklet_struct	dma_transfers;	

	/* Current message transfer state info */
	struct spi_message	*cur_msg;
	struct spi_transfer	*cur_transfer;
	struct chip_data	*cur_chip;
	struct chip_data	*prev_chip;
	size_t			len;
	void			*tx;
	void			*tx_end;
	void			*rx;
	void			*rx_end;
	int			dma_mapped;
	dma_addr_t		rx_dma;
	dma_addr_t		tx_dma;
	void			*buffer_tx_dma;
	void			*buffer_rx_dma;
	size_t			rx_map_len;
	size_t			tx_map_len;
	u8			n_bytes;	/* current is a 1/2 bytes op */
	u8			max_bits_per_word;	/* maxim is 16b */
	u32			dma_width;
	int			cs_change;
	int			(*write)(struct rk29xx_spi *dws);
	int			(*read)(struct rk29xx_spi *dws);
	irqreturn_t		(*transfer_handler)(struct rk29xx_spi *dws);
	void (*cs_control)(struct rk29xx_spi *dws, u32 cs, u8 flag);

	/* Dma info */
	struct completion               xfer_completion;
	
	struct completion               tx_completion;
	struct completion               rx_completion;
	unsigned    state;
	unsigned                        cur_speed;
	unsigned long                   sfr_start;
	int			dma_inited;
	enum dma_ch rx_dmach;
	enum dma_ch tx_dmach;
	int			txdma_done;
	int			rxdma_done;
	u64			tx_param;
	u64			rx_param;
	struct device		*dma_dev;
	dma_addr_t		dma_addr;

	/* Bus interface info */
	void			*priv;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif
#ifdef CONFIG_CPU_FREQ
        struct notifier_block   freq_transition;
#endif
};

#define rk29xx_readl(dw, off) \
	__raw_readl(dw->regs + off)
#define rk29xx_writel(dw,off,val) \
	__raw_writel(val, dw->regs + off)
#define rk29xx_readw(dw, off) \
	__raw_readw(dw->regs + off)
#define rk29xx_writew(dw,off,val) \
	__raw_writel(val, dw->regs + off)

static inline void spi_enable_chip(struct rk29xx_spi *dws, int enable)
{
	rk29xx_writel(dws, SPIM_ENR, (enable ? 1 : 0));
}

static inline void spi_set_clk(struct rk29xx_spi *dws, u16 div)
{
	rk29xx_writel(dws, SPIM_BAUDR, div);
}

/* Disable IRQ bits */
static inline void spi_mask_intr(struct rk29xx_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = rk29xx_readl(dws, SPIM_IMR) & ~mask;
	rk29xx_writel(dws, SPIM_IMR, new_mask);
}

/* Enable IRQ bits */
static inline void spi_umask_intr(struct rk29xx_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = rk29xx_readl(dws, SPIM_IMR) | mask;
	rk29xx_writel(dws, SPIM_IMR, new_mask);
}

//spi transfer mode                   add by lyx
#define rk29xx_SPI_HALF_DUPLEX 0
#define rk29xx_SPI_FULL_DUPLEX 1

/*
 * Each SPI slave device to work with rk29xx spi controller should
 * has such a structure claiming its working mode (PIO/DMA etc),
 * which can be save in the "controller_data" member of the
 * struct spi_device
 */
struct rk29xx_spi_chip {
	u8 transfer_mode;/*full or half duplex*/
	u8 poll_mode;	/* 0 for contoller polling mode */
	u8 type;	/* SPI/SSP/Micrwire */
	u8 enable_dma;
	u8 slave_enable;
	void (*cs_control)(struct rk29xx_spi *dws, u32 cs, u8 flag);
};

#endif /* __DRIVERS_SPIM_RK29XX_HEADER_H */
