/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2009 Nokia Corporation.
 */

#ifndef __MACH_OMAP2_I2C_H
#define __MACH_OMAP2_I2C_H

struct omap_hwmod;

int omap_i2c_reset(struct omap_hwmod *oh);

#endif	/* __MACH_OMAP2_I2C_H */
