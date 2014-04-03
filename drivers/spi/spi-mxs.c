/*
 * Freescale MXS SPI master driver
 *
 * Copyright 2012 DENX Software Engineering, GmbH.
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * Rework and transition to new API by:
 * Marek Vasut <marex@denx.de>
 *
 * Based on previous attempt by:
 * Fabio Estevam <fabio.estevam@freescale.com>
 *
 * Based on code from U-Boot bootloader by:
 * Marek Vasut <marex@denx.de>
 *
 * Based on spi-stmp.c, which is:
 * Author: Dmitry Pervushin <dimka@embeddedalley.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/highmem.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/stmp_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/mxs-spi.h>

#define DRIVER_NAME		"mxs-spi"

/* Use 10S timeout for very long transfers, it should suffice. */
#define SSP_TIMEOUT		10000

#define SG_MAXLEN		0xff00

/*
 * Flags for txrx functions.  More efficient that using an argument register for
 * each one.
 */
#define TXRX_WRITE		(1<<0)	/* This is a write */
#define TXRX_DEASSERT_CS	(1<<1)	/* De-assert CS at end of txrx */

struct mxs_spi {
	struct mxs_ssp		ssp;
	struct completion	c;
	unsigned int		sck;	/* Rate requested (vs actual) */
};

static int mxs_spi_setup_transfer(struct spi_device *dev,
				  const struct spi_transfer *t)
{
	struct mxs_spi *spi = spi_master_get_devdata(dev->master);
	struct mxs_ssp *ssp = &spi->ssp;
	const unsigned int hz = min(dev->max_speed_hz, t->speed_hz);

	if (hz == 0) {
		dev_err(&dev->dev, "SPI clock rate of zero not allowed\n");
		return -EINVAL;
	}

	if (hz != spi->sck) {
		mxs_ssp_set_clk_rate(ssp, hz);
		/*
		 * Save requested rate, hz, rather than the actual rate,
		 * ssp->clk_rate.  Otherwise we would set the rate every trasfer
		 * when the actual rate is not quite the same as requested rate.
		 */
		spi->sck = hz;
		/*
		 * Perhaps we should return an error if the actual clock is
		 * nowhere close to what was requested?
		 */
	}

	writel(BM_SSP_CTRL0_LOCK_CS,
		ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_SET);

	writel(BF_SSP_CTRL1_SSP_MODE(BV_SSP_CTRL1_SSP_MODE__SPI) |
	       BF_SSP_CTRL1_WORD_LENGTH(BV_SSP_CTRL1_WORD_LENGTH__EIGHT_BITS) |
	       ((dev->mode & SPI_CPOL) ? BM_SSP_CTRL1_POLARITY : 0) |
	       ((dev->mode & SPI_CPHA) ? BM_SSP_CTRL1_PHASE : 0),
	       ssp->base + HW_SSP_CTRL1(ssp));

	writel(0x0, ssp->base + HW_SSP_CMD0);
	writel(0x0, ssp->base + HW_SSP_CMD1);

	return 0;
}

static u32 mxs_spi_cs_to_reg(unsigned cs)
{
	u32 select = 0;

	/*
	 * i.MX28 Datasheet: 17.10.1: HW_SSP_CTRL0
	 *
	 * The bits BM_SSP_CTRL0_WAIT_FOR_CMD and BM_SSP_CTRL0_WAIT_FOR_IRQ
	 * in HW_SSP_CTRL0 register do have multiple usage, please refer to
	 * the datasheet for further details. In SPI mode, they are used to
	 * toggle the chip-select lines (nCS pins).
	 */
	if (cs & 1)
		select |= BM_SSP_CTRL0_WAIT_FOR_CMD;
	if (cs & 2)
		select |= BM_SSP_CTRL0_WAIT_FOR_IRQ;

	return select;
}

static int mxs_ssp_wait(struct mxs_spi *spi, int offset, int mask, bool set)
{
	const unsigned long timeout = jiffies + msecs_to_jiffies(SSP_TIMEOUT);
	struct mxs_ssp *ssp = &spi->ssp;
	u32 reg;

	do {
		reg = readl_relaxed(ssp->base + offset);

		if (!set)
			reg = ~reg;

		reg &= mask;

		if (reg == mask)
			return 0;
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

static void mxs_ssp_dma_irq_callback(void *param)
{
	struct mxs_spi *spi = param;
	complete(&spi->c);
}

static irqreturn_t mxs_ssp_irq_handler(int irq, void *dev_id)
{
	struct mxs_ssp *ssp = dev_id;
	dev_err(ssp->dev, "%s[%i] CTRL1=%08x STATUS=%08x\n",
		__func__, __LINE__,
		readl(ssp->base + HW_SSP_CTRL1(ssp)),
		readl(ssp->base + HW_SSP_STATUS(ssp)));
	return IRQ_HANDLED;
}

static int mxs_spi_txrx_dma(struct mxs_spi *spi,
			    unsigned char *buf, int len,
			    unsigned int flags)
{
	struct mxs_ssp *ssp = &spi->ssp;
	struct dma_async_tx_descriptor *desc = NULL;
	const bool vmalloced_buf = is_vmalloc_addr(buf);
	const int desc_len = vmalloced_buf ? PAGE_SIZE : SG_MAXLEN;
	const int sgs = DIV_ROUND_UP(len, desc_len);
	int sg_count;
	int min, ret;
	u32 ctrl0;
	struct page *vm_page;
	void *sg_buf;
	struct {
		u32			pio[4];
		struct scatterlist	sg;
	} *dma_xfer;

	if (!len)
		return -EINVAL;

	dma_xfer = kzalloc(sizeof(*dma_xfer) * sgs, GFP_KERNEL);
	if (!dma_xfer)
		return -ENOMEM;

	reinit_completion(&spi->c);

	/* Chip select was already programmed into CTRL0 */
	ctrl0 = readl(ssp->base + HW_SSP_CTRL0);
	ctrl0 &= ~(BM_SSP_CTRL0_XFER_COUNT | BM_SSP_CTRL0_IGNORE_CRC |
		 BM_SSP_CTRL0_READ);
	ctrl0 |= BM_SSP_CTRL0_DATA_XFER;

	if (!(flags & TXRX_WRITE))
		ctrl0 |= BM_SSP_CTRL0_READ;

	/* Queue the DMA data transfer. */
	for (sg_count = 0; sg_count < sgs; sg_count++) {
		/* Prepare the transfer descriptor. */
		min = min(len, desc_len);

		/*
		 * De-assert CS on last segment if flag is set (i.e., no more
		 * transfers will follow)
		 */
		if ((sg_count + 1 == sgs) && (flags & TXRX_DEASSERT_CS))
			ctrl0 |= BM_SSP_CTRL0_IGNORE_CRC;

		if (ssp->devid == IMX23_SSP) {
			ctrl0 &= ~BM_SSP_CTRL0_XFER_COUNT;
			ctrl0 |= min;
		}

		dma_xfer[sg_count].pio[0] = ctrl0;
		dma_xfer[sg_count].pio[3] = min;

		if (vmalloced_buf) {
			vm_page = vmalloc_to_page(buf);
			if (!vm_page) {
				ret = -ENOMEM;
				goto err_vmalloc;
			}
			sg_buf = page_address(vm_page) +
				((size_t)buf & ~PAGE_MASK);
		} else {
			sg_buf = buf;
		}

		sg_init_one(&dma_xfer[sg_count].sg, sg_buf, min);
		ret = dma_map_sg(ssp->dev, &dma_xfer[sg_count].sg, 1,
			(flags & TXRX_WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

		len -= min;
		buf += min;

		/* Queue the PIO register write transfer. */
		desc = dmaengine_prep_slave_sg(ssp->dmach,
				(struct scatterlist *)dma_xfer[sg_count].pio,
				(ssp->devid == IMX23_SSP) ? 1 : 4,
				DMA_TRANS_NONE,
				sg_count ? DMA_PREP_INTERRUPT : 0);
		if (!desc) {
			dev_err(ssp->dev,
				"Failed to get PIO reg. write descriptor.\n");
			ret = -EINVAL;
			goto err_mapped;
		}

		desc = dmaengine_prep_slave_sg(ssp->dmach,
				&dma_xfer[sg_count].sg, 1,
				(flags & TXRX_WRITE) ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

		if (!desc) {
			dev_err(ssp->dev,
				"Failed to get DMA data write descriptor.\n");
			ret = -EINVAL;
			goto err_mapped;
		}
	}

	/*
	 * The last descriptor must have this callback,
	 * to finish the DMA transaction.
	 */
	desc->callback = mxs_ssp_dma_irq_callback;
	desc->callback_param = spi;

	/* Start the transfer. */
	dmaengine_submit(desc);
	dma_async_issue_pending(ssp->dmach);

	ret = wait_for_completion_timeout(&spi->c,
				msecs_to_jiffies(SSP_TIMEOUT));
	if (!ret) {
		dev_err(ssp->dev, "DMA transfer timeout\n");
		ret = -ETIMEDOUT;
		dmaengine_terminate_all(ssp->dmach);
		goto err_vmalloc;
	}

	ret = 0;

err_vmalloc:
	while (--sg_count >= 0) {
err_mapped:
		dma_unmap_sg(ssp->dev, &dma_xfer[sg_count].sg, 1,
			(flags & TXRX_WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	}

	kfree(dma_xfer);

	return ret;
}

static int mxs_spi_txrx_pio(struct mxs_spi *spi,
			    unsigned char *buf, int len,
			    unsigned int flags)
{
	struct mxs_ssp *ssp = &spi->ssp;

	writel(BM_SSP_CTRL0_IGNORE_CRC,
	       ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_CLR);

	while (len--) {
		if (len == 0 && (flags & TXRX_DEASSERT_CS))
			writel(BM_SSP_CTRL0_IGNORE_CRC,
			       ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_SET);

		if (ssp->devid == IMX23_SSP) {
			writel(BM_SSP_CTRL0_XFER_COUNT,
				ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_CLR);
			writel(1,
				ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_SET);
		} else {
			writel(1, ssp->base + HW_SSP_XFER_SIZE);
		}

		if (flags & TXRX_WRITE)
			writel(BM_SSP_CTRL0_READ,
				ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_CLR);
		else
			writel(BM_SSP_CTRL0_READ,
				ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_SET);

		writel(BM_SSP_CTRL0_RUN,
				ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_SET);

		if (mxs_ssp_wait(spi, HW_SSP_CTRL0, BM_SSP_CTRL0_RUN, 1))
			return -ETIMEDOUT;

		if (flags & TXRX_WRITE)
			writel(*buf, ssp->base + HW_SSP_DATA(ssp));

		writel(BM_SSP_CTRL0_DATA_XFER,
			     ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_SET);

		if (!(flags & TXRX_WRITE)) {
			if (mxs_ssp_wait(spi, HW_SSP_STATUS(ssp),
						BM_SSP_STATUS_FIFO_EMPTY, 0))
				return -ETIMEDOUT;

			*buf = (readl(ssp->base + HW_SSP_DATA(ssp)) & 0xff);
		}

		if (mxs_ssp_wait(spi, HW_SSP_CTRL0, BM_SSP_CTRL0_RUN, 0))
			return -ETIMEDOUT;

		buf++;
	}

	if (len <= 0)
		return 0;

	return -ETIMEDOUT;
}

static int mxs_spi_transfer_one(struct spi_master *master,
				struct spi_message *m)
{
	struct mxs_spi *spi = spi_master_get_devdata(master);
	struct mxs_ssp *ssp = &spi->ssp;
	struct spi_transfer *t, *tmp_t;
	unsigned int flag;
	int status = 0;

	/* Program CS register bits here, it will be used for all transfers. */
	writel(BM_SSP_CTRL0_WAIT_FOR_CMD | BM_SSP_CTRL0_WAIT_FOR_IRQ,
	       ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_CLR);
	writel(mxs_spi_cs_to_reg(m->spi->chip_select),
	       ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_SET);

	list_for_each_entry_safe(t, tmp_t, &m->transfers, transfer_list) {

		status = mxs_spi_setup_transfer(m->spi, t);
		if (status)
			break;

		/* De-assert on last transfer, inverted by cs_change flag */
		flag = (&t->transfer_list == m->transfers.prev) ^ t->cs_change ?
		       TXRX_DEASSERT_CS : 0;

		/*
		 * Small blocks can be transfered via PIO.
		 * Measured by empiric means:
		 *
		 * dd if=/dev/mtdblock0 of=/dev/null bs=1024k count=1
		 *
		 * DMA only: 2.164808 seconds, 473.0KB/s
		 * Combined: 1.676276 seconds, 610.9KB/s
		 */
		if (t->len < 32) {
			writel(BM_SSP_CTRL1_DMA_ENABLE,
				ssp->base + HW_SSP_CTRL1(ssp) +
				STMP_OFFSET_REG_CLR);

			if (t->tx_buf)
				status = mxs_spi_txrx_pio(spi,
						(void *)t->tx_buf,
						t->len, flag | TXRX_WRITE);
			if (t->rx_buf)
				status = mxs_spi_txrx_pio(spi,
						t->rx_buf, t->len,
						flag);
		} else {
			writel(BM_SSP_CTRL1_DMA_ENABLE,
				ssp->base + HW_SSP_CTRL1(ssp) +
				STMP_OFFSET_REG_SET);

			if (t->tx_buf)
				status = mxs_spi_txrx_dma(spi,
						(void *)t->tx_buf, t->len,
						flag | TXRX_WRITE);
			if (t->rx_buf)
				status = mxs_spi_txrx_dma(spi,
						t->rx_buf, t->len,
						flag);
		}

		if (status) {
			stmp_reset_block(ssp->base);
			break;
		}

		m->actual_length += t->len;
	}

	m->status = status;
	spi_finalize_current_message(master);

	return status;
}

static const struct of_device_id mxs_spi_dt_ids[] = {
	{ .compatible = "fsl,imx23-spi", .data = (void *) IMX23_SSP, },
	{ .compatible = "fsl,imx28-spi", .data = (void *) IMX28_SSP, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_spi_dt_ids);

static int mxs_spi_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(mxs_spi_dt_ids, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	struct spi_master *master;
	struct mxs_spi *spi;
	struct mxs_ssp *ssp;
	struct resource *iores;
	struct clk *clk;
	void __iomem *base;
	int devid, clk_freq;
	int ret = 0, irq_err;

	/*
	 * Default clock speed for the SPI core. 160MHz seems to
	 * work reasonably well with most SPI flashes, so use this
	 * as a default. Override with "clock-frequency" DT prop.
	 */
	const int clk_freq_default = 160000000;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_err = platform_get_irq(pdev, 0);
	if (irq_err < 0)
		return -EINVAL;

	base = devm_ioremap_resource(&pdev->dev, iores);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	devid = (enum mxs_ssp_id) of_id->data;
	ret = of_property_read_u32(np, "clock-frequency",
				   &clk_freq);
	if (ret)
		clk_freq = clk_freq_default;

	master = spi_alloc_master(&pdev->dev, sizeof(*spi));
	if (!master)
		return -ENOMEM;

	master->transfer_one_message = mxs_spi_transfer_one;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->mode_bits = SPI_CPOL | SPI_CPHA;
	master->num_chipselect = 3;
	master->dev.of_node = np;
	master->flags = SPI_MASTER_HALF_DUPLEX;

	spi = spi_master_get_devdata(master);
	ssp = &spi->ssp;
	ssp->dev = &pdev->dev;
	ssp->clk = clk;
	ssp->base = base;
	ssp->devid = devid;

	init_completion(&spi->c);

	ret = devm_request_irq(&pdev->dev, irq_err, mxs_ssp_irq_handler, 0,
			       DRIVER_NAME, ssp);
	if (ret)
		goto out_master_free;

	ssp->dmach = dma_request_slave_channel(&pdev->dev, "rx-tx");
	if (!ssp->dmach) {
		dev_err(ssp->dev, "Failed to request DMA\n");
		ret = -ENODEV;
		goto out_master_free;
	}

	ret = clk_prepare_enable(ssp->clk);
	if (ret)
		goto out_dma_release;

	clk_set_rate(ssp->clk, clk_freq);

	ret = stmp_reset_block(ssp->base);
	if (ret)
		goto out_disable_clk;

	platform_set_drvdata(pdev, master);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret) {
		dev_err(&pdev->dev, "Cannot register SPI master, %d\n", ret);
		goto out_disable_clk;
	}

	return 0;

out_disable_clk:
	clk_disable_unprepare(ssp->clk);
out_dma_release:
	dma_release_channel(ssp->dmach);
out_master_free:
	spi_master_put(master);
	return ret;
}

static int mxs_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master;
	struct mxs_spi *spi;
	struct mxs_ssp *ssp;

	master = platform_get_drvdata(pdev);
	spi = spi_master_get_devdata(master);
	ssp = &spi->ssp;

	clk_disable_unprepare(ssp->clk);
	dma_release_channel(ssp->dmach);

	return 0;
}

static struct platform_driver mxs_spi_driver = {
	.probe	= mxs_spi_probe,
	.remove	= mxs_spi_remove,
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = mxs_spi_dt_ids,
	},
};

module_platform_driver(mxs_spi_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("MXS SPI master driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mxs-spi");
