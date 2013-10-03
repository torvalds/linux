/*
 * Freescale SPI/eSPI controller driver library.
 *
 * Maintainer: Kumar Gala
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 * Copyright (C) 2006 Polycom, Inc.
 *
 * CPM SPI and QE buffer descriptors mode support:
 * Copyright (c) 2009  MontaVista Software, Inc.
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __SPI_FSL_LIB_H__
#define __SPI_FSL_LIB_H__

#include <asm/io.h>

/* SPI/eSPI Controller driver's private data. */
struct mpc8xxx_spi {
	struct device *dev;
	void *reg_base;

	/* rx & tx bufs from the spi_transfer */
	const void *tx;
	void *rx;
#ifdef CONFIG_SPI_FSL_ESPI
	int len;
#endif

	int subblock;
	struct spi_pram __iomem *pram;
#ifdef CONFIG_FSL_SOC
	struct cpm_buf_desc __iomem *tx_bd;
	struct cpm_buf_desc __iomem *rx_bd;
#endif

	struct spi_transfer *xfer_in_progress;

	/* dma addresses for CPM transfers */
	dma_addr_t tx_dma;
	dma_addr_t rx_dma;
	bool map_tx_dma;
	bool map_rx_dma;

	dma_addr_t dma_dummy_tx;
	dma_addr_t dma_dummy_rx;

	/* functions to deal with different sized buffers */
	void (*get_rx) (u32 rx_data, struct mpc8xxx_spi *);
	u32(*get_tx) (struct mpc8xxx_spi *);

	/* hooks for different controller driver */
	void (*spi_do_one_msg) (struct spi_message *m);
	void (*spi_remove) (struct mpc8xxx_spi *mspi);

	unsigned int count;
	unsigned int irq;

	unsigned nsecs;		/* (clock cycle time)/2 */

	u32 spibrg;		/* SPIBRG input clock */
	u32 rx_shift;		/* RX data reg shift when in qe mode */
	u32 tx_shift;		/* TX data reg shift when in qe mode */

	unsigned int flags;

#ifdef CONFIG_SPI_FSL_SPI
	int type;
	int native_chipselects;
	u8 max_bits_per_word;

	void (*set_shifts)(u32 *rx_shift, u32 *tx_shift,
			   int bits_per_word, int msb_first);
#endif

	struct workqueue_struct *workqueue;
	struct work_struct work;

	struct list_head queue;
	spinlock_t lock;

	struct completion done;
};

struct spi_mpc8xxx_cs {
	/* functions to deal with different sized buffers */
	void (*get_rx) (u32 rx_data, struct mpc8xxx_spi *);
	u32 (*get_tx) (struct mpc8xxx_spi *);
	u32 rx_shift;		/* RX data reg shift when in qe mode */
	u32 tx_shift;		/* TX data reg shift when in qe mode */
	u32 hw_mode;		/* Holds HW mode register settings */
};

static inline void mpc8xxx_spi_write_reg(__be32 __iomem *reg, u32 val)
{
	iowrite32be(val, reg);
}

static inline u32 mpc8xxx_spi_read_reg(__be32 __iomem *reg)
{
	return ioread32be(reg);
}

struct mpc8xxx_spi_probe_info {
	struct fsl_spi_platform_data pdata;
	int *gpios;
	bool *alow_flags;
};

extern u32 mpc8xxx_spi_tx_buf_u8(struct mpc8xxx_spi *mpc8xxx_spi);
extern u32 mpc8xxx_spi_tx_buf_u16(struct mpc8xxx_spi *mpc8xxx_spi);
extern u32 mpc8xxx_spi_tx_buf_u32(struct mpc8xxx_spi *mpc8xxx_spi);
extern void mpc8xxx_spi_rx_buf_u8(u32 data, struct mpc8xxx_spi *mpc8xxx_spi);
extern void mpc8xxx_spi_rx_buf_u16(u32 data, struct mpc8xxx_spi *mpc8xxx_spi);
extern void mpc8xxx_spi_rx_buf_u32(u32 data, struct mpc8xxx_spi *mpc8xxx_spi);

extern struct mpc8xxx_spi_probe_info *to_of_pinfo(
		struct fsl_spi_platform_data *pdata);
extern int mpc8xxx_spi_bufs(struct mpc8xxx_spi *mspi,
		struct spi_transfer *t, unsigned int len);
extern int mpc8xxx_spi_transfer(struct spi_device *spi, struct spi_message *m);
extern void mpc8xxx_spi_cleanup(struct spi_device *spi);
extern const char *mpc8xxx_spi_strmode(unsigned int flags);
extern int mpc8xxx_spi_probe(struct device *dev, struct resource *mem,
		unsigned int irq);
extern int mpc8xxx_spi_remove(struct device *dev);
extern int of_mpc8xxx_spi_probe(struct platform_device *ofdev);

#endif /* __SPI_FSL_LIB_H__ */
