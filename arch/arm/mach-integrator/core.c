/*
 *  linux/arch/arm/mach-integrator/core.c
 *
 *  Copyright (C) 2000-2003 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/memblock.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>
#include <linux/io.h>
#include <linux/stat.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/cm.h>
#include <mach/irqs.h>

#include <asm/mach-types.h>
#include <asm/mach/time.h>
#include <asm/pgtable.h>

#include "common.h"

#ifdef CONFIG_ATAGS

#define INTEGRATOR_RTC_IRQ	{ IRQ_RTCINT }
#define INTEGRATOR_UART0_IRQ	{ IRQ_UARTINT0 }
#define INTEGRATOR_UART1_IRQ	{ IRQ_UARTINT1 }
#define KMI0_IRQ		{ IRQ_KMIINT0 }
#define KMI1_IRQ		{ IRQ_KMIINT1 }

static AMBA_APB_DEVICE(rtc, "rtc", 0,
	INTEGRATOR_RTC_BASE, INTEGRATOR_RTC_IRQ, NULL);

static AMBA_APB_DEVICE(uart0, "uart0", 0,
	INTEGRATOR_UART0_BASE, INTEGRATOR_UART0_IRQ, NULL);

static AMBA_APB_DEVICE(uart1, "uart1", 0,
	INTEGRATOR_UART1_BASE, INTEGRATOR_UART1_IRQ, NULL);

static AMBA_APB_DEVICE(kmi0, "kmi0", 0, KMI0_BASE, KMI0_IRQ, NULL);
static AMBA_APB_DEVICE(kmi1, "kmi1", 0, KMI1_BASE, KMI1_IRQ, NULL);

static struct amba_device *amba_devs[] __initdata = {
	&rtc_device,
	&uart0_device,
	&uart1_device,
	&kmi0_device,
	&kmi1_device,
};

int __init integrator_init(bool is_cp)
{
	int i;

	/*
	 * The Integrator/AP lacks necessary AMBA PrimeCell IDs, so we need to
	 * hard-code them. The Integator/CP and forward have proper cell IDs.
	 * Else we leave them undefined to the bus driver can autoprobe them.
	 */
	if (!is_cp) {
		rtc_device.periphid	= 0x00041030;
		uart0_device.periphid	= 0x00041010;
		uart1_device.periphid	= 0x00041010;
		kmi0_device.periphid	= 0x00041050;
		kmi1_device.periphid	= 0x00041050;
		uart0_device.dev.platform_data = &ap_uart_data;
		uart1_device.dev.platform_data = &ap_uart_data;
	}

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

	return 0;
}

#endif

static DEFINE_RAW_SPINLOCK(cm_lock);

/**
 * cm_control - update the CM_CTRL register.
 * @mask: bits to change
 * @set: bits to set
 */
void cm_control(u32 mask, u32 set)
{
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&cm_lock, flags);
	val = readl(CM_CTRL) & ~mask;
	writel(val | set, CM_CTRL);
	raw_spin_unlock_irqrestore(&cm_lock, flags);
}

EXPORT_SYMBOL(cm_control);

/*
 * We need to stop things allocating the low memory; ideally we need a
 * better implementation of GFP_DMA which does not assume that DMA-able
 * memory starts at zero.
 */
void __init integrator_reserve(void)
{
	memblock_reserve(PHYS_OFFSET, __pa(swapper_pg_dir) - PHYS_OFFSET);
}

/*
 * To reset, we hit the on-board reset register in the system FPGA
 */
void integrator_restart(char mode, const char *cmd)
{
	cm_control(CM_CTRL_RESET, CM_CTRL_RESET);
}

static u32 integrator_id;

static ssize_t intcp_get_manf(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%02x\n", integrator_id >> 24);
}

static struct device_attribute intcp_manf_attr =
	__ATTR(manufacturer,  S_IRUGO, intcp_get_manf,  NULL);

static ssize_t intcp_get_arch(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	const char *arch;

	switch ((integrator_id >> 16) & 0xff) {
	case 0x00:
		arch = "ASB little-endian";
		break;
	case 0x01:
		arch = "AHB little-endian";
		break;
	case 0x03:
		arch = "AHB-Lite system bus, bi-endian";
		break;
	case 0x04:
		arch = "AHB";
		break;
	default:
		arch = "Unknown";
		break;
	}

	return sprintf(buf, "%s\n", arch);
}

static struct device_attribute intcp_arch_attr =
	__ATTR(architecture,  S_IRUGO, intcp_get_arch,  NULL);

static ssize_t intcp_get_fpga(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	const char *fpga;

	switch ((integrator_id >> 12) & 0xf) {
	case 0x01:
		fpga = "XC4062";
		break;
	case 0x02:
		fpga = "XC4085";
		break;
	case 0x04:
		fpga = "EPM7256AE (Altera PLD)";
		break;
	default:
		fpga = "Unknown";
		break;
	}

	return sprintf(buf, "%s\n", fpga);
}

static struct device_attribute intcp_fpga_attr =
	__ATTR(fpga,  S_IRUGO, intcp_get_fpga,  NULL);

static ssize_t intcp_get_build(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return sprintf(buf, "%02x\n", (integrator_id >> 4) & 0xFF);
}

static struct device_attribute intcp_build_attr =
	__ATTR(build,  S_IRUGO, intcp_get_build,  NULL);



void integrator_init_sysfs(struct device *parent, u32 id)
{
	integrator_id = id;
	device_create_file(parent, &intcp_manf_attr);
	device_create_file(parent, &intcp_arch_attr);
	device_create_file(parent, &intcp_fpga_attr);
	device_create_file(parent, &intcp_build_attr);
}
