// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Firmware-Assisted Dump support on POWERVM platform.
 *
 * Copyright 2011, Mahesh Salgaonkar, IBM Corporation.
 * Copyright 2019, Hari Bathini, IBM Corporation.
 */

#define pr_fmt(fmt) "rtas fadump: " fmt

#include <linux/string.h>
#include <linux/memblock.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/crash_dump.h>

#include <asm/page.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/fadump.h>
#include <asm/fadump-internal.h>

#include "rtas-fadump.h"

static u64 rtas_fadump_init_mem_struct(struct fw_dump *fadump_conf)
{
	return fadump_conf->reserve_dump_area_start;
}

static int rtas_fadump_register(struct fw_dump *fadump_conf)
{
	return -EIO;
}

static int rtas_fadump_unregister(struct fw_dump *fadump_conf)
{
	return -EIO;
}

static int rtas_fadump_invalidate(struct fw_dump *fadump_conf)
{
	return -EIO;
}

/*
 * Validate and process the dump data stored by firmware before exporting
 * it through '/proc/vmcore'.
 */
static int __init rtas_fadump_process(struct fw_dump *fadump_conf)
{
	return -EINVAL;
}

static void rtas_fadump_region_show(struct fw_dump *fadump_conf,
				    struct seq_file *m)
{
}

static void rtas_fadump_trigger(struct fadump_crash_info_header *fdh,
				const char *msg)
{
	/* Call ibm,os-term rtas call to trigger firmware assisted dump */
	rtas_os_term((char *)msg);
}

static struct fadump_ops rtas_fadump_ops = {
	.fadump_init_mem_struct		= rtas_fadump_init_mem_struct,
	.fadump_register		= rtas_fadump_register,
	.fadump_unregister		= rtas_fadump_unregister,
	.fadump_invalidate		= rtas_fadump_invalidate,
	.fadump_process			= rtas_fadump_process,
	.fadump_region_show		= rtas_fadump_region_show,
	.fadump_trigger			= rtas_fadump_trigger,
};

void __init rtas_fadump_dt_scan(struct fw_dump *fadump_conf, u64 node)
{
	int i, size, num_sections;
	const __be32 *sections;
	const __be32 *token;

	/*
	 * Check if Firmware Assisted dump is supported. if yes, check
	 * if dump has been initiated on last reboot.
	 */
	token = of_get_flat_dt_prop(node, "ibm,configure-kernel-dump", NULL);
	if (!token)
		return;

	fadump_conf->ibm_configure_kernel_dump = be32_to_cpu(*token);
	fadump_conf->ops		= &rtas_fadump_ops;
	fadump_conf->fadump_supported	= 1;

	/* Get the sizes required to store dump data for the firmware provided
	 * dump sections.
	 * For each dump section type supported, a 32bit cell which defines
	 * the ID of a supported section followed by two 32 bit cells which
	 * gives the size of the section in bytes.
	 */
	sections = of_get_flat_dt_prop(node, "ibm,configure-kernel-dump-sizes",
					&size);

	if (!sections)
		return;

	num_sections = size / (3 * sizeof(u32));

	for (i = 0; i < num_sections; i++, sections += 3) {
		u32 type = (u32)of_read_number(sections, 1);

		switch (type) {
		case RTAS_FADUMP_CPU_STATE_DATA:
			fadump_conf->cpu_state_data_size =
					of_read_ulong(&sections[1], 2);
			break;
		case RTAS_FADUMP_HPTE_REGION:
			fadump_conf->hpte_region_size =
					of_read_ulong(&sections[1], 2);
			break;
		}
	}
}
