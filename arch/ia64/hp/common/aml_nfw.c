/*
 * OpRegion handler to allow AML to call native firmware
 *
 * (c) Copyright 2007 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This driver implements HP Open Source Review Board proposal 1842,
 * which was approved on 9/20/2006.
 *
 * For technical documentation, see the HP SPPA Firmware EAS, Appendix F.
 *
 * ACPI does not define a mechanism for AML methods to call native firmware
 * interfaces such as PAL or SAL.  This OpRegion handler adds such a mechanism.
 * After the handler is installed, an AML method can call native firmware by
 * storing the arguments and firmware entry point to specific offsets in the
 * OpRegion.  When AML reads the "return value" offset from the OpRegion, this
 * handler loads up the arguments, makes the firmware call, and returns the
 * result.
 */

#include <linux/module.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <asm/sal.h>

MODULE_AUTHOR("Bjorn Helgaas <bjorn.helgaas@hp.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ACPI opregion handler for native firmware calls");

static int force_register;
module_param_named(force, force_register, bool, 0);
MODULE_PARM_DESC(force, "Install opregion handler even without HPQ5001 device");

#define AML_NFW_SPACE		0xA1

struct ia64_pdesc {
	void *ip;
	void *gp;
};

/*
 * N.B.  The layout of this structure is defined in the HP SPPA FW EAS, and
 *	 the member offsets are embedded in AML methods.
 */
struct ia64_nfw_context {
	u64 arg[8];
	struct ia64_sal_retval ret;
	u64 ip;
	u64 gp;
	u64 pad[2];
};

static void *virt_map(u64 address)
{
	if (address & (1UL << 63))
		return (void *) (__IA64_UNCACHED_OFFSET | address);

	return __va(address);
}

static void aml_nfw_execute(struct ia64_nfw_context *c)
{
	struct ia64_pdesc virt_entry;
	ia64_sal_handler entry;

	virt_entry.ip = virt_map(c->ip);
	virt_entry.gp = virt_map(c->gp);

	entry = (ia64_sal_handler) &virt_entry;

	IA64_FW_CALL(entry, c->ret,
		     c->arg[0], c->arg[1], c->arg[2], c->arg[3],
		     c->arg[4], c->arg[5], c->arg[6], c->arg[7]);
}

static void aml_nfw_read_arg(u8 *offset, u32 bit_width, acpi_integer *value)
{
	switch (bit_width) {
	case 8:
		*value = *(u8 *)offset;
		break;
	case 16:
		*value = *(u16 *)offset;
		break;
	case 32:
		*value = *(u32 *)offset;
		break;
	case 64:
		*value = *(u64 *)offset;
		break;
	}
}

static void aml_nfw_write_arg(u8 *offset, u32 bit_width, acpi_integer *value)
{
	switch (bit_width) {
	case 8:
		*(u8 *) offset = *value;
		break;
	case 16:
		*(u16 *) offset = *value;
		break;
	case 32:
		*(u32 *) offset = *value;
		break;
	case 64:
		*(u64 *) offset = *value;
		break;
	}
}

static acpi_status aml_nfw_handler(u32 function, acpi_physical_address address,
	u32 bit_width, acpi_integer *value, void *handler_context,
	void *region_context)
{
	struct ia64_nfw_context *context = handler_context;
	u8 *offset = (u8 *) context + address;

	if (bit_width !=  8 && bit_width != 16 &&
	    bit_width != 32 && bit_width != 64)
		return AE_BAD_PARAMETER;

	if (address + (bit_width >> 3) > sizeof(struct ia64_nfw_context))
		return AE_BAD_PARAMETER;

	switch (function) {
	case ACPI_READ:
		if (address == offsetof(struct ia64_nfw_context, ret))
			aml_nfw_execute(context);
		aml_nfw_read_arg(offset, bit_width, value);
		break;
	case ACPI_WRITE:
		aml_nfw_write_arg(offset, bit_width, value);
		break;
	}

	return AE_OK;
}

static struct ia64_nfw_context global_context;
static int global_handler_registered;

static int aml_nfw_add_global_handler(void)
{
	acpi_status status;

	if (global_handler_registered)
		return 0;

	status = acpi_install_address_space_handler(ACPI_ROOT_OBJECT,
		AML_NFW_SPACE, aml_nfw_handler, NULL, &global_context);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	global_handler_registered = 1;
	printk(KERN_INFO "Global 0x%02X opregion handler registered\n",
		AML_NFW_SPACE);
	return 0;
}

static int aml_nfw_remove_global_handler(void)
{
	acpi_status status;

	if (!global_handler_registered)
		return 0;

	status = acpi_remove_address_space_handler(ACPI_ROOT_OBJECT,
		AML_NFW_SPACE, aml_nfw_handler);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	global_handler_registered = 0;
	printk(KERN_INFO "Global 0x%02X opregion handler removed\n",
		AML_NFW_SPACE);
	return 0;
}

static int aml_nfw_add(struct acpi_device *device)
{
	/*
	 * We would normally allocate a new context structure and install
	 * the address space handler for the specific device we found.
	 * But the HP-UX implementation shares a single global context
	 * and always puts the handler at the root, so we'll do the same.
	 */
	return aml_nfw_add_global_handler();
}

static int aml_nfw_remove(struct acpi_device *device, int type)
{
	return aml_nfw_remove_global_handler();
}

static const struct acpi_device_id aml_nfw_ids[] = {
	{"HPQ5001", 0},
	{"", 0}
};

static struct acpi_driver acpi_aml_nfw_driver = {
	.name = "native firmware",
	.ids = aml_nfw_ids,
	.ops = {
		.add = aml_nfw_add,
		.remove = aml_nfw_remove,
		},
};

static int __init aml_nfw_init(void)
{
	int result;

	if (force_register)
		aml_nfw_add_global_handler();

	result = acpi_bus_register_driver(&acpi_aml_nfw_driver);
	if (result < 0) {
		aml_nfw_remove_global_handler();
		return result;
	}

	return 0;
}

static void __exit aml_nfw_exit(void)
{
	acpi_bus_unregister_driver(&acpi_aml_nfw_driver);
	aml_nfw_remove_global_handler();
}

module_init(aml_nfw_init);
module_exit(aml_nfw_exit);
