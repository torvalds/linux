/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <john@phrozen.org>
 *  Copyright (C) 2013-2015 Lantiq Beteiligungs-GmbH & Co.KG
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/pm.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/reset-controller.h>

#include <asm/reboot.h>

#include <lantiq_soc.h>

#include "../prom.h"

/* reset request register */
#define RCU_RST_REQ		0x0010
/* reset status register */
#define RCU_RST_STAT		0x0014

/* xbar BE flag */
#define RCU_AHB_ENDIAN          0x004C
#define RCU_VR9_BE_AHB1S        0x00000008

/* reboot bit */
#define RCU_RD_GPHY0_XRX200	BIT(31)
#define RCU_RD_SRST		BIT(30)
#define RCU_RD_GPHY1_XRX200	BIT(29)

/* reset cause */
#define RCU_STAT_SHIFT		26
/* boot selection */
#define RCU_BOOT_SEL(x)		((x >> 18) & 0x7)
#define RCU_BOOT_SEL_XRX200(x)	(((x >> 17) & 0xf) | ((x >> 8) & 0x10))

/* dwc2 USB configuration registers */
#define RCU_USB1CFG		0x0018
#define RCU_USB2CFG		0x0034

/* USB DMA endianness bits */
#define RCU_USBCFG_HDSEL_BIT	BIT(11)
#define RCU_USBCFG_HOST_END_BIT	BIT(10)
#define RCU_USBCFG_SLV_END_BIT	BIT(9)

/* USB reset bits */
#define RCU_USBRESET		0x0010

#define USBRESET_BIT		BIT(4)

#define RCU_USBRESET2		0x0048

#define USB1RESET_BIT		BIT(4)
#define USB2RESET_BIT		BIT(5)

#define RCU_CFG1A		0x0038
#define RCU_CFG1B		0x003C

/* USB PMU devices */
#define PMU_AHBM		BIT(15)
#define PMU_USB0		BIT(6)
#define PMU_USB1		BIT(27)

/* USB PHY PMU devices */
#define PMU_USB0_P		BIT(0)
#define PMU_USB1_P		BIT(26)

/* remapped base addr of the reset control unit */
static void __iomem *ltq_rcu_membase;
static struct device_node *ltq_rcu_np;

static void ltq_rcu_w32(uint32_t val, uint32_t reg_off)
{
	ltq_w32(val, ltq_rcu_membase + reg_off);
}

static uint32_t ltq_rcu_r32(uint32_t reg_off)
{
	return ltq_r32(ltq_rcu_membase + reg_off);
}

static void ltq_machine_restart(char *command)
{
	u32 val = ltq_rcu_r32(RCU_RST_REQ);

	if (of_device_is_compatible(ltq_rcu_np, "lantiq,rcu-xrx200"))
		val |= RCU_RD_GPHY1_XRX200 | RCU_RD_GPHY0_XRX200;

	val |= RCU_RD_SRST;

	local_irq_disable();
	ltq_rcu_w32(val, RCU_RST_REQ);
	unreachable();
}

static void ltq_machine_halt(void)
{
	local_irq_disable();
	unreachable();
}

static void ltq_machine_power_off(void)
{
	local_irq_disable();
	unreachable();
}

static int __init mips_reboot_setup(void)
{
	struct resource res;

	ltq_rcu_np = of_find_compatible_node(NULL, NULL, "lantiq,rcu-xway");
	if (!ltq_rcu_np)
		ltq_rcu_np = of_find_compatible_node(NULL, NULL,
							"lantiq,rcu-xrx200");

	/* check if all the reset register range is available */
	if (!ltq_rcu_np)
		panic("Failed to load reset resources from devicetree");

	if (of_address_to_resource(ltq_rcu_np, 0, &res))
		panic("Failed to get rcu memory range");

	if (!request_mem_region(res.start, resource_size(&res), res.name))
		pr_err("Failed to request rcu memory");

	ltq_rcu_membase = ioremap_nocache(res.start, resource_size(&res));
	if (!ltq_rcu_membase)
		panic("Failed to remap core memory");

	_machine_restart = ltq_machine_restart;
	_machine_halt = ltq_machine_halt;
	pm_power_off = ltq_machine_power_off;

	return 0;
}

arch_initcall(mips_reboot_setup);
