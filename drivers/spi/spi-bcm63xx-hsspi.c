/*
 * Broadcom BCM63XX High Speed SPI Controller driver
 *
 * Copyright 2000-2010 Broadcom Corporation
 * Copyright 2012-2013 Jonas Gorski <jogo@openwrt.org>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/reset.h>

#define HSSPI_GLOBAL_CTRL_REG			0x0
#define GLOBAL_CTRL_CS_POLARITY_SHIFT		0
#define GLOBAL_CTRL_CS_POLARITY_MASK		0x000000ff
#define GLOBAL_CTRL_PLL_CLK_CTRL_SHIFT		8
#define GLOBAL_CTRL_PLL_CLK_CTRL_MASK		0x0000ff00
#define GLOBAL_CTRL_CLK_GATE_SSOFF		BIT(16)
#define GLOBAL_CTRL_CLK_POLARITY		BIT(17)
#define GLOBAL_CTRL_MOSI_IDLE			BIT(18)

#define HSSPI_GLOBAL_EXT_TRIGGER_REG		0x4

#define HSSPI_INT_STATUS_REG			0x8
#define HSSPI_INT_STATUS_MASKED_REG		0xc
#define HSSPI_INT_MASK_REG			0x10

#define HSSPI_PINGx_CMD_DONE(i)			BIT((i * 8) + 0)
#define HSSPI_PINGx_RX_OVER(i)			BIT((i * 8) + 1)
#define HSSPI_PINGx_TX_UNDER(i)			BIT((i * 8) + 2)
#define HSSPI_PINGx_POLL_TIMEOUT(i)		BIT((i * 8) + 3)
#define HSSPI_PINGx_CTRL_INVAL(i)		BIT((i * 8) + 4)

#define HSSPI_INT_CLEAR_ALL			0xff001f1f

#define HSSPI_PINGPONG_COMMAND_REG(x)		(0x80 + (x) * 0x40)
#define PINGPONG_CMD_COMMAND_MASK		0xf
#define PINGPONG_COMMAND_NOOP			0
#define PINGPONG_COMMAND_START_NOW		1
#define PINGPONG_COMMAND_START_TRIGGER		2
#define PINGPONG_COMMAND_HALT			3
#define PINGPONG_COMMAND_FLUSH			4
#define PINGPONG_CMD_PROFILE_SHIFT		8
#define PINGPONG_CMD_SS_SHIFT			12

#define HSSPI_PINGPONG_STATUS_REG(x)		(0x84 + (x) * 0x40)

#define HSSPI_PROFILE_CLK_CTRL_REG(x)		(0x100 + (x) * 0x20)
#define CLK_CTRL_FREQ_CTRL_MASK			0x0000ffff
#define CLK_CTRL_SPI_CLK_2X_SEL			BIT(14)
#define CLK_CTRL_ACCUM_RST_ON_LOOP		BIT(15)

#define HSSPI_PROFILE_SIGNAL_CTRL_REG(x)	(0x104 + (x) * 0x20)
#define SIGNAL_CTRL_LATCH_RISING		BIT(12)
#define SIGNAL_CTRL_LAUNCH_RISING		BIT(13)
#define SIGNAL_CTRL_ASYNC_INPUT_PATH		BIT(16)

#define HSSPI_PROFILE_MODE_CTRL_REG(x)		(0x108 + (x) * 0x20)
#define MODE_CTRL_MULTIDATA_RD_STRT_SHIFT	8
#define MODE_CTRL_MULTIDATA_WR_STRT_SHIFT	12
#define MODE_CTRL_MULTIDATA_RD_SIZE_SHIFT	16
#define MODE_CTRL_MULTIDATA_WR_SIZE_SHIFT	18
#define MODE_CTRL_MODE_3WIRE			BIT(20)
#define MODE_CTRL_PREPENDBYTE_CNT_SHIFT		24

#define HSSPI_FIFO_REG(x)			(0x200 + (x) * 0x200)


#define HSSPI_OP_MULTIBIT			BIT(11)
#define HSSPI_OP_CODE_SHIFT			13
#define HSSPI_OP_SLEEP				(0 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_READ_WRITE			(1 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_WRITE				(2 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_READ				(3 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_SETIRQ				(4 << HSSPI_OP_CODE_SHIFT)

#define HSSPI_BUFFER_LEN			512
#define HSSPI_OPCODE_LEN			2

#define HSSPI_MAX_PREPEND_LEN			15

#define HSSPI_MAX_SYNC_CLOCK			30000000

#define HSSPI_SPI_MAX_CS			8
#define HSSPI_BUS_NUM				1 /* 0 is legacy SPI */

struct bcm63xx_hsspi {
	struct completion done;
	struct mutex bus_mutex;

	struct platform_device *pdev;
	struct clk *clk;
	struct clk *pll_clk;
	void __iomem *regs;
	u8 __iomem *fifo;

	u32 speed_hz;
	u8 cs_polarity;
};

static void bcm63xx_hsspi_set_cs(struct bcm63xx_hsspi *bs, unsigned int cs,
				 bool active)
{
	u32 reg;

	mutex_lock(&bs->bus_mutex);
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);

	reg &= ~BIT(cs);
	if (active == !(bs->cs_polarity & BIT(cs)))
		reg |= BIT(cs);

	__raw_writel(reg, bs->regs + HSSPI_GLOBAL_CTRL_REG);
	mutex_unlock(&bs->bus_mutex);
}

static void bcm63xx_hsspi_set_clk(struct bcm63xx_hsspi *bs,
				  struct spi_device *spi, int hz)
{
	unsigned int profile = spi->chip_select;
	u32 reg;

	reg = DIV_ROUND_UP(2048, DIV_ROUND_UP(bs->speed_hz, hz));
	__raw_writel(CLK_CTRL_ACCUM_RST_ON_LOOP | reg,
		     bs->regs + HSSPI_PROFILE_CLK_CTRL_REG(profile));

	reg = __raw_readl(bs->regs + HSSPI_PROFILE_SIGNAL_CTRL_REG(profile));
	if (hz > HSSPI_MAX_SYNC_CLOCK)
		reg |= SIGNAL_CTRL_ASYNC_INPUT_PATH;
	else
		reg &= ~SIGNAL_CTRL_ASYNC_INPUT_PATH;
	__raw_writel(reg, bs->regs + HSSPI_PROFILE_SIGNAL_CTRL_REG(profile));

	mutex_lock(&bs->bus_mutex);
	/* setup clock polarity */
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);
	reg &= ~GLOBAL_CTRL_CLK_POLARITY;
	if (spi->mode & SPI_CPOL)
		reg |= GLOBAL_CTRL_CLK_POLARITY;
	__raw_writel(reg, bs->regs + HSSPI_GLOBAL_CTRL_REG);
	mutex_unlock(&bs->bus_mutex);
}

static int bcm63xx_hsspi_do_txrx(struct spi_device *spi, struct spi_transfer *t)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(spi->master);
	unsigned int chip_select = spi->chip_select;
	u16 opcode = 0;
	int pending = t->len;
	int step_size = HSSPI_BUFFER_LEN;
	const u8 *tx = t->tx_buf;
	u8 *rx = t->rx_buf;

	bcm63xx_hsspi_set_clk(bs, spi, t->speed_hz);
	bcm63xx_hsspi_set_cs(bs, spi->chip_select, true);

	if (tx && rx)
		opcode = HSSPI_OP_READ_WRITE;
	else if (tx)
		opcode = HSSPI_OP_WRITE;
	else if (rx)
		opcode = HSSPI_OP_READ;

	if (opcode != HSSPI_OP_READ)
		step_size -= HSSPI_OPCODE_LEN;

	if ((opcode == HSSPI_OP_READ && t->rx_nbits == SPI_NBITS_DUAL) ||
	    (opcode == HSSPI_OP_WRITE && t->tx_nbits == SPI_NBITS_DUAL))
		opcode |= HSSPI_OP_MULTIBIT;

	__raw_writel(1 << MODE_CTRL_MULTIDATA_WR_SIZE_SHIFT |
		     1 << MODE_CTRL_MULTIDATA_RD_SIZE_SHIFT | 0xff,
		     bs->regs + HSSPI_PROFILE_MODE_CTRL_REG(chip_select));

	while (pending > 0) {
		int curr_step = min_t(int, step_size, pending);

		reinit_completion(&bs->done);
		if (tx) {
			memcpy_toio(bs->fifo + HSSPI_OPCODE_LEN, tx, curr_step);
			tx += curr_step;
		}

		__raw_writew(opcode | curr_step, bs->fifo);

		/* enable interrupt */
		__raw_writel(HSSPI_PINGx_CMD_DONE(0),
			     bs->regs + HSSPI_INT_MASK_REG);

		/* start the transfer */
		__raw_writel(!chip_select << PINGPONG_CMD_SS_SHIFT |
			     chip_select << PINGPONG_CMD_PROFILE_SHIFT |
			     PINGPONG_COMMAND_START_NOW,
			     bs->regs + HSSPI_PINGPONG_COMMAND_REG(0));

		if (wait_for_completion_timeout(&bs->done, HZ) == 0) {
			dev_err(&bs->pdev->dev, "transfer timed out!\n");
			return -ETIMEDOUT;
		}

		if (rx) {
			memcpy_fromio(rx, bs->fifo, curr_step);
			rx += curr_step;
		}

		pending -= curr_step;
	}

	return 0;
}

static int bcm63xx_hsspi_setup(struct spi_device *spi)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(spi->master);
	u32 reg;

	reg = __raw_readl(bs->regs +
			  HSSPI_PROFILE_SIGNAL_CTRL_REG(spi->chip_select));
	reg &= ~(SIGNAL_CTRL_LAUNCH_RISING | SIGNAL_CTRL_LATCH_RISING);
	if (spi->mode & SPI_CPHA)
		reg |= SIGNAL_CTRL_LAUNCH_RISING;
	else
		reg |= SIGNAL_CTRL_LATCH_RISING;
	__raw_writel(reg, bs->regs +
		     HSSPI_PROFILE_SIGNAL_CTRL_REG(spi->chip_select));

	mutex_lock(&bs->bus_mutex);
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);

	/* only change actual polarities if there is no transfer */
	if ((reg & GLOBAL_CTRL_CS_POLARITY_MASK) == bs->cs_polarity) {
		if (spi->mode & SPI_CS_HIGH)
			reg |= BIT(spi->chip_select);
		else
			reg &= ~BIT(spi->chip_select);
		__raw_writel(reg, bs->regs + HSSPI_GLOBAL_CTRL_REG);
	}

	if (spi->mode & SPI_CS_HIGH)
		bs->cs_polarity |= BIT(spi->chip_select);
	else
		bs->cs_polarity &= ~BIT(spi->chip_select);

	mutex_unlock(&bs->bus_mutex);

	return 0;
}

static int bcm63xx_hsspi_transfer_one(struct spi_master *master,
				      struct spi_message *msg)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);
	struct spi_transfer *t;
	struct spi_device *spi = msg->spi;
	int status = -EINVAL;
	int dummy_cs;
	u32 reg;

	/* This controller does not support keeping CS active during idle.
	 * To work around this, we use the following ugly hack:
	 *
	 * a. Invert the target chip select's polarity so it will be active.
	 * b. Select a "dummy" chip select to use as the hardware target.
	 * c. Invert the dummy chip select's polarity so it will be inactive
	 *    during the actual transfers.
	 * d. Tell the hardware to send to the dummy chip select. Thanks to
	 *    the multiplexed nature of SPI the actual target will receive
	 *    the transfer and we see its response.
	 *
	 * e. At the end restore the polarities again to their default values.
	 */

	dummy_cs = !spi->chip_select;
	bcm63xx_hsspi_set_cs(bs, dummy_cs, true);

	list_for_each_entry(t, &msg->transfers, transfer_list) {
		status = bcm63xx_hsspi_do_txrx(spi, t);
		if (status)
			break;

		msg->actual_length += t->len;

		spi_transfer_delay_exec(t);

		if (t->cs_change)
			bcm63xx_hsspi_set_cs(bs, spi->chip_select, false);
	}

	mutex_lock(&bs->bus_mutex);
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);
	reg &= ~GLOBAL_CTRL_CS_POLARITY_MASK;
	reg |= bs->cs_polarity;
	__raw_writel(reg, bs->regs + HSSPI_GLOBAL_CTRL_REG);
	mutex_unlock(&bs->bus_mutex);

	msg->status = status;
	spi_finalize_current_message(master);

	return 0;
}

static irqreturn_t bcm63xx_hsspi_interrupt(int irq, void *dev_id)
{
	struct bcm63xx_hsspi *bs = (struct bcm63xx_hsspi *)dev_id;

	if (__raw_readl(bs->regs + HSSPI_INT_STATUS_MASKED_REG) == 0)
		return IRQ_NONE;

	__raw_writel(HSSPI_INT_CLEAR_ALL, bs->regs + HSSPI_INT_STATUS_REG);
	__raw_writel(0, bs->regs + HSSPI_INT_MASK_REG);

	complete(&bs->done);

	return IRQ_HANDLED;
}

static int bcm63xx_hsspi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct bcm63xx_hsspi *bs;
	void __iomem *regs;
	struct device *dev = &pdev->dev;
	struct clk *clk, *pll_clk = NULL;
	int irq, ret;
	u32 reg, rate, num_cs = HSSPI_SPI_MAX_CS;
	struct reset_control *reset;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	clk = devm_clk_get(dev, "hsspi");

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	reset = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	ret = reset_control_reset(reset);
	if (ret) {
		dev_err(dev, "unable to reset device: %d\n", ret);
		goto out_disable_clk;
	}

	rate = clk_get_rate(clk);
	if (!rate) {
		pll_clk = devm_clk_get(dev, "pll");

		if (IS_ERR(pll_clk)) {
			ret = PTR_ERR(pll_clk);
			goto out_disable_clk;
		}

		ret = clk_prepare_enable(pll_clk);
		if (ret)
			goto out_disable_clk;

		rate = clk_get_rate(pll_clk);
		if (!rate) {
			ret = -EINVAL;
			goto out_disable_pll_clk;
		}
	}

	master = spi_alloc_master(&pdev->dev, sizeof(*bs));
	if (!master) {
		ret = -ENOMEM;
		goto out_disable_pll_clk;
	}

	bs = spi_master_get_devdata(master);
	bs->pdev = pdev;
	bs->clk = clk;
	bs->pll_clk = pll_clk;
	bs->regs = regs;
	bs->speed_hz = rate;
	bs->fifo = (u8 __iomem *)(bs->regs + HSSPI_FIFO_REG(0));

	mutex_init(&bs->bus_mutex);
	init_completion(&bs->done);

	master->dev.of_node = dev->of_node;
	if (!dev->of_node)
		master->bus_num = HSSPI_BUS_NUM;

	of_property_read_u32(dev->of_node, "num-cs", &num_cs);
	if (num_cs > 8) {
		dev_warn(dev, "unsupported number of cs (%i), reducing to 8\n",
			 num_cs);
		num_cs = HSSPI_SPI_MAX_CS;
	}
	master->num_chipselect = num_cs;
	master->setup = bcm63xx_hsspi_setup;
	master->transfer_one_message = bcm63xx_hsspi_transfer_one;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH |
			    SPI_RX_DUAL | SPI_TX_DUAL;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->auto_runtime_pm = true;

	platform_set_drvdata(pdev, master);

	/* Initialize the hardware */
	__raw_writel(0, bs->regs + HSSPI_INT_MASK_REG);

	/* clean up any pending interrupts */
	__raw_writel(HSSPI_INT_CLEAR_ALL, bs->regs + HSSPI_INT_STATUS_REG);

	/* read out default CS polarities */
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);
	bs->cs_polarity = reg & GLOBAL_CTRL_CS_POLARITY_MASK;
	__raw_writel(reg | GLOBAL_CTRL_CLK_GATE_SSOFF,
		     bs->regs + HSSPI_GLOBAL_CTRL_REG);

	ret = devm_request_irq(dev, irq, bcm63xx_hsspi_interrupt, IRQF_SHARED,
			       pdev->name, bs);

	if (ret)
		goto out_put_master;

	/* register and we are done */
	ret = devm_spi_register_master(dev, master);
	if (ret)
		goto out_put_master;

	return 0;

out_put_master:
	spi_master_put(master);
out_disable_pll_clk:
	clk_disable_unprepare(pll_clk);
out_disable_clk:
	clk_disable_unprepare(clk);
	return ret;
}


static int bcm63xx_hsspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);

	/* reset the hardware and block queue progress */
	__raw_writel(0, bs->regs + HSSPI_INT_MASK_REG);
	clk_disable_unprepare(bs->pll_clk);
	clk_disable_unprepare(bs->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bcm63xx_hsspi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);

	spi_master_suspend(master);
	clk_disable_unprepare(bs->pll_clk);
	clk_disable_unprepare(bs->clk);

	return 0;
}

static int bcm63xx_hsspi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(bs->clk);
	if (ret)
		return ret;

	if (bs->pll_clk) {
		ret = clk_prepare_enable(bs->pll_clk);
		if (ret) {
			clk_disable_unprepare(bs->clk);
			return ret;
		}
	}

	spi_master_resume(master);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(bcm63xx_hsspi_pm_ops, bcm63xx_hsspi_suspend,
			 bcm63xx_hsspi_resume);

static const struct of_device_id bcm63xx_hsspi_of_match[] = {
	{ .compatible = "brcm,bcm6328-hsspi", },
	{ },
};
MODULE_DEVICE_TABLE(of, bcm63xx_hsspi_of_match);

static struct platform_driver bcm63xx_hsspi_driver = {
	.driver = {
		.name	= "bcm63xx-hsspi",
		.pm	= &bcm63xx_hsspi_pm_ops,
		.of_match_table = bcm63xx_hsspi_of_match,
	},
	.probe		= bcm63xx_hsspi_probe,
	.remove		= bcm63xx_hsspi_remove,
};

module_platform_driver(bcm63xx_hsspi_driver);

MODULE_ALIAS("platform:bcm63xx_hsspi");
MODULE_DESCRIPTION("Broadcom BCM63xx High Speed SPI Controller driver");
MODULE_AUTHOR("Jonas Gorski <jogo@openwrt.org>");
MODULE_LICENSE("GPL");
