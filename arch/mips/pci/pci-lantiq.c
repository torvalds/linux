/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <john@phrozen.org>
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>

#include <asm/addrspace.h>

#include <lantiq_soc.h>
#include <lantiq_irq.h>

#include "pci-lantiq.h"

#define PCI_CR_FCI_ADDR_MAP0		0x00C0
#define PCI_CR_FCI_ADDR_MAP1		0x00C4
#define PCI_CR_FCI_ADDR_MAP2		0x00C8
#define PCI_CR_FCI_ADDR_MAP3		0x00CC
#define PCI_CR_FCI_ADDR_MAP4		0x00D0
#define PCI_CR_FCI_ADDR_MAP5		0x00D4
#define PCI_CR_FCI_ADDR_MAP6		0x00D8
#define PCI_CR_FCI_ADDR_MAP7		0x00DC
#define PCI_CR_CLK_CTRL			0x0000
#define PCI_CR_PCI_MOD			0x0030
#define PCI_CR_PC_ARB			0x0080
#define PCI_CR_FCI_ADDR_MAP11hg		0x00E4
#define PCI_CR_BAR11MASK		0x0044
#define PCI_CR_BAR12MASK		0x0048
#define PCI_CR_BAR13MASK		0x004C
#define PCI_CS_BASE_ADDR1		0x0010
#define PCI_CR_PCI_ADDR_MAP11		0x0064
#define PCI_CR_FCI_BURST_LENGTH		0x00E8
#define PCI_CR_PCI_EOI			0x002C
#define PCI_CS_STS_CMD			0x0004

#define PCI_MASTER0_REQ_MASK_2BITS	8
#define PCI_MASTER1_REQ_MASK_2BITS	10
#define PCI_MASTER2_REQ_MASK_2BITS	12
#define INTERNAL_ARB_ENABLE_BIT		0

#define LTQ_CGU_IFCCR		0x0018
#define LTQ_CGU_PCICR		0x0034

#define ltq_pci_w32(x, y)	ltq_w32((x), ltq_pci_membase + (y))
#define ltq_pci_r32(x)		ltq_r32(ltq_pci_membase + (x))

#define ltq_pci_cfg_w32(x, y)	ltq_w32((x), ltq_pci_mapped_cfg + (y))
#define ltq_pci_cfg_r32(x)	ltq_r32(ltq_pci_mapped_cfg + (x))

__iomem void *ltq_pci_mapped_cfg;
static __iomem void *ltq_pci_membase;

static int reset_gpio;
static struct clk *clk_pci, *clk_external;
static struct resource pci_io_resource;
static struct resource pci_mem_resource;
static struct pci_ops pci_ops = {
	.read	= ltq_pci_read_config_dword,
	.write	= ltq_pci_write_config_dword
};

static struct pci_controller pci_controller = {
	.pci_ops	= &pci_ops,
	.mem_resource	= &pci_mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_resource	= &pci_io_resource,
	.io_offset	= 0x00000000UL,
};

static inline u32 ltq_calc_bar11mask(void)
{
	u32 mem, bar11mask;

	/* BAR11MASK value depends on available memory on system. */
	mem = get_num_physpages() * PAGE_SIZE;
	bar11mask = (0x0ffffff0 & ~((1 << (fls(mem) - 1)) - 1)) | 8;

	return bar11mask;
}

static int ltq_pci_startup(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const __be32 *req_mask, *bus_clk;
	u32 temp_buffer;

	/* get our clocks */
	clk_pci = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk_pci)) {
		dev_err(&pdev->dev, "failed to get pci clock\n");
		return PTR_ERR(clk_pci);
	}

	clk_external = clk_get(&pdev->dev, "external");
	if (IS_ERR(clk_external)) {
		clk_put(clk_pci);
		dev_err(&pdev->dev, "failed to get external pci clock\n");
		return PTR_ERR(clk_external);
	}

	/* read the bus speed that we want */
	bus_clk = of_get_property(node, "lantiq,bus-clock", NULL);
	if (bus_clk)
		clk_set_rate(clk_pci, *bus_clk);

	/* and enable the clocks */
	clk_enable(clk_pci);
	if (of_find_property(node, "lantiq,external-clock", NULL))
		clk_enable(clk_external);
	else
		clk_disable(clk_external);

	/* setup reset gpio used by pci */
	reset_gpio = of_get_named_gpio(node, "gpio-reset", 0);
	if (gpio_is_valid(reset_gpio)) {
		int ret = devm_gpio_request(&pdev->dev,
						reset_gpio, "pci-reset");
		if (ret) {
			dev_err(&pdev->dev,
				"failed to request gpio %d\n", reset_gpio);
			return ret;
		}
		gpio_direction_output(reset_gpio, 1);
	}

	/* enable auto-switching between PCI and EBU */
	ltq_pci_w32(0xa, PCI_CR_CLK_CTRL);

	/* busy, i.e. configuration is not done, PCI access has to be retried */
	ltq_pci_w32(ltq_pci_r32(PCI_CR_PCI_MOD) & ~(1 << 24), PCI_CR_PCI_MOD);
	wmb();
	/* BUS Master/IO/MEM access */
	ltq_pci_cfg_w32(ltq_pci_cfg_r32(PCI_CS_STS_CMD) | 7, PCI_CS_STS_CMD);

	/* enable external 2 PCI masters */
	temp_buffer = ltq_pci_r32(PCI_CR_PC_ARB);
	/* setup the request mask */
	req_mask = of_get_property(node, "req-mask", NULL);
	if (req_mask)
		temp_buffer &= ~((*req_mask & 0xf) << 16);
	else
		temp_buffer &= ~0xf0000;
	/* enable internal arbiter */
	temp_buffer |= (1 << INTERNAL_ARB_ENABLE_BIT);
	/* enable internal PCI master reqest */
	temp_buffer &= (~(3 << PCI_MASTER0_REQ_MASK_2BITS));

	/* enable EBU request */
	temp_buffer &= (~(3 << PCI_MASTER1_REQ_MASK_2BITS));

	/* enable all external masters request */
	temp_buffer &= (~(3 << PCI_MASTER2_REQ_MASK_2BITS));
	ltq_pci_w32(temp_buffer, PCI_CR_PC_ARB);
	wmb();

	/* setup BAR memory regions */
	ltq_pci_w32(0x18000000, PCI_CR_FCI_ADDR_MAP0);
	ltq_pci_w32(0x18400000, PCI_CR_FCI_ADDR_MAP1);
	ltq_pci_w32(0x18800000, PCI_CR_FCI_ADDR_MAP2);
	ltq_pci_w32(0x18c00000, PCI_CR_FCI_ADDR_MAP3);
	ltq_pci_w32(0x19000000, PCI_CR_FCI_ADDR_MAP4);
	ltq_pci_w32(0x19400000, PCI_CR_FCI_ADDR_MAP5);
	ltq_pci_w32(0x19800000, PCI_CR_FCI_ADDR_MAP6);
	ltq_pci_w32(0x19c00000, PCI_CR_FCI_ADDR_MAP7);
	ltq_pci_w32(0x1ae00000, PCI_CR_FCI_ADDR_MAP11hg);
	ltq_pci_w32(ltq_calc_bar11mask(), PCI_CR_BAR11MASK);
	ltq_pci_w32(0, PCI_CR_PCI_ADDR_MAP11);
	ltq_pci_w32(0, PCI_CS_BASE_ADDR1);
	/* both TX and RX endian swap are enabled */
	ltq_pci_w32(ltq_pci_r32(PCI_CR_PCI_EOI) | 3, PCI_CR_PCI_EOI);
	wmb();
	ltq_pci_w32(ltq_pci_r32(PCI_CR_BAR12MASK) | 0x80000000,
		PCI_CR_BAR12MASK);
	ltq_pci_w32(ltq_pci_r32(PCI_CR_BAR13MASK) | 0x80000000,
		PCI_CR_BAR13MASK);
	/*use 8 dw burst length */
	ltq_pci_w32(0x303, PCI_CR_FCI_BURST_LENGTH);
	ltq_pci_w32(ltq_pci_r32(PCI_CR_PCI_MOD) | (1 << 24), PCI_CR_PCI_MOD);
	wmb();

	/* setup irq line */
	ltq_ebu_w32(ltq_ebu_r32(LTQ_EBU_PCC_CON) | 0xc, LTQ_EBU_PCC_CON);
	ltq_ebu_w32(ltq_ebu_r32(LTQ_EBU_PCC_IEN) | 0x10, LTQ_EBU_PCC_IEN);

	/* toggle reset pin */
	if (gpio_is_valid(reset_gpio)) {
		__gpio_set_value(reset_gpio, 0);
		wmb();
		mdelay(1);
		__gpio_set_value(reset_gpio, 1);
	}
	return 0;
}

static int ltq_pci_probe(struct platform_device *pdev)
{
	struct resource *res_cfg, *res_bridge;

	pci_clear_flags(PCI_PROBE_ONLY);

	res_bridge = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	ltq_pci_membase = devm_ioremap_resource(&pdev->dev, res_bridge);
	if (IS_ERR(ltq_pci_membase))
		return PTR_ERR(ltq_pci_membase);

	res_cfg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ltq_pci_mapped_cfg = devm_ioremap_resource(&pdev->dev, res_cfg);
	if (IS_ERR(ltq_pci_mapped_cfg))
		return PTR_ERR(ltq_pci_mapped_cfg);

	ltq_pci_startup(pdev);

	pci_load_of_ranges(&pci_controller, pdev->dev.of_node);
	register_pci_controller(&pci_controller);
	return 0;
}

static const struct of_device_id ltq_pci_match[] = {
	{ .compatible = "lantiq,pci-xway" },
	{},
};
MODULE_DEVICE_TABLE(of, ltq_pci_match);

static struct platform_driver ltq_pci_driver = {
	.probe = ltq_pci_probe,
	.driver = {
		.name = "pci-xway",
		.of_match_table = ltq_pci_match,
	},
};

int __init pcibios_init(void)
{
	int ret = platform_driver_register(&ltq_pci_driver);
	if (ret)
		pr_info("pci-xway: Error registering platform driver!");
	return ret;
}

arch_initcall(pcibios_init);
