/*
 * Copyright 2004-2006 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 by Sascha Hauer <kernel@pengutronix.de>
 * Copyright (C) 2009 by Jan Weitzel Phytec Messtechnik GmbH,
 *                       <armlinux@phytec.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <asm/mach/map.h>
#include <mach/iomux-v3.h>

static void __iomem *base;

static unsigned long iomux_v3_pad_alloc_map[0x200 / BITS_PER_LONG];

/*
 * setups a single pin:
 * 	- reserves the pin so that it is not claimed by another driver
 * 	- setups the iomux according to the configuration
 */
int mxc_iomux_v3_setup_pad(struct pad_desc *pad)
{
	unsigned int pad_ofs = pad->pad_ctrl_ofs;

	if (test_and_set_bit(pad_ofs >> 2, iomux_v3_pad_alloc_map))
		return -EBUSY;
	if (pad->mux_ctrl_ofs)
		__raw_writel(pad->mux_mode, base + pad->mux_ctrl_ofs);

	if (pad->select_input_ofs)
		__raw_writel(pad->select_input,
				base + pad->select_input_ofs);

	if (!(pad->pad_ctrl & NO_PAD_CTRL) && pad->pad_ctrl_ofs)
		__raw_writel(pad->pad_ctrl, base + pad->pad_ctrl_ofs);
	return 0;
}
EXPORT_SYMBOL(mxc_iomux_v3_setup_pad);

int mxc_iomux_v3_setup_multiple_pads(struct pad_desc *pad_list, unsigned count)
{
	struct pad_desc *p = pad_list;
	int i;
	int ret;

	for (i = 0; i < count; i++) {
		ret = mxc_iomux_v3_setup_pad(p);
		if (ret)
			goto setup_error;
		p++;
	}
	return 0;

setup_error:
	mxc_iomux_v3_release_multiple_pads(pad_list, i);
	return ret;
}
EXPORT_SYMBOL(mxc_iomux_v3_setup_multiple_pads);

void mxc_iomux_v3_release_pad(struct pad_desc *pad)
{
	unsigned int pad_ofs = pad->pad_ctrl_ofs;

	clear_bit(pad_ofs >> 2, iomux_v3_pad_alloc_map);
}
EXPORT_SYMBOL(mxc_iomux_v3_release_pad);

void mxc_iomux_v3_release_multiple_pads(struct pad_desc *pad_list, int count)
{
	struct pad_desc *p = pad_list;
	int i;

	for (i = 0; i < count; i++) {
		mxc_iomux_v3_release_pad(p);
		p++;
	}
}
EXPORT_SYMBOL(mxc_iomux_v3_release_multiple_pads);

void mxc_iomux_v3_init(void __iomem *iomux_v3_base)
{
	base = iomux_v3_base;
}
