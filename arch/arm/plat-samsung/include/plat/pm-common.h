/*
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *	Tomasz Figa <t.figa@samsung.com>
 * Copyright (c) 2004 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Written by Ben Dooks, <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_SAMSUNG_PM_COMMON_H
#define __PLAT_SAMSUNG_PM_COMMON_H __FILE__

/* PM debug functions */

/**
 * struct pm_uart_save - save block for core UART
 * @ulcon: Save value for S3C2410_ULCON
 * @ucon: Save value for S3C2410_UCON
 * @ufcon: Save value for S3C2410_UFCON
 * @umcon: Save value for S3C2410_UMCON
 * @ubrdiv: Save value for S3C2410_UBRDIV
 *
 * Save block for UART registers to be held over sleep and restored if they
 * are needed (say by debug).
*/
struct pm_uart_save {
	u32	ulcon;
	u32	ucon;
	u32	ufcon;
	u32	umcon;
	u32	ubrdiv;
	u32	udivslot;
};

#ifdef CONFIG_SAMSUNG_PM_DEBUG
/**
 * s3c_pm_dbg() - low level debug function for use in suspend/resume.
 * @msg: The message to print.
 *
 * This function is used mainly to debug the resume process before the system
 * can rely on printk/console output. It uses the low-level debugging output
 * routine printascii() to do its work.
 */
extern void s3c_pm_dbg(const char *msg, ...);

/**
 * s3c_pm_debug_init() - suspend/resume low level debug initialization.
 * @base: Virtual base of UART to use for suspend/resume debugging.
 *
 * This function needs to be called before S3C_PMDBG() can be used, to set up
 * UART port base address and configuration.
 */
extern void s3c_pm_debug_init(void);

#define S3C_PMDBG(fmt...) s3c_pm_dbg(fmt)

extern void s3c_pm_save_uarts(void);
extern void s3c_pm_restore_uarts(void);
#else
#define S3C_PMDBG(fmt...) pr_debug(fmt)
#define s3c_pm_debug_init() do { } while (0)

static inline void s3c_pm_save_uarts(void) { }
static inline void s3c_pm_restore_uarts(void) { }
#endif

#endif
