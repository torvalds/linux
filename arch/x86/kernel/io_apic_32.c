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
#include <linux/bootmem.h>
#include <linux/mc146818rtc.h>
#include <linux/compiler.h>
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/htirq.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>	/* time_after() */

#include <asm/io.h>
#include <asm/smp.h>
#include <asm/desc.h>
#include <asm/timer.h>
#include <asm/i8259.h>
#include <asm/nmi.h>
#include <asm/msidef.h>
#include <asm/hypertransport.h>
#include <asm/setup.h>

#include <mach_apic.h>
#include <mach_apicdef.h>

#define __apicdebuginit(type) static type __init

int (*ioapic_renumber_irq)(int ioapic, int irq);
atomic_t irq_mis_count;

/* Where if anywhere is the i8259 connect in external int mode */
static struct { int pin, apic; } ioapic_i8259 = { -1, -1 };

static DEFINE_SPINLOCK(ioapic_lock);
DEFINE_SPINLOCK(vector_lock);

int timer_through_8259 __initdata;

/*
 *	Is the SiS APIC rmw bug present ?
 *	-1 = don't know, 0 = no, 1 = yes
 */
int sis_apic_bug = -1;

int first_free_entry;
/*
 * # of IRQ routing registers
 */
int nr_ioapic_registers[MAX_IO_APICS];

/* I/O APIC entries */
struct mp_config_ioapic mp_ioapics[MAX_IO_APICS];
int nr_ioapics;

/* MP IRQ source entries */
struct mp_config_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* # of MP IRQ source entries */
int mp_irq_entries;

#if defined (CONFIG_MCA) || defined (CONFIG_EISA)
int mp_bus_id_to_type[MAX_MP_BUSSES];
#endif

DECLARE_BITMAP(mp_bus_not_pci, MAX_MP_BUSSES);

static int disable_timer_pin_1 __initdata;

struct irq_cfg;

struct irq_cfg {
	unsigned int irq;
	struct irq_cfg *next;
	u8 vector;
};


/* irq_cfg is indexed by the sum of all RTEs in all I/O APICs. */
static struct irq_cfg irq_cfg_legacy[] __initdata = {
	[0]  = { .irq =  0, .vector = IRQ0_VECTOR,  },
	[1]  = { .irq =  1, .vector = IRQ1_VECTOR,  },
	[2]  = { .irq =  2, .vector = IRQ2_VECTOR,  },
	[3]  = { .irq =  3, .vector = IRQ3_VECTOR,  },
	[4]  = { .irq =  4, .vector = IRQ4_VECTOR,  },
	[5]  = { .irq =  5, .vector = IRQ5_VECTOR,  },
	[6]  = { .irq =  6, .vector = IRQ6_VECTOR,  },
	[7]  = { .irq =  7, .vector = IRQ7_VECTOR,  },
	[8]  = { .irq =  8, .vector = IRQ8_VECTOR,  },
	[9]  = { .irq =  9, .vector = IRQ9_VECTOR,  },
	[10] = { .irq = 10, .vector = IRQ10_VECTOR, },
	[11] = { .irq = 11, .vector = IRQ11_VECTOR, },
	[12] = { .irq = 12, .vector = IRQ12_VECTOR, },
	[13] = { .irq = 13, .vector = IRQ13_VECTOR, },
	[14] = { .irq = 14, .vector = IRQ14_VECTOR, },
	[15] = { .irq = 15, .vector = IRQ15_VECTOR, },
};

static struct irq_cfg irq_cfg_init = { .irq =  -1U, };
/* need to be biger than size of irq_cfg_legacy */
static int nr_irq_cfg = 32;

static int __init parse_nr_irq_cfg(char *arg)
{
	if (arg) {
		nr_irq_cfg = simple_strtoul(arg, NULL, 0);
		if (nr_irq_cfg < 32)
			nr_irq_cfg = 32;
	}
	return 0;
}

early_param("nr_irq_cfg", parse_nr_irq_cfg);

static void init_one_irq_cfg(struct irq_cfg *cfg)
{
	memcpy(cfg, &irq_cfg_init, sizeof(struct irq_cfg));
}

static struct irq_cfg *irq_cfgx;
static struct irq_cfg *irq_cfgx_free;
static void __init init_work(void *data)
{
	struct dyn_array *da = data;
	struct irq_cfg *cfg;
	int legacy_count;
	int i;

	cfg = *da->name;

	memcpy(cfg, irq_cfg_legacy, sizeof(irq_cfg_legacy));

	legacy_count = sizeof(irq_cfg_legacy)/sizeof(irq_cfg_legacy[0]);
	for (i = legacy_count; i < *da->nr; i++)
		init_one_irq_cfg(&cfg[i]);

	for (i = 1; i < *da->nr; i++)
		cfg[i-1].next = &cfg[i];

	irq_cfgx_free = &irq_cfgx[legacy_count];
	irq_cfgx[legacy_count - 1].next = NULL;
}

#define for_each_irq_cfg(cfg)           \
	for (cfg = irq_cfgx; cfg; cfg = cfg->next)

DEFINE_DYN_ARRAY(irq_cfgx, sizeof(struct irq_cfg), nr_irq_cfg, PAGE_SIZE, init_work);

static struct irq_cfg *irq_cfg(unsigned int irq)
{
	struct irq_cfg *cfg;

	cfg = irq_cfgx;
	while (cfg) {
		if (cfg->irq == irq)
			return cfg;

		cfg = cfg->next;
	}

	return NULL;
}

static struct irq_cfg *irq_cfg_alloc(unsigned int irq)
{
	struct irq_cfg *cfg, *cfg_pri;
	int i;
	int count = 0;

	cfg_pri = cfg = irq_cfgx;
	while (cfg) {
		if (cfg->irq == irq)
			return cfg;

		cfg_pri = cfg;
		cfg = cfg->next;
		count++;
	}

	if (!irq_cfgx_free) {
		unsigned long phys;
		unsigned long total_bytes;
		/*
		 *  we run out of pre-allocate ones, allocate more
		 */
		printk(KERN_DEBUG "try to get more irq_cfg %d\n", nr_irq_cfg);

		total_bytes = sizeof(struct irq_cfg) * nr_irq_cfg;
		if (after_bootmem)
			cfg = kzalloc(total_bytes, GFP_ATOMIC);
		else
			cfg = __alloc_bootmem_nopanic(total_bytes, PAGE_SIZE, 0);

		if (!cfg)
			panic("please boot with nr_irq_cfg= %d\n", count * 2);

		phys = __pa(cfg);
		printk(KERN_DEBUG "irq_irq ==> [%#lx - %#lx]\n", phys, phys + total_bytes);

		for (i = 0; i < nr_irq_cfg; i++)
			init_one_irq_cfg(&cfg[i]);

		for (i = 1; i < nr_irq_cfg; i++)
			cfg[i-1].next = &cfg[i];

		irq_cfgx_free = cfg;
	}

	cfg = irq_cfgx_free;
	irq_cfgx_free = irq_cfgx_free->next;
	cfg->next = NULL;
	if (cfg_pri)
		cfg_pri->next = cfg;
	else
		irq_cfgx = cfg;
	cfg->irq = irq;
	printk(KERN_DEBUG "found new irq_cfg for irq %d\n", cfg->irq);

#ifdef CONFIG_HAVE_SPARSE_IRQ_DEBUG
	{
		/* dump the results */
		struct irq_cfg *cfg;
		unsigned long phys;
		unsigned long bytes = sizeof(struct irq_cfg);

		printk(KERN_DEBUG "=========================== %d\n", irq);
		printk(KERN_DEBUG "irq_cfg dump after get that for %d\n", irq);
		for_each_irq_cfg(cfg) {
			phys = __pa(cfg);
			printk(KERN_DEBUG "irq_cfg %d ==> [%#lx - %#lx]\n", cfg->irq, phys, phys + bytes);
		}
		printk(KERN_DEBUG "===========================\n");
	}
#endif
	return cfg;
}

/*
 * Rough estimation of how many shared IRQs there are, can
 * be changed anytime.
 */
int pin_map_size;

/*
 * This is performance-critical, we want to do it O(1)
 *
 * the indexing order of this array favors 1:1 mappings
 * between pins and IRQs.
 */

static struct irq_pin_list {
	int apic, pin, next;
} *irq_2_pin;

DEFINE_DYN_ARRAY(irq_2_pin, sizeof(struct irq_pin_list), pin_map_size, 16, NULL);

struct io_apic {
	unsigned int index;
	unsigned int unused[3];
	unsigned int data;
};

static __attribute_const__ struct io_apic __iomem *io_apic_base(int idx)
{
	return (void __iomem *) __fix_to_virt(FIX_IO_APIC_BASE_0 + idx)
		+ (mp_ioapics[idx].mp_apicaddr & ~PAGE_MASK);
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
 *
 * Older SiS APIC requires we rewrite the index register
 */
static inline void io_apic_modify(unsigned int apic, unsigned int reg, unsigned int value)
{
	volatile struct io_apic __iomem *io_apic = io_apic_base(apic);
	if (sis_apic_bug)
		writel(reg, &io_apic->index);
	writel(value, &io_apic->data);
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

/*
 * The common case is 1:1 IRQ<->pin mappings. Sometimes there are
 * shared ISA-space IRQs, so we have to support them. We are super
 * fast in the common case, and fast for shared ISA-space IRQs.
 */
static void add_pin_to_irq(unsigned int irq, int apic, int pin)
{
	struct irq_pin_list *entry = irq_2_pin + irq;

	irq_cfg_alloc(irq);
	while (entry->next)
		entry = irq_2_pin + entry->next;

	if (entry->pin != -1) {
		entry->next = first_free_entry;
		entry = irq_2_pin + entry->next;
		if (++first_free_entry >= pin_map_size)
			panic("io_apic.c: whoops");
	}
	entry->apic = apic;
	entry->pin = pin;
}

/*
 * Reroute an IRQ to a different pin.
 */
static void __init replace_pin_at_irq(unsigned int irq,
				      int oldapic, int oldpin,
				      int newapic, int newpin)
{
	struct irq_pin_list *entry = irq_2_pin + irq;

	while (1) {
		if (entry->apic == oldapic && entry->pin == oldpin) {
			entry->apic = newapic;
			entry->pin = newpin;
		}
		if (!entry->next)
			break;
		entry = irq_2_pin + entry->next;
	}
}

static void __modify_IO_APIC_irq(unsigned int irq, unsigned long enable, unsigned long disable)
{
	struct irq_pin_list *entry = irq_2_pin + irq;
	unsigned int pin, reg;

	for (;;) {
		pin = entry->pin;
		if (pin == -1)
			break;
		reg = io_apic_read(entry->apic, 0x10 + pin*2);
		reg &= ~disable;
		reg |= enable;
		io_apic_modify(entry->apic, 0x10 + pin*2, reg);
		if (!entry->next)
			break;
		entry = irq_2_pin + entry->next;
	}
}

/* mask = 1 */
static void __mask_IO_APIC_irq(unsigned int irq)
{
	__modify_IO_APIC_irq(irq, IO_APIC_REDIR_MASKED, 0);
}

/* mask = 0 */
static void __unmask_IO_APIC_irq(unsigned int irq)
{
	__modify_IO_APIC_irq(irq, 0, IO_APIC_REDIR_MASKED);
}

/* mask = 1, trigger = 0 */
static void __mask_and_edge_IO_APIC_irq(unsigned int irq)
{
	__modify_IO_APIC_irq(irq, IO_APIC_REDIR_MASKED,
				IO_APIC_REDIR_LEVEL_TRIGGER);
}

/* mask = 0, trigger = 1 */
static void __unmask_and_level_IO_APIC_irq(unsigned int irq)
{
	__modify_IO_APIC_irq(irq, IO_APIC_REDIR_LEVEL_TRIGGER,
				IO_APIC_REDIR_MASKED);
}

static void mask_IO_APIC_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	__mask_IO_APIC_irq(irq);
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void unmask_IO_APIC_irq(unsigned int irq)
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

static void clear_IO_APIC(void)
{
	int apic, pin;

	for (apic = 0; apic < nr_ioapics; apic++)
		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++)
			clear_IO_APIC_pin(apic, pin);
}

#ifdef CONFIG_SMP
static void set_ioapic_affinity_irq(unsigned int irq, cpumask_t cpumask)
{
	unsigned long flags;
	int pin;
	struct irq_pin_list *entry = irq_2_pin + irq;
	unsigned int apicid_value;
	cpumask_t tmp;
	struct irq_desc *desc;

	cpus_and(tmp, cpumask, cpu_online_map);
	if (cpus_empty(tmp))
		tmp = TARGET_CPUS;

	cpus_and(cpumask, tmp, CPU_MASK_ALL);

	apicid_value = cpu_mask_to_apicid(cpumask);
	/* Prepare to do the io_apic_write */
	apicid_value = apicid_value << 24;
	spin_lock_irqsave(&ioapic_lock, flags);
	for (;;) {
		pin = entry->pin;
		if (pin == -1)
			break;
		io_apic_write(entry->apic, 0x10 + 1 + pin*2, apicid_value);
		if (!entry->next)
			break;
		entry = irq_2_pin + entry->next;
	}
	desc = irq_to_desc(irq);
	desc->affinity = cpumask;
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

#endif /* CONFIG_SMP */

#ifndef CONFIG_SMP
void send_IPI_self(int vector)
{
	unsigned int cfg;

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();
	cfg = APIC_DM_FIXED | APIC_DEST_SELF | vector | APIC_DEST_LOGICAL;
	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write(APIC_ICR, cfg);
}
#endif /* !CONFIG_SMP */


/*
 * support for broken MP BIOSs, enables hand-redirection of PIRQ0-7 to
 * specific CPU-side IRQs.
 */

#define MAX_PIRQS 8
static int pirq_entries [MAX_PIRQS];
static int pirqs_enabled;
int skip_ioapic_setup;

static int __init ioapic_pirq_setup(char *str)
{
	int i, max;
	int ints[MAX_PIRQS+1];

	get_options(str, ARRAY_SIZE(ints), ints);

	for (i = 0; i < MAX_PIRQS; i++)
		pirq_entries[i] = -1;

	pirqs_enabled = 1;
	apic_printk(APIC_VERBOSE, KERN_INFO
			"PIRQ redirection, working around broken MP-BIOS.\n");
	max = MAX_PIRQS;
	if (ints[0] < MAX_PIRQS)
		max = ints[0];

	for (i = 0; i < max; i++) {
		apic_printk(APIC_VERBOSE, KERN_DEBUG
				"... PIRQ%d -> IRQ %d\n", i, ints[i+1]);
		/*
		 * PIRQs are mapped upside down, usually.
		 */
		pirq_entries[MAX_PIRQS-i-1] = ints[i+1];
	}
	return 1;
}

__setup("pirq=", ioapic_pirq_setup);

/*
 * Find the IRQ entry number of a certain pin.
 */
static int find_irq_entry(int apic, int pin, int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++)
		if (mp_irqs[i].mp_irqtype == type &&
		    (mp_irqs[i].mp_dstapic == mp_ioapics[apic].mp_apicid ||
		     mp_irqs[i].mp_dstapic == MP_APIC_ALL) &&
		    mp_irqs[i].mp_dstirq == pin)
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
		int lbus = mp_irqs[i].mp_srcbus;

		if (test_bit(lbus, mp_bus_not_pci) &&
		    (mp_irqs[i].mp_irqtype == type) &&
		    (mp_irqs[i].mp_srcbusirq == irq))

			return mp_irqs[i].mp_dstirq;
	}
	return -1;
}

static int __init find_isa_irq_apic(int irq, int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++) {
		int lbus = mp_irqs[i].mp_srcbus;

		if (test_bit(lbus, mp_bus_not_pci) &&
		    (mp_irqs[i].mp_irqtype == type) &&
		    (mp_irqs[i].mp_srcbusirq == irq))
			break;
	}
	if (i < mp_irq_entries) {
		int apic;
		for (apic = 0; apic < nr_ioapics; apic++) {
			if (mp_ioapics[apic].mp_apicid == mp_irqs[i].mp_dstapic)
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

	apic_printk(APIC_DEBUG, "querying PCI -> IRQ mapping bus:%d, "
		"slot:%d, pin:%d.\n", bus, slot, pin);
	if (test_bit(bus, mp_bus_not_pci)) {
		printk(KERN_WARNING "PCI BIOS passed nonexistent PCI bus %d!\n", bus);
		return -1;
	}
	for (i = 0; i < mp_irq_entries; i++) {
		int lbus = mp_irqs[i].mp_srcbus;

		for (apic = 0; apic < nr_ioapics; apic++)
			if (mp_ioapics[apic].mp_apicid == mp_irqs[i].mp_dstapic ||
			    mp_irqs[i].mp_dstapic == MP_APIC_ALL)
				break;

		if (!test_bit(lbus, mp_bus_not_pci) &&
		    !mp_irqs[i].mp_irqtype &&
		    (bus == lbus) &&
		    (slot == ((mp_irqs[i].mp_srcbusirq >> 2) & 0x1f))) {
			int irq = pin_2_irq(i, apic, mp_irqs[i].mp_dstirq);

			if (!(apic || IO_APIC_IRQ(irq)))
				continue;

			if (pin == (mp_irqs[i].mp_srcbusirq & 3))
				return irq;
			/*
			 * Use the first all-but-pin matching entry as a
			 * best-guess fuzzy result for broken mptables.
			 */
			if (best_guess < 0)
				best_guess = irq;
		}
	}
	return best_guess;
}
EXPORT_SYMBOL(IO_APIC_get_PCI_irq_vector);

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
			set_ioapic_affinity_irq(irq, TARGET_CPUS);
		}

	}
}
#endif

#if defined(CONFIG_EISA) || defined(CONFIG_MCA)
/*
 * EISA Edge/Level control register, ELCR
 */
static int EISA_ELCR(unsigned int irq)
{
	if (irq < 16) {
		unsigned int port = 0x4d0 + (irq >> 3);
		return (inb(port) >> (irq & 7)) & 1;
	}
	apic_printk(APIC_VERBOSE, KERN_INFO
			"Broken MPtable reports ISA irq %d\n", irq);
	return 0;
}
#endif

/* ISA interrupts are always polarity zero edge triggered,
 * when listed as conforming in the MP table. */

#define default_ISA_trigger(idx)	(0)
#define default_ISA_polarity(idx)	(0)

/* EISA interrupts are always polarity zero and can be edge or level
 * trigger depending on the ELCR value.  If an interrupt is listed as
 * EISA conforming in the MP table, that means its trigger type must
 * be read in from the ELCR */

#define default_EISA_trigger(idx)	(EISA_ELCR(mp_irqs[idx].mp_srcbusirq))
#define default_EISA_polarity(idx)	default_ISA_polarity(idx)

/* PCI interrupts are always polarity one level triggered,
 * when listed as conforming in the MP table. */

#define default_PCI_trigger(idx)	(1)
#define default_PCI_polarity(idx)	(1)

/* MCA interrupts are always polarity zero level triggered,
 * when listed as conforming in the MP table. */

#define default_MCA_trigger(idx)	(1)
#define default_MCA_polarity(idx)	default_ISA_polarity(idx)

static int MPBIOS_polarity(int idx)
{
	int bus = mp_irqs[idx].mp_srcbus;
	int polarity;

	/*
	 * Determine IRQ line polarity (high active or low active):
	 */
	switch (mp_irqs[idx].mp_irqflag & 3) {
	case 0: /* conforms, ie. bus-type dependent polarity */
	{
		polarity = test_bit(bus, mp_bus_not_pci)?
			default_ISA_polarity(idx):
			default_PCI_polarity(idx);
		break;
	}
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
	int bus = mp_irqs[idx].mp_srcbus;
	int trigger;

	/*
	 * Determine IRQ trigger mode (edge or level sensitive):
	 */
	switch ((mp_irqs[idx].mp_irqflag>>2) & 3) {
	case 0: /* conforms, ie. bus-type dependent */
	{
		trigger = test_bit(bus, mp_bus_not_pci)?
				default_ISA_trigger(idx):
				default_PCI_trigger(idx);
#if defined(CONFIG_EISA) || defined(CONFIG_MCA)
		switch (mp_bus_id_to_type[bus]) {
		case MP_BUS_ISA: /* ISA pin */
		{
			/* set before the switch */
			break;
		}
		case MP_BUS_EISA: /* EISA pin */
		{
			trigger = default_EISA_trigger(idx);
			break;
		}
		case MP_BUS_PCI: /* PCI pin */
		{
			/* set before the switch */
			break;
		}
		case MP_BUS_MCA: /* MCA pin */
		{
			trigger = default_MCA_trigger(idx);
			break;
		}
		default:
		{
			printk(KERN_WARNING "broken BIOS!!\n");
			trigger = 1;
			break;
		}
	}
#endif
		break;
	}
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
	int bus = mp_irqs[idx].mp_srcbus;

	/*
	 * Debugging check, we are in big trouble if this message pops up!
	 */
	if (mp_irqs[idx].mp_dstirq != pin)
		printk(KERN_ERR "broken BIOS or MPTABLE parser, ayiee!!\n");

	if (test_bit(bus, mp_bus_not_pci))
		irq = mp_irqs[idx].mp_srcbusirq;
	else {
		/*
		 * PCI IRQs are mapped in order
		 */
		i = irq = 0;
		while (i < apic)
			irq += nr_ioapic_registers[i++];
		irq += pin;

		/*
		 * For MPS mode, so far only needed by ES7000 platform
		 */
		if (ioapic_renumber_irq)
			irq = ioapic_renumber_irq(apic, irq);
	}

	/*
	 * PCI IRQ command line redirection. Yes, limits are hardcoded.
	 */
	if ((pin >= 16) && (pin <= 23)) {
		if (pirq_entries[pin-16] != -1) {
			if (!pirq_entries[pin-16]) {
				apic_printk(APIC_VERBOSE, KERN_DEBUG
						"disabling PIRQ%d\n", pin-16);
			} else {
				irq = pirq_entries[pin-16];
				apic_printk(APIC_VERBOSE, KERN_DEBUG
						"using PIRQ%d -> IRQ %d\n",
						pin-16, irq);
			}
		}
	}
	return irq;
}

static inline int IO_APIC_irq_trigger(int irq)
{
	int apic, idx, pin;

	for (apic = 0; apic < nr_ioapics; apic++) {
		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++) {
			idx = find_irq_entry(apic, pin, mp_INT);
			if ((idx != -1) && (irq == pin_2_irq(idx, apic, pin)))
				return irq_trigger(idx);
		}
	}
	/*
	 * nonexistent IRQs are edge default
	 */
	return 0;
}


static int __assign_irq_vector(int irq)
{
	static int current_vector = FIRST_DEVICE_VECTOR, current_offset;
	int vector, offset;
	struct irq_cfg *cfg;

	BUG_ON((unsigned)irq >= nr_irqs);

	cfg = irq_cfg(irq);
	if (cfg->vector > 0)
		return cfg->vector;

	vector = current_vector;
	offset = current_offset;
next:
	vector += 8;
	if (vector >= first_system_vector) {
		offset = (offset + 1) % 8;
		vector = FIRST_DEVICE_VECTOR + offset;
	}
	if (vector == current_vector)
		return -ENOSPC;
	if (test_and_set_bit(vector, used_vectors))
		goto next;

	current_vector = vector;
	current_offset = offset;
	cfg->vector = vector;

	return vector;
}

static int assign_irq_vector(int irq)
{
	unsigned long flags;
	int vector;

	spin_lock_irqsave(&vector_lock, flags);
	vector = __assign_irq_vector(irq);
	spin_unlock_irqrestore(&vector_lock, flags);

	return vector;
}

static struct irq_chip ioapic_chip;

#define IOAPIC_AUTO	-1
#define IOAPIC_EDGE	0
#define IOAPIC_LEVEL	1

static void ioapic_register_intr(int irq, int vector, unsigned long trigger)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	if ((trigger == IOAPIC_AUTO && IO_APIC_irq_trigger(irq)) ||
	    trigger == IOAPIC_LEVEL) {
		desc->status |= IRQ_LEVEL;
		set_irq_chip_and_handler_name(irq, &ioapic_chip,
					 handle_fasteoi_irq, "fasteoi");
	} else {
		desc->status &= ~IRQ_LEVEL;
		set_irq_chip_and_handler_name(irq, &ioapic_chip,
					 handle_edge_irq, "edge");
	}
	set_intr_gate(vector, interrupt[irq]);
}

static void __init setup_IO_APIC_irqs(void)
{
	struct IO_APIC_route_entry entry;
	int apic, pin, idx, irq, first_notcon = 1, vector;

	apic_printk(APIC_VERBOSE, KERN_DEBUG "init IO_APIC IRQs\n");

	for (apic = 0; apic < nr_ioapics; apic++) {
	for (pin = 0; pin < nr_ioapic_registers[apic]; pin++) {

		/*
		 * add it to the IO-APIC irq-routing table:
		 */
		memset(&entry, 0, sizeof(entry));

		entry.delivery_mode = INT_DELIVERY_MODE;
		entry.dest_mode = INT_DEST_MODE;
		entry.mask = 0;				/* enable IRQ */
		entry.dest.logical.logical_dest =
					cpu_mask_to_apicid(TARGET_CPUS);

		idx = find_irq_entry(apic, pin, mp_INT);
		if (idx == -1) {
			if (first_notcon) {
				apic_printk(APIC_VERBOSE, KERN_DEBUG
						" IO-APIC (apicid-pin) %d-%d",
						mp_ioapics[apic].mp_apicid,
						pin);
				first_notcon = 0;
			} else
				apic_printk(APIC_VERBOSE, ", %d-%d",
					mp_ioapics[apic].mp_apicid, pin);
			continue;
		}

		if (!first_notcon) {
			apic_printk(APIC_VERBOSE, " not connected.\n");
			first_notcon = 1;
		}

		entry.trigger = irq_trigger(idx);
		entry.polarity = irq_polarity(idx);

		if (irq_trigger(idx)) {
			entry.trigger = 1;
			entry.mask = 1;
		}

		irq = pin_2_irq(idx, apic, pin);
		/*
		 * skip adding the timer int on secondary nodes, which causes
		 * a small but painful rift in the time-space continuum
		 */
		if (multi_timer_check(apic, irq))
			continue;
		else
			add_pin_to_irq(irq, apic, pin);

		if (!apic && !IO_APIC_IRQ(irq))
			continue;

		if (IO_APIC_IRQ(irq)) {
			vector = assign_irq_vector(irq);
			entry.vector = vector;
			ioapic_register_intr(irq, vector, IOAPIC_AUTO);

			if (!apic && (irq < 16))
				disable_8259A_irq(irq);
		}
		ioapic_write_entry(apic, pin, entry);
	}
	}

	if (!first_notcon)
		apic_printk(APIC_VERBOSE, " not connected.\n");
}

/*
 * Set up the timer pin, possibly with the 8259A-master behind.
 */
static void __init setup_timer_IRQ0_pin(unsigned int apic, unsigned int pin,
					int vector)
{
	struct IO_APIC_route_entry entry;

	memset(&entry, 0, sizeof(entry));

	/*
	 * We use logical delivery to get the timer IRQ
	 * to the first CPU.
	 */
	entry.dest_mode = INT_DEST_MODE;
	entry.mask = 1;					/* mask IRQ now */
	entry.dest.logical.logical_dest = cpu_mask_to_apicid(TARGET_CPUS);
	entry.delivery_mode = INT_DELIVERY_MODE;
	entry.polarity = 0;
	entry.trigger = 0;
	entry.vector = vector;

	/*
	 * The timer IRQ doesn't have to know that behind the
	 * scene we may have a 8259A-master in AEOI mode ...
	 */
	ioapic_register_intr(0, vector, IOAPIC_EDGE);

	/*
	 * Add it to the IO-APIC irq-routing table:
	 */
	ioapic_write_entry(apic, pin, entry);
}


__apicdebuginit(void) print_IO_APIC(void)
{
	int apic, i;
	union IO_APIC_reg_00 reg_00;
	union IO_APIC_reg_01 reg_01;
	union IO_APIC_reg_02 reg_02;
	union IO_APIC_reg_03 reg_03;
	unsigned long flags;

	if (apic_verbosity == APIC_QUIET)
		return;

	printk(KERN_DEBUG "number of MP IRQ sources: %d.\n", mp_irq_entries);
	for (i = 0; i < nr_ioapics; i++)
		printk(KERN_DEBUG "number of IO-APIC #%d registers: %d.\n",
		       mp_ioapics[i].mp_apicid, nr_ioapic_registers[i]);

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
	if (reg_01.bits.version >= 0x20)
		reg_03.raw = io_apic_read(apic, 3);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	printk(KERN_DEBUG "IO APIC #%d......\n", mp_ioapics[apic].mp_apicid);
	printk(KERN_DEBUG ".... register #00: %08X\n", reg_00.raw);
	printk(KERN_DEBUG ".......    : physical APIC id: %02X\n", reg_00.bits.ID);
	printk(KERN_DEBUG ".......    : Delivery Type: %X\n", reg_00.bits.delivery_type);
	printk(KERN_DEBUG ".......    : LTS          : %X\n", reg_00.bits.LTS);

	printk(KERN_DEBUG ".... register #01: %08X\n", reg_01.raw);
	printk(KERN_DEBUG ".......     : max redirection entries: %04X\n", reg_01.bits.entries);

	printk(KERN_DEBUG ".......     : PRQ implemented: %X\n", reg_01.bits.PRQ);
	printk(KERN_DEBUG ".......     : IO APIC version: %04X\n", reg_01.bits.version);

	/*
	 * Some Intel chipsets with IO APIC VERSION of 0x1? don't have reg_02,
	 * but the value of reg_02 is read as the previous read register
	 * value, so ignore it if reg_02 == reg_01.
	 */
	if (reg_01.bits.version >= 0x10 && reg_02.raw != reg_01.raw) {
		printk(KERN_DEBUG ".... register #02: %08X\n", reg_02.raw);
		printk(KERN_DEBUG ".......     : arbitration: %02X\n", reg_02.bits.arbitration);
	}

	/*
	 * Some Intel chipsets with IO APIC VERSION of 0x2? don't have reg_02
	 * or reg_03, but the value of reg_0[23] is read as the previous read
	 * register value, so ignore it if reg_03 == reg_0[12].
	 */
	if (reg_01.bits.version >= 0x20 && reg_03.raw != reg_02.raw &&
	    reg_03.raw != reg_01.raw) {
		printk(KERN_DEBUG ".... register #03: %08X\n", reg_03.raw);
		printk(KERN_DEBUG ".......     : Boot DT    : %X\n", reg_03.bits.boot_DT);
	}

	printk(KERN_DEBUG ".... IRQ redirection table:\n");

	printk(KERN_DEBUG " NR Log Phy Mask Trig IRR Pol"
			  " Stat Dest Deli Vect:   \n");

	for (i = 0; i <= reg_01.bits.entries; i++) {
		struct IO_APIC_route_entry entry;

		entry = ioapic_read_entry(apic, i);

		printk(KERN_DEBUG " %02x %03X %02X  ",
			i,
			entry.dest.logical.logical_dest,
			entry.dest.physical.physical_dest
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
	for (i = 0; i < nr_irqs; i++) {
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

__apicdebuginit(void) print_APIC_bitfield(int base)
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

__apicdebuginit(void) print_local_APIC(void *dummy)
{
	unsigned int v, ver, maxlvt;
	u64 icr;

	if (apic_verbosity == APIC_QUIET)
		return;

	printk("\n" KERN_DEBUG "printing local APIC contents on CPU#%d/%d:\n",
		smp_processor_id(), hard_smp_processor_id());
	v = apic_read(APIC_ID);
	printk(KERN_INFO "... APIC ID:      %08x (%01x)\n", v,
			GET_APIC_ID(v));
	v = apic_read(APIC_LVR);
	printk(KERN_INFO "... APIC VERSION: %08x\n", v);
	ver = GET_APIC_VERSION(v);
	maxlvt = lapic_get_maxlvt();

	v = apic_read(APIC_TASKPRI);
	printk(KERN_DEBUG "... APIC TASKPRI: %08x (%02x)\n", v, v & APIC_TPRI_MASK);

	if (APIC_INTEGRATED(ver)) {			/* !82489DX */
		v = apic_read(APIC_ARBPRI);
		printk(KERN_DEBUG "... APIC ARBPRI: %08x (%02x)\n", v,
			v & APIC_ARBPRI_MASK);
		v = apic_read(APIC_PROCPRI);
		printk(KERN_DEBUG "... APIC PROCPRI: %08x\n", v);
	}

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

	if (APIC_INTEGRATED(ver)) {		/* !82489DX */
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP. */
			apic_write(APIC_ESR, 0);
		v = apic_read(APIC_ESR);
		printk(KERN_DEBUG "... APIC ESR: %08x\n", v);
	}

	icr = apic_icr_read();
	printk(KERN_DEBUG "... APIC ICR: %08x\n", icr);
	printk(KERN_DEBUG "... APIC ICR2: %08x\n", icr >> 32);

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

__apicdebuginit(void) print_all_local_APICs(void)
{
	on_each_cpu(print_local_APIC, NULL, 1);
}

__apicdebuginit(void) print_PIC(void)
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

	outb(0x0b, 0xa0);
	outb(0x0b, 0x20);
	v = inb(0xa0) << 8 | inb(0x20);
	outb(0x0a, 0xa0);
	outb(0x0a, 0x20);

	spin_unlock_irqrestore(&i8259A_lock, flags);

	printk(KERN_DEBUG "... PIC  ISR: %04x\n", v);

	v = inb(0x4d1) << 8 | inb(0x4d0);
	printk(KERN_DEBUG "... PIC ELCR: %04x\n", v);
}

__apicdebuginit(int) print_all_ICs(void)
{
	print_PIC();
	print_all_local_APICs();
	print_IO_APIC();

	return 0;
}

fs_initcall(print_all_ICs);


static void __init enable_IO_APIC(void)
{
	union IO_APIC_reg_01 reg_01;
	int i8259_apic, i8259_pin;
	int i, apic;
	unsigned long flags;

	for (i = 0; i < pin_map_size; i++) {
		irq_2_pin[i].pin = -1;
		irq_2_pin[i].next = 0;
	}
	if (!pirqs_enabled)
		for (i = 0; i < MAX_PIRQS; i++)
			pirq_entries[i] = -1;

	/*
	 * The number of IO-APIC IRQ registers (== #pins):
	 */
	for (apic = 0; apic < nr_ioapics; apic++) {
		spin_lock_irqsave(&ioapic_lock, flags);
		reg_01.raw = io_apic_read(apic, 1);
		spin_unlock_irqrestore(&ioapic_lock, flags);
		nr_ioapic_registers[apic] = reg_01.bits.entries+1;
	}
	for (apic = 0; apic < nr_ioapics; apic++) {
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
	/* If we could not find the appropriate pin by looking at the ioapic
	 * the i8259 probably is not connected the ioapic but give the
	 * mptable a chance anyway.
	 */
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
		entry.dest.physical.physical_dest = read_apic_id();

		/*
		 * Add it to the IO-APIC irq-routing table:
		 */
		ioapic_write_entry(ioapic_i8259.apic, ioapic_i8259.pin, entry);
	}
	disconnect_bsp_APIC(ioapic_i8259.pin != -1);
}

/*
 * function to set the IO-APIC physical IDs based on the
 * values stored in the MPC table.
 *
 * by Matt Domsch <Matt_Domsch@dell.com>  Tue Dec 21 12:25:05 CST 1999
 */

static void __init setup_ioapic_ids_from_mpc(void)
{
	union IO_APIC_reg_00 reg_00;
	physid_mask_t phys_id_present_map;
	int apic;
	int i;
	unsigned char old_id;
	unsigned long flags;

	if (x86_quirks->setup_ioapic_ids && x86_quirks->setup_ioapic_ids())
		return;

	/*
	 * Don't check I/O APIC IDs for xAPIC systems.  They have
	 * no meaning without the serial APIC bus.
	 */
	if (!(boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		|| APIC_XAPIC(apic_version[boot_cpu_physical_apicid]))
		return;
	/*
	 * This is broken; anything with a real cpu count has to
	 * circumvent this idiocy regardless.
	 */
	phys_id_present_map = ioapic_phys_id_map(phys_cpu_present_map);

	/*
	 * Set the IOAPIC ID to the value stored in the MPC table.
	 */
	for (apic = 0; apic < nr_ioapics; apic++) {

		/* Read the register 0 value */
		spin_lock_irqsave(&ioapic_lock, flags);
		reg_00.raw = io_apic_read(apic, 0);
		spin_unlock_irqrestore(&ioapic_lock, flags);

		old_id = mp_ioapics[apic].mp_apicid;

		if (mp_ioapics[apic].mp_apicid >= get_physical_broadcast()) {
			printk(KERN_ERR "BIOS bug, IO-APIC#%d ID is %d in the MPC table!...\n",
				apic, mp_ioapics[apic].mp_apicid);
			printk(KERN_ERR "... fixing up to %d. (tell your hw vendor)\n",
				reg_00.bits.ID);
			mp_ioapics[apic].mp_apicid = reg_00.bits.ID;
		}

		/*
		 * Sanity check, is the ID really free? Every APIC in a
		 * system must have a unique ID or we get lots of nice
		 * 'stuck on smp_invalidate_needed IPI wait' messages.
		 */
		if (check_apicid_used(phys_id_present_map,
					mp_ioapics[apic].mp_apicid)) {
			printk(KERN_ERR "BIOS bug, IO-APIC#%d ID %d is already used!...\n",
				apic, mp_ioapics[apic].mp_apicid);
			for (i = 0; i < get_physical_broadcast(); i++)
				if (!physid_isset(i, phys_id_present_map))
					break;
			if (i >= get_physical_broadcast())
				panic("Max APIC ID exceeded!\n");
			printk(KERN_ERR "... fixing up to %d. (tell your hw vendor)\n",
				i);
			physid_set(i, phys_id_present_map);
			mp_ioapics[apic].mp_apicid = i;
		} else {
			physid_mask_t tmp;
			tmp = apicid_to_cpu_present(mp_ioapics[apic].mp_apicid);
			apic_printk(APIC_VERBOSE, "Setting %d in the "
					"phys_id_present_map\n",
					mp_ioapics[apic].mp_apicid);
			physids_or(phys_id_present_map, phys_id_present_map, tmp);
		}


		/*
		 * We need to adjust the IRQ routing table
		 * if the ID changed.
		 */
		if (old_id != mp_ioapics[apic].mp_apicid)
			for (i = 0; i < mp_irq_entries; i++)
				if (mp_irqs[i].mp_dstapic == old_id)
					mp_irqs[i].mp_dstapic
						= mp_ioapics[apic].mp_apicid;

		/*
		 * Read the right value from the MPC table and
		 * write it into the ID register.
		 */
		apic_printk(APIC_VERBOSE, KERN_INFO
			"...changing IO-APIC physical APIC ID to %d ...",
			mp_ioapics[apic].mp_apicid);

		reg_00.bits.ID = mp_ioapics[apic].mp_apicid;
		spin_lock_irqsave(&ioapic_lock, flags);
		io_apic_write(apic, 0, reg_00.raw);
		spin_unlock_irqrestore(&ioapic_lock, flags);

		/*
		 * Sanity check
		 */
		spin_lock_irqsave(&ioapic_lock, flags);
		reg_00.raw = io_apic_read(apic, 0);
		spin_unlock_irqrestore(&ioapic_lock, flags);
		if (reg_00.bits.ID != mp_ioapics[apic].mp_apicid)
			printk("could not set ID!\n");
		else
			apic_printk(APIC_VERBOSE, " ok.\n");
	}
}

int no_timer_check __initdata;

static int __init notimercheck(char *s)
{
	no_timer_check = 1;
	return 1;
}
__setup("no_timer_check", notimercheck);

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

	if (no_timer_check)
		return 1;

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
 * Startup quirk:
 *
 * Starting up a edge-triggered IO-APIC interrupt is
 * nasty - we need to make sure that we get the edge.
 * If it is already asserted for some reason, we need
 * return 1 to indicate that is was pending.
 *
 * This is not complete - we should be able to fake
 * an edge even if it isn't on the 8259A...
 *
 * (We do this for level-triggered IRQs too - it cannot hurt.)
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

static void ack_ioapic_irq(unsigned int irq)
{
	move_native_irq(irq);
	ack_APIC_irq();
}

static void ack_ioapic_quirk_irq(unsigned int irq)
{
	unsigned long v;
	int i;

	move_native_irq(irq);
/*
 * It appears there is an erratum which affects at least version 0x11
 * of I/O APIC (that's the 82093AA and cores integrated into various
 * chipsets).  Under certain conditions a level-triggered interrupt is
 * erroneously delivered as edge-triggered one but the respective IRR
 * bit gets set nevertheless.  As a result the I/O unit expects an EOI
 * message but it will never arrive and further interrupts are blocked
 * from the source.  The exact reason is so far unknown, but the
 * phenomenon was observed when two consecutive interrupt requests
 * from a given source get delivered to the same CPU and the source is
 * temporarily disabled in between.
 *
 * A workaround is to simulate an EOI message manually.  We achieve it
 * by setting the trigger mode to edge and then to level when the edge
 * trigger mode gets detected in the TMR of a local APIC for a
 * level-triggered interrupt.  We mask the source for the time of the
 * operation to prevent an edge-triggered interrupt escaping meanwhile.
 * The idea is from Manfred Spraul.  --macro
 */
	i = irq_cfg(irq)->vector;

	v = apic_read(APIC_TMR + ((i & ~0x1f) >> 1));

	ack_APIC_irq();

	if (!(v & (1 << (i & 0x1f)))) {
		atomic_inc(&irq_mis_count);
		spin_lock(&ioapic_lock);
		__mask_and_edge_IO_APIC_irq(irq);
		__unmask_and_level_IO_APIC_irq(irq);
		spin_unlock(&ioapic_lock);
	}
}

static int ioapic_retrigger_irq(unsigned int irq)
{
	send_IPI_self(irq_cfg(irq)->vector);

	return 1;
}

static struct irq_chip ioapic_chip __read_mostly = {
	.name 		= "IO-APIC",
	.startup 	= startup_ioapic_irq,
	.mask	 	= mask_IO_APIC_irq,
	.unmask	 	= unmask_IO_APIC_irq,
	.ack 		= ack_ioapic_irq,
	.eoi 		= ack_ioapic_quirk_irq,
#ifdef CONFIG_SMP
	.set_affinity 	= set_ioapic_affinity_irq,
#endif
	.retrigger	= ioapic_retrigger_irq,
};


static inline void init_IO_APIC_traps(void)
{
	int irq;
	struct irq_desc *desc;
	struct irq_cfg *cfg;

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
	for_each_irq_cfg(cfg) {
		irq = cfg->irq;
		if (IO_APIC_IRQ(irq) && !cfg->vector) {
			/*
			 * Hmm.. We don't have an entry for this,
			 * so default to an old-fashioned 8259
			 * interrupt if we can..
			 */
			if (irq < 16)
				make_8259A_irq(irq);
			else {
				desc = irq_to_desc(irq);
				/* Strange. Oh, well.. */
				desc->chip = &no_irq_chip;
			}
		}
	}
}

/*
 * The local APIC irq-chip implementation:
 */

static void ack_lapic_irq(unsigned int irq)
{
	ack_APIC_irq();
}

static void mask_lapic_irq(unsigned int irq)
{
	unsigned long v;

	v = apic_read(APIC_LVT0);
	apic_write(APIC_LVT0, v | APIC_LVT_MASKED);
}

static void unmask_lapic_irq(unsigned int irq)
{
	unsigned long v;

	v = apic_read(APIC_LVT0);
	apic_write(APIC_LVT0, v & ~APIC_LVT_MASKED);
}

static struct irq_chip lapic_chip __read_mostly = {
	.name		= "local-APIC",
	.mask		= mask_lapic_irq,
	.unmask		= unmask_lapic_irq,
	.ack		= ack_lapic_irq,
};

static void lapic_register_intr(int irq, int vector)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	desc->status &= ~IRQ_LEVEL;
	set_irq_chip_and_handler_name(irq, &lapic_chip, handle_edge_irq,
				      "edge");
	set_intr_gate(vector, interrupt[irq]);
}

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
	apic_printk(APIC_VERBOSE, KERN_INFO "activating NMI Watchdog ...");

	enable_NMI_through_LVT0();

	apic_printk(APIC_VERBOSE, " done.\n");
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
	if (pin == -1) {
		WARN_ON_ONCE(1);
		return;
	}
	apic = find_isa_irq_apic(8, mp_INT);
	if (apic == -1) {
		WARN_ON_ONCE(1);
		return;
	}

	entry0 = ioapic_read_entry(apic, pin);
	clear_IO_APIC_pin(apic, pin);

	memset(&entry1, 0, sizeof(entry1));

	entry1.dest_mode = 0;			/* physical delivery */
	entry1.mask = 0;			/* unmask IRQ now */
	entry1.dest.physical.physical_dest = hard_smp_processor_id();
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
 */
static inline void __init check_timer(void)
{
	int apic1, pin1, apic2, pin2;
	int no_pin1 = 0;
	int vector;
	unsigned int ver;
	unsigned long flags;

	local_irq_save(flags);

	ver = apic_read(APIC_LVR);
	ver = GET_APIC_VERSION(ver);

	/*
	 * get/set the timer IRQ vector:
	 */
	disable_8259A_irq(0);
	vector = assign_irq_vector(0);
	set_intr_gate(vector, interrupt[0]);

	/*
	 * As IRQ0 is to be enabled in the 8259A, the virtual
	 * wire has to be disabled in the local APIC.  Also
	 * timer interrupts need to be acknowledged manually in
	 * the 8259A for the i82489DX when using the NMI
	 * watchdog as that APIC treats NMIs as level-triggered.
	 * The AEOI mode will finish them in the 8259A
	 * automatically.
	 */
	apic_write(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_EXTINT);
	init_8259A(1);
	timer_ack = (nmi_watchdog == NMI_IO_APIC && !APIC_INTEGRATED(ver));

	pin1  = find_isa_irq_pin(0, mp_INT);
	apic1 = find_isa_irq_apic(0, mp_INT);
	pin2  = ioapic_i8259.pin;
	apic2 = ioapic_i8259.apic;

	apic_printk(APIC_QUIET, KERN_INFO "..TIMER: vector=0x%02X "
		    "apic1=%d pin1=%d apic2=%d pin2=%d\n",
		    vector, apic1, pin1, apic2, pin2);

	/*
	 * Some BIOS writers are clueless and report the ExtINTA
	 * I/O APIC input from the cascaded 8259A as the timer
	 * interrupt input.  So just in case, if only one pin
	 * was found above, try it both directly and through the
	 * 8259A.
	 */
	if (pin1 == -1) {
		pin1 = pin2;
		apic1 = apic2;
		no_pin1 = 1;
	} else if (pin2 == -1) {
		pin2 = pin1;
		apic2 = apic1;
	}

	if (pin1 != -1) {
		/*
		 * Ok, does IRQ0 through the IOAPIC work?
		 */
		if (no_pin1) {
			add_pin_to_irq(0, apic1, pin1);
			setup_timer_IRQ0_pin(apic1, pin1, vector);
		}
		unmask_IO_APIC_irq(0);
		if (timer_irq_works()) {
			if (nmi_watchdog == NMI_IO_APIC) {
				setup_nmi();
				enable_8259A_irq(0);
			}
			if (disable_timer_pin_1 > 0)
				clear_IO_APIC_pin(0, pin1);
			goto out;
		}
		clear_IO_APIC_pin(apic1, pin1);
		if (!no_pin1)
			apic_printk(APIC_QUIET, KERN_ERR "..MP-BIOS bug: "
				    "8254 timer not connected to IO-APIC\n");

		apic_printk(APIC_QUIET, KERN_INFO "...trying to set up timer "
			    "(IRQ0) through the 8259A ...\n");
		apic_printk(APIC_QUIET, KERN_INFO
			    "..... (found apic %d pin %d) ...\n", apic2, pin2);
		/*
		 * legacy devices should be connected to IO APIC #0
		 */
		replace_pin_at_irq(0, apic1, pin1, apic2, pin2);
		setup_timer_IRQ0_pin(apic2, pin2, vector);
		unmask_IO_APIC_irq(0);
		enable_8259A_irq(0);
		if (timer_irq_works()) {
			apic_printk(APIC_QUIET, KERN_INFO "....... works.\n");
			timer_through_8259 = 1;
			if (nmi_watchdog == NMI_IO_APIC) {
				disable_8259A_irq(0);
				setup_nmi();
				enable_8259A_irq(0);
			}
			goto out;
		}
		/*
		 * Cleanup, just in case ...
		 */
		disable_8259A_irq(0);
		clear_IO_APIC_pin(apic2, pin2);
		apic_printk(APIC_QUIET, KERN_INFO "....... failed.\n");
	}

	if (nmi_watchdog == NMI_IO_APIC) {
		apic_printk(APIC_QUIET, KERN_WARNING "timer doesn't work "
			    "through the IO-APIC - disabling NMI Watchdog!\n");
		nmi_watchdog = NMI_NONE;
	}
	timer_ack = 0;

	apic_printk(APIC_QUIET, KERN_INFO
		    "...trying to set up timer as Virtual Wire IRQ...\n");

	lapic_register_intr(0, vector);
	apic_write(APIC_LVT0, APIC_DM_FIXED | vector);	/* Fixed mode */
	enable_8259A_irq(0);

	if (timer_irq_works()) {
		apic_printk(APIC_QUIET, KERN_INFO "..... works.\n");
		goto out;
	}
	disable_8259A_irq(0);
	apic_write(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_FIXED | vector);
	apic_printk(APIC_QUIET, KERN_INFO "..... failed.\n");

	apic_printk(APIC_QUIET, KERN_INFO
		    "...trying to set up timer as ExtINT IRQ...\n");

	init_8259A(0);
	make_8259A_irq(0);
	apic_write(APIC_LVT0, APIC_DM_EXTINT);

	unlock_ExtINT_logic();

	if (timer_irq_works()) {
		apic_printk(APIC_QUIET, KERN_INFO "..... works.\n");
		goto out;
	}
	apic_printk(APIC_QUIET, KERN_INFO "..... failed :(.\n");
	panic("IO-APIC + timer doesn't work!  Boot with apic=debug and send a "
		"report.  Then try booting with the 'noapic' option.\n");
out:
	local_irq_restore(flags);
}

/*
 * Traditionally ISA IRQ2 is the cascade IRQ, and is not available
 * to devices.  However there may be an I/O APIC pin available for
 * this interrupt regardless.  The pin may be left unconnected, but
 * typically it will be reused as an ExtINT cascade interrupt for
 * the master 8259A.  In the MPS case such a pin will normally be
 * reported as an ExtINT interrupt in the MP table.  With ACPI
 * there is no provision for ExtINT interrupts, and in the absence
 * of an override it would be treated as an ordinary ISA I/O APIC
 * interrupt, that is edge-triggered and unmasked by default.  We
 * used to do this, but it caused problems on some systems because
 * of the NMI watchdog and sometimes IRQ0 of the 8254 timer using
 * the same ExtINT cascade interrupt to drive the local APIC of the
 * bootstrap processor.  Therefore we refrain from routing IRQ2 to
 * the I/O APIC in all cases now.  No actual device should request
 * it anyway.  --macro
 */
#define PIC_IRQS	(1 << PIC_CASCADE_IR)

void __init setup_IO_APIC(void)
{
	int i;

	/* Reserve all the system vectors. */
	for (i = first_system_vector; i < NR_VECTORS; i++)
		set_bit(i, used_vectors);

	enable_IO_APIC();

	io_apic_irqs = ~PIC_IRQS;

	printk("ENABLING IO-APIC IRQs\n");

	/*
	 * Set up IO-APIC IRQ routing.
	 */
	if (!acpi_ioapic)
		setup_ioapic_ids_from_mpc();
	sync_Arb_IDs();
	setup_IO_APIC_irqs();
	init_IO_APIC_traps();
	check_timer();
}

/*
 *	Called after all the initialization is done. If we didnt find any
 *	APIC bugs then we can allow the modify fast path
 */

static int __init io_apic_bug_finalize(void)
{
	if (sis_apic_bug == -1)
		sis_apic_bug = 0;
	return 0;
}

late_initcall(io_apic_bug_finalize);

struct sysfs_ioapic_data {
	struct sys_device dev;
	struct IO_APIC_route_entry entry[0];
};
static struct sysfs_ioapic_data *mp_ioapic_data[MAX_IO_APICS];

static int ioapic_suspend(struct sys_device *dev, pm_message_t state)
{
	struct IO_APIC_route_entry *entry;
	struct sysfs_ioapic_data *data;
	int i;

	data = container_of(dev, struct sysfs_ioapic_data, dev);
	entry = data->entry;
	for (i = 0; i < nr_ioapic_registers[dev->id]; i++)
		entry[i] = ioapic_read_entry(dev->id, i);

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
	if (reg_00.bits.ID != mp_ioapics[dev->id].mp_apicid) {
		reg_00.bits.ID = mp_ioapics[dev->id].mp_apicid;
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
	struct sys_device *dev;
	int i, size, error = 0;

	error = sysdev_class_register(&ioapic_sysdev_class);
	if (error)
		return error;

	for (i = 0; i < nr_ioapics; i++) {
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
	int irq, new, vector = 0;
	unsigned long flags;
	struct irq_cfg *cfg_new;

	irq = -ENOSPC;
	spin_lock_irqsave(&vector_lock, flags);
	for (new = (nr_irqs - 1); new >= 0; new--) {
		if (platform_legacy_irq(new))
			continue;
		cfg_new = irq_cfg(new);
		if (cfg_new && cfg_new->vector != 0)
			continue;
		if (!cfg_new)
			cfg_new = irq_cfg_alloc(new);
		vector = __assign_irq_vector(new);
		if (likely(vector > 0))
			irq = new;
		break;
	}
	spin_unlock_irqrestore(&vector_lock, flags);

	if (irq >= 0) {
		set_intr_gate(vector, interrupt[irq]);
		dynamic_irq_init(irq);
	}
	return irq;
}

void destroy_irq(unsigned int irq)
{
	unsigned long flags;

	dynamic_irq_cleanup(irq);

	spin_lock_irqsave(&vector_lock, flags);
	clear_bit(irq_cfg(irq)->vector, used_vectors);
	irq_cfg(irq)->vector = 0;
	spin_unlock_irqrestore(&vector_lock, flags);
}

/*
 * MSI message composition
 */
#ifdef CONFIG_PCI_MSI
static int msi_compose_msg(struct pci_dev *pdev, unsigned int irq, struct msi_msg *msg)
{
	int vector;
	unsigned dest;

	vector = assign_irq_vector(irq);
	if (vector >= 0) {
		dest = cpu_mask_to_apicid(TARGET_CPUS);

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
			MSI_DATA_VECTOR(vector);
	}
	return vector;
}

#ifdef CONFIG_SMP
static void set_msi_irq_affinity(unsigned int irq, cpumask_t mask)
{
	struct msi_msg msg;
	unsigned int dest;
	cpumask_t tmp;
	int vector;
	struct irq_desc *desc;

	cpus_and(tmp, mask, cpu_online_map);
	if (cpus_empty(tmp))
		tmp = TARGET_CPUS;

	vector = assign_irq_vector(irq);
	if (vector < 0)
		return;

	dest = cpu_mask_to_apicid(mask);

	read_msi_msg(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	write_msi_msg(irq, &msg);
	desc = irq_to_desc(irq);
	desc->affinity = mask;
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
	.ack		= ack_ioapic_irq,
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

	set_irq_chip_and_handler_name(irq, &msi_chip, handle_edge_irq,
				      "edge");

	return 0;
}

void arch_teardown_msi_irq(unsigned int irq)
{
	destroy_irq(irq);
}

#endif /* CONFIG_PCI_MSI */

/*
 * Hypertransport interrupt support
 */
#ifdef CONFIG_HT_IRQ

#ifdef CONFIG_SMP

static void target_ht_irq(unsigned int irq, unsigned int dest)
{
	struct ht_irq_msg msg;
	fetch_ht_irq_msg(irq, &msg);

	msg.address_lo &= ~(HT_IRQ_LOW_DEST_ID_MASK);
	msg.address_hi &= ~(HT_IRQ_HIGH_DEST_ID_MASK);

	msg.address_lo |= HT_IRQ_LOW_DEST_ID(dest);
	msg.address_hi |= HT_IRQ_HIGH_DEST_ID(dest);

	write_ht_irq_msg(irq, &msg);
}

static void set_ht_irq_affinity(unsigned int irq, cpumask_t mask)
{
	unsigned int dest;
	cpumask_t tmp;
	struct irq_desc *desc;

	cpus_and(tmp, mask, cpu_online_map);
	if (cpus_empty(tmp))
		tmp = TARGET_CPUS;

	cpus_and(mask, tmp, CPU_MASK_ALL);

	dest = cpu_mask_to_apicid(mask);

	target_ht_irq(irq, dest);
	desc = irq_to_desc(irq);
	desc->affinity = mask;
}
#endif

static struct irq_chip ht_irq_chip = {
	.name		= "PCI-HT",
	.mask		= mask_ht_irq,
	.unmask		= unmask_ht_irq,
	.ack		= ack_ioapic_irq,
#ifdef CONFIG_SMP
	.set_affinity	= set_ht_irq_affinity,
#endif
	.retrigger	= ioapic_retrigger_irq,
};

int arch_setup_ht_irq(unsigned int irq, struct pci_dev *dev)
{
	int vector;

	vector = assign_irq_vector(irq);
	if (vector >= 0) {
		struct ht_irq_msg msg;
		unsigned dest;
		cpumask_t tmp;

		cpus_clear(tmp);
		cpu_set(vector >> 8, tmp);
		dest = cpu_mask_to_apicid(tmp);

		msg.address_hi = HT_IRQ_HIGH_DEST_ID(dest);

		msg.address_lo =
			HT_IRQ_LOW_BASE |
			HT_IRQ_LOW_DEST_ID(dest) |
			HT_IRQ_LOW_VECTOR(vector) |
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
	return vector;
}
#endif /* CONFIG_HT_IRQ */

/* --------------------------------------------------------------------------
			ACPI-based IOAPIC Configuration
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI

int __init io_apic_get_unique_id(int ioapic, int apic_id)
{
	union IO_APIC_reg_00 reg_00;
	static physid_mask_t apic_id_map = PHYSID_MASK_NONE;
	physid_mask_t tmp;
	unsigned long flags;
	int i = 0;

	/*
	 * The P4 platform supports up to 256 APIC IDs on two separate APIC
	 * buses (one for LAPICs, one for IOAPICs), where predecessors only
	 * supports up to 16 on one shared APIC bus.
	 *
	 * TBD: Expand LAPIC/IOAPIC support on P4-class systems to take full
	 *      advantage of new APIC bus architecture.
	 */

	if (physids_empty(apic_id_map))
		apic_id_map = ioapic_phys_id_map(phys_cpu_present_map);

	spin_lock_irqsave(&ioapic_lock, flags);
	reg_00.raw = io_apic_read(ioapic, 0);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	if (apic_id >= get_physical_broadcast()) {
		printk(KERN_WARNING "IOAPIC[%d]: Invalid apic_id %d, trying "
			"%d\n", ioapic, apic_id, reg_00.bits.ID);
		apic_id = reg_00.bits.ID;
	}

	/*
	 * Every APIC in a system must have a unique ID or we get lots of nice
	 * 'stuck on smp_invalidate_needed IPI wait' messages.
	 */
	if (check_apicid_used(apic_id_map, apic_id)) {

		for (i = 0; i < get_physical_broadcast(); i++) {
			if (!check_apicid_used(apic_id_map, i))
				break;
		}

		if (i == get_physical_broadcast())
			panic("Max apic_id exceeded!\n");

		printk(KERN_WARNING "IOAPIC[%d]: apic_id %d already used, "
			"trying %d\n", ioapic, apic_id, i);

		apic_id = i;
	}

	tmp = apicid_to_cpu_present(apic_id);
	physids_or(apic_id_map, apic_id_map, tmp);

	if (reg_00.bits.ID != apic_id) {
		reg_00.bits.ID = apic_id;

		spin_lock_irqsave(&ioapic_lock, flags);
		io_apic_write(ioapic, 0, reg_00.raw);
		reg_00.raw = io_apic_read(ioapic, 0);
		spin_unlock_irqrestore(&ioapic_lock, flags);

		/* Sanity check */
		if (reg_00.bits.ID != apic_id) {
			printk("IOAPIC[%d]: Unable to change apic_id!\n", ioapic);
			return -1;
		}
	}

	apic_printk(APIC_VERBOSE, KERN_INFO
			"IOAPIC[%d]: Assigned apic_id %d\n", ioapic, apic_id);

	return apic_id;
}


int __init io_apic_get_version(int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return reg_01.bits.version;
}


int __init io_apic_get_redir_entries(int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return reg_01.bits.entries;
}


int io_apic_set_pci_routing(int ioapic, int pin, int irq, int edge_level, int active_high_low)
{
	struct IO_APIC_route_entry entry;

	if (!IO_APIC_IRQ(irq)) {
		printk(KERN_ERR "IOAPIC[%d]: Invalid reference to IRQ 0\n",
			ioapic);
		return -EINVAL;
	}

	/*
	 * Generate a PCI IRQ routing entry and program the IOAPIC accordingly.
	 * Note that we mask (disable) IRQs now -- these get enabled when the
	 * corresponding device driver registers for this IRQ.
	 */

	memset(&entry, 0, sizeof(entry));

	entry.delivery_mode = INT_DELIVERY_MODE;
	entry.dest_mode = INT_DEST_MODE;
	entry.dest.logical.logical_dest = cpu_mask_to_apicid(TARGET_CPUS);
	entry.trigger = edge_level;
	entry.polarity = active_high_low;
	entry.mask  = 1;

	/*
	 * IRQs < 16 are already in the irq_2_pin[] map
	 */
	if (irq >= 16)
		add_pin_to_irq(irq, ioapic, pin);

	entry.vector = assign_irq_vector(irq);

	apic_printk(APIC_DEBUG, KERN_DEBUG "IOAPIC[%d]: Set PCI routing entry "
		"(%d-%d -> 0x%x -> IRQ %d Mode:%i Active:%i)\n", ioapic,
		mp_ioapics[ioapic].mp_apicid, pin, entry.vector, irq,
		edge_level, active_high_low);

	ioapic_register_intr(irq, entry.vector, edge_level);

	if (!ioapic && (irq < 16))
		disable_8259A_irq(irq);

	ioapic_write_entry(ioapic, pin, entry);

	return 0;
}

int acpi_get_override_irq(int bus_irq, int *trigger, int *polarity)
{
	int i;

	if (skip_ioapic_setup)
		return -1;

	for (i = 0; i < mp_irq_entries; i++)
		if (mp_irqs[i].mp_irqtype == mp_INT &&
		    mp_irqs[i].mp_srcbusirq == bus_irq)
			break;
	if (i >= mp_irq_entries)
		return -1;

	*trigger = irq_trigger(i);
	*polarity = irq_polarity(i);
	return 0;
}

#endif /* CONFIG_ACPI */

static int __init parse_disable_timer_pin_1(char *arg)
{
	disable_timer_pin_1 = 1;
	return 0;
}
early_param("disable_timer_pin_1", parse_disable_timer_pin_1);

static int __init parse_enable_timer_pin_1(char *arg)
{
	disable_timer_pin_1 = -1;
	return 0;
}
early_param("enable_timer_pin_1", parse_enable_timer_pin_1);

static int __init parse_noapic(char *arg)
{
	/* disable IO-APIC */
	disable_ioapic_setup();
	return 0;
}
early_param("noapic", parse_noapic);

void __init ioapic_init_mappings(void)
{
	unsigned long ioapic_phys, idx = FIX_IO_APIC_BASE_0;
	int i;

	for (i = 0; i < nr_ioapics; i++) {
		if (smp_found_config) {
			ioapic_phys = mp_ioapics[i].mp_apicaddr;
			if (!ioapic_phys) {
				printk(KERN_ERR
				       "WARNING: bogus zero IO-APIC "
				       "address found in MPTABLE, "
				       "disabling IO/APIC support!\n");
				smp_found_config = 0;
				skip_ioapic_setup = 1;
				goto fake_ioapic_page;
			}
		} else {
fake_ioapic_page:
			ioapic_phys = (unsigned long)
				      alloc_bootmem_pages(PAGE_SIZE);
			ioapic_phys = __pa(ioapic_phys);
		}
		set_fixmap_nocache(idx, ioapic_phys);
		printk(KERN_DEBUG "mapped IOAPIC to %08lx (%08lx)\n",
		       __fix_to_virt(idx), ioapic_phys);
		idx++;
	}
}

