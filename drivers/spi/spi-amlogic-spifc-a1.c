// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Amlogic A1 SPI flash controller (SPIFC)
 *
 * Copyright (c) 2023, SberDevices. All Rights Reserved.
 *
 * Author: Martin Kurbanov <mmkurbanov@sberdevices.ru>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/types.h>

#define SPIFC_A1_AHB_CTRL_REG		0x0
#define SPIFC_A1_AHB_BUS_EN		BIT(31)

#define SPIFC_A1_USER_CTRL0_REG		0x200
#define SPIFC_A1_USER_REQUEST_ENABLE	BIT(31)
#define SPIFC_A1_USER_REQUEST_FINISH	BIT(30)
#define SPIFC_A1_USER_DATA_UPDATED	BIT(0)

#define SPIFC_A1_USER_CTRL1_REG		0x204
#define SPIFC_A1_USER_CMD_ENABLE	BIT(30)
#define SPIFC_A1_USER_CMD_MODE		GENMASK(29, 28)
#define SPIFC_A1_USER_CMD_CODE		GENMASK(27, 20)
#define SPIFC_A1_USER_ADDR_ENABLE	BIT(19)
#define SPIFC_A1_USER_ADDR_MODE		GENMASK(18, 17)
#define SPIFC_A1_USER_ADDR_BYTES	GENMASK(16, 15)
#define SPIFC_A1_USER_DOUT_ENABLE	BIT(14)
#define SPIFC_A1_USER_DOUT_MODE		GENMASK(11, 10)
#define SPIFC_A1_USER_DOUT_BYTES	GENMASK(9, 0)

#define SPIFC_A1_USER_CTRL2_REG		0x208
#define SPIFC_A1_USER_DUMMY_ENABLE	BIT(31)
#define SPIFC_A1_USER_DUMMY_MODE	GENMASK(30, 29)
#define SPIFC_A1_USER_DUMMY_CLK_SYCLES	GENMASK(28, 23)

#define SPIFC_A1_USER_CTRL3_REG		0x20c
#define SPIFC_A1_USER_DIN_ENABLE	BIT(31)
#define SPIFC_A1_USER_DIN_MODE		GENMASK(28, 27)
#define SPIFC_A1_USER_DIN_BYTES		GENMASK(25, 16)

#define SPIFC_A1_USER_ADDR_REG		0x210

#define SPIFC_A1_AHB_REQ_CTRL_REG	0x214
#define SPIFC_A1_AHB_REQ_ENABLE		BIT(31)

#define SPIFC_A1_ACTIMING0_REG		(0x0088 << 2)
#define SPIFC_A1_TSLCH			GENMASK(31, 30)
#define SPIFC_A1_TCLSH			GENMASK(29, 28)
#define SPIFC_A1_TSHWL			GENMASK(20, 16)
#define SPIFC_A1_TSHSL2			GENMASK(15, 12)
#define SPIFC_A1_TSHSL1			GENMASK(11, 8)
#define SPIFC_A1_TWHSL			GENMASK(7, 0)

#define SPIFC_A1_DBUF_CTRL_REG		0x240
#define SPIFC_A1_DBUF_DIR		BIT(31)
#define SPIFC_A1_DBUF_AUTO_UPDATE_ADDR	BIT(30)
#define SPIFC_A1_DBUF_ADDR		GENMASK(7, 0)

#define SPIFC_A1_DBUF_DATA_REG		0x244

#define SPIFC_A1_USER_DBUF_ADDR_REG	0x248

#define SPIFC_A1_BUFFER_SIZE		512U

#define SPIFC_A1_MAX_HZ			200000000
#define SPIFC_A1_MIN_HZ			1000000

#define SPIFC_A1_USER_CMD(op) ( \
	SPIFC_A1_USER_CMD_ENABLE | \
	FIELD_PREP(SPIFC_A1_USER_CMD_CODE, (op)->cmd.opcode) | \
	FIELD_PREP(SPIFC_A1_USER_CMD_MODE, ilog2((op)->cmd.buswidth)))

#define SPIFC_A1_USER_ADDR(op) ( \
	SPIFC_A1_USER_ADDR_ENABLE | \
	FIELD_PREP(SPIFC_A1_USER_ADDR_MODE, ilog2((op)->addr.buswidth)) | \
	FIELD_PREP(SPIFC_A1_USER_ADDR_BYTES, (op)->addr.nbytes - 1))

#define SPIFC_A1_USER_DUMMY(op) ( \
	SPIFC_A1_USER_DUMMY_ENABLE | \
	FIELD_PREP(SPIFC_A1_USER_DUMMY_MODE, ilog2((op)->dummy.buswidth)) | \
	FIELD_PREP(SPIFC_A1_USER_DUMMY_CLK_SYCLES, (op)->dummy.nbytes << 3))

#define SPIFC_A1_TSLCH_VAL	FIELD_PREP(SPIFC_A1_TSLCH, 1)
#define SPIFC_A1_TCLSH_VAL	FIELD_PREP(SPIFC_A1_TCLSH, 1)
#define SPIFC_A1_TSHWL_VAL	FIELD_PREP(SPIFC_A1_TSHWL, 7)
#define SPIFC_A1_TSHSL2_VAL	FIELD_PREP(SPIFC_A1_TSHSL2, 7)
#define SPIFC_A1_TSHSL1_VAL	FIELD_PREP(SPIFC_A1_TSHSL1, 7)
#define SPIFC_A1_TWHSL_VAL	FIELD_PREP(SPIFC_A1_TWHSL, 2)
#define SPIFC_A1_ACTIMING0_VAL	(SPIFC_A1_TSLCH_VAL | SPIFC_A1_TCLSH_VAL | \
				 SPIFC_A1_TSHWL_VAL | SPIFC_A1_TSHSL2_VAL | \
				 SPIFC_A1_TSHSL1_VAL | SPIFC_A1_TWHSL_VAL)

struct amlogic_spifc_a1 {
	struct spi_controller *ctrl;
	struct clk *clk;
	struct device *dev;
	void __iomem *base;
	u32 curr_speed_hz;
};

static int amlogic_spifc_a1_request(struct amlogic_spifc_a1 *spifc, bool read)
{
	u32 mask = SPIFC_A1_USER_REQUEST_FINISH |
		   (read ? SPIFC_A1_USER_DATA_UPDATED : 0);
	u32 val;

	writel(SPIFC_A1_USER_REQUEST_ENABLE,
	       spifc->base + SPIFC_A1_USER_CTRL0_REG);

	return readl_poll_timeout(spifc->base + SPIFC_A1_USER_CTRL0_REG,
				  val, (val & mask) == mask, 0,
				  200 * USEC_PER_MSEC);
}

static void amlogic_spifc_a1_drain_buffer(struct amlogic_spifc_a1 *spifc,
					  char *buf, u32 len)
{
	u32 data;
	const u32 count = len / sizeof(data);
	const u32 pad = len % sizeof(data);

	writel(SPIFC_A1_DBUF_AUTO_UPDATE_ADDR,
	       spifc->base + SPIFC_A1_DBUF_CTRL_REG);
	ioread32_rep(spifc->base + SPIFC_A1_DBUF_DATA_REG, buf, count);

	if (pad) {
		data = readl(spifc->base + SPIFC_A1_DBUF_DATA_REG);
		memcpy(buf + len - pad, &data, pad);
	}
}

static void amlogic_spifc_a1_fill_buffer(struct amlogic_spifc_a1 *spifc,
					 const char *buf, u32 len)
{
	u32 data;
	const u32 count = len / sizeof(data);
	const u32 pad = len % sizeof(data);

	writel(SPIFC_A1_DBUF_DIR | SPIFC_A1_DBUF_AUTO_UPDATE_ADDR,
	       spifc->base + SPIFC_A1_DBUF_CTRL_REG);
	iowrite32_rep(spifc->base + SPIFC_A1_DBUF_DATA_REG, buf, count);

	if (pad) {
		memcpy(&data, buf + len - pad, pad);
		writel(data, spifc->base + SPIFC_A1_DBUF_DATA_REG);
	}
}

static void amlogic_spifc_a1_user_init(struct amlogic_spifc_a1 *spifc)
{
	writel(0, spifc->base + SPIFC_A1_USER_CTRL0_REG);
	writel(0, spifc->base + SPIFC_A1_USER_CTRL1_REG);
	writel(0, spifc->base + SPIFC_A1_USER_CTRL2_REG);
	writel(0, spifc->base + SPIFC_A1_USER_CTRL3_REG);
}

static void amlogic_spifc_a1_set_cmd(struct amlogic_spifc_a1 *spifc,
				     u32 cmd_cfg)
{
	u32 val;

	val = readl(spifc->base + SPIFC_A1_USER_CTRL1_REG);
	val &= ~(SPIFC_A1_USER_CMD_MODE | SPIFC_A1_USER_CMD_CODE);
	val |= cmd_cfg;
	writel(val, spifc->base + SPIFC_A1_USER_CTRL1_REG);
}

static void amlogic_spifc_a1_set_addr(struct amlogic_spifc_a1 *spifc, u32 addr,
				      u32 addr_cfg)
{
	u32 val;

	writel(addr, spifc->base + SPIFC_A1_USER_ADDR_REG);

	val = readl(spifc->base + SPIFC_A1_USER_CTRL1_REG);
	val &= ~(SPIFC_A1_USER_ADDR_MODE | SPIFC_A1_USER_ADDR_BYTES);
	val |= addr_cfg;
	writel(val, spifc->base + SPIFC_A1_USER_CTRL1_REG);
}

static void amlogic_spifc_a1_set_dummy(struct amlogic_spifc_a1 *spifc,
				       u32 dummy_cfg)
{
	u32 val = readl(spifc->base + SPIFC_A1_USER_CTRL2_REG);

	val &= ~(SPIFC_A1_USER_DUMMY_MODE | SPIFC_A1_USER_DUMMY_CLK_SYCLES);
	val |= dummy_cfg;
	writel(val, spifc->base + SPIFC_A1_USER_CTRL2_REG);
}

static int amlogic_spifc_a1_read(struct amlogic_spifc_a1 *spifc, void *buf,
				 u32 size, u32 mode)
{
	u32 val = readl(spifc->base + SPIFC_A1_USER_CTRL3_REG);
	int ret;

	val &= ~(SPIFC_A1_USER_DIN_MODE | SPIFC_A1_USER_DIN_BYTES);
	val |= SPIFC_A1_USER_DIN_ENABLE;
	val |= FIELD_PREP(SPIFC_A1_USER_DIN_MODE, mode);
	val |= FIELD_PREP(SPIFC_A1_USER_DIN_BYTES, size);
	writel(val, spifc->base + SPIFC_A1_USER_CTRL3_REG);

	ret = amlogic_spifc_a1_request(spifc, true);
	if (!ret)
		amlogic_spifc_a1_drain_buffer(spifc, buf, size);

	return ret;
}

static int amlogic_spifc_a1_write(struct amlogic_spifc_a1 *spifc,
				  const void *buf, u32 size, u32 mode)
{
	u32 val;

	amlogic_spifc_a1_fill_buffer(spifc, buf, size);

	val = readl(spifc->base + SPIFC_A1_USER_CTRL1_REG);
	val &= ~(SPIFC_A1_USER_DOUT_MODE | SPIFC_A1_USER_DOUT_BYTES);
	val |= FIELD_PREP(SPIFC_A1_USER_DOUT_MODE, mode);
	val |= FIELD_PREP(SPIFC_A1_USER_DOUT_BYTES, size);
	val |= SPIFC_A1_USER_DOUT_ENABLE;
	writel(val, spifc->base + SPIFC_A1_USER_CTRL1_REG);

	return amlogic_spifc_a1_request(spifc, false);
}

static int amlogic_spifc_a1_set_freq(struct amlogic_spifc_a1 *spifc, u32 freq)
{
	int ret;

	if (freq == spifc->curr_speed_hz)
		return 0;

	ret = clk_set_rate(spifc->clk, freq);
	if (ret)
		return ret;

	spifc->curr_speed_hz = freq;
	return 0;
}

static int amlogic_spifc_a1_exec_op(struct spi_mem *mem,
				    const struct spi_mem_op *op)
{
	struct amlogic_spifc_a1 *spifc =
		spi_controller_get_devdata(mem->spi->controller);
	size_t data_size = op->data.nbytes;
	int ret;

	ret = amlogic_spifc_a1_set_freq(spifc, op->max_freq);
	if (ret)
		return ret;

	amlogic_spifc_a1_user_init(spifc);
	amlogic_spifc_a1_set_cmd(spifc, SPIFC_A1_USER_CMD(op));

	if (op->addr.nbytes)
		amlogic_spifc_a1_set_addr(spifc, op->addr.val,
					  SPIFC_A1_USER_ADDR(op));

	if (op->dummy.nbytes)
		amlogic_spifc_a1_set_dummy(spifc, SPIFC_A1_USER_DUMMY(op));

	if (data_size) {
		u32 mode = ilog2(op->data.buswidth);

		writel(0, spifc->base + SPIFC_A1_USER_DBUF_ADDR_REG);

		if (op->data.dir == SPI_MEM_DATA_IN)
			ret = amlogic_spifc_a1_read(spifc, op->data.buf.in,
						    data_size, mode);
		else
			ret = amlogic_spifc_a1_write(spifc, op->data.buf.out,
						     data_size, mode);
	} else {
		ret = amlogic_spifc_a1_request(spifc, false);
	}

	return ret;
}

static int amlogic_spifc_a1_adjust_op_size(struct spi_mem *mem,
					   struct spi_mem_op *op)
{
	op->data.nbytes = min(op->data.nbytes, SPIFC_A1_BUFFER_SIZE);
	return 0;
}

static void amlogic_spifc_a1_hw_init(struct amlogic_spifc_a1 *spifc)
{
	u32 regv;

	regv = readl(spifc->base + SPIFC_A1_AHB_REQ_CTRL_REG);
	regv &= ~(SPIFC_A1_AHB_REQ_ENABLE);
	writel(regv, spifc->base + SPIFC_A1_AHB_REQ_CTRL_REG);

	regv = readl(spifc->base + SPIFC_A1_AHB_CTRL_REG);
	regv &= ~(SPIFC_A1_AHB_BUS_EN);
	writel(regv, spifc->base + SPIFC_A1_AHB_CTRL_REG);

	writel(SPIFC_A1_ACTIMING0_VAL, spifc->base + SPIFC_A1_ACTIMING0_REG);

	writel(0, spifc->base + SPIFC_A1_USER_DBUF_ADDR_REG);
}

static const struct spi_controller_mem_ops amlogic_spifc_a1_mem_ops = {
	.exec_op = amlogic_spifc_a1_exec_op,
	.adjust_op_size = amlogic_spifc_a1_adjust_op_size,
};

static const struct spi_controller_mem_caps amlogic_spifc_a1_mem_caps = {
	.per_op_freq = true,
};

static int amlogic_spifc_a1_probe(struct platform_device *pdev)
{
	struct spi_controller *ctrl;
	struct amlogic_spifc_a1 *spifc;
	int ret;

	ctrl = devm_spi_alloc_host(&pdev->dev, sizeof(*spifc));
	if (!ctrl)
		return -ENOMEM;

	spifc = spi_controller_get_devdata(ctrl);
	platform_set_drvdata(pdev, spifc);

	spifc->dev = &pdev->dev;
	spifc->ctrl = ctrl;

	spifc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spifc->base))
		return PTR_ERR(spifc->base);

	spifc->clk = devm_clk_get_enabled(spifc->dev, NULL);
	if (IS_ERR(spifc->clk))
		return dev_err_probe(spifc->dev, PTR_ERR(spifc->clk),
				     "unable to get clock\n");

	amlogic_spifc_a1_hw_init(spifc);

	pm_runtime_set_autosuspend_delay(spifc->dev, 500);
	pm_runtime_use_autosuspend(spifc->dev);
	ret = devm_pm_runtime_enable(spifc->dev);
	if (ret)
		return ret;

	ctrl->num_chipselect = 1;
	ctrl->dev.of_node = pdev->dev.of_node;
	ctrl->bits_per_word_mask = SPI_BPW_MASK(8);
	ctrl->auto_runtime_pm = true;
	ctrl->mem_ops = &amlogic_spifc_a1_mem_ops;
	ctrl->mem_caps = &amlogic_spifc_a1_mem_caps;
	ctrl->min_speed_hz = SPIFC_A1_MIN_HZ;
	ctrl->max_speed_hz = SPIFC_A1_MAX_HZ;
	ctrl->mode_bits = (SPI_RX_DUAL | SPI_TX_DUAL |
			   SPI_RX_QUAD | SPI_TX_QUAD);

	ret = devm_spi_register_controller(spifc->dev, ctrl);
	if (ret)
		return dev_err_probe(spifc->dev, ret,
				     "failed to register spi controller\n");

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int amlogic_spifc_a1_suspend(struct device *dev)
{
	struct amlogic_spifc_a1 *spifc = dev_get_drvdata(dev);
	int ret;

	ret = spi_controller_suspend(spifc->ctrl);
	if (ret)
		return ret;

	if (!pm_runtime_suspended(dev))
		clk_disable_unprepare(spifc->clk);

	return 0;
}

static int amlogic_spifc_a1_resume(struct device *dev)
{
	struct amlogic_spifc_a1 *spifc = dev_get_drvdata(dev);
	int ret = 0;

	if (!pm_runtime_suspended(dev)) {
		ret = clk_prepare_enable(spifc->clk);
		if (ret)
			return ret;
	}

	amlogic_spifc_a1_hw_init(spifc);

	ret = spi_controller_resume(spifc->ctrl);
	if (ret)
		clk_disable_unprepare(spifc->clk);

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int amlogic_spifc_a1_runtime_suspend(struct device *dev)
{
	struct amlogic_spifc_a1 *spifc = dev_get_drvdata(dev);

	clk_disable_unprepare(spifc->clk);

	return 0;
}

static int amlogic_spifc_a1_runtime_resume(struct device *dev)
{
	struct amlogic_spifc_a1 *spifc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(spifc->clk);
	if (!ret)
		amlogic_spifc_a1_hw_init(spifc);

	return ret;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops amlogic_spifc_a1_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(amlogic_spifc_a1_suspend,
				amlogic_spifc_a1_resume)
	SET_RUNTIME_PM_OPS(amlogic_spifc_a1_runtime_suspend,
			   amlogic_spifc_a1_runtime_resume,
			   NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id amlogic_spifc_a1_dt_match[] = {
	{ .compatible = "amlogic,a1-spifc", },
	{ },
};
MODULE_DEVICE_TABLE(of, amlogic_spifc_a1_dt_match);
#endif /* CONFIG_OF */

static struct platform_driver amlogic_spifc_a1_driver = {
	.probe	= amlogic_spifc_a1_probe,
	.driver	= {
		.name		= "amlogic-spifc-a1",
		.of_match_table	= of_match_ptr(amlogic_spifc_a1_dt_match),
		.pm		= &amlogic_spifc_a1_pm_ops,
	},
};
module_platform_driver(amlogic_spifc_a1_driver);

MODULE_AUTHOR("Martin Kurbanov <mmkurbanov@sberdevices.ru>");
MODULE_DESCRIPTION("Amlogic A1 SPIFC driver");
MODULE_LICENSE("GPL");
