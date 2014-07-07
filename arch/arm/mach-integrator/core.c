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
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/mach-types.h>
#include <asm/mach/time.h>
#include <asm/pgtable.h>

#include "hardware.h"
#include "cm.h"
#include "common.h"

static DEFINE_RAW_SPINLOCK(cm_lock);
static void __iomem *cm_base;

/**
 * cm_get - get the value from the CM_CTRL register
 */
u32 cm_get(void)
{
	return readl(cm_base + INTEGRATOR_HDR_CTRL_OFFSET);
}

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
	val = readl(cm_base + INTEGRATOR_HDR_CTRL_OFFSET) & ~mask;
	writel(val | set, cm_base + INTEGRATOR_HDR_CTRL_OFFSET);
	raw_spin_unlock_irqrestore(&cm_lock, flags);
}

static const char *integrator_arch_str(u32 id)
{
	switch ((id >> 16) & 0xff) {
	case 0x00:
		return "ASB little-endian";
	case 0x01:
		return "AHB little-endian";
	case 0x03:
		return "AHB-Lite system bus, bi-endian";
	case 0x04:
		return "AHB";
	case 0x08:
		return "AHB system bus, ASB processor bus";
	default:
		return "Unknown";
	}
}

static const char *integrator_fpga_str(u32 id)
{
	switch ((id >> 12) & 0xf) {
	case 0x01:
		return "XC4062";
	case 0x02:
		return "XC4085";
	case 0x03:
		return "XVC600";
	case 0x04:
		return "EPM7256AE (Altera PLD)";
	default:
		return "Unknown";
	}
}

void cm_clear_irqs(void)
{
	/* disable core module IRQs */
	writel(0xffffffffU, cm_base + INTEGRATOR_HDR_IC_OFFSET +
		IRQ_ENABLE_CLEAR);
}

static const struct of_device_id cm_match[] = {
	{ .compatible = "arm,core-module-integrator"},
	{ },
};

void cm_init(void)
{
	struct device_node *cm = of_find_matching_node(NULL, cm_match);
	u32 val;

	if (!cm) {
		pr_crit("no core module node found in device tree\n");
		return;
	}
	cm_base = of_iomap(cm, 0);
	if (!cm_base) {
		pr_crit("could not remap core module\n");
		return;
	}
	cm_clear_irqs();
	val = readl(cm_base + INTEGRATOR_HDR_ID_OFFSET);
	pr_info("Detected ARM core module:\n");
	pr_info("    Manufacturer: %02x\n", (val >> 24));
	pr_info("    Architecture: %s\n", integrator_arch_str(val));
	pr_info("    FPGA: %s\n", integrator_fpga_str(val));
	pr_info("    Build: %02x\n", (val >> 4) & 0xFF);
	pr_info("    Rev: %c\n", ('A' + (val & 0x03)));
}

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
void integrator_restart(enum reboot_mode mode, const char *cmd)
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
	return sprintf(buf, "%s\n", integrator_arch_str(integrator_id));
}

static struct device_attribute intcp_arch_attr =
	__ATTR(architecture,  S_IRUGO, intcp_get_arch,  NULL);

static ssize_t intcp_get_fpga(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%s\n", integrator_fpga_str(integrator_id));
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
