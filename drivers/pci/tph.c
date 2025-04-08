// SPDX-License-Identifier: GPL-2.0
/*
 * TPH (TLP Processing Hints) support
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 *     Eric Van Tassell <Eric.VanTassell@amd.com>
 *     Wei Huang <wei.huang2@amd.com>
 */
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/msi.h>
#include <linux/bitfield.h>
#include <linux/pci-tph.h>

#include "pci.h"

/* System-wide TPH disabled */
static bool pci_tph_disabled;

#ifdef CONFIG_ACPI
/*
 * The st_info struct defines the Steering Tag (ST) info returned by the
 * firmware PCI ACPI _DSM method (rev=0x7, func=0xF, "_DSM to Query Cache
 * Locality TPH Features"), as specified in the approved ECN for PCI Firmware
 * Spec and available at https://members.pcisig.com/wg/PCI-SIG/document/15470.
 *
 * @vm_st_valid:  8-bit ST for volatile memory is valid
 * @vm_xst_valid: 16-bit extended ST for volatile memory is valid
 * @vm_ph_ignore: 1 => PH was and will be ignored, 0 => PH should be supplied
 * @vm_st:        8-bit ST for volatile mem
 * @vm_xst:       16-bit extended ST for volatile mem
 * @pm_st_valid:  8-bit ST for persistent memory is valid
 * @pm_xst_valid: 16-bit extended ST for persistent memory is valid
 * @pm_ph_ignore: 1 => PH was and will be ignored, 0 => PH should be supplied
 * @pm_st:        8-bit ST for persistent mem
 * @pm_xst:       16-bit extended ST for persistent mem
 */
union st_info {
	struct {
		u64 vm_st_valid : 1;
		u64 vm_xst_valid : 1;
		u64 vm_ph_ignore : 1;
		u64 rsvd1 : 5;
		u64 vm_st : 8;
		u64 vm_xst : 16;
		u64 pm_st_valid : 1;
		u64 pm_xst_valid : 1;
		u64 pm_ph_ignore : 1;
		u64 rsvd2 : 5;
		u64 pm_st : 8;
		u64 pm_xst : 16;
	};
	u64 value;
};

static u16 tph_extract_tag(enum tph_mem_type mem_type, u8 req_type,
			   union st_info *info)
{
	switch (req_type) {
	case PCI_TPH_REQ_TPH_ONLY: /* 8-bit tag */
		switch (mem_type) {
		case TPH_MEM_TYPE_VM:
			if (info->vm_st_valid)
				return info->vm_st;
			break;
		case TPH_MEM_TYPE_PM:
			if (info->pm_st_valid)
				return info->pm_st;
			break;
		}
		break;
	case PCI_TPH_REQ_EXT_TPH: /* 16-bit tag */
		switch (mem_type) {
		case TPH_MEM_TYPE_VM:
			if (info->vm_xst_valid)
				return info->vm_xst;
			break;
		case TPH_MEM_TYPE_PM:
			if (info->pm_xst_valid)
				return info->pm_xst;
			break;
		}
		break;
	default:
		return 0;
	}

	return 0;
}

#define TPH_ST_DSM_FUNC_INDEX	0xF
static acpi_status tph_invoke_dsm(acpi_handle handle, u32 cpu_uid,
				  union st_info *st_out)
{
	union acpi_object arg3[3], in_obj, *out_obj;

	if (!acpi_check_dsm(handle, &pci_acpi_dsm_guid, 7,
			    BIT(TPH_ST_DSM_FUNC_INDEX)))
		return AE_ERROR;

	/* DWORD: feature ID (0 for processor cache ST query) */
	arg3[0].integer.type = ACPI_TYPE_INTEGER;
	arg3[0].integer.value = 0;

	/* DWORD: target UID */
	arg3[1].integer.type = ACPI_TYPE_INTEGER;
	arg3[1].integer.value = cpu_uid;

	/* QWORD: properties, all 0's */
	arg3[2].integer.type = ACPI_TYPE_INTEGER;
	arg3[2].integer.value = 0;

	in_obj.type = ACPI_TYPE_PACKAGE;
	in_obj.package.count = ARRAY_SIZE(arg3);
	in_obj.package.elements = arg3;

	out_obj = acpi_evaluate_dsm(handle, &pci_acpi_dsm_guid, 7,
				    TPH_ST_DSM_FUNC_INDEX, &in_obj);
	if (!out_obj)
		return AE_ERROR;

	if (out_obj->type != ACPI_TYPE_BUFFER) {
		ACPI_FREE(out_obj);
		return AE_ERROR;
	}

	st_out->value = *((u64 *)(out_obj->buffer.pointer));

	ACPI_FREE(out_obj);

	return AE_OK;
}
#endif

/* Update the TPH Requester Enable field of TPH Control Register */
static void set_ctrl_reg_req_en(struct pci_dev *pdev, u8 req_type)
{
	u32 reg;

	pci_read_config_dword(pdev, pdev->tph_cap + PCI_TPH_CTRL, &reg);

	reg &= ~PCI_TPH_CTRL_REQ_EN_MASK;
	reg |= FIELD_PREP(PCI_TPH_CTRL_REQ_EN_MASK, req_type);

	pci_write_config_dword(pdev, pdev->tph_cap + PCI_TPH_CTRL, reg);
}

static u8 get_st_modes(struct pci_dev *pdev)
{
	u32 reg;

	pci_read_config_dword(pdev, pdev->tph_cap + PCI_TPH_CAP, &reg);
	reg &= PCI_TPH_CAP_ST_NS | PCI_TPH_CAP_ST_IV | PCI_TPH_CAP_ST_DS;

	return reg;
}

static u32 get_st_table_loc(struct pci_dev *pdev)
{
	u32 reg;

	pci_read_config_dword(pdev, pdev->tph_cap + PCI_TPH_CAP, &reg);

	return FIELD_GET(PCI_TPH_CAP_LOC_MASK, reg);
}

/*
 * Return the size of ST table. If ST table is not in TPH Requester Extended
 * Capability space, return 0. Otherwise return the ST Table Size + 1.
 */
static u16 get_st_table_size(struct pci_dev *pdev)
{
	u32 reg;
	u32 loc;

	/* Check ST table location first */
	loc = get_st_table_loc(pdev);

	/* Convert loc to match with PCI_TPH_LOC_* defined in pci_regs.h */
	loc = FIELD_PREP(PCI_TPH_CAP_LOC_MASK, loc);
	if (loc != PCI_TPH_LOC_CAP)
		return 0;

	pci_read_config_dword(pdev, pdev->tph_cap + PCI_TPH_CAP, &reg);

	return FIELD_GET(PCI_TPH_CAP_ST_MASK, reg) + 1;
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

/* Write ST to MSI-X vector control reg - Return 0 if OK, otherwise -errno */
static int write_tag_to_msix(struct pci_dev *pdev, int msix_idx, u16 tag)
{
#ifdef CONFIG_PCI_MSI
	struct msi_desc *msi_desc = NULL;
	void __iomem *vec_ctrl;
	u32 val;
	int err = 0;

	msi_lock_descs(&pdev->dev);

	/* Find the msi_desc entry with matching msix_idx */
	msi_for_each_desc(msi_desc, &pdev->dev, MSI_DESC_ASSOCIATED) {
		if (msi_desc->msi_index == msix_idx)
			break;
	}

	if (!msi_desc) {
		err = -ENXIO;
		goto err_out;
	}

	/* Get the vector control register (offset 0xc) pointed by msix_idx */
	vec_ctrl = pdev->msix_base + msix_idx * PCI_MSIX_ENTRY_SIZE;
	vec_ctrl += PCI_MSIX_ENTRY_VECTOR_CTRL;

	val = readl(vec_ctrl);
	val &= ~PCI_MSIX_ENTRY_CTRL_ST;
	val |= FIELD_PREP(PCI_MSIX_ENTRY_CTRL_ST, tag);
	writel(val, vec_ctrl);

	/* Read back to flush the update */
	val = readl(vec_ctrl);

err_out:
	msi_unlock_descs(&pdev->dev);
	return err;
#else
	return -ENODEV;
#endif
}

/* Write tag to ST table - Return 0 if OK, otherwise -errno */
static int write_tag_to_st_table(struct pci_dev *pdev, int index, u16 tag)
{
	int st_table_size;
	int offset;

	/* Check if index is out of bound */
	st_table_size = get_st_table_size(pdev);
	if (index >= st_table_size)
		return -ENXIO;

	offset = pdev->tph_cap + PCI_TPH_BASE_SIZEOF + index * sizeof(u16);

	return pci_write_config_word(pdev, offset, tag);
}

/**
 * pcie_tph_get_cpu_st() - Retrieve Steering Tag for a target memory associated
 * with a specific CPU
 * @pdev: PCI device
 * @mem_type: target memory type (volatile or persistent RAM)
 * @cpu_uid: associated CPU id
 * @tag: Steering Tag to be returned
 *
 * Return the Steering Tag for a target memory that is associated with a
 * specific CPU as indicated by cpu_uid.
 *
 * Return: 0 if success, otherwise negative value (-errno)
 */
int pcie_tph_get_cpu_st(struct pci_dev *pdev, enum tph_mem_type mem_type,
			unsigned int cpu_uid, u16 *tag)
{
#ifdef CONFIG_ACPI
	struct pci_dev *rp;
	acpi_handle rp_acpi_handle;
	union st_info info;

	rp = pcie_find_root_port(pdev);
	if (!rp || !rp->bus || !rp->bus->bridge)
		return -ENODEV;

	rp_acpi_handle = ACPI_HANDLE(rp->bus->bridge);

	if (tph_invoke_dsm(rp_acpi_handle, cpu_uid, &info) != AE_OK) {
		*tag = 0;
		return -EINVAL;
	}

	*tag = tph_extract_tag(mem_type, pdev->tph_req_type, &info);

	pci_dbg(pdev, "get steering tag: mem_type=%s, cpu_uid=%d, tag=%#04x\n",
		(mem_type == TPH_MEM_TYPE_VM) ? "volatile" : "persistent",
		cpu_uid, *tag);

	return 0;
#else
	return -ENODEV;
#endif
}
EXPORT_SYMBOL(pcie_tph_get_cpu_st);

/**
 * pcie_tph_set_st_entry() - Set Steering Tag in the ST table entry
 * @pdev: PCI device
 * @index: ST table entry index
 * @tag: Steering Tag to be written
 *
 * Figure out the proper location of ST table, either in the MSI-X table or
 * in the TPH Extended Capability space, and write the Steering Tag into
 * the ST entry pointed by index.
 *
 * Return: 0 if success, otherwise negative value (-errno)
 */
int pcie_tph_set_st_entry(struct pci_dev *pdev, unsigned int index, u16 tag)
{
	u32 loc;
	int err = 0;

	if (!pdev->tph_cap)
		return -EINVAL;

	if (!pdev->tph_enabled)
		return -EINVAL;

	/* No need to write tag if device is in "No ST Mode" */
	if (pdev->tph_mode == PCI_TPH_ST_NS_MODE)
		return 0;

	/*
	 * Disable TPH before updating ST to avoid potential instability as
	 * cautioned in PCIe r6.2, sec 6.17.3, "ST Modes of Operation"
	 */
	set_ctrl_reg_req_en(pdev, PCI_TPH_REQ_DISABLE);

	loc = get_st_table_loc(pdev);
	/* Convert loc to match with PCI_TPH_LOC_* */
	loc = FIELD_PREP(PCI_TPH_CAP_LOC_MASK, loc);

	switch (loc) {
	case PCI_TPH_LOC_MSIX:
		err = write_tag_to_msix(pdev, index, tag);
		break;
	case PCI_TPH_LOC_CAP:
		err = write_tag_to_st_table(pdev, index, tag);
		break;
	default:
		err = -EINVAL;
	}

	if (err) {
		pcie_disable_tph(pdev);
		return err;
	}

	set_ctrl_reg_req_en(pdev, pdev->tph_req_type);

	pci_dbg(pdev, "set steering tag: %s table, index=%d, tag=%#04x\n",
		(loc == PCI_TPH_LOC_MSIX) ? "MSI-X" : "ST", index, tag);

	return 0;
}
EXPORT_SYMBOL(pcie_tph_set_st_entry);

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
	int num_entries, i, offset;
	u16 *st_entry;
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
	st_entry = (u16 *)cap;
	offset = PCI_TPH_BASE_SIZEOF;
	num_entries = get_st_table_size(pdev);
	for (i = 0; i < num_entries; i++) {
		pci_write_config_word(pdev, pdev->tph_cap + offset,
				      *st_entry++);
		offset += sizeof(u16);
	}
}

void pci_save_tph_state(struct pci_dev *pdev)
{
	struct pci_cap_saved_state *save_state;
	int num_entries, i, offset;
	u16 *st_entry;
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

	/* Save all ST entries in extended capability structure */
	st_entry = (u16 *)cap;
	offset = PCI_TPH_BASE_SIZEOF;
	num_entries = get_st_table_size(pdev);
	for (i = 0; i < num_entries; i++) {
		pci_read_config_word(pdev, pdev->tph_cap + offset,
				     st_entry++);
		offset += sizeof(u16);
	}
}

void pci_no_tph(void)
{
	pci_tph_disabled = true;

	pr_info("PCIe TPH is disabled\n");
}

void pci_tph_init(struct pci_dev *pdev)
{
	int num_entries;
	u32 save_size;

	pdev->tph_cap = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_TPH);
	if (!pdev->tph_cap)
		return;

	num_entries = get_st_table_size(pdev);
	save_size = sizeof(u32) + num_entries * sizeof(u16);
	pci_add_ext_cap_save_buffer(pdev, PCI_EXT_CAP_ID_TPH, save_size);
}
