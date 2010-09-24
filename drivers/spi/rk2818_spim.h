/* drivers/spi/rk2818_spim.h
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
#ifndef __DRIVERS_SPIM_RK2818_HEADER_H
#define __DRIVERS_SPIM_RK2818_HEADER_H
#include <linux/io.h>

/* Bit fields in CTRLR0 */
#define SPI_DFS_OFFSET			0

#define SPI_FRF_OFFSET			4
#define SPI_FRF_SPI			0x0
#define SPI_FRF_SSP			0x1
#define SPI_FRF_MICROWIRE		0x2
#define SPI_FRF_RESV			0x3

#define SPI_MODE_OFFSET			6
#define SPI_SCPH_OFFSET			6
#define SPI_SCOL_OFFSET			7
#define SPI_TMOD_OFFSET			8
#define	SPI_TMOD_TR			0x0		/* xmit & recv */
#define SPI_TMOD_TO			0x1		/* xmit only */
#define SPI_TMOD_RO			0x2		/* recv only */
#define SPI_TMOD_EPROMREAD		0x3		/* eeprom read mode */

#define SPI_SLVOE_OFFSET		10
#define SPI_SRL_OFFSET			11
#define SPI_CFS_OFFSET			12

/* Bit fields in SR, 7 bits */
#define SR_MASK				0x7f		/* cover 7 bits */
#define SR_BUSY				(1 << 0)
#define SR_TF_NOT_FULL			(1 << 1)
#define SR_TF_EMPT			(1 << 2)
#define SR_RF_NOT_EMPT			(1 << 3)
#define SR_RF_FULL			(1 << 4)
#define SR_TX_ERR			(1 << 5)
#define SR_DCOL				(1 << 6)

/* Bit fields in ISR, IMR, RISR, 7 bits */
#define SPI_INT_TXEI			(1 << 0)
#define SPI_INT_TXOI			(1 << 1)
#define SPI_INT_RXUI			(1 << 2)
#define SPI_INT_RXOI			(1 << 3)
#define SPI_INT_RXFI			(1 << 4)
#define SPI_INT_MSTI			(1 << 5)

/* TX RX interrupt level threshhold, max can be 256 */
#define SPI_INT_THRESHOLD		32

#define SPIM_CTRLR0				0x0000
#define SPIM_CTRLR1				0x0004
#define SPIM_SPIENR				0x0008
#define SPIM_MWCR				0x000c
#define SPIM_SER				0x0010
#define SPIM_BAUDR				0x0014
#define SPIM_TXFTLR				0x0018
#define SPIM_RXFTLR				0x001c
#define SPIM_TXFLR				0x0020
#define SPIM_RXFLR				0x0024
#define SPIM_SR					0x0028
#define SPIM_IMR				0x002c
#define SPIM_ISR				0x0030
#define SPIM_RISR				0x0034
#define SPIM_TXOICR				0x0038
#define SPIM_RXOICR				0x003c
#define SPIM_RXUICR				0x0040
#define SPIM_MSTICR				0x0044
#define SPIM_ICR				0x0048
#define SPIM_DMACR				0x004c
#define SPIM_DMATDLR			0x0050
#define SPIM_DMARDLR			0x0054
#define SPIM_IDR0				0x0058
#define SPIM_COMP_VERSION		0x005c
#define SPIM_DR0				0x0060

enum rk2818_ssi_type {
	SSI_MOTO_SPI = 0,
	SSI_TI_SSP,
	SSI_NS_MICROWIRE,
};

struct rk2818_spi {
	struct spi_master	*master;
	struct spi_device	*cur_dev;
	enum rk2818_ssi_type	type;

	void __iomem		*regs;
	unsigned long		paddr;
	u32			iolen;
	int			irq;
	u32			fifo_len;	/* depth of the FIFO buffer */
	struct clk		*clock_spim;	/* clk apb */
	struct platform_device	*pdev;
	
	/* Driver message queue */
	struct workqueue_struct	*workqueue;
	struct work_struct	pump_messages;
	spinlock_t		lock;
	struct list_head	queue;
	int			busy;
	int			run;

	/* Message Transfer pump */
	struct tasklet_struct	pump_transfers;

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
	size_t			rx_map_len;
	size_t			tx_map_len;
	u8			n_bytes;	/* current is a 1/2 bytes op */
	u8			max_bits_per_word;	/* maxim is 16b */
	u32			dma_width;
	int			cs_change;
	int			(*write)(struct rk2818_spi *dws);
	int			(*read)(struct rk2818_spi *dws);
	irqreturn_t		(*transfer_handler)(struct rk2818_spi *dws);
	void (*cs_control)(struct rk2818_spi *dws, u32 cs, u8 flag);

	/* Dma info */
	int			dma_inited;
	struct dma_chan		*txchan;
	struct dma_chan		*rxchan;
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

#define rk2818_readl(dw, off) \
	__raw_readl(dw->regs + off)
#define rk2818_writel(dw,off,val) \
	__raw_writel(val, dw->regs + off)
#define rk2818_readw(dw, off) \
	__raw_readw(dw->regs + off)
#define rk2818_writew(dw,off,val) \
	__raw_writel(val, dw->regs + off)

static inline void spi_enable_chip(struct rk2818_spi *dws, int enable)
{
	rk2818_writel(dws, SPIM_SPIENR, (enable ? 1 : 0));
}

static inline void spi_set_clk(struct rk2818_spi *dws, u16 div)
{
	rk2818_writel(dws, SPIM_BAUDR, div);
}

/* Disable IRQ bits */
static inline void spi_mask_intr(struct rk2818_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = rk2818_readl(dws, SPIM_IMR) & ~mask;
	rk2818_writel(dws, SPIM_IMR, new_mask);
}

/* Enable IRQ bits */
static inline void spi_umask_intr(struct rk2818_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = rk2818_readl(dws, SPIM_IMR) | mask;
	rk2818_writel(dws, SPIM_IMR, new_mask);
}

//spi transfer mode                   add by lyx
#define RK2818_SPI_HALF_DUPLEX 0
#define RK2818_SPI_FULL_DUPLEX 1

/*
 * Each SPI slave device to work with rk2818_api controller should
 * has such a structure claiming its working mode (PIO/DMA etc),
 * which can be save in the "controller_data" member of the
 * struct spi_device
 */
struct rk2818_spi_chip {
	u8 transfer_mode;/*full or half duplex*/
	u8 poll_mode;	/* 0 for contoller polling mode */
	u8 type;	/* SPI/SSP/Micrwire */
	u8 enable_dma;
	void (*cs_control)(struct rk2818_spi *dws, u32 cs, u8 flag);
};

#endif /* __DRIVERS_SPIM_RK2818_HEADER_H */
