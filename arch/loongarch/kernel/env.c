// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <asm/early_ioremap.h>
#include <asm/bootinfo.h>
#include <asm/loongson.h>

u64 efi_system_table;
struct loongson_system_configuration loongson_sysconf;
EXPORT_SYMBOL(loongson_sysconf);

void __init init_environ(void)
{
	int efi_boot = fw_arg0;
	struct efi_memory_map_data data;
	void *fdt_ptr = early_memremap_ro(fw_arg1, SZ_64K);

	if (efi_boot)
		set_bit(EFI_BOOT, &efi.flags);
	else
		clear_bit(EFI_BOOT, &efi.flags);

	early_init_dt_scan(fdt_ptr);
	early_init_fdt_reserve_self();
	efi_system_table = efi_get_fdt_params(&data);

	efi_memmap_init_early(&data);
	memblock_reserve(data.phys_map & PAGE_MASK,
			 PAGE_ALIGN(data.size + (data.phys_map & ~PAGE_MASK)));
}

static int __init init_cpu_fullname(void)
{
	int cpu;

	if (loongson_sysconf.cpuname && !strncmp(loongson_sysconf.cpuname, "Loongson", 8)) {
		for (cpu = 0; cpu < NR_CPUS; cpu++)
			__cpu_full_name[cpu] = loongson_sysconf.cpuname;
	}
	return 0;
}
arch_initcall(init_cpu_fullname);

static ssize_t boardinfo_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,
		"BIOS Information\n"
		"Vendor\t\t\t: %s\n"
		"Version\t\t\t: %s\n"
		"ROM Size\t\t: %d KB\n"
		"Release Date\t\t: %s\n\n"
		"Board Information\n"
		"Manufacturer\t\t: %s\n"
		"Board Name\t\t: %s\n"
		"Family\t\t\t: LOONGSON64\n\n",
		b_info.bios_vendor, b_info.bios_version,
		b_info.bios_size, b_info.bios_release_date,
		b_info.board_vendor, b_info.board_name);
}

static struct kobj_attribute boardinfo_attr = __ATTR(boardinfo, 0444,
						     boardinfo_show, NULL);

static int __init boardinfo_init(void)
{
	struct kobject *loongson_kobj;

	loongson_kobj = kobject_create_and_add("loongson", firmware_kobj);

	return sysfs_create_file(loongson_kobj, &boardinfo_attr.attr);
}
late_initcall(boardinfo_init);
