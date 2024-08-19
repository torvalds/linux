// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom BCM63xx SPI controller support
 *
 * Copyright (C) 2009-2012 Florian Fainelli <florian@openwrt.org>
 * Copyright (C) 2010 Tanguy Bouzeloc <tanguy.bouzeloc@efixo.com>
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
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/reset.h>

/* BCM 6338/6348 SPI core */
#define SPI_6348_RSET_SIZE		64
#define SPI_6348_CMD			0x00	/* 16-bits register */
#define SPI_6348_INT_STATUS		0x02
#define SPI_6348_INT_MASK_ST		0x03
#define SPI_6348_INT_MASK		0x04
#define SPI_6348_ST			0x05
#define SPI_6348_CLK_CFG		0x06
#define SPI_6348_FILL_BYTE		0x07
#define SPI_6348_MSG_TAIL		0x09
#define SPI_6348_RX_TAIL		0x0b
#define SPI_6348_MSG_CTL		0x40	/* 8-bits register */
#define SPI_6348_MSG_CTL_WIDTH		8
#define SPI_6348_MSG_DATA		0x41
#define SPI_6348_MSG_DATA_SIZE		0x3f
#define SPI_6348_RX_DATA		0x80
#define SPI_6348_RX_DATA_SIZE		0x3f

/* BCM 3368/6358/6262/6368 SPI core */
#define SPI_6358_RSET_SIZE		1804
#define SPI_6358_MSG_CTL		0x00	/* 16-bits register */
#define SPI_6358_MSG_CTL_WIDTH		16
#define SPI_6358_MSG_DATA		0x02
#define SPI_6358_MSG_DATA_SIZE		0x21e
#define SPI_6358_RX_DATA		0x400
#define SPI_6358_RX_DATA_SIZE		0x220
#define SPI_6358_CMD			0x700	/* 16-bits register */
#define SPI_6358_INT_STATUS		0x702
#define SPI_6358_INT_MASK_ST		0x703
#define SPI_6358_INT_MASK		0x704
#define SPI_6358_ST			0x705
#define SPI_6358_CLK_CFG		0x706
#define SPI_6358_FILL_BYTE		0x707
#define SPI_6358_MSG_TAIL		0x709
#define SPI_6358_RX_TAIL		0x70B

/* Shared SPI definitions */

/* Message configuration */
#define SPI_FD_RW			0x00
#define SPI_HD_W			0x01
#define SPI_HD_R			0x02
#define SPI_BYTE_CNT_SHIFT		0
#define SPI_6348_MSG_TYPE_SHIFT		6
#define SPI_6358_MSG_TYPE_SHIFT		14

/* Command */
#define SPI_CMD_NOOP			0x00
#define SPI_CMD_SOFT_RESET		0x01
#define SPI_CMD_HARD_RESET		0x02
#define SPI_CMD_START_IMMEDIATE		0x03
#define SPI_CMD_COMMAND_SHIFT		0
#define SPI_CMD_COMMAND_MASK		0x000f
#define SPI_CMD_DEVICE_ID_SHIFT		4
#define SPI_CMD_PREPEND_BYTE_CNT_SHIFT	8
#define SPI_CMD_ONE_BYTE_SHIFT		11
#define SPI_CMD_ONE_WIRE_SHIFT		12
#define SPI_DEV_ID_0			0
#define SPI_DEV_ID_1			1
#define SPI_DEV_ID_2			2
#define SPI_DEV_ID_3			3

/* Interrupt mask */
#define SPI_INTR_CMD_DONE		0x01
#define SPI_INTR_RX_OVERFLOW		0x02
#define SPI_INTR_TX_UNDERFLOW		0x04
#define SPI_INTR_TX_OVERFLOW		0x08
#define SPI_INTR_RX_UNDERFLOW		0x10
#define SPI_INTR_CLEAR_ALL		0x1f

/* Status */
#define SPI_RX_EMPTY			0x02
#define SPI_CMD_BUSY			0x04
#define SPI_SERIAL_BUSY			0x08

/* Clock configuration */
#define SPI_CLK_20MHZ			0x00
#define SPI_CLK_0_391MHZ		0x01
#define SPI_CLK_0_781MHZ		0x02	/* default */
#define SPI_CLK_1_563MHZ		0x03
#define SPI_CLK_3_125MHZ		0x04
#define SPI_CLK_6_250MHZ		0x05
#define SPI_CLK_12_50MHZ		0x06
#define SPI_CLK_MASK			0x07
#define SPI_SSOFFTIME_MASK		0x38
#define SPI_SSOFFTIME_SHIFT		3
#define SPI_BYTE_SWAP			0x80

enum bcm63xx_regs_spi {
	SPI_CMD,
	SPI_INT_STATUS,
	SPI_INT_MASK_ST,
	SPI_INT_MASK,
	SPI_ST,
	SPI_CLK_CFG,
	SPI_FILL_BYTE,
	SPI_MSG_TAIL,
	SPI_RX_TAIL,
	SPI_MSG_CTL,
	SPI_MSG_DATA,
	SPI_RX_DATA,
	SPI_MSG_TYPE_SHIFT,
	SPI_MSG_CTL_WIDTH,
	SPI_MSG_DATA_SIZE,
};

#define BCM63XX_SPI_MAX_PREPEND		7

#define BCM63XX_SPI_MAX_CS		8
#define BCM63XX_SPI_BUS_NUM		0

struct bcm63xx_spi {
	struct completion	done;

	void __iomem		*regs;
	int			irq;

	/* Platform data */
	const unsigned long	*reg_offsets;
	unsigned int		fifo_size;
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
	return readb(bs->regs + bs->reg_offsets[offset]);
}

static inline u16 bcm_spi_readw(struct bcm63xx_spi *bs,
				unsigned int offset)
{
#ifdef CONFIG_CPU_BIG_ENDIAN
	return ioread16be(bs->regs + bs->reg_offsets[offset]);
#else
	return readw(bs->regs + bs->reg_offsets[offset]);
#endif
}

static inline void bcm_spi_writeb(struct bcm63xx_spi *bs,
				  u8 value, unsigned int offset)
{
	writeb(value, bs->regs + bs->reg_offsets[offset]);
}

static inline void bcm_spi_writew(struct bcm63xx_spi *bs,
				  u16 value, unsigned int offset)
{
#ifdef CONFIG_CPU_BIG_ENDIAN
	iowrite16be(value, bs->regs + bs->reg_offsets[offset]);
#else
	writew(value, bs->regs + bs->reg_offsets[offset]);
#endif
}

static const unsigned int bcm63xx_spi_freq_table[SPI_CLK_MASK][2] = {
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

	/* Default to lowest clock configuration */
	clk_cfg = SPI_CLK_0_391MHZ;

	/* Find the closest clock configuration */
	for (i = 0; i < SPI_CLK_MASK; i++) {
		if (t->speed_hz >= bcm63xx_spi_freq_table[i][0]) {
			clk_cfg = bcm63xx_spi_freq_table[i][1];
			break;
		}
	}

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
		if (t->delay.value) {
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

static size_t bcm63xx_spi_max_length(struct spi_device *spi)
{
	struct bcm63xx_spi *bs = spi_master_get_devdata(spi->master);

	return bs->fifo_size;
}

static const unsigned long bcm6348_spi_reg_offsets[] = {
	[SPI_CMD]		= SPI_6348_CMD,
	[SPI_INT_STATUS]	= SPI_6348_INT_STATUS,
	[SPI_INT_MASK_ST]	= SPI_6348_INT_MASK_ST,
	[SPI_INT_MASK]		= SPI_6348_INT_MASK,
	[SPI_ST]		= SPI_6348_ST,
	[SPI_CLK_CFG]		= SPI_6348_CLK_CFG,
	[SPI_FILL_BYTE]		= SPI_6348_FILL_BYTE,
	[SPI_MSG_TAIL]		= SPI_6348_MSG_TAIL,
	[SPI_RX_TAIL]		= SPI_6348_RX_TAIL,
	[SPI_MSG_CTL]		= SPI_6348_MSG_CTL,
	[SPI_MSG_DATA]		= SPI_6348_MSG_DATA,
	[SPI_RX_DATA]		= SPI_6348_RX_DATA,
	[SPI_MSG_TYPE_SHIFT]	= SPI_6348_MSG_TYPE_SHIFT,
	[SPI_MSG_CTL_WIDTH]	= SPI_6348_MSG_CTL_WIDTH,
	[SPI_MSG_DATA_SIZE]	= SPI_6348_MSG_DATA_SIZE,
};

static const unsigned long bcm6358_spi_reg_offsets[] = {
	[SPI_CMD]		= SPI_6358_CMD,
	[SPI_INT_STATUS]	= SPI_6358_INT_STATUS,
	[SPI_INT_MASK_ST]	= SPI_6358_INT_MASK_ST,
	[SPI_INT_MASK]		= SPI_6358_INT_MASK,
	[SPI_ST]		= SPI_6358_ST,
	[SPI_CLK_CFG]		= SPI_6358_CLK_CFG,
	[SPI_FILL_BYTE]		= SPI_6358_FILL_BYTE,
	[SPI_MSG_TAIL]		= SPI_6358_MSG_TAIL,
	[SPI_RX_TAIL]		= SPI_6358_RX_TAIL,
	[SPI_MSG_CTL]		= SPI_6358_MSG_CTL,
	[SPI_MSG_DATA]		= SPI_6358_MSG_DATA,
	[SPI_RX_DATA]		= SPI_6358_RX_DATA,
	[SPI_MSG_TYPE_SHIFT]	= SPI_6358_MSG_TYPE_SHIFT,
	[SPI_MSG_CTL_WIDTH]	= SPI_6358_MSG_CTL_WIDTH,
	[SPI_MSG_DATA_SIZE]	= SPI_6358_MSG_DATA_SIZE,
};

static const struct platform_device_id bcm63xx_spi_dev_match[] = {
	{
		.name = "bcm6348-spi",
		.driver_data = (unsigned long)bcm6348_spi_reg_offsets,
	},
	{
		.name = "bcm6358-spi",
		.driver_data = (unsigned long)bcm6358_spi_reg_offsets,
	},
	{
	},
};
MODULE_DEVICE_TABLE(platform, bcm63xx_spi_dev_match);

static const struct of_device_id bcm63xx_spi_of_match[] = {
	{ .compatible = "brcm,bcm6348-spi", .data = &bcm6348_spi_reg_offsets },
	{ .compatible = "brcm,bcm6358-spi", .data = &bcm6358_spi_reg_offsets },
	{ },
};
MODULE_DEVICE_TABLE(of, bcm63xx_spi_of_match);

static int bcm63xx_spi_probe(struct platform_device *pdev)
{
	struct resource *r;
	const unsigned long *bcm63xx_spireg;
	struct device *dev = &pdev->dev;
	int irq, bus_num;
	struct spi_master *master;
	struct clk *clk;
	struct bcm63xx_spi *bs;
	int ret;
	u32 num_cs = BCM63XX_SPI_MAX_CS;
	struct reset_control *reset;

	if (dev->of_node) {
		const struct of_device_id *match;

		match = of_match_node(bcm63xx_spi_of_match, dev->of_node);
		if (!match)
			return -EINVAL;
		bcm63xx_spireg = match->data;

		of_property_read_u32(dev->of_node, "num-cs", &num_cs);
		if (num_cs > BCM63XX_SPI_MAX_CS) {
			dev_warn(dev, "unsupported number of cs (%i), reducing to 8\n",
				 num_cs);
			num_cs = BCM63XX_SPI_MAX_CS;
		}

		bus_num = -1;
	} else if (pdev->id_entry->driver_data) {
		const struct platform_device_id *match = pdev->id_entry;

		bcm63xx_spireg = (const unsigned long *)match->driver_data;
		bus_num = BCM63XX_SPI_BUS_NUM;
	} else {
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	clk = devm_clk_get(dev, "spi");
	if (IS_ERR(clk)) {
		dev_err(dev, "no clock for device\n");
		return PTR_ERR(clk);
	}

	reset = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

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
	bs->reg_offsets = bcm63xx_spireg;
	bs->fifo_size = bs->reg_offsets[SPI_MSG_DATA_SIZE];

	ret = devm_request_irq(&pdev->dev, irq, bcm63xx_spi_interrupt, 0,
							pdev->name, master);
	if (ret) {
		dev_err(dev, "unable to request irq\n");
		goto out_err;
	}

	master->dev.of_node = dev->of_node;
	master->bus_num = bus_num;
	master->num_chipselect = num_cs;
	master->transfer_one_message = bcm63xx_spi_transfer_one;
	master->mode_bits = MODEBITS;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->max_transfer_size = bcm63xx_spi_max_length;
	master->max_message_size = bcm63xx_spi_max_length;
	master->auto_runtime_pm = true;
	bs->msg_type_shift = bs->reg_offsets[SPI_MSG_TYPE_SHIFT];
	bs->msg_ctl_width = bs->reg_offsets[SPI_MSG_CTL_WIDTH];
	bs->tx_io = (u8 *)(bs->regs + bs->reg_offsets[SPI_MSG_DATA]);
	bs->rx_io = (const u8 *)(bs->regs + bs->reg_offsets[SPI_RX_DATA]);

	/* Initialize hardware */
	ret = clk_prepare_enable(bs->clk);
	if (ret)
		goto out_err;

	ret = reset_control_reset(reset);
	if (ret) {
		dev_err(dev, "unable to reset device: %d\n", ret);
		goto out_clk_disable;
	}

	bcm_spi_writeb(bs, SPI_INTR_CLEAR_ALL, SPI_INT_STATUS);

	pm_runtime_enable(&pdev->dev);

	/* register and we are done */
	ret = devm_spi_register_master(dev, master);
	if (ret) {
		dev_err(dev, "spi register failed\n");
		goto out_pm_disable;
	}

	dev_info(dev, "at %pr (irq %d, FIFOs size %d)\n",
		 r, irq, bs->fifo_size);

	return 0;

out_pm_disable:
	pm_runtime_disable(&pdev->dev);
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
		.pm	= &bcm63xx_spi_pm_ops,
		.of_match_table = bcm63xx_spi_of_match,
	},
	.id_table	= bcm63xx_spi_dev_match,
	.probe		= bcm63xx_spi_probe,
	.remove		= bcm63xx_spi_remove,
};

module_platform_driver(bcm63xx_spi_driver);

MODULE_ALIAS("platform:bcm63xx_spi");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_AUTHOR("Tanguy Bouzeloc <tanguy.bouzeloc@efixo.com>");
MODULE_DESCRIPTION("Broadcom BCM63xx SPI Controller driver");
MODULE_LICENSE("GPL");
