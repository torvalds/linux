/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_DIGCTL_H__
#define __MACH_DIGCTL_H__

/* MXS DIGCTL SAIF CLKMUX */
#define MXS_DIGCTL_SAIF_CLKMUX_DIRECT		0x0
#define MXS_DIGCTL_SAIF_CLKMUX_CROSSINPUT	0x1
#define MXS_DIGCTL_SAIF_CLKMUX_EXTMSTR0		0x2
#define MXS_DIGCTL_SAIF_CLKMUX_EXTMSTR1		0x3

#define HW_DIGCTL_CTRL			0x0
#define  BP_DIGCTL_CTRL_SAIF_CLKMUX	10
#define  BM_DIGCTL_CTRL_SAIF_CLKMUX	(0x3 << 10)
#define HW_DIGCTL_CHIPID		0x310
#endif
