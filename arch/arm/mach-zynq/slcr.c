// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xilinx SLCR driver
 *
 * Copyright (c) 2011-2013 Xilinx Inc.
 */

#include <linux/io.h>
#include <linux/reboot.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/clk/zynq.h>
#include "common.h"

/* register offsets */
#define SLCR_UNLOCK_OFFSET		0x8   /* SCLR unlock register */
#define SLCR_PS_RST_CTRL_OFFSET		0x200 /* PS Software Reset Control */
#define SLCR_A9_CPU_RST_CTRL_OFFSET	0x244 /* CPU Software Reset Control */
#define SLCR_REBOOT_STATUS_OFFSET	0x258 /* PS Reboot Status */
#define SLCR_PSS_IDCODE			0x530 /* PS IDCODE */
#define SLCR_L2C_RAM			0xA1C /* L2C_RAM in AR#54190 */

#define SLCR_UNLOCK_MAGIC		0xDF0D
#define SLCR_A9_CPU_CLKSTOP		0x10
#define SLCR_A9_CPU_RST			0x1
#define SLCR_PSS_IDCODE_DEVICE_SHIFT	12
#define SLCR_PSS_IDCODE_DEVICE_MASK	0x1F

static void __iomem *zynq_slcr_base;
static struct regmap *zynq_slcr_regmap;

/**
 * zynq_slcr_write - Write to a register in SLCR block
 *
 * @val:	Value to write to the register
 * @offset:	Register offset in SLCR block
 *
 * Return:	a negative value on error, 0 on success
 */
static int zynq_slcr_write(u32 val, u32 offset)
{
	return regmap_write(zynq_slcr_regmap, offset, val);
}

/**
 * zynq_slcr_read - Read a register in SLCR block
 *
 * @val:	Pointer to value to be read from SLCR
 * @offset:	Register offset in SLCR block
 *
 * Return:	a negative value on error, 0 on success
 */
static int zynq_slcr_read(u32 *val, u32 offset)
{
	return regmap_read(zynq_slcr_regmap, offset, val);
}

/**
 * zynq_slcr_unlock - Unlock SLCR registers
 *
 * Return:	a negative value on error, 0 on success
 */
static inline int zynq_slcr_unlock(void)
{
	zynq_slcr_write(SLCR_UNLOCK_MAGIC, SLCR_UNLOCK_OFFSET);

	return 0;
}

/**
 * zynq_slcr_get_device_id - Read device code id
 *
 * Return:	Device code id
 */
u32 zynq_slcr_get_device_id(void)
{
	u32 val;

	zynq_slcr_read(&val, SLCR_PSS_IDCODE);
	val >>= SLCR_PSS_IDCODE_DEVICE_SHIFT;
	val &= SLCR_PSS_IDCODE_DEVICE_MASK;

	return val;
}

/**
 * zynq_slcr_system_restart - Restart the entire system.
 *
 * @nb:		Pointer to restart notifier block (unused)
 * @action:	Reboot mode (unused)
 * @data:	Restart handler private data (unused)
 *
 * Return:	0 always
 */
static
int zynq_slcr_system_restart(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	u32 reboot;

	/*
	 * Clear 0x0F000000 bits of reboot status register to workaround
	 * the FSBL not loading the bitstream after soft-reboot
	 * This is a temporary solution until we know more.
	 */
	zynq_slcr_read(&reboot, SLCR_REBOOT_STATUS_OFFSET);
	zynq_slcr_write(reboot & 0xF0FFFFFF, SLCR_REBOOT_STATUS_OFFSET);
	zynq_slcr_write(1, SLCR_PS_RST_CTRL_OFFSET);
	return 0;
}

static struct notifier_block zynq_slcr_restart_nb = {
	.notifier_call	= zynq_slcr_system_restart,
	.priority	= 192,
};

/**
 * zynq_slcr_cpu_start - Start cpu
 * @cpu:	cpu number
 */
void zynq_slcr_cpu_start(int cpu)
{
	u32 reg;

	zynq_slcr_read(&reg, SLCR_A9_CPU_RST_CTRL_OFFSET);
	reg &= ~(SLCR_A9_CPU_RST << cpu);
	zynq_slcr_write(reg, SLCR_A9_CPU_RST_CTRL_OFFSET);
	reg &= ~(SLCR_A9_CPU_CLKSTOP << cpu);
	zynq_slcr_write(reg, SLCR_A9_CPU_RST_CTRL_OFFSET);

	zynq_slcr_cpu_state_write(cpu, false);
}

/**
 * zynq_slcr_cpu_stop - Stop cpu
 * @cpu:	cpu number
 */
void zynq_slcr_cpu_stop(int cpu)
{
	u32 reg;

	zynq_slcr_read(&reg, SLCR_A9_CPU_RST_CTRL_OFFSET);
	reg |= (SLCR_A9_CPU_CLKSTOP | SLCR_A9_CPU_RST) << cpu;
	zynq_slcr_write(reg, SLCR_A9_CPU_RST_CTRL_OFFSET);
}

/**
 * zynq_slcr_cpu_state - Read/write cpu state
 * @cpu:	cpu number
 *
 * SLCR_REBOOT_STATUS save upper 2 bits (31/30 cpu states for cpu0 and cpu1)
 * 0 means cpu is running, 1 cpu is going to die.
 *
 * Return: true if cpu is running, false if cpu is going to die
 */
bool zynq_slcr_cpu_state_read(int cpu)
{
	u32 state;

	state = readl(zynq_slcr_base + SLCR_REBOOT_STATUS_OFFSET);
	state &= 1 << (31 - cpu);

	return !state;
}

/**
 * zynq_slcr_cpu_state - Read/write cpu state
 * @cpu:	cpu number
 * @die:	cpu state - true if cpu is going to die
 *
 * SLCR_REBOOT_STATUS save upper 2 bits (31/30 cpu states for cpu0 and cpu1)
 * 0 means cpu is running, 1 cpu is going to die.
 */
void zynq_slcr_cpu_state_write(int cpu, bool die)
{
	u32 state, mask;

	state = readl(zynq_slcr_base + SLCR_REBOOT_STATUS_OFFSET);
	mask = 1 << (31 - cpu);
	if (die)
		state |= mask;
	else
		state &= ~mask;
	writel(state, zynq_slcr_base + SLCR_REBOOT_STATUS_OFFSET);
}

/**
 * zynq_early_slcr_init - Early slcr init function
 *
 * Return:	0 on success, negative errno otherwise.
 *
 * Called very early during boot from platform code to unlock SLCR.
 */
int __init zynq_early_slcr_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynq-slcr");
	if (!np) {
		pr_err("%s: no slcr node found\n", __func__);
		BUG();
	}

	zynq_slcr_base = of_iomap(np, 0);
	if (!zynq_slcr_base) {
		pr_err("%s: Unable to map I/O memory\n", __func__);
		BUG();
	}

	np->data = (__force void *)zynq_slcr_base;

	zynq_slcr_regmap = syscon_regmap_lookup_by_compatible("xlnx,zynq-slcr");
	if (IS_ERR(zynq_slcr_regmap)) {
		pr_err("%s: failed to find zynq-slcr\n", __func__);
		of_node_put(np);
		return -ENODEV;
	}

	/* unlock the SLCR so that registers can be changed */
	zynq_slcr_unlock();

	/* See AR#54190 design advisory */
	regmap_update_bits(zynq_slcr_regmap, SLCR_L2C_RAM, 0x70707, 0x20202);

	register_restart_handler(&zynq_slcr_restart_nb);

	pr_info("%pOFn mapped to %p\n", np, zynq_slcr_base);

	of_node_put(np);

	return 0;
}
