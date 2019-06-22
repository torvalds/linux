// SPDX-License-Identifier: GPL-2.0+

#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include "ipmi_si.h"

#define PFX "ipmi_hardcode: "
/*
 * There can be 4 IO ports passed in (with or without IRQs), 4 addresses,
 * a default IO port, and 1 ACPI/SPMI address.  That sets SI_MAX_DRIVERS.
 */

#define SI_MAX_PARMS 4

#define MAX_SI_TYPE_STR 30
static char          si_type_str[MAX_SI_TYPE_STR] __initdata;
static unsigned long addrs[SI_MAX_PARMS];
static unsigned int num_addrs;
static unsigned int  ports[SI_MAX_PARMS];
static unsigned int num_ports;
static int           irqs[SI_MAX_PARMS] __initdata;
static unsigned int num_irqs __initdata;
static int           regspacings[SI_MAX_PARMS] __initdata;
static unsigned int num_regspacings __initdata;
static int           regsizes[SI_MAX_PARMS] __initdata;
static unsigned int num_regsizes __initdata;
static int           regshifts[SI_MAX_PARMS] __initdata;
static unsigned int num_regshifts __initdata;
static int slave_addrs[SI_MAX_PARMS] __initdata;
static unsigned int num_slave_addrs __initdata;

module_param_string(type, si_type_str, MAX_SI_TYPE_STR, 0);
MODULE_PARM_DESC(type, "Defines the type of each interface, each"
		 " interface separated by commas.  The types are 'kcs',"
		 " 'smic', and 'bt'.  For example si_type=kcs,bt will set"
		 " the first interface to kcs and the second to bt");
module_param_hw_array(addrs, ulong, iomem, &num_addrs, 0);
MODULE_PARM_DESC(addrs, "Sets the memory address of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " is in memory.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_hw_array(ports, uint, ioport, &num_ports, 0);
MODULE_PARM_DESC(ports, "Sets the port address of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " is a port.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_hw_array(irqs, int, irq, &num_irqs, 0);
MODULE_PARM_DESC(irqs, "Sets the interrupt of each interface, the"
		 " addresses separated by commas.  Only use if an interface"
		 " has an interrupt.  Otherwise, set it to zero or leave"
		 " it blank.");
module_param_hw_array(regspacings, int, other, &num_regspacings, 0);
MODULE_PARM_DESC(regspacings, "The number of bytes between the start address"
		 " and each successive register used by the interface.  For"
		 " instance, if the start address is 0xca2 and the spacing"
		 " is 2, then the second address is at 0xca4.  Defaults"
		 " to 1.");
module_param_hw_array(regsizes, int, other, &num_regsizes, 0);
MODULE_PARM_DESC(regsizes, "The size of the specific IPMI register in bytes."
		 " This should generally be 1, 2, 4, or 8 for an 8-bit,"
		 " 16-bit, 32-bit, or 64-bit register.  Use this if you"
		 " the 8-bit IPMI register has to be read from a larger"
		 " register.");
module_param_hw_array(regshifts, int, other, &num_regshifts, 0);
MODULE_PARM_DESC(regshifts, "The amount to shift the data read from the."
		 " IPMI register, in bits.  For instance, if the data"
		 " is read from a 32-bit word and the IPMI data is in"
		 " bit 8-15, then the shift would be 8");
module_param_hw_array(slave_addrs, int, other, &num_slave_addrs, 0);
MODULE_PARM_DESC(slave_addrs, "Set the default IPMB slave address for"
		 " the controller.  Normally this is 0x20, but can be"
		 " overridden by this parm.  This is an array indexed"
		 " by interface number.");

static struct platform_device *ipmi_hc_pdevs[SI_MAX_PARMS];

static void __init ipmi_hardcode_init_one(const char *si_type_str,
					  unsigned int i,
					  unsigned long addr,
					  unsigned int flags)
{
	struct platform_device *pdev;
	unsigned int num_r = 1, size;
	struct resource r[4];
	struct property_entry p[6];
	enum si_type si_type;
	unsigned int regspacing, regsize;
	int rv;

	memset(p, 0, sizeof(p));
	memset(r, 0, sizeof(r));

	if (!si_type_str || !*si_type_str || strcmp(si_type_str, "kcs") == 0) {
		size = 2;
		si_type = SI_KCS;
	} else if (strcmp(si_type_str, "smic") == 0) {
		size = 2;
		si_type = SI_SMIC;
	} else if (strcmp(si_type_str, "bt") == 0) {
		size = 3;
		si_type = SI_BT;
	} else if (strcmp(si_type_str, "invalid") == 0) {
		/*
		 * Allow a firmware-specified interface to be
		 * disabled.
		 */
		size = 1;
		si_type = SI_TYPE_INVALID;
	} else {
		pr_warn("Interface type specified for interface %d, was invalid: %s\n",
			i, si_type_str);
		return;
	}

	regsize = regsizes[i];
	if (regsize == 0)
		regsize = DEFAULT_REGSIZE;

	p[0] = PROPERTY_ENTRY_U8("ipmi-type", si_type);
	p[1] = PROPERTY_ENTRY_U8("slave-addr", slave_addrs[i]);
	p[2] = PROPERTY_ENTRY_U8("addr-source", SI_HARDCODED);
	p[3] = PROPERTY_ENTRY_U8("reg-shift", regshifts[i]);
	p[4] = PROPERTY_ENTRY_U8("reg-size", regsize);
	/* Last entry must be left NULL to terminate it. */

	/*
	 * Register spacing is derived from the resources in
	 * the IPMI platform code.
	 */
	regspacing = regspacings[i];
	if (regspacing == 0)
		regspacing = regsize;

	r[0].start = addr;
	r[0].end = r[0].start + regsize - 1;
	r[0].name = "IPMI Address 1";
	r[0].flags = flags;

	if (size > 1) {
		r[1].start = r[0].start + regspacing;
		r[1].end = r[1].start + regsize - 1;
		r[1].name = "IPMI Address 2";
		r[1].flags = flags;
		num_r++;
	}

	if (size > 2) {
		r[2].start = r[1].start + regspacing;
		r[2].end = r[2].start + regsize - 1;
		r[2].name = "IPMI Address 3";
		r[2].flags = flags;
		num_r++;
	}

	if (irqs[i]) {
		r[num_r].start = irqs[i];
		r[num_r].end = irqs[i];
		r[num_r].name = "IPMI IRQ";
		r[num_r].flags = IORESOURCE_IRQ;
		num_r++;
	}

	pdev = platform_device_alloc("hardcode-ipmi-si", i);
	if (!pdev) {
		pr_err("Error allocating IPMI platform device %d\n", i);
		return;
	}

	rv = platform_device_add_resources(pdev, r, num_r);
	if (rv) {
		dev_err(&pdev->dev,
			"Unable to add hard-code resources: %d\n", rv);
		goto err;
	}

	rv = platform_device_add_properties(pdev, p);
	if (rv) {
		dev_err(&pdev->dev,
			"Unable to add hard-code properties: %d\n", rv);
		goto err;
	}

	rv = platform_device_add(pdev);
	if (rv) {
		dev_err(&pdev->dev,
			"Unable to add hard-code device: %d\n", rv);
		goto err;
	}

	ipmi_hc_pdevs[i] = pdev;
	return;

err:
	platform_device_put(pdev);
}

void __init ipmi_hardcode_init(void)
{
	unsigned int i;
	char *str;
	char *si_type[SI_MAX_PARMS];

	memset(si_type, 0, sizeof(si_type));

	/* Parse out the si_type string into its components. */
	str = si_type_str;
	if (*str != '\0') {
		for (i = 0; (i < SI_MAX_PARMS) && (*str != '\0'); i++) {
			si_type[i] = str;
			str = strchr(str, ',');
			if (str) {
				*str = '\0';
				str++;
			} else {
				break;
			}
		}
	}

	for (i = 0; i < SI_MAX_PARMS; i++) {
		if (i < num_ports && ports[i])
			ipmi_hardcode_init_one(si_type[i], i, ports[i],
					       IORESOURCE_IO);
		if (i < num_addrs && addrs[i])
			ipmi_hardcode_init_one(si_type[i], i, addrs[i],
					       IORESOURCE_MEM);
	}
}

void ipmi_si_hardcode_exit(void)
{
	unsigned int i;

	for (i = 0; i < SI_MAX_PARMS; i++) {
		if (ipmi_hc_pdevs[i])
			platform_device_unregister(ipmi_hc_pdevs[i]);
	}
}

/*
 * Returns true of the given address exists as a hardcoded address,
 * false if not.
 */
int ipmi_si_hardcode_match(int addr_type, unsigned long addr)
{
	unsigned int i;

	if (addr_type == IPMI_IO_ADDR_SPACE) {
		for (i = 0; i < num_ports; i++) {
			if (ports[i] == addr)
				return 1;
		}
	} else {
		for (i = 0; i < num_addrs; i++) {
			if (addrs[i] == addr)
				return 1;
		}
	}

	return 0;
}
