// SPDX-License-Identifier: GPL-2.0
/*
 * TPH (TLP Processing Hints) support
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 *     Eric Van Tassell <Eric.VanTassell@amd.com>
 *     Wei Huang <wei.huang2@amd.com>
 */
#include <linux/pci.h>
#include <linux/bitfield.h>
#include <linux/pci-tph.h>

#include "pci.h"

/* System-wide TPH disabled */
static bool pci_tph_disabled;

static u8 get_st_modes(struct pci_dev *pdev)
{
	u32 reg;

	pci_read_config_dword(pdev, pdev->tph_cap + PCI_TPH_CAP, &reg);
	reg &= PCI_TPH_CAP_ST_NS | PCI_TPH_CAP_ST_IV | PCI_TPH_CAP_ST_DS;

	return reg;
}

/* Return device's Root Port completer capability */
static u8 get_rp_completer_type(struct pci_dev *pdev)
{
	struct pci_dev *rp;
	u32 reg;
	int ret;

	rp = pcie_find_root_port(pdev);
	if (!rp)
		return 0;

	ret = pcie_capability_read_dword(rp, PCI_EXP_DEVCAP2, &reg);
	if (ret)
		return 0;

	return FIELD_GET(PCI_EXP_DEVCAP2_TPH_COMP_MASK, reg);
}

/**
 * pcie_disable_tph - Turn off TPH support for device
 * @pdev: PCI device
 *
 * Return: none
 */
void pcie_disable_tph(struct pci_dev *pdev)
{
	if (!pdev->tph_cap)
		return;

	if (!pdev->tph_enabled)
		return;

	pci_write_config_dword(pdev, pdev->tph_cap + PCI_TPH_CTRL, 0);

	pdev->tph_mode = 0;
	pdev->tph_req_type = 0;
	pdev->tph_enabled = 0;
}
EXPORT_SYMBOL(pcie_disable_tph);

/**
 * pcie_enable_tph - Enable TPH support for device using a specific ST mode
 * @pdev: PCI device
 * @mode: ST mode to enable. Current supported modes include:
 *
 *   - PCI_TPH_ST_NS_MODE: NO ST Mode
 *   - PCI_TPH_ST_IV_MODE: Interrupt Vector Mode
 *   - PCI_TPH_ST_DS_MODE: Device Specific Mode
 *
 * Check whether the mode is actually supported by the device before enabling
 * and return an error if not. Additionally determine what types of requests,
 * TPH or extended TPH, can be issued by the device based on its TPH requester
 * capability and the Root Port's completer capability.
 *
 * Return: 0 on success, otherwise negative value (-errno)
 */
int pcie_enable_tph(struct pci_dev *pdev, int mode)
{
	u32 reg;
	u8 dev_modes;
	u8 rp_req_type;

	/* Honor "notph" kernel parameter */
	if (pci_tph_disabled)
		return -EINVAL;

	if (!pdev->tph_cap)
		return -EINVAL;

	if (pdev->tph_enabled)
		return -EBUSY;

	/* Sanitize and check ST mode compatibility */
	mode &= PCI_TPH_CTRL_MODE_SEL_MASK;
	dev_modes = get_st_modes(pdev);
	if (!((1 << mode) & dev_modes))
		return -EINVAL;

	pdev->tph_mode = mode;

	/* Get req_type supported by device and its Root Port */
	pci_read_config_dword(pdev, pdev->tph_cap + PCI_TPH_CAP, &reg);
	if (FIELD_GET(PCI_TPH_CAP_EXT_TPH, reg))
		pdev->tph_req_type = PCI_TPH_REQ_EXT_TPH;
	else
		pdev->tph_req_type = PCI_TPH_REQ_TPH_ONLY;

	rp_req_type = get_rp_completer_type(pdev);

	/* Final req_type is the smallest value of two */
	pdev->tph_req_type = min(pdev->tph_req_type, rp_req_type);

	if (pdev->tph_req_type == PCI_TPH_REQ_DISABLE)
		return -EINVAL;

	/* Write them into TPH control register */
	pci_read_config_dword(pdev, pdev->tph_cap + PCI_TPH_CTRL, &reg);

	reg &= ~PCI_TPH_CTRL_MODE_SEL_MASK;
	reg |= FIELD_PREP(PCI_TPH_CTRL_MODE_SEL_MASK, pdev->tph_mode);

	reg &= ~PCI_TPH_CTRL_REQ_EN_MASK;
	reg |= FIELD_PREP(PCI_TPH_CTRL_REQ_EN_MASK, pdev->tph_req_type);

	pci_write_config_dword(pdev, pdev->tph_cap + PCI_TPH_CTRL, reg);

	pdev->tph_enabled = 1;

	return 0;
}
EXPORT_SYMBOL(pcie_enable_tph);

void pci_restore_tph_state(struct pci_dev *pdev)
{
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	if (!pdev->tph_cap)
		return;

	if (!pdev->tph_enabled)
		return;

	save_state = pci_find_saved_ext_cap(pdev, PCI_EXT_CAP_ID_TPH);
	if (!save_state)
		return;

	/* Restore control register and all ST entries */
	cap = &save_state->cap.data[0];
	pci_write_config_dword(pdev, pdev->tph_cap + PCI_TPH_CTRL, *cap++);
}

void pci_save_tph_state(struct pci_dev *pdev)
{
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	if (!pdev->tph_cap)
		return;

	if (!pdev->tph_enabled)
		return;

	save_state = pci_find_saved_ext_cap(pdev, PCI_EXT_CAP_ID_TPH);
	if (!save_state)
		return;

	/* Save control register */
	cap = &save_state->cap.data[0];
	pci_read_config_dword(pdev, pdev->tph_cap + PCI_TPH_CTRL, cap++);
}

void pci_no_tph(void)
{
	pci_tph_disabled = true;

	pr_info("PCIe TPH is disabled\n");
}

void pci_tph_init(struct pci_dev *pdev)
{
	u32 save_size;

	pdev->tph_cap = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_TPH);
	if (!pdev->tph_cap)
		return;

	save_size = sizeof(u32);
	pci_add_ext_cap_save_buffer(pdev, PCI_EXT_CAP_ID_TPH, save_size);
}
