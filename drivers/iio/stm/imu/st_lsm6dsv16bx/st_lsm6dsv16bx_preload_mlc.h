/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#ifndef ST_LSM6DSV16BX_PRELOAD_MLC_H
#define ST_LSM6DSV16BX_PRELOAD_MLC_H

static const u8 mlcdata[] = {
	/* put here MLC/FSM configuration */
};

static struct firmware st_lsm6dsv16bx_mlc_preload = {
		.size = sizeof(mlcdata),
		.data = mlcdata
};

#endif /* ST_LSM6DSV16BX_PRELOAD_MLC_H */
