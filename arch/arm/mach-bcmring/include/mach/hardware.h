/*
 *
 *  This file contains the hardware definitions of the BCMRing.
 *
 *  Copyright (C) 1999 ARM Limited.
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
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>
#include <mach/memory.h>
#include <cfg_global.h>
#include <mach/csp/mm_io.h>

/* Hardware addresses of major areas.
 *  *_START is the physical address
 *  *_SIZE  is the size of the region
 *  *_BASE  is the virtual address
 */
#define RAM_START               PLAT_PHYS_OFFSET

#define RAM_SIZE                (CFG_GLOBAL_RAM_SIZE-CFG_GLOBAL_RAM_SIZE_RESERVED)
#define RAM_BASE                PAGE_OFFSET

/* Macros to make managing spinlocks a bit more controlled in terms of naming. */
/* See reg_gpio.h, reg_irq.h, arch.c, gpio.c for example usage. */
#if defined(__KERNEL__)
#define HW_DECLARE_SPINLOCK(name)  DEFINE_SPINLOCK(bcmring_##name##_reg_lock);
#define HW_EXTERN_SPINLOCK(name)   extern spinlock_t bcmring_##name##_reg_lock;
#define HW_IRQ_SAVE(name, val)     spin_lock_irqsave(&bcmring_##name##_reg_lock, (val))
#define HW_IRQ_RESTORE(name, val)  spin_unlock_irqrestore(&bcmring_##name##_reg_lock, (val))
#else
#define HW_DECLARE_SPINLOCK(name)
#define HW_EXTERN_SPINLOCK(name)
#define HW_IRQ_SAVE(name, val)     {(void)(name); (void)(val); }
#define HW_IRQ_RESTORE(name, val)  {(void)(name); (void)(val); }
#endif

#ifndef HW_IO_PHYS_TO_VIRT
#define HW_IO_PHYS_TO_VIRT MM_IO_PHYS_TO_VIRT
#endif
#define HW_IO_VIRT_TO_PHYS MM_IO_VIRT_TO_PHYS

#endif
