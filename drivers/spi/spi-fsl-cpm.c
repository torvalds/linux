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
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/fsl_devices.h>
#include <linux/dma-mapping.h>
#include <asm/cpm.h>
#include <asm/qe.h>

#include "spi-fsl-lib.h"
#include "spi-fsl-cpm.h"
#include "spi-fsl-spi.h"

/* CPM1 and CPM2 are mutually exclusive. */
#ifdef CONFIG_CPM1
#include <asm/cpm1.h>
#define CPM_SPI_CMD mk_cr_cmd(CPM_CR_CH_SPI, 0)
#else
#include <asm/cpm2.h>
#define CPM_SPI_CMD mk_cr_cmd(CPM_CR_SPI_PAGE, CPM_CR_SPI_SBLOCK, 0, 0)
#endif

#define	SPIE_TXB	0x00000200	/* Last char is written to tx fifo */
#define	SPIE_RXB	0x00000100	/* Last char is written to rx buf */

/* SPCOM register values */
#define	SPCOM_STR	(1 << 23)	/* Start transmit */

#define	SPI_PRAM_SIZE	0x100
#define	SPI_MRBLR	((unsigned int)PAGE_SIZE)

static void *fsl_dummy_rx;
static DEFINE_MUTEX(fsl_dummy_rx_lock);
static int fsl_dummy_rx_refcnt;

void fsl_spi_cpm_reinit_txrx(struct mpc8xxx_spi *mspi)
{
	if (mspi->flags & SPI_QE) {
		qe_issue_cmd(QE_INIT_TX_RX, mspi->subblock,
			     QE_CR_PROTOCOL_UNSPECIFIED, 0);
	} else {
		cpm_command(CPM_SPI_CMD, CPM_CR_INIT_TRX);
		if (mspi->flags & SPI_CPM1) {
			out_be16(&mspi->pram->rbptr,
				 in_be16(&mspi->pram->rbase));
			out_be16(&mspi->pram->tbptr,
				 in_be16(&mspi->pram->tbase));
		}
	}
}

static void fsl_spi_cpm_bufs_start(struct mpc8xxx_spi *mspi)
{
	struct cpm_buf_desc __iomem *tx_bd = mspi->tx_bd;
	struct cpm_buf_desc __iomem *rx_bd = mspi->rx_bd;
	unsigned int xfer_len = min(mspi->count, SPI_MRBLR);
	unsigned int xfer_ofs;
	struct fsl_spi_reg *reg_base = mspi->reg_base;

	xfer_ofs = mspi->xfer_in_progress->len - mspi->count;

	if (mspi->rx_dma == mspi->dma_dummy_rx)
		out_be32(&rx_bd->cbd_bufaddr, mspi->rx_dma);
	else
		out_be32(&rx_bd->cbd_bufaddr, mspi->rx_dma + xfer_ofs);
	out_be16(&rx_bd->cbd_datlen, 0);
	out_be16(&rx_bd->cbd_sc, BD_SC_EMPTY | BD_SC_INTRPT | BD_SC_WRAP);

	if (mspi->tx_dma == mspi->dma_dummy_tx)
		out_be32(&tx_bd->cbd_bufaddr, mspi->tx_dma);
	else
		out_be32(&tx_bd->cbd_bufaddr, mspi->tx_dma + xfer_ofs);
	out_be16(&tx_bd->cbd_datlen, xfer_len);
	out_be16(&tx_bd->cbd_sc, BD_SC_READY | BD_SC_INTRPT | BD_SC_WRAP |
				 BD_SC_LAST);

	/* start transfer */
	mpc8xxx_spi_write_reg(&reg_base->command, SPCOM_STR);
}

int fsl_spi_cpm_bufs(struct mpc8xxx_spi *mspi,
		     struct spi_transfer *t, bool is_dma_mapped)
{
	struct device *dev = mspi->dev;
	struct fsl_spi_reg *reg_base = mspi->reg_base;

	if (is_dma_mapped) {
		mspi->map_tx_dma = 0;
		mspi->map_rx_dma = 0;
	} else {
		mspi->map_tx_dma = 1;
		mspi->map_rx_dma = 1;
	}

	if (!t->tx_buf) {
		mspi->tx_dma = mspi->dma_dummy_tx;
		mspi->map_tx_dma = 0;
	}

	if (!t->rx_buf) {
		mspi->rx_dma = mspi->dma_dummy_rx;
		mspi->map_rx_dma = 0;
	}

	if (mspi->map_tx_dma) {
		void *nonconst_tx = (void *)mspi->tx; /* shut up gcc */

		mspi->tx_dma = dma_map_single(dev, nonconst_tx, t->len,
					      DMA_TO_DEVICE);
		if (dma_mapping_error(dev, mspi->tx_dma)) {
			dev_err(dev, "unable to map tx dma\n");
			return -ENOMEM;
		}
	} else if (t->tx_buf) {
		mspi->tx_dma = t->tx_dma;
	}

	if (mspi->map_rx_dma) {
		mspi->rx_dma = dma_map_single(dev, mspi->rx, t->len,
					      DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, mspi->rx_dma)) {
			dev_err(dev, "unable to map rx dma\n");
			goto err_rx_dma;
		}
	} else if (t->rx_buf) {
		mspi->rx_dma = t->rx_dma;
	}

	/* enable rx ints */
	mpc8xxx_spi_write_reg(&reg_base->mask, SPIE_RXB);

	mspi->xfer_in_progress = t;
	mspi->count = t->len;

	/* start CPM transfers */
	fsl_spi_cpm_bufs_start(mspi);

	return 0;

err_rx_dma:
	if (mspi->map_tx_dma)
		dma_unmap_single(dev, mspi->tx_dma, t->len, DMA_TO_DEVICE);
	return -ENOMEM;
}

void fsl_spi_cpm_bufs_complete(struct mpc8xxx_spi *mspi)
{
	struct device *dev = mspi->dev;
	struct spi_transfer *t = mspi->xfer_in_progress;

	if (mspi->map_tx_dma)
		dma_unmap_single(dev, mspi->tx_dma, t->len, DMA_TO_DEVICE);
	if (mspi->map_rx_dma)
		dma_unmap_single(dev, mspi->rx_dma, t->len, DMA_FROM_DEVICE);
	mspi->xfer_in_progress = NULL;
}

void fsl_spi_cpm_irq(struct mpc8xxx_spi *mspi, u32 events)
{
	u16 len;
	struct fsl_spi_reg *reg_base = mspi->reg_base;

	dev_dbg(mspi->dev, "%s: bd datlen %d, count %d\n", __func__,
		in_be16(&mspi->rx_bd->cbd_datlen), mspi->count);

	len = in_be16(&mspi->rx_bd->cbd_datlen);
	if (len > mspi->count) {
		WARN_ON(1);
		len = mspi->count;
	}

	/* Clear the events */
	mpc8xxx_spi_write_reg(&reg_base->event, events);

	mspi->count -= len;
	if (mspi->count)
		fsl_spi_cpm_bufs_start(mspi);
	else
		complete(&mspi->done);
}

static void *fsl_spi_alloc_dummy_rx(void)
{
	mutex_lock(&fsl_dummy_rx_lock);

	if (!fsl_dummy_rx)
		fsl_dummy_rx = kmalloc(SPI_MRBLR, GFP_KERNEL);
	if (fsl_dummy_rx)
		fsl_dummy_rx_refcnt++;

	mutex_unlock(&fsl_dummy_rx_lock);

	return fsl_dummy_rx;
}

static void fsl_spi_free_dummy_rx(void)
{
	mutex_lock(&fsl_dummy_rx_lock);

	switch (fsl_dummy_rx_refcnt) {
	case 0:
		WARN_ON(1);
		break;
	case 1:
		kfree(fsl_dummy_rx);
		fsl_dummy_rx = NULL;
		/* fall through */
	default:
		fsl_dummy_rx_refcnt--;
		break;
	}

	mutex_unlock(&fsl_dummy_rx_lock);
}

static unsigned long fsl_spi_cpm_get_pram(struct mpc8xxx_spi *mspi)
{
	struct device *dev = mspi->dev;
	struct device_node *np = dev->of_node;
	const u32 *iprop;
	int size;
	void __iomem *spi_base;
	unsigned long pram_ofs = -ENOMEM;

	/* Can't use of_address_to_resource(), QE muram isn't at 0. */
	iprop = of_get_property(np, "reg", &size);

	/* QE with a fixed pram location? */
	if (mspi->flags & SPI_QE && iprop && size == sizeof(*iprop) * 4)
		return cpm_muram_alloc_fixed(iprop[2], SPI_PRAM_SIZE);

	/* QE but with a dynamic pram location? */
	if (mspi->flags & SPI_QE) {
		pram_ofs = cpm_muram_alloc(SPI_PRAM_SIZE, 64);
		qe_issue_cmd(QE_ASSIGN_PAGE_TO_DEVICE, mspi->subblock,
			     QE_CR_PROTOCOL_UNSPECIFIED, pram_ofs);
		return pram_ofs;
	}

	spi_base = of_iomap(np, 1);
	if (spi_base == NULL)
		return -EINVAL;

	if (mspi->flags & SPI_CPM2) {
		pram_ofs = cpm_muram_alloc(SPI_PRAM_SIZE, 64);
		out_be16(spi_base, pram_ofs);
	} else {
		struct spi_pram __iomem *pram = spi_base;
		u16 rpbase = in_be16(&pram->rpbase);

		/* Microcode relocation patch applied? */
		if (rpbase) {
			pram_ofs = rpbase;
		} else {
			pram_ofs = cpm_muram_alloc(SPI_PRAM_SIZE, 64);
			out_be16(spi_base, pram_ofs);
		}
	}

	iounmap(spi_base);
	return pram_ofs;
}

int fsl_spi_cpm_init(struct mpc8xxx_spi *mspi)
{
	struct device *dev = mspi->dev;
	struct device_node *np = dev->of_node;
	const u32 *iprop;
	int size;
	unsigned long pram_ofs;
	unsigned long bds_ofs;

	if (!(mspi->flags & SPI_CPM_MODE))
		return 0;

	if (!fsl_spi_alloc_dummy_rx())
		return -ENOMEM;

	if (mspi->flags & SPI_QE) {
		iprop = of_get_property(np, "cell-index", &size);
		if (iprop && size == sizeof(*iprop))
			mspi->subblock = *iprop;

		switch (mspi->subblock) {
		default:
			dev_warn(dev, "cell-index unspecified, assuming SPI1");
			/* fall through */
		case 0:
			mspi->subblock = QE_CR_SUBBLOCK_SPI1;
			break;
		case 1:
			mspi->subblock = QE_CR_SUBBLOCK_SPI2;
			break;
		}
	}

	pram_ofs = fsl_spi_cpm_get_pram(mspi);
	if (IS_ERR_VALUE(pram_ofs)) {
		dev_err(dev, "can't allocate spi parameter ram\n");
		goto err_pram;
	}

	bds_ofs = cpm_muram_alloc(sizeof(*mspi->tx_bd) +
				  sizeof(*mspi->rx_bd), 8);
	if (IS_ERR_VALUE(bds_ofs)) {
		dev_err(dev, "can't allocate bds\n");
		goto err_bds;
	}

	mspi->dma_dummy_tx = dma_map_single(dev, empty_zero_page, PAGE_SIZE,
					    DMA_TO_DEVICE);
	if (dma_mapping_error(dev, mspi->dma_dummy_tx)) {
		dev_err(dev, "unable to map dummy tx buffer\n");
		goto err_dummy_tx;
	}

	mspi->dma_dummy_rx = dma_map_single(dev, fsl_dummy_rx, SPI_MRBLR,
					    DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, mspi->dma_dummy_rx)) {
		dev_err(dev, "unable to map dummy rx buffer\n");
		goto err_dummy_rx;
	}

	mspi->pram = cpm_muram_addr(pram_ofs);

	mspi->tx_bd = cpm_muram_addr(bds_ofs);
	mspi->rx_bd = cpm_muram_addr(bds_ofs + sizeof(*mspi->tx_bd));

	/* Initialize parameter ram. */
	out_be16(&mspi->pram->tbase, cpm_muram_offset(mspi->tx_bd));
	out_be16(&mspi->pram->rbase, cpm_muram_offset(mspi->rx_bd));
	out_8(&mspi->pram->tfcr, CPMFCR_EB | CPMFCR_GBL);
	out_8(&mspi->pram->rfcr, CPMFCR_EB | CPMFCR_GBL);
	out_be16(&mspi->pram->mrblr, SPI_MRBLR);
	out_be32(&mspi->pram->rstate, 0);
	out_be32(&mspi->pram->rdp, 0);
	out_be16(&mspi->pram->rbptr, 0);
	out_be16(&mspi->pram->rbc, 0);
	out_be32(&mspi->pram->rxtmp, 0);
	out_be32(&mspi->pram->tstate, 0);
	out_be32(&mspi->pram->tdp, 0);
	out_be16(&mspi->pram->tbptr, 0);
	out_be16(&mspi->pram->tbc, 0);
	out_be32(&mspi->pram->txtmp, 0);

	return 0;

err_dummy_rx:
	dma_unmap_single(dev, mspi->dma_dummy_tx, PAGE_SIZE, DMA_TO_DEVICE);
err_dummy_tx:
	cpm_muram_free(bds_ofs);
err_bds:
	cpm_muram_free(pram_ofs);
err_pram:
	fsl_spi_free_dummy_rx();
	return -ENOMEM;
}

void fsl_spi_cpm_free(struct mpc8xxx_spi *mspi)
{
	struct device *dev = mspi->dev;

	if (!(mspi->flags & SPI_CPM_MODE))
		return;

	dma_unmap_single(dev, mspi->dma_dummy_rx, SPI_MRBLR, DMA_FROM_DEVICE);
	dma_unmap_single(dev, mspi->dma_dummy_tx, PAGE_SIZE, DMA_TO_DEVICE);
	cpm_muram_free(cpm_muram_offset(mspi->tx_bd));
	cpm_muram_free(cpm_muram_offset(mspi->pram));
	fsl_spi_free_dummy_rx();
}
