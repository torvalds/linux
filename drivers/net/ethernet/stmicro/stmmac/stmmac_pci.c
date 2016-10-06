/*******************************************************************************
  This contains the functions to handle the pci driver.

  Copyright (C) 2011-2012  Vayavya Labs Pvt Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Rayagond Kokatanur <rayagond@vayavyalabs.com>
  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/pci.h>
#include <linux/dmi.h>

#include "stmmac.h"

/*
 * This struct is used to associate PCI Function of MAC controller on a board,
 * discovered via DMI, with the address of PHY connected to the MAC. The
 * negative value of the address means that MAC controller is not connected
 * with PHY.
 */
struct stmmac_pci_dmi_data {
	const char *name;
	unsigned int func;
	int phy_addr;
};

struct stmmac_pci_info {
	struct pci_dev *pdev;
	int (*setup)(struct plat_stmmacenet_data *plat,
		     struct stmmac_pci_info *info);
	struct stmmac_pci_dmi_data *dmi;
};

static int stmmac_pci_find_phy_addr(struct stmmac_pci_info *info)
{
	const char *name = dmi_get_system_info(DMI_BOARD_NAME);
	unsigned int func = PCI_FUNC(info->pdev->devfn);
	struct stmmac_pci_dmi_data *dmi;

	/*
	 * Galileo boards with old firmware don't support DMI. We always return
	 * 1 here, so at least first found MAC controller would be probed.
	 */
	if (!name)
		return 1;

	for (dmi = info->dmi; dmi->name && *dmi->name; dmi++) {
		if (!strcmp(dmi->name, name) && dmi->func == func)
			return dmi->phy_addr;
	}

	return -ENODEV;
}

static void stmmac_default_data(struct plat_stmmacenet_data *plat)
{
	plat->bus_id = 1;
	plat->phy_addr = 0;
	plat->interface = PHY_INTERFACE_MODE_GMII;
	plat->clk_csr = 2;	/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat->has_gmac = 1;
	plat->force_sf_dma_mode = 1;

	plat->mdio_bus_data->phy_reset = NULL;
	plat->mdio_bus_data->phy_mask = 0;

	plat->dma_cfg->pbl = 32;
	/* TODO: AXI */

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;
}

static int quark_default_data(struct plat_stmmacenet_data *plat,
			      struct stmmac_pci_info *info)
{
	struct pci_dev *pdev = info->pdev;
	int ret;

	/*
	 * Refuse to load the driver and register net device if MAC controller
	 * does not connect to any PHY interface.
	 */
	ret = stmmac_pci_find_phy_addr(info);
	if (ret < 0)
		return ret;

	plat->bus_id = PCI_DEVID(pdev->bus->number, pdev->devfn);
	plat->phy_addr = ret;
	plat->interface = PHY_INTERFACE_MODE_RMII;
	plat->clk_csr = 2;
	plat->has_gmac = 1;
	plat->force_sf_dma_mode = 1;

	plat->mdio_bus_data->phy_reset = NULL;
	plat->mdio_bus_data->phy_mask = 0;

	plat->dma_cfg->pbl = 16;
	plat->dma_cfg->fixed_burst = 1;
	/* AXI (TODO) */

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	return 0;
}

static struct stmmac_pci_dmi_data quark_pci_dmi_data[] = {
	{
		.name = "Galileo",
		.func = 6,
		.phy_addr = 1,
	},
	{
		.name = "GalileoGen2",
		.func = 6,
		.phy_addr = 1,
	},
	{}
};

static struct stmmac_pci_info quark_pci_info = {
	.setup = quark_default_data,
	.dmi = quark_pci_dmi_data,
};

/**
 * stmmac_pci_probe
 *
 * @pdev: pci device pointer
 * @id: pointer to table of device id/id's.
 *
 * Description: This probing function gets called for all PCI devices which
 * match the ID table and are not "owned" by other driver yet. This function
 * gets passed a "struct pci_dev *" for each device whose entry in the ID table
 * matches the device. The probe functions returns zero when the driver choose
 * to take "ownership" of the device or an error code(-ve no) otherwise.
 */
static int stmmac_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct stmmac_pci_info *info = (struct stmmac_pci_info *)id->driver_data;
	struct plat_stmmacenet_data *plat;
	struct stmmac_resources res;
	int i;
	int ret;

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return -ENOMEM;

	plat->mdio_bus_data = devm_kzalloc(&pdev->dev,
					   sizeof(*plat->mdio_bus_data),
					   GFP_KERNEL);
	if (!plat->mdio_bus_data)
		return -ENOMEM;

	plat->dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*plat->dma_cfg),
				     GFP_KERNEL);
	if (!plat->dma_cfg)
		return -ENOMEM;

	/* Enable pci device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: ERROR: failed to enable device\n",
			__func__);
		return ret;
	}

	/* Get the base address of device */
	for (i = 0; i <= PCI_STD_RESOURCE_END; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		ret = pcim_iomap_regions(pdev, BIT(i), pci_name(pdev));
		if (ret)
			return ret;
		break;
	}

	pci_set_master(pdev);

	if (info) {
		info->pdev = pdev;
		if (info->setup) {
			ret = info->setup(plat, info);
			if (ret)
				return ret;
		}
	} else
		stmmac_default_data(plat);

	pci_enable_msi(pdev);

	memset(&res, 0, sizeof(res));
	res.addr = pcim_iomap_table(pdev)[i];
	res.wol_irq = pdev->irq;
	res.irq = pdev->irq;

	return stmmac_dvr_probe(&pdev->dev, plat, &res);
}

/**
 * stmmac_pci_remove
 *
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources
 * and releases the PCI resources.
 */
static void stmmac_pci_remove(struct pci_dev *pdev)
{
	stmmac_dvr_remove(&pdev->dev);
}

static SIMPLE_DEV_PM_OPS(stmmac_pm_ops, stmmac_suspend, stmmac_resume);

#define STMMAC_VENDOR_ID 0x700
#define STMMAC_QUARK_ID  0x0937
#define STMMAC_DEVICE_ID 0x1108

static const struct pci_device_id stmmac_id_table[] = {
	{PCI_DEVICE(STMMAC_VENDOR_ID, STMMAC_DEVICE_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_STMICRO, PCI_DEVICE_ID_STMICRO_MAC)},
	{PCI_VDEVICE(INTEL, STMMAC_QUARK_ID), (kernel_ulong_t)&quark_pci_info},
	{}
};

MODULE_DEVICE_TABLE(pci, stmmac_id_table);

static struct pci_driver stmmac_pci_driver = {
	.name = STMMAC_RESOURCE_NAME,
	.id_table = stmmac_id_table,
	.probe = stmmac_pci_probe,
	.remove = stmmac_pci_remove,
	.driver         = {
		.pm     = &stmmac_pm_ops,
	},
};

module_pci_driver(stmmac_pci_driver);

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet PCI driver");
MODULE_AUTHOR("Rayagond Kokatanur <rayagond.kokatanur@vayavyalabs.com>");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL");
