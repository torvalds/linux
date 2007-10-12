/*
 * This file contains work-arounds for x86 and x86_64 platform bugs.
 */
#include <linux/pci.h>
#include <linux/irq.h>

#include <asm/hpet.h>

#if defined(CONFIG_X86_IO_APIC) && defined(CONFIG_SMP) && defined(CONFIG_PCI)

static void __devinit quirk_intel_irqbalance(struct pci_dev *dev)
{
	u8 config, rev;
	u32 word;

	/* BIOS may enable hardware IRQ balancing for
	 * E7520/E7320/E7525(revision ID 0x9 and below)
	 * based platforms.
	 * Disable SW irqbalance/affinity on those platforms.
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
		printk(KERN_INFO "Intel E7520/7320/7525 detected. "
			"Disabling irq balancing and affinity\n");
#ifdef CONFIG_IRQBALANCE
		irqbalance_disable("");
#endif
		noirqdebug_setup("");
#ifdef CONFIG_PROC_FS
		no_irq_affinity = 1;
#endif
	}

	/* put back the original value for config space*/
	if (!(config & 0x2))
		pci_write_config_byte(dev, 0xf4, config);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7320_MCH,	quirk_intel_irqbalance);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7525_MCH,	quirk_intel_irqbalance);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7520_MCH,	quirk_intel_irqbalance);
#endif

#if defined(CONFIG_HPET_TIMER)
unsigned long force_hpet_address;

static void __iomem *rcba_base;

void ich_force_hpet_resume(void)
{
	u32 val;

	if (!force_hpet_address)
		return;

	if (rcba_base == NULL)
		BUG();

	/* read the Function Disable register, dword mode only */
	val = readl(rcba_base + 0x3404);
	if (!(val & 0x80)) {
		/* HPET disabled in HPTC. Trying to enable */
		writel(val | 0x80, rcba_base + 0x3404);
	}

	val = readl(rcba_base + 0x3404);
	if (!(val & 0x80))
		BUG();
	else
		printk(KERN_DEBUG "Force enabled HPET at resume\n");

	return;
}

static void ich_force_enable_hpet(struct pci_dev *dev)
{
	u32 val;
	u32 uninitialized_var(rcba);
	int err = 0;

	if (hpet_address || force_hpet_address)
		return;

	pci_read_config_dword(dev, 0xF0, &rcba);
	rcba &= 0xFFFFC000;
	if (rcba == 0) {
		printk(KERN_DEBUG "RCBA disabled. Cannot force enable HPET\n");
		return;
	}

	/* use bits 31:14, 16 kB aligned */
	rcba_base = ioremap_nocache(rcba, 0x4000);
	if (rcba_base == NULL) {
		printk(KERN_DEBUG "ioremap failed. Cannot force enable HPET\n");
		return;
	}

	/* read the Function Disable register, dword mode only */
	val = readl(rcba_base + 0x3404);

	if (val & 0x80) {
		/* HPET is enabled in HPTC. Just not reported by BIOS */
		val = val & 0x3;
		force_hpet_address = 0xFED00000 | (val << 12);
		printk(KERN_DEBUG "Force enabled HPET at base address 0x%lx\n",
			       force_hpet_address);
		iounmap(rcba_base);
		return;
	}

	/* HPET disabled in HPTC. Trying to enable */
	writel(val | 0x80, rcba_base + 0x3404);

	val = readl(rcba_base + 0x3404);
	if (!(val & 0x80)) {
		err = 1;
	} else {
		val = val & 0x3;
		force_hpet_address = 0xFED00000 | (val << 12);
	}

	if (err) {
		force_hpet_address = 0;
		iounmap(rcba_base);
		printk(KERN_DEBUG "Failed to force enable HPET\n");
	} else {
		printk(KERN_DEBUG "Force enabled HPET at base address 0x%lx\n",
			       force_hpet_address);
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB2_0,
                         ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_1,
                         ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_1,
                         ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_31,
                         ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH8_1,
                         ich_force_enable_hpet);
#endif
