/*
 * wdt.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * IO dispatcher for a shared memory channel driver.
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#ifndef __DSP_WDT3_H_
#define __DSP_WDT3_H_

/* WDT defines */
#define OMAP3_WDT3_ISR_OFFSET	0x0018


/**
 * struct dsp_wdt_setting - the basic dsp_wdt_setting structure
 * @reg_base:	pointer to the base of the wdt registers
 * @sm_wdt:	pointer to flags in shared memory
 * @wdt3_tasklet	tasklet to manage wdt event
 * @fclk		handle to wdt3 functional clock
 * @iclk		handle to wdt3 interface clock
 *
 * This struct is used in the function to manage wdt3.
 */

struct dsp_wdt_setting {
	void __iomem *reg_base;
	struct shm *sm_wdt;
	struct tasklet_struct wdt3_tasklet;
	struct clk *fclk;
	struct clk *iclk;
};

/**
 * dsp_wdt_init() - initialize wdt3 module.
 *
 * This function initilize to wdt3 module, so that
 * other wdt3 function can be used.
 */
int dsp_wdt_init(void);

/**
 * dsp_wdt_exit() - initialize wdt3 module.
 *
 * This function frees all resources allocated for wdt3 module.
 */
void dsp_wdt_exit(void);

/**
 * dsp_wdt_enable() - enable/disable wdt3
 * @enable:	bool value to enable/disable wdt3
 *
 * This function enables or disables wdt3 base on @enable value.
 *
 */
void dsp_wdt_enable(bool enable);

/**
 * dsp_wdt_sm_set() - store pointer to the share memory
 * @data:		pointer to dspbridge share memory
 *
 * This function is used to pass a valid pointer to share memory,
 * so that the flags can be set in order DSP side can read them.
 *
 */
void dsp_wdt_sm_set(void *data);

#endif

