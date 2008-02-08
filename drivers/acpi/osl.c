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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/workqueue.h>
#include <linux/nmi.h>
#include <linux/acpi.h>
#include <acpi/acpi.h>
#include <asm/io.h>
#include <acpi/acpi_bus.h>
#include <acpi/processor.h>
#include <asm/uaccess.h>

#include <linux/efi.h>
#include <linux/ioport.h>
#include <linux/list.h>

#define _COMPONENT		ACPI_OS_SERVICES
ACPI_MODULE_NAME("osl");
#define PREFIX		"ACPI: "
struct acpi_os_dpc {
	acpi_osd_exec_callback function;
	void *context;
	struct work_struct work;
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

struct acpi_res_list {
	resource_size_t start;
	resource_size_t end;
	acpi_adr_space_type resource_type; /* IO port, System memory, ...*/
	char name[5];   /* only can have a length of 4 chars, make use of this
			   one instead of res->name, no need to kalloc then */
	struct list_head resource_list;
};

static LIST_HEAD(resource_list_head);
static DEFINE_SPINLOCK(acpi_res_lock);

#define	OSI_STRING_LENGTH_MAX 64	/* arbitrary */
static char osi_additional_string[OSI_STRING_LENGTH_MAX];

#ifdef CONFIG_ACPI_CUSTOM_DSDT_INITRD
static int acpi_no_initrd_override;
#endif

/*
 * "Ode to _OSI(Linux)"
 *
 * osi_linux -- Control response to BIOS _OSI(Linux) query.
 *
 * As Linux evolves, the features that it supports change.
 * So an OSI string such as "Linux" is not specific enough
 * to be useful across multiple versions of Linux.  It
 * doesn't identify any particular feature, interface,
 * or even any particular version of Linux...
 *
 * Unfortunately, Linux-2.6.22 and earlier responded "yes"
 * to a BIOS _OSI(Linux) query.  When
 * a reference mobile BIOS started using it, its use
 * started to spread to many vendor platforms.
 * As it is not supportable, we need to halt that spread.
 *
 * Today, most BIOS references to _OSI(Linux) are noise --
 * they have no functional effect and are just dead code
 * carried over from the reference BIOS.
 *
 * The next most common case is that _OSI(Linux) harms Linux,
 * usually by causing the BIOS to follow paths that are
 * not tested during Windows validation.
 *
 * Finally, there is a short list of platforms
 * where OSI(Linux) benefits Linux.
 *
 * In Linux-2.6.23, OSI(Linux) is first disabled by default.
 * DMI is used to disable the dmesg warning about OSI(Linux)
 * on platforms where it is known to have no effect.
 * But a dmesg warning remains for systems where
 * we do not know if OSI(Linux) is good or bad for the system.
 * DMI is also used to enable OSI(Linux) for the machines
 * that are known to need it.
 *
 * BIOS writers should NOT query _OSI(Linux) on future systems.
 * It will be ignored by default, and to get Linux to
 * not ignore it will require a kernel source update to
 * add a DMI entry, or a boot-time "acpi_osi=Linux" invocation.
 */
#define OSI_LINUX_ENABLE 0

static struct osi_linux {
	unsigned int	enable:1;
	unsigned int	dmi:1;
	unsigned int	cmdline:1;
	unsigned int	known:1;
} osi_linux = { OSI_LINUX_ENABLE, 0, 0, 0};

static void __init acpi_request_region (struct acpi_generic_address *addr,
	unsigned int length, char *desc)
{
	struct resource *res;

	if (!addr->address || !length)
		return;

	if (addr->space_id == ACPI_ADR_SPACE_SYSTEM_IO)
		res = request_region(addr->address, length, desc);
	else if (addr->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		res = request_mem_region(addr->address, length, desc);
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
	kacpi_notify_wq = create_singlethread_workqueue("kacpi_notify");
	BUG_ON(!kacpid_wq);
	BUG_ON(!kacpi_notify_wq);
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
		printk("%s", buffer);
	}
#else
	printk("%s", buffer);
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

void acpi_os_unmap_memory(void __iomem * virt, acpi_size size)
{
	if (acpi_gbl_permanent_mmap) {
		iounmap(virt);
	}
}
EXPORT_SYMBOL_GPL(acpi_os_unmap_memory);

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

#ifdef CONFIG_ACPI_CUSTOM_DSDT_INITRD
struct acpi_table_header *acpi_find_dsdt_initrd(void)
{
	struct file *firmware_file;
	mm_segment_t oldfs;
	unsigned long len, len2;
	struct acpi_table_header *dsdt_buffer, *ret = NULL;
	struct kstat stat;
	char *ramfs_dsdt_name = "/DSDT.aml";

	printk(KERN_INFO PREFIX "Checking initramfs for custom DSDT\n");

	/*
	 * Never do this at home, only the user-space is allowed to open a file.
	 * The clean way would be to use the firmware loader.
	 * But this code must be run before there is any userspace available.
	 * A static/init firmware infrastructure doesn't exist yet...
	 */
	if (vfs_stat(ramfs_dsdt_name, &stat) < 0)
		return ret;

	len = stat.size;
	/* check especially against empty files */
	if (len <= 4) {
		printk(KERN_ERR PREFIX "Failed: DSDT only %lu bytes.\n", len);
		return ret;
	}

	firmware_file = filp_open(ramfs_dsdt_name, O_RDONLY, 0);
	if (IS_ERR(firmware_file)) {
		printk(KERN_ERR PREFIX "Failed to open %s.\n", ramfs_dsdt_name);
		return ret;
	}

	dsdt_buffer = kmalloc(len, GFP_ATOMIC);
	if (!dsdt_buffer) {
		printk(KERN_ERR PREFIX "Failed to allocate %lu bytes.\n", len);
		goto err;
	}

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	len2 = vfs_read(firmware_file, (char __user *)dsdt_buffer, len,
		&firmware_file->f_pos);
	set_fs(oldfs);
	if (len2 < len) {
		printk(KERN_ERR PREFIX "Failed to read %lu bytes from %s.\n",
			len, ramfs_dsdt_name);
		ACPI_FREE(dsdt_buffer);
		goto err;
	}

	printk(KERN_INFO PREFIX "Found %lu byte DSDT in %s.\n",
			len, ramfs_dsdt_name);
	ret = dsdt_buffer;
err:
	filp_close(firmware_file, NULL);
	return ret;
}
#endif

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
#ifdef CONFIG_ACPI_CUSTOM_DSDT_INITRD
	if ((strncmp(existing_table->signature, "DSDT", 4) == 0) &&
	    !acpi_no_initrd_override) {
		struct acpi_table_header *initrd_table;

		initrd_table = acpi_find_dsdt_initrd();
		if (initrd_table)
			*new_table = initrd_table;
	}
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

#ifdef CONFIG_ACPI_CUSTOM_DSDT_INITRD
int __init acpi_no_initrd_override_setup(char *s)
{
	acpi_no_initrd_override = 1;
	return 1;
}
__setup("acpi_no_initrd_override", acpi_no_initrd_override_setup);
#endif

static irqreturn_t acpi_irq(int irq, void *dev_id)
{
	u32 handled;

	handled = (*acpi_irq_handler) (acpi_irq_context);

	if (handled) {
		acpi_irq_handled++;
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;
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

void acpi_os_sleep(acpi_integer ms)
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

static void acpi_os_execute_deferred(struct work_struct *work)
{
	struct acpi_os_dpc *dpc = container_of(work, struct acpi_os_dpc, work);
	if (!dpc) {
		printk(KERN_ERR PREFIX "Invalid (NULL) context\n");
		return;
	}

	dpc->function(dpc->context);
	kfree(dpc);

	return;
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

acpi_status acpi_os_execute(acpi_execute_type type,
			    acpi_osd_exec_callback function, void *context)
{
	acpi_status status = AE_OK;
	struct acpi_os_dpc *dpc;
	struct workqueue_struct *queue;
	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Scheduling function [%p(%p)] for deferred execution.\n",
			  function, context));

	if (!function)
		return AE_BAD_PARAMETER;

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
		return_ACPI_STATUS(AE_NO_MEMORY);

	dpc->function = function;
	dpc->context = context;

	INIT_WORK(&dpc->work, acpi_os_execute_deferred);
	queue = (type == OSL_NOTIFY_HANDLER) ? kacpi_notify_wq : kacpid_wq;
	if (!queue_work(queue, &dpc->work)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			  "Call to queue_work() failed.\n"));
		status = AE_ERROR;
		kfree(dpc);
	}
	return_ACPI_STATUS(status);
}

EXPORT_SYMBOL(acpi_os_execute);

void acpi_os_wait_events_complete(void *context)
{
	flush_workqueue(kacpid_wq);
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

	kfree(sem);
	sem = NULL;

	return AE_OK;
}

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


	if (!sem || (units < 1))
		return AE_BAD_PARAMETER;

	if (units > 1)
		return AE_SUPPORT;

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Waiting for semaphore[%p|%d|%d]\n",
			  handle, units, timeout));

	/*
	 * This can be called during resume with interrupts off.
	 * Like boot-time, we should be single threaded and will
	 * always get the lock if we try -- timeout or not.
	 * If this doesn't succeed, then we will oops courtesy of
	 * might_sleep() in down().
	 */
	if (!down_trylock(sem))
		return AE_OK;

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
			for (i = timeout; (i > 0 && ret != 0); i -= quantum_ms) {
				schedule_timeout_interruptible(1);
				ret = down_trylock(sem);
			}

			if (ret != 0)
				status = AE_TIME;
		}
		break;
	}

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
static int __init acpi_osi_setup(char *str)
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

/* Check of resource interference between native drivers and ACPI
 * OperationRegions (SystemIO and System Memory only).
 * IO ports and memory declared in ACPI might be used by the ACPI subsystem
 * in arbitrary AML code and can interfere with legacy drivers.
 * acpi_enforce_resources= can be set to:
 *
 *   - strict           (2)
 *     -> further driver trying to access the resources will not load
 *   - lax (default)    (1)
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

static unsigned int acpi_enforce_resources = ENFORCE_RESOURCES_LAX;

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
int acpi_check_resource_conflict(struct resource *res)
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
			printk(KERN_INFO "%sACPI: %s resource %s [0x%llx-0x%llx]"
			       " conflicts with ACPI region %s"
			       " [0x%llx-0x%llx]\n",
			       acpi_enforce_resources == ENFORCE_RESOURCES_LAX
			       ? KERN_WARNING : KERN_ERR,
			       ioport ? "I/O" : "Memory", res->name,
			       (long long) res->start, (long long) res->end,
			       res_list_elem->name,
			       (long long) res_list_elem->start,
			       (long long) res_list_elem->end);
			printk(KERN_INFO "ACPI: Device needs an ACPI driver\n");
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

/**
 *	acpi_dmi_dump - dump DMI slots needed for blacklist entry
 *
 *	Returns 0 on success
 */
static int acpi_dmi_dump(void)
{

	if (!dmi_available)
		return -1;

	printk(KERN_NOTICE PREFIX "DMI System Vendor: %s\n",
		dmi_get_system_info(DMI_SYS_VENDOR));
	printk(KERN_NOTICE PREFIX "DMI Product Name: %s\n",
		dmi_get_system_info(DMI_PRODUCT_NAME));
	printk(KERN_NOTICE PREFIX "DMI Product Version: %s\n",
		dmi_get_system_info(DMI_PRODUCT_VERSION));
	printk(KERN_NOTICE PREFIX "DMI Board Name: %s\n",
		dmi_get_system_info(DMI_BOARD_NAME));
	printk(KERN_NOTICE PREFIX "DMI BIOS Vendor: %s\n",
		dmi_get_system_info(DMI_BIOS_VENDOR));
	printk(KERN_NOTICE PREFIX "DMI BIOS Date: %s\n",
		dmi_get_system_info(DMI_BIOS_DATE));

	return 0;
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

		if (!osi_linux.dmi) {
			if (acpi_dmi_dump())
				printk(KERN_NOTICE PREFIX
					"[please extract dmidecode output]\n");
			printk(KERN_NOTICE PREFIX
				"Please send DMI info above to "
				"linux-acpi@vger.kernel.org\n");
		}
		if (!osi_linux.known && !osi_linux.cmdline) {
			printk(KERN_NOTICE PREFIX
				"If \"acpi_osi=%sLinux\" works better, "
				"please notify linux-acpi@vger.kernel.org\n",
				osi_linux.enable ? "!" : "");
		}

		if (osi_linux.enable)
			return AE_OK;
	}
	return AE_SUPPORT;
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
	if (acpi_enforce_resources == ENFORCE_RESOURCES_NO)
		return AE_OK;

	switch (space_id) {
	case ACPI_ADR_SPACE_SYSTEM_IO:
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:
		/* Only interference checks against SystemIO and SytemMemory
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
		list_add(&res->resource_list, &resource_list_head);
		spin_unlock(&acpi_res_lock);
		pr_debug("Added %s resource: start: 0x%llx, end: 0x%llx, "
			 "name: %s\n", (space_id == ACPI_ADR_SPACE_SYSTEM_IO)
			 ? "SystemIO" : "System Memory",
			 (unsigned long long)res->start,
			 (unsigned long long)res->end,
			 res->name);
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
