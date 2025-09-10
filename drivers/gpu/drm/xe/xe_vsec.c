// SPDX-License-Identifier: GPL-2.0
/* Copyright © 2024 Intel Corporation */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/errno.h>
#include <linux/intel_vsec.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_drv.h"
#include "xe_mmio.h"
#include "xe_platform_types.h"
#include "xe_pm.h"
#include "xe_vsec.h"

#include "regs/xe_pmt.h"

/* PMT GUID value for BMG devices.  NOTE: this is NOT a PCI id */
#define BMG_DEVICE_ID 0xE2F8

static struct intel_vsec_header bmg_telemetry = {
	.rev = 1,
	.length = 0x10,
	.id = VSEC_ID_TELEMETRY,
	.num_entries = 2,
	.entry_size = 4,
	.tbir = 0,
	.offset = BMG_DISCOVERY_OFFSET,
};

static struct intel_vsec_header bmg_crashlog = {
	.rev = 1,
	.length = 0x10,
	.id = VSEC_ID_CRASHLOG,
	.num_entries = 2,
	.entry_size = 6,
	.tbir = 0,
	.offset = BMG_DISCOVERY_OFFSET + 0x60,
};

static struct intel_vsec_header *bmg_capabilities[] = {
	&bmg_telemetry,
	&bmg_crashlog,
	NULL
};

enum xe_vsec {
	XE_VSEC_UNKNOWN = 0,
	XE_VSEC_BMG,
};

static struct intel_vsec_platform_info xe_vsec_info[] = {
	[XE_VSEC_BMG] = {
		.caps = VSEC_CAP_TELEMETRY | VSEC_CAP_CRASHLOG,
		.headers = bmg_capabilities,
	},
	{ }
};

/*
 * The GUID will have the following bits to decode:
 *   [0:3]   - {Telemetry space iteration number (0,1,..)}
 *   [4:7]   - Segment (SEGMENT_INDEPENDENT-0, Client-1, Server-2)
 *   [8:11]  - SOC_SKU
 *   [12:27] – Device ID – changes for each down bin SKU’s
 *   [28:29] - Capability Type (Crashlog-0, Telemetry Aggregator-1, Watcher-2)
 *   [30:31] - Record-ID (0-PUNIT, 1-OOBMSM_0, 2-OOBMSM_1)
 */
#define GUID_TELEM_ITERATION	GENMASK(3, 0)
#define GUID_SEGMENT		GENMASK(7, 4)
#define GUID_SOC_SKU		GENMASK(11, 8)
#define GUID_DEVICE_ID		GENMASK(27, 12)
#define GUID_CAP_TYPE		GENMASK(29, 28)
#define GUID_RECORD_ID		GENMASK(31, 30)

#define PUNIT_TELEMETRY_OFFSET		0x0200
#define PUNIT_WATCHER_OFFSET		0x14A0
#define OOBMSM_0_WATCHER_OFFSET		0x18D8
#define OOBMSM_1_TELEMETRY_OFFSET	0x1000

enum record_id {
	PUNIT,
	OOBMSM_0,
	OOBMSM_1,
};

enum capability {
	CRASHLOG,
	TELEMETRY,
	WATCHER,
};

static int xe_guid_decode(u32 guid, int *index, u32 *offset)
{
	u32 record_id = FIELD_GET(GUID_RECORD_ID, guid);
	u32 cap_type  = FIELD_GET(GUID_CAP_TYPE, guid);
	u32 device_id = FIELD_GET(GUID_DEVICE_ID, guid);

	if (device_id != BMG_DEVICE_ID)
		return -ENODEV;

	if (cap_type > WATCHER)
		return -EINVAL;

	*offset = 0;

	if (cap_type == CRASHLOG) {
		*index = record_id == PUNIT ? 2 : 4;
		return 0;
	}

	switch (record_id) {
	case PUNIT:
		*index = 0;
		if (cap_type == TELEMETRY)
			*offset = PUNIT_TELEMETRY_OFFSET;
		else
			*offset = PUNIT_WATCHER_OFFSET;
		break;

	case OOBMSM_0:
		*index = 1;
		if (cap_type == WATCHER)
			*offset = OOBMSM_0_WATCHER_OFFSET;
		break;

	case OOBMSM_1:
		*index = 1;
		if (cap_type == TELEMETRY)
			*offset = OOBMSM_1_TELEMETRY_OFFSET;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int xe_pmt_telem_read(struct pci_dev *pdev, u32 guid, u64 *data, loff_t user_offset,
		      u32 count)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);
	void __iomem *telem_addr = xe->mmio.regs + BMG_TELEMETRY_OFFSET;
	u32 mem_region;
	u32 offset;
	int ret;

	ret = xe_guid_decode(guid, &mem_region, &offset);
	if (ret)
		return ret;

	telem_addr += offset + user_offset;

	guard(mutex)(&xe->pmt.lock);

	/* indicate that we are not at an appropriate power level */
	if (!xe_pm_runtime_get_if_active(xe))
		return -ENODATA;

	/* set SoC re-mapper index register based on GUID memory region */
	xe_mmio_rmw32(xe_root_tile_mmio(xe), SG_REMAP_INDEX1, SG_REMAP_BITS,
		      REG_FIELD_PREP(SG_REMAP_BITS, mem_region));

	memcpy_fromio(data, telem_addr, count);
	xe_pm_runtime_put(xe);

	return count;
}

static struct pmt_callbacks xe_pmt_cb = {
	.read_telem = xe_pmt_telem_read,
};

static const int vsec_platforms[] = {
	[XE_BATTLEMAGE] = XE_VSEC_BMG,
};

static enum xe_vsec get_platform_info(struct xe_device *xe)
{
	if (xe->info.platform > XE_BATTLEMAGE)
		return XE_VSEC_UNKNOWN;

	return vsec_platforms[xe->info.platform];
}

/**
 * xe_vsec_init - Initialize resources and add intel_vsec auxiliary
 * interface
 * @xe: valid xe instance
 */
void xe_vsec_init(struct xe_device *xe)
{
	struct intel_vsec_platform_info *info;
	struct device *dev = xe->drm.dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	enum xe_vsec platform;

	platform = get_platform_info(xe);
	if (platform == XE_VSEC_UNKNOWN)
		return;

	info = &xe_vsec_info[platform];
	if (!info->headers)
		return;

	switch (platform) {
	case XE_VSEC_BMG:
		info->priv_data = &xe_pmt_cb;
		break;
	default:
		break;
	}

	/*
	 * Register a VSEC. Cleanup is handled using device managed
	 * resources.
	 */
	intel_vsec_register(pdev, info);
}
MODULE_IMPORT_NS("INTEL_VSEC");
