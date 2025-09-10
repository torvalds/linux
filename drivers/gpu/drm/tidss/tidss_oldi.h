/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2025 - Texas Instruments Incorporated
 *
 * Aradhya Bhatia <a-bhatia1@ti.com>
 */

#ifndef __TIDSS_OLDI_H__
#define __TIDSS_OLDI_H__

#include "tidss_drv.h"

struct tidss_oldi;

/* OLDI PORTS */
#define OLDI_INPUT_PORT		0
#define OLDI_OUTPUT_PORT	1

/* Control MMR Registers */

/* Register offsets */
#define OLDI_PD_CTRL            0x100
#define OLDI_LB_CTRL            0x104

/* Power control bits */
#define OLDI_PWRDOWN_TX(n)	BIT(n)

/* LVDS Bandgap reference Enable/Disable */
#define OLDI_PWRDN_BG		BIT(8)

enum tidss_oldi_link_type {
	OLDI_MODE_UNSUPPORTED,
	OLDI_MODE_SINGLE_LINK,
	OLDI_MODE_CLONE_SINGLE_LINK,
	OLDI_MODE_SECONDARY_CLONE_SINGLE_LINK,
	OLDI_MODE_DUAL_LINK,
	OLDI_MODE_SECONDARY_DUAL_LINK,
};

int tidss_oldi_init(struct tidss_device *tidss);
void tidss_oldi_deinit(struct tidss_device *tidss);

#endif /* __TIDSS_OLDI_H__ */
