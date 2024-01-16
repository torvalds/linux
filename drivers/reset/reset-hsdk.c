/*
 * Copyright (C) 2017 Synopsys.
 *
 * Synopsys HSDK Development platform reset driver.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

#define to_hsdk_rst(p)	container_of((p), struct hsdk_rst, rcdev)

struct hsdk_rst {
	void __iomem			*regs_ctl;
	void __iomem			*regs_rst;
	spinlock_t			lock;
	struct reset_controller_dev	rcdev;
};

static const u32 rst_map[] = {
	BIT(16), /* APB_RST  */
	BIT(17), /* AXI_RST  */
	BIT(18), /* ETH_RST  */
	BIT(19), /* USB_RST  */
	BIT(20), /* SDIO_RST */
	BIT(21), /* HDMI_RST */
	BIT(22), /* GFX_RST  */
	BIT(25), /* DMAC_RST */
	BIT(31), /* EBI_RST  */
};

#define HSDK_MAX_RESETS			ARRAY_SIZE(rst_map)

#define CGU_SYS_RST_CTRL		0x0
#define CGU_IP_SW_RESET			0x0
#define CGU_IP_SW_RESET_DELAY_SHIFT	16
#define CGU_IP_SW_RESET_DELAY_MASK	GENMASK(31, CGU_IP_SW_RESET_DELAY_SHIFT)
#define CGU_IP_SW_RESET_DELAY		0
#define CGU_IP_SW_RESET_RESET		BIT(0)
#define SW_RESET_TIMEOUT		10000

static void hsdk_reset_config(struct hsdk_rst *rst, unsigned long id)
{
	writel(rst_map[id], rst->regs_ctl + CGU_SYS_RST_CTRL);
}

static int hsdk_reset_do(struct hsdk_rst *rst)
{
	u32 reg;

	reg = readl(rst->regs_rst + CGU_IP_SW_RESET);
	reg &= ~CGU_IP_SW_RESET_DELAY_MASK;
	reg |= CGU_IP_SW_RESET_DELAY << CGU_IP_SW_RESET_DELAY_SHIFT;
	reg |= CGU_IP_SW_RESET_RESET;
	writel(reg, rst->regs_rst + CGU_IP_SW_RESET);

	/* wait till reset bit is back to 0 */
	return readl_poll_timeout_atomic(rst->regs_rst + CGU_IP_SW_RESET, reg,
		!(reg & CGU_IP_SW_RESET_RESET), 5, SW_RESET_TIMEOUT);
}

static int hsdk_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct hsdk_rst *rst = to_hsdk_rst(rcdev);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&rst->lock, flags);
	hsdk_reset_config(rst, id);
	ret = hsdk_reset_do(rst);
	spin_unlock_irqrestore(&rst->lock, flags);

	return ret;
}

static const struct reset_control_ops hsdk_reset_ops = {
	.reset	= hsdk_reset_reset,
	.deassert = hsdk_reset_reset,
};

static int hsdk_reset_probe(struct platform_device *pdev)
{
	struct hsdk_rst *rst;
	struct resource *mem;

	rst = devm_kzalloc(&pdev->dev, sizeof(*rst), GFP_KERNEL);
	if (!rst)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rst->regs_ctl = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(rst->regs_ctl))
		return PTR_ERR(rst->regs_ctl);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	rst->regs_rst = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(rst->regs_rst))
		return PTR_ERR(rst->regs_rst);

	spin_lock_init(&rst->lock);

	rst->rcdev.owner = THIS_MODULE;
	rst->rcdev.ops = &hsdk_reset_ops;
	rst->rcdev.of_node = pdev->dev.of_node;
	rst->rcdev.nr_resets = HSDK_MAX_RESETS;
	rst->rcdev.of_reset_n_cells = 1;

	return reset_controller_register(&rst->rcdev);
}

static const struct of_device_id hsdk_reset_dt_match[] = {
	{ .compatible = "snps,hsdk-reset" },
	{ },
};

static struct platform_driver hsdk_reset_driver = {
	.probe	= hsdk_reset_probe,
	.driver	= {
		.name = "hsdk-reset",
		.of_match_table = hsdk_reset_dt_match,
	},
};
builtin_platform_driver(hsdk_reset_driver);

MODULE_AUTHOR("Eugeniy Paltsev <Eugeniy.Paltsev@synopsys.com>");
MODULE_DESCRIPTION("Synopsys HSDK SDP reset driver");
MODULE_LICENSE("GPL v2");
