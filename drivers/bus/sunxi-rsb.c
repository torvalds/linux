// SPDX-License-Identifier: GPL-2.0-only
/*
 * RSB (Reduced Serial Bus) driver.
 *
 * Author: Chen-Yu Tsai <wens@csie.org>
 *
 * The RSB controller looks like an SMBus controller which only supports
 * byte and word data transfers. But, it differs from standard SMBus
 * protocol on several aspects:
 * - it uses addresses set at runtime to address slaves. Runtime addresses
 *   are sent to slaves using their 12bit hardware addresses. Up to 15
 *   runtime addresses are available.
 * - it adds a parity bit every 8bits of data and address for read and
 *   write accesses; this replaces the ack bit
 * - only one read access is required to read a byte (instead of a write
 *   followed by a read access in standard SMBus protocol)
 * - there's no Ack bit after each read access
 *
 * This means this bus cannot be used to interface with standard SMBus
 * devices. Devices known to support this interface include the AXP223,
 * AXP809, and AXP806 PMICs, and the AC100 audio codec, all from X-Powers.
 *
 * A description of the operation and wire protocol can be found in the
 * RSB section of Allwinner's A80 user manual, which can be found at
 *
 *     https://github.com/allwinner-zh/documents/tree/master/A80
 *
 * This document is officially released by Allwinner.
 *
 * This driver is based on i2c-sun6i-p2wi.c, the P2WI bus driver.
 */

#include <linux/clk.h>
#include <linux/clk/clk-conf.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/sunxi-rsb.h>
#include <linux/types.h>

/* RSB registers */
#define RSB_CTRL	0x0	/* Global control */
#define RSB_CCR		0x4	/* Clock control */
#define RSB_INTE	0x8	/* Interrupt controls */
#define RSB_INTS	0xc	/* Interrupt status */
#define RSB_ADDR	0x10	/* Address to send with read/write command */
#define RSB_DATA	0x1c	/* Data to read/write */
#define RSB_LCR		0x24	/* Line control */
#define RSB_DMCR	0x28	/* Device mode (init) control */
#define RSB_CMD		0x2c	/* RSB Command */
#define RSB_DAR		0x30	/* Device address / runtime address */

/* CTRL fields */
#define RSB_CTRL_START_TRANS		BIT(7)
#define RSB_CTRL_ABORT_TRANS		BIT(6)
#define RSB_CTRL_GLOBAL_INT_ENB		BIT(1)
#define RSB_CTRL_SOFT_RST		BIT(0)

/* CLK CTRL fields */
#define RSB_CCR_SDA_OUT_DELAY(v)	(((v) & 0x7) << 8)
#define RSB_CCR_MAX_CLK_DIV		0xff
#define RSB_CCR_CLK_DIV(v)		((v) & RSB_CCR_MAX_CLK_DIV)

/* STATUS fields */
#define RSB_INTS_TRANS_ERR_ACK		BIT(16)
#define RSB_INTS_TRANS_ERR_DATA_BIT(v)	(((v) >> 8) & 0xf)
#define RSB_INTS_TRANS_ERR_DATA		GENMASK(11, 8)
#define RSB_INTS_LOAD_BSY		BIT(2)
#define RSB_INTS_TRANS_ERR		BIT(1)
#define RSB_INTS_TRANS_OVER		BIT(0)

/* LINE CTRL fields*/
#define RSB_LCR_SCL_STATE		BIT(5)
#define RSB_LCR_SDA_STATE		BIT(4)
#define RSB_LCR_SCL_CTL			BIT(3)
#define RSB_LCR_SCL_CTL_EN		BIT(2)
#define RSB_LCR_SDA_CTL			BIT(1)
#define RSB_LCR_SDA_CTL_EN		BIT(0)

/* DEVICE MODE CTRL field values */
#define RSB_DMCR_DEVICE_START		BIT(31)
#define RSB_DMCR_MODE_DATA		(0x7c << 16)
#define RSB_DMCR_MODE_REG		(0x3e << 8)
#define RSB_DMCR_DEV_ADDR		0x00

/* CMD values */
#define RSB_CMD_RD8			0x8b
#define RSB_CMD_RD16			0x9c
#define RSB_CMD_RD32			0xa6
#define RSB_CMD_WR8			0x4e
#define RSB_CMD_WR16			0x59
#define RSB_CMD_WR32			0x63
#define RSB_CMD_STRA			0xe8

/* DAR fields */
#define RSB_DAR_RTA(v)			(((v) & 0xff) << 16)
#define RSB_DAR_DA(v)			((v) & 0xffff)

#define RSB_MAX_FREQ			20000000

#define RSB_CTRL_NAME			"sunxi-rsb"

struct sunxi_rsb_addr_map {
	u16 hwaddr;
	u8 rtaddr;
};

struct sunxi_rsb {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;
	struct reset_control *rstc;
	struct completion complete;
	struct mutex lock;
	unsigned int status;
	u32 clk_freq;
};

/* bus / slave device related functions */
static struct bus_type sunxi_rsb_bus;

static int sunxi_rsb_device_match(struct device *dev, struct device_driver *drv)
{
	return of_driver_match_device(dev, drv);
}

static int sunxi_rsb_device_probe(struct device *dev)
{
	const struct sunxi_rsb_driver *drv = to_sunxi_rsb_driver(dev->driver);
	struct sunxi_rsb_device *rdev = to_sunxi_rsb_device(dev);
	int ret;

	if (!drv->probe)
		return -ENODEV;

	if (!rdev->irq) {
		int irq = -ENOENT;

		if (dev->of_node)
			irq = of_irq_get(dev->of_node, 0);

		if (irq == -EPROBE_DEFER)
			return irq;
		if (irq < 0)
			irq = 0;

		rdev->irq = irq;
	}

	ret = of_clk_set_defaults(dev->of_node, false);
	if (ret < 0)
		return ret;

	return drv->probe(rdev);
}

static void sunxi_rsb_device_remove(struct device *dev)
{
	const struct sunxi_rsb_driver *drv = to_sunxi_rsb_driver(dev->driver);

	drv->remove(to_sunxi_rsb_device(dev));
}

static struct bus_type sunxi_rsb_bus = {
	.name		= RSB_CTRL_NAME,
	.match		= sunxi_rsb_device_match,
	.probe		= sunxi_rsb_device_probe,
	.remove		= sunxi_rsb_device_remove,
	.uevent		= of_device_uevent_modalias,
};

static void sunxi_rsb_dev_release(struct device *dev)
{
	struct sunxi_rsb_device *rdev = to_sunxi_rsb_device(dev);

	kfree(rdev);
}

/**
 * sunxi_rsb_device_create() - allocate and add an RSB device
 * @rsb:	RSB controller
 * @node:	RSB slave device node
 * @hwaddr:	RSB slave hardware address
 * @rtaddr:	RSB slave runtime address
 */
static struct sunxi_rsb_device *sunxi_rsb_device_create(struct sunxi_rsb *rsb,
		struct device_node *node, u16 hwaddr, u8 rtaddr)
{
	int err;
	struct sunxi_rsb_device *rdev;

	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (!rdev)
		return ERR_PTR(-ENOMEM);

	rdev->rsb = rsb;
	rdev->hwaddr = hwaddr;
	rdev->rtaddr = rtaddr;
	rdev->dev.bus = &sunxi_rsb_bus;
	rdev->dev.parent = rsb->dev;
	rdev->dev.of_node = node;
	rdev->dev.release = sunxi_rsb_dev_release;

	dev_set_name(&rdev->dev, "%s-%x", RSB_CTRL_NAME, hwaddr);

	err = device_register(&rdev->dev);
	if (err < 0) {
		dev_err(&rdev->dev, "Can't add %s, status %d\n",
			dev_name(&rdev->dev), err);
		goto err_device_add;
	}

	dev_dbg(&rdev->dev, "device %s registered\n", dev_name(&rdev->dev));

	return rdev;

err_device_add:
	put_device(&rdev->dev);

	return ERR_PTR(err);
}

/**
 * sunxi_rsb_device_unregister(): unregister an RSB device
 * @rdev:	rsb_device to be removed
 */
static void sunxi_rsb_device_unregister(struct sunxi_rsb_device *rdev)
{
	device_unregister(&rdev->dev);
}

static int sunxi_rsb_remove_devices(struct device *dev, void *data)
{
	struct sunxi_rsb_device *rdev = to_sunxi_rsb_device(dev);

	if (dev->bus == &sunxi_rsb_bus)
		sunxi_rsb_device_unregister(rdev);

	return 0;
}

/**
 * sunxi_rsb_driver_register() - Register device driver with RSB core
 * @rdrv:	device driver to be associated with slave-device.
 *
 * This API will register the client driver with the RSB framework.
 * It is typically called from the driver's module-init function.
 */
int sunxi_rsb_driver_register(struct sunxi_rsb_driver *rdrv)
{
	rdrv->driver.bus = &sunxi_rsb_bus;
	return driver_register(&rdrv->driver);
}
EXPORT_SYMBOL_GPL(sunxi_rsb_driver_register);

/* common code that starts a transfer */
static int _sunxi_rsb_run_xfer(struct sunxi_rsb *rsb)
{
	if (readl(rsb->regs + RSB_CTRL) & RSB_CTRL_START_TRANS) {
		dev_dbg(rsb->dev, "RSB transfer still in progress\n");
		return -EBUSY;
	}

	reinit_completion(&rsb->complete);

	writel(RSB_INTS_LOAD_BSY | RSB_INTS_TRANS_ERR | RSB_INTS_TRANS_OVER,
	       rsb->regs + RSB_INTE);
	writel(RSB_CTRL_START_TRANS | RSB_CTRL_GLOBAL_INT_ENB,
	       rsb->regs + RSB_CTRL);

	if (!wait_for_completion_io_timeout(&rsb->complete,
					    msecs_to_jiffies(100))) {
		dev_dbg(rsb->dev, "RSB timeout\n");

		/* abort the transfer */
		writel(RSB_CTRL_ABORT_TRANS, rsb->regs + RSB_CTRL);

		/* clear any interrupt flags */
		writel(readl(rsb->regs + RSB_INTS), rsb->regs + RSB_INTS);

		return -ETIMEDOUT;
	}

	if (rsb->status & RSB_INTS_LOAD_BSY) {
		dev_dbg(rsb->dev, "RSB busy\n");
		return -EBUSY;
	}

	if (rsb->status & RSB_INTS_TRANS_ERR) {
		if (rsb->status & RSB_INTS_TRANS_ERR_ACK) {
			dev_dbg(rsb->dev, "RSB slave nack\n");
			return -EINVAL;
		}

		if (rsb->status & RSB_INTS_TRANS_ERR_DATA) {
			dev_dbg(rsb->dev, "RSB transfer data error\n");
			return -EIO;
		}
	}

	return 0;
}

static int sunxi_rsb_read(struct sunxi_rsb *rsb, u8 rtaddr, u8 addr,
			  u32 *buf, size_t len)
{
	u32 cmd;
	int ret;

	if (!buf)
		return -EINVAL;

	switch (len) {
	case 1:
		cmd = RSB_CMD_RD8;
		break;
	case 2:
		cmd = RSB_CMD_RD16;
		break;
	case 4:
		cmd = RSB_CMD_RD32;
		break;
	default:
		dev_err(rsb->dev, "Invalid access width: %zd\n", len);
		return -EINVAL;
	}

	ret = pm_runtime_resume_and_get(rsb->dev);
	if (ret)
		return ret;

	mutex_lock(&rsb->lock);

	writel(addr, rsb->regs + RSB_ADDR);
	writel(RSB_DAR_RTA(rtaddr), rsb->regs + RSB_DAR);
	writel(cmd, rsb->regs + RSB_CMD);

	ret = _sunxi_rsb_run_xfer(rsb);
	if (ret)
		goto unlock;

	*buf = readl(rsb->regs + RSB_DATA) & GENMASK(len * 8 - 1, 0);

unlock:
	mutex_unlock(&rsb->lock);

	pm_runtime_mark_last_busy(rsb->dev);
	pm_runtime_put_autosuspend(rsb->dev);

	return ret;
}

static int sunxi_rsb_write(struct sunxi_rsb *rsb, u8 rtaddr, u8 addr,
			   const u32 *buf, size_t len)
{
	u32 cmd;
	int ret;

	if (!buf)
		return -EINVAL;

	switch (len) {
	case 1:
		cmd = RSB_CMD_WR8;
		break;
	case 2:
		cmd = RSB_CMD_WR16;
		break;
	case 4:
		cmd = RSB_CMD_WR32;
		break;
	default:
		dev_err(rsb->dev, "Invalid access width: %zd\n", len);
		return -EINVAL;
	}

	ret = pm_runtime_resume_and_get(rsb->dev);
	if (ret)
		return ret;

	mutex_lock(&rsb->lock);

	writel(addr, rsb->regs + RSB_ADDR);
	writel(RSB_DAR_RTA(rtaddr), rsb->regs + RSB_DAR);
	writel(*buf, rsb->regs + RSB_DATA);
	writel(cmd, rsb->regs + RSB_CMD);
	ret = _sunxi_rsb_run_xfer(rsb);

	mutex_unlock(&rsb->lock);

	pm_runtime_mark_last_busy(rsb->dev);
	pm_runtime_put_autosuspend(rsb->dev);

	return ret;
}

/* RSB regmap functions */
struct sunxi_rsb_ctx {
	struct sunxi_rsb_device *rdev;
	int size;
};

static int regmap_sunxi_rsb_reg_read(void *context, unsigned int reg,
				     unsigned int *val)
{
	struct sunxi_rsb_ctx *ctx = context;
	struct sunxi_rsb_device *rdev = ctx->rdev;

	if (reg > 0xff)
		return -EINVAL;

	return sunxi_rsb_read(rdev->rsb, rdev->rtaddr, reg, val, ctx->size);
}

static int regmap_sunxi_rsb_reg_write(void *context, unsigned int reg,
				      unsigned int val)
{
	struct sunxi_rsb_ctx *ctx = context;
	struct sunxi_rsb_device *rdev = ctx->rdev;

	return sunxi_rsb_write(rdev->rsb, rdev->rtaddr, reg, &val, ctx->size);
}

static void regmap_sunxi_rsb_free_ctx(void *context)
{
	struct sunxi_rsb_ctx *ctx = context;

	kfree(ctx);
}

static struct regmap_bus regmap_sunxi_rsb = {
	.reg_write = regmap_sunxi_rsb_reg_write,
	.reg_read = regmap_sunxi_rsb_reg_read,
	.free_context = regmap_sunxi_rsb_free_ctx,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

static struct sunxi_rsb_ctx *regmap_sunxi_rsb_init_ctx(struct sunxi_rsb_device *rdev,
		const struct regmap_config *config)
{
	struct sunxi_rsb_ctx *ctx;

	switch (config->val_bits) {
	case 8:
	case 16:
	case 32:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->rdev = rdev;
	ctx->size = config->val_bits / 8;

	return ctx;
}

struct regmap *__devm_regmap_init_sunxi_rsb(struct sunxi_rsb_device *rdev,
					    const struct regmap_config *config,
					    struct lock_class_key *lock_key,
					    const char *lock_name)
{
	struct sunxi_rsb_ctx *ctx = regmap_sunxi_rsb_init_ctx(rdev, config);

	if (IS_ERR(ctx))
		return ERR_CAST(ctx);

	return __devm_regmap_init(&rdev->dev, &regmap_sunxi_rsb, ctx, config,
				  lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_sunxi_rsb);

/* RSB controller driver functions */
static irqreturn_t sunxi_rsb_irq(int irq, void *dev_id)
{
	struct sunxi_rsb *rsb = dev_id;
	u32 status;

	status = readl(rsb->regs + RSB_INTS);
	rsb->status = status;

	/* Clear interrupts */
	status &= (RSB_INTS_LOAD_BSY | RSB_INTS_TRANS_ERR |
		   RSB_INTS_TRANS_OVER);
	writel(status, rsb->regs + RSB_INTS);

	complete(&rsb->complete);

	return IRQ_HANDLED;
}

static int sunxi_rsb_init_device_mode(struct sunxi_rsb *rsb)
{
	int ret = 0;
	u32 reg;

	/* send init sequence */
	writel(RSB_DMCR_DEVICE_START | RSB_DMCR_MODE_DATA |
	       RSB_DMCR_MODE_REG | RSB_DMCR_DEV_ADDR, rsb->regs + RSB_DMCR);

	readl_poll_timeout(rsb->regs + RSB_DMCR, reg,
			   !(reg & RSB_DMCR_DEVICE_START), 100, 250000);
	if (reg & RSB_DMCR_DEVICE_START)
		ret = -ETIMEDOUT;

	/* clear interrupt status bits */
	writel(readl(rsb->regs + RSB_INTS), rsb->regs + RSB_INTS);

	return ret;
}

/*
 * There are 15 valid runtime addresses, though Allwinner typically
 * skips the first, for unknown reasons, and uses the following three.
 *
 * 0x17, 0x2d, 0x3a, 0x4e, 0x59, 0x63, 0x74, 0x8b,
 * 0x9c, 0xa6, 0xb1, 0xc5, 0xd2, 0xe8, 0xff
 *
 * No designs with 2 RSB slave devices sharing identical hardware
 * addresses on the same bus have been seen in the wild. All designs
 * use 0x2d for the primary PMIC, 0x3a for the secondary PMIC if
 * there is one, and 0x45 for peripheral ICs.
 *
 * The hardware does not seem to support re-setting runtime addresses.
 * Attempts to do so result in the slave devices returning a NACK.
 * Hence we just hardcode the mapping here, like Allwinner does.
 */

static const struct sunxi_rsb_addr_map sunxi_rsb_addr_maps[] = {
	{ 0x3a3, 0x2d }, /* Primary PMIC: AXP223, AXP809, AXP81X, ... */
	{ 0x745, 0x3a }, /* Secondary PMIC: AXP806, ... */
	{ 0xe89, 0x4e }, /* Peripheral IC: AC100, ... */
};

static u8 sunxi_rsb_get_rtaddr(u16 hwaddr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sunxi_rsb_addr_maps); i++)
		if (hwaddr == sunxi_rsb_addr_maps[i].hwaddr)
			return sunxi_rsb_addr_maps[i].rtaddr;

	return 0; /* 0 is an invalid runtime address */
}

static int of_rsb_register_devices(struct sunxi_rsb *rsb)
{
	struct device *dev = rsb->dev;
	struct device_node *child, *np = dev->of_node;
	u32 hwaddr;
	u8 rtaddr;
	int ret;

	if (!np)
		return -EINVAL;

	/* Runtime addresses for all slaves should be set first */
	for_each_available_child_of_node(np, child) {
		dev_dbg(dev, "setting child %pOF runtime address\n",
			child);

		ret = of_property_read_u32(child, "reg", &hwaddr);
		if (ret) {
			dev_err(dev, "%pOF: invalid 'reg' property: %d\n",
				child, ret);
			continue;
		}

		rtaddr = sunxi_rsb_get_rtaddr(hwaddr);
		if (!rtaddr) {
			dev_err(dev, "%pOF: unknown hardware device address\n",
				child);
			continue;
		}

		/*
		 * Since no devices have been registered yet, we are the
		 * only ones using the bus, we can skip locking the bus.
		 */

		/* setup command parameters */
		writel(RSB_CMD_STRA, rsb->regs + RSB_CMD);
		writel(RSB_DAR_RTA(rtaddr) | RSB_DAR_DA(hwaddr),
		       rsb->regs + RSB_DAR);

		/* send command */
		ret = _sunxi_rsb_run_xfer(rsb);
		if (ret)
			dev_warn(dev, "%pOF: set runtime address failed: %d\n",
				 child, ret);
	}

	/* Then we start adding devices and probing them */
	for_each_available_child_of_node(np, child) {
		struct sunxi_rsb_device *rdev;

		dev_dbg(dev, "adding child %pOF\n", child);

		ret = of_property_read_u32(child, "reg", &hwaddr);
		if (ret)
			continue;

		rtaddr = sunxi_rsb_get_rtaddr(hwaddr);
		if (!rtaddr)
			continue;

		rdev = sunxi_rsb_device_create(rsb, child, hwaddr, rtaddr);
		if (IS_ERR(rdev))
			dev_err(dev, "failed to add child device %pOF: %ld\n",
				child, PTR_ERR(rdev));
	}

	return 0;
}

static int sunxi_rsb_hw_init(struct sunxi_rsb *rsb)
{
	struct device *dev = rsb->dev;
	unsigned long p_clk_freq;
	u32 clk_delay, reg;
	int clk_div, ret;

	ret = clk_prepare_enable(rsb->clk);
	if (ret) {
		dev_err(dev, "failed to enable clk: %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(rsb->rstc);
	if (ret) {
		dev_err(dev, "failed to deassert reset line: %d\n", ret);
		goto err_clk_disable;
	}

	/* reset the controller */
	writel(RSB_CTRL_SOFT_RST, rsb->regs + RSB_CTRL);
	readl_poll_timeout(rsb->regs + RSB_CTRL, reg,
			   !(reg & RSB_CTRL_SOFT_RST), 1000, 100000);

	/*
	 * Clock frequency and delay calculation code is from
	 * Allwinner U-boot sources.
	 *
	 * From A83 user manual:
	 * bus clock frequency = parent clock frequency / (2 * (divider + 1))
	 */
	p_clk_freq = clk_get_rate(rsb->clk);
	clk_div = p_clk_freq / rsb->clk_freq / 2;
	if (!clk_div)
		clk_div = 1;
	else if (clk_div > RSB_CCR_MAX_CLK_DIV + 1)
		clk_div = RSB_CCR_MAX_CLK_DIV + 1;

	clk_delay = clk_div >> 1;
	if (!clk_delay)
		clk_delay = 1;

	dev_info(dev, "RSB running at %lu Hz\n", p_clk_freq / clk_div / 2);
	writel(RSB_CCR_SDA_OUT_DELAY(clk_delay) | RSB_CCR_CLK_DIV(clk_div - 1),
	       rsb->regs + RSB_CCR);

	return 0;

err_clk_disable:
	clk_disable_unprepare(rsb->clk);

	return ret;
}

static void sunxi_rsb_hw_exit(struct sunxi_rsb *rsb)
{
	reset_control_assert(rsb->rstc);

	/* Keep the clock and PM reference counts consistent. */
	if (!pm_runtime_status_suspended(rsb->dev))
		clk_disable_unprepare(rsb->clk);
}

static int __maybe_unused sunxi_rsb_runtime_suspend(struct device *dev)
{
	struct sunxi_rsb *rsb = dev_get_drvdata(dev);

	clk_disable_unprepare(rsb->clk);

	return 0;
}

static int __maybe_unused sunxi_rsb_runtime_resume(struct device *dev)
{
	struct sunxi_rsb *rsb = dev_get_drvdata(dev);

	return clk_prepare_enable(rsb->clk);
}

static int __maybe_unused sunxi_rsb_suspend(struct device *dev)
{
	struct sunxi_rsb *rsb = dev_get_drvdata(dev);

	sunxi_rsb_hw_exit(rsb);

	return 0;
}

static int __maybe_unused sunxi_rsb_resume(struct device *dev)
{
	struct sunxi_rsb *rsb = dev_get_drvdata(dev);

	return sunxi_rsb_hw_init(rsb);
}

static int sunxi_rsb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *r;
	struct sunxi_rsb *rsb;
	u32 clk_freq = 3000000;
	int irq, ret;

	of_property_read_u32(np, "clock-frequency", &clk_freq);
	if (clk_freq > RSB_MAX_FREQ) {
		dev_err(dev,
			"clock-frequency (%u Hz) is too high (max = 20MHz)\n",
			clk_freq);
		return -EINVAL;
	}

	rsb = devm_kzalloc(dev, sizeof(*rsb), GFP_KERNEL);
	if (!rsb)
		return -ENOMEM;

	rsb->dev = dev;
	rsb->clk_freq = clk_freq;
	platform_set_drvdata(pdev, rsb);
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rsb->regs = devm_ioremap_resource(dev, r);
	if (IS_ERR(rsb->regs))
		return PTR_ERR(rsb->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	rsb->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(rsb->clk)) {
		ret = PTR_ERR(rsb->clk);
		dev_err(dev, "failed to retrieve clk: %d\n", ret);
		return ret;
	}

	rsb->rstc = devm_reset_control_get(dev, NULL);
	if (IS_ERR(rsb->rstc)) {
		ret = PTR_ERR(rsb->rstc);
		dev_err(dev, "failed to retrieve reset controller: %d\n", ret);
		return ret;
	}

	init_completion(&rsb->complete);
	mutex_init(&rsb->lock);

	ret = devm_request_irq(dev, irq, sunxi_rsb_irq, 0, RSB_CTRL_NAME, rsb);
	if (ret) {
		dev_err(dev, "can't register interrupt handler irq %d: %d\n",
			irq, ret);
		return ret;
	}

	ret = sunxi_rsb_hw_init(rsb);
	if (ret)
		return ret;

	/* initialize all devices on the bus into RSB mode */
	ret = sunxi_rsb_init_device_mode(rsb);
	if (ret)
		dev_warn(dev, "Initialize device mode failed: %d\n", ret);

	pm_suspend_ignore_children(dev, true);
	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, MSEC_PER_SEC);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	of_rsb_register_devices(rsb);

	return 0;
}

static int sunxi_rsb_remove(struct platform_device *pdev)
{
	struct sunxi_rsb *rsb = platform_get_drvdata(pdev);

	device_for_each_child(rsb->dev, NULL, sunxi_rsb_remove_devices);
	pm_runtime_disable(&pdev->dev);
	sunxi_rsb_hw_exit(rsb);

	return 0;
}

static void sunxi_rsb_shutdown(struct platform_device *pdev)
{
	struct sunxi_rsb *rsb = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	sunxi_rsb_hw_exit(rsb);
}

static const struct dev_pm_ops sunxi_rsb_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(sunxi_rsb_runtime_suspend,
			   sunxi_rsb_runtime_resume, NULL)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(sunxi_rsb_suspend, sunxi_rsb_resume)
};

static const struct of_device_id sunxi_rsb_of_match_table[] = {
	{ .compatible = "allwinner,sun8i-a23-rsb" },
	{}
};
MODULE_DEVICE_TABLE(of, sunxi_rsb_of_match_table);

static struct platform_driver sunxi_rsb_driver = {
	.probe = sunxi_rsb_probe,
	.remove	= sunxi_rsb_remove,
	.shutdown = sunxi_rsb_shutdown,
	.driver	= {
		.name = RSB_CTRL_NAME,
		.of_match_table = sunxi_rsb_of_match_table,
		.pm = &sunxi_rsb_dev_pm_ops,
	},
};

static int __init sunxi_rsb_init(void)
{
	int ret;

	ret = bus_register(&sunxi_rsb_bus);
	if (ret) {
		pr_err("failed to register sunxi sunxi_rsb bus: %d\n", ret);
		return ret;
	}

	return platform_driver_register(&sunxi_rsb_driver);
}
module_init(sunxi_rsb_init);

static void __exit sunxi_rsb_exit(void)
{
	platform_driver_unregister(&sunxi_rsb_driver);
	bus_unregister(&sunxi_rsb_bus);
}
module_exit(sunxi_rsb_exit);

MODULE_AUTHOR("Chen-Yu Tsai <wens@csie.org>");
MODULE_DESCRIPTION("Allwinner sunXi Reduced Serial Bus controller driver");
MODULE_LICENSE("GPL v2");
