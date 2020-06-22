// SPDX-License-Identifier:     GPL-2.0+
/*
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/initramfs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/rockchip/rockchip_decompress.h>

#define DECOM_CTRL		0x0
#define DECOM_ENR		0x4
#define DECOM_RADDR		0x8
#define DECOM_WADDR		0xc
#define DECOM_UDDSL		0x10
#define DECOM_UDDSH		0x14
#define DECOM_TXTHR		0x18
#define DECOM_RXTHR		0x1c
#define DECOM_SLEN		0x20
#define DECOM_STAT		0x24
#define DECOM_ISR		0x28
#define DECOM_IEN		0x2c
#define DECOM_AXI_STAT		0x30
#define DECOM_TSIZEL		0x34
#define DECOM_TSIZEH		0x38
#define DECOM_MGNUM		0x3c
#define DECOM_FRAME		0x40
#define DECOM_DICTID		0x44
#define DECOM_CSL		0x48
#define DECOM_CSH		0x4c
#define DECOM_LMTSL		0x50
#define DECOM_LMTSH		0x54

#define LZ4_HEAD_CSUM_CHECK_EN	BIT(1)
#define LZ4_BLOCK_CSUM_CHECK_EN	BIT(2)
#define LZ4_CONT_CSUM_CHECK_EN	BIT(3)

#define DSOLIEN			BIT(19)
#define ZDICTEIEN		BIT(18)
#define GCMEIEN			BIT(17)
#define GIDEIEN			BIT(16)
#define CCCEIEN			BIT(15)
#define BCCEIEN			BIT(14)
#define HCCEIEN			BIT(13)
#define CSEIEN			BIT(12)
#define DICTEIEN		BIT(11)
#define VNEIEN			BIT(10)
#define WNEIEN			BIT(9)
#define RDCEIEN			BIT(8)
#define WRCEIEN			BIT(7)
#define DISEIEN			BIT(6)
#define LENEIEN			BIT(5)
#define LITEIEN			BIT(4)
#define SQMEIEN			BIT(3)
#define SLCIEN			BIT(2)
#define HDEIEN			BIT(1)
#define DSIEN			BIT(0)

#define DECOM_STOP		BIT(0)
#define DECOM_COMPLETE		BIT(0)
#define DECOM_GZIP_MODE		BIT(4)
#define DECOM_ZLIB_MODE		BIT(5)
#define DECOM_DEFLATE_MODE	BIT(0)

#define DECOM_ENABLE		0x1
#define DECOM_DISABLE		0x0

#define DECOM_INT_MASK \
	(DSOLIEN | ZDICTEIEN | GCMEIEN | GIDEIEN | \
	CCCEIEN | BCCEIEN | HCCEIEN | CSEIEN | \
	DICTEIEN | VNEIEN | WNEIEN | RDCEIEN | WRCEIEN | \
	DISEIEN | LENEIEN | LITEIEN | SQMEIEN | SLCIEN | \
	HDEIEN | DSIEN)

struct rk_decom {
	struct device *dev;
	int irq;
	int num_clocks;
	struct clk_bulk_data *clocks;
	void __iomem *regs;
	phys_addr_t mem_start;
	size_t mem_size;
	struct reset_control *reset;
};

static struct rk_decom *g_decom;

static DECLARE_WAIT_QUEUE_HEAD(initrd_decom_done);
static bool initrd_continue;

void __init wait_initrd_hw_decom_done(void)
{
	wait_event(initrd_decom_done, initrd_continue);
}

static DECLARE_WAIT_QUEUE_HEAD(decom_init_done);

int rk_decom_start(u32 mode, phys_addr_t src, phys_addr_t dst, u32 dst_max_size)
{
	u32 irq_status;
	u32 decom_enr;

	pr_info("%s: mode %u src %pa dst %pa max_size %u\n",
		__func__, mode, &src, &dst, dst_max_size);

	wait_event_timeout(decom_init_done, g_decom, HZ);
	if (!g_decom)
		return -EINVAL;

	decom_enr = readl(g_decom->regs + DECOM_ENR);
	if (decom_enr & 0x1) {
		pr_err("decompress busy\n");
		return -EBUSY;
	}

	if (g_decom->reset) {
		reset_control_assert(g_decom->reset);
		udelay(10);
		reset_control_deassert(g_decom->reset);
	}

	irq_status = readl(g_decom->regs + DECOM_ISR);
	/* clear interrupts */
	if (irq_status)
		writel(irq_status, g_decom->regs + DECOM_ISR);

	switch (mode) {
	case LZ4_MOD:
		writel(LZ4_CONT_CSUM_CHECK_EN |
		       LZ4_HEAD_CSUM_CHECK_EN |
		       LZ4_BLOCK_CSUM_CHECK_EN |
		       LZ4_MOD, g_decom->regs + DECOM_CTRL);
		break;
	case GZIP_MOD:
		writel(DECOM_DEFLATE_MODE | DECOM_GZIP_MODE,
		       g_decom->regs + DECOM_CTRL);
		break;
	case ZLIB_MOD:
		writel(DECOM_DEFLATE_MODE | DECOM_ZLIB_MODE,
		       g_decom->regs + DECOM_CTRL);
		break;
	default:
		pr_err("undefined mode : %d\n", mode);
		return -EINVAL;
	}

	writel(src, g_decom->regs + DECOM_RADDR);
	writel(dst, g_decom->regs + DECOM_WADDR);

	writel(dst_max_size, g_decom->regs + DECOM_LMTSL);
	writel(0x0, g_decom->regs + DECOM_LMTSH);

	writel(DECOM_INT_MASK, g_decom->regs + DECOM_IEN);
	writel(DECOM_ENABLE, g_decom->regs + DECOM_ENR);

	pr_info("%s: started\n", __func__);

	return 0;
}
EXPORT_SYMBOL(rk_decom_start);

static irqreturn_t rk_decom_irq_handler(int irq, void *priv)
{
	struct rk_decom *rk_dec = priv;
	u32 irq_status;
	u32 decom_status;

	irq_status = readl(rk_dec->regs + DECOM_ISR);
	/* clear interrupts */
	writel(irq_status, rk_dec->regs + DECOM_ISR);
	if (irq_status & DECOM_STOP) {
		decom_status = readl(rk_dec->regs + DECOM_STAT);
		if (decom_status & DECOM_COMPLETE) {
			initrd_continue = true;
			wake_up(&initrd_decom_done);
			dev_info(rk_dec->dev, "decom completed\n");
		} else {
			dev_info(rk_dec->dev,
				 "decom failed, irq_status = 0x%x, decom_status = 0x%x, try again !\n",
				 irq_status, decom_status);

			print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
				       32, 4, rk_dec->regs, 0x128, false);

			writel(DECOM_ENABLE, rk_dec->regs + DECOM_ENR);
		}
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rk_decom_irq_thread(int irq, void *priv)
{
	struct rk_decom *rk_dec = priv;

	if (initrd_continue) {
		void *start, *end;

		/*
		 * Now it is safe to free reserve memory that
		 * store the origin ramdisk file
		 */
		start = phys_to_virt(rk_dec->mem_start);
		end = start + rk_dec->mem_size;
		free_reserved_area(start, end, -1, "ramdisk gzip archive");
		clk_bulk_disable_unprepare(rk_dec->num_clocks, rk_dec->clocks);
	}

	return IRQ_HANDLED;
}

static int __init rockchip_decom_probe(struct platform_device *pdev)
{
	struct rk_decom *rk_dec;
	struct resource *res = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *mem;
	struct resource reg;
	int ret = 0;

	rk_dec = devm_kzalloc(dev, sizeof(*rk_dec), GFP_KERNEL);
	if (!rk_dec)
		return -ENOMEM;

	rk_dec->dev = dev;
	rk_dec->irq = platform_get_irq(pdev, 0);
	if (rk_dec->irq < 0) {
		dev_err(dev, "failed to get rk_dec irq\n");
		return -ENOENT;
	}

	mem = of_parse_phandle(np, "memory-region", 0);
	if (!mem) {
		dev_err(dev, "missing \"memory-region\" property\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(mem, 0, &reg);
	of_node_put(mem);
	if (ret) {
		dev_err(dev, "missing \"reg\" property\n");
		return -ENODEV;
	}

	rk_dec->mem_start = reg.start;
	rk_dec->mem_size = resource_size(&reg);

	rk_dec->num_clocks = devm_clk_bulk_get_all(dev, &rk_dec->clocks);
	if (rk_dec->num_clocks < 0) {
		dev_err(dev, "failed to get decompress clock\n");
		return -ENODEV;
	}

	ret = clk_bulk_prepare_enable(rk_dec->num_clocks, rk_dec->clocks);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rk_dec->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(rk_dec->regs)) {
		ret = PTR_ERR(rk_dec->regs);
		goto disable_clk;
	}

	dev_set_drvdata(dev, rk_dec);

	rk_dec->reset = devm_reset_control_get_exclusive(dev, "dresetn");
	if (IS_ERR(rk_dec->reset)) {
		ret = PTR_ERR(rk_dec->reset);
		if (ret != -ENOENT)
			return ret;

		dev_dbg(dev, "no reset control found\n");
		rk_dec->reset = NULL;
	}

	ret = devm_request_threaded_irq(dev, rk_dec->irq, rk_decom_irq_handler,
					rk_decom_irq_thread, IRQF_ONESHOT,
					dev_name(dev), rk_dec);
	if (ret < 0) {
		dev_err(dev, "failed to attach decompress irq\n");
		goto disable_clk;
	}

	g_decom = rk_dec;
	wake_up(&decom_init_done);

	return 0;

disable_clk:
	clk_bulk_disable_unprepare(rk_dec->num_clocks, rk_dec->clocks);

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_decom_dt_match[] = {
	{ .compatible = "rockchip,hw-decompress" },
	{},
};
#endif

static struct platform_driver rk_decom_driver = {
	.driver		= {
		.name	= "rockchip_hw_decompress",
		.of_match_table = rockchip_decom_dt_match,
	},
};

static int __init rockchip_hw_decompress_init(void)
{
	struct device_node *node;

	node = of_find_matching_node(NULL, rockchip_decom_dt_match);
	if (node) {
		of_platform_device_create(node, NULL, NULL);
		of_node_put(node);
		return platform_driver_probe(&rk_decom_driver, rockchip_decom_probe);
	}

	return 0;
}

pure_initcall(rockchip_hw_decompress_init);
