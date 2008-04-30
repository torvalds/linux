/*
 *	Intel IO-APIC support for multi-Pentium hosts.
 *
 *	Copyright (C) 1997, 1998, 1999, 2000 Ingo Molnar, Hajnalka Szabo
 *
 *	Many thanks to Stig Venaas for trying out countless experimental
 *	patches and reporting/debugging problems patiently!
 *
 *	(c) 1999, Multiple IO-APIC support, developed by
 *	Ken-ichi Yaku <yaku@css1.kbnes.nec.co.jp> and
 *      Hidemi Kishimoto <kisimoto@css1.kbnes.nec.co.jp>,
 *	further tested and cleaned up by Zach Brown <zab@redhat.com>
 *	and Ingo Molnar <mingo@redhat.com>
 *
 *	Fixes
 *	Maciej W. Rozycki	:	Bits for genuine 82489DX APICs;
 *					thanks to Eric Gilmore
 *					and Rolf G. Tews
 *					for testing these extensively
 *	Paul Diefenbaugh	:	Added full ACPI support
 */

#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/mc146818rtc.h>
#include <linux/acpi.h>
#include <linux/sysdev.h>
#include <linux/msi.h>
#include <linux/htirq.h>
#include <linux/dmar.h>
#include <linux/jiffies.h>
#ifdef CONFIG_ACPI
#include <acpi/acpi_bus.h>
#endif
#include <linux/bootmem.h>

#include <asm/idle.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/desc.h>
#include <asm/proto.h>
#include <asm/acpi.h>
#include <asm/dma.h>
#include <asm/nmi.h>
#include <asm/msidef.h>
#include <asm/hypertransport.h>

#include <mach_ipi.h>
#include <mach_apic.h>

struct irq_cfg {
	cpumask_t domain;
	cpumask_t old_domain;
	unsigned move_cleanup_count;
	u8 vector;
	u8 move_in_progress : 1;
};

/* irq_cfg is indexed by the sum of all RTEs in all I/O APICs. */
struct irq_cfg irq_cfg[NR_IRQS] __read_mostly = {
	[0]  = { .domain = CPU_MASK_ALL, .vector = IRQ0_VECTOR,  },
	[1]  = { .domain = CPU_MASK_ALL, .vector = IRQ1_VECTOR,  },
	[2]  = { .domain = CPU_MASK_ALL, .vector = IRQ2_VECTOR,  },
	[3]  = { .domain = CPU_MASK_ALL, .vector = IRQ3_VECTOR,  },
	[4]  = { .domain = CPU_MASK_ALL, .vector = IRQ4_VECTOR,  },
	[5]  = { .domain = CPU_MASK_ALL, .vector = IRQ5_VECTOR,  },
	[6]  = { .domain = CPU_MASK_ALL, .vector = IRQ6_VECTOR,  },
	[7]  = { .domain = CPU_MASK_ALL, .vector = IRQ7_VECTOR,  },
	[8]  = { .domain = CPU_MASK_ALL, .vector = IRQ8_VECTOR,  },
	[9]  = { .domain = CPU_MASK_ALL, .vector = IRQ9_VECTOR,  },
	[10] = { .domain = CPU_MASK_ALL, .vector = IRQ10_VECTOR, },
	[11] = { .domain = CPU_MASK_ALL, .vector = IRQ11_VECTOR, },
	[12] = { .domain = CPU_MASK_ALL, .vector = IRQ12_VECTOR, },
	[13] = { .domain = CPU_MASK_ALL, .vector = IRQ13_VECTOR, },
	[14] = { .domain = CPU_MASK_ALL, .vector = IRQ14_VECTOR, },
	[15] = { .domain = CPU_MASK_ALL, .vector = IRQ15_VECTOR, },
};

static int assign_irq_vector(int irq, cpumask_t mask);

#define __apicdebuginit  __init

int sis_apic_bug; /* not actually supported, dummy for compile */

static int no_timer_check;

static int disable_timer_pin_1 __initdata;

int timer_over_8254 __initdata = 1;

/* Where if anywhere is the i8259 connect in external int mode */
static struct { int pin, apic; } ioapic_i8259 = { -1, -1 };

static DEFINE_SPINLOCK(ioapic_lock);
DEFINE_SPINLOCK(vector_lock);

/*
 * # of IRQ routing registers
 */
int nr_ioapic_registers[MAX_IO_APICS];

/* I/O APIC entries */
struct mpc_config_ioapic mp_ioapics[MAX_IO_APICS];
int nr_ioapics;

/* MP IRQ source entries */
struct mpc_config_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* # of MP IRQ source entries */
int mp_irq_entries;

/*
 * Rough estimation of how many shared IRQs there are, can
 * be changed anytime.
 */
#define MAX_PLUS_SHARED_IRQS NR_IRQS
#define PIN_MAP_SIZE (MAX_PLUS_SHARED_IRQS + NR_IRQS)

/*
 * This is performance-critical, we want to do it O(1)
 *
 * the indexing order of this array favors 1:1 mappings
 * between pins and IRQs.
 */

static struct irq_pin_list {
	short apic, pin, next;
} irq_2_pin[PIN_MAP_SIZE];

struct io_apic {
	unsigned int index;
	unsigned int unused[3];
	unsigned int data;
};

static __attribute_const__ struct io_apic __iomem *io_apic_base(int idx)
{
	return (void __iomem *) __fix_to_virt(FIX_IO_APIC_BASE_0 + idx)
		+ (mp_ioapics[idx].mpc_apicaddr & ~PAGE_MASK);
}

static inline unsigned int io_apic_read(unsigned int apic, unsigned int reg)
{
	struct io_apic __iomem *io_apic = io_apic_base(apic);
	writel(reg, &io_apic->index);
	return readl(&io_apic->data);
}

static inline void io_apic_write(unsigned int apic, unsigned int reg, unsigned int value)
{
	struct io_apic __iomem *io_apic = io_apic_base(apic);
	writel(reg, &io_apic->index);
	writel(value, &io_apic->data);
}

/*
 * Re-write a value: to be used for read-modify-write
 * cycles where the read already set up the index register.
 */
static inline void io_apic_modify(unsigned int apic, unsigned int value)
{
	struct io_apic __iomem *io_apic = io_apic_base(apic);
	writel(value, &io_apic->data);
}

static bool io_apic_level_ack_pending(unsigned int irq)
{
	struct irq_pin_list *entry;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	entry = irq_2_pin + irq;
	for (;;) {
		unsigned int reg;
		int pin;

		pin = entry->pin;
		if (pin == -1)
			break;
		reg = io_apic_read(entry->apic, 0x10 + pin*2);
		/* Is the remote IRR bit set? */
		if ((reg >> 14) & 1) {
			spin_unlock_irqrestore(&ioapic_lock, flags);
			return true;
		}
		if (!entry->next)
			break;
		entry = irq_2_pin + entry->next;
	}
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return false;
}

/*
 * Synchronize the IO-APIC and the CPU by doing
 * a dummy read from the IO-APIC
 */
static inline void io_apic_sync(unsigned int apic)
{
	struct io_apic __iomem *io_apic = io_apic_base(apic);
	readl(&io_apic->data);
}

#define __DO_ACTION(R, ACTION, FINAL)					\
									\
{									\
	int pin;							\
	struct irq_pin_list *entry = irq_2_pin + irq;			\
									\
	BUG_ON(irq >= NR_IRQS);						\
	for (;;) {							\
		unsigned int reg;					\
		pin = entry->pin;					\
		if (pin == -1)						\
			break;						\
		reg = io_apic_read(entry->apic, 0x10 + R + pin*2);	\
		reg ACTION;						\
		io_apic_modify(entry->apic, reg);			\
		FINAL;							\
		if (!entry->next)					\
			break;						\
		entry = irq_2_pin + entry->next;			\
	}								\
}

union entry_union {
	struct { u32 w1, w2; };
	struct IO_APIC_route_entry entry;
};

static struct IO_APIC_route_entry ioapic_read_entry(int apic, int pin)
{
	union entry_union eu;
	unsigned long flags;
	spin_lock_irqsave(&ioapic_lock, flags);
	eu.w1 = io_apic_read(apic, 0x10 + 2 * pin);
	eu.w2 = io_apic_read(apic, 0x11 + 2 * pin);
	spin_unlock_irqrestore(&ioapic_lock, flags);
	return eu.entry;
}

/*
 * When we write a new IO APIC routing entry, we need to write the high
 * word first! If the mask bit in the low word is clear, we will enable
 * the interrupt, and we need to make sure the entry is fully populated
 * before that happens.
 */
static void
__ioapic_write_entry(int apic, int pin, struct IO_APIC_route_entry e)
{
	union entry_union eu;
	eu.entry = e;
	io_apic_write(apic, 0x11 + 2*pin, eu.w2);
	io_apic_write(apic, 0x10 + 2*pin, eu.w1);
}

static void ioapic_write_entry(int apic, int pin, struct IO_APIC_route_entry e)
{
	unsigned long flags;
	spin_lock_irqsave(&ioapic_lock, flags);
	__ioapic_write_entry(apic, pin, e);
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

/*
 * When we mask an IO APIC routing entry, we need to write the low
 * word first, in order to set the mask bit before we change the
 * high bits!
 */
static void ioapic_mask_entry(int apic, int pin)
{
	unsigned long flags;
	union entry_union eu = { .entry.mask = 1 };

	spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_write(apic, 0x10 + 2*pin, eu.w1);
	io_apic_write(apic, 0x11 + 2*pin, eu.w2);
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

#ifdef CONFIG_SMP
static void __target_IO_APIC_irq(unsigned int irq, unsigned int dest, u8 vector)
{
	int apic, pin;
	struct irq_pin_list *entry = irq_2_pin + irq;

	BUG_ON(irq >= NR_IRQS);
	for (;;) {
		unsigned int reg;
		apic = entry->apic;
		pin = entry->pin;
		if (pin == -1)
			break;
		io_apic_write(apic, 0x11 + pin*2, dest);
		reg = io_apic_read(apic, 0x10 + pin*2);
		reg &= ~0x000000ff;
		reg |= vector;
		io_apic_modify(apic, reg);
		if (!entry->next)
			break;
		entry = irq_2_pin + entry->next;
	}
}

static void set_ioapic_affinity_irq(unsigned int irq, cpumask_t mask)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	unsigned long flags;
	unsigned int dest;
	cpumask_t tmp;

	cpus_and(tmp, mask, cpu_online_map);
	if (cpus_empty(tmp))
		return;

	if (assign_irq_vector(irq, mask))
		return;

	cpus_and(tmp, cfg->domain, mask);
	dest = cpu_mask_to_apicid(tmp);

	/*
	 * Only the high 8 bits are valid.
	 */
	dest = SET_APIC_LOGICAL_ID(dest);

	spin_lock_irqsave(&ioapic_lock, flags);
	__target_IO_APIC_irq(irq, dest, cfg->vector);
	irq_desc[irq].affinity = mask;
	spin_unlock_irqrestore(&ioapic_lock, flags);
}
#endif

/*
 * The common case is 1:1 IRQ<->pin mappings. Sometimes there are
 * shared ISA-space IRQs, so we have to support them. We are super
 * fast in the common case, and fast for shared ISA-space IRQs.
 */
static void add_pin_to_irq(unsigned int irq, int apic, int pin)
{
	static int first_free_entry = NR_IRQS;
	struct irq_pin_list *entry = irq_2_pin + irq;

	BUG_ON(irq >= NR_IRQS);
	while (entry->next)
		entry = irq_2_pin + entry->next;

	if (entry->pin != -1) {
		entry->next = first_free_entry;
		entry = irq_2_pin + entry->next;
		if (++first_free_entry >= PIN_MAP_SIZE)
			panic("io_apic.c: ran out of irq_2_pin entries!");
	}
	entry->apic = apic;
	entry->pin = pin;
}


#define DO_ACTION(name,R,ACTION, FINAL)					\
									\
	static void name##_IO_APIC_irq (unsigned int irq)		\
	__DO_ACTION(R, ACTION, FINAL)

DO_ACTION( __mask,             0, |= 0x00010000, io_apic_sync(entry->apic) )
						/* mask = 1 */
DO_ACTION( __unmask,           0, &= 0xfffeffff, )
						/* mask = 0 */

static void mask_IO_APIC_irq (unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	__mask_IO_APIC_irq(irq);
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void unmask_IO_APIC_irq (unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	__unmask_IO_APIC_irq(irq);
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void clear_IO_APIC_pin(unsigned int apic, unsigned int pin)
{
	struct IO_APIC_route_entry entry;

	/* Check delivery_mode to be sure we're not clearing an SMI pin */
	entry = ioapic_read_entry(apic, pin);
	if (entry.delivery_mode == dest_SMI)
		return;
	/*
	 * Disable it in the IO-APIC irq-routing table:
	 */
	ioapic_mask_entry(apic, pin);
}

static void clear_IO_APIC (void)
{
	int apic, pin;

	for (apic = 0; apic < nr_ioapics; apic++)
		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++)
			clear_IO_APIC_pin(apic, pin);
}

int skip_ioapic_setup;
int ioapic_force;

static int __init parse_noapic(char *str)
{
	disable_ioapic_setup();
	return 0;
}
early_param("noapic", parse_noapic);

/* Actually the next is obsolete, but keep it for paranoid reasons -AK */
static int __init disable_timer_pin_setup(char *arg)
{
	disable_timer_pin_1 = 1;
	return 1;
}
__setup("disable_timer_pin_1", disable_timer_pin_setup);

static int __init setup_disable_8254_timer(char *s)
{
	timer_over_8254 = -1;
	return 1;
}
static int __init setup_enable_8254_timer(char *s)
{
	timer_over_8254 = 2;
	return 1;
}

__setup("disable_8254_timer", setup_disable_8254_timer);
__setup("enable_8254_timer", setup_enable_8254_timer);


/*
 * Find the IRQ entry number of a certain pin.
 */
static int find_irq_entry(int apic, int pin, int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++)
		if (mp_irqs[i].mpc_irqtype == type &&
		    (mp_irqs[i].mpc_dstapic == mp_ioapics[apic].mpc_apicid ||
		     mp_irqs[i].mpc_dstapic == MP_APIC_ALL) &&
		    mp_irqs[i].mpc_dstirq == pin)
			return i;

	return -1;
}

/*
 * Find the pin to which IRQ[irq] (ISA) is connected
 */
static int __init find_isa_irq_pin(int irq, int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++) {
		int lbus = mp_irqs[i].mpc_srcbus;

		if (test_bit(lbus, mp_bus_not_pci) &&
		    (mp_irqs[i].mpc_irqtype == type) &&
		    (mp_irqs[i].mpc_srcbusirq == irq))

			return mp_irqs[i].mpc_dstirq;
	}
	return -1;
}

static int __init find_isa_irq_apic(int irq, int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++) {
		int lbus = mp_irqs[i].mpc_srcbus;

		if (test_bit(lbus, mp_bus_not_pci) &&
		    (mp_irqs[i].mpc_irqtype == type) &&
		    (mp_irqs[i].mpc_srcbusirq == irq))
			break;
	}
	if (i < mp_irq_entries) {
		int apic;
		for(apic = 0; apic < nr_ioapics; apic++) {
			if (mp_ioapics[apic].mpc_apicid == mp_irqs[i].mpc_dstapic)
				return apic;
		}
	}

	return -1;
}

/*
 * Find a specific PCI IRQ entry.
 * Not an __init, possibly needed by modules
 */
static int pin_2_irq(int idx, int apic, int pin);

int IO_APIC_get_PCI_irq_vector(int bus, int slot, int pin)
{
	int apic, i, best_guess = -1;

	apic_printk(APIC_DEBUG, "querying PCI -> IRQ mapping bus:%d, slot:%d, pin:%d.\n",
		bus, slot, pin);
	if (mp_bus_id_to_pci_bus[bus] == -1) {
		apic_printk(APIC_VERBOSE, "PCI BIOS passed nonexistent PCI bus %d!\n", bus);
		return -1;
	}
	for (i = 0; i < mp_irq_entries; i++) {
		int lbus = mp_irqs[i].mpc_srcbus;

		for (apic = 0; apic < nr_ioapics; apic++)
			if (mp_ioapics[apic].mpc_apicid == mp_irqs[i].mpc_dstapic ||
			    mp_irqs[i].mpc_dstapic == MP_APIC_ALL)
				break;

		if (!test_bit(lbus, mp_bus_not_pci) &&
		    !mp_irqs[i].mpc_irqtype &&
		    (bus == lbus) &&
		    (slot == ((mp_irqs[i].mpc_srcbusirq >> 2) & 0x1f))) {
			int irq = pin_2_irq(i,apic,mp_irqs[i].mpc_dstirq);

			if (!(apic || IO_APIC_IRQ(irq)))
				continue;

			if (pin == (mp_irqs[i].mpc_srcbusirq & 3))
				return irq;
			/*
			 * Use the first all-but-pin matching entry as a
			 * best-guess fuzzy result for broken mptables.
			 */
			if (best_guess < 0)
				best_guess = irq;
		}
	}
	BUG_ON(best_guess >= NR_IRQS);
	return best_guess;
}

/* ISA interrupts are always polarity zero edge triggered,
 * when listed as conforming in the MP table. */

#define default_ISA_trigger(idx)	(0)
#define default_ISA_polarity(idx)	(0)

/* PCI interrupts are always polarity one level triggered,
 * when listed as conforming in the MP table. */

#define default_PCI_trigger(idx)	(1)
#define default_PCI_polarity(idx)	(1)

static int MPBIOS_polarity(int idx)
{
	int bus = mp_irqs[idx].mpc_srcbus;
	int polarity;

	/*
	 * Determine IRQ line polarity (high active or low active):
	 */
	switch (mp_irqs[idx].mpc_irqflag & 3)
	{
		case 0: /* conforms, ie. bus-type dependent polarity */
			if (test_bit(bus, mp_bus_not_pci))
				polarity = default_ISA_polarity(idx);
			else
				polarity = default_PCI_polarity(idx);
			break;
		case 1: /* high active */
		{
			polarity = 0;
			break;
		}
		case 2: /* reserved */
		{
			printk(KERN_WARNING "broken BIOS!!\n");
			polarity = 1;
			break;
		}
		case 3: /* low active */
		{
			polarity = 1;
			break;
		}
		default: /* invalid */
		{
			printk(KERN_WARNING "broken BIOS!!\n");
			polarity = 1;
			break;
		}
	}
	return polarity;
}

static int MPBIOS_trigger(int idx)
{
	int bus = mp_irqs[idx].mpc_srcbus;
	int trigger;

	/*
	 * Determine IRQ trigger mode (edge or level sensitive):
	 */
	switch ((mp_irqs[idx].mpc_irqflag>>2) & 3)
	{
		case 0: /* conforms, ie. bus-type dependent */
			if (test_bit(bus, mp_bus_not_pci))
				trigger = default_ISA_trigger(idx);
			else
				trigger = default_PCI_trigger(idx);
			break;
		case 1: /* edge */
		{
			trigger = 0;
			break;
		}
		case 2: /* reserved */
		{
			printk(KERN_WARNING "broken BIOS!!\n");
			trigger = 1;
			break;
		}
		case 3: /* level */
		{
			trigger = 1;
			break;
		}
		default: /* invalid */
		{
			printk(KERN_WARNING "broken BIOS!!\n");
			trigger = 0;
			break;
		}
	}
	return trigger;
}

static inline int irq_polarity(int idx)
{
	return MPBIOS_polarity(idx);
}

static inline int irq_trigger(int idx)
{
	return MPBIOS_trigger(idx);
}

static int pin_2_irq(int idx, int apic, int pin)
{
	int irq, i;
	int bus = mp_irqs[idx].mpc_srcbus;

	/*
	 * Debugging check, we are in big trouble if this message pops up!
	 */
	if (mp_irqs[idx].mpc_dstirq != pin)
		printk(KERN_ERR "broken BIOS or MPTABLE parser, ayiee!!\n");

	if (test_bit(bus, mp_bus_not_pci)) {
		irq = mp_irqs[idx].mpc_srcbusirq;
	} else {
		/*
		 * PCI IRQs are mapped in order
		 */
		i = irq = 0;
		while (i < apic)
			irq += nr_ioapic_registers[i++];
		irq += pin;
	}
	BUG_ON(irq >= NR_IRQS);
	return irq;
}

static int __assign_irq_vector(int irq, cpumask_t mask)
{
	/*
	 * NOTE! The local APIC isn't very good at handling
	 * multiple interrupts at the same interrupt level.
	 * As the interrupt level is determined by taking the
	 * vector number and shifting that right by 4, we
	 * want to spread these out a bit so that they don't
	 * all fall in the same interrupt level.
	 *
	 * Also, we've got to be careful not to trash gate
	 * 0x80, because int 0x80 is hm, kind of importantish. ;)
	 */
	static int current_vector = FIRST_DEVICE_VECTOR, current_offset = 0;
	unsigned int old_vector;
	int cpu;
	struct irq_cfg *cfg;

	BUG_ON((unsigned)irq >= NR_IRQS);
	cfg = &irq_cfg[irq];

	/* Only try and allocate irqs on cpus that are present */
	cpus_and(mask, mask, cpu_online_map);

	if ((cfg->move_in_progress) || cfg->move_cleanup_count)
		return -EBUSY;

	old_vector = cfg->vector;
	if (old_vector) {
		cpumask_t tmp;
		cpus_and(tmp, cfg->domain, mask);
		if (!cpus_empty(tmp))
			return 0;
	}

	for_each_cpu_mask(cpu, mask) {
		cpumask_t domain, new_mask;
		int new_cpu;
		int vector, offset;

		domain = vector_allocation_domain(cpu);
		cpus_and(new_mask, domain, cpu_online_map);

		vector = current_vector;
		offset = current_offset;
next:
		vector += 8;
		if (vector >= FIRST_SYSTEM_VECTOR) {
			/* If we run out of vectors on large boxen, must share them. */
			offset = (offset + 1) % 8;
			vector = FIRST_DEVICE_VECTOR + offset;
		}
		if (unlikely(current_vector == vector))
			continue;
		if (vector == IA32_SYSCALL_VECTOR)
			goto next;
		for_each_cpu_mask(new_cpu, new_mask)
			if (per_cpu(vector_irq, new_cpu)[vector] != -1)
				goto next;
		/* Found one! */
		current_vector = vector;
		current_offset = offset;
		if (old_vector) {
			cfg->move_in_progress = 1;
			cfg->old_domain = cfg->domain;
		}
		for_each_cpu_mask(new_cpu, new_mask)
			per_cpu(vector_irq, new_cpu)[vector] = irq;
		cfg->vector = vector;
		cfg->domain = domain;
		return 0;
	}
	return -ENOSPC;
}

static int assign_irq_vector(int irq, cpumask_t mask)
{
	int err;
	unsigned long flags;

	spin_lock_irqsave(&vector_lock, flags);
	err = __assign_irq_vector(irq, mask);
	spin_unlock_irqrestore(&vector_lock, flags);
	return err;
}

static void __clear_irq_vector(int irq)
{
	struct irq_cfg *cfg;
	cpumask_t mask;
	int cpu, vector;

	BUG_ON((unsigned)irq >= NR_IRQS);
	cfg = &irq_cfg[irq];
	BUG_ON(!cfg->vector);

	vector = cfg->vector;
	cpus_and(mask, cfg->domain, cpu_online_map);
	for_each_cpu_mask(cpu, mask)
		per_cpu(vector_irq, cpu)[vector] = -1;

	cfg->vector = 0;
	cpus_clear(cfg->domain);
}

void __setup_vector_irq(int cpu)
{
	/* Initialize vector_irq on a new cpu */
	/* This function must be called with vector_lock held */
	int irq, vector;

	/* Mark the inuse vectors */
	for (irq = 0; irq < NR_IRQS; ++irq) {
		if (!cpu_isset(cpu, irq_cfg[irq].domain))
			continue;
		vector = irq_cfg[irq].vector;
		per_cpu(vector_irq, cpu)[vector] = irq;
	}
	/* Mark the free vectors */
	for (vector = 0; vector < NR_VECTORS; ++vector) {
		irq = per_cpu(vector_irq, cpu)[vector];
		if (irq < 0)
			continue;
		if (!cpu_isset(cpu, irq_cfg[irq].domain))
			per_cpu(vector_irq, cpu)[vector] = -1;
	}
}


static struct irq_chip ioapic_chip;

static void ioapic_register_intr(int irq, unsigned long trigger)
{
	if (trigger) {
		irq_desc[irq].status |= IRQ_LEVEL;
		set_irq_chip_and_handler_name(irq, &ioapic_chip,
					      handle_fasteoi_irq, "fasteoi");
	} else {
		irq_desc[irq].status &= ~IRQ_LEVEL;
		set_irq_chip_and_handler_name(irq, &ioapic_chip,
					      handle_edge_irq, "edge");
	}
}

static void setup_IO_APIC_irq(int apic, int pin, unsigned int irq,
			      int trigger, int polarity)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	struct IO_APIC_route_entry entry;
	cpumask_t mask;

	if (!IO_APIC_IRQ(irq))
		return;

	mask = TARGET_CPUS;
	if (assign_irq_vector(irq, mask))
		return;

	cpus_and(mask, cfg->domain, mask);

	apic_printk(APIC_VERBOSE,KERN_DEBUG
		    "IOAPIC[%d]: Set routing entry (%d-%d -> 0x%x -> "
		    "IRQ %d Mode:%i Active:%i)\n",
		    apic, mp_ioapics[apic].mpc_apicid, pin, cfg->vector,
		    irq, trigger, polarity);

	/*
	 * add it to the IO-APIC irq-routing table:
	 */
	memset(&entry,0,sizeof(entry));

	entry.delivery_mode = INT_DELIVERY_MODE;
	entry.dest_mode = INT_DEST_MODE;
	entry.dest = cpu_mask_to_apicid(mask);
	entry.mask = 0;				/* enable IRQ */
	entry.trigger = trigger;
	entry.polarity = polarity;
	entry.vector = cfg->vector;

	/* Mask level triggered irqs.
	 * Use IRQ_DELAYED_DISABLE for edge triggered irqs.
	 */
	if (trigger)
		entry.mask = 1;

	ioapic_register_intr(irq, trigger);
	if (irq < 16)
		disable_8259A_irq(irq);

	ioapic_write_entry(apic, pin, entry);
}

static void __init setup_IO_APIC_irqs(void)
{
	int apic, pin, idx, irq, first_notcon = 1;

	apic_printk(APIC_VERBOSE, KERN_DEBUG "init IO_APIC IRQs\n");

	for (apic = 0; apic < nr_ioapics; apic++) {
	for (pin = 0; pin < nr_ioapic_registers[apic]; pin++) {

		idx = find_irq_entry(apic,pin,mp_INT);
		if (idx == -1) {
			if (first_notcon) {
				apic_printk(APIC_VERBOSE, KERN_DEBUG " IO-APIC (apicid-pin) %d-%d", mp_ioapics[apic].mpc_apicid, pin);
				first_notcon = 0;
			} else
				apic_printk(APIC_VERBOSE, ", %d-%d", mp_ioapics[apic].mpc_apicid, pin);
			continue;
		}
		if (!first_notcon) {
			apic_printk(APIC_VERBOSE, " not connected.\n");
			first_notcon = 1;
		}

		irq = pin_2_irq(idx, apic, pin);
		add_pin_to_irq(irq, apic, pin);

		setup_IO_APIC_irq(apic, pin, irq,
				  irq_trigger(idx), irq_polarity(idx));
	}
	}

	if (!first_notcon)
		apic_printk(APIC_VERBOSE, " not connected.\n");
}

/*
 * Set up the 8259A-master output pin as broadcast to all
 * CPUs.
 */
static void __init setup_ExtINT_IRQ0_pin(unsigned int apic, unsigned int pin, int vector)
{
	struct IO_APIC_route_entry entry;

	memset(&entry, 0, sizeof(entry));

	disable_8259A_irq(0);

	/* mask LVT0 */
	apic_write(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_EXTINT);

	/*
	 * We use logical delivery to get the timer IRQ
	 * to the first CPU.
	 */
	entry.dest_mode = INT_DEST_MODE;
	entry.mask = 0;					/* unmask IRQ now */
	entry.dest = cpu_mask_to_apicid(TARGET_CPUS);
	entry.delivery_mode = INT_DELIVERY_MODE;
	entry.polarity = 0;
	entry.trigger = 0;
	entry.vector = vector;

	/*
	 * The timer IRQ doesn't have to know that behind the
	 * scene we have a 8259A-master in AEOI mode ...
	 */
	set_irq_chip_and_handler_name(0, &ioapic_chip, handle_edge_irq, "edge");

	/*
	 * Add it to the IO-APIC irq-routing table:
	 */
	ioapic_write_entry(apic, pin, entry);

	enable_8259A_irq(0);
}

void __apicdebuginit print_IO_APIC(void)
{
	int apic, i;
	union IO_APIC_reg_00 reg_00;
	union IO_APIC_reg_01 reg_01;
	union IO_APIC_reg_02 reg_02;
	unsigned long flags;

	if (apic_verbosity == APIC_QUIET)
		return;

	printk(KERN_DEBUG "number of MP IRQ sources: %d.\n", mp_irq_entries);
	for (i = 0; i < nr_ioapics; i++)
		printk(KERN_DEBUG "number of IO-APIC #%d registers: %d.\n",
		       mp_ioapics[i].mpc_apicid, nr_ioapic_registers[i]);

	/*
	 * We are a bit conservative about what we expect.  We have to
	 * know about every hardware change ASAP.
	 */
	printk(KERN_INFO "testing the IO APIC.......................\n");

	for (apic = 0; apic < nr_ioapics; apic++) {

	spin_lock_irqsave(&ioapic_lock, flags);
	reg_00.raw = io_apic_read(apic, 0);
	reg_01.raw = io_apic_read(apic, 1);
	if (reg_01.bits.version >= 0x10)
		reg_02.raw = io_apic_read(apic, 2);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	printk("\n");
	printk(KERN_DEBUG "IO APIC #%d......\n", mp_ioapics[apic].mpc_apicid);
	printk(KERN_DEBUG ".... register #00: %08X\n", reg_00.raw);
	printk(KERN_DEBUG ".......    : physical APIC id: %02X\n", reg_00.bits.ID);

	printk(KERN_DEBUG ".... register #01: %08X\n", *(int *)&reg_01);
	printk(KERN_DEBUG ".......     : max redirection entries: %04X\n", reg_01.bits.entries);

	printk(KERN_DEBUG ".......     : PRQ implemented: %X\n", reg_01.bits.PRQ);
	printk(KERN_DEBUG ".......     : IO APIC version: %04X\n", reg_01.bits.version);

	if (reg_01.bits.version >= 0x10) {
		printk(KERN_DEBUG ".... register #02: %08X\n", reg_02.raw);
		printk(KERN_DEBUG ".......     : arbitration: %02X\n", reg_02.bits.arbitration);
	}

	printk(KERN_DEBUG ".... IRQ redirection table:\n");

	printk(KERN_DEBUG " NR Dst Mask Trig IRR Pol"
			  " Stat Dmod Deli Vect:   \n");

	for (i = 0; i <= reg_01.bits.entries; i++) {
		struct IO_APIC_route_entry entry;

		entry = ioapic_read_entry(apic, i);

		printk(KERN_DEBUG " %02x %03X ",
			i,
			entry.dest
		);

		printk("%1d    %1d    %1d   %1d   %1d    %1d    %1d    %02X\n",
			entry.mask,
			entry.trigger,
			entry.irr,
			entry.polarity,
			entry.delivery_status,
			entry.dest_mode,
			entry.delivery_mode,
			entry.vector
		);
	}
	}
	printk(KERN_DEBUG "IRQ to pin mappings:\n");
	for (i = 0; i < NR_IRQS; i++) {
		struct irq_pin_list *entry = irq_2_pin + i;
		if (entry->pin < 0)
			continue;
		printk(KERN_DEBUG "IRQ%d ", i);
		for (;;) {
			printk("-> %d:%d", entry->apic, entry->pin);
			if (!entry->next)
				break;
			entry = irq_2_pin + entry->next;
		}
		printk("\n");
	}

	printk(KERN_INFO ".................................... done.\n");

	return;
}

#if 0

static __apicdebuginit void print_APIC_bitfield (int base)
{
	unsigned int v;
	int i, j;

	if (apic_verbosity == APIC_QUIET)
		return;

	printk(KERN_DEBUG "0123456789abcdef0123456789abcdef\n" KERN_DEBUG);
	for (i = 0; i < 8; i++) {
		v = apic_read(base + i*0x10);
		for (j = 0; j < 32; j++) {
			if (v & (1<<j))
				printk("1");
			else
				printk("0");
		}
		printk("\n");
	}
}

void __apicdebuginit print_local_APIC(void * dummy)
{
	unsigned int v, ver, maxlvt;

	if (apic_verbosity == APIC_QUIET)
		return;

	printk("\n" KERN_DEBUG "printing local APIC contents on CPU#%d/%d:\n",
		smp_processor_id(), hard_smp_processor_id());
	printk(KERN_INFO "... APIC ID:      %08x (%01x)\n", v, GET_APIC_ID(read_apic_id()));
	v = apic_read(APIC_LVR);
	printk(KERN_INFO "... APIC VERSION: %08x\n", v);
	ver = GET_APIC_VERSION(v);
	maxlvt = lapic_get_maxlvt();

	v = apic_read(APIC_TASKPRI);
	printk(KERN_DEBUG "... APIC TASKPRI: %08x (%02x)\n", v, v & APIC_TPRI_MASK);

	v = apic_read(APIC_ARBPRI);
	printk(KERN_DEBUG "... APIC ARBPRI: %08x (%02x)\n", v,
		v & APIC_ARBPRI_MASK);
	v = apic_read(APIC_PROCPRI);
	printk(KERN_DEBUG "... APIC PROCPRI: %08x\n", v);

	v = apic_read(APIC_EOI);
	printk(KERN_DEBUG "... APIC EOI: %08x\n", v);
	v = apic_read(APIC_RRR);
	printk(KERN_DEBUG "... APIC RRR: %08x\n", v);
	v = apic_read(APIC_LDR);
	printk(KERN_DEBUG "... APIC LDR: %08x\n", v);
	v = apic_read(APIC_DFR);
	printk(KERN_DEBUG "... APIC DFR: %08x\n", v);
	v = apic_read(APIC_SPIV);
	printk(KERN_DEBUG "... APIC SPIV: %08x\n", v);

	printk(KERN_DEBUG "... APIC ISR field:\n");
	print_APIC_bitfield(APIC_ISR);
	printk(KERN_DEBUG "... APIC TMR field:\n");
	print_APIC_bitfield(APIC_TMR);
	printk(KERN_DEBUG "... APIC IRR field:\n");
	print_APIC_bitfield(APIC_IRR);

	v = apic_read(APIC_ESR);
	printk(KERN_DEBUG "... APIC ESR: %08x\n", v);

	v = apic_read(APIC_ICR);
	printk(KERN_DEBUG "... APIC ICR: %08x\n", v);
	v = apic_read(APIC_ICR2);
	printk(KERN_DEBUG "... APIC ICR2: %08x\n", v);

	v = apic_read(APIC_LVTT);
	printk(KERN_DEBUG "... APIC LVTT: %08x\n", v);

	if (maxlvt > 3) {                       /* PC is LVT#4. */
		v = apic_read(APIC_LVTPC);
		printk(KERN_DEBUG "... APIC LVTPC: %08x\n", v);
	}
	v = apic_read(APIC_LVT0);
	printk(KERN_DEBUG "... APIC LVT0: %08x\n", v);
	v = apic_read(APIC_LVT1);
	printk(KERN_DEBUG "... APIC LVT1: %08x\n", v);

	if (maxlvt > 2) {			/* ERR is LVT#3. */
		v = apic_read(APIC_LVTERR);
		printk(KERN_DEBUG "... APIC LVTERR: %08x\n", v);
	}

	v = apic_read(APIC_TMICT);
	printk(KERN_DEBUG "... APIC TMICT: %08x\n", v);
	v = apic_read(APIC_TMCCT);
	printk(KERN_DEBUG "... APIC TMCCT: %08x\n", v);
	v = apic_read(APIC_TDCR);
	printk(KERN_DEBUG "... APIC TDCR: %08x\n", v);
	printk("\n");
}

void print_all_local_APICs (void)
{
	on_each_cpu(print_local_APIC, NULL, 1, 1);
}

void __apicdebuginit print_PIC(void)
{
	unsigned int v;
	unsigned long flags;

	if (apic_verbosity == APIC_QUIET)
		return;

	printk(KERN_DEBUG "\nprinting PIC contents\n");

	spin_lock_irqsave(&i8259A_lock, flags);

	v = inb(0xa1) << 8 | inb(0x21);
	printk(KERN_DEBUG "... PIC  IMR: %04x\n", v);

	v = inb(0xa0) << 8 | inb(0x20);
	printk(KERN_DEBUG "... PIC  IRR: %04x\n", v);

	outb(0x0b,0xa0);
	outb(0x0b,0x20);
	v = inb(0xa0) << 8 | inb(0x20);
	outb(0x0a,0xa0);
	outb(0x0a,0x20);

	spin_unlock_irqrestore(&i8259A_lock, flags);

	printk(KERN_DEBUG "... PIC  ISR: %04x\n", v);

	v = inb(0x4d1) << 8 | inb(0x4d0);
	printk(KERN_DEBUG "... PIC ELCR: %04x\n", v);
}

#endif  /*  0  */

void __init enable_IO_APIC(void)
{
	union IO_APIC_reg_01 reg_01;
	int i8259_apic, i8259_pin;
	int i, apic;
	unsigned long flags;

	for (i = 0; i < PIN_MAP_SIZE; i++) {
		irq_2_pin[i].pin = -1;
		irq_2_pin[i].next = 0;
	}

	/*
	 * The number of IO-APIC IRQ registers (== #pins):
	 */
	for (apic = 0; apic < nr_ioapics; apic++) {
		spin_lock_irqsave(&ioapic_lock, flags);
		reg_01.raw = io_apic_read(apic, 1);
		spin_unlock_irqrestore(&ioapic_lock, flags);
		nr_ioapic_registers[apic] = reg_01.bits.entries+1;
	}
	for(apic = 0; apic < nr_ioapics; apic++) {
		int pin;
		/* See if any of the pins is in ExtINT mode */
		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++) {
			struct IO_APIC_route_entry entry;
			entry = ioapic_read_entry(apic, pin);

			/* If the interrupt line is enabled and in ExtInt mode
			 * I have found the pin where the i8259 is connected.
			 */
			if ((entry.mask == 0) && (entry.delivery_mode == dest_ExtINT)) {
				ioapic_i8259.apic = apic;
				ioapic_i8259.pin  = pin;
				goto found_i8259;
			}
		}
	}
 found_i8259:
	/* Look to see what if the MP table has reported the ExtINT */
	i8259_pin  = find_isa_irq_pin(0, mp_ExtINT);
	i8259_apic = find_isa_irq_apic(0, mp_ExtINT);
	/* Trust the MP table if nothing is setup in the hardware */
	if ((ioapic_i8259.pin == -1) && (i8259_pin >= 0)) {
		printk(KERN_WARNING "ExtINT not setup in hardware but reported by MP table\n");
		ioapic_i8259.pin  = i8259_pin;
		ioapic_i8259.apic = i8259_apic;
	}
	/* Complain if the MP table and the hardware disagree */
	if (((ioapic_i8259.apic != i8259_apic) || (ioapic_i8259.pin != i8259_pin)) &&
		(i8259_pin >= 0) && (ioapic_i8259.pin >= 0))
	{
		printk(KERN_WARNING "ExtINT in hardware and MP table differ\n");
	}

	/*
	 * Do not trust the IO-APIC being empty at bootup
	 */
	clear_IO_APIC();
}

/*
 * Not an __init, needed by the reboot code
 */
void disable_IO_APIC(void)
{
	/*
	 * Clear the IO-APIC before rebooting:
	 */
	clear_IO_APIC();

	/*
	 * If the i8259 is routed through an IOAPIC
	 * Put that IOAPIC in virtual wire mode
	 * so legacy interrupts can be delivered.
	 */
	if (ioapic_i8259.pin != -1) {
		struct IO_APIC_route_entry entry;

		memset(&entry, 0, sizeof(entry));
		entry.mask            = 0; /* Enabled */
		entry.trigger         = 0; /* Edge */
		entry.irr             = 0;
		entry.polarity        = 0; /* High */
		entry.delivery_status = 0;
		entry.dest_mode       = 0; /* Physical */
		entry.delivery_mode   = dest_ExtINT; /* ExtInt */
		entry.vector          = 0;
		entry.dest          = GET_APIC_ID(read_apic_id());

		/*
		 * Add it to the IO-APIC irq-routing table:
		 */
		ioapic_write_entry(ioapic_i8259.apic, ioapic_i8259.pin, entry);
	}

	disconnect_bsp_APIC(ioapic_i8259.pin != -1);
}

/*
 * There is a nasty bug in some older SMP boards, their mptable lies
 * about the timer IRQ. We do the following to work around the situation:
 *
 *	- timer IRQ defaults to IO-APIC IRQ
 *	- if this function detects that timer IRQs are defunct, then we fall
 *	  back to ISA timer IRQs
 */
static int __init timer_irq_works(void)
{
	unsigned long t1 = jiffies;
	unsigned long flags;

	local_save_flags(flags);
	local_irq_enable();
	/* Let ten ticks pass... */
	mdelay((10 * 1000) / HZ);
	local_irq_restore(flags);

	/*
	 * Expect a few ticks at least, to be sure some possible
	 * glue logic does not lock up after one or two first
	 * ticks in a non-ExtINT mode.  Also the local APIC
	 * might have cached one ExtINT interrupt.  Finally, at
	 * least one tick may be lost due to delays.
	 */

	/* jiffies wrap? */
	if (time_after(jiffies, t1 + 4))
		return 1;
	return 0;
}

/*
 * In the SMP+IOAPIC case it might happen that there are an unspecified
 * number of pending IRQ events unhandled. These cases are very rare,
 * so we 'resend' these IRQs via IPIs, to the same CPU. It's much
 * better to do it this way as thus we do not have to be aware of
 * 'pending' interrupts in the IRQ path, except at this point.
 */
/*
 * Edge triggered needs to resend any interrupt
 * that was delayed but this is now handled in the device
 * independent code.
 */

/*
 * Starting up a edge-triggered IO-APIC interrupt is
 * nasty - we need to make sure that we get the edge.
 * If it is already asserted for some reason, we need
 * return 1 to indicate that is was pending.
 *
 * This is not complete - we should be able to fake
 * an edge even if it isn't on the 8259A...
 */

static unsigned int startup_ioapic_irq(unsigned int irq)
{
	int was_pending = 0;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	if (irq < 16) {
		disable_8259A_irq(irq);
		if (i8259A_irq_pending(irq))
			was_pending = 1;
	}
	__unmask_IO_APIC_irq(irq);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return was_pending;
}

static int ioapic_retrigger_irq(unsigned int irq)
{
	struct irq_cfg *cfg = &irq_cfg[irq];
	cpumask_t mask;
	unsigned long flags;

	spin_lock_irqsave(&vector_lock, flags);
	mask = cpumask_of_cpu(first_cpu(cfg->domain));
	send_IPI_mask(mask, cfg->vector);
	spin_unlock_irqrestore(&vector_lock, flags);

	return 1;
}

/*
 * Level and edge triggered IO-APIC interrupts need different handling,
 * so we use two separate IRQ descriptors. Edge triggered IRQs can be
 * handled with the level-triggered descriptor, but that one has slightly
 * more overhead. Level-triggered interrupts cannot be handled with the
 * edge-triggered handler, without risking IRQ storms and other ugly
 * races.
 */

#ifdef CONFIG_SMP
asmlinkage void smp_irq_move_cleanup_interrupt(void)
{
	unsigned vector, me;
	ack_APIC_irq();
	exit_idle();
	irq_enter();

	me = smp_processor_id();
	for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS; vector++) {
		unsigned int irq;
		struct irq_desc *desc;
		struct irq_cfg *cfg;
		irq = __get_cpu_var(vector_irq)[vector];
		if (irq >= NR_IRQS)
			continue;

		desc = irq_desc + irq;
		cfg = irq_cfg + irq;
		spin_lock(&desc->lock);
		if (!cfg->move_cleanup_count)
			goto unlock;

		if ((vector == cfg->vector) && cpu_isset(me, cfg->domain))
			goto unlock;

		__get_cpu_var(vector_irq)[vector] = -1;
		cfg->move_cleanup_count--;
unlock:
		spin_unlock(&desc->lock);
	}

	irq_exit();
}

static void irq_complete_move(unsigned int irq)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	unsigned vector, me;

	if (likely(!cfg->move_in_progress))
		return;

	vector = ~get_irq_regs()->orig_ax;
	me = smp_processor_id();
	if ((vector == cfg->vector) && cpu_isset(me, cfg->domain)) {
		cpumask_t cleanup_mask;

		cpus_and(cleanup_mask, cfg->old_domain, cpu_online_map);
		cfg->move_cleanup_count = cpus_weight(cleanup_mask);
		send_IPI_mask(cleanup_mask, IRQ_MOVE_CLEANUP_VECTOR);
		cfg->move_in_progress = 0;
	}
}
#else
static inline void irq_complete_move(unsigned int irq) {}
#endif

static void ack_apic_edge(unsigned int irq)
{
	irq_complete_move(irq);
	move_native_irq(irq);
	ack_APIC_irq();
}

static void ack_apic_level(unsigned int irq)
{
	int do_unmask_irq = 0;

	irq_complete_move(irq);
#ifdef CONFIG_GENERIC_PENDING_IRQ
	/* If we are moving the irq we need to mask it */
	if (unlikely(irq_desc[irq].status & IRQ_MOVE_PENDING)) {
		do_unmask_irq = 1;
		mask_IO_APIC_irq(irq);
	}
#endif

	/*
	 * We must acknowledge the irq before we move it or the acknowledge will
	 * not propagate properly.
	 */
	ack_APIC_irq();

	/* Now we can move and renable the irq */
	if (unlikely(do_unmask_irq)) {
		/* Only migrate the irq if the ack has been received.
		 *
		 * On rare occasions the broadcast level triggered ack gets
		 * delayed going to ioapics, and if we reprogram the
		 * vector while Remote IRR is still set the irq will never
		 * fire again.
		 *
		 * To prevent this scenario we read the Remote IRR bit
		 * of the ioapic.  This has two effects.
		 * - On any sane system the read of the ioapic will
		 *   flush writes (and acks) going to the ioapic from
		 *   this cpu.
		 * - We get to see if the ACK has actually been delivered.
		 *
		 * Based on failed experiments of reprogramming the
		 * ioapic entry from outside of irq context starting
		 * with masking the ioapic entry and then polling until
		 * Remote IRR was clear before reprogramming the
		 * ioapic I don't trust the Remote IRR bit to be
		 * completey accurate.
		 *
		 * However there appears to be no other way to plug
		 * this race, so if the Remote IRR bit is not
		 * accurate and is causing problems then it is a hardware bug
		 * and you can go talk to the chipset vendor about it.
		 */
		if (!io_apic_level_ack_pending(irq))
			move_masked_irq(irq);
		unmask_IO_APIC_irq(irq);
	}
}

static struct irq_chip ioapic_chip __read_mostly = {
	.name 		= "IO-APIC",
	.startup 	= startup_ioapic_irq,
	.mask	 	= mask_IO_APIC_irq,
	.unmask	 	= unmask_IO_APIC_irq,
	.ack 		= ack_apic_edge,
	.eoi 		= ack_apic_level,
#ifdef CONFIG_SMP
	.set_affinity 	= set_ioapic_affinity_irq,
#endif
	.retrigger	= ioapic_retrigger_irq,
};

static inline void init_IO_APIC_traps(void)
{
	int irq;

	/*
	 * NOTE! The local APIC isn't very good at handling
	 * multiple interrupts at the same interrupt level.
	 * As the interrupt level is determined by taking the
	 * vector number and shifting that right by 4, we
	 * want to spread these out a bit so that they don't
	 * all fall in the same interrupt level.
	 *
	 * Also, we've got to be careful not to trash gate
	 * 0x80, because int 0x80 is hm, kind of importantish. ;)
	 */
	for (irq = 0; irq < NR_IRQS ; irq++) {
		if (IO_APIC_IRQ(irq) && !irq_cfg[irq].vector) {
			/*
			 * Hmm.. We don't have an entry for this,
			 * so default to an old-fashioned 8259
			 * interrupt if we can..
			 */
			if (irq < 16)
				make_8259A_irq(irq);
			else
				/* Strange. Oh, well.. */
				irq_desc[irq].chip = &no_irq_chip;
		}
	}
}

static void enable_lapic_irq (unsigned int irq)
{
	unsigned long v;

	v = apic_read(APIC_LVT0);
	apic_write(APIC_LVT0, v & ~APIC_LVT_MASKED);
}

static void disable_lapic_irq (unsigned int irq)
{
	unsigned long v;

	v = apic_read(APIC_LVT0);
	apic_write(APIC_LVT0, v | APIC_LVT_MASKED);
}

static void ack_lapic_irq (unsigned int irq)
{
	ack_APIC_irq();
}

static void end_lapic_irq (unsigned int i) { /* nothing */ }

static struct hw_interrupt_type lapic_irq_type __read_mostly = {
	.name = "local-APIC",
	.typename = "local-APIC-edge",
	.startup = NULL, /* startup_irq() not used for IRQ0 */
	.shutdown = NULL, /* shutdown_irq() not used for IRQ0 */
	.enable = enable_lapic_irq,
	.disable = disable_lapic_irq,
	.ack = ack_lapic_irq,
	.end = end_lapic_irq,
};

static void __init setup_nmi(void)
{
	/*
 	 * Dirty trick to enable the NMI watchdog ...
	 * We put the 8259A master into AEOI mode and
	 * unmask on all local APICs LVT0 as NMI.
	 *
	 * The idea to use the 8259A in AEOI mode ('8259A Virtual Wire')
	 * is from Maciej W. Rozycki - so we do not have to EOI from
	 * the NMI handler or the timer interrupt.
	 */ 
	printk(KERN_INFO "activating NMI Watchdog ...");

	enable_NMI_through_LVT0();

	printk(" done.\n");
}

/*
 * This looks a bit hackish but it's about the only one way of sending
 * a few INTA cycles to 8259As and any associated glue logic.  ICR does
 * not support the ExtINT mode, unfortunately.  We need to send these
 * cycles as some i82489DX-based boards have glue logic that keeps the
 * 8259A interrupt line asserted until INTA.  --macro
 */
static inline void __init unlock_ExtINT_logic(void)
{
	int apic, pin, i;
	struct IO_APIC_route_entry entry0, entry1;
	unsigned char save_control, save_freq_select;

	pin  = find_isa_irq_pin(8, mp_INT);
	apic = find_isa_irq_apic(8, mp_INT);
	if (pin == -1)
		return;

	entry0 = ioapic_read_entry(apic, pin);

	clear_IO_APIC_pin(apic, pin);

	memset(&entry1, 0, sizeof(entry1));

	entry1.dest_mode = 0;			/* physical delivery */
	entry1.mask = 0;			/* unmask IRQ now */
	entry1.dest = hard_smp_processor_id();
	entry1.delivery_mode = dest_ExtINT;
	entry1.polarity = entry0.polarity;
	entry1.trigger = 0;
	entry1.vector = 0;

	ioapic_write_entry(apic, pin, entry1);

	save_control = CMOS_READ(RTC_CONTROL);
	save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
	CMOS_WRITE((save_freq_select & ~RTC_RATE_SELECT) | 0x6,
		   RTC_FREQ_SELECT);
	CMOS_WRITE(save_control | RTC_PIE, RTC_CONTROL);

	i = 100;
	while (i-- > 0) {
		mdelay(10);
		if ((CMOS_READ(RTC_INTR_FLAGS) & RTC_PF) == RTC_PF)
			i -= 10;
	}

	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);
	clear_IO_APIC_pin(apic, pin);

	ioapic_write_entry(apic, pin, entry0);
}

/*
 * This code may look a bit paranoid, but it's supposed to cooperate with
 * a wide range of boards and BIOS bugs.  Fortunately only the timer IRQ
 * is so screwy.  Thanks to Brian Perkins for testing/hacking this beast
 * fanatically on his truly buggy board.
 *
 * FIXME: really need to revamp this for modern platforms only.
 */
static inline void __init check_timer(void)
{
	struct irq_cfg *cfg = irq_cfg + 0;
	int apic1, pin1, apic2, pin2;
	unsigned long flags;

	local_irq_save(flags);

	/*
	 * get/set the timer IRQ vector:
	 */
	disable_8259A_irq(0);
	assign_irq_vector(0, TARGET_CPUS);

	/*
	 * Subtle, code in do_timer_interrupt() expects an AEOI
	 * mode for the 8259A whenever interrupts are routed
	 * through I/O APICs.  Also IRQ0 has to be enabled in
	 * the 8259A which implies the virtual wire has to be
	 * disabled in the local APIC.
	 */
	apic_write(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_EXTINT);
	init_8259A(1);
	if (timer_over_8254 > 0)
		enable_8259A_irq(0);

	pin1  = find_isa_irq_pin(0, mp_INT);
	apic1 = find_isa_irq_apic(0, mp_INT);
	pin2  = ioapic_i8259.pin;
	apic2 = ioapic_i8259.apic;

	apic_printk(APIC_VERBOSE,KERN_INFO "..TIMER: vector=0x%02X apic1=%d pin1=%d apic2=%d pin2=%d\n",
		cfg->vector, apic1, pin1, apic2, pin2);

	if (pin1 != -1) {
		/*
		 * Ok, does IRQ0 through the IOAPIC work?
		 */
		unmask_IO_APIC_irq(0);
		if (!no_timer_check && timer_irq_works()) {
			nmi_watchdog_default();
			if (nmi_watchdog == NMI_IO_APIC) {
				disable_8259A_irq(0);
				setup_nmi();
				enable_8259A_irq(0);
			}
			if (disable_timer_pin_1 > 0)
				clear_IO_APIC_pin(0, pin1);
			goto out;
		}
		clear_IO_APIC_pin(apic1, pin1);
		apic_printk(APIC_QUIET,KERN_ERR "..MP-BIOS bug: 8254 timer not "
				"connected to IO-APIC\n");
	}

	apic_printk(APIC_VERBOSE,KERN_INFO "...trying to set up timer (IRQ0) "
				"through the 8259A ... ");
	if (pin2 != -1) {
		apic_printk(APIC_VERBOSE,"\n..... (found apic %d pin %d) ...",
			apic2, pin2);
		/*
		 * legacy devices should be connected to IO APIC #0
		 */
		setup_ExtINT_IRQ0_pin(apic2, pin2, cfg->vector);
		if (timer_irq_works()) {
			apic_printk(APIC_VERBOSE," works.\n");
			nmi_watchdog_default();
			if (nmi_watchdog == NMI_IO_APIC) {
				setup_nmi();
			}
			goto out;
		}
		/*
		 * Cleanup, just in case ...
		 */
		clear_IO_APIC_pin(apic2, pin2);
	}
	apic_printk(APIC_VERBOSE," failed.\n");

	if (nmi_watchdog == NMI_IO_APIC) {
		printk(KERN_WARNING "timer doesn't work through the IO-APIC - disabling NMI Watchdog!\n");
		nmi_watchdog = 0;
	}

	apic_printk(APIC_VERBOSE, KERN_INFO "...trying to set up timer as Virtual Wire IRQ...");

	disable_8259A_irq(0);
	irq_desc[0].chip = &lapic_irq_type;
	apic_write(APIC_LVT0, APIC_DM_FIXED | cfg->vector);	/* Fixed mode */
	enable_8259A_irq(0);

	if (timer_irq_works()) {
		apic_printk(APIC_VERBOSE," works.\n");
		goto out;
	}
	apic_write(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_FIXED | cfg->vector);
	apic_printk(APIC_VERBOSE," failed.\n");

	apic_printk(APIC_VERBOSE, KERN_INFO "...trying to set up timer as ExtINT IRQ...");

	init_8259A(0);
	make_8259A_irq(0);
	apic_write(APIC_LVT0, APIC_DM_EXTINT);

	unlock_ExtINT_logic();

	if (timer_irq_works()) {
		apic_printk(APIC_VERBOSE," works.\n");
		goto out;
	}
	apic_printk(APIC_VERBOSE," failed :(.\n");
	panic("IO-APIC + timer doesn't work! Try using the 'noapic' kernel parameter\n");
out:
	local_irq_restore(flags);
}

static int __init notimercheck(char *s)
{
	no_timer_check = 1;
	return 1;
}
__setup("no_timer_check", notimercheck);

/*
 *
 * IRQs that are handled by the PIC in the MPS IOAPIC case.
 * - IRQ2 is the cascade IRQ, and cannot be a io-apic IRQ.
 *   Linux doesn't really care, as it's not actually used
 *   for any interrupt handling anyway.
 */
#define PIC_IRQS	(1<<2)

void __init setup_IO_APIC(void)
{

	/*
	 * calling enable_IO_APIC() is moved to setup_local_APIC for BP
	 */

	if (acpi_ioapic)
		io_apic_irqs = ~0;	/* all IRQs go through IOAPIC */
	else
		io_apic_irqs = ~PIC_IRQS;

	apic_printk(APIC_VERBOSE, "ENABLING IO-APIC IRQs\n");

	sync_Arb_IDs();
	setup_IO_APIC_irqs();
	init_IO_APIC_traps();
	check_timer();
	if (!acpi_ioapic)
		print_IO_APIC();
}

struct sysfs_ioapic_data {
	struct sys_device dev;
	struct IO_APIC_route_entry entry[0];
};
static struct sysfs_ioapic_data * mp_ioapic_data[MAX_IO_APICS];

static int ioapic_suspend(struct sys_device *dev, pm_message_t state)
{
	struct IO_APIC_route_entry *entry;
	struct sysfs_ioapic_data *data;
	int i;

	data = container_of(dev, struct sysfs_ioapic_data, dev);
	entry = data->entry;
	for (i = 0; i < nr_ioapic_registers[dev->id]; i ++, entry ++ )
		*entry = ioapic_read_entry(dev->id, i);

	return 0;
}

static int ioapic_resume(struct sys_device *dev)
{
	struct IO_APIC_route_entry *entry;
	struct sysfs_ioapic_data *data;
	unsigned long flags;
	union IO_APIC_reg_00 reg_00;
	int i;

	data = container_of(dev, struct sysfs_ioapic_data, dev);
	entry = data->entry;

	spin_lock_irqsave(&ioapic_lock, flags);
	reg_00.raw = io_apic_read(dev->id, 0);
	if (reg_00.bits.ID != mp_ioapics[dev->id].mpc_apicid) {
		reg_00.bits.ID = mp_ioapics[dev->id].mpc_apicid;
		io_apic_write(dev->id, 0, reg_00.raw);
	}
	spin_unlock_irqrestore(&ioapic_lock, flags);
	for (i = 0; i < nr_ioapic_registers[dev->id]; i++)
		ioapic_write_entry(dev->id, i, entry[i]);

	return 0;
}

static struct sysdev_class ioapic_sysdev_class = {
	.name = "ioapic",
	.suspend = ioapic_suspend,
	.resume = ioapic_resume,
};

static int __init ioapic_init_sysfs(void)
{
	struct sys_device * dev;
	int i, size, error;

	error = sysdev_class_register(&ioapic_sysdev_class);
	if (error)
		return error;

	for (i = 0; i < nr_ioapics; i++ ) {
		size = sizeof(struct sys_device) + nr_ioapic_registers[i]
			* sizeof(struct IO_APIC_route_entry);
		mp_ioapic_data[i] = kzalloc(size, GFP_KERNEL);
		if (!mp_ioapic_data[i]) {
			printk(KERN_ERR "Can't suspend/resume IOAPIC %d\n", i);
			continue;
		}
		dev = &mp_ioapic_data[i]->dev;
		dev->id = i;
		dev->cls = &ioapic_sysdev_class;
		error = sysdev_register(dev);
		if (error) {
			kfree(mp_ioapic_data[i]);
			mp_ioapic_data[i] = NULL;
			printk(KERN_ERR "Can't suspend/resume IOAPIC %d\n", i);
			continue;
		}
	}

	return 0;
}

device_initcall(ioapic_init_sysfs);

/*
 * Dynamic irq allocate and deallocation
 */
int create_irq(void)
{
	/* Allocate an unused irq */
	int irq;
	int new;
	unsigned long flags;

	irq = -ENOSPC;
	spin_lock_irqsave(&vector_lock, flags);
	for (new = (NR_IRQS - 1); new >= 0; new--) {
		if (platform_legacy_irq(new))
			continue;
		if (irq_cfg[new].vector != 0)
			continue;
		if (__assign_irq_vector(new, TARGET_CPUS) == 0)
			irq = new;
		break;
	}
	spin_unlock_irqrestore(&vector_lock, flags);

	if (irq >= 0) {
		dynamic_irq_init(irq);
	}
	return irq;
}

void destroy_irq(unsigned int irq)
{
	unsigned long flags;

	dynamic_irq_cleanup(irq);

	spin_lock_irqsave(&vector_lock, flags);
	__clear_irq_vector(irq);
	spin_unlock_irqrestore(&vector_lock, flags);
}

/*
 * MSI message composition
 */
#ifdef CONFIG_PCI_MSI
static int msi_compose_msg(struct pci_dev *pdev, unsigned int irq, struct msi_msg *msg)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	int err;
	unsigned dest;
	cpumask_t tmp;

	tmp = TARGET_CPUS;
	err = assign_irq_vector(irq, tmp);
	if (!err) {
		cpus_and(tmp, cfg->domain, tmp);
		dest = cpu_mask_to_apicid(tmp);

		msg->address_hi = MSI_ADDR_BASE_HI;
		msg->address_lo =
			MSI_ADDR_BASE_LO |
			((INT_DEST_MODE == 0) ?
				MSI_ADDR_DEST_MODE_PHYSICAL:
				MSI_ADDR_DEST_MODE_LOGICAL) |
			((INT_DELIVERY_MODE != dest_LowestPrio) ?
				MSI_ADDR_REDIRECTION_CPU:
				MSI_ADDR_REDIRECTION_LOWPRI) |
			MSI_ADDR_DEST_ID(dest);

		msg->data =
			MSI_DATA_TRIGGER_EDGE |
			MSI_DATA_LEVEL_ASSERT |
			((INT_DELIVERY_MODE != dest_LowestPrio) ?
				MSI_DATA_DELIVERY_FIXED:
				MSI_DATA_DELIVERY_LOWPRI) |
			MSI_DATA_VECTOR(cfg->vector);
	}
	return err;
}

#ifdef CONFIG_SMP
static void set_msi_irq_affinity(unsigned int irq, cpumask_t mask)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	struct msi_msg msg;
	unsigned int dest;
	cpumask_t tmp;

	cpus_and(tmp, mask, cpu_online_map);
	if (cpus_empty(tmp))
		return;

	if (assign_irq_vector(irq, mask))
		return;

	cpus_and(tmp, cfg->domain, mask);
	dest = cpu_mask_to_apicid(tmp);

	read_msi_msg(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	write_msi_msg(irq, &msg);
	irq_desc[irq].affinity = mask;
}
#endif /* CONFIG_SMP */

/*
 * IRQ Chip for MSI PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI or MSI-X Capability Structure.
 */
static struct irq_chip msi_chip = {
	.name		= "PCI-MSI",
	.unmask		= unmask_msi_irq,
	.mask		= mask_msi_irq,
	.ack		= ack_apic_edge,
#ifdef CONFIG_SMP
	.set_affinity	= set_msi_irq_affinity,
#endif
	.retrigger	= ioapic_retrigger_irq,
};

int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc)
{
	struct msi_msg msg;
	int irq, ret;
	irq = create_irq();
	if (irq < 0)
		return irq;

	ret = msi_compose_msg(dev, irq, &msg);
	if (ret < 0) {
		destroy_irq(irq);
		return ret;
	}

	set_irq_msi(irq, desc);
	write_msi_msg(irq, &msg);

	set_irq_chip_and_handler_name(irq, &msi_chip, handle_edge_irq, "edge");

	return 0;
}

void arch_teardown_msi_irq(unsigned int irq)
{
	destroy_irq(irq);
}

#ifdef CONFIG_DMAR
#ifdef CONFIG_SMP
static void dmar_msi_set_affinity(unsigned int irq, cpumask_t mask)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	struct msi_msg msg;
	unsigned int dest;
	cpumask_t tmp;

	cpus_and(tmp, mask, cpu_online_map);
	if (cpus_empty(tmp))
		return;

	if (assign_irq_vector(irq, mask))
		return;

	cpus_and(tmp, cfg->domain, mask);
	dest = cpu_mask_to_apicid(tmp);

	dmar_msi_read(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	dmar_msi_write(irq, &msg);
	irq_desc[irq].affinity = mask;
}
#endif /* CONFIG_SMP */

struct irq_chip dmar_msi_type = {
	.name = "DMAR_MSI",
	.unmask = dmar_msi_unmask,
	.mask = dmar_msi_mask,
	.ack = ack_apic_edge,
#ifdef CONFIG_SMP
	.set_affinity = dmar_msi_set_affinity,
#endif
	.retrigger = ioapic_retrigger_irq,
};

int arch_setup_dmar_msi(unsigned int irq)
{
	int ret;
	struct msi_msg msg;

	ret = msi_compose_msg(NULL, irq, &msg);
	if (ret < 0)
		return ret;
	dmar_msi_write(irq, &msg);
	set_irq_chip_and_handler_name(irq, &dmar_msi_type, handle_edge_irq,
		"edge");
	return 0;
}
#endif

#endif /* CONFIG_PCI_MSI */
/*
 * Hypertransport interrupt support
 */
#ifdef CONFIG_HT_IRQ

#ifdef CONFIG_SMP

static void target_ht_irq(unsigned int irq, unsigned int dest, u8 vector)
{
	struct ht_irq_msg msg;
	fetch_ht_irq_msg(irq, &msg);

	msg.address_lo &= ~(HT_IRQ_LOW_VECTOR_MASK | HT_IRQ_LOW_DEST_ID_MASK);
	msg.address_hi &= ~(HT_IRQ_HIGH_DEST_ID_MASK);

	msg.address_lo |= HT_IRQ_LOW_VECTOR(vector) | HT_IRQ_LOW_DEST_ID(dest);
	msg.address_hi |= HT_IRQ_HIGH_DEST_ID(dest);

	write_ht_irq_msg(irq, &msg);
}

static void set_ht_irq_affinity(unsigned int irq, cpumask_t mask)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	unsigned int dest;
	cpumask_t tmp;

	cpus_and(tmp, mask, cpu_online_map);
	if (cpus_empty(tmp))
		return;

	if (assign_irq_vector(irq, mask))
		return;

	cpus_and(tmp, cfg->domain, mask);
	dest = cpu_mask_to_apicid(tmp);

	target_ht_irq(irq, dest, cfg->vector);
	irq_desc[irq].affinity = mask;
}
#endif

static struct irq_chip ht_irq_chip = {
	.name		= "PCI-HT",
	.mask		= mask_ht_irq,
	.unmask		= unmask_ht_irq,
	.ack		= ack_apic_edge,
#ifdef CONFIG_SMP
	.set_affinity	= set_ht_irq_affinity,
#endif
	.retrigger	= ioapic_retrigger_irq,
};

int arch_setup_ht_irq(unsigned int irq, struct pci_dev *dev)
{
	struct irq_cfg *cfg = irq_cfg + irq;
	int err;
	cpumask_t tmp;

	tmp = TARGET_CPUS;
	err = assign_irq_vector(irq, tmp);
	if (!err) {
		struct ht_irq_msg msg;
		unsigned dest;

		cpus_and(tmp, cfg->domain, tmp);
		dest = cpu_mask_to_apicid(tmp);

		msg.address_hi = HT_IRQ_HIGH_DEST_ID(dest);

		msg.address_lo =
			HT_IRQ_LOW_BASE |
			HT_IRQ_LOW_DEST_ID(dest) |
			HT_IRQ_LOW_VECTOR(cfg->vector) |
			((INT_DEST_MODE == 0) ?
				HT_IRQ_LOW_DM_PHYSICAL :
				HT_IRQ_LOW_DM_LOGICAL) |
			HT_IRQ_LOW_RQEOI_EDGE |
			((INT_DELIVERY_MODE != dest_LowestPrio) ?
				HT_IRQ_LOW_MT_FIXED :
				HT_IRQ_LOW_MT_ARBITRATED) |
			HT_IRQ_LOW_IRQ_MASKED;

		write_ht_irq_msg(irq, &msg);

		set_irq_chip_and_handler_name(irq, &ht_irq_chip,
					      handle_edge_irq, "edge");
	}
	return err;
}
#endif /* CONFIG_HT_IRQ */

/* --------------------------------------------------------------------------
                          ACPI-based IOAPIC Configuration
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI

#define IO_APIC_MAX_ID		0xFE

int __init io_apic_get_redir_entries (int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return reg_01.bits.entries;
}


int io_apic_set_pci_routing (int ioapic, int pin, int irq, int triggering, int polarity)
{
	if (!IO_APIC_IRQ(irq)) {
		apic_printk(APIC_QUIET,KERN_ERR "IOAPIC[%d]: Invalid reference to IRQ 0\n",
			ioapic);
		return -EINVAL;
	}

	/*
	 * IRQs < 16 are already in the irq_2_pin[] map
	 */
	if (irq >= 16)
		add_pin_to_irq(irq, ioapic, pin);

	setup_IO_APIC_irq(ioapic, pin, irq, triggering, polarity);

	return 0;
}


int acpi_get_override_irq(int bus_irq, int *trigger, int *polarity)
{
	int i;

	if (skip_ioapic_setup)
		return -1;

	for (i = 0; i < mp_irq_entries; i++)
		if (mp_irqs[i].mpc_irqtype == mp_INT &&
		    mp_irqs[i].mpc_srcbusirq == bus_irq)
			break;
	if (i >= mp_irq_entries)
		return -1;

	*trigger = irq_trigger(i);
	*polarity = irq_polarity(i);
	return 0;
}

#endif /* CONFIG_ACPI */

/*
 * This function currently is only a helper for the i386 smp boot process where
 * we need to reprogram the ioredtbls to cater for the cpus which have come online
 * so mask in all cases should simply be TARGET_CPUS
 */
#ifdef CONFIG_SMP
void __init setup_ioapic_dest(void)
{
	int pin, ioapic, irq, irq_entry;

	if (skip_ioapic_setup == 1)
		return;

	for (ioapic = 0; ioapic < nr_ioapics; ioapic++) {
		for (pin = 0; pin < nr_ioapic_registers[ioapic]; pin++) {
			irq_entry = find_irq_entry(ioapic, pin, mp_INT);
			if (irq_entry == -1)
				continue;
			irq = pin_2_irq(irq_entry, ioapic, pin);

			/* setup_IO_APIC_irqs could fail to get vector for some device
			 * when you have too many devices, because at that time only boot
			 * cpu is online.
			 */
			if (!irq_cfg[irq].vector)
				setup_IO_APIC_irq(ioapic, pin, irq,
						  irq_trigger(irq_entry),
						  irq_polarity(irq_entry));
			else
				set_ioapic_affinity_irq(irq, TARGET_CPUS);
		}

	}
}
#endif

#define IOAPIC_RESOURCE_NAME_SIZE 11

static struct resource *ioapic_resources;

static struct resource * __init ioapic_setup_resources(void)
{
	unsigned long n;
	struct resource *res;
	char *mem;
	int i;

	if (nr_ioapics <= 0)
		return NULL;

	n = IOAPIC_RESOURCE_NAME_SIZE + sizeof(struct resource);
	n *= nr_ioapics;

	mem = alloc_bootmem(n);
	res = (void *)mem;

	if (mem != NULL) {
		mem += sizeof(struct resource) * nr_ioapics;

		for (i = 0; i < nr_ioapics; i++) {
			res[i].name = mem;
			res[i].flags = IORESOURCE_MEM | IORESOURCE_BUSY;
			sprintf(mem,  "IOAPIC %u", i);
			mem += IOAPIC_RESOURCE_NAME_SIZE;
		}
	}

	ioapic_resources = res;

	return res;
}

void __init ioapic_init_mappings(void)
{
	unsigned long ioapic_phys, idx = FIX_IO_APIC_BASE_0;
	struct resource *ioapic_res;
	int i;

	ioapic_res = ioapic_setup_resources();
	for (i = 0; i < nr_ioapics; i++) {
		if (smp_found_config) {
			ioapic_phys = mp_ioapics[i].mpc_apicaddr;
		} else {
			ioapic_phys = (unsigned long)
				alloc_bootmem_pages(PAGE_SIZE);
			ioapic_phys = __pa(ioapic_phys);
		}
		set_fixmap_nocache(idx, ioapic_phys);
		apic_printk(APIC_VERBOSE,
			    "mapped IOAPIC to %016lx (%016lx)\n",
			    __fix_to_virt(idx), ioapic_phys);
		idx++;

		if (ioapic_res != NULL) {
			ioapic_res->start = ioapic_phys;
			ioapic_res->end = ioapic_phys + (4 * 1024) - 1;
			ioapic_res++;
		}
	}
}

static int __init ioapic_insert_resources(void)
{
	int i;
	struct resource *r = ioapic_resources;

	if (!r) {
		printk(KERN_ERR
		       "IO APIC resources could be not be allocated.\n");
		return -1;
	}

	for (i = 0; i < nr_ioapics; i++) {
		insert_resource(&iomem_resource, r);
		r++;
	}

	return 0;
}

/* Insert the IO APIC resources after PCI initialization has occured to handle
 * IO APICS that are mapped in on a BAR in PCI space. */
late_initcall(ioapic_insert_resources);

