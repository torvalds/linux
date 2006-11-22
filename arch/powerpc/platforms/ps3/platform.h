/*
 *  PS3 platform declarations.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined(_PS3_PLATFORM_H)
#define _PS3_PLATFORM_H

#include <linux/rtc.h>

/* htab */

void __init ps3_hpte_init(unsigned long htab_size);
void __init ps3_map_htab(void);

/* mm */

void __init ps3_mm_init(void);
void __init ps3_mm_vas_create(unsigned long* htab_size);
void ps3_mm_vas_destroy(void);
void ps3_mm_shutdown(void);

/* irq */

void ps3_init_IRQ(void);
void __init ps3_register_ipi_debug_brk(unsigned int cpu, unsigned int virq);

/* smp */

void smp_init_ps3(void);
void ps3_smp_cleanup_cpu(int cpu);

/* time */

void __init ps3_calibrate_decr(void);
unsigned long __init ps3_get_boot_time(void);
void ps3_get_rtc_time(struct rtc_time *time);
int ps3_set_rtc_time(struct rtc_time *time);

/* os area */

int __init ps3_os_area_init(void);
u64 ps3_os_area_rtc_diff(void);

/* spu */

#if defined(CONFIG_SPU_BASE)
void ps3_spu_set_platform (void);
#else
static inline void ps3_spu_set_platform (void) {}
#endif

#endif
