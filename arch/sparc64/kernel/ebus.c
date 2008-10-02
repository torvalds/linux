/*
 * ebus.c: PCI to EBus bridge device.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1999  David S. Miller (davem@redhat.com)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/of_device.h>

#include <asm/system.h>
#include <asm/page.h>
#include <asm/ebus.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/bpp.h>
#include <asm/irq.h>
#include <asm/io.h>

/* EBUS dma library. */

#define EBDMA_CSR	0x00UL	/* Control/Status */
#define EBDMA_ADDR	0x04UL	/* DMA Address */
#define EBDMA_COUNT	0x08UL	/* DMA Count */

#define EBDMA_CSR_INT_PEND	0x00000001
#define EBDMA_CSR_ERR_PEND	0x00000002
#define EBDMA_CSR_DRAIN		0x00000004
#define EBDMA_CSR_INT_EN	0x00000010
#define EBDMA_CSR_RESET		0x00000080
#define EBDMA_CSR_WRITE		0x00000100
#define EBDMA_CSR_EN_DMA	0x00000200
#define EBDMA_CSR_CYC_PEND	0x00000400
#define EBDMA_CSR_DIAG_RD_DONE	0x00000800
#define EBDMA_CSR_DIAG_WR_DONE	0x00001000
#define EBDMA_CSR_EN_CNT	0x00002000
#define EBDMA_CSR_TC		0x00004000
#define EBDMA_CSR_DIS_CSR_DRN	0x00010000
#define EBDMA_CSR_BURST_SZ_MASK	0x000c0000
#define EBDMA_CSR_BURST_SZ_1	0x00080000
#define EBDMA_CSR_BURST_SZ_4	0x00000000
#define EBDMA_CSR_BURST_SZ_8	0x00040000
#define EBDMA_CSR_BURST_SZ_16	0x000c0000
#define EBDMA_CSR_DIAG_EN	0x00100000
#define EBDMA_CSR_DIS_ERR_PEND	0x00400000
#define EBDMA_CSR_TCI_DIS	0x00800000
#define EBDMA_CSR_EN_NEXT	0x01000000
#define EBDMA_CSR_DMA_ON	0x02000000
#define EBDMA_CSR_A_LOADED	0x04000000
#define EBDMA_CSR_NA_LOADED	0x08000000
#define EBDMA_CSR_DEV_ID_MASK	0xf0000000

#define EBUS_DMA_RESET_TIMEOUT	10000

static void __ebus_dma_reset(struct ebus_dma_info *p, int no_drain)
{
	int i;
	u32 val = 0;

	writel(EBDMA_CSR_RESET, p->regs + EBDMA_CSR);
	udelay(1);

	if (no_drain)
		return;

	for (i = EBUS_DMA_RESET_TIMEOUT; i > 0; i--) {
		val = readl(p->regs + EBDMA_CSR);

		if (!(val & (EBDMA_CSR_DRAIN | EBDMA_CSR_CYC_PEND)))
			break;
		udelay(10);
	}
}

static irqreturn_t ebus_dma_irq(int irq, void *dev_id)
{
	struct ebus_dma_info *p = dev_id;
	unsigned long flags;
	u32 csr = 0;

	spin_lock_irqsave(&p->lock, flags);
	csr = readl(p->regs + EBDMA_CSR);
	writel(csr, p->regs + EBDMA_CSR);
	spin_unlock_irqrestore(&p->lock, flags);

	if (csr & EBDMA_CSR_ERR_PEND) {
		printk(KERN_CRIT "ebus_dma(%s): DMA error!\n", p->name);
		p->callback(p, EBUS_DMA_EVENT_ERROR, p->client_cookie);
		return IRQ_HANDLED;
	} else if (csr & EBDMA_CSR_INT_PEND) {
		p->callback(p,
			    (csr & EBDMA_CSR_TC) ?
			    EBUS_DMA_EVENT_DMA : EBUS_DMA_EVENT_DEVICE,
			    p->client_cookie);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;

}

int ebus_dma_register(struct ebus_dma_info *p)
{
	u32 csr;

	if (!p->regs)
		return -EINVAL;
	if (p->flags & ~(EBUS_DMA_FLAG_USE_EBDMA_HANDLER |
			 EBUS_DMA_FLAG_TCI_DISABLE))
		return -EINVAL;
	if ((p->flags & EBUS_DMA_FLAG_USE_EBDMA_HANDLER) && !p->callback)
		return -EINVAL;
	if (!strlen(p->name))
		return -EINVAL;

	__ebus_dma_reset(p, 1);

	csr = EBDMA_CSR_BURST_SZ_16 | EBDMA_CSR_EN_CNT;

	if (p->flags & EBUS_DMA_FLAG_TCI_DISABLE)
		csr |= EBDMA_CSR_TCI_DIS;

	writel(csr, p->regs + EBDMA_CSR);

	return 0;
}
EXPORT_SYMBOL(ebus_dma_register);

int ebus_dma_irq_enable(struct ebus_dma_info *p, int on)
{
	unsigned long flags;
	u32 csr;

	if (on) {
		if (p->flags & EBUS_DMA_FLAG_USE_EBDMA_HANDLER) {
			if (request_irq(p->irq, ebus_dma_irq, IRQF_SHARED, p->name, p))
				return -EBUSY;
		}

		spin_lock_irqsave(&p->lock, flags);
		csr = readl(p->regs + EBDMA_CSR);
		csr |= EBDMA_CSR_INT_EN;
		writel(csr, p->regs + EBDMA_CSR);
		spin_unlock_irqrestore(&p->lock, flags);
	} else {
		spin_lock_irqsave(&p->lock, flags);
		csr = readl(p->regs + EBDMA_CSR);
		csr &= ~EBDMA_CSR_INT_EN;
		writel(csr, p->regs + EBDMA_CSR);
		spin_unlock_irqrestore(&p->lock, flags);

		if (p->flags & EBUS_DMA_FLAG_USE_EBDMA_HANDLER) {
			free_irq(p->irq, p);
		}
	}

	return 0;
}
EXPORT_SYMBOL(ebus_dma_irq_enable);

void ebus_dma_unregister(struct ebus_dma_info *p)
{
	unsigned long flags;
	u32 csr;
	int irq_on = 0;

	spin_lock_irqsave(&p->lock, flags);
	csr = readl(p->regs + EBDMA_CSR);
	if (csr & EBDMA_CSR_INT_EN) {
		csr &= ~EBDMA_CSR_INT_EN;
		writel(csr, p->regs + EBDMA_CSR);
		irq_on = 1;
	}
	spin_unlock_irqrestore(&p->lock, flags);

	if (irq_on)
		free_irq(p->irq, p);
}
EXPORT_SYMBOL(ebus_dma_unregister);

int ebus_dma_request(struct ebus_dma_info *p, dma_addr_t bus_addr, size_t len)
{
	unsigned long flags;
	u32 csr;
	int err;

	if (len >= (1 << 24))
		return -EINVAL;

	spin_lock_irqsave(&p->lock, flags);
	csr = readl(p->regs + EBDMA_CSR);
	err = -EINVAL;
	if (!(csr & EBDMA_CSR_EN_DMA))
		goto out;
	err = -EBUSY;
	if (csr & EBDMA_CSR_NA_LOADED)
		goto out;

	writel(len,      p->regs + EBDMA_COUNT);
	writel(bus_addr, p->regs + EBDMA_ADDR);
	err = 0;

out:
	spin_unlock_irqrestore(&p->lock, flags);

	return err;
}
EXPORT_SYMBOL(ebus_dma_request);

void ebus_dma_prepare(struct ebus_dma_info *p, int write)
{
	unsigned long flags;
	u32 csr;

	spin_lock_irqsave(&p->lock, flags);
	__ebus_dma_reset(p, 0);

	csr = (EBDMA_CSR_INT_EN |
	       EBDMA_CSR_EN_CNT |
	       EBDMA_CSR_BURST_SZ_16 |
	       EBDMA_CSR_EN_NEXT);

	if (write)
		csr |= EBDMA_CSR_WRITE;
	if (p->flags & EBUS_DMA_FLAG_TCI_DISABLE)
		csr |= EBDMA_CSR_TCI_DIS;

	writel(csr, p->regs + EBDMA_CSR);

	spin_unlock_irqrestore(&p->lock, flags);
}
EXPORT_SYMBOL(ebus_dma_prepare);

unsigned int ebus_dma_residue(struct ebus_dma_info *p)
{
	return readl(p->regs + EBDMA_COUNT);
}
EXPORT_SYMBOL(ebus_dma_residue);

unsigned int ebus_dma_addr(struct ebus_dma_info *p)
{
	return readl(p->regs + EBDMA_ADDR);
}
EXPORT_SYMBOL(ebus_dma_addr);

void ebus_dma_enable(struct ebus_dma_info *p, int on)
{
	unsigned long flags;
	u32 orig_csr, csr;

	spin_lock_irqsave(&p->lock, flags);
	orig_csr = csr = readl(p->regs + EBDMA_CSR);
	if (on)
		csr |= EBDMA_CSR_EN_DMA;
	else
		csr &= ~EBDMA_CSR_EN_DMA;
	if ((orig_csr & EBDMA_CSR_EN_DMA) !=
	    (csr & EBDMA_CSR_EN_DMA))
		writel(csr, p->regs + EBDMA_CSR);
	spin_unlock_irqrestore(&p->lock, flags);
}
EXPORT_SYMBOL(ebus_dma_enable);

struct linux_ebus *ebus_chain = NULL;

static inline void *ebus_alloc(size_t size)
{
	void *mem;

	mem = kzalloc(size, GFP_ATOMIC);
	if (!mem)
		panic("ebus_alloc: out of memory");
	return mem;
}

static void __init fill_ebus_child(struct device_node *dp,
				   struct linux_ebus_child *dev,
				   int non_standard_regs)
{
	struct of_device *op;
	const int *regs;
	int i, len;

	dev->prom_node = dp;
	printk(" (%s)", dp->name);

	regs = of_get_property(dp, "reg", &len);
	if (!regs)
		dev->num_addrs = 0;
	else
		dev->num_addrs = len / sizeof(regs[0]);

	if (non_standard_regs) {
		/* This is to handle reg properties which are not
		 * in the parent relative format.  One example are
		 * children of the i2c device on CompactPCI systems.
		 *
		 * So, for such devices we just record the property
		 * raw in the child resources.
		 */
		for (i = 0; i < dev->num_addrs; i++)
			dev->resource[i].start = regs[i];
	} else {
		for (i = 0; i < dev->num_addrs; i++) {
			int rnum = regs[i];
			if (rnum >= dev->parent->num_addrs) {
				prom_printf("UGH: property for %s was %d, need < %d\n",
					    dp->name, len, dev->parent->num_addrs);
				prom_halt();
			}
			dev->resource[i].start = dev->parent->resource[i].start;
			dev->resource[i].end = dev->parent->resource[i].end;
			dev->resource[i].flags = IORESOURCE_MEM;
			dev->resource[i].name = dp->name;
		}
	}

	op = of_find_device_by_node(dp);
	if (!op) {
		dev->num_irqs = 0;
	} else {
		dev->num_irqs = op->num_irqs;
		for (i = 0; i < dev->num_irqs; i++)
			dev->irqs[i] = op->irqs[i];
	}

	if (!dev->num_irqs) {
		/*
		 * Oh, well, some PROMs don't export interrupts
		 * property to children of EBus devices...
		 *
		 * Be smart about PS/2 keyboard and mouse.
		 */
		if (!strcmp(dev->parent->prom_node->name, "8042")) {
			if (!strcmp(dev->prom_node->name, "kb_ps2")) {
				dev->num_irqs = 1;
				dev->irqs[0] = dev->parent->irqs[0];
			} else {
				dev->num_irqs = 1;
				dev->irqs[0] = dev->parent->irqs[1];
			}
		}
	}
}

static int __init child_regs_nonstandard(struct linux_ebus_device *dev)
{
	if (!strcmp(dev->prom_node->name, "i2c") ||
	    !strcmp(dev->prom_node->name, "SUNW,lombus"))
		return 1;
	return 0;
}

static void __init fill_ebus_device(struct device_node *dp, struct linux_ebus_device *dev)
{
	struct linux_ebus_child *child;
	struct dev_archdata *sd;
	struct of_device *op;
	int i, len;

	dev->prom_node = dp;

	printk(" [%s", dp->name);

	op = of_find_device_by_node(dp);
	if (!op) {
		dev->num_addrs = 0;
		dev->num_irqs = 0;
	} else {
		const int *regs = of_get_property(dp, "reg", &len);

		if (!regs)
			len = 0;
		dev->num_addrs = len / sizeof(struct linux_prom_registers);

		for (i = 0; i < dev->num_addrs; i++)
			memcpy(&dev->resource[i],
			       &op->resource[i],
			       sizeof(struct resource));

		dev->num_irqs = op->num_irqs;
		for (i = 0; i < dev->num_irqs; i++)
			dev->irqs[i] = op->irqs[i];
	}

	sd = &dev->ofdev.dev.archdata;
	sd->prom_node = dp;
	sd->op = &dev->ofdev;
	sd->iommu = dev->bus->ofdev.dev.parent->archdata.iommu;
	sd->stc = dev->bus->ofdev.dev.parent->archdata.stc;
	sd->numa_node = dev->bus->ofdev.dev.parent->archdata.numa_node;

	dev->ofdev.node = dp;
	dev->ofdev.dev.parent = &dev->bus->ofdev.dev;
	dev->ofdev.dev.bus = &ebus_bus_type;
	dev_set_name(&dev->ofdev.dev, "ebus[%08x]", dp->node);

	/* Register with core */
	if (of_device_register(&dev->ofdev) != 0)
		printk(KERN_DEBUG "ebus: device registration error for %s!\n",
		       dp->path_component_name);

	dp = dp->child;
	if (dp) {
		printk(" ->");
		dev->children = ebus_alloc(sizeof(struct linux_ebus_child));

		child = dev->children;
		child->next = NULL;
		child->parent = dev;
		child->bus = dev->bus;
		fill_ebus_child(dp, child,
				child_regs_nonstandard(dev));

		while ((dp = dp->sibling) != NULL) {
			child->next = ebus_alloc(sizeof(struct linux_ebus_child));

			child = child->next;
			child->next = NULL;
			child->parent = dev;
			child->bus = dev->bus;
			fill_ebus_child(dp, child,
					child_regs_nonstandard(dev));
		}
	}
	printk("]");
}

static struct pci_dev *find_next_ebus(struct pci_dev *start, int *is_rio_p)
{
	struct pci_dev *pdev = start;

	while ((pdev = pci_get_device(PCI_VENDOR_ID_SUN, PCI_ANY_ID, pdev)))
		if (pdev->device == PCI_DEVICE_ID_SUN_EBUS ||
			pdev->device == PCI_DEVICE_ID_SUN_RIO_EBUS)
			break;

	*is_rio_p = !!(pdev && (pdev->device == PCI_DEVICE_ID_SUN_RIO_EBUS));

	return pdev;
}

void __init ebus_init(void)
{
	struct linux_ebus_device *dev;
	struct linux_ebus *ebus;
	struct pci_dev *pdev;
	struct device_node *dp;
	int is_rio;
	int num_ebus = 0;

	pdev = find_next_ebus(NULL, &is_rio);
	if (!pdev) {
		printk("ebus: No EBus's found.\n");
		return;
	}

	dp = pci_device_to_OF_node(pdev);

	ebus_chain = ebus = ebus_alloc(sizeof(struct linux_ebus));
	ebus->next = NULL;
	ebus->is_rio = is_rio;

	while (dp) {
		struct device_node *child;

		/* SUNW,pci-qfe uses four empty ebuses on it.
		   I think we should not consider them here,
		   as they have half of the properties this
		   code expects and once we do PCI hot-plug,
		   we'd have to tweak with the ebus_chain
		   in the runtime after initialization. -jj */
		if (!dp->child) {
			pdev = find_next_ebus(pdev, &is_rio);
			if (!pdev) {
				if (ebus == ebus_chain) {
					ebus_chain = NULL;
					printk("ebus: No EBus's found.\n");
					return;
				}
				break;
			}
			ebus->is_rio = is_rio;
			dp = pci_device_to_OF_node(pdev);
			continue;
		}
		printk("ebus%d:", num_ebus);

		ebus->index = num_ebus;
		ebus->prom_node = dp;
		ebus->self = pdev;

		ebus->ofdev.node = dp;
		ebus->ofdev.dev.parent = &pdev->dev;
		ebus->ofdev.dev.bus = &ebus_bus_type;
		dev_set_name(&ebus->ofdev.dev, "ebus%d", num_ebus);

		/* Register with core */
		if (of_device_register(&ebus->ofdev) != 0)
			printk(KERN_DEBUG "ebus: device registration error for %s!\n",
			       dp->path_component_name);


		child = dp->child;
		if (!child)
			goto next_ebus;

		ebus->devices = ebus_alloc(sizeof(struct linux_ebus_device));

		dev = ebus->devices;
		dev->next = NULL;
		dev->children = NULL;
		dev->bus = ebus;
		fill_ebus_device(child, dev);

		while ((child = child->sibling) != NULL) {
			dev->next = ebus_alloc(sizeof(struct linux_ebus_device));

			dev = dev->next;
			dev->next = NULL;
			dev->children = NULL;
			dev->bus = ebus;
			fill_ebus_device(child, dev);
		}

	next_ebus:
		printk("\n");

		pdev = find_next_ebus(pdev, &is_rio);
		if (!pdev)
			break;

		dp = pci_device_to_OF_node(pdev);

		ebus->next = ebus_alloc(sizeof(struct linux_ebus));
		ebus = ebus->next;
		ebus->next = NULL;
		ebus->is_rio = is_rio;
		++num_ebus;
	}
	pci_dev_put(pdev); /* XXX for the case, when ebusnd is 0, is it OK? */
}
