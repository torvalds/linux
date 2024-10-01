/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 * Copyright 2021 NXP
 */

#ifndef _IMX_RPROC_H
#define _IMX_RPROC_H

/* address translation table */
struct imx_rproc_att {
	u32 da;	/* device address (From Cortex M4 view)*/
	u32 sa;	/* system bus address */
	u32 size; /* size of reg range */
	int flags;
};

/* Remote core start/stop method */
enum imx_rproc_method {
	IMX_RPROC_NONE,
	/* Through syscon regmap */
	IMX_RPROC_MMIO,
	/* Through ARM SMCCC */
	IMX_RPROC_SMC,
	/* Through System Control Unit API */
	IMX_RPROC_SCU_API,
};

struct imx_rproc_dcfg {
	u32				src_reg;
	u32				src_mask;
	u32				src_start;
	u32				src_stop;
	const struct imx_rproc_att	*att;
	size_t				att_size;
	enum imx_rproc_method		method;
};

#endif /* _IMX_RPROC_H */
