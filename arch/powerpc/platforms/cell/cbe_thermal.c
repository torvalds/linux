/*
 * thermal support for the cell processor
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Christian Krafft <krafft@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/kernel.h>
#include <linux/cpu.h>
#include <asm/spu.h>
#include <asm/io.h>
#include <asm/prom.h>

#include "cbe_regs.h"
#include "spu_priv1_mmio.h"

static struct cbe_pmd_regs __iomem *get_pmd_regs(struct sys_device *sysdev)
{
	struct spu *spu;

	spu = container_of(sysdev, struct spu, sysdev);

	return cbe_get_pmd_regs(spu_devnode(spu));
}

/* returns the value for a given spu in a given register */
static u8 spu_read_register_value(struct sys_device *sysdev, union spe_reg __iomem *reg)
{
	unsigned int *id;
	union spe_reg value;
	struct spu *spu;

	/* getting the id from the reg attribute will not work on future device-tree layouts
	 * in future we should store the id to the spu struct and use it here */
	spu = container_of(sysdev, struct spu, sysdev);
	id = (unsigned int *)get_property(spu_devnode(spu), "reg", NULL);
	value.val = in_be64(&reg->val);

	return value.spe[*id];
}

static ssize_t spu_show_temp(struct sys_device *sysdev, char *buf)
{
	int value;
	struct cbe_pmd_regs __iomem *pmd_regs;

	pmd_regs = get_pmd_regs(sysdev);

	value = spu_read_register_value(sysdev, &pmd_regs->ts_ctsr1);
	/* clear all other bits */
	value &= 0x3F;
	/* temp is stored in steps of 2 degrees */
	value *= 2;
	/* base temp is 65 degrees */
	value += 65;

	return sprintf(buf, "%d\n", (int) value);
}

static ssize_t ppe_show_temp(struct sys_device *sysdev, char *buf, int pos)
{
	struct cbe_pmd_regs __iomem *pmd_regs;
	u64 value;

	pmd_regs = cbe_get_cpu_pmd_regs(sysdev->id);
	value = in_be64(&pmd_regs->ts_ctsr2);

	/* access the corresponding byte */
	value >>= pos;
	/* clear all other bits */
	value &= 0x3F;
	/* temp is stored in steps of 2 degrees */
	value *= 2;
	/* base temp is 65 degrees */
	value += 65;

	return sprintf(buf, "%d\n", (int) value);
}


/* shows the temperature of the DTS on the PPE,
 * located near the linear thermal sensor */
static ssize_t ppe_show_temp0(struct sys_device *sysdev, char *buf)
{
	return ppe_show_temp(sysdev, buf, 32);
}

/* shows the temperature of the second DTS on the PPE */
static ssize_t ppe_show_temp1(struct sys_device *sysdev, char *buf)
{
	return ppe_show_temp(sysdev, buf, 0);
}

static struct sysdev_attribute attr_spu_temperature = {
	.attr = {.name = "temperature", .mode = 0400 },
	.show = spu_show_temp,
};

static struct attribute *spu_attributes[] = {
	&attr_spu_temperature.attr,
};

static struct attribute_group spu_attribute_group = {
	.name	= "thermal",
	.attrs	= spu_attributes,
};

static struct sysdev_attribute attr_ppe_temperature0 = {
	.attr = {.name = "temperature0", .mode = 0400 },
	.show = ppe_show_temp0,
};

static struct sysdev_attribute attr_ppe_temperature1 = {
	.attr = {.name = "temperature1", .mode = 0400 },
	.show = ppe_show_temp1,
};

static struct attribute *ppe_attributes[] = {
	&attr_ppe_temperature0.attr,
	&attr_ppe_temperature1.attr,
};

static struct attribute_group ppe_attribute_group = {
	.name	= "thermal",
	.attrs	= ppe_attributes,
};

/*
 * initialize throttling with default values
 */
static void __init init_default_values(void)
{
	int cpu;
	struct cbe_pmd_regs __iomem *pmd_regs;
	struct sys_device *sysdev;
	union ppe_spe_reg tpr;
	union spe_reg str1;
	u64 str2;
	union spe_reg cr1;
	u64 cr2;

	/* TPR defaults */
	/* ppe
	 *	1F - no full stop
	 *	08 - dynamic throttling starts if over 80 degrees
	 *	03 - dynamic throttling ceases if below 70 degrees */
	tpr.ppe = 0x1F0803;
	/* spe
	 *	10 - full stopped when over 96 degrees
	 *	08 - dynamic throttling starts if over 80 degrees
	 *	03 - dynamic throttling ceases if below 70 degrees
	 */
	tpr.spe = 0x100803;

	/* STR defaults */
	/* str1
	 *	10 - stop 16 of 32 cycles
	 */
	str1.val = 0x1010101010101010ull;
	/* str2
	 *	10 - stop 16 of 32 cycles
	 */
	str2 = 0x10;

	/* CR defaults */
	/* cr1
	 *	4 - normal operation
	 */
	cr1.val = 0x0404040404040404ull;
	/* cr2
	 *	4 - normal operation
	 */
	cr2 = 0x04;

	for_each_possible_cpu (cpu) {
		pr_debug("processing cpu %d\n", cpu);
		sysdev = get_cpu_sysdev(cpu);
		pmd_regs = cbe_get_cpu_pmd_regs(sysdev->id);

		out_be64(&pmd_regs->tm_str2, str2);
		out_be64(&pmd_regs->tm_str1.val, str1.val);
		out_be64(&pmd_regs->tm_tpr.val, tpr.val);
		out_be64(&pmd_regs->tm_cr1.val, cr1.val);
		out_be64(&pmd_regs->tm_cr2, cr2);
	}
}


static int __init thermal_init(void)
{
	init_default_values();

	spu_add_sysdev_attr_group(&spu_attribute_group);
	cpu_add_sysdev_attr_group(&ppe_attribute_group);

	return 0;
}
module_init(thermal_init);

static void __exit thermal_exit(void)
{
	spu_remove_sysdev_attr_group(&spu_attribute_group);
	cpu_remove_sysdev_attr_group(&ppe_attribute_group);
}
module_exit(thermal_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Krafft <krafft@de.ibm.com>");

