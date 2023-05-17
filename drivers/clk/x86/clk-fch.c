// SPDX-License-Identifier: MIT
/*
 * clock framework for AMD FCH controller block
 *
 * Copyright 2018 Advanced Micro Devices, Inc.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/pci.h>
#include <linux/platform_data/clk-fch.h>
#include <linux/platform_device.h>

/* Clock Driving Strength 2 register */
#define CLKDRVSTR2	0x28
/* Clock Control 1 register */
#define MISCCLKCNTL1	0x40
/* Auxiliary clock1 enable bit */
#define OSCCLKENB	2
/* 25Mhz auxiliary output clock freq bit */
#define OSCOUT1CLK25MHZ	16

#define ST_CLK_48M	0
#define ST_CLK_25M	1
#define ST_CLK_MUX	2
#define ST_CLK_GATE	3
#define ST_MAX_CLKS	4

#define CLK_48M_FIXED	0
#define CLK_GATE_FIXED	1
#define CLK_MAX_FIXED	2

/* List of supported CPU ids for clk mux with 25Mhz clk support */
#define AMD_CPU_ID_ST                  0x1576

static const char * const clk_oscout1_parents[] = { "clk48MHz", "clk25MHz" };
static struct clk_hw *hws[ST_MAX_CLKS];

static const struct pci_device_id fch_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_ST) },
	{ }
};

static int fch_clk_probe(struct platform_device *pdev)
{
	struct fch_clk_data *fch_data;
	struct pci_dev *rdev;

	fch_data = dev_get_platdata(&pdev->dev);
	if (!fch_data || !fch_data->base)
		return -EINVAL;

	rdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!rdev) {
		dev_err(&pdev->dev, "FCH device not found\n");
		return -ENODEV;
	}

	if (pci_match_id(fch_pci_ids, rdev)) {
		hws[ST_CLK_48M] = clk_hw_register_fixed_rate(NULL, "clk48MHz",
			NULL, 0, 48000000);
		hws[ST_CLK_25M] = clk_hw_register_fixed_rate(NULL, "clk25MHz",
			NULL, 0, 25000000);

		hws[ST_CLK_MUX] = clk_hw_register_mux(NULL, "oscout1_mux",
			clk_oscout1_parents, ARRAY_SIZE(clk_oscout1_parents),
			0, fch_data->base + CLKDRVSTR2, OSCOUT1CLK25MHZ, 3, 0,
			NULL);

		clk_set_parent(hws[ST_CLK_MUX]->clk, hws[ST_CLK_48M]->clk);

		hws[ST_CLK_GATE] = clk_hw_register_gate(NULL, "oscout1",
			"oscout1_mux", 0, fch_data->base + MISCCLKCNTL1,
			OSCCLKENB, CLK_GATE_SET_TO_DISABLE, NULL);

		devm_clk_hw_register_clkdev(&pdev->dev, hws[ST_CLK_GATE],
					    fch_data->name, NULL);
	} else {
		hws[CLK_48M_FIXED] = clk_hw_register_fixed_rate(NULL, "clk48MHz",
			NULL, 0, 48000000);

		hws[CLK_GATE_FIXED] = clk_hw_register_gate(NULL, "oscout1",
			"clk48MHz", 0, fch_data->base + MISCCLKCNTL1,
			OSCCLKENB, 0, NULL);

		devm_clk_hw_register_clkdev(&pdev->dev, hws[CLK_GATE_FIXED],
					    fch_data->name, NULL);
	}

	pci_dev_put(rdev);
	return 0;
}

static void fch_clk_remove(struct platform_device *pdev)
{
	int i, clks;
	struct pci_dev *rdev;

	rdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!rdev)
		return;

	clks = pci_match_id(fch_pci_ids, rdev) ? CLK_MAX_FIXED : ST_MAX_CLKS;

	for (i = 0; i < clks; i++)
		clk_hw_unregister(hws[i]);

	pci_dev_put(rdev);
}

static struct platform_driver fch_clk_driver = {
	.driver = {
		.name = "clk-fch",
		.suppress_bind_attrs = true,
	},
	.probe = fch_clk_probe,
	.remove_new = fch_clk_remove,
};
builtin_platform_driver(fch_clk_driver);
