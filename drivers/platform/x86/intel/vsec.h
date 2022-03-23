/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VSEC_H
#define _VSEC_H

#include <linux/auxiliary_bus.h>
#include <linux/bits.h>

struct pci_dev;
struct resource;

enum intel_vsec_quirks {
	/* Watcher feature not supported */
	VSEC_QUIRK_NO_WATCHER	= BIT(0),

	/* Crashlog feature not supported */
	VSEC_QUIRK_NO_CRASHLOG	= BIT(1),

	/* Use shift instead of mask to read discovery table offset */
	VSEC_QUIRK_TABLE_SHIFT	= BIT(2),

	/* DVSEC not present (provided in driver data) */
	VSEC_QUIRK_NO_DVSEC	= BIT(3),
};

struct intel_vsec_device {
	struct auxiliary_device auxdev;
	struct pci_dev *pcidev;
	struct resource *resource;
	struct ida *ida;
	unsigned long quirks;
	int num_resources;
};

static inline struct intel_vsec_device *dev_to_ivdev(struct device *dev)
{
	return container_of(dev, struct intel_vsec_device, auxdev.dev);
}

static inline struct intel_vsec_device *auxdev_to_ivdev(struct auxiliary_device *auxdev)
{
	return container_of(auxdev, struct intel_vsec_device, auxdev);
}
#endif
