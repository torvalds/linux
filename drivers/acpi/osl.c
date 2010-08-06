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

static unsigned int acpi_irq_irq;
static acpi_osd_handler acpi_irq_handler;
static void *acpi_irq_context;
static struct workqueue_struct *kacpid_wq;
static struct workqueue_struct *kacpi_notify_wq;
static struct workqueue_struct *kacpi_hotplug_wq;

struct acpi_res_list {
	resource_size_t start;
	resource_size_t end;
	acpi_adr_space_type resource_type; /* IO port, System memory, ...*/
	char name[5];   /* only can have a length of 4 chars, make use of this
			   one instead of res->name, no need to kalloc then */
	struct list_head resource_list;
	int count;
};

static LIST_HEAD(resource_list_head);
static DEFINE_SPINLOCK(acpi_res_lock);

#define	OSI_STRING_LENGTH_MAX 64	/* arbitrary */
static char osi_additional_string[OSI_STRING_LENGTH_MAX];

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
	unsigned int	known:1;
} osi_linux = { 0, 0, 0, 0};

static void __init acpi_request_region (struct acpi_generic_address *addr,
	unsigned int length, char *desc)
{
	if (!addr->address || !length)
		return;

	/* Resources are never freed */
	if (addr->space_id == ACPI_ADR_SPACE_SYSTEM_IO)
		request_region(addr->address, length, desc);
	else if (addr->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		request_mem_region(addr->address, length, desc);
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

acpi_status __init acpi_os_initialize(void)
{
	return AE_OK;
}

acpi_status acpi_os_initialize1(void)
{
	kacpid_wq = create_workqueue("kacpid");
	kacpi_notify_wq = create_workqueue("kacpi_notify");
	kacpi_hotplug_wq = create_workqueue("kacpi_hotplug");
	BUG_ON(!kacpid_wq);
	BUG_ON(!kacpi_notify_wq);
	BUG_ON(!kacpi_hotplug_wq);
	return AE_OK;
}

acpi_status acpi_os_terminate(void)
{
	if (acpi_irq_handler) {
		acpi_os_remove_interrupt_handler(acpi_irq_irq,
						 acpi_irq_handler);
	}

	destroy_workqueue(kacpid_wq);
	destroy_workqueue(kacpi_notify_wq);
	destroy_workqueue(kacpi_hotplug_wq);

	return AE_OK;
}

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

acpi_physical_address __init acpi_os_get_root_pointer(void)
{
	if (efi_enabled) {
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

void __iomem *__init_refok
acpi_os_map_memory(acpi_physical_address phys, acpi_size size)
{
	if (phys > ULONG_MAX) {
		printk(KERN_ERR PREFIX "Cannot map memory that high\n");
		return NULL;
	}
	if (acpi_gbl_permanent_mmap)
		/*
		* ioremap checks to ensure this is in reserved space
		*/
		return ioremap((unsigned long)phys, size);
	else
		return __acpi_map_table((unsigned long)phys, size);
}
EXPORT_SYMBOL_GPL(acpi_os_map_memory);

void __ref acpi_os_unmap_memory(void __iomem *virt, acpi_size size)
{
	if (acpi_gbl_permanent_mmap)
		iounmap(virt);
	else
		__acpi_unmap_table(virt, size);
}
EXPORT_SYMBOL_GPL(acpi_os_unmap_memory);

void __init early_acpi_os_unmap_memory(void __iomem *virt, acpi_size size)
{
	if (!acpi_gbl_permanent_mmap)
		__acpi_unmap_table(virt, size);
}

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
	if (*new_table != NULL) {
		printk(KERN_WARNING PREFIX "Override [%4.4s-%8.8s], "
			   "this is unsafe: tainting kernel\n",
		       existing_table->signature,
		       existing_table->oem_table_id);
		add_taint(TAINT_OVERRIDDEN_ACPI_TABLE);
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
	 * Ignore the GSI from the core, and use the value in our copy of the
	 * FADT. It may not be the same if an interrupt source override exists
	 * for the SCI.
	 */
	gsi = acpi_gbl_FADT.sci_interrupt;
	if (acpi_gsi_to_irq(gsi, &irq) < 0) {
		printk(KERN_ERR PREFIX "SCI (ACPI GSI %d) not registered\n",
		       gsi);
		return AE_OK;
	}

	acpi_irq_handler = handler;
	acpi_irq_context = context;
	if (request_irq(irq, acpi_irq, IRQF_SHARED, "acpi", acpi_irq)) {
		printk(KERN_ERR PREFIX "SCI (IRQ%d) allocation failed\n", irq);
		return AE_NOT_ACQUIRED;
	}
	acpi_irq_irq = irq;

	return AE_OK;
}

acpi_status acpi_os_remove_interrupt_handler(u32 irq, acpi_osd_handler handler)
{
	if (irq) {
		free_irq(irq, acpi_irq);
		acpi_irq_handler = NULL;
		acpi_irq_irq = 0;
	}

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
	static u64 t;

#ifdef	CONFIG_HPET
	/* TBD: use HPET if available */
#endif

#ifdef	CONFIG_X86_PM_TIMER
	/* TBD: default to PM timer if HPET was not available */
#endif
	if (!t)
		printk(KERN_ERR PREFIX "acpi_os_get_timer() TBD\n");

	return ++t;
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
acpi_os_read_memory(acpi_physical_address phys_addr, u32 * value, u32 width)
{
	u32 dummy;
	void __iomem *virt_addr;

	virt_addr = ioremap(phys_addr, width);
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
	default:
		BUG();
	}

	iounmap(virt_addr);

	return AE_OK;
}

acpi_status
acpi_os_write_memory(acpi_physical_address phys_addr, u32 value, u32 width)
{
	void __iomem *virt_addr;

	virt_addr = ioremap(phys_addr, width);

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
	default:
		BUG();
	}

	iounmap(virt_addr);

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

/* TODO: Change code to take advantage of driver model more */
static void acpi_os_derive_pci_id_2(acpi_handle rhandle,	/* upper bound  */
				    acpi_handle chandle,	/* current node */
				    struct acpi_pci_id **id,
				    int *is_bridge, u8 * bus_number)
{
	acpi_handle handle;
	struct acpi_pci_id *pci_id = *id;
	acpi_status status;
	unsigned long long temp;
	acpi_object_type type;

	acpi_get_parent(chandle, &handle);
	if (handle != rhandle) {
		acpi_os_derive_pci_id_2(rhandle, handle, &pci_id, is_bridge,
					bus_number);

		status = acpi_get_type(handle, &type);
		if ((ACPI_FAILURE(status)) || (type != ACPI_TYPE_DEVICE))
			return;

		status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL,
					  &temp);
		if (ACPI_SUCCESS(status)) {
			u64 val;
			pci_id->device = ACPI_HIWORD(ACPI_LODWORD(temp));
			pci_id->function = ACPI_LOWORD(ACPI_LODWORD(temp));

			if (*is_bridge)
				pci_id->bus = *bus_number;

			/* any nicer way to get bus number of bridge ? */
			status =
			    acpi_os_read_pci_configuration(pci_id, 0x0e, &val,
							   8);
			if (ACPI_SUCCESS(status)
			    && ((val & 0x7f) == 1 || (val & 0x7f) == 2)) {
				status =
				    acpi_os_read_pci_configuration(pci_id, 0x18,
								   &val, 8);
				if (!ACPI_SUCCESS(status)) {
					/* Certainly broken...  FIX ME */
					return;
				}
				*is_bridge = 1;
				pci_id->bus = val;
				status =
				    acpi_os_read_pci_configuration(pci_id, 0x19,
								   &val, 8);
				if (ACPI_SUCCESS(status)) {
					*bus_number = val;
				}
			} else
				*is_bridge = 0;
		}
	}
}

void acpi_os_derive_pci_id(acpi_handle rhandle,	/* upper bound  */
			   acpi_handle chandle,	/* current node */
			   struct acpi_pci_id **id)
{
	int is_bridge = 1;
	u8 bus_number = (*id)->bus;

	acpi_os_derive_pci_id_2(rhandle, chandle, id, &is_bridge, &bus_number);
}

static void acpi_os_execute_deferred(struct work_struct *work)
{
	struct acpi_os_dpc *dpc = container_of(work, struct acpi_os_dpc, work);

	if (dpc->wait)
		acpi_os_wait_events_complete(NULL);

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

	dpc = kmalloc(sizeof(struct acpi_os_dpc), GFP_ATOMIC);
	if (!dpc)
		return AE_NO_MEMORY;

	dpc->function = function;
	dpc->context = context;

	/*
	 * We can't run hotplug code in keventd_wq/kacpid_wq/kacpid_notify_wq
	 * because the hotplug code may call driver .remove() functions,
	 * which invoke flush_scheduled_work/acpi_os_wait_events_complete
	 * to flush these workqueues.
	 */
	queue = hp ? kacpi_hotplug_wq :
		(type == OSL_NOTIFY_HANDLER ? kacpi_notify_wq : kacpid_wq);
	dpc->wait = hp ? 1 : 0;

	if (queue == kacpi_hotplug_wq)
		INIT_WORK(&dpc->work, acpi_os_execute_deferred);
	else if (queue == kacpi_notify_wq)
		INIT_WORK(&dpc->work, acpi_os_execute_deferred);
	else
		INIT_WORK(&dpc->work, acpi_os_execute_deferred);

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

void acpi_os_wait_events_complete(void *context)
{
	flush_workqueue(kacpid_wq);
	flush_workqueue(kacpi_notify_wq);
}

EXPORT_SYMBOL(acpi_os_wait_events_complete);

/*
 * Allocate the memory for a spinlock and initialize it.
 */
acpi_status acpi_os_create_lock(acpi_spinlock * handle)
{
	spin_lock_init(*handle);

	return AE_OK;
}

/*
 * Deallocate the memory for a spinlock.
 */
void acpi_os_delete_lock(acpi_spinlock handle)
{
	return;
}

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

static void __init set_osi_linux(unsigned int enable)
{
	if (osi_linux.enable != enable) {
		osi_linux.enable = enable;
		printk(KERN_NOTICE PREFIX "%sed _OSI(Linux)\n",
			enable ? "Add": "Delet");
	}
	return;
}

static void __init acpi_cmdline_osi_linux(unsigned int enable)
{
	osi_linux.cmdline = 1;	/* cmdline set the default */
	set_osi_linux(enable);

	return;
}

void __init acpi_dmi_osi_linux(int enable, const struct dmi_system_id *d)
{
	osi_linux.dmi = 1;	/* DMI knows that this box asks OSI(Linux) */

	printk(KERN_NOTICE PREFIX "DMI detected: %s\n", d->ident);

	if (enable == -1)
		return;

	osi_linux.known = 1;	/* DMI knows which OSI(Linux) default needed */

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
int __init acpi_osi_setup(char *str)
{
	if (str == NULL || *str == '\0') {
		printk(KERN_INFO PREFIX "_OSI method disabled\n");
		acpi_gbl_create_osi_method = FALSE;
	} else if (!strcmp("!Linux", str)) {
		acpi_cmdline_osi_linux(0);	/* !enable */
	} else if (*str == '!') {
		if (acpi_osi_invalidate(++str) == AE_OK)
			printk(KERN_INFO PREFIX "Deleted _OSI(%s)\n", str);
	} else if (!strcmp("Linux", str)) {
		acpi_cmdline_osi_linux(1);	/* enable */
	} else if (*osi_additional_string == '\0') {
		strncpy(osi_additional_string, str, OSI_STRING_LENGTH_MAX);
		printk(KERN_INFO PREFIX "Added _OSI(%s)\n", str);
	}

	return 1;
}

__setup("acpi_osi=", acpi_osi_setup);

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
	struct acpi_res_list *res_list_elem;
	int ioport;
	int clash = 0;

	if (acpi_enforce_resources == ENFORCE_RESOURCES_NO)
		return 0;
	if (!(res->flags & IORESOURCE_IO) && !(res->flags & IORESOURCE_MEM))
		return 0;

	ioport = res->flags & IORESOURCE_IO;

	spin_lock(&acpi_res_lock);
	list_for_each_entry(res_list_elem, &resource_list_head,
			    resource_list) {
		if (ioport && (res_list_elem->resource_type
			       != ACPI_ADR_SPACE_SYSTEM_IO))
			continue;
		if (!ioport && (res_list_elem->resource_type
				!= ACPI_ADR_SPACE_SYSTEM_MEMORY))
			continue;

		if (res->end < res_list_elem->start
		    || res_list_elem->end < res->start)
			continue;
		clash = 1;
		break;
	}
	spin_unlock(&acpi_res_lock);

	if (clash) {
		if (acpi_enforce_resources != ENFORCE_RESOURCES_NO) {
			printk(KERN_WARNING "ACPI: resource %s %pR"
			       " conflicts with ACPI region %s %pR\n",
			       res->name, res, res_list_elem->name,
			       res_list_elem);
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

int acpi_check_mem_region(resource_size_t start, resource_size_t n,
		      const char *name)
{
	struct resource res = {
		.start = start,
		.end   = start + n - 1,
		.name  = name,
		.flags = IORESOURCE_MEM,
	};

	return acpi_check_resource_conflict(&res);

}
EXPORT_SYMBOL(acpi_check_mem_region);

/*
 * Let drivers know whether the resource checks are effective
 */
int acpi_resources_are_enforced(void)
{
	return acpi_enforce_resources == ENFORCE_RESOURCES_STRICT;
}
EXPORT_SYMBOL(acpi_resources_are_enforced);

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

/******************************************************************************
 *
 * FUNCTION:    acpi_os_validate_interface
 *
 * PARAMETERS:  interface           - Requested interface to be validated
 *
 * RETURN:      AE_OK if interface is supported, AE_SUPPORT otherwise
 *
 * DESCRIPTION: Match an interface string to the interfaces supported by the
 *              host. Strings originate from an AML call to the _OSI method.
 *
 *****************************************************************************/

acpi_status
acpi_os_validate_interface (char *interface)
{
	if (!strncmp(osi_additional_string, interface, OSI_STRING_LENGTH_MAX))
		return AE_OK;
	if (!strcmp("Linux", interface)) {

		printk(KERN_NOTICE PREFIX
			"BIOS _OSI(Linux) query %s%s\n",
			osi_linux.enable ? "honored" : "ignored",
			osi_linux.cmdline ? " via cmdline" :
			osi_linux.dmi ? " via DMI" : "");

		if (osi_linux.enable)
			return AE_OK;
	}
	return AE_SUPPORT;
}

static inline int acpi_res_list_add(struct acpi_res_list *res)
{
	struct acpi_res_list *res_list_elem;

	list_for_each_entry(res_list_elem, &resource_list_head,
			    resource_list) {

		if (res->resource_type == res_list_elem->resource_type &&
		    res->start == res_list_elem->start &&
		    res->end == res_list_elem->end) {

			/*
			 * The Region(addr,len) already exist in the list,
			 * just increase the count
			 */

			res_list_elem->count++;
			return 0;
		}
	}

	res->count = 1;
	list_add(&res->resource_list, &resource_list_head);
	return 1;
}

static inline void acpi_res_list_del(struct acpi_res_list *res)
{
	struct acpi_res_list *res_list_elem;

	list_for_each_entry(res_list_elem, &resource_list_head,
			    resource_list) {

		if (res->resource_type == res_list_elem->resource_type &&
		    res->start == res_list_elem->start &&
		    res->end == res_list_elem->end) {

			/*
			 * If the res count is decreased to 0,
			 * remove and free it
			 */

			if (--res_list_elem->count == 0) {
				list_del(&res_list_elem->resource_list);
				kfree(res_list_elem);
			}
			return;
		}
	}
}

acpi_status
acpi_os_invalidate_address(
    u8                   space_id,
    acpi_physical_address   address,
    acpi_size               length)
{
	struct acpi_res_list res;

	switch (space_id) {
	case ACPI_ADR_SPACE_SYSTEM_IO:
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:
		/* Only interference checks against SystemIO and SystemMemory
		   are needed */
		res.start = address;
		res.end = address + length - 1;
		res.resource_type = space_id;
		spin_lock(&acpi_res_lock);
		acpi_res_list_del(&res);
		spin_unlock(&acpi_res_lock);
		break;
	case ACPI_ADR_SPACE_PCI_CONFIG:
	case ACPI_ADR_SPACE_EC:
	case ACPI_ADR_SPACE_SMBUS:
	case ACPI_ADR_SPACE_CMOS:
	case ACPI_ADR_SPACE_PCI_BAR_TARGET:
	case ACPI_ADR_SPACE_DATA_TABLE:
	case ACPI_ADR_SPACE_FIXED_HARDWARE:
		break;
	}
	return AE_OK;
}

/******************************************************************************
 *
 * FUNCTION:    acpi_os_validate_address
 *
 * PARAMETERS:  space_id             - ACPI space ID
 *              address             - Physical address
 *              length              - Address length
 *
 * RETURN:      AE_OK if address/length is valid for the space_id. Otherwise,
 *              should return AE_AML_ILLEGAL_ADDRESS.
 *
 * DESCRIPTION: Validate a system address via the host OS. Used to validate
 *              the addresses accessed by AML operation regions.
 *
 *****************************************************************************/

acpi_status
acpi_os_validate_address (
    u8                   space_id,
    acpi_physical_address   address,
    acpi_size               length,
    char *name)
{
	struct acpi_res_list *res;
	int added;
	if (acpi_enforce_resources == ENFORCE_RESOURCES_NO)
		return AE_OK;

	switch (space_id) {
	case ACPI_ADR_SPACE_SYSTEM_IO:
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:
		/* Only interference checks against SystemIO and SystemMemory
		   are needed */
		res = kzalloc(sizeof(struct acpi_res_list), GFP_KERNEL);
		if (!res)
			return AE_OK;
		/* ACPI names are fixed to 4 bytes, still better use strlcpy */
		strlcpy(res->name, name, 5);
		res->start = address;
		res->end = address + length - 1;
		res->resource_type = space_id;
		spin_lock(&acpi_res_lock);
		added = acpi_res_list_add(res);
		spin_unlock(&acpi_res_lock);
		pr_debug("%s %s resource: start: 0x%llx, end: 0x%llx, "
			 "name: %s\n", added ? "Added" : "Already exist",
			 (space_id == ACPI_ADR_SPACE_SYSTEM_IO)
			 ? "SystemIO" : "System Memory",
			 (unsigned long long)res->start,
			 (unsigned long long)res->end,
			 res->name);
		if (!added)
			kfree(res);
		break;
	case ACPI_ADR_SPACE_PCI_CONFIG:
	case ACPI_ADR_SPACE_EC:
	case ACPI_ADR_SPACE_SMBUS:
	case ACPI_ADR_SPACE_CMOS:
	case ACPI_ADR_SPACE_PCI_BAR_TARGET:
	case ACPI_ADR_SPACE_DATA_TABLE:
	case ACPI_ADR_SPACE_FIXED_HARDWARE:
		break;
	}
	return AE_OK;
}

#endif
