/*
 * Copyright (c) 2014, Patrik Jakobsson
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Authors: Patrik Jakobsson <patrik.r.jakobsson@gmail.com>
 */

#include "psb_drv.h"

#include "blitter.h"
#include "psb_reg.h"

/* Wait for the blitter to be completely idle */
int gma_blt_wait_idle(struct drm_psb_private *dev_priv)
{
	unsigned long stop = jiffies + HZ;
	int busy = 1;

	/* NOP for Cedarview */
	if (IS_CDV(dev_priv->dev))
		return 0;

	/* First do a quick check */
	if ((PSB_RSGX32(PSB_CR_2D_SOCIF) == _PSB_C2_SOCIF_EMPTY) &&
	    ((PSB_RSGX32(PSB_CR_2D_BLIT_STATUS) & _PSB_C2B_STATUS_BUSY) == 0))
		return 0;

	do {
		busy = (PSB_RSGX32(PSB_CR_2D_SOCIF) != _PSB_C2_SOCIF_EMPTY);
	} while (busy && !time_after_eq(jiffies, stop));

	if (busy)
		return -EBUSY;

	do {
		busy = ((PSB_RSGX32(PSB_CR_2D_BLIT_STATUS) &
			_PSB_C2B_STATUS_BUSY) != 0);
	} while (busy && !time_after_eq(jiffies, stop));

	/* If still busy, we probably have a hang */
	return (busy) ? -EBUSY : 0;
}
