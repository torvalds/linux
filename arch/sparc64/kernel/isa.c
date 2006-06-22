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
		printk(" (%s)", isa_dev->prom_name);
	else
		printk(" [%s", isa_dev->prom_name);
}

static void __init isa_dev_get_resource(struct sparc_isa_device *isa_dev,
					struct linux_prom_registers *pregs,
					int pregs_size)
{
	unsigned long base, len;
	int prop_len;

	prop_len = prom_getproperty(isa_dev->prom_node, "reg",
				    (char *) pregs, pregs_size);

	if (prop_len <= 0)
		return;

	/* Only the first one is interesting. */
	len = pregs[0].reg_size;
	base = (((unsigned long)pregs[0].which_io << 32) |
		(unsigned long)pregs[0].phys_addr);
	base += isa_dev->bus->parent->io_space.start;

	isa_dev->resource.start = base;
	isa_dev->resource.end   = (base + len - 1UL);
	isa_dev->resource.flags = IORESOURCE_IO;
	isa_dev->resource.name  = isa_dev->prom_name;

	request_resource(&isa_dev->bus->parent->io_space,
			 &isa_dev->resource);
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
					     struct linux_prom_registers *pregs)
{
	unsigned int hi, lo, irq;
	int i;

	hi = pregs->which_io & isa_br->isa_intmask.phys_hi;
	lo = pregs->phys_addr & isa_br->isa_intmask.phys_lo;
	irq = *interrupt & isa_br->isa_intmask.interrupt;
	for (i = 0; i < isa_br->num_isa_intmap; i++) {
		if ((isa_br->isa_intmap[i].phys_hi == hi) &&
		    (isa_br->isa_intmap[i].phys_lo == lo) &&
		    (isa_br->isa_intmap[i].interrupt == irq)) {
			*interrupt = isa_br->isa_intmap[i].cinterrupt;
			return 0;
		}
	}
	return -1;
}

static void __init isa_dev_get_irq(struct sparc_isa_device *isa_dev,
				   struct linux_prom_registers *pregs)
{
	int irq_prop;

	irq_prop = prom_getintdefault(isa_dev->prom_node,
				      "interrupts", -1);
	if (irq_prop <= 0) {
		goto no_irq;
	} else {
		struct pci_controller_info *pcic;
		struct pci_pbm_info *pbm;
		int i;

		if (isa_dev->bus->num_isa_intmap) {
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
	int node = prom_getchild(parent_isa_dev->prom_node);

	if (node == 0)
		return;

	printk(" ->");
	while (node != 0) {
		struct linux_prom_registers regs[PROMREG_MAX];
		struct sparc_isa_device *isa_dev;
		int prop_len;

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
		isa_dev->prom_node = node;
		prop_len = prom_getproperty(node, "name",
					    (char *) isa_dev->prom_name,
					    sizeof(isa_dev->prom_name));
		if (prop_len <= 0) {
			fatal_err("cannot get child isa_dev OBP node name");
			prom_halt();
		}

		prop_len = prom_getproperty(node, "compatible",
					    (char *) isa_dev->compatible,
					    sizeof(isa_dev->compatible));

		/* Not having this is OK. */
		if (prop_len <= 0)
			isa_dev->compatible[0] = '\0';

		isa_dev_get_resource(isa_dev, regs, sizeof(regs));
		isa_dev_get_irq(isa_dev, regs);

		report_dev(isa_dev, 1);

		node = prom_getsibling(node);
	}
}

static void __init isa_fill_devices(struct sparc_isa_bridge *isa_br)
{
	int node = prom_getchild(isa_br->prom_node);

	while (node != 0) {
		struct linux_prom_registers regs[PROMREG_MAX];
		struct sparc_isa_device *isa_dev;
		int prop_len;

		isa_dev = kmalloc(sizeof(*isa_dev), GFP_KERNEL);
		if (!isa_dev) {
			fatal_err("cannot allocate isa_dev");
			prom_halt();
		}

		memset(isa_dev, 0, sizeof(*isa_dev));

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
		isa_dev->prom_node = node;
		prop_len = prom_getproperty(node, "name",
					    (char *) isa_dev->prom_name,
					    sizeof(isa_dev->prom_name));
		if (prop_len <= 0) {
			fatal_err("cannot get isa_dev OBP node name");
			prom_halt();
		}

		prop_len = prom_getproperty(node, "compatible",
					    (char *) isa_dev->compatible,
					    sizeof(isa_dev->compatible));

		/* Not having this is OK. */
		if (prop_len <= 0)
			isa_dev->compatible[0] = '\0';

		isa_dev_get_resource(isa_dev, regs, sizeof(regs));
		isa_dev_get_irq(isa_dev, regs);

		report_dev(isa_dev, 0);

		isa_fill_children(isa_dev);

		printk("]");

		node = prom_getsibling(node);
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
		int prop_len;

		pdev_cookie = pdev->sysdata;
		if (!pdev_cookie) {
			printk("ISA: Warning, ISA bridge ignored due to "
			       "lack of OBP data.\n");
			continue;
		}
		pbm = pdev_cookie->pbm;

		isa_br = kmalloc(sizeof(*isa_br), GFP_KERNEL);
		if (!isa_br) {
			fatal_err("cannot allocate sparc_isa_bridge");
			prom_halt();
		}

		memset(isa_br, 0, sizeof(*isa_br));

		/* Link it in. */
		isa_br->next = isa_chain;
		isa_chain = isa_br;

		isa_br->parent = pbm;
		isa_br->self = pdev;
		isa_br->index = index++;
		isa_br->prom_node = pdev_cookie->prom_node->node;
		strncpy(isa_br->prom_name, pdev_cookie->prom_node->name,
			sizeof(isa_br->prom_name));

		prop_len = prom_getproperty(isa_br->prom_node,
					    "ranges",
					    (char *) isa_br->isa_ranges,
					    sizeof(isa_br->isa_ranges));
		if (prop_len <= 0)
			isa_br->num_isa_ranges = 0;
		else
			isa_br->num_isa_ranges =
				(prop_len / sizeof(struct linux_prom_isa_ranges));

		prop_len = prom_getproperty(isa_br->prom_node,
					    "interrupt-map",
					    (char *) isa_br->isa_intmap,
					    sizeof(isa_br->isa_intmap));
		if (prop_len <= 0)
			isa_br->num_isa_intmap = 0;
		else
			isa_br->num_isa_intmap =
				(prop_len / sizeof(struct linux_prom_isa_intmap));

		prop_len = prom_getproperty(isa_br->prom_node,
					    "interrupt-map-mask",
					    (char *) &(isa_br->isa_intmask),
					    sizeof(isa_br->isa_intmask));

		printk("isa%d:", isa_br->index);

		isa_fill_devices(isa_br);

		printk("\n");
	}
}
