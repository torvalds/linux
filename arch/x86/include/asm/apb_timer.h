/*
 * apb_timer.h: Driver for Langwell APB timer based on Synopsis DesignWare
 *
 * (C) Copyright 2009 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * Note:
 */

#ifndef ASM_X86_APBT_H
#define ASM_X86_APBT_H
#include <linux/sfi.h>

#ifdef CONFIG_APB_TIMER

/* default memory mapped register base */
#define LNW_SCU_ADDR           0xFF100000
#define LNW_EXT_TIMER_OFFSET   0x1B800
#define APBT_DEFAULT_BASE      (LNW_SCU_ADDR+LNW_EXT_TIMER_OFFSET)
#define LNW_EXT_TIMER_PGOFFSET         0x800

/* APBT clock speed range from PCLK to fabric base, 25-100MHz */
#define APBT_MAX_FREQ          50000000
#define APBT_MIN_FREQ          1000000
#define APBT_MMAP_SIZE         1024

#define APBT_DEV_USED  1

extern void apbt_time_init(void);
extern unsigned long apbt_quick_calibrate(void);
extern int arch_setup_apbt_irqs(int irq, int trigger, int mask, int cpu);
extern void apbt_setup_secondary_clock(void);

extern struct sfi_timer_table_entry *sfi_get_mtmr(int hint);
extern void sfi_free_mtmr(struct sfi_timer_table_entry *mtmr);
extern int sfi_mtimer_num;

#else /* CONFIG_APB_TIMER */

static inline unsigned long apbt_quick_calibrate(void) {return 0; }
static inline void apbt_time_init(void) { }

#endif
#endif /* ASM_X86_APBT_H */
