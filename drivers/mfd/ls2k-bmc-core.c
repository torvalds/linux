// SPDX-License-Identifier: GPL-2.0-only
/*
 * Loongson-2K Board Management Controller (BMC) Core Driver.
 *
 * Copyright (C) 2024-2025 Loongson Technology Corporation Limited.
 *
 * Authors:
 *	Chong Qiao <qiaochong@loongson.cn>
 *	Binbin Zhou <zhoubinbin@loongson.cn>
 */

#include <linux/aperture.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>

/* LS2K BMC resources */
#define LS2K_DISPLAY_RES_START		(SZ_16M + SZ_2M)
#define LS2K_IPMI_RES_SIZE		0x1C
#define LS2K_IPMI0_RES_START		(SZ_16M + 0xF00000)
#define LS2K_IPMI1_RES_START		(LS2K_IPMI0_RES_START + LS2K_IPMI_RES_SIZE)
#define LS2K_IPMI2_RES_START		(LS2K_IPMI1_RES_START + LS2K_IPMI_RES_SIZE)
#define LS2K_IPMI3_RES_START		(LS2K_IPMI2_RES_START + LS2K_IPMI_RES_SIZE)
#define LS2K_IPMI4_RES_START		(LS2K_IPMI3_RES_START + LS2K_IPMI_RES_SIZE)

enum {
	LS2K_BMC_DISPLAY,
	LS2K_BMC_IPMI0,
	LS2K_BMC_IPMI1,
	LS2K_BMC_IPMI2,
	LS2K_BMC_IPMI3,
	LS2K_BMC_IPMI4,
};

static struct resource ls2k_display_resources[] = {
	DEFINE_RES_MEM_NAMED(LS2K_DISPLAY_RES_START, SZ_4M, "simpledrm-res"),
};

static struct resource ls2k_ipmi0_resources[] = {
	DEFINE_RES_MEM_NAMED(LS2K_IPMI0_RES_START, LS2K_IPMI_RES_SIZE, "ipmi0-res"),
};

static struct resource ls2k_ipmi1_resources[] = {
	DEFINE_RES_MEM_NAMED(LS2K_IPMI1_RES_START, LS2K_IPMI_RES_SIZE, "ipmi1-res"),
};

static struct resource ls2k_ipmi2_resources[] = {
	DEFINE_RES_MEM_NAMED(LS2K_IPMI2_RES_START, LS2K_IPMI_RES_SIZE, "ipmi2-res"),
};

static struct resource ls2k_ipmi3_resources[] = {
	DEFINE_RES_MEM_NAMED(LS2K_IPMI3_RES_START, LS2K_IPMI_RES_SIZE, "ipmi3-res"),
};

static struct resource ls2k_ipmi4_resources[] = {
	DEFINE_RES_MEM_NAMED(LS2K_IPMI4_RES_START, LS2K_IPMI_RES_SIZE, "ipmi4-res"),
};

static struct mfd_cell ls2k_bmc_cells[] = {
	[LS2K_BMC_DISPLAY] = {
		.name = "simple-framebuffer",
		.num_resources = ARRAY_SIZE(ls2k_display_resources),
		.resources = ls2k_display_resources
	},
	[LS2K_BMC_IPMI0] = {
		.name = "ls2k-ipmi-si",
		.num_resources = ARRAY_SIZE(ls2k_ipmi0_resources),
		.resources = ls2k_ipmi0_resources
	},
	[LS2K_BMC_IPMI1] = {
		.name = "ls2k-ipmi-si",
		.num_resources = ARRAY_SIZE(ls2k_ipmi1_resources),
		.resources = ls2k_ipmi1_resources
	},
	[LS2K_BMC_IPMI2] = {
		.name = "ls2k-ipmi-si",
		.num_resources = ARRAY_SIZE(ls2k_ipmi2_resources),
		.resources = ls2k_ipmi2_resources
	},
	[LS2K_BMC_IPMI3] = {
		.name = "ls2k-ipmi-si",
		.num_resources = ARRAY_SIZE(ls2k_ipmi3_resources),
		.resources = ls2k_ipmi3_resources
	},
	[LS2K_BMC_IPMI4] = {
		.name = "ls2k-ipmi-si",
		.num_resources = ARRAY_SIZE(ls2k_ipmi4_resources),
		.resources = ls2k_ipmi4_resources
	},
};

/*
 * Currently the Loongson-2K BMC hardware does not have an I2C interface to adapt to the
 * resolution. We set the resolution by presetting "video=1280x1024-16@2M" to the BMC memory.
 */
static int ls2k_bmc_parse_mode(struct pci_dev *pdev, struct simplefb_platform_data *pd)
{
	char *mode;
	int depth, ret;

	/* The last 16M of PCI BAR0 is used to store the resolution string. */
	mode = devm_ioremap(&pdev->dev, pci_resource_start(pdev, 0) + SZ_16M, SZ_16M);
	if (!mode)
		return -ENOMEM;

	/* The resolution field starts with the flag "video=". */
	if (!strncmp(mode, "video=", 6))
		mode = mode + 6;

	ret = kstrtoint(strsep(&mode, "x"), 10, &pd->width);
	if (ret)
		return ret;

	ret = kstrtoint(strsep(&mode, "-"), 10, &pd->height);
	if (ret)
		return ret;

	ret = kstrtoint(strsep(&mode, "@"), 10, &depth);
	if (ret)
		return ret;

	pd->stride = pd->width * depth / 8;
	pd->format = depth == 32 ? "a8r8g8b8" : "r5g6b5";

	return 0;
}

static int ls2k_bmc_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct simplefb_platform_data pd;
	resource_size_t base;
	int ret;

	ret = pci_enable_device(dev);
	if (ret)
		return ret;

	ret = ls2k_bmc_parse_mode(dev, &pd);
	if (ret)
		goto disable_pci;

	ls2k_bmc_cells[LS2K_BMC_DISPLAY].platform_data = &pd;
	ls2k_bmc_cells[LS2K_BMC_DISPLAY].pdata_size = sizeof(pd);
	base = dev->resource[0].start + LS2K_DISPLAY_RES_START;

	/* Remove conflicting efifb device */
	ret = aperture_remove_conflicting_devices(base, SZ_4M, "simple-framebuffer");
	if (ret) {
		dev_err(&dev->dev, "Failed to removed firmware framebuffers: %d\n", ret);
		goto disable_pci;
	}

	return devm_mfd_add_devices(&dev->dev, PLATFORM_DEVID_AUTO,
				    ls2k_bmc_cells, ARRAY_SIZE(ls2k_bmc_cells),
				    &dev->resource[0], 0, NULL);

disable_pci:
	pci_disable_device(dev);
	return ret;
}

static void ls2k_bmc_remove(struct pci_dev *dev)
{
	pci_disable_device(dev);
}

static struct pci_device_id ls2k_bmc_devices[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_LOONGSON, 0x1a05) },
	{ }
};
MODULE_DEVICE_TABLE(pci, ls2k_bmc_devices);

static struct pci_driver ls2k_bmc_driver = {
	.name = "ls2k-bmc",
	.id_table = ls2k_bmc_devices,
	.probe = ls2k_bmc_probe,
	.remove = ls2k_bmc_remove,
};
module_pci_driver(ls2k_bmc_driver);

MODULE_DESCRIPTION("Loongson-2K Board Management Controller (BMC) Core driver");
MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_LICENSE("GPL");
