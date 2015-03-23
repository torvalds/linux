/*
 * rcar_du_group.c  --  R-Car Display Unit Channels Pair
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * The R8A7779 DU is split in per-CRTC resources (scan-out engine, blending
 * unit, timings generator, ...) and device-global resources (start/stop
 * control, planes, ...) shared between the two CRTCs.
 *
 * The R8A7790 introduced a third CRTC with its own set of global resources.
 * This would be modeled as two separate DU device instances if it wasn't for
 * a handful or resources that are shared between the three CRTCs (mostly
 * related to input and output routing). For this reason the R8A7790 DU must be
 * modeled as a single device with three CRTCs, two sets of "semi-global"
 * resources, and a few device-global resources.
 *
 * The rcar_du_group object is a driver specific object, without any real
 * counterpart in the DU documentation, that models those semi-global resources.
 */

#include <linux/clk.h>
#include <linux/io.h>

#include "rcar_du_drv.h"
#include "rcar_du_group.h"
#include "rcar_du_regs.h"

u32 rcar_du_group_read(struct rcar_du_group *rgrp, u32 reg)
{
	return rcar_du_read(rgrp->dev, rgrp->mmio_offset + reg);
}

void rcar_du_group_write(struct rcar_du_group *rgrp, u32 reg, u32 data)
{
	rcar_du_write(rgrp->dev, rgrp->mmio_offset + reg, data);
}

static void rcar_du_group_setup_defr8(struct rcar_du_group *rgrp)
{
	u32 defr8 = DEFR8_CODE | DEFR8_DEFE8;

	/* The DEFR8 register for the first group also controls RGB output
	 * routing to DPAD0
	 */
	if (rgrp->index == 0)
		defr8 |= DEFR8_DRGBS_DU(rgrp->dev->dpad0_source);

	rcar_du_group_write(rgrp, DEFR8, defr8);
}

static void rcar_du_group_setup(struct rcar_du_group *rgrp)
{
	/* Enable extended features */
	rcar_du_group_write(rgrp, DEFR, DEFR_CODE | DEFR_DEFE);
	rcar_du_group_write(rgrp, DEFR2, DEFR2_CODE | DEFR2_DEFE2G);
	rcar_du_group_write(rgrp, DEFR3, DEFR3_CODE | DEFR3_DEFE3);
	rcar_du_group_write(rgrp, DEFR4, DEFR4_CODE);
	rcar_du_group_write(rgrp, DEFR5, DEFR5_CODE | DEFR5_DEFE5);

	if (rcar_du_has(rgrp->dev, RCAR_DU_FEATURE_EXT_CTRL_REGS)) {
		rcar_du_group_setup_defr8(rgrp);

		/* Configure input dot clock routing. We currently hardcode the
		 * configuration to routing DOTCLKINn to DUn.
		 */
		rcar_du_group_write(rgrp, DIDSR, DIDSR_CODE |
				    DIDSR_LCDS_DCLKIN(2) |
				    DIDSR_LCDS_DCLKIN(1) |
				    DIDSR_LCDS_DCLKIN(0) |
				    DIDSR_PDCS_CLK(2, 0) |
				    DIDSR_PDCS_CLK(1, 0) |
				    DIDSR_PDCS_CLK(0, 0));
	}

	/* Use DS1PR and DS2PR to configure planes priorities and connects the
	 * superposition 0 to DU0 pins. DU1 pins will be configured dynamically.
	 */
	rcar_du_group_write(rgrp, DORCR, DORCR_PG1D_DS1 | DORCR_DPRS);
}

/*
 * rcar_du_group_get - Acquire a reference to the DU channels group
 *
 * Acquiring the first reference setups core registers. A reference must be held
 * before accessing any hardware registers.
 *
 * This function must be called with the DRM mode_config lock held.
 *
 * Return 0 in case of success or a negative error code otherwise.
 */
int rcar_du_group_get(struct rcar_du_group *rgrp)
{
	if (rgrp->use_count)
		goto done;

	rcar_du_group_setup(rgrp);

done:
	rgrp->use_count++;
	return 0;
}

/*
 * rcar_du_group_put - Release a reference to the DU
 *
 * This function must be called with the DRM mode_config lock held.
 */
void rcar_du_group_put(struct rcar_du_group *rgrp)
{
	--rgrp->use_count;
}

static void __rcar_du_group_start_stop(struct rcar_du_group *rgrp, bool start)
{
	rcar_du_group_write(rgrp, DSYSR,
		(rcar_du_group_read(rgrp, DSYSR) & ~(DSYSR_DRES | DSYSR_DEN)) |
		(start ? DSYSR_DEN : DSYSR_DRES));
}

void rcar_du_group_start_stop(struct rcar_du_group *rgrp, bool start)
{
	/* Many of the configuration bits are only updated when the display
	 * reset (DRES) bit in DSYSR is set to 1, disabling *both* CRTCs. Some
	 * of those bits could be pre-configured, but others (especially the
	 * bits related to plane assignment to display timing controllers) need
	 * to be modified at runtime.
	 *
	 * Restart the display controller if a start is requested. Sorry for the
	 * flicker. It should be possible to move most of the "DRES-update" bits
	 * setup to driver initialization time and minimize the number of cases
	 * when the display controller will have to be restarted.
	 */
	if (start) {
		if (rgrp->used_crtcs++ != 0)
			__rcar_du_group_start_stop(rgrp, false);
		__rcar_du_group_start_stop(rgrp, true);
	} else {
		if (--rgrp->used_crtcs == 0)
			__rcar_du_group_start_stop(rgrp, false);
	}
}

void rcar_du_group_restart(struct rcar_du_group *rgrp)
{
	__rcar_du_group_start_stop(rgrp, false);
	__rcar_du_group_start_stop(rgrp, true);
}

static int rcar_du_set_dpad0_routing(struct rcar_du_device *rcdu)
{
	int ret;

	if (!rcar_du_has(rcdu, RCAR_DU_FEATURE_EXT_CTRL_REGS))
		return 0;

	/* RGB output routing to DPAD0 is configured in the DEFR8 register of
	 * the first group. As this function can be called with the DU0 and DU1
	 * CRTCs disabled, we need to enable the first group clock before
	 * accessing the register.
	 */
	ret = clk_prepare_enable(rcdu->crtcs[0].clock);
	if (ret < 0)
		return ret;

	rcar_du_group_setup_defr8(&rcdu->groups[0]);

	clk_disable_unprepare(rcdu->crtcs[0].clock);

	return 0;
}

int rcar_du_group_set_routing(struct rcar_du_group *rgrp)
{
	struct rcar_du_crtc *crtc0 = &rgrp->dev->crtcs[rgrp->index * 2];
	u32 dorcr = rcar_du_group_read(rgrp, DORCR);

	dorcr &= ~(DORCR_PG2T | DORCR_DK2S | DORCR_PG2D_MASK);

	/* Set the DPAD1 pins sources. Select CRTC 0 if explicitly requested and
	 * CRTC 1 in all other cases to avoid cloning CRTC 0 to DPAD0 and DPAD1
	 * by default.
	 */
	if (crtc0->outputs & BIT(RCAR_DU_OUTPUT_DPAD1))
		dorcr |= DORCR_PG2D_DS1;
	else
		dorcr |= DORCR_PG2T | DORCR_DK2S | DORCR_PG2D_DS2;

	rcar_du_group_write(rgrp, DORCR, dorcr);

	return rcar_du_set_dpad0_routing(rgrp->dev);
}
