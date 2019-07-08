// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2010 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * basic modesetting functions
 */

#include <linux/kernel.h>
#include <linux/via-core.h>
#include "via_modesetting.h"
#include "share.h"
#include "debug.h"


void via_set_primary_timing(const struct via_display_timing *timing)
{
	struct via_display_timing raw;

	raw.hor_total = timing->hor_total / 8 - 5;
	raw.hor_addr = timing->hor_addr / 8 - 1;
	raw.hor_blank_start = timing->hor_blank_start / 8 - 1;
	raw.hor_blank_end = timing->hor_blank_end / 8 - 1;
	raw.hor_sync_start = timing->hor_sync_start / 8;
	raw.hor_sync_end = timing->hor_sync_end / 8;
	raw.ver_total = timing->ver_total - 2;
	raw.ver_addr = timing->ver_addr - 1;
	raw.ver_blank_start = timing->ver_blank_start - 1;
	raw.ver_blank_end = timing->ver_blank_end - 1;
	raw.ver_sync_start = timing->ver_sync_start - 1;
	raw.ver_sync_end = timing->ver_sync_end - 1;

	/* unlock timing registers */
	via_write_reg_mask(VIACR, 0x11, 0x00, 0x80);

	via_write_reg(VIACR, 0x00, raw.hor_total & 0xFF);
	via_write_reg(VIACR, 0x01, raw.hor_addr & 0xFF);
	via_write_reg(VIACR, 0x02, raw.hor_blank_start & 0xFF);
	via_write_reg_mask(VIACR, 0x03, raw.hor_blank_end & 0x1F, 0x1F);
	via_write_reg(VIACR, 0x04, raw.hor_sync_start & 0xFF);
	via_write_reg_mask(VIACR, 0x05, (raw.hor_sync_end & 0x1F)
		| (raw.hor_blank_end << (7 - 5) & 0x80), 0x9F);
	via_write_reg(VIACR, 0x06, raw.ver_total & 0xFF);
	via_write_reg_mask(VIACR, 0x07, (raw.ver_total >> 8 & 0x01)
		| (raw.ver_addr >> (8 - 1) & 0x02)
		| (raw.ver_sync_start >> (8 - 2) & 0x04)
		| (raw.ver_blank_start >> (8 - 3) & 0x08)
		| (raw.ver_total >> (9 - 5) & 0x20)
		| (raw.ver_addr >> (9 - 6) & 0x40)
		| (raw.ver_sync_start >> (9 - 7) & 0x80), 0xEF);
	via_write_reg_mask(VIACR, 0x09, raw.ver_blank_start >> (9 - 5) & 0x20,
		0x20);
	via_write_reg(VIACR, 0x10, raw.ver_sync_start & 0xFF);
	via_write_reg_mask(VIACR, 0x11, raw.ver_sync_end & 0x0F, 0x0F);
	via_write_reg(VIACR, 0x12, raw.ver_addr & 0xFF);
	via_write_reg(VIACR, 0x15, raw.ver_blank_start & 0xFF);
	via_write_reg(VIACR, 0x16, raw.ver_blank_end & 0xFF);
	via_write_reg_mask(VIACR, 0x33, (raw.hor_sync_start >> (8 - 4) & 0x10)
		| (raw.hor_blank_end >> (6 - 5) & 0x20), 0x30);
	via_write_reg_mask(VIACR, 0x35, (raw.ver_total >> 10 & 0x01)
		| (raw.ver_sync_start >> (10 - 1) & 0x02)
		| (raw.ver_addr >> (10 - 2) & 0x04)
		| (raw.ver_blank_start >> (10 - 3) & 0x08), 0x0F);
	via_write_reg_mask(VIACR, 0x36, raw.hor_total >> (8 - 3) & 0x08, 0x08);

	/* lock timing registers */
	via_write_reg_mask(VIACR, 0x11, 0x80, 0x80);

	/* reset timing control */
	via_write_reg_mask(VIACR, 0x17, 0x00, 0x80);
	via_write_reg_mask(VIACR, 0x17, 0x80, 0x80);
}

void via_set_secondary_timing(const struct via_display_timing *timing)
{
	struct via_display_timing raw;

	raw.hor_total = timing->hor_total - 1;
	raw.hor_addr = timing->hor_addr - 1;
	raw.hor_blank_start = timing->hor_blank_start - 1;
	raw.hor_blank_end = timing->hor_blank_end - 1;
	raw.hor_sync_start = timing->hor_sync_start - 1;
	raw.hor_sync_end = timing->hor_sync_end - 1;
	raw.ver_total = timing->ver_total - 1;
	raw.ver_addr = timing->ver_addr - 1;
	raw.ver_blank_start = timing->ver_blank_start - 1;
	raw.ver_blank_end = timing->ver_blank_end - 1;
	raw.ver_sync_start = timing->ver_sync_start - 1;
	raw.ver_sync_end = timing->ver_sync_end - 1;

	via_write_reg(VIACR, 0x50, raw.hor_total & 0xFF);
	via_write_reg(VIACR, 0x51, raw.hor_addr & 0xFF);
	via_write_reg(VIACR, 0x52, raw.hor_blank_start & 0xFF);
	via_write_reg(VIACR, 0x53, raw.hor_blank_end & 0xFF);
	via_write_reg(VIACR, 0x54, (raw.hor_blank_start >> 8 & 0x07)
		| (raw.hor_blank_end >> (8 - 3) & 0x38)
		| (raw.hor_sync_start >> (8 - 6) & 0xC0));
	via_write_reg_mask(VIACR, 0x55, (raw.hor_total >> 8 & 0x0F)
		| (raw.hor_addr >> (8 - 4) & 0x70), 0x7F);
	via_write_reg(VIACR, 0x56, raw.hor_sync_start & 0xFF);
	via_write_reg(VIACR, 0x57, raw.hor_sync_end & 0xFF);
	via_write_reg(VIACR, 0x58, raw.ver_total & 0xFF);
	via_write_reg(VIACR, 0x59, raw.ver_addr & 0xFF);
	via_write_reg(VIACR, 0x5A, raw.ver_blank_start & 0xFF);
	via_write_reg(VIACR, 0x5B, raw.ver_blank_end & 0xFF);
	via_write_reg(VIACR, 0x5C, (raw.ver_blank_start >> 8 & 0x07)
		| (raw.ver_blank_end >> (8 - 3) & 0x38)
		| (raw.hor_sync_end >> (8 - 6) & 0x40)
		| (raw.hor_sync_start >> (10 - 7) & 0x80));
	via_write_reg(VIACR, 0x5D, (raw.ver_total >> 8 & 0x07)
		| (raw.ver_addr >> (8 - 3) & 0x38)
		| (raw.hor_blank_end >> (11 - 6) & 0x40)
		| (raw.hor_sync_start >> (11 - 7) & 0x80));
	via_write_reg(VIACR, 0x5E, raw.ver_sync_start & 0xFF);
	via_write_reg(VIACR, 0x5F, (raw.ver_sync_end & 0x1F)
		| (raw.ver_sync_start >> (8 - 5) & 0xE0));
}

void via_set_primary_address(u32 addr)
{
	DEBUG_MSG(KERN_DEBUG "via_set_primary_address(0x%08X)\n", addr);
	via_write_reg(VIACR, 0x0D, addr & 0xFF);
	via_write_reg(VIACR, 0x0C, (addr >> 8) & 0xFF);
	via_write_reg(VIACR, 0x34, (addr >> 16) & 0xFF);
	via_write_reg_mask(VIACR, 0x48, (addr >> 24) & 0x1F, 0x1F);
}

void via_set_secondary_address(u32 addr)
{
	DEBUG_MSG(KERN_DEBUG "via_set_secondary_address(0x%08X)\n", addr);
	/* secondary display supports only quadword aligned memory */
	via_write_reg_mask(VIACR, 0x62, (addr >> 2) & 0xFE, 0xFE);
	via_write_reg(VIACR, 0x63, (addr >> 10) & 0xFF);
	via_write_reg(VIACR, 0x64, (addr >> 18) & 0xFF);
	via_write_reg_mask(VIACR, 0xA3, (addr >> 26) & 0x07, 0x07);
}

void via_set_primary_pitch(u32 pitch)
{
	DEBUG_MSG(KERN_DEBUG "via_set_primary_pitch(0x%08X)\n", pitch);
	/* spec does not say that first adapter skips 3 bits but old
	 * code did it and seems to be reasonable in analogy to 2nd adapter
	 */
	pitch = pitch >> 3;
	via_write_reg(VIACR, 0x13, pitch & 0xFF);
	via_write_reg_mask(VIACR, 0x35, (pitch >> (8 - 5)) & 0xE0, 0xE0);
}

void via_set_secondary_pitch(u32 pitch)
{
	DEBUG_MSG(KERN_DEBUG "via_set_secondary_pitch(0x%08X)\n", pitch);
	pitch = pitch >> 3;
	via_write_reg(VIACR, 0x66, pitch & 0xFF);
	via_write_reg_mask(VIACR, 0x67, (pitch >> 8) & 0x03, 0x03);
	via_write_reg_mask(VIACR, 0x71, (pitch >> (10 - 7)) & 0x80, 0x80);
}

void via_set_primary_color_depth(u8 depth)
{
	u8 value;

	DEBUG_MSG(KERN_DEBUG "via_set_primary_color_depth(%d)\n", depth);
	switch (depth) {
	case 8:
		value = 0x00;
		break;
	case 15:
		value = 0x04;
		break;
	case 16:
		value = 0x14;
		break;
	case 24:
		value = 0x0C;
		break;
	case 30:
		value = 0x08;
		break;
	default:
		printk(KERN_WARNING "via_set_primary_color_depth: "
			"Unsupported depth: %d\n", depth);
		return;
	}

	via_write_reg_mask(VIASR, 0x15, value, 0x1C);
}

void via_set_secondary_color_depth(u8 depth)
{
	u8 value;

	DEBUG_MSG(KERN_DEBUG "via_set_secondary_color_depth(%d)\n", depth);
	switch (depth) {
	case 8:
		value = 0x00;
		break;
	case 16:
		value = 0x40;
		break;
	case 24:
		value = 0xC0;
		break;
	case 30:
		value = 0x80;
		break;
	default:
		printk(KERN_WARNING "via_set_secondary_color_depth: "
			"Unsupported depth: %d\n", depth);
		return;
	}

	via_write_reg_mask(VIACR, 0x67, value, 0xC0);
}
