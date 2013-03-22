/*
 * Host1x init for T20 and T30 Architecture Chips
 *
 * Copyright (c) 2011-2013, NVIDIA Corporation.
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
#include "hw/host1x01.h"
#include "hw/host1x01_hardware.h"

/* include code */
#include "hw/syncpt_hw.c"

#include "dev.h"

int host1x01_init(struct host1x *host)
{
	host->syncpt_op = &host1x_syncpt_ops;

	return 0;
}
