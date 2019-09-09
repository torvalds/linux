/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *	Tomasz Figa <t.figa@samsung.com>
 * Copyright (c) 2004 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Written by Ben Dooks, <ben@simtec.co.uk>
 */

#ifndef __PLAT_SAMSUNG_PM_COMMON_H
#define __PLAT_SAMSUNG_PM_COMMON_H __FILE__

#include <linux/irq.h>

/* sleep save info */

/**
 * struct sleep_save - save information for shared peripherals.
 * @reg: Pointer to the register to save.
 * @val: Holder for the value saved from reg.
 *
 * This describes a list of registers which is used by the pm core and
 * other subsystem to save and restore register values over suspend.
 */
struct sleep_save {
	void __iomem	*reg;
	unsigned long	val;
};

#define SAVE_ITEM(x) \
	{ .reg = (x) }

/* helper functions to save/restore lists of registers. */

extern void s3c_pm_do_save(struct sleep_save *ptr, int count);
extern void s3c_pm_do_restore(const struct sleep_save *ptr, int count);
extern void s3c_pm_do_restore_core(const struct sleep_save *ptr, int count);

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

/* suspend memory checking */

#ifdef CONFIG_SAMSUNG_PM_CHECK
extern void s3c_pm_check_prepare(void);
extern void s3c_pm_check_restore(void);
extern void s3c_pm_check_cleanup(void);
extern void s3c_pm_check_store(void);
#else
#define s3c_pm_check_prepare() do { } while (0)
#define s3c_pm_check_restore() do { } while (0)
#define s3c_pm_check_cleanup() do { } while (0)
#define s3c_pm_check_store()   do { } while (0)
#endif

#endif
