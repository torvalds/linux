/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011, 2012 Cavium, Inc.
 */

#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "spi-cavium.h"

static void octeon_spi_wait_ready(struct octeon_spi *p)
{
	union cvmx_mpi_sts mpi_sts;
	unsigned int loops = 0;

	do {
		if (loops++)
			__delay(500);
		mpi_sts.u64 = readq(p->register_base + OCTEON_SPI_STS(p));
	} while (mpi_sts.s.busy);
}

static int octeon_spi_do_transfer(struct octeon_spi *p,
				  struct spi_message *msg,
				  struct spi_transfer *xfer,
				  bool last_xfer)
{
	struct spi_device *spi = msg->spi;
	union cvmx_mpi_cfg mpi_cfg;
	union cvmx_mpi_tx mpi_tx;
	unsigned int clkdiv;
	int mode;
	bool cpha, cpol;
	const u8 *tx_buf;
	u8 *rx_buf;
	int len;
	int i;

	mode = spi->mode;
	cpha = mode & SPI_CPHA;
	cpol = mode & SPI_CPOL;

	clkdiv = p->sys_freq / (2 * xfer->speed_hz);

	mpi_cfg.u64 = 0;

	mpi_cfg.s.clkdiv = clkdiv;
	mpi_cfg.s.cshi = (mode & SPI_CS_HIGH) ? 1 : 0;
	mpi_cfg.s.lsbfirst = (mode & SPI_LSB_FIRST) ? 1 : 0;
	mpi_cfg.s.wireor = (mode & SPI_3WIRE) ? 1 : 0;
	mpi_cfg.s.idlelo = cpha != cpol;
	mpi_cfg.s.cslate = cpha ? 1 : 0;
	mpi_cfg.s.enable = 1;

	if (spi->chip_select < 4)
		p->cs_enax |= 1ull << (12 + spi->chip_select);
	mpi_cfg.u64 |= p->cs_enax;

	if (mpi_cfg.u64 != p->last_cfg) {
		p->last_cfg = mpi_cfg.u64;
		writeq(mpi_cfg.u64, p->register_base + OCTEON_SPI_CFG(p));
	}
	tx_buf = xfer->tx_buf;
	rx_buf = xfer->rx_buf;
	len = xfer->len;
	while (len > OCTEON_SPI_MAX_BYTES) {
		for (i = 0; i < OCTEON_SPI_MAX_BYTES; i++) {
			u8 d;
			if (tx_buf)
				d = *tx_buf++;
			else
				d = 0;
			writeq(d, p->register_base + OCTEON_SPI_DAT0(p) + (8 * i));
		}
		mpi_tx.u64 = 0;
		mpi_tx.s.csid = spi->chip_select;
		mpi_tx.s.leavecs = 1;
		mpi_tx.s.txnum = tx_buf ? OCTEON_SPI_MAX_BYTES : 0;
		mpi_tx.s.totnum = OCTEON_SPI_MAX_BYTES;
		writeq(mpi_tx.u64, p->register_base + OCTEON_SPI_TX(p));

		octeon_spi_wait_ready(p);
		if (rx_buf)
			for (i = 0; i < OCTEON_SPI_MAX_BYTES; i++) {
				u64 v = readq(p->register_base + OCTEON_SPI_DAT0(p) + (8 * i));
				*rx_buf++ = (u8)v;
			}
		len -= OCTEON_SPI_MAX_BYTES;
	}

	for (i = 0; i < len; i++) {
		u8 d;
		if (tx_buf)
			d = *tx_buf++;
		else
			d = 0;
		writeq(d, p->register_base + OCTEON_SPI_DAT0(p) + (8 * i));
	}

	mpi_tx.u64 = 0;
	mpi_tx.s.csid = spi->chip_select;
	if (last_xfer)
		mpi_tx.s.leavecs = xfer->cs_change;
	else
		mpi_tx.s.leavecs = !xfer->cs_change;
	mpi_tx.s.txnum = tx_buf ? len : 0;
	mpi_tx.s.totnum = len;
	writeq(mpi_tx.u64, p->register_base + OCTEON_SPI_TX(p));

	octeon_spi_wait_ready(p);
	if (rx_buf)
		for (i = 0; i < len; i++) {
			u64 v = readq(p->register_base + OCTEON_SPI_DAT0(p) + (8 * i));
			*rx_buf++ = (u8)v;
		}

	spi_transfer_delay_exec(xfer);

	return xfer->len;
}

int octeon_spi_transfer_one_message(struct spi_master *master,
				    struct spi_message *msg)
{
	struct octeon_spi *p = spi_master_get_devdata(master);
	unsigned int total_len = 0;
	int status = 0;
	struct spi_transfer *xfer;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		bool last_xfer = list_is_last(&xfer->transfer_list,
					      &msg->transfers);
		int r = octeon_spi_do_transfer(p, msg, xfer, last_xfer);
		if (r < 0) {
			status = r;
			goto err;
		}
		total_len += r;
	}
err:
	msg->status = status;
	msg->actual_length = total_len;
	spi_finalize_current_message(master);
	return status;
}
