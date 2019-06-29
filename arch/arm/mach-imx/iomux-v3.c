// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2004-2006 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 by Sascha Hauer <kernel@pengutronix.de>
 * Copyright (C) 2009 by Jan Weitzel Phytec Messtechnik GmbH,
 *                       <armlinux@phytec.de>
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/gpio.h>

#include <asm/mach/map.h>

#include "hardware.h"
#include "iomux-v3.h"

static void __iomem *base;

/*
 * configures a single pad in the iomuxer
 */
int mxc_iomux_v3_setup_pad(iomux_v3_cfg_t pad)
{
	u32 mux_ctrl_ofs = (pad & MUX_CTRL_OFS_MASK) >> MUX_CTRL_OFS_SHIFT;
	u32 mux_mode = (pad & MUX_MODE_MASK) >> MUX_MODE_SHIFT;
	u32 sel_input_ofs = (pad & MUX_SEL_INPUT_OFS_MASK) >> MUX_SEL_INPUT_OFS_SHIFT;
	u32 sel_input = (pad & MUX_SEL_INPUT_MASK) >> MUX_SEL_INPUT_SHIFT;
	u32 pad_ctrl_ofs = (pad & MUX_PAD_CTRL_OFS_MASK) >> MUX_PAD_CTRL_OFS_SHIFT;
	u32 pad_ctrl = (pad & MUX_PAD_CTRL_MASK) >> MUX_PAD_CTRL_SHIFT;

	if (mux_ctrl_ofs)
		imx_writel(mux_mode, base + mux_ctrl_ofs);

	if (sel_input_ofs)
		imx_writel(sel_input, base + sel_input_ofs);

	if (!(pad_ctrl & NO_PAD_CTRL) && pad_ctrl_ofs)
		imx_writel(pad_ctrl, base + pad_ctrl_ofs);

	return 0;
}

int mxc_iomux_v3_setup_multiple_pads(const iomux_v3_cfg_t *pad_list,
		unsigned count)
{
	const iomux_v3_cfg_t *p = pad_list;
	int i;
	int ret;

	for (i = 0; i < count; i++) {
		ret = mxc_iomux_v3_setup_pad(*p);
		if (ret)
			return ret;
		p++;
	}
	return 0;
}

void mxc_iomux_v3_init(void __iomem *iomux_v3_base)
{
	base = iomux_v3_base;
}
