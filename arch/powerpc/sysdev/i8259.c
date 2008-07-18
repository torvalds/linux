/*
 * i8259 interrupt controller driver.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#undef DEBUG

#include <linux/init.h>
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

static DEFINE_SPINLOCK(i8259_lock);

static struct irq_host *i8259_host;

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
		spin_lock(&i8259_lock);
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
			irq = NO_IRQ;
	} else if (irq == 0xff)
		irq = NO_IRQ;

	if (lock)
		spin_unlock(&i8259_lock);
	return irq;
}

static void i8259_mask_and_ack_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i8259_lock, flags);
	if (irq_nr > 7) {
		cached_A1 |= 1 << (irq_nr-8);
		inb(0xA1); 	/* DUMMY */
		outb(cached_A1, 0xA1);
		outb(0x20, 0xA0);	/* Non-specific EOI */
		outb(0x20, 0x20);	/* Non-specific EOI to cascade */
	} else {
		cached_21 |= 1 << irq_nr;
		inb(0x21); 	/* DUMMY */
		outb(cached_21, 0x21);
		outb(0x20, 0x20);	/* Non-specific EOI */
	}
	spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_set_irq_mask(int irq_nr)
{
	outb(cached_A1,0xA1);
	outb(cached_21,0x21);
}

static void i8259_mask_irq(unsigned int irq_nr)
{
	unsigned long flags;

	pr_debug("i8259_mask_irq(%d)\n", irq_nr);

	spin_lock_irqsave(&i8259_lock, flags);
	if (irq_nr < 8)
		cached_21 |= 1 << irq_nr;
	else
		cached_A1 |= 1 << (irq_nr-8);
	i8259_set_irq_mask(irq_nr);
	spin_unlock_irqrestore(&i8259_lock, flags);
}

static void i8259_unmask_irq(unsigned int irq_nr)
{
	unsigned long flags;

	pr_debug("i8259_unmask_irq(%d)\n", irq_nr);

	spin_lock_irqsave(&i8259_lock, flags);
	if (irq_nr < 8)
		cached_21 &= ~(1 << irq_nr);
	else
		cached_A1 &= ~(1 << (irq_nr-8));
	i8259_set_irq_mask(irq_nr);
	spin_unlock_irqrestore(&i8259_lock, flags);
}

static struct irq_chip i8259_pic = {
	.typename	= " i8259    ",
	.mask		= i8259_mask_irq,
	.disable	= i8259_mask_irq,
	.unmask		= i8259_unmask_irq,
	.mask_ack	= i8259_mask_and_ack_irq,
};

static struct resource pic1_iores = {
	.name = "8259 (master)",
	.start = 0x20,
	.end = 0x21,
	.flags = IORESOURCE_BUSY,
};

static struct resource pic2_iores = {
	.name = "8259 (slave)",
	.start = 0xa0,
	.end = 0xa1,
	.flags = IORESOURCE_BUSY,
};

static struct resource pic_edgectrl_iores = {
	.name = "8259 edge control",
	.start = 0x4d0,
	.end = 0x4d1,
	.flags = IORESOURCE_BUSY,
};

static int i8259_host_match(struct irq_host *h, struct device_node *node)
{
	return h->of_node == NULL || h->of_node == node;
}

static int i8259_host_map(struct irq_host *h, unsigned int virq,
			  irq_hw_number_t hw)
{
	pr_debug("i8259_host_map(%d, 0x%lx)\n", virq, hw);

	/* We block the internal cascade */
	if (hw == 2)
		get_irq_desc(virq)->status |= IRQ_NOREQUEST;

	/* We use the level handler only for now, we might want to
	 * be more cautious here but that works for now
	 */
	get_irq_desc(virq)->status |= IRQ_LEVEL;
	set_irq_chip_and_handler(virq, &i8259_pic, handle_level_irq);
	return 0;
}

static void i8259_host_unmap(struct irq_host *h, unsigned int virq)
{
	/* Make sure irq is masked in hardware */
	i8259_mask_irq(virq);

	/* remove chip and handler */
	set_irq_chip_and_handler(virq, NULL, NULL);

	/* Make sure it's completed */
	synchronize_irq(virq);
}

static int i8259_host_xlate(struct irq_host *h, struct device_node *ct,
			    u32 *intspec, unsigned int intsize,
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

static struct irq_host_ops i8259_host_ops = {
	.match = i8259_host_match,
	.map = i8259_host_map,
	.unmap = i8259_host_unmap,
	.xlate = i8259_host_xlate,
};

struct irq_host *i8259_get_host(void)
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
	spin_lock_irqsave(&i8259_lock, flags);

	/* Mask all first */
	outb(0xff, 0xA1);
	outb(0xff, 0x21);

	/* init master interrupt controller */
	outb(0x11, 0x20); /* Start init sequence */
	outb(0x00, 0x21); /* Vector base */
	outb(0x04, 0x21); /* edge tiggered, Cascade (slave) on IRQ2 */
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

	spin_unlock_irqrestore(&i8259_lock, flags);

	/* create a legacy host */
	i8259_host = irq_alloc_host(node, IRQ_HOST_MAP_LEGACY,
				    0, &i8259_host_ops, 0);
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
