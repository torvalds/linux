/*
 *  acpi_osl.c - OS-dependent functions ($Revision: 83 $)
 *
 *  Copyright (C) 2000       Andrew Henroid
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/nmi.h>
#include <acpi/acpi.h>
#include <asm/io.h>
#include <acpi/acpi_bus.h>
#include <acpi/processor.h>
#include <asm/uaccess.h>

#include <linux/efi.h>

#define _COMPONENT		ACPI_OS_SERVICES
ACPI_MODULE_NAME("osl")
#define PREFIX		"ACPI: "
struct acpi_os_dpc {
	acpi_osd_exec_callback function;
	void *context;
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

int acpi_specific_hotkey_enabled = TRUE;
EXPORT_SYMBOL(acpi_specific_hotkey_enabled);

static unsigned int acpi_irq_irq;
static acpi_osd_handler acpi_irq_handler;
static void *acpi_irq_context;
static struct workqueue_struct *kacpid_wq;

acpi_status acpi_os_initialize(void)
{
	return AE_OK;
}

acpi_status acpi_os_initialize1(void)
{
	/*
	 * Initialize PCI configuration space access, as we'll need to access
	 * it while walking the namespace (bus 0 and root bridges w/ _BBNs).
	 */
	if (!raw_pci_ops) {
		printk(KERN_ERR PREFIX
		       "Access to PCI configuration space unavailable\n");
		return AE_NULL_ENTRY;
	}
	kacpid_wq = create_singlethread_workqueue("kacpid");
	BUG_ON(!kacpid_wq);

	return AE_OK;
}

acpi_status acpi_os_terminate(void)
{
	if (acpi_irq_handler) {
		acpi_os_remove_interrupt_handler(acpi_irq_irq,
						 acpi_irq_handler);
	}

	destroy_workqueue(kacpid_wq);

	return AE_OK;
}

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
		printk("%s", buffer);
	}
#else
	printk("%s", buffer);
#endif
}

extern int acpi_in_resume;
void *acpi_os_allocate(acpi_size size)
{
	if (acpi_in_resume)
		return kmalloc(size, GFP_ATOMIC);
	else
		return kmalloc(size, GFP_KERNEL);
}

void acpi_os_free(void *ptr)
{
	kfree(ptr);
}

EXPORT_SYMBOL(acpi_os_free);

acpi_status acpi_os_get_root_pointer(u32 flags, struct acpi_pointer *addr)
{
	if (efi_enabled) {
		addr->pointer_type = ACPI_PHYSICAL_POINTER;
		if (efi.acpi20)
			addr->pointer.physical =
			    (acpi_physical_address) virt_to_phys(efi.acpi20);
		else if (efi.acpi)
			addr->pointer.physical =
			    (acpi_physical_address) virt_to_phys(efi.acpi);
		else {
			printk(KERN_ERR PREFIX
			       "System description tables not found\n");
			return AE_NOT_FOUND;
		}
	} else {
		if (ACPI_FAILURE(acpi_find_root_pointer(flags, addr))) {
			printk(KERN_ERR PREFIX
			       "System description tables not found\n");
			return AE_NOT_FOUND;
		}
	}

	return AE_OK;
}

acpi_status
acpi_os_map_memory(acpi_physical_address phys, acpi_size size,
		   void __iomem ** virt)
{
	if (efi_enabled) {
		if (EFI_MEMORY_WB & efi_mem_attributes(phys)) {
			*virt = (void __iomem *)phys_to_virt(phys);
		} else {
			*virt = ioremap(phys, size);
		}
	} else {
		if (phys > ULONG_MAX) {
			printk(KERN_ERR PREFIX "Cannot map memory that high\n");
			return AE_BAD_PARAMETER;
		}
		/*
		 * ioremap checks to ensure this is in reserved space
		 */
		*virt = ioremap((unsigned long)phys, size);
	}

	if (!*virt)
		return AE_NO_MEMORY;

	return AE_OK;
}

void acpi_os_unmap_memory(void __iomem * virt, acpi_size size)
{
	iounmap(virt);
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

#ifdef CONFIG_ACPI_CUSTOM_DSDT
	if (strncmp(existing_table->signature, "DSDT", 4) == 0)
		*new_table = (struct acpi_table_header *)AmlCode;
	else
		*new_table = NULL;
#else
	*new_table = NULL;
#endif
	return AE_OK;
}

static irqreturn_t acpi_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	return (*acpi_irq_handler) (acpi_irq_context) ? IRQ_HANDLED : IRQ_NONE;
}

acpi_status
acpi_os_install_interrupt_handler(u32 gsi, acpi_osd_handler handler,
				  void *context)
{
	unsigned int irq;

	/*
	 * Ignore the GSI from the core, and use the value in our copy of the
	 * FADT. It may not be the same if an interrupt source override exists
	 * for the SCI.
	 */
	gsi = acpi_fadt.sci_int;
	if (acpi_gsi_to_irq(gsi, &irq) < 0) {
		printk(KERN_ERR PREFIX "SCI (ACPI GSI %d) not registered\n",
		       gsi);
		return AE_OK;
	}

	acpi_irq_handler = handler;
	acpi_irq_context = context;
	if (request_irq(irq, acpi_irq, SA_SHIRQ, "acpi", acpi_irq)) {
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

void acpi_os_sleep(acpi_integer ms)
{
	schedule_timeout_interruptible(msecs_to_jiffies(ms));
}

EXPORT_SYMBOL(acpi_os_sleep);

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

EXPORT_SYMBOL(acpi_os_stall);

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

	switch (width) {
	case 8:
		*(u8 *) value = inb(port);
		break;
	case 16:
		*(u16 *) value = inw(port);
		break;
	case 32:
		*(u32 *) value = inl(port);
		break;
	default:
		BUG();
	}

	return AE_OK;
}

EXPORT_SYMBOL(acpi_os_read_port);

acpi_status acpi_os_write_port(acpi_io_address port, u32 value, u32 width)
{
	switch (width) {
	case 8:
		outb(value, port);
		break;
	case 16:
		outw(value, port);
		break;
	case 32:
		outl(value, port);
		break;
	default:
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
	int iomem = 0;

	if (efi_enabled) {
		if (EFI_MEMORY_WB & efi_mem_attributes(phys_addr)) {
			/* HACK ALERT! We can use readb/w/l on real memory too.. */
			virt_addr = (void __iomem *)phys_to_virt(phys_addr);
		} else {
			iomem = 1;
			virt_addr = ioremap(phys_addr, width);
		}
	} else
		virt_addr = (void __iomem *)phys_to_virt(phys_addr);
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

	if (efi_enabled) {
		if (iomem)
			iounmap(virt_addr);
	}

	return AE_OK;
}

acpi_status
acpi_os_write_memory(acpi_physical_address phys_addr, u32 value, u32 width)
{
	void __iomem *virt_addr;
	int iomem = 0;

	if (efi_enabled) {
		if (EFI_MEMORY_WB & efi_mem_attributes(phys_addr)) {
			/* HACK ALERT! We can use writeb/w/l on real memory too */
			virt_addr = (void __iomem *)phys_to_virt(phys_addr);
		} else {
			iomem = 1;
			virt_addr = ioremap(phys_addr, width);
		}
	} else
		virt_addr = (void __iomem *)phys_to_virt(phys_addr);

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

	if (iomem)
		iounmap(virt_addr);

	return AE_OK;
}

acpi_status
acpi_os_read_pci_configuration(struct acpi_pci_id * pci_id, u32 reg,
			       void *value, u32 width)
{
	int result, size;

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

	BUG_ON(!raw_pci_ops);

	result = raw_pci_ops->read(pci_id->segment, pci_id->bus,
				   PCI_DEVFN(pci_id->device, pci_id->function),
				   reg, size, value);

	return (result ? AE_ERROR : AE_OK);
}

EXPORT_SYMBOL(acpi_os_read_pci_configuration);

acpi_status
acpi_os_write_pci_configuration(struct acpi_pci_id * pci_id, u32 reg,
				acpi_integer value, u32 width)
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

	BUG_ON(!raw_pci_ops);

	result = raw_pci_ops->write(pci_id->segment, pci_id->bus,
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
	unsigned long temp;
	acpi_object_type type;
	u8 tu8;

	acpi_get_parent(chandle, &handle);
	if (handle != rhandle) {
		acpi_os_derive_pci_id_2(rhandle, handle, &pci_id, is_bridge,
					bus_number);

		status = acpi_get_type(handle, &type);
		if ((ACPI_FAILURE(status)) || (type != ACPI_TYPE_DEVICE))
			return;

		status =
		    acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL,
					  &temp);
		if (ACPI_SUCCESS(status)) {
			pci_id->device = ACPI_HIWORD(ACPI_LODWORD(temp));
			pci_id->function = ACPI_LOWORD(ACPI_LODWORD(temp));

			if (*is_bridge)
				pci_id->bus = *bus_number;

			/* any nicer way to get bus number of bridge ? */
			status =
			    acpi_os_read_pci_configuration(pci_id, 0x0e, &tu8,
							   8);
			if (ACPI_SUCCESS(status)
			    && ((tu8 & 0x7f) == 1 || (tu8 & 0x7f) == 2)) {
				status =
				    acpi_os_read_pci_configuration(pci_id, 0x18,
								   &tu8, 8);
				if (!ACPI_SUCCESS(status)) {
					/* Certainly broken...  FIX ME */
					return;
				}
				*is_bridge = 1;
				pci_id->bus = tu8;
				status =
				    acpi_os_read_pci_configuration(pci_id, 0x19,
								   &tu8, 8);
				if (ACPI_SUCCESS(status)) {
					*bus_number = tu8;
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

static void acpi_os_execute_deferred(void *context)
{
	struct acpi_os_dpc *dpc = NULL;

	ACPI_FUNCTION_TRACE("os_execute_deferred");

	dpc = (struct acpi_os_dpc *)context;
	if (!dpc) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid (NULL) context.\n"));
		return_VOID;
	}

	dpc->function(dpc->context);

	kfree(dpc);

	return_VOID;
}

acpi_status
acpi_os_queue_for_execution(u32 priority,
			    acpi_osd_exec_callback function, void *context)
{
	acpi_status status = AE_OK;
	struct acpi_os_dpc *dpc;
	struct work_struct *task;

	ACPI_FUNCTION_TRACE("os_queue_for_execution");

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Scheduling function [%p(%p)] for deferred execution.\n",
			  function, context));

	if (!function)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	/*
	 * Allocate/initialize DPC structure.  Note that this memory will be
	 * freed by the callee.  The kernel handles the tq_struct list  in a
	 * way that allows us to also free its memory inside the callee.
	 * Because we may want to schedule several tasks with different
	 * parameters we can't use the approach some kernel code uses of
	 * having a static tq_struct.
	 * We can save time and code by allocating the DPC and tq_structs
	 * from the same memory.
	 */

	dpc =
	    kmalloc(sizeof(struct acpi_os_dpc) + sizeof(struct work_struct),
		    GFP_ATOMIC);
	if (!dpc)
		return_ACPI_STATUS(AE_NO_MEMORY);

	dpc->function = function;
	dpc->context = context;

	task = (void *)(dpc + 1);
	INIT_WORK(task, acpi_os_execute_deferred, (void *)dpc);

	if (!queue_work(kacpid_wq, task)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Call to queue_work() failed.\n"));
		kfree(dpc);
		status = AE_ERROR;
	}

	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_os_queue_for_execution);

void acpi_os_wait_events_complete(void *context)
{
	flush_workqueue(kacpid_wq);
}

EXPORT_SYMBOL(acpi_os_wait_events_complete);

/*
 * Allocate the memory for a spinlock and initialize it.
 */
acpi_status acpi_os_create_lock(acpi_handle * out_handle)
{
	spinlock_t *lock_ptr;

	ACPI_FUNCTION_TRACE("os_create_lock");

	lock_ptr = acpi_os_allocate(sizeof(spinlock_t));

	spin_lock_init(lock_ptr);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Creating spinlock[%p].\n", lock_ptr));

	*out_handle = lock_ptr;

	return_ACPI_STATUS(AE_OK);
}

/*
 * Deallocate the memory for a spinlock.
 */
void acpi_os_delete_lock(acpi_handle handle)
{
	ACPI_FUNCTION_TRACE("os_create_lock");

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Deleting spinlock[%p].\n", handle));

	acpi_os_free(handle);

	return_VOID;
}

acpi_status
acpi_os_create_semaphore(u32 max_units, u32 initial_units, acpi_handle * handle)
{
	struct semaphore *sem = NULL;

	ACPI_FUNCTION_TRACE("os_create_semaphore");

	sem = acpi_os_allocate(sizeof(struct semaphore));
	if (!sem)
		return_ACPI_STATUS(AE_NO_MEMORY);
	memset(sem, 0, sizeof(struct semaphore));

	sema_init(sem, initial_units);

	*handle = (acpi_handle *) sem;

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Creating semaphore[%p|%d].\n",
			  *handle, initial_units));

	return_ACPI_STATUS(AE_OK);
}

EXPORT_SYMBOL(acpi_os_create_semaphore);

/*
 * TODO: A better way to delete semaphores?  Linux doesn't have a
 * 'delete_semaphore()' function -- may result in an invalid
 * pointer dereference for non-synchronized consumers.	Should
 * we at least check for blocked threads and signal/cancel them?
 */

acpi_status acpi_os_delete_semaphore(acpi_handle handle)
{
	struct semaphore *sem = (struct semaphore *)handle;

	ACPI_FUNCTION_TRACE("os_delete_semaphore");

	if (!sem)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Deleting semaphore[%p].\n", handle));

	acpi_os_free(sem);
	sem = NULL;

	return_ACPI_STATUS(AE_OK);
}

EXPORT_SYMBOL(acpi_os_delete_semaphore);

/*
 * TODO: The kernel doesn't have a 'down_timeout' function -- had to
 * improvise.  The process is to sleep for one scheduler quantum
 * until the semaphore becomes available.  Downside is that this
 * may result in starvation for timeout-based waits when there's
 * lots of semaphore activity.
 *
 * TODO: Support for units > 1?
 */
acpi_status acpi_os_wait_semaphore(acpi_handle handle, u32 units, u16 timeout)
{
	acpi_status status = AE_OK;
	struct semaphore *sem = (struct semaphore *)handle;
	int ret = 0;

	ACPI_FUNCTION_TRACE("os_wait_semaphore");

	if (!sem || (units < 1))
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	if (units > 1)
		return_ACPI_STATUS(AE_SUPPORT);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Waiting for semaphore[%p|%d|%d]\n",
			  handle, units, timeout));

	if (in_atomic())
		timeout = 0;

	switch (timeout) {
		/*
		 * No Wait:
		 * --------
		 * A zero timeout value indicates that we shouldn't wait - just
		 * acquire the semaphore if available otherwise return AE_TIME
		 * (a.k.a. 'would block').
		 */
	case 0:
		if (down_trylock(sem))
			status = AE_TIME;
		break;

		/*
		 * Wait Indefinitely:
		 * ------------------
		 */
	case ACPI_WAIT_FOREVER:
		down(sem);
		break;

		/*
		 * Wait w/ Timeout:
		 * ----------------
		 */
	default:
		// TODO: A better timeout algorithm?
		{
			int i = 0;
			static const int quantum_ms = 1000 / HZ;

			ret = down_trylock(sem);
			for (i = timeout; (i > 0 && ret < 0); i -= quantum_ms) {
				schedule_timeout_interruptible(1);
				ret = down_trylock(sem);
			}

			if (ret != 0)
				status = AE_TIME;
		}
		break;
	}

	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Failed to acquire semaphore[%p|%d|%d], %s\n",
				  handle, units, timeout,
				  acpi_format_exception(status)));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
				  "Acquired semaphore[%p|%d|%d]\n", handle,
				  units, timeout));
	}

	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_os_wait_semaphore);

/*
 * TODO: Support for units > 1?
 */
acpi_status acpi_os_signal_semaphore(acpi_handle handle, u32 units)
{
	struct semaphore *sem = (struct semaphore *)handle;

	ACPI_FUNCTION_TRACE("os_signal_semaphore");

	if (!sem || (units < 1))
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	if (units > 1)
		return_ACPI_STATUS(AE_SUPPORT);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Signaling semaphore[%p|%d]\n", handle,
			  units));

	up(sem);

	return_ACPI_STATUS(AE_OK);
}

EXPORT_SYMBOL(acpi_os_signal_semaphore);

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

/* Assumes no unreadable holes inbetween */
u8 acpi_os_readable(void *ptr, acpi_size len)
{
#if defined(__i386__) || defined(__x86_64__)
	char tmp;
	return !__get_user(tmp, (char __user *)ptr)
	    && !__get_user(tmp, (char __user *)ptr + len - 1);
#endif
	return 1;
}

#ifdef ACPI_FUTURE_USAGE
u8 acpi_os_writable(void *ptr, acpi_size len)
{
	/* could do dummy write (racy) or a kernel page table lookup.
	   The later may be difficult at early boot when kmap doesn't work yet. */
	return 1;
}
#endif

u32 acpi_os_get_thread_id(void)
{
	if (!in_atomic())
		return current->pid;

	return 0;
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

EXPORT_SYMBOL(acpi_os_signal);

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

/*
 * _OSI control
 * empty string disables _OSI
 * TBD additional string adds to _OSI
 */
static int __init acpi_osi_setup(char *str)
{
	if (str == NULL || *str == '\0') {
		printk(KERN_INFO PREFIX "_OSI method disabled\n");
		acpi_gbl_create_osi_method = FALSE;
	} else {
		/* TBD */
		printk(KERN_ERR PREFIX "_OSI additional string ignored -- %s\n",
		       str);
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

/*
 * Wake and Run-Time GPES are expected to be separate.
 * We disable wake-GPEs at run-time to prevent spurious
 * interrupts.
 *
 * However, if a system exists that shares Wake and
 * Run-time events on the same GPE this flag is available
 * to tell Linux to keep the wake-time GPEs enabled at run-time.
 */
static int __init acpi_wake_gpes_always_on_setup(char *str)
{
	printk(KERN_INFO PREFIX "wake GPEs not disabled\n");

	acpi_gbl_leave_wake_gpes_disabled = FALSE;

	return 1;
}

__setup("acpi_wake_gpes_always_on", acpi_wake_gpes_always_on_setup);

static int __init acpi_hotkey_setup(char *str)
{
	acpi_specific_hotkey_enabled = FALSE;
	return 1;
}

__setup("acpi_generic_hotkey", acpi_hotkey_setup);

/*
 * max_cstate is defined in the base kernel so modules can
 * change it w/o depending on the state of the processor module.
 */
unsigned int max_cstate = ACPI_PROCESSOR_MAX_POWER;

EXPORT_SYMBOL(max_cstate);

/*
 * Acquire a spinlock.
 *
 * handle is a pointer to the spinlock_t.
 * flags is *not* the result of save_flags - it is an ACPI-specific flag variable
 *   that indicates whether we are at interrupt level.
 */

unsigned long acpi_os_acquire_lock(acpi_handle handle)
{
	unsigned long flags;
	spin_lock_irqsave((spinlock_t *) handle, flags);
	return flags;
}

/*
 * Release a spinlock. See above.
 */

void acpi_os_release_lock(acpi_handle handle, unsigned long flags)
{
	spin_unlock_irqrestore((spinlock_t *) handle, flags);
}

#ifndef ACPI_USE_LOCAL_CACHE

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_create_cache
 *
 * PARAMETERS:  CacheName       - Ascii name for the cache
 *              ObjectSize      - Size of each cached object
 *              MaxDepth        - Maximum depth of the cache (in objects)
 *              ReturnCache     - Where the new cache object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a cache object
 *
 ******************************************************************************/

acpi_status
acpi_os_create_cache(char *name, u16 size, u16 depth, acpi_cache_t ** cache)
{
	*cache = kmem_cache_create(name, size, 0, 0, NULL, NULL);
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
	(void)kmem_cache_shrink(cache);
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
	(void)kmem_cache_destroy(cache);
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

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_acquire_object
 *
 * PARAMETERS:  Cache           - Handle to cache object
 *              ReturnObject    - Where the object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get an object from the specified cache.  If cache is empty,
 *              the object is allocated.
 *
 ******************************************************************************/

void *acpi_os_acquire_object(acpi_cache_t * cache)
{
	void *object = kmem_cache_alloc(cache, GFP_KERNEL);
	WARN_ON(!object);
	return object;
}

#endif
