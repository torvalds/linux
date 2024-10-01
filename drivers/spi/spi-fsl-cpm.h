/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Freescale SPI controller driver cpm functions.
 *
 * Maintainer: Kumar Gala
 *
 * Copyright (C) 2006 Polycom, Inc.
 * Copyright 2010 Freescale Semiconductor, Inc.
 *
 * CPM SPI and QE buffer descriptors mode support:
 * Copyright (c) 2009  MontaVista Software, Inc.
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 */

#ifndef __SPI_FSL_CPM_H__
#define __SPI_FSL_CPM_H__

#include "spi-fsl-lib.h"

#ifdef CONFIG_FSL_SOC
extern void fsl_spi_cpm_reinit_txrx(struct mpc8xxx_spi *mspi);
extern int fsl_spi_cpm_bufs(struct mpc8xxx_spi *mspi,
			    struct spi_transfer *t, bool is_dma_mapped);
extern void fsl_spi_cpm_bufs_complete(struct mpc8xxx_spi *mspi);
extern void fsl_spi_cpm_irq(struct mpc8xxx_spi *mspi, u32 events);
extern int fsl_spi_cpm_init(struct mpc8xxx_spi *mspi);
extern void fsl_spi_cpm_free(struct mpc8xxx_spi *mspi);
#else
static inline void fsl_spi_cpm_reinit_txrx(struct mpc8xxx_spi *mspi) { }
static inline int fsl_spi_cpm_bufs(struct mpc8xxx_spi *mspi,
				   struct spi_transfer *t,
				   bool is_dma_mapped) { return 0; }
static inline void fsl_spi_cpm_bufs_complete(struct mpc8xxx_spi *mspi) { }
static inline void fsl_spi_cpm_irq(struct mpc8xxx_spi *mspi, u32 events) { }
static inline int fsl_spi_cpm_init(struct mpc8xxx_spi *mspi) { return 0; }
static inline void fsl_spi_cpm_free(struct mpc8xxx_spi *mspi) { }
#endif

#endif /* __SPI_FSL_CPM_H__ */
