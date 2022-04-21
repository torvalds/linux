// SPDX-License-Identifier: GPL-2.0
/*
 * Exceptions for specific devices. Usually work-arounds for fatal design flaws.
 */

#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/vgaarb.h>
#include <asm/hpet.h>
#include <asm/pci_x86.h>

static void pci_fixup_i450nx(struct pci_dev *d)
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
		dev_dbg(&d->dev, "i450NX PXB %d: %02x/%02x/%02x\n", pxb, busno,
			suba, subb);
		if (busno)
			pcibios_scan_root(busno);	/* Bus A */
		if (suba < subb)
			pcibios_scan_root(suba+1);	/* Bus B */
	}
	pcibios_last_bus = -1;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82451NX, pci_fixup_i450nx);

static void pci_fixup_i450gx(struct pci_dev *d)
{
	/*
	 * i450GX and i450KX -- Find and scan all secondary buses.
	 * (called separately for each PCI bridge found)
	 */
	u8 busno;
	pci_read_config_byte(d, 0x4a, &busno);
	dev_info(&d->dev, "i440KX/GX host bridge; secondary bus %02x\n", busno);
	pcibios_scan_root(busno);
	pcibios_last_bus = -1;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82454GX, pci_fixup_i450gx);

static void pci_fixup_umc_ide(struct pci_dev *d)
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

static void pci_fixup_latency(struct pci_dev *d)
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

static void pci_fixup_piix4_acpi(struct pci_dev *d)
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
static void pci_fixup_transparent_bridge(struct pci_dev *dev)
{
	if ((dev->device & 0xff00) == 0x2400)
		dev->transparent = 1;
}
DECLARE_PCI_FIXUP_CLASS_HEADER(PCI_VENDOR_ID_INTEL, PCI_ANY_ID,
			 PCI_CLASS_BRIDGE_PCI, 8, pci_fixup_transparent_bridge);

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
	return raw_pci_read(pci_domain_nr(bus), bus->number,
						devfn, where, size, value);
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
		value = value & ~PCI_EXP_LNKCTL_ASPMC;

	return raw_pci_write(pci_domain_nr(bus), bus->number,
						devfn, where, size, value);
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
	int i;
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

		pci_bus_set_ops(pbus, pbus->parent->ops);
	} else {
		/*
		 * If devices are attached to the root port at power-up or
		 * after hot-add, the code loops through the device list of
		 * each root port to save the register offsets and replace the
		 * bus ops.
		 */
		list_for_each_entry(dev, &pbus->devices, bus_list)
			/* There are 0 to 8 devices attached to this bus */
			quirk_aspm_offset[GET_INDEX(pdev->device, dev->devfn)] =
				dev->pcie_cap + PCI_EXP_LNKCTL;

		pci_bus_set_ops(pbus, &quirk_pcie_aspm_ops);
		dev_info(&pbus->dev, "writes to ASPM control bits will be ignored\n");
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
 * card will have its BIOS copied to 0xC0000 in system RAM.
 * IORESOURCE_ROM_SHADOW is used to associate the boot video
 * card with this copy. On laptops this copy has to be used since
 * the main ROM may be compressed or combined with another image.
 * See pci_map_rom() for use of this flag. Before marking the device
 * with IORESOURCE_ROM_SHADOW check if a vga_default_device is already set
 * by either arch code or vga-arbitration; if so only apply the fixup to this
 * already-determined primary video card.
 */

static void pci_fixup_video(struct pci_dev *pdev)
{
	struct pci_dev *bridge;
	struct pci_bus *bus;
	u16 config;
	struct resource *res;

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
		if (bridge && (pci_is_bridge(bridge))) {
			pci_read_config_word(bridge, PCI_BRIDGE_CONTROL,
						&config);
			if (!(config & PCI_BRIDGE_CTL_VGA))
				return;
		}
		bus = bus->parent;
	}
	if (!vga_default_device() || pdev == vga_default_device()) {
		pci_read_config_word(pdev, PCI_COMMAND, &config);
		if (config & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) {
			res = &pdev->resource[PCI_ROM_RESOURCE];

			pci_disable_rom(pdev);
			if (res->parent)
				release_resource(res);

			res->start = 0xC0000;
			res->end = res->start + 0x20000 - 1;
			res->flags = IORESOURCE_MEM | IORESOURCE_ROM_SHADOW |
				     IORESOURCE_PCI_FIXED;
			dev_info(&pdev->dev, "Video device with shadowed ROM at %pR\n",
				 res);
		}
	}
}
DECLARE_PCI_FIXUP_CLASS_HEADER(PCI_ANY_ID, PCI_ANY_ID,
			       PCI_CLASS_DISPLAY_VGA, 8, pci_fixup_video);


static const struct dmi_system_id msi_k8t_dmi_table[] = {
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
 * The soundcard is only enabled, if the mainboard is identified
 * via DMI-tables and the soundcard is detected to be off.
 */
static void pci_fixup_msi_k8t_onboard_sound(struct pci_dev *dev)
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

static const struct dmi_system_id toshiba_ohci1394_dmi_table[] = {
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

static void pci_pre_fixup_toshiba_ohci1394(struct pci_dev *dev)
{
	if (!dmi_check_system(toshiba_ohci1394_dmi_table))
		return; /* only applies to certain Toshibas (so far) */

	dev->current_state = PCI_D3cold;
	pci_read_config_word(dev, PCI_CACHE_LINE_SIZE, &toshiba_line_size);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TI, 0x8032,
			 pci_pre_fixup_toshiba_ohci1394);

static void pci_post_fixup_toshiba_ohci1394(struct pci_dev *dev)
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
static void pci_siemens_interrupt_controller(struct pci_dev *dev)
{
	dev->resource[0].flags |= IORESOURCE_PCI_FIXED;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SIEMENS, 0x0015,
			  pci_siemens_interrupt_controller);

/*
 * SB600: Disable BAR1 on device 14.0 to avoid HPET resources from
 * confusing the PCI engine:
 */
static void sb600_disable_hpet_bar(struct pci_dev *dev)
{
	u8 val;

	/*
	 * The SB600 and SB700 both share the same device
	 * ID, but the PM register 0x55 does something different
	 * for the SB700, so make sure we are dealing with the
	 * SB600 before touching the bit:
	 */

	pci_read_config_byte(dev, 0x08, &val);

	if (val < 0x2F) {
		outb(0x55, 0xCD6);
		val = inb(0xCD7);

		/* Set bit 7 in PM register 0x55 */
		outb(0x55, 0xCD6);
		outb(val | 0x80, 0xCD7);
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_ATI, 0x4385, sb600_disable_hpet_bar);

#ifdef CONFIG_HPET_TIMER
static void sb600_hpet_quirk(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[1];

	if (r->flags & IORESOURCE_MEM && r->start == hpet_address) {
		r->flags |= IORESOURCE_PCI_FIXED;
		dev_info(&dev->dev, "reg 0x14 contains HPET; making it immovable\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATI, 0x4385, sb600_hpet_quirk);
#endif

/*
 * Twinhead H12Y needs us to block out a region otherwise we map devices
 * there and any access kills the box.
 *
 *   See: https://bugzilla.kernel.org/show_bug.cgi?id=10231
 *
 * Match off the LPC and svid/sdid (older kernels lose the bridge subvendor)
 */
static void twinhead_reserve_killing_zone(struct pci_dev *dev)
{
        if (dev->subsystem_vendor == 0x14FF && dev->subsystem_device == 0xA003) {
                pr_info("Reserving memory on Twinhead H12Y\n");
                request_mem_region(0xFFB00000, 0x100000, "twinhead");
        }
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x27B9, twinhead_reserve_killing_zone);

/*
 * Device [8086:2fc0]
 * Erratum HSE43
 * CONFIG_TDP_NOMINAL CSR Implemented at Incorrect Offset
 * https://www.intel.com/content/www/us/en/processors/xeon/xeon-e5-v3-spec-update.html
 *
 * Devices [8086:6f60,6fa0,6fc0]
 * Erratum BDF2
 * PCI BARs in the Home Agent Will Return Non-Zero Values During Enumeration
 * https://www.intel.com/content/www/us/en/processors/xeon/xeon-e5-v4-spec-update.html
 */
static void pci_invalid_bar(struct pci_dev *dev)
{
	dev->non_compliant_bars = 1;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x2fc0, pci_invalid_bar);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x6f60, pci_invalid_bar);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x6fa0, pci_invalid_bar);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0x6fc0, pci_invalid_bar);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0xa1ec, pci_invalid_bar);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0xa1ed, pci_invalid_bar);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0xa26c, pci_invalid_bar);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, 0xa26d, pci_invalid_bar);

/*
 * Device [1022:7808]
 * 23. USB Wake on Connect/Disconnect with Low Speed Devices
 * https://support.amd.com/TechDocs/46837.pdf
 * Appendix A2
 * https://support.amd.com/TechDocs/42413.pdf
 */
static void pci_fixup_amd_ehci_pme(struct pci_dev *dev)
{
	dev_info(&dev->dev, "PME# does not work under D3, disabling it\n");
	dev->pme_support &= ~((PCI_PM_CAP_PME_D3hot | PCI_PM_CAP_PME_D3cold)
		>> PCI_PM_CAP_PME_SHIFT);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x7808, pci_fixup_amd_ehci_pme);

/*
 * Device [1022:7914]
 * When in D0, PME# doesn't get asserted when plugging USB 2.0 device.
 */
static void pci_fixup_amd_fch_xhci_pme(struct pci_dev *dev)
{
	dev_info(&dev->dev, "PME# does not work under D0, disabling it\n");
	dev->pme_support &= ~(PCI_PM_CAP_PME_D0 >> PCI_PM_CAP_PME_SHIFT);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x7914, pci_fixup_amd_fch_xhci_pme);

/*
 * Apple MacBook Pro: Avoid [mem 0x7fa00000-0x7fbfffff]
 *
 * Using the [mem 0x7fa00000-0x7fbfffff] region, e.g., by assigning it to
 * the 00:1c.0 Root Port, causes a conflict with [io 0x1804], which is used
 * for soft poweroff and suspend-to-RAM.
 *
 * As far as we know, this is related to the address space, not to the Root
 * Port itself.  Attaching the quirk to the Root Port is a convenience, but
 * it could probably also be a standalone DMI quirk.
 *
 * https://bugzilla.kernel.org/show_bug.cgi?id=103211
 */
static void quirk_apple_mbp_poweroff(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;

	if ((!dmi_match(DMI_PRODUCT_NAME, "MacBookPro11,4") &&
	     !dmi_match(DMI_PRODUCT_NAME, "MacBookPro11,5")) ||
	    pdev->bus->number != 0 || pdev->devfn != PCI_DEVFN(0x1c, 0))
		return;

	res = request_mem_region(0x7fa00000, 0x200000,
				 "MacBook Pro poweroff workaround");
	if (res)
		dev_info(dev, "claimed %s %pR\n", res->name, res);
	else
		dev_info(dev, "can't work around MacBook Pro poweroff issue\n");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x8c10, quirk_apple_mbp_poweroff);

/*
 * VMD-enabled root ports will change the source ID for all messages
 * to the VMD device. Rather than doing device matching with the source
 * ID, the AER driver should traverse the child device tree, reading
 * AER registers to find the faulting device.
 */
static void quirk_no_aersid(struct pci_dev *pdev)
{
	/* VMD Domain */
	if (is_vmd(pdev->bus) && pci_is_root_bus(pdev->bus))
		pdev->bus->bus_flags |= PCI_BUS_FLAGS_NO_AERSID;
}
DECLARE_PCI_FIXUP_CLASS_EARLY(PCI_VENDOR_ID_INTEL, PCI_ANY_ID,
			      PCI_CLASS_BRIDGE_PCI, 8, quirk_no_aersid);

static void quirk_intel_th_dnv(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[4];

	/*
	 * Denverton reports 2k of RTIT_BAR (intel_th resource 4), which
	 * appears to be 4 MB in reality.
	 */
	if (r->end == r->start + 0x7ff) {
		r->start = 0;
		r->end   = 0x3fffff;
		r->flags |= IORESOURCE_UNSET;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x19e1, quirk_intel_th_dnv);

#ifdef CONFIG_PHYS_ADDR_T_64BIT

#define AMD_141b_MMIO_BASE(x)	(0x80 + (x) * 0x8)
#define AMD_141b_MMIO_BASE_RE_MASK		BIT(0)
#define AMD_141b_MMIO_BASE_WE_MASK		BIT(1)
#define AMD_141b_MMIO_BASE_MMIOBASE_MASK	GENMASK(31,8)

#define AMD_141b_MMIO_LIMIT(x)	(0x84 + (x) * 0x8)
#define AMD_141b_MMIO_LIMIT_MMIOLIMIT_MASK	GENMASK(31,8)

#define AMD_141b_MMIO_HIGH(x)	(0x180 + (x) * 0x4)
#define AMD_141b_MMIO_HIGH_MMIOBASE_MASK	GENMASK(7,0)
#define AMD_141b_MMIO_HIGH_MMIOLIMIT_SHIFT	16
#define AMD_141b_MMIO_HIGH_MMIOLIMIT_MASK	GENMASK(23,16)

/*
 * The PCI Firmware Spec, rev 3.2, notes that ACPI should optionally allow
 * configuring host bridge windows using the _PRS and _SRS methods.
 *
 * But this is rarely implemented, so we manually enable a large 64bit BAR for
 * PCIe device on AMD Family 15h (Models 00h-1fh, 30h-3fh, 60h-7fh) Processors
 * here.
 */
static void pci_amd_enable_64bit_bar(struct pci_dev *dev)
{
	static const char *name = "PCI Bus 0000:00";
	struct resource *res, *conflict;
	u32 base, limit, high;
	struct pci_dev *other;
	unsigned i;

	if (!(pci_probe & PCI_BIG_ROOT_WINDOW))
		return;

	/* Check that we are the only device of that type */
	other = pci_get_device(dev->vendor, dev->device, NULL);
	if (other != dev ||
	    (other = pci_get_device(dev->vendor, dev->device, other))) {
		/* This is a multi-socket system, don't touch it for now */
		pci_dev_put(other);
		return;
	}

	for (i = 0; i < 8; i++) {
		pci_read_config_dword(dev, AMD_141b_MMIO_BASE(i), &base);
		pci_read_config_dword(dev, AMD_141b_MMIO_HIGH(i), &high);

		/* Is this slot free? */
		if (!(base & (AMD_141b_MMIO_BASE_RE_MASK |
			      AMD_141b_MMIO_BASE_WE_MASK)))
			break;

		base >>= 8;
		base |= high << 24;

		/* Abort if a slot already configures a 64bit BAR. */
		if (base > 0x10000)
			return;
	}
	if (i == 8)
		return;

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return;

	/*
	 * Allocate a 256GB window directly below the 0xfd00000000 hardware
	 * limit (see AMD Family 15h Models 30h-3Fh BKDG, sec 2.4.6).
	 */
	res->name = name;
	res->flags = IORESOURCE_PREFETCH | IORESOURCE_MEM |
		IORESOURCE_MEM_64 | IORESOURCE_WINDOW;
	res->start = 0xbd00000000ull;
	res->end = 0xfd00000000ull - 1;

	conflict = request_resource_conflict(&iomem_resource, res);
	if (conflict) {
		kfree(res);
		if (conflict->name != name)
			return;

		/* We are resuming from suspend; just reenable the window */
		res = conflict;
	} else {
		dev_info(&dev->dev, "adding root bus resource %pR (tainting kernel)\n",
			 res);
		add_taint(TAINT_FIRMWARE_WORKAROUND, LOCKDEP_STILL_OK);
		pci_bus_add_resource(dev->bus, res, 0);
	}

	base = ((res->start >> 8) & AMD_141b_MMIO_BASE_MMIOBASE_MASK) |
		AMD_141b_MMIO_BASE_RE_MASK | AMD_141b_MMIO_BASE_WE_MASK;
	limit = ((res->end + 1) >> 8) & AMD_141b_MMIO_LIMIT_MMIOLIMIT_MASK;
	high = ((res->start >> 40) & AMD_141b_MMIO_HIGH_MMIOBASE_MASK) |
		((((res->end + 1) >> 40) << AMD_141b_MMIO_HIGH_MMIOLIMIT_SHIFT)
		 & AMD_141b_MMIO_HIGH_MMIOLIMIT_MASK);

	pci_write_config_dword(dev, AMD_141b_MMIO_HIGH(i), high);
	pci_write_config_dword(dev, AMD_141b_MMIO_LIMIT(i), limit);
	pci_write_config_dword(dev, AMD_141b_MMIO_BASE(i), base);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x1401, pci_amd_enable_64bit_bar);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x141b, pci_amd_enable_64bit_bar);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x1571, pci_amd_enable_64bit_bar);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x15b1, pci_amd_enable_64bit_bar);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, 0x1601, pci_amd_enable_64bit_bar);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD, 0x1401, pci_amd_enable_64bit_bar);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD, 0x141b, pci_amd_enable_64bit_bar);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD, 0x1571, pci_amd_enable_64bit_bar);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD, 0x15b1, pci_amd_enable_64bit_bar);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD, 0x1601, pci_amd_enable_64bit_bar);

#define RS690_LOWER_TOP_OF_DRAM2	0x30
#define RS690_LOWER_TOP_OF_DRAM2_VALID	0x1
#define RS690_UPPER_TOP_OF_DRAM2	0x31
#define RS690_HTIU_NB_INDEX		0xA8
#define RS690_HTIU_NB_INDEX_WR_ENABLE	0x100
#define RS690_HTIU_NB_DATA		0xAC

/*
 * Some BIOS implementations support RAM above 4GB, but do not configure the
 * PCI host to respond to bus master accesses for these addresses. These
 * implementations set the TOP_OF_DRAM_SLOT1 register correctly, so PCI DMA
 * works as expected for addresses below 4GB.
 *
 * Reference: "AMD RS690 ASIC Family Register Reference Guide" (pg. 2-57)
 * https://www.amd.com/system/files/TechDocs/43372_rs690_rrg_3.00o.pdf
 */
static void rs690_fix_64bit_dma(struct pci_dev *pdev)
{
	u32 val = 0;
	phys_addr_t top_of_dram = __pa(high_memory - 1) + 1;

	if (top_of_dram <= (1ULL << 32))
		return;

	pci_write_config_dword(pdev, RS690_HTIU_NB_INDEX,
				RS690_LOWER_TOP_OF_DRAM2);
	pci_read_config_dword(pdev, RS690_HTIU_NB_DATA, &val);

	if (val)
		return;

	pci_info(pdev, "Adjusting top of DRAM to %pa for 64-bit DMA support\n", &top_of_dram);

	pci_write_config_dword(pdev, RS690_HTIU_NB_INDEX,
		RS690_UPPER_TOP_OF_DRAM2 | RS690_HTIU_NB_INDEX_WR_ENABLE);
	pci_write_config_dword(pdev, RS690_HTIU_NB_DATA, top_of_dram >> 32);

	pci_write_config_dword(pdev, RS690_HTIU_NB_INDEX,
		RS690_LOWER_TOP_OF_DRAM2 | RS690_HTIU_NB_INDEX_WR_ENABLE);
	pci_write_config_dword(pdev, RS690_HTIU_NB_DATA,
		top_of_dram | RS690_LOWER_TOP_OF_DRAM2_VALID);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x7910, rs690_fix_64bit_dma);

#endif
