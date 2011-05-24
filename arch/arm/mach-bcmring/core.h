/*
 *  linux/arch/arm/mach-versatile/core.h
 *
 *  Copyright (C) 2004 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/* Portions copyright Broadcom 2008 */
#ifndef __ASM_ARCH_BCMRING_H
#define __ASM_ARCH_BCMRING_H

void __init bcmring_amba_init(void);
void __init bcmring_map_io(void);
void __init bcmring_init_irq(void);
void __init bcmring_init_early(void);

extern struct sys_timer bcmring_timer;
#endif
