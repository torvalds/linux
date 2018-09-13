// SPDX-License-Identifier: GPL-2.0-only
/*
 * cec-edid - HDMI Consumer Electronics Control EDID & CEC helper functions
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <media/cec.h>

u16 cec_get_edid_phys_addr(const u8 *edid, unsigned int size,
			   unsigned int *offset)
{
	unsigned int loc = cec_get_edid_spa_location(edid, size);

	if (offset)
		*offset = loc;
	if (loc == 0)
		return CEC_PHYS_ADDR_INVALID;
	return (edid[loc] << 8) | edid[loc + 1];
}
EXPORT_SYMBOL_GPL(cec_get_edid_phys_addr);
