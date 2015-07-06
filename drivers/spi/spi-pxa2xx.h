/*
 * Copyright (C) 2005 Stephen Street / StreetFire Sound Labs
 * Copyright (C) 2013, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SPI_PXA2XX_H
#define SPI_PXA2XX_H

#include <linux/atomic.h>
#include <linux/dmaengine.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pxa2xx_ssp.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>

struct driver_data {
	/* Driver model hookup */
	struct platform_device *pdev;

	/* SSP Info */
	struct ssp_device *ssp;

	/* SPI framework hookup */
	enum pxa_ssp_type ssp_type;
	struct spi_master *master;

	/* PXA hookup */
	struct pxa2xx_spi_master *master_info;

	/* PXA private DMA setup stuff */
	int rx_channel;
	int tx_channel;
	u32 *null_dma_buf;

	/* SSP register addresses */
	void __iomem *ioaddr;
	u32 ssdr_physical;

	/* SSP masks*/
	u32 dma_cr1;
	u32 int_cr1;
	u32 clear_sr;
	u32 mask_sr;

	/* Maximun clock rate */
	unsigned long max_clk_rate;

	/* Message Transfer pump */
	struct tasklet_struct pump_transfers;

	/* DMA engine support */
	struct dma_chan *rx_chan;
	struct dma_chan *tx_chan;
	struct sg_table rx_sgt;
	struct sg_table tx_sgt;
	int rx_nents;
	int tx_nents;
	void *dummy;
	atomic_t dma_running;

	/* Current message transfer state info */
	struct spi_message *cur_msg;
	struct spi_transfer *cur_transfer;
	struct chip_data *cur_chip;
	size_t len;
	void *tx;
	void *tx_end;
	void *rx;
	void *rx_end;
	int dma_mapped;
	dma_addr_t rx_dma;
	dma_addr_t tx_dma;
	size_t rx_map_len;
	size_t tx_map_len;
	u8 n_bytes;
	int (*write)(struct driver_data *drv_data);
	int (*read)(struct driver_data *drv_data);
	irqreturn_t (*transfer_handler)(struct driver_data *drv_data);
	void (*cs_control)(u32 command);

	void __iomem *lpss_base;
};

struct chip_data {
	u32 cr0;
	u32 cr1;
	u32 dds_rate;
	u32 psp;
	u32 timeout;
	u8 n_bytes;
	u32 dma_burst_size;
	u32 threshold;
	u32 dma_threshold;
	u16 lpss_rx_threshold;
	u16 lpss_tx_threshold;
	u8 enable_dma;
	u8 bits_per_word;
	u32 speed_hz;
	union {
		int gpio_cs;
		unsigned int frm;
	};
	int gpio_cs_inverted;
	int (*write)(struct driver_data *drv_data);
	int (*read)(struct driver_data *drv_data);
	void (*cs_control)(u32 command);
};

static inline u32 pxa2xx_spi_read(const struct driver_data *drv_data,
				  unsigned reg)
{
	return __raw_readl(drv_data->ioaddr + reg);
}

static  inline void pxa2xx_spi_write(const struct driver_data *drv_data,
				     unsigned reg, u32 val)
{
	__raw_writel(val, drv_data->ioaddr + reg);
}

#define START_STATE ((void *)0)
#define RUNNING_STATE ((void *)1)
#define DONE_STATE ((void *)2)
#define ERROR_STATE ((void *)-1)

#define IS_DMA_ALIGNED(x)	IS_ALIGNED((unsigned long)(x), DMA_ALIGNMENT)
#define DMA_ALIGNMENT		8

static inline int pxa25x_ssp_comp(struct driver_data *drv_data)
{
	switch (drv_data->ssp_type) {
	case PXA25x_SSP:
	case CE4100_SSP:
	case QUARK_X1000_SSP:
		return 1;
	default:
		return 0;
	}
}

static inline void write_SSSR_CS(struct driver_data *drv_data, u32 val)
{
	if (drv_data->ssp_type == CE4100_SSP ||
	    drv_data->ssp_type == QUARK_X1000_SSP)
		val |= pxa2xx_spi_read(drv_data, SSSR) & SSSR_ALT_FRM_MASK;

	pxa2xx_spi_write(drv_data, SSSR, val);
}

extern int pxa2xx_spi_flush(struct driver_data *drv_data);
extern void *pxa2xx_spi_next_transfer(struct driver_data *drv_data);

/*
 * Select the right DMA implementation.
 */
#if defined(CONFIG_SPI_PXA2XX_DMA)
#define SPI_PXA2XX_USE_DMA	1
#define MAX_DMA_LEN		SZ_64K
#define DEFAULT_DMA_CR1		(SSCR1_TSRE | SSCR1_RSRE | SSCR1_TRAIL)
#else
#undef SPI_PXA2XX_USE_DMA
#define MAX_DMA_LEN		0
#define DEFAULT_DMA_CR1		0
#endif

#ifdef SPI_PXA2XX_USE_DMA
extern bool pxa2xx_spi_dma_is_possible(size_t len);
extern int pxa2xx_spi_map_dma_buffers(struct driver_data *drv_data);
extern irqreturn_t pxa2xx_spi_dma_transfer(struct driver_data *drv_data);
extern int pxa2xx_spi_dma_prepare(struct driver_data *drv_data, u32 dma_burst);
extern void pxa2xx_spi_dma_start(struct driver_data *drv_data);
extern int pxa2xx_spi_dma_setup(struct driver_data *drv_data);
extern void pxa2xx_spi_dma_release(struct driver_data *drv_data);
extern void pxa2xx_spi_dma_resume(struct driver_data *drv_data);
extern int pxa2xx_spi_set_dma_burst_and_threshold(struct chip_data *chip,
						  struct spi_device *spi,
						  u8 bits_per_word,
						  u32 *burst_code,
						  u32 *threshold);
#else
static inline bool pxa2xx_spi_dma_is_possible(size_t len) { return false; }
static inline int pxa2xx_spi_map_dma_buffers(struct driver_data *drv_data)
{
	return 0;
}
#define pxa2xx_spi_dma_transfer NULL
static inline void pxa2xx_spi_dma_prepare(struct driver_data *drv_data,
					  u32 dma_burst) {}
static inline void pxa2xx_spi_dma_start(struct driver_data *drv_data) {}
static inline int pxa2xx_spi_dma_setup(struct driver_data *drv_data)
{
	return 0;
}
static inline void pxa2xx_spi_dma_release(struct driver_data *drv_data) {}
static inline void pxa2xx_spi_dma_resume(struct driver_data *drv_data) {}
static inline int pxa2xx_spi_set_dma_burst_and_threshold(struct chip_data *chip,
							 struct spi_device *spi,
							 u8 bits_per_word,
							 u32 *burst_code,
							 u32 *threshold)
{
	return -ENODEV;
}
#endif

#endif /* SPI_PXA2XX_H */
