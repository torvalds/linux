#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <asm/oplib.h>
#include <asm/isa.h>

struct sparc_isa_bridge *isa_chain;

static void __init fatal_err(const char *reason)
{
	prom_printf("ISA: fatal error, %s.\n", reason);
}

static void __init report_dev(struct sparc_isa_device *isa_dev, int child)
{
	if (child)
		printk(" (%s)", isa_dev->prom_node->name);
	else
		printk(" [%s", isa_dev->prom_node->name);
}

static struct linux_prom_registers * __init
isa_dev_get_resource(struct sparc_isa_device *isa_dev)
{
	struct linux_prom_registers *pregs;
	unsigned long base, len;
	int prop_len;

	pregs = of_get_property(isa_dev->prom_node, "reg", &prop_len);

	/* Only the first one is interesting. */
	len = pregs[0].reg_size;
	base = (((unsigned long)pregs[0].which_io << 32) |
		(unsigned long)pregs[0].phys_addr);
	base += isa_dev->bus->parent->io_space.start;

	isa_dev->resource.start = base;
	isa_dev->resource.end   = (base + len - 1UL);
	isa_dev->resource.flags = IORESOURCE_IO;
	isa_dev->resource.name  = isa_dev->prom_node->name;

	request_resource(&isa_dev->bus->parent->io_space,
			 &isa_dev->resource);

	return pregs;
}

/* I can't believe they didn't put a real INO in the isa device
 * interrupts property.  The whole point of the OBP properties
 * is to shield the kernel from IRQ routing details.
 *
 * The P1275 standard for ISA devices seems to also have been
 * totally ignored.
 *
 * On later systems, an interrupt-map and interrupt-map-mask scheme
 * akin to EBUS is used.
 */
static struct {
	int	obp_irq;
	int	pci_ino;
} grover_irq_table[] = {
	{ 1, 0x00 },	/* dma, unknown ino at this point */
	{ 2, 0x27 },	/* floppy */
	{ 3, 0x22 },	/* parallel */
	{ 4, 0x2b },	/* serial */
	{ 5, 0x25 },	/* acpi power management */

	{ 0, 0x00 }	/* end of table */
};

static int __init isa_dev_get_irq_using_imap(struct sparc_isa_device *isa_dev,
					     struct sparc_isa_bridge *isa_br,
					     int *interrupt,
					     struct linux_prom_registers *reg)
{
	struct linux_prom_ebus_intmap *imap;
	struct linux_prom_ebus_intmap *imask;
	unsigned int hi, lo, irq;
	int i, len, n_imap;

	imap = of_get_property(isa_br->prom_node, "interrupt-map", &len);
	if (!imap)
		return 0;
	n_imap = len / sizeof(imap[0]);

	imask = of_get_property(isa_br->prom_node, "interrupt-map-mask", NULL);
	if (!imask)
		return 0;

	hi = reg->which_io & imask->phys_hi;
	lo = reg->phys_addr & imask->phys_lo;
	irq = *interrupt & imask->interrupt;
	for (i = 0; i < n_imap; i++) {
		if ((imap[i].phys_hi == hi) &&
		    (imap[i].phys_lo == lo) &&
		    (imap[i].interrupt == irq)) {
			*interrupt = imap[i].cinterrupt;
			return 0;
		}
	}
	return -1;
}

static void __init isa_dev_get_irq(struct sparc_isa_device *isa_dev,
				   struct linux_prom_registers *pregs)
{
	int irq_prop;

	irq_prop = of_getintprop_default(isa_dev->prom_node,
					 "interrupts", -1);
	if (irq_prop <= 0) {
		goto no_irq;
	} else {
		struct pci_controller_info *pcic;
		struct pci_pbm_info *pbm;
		int i;

		if (of_find_property(isa_dev->bus->prom_node,
				     "interrupt-map", NULL)) {
			if (!isa_dev_get_irq_using_imap(isa_dev,
							isa_dev->bus,
							&irq_prop,
							pregs))
				goto route_irq;
		}

		for (i = 0; grover_irq_table[i].obp_irq != 0; i++) {
			if (grover_irq_table[i].obp_irq == irq_prop) {
				int ino = grover_irq_table[i].pci_ino;

				if (ino == 0)
					goto no_irq;
 
				irq_prop = ino;
				goto route_irq;
			}
		}
		goto no_irq;

route_irq:
		pbm = isa_dev->bus->parent;
		pcic = pbm->parent;
		isa_dev->irq = pcic->irq_build(pbm, NULL, irq_prop);
		return;
	}

no_irq:
	isa_dev->irq = PCI_IRQ_NONE;
}

static void __init isa_fill_children(struct sparc_isa_device *parent_isa_dev)
{
	struct device_node *dp = parent_isa_dev->prom_node->child;

	if (!dp)
		return;

	printk(" ->");
	while (dp) {
		struct linux_prom_registers *regs;
		struct sparc_isa_device *isa_dev;

		isa_dev = kmalloc(sizeof(*isa_dev), GFP_KERNEL);
		if (!isa_dev) {
			fatal_err("cannot allocate child isa_dev");
			prom_halt();
		}

		memset(isa_dev, 0, sizeof(*isa_dev));

		/* Link it in to parent. */
		isa_dev->next = parent_isa_dev->child;
		parent_isa_dev->child = isa_dev;

		isa_dev->bus = parent_isa_dev->bus;
		isa_dev->prom_node = dp;

		regs = isa_dev_get_resource(isa_dev);
		isa_dev_get_irq(isa_dev, regs);

		report_dev(isa_dev, 1);

		dp = dp->sibling;
	}
}

static void __init isa_fill_devices(struct sparc_isa_bridge *isa_br)
{
	struct device_node *dp = isa_br->prom_node->child;

	while (dp) {
		struct linux_prom_registers *regs;
		struct sparc_isa_device *isa_dev;

		isa_dev = kmalloc(sizeof(*isa_dev), GFP_KERNEL);
		if (!isa_dev) {
			printk(KERN_DEBUG "ISA: cannot allocate isa_dev");
			return;
		}

		memset(isa_dev, 0, sizeof(*isa_dev));

		isa_dev->ofdev.node = dp;
		isa_dev->ofdev.dev.parent = &isa_br->ofdev.dev;
		isa_dev->ofdev.dev.bus = &isa_bus_type;
		strcpy(isa_dev->ofdev.dev.bus_id, dp->path_component_name);

		/* Register with core */
		if (of_device_register(&isa_dev->ofdev) != 0) {
			printk(KERN_DEBUG "isa: device registration error for %s!\n",
			       isa_dev->ofdev.dev.bus_id);
			kfree(isa_dev);
			goto next_sibling;
		}

		/* Link it in. */
		isa_dev->next = NULL;
		if (isa_br->devices == NULL) {
			isa_br->devices = isa_dev;
		} else {
			struct sparc_isa_device *tmp = isa_br->devices;

			while (tmp->next)
				tmp = tmp->next;

			tmp->next = isa_dev;
		}

		isa_dev->bus = isa_br;
		isa_dev->prom_node = dp;

		regs = isa_dev_get_resource(isa_dev);
		isa_dev_get_irq(isa_dev, regs);

		report_dev(isa_dev, 0);

		isa_fill_children(isa_dev);

		printk("]");

	next_sibling:
		dp = dp->sibling;
	}
}

void __init isa_init(void)
{
	struct pci_dev *pdev;
	unsigned short vendor, device;
	int index = 0;

	vendor = PCI_VENDOR_ID_AL;
	device = PCI_DEVICE_ID_AL_M1533;

	pdev = NULL;
	while ((pdev = pci_get_device(vendor, device, pdev)) != NULL) {
		struct pcidev_cookie *pdev_cookie;
		struct pci_pbm_info *pbm;
		struct sparc_isa_bridge *isa_br;
		struct device_node *dp;

		pdev_cookie = pdev->sysdata;
		if (!pdev_cookie) {
			printk("ISA: Warning, ISA bridge ignored due to "
			       "lack of OBP data.\n");
			continue;
		}
		pbm = pdev_cookie->pbm;
		dp = pdev_cookie->prom_node;

		isa_br = kmalloc(sizeof(*isa_br), GFP_KERNEL);
		if (!isa_br) {
			printk(KERN_DEBUG "isa: cannot allocate sparc_isa_bridge");
			return;
		}

		memset(isa_br, 0, sizeof(*isa_br));

		isa_br->ofdev.node = dp;
		isa_br->ofdev.dev.parent = &pdev->dev;
		isa_br->ofdev.dev.bus = &isa_bus_type;
		strcpy(isa_br->ofdev.dev.bus_id, dp->path_component_name);

		/* Register with core */
		if (of_device_register(&isa_br->ofdev) != 0) {
			printk(KERN_DEBUG "isa: device registration error for %s!\n",
			       isa_br->ofdev.dev.bus_id);
			kfree(isa_br);
			return;
		}

		/* Link it in. */
		isa_br->next = isa_chain;
		isa_chain = isa_br;

		isa_br->parent = pbm;
		isa_br->self = pdev;
		isa_br->index = index++;
		isa_br->prom_node = pdev_cookie->prom_node;

		printk("isa%d:", isa_br->index);

		isa_fill_devices(isa_br);

		printk("\n");
	}
}
