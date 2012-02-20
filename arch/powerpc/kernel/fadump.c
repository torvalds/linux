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
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/page.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/fadump.h>

static struct fw_dump fw_dump;
static struct fadump_mem_struct fdm;
static const struct fadump_mem_struct *fdm_active;

static DEFINE_MUTEX(fadump_mutex);

/* Scan the Firmware Assisted dump configuration details. */
int __init early_init_dt_scan_fw_dump(unsigned long node,
			const char *uname, int depth, void *data)
{
	__be32 *sections;
	int i, num_sections;
	unsigned long size;
	const int *token;

	if (depth != 1 || strcmp(uname, "rtas") != 0)
		return 0;

	/*
	 * Check if Firmware Assisted dump is supported. if yes, check
	 * if dump has been initiated on last reboot.
	 */
	token = of_get_flat_dt_prop(node, "ibm,configure-kernel-dump", NULL);
	if (!token)
		return 0;

	fw_dump.fadump_supported = 1;
	fw_dump.ibm_configure_kernel_dump = *token;

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
		return 0;

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

	fdm->header.dump_format_version = 0x00000001;
	fdm->header.dump_num_sections = 3;
	fdm->header.dump_status_flag = 0;
	fdm->header.offset_first_dump_section =
		(u32)offsetof(struct fadump_mem_struct, cpu_state_data);

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
	fdm->cpu_state_data.request_flag = FADUMP_REQUEST_FLAG;
	fdm->cpu_state_data.source_data_type = FADUMP_CPU_STATE_DATA;
	fdm->cpu_state_data.source_address = 0;
	fdm->cpu_state_data.source_len = fw_dump.cpu_state_data_size;
	fdm->cpu_state_data.destination_address = addr;
	addr += fw_dump.cpu_state_data_size;

	/* hpte region section */
	fdm->hpte_region.request_flag = FADUMP_REQUEST_FLAG;
	fdm->hpte_region.source_data_type = FADUMP_HPTE_REGION;
	fdm->hpte_region.source_address = 0;
	fdm->hpte_region.source_len = fw_dump.hpte_region_size;
	fdm->hpte_region.destination_address = addr;
	addr += fw_dump.hpte_region_size;

	/* RMA region section */
	fdm->rmr_region.request_flag = FADUMP_REQUEST_FLAG;
	fdm->rmr_region.source_data_type = FADUMP_REAL_MODE_REGION;
	fdm->rmr_region.source_address = RMA_START;
	fdm->rmr_region.source_len = fw_dump.boot_memory_size;
	fdm->rmr_region.destination_address = addr;
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
	unsigned long size;

	/*
	 * Check if the size is specified through fadump_reserve_mem= cmdline
	 * option. If yes, then use that.
	 */
	if (fw_dump.reserve_bootvar)
		return fw_dump.reserve_bootvar;

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
		fw_dump.boot_memory_size = fdm_active->rmr_region.source_len;
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
				" dump, now %#016llx\n",
				(unsigned long long)memory_limit);
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
	} else {
		/* Reserve the memory at the top of memory. */
		size = get_fadump_area_size();
		base = memory_boundary - size;
		memblock_reserve(base, size);
		printk(KERN_INFO "Reserved %ldMB of memory at %ldMB "
				"for firmware-assisted dump\n",
				(unsigned long)(size >> 20),
				(unsigned long)(base >> 20));
	}
	fw_dump.reserve_dump_area_start = base;
	fw_dump.reserve_dump_area_size = size;
	return 1;
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

/* Look for fadump_reserve_mem= cmdline option */
static int __init early_fadump_reserve_mem(char *p)
{
	if (p)
		fw_dump.reserve_bootvar = memparse(p, &p);
	return 0;
}
early_param("fadump_reserve_mem", early_fadump_reserve_mem);

static void register_fw_dump(struct fadump_mem_struct *fdm)
{
	int rc;
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

	switch (rc) {
	case -1:
		printk(KERN_ERR "Failed to register firmware-assisted kernel"
			" dump. Hardware Error(%d).\n", rc);
		break;
	case -3:
		printk(KERN_ERR "Failed to register firmware-assisted kernel"
			" dump. Parameter Error(%d).\n", rc);
		break;
	case -9:
		printk(KERN_ERR "firmware-assisted kernel dump is already "
			" registered.");
		fw_dump.dump_registered = 1;
		break;
	case 0:
		printk(KERN_INFO "firmware-assisted kernel dump registration"
			" is successful\n");
		fw_dump.dump_registered = 1;
		break;
	}
}

static void register_fadump(void)
{
	/*
	 * If no memory is reserved then we can not register for firmware-
	 * assisted dump.
	 */
	if (!fw_dump.reserve_dump_area_size)
		return;

	/* register the future kernel dump with firmware. */
	register_fw_dump(&fdm);
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
			ret = -EINVAL;
			goto unlock_out;
		}
		/* Un-register Firmware-assisted dump */
		fadump_unregister_dump(&fdm);
		break;
	case '1':
		if (fw_dump.dump_registered == 1) {
			ret = -EINVAL;
			goto unlock_out;
		}
		/* Register Firmware-assisted dump */
		register_fadump();
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

	if (fdm_active)
		fdm_ptr = fdm_active;
	else
		fdm_ptr = &fdm;

	seq_printf(m,
			"CPU : [%#016llx-%#016llx] %#llx bytes, "
			"Dumped: %#llx\n",
			fdm_ptr->cpu_state_data.destination_address,
			fdm_ptr->cpu_state_data.destination_address +
			fdm_ptr->cpu_state_data.source_len - 1,
			fdm_ptr->cpu_state_data.source_len,
			fdm_ptr->cpu_state_data.bytes_dumped);
	seq_printf(m,
			"HPTE: [%#016llx-%#016llx] %#llx bytes, "
			"Dumped: %#llx\n",
			fdm_ptr->hpte_region.destination_address,
			fdm_ptr->hpte_region.destination_address +
			fdm_ptr->hpte_region.source_len - 1,
			fdm_ptr->hpte_region.source_len,
			fdm_ptr->hpte_region.bytes_dumped);
	seq_printf(m,
			"DUMP: [%#016llx-%#016llx] %#llx bytes, "
			"Dumped: %#llx\n",
			fdm_ptr->rmr_region.destination_address,
			fdm_ptr->rmr_region.destination_address +
			fdm_ptr->rmr_region.source_len - 1,
			fdm_ptr->rmr_region.source_len,
			fdm_ptr->rmr_region.bytes_dumped);

	if (!fdm_active ||
		(fw_dump.reserve_dump_area_start ==
		fdm_ptr->cpu_state_data.destination_address))
		return 0;

	/* Dump is active. Show reserved memory region. */
	seq_printf(m,
			"    : [%#016llx-%#016llx] %#llx bytes, "
			"Dumped: %#llx\n",
			(unsigned long long)fw_dump.reserve_dump_area_start,
			fdm_ptr->cpu_state_data.destination_address - 1,
			fdm_ptr->cpu_state_data.destination_address -
			fw_dump.reserve_dump_area_start,
			fdm_ptr->cpu_state_data.destination_address -
			fw_dump.reserve_dump_area_start);
	return 0;
}

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
	/* Initialize the kernel dump memory structure for FAD registration. */
	if (fw_dump.reserve_dump_area_size)
		init_fadump_mem_struct(&fdm, fw_dump.reserve_dump_area_start);
	fadump_init_files();

	return 1;
}
subsys_initcall(setup_fadump);
