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

/* Langwell DW APB timer registers */
#define APBTMR_N_LOAD_COUNT    0x00
#define APBTMR_N_CURRENT_VALUE 0x04
#define APBTMR_N_CONTROL       0x08
#define APBTMR_N_EOI           0x0c
#define APBTMR_N_INT_STATUS    0x10

#define APBTMRS_INT_STATUS     0xa0
#define APBTMRS_EOI            0xa4
#define APBTMRS_RAW_INT_STATUS 0xa8
#define APBTMRS_COMP_VERSION   0xac
#define APBTMRS_REG_SIZE       0x14

/* register bits */
#define APBTMR_CONTROL_ENABLE  (1<<0)
#define APBTMR_CONTROL_MODE_PERIODIC   (1<<1) /*1: periodic 0:free running */
#define APBTMR_CONTROL_INT     (1<<2)

/* default memory mapped register base */
#define LNW_SCU_ADDR           0xFF100000
#define LNW_EXT_TIMER_OFFSET   0x1B800
#define APBT_DEFAULT_BASE      (LNW_SCU_ADDR+LNW_EXT_TIMER_OFFSET)
#define LNW_EXT_TIMER_PGOFFSET         0x800

/* APBT clock speed range from PCLK to fabric base, 25-100MHz */
#define APBT_MAX_FREQ          50
#define APBT_MIN_FREQ          1
#define APBT_MMAP_SIZE         1024

#define APBT_DEV_USED  1

extern void apbt_time_init(void);
extern struct clock_event_device *global_clock_event;
extern unsigned long apbt_quick_calibrate(void);
extern int arch_setup_apbt_irqs(int irq, int trigger, int mask, int cpu);
extern void apbt_setup_secondary_clock(void);
extern unsigned int boot_cpu_id;
extern int disable_apbt_percpu;

extern struct sfi_timer_table_entry *sfi_get_mtmr(int hint);
extern void sfi_free_mtmr(struct sfi_timer_table_entry *mtmr);
extern int sfi_mtimer_num;

#else /* CONFIG_APB_TIMER */

static inline unsigned long apbt_quick_calibrate(void) {return 0; }
static inline void apbt_time_init(void) {return 0; }

#endif
#endif /* ASM_X86_APBT_H */
