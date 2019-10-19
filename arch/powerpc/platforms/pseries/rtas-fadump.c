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
static const struct rtas_fadump_mem_struct *fdm_active;

static void rtas_fadump_update_config(struct fw_dump *fadump_conf,
				      const struct rtas_fadump_mem_struct *fdm)
{
	fadump_conf->boot_mem_dest_addr =
		be64_to_cpu(fdm->rmr_region.destination_address);

	fadump_conf->fadumphdr_addr = (fadump_conf->boot_mem_dest_addr +
				       fadump_conf->boot_memory_size);
}

/*
 * This function is called in the capture kernel to get configuration details
 * setup in the first kernel and passed to the f/w.
 */
static void rtas_fadump_get_config(struct fw_dump *fadump_conf,
				   const struct rtas_fadump_mem_struct *fdm)
{
	fadump_conf->boot_mem_addr[0] =
		be64_to_cpu(fdm->rmr_region.source_address);
	fadump_conf->boot_mem_sz[0] = be64_to_cpu(fdm->rmr_region.source_len);
	fadump_conf->boot_memory_size = fadump_conf->boot_mem_sz[0];

	fadump_conf->boot_mem_top = fadump_conf->boot_memory_size;
	fadump_conf->boot_mem_regs_cnt = 1;

	/*
	 * Start address of reserve dump area (permanent reservation) for
	 * re-registering FADump after dump capture.
	 */
	fadump_conf->reserve_dump_area_start =
		be64_to_cpu(fdm->cpu_state_data.destination_address);

	rtas_fadump_update_config(fadump_conf, fdm);
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
	fdm.rmr_region.source_address = cpu_to_be64(0);
	fdm.rmr_region.source_len = cpu_to_be64(fadump_conf->boot_memory_size);
	fdm.rmr_region.destination_address = cpu_to_be64(addr);
	addr += fadump_conf->boot_memory_size;

	rtas_fadump_update_config(fadump_conf, &fdm);

	return addr;
}

static u64 rtas_fadump_get_bootmem_min(void)
{
	return RTAS_FADUMP_MIN_BOOT_MEM;
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
	unsigned int wait_time;
	int rc;

	/* TODO: Add upper time limit for the delay */
	do {
		rc =  rtas_call(fadump_conf->ibm_configure_kernel_dump, 3, 1,
				NULL, FADUMP_INVALIDATE, fdm_active,
				sizeof(struct rtas_fadump_mem_struct));

		wait_time = rtas_busy_delay_time(rc);
		if (wait_time)
			mdelay(wait_time);
	} while (wait_time);

	if (rc) {
		pr_err("Failed to invalidate - unexpected error (%d).\n", rc);
		return -EIO;
	}

	fadump_conf->dump_active = 0;
	fdm_active = NULL;
	return 0;
}

#define RTAS_FADUMP_GPR_MASK		0xffffff0000000000
static inline int rtas_fadump_gpr_index(u64 id)
{
	char str[3];
	int i = -1;

	if ((id & RTAS_FADUMP_GPR_MASK) == fadump_str_to_u64("GPR")) {
		/* get the digits at the end */
		id &= ~RTAS_FADUMP_GPR_MASK;
		id >>= 24;
		str[2] = '\0';
		str[1] = id & 0xff;
		str[0] = (id >> 8) & 0xff;
		if (kstrtoint(str, 10, &i))
			i = -EINVAL;
		if (i > 31)
			i = -1;
	}
	return i;
}

void rtas_fadump_set_regval(struct pt_regs *regs, u64 reg_id, u64 reg_val)
{
	int i;

	i = rtas_fadump_gpr_index(reg_id);
	if (i >= 0)
		regs->gpr[i] = (unsigned long)reg_val;
	else if (reg_id == fadump_str_to_u64("NIA"))
		regs->nip = (unsigned long)reg_val;
	else if (reg_id == fadump_str_to_u64("MSR"))
		regs->msr = (unsigned long)reg_val;
	else if (reg_id == fadump_str_to_u64("CTR"))
		regs->ctr = (unsigned long)reg_val;
	else if (reg_id == fadump_str_to_u64("LR"))
		regs->link = (unsigned long)reg_val;
	else if (reg_id == fadump_str_to_u64("XER"))
		regs->xer = (unsigned long)reg_val;
	else if (reg_id == fadump_str_to_u64("CR"))
		regs->ccr = (unsigned long)reg_val;
	else if (reg_id == fadump_str_to_u64("DAR"))
		regs->dar = (unsigned long)reg_val;
	else if (reg_id == fadump_str_to_u64("DSISR"))
		regs->dsisr = (unsigned long)reg_val;
}

static struct rtas_fadump_reg_entry*
rtas_fadump_read_regs(struct rtas_fadump_reg_entry *reg_entry,
		      struct pt_regs *regs)
{
	memset(regs, 0, sizeof(struct pt_regs));

	while (be64_to_cpu(reg_entry->reg_id) != fadump_str_to_u64("CPUEND")) {
		rtas_fadump_set_regval(regs, be64_to_cpu(reg_entry->reg_id),
				       be64_to_cpu(reg_entry->reg_value));
		reg_entry++;
	}
	reg_entry++;
	return reg_entry;
}

/*
 * Read CPU state dump data and convert it into ELF notes.
 * The CPU dump starts with magic number "REGSAVE". NumCpusOffset should be
 * used to access the data to allow for additional fields to be added without
 * affecting compatibility. Each list of registers for a CPU starts with
 * "CPUSTRT" and ends with "CPUEND". Each register entry is of 16 bytes,
 * 8 Byte ASCII identifier and 8 Byte register value. The register entry
 * with identifier "CPUSTRT" and "CPUEND" contains 4 byte cpu id as part
 * of register value. For more details refer to PAPR document.
 *
 * Only for the crashing cpu we ignore the CPU dump data and get exact
 * state from fadump crash info structure populated by first kernel at the
 * time of crash.
 */
static int __init rtas_fadump_build_cpu_notes(struct fw_dump *fadump_conf)
{
	struct rtas_fadump_reg_save_area_header *reg_header;
	struct fadump_crash_info_header *fdh = NULL;
	struct rtas_fadump_reg_entry *reg_entry;
	u32 num_cpus, *note_buf;
	int i, rc = 0, cpu = 0;
	struct pt_regs regs;
	unsigned long addr;
	void *vaddr;

	addr = be64_to_cpu(fdm_active->cpu_state_data.destination_address);
	vaddr = __va(addr);

	reg_header = vaddr;
	if (be64_to_cpu(reg_header->magic_number) !=
	    fadump_str_to_u64("REGSAVE")) {
		pr_err("Unable to read register save area.\n");
		return -ENOENT;
	}

	pr_debug("--------CPU State Data------------\n");
	pr_debug("Magic Number: %llx\n", be64_to_cpu(reg_header->magic_number));
	pr_debug("NumCpuOffset: %x\n", be32_to_cpu(reg_header->num_cpu_offset));

	vaddr += be32_to_cpu(reg_header->num_cpu_offset);
	num_cpus = be32_to_cpu(*((__be32 *)(vaddr)));
	pr_debug("NumCpus     : %u\n", num_cpus);
	vaddr += sizeof(u32);
	reg_entry = (struct rtas_fadump_reg_entry *)vaddr;

	rc = fadump_setup_cpu_notes_buf(num_cpus);
	if (rc != 0)
		return rc;

	note_buf = (u32 *)fadump_conf->cpu_notes_buf_vaddr;

	if (fadump_conf->fadumphdr_addr)
		fdh = __va(fadump_conf->fadumphdr_addr);

	for (i = 0; i < num_cpus; i++) {
		if (be64_to_cpu(reg_entry->reg_id) !=
		    fadump_str_to_u64("CPUSTRT")) {
			pr_err("Unable to read CPU state data\n");
			rc = -ENOENT;
			goto error_out;
		}
		/* Lower 4 bytes of reg_value contains logical cpu id */
		cpu = (be64_to_cpu(reg_entry->reg_value) &
		       RTAS_FADUMP_CPU_ID_MASK);
		if (fdh && !cpumask_test_cpu(cpu, &fdh->online_mask)) {
			RTAS_FADUMP_SKIP_TO_NEXT_CPU(reg_entry);
			continue;
		}
		pr_debug("Reading register data for cpu %d...\n", cpu);
		if (fdh && fdh->crashing_cpu == cpu) {
			regs = fdh->regs;
			note_buf = fadump_regs_to_elf_notes(note_buf, &regs);
			RTAS_FADUMP_SKIP_TO_NEXT_CPU(reg_entry);
		} else {
			reg_entry++;
			reg_entry = rtas_fadump_read_regs(reg_entry, &regs);
			note_buf = fadump_regs_to_elf_notes(note_buf, &regs);
		}
	}
	final_note(note_buf);

	if (fdh) {
		pr_debug("Updating elfcore header (%llx) with cpu notes\n",
			 fdh->elfcorehdr_addr);
		fadump_update_elfcore_header(__va(fdh->elfcorehdr_addr));
	}
	return 0;

error_out:
	fadump_free_cpu_notes_buf();
	return rc;

}

/*
 * Validate and process the dump data stored by firmware before exporting
 * it through '/proc/vmcore'.
 */
static int __init rtas_fadump_process(struct fw_dump *fadump_conf)
{
	struct fadump_crash_info_header *fdh;
	int rc = 0;

	if (!fdm_active || !fadump_conf->fadumphdr_addr)
		return -EINVAL;

	/* Check if the dump data is valid. */
	if ((be16_to_cpu(fdm_active->header.dump_status_flag) ==
			RTAS_FADUMP_ERROR_FLAG) ||
			(fdm_active->cpu_state_data.error_flags != 0) ||
			(fdm_active->rmr_region.error_flags != 0)) {
		pr_err("Dump taken by platform is not valid\n");
		return -EINVAL;
	}
	if ((fdm_active->rmr_region.bytes_dumped !=
			fdm_active->rmr_region.source_len) ||
			!fdm_active->cpu_state_data.bytes_dumped) {
		pr_err("Dump taken by platform is incomplete\n");
		return -EINVAL;
	}

	/* Validate the fadump crash info header */
	fdh = __va(fadump_conf->fadumphdr_addr);
	if (fdh->magic_number != FADUMP_CRASH_INFO_MAGIC) {
		pr_err("Crash info header is not valid.\n");
		return -EINVAL;
	}

	rc = rtas_fadump_build_cpu_notes(fadump_conf);
	if (rc)
		return rc;

	/*
	 * We are done validating dump info and elfcore header is now ready
	 * to be exported. set elfcorehdr_addr so that vmcore module will
	 * export the elfcore header through '/proc/vmcore'.
	 */
	elfcorehdr_addr = fdh->elfcorehdr_addr;

	return 0;
}

static void rtas_fadump_region_show(struct fw_dump *fadump_conf,
				    struct seq_file *m)
{
	const struct rtas_fadump_section *cpu_data_section;
	const struct rtas_fadump_mem_struct *fdm_ptr;

	if (fdm_active)
		fdm_ptr = fdm_active;
	else
		fdm_ptr = &fdm;

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

	seq_printf(m, "DUMP: Src: %#016llx, Dest: %#016llx, ",
		   be64_to_cpu(fdm_ptr->rmr_region.source_address),
		   be64_to_cpu(fdm_ptr->rmr_region.destination_address));
	seq_printf(m, "Size: %#llx, Dumped: %#llx bytes\n",
		   be64_to_cpu(fdm_ptr->rmr_region.source_len),
		   be64_to_cpu(fdm_ptr->rmr_region.bytes_dumped));

	/* Dump is active. Show reserved area start address. */
	if (fdm_active) {
		seq_printf(m, "\nMemory above %#016lx is reserved for saving crash dump\n",
			   fadump_conf->reserve_dump_area_start);
	}
}

static void rtas_fadump_trigger(struct fadump_crash_info_header *fdh,
				const char *msg)
{
	/* Call ibm,os-term rtas call to trigger firmware assisted dump */
	rtas_os_term((char *)msg);
}

static struct fadump_ops rtas_fadump_ops = {
	.fadump_init_mem_struct		= rtas_fadump_init_mem_struct,
	.fadump_get_bootmem_min		= rtas_fadump_get_bootmem_min,
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

	/* Firmware supports 64-bit value for size, align it to pagesize. */
	fadump_conf->max_copy_size = _ALIGN_DOWN(U64_MAX, PAGE_SIZE);

	/*
	 * The 'ibm,kernel-dump' rtas node is present only if there is
	 * dump data waiting for us.
	 */
	fdm_active = of_get_flat_dt_prop(node, "ibm,kernel-dump", NULL);
	if (fdm_active) {
		pr_info("Firmware-assisted dump is active.\n");
		fadump_conf->dump_active = 1;
		rtas_fadump_get_config(fadump_conf, (void *)__pa(fdm_active));
	}

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
