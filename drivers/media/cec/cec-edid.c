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

void cec_set_edid_phys_addr(u8 *edid, unsigned int size, u16 phys_addr)
{
	unsigned int loc = cec_get_edid_spa_location(edid, size);
	u8 sum = 0;
	unsigned int i;

	if (loc == 0)
		return;
	edid[loc] = phys_addr >> 8;
	edid[loc + 1] = phys_addr & 0xff;
	loc &= ~0x7f;

	/* update the checksum */
	for (i = loc; i < loc + 127; i++)
		sum += edid[i];
	edid[i] = 256 - sum;
}
EXPORT_SYMBOL_GPL(cec_set_edid_phys_addr);

u16 cec_phys_addr_for_input(u16 phys_addr, u8 input)
{
	/* Check if input is sane */
	if (WARN_ON(input == 0 || input > 0xf))
		return CEC_PHYS_ADDR_INVALID;

	if (phys_addr == 0)
		return input << 12;

	if ((phys_addr & 0x0fff) == 0)
		return phys_addr | (input << 8);

	if ((phys_addr & 0x00ff) == 0)
		return phys_addr | (input << 4);

	if ((phys_addr & 0x000f) == 0)
		return phys_addr | input;

	/*
	 * All nibbles are used so no valid physical addresses can be assigned
	 * to the input.
	 */
	return CEC_PHYS_ADDR_INVALID;
}
EXPORT_SYMBOL_GPL(cec_phys_addr_for_input);

int cec_phys_addr_validate(u16 phys_addr, u16 *parent, u16 *port)
{
	int i;

	if (parent)
		*parent = phys_addr;
	if (port)
		*port = 0;
	if (phys_addr == CEC_PHYS_ADDR_INVALID)
		return 0;
	for (i = 0; i < 16; i += 4)
		if (phys_addr & (0xf << i))
			break;
	if (i == 16)
		return 0;
	if (parent)
		*parent = phys_addr & (0xfff0 << i);
	if (port)
		*port = (phys_addr >> i) & 0xf;
	for (i += 4; i < 16; i += 4)
		if ((phys_addr & (0xf << i)) == 0)
			return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(cec_phys_addr_validate);
