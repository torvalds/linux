#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/of_device.h>
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

static void __init isa_dev_get_resource(struct sparc_isa_device *isa_dev)
{
	struct of_device *op = of_find_device_by_node(isa_dev->prom_node);

	memcpy(&isa_dev->resource, &op->resource[0], sizeof(struct resource));
}

static void __init isa_dev_get_irq(struct sparc_isa_device *isa_dev)
{
	struct of_device *op = of_find_device_by_node(isa_dev->prom_node);

	if (!op || !op->num_irqs) {
		isa_dev->irq = PCI_IRQ_NONE;
	} else {
		isa_dev->irq = op->irqs[0];
	}
}

static void __init isa_fill_children(struct sparc_isa_device *parent_isa_dev)
{
	struct device_node *dp = parent_isa_dev->prom_node->child;

	if (!dp)
		return;

	printk(" ->");
	while (dp) {
		struct sparc_isa_device *isa_dev;

		isa_dev = kzalloc(sizeof(*isa_dev), GFP_KERNEL);
		if (!isa_dev) {
			fatal_err("cannot allocate child isa_dev");
			prom_halt();
		}

		/* Link it in to parent. */
		isa_dev->next = parent_isa_dev->child;
		parent_isa_dev->child = isa_dev;

		isa_dev->bus = parent_isa_dev->bus;
		isa_dev->prom_node = dp;

		isa_dev_get_resource(isa_dev);
		isa_dev_get_irq(isa_dev);

		report_dev(isa_dev, 1);

		dp = dp->sibling;
	}
}

static void __init isa_fill_devices(struct sparc_isa_bridge *isa_br)
{
	struct device_node *dp = isa_br->prom_node->child;

	while (dp) {
		struct sparc_isa_device *isa_dev;
		struct dev_archdata *sd;

		isa_dev = kzalloc(sizeof(*isa_dev), GFP_KERNEL);
		if (!isa_dev) {
			printk(KERN_DEBUG "ISA: cannot allocate isa_dev");
			return;
		}

		sd = &isa_dev->ofdev.dev.archdata;
		sd->prom_node = dp;
		sd->op = &isa_dev->ofdev;
		sd->iommu = isa_br->ofdev.dev.parent->archdata.iommu;
		sd->stc = isa_br->ofdev.dev.parent->archdata.stc;

		isa_dev->ofdev.node = dp;
		isa_dev->ofdev.dev.parent = &isa_br->ofdev.dev;
		isa_dev->ofdev.dev.bus = &isa_bus_type;
		sprintf(isa_dev->ofdev.dev.bus_id, "isa[%08x]", dp->node);

		/* Register with core */
		if (of_device_register(&isa_dev->ofdev) != 0) {
			printk(KERN_DEBUG "isa: device registration error for %s!\n",
			       dp->path_component_name);
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

		isa_dev_get_resource(isa_dev);
		isa_dev_get_irq(isa_dev);

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
		struct sparc_isa_bridge *isa_br;
		struct device_node *dp;

		dp = pci_device_to_OF_node(pdev);

		isa_br = kzalloc(sizeof(*isa_br), GFP_KERNEL);
		if (!isa_br) {
			printk(KERN_DEBUG "isa: cannot allocate sparc_isa_bridge");
			pci_dev_put(pdev);
			return;
		}

		isa_br->ofdev.node = dp;
		isa_br->ofdev.dev.parent = &pdev->dev;
		isa_br->ofdev.dev.bus = &isa_bus_type;
		sprintf(isa_br->ofdev.dev.bus_id, "isa%d", index);

		/* Register with core */
		if (of_device_register(&isa_br->ofdev) != 0) {
			printk(KERN_DEBUG "isa: device registration error for %s!\n",
			       dp->path_component_name);
			kfree(isa_br);
			pci_dev_put(pdev);
			return;
		}

		/* Link it in. */
		isa_br->next = isa_chain;
		isa_chain = isa_br;

		isa_br->self = pdev;
		isa_br->index = index++;
		isa_br->prom_node = dp;

		printk("isa%d:", isa_br->index);

		isa_fill_devices(isa_br);

		printk("\n");
	}
}
