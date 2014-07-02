/*
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRM_TEGRA_MIPI_PHY_H
#define DRM_TEGRA_MIPI_PHY_H

/*
 * D-PHY timing parameters
 *
 * A detailed description of these parameters can be found in the  MIPI
 * Alliance Specification for D-PHY, Section 5.9 "Global Operation Timing
 * Parameters".
 *
 * All parameters are specified in nanoseconds.
 */
struct mipi_dphy_timing {
	unsigned int clkmiss;
	unsigned int clkpost;
	unsigned int clkpre;
	unsigned int clkprepare;
	unsigned int clksettle;
	unsigned int clktermen;
	unsigned int clktrail;
	unsigned int clkzero;
	unsigned int dtermen;
	unsigned int eot;
	unsigned int hsexit;
	unsigned int hsprepare;
	unsigned int hszero;
	unsigned int hssettle;
	unsigned int hsskip;
	unsigned int hstrail;
	unsigned int init;
	unsigned int lpx;
	unsigned int taget;
	unsigned int tago;
	unsigned int tasure;
	unsigned int wakeup;
};

int mipi_dphy_timing_get_default(struct mipi_dphy_timing *timing,
				 unsigned long period);
int mipi_dphy_timing_validate(struct mipi_dphy_timing *timing,
			      unsigned long period);

#endif
