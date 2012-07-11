/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx23.h>
#include <mach/devices-common.h>
#include <mach/mxsfb.h>
#include <linux/amba/bus.h>

static inline int mx23_add_duart(void)
{
	struct amba_device *d;

	d = amba_ahb_device_add(NULL, "duart", MX23_DUART_BASE_ADDR, SZ_8K,
				MX23_INT_DUART, 0, 0, 0);
	return IS_ERR(d) ? PTR_ERR(d) : 0;
}

extern const struct mxs_auart_data mx23_auart_data[] __initconst;
#define mx23_add_auart(id)	mxs_add_auart(&mx23_auart_data[id])
#define mx23_add_auart0()		mx23_add_auart(0)
#define mx23_add_auart1()		mx23_add_auart(1)

extern const struct mxs_gpmi_nand_data mx23_gpmi_nand_data __initconst;
#define mx23_add_gpmi_nand(pdata)	\
	mxs_add_gpmi_nand(pdata, &mx23_gpmi_nand_data)

extern const struct mxs_mxs_mmc_data mx23_mxs_mmc_data[] __initconst;
#define mx23_add_mxs_mmc(id, pdata) \
	mxs_add_mxs_mmc(&mx23_mxs_mmc_data[id], pdata)

#define mx23_add_mxs_pwm(id)		mxs_add_mxs_pwm(MX23_PWM_BASE_ADDR, id)

struct platform_device *__init mx23_add_mxsfb(
		const struct mxsfb_platform_data *pdata);

struct platform_device *__init mx23_add_rtc_stmp3xxx(void);
