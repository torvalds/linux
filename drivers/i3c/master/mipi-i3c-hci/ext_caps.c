// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/kernel.h>
#include <linux/io.h>

#include "hci.h"
#include "ext_caps.h"
#include "xfer_mode_rate.h"


/* Extended Capability Header */
#define CAP_HEADER_LENGTH		GENMASK(23, 8)
#define CAP_HEADER_ID			GENMASK(7, 0)

static int hci_extcap_hardware_id(struct i3c_hci *hci, void __iomem *base)
{
	hci->vendor_mipi_id	= readl(base + 0x04);
	hci->vendor_version_id	= readl(base + 0x08);
	hci->vendor_product_id	= readl(base + 0x0c);

	dev_info(&hci->master.dev, "vendor MIPI ID: %#x\n", hci->vendor_mipi_id);
	dev_info(&hci->master.dev, "vendor version ID: %#x\n", hci->vendor_version_id);
	dev_info(&hci->master.dev, "vendor product ID: %#x\n", hci->vendor_product_id);

	/* ought to go in a table if this grows too much */
	switch (hci->vendor_mipi_id) {
	case MIPI_VENDOR_NXP:
		hci->quirks |= HCI_QUIRK_RAW_CCC;
		dev_dbg(&hci->master.dev, "raw CCC quirks set");
		break;
	}

	return 0;
}

static int hci_extcap_master_config(struct i3c_hci *hci, void __iomem *base)
{
	u32 master_config = readl(base + 0x04);
	unsigned int operation_mode = FIELD_GET(GENMASK(5, 4), master_config);
	static const char * const functionality[] = {
		"(unknown)", "master only", "target only",
		"primary/secondary master" };
	dev_info(&hci->master.dev, "operation mode: %s\n", functionality[operation_mode]);
	if (operation_mode & 0x1)
		return 0;
	dev_err(&hci->master.dev, "only master mode is currently supported\n");
	return -EOPNOTSUPP;
}

static int hci_extcap_multi_bus(struct i3c_hci *hci, void __iomem *base)
{
	u32 bus_instance = readl(base + 0x04);
	unsigned int count = FIELD_GET(GENMASK(3, 0), bus_instance);

	dev_info(&hci->master.dev, "%d bus instances\n", count);
	return 0;
}

static int hci_extcap_xfer_modes(struct i3c_hci *hci, void __iomem *base)
{
	u32 header = readl(base);
	u32 entries = FIELD_GET(CAP_HEADER_LENGTH, header) - 1;
	unsigned int index;

	dev_info(&hci->master.dev, "transfer mode table has %d entries\n",
		 entries);
	base += 4;  /* skip header */
	for (index = 0; index < entries; index++) {
		u32 mode_entry = readl(base);

		dev_dbg(&hci->master.dev, "mode %d: 0x%08x",
			index, mode_entry);
		/* TODO: will be needed when I3C core does more than SDR */
		base += 4;
	}

	return 0;
}

static int hci_extcap_xfer_rates(struct i3c_hci *hci, void __iomem *base)
{
	u32 header = readl(base);
	u32 entries = FIELD_GET(CAP_HEADER_LENGTH, header) - 1;
	u32 rate_entry;
	unsigned int index, rate, rate_id, mode_id;

	base += 4;  /* skip header */

	dev_info(&hci->master.dev, "available data rates:\n");
	for (index = 0; index < entries; index++) {
		rate_entry = readl(base);
		dev_dbg(&hci->master.dev, "entry %d: 0x%08x",
			index, rate_entry);
		rate = FIELD_GET(XFERRATE_ACTUAL_RATE_KHZ, rate_entry);
		rate_id = FIELD_GET(XFERRATE_RATE_ID, rate_entry);
		mode_id = FIELD_GET(XFERRATE_MODE_ID, rate_entry);
		dev_info(&hci->master.dev, "rate %d for %s = %d kHz\n",
			 rate_id,
			 mode_id == XFERRATE_MODE_I3C ? "I3C" :
			 mode_id == XFERRATE_MODE_I2C ? "I2C" :
			 "unknown mode",
			 rate);
		base += 4;
	}

	return 0;
}

static int hci_extcap_auto_command(struct i3c_hci *hci, void __iomem *base)
{
	u32 autocmd_ext_caps = readl(base + 0x04);
	unsigned int max_count = FIELD_GET(GENMASK(3, 0), autocmd_ext_caps);
	u32 autocmd_ext_config = readl(base + 0x08);
	unsigned int count = FIELD_GET(GENMASK(3, 0), autocmd_ext_config);

	dev_info(&hci->master.dev, "%d/%d active auto-command entries\n",
		 count, max_count);
	/* remember auto-command register location for later use */
	hci->AUTOCMD_regs = base;
	return 0;
}

static int hci_extcap_debug(struct i3c_hci *hci, void __iomem *base)
{
	dev_info(&hci->master.dev, "debug registers present\n");
	hci->DEBUG_regs = base;
	return 0;
}

static int hci_extcap_scheduled_cmd(struct i3c_hci *hci, void __iomem *base)
{
	dev_info(&hci->master.dev, "scheduled commands available\n");
	/* hci->schedcmd_regs = base; */
	return 0;
}

static int hci_extcap_non_curr_master(struct i3c_hci *hci, void __iomem *base)
{
	dev_info(&hci->master.dev, "Non-Current Master support available\n");
	/* hci->NCM_regs = base; */
	return 0;
}

static int hci_extcap_ccc_resp_conf(struct i3c_hci *hci, void __iomem *base)
{
	dev_info(&hci->master.dev, "CCC Response Configuration available\n");
	return 0;
}

static int hci_extcap_global_DAT(struct i3c_hci *hci, void __iomem *base)
{
	dev_info(&hci->master.dev, "Global DAT available\n");
	return 0;
}

static int hci_extcap_multilane(struct i3c_hci *hci, void __iomem *base)
{
	dev_info(&hci->master.dev, "Master Multi-Lane support available\n");
	return 0;
}

static int hci_extcap_ncm_multilane(struct i3c_hci *hci, void __iomem *base)
{
	dev_info(&hci->master.dev, "NCM Multi-Lane support available\n");
	return 0;
}

struct hci_ext_caps {
	u8  id;
	u16 min_length;
	int (*parser)(struct i3c_hci *hci, void __iomem *base);
};

#define EXT_CAP(_id, _highest_mandatory_reg_offset, _parser) \
	{ .id = (_id), .parser = (_parser), \
	  .min_length = (_highest_mandatory_reg_offset)/4 + 1 }

static const struct hci_ext_caps ext_capabilities[] = {
	EXT_CAP(0x01, 0x0c, hci_extcap_hardware_id),
	EXT_CAP(0x02, 0x04, hci_extcap_master_config),
	EXT_CAP(0x03, 0x04, hci_extcap_multi_bus),
	EXT_CAP(0x04, 0x24, hci_extcap_xfer_modes),
	EXT_CAP(0x05, 0x08, hci_extcap_auto_command),
	EXT_CAP(0x08, 0x40, hci_extcap_xfer_rates),
	EXT_CAP(0x0c, 0x10, hci_extcap_debug),
	EXT_CAP(0x0d, 0x0c, hci_extcap_scheduled_cmd),
	EXT_CAP(0x0e, 0x80, hci_extcap_non_curr_master), /* TODO confirm size */
	EXT_CAP(0x0f, 0x04, hci_extcap_ccc_resp_conf),
	EXT_CAP(0x10, 0x08, hci_extcap_global_DAT),
	EXT_CAP(0x9d, 0x04,	hci_extcap_multilane),
	EXT_CAP(0x9e, 0x04, hci_extcap_ncm_multilane),
};

static int hci_extcap_vendor_NXP(struct i3c_hci *hci, void __iomem *base)
{
	hci->vendor_data = (__force void *)base;
	dev_info(&hci->master.dev, "Build Date Info = %#x\n", readl(base + 1*4));
	/* reset the FPGA */
	writel(0xdeadbeef, base + 1*4);
	return 0;
}

struct hci_ext_cap_vendor_specific {
	u32 vendor;
	u8  cap;
	u16 min_length;
	int (*parser)(struct i3c_hci *hci, void __iomem *base);
};

#define EXT_CAP_VENDOR(_vendor, _cap, _highest_mandatory_reg_offset) \
	{ .vendor = (MIPI_VENDOR_##_vendor), .cap = (_cap), \
	  .parser = (hci_extcap_vendor_##_vendor), \
	  .min_length = (_highest_mandatory_reg_offset)/4 + 1 }

static const struct hci_ext_cap_vendor_specific vendor_ext_caps[] = {
	EXT_CAP_VENDOR(NXP, 0xc0, 0x20),
};

static int hci_extcap_vendor_specific(struct i3c_hci *hci, void __iomem *base,
				      u32 cap_id, u32 cap_length)
{
	const struct hci_ext_cap_vendor_specific *vendor_cap_entry;
	int i;

	vendor_cap_entry = NULL;
	for (i = 0; i < ARRAY_SIZE(vendor_ext_caps); i++) {
		if (vendor_ext_caps[i].vendor == hci->vendor_mipi_id &&
		    vendor_ext_caps[i].cap == cap_id) {
			vendor_cap_entry = &vendor_ext_caps[i];
			break;
		}
	}

	if (!vendor_cap_entry) {
		dev_notice(&hci->master.dev,
			   "unknown ext_cap 0x%02x for vendor 0x%02x\n",
			   cap_id, hci->vendor_mipi_id);
		return 0;
	}
	if (cap_length < vendor_cap_entry->min_length) {
		dev_err(&hci->master.dev,
			"ext_cap 0x%02x has size %d (expecting >= %d)\n",
			cap_id, cap_length, vendor_cap_entry->min_length);
		return -EINVAL;
	}
	return vendor_cap_entry->parser(hci, base);
}

int i3c_hci_parse_ext_caps(struct i3c_hci *hci)
{
	void __iomem *curr_cap = hci->EXTCAPS_regs;
	void __iomem *end = curr_cap + 0x1000; /* some arbitrary limit */
	u32 cap_header, cap_id, cap_length;
	const struct hci_ext_caps *cap_entry;
	int i, err = 0;

	if (!curr_cap)
		return 0;

	for (; !err && curr_cap < end; curr_cap += cap_length * 4) {
		cap_header = readl(curr_cap);
		cap_id = FIELD_GET(CAP_HEADER_ID, cap_header);
		cap_length = FIELD_GET(CAP_HEADER_LENGTH, cap_header);
		dev_dbg(&hci->master.dev, "id=0x%02x length=%d",
			cap_id, cap_length);
		if (!cap_length)
			break;
		if (curr_cap + cap_length * 4 >= end) {
			dev_err(&hci->master.dev,
				"ext_cap 0x%02x has size %d (too big)\n",
				cap_id, cap_length);
			err = -EINVAL;
			break;
		}

		if (cap_id >= 0xc0 && cap_id <= 0xcf) {
			err = hci_extcap_vendor_specific(hci, curr_cap,
							 cap_id, cap_length);
			continue;
		}

		cap_entry = NULL;
		for (i = 0; i < ARRAY_SIZE(ext_capabilities); i++) {
			if (ext_capabilities[i].id == cap_id) {
				cap_entry = &ext_capabilities[i];
				break;
			}
		}
		if (!cap_entry) {
			dev_notice(&hci->master.dev,
				   "unknown ext_cap 0x%02x\n", cap_id);
		} else if (cap_length < cap_entry->min_length) {
			dev_err(&hci->master.dev,
				"ext_cap 0x%02x has size %d (expecting >= %d)\n",
				cap_id, cap_length, cap_entry->min_length);
			err = -EINVAL;
		} else {
			err = cap_entry->parser(hci, curr_cap);
		}
	}
	return err;
}
