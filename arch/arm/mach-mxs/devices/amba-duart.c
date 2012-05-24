/*
 * Copyright (C) 2009-2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <asm/irq.h>
#include <mach/mx23.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>

#define MXS_AMBA_DUART_DEVICE(name, soc)			\
const struct amba_device name##_device __initconst = {		\
	.dev = {						\
		.init_name = "duart",				\
	},							\
	.res = {						\
		.start = soc ## _DUART_BASE_ADDR,		\
		.end = (soc ## _DUART_BASE_ADDR) + SZ_8K - 1,	\
		.flags = IORESOURCE_MEM,			\
	},							\
	.irq = {soc ## _INT_DUART},				\
}

#ifdef CONFIG_SOC_IMX23
MXS_AMBA_DUART_DEVICE(mx23_duart, MX23);
#endif

#ifdef CONFIG_SOC_IMX28
MXS_AMBA_DUART_DEVICE(mx28_duart, MX28);
#endif

int __init mxs_add_duart(const struct amba_device *dev)
{
	return mxs_add_amba_device(dev);
}
