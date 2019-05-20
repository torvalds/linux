// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/kexec.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/ucs2_string.h>
#include <linux/memblock.h>

#include <asm/early_ioremap.h>

struct efi __read_mostly efi = {
	.mps			= EFI_INVALID_TABLE_ADDR,
	.acpi			= EFI_INVALID_TABLE_ADDR,
	.acpi20			= EFI_INVALID_TABLE_ADDR,
	.smbios			= EFI_INVALID_TABLE_ADDR,
	.smbios3		= EFI_INVALID_TABLE_ADDR,
	.sal_systab		= EFI_INVALID_TABLE_ADDR,
	.boot_info		= EFI_INVALID_TABLE_ADDR,
	.hcdp			= EFI_INVALID_TABLE_ADDR,
	.uga			= EFI_INVALID_TABLE_ADDR,
	.uv_systab		= EFI_INVALID_TABLE_ADDR,
	.fw_vendor		= EFI_INVALID_TABLE_ADDR,
	.runtime		= EFI_INVALID_TABLE_ADDR,
	.config_table		= EFI_INVALID_TABLE_ADDR,
	.esrt			= EFI_INVALID_TABLE_ADDR,
	.properties_table	= EFI_INVALID_TABLE_ADDR,
	.mem_attr_table		= EFI_INVALID_TABLE_ADDR,
	.rng_seed		= EFI_INVALID_TABLE_ADDR,
	.tpm_log		= EFI_INVALID_TABLE_ADDR,
	.tpm_final_log		= EFI_INVALID_TABLE_ADDR,
	.mem_reserve		= EFI_INVALID_TABLE_ADDR,
};
EXPORT_SYMBOL(efi);

static unsigned long *efi_tables[] = {
	&efi.mps,
	&efi.acpi,
	&efi.acpi20,
	&efi.smbios,
	&efi.smbios3,
	&efi.sal_systab,
	&efi.boot_info,
	&efi.hcdp,
	&efi.uga,
	&efi.uv_systab,
	&efi.fw_vendor,
	&efi.runtime,
	&efi.config_table,
	&efi.esrt,
	&efi.properties_table,
	&efi.mem_attr_table,
};

struct mm_struct efi_mm = {
	.mm_rb			= RB_ROOT,
	.mm_users		= ATOMIC_INIT(2),
	.mm_count		= ATOMIC_INIT(1),
	.mmap_sem		= __RWSEM_INITIALIZER(efi_mm.mmap_sem),
	.page_table_lock	= __SPIN_LOCK_UNLOCKED(efi_mm.page_table_lock),
	.mmlist			= LIST_HEAD_INIT(efi_mm.mmlist),
	.cpu_bitmap		= { [BITS_TO_LONGS(NR_CPUS)] = 0},
};

struct workqueue_struct *efi_rts_wq;

static bool disable_runtime;
static int __init setup_noefi(char *arg)
{
	disable_runtime = true;
	return 0;
}
early_param("noefi", setup_noefi);

bool efi_runtime_disabled(void)
{
	return disable_runtime;
}

static int __init parse_efi_cmdline(char *str)
{
	if (!str) {
		pr_warn("need at least one option\n");
		return -EINVAL;
	}

	if (parse_option_str(str, "debug"))
		set_bit(EFI_DBG, &efi.flags);

	if (parse_option_str(str, "noruntime"))
		disable_runtime = true;

	return 0;
}
early_param("efi", parse_efi_cmdline);

struct kobject *efi_kobj;

/*
 * Let's not leave out systab information that snuck into
 * the efivars driver
 * Note, do not add more fields in systab sysfs file as it breaks sysfs
 * one value per file rule!
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
	/*
	 * If both SMBIOS and SMBIOS3 entry points are implemented, the
	 * SMBIOS3 entry point shall be preferred, so we list it first to
	 * let applications stop parsing after the first match.
	 */
	if (efi.smbios3 != EFI_INVALID_TABLE_ADDR)
		str += sprintf(str, "SMBIOS3=0x%lx\n", efi.smbios3);
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

static struct kobj_attribute efi_attr_systab = __ATTR_RO_MODE(systab, 0400);

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

static ssize_t fw_platform_size_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", efi_enabled(EFI_64BIT) ? 64 : 32);
}

static struct kobj_attribute efi_attr_fw_vendor = __ATTR_RO(fw_vendor);
static struct kobj_attribute efi_attr_runtime = __ATTR_RO(runtime);
static struct kobj_attribute efi_attr_config_table = __ATTR_RO(config_table);
static struct kobj_attribute efi_attr_fw_platform_size =
	__ATTR_RO(fw_platform_size);

static struct attribute *efi_subsys_attrs[] = {
	&efi_attr_systab.attr,
	&efi_attr_fw_vendor.attr,
	&efi_attr_runtime.attr,
	&efi_attr_config_table.attr,
	&efi_attr_fw_platform_size.attr,
	NULL,
};

static umode_t efi_attr_is_visible(struct kobject *kobj,
				   struct attribute *attr, int n)
{
	if (attr == &efi_attr_fw_vendor.attr) {
		if (efi_enabled(EFI_PARAVIRT) ||
				efi.fw_vendor == EFI_INVALID_TABLE_ADDR)
			return 0;
	} else if (attr == &efi_attr_runtime.attr) {
		if (efi.runtime == EFI_INVALID_TABLE_ADDR)
			return 0;
	} else if (attr == &efi_attr_config_table.attr) {
		if (efi.config_table == EFI_INVALID_TABLE_ADDR)
			return 0;
	}

	return attr->mode;
}

static const struct attribute_group efi_subsys_attr_group = {
	.attrs = efi_subsys_attrs,
	.is_visible = efi_attr_is_visible,
};

static struct efivars generic_efivars;
static struct efivar_operations generic_ops;

static int generic_ops_register(void)
{
	generic_ops.get_variable = efi.get_variable;
	generic_ops.set_variable = efi.set_variable;
	generic_ops.set_variable_nonblocking = efi.set_variable_nonblocking;
	generic_ops.get_next_variable = efi.get_next_variable;
	generic_ops.query_variable_store = efi_query_variable_store;

	return efivars_register(&generic_efivars, &generic_ops, efi_kobj);
}

static void generic_ops_unregister(void)
{
	efivars_unregister(&generic_efivars);
}

#if IS_ENABLED(CONFIG_ACPI)
#define EFIVAR_SSDT_NAME_MAX	16
static char efivar_ssdt[EFIVAR_SSDT_NAME_MAX] __initdata;
static int __init efivar_ssdt_setup(char *str)
{
	if (strlen(str) < sizeof(efivar_ssdt))
		memcpy(efivar_ssdt, str, strlen(str));
	else
		pr_warn("efivar_ssdt: name too long: %s\n", str);
	return 0;
}
__setup("efivar_ssdt=", efivar_ssdt_setup);

static __init int efivar_ssdt_iter(efi_char16_t *name, efi_guid_t vendor,
				   unsigned long name_size, void *data)
{
	struct efivar_entry *entry;
	struct list_head *list = data;
	char utf8_name[EFIVAR_SSDT_NAME_MAX];
	int limit = min_t(unsigned long, EFIVAR_SSDT_NAME_MAX, name_size);

	ucs2_as_utf8(utf8_name, name, limit - 1);
	if (strncmp(utf8_name, efivar_ssdt, limit) != 0)
		return 0;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return 0;

	memcpy(entry->var.VariableName, name, name_size);
	memcpy(&entry->var.VendorGuid, &vendor, sizeof(efi_guid_t));

	efivar_entry_add(entry, list);

	return 0;
}

static __init int efivar_ssdt_load(void)
{
	LIST_HEAD(entries);
	struct efivar_entry *entry, *aux;
	unsigned long size;
	void *data;
	int ret;

	ret = efivar_init(efivar_ssdt_iter, &entries, true, &entries);

	list_for_each_entry_safe(entry, aux, &entries, list) {
		pr_info("loading SSDT from variable %s-%pUl\n", efivar_ssdt,
			&entry->var.VendorGuid);

		list_del(&entry->list);

		ret = efivar_entry_size(entry, &size);
		if (ret) {
			pr_err("failed to get var size\n");
			goto free_entry;
		}

		data = kmalloc(size, GFP_KERNEL);
		if (!data) {
			ret = -ENOMEM;
			goto free_entry;
		}

		ret = efivar_entry_get(entry, NULL, &size, data);
		if (ret) {
			pr_err("failed to get var data\n");
			goto free_data;
		}

		ret = acpi_load_table(data);
		if (ret) {
			pr_err("failed to load table: %d\n", ret);
			goto free_data;
		}

		goto free_entry;

free_data:
		kfree(data);

free_entry:
		kfree(entry);
	}

	return ret;
}
#else
static inline int efivar_ssdt_load(void) { return 0; }
#endif

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

	/*
	 * Since we process only one efi_runtime_service() at a time, an
	 * ordered workqueue (which creates only one execution context)
	 * should suffice all our needs.
	 */
	efi_rts_wq = alloc_ordered_workqueue("efi_rts_wq", 0);
	if (!efi_rts_wq) {
		pr_err("Creating efi_rts_wq failed, EFI runtime services disabled.\n");
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return 0;
	}

	/* We register the efi directory at /sys/firmware/efi */
	efi_kobj = kobject_create_and_add("efi", firmware_kobj);
	if (!efi_kobj) {
		pr_err("efi: Firmware registration failed.\n");
		return -ENOMEM;
	}

	error = generic_ops_register();
	if (error)
		goto err_put;

	if (efi_enabled(EFI_RUNTIME_SERVICES))
		efivar_ssdt_load();

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
	error = sysfs_create_mount_point(efi_kobj, "efivars");
	if (error) {
		pr_err("efivars: Subsystem registration failed.\n");
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
 * Find the efi memory descriptor for a given physical address.  Given a
 * physical address, determine if it exists within an EFI Memory Map entry,
 * and if so, populate the supplied memory descriptor with the appropriate
 * data.
 */
int efi_mem_desc_lookup(u64 phys_addr, efi_memory_desc_t *out_md)
{
	efi_memory_desc_t *md;

	if (!efi_enabled(EFI_MEMMAP)) {
		pr_err_once("EFI_MEMMAP is not enabled.\n");
		return -EINVAL;
	}

	if (!out_md) {
		pr_err_once("out_md is null.\n");
		return -EINVAL;
        }

	for_each_efi_memory_desc(md) {
		u64 size;
		u64 end;

		size = md->num_pages << EFI_PAGE_SHIFT;
		end = md->phys_addr + size;
		if (phys_addr >= md->phys_addr && phys_addr < end) {
			memcpy(out_md, md, sizeof(*out_md));
			return 0;
		}
	}
	return -ENOENT;
}

/*
 * Calculate the highest address of an efi memory descriptor.
 */
u64 __init efi_mem_desc_end(efi_memory_desc_t *md)
{
	u64 size = md->num_pages << EFI_PAGE_SHIFT;
	u64 end = md->phys_addr + size;
	return end;
}

void __init __weak efi_arch_mem_reserve(phys_addr_t addr, u64 size) {}

/**
 * efi_mem_reserve - Reserve an EFI memory region
 * @addr: Physical address to reserve
 * @size: Size of reservation
 *
 * Mark a region as reserved from general kernel allocation and
 * prevent it being released by efi_free_boot_services().
 *
 * This function should be called drivers once they've parsed EFI
 * configuration tables to figure out where their data lives, e.g.
 * efi_esrt_init().
 */
void __init efi_mem_reserve(phys_addr_t addr, u64 size)
{
	if (!memblock_is_region_reserved(addr, size))
		memblock_reserve(addr, size);

	/*
	 * Some architectures (x86) reserve all boot services ranges
	 * until efi_free_boot_services() because of buggy firmware
	 * implementations. This means the above memblock_reserve() is
	 * superfluous on x86 and instead what it needs to do is
	 * ensure the @start, @size is not freed.
	 */
	efi_arch_mem_reserve(addr, size);
}

static __initdata efi_config_table_type_t common_tables[] = {
	{ACPI_20_TABLE_GUID, "ACPI 2.0", &efi.acpi20},
	{ACPI_TABLE_GUID, "ACPI", &efi.acpi},
	{HCDP_TABLE_GUID, "HCDP", &efi.hcdp},
	{MPS_TABLE_GUID, "MPS", &efi.mps},
	{SAL_SYSTEM_TABLE_GUID, "SALsystab", &efi.sal_systab},
	{SMBIOS_TABLE_GUID, "SMBIOS", &efi.smbios},
	{SMBIOS3_TABLE_GUID, "SMBIOS 3.0", &efi.smbios3},
	{UGA_IO_PROTOCOL_GUID, "UGA", &efi.uga},
	{EFI_SYSTEM_RESOURCE_TABLE_GUID, "ESRT", &efi.esrt},
	{EFI_PROPERTIES_TABLE_GUID, "PROP", &efi.properties_table},
	{EFI_MEMORY_ATTRIBUTES_TABLE_GUID, "MEMATTR", &efi.mem_attr_table},
	{LINUX_EFI_RANDOM_SEED_TABLE_GUID, "RNG", &efi.rng_seed},
	{LINUX_EFI_TPM_EVENT_LOG_GUID, "TPMEventLog", &efi.tpm_log},
	{LINUX_EFI_TPM_FINAL_LOG_GUID, "TPMFinalLog", &efi.tpm_final_log},
	{LINUX_EFI_MEMRESERVE_TABLE_GUID, "MEMRESERVE", &efi.mem_reserve},
	{NULL_GUID, NULL, NULL},
};

static __init int match_config_table(efi_guid_t *guid,
				     unsigned long table,
				     efi_config_table_type_t *table_types)
{
	int i;

	if (table_types) {
		for (i = 0; efi_guidcmp(table_types[i].guid, NULL_GUID); i++) {
			if (!efi_guidcmp(*guid, table_types[i].guid)) {
				*(table_types[i].ptr) = table;
				if (table_types[i].name)
					pr_cont(" %s=0x%lx ",
						table_types[i].name, table);
				return 1;
			}
		}
	}

	return 0;
}

int __init efi_config_parse_tables(void *config_tables, int count, int sz,
				   efi_config_table_type_t *arch_tables)
{
	void *tablep;
	int i;

	tablep = config_tables;
	pr_info("");
	for (i = 0; i < count; i++) {
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
	set_bit(EFI_CONFIG_TABLES, &efi.flags);

	if (efi.rng_seed != EFI_INVALID_TABLE_ADDR) {
		struct linux_efi_random_seed *seed;
		u32 size = 0;

		seed = early_memremap(efi.rng_seed, sizeof(*seed));
		if (seed != NULL) {
			size = seed->size;
			early_memunmap(seed, sizeof(*seed));
		} else {
			pr_err("Could not map UEFI random seed!\n");
		}
		if (size > 0) {
			seed = early_memremap(efi.rng_seed,
					      sizeof(*seed) + size);
			if (seed != NULL) {
				pr_notice("seeding entropy pool\n");
				add_device_randomness(seed->bits, seed->size);
				early_memunmap(seed, sizeof(*seed) + size);
			} else {
				pr_err("Could not map UEFI random seed!\n");
			}
		}
	}

	if (efi_enabled(EFI_MEMMAP))
		efi_memattr_init();

	efi_tpm_eventlog_init();

	/* Parse the EFI Properties table if it exists */
	if (efi.properties_table != EFI_INVALID_TABLE_ADDR) {
		efi_properties_table_t *tbl;

		tbl = early_memremap(efi.properties_table, sizeof(*tbl));
		if (tbl == NULL) {
			pr_err("Could not map Properties table!\n");
			return -ENOMEM;
		}

		if (tbl->memory_protection_attribute &
		    EFI_PROPERTIES_RUNTIME_MEMORY_PROTECTION_NON_EXECUTABLE_PE_DATA)
			set_bit(EFI_NX_PE_DATA, &efi.flags);

		early_memunmap(tbl, sizeof(*tbl));
	}

	if (efi.mem_reserve != EFI_INVALID_TABLE_ADDR) {
		unsigned long prsv = efi.mem_reserve;

		while (prsv) {
			struct linux_efi_memreserve *rsv;
			u8 *p;
			int i;

			/*
			 * Just map a full page: that is what we will get
			 * anyway, and it permits us to map the entire entry
			 * before knowing its size.
			 */
			p = early_memremap(ALIGN_DOWN(prsv, PAGE_SIZE),
					   PAGE_SIZE);
			if (p == NULL) {
				pr_err("Could not map UEFI memreserve entry!\n");
				return -ENOMEM;
			}

			rsv = (void *)(p + prsv % PAGE_SIZE);

			/* reserve the entry itself */
			memblock_reserve(prsv, EFI_MEMRESERVE_SIZE(rsv->size));

			for (i = 0; i < atomic_read(&rsv->count); i++) {
				memblock_reserve(rsv->entry[i].base,
						 rsv->entry[i].size);
			}

			prsv = rsv->next;
			early_memunmap(p, PAGE_SIZE);
		}
	}

	return 0;
}

int __init efi_config_init(efi_config_table_type_t *arch_tables)
{
	void *config_tables;
	int sz, ret;

	if (efi.systab->nr_tables == 0)
		return 0;

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

	ret = efi_config_parse_tables(config_tables, efi.systab->nr_tables, sz,
				      arch_tables);

	early_memunmap(config_tables, efi.systab->nr_tables * sz);
	return ret;
}

#ifdef CONFIG_EFI_VARS_MODULE
static int __init efi_load_efivars(void)
{
	struct platform_device *pdev;

	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return 0;

	pdev = platform_device_register_simple("efivars", 0, NULL, 0);
	return PTR_ERR_OR_ZERO(pdev);
}
device_initcall(efi_load_efivars);
#endif

#ifdef CONFIG_EFI_PARAMS_FROM_FDT

#define UEFI_PARAM(name, prop, field)			   \
	{						   \
		{ name },				   \
		{ prop },				   \
		offsetof(struct efi_fdt_params, field),    \
		FIELD_SIZEOF(struct efi_fdt_params, field) \
	}

struct params {
	const char name[32];
	const char propname[32];
	int offset;
	int size;
};

static __initdata struct params fdt_params[] = {
	UEFI_PARAM("System Table", "linux,uefi-system-table", system_table),
	UEFI_PARAM("MemMap Address", "linux,uefi-mmap-start", mmap),
	UEFI_PARAM("MemMap Size", "linux,uefi-mmap-size", mmap_size),
	UEFI_PARAM("MemMap Desc. Size", "linux,uefi-mmap-desc-size", desc_size),
	UEFI_PARAM("MemMap Desc. Version", "linux,uefi-mmap-desc-ver", desc_ver)
};

static __initdata struct params xen_fdt_params[] = {
	UEFI_PARAM("System Table", "xen,uefi-system-table", system_table),
	UEFI_PARAM("MemMap Address", "xen,uefi-mmap-start", mmap),
	UEFI_PARAM("MemMap Size", "xen,uefi-mmap-size", mmap_size),
	UEFI_PARAM("MemMap Desc. Size", "xen,uefi-mmap-desc-size", desc_size),
	UEFI_PARAM("MemMap Desc. Version", "xen,uefi-mmap-desc-ver", desc_ver)
};

#define EFI_FDT_PARAMS_SIZE	ARRAY_SIZE(fdt_params)

static __initdata struct {
	const char *uname;
	const char *subnode;
	struct params *params;
} dt_params[] = {
	{ "hypervisor", "uefi", xen_fdt_params },
	{ "chosen", NULL, fdt_params },
};

struct param_info {
	int found;
	void *params;
	const char *missing;
};

static int __init __find_uefi_params(unsigned long node,
				     struct param_info *info,
				     struct params *params)
{
	const void *prop;
	void *dest;
	u64 val;
	int i, len;

	for (i = 0; i < EFI_FDT_PARAMS_SIZE; i++) {
		prop = of_get_flat_dt_prop(node, params[i].propname, &len);
		if (!prop) {
			info->missing = params[i].name;
			return 0;
		}

		dest = info->params + params[i].offset;
		info->found++;

		val = of_read_number(prop, len / sizeof(u32));

		if (params[i].size == sizeof(u32))
			*(u32 *)dest = val;
		else
			*(u64 *)dest = val;

		if (efi_enabled(EFI_DBG))
			pr_info("  %s: 0x%0*llx\n", params[i].name,
				params[i].size * 2, val);
	}

	return 1;
}

static int __init fdt_find_uefi_params(unsigned long node, const char *uname,
				       int depth, void *data)
{
	struct param_info *info = data;
	int i;

	for (i = 0; i < ARRAY_SIZE(dt_params); i++) {
		const char *subnode = dt_params[i].subnode;

		if (depth != 1 || strcmp(uname, dt_params[i].uname) != 0) {
			info->missing = dt_params[i].params[0].name;
			continue;
		}

		if (subnode) {
			int err = of_get_flat_dt_subnode_by_name(node, subnode);

			if (err < 0)
				return 0;

			node = err;
		}

		return __find_uefi_params(node, info, dt_params[i].params);
	}

	return 0;
}

int __init efi_get_fdt_params(struct efi_fdt_params *params)
{
	struct param_info info;
	int ret;

	pr_info("Getting EFI parameters from FDT:\n");

	info.found = 0;
	info.params = params;

	ret = of_scan_flat_dt(fdt_find_uefi_params, &info);
	if (!info.found)
		pr_info("UEFI not found.\n");
	else if (!ret)
		pr_err("Can't find '%s' in device tree!\n",
		       info.missing);

	return ret;
}
#endif /* CONFIG_EFI_PARAMS_FROM_FDT */

static __initdata char memory_type_name[][20] = {
	"Reserved",
	"Loader Code",
	"Loader Data",
	"Boot Code",
	"Boot Data",
	"Runtime Code",
	"Runtime Data",
	"Conventional Memory",
	"Unusable Memory",
	"ACPI Reclaim Memory",
	"ACPI Memory NVS",
	"Memory Mapped I/O",
	"MMIO Port Space",
	"PAL Code",
	"Persistent Memory",
};

char * __init efi_md_typeattr_format(char *buf, size_t size,
				     const efi_memory_desc_t *md)
{
	char *pos;
	int type_len;
	u64 attr;

	pos = buf;
	if (md->type >= ARRAY_SIZE(memory_type_name))
		type_len = snprintf(pos, size, "[type=%u", md->type);
	else
		type_len = snprintf(pos, size, "[%-*s",
				    (int)(sizeof(memory_type_name[0]) - 1),
				    memory_type_name[md->type]);
	if (type_len >= size)
		return buf;

	pos += type_len;
	size -= type_len;

	attr = md->attribute;
	if (attr & ~(EFI_MEMORY_UC | EFI_MEMORY_WC | EFI_MEMORY_WT |
		     EFI_MEMORY_WB | EFI_MEMORY_UCE | EFI_MEMORY_RO |
		     EFI_MEMORY_WP | EFI_MEMORY_RP | EFI_MEMORY_XP |
		     EFI_MEMORY_NV |
		     EFI_MEMORY_RUNTIME | EFI_MEMORY_MORE_RELIABLE))
		snprintf(pos, size, "|attr=0x%016llx]",
			 (unsigned long long)attr);
	else
		snprintf(pos, size,
			 "|%3s|%2s|%2s|%2s|%2s|%2s|%2s|%3s|%2s|%2s|%2s|%2s]",
			 attr & EFI_MEMORY_RUNTIME ? "RUN" : "",
			 attr & EFI_MEMORY_MORE_RELIABLE ? "MR" : "",
			 attr & EFI_MEMORY_NV      ? "NV"  : "",
			 attr & EFI_MEMORY_XP      ? "XP"  : "",
			 attr & EFI_MEMORY_RP      ? "RP"  : "",
			 attr & EFI_MEMORY_WP      ? "WP"  : "",
			 attr & EFI_MEMORY_RO      ? "RO"  : "",
			 attr & EFI_MEMORY_UCE     ? "UCE" : "",
			 attr & EFI_MEMORY_WB      ? "WB"  : "",
			 attr & EFI_MEMORY_WT      ? "WT"  : "",
			 attr & EFI_MEMORY_WC      ? "WC"  : "",
			 attr & EFI_MEMORY_UC      ? "UC"  : "");
	return buf;
}

/*
 * IA64 has a funky EFI memory map that doesn't work the same way as
 * other architectures.
 */
#ifndef CONFIG_IA64
/*
 * efi_mem_attributes - lookup memmap attributes for physical address
 * @phys_addr: the physical address to lookup
 *
 * Search in the EFI memory map for the region covering
 * @phys_addr. Returns the EFI memory attributes if the region
 * was found in the memory map, 0 otherwise.
 */
u64 efi_mem_attributes(unsigned long phys_addr)
{
	efi_memory_desc_t *md;

	if (!efi_enabled(EFI_MEMMAP))
		return 0;

	for_each_efi_memory_desc(md) {
		if ((md->phys_addr <= phys_addr) &&
		    (phys_addr < (md->phys_addr +
		    (md->num_pages << EFI_PAGE_SHIFT))))
			return md->attribute;
	}
	return 0;
}

/*
 * efi_mem_type - lookup memmap type for physical address
 * @phys_addr: the physical address to lookup
 *
 * Search in the EFI memory map for the region covering @phys_addr.
 * Returns the EFI memory type if the region was found in the memory
 * map, EFI_RESERVED_TYPE (zero) otherwise.
 */
int efi_mem_type(unsigned long phys_addr)
{
	const efi_memory_desc_t *md;

	if (!efi_enabled(EFI_MEMMAP))
		return -ENOTSUPP;

	for_each_efi_memory_desc(md) {
		if ((md->phys_addr <= phys_addr) &&
		    (phys_addr < (md->phys_addr +
				  (md->num_pages << EFI_PAGE_SHIFT))))
			return md->type;
	}
	return -EINVAL;
}
#endif

int efi_status_to_err(efi_status_t status)
{
	int err;

	switch (status) {
	case EFI_SUCCESS:
		err = 0;
		break;
	case EFI_INVALID_PARAMETER:
		err = -EINVAL;
		break;
	case EFI_OUT_OF_RESOURCES:
		err = -ENOSPC;
		break;
	case EFI_DEVICE_ERROR:
		err = -EIO;
		break;
	case EFI_WRITE_PROTECTED:
		err = -EROFS;
		break;
	case EFI_SECURITY_VIOLATION:
		err = -EACCES;
		break;
	case EFI_NOT_FOUND:
		err = -ENOENT;
		break;
	case EFI_ABORTED:
		err = -EINTR;
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

bool efi_is_table_address(unsigned long phys_addr)
{
	unsigned int i;

	if (phys_addr == EFI_INVALID_TABLE_ADDR)
		return false;

	for (i = 0; i < ARRAY_SIZE(efi_tables); i++)
		if (*(efi_tables[i]) == phys_addr)
			return true;

	return false;
}

static DEFINE_SPINLOCK(efi_mem_reserve_persistent_lock);
static struct linux_efi_memreserve *efi_memreserve_root __ro_after_init;

static int __init efi_memreserve_map_root(void)
{
	if (efi.mem_reserve == EFI_INVALID_TABLE_ADDR)
		return -ENODEV;

	efi_memreserve_root = memremap(efi.mem_reserve,
				       sizeof(*efi_memreserve_root),
				       MEMREMAP_WB);
	if (WARN_ON_ONCE(!efi_memreserve_root))
		return -ENOMEM;
	return 0;
}

int __ref efi_mem_reserve_persistent(phys_addr_t addr, u64 size)
{
	struct linux_efi_memreserve *rsv;
	unsigned long prsv;
	int rc, index;

	if (efi_memreserve_root == (void *)ULONG_MAX)
		return -ENODEV;

	if (!efi_memreserve_root) {
		rc = efi_memreserve_map_root();
		if (rc)
			return rc;
	}

	/* first try to find a slot in an existing linked list entry */
	for (prsv = efi_memreserve_root->next; prsv; prsv = rsv->next) {
		rsv = __va(prsv);
		index = atomic_fetch_add_unless(&rsv->count, 1, rsv->size);
		if (index < rsv->size) {
			rsv->entry[index].base = addr;
			rsv->entry[index].size = size;

			return 0;
		}
	}

	/* no slot found - allocate a new linked list entry */
	rsv = (struct linux_efi_memreserve *)__get_free_page(GFP_ATOMIC);
	if (!rsv)
		return -ENOMEM;

	rsv->size = EFI_MEMRESERVE_COUNT(PAGE_SIZE);
	atomic_set(&rsv->count, 1);
	rsv->entry[0].base = addr;
	rsv->entry[0].size = size;

	spin_lock(&efi_mem_reserve_persistent_lock);
	rsv->next = efi_memreserve_root->next;
	efi_memreserve_root->next = __pa(rsv);
	spin_unlock(&efi_mem_reserve_persistent_lock);

	return 0;
}

static int __init efi_memreserve_root_init(void)
{
	if (efi_memreserve_root)
		return 0;
	if (efi_memreserve_map_root())
		efi_memreserve_root = (void *)ULONG_MAX;
	return 0;
}
early_initcall(efi_memreserve_root_init);

#ifdef CONFIG_KEXEC
static int update_efi_random_seed(struct notifier_block *nb,
				  unsigned long code, void *unused)
{
	struct linux_efi_random_seed *seed;
	u32 size = 0;

	if (!kexec_in_progress)
		return NOTIFY_DONE;

	seed = memremap(efi.rng_seed, sizeof(*seed), MEMREMAP_WB);
	if (seed != NULL) {
		size = min(seed->size, EFI_RANDOM_SEED_SIZE);
		memunmap(seed);
	} else {
		pr_err("Could not map UEFI random seed!\n");
	}
	if (size > 0) {
		seed = memremap(efi.rng_seed, sizeof(*seed) + size,
				MEMREMAP_WB);
		if (seed != NULL) {
			seed->size = size;
			get_random_bytes(seed->bits, seed->size);
			memunmap(seed);
		} else {
			pr_err("Could not map UEFI random seed!\n");
		}
	}
	return NOTIFY_DONE;
}

static struct notifier_block efi_random_seed_nb = {
	.notifier_call = update_efi_random_seed,
};

static int register_update_efi_random_seed(void)
{
	if (efi.rng_seed == EFI_INVALID_TABLE_ADDR)
		return 0;
	return register_reboot_notifier(&efi_random_seed_nb);
}
late_initcall(register_update_efi_random_seed);
#endif
