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
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/efi.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/kexec.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/ucs2_string.h>
#include <linux/memblock.h>
#include <linux/security.h>

#include <asm/early_ioremap.h>

struct efi __read_mostly efi = {
	.runtime_supported_mask = EFI_RT_SUPPORTED_ALL,
	.acpi			= EFI_INVALID_TABLE_ADDR,
	.acpi20			= EFI_INVALID_TABLE_ADDR,
	.smbios			= EFI_INVALID_TABLE_ADDR,
	.smbios3		= EFI_INVALID_TABLE_ADDR,
	.esrt			= EFI_INVALID_TABLE_ADDR,
	.tpm_log		= EFI_INVALID_TABLE_ADDR,
	.tpm_final_log		= EFI_INVALID_TABLE_ADDR,
};
EXPORT_SYMBOL(efi);

unsigned long __ro_after_init efi_rng_seed = EFI_INVALID_TABLE_ADDR;
static unsigned long __initdata mem_reserve = EFI_INVALID_TABLE_ADDR;
static unsigned long __initdata rt_prop = EFI_INVALID_TABLE_ADDR;

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

bool __pure __efi_soft_reserve_enabled(void)
{
	return !efi_enabled(EFI_MEM_NO_SOFT_RESERVE);
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

	if (parse_option_str(str, "nosoftreserve"))
		set_bit(EFI_MEM_NO_SOFT_RESERVE, &efi.flags);

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

	if (IS_ENABLED(CONFIG_IA64) || IS_ENABLED(CONFIG_X86))
		str = efi_systab_show_arch(str);

	return str - buf;
}

static struct kobj_attribute efi_attr_systab = __ATTR_RO_MODE(systab, 0400);

static ssize_t fw_platform_size_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", efi_enabled(EFI_64BIT) ? 64 : 32);
}

extern __weak struct kobj_attribute efi_attr_fw_vendor;
extern __weak struct kobj_attribute efi_attr_runtime;
extern __weak struct kobj_attribute efi_attr_config_table;
static struct kobj_attribute efi_attr_fw_platform_size =
	__ATTR_RO(fw_platform_size);

static struct attribute *efi_subsys_attrs[] = {
	&efi_attr_systab.attr,
	&efi_attr_fw_platform_size.attr,
	&efi_attr_fw_vendor.attr,
	&efi_attr_runtime.attr,
	&efi_attr_config_table.attr,
	NULL,
};

umode_t __weak efi_attr_is_visible(struct kobject *kobj, struct attribute *attr,
				   int n)
{
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
	int ret = security_locked_down(LOCKDOWN_ACPI_TABLES);

	if (ret)
		return ret;

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

	if (!efivar_ssdt[0])
		return 0;

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

		ret = acpi_load_table(data, NULL);
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

#ifdef CONFIG_DEBUG_FS

#define EFI_DEBUGFS_MAX_BLOBS 32

static struct debugfs_blob_wrapper debugfs_blob[EFI_DEBUGFS_MAX_BLOBS];

static void __init efi_debugfs_init(void)
{
	struct dentry *efi_debugfs;
	efi_memory_desc_t *md;
	char name[32];
	int type_count[EFI_BOOT_SERVICES_DATA + 1] = {};
	int i = 0;

	efi_debugfs = debugfs_create_dir("efi", NULL);
	if (IS_ERR_OR_NULL(efi_debugfs))
		return;

	for_each_efi_memory_desc(md) {
		switch (md->type) {
		case EFI_BOOT_SERVICES_CODE:
			snprintf(name, sizeof(name), "boot_services_code%d",
				 type_count[md->type]++);
			break;
		case EFI_BOOT_SERVICES_DATA:
			snprintf(name, sizeof(name), "boot_services_data%d",
				 type_count[md->type]++);
			break;
		default:
			continue;
		}

		if (i >= EFI_DEBUGFS_MAX_BLOBS) {
			pr_warn("More then %d EFI boot service segments, only showing first %d in debugfs\n",
				EFI_DEBUGFS_MAX_BLOBS, EFI_DEBUGFS_MAX_BLOBS);
			break;
		}

		debugfs_blob[i].size = md->num_pages << EFI_PAGE_SHIFT;
		debugfs_blob[i].data = memremap(md->phys_addr,
						debugfs_blob[i].size,
						MEMREMAP_WB);
		if (!debugfs_blob[i].data)
			continue;

		debugfs_create_blob(name, 0400, efi_debugfs, &debugfs_blob[i]);
		i++;
	}
}
#else
static inline void efi_debugfs_init(void) {}
#endif

/*
 * We register the efi subsystem with the firmware subsystem and the
 * efivars subsystem with the efi subsystem, if the system was booted with
 * EFI.
 */
static int __init efisubsys_init(void)
{
	int error;

	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		efi.runtime_supported_mask = 0;

	if (!efi_enabled(EFI_BOOT))
		return 0;

	if (efi.runtime_supported_mask) {
		/*
		 * Since we process only one efi_runtime_service() at a time, an
		 * ordered workqueue (which creates only one execution context)
		 * should suffice for all our needs.
		 */
		efi_rts_wq = alloc_ordered_workqueue("efi_rts_wq", 0);
		if (!efi_rts_wq) {
			pr_err("Creating efi_rts_wq failed, EFI runtime services disabled.\n");
			clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
			efi.runtime_supported_mask = 0;
			return 0;
		}
	}

	if (efi_rt_services_supported(EFI_RT_SUPPORTED_TIME_SERVICES))
		platform_device_register_simple("rtc-efi", 0, NULL, 0);

	/* We register the efi directory at /sys/firmware/efi */
	efi_kobj = kobject_create_and_add("efi", firmware_kobj);
	if (!efi_kobj) {
		pr_err("efi: Firmware registration failed.\n");
		return -ENOMEM;
	}

	if (efi_rt_services_supported(EFI_RT_SUPPORTED_VARIABLE_SERVICES)) {
		efivar_ssdt_load();
		error = generic_ops_register();
		if (error)
			goto err_put;
		platform_device_register_simple("efivars", 0, NULL, 0);
	}

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

	if (efi_enabled(EFI_DBG) && efi_enabled(EFI_PRESERVE_BS_REGIONS))
		efi_debugfs_init();

	return 0;

err_remove_group:
	sysfs_remove_group(efi_kobj, &efi_subsys_attr_group);
err_unregister:
	if (efi_rt_services_supported(EFI_RT_SUPPORTED_VARIABLE_SERVICES))
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

static const efi_config_table_type_t common_tables[] __initconst = {
	{ACPI_20_TABLE_GUID, "ACPI 2.0", &efi.acpi20},
	{ACPI_TABLE_GUID, "ACPI", &efi.acpi},
	{SMBIOS_TABLE_GUID, "SMBIOS", &efi.smbios},
	{SMBIOS3_TABLE_GUID, "SMBIOS 3.0", &efi.smbios3},
	{EFI_SYSTEM_RESOURCE_TABLE_GUID, "ESRT", &efi.esrt},
	{EFI_MEMORY_ATTRIBUTES_TABLE_GUID, "MEMATTR", &efi_mem_attr_table},
	{LINUX_EFI_RANDOM_SEED_TABLE_GUID, "RNG", &efi_rng_seed},
	{LINUX_EFI_TPM_EVENT_LOG_GUID, "TPMEventLog", &efi.tpm_log},
	{LINUX_EFI_TPM_FINAL_LOG_GUID, "TPMFinalLog", &efi.tpm_final_log},
	{LINUX_EFI_MEMRESERVE_TABLE_GUID, "MEMRESERVE", &mem_reserve},
	{EFI_RT_PROPERTIES_TABLE_GUID, "RTPROP", &rt_prop},
#ifdef CONFIG_EFI_RCI2_TABLE
	{DELLEMC_EFI_RCI2_TABLE_GUID, NULL, &rci2_table_phys},
#endif
	{NULL_GUID, NULL, NULL},
};

static __init int match_config_table(const efi_guid_t *guid,
				     unsigned long table,
				     const efi_config_table_type_t *table_types)
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

int __init efi_config_parse_tables(const efi_config_table_t *config_tables,
				   int count,
				   const efi_config_table_type_t *arch_tables)
{
	const efi_config_table_64_t *tbl64 = (void *)config_tables;
	const efi_config_table_32_t *tbl32 = (void *)config_tables;
	const efi_guid_t *guid;
	unsigned long table;
	int i;

	pr_info("");
	for (i = 0; i < count; i++) {
		if (!IS_ENABLED(CONFIG_X86)) {
			guid = &config_tables[i].guid;
			table = (unsigned long)config_tables[i].table;
		} else if (efi_enabled(EFI_64BIT)) {
			guid = &tbl64[i].guid;
			table = tbl64[i].table;

			if (IS_ENABLED(CONFIG_X86_32) &&
			    tbl64[i].table > U32_MAX) {
				pr_cont("\n");
				pr_err("Table located above 4GB, disabling EFI.\n");
				return -EINVAL;
			}
		} else {
			guid = &tbl32[i].guid;
			table = tbl32[i].table;
		}

		if (!match_config_table(guid, table, common_tables))
			match_config_table(guid, table, arch_tables);
	}
	pr_cont("\n");
	set_bit(EFI_CONFIG_TABLES, &efi.flags);

	if (efi_rng_seed != EFI_INVALID_TABLE_ADDR) {
		struct linux_efi_random_seed *seed;
		u32 size = 0;

		seed = early_memremap(efi_rng_seed, sizeof(*seed));
		if (seed != NULL) {
			size = READ_ONCE(seed->size);
			early_memunmap(seed, sizeof(*seed));
		} else {
			pr_err("Could not map UEFI random seed!\n");
		}
		if (size > 0) {
			seed = early_memremap(efi_rng_seed,
					      sizeof(*seed) + size);
			if (seed != NULL) {
				pr_notice("seeding entropy pool\n");
				add_bootloader_randomness(seed->bits, size);
				early_memunmap(seed, sizeof(*seed) + size);
			} else {
				pr_err("Could not map UEFI random seed!\n");
			}
		}
	}

	if (!IS_ENABLED(CONFIG_X86_32) && efi_enabled(EFI_MEMMAP))
		efi_memattr_init();

	efi_tpm_eventlog_init();

	if (mem_reserve != EFI_INVALID_TABLE_ADDR) {
		unsigned long prsv = mem_reserve;

		while (prsv) {
			struct linux_efi_memreserve *rsv;
			u8 *p;

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

	if (rt_prop != EFI_INVALID_TABLE_ADDR) {
		efi_rt_properties_table_t *tbl;

		tbl = early_memremap(rt_prop, sizeof(*tbl));
		if (tbl) {
			efi.runtime_supported_mask &= tbl->runtime_services_supported;
			early_memunmap(tbl, sizeof(*tbl));
		}
	}

	return 0;
}

int __init efi_systab_check_header(const efi_table_hdr_t *systab_hdr,
				   int min_major_version)
{
	if (systab_hdr->signature != EFI_SYSTEM_TABLE_SIGNATURE) {
		pr_err("System table signature incorrect!\n");
		return -EINVAL;
	}

	if ((systab_hdr->revision >> 16) < min_major_version)
		pr_err("Warning: System table version %d.%02d, expected %d.00 or greater!\n",
		       systab_hdr->revision >> 16,
		       systab_hdr->revision & 0xffff,
		       min_major_version);

	return 0;
}

#ifndef CONFIG_IA64
static const efi_char16_t *__init map_fw_vendor(unsigned long fw_vendor,
						size_t size)
{
	const efi_char16_t *ret;

	ret = early_memremap_ro(fw_vendor, size);
	if (!ret)
		pr_err("Could not map the firmware vendor!\n");
	return ret;
}

static void __init unmap_fw_vendor(const void *fw_vendor, size_t size)
{
	early_memunmap((void *)fw_vendor, size);
}
#else
#define map_fw_vendor(p, s)	__va(p)
#define unmap_fw_vendor(v, s)
#endif

void __init efi_systab_report_header(const efi_table_hdr_t *systab_hdr,
				     unsigned long fw_vendor)
{
	char vendor[100] = "unknown";
	const efi_char16_t *c16;
	size_t i;

	c16 = map_fw_vendor(fw_vendor, sizeof(vendor) * sizeof(efi_char16_t));
	if (c16) {
		for (i = 0; i < sizeof(vendor) - 1 && c16[i]; ++i)
			vendor[i] = c16[i];
		vendor[i] = '\0';

		unmap_fw_vendor(c16, sizeof(vendor) * sizeof(efi_char16_t));
	}

	pr_info("EFI v%u.%.02u by %s\n",
		systab_hdr->revision >> 16,
		systab_hdr->revision & 0xffff,
		vendor);
}

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
		     EFI_MEMORY_NV | EFI_MEMORY_SP |
		     EFI_MEMORY_RUNTIME | EFI_MEMORY_MORE_RELIABLE))
		snprintf(pos, size, "|attr=0x%016llx]",
			 (unsigned long long)attr);
	else
		snprintf(pos, size,
			 "|%3s|%2s|%2s|%2s|%2s|%2s|%2s|%2s|%3s|%2s|%2s|%2s|%2s]",
			 attr & EFI_MEMORY_RUNTIME ? "RUN" : "",
			 attr & EFI_MEMORY_MORE_RELIABLE ? "MR" : "",
			 attr & EFI_MEMORY_SP      ? "SP"  : "",
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
 * map, -EINVAL otherwise.
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

static DEFINE_SPINLOCK(efi_mem_reserve_persistent_lock);
static struct linux_efi_memreserve *efi_memreserve_root __ro_after_init;

static int __init efi_memreserve_map_root(void)
{
	if (mem_reserve == EFI_INVALID_TABLE_ADDR)
		return -ENODEV;

	efi_memreserve_root = memremap(mem_reserve,
				       sizeof(*efi_memreserve_root),
				       MEMREMAP_WB);
	if (WARN_ON_ONCE(!efi_memreserve_root))
		return -ENOMEM;
	return 0;
}

static int efi_mem_reserve_iomem(phys_addr_t addr, u64 size)
{
	struct resource *res, *parent;

	res = kzalloc(sizeof(struct resource), GFP_ATOMIC);
	if (!res)
		return -ENOMEM;

	res->name	= "reserved";
	res->flags	= IORESOURCE_MEM;
	res->start	= addr;
	res->end	= addr + size - 1;

	/* we expect a conflict with a 'System RAM' region */
	parent = request_resource_conflict(&iomem_resource, res);
	return parent ? request_resource(parent, res) : 0;
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
		rsv = memremap(prsv, sizeof(*rsv), MEMREMAP_WB);
		index = atomic_fetch_add_unless(&rsv->count, 1, rsv->size);
		if (index < rsv->size) {
			rsv->entry[index].base = addr;
			rsv->entry[index].size = size;

			memunmap(rsv);
			return efi_mem_reserve_iomem(addr, size);
		}
		memunmap(rsv);
	}

	/* no slot found - allocate a new linked list entry */
	rsv = (struct linux_efi_memreserve *)__get_free_page(GFP_ATOMIC);
	if (!rsv)
		return -ENOMEM;

	rc = efi_mem_reserve_iomem(__pa(rsv), SZ_4K);
	if (rc) {
		free_page((unsigned long)rsv);
		return rc;
	}

	/*
	 * The memremap() call above assumes that a linux_efi_memreserve entry
	 * never crosses a page boundary, so let's ensure that this remains true
	 * even when kexec'ing a 4k pages kernel from a >4k pages kernel, by
	 * using SZ_4K explicitly in the size calculation below.
	 */
	rsv->size = EFI_MEMRESERVE_COUNT(SZ_4K);
	atomic_set(&rsv->count, 1);
	rsv->entry[0].base = addr;
	rsv->entry[0].size = size;

	spin_lock(&efi_mem_reserve_persistent_lock);
	rsv->next = efi_memreserve_root->next;
	efi_memreserve_root->next = __pa(rsv);
	spin_unlock(&efi_mem_reserve_persistent_lock);

	return efi_mem_reserve_iomem(addr, size);
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

	seed = memremap(efi_rng_seed, sizeof(*seed), MEMREMAP_WB);
	if (seed != NULL) {
		size = min(seed->size, EFI_RANDOM_SEED_SIZE);
		memunmap(seed);
	} else {
		pr_err("Could not map UEFI random seed!\n");
	}
	if (size > 0) {
		seed = memremap(efi_rng_seed, sizeof(*seed) + size,
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

static int __init register_update_efi_random_seed(void)
{
	if (efi_rng_seed == EFI_INVALID_TABLE_ADDR)
		return 0;
	return register_reboot_notifier(&efi_random_seed_nb);
}
late_initcall(register_update_efi_random_seed);
#endif
