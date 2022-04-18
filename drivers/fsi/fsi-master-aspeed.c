// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) IBM Corporation 2018
// FSI master driver for AST2600

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/fsi.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/iopoll.h>
#include <linux/gpio/consumer.h>

#include "fsi-master.h"

struct fsi_master_aspeed {
	struct fsi_master	master;
	struct mutex		lock;	/* protect HW access */
	struct device		*dev;
	void __iomem		*base;
	struct clk		*clk;
	struct gpio_desc	*cfam_reset_gpio;
};

#define to_fsi_master_aspeed(m) \
	container_of(m, struct fsi_master_aspeed, master)

/* Control register (size 0x400) */
static const u32 ctrl_base = 0x80000000;

static const u32 fsi_base = 0xa0000000;

#define OPB_FSI_VER	0x00
#define OPB_TRIGGER	0x04
#define OPB_CTRL_BASE	0x08
#define OPB_FSI_BASE	0x0c
#define OPB_CLK_SYNC	0x3c
#define OPB_IRQ_CLEAR	0x40
#define OPB_IRQ_MASK	0x44
#define OPB_IRQ_STATUS	0x48

#define OPB0_SELECT	0x10
#define OPB0_RW		0x14
#define OPB0_XFER_SIZE	0x18
#define OPB0_FSI_ADDR	0x1c
#define OPB0_FSI_DATA_W	0x20
#define OPB0_STATUS	0x80
#define OPB0_FSI_DATA_R	0x84

#define OPB0_WRITE_ORDER1	0x4c
#define OPB0_WRITE_ORDER2	0x50
#define OPB1_WRITE_ORDER1	0x54
#define OPB1_WRITE_ORDER2	0x58
#define OPB0_READ_ORDER1	0x5c
#define OPB1_READ_ORDER2	0x60

#define OPB_RETRY_COUNTER	0x64

/* OPBn_STATUS */
#define STATUS_HALFWORD_ACK	BIT(0)
#define STATUS_FULLWORD_ACK	BIT(1)
#define STATUS_ERR_ACK		BIT(2)
#define STATUS_RETRY		BIT(3)
#define STATUS_TIMEOUT		BIT(4)

/* OPB_IRQ_MASK */
#define OPB1_XFER_ACK_EN BIT(17)
#define OPB0_XFER_ACK_EN BIT(16)

/* OPB_RW */
#define CMD_READ	BIT(0)
#define CMD_WRITE	0

/* OPBx_XFER_SIZE */
#define XFER_FULLWORD	(BIT(1) | BIT(0))
#define XFER_HALFWORD	(BIT(0))
#define XFER_BYTE	(0)

#define CREATE_TRACE_POINTS
#include <trace/events/fsi_master_aspeed.h>

#define FSI_LINK_ENABLE_SETUP_TIME	10	/* in mS */

/* Run the bus at maximum speed by default */
#define FSI_DIVISOR_DEFAULT            1
#define FSI_DIVISOR_CABLED             2
static u16 aspeed_fsi_divisor = FSI_DIVISOR_DEFAULT;
module_param_named(bus_div,aspeed_fsi_divisor, ushort, 0);

#define OPB_POLL_TIMEOUT		10000

static int __opb_write(struct fsi_master_aspeed *aspeed, u32 addr,
		       u32 val, u32 transfer_size)
{
	void __iomem *base = aspeed->base;
	u32 reg, status;
	int ret;

	writel(CMD_WRITE, base + OPB0_RW);
	writel(transfer_size, base + OPB0_XFER_SIZE);
	writel(addr, base + OPB0_FSI_ADDR);
	writel(val, base + OPB0_FSI_DATA_W);
	writel(0x1, base + OPB_IRQ_CLEAR);
	writel(0x1, base + OPB_TRIGGER);

	ret = readl_poll_timeout(base + OPB_IRQ_STATUS, reg,
				(reg & OPB0_XFER_ACK_EN) != 0,
				0, OPB_POLL_TIMEOUT);

	status = readl(base + OPB0_STATUS);

	trace_fsi_master_aspeed_opb_write(addr, val, transfer_size, status, reg);

	/* Return error when poll timed out */
	if (ret)
		return ret;

	/* Command failed, master will reset */
	if (status & STATUS_ERR_ACK)
		return -EIO;

	return 0;
}

static int opb_writeb(struct fsi_master_aspeed *aspeed, u32 addr, u8 val)
{
	return __opb_write(aspeed, addr, val, XFER_BYTE);
}

static int opb_writew(struct fsi_master_aspeed *aspeed, u32 addr, __be16 val)
{
	return __opb_write(aspeed, addr, (__force u16)val, XFER_HALFWORD);
}

static int opb_writel(struct fsi_master_aspeed *aspeed, u32 addr, __be32 val)
{
	return __opb_write(aspeed, addr, (__force u32)val, XFER_FULLWORD);
}

static int __opb_read(struct fsi_master_aspeed *aspeed, uint32_t addr,
		      u32 transfer_size, void *out)
{
	void __iomem *base = aspeed->base;
	u32 result, reg;
	int status, ret;

	writel(CMD_READ, base + OPB0_RW);
	writel(transfer_size, base + OPB0_XFER_SIZE);
	writel(addr, base + OPB0_FSI_ADDR);
	writel(0x1, base + OPB_IRQ_CLEAR);
	writel(0x1, base + OPB_TRIGGER);

	ret = readl_poll_timeout(base + OPB_IRQ_STATUS, reg,
			   (reg & OPB0_XFER_ACK_EN) != 0,
			   0, OPB_POLL_TIMEOUT);

	status = readl(base + OPB0_STATUS);

	result = readl(base + OPB0_FSI_DATA_R);

	trace_fsi_master_aspeed_opb_read(addr, transfer_size, result,
			readl(base + OPB0_STATUS),
			reg);

	/* Return error when poll timed out */
	if (ret)
		return ret;

	/* Command failed, master will reset */
	if (status & STATUS_ERR_ACK)
		return -EIO;

	if (out) {
		switch (transfer_size) {
		case XFER_BYTE:
			*(u8 *)out = result;
			break;
		case XFER_HALFWORD:
			*(u16 *)out = result;
			break;
		case XFER_FULLWORD:
			*(u32 *)out = result;
			break;
		default:
			return -EINVAL;
		}

	}

	return 0;
}

static int opb_readl(struct fsi_master_aspeed *aspeed, uint32_t addr, __be32 *out)
{
	return __opb_read(aspeed, addr, XFER_FULLWORD, out);
}

static int opb_readw(struct fsi_master_aspeed *aspeed, uint32_t addr, __be16 *out)
{
	return __opb_read(aspeed, addr, XFER_HALFWORD, (void *)out);
}

static int opb_readb(struct fsi_master_aspeed *aspeed, uint32_t addr, u8 *out)
{
	return __opb_read(aspeed, addr, XFER_BYTE, (void *)out);
}

static int check_errors(struct fsi_master_aspeed *aspeed, int err)
{
	int ret;

	if (trace_fsi_master_aspeed_opb_error_enabled()) {
		__be32 mresp0, mstap0, mesrb0;

		opb_readl(aspeed, ctrl_base + FSI_MRESP0, &mresp0);
		opb_readl(aspeed, ctrl_base + FSI_MSTAP0, &mstap0);
		opb_readl(aspeed, ctrl_base + FSI_MESRB0, &mesrb0);

		trace_fsi_master_aspeed_opb_error(
				be32_to_cpu(mresp0),
				be32_to_cpu(mstap0),
				be32_to_cpu(mesrb0));
	}

	if (err == -EIO) {
		/* Check MAEB (0x70) ? */

		/* Then clear errors in master */
		ret = opb_writel(aspeed, ctrl_base + FSI_MRESP0,
				cpu_to_be32(FSI_MRESP_RST_ALL_MASTER));
		if (ret) {
			/* TODO: log? return different code? */
			return ret;
		}
		/* TODO: confirm that 0x70 was okay */
	}

	/* This will pass through timeout errors */
	return err;
}

static int aspeed_master_read(struct fsi_master *master, int link,
			uint8_t id, uint32_t addr, void *val, size_t size)
{
	struct fsi_master_aspeed *aspeed = to_fsi_master_aspeed(master);
	int ret;

	if (id > 0x3)
		return -EINVAL;

	addr |= id << 21;
	addr += link * FSI_HUB_LINK_SIZE;

	mutex_lock(&aspeed->lock);

	switch (size) {
	case 1:
		ret = opb_readb(aspeed, fsi_base + addr, val);
		break;
	case 2:
		ret = opb_readw(aspeed, fsi_base + addr, val);
		break;
	case 4:
		ret = opb_readl(aspeed, fsi_base + addr, val);
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	ret = check_errors(aspeed, ret);
done:
	mutex_unlock(&aspeed->lock);
	return ret;
}

static int aspeed_master_write(struct fsi_master *master, int link,
			uint8_t id, uint32_t addr, const void *val, size_t size)
{
	struct fsi_master_aspeed *aspeed = to_fsi_master_aspeed(master);
	int ret;

	if (id > 0x3)
		return -EINVAL;

	addr |= id << 21;
	addr += link * FSI_HUB_LINK_SIZE;

	mutex_lock(&aspeed->lock);

	switch (size) {
	case 1:
		ret = opb_writeb(aspeed, fsi_base + addr, *(u8 *)val);
		break;
	case 2:
		ret = opb_writew(aspeed, fsi_base + addr, *(__be16 *)val);
		break;
	case 4:
		ret = opb_writel(aspeed, fsi_base + addr, *(__be32 *)val);
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	ret = check_errors(aspeed, ret);
done:
	mutex_unlock(&aspeed->lock);
	return ret;
}

static int aspeed_master_link_enable(struct fsi_master *master, int link,
				     bool enable)
{
	struct fsi_master_aspeed *aspeed = to_fsi_master_aspeed(master);
	int idx, bit, ret;
	__be32 reg;

	idx = link / 32;
	bit = link % 32;

	reg = cpu_to_be32(0x80000000 >> bit);

	mutex_lock(&aspeed->lock);

	if (!enable) {
		ret = opb_writel(aspeed, ctrl_base + FSI_MCENP0 + (4 * idx), reg);
		goto done;
	}

	ret = opb_writel(aspeed, ctrl_base + FSI_MSENP0 + (4 * idx), reg);
	if (ret)
		goto done;

	mdelay(FSI_LINK_ENABLE_SETUP_TIME);
done:
	mutex_unlock(&aspeed->lock);
	return ret;
}

static int aspeed_master_term(struct fsi_master *master, int link, uint8_t id)
{
	uint32_t addr;
	__be32 cmd;

	addr = 0x4;
	cmd = cpu_to_be32(0xecc00000);

	return aspeed_master_write(master, link, id, addr, &cmd, 4);
}

static int aspeed_master_break(struct fsi_master *master, int link)
{
	uint32_t addr;
	__be32 cmd;

	addr = 0x0;
	cmd = cpu_to_be32(0xc0de0000);

	return aspeed_master_write(master, link, 0, addr, &cmd, 4);
}

static void aspeed_master_release(struct device *dev)
{
	struct fsi_master_aspeed *aspeed =
		to_fsi_master_aspeed(dev_to_fsi_master(dev));

	kfree(aspeed);
}

/* mmode encoders */
static inline u32 fsi_mmode_crs0(u32 x)
{
	return (x & FSI_MMODE_CRS0MASK) << FSI_MMODE_CRS0SHFT;
}

static inline u32 fsi_mmode_crs1(u32 x)
{
	return (x & FSI_MMODE_CRS1MASK) << FSI_MMODE_CRS1SHFT;
}

static int aspeed_master_init(struct fsi_master_aspeed *aspeed)
{
	__be32 reg;

	reg = cpu_to_be32(FSI_MRESP_RST_ALL_MASTER | FSI_MRESP_RST_ALL_LINK
			| FSI_MRESP_RST_MCR | FSI_MRESP_RST_PYE);
	opb_writel(aspeed, ctrl_base + FSI_MRESP0, reg);

	/* Initialize the MFSI (hub master) engine */
	reg = cpu_to_be32(FSI_MRESP_RST_ALL_MASTER | FSI_MRESP_RST_ALL_LINK
			| FSI_MRESP_RST_MCR | FSI_MRESP_RST_PYE);
	opb_writel(aspeed, ctrl_base + FSI_MRESP0, reg);

	reg = cpu_to_be32(FSI_MECTRL_EOAE | FSI_MECTRL_P8_AUTO_TERM);
	opb_writel(aspeed, ctrl_base + FSI_MECTRL, reg);

	reg = cpu_to_be32(FSI_MMODE_ECRC | FSI_MMODE_EPC | FSI_MMODE_RELA
			| fsi_mmode_crs0(aspeed_fsi_divisor)
			| fsi_mmode_crs1(aspeed_fsi_divisor)
			| FSI_MMODE_P8_TO_LSB);
	dev_info(aspeed->dev, "mmode set to %08x (divisor %d)\n",
			be32_to_cpu(reg), aspeed_fsi_divisor);
	opb_writel(aspeed, ctrl_base + FSI_MMODE, reg);

	reg = cpu_to_be32(0xffff0000);
	opb_writel(aspeed, ctrl_base + FSI_MDLYR, reg);

	reg = cpu_to_be32(~0);
	opb_writel(aspeed, ctrl_base + FSI_MSENP0, reg);

	/* Leave enabled long enough for master logic to set up */
	mdelay(FSI_LINK_ENABLE_SETUP_TIME);

	opb_writel(aspeed, ctrl_base + FSI_MCENP0, reg);

	opb_readl(aspeed, ctrl_base + FSI_MAEB, NULL);

	reg = cpu_to_be32(FSI_MRESP_RST_ALL_MASTER | FSI_MRESP_RST_ALL_LINK);
	opb_writel(aspeed, ctrl_base + FSI_MRESP0, reg);

	opb_readl(aspeed, ctrl_base + FSI_MLEVP0, NULL);

	/* Reset the master bridge */
	reg = cpu_to_be32(FSI_MRESB_RST_GEN);
	opb_writel(aspeed, ctrl_base + FSI_MRESB0, reg);

	reg = cpu_to_be32(FSI_MRESB_RST_ERR);
	opb_writel(aspeed, ctrl_base + FSI_MRESB0, reg);

	return 0;
}

static ssize_t cfam_reset_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct fsi_master_aspeed *aspeed = dev_get_drvdata(dev);

	mutex_lock(&aspeed->lock);
	gpiod_set_value(aspeed->cfam_reset_gpio, 1);
	usleep_range(900, 1000);
	gpiod_set_value(aspeed->cfam_reset_gpio, 0);
	mutex_unlock(&aspeed->lock);

	return count;
}

static DEVICE_ATTR(cfam_reset, 0200, NULL, cfam_reset_store);

static int setup_cfam_reset(struct fsi_master_aspeed *aspeed)
{
	struct device *dev = aspeed->dev;
	struct gpio_desc *gpio;
	int rc;

	gpio = devm_gpiod_get_optional(dev, "cfam-reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);
	if (!gpio)
		return 0;

	aspeed->cfam_reset_gpio = gpio;

	rc = device_create_file(dev, &dev_attr_cfam_reset);
	if (rc) {
		devm_gpiod_put(dev, gpio);
		return rc;
	}

	return 0;
}

static int tacoma_cabled_fsi_fixup(struct device *dev)
{
	struct gpio_desc *routing_gpio, *mux_gpio;
	int gpio;

	/*
	 * The routing GPIO is a jumper indicating we should mux for the
	 * externally connected FSI cable.
	 */
	routing_gpio = devm_gpiod_get_optional(dev, "fsi-routing",
			GPIOD_IN | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(routing_gpio))
		return PTR_ERR(routing_gpio);
	if (!routing_gpio)
		return 0;

	mux_gpio = devm_gpiod_get_optional(dev, "fsi-mux", GPIOD_ASIS);
	if (IS_ERR(mux_gpio))
		return PTR_ERR(mux_gpio);
	if (!mux_gpio)
		return 0;

	gpio = gpiod_get_value(routing_gpio);
	if (gpio < 0)
		return gpio;

	/* If the routing GPIO is high we should set the mux to low. */
	if (gpio) {
		/*
		 * Cable signal integrity means we should run the bus
		 * slightly slower. Do not override if a kernel param
		 * has already overridden.
		 */
		if (aspeed_fsi_divisor == FSI_DIVISOR_DEFAULT)
			aspeed_fsi_divisor = FSI_DIVISOR_CABLED;

		gpiod_direction_output(mux_gpio, 0);
		dev_info(dev, "FSI configured for external cable\n");
	} else {
		gpiod_direction_output(mux_gpio, 1);
	}

	devm_gpiod_put(dev, routing_gpio);

	return 0;
}

static int fsi_master_aspeed_probe(struct platform_device *pdev)
{
	struct fsi_master_aspeed *aspeed;
	int rc, links, reg;
	__be32 raw;

	rc = tacoma_cabled_fsi_fixup(&pdev->dev);
	if (rc) {
		dev_err(&pdev->dev, "Tacoma FSI cable fixup failed\n");
		return rc;
	}

	aspeed = kzalloc(sizeof(*aspeed), GFP_KERNEL);
	if (!aspeed)
		return -ENOMEM;

	aspeed->dev = &pdev->dev;

	aspeed->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(aspeed->base)) {
		rc = PTR_ERR(aspeed->base);
		goto err_free_aspeed;
	}

	aspeed->clk = devm_clk_get(aspeed->dev, NULL);
	if (IS_ERR(aspeed->clk)) {
		dev_err(aspeed->dev, "couldn't get clock\n");
		rc = PTR_ERR(aspeed->clk);
		goto err_free_aspeed;
	}
	rc = clk_prepare_enable(aspeed->clk);
	if (rc) {
		dev_err(aspeed->dev, "couldn't enable clock\n");
		goto err_free_aspeed;
	}

	rc = setup_cfam_reset(aspeed);
	if (rc) {
		dev_err(&pdev->dev, "CFAM reset GPIO setup failed\n");
	}

	writel(0x1, aspeed->base + OPB_CLK_SYNC);
	writel(OPB1_XFER_ACK_EN | OPB0_XFER_ACK_EN,
			aspeed->base + OPB_IRQ_MASK);

	/* TODO: determine an appropriate value */
	writel(0x10, aspeed->base + OPB_RETRY_COUNTER);

	writel(ctrl_base, aspeed->base + OPB_CTRL_BASE);
	writel(fsi_base, aspeed->base + OPB_FSI_BASE);

	/* Set read data order */
	writel(0x00030b1b, aspeed->base + OPB0_READ_ORDER1);

	/* Set write data order */
	writel(0x0011101b, aspeed->base + OPB0_WRITE_ORDER1);
	writel(0x0c330f3f, aspeed->base + OPB0_WRITE_ORDER2);

	/*
	 * Select OPB0 for all operations.
	 * Will need to be reworked when enabling DMA or anything that uses
	 * OPB1.
	 */
	writel(0x1, aspeed->base + OPB0_SELECT);

	rc = opb_readl(aspeed, ctrl_base + FSI_MVER, &raw);
	if (rc) {
		dev_err(&pdev->dev, "failed to read hub version\n");
		goto err_release;
	}

	reg = be32_to_cpu(raw);
	links = (reg >> 8) & 0xff;
	dev_info(&pdev->dev, "hub version %08x (%d links)\n", reg, links);

	aspeed->master.dev.parent = &pdev->dev;
	aspeed->master.dev.release = aspeed_master_release;
	aspeed->master.dev.of_node = of_node_get(dev_of_node(&pdev->dev));

	aspeed->master.n_links = links;
	aspeed->master.read = aspeed_master_read;
	aspeed->master.write = aspeed_master_write;
	aspeed->master.send_break = aspeed_master_break;
	aspeed->master.term = aspeed_master_term;
	aspeed->master.link_enable = aspeed_master_link_enable;

	dev_set_drvdata(&pdev->dev, aspeed);

	mutex_init(&aspeed->lock);
	aspeed_master_init(aspeed);

	rc = fsi_master_register(&aspeed->master);
	if (rc)
		goto err_release;

	/* At this point, fsi_master_register performs the device_initialize(),
	 * and holds the sole reference on master.dev. This means the device
	 * will be freed (via ->release) during any subsequent call to
	 * fsi_master_unregister.  We add our own reference to it here, so we
	 * can perform cleanup (in _remove()) without it being freed before
	 * we're ready.
	 */
	get_device(&aspeed->master.dev);
	return 0;

err_release:
	clk_disable_unprepare(aspeed->clk);
err_free_aspeed:
	kfree(aspeed);
	return rc;
}

static int fsi_master_aspeed_remove(struct platform_device *pdev)
{
	struct fsi_master_aspeed *aspeed = platform_get_drvdata(pdev);

	fsi_master_unregister(&aspeed->master);
	clk_disable_unprepare(aspeed->clk);

	return 0;
}

static const struct of_device_id fsi_master_aspeed_match[] = {
	{ .compatible = "aspeed,ast2600-fsi-master" },
	{ },
};
MODULE_DEVICE_TABLE(of, fsi_master_aspeed_match);

static struct platform_driver fsi_master_aspeed_driver = {
	.driver = {
		.name		= "fsi-master-aspeed",
		.of_match_table	= fsi_master_aspeed_match,
	},
	.probe	= fsi_master_aspeed_probe,
	.remove = fsi_master_aspeed_remove,
};

module_platform_driver(fsi_master_aspeed_driver);
MODULE_LICENSE("GPL");
