/*
 * generic/default IDE host driver
 *
 * Copyright (C) 2004, 2008 Bartlomiej Zolnierkiewicz
 * This code was split off from ide.c.  See it for original copyrights.
 *
 * May be copied or modified under the terms of the GNU General Public License.
 */

/*
 * For special cases new interfaces may be added using sysfs, i.e.
 *
 *	echo -n "0x168:0x36e:10" > /sys/class/ide_generic/add
 *
 * will add an interface using I/O ports 0x168-0x16f/0x36e and IRQ 10.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ide.h>
#include <linux/pci_ids.h>

/* FIXME: convert m32r to use ide_platform host driver */
#ifdef CONFIG_M32R
#include <asm/m32r.h>
#endif

#define DRV_NAME	"ide_generic"

static int probe_mask;
module_param(probe_mask, int, 0);
MODULE_PARM_DESC(probe_mask, "probe mask for legacy ISA IDE ports");

static ssize_t store_add(struct class *cls, const char *buf, size_t n)
{
	unsigned int base, ctl;
	int irq, rc;
	hw_regs_t hw, *hws[] = { &hw, NULL, NULL, NULL };

	if (sscanf(buf, "%x:%x:%d", &base, &ctl, &irq) != 3)
		return -EINVAL;

	memset(&hw, 0, sizeof(hw));
	ide_std_init_ports(&hw, base, ctl);
	hw.irq = irq;
	hw.chipset = ide_generic;

	rc = ide_host_add(NULL, hws, NULL);
	if (rc)
		return rc;

	return n;
};

static struct class_attribute ide_generic_class_attrs[] = {
	__ATTR(add, S_IWUSR, NULL, store_add),
	__ATTR_NULL
};

static void ide_generic_class_release(struct class *cls)
{
	kfree(cls);
}

static int __init ide_generic_sysfs_init(void)
{
	struct class *cls;
	int rc;

	cls = kzalloc(sizeof(*cls), GFP_KERNEL);
	if (!cls)
		return -ENOMEM;

	cls->name = DRV_NAME;
	cls->owner = THIS_MODULE;
	cls->class_release = ide_generic_class_release;
	cls->class_attrs = ide_generic_class_attrs;

	rc = class_register(cls);
	if (rc) {
		kfree(cls);
		return rc;
	}

	return 0;
}

#if defined(CONFIG_PLAT_M32700UT) || defined(CONFIG_PLAT_MAPPI2) \
	|| defined(CONFIG_PLAT_OPSPUT)
static const u16 legacy_bases[] = { 0x1f0 };
static const int legacy_irqs[]  = { PLD_IRQ_CFIREQ };
#elif defined(CONFIG_PLAT_MAPPI3)
static const u16 legacy_bases[] = { 0x1f0, 0x170 };
static const int legacy_irqs[]  = { PLD_IRQ_CFIREQ, PLD_IRQ_IDEIREQ };
#elif defined(CONFIG_ALPHA)
static const u16 legacy_bases[] = { 0x1f0, 0x170, 0x1e8, 0x168 };
static const int legacy_irqs[]  = { 14, 15, 11, 10 };
#else
static const u16 legacy_bases[] = { 0x1f0, 0x170, 0x1e8, 0x168, 0x1e0, 0x160 };
static const int legacy_irqs[]  = { 14, 15, 11, 10, 8, 12 };
#endif

static void ide_generic_check_pci_legacy_iobases(int *primary, int *secondary)
{
	struct pci_dev *p = NULL;
	u16 val;

	for_each_pci_dev(p) {

		if (pci_resource_start(p, 0) == 0x1f0)
			*primary = 1;
		if (pci_resource_start(p, 2) == 0x170)
			*secondary = 1;

		/* Cyrix CS55{1,2}0 pre SFF MWDMA ATA on the bridge */
		if (p->vendor == PCI_VENDOR_ID_CYRIX &&
		    (p->device == PCI_DEVICE_ID_CYRIX_5510 ||
		     p->device == PCI_DEVICE_ID_CYRIX_5520))
			*primary = *secondary = 1;

		/* Intel MPIIX - PIO ATA on non PCI side of bridge */
		if (p->vendor == PCI_VENDOR_ID_INTEL &&
		    p->device == PCI_DEVICE_ID_INTEL_82371MX) {

			pci_read_config_word(p, 0x6C, &val);
			if (val & 0x8000) {
				/* ATA port enabled */
				if (val & 0x4000)
					*secondary = 1;
				else
					*primary = 1;
			}
		}
	}
}

static int __init ide_generic_init(void)
{
	hw_regs_t hw, *hws[] = { &hw, NULL, NULL, NULL };
	unsigned long io_addr;
	int i, rc = 0, primary = 0, secondary = 0;

	ide_generic_check_pci_legacy_iobases(&primary, &secondary);

	if (!probe_mask) {
		printk(KERN_INFO DRV_NAME ": please use \"probe_mask=0x3f\" "
		     "module parameter for probing all legacy ISA IDE ports\n");

		if (primary == 0)
			probe_mask |= 0x1;

		if (secondary == 0)
			probe_mask |= 0x2;
	} else
		printk(KERN_INFO DRV_NAME ": enforcing probing of I/O ports "
			"upon user request\n");

	for (i = 0; i < ARRAY_SIZE(legacy_bases); i++) {
		io_addr = legacy_bases[i];

		if ((probe_mask & (1 << i)) && io_addr) {
			if (!request_region(io_addr, 8, DRV_NAME)) {
				printk(KERN_ERR "%s: I/O resource 0x%lX-0x%lX "
						"not free.\n",
						DRV_NAME, io_addr, io_addr + 7);
				continue;
			}

			if (!request_region(io_addr + 0x206, 1, DRV_NAME)) {
				printk(KERN_ERR "%s: I/O resource 0x%lX "
						"not free.\n",
						DRV_NAME, io_addr + 0x206);
				release_region(io_addr, 8);
				continue;
			}

			memset(&hw, 0, sizeof(hw));
			ide_std_init_ports(&hw, io_addr, io_addr + 0x206);
#ifdef CONFIG_IA64
			hw.irq = isa_irq_to_vector(legacy_irqs[i]);
#else
			hw.irq = legacy_irqs[i];
#endif
			hw.chipset = ide_generic;

			rc = ide_host_add(NULL, hws, NULL);
			if (rc) {
				release_region(io_addr + 0x206, 1);
				release_region(io_addr, 8);
			}
		}
	}

	if (ide_generic_sysfs_init())
		printk(KERN_ERR DRV_NAME ": failed to create ide_generic "
					 "class\n");

	return rc;
}

module_init(ide_generic_init);

MODULE_LICENSE("GPL");
