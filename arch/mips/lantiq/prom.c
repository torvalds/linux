/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 * Copyright (C) 2010 John Crispin <john@phrozen.org>
 */

#include <linux/export.h>
#include <linux/clk.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>

#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/prom.h>

#include <lantiq.h>

#include "prom.h"
#include "clk.h"

/* access to the ebu needs to be locked between different drivers */
DEFINE_SPINLOCK(ebu_lock);
EXPORT_SYMBOL_GPL(ebu_lock);

/*
 * This is needed by the VPE loader code, just set it to 0 and assume
 * that the firmware hardcodes this value to something useful.
 */
unsigned long physical_memsize = 0L;

/*
 * this struct is filled by the soc specific detection code and holds
 * information about the specific soc type, revision and name
 */
static struct ltq_soc_info soc_info;

const char *get_system_type(void)
{
	return soc_info.sys_type;
}

int ltq_soc_type(void)
{
	return soc_info.type;
}

void __init prom_free_prom_memory(void)
{
}

static void __init prom_init_cmdline(void)
{
	int argc = fw_arg0;
	char **argv = (char **) KSEG1ADDR(fw_arg1);
	int i;

	arcs_cmdline[0] = '\0';

	for (i = 0; i < argc; i++) {
		char *p = (char *) KSEG1ADDR(argv[i]);

		if (CPHYSADDR(p) && *p) {
			strlcat(arcs_cmdline, p, sizeof(arcs_cmdline));
			strlcat(arcs_cmdline, " ", sizeof(arcs_cmdline));
		}
	}
}

void __init plat_mem_setup(void)
{
	void *dtb;

	ioport_resource.start = IOPORT_RESOURCE_START;
	ioport_resource.end = IOPORT_RESOURCE_END;
	iomem_resource.start = IOMEM_RESOURCE_START;
	iomem_resource.end = IOMEM_RESOURCE_END;

	set_io_port_base((unsigned long) KSEG1);

	if (fw_passed_dtb) /* UHI interface */
		dtb = (void *)fw_passed_dtb;
	else if (__dtb_start != __dtb_end)
		dtb = (void *)__dtb_start;
	else
		panic("no dtb found");

	/*
	 * Load the devicetree. This causes the chosen node to be
	 * parsed resulting in our memory appearing
	 */
	__dt_setup_arch(dtb);
}

void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();
}

void __init prom_init(void)
{
	/* call the soc specific detetcion code and get it to fill soc_info */
	ltq_soc_detect(&soc_info);
	snprintf(soc_info.sys_type, LTQ_SYS_TYPE_LEN - 1, "%s rev %s",
		soc_info.name, soc_info.rev_type);
	soc_info.sys_type[LTQ_SYS_TYPE_LEN - 1] = '\0';
	pr_info("SoC: %s\n", soc_info.sys_type);
	prom_init_cmdline();

#if defined(CONFIG_MIPS_MT_SMP)
	if (register_vsmp_smp_ops())
		panic("failed to register_vsmp_smp_ops()");
#endif
}
