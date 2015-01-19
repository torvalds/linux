/*
 * arch/arm/plat-meson/include/plat/sram.h
 *
 * Copyright (C) 2011-2012 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __PLAT_MESON_SRAM_H
#define __PLAT_MESON_SRAM_H

#define SRAM_SIZE			(127 * 1024 + 512)
#define SRAM_GRANULARITY		(512)

#define MESON_SRAM_REBOOT_MODE		(0xC9000000 + SRAM_SIZE + SRAM_GRANULARITY - 4)

/*
 * SRAM allocations return a CPU virtual address, or NULL on error.
 */
extern void *sram_alloc(size_t len);
extern void sram_free(void *addr, size_t len);

#endif /* __PLAT_MESON_SRAM_H */
