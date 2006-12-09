/*
 * This file contains work-arounds for x86 and x86_64 platform bugs.
 */
#include <linux/pci.h>
#include <linux/irq.h>
#include <asm/pci-direct.h>
#include <asm/genapic.h>
#include <asm/cpu.h>

#if defined(CONFIG_X86_IO_APIC) && defined(CONFIG_SMP) && defined(CONFIG_PCI)
static void __devinit verify_quirk_intel_irqbalance(struct pci_dev *dev)
{
	u8 config, rev;
	u32 word;

	/* BIOS may enable hardware IRQ balancing for
	 * E7520/E7320/E7525(revision ID 0x9 and below)
	 * based platforms.
	 * For those platforms, make sure that the genapic is set to 'flat'
	 */
	pci_read_config_byte(dev, PCI_CLASS_REVISION, &rev);
	if (rev > 0x9)
		return;

	/* enable access to config space*/
	pci_read_config_byte(dev, 0xf4, &config);
	pci_write_config_byte(dev, 0xf4, config|0x2);

	/* read xTPR register */
	raw_pci_ops->read(0, 0, 0x40, 0x4c, 2, &word);

	if (!(word & (1 << 13))) {
#ifdef CONFIG_X86_64
		if (genapic !=  &apic_flat)
			panic("APIC mode must be flat on this system\n");
#elif defined(CONFIG_X86_GENERICARCH)
		if (genapic != &apic_default)
			panic("APIC mode must be default(flat) on this system. Use apic=default\n");
#endif
	}

	/* put back the original value for config space*/
	if (!(config & 0x2))
		pci_write_config_byte(dev, 0xf4, config);
}

void __init quirk_intel_irqbalance(void)
{
	u8 config, rev;
	u32 word;

	/* BIOS may enable hardware IRQ balancing for
	 * E7520/E7320/E7525(revision ID 0x9 and below)
	 * based platforms.
	 * Disable SW irqbalance/affinity on those platforms.
	 */
	rev = read_pci_config_byte(0, 0, 0, PCI_CLASS_REVISION);
	if (rev > 0x9)
		return;

	printk(KERN_INFO "Intel E7520/7320/7525 detected.");

	/* enable access to config space */
	config = read_pci_config_byte(0, 0, 0, 0xf4);
	write_pci_config_byte(0, 0, 0, 0xf4, config|0x2);

	/* read xTPR register */
	word = read_pci_config_16(0, 0, 0x40, 0x4c);

	if (!(word & (1 << 13))) {
		printk(KERN_INFO "Disabling irq balancing and affinity\n");
#ifdef CONFIG_IRQBALANCE
		irqbalance_disable("");
#endif
		noirqdebug_setup("");
#ifdef CONFIG_PROC_FS
		no_irq_affinity = 1;
#endif
#ifdef CONFIG_HOTPLUG_CPU
		printk(KERN_INFO "Disabling cpu hotplug control\n");
		enable_cpu_hotplug = 0;
#endif
#ifdef CONFIG_X86_64
		/* force the genapic selection to flat mode so that
		 * interrupts can be redirected to more than one CPU.
		 */
		genapic_force = &apic_flat;
#endif
	}

	/* put back the original value for config space */
	if (!(config & 0x2))
		write_pci_config_byte(0, 0, 0, 0xf4, config);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,   PCI_DEVICE_ID_INTEL_E7320_MCH,  verify_quirk_intel_irqbalance);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,   PCI_DEVICE_ID_INTEL_E7525_MCH,  verify_quirk_intel_irqbalance);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,   PCI_DEVICE_ID_INTEL_E7520_MCH,  verify_quirk_intel_irqbalance);

#endif
