// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017 IBM Corp.
#include <linux/pci.h>
#include <asm/pnv-ocxl.h>
#include <misc/ocxl-config.h>
#include "ocxl_internal.h"

#define EXTRACT_BIT(val, bit) (!!(val & BIT(bit)))
#define EXTRACT_BITS(val, s, e) ((val & GENMASK(e, s)) >> s)

#define OCXL_DVSEC_AFU_IDX_MASK              GENMASK(5, 0)
#define OCXL_DVSEC_ACTAG_MASK                GENMASK(11, 0)
#define OCXL_DVSEC_PASID_MASK                GENMASK(19, 0)
#define OCXL_DVSEC_PASID_LOG_MASK            GENMASK(4, 0)

#define OCXL_DVSEC_TEMPL_VERSION         0x0
#define OCXL_DVSEC_TEMPL_NAME            0x4
#define OCXL_DVSEC_TEMPL_AFU_VERSION     0x1C
#define OCXL_DVSEC_TEMPL_MMIO_GLOBAL     0x20
#define OCXL_DVSEC_TEMPL_MMIO_GLOBAL_SZ  0x28
#define OCXL_DVSEC_TEMPL_MMIO_PP         0x30
#define OCXL_DVSEC_TEMPL_MMIO_PP_SZ      0x38
#define OCXL_DVSEC_TEMPL_ALL_MEM_SZ      0x3C
#define OCXL_DVSEC_TEMPL_LPC_MEM_START   0x40
#define OCXL_DVSEC_TEMPL_WWID            0x48
#define OCXL_DVSEC_TEMPL_LPC_MEM_SZ      0x58

#define OCXL_MAX_AFU_PER_FUNCTION 64
#define OCXL_TEMPL_LEN_1_0        0x58
#define OCXL_TEMPL_LEN_1_1        0x60
#define OCXL_TEMPL_NAME_LEN       24
#define OCXL_CFG_TIMEOUT     3

static int find_dvsec(struct pci_dev *dev, int dvsec_id)
{
	int vsec = 0;
	u16 vendor, id;

	while ((vsec = pci_find_next_ext_capability(dev, vsec,
						    OCXL_EXT_CAP_ID_DVSEC))) {
		pci_read_config_word(dev, vsec + OCXL_DVSEC_VENDOR_OFFSET,
				&vendor);
		pci_read_config_word(dev, vsec + OCXL_DVSEC_ID_OFFSET, &id);
		if (vendor == PCI_VENDOR_ID_IBM && id == dvsec_id)
			return vsec;
	}
	return 0;
}

static int find_dvsec_afu_ctrl(struct pci_dev *dev, u8 afu_idx)
{
	int vsec = 0;
	u16 vendor, id;
	u8 idx;

	while ((vsec = pci_find_next_ext_capability(dev, vsec,
						    OCXL_EXT_CAP_ID_DVSEC))) {
		pci_read_config_word(dev, vsec + OCXL_DVSEC_VENDOR_OFFSET,
				&vendor);
		pci_read_config_word(dev, vsec + OCXL_DVSEC_ID_OFFSET, &id);

		if (vendor == PCI_VENDOR_ID_IBM &&
			id == OCXL_DVSEC_AFU_CTRL_ID) {
			pci_read_config_byte(dev,
					vsec + OCXL_DVSEC_AFU_CTRL_AFU_IDX,
					&idx);
			if (idx == afu_idx)
				return vsec;
		}
	}
	return 0;
}

/**
 * get_function_0() - Find a related PCI device (function 0)
 * @dev: PCI device to match
 *
 * Returns a pointer to the related device, or null if not found
 */
static struct pci_dev *get_function_0(struct pci_dev *dev)
{
	unsigned int devfn = PCI_DEVFN(PCI_SLOT(dev->devfn), 0);

	return pci_get_domain_bus_and_slot(pci_domain_nr(dev->bus),
					   dev->bus->number, devfn);
}

static void read_pasid(struct pci_dev *dev, struct ocxl_fn_config *fn)
{
	u16 val;
	int pos;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_PASID);
	if (!pos) {
		/*
		 * PASID capability is not mandatory, but there
		 * shouldn't be any AFU
		 */
		dev_dbg(&dev->dev, "Function doesn't require any PASID\n");
		fn->max_pasid_log = -1;
		goto out;
	}
	pci_read_config_word(dev, pos + PCI_PASID_CAP, &val);
	fn->max_pasid_log = EXTRACT_BITS(val, 8, 12);

out:
	dev_dbg(&dev->dev, "PASID capability:\n");
	dev_dbg(&dev->dev, "  Max PASID log = %d\n", fn->max_pasid_log);
}

static int read_dvsec_tl(struct pci_dev *dev, struct ocxl_fn_config *fn)
{
	int pos;

	pos = find_dvsec(dev, OCXL_DVSEC_TL_ID);
	if (!pos && PCI_FUNC(dev->devfn) == 0) {
		dev_err(&dev->dev, "Can't find TL DVSEC\n");
		return -ENODEV;
	}
	if (pos && PCI_FUNC(dev->devfn) != 0) {
		dev_err(&dev->dev, "TL DVSEC is only allowed on function 0\n");
		return -ENODEV;
	}
	fn->dvsec_tl_pos = pos;
	return 0;
}

static int read_dvsec_function(struct pci_dev *dev, struct ocxl_fn_config *fn)
{
	int pos, afu_present;
	u32 val;

	pos = find_dvsec(dev, OCXL_DVSEC_FUNC_ID);
	if (!pos) {
		dev_err(&dev->dev, "Can't find function DVSEC\n");
		return -ENODEV;
	}
	fn->dvsec_function_pos = pos;

	pci_read_config_dword(dev, pos + OCXL_DVSEC_FUNC_OFF_INDEX, &val);
	afu_present = EXTRACT_BIT(val, 31);
	if (!afu_present) {
		fn->max_afu_index = -1;
		dev_dbg(&dev->dev, "Function doesn't define any AFU\n");
		goto out;
	}
	fn->max_afu_index = EXTRACT_BITS(val, 24, 29);

out:
	dev_dbg(&dev->dev, "Function DVSEC:\n");
	dev_dbg(&dev->dev, "  Max AFU index = %d\n", fn->max_afu_index);
	return 0;
}

static int read_dvsec_afu_info(struct pci_dev *dev, struct ocxl_fn_config *fn)
{
	int pos;

	if (fn->max_afu_index < 0) {
		fn->dvsec_afu_info_pos = -1;
		return 0;
	}

	pos = find_dvsec(dev, OCXL_DVSEC_AFU_INFO_ID);
	if (!pos) {
		dev_err(&dev->dev, "Can't find AFU information DVSEC\n");
		return -ENODEV;
	}
	fn->dvsec_afu_info_pos = pos;
	return 0;
}

static int read_dvsec_vendor(struct pci_dev *dev)
{
	int pos;
	u32 cfg, tlx, dlx, reset_reload;

	/*
	 * vendor specific DVSEC, for IBM images only. Some older
	 * images may not have it
	 *
	 * It's only used on function 0 to specify the version of some
	 * logic blocks and to give access to special registers to
	 * enable host-based flashing.
	 */
	if (PCI_FUNC(dev->devfn) != 0)
		return 0;

	pos = find_dvsec(dev, OCXL_DVSEC_VENDOR_ID);
	if (!pos)
		return 0;

	pci_read_config_dword(dev, pos + OCXL_DVSEC_VENDOR_CFG_VERS, &cfg);
	pci_read_config_dword(dev, pos + OCXL_DVSEC_VENDOR_TLX_VERS, &tlx);
	pci_read_config_dword(dev, pos + OCXL_DVSEC_VENDOR_DLX_VERS, &dlx);
	pci_read_config_dword(dev, pos + OCXL_DVSEC_VENDOR_RESET_RELOAD,
			      &reset_reload);

	dev_dbg(&dev->dev, "Vendor specific DVSEC:\n");
	dev_dbg(&dev->dev, "  CFG version = 0x%x\n", cfg);
	dev_dbg(&dev->dev, "  TLX version = 0x%x\n", tlx);
	dev_dbg(&dev->dev, "  DLX version = 0x%x\n", dlx);
	dev_dbg(&dev->dev, "  ResetReload = 0x%x\n", reset_reload);
	return 0;
}

static int get_dvsec_vendor0(struct pci_dev *dev, struct pci_dev **dev0,
			     int *out_pos)
{
	int pos;

	if (PCI_FUNC(dev->devfn) != 0) {
		dev = get_function_0(dev);
		if (!dev)
			return -1;
	}
	pos = find_dvsec(dev, OCXL_DVSEC_VENDOR_ID);
	if (!pos)
		return -1;
	*dev0 = dev;
	*out_pos = pos;
	return 0;
}

int ocxl_config_get_reset_reload(struct pci_dev *dev, int *val)
{
	struct pci_dev *dev0;
	u32 reset_reload;
	int pos;

	if (get_dvsec_vendor0(dev, &dev0, &pos))
		return -1;

	pci_read_config_dword(dev0, pos + OCXL_DVSEC_VENDOR_RESET_RELOAD,
			      &reset_reload);
	*val = !!(reset_reload & BIT(0));
	return 0;
}

int ocxl_config_set_reset_reload(struct pci_dev *dev, int val)
{
	struct pci_dev *dev0;
	u32 reset_reload;
	int pos;

	if (get_dvsec_vendor0(dev, &dev0, &pos))
		return -1;

	pci_read_config_dword(dev0, pos + OCXL_DVSEC_VENDOR_RESET_RELOAD,
			      &reset_reload);
	if (val)
		reset_reload |= BIT(0);
	else
		reset_reload &= ~BIT(0);
	pci_write_config_dword(dev0, pos + OCXL_DVSEC_VENDOR_RESET_RELOAD,
			       reset_reload);
	return 0;
}

static int validate_function(struct pci_dev *dev, struct ocxl_fn_config *fn)
{
	if (fn->max_pasid_log == -1 && fn->max_afu_index >= 0) {
		dev_err(&dev->dev,
			"AFUs are defined but no PASIDs are requested\n");
		return -EINVAL;
	}

	if (fn->max_afu_index > OCXL_MAX_AFU_PER_FUNCTION) {
		dev_err(&dev->dev,
			"Max AFU index out of architectural limit (%d vs %d)\n",
			fn->max_afu_index, OCXL_MAX_AFU_PER_FUNCTION);
		return -EINVAL;
	}
	return 0;
}

int ocxl_config_read_function(struct pci_dev *dev, struct ocxl_fn_config *fn)
{
	int rc;

	read_pasid(dev, fn);

	rc = read_dvsec_tl(dev, fn);
	if (rc) {
		dev_err(&dev->dev,
			"Invalid Transaction Layer DVSEC configuration: %d\n",
			rc);
		return -ENODEV;
	}

	rc = read_dvsec_function(dev, fn);
	if (rc) {
		dev_err(&dev->dev,
			"Invalid Function DVSEC configuration: %d\n", rc);
		return -ENODEV;
	}

	rc = read_dvsec_afu_info(dev, fn);
	if (rc) {
		dev_err(&dev->dev, "Invalid AFU configuration: %d\n", rc);
		return -ENODEV;
	}

	rc = read_dvsec_vendor(dev);
	if (rc) {
		dev_err(&dev->dev,
			"Invalid vendor specific DVSEC configuration: %d\n",
			rc);
		return -ENODEV;
	}

	rc = validate_function(dev, fn);
	return rc;
}
EXPORT_SYMBOL_GPL(ocxl_config_read_function);

static int read_afu_info(struct pci_dev *dev, struct ocxl_fn_config *fn,
			int offset, u32 *data)
{
	u32 val;
	unsigned long timeout = jiffies + (HZ * OCXL_CFG_TIMEOUT);
	int pos = fn->dvsec_afu_info_pos;

	/* Protect 'data valid' bit */
	if (EXTRACT_BIT(offset, 31)) {
		dev_err(&dev->dev, "Invalid offset in AFU info DVSEC\n");
		return -EINVAL;
	}

	pci_write_config_dword(dev, pos + OCXL_DVSEC_AFU_INFO_OFF, offset);
	pci_read_config_dword(dev, pos + OCXL_DVSEC_AFU_INFO_OFF, &val);
	while (!EXTRACT_BIT(val, 31)) {
		if (time_after_eq(jiffies, timeout)) {
			dev_err(&dev->dev,
				"Timeout while reading AFU info DVSEC (offset=%d)\n",
				offset);
			return -EBUSY;
		}
		cpu_relax();
		pci_read_config_dword(dev, pos + OCXL_DVSEC_AFU_INFO_OFF, &val);
	}
	pci_read_config_dword(dev, pos + OCXL_DVSEC_AFU_INFO_DATA, data);
	return 0;
}

/**
 * read_template_version() - Read the template version from the AFU
 * @dev: the device for the AFU
 * @fn: the AFU offsets
 * @len: outputs the template length
 * @version: outputs the major<<8,minor version
 *
 * Returns 0 on success, negative on failure
 */
static int read_template_version(struct pci_dev *dev, struct ocxl_fn_config *fn,
				 u16 *len, u16 *version)
{
	u32 val32;
	u8 major, minor;
	int rc;

	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_VERSION, &val32);
	if (rc)
		return rc;

	*len = EXTRACT_BITS(val32, 16, 31);
	major = EXTRACT_BITS(val32, 8, 15);
	minor = EXTRACT_BITS(val32, 0, 7);
	*version = (major << 8) + minor;
	return 0;
}

int ocxl_config_check_afu_index(struct pci_dev *dev,
				struct ocxl_fn_config *fn, int afu_idx)
{
	int rc;
	u16 templ_version;
	u16 len, expected_len;

	pci_write_config_byte(dev,
			fn->dvsec_afu_info_pos + OCXL_DVSEC_AFU_INFO_AFU_IDX,
			afu_idx);

	rc = read_template_version(dev, fn, &len, &templ_version);
	if (rc)
		return rc;

	/* AFU index map can have holes, in which case we read all 0's */
	if (!templ_version && !len)
		return 0;

	dev_dbg(&dev->dev, "AFU descriptor template version %d.%d\n",
		templ_version >> 8, templ_version & 0xFF);

	switch (templ_version) {
	case 0x0005: // v0.5 was used prior to the spec approval
	case 0x0100:
		expected_len = OCXL_TEMPL_LEN_1_0;
		break;
	case 0x0101:
		expected_len = OCXL_TEMPL_LEN_1_1;
		break;
	default:
		dev_warn(&dev->dev, "Unknown AFU template version %#x\n",
			templ_version);
		expected_len = len;
	}
	if (len != expected_len)
		dev_warn(&dev->dev,
			"Unexpected template length %#x in AFU information, expected %#x for version %#x\n",
			len, expected_len, templ_version);
	return 1;
}

static int read_afu_name(struct pci_dev *dev, struct ocxl_fn_config *fn,
			struct ocxl_afu_config *afu)
{
	int i, rc;
	u32 val, *ptr;

	BUILD_BUG_ON(OCXL_AFU_NAME_SZ < OCXL_TEMPL_NAME_LEN);
	for (i = 0; i < OCXL_TEMPL_NAME_LEN; i += 4) {
		rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_NAME + i, &val);
		if (rc)
			return rc;
		ptr = (u32 *) &afu->name[i];
		*ptr = le32_to_cpu((__force __le32) val);
	}
	afu->name[OCXL_AFU_NAME_SZ - 1] = '\0'; /* play safe */
	return 0;
}

static int read_afu_mmio(struct pci_dev *dev, struct ocxl_fn_config *fn,
			struct ocxl_afu_config *afu)
{
	int rc;
	u32 val;

	/*
	 * Global MMIO
	 */
	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_MMIO_GLOBAL, &val);
	if (rc)
		return rc;
	afu->global_mmio_bar = EXTRACT_BITS(val, 0, 2);
	afu->global_mmio_offset = EXTRACT_BITS(val, 16, 31) << 16;

	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_MMIO_GLOBAL + 4, &val);
	if (rc)
		return rc;
	afu->global_mmio_offset += (u64) val << 32;

	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_MMIO_GLOBAL_SZ, &val);
	if (rc)
		return rc;
	afu->global_mmio_size = val;

	/*
	 * Per-process MMIO
	 */
	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_MMIO_PP, &val);
	if (rc)
		return rc;
	afu->pp_mmio_bar = EXTRACT_BITS(val, 0, 2);
	afu->pp_mmio_offset = EXTRACT_BITS(val, 16, 31) << 16;

	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_MMIO_PP + 4, &val);
	if (rc)
		return rc;
	afu->pp_mmio_offset += (u64) val << 32;

	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_MMIO_PP_SZ, &val);
	if (rc)
		return rc;
	afu->pp_mmio_stride = val;

	return 0;
}

static int read_afu_control(struct pci_dev *dev, struct ocxl_afu_config *afu)
{
	int pos;
	u8 val8;
	u16 val16;

	pos = find_dvsec_afu_ctrl(dev, afu->idx);
	if (!pos) {
		dev_err(&dev->dev, "Can't find AFU control DVSEC for AFU %d\n",
			afu->idx);
		return -ENODEV;
	}
	afu->dvsec_afu_control_pos = pos;

	pci_read_config_byte(dev, pos + OCXL_DVSEC_AFU_CTRL_PASID_SUP, &val8);
	afu->pasid_supported_log = EXTRACT_BITS(val8, 0, 4);

	pci_read_config_word(dev, pos + OCXL_DVSEC_AFU_CTRL_ACTAG_SUP, &val16);
	afu->actag_supported = EXTRACT_BITS(val16, 0, 11);
	return 0;
}

static bool char_allowed(int c)
{
	/*
	 * Permitted Characters : Alphanumeric, hyphen, underscore, comma
	 */
	if ((c >= 0x30 && c <= 0x39) /* digits */ ||
		(c >= 0x41 && c <= 0x5A) /* upper case */ ||
		(c >= 0x61 && c <= 0x7A) /* lower case */ ||
		c == 0 /* NULL */ ||
		c == 0x2D /* - */ ||
		c == 0x5F /* _ */ ||
		c == 0x2C /* , */)
		return true;
	return false;
}

static int validate_afu(struct pci_dev *dev, struct ocxl_afu_config *afu)
{
	int i;

	if (!afu->name[0]) {
		dev_err(&dev->dev, "Empty AFU name\n");
		return -EINVAL;
	}
	for (i = 0; i < OCXL_TEMPL_NAME_LEN; i++) {
		if (!char_allowed(afu->name[i])) {
			dev_err(&dev->dev,
				"Invalid character in AFU name\n");
			return -EINVAL;
		}
	}

	if (afu->global_mmio_bar != 0 &&
		afu->global_mmio_bar != 2 &&
		afu->global_mmio_bar != 4) {
		dev_err(&dev->dev, "Invalid global MMIO bar number\n");
		return -EINVAL;
	}
	if (afu->pp_mmio_bar != 0 &&
		afu->pp_mmio_bar != 2 &&
		afu->pp_mmio_bar != 4) {
		dev_err(&dev->dev, "Invalid per-process MMIO bar number\n");
		return -EINVAL;
	}
	return 0;
}

/**
 * read_afu_lpc_memory_info() - Populate AFU metadata regarding LPC memory
 * @dev: the device for the AFU
 * @fn: the AFU offsets
 * @afu: the AFU struct to populate the LPC metadata into
 *
 * Returns 0 on success, negative on failure
 */
static int read_afu_lpc_memory_info(struct pci_dev *dev,
				    struct ocxl_fn_config *fn,
				    struct ocxl_afu_config *afu)
{
	int rc;
	u32 val32;
	u16 templ_version;
	u16 templ_len;
	u64 total_mem_size = 0;
	u64 lpc_mem_size = 0;

	afu->lpc_mem_offset = 0;
	afu->lpc_mem_size = 0;
	afu->special_purpose_mem_offset = 0;
	afu->special_purpose_mem_size = 0;
	/*
	 * For AFUs following template v1.0, the LPC memory covers the
	 * total memory. Its size is a power of 2.
	 *
	 * For AFUs with template >= v1.01, the total memory size is
	 * still a power of 2, but it is split in 2 parts:
	 * - the LPC memory, whose size can now be anything
	 * - the remainder memory is a special purpose memory, whose
	 *   definition is AFU-dependent. It is not accessible through
	 *   the usual commands for LPC memory
	 */
	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_ALL_MEM_SZ, &val32);
	if (rc)
		return rc;

	val32 = EXTRACT_BITS(val32, 0, 7);
	if (!val32)
		return 0; /* No LPC memory */

	/*
	 * The configuration space spec allows for a memory size of up
	 * to 2^255 bytes.
	 *
	 * Current generation hardware uses 56-bit physical addresses,
	 * but we won't be able to get near close to that, as we won't
	 * have a hole big enough in the memory map.  Let it pass in
	 * the driver for now. We'll get an error from the firmware
	 * when trying to configure something too big.
	 */
	total_mem_size = 1ull << val32;

	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_LPC_MEM_START, &val32);
	if (rc)
		return rc;

	afu->lpc_mem_offset = val32;

	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_LPC_MEM_START + 4, &val32);
	if (rc)
		return rc;

	afu->lpc_mem_offset |= (u64) val32 << 32;

	rc = read_template_version(dev, fn, &templ_len, &templ_version);
	if (rc)
		return rc;

	if (templ_version >= 0x0101) {
		rc = read_afu_info(dev, fn,
				OCXL_DVSEC_TEMPL_LPC_MEM_SZ, &val32);
		if (rc)
			return rc;
		lpc_mem_size = val32;

		rc = read_afu_info(dev, fn,
				OCXL_DVSEC_TEMPL_LPC_MEM_SZ + 4, &val32);
		if (rc)
			return rc;
		lpc_mem_size |= (u64) val32 << 32;
	} else {
		lpc_mem_size = total_mem_size;
	}
	afu->lpc_mem_size = lpc_mem_size;

	if (lpc_mem_size < total_mem_size) {
		afu->special_purpose_mem_offset =
			afu->lpc_mem_offset + lpc_mem_size;
		afu->special_purpose_mem_size =
			total_mem_size - lpc_mem_size;
	}
	return 0;
}

int ocxl_config_read_afu(struct pci_dev *dev, struct ocxl_fn_config *fn,
			struct ocxl_afu_config *afu, u8 afu_idx)
{
	int rc;
	u32 val32;

	/*
	 * First, we need to write the AFU idx for the AFU we want to
	 * access.
	 */
	WARN_ON((afu_idx & OCXL_DVSEC_AFU_IDX_MASK) != afu_idx);
	afu->idx = afu_idx;
	pci_write_config_byte(dev,
			fn->dvsec_afu_info_pos + OCXL_DVSEC_AFU_INFO_AFU_IDX,
			afu->idx);

	rc = read_afu_name(dev, fn, afu);
	if (rc)
		return rc;

	rc = read_afu_info(dev, fn, OCXL_DVSEC_TEMPL_AFU_VERSION, &val32);
	if (rc)
		return rc;
	afu->version_major = EXTRACT_BITS(val32, 24, 31);
	afu->version_minor = EXTRACT_BITS(val32, 16, 23);
	afu->afuc_type = EXTRACT_BITS(val32, 14, 15);
	afu->afum_type = EXTRACT_BITS(val32, 12, 13);
	afu->profile = EXTRACT_BITS(val32, 0, 7);

	rc = read_afu_mmio(dev, fn, afu);
	if (rc)
		return rc;

	rc = read_afu_lpc_memory_info(dev, fn, afu);
	if (rc)
		return rc;

	rc = read_afu_control(dev, afu);
	if (rc)
		return rc;

	dev_dbg(&dev->dev, "AFU configuration:\n");
	dev_dbg(&dev->dev, "  name = %s\n", afu->name);
	dev_dbg(&dev->dev, "  version = %d.%d\n", afu->version_major,
		afu->version_minor);
	dev_dbg(&dev->dev, "  global mmio bar = %hhu\n", afu->global_mmio_bar);
	dev_dbg(&dev->dev, "  global mmio offset = %#llx\n",
		afu->global_mmio_offset);
	dev_dbg(&dev->dev, "  global mmio size = %#x\n", afu->global_mmio_size);
	dev_dbg(&dev->dev, "  pp mmio bar = %hhu\n", afu->pp_mmio_bar);
	dev_dbg(&dev->dev, "  pp mmio offset = %#llx\n", afu->pp_mmio_offset);
	dev_dbg(&dev->dev, "  pp mmio stride = %#x\n", afu->pp_mmio_stride);
	dev_dbg(&dev->dev, "  lpc_mem offset = %#llx\n", afu->lpc_mem_offset);
	dev_dbg(&dev->dev, "  lpc_mem size = %#llx\n", afu->lpc_mem_size);
	dev_dbg(&dev->dev, "  special purpose mem offset = %#llx\n",
		afu->special_purpose_mem_offset);
	dev_dbg(&dev->dev, "  special purpose mem size = %#llx\n",
		afu->special_purpose_mem_size);
	dev_dbg(&dev->dev, "  pasid supported (log) = %u\n",
		afu->pasid_supported_log);
	dev_dbg(&dev->dev, "  actag supported = %u\n",
		afu->actag_supported);

	rc = validate_afu(dev, afu);
	return rc;
}
EXPORT_SYMBOL_GPL(ocxl_config_read_afu);

int ocxl_config_get_actag_info(struct pci_dev *dev, u16 *base, u16 *enabled,
			u16 *supported)
{
	int rc;

	/*
	 * This is really a simple wrapper for the kernel API, to
	 * avoid an external driver using ocxl as a library to call
	 * platform-dependent code
	 */
	rc = pnv_ocxl_get_actag(dev, base, enabled, supported);
	if (rc) {
		dev_err(&dev->dev, "Can't get actag for device: %d\n", rc);
		return rc;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ocxl_config_get_actag_info);

void ocxl_config_set_afu_actag(struct pci_dev *dev, int pos, int actag_base,
			int actag_count)
{
	u16 val;

	val = actag_count & OCXL_DVSEC_ACTAG_MASK;
	pci_write_config_byte(dev, pos + OCXL_DVSEC_AFU_CTRL_ACTAG_EN, val);

	val = actag_base & OCXL_DVSEC_ACTAG_MASK;
	pci_write_config_dword(dev, pos + OCXL_DVSEC_AFU_CTRL_ACTAG_BASE, val);
}
EXPORT_SYMBOL_GPL(ocxl_config_set_afu_actag);

int ocxl_config_get_pasid_info(struct pci_dev *dev, int *count)
{
	return pnv_ocxl_get_pasid_count(dev, count);
}

void ocxl_config_set_afu_pasid(struct pci_dev *dev, int pos, int pasid_base,
			u32 pasid_count_log)
{
	u8 val8;
	u32 val32;

	val8 = pasid_count_log & OCXL_DVSEC_PASID_LOG_MASK;
	pci_write_config_byte(dev, pos + OCXL_DVSEC_AFU_CTRL_PASID_EN, val8);

	pci_read_config_dword(dev, pos + OCXL_DVSEC_AFU_CTRL_PASID_BASE,
			&val32);
	val32 &= ~OCXL_DVSEC_PASID_MASK;
	val32 |= pasid_base & OCXL_DVSEC_PASID_MASK;
	pci_write_config_dword(dev, pos + OCXL_DVSEC_AFU_CTRL_PASID_BASE,
			val32);
}
EXPORT_SYMBOL_GPL(ocxl_config_set_afu_pasid);

void ocxl_config_set_afu_state(struct pci_dev *dev, int pos, int enable)
{
	u8 val;

	pci_read_config_byte(dev, pos + OCXL_DVSEC_AFU_CTRL_ENABLE, &val);
	if (enable)
		val |= 1;
	else
		val &= 0xFE;
	pci_write_config_byte(dev, pos + OCXL_DVSEC_AFU_CTRL_ENABLE, val);
}
EXPORT_SYMBOL_GPL(ocxl_config_set_afu_state);

int ocxl_config_set_TL(struct pci_dev *dev, int tl_dvsec)
{
	u32 val;
	__be32 *be32ptr;
	u8 timers;
	int i, rc;
	long recv_cap;
	char *recv_rate;

	/*
	 * Skip on function != 0, as the TL can only be defined on 0
	 */
	if (PCI_FUNC(dev->devfn) != 0)
		return 0;

	recv_rate = kzalloc(PNV_OCXL_TL_RATE_BUF_SIZE, GFP_KERNEL);
	if (!recv_rate)
		return -ENOMEM;
	/*
	 * The spec defines 64 templates for messages in the
	 * Transaction Layer (TL).
	 *
	 * The host and device each support a subset, so we need to
	 * configure the transmitters on each side to send only
	 * templates the receiver understands, at a rate the receiver
	 * can process.  Per the spec, template 0 must be supported by
	 * everybody. That's the template which has been used by the
	 * host and device so far.
	 *
	 * The sending rate limit must be set before the template is
	 * enabled.
	 */

	/*
	 * Device -> host
	 */
	rc = pnv_ocxl_get_tl_cap(dev, &recv_cap, recv_rate,
				PNV_OCXL_TL_RATE_BUF_SIZE);
	if (rc)
		goto out;

	for (i = 0; i < PNV_OCXL_TL_RATE_BUF_SIZE; i += 4) {
		be32ptr = (__be32 *) &recv_rate[i];
		pci_write_config_dword(dev,
				tl_dvsec + OCXL_DVSEC_TL_SEND_RATE + i,
				be32_to_cpu(*be32ptr));
	}
	val = recv_cap >> 32;
	pci_write_config_dword(dev, tl_dvsec + OCXL_DVSEC_TL_SEND_CAP, val);
	val = recv_cap & GENMASK(31, 0);
	pci_write_config_dword(dev, tl_dvsec + OCXL_DVSEC_TL_SEND_CAP + 4, val);

	/*
	 * Host -> device
	 */
	for (i = 0; i < PNV_OCXL_TL_RATE_BUF_SIZE; i += 4) {
		pci_read_config_dword(dev,
				tl_dvsec + OCXL_DVSEC_TL_RECV_RATE + i,
				&val);
		be32ptr = (__be32 *) &recv_rate[i];
		*be32ptr = cpu_to_be32(val);
	}
	pci_read_config_dword(dev, tl_dvsec + OCXL_DVSEC_TL_RECV_CAP, &val);
	recv_cap = (long) val << 32;
	pci_read_config_dword(dev, tl_dvsec + OCXL_DVSEC_TL_RECV_CAP + 4, &val);
	recv_cap |= val;

	rc = pnv_ocxl_set_tl_conf(dev, recv_cap, __pa(recv_rate),
				PNV_OCXL_TL_RATE_BUF_SIZE);
	if (rc)
		goto out;

	/*
	 * Opencapi commands needing to be retried are classified per
	 * the TL in 2 groups: short and long commands.
	 *
	 * The short back off timer it not used for now. It will be
	 * for opencapi 4.0.
	 *
	 * The long back off timer is typically used when an AFU hits
	 * a page fault but the NPU is already processing one. So the
	 * AFU needs to wait before it can resubmit. Having a value
	 * too low doesn't break anything, but can generate extra
	 * traffic on the link.
	 * We set it to 1.6 us for now. It's shorter than, but in the
	 * same order of magnitude as the time spent to process a page
	 * fault.
	 */
	timers = 0x2 << 4; /* long timer = 1.6 us */
	pci_write_config_byte(dev, tl_dvsec + OCXL_DVSEC_TL_BACKOFF_TIMERS,
			timers);

	rc = 0;
out:
	kfree(recv_rate);
	return rc;
}
EXPORT_SYMBOL_GPL(ocxl_config_set_TL);

int ocxl_config_terminate_pasid(struct pci_dev *dev, int afu_control, int pasid)
{
	u32 val;
	unsigned long timeout;

	pci_read_config_dword(dev, afu_control + OCXL_DVSEC_AFU_CTRL_TERM_PASID,
			&val);
	if (EXTRACT_BIT(val, 20)) {
		dev_err(&dev->dev,
			"Can't terminate PASID %#x, previous termination didn't complete\n",
			pasid);
		return -EBUSY;
	}

	val &= ~OCXL_DVSEC_PASID_MASK;
	val |= pasid & OCXL_DVSEC_PASID_MASK;
	val |= BIT(20);
	pci_write_config_dword(dev,
			afu_control + OCXL_DVSEC_AFU_CTRL_TERM_PASID,
			val);

	timeout = jiffies + (HZ * OCXL_CFG_TIMEOUT);
	pci_read_config_dword(dev, afu_control + OCXL_DVSEC_AFU_CTRL_TERM_PASID,
			&val);
	while (EXTRACT_BIT(val, 20)) {
		if (time_after_eq(jiffies, timeout)) {
			dev_err(&dev->dev,
				"Timeout while waiting for AFU to terminate PASID %#x\n",
				pasid);
			return -EBUSY;
		}
		cpu_relax();
		pci_read_config_dword(dev,
				afu_control + OCXL_DVSEC_AFU_CTRL_TERM_PASID,
				&val);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ocxl_config_terminate_pasid);

void ocxl_config_set_actag(struct pci_dev *dev, int func_dvsec, u32 tag_first,
			u32 tag_count)
{
	u32 val;

	val = (tag_first & OCXL_DVSEC_ACTAG_MASK) << 16;
	val |= tag_count & OCXL_DVSEC_ACTAG_MASK;
	pci_write_config_dword(dev, func_dvsec + OCXL_DVSEC_FUNC_OFF_ACTAG,
			val);
}
EXPORT_SYMBOL_GPL(ocxl_config_set_actag);
