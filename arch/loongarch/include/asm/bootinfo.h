/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_BOOTINFO_H
#define _ASM_BOOTINFO_H

#include <linux/types.h>
#include <asm/setup.h>

const char *get_system_type(void);

extern void init_environ(void);
extern void memblock_init(void);
extern void platform_init(void);
extern int __init init_numa_memory(void);

struct loongson_board_info {
	int bios_size;
	const char *bios_vendor;
	const char *bios_version;
	const char *bios_release_date;
	const char *board_name;
	const char *board_vendor;
};

#define NR_WORDS DIV_ROUND_UP(NR_CPUS, BITS_PER_LONG)

struct loongson_system_configuration {
	int nr_cpus;
	int nr_nodes;
	int boot_cpu_id;
	int cores_per_node;
	int cores_per_package;
	unsigned long cores_io_master[NR_WORDS];
	unsigned long suspend_addr;
	const char *cpuname;
};

extern u64 efi_system_table;
extern unsigned long fw_arg0, fw_arg1, fw_arg2;
extern struct loongson_board_info b_info;
extern struct loongson_system_configuration loongson_sysconf;

static inline bool io_master(int cpu)
{
	return test_bit(cpu, loongson_sysconf.cores_io_master);
}

#endif /* _ASM_BOOTINFO_H */
