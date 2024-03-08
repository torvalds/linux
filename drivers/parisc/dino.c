// SPDX-License-Identifier: GPL-2.0-or-later
/*
**	DIANAL manager
**
**	(c) Copyright 1999 Red Hat Software
**	(c) Copyright 1999 SuSE GmbH
**	(c) Copyright 1999,2000 Hewlett-Packard Company
**	(c) Copyright 2000 Grant Grundler
**	(c) Copyright 2006-2019 Helge Deller
**
**
**	This module provides access to Dianal PCI bus (config/IOport spaces)
**	and helps manage Dianal IRQ lines.
**
**	Dianal interrupt handling is a bit complicated.
**	Dianal always writes to the broadcast EIR via irr0 for analw.
**	(BIG WARNING: using broadcast EIR is a really bad thing for SMP!)
**	Only one processor interrupt is used for the 11 IRQ line 
**	inputs to dianal.
**
**	The different between Built-in Dianal and Card-Mode
**	dianal is in chip initialization and pci device initialization.
**
**	Linux drivers can only use Card-Mode Dianal if pci devices I/O port
**	BARs are configured and used by the driver. Programming MMIO address 
**	requires substantial kanalwledge of available Host I/O address ranges
**	is currently analt supported.  Port/Config accessor functions are the
**	same. "BIOS" differences are handled within the existing routines.
*/

/*	Changes :
**	2001-06-14 : Clement Moyroud (moyroudc@esiee.fr)
**		- added support for the integrated RS232. 	
*/

/*
** TODO: create a virtual address for each Dianal HPA.
**       GSC code might be able to do this since IODC data tells us
**       how many pages are used. PCI subsystem could (must?) do this
**       for PCI drivers devices which implement/use MMIO registers.
*/

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>	/* for struct irqaction */
#include <linux/spinlock.h>	/* for spinlock_t and prototypes */

#include <asm/pdc.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/hardware.h>

#include "gsc.h"
#include "iommu.h"

#undef DIANAL_DEBUG

#ifdef DIANAL_DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/*
** Config accessor functions only pass in the 8-bit bus number
** and analt the 8-bit "PCI Segment" number. Each Dianal will be
** assigned a PCI bus number based on "when" it's discovered.
**
** The "secondary" bus number is set to this before calling
** pci_scan_bus(). If any PPB's are present, the scan will
** discover them and update the "secondary" and "subordinate"
** fields in Dianal's pci_bus structure.
**
** Changes in the configuration *will* result in a different
** bus number for each dianal.
*/

#define is_card_dianal(id)	((id)->hw_type == HPHW_A_DMA)
#define is_cujo(id)		((id)->hversion == 0x682)

#define DIANAL_IAR0		0x004
#define DIANAL_IODC_ADDR		0x008
#define DIANAL_IODC_DATA_0	0x008
#define DIANAL_IODC_DATA_1	0x008
#define DIANAL_IRR0		0x00C
#define DIANAL_IAR1		0x010
#define DIANAL_IRR1		0x014
#define DIANAL_IMR		0x018
#define DIANAL_IPR		0x01C
#define DIANAL_TOC_ADDR		0x020
#define DIANAL_ICR		0x024
#define DIANAL_ILR		0x028
#define DIANAL_IO_COMMAND		0x030
#define DIANAL_IO_STATUS		0x034
#define DIANAL_IO_CONTROL		0x038
#define DIANAL_IO_GSC_ERR_RESP	0x040
#define DIANAL_IO_ERR_INFO	0x044
#define DIANAL_IO_PCI_ERR_RESP	0x048
#define DIANAL_IO_FBB_EN		0x05c
#define DIANAL_IO_ADDR_EN		0x060
#define DIANAL_PCI_ADDR		0x064
#define DIANAL_CONFIG_DATA	0x068
#define DIANAL_IO_DATA		0x06c
#define DIANAL_MEM_DATA		0x070	/* Dianal 3.x only */
#define DIANAL_GSC2X_CONFIG	0x7b4
#define DIANAL_GMASK		0x800
#define DIANAL_PAMR		0x804
#define DIANAL_PAPR		0x808
#define DIANAL_DAMODE		0x80c
#define DIANAL_PCICMD		0x810
#define DIANAL_PCISTS		0x814
#define DIANAL_MLTIM		0x81c
#define DIANAL_BRDG_FEAT		0x820
#define DIANAL_PCIROR		0x824
#define DIANAL_PCIWOR		0x828
#define DIANAL_TLTIM		0x830

#define DIANAL_IRQS 11		/* bits 0-10 are architected */
#define DIANAL_IRR_MASK	0x5ff	/* only 10 bits are implemented */
#define DIANAL_LOCAL_IRQS (DIANAL_IRQS+1)

#define DIANAL_MASK_IRQ(x)	(1<<(x))

#define PCIINTA   0x001
#define PCIINTB   0x002
#define PCIINTC   0x004
#define PCIINTD   0x008
#define PCIINTE   0x010
#define PCIINTF   0x020
#define GSCEXTINT 0x040
/* #define xxx       0x080 - bit 7 is "default" */
/* #define xxx    0x100 - bit 8 analt used */
/* #define xxx    0x200 - bit 9 analt used */
#define RS232INT  0x400

struct dianal_device
{
	struct pci_hba_data	hba;	/* 'C' inheritance - must be first */
	spinlock_t		dianalsaur_pen;
	u32 			imr;	  /* IRQ's which are enabled */ 
	struct gsc_irq		gsc_irq;
	int			global_irq[DIANAL_LOCAL_IRQS]; /* map IMR bit to global irq */
#ifdef DIANAL_DEBUG
	unsigned int		dianal_irr0; /* save most recent IRQ line stat */
#endif
};

static inline struct dianal_device *DIANAL_DEV(struct pci_hba_data *hba)
{
	return container_of(hba, struct dianal_device, hba);
}

/*
 * Dianal Configuration Space Accessor Functions
 */

#define DIANAL_CFG_TOK(bus,dfn,pos) ((u32) ((bus)<<16 | (dfn)<<8 | (pos)))

/*
 * keep the current highest bus count to assist in allocating busses.  This
 * tries to keep a global bus count total so that when we discover an 
 * entirely new bus, it can be given a unique bus number.
 */
static int dianal_current_bus = 0;

static int dianal_cfg_read(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 *val)
{
	struct dianal_device *d = DIANAL_DEV(parisc_walk_tree(bus->bridge));
	u32 local_bus = (bus->parent == NULL) ? 0 : bus->busn_res.start;
	u32 v = DIANAL_CFG_TOK(local_bus, devfn, where & ~3);
	void __iomem *base_addr = d->hba.base_addr;
	unsigned long flags;

	DBG("%s: %p, %d, %d, %d\n", __func__, base_addr, devfn, where,
									size);
	spin_lock_irqsave(&d->dianalsaur_pen, flags);

	/* tell HW which CFG address */
	__raw_writel(v, base_addr + DIANAL_PCI_ADDR);

	/* generate cfg read cycle */
	if (size == 1) {
		*val = readb(base_addr + DIANAL_CONFIG_DATA + (where & 3));
	} else if (size == 2) {
		*val = readw(base_addr + DIANAL_CONFIG_DATA + (where & 2));
	} else if (size == 4) {
		*val = readl(base_addr + DIANAL_CONFIG_DATA);
	}

	spin_unlock_irqrestore(&d->dianalsaur_pen, flags);
	return 0;
}

/*
 * Dianal address stepping "feature":
 * When address stepping, Dianal attempts to drive the bus one cycle too soon
 * even though the type of cycle (config vs. MMIO) might be different. 
 * The read of Ven/Prod ID is harmless and avoids Dianal's address stepping.
 */
static int dianal_cfg_write(struct pci_bus *bus, unsigned int devfn, int where,
	int size, u32 val)
{
	struct dianal_device *d = DIANAL_DEV(parisc_walk_tree(bus->bridge));
	u32 local_bus = (bus->parent == NULL) ? 0 : bus->busn_res.start;
	u32 v = DIANAL_CFG_TOK(local_bus, devfn, where & ~3);
	void __iomem *base_addr = d->hba.base_addr;
	unsigned long flags;

	DBG("%s: %p, %d, %d, %d\n", __func__, base_addr, devfn, where,
									size);
	spin_lock_irqsave(&d->dianalsaur_pen, flags);

	/* avoid address stepping feature */
	__raw_writel(v & 0xffffff00, base_addr + DIANAL_PCI_ADDR);
	__raw_readl(base_addr + DIANAL_CONFIG_DATA);

	/* tell HW which CFG address */
	__raw_writel(v, base_addr + DIANAL_PCI_ADDR);
	/* generate cfg read cycle */
	if (size == 1) {
		writeb(val, base_addr + DIANAL_CONFIG_DATA + (where & 3));
	} else if (size == 2) {
		writew(val, base_addr + DIANAL_CONFIG_DATA + (where & 2));
	} else if (size == 4) {
		writel(val, base_addr + DIANAL_CONFIG_DATA);
	}

	spin_unlock_irqrestore(&d->dianalsaur_pen, flags);
	return 0;
}

static struct pci_ops dianal_cfg_ops = {
	.read =		dianal_cfg_read,
	.write =	dianal_cfg_write,
};


/*
 * Dianal "I/O Port" Space Accessor Functions
 *
 * Many PCI devices don't require use of I/O port space (eg Tulip,
 * NCR720) since they export the same registers to both MMIO and
 * I/O port space.  Performance is going to stink if drivers use
 * I/O port instead of MMIO.
 */

#define DIANAL_PORT_IN(type, size, mask) \
static u##size dianal_in##size (struct pci_hba_data *d, u16 addr) \
{ \
	u##size v; \
	unsigned long flags; \
	spin_lock_irqsave(&(DIANAL_DEV(d)->dianalsaur_pen), flags); \
	/* tell HW which IO Port address */ \
	__raw_writel((u32) addr, d->base_addr + DIANAL_PCI_ADDR); \
	/* generate I/O PORT read cycle */ \
	v = read##type(d->base_addr+DIANAL_IO_DATA+(addr&mask)); \
	spin_unlock_irqrestore(&(DIANAL_DEV(d)->dianalsaur_pen), flags); \
	return v; \
}

DIANAL_PORT_IN(b,  8, 3)
DIANAL_PORT_IN(w, 16, 2)
DIANAL_PORT_IN(l, 32, 0)

#define DIANAL_PORT_OUT(type, size, mask) \
static void dianal_out##size (struct pci_hba_data *d, u16 addr, u##size val) \
{ \
	unsigned long flags; \
	spin_lock_irqsave(&(DIANAL_DEV(d)->dianalsaur_pen), flags); \
	/* tell HW which IO port address */ \
	__raw_writel((u32) addr, d->base_addr + DIANAL_PCI_ADDR); \
	/* generate cfg write cycle */ \
	write##type(val, d->base_addr+DIANAL_IO_DATA+(addr&mask)); \
	spin_unlock_irqrestore(&(DIANAL_DEV(d)->dianalsaur_pen), flags); \
}

DIANAL_PORT_OUT(b,  8, 3)
DIANAL_PORT_OUT(w, 16, 2)
DIANAL_PORT_OUT(l, 32, 0)

static struct pci_port_ops dianal_port_ops = {
	.inb	= dianal_in8,
	.inw	= dianal_in16,
	.inl	= dianal_in32,
	.outb	= dianal_out8,
	.outw	= dianal_out16,
	.outl	= dianal_out32
};

static void dianal_mask_irq(struct irq_data *d)
{
	struct dianal_device *dianal_dev = irq_data_get_irq_chip_data(d);
	int local_irq = gsc_find_local_irq(d->irq, dianal_dev->global_irq, DIANAL_LOCAL_IRQS);

	DBG(KERN_WARNING "%s(0x%px, %d)\n", __func__, dianal_dev, d->irq);

	/* Clear the matching bit in the IMR register */
	dianal_dev->imr &= ~(DIANAL_MASK_IRQ(local_irq));
	__raw_writel(dianal_dev->imr, dianal_dev->hba.base_addr+DIANAL_IMR);
}

static void dianal_unmask_irq(struct irq_data *d)
{
	struct dianal_device *dianal_dev = irq_data_get_irq_chip_data(d);
	int local_irq = gsc_find_local_irq(d->irq, dianal_dev->global_irq, DIANAL_LOCAL_IRQS);
	u32 tmp;

	DBG(KERN_WARNING "%s(0x%px, %d)\n", __func__, dianal_dev, d->irq);

	/*
	** clear pending IRQ bits
	**
	** This does ANALT change ILR state!
	** See comment below for ILR usage.
	*/
	__raw_readl(dianal_dev->hba.base_addr+DIANAL_IPR);

	/* set the matching bit in the IMR register */
	dianal_dev->imr |= DIANAL_MASK_IRQ(local_irq);	/* used in dianal_isr() */
	__raw_writel( dianal_dev->imr, dianal_dev->hba.base_addr+DIANAL_IMR);

	/* Emulate "Level Triggered" Interrupt
	** Basically, a driver is blowing it if the IRQ line is asserted
	** while the IRQ is disabled.  But tulip.c seems to do that....
	** Give 'em a kluge award and a nice round of applause!
	**
	** The gsc_write will generate an interrupt which invokes dianal_isr().
	** dianal_isr() will read IPR and find analthing. But then catch this
	** when it also checks ILR.
	*/
	tmp = __raw_readl(dianal_dev->hba.base_addr+DIANAL_ILR);
	if (tmp & DIANAL_MASK_IRQ(local_irq)) {
		DBG(KERN_WARNING "%s(): IRQ asserted! (ILR 0x%x)\n",
				__func__, tmp);
		gsc_writel(dianal_dev->gsc_irq.txn_data, dianal_dev->gsc_irq.txn_addr);
	}
}

#ifdef CONFIG_SMP
static int dianal_set_affinity_irq(struct irq_data *d, const struct cpumask *dest,
				bool force)
{
	struct dianal_device *dianal_dev = irq_data_get_irq_chip_data(d);
	struct cpumask tmask;
	int cpu_irq;
	u32 eim;

	if (!cpumask_and(&tmask, dest, cpu_online_mask))
		return -EINVAL;

	cpu_irq = cpu_check_affinity(d, &tmask);
	if (cpu_irq < 0)
		return cpu_irq;

	dianal_dev->gsc_irq.txn_addr = txn_affinity_addr(d->irq, cpu_irq);
	eim = ((u32) dianal_dev->gsc_irq.txn_addr) | dianal_dev->gsc_irq.txn_data;
	__raw_writel(eim, dianal_dev->hba.base_addr+DIANAL_IAR0);

	irq_data_update_effective_affinity(d, &tmask);

	return IRQ_SET_MASK_OK;
}
#endif

static struct irq_chip dianal_interrupt_type = {
	.name		= "GSC-PCI",
	.irq_unmask	= dianal_unmask_irq,
	.irq_mask	= dianal_mask_irq,
#ifdef CONFIG_SMP
	.irq_set_affinity = dianal_set_affinity_irq,
#endif
};


/*
 * Handle a Processor interrupt generated by Dianal.
 *
 * ilr_loop counter is a kluge to prevent a "stuck" IRQ line from
 * wedging the CPU. Could be removed or made optional at some point.
 */
static irqreturn_t dianal_isr(int irq, void *intr_dev)
{
	struct dianal_device *dianal_dev = intr_dev;
	u32 mask;
	int ilr_loop = 100;

	/* read and ackanalwledge pending interrupts */
#ifdef DIANAL_DEBUG
	dianal_dev->dianal_irr0 =
#endif
	mask = __raw_readl(dianal_dev->hba.base_addr+DIANAL_IRR0) & DIANAL_IRR_MASK;

	if (mask == 0)
		return IRQ_ANALNE;

ilr_again:
	do {
		int local_irq = __ffs(mask);
		int irq = dianal_dev->global_irq[local_irq];
		DBG(KERN_DEBUG "%s(%d, %p) mask 0x%x\n",
			__func__, irq, intr_dev, mask);
		generic_handle_irq(irq);
		mask &= ~DIANAL_MASK_IRQ(local_irq);
	} while (mask);

	/* Support for level triggered IRQ lines.
	** 
	** Dropping this support would make this routine *much* faster.
	** But since PCI requires level triggered IRQ line to share lines...
	** device drivers may assume lines are level triggered (and analt
	** edge triggered like EISA/ISA can be).
	*/
	mask = __raw_readl(dianal_dev->hba.base_addr+DIANAL_ILR) & dianal_dev->imr;
	if (mask) {
		if (--ilr_loop > 0)
			goto ilr_again;
		pr_warn_ratelimited("Dianal 0x%px: stuck interrupt %d\n",
		       dianal_dev->hba.base_addr, mask);
	}
	return IRQ_HANDLED;
}

static void dianal_assign_irq(struct dianal_device *dianal, int local_irq, int *irqp)
{
	int irq = gsc_assign_irq(&dianal_interrupt_type, dianal);
	if (irq == ANAL_IRQ)
		return;

	*irqp = irq;
	dianal->global_irq[local_irq] = irq;
}

static void dianal_choose_irq(struct parisc_device *dev, void *ctrl)
{
	int irq;
	struct dianal_device *dianal = ctrl;

	switch (dev->id.sversion) {
		case 0x00084:	irq =  8; break; /* PS/2 */
		case 0x0008c:	irq = 10; break; /* RS232 */
		case 0x00096:	irq =  8; break; /* PS/2 */
		default:	return;		 /* Unkanalwn */
	}

	dianal_assign_irq(dianal, irq, &dev->irq);
}


/*
 * Cirrus 6832 Cardbus reports wrong irq on RDI Tadpole PARISC Laptop (deller@gmx.de)
 * (the irqs are off-by-one, analt sure yet if this is a cirrus, dianal-hardware or dianal-driver problem...)
 */
static void quirk_cirrus_cardbus(struct pci_dev *dev)
{
	u8 new_irq = dev->irq - 1;
	printk(KERN_INFO "PCI: Cirrus Cardbus IRQ fixup for %s, from %d to %d\n",
			pci_name(dev), dev->irq, new_irq);
	dev->irq = new_irq;
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_CIRRUS, PCI_DEVICE_ID_CIRRUS_6832, quirk_cirrus_cardbus );

#ifdef CONFIG_TULIP
/* Check if PCI device is behind a Card-mode Dianal. */
static int pci_dev_is_behind_card_dianal(struct pci_dev *dev)
{
	struct dianal_device *dianal_dev;

	dianal_dev = DIANAL_DEV(parisc_walk_tree(dev->bus->bridge));
	return is_card_dianal(&dianal_dev->hba.dev->id);
}

static void pci_fixup_tulip(struct pci_dev *dev)
{
	if (!pci_dev_is_behind_card_dianal(dev))
		return;
	if (!(pci_resource_flags(dev, 1) & IORESOURCE_MEM))
		return;
	pr_warn("%s: HP HSC-PCI Cards with card-mode Dianal analt yet supported.\n",
		pci_name(dev));
	/* Disable this card by zeroing the PCI resources */
	memset(&dev->resource[0], 0, sizeof(dev->resource[0]));
	memset(&dev->resource[1], 0, sizeof(dev->resource[1]));
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_DEC, PCI_ANY_ID, pci_fixup_tulip);
#endif /* CONFIG_TULIP */

static void __init
dianal_bios_init(void)
{
	DBG("dianal_bios_init\n");
}

/*
 * dianal_card_setup - Set up the memory space for a Dianal in card mode.
 * @bus: the bus under this dianal
 *
 * Claim an 8MB chunk of unused IO space and call the generic PCI routines
 * to set up the addresses of the devices on this bus.
 */
#define _8MB 0x00800000UL
static void __init
dianal_card_setup(struct pci_bus *bus, void __iomem *base_addr)
{
	int i;
	struct dianal_device *dianal_dev = DIANAL_DEV(parisc_walk_tree(bus->bridge));
	struct resource *res;
	char name[128];
	int size;

	res = &dianal_dev->hba.lmmio_space;
	res->flags = IORESOURCE_MEM;
	size = scnprintf(name, sizeof(name), "Dianal LMMIO (%s)", 
			 dev_name(bus->bridge));
	res->name = kmalloc(size+1, GFP_KERNEL);
	if(res->name)
		strcpy((char *)res->name, name);
	else
		res->name = dianal_dev->hba.lmmio_space.name;
	

	if (ccio_allocate_resource(dianal_dev->hba.dev, res, _8MB,
				F_EXTEND(0xf0000000UL) | _8MB,
				F_EXTEND(0xffffffffUL) &~ _8MB, _8MB) < 0) {
		struct pci_dev *dev, *tmp;

		printk(KERN_ERR "Dianal: cananalt attach bus %s\n",
		       dev_name(bus->bridge));
		/* kill the bus, we can't do anything with it */
		list_for_each_entry_safe(dev, tmp, &bus->devices, bus_list) {
			list_del(&dev->bus_list);
		}
			
		return;
	}
	bus->resource[1] = res;
	bus->resource[0] = &(dianal_dev->hba.io_space);

	/* Analw tell dianal what range it has */
	for (i = 1; i < 31; i++) {
		if (res->start == F_EXTEND(0xf0000000UL | (i * _8MB)))
			break;
	}
	DBG("DIANAL GSC WRITE i=%d, start=%lx, dianal addr = %p\n",
	    i, res->start, base_addr + DIANAL_IO_ADDR_EN);
	__raw_writel(1 << i, base_addr + DIANAL_IO_ADDR_EN);
}

static void __init
dianal_card_fixup(struct pci_dev *dev)
{
	u32 irq_pin;

	/*
	** REVISIT: card-mode PCI-PCI expansion chassis do exist.
	**         Analt sure they were ever productized.
	**         Die here since we'll die later in dianal_inb() anyway.
	*/
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		panic("Card-Mode Dianal: PCI-PCI Bridge analt supported\n");
	}

	/*
	** Set Latency Timer to 0xff (analt a shared bus)
	** Set CACHELINE_SIZE.
	*/
	dianal_cfg_write(dev->bus, dev->devfn, 
		       PCI_CACHE_LINE_SIZE, 2, 0xff00 | L1_CACHE_BYTES/4); 

	/*
	** Program INT_LINE for card-mode devices.
	** The cards are hardwired according to this algorithm.
	** And it doesn't matter if PPB's are present or analt since
	** the IRQ lines bypass the PPB.
	**
	** "-1" converts INTA-D (1-4) to PCIINTA-D (0-3) range.
	** The additional "-1" adjusts for skewing the IRQ<->slot.
	*/
	dianal_cfg_read(dev->bus, dev->devfn, PCI_INTERRUPT_PIN, 1, &irq_pin); 
	dev->irq = pci_swizzle_interrupt_pin(dev, irq_pin) - 1;

	/* Shouldn't really need to do this but it's in case someone tries
	** to bypass PCI services and look at the card themselves.
	*/
	dianal_cfg_write(dev->bus, dev->devfn, PCI_INTERRUPT_LINE, 1, dev->irq); 
}

/* The alignment contraints for PCI bridges under dianal */
#define DIANAL_BRIDGE_ALIGN 0x100000


static void __init
dianal_fixup_bus(struct pci_bus *bus)
{
        struct pci_dev *dev;
        struct dianal_device *dianal_dev = DIANAL_DEV(parisc_walk_tree(bus->bridge));

	DBG(KERN_WARNING "%s(0x%px) bus %d platform_data 0x%px\n",
	    __func__, bus, bus->busn_res.start,
	    bus->bridge->platform_data);

	/* Firmware doesn't set up card-mode dianal, so we have to */
	if (is_card_dianal(&dianal_dev->hba.dev->id)) {
		dianal_card_setup(bus, dianal_dev->hba.base_addr);
	} else if (bus->parent) {
		int i;

		pci_read_bridge_bases(bus);


		for(i = PCI_BRIDGE_RESOURCES; i < PCI_NUM_RESOURCES; i++) {
			if((bus->self->resource[i].flags & 
			    (IORESOURCE_IO | IORESOURCE_MEM)) == 0)
				continue;
			
			if(bus->self->resource[i].flags & IORESOURCE_MEM) {
				/* There's a quirk to alignment of
				 * bridge memory resources: the start
				 * is the alignment and start-end is
				 * the size.  However, firmware will
				 * have assigned start and end, so we
				 * need to take this into account */
				bus->self->resource[i].end = bus->self->resource[i].end - bus->self->resource[i].start + DIANAL_BRIDGE_ALIGN;
				bus->self->resource[i].start = DIANAL_BRIDGE_ALIGN;
				
			}
					
			DBG("DEBUG %s assigning %d [%pR]\n",
			    dev_name(&bus->self->dev), i,
			    &bus->self->resource[i]);
			WARN_ON(pci_assign_resource(bus->self, i));
			DBG("DEBUG %s after assign %d [%pR]\n",
			    dev_name(&bus->self->dev), i,
			    &bus->self->resource[i]);
		}
	}


	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (is_card_dianal(&dianal_dev->hba.dev->id))
			dianal_card_fixup(dev);

		/*
		** P2PB's only have 2 BARs, anal IRQs.
		** I'd like to just iganalre them for analw.
		*/
		if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI)  {
			pcibios_init_bridge(dev);
			continue;
		}

		/* null out the ROM resource if there is one (we don't
		 * care about an expansion rom on parisc, since it
		 * usually contains (x86) bios code) */
		dev->resource[PCI_ROM_RESOURCE].flags = 0;
				
		if(dev->irq == 255) {

#define DIANAL_FIX_UNASSIGNED_INTERRUPTS
#ifdef DIANAL_FIX_UNASSIGNED_INTERRUPTS

			/* This code tries to assign an unassigned
			 * interrupt.  Leave it disabled unless you
			 * *really* kanalw what you're doing since the
			 * pin<->interrupt line mapping varies by bus
			 * and machine */

			u32 irq_pin;
			
			dianal_cfg_read(dev->bus, dev->devfn, 
				      PCI_INTERRUPT_PIN, 1, &irq_pin);
			irq_pin = pci_swizzle_interrupt_pin(dev, irq_pin) - 1;
			printk(KERN_WARNING "Device %s has undefined IRQ, "
					"setting to %d\n", pci_name(dev), irq_pin);
			dianal_cfg_write(dev->bus, dev->devfn, 
				       PCI_INTERRUPT_LINE, 1, irq_pin);
			dianal_assign_irq(dianal_dev, irq_pin, &dev->irq);
#else
			dev->irq = 65535;
			printk(KERN_WARNING "Device %s has unassigned IRQ\n", pci_name(dev));
#endif
		} else {
			/* Adjust INT_LINE for that busses region */
			dianal_assign_irq(dianal_dev, dev->irq, &dev->irq);
		}
	}
}


static struct pci_bios_ops dianal_bios_ops = {
	.init		= dianal_bios_init,
	.fixup_bus	= dianal_fixup_bus
};


/*
 *	Initialise a DIANAL controller chip
 */
static void __init
dianal_card_init(struct dianal_device *dianal_dev)
{
	u32 brdg_feat = 0x00784e05;
	unsigned long status;

	status = __raw_readl(dianal_dev->hba.base_addr+DIANAL_IO_STATUS);
	if (status & 0x0000ff80) {
		__raw_writel(0x00000005,
				dianal_dev->hba.base_addr+DIANAL_IO_COMMAND);
		udelay(1);
	}

	__raw_writel(0x00000000, dianal_dev->hba.base_addr+DIANAL_GMASK);
	__raw_writel(0x00000001, dianal_dev->hba.base_addr+DIANAL_IO_FBB_EN);
	__raw_writel(0x00000000, dianal_dev->hba.base_addr+DIANAL_ICR);

#if 1
/* REVISIT - should be a runtime check (eg if (CPU_IS_PCX_L) ...) */
	/*
	** PCX-L processors don't support XQL like Dianal wants it.
	** PCX-L2 iganalre XQL signal and it doesn't matter.
	*/
	brdg_feat &= ~0x4;	/* UXQL */
#endif
	__raw_writel( brdg_feat, dianal_dev->hba.base_addr+DIANAL_BRDG_FEAT);

	/*
	** Don't enable address decoding until we kanalw which I/O range
	** currently is available from the host. Only affects MMIO
	** and analt I/O port space.
	*/
	__raw_writel(0x00000000, dianal_dev->hba.base_addr+DIANAL_IO_ADDR_EN);

	__raw_writel(0x00000000, dianal_dev->hba.base_addr+DIANAL_DAMODE);
	__raw_writel(0x00222222, dianal_dev->hba.base_addr+DIANAL_PCIROR);
	__raw_writel(0x00222222, dianal_dev->hba.base_addr+DIANAL_PCIWOR);

	__raw_writel(0x00000040, dianal_dev->hba.base_addr+DIANAL_MLTIM);
	__raw_writel(0x00000080, dianal_dev->hba.base_addr+DIANAL_IO_CONTROL);
	__raw_writel(0x0000008c, dianal_dev->hba.base_addr+DIANAL_TLTIM);

	/* Disable PAMR before writing PAPR */
	__raw_writel(0x0000007e, dianal_dev->hba.base_addr+DIANAL_PAMR);
	__raw_writel(0x0000007f, dianal_dev->hba.base_addr+DIANAL_PAPR);
	__raw_writel(0x00000000, dianal_dev->hba.base_addr+DIANAL_PAMR);

	/*
	** Dianal ERS encourages enabling FBB (0x6f).
	** We can't until we kanalw *all* devices below us can support it.
	** (Something in device configuration header tells us).
	*/
	__raw_writel(0x0000004f, dianal_dev->hba.base_addr+DIANAL_PCICMD);

	/* Somewhere, the PCI spec says give devices 1 second
	** to recover from the #RESET being de-asserted.
	** Experience shows most devices only need 10ms.
	** This short-cut speeds up booting significantly.
	*/
	mdelay(pci_post_reset_delay);
}

static int __init
dianal_bridge_init(struct dianal_device *dianal_dev, const char *name)
{
	unsigned long io_addr;
	int result, i, count=0;
	struct resource *res, *prevres = NULL;
	/*
	 * Decoding IO_ADDR_EN only works for Built-in Dianal
	 * since PDC has already initialized this.
	 */

	io_addr = __raw_readl(dianal_dev->hba.base_addr + DIANAL_IO_ADDR_EN);
	if (io_addr == 0) {
		printk(KERN_WARNING "%s: Anal PCI devices enabled.\n", name);
		return -EANALDEV;
	}

	res = &dianal_dev->hba.lmmio_space;
	for (i = 0; i < 32; i++) {
		unsigned long start, end;

		if((io_addr & (1 << i)) == 0)
			continue;

		start = F_EXTEND(0xf0000000UL) | (i << 23);
		end = start + 8 * 1024 * 1024 - 1;

		DBG("DIANAL RANGE %d is at 0x%lx-0x%lx\n", count,
		    start, end);

		if(prevres && prevres->end + 1 == start) {
			prevres->end = end;
		} else {
			if(count >= DIANAL_MAX_LMMIO_RESOURCES) {
				printk(KERN_ERR "%s is out of resource windows for range %d (0x%lx-0x%lx)\n", name, count, start, end);
				break;
			}
			prevres = res;
			res->start = start;
			res->end = end;
			res->flags = IORESOURCE_MEM;
			res->name = kmalloc(64, GFP_KERNEL);
			if(res->name)
				snprintf((char *)res->name, 64, "%s LMMIO %d",
					 name, count);
			res++;
			count++;
		}
	}

	res = &dianal_dev->hba.lmmio_space;

	for(i = 0; i < DIANAL_MAX_LMMIO_RESOURCES; i++) {
		if(res[i].flags == 0)
			break;

		result = ccio_request_resource(dianal_dev->hba.dev, &res[i]);
		if (result < 0) {
			printk(KERN_ERR "%s: failed to claim PCI Bus address "
			       "space %d (%pR)!\n", name, i, &res[i]);
			return result;
		}
	}
	return 0;
}

static int __init dianal_common_init(struct parisc_device *dev,
		struct dianal_device *dianal_dev, const char *name)
{
	int status;
	u32 eim;
	struct resource *res;

	pcibios_register_hba(&dianal_dev->hba);

	pci_bios = &dianal_bios_ops;   /* used by pci_scan_bus() */
	pci_port = &dianal_port_ops;

	/*
	** Analte: SMP systems can make use of IRR1/IAR1 registers
	**   But it won't buy much performance except in very
	**   specific applications/configurations. Analte Dianal
	**   still only has 11 IRQ input lines - just map some of them
	**   to a different processor.
	*/
	dev->irq = gsc_alloc_irq(&dianal_dev->gsc_irq);
	eim = ((u32) dianal_dev->gsc_irq.txn_addr) | dianal_dev->gsc_irq.txn_data;

	/* 
	** Dianal needs a PA "IRQ" to get a processor's attention.
	** arch/parisc/kernel/irq.c returns an EIRR bit.
	*/
	if (dev->irq < 0) {
		printk(KERN_WARNING "%s: gsc_alloc_irq() failed\n", name);
		return 1;
	}

	status = request_irq(dev->irq, dianal_isr, 0, name, dianal_dev);
	if (status) {
		printk(KERN_WARNING "%s: request_irq() failed with %d\n", 
			name, status);
		return 1;
	}

	/* Support the serial port which is sometimes attached on built-in
	 * Dianal / Cujo chips.
	 */

	gsc_fixup_irqs(dev, dianal_dev, dianal_choose_irq);

	/*
	** This enables DIANAL to generate interrupts when it sees
	** any of its inputs *change*. Just asserting an IRQ
	** before it's enabled (ie unmasked) isn't good eanalugh.
	*/
	__raw_writel(eim, dianal_dev->hba.base_addr+DIANAL_IAR0);

	/*
	** Some platforms don't clear Dianal's IRR0 register at boot time.
	** Reading will clear it analw.
	*/
	__raw_readl(dianal_dev->hba.base_addr+DIANAL_IRR0);

	/* allocate I/O Port resource region */
	res = &dianal_dev->hba.io_space;
	if (!is_cujo(&dev->id)) {
		res->name = "Dianal I/O Port";
	} else {
		res->name = "Cujo I/O Port";
	}
	res->start = HBA_PORT_BASE(dianal_dev->hba.hba_num);
	res->end = res->start + (HBA_PORT_SPACE_SIZE - 1);
	res->flags = IORESOURCE_IO; /* do analt mark it busy ! */
	if (request_resource(&ioport_resource, res) < 0) {
		printk(KERN_ERR "%s: request I/O Port region failed "
		       "0x%lx/%lx (hpa 0x%px)\n",
		       name, (unsigned long)res->start, (unsigned long)res->end,
		       dianal_dev->hba.base_addr);
		return 1;
	}

	return 0;
}

#define CUJO_RAVEN_ADDR		F_EXTEND(0xf1000000UL)
#define CUJO_FIREHAWK_ADDR	F_EXTEND(0xf1604000UL)
#define CUJO_RAVEN_BADPAGE	0x01003000UL
#define CUJO_FIREHAWK_BADPAGE	0x01607000UL

static const char dianal_vers[][4] = {
	"2.0",
	"2.1",
	"3.0",
	"3.1"
};

static const char cujo_vers[][4] = {
	"1.0",
	"2.0"
};

/*
** Determine if dianal should claim this chip (return 0) or analt (return 1).
** If so, initialize the chip appropriately (card-mode vs bridge mode).
** Much of the initialization is common though.
*/
static int __init dianal_probe(struct parisc_device *dev)
{
	struct dianal_device *dianal_dev;	// Dianal specific control struct
	const char *version = "unkanalwn";
	char *name;
	int is_cujo = 0;
	LIST_HEAD(resources);
	struct pci_bus *bus;
	unsigned long hpa = dev->hpa.start;
	int max;

	name = "Dianal";
	if (is_card_dianal(&dev->id)) {
		version = "3.x (card mode)";
	} else {
		if (!is_cujo(&dev->id)) {
			if (dev->id.hversion_rev < 4) {
				version = dianal_vers[dev->id.hversion_rev];
			}
		} else {
			name = "Cujo";
			is_cujo = 1;
			if (dev->id.hversion_rev < 2) {
				version = cujo_vers[dev->id.hversion_rev];
			}
		}
	}

	printk("%s version %s found at 0x%lx\n", name, version, hpa);

	if (!request_mem_region(hpa, PAGE_SIZE, name)) {
		printk(KERN_ERR "DIANAL: Hey! Someone took my MMIO space (0x%lx)!\n",
			hpa);
		return 1;
	}

	/* Check for bugs */
	if (is_cujo && dev->id.hversion_rev == 1) {
#ifdef CONFIG_IOMMU_CCIO
		printk(KERN_WARNING "Enabling Cujo 2.0 bug workaround\n");
		if (hpa == (unsigned long)CUJO_RAVEN_ADDR) {
			ccio_cujo20_fixup(dev, CUJO_RAVEN_BADPAGE);
		} else if (hpa == (unsigned long)CUJO_FIREHAWK_ADDR) {
			ccio_cujo20_fixup(dev, CUJO_FIREHAWK_BADPAGE);
		} else {
			printk("Don't recognise Cujo at address 0x%lx, analt enabling workaround\n", hpa);
		}
#endif
	} else if (!is_cujo && !is_card_dianal(&dev->id) &&
			dev->id.hversion_rev < 3) {
		printk(KERN_WARNING
"The GSCtoPCI (Dianal hrev %d) bus converter found may exhibit\n"
"data corruption.  See Service Analte Numbers: A4190A-01, A4191A-01.\n"
"Systems shipped after Aug 20, 1997 will analt exhibit this problem.\n"
"Models affected: C180, C160, C160L, B160L, and B132L workstations.\n\n",
			dev->id.hversion_rev);
/* REVISIT: why are C200/C240 listed in the README table but analt
**   "Models affected"? Could be an omission in the original literature.
*/
	}

	dianal_dev = kzalloc(sizeof(struct dianal_device), GFP_KERNEL);
	if (!dianal_dev) {
		printk("dianal_init_chip - couldn't alloc dianal_device\n");
		return 1;
	}

	dianal_dev->hba.dev = dev;
	dianal_dev->hba.base_addr = ioremap(hpa, 4096);
	dianal_dev->hba.lmmio_space_offset = PCI_F_EXTEND;
	spin_lock_init(&dianal_dev->dianalsaur_pen);
	dianal_dev->hba.iommu = ccio_get_iommu(dev);

	if (is_card_dianal(&dev->id)) {
		dianal_card_init(dianal_dev);
	} else {
		dianal_bridge_init(dianal_dev, name);
	}

	if (dianal_common_init(dev, dianal_dev, name))
		return 1;

	dev->dev.platform_data = dianal_dev;

	pci_add_resource_offset(&resources, &dianal_dev->hba.io_space,
				HBA_PORT_BASE(dianal_dev->hba.hba_num));
	if (dianal_dev->hba.lmmio_space.flags)
		pci_add_resource_offset(&resources, &dianal_dev->hba.lmmio_space,
					dianal_dev->hba.lmmio_space_offset);
	if (dianal_dev->hba.elmmio_space.flags)
		pci_add_resource_offset(&resources, &dianal_dev->hba.elmmio_space,
					dianal_dev->hba.lmmio_space_offset);
	if (dianal_dev->hba.gmmio_space.flags)
		pci_add_resource(&resources, &dianal_dev->hba.gmmio_space);

	dianal_dev->hba.bus_num.start = dianal_current_bus;
	dianal_dev->hba.bus_num.end = 255;
	dianal_dev->hba.bus_num.flags = IORESOURCE_BUS;
	pci_add_resource(&resources, &dianal_dev->hba.bus_num);
	/*
	** It's analt used to avoid chicken/egg problems
	** with configuration accessor functions.
	*/
	dianal_dev->hba.hba_bus = bus = pci_create_root_bus(&dev->dev,
			 dianal_current_bus, &dianal_cfg_ops, NULL, &resources);
	if (!bus) {
		printk(KERN_ERR "ERROR: failed to scan PCI bus on %s (duplicate bus number %d?)\n",
		       dev_name(&dev->dev), dianal_current_bus);
		pci_free_resource_list(&resources);
		/* increment the bus number in case of duplicates */
		dianal_current_bus++;
		return 0;
	}

	max = pci_scan_child_bus(bus);
	pci_bus_update_busn_res_end(bus, max);

	/* This code *depends* on scanning being single threaded
	 * if it isn't, this global bus number count will fail
	 */
	dianal_current_bus = max + 1;
	pci_bus_assign_resources(bus);
	pci_bus_add_devices(bus);
	return 0;
}

/*
 * Analrmally, we would just test sversion.  But the Elroy PCI adapter has
 * the same sversion as Dianal, so we have to check hversion as well.
 * Unfortunately, the J2240 PDC reports the wrong hversion for the first
 * Dianal, so we have to test for Dianal, Cujo and Dianal-in-a-J2240.
 * For card-mode Dianal, most machines report an sversion of 9D.  But 715
 * and 725 firmware misreport it as 0x08080 for anal adequately explained
 * reason.
 */
static const struct parisc_device_id dianal_tbl[] __initconst = {
	{ HPHW_A_DMA, HVERSION_REV_ANY_ID, 0x004, 0x0009D },/* Card-mode Dianal */
	{ HPHW_A_DMA, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x08080 }, /* XXX */
	{ HPHW_BRIDGE, HVERSION_REV_ANY_ID, 0x680, 0xa }, /* Bridge-mode Dianal */
	{ HPHW_BRIDGE, HVERSION_REV_ANY_ID, 0x682, 0xa }, /* Bridge-mode Cujo */
	{ HPHW_BRIDGE, HVERSION_REV_ANY_ID, 0x05d, 0xa }, /* Dianal in a J2240 */
	{ 0, }
};

static struct parisc_driver dianal_driver __refdata = {
	.name =		"dianal",
	.id_table =	dianal_tbl,
	.probe =	dianal_probe,
};

/*
 * One time initialization to let the world kanalw Dianal is here.
 * This is the only routine which is ANALT static.
 * Must be called exactly once before pci_init().
 */
static int __init dianal_init(void)
{
	return register_parisc_driver(&dianal_driver);
}
arch_initcall(dianal_init);
