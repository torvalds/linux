// SPDX-License-Identifier: GPL-2.0

#include <linux/pci.h>
#include <linux/acpi.h>
#include <acpi/reboot.h>
#include <linux/delay.h>

#ifdef CONFIG_PCI
static void acpi_pci_reboot(struct acpi_generic_address *rr, u8 reset_value)
{
	unsigned int devfn;
	struct pci_bus *bus0;

	/* The reset register can only live on bus 0. */
	bus0 = pci_find_bus(0, 0);
	if (!bus0)
		return;
	/* Form PCI device/function pair. */
	devfn = PCI_DEVFN((rr->address >> 32) & 0xffff,
			  (rr->address >> 16) & 0xffff);
	pr_debug("Resetting with ACPI PCI RESET_REG.\n");
	/* Write the value that resets us. */
	pci_bus_write_config_byte(bus0, devfn,
			(rr->address & 0xffff), reset_value);
}
#else
static inline void acpi_pci_reboot(struct acpi_generic_address *rr,
				   u8 reset_value)
{
	pr_warn_once("PCI configuration space access is not supported\n");
}
#endif

void acpi_reboot(void)
{
	struct acpi_generic_address *rr;
	u8 reset_value;

	if (acpi_disabled)
		return;

	rr = &acpi_gbl_FADT.reset_register;

	/* ACPI reset register was only introduced with v2 of the FADT */

	if (acpi_gbl_FADT.header.revision < 2)
		return;

	/* Is the reset register supported? The spec says we should be
	 * checking the bit width and bit offset, but Windows ignores
	 * these fields */
	if (!(acpi_gbl_FADT.flags & ACPI_FADT_RESET_REGISTER))
		return;

	reset_value = acpi_gbl_FADT.reset_value;

	/* The reset register can only exist in I/O, Memory or PCI config space
	 * on a device on bus 0. */
	switch (rr->space_id) {
	case ACPI_ADR_SPACE_PCI_CONFIG:
		acpi_pci_reboot(rr, reset_value);
		break;

	case ACPI_ADR_SPACE_SYSTEM_MEMORY:
	case ACPI_ADR_SPACE_SYSTEM_IO:
		printk(KERN_DEBUG "ACPI MEMORY or I/O RESET_REG.\n");
		acpi_reset();
		break;
	}

	/*
	 * Some platforms do not shut down immediately after writing to the
	 * ACPI reset register, and this results in racing with the
	 * subsequent reboot mechanism.
	 *
	 * The 15ms delay has been found to be long enough for the system
	 * to reboot on the affected platforms.
	 */
	mdelay(15);
}
