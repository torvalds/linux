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
#include <mach/mx28.h>
#include <mach/devices-common.h>
#include <mach/mxsfb.h>

extern const struct amba_device mx28_duart_device __initconst;
#define mx28_add_duart() \
	mxs_add_duart(&mx28_duart_device)

extern const struct mxs_auart_data mx28_auart_data[] __initconst;
#define mx28_add_auart(id)	mxs_add_auart(&mx28_auart_data[id])
#define mx28_add_auart0()		mx28_add_auart(0)
#define mx28_add_auart1()		mx28_add_auart(1)
#define mx28_add_auart2()		mx28_add_auart(2)
#define mx28_add_auart3()		mx28_add_auart(3)
#define mx28_add_auart4()		mx28_add_auart(4)

extern const struct mxs_fec_data mx28_fec_data[] __initconst;
#define mx28_add_fec(id, pdata) \
	mxs_add_fec(&mx28_fec_data[id], pdata)

extern const struct mxs_flexcan_data mx28_flexcan_data[] __initconst;
#define mx28_add_flexcan(id, pdata)	\
	mxs_add_flexcan(&mx28_flexcan_data[id], pdata)
#define mx28_add_flexcan0(pdata)	mx28_add_flexcan(0, pdata)
#define mx28_add_flexcan1(pdata)	mx28_add_flexcan(1, pdata)

extern const struct mxs_i2c_data mx28_mxs_i2c_data[] __initconst;
#define mx28_add_mxs_i2c(id)		mxs_add_mxs_i2c(&mx28_mxs_i2c_data[id])

extern const struct mxs_mxs_mmc_data mx28_mxs_mmc_data[] __initconst;
#define mx28_add_mxs_mmc(id, pdata) \
	mxs_add_mxs_mmc(&mx28_mxs_mmc_data[id], pdata)

#define mx28_add_mxs_pwm(id)		mxs_add_mxs_pwm(MX28_PWM_BASE_ADDR, id)

struct platform_device *__init mx28_add_mxsfb(
		const struct mxsfb_platform_data *pdata);
