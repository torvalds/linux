/*
 *  linux/arch/arm/mach-integrator/integrator_ap.c
 *
 *  Copyright (C) 2000-2003 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/hardware/amba.h>
#include <asm/hardware/amba_kmi.h>

#include <asm/arch/lm.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include "common.h"

/* 
 * All IO addresses are mapped onto VA 0xFFFx.xxxx, where x.xxxx
 * is the (PA >> 12).
 *
 * Setup a VA for the Integrator interrupt controller (for header #0,
 * just for now).
 */
#define VA_IC_BASE	IO_ADDRESS(INTEGRATOR_IC_BASE) 
#define VA_SC_BASE	IO_ADDRESS(INTEGRATOR_SC_BASE)
#define VA_EBI_BASE	IO_ADDRESS(INTEGRATOR_EBI_BASE)
#define VA_CMIC_BASE	IO_ADDRESS(INTEGRATOR_HDR_BASE) + INTEGRATOR_HDR_IC_OFFSET

/*
 * Logical      Physical
 * e8000000	40000000	PCI memory		PHYS_PCI_MEM_BASE	(max 512M)
 * ec000000	61000000	PCI config space	PHYS_PCI_CONFIG_BASE	(max 16M)
 * ed000000	62000000	PCI V3 regs		PHYS_PCI_V3_BASE	(max 64k)
 * ee000000	60000000	PCI IO			PHYS_PCI_IO_BASE	(max 16M)
 * ef000000			Cache flush
 * f1000000	10000000	Core module registers
 * f1100000	11000000	System controller registers
 * f1200000	12000000	EBI registers
 * f1300000	13000000	Counter/Timer
 * f1400000	14000000	Interrupt controller
 * f1600000	16000000	UART 0
 * f1700000	17000000	UART 1
 * f1a00000	1a000000	Debug LEDs
 * f1b00000	1b000000	GPIO
 */

static struct map_desc ap_io_desc[] __initdata = {
	{
		.virtual	= IO_ADDRESS(INTEGRATOR_HDR_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_HDR_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_SC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_SC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_EBI_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_EBI_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_CT_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CT_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_IC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_IC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_UART0_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_UART1_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_UART1_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_DBG_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_DBG_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_GPIO_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_GPIO_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= PCI_MEMORY_VADDR,
		.pfn		= __phys_to_pfn(PHYS_PCI_MEM_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}, {
		.virtual	= PCI_CONFIG_VADDR,
		.pfn		= __phys_to_pfn(PHYS_PCI_CONFIG_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}, {
		.virtual	= PCI_V3_VADDR,
		.pfn		= __phys_to_pfn(PHYS_PCI_V3_BASE),
		.length		= SZ_64K,
		.type		= MT_DEVICE
	}, {
		.virtual	= PCI_IO_VADDR,
		.pfn		= __phys_to_pfn(PHYS_PCI_IO_BASE),
		.length		= SZ_64K,
		.type		= MT_DEVICE
	}
};

static void __init ap_map_io(void)
{
	iotable_init(ap_io_desc, ARRAY_SIZE(ap_io_desc));
}

#define INTEGRATOR_SC_VALID_INT	0x003fffff

static void sc_mask_irq(unsigned int irq)
{
	writel(1 << irq, VA_IC_BASE + IRQ_ENABLE_CLEAR);
}

static void sc_unmask_irq(unsigned int irq)
{
	writel(1 << irq, VA_IC_BASE + IRQ_ENABLE_SET);
}

static struct irqchip sc_chip = {
	.ack	= sc_mask_irq,
	.mask	= sc_mask_irq,
	.unmask = sc_unmask_irq,
};

static void __init ap_init_irq(void)
{
	unsigned int i;

	/* Disable all interrupts initially. */
	/* Do the core module ones */
	writel(-1, VA_CMIC_BASE + IRQ_ENABLE_CLEAR);

	/* do the header card stuff next */
	writel(-1, VA_IC_BASE + IRQ_ENABLE_CLEAR);
	writel(-1, VA_IC_BASE + FIQ_ENABLE_CLEAR);

	for (i = 0; i < NR_IRQS; i++) {
		if (((1 << i) & INTEGRATOR_SC_VALID_INT) != 0) {
			set_irq_chip(i, &sc_chip);
			set_irq_handler(i, do_level_IRQ);
			set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
		}
	}
}

#ifdef CONFIG_PM
static unsigned long ic_irq_enable;

static int irq_suspend(struct sys_device *dev, pm_message_t state)
{
	ic_irq_enable = readl(VA_IC_BASE + IRQ_ENABLE);
	return 0;
}

static int irq_resume(struct sys_device *dev)
{
	/* disable all irq sources */
	writel(-1, VA_CMIC_BASE + IRQ_ENABLE_CLEAR);
	writel(-1, VA_IC_BASE + IRQ_ENABLE_CLEAR);
	writel(-1, VA_IC_BASE + FIQ_ENABLE_CLEAR);

	writel(ic_irq_enable, VA_IC_BASE + IRQ_ENABLE_SET);
	return 0;
}
#else
#define irq_suspend NULL
#define irq_resume NULL
#endif

static struct sysdev_class irq_class = {
	set_kset_name("irq"),
	.suspend	= irq_suspend,
	.resume		= irq_resume,
};

static struct sys_device irq_device = {
	.id	= 0,
	.cls	= &irq_class,
};

static int __init irq_init_sysfs(void)
{
	int ret = sysdev_class_register(&irq_class);
	if (ret == 0)
		ret = sysdev_register(&irq_device);
	return ret;
}

device_initcall(irq_init_sysfs);

/*
 * Flash handling.
 */
#define SC_CTRLC (VA_SC_BASE + INTEGRATOR_SC_CTRLC_OFFSET)
#define SC_CTRLS (VA_SC_BASE + INTEGRATOR_SC_CTRLS_OFFSET)
#define EBI_CSR1 (VA_EBI_BASE + INTEGRATOR_EBI_CSR1_OFFSET)
#define EBI_LOCK (VA_EBI_BASE + INTEGRATOR_EBI_LOCK_OFFSET)

static int ap_flash_init(void)
{
	u32 tmp;

	writel(INTEGRATOR_SC_CTRL_nFLVPPEN | INTEGRATOR_SC_CTRL_nFLWP, SC_CTRLC);

	tmp = readl(EBI_CSR1) | INTEGRATOR_EBI_WRITE_ENABLE;
	writel(tmp, EBI_CSR1);

	if (!(readl(EBI_CSR1) & INTEGRATOR_EBI_WRITE_ENABLE)) {
		writel(0xa05f, EBI_LOCK);
		writel(tmp, EBI_CSR1);
		writel(0, EBI_LOCK);
	}
	return 0;
}

static void ap_flash_exit(void)
{
	u32 tmp;

	writel(INTEGRATOR_SC_CTRL_nFLVPPEN | INTEGRATOR_SC_CTRL_nFLWP, SC_CTRLC);

	tmp = readl(EBI_CSR1) & ~INTEGRATOR_EBI_WRITE_ENABLE;
	writel(tmp, EBI_CSR1);

	if (readl(EBI_CSR1) & INTEGRATOR_EBI_WRITE_ENABLE) {
		writel(0xa05f, EBI_LOCK);
		writel(tmp, EBI_CSR1);
		writel(0, EBI_LOCK);
	}
}

static void ap_flash_set_vpp(int on)
{
	unsigned long reg = on ? SC_CTRLS : SC_CTRLC;

	writel(INTEGRATOR_SC_CTRL_nFLVPPEN, reg);
}

static struct flash_platform_data ap_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 4,
	.init		= ap_flash_init,
	.exit		= ap_flash_exit,
	.set_vpp	= ap_flash_set_vpp,
};

static struct resource cfi_flash_resource = {
	.start		= INTEGRATOR_FLASH_BASE,
	.end		= INTEGRATOR_FLASH_BASE + INTEGRATOR_FLASH_SIZE - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device cfi_flash_device = {
	.name		= "armflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &ap_flash_data,
	},
	.num_resources	= 1,
	.resource	= &cfi_flash_resource,
};

static void __init ap_init(void)
{
	unsigned long sc_dec;
	int i;

	platform_device_register(&cfi_flash_device);

	sc_dec = readl(VA_SC_BASE + INTEGRATOR_SC_DEC_OFFSET);
	for (i = 0; i < 4; i++) {
		struct lm_device *lmdev;

		if ((sc_dec & (16 << i)) == 0)
			continue;

		lmdev = kmalloc(sizeof(struct lm_device), GFP_KERNEL);
		if (!lmdev)
			continue;

		memset(lmdev, 0, sizeof(struct lm_device));

		lmdev->resource.start = 0xc0000000 + 0x10000000 * i;
		lmdev->resource.end = lmdev->resource.start + 0x0fffffff;
		lmdev->resource.flags = IORESOURCE_MEM;
		lmdev->irq = IRQ_AP_EXPINT0 + i;
		lmdev->id = i;

		lm_device_register(lmdev);
	}
}

static void __init ap_init_timer(void)
{
	integrator_time_init(1000000 * TICKS_PER_uSEC / HZ, 0);
}

static struct sys_timer ap_timer = {
	.init		= ap_init_timer,
	.offset		= integrator_gettimeoffset,
};

MACHINE_START(INTEGRATOR, "ARM-Integrator")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.phys_ram	= 0x00000000,
	.phys_io	= 0x16000000,
	.io_pg_offst	= ((0xf1600000) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.map_io		= ap_map_io,
	.init_irq	= ap_init_irq,
	.timer		= &ap_timer,
	.init_machine	= ap_init,
MACHINE_END
