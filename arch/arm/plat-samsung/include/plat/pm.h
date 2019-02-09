/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2004 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Written by Ben Dooks, <ben@simtec.co.uk>
 */

/* s3c_pm_init
 *
 * called from board at initialisation time to setup the power
 * management
*/

#include <plat/pm-common.h>

struct device;

#ifdef CONFIG_SAMSUNG_PM

extern __init int s3c_pm_init(void);
extern __init int s3c64xx_pm_init(void);

#else

static inline int s3c_pm_init(void)
{
	return 0;
}

static inline int s3c64xx_pm_init(void)
{
	return 0;
}
#endif

/* configuration for the IRQ mask over sleep */
extern unsigned long s3c_irqwake_intmask;
extern unsigned long s3c_irqwake_eintmask;

/* per-cpu sleep functions */

extern void (*pm_cpu_prep)(void);
extern int (*pm_cpu_sleep)(unsigned long);

/* Flags for PM Control */

extern unsigned long s3c_pm_flags;

/* from sleep.S */

extern int s3c2410_cpu_suspend(unsigned long);

#ifdef CONFIG_PM_SLEEP
extern int s3c_irq_wake(struct irq_data *data, unsigned int state);
extern void s3c_cpu_resume(void);
#else
#define s3c_irq_wake NULL
#define s3c_cpu_resume NULL
#endif

#ifdef CONFIG_SAMSUNG_PM
extern int s3c_irqext_wake(struct irq_data *data, unsigned int state);
#else
#define s3c_irqext_wake NULL
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

/**
 * s3c_pm_configure_extint() - ensure pins are correctly set for IRQ
 *
 * Setup all the necessary GPIO pins for waking the system on external
 * interrupt.
 */
extern void s3c_pm_configure_extint(void);

#ifdef CONFIG_GPIO_SAMSUNG
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
#else
static inline void samsung_pm_restore_gpios(void) {}
static inline void samsung_pm_save_gpios(void) {}
#endif

extern void s3c_pm_save_core(void);
extern void s3c_pm_restore_core(void);
