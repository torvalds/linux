/*
 *  acpi_osl.c - OS-dependent functions ($Revision: 83 $)
 *
 *  Copyright (C) 2000       Andrew Henroid
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (c) 2008 Intel Corporation
 *   Author: Matthew Wilcox <willy@linux.intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/nmi.h>
#include <linux/acpi.h>
#include <linux/acpi_io.h>
#include <linux/efi.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/semaphore.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/processor.h>

#define _COMPONENT		ACPI_OS_SERVICES
ACPI_MODULE_NAME("osl");
#define PREFIX		"ACPI: "
struct acpi_os_dpc {
	acpi_osd_exec_callback function;
	void *context;
	struct work_struct work;
	int wait;
};

#ifdef CONFIG_ACPI_CUSTOM_DSDT
#include CONFIG_ACPI_CUSTOM_DSDT_FILE
#endif

#ifdef ENABLE_DEBUGGER
#include <linux/kdb.h>

/* stuff for debugger support */
int acpi_in_debugger;
EXPORT_SYMBOL(acpi_in_debugger);

extern char line_buf[80];
#endif				/*ENABLE_DEBUGGER */

static int (*__acpi_os_prepare_sleep)(u8 sleep_state, u32 pm1a_ctrl,
				      u32 pm1b_ctrl);

static acpi_osd_handler acpi_irq_handler;
static void *acpi_irq_context;
static struct workqueue_struct *kacpid_wq;
static struct workqueue_struct *kacpi_notify_wq;
static struct workqueue_struct *kacpi_hotplug_wq;

/*
 * This list of permanent mappings is for memory that may be accessed from
 * interrupt context, where we can't do the ioremap().
 */
struct acpi_ioremap {
	struct list_head list;
	void __iomem *virt;
	acpi_physical_address phys;
	acpi_size size;
	unsigned long refcount;
};

static LIST_HEAD(acpi_ioremaps);
static DEFINE_MUTEX(acpi_ioremap_lock);

static void __init acpi_osi_setup_late(void);

/*
 * The story of _OSI(Linux)
 *
 * From pre-history through Linux-2.6.22,
 * Linux responded TRUE upon a BIOS OSI(Linux) query.
 *
 * Unfortunately, reference BIOS writers got wind of this
 * and put OSI(Linux) in their example code, quickly exposing
 * this string as ill-conceived and opening the door to
 * an un-bounded number of BIOS incompatibilities.
 *
 * For example, OSI(Linux) was used on resume to re-POST a
 * video card on one system, because Linux at that time
 * could not do a speedy restore in its native driver.
 * But then upon gaining quick native restore capability,
 * Linux has no way to tell the BIOS to skip the time-consuming
 * POST -- putting Linux at a permanent performance disadvantage.
 * On another system, the BIOS writer used OSI(Linux)
 * to infer native OS support for IPMI!  On other systems,
 * OSI(Linux) simply got in the way of Linux claiming to
 * be compatible with other operating systems, exposing
 * BIOS issues such as skipped device initialization.
 *
 * So "Linux" turned out to be a really poor chose of
 * OSI string, and from Linux-2.6.23 onward we respond FALSE.
 *
 * BIOS writers should NOT query _OSI(Linux) on future systems.
 * Linux will complain on the console when it sees it, and return FALSE.
 * To get Linux to return TRUE for your system  will require
 * a kernel source update to add a DMI entry,
 * or boot with "acpi_osi=Linux"
 */

static struct osi_linux {
	unsigned int	enable:1;
	unsigned int	dmi:1;
	unsigned int	cmdline:1;
} osi_linux = {0, 0, 0};

static u32 acpi_osi_handler(acpi_string interface, u32 supported)
{
	if (!strcmp("Linux", interface)) {

		printk_once(KERN_NOTICE FW_BUG PREFIX
			"BIOS _OSI(Linux) query %s%s\n",
			osi_linux.enable ? "honored" : "ignored",
			osi_linux.cmdline ? " via cmdline" :
			osi_linux.dmi ? " via DMI" : "");
	}

	return supported;
}

static void __init acpi_request_region (struct acpi_generic_address *gas,
	unsigned int length, char *desc)
{
	u64 addr;

	/* Handle possible alignment issues */
	memcpy(&addr, &gas->address, sizeof(addr));
	if (!addr || !length)
		return;

	/* Resources are never freed */
	if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_IO)
		request_region(addr, length, desc);
	else if (gas->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		request_mem_region(addr, length, desc);
}

static int __init acpi_reserve_resources(void)
{
	acpi_request_region(&acpi_gbl_FADT.xpm1a_event_block, acpi_gbl_FADT.pm1_event_length,
		"ACPI PM1a_EVT_BLK");

	acpi_request_region(&acpi_gbl_FADT.xpm1b_event_block, acpi_gbl_FADT.pm1_event_length,
		"ACPI PM1b_EVT_BLK");

	acpi_request_region(&acpi_gbl_FADT.xpm1a_control_block, acpi_gbl_FADT.pm1_control_length,
		"ACPI PM1a_CNT_BLK");

	acpi_request_region(&acpi_gbl_FADT.xpm1b_control_block, acpi_gbl_FADT.pm1_control_length,
		"ACPI PM1b_CNT_BLK");

	if (acpi_gbl_FADT.pm_timer_length == 4)
		acpi_request_region(&acpi_gbl_FADT.xpm_timer_block, 4, "ACPI PM_TMR");

	acpi_request_region(&acpi_gbl_FADT.xpm2_control_block, acpi_gbl_FADT.pm2_control_length,
		"ACPI PM2_CNT_BLK");

	/* Length of GPE blocks must be a non-negative multiple of 2 */

	if (!(acpi_gbl_FADT.gpe0_block_length & 0x1))
		acpi_request_region(&acpi_gbl_FADT.xgpe0_block,
			       acpi_gbl_FADT.gpe0_block_length, "ACPI GPE0_BLK");

	if (!(acpi_gbl_FADT.gpe1_block_length & 0x1))
		acpi_request_region(&acpi_gbl_FADT.xgpe1_block,
			       acpi_gbl_FADT.gpe1_block_length, "ACPI GPE1_BLK");

	return 0;
}
device_initcall(acpi_reserve_resources);

void acpi_os_printf(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	acpi_os_vprintf(fmt, args);
	va_end(args);
}

void acpi_os_vprintf(const char *fmt, va_list args)
{
	static char buffer[512];

	vsprintf(buffer, fmt, args);

#ifdef ENABLE_DEBUGGER
	if (acpi_in_debugger) {
		kdb_printf("%s", buffer);
	} else {
		printk(KERN_CONT "%s", buffer);
	}
#else
	printk(KERN_CONT "%s", buffer);
#endif
}

#ifdef CONFIG_KEXEC
static unsigned long acpi_rsdp;
static int __init setup_acpi_rsdp(char *arg)
{
	acpi_rsdp = simple_strtoul(arg, NULL, 16);
	return 0;
}
early_param("acpi_rsdp", setup_acpi_rsdp);
#endif

acpi_physical_address __init acpi_os_get_root_pointer(void)
{
#ifdef CONFIG_KEXEC
	if (acpi_rsdp)
		return acpi_rsdp;
#endif

	if (efi_enabled(EFI_CONFIG_TABLES)) {
		if (efi.acpi20 != EFI_INVALID_TABLE_ADDR)
			return efi.acpi20;
		else if (efi.acpi != EFI_INVALID_TABLE_ADDR)
			return efi.acpi;
		else {
			printk(KERN_ERR PREFIX
			       "System description tables not found\n");
			return 0;
		}
	} else {
		acpi_physical_address pa = 0;

		acpi_find_root_pointer(&pa);
		return pa;
	}
}

/* Must be called with 'acpi_ioremap_lock' or RCU read lock held. */
static struct acpi_ioremap *
acpi_map_lookup(acpi_physical_address phys, acpi_size size)
{
	struct acpi_ioremap *map;

	list_for_each_entry_rcu(map, &acpi_ioremaps, list)
		if (map->phys <= phys &&
		    phys + size <= map->phys + map->size)
			return map;

	return NULL;
}

/* Must be called with 'acpi_ioremap_lock' or RCU read lock held. */
static void __iomem *
acpi_map_vaddr_lookup(acpi_physical_address phys, unsigned int size)
{
	struct acpi_ioremap *map;

	map = acpi_map_lookup(phys, size);
	if (map)
		return map->virt + (phys - map->phys);

	return NULL;
}

void __iomem *acpi_os_get_iomem(acpi_physical_address phys, unsigned int size)
{
	struct acpi_ioremap *map;
	void __iomem *virt = NULL;

	mutex_lock(&acpi_ioremap_lock);
	map = acpi_map_lookup(phys, size);
	if (map) {
		virt = map->virt + (phys - map->phys);
		map->refcount++;
	}
	mutex_unlock(&acpi_ioremap_lock);
	return virt;
}
EXPORT_SYMBOL_GPL(acpi_os_get_iomem);

/* Must be called with 'acpi_ioremap_lock' or RCU read lock held. */
static struct acpi_ioremap *
acpi_map_lookup_virt(void __iomem *virt, acpi_size size)
{
	struct acpi_ioremap *map;

	list_for_each_entry_rcu(map, &acpi_ioremaps, list)
		if (map->virt <= virt &&
		    virt + size <= map->virt + map->size)
			return map;

	return NULL;
}

#ifndef CONFIG_IA64
#define should_use_kmap(pfn)   page_is_ram(pfn)
#else
/* ioremap will take care of cache attributes */
#define should_use_kmap(pfn)   0
#endif

static void __iomem *acpi_map(acpi_physical_address pg_off, unsigned long pg_sz)
{
	unsigned long pfn;

	pfn = pg_off >> PAGE_SHIFT;
	if (should_use_kmap(pfn)) {
		if (pg_sz > PAGE_SIZE)
			return NULL;
		return (void __iomem __force *)kmap(pfn_to_page(pfn));
	} else
		return acpi_os_ioremap(pg_off, pg_sz);
}

static void acpi_unmap(acpi_physical_address pg_off, void __iomem *vaddr)
{
	unsigned long pfn;

	pfn = pg_off >> PAGE_SHIFT;
	if (should_use_kmap(pfn))
		kunmap(pfn_to_page(pfn));
	else
		iounmap(vaddr);
}

void __iomem *__init_refok
acpi_os_map_memory(acpi_physical_address phys, acpi_size size)
{
	struct acpi_ioremap *map;
	void __iomem *virt;
	acpi_physical_address pg_off;
	acpi_size pg_sz;

	if (phys > ULONG_MAX) {
		printk(KERN_ERR PREFIX "Cannot map memory that high\n");
		return NULL;
	}

	if (!acpi_gbl_permanent_mmap)
		return __acpi_map_table((unsigned long)phys, size);

	mutex_lock(&acpi_ioremap_lock);
	/* Check if there's a suitable mapping already. */
	map = acpi_map_lookup(phys, size);
	if (map) {
		map->refcount++;
		goto out;
	}

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map) {
		mutex_unlock(&acpi_ioremap_lock);
		return NULL;
	}

	pg_off = round_down(phys, PAGE_SIZE);
	pg_sz = round_up(phys + size, PAGE_SIZE) - pg_off;
	virt = acpi_map(pg_off, pg_sz);
	if (!virt) {
		mutex_unlock(&acpi_ioremap_lock);
		kfree(map);
		return NULL;
	}

	INIT_LIST_HEAD(&map->list);
	map->virt = virt;
	map->phys = pg_off;
	map->size = pg_sz;
	map->refcount = 1;

	list_add_tail_rcu(&map->list, &acpi_ioremaps);

 out:
	mutex_unlock(&acpi_ioremap_lock);
	return map->virt + (phys - map->phys);
}
EXPORT_SYMBOL_GPL(acpi_os_map_memory);

static void acpi_os_drop_map_ref(struct acpi_ioremap *map)
{
	if (!--map->refcount)
		list_del_rcu(&map->list);
}

static void acpi_os_map_cleanup(struct acpi_ioremap *map)
{
	if (!map->refcount) {
		synchronize_rcu();
		acpi_unmap(map->phys, map->virt);
		kfree(map);
	}
}

void __ref acpi_os_unmap_memory(void __iomem *virt, acpi_size size)
{
	struct acpi_ioremap *map;

	if (!acpi_gbl_permanent_mmap) {
		__acpi_unmap_table(virt, size);
		return;
	}

	mutex_lock(&acpi_ioremap_lock);
	map = acpi_map_lookup_virt(virt, size);
	if (!map) {
		mutex_unlock(&acpi_ioremap_lock);
		WARN(true, PREFIX "%s: bad address %p\n", __func__, virt);
		return;
	}
	acpi_os_drop_map_ref(map);
	mutex_unlock(&acpi_ioremap_lock);

	acpi_os_map_cleanup(map);
}
EXPORT_SYMBOL_GPL(acpi_os_unmap_memory);

void __init early_acpi_os_unmap_memory(void __iomem *virt, acpi_size size)
{
	if (!acpi_gbl_permanent_mmap)
		__acpi_unmap_table(virt, size);
}

int acpi_os_map_generic_address(struct acpi_generic_address *gas)
{
	u64 addr;
	void __iomem *virt;

	if (gas->space_id != ACPI_ADR_SPACE_SYSTEM_MEMORY)
		return 0;

	/* Handle possible alignment issues */
	memcpy(&addr, &gas->address, sizeof(addr));
	if (!addr || !gas->bit_width)
		return -EINVAL;

	virt = acpi_os_map_memory(addr, gas->bit_width / 8);
	if (!virt)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL(acpi_os_map_generic_address);

void acpi_os_unmap_generic_address(struct acpi_generic_address *gas)
{
	u64 addr;
	struct acpi_ioremap *map;

	if (gas->space_id != ACPI_ADR_SPACE_SYSTEM_MEMORY)
		return;

	/* Handle possible alignment issues */
	memcpy(&addr, &gas->address, sizeof(addr));
	if (!addr || !gas->bit_width)
		return;

	mutex_lock(&acpi_ioremap_lock);
	map = acpi_map_lookup(addr, gas->bit_width / 8);
	if (!map) {
		mutex_unlock(&acpi_ioremap_lock);
		return;
	}
	acpi_os_drop_map_ref(map);
	mutex_unlock(&acpi_ioremap_lock);

	acpi_os_map_cleanup(map);
}
EXPORT_SYMBOL(acpi_os_unmap_generic_address);

#ifdef ACPI_FUTURE_USAGE
acpi_status
acpi_os_get_physical_address(void *virt, acpi_physical_address * phys)
{
	if (!phys || !virt)
		return AE_BAD_PARAMETER;

	*phys = virt_to_phys(virt);

	return AE_OK;
}
#endif

#define ACPI_MAX_OVERRIDE_LEN 100

static char acpi_os_name[ACPI_MAX_OVERRIDE_LEN];

acpi_status
acpi_os_predefined_override(const struct acpi_predefined_names *init_val,
			    acpi_string * new_val)
{
	if (!init_val || !new_val)
		return AE_BAD_PARAMETER;

	*new_val = NULL;
	if (!memcmp(init_val->name, "_OS_", 4) && strlen(acpi_os_name)) {
		printk(KERN_INFO PREFIX "Overriding _OS definition to '%s'\n",
		       acpi_os_name);
		*new_val = acpi_os_name;
	}

	return AE_OK;
}

#ifdef CONFIG_ACPI_INITRD_TABLE_OVERRIDE
#include <linux/earlycpio.h>
#include <linux/memblock.h>

static u64 acpi_tables_addr;
static int all_tables_size;

/* Copied from acpica/tbutils.c:acpi_tb_checksum() */
u8 __init acpi_table_checksum(u8 *buffer, u32 length)
{
	u8 sum = 0;
	u8 *end = buffer + length;

	while (buffer < end)
		sum = (u8) (sum + *(buffer++));
	return sum;
}

/* All but ACPI_SIG_RSDP and ACPI_SIG_FACS: */
static const char * const table_sigs[] = {
	ACPI_SIG_BERT, ACPI_SIG_CPEP, ACPI_SIG_ECDT, ACPI_SIG_EINJ,
	ACPI_SIG_ERST, ACPI_SIG_HEST, ACPI_SIG_MADT, ACPI_SIG_MSCT,
	ACPI_SIG_SBST, ACPI_SIG_SLIT, ACPI_SIG_SRAT, ACPI_SIG_ASF,
	ACPI_SIG_BOOT, ACPI_SIG_DBGP, ACPI_SIG_DMAR, ACPI_SIG_HPET,
	ACPI_SIG_IBFT, ACPI_SIG_IVRS, ACPI_SIG_MCFG, ACPI_SIG_MCHI,
	ACPI_SIG_SLIC, ACPI_SIG_SPCR, ACPI_SIG_SPMI, ACPI_SIG_TCPA,
	ACPI_SIG_UEFI, ACPI_SIG_WAET, ACPI_SIG_WDAT, ACPI_SIG_WDDT,
	ACPI_SIG_WDRT, ACPI_SIG_DSDT, ACPI_SIG_FADT, ACPI_SIG_PSDT,
	ACPI_SIG_RSDT, ACPI_SIG_XSDT, ACPI_SIG_SSDT, NULL };

/* Non-fatal errors: Affected tables/files are ignored */
#define INVALID_TABLE(x, path, name)					\
	{ pr_err("ACPI OVERRIDE: " x " [%s%s]\n", path, name); continue; }

#define ACPI_HEADER_SIZE sizeof(struct acpi_table_header)

/* Must not increase 10 or needs code modification below */
#define ACPI_OVERRIDE_TABLES 10

void __init acpi_initrd_override(void *data, size_t size)
{
	int sig, no, table_nr = 0, total_offset = 0;
	long offset = 0;
	struct acpi_table_header *table;
	char cpio_path[32] = "kernel/firmware/acpi/";
	struct cpio_data file;
	struct cpio_data early_initrd_files[ACPI_OVERRIDE_TABLES];
	char *p;

	if (data == NULL || size == 0)
		return;

	for (no = 0; no < ACPI_OVERRIDE_TABLES; no++) {
		file = find_cpio_data(cpio_path, data, size, &offset);
		if (!file.data)
			break;

		data += offset;
		size -= offset;

		if (file.size < sizeof(struct acpi_table_header))
			INVALID_TABLE("Table smaller than ACPI header",
				      cpio_path, file.name);

		table = file.data;

		for (sig = 0; table_sigs[sig]; sig++)
			if (!memcmp(table->signature, table_sigs[sig], 4))
				break;

		if (!table_sigs[sig])
			INVALID_TABLE("Unknown signature",
				      cpio_path, file.name);
		if (file.size != table->length)
			INVALID_TABLE("File length does not match table length",
				      cpio_path, file.name);
		if (acpi_table_checksum(file.data, table->length))
			INVALID_TABLE("Bad table checksum",
				      cpio_path, file.name);

		pr_info("%4.4s ACPI table found in initrd [%s%s][0x%x]\n",
			table->signature, cpio_path, file.name, table->length);

		all_tables_size += table->length;
		early_initrd_files[table_nr].data = file.data;
		early_initrd_files[table_nr].size = file.size;
		table_nr++;
	}
	if (table_nr == 0)
		return;

	acpi_tables_addr =
		memblock_find_in_range(0, max_low_pfn_mapped << PAGE_SHIFT,
				       all_tables_size, PAGE_SIZE);
	if (!acpi_tables_addr) {
		WARN_ON(1);
		return;
	}
	/*
	 * Only calling e820_add_reserve does not work and the
	 * tables are invalid (memory got used) later.
	 * memblock_reserve works as expected and the tables won't get modified.
	 * But it's not enough on X86 because ioremap will
	 * complain later (used by acpi_os_map_memory) that the pages
	 * that should get mapped are not marked "reserved".
	 * Both memblock_reserve and e820_add_region (via arch_reserve_mem_area)
	 * works fine.
	 */
	memblock_reserve(acpi_tables_addr, all_tables_size);
	arch_reserve_mem_area(acpi_tables_addr, all_tables_size);

	p = early_ioremap(acpi_tables_addr, all_tables_size);

	for (no = 0; no < table_nr; no++) {
		memcpy(p + total_offset, early_initrd_files[no].data,
		       early_initrd_files[no].size);
		total_offset += early_initrd_files[no].size;
	}
	early_iounmap(p, all_tables_size);
}
#endif /* CONFIG_ACPI_INITRD_TABLE_OVERRIDE */

static void acpi_table_taint(struct acpi_table_header *table)
{
	pr_warn(PREFIX
		"Override [%4.4s-%8.8s], this is unsafe: tainting kernel\n",
		table->signature, table->oem_table_id);
	add_taint(TAINT_OVERRIDDEN_ACPI_TABLE, LOCKDEP_NOW_UNRELIABLE);
}


acpi_status
acpi_os_table_override(struct acpi_table_header * existing_table,
		       struct acpi_table_header ** new_table)
{
	if (!existing_table || !new_table)
		return AE_BAD_PARAMETER;

	*new_table = NULL;

#ifdef CONFIG_ACPI_CUSTOM_DSDT
	if (strncmp(existing_table->signature, "DSDT", 4) == 0)
		*new_table = (struct acpi_table_header *)AmlCode;
#endif
	if (*new_table != NULL)
		acpi_table_taint(existing_table);
	return AE_OK;
}

acpi_status
acpi_os_physical_table_override(struct acpi_table_header *existing_table,
				acpi_physical_address *address,
				u32 *table_length)
{
#ifndef CONFIG_ACPI_INITRD_TABLE_OVERRIDE
	*table_length = 0;
	*address = 0;
	return AE_OK;
#else
	int table_offset = 0;
	struct acpi_table_header *table;

	*table_length = 0;
	*address = 0;

	if (!acpi_tables_addr)
		return AE_OK;

	do {
		if (table_offset + ACPI_HEADER_SIZE > all_tables_size) {
			WARN_ON(1);
			return AE_OK;
		}

		table = acpi_os_map_memory(acpi_tables_addr + table_offset,
					   ACPI_HEADER_SIZE);

		if (table_offset + table->length > all_tables_size) {
			acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
			WARN_ON(1);
			return AE_OK;
		}

		table_offset += table->length;

		if (memcmp(existing_table->signature, table->signature, 4)) {
			acpi_os_unmap_memory(table,
				     ACPI_HEADER_SIZE);
			continue;
		}

		/* Only override tables with matching oem id */
		if (memcmp(table->oem_table_id, existing_table->oem_table_id,
			   ACPI_OEM_TABLE_ID_SIZE)) {
			acpi_os_unmap_memory(table,
				     ACPI_HEADER_SIZE);
			continue;
		}

		table_offset -= table->length;
		*table_length = table->length;
		acpi_os_unmap_memory(table, ACPI_HEADER_SIZE);
		*address = acpi_tables_addr + table_offset;
		break;
	} while (table_offset + ACPI_HEADER_SIZE < all_tables_size);

	if (*address != 0)
		acpi_table_taint(existing_table);
	return AE_OK;
#endif
}

static irqreturn_t acpi_irq(int irq, void *dev_id)
{
	u32 handled;

	handled = (*acpi_irq_handler) (acpi_irq_context);

	if (handled) {
		acpi_irq_handled++;
		return IRQ_HANDLED;
	} else {
		acpi_irq_not_handled++;
		return IRQ_NONE;
	}
}

acpi_status
acpi_os_install_interrupt_handler(u32 gsi, acpi_osd_handler handler,
				  void *context)
{
	unsigned int irq;

	acpi_irq_stats_init();

	/*
	 * ACPI interrupts different from the SCI in our copy of the FADT are
	 * not supported.
	 */
	if (gsi != acpi_gbl_FADT.sci_interrupt)
		return AE_BAD_PARAMETER;

	if (acpi_irq_handler)
		return AE_ALREADY_ACQUIRED;

	if (acpi_gsi_to_irq(gsi, &irq) < 0) {
		printk(KERN_ERR PREFIX "SCI (ACPI GSI %d) not registered\n",
		       gsi);
		return AE_OK;
	}

	acpi_irq_handler = handler;
	acpi_irq_context = context;
	if (request_irq(irq, acpi_irq, IRQF_SHARED | IRQF_NO_SUSPEND, "acpi", acpi_irq)) {
		printk(KERN_ERR PREFIX "SCI (IRQ%d) allocation failed\n", irq);
		acpi_irq_handler = NULL;
		return AE_NOT_ACQUIRED;
	}

	return AE_OK;
}

acpi_status acpi_os_remove_interrupt_handler(u32 irq, acpi_osd_handler handler)
{
	if (irq != acpi_gbl_FADT.sci_interrupt)
		return AE_BAD_PARAMETER;

	free_irq(irq, acpi_irq);
	acpi_irq_handler = NULL;

	return AE_OK;
}

/*
 * Running in interpreter thread context, safe to sleep
 */

void acpi_os_sleep(u64 ms)
{
	schedule_timeout_interruptible(msecs_to_jiffies(ms));
}

void acpi_os_stall(u32 us)
{
	while (us) {
		u32 delay = 1000;

		if (delay > us)
			delay = us;
		udelay(delay);
		touch_nmi_watchdog();
		us -= delay;
	}
}

/*
 * Support ACPI 3.0 AML Timer operand
 * Returns 64-bit free-running, monotonically increasing timer
 * with 100ns granularity
 */
u64 acpi_os_get_timer(void)
{
	u64 time_ns = ktime_to_ns(ktime_get());
	do_div(time_ns, 100);
	return time_ns;
}

acpi_status acpi_os_read_port(acpi_io_address port, u32 * value, u32 width)
{
	u32 dummy;

	if (!value)
		value = &dummy;

	*value = 0;
	if (width <= 8) {
		*(u8 *) value = inb(port);
	} else if (width <= 16) {
		*(u16 *) value = inw(port);
	} else if (width <= 32) {
		*(u32 *) value = inl(port);
	} else {
		BUG();
	}

	return AE_OK;
}

EXPORT_SYMBOL(acpi_os_read_port);

acpi_status acpi_os_write_port(acpi_io_address port, u32 value, u32 width)
{
	if (width <= 8) {
		outb(value, port);
	} else if (width <= 16) {
		outw(value, port);
	} else if (width <= 32) {
		outl(value, port);
	} else {
		BUG();
	}

	return AE_OK;
}

EXPORT_SYMBOL(acpi_os_write_port);

#ifdef readq
static inline u64 read64(const volatile void __iomem *addr)
{
	return readq(addr);
}
#else
static inline u64 read64(const volatile void __iomem *addr)
{
	u64 l, h;
	l = readl(addr);
	h = readl(addr+4);
	return l | (h << 32);
}
#endif

acpi_status
acpi_os_read_memory(acpi_physical_address phys_addr, u64 *value, u32 width)
{
	void __iomem *virt_addr;
	unsigned int size = width / 8;
	bool unmap = false;
	u64 dummy;

	rcu_read_lock();
	virt_addr = acpi_map_vaddr_lookup(phys_addr, size);
	if (!virt_addr) {
		rcu_read_unlock();
		virt_addr = acpi_os_ioremap(phys_addr, size);
		if (!virt_addr)
			return AE_BAD_ADDRESS;
		unmap = true;
	}

	if (!value)
		value = &dummy;

	switch (width) {
	case 8:
		*(u8 *) value = readb(virt_addr);
		break;
	case 16:
		*(u16 *) value = readw(virt_addr);
		break;
	case 32:
		*(u32 *) value = readl(virt_addr);
		break;
	case 64:
		*(u64 *) value = read64(virt_addr);
		break;
	default:
		BUG();
	}

	if (unmap)
		iounmap(virt_addr);
	else
		rcu_read_unlock();

	return AE_OK;
}

#ifdef writeq
static inline void write64(u64 val, volatile void __iomem *addr)
{
	writeq(val, addr);
}
#else
static inline void write64(u64 val, volatile void __iomem *addr)
{
	writel(val, addr);
	writel(val>>32, addr+4);
}
#endif

acpi_status
acpi_os_write_memory(acpi_physical_address phys_addr, u64 value, u32 width)
{
	void __iomem *virt_addr;
	unsigned int size = width / 8;
	bool unmap = false;

	rcu_read_lock();
	virt_addr = acpi_map_vaddr_lookup(phys_addr, size);
	if (!virt_addr) {
		rcu_read_unlock();
		virt_addr = acpi_os_ioremap(phys_addr, size);
		if (!virt_addr)
			return AE_BAD_ADDRESS;
		unmap = true;
	}

	switch (width) {
	case 8:
		writeb(value, virt_addr);
		break;
	case 16:
		writew(value, virt_addr);
		break;
	case 32:
		writel(value, virt_addr);
		break;
	case 64:
		write64(value, virt_addr);
		break;
	default:
		BUG();
	}

	if (unmap)
		iounmap(virt_addr);
	else
		rcu_read_unlock();

	return AE_OK;
}

acpi_status
acpi_os_read_pci_configuration(struct acpi_pci_id * pci_id, u32 reg,
			       u64 *value, u32 width)
{
	int result, size;
	u32 value32;

	if (!value)
		return AE_BAD_PARAMETER;

	switch (width) {
	case 8:
		size = 1;
		break;
	case 16:
		size = 2;
		break;
	case 32:
		size = 4;
		break;
	default:
		return AE_ERROR;
	}

	result = raw_pci_read(pci_id->segment, pci_id->bus,
				PCI_DEVFN(pci_id->device, pci_id->function),
				reg, size, &value32);
	*value = value32;

	return (result ? AE_ERROR : AE_OK);
}

acpi_status
acpi_os_write_pci_configuration(struct acpi_pci_id * pci_id, u32 reg,
				u64 value, u32 width)
{
	int result, size;

	switch (width) {
	case 8:
		size = 1;
		break;
	case 16:
		size = 2;
		break;
	case 32:
		size = 4;
		break;
	default:
		return AE_ERROR;
	}

	result = raw_pci_write(pci_id->segment, pci_id->bus,
				PCI_DEVFN(pci_id->device, pci_id->function),
				reg, size, value);

	return (result ? AE_ERROR : AE_OK);
}

static void acpi_os_execute_deferred(struct work_struct *work)
{
	struct acpi_os_dpc *dpc = container_of(work, struct acpi_os_dpc, work);

	if (dpc->wait)
		acpi_os_wait_events_complete();

	dpc->function(dpc->context);
	kfree(dpc);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_execute
 *
 * PARAMETERS:  Type               - Type of the callback
 *              Function           - Function to be executed
 *              Context            - Function parameters
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Depending on type, either queues function for deferred execution or
 *              immediately executes function on a separate thread.
 *
 ******************************************************************************/

static acpi_status __acpi_os_execute(acpi_execute_type type,
	acpi_osd_exec_callback function, void *context, int hp)
{
	acpi_status status = AE_OK;
	struct acpi_os_dpc *dpc;
	struct workqueue_struct *queue;
	int ret;
	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Scheduling function [%p(%p)] for deferred execution.\n",
			  function, context));

	/*
	 * Allocate/initialize DPC structure.  Note that this memory will be
	 * freed by the callee.  The kernel handles the work_struct list  in a
	 * way that allows us to also free its memory inside the callee.
	 * Because we may want to schedule several tasks with different
	 * parameters we can't use the approach some kernel code uses of
	 * having a static work_struct.
	 */

	dpc = kzalloc(sizeof(struct acpi_os_dpc), GFP_ATOMIC);
	if (!dpc)
		return AE_NO_MEMORY;

	dpc->function = function;
	dpc->context = context;

	/*
	 * We can't run hotplug code in keventd_wq/kacpid_wq/kacpid_notify_wq
	 * because the hotplug code may call driver .remove() functions,
	 * which invoke flush_scheduled_work/acpi_os_wait_events_complete
	 * to flush these workqueues.
	 *
	 * To prevent lockdep from complaining unnecessarily, make sure that
	 * there is a different static lockdep key for each workqueue by using
	 * INIT_WORK() for each of them separately.
	 */
	if (hp) {
		queue = kacpi_hotplug_wq;
		dpc->wait = 1;
		INIT_WORK(&dpc->work, acpi_os_execute_deferred);
	} else if (type == OSL_NOTIFY_HANDLER) {
		queue = kacpi_notify_wq;
		INIT_WORK(&dpc->work, acpi_os_execute_deferred);
	} else {
		queue = kacpid_wq;
		INIT_WORK(&dpc->work, acpi_os_execute_deferred);
	}

	/*
	 * On some machines, a software-initiated SMI causes corruption unless
	 * the SMI runs on CPU 0.  An SMI can be initiated by any AML, but
	 * typically it's done in GPE-related methods that are run via
	 * workqueues, so we can avoid the known corruption cases by always
	 * queueing on CPU 0.
	 */
	ret = queue_work_on(0, queue, &dpc->work);

	if (!ret) {
		printk(KERN_ERR PREFIX
			  "Call to queue_work() failed.\n");
		status = AE_ERROR;
		kfree(dpc);
	}
	return status;
}

acpi_status acpi_os_execute(acpi_execute_type type,
			    acpi_osd_exec_callback function, void *context)
{
	return __acpi_os_execute(type, function, context, 0);
}
EXPORT_SYMBOL(acpi_os_execute);

acpi_status acpi_os_hotplug_execute(acpi_osd_exec_callback function,
	void *context)
{
	return __acpi_os_execute(0, function, context, 1);
}
EXPORT_SYMBOL(acpi_os_hotplug_execute);

void acpi_os_wait_events_complete(void)
{
	flush_workqueue(kacpid_wq);
	flush_workqueue(kacpi_notify_wq);
}

EXPORT_SYMBOL(acpi_os_wait_events_complete);

acpi_status
acpi_os_create_semaphore(u32 max_units, u32 initial_units, acpi_handle * handle)
{
	struct semaphore *sem = NULL;

	sem = acpi_os_allocate(sizeof(struct semaphore));
	if (!sem)
		return AE_NO_MEMORY;
	memset(sem, 0, sizeof(struct semaphore));

	sema_init(sem, initial_units);

	*handle = (acpi_handle *) sem;

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Creating semaphore[%p|%d].\n",
			  *handle, initial_units));

	return AE_OK;
}

/*
 * TODO: A better way to delete semaphores?  Linux doesn't have a
 * 'delete_semaphore()' function -- may result in an invalid
 * pointer dereference for non-synchronized consumers.	Should
 * we at least check for blocked threads and signal/cancel them?
 */

acpi_status acpi_os_delete_semaphore(acpi_handle handle)
{
	struct semaphore *sem = (struct semaphore *)handle;

	if (!sem)
		return AE_BAD_PARAMETER;

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Deleting semaphore[%p].\n", handle));

	BUG_ON(!list_empty(&sem->wait_list));
	kfree(sem);
	sem = NULL;

	return AE_OK;
}

/*
 * TODO: Support for units > 1?
 */
acpi_status acpi_os_wait_semaphore(acpi_handle handle, u32 units, u16 timeout)
{
	acpi_status status = AE_OK;
	struct semaphore *sem = (struct semaphore *)handle;
	long jiffies;
	int ret = 0;

	if (!sem || (units < 1))
		return AE_BAD_PARAMETER;

	if (units > 1)
		return AE_SUPPORT;

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Waiting for semaphore[%p|%d|%d]\n",
			  handle, units, timeout));

	if (timeout == ACPI_WAIT_FOREVER)
		jiffies = MAX_SCHEDULE_TIMEOUT;
	else
		jiffies = msecs_to_jiffies(timeout);
	
	ret = down_timeout(sem, jiffies);
	if (ret)
		status = AE_TIME;

	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
				  "Failed to acquire semaphore[%p|%d|%d], %s",
				  handle, units, timeout,
				  acpi_format_exception(status)));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
				  "Acquired semaphore[%p|%d|%d]", handle,
				  units, timeout));
	}

	return status;
}

/*
 * TODO: Support for units > 1?
 */
acpi_status acpi_os_signal_semaphore(acpi_handle handle, u32 units)
{
	struct semaphore *sem = (struct semaphore *)handle;

	if (!sem || (units < 1))
		return AE_BAD_PARAMETER;

	if (units > 1)
		return AE_SUPPORT;

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Signaling semaphore[%p|%d]\n", handle,
			  units));

	up(sem);

	return AE_OK;
}

#ifdef ACPI_FUTURE_USAGE
u32 acpi_os_get_line(char *buffer)
{

#ifdef ENABLE_DEBUGGER
	if (acpi_in_debugger) {
		u32 chars;

		kdb_read(buffer, sizeof(line_buf));

		/* remove the CR kdb includes */
		chars = strlen(buffer) - 1;
		buffer[chars] = '\0';
	}
#endif

	return 0;
}
#endif				/*  ACPI_FUTURE_USAGE  */

acpi_status acpi_os_signal(u32 function, void *info)
{
	switch (function) {
	case ACPI_SIGNAL_FATAL:
		printk(KERN_ERR PREFIX "Fatal opcode executed\n");
		break;
	case ACPI_SIGNAL_BREAKPOINT:
		/*
		 * AML Breakpoint
		 * ACPI spec. says to treat it as a NOP unless
		 * you are debugging.  So if/when we integrate
		 * AML debugger into the kernel debugger its
		 * hook will go here.  But until then it is
		 * not useful to print anything on breakpoints.
		 */
		break;
	default:
		break;
	}

	return AE_OK;
}

static int __init acpi_os_name_setup(char *str)
{
	char *p = acpi_os_name;
	int count = ACPI_MAX_OVERRIDE_LEN - 1;

	if (!str || !*str)
		return 0;

	for (; count-- && str && *str; str++) {
		if (isalnum(*str) || *str == ' ' || *str == ':')
			*p++ = *str;
		else if (*str == '\'' || *str == '"')
			continue;
		else
			break;
	}
	*p = 0;

	return 1;

}

__setup("acpi_os_name=", acpi_os_name_setup);

#define	OSI_STRING_LENGTH_MAX 64	/* arbitrary */
#define	OSI_STRING_ENTRIES_MAX 16	/* arbitrary */

struct osi_setup_entry {
	char string[OSI_STRING_LENGTH_MAX];
	bool enable;
};

static struct osi_setup_entry __initdata
		osi_setup_entries[OSI_STRING_ENTRIES_MAX] = {
	{"Module Device", true},
	{"Processor Device", true},
	{"3.0 _SCP Extensions", true},
	{"Processor Aggregator Device", true},
};

void __init acpi_osi_setup(char *str)
{
	struct osi_setup_entry *osi;
	bool enable = true;
	int i;

	if (!acpi_gbl_create_osi_method)
		return;

	if (str == NULL || *str == '\0') {
		printk(KERN_INFO PREFIX "_OSI method disabled\n");
		acpi_gbl_create_osi_method = FALSE;
		return;
	}

	if (*str == '!') {
		str++;
		enable = false;
	}

	for (i = 0; i < OSI_STRING_ENTRIES_MAX; i++) {
		osi = &osi_setup_entries[i];
		if (!strcmp(osi->string, str)) {
			osi->enable = enable;
			break;
		} else if (osi->string[0] == '\0') {
			osi->enable = enable;
			strncpy(osi->string, str, OSI_STRING_LENGTH_MAX);
			break;
		}
	}
}

static void __init set_osi_linux(unsigned int enable)
{
	if (osi_linux.enable != enable)
		osi_linux.enable = enable;

	if (osi_linux.enable)
		acpi_osi_setup("Linux");
	else
		acpi_osi_setup("!Linux");

	return;
}

static void __init acpi_cmdline_osi_linux(unsigned int enable)
{
	osi_linux.cmdline = 1;	/* cmdline set the default and override DMI */
	osi_linux.dmi = 0;
	set_osi_linux(enable);

	return;
}

void __init acpi_dmi_osi_linux(int enable, const struct dmi_system_id *d)
{
	printk(KERN_NOTICE PREFIX "DMI detected: %s\n", d->ident);

	if (enable == -1)
		return;

	osi_linux.dmi = 1;	/* DMI knows that this box asks OSI(Linux) */
	set_osi_linux(enable);

	return;
}

/*
 * Modify the list of "OS Interfaces" reported to BIOS via _OSI
 *
 * empty string disables _OSI
 * string starting with '!' disables that string
 * otherwise string is added to list, augmenting built-in strings
 */
static void __init acpi_osi_setup_late(void)
{
	struct osi_setup_entry *osi;
	char *str;
	int i;
	acpi_status status;

	for (i = 0; i < OSI_STRING_ENTRIES_MAX; i++) {
		osi = &osi_setup_entries[i];
		str = osi->string;

		if (*str == '\0')
			break;
		if (osi->enable) {
			status = acpi_install_interface(str);

			if (ACPI_SUCCESS(status))
				printk(KERN_INFO PREFIX "Added _OSI(%s)\n", str);
		} else {
			status = acpi_remove_interface(str);

			if (ACPI_SUCCESS(status))
				printk(KERN_INFO PREFIX "Deleted _OSI(%s)\n", str);
		}
	}
}

static int __init osi_setup(char *str)
{
	if (str && !strcmp("Linux", str))
		acpi_cmdline_osi_linux(1);
	else if (str && !strcmp("!Linux", str))
		acpi_cmdline_osi_linux(0);
	else
		acpi_osi_setup(str);

	return 1;
}

__setup("acpi_osi=", osi_setup);

/* enable serialization to combat AE_ALREADY_EXISTS errors */
static int __init acpi_serialize_setup(char *str)
{
	printk(KERN_INFO PREFIX "serialize enabled\n");

	acpi_gbl_all_methods_serialized = TRUE;

	return 1;
}

__setup("acpi_serialize", acpi_serialize_setup);

/* Check of resource interference between native drivers and ACPI
 * OperationRegions (SystemIO and System Memory only).
 * IO ports and memory declared in ACPI might be used by the ACPI subsystem
 * in arbitrary AML code and can interfere with legacy drivers.
 * acpi_enforce_resources= can be set to:
 *
 *   - strict (default) (2)
 *     -> further driver trying to access the resources will not load
 *   - lax              (1)
 *     -> further driver trying to access the resources will load, but you
 *     get a system message that something might go wrong...
 *
 *   - no               (0)
 *     -> ACPI Operation Region resources will not be registered
 *
 */
#define ENFORCE_RESOURCES_STRICT 2
#define ENFORCE_RESOURCES_LAX    1
#define ENFORCE_RESOURCES_NO     0

static unsigned int acpi_enforce_resources = ENFORCE_RESOURCES_STRICT;

static int __init acpi_enforce_resources_setup(char *str)
{
	if (str == NULL || *str == '\0')
		return 0;

	if (!strcmp("strict", str))
		acpi_enforce_resources = ENFORCE_RESOURCES_STRICT;
	else if (!strcmp("lax", str))
		acpi_enforce_resources = ENFORCE_RESOURCES_LAX;
	else if (!strcmp("no", str))
		acpi_enforce_resources = ENFORCE_RESOURCES_NO;

	return 1;
}

__setup("acpi_enforce_resources=", acpi_enforce_resources_setup);

/* Check for resource conflicts between ACPI OperationRegions and native
 * drivers */
int acpi_check_resource_conflict(const struct resource *res)
{
	acpi_adr_space_type space_id;
	acpi_size length;
	u8 warn = 0;
	int clash = 0;

	if (acpi_enforce_resources == ENFORCE_RESOURCES_NO)
		return 0;
	if (!(res->flags & IORESOURCE_IO) && !(res->flags & IORESOURCE_MEM))
		return 0;

	if (res->flags & IORESOURCE_IO)
		space_id = ACPI_ADR_SPACE_SYSTEM_IO;
	else
		space_id = ACPI_ADR_SPACE_SYSTEM_MEMORY;

	length = resource_size(res);
	if (acpi_enforce_resources != ENFORCE_RESOURCES_NO)
		warn = 1;
	clash = acpi_check_address_range(space_id, res->start, length, warn);

	if (clash) {
		if (acpi_enforce_resources != ENFORCE_RESOURCES_NO) {
			if (acpi_enforce_resources == ENFORCE_RESOURCES_LAX)
				printk(KERN_NOTICE "ACPI: This conflict may"
				       " cause random problems and system"
				       " instability\n");
			printk(KERN_INFO "ACPI: If an ACPI driver is available"
			       " for this device, you should use it instead of"
			       " the native driver\n");
		}
		if (acpi_enforce_resources == ENFORCE_RESOURCES_STRICT)
			return -EBUSY;
	}
	return 0;
}
EXPORT_SYMBOL(acpi_check_resource_conflict);

int acpi_check_region(resource_size_t start, resource_size_t n,
		      const char *name)
{
	struct resource res = {
		.start = start,
		.end   = start + n - 1,
		.name  = name,
		.flags = IORESOURCE_IO,
	};

	return acpi_check_resource_conflict(&res);
}
EXPORT_SYMBOL(acpi_check_region);

/*
 * Let drivers know whether the resource checks are effective
 */
int acpi_resources_are_enforced(void)
{
	return acpi_enforce_resources == ENFORCE_RESOURCES_STRICT;
}
EXPORT_SYMBOL(acpi_resources_are_enforced);

/*
 * Deallocate the memory for a spinlock.
 */
void acpi_os_delete_lock(acpi_spinlock handle)
{
	ACPI_FREE(handle);
}

/*
 * Acquire a spinlock.
 *
 * handle is a pointer to the spinlock_t.
 */

acpi_cpu_flags acpi_os_acquire_lock(acpi_spinlock lockp)
{
	acpi_cpu_flags flags;
	spin_lock_irqsave(lockp, flags);
	return flags;
}

/*
 * Release a spinlock. See above.
 */

void acpi_os_release_lock(acpi_spinlock lockp, acpi_cpu_flags flags)
{
	spin_unlock_irqrestore(lockp, flags);
}

#ifndef ACPI_USE_LOCAL_CACHE

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_create_cache
 *
 * PARAMETERS:  name      - Ascii name for the cache
 *              size      - Size of each cached object
 *              depth     - Maximum depth of the cache (in objects) <ignored>
 *              cache     - Where the new cache object is returned
 *
 * RETURN:      status
 *
 * DESCRIPTION: Create a cache object
 *
 ******************************************************************************/

acpi_status
acpi_os_create_cache(char *name, u16 size, u16 depth, acpi_cache_t ** cache)
{
	*cache = kmem_cache_create(name, size, 0, 0, NULL);
	if (*cache == NULL)
		return AE_ERROR;
	else
		return AE_OK;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_purge_cache
 *
 * PARAMETERS:  Cache           - Handle to cache object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Free all objects within the requested cache.
 *
 ******************************************************************************/

acpi_status acpi_os_purge_cache(acpi_cache_t * cache)
{
	kmem_cache_shrink(cache);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_delete_cache
 *
 * PARAMETERS:  Cache           - Handle to cache object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Free all objects within the requested cache and delete the
 *              cache object.
 *
 ******************************************************************************/

acpi_status acpi_os_delete_cache(acpi_cache_t * cache)
{
	kmem_cache_destroy(cache);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_release_object
 *
 * PARAMETERS:  Cache       - Handle to cache object
 *              Object      - The object to be released
 *
 * RETURN:      None
 *
 * DESCRIPTION: Release an object to the specified cache.  If cache is full,
 *              the object is deleted.
 *
 ******************************************************************************/

acpi_status acpi_os_release_object(acpi_cache_t * cache, void *object)
{
	kmem_cache_free(cache, object);
	return (AE_OK);
}
#endif

static int __init acpi_no_auto_ssdt_setup(char *s)
{
        printk(KERN_NOTICE PREFIX "SSDT auto-load disabled\n");

        acpi_gbl_disable_ssdt_table_load = TRUE;

        return 1;
}

__setup("acpi_no_auto_ssdt", acpi_no_auto_ssdt_setup);

acpi_status __init acpi_os_initialize(void)
{
	acpi_os_map_generic_address(&acpi_gbl_FADT.xpm1a_event_block);
	acpi_os_map_generic_address(&acpi_gbl_FADT.xpm1b_event_block);
	acpi_os_map_generic_address(&acpi_gbl_FADT.xgpe0_block);
	acpi_os_map_generic_address(&acpi_gbl_FADT.xgpe1_block);

	return AE_OK;
}

acpi_status __init acpi_os_initialize1(void)
{
	kacpid_wq = alloc_workqueue("kacpid", 0, 1);
	kacpi_notify_wq = alloc_workqueue("kacpi_notify", 0, 1);
	kacpi_hotplug_wq = alloc_workqueue("kacpi_hotplug", 0, 1);
	BUG_ON(!kacpid_wq);
	BUG_ON(!kacpi_notify_wq);
	BUG_ON(!kacpi_hotplug_wq);
	acpi_install_interface_handler(acpi_osi_handler);
	acpi_osi_setup_late();
	return AE_OK;
}

acpi_status acpi_os_terminate(void)
{
	if (acpi_irq_handler) {
		acpi_os_remove_interrupt_handler(acpi_gbl_FADT.sci_interrupt,
						 acpi_irq_handler);
	}

	acpi_os_unmap_generic_address(&acpi_gbl_FADT.xgpe1_block);
	acpi_os_unmap_generic_address(&acpi_gbl_FADT.xgpe0_block);
	acpi_os_unmap_generic_address(&acpi_gbl_FADT.xpm1b_event_block);
	acpi_os_unmap_generic_address(&acpi_gbl_FADT.xpm1a_event_block);

	destroy_workqueue(kacpid_wq);
	destroy_workqueue(kacpi_notify_wq);
	destroy_workqueue(kacpi_hotplug_wq);

	return AE_OK;
}

acpi_status acpi_os_prepare_sleep(u8 sleep_state, u32 pm1a_control,
				  u32 pm1b_control)
{
	int rc = 0;
	if (__acpi_os_prepare_sleep)
		rc = __acpi_os_prepare_sleep(sleep_state,
					     pm1a_control, pm1b_control);
	if (rc < 0)
		return AE_ERROR;
	else if (rc > 0)
		return AE_CTRL_SKIP;

	return AE_OK;
}

void acpi_os_set_prepare_sleep(int (*func)(u8 sleep_state,
			       u32 pm1a_ctrl, u32 pm1b_ctrl))
{
	__acpi_os_prepare_sleep = func;
}

void alloc_acpi_hp_work(acpi_handle handle, u32 type, void *context,
			void (*func)(struct work_struct *work))
{
	struct acpi_hp_work *hp_work;
	int ret;

	hp_work = kmalloc(sizeof(*hp_work), GFP_KERNEL);
	if (!hp_work)
		return;

	hp_work->handle = handle;
	hp_work->type = type;
	hp_work->context = context;

	INIT_WORK(&hp_work->work, func);
	ret = queue_work(kacpi_hotplug_wq, &hp_work->work);
	if (!ret)
		kfree(hp_work);
}
EXPORT_SYMBOL_GPL(alloc_acpi_hp_work);
