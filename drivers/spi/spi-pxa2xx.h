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
	struct spi_controller *master;

	/* PXA hookup */
	struct pxa2xx_spi_master *master_info;

	/* SSP register addresses */
	void __iomem *ioaddr;
	phys_addr_t ssdr_physical;

	/* SSP masks*/
	u32 dma_cr1;
	u32 int_cr1;
	u32 clear_sr;
	u32 mask_sr;

	/* DMA engine support */
	atomic_t dma_running;

	/* Current transfer state info */
	void *tx;
	void *tx_end;
	void *rx;
	void *rx_end;
	u8 n_bytes;
	int (*write)(struct driver_data *drv_data);
	int (*read)(struct driver_data *drv_data);
	irqreturn_t (*transfer_handler)(struct driver_data *drv_data);
	void (*cs_control)(u32 command);

	void __iomem *lpss_base;

	/* GPIOs for chip selects */
	struct gpio_desc **cs_gpiods;
};

struct chip_data {
	u32 cr1;
	u32 dds_rate;
	u32 timeout;
	u8 n_bytes;
	u32 dma_burst_size;
	u32 threshold;
	u32 dma_threshold;
	u16 lpss_rx_threshold;
	u16 lpss_tx_threshold;
	u8 enable_dma;
	union {
		struct gpio_desc *gpiod_cs;
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

#define MAX_DMA_LEN		SZ_64K
#define DEFAULT_DMA_CR1		(SSCR1_TSRE | SSCR1_RSRE | SSCR1_TRAIL)

extern irqreturn_t pxa2xx_spi_dma_transfer(struct driver_data *drv_data);
extern int pxa2xx_spi_dma_prepare(struct driver_data *drv_data,
				  struct spi_transfer *xfer);
extern void pxa2xx_spi_dma_start(struct driver_data *drv_data);
extern void pxa2xx_spi_dma_stop(struct driver_data *drv_data);
extern int pxa2xx_spi_dma_setup(struct driver_data *drv_data);
extern void pxa2xx_spi_dma_release(struct driver_data *drv_data);
extern int pxa2xx_spi_set_dma_burst_and_threshold(struct chip_data *chip,
						  struct spi_device *spi,
						  u8 bits_per_word,
						  u32 *burst_code,
						  u32 *threshold);

#endif /* SPI_PXA2XX_H */
