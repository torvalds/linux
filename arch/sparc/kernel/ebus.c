/*
 * ebus.c: PCI to EBus bridge device.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 *
 * Adopted for sparc by V. Roganov and G. Raiko.
 * Fixes for different platforms by Pete Zaitcev.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/system.h>
#include <asm/page.h>
#include <asm/pbm.h>
#include <asm/ebus.h>
#include <asm/io.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/bpp.h>

struct linux_ebus *ebus_chain = NULL;

/* We are together with pcic.c under CONFIG_PCI. */
extern unsigned int pcic_pin_to_irq(unsigned int, const char *name);

/*
 * IRQ Blacklist
 * Here we list PROMs and systems that are known to supply crap as IRQ numbers.
 */
struct ebus_device_irq {
	char *name;
	unsigned int pin;
};

struct ebus_system_entry {
	char *esname;
	struct ebus_device_irq *ipt;
};

static struct ebus_device_irq je1_1[] = {
	{ "8042",		 3 },
	{ "SUNW,CS4231",	 0 },
	{ "parallel",		 0 },
	{ "se",			 2 },
	{ NULL, 0 }
};

/*
 * Gleb's JE1 supplied reasonable pin numbers, but mine did not (OBP 2.32).
 * Blacklist the sucker... Note that Gleb's system will work.
 */
static struct ebus_system_entry ebus_blacklist[] = {
	{ "SUNW,JavaEngine1", je1_1 },
	{ NULL, NULL }
};

static struct ebus_device_irq *ebus_blackp = NULL;

/*
 */
static inline unsigned long ebus_alloc(size_t size)
{
	return (unsigned long)kmalloc(size, GFP_ATOMIC);
}

/*
 */
static int __init ebus_blacklist_irq(const char *name)
{
	struct ebus_device_irq *dp;

	if ((dp = ebus_blackp) != NULL) {
		for (; dp->name != NULL; dp++) {
			if (strcmp(name, dp->name) == 0) {
				return pcic_pin_to_irq(dp->pin, name);
			}
		}
	}
	return 0;
}

static void __init fill_ebus_child(struct device_node *dp,
				   struct linux_ebus_child *dev)
{
	const int *regs;
	const int *irqs;
	int i, len;

	dev->prom_node = dp;
	regs = of_get_property(dp, "reg", &len);
	if (!regs)
		len = 0;
	dev->num_addrs = len / sizeof(regs[0]);

	for (i = 0; i < dev->num_addrs; i++) {
		if (regs[i] >= dev->parent->num_addrs) {
			prom_printf("UGH: property for %s was %d, need < %d\n",
				    dev->prom_node->name, len,
				    dev->parent->num_addrs);
			panic(__func__);
		}

		/* XXX resource */
		dev->resource[i].start =
			dev->parent->resource[regs[i]].start;
	}

	for (i = 0; i < PROMINTR_MAX; i++)
		dev->irqs[i] = PCI_IRQ_NONE;

	if ((dev->irqs[0] = ebus_blacklist_irq(dev->prom_node->name)) != 0) {
		dev->num_irqs = 1;
	} else {
		irqs = of_get_property(dp, "interrupts", &len);
		if (!irqs) {
			dev->num_irqs = 0;
			dev->irqs[0] = 0;
			if (dev->parent->num_irqs != 0) {
				dev->num_irqs = 1;
				dev->irqs[0] = dev->parent->irqs[0];
			}
		} else {
			dev->num_irqs = len / sizeof(irqs[0]);
			if (irqs[0] == 0 || irqs[0] >= 8) {
				/*
				 * XXX Zero is a valid pin number...
				 * This works as long as Ebus is not wired
				 * to INTA#.
				 */
				printk("EBUS: %s got bad irq %d from PROM\n",
				       dev->prom_node->name, irqs[0]);
				dev->num_irqs = 0;
				dev->irqs[0] = 0;
			} else {
				dev->irqs[0] =
					pcic_pin_to_irq(irqs[0],
							dev->prom_node->name);
			}
		}
	}
}

static void __init fill_ebus_device(struct device_node *dp,
				    struct linux_ebus_device *dev)
{
	const struct linux_prom_registers *regs;
	struct linux_ebus_child *child;
	struct dev_archdata *sd;
	const int *irqs;
	int i, n, len;
	unsigned long baseaddr;

	dev->prom_node = dp;

	regs = of_get_property(dp, "reg", &len);
	if (!regs)
		len = 0;
	if (len % sizeof(struct linux_prom_registers)) {
		prom_printf("UGH: proplen for %s was %d, need multiple of %d\n",
			    dev->prom_node->name, len,
			    (int)sizeof(struct linux_prom_registers));
		panic(__func__);
	}
	dev->num_addrs = len / sizeof(struct linux_prom_registers);

	for (i = 0; i < dev->num_addrs; i++) {
		/*
		 * XXX Collect JE-1 PROM
		 * 
		 * Example - JS-E with 3.11:
		 *  /ebus
		 *      regs 
		 *        0x00000000, 0x0, 0x00000000, 0x0, 0x00000000,
		 *        0x82000010, 0x0, 0xf0000000, 0x0, 0x01000000,
		 *        0x82000014, 0x0, 0x38800000, 0x0, 0x00800000,
		 *      ranges
		 *        0x00, 0x00000000, 0x02000010, 0x0, 0x0, 0x01000000,
		 *        0x01, 0x01000000, 0x02000014, 0x0, 0x0, 0x00800000,
		 *  /ebus/8042
		 *      regs
		 *        0x00000001, 0x00300060, 0x00000008,
		 *        0x00000001, 0x00300060, 0x00000008,
		 */
		n = regs[i].which_io;
		if (n >= 4) {
			/* XXX This is copied from old JE-1 by Gleb. */
			n = (regs[i].which_io - 0x10) >> 2;
		} else {
			;
		}

/*
 * XXX Now as we have regions, why don't we make an on-demand allocation...
 */
		dev->resource[i].start = 0;
		if ((baseaddr = dev->bus->self->resource[n].start +
		    regs[i].phys_addr) != 0) {
			/* dev->resource[i].name = dev->prom_name; */
			if ((baseaddr = (unsigned long) ioremap(baseaddr,
			    regs[i].reg_size)) == 0) {
				panic("ebus: unable to remap dev %s",
				      dev->prom_node->name);
			}
		}
		dev->resource[i].start = baseaddr;	/* XXX Unaligned */
	}

	for (i = 0; i < PROMINTR_MAX; i++)
		dev->irqs[i] = PCI_IRQ_NONE;

	if ((dev->irqs[0] = ebus_blacklist_irq(dev->prom_node->name)) != 0) {
		dev->num_irqs = 1;
	} else {
		irqs = of_get_property(dp, "interrupts", &len);
		if (!irqs) {
			dev->num_irqs = 0;
			if ((dev->irqs[0] = dev->bus->self->irq) != 0) {
				dev->num_irqs = 1;
/* P3 */ /* printk("EBUS: child %s irq %d from parent\n", dev->prom_name, dev->irqs[0]); */
			}
		} else {
			dev->num_irqs = 1;  /* dev->num_irqs = len / sizeof(irqs[0]); */
			if (irqs[0] == 0 || irqs[0] >= 8) {
				/* See above for the parent. XXX */
				printk("EBUS: %s got bad irq %d from PROM\n",
				       dev->prom_node->name, irqs[0]);
				dev->num_irqs = 0;
				dev->irqs[0] = 0;
			} else {
				dev->irqs[0] =
					pcic_pin_to_irq(irqs[0],
							dev->prom_node->name);
			}
		}
	}

	sd = &dev->ofdev.dev.archdata;
	sd->prom_node = dp;
	sd->op = &dev->ofdev;
	sd->iommu = dev->bus->ofdev.dev.parent->archdata.iommu;

	dev->ofdev.node = dp;
	dev->ofdev.dev.parent = &dev->bus->ofdev.dev;
	dev->ofdev.dev.bus = &ebus_bus_type;
	sprintf(dev->ofdev.dev.bus_id, "ebus[%08x]", dp->node);

	/* Register with core */
	if (of_device_register(&dev->ofdev) != 0)
		printk(KERN_DEBUG "ebus: device registration error for %s!\n",
		       dp->path_component_name);

	if ((dp = dp->child) != NULL) {
		dev->children = (struct linux_ebus_child *)
			ebus_alloc(sizeof(struct linux_ebus_child));

		child = dev->children;
		child->next = NULL;
		child->parent = dev;
		child->bus = dev->bus;
		fill_ebus_child(dp, child);

		while ((dp = dp->sibling) != NULL) {
			child->next = (struct linux_ebus_child *)
				ebus_alloc(sizeof(struct linux_ebus_child));

			child = child->next;
			child->next = NULL;
			child->parent = dev;
			child->bus = dev->bus;
			fill_ebus_child(dp, child);
		}
	}
}

void __init ebus_init(void)
{
	const struct linux_prom_pci_registers *regs;
	struct linux_pbm_info *pbm;
	struct linux_ebus_device *dev;
	struct linux_ebus *ebus;
	struct ebus_system_entry *sp;
	struct pci_dev *pdev;
	struct pcidev_cookie *cookie;
	struct device_node *dp;
	struct resource *p;
	unsigned short pci_command;
	int len, reg, nreg;
	int num_ebus = 0;

	dp = of_find_node_by_path("/");
	for (sp = ebus_blacklist; sp->esname != NULL; sp++) {
		if (strcmp(dp->name, sp->esname) == 0) {
			ebus_blackp = sp->ipt;
			break;
		}
	}

	pdev = pci_get_device(PCI_VENDOR_ID_SUN, PCI_DEVICE_ID_SUN_EBUS, NULL);
	if (!pdev)
		return;

	cookie = pdev->sysdata;
	dp = cookie->prom_node;

	ebus_chain = ebus = (struct linux_ebus *)
			ebus_alloc(sizeof(struct linux_ebus));
	ebus->next = NULL;

	while (dp) {
		struct device_node *nd;

		ebus->prom_node = dp;
		ebus->self = pdev;
		ebus->parent = pbm = cookie->pbm;

		/* Enable BUS Master. */
		pci_read_config_word(pdev, PCI_COMMAND, &pci_command);
		pci_command |= PCI_COMMAND_MASTER;
		pci_write_config_word(pdev, PCI_COMMAND, pci_command);

		regs = of_get_property(dp, "reg", &len);
		if (!regs) {
			prom_printf("%s: can't find reg property\n",
				    __func__);
			prom_halt();
		}
		nreg = len / sizeof(struct linux_prom_pci_registers);

		p = &ebus->self->resource[0];
		for (reg = 0; reg < nreg; reg++) {
			if (!(regs[reg].which_io & 0x03000000))
				continue;

			(p++)->start = regs[reg].phys_lo;
		}

		ebus->ofdev.node = dp;
		ebus->ofdev.dev.parent = &pdev->dev;
		ebus->ofdev.dev.bus = &ebus_bus_type;
		sprintf(ebus->ofdev.dev.bus_id, "ebus%d", num_ebus);

		/* Register with core */
		if (of_device_register(&ebus->ofdev) != 0)
			printk(KERN_DEBUG "ebus: device registration error for %s!\n",
			       dp->path_component_name);


		nd = dp->child;
		if (!nd)
			goto next_ebus;

		ebus->devices = (struct linux_ebus_device *)
				ebus_alloc(sizeof(struct linux_ebus_device));

		dev = ebus->devices;
		dev->next = NULL;
		dev->children = NULL;
		dev->bus = ebus;
		fill_ebus_device(nd, dev);

		while ((nd = nd->sibling) != NULL) {
			dev->next = (struct linux_ebus_device *)
				ebus_alloc(sizeof(struct linux_ebus_device));

			dev = dev->next;
			dev->next = NULL;
			dev->children = NULL;
			dev->bus = ebus;
			fill_ebus_device(nd, dev);
		}

	next_ebus:
		pdev = pci_get_device(PCI_VENDOR_ID_SUN,
				       PCI_DEVICE_ID_SUN_EBUS, pdev);
		if (!pdev)
			break;

		cookie = pdev->sysdata;
		dp = cookie->prom_node;

		ebus->next = (struct linux_ebus *)
			ebus_alloc(sizeof(struct linux_ebus));
		ebus = ebus->next;
		ebus->next = NULL;
		++num_ebus;
	}
	if (pdev)
		pci_dev_put(pdev);
}
