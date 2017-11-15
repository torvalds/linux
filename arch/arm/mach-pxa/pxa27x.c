/*
 *  linux/arch/arm/mach-pxa/pxa27x.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Nov 05, 2002
 *  Copyright:	MontaVista Software Inc.
 *
 * Code specific to PXA27x aka Bulverde.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/gpio-pxa.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/i2c/pxa-i2c.h>

#include <asm/mach/map.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/suspend.h>
#include <mach/irqs.h>
#include <mach/pxa27x.h>
#include <mach/reset.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include <mach/pm.h>
#include <mach/dma.h>
#include <mach/smemc.h>

#include "generic.h"
#include "devices.h"
#include <linux/clk-provider.h>
#include <linux/clkdev.h>

void pxa27x_clear_otgph(void)
{
	if (cpu_is_pxa27x() && (PSSR & PSSR_OTGPH))
		PSSR |= PSSR_OTGPH;
}
EXPORT_SYMBOL(pxa27x_clear_otgph);

static unsigned long ac97_reset_config[] = {
	GPIO113_AC97_nRESET_GPIO_HIGH,
	GPIO113_AC97_nRESET,
	GPIO95_AC97_nRESET_GPIO_HIGH,
	GPIO95_AC97_nRESET,
};

void pxa27x_configure_ac97reset(int reset_gpio, bool to_gpio)
{
	/*
	 * This helper function is used to work around a bug in the pxa27x's
	 * ac97 controller during a warm reset.  The configuration of the
	 * reset_gpio is changed as follows:
	 * to_gpio == true: configured to generic output gpio and driven high
	 * to_gpio == false: configured to ac97 controller alt fn AC97_nRESET
	 */

	if (reset_gpio == 113)
		pxa2xx_mfp_config(to_gpio ? &ac97_reset_config[0] :
				  &ac97_reset_config[1], 1);

	if (reset_gpio == 95)
		pxa2xx_mfp_config(to_gpio ? &ac97_reset_config[2] :
				  &ac97_reset_config[3], 1);
}
EXPORT_SYMBOL_GPL(pxa27x_configure_ac97reset);

#ifdef CONFIG_PM

#define SAVE(x)		sleep_save[SLEEP_SAVE_##x] = x
#define RESTORE(x)	x = sleep_save[SLEEP_SAVE_##x]

/*
 * allow platforms to override default PWRMODE setting used for PM_SUSPEND_MEM
 */
static unsigned int pwrmode = PWRMODE_SLEEP;

int pxa27x_set_pwrmode(unsigned int mode)
{
	switch (mode) {
	case PWRMODE_SLEEP:
	case PWRMODE_DEEPSLEEP:
		pwrmode = mode;
		return 0;
	}

	return -EINVAL;
}

/*
 * List of global PXA peripheral registers to preserve.
 * More ones like CP and general purpose register values are preserved
 * with the stack pointer in sleep.S.
 */
enum {
	SLEEP_SAVE_PSTR,
	SLEEP_SAVE_MDREFR,
	SLEEP_SAVE_PCFR,
	SLEEP_SAVE_COUNT
};

void pxa27x_cpu_pm_save(unsigned long *sleep_save)
{
	sleep_save[SLEEP_SAVE_MDREFR] = __raw_readl(MDREFR);
	SAVE(PCFR);

	SAVE(PSTR);
}

void pxa27x_cpu_pm_restore(unsigned long *sleep_save)
{
	__raw_writel(sleep_save[SLEEP_SAVE_MDREFR], MDREFR);
	RESTORE(PCFR);

	PSSR = PSSR_RDH | PSSR_PH;

	RESTORE(PSTR);
}

void pxa27x_cpu_pm_enter(suspend_state_t state)
{
	extern void pxa_cpu_standby(void);
#ifndef CONFIG_IWMMXT
	u64 acc0;

	asm volatile("mra %Q0, %R0, acc0" : "=r" (acc0));
#endif

	/* ensure voltage-change sequencer not initiated, which hangs */
	PCFR &= ~PCFR_FVC;

	/* Clear edge-detect status register. */
	PEDR = 0xDF12FE1B;

	/* Clear reset status */
	RCSR = RCSR_HWR | RCSR_WDR | RCSR_SMR | RCSR_GPR;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		pxa_cpu_standby();
		break;
	case PM_SUSPEND_MEM:
		cpu_suspend(pwrmode, pxa27x_finish_suspend);
#ifndef CONFIG_IWMMXT
		asm volatile("mar acc0, %Q0, %R0" : "=r" (acc0));
#endif
		break;
	}
}

static int pxa27x_cpu_pm_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM || state == PM_SUSPEND_STANDBY;
}

static int pxa27x_cpu_pm_prepare(void)
{
	/* set resume return address */
	PSPR = virt_to_phys(cpu_resume);
	return 0;
}

static void pxa27x_cpu_pm_finish(void)
{
	/* ensure not to come back here if it wasn't intended */
	PSPR = 0;
}

static struct pxa_cpu_pm_fns pxa27x_cpu_pm_fns = {
	.save_count	= SLEEP_SAVE_COUNT,
	.save		= pxa27x_cpu_pm_save,
	.restore	= pxa27x_cpu_pm_restore,
	.valid		= pxa27x_cpu_pm_valid,
	.enter		= pxa27x_cpu_pm_enter,
	.prepare	= pxa27x_cpu_pm_prepare,
	.finish		= pxa27x_cpu_pm_finish,
};

static void __init pxa27x_init_pm(void)
{
	pxa_cpu_pm_fns = &pxa27x_cpu_pm_fns;
}
#else
static inline void pxa27x_init_pm(void) {}
#endif

/* PXA27x:  Various gpios can issue wakeup events.  This logic only
 * handles the simple cases, not the WEMUX2 and WEMUX3 options
 */
static int pxa27x_set_wake(struct irq_data *d, unsigned int on)
{
	int gpio = pxa_irq_to_gpio(d->irq);
	uint32_t mask;

	if (gpio >= 0 && gpio < 128)
		return gpio_set_wake(gpio, on);

	if (d->irq == IRQ_KEYPAD)
		return keypad_set_wake(on);

	switch (d->irq) {
	case IRQ_RTCAlrm:
		mask = PWER_RTC;
		break;
	case IRQ_USB:
		mask = 1u << 26;
		break;
	default:
		return -EINVAL;
	}

	if (on)
		PWER |= mask;
	else
		PWER &=~mask;

	return 0;
}

void __init pxa27x_init_irq(void)
{
	pxa_init_irq(34, pxa27x_set_wake);
}

void __init pxa27x_dt_init_irq(void)
{
	if (IS_ENABLED(CONFIG_OF))
		pxa_dt_irq_init(pxa27x_set_wake);
}

static struct map_desc pxa27x_io_desc[] __initdata = {
	{	/* Mem Ctl */
		.virtual	= (unsigned long)SMEMC_VIRT,
		.pfn		= __phys_to_pfn(PXA2XX_SMEMC_BASE),
		.length		= SMEMC_SIZE,
		.type		= MT_DEVICE
	}, {	/* UNCACHED_PHYS_0 */
		.virtual	= UNCACHED_PHYS_0,
		.pfn		= __phys_to_pfn(0x00000000),
		.length		= UNCACHED_PHYS_0_SIZE,
		.type		= MT_DEVICE
	},
};

void __init pxa27x_map_io(void)
{
	pxa_map_io();
	iotable_init(ARRAY_AND_SIZE(pxa27x_io_desc));
	pxa27x_get_clk_frequency_khz(1);
}

/*
 * device registration specific to PXA27x.
 */
void __init pxa27x_set_i2c_power_info(struct i2c_pxa_platform_data *info)
{
	local_irq_disable();
	PCFR |= PCFR_PI2CEN;
	local_irq_enable();
	pxa_register_device(&pxa27x_device_i2c_power, info);
}

static struct pxa_gpio_platform_data pxa27x_gpio_info __initdata = {
	.irq_base	= PXA_GPIO_TO_IRQ(0),
	.gpio_set_wake	= gpio_set_wake,
};

static struct platform_device *devices[] __initdata = {
	&pxa27x_device_udc,
	&pxa_device_pmu,
	&pxa_device_i2s,
	&pxa_device_asoc_ssp1,
	&pxa_device_asoc_ssp2,
	&pxa_device_asoc_ssp3,
	&pxa_device_asoc_platform,
	&pxa_device_rtc,
	&pxa27x_device_ssp1,
	&pxa27x_device_ssp2,
	&pxa27x_device_ssp3,
	&pxa27x_device_pwm0,
	&pxa27x_device_pwm1,
};

static int __init pxa27x_init(void)
{
	int ret = 0;

	if (cpu_is_pxa27x()) {

		reset_status = RCSR;

		if ((ret = pxa_init_dma(IRQ_DMA, 32)))
			return ret;

		pxa27x_init_pm();

		register_syscore_ops(&pxa_irq_syscore_ops);
		register_syscore_ops(&pxa2xx_mfp_syscore_ops);

		if (!of_have_populated_dt()) {
			pxa_register_device(&pxa27x_device_gpio,
					    &pxa27x_gpio_info);
			pxa2xx_set_dmac_info(32, 75);
			ret = platform_add_devices(devices,
						   ARRAY_SIZE(devices));
		}
	}

	return ret;
}

postcore_initcall(pxa27x_init);
