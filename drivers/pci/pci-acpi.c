// SPDX-License-Identifier: GPL-2.0
/*
 * PCI support in ACPI
 *
 * Copyright (C) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (C) 2004 Tom Long Nguyen <tom.l.nguyen@intel.com>
 * Copyright (C) 2004 Intel Corp.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irqdomain.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/pci_hotplug.h>
#include <linux/module.h>
#include <linux/pci-acpi.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include "pci.h"

/*
 * The GUID is defined in the PCI Firmware Specification available here:
 * https://www.pcisig.com/members/downloads/pcifw_r3_1_13Dec10.pdf
 */
const guid_t pci_acpi_dsm_guid =
	GUID_INIT(0xe5c937d0, 0x3553, 0x4d7a,
		  0x91, 0x17, 0xea, 0x4d, 0x19, 0xc3, 0x43, 0x4d);

#if defined(CONFIG_PCI_QUIRKS) && defined(CONFIG_ARM64)
static int acpi_get_rc_addr(struct acpi_device *adev, struct resource *res)
{
	struct device *dev = &adev->dev;
	struct resource_entry *entry;
	struct list_head list;
	unsigned long flags;
	int ret;

	INIT_LIST_HEAD(&list);
	flags = IORESOURCE_MEM;
	ret = acpi_dev_get_resources(adev, &list,
				     acpi_dev_filter_resource_type_cb,
				     (void *) flags);
	if (ret < 0) {
		dev_err(dev, "failed to parse _CRS method, error code %d\n",
			ret);
		return ret;
	}

	if (ret == 0) {
		dev_err(dev, "no IO and memory resources present in _CRS\n");
		return -EINVAL;
	}

	entry = list_first_entry(&list, struct resource_entry, node);
	*res = *entry->res;
	acpi_dev_free_resource_list(&list);
	return 0;
}

static acpi_status acpi_match_rc(acpi_handle handle, u32 lvl, void *context,
				 void **retval)
{
	u16 *segment = context;
	unsigned long long uid;
	acpi_status status;

	status = acpi_evaluate_integer(handle, "_UID", NULL, &uid);
	if (ACPI_FAILURE(status) || uid != *segment)
		return AE_CTRL_DEPTH;

	*(acpi_handle *)retval = handle;
	return AE_CTRL_TERMINATE;
}

int acpi_get_rc_resources(struct device *dev, const char *hid, u16 segment,
			  struct resource *res)
{
	struct acpi_device *adev;
	acpi_status status;
	acpi_handle handle;
	int ret;

	status = acpi_get_devices(hid, acpi_match_rc, &segment, &handle);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "can't find _HID %s device to locate resources\n",
			hid);
		return -ENODEV;
	}

	ret = acpi_bus_get_device(handle, &adev);
	if (ret)
		return ret;

	ret = acpi_get_rc_addr(adev, res);
	if (ret) {
		dev_err(dev, "can't get resource from %s\n",
			dev_name(&adev->dev));
		return ret;
	}

	return 0;
}
#endif

phys_addr_t acpi_pci_root_get_mcfg_addr(acpi_handle handle)
{
	acpi_status status = AE_NOT_EXIST;
	unsigned long long mcfg_addr;

	if (handle)
		status = acpi_evaluate_integer(handle, METHOD_NAME__CBA,
					       NULL, &mcfg_addr);
	if (ACPI_FAILURE(status))
		return 0;

	return (phys_addr_t)mcfg_addr;
}

/* _HPX PCI Setting Record (Type 0); same as _HPP */
struct hpx_type0 {
	u32 revision;		/* Not present in _HPP */
	u8  cache_line_size;	/* Not applicable to PCIe */
	u8  latency_timer;	/* Not applicable to PCIe */
	u8  enable_serr;
	u8  enable_perr;
};

static struct hpx_type0 pci_default_type0 = {
	.revision = 1,
	.cache_line_size = 8,
	.latency_timer = 0x40,
	.enable_serr = 0,
	.enable_perr = 0,
};

static void program_hpx_type0(struct pci_dev *dev, struct hpx_type0 *hpx)
{
	u16 pci_cmd, pci_bctl;

	if (!hpx)
		hpx = &pci_default_type0;

	if (hpx->revision > 1) {
		pci_warn(dev, "PCI settings rev %d not supported; using defaults\n",
			 hpx->revision);
		hpx = &pci_default_type0;
	}

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, hpx->cache_line_size);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, hpx->latency_timer);
	pci_read_config_word(dev, PCI_COMMAND, &pci_cmd);
	if (hpx->enable_serr)
		pci_cmd |= PCI_COMMAND_SERR;
	if (hpx->enable_perr)
		pci_cmd |= PCI_COMMAND_PARITY;
	pci_write_config_word(dev, PCI_COMMAND, pci_cmd);

	/* Program bridge control value */
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_write_config_byte(dev, PCI_SEC_LATENCY_TIMER,
				      hpx->latency_timer);
		pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &pci_bctl);
		if (hpx->enable_perr)
			pci_bctl |= PCI_BRIDGE_CTL_PARITY;
		pci_write_config_word(dev, PCI_BRIDGE_CONTROL, pci_bctl);
	}
}

static acpi_status decode_type0_hpx_record(union acpi_object *record,
					   struct hpx_type0 *hpx0)
{
	int i;
	union acpi_object *fields = record->package.elements;
	u32 revision = fields[1].integer.value;

	switch (revision) {
	case 1:
		if (record->package.count != 6)
			return AE_ERROR;
		for (i = 2; i < 6; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;
		hpx0->revision        = revision;
		hpx0->cache_line_size = fields[2].integer.value;
		hpx0->latency_timer   = fields[3].integer.value;
		hpx0->enable_serr     = fields[4].integer.value;
		hpx0->enable_perr     = fields[5].integer.value;
		break;
	default:
		pr_warn("%s: Type 0 Revision %d record not supported\n",
		       __func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

/* _HPX PCI-X Setting Record (Type 1) */
struct hpx_type1 {
	u32 revision;
	u8  max_mem_read;
	u8  avg_max_split;
	u16 tot_max_split;
};

static void program_hpx_type1(struct pci_dev *dev, struct hpx_type1 *hpx)
{
	int pos;

	if (!hpx)
		return;

	pos = pci_find_capability(dev, PCI_CAP_ID_PCIX);
	if (!pos)
		return;

	pci_warn(dev, "PCI-X settings not supported\n");
}

static acpi_status decode_type1_hpx_record(union acpi_object *record,
					   struct hpx_type1 *hpx1)
{
	int i;
	union acpi_object *fields = record->package.elements;
	u32 revision = fields[1].integer.value;

	switch (revision) {
	case 1:
		if (record->package.count != 5)
			return AE_ERROR;
		for (i = 2; i < 5; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;
		hpx1->revision      = revision;
		hpx1->max_mem_read  = fields[2].integer.value;
		hpx1->avg_max_split = fields[3].integer.value;
		hpx1->tot_max_split = fields[4].integer.value;
		break;
	default:
		pr_warn("%s: Type 1 Revision %d record not supported\n",
		       __func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

static bool pcie_root_rcb_set(struct pci_dev *dev)
{
	struct pci_dev *rp = pcie_find_root_port(dev);
	u16 lnkctl;

	if (!rp)
		return false;

	pcie_capability_read_word(rp, PCI_EXP_LNKCTL, &lnkctl);
	if (lnkctl & PCI_EXP_LNKCTL_RCB)
		return true;

	return false;
}

/* _HPX PCI Express Setting Record (Type 2) */
struct hpx_type2 {
	u32 revision;
	u32 unc_err_mask_and;
	u32 unc_err_mask_or;
	u32 unc_err_sever_and;
	u32 unc_err_sever_or;
	u32 cor_err_mask_and;
	u32 cor_err_mask_or;
	u32 adv_err_cap_and;
	u32 adv_err_cap_or;
	u16 pci_exp_devctl_and;
	u16 pci_exp_devctl_or;
	u16 pci_exp_lnkctl_and;
	u16 pci_exp_lnkctl_or;
	u32 sec_unc_err_sever_and;
	u32 sec_unc_err_sever_or;
	u32 sec_unc_err_mask_and;
	u32 sec_unc_err_mask_or;
};

static void program_hpx_type2(struct pci_dev *dev, struct hpx_type2 *hpx)
{
	int pos;
	u32 reg32;

	if (!hpx)
		return;

	if (!pci_is_pcie(dev))
		return;

	if (hpx->revision > 1) {
		pci_warn(dev, "PCIe settings rev %d not supported\n",
			 hpx->revision);
		return;
	}

	/*
	 * Don't allow _HPX to change MPS or MRRS settings.  We manage
	 * those to make sure they're consistent with the rest of the
	 * platform.
	 */
	hpx->pci_exp_devctl_and |= PCI_EXP_DEVCTL_PAYLOAD |
				    PCI_EXP_DEVCTL_READRQ;
	hpx->pci_exp_devctl_or &= ~(PCI_EXP_DEVCTL_PAYLOAD |
				    PCI_EXP_DEVCTL_READRQ);

	/* Initialize Device Control Register */
	pcie_capability_clear_and_set_word(dev, PCI_EXP_DEVCTL,
			~hpx->pci_exp_devctl_and, hpx->pci_exp_devctl_or);

	/* Initialize Link Control Register */
	if (pcie_cap_has_lnkctl(dev)) {

		/*
		 * If the Root Port supports Read Completion Boundary of
		 * 128, set RCB to 128.  Otherwise, clear it.
		 */
		hpx->pci_exp_lnkctl_and |= PCI_EXP_LNKCTL_RCB;
		hpx->pci_exp_lnkctl_or &= ~PCI_EXP_LNKCTL_RCB;
		if (pcie_root_rcb_set(dev))
			hpx->pci_exp_lnkctl_or |= PCI_EXP_LNKCTL_RCB;

		pcie_capability_clear_and_set_word(dev, PCI_EXP_LNKCTL,
			~hpx->pci_exp_lnkctl_and, hpx->pci_exp_lnkctl_or);
	}

	/* Find Advanced Error Reporting Enhanced Capability */
	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return;

	/* Initialize Uncorrectable Error Mask Register */
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_MASK, &reg32);
	reg32 = (reg32 & hpx->unc_err_mask_and) | hpx->unc_err_mask_or;
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_MASK, reg32);

	/* Initialize Uncorrectable Error Severity Register */
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, &reg32);
	reg32 = (reg32 & hpx->unc_err_sever_and) | hpx->unc_err_sever_or;
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, reg32);

	/* Initialize Correctable Error Mask Register */
	pci_read_config_dword(dev, pos + PCI_ERR_COR_MASK, &reg32);
	reg32 = (reg32 & hpx->cor_err_mask_and) | hpx->cor_err_mask_or;
	pci_write_config_dword(dev, pos + PCI_ERR_COR_MASK, reg32);

	/* Initialize Advanced Error Capabilities and Control Register */
	pci_read_config_dword(dev, pos + PCI_ERR_CAP, &reg32);
	reg32 = (reg32 & hpx->adv_err_cap_and) | hpx->adv_err_cap_or;

	/* Don't enable ECRC generation or checking if unsupported */
	if (!(reg32 & PCI_ERR_CAP_ECRC_GENC))
		reg32 &= ~PCI_ERR_CAP_ECRC_GENE;
	if (!(reg32 & PCI_ERR_CAP_ECRC_CHKC))
		reg32 &= ~PCI_ERR_CAP_ECRC_CHKE;
	pci_write_config_dword(dev, pos + PCI_ERR_CAP, reg32);

	/*
	 * FIXME: The following two registers are not supported yet.
	 *
	 *   o Secondary Uncorrectable Error Severity Register
	 *   o Secondary Uncorrectable Error Mask Register
	 */
}

static acpi_status decode_type2_hpx_record(union acpi_object *record,
					   struct hpx_type2 *hpx2)
{
	int i;
	union acpi_object *fields = record->package.elements;
	u32 revision = fields[1].integer.value;

	switch (revision) {
	case 1:
		if (record->package.count != 18)
			return AE_ERROR;
		for (i = 2; i < 18; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;
		hpx2->revision      = revision;
		hpx2->unc_err_mask_and      = fields[2].integer.value;
		hpx2->unc_err_mask_or       = fields[3].integer.value;
		hpx2->unc_err_sever_and     = fields[4].integer.value;
		hpx2->unc_err_sever_or      = fields[5].integer.value;
		hpx2->cor_err_mask_and      = fields[6].integer.value;
		hpx2->cor_err_mask_or       = fields[7].integer.value;
		hpx2->adv_err_cap_and       = fields[8].integer.value;
		hpx2->adv_err_cap_or        = fields[9].integer.value;
		hpx2->pci_exp_devctl_and    = fields[10].integer.value;
		hpx2->pci_exp_devctl_or     = fields[11].integer.value;
		hpx2->pci_exp_lnkctl_and    = fields[12].integer.value;
		hpx2->pci_exp_lnkctl_or     = fields[13].integer.value;
		hpx2->sec_unc_err_sever_and = fields[14].integer.value;
		hpx2->sec_unc_err_sever_or  = fields[15].integer.value;
		hpx2->sec_unc_err_mask_and  = fields[16].integer.value;
		hpx2->sec_unc_err_mask_or   = fields[17].integer.value;
		break;
	default:
		pr_warn("%s: Type 2 Revision %d record not supported\n",
		       __func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

/* _HPX PCI Express Setting Record (Type 3) */
struct hpx_type3 {
	u16 device_type;
	u16 function_type;
	u16 config_space_location;
	u16 pci_exp_cap_id;
	u16 pci_exp_cap_ver;
	u16 pci_exp_vendor_id;
	u16 dvsec_id;
	u16 dvsec_rev;
	u16 match_offset;
	u32 match_mask_and;
	u32 match_value;
	u16 reg_offset;
	u32 reg_mask_and;
	u32 reg_mask_or;
};

enum hpx_type3_dev_type {
	HPX_TYPE_ENDPOINT	= BIT(0),
	HPX_TYPE_LEG_END	= BIT(1),
	HPX_TYPE_RC_END		= BIT(2),
	HPX_TYPE_RC_EC		= BIT(3),
	HPX_TYPE_ROOT_PORT	= BIT(4),
	HPX_TYPE_UPSTREAM	= BIT(5),
	HPX_TYPE_DOWNSTREAM	= BIT(6),
	HPX_TYPE_PCI_BRIDGE	= BIT(7),
	HPX_TYPE_PCIE_BRIDGE	= BIT(8),
};

static u16 hpx3_device_type(struct pci_dev *dev)
{
	u16 pcie_type = pci_pcie_type(dev);
	const int pcie_to_hpx3_type[] = {
		[PCI_EXP_TYPE_ENDPOINT]    = HPX_TYPE_ENDPOINT,
		[PCI_EXP_TYPE_LEG_END]     = HPX_TYPE_LEG_END,
		[PCI_EXP_TYPE_RC_END]      = HPX_TYPE_RC_END,
		[PCI_EXP_TYPE_RC_EC]       = HPX_TYPE_RC_EC,
		[PCI_EXP_TYPE_ROOT_PORT]   = HPX_TYPE_ROOT_PORT,
		[PCI_EXP_TYPE_UPSTREAM]    = HPX_TYPE_UPSTREAM,
		[PCI_EXP_TYPE_DOWNSTREAM]  = HPX_TYPE_DOWNSTREAM,
		[PCI_EXP_TYPE_PCI_BRIDGE]  = HPX_TYPE_PCI_BRIDGE,
		[PCI_EXP_TYPE_PCIE_BRIDGE] = HPX_TYPE_PCIE_BRIDGE,
	};

	if (pcie_type >= ARRAY_SIZE(pcie_to_hpx3_type))
		return 0;

	return pcie_to_hpx3_type[pcie_type];
}

enum hpx_type3_fn_type {
	HPX_FN_NORMAL		= BIT(0),
	HPX_FN_SRIOV_PHYS	= BIT(1),
	HPX_FN_SRIOV_VIRT	= BIT(2),
};

static u8 hpx3_function_type(struct pci_dev *dev)
{
	if (dev->is_virtfn)
		return HPX_FN_SRIOV_VIRT;
	else if (pci_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV) > 0)
		return HPX_FN_SRIOV_PHYS;
	else
		return HPX_FN_NORMAL;
}

static bool hpx3_cap_ver_matches(u8 pcie_cap_id, u8 hpx3_cap_id)
{
	u8 cap_ver = hpx3_cap_id & 0xf;

	if ((hpx3_cap_id & BIT(4)) && cap_ver >= pcie_cap_id)
		return true;
	else if (cap_ver == pcie_cap_id)
		return true;

	return false;
}

enum hpx_type3_cfg_loc {
	HPX_CFG_PCICFG		= 0,
	HPX_CFG_PCIE_CAP	= 1,
	HPX_CFG_PCIE_CAP_EXT	= 2,
	HPX_CFG_VEND_CAP	= 3,
	HPX_CFG_DVSEC		= 4,
	HPX_CFG_MAX,
};

static void program_hpx_type3_register(struct pci_dev *dev,
				       const struct hpx_type3 *reg)
{
	u32 match_reg, write_reg, header, orig_value;
	u16 pos;

	if (!(hpx3_device_type(dev) & reg->device_type))
		return;

	if (!(hpx3_function_type(dev) & reg->function_type))
		return;

	switch (reg->config_space_location) {
	case HPX_CFG_PCICFG:
		pos = 0;
		break;
	case HPX_CFG_PCIE_CAP:
		pos = pci_find_capability(dev, reg->pci_exp_cap_id);
		if (pos == 0)
			return;

		break;
	case HPX_CFG_PCIE_CAP_EXT:
		pos = pci_find_ext_capability(dev, reg->pci_exp_cap_id);
		if (pos == 0)
			return;

		pci_read_config_dword(dev, pos, &header);
		if (!hpx3_cap_ver_matches(PCI_EXT_CAP_VER(header),
					  reg->pci_exp_cap_ver))
			return;

		break;
	case HPX_CFG_VEND_CAP:	/* Fall through */
	case HPX_CFG_DVSEC:	/* Fall through */
	default:
		pci_warn(dev, "Encountered _HPX type 3 with unsupported config space location");
		return;
	}

	pci_read_config_dword(dev, pos + reg->match_offset, &match_reg);

	if ((match_reg & reg->match_mask_and) != reg->match_value)
		return;

	pci_read_config_dword(dev, pos + reg->reg_offset, &write_reg);
	orig_value = write_reg;
	write_reg &= reg->reg_mask_and;
	write_reg |= reg->reg_mask_or;

	if (orig_value == write_reg)
		return;

	pci_write_config_dword(dev, pos + reg->reg_offset, write_reg);

	pci_dbg(dev, "Applied _HPX3 at [0x%x]: 0x%08x -> 0x%08x",
		pos, orig_value, write_reg);
}

static void program_hpx_type3(struct pci_dev *dev, struct hpx_type3 *hpx)
{
	if (!hpx)
		return;

	if (!pci_is_pcie(dev))
		return;

	program_hpx_type3_register(dev, hpx);
}

static void parse_hpx3_register(struct hpx_type3 *hpx3_reg,
				union acpi_object *reg_fields)
{
	hpx3_reg->device_type            = reg_fields[0].integer.value;
	hpx3_reg->function_type          = reg_fields[1].integer.value;
	hpx3_reg->config_space_location  = reg_fields[2].integer.value;
	hpx3_reg->pci_exp_cap_id         = reg_fields[3].integer.value;
	hpx3_reg->pci_exp_cap_ver        = reg_fields[4].integer.value;
	hpx3_reg->pci_exp_vendor_id      = reg_fields[5].integer.value;
	hpx3_reg->dvsec_id               = reg_fields[6].integer.value;
	hpx3_reg->dvsec_rev              = reg_fields[7].integer.value;
	hpx3_reg->match_offset           = reg_fields[8].integer.value;
	hpx3_reg->match_mask_and         = reg_fields[9].integer.value;
	hpx3_reg->match_value            = reg_fields[10].integer.value;
	hpx3_reg->reg_offset             = reg_fields[11].integer.value;
	hpx3_reg->reg_mask_and           = reg_fields[12].integer.value;
	hpx3_reg->reg_mask_or            = reg_fields[13].integer.value;
}

static acpi_status program_type3_hpx_record(struct pci_dev *dev,
					   union acpi_object *record)
{
	union acpi_object *fields = record->package.elements;
	u32 desc_count, expected_length, revision;
	union acpi_object *reg_fields;
	struct hpx_type3 hpx3;
	int i;

	revision = fields[1].integer.value;
	switch (revision) {
	case 1:
		desc_count = fields[2].integer.value;
		expected_length = 3 + desc_count * 14;

		if (record->package.count != expected_length)
			return AE_ERROR;

		for (i = 2; i < expected_length; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;

		for (i = 0; i < desc_count; i++) {
			reg_fields = fields + 3 + i * 14;
			parse_hpx3_register(&hpx3, reg_fields);
			program_hpx_type3(dev, &hpx3);
		}

		break;
	default:
		printk(KERN_WARNING
			"%s: Type 3 Revision %d record not supported\n",
			__func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

static acpi_status acpi_run_hpx(struct pci_dev *dev, acpi_handle handle)
{
	acpi_status status;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *package, *record, *fields;
	struct hpx_type0 hpx0;
	struct hpx_type1 hpx1;
	struct hpx_type2 hpx2;
	u32 type;
	int i;

	status = acpi_evaluate_object(handle, "_HPX", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return status;

	package = (union acpi_object *)buffer.pointer;
	if (package->type != ACPI_TYPE_PACKAGE) {
		status = AE_ERROR;
		goto exit;
	}

	for (i = 0; i < package->package.count; i++) {
		record = &package->package.elements[i];
		if (record->type != ACPI_TYPE_PACKAGE) {
			status = AE_ERROR;
			goto exit;
		}

		fields = record->package.elements;
		if (fields[0].type != ACPI_TYPE_INTEGER ||
		    fields[1].type != ACPI_TYPE_INTEGER) {
			status = AE_ERROR;
			goto exit;
		}

		type = fields[0].integer.value;
		switch (type) {
		case 0:
			memset(&hpx0, 0, sizeof(hpx0));
			status = decode_type0_hpx_record(record, &hpx0);
			if (ACPI_FAILURE(status))
				goto exit;
			program_hpx_type0(dev, &hpx0);
			break;
		case 1:
			memset(&hpx1, 0, sizeof(hpx1));
			status = decode_type1_hpx_record(record, &hpx1);
			if (ACPI_FAILURE(status))
				goto exit;
			program_hpx_type1(dev, &hpx1);
			break;
		case 2:
			memset(&hpx2, 0, sizeof(hpx2));
			status = decode_type2_hpx_record(record, &hpx2);
			if (ACPI_FAILURE(status))
				goto exit;
			program_hpx_type2(dev, &hpx2);
			break;
		case 3:
			status = program_type3_hpx_record(dev, record);
			if (ACPI_FAILURE(status))
				goto exit;
			break;
		default:
			pr_err("%s: Type %d record not supported\n",
			       __func__, type);
			status = AE_ERROR;
			goto exit;
		}
	}
 exit:
	kfree(buffer.pointer);
	return status;
}

static acpi_status acpi_run_hpp(struct pci_dev *dev, acpi_handle handle)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *package, *fields;
	struct hpx_type0 hpx0;
	int i;

	memset(&hpx0, 0, sizeof(hpx0));

	status = acpi_evaluate_object(handle, "_HPP", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return status;

	package = (union acpi_object *) buffer.pointer;
	if (package->type != ACPI_TYPE_PACKAGE ||
	    package->package.count != 4) {
		status = AE_ERROR;
		goto exit;
	}

	fields = package->package.elements;
	for (i = 0; i < 4; i++) {
		if (fields[i].type != ACPI_TYPE_INTEGER) {
			status = AE_ERROR;
			goto exit;
		}
	}

	hpx0.revision        = 1;
	hpx0.cache_line_size = fields[0].integer.value;
	hpx0.latency_timer   = fields[1].integer.value;
	hpx0.enable_serr     = fields[2].integer.value;
	hpx0.enable_perr     = fields[3].integer.value;

	program_hpx_type0(dev, &hpx0);

exit:
	kfree(buffer.pointer);
	return status;
}

/* pci_acpi_program_hp_params
 *
 * @dev - the pci_dev for which we want parameters
 */
int pci_acpi_program_hp_params(struct pci_dev *dev)
{
	acpi_status status;
	acpi_handle handle, phandle;
	struct pci_bus *pbus;

	if (acpi_pci_disabled)
		return -ENODEV;

	handle = NULL;
	for (pbus = dev->bus; pbus; pbus = pbus->parent) {
		handle = acpi_pci_get_bridge_handle(pbus);
		if (handle)
			break;
	}

	/*
	 * _HPP settings apply to all child buses, until another _HPP is
	 * encountered. If we don't find an _HPP for the input pci dev,
	 * look for it in the parent device scope since that would apply to
	 * this pci dev.
	 */
	while (handle) {
		status = acpi_run_hpx(dev, handle);
		if (ACPI_SUCCESS(status))
			return 0;
		status = acpi_run_hpp(dev, handle);
		if (ACPI_SUCCESS(status))
			return 0;
		if (acpi_is_root_bridge(handle))
			break;
		status = acpi_get_parent(handle, &phandle);
		if (ACPI_FAILURE(status))
			break;
		handle = phandle;
	}
	return -ENODEV;
}

/**
 * pciehp_is_native - Check whether a hotplug port is handled by the OS
 * @bridge: Hotplug port to check
 *
 * Returns true if the given @bridge is handled by the native PCIe hotplug
 * driver.
 */
bool pciehp_is_native(struct pci_dev *bridge)
{
	const struct pci_host_bridge *host;
	u32 slot_cap;

	if (!IS_ENABLED(CONFIG_HOTPLUG_PCI_PCIE))
		return false;

	pcie_capability_read_dword(bridge, PCI_EXP_SLTCAP, &slot_cap);
	if (!(slot_cap & PCI_EXP_SLTCAP_HPC))
		return false;

	if (pcie_ports_native)
		return true;

	host = pci_find_host_bridge(bridge->bus);
	return host->native_pcie_hotplug;
}

/**
 * shpchp_is_native - Check whether a hotplug port is handled by the OS
 * @bridge: Hotplug port to check
 *
 * Returns true if the given @bridge is handled by the native SHPC hotplug
 * driver.
 */
bool shpchp_is_native(struct pci_dev *bridge)
{
	return bridge->shpc_managed;
}

/**
 * pci_acpi_wake_bus - Root bus wakeup notification fork function.
 * @context: Device wakeup context.
 */
static void pci_acpi_wake_bus(struct acpi_device_wakeup_context *context)
{
	struct acpi_device *adev;
	struct acpi_pci_root *root;

	adev = container_of(context, struct acpi_device, wakeup.context);
	root = acpi_driver_data(adev);
	pci_pme_wakeup_bus(root->bus);
}

/**
 * pci_acpi_wake_dev - PCI device wakeup notification work function.
 * @context: Device wakeup context.
 */
static void pci_acpi_wake_dev(struct acpi_device_wakeup_context *context)
{
	struct pci_dev *pci_dev;

	pci_dev = to_pci_dev(context->dev);

	if (pci_dev->pme_poll)
		pci_dev->pme_poll = false;

	if (pci_dev->current_state == PCI_D3cold) {
		pci_wakeup_event(pci_dev);
		pm_request_resume(&pci_dev->dev);
		return;
	}

	/* Clear PME Status if set. */
	if (pci_dev->pme_support)
		pci_check_pme_status(pci_dev);

	pci_wakeup_event(pci_dev);
	pm_request_resume(&pci_dev->dev);

	pci_pme_wakeup_bus(pci_dev->subordinate);
}

/**
 * pci_acpi_add_bus_pm_notifier - Register PM notifier for root PCI bus.
 * @dev: PCI root bridge ACPI device.
 */
acpi_status pci_acpi_add_bus_pm_notifier(struct acpi_device *dev)
{
	return acpi_add_pm_notifier(dev, NULL, pci_acpi_wake_bus);
}

/**
 * pci_acpi_add_pm_notifier - Register PM notifier for given PCI device.
 * @dev: ACPI device to add the notifier for.
 * @pci_dev: PCI device to check for the PME status if an event is signaled.
 */
acpi_status pci_acpi_add_pm_notifier(struct acpi_device *dev,
				     struct pci_dev *pci_dev)
{
	return acpi_add_pm_notifier(dev, &pci_dev->dev, pci_acpi_wake_dev);
}

/*
 * _SxD returns the D-state with the highest power
 * (lowest D-state number) supported in the S-state "x".
 *
 * If the devices does not have a _PRW
 * (Power Resources for Wake) supporting system wakeup from "x"
 * then the OS is free to choose a lower power (higher number
 * D-state) than the return value from _SxD.
 *
 * But if _PRW is enabled at S-state "x", the OS
 * must not choose a power lower than _SxD --
 * unless the device has an _SxW method specifying
 * the lowest power (highest D-state number) the device
 * may enter while still able to wake the system.
 *
 * ie. depending on global OS policy:
 *
 * if (_PRW at S-state x)
 *	choose from highest power _SxD to lowest power _SxW
 * else // no _PRW at S-state x
 *	choose highest power _SxD or any lower power
 */

static pci_power_t acpi_pci_choose_state(struct pci_dev *pdev)
{
	int acpi_state, d_max;

	if (pdev->no_d3cold)
		d_max = ACPI_STATE_D3_HOT;
	else
		d_max = ACPI_STATE_D3_COLD;
	acpi_state = acpi_pm_device_sleep_state(&pdev->dev, NULL, d_max);
	if (acpi_state < 0)
		return PCI_POWER_ERROR;

	switch (acpi_state) {
	case ACPI_STATE_D0:
		return PCI_D0;
	case ACPI_STATE_D1:
		return PCI_D1;
	case ACPI_STATE_D2:
		return PCI_D2;
	case ACPI_STATE_D3_HOT:
		return PCI_D3hot;
	case ACPI_STATE_D3_COLD:
		return PCI_D3cold;
	}
	return PCI_POWER_ERROR;
}

static struct acpi_device *acpi_pci_find_companion(struct device *dev);

static bool acpi_pci_bridge_d3(struct pci_dev *dev)
{
	const struct fwnode_handle *fwnode;
	struct acpi_device *adev;
	struct pci_dev *root;
	u8 val;

	if (!dev->is_hotplug_bridge)
		return false;

	/*
	 * Look for a special _DSD property for the root port and if it
	 * is set we know the hierarchy behind it supports D3 just fine.
	 */
	root = pci_find_pcie_root_port(dev);
	if (!root)
		return false;

	adev = ACPI_COMPANION(&root->dev);
	if (root == dev) {
		/*
		 * It is possible that the ACPI companion is not yet bound
		 * for the root port so look it up manually here.
		 */
		if (!adev && !pci_dev_is_added(root))
			adev = acpi_pci_find_companion(&root->dev);
	}

	if (!adev)
		return false;

	fwnode = acpi_fwnode_handle(adev);
	if (fwnode_property_read_u8(fwnode, "HotPlugSupportInD3", &val))
		return false;

	return val == 1;
}

static bool acpi_pci_power_manageable(struct pci_dev *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);
	return adev ? acpi_device_power_manageable(adev) : false;
}

static int acpi_pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);
	static const u8 state_conv[] = {
		[PCI_D0] = ACPI_STATE_D0,
		[PCI_D1] = ACPI_STATE_D1,
		[PCI_D2] = ACPI_STATE_D2,
		[PCI_D3hot] = ACPI_STATE_D3_HOT,
		[PCI_D3cold] = ACPI_STATE_D3_COLD,
	};
	int error = -EINVAL;

	/* If the ACPI device has _EJ0, ignore the device */
	if (!adev || acpi_has_method(adev->handle, "_EJ0"))
		return -ENODEV;

	switch (state) {
	case PCI_D3cold:
		if (dev_pm_qos_flags(&dev->dev, PM_QOS_FLAG_NO_POWER_OFF) ==
				PM_QOS_FLAGS_ALL) {
			error = -EBUSY;
			break;
		}
		/* Fall through */
	case PCI_D0:
	case PCI_D1:
	case PCI_D2:
	case PCI_D3hot:
		error = acpi_device_set_power(adev, state_conv[state]);
	}

	if (!error)
		pci_dbg(dev, "power state changed by ACPI to %s\n",
			 acpi_power_state_string(state_conv[state]));

	return error;
}

static pci_power_t acpi_pci_get_power_state(struct pci_dev *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);
	static const pci_power_t state_conv[] = {
		[ACPI_STATE_D0]      = PCI_D0,
		[ACPI_STATE_D1]      = PCI_D1,
		[ACPI_STATE_D2]      = PCI_D2,
		[ACPI_STATE_D3_HOT]  = PCI_D3hot,
		[ACPI_STATE_D3_COLD] = PCI_D3cold,
	};
	int state;

	if (!adev || !acpi_device_power_manageable(adev))
		return PCI_UNKNOWN;

	state = adev->power.state;
	if (state == ACPI_STATE_UNKNOWN)
		return PCI_UNKNOWN;

	return state_conv[state];
}

static void acpi_pci_refresh_power_state(struct pci_dev *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);

	if (adev && acpi_device_power_manageable(adev))
		acpi_device_update_power(adev, NULL);
}

static int acpi_pci_propagate_wakeup(struct pci_bus *bus, bool enable)
{
	while (bus->parent) {
		if (acpi_pm_device_can_wakeup(&bus->self->dev))
			return acpi_pm_set_bridge_wakeup(&bus->self->dev, enable);

		bus = bus->parent;
	}

	/* We have reached the root bus. */
	if (bus->bridge) {
		if (acpi_pm_device_can_wakeup(bus->bridge))
			return acpi_pm_set_bridge_wakeup(bus->bridge, enable);
	}
	return 0;
}

static int acpi_pci_wakeup(struct pci_dev *dev, bool enable)
{
	if (acpi_pm_device_can_wakeup(&dev->dev))
		return acpi_pm_set_device_wakeup(&dev->dev, enable);

	return acpi_pci_propagate_wakeup(dev->bus, enable);
}

static bool acpi_pci_need_resume(struct pci_dev *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);

	/*
	 * In some cases (eg. Samsung 305V4A) leaving a bridge in suspend over
	 * system-wide suspend/resume confuses the platform firmware, so avoid
	 * doing that.  According to Section 16.1.6 of ACPI 6.2, endpoint
	 * devices are expected to be in D3 before invoking the S3 entry path
	 * from the firmware, so they should not be affected by this issue.
	 */
	if (pci_is_bridge(dev) && acpi_target_system_state() != ACPI_STATE_S0)
		return true;

	if (!adev || !acpi_device_power_manageable(adev))
		return false;

	if (adev->wakeup.flags.valid &&
	    device_may_wakeup(&dev->dev) != !!adev->wakeup.prepare_count)
		return true;

	if (acpi_target_system_state() == ACPI_STATE_S0)
		return false;

	return !!adev->power.flags.dsw_present;
}

static const struct pci_platform_pm_ops acpi_pci_platform_pm = {
	.bridge_d3 = acpi_pci_bridge_d3,
	.is_manageable = acpi_pci_power_manageable,
	.set_state = acpi_pci_set_power_state,
	.get_state = acpi_pci_get_power_state,
	.refresh_state = acpi_pci_refresh_power_state,
	.choose_state = acpi_pci_choose_state,
	.set_wakeup = acpi_pci_wakeup,
	.need_resume = acpi_pci_need_resume,
};

void acpi_pci_add_bus(struct pci_bus *bus)
{
	union acpi_object *obj;
	struct pci_host_bridge *bridge;

	if (acpi_pci_disabled || !bus->bridge || !ACPI_HANDLE(bus->bridge))
		return;

	acpi_pci_slot_enumerate(bus);
	acpiphp_enumerate_slots(bus);

	/*
	 * For a host bridge, check its _DSM for function 8 and if
	 * that is available, mark it in pci_host_bridge.
	 */
	if (!pci_is_root_bus(bus))
		return;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(bus->bridge), &pci_acpi_dsm_guid, 3,
				RESET_DELAY_DSM, NULL);
	if (!obj)
		return;

	if (obj->type == ACPI_TYPE_INTEGER && obj->integer.value == 1) {
		bridge = pci_find_host_bridge(bus);
		bridge->ignore_reset_delay = 1;
	}
	ACPI_FREE(obj);
}

void acpi_pci_remove_bus(struct pci_bus *bus)
{
	if (acpi_pci_disabled || !bus->bridge)
		return;

	acpiphp_remove_slots(bus);
	acpi_pci_slot_remove(bus);
}

/* ACPI bus type */
static struct acpi_device *acpi_pci_find_companion(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	bool check_children;
	u64 addr;

	check_children = pci_is_bridge(pci_dev);
	/* Please ref to ACPI spec for the syntax of _ADR */
	addr = (PCI_SLOT(pci_dev->devfn) << 16) | PCI_FUNC(pci_dev->devfn);
	return acpi_find_child_device(ACPI_COMPANION(dev->parent), addr,
				      check_children);
}

/**
 * pci_acpi_optimize_delay - optimize PCI D3 and D3cold delay from ACPI
 * @pdev: the PCI device whose delay is to be updated
 * @handle: ACPI handle of this device
 *
 * Update the d3_delay and d3cold_delay of a PCI device from the ACPI _DSM
 * control method of either the device itself or the PCI host bridge.
 *
 * Function 8, "Reset Delay," applies to the entire hierarchy below a PCI
 * host bridge.  If it returns one, the OS may assume that all devices in
 * the hierarchy have already completed power-on reset delays.
 *
 * Function 9, "Device Readiness Durations," applies only to the object
 * where it is located.  It returns delay durations required after various
 * events if the device requires less time than the spec requires.  Delays
 * from this function take precedence over the Reset Delay function.
 *
 * These _DSM functions are defined by the draft ECN of January 28, 2014,
 * titled "ACPI additions for FW latency optimizations."
 */
static void pci_acpi_optimize_delay(struct pci_dev *pdev,
				    acpi_handle handle)
{
	struct pci_host_bridge *bridge = pci_find_host_bridge(pdev->bus);
	int value;
	union acpi_object *obj, *elements;

	if (bridge->ignore_reset_delay)
		pdev->d3cold_delay = 0;

	obj = acpi_evaluate_dsm(handle, &pci_acpi_dsm_guid, 3,
				FUNCTION_DELAY_DSM, NULL);
	if (!obj)
		return;

	if (obj->type == ACPI_TYPE_PACKAGE && obj->package.count == 5) {
		elements = obj->package.elements;
		if (elements[0].type == ACPI_TYPE_INTEGER) {
			value = (int)elements[0].integer.value / 1000;
			if (value < PCI_PM_D3COLD_WAIT)
				pdev->d3cold_delay = value;
		}
		if (elements[3].type == ACPI_TYPE_INTEGER) {
			value = (int)elements[3].integer.value / 1000;
			if (value < PCI_PM_D3_WAIT)
				pdev->d3_delay = value;
		}
	}
	ACPI_FREE(obj);
}

static void pci_acpi_set_untrusted(struct pci_dev *dev)
{
	u8 val;

	if (pci_pcie_type(dev) != PCI_EXP_TYPE_ROOT_PORT)
		return;
	if (device_property_read_u8(&dev->dev, "ExternalFacingPort", &val))
		return;

	/*
	 * These root ports expose PCIe (including DMA) outside of the
	 * system so make sure we treat them and everything behind as
	 * untrusted.
	 */
	if (val)
		dev->untrusted = 1;
}

static void pci_acpi_setup(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct acpi_device *adev = ACPI_COMPANION(dev);

	if (!adev)
		return;

	pci_acpi_optimize_delay(pci_dev, adev->handle);
	pci_acpi_set_untrusted(pci_dev);

	pci_acpi_add_pm_notifier(adev, pci_dev);
	if (!adev->wakeup.flags.valid)
		return;

	device_set_wakeup_capable(dev, true);
	/*
	 * For bridges that can do D3 we enable wake automatically (as
	 * we do for the power management itself in that case). The
	 * reason is that the bridge may have additional methods such as
	 * _DSW that need to be called.
	 */
	if (pci_dev->bridge_d3)
		device_wakeup_enable(dev);

	acpi_pci_wakeup(pci_dev, false);
	acpi_device_power_add_dependent(adev, dev);
}

static void pci_acpi_cleanup(struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct pci_dev *pci_dev = to_pci_dev(dev);

	if (!adev)
		return;

	pci_acpi_remove_pm_notifier(adev);
	if (adev->wakeup.flags.valid) {
		acpi_device_power_remove_dependent(adev, dev);
		if (pci_dev->bridge_d3)
			device_wakeup_disable(dev);

		device_set_wakeup_capable(dev, false);
	}
}

static bool pci_acpi_bus_match(struct device *dev)
{
	return dev_is_pci(dev);
}

static struct acpi_bus_type acpi_pci_bus = {
	.name = "PCI",
	.match = pci_acpi_bus_match,
	.find_companion = acpi_pci_find_companion,
	.setup = pci_acpi_setup,
	.cleanup = pci_acpi_cleanup,
};


static struct fwnode_handle *(*pci_msi_get_fwnode_cb)(struct device *dev);

/**
 * pci_msi_register_fwnode_provider - Register callback to retrieve fwnode
 * @fn:       Callback matching a device to a fwnode that identifies a PCI
 *            MSI domain.
 *
 * This should be called by irqchip driver, which is the parent of
 * the MSI domain to provide callback interface to query fwnode.
 */
void
pci_msi_register_fwnode_provider(struct fwnode_handle *(*fn)(struct device *))
{
	pci_msi_get_fwnode_cb = fn;
}

/**
 * pci_host_bridge_acpi_msi_domain - Retrieve MSI domain of a PCI host bridge
 * @bus:      The PCI host bridge bus.
 *
 * This function uses the callback function registered by
 * pci_msi_register_fwnode_provider() to retrieve the irq_domain with
 * type DOMAIN_BUS_PCI_MSI of the specified host bridge bus.
 * This returns NULL on error or when the domain is not found.
 */
struct irq_domain *pci_host_bridge_acpi_msi_domain(struct pci_bus *bus)
{
	struct fwnode_handle *fwnode;

	if (!pci_msi_get_fwnode_cb)
		return NULL;

	fwnode = pci_msi_get_fwnode_cb(&bus->dev);
	if (!fwnode)
		return NULL;

	return irq_find_matching_fwnode(fwnode, DOMAIN_BUS_PCI_MSI);
}

static int __init acpi_pci_init(void)
{
	int ret;

	if (acpi_gbl_FADT.boot_flags & ACPI_FADT_NO_MSI) {
		pr_info("ACPI FADT declares the system doesn't support MSI, so disable it\n");
		pci_no_msi();
	}

	if (acpi_gbl_FADT.boot_flags & ACPI_FADT_NO_ASPM) {
		pr_info("ACPI FADT declares the system doesn't support PCIe ASPM, so disable it\n");
		pcie_no_aspm();
	}

	ret = register_acpi_bus_type(&acpi_pci_bus);
	if (ret)
		return 0;

	pci_set_platform_pm(&acpi_pci_platform_pm);
	acpi_pci_slot_init();
	acpiphp_init();

	return 0;
}
arch_initcall(acpi_pci_init);
