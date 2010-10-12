/*
 * host_os.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _HOST_OS_H_
#define _HOST_OS_H_

#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/syscalls.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <plat/clock.h>
#include <linux/clk.h>
#include <plat/mailbox.h>
#include <linux/pagemap.h>
#include <asm/cacheflush.h>
#include <linux/dma-mapping.h>

/* TODO -- Remove, once BP defines them */
#define INT_DSP_MMU_IRQ        28

struct dspbridge_platform_data {
	void (*dsp_set_min_opp) (u8 opp_id);
	 u8(*dsp_get_opp) (void);
	void (*cpu_set_freq) (unsigned long f);
	unsigned long (*cpu_get_freq) (void);
	unsigned long mpu_speed[6];

	/* functions to write and read PRCM registers */
	void (*dsp_prm_write)(u32, s16 , u16);
	u32 (*dsp_prm_read)(s16 , u16);
	u32 (*dsp_prm_rmw_bits)(u32, u32, s16, s16);
	void (*dsp_cm_write)(u32, s16 , u16);
	u32 (*dsp_cm_read)(s16 , u16);
	u32 (*dsp_cm_rmw_bits)(u32, u32, s16, s16);

	u32 phys_mempool_base;
	u32 phys_mempool_size;
};

#define PRCM_VDD1 1

extern struct platform_device *omap_dspbridge_dev;
extern struct device *bridge;

#if defined(CONFIG_TIDSPBRIDGE) || defined(CONFIG_TIDSPBRIDGE_MODULE)
extern void dspbridge_reserve_sdram(void);
#else
static inline void dspbridge_reserve_sdram(void)
{
}
#endif

extern unsigned long dspbridge_get_mempool_base(void);
#endif
