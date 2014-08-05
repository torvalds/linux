/*
 * efi.c - EFI subsystem
 *
 * Copyright (C) 2001,2003,2004 Dell <Matt_Domsch@dell.com>
 * Copyright (C) 2004 Intel Corporation <matthew.e.tolentino@intel.com>
 * Copyright (C) 2013 Tom Gundersen <teg@jklm.no>
 *
 * This code registers /sys/firmware/efi{,/efivars} when EFI is supported,
 * allowing the efivarfs to be mounted or the efivars module to be loaded.
 * The existance of /sys/firmware/efi may also be used by userspace to
 * determine that the system supports EFI.
 *
 * This file is released under the GPLv2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/efi.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/io.h>

struct efi __read_mostly efi = {
	.mps        = EFI_INVALID_TABLE_ADDR,
	.acpi       = EFI_INVALID_TABLE_ADDR,
	.acpi20     = EFI_INVALID_TABLE_ADDR,
	.smbios     = EFI_INVALID_TABLE_ADDR,
	.sal_systab = EFI_INVALID_TABLE_ADDR,
	.boot_info  = EFI_INVALID_TABLE_ADDR,
	.hcdp       = EFI_INVALID_TABLE_ADDR,
	.uga        = EFI_INVALID_TABLE_ADDR,
	.uv_systab  = EFI_INVALID_TABLE_ADDR,
	.fw_vendor  = EFI_INVALID_TABLE_ADDR,
	.runtime    = EFI_INVALID_TABLE_ADDR,
	.config_table  = EFI_INVALID_TABLE_ADDR,
};
EXPORT_SYMBOL(efi);

static struct kobject *efi_kobj;
static struct kobject *efivars_kobj;

/*
 * Let's not leave out systab information that snuck into
 * the efivars driver
 */
static ssize_t systab_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	char *str = buf;

	if (!kobj || !buf)
		return -EINVAL;

	if (efi.mps != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "MPS=0x%lx\n", efi.mps);
	if (efi.acpi20 != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "ACPI20=0x%lx\n", efi.acpi20);
	if (efi.acpi != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "ACPI=0x%lx\n", efi.acpi);
	if (efi.smbios != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "SMBIOS=0x%lx\n", efi.smbios);
	if (efi.hcdp != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "HCDP=0x%lx\n", efi.hcdp);
	if (efi.boot_info != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "BOOTINFO=0x%lx\n", efi.boot_info);
	if (efi.uga != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "UGA=0x%lx\n", efi.uga);

	return str - buf;
}

static struct kobj_attribute efi_attr_systab =
			__ATTR(systab, 0400, systab_show, NULL);

#define EFI_FIELD(var) efi.var

#define EFI_ATTR_SHOW(name) \
static ssize_t name##_show(struct kobject *kobj, \
				struct kobj_attribute *attr, char *buf) \
{ \
	return sprintf(buf, "0x%lx\n", EFI_FIELD(name)); \
}

EFI_ATTR_SHOW(fw_vendor);
EFI_ATTR_SHOW(runtime);
EFI_ATTR_SHOW(config_table);

static struct kobj_attribute efi_attr_fw_vendor = __ATTR_RO(fw_vendor);
static struct kobj_attribute efi_attr_runtime = __ATTR_RO(runtime);
static struct kobj_attribute efi_attr_config_table = __ATTR_RO(config_table);

static struct attribute *efi_subsys_attrs[] = {
	&efi_attr_systab.attr,
	&efi_attr_fw_vendor.attr,
	&efi_attr_runtime.attr,
	&efi_attr_config_table.attr,
	NULL,
};

static umode_t efi_attr_is_visible(struct kobject *kobj,
				   struct attribute *attr, int n)
{
	umode_t mode = attr->mode;

	if (attr == &efi_attr_fw_vendor.attr)
		return (efi.fw_vendor == EFI_INVALID_TABLE_ADDR) ? 0 : mode;
	else if (attr == &efi_attr_runtime.attr)
		return (efi.runtime == EFI_INVALID_TABLE_ADDR) ? 0 : mode;
	else if (attr == &efi_attr_config_table.attr)
		return (efi.config_table == EFI_INVALID_TABLE_ADDR) ? 0 : mode;

	return mode;
}

static struct attribute_group efi_subsys_attr_group = {
	.attrs = efi_subsys_attrs,
	.is_visible = efi_attr_is_visible,
};

static struct efivars generic_efivars;
static struct efivar_operations generic_ops;

static int generic_ops_register(void)
{
	generic_ops.get_variable = efi.get_variable;
	generic_ops.set_variable = efi.set_variable;
	generic_ops.get_next_variable = efi.get_next_variable;
	generic_ops.query_variable_store = efi_query_variable_store;

	return efivars_register(&generic_efivars, &generic_ops, efi_kobj);
}

static void generic_ops_unregister(void)
{
	efivars_unregister(&generic_efivars);
}

/*
 * We register the efi subsystem with the firmware subsystem and the
 * efivars subsystem with the efi subsystem, if the system was booted with
 * EFI.
 */
static int __init efisubsys_init(void)
{
	int error;

	if (!efi_enabled(EFI_BOOT))
		return 0;

	/* We register the efi directory at /sys/firmware/efi */
	efi_kobj = kobject_create_and_add("efi", firmware_kobj);
	if (!efi_kobj) {
		pr_err("efi: Firmware registration failed.\n");
		return -ENOMEM;
	}

	error = generic_ops_register();
	if (error)
		goto err_put;

	error = sysfs_create_group(efi_kobj, &efi_subsys_attr_group);
	if (error) {
		pr_err("efi: Sysfs attribute export failed with error %d.\n",
		       error);
		goto err_unregister;
	}

	error = efi_runtime_map_init(efi_kobj);
	if (error)
		goto err_remove_group;

	/* and the standard mountpoint for efivarfs */
	efivars_kobj = kobject_create_and_add("efivars", efi_kobj);
	if (!efivars_kobj) {
		pr_err("efivars: Subsystem registration failed.\n");
		error = -ENOMEM;
		goto err_remove_group;
	}

	return 0;

err_remove_group:
	sysfs_remove_group(efi_kobj, &efi_subsys_attr_group);
err_unregister:
	generic_ops_unregister();
err_put:
	kobject_put(efi_kobj);
	return error;
}

subsys_initcall(efisubsys_init);


/*
 * We can't ioremap data in EFI boot services RAM, because we've already mapped
 * it as RAM.  So, look it up in the existing EFI memory map instead.  Only
 * callable after efi_enter_virtual_mode and before efi_free_boot_services.
 */
void __iomem *efi_lookup_mapped_addr(u64 phys_addr)
{
	struct efi_memory_map *map;
	void *p;
	map = efi.memmap;
	if (!map)
		return NULL;
	if (WARN_ON(!map->map))
		return NULL;
	for (p = map->map; p < map->map_end; p += map->desc_size) {
		efi_memory_desc_t *md = p;
		u64 size = md->num_pages << EFI_PAGE_SHIFT;
		u64 end = md->phys_addr + size;
		if (!(md->attribute & EFI_MEMORY_RUNTIME) &&
		    md->type != EFI_BOOT_SERVICES_CODE &&
		    md->type != EFI_BOOT_SERVICES_DATA)
			continue;
		if (!md->virt_addr)
			continue;
		if (phys_addr >= md->phys_addr && phys_addr < end) {
			phys_addr += md->virt_addr - md->phys_addr;
			return (__force void __iomem *)(unsigned long)phys_addr;
		}
	}
	return NULL;
}

static __initdata efi_config_table_type_t common_tables[] = {
	{ACPI_20_TABLE_GUID, "ACPI 2.0", &efi.acpi20},
	{ACPI_TABLE_GUID, "ACPI", &efi.acpi},
	{HCDP_TABLE_GUID, "HCDP", &efi.hcdp},
	{MPS_TABLE_GUID, "MPS", &efi.mps},
	{SAL_SYSTEM_TABLE_GUID, "SALsystab", &efi.sal_systab},
	{SMBIOS_TABLE_GUID, "SMBIOS", &efi.smbios},
	{UGA_IO_PROTOCOL_GUID, "UGA", &efi.uga},
	{NULL_GUID, NULL, NULL},
};

static __init int match_config_table(efi_guid_t *guid,
				     unsigned long table,
				     efi_config_table_type_t *table_types)
{
	u8 str[EFI_VARIABLE_GUID_LEN + 1];
	int i;

	if (table_types) {
		efi_guid_unparse(guid, str);

		for (i = 0; efi_guidcmp(table_types[i].guid, NULL_GUID); i++) {
			efi_guid_unparse(&table_types[i].guid, str);

			if (!efi_guidcmp(*guid, table_types[i].guid)) {
				*(table_types[i].ptr) = table;
				pr_cont(" %s=0x%lx ",
					table_types[i].name, table);
				return 1;
			}
		}
	}

	return 0;
}

int __init efi_config_init(efi_config_table_type_t *arch_tables)
{
	void *config_tables, *tablep;
	int i, sz;

	if (efi_enabled(EFI_64BIT))
		sz = sizeof(efi_config_table_64_t);
	else
		sz = sizeof(efi_config_table_32_t);

	/*
	 * Let's see what config tables the firmware passed to us.
	 */
	config_tables = early_memremap(efi.systab->tables,
				       efi.systab->nr_tables * sz);
	if (config_tables == NULL) {
		pr_err("Could not map Configuration table!\n");
		return -ENOMEM;
	}

	tablep = config_tables;
	pr_info("");
	for (i = 0; i < efi.systab->nr_tables; i++) {
		efi_guid_t guid;
		unsigned long table;

		if (efi_enabled(EFI_64BIT)) {
			u64 table64;
			guid = ((efi_config_table_64_t *)tablep)->guid;
			table64 = ((efi_config_table_64_t *)tablep)->table;
			table = table64;
#ifndef CONFIG_64BIT
			if (table64 >> 32) {
				pr_cont("\n");
				pr_err("Table located above 4GB, disabling EFI.\n");
				early_iounmap(config_tables,
					       efi.systab->nr_tables * sz);
				return -EINVAL;
			}
#endif
		} else {
			guid = ((efi_config_table_32_t *)tablep)->guid;
			table = ((efi_config_table_32_t *)tablep)->table;
		}

		if (!match_config_table(&guid, table, common_tables))
			match_config_table(&guid, table, arch_tables);

		tablep += sz;
	}
	pr_cont("\n");
	early_iounmap(config_tables, efi.systab->nr_tables * sz);

	set_bit(EFI_CONFIG_TABLES, &efi.flags);

	return 0;
}

#ifdef CONFIG_EFI_PARAMS_FROM_FDT

#define UEFI_PARAM(name, prop, field)			   \
	{						   \
		{ name },				   \
		{ prop },				   \
		offsetof(struct efi_fdt_params, field),    \
		FIELD_SIZEOF(struct efi_fdt_params, field) \
	}

static __initdata struct {
	const char name[32];
	const char propname[32];
	int offset;
	int size;
} dt_params[] = {
	UEFI_PARAM("System Table", "linux,uefi-system-table", system_table),
	UEFI_PARAM("MemMap Address", "linux,uefi-mmap-start", mmap),
	UEFI_PARAM("MemMap Size", "linux,uefi-mmap-size", mmap_size),
	UEFI_PARAM("MemMap Desc. Size", "linux,uefi-mmap-desc-size", desc_size),
	UEFI_PARAM("MemMap Desc. Version", "linux,uefi-mmap-desc-ver", desc_ver)
};

struct param_info {
	int verbose;
	void *params;
};

static int __init fdt_find_uefi_params(unsigned long node, const char *uname,
				       int depth, void *data)
{
	struct param_info *info = data;
	const void *prop;
	void *dest;
	u64 val;
	int i, len;

	if (depth != 1 ||
	    (strcmp(uname, "chosen") != 0 && strcmp(uname, "chosen@0") != 0))
		return 0;

	pr_info("Getting parameters from FDT:\n");

	for (i = 0; i < ARRAY_SIZE(dt_params); i++) {
		prop = of_get_flat_dt_prop(node, dt_params[i].propname, &len);
		if (!prop) {
			pr_err("Can't find %s in device tree!\n",
			       dt_params[i].name);
			return 0;
		}
		dest = info->params + dt_params[i].offset;

		val = of_read_number(prop, len / sizeof(u32));

		if (dt_params[i].size == sizeof(u32))
			*(u32 *)dest = val;
		else
			*(u64 *)dest = val;

		if (info->verbose)
			pr_info("  %s: 0x%0*llx\n", dt_params[i].name,
				dt_params[i].size * 2, val);
	}
	return 1;
}

int __init efi_get_fdt_params(struct efi_fdt_params *params, int verbose)
{
	struct param_info info;

	info.verbose = verbose;
	info.params = params;

	return of_scan_flat_dt(fdt_find_uefi_params, &info);
}
#endif /* CONFIG_EFI_PARAMS_FROM_FDT */
