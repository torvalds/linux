// SPDX-License-Identifier: GPL-2.0
/*
 * EFI initialization
 *
 * Author: Jianmin Lv <lvjianmin@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/efi-bgrt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/memblock.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>

#include <asm/early_ioremap.h>
#include <asm/efi.h>
#include <asm/loongson.h>

static unsigned long efi_nr_tables;
static unsigned long efi_config_table;

static unsigned long __initdata boot_memmap = EFI_INVALID_TABLE_ADDR;

static efi_system_table_t *efi_systab;
static efi_config_table_type_t arch_tables[] __initdata = {
	{LINUX_EFI_BOOT_MEMMAP_GUID,	&boot_memmap,	"MEMMAP" },
	{},
};

void __init efi_runtime_init(void)
{
	if (!efi_enabled(EFI_BOOT))
		return;

	if (efi_runtime_disabled()) {
		pr_info("EFI runtime services will be disabled.\n");
		return;
	}

	efi.runtime = (efi_runtime_services_t *)efi_systab->runtime;
	efi.runtime_version = (unsigned int)efi.runtime->hdr.revision;

	efi_native_runtime_setup();
	set_bit(EFI_RUNTIME_SERVICES, &efi.flags);
}

void __init efi_init(void)
{
	int size;
	void *config_tables;
	struct efi_boot_memmap *tbl;

	if (!efi_system_table)
		return;

	efi_systab = (efi_system_table_t *)early_memremap_ro(efi_system_table, sizeof(*efi_systab));
	if (!efi_systab) {
		pr_err("Can't find EFI system table.\n");
		return;
	}

	efi_systab_report_header(&efi_systab->hdr, efi_systab->fw_vendor);

	set_bit(EFI_64BIT, &efi.flags);
	efi_nr_tables	 = efi_systab->nr_tables;
	efi_config_table = (unsigned long)efi_systab->tables;

	size = sizeof(efi_config_table_t);
	config_tables = early_memremap(efi_config_table, efi_nr_tables * size);
	efi_config_parse_tables(config_tables, efi_systab->nr_tables, arch_tables);
	early_memunmap(config_tables, efi_nr_tables * size);

	set_bit(EFI_CONFIG_TABLES, &efi.flags);

	if (screen_info.orig_video_isVGA == VIDEO_TYPE_EFI)
		memblock_reserve(screen_info.lfb_base, screen_info.lfb_size);

	if (boot_memmap == EFI_INVALID_TABLE_ADDR)
		return;

	tbl = early_memremap_ro(boot_memmap, sizeof(*tbl));
	if (tbl) {
		struct efi_memory_map_data data;

		data.phys_map		= boot_memmap + sizeof(*tbl);
		data.size		= tbl->map_size;
		data.desc_size		= tbl->desc_size;
		data.desc_version	= tbl->desc_ver;

		if (efi_memmap_init_early(&data) < 0)
			panic("Unable to map EFI memory map.\n");

		early_memunmap(tbl, sizeof(*tbl));
	}
}
