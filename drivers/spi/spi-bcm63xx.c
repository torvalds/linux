/*
 * Broadcom BCM63xx SPI controller support
 *
 * Copyright (C) 2009-2012 Florian Fainelli <florian@openwrt.org>
 * Copyright (C) 2010 Tanguy Bouzeloc <tanguy.bouzeloc@efixo.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>

#include <bcm63xx_dev_spi.h>

#define BCM63XX_SPI_MAX_PREPEND		15

struct bcm63xx_spi {
	struct completion	done;

	void __iomem		*regs;
	int			irq;

	/* Platform data */
	unsigned		fifo_size;
	unsigned int		msg_type_shift;
	unsigned int		msg_ctl_width;

	/* data iomem */
	u8 __iomem		*tx_io;
	const u8 __iomem	*rx_io;

	struct clk		*clk;
	struct platform_device	*pdev;
};

static inline u8 bcm_spi_readb(struct bcm63xx_spi *bs,
				unsigned int offset)
{
	return bcm_readb(bs->regs + bcm63xx_spireg(offset));
}

static inline u16 bcm_spi_readw(struct bcm63xx_spi *bs,
				unsigned int offset)
{
	return bcm_readw(bs->regs + bcm63xx_spireg(offset));
}

static inline void bcm_spi_writeb(struct bcm63xx_spi *bs,
				  u8 value, unsigned int offset)
{
	bcm_writeb(value, bs->regs + bcm63xx_spireg(offset));
}

static inline void bcm_spi_writew(struct bcm63xx_spi *bs,
				  u16 value, unsigned int offset)
{
	bcm_writew(value, bs->regs + bcm63xx_spireg(offset));
}

static const unsigned bcm63xx_spi_freq_table[SPI_CLK_MASK][2] = {
	{ 20000000, SPI_CLK_20MHZ },
	{ 12500000, SPI_CLK_12_50MHZ },
	{  6250000, SPI_CLK_6_250MHZ },
	{  3125000, SPI_CLK_3_125MHZ },
	{  1563000, SPI_CLK_1_563MHZ },
	{   781000, SPI_CLK_0_781MHZ },
	{   391000, SPI_CLK_0_391MHZ }
};

static void bcm63xx_spi_setup_transfer(struct spi_device *spi,
				      struct spi_transfer *t)
{
	struct bcm63xx_spi *bs = spi_master_get_devdata(spi->master);
	u8 clk_cfg, reg;
	int i;

	/* Find the closest clock configuration */
	for (i = 0; i < SPI_CLK_MASK; i++) {
		if (t->speed_hz >= bcm63xx_spi_freq_table[i][0]) {
			clk_cfg = bcm63xx_spi_freq_table[i][1];
			break;
		}
	}

	/* No matching configuration found, default to lowest */
	if (i == SPI_CLK_MASK)
		clk_cfg = SPI_CLK_0_391MHZ;

	/* clear existing clock configuration bits of the register */
	reg = bcm_spi_readb(bs, SPI_CLK_CFG);
	reg &= ~SPI_CLK_MASK;
	reg |= clk_cfg;

	bcm_spi_writeb(bs, reg, SPI_CLK_CFG);
	dev_dbg(&spi->dev, "Setting clock register to %02x (hz %d)\n",
		clk_cfg, t->speed_hz);
}

/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_CPOL | SPI_CPHA)

static int bcm63xx_txrx_bufs(struct spi_device *spi, struct spi_transfer *first,
				unsigned int num_transfers)
{
	struct bcm63xx_spi *bs = spi_master_get_devdata(spi->master);
	u16 msg_ctl;
	u16 cmd;
	u8 rx_tail;
	unsigned int i, timeout = 0, prepend_len = 0, len = 0;
	struct spi_transfer *t = first;
	bool do_rx = false;
	bool do_tx = false;

	/* Disable the CMD_DONE interrupt */
	bcm_spi_writeb(bs, 0, SPI_INT_MASK);

	dev_dbg(&spi->dev, "txrx: tx %p, rx %p, len %d\n",
		t->tx_buf, t->rx_buf, t->len);

	if (num_transfers > 1 && t->tx_buf && t->len <= BCM63XX_SPI_MAX_PREPEND)
		prepend_len = t->len;

	/* prepare the buffer */
	for (i = 0; i < num_transfers; i++) {
		if (t->tx_buf) {
			do_tx = true;
			memcpy_toio(bs->tx_io + len, t->tx_buf, t->len);

			/* don't prepend more than one tx */
			if (t != first)
				prepend_len = 0;
		}

		if (t->rx_buf) {
			do_rx = true;
			/* prepend is half-duplex write only */
			if (t == first)
				prepend_len = 0;
		}

		len += t->len;

		t = list_entry(t->transfer_list.next, struct spi_transfer,
			       transfer_list);
	}

	reinit_completion(&bs->done);

	/* Fill in the Message control register */
	msg_ctl = (len << SPI_BYTE_CNT_SHIFT);

	if (do_rx && do_tx && prepend_len == 0)
		msg_ctl |= (SPI_FD_RW << bs->msg_type_shift);
	else if (do_rx)
		msg_ctl |= (SPI_HD_R << bs->msg_type_shift);
	else if (do_tx)
		msg_ctl |= (SPI_HD_W << bs->msg_type_shift);

	switch (bs->msg_ctl_width) {
	case 8:
		bcm_spi_writeb(bs, msg_ctl, SPI_MSG_CTL);
		break;
	case 16:
		bcm_spi_writew(bs, msg_ctl, SPI_MSG_CTL);
		break;
	}

	/* Issue the transfer */
	cmd = SPI_CMD_START_IMMEDIATE;
	cmd |= (prepend_len << SPI_CMD_PREPEND_BYTE_CNT_SHIFT);
	cmd |= (spi->chip_select << SPI_CMD_DEVICE_ID_SHIFT);
	bcm_spi_writew(bs, cmd, SPI_CMD);

	/* Enable the CMD_DONE interrupt */
	bcm_spi_writeb(bs, SPI_INTR_CMD_DONE, SPI_INT_MASK);

	timeout = wait_for_completion_timeout(&bs->done, HZ);
	if (!timeout)
		return -ETIMEDOUT;

	if (!do_rx)
		return 0;

	len = 0;
	t = first;
	/* Read out all the data */
	for (i = 0; i < num_transfers; i++) {
		if (t->rx_buf)
			memcpy_fromio(t->rx_buf, bs->rx_io + len, t->len);

		if (t != first || prepend_len == 0)
			len += t->len;

		t = list_entry(t->transfer_list.next, struct spi_transfer,
			       transfer_list);
	}

	return 0;
}

static int bcm63xx_spi_transfer_one(struct spi_master *master,
					struct spi_message *m)
{
	struct bcm63xx_spi *bs = spi_master_get_devdata(master);
	struct spi_transfer *t, *first = NULL;
	struct spi_device *spi = m->spi;
	int status = 0;
	unsigned int n_transfers = 0, total_len = 0;
	bool can_use_prepend = false;

	/*
	 * This SPI controller does not support keeping CS active after a
	 * transfer.
	 * Work around this by merging as many transfers we can into one big
	 * full-duplex transfers.
	 */
	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (!first)
			first = t;

		n_transfers++;
		total_len += t->len;

		if (n_transfers == 2 && !first->rx_buf && !t->tx_buf &&
		    first->len <= BCM63XX_SPI_MAX_PREPEND)
			can_use_prepend = true;
		else if (can_use_prepend && t->tx_buf)
			can_use_prepend = false;

		/* we can only transfer one fifo worth of data */
		if ((can_use_prepend &&
		     total_len > (bs->fifo_size + BCM63XX_SPI_MAX_PREPEND)) ||
		    (!can_use_prepend && total_len > bs->fifo_size)) {
			dev_err(&spi->dev, "unable to do transfers larger than FIFO size (%i > %i)\n",
				total_len, bs->fifo_size);
			status = -EINVAL;
			goto exit;
		}

		/* all combined transfers have to have the same speed */
		if (t->speed_hz != first->speed_hz) {
			dev_err(&spi->dev, "unable to change speed between transfers\n");
			status = -EINVAL;
			goto exit;
		}

		/* CS will be deasserted directly after transfer */
		if (t->delay_usecs) {
			dev_err(&spi->dev, "unable to keep CS asserted after transfer\n");
			status = -EINVAL;
			goto exit;
		}

		if (t->cs_change ||
		    list_is_last(&t->transfer_list, &m->transfers)) {
			/* configure adapter for a new transfer */
			bcm63xx_spi_setup_transfer(spi, first);

			/* send the data */
			status = bcm63xx_txrx_bufs(spi, first, n_transfers);
			if (status)
				goto exit;

			m->actual_length += total_len;

			first = NULL;
			n_transfers = 0;
			total_len = 0;
			can_use_prepend = false;
		}
	}
exit:
	m->status = status;
	spi_finalize_current_message(master);

	return 0;
}

/* This driver supports single master mode only. Hence
 * CMD_DONE is the only interrupt we care about
 */
static irqreturn_t bcm63xx_spi_interrupt(int irq, void *dev_id)
{
	struct spi_master *master = (struct spi_master *)dev_id;
	struct bcm63xx_spi *bs = spi_master_get_devdata(master);
	u8 intr;

	/* Read interupts and clear them immediately */
	intr = bcm_spi_readb(bs, SPI_INT_STATUS);
	bcm_spi_writeb(bs, SPI_INTR_CLEAR_ALL, SPI_INT_STATUS);
	bcm_spi_writeb(bs, 0, SPI_INT_MASK);

	/* A transfer completed */
	if (intr & SPI_INTR_CMD_DONE)
		complete(&bs->done);

	return IRQ_HANDLED;
}


static int bcm63xx_spi_probe(struct platform_device *pdev)
{
	struct resource *r;
	struct device *dev = &pdev->dev;
	struct bcm63xx_spi_pdata *pdata = dev_get_platdata(&pdev->dev);
	int irq;
	struct spi_master *master;
	struct clk *clk;
	struct bcm63xx_spi *bs;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq\n");
		return -ENXIO;
	}

	clk = devm_clk_get(dev, "spi");
	if (IS_ERR(clk)) {
		dev_err(dev, "no clock for device\n");
		return PTR_ERR(clk);
	}

	master = spi_alloc_master(dev, sizeof(*bs));
	if (!master) {
		dev_err(dev, "out of memory\n");
		return -ENOMEM;
	}

	bs = spi_master_get_devdata(master);
	init_completion(&bs->done);

	platform_set_drvdata(pdev, master);
	bs->pdev = pdev;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bs->regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(bs->regs)) {
		ret = PTR_ERR(bs->regs);
		goto out_err;
	}

	bs->irq = irq;
	bs->clk = clk;
	bs->fifo_size = pdata->fifo_size;

	ret = devm_request_irq(&pdev->dev, irq, bcm63xx_spi_interrupt, 0,
							pdev->name, master);
	if (ret) {
		dev_err(dev, "unable to request irq\n");
		goto out_err;
	}

	master->bus_num = pdata->bus_num;
	master->num_chipselect = pdata->num_chipselect;
	master->transfer_one_message = bcm63xx_spi_transfer_one;
	master->mode_bits = MODEBITS;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->auto_runtime_pm = true;
	bs->msg_type_shift = pdata->msg_type_shift;
	bs->msg_ctl_width = pdata->msg_ctl_width;
	bs->tx_io = (u8 *)(bs->regs + bcm63xx_spireg(SPI_MSG_DATA));
	bs->rx_io = (const u8 *)(bs->regs + bcm63xx_spireg(SPI_RX_DATA));

	switch (bs->msg_ctl_width) {
	case 8:
	case 16:
		break;
	default:
		dev_err(dev, "unsupported MSG_CTL width: %d\n",
			 bs->msg_ctl_width);
		goto out_err;
	}

	/* Initialize hardware */
	ret = clk_prepare_enable(bs->clk);
	if (ret)
		goto out_err;

	bcm_spi_writeb(bs, SPI_INTR_CLEAR_ALL, SPI_INT_STATUS);

	/* register and we are done */
	ret = devm_spi_register_master(dev, master);
	if (ret) {
		dev_err(dev, "spi register failed\n");
		goto out_clk_disable;
	}

	dev_info(dev, "at 0x%08x (irq %d, FIFOs size %d)\n",
		 r->start, irq, bs->fifo_size);

	return 0;

out_clk_disable:
	clk_disable_unprepare(clk);
out_err:
	spi_master_put(master);
	return ret;
}

static int bcm63xx_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct bcm63xx_spi *bs = spi_master_get_devdata(master);

	/* reset spi block */
	bcm_spi_writeb(bs, 0, SPI_INT_MASK);

	/* HW shutdown */
	clk_disable_unprepare(bs->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bcm63xx_spi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct bcm63xx_spi *bs = spi_master_get_devdata(master);

	spi_master_suspend(master);

	clk_disable_unprepare(bs->clk);

	return 0;
}

static int bcm63xx_spi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct bcm63xx_spi *bs = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(bs->clk);
	if (ret)
		return ret;

	spi_master_resume(master);

	return 0;
}
#endif

static const struct dev_pm_ops bcm63xx_spi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bcm63xx_spi_suspend, bcm63xx_spi_resume)
};

static struct platform_driver bcm63xx_spi_driver = {
	.driver = {
		.name	= "bcm63xx-spi",
		.owner	= THIS_MODULE,
		.pm	= &bcm63xx_spi_pm_ops,
	},
	.probe		= bcm63xx_spi_probe,
	.remove		= bcm63xx_spi_remove,
};

module_platform_driver(bcm63xx_spi_driver);

MODULE_ALIAS("platform:bcm63xx_spi");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_AUTHOR("Tanguy Bouzeloc <tanguy.bouzeloc@efixo.com>");
MODULE_DESCRIPTION("Broadcom BCM63xx SPI Controller driver");
MODULE_LICENSE("GPL");
