/*
 * linux/arch/arm/mach-pxa/pxa3xx.c
 *
 * code specific to pxa3xx aka Monahans
 *
 * Copyright (C) 2006 Marvell International Ltd.
 *
 * 2007-09-02: eric miao <eric.miao@marvell.com>
 *             initial version
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio-pxa.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/syscore_ops.h>
#include <linux/i2c/pxa-i2c.h>

#include <asm/mach/map.h>
#include <asm/suspend.h>
#include <mach/hardware.h>
#include <mach/pxa3xx-regs.h>
#include <mach/reset.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include "pm.h"
#include <mach/dma.h>
#include <mach/smemc.h>
#include <mach/irqs.h>

#include "generic.h"
#include "devices.h"

#define PECR_IE(n)	((1 << ((n) * 2)) << 28)
#define PECR_IS(n)	((1 << ((n) * 2)) << 29)

extern void __init pxa_dt_irq_init(int (*fn)(struct irq_data *, unsigned int));

/*
 * NAND NFC: DFI bus arbitration subset
 */
#define NDCR			(*(volatile u32 __iomem*)(NAND_VIRT + 0))
#define NDCR_ND_ARB_EN		(1 << 12)
#define NDCR_ND_ARB_CNTL	(1 << 19)

#ifdef CONFIG_PM

#define ISRAM_START	0x5c000000
#define ISRAM_SIZE	SZ_256K

static void __iomem *sram;
static unsigned long wakeup_src;

/*
 * Enter a standby mode (S0D1C2 or S0D2C2).  Upon wakeup, the dynamic
 * memory controller has to be reinitialised, so we place some code
 * in the SRAM to perform this function.
 *
 * We disable FIQs across the standby - otherwise, we might receive a
 * FIQ while the SDRAM is unavailable.
 */
static void pxa3xx_cpu_standby(unsigned int pwrmode)
{
	void (*fn)(unsigned int) = (void __force *)(sram + 0x8000);

	memcpy_toio(sram + 0x8000, pm_enter_standby_start,
		    pm_enter_standby_end - pm_enter_standby_start);

	AD2D0SR = ~0;
	AD2D1SR = ~0;
	AD2D0ER = wakeup_src;
	AD2D1ER = 0;
	ASCR = ASCR;
	ARSR = ARSR;

	local_fiq_disable();
	fn(pwrmode);
	local_fiq_enable();

	AD2D0ER = 0;
	AD2D1ER = 0;
}

/*
 * NOTE:  currently, the OBM (OEM Boot Module) binary comes along with
 * PXA3xx development kits assumes that the resuming process continues
 * with the address stored within the first 4 bytes of SDRAM. The PSPR
 * register is used privately by BootROM and OBM, and _must_ be set to
 * 0x5c014000 for the moment.
 */
static void pxa3xx_cpu_pm_suspend(void)
{
	volatile unsigned long *p = (volatile void *)0xc0000000;
	unsigned long saved_data = *p;
#ifndef CONFIG_IWMMXT
	u64 acc0;

	asm volatile(".arch_extension xscale\n\t"
		     "mra %Q0, %R0, acc0" : "=r" (acc0));
#endif

	/* resuming from D2 requires the HSIO2/BOOT/TPM clocks enabled */
	CKENA |= (1 << CKEN_BOOT) | (1 << CKEN_TPM);
	CKENB |= 1 << (CKEN_HSIO2 & 0x1f);

	/* clear and setup wakeup source */
	AD3SR = ~0;
	AD3ER = wakeup_src;
	ASCR = ASCR;
	ARSR = ARSR;

	PCFR |= (1u << 13);			/* L1_DIS */
	PCFR &= ~((1u << 12) | (1u << 1));	/* L0_EN | SL_ROD */

	PSPR = 0x5c014000;

	/* overwrite with the resume address */
	*p = virt_to_phys(cpu_resume);

	cpu_suspend(0, pxa3xx_finish_suspend);

	*p = saved_data;

	AD3ER = 0;

#ifndef CONFIG_IWMMXT
	asm volatile(".arch_extension xscale\n\t"
		     "mar acc0, %Q0, %R0" : "=r" (acc0));
#endif
}

static void pxa3xx_cpu_pm_enter(suspend_state_t state)
{
	/*
	 * Don't sleep if no wakeup sources are defined
	 */
	if (wakeup_src == 0) {
		printk(KERN_ERR "Not suspending: no wakeup sources\n");
		return;
	}

	switch (state) {
	case PM_SUSPEND_STANDBY:
		pxa3xx_cpu_standby(PXA3xx_PM_S0D2C2);
		break;

	case PM_SUSPEND_MEM:
		pxa3xx_cpu_pm_suspend();
		break;
	}
}

static int pxa3xx_cpu_pm_valid(suspend_state_t state)
{
	return state == PM_SUSPEND_MEM || state == PM_SUSPEND_STANDBY;
}

static struct pxa_cpu_pm_fns pxa3xx_cpu_pm_fns = {
	.valid		= pxa3xx_cpu_pm_valid,
	.enter		= pxa3xx_cpu_pm_enter,
};

static void __init pxa3xx_init_pm(void)
{
	sram = ioremap(ISRAM_START, ISRAM_SIZE);
	if (!sram) {
		printk(KERN_ERR "Unable to map ISRAM: disabling standby/suspend\n");
		return;
	}

	/*
	 * Since we copy wakeup code into the SRAM, we need to ensure
	 * that it is preserved over the low power modes.  Note: bit 8
	 * is undocumented in the developer manual, but must be set.
	 */
	AD1R |= ADXR_L2 | ADXR_R0;
	AD2R |= ADXR_L2 | ADXR_R0;
	AD3R |= ADXR_L2 | ADXR_R0;

	/*
	 * Clear the resume enable registers.
	 */
	AD1D0ER = 0;
	AD2D0ER = 0;
	AD2D1ER = 0;
	AD3ER = 0;

	pxa_cpu_pm_fns = &pxa3xx_cpu_pm_fns;
}

static int pxa3xx_set_wake(struct irq_data *d, unsigned int on)
{
	unsigned long flags, mask = 0;

	switch (d->irq) {
	case IRQ_SSP3:
		mask = ADXER_MFP_WSSP3;
		break;
	case IRQ_MSL:
		mask = ADXER_WMSL0;
		break;
	case IRQ_USBH2:
	case IRQ_USBH1:
		mask = ADXER_WUSBH;
		break;
	case IRQ_KEYPAD:
		mask = ADXER_WKP;
		break;
	case IRQ_AC97:
		mask = ADXER_MFP_WAC97;
		break;
	case IRQ_USIM:
		mask = ADXER_WUSIM0;
		break;
	case IRQ_SSP2:
		mask = ADXER_MFP_WSSP2;
		break;
	case IRQ_I2C:
		mask = ADXER_MFP_WI2C;
		break;
	case IRQ_STUART:
		mask = ADXER_MFP_WUART3;
		break;
	case IRQ_BTUART:
		mask = ADXER_MFP_WUART2;
		break;
	case IRQ_FFUART:
		mask = ADXER_MFP_WUART1;
		break;
	case IRQ_MMC:
		mask = ADXER_MFP_WMMC1;
		break;
	case IRQ_SSP:
		mask = ADXER_MFP_WSSP1;
		break;
	case IRQ_RTCAlrm:
		mask = ADXER_WRTC;
		break;
	case IRQ_SSP4:
		mask = ADXER_MFP_WSSP4;
		break;
	case IRQ_TSI:
		mask = ADXER_WTSI;
		break;
	case IRQ_USIM2:
		mask = ADXER_WUSIM1;
		break;
	case IRQ_MMC2:
		mask = ADXER_MFP_WMMC2;
		break;
	case IRQ_NAND:
		mask = ADXER_MFP_WFLASH;
		break;
	case IRQ_USB2:
		mask = ADXER_WUSB2;
		break;
	case IRQ_WAKEUP0:
		mask = ADXER_WEXTWAKE0;
		break;
	case IRQ_WAKEUP1:
		mask = ADXER_WEXTWAKE1;
		break;
	case IRQ_MMC3:
		mask = ADXER_MFP_GEN12;
		break;
	default:
		return -EINVAL;
	}

	local_irq_save(flags);
	if (on)
		wakeup_src |= mask;
	else
		wakeup_src &= ~mask;
	local_irq_restore(flags);

	return 0;
}
#else
static inline void pxa3xx_init_pm(void) {}
#define pxa3xx_set_wake	NULL
#endif

static void pxa_ack_ext_wakeup(struct irq_data *d)
{
	PECR |= PECR_IS(d->irq - IRQ_WAKEUP0);
}

static void pxa_mask_ext_wakeup(struct irq_data *d)
{
	pxa_mask_irq(d);
	PECR &= ~PECR_IE(d->irq - IRQ_WAKEUP0);
}

static void pxa_unmask_ext_wakeup(struct irq_data *d)
{
	pxa_unmask_irq(d);
	PECR |= PECR_IE(d->irq - IRQ_WAKEUP0);
}

static int pxa_set_ext_wakeup_type(struct irq_data *d, unsigned int flow_type)
{
	if (flow_type & IRQ_TYPE_EDGE_RISING)
		PWER |= 1 << (d->irq - IRQ_WAKEUP0);

	if (flow_type & IRQ_TYPE_EDGE_FALLING)
		PWER |= 1 << (d->irq - IRQ_WAKEUP0 + 2);

	return 0;
}

static struct irq_chip pxa_ext_wakeup_chip = {
	.name		= "WAKEUP",
	.irq_ack	= pxa_ack_ext_wakeup,
	.irq_mask	= pxa_mask_ext_wakeup,
	.irq_unmask	= pxa_unmask_ext_wakeup,
	.irq_set_type	= pxa_set_ext_wakeup_type,
};

static void __init pxa_init_ext_wakeup_irq(int (*fn)(struct irq_data *,
					   unsigned int))
{
	int irq;

	for (irq = IRQ_WAKEUP0; irq <= IRQ_WAKEUP1; irq++) {
		irq_set_chip_and_handler(irq, &pxa_ext_wakeup_chip,
					 handle_edge_irq);
		irq_clear_status_flags(irq, IRQ_NOREQUEST);
	}

	pxa_ext_wakeup_chip.irq_set_wake = fn;
}

static void __init __pxa3xx_init_irq(void)
{
	/* enable CP6 access */
	u32 value;
	__asm__ __volatile__("mrc p15, 0, %0, c15, c1, 0\n": "=r"(value));
	value |= (1 << 6);
	__asm__ __volatile__("mcr p15, 0, %0, c15, c1, 0\n": :"r"(value));

	pxa_init_ext_wakeup_irq(pxa3xx_set_wake);
}

void __init pxa3xx_init_irq(void)
{
	__pxa3xx_init_irq();
	pxa_init_irq(56, pxa3xx_set_wake);
}

#ifdef CONFIG_OF
void __init pxa3xx_dt_init_irq(void)
{
	__pxa3xx_init_irq();
	pxa_dt_irq_init(pxa3xx_set_wake);
}
#endif	/* CONFIG_OF */

static struct map_desc pxa3xx_io_desc[] __initdata = {
	{	/* Mem Ctl */
		.virtual	= (unsigned long)SMEMC_VIRT,
		.pfn		= __phys_to_pfn(PXA3XX_SMEMC_BASE),
		.length		= SMEMC_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= (unsigned long)NAND_VIRT,
		.pfn		= __phys_to_pfn(NAND_PHYS),
		.length		= NAND_SIZE,
		.type		= MT_DEVICE
	},
};

void __init pxa3xx_map_io(void)
{
	pxa_map_io();
	iotable_init(ARRAY_AND_SIZE(pxa3xx_io_desc));
	pxa3xx_get_clk_frequency_khz(1);
}

/*
 * device registration specific to PXA3xx.
 */

void __init pxa3xx_set_i2c_power_info(struct i2c_pxa_platform_data *info)
{
	pxa_register_device(&pxa3xx_device_i2c_power, info);
}

static struct pxa_gpio_platform_data pxa3xx_gpio_pdata = {
	.irq_base	= PXA_GPIO_TO_IRQ(0),
};

static struct platform_device *devices[] __initdata = {
	&pxa27x_device_udc,
	&pxa_device_pmu,
	&pxa_device_i2s,
	&pxa_device_asoc_ssp1,
	&pxa_device_asoc_ssp2,
	&pxa_device_asoc_ssp3,
	&pxa_device_asoc_ssp4,
	&pxa_device_asoc_platform,
	&pxa_device_rtc,
	&pxa3xx_device_ssp1,
	&pxa3xx_device_ssp2,
	&pxa3xx_device_ssp3,
	&pxa3xx_device_ssp4,
	&pxa27x_device_pwm0,
	&pxa27x_device_pwm1,
};

static int __init pxa3xx_init(void)
{
	int ret = 0;

	if (cpu_is_pxa3xx()) {

		reset_status = ARSR;

		/*
		 * clear RDH bit every time after reset
		 *
		 * Note: the last 3 bits DxS are write-1-to-clear so carefully
		 * preserve them here in case they will be referenced later
		 */
		ASCR &= ~(ASCR_RDH | ASCR_D1S | ASCR_D2S | ASCR_D3S);

		/*
		 * Disable DFI bus arbitration, to prevent a system bus lock if
		 * somebody disables the NAND clock (unused clock) while this
		 * bit remains set.
		 */
		NDCR = (NDCR & ~NDCR_ND_ARB_EN) | NDCR_ND_ARB_CNTL;

		if ((ret = pxa_init_dma(IRQ_DMA, 32)))
			return ret;

		pxa3xx_init_pm();

		register_syscore_ops(&pxa_irq_syscore_ops);
		register_syscore_ops(&pxa3xx_mfp_syscore_ops);

		if (of_have_populated_dt())
			return 0;

		pxa2xx_set_dmac_info(32, 100);
		ret = platform_add_devices(devices, ARRAY_SIZE(devices));
		if (ret)
			return ret;
		if (cpu_is_pxa300() || cpu_is_pxa310() || cpu_is_pxa320()) {
			platform_device_add_data(&pxa3xx_device_gpio,
						 &pxa3xx_gpio_pdata,
						 sizeof(pxa3xx_gpio_pdata));
			ret = platform_device_register(&pxa3xx_device_gpio);
		}
	}

	return ret;
}

postcore_initcall(pxa3xx_init);
