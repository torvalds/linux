/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#include "gpiomux.h"

struct msm_gpiomux_config msm_gpiomux_configs[GPIOMUX_NGPIOS] = {
#ifdef CONFIG_SERIAL_MSM_CONSOLE
	[49] = { /* UART2 RFR */
		.suspended = GPIOMUX_DRV_2MA | GPIOMUX_PULL_DOWN |
			     GPIOMUX_FUNC_2 | GPIOMUX_VALID,
	},
	[50] = { /* UART2 CTS */
		.suspended = GPIOMUX_DRV_2MA | GPIOMUX_PULL_DOWN |
			     GPIOMUX_FUNC_2 | GPIOMUX_VALID,
	},
	[51] = { /* UART2 RX */
		.suspended = GPIOMUX_DRV_2MA | GPIOMUX_PULL_DOWN |
			     GPIOMUX_FUNC_2 | GPIOMUX_VALID,
	},
	[52] = { /* UART2 TX */
		.suspended = GPIOMUX_DRV_2MA | GPIOMUX_PULL_DOWN |
			     GPIOMUX_FUNC_2 | GPIOMUX_VALID,
	},
#endif
};
