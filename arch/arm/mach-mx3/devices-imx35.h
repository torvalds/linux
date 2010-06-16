/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx35.h>
#include <mach/devices-common.h>

#define imx35_add_mxc_nand(pdata)	\
	imx_add_mxc_nand_v21(MX35_NFC_BASE_ADDR, MX35_INT_NANDFC, pdata)
