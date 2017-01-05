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
#include <linux/efi.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/semaphore.h>

#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#include "internal.h"

#define _COMPONENT		ACPI_OS_SERVICES
ACPI_MODULE_NAME("osl");

struct acpi_os_dpc {
	acpi_osd_exec_callback function;
	void *context;
	struct work_struct work;
};

#ifdef ENABLE_DEBUGGER
#include <linux/kdb.h>

/* stuff for debugger support */
int acpi_in_debugger;
EXPORT_SYMBOL(acpi_in_debugger);
#endif				/*ENABLE_DEBUGGER */

static int (*__acpi_os_prepare_sleep)(u8 sleep_state, u32 pm1a_ctrl,
				      u32 pm1b_ctrl);
static int (*__acpi_os_prepare_extended_sleep)(u8 sleep_state, u32 val_a,
				      u32 val_b);

static acpi_osd_handler acpi_irq_handler;
static void *acpi_irq_context;
static struct workqueue_struct *kacpid_wq;
static struct workqueue_struct *kacpi_notify_wq;
static struct workqueue_struct *kacpi_hotplug_wq;
static bool acpi_os_initialized;
unsigned int acpi_sci_irq = INVALID_ACPI_IRQ;
bool acpi_permanent_mmap = false;

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
fs_initcall_sync(acpi_reserve_resources);

void acpi_os_printf(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	acpi_os_vprintf(fmt, args);
	va_end(args);
}
EXPORT_SYMBOL(acpi_os_printf);

void acpi_os_vprintf(const char *fmt, va_list args)
{
	static char buffer[512];

	vsprintf(buffer, fmt, args);

#ifdef ENABLE_DEBUGGER
	if (acpi_in_debugger) {
		kdb_printf("%s", buffer);
	} else {
		if (printk_get_level(buffer))
			printk("%s", buffer);
		else
			printk(KERN_CONT "%s", buffer);
	}
#else
	if (acpi_debugger_write_log(buffer) < 0) {
		if (printk_get_level(buffer))
			printk("%s", buffer);
		else
			printk(KERN_CONT "%s", buffer);
	}
#endif
}

#ifdef CONFIG_KEXEC
static unsigned long acpi_rsdp;
static int __init setup_acpi_rsdp(char *arg)
{
	return kstrtoul(arg, 16, &acpi_rsdp);
}
early_param("acpi_rsdp", setup_acpi_rsdp);
#endif

acpi_physical_address __init acpi_os_get_root_pointer(void)
{
	acpi_physical_address pa = 0;

#ifdef CONFIG_KEXEC
	if (acpi_rsdp)
		return acpi_rsdp;
#endif

	if (efi_enabled(EFI_CONFIG_TABLES)) {
		if (efi.acpi20 != EFI_INVALID_TABLE_ADDR)
			return efi.acpi20;
		if (efi.acpi != EFI_INVALID_TABLE_ADDR)
			return efi.acpi;
		pr_err(PREFIX "System description tables not found\n");
	} else if (IS_ENABLED(CONFIG_ACPI_LEGACY_TABLES_LOOKUP)) {
		acpi_find_root_pointer(&pa);
	}

	return pa;
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

#if defined(CONFIG_IA64) || defined(CONFIG_ARM64)
/* ioremap will take care of cache attributes */
#define should_use_kmap(pfn)   0
#else
#define should_use_kmap(pfn)   page_is_ram(pfn)
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

/**
 * acpi_os_map_iomem - Get a virtual address for a given physical address range.
 * @phys: Start of the physical address range to map.
 * @size: Size of the physical address range to map.
 *
 * Look up the given physical address range in the list of existing ACPI memory
 * mappings.  If found, get a reference to it and return a pointer to it (its
 * virtual address).  If not found, map it, add it to that list and return a
 * pointer to it.
 *
 * During early init (when acpi_permanent_mmap has not been set yet) this
 * routine simply calls __acpi_map_table() to get the job done.
 */
void __iomem *__ref
acpi_os_map_iomem(acpi_physical_address phys, acpi_size size)
{
	struct acpi_ioremap *map;
	void __iomem *virt;
	acpi_physical_address pg_off;
	acpi_size pg_sz;

	if (phys > ULONG_MAX) {
		printk(KERN_ERR PREFIX "Cannot map memory that high\n");
		return NULL;
	}

	if (!acpi_permanent_mmap)
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
EXPORT_SYMBOL_GPL(acpi_os_map_iomem);

void *__ref acpi_os_map_memory(acpi_physical_address phys, acpi_size size)
{
	return (void *)acpi_os_map_iomem(phys, size);
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
		synchronize_rcu_expedited();
		acpi_unmap(map->phys, map->virt);
		kfree(map);
	}
}

/**
 * acpi_os_unmap_iomem - Drop a memory mapping reference.
 * @virt: Start of the address range to drop a reference to.
 * @size: Size of the address range to drop a reference to.
 *
 * Look up the given virtual address range in the list of existing ACPI memory
 * mappings, drop a reference to it and unmap it if there are no more active
 * references to it.
 *
 * During early init (when acpi_permanent_mmap has not been set yet) this
 * routine simply calls __acpi_unmap_table() to get the job done.  Since
 * __acpi_unmap_table() is an __init function, the __ref annotation is needed
 * here.
 */
void __ref acpi_os_unmap_iomem(void __iomem *virt, acpi_size size)
{
	struct acpi_ioremap *map;

	if (!acpi_permanent_mmap) {
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
EXPORT_SYMBOL_GPL(acpi_os_unmap_iomem);

void __ref acpi_os_unmap_memory(void *virt, acpi_size size)
{
	return acpi_os_unmap_iomem((void __iomem *)virt, size);
}
EXPORT_SYMBOL_GPL(acpi_os_unmap_memory);

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

	virt = acpi_os_map_iomem(addr, gas->bit_width / 8);
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

#ifdef CONFIG_ACPI_REV_OVERRIDE_POSSIBLE
static bool acpi_rev_override;

int __init acpi_rev_override_setup(char *str)
{
	acpi_rev_override = true;
	return 1;
}
__setup("acpi_rev_override", acpi_rev_override_setup);
#else
#define acpi_rev_override	false
#endif

#define ACPI_MAX_OVERRIDE_LEN 100

static char acpi_os_name[ACPI_MAX_OVERRIDE_LEN];

acpi_status
acpi_os_predefined_override(const struct acpi_predefined_names *init_val,
			    acpi_string *new_val)
{
	if (!init_val || !new_val)
		return AE_BAD_PARAMETER;

	*new_val = NULL;
	if (!memcmp(init_val->name, "_OS_", 4) && strlen(acpi_os_name)) {
		printk(KERN_INFO PREFIX "Overriding _OS definition to '%s'\n",
		       acpi_os_name);
		*new_val = acpi_os_name;
	}

	if (!memcmp(init_val->name, "_REV", 4) && acpi_rev_override) {
		printk(KERN_INFO PREFIX "Overriding _REV return value to 5\n");
		*new_val = (char *)5;
	}

	return AE_OK;
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
	if (request_irq(irq, acpi_irq, IRQF_SHARED, "acpi", acpi_irq)) {
		printk(KERN_ERR PREFIX "SCI (IRQ%d) allocation failed\n", irq);
		acpi_irq_handler = NULL;
		return AE_NOT_ACQUIRED;
	}
	acpi_sci_irq = irq;

	return AE_OK;
}

acpi_status acpi_os_remove_interrupt_handler(u32 gsi, acpi_osd_handler handler)
{
	if (gsi != acpi_gbl_FADT.sci_interrupt || !acpi_sci_irq_valid())
		return AE_BAD_PARAMETER;

	free_irq(acpi_sci_irq, acpi_irq);
	acpi_irq_handler = NULL;
	acpi_sci_irq = INVALID_ACPI_IRQ;

	return AE_OK;
}

/*
 * Running in interpreter thread context, safe to sleep
 */

void acpi_os_sleep(u64 ms)
{
	msleep(ms);
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
		*(u64 *) value = readq(virt_addr);
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
		writeq(value, virt_addr);
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

	dpc->function(dpc->context);
	kfree(dpc);
}

#ifdef CONFIG_ACPI_DEBUGGER
static struct acpi_debugger acpi_debugger;
static bool acpi_debugger_initialized;

int acpi_register_debugger(struct module *owner,
			   const struct acpi_debugger_ops *ops)
{
	int ret = 0;

	mutex_lock(&acpi_debugger.lock);
	if (acpi_debugger.ops) {
		ret = -EBUSY;
		goto err_lock;
	}

	acpi_debugger.owner = owner;
	acpi_debugger.ops = ops;

err_lock:
	mutex_unlock(&acpi_debugger.lock);
	return ret;
}
EXPORT_SYMBOL(acpi_register_debugger);

void acpi_unregister_debugger(const struct acpi_debugger_ops *ops)
{
	mutex_lock(&acpi_debugger.lock);
	if (ops == acpi_debugger.ops) {
		acpi_debugger.ops = NULL;
		acpi_debugger.owner = NULL;
	}
	mutex_unlock(&acpi_debugger.lock);
}
EXPORT_SYMBOL(acpi_unregister_debugger);

int acpi_debugger_create_thread(acpi_osd_exec_callback function, void *context)
{
	int ret;
	int (*func)(acpi_osd_exec_callback, void *);
	struct module *owner;

	if (!acpi_debugger_initialized)
		return -ENODEV;
	mutex_lock(&acpi_debugger.lock);
	if (!acpi_debugger.ops) {
		ret = -ENODEV;
		goto err_lock;
	}
	if (!try_module_get(acpi_debugger.owner)) {
		ret = -ENODEV;
		goto err_lock;
	}
	func = acpi_debugger.ops->create_thread;
	owner = acpi_debugger.owner;
	mutex_unlock(&acpi_debugger.lock);

	ret = func(function, context);

	mutex_lock(&acpi_debugger.lock);
	module_put(owner);
err_lock:
	mutex_unlock(&acpi_debugger.lock);
	return ret;
}

ssize_t acpi_debugger_write_log(const char *msg)
{
	ssize_t ret;
	ssize_t (*func)(const char *);
	struct module *owner;

	if (!acpi_debugger_initialized)
		return -ENODEV;
	mutex_lock(&acpi_debugger.lock);
	if (!acpi_debugger.ops) {
		ret = -ENODEV;
		goto err_lock;
	}
	if (!try_module_get(acpi_debugger.owner)) {
		ret = -ENODEV;
		goto err_lock;
	}
	func = acpi_debugger.ops->write_log;
	owner = acpi_debugger.owner;
	mutex_unlock(&acpi_debugger.lock);

	ret = func(msg);

	mutex_lock(&acpi_debugger.lock);
	module_put(owner);
err_lock:
	mutex_unlock(&acpi_debugger.lock);
	return ret;
}

ssize_t acpi_debugger_read_cmd(char *buffer, size_t buffer_length)
{
	ssize_t ret;
	ssize_t (*func)(char *, size_t);
	struct module *owner;

	if (!acpi_debugger_initialized)
		return -ENODEV;
	mutex_lock(&acpi_debugger.lock);
	if (!acpi_debugger.ops) {
		ret = -ENODEV;
		goto err_lock;
	}
	if (!try_module_get(acpi_debugger.owner)) {
		ret = -ENODEV;
		goto err_lock;
	}
	func = acpi_debugger.ops->read_cmd;
	owner = acpi_debugger.owner;
	mutex_unlock(&acpi_debugger.lock);

	ret = func(buffer, buffer_length);

	mutex_lock(&acpi_debugger.lock);
	module_put(owner);
err_lock:
	mutex_unlock(&acpi_debugger.lock);
	return ret;
}

int acpi_debugger_wait_command_ready(void)
{
	int ret;
	int (*func)(bool, char *, size_t);
	struct module *owner;

	if (!acpi_debugger_initialized)
		return -ENODEV;
	mutex_lock(&acpi_debugger.lock);
	if (!acpi_debugger.ops) {
		ret = -ENODEV;
		goto err_lock;
	}
	if (!try_module_get(acpi_debugger.owner)) {
		ret = -ENODEV;
		goto err_lock;
	}
	func = acpi_debugger.ops->wait_command_ready;
	owner = acpi_debugger.owner;
	mutex_unlock(&acpi_debugger.lock);

	ret = func(acpi_gbl_method_executing,
		   acpi_gbl_db_line_buf, ACPI_DB_LINE_BUFFER_SIZE);

	mutex_lock(&acpi_debugger.lock);
	module_put(owner);
err_lock:
	mutex_unlock(&acpi_debugger.lock);
	return ret;
}

int acpi_debugger_notify_command_complete(void)
{
	int ret;
	int (*func)(void);
	struct module *owner;

	if (!acpi_debugger_initialized)
		return -ENODEV;
	mutex_lock(&acpi_debugger.lock);
	if (!acpi_debugger.ops) {
		ret = -ENODEV;
		goto err_lock;
	}
	if (!try_module_get(acpi_debugger.owner)) {
		ret = -ENODEV;
		goto err_lock;
	}
	func = acpi_debugger.ops->notify_command_complete;
	owner = acpi_debugger.owner;
	mutex_unlock(&acpi_debugger.lock);

	ret = func();

	mutex_lock(&acpi_debugger.lock);
	module_put(owner);
err_lock:
	mutex_unlock(&acpi_debugger.lock);
	return ret;
}

int __init acpi_debugger_init(void)
{
	mutex_init(&acpi_debugger.lock);
	acpi_debugger_initialized = true;
	return 0;
}
#endif

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

acpi_status acpi_os_execute(acpi_execute_type type,
			    acpi_osd_exec_callback function, void *context)
{
	acpi_status status = AE_OK;
	struct acpi_os_dpc *dpc;
	struct workqueue_struct *queue;
	int ret;
	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Scheduling function [%p(%p)] for deferred execution.\n",
			  function, context));

	if (type == OSL_DEBUGGER_MAIN_THREAD) {
		ret = acpi_debugger_create_thread(function, context);
		if (ret) {
			pr_err("Call to kthread_create() failed.\n");
			status = AE_ERROR;
		}
		goto out_thread;
	}

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
	 * To prevent lockdep from complaining unnecessarily, make sure that
	 * there is a different static lockdep key for each workqueue by using
	 * INIT_WORK() for each of them separately.
	 */
	if (type == OSL_NOTIFY_HANDLER) {
		queue = kacpi_notify_wq;
		INIT_WORK(&dpc->work, acpi_os_execute_deferred);
	} else if (type == OSL_GPE_HANDLER) {
		queue = kacpid_wq;
		INIT_WORK(&dpc->work, acpi_os_execute_deferred);
	} else {
		pr_err("Unsupported os_execute type %d.\n", type);
		status = AE_ERROR;
	}

	if (ACPI_FAILURE(status))
		goto err_workqueue;

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
	}
err_workqueue:
	if (ACPI_FAILURE(status))
		kfree(dpc);
out_thread:
	return status;
}
EXPORT_SYMBOL(acpi_os_execute);

void acpi_os_wait_events_complete(void)
{
	/*
	 * Make sure the GPE handler or the fixed event handler is not used
	 * on another CPU after removal.
	 */
	if (acpi_sci_irq_valid())
		synchronize_hardirq(acpi_sci_irq);
	flush_workqueue(kacpid_wq);
	flush_workqueue(kacpi_notify_wq);
}

struct acpi_hp_work {
	struct work_struct work;
	struct acpi_device *adev;
	u32 src;
};

static void acpi_hotplug_work_fn(struct work_struct *work)
{
	struct acpi_hp_work *hpw = container_of(work, struct acpi_hp_work, work);

	acpi_os_wait_events_complete();
	acpi_device_hotplug(hpw->adev, hpw->src);
	kfree(hpw);
}

acpi_status acpi_hotplug_schedule(struct acpi_device *adev, u32 src)
{
	struct acpi_hp_work *hpw;

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
		  "Scheduling hotplug event (%p, %u) for deferred execution.\n",
		  adev, src));

	hpw = kmalloc(sizeof(*hpw), GFP_KERNEL);
	if (!hpw)
		return AE_NO_MEMORY;

	INIT_WORK(&hpw->work, acpi_hotplug_work_fn);
	hpw->adev = adev;
	hpw->src = src;
	/*
	 * We can't run hotplug code in kacpid_wq/kacpid_notify_wq etc., because
	 * the hotplug code may call driver .remove() functions, which may
	 * invoke flush_scheduled_work()/acpi_os_wait_events_complete() to flush
	 * these workqueues.
	 */
	if (!queue_work(kacpi_hotplug_wq, &hpw->work)) {
		kfree(hpw);
		return AE_ERROR;
	}
	return AE_OK;
}

bool acpi_queue_hotplug_work(struct work_struct *work)
{
	return queue_work(kacpi_hotplug_wq, work);
}

acpi_status
acpi_os_create_semaphore(u32 max_units, u32 initial_units, acpi_handle * handle)
{
	struct semaphore *sem = NULL;

	sem = acpi_os_allocate_zeroed(sizeof(struct semaphore));
	if (!sem)
		return AE_NO_MEMORY;

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

	if (!acpi_os_initialized)
		return AE_OK;

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

	if (!acpi_os_initialized)
		return AE_OK;

	if (!sem || (units < 1))
		return AE_BAD_PARAMETER;

	if (units > 1)
		return AE_SUPPORT;

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Signaling semaphore[%p|%d]\n", handle,
			  units));

	up(sem);

	return AE_OK;
}

acpi_status acpi_os_get_line(char *buffer, u32 buffer_length, u32 *bytes_read)
{
#ifdef ENABLE_DEBUGGER
	if (acpi_in_debugger) {
		u32 chars;

		kdb_read(buffer, buffer_length);

		/* remove the CR kdb includes */
		chars = strlen(buffer) - 1;
		buffer[chars] = '\0';
	}
#else
	int ret;

	ret = acpi_debugger_read_cmd(buffer, buffer_length);
	if (ret < 0)
		return AE_ERROR;
	if (bytes_read)
		*bytes_read = ret;
#endif

	return AE_OK;
}
EXPORT_SYMBOL(acpi_os_get_line);

acpi_status acpi_os_wait_command_ready(void)
{
	int ret;

	ret = acpi_debugger_wait_command_ready();
	if (ret < 0)
		return AE_ERROR;
	return AE_OK;
}

acpi_status acpi_os_notify_command_complete(void)
{
	int ret;

	ret = acpi_debugger_notify_command_complete();
	if (ret < 0)
		return AE_ERROR;
	return AE_OK;
}

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

	for (; count-- && *str; str++) {
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

/*
 * Disable the auto-serialization of named objects creation methods.
 *
 * This feature is enabled by default.  It marks the AML control methods
 * that contain the opcodes to create named objects as "Serialized".
 */
static int __init acpi_no_auto_serialize_setup(char *str)
{
	acpi_gbl_auto_serialize_methods = FALSE;
	pr_info("ACPI: auto-serialization disabled\n");

	return 1;
}

__setup("acpi_no_auto_serialize", acpi_no_auto_serialize_setup);

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

static int __init acpi_no_static_ssdt_setup(char *s)
{
	acpi_gbl_disable_ssdt_table_install = TRUE;
	pr_info("ACPI: static SSDT installation disabled\n");

	return 0;
}

early_param("acpi_no_static_ssdt", acpi_no_static_ssdt_setup);

static int __init acpi_disable_return_repair(char *s)
{
	printk(KERN_NOTICE PREFIX
	       "ACPI: Predefined validation mechanism disabled\n");
	acpi_gbl_disable_auto_repair = TRUE;

	return 1;
}

__setup("acpica_no_return_repair", acpi_disable_return_repair);

acpi_status __init acpi_os_initialize(void)
{
	acpi_os_map_generic_address(&acpi_gbl_FADT.xpm1a_event_block);
	acpi_os_map_generic_address(&acpi_gbl_FADT.xpm1b_event_block);
	acpi_os_map_generic_address(&acpi_gbl_FADT.xgpe0_block);
	acpi_os_map_generic_address(&acpi_gbl_FADT.xgpe1_block);
	if (acpi_gbl_FADT.flags & ACPI_FADT_RESET_REGISTER) {
		/*
		 * Use acpi_os_map_generic_address to pre-map the reset
		 * register if it's in system memory.
		 */
		int rv;

		rv = acpi_os_map_generic_address(&acpi_gbl_FADT.reset_register);
		pr_debug(PREFIX "%s: map reset_reg status %d\n", __func__, rv);
	}
	acpi_os_initialized = true;

	return AE_OK;
}

acpi_status __init acpi_os_initialize1(void)
{
	kacpid_wq = alloc_workqueue("kacpid", 0, 1);
	kacpi_notify_wq = alloc_workqueue("kacpi_notify", 0, 1);
	kacpi_hotplug_wq = alloc_ordered_workqueue("kacpi_hotplug", 0);
	BUG_ON(!kacpid_wq);
	BUG_ON(!kacpi_notify_wq);
	BUG_ON(!kacpi_hotplug_wq);
	acpi_osi_init();
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
	if (acpi_gbl_FADT.flags & ACPI_FADT_RESET_REGISTER)
		acpi_os_unmap_generic_address(&acpi_gbl_FADT.reset_register);

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

acpi_status acpi_os_prepare_extended_sleep(u8 sleep_state, u32 val_a,
				  u32 val_b)
{
	int rc = 0;
	if (__acpi_os_prepare_extended_sleep)
		rc = __acpi_os_prepare_extended_sleep(sleep_state,
					     val_a, val_b);
	if (rc < 0)
		return AE_ERROR;
	else if (rc > 0)
		return AE_CTRL_SKIP;

	return AE_OK;
}

void acpi_os_set_prepare_extended_sleep(int (*func)(u8 sleep_state,
			       u32 val_a, u32 val_b))
{
	__acpi_os_prepare_extended_sleep = func;
}
