// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/module.h>
#include "cxl.h"

/**
 * DOC: cxl core
 *
 * The CXL core provides a sysfs hierarchy for control devices and a rendezvous
 * point for cross-device interleave coordination through cxl ports.
 */

/**
 * cxl_setup_device_regs() - Detect CXL Device register blocks
 * @dev: Host device of the @base mapping
 * @base: Mapping of CXL 2.0 8.2.8 CXL Device Register Interface
 * @regs: Base pointers for device register blocks (see CXL_DEVICE_REGS())
 */
void cxl_setup_device_regs(struct device *dev, void __iomem *base,
			   struct cxl_device_regs *regs)
{
	int cap, cap_count;
	u64 cap_array;

	*regs = (struct cxl_device_regs) { 0 };

	cap_array = readq(base + CXLDEV_CAP_ARRAY_OFFSET);
	if (FIELD_GET(CXLDEV_CAP_ARRAY_ID_MASK, cap_array) !=
	    CXLDEV_CAP_ARRAY_CAP_ID)
		return;

	cap_count = FIELD_GET(CXLDEV_CAP_ARRAY_COUNT_MASK, cap_array);

	for (cap = 1; cap <= cap_count; cap++) {
		void __iomem *register_block;
		u32 offset;
		u16 cap_id;

		cap_id = FIELD_GET(CXLDEV_CAP_HDR_CAP_ID_MASK,
				   readl(base + cap * 0x10));
		offset = readl(base + cap * 0x10 + 0x4);
		register_block = base + offset;

		switch (cap_id) {
		case CXLDEV_CAP_CAP_ID_DEVICE_STATUS:
			dev_dbg(dev, "found Status capability (0x%x)\n", offset);
			regs->status = register_block;
			break;
		case CXLDEV_CAP_CAP_ID_PRIMARY_MAILBOX:
			dev_dbg(dev, "found Mailbox capability (0x%x)\n", offset);
			regs->mbox = register_block;
			break;
		case CXLDEV_CAP_CAP_ID_SECONDARY_MAILBOX:
			dev_dbg(dev, "found Secondary Mailbox capability (0x%x)\n", offset);
			break;
		case CXLDEV_CAP_CAP_ID_MEMDEV:
			dev_dbg(dev, "found Memory Device capability (0x%x)\n", offset);
			regs->memdev = register_block;
			break;
		default:
			if (cap_id >= 0x8000)
				dev_dbg(dev, "Vendor cap ID: %#x offset: %#x\n", cap_id, offset);
			else
				dev_dbg(dev, "Unknown cap ID: %#x offset: %#x\n", cap_id, offset);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(cxl_setup_device_regs);

struct bus_type cxl_bus_type = {
	.name = "cxl",
};
EXPORT_SYMBOL_GPL(cxl_bus_type);

static __init int cxl_core_init(void)
{
	return bus_register(&cxl_bus_type);
}

static void cxl_core_exit(void)
{
	bus_unregister(&cxl_bus_type);
}

module_init(cxl_core_init);
module_exit(cxl_core_exit);
MODULE_LICENSE("GPL v2");
