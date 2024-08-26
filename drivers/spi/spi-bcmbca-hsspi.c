// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom BCMBCA High Speed SPI Controller driver
 *
 * Copyright 2000-2010 Broadcom Corporation
 * Copyright 2012-2013 Jonas Gorski <jonas.gorski@gmail.com>
 * Copyright 2019-2022 Broadcom Ltd
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
#include <linux/spi/spi-mem.h>
#include <linux/pm_runtime.h>

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
#define HSSPI_PINGPONG_STATUS_SRC_BUSY          BIT(1)

#define HSSPI_PROFILE_CLK_CTRL_REG(x)		(0x100 + (x) * 0x20)
#define CLK_CTRL_FREQ_CTRL_MASK			0x0000ffff
#define CLK_CTRL_SPI_CLK_2X_SEL			BIT(14)
#define CLK_CTRL_ACCUM_RST_ON_LOOP		BIT(15)
#define CLK_CTRL_CLK_POLARITY			BIT(16)

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
#define HSSPI_BUS_NUM				1	/* 0 is legacy SPI */
#define HSSPI_POLL_STATUS_TIMEOUT_MS	100

#define HSSPI_WAIT_MODE_POLLING		0
#define HSSPI_WAIT_MODE_INTR		1
#define HSSPI_WAIT_MODE_MAX			HSSPI_WAIT_MODE_INTR

#define SPIM_CTRL_CS_OVERRIDE_SEL_SHIFT		0
#define SPIM_CTRL_CS_OVERRIDE_SEL_MASK		0xff
#define SPIM_CTRL_CS_OVERRIDE_VAL_SHIFT		8
#define SPIM_CTRL_CS_OVERRIDE_VAL_MASK		0xff

struct bcmbca_hsspi {
	struct completion done;
	struct mutex bus_mutex;
	struct mutex msg_mutex;
	struct platform_device *pdev;
	struct clk *clk;
	struct clk *pll_clk;
	void __iomem *regs;
	void __iomem *spim_ctrl;
	u8 __iomem *fifo;
	u32 speed_hz;
	u8 cs_polarity;
	u32 wait_mode;
};

static ssize_t wait_mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct bcmbca_hsspi *bs = spi_controller_get_devdata(ctrl);

	return sprintf(buf, "%d\n", bs->wait_mode);
}

static ssize_t wait_mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct bcmbca_hsspi *bs = spi_controller_get_devdata(ctrl);
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	if (val > HSSPI_WAIT_MODE_MAX) {
		dev_warn(dev, "invalid wait mode %u\n", val);
		return -EINVAL;
	}

	mutex_lock(&bs->msg_mutex);
	bs->wait_mode = val;
	/* clear interrupt status to avoid spurious int on next transfer */
	if (val == HSSPI_WAIT_MODE_INTR)
		__raw_writel(HSSPI_INT_CLEAR_ALL, bs->regs + HSSPI_INT_STATUS_REG);
	mutex_unlock(&bs->msg_mutex);

	return count;
}

static DEVICE_ATTR_RW(wait_mode);

static struct attribute *bcmbca_hsspi_attrs[] = {
	&dev_attr_wait_mode.attr,
	NULL,
};

static const struct attribute_group bcmbca_hsspi_group = {
	.attrs = bcmbca_hsspi_attrs,
};

static void bcmbca_hsspi_set_cs(struct bcmbca_hsspi *bs, unsigned int cs,
				 bool active)
{
	u32 reg;

	/* No cs orerriden needed for SS7 internal cs on pcm based voice dev */
	if (cs == 7)
		return;

	mutex_lock(&bs->bus_mutex);

	reg = __raw_readl(bs->spim_ctrl);
	if (active)
		reg |= BIT(cs + SPIM_CTRL_CS_OVERRIDE_SEL_SHIFT);
	else
		reg &= ~BIT(cs + SPIM_CTRL_CS_OVERRIDE_SEL_SHIFT);

	__raw_writel(reg, bs->spim_ctrl);

	mutex_unlock(&bs->bus_mutex);
}

static void bcmbca_hsspi_set_clk(struct bcmbca_hsspi *bs,
				  struct spi_device *spi, int hz)
{
	unsigned int profile = spi_get_chipselect(spi, 0);
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

static int bcmbca_hsspi_wait_cmd(struct bcmbca_hsspi *bs, unsigned int cs)
{
	unsigned long limit;
	u32 reg = 0;
	int rc = 0;

	if (bs->wait_mode == HSSPI_WAIT_MODE_INTR) {
		if (wait_for_completion_timeout(&bs->done, HZ) == 0)
			rc = 1;
	} else {
		limit = jiffies + msecs_to_jiffies(HSSPI_POLL_STATUS_TIMEOUT_MS);

		while (!time_after(jiffies, limit)) {
			reg = __raw_readl(bs->regs + HSSPI_PINGPONG_STATUS_REG(0));
			if (reg & HSSPI_PINGPONG_STATUS_SRC_BUSY)
				cpu_relax();
			else
				break;
		}
		if (reg & HSSPI_PINGPONG_STATUS_SRC_BUSY)
			rc = 1;
	}

	if (rc)
		dev_err(&bs->pdev->dev, "transfer timed out!\n");

	return rc;
}

static int bcmbca_hsspi_do_txrx(struct spi_device *spi, struct spi_transfer *t,
								struct spi_message *msg)
{
	struct bcmbca_hsspi *bs = spi_controller_get_devdata(spi->controller);
	unsigned int chip_select = spi_get_chipselect(spi, 0);
	u16 opcode = 0, val;
	int pending = t->len;
	int step_size = HSSPI_BUFFER_LEN;
	const u8 *tx = t->tx_buf;
	u8 *rx = t->rx_buf;
	u32 reg = 0, cs_act = 0;

	bcmbca_hsspi_set_clk(bs, spi, t->speed_hz);

	if (tx && rx)
		opcode = HSSPI_OP_READ_WRITE;
	else if (tx)
		opcode = HSSPI_OP_WRITE;
	else if (rx)
		opcode = HSSPI_OP_READ;

	if (opcode != HSSPI_OP_READ)
		step_size -= HSSPI_OPCODE_LEN;

	if ((opcode == HSSPI_OP_READ && t->rx_nbits == SPI_NBITS_DUAL) ||
	    (opcode == HSSPI_OP_WRITE && t->tx_nbits == SPI_NBITS_DUAL)) {
		opcode |= HSSPI_OP_MULTIBIT;

		if (t->rx_nbits == SPI_NBITS_DUAL)
			reg |= 1 << MODE_CTRL_MULTIDATA_RD_SIZE_SHIFT;
		if (t->tx_nbits == SPI_NBITS_DUAL)
			reg |= 1 << MODE_CTRL_MULTIDATA_WR_SIZE_SHIFT;
	}

	__raw_writel(reg | 0xff,
		     bs->regs + HSSPI_PROFILE_MODE_CTRL_REG(chip_select));

	while (pending > 0) {
		int curr_step = min_t(int, step_size, pending);

		reinit_completion(&bs->done);
		if (tx) {
			memcpy_toio(bs->fifo + HSSPI_OPCODE_LEN, tx, curr_step);
			tx += curr_step;
		}

		*(__be16 *)(&val) = cpu_to_be16(opcode | curr_step);
		__raw_writew(val, bs->fifo);

		/* enable interrupt */
		if (bs->wait_mode == HSSPI_WAIT_MODE_INTR)
			__raw_writel(HSSPI_PINGx_CMD_DONE(0),
			    bs->regs + HSSPI_INT_MASK_REG);

		if (!cs_act) {
			/* must apply cs signal as close as the cmd starts */
			bcmbca_hsspi_set_cs(bs, chip_select, true);
			cs_act = 1;
		}

		reg = chip_select << PINGPONG_CMD_SS_SHIFT |
			    chip_select << PINGPONG_CMD_PROFILE_SHIFT |
			    PINGPONG_COMMAND_START_NOW;
		__raw_writel(reg, bs->regs + HSSPI_PINGPONG_COMMAND_REG(0));

		if (bcmbca_hsspi_wait_cmd(bs, spi_get_chipselect(spi, 0)))
			return -ETIMEDOUT;

		pending -= curr_step;

		if (rx) {
			memcpy_fromio(rx, bs->fifo, curr_step);
			rx += curr_step;
		}
	}

	return 0;
}

static int bcmbca_hsspi_setup(struct spi_device *spi)
{
	struct bcmbca_hsspi *bs = spi_controller_get_devdata(spi->controller);
	u32 reg;

	reg = __raw_readl(bs->regs +
			  HSSPI_PROFILE_SIGNAL_CTRL_REG(spi_get_chipselect(spi, 0)));
	reg &= ~(SIGNAL_CTRL_LAUNCH_RISING | SIGNAL_CTRL_LATCH_RISING);
	if (spi->mode & SPI_CPHA)
		reg |= SIGNAL_CTRL_LAUNCH_RISING;
	else
		reg |= SIGNAL_CTRL_LATCH_RISING;
	__raw_writel(reg, bs->regs +
		     HSSPI_PROFILE_SIGNAL_CTRL_REG(spi_get_chipselect(spi, 0)));

	mutex_lock(&bs->bus_mutex);
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);

	if (spi->mode & SPI_CS_HIGH)
		reg |= BIT(spi_get_chipselect(spi, 0));
	else
		reg &= ~BIT(spi_get_chipselect(spi, 0));
	__raw_writel(reg, bs->regs + HSSPI_GLOBAL_CTRL_REG);

	if (spi->mode & SPI_CS_HIGH)
		bs->cs_polarity |= BIT(spi_get_chipselect(spi, 0));
	else
		bs->cs_polarity &= ~BIT(spi_get_chipselect(spi, 0));

	reg = __raw_readl(bs->spim_ctrl);
	reg &= ~BIT(spi_get_chipselect(spi, 0) + SPIM_CTRL_CS_OVERRIDE_VAL_SHIFT);
	if (spi->mode & SPI_CS_HIGH)
		reg |= BIT(spi_get_chipselect(spi, 0) + SPIM_CTRL_CS_OVERRIDE_VAL_SHIFT);
	__raw_writel(reg, bs->spim_ctrl);

	mutex_unlock(&bs->bus_mutex);

	return 0;
}

static int bcmbca_hsspi_transfer_one(struct spi_controller *host,
				      struct spi_message *msg)
{
	struct bcmbca_hsspi *bs = spi_controller_get_devdata(host);
	struct spi_transfer *t;
	struct spi_device *spi = msg->spi;
	int status = -EINVAL;
	bool keep_cs = false;

	mutex_lock(&bs->msg_mutex);
	list_for_each_entry(t, &msg->transfers, transfer_list) {
		status = bcmbca_hsspi_do_txrx(spi, t, msg);
		if (status)
			break;

		spi_transfer_delay_exec(t);

		if (t->cs_change) {
			if (list_is_last(&t->transfer_list,	&msg->transfers)) {
				keep_cs = true;
			} else {
				if (!t->cs_off)
					bcmbca_hsspi_set_cs(bs, spi_get_chipselect(spi, 0), false);

				spi_transfer_cs_change_delay_exec(msg, t);

				if (!list_next_entry(t, transfer_list)->cs_off)
					bcmbca_hsspi_set_cs(bs, spi_get_chipselect(spi, 0), true);
			}
		} else if (!list_is_last(&t->transfer_list, &msg->transfers) &&
			   t->cs_off != list_next_entry(t, transfer_list)->cs_off) {
			bcmbca_hsspi_set_cs(bs, spi_get_chipselect(spi, 0), t->cs_off);
		}

		msg->actual_length += t->len;
	}

	mutex_unlock(&bs->msg_mutex);

	if (status || !keep_cs)
		bcmbca_hsspi_set_cs(bs, spi_get_chipselect(spi, 0), false);

	msg->status = status;
	spi_finalize_current_message(host);

	return 0;
}

static irqreturn_t bcmbca_hsspi_interrupt(int irq, void *dev_id)
{
	struct bcmbca_hsspi *bs = (struct bcmbca_hsspi *)dev_id;

	if (__raw_readl(bs->regs + HSSPI_INT_STATUS_MASKED_REG) == 0)
		return IRQ_NONE;

	__raw_writel(HSSPI_INT_CLEAR_ALL, bs->regs + HSSPI_INT_STATUS_REG);
	__raw_writel(0, bs->regs + HSSPI_INT_MASK_REG);

	complete(&bs->done);

	return IRQ_HANDLED;
}

static int bcmbca_hsspi_probe(struct platform_device *pdev)
{
	struct spi_controller *host;
	struct bcmbca_hsspi *bs;
	struct resource *res_mem;
	void __iomem *spim_ctrl;
	void __iomem *regs;
	struct device *dev = &pdev->dev;
	struct clk *clk, *pll_clk = NULL;
	int irq, ret;
	u32 reg, rate, num_cs = HSSPI_SPI_MAX_CS;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	res_mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hsspi");
	if (!res_mem)
		return -EINVAL;
	regs = devm_ioremap_resource(dev, res_mem);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	res_mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "spim-ctrl");
	if (!res_mem)
		return -EINVAL;
	spim_ctrl = devm_ioremap_resource(dev, res_mem);
	if (IS_ERR(spim_ctrl))
		return PTR_ERR(spim_ctrl);

	clk = devm_clk_get(dev, "hsspi");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

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

	host = spi_alloc_host(&pdev->dev, sizeof(*bs));
	if (!host) {
		ret = -ENOMEM;
		goto out_disable_pll_clk;
	}

	bs = spi_controller_get_devdata(host);
	bs->pdev = pdev;
	bs->clk = clk;
	bs->pll_clk = pll_clk;
	bs->regs = regs;
	bs->spim_ctrl = spim_ctrl;
	bs->speed_hz = rate;
	bs->fifo = (u8 __iomem *) (bs->regs + HSSPI_FIFO_REG(0));
	bs->wait_mode = HSSPI_WAIT_MODE_POLLING;

	mutex_init(&bs->bus_mutex);
	mutex_init(&bs->msg_mutex);
	init_completion(&bs->done);

	host->dev.of_node = dev->of_node;
	if (!dev->of_node)
		host->bus_num = HSSPI_BUS_NUM;

	of_property_read_u32(dev->of_node, "num-cs", &num_cs);
	if (num_cs > 8) {
		dev_warn(dev, "unsupported number of cs (%i), reducing to 8\n",
			 num_cs);
		num_cs = HSSPI_SPI_MAX_CS;
	}
	host->num_chipselect = num_cs;
	host->setup = bcmbca_hsspi_setup;
	host->transfer_one_message = bcmbca_hsspi_transfer_one;
	host->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH |
			  SPI_RX_DUAL | SPI_TX_DUAL;
	host->bits_per_word_mask = SPI_BPW_MASK(8);
	host->auto_runtime_pm = true;

	platform_set_drvdata(pdev, host);

	/* Initialize the hardware */
	__raw_writel(0, bs->regs + HSSPI_INT_MASK_REG);

	/* clean up any pending interrupts */
	__raw_writel(HSSPI_INT_CLEAR_ALL, bs->regs + HSSPI_INT_STATUS_REG);

	/* read out default CS polarities */
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);
	bs->cs_polarity = reg & GLOBAL_CTRL_CS_POLARITY_MASK;
	__raw_writel(reg | GLOBAL_CTRL_CLK_GATE_SSOFF,
		     bs->regs + HSSPI_GLOBAL_CTRL_REG);

	if (irq > 0) {
		ret = devm_request_irq(dev, irq, bcmbca_hsspi_interrupt, IRQF_SHARED,
			       pdev->name, bs);
		if (ret)
			goto out_put_host;
	}

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		goto out_put_host;

	ret = sysfs_create_group(&pdev->dev.kobj, &bcmbca_hsspi_group);
	if (ret) {
		dev_err(&pdev->dev, "couldn't register sysfs group\n");
		goto out_put_host;
	}

	/* register and we are done */
	ret = devm_spi_register_controller(dev, host);
	if (ret)
		goto out_sysgroup_disable;

	dev_info(dev, "Broadcom BCMBCA High Speed SPI Controller driver");

	return 0;

out_sysgroup_disable:
	sysfs_remove_group(&pdev->dev.kobj, &bcmbca_hsspi_group);
out_put_host:
	spi_controller_put(host);
out_disable_pll_clk:
	clk_disable_unprepare(pll_clk);
out_disable_clk:
	clk_disable_unprepare(clk);
	return ret;
}

static void bcmbca_hsspi_remove(struct platform_device *pdev)
{
	struct spi_controller *host = platform_get_drvdata(pdev);
	struct bcmbca_hsspi *bs = spi_controller_get_devdata(host);

	/* reset the hardware and block queue progress */
	__raw_writel(0, bs->regs + HSSPI_INT_MASK_REG);
	clk_disable_unprepare(bs->pll_clk);
	clk_disable_unprepare(bs->clk);
	sysfs_remove_group(&pdev->dev.kobj, &bcmbca_hsspi_group);
}

#ifdef CONFIG_PM_SLEEP
static int bcmbca_hsspi_suspend(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct bcmbca_hsspi *bs = spi_controller_get_devdata(host);

	spi_controller_suspend(host);
	clk_disable_unprepare(bs->pll_clk);
	clk_disable_unprepare(bs->clk);

	return 0;
}

static int bcmbca_hsspi_resume(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct bcmbca_hsspi *bs = spi_controller_get_devdata(host);
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

	spi_controller_resume(host);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(bcmbca_hsspi_pm_ops, bcmbca_hsspi_suspend,
			 bcmbca_hsspi_resume);

static const struct of_device_id bcmbca_hsspi_of_match[] = {
	{ .compatible = "brcm,bcmbca-hsspi-v1.1", },
	{},
};

MODULE_DEVICE_TABLE(of, bcmbca_hsspi_of_match);

static struct platform_driver bcmbca_hsspi_driver = {
	.driver = {
		   .name = "bcmbca-hsspi",
		   .pm = &bcmbca_hsspi_pm_ops,
		   .of_match_table = bcmbca_hsspi_of_match,
		   },
	.probe = bcmbca_hsspi_probe,
	.remove_new = bcmbca_hsspi_remove,
};

module_platform_driver(bcmbca_hsspi_driver);

MODULE_ALIAS("platform:bcmbca_hsspi");
MODULE_DESCRIPTION("Broadcom BCMBCA High Speed SPI Controller driver");
MODULE_LICENSE("GPL");
