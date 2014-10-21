/*
 * Abilis Systems TB10x platform initialisation
 *
 * Copyright (C) Abilis Systems 2012
 *
 * Author: Christian Ruppert <christian.ruppert@abilis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <asm/mach_desc.h>

static const char *tb10x_compat[] __initdata = {
	"abilis,arc-tb10x",
	NULL,
};

MACHINE_START(TB10x, "tb10x")
	.dt_compat	= tb10x_compat,
MACHINE_END
