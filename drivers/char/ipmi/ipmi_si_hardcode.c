// SPDX-License-Identifier: GPL-2.0+

#define pr_fmt(fmt) "ipmi_hardcode: " fmt

#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include "ipmi_si.h"
#include "ipmi_plat_data.h"

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
MODULE_PARM_DESC(type,
		 "Defines the type of each interface, each interface separated by commas.  The types are 'kcs', 'smic', and 'bt'.  For example si_type=kcs,bt will set the first interface to kcs and the second to bt");
module_param_hw_array(addrs, ulong, iomem, &num_addrs, 0);
MODULE_PARM_DESC(addrs,
		 "Sets the memory address of each interface, the addresses separated by commas.  Only use if an interface is in memory.  Otherwise, set it to zero or leave it blank.");
module_param_hw_array(ports, uint, ioport, &num_ports, 0);
MODULE_PARM_DESC(ports,
		 "Sets the port address of each interface, the addresses separated by commas.  Only use if an interface is a port.  Otherwise, set it to zero or leave it blank.");
module_param_hw_array(irqs, int, irq, &num_irqs, 0);
MODULE_PARM_DESC(irqs,
		 "Sets the interrupt of each interface, the addresses separated by commas.  Only use if an interface has an interrupt.  Otherwise, set it to zero or leave it blank.");
module_param_hw_array(regspacings, int, other, &num_regspacings, 0);
MODULE_PARM_DESC(regspacings,
		 "The number of bytes between the start address and each successive register used by the interface.  For instance, if the start address is 0xca2 and the spacing is 2, then the second address is at 0xca4.  Defaults to 1.");
module_param_hw_array(regsizes, int, other, &num_regsizes, 0);
MODULE_PARM_DESC(regsizes,
		 "The size of the specific IPMI register in bytes. This should generally be 1, 2, 4, or 8 for an 8-bit, 16-bit, 32-bit, or 64-bit register.  Use this if you the 8-bit IPMI register has to be read from a larger register.");
module_param_hw_array(regshifts, int, other, &num_regshifts, 0);
MODULE_PARM_DESC(regshifts,
		 "The amount to shift the data read from the. IPMI register, in bits.  For instance, if the data is read from a 32-bit word and the IPMI data is in bit 8-15, then the shift would be 8");
module_param_hw_array(slave_addrs, int, other, &num_slave_addrs, 0);
MODULE_PARM_DESC(slave_addrs,
		 "Set the default IPMB slave address for the controller.  Normally this is 0x20, but can be overridden by this parm.  This is an array indexed by interface number.");

static void __init ipmi_hardcode_init_one(const char *si_type_str,
					  unsigned int i,
					  unsigned long addr,
					  enum ipmi_addr_space addr_space)
{
	struct ipmi_plat_data p;
	int t;

	memset(&p, 0, sizeof(p));

	p.iftype = IPMI_PLAT_IF_SI;
	if (!si_type_str || !*si_type_str) {
		p.type = SI_KCS;
	} else {
		t = match_string(si_to_str, -1, si_type_str);
		if (t < 0) {
			pr_warn("Interface type specified for interface %d, was invalid: %s\n",
				i, si_type_str);
			return;
		}
		p.type = t;
	}

	p.regsize = regsizes[i];
	p.slave_addr = slave_addrs[i];
	p.addr_source = SI_HARDCODED;
	p.regshift = regshifts[i];
	p.regsize = regsizes[i];
	p.addr = addr;
	p.space = addr_space;

	ipmi_platform_add("hardcode-ipmi-si", i, &p);
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
					       IPMI_IO_ADDR_SPACE);
		if (i < num_addrs && addrs[i])
			ipmi_hardcode_init_one(si_type[i], i, addrs[i],
					       IPMI_MEM_ADDR_SPACE);
	}
}


void ipmi_si_hardcode_exit(void)
{
	ipmi_remove_platform_device_by_name("hardcode-ipmi-si");
}

/*
 * Returns true of the given address exists as a hardcoded address,
 * false if not.
 */
int ipmi_si_hardcode_match(int addr_space, unsigned long addr)
{
	unsigned int i;

	if (addr_space == IPMI_IO_ADDR_SPACE) {
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
