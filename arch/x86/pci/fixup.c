/*
 * Exceptions for specific devices. Usually work-arounds for fatal design flaws.
 */

#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/init.h>
#include "pci.h"


static void __devinit pci_fixup_i450nx(struct pci_dev *d)
{
	/*
	 * i450NX -- Find and scan all secondary buses on all PXB's.
	 */
	int pxb, reg;
	u8 busno, suba, subb;

	dev_warn(&d->dev, "Searching for i450NX host bridges\n");
	reg = 0xd0;
	for(pxb = 0; pxb < 2; pxb++) {
		pci_read_config_byte(d, reg++, &busno);
		pci_read_config_byte(d, reg++, &suba);
		pci_read_config_byte(d, reg++, &subb);
		DBG("i450NX PXB %d: %02x/%02x/%02x\n", pxb, busno, suba, subb);
		if (busno)
			pci_scan_bus_with_sysdata(busno);	/* Bus A */
		if (suba < subb)
			pci_scan_bus_with_sysdata(suba+1);	/* Bus B */
	}
	pcibios_last_bus = -1;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82451NX, pci_fixup_i450nx);

static void __devinit pci_fixup_i450gx(struct pci_dev *d)
{
	/*
	 * i450GX and i450KX -- Find and scan all secondary buses.
	 * (called separately for each PCI bridge found)
	 */
	u8 busno;
	pci_read_config_byte(d, 0x4a, &busno);
	dev_info(&d->dev, "i440KX/GX host bridge; secondary bus %02x\n", busno);
	pci_scan_bus_with_sysdata(busno);
	pcibios_last_bus = -1;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82454GX, pci_fixup_i450gx);

static void __devinit  pci_fixup_umc_ide(struct pci_dev *d)
{
	/*
	 * UM8886BF IDE controller sets region type bits incorrectly,
	 * therefore they look like memory despite of them being I/O.
	 */
	int i;

	dev_warn(&d->dev, "Fixing base address flags\n");
	for(i = 0; i < 4; i++)
		d->resource[i].flags |= PCI_BASE_ADDRESS_SPACE_IO;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_UMC, PCI_DEVICE_ID_UMC_UM8886BF, pci_fixup_umc_ide);

static void __devinit  pci_fixup_ncr53c810(struct pci_dev *d)
{
	/*
	 * NCR 53C810 returns class code 0 (at least on some systems).
	 * Fix class to be PCI_CLASS_STORAGE_SCSI
	 */
	if (!d->class) {
		dev_warn(&d->dev, "Fixing NCR 53C810 class code\n");
		d->class = PCI_CLASS_STORAGE_SCSI << 8;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NCR, PCI_DEVICE_ID_NCR_53C810, pci_fixup_ncr53c810);

static void __devinit  pci_fixup_latency(struct pci_dev *d)
{
	/*
	 *  SiS 5597 and 5598 chipsets require latency timer set to
	 *  at most 32 to avoid lockups.
	 */
	dev_dbg(&d->dev, "Setting max latency to 32\n");
	pcibios_max_latency = 32;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_5597, pci_fixup_latency);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_5598, pci_fixup_latency);

static void __devinit pci_fixup_piix4_acpi(struct pci_dev *d)
{
	/*
	 * PIIX4 ACPI device: hardwired IRQ9
	 */
	d->irq = 9;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3, pci_fixup_piix4_acpi);

/*
 * Addresses issues with problems in the memory write queue timer in
 * certain VIA Northbridges.  This bugfix is per VIA's specifications,
 * except for the KL133/KM133: clearing bit 5 on those Northbridges seems
 * to trigger a bug in its integrated ProSavage video card, which
 * causes screen corruption.  We only clear bits 6 and 7 for that chipset,
 * until VIA can provide us with definitive information on why screen
 * corruption occurs, and what exactly those bits do.
 *
 * VIA 8363,8622,8361 Northbridges:
 *  - bits  5, 6, 7 at offset 0x55 need to be turned off
 * VIA 8367 (KT266x) Northbridges:
 *  - bits  5, 6, 7 at offset 0x95 need to be turned off
 * VIA 8363 rev 0x81/0x84 (KL133/KM133) Northbridges:
 *  - bits     6, 7 at offset 0x55 need to be turned off
 */

#define VIA_8363_KL133_REVISION_ID 0x81
#define VIA_8363_KM133_REVISION_ID 0x84

static void pci_fixup_via_northbridge_bug(struct pci_dev *d)
{
	u8 v;
	int where = 0x55;
	int mask = 0x1f; /* clear bits 5, 6, 7 by default */

	if (d->device == PCI_DEVICE_ID_VIA_8367_0) {
		/* fix pci bus latency issues resulted by NB bios error
		   it appears on bug free^Wreduced kt266x's bios forces
		   NB latency to zero */
		pci_write_config_byte(d, PCI_LATENCY_TIMER, 0);

		where = 0x95; /* the memory write queue timer register is
				different for the KT266x's: 0x95 not 0x55 */
	} else if (d->device == PCI_DEVICE_ID_VIA_8363_0 &&
			(d->revision == VIA_8363_KL133_REVISION_ID ||
			d->revision == VIA_8363_KM133_REVISION_ID)) {
			mask = 0x3f; /* clear only bits 6 and 7; clearing bit 5
					causes screen corruption on the KL133/KM133 */
	}

	pci_read_config_byte(d, where, &v);
	if (v & ~mask) {
		dev_warn(&d->dev, "Disabling VIA memory write queue (PCI ID %04x, rev %02x): [%02x] %02x & %02x -> %02x\n", \
			d->device, d->revision, where, v, mask, v & mask);
		v &= mask;
		pci_write_config_byte(d, where, v);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8363_0, pci_fixup_via_northbridge_bug);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8622, pci_fixup_via_northbridge_bug);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8361, pci_fixup_via_northbridge_bug);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8367_0, pci_fixup_via_northbridge_bug);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8363_0, pci_fixup_via_northbridge_bug);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8622, pci_fixup_via_northbridge_bug);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8361, pci_fixup_via_northbridge_bug);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8367_0, pci_fixup_via_northbridge_bug);

/*
 * For some reasons Intel decided that certain parts of their
 * 815, 845 and some other chipsets must look like PCI-to-PCI bridges
 * while they are obviously not. The 82801 family (AA, AB, BAM/CAM,
 * BA/CA/DB and E) PCI bridges are actually HUB-to-PCI ones, according
 * to Intel terminology. These devices do forward all addresses from
 * system to PCI bus no matter what are their window settings, so they are
 * "transparent" (or subtractive decoding) from programmers point of view.
 */
static void __devinit pci_fixup_transparent_bridge(struct pci_dev *dev)
{
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI &&
	    (dev->device & 0xff00) == 0x2400)
		dev->transparent = 1;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_ANY_ID, pci_fixup_transparent_bridge);

/*
 * Fixup for C1 Halt Disconnect problem on nForce2 systems.
 *
 * From information provided by "Allen Martin" <AMartin@nvidia.com>:
 *
 * A hang is caused when the CPU generates a very fast CONNECT/HALT cycle
 * sequence.  Workaround is to set the SYSTEM_IDLE_TIMEOUT to 80 ns.
 * This allows the state-machine and timer to return to a proper state within
 * 80 ns of the CONNECT and probe appearing together.  Since the CPU will not
 * issue another HALT within 80 ns of the initial HALT, the failure condition
 * is avoided.
 */
static void pci_fixup_nforce2(struct pci_dev *dev)
{
	u32 val;

	/*
	 * Chip  Old value   New value
	 * C17   0x1F0FFF01  0x1F01FF01
	 * C18D  0x9F0FFF01  0x9F01FF01
	 *
	 * Northbridge chip version may be determined by
	 * reading the PCI revision ID (0xC1 or greater is C18D).
	 */
	pci_read_config_dword(dev, 0x6c, &val);

	/*
	 * Apply fixup if needed, but don't touch disconnect state
	 */
	if ((val & 0x00FF0000) != 0x00010000) {
		dev_warn(&dev->dev, "nForce2 C1 Halt Disconnect fixup\n");
		pci_write_config_dword(dev, 0x6c, (val & 0xFF00FFFF) | 0x00010000);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2, pci_fixup_nforce2);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2, pci_fixup_nforce2);

/* Max PCI Express root ports */
#define MAX_PCIEROOT	6
static int quirk_aspm_offset[MAX_PCIEROOT << 3];

#define GET_INDEX(a, b) ((((a) - PCI_DEVICE_ID_INTEL_MCH_PA) << 3) + ((b) & 7))

static int quirk_pcie_aspm_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	return raw_pci_ops->read(0, bus->number, devfn, where, size, value);
}

/*
 * Replace the original pci bus ops for write with a new one that will filter
 * the request to insure ASPM cannot be enabled.
 */
static int quirk_pcie_aspm_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	u8 offset;

	offset = quirk_aspm_offset[GET_INDEX(bus->self->device, devfn)];

	if ((offset) && (where == offset))
		value = value & 0xfffffffc;

	return raw_pci_ops->write(0, bus->number, devfn, where, size, value);
}

static struct pci_ops quirk_pcie_aspm_ops = {
	.read = quirk_pcie_aspm_read,
	.write = quirk_pcie_aspm_write,
};

/*
 * Prevents PCI Express ASPM (Active State Power Management) being enabled.
 *
 * Save the register offset, where the ASPM control bits are located,
 * for each PCI Express device that is in the device list of
 * the root port in an array for fast indexing. Replace the bus ops
 * with the modified one.
 */
static void pcie_rootport_aspm_quirk(struct pci_dev *pdev)
{
	int cap_base, i;
	struct pci_bus  *pbus;
	struct pci_dev *dev;

	if ((pbus = pdev->subordinate) == NULL)
		return;

	/*
	 * Check if the DID of pdev matches one of the six root ports. This
	 * check is needed in the case this function is called directly by the
	 * hot-plug driver.
	 */
	if ((pdev->device < PCI_DEVICE_ID_INTEL_MCH_PA) ||
	    (pdev->device > PCI_DEVICE_ID_INTEL_MCH_PC1))
		return;

	if (list_empty(&pbus->devices)) {
		/*
		 * If no device is attached to the root port at power-up or
		 * after hot-remove, the pbus->devices is empty and this code
		 * will set the offsets to zero and the bus ops to parent's bus
		 * ops, which is unmodified.
		 */
		for (i = GET_INDEX(pdev->device, 0); i <= GET_INDEX(pdev->device, 7); ++i)
			quirk_aspm_offset[i] = 0;

		pbus->ops = pbus->parent->ops;
	} else {
		/*
		 * If devices are attached to the root port at power-up or
		 * after hot-add, the code loops through the device list of
		 * each root port to save the register offsets and replace the
		 * bus ops.
		 */
		list_for_each_entry(dev, &pbus->devices, bus_list) {
			/* There are 0 to 8 devices attached to this bus */
			cap_base = pci_find_capability(dev, PCI_CAP_ID_EXP);
			quirk_aspm_offset[GET_INDEX(pdev->device, dev->devfn)] = cap_base + 0x10;
		}
		pbus->ops = &quirk_pcie_aspm_ops;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_MCH_PA,	pcie_rootport_aspm_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_MCH_PA1,	pcie_rootport_aspm_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_MCH_PB,	pcie_rootport_aspm_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_MCH_PB1,	pcie_rootport_aspm_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_MCH_PC,	pcie_rootport_aspm_quirk);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_MCH_PC1,	pcie_rootport_aspm_quirk);

/*
 * Fixup to mark boot BIOS video selected by BIOS before it changes
 *
 * From information provided by "Jon Smirl" <jonsmirl@gmail.com>
 *
 * The standard boot ROM sequence for an x86 machine uses the BIOS
 * to select an initial video card for boot display. This boot video
 * card will have it's BIOS copied to C0000 in system RAM.
 * IORESOURCE_ROM_SHADOW is used to associate the boot video
 * card with this copy. On laptops this copy has to be used since
 * the main ROM may be compressed or combined with another image.
 * See pci_map_rom() for use of this flag. IORESOURCE_ROM_SHADOW
 * is marked here since the boot video device will be the only enabled
 * video device at this point.
 */

static void __devinit pci_fixup_video(struct pci_dev *pdev)
{
	struct pci_dev *bridge;
	struct pci_bus *bus;
	u16 config;

	if ((pdev->class >> 8) != PCI_CLASS_DISPLAY_VGA)
		return;

	/* Is VGA routed to us? */
	bus = pdev->bus;
	while (bus) {
		bridge = bus->self;

		/*
		 * From information provided by
		 * "David Miller" <davem@davemloft.net>
		 * The bridge control register is valid for PCI header
		 * type BRIDGE, or CARDBUS. Host to PCI controllers use
		 * PCI header type NORMAL.
		 */
		if (bridge
		    && ((bridge->hdr_type == PCI_HEADER_TYPE_BRIDGE)
		       || (bridge->hdr_type == PCI_HEADER_TYPE_CARDBUS))) {
			pci_read_config_word(bridge, PCI_BRIDGE_CONTROL,
						&config);
			if (!(config & PCI_BRIDGE_CTL_VGA))
				return;
		}
		bus = bus->parent;
	}
	pci_read_config_word(pdev, PCI_COMMAND, &config);
	if (config & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) {
		pdev->resource[PCI_ROM_RESOURCE].flags |= IORESOURCE_ROM_SHADOW;
		dev_printk(KERN_DEBUG, &pdev->dev, "Boot video device\n");
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID, pci_fixup_video);


static struct dmi_system_id __devinitdata msi_k8t_dmi_table[] = {
	{
		.ident = "MSI-K8T-Neo2Fir",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MSI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "MS-6702E"),
		},
	},
	{}
};

/*
 * The AMD-Athlon64 board MSI "K8T Neo2-FIR" disables the onboard sound
 * card if a PCI-soundcard is added.
 *
 * The BIOS only gives options "DISABLED" and "AUTO". This code sets
 * the corresponding register-value to enable the soundcard.
 *
 * The soundcard is only enabled, if the mainborad is identified
 * via DMI-tables and the soundcard is detected to be off.
 */
static void __devinit pci_fixup_msi_k8t_onboard_sound(struct pci_dev *dev)
{
	unsigned char val;
	if (!dmi_check_system(msi_k8t_dmi_table))
		return; /* only applies to MSI K8T Neo2-FIR */

	pci_read_config_byte(dev, 0x50, &val);
	if (val & 0x40) {
		pci_write_config_byte(dev, 0x50, val & (~0x40));

		/* verify the change for status output */
		pci_read_config_byte(dev, 0x50, &val);
		if (val & 0x40)
			dev_info(&dev->dev, "Detected MSI K8T Neo2-FIR; "
					"can't enable onboard soundcard!\n");
		else
			dev_info(&dev->dev, "Detected MSI K8T Neo2-FIR; "
					"enabled onboard soundcard\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8237,
		pci_fixup_msi_k8t_onboard_sound);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8237,
		pci_fixup_msi_k8t_onboard_sound);

/*
 * Some Toshiba laptops need extra code to enable their TI TSB43AB22/A.
 *
 * We pretend to bring them out of full D3 state, and restore the proper
 * IRQ, PCI cache line size, and BARs, otherwise the device won't function
 * properly.  In some cases, the device will generate an interrupt on
 * the wrong IRQ line, causing any devices sharing the line it's
 * *supposed* to use to be disabled by the kernel's IRQ debug code.
 */
static u16 toshiba_line_size;

static struct dmi_system_id __devinitdata toshiba_ohci1394_dmi_table[] = {
	{
		.ident = "Toshiba PS5 based laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "PS5"),
		},
	},
	{
		.ident = "Toshiba PSM4 based laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "PSM4"),
		},
	},
	{
		.ident = "Toshiba A40 based laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "PSA40U"),
		},
	},
	{ }
};

static void __devinit pci_pre_fixup_toshiba_ohci1394(struct pci_dev *dev)
{
	if (!dmi_check_system(toshiba_ohci1394_dmi_table))
		return; /* only applies to certain Toshibas (so far) */

	dev->current_state = PCI_D3cold;
	pci_read_config_word(dev, PCI_CACHE_LINE_SIZE, &toshiba_line_size);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TI, 0x8032,
			 pci_pre_fixup_toshiba_ohci1394);

static void __devinit pci_post_fixup_toshiba_ohci1394(struct pci_dev *dev)
{
	if (!dmi_check_system(toshiba_ohci1394_dmi_table))
		return; /* only applies to certain Toshibas (so far) */

	/* Restore config space on Toshiba laptops */
	pci_write_config_word(dev, PCI_CACHE_LINE_SIZE, toshiba_line_size);
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, (u8 *)&dev->irq);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0,
			       pci_resource_start(dev, 0));
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_1,
			       pci_resource_start(dev, 1));
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_TI, 0x8032,
			 pci_post_fixup_toshiba_ohci1394);


/*
 * Prevent the BIOS trapping accesses to the Cyrix CS5530A video device
 * configuration space.
 */
static void pci_early_fixup_cyrix_5530(struct pci_dev *dev)
{
	u8 r;
	/* clear 'F4 Video Configuration Trap' bit */
	pci_read_config_byte(dev, 0x42, &r);
	r &= 0xfd;
	pci_write_config_byte(dev, 0x42, r);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5530_LEGACY,
			pci_early_fixup_cyrix_5530);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5530_LEGACY,
			pci_early_fixup_cyrix_5530);

/*
 * Siemens Nixdorf AG FSC Multiprocessor Interrupt Controller:
 * prevent update of the BAR0, which doesn't look like a normal BAR.
 */
static void __devinit pci_siemens_interrupt_controller(struct pci_dev *dev)
{
	dev->resource[0].flags |= IORESOURCE_PCI_FIXED;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SIEMENS, 0x0015,
			  pci_siemens_interrupt_controller);
