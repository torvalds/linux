/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#ifndef __TIDSS_DISPC_COEF_H__
#define __TIDSS_DISPC_COEF_H__

#include <linux/types.h>

struct tidss_scale_coefs {
	s16 c2[16];
	s16 c1[16];
	u16 c0[9];
};

const struct tidss_scale_coefs *tidss_get_scale_coefs(struct device *dev,
						      u32 firinc,
						      bool five_taps);

#endif
