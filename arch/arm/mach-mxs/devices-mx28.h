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

extern const struct mxs_duart_data mx28_duart_data __initconst;
#define mx28_add_duart() \
	mxs_add_duart(&mx28_duart_data)

extern const struct mxs_fec_data mx28_fec_data[] __initconst;
#define mx28_add_fec(id, pdata) \
	mxs_add_fec(&mx28_fec_data[id], pdata)
