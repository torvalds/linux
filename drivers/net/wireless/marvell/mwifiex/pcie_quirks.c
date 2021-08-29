/*
 * NXP Wireless LAN device driver: PCIE and platform specific quirks
 *
 * This software file (the "File") is distributed by NXP
 * under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include <linux/dmi.h>

#include "pcie_quirks.h"

/* quirk table based on DMI matching */
static const struct dmi_system_id mwifiex_quirk_table[] = {
	{
		.ident = "Surface Pro 4",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Pro 4"),
		},
		.driver_data = (void *)QUIRK_FW_RST_D3COLD,
	},
	{
		.ident = "Surface Pro 5",
		.matches = {
			/* match for SKU here due to generic product name "Surface Pro" */
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "Surface_Pro_1796"),
		},
		.driver_data = (void *)QUIRK_FW_RST_D3COLD,
	},
	{
		.ident = "Surface Pro 5 (LTE)",
		.matches = {
			/* match for SKU here due to generic product name "Surface Pro" */
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "Surface_Pro_1807"),
		},
		.driver_data = (void *)QUIRK_FW_RST_D3COLD,
	},
	{
		.ident = "Surface Pro 6",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Pro 6"),
		},
		.driver_data = (void *)QUIRK_FW_RST_D3COLD,
	},
	{
		.ident = "Surface Book 1",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Book"),
		},
		.driver_data = (void *)QUIRK_FW_RST_D3COLD,
	},
	{
		.ident = "Surface Book 2",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Book 2"),
		},
		.driver_data = (void *)QUIRK_FW_RST_D3COLD,
	},
	{
		.ident = "Surface Laptop 1",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Laptop"),
		},
		.driver_data = (void *)QUIRK_FW_RST_D3COLD,
	},
	{
		.ident = "Surface Laptop 2",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Laptop 2"),
		},
		.driver_data = (void *)QUIRK_FW_RST_D3COLD,
	},
	{}
};

void mwifiex_initialize_quirks(struct pcie_service_card *card)
{
	struct pci_dev *pdev = card->dev;
	const struct dmi_system_id *dmi_id;

	dmi_id = dmi_first_match(mwifiex_quirk_table);
	if (dmi_id)
		card->quirks = (uintptr_t)dmi_id->driver_data;

	if (!card->quirks)
		dev_info(&pdev->dev, "no quirks enabled\n");
	if (card->quirks & QUIRK_FW_RST_D3COLD)
		dev_info(&pdev->dev, "quirk reset_d3cold enabled\n");
}

static void mwifiex_pcie_set_power_d3cold(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "putting into D3cold...\n");

	pci_save_state(pdev);
	if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3cold);
}

static int mwifiex_pcie_set_power_d0(struct pci_dev *pdev)
{
	int ret;

	dev_info(&pdev->dev, "putting into D0...\n");

	pci_set_power_state(pdev, PCI_D0);
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		return ret;
	}
	pci_restore_state(pdev);

	return 0;
}

int mwifiex_pcie_reset_d3cold_quirk(struct pci_dev *pdev)
{
	struct pci_dev *parent_pdev = pci_upstream_bridge(pdev);
	int ret;

	/* Power-cycle (put into D3cold then D0) */
	dev_info(&pdev->dev, "Using reset_d3cold quirk to perform FW reset\n");

	/* We need to perform power-cycle also for bridge of wifi because
	 * on some devices (e.g. Surface Book 1), the OS for some reasons
	 * can't know the real power state of the bridge.
	 * When tried to power-cycle only wifi, the reset failed with the
	 * following dmesg log:
	 * "Cannot transition to power state D0 for parent in D3hot".
	 */
	mwifiex_pcie_set_power_d3cold(pdev);
	mwifiex_pcie_set_power_d3cold(parent_pdev);

	ret = mwifiex_pcie_set_power_d0(parent_pdev);
	if (ret)
		return ret;
	ret = mwifiex_pcie_set_power_d0(pdev);
	if (ret)
		return ret;

	return 0;
}
