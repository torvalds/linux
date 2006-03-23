/* $Id: ebus.c,v 1.64 2001/11/08 04:41:33 davem Exp $
 * ebus.c: PCI to EBus bridge device.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1999  David S. Miller (davem@redhat.com)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/page.h>
#include <asm/pbm.h>
#include <asm/ebus.h>
#include <asm/oplib.h>
#include <asm/bpp.h>
#include <asm/irq.h>

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

static irqreturn_t ebus_dma_irq(int irq, void *dev_id, struct pt_regs *regs)
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
			if (request_irq(p->irq, ebus_dma_irq, SA_SHIRQ, p->name, p))
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

#ifdef CONFIG_SUN_AUXIO
extern void auxio_probe(void);
#endif

static inline void *ebus_alloc(size_t size)
{
	void *mem;

	mem = kzalloc(size, GFP_ATOMIC);
	if (!mem)
		panic("ebus_alloc: out of memory");
	return mem;
}

static void __init ebus_ranges_init(struct linux_ebus *ebus)
{
	int success;

	ebus->num_ebus_ranges = 0;
	success = prom_getproperty(ebus->prom_node, "ranges",
				   (char *)ebus->ebus_ranges,
				   sizeof(ebus->ebus_ranges));
	if (success != -1)
		ebus->num_ebus_ranges = (success/sizeof(struct linux_prom_ebus_ranges));
}

static void __init ebus_intmap_init(struct linux_ebus *ebus)
{
	int success;

	ebus->num_ebus_intmap = 0;
	success = prom_getproperty(ebus->prom_node, "interrupt-map",
				   (char *)ebus->ebus_intmap,
				   sizeof(ebus->ebus_intmap));
	if (success == -1)
		return;

	ebus->num_ebus_intmap = (success/sizeof(struct linux_prom_ebus_intmap));

	success = prom_getproperty(ebus->prom_node, "interrupt-map-mask",
				   (char *)&ebus->ebus_intmask,
				   sizeof(ebus->ebus_intmask));
	if (success == -1) {
		prom_printf("%s: can't get interrupt-map-mask\n", __FUNCTION__);
		prom_halt();
	}
}

int __init ebus_intmap_match(struct linux_ebus *ebus,
			     struct linux_prom_registers *reg,
			     int *interrupt)
{
	unsigned int hi, lo, irq;
	int i;

	if (!ebus->num_ebus_intmap)
		return 0;

	hi = reg->which_io & ebus->ebus_intmask.phys_hi;
	lo = reg->phys_addr & ebus->ebus_intmask.phys_lo;
	irq = *interrupt & ebus->ebus_intmask.interrupt;
	for (i = 0; i < ebus->num_ebus_intmap; i++) {
		if ((ebus->ebus_intmap[i].phys_hi == hi) &&
		    (ebus->ebus_intmap[i].phys_lo == lo) &&
		    (ebus->ebus_intmap[i].interrupt == irq)) {
			*interrupt = ebus->ebus_intmap[i].cinterrupt;
			return 0;
		}
	}
	return -1;
}

void __init fill_ebus_child(int node, struct linux_prom_registers *preg,
			    struct linux_ebus_child *dev, int non_standard_regs)
{
	int regs[PROMREG_MAX];
	int irqs[PROMREG_MAX];
	int i, len;

	dev->prom_node = node;
	prom_getstring(node, "name", dev->prom_name, sizeof(dev->prom_name));
	printk(" (%s)", dev->prom_name);

	len = prom_getproperty(node, "reg", (void *)regs, sizeof(regs));
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
					    dev->prom_name, len, dev->parent->num_addrs);
				panic(__FUNCTION__);
			}
			dev->resource[i].start = dev->parent->resource[i].start;
			dev->resource[i].end = dev->parent->resource[i].end;
			dev->resource[i].flags = IORESOURCE_MEM;
			dev->resource[i].name = dev->prom_name;
		}
	}

	for (i = 0; i < PROMINTR_MAX; i++)
		dev->irqs[i] = PCI_IRQ_NONE;

	len = prom_getproperty(node, "interrupts", (char *)&irqs, sizeof(irqs));
	if ((len == -1) || (len == 0)) {
		dev->num_irqs = 0;
		/*
		 * Oh, well, some PROMs don't export interrupts
		 * property to children of EBus devices...
		 *
		 * Be smart about PS/2 keyboard and mouse.
		 */
		if (!strcmp(dev->parent->prom_name, "8042")) {
			if (!strcmp(dev->prom_name, "kb_ps2")) {
				dev->num_irqs = 1;
				dev->irqs[0] = dev->parent->irqs[0];
			} else {
				dev->num_irqs = 1;
				dev->irqs[0] = dev->parent->irqs[1];
			}
		}
	} else {
		dev->num_irqs = len / sizeof(irqs[0]);
		for (i = 0; i < dev->num_irqs; i++) {
			struct pci_pbm_info *pbm = dev->bus->parent;
			struct pci_controller_info *p = pbm->parent;

			if (ebus_intmap_match(dev->bus, preg, &irqs[i]) != -1) {
				dev->irqs[i] = p->irq_build(pbm,
							    dev->bus->self,
							    irqs[i]);
			} else {
				/* If we get a bogus interrupt property, just
				 * record the raw value instead of punting.
				 */
				dev->irqs[i] = irqs[i];
			}
		}
	}
}

static int __init child_regs_nonstandard(struct linux_ebus_device *dev)
{
	if (!strcmp(dev->prom_name, "i2c") ||
	    !strcmp(dev->prom_name, "SUNW,lombus"))
		return 1;
	return 0;
}

void __init fill_ebus_device(int node, struct linux_ebus_device *dev)
{
	struct linux_prom_registers regs[PROMREG_MAX];
	struct linux_ebus_child *child;
	int irqs[PROMINTR_MAX];
	int i, n, len;

	dev->prom_node = node;
	prom_getstring(node, "name", dev->prom_name, sizeof(dev->prom_name));
	printk(" [%s", dev->prom_name);

	len = prom_getproperty(node, "reg", (void *)regs, sizeof(regs));
	if (len == -1) {
		dev->num_addrs = 0;
		goto probe_interrupts;
	}

	if (len % sizeof(struct linux_prom_registers)) {
		prom_printf("UGH: proplen for %s was %d, need multiple of %d\n",
			    dev->prom_name, len,
			    (int)sizeof(struct linux_prom_registers));
		prom_halt();
	}
	dev->num_addrs = len / sizeof(struct linux_prom_registers);

	for (i = 0; i < dev->num_addrs; i++) {
		/* XXX Learn how to interpret ebus ranges... -DaveM */
		if (regs[i].which_io >= 0x10)
			n = (regs[i].which_io - 0x10) >> 2;
		else
			n = regs[i].which_io;

		dev->resource[i].start  = dev->bus->self->resource[n].start;
		dev->resource[i].start += (unsigned long)regs[i].phys_addr;
		dev->resource[i].end    =
			(dev->resource[i].start + (unsigned long)regs[i].reg_size - 1UL);
		dev->resource[i].flags  = IORESOURCE_MEM;
		dev->resource[i].name   = dev->prom_name;
		request_resource(&dev->bus->self->resource[n],
				 &dev->resource[i]);
	}

probe_interrupts:
	for (i = 0; i < PROMINTR_MAX; i++)
		dev->irqs[i] = PCI_IRQ_NONE;

	len = prom_getproperty(node, "interrupts", (char *)&irqs, sizeof(irqs));
	if ((len == -1) || (len == 0)) {
		dev->num_irqs = 0;
	} else {
		dev->num_irqs = len / sizeof(irqs[0]);
		for (i = 0; i < dev->num_irqs; i++) {
			struct pci_pbm_info *pbm = dev->bus->parent;
			struct pci_controller_info *p = pbm->parent;

			if (ebus_intmap_match(dev->bus, &regs[0], &irqs[i]) != -1) {
				dev->irqs[i] = p->irq_build(pbm,
							    dev->bus->self,
							    irqs[i]);
			} else {
				/* If we get a bogus interrupt property, just
				 * record the raw value instead of punting.
				 */
				dev->irqs[i] = irqs[i];
			}
		}
	}

	if ((node = prom_getchild(node))) {
		printk(" ->");
		dev->children = ebus_alloc(sizeof(struct linux_ebus_child));

		child = dev->children;
		child->next = NULL;
		child->parent = dev;
		child->bus = dev->bus;
		fill_ebus_child(node, &regs[0],
				child, child_regs_nonstandard(dev));

		while ((node = prom_getsibling(node)) != 0) {
			child->next = ebus_alloc(sizeof(struct linux_ebus_child));

			child = child->next;
			child->next = NULL;
			child->parent = dev;
			child->bus = dev->bus;
			fill_ebus_child(node, &regs[0],
					child, child_regs_nonstandard(dev));
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
	struct pci_pbm_info *pbm;
	struct linux_ebus_device *dev;
	struct linux_ebus *ebus;
	struct pci_dev *pdev;
	struct pcidev_cookie *cookie;
	int nd, ebusnd, is_rio;
	int num_ebus = 0;

	pdev = find_next_ebus(NULL, &is_rio);
	if (!pdev) {
		printk("ebus: No EBus's found.\n");
		return;
	}

	cookie = pdev->sysdata;
	ebusnd = cookie->prom_node;

	ebus_chain = ebus = ebus_alloc(sizeof(struct linux_ebus));
	ebus->next = NULL;
	ebus->is_rio = is_rio;

	while (ebusnd) {
		/* SUNW,pci-qfe uses four empty ebuses on it.
		   I think we should not consider them here,
		   as they have half of the properties this
		   code expects and once we do PCI hot-plug,
		   we'd have to tweak with the ebus_chain
		   in the runtime after initialization. -jj */
		if (!prom_getchild (ebusnd)) {
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
			cookie = pdev->sysdata;
			ebusnd = cookie->prom_node;
			continue;
		}
		printk("ebus%d:", num_ebus);

		prom_getstring(ebusnd, "name", ebus->prom_name, sizeof(ebus->prom_name));
		ebus->index = num_ebus;
		ebus->prom_node = ebusnd;
		ebus->self = pdev;
		ebus->parent = pbm = cookie->pbm;

		ebus_ranges_init(ebus);
		ebus_intmap_init(ebus);

		nd = prom_getchild(ebusnd);
		if (!nd)
			goto next_ebus;

		ebus->devices = ebus_alloc(sizeof(struct linux_ebus_device));

		dev = ebus->devices;
		dev->next = NULL;
		dev->children = NULL;
		dev->bus = ebus;
		fill_ebus_device(nd, dev);

		while ((nd = prom_getsibling(nd)) != 0) {
			dev->next = ebus_alloc(sizeof(struct linux_ebus_device));

			dev = dev->next;
			dev->next = NULL;
			dev->children = NULL;
			dev->bus = ebus;
			fill_ebus_device(nd, dev);
		}

	next_ebus:
		printk("\n");

		pdev = find_next_ebus(pdev, &is_rio);
		if (!pdev)
			break;

		cookie = pdev->sysdata;
		ebusnd = cookie->prom_node;

		ebus->next = ebus_alloc(sizeof(struct linux_ebus));
		ebus = ebus->next;
		ebus->next = NULL;
		ebus->is_rio = is_rio;
		++num_ebus;
	}
	pci_dev_put(pdev); /* XXX for the case, when ebusnd is 0, is it OK? */

#ifdef CONFIG_SUN_AUXIO
	auxio_probe();
#endif
}
