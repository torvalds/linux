/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef __MACH_TEGRA_IRAMMAP_H
#define __MACH_TEGRA_IRAMMAP_H

#include <asm/sizes.h>

/* The first 1K of IRAM is permanently reserved for the CPU reset handler */
#define TEGRA_IRAM_RESET_HANDLER_OFFSET	0
#define TEGRA_IRAM_RESET_HANDLER_SIZE	SZ_1K

#endif
