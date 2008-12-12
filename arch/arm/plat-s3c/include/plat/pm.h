/* linux/include/asm-arm/plat-s3c24xx/pm.h
 *
 * Copyright (c) 2004 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Written by Ben Dooks, <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* s3c2410_pm_init
 *
 * called from board at initialisation time to setup the power
 * management
*/

#ifdef CONFIG_PM

extern __init int s3c2410_pm_init(void);

#else

static inline int s3c2410_pm_init(void)
{
	return 0;
}
#endif

/* configuration for the IRQ mask over sleep */
extern unsigned long s3c_irqwake_intmask;
extern unsigned long s3c_irqwake_eintmask;

/* IRQ masks for IRQs allowed to go to sleep (see irq.c) */
extern unsigned long s3c_irqwake_intallow;
extern unsigned long s3c_irqwake_eintallow;

/* per-cpu sleep functions */

extern void (*pm_cpu_prep)(void);
extern void (*pm_cpu_sleep)(void);

/* Flags for PM Control */

extern unsigned long s3c_pm_flags;

/* from sleep.S */

extern int  s3c2410_cpu_save(unsigned long *saveblk);
extern void s3c2410_cpu_suspend(void);
extern void s3c2410_cpu_resume(void);

extern unsigned long s3c_sleep_save_phys;

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
extern void s3c_pm_do_restore(struct sleep_save *ptr, int count);
extern void s3c_pm_do_restore_core(struct sleep_save *ptr, int count);

#ifdef CONFIG_PM
extern int s3c24xx_irq_suspend(struct sys_device *dev, pm_message_t state);
extern int s3c24xx_irq_resume(struct sys_device *dev);
#else
#define s3c24xx_irq_suspend NULL
#define s3c24xx_irq_resume  NULL
#endif

/* PM debug functions */

#ifdef CONFIG_S3C2410_PM_DEBUG
/**
 * s3c_pm_dbg() - low level debug function for use in suspend/resume.
 * @msg: The message to print.
 *
 * This function is used mainly to debug the resume process before the system
 * can rely on printk/console output. It uses the low-level debugging output
 * routine printascii() to do its work.
 */
extern void s3c_pm_dbg(const char *msg, ...);

#define S3C_PMDBG(fmt...) s3c_pm_dbg(fmt)
#else
#define S3C_PMDBG(fmt...) printk(KERN_DEBUG fmt)
#endif

/* suspend memory checking */

#ifdef CONFIG_S3C2410_PM_CHECK
extern void s3c_pm_check_prepare(void);
extern void s3c_pm_check_restore(void);
extern void s3c_pm_check_store(void);
#else
#define s3c_pm_check_prepare() do { } while(0)
#define s3c_pm_check_restore() do { } while(0)
#define s3c_pm_check_store()   do { } while(0)
#endif
