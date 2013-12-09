/*
 * lttng-calibrate.c
 *
 * LTTng probe calibration.
 *
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "lttng-abi.h"
#include "lttng-events.h"

noinline
void lttng_calibrate_kretprobe(void)
{
	asm volatile ("");
}

int lttng_calibrate(struct lttng_kernel_calibrate *calibrate)
{
	switch (calibrate->type) {
	case LTTNG_KERNEL_CALIBRATE_KRETPROBE:
		lttng_calibrate_kretprobe();
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
