/*
 * Genmai board support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>

#include <asm/mach/arch.h>

#include "common.h"

static const char * const genmai_boards_compat_dt[] __initconst = {
	"renesas,genmai",
	NULL,
};

DT_MACHINE_START(GENMAI_DT, "genmai")
	.init_early	= shmobile_init_delay,
	.dt_compat	= genmai_boards_compat_dt,
MACHINE_END
