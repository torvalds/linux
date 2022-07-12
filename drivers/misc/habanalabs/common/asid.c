// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/slab.h>

int hl_asid_init(struct hl_device *hdev)
{
	hdev->asid_bitmap = bitmap_zalloc(hdev->asic_prop.max_asid, GFP_KERNEL);
	if (!hdev->asid_bitmap)
		return -ENOMEM;

	mutex_init(&hdev->asid_mutex);

	/* ASID 0 is reserved for the kernel driver and device CPU */
	set_bit(0, hdev->asid_bitmap);

	return 0;
}

void hl_asid_fini(struct hl_device *hdev)
{
	mutex_destroy(&hdev->asid_mutex);
	bitmap_free(hdev->asid_bitmap);
}

unsigned long hl_asid_alloc(struct hl_device *hdev)
{
	unsigned long found;

	mutex_lock(&hdev->asid_mutex);

	found = find_first_zero_bit(hdev->asid_bitmap,
					hdev->asic_prop.max_asid);
	if (found == hdev->asic_prop.max_asid)
		found = 0;
	else
		set_bit(found, hdev->asid_bitmap);

	mutex_unlock(&hdev->asid_mutex);

	return found;
}

void hl_asid_free(struct hl_device *hdev, unsigned long asid)
{
	if (asid == HL_KERNEL_ASID_ID || asid >= hdev->asic_prop.max_asid) {
		dev_crit(hdev->dev, "Invalid ASID %lu", asid);
		return;
	}

	clear_bit(asid, hdev->asid_bitmap);
}
