// SPDX-License-Identifier: GPL-2.0
/*
 * dim2.h - MediaLB DIM2 HDM Header
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 */

#ifndef DIM2_HDM_H
#define	DIM2_HDM_H

struct device;

/* platform dependent data for dim2 interface */
struct dim2_platform_data {
	int (*init)(struct dim2_platform_data *pd, void __iomem *io_base,
		    int clk_speed);
	void (*destroy)(struct dim2_platform_data *pd);
	void *priv;
};

#endif	/* DIM2_HDM_H */
