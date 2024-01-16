// SPDX-License-Identifier: GPL-2.0
/*
 * SDK7786 FPGA PCIe mux handling
 *
 * Copyright (C) 2010  Paul Mundt
 */
#define pr_fmt(fmt) "PCI: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <mach/fpga.h>

/*
 * The SDK7786 FPGA supports mangling of most of the slots in some way or
 * another. Slots 3/4 are special in that only one can be supported at a
 * time, and both appear on port 3 to the PCI bus scan. Enabling slot 4
 * (the horizontal edge connector) will disable slot 3 entirely.
 *
 * Misconfigurations can be detected through the FPGA via the slot
 * resistors to determine card presence. Hotplug remains unsupported.
 */
static unsigned int slot4en __initdata;

char *__init pcibios_setup(char *str)
{
	if (strcmp(str, "slot4en") == 0) {
		slot4en = 1;
		return NULL;
	}

	return str;
}

static int __init sdk7786_pci_init(void)
{
	u16 data = fpga_read_reg(PCIECR);

	/*
	 * Enable slot #4 if it's been specified on the command line.
	 *
	 * Optionally reroute if slot #4 has a card present while slot #3
	 * does not, regardless of command line value.
	 *
	 * Card presence is logically inverted.
	 */
	slot4en ?: (!(data & PCIECR_PRST4) && (data & PCIECR_PRST3));
	if (slot4en) {
		pr_info("Activating PCIe slot#4 (disabling slot#3)\n");

		data &= ~PCIECR_PCIEMUX1;
		fpga_write_reg(data, PCIECR);

		/* Warn about forced rerouting if slot#3 is occupied */
		if ((data & PCIECR_PRST3) == 0) {
			pr_warn("Unreachable card detected in slot#3\n");
			return -EBUSY;
		}
	} else
		pr_info("PCIe slot#4 disabled\n");

	return 0;
}
postcore_initcall(sdk7786_pci_init);
