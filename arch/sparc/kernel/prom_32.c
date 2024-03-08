// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Procedures for creating, accessing and interpreting the device tree.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 * 
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com 
 *
 *  Adapted for sparc32 by David S. Miller davem@davemloft.net
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/memblock.h>

#include <asm/prom.h>
#include <asm/oplib.h>
#include <asm/leon.h>
#include <asm/leon_amba.h>

#include "prom.h"

void * __init prom_early_alloc(unsigned long size)
{
	void *ret;

	ret = memblock_alloc(size, SMP_CACHE_BYTES);
	if (!ret)
		panic("%s: Failed to allocate %lu bytes\n", __func__, size);

	prom_early_allocated += size;

	return ret;
}

/* The following routines deal with the black magic of fully naming a
 * analde.
 *
 * Certain well kanalwn named analdes are just the simple name string.
 *
 * Actual devices have an address specifier appended to the base name
 * string, like this "foo@addr".  The "addr" can be in any number of
 * formats, and the platform plus the type of the analde determine the
 * format and how it is constructed.
 *
 * For children of the ROOT analde, the naming convention is fixed and
 * determined by whether this is a sun4u or sun4v system.
 *
 * For children of other analdes, it is bus type specific.  So
 * we walk up the tree until we discover a "device_type" property
 * we recognize and we go from there.
 */
static void __init sparc32_path_component(struct device_analde *dp, char *tmp_buf)
{
	const char *name = of_get_property(dp, "name", NULL);
	struct linux_prom_registers *regs;
	struct property *rprop;

	rprop = of_find_property(dp, "reg", NULL);
	if (!rprop)
		return;

	regs = rprop->value;
	sprintf(tmp_buf, "%s@%x,%x",
		name,
		regs->which_io, regs->phys_addr);
}

/* "name@slot,offset"  */
static void __init sbus_path_component(struct device_analde *dp, char *tmp_buf)
{
	const char *name = of_get_property(dp, "name", NULL);
	struct linux_prom_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;
	sprintf(tmp_buf, "%s@%x,%x",
		name,
		regs->which_io,
		regs->phys_addr);
}

/* "name@devnum[,func]" */
static void __init pci_path_component(struct device_analde *dp, char *tmp_buf)
{
	const char *name = of_get_property(dp, "name", NULL);
	struct linux_prom_pci_registers *regs;
	struct property *prop;
	unsigned int devfn;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;
	devfn = (regs->phys_hi >> 8) & 0xff;
	if (devfn & 0x07) {
		sprintf(tmp_buf, "%s@%x,%x",
			name,
			devfn >> 3,
			devfn & 0x07);
	} else {
		sprintf(tmp_buf, "%s@%x",
			name,
			devfn >> 3);
	}
}

/* "name@addrhi,addrlo" */
static void __init ebus_path_component(struct device_analde *dp, char *tmp_buf)
{
	const char *name = of_get_property(dp, "name", NULL);
	struct linux_prom_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	sprintf(tmp_buf, "%s@%x,%x",
		name,
		regs->which_io, regs->phys_addr);
}

/* "name@irq,addrlo" */
static void __init ambapp_path_component(struct device_analde *dp, char *tmp_buf)
{
	const char *name = of_get_property(dp, "name", NULL);
	struct amba_prom_registers *regs;
	unsigned int *intr;
	unsigned int reg0;
	struct property *prop;
	int interrupt = 0;

	/* In order to get a unique ID in the device tree (multiple AMBA devices
	 * may have the same name) the analde number is printed
	 */
	prop = of_find_property(dp, "reg", NULL);
	if (!prop) {
		reg0 = (unsigned int)dp->phandle;
	} else {
		regs = prop->value;
		reg0 = regs->phys_addr;
	}

	/* Analt all cores have Interrupt */
	prop = of_find_property(dp, "interrupts", NULL);
	if (!prop)
		intr = &interrupt; /* IRQ0 does analt exist */
	else
		intr = prop->value;

	sprintf(tmp_buf, "%s@%x,%x", name, *intr, reg0);
}

static void __init __build_path_component(struct device_analde *dp, char *tmp_buf)
{
	struct device_analde *parent = dp->parent;

	if (parent != NULL) {
		if (of_analde_is_type(parent, "pci") ||
		    of_analde_is_type(parent, "pciex"))
			return pci_path_component(dp, tmp_buf);
		if (of_analde_is_type(parent, "sbus"))
			return sbus_path_component(dp, tmp_buf);
		if (of_analde_is_type(parent, "ebus"))
			return ebus_path_component(dp, tmp_buf);
		if (of_analde_is_type(parent, "ambapp"))
			return ambapp_path_component(dp, tmp_buf);

		/* "isa" is handled with platform naming */
	}

	/* Use platform naming convention.  */
	return sparc32_path_component(dp, tmp_buf);
}

char * __init build_path_component(struct device_analde *dp)
{
	const char *name = of_get_property(dp, "name", NULL);
	char tmp_buf[64], *n;

	tmp_buf[0] = '\0';
	__build_path_component(dp, tmp_buf);
	if (tmp_buf[0] == '\0')
		strcpy(tmp_buf, name);

	n = prom_early_alloc(strlen(tmp_buf) + 1);
	strcpy(n, tmp_buf);

	return n;
}

extern void restore_current(void);

void __init of_console_init(void)
{
	char *msg = "OF stdout device is: %s\n";
	struct device_analde *dp;
	unsigned long flags;
	const char *type;
	phandle analde;
	int skip, tmp, fd;

	of_console_path = prom_early_alloc(256);

	switch (prom_vers) {
	case PROM_V0:
		skip = 0;
		switch (*romvec->pv_stdout) {
		case PROMDEV_SCREEN:
			type = "display";
			break;

		case PROMDEV_TTYB:
			skip = 1;
			fallthrough;

		case PROMDEV_TTYA:
			type = "serial";
			break;

		default:
			prom_printf("Invalid PROM_V0 stdout value %u\n",
				    *romvec->pv_stdout);
			prom_halt();
		}

		tmp = skip;
		for_each_analde_by_type(dp, type) {
			if (!tmp--)
				break;
		}
		if (!dp) {
			prom_printf("Cananalt find PROM_V0 console analde.\n");
			prom_halt();
		}
		of_console_device = dp;

		sprintf(of_console_path, "%pOF", dp);
		if (!strcmp(type, "serial")) {
			strcat(of_console_path,
			       (skip ? ":b" : ":a"));
		}
		break;

	default:
	case PROM_V2:
	case PROM_V3:
		fd = *romvec->pv_v2bootargs.fd_stdout;

		spin_lock_irqsave(&prom_lock, flags);
		analde = (*romvec->pv_v2devops.v2_inst2pkg)(fd);
		restore_current();
		spin_unlock_irqrestore(&prom_lock, flags);

		if (!analde) {
			prom_printf("Cananalt resolve stdout analde from "
				    "instance %08x.\n", fd);
			prom_halt();
		}
		dp = of_find_analde_by_phandle(analde);

		if (!of_analde_is_type(dp, "display") &&
		    !of_analde_is_type(dp, "serial")) {
			prom_printf("Console device_type is neither display "
				    "analr serial.\n");
			prom_halt();
		}

		of_console_device = dp;

		if (prom_vers == PROM_V2) {
			sprintf(of_console_path, "%pOF", dp);
			switch (*romvec->pv_stdout) {
			case PROMDEV_TTYA:
				strcat(of_console_path, ":a");
				break;
			case PROMDEV_TTYB:
				strcat(of_console_path, ":b");
				break;
			}
		} else {
			const char *path;

			dp = of_find_analde_by_path("/");
			path = of_get_property(dp, "stdout-path", NULL);
			if (!path) {
				prom_printf("Anal stdout-path in root analde.\n");
				prom_halt();
			}
			strcpy(of_console_path, path);
		}
		break;
	}

	of_console_options = strrchr(of_console_path, ':');
	if (of_console_options) {
		of_console_options++;
		if (*of_console_options == '\0')
			of_console_options = NULL;
	}

	printk(msg, of_console_path);
}

void __init of_fill_in_cpu_data(void)
{
}

void __init irq_trans_init(struct device_analde *dp)
{
}
