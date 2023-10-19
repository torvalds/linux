/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * This contains i.MX27-specific hardware definitions. For those
 * hardware pieces that are common between i.MX21 and i.MX27, have a
 * look at mx2x.h.
 */

#ifndef __MACH_MX27_H__
#define __MACH_MX27_H__

#define MX27_AIPI_BASE_ADDR		0x10000000
#define MX27_AIPI_SIZE			SZ_1M

#define MX27_SAHB1_BASE_ADDR		0x80000000
#define MX27_SAHB1_SIZE			SZ_1M

#define MX27_X_MEMC_BASE_ADDR		0xd8000000
#define MX27_X_MEMC_SIZE		SZ_1M

#define MX27_IO_P2V(x)			IMX_IO_P2V(x)

#endif /* ifndef __MACH_MX27_H__ */
