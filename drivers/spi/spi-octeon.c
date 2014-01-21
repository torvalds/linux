/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011, 2012 Cavium, Inc.
 */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-mpi-defs.h>

#define OCTEON_SPI_CFG 0
#define OCTEON_SPI_STS 0x08
#define OCTEON_SPI_TX 0x10
#define OCTEON_SPI_DAT0 0x80

#define OCTEON_SPI_MAX_BYTES 9

#define OCTEON_SPI_MAX_CLOCK_HZ 16000000

struct octeon_spi {
	u64 register_base;
	u64 last_cfg;
	u64 cs_enax;
};

struct octeon_spi_setup {
	u32 max_speed_hz;
	u8 chip_select;
	u8 mode;
	u8 bits_per_word;
};

static void octeon_spi_wait_ready(struct octeon_spi *p)
{
	union cvmx_mpi_sts mpi_sts;
	unsigned int loops = 0;

	do {
		if (loops++)
			__delay(500);
		mpi_sts.u64 = cvmx_read_csr(p->register_base + OCTEON_SPI_STS);
	} while (mpi_sts.s.busy);
}

static int octeon_spi_do_transfer(struct octeon_spi *p,
				  struct spi_message *msg,
				  struct spi_transfer *xfer,
				  bool last_xfer)
{
	union cvmx_mpi_cfg mpi_cfg;
	union cvmx_mpi_tx mpi_tx;
	unsigned int clkdiv;
	unsigned int speed_hz;
	int mode;
	bool cpha, cpol;
	const u8 *tx_buf;
	u8 *rx_buf;
	int len;
	int i;

	struct octeon_spi_setup *msg_setup = spi_get_ctldata(msg->spi);

	speed_hz = msg_setup->max_speed_hz;
	mode = msg_setup->mode;
	cpha = mode & SPI_CPHA;
	cpol = mode & SPI_CPOL;

	if (xfer->speed_hz)
		speed_hz = xfer->speed_hz;

	if (speed_hz > OCTEON_SPI_MAX_CLOCK_HZ)
		speed_hz = OCTEON_SPI_MAX_CLOCK_HZ;

	clkdiv = octeon_get_io_clock_rate() / (2 * speed_hz);

	mpi_cfg.u64 = 0;

	mpi_cfg.s.clkdiv = clkdiv;
	mpi_cfg.s.cshi = (mode & SPI_CS_HIGH) ? 1 : 0;
	mpi_cfg.s.lsbfirst = (mode & SPI_LSB_FIRST) ? 1 : 0;
	mpi_cfg.s.wireor = (mode & SPI_3WIRE) ? 1 : 0;
	mpi_cfg.s.idlelo = cpha != cpol;
	mpi_cfg.s.cslate = cpha ? 1 : 0;
	mpi_cfg.s.enable = 1;

	if (msg_setup->chip_select < 4)
		p->cs_enax |= 1ull << (12 + msg_setup->chip_select);
	mpi_cfg.u64 |= p->cs_enax;

	if (mpi_cfg.u64 != p->last_cfg) {
		p->last_cfg = mpi_cfg.u64;
		cvmx_write_csr(p->register_base + OCTEON_SPI_CFG, mpi_cfg.u64);
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
			cvmx_write_csr(p->register_base + OCTEON_SPI_DAT0 + (8 * i), d);
		}
		mpi_tx.u64 = 0;
		mpi_tx.s.csid = msg_setup->chip_select;
		mpi_tx.s.leavecs = 1;
		mpi_tx.s.txnum = tx_buf ? OCTEON_SPI_MAX_BYTES : 0;
		mpi_tx.s.totnum = OCTEON_SPI_MAX_BYTES;
		cvmx_write_csr(p->register_base + OCTEON_SPI_TX, mpi_tx.u64);

		octeon_spi_wait_ready(p);
		if (rx_buf)
			for (i = 0; i < OCTEON_SPI_MAX_BYTES; i++) {
				u64 v = cvmx_read_csr(p->register_base + OCTEON_SPI_DAT0 + (8 * i));
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
		cvmx_write_csr(p->register_base + OCTEON_SPI_DAT0 + (8 * i), d);
	}

	mpi_tx.u64 = 0;
	mpi_tx.s.csid = msg_setup->chip_select;
	if (last_xfer)
		mpi_tx.s.leavecs = xfer->cs_change;
	else
		mpi_tx.s.leavecs = !xfer->cs_change;
	mpi_tx.s.txnum = tx_buf ? len : 0;
	mpi_tx.s.totnum = len;
	cvmx_write_csr(p->register_base + OCTEON_SPI_TX, mpi_tx.u64);

	octeon_spi_wait_ready(p);
	if (rx_buf)
		for (i = 0; i < len; i++) {
			u64 v = cvmx_read_csr(p->register_base + OCTEON_SPI_DAT0 + (8 * i));
			*rx_buf++ = (u8)v;
		}

	if (xfer->delay_usecs)
		udelay(xfer->delay_usecs);

	return xfer->len;
}

static int octeon_spi_transfer_one_message(struct spi_master *master,
					   struct spi_message *msg)
{
	struct octeon_spi *p = spi_master_get_devdata(master);
	unsigned int total_len = 0;
	int status = 0;
	struct spi_transfer *xfer;

	/*
	 * We better have set the configuration via a call to .setup
	 * before we get here.
	 */
	if (spi_get_ctldata(msg->spi) == NULL) {
		status = -EINVAL;
		goto err;
	}

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		bool last_xfer = &xfer->transfer_list == msg->transfers.prev;
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

static struct octeon_spi_setup *octeon_spi_new_setup(struct spi_device *spi)
{
	struct octeon_spi_setup *setup = kzalloc(sizeof(*setup), GFP_KERNEL);
	if (!setup)
		return NULL;

	setup->max_speed_hz = spi->max_speed_hz;
	setup->chip_select = spi->chip_select;
	setup->mode = spi->mode;
	setup->bits_per_word = spi->bits_per_word;
	return setup;
}

static int octeon_spi_setup(struct spi_device *spi)
{
	struct octeon_spi_setup *new_setup;
	struct octeon_spi_setup *old_setup = spi_get_ctldata(spi);

	new_setup = octeon_spi_new_setup(spi);
	if (!new_setup)
		return -ENOMEM;

	spi_set_ctldata(spi, new_setup);
	kfree(old_setup);

	return 0;
}

static void octeon_spi_cleanup(struct spi_device *spi)
{
	struct octeon_spi_setup *old_setup = spi_get_ctldata(spi);
	spi_set_ctldata(spi, NULL);
	kfree(old_setup);
}

static int octeon_spi_probe(struct platform_device *pdev)
{
	struct resource *res_mem;
	struct spi_master *master;
	struct octeon_spi *p;
	int err = -ENOENT;

	master = spi_alloc_master(&pdev->dev, sizeof(struct octeon_spi));
	if (!master)
		return -ENOMEM;
	p = spi_master_get_devdata(master);
	platform_set_drvdata(pdev, master);

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (res_mem == NULL) {
		dev_err(&pdev->dev, "found no memory resource\n");
		err = -ENXIO;
		goto fail;
	}
	if (!devm_request_mem_region(&pdev->dev, res_mem->start,
				     resource_size(res_mem), res_mem->name)) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		goto fail;
	}
	p->register_base = (u64)devm_ioremap(&pdev->dev, res_mem->start,
					     resource_size(res_mem));

	/* Dynamic bus numbering */
	master->bus_num = -1;
	master->num_chipselect = 4;
	master->mode_bits = SPI_CPHA |
			    SPI_CPOL |
			    SPI_CS_HIGH |
			    SPI_LSB_FIRST |
			    SPI_3WIRE;

	master->setup = octeon_spi_setup;
	master->cleanup = octeon_spi_cleanup;
	master->transfer_one_message = octeon_spi_transfer_one_message;
	master->bits_per_word_mask = SPI_BPW_MASK(8);

	master->dev.of_node = pdev->dev.of_node;
	err = devm_spi_register_master(&pdev->dev, master);
	if (err) {
		dev_err(&pdev->dev, "register master failed: %d\n", err);
		goto fail;
	}

	dev_info(&pdev->dev, "OCTEON SPI bus driver\n");

	return 0;
fail:
	spi_master_put(master);
	return err;
}

static int octeon_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct octeon_spi *p = spi_master_get_devdata(master);
	u64 register_base = p->register_base;

	/* Clear the CSENA* and put everything in a known state. */
	cvmx_write_csr(register_base + OCTEON_SPI_CFG, 0);

	return 0;
}

static struct of_device_id octeon_spi_match[] = {
	{ .compatible = "cavium,octeon-3010-spi", },
	{},
};
MODULE_DEVICE_TABLE(of, octeon_spi_match);

static struct platform_driver octeon_spi_driver = {
	.driver = {
		.name		= "spi-octeon",
		.owner		= THIS_MODULE,
		.of_match_table = octeon_spi_match,
	},
	.probe		= octeon_spi_probe,
	.remove		= octeon_spi_remove,
};

module_platform_driver(octeon_spi_driver);

MODULE_DESCRIPTION("Cavium, Inc. OCTEON SPI bus driver");
MODULE_AUTHOR("David Daney");
MODULE_LICENSE("GPL");
