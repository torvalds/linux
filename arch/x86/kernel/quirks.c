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
	u16 word;

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

	/*
	 * read xTPR register.  We may not have a pci_dev for device 8
	 * because it might be hidden until the above write.
	 */
	pci_bus_read_config_word(dev->bus, PCI_DEVFN(8, 0), 0x4c, &word);

	if (!(word & (1 << 13))) {
		dev_info(&dev->dev, "Intel E7520/7320/7525 detected; "
			"disabling irq balancing and affinity\n");
		noirqdebug_setup("");
#ifdef CONFIG_PROC_FS
		no_irq_affinity = 1;
#endif
	}

	/* put back the original value for config space*/
	if (!(config & 0x2))
		pci_write_config_byte(dev, 0xf4, config);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_E7320_MCH,
			quirk_intel_irqbalance);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_E7525_MCH,
			quirk_intel_irqbalance);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_E7520_MCH,
			quirk_intel_irqbalance);
#endif

#if defined(CONFIG_HPET_TIMER)
unsigned long force_hpet_address;

static enum {
	NONE_FORCE_HPET_RESUME,
	OLD_ICH_FORCE_HPET_RESUME,
	ICH_FORCE_HPET_RESUME,
	VT8237_FORCE_HPET_RESUME,
	NVIDIA_FORCE_HPET_RESUME,
	ATI_FORCE_HPET_RESUME,
} force_hpet_resume_type;

static void __iomem *rcba_base;

static void ich_force_hpet_resume(void)
{
	u32 val;

	if (!force_hpet_address)
		return;

	BUG_ON(rcba_base == NULL);

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
		dev_printk(KERN_DEBUG, &dev->dev, "RCBA disabled; "
			"cannot force enable HPET\n");
		return;
	}

	/* use bits 31:14, 16 kB aligned */
	rcba_base = ioremap_nocache(rcba, 0x4000);
	if (rcba_base == NULL) {
		dev_printk(KERN_DEBUG, &dev->dev, "ioremap failed; "
			"cannot force enable HPET\n");
		return;
	}

	/* read the Function Disable register, dword mode only */
	val = readl(rcba_base + 0x3404);

	if (val & 0x80) {
		/* HPET is enabled in HPTC. Just not reported by BIOS */
		val = val & 0x3;
		force_hpet_address = 0xFED00000 | (val << 12);
		dev_printk(KERN_DEBUG, &dev->dev, "Force enabled HPET at "
			"0x%lx\n", force_hpet_address);
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
		dev_printk(KERN_DEBUG, &dev->dev,
			"Failed to force enable HPET\n");
	} else {
		force_hpet_resume_type = ICH_FORCE_HPET_RESUME;
		dev_printk(KERN_DEBUG, &dev->dev, "Force enabled HPET at "
			"0x%lx\n", force_hpet_address);
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB2_0,
			 ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_0,
			 ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_1,
			 ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_0,
			 ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_1,
			 ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_31,
			 ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH8_1,
			 ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH8_4,
			 ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH9_7,
			 ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x3a16,	/* ICH10 */
			 ich_force_enable_hpet);

static struct pci_dev *cached_dev;

static void hpet_print_force_info(void)
{
	printk(KERN_INFO "HPET not enabled in BIOS. "
	       "You might try hpet=force boot option\n");
}

static void old_ich_force_hpet_resume(void)
{
	u32 val;
	u32 uninitialized_var(gen_cntl);

	if (!force_hpet_address || !cached_dev)
		return;

	pci_read_config_dword(cached_dev, 0xD0, &gen_cntl);
	gen_cntl &= (~(0x7 << 15));
	gen_cntl |= (0x4 << 15);

	pci_write_config_dword(cached_dev, 0xD0, gen_cntl);
	pci_read_config_dword(cached_dev, 0xD0, &gen_cntl);
	val = gen_cntl >> 15;
	val &= 0x7;
	if (val == 0x4)
		printk(KERN_DEBUG "Force enabled HPET at resume\n");
	else
		BUG();
}

static void old_ich_force_enable_hpet(struct pci_dev *dev)
{
	u32 val;
	u32 uninitialized_var(gen_cntl);

	if (hpet_address || force_hpet_address)
		return;

	pci_read_config_dword(dev, 0xD0, &gen_cntl);
	/*
	 * Bit 17 is HPET enable bit.
	 * Bit 16:15 control the HPET base address.
	 */
	val = gen_cntl >> 15;
	val &= 0x7;
	if (val & 0x4) {
		val &= 0x3;
		force_hpet_address = 0xFED00000 | (val << 12);
		dev_printk(KERN_DEBUG, &dev->dev, "HPET at 0x%lx\n",
			force_hpet_address);
		return;
	}

	/*
	 * HPET is disabled. Trying enabling at FED00000 and check
	 * whether it sticks
	 */
	gen_cntl &= (~(0x7 << 15));
	gen_cntl |= (0x4 << 15);
	pci_write_config_dword(dev, 0xD0, gen_cntl);

	pci_read_config_dword(dev, 0xD0, &gen_cntl);

	val = gen_cntl >> 15;
	val &= 0x7;
	if (val & 0x4) {
		/* HPET is enabled in HPTC. Just not reported by BIOS */
		val &= 0x3;
		force_hpet_address = 0xFED00000 | (val << 12);
		dev_printk(KERN_DEBUG, &dev->dev, "Force enabled HPET at "
			"0x%lx\n", force_hpet_address);
		cached_dev = dev;
		force_hpet_resume_type = OLD_ICH_FORCE_HPET_RESUME;
		return;
	}

	dev_printk(KERN_DEBUG, &dev->dev, "Failed to force enable HPET\n");
}

/*
 * Undocumented chipset features. Make sure that the user enforced
 * this.
 */
static void old_ich_force_enable_hpet_user(struct pci_dev *dev)
{
	if (hpet_force_user)
		old_ich_force_enable_hpet(dev);
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB_1,
			 old_ich_force_enable_hpet_user);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_0,
			 old_ich_force_enable_hpet_user);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_12,
			 old_ich_force_enable_hpet_user);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_0,
			 old_ich_force_enable_hpet_user);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_12,
			 old_ich_force_enable_hpet_user);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_0,
			 old_ich_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_12,
			 old_ich_force_enable_hpet);


static void vt8237_force_hpet_resume(void)
{
	u32 val;

	if (!force_hpet_address || !cached_dev)
		return;

	val = 0xfed00000 | 0x80;
	pci_write_config_dword(cached_dev, 0x68, val);

	pci_read_config_dword(cached_dev, 0x68, &val);
	if (val & 0x80)
		printk(KERN_DEBUG "Force enabled HPET at resume\n");
	else
		BUG();
}

static void vt8237_force_enable_hpet(struct pci_dev *dev)
{
	u32 uninitialized_var(val);

	if (hpet_address || force_hpet_address)
		return;

	if (!hpet_force_user) {
		hpet_print_force_info();
		return;
	}

	pci_read_config_dword(dev, 0x68, &val);
	/*
	 * Bit 7 is HPET enable bit.
	 * Bit 31:10 is HPET base address (contrary to what datasheet claims)
	 */
	if (val & 0x80) {
		force_hpet_address = (val & ~0x3ff);
		dev_printk(KERN_DEBUG, &dev->dev, "HPET at 0x%lx\n",
			force_hpet_address);
		return;
	}

	/*
	 * HPET is disabled. Trying enabling at FED00000 and check
	 * whether it sticks
	 */
	val = 0xfed00000 | 0x80;
	pci_write_config_dword(dev, 0x68, val);

	pci_read_config_dword(dev, 0x68, &val);
	if (val & 0x80) {
		force_hpet_address = (val & ~0x3ff);
		dev_printk(KERN_DEBUG, &dev->dev, "Force enabled HPET at "
			"0x%lx\n", force_hpet_address);
		cached_dev = dev;
		force_hpet_resume_type = VT8237_FORCE_HPET_RESUME;
		return;
	}

	dev_printk(KERN_DEBUG, &dev->dev, "Failed to force enable HPET\n");
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8235,
			 vt8237_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8237,
			 vt8237_force_enable_hpet);

static void ati_force_hpet_resume(void)
{
	pci_write_config_dword(cached_dev, 0x14, 0xfed00000);
	printk(KERN_DEBUG "Force enabled HPET at resume\n");
}

static u32 ati_ixp4x0_rev(struct pci_dev *dev)
{
	u32 d;
	u8  b;

	pci_read_config_byte(dev, 0xac, &b);
	b &= ~(1<<5);
	pci_write_config_byte(dev, 0xac, b);
	pci_read_config_dword(dev, 0x70, &d);
	d |= 1<<8;
	pci_write_config_dword(dev, 0x70, d);
	pci_read_config_dword(dev, 0x8, &d);
	d &= 0xff;
	dev_printk(KERN_DEBUG, &dev->dev, "SB4X0 revision 0x%x\n", d);
	return d;
}

static void ati_force_enable_hpet(struct pci_dev *dev)
{
	u32 d, val;
	u8  b;

	if (hpet_address || force_hpet_address)
		return;

	if (!hpet_force_user) {
		hpet_print_force_info();
		return;
	}

	d = ati_ixp4x0_rev(dev);
	if (d  < 0x82)
		return;

	/* base address */
	pci_write_config_dword(dev, 0x14, 0xfed00000);
	pci_read_config_dword(dev, 0x14, &val);

	/* enable interrupt */
	outb(0x72, 0xcd6); b = inb(0xcd7);
	b |= 0x1;
	outb(0x72, 0xcd6); outb(b, 0xcd7);
	outb(0x72, 0xcd6); b = inb(0xcd7);
	if (!(b & 0x1))
		return;
	pci_read_config_dword(dev, 0x64, &d);
	d |= (1<<10);
	pci_write_config_dword(dev, 0x64, d);
	pci_read_config_dword(dev, 0x64, &d);
	if (!(d & (1<<10)))
		return;

	force_hpet_address = val;
	force_hpet_resume_type = ATI_FORCE_HPET_RESUME;
	dev_printk(KERN_DEBUG, &dev->dev, "Force enabled HPET at 0x%lx\n",
		   force_hpet_address);
	cached_dev = dev;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP400_SMBUS,
			 ati_force_enable_hpet);

/*
 * Undocumented chipset feature taken from LinuxBIOS.
 */
static void nvidia_force_hpet_resume(void)
{
	pci_write_config_dword(cached_dev, 0x44, 0xfed00001);
	printk(KERN_DEBUG "Force enabled HPET at resume\n");
}

static void nvidia_force_enable_hpet(struct pci_dev *dev)
{
	u32 uninitialized_var(val);

	if (hpet_address || force_hpet_address)
		return;

	if (!hpet_force_user) {
		hpet_print_force_info();
		return;
	}

	pci_write_config_dword(dev, 0x44, 0xfed00001);
	pci_read_config_dword(dev, 0x44, &val);
	force_hpet_address = val & 0xfffffffe;
	force_hpet_resume_type = NVIDIA_FORCE_HPET_RESUME;
	dev_printk(KERN_DEBUG, &dev->dev, "Force enabled HPET at 0x%lx\n",
		force_hpet_address);
	cached_dev = dev;
	return;
}

/* ISA Bridges */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0050,
			nvidia_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0051,
			nvidia_force_enable_hpet);

/* LPC bridges */
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0260,
			nvidia_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0360,
			nvidia_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0361,
			nvidia_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0362,
			nvidia_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0363,
			nvidia_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0364,
			nvidia_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0365,
			nvidia_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0366,
			nvidia_force_enable_hpet);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NVIDIA, 0x0367,
			nvidia_force_enable_hpet);

void force_hpet_resume(void)
{
	switch (force_hpet_resume_type) {
	case ICH_FORCE_HPET_RESUME:
		ich_force_hpet_resume();
		return;
	case OLD_ICH_FORCE_HPET_RESUME:
		old_ich_force_hpet_resume();
		return;
	case VT8237_FORCE_HPET_RESUME:
		vt8237_force_hpet_resume();
		return;
	case NVIDIA_FORCE_HPET_RESUME:
		nvidia_force_hpet_resume();
		return;
	case ATI_FORCE_HPET_RESUME:
		ati_force_hpet_resume();
		return;
	default:
		break;
	}
}
#endif

#if defined(CONFIG_PCI) && defined(CONFIG_NUMA)
/* Set correct numa_node information for AMD NB functions */
static void __init quirk_amd_nb_node(struct pci_dev *dev)
{
	struct pci_dev *nb_ht;
	unsigned int devfn;
	u32 val;

	devfn = PCI_DEVFN(PCI_SLOT(dev->devfn), 0);
	nb_ht = pci_get_slot(dev->bus, devfn);
	if (!nb_ht)
		return;

	pci_read_config_dword(nb_ht, 0x60, &val);
	set_dev_node(&dev->dev, val & 7);
	pci_dev_put(dev);
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_ADDRMAP,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MEMCTL,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MISC,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_HT,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_MAP,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_DRAM,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_MISC,
			quirk_amd_nb_node);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_10H_NB_LINK,
			quirk_amd_nb_node);
#endif
