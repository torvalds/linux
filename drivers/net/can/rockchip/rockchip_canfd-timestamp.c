// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2023, 2024 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include "rockchip_canfd.h"

void rkcanfd_timestamp_init(struct rkcanfd_priv *priv)
{
	u32 reg;

	reg = RKCANFD_REG_TIMESTAMP_CTRL_TIME_BASE_COUNTER_ENABLE;
	rkcanfd_write(priv, RKCANFD_REG_TIMESTAMP_CTRL, reg);
}
