/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Firmware-Assisted Dump support on POWERVM platform.
 *
 * Copyright 2011, Mahesh Salgaonkar, IBM Corporation.
 * Copyright 2019, Hari Bathini, IBM Corporation.
 */

#ifndef _PSERIES_RTAS_FADUMP_H
#define _PSERIES_RTAS_FADUMP_H

/*
 * On some Power systems where RMO is 128MB, it still requires minimum of
 * 256MB for kernel to boot successfully. When kdump infrastructure is
 * configured to save vmcore over network, we run into OOM issue while
 * loading modules related to network setup. Hence we need additional 64M
 * of memory to avoid OOM issue.
 */
#define RTAS_FADUMP_MIN_BOOT_MEM	((0x1UL << 28) + (0x1UL << 26))

/* Firmware provided dump sections */
#define RTAS_FADUMP_CPU_STATE_DATA	0x0001
#define RTAS_FADUMP_HPTE_REGION		0x0002
#define RTAS_FADUMP_REAL_MODE_REGION	0x0011

/* Dump request flag */
#define RTAS_FADUMP_REQUEST_FLAG	0x00000001

/* Dump status flag */
#define RTAS_FADUMP_ERROR_FLAG		0x2000

/* Kernel Dump section info */
struct rtas_fadump_section {
	__be32	request_flag;
	__be16	source_data_type;
	__be16	error_flags;
	__be64	source_address;
	__be64	source_len;
	__be64	bytes_dumped;
	__be64	destination_address;
};

/* ibm,configure-kernel-dump header. */
struct rtas_fadump_section_header {
	__be32	dump_format_version;
	__be16	dump_num_sections;
	__be16	dump_status_flag;
	__be32	offset_first_dump_section;

	/* Fields for disk dump option. */
	__be32	dd_block_size;
	__be64	dd_block_offset;
	__be64	dd_num_blocks;
	__be32	dd_offset_disk_path;

	/* Maximum time allowed to prevent an automatic dump-reboot. */
	__be32	max_time_auto;
};

/*
 * Firmware Assisted dump memory structure. This structure is required for
 * registering future kernel dump with power firmware through rtas call.
 *
 * No disk dump option. Hence disk dump path string section is not included.
 */
struct rtas_fadump_mem_struct {
	struct rtas_fadump_section_header	header;

	/* Kernel dump sections */
	struct rtas_fadump_section		cpu_state_data;
	struct rtas_fadump_section		hpte_region;

	/*
	 * TODO: Extend multiple boot memory regions support in the kernel
	 *       for this platform.
	 */
	struct rtas_fadump_section		rmr_region;
};

/*
 * The firmware-assisted dump format.
 *
 * The register save area is an area in the partition's memory used to preserve
 * the register contents (CPU state data) for the active CPUs during a firmware
 * assisted dump. The dump format contains register save area header followed
 * by register entries. Each list of registers for a CPU starts with "CPUSTRT"
 * and ends with "CPUEND".
 */

/* Register save area header. */
struct rtas_fadump_reg_save_area_header {
	__be64		magic_number;
	__be32		version;
	__be32		num_cpu_offset;
};

/* Register entry. */
struct rtas_fadump_reg_entry {
	__be64		reg_id;
	__be64		reg_value;
};

/* Utility macros */
#define RTAS_FADUMP_SKIP_TO_NEXT_CPU(reg_entry)				\
({									\
	while (be64_to_cpu(reg_entry->reg_id) !=			\
	       fadump_str_to_u64("CPUEND"))				\
		reg_entry++;						\
	reg_entry++;							\
})

#define RTAS_FADUMP_CPU_ID_MASK			((1UL << 32) - 1)

#endif /* _PSERIES_RTAS_FADUMP_H */
