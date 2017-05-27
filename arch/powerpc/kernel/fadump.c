/*
 * Firmware Assisted dump: A robust mechanism to get reliable kernel crash
 * dump with assistance from firmware. This approach does not use kexec,
 * instead firmware assists in booting the kdump kernel while preserving
 * memory contents. The most of the code implementation has been adapted
 * from phyp assisted dump implementation written by Linas Vepstas and
 * Manish Ahuja
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright 2011 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#undef DEBUG
#define pr_fmt(fmt) "fadump: " fmt

#include <linux/string.h>
#include <linux/memblock.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/crash_dump.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include <asm/debugfs.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/fadump.h>
#include <asm/setup.h>

static struct fw_dump fw_dump;
static struct fadump_mem_struct fdm;
static const struct fadump_mem_struct *fdm_active;

static DEFINE_MUTEX(fadump_mutex);
struct fad_crash_memory_ranges crash_memory_ranges[INIT_CRASHMEM_RANGES];
int crash_mem_ranges;

/* Scan the Firmware Assisted dump configuration details. */
int __init early_init_dt_scan_fw_dump(unsigned long node,
			const char *uname, int depth, void *data)
{
	const __be32 *sections;
	int i, num_sections;
	int size;
	const __be32 *token;

	if (depth != 1 || strcmp(uname, "rtas") != 0)
		return 0;

	/*
	 * Check if Firmware Assisted dump is supported. if yes, check
	 * if dump has been initiated on last reboot.
	 */
	token = of_get_flat_dt_prop(node, "ibm,configure-kernel-dump", NULL);
	if (!token)
		return 1;

	fw_dump.fadump_supported = 1;
	fw_dump.ibm_configure_kernel_dump = be32_to_cpu(*token);

	/*
	 * The 'ibm,kernel-dump' rtas node is present only if there is
	 * dump data waiting for us.
	 */
	fdm_active = of_get_flat_dt_prop(node, "ibm,kernel-dump", NULL);
	if (fdm_active)
		fw_dump.dump_active = 1;

	/* Get the sizes required to store dump data for the firmware provided
	 * dump sections.
	 * For each dump section type supported, a 32bit cell which defines
	 * the ID of a supported section followed by two 32 bit cells which
	 * gives teh size of the section in bytes.
	 */
	sections = of_get_flat_dt_prop(node, "ibm,configure-kernel-dump-sizes",
					&size);

	if (!sections)
		return 1;

	num_sections = size / (3 * sizeof(u32));

	for (i = 0; i < num_sections; i++, sections += 3) {
		u32 type = (u32)of_read_number(sections, 1);

		switch (type) {
		case FADUMP_CPU_STATE_DATA:
			fw_dump.cpu_state_data_size =
					of_read_ulong(&sections[1], 2);
			break;
		case FADUMP_HPTE_REGION:
			fw_dump.hpte_region_size =
					of_read_ulong(&sections[1], 2);
			break;
		}
	}

	return 1;
}

int is_fadump_active(void)
{
	return fw_dump.dump_active;
}

/* Print firmware assisted dump configurations for debugging purpose. */
static void fadump_show_config(void)
{
	pr_debug("Support for firmware-assisted dump (fadump): %s\n",
			(fw_dump.fadump_supported ? "present" : "no support"));

	if (!fw_dump.fadump_supported)
		return;

	pr_debug("Fadump enabled    : %s\n",
				(fw_dump.fadump_enabled ? "yes" : "no"));
	pr_debug("Dump Active       : %s\n",
				(fw_dump.dump_active ? "yes" : "no"));
	pr_debug("Dump section sizes:\n");
	pr_debug("    CPU state data size: %lx\n", fw_dump.cpu_state_data_size);
	pr_debug("    HPTE region size   : %lx\n", fw_dump.hpte_region_size);
	pr_debug("Boot memory size  : %lx\n", fw_dump.boot_memory_size);
}

static unsigned long init_fadump_mem_struct(struct fadump_mem_struct *fdm,
				unsigned long addr)
{
	if (!fdm)
		return 0;

	memset(fdm, 0, sizeof(struct fadump_mem_struct));
	addr = addr & PAGE_MASK;

	fdm->header.dump_format_version = cpu_to_be32(0x00000001);
	fdm->header.dump_num_sections = cpu_to_be16(3);
	fdm->header.dump_status_flag = 0;
	fdm->header.offset_first_dump_section =
		cpu_to_be32((u32)offsetof(struct fadump_mem_struct, cpu_state_data));

	/*
	 * Fields for disk dump option.
	 * We are not using disk dump option, hence set these fields to 0.
	 */
	fdm->header.dd_block_size = 0;
	fdm->header.dd_block_offset = 0;
	fdm->header.dd_num_blocks = 0;
	fdm->header.dd_offset_disk_path = 0;

	/* set 0 to disable an automatic dump-reboot. */
	fdm->header.max_time_auto = 0;

	/* Kernel dump sections */
	/* cpu state data section. */
	fdm->cpu_state_data.request_flag = cpu_to_be32(FADUMP_REQUEST_FLAG);
	fdm->cpu_state_data.source_data_type = cpu_to_be16(FADUMP_CPU_STATE_DATA);
	fdm->cpu_state_data.source_address = 0;
	fdm->cpu_state_data.source_len = cpu_to_be64(fw_dump.cpu_state_data_size);
	fdm->cpu_state_data.destination_address = cpu_to_be64(addr);
	addr += fw_dump.cpu_state_data_size;

	/* hpte region section */
	fdm->hpte_region.request_flag = cpu_to_be32(FADUMP_REQUEST_FLAG);
	fdm->hpte_region.source_data_type = cpu_to_be16(FADUMP_HPTE_REGION);
	fdm->hpte_region.source_address = 0;
	fdm->hpte_region.source_len = cpu_to_be64(fw_dump.hpte_region_size);
	fdm->hpte_region.destination_address = cpu_to_be64(addr);
	addr += fw_dump.hpte_region_size;

	/* RMA region section */
	fdm->rmr_region.request_flag = cpu_to_be32(FADUMP_REQUEST_FLAG);
	fdm->rmr_region.source_data_type = cpu_to_be16(FADUMP_REAL_MODE_REGION);
	fdm->rmr_region.source_address = cpu_to_be64(RMA_START);
	fdm->rmr_region.source_len = cpu_to_be64(fw_dump.boot_memory_size);
	fdm->rmr_region.destination_address = cpu_to_be64(addr);
	addr += fw_dump.boot_memory_size;

	return addr;
}

/**
 * fadump_calculate_reserve_size(): reserve variable boot area 5% of System RAM
 *
 * Function to find the largest memory size we need to reserve during early
 * boot process. This will be the size of the memory that is required for a
 * kernel to boot successfully.
 *
 * This function has been taken from phyp-assisted dump feature implementation.
 *
 * returns larger of 256MB or 5% rounded down to multiples of 256MB.
 *
 * TODO: Come up with better approach to find out more accurate memory size
 * that is required for a kernel to boot successfully.
 *
 */
static inline unsigned long fadump_calculate_reserve_size(void)
{
	int ret;
	unsigned long long base, size;

	/*
	 * Check if the size is specified through crashkernel= cmdline
	 * option. If yes, then use that but ignore base as fadump
	 * reserves memory at end of RAM.
	 */
	ret = parse_crashkernel(boot_command_line, memblock_phys_mem_size(),
				&size, &base);
	if (ret == 0 && size > 0) {
		fw_dump.reserve_bootvar = (unsigned long)size;
		return fw_dump.reserve_bootvar;
	}

	/* divide by 20 to get 5% of value */
	size = memblock_end_of_DRAM() / 20;

	/* round it down in multiples of 256 */
	size = size & ~0x0FFFFFFFUL;

	/* Truncate to memory_limit. We don't want to over reserve the memory.*/
	if (memory_limit && size > memory_limit)
		size = memory_limit;

	return (size > MIN_BOOT_MEM ? size : MIN_BOOT_MEM);
}

/*
 * Calculate the total memory size required to be reserved for
 * firmware-assisted dump registration.
 */
static unsigned long get_fadump_area_size(void)
{
	unsigned long size = 0;

	size += fw_dump.cpu_state_data_size;
	size += fw_dump.hpte_region_size;
	size += fw_dump.boot_memory_size;
	size += sizeof(struct fadump_crash_info_header);
	size += sizeof(struct elfhdr); /* ELF core header.*/
	size += sizeof(struct elf_phdr); /* place holder for cpu notes */
	/* Program headers for crash memory regions. */
	size += sizeof(struct elf_phdr) * (memblock_num_regions(memory) + 2);

	size = PAGE_ALIGN(size);
	return size;
}

int __init fadump_reserve_mem(void)
{
	unsigned long base, size, memory_boundary;

	if (!fw_dump.fadump_enabled)
		return 0;

	if (!fw_dump.fadump_supported) {
		printk(KERN_INFO "Firmware-assisted dump is not supported on"
				" this hardware\n");
		fw_dump.fadump_enabled = 0;
		return 0;
	}
	/*
	 * Initialize boot memory size
	 * If dump is active then we have already calculated the size during
	 * first kernel.
	 */
	if (fdm_active)
		fw_dump.boot_memory_size = be64_to_cpu(fdm_active->rmr_region.source_len);
	else
		fw_dump.boot_memory_size = fadump_calculate_reserve_size();

	/*
	 * Calculate the memory boundary.
	 * If memory_limit is less than actual memory boundary then reserve
	 * the memory for fadump beyond the memory_limit and adjust the
	 * memory_limit accordingly, so that the running kernel can run with
	 * specified memory_limit.
	 */
	if (memory_limit && memory_limit < memblock_end_of_DRAM()) {
		size = get_fadump_area_size();
		if ((memory_limit + size) < memblock_end_of_DRAM())
			memory_limit += size;
		else
			memory_limit = memblock_end_of_DRAM();
		printk(KERN_INFO "Adjusted memory_limit for firmware-assisted"
				" dump, now %#016llx\n", memory_limit);
	}
	if (memory_limit)
		memory_boundary = memory_limit;
	else
		memory_boundary = memblock_end_of_DRAM();

	if (fw_dump.dump_active) {
		printk(KERN_INFO "Firmware-assisted dump is active.\n");
		/*
		 * If last boot has crashed then reserve all the memory
		 * above boot_memory_size so that we don't touch it until
		 * dump is written to disk by userspace tool. This memory
		 * will be released for general use once the dump is saved.
		 */
		base = fw_dump.boot_memory_size;
		size = memory_boundary - base;
		memblock_reserve(base, size);
		printk(KERN_INFO "Reserved %ldMB of memory at %ldMB "
				"for saving crash dump\n",
				(unsigned long)(size >> 20),
				(unsigned long)(base >> 20));

		fw_dump.fadumphdr_addr =
				be64_to_cpu(fdm_active->rmr_region.destination_address) +
				be64_to_cpu(fdm_active->rmr_region.source_len);
		pr_debug("fadumphdr_addr = %p\n",
				(void *) fw_dump.fadumphdr_addr);
	} else {
		size = get_fadump_area_size();

		/*
		 * Reserve memory at an offset closer to bottom of the RAM to
		 * minimize the impact of memory hot-remove operation. We can't
		 * use memblock_find_in_range() here since it doesn't allocate
		 * from bottom to top.
		 */
		for (base = fw_dump.boot_memory_size;
		     base <= (memory_boundary - size);
		     base += size) {
			if (memblock_is_region_memory(base, size) &&
			    !memblock_is_region_reserved(base, size))
				break;
		}
		if ((base > (memory_boundary - size)) ||
		    memblock_reserve(base, size)) {
			pr_err("Failed to reserve memory\n");
			return 0;
		}

		pr_info("Reserved %ldMB of memory at %ldMB for firmware-"
			"assisted dump (System RAM: %ldMB)\n",
			(unsigned long)(size >> 20),
			(unsigned long)(base >> 20),
			(unsigned long)(memblock_phys_mem_size() >> 20));
	}

	fw_dump.reserve_dump_area_start = base;
	fw_dump.reserve_dump_area_size = size;
	return 1;
}

unsigned long __init arch_reserved_kernel_pages(void)
{
	return memblock_reserved_size() / PAGE_SIZE;
}

/* Look for fadump= cmdline option. */
static int __init early_fadump_param(char *p)
{
	if (!p)
		return 1;

	if (strncmp(p, "on", 2) == 0)
		fw_dump.fadump_enabled = 1;
	else if (strncmp(p, "off", 3) == 0)
		fw_dump.fadump_enabled = 0;

	return 0;
}
early_param("fadump", early_fadump_param);

static int register_fw_dump(struct fadump_mem_struct *fdm)
{
	int rc, err;
	unsigned int wait_time;

	pr_debug("Registering for firmware-assisted kernel dump...\n");

	/* TODO: Add upper time limit for the delay */
	do {
		rc = rtas_call(fw_dump.ibm_configure_kernel_dump, 3, 1, NULL,
			FADUMP_REGISTER, fdm,
			sizeof(struct fadump_mem_struct));

		wait_time = rtas_busy_delay_time(rc);
		if (wait_time)
			mdelay(wait_time);

	} while (wait_time);

	err = -EIO;
	switch (rc) {
	default:
		pr_err("Failed to register. Unknown Error(%d).\n", rc);
		break;
	case -1:
		printk(KERN_ERR "Failed to register firmware-assisted kernel"
			" dump. Hardware Error(%d).\n", rc);
		break;
	case -3:
		printk(KERN_ERR "Failed to register firmware-assisted kernel"
			" dump. Parameter Error(%d).\n", rc);
		err = -EINVAL;
		break;
	case -9:
		printk(KERN_ERR "firmware-assisted kernel dump is already "
			" registered.");
		fw_dump.dump_registered = 1;
		err = -EEXIST;
		break;
	case 0:
		printk(KERN_INFO "firmware-assisted kernel dump registration"
			" is successful\n");
		fw_dump.dump_registered = 1;
		err = 0;
		break;
	}
	return err;
}

void crash_fadump(struct pt_regs *regs, const char *str)
{
	struct fadump_crash_info_header *fdh = NULL;
	int old_cpu, this_cpu;

	if (!fw_dump.dump_registered || !fw_dump.fadumphdr_addr)
		return;

	/*
	 * old_cpu == -1 means this is the first CPU which has come here,
	 * go ahead and trigger fadump.
	 *
	 * old_cpu != -1 means some other CPU has already on it's way
	 * to trigger fadump, just keep looping here.
	 */
	this_cpu = smp_processor_id();
	old_cpu = cmpxchg(&crashing_cpu, -1, this_cpu);

	if (old_cpu != -1) {
		/*
		 * We can't loop here indefinitely. Wait as long as fadump
		 * is in force. If we race with fadump un-registration this
		 * loop will break and then we go down to normal panic path
		 * and reboot. If fadump is in force the first crashing
		 * cpu will definitely trigger fadump.
		 */
		while (fw_dump.dump_registered)
			cpu_relax();
		return;
	}

	fdh = __va(fw_dump.fadumphdr_addr);
	fdh->crashing_cpu = crashing_cpu;
	crash_save_vmcoreinfo();

	if (regs)
		fdh->regs = *regs;
	else
		ppc_save_regs(&fdh->regs);

	fdh->online_mask = *cpu_online_mask;

	/* Call ibm,os-term rtas call to trigger firmware assisted dump */
	rtas_os_term((char *)str);
}

#define GPR_MASK	0xffffff0000000000
static inline int fadump_gpr_index(u64 id)
{
	int i = -1;
	char str[3];

	if ((id & GPR_MASK) == REG_ID("GPR")) {
		/* get the digits at the end */
		id &= ~GPR_MASK;
		id >>= 24;
		str[2] = '\0';
		str[1] = id & 0xff;
		str[0] = (id >> 8) & 0xff;
		sscanf(str, "%d", &i);
		if (i > 31)
			i = -1;
	}
	return i;
}

static inline void fadump_set_regval(struct pt_regs *regs, u64 reg_id,
								u64 reg_val)
{
	int i;

	i = fadump_gpr_index(reg_id);
	if (i >= 0)
		regs->gpr[i] = (unsigned long)reg_val;
	else if (reg_id == REG_ID("NIA"))
		regs->nip = (unsigned long)reg_val;
	else if (reg_id == REG_ID("MSR"))
		regs->msr = (unsigned long)reg_val;
	else if (reg_id == REG_ID("CTR"))
		regs->ctr = (unsigned long)reg_val;
	else if (reg_id == REG_ID("LR"))
		regs->link = (unsigned long)reg_val;
	else if (reg_id == REG_ID("XER"))
		regs->xer = (unsigned long)reg_val;
	else if (reg_id == REG_ID("CR"))
		regs->ccr = (unsigned long)reg_val;
	else if (reg_id == REG_ID("DAR"))
		regs->dar = (unsigned long)reg_val;
	else if (reg_id == REG_ID("DSISR"))
		regs->dsisr = (unsigned long)reg_val;
}

static struct fadump_reg_entry*
fadump_read_registers(struct fadump_reg_entry *reg_entry, struct pt_regs *regs)
{
	memset(regs, 0, sizeof(struct pt_regs));

	while (be64_to_cpu(reg_entry->reg_id) != REG_ID("CPUEND")) {
		fadump_set_regval(regs, be64_to_cpu(reg_entry->reg_id),
					be64_to_cpu(reg_entry->reg_value));
		reg_entry++;
	}
	reg_entry++;
	return reg_entry;
}

static u32 *fadump_regs_to_elf_notes(u32 *buf, struct pt_regs *regs)
{
	struct elf_prstatus prstatus;

	memset(&prstatus, 0, sizeof(prstatus));
	/*
	 * FIXME: How do i get PID? Do I really need it?
	 * prstatus.pr_pid = ????
	 */
	elf_core_copy_kernel_regs(&prstatus.pr_reg, regs);
	buf = append_elf_note(buf, CRASH_CORE_NOTE_NAME, NT_PRSTATUS,
			      &prstatus, sizeof(prstatus));
	return buf;
}

static void fadump_update_elfcore_header(char *bufp)
{
	struct elfhdr *elf;
	struct elf_phdr *phdr;

	elf = (struct elfhdr *)bufp;
	bufp += sizeof(struct elfhdr);

	/* First note is a place holder for cpu notes info. */
	phdr = (struct elf_phdr *)bufp;

	if (phdr->p_type == PT_NOTE) {
		phdr->p_paddr = fw_dump.cpu_notes_buf;
		phdr->p_offset	= phdr->p_paddr;
		phdr->p_filesz	= fw_dump.cpu_notes_buf_size;
		phdr->p_memsz = fw_dump.cpu_notes_buf_size;
	}
	return;
}

static void *fadump_cpu_notes_buf_alloc(unsigned long size)
{
	void *vaddr;
	struct page *page;
	unsigned long order, count, i;

	order = get_order(size);
	vaddr = (void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, order);
	if (!vaddr)
		return NULL;

	count = 1 << order;
	page = virt_to_page(vaddr);
	for (i = 0; i < count; i++)
		SetPageReserved(page + i);
	return vaddr;
}

static void fadump_cpu_notes_buf_free(unsigned long vaddr, unsigned long size)
{
	struct page *page;
	unsigned long order, count, i;

	order = get_order(size);
	count = 1 << order;
	page = virt_to_page(vaddr);
	for (i = 0; i < count; i++)
		ClearPageReserved(page + i);
	__free_pages(page, order);
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
static int __init fadump_build_cpu_notes(const struct fadump_mem_struct *fdm)
{
	struct fadump_reg_save_area_header *reg_header;
	struct fadump_reg_entry *reg_entry;
	struct fadump_crash_info_header *fdh = NULL;
	void *vaddr;
	unsigned long addr;
	u32 num_cpus, *note_buf;
	struct pt_regs regs;
	int i, rc = 0, cpu = 0;

	if (!fdm->cpu_state_data.bytes_dumped)
		return -EINVAL;

	addr = be64_to_cpu(fdm->cpu_state_data.destination_address);
	vaddr = __va(addr);

	reg_header = vaddr;
	if (be64_to_cpu(reg_header->magic_number) != REGSAVE_AREA_MAGIC) {
		printk(KERN_ERR "Unable to read register save area.\n");
		return -ENOENT;
	}
	pr_debug("--------CPU State Data------------\n");
	pr_debug("Magic Number: %llx\n", be64_to_cpu(reg_header->magic_number));
	pr_debug("NumCpuOffset: %x\n", be32_to_cpu(reg_header->num_cpu_offset));

	vaddr += be32_to_cpu(reg_header->num_cpu_offset);
	num_cpus = be32_to_cpu(*((__be32 *)(vaddr)));
	pr_debug("NumCpus     : %u\n", num_cpus);
	vaddr += sizeof(u32);
	reg_entry = (struct fadump_reg_entry *)vaddr;

	/* Allocate buffer to hold cpu crash notes. */
	fw_dump.cpu_notes_buf_size = num_cpus * sizeof(note_buf_t);
	fw_dump.cpu_notes_buf_size = PAGE_ALIGN(fw_dump.cpu_notes_buf_size);
	note_buf = fadump_cpu_notes_buf_alloc(fw_dump.cpu_notes_buf_size);
	if (!note_buf) {
		printk(KERN_ERR "Failed to allocate 0x%lx bytes for "
			"cpu notes buffer\n", fw_dump.cpu_notes_buf_size);
		return -ENOMEM;
	}
	fw_dump.cpu_notes_buf = __pa(note_buf);

	pr_debug("Allocated buffer for cpu notes of size %ld at %p\n",
			(num_cpus * sizeof(note_buf_t)), note_buf);

	if (fw_dump.fadumphdr_addr)
		fdh = __va(fw_dump.fadumphdr_addr);

	for (i = 0; i < num_cpus; i++) {
		if (be64_to_cpu(reg_entry->reg_id) != REG_ID("CPUSTRT")) {
			printk(KERN_ERR "Unable to read CPU state data\n");
			rc = -ENOENT;
			goto error_out;
		}
		/* Lower 4 bytes of reg_value contains logical cpu id */
		cpu = be64_to_cpu(reg_entry->reg_value) & FADUMP_CPU_ID_MASK;
		if (fdh && !cpumask_test_cpu(cpu, &fdh->online_mask)) {
			SKIP_TO_NEXT_CPU(reg_entry);
			continue;
		}
		pr_debug("Reading register data for cpu %d...\n", cpu);
		if (fdh && fdh->crashing_cpu == cpu) {
			regs = fdh->regs;
			note_buf = fadump_regs_to_elf_notes(note_buf, &regs);
			SKIP_TO_NEXT_CPU(reg_entry);
		} else {
			reg_entry++;
			reg_entry = fadump_read_registers(reg_entry, &regs);
			note_buf = fadump_regs_to_elf_notes(note_buf, &regs);
		}
	}
	final_note(note_buf);

	if (fdh) {
		pr_debug("Updating elfcore header (%llx) with cpu notes\n",
							fdh->elfcorehdr_addr);
		fadump_update_elfcore_header((char *)__va(fdh->elfcorehdr_addr));
	}
	return 0;

error_out:
	fadump_cpu_notes_buf_free((unsigned long)__va(fw_dump.cpu_notes_buf),
					fw_dump.cpu_notes_buf_size);
	fw_dump.cpu_notes_buf = 0;
	fw_dump.cpu_notes_buf_size = 0;
	return rc;

}

/*
 * Validate and process the dump data stored by firmware before exporting
 * it through '/proc/vmcore'.
 */
static int __init process_fadump(const struct fadump_mem_struct *fdm_active)
{
	struct fadump_crash_info_header *fdh;
	int rc = 0;

	if (!fdm_active || !fw_dump.fadumphdr_addr)
		return -EINVAL;

	/* Check if the dump data is valid. */
	if ((be16_to_cpu(fdm_active->header.dump_status_flag) == FADUMP_ERROR_FLAG) ||
			(fdm_active->cpu_state_data.error_flags != 0) ||
			(fdm_active->rmr_region.error_flags != 0)) {
		printk(KERN_ERR "Dump taken by platform is not valid\n");
		return -EINVAL;
	}
	if ((fdm_active->rmr_region.bytes_dumped !=
			fdm_active->rmr_region.source_len) ||
			!fdm_active->cpu_state_data.bytes_dumped) {
		printk(KERN_ERR "Dump taken by platform is incomplete\n");
		return -EINVAL;
	}

	/* Validate the fadump crash info header */
	fdh = __va(fw_dump.fadumphdr_addr);
	if (fdh->magic_number != FADUMP_CRASH_INFO_MAGIC) {
		printk(KERN_ERR "Crash info header is not valid.\n");
		return -EINVAL;
	}

	rc = fadump_build_cpu_notes(fdm_active);
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

static inline void fadump_add_crash_memory(unsigned long long base,
					unsigned long long end)
{
	if (base == end)
		return;

	pr_debug("crash_memory_range[%d] [%#016llx-%#016llx], %#llx bytes\n",
		crash_mem_ranges, base, end - 1, (end - base));
	crash_memory_ranges[crash_mem_ranges].base = base;
	crash_memory_ranges[crash_mem_ranges].size = end - base;
	crash_mem_ranges++;
}

static void fadump_exclude_reserved_area(unsigned long long start,
					unsigned long long end)
{
	unsigned long long ra_start, ra_end;

	ra_start = fw_dump.reserve_dump_area_start;
	ra_end = ra_start + fw_dump.reserve_dump_area_size;

	if ((ra_start < end) && (ra_end > start)) {
		if ((start < ra_start) && (end > ra_end)) {
			fadump_add_crash_memory(start, ra_start);
			fadump_add_crash_memory(ra_end, end);
		} else if (start < ra_start) {
			fadump_add_crash_memory(start, ra_start);
		} else if (ra_end < end) {
			fadump_add_crash_memory(ra_end, end);
		}
	} else
		fadump_add_crash_memory(start, end);
}

static int fadump_init_elfcore_header(char *bufp)
{
	struct elfhdr *elf;

	elf = (struct elfhdr *) bufp;
	bufp += sizeof(struct elfhdr);
	memcpy(elf->e_ident, ELFMAG, SELFMAG);
	elf->e_ident[EI_CLASS] = ELF_CLASS;
	elf->e_ident[EI_DATA] = ELF_DATA;
	elf->e_ident[EI_VERSION] = EV_CURRENT;
	elf->e_ident[EI_OSABI] = ELF_OSABI;
	memset(elf->e_ident+EI_PAD, 0, EI_NIDENT-EI_PAD);
	elf->e_type = ET_CORE;
	elf->e_machine = ELF_ARCH;
	elf->e_version = EV_CURRENT;
	elf->e_entry = 0;
	elf->e_phoff = sizeof(struct elfhdr);
	elf->e_shoff = 0;
#if defined(_CALL_ELF)
	elf->e_flags = _CALL_ELF;
#else
	elf->e_flags = 0;
#endif
	elf->e_ehsize = sizeof(struct elfhdr);
	elf->e_phentsize = sizeof(struct elf_phdr);
	elf->e_phnum = 0;
	elf->e_shentsize = 0;
	elf->e_shnum = 0;
	elf->e_shstrndx = 0;

	return 0;
}

/*
 * Traverse through memblock structure and setup crash memory ranges. These
 * ranges will be used create PT_LOAD program headers in elfcore header.
 */
static void fadump_setup_crash_memory_ranges(void)
{
	struct memblock_region *reg;
	unsigned long long start, end;

	pr_debug("Setup crash memory ranges.\n");
	crash_mem_ranges = 0;
	/*
	 * add the first memory chunk (RMA_START through boot_memory_size) as
	 * a separate memory chunk. The reason is, at the time crash firmware
	 * will move the content of this memory chunk to different location
	 * specified during fadump registration. We need to create a separate
	 * program header for this chunk with the correct offset.
	 */
	fadump_add_crash_memory(RMA_START, fw_dump.boot_memory_size);

	for_each_memblock(memory, reg) {
		start = (unsigned long long)reg->base;
		end = start + (unsigned long long)reg->size;
		if (start == RMA_START && end >= fw_dump.boot_memory_size)
			start = fw_dump.boot_memory_size;

		/* add this range excluding the reserved dump area. */
		fadump_exclude_reserved_area(start, end);
	}
}

/*
 * If the given physical address falls within the boot memory region then
 * return the relocated address that points to the dump region reserved
 * for saving initial boot memory contents.
 */
static inline unsigned long fadump_relocate(unsigned long paddr)
{
	if (paddr > RMA_START && paddr < fw_dump.boot_memory_size)
		return be64_to_cpu(fdm.rmr_region.destination_address) + paddr;
	else
		return paddr;
}

static int fadump_create_elfcore_headers(char *bufp)
{
	struct elfhdr *elf;
	struct elf_phdr *phdr;
	int i;

	fadump_init_elfcore_header(bufp);
	elf = (struct elfhdr *)bufp;
	bufp += sizeof(struct elfhdr);

	/*
	 * setup ELF PT_NOTE, place holder for cpu notes info. The notes info
	 * will be populated during second kernel boot after crash. Hence
	 * this PT_NOTE will always be the first elf note.
	 *
	 * NOTE: Any new ELF note addition should be placed after this note.
	 */
	phdr = (struct elf_phdr *)bufp;
	bufp += sizeof(struct elf_phdr);
	phdr->p_type = PT_NOTE;
	phdr->p_flags = 0;
	phdr->p_vaddr = 0;
	phdr->p_align = 0;

	phdr->p_offset = 0;
	phdr->p_paddr = 0;
	phdr->p_filesz = 0;
	phdr->p_memsz = 0;

	(elf->e_phnum)++;

	/* setup ELF PT_NOTE for vmcoreinfo */
	phdr = (struct elf_phdr *)bufp;
	bufp += sizeof(struct elf_phdr);
	phdr->p_type	= PT_NOTE;
	phdr->p_flags	= 0;
	phdr->p_vaddr	= 0;
	phdr->p_align	= 0;

	phdr->p_paddr	= fadump_relocate(paddr_vmcoreinfo_note());
	phdr->p_offset	= phdr->p_paddr;
	phdr->p_memsz	= vmcoreinfo_max_size;
	phdr->p_filesz	= vmcoreinfo_max_size;

	/* Increment number of program headers. */
	(elf->e_phnum)++;

	/* setup PT_LOAD sections. */

	for (i = 0; i < crash_mem_ranges; i++) {
		unsigned long long mbase, msize;
		mbase = crash_memory_ranges[i].base;
		msize = crash_memory_ranges[i].size;

		if (!msize)
			continue;

		phdr = (struct elf_phdr *)bufp;
		bufp += sizeof(struct elf_phdr);
		phdr->p_type	= PT_LOAD;
		phdr->p_flags	= PF_R|PF_W|PF_X;
		phdr->p_offset	= mbase;

		if (mbase == RMA_START) {
			/*
			 * The entire RMA region will be moved by firmware
			 * to the specified destination_address. Hence set
			 * the correct offset.
			 */
			phdr->p_offset = be64_to_cpu(fdm.rmr_region.destination_address);
		}

		phdr->p_paddr = mbase;
		phdr->p_vaddr = (unsigned long)__va(mbase);
		phdr->p_filesz = msize;
		phdr->p_memsz = msize;
		phdr->p_align = 0;

		/* Increment number of program headers. */
		(elf->e_phnum)++;
	}
	return 0;
}

static unsigned long init_fadump_header(unsigned long addr)
{
	struct fadump_crash_info_header *fdh;

	if (!addr)
		return 0;

	fw_dump.fadumphdr_addr = addr;
	fdh = __va(addr);
	addr += sizeof(struct fadump_crash_info_header);

	memset(fdh, 0, sizeof(struct fadump_crash_info_header));
	fdh->magic_number = FADUMP_CRASH_INFO_MAGIC;
	fdh->elfcorehdr_addr = addr;
	/* We will set the crashing cpu id in crash_fadump() during crash. */
	fdh->crashing_cpu = CPU_UNKNOWN;

	return addr;
}

static int register_fadump(void)
{
	unsigned long addr;
	void *vaddr;

	/*
	 * If no memory is reserved then we can not register for firmware-
	 * assisted dump.
	 */
	if (!fw_dump.reserve_dump_area_size)
		return -ENODEV;

	fadump_setup_crash_memory_ranges();

	addr = be64_to_cpu(fdm.rmr_region.destination_address) + be64_to_cpu(fdm.rmr_region.source_len);
	/* Initialize fadump crash info header. */
	addr = init_fadump_header(addr);
	vaddr = __va(addr);

	pr_debug("Creating ELF core headers at %#016lx\n", addr);
	fadump_create_elfcore_headers(vaddr);

	/* register the future kernel dump with firmware. */
	return register_fw_dump(&fdm);
}

static int fadump_unregister_dump(struct fadump_mem_struct *fdm)
{
	int rc = 0;
	unsigned int wait_time;

	pr_debug("Un-register firmware-assisted dump\n");

	/* TODO: Add upper time limit for the delay */
	do {
		rc = rtas_call(fw_dump.ibm_configure_kernel_dump, 3, 1, NULL,
			FADUMP_UNREGISTER, fdm,
			sizeof(struct fadump_mem_struct));

		wait_time = rtas_busy_delay_time(rc);
		if (wait_time)
			mdelay(wait_time);
	} while (wait_time);

	if (rc) {
		printk(KERN_ERR "Failed to un-register firmware-assisted dump."
			" unexpected error(%d).\n", rc);
		return rc;
	}
	fw_dump.dump_registered = 0;
	return 0;
}

static int fadump_invalidate_dump(struct fadump_mem_struct *fdm)
{
	int rc = 0;
	unsigned int wait_time;

	pr_debug("Invalidating firmware-assisted dump registration\n");

	/* TODO: Add upper time limit for the delay */
	do {
		rc = rtas_call(fw_dump.ibm_configure_kernel_dump, 3, 1, NULL,
			FADUMP_INVALIDATE, fdm,
			sizeof(struct fadump_mem_struct));

		wait_time = rtas_busy_delay_time(rc);
		if (wait_time)
			mdelay(wait_time);
	} while (wait_time);

	if (rc) {
		pr_err("Failed to invalidate firmware-assisted dump registration. Unexpected error (%d).\n", rc);
		return rc;
	}
	fw_dump.dump_active = 0;
	fdm_active = NULL;
	return 0;
}

void fadump_cleanup(void)
{
	/* Invalidate the registration only if dump is active. */
	if (fw_dump.dump_active) {
		init_fadump_mem_struct(&fdm,
			be64_to_cpu(fdm_active->cpu_state_data.destination_address));
		fadump_invalidate_dump(&fdm);
	}
}

/*
 * Release the memory that was reserved in early boot to preserve the memory
 * contents. The released memory will be available for general use.
 */
static void fadump_release_memory(unsigned long begin, unsigned long end)
{
	unsigned long addr;
	unsigned long ra_start, ra_end;

	ra_start = fw_dump.reserve_dump_area_start;
	ra_end = ra_start + fw_dump.reserve_dump_area_size;

	for (addr = begin; addr < end; addr += PAGE_SIZE) {
		/*
		 * exclude the dump reserve area. Will reuse it for next
		 * fadump registration.
		 */
		if (addr <= ra_end && ((addr + PAGE_SIZE) > ra_start))
			continue;

		free_reserved_page(pfn_to_page(addr >> PAGE_SHIFT));
	}
}

static void fadump_invalidate_release_mem(void)
{
	unsigned long reserved_area_start, reserved_area_end;
	unsigned long destination_address;

	mutex_lock(&fadump_mutex);
	if (!fw_dump.dump_active) {
		mutex_unlock(&fadump_mutex);
		return;
	}

	destination_address = be64_to_cpu(fdm_active->cpu_state_data.destination_address);
	fadump_cleanup();
	mutex_unlock(&fadump_mutex);

	/*
	 * Save the current reserved memory bounds we will require them
	 * later for releasing the memory for general use.
	 */
	reserved_area_start = fw_dump.reserve_dump_area_start;
	reserved_area_end = reserved_area_start +
			fw_dump.reserve_dump_area_size;
	/*
	 * Setup reserve_dump_area_start and its size so that we can
	 * reuse this reserved memory for Re-registration.
	 */
	fw_dump.reserve_dump_area_start = destination_address;
	fw_dump.reserve_dump_area_size = get_fadump_area_size();

	fadump_release_memory(reserved_area_start, reserved_area_end);
	if (fw_dump.cpu_notes_buf) {
		fadump_cpu_notes_buf_free(
				(unsigned long)__va(fw_dump.cpu_notes_buf),
				fw_dump.cpu_notes_buf_size);
		fw_dump.cpu_notes_buf = 0;
		fw_dump.cpu_notes_buf_size = 0;
	}
	/* Initialize the kernel dump memory structure for FAD registration. */
	init_fadump_mem_struct(&fdm, fw_dump.reserve_dump_area_start);
}

static ssize_t fadump_release_memory_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	if (!fw_dump.dump_active)
		return -EPERM;

	if (buf[0] == '1') {
		/*
		 * Take away the '/proc/vmcore'. We are releasing the dump
		 * memory, hence it will not be valid anymore.
		 */
#ifdef CONFIG_PROC_VMCORE
		vmcore_cleanup();
#endif
		fadump_invalidate_release_mem();

	} else
		return -EINVAL;
	return count;
}

static ssize_t fadump_enabled_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", fw_dump.fadump_enabled);
}

static ssize_t fadump_register_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", fw_dump.dump_registered);
}

static ssize_t fadump_register_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	int ret = 0;

	if (!fw_dump.fadump_enabled || fdm_active)
		return -EPERM;

	mutex_lock(&fadump_mutex);

	switch (buf[0]) {
	case '0':
		if (fw_dump.dump_registered == 0) {
			goto unlock_out;
		}
		/* Un-register Firmware-assisted dump */
		fadump_unregister_dump(&fdm);
		break;
	case '1':
		if (fw_dump.dump_registered == 1) {
			ret = -EEXIST;
			goto unlock_out;
		}
		/* Register Firmware-assisted dump */
		ret = register_fadump();
		break;
	default:
		ret = -EINVAL;
		break;
	}

unlock_out:
	mutex_unlock(&fadump_mutex);
	return ret < 0 ? ret : count;
}

static int fadump_region_show(struct seq_file *m, void *private)
{
	const struct fadump_mem_struct *fdm_ptr;

	if (!fw_dump.fadump_enabled)
		return 0;

	mutex_lock(&fadump_mutex);
	if (fdm_active)
		fdm_ptr = fdm_active;
	else {
		mutex_unlock(&fadump_mutex);
		fdm_ptr = &fdm;
	}

	seq_printf(m,
			"CPU : [%#016llx-%#016llx] %#llx bytes, "
			"Dumped: %#llx\n",
			be64_to_cpu(fdm_ptr->cpu_state_data.destination_address),
			be64_to_cpu(fdm_ptr->cpu_state_data.destination_address) +
			be64_to_cpu(fdm_ptr->cpu_state_data.source_len) - 1,
			be64_to_cpu(fdm_ptr->cpu_state_data.source_len),
			be64_to_cpu(fdm_ptr->cpu_state_data.bytes_dumped));
	seq_printf(m,
			"HPTE: [%#016llx-%#016llx] %#llx bytes, "
			"Dumped: %#llx\n",
			be64_to_cpu(fdm_ptr->hpte_region.destination_address),
			be64_to_cpu(fdm_ptr->hpte_region.destination_address) +
			be64_to_cpu(fdm_ptr->hpte_region.source_len) - 1,
			be64_to_cpu(fdm_ptr->hpte_region.source_len),
			be64_to_cpu(fdm_ptr->hpte_region.bytes_dumped));
	seq_printf(m,
			"DUMP: [%#016llx-%#016llx] %#llx bytes, "
			"Dumped: %#llx\n",
			be64_to_cpu(fdm_ptr->rmr_region.destination_address),
			be64_to_cpu(fdm_ptr->rmr_region.destination_address) +
			be64_to_cpu(fdm_ptr->rmr_region.source_len) - 1,
			be64_to_cpu(fdm_ptr->rmr_region.source_len),
			be64_to_cpu(fdm_ptr->rmr_region.bytes_dumped));

	if (!fdm_active ||
		(fw_dump.reserve_dump_area_start ==
		be64_to_cpu(fdm_ptr->cpu_state_data.destination_address)))
		goto out;

	/* Dump is active. Show reserved memory region. */
	seq_printf(m,
			"    : [%#016llx-%#016llx] %#llx bytes, "
			"Dumped: %#llx\n",
			(unsigned long long)fw_dump.reserve_dump_area_start,
			be64_to_cpu(fdm_ptr->cpu_state_data.destination_address) - 1,
			be64_to_cpu(fdm_ptr->cpu_state_data.destination_address) -
			fw_dump.reserve_dump_area_start,
			be64_to_cpu(fdm_ptr->cpu_state_data.destination_address) -
			fw_dump.reserve_dump_area_start);
out:
	if (fdm_active)
		mutex_unlock(&fadump_mutex);
	return 0;
}

static struct kobj_attribute fadump_release_attr = __ATTR(fadump_release_mem,
						0200, NULL,
						fadump_release_memory_store);
static struct kobj_attribute fadump_attr = __ATTR(fadump_enabled,
						0444, fadump_enabled_show,
						NULL);
static struct kobj_attribute fadump_register_attr = __ATTR(fadump_registered,
						0644, fadump_register_show,
						fadump_register_store);

static int fadump_region_open(struct inode *inode, struct file *file)
{
	return single_open(file, fadump_region_show, inode->i_private);
}

static const struct file_operations fadump_region_fops = {
	.open    = fadump_region_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

static void fadump_init_files(void)
{
	struct dentry *debugfs_file;
	int rc = 0;

	rc = sysfs_create_file(kernel_kobj, &fadump_attr.attr);
	if (rc)
		printk(KERN_ERR "fadump: unable to create sysfs file"
			" fadump_enabled (%d)\n", rc);

	rc = sysfs_create_file(kernel_kobj, &fadump_register_attr.attr);
	if (rc)
		printk(KERN_ERR "fadump: unable to create sysfs file"
			" fadump_registered (%d)\n", rc);

	debugfs_file = debugfs_create_file("fadump_region", 0444,
					powerpc_debugfs_root, NULL,
					&fadump_region_fops);
	if (!debugfs_file)
		printk(KERN_ERR "fadump: unable to create debugfs file"
				" fadump_region\n");

	if (fw_dump.dump_active) {
		rc = sysfs_create_file(kernel_kobj, &fadump_release_attr.attr);
		if (rc)
			printk(KERN_ERR "fadump: unable to create sysfs file"
				" fadump_release_mem (%d)\n", rc);
	}
	return;
}

/*
 * Prepare for firmware-assisted dump.
 */
int __init setup_fadump(void)
{
	if (!fw_dump.fadump_enabled)
		return 0;

	if (!fw_dump.fadump_supported) {
		printk(KERN_ERR "Firmware-assisted dump is not supported on"
			" this hardware\n");
		return 0;
	}

	fadump_show_config();
	/*
	 * If dump data is available then see if it is valid and prepare for
	 * saving it to the disk.
	 */
	if (fw_dump.dump_active) {
		/*
		 * if dump process fails then invalidate the registration
		 * and release memory before proceeding for re-registration.
		 */
		if (process_fadump(fdm_active) < 0)
			fadump_invalidate_release_mem();
	}
	/* Initialize the kernel dump memory structure for FAD registration. */
	else if (fw_dump.reserve_dump_area_size)
		init_fadump_mem_struct(&fdm, fw_dump.reserve_dump_area_start);
	fadump_init_files();

	return 1;
}
subsys_initcall(setup_fadump);
