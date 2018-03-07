/*
 * Host1x init for Tegra124 SoCs
 *
 * Copyright (c) 2013 NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* include hw specification */
#include "host1x04.h"
#include "host1x04_hardware.h"

/* include code */
#define HOST1X_HW 4

#include "cdma_hw.c"
#include "channel_hw.c"
#include "debug_hw.c"
#include "intr_hw.c"
#include "syncpt_hw.c"

#include "../dev.h"

int host1x04_init(struct host1x *host)
{
	host->channel_op = &host1x_channel_ops;
	host->cdma_op = &host1x_cdma_ops;
	host->cdma_pb_op = &host1x_pushbuffer_ops;
	host->syncpt_op = &host1x_syncpt_ops;
	host->intr_op = &host1x_intr_ops;
	host->debug_op = &host1x_debug_ops;

	return 0;
}
