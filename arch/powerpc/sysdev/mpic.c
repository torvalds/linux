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

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/bootmem.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/mpic.h>
#include <asm/smp.h>

#ifdef DEBUG
#define DBG(fmt...) printk(fmt)
#else
#define DBG(fmt...)
#endif

static struct mpic *mpics;
static struct mpic *mpic_primary;
static DEFINE_SPINLOCK(mpic_lock);

#ifdef CONFIG_PPC32	/* XXX for now */
#define distribute_irqs	CONFIG_IRQ_ALL_CPUS
#endif

/*
 * Register accessor functions
 */


static inline u32 _mpic_read(unsigned int be, volatile u32 __iomem *base,
			    unsigned int reg)
{
	if (be)
		return in_be32(base + (reg >> 2));
	else
		return in_le32(base + (reg >> 2));
}

static inline void _mpic_write(unsigned int be, volatile u32 __iomem *base,
			      unsigned int reg, u32 value)
{
	if (be)
		out_be32(base + (reg >> 2), value);
	else
		out_le32(base + (reg >> 2), value);
}

static inline u32 _mpic_ipi_read(struct mpic *mpic, unsigned int ipi)
{
	unsigned int be = (mpic->flags & MPIC_BIG_ENDIAN) != 0;
	unsigned int offset = MPIC_GREG_IPI_VECTOR_PRI_0 + (ipi * 0x10);

	if (mpic->flags & MPIC_BROKEN_IPI)
		be = !be;
	return _mpic_read(be, mpic->gregs, offset);
}

static inline void _mpic_ipi_write(struct mpic *mpic, unsigned int ipi, u32 value)
{
	unsigned int offset = MPIC_GREG_IPI_VECTOR_PRI_0 + (ipi * 0x10);

	_mpic_write(mpic->flags & MPIC_BIG_ENDIAN, mpic->gregs, offset, value);
}

static inline u32 _mpic_cpu_read(struct mpic *mpic, unsigned int reg)
{
	unsigned int cpu = 0;

	if (mpic->flags & MPIC_PRIMARY)
		cpu = hard_smp_processor_id();

	return _mpic_read(mpic->flags & MPIC_BIG_ENDIAN, mpic->cpuregs[cpu], reg);
}

static inline void _mpic_cpu_write(struct mpic *mpic, unsigned int reg, u32 value)
{
	unsigned int cpu = 0;

	if (mpic->flags & MPIC_PRIMARY)
		cpu = hard_smp_processor_id();

	_mpic_write(mpic->flags & MPIC_BIG_ENDIAN, mpic->cpuregs[cpu], reg, value);
}

static inline u32 _mpic_irq_read(struct mpic *mpic, unsigned int src_no, unsigned int reg)
{
	unsigned int	isu = src_no >> mpic->isu_shift;
	unsigned int	idx = src_no & mpic->isu_mask;

	return _mpic_read(mpic->flags & MPIC_BIG_ENDIAN, mpic->isus[isu],
			  reg + (idx * MPIC_IRQ_STRIDE));
}

static inline void _mpic_irq_write(struct mpic *mpic, unsigned int src_no,
				   unsigned int reg, u32 value)
{
	unsigned int	isu = src_no >> mpic->isu_shift;
	unsigned int	idx = src_no & mpic->isu_mask;

	_mpic_write(mpic->flags & MPIC_BIG_ENDIAN, mpic->isus[isu],
		    reg + (idx * MPIC_IRQ_STRIDE), value);
}

#define mpic_read(b,r)		_mpic_read(mpic->flags & MPIC_BIG_ENDIAN,(b),(r))
#define mpic_write(b,r,v)	_mpic_write(mpic->flags & MPIC_BIG_ENDIAN,(b),(r),(v))
#define mpic_ipi_read(i)	_mpic_ipi_read(mpic,(i))
#define mpic_ipi_write(i,v)	_mpic_ipi_write(mpic,(i),(v))
#define mpic_cpu_read(i)	_mpic_cpu_read(mpic,(i))
#define mpic_cpu_write(i,v)	_mpic_cpu_write(mpic,(i),(v))
#define mpic_irq_read(s,r)	_mpic_irq_read(mpic,(s),(r))
#define mpic_irq_write(s,r,v)	_mpic_irq_write(mpic,(s),(r),(v))


/*
 * Low level utility functions
 */



/* Check if we have one of those nice broken MPICs with a flipped endian on
 * reads from IPI registers
 */
static void __init mpic_test_broken_ipi(struct mpic *mpic)
{
	u32 r;

	mpic_write(mpic->gregs, MPIC_GREG_IPI_VECTOR_PRI_0, MPIC_VECPRI_MASK);
	r = mpic_read(mpic->gregs, MPIC_GREG_IPI_VECTOR_PRI_0);

	if (r == le32_to_cpu(MPIC_VECPRI_MASK)) {
		printk(KERN_INFO "mpic: Detected reversed IPI registers\n");
		mpic->flags |= MPIC_BROKEN_IPI;
	}
}

#ifdef CONFIG_MPIC_BROKEN_U3

/* Test if an interrupt is sourced from HyperTransport (used on broken U3s)
 * to force the edge setting on the MPIC and do the ack workaround.
 */
static inline int mpic_is_ht_interrupt(struct mpic *mpic, unsigned int source_no)
{
	if (source_no >= 128 || !mpic->fixups)
		return 0;
	return mpic->fixups[source_no].base != NULL;
}

static inline void mpic_apic_end_irq(struct mpic *mpic, unsigned int source_no)
{
	struct mpic_irq_fixup *fixup = &mpic->fixups[source_no];
	u32 tmp;

	spin_lock(&mpic->fixup_lock);
	writeb(0x11 + 2 * fixup->irq, fixup->base);
	tmp = readl(fixup->base + 2);
	writel(tmp | 0x80000000ul, fixup->base + 2);
	/* config writes shouldn't be posted but let's be safe ... */
	(void)readl(fixup->base + 2);
	spin_unlock(&mpic->fixup_lock);
}


static void __init mpic_amd8111_read_irq(struct mpic *mpic, u8 __iomem *devbase)
{
	int i, irq;
	u32 tmp;

	printk(KERN_INFO "mpic:    - Workarounds on AMD 8111 @ %p\n", devbase);

	for (i=0; i < 24; i++) {
		writeb(0x10 + 2*i, devbase + 0xf2);
		tmp = readl(devbase + 0xf4);
		if ((tmp & 0x1) || !(tmp & 0x20))
			continue;
		irq = (tmp >> 16) & 0xff;
		mpic->fixups[irq].irq = i;
		mpic->fixups[irq].base = devbase + 0xf2;
	}
}
 
static void __init mpic_amd8131_read_irq(struct mpic *mpic, u8 __iomem *devbase)
{
	int i, irq;
	u32 tmp;

	printk(KERN_INFO "mpic:    - Workarounds on AMD 8131 @ %p\n", devbase);

	for (i=0; i < 4; i++) {
		writeb(0x10 + 2*i, devbase + 0xba);
		tmp = readl(devbase + 0xbc);
		if ((tmp & 0x1) || !(tmp & 0x20))
			continue;
		irq = (tmp >> 16) & 0xff;
		mpic->fixups[irq].irq = i;
		mpic->fixups[irq].base = devbase + 0xba;
	}
}
 
static void __init mpic_scan_ioapics(struct mpic *mpic)
{
	unsigned int devfn;
	u8 __iomem *cfgspace;

	printk(KERN_INFO "mpic: Setting up IO-APICs workarounds for U3\n");

	/* Allocate fixups array */
	mpic->fixups = alloc_bootmem(128 * sizeof(struct mpic_irq_fixup));
	BUG_ON(mpic->fixups == NULL);
	memset(mpic->fixups, 0, 128 * sizeof(struct mpic_irq_fixup));

	/* Init spinlock */
	spin_lock_init(&mpic->fixup_lock);

	/* Map u3 config space. We assume all IO-APICs are on the primary bus
	 * and slot will never be above "0xf" so we only need to map 32k
	 */
	cfgspace = (unsigned char __iomem *)ioremap(0xf2000000, 0x8000);
	BUG_ON(cfgspace == NULL);

	/* Now we scan all slots. We do a very quick scan, we read the header type,
	 * vendor ID and device ID only, that's plenty enough
	 */
	for (devfn = 0; devfn < PCI_DEVFN(0x10,0); devfn ++) {
		u8 __iomem *devbase = cfgspace + (devfn << 8);
		u8 hdr_type = readb(devbase + PCI_HEADER_TYPE);
		u32 l = readl(devbase + PCI_VENDOR_ID);
		u16 vendor_id, device_id;
		int multifunc = 0;

		DBG("devfn %x, l: %x\n", devfn, l);

		/* If no device, skip */
		if (l == 0xffffffff || l == 0x00000000 ||
		    l == 0x0000ffff || l == 0xffff0000)
			goto next;

		/* Check if it's a multifunction device (only really used
		 * to function 0 though
		 */
		multifunc = !!(hdr_type & 0x80);
		vendor_id = l & 0xffff;
		device_id = (l >> 16) & 0xffff;

		/* If a known device, go to fixup setup code */
		if (vendor_id == PCI_VENDOR_ID_AMD && device_id == 0x7460)
			mpic_amd8111_read_irq(mpic, devbase);
		if (vendor_id == PCI_VENDOR_ID_AMD && device_id == 0x7450)
			mpic_amd8131_read_irq(mpic, devbase);
	next:
		/* next device, if function 0 */
		if ((PCI_FUNC(devfn) == 0) && !multifunc)
			devfn += 7;
	}
}

#endif /* CONFIG_MPIC_BROKEN_U3 */


/* Find an mpic associated with a given linux interrupt */
static struct mpic *mpic_find(unsigned int irq, unsigned int *is_ipi)
{
	struct mpic *mpic = mpics;

	while(mpic) {
		/* search IPIs first since they may override the main interrupts */
		if (irq >= mpic->ipi_offset && irq < (mpic->ipi_offset + 4)) {
			if (is_ipi)
				*is_ipi = 1;
			return mpic;
		}
		if (irq >= mpic->irq_offset &&
		    irq < (mpic->irq_offset + mpic->irq_count)) {
			if (is_ipi)
				*is_ipi = 0;
			return mpic;
		}
		mpic = mpic -> next;
	}
	return NULL;
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
static inline struct mpic * mpic_from_ipi(unsigned int ipi)
{
	return container_of(irq_desc[ipi].handler, struct mpic, hc_ipi);
}
#endif

/* Get the mpic structure from the irq number */
static inline struct mpic * mpic_from_irq(unsigned int irq)
{
	return container_of(irq_desc[irq].handler, struct mpic, hc_irq);
}

/* Send an EOI */
static inline void mpic_eoi(struct mpic *mpic)
{
	mpic_cpu_write(MPIC_CPU_EOI, 0);
	(void)mpic_cpu_read(MPIC_CPU_WHOAMI);
}

#ifdef CONFIG_SMP
static irqreturn_t mpic_ipi_action(int irq, void *dev_id, struct pt_regs *regs)
{
	struct mpic *mpic = dev_id;

	smp_message_recv(irq - mpic->ipi_offset, regs);
	return IRQ_HANDLED;
}
#endif /* CONFIG_SMP */

/*
 * Linux descriptor level callbacks
 */


static void mpic_enable_irq(unsigned int irq)
{
	unsigned int loops = 100000;
	struct mpic *mpic = mpic_from_irq(irq);
	unsigned int src = irq - mpic->irq_offset;

	DBG("%p: %s: enable_irq: %d (src %d)\n", mpic, mpic->name, irq, src);

	mpic_irq_write(src, MPIC_IRQ_VECTOR_PRI,
		       mpic_irq_read(src, MPIC_IRQ_VECTOR_PRI) & ~MPIC_VECPRI_MASK);

	/* make sure mask gets to controller before we return to user */
	do {
		if (!loops--) {
			printk(KERN_ERR "mpic_enable_irq timeout\n");
			break;
		}
	} while(mpic_irq_read(src, MPIC_IRQ_VECTOR_PRI) & MPIC_VECPRI_MASK);	
}

static void mpic_disable_irq(unsigned int irq)
{
	unsigned int loops = 100000;
	struct mpic *mpic = mpic_from_irq(irq);
	unsigned int src = irq - mpic->irq_offset;

	DBG("%s: disable_irq: %d (src %d)\n", mpic->name, irq, src);

	mpic_irq_write(src, MPIC_IRQ_VECTOR_PRI,
		       mpic_irq_read(src, MPIC_IRQ_VECTOR_PRI) | MPIC_VECPRI_MASK);

	/* make sure mask gets to controller before we return to user */
	do {
		if (!loops--) {
			printk(KERN_ERR "mpic_enable_irq timeout\n");
			break;
		}
	} while(!(mpic_irq_read(src, MPIC_IRQ_VECTOR_PRI) & MPIC_VECPRI_MASK));
}

static void mpic_end_irq(unsigned int irq)
{
	struct mpic *mpic = mpic_from_irq(irq);

	DBG("%s: end_irq: %d\n", mpic->name, irq);

	/* We always EOI on end_irq() even for edge interrupts since that
	 * should only lower the priority, the MPIC should have properly
	 * latched another edge interrupt coming in anyway
	 */

#ifdef CONFIG_MPIC_BROKEN_U3
	if (mpic->flags & MPIC_BROKEN_U3) {
		unsigned int src = irq - mpic->irq_offset;
		if (mpic_is_ht_interrupt(mpic, src))
			mpic_apic_end_irq(mpic, src);
	}
#endif /* CONFIG_MPIC_BROKEN_U3 */

	mpic_eoi(mpic);
}

#ifdef CONFIG_SMP

static void mpic_enable_ipi(unsigned int irq)
{
	struct mpic *mpic = mpic_from_ipi(irq);
	unsigned int src = irq - mpic->ipi_offset;

	DBG("%s: enable_ipi: %d (ipi %d)\n", mpic->name, irq, src);
	mpic_ipi_write(src, mpic_ipi_read(src) & ~MPIC_VECPRI_MASK);
}

static void mpic_disable_ipi(unsigned int irq)
{
	/* NEVER disable an IPI... that's just plain wrong! */
}

static void mpic_end_ipi(unsigned int irq)
{
	struct mpic *mpic = mpic_from_ipi(irq);

	/*
	 * IPIs are marked IRQ_PER_CPU. This has the side effect of
	 * preventing the IRQ_PENDING/IRQ_INPROGRESS logic from
	 * applying to them. We EOI them late to avoid re-entering.
	 * We mark IPI's with SA_INTERRUPT as they must run with
	 * irqs disabled.
	 */
	mpic_eoi(mpic);
}

#endif /* CONFIG_SMP */

static void mpic_set_affinity(unsigned int irq, cpumask_t cpumask)
{
	struct mpic *mpic = mpic_from_irq(irq);

	cpumask_t tmp;

	cpus_and(tmp, cpumask, cpu_online_map);

	mpic_irq_write(irq - mpic->irq_offset, MPIC_IRQ_DESTINATION,
		       mpic_physmask(cpus_addr(tmp)[0]));	
}


/*
 * Exported functions
 */


struct mpic * __init mpic_alloc(unsigned long phys_addr,
				unsigned int flags,
				unsigned int isu_size,
				unsigned int irq_offset,
				unsigned int irq_count,
				unsigned int ipi_offset,
				unsigned char *senses,
				unsigned int senses_count,
				const char *name)
{
	struct mpic	*mpic;
	u32		reg;
	const char	*vers;
	int		i;

	mpic = alloc_bootmem(sizeof(struct mpic));
	if (mpic == NULL)
		return NULL;
	

	memset(mpic, 0, sizeof(struct mpic));
	mpic->name = name;

	mpic->hc_irq.typename = name;
	mpic->hc_irq.enable = mpic_enable_irq;
	mpic->hc_irq.disable = mpic_disable_irq;
	mpic->hc_irq.end = mpic_end_irq;
	if (flags & MPIC_PRIMARY)
		mpic->hc_irq.set_affinity = mpic_set_affinity;
#ifdef CONFIG_SMP
	mpic->hc_ipi.typename = name;
	mpic->hc_ipi.enable = mpic_enable_ipi;
	mpic->hc_ipi.disable = mpic_disable_ipi;
	mpic->hc_ipi.end = mpic_end_ipi;
#endif /* CONFIG_SMP */

	mpic->flags = flags;
	mpic->isu_size = isu_size;
	mpic->irq_offset = irq_offset;
	mpic->irq_count = irq_count;
	mpic->ipi_offset = ipi_offset;
	mpic->num_sources = 0; /* so far */
	mpic->senses = senses;
	mpic->senses_count = senses_count;

	/* Map the global registers */
	mpic->gregs = ioremap(phys_addr + MPIC_GREG_BASE, 0x1000);
	mpic->tmregs = mpic->gregs + ((MPIC_TIMER_BASE - MPIC_GREG_BASE) >> 2);
	BUG_ON(mpic->gregs == NULL);

	/* Reset */
	if (flags & MPIC_WANTS_RESET) {
		mpic_write(mpic->gregs, MPIC_GREG_GLOBAL_CONF_0,
			   mpic_read(mpic->gregs, MPIC_GREG_GLOBAL_CONF_0)
			   | MPIC_GREG_GCONF_RESET);
		while( mpic_read(mpic->gregs, MPIC_GREG_GLOBAL_CONF_0)
		       & MPIC_GREG_GCONF_RESET)
			mb();
	}

	/* Read feature register, calculate num CPUs and, for non-ISU
	 * MPICs, num sources as well. On ISU MPICs, sources are counted
	 * as ISUs are added
	 */
	reg = mpic_read(mpic->gregs, MPIC_GREG_FEATURE_0);
	mpic->num_cpus = ((reg & MPIC_GREG_FEATURE_LAST_CPU_MASK)
			  >> MPIC_GREG_FEATURE_LAST_CPU_SHIFT) + 1;
	if (isu_size == 0)
		mpic->num_sources = ((reg & MPIC_GREG_FEATURE_LAST_SRC_MASK)
				     >> MPIC_GREG_FEATURE_LAST_SRC_SHIFT) + 1;

	/* Map the per-CPU registers */
	for (i = 0; i < mpic->num_cpus; i++) {
		mpic->cpuregs[i] = ioremap(phys_addr + MPIC_CPU_BASE +
					   i * MPIC_CPU_STRIDE, 0x1000);
		BUG_ON(mpic->cpuregs[i] == NULL);
	}

	/* Initialize main ISU if none provided */
	if (mpic->isu_size == 0) {
		mpic->isu_size = mpic->num_sources;
		mpic->isus[0] = ioremap(phys_addr + MPIC_IRQ_BASE,
					MPIC_IRQ_STRIDE * mpic->isu_size);
		BUG_ON(mpic->isus[0] == NULL);
	}
	mpic->isu_shift = 1 + __ilog2(mpic->isu_size - 1);
	mpic->isu_mask = (1 << mpic->isu_shift) - 1;

	/* Display version */
	switch (reg & MPIC_GREG_FEATURE_VERSION_MASK) {
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
	printk(KERN_INFO "mpic: Setting up MPIC \"%s\" version %s at %lx, max %d CPUs\n",
	       name, vers, phys_addr, mpic->num_cpus);
	printk(KERN_INFO "mpic: ISU size: %d, shift: %d, mask: %x\n", mpic->isu_size,
	       mpic->isu_shift, mpic->isu_mask);

	mpic->next = mpics;
	mpics = mpic;

	if (flags & MPIC_PRIMARY)
		mpic_primary = mpic;

	return mpic;
}

void __init mpic_assign_isu(struct mpic *mpic, unsigned int isu_num,
			    unsigned long phys_addr)
{
	unsigned int isu_first = isu_num * mpic->isu_size;

	BUG_ON(isu_num >= MPIC_MAX_ISU);

	mpic->isus[isu_num] = ioremap(phys_addr, MPIC_IRQ_STRIDE * mpic->isu_size);
	if ((isu_first + mpic->isu_size) > mpic->num_sources)
		mpic->num_sources = isu_first + mpic->isu_size;
}

void __init mpic_setup_cascade(unsigned int irq, mpic_cascade_t handler,
			       void *data)
{
	struct mpic *mpic = mpic_find(irq, NULL);
	unsigned long flags;

	/* Synchronization here is a bit dodgy, so don't try to replace cascade
	 * interrupts on the fly too often ... but normally it's set up at boot.
	 */
	spin_lock_irqsave(&mpic_lock, flags);
	if (mpic->cascade)	       
		mpic_disable_irq(mpic->cascade_vec + mpic->irq_offset);
	mpic->cascade = NULL;
	wmb();
	mpic->cascade_vec = irq - mpic->irq_offset;
	mpic->cascade_data = data;
	wmb();
	mpic->cascade = handler;
	mpic_enable_irq(irq);
	spin_unlock_irqrestore(&mpic_lock, flags);
}

void __init mpic_init(struct mpic *mpic)
{
	int i;

	BUG_ON(mpic->num_sources == 0);

	printk(KERN_INFO "mpic: Initializing for %d sources\n", mpic->num_sources);

	/* Set current processor priority to max */
	mpic_cpu_write(MPIC_CPU_CURRENT_TASK_PRI, 0xf);

	/* Initialize timers: just disable them all */
	for (i = 0; i < 4; i++) {
		mpic_write(mpic->tmregs,
			   i * MPIC_TIMER_STRIDE + MPIC_TIMER_DESTINATION, 0);
		mpic_write(mpic->tmregs,
			   i * MPIC_TIMER_STRIDE + MPIC_TIMER_VECTOR_PRI,
			   MPIC_VECPRI_MASK |
			   (MPIC_VEC_TIMER_0 + i));
	}

	/* Initialize IPIs to our reserved vectors and mark them disabled for now */
	mpic_test_broken_ipi(mpic);
	for (i = 0; i < 4; i++) {
		mpic_ipi_write(i,
			       MPIC_VECPRI_MASK |
			       (10 << MPIC_VECPRI_PRIORITY_SHIFT) |
			       (MPIC_VEC_IPI_0 + i));
#ifdef CONFIG_SMP
		if (!(mpic->flags & MPIC_PRIMARY))
			continue;
		irq_desc[mpic->ipi_offset+i].status |= IRQ_PER_CPU;
		irq_desc[mpic->ipi_offset+i].handler = &mpic->hc_ipi;
#endif /* CONFIG_SMP */
	}

	/* Initialize interrupt sources */
	if (mpic->irq_count == 0)
		mpic->irq_count = mpic->num_sources;

#ifdef CONFIG_MPIC_BROKEN_U3
	/* Do the ioapic fixups on U3 broken mpic */
	DBG("MPIC flags: %x\n", mpic->flags);
	if ((mpic->flags & MPIC_BROKEN_U3) && (mpic->flags & MPIC_PRIMARY))
		mpic_scan_ioapics(mpic);
#endif /* CONFIG_MPIC_BROKEN_U3 */

	for (i = 0; i < mpic->num_sources; i++) {
		/* start with vector = source number, and masked */
		u32 vecpri = MPIC_VECPRI_MASK | i | (8 << MPIC_VECPRI_PRIORITY_SHIFT);
		int level = 0;
		
		/* if it's an IPI, we skip it */
		if ((mpic->irq_offset + i) >= (mpic->ipi_offset + i) &&
		    (mpic->irq_offset + i) <  (mpic->ipi_offset + i + 4))
			continue;

		/* do senses munging */
		if (mpic->senses && i < mpic->senses_count) {
			if (mpic->senses[i] & IRQ_SENSE_LEVEL)
				vecpri |= MPIC_VECPRI_SENSE_LEVEL;
			if (mpic->senses[i] & IRQ_POLARITY_POSITIVE)
				vecpri |= MPIC_VECPRI_POLARITY_POSITIVE;
		} else
			vecpri |= MPIC_VECPRI_SENSE_LEVEL;

		/* remember if it was a level interrupts */
		level = (vecpri & MPIC_VECPRI_SENSE_LEVEL);

		/* deal with broken U3 */
		if (mpic->flags & MPIC_BROKEN_U3) {
#ifdef CONFIG_MPIC_BROKEN_U3
			if (mpic_is_ht_interrupt(mpic, i)) {
				vecpri &= ~(MPIC_VECPRI_SENSE_MASK |
					    MPIC_VECPRI_POLARITY_MASK);
				vecpri |= MPIC_VECPRI_POLARITY_POSITIVE;
			}
#else
			printk(KERN_ERR "mpic: BROKEN_U3 set, but CONFIG doesn't match\n");
#endif
		}

		DBG("setup source %d, vecpri: %08x, level: %d\n", i, vecpri,
		    (level != 0));

		/* init hw */
		mpic_irq_write(i, MPIC_IRQ_VECTOR_PRI, vecpri);
		mpic_irq_write(i, MPIC_IRQ_DESTINATION,
			       1 << hard_smp_processor_id());

		/* init linux descriptors */
		if (i < mpic->irq_count) {
			irq_desc[mpic->irq_offset+i].status = level ? IRQ_LEVEL : 0;
			irq_desc[mpic->irq_offset+i].handler = &mpic->hc_irq;
		}
	}
	
	/* Init spurrious vector */
	mpic_write(mpic->gregs, MPIC_GREG_SPURIOUS, MPIC_VEC_SPURRIOUS);

	/* Disable 8259 passthrough */
	mpic_write(mpic->gregs, MPIC_GREG_GLOBAL_CONF_0,
		   mpic_read(mpic->gregs, MPIC_GREG_GLOBAL_CONF_0)
		   | MPIC_GREG_GCONF_8259_PTHROU_DIS);

	/* Set current processor priority to 0 */
	mpic_cpu_write(MPIC_CPU_CURRENT_TASK_PRI, 0);
}



void mpic_irq_set_priority(unsigned int irq, unsigned int pri)
{
	int is_ipi;
	struct mpic *mpic = mpic_find(irq, &is_ipi);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&mpic_lock, flags);
	if (is_ipi) {
		reg = mpic_ipi_read(irq - mpic->ipi_offset) & MPIC_VECPRI_PRIORITY_MASK;
		mpic_ipi_write(irq - mpic->ipi_offset,
			       reg | (pri << MPIC_VECPRI_PRIORITY_SHIFT));
	} else {
		reg = mpic_irq_read(irq - mpic->irq_offset, MPIC_IRQ_VECTOR_PRI)
			& MPIC_VECPRI_PRIORITY_MASK;
		mpic_irq_write(irq - mpic->irq_offset, MPIC_IRQ_VECTOR_PRI,
			       reg | (pri << MPIC_VECPRI_PRIORITY_SHIFT));
	}
	spin_unlock_irqrestore(&mpic_lock, flags);
}

unsigned int mpic_irq_get_priority(unsigned int irq)
{
	int is_ipi;
	struct mpic *mpic = mpic_find(irq, &is_ipi);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&mpic_lock, flags);
	if (is_ipi)
		reg = mpic_ipi_read(irq - mpic->ipi_offset);
	else
		reg = mpic_irq_read(irq - mpic->irq_offset, MPIC_IRQ_VECTOR_PRI);
	spin_unlock_irqrestore(&mpic_lock, flags);
	return (reg & MPIC_VECPRI_PRIORITY_MASK) >> MPIC_VECPRI_PRIORITY_SHIFT;
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

	spin_lock_irqsave(&mpic_lock, flags);

 	/* let the mpic know we want intrs. default affinity is 0xffffffff
	 * until changed via /proc. That's how it's done on x86. If we want
	 * it differently, then we should make sure we also change the default
	 * values of irq_affinity in irq.c.
 	 */
	if (distribute_irqs) {
	 	for (i = 0; i < mpic->num_sources ; i++)
			mpic_irq_write(i, MPIC_IRQ_DESTINATION,
				mpic_irq_read(i, MPIC_IRQ_DESTINATION) | msk);
	}

	/* Set current processor priority to 0 */
	mpic_cpu_write(MPIC_CPU_CURRENT_TASK_PRI, 0);

	spin_unlock_irqrestore(&mpic_lock, flags);
#endif /* CONFIG_SMP */
}

int mpic_cpu_get_priority(void)
{
	struct mpic *mpic = mpic_primary;

	return mpic_cpu_read(MPIC_CPU_CURRENT_TASK_PRI);
}

void mpic_cpu_set_priority(int prio)
{
	struct mpic *mpic = mpic_primary;

	prio &= MPIC_CPU_TASKPRI_MASK;
	mpic_cpu_write(MPIC_CPU_CURRENT_TASK_PRI, prio);
}

/*
 * XXX: someone who knows mpic should check this.
 * do we need to eoi the ipi including for kexec cpu here (see xics comments)?
 * or can we reset the mpic in the new kernel?
 */
void mpic_teardown_this_cpu(int secondary)
{
	struct mpic *mpic = mpic_primary;
	unsigned long flags;
	u32 msk = 1 << hard_smp_processor_id();
	unsigned int i;

	BUG_ON(mpic == NULL);

	DBG("%s: teardown_this_cpu(%d)\n", mpic->name, hard_smp_processor_id());
	spin_lock_irqsave(&mpic_lock, flags);

	/* let the mpic know we don't want intrs.  */
	for (i = 0; i < mpic->num_sources ; i++)
		mpic_irq_write(i, MPIC_IRQ_DESTINATION,
			mpic_irq_read(i, MPIC_IRQ_DESTINATION) & ~msk);

	/* Set current processor priority to max */
	mpic_cpu_write(MPIC_CPU_CURRENT_TASK_PRI, 0xf);

	spin_unlock_irqrestore(&mpic_lock, flags);
}


void mpic_send_ipi(unsigned int ipi_no, unsigned int cpu_mask)
{
	struct mpic *mpic = mpic_primary;

	BUG_ON(mpic == NULL);

	DBG("%s: send_ipi(ipi_no: %d)\n", mpic->name, ipi_no);

	mpic_cpu_write(MPIC_CPU_IPI_DISPATCH_0 + ipi_no * 0x10,
		       mpic_physmask(cpu_mask & cpus_addr(cpu_online_map)[0]));
}

int mpic_get_one_irq(struct mpic *mpic, struct pt_regs *regs)
{
	u32 irq;

	irq = mpic_cpu_read(MPIC_CPU_INTACK) & MPIC_VECPRI_VECTOR_MASK;
	DBG("%s: get_one_irq(): %d\n", mpic->name, irq);

	if (mpic->cascade && irq == mpic->cascade_vec) {
		DBG("%s: cascading ...\n", mpic->name);
		irq = mpic->cascade(regs, mpic->cascade_data);
		mpic_eoi(mpic);
		return irq;
	}
	if (unlikely(irq == MPIC_VEC_SPURRIOUS))
		return -1;
	if (irq < MPIC_VEC_IPI_0) 
		return irq + mpic->irq_offset;
       	DBG("%s: ipi %d !\n", mpic->name, irq - MPIC_VEC_IPI_0);
	return irq - MPIC_VEC_IPI_0 + mpic->ipi_offset;
}

int mpic_get_irq(struct pt_regs *regs)
{
	struct mpic *mpic = mpic_primary;

	BUG_ON(mpic == NULL);

	return mpic_get_one_irq(mpic, regs);
}


#ifdef CONFIG_SMP
void mpic_request_ipis(void)
{
	struct mpic *mpic = mpic_primary;

	BUG_ON(mpic == NULL);
	
	printk("requesting IPIs ... \n");

	/* IPIs are marked SA_INTERRUPT as they must run with irqs disabled */
	request_irq(mpic->ipi_offset+0, mpic_ipi_action, SA_INTERRUPT,
		    "IPI0 (call function)", mpic);
	request_irq(mpic->ipi_offset+1, mpic_ipi_action, SA_INTERRUPT,
		   "IPI1 (reschedule)", mpic);
	request_irq(mpic->ipi_offset+2, mpic_ipi_action, SA_INTERRUPT,
		   "IPI2 (unused)", mpic);
	request_irq(mpic->ipi_offset+3, mpic_ipi_action, SA_INTERRUPT,
		   "IPI3 (debugger break)", mpic);

	printk("IPIs requested... \n");
}

void smp_mpic_message_pass(int target, int msg)
{
	/* make sure we're sending something that translates to an IPI */
	if ((unsigned int)msg > 3) {
		printk("SMP %d: smp_message_pass: unknown msg %d\n",
		       smp_processor_id(), msg);
		return;
	}
	switch (target) {
	case MSG_ALL:
		mpic_send_ipi(msg, 0xffffffff);
		break;
	case MSG_ALL_BUT_SELF:
		mpic_send_ipi(msg, 0xffffffff & ~(1 << smp_processor_id()));
		break;
	default:
		mpic_send_ipi(msg, 1 << target);
		break;
	}
}
#endif /* CONFIG_SMP */
