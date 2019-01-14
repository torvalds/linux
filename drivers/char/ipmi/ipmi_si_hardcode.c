// SPDX-License-Identifier: GPL-2.0+

#define pr_fmt(fmt) "ipmi_hardcode: " fmt

#include <linux/moduleparam.h>
#include "ipmi_si.h"

/*
 * There can be 4 IO ports passed in (with or without IRQs), 4 addresses,
 * a default IO port, and 1 ACPI/SPMI address.  That sets SI_MAX_DRIVERS.
 */

#define SI_MAX_PARMS 4

static char          *si_type[SI_MAX_PARMS];
#define MAX_SI_TYPE_STR 30
static char          si_type_str[MAX_SI_TYPE_STR];
static unsigned long addrs[SI_MAX_PARMS];
static unsigned int num_addrs;
static unsigned int  ports[SI_MAX_PARMS];
static unsigned int num_ports;
static int           irqs[SI_MAX_PARMS];
static unsigned int num_irqs;
static int           regspacings[SI_MAX_PARMS];
static unsigned int num_regspacings;
static int           regsizes[SI_MAX_PARMS];
static unsigned int num_regsizes;
static int           regshifts[SI_MAX_PARMS];
static unsigned int num_regshifts;
static int slave_addrs[SI_MAX_PARMS]; /* Leaving 0 chooses the default value */
static unsigned int num_slave_addrs;

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

int ipmi_si_hardcode_find_bmc(void)
{
	int ret = -ENODEV;
	int             i;
	struct si_sm_io io;
	char *str;

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

	memset(&io, 0, sizeof(io));
	for (i = 0; i < SI_MAX_PARMS; i++) {
		if (!ports[i] && !addrs[i])
			continue;

		io.addr_source = SI_HARDCODED;
		pr_info("probing via hardcoded address\n");

		if (!si_type[i] || strcmp(si_type[i], "kcs") == 0) {
			io.si_type = SI_KCS;
		} else if (strcmp(si_type[i], "smic") == 0) {
			io.si_type = SI_SMIC;
		} else if (strcmp(si_type[i], "bt") == 0) {
			io.si_type = SI_BT;
		} else {
			pr_warn("Interface type specified for interface %d, was invalid: %s\n",
				i, si_type[i]);
			continue;
		}

		if (ports[i]) {
			/* An I/O port */
			io.addr_data = ports[i];
			io.addr_type = IPMI_IO_ADDR_SPACE;
		} else if (addrs[i]) {
			/* A memory port */
			io.addr_data = addrs[i];
			io.addr_type = IPMI_MEM_ADDR_SPACE;
		} else {
			pr_warn("Interface type specified for interface %d, but port and address were not set or set to zero\n",
				i);
			continue;
		}

		io.addr = NULL;
		io.regspacing = regspacings[i];
		if (!io.regspacing)
			io.regspacing = DEFAULT_REGSPACING;
		io.regsize = regsizes[i];
		if (!io.regsize)
			io.regsize = DEFAULT_REGSIZE;
		io.regshift = regshifts[i];
		io.irq = irqs[i];
		if (io.irq)
			io.irq_setup = ipmi_std_irq_setup;
		io.slave_addr = slave_addrs[i];

		ret = ipmi_si_add_smi(&io);
	}
	return ret;
}
