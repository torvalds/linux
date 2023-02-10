// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2023 Linaro Ltd. */

#include <linux/platform_device.h>
#include <linux/io.h>

#include "gsi.h"
#include "gsi_reg.h"

/* GSI EE registers as a group are shifted downward by a fixed constant amount
 * for IPA versions 4.5 and beyond.  This applies to all GSI registers we use
 * *except* the ones that disable inter-EE interrupts for channels and event
 * channels.
 *
 * The "raw" (not adjusted) GSI register range is mapped, and a pointer to
 * the mapped range is held in gsi->virt_raw.  The inter-EE interrupt
 * registers are accessed using that pointer.
 *
 * Most registers are accessed using gsi->virt, which is a copy of the "raw"
 * pointer, adjusted downward by the fixed amount.
 */
#define GSI_EE_REG_ADJUST	0x0000d000			/* IPA v4.5+ */

/* Sets gsi->virt_raw and gsi->virt, and I/O maps the "gsi" memory range */
int gsi_reg_init(struct gsi *gsi, struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	resource_size_t size;
	u32 adjust;

	/* Get GSI memory range and map it */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gsi");
	if (!res) {
		dev_err(dev, "DT error getting \"gsi\" memory property\n");
		return -ENODEV;
	}

	size = resource_size(res);
	if (res->start > U32_MAX || size > U32_MAX - res->start) {
		dev_err(dev, "DT memory resource \"gsi\" out of range\n");
		return -EINVAL;
	}

	/* Make sure we can make our pointer adjustment if necessary */
	adjust = gsi->version < IPA_VERSION_4_5 ? 0 : GSI_EE_REG_ADJUST;
	if (res->start < adjust) {
		dev_err(dev, "DT memory resource \"gsi\" too low (< %u)\n",
			adjust);
		return -EINVAL;
	}

	gsi->virt_raw = ioremap(res->start, size);
	if (!gsi->virt_raw) {
		dev_err(dev, "unable to remap \"gsi\" memory\n");
		return -ENOMEM;
	}
	/* Most registers are accessed using an adjusted register range */
	gsi->virt = gsi->virt_raw - adjust;

	return 0;
}

/* Inverse of gsi_reg_init() */
void gsi_reg_exit(struct gsi *gsi)
{
	gsi->virt = NULL;
	iounmap(gsi->virt_raw);
	gsi->virt_raw = NULL;
}
