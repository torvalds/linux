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
