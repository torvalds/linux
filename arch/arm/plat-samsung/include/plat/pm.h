/* arch/arm/plat-samsung/include/plat/pm.h
 *
 * Copyright (c) 2004 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Written by Ben Dooks, <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* s3c_pm_init
 *
 * called from board at initialisation time to setup the power
 * management
*/

#include <linux/irq.h>

struct sys_device;

#ifdef CONFIG_PM

extern __init int s3c_pm_init(void);

#else

static inline int s3c_pm_init(void)
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
extern int (*pm_cpu_sleep)(unsigned long);

/* Flags for PM Control */

extern unsigned long s3c_pm_flags;

extern unsigned char pm_uart_udivslot;  /* true to save UART UDIVSLOT */

/* from sleep.S */

extern void s3c_cpu_resume(void);

extern int s3c2410_cpu_suspend(unsigned long);

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

/* helper functions to save/restore lists of registers. */

extern void s3c_pm_do_save(struct sleep_save *ptr, int count);
extern void s3c_pm_do_restore(struct sleep_save *ptr, int count);
extern void s3c_pm_do_restore_core(struct sleep_save *ptr, int count);

#ifdef CONFIG_PM
extern int s3c_irqext_wake(struct irq_data *data, unsigned int state);
extern int s3c24xx_irq_suspend(void);
extern void s3c24xx_irq_resume(void);
#else
#define s3c_irqext_wake NULL
#define s3c24xx_irq_suspend NULL
#define s3c24xx_irq_resume  NULL
#endif

extern struct syscore_ops s3c24xx_irq_syscore_ops;

/* PM debug functions */

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

#define S3C_PMDBG(fmt...) s3c_pm_dbg(fmt)
#else
#define S3C_PMDBG(fmt...) printk(KERN_DEBUG fmt)
#endif

#ifdef CONFIG_S3C_PM_DEBUG_LED_SMDK
/**
 * s3c_pm_debug_smdkled() - Debug PM suspend/resume via SMDK Board LEDs
 * @set: set bits for the state of the LEDs
 * @clear: clear bits for the state of the LEDs.
 */
extern void s3c_pm_debug_smdkled(u32 set, u32 clear);

#else
static inline void s3c_pm_debug_smdkled(u32 set, u32 clear) { }
#endif /* CONFIG_S3C_PM_DEBUG_LED_SMDK */

/* suspend memory checking */

#ifdef CONFIG_SAMSUNG_PM_CHECK
extern void s3c_pm_check_prepare(void);
extern void s3c_pm_check_restore(void);
extern void s3c_pm_check_cleanup(void);
extern void s3c_pm_check_store(void);
#else
#define s3c_pm_check_prepare() do { } while(0)
#define s3c_pm_check_restore() do { } while(0)
#define s3c_pm_check_cleanup() do { } while(0)
#define s3c_pm_check_store()   do { } while(0)
#endif

/**
 * s3c_pm_configure_extint() - ensure pins are correctly set for IRQ
 *
 * Setup all the necessary GPIO pins for waking the system on external
 * interrupt.
 */
extern void s3c_pm_configure_extint(void);

/**
 * samsung_pm_restore_gpios() - restore the state of the gpios after sleep.
 *
 * Restore the state of the GPIO pins after sleep, which may involve ensuring
 * that we do not glitch the state of the pins from that the bootloader's
 * resume code has done.
*/
extern void samsung_pm_restore_gpios(void);

/**
 * samsung_pm_save_gpios() - save the state of the GPIOs for restoring after sleep.
 *
 * Save the GPIO states for resotration on resume. See samsung_pm_restore_gpios().
 */
extern void samsung_pm_save_gpios(void);

extern void s3c_pm_save_core(void);
extern void s3c_pm_restore_core(void);
