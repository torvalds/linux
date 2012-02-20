/*
 * Firmware Assisted dump header file.
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

#ifndef __PPC64_FA_DUMP_H__
#define __PPC64_FA_DUMP_H__

#ifdef CONFIG_FA_DUMP

/*
 * The RMA region will be saved for later dumping when kernel crashes.
 * RMA is Real Mode Area, the first block of logical memory address owned
 * by logical partition, containing the storage that may be accessed with
 * translate off.
 */
#define RMA_START	0x0
#define RMA_END		(ppc64_rma_size)

/*
 * On some Power systems where RMO is 128MB, it still requires minimum of
 * 256MB for kernel to boot successfully. When kdump infrastructure is
 * configured to save vmcore over network, we run into OOM issue while
 * loading modules related to network setup. Hence we need aditional 64M
 * of memory to avoid OOM issue.
 */
#define MIN_BOOT_MEM	(((RMA_END < (0x1UL << 28)) ? (0x1UL << 28) : RMA_END) \
			+ (0x1UL << 26))

/* Firmware provided dump sections */
#define FADUMP_CPU_STATE_DATA	0x0001
#define FADUMP_HPTE_REGION	0x0002
#define FADUMP_REAL_MODE_REGION	0x0011

/* Dump request flag */
#define FADUMP_REQUEST_FLAG	0x00000001

/* FAD commands */
#define FADUMP_REGISTER		1
#define FADUMP_UNREGISTER	2
#define FADUMP_INVALIDATE	3

/* Kernel Dump section info */
struct fadump_section {
	u32	request_flag;
	u16	source_data_type;
	u16	error_flags;
	u64	source_address;
	u64	source_len;
	u64	bytes_dumped;
	u64	destination_address;
};

/* ibm,configure-kernel-dump header. */
struct fadump_section_header {
	u32	dump_format_version;
	u16	dump_num_sections;
	u16	dump_status_flag;
	u32	offset_first_dump_section;

	/* Fields for disk dump option. */
	u32	dd_block_size;
	u64	dd_block_offset;
	u64	dd_num_blocks;
	u32	dd_offset_disk_path;

	/* Maximum time allowed to prevent an automatic dump-reboot. */
	u32	max_time_auto;
};

/*
 * Firmware Assisted dump memory structure. This structure is required for
 * registering future kernel dump with power firmware through rtas call.
 *
 * No disk dump option. Hence disk dump path string section is not included.
 */
struct fadump_mem_struct {
	struct fadump_section_header	header;

	/* Kernel dump sections */
	struct fadump_section		cpu_state_data;
	struct fadump_section		hpte_region;
	struct fadump_section		rmr_region;
};

/* Firmware-assisted dump configuration details. */
struct fw_dump {
	unsigned long	cpu_state_data_size;
	unsigned long	hpte_region_size;
	unsigned long	boot_memory_size;
	unsigned long	reserve_dump_area_start;
	unsigned long	reserve_dump_area_size;
	/* cmd line option during boot */
	unsigned long	reserve_bootvar;

	int		ibm_configure_kernel_dump;

	unsigned long	fadump_enabled:1;
	unsigned long	fadump_supported:1;
	unsigned long	dump_active:1;
	unsigned long	dump_registered:1;
};

extern int early_init_dt_scan_fw_dump(unsigned long node,
		const char *uname, int depth, void *data);
extern int fadump_reserve_mem(void);
extern int setup_fadump(void);
extern int is_fadump_active(void);
#else	/* CONFIG_FA_DUMP */
static inline int is_fadump_active(void) { return 0; }
#endif
#endif
