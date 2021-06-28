// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * i8259 interrupt controller driver.
 */
#undef DEBUG

#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/i8259.h>
#include <asm/prom.h>

static volatile void __iomem *pci_intack; /* RO, gives us the irq vector */

static unsigned char cached_8259[2] = { 0xff, 0xff };
#define cached_A1 (cached_8259[0])
#define cached_21 (cached_8259[1])

static DEFINE_RAW_SPINLOCK(i8259_lock);

static struct irq_domain *i8259_host;

/*
 * Acknowledge the IRQ using either the PCI host bridge's interrupt
 * acknowledge feature or poll.  How i8259_init() is called determines
 * which is called.  It should be noted that polling is broken on some
 * IBM and Motorola PReP boxes so we must use the int-ack feature on them.
 */
unsigned int i8259_irq(void)
{
	int irq;
	int lock = 0;

	/* Either int-ack or poll for the IRQ */
	if (pci_intack)
		irq = readb(pci_intack);
	else {
		raw_spin_lock(&i8259_lock);
		lock = 1;

		/* Perform an interrupt acknowledge cycle on controller 1. */
		outb(0x0C, 0x20);		/* prepare for poll */
		irq = inb(0x20) & 7;
		if (irq == 2 ) {
			/*
			 * Interrupt is cascaded so perform interrupt
			 * acknowledge on controller 2.
			 */
			outb(0x0C, 0xA0);	/* prepare for poll */
			irq = (inb(0xA0) & 7) + 8;
		}
	}

	if (irq == 7) {
		/*
		 * This may be a spurious interrupt.
		 *
		 * Read the interrupt status register (ISR). If the most
		 * significant bit is not set then there is no valid
		 * interrupt.
		 */
		if (!pci_intack)
			outb(0x0B, 0x20);	/* ISR register */
		if(~inb(0x20) & 0x80)
			irq = 0;
	} else if (irq == 0xff)
		irq = 0;

	if (lock)
		raw_spin_unlock(&i8259_lock);
	return irq;
}

static void i8259_mask_and_ack_irq(struct irq_data *d)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&i8259_lock, flags);
	if (d->irq > 7) {
		cached_A1 |= 1 << (d->irq-8);
		inb(0xA1); 	/* DUMMY */
		outb(cached_A1, 0xA1);
		outb(0x20, 0xA0);	/* Non-specific EOI */
		outb(0x20, 0x20);	/* Non-specific EOI to cascade */
	} else {
		cached_21 |= 1 << d->irq;
		inb(0x21); 	/* DUMMY */
		outb(cached_21, 0x21);
		outb(0x20, 0x20);	/* Non-specific EOI */
	}
	raw_spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_set_irq_mask(int irq_nr)
{
	outb(cached_A1,0xA1);
	outb(cached_21,0x21);
}

static void i8259_mask_irq(struct irq_data *d)
{
	unsigned long flags;

	pr_debug("i8259_mask_irq(%d)\n", d->irq);

	raw_spin_lock_irqsave(&i8259_lock, flags);
	if (d->irq < 8)
		cached_21 |= 1 << d->irq;
	else
		cached_A1 |= 1 << (d->irq-8);
	i8259_set_irq_mask(d->irq);
	raw_spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_unmask_irq(struct irq_data *d)
{
	unsigned long flags;

	pr_debug("i8259_unmask_irq(%d)\n", d->irq);

	raw_spin_lock_irqsave(&i8259_lock, flags);
	if (d->irq < 8)
		cached_21 &= ~(1 << d->irq);
	else
		cached_A1 &= ~(1 << (d->irq-8));
	i8259_set_irq_mask(d->irq);
	raw_spin_unlock_irqrestore(&i8259_lock, flags);
}

static struct irq_chip i8259_pic = {
	.name		= "i8259",
	.irq_mask	= i8259_mask_irq,
	.irq_disable	= i8259_mask_irq,
	.irq_unmask	= i8259_unmask_irq,
	.irq_mask_ack	= i8259_mask_and_ack_irq,
};

static struct resource pic1_iores = {
	.name = "8259 (master)",
	.start = 0x20,
	.end = 0x21,
	.flags = IORESOURCE_IO | IORESOURCE_BUSY,
};

static struct resource pic2_iores = {
	.name = "8259 (slave)",
	.start = 0xa0,
	.end = 0xa1,
	.flags = IORESOURCE_IO | IORESOURCE_BUSY,
};

static struct resource pic_edgectrl_iores = {
	.name = "8259 edge control",
	.start = 0x4d0,
	.end = 0x4d1,
	.flags = IORESOURCE_IO | IORESOURCE_BUSY,
};

static int i8259_host_match(struct irq_domain *h, struct device_node *node,
			    enum irq_domain_bus_token bus_token)
{
	struct device_node *of_node = irq_domain_get_of_node(h);
	return of_node == NULL || of_node == node;
}

static int i8259_host_map(struct irq_domain *h, unsigned int virq,
			  irq_hw_number_t hw)
{
	pr_debug("i8259_host_map(%d, 0x%lx)\n", virq, hw);

	/* We block the internal cascade */
	if (hw == 2)
		irq_set_status_flags(virq, IRQ_NOREQUEST);

	/* We use the level handler only for now, we might want to
	 * be more cautious here but that works for now
	 */
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &i8259_pic, handle_level_irq);
	return 0;
}

static int i8259_host_xlate(struct irq_domain *h, struct device_node *ct,
			    const u32 *intspec, unsigned int intsize,
			    irq_hw_number_t *out_hwirq, unsigned int *out_flags)
{
	static unsigned char map_isa_senses[4] = {
		IRQ_TYPE_LEVEL_LOW,
		IRQ_TYPE_LEVEL_HIGH,
		IRQ_TYPE_EDGE_FALLING,
		IRQ_TYPE_EDGE_RISING,
	};

	*out_hwirq = intspec[0];
	if (intsize > 1 && intspec[1] < 4)
		*out_flags = map_isa_senses[intspec[1]];
	else
		*out_flags = IRQ_TYPE_NONE;

	return 0;
}

static const struct irq_domain_ops i8259_host_ops = {
	.match = i8259_host_match,
	.map = i8259_host_map,
	.xlate = i8259_host_xlate,
};

struct irq_domain *i8259_get_host(void)
{
	return i8259_host;
}

/**
 * i8259_init - Initialize the legacy controller
 * @node: device node of the legacy PIC (can be NULL, but then, it will match
 *        all interrupts, so beware)
 * @intack_addr: PCI interrupt acknowledge (real) address which will return
 *             	 the active irq from the 8259
 */
void i8259_init(struct device_node *node, unsigned long intack_addr)
{
	unsigned long flags;

	/* initialize the controller */
	raw_spin_lock_irqsave(&i8259_lock, flags);

	/* Mask all first */
	outb(0xff, 0xA1);
	outb(0xff, 0x21);

	/* init master interrupt controller */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x00, 0x21); /* Vector base */
	outb(0x04, 0x21); /* edge triggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0x21); /* Select 8086 mode */

	/* init slave interrupt controller */
	outb(0x11, 0xA0); /* Start init sequence */
	outb(0x08, 0xA1); /* Vector base */
	outb(0x02, 0xA1); /* edge triggered, Cascade (slave) on IRQ2 */
	outb(0x01, 0xA1); /* Select 8086 mode */

	/* That thing is slow */
	udelay(100);

	/* always read ISR */
	outb(0x0B, 0x20);
	outb(0x0B, 0xA0);

	/* Unmask the internal cascade */
	cached_21 &= ~(1 << 2);

	/* Set interrupt masks */
	outb(cached_A1, 0xA1);
	outb(cached_21, 0x21);

	raw_spin_unlock_irqrestore(&i8259_lock, flags);

	/* create a legacy host */
	i8259_host = irq_domain_add_legacy(node, NR_IRQS_LEGACY, 0, 0,
					   &i8259_host_ops, NULL);
	if (i8259_host == NULL) {
		printk(KERN_ERR "i8259: failed to allocate irq host !\n");
		return;
	}

	/* reserve our resources */
	/* XXX should we continue doing that ? it seems to cause problems
	 * with further requesting of PCI IO resources for that range...
	 * need to look into it.
	 */
	request_resource(&ioport_resource, &pic1_iores);
	request_resource(&ioport_resource, &pic2_iores);
	request_resource(&ioport_resource, &pic_edgectrl_iores);

	if (intack_addr != 0)
		pci_intack = ioremap(intack_addr, 1);

	printk(KERN_INFO "i8259 legacy interrupt controller initialized\n");
}
