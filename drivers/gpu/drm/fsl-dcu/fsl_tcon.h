/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2015 Toradex AG
 *
 * Stefan Agner <stefan@agner.ch>
 *
 * Freescale TCON device driver
 */

#ifndef __FSL_TCON_H__
#define __FSL_TCON_H__

#include <linux/bitops.h>

#define FSL_TCON_CTRL1			0x0
#define FSL_TCON_CTRL1_TCON_BYPASS	BIT(29)

struct fsl_tcon {
	struct regmap		*regs;
	struct clk		*ipg_clk;
};

struct fsl_tcon *fsl_tcon_init(struct device *dev);
void fsl_tcon_free(struct fsl_tcon *tcon);

void fsl_tcon_bypass_disable(struct fsl_tcon *tcon);
void fsl_tcon_bypass_enable(struct fsl_tcon *tcon);

#endif /* __FSL_TCON_H__ */
