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

static struct rtas_fadump_mem_struct fdm;

static void rtas_fadump_update_config(struct fw_dump *fadump_conf,
				      const struct rtas_fadump_mem_struct *fdm)
{
	fadump_conf->boot_mem_dest_addr =
		be64_to_cpu(fdm->rmr_region.destination_address);

	fadump_conf->fadumphdr_addr = (fadump_conf->boot_mem_dest_addr +
				       fadump_conf->boot_memory_size);
}

static u64 rtas_fadump_init_mem_struct(struct fw_dump *fadump_conf)
{
	u64 addr = fadump_conf->reserve_dump_area_start;

	memset(&fdm, 0, sizeof(struct rtas_fadump_mem_struct));
	addr = addr & PAGE_MASK;

	fdm.header.dump_format_version = cpu_to_be32(0x00000001);
	fdm.header.dump_num_sections = cpu_to_be16(3);
	fdm.header.dump_status_flag = 0;
	fdm.header.offset_first_dump_section =
		cpu_to_be32((u32)offsetof(struct rtas_fadump_mem_struct,
					  cpu_state_data));

	/*
	 * Fields for disk dump option.
	 * We are not using disk dump option, hence set these fields to 0.
	 */
	fdm.header.dd_block_size = 0;
	fdm.header.dd_block_offset = 0;
	fdm.header.dd_num_blocks = 0;
	fdm.header.dd_offset_disk_path = 0;

	/* set 0 to disable an automatic dump-reboot. */
	fdm.header.max_time_auto = 0;

	/* Kernel dump sections */
	/* cpu state data section. */
	fdm.cpu_state_data.request_flag =
		cpu_to_be32(RTAS_FADUMP_REQUEST_FLAG);
	fdm.cpu_state_data.source_data_type =
		cpu_to_be16(RTAS_FADUMP_CPU_STATE_DATA);
	fdm.cpu_state_data.source_address = 0;
	fdm.cpu_state_data.source_len =
		cpu_to_be64(fadump_conf->cpu_state_data_size);
	fdm.cpu_state_data.destination_address = cpu_to_be64(addr);
	addr += fadump_conf->cpu_state_data_size;

	/* hpte region section */
	fdm.hpte_region.request_flag = cpu_to_be32(RTAS_FADUMP_REQUEST_FLAG);
	fdm.hpte_region.source_data_type =
		cpu_to_be16(RTAS_FADUMP_HPTE_REGION);
	fdm.hpte_region.source_address = 0;
	fdm.hpte_region.source_len =
		cpu_to_be64(fadump_conf->hpte_region_size);
	fdm.hpte_region.destination_address = cpu_to_be64(addr);
	addr += fadump_conf->hpte_region_size;

	/* RMA region section */
	fdm.rmr_region.request_flag = cpu_to_be32(RTAS_FADUMP_REQUEST_FLAG);
	fdm.rmr_region.source_data_type =
		cpu_to_be16(RTAS_FADUMP_REAL_MODE_REGION);
	fdm.rmr_region.source_address = cpu_to_be64(RMA_START);
	fdm.rmr_region.source_len = cpu_to_be64(fadump_conf->boot_memory_size);
	fdm.rmr_region.destination_address = cpu_to_be64(addr);
	addr += fadump_conf->boot_memory_size;

	rtas_fadump_update_config(fadump_conf, &fdm);

	return addr;
}

static int rtas_fadump_register(struct fw_dump *fadump_conf)
{
	unsigned int wait_time;
	int rc, err = -EIO;

	/* TODO: Add upper time limit for the delay */
	do {
		rc =  rtas_call(fadump_conf->ibm_configure_kernel_dump, 3, 1,
				NULL, FADUMP_REGISTER, &fdm,
				sizeof(struct rtas_fadump_mem_struct));

		wait_time = rtas_busy_delay_time(rc);
		if (wait_time)
			mdelay(wait_time);

	} while (wait_time);

	switch (rc) {
	case 0:
		pr_info("Registration is successful!\n");
		fadump_conf->dump_registered = 1;
		err = 0;
		break;
	case -1:
		pr_err("Failed to register. Hardware Error(%d).\n", rc);
		break;
	case -3:
		if (!is_fadump_boot_mem_contiguous())
			pr_err("Can't have holes in boot memory area.\n");
		else if (!is_fadump_reserved_mem_contiguous())
			pr_err("Can't have holes in reserved memory area.\n");

		pr_err("Failed to register. Parameter Error(%d).\n", rc);
		err = -EINVAL;
		break;
	case -9:
		pr_err("Already registered!\n");
		fadump_conf->dump_registered = 1;
		err = -EEXIST;
		break;
	default:
		pr_err("Failed to register. Unknown Error(%d).\n", rc);
		break;
	}

	return err;
}

static int rtas_fadump_unregister(struct fw_dump *fadump_conf)
{
	unsigned int wait_time;
	int rc;

	/* TODO: Add upper time limit for the delay */
	do {
		rc =  rtas_call(fadump_conf->ibm_configure_kernel_dump, 3, 1,
				NULL, FADUMP_UNREGISTER, &fdm,
				sizeof(struct rtas_fadump_mem_struct));

		wait_time = rtas_busy_delay_time(rc);
		if (wait_time)
			mdelay(wait_time);
	} while (wait_time);

	if (rc) {
		pr_err("Failed to un-register - unexpected error(%d).\n", rc);
		return -EIO;
	}

	fadump_conf->dump_registered = 0;
	return 0;
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
	const struct rtas_fadump_mem_struct *fdm_ptr = &fdm;
	const struct rtas_fadump_section *cpu_data_section;

	cpu_data_section = &(fdm_ptr->cpu_state_data);
	seq_printf(m, "CPU :[%#016llx-%#016llx] %#llx bytes, Dumped: %#llx\n",
		   be64_to_cpu(cpu_data_section->destination_address),
		   be64_to_cpu(cpu_data_section->destination_address) +
		   be64_to_cpu(cpu_data_section->source_len) - 1,
		   be64_to_cpu(cpu_data_section->source_len),
		   be64_to_cpu(cpu_data_section->bytes_dumped));

	seq_printf(m, "HPTE:[%#016llx-%#016llx] %#llx bytes, Dumped: %#llx\n",
		   be64_to_cpu(fdm_ptr->hpte_region.destination_address),
		   be64_to_cpu(fdm_ptr->hpte_region.destination_address) +
		   be64_to_cpu(fdm_ptr->hpte_region.source_len) - 1,
		   be64_to_cpu(fdm_ptr->hpte_region.source_len),
		   be64_to_cpu(fdm_ptr->hpte_region.bytes_dumped));

	seq_printf(m, "DUMP: [%#016llx-%#016llx] %#llx bytes, Dumped: %#llx\n",
		   be64_to_cpu(fdm_ptr->rmr_region.destination_address),
		   be64_to_cpu(fdm_ptr->rmr_region.destination_address) +
		   be64_to_cpu(fdm_ptr->rmr_region.source_len) - 1,
		   be64_to_cpu(fdm_ptr->rmr_region.source_len),
		   be64_to_cpu(fdm_ptr->rmr_region.bytes_dumped));
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
