/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Rockchip Electronics Co. Ltd.
 *
 * Author: Steven Liu <steven.liu@rock-chips.com>
 */

#ifndef _PINCTRL_API_H_
#define _PINCTRL_API_H_

#include "hal_def.h"
#include "hal_os_def.h"
#include "pinctrl_rkx110_x120.h"

static inline int hwpin_set(struct xferpin xfer)
{
	struct hwpin hw;
	int ret;

	if (!xfer.read || !xfer.write || !xfer.client || !xfer.name[0]) {
		return HAL_ERROR;
	}

	if (xfer.type == PIN_UNDEF || xfer.type >= PIN_MAX) {
		return HAL_INVAL;
	}

	memset(&hw, 0, sizeof(hw));
	hw.type = xfer.type;
	hw.xfer = xfer;
	hw.bank = xfer.bank;
	hw.mpins = xfer.mpins;
	hw.param = xfer.param;

	ret = HAL_PINCTRL_SetParam(&hw, hw.mpins, hw.param);

	return ret;
}

static inline int hwpin_init(void)
{
	return HAL_PINCTRL_Init();
}

#endif
