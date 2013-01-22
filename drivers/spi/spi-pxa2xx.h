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

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pxa2xx_ssp.h>
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
};

struct chip_data {
	u32 cr0;
	u32 cr1;
	u32 psp;
	u32 timeout;
	u8 n_bytes;
	u32 dma_burst_size;
	u32 threshold;
	u32 dma_threshold;
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

#define DEFINE_SSP_REG(reg, off) \
static inline u32 read_##reg(void const __iomem *p) \
{ return __raw_readl(p + (off)); } \
\
static inline void write_##reg(u32 v, void __iomem *p) \
{ __raw_writel(v, p + (off)); }

DEFINE_SSP_REG(SSCR0, 0x00)
DEFINE_SSP_REG(SSCR1, 0x04)
DEFINE_SSP_REG(SSSR, 0x08)
DEFINE_SSP_REG(SSITR, 0x0c)
DEFINE_SSP_REG(SSDR, 0x10)
DEFINE_SSP_REG(SSTO, 0x28)
DEFINE_SSP_REG(SSPSP, 0x2c)

#define START_STATE ((void *)0)
#define RUNNING_STATE ((void *)1)
#define DONE_STATE ((void *)2)
#define ERROR_STATE ((void *)-1)

#define MAX_DMA_LEN		8191
#define IS_DMA_ALIGNED(x)	IS_ALIGNED((unsigned long)(x), DMA_ALIGNMENT)
#define DMA_ALIGNMENT		8

static inline int pxa25x_ssp_comp(struct driver_data *drv_data)
{
	if (drv_data->ssp_type == PXA25x_SSP)
		return 1;
	if (drv_data->ssp_type == CE4100_SSP)
		return 1;
	return 0;
}

static inline void write_SSSR_CS(struct driver_data *drv_data, u32 val)
{
	void __iomem *reg = drv_data->ioaddr;

	if (drv_data->ssp_type == CE4100_SSP)
		val |= read_SSSR(reg) & SSSR_ALT_FRM_MASK;

	write_SSSR(val, reg);
}

extern int pxa2xx_spi_flush(struct driver_data *drv_data);
extern void *pxa2xx_spi_next_transfer(struct driver_data *drv_data);

#if defined(CONFIG_SPI_PXA2XX_PXADMA)
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
