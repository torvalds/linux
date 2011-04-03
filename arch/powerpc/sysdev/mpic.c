/*
 *  arch/powerpc/kernel/mpic.c
 *
 *  Driver for interrupt controllers following the OpenPIC standard, the
 *  common implementation beeing IBM's MPIC. This driver also can deal
 *  with various broken implementations of this HW.
 *
 *  Copyright (C) 2004 Benjamin Herrenschmidt, IBM Corp.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#undef DEBUG
#undef DEBUG_IPI
#undef DEBUG_IRQ
#undef DEBUG_LOW

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/bootmem.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/mpic.h>
#include <asm/smp.h>

#include "mpic.h"

#ifdef DEBUG
#define DBG(fmt...) printk(fmt)
#else
#define DBG(fmt...)
#endif

static struct mpic *mpics;
static struct mpic *mpic_primary;
static DEFINE_RAW_SPINLOCK(mpic_lock);

#ifdef CONFIG_PPC32	/* XXX for now */
#ifdef CONFIG_IRQ_ALL_CPUS
#define distribute_irqs	(1)
#else
#define distribute_irqs	(0)
#endif
#endif

#ifdef CONFIG_MPIC_WEIRD
static u32 mpic_infos[][MPIC_IDX_END] = {
	[0] = {	/* Original OpenPIC compatible MPIC */
		MPIC_GREG_BASE,
		MPIC_GREG_FEATURE_0,
		MPIC_GREG_GLOBAL_CONF_0,
		MPIC_GREG_VENDOR_ID,
		MPIC_GREG_IPI_VECTOR_PRI_0,
		MPIC_GREG_IPI_STRIDE,
		MPIC_GREG_SPURIOUS,
		MPIC_GREG_TIMER_FREQ,

		MPIC_TIMER_BASE,
		MPIC_TIMER_STRIDE,
		MPIC_TIMER_CURRENT_CNT,
		MPIC_TIMER_BASE_CNT,
		MPIC_TIMER_VECTOR_PRI,
		MPIC_TIMER_DESTINATION,

		MPIC_CPU_BASE,
		MPIC_CPU_STRIDE,
		MPIC_CPU_IPI_DISPATCH_0,
		MPIC_CPU_IPI_DISPATCH_STRIDE,
		MPIC_CPU_CURRENT_TASK_PRI,
		MPIC_CPU_WHOAMI,
		MPIC_CPU_INTACK,
		MPIC_CPU_EOI,
		MPIC_CPU_MCACK,

		MPIC_IRQ_BASE,
		MPIC_IRQ_STRIDE,
		MPIC_IRQ_VECTOR_PRI,
		MPIC_VECPRI_VECTOR_MASK,
		MPIC_VECPRI_POLARITY_POSITIVE,
		MPIC_VECPRI_POLARITY_NEGATIVE,
		MPIC_VECPRI_SENSE_LEVEL,
		MPIC_VECPRI_SENSE_EDGE,
		MPIC_VECPRI_POLARITY_MASK,
		MPIC_VECPRI_SENSE_MASK,
		MPIC_IRQ_DESTINATION
	},
	[1] = {	/* Tsi108/109 PIC */
		TSI108_GREG_BASE,
		TSI108_GREG_FEATURE_0,
		TSI108_GREG_GLOBAL_CONF_0,
		TSI108_GREG_VENDOR_ID,
		TSI108_GREG_IPI_VECTOR_PRI_0,
		TSI108_GREG_IPI_STRIDE,
		TSI108_GREG_SPURIOUS,
		TSI108_GREG_TIMER_FREQ,

		TSI108_TIMER_BASE,
		TSI108_TIMER_STRIDE,
		TSI108_TIMER_CURRENT_CNT,
		TSI108_TIMER_BASE_CNT,
		TSI108_TIMER_VECTOR_PRI,
		TSI108_TIMER_DESTINATION,

		TSI108_CPU_BASE,
		TSI108_CPU_STRIDE,
		TSI108_CPU_IPI_DISPATCH_0,
		TSI108_CPU_IPI_DISPATCH_STRIDE,
		TSI108_CPU_CURRENT_TASK_PRI,
		TSI108_CPU_WHOAMI,
		TSI108_CPU_INTACK,
		TSI108_CPU_EOI,
		TSI108_CPU_MCACK,

		TSI108_IRQ_BASE,
		TSI108_IRQ_STRIDE,
		TSI108_IRQ_VECTOR_PRI,
		TSI108_VECPRI_VECTOR_MASK,
		TSI108_VECPRI_POLARITY_POSITIVE,
		TSI108_VECPRI_POLARITY_NEGATIVE,
		TSI108_VECPRI_SENSE_LEVEL,
		TSI108_VECPRI_SENSE_EDGE,
		TSI108_VECPRI_POLARITY_MASK,
		TSI108_VECPRI_SENSE_MASK,
		TSI108_IRQ_DESTINATION
	},
};

#define MPIC_INFO(name) mpic->hw_set[MPIC_IDX_##name]

#else /* CONFIG_MPIC_WEIRD */

#define MPIC_INFO(name) MPIC_##name

#endif /* CONFIG_MPIC_WEIRD */

static inline unsigned int mpic_processor_id(struct mpic *mpic)
{
	unsigned int cpu = 0;

	if (mpic->flags & MPIC_PRIMARY)
		cpu = hard_smp_processor_id();

	return cpu;
}

/*
 * Register accessor functions
 */


static inline u32 _mpic_read(enum mpic_reg_type type,
			     struct mpic_reg_bank *rb,
			     unsigned int reg)
{
	switch(type) {
#ifdef CONFIG_PPC_DCR
	case mpic_access_dcr:
		return dcr_read(rb->dhost, reg);
#endif
	case mpic_access_mmio_be:
		return in_be32(rb->base + (reg >> 2));
	case mpic_access_mmio_le:
	default:
		return in_le32(rb->base + (reg >> 2));
	}
}

static inline void _mpic_write(enum mpic_reg_type type,
			       struct mpic_reg_bank *rb,
 			       unsigned int reg, u32 value)
{
	switch(type) {
#ifdef CONFIG_PPC_DCR
	case mpic_access_dcr:
		dcr_write(rb->dhost, reg, value);
		break;
#endif
	case mpic_access_mmio_be:
		out_be32(rb->base + (reg >> 2), value);
		break;
	case mpic_access_mmio_le:
	default:
		out_le32(rb->base + (reg >> 2), value);
		break;
	}
}

static inline u32 _mpic_ipi_read(struct mpic *mpic, unsigned int ipi)
{
	enum mpic_reg_type type = mpic->reg_type;
	unsigned int offset = MPIC_INFO(GREG_IPI_VECTOR_PRI_0) +
			      (ipi * MPIC_INFO(GREG_IPI_STRIDE));

	if ((mpic->flags & MPIC_BROKEN_IPI) && type == mpic_access_mmio_le)
		type = mpic_access_mmio_be;
	return _mpic_read(type, &mpic->gregs, offset);
}

static inline void _mpic_ipi_write(struct mpic *mpic, unsigned int ipi, u32 value)
{
	unsigned int offset = MPIC_INFO(GREG_IPI_VECTOR_PRI_0) +
			      (ipi * MPIC_INFO(GREG_IPI_STRIDE));

	_mpic_write(mpic->reg_type, &mpic->gregs, offset, value);
}

static inline u32 _mpic_cpu_read(struct mpic *mpic, unsigned int reg)
{
	unsigned int cpu = mpic_processor_id(mpic);

	return _mpic_read(mpic->reg_type, &mpic->cpuregs[cpu], reg);
}

static inline void _mpic_cpu_write(struct mpic *mpic, unsigned int reg, u32 value)
{
	unsigned int cpu = mpic_processor_id(mpic);

	_mpic_write(mpic->reg_type, &mpic->cpuregs[cpu], reg, value);
}

static inline u32 _mpic_irq_read(struct mpic *mpic, unsigned int src_no, unsigned int reg)
{
	unsigned int	isu = src_no >> mpic->isu_shift;
	unsigned int	idx = src_no & mpic->isu_mask;
	unsigned int	val;

	val = _mpic_read(mpic->reg_type, &mpic->isus[isu],
			 reg + (idx * MPIC_INFO(IRQ_STRIDE)));
#ifdef CONFIG_MPIC_BROKEN_REGREAD
	if (reg == 0)
		val = (val & (MPIC_VECPRI_MASK | MPIC_VECPRI_ACTIVITY)) |
			mpic->isu_reg0_shadow[src_no];
#endif
	return val;
}

static inline void _mpic_irq_write(struct mpic *mpic, unsigned int src_no,
				   unsigned int reg, u32 value)
{
	unsigned int	isu = src_no >> mpic->isu_shift;
	unsigned int	idx = src_no & mpic->isu_mask;

	_mpic_write(mpic->reg_type, &mpic->isus[isu],
		    reg + (idx * MPIC_INFO(IRQ_STRIDE)), value);

#ifdef CONFIG_MPIC_BROKEN_REGREAD
	if (reg == 0)
		mpic->isu_reg0_shadow[src_no] =
			value & ~(MPIC_VECPRI_MASK | MPIC_VECPRI_ACTIVITY);
#endif
}

#define mpic_read(b,r)		_mpic_read(mpic->reg_type,&(b),(r))
#define mpic_write(b,r,v)	_mpic_write(mpic->reg_type,&(b),(r),(v))
#define mpic_ipi_read(i)	_mpic_ipi_read(mpic,(i))
#define mpic_ipi_write(i,v)	_mpic_ipi_write(mpic,(i),(v))
#define mpic_cpu_read(i)	_mpic_cpu_read(mpic,(i))
#define mpic_cpu_write(i,v)	_mpic_cpu_write(mpic,(i),(v))
#define mpic_irq_read(s,r)	_mpic_irq_read(mpic,(s),(r))
#define mpic_irq_write(s,r,v)	_mpic_irq_write(mpic,(s),(r),(v))


/*
 * Low level utility functions
 */


static void _mpic_map_mmio(struct mpic *mpic, phys_addr_t phys_addr,
			   struct mpic_reg_bank *rb, unsigned int offset,
			   unsigned int size)
{
	rb->base = ioremap(phys_addr + offset, size);
	BUG_ON(rb->base == NULL);
}

#ifdef CONFIG_PPC_DCR
static void _mpic_map_dcr(struct mpic *mpic, struct device_node *node,
			  struct mpic_reg_bank *rb,
			  unsigned int offset, unsigned int size)
{
	const u32 *dbasep;

	dbasep = of_get_property(node, "dcr-reg", NULL);

	rb->dhost = dcr_map(node, *dbasep + offset, size);
	BUG_ON(!DCR_MAP_OK(rb->dhost));
}

static inline void mpic_map(struct mpic *mpic, struct device_node *node,
			    phys_addr_t phys_addr, struct mpic_reg_bank *rb,
			    unsigned int offset, unsigned int size)
{
	if (mpic->flags & MPIC_USES_DCR)
		_mpic_map_dcr(mpic, node, rb, offset, size);
	else
		_mpic_map_mmio(mpic, phys_addr, rb, offset, size);
}
#else /* CONFIG_PPC_DCR */
#define mpic_map(m,n,p,b,o,s)	_mpic_map_mmio(m,p,b,o,s)
#endif /* !CONFIG_PPC_DCR */



/* Check if we have one of those nice broken MPICs with a flipped endian on
 * reads from IPI registers
 */
static void __init mpic_test_broken_ipi(struct mpic *mpic)
{
	u32 r;

	mpic_write(mpic->gregs, MPIC_INFO(GREG_IPI_VECTOR_PRI_0), MPIC_VECPRI_MASK);
	r = mpic_read(mpic->gregs, MPIC_INFO(GREG_IPI_VECTOR_PRI_0));

	if (r == le32_to_cpu(MPIC_VECPRI_MASK)) {
		printk(KERN_INFO "mpic: Detected reversed IPI registers\n");
		mpic->flags |= MPIC_BROKEN_IPI;
	}
}

#ifdef CONFIG_MPIC_U3_HT_IRQS

/* Test if an interrupt is sourced from HyperTransport (used on broken U3s)
 * to force the edge setting on the MPIC and do the ack workaround.
 */
static inline int mpic_is_ht_interrupt(struct mpic *mpic, unsigned int source)
{
	if (source >= 128 || !mpic->fixups)
		return 0;
	return mpic->fixups[source].base != NULL;
}


static inline void mpic_ht_end_irq(struct mpic *mpic, unsigned int source)
{
	struct mpic_irq_fixup *fixup = &mpic->fixups[source];

	if (fixup->applebase) {
		unsigned int soff = (fixup->index >> 3) & ~3;
		unsigned int mask = 1U << (fixup->index & 0x1f);
		writel(mask, fixup->applebase + soff);
	} else {
		raw_spin_lock(&mpic->fixup_lock);
		writeb(0x11 + 2 * fixup->index, fixup->base + 2);
		writel(fixup->data, fixup->base + 4);
		raw_spin_unlock(&mpic->fixup_lock);
	}
}

static void mpic_startup_ht_interrupt(struct mpic *mpic, unsigned int source,
				      bool level)
{
	struct mpic_irq_fixup *fixup = &mpic->fixups[source];
	unsigned long flags;
	u32 tmp;

	if (fixup->base == NULL)
		return;

	DBG("startup_ht_interrupt(0x%x) index: %d\n",
	    source, fixup->index);
	raw_spin_lock_irqsave(&mpic->fixup_lock, flags);
	/* Enable and configure */
	writeb(0x10 + 2 * fixup->index, fixup->base + 2);
	tmp = readl(fixup->base + 4);
	tmp &= ~(0x23U);
	if (level)
		tmp |= 0x22;
	writel(tmp, fixup->base + 4);
	raw_spin_unlock_irqrestore(&mpic->fixup_lock, flags);

#ifdef CONFIG_PM
	/* use the lowest bit inverted to the actual HW,
	 * set if this fixup was enabled, clear otherwise */
	mpic->save_data[source].fixup_data = tmp | 1;
#endif
}

static void mpic_shutdown_ht_interrupt(struct mpic *mpic, unsigned int source)
{
	struct mpic_irq_fixup *fixup = &mpic->fixups[source];
	unsigned long flags;
	u32 tmp;

	if (fixup->base == NULL)
		return;

	DBG("shutdown_ht_interrupt(0x%x)\n", source);

	/* Disable */
	raw_spin_lock_irqsave(&mpic->fixup_lock, flags);
	writeb(0x10 + 2 * fixup->index, fixup->base + 2);
	tmp = readl(fixup->base + 4);
	tmp |= 1;
	writel(tmp, fixup->base + 4);
	raw_spin_unlock_irqrestore(&mpic->fixup_lock, flags);

#ifdef CONFIG_PM
	/* use the lowest bit inverted to the actual HW,
	 * set if this fixup was enabled, clear otherwise */
	mpic->save_data[source].fixup_data = tmp & ~1;
#endif
}

#ifdef CONFIG_PCI_MSI
static void __init mpic_scan_ht_msi(struct mpic *mpic, u8 __iomem *devbase,
				    unsigned int devfn)
{
	u8 __iomem *base;
	u8 pos, flags;
	u64 addr = 0;

	for (pos = readb(devbase + PCI_CAPABILITY_LIST); pos != 0;
	     pos = readb(devbase + pos + PCI_CAP_LIST_NEXT)) {
		u8 id = readb(devbase + pos + PCI_CAP_LIST_ID);
		if (id == PCI_CAP_ID_HT) {
			id = readb(devbase + pos + 3);
			if ((id & HT_5BIT_CAP_MASK) == HT_CAPTYPE_MSI_MAPPING)
				break;
		}
	}

	if (pos == 0)
		return;

	base = devbase + pos;

	flags = readb(base + HT_MSI_FLAGS);
	if (!(flags & HT_MSI_FLAGS_FIXED)) {
		addr = readl(base + HT_MSI_ADDR_LO) & HT_MSI_ADDR_LO_MASK;
		addr = addr | ((u64)readl(base + HT_MSI_ADDR_HI) << 32);
	}

	printk(KERN_DEBUG "mpic:   - HT:%02x.%x %s MSI mapping found @ 0x%llx\n",
		PCI_SLOT(devfn), PCI_FUNC(devfn),
		flags & HT_MSI_FLAGS_ENABLE ? "enabled" : "disabled", addr);

	if (!(flags & HT_MSI_FLAGS_ENABLE))
		writeb(flags | HT_MSI_FLAGS_ENABLE, base + HT_MSI_FLAGS);
}
#else
static void __init mpic_scan_ht_msi(struct mpic *mpic, u8 __iomem *devbase,
				    unsigned int devfn)
{
	return;
}
#endif

static void __init mpic_scan_ht_pic(struct mpic *mpic, u8 __iomem *devbase,
				    unsigned int devfn, u32 vdid)
{
	int i, irq, n;
	u8 __iomem *base;
	u32 tmp;
	u8 pos;

	for (pos = readb(devbase + PCI_CAPABILITY_LIST); pos != 0;
	     pos = readb(devbase + pos + PCI_CAP_LIST_NEXT)) {
		u8 id = readb(devbase + pos + PCI_CAP_LIST_ID);
		if (id == PCI_CAP_ID_HT) {
			id = readb(devbase + pos + 3);
			if ((id & HT_5BIT_CAP_MASK) == HT_CAPTYPE_IRQ)
				break;
		}
	}
	if (pos == 0)
		return;

	base = devbase + pos;
	writeb(0x01, base + 2);
	n = (readl(base + 4) >> 16) & 0xff;

	printk(KERN_INFO "mpic:   - HT:%02x.%x [0x%02x] vendor %04x device %04x"
	       " has %d irqs\n",
	       devfn >> 3, devfn & 0x7, pos, vdid & 0xffff, vdid >> 16, n + 1);

	for (i = 0; i <= n; i++) {
		writeb(0x10 + 2 * i, base + 2);
		tmp = readl(base + 4);
		irq = (tmp >> 16) & 0xff;
		DBG("HT PIC index 0x%x, irq 0x%x, tmp: %08x\n", i, irq, tmp);
		/* mask it , will be unmasked later */
		tmp |= 0x1;
		writel(tmp, base + 4);
		mpic->fixups[irq].index = i;
		mpic->fixups[irq].base = base;
		/* Apple HT PIC has a non-standard way of doing EOIs */
		if ((vdid & 0xffff) == 0x106b)
			mpic->fixups[irq].applebase = devbase + 0x60;
		else
			mpic->fixups[irq].applebase = NULL;
		writeb(0x11 + 2 * i, base + 2);
		mpic->fixups[irq].data = readl(base + 4) | 0x80000000;
	}
}
 

static void __init mpic_scan_ht_pics(struct mpic *mpic)
{
	unsigned int devfn;
	u8 __iomem *cfgspace;

	printk(KERN_INFO "mpic: Setting up HT PICs workarounds for U3/U4\n");

	/* Allocate fixups array */
	mpic->fixups = kzalloc(128 * sizeof(*mpic->fixups), GFP_KERNEL);
	BUG_ON(mpic->fixups == NULL);

	/* Init spinlock */
	raw_spin_lock_init(&mpic->fixup_lock);

	/* Map U3 config space. We assume all IO-APICs are on the primary bus
	 * so we only need to map 64kB.
	 */
	cfgspace = ioremap(0xf2000000, 0x10000);
	BUG_ON(cfgspace == NULL);

	/* Now we scan all slots. We do a very quick scan, we read the header
	 * type, vendor ID and device ID only, that's plenty enough
	 */
	for (devfn = 0; devfn < 0x100; devfn++) {
		u8 __iomem *devbase = cfgspace + (devfn << 8);
		u8 hdr_type = readb(devbase + PCI_HEADER_TYPE);
		u32 l = readl(devbase + PCI_VENDOR_ID);
		u16 s;

		DBG("devfn %x, l: %x\n", devfn, l);

		/* If no device, skip */
		if (l == 0xffffffff || l == 0x00000000 ||
		    l == 0x0000ffff || l == 0xffff0000)
			goto next;
		/* Check if is supports capability lists */
		s = readw(devbase + PCI_STATUS);
		if (!(s & PCI_STATUS_CAP_LIST))
			goto next;

		mpic_scan_ht_pic(mpic, devbase, devfn, l);
		mpic_scan_ht_msi(mpic, devbase, devfn);

	next:
		/* next device, if function 0 */
		if (PCI_FUNC(devfn) == 0 && (hdr_type & 0x80) == 0)
			devfn += 7;
	}
}

#else /* CONFIG_MPIC_U3_HT_IRQS */

static inline int mpic_is_ht_interrupt(struct mpic *mpic, unsigned int source)
{
	return 0;
}

static void __init mpic_scan_ht_pics(struct mpic *mpic)
{
}

#endif /* CONFIG_MPIC_U3_HT_IRQS */

#ifdef CONFIG_SMP
static int irq_choose_cpu(const struct cpumask *mask)
{
	int cpuid;

	if (cpumask_equal(mask, cpu_all_mask)) {
		static int irq_rover = 0;
		static DEFINE_RAW_SPINLOCK(irq_rover_lock);
		unsigned long flags;

		/* Round-robin distribution... */
	do_round_robin:
		raw_spin_lock_irqsave(&irq_rover_lock, flags);

		irq_rover = cpumask_next(irq_rover, cpu_online_mask);
		if (irq_rover >= nr_cpu_ids)
			irq_rover = cpumask_first(cpu_online_mask);

		cpuid = irq_rover;

		raw_spin_unlock_irqrestore(&irq_rover_lock, flags);
	} else {
		cpuid = cpumask_first_and(mask, cpu_online_mask);
		if (cpuid >= nr_cpu_ids)
			goto do_round_robin;
	}

	return get_hard_smp_processor_id(cpuid);
}
#else
static int irq_choose_cpu(const struct cpumask *mask)
{
	return hard_smp_processor_id();
}
#endif

#define mpic_irq_to_hw(virq)	((unsigned int)irq_map[virq].hwirq)

/* Find an mpic associated with a given linux interrupt */
static struct mpic *mpic_find(unsigned int irq)
{
	if (irq < NUM_ISA_INTERRUPTS)
		return NULL;

	return irq_get_chip_data(irq);
}

/* Determine if the linux irq is an IPI */
static unsigned int mpic_is_ipi(struct mpic *mpic, unsigned int irq)
{
	unsigned int src = mpic_irq_to_hw(irq);

	return (src >= mpic->ipi_vecs[0] && src <= mpic->ipi_vecs[3]);
}


/* Convert a cpu mask from logical to physical cpu numbers. */
static inline u32 mpic_physmask(u32 cpumask)
{
	int i;
	u32 mask = 0;

	for (i = 0; i < NR_CPUS; ++i, cpumask >>= 1)
		mask |= (cpumask & 1) << get_hard_smp_processor_id(i);
	return mask;
}

#ifdef CONFIG_SMP
/* Get the mpic structure from the IPI number */
static inline struct mpic * mpic_from_ipi(struct irq_data *d)
{
	return irq_data_get_irq_chip_data(d);
}
#endif

/* Get the mpic structure from the irq number */
static inline struct mpic * mpic_from_irq(unsigned int irq)
{
	return irq_get_chip_data(irq);
}

/* Get the mpic structure from the irq data */
static inline struct mpic * mpic_from_irq_data(struct irq_data *d)
{
	return irq_data_get_irq_chip_data(d);
}

/* Send an EOI */
static inline void mpic_eoi(struct mpic *mpic)
{
	mpic_cpu_write(MPIC_INFO(CPU_EOI), 0);
	(void)mpic_cpu_read(MPIC_INFO(CPU_WHOAMI));
}

/*
 * Linux descriptor level callbacks
 */


void mpic_unmask_irq(struct irq_data *d)
{
	unsigned int loops = 100000;
	struct mpic *mpic = mpic_from_irq_data(d);
	unsigned int src = mpic_irq_to_hw(d->irq);

	DBG("%p: %s: enable_irq: %d (src %d)\n", mpic, mpic->name, d->irq, src);

	mpic_irq_write(src, MPIC_INFO(IRQ_VECTOR_PRI),
		       mpic_irq_read(src, MPIC_INFO(IRQ_VECTOR_PRI)) &
		       ~MPIC_VECPRI_MASK);
	/* make sure mask gets to controller before we return to user */
	do {
		if (!loops--) {
			printk(KERN_ERR "%s: timeout on hwirq %u\n",
			       __func__, src);
			break;
		}
	} while(mpic_irq_read(src, MPIC_INFO(IRQ_VECTOR_PRI)) & MPIC_VECPRI_MASK);
}

void mpic_mask_irq(struct irq_data *d)
{
	unsigned int loops = 100000;
	struct mpic *mpic = mpic_from_irq_data(d);
	unsigned int src = mpic_irq_to_hw(d->irq);

	DBG("%s: disable_irq: %d (src %d)\n", mpic->name, d->irq, src);

	mpic_irq_write(src, MPIC_INFO(IRQ_VECTOR_PRI),
		       mpic_irq_read(src, MPIC_INFO(IRQ_VECTOR_PRI)) |
		       MPIC_VECPRI_MASK);

	/* make sure mask gets to controller before we return to user */
	do {
		if (!loops--) {
			printk(KERN_ERR "%s: timeout on hwirq %u\n",
			       __func__, src);
			break;
		}
	} while(!(mpic_irq_read(src, MPIC_INFO(IRQ_VECTOR_PRI)) & MPIC_VECPRI_MASK));
}

void mpic_end_irq(struct irq_data *d)
{
	struct mpic *mpic = mpic_from_irq_data(d);

#ifdef DEBUG_IRQ
	DBG("%s: end_irq: %d\n", mpic->name, d->irq);
#endif
	/* We always EOI on end_irq() even for edge interrupts since that
	 * should only lower the priority, the MPIC should have properly
	 * latched another edge interrupt coming in anyway
	 */

	mpic_eoi(mpic);
}

#ifdef CONFIG_MPIC_U3_HT_IRQS

static void mpic_unmask_ht_irq(struct irq_data *d)
{
	struct mpic *mpic = mpic_from_irq_data(d);
	unsigned int src = mpic_irq_to_hw(d->irq);

	mpic_unmask_irq(d);

	if (irqd_is_level_type(d))
		mpic_ht_end_irq(mpic, src);
}

static unsigned int mpic_startup_ht_irq(struct irq_data *d)
{
	struct mpic *mpic = mpic_from_irq_data(d);
	unsigned int src = mpic_irq_to_hw(d->irq);

	mpic_unmask_irq(d);
	mpic_startup_ht_interrupt(mpic, src, irqd_is_level_type(d));

	return 0;
}

static void mpic_shutdown_ht_irq(struct irq_data *d)
{
	struct mpic *mpic = mpic_from_irq_data(d);
	unsigned int src = mpic_irq_to_hw(d->irq);

	mpic_shutdown_ht_interrupt(mpic, src);
	mpic_mask_irq(d);
}

static void mpic_end_ht_irq(struct irq_data *d)
{
	struct mpic *mpic = mpic_from_irq_data(d);
	unsigned int src = mpic_irq_to_hw(d->irq);

#ifdef DEBUG_IRQ
	DBG("%s: end_irq: %d\n", mpic->name, d->irq);
#endif
	/* We always EOI on end_irq() even for edge interrupts since that
	 * should only lower the priority, the MPIC should have properly
	 * latched another edge interrupt coming in anyway
	 */

	if (irqd_is_level_type(d))
		mpic_ht_end_irq(mpic, src);
	mpic_eoi(mpic);
}
#endif /* !CONFIG_MPIC_U3_HT_IRQS */

#ifdef CONFIG_SMP

static void mpic_unmask_ipi(struct irq_data *d)
{
	struct mpic *mpic = mpic_from_ipi(d);
	unsigned int src = mpic_irq_to_hw(d->irq) - mpic->ipi_vecs[0];

	DBG("%s: enable_ipi: %d (ipi %d)\n", mpic->name, d->irq, src);
	mpic_ipi_write(src, mpic_ipi_read(src) & ~MPIC_VECPRI_MASK);
}

static void mpic_mask_ipi(struct irq_data *d)
{
	/* NEVER disable an IPI... that's just plain wrong! */
}

static void mpic_end_ipi(struct irq_data *d)
{
	struct mpic *mpic = mpic_from_ipi(d);

	/*
	 * IPIs are marked IRQ_PER_CPU. This has the side effect of
	 * preventing the IRQ_PENDING/IRQ_INPROGRESS logic from
	 * applying to them. We EOI them late to avoid re-entering.
	 * We mark IPI's with IRQF_DISABLED as they must run with
	 * irqs disabled.
	 */
	mpic_eoi(mpic);
}

#endif /* CONFIG_SMP */

int mpic_set_affinity(struct irq_data *d, const struct cpumask *cpumask,
		      bool force)
{
	struct mpic *mpic = mpic_from_irq_data(d);
	unsigned int src = mpic_irq_to_hw(d->irq);

	if (mpic->flags & MPIC_SINGLE_DEST_CPU) {
		int cpuid = irq_choose_cpu(cpumask);

		mpic_irq_write(src, MPIC_INFO(IRQ_DESTINATION), 1 << cpuid);
	} else {
		cpumask_var_t tmp;

		alloc_cpumask_var(&tmp, GFP_KERNEL);

		cpumask_and(tmp, cpumask, cpu_online_mask);

		mpic_irq_write(src, MPIC_INFO(IRQ_DESTINATION),
			       mpic_physmask(cpumask_bits(tmp)[0]));

		free_cpumask_var(tmp);
	}

	return 0;
}

static unsigned int mpic_type_to_vecpri(struct mpic *mpic, unsigned int type)
{
	/* Now convert sense value */
	switch(type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		return MPIC_INFO(VECPRI_SENSE_EDGE) |
		       MPIC_INFO(VECPRI_POLARITY_POSITIVE);
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		return MPIC_INFO(VECPRI_SENSE_EDGE) |
		       MPIC_INFO(VECPRI_POLARITY_NEGATIVE);
	case IRQ_TYPE_LEVEL_HIGH:
		return MPIC_INFO(VECPRI_SENSE_LEVEL) |
		       MPIC_INFO(VECPRI_POLARITY_POSITIVE);
	case IRQ_TYPE_LEVEL_LOW:
	default:
		return MPIC_INFO(VECPRI_SENSE_LEVEL) |
		       MPIC_INFO(VECPRI_POLARITY_NEGATIVE);
	}
}

int mpic_set_irq_type(struct irq_data *d, unsigned int flow_type)
{
	struct mpic *mpic = mpic_from_irq_data(d);
	unsigned int src = mpic_irq_to_hw(d->irq);
	unsigned int vecpri, vold, vnew;

	DBG("mpic: set_irq_type(mpic:@%p,virq:%d,src:0x%x,type:0x%x)\n",
	    mpic, d->irq, src, flow_type);

	if (src >= mpic->irq_count)
		return -EINVAL;

	if (flow_type == IRQ_TYPE_NONE)
		if (mpic->senses && src < mpic->senses_count)
			flow_type = mpic->senses[src];
	if (flow_type == IRQ_TYPE_NONE)
		flow_type = IRQ_TYPE_LEVEL_LOW;

	irqd_set_trigger_type(d, flow_type);

	if (mpic_is_ht_interrupt(mpic, src))
		vecpri = MPIC_VECPRI_POLARITY_POSITIVE |
			MPIC_VECPRI_SENSE_EDGE;
	else
		vecpri = mpic_type_to_vecpri(mpic, flow_type);

	vold = mpic_irq_read(src, MPIC_INFO(IRQ_VECTOR_PRI));
	vnew = vold & ~(MPIC_INFO(VECPRI_POLARITY_MASK) |
			MPIC_INFO(VECPRI_SENSE_MASK));
	vnew |= vecpri;
	if (vold != vnew)
		mpic_irq_write(src, MPIC_INFO(IRQ_VECTOR_PRI), vnew);

	return IRQ_SET_MASK_OK_NOCOPY;;
}

void mpic_set_vector(unsigned int virq, unsigned int vector)
{
	struct mpic *mpic = mpic_from_irq(virq);
	unsigned int src = mpic_irq_to_hw(virq);
	unsigned int vecpri;

	DBG("mpic: set_vector(mpic:@%p,virq:%d,src:%d,vector:0x%x)\n",
	    mpic, virq, src, vector);

	if (src >= mpic->irq_count)
		return;

	vecpri = mpic_irq_read(src, MPIC_INFO(IRQ_VECTOR_PRI));
	vecpri = vecpri & ~MPIC_INFO(VECPRI_VECTOR_MASK);
	vecpri |= vector;
	mpic_irq_write(src, MPIC_INFO(IRQ_VECTOR_PRI), vecpri);
}

void mpic_set_destination(unsigned int virq, unsigned int cpuid)
{
	struct mpic *mpic = mpic_from_irq(virq);
	unsigned int src = mpic_irq_to_hw(virq);

	DBG("mpic: set_destination(mpic:@%p,virq:%d,src:%d,cpuid:0x%x)\n",
	    mpic, virq, src, cpuid);

	if (src >= mpic->irq_count)
		return;

	mpic_irq_write(src, MPIC_INFO(IRQ_DESTINATION), 1 << cpuid);
}

static struct irq_chip mpic_irq_chip = {
	.irq_mask	= mpic_mask_irq,
	.irq_unmask	= mpic_unmask_irq,
	.irq_eoi	= mpic_end_irq,
	.irq_set_type	= mpic_set_irq_type,
};

#ifdef CONFIG_SMP
static struct irq_chip mpic_ipi_chip = {
	.irq_mask	= mpic_mask_ipi,
	.irq_unmask	= mpic_unmask_ipi,
	.irq_eoi	= mpic_end_ipi,
};
#endif /* CONFIG_SMP */

#ifdef CONFIG_MPIC_U3_HT_IRQS
static struct irq_chip mpic_irq_ht_chip = {
	.irq_startup	= mpic_startup_ht_irq,
	.irq_shutdown	= mpic_shutdown_ht_irq,
	.irq_mask	= mpic_mask_irq,
	.irq_unmask	= mpic_unmask_ht_irq,
	.irq_eoi	= mpic_end_ht_irq,
	.irq_set_type	= mpic_set_irq_type,
};
#endif /* CONFIG_MPIC_U3_HT_IRQS */


static int mpic_host_match(struct irq_host *h, struct device_node *node)
{
	/* Exact match, unless mpic node is NULL */
	return h->of_node == NULL || h->of_node == node;
}

static int mpic_host_map(struct irq_host *h, unsigned int virq,
			 irq_hw_number_t hw)
{
	struct mpic *mpic = h->host_data;
	struct irq_chip *chip;

	DBG("mpic: map virq %d, hwirq 0x%lx\n", virq, hw);

	if (hw == mpic->spurious_vec)
		return -EINVAL;
	if (mpic->protected && test_bit(hw, mpic->protected))
		return -EINVAL;

#ifdef CONFIG_SMP
	else if (hw >= mpic->ipi_vecs[0]) {
		WARN_ON(!(mpic->flags & MPIC_PRIMARY));

		DBG("mpic: mapping as IPI\n");
		irq_set_chip_data(virq, mpic);
		irq_set_chip_and_handler(virq, &mpic->hc_ipi,
					 handle_percpu_irq);
		return 0;
	}
#endif /* CONFIG_SMP */

	if (hw >= mpic->irq_count)
		return -EINVAL;

	mpic_msi_reserve_hwirq(mpic, hw);

	/* Default chip */
	chip = &mpic->hc_irq;

#ifdef CONFIG_MPIC_U3_HT_IRQS
	/* Check for HT interrupts, override vecpri */
	if (mpic_is_ht_interrupt(mpic, hw))
		chip = &mpic->hc_ht_irq;
#endif /* CONFIG_MPIC_U3_HT_IRQS */

	DBG("mpic: mapping to irq chip @%p\n", chip);

	irq_set_chip_data(virq, mpic);
	irq_set_chip_and_handler(virq, chip, handle_fasteoi_irq);

	/* Set default irq type */
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	/* If the MPIC was reset, then all vectors have already been
	 * initialized.  Otherwise, a per source lazy initialization
	 * is done here.
	 */
	if (!mpic_is_ipi(mpic, hw) && (mpic->flags & MPIC_NO_RESET)) {
		mpic_set_vector(virq, hw);
		mpic_set_destination(virq, mpic_processor_id(mpic));
		mpic_irq_set_priority(virq, 8);
	}

	return 0;
}

static int mpic_host_xlate(struct irq_host *h, struct device_node *ct,
			   const u32 *intspec, unsigned int intsize,
			   irq_hw_number_t *out_hwirq, unsigned int *out_flags)

{
	static unsigned char map_mpic_senses[4] = {
		IRQ_TYPE_EDGE_RISING,
		IRQ_TYPE_LEVEL_LOW,
		IRQ_TYPE_LEVEL_HIGH,
		IRQ_TYPE_EDGE_FALLING,
	};

	*out_hwirq = intspec[0];
	if (intsize > 1) {
		u32 mask = 0x3;

		/* Apple invented a new race of encoding on machines with
		 * an HT APIC. They encode, among others, the index within
		 * the HT APIC. We don't care about it here since thankfully,
		 * it appears that they have the APIC already properly
		 * configured, and thus our current fixup code that reads the
		 * APIC config works fine. However, we still need to mask out
		 * bits in the specifier to make sure we only get bit 0 which
		 * is the level/edge bit (the only sense bit exposed by Apple),
		 * as their bit 1 means something else.
		 */
		if (machine_is(powermac))
			mask = 0x1;
		*out_flags = map_mpic_senses[intspec[1] & mask];
	} else
		*out_flags = IRQ_TYPE_NONE;

	DBG("mpic: xlate (%d cells: 0x%08x 0x%08x) to line 0x%lx sense 0x%x\n",
	    intsize, intspec[0], intspec[1], *out_hwirq, *out_flags);

	return 0;
}

static struct irq_host_ops mpic_host_ops = {
	.match = mpic_host_match,
	.map = mpic_host_map,
	.xlate = mpic_host_xlate,
};

static int mpic_reset_prohibited(struct device_node *node)
{
	return node && of_get_property(node, "pic-no-reset", NULL);
}

/*
 * Exported functions
 */

struct mpic * __init mpic_alloc(struct device_node *node,
				phys_addr_t phys_addr,
				unsigned int flags,
				unsigned int isu_size,
				unsigned int irq_count,
				const char *name)
{
	struct mpic	*mpic;
	u32		greg_feature;
	const char	*vers;
	int		i;
	int		intvec_top;
	u64		paddr = phys_addr;

	mpic = kzalloc(sizeof(struct mpic), GFP_KERNEL);
	if (mpic == NULL)
		return NULL;

	mpic->name = name;

	mpic->hc_irq = mpic_irq_chip;
	mpic->hc_irq.name = name;
	if (flags & MPIC_PRIMARY)
		mpic->hc_irq.irq_set_affinity = mpic_set_affinity;
#ifdef CONFIG_MPIC_U3_HT_IRQS
	mpic->hc_ht_irq = mpic_irq_ht_chip;
	mpic->hc_ht_irq.name = name;
	if (flags & MPIC_PRIMARY)
		mpic->hc_ht_irq.irq_set_affinity = mpic_set_affinity;
#endif /* CONFIG_MPIC_U3_HT_IRQS */

#ifdef CONFIG_SMP
	mpic->hc_ipi = mpic_ipi_chip;
	mpic->hc_ipi.name = name;
#endif /* CONFIG_SMP */

	mpic->flags = flags;
	mpic->isu_size = isu_size;
	mpic->irq_count = irq_count;
	mpic->num_sources = 0; /* so far */

	if (flags & MPIC_LARGE_VECTORS)
		intvec_top = 2047;
	else
		intvec_top = 255;

	mpic->timer_vecs[0] = intvec_top - 8;
	mpic->timer_vecs[1] = intvec_top - 7;
	mpic->timer_vecs[2] = intvec_top - 6;
	mpic->timer_vecs[3] = intvec_top - 5;
	mpic->ipi_vecs[0]   = intvec_top - 4;
	mpic->ipi_vecs[1]   = intvec_top - 3;
	mpic->ipi_vecs[2]   = intvec_top - 2;
	mpic->ipi_vecs[3]   = intvec_top - 1;
	mpic->spurious_vec  = intvec_top;

	/* Check for "big-endian" in device-tree */
	if (node && of_get_property(node, "big-endian", NULL) != NULL)
		mpic->flags |= MPIC_BIG_ENDIAN;

	/* Look for protected sources */
	if (node) {
		int psize;
		unsigned int bits, mapsize;
		const u32 *psrc =
			of_get_property(node, "protected-sources", &psize);
		if (psrc) {
			psize /= 4;
			bits = intvec_top + 1;
			mapsize = BITS_TO_LONGS(bits) * sizeof(unsigned long);
			mpic->protected = kzalloc(mapsize, GFP_KERNEL);
			BUG_ON(mpic->protected == NULL);
			for (i = 0; i < psize; i++) {
				if (psrc[i] > intvec_top)
					continue;
				__set_bit(psrc[i], mpic->protected);
			}
		}
	}

#ifdef CONFIG_MPIC_WEIRD
	mpic->hw_set = mpic_infos[MPIC_GET_REGSET(flags)];
#endif

	/* default register type */
	mpic->reg_type = (flags & MPIC_BIG_ENDIAN) ?
		mpic_access_mmio_be : mpic_access_mmio_le;

	/* If no physical address is passed in, a device-node is mandatory */
	BUG_ON(paddr == 0 && node == NULL);

	/* If no physical address passed in, check if it's dcr based */
	if (paddr == 0 && of_get_property(node, "dcr-reg", NULL) != NULL) {
#ifdef CONFIG_PPC_DCR
		mpic->flags |= MPIC_USES_DCR;
		mpic->reg_type = mpic_access_dcr;
#else
		BUG();
#endif /* CONFIG_PPC_DCR */
	}

	/* If the MPIC is not DCR based, and no physical address was passed
	 * in, try to obtain one
	 */
	if (paddr == 0 && !(mpic->flags & MPIC_USES_DCR)) {
		const u32 *reg = of_get_property(node, "reg", NULL);
		BUG_ON(reg == NULL);
		paddr = of_translate_address(node, reg);
		BUG_ON(paddr == OF_BAD_ADDR);
	}

	/* Map the global registers */
	mpic_map(mpic, node, paddr, &mpic->gregs, MPIC_INFO(GREG_BASE), 0x1000);
	mpic_map(mpic, node, paddr, &mpic->tmregs, MPIC_INFO(TIMER_BASE), 0x1000);

	/* Reset */

	/* When using a device-node, reset requests are only honored if the MPIC
	 * is allowed to reset.
	 */
	if (mpic_reset_prohibited(node))
		mpic->flags |= MPIC_NO_RESET;

	if ((flags & MPIC_WANTS_RESET) && !(mpic->flags & MPIC_NO_RESET)) {
		printk(KERN_DEBUG "mpic: Resetting\n");
		mpic_write(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0),
			   mpic_read(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0))
			   | MPIC_GREG_GCONF_RESET);
		while( mpic_read(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0))
		       & MPIC_GREG_GCONF_RESET)
			mb();
	}

	/* CoreInt */
	if (flags & MPIC_ENABLE_COREINT)
		mpic_write(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0),
			   mpic_read(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0))
			   | MPIC_GREG_GCONF_COREINT);

	if (flags & MPIC_ENABLE_MCK)
		mpic_write(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0),
			   mpic_read(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0))
			   | MPIC_GREG_GCONF_MCK);

	/* Read feature register, calculate num CPUs and, for non-ISU
	 * MPICs, num sources as well. On ISU MPICs, sources are counted
	 * as ISUs are added
	 */
	greg_feature = mpic_read(mpic->gregs, MPIC_INFO(GREG_FEATURE_0));
	mpic->num_cpus = ((greg_feature & MPIC_GREG_FEATURE_LAST_CPU_MASK)
			  >> MPIC_GREG_FEATURE_LAST_CPU_SHIFT) + 1;
	if (isu_size == 0) {
		if (flags & MPIC_BROKEN_FRR_NIRQS)
			mpic->num_sources = mpic->irq_count;
		else
			mpic->num_sources =
				((greg_feature & MPIC_GREG_FEATURE_LAST_SRC_MASK)
				 >> MPIC_GREG_FEATURE_LAST_SRC_SHIFT) + 1;
	}

	/* Map the per-CPU registers */
	for (i = 0; i < mpic->num_cpus; i++) {
		mpic_map(mpic, node, paddr, &mpic->cpuregs[i],
			 MPIC_INFO(CPU_BASE) + i * MPIC_INFO(CPU_STRIDE),
			 0x1000);
	}

	/* Initialize main ISU if none provided */
	if (mpic->isu_size == 0) {
		mpic->isu_size = mpic->num_sources;
		mpic_map(mpic, node, paddr, &mpic->isus[0],
			 MPIC_INFO(IRQ_BASE), MPIC_INFO(IRQ_STRIDE) * mpic->isu_size);
	}
	mpic->isu_shift = 1 + __ilog2(mpic->isu_size - 1);
	mpic->isu_mask = (1 << mpic->isu_shift) - 1;

	mpic->irqhost = irq_alloc_host(node, IRQ_HOST_MAP_LINEAR,
				       isu_size ? isu_size : mpic->num_sources,
				       &mpic_host_ops,
				       flags & MPIC_LARGE_VECTORS ? 2048 : 256);
	if (mpic->irqhost == NULL)
		return NULL;

	mpic->irqhost->host_data = mpic;

	/* Display version */
	switch (greg_feature & MPIC_GREG_FEATURE_VERSION_MASK) {
	case 1:
		vers = "1.0";
		break;
	case 2:
		vers = "1.2";
		break;
	case 3:
		vers = "1.3";
		break;
	default:
		vers = "<unknown>";
		break;
	}
	printk(KERN_INFO "mpic: Setting up MPIC \"%s\" version %s at %llx,"
	       " max %d CPUs\n",
	       name, vers, (unsigned long long)paddr, mpic->num_cpus);
	printk(KERN_INFO "mpic: ISU size: %d, shift: %d, mask: %x\n",
	       mpic->isu_size, mpic->isu_shift, mpic->isu_mask);

	mpic->next = mpics;
	mpics = mpic;

	if (flags & MPIC_PRIMARY) {
		mpic_primary = mpic;
		irq_set_default_host(mpic->irqhost);
	}

	return mpic;
}

void __init mpic_assign_isu(struct mpic *mpic, unsigned int isu_num,
			    phys_addr_t paddr)
{
	unsigned int isu_first = isu_num * mpic->isu_size;

	BUG_ON(isu_num >= MPIC_MAX_ISU);

	mpic_map(mpic, mpic->irqhost->of_node,
		 paddr, &mpic->isus[isu_num], 0,
		 MPIC_INFO(IRQ_STRIDE) * mpic->isu_size);

	if ((isu_first + mpic->isu_size) > mpic->num_sources)
		mpic->num_sources = isu_first + mpic->isu_size;
}

void __init mpic_set_default_senses(struct mpic *mpic, u8 *senses, int count)
{
	mpic->senses = senses;
	mpic->senses_count = count;
}

void __init mpic_init(struct mpic *mpic)
{
	int i;
	int cpu;

	BUG_ON(mpic->num_sources == 0);

	printk(KERN_INFO "mpic: Initializing for %d sources\n", mpic->num_sources);

	/* Set current processor priority to max */
	mpic_cpu_write(MPIC_INFO(CPU_CURRENT_TASK_PRI), 0xf);

	/* Initialize timers: just disable them all */
	for (i = 0; i < 4; i++) {
		mpic_write(mpic->tmregs,
			   i * MPIC_INFO(TIMER_STRIDE) +
			   MPIC_INFO(TIMER_DESTINATION), 0);
		mpic_write(mpic->tmregs,
			   i * MPIC_INFO(TIMER_STRIDE) +
			   MPIC_INFO(TIMER_VECTOR_PRI),
			   MPIC_VECPRI_MASK |
			   (mpic->timer_vecs[0] + i));
	}

	/* Initialize IPIs to our reserved vectors and mark them disabled for now */
	mpic_test_broken_ipi(mpic);
	for (i = 0; i < 4; i++) {
		mpic_ipi_write(i,
			       MPIC_VECPRI_MASK |
			       (10 << MPIC_VECPRI_PRIORITY_SHIFT) |
			       (mpic->ipi_vecs[0] + i));
	}

	/* Initialize interrupt sources */
	if (mpic->irq_count == 0)
		mpic->irq_count = mpic->num_sources;

	/* Do the HT PIC fixups on U3 broken mpic */
	DBG("MPIC flags: %x\n", mpic->flags);
	if ((mpic->flags & MPIC_U3_HT_IRQS) && (mpic->flags & MPIC_PRIMARY)) {
		mpic_scan_ht_pics(mpic);
		mpic_u3msi_init(mpic);
	}

	mpic_pasemi_msi_init(mpic);

	cpu = mpic_processor_id(mpic);

	if (!(mpic->flags & MPIC_NO_RESET)) {
		for (i = 0; i < mpic->num_sources; i++) {
			/* start with vector = source number, and masked */
			u32 vecpri = MPIC_VECPRI_MASK | i |
				(8 << MPIC_VECPRI_PRIORITY_SHIFT);
		
			/* check if protected */
			if (mpic->protected && test_bit(i, mpic->protected))
				continue;
			/* init hw */
			mpic_irq_write(i, MPIC_INFO(IRQ_VECTOR_PRI), vecpri);
			mpic_irq_write(i, MPIC_INFO(IRQ_DESTINATION), 1 << cpu);
		}
	}
	
	/* Init spurious vector */
	mpic_write(mpic->gregs, MPIC_INFO(GREG_SPURIOUS), mpic->spurious_vec);

	/* Disable 8259 passthrough, if supported */
	if (!(mpic->flags & MPIC_NO_PTHROU_DIS))
		mpic_write(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0),
			   mpic_read(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0))
			   | MPIC_GREG_GCONF_8259_PTHROU_DIS);

	if (mpic->flags & MPIC_NO_BIAS)
		mpic_write(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0),
			mpic_read(mpic->gregs, MPIC_INFO(GREG_GLOBAL_CONF_0))
			| MPIC_GREG_GCONF_NO_BIAS);

	/* Set current processor priority to 0 */
	mpic_cpu_write(MPIC_INFO(CPU_CURRENT_TASK_PRI), 0);

#ifdef CONFIG_PM
	/* allocate memory to save mpic state */
	mpic->save_data = kmalloc(mpic->num_sources * sizeof(*mpic->save_data),
				  GFP_KERNEL);
	BUG_ON(mpic->save_data == NULL);
#endif
}

void __init mpic_set_clk_ratio(struct mpic *mpic, u32 clock_ratio)
{
	u32 v;

	v = mpic_read(mpic->gregs, MPIC_GREG_GLOBAL_CONF_1);
	v &= ~MPIC_GREG_GLOBAL_CONF_1_CLK_RATIO_MASK;
	v |= MPIC_GREG_GLOBAL_CONF_1_CLK_RATIO(clock_ratio);
	mpic_write(mpic->gregs, MPIC_GREG_GLOBAL_CONF_1, v);
}

void __init mpic_set_serial_int(struct mpic *mpic, int enable)
{
	unsigned long flags;
	u32 v;

	raw_spin_lock_irqsave(&mpic_lock, flags);
	v = mpic_read(mpic->gregs, MPIC_GREG_GLOBAL_CONF_1);
	if (enable)
		v |= MPIC_GREG_GLOBAL_CONF_1_SIE;
	else
		v &= ~MPIC_GREG_GLOBAL_CONF_1_SIE;
	mpic_write(mpic->gregs, MPIC_GREG_GLOBAL_CONF_1, v);
	raw_spin_unlock_irqrestore(&mpic_lock, flags);
}

void mpic_irq_set_priority(unsigned int irq, unsigned int pri)
{
	struct mpic *mpic = mpic_find(irq);
	unsigned int src = mpic_irq_to_hw(irq);
	unsigned long flags;
	u32 reg;

	if (!mpic)
		return;

	raw_spin_lock_irqsave(&mpic_lock, flags);
	if (mpic_is_ipi(mpic, irq)) {
		reg = mpic_ipi_read(src - mpic->ipi_vecs[0]) &
			~MPIC_VECPRI_PRIORITY_MASK;
		mpic_ipi_write(src - mpic->ipi_vecs[0],
			       reg | (pri << MPIC_VECPRI_PRIORITY_SHIFT));
	} else {
		reg = mpic_irq_read(src, MPIC_INFO(IRQ_VECTOR_PRI))
			& ~MPIC_VECPRI_PRIORITY_MASK;
		mpic_irq_write(src, MPIC_INFO(IRQ_VECTOR_PRI),
			       reg | (pri << MPIC_VECPRI_PRIORITY_SHIFT));
	}
	raw_spin_unlock_irqrestore(&mpic_lock, flags);
}

void mpic_setup_this_cpu(void)
{
#ifdef CONFIG_SMP
	struct mpic *mpic = mpic_primary;
	unsigned long flags;
	u32 msk = 1 << hard_smp_processor_id();
	unsigned int i;

	BUG_ON(mpic == NULL);

	DBG("%s: setup_this_cpu(%d)\n", mpic->name, hard_smp_processor_id());

	raw_spin_lock_irqsave(&mpic_lock, flags);

 	/* let the mpic know we want intrs. default affinity is 0xffffffff
	 * until changed via /proc. That's how it's done on x86. If we want
	 * it differently, then we should make sure we also change the default
	 * values of irq_desc[].affinity in irq.c.
 	 */
	if (distribute_irqs) {
	 	for (i = 0; i < mpic->num_sources ; i++)
			mpic_irq_write(i, MPIC_INFO(IRQ_DESTINATION),
				mpic_irq_read(i, MPIC_INFO(IRQ_DESTINATION)) | msk);
	}

	/* Set current processor priority to 0 */
	mpic_cpu_write(MPIC_INFO(CPU_CURRENT_TASK_PRI), 0);

	raw_spin_unlock_irqrestore(&mpic_lock, flags);
#endif /* CONFIG_SMP */
}

int mpic_cpu_get_priority(void)
{
	struct mpic *mpic = mpic_primary;

	return mpic_cpu_read(MPIC_INFO(CPU_CURRENT_TASK_PRI));
}

void mpic_cpu_set_priority(int prio)
{
	struct mpic *mpic = mpic_primary;

	prio &= MPIC_CPU_TASKPRI_MASK;
	mpic_cpu_write(MPIC_INFO(CPU_CURRENT_TASK_PRI), prio);
}

void mpic_teardown_this_cpu(int secondary)
{
	struct mpic *mpic = mpic_primary;
	unsigned long flags;
	u32 msk = 1 << hard_smp_processor_id();
	unsigned int i;

	BUG_ON(mpic == NULL);

	DBG("%s: teardown_this_cpu(%d)\n", mpic->name, hard_smp_processor_id());
	raw_spin_lock_irqsave(&mpic_lock, flags);

	/* let the mpic know we don't want intrs.  */
	for (i = 0; i < mpic->num_sources ; i++)
		mpic_irq_write(i, MPIC_INFO(IRQ_DESTINATION),
			mpic_irq_read(i, MPIC_INFO(IRQ_DESTINATION)) & ~msk);

	/* Set current processor priority to max */
	mpic_cpu_write(MPIC_INFO(CPU_CURRENT_TASK_PRI), 0xf);
	/* We need to EOI the IPI since not all platforms reset the MPIC
	 * on boot and new interrupts wouldn't get delivered otherwise.
	 */
	mpic_eoi(mpic);

	raw_spin_unlock_irqrestore(&mpic_lock, flags);
}


static unsigned int _mpic_get_one_irq(struct mpic *mpic, int reg)
{
	u32 src;

	src = mpic_cpu_read(reg) & MPIC_INFO(VECPRI_VECTOR_MASK);
#ifdef DEBUG_LOW
	DBG("%s: get_one_irq(reg 0x%x): %d\n", mpic->name, reg, src);
#endif
	if (unlikely(src == mpic->spurious_vec)) {
		if (mpic->flags & MPIC_SPV_EOI)
			mpic_eoi(mpic);
		return NO_IRQ;
	}
	if (unlikely(mpic->protected && test_bit(src, mpic->protected))) {
		if (printk_ratelimit())
			printk(KERN_WARNING "%s: Got protected source %d !\n",
			       mpic->name, (int)src);
		mpic_eoi(mpic);
		return NO_IRQ;
	}

	return irq_linear_revmap(mpic->irqhost, src);
}

unsigned int mpic_get_one_irq(struct mpic *mpic)
{
	return _mpic_get_one_irq(mpic, MPIC_INFO(CPU_INTACK));
}

unsigned int mpic_get_irq(void)
{
	struct mpic *mpic = mpic_primary;

	BUG_ON(mpic == NULL);

	return mpic_get_one_irq(mpic);
}

unsigned int mpic_get_coreint_irq(void)
{
#ifdef CONFIG_BOOKE
	struct mpic *mpic = mpic_primary;
	u32 src;

	BUG_ON(mpic == NULL);

	src = mfspr(SPRN_EPR);

	if (unlikely(src == mpic->spurious_vec)) {
		if (mpic->flags & MPIC_SPV_EOI)
			mpic_eoi(mpic);
		return NO_IRQ;
	}
	if (unlikely(mpic->protected && test_bit(src, mpic->protected))) {
		if (printk_ratelimit())
			printk(KERN_WARNING "%s: Got protected source %d !\n",
			       mpic->name, (int)src);
		return NO_IRQ;
	}

	return irq_linear_revmap(mpic->irqhost, src);
#else
	return NO_IRQ;
#endif
}

unsigned int mpic_get_mcirq(void)
{
	struct mpic *mpic = mpic_primary;

	BUG_ON(mpic == NULL);

	return _mpic_get_one_irq(mpic, MPIC_INFO(CPU_MCACK));
}

#ifdef CONFIG_SMP
void mpic_request_ipis(void)
{
	struct mpic *mpic = mpic_primary;
	int i;
	BUG_ON(mpic == NULL);

	printk(KERN_INFO "mpic: requesting IPIs...\n");

	for (i = 0; i < 4; i++) {
		unsigned int vipi = irq_create_mapping(mpic->irqhost,
						       mpic->ipi_vecs[0] + i);
		if (vipi == NO_IRQ) {
			printk(KERN_ERR "Failed to map %s\n", smp_ipi_name[i]);
			continue;
		}
		smp_request_message_ipi(vipi, i);
	}
}

static void mpic_send_ipi(unsigned int ipi_no, const struct cpumask *cpu_mask)
{
	struct mpic *mpic = mpic_primary;

	BUG_ON(mpic == NULL);

#ifdef DEBUG_IPI
	DBG("%s: send_ipi(ipi_no: %d)\n", mpic->name, ipi_no);
#endif

	mpic_cpu_write(MPIC_INFO(CPU_IPI_DISPATCH_0) +
		       ipi_no * MPIC_INFO(CPU_IPI_DISPATCH_STRIDE),
		       mpic_physmask(cpumask_bits(cpu_mask)[0]));
}

void smp_mpic_message_pass(int target, int msg)
{
	cpumask_var_t tmp;

	/* make sure we're sending something that translates to an IPI */
	if ((unsigned int)msg > 3) {
		printk("SMP %d: smp_message_pass: unknown msg %d\n",
		       smp_processor_id(), msg);
		return;
	}
	switch (target) {
	case MSG_ALL:
		mpic_send_ipi(msg, cpu_online_mask);
		break;
	case MSG_ALL_BUT_SELF:
		alloc_cpumask_var(&tmp, GFP_NOWAIT);
		cpumask_andnot(tmp, cpu_online_mask,
			       cpumask_of(smp_processor_id()));
		mpic_send_ipi(msg, tmp);
		free_cpumask_var(tmp);
		break;
	default:
		mpic_send_ipi(msg, cpumask_of(target));
		break;
	}
}

int __init smp_mpic_probe(void)
{
	int nr_cpus;

	DBG("smp_mpic_probe()...\n");

	nr_cpus = cpumask_weight(cpu_possible_mask);

	DBG("nr_cpus: %d\n", nr_cpus);

	if (nr_cpus > 1)
		mpic_request_ipis();

	return nr_cpus;
}

void __devinit smp_mpic_setup_cpu(int cpu)
{
	mpic_setup_this_cpu();
}

void mpic_reset_core(int cpu)
{
	struct mpic *mpic = mpic_primary;
	u32 pir;
	int cpuid = get_hard_smp_processor_id(cpu);

	/* Set target bit for core reset */
	pir = mpic_read(mpic->gregs, MPIC_INFO(GREG_PROCESSOR_INIT));
	pir |= (1 << cpuid);
	mpic_write(mpic->gregs, MPIC_INFO(GREG_PROCESSOR_INIT), pir);
	mpic_read(mpic->gregs, MPIC_INFO(GREG_PROCESSOR_INIT));

	/* Restore target bit after reset complete */
	pir &= ~(1 << cpuid);
	mpic_write(mpic->gregs, MPIC_INFO(GREG_PROCESSOR_INIT), pir);
	mpic_read(mpic->gregs, MPIC_INFO(GREG_PROCESSOR_INIT));
}
#endif /* CONFIG_SMP */

#ifdef CONFIG_PM
static int mpic_suspend(struct sys_device *dev, pm_message_t state)
{
	struct mpic *mpic = container_of(dev, struct mpic, sysdev);
	int i;

	for (i = 0; i < mpic->num_sources; i++) {
		mpic->save_data[i].vecprio =
			mpic_irq_read(i, MPIC_INFO(IRQ_VECTOR_PRI));
		mpic->save_data[i].dest =
			mpic_irq_read(i, MPIC_INFO(IRQ_DESTINATION));
	}

	return 0;
}

static int mpic_resume(struct sys_device *dev)
{
	struct mpic *mpic = container_of(dev, struct mpic, sysdev);
	int i;

	for (i = 0; i < mpic->num_sources; i++) {
		mpic_irq_write(i, MPIC_INFO(IRQ_VECTOR_PRI),
			       mpic->save_data[i].vecprio);
		mpic_irq_write(i, MPIC_INFO(IRQ_DESTINATION),
			       mpic->save_data[i].dest);

#ifdef CONFIG_MPIC_U3_HT_IRQS
	if (mpic->fixups) {
		struct mpic_irq_fixup *fixup = &mpic->fixups[i];

		if (fixup->base) {
			/* we use the lowest bit in an inverted meaning */
			if ((mpic->save_data[i].fixup_data & 1) == 0)
				continue;

			/* Enable and configure */
			writeb(0x10 + 2 * fixup->index, fixup->base + 2);

			writel(mpic->save_data[i].fixup_data & ~1,
			       fixup->base + 4);
		}
	}
#endif
	} /* end for loop */

	return 0;
}
#endif

static struct sysdev_class mpic_sysclass = {
#ifdef CONFIG_PM
	.resume = mpic_resume,
	.suspend = mpic_suspend,
#endif
	.name = "mpic",
};

static int mpic_init_sys(void)
{
	struct mpic *mpic = mpics;
	int error, id = 0;

	error = sysdev_class_register(&mpic_sysclass);

	while (mpic && !error) {
		mpic->sysdev.cls = &mpic_sysclass;
		mpic->sysdev.id = id++;
		error = sysdev_register(&mpic->sysdev);
		mpic = mpic->next;
	}
	return error;
}

device_initcall(mpic_init_sys);
