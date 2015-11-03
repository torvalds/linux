/*
 * dim2_hdm.h - MediaLB DIM2 HDM Header
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */

#ifndef DIM2_HDM_H
#define	DIM2_HDM_H

struct device;

/* platform dependent data for dim2 interface */
struct dim2_platform_data {
	int (*init)(struct dim2_platform_data *pd, void *io_base, int clk_speed);
	void (*destroy)(struct dim2_platform_data *pd);
	void *priv;
};

#endif	/* DIM2_HDM_H */
