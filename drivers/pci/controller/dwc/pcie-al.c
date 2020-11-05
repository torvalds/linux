// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Amazon's Annapurna Labs IP (used in chips
 * such as Graviton and Alpine)
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Author: Jonathan Chocron <jonnyc@amazon.com>
 */

#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include <linux/pci-acpi.h>
#include "../../pci.h"

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)

struct al_pcie_acpi  {
	void __iomem *dbi_base;
};

static void __iomem *al_pcie_map_bus(struct pci_bus *bus, unsigned int devfn,
				     int where)
{
	struct pci_config_window *cfg = bus->sysdata;
	struct al_pcie_acpi *pcie = cfg->priv;
	void __iomem *dbi_base = pcie->dbi_base;

	if (bus->number == cfg->busr.start) {
		/*
		 * The DW PCIe core doesn't filter out transactions to other
		 * devices/functions on the root bus num, so we do this here.
		 */
		if (PCI_SLOT(devfn) > 0)
			return NULL;
		else
			return dbi_base + where;
	}

	return pci_ecam_map_bus(bus, devfn, where);
}

static int al_pcie_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct acpi_device *adev = to_acpi_device(dev);
	struct acpi_pci_root *root = acpi_driver_data(adev);
	struct al_pcie_acpi *al_pcie;
	struct resource *res;
	int ret;

	al_pcie = devm_kzalloc(dev, sizeof(*al_pcie), GFP_KERNEL);
	if (!al_pcie)
		return -ENOMEM;

	res = devm_kzalloc(dev, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	ret = acpi_get_rc_resources(dev, "AMZN0001", root->segment, res);
	if (ret) {
		dev_err(dev, "can't get rc dbi base address for SEG %d\n",
			root->segment);
		return ret;
	}

	dev_dbg(dev, "Root port dbi res: %pR\n", res);

	al_pcie->dbi_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(al_pcie->dbi_base))
		return PTR_ERR(al_pcie->dbi_base);

	cfg->priv = al_pcie;

	return 0;
}

const struct pci_ecam_ops al_pcie_ops = {
	.bus_shift    = 20,
	.init         =  al_pcie_init,
	.pci_ops      = {
		.map_bus    = al_pcie_map_bus,
		.read       = pci_generic_config_read,
		.write      = pci_generic_config_write,
	}
};

#endif /* defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS) */

#ifdef CONFIG_PCIE_AL

#include <linux/of_pci.h>
#include "pcie-designware.h"

#define AL_PCIE_REV_ID_2	2
#define AL_PCIE_REV_ID_3	3
#define AL_PCIE_REV_ID_4	4

#define AXI_BASE_OFFSET		0x0

#define DEVICE_ID_OFFSET	0x16c

#define DEVICE_REV_ID			0x0
#define DEVICE_REV_ID_DEV_ID_MASK	GENMASK(31, 16)

#define DEVICE_REV_ID_DEV_ID_X4		0
#define DEVICE_REV_ID_DEV_ID_X8		2
#define DEVICE_REV_ID_DEV_ID_X16	4

#define OB_CTRL_REV1_2_OFFSET	0x0040
#define OB_CTRL_REV3_5_OFFSET	0x0030

#define CFG_TARGET_BUS			0x0
#define CFG_TARGET_BUS_MASK_MASK	GENMASK(7, 0)
#define CFG_TARGET_BUS_BUSNUM_MASK	GENMASK(15, 8)

#define CFG_CONTROL			0x4
#define CFG_CONTROL_SUBBUS_MASK		GENMASK(15, 8)
#define CFG_CONTROL_SEC_BUS_MASK	GENMASK(23, 16)

struct al_pcie_reg_offsets {
	unsigned int ob_ctrl;
};

struct al_pcie_target_bus_cfg {
	u8 reg_val;
	u8 reg_mask;
	u8 ecam_mask;
};

struct al_pcie {
	struct dw_pcie *pci;
	void __iomem *controller_base; /* base of PCIe unit (not DW core) */
	struct device *dev;
	resource_size_t ecam_size;
	unsigned int controller_rev_id;
	struct al_pcie_reg_offsets reg_offsets;
	struct al_pcie_target_bus_cfg target_bus_cfg;
};

#define PCIE_ECAM_DEVFN(x)		(((x) & 0xff) << 12)

#define to_al_pcie(x)		dev_get_drvdata((x)->dev)

static inline u32 al_pcie_controller_readl(struct al_pcie *pcie, u32 offset)
{
	return readl_relaxed(pcie->controller_base + offset);
}

static inline void al_pcie_controller_writel(struct al_pcie *pcie, u32 offset,
					     u32 val)
{
	writel_relaxed(val, pcie->controller_base + offset);
}

static int al_pcie_rev_id_get(struct al_pcie *pcie, unsigned int *rev_id)
{
	u32 dev_rev_id_val;
	u32 dev_id_val;

	dev_rev_id_val = al_pcie_controller_readl(pcie, AXI_BASE_OFFSET +
						  DEVICE_ID_OFFSET +
						  DEVICE_REV_ID);
	dev_id_val = FIELD_GET(DEVICE_REV_ID_DEV_ID_MASK, dev_rev_id_val);

	switch (dev_id_val) {
	case DEVICE_REV_ID_DEV_ID_X4:
		*rev_id = AL_PCIE_REV_ID_2;
		break;
	case DEVICE_REV_ID_DEV_ID_X8:
		*rev_id = AL_PCIE_REV_ID_3;
		break;
	case DEVICE_REV_ID_DEV_ID_X16:
		*rev_id = AL_PCIE_REV_ID_4;
		break;
	default:
		dev_err(pcie->dev, "Unsupported dev_id_val (0x%x)\n",
			dev_id_val);
		return -EINVAL;
	}

	dev_dbg(pcie->dev, "dev_id_val: 0x%x\n", dev_id_val);

	return 0;
}

static int al_pcie_reg_offsets_set(struct al_pcie *pcie)
{
	switch (pcie->controller_rev_id) {
	case AL_PCIE_REV_ID_2:
		pcie->reg_offsets.ob_ctrl = OB_CTRL_REV1_2_OFFSET;
		break;
	case AL_PCIE_REV_ID_3:
	case AL_PCIE_REV_ID_4:
		pcie->reg_offsets.ob_ctrl = OB_CTRL_REV3_5_OFFSET;
		break;
	default:
		dev_err(pcie->dev, "Unsupported controller rev_id: 0x%x\n",
			pcie->controller_rev_id);
		return -EINVAL;
	}

	return 0;
}

static inline void al_pcie_target_bus_set(struct al_pcie *pcie,
					  u8 target_bus,
					  u8 mask_target_bus)
{
	u32 reg;

	reg = FIELD_PREP(CFG_TARGET_BUS_MASK_MASK, mask_target_bus) |
	      FIELD_PREP(CFG_TARGET_BUS_BUSNUM_MASK, target_bus);

	al_pcie_controller_writel(pcie, AXI_BASE_OFFSET +
				  pcie->reg_offsets.ob_ctrl + CFG_TARGET_BUS,
				  reg);
}

static void __iomem *al_pcie_conf_addr_map_bus(struct pci_bus *bus,
					       unsigned int devfn, int where)
{
	struct pcie_port *pp = bus->sysdata;
	struct al_pcie *pcie = to_al_pcie(to_dw_pcie_from_pp(pp));
	unsigned int busnr = bus->number;
	struct al_pcie_target_bus_cfg *target_bus_cfg = &pcie->target_bus_cfg;
	unsigned int busnr_ecam = busnr & target_bus_cfg->ecam_mask;
	unsigned int busnr_reg = busnr & target_bus_cfg->reg_mask;
	void __iomem *pci_base_addr;

	pci_base_addr = (void __iomem *)((uintptr_t)pp->va_cfg0_base +
					 (busnr_ecam << 20) +
					 PCIE_ECAM_DEVFN(devfn));

	if (busnr_reg != target_bus_cfg->reg_val) {
		dev_dbg(pcie->pci->dev, "Changing target bus busnum val from 0x%x to 0x%x\n",
			target_bus_cfg->reg_val, busnr_reg);
		target_bus_cfg->reg_val = busnr_reg;
		al_pcie_target_bus_set(pcie,
				       target_bus_cfg->reg_val,
				       target_bus_cfg->reg_mask);
	}

	return pci_base_addr + where;
}

static struct pci_ops al_child_pci_ops = {
	.map_bus = al_pcie_conf_addr_map_bus,
	.read = pci_generic_config_read,
	.write = pci_generic_config_write,
};

static void al_pcie_config_prepare(struct al_pcie *pcie)
{
	struct al_pcie_target_bus_cfg *target_bus_cfg;
	struct pcie_port *pp = &pcie->pci->pp;
	unsigned int ecam_bus_mask;
	u32 cfg_control_offset;
	u8 subordinate_bus;
	u8 secondary_bus;
	u32 cfg_control;
	u32 reg;
	struct resource *bus = resource_list_first_type(&pp->bridge->windows, IORESOURCE_BUS)->res;

	target_bus_cfg = &pcie->target_bus_cfg;

	ecam_bus_mask = (pcie->ecam_size >> 20) - 1;
	if (ecam_bus_mask > 255) {
		dev_warn(pcie->dev, "ECAM window size is larger than 256MB. Cutting off at 256\n");
		ecam_bus_mask = 255;
	}

	/* This portion is taken from the transaction address */
	target_bus_cfg->ecam_mask = ecam_bus_mask;
	/* This portion is taken from the cfg_target_bus reg */
	target_bus_cfg->reg_mask = ~target_bus_cfg->ecam_mask;
	target_bus_cfg->reg_val = bus->start & target_bus_cfg->reg_mask;

	al_pcie_target_bus_set(pcie, target_bus_cfg->reg_val,
			       target_bus_cfg->reg_mask);

	secondary_bus = bus->start + 1;
	subordinate_bus = bus->end;

	/* Set the valid values of secondary and subordinate buses */
	cfg_control_offset = AXI_BASE_OFFSET + pcie->reg_offsets.ob_ctrl +
			     CFG_CONTROL;

	cfg_control = al_pcie_controller_readl(pcie, cfg_control_offset);

	reg = cfg_control &
	      ~(CFG_CONTROL_SEC_BUS_MASK | CFG_CONTROL_SUBBUS_MASK);

	reg |= FIELD_PREP(CFG_CONTROL_SUBBUS_MASK, subordinate_bus) |
	       FIELD_PREP(CFG_CONTROL_SEC_BUS_MASK, secondary_bus);

	al_pcie_controller_writel(pcie, cfg_control_offset, reg);
}

static int al_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct al_pcie *pcie = to_al_pcie(pci);
	int rc;

	pp->bridge->child_ops = &al_child_pci_ops;

	rc = al_pcie_rev_id_get(pcie, &pcie->controller_rev_id);
	if (rc)
		return rc;

	rc = al_pcie_reg_offsets_set(pcie);
	if (rc)
		return rc;

	al_pcie_config_prepare(pcie);

	return 0;
}

static const struct dw_pcie_host_ops al_pcie_host_ops = {
	.host_init = al_pcie_host_init,
};

static const struct dw_pcie_ops dw_pcie_ops = {
};

static int al_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *controller_res;
	struct resource *ecam_res;
	struct al_pcie *al_pcie;
	struct dw_pcie *pci;

	al_pcie = devm_kzalloc(dev, sizeof(*al_pcie), GFP_KERNEL);
	if (!al_pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;
	pci->pp.ops = &al_pcie_host_ops;

	al_pcie->pci = pci;
	al_pcie->dev = dev;

	ecam_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (!ecam_res) {
		dev_err(dev, "couldn't find 'config' reg in DT\n");
		return -ENOENT;
	}
	al_pcie->ecam_size = resource_size(ecam_res);

	controller_res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						      "controller");
	al_pcie->controller_base = devm_ioremap_resource(dev, controller_res);
	if (IS_ERR(al_pcie->controller_base)) {
		dev_err(dev, "couldn't remap controller base %pR\n",
			controller_res);
		return PTR_ERR(al_pcie->controller_base);
	}

	dev_dbg(dev, "From DT: controller_base: %pR\n", controller_res);

	platform_set_drvdata(pdev, al_pcie);

	return dw_pcie_host_init(&pci->pp);
}

static const struct of_device_id al_pcie_of_match[] = {
	{ .compatible = "amazon,al-alpine-v2-pcie",
	},
	{ .compatible = "amazon,al-alpine-v3-pcie",
	},
	{},
};

static struct platform_driver al_pcie_driver = {
	.driver = {
		.name	= "al-pcie",
		.of_match_table = al_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = al_pcie_probe,
};
builtin_platform_driver(al_pcie_driver);

#endif /* CONFIG_PCIE_AL*/
