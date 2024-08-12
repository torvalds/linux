/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Firmware-Assisted Dump internal code.
 *
 * Copyright 2011, Mahesh Salgaonkar, IBM Corporation.
 * Copyright 2019, Hari Bathini, IBM Corporation.
 */

#ifndef _ASM_POWERPC_FADUMP_INTERNAL_H
#define _ASM_POWERPC_FADUMP_INTERNAL_H

/* Maximum number of memory regions kernel supports */
#define FADUMP_MAX_MEM_REGS			128

#ifndef CONFIG_PRESERVE_FA_DUMP

/* The upper limit percentage for user specified boot memory size (25%) */
#define MAX_BOOT_MEM_RATIO			4

#define memblock_num_regions(memblock_type)	(memblock.memblock_type.cnt)

/* FAD commands */
#define FADUMP_REGISTER			1
#define FADUMP_UNREGISTER		2
#define FADUMP_INVALIDATE		3

/*
 * Copy the ascii values for first 8 characters from a string into u64
 * variable at their respective indexes.
 * e.g.
 *  The string "FADMPINF" will be converted into 0x4641444d50494e46
 */
static inline u64 fadump_str_to_u64(const char *str)
{
	u64 val = 0;
	int i;

	for (i = 0; i < sizeof(val); i++)
		val = (*str) ? (val << 8) | *str++ : val << 8;
	return val;
}

#define FADUMP_CPU_UNKNOWN		(~((u32)0))

/*
 * The introduction of new fields in the fadump crash info header has
 * led to a change in the magic key from `FADMPINF` to `FADMPSIG` for
 * identifying a kernel crash from an old kernel.
 *
 * To prevent the need for further changes to the magic number in the
 * event of future modifications to the fadump crash info header, a
 * version field has been introduced to track the fadump crash info
 * header version.
 *
 * Consider a few points before adding new members to the fadump crash info
 * header structure:
 *
 *  - Append new members; avoid adding them in between.
 *  - Non-primitive members should have a size member as well.
 *  - For every change in the fadump header, increment the
 *    fadump header version. This helps the updated kernel decide how to
 *    handle kernel dumps from older kernels.
 */
#define FADUMP_CRASH_INFO_MAGIC_OLD	fadump_str_to_u64("FADMPINF")
#define FADUMP_CRASH_INFO_MAGIC		fadump_str_to_u64("FADMPSIG")
#define FADUMP_HEADER_VERSION		1

/* fadump crash info structure */
struct fadump_crash_info_header {
	u64		magic_number;
	u32		version;
	u32		crashing_cpu;
	u64		vmcoreinfo_raddr;
	u64		vmcoreinfo_size;
	u32		pt_regs_sz;
	u32		cpu_mask_sz;
	struct pt_regs	regs;
	struct cpumask	cpu_mask;
};

struct fadump_memory_range {
	u64	base;
	u64	size;
};

/* fadump memory ranges info */
#define RNG_NAME_SZ			16
struct fadump_mrange_info {
	char				name[RNG_NAME_SZ];
	struct fadump_memory_range	*mem_ranges;
	u32				mem_ranges_sz;
	u32				mem_range_cnt;
	u32				max_mem_ranges;
	bool				is_static;
};

/* Platform specific callback functions */
struct fadump_ops;

/* Firmware-assisted dump configuration details. */
struct fw_dump {
	unsigned long	reserve_dump_area_start;
	unsigned long	reserve_dump_area_size;
	/* cmd line option during boot */
	unsigned long	reserve_bootvar;

	unsigned long	cpu_state_data_size;
	u64		cpu_state_dest_vaddr;
	u32		cpu_state_data_version;
	u32		cpu_state_entry_size;

	unsigned long	hpte_region_size;

	unsigned long	boot_memory_size;
	u64		boot_mem_dest_addr;
	u64		boot_mem_addr[FADUMP_MAX_MEM_REGS];
	u64		boot_mem_sz[FADUMP_MAX_MEM_REGS];
	u64		boot_mem_top;
	u64		boot_mem_regs_cnt;

	unsigned long	fadumphdr_addr;
	u64		elfcorehdr_addr;
	u64		elfcorehdr_size;
	unsigned long	cpu_notes_buf_vaddr;
	unsigned long	cpu_notes_buf_size;

	unsigned long	param_area;

	/*
	 * Maximum size supported by firmware to copy from source to
	 * destination address per entry.
	 */
	u64		max_copy_size;
	u64		kernel_metadata;

	int		ibm_configure_kernel_dump;

	unsigned long	fadump_enabled:1;
	unsigned long	fadump_supported:1;
	unsigned long	dump_active:1;
	unsigned long	dump_registered:1;
	unsigned long	nocma:1;
	unsigned long	param_area_supported:1;

	struct fadump_ops	*ops;
};

struct fadump_ops {
	u64	(*fadump_init_mem_struct)(struct fw_dump *fadump_conf);
	u64	(*fadump_get_metadata_size)(void);
	int	(*fadump_setup_metadata)(struct fw_dump *fadump_conf);
	u64	(*fadump_get_bootmem_min)(void);
	int	(*fadump_register)(struct fw_dump *fadump_conf);
	int	(*fadump_unregister)(struct fw_dump *fadump_conf);
	int	(*fadump_invalidate)(struct fw_dump *fadump_conf);
	void	(*fadump_cleanup)(struct fw_dump *fadump_conf);
	int	(*fadump_process)(struct fw_dump *fadump_conf);
	void	(*fadump_region_show)(struct fw_dump *fadump_conf,
				      struct seq_file *m);
	void	(*fadump_trigger)(struct fadump_crash_info_header *fdh,
				  const char *msg);
	int	(*fadump_max_boot_mem_rgns)(void);
};

/* Helper functions */
s32 __init fadump_setup_cpu_notes_buf(u32 num_cpus);
void fadump_free_cpu_notes_buf(void);
u32 *__init fadump_regs_to_elf_notes(u32 *buf, struct pt_regs *regs);
void __init fadump_update_elfcore_header(char *bufp);
bool is_fadump_reserved_mem_contiguous(void);

#else /* !CONFIG_PRESERVE_FA_DUMP */

/* Firmware-assisted dump configuration details. */
struct fw_dump {
	u64	boot_mem_top;
	u64	dump_active;
};

#endif /* CONFIG_PRESERVE_FA_DUMP */

#ifdef CONFIG_PPC_PSERIES
extern void rtas_fadump_dt_scan(struct fw_dump *fadump_conf, u64 node);
#else
static inline void
rtas_fadump_dt_scan(struct fw_dump *fadump_conf, u64 node) { }
#endif

#ifdef CONFIG_PPC_POWERNV
extern void opal_fadump_dt_scan(struct fw_dump *fadump_conf, u64 node);
#else
static inline void
opal_fadump_dt_scan(struct fw_dump *fadump_conf, u64 node) { }
#endif

#endif /* _ASM_POWERPC_FADUMP_INTERNAL_H */
