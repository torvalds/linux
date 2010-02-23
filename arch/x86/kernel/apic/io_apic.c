/*
 *	Intel IO-APIC support for multi-Pentium hosts.
 *
 *	Copyright (C) 1997, 1998, 1999, 2000, 2009 Ingo Molnar, Hajnalka Szabo
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
#include <linux/compiler.h>
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/msi.h>
#include <linux/htirq.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>	/* time_after() */
#ifdef CONFIG_ACPI
#include <acpi/acpi_bus.h>
#endif
#include <linux/bootmem.h>
#include <linux/dmar.h>
#include <linux/hpet.h>

#include <asm/idle.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/cpu.h>
#include <asm/desc.h>
#include <asm/proto.h>
#include <asm/acpi.h>
#include <asm/dma.h>
#include <asm/timer.h>
#include <asm/i8259.h>
#include <asm/nmi.h>
#include <asm/msidef.h>
#include <asm/hypertransport.h>
#include <asm/setup.h>
#include <asm/irq_remapping.h>
#include <asm/hpet.h>
#include <asm/hw_irq.h>

#include <asm/apic.h>

#define __apicdebuginit(type) static type __init
#define for_each_irq_pin(entry, head) \
	for (entry = head; entry; entry = entry->next)

/*
 *      Is the SiS APIC rmw bug present ?
 *      -1 = don't know, 0 = no, 1 = yes
 */
int sis_apic_bug = -1;

static DEFINE_RAW_SPINLOCK(ioapic_lock);
static DEFINE_RAW_SPINLOCK(vector_lock);

/*
 * # of IRQ routing registers
 */
int nr_ioapic_registers[MAX_IO_APICS];

/* I/O APIC entries */
struct mpc_ioapic mp_ioapics[MAX_IO_APICS];
int nr_ioapics;

/* IO APIC gsi routing info */
struct mp_ioapic_gsi  mp_gsi_routing[MAX_IO_APICS];

/* MP IRQ source entries */
struct mpc_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* # of MP IRQ source entries */
int mp_irq_entries;

/* GSI interrupts */
static int nr_irqs_gsi = NR_IRQS_LEGACY;

#if defined (CONFIG_MCA) || defined (CONFIG_EISA)
int mp_bus_id_to_type[MAX_MP_BUSSES];
#endif

DECLARE_BITMAP(mp_bus_not_pci, MAX_MP_BUSSES);

int skip_ioapic_setup;

void arch_disable_smp_support(void)
{
#ifdef CONFIG_PCI
	noioapicquirk = 1;
	noioapicreroute = -1;
#endif
	skip_ioapic_setup = 1;
}

static int __init parse_noapic(char *str)
{
	/* disable IO-APIC */
	arch_disable_smp_support();
	return 0;
}
early_param("noapic", parse_noapic);

struct irq_pin_list {
	int apic, pin;
	struct irq_pin_list *next;
};

static struct irq_pin_list *get_one_free_irq_2_pin(int node)
{
	struct irq_pin_list *pin;

	pin = kzalloc_node(sizeof(*pin), GFP_ATOMIC, node);

	return pin;
}

/* irq_cfg is indexed by the sum of all RTEs in all I/O APICs. */
#ifdef CONFIG_SPARSE_IRQ
static struct irq_cfg irq_cfgx[NR_IRQS_LEGACY];
#else
static struct irq_cfg irq_cfgx[NR_IRQS];
#endif

void __init io_apic_disable_legacy(void)
{
	nr_legacy_irqs = 0;
	nr_irqs_gsi = 0;
}

int __init arch_early_irq_init(void)
{
	struct irq_cfg *cfg;
	struct irq_desc *desc;
	int count;
	int node;
	int i;

	cfg = irq_cfgx;
	count = ARRAY_SIZE(irq_cfgx);
	node= cpu_to_node(boot_cpu_id);

	for (i = 0; i < count; i++) {
		desc = irq_to_desc(i);
		desc->chip_data = &cfg[i];
		zalloc_cpumask_var_node(&cfg[i].domain, GFP_NOWAIT, node);
		zalloc_cpumask_var_node(&cfg[i].old_domain, GFP_NOWAIT, node);
		/*
		 * For legacy IRQ's, start with assigning irq0 to irq15 to
		 * IRQ0_VECTOR to IRQ15_VECTOR on cpu 0.
		 */
		if (i < nr_legacy_irqs) {
			cfg[i].vector = IRQ0_VECTOR + i;
			cpumask_set_cpu(0, cfg[i].domain);
		}
	}

	return 0;
}

#ifdef CONFIG_SPARSE_IRQ
struct irq_cfg *irq_cfg(unsigned int irq)
{
	struct irq_cfg *cfg = NULL;
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	if (desc)
		cfg = desc->chip_data;

	return cfg;
}

static struct irq_cfg *get_one_free_irq_cfg(int node)
{
	struct irq_cfg *cfg;

	cfg = kzalloc_node(sizeof(*cfg), GFP_ATOMIC, node);
	if (cfg) {
		if (!zalloc_cpumask_var_node(&cfg->domain, GFP_ATOMIC, node)) {
			kfree(cfg);
			cfg = NULL;
		} else if (!zalloc_cpumask_var_node(&cfg->old_domain,
							  GFP_ATOMIC, node)) {
			free_cpumask_var(cfg->domain);
			kfree(cfg);
			cfg = NULL;
		}
	}

	return cfg;
}

int arch_init_chip_data(struct irq_desc *desc, int node)
{
	struct irq_cfg *cfg;

	cfg = desc->chip_data;
	if (!cfg) {
		desc->chip_data = get_one_free_irq_cfg(node);
		if (!desc->chip_data) {
			printk(KERN_ERR "can not alloc irq_cfg\n");
			BUG_ON(1);
		}
	}

	return 0;
}

/* for move_irq_desc */
static void
init_copy_irq_2_pin(struct irq_cfg *old_cfg, struct irq_cfg *cfg, int node)
{
	struct irq_pin_list *old_entry, *head, *tail, *entry;

	cfg->irq_2_pin = NULL;
	old_entry = old_cfg->irq_2_pin;
	if (!old_entry)
		return;

	entry = get_one_free_irq_2_pin(node);
	if (!entry)
		return;

	entry->apic	= old_entry->apic;
	entry->pin	= old_entry->pin;
	head		= entry;
	tail		= entry;
	old_entry	= old_entry->next;
	while (old_entry) {
		entry = get_one_free_irq_2_pin(node);
		if (!entry) {
			entry = head;
			while (entry) {
				head = entry->next;
				kfree(entry);
				entry = head;
			}
			/* still use the old one */
			return;
		}
		entry->apic	= old_entry->apic;
		entry->pin	= old_entry->pin;
		tail->next	= entry;
		tail		= entry;
		old_entry	= old_entry->next;
	}

	tail->next = NULL;
	cfg->irq_2_pin = head;
}

static void free_irq_2_pin(struct irq_cfg *old_cfg, struct irq_cfg *cfg)
{
	struct irq_pin_list *entry, *next;

	if (old_cfg->irq_2_pin == cfg->irq_2_pin)
		return;

	entry = old_cfg->irq_2_pin;

	while (entry) {
		next = entry->next;
		kfree(entry);
		entry = next;
	}
	old_cfg->irq_2_pin = NULL;
}

void arch_init_copy_chip_data(struct irq_desc *old_desc,
				 struct irq_desc *desc, int node)
{
	struct irq_cfg *cfg;
	struct irq_cfg *old_cfg;

	cfg = get_one_free_irq_cfg(node);

	if (!cfg)
		return;

	desc->chip_data = cfg;

	old_cfg = old_desc->chip_data;

	memcpy(cfg, old_cfg, sizeof(struct irq_cfg));

	init_copy_irq_2_pin(old_cfg, cfg, node);
}

static void free_irq_cfg(struct irq_cfg *old_cfg)
{
	kfree(old_cfg);
}

void arch_free_chip_data(struct irq_desc *old_desc, struct irq_desc *desc)
{
	struct irq_cfg *old_cfg, *cfg;

	old_cfg = old_desc->chip_data;
	cfg = desc->chip_data;

	if (old_cfg == cfg)
		return;

	if (old_cfg) {
		free_irq_2_pin(old_cfg, cfg);
		free_irq_cfg(old_cfg);
		old_desc->chip_data = NULL;
	}
}
/* end for move_irq_desc */

#else
struct irq_cfg *irq_cfg(unsigned int irq)
{
	return irq < nr_irqs ? irq_cfgx + irq : NULL;
}

#endif

struct io_apic {
	unsigned int index;
	unsigned int unused[3];
	unsigned int data;
	unsigned int unused2[11];
	unsigned int eoi;
};

static __attribute_const__ struct io_apic __iomem *io_apic_base(int idx)
{
	return (void __iomem *) __fix_to_virt(FIX_IO_APIC_BASE_0 + idx)
		+ (mp_ioapics[idx].apicaddr & ~PAGE_MASK);
}

static inline void io_apic_eoi(unsigned int apic, unsigned int vector)
{
	struct io_apic __iomem *io_apic = io_apic_base(apic);
	writel(vector, &io_apic->eoi);
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
	struct io_apic __iomem *io_apic = io_apic_base(apic);

	if (sis_apic_bug)
		writel(reg, &io_apic->index);
	writel(value, &io_apic->data);
}

static bool io_apic_level_ack_pending(struct irq_cfg *cfg)
{
	struct irq_pin_list *entry;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	for_each_irq_pin(entry, cfg->irq_2_pin) {
		unsigned int reg;
		int pin;

		pin = entry->pin;
		reg = io_apic_read(entry->apic, 0x10 + pin*2);
		/* Is the remote IRR bit set? */
		if (reg & IO_APIC_REDIR_REMOTE_IRR) {
			raw_spin_unlock_irqrestore(&ioapic_lock, flags);
			return true;
		}
	}
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return false;
}

union entry_union {
	struct { u32 w1, w2; };
	struct IO_APIC_route_entry entry;
};

static struct IO_APIC_route_entry ioapic_read_entry(int apic, int pin)
{
	union entry_union eu;
	unsigned long flags;
	raw_spin_lock_irqsave(&ioapic_lock, flags);
	eu.w1 = io_apic_read(apic, 0x10 + 2 * pin);
	eu.w2 = io_apic_read(apic, 0x11 + 2 * pin);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
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
	union entry_union eu = {{0, 0}};

	eu.entry = e;
	io_apic_write(apic, 0x11 + 2*pin, eu.w2);
	io_apic_write(apic, 0x10 + 2*pin, eu.w1);
}

void ioapic_write_entry(int apic, int pin, struct IO_APIC_route_entry e)
{
	unsigned long flags;
	raw_spin_lock_irqsave(&ioapic_lock, flags);
	__ioapic_write_entry(apic, pin, e);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
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

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_write(apic, 0x10 + 2*pin, eu.w1);
	io_apic_write(apic, 0x11 + 2*pin, eu.w2);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
}

/*
 * The common case is 1:1 IRQ<->pin mappings. Sometimes there are
 * shared ISA-space IRQs, so we have to support them. We are super
 * fast in the common case, and fast for shared ISA-space IRQs.
 */
static int
add_pin_to_irq_node_nopanic(struct irq_cfg *cfg, int node, int apic, int pin)
{
	struct irq_pin_list **last, *entry;

	/* don't allow duplicates */
	last = &cfg->irq_2_pin;
	for_each_irq_pin(entry, cfg->irq_2_pin) {
		if (entry->apic == apic && entry->pin == pin)
			return 0;
		last = &entry->next;
	}

	entry = get_one_free_irq_2_pin(node);
	if (!entry) {
		printk(KERN_ERR "can not alloc irq_pin_list (%d,%d,%d)\n",
				node, apic, pin);
		return -ENOMEM;
	}
	entry->apic = apic;
	entry->pin = pin;

	*last = entry;
	return 0;
}

static void add_pin_to_irq_node(struct irq_cfg *cfg, int node, int apic, int pin)
{
	if (add_pin_to_irq_node_nopanic(cfg, node, apic, pin))
		panic("IO-APIC: failed to add irq-pin. Can not proceed\n");
}

/*
 * Reroute an IRQ to a different pin.
 */
static void __init replace_pin_at_irq_node(struct irq_cfg *cfg, int node,
					   int oldapic, int oldpin,
					   int newapic, int newpin)
{
	struct irq_pin_list *entry;

	for_each_irq_pin(entry, cfg->irq_2_pin) {
		if (entry->apic == oldapic && entry->pin == oldpin) {
			entry->apic = newapic;
			entry->pin = newpin;
			/* every one is different, right? */
			return;
		}
	}

	/* old apic/pin didn't exist, so just add new ones */
	add_pin_to_irq_node(cfg, node, newapic, newpin);
}

static void __io_apic_modify_irq(struct irq_pin_list *entry,
				 int mask_and, int mask_or,
				 void (*final)(struct irq_pin_list *entry))
{
	unsigned int reg, pin;

	pin = entry->pin;
	reg = io_apic_read(entry->apic, 0x10 + pin * 2);
	reg &= mask_and;
	reg |= mask_or;
	io_apic_modify(entry->apic, 0x10 + pin * 2, reg);
	if (final)
		final(entry);
}

static void io_apic_modify_irq(struct irq_cfg *cfg,
			       int mask_and, int mask_or,
			       void (*final)(struct irq_pin_list *entry))
{
	struct irq_pin_list *entry;

	for_each_irq_pin(entry, cfg->irq_2_pin)
		__io_apic_modify_irq(entry, mask_and, mask_or, final);
}

static void __mask_and_edge_IO_APIC_irq(struct irq_pin_list *entry)
{
	__io_apic_modify_irq(entry, ~IO_APIC_REDIR_LEVEL_TRIGGER,
			     IO_APIC_REDIR_MASKED, NULL);
}

static void __unmask_and_level_IO_APIC_irq(struct irq_pin_list *entry)
{
	__io_apic_modify_irq(entry, ~IO_APIC_REDIR_MASKED,
			     IO_APIC_REDIR_LEVEL_TRIGGER, NULL);
}

static void __unmask_IO_APIC_irq(struct irq_cfg *cfg)
{
	io_apic_modify_irq(cfg, ~IO_APIC_REDIR_MASKED, 0, NULL);
}

static void io_apic_sync(struct irq_pin_list *entry)
{
	/*
	 * Synchronize the IO-APIC and the CPU by doing
	 * a dummy read from the IO-APIC
	 */
	struct io_apic __iomem *io_apic;
	io_apic = io_apic_base(entry->apic);
	readl(&io_apic->data);
}

static void __mask_IO_APIC_irq(struct irq_cfg *cfg)
{
	io_apic_modify_irq(cfg, ~0, IO_APIC_REDIR_MASKED, &io_apic_sync);
}

static void mask_IO_APIC_irq_desc(struct irq_desc *desc)
{
	struct irq_cfg *cfg = desc->chip_data;
	unsigned long flags;

	BUG_ON(!cfg);

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	__mask_IO_APIC_irq(cfg);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void unmask_IO_APIC_irq_desc(struct irq_desc *desc)
{
	struct irq_cfg *cfg = desc->chip_data;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	__unmask_IO_APIC_irq(cfg);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void mask_IO_APIC_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	mask_IO_APIC_irq_desc(desc);
}
static void unmask_IO_APIC_irq(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	unmask_IO_APIC_irq_desc(desc);
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

#ifdef CONFIG_X86_32
/*
 * support for broken MP BIOSs, enables hand-redirection of PIRQ0-7 to
 * specific CPU-side IRQs.
 */

#define MAX_PIRQS 8
static int pirq_entries[MAX_PIRQS] = {
	[0 ... MAX_PIRQS - 1] = -1
};

static int __init ioapic_pirq_setup(char *str)
{
	int i, max;
	int ints[MAX_PIRQS+1];

	get_options(str, ARRAY_SIZE(ints), ints);

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
#endif /* CONFIG_X86_32 */

struct IO_APIC_route_entry **alloc_ioapic_entries(void)
{
	int apic;
	struct IO_APIC_route_entry **ioapic_entries;

	ioapic_entries = kzalloc(sizeof(*ioapic_entries) * nr_ioapics,
				GFP_ATOMIC);
	if (!ioapic_entries)
		return 0;

	for (apic = 0; apic < nr_ioapics; apic++) {
		ioapic_entries[apic] =
			kzalloc(sizeof(struct IO_APIC_route_entry) *
				nr_ioapic_registers[apic], GFP_ATOMIC);
		if (!ioapic_entries[apic])
			goto nomem;
	}

	return ioapic_entries;

nomem:
	while (--apic >= 0)
		kfree(ioapic_entries[apic]);
	kfree(ioapic_entries);

	return 0;
}

/*
 * Saves all the IO-APIC RTE's
 */
int save_IO_APIC_setup(struct IO_APIC_route_entry **ioapic_entries)
{
	int apic, pin;

	if (!ioapic_entries)
		return -ENOMEM;

	for (apic = 0; apic < nr_ioapics; apic++) {
		if (!ioapic_entries[apic])
			return -ENOMEM;

		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++)
			ioapic_entries[apic][pin] =
				ioapic_read_entry(apic, pin);
	}

	return 0;
}

/*
 * Mask all IO APIC entries.
 */
void mask_IO_APIC_setup(struct IO_APIC_route_entry **ioapic_entries)
{
	int apic, pin;

	if (!ioapic_entries)
		return;

	for (apic = 0; apic < nr_ioapics; apic++) {
		if (!ioapic_entries[apic])
			break;

		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++) {
			struct IO_APIC_route_entry entry;

			entry = ioapic_entries[apic][pin];
			if (!entry.mask) {
				entry.mask = 1;
				ioapic_write_entry(apic, pin, entry);
			}
		}
	}
}

/*
 * Restore IO APIC entries which was saved in ioapic_entries.
 */
int restore_IO_APIC_setup(struct IO_APIC_route_entry **ioapic_entries)
{
	int apic, pin;

	if (!ioapic_entries)
		return -ENOMEM;

	for (apic = 0; apic < nr_ioapics; apic++) {
		if (!ioapic_entries[apic])
			return -ENOMEM;

		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++)
			ioapic_write_entry(apic, pin,
					ioapic_entries[apic][pin]);
	}
	return 0;
}

void free_ioapic_entries(struct IO_APIC_route_entry **ioapic_entries)
{
	int apic;

	for (apic = 0; apic < nr_ioapics; apic++)
		kfree(ioapic_entries[apic]);

	kfree(ioapic_entries);
}

/*
 * Find the IRQ entry number of a certain pin.
 */
static int find_irq_entry(int apic, int pin, int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++)
		if (mp_irqs[i].irqtype == type &&
		    (mp_irqs[i].dstapic == mp_ioapics[apic].apicid ||
		     mp_irqs[i].dstapic == MP_APIC_ALL) &&
		    mp_irqs[i].dstirq == pin)
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
		int lbus = mp_irqs[i].srcbus;

		if (test_bit(lbus, mp_bus_not_pci) &&
		    (mp_irqs[i].irqtype == type) &&
		    (mp_irqs[i].srcbusirq == irq))

			return mp_irqs[i].dstirq;
	}
	return -1;
}

static int __init find_isa_irq_apic(int irq, int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++) {
		int lbus = mp_irqs[i].srcbus;

		if (test_bit(lbus, mp_bus_not_pci) &&
		    (mp_irqs[i].irqtype == type) &&
		    (mp_irqs[i].srcbusirq == irq))
			break;
	}
	if (i < mp_irq_entries) {
		int apic;
		for(apic = 0; apic < nr_ioapics; apic++) {
			if (mp_ioapics[apic].apicid == mp_irqs[i].dstapic)
				return apic;
		}
	}

	return -1;
}

#if defined(CONFIG_EISA) || defined(CONFIG_MCA)
/*
 * EISA Edge/Level control register, ELCR
 */
static int EISA_ELCR(unsigned int irq)
{
	if (irq < nr_legacy_irqs) {
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

#define default_EISA_trigger(idx)	(EISA_ELCR(mp_irqs[idx].srcbusirq))
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
	int bus = mp_irqs[idx].srcbus;
	int polarity;

	/*
	 * Determine IRQ line polarity (high active or low active):
	 */
	switch (mp_irqs[idx].irqflag & 3)
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
	int bus = mp_irqs[idx].srcbus;
	int trigger;

	/*
	 * Determine IRQ trigger mode (edge or level sensitive):
	 */
	switch ((mp_irqs[idx].irqflag>>2) & 3)
	{
		case 0: /* conforms, ie. bus-type dependent */
			if (test_bit(bus, mp_bus_not_pci))
				trigger = default_ISA_trigger(idx);
			else
				trigger = default_PCI_trigger(idx);
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

int (*ioapic_renumber_irq)(int ioapic, int irq);
static int pin_2_irq(int idx, int apic, int pin)
{
	int irq, i;
	int bus = mp_irqs[idx].srcbus;

	/*
	 * Debugging check, we are in big trouble if this message pops up!
	 */
	if (mp_irqs[idx].dstirq != pin)
		printk(KERN_ERR "broken BIOS or MPTABLE parser, ayiee!!\n");

	if (test_bit(bus, mp_bus_not_pci)) {
		irq = mp_irqs[idx].srcbusirq;
	} else {
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

#ifdef CONFIG_X86_32
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
#endif

	return irq;
}

/*
 * Find a specific PCI IRQ entry.
 * Not an __init, possibly needed by modules
 */
int IO_APIC_get_PCI_irq_vector(int bus, int slot, int pin,
				struct io_apic_irq_attr *irq_attr)
{
	int apic, i, best_guess = -1;

	apic_printk(APIC_DEBUG,
		    "querying PCI -> IRQ mapping bus:%d, slot:%d, pin:%d.\n",
		    bus, slot, pin);
	if (test_bit(bus, mp_bus_not_pci)) {
		apic_printk(APIC_VERBOSE,
			    "PCI BIOS passed nonexistent PCI bus %d!\n", bus);
		return -1;
	}
	for (i = 0; i < mp_irq_entries; i++) {
		int lbus = mp_irqs[i].srcbus;

		for (apic = 0; apic < nr_ioapics; apic++)
			if (mp_ioapics[apic].apicid == mp_irqs[i].dstapic ||
			    mp_irqs[i].dstapic == MP_APIC_ALL)
				break;

		if (!test_bit(lbus, mp_bus_not_pci) &&
		    !mp_irqs[i].irqtype &&
		    (bus == lbus) &&
		    (slot == ((mp_irqs[i].srcbusirq >> 2) & 0x1f))) {
			int irq = pin_2_irq(i, apic, mp_irqs[i].dstirq);

			if (!(apic || IO_APIC_IRQ(irq)))
				continue;

			if (pin == (mp_irqs[i].srcbusirq & 3)) {
				set_io_apic_irq_attr(irq_attr, apic,
						     mp_irqs[i].dstirq,
						     irq_trigger(i),
						     irq_polarity(i));
				return irq;
			}
			/*
			 * Use the first all-but-pin matching entry as a
			 * best-guess fuzzy result for broken mptables.
			 */
			if (best_guess < 0) {
				set_io_apic_irq_attr(irq_attr, apic,
						     mp_irqs[i].dstirq,
						     irq_trigger(i),
						     irq_polarity(i));
				best_guess = irq;
			}
		}
	}
	return best_guess;
}
EXPORT_SYMBOL(IO_APIC_get_PCI_irq_vector);

void lock_vector_lock(void)
{
	/* Used to the online set of cpus does not change
	 * during assign_irq_vector.
	 */
	raw_spin_lock(&vector_lock);
}

void unlock_vector_lock(void)
{
	raw_spin_unlock(&vector_lock);
}

static int
__assign_irq_vector(int irq, struct irq_cfg *cfg, const struct cpumask *mask)
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
	static int current_vector = FIRST_EXTERNAL_VECTOR + VECTOR_OFFSET_START;
	static int current_offset = VECTOR_OFFSET_START % 8;
	unsigned int old_vector;
	int cpu, err;
	cpumask_var_t tmp_mask;

	if (cfg->move_in_progress)
		return -EBUSY;

	if (!alloc_cpumask_var(&tmp_mask, GFP_ATOMIC))
		return -ENOMEM;

	old_vector = cfg->vector;
	if (old_vector) {
		cpumask_and(tmp_mask, mask, cpu_online_mask);
		cpumask_and(tmp_mask, cfg->domain, tmp_mask);
		if (!cpumask_empty(tmp_mask)) {
			free_cpumask_var(tmp_mask);
			return 0;
		}
	}

	/* Only try and allocate irqs on cpus that are present */
	err = -ENOSPC;
	for_each_cpu_and(cpu, mask, cpu_online_mask) {
		int new_cpu;
		int vector, offset;

		apic->vector_allocation_domain(cpu, tmp_mask);

		vector = current_vector;
		offset = current_offset;
next:
		vector += 8;
		if (vector >= first_system_vector) {
			/* If out of vectors on large boxen, must share them. */
			offset = (offset + 1) % 8;
			vector = FIRST_EXTERNAL_VECTOR + offset;
		}
		if (unlikely(current_vector == vector))
			continue;

		if (test_bit(vector, used_vectors))
			goto next;

		for_each_cpu_and(new_cpu, tmp_mask, cpu_online_mask)
			if (per_cpu(vector_irq, new_cpu)[vector] != -1)
				goto next;
		/* Found one! */
		current_vector = vector;
		current_offset = offset;
		if (old_vector) {
			cfg->move_in_progress = 1;
			cpumask_copy(cfg->old_domain, cfg->domain);
		}
		for_each_cpu_and(new_cpu, tmp_mask, cpu_online_mask)
			per_cpu(vector_irq, new_cpu)[vector] = irq;
		cfg->vector = vector;
		cpumask_copy(cfg->domain, tmp_mask);
		err = 0;
		break;
	}
	free_cpumask_var(tmp_mask);
	return err;
}

int assign_irq_vector(int irq, struct irq_cfg *cfg, const struct cpumask *mask)
{
	int err;
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);
	err = __assign_irq_vector(irq, cfg, mask);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	return err;
}

static void __clear_irq_vector(int irq, struct irq_cfg *cfg)
{
	int cpu, vector;

	BUG_ON(!cfg->vector);

	vector = cfg->vector;
	for_each_cpu_and(cpu, cfg->domain, cpu_online_mask)
		per_cpu(vector_irq, cpu)[vector] = -1;

	cfg->vector = 0;
	cpumask_clear(cfg->domain);

	if (likely(!cfg->move_in_progress))
		return;
	for_each_cpu_and(cpu, cfg->old_domain, cpu_online_mask) {
		for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS;
								vector++) {
			if (per_cpu(vector_irq, cpu)[vector] != irq)
				continue;
			per_cpu(vector_irq, cpu)[vector] = -1;
			break;
		}
	}
	cfg->move_in_progress = 0;
}

void __setup_vector_irq(int cpu)
{
	/* Initialize vector_irq on a new cpu */
	int irq, vector;
	struct irq_cfg *cfg;
	struct irq_desc *desc;

	/*
	 * vector_lock will make sure that we don't run into irq vector
	 * assignments that might be happening on another cpu in parallel,
	 * while we setup our initial vector to irq mappings.
	 */
	raw_spin_lock(&vector_lock);
	/* Mark the inuse vectors */
	for_each_irq_desc(irq, desc) {
		cfg = desc->chip_data;
		if (!cpumask_test_cpu(cpu, cfg->domain))
			continue;
		vector = cfg->vector;
		per_cpu(vector_irq, cpu)[vector] = irq;
	}
	/* Mark the free vectors */
	for (vector = 0; vector < NR_VECTORS; ++vector) {
		irq = per_cpu(vector_irq, cpu)[vector];
		if (irq < 0)
			continue;

		cfg = irq_cfg(irq);
		if (!cpumask_test_cpu(cpu, cfg->domain))
			per_cpu(vector_irq, cpu)[vector] = -1;
	}
	raw_spin_unlock(&vector_lock);
}

static struct irq_chip ioapic_chip;
static struct irq_chip ir_ioapic_chip;

#define IOAPIC_AUTO     -1
#define IOAPIC_EDGE     0
#define IOAPIC_LEVEL    1

#ifdef CONFIG_X86_32
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
#else
static inline int IO_APIC_irq_trigger(int irq)
{
	return 1;
}
#endif

static void ioapic_register_intr(int irq, struct irq_desc *desc, unsigned long trigger)
{

	if ((trigger == IOAPIC_AUTO && IO_APIC_irq_trigger(irq)) ||
	    trigger == IOAPIC_LEVEL)
		desc->status |= IRQ_LEVEL;
	else
		desc->status &= ~IRQ_LEVEL;

	if (irq_remapped(irq)) {
		desc->status |= IRQ_MOVE_PCNTXT;
		if (trigger)
			set_irq_chip_and_handler_name(irq, &ir_ioapic_chip,
						      handle_fasteoi_irq,
						     "fasteoi");
		else
			set_irq_chip_and_handler_name(irq, &ir_ioapic_chip,
						      handle_edge_irq, "edge");
		return;
	}

	if ((trigger == IOAPIC_AUTO && IO_APIC_irq_trigger(irq)) ||
	    trigger == IOAPIC_LEVEL)
		set_irq_chip_and_handler_name(irq, &ioapic_chip,
					      handle_fasteoi_irq,
					      "fasteoi");
	else
		set_irq_chip_and_handler_name(irq, &ioapic_chip,
					      handle_edge_irq, "edge");
}

int setup_ioapic_entry(int apic_id, int irq,
		       struct IO_APIC_route_entry *entry,
		       unsigned int destination, int trigger,
		       int polarity, int vector, int pin)
{
	/*
	 * add it to the IO-APIC irq-routing table:
	 */
	memset(entry,0,sizeof(*entry));

	if (intr_remapping_enabled) {
		struct intel_iommu *iommu = map_ioapic_to_ir(apic_id);
		struct irte irte;
		struct IR_IO_APIC_route_entry *ir_entry =
			(struct IR_IO_APIC_route_entry *) entry;
		int index;

		if (!iommu)
			panic("No mapping iommu for ioapic %d\n", apic_id);

		index = alloc_irte(iommu, irq, 1);
		if (index < 0)
			panic("Failed to allocate IRTE for ioapic %d\n", apic_id);

		memset(&irte, 0, sizeof(irte));

		irte.present = 1;
		irte.dst_mode = apic->irq_dest_mode;
		/*
		 * Trigger mode in the IRTE will always be edge, and the
		 * actual level or edge trigger will be setup in the IO-APIC
		 * RTE. This will help simplify level triggered irq migration.
		 * For more details, see the comments above explainig IO-APIC
		 * irq migration in the presence of interrupt-remapping.
		 */
		irte.trigger_mode = 0;
		irte.dlvry_mode = apic->irq_delivery_mode;
		irte.vector = vector;
		irte.dest_id = IRTE_DEST(destination);

		/* Set source-id of interrupt request */
		set_ioapic_sid(&irte, apic_id);

		modify_irte(irq, &irte);

		ir_entry->index2 = (index >> 15) & 0x1;
		ir_entry->zero = 0;
		ir_entry->format = 1;
		ir_entry->index = (index & 0x7fff);
		/*
		 * IO-APIC RTE will be configured with virtual vector.
		 * irq handler will do the explicit EOI to the io-apic.
		 */
		ir_entry->vector = pin;
	} else {
		entry->delivery_mode = apic->irq_delivery_mode;
		entry->dest_mode = apic->irq_dest_mode;
		entry->dest = destination;
		entry->vector = vector;
	}

	entry->mask = 0;				/* enable IRQ */
	entry->trigger = trigger;
	entry->polarity = polarity;

	/* Mask level triggered irqs.
	 * Use IRQ_DELAYED_DISABLE for edge triggered irqs.
	 */
	if (trigger)
		entry->mask = 1;
	return 0;
}

static void setup_IO_APIC_irq(int apic_id, int pin, unsigned int irq, struct irq_desc *desc,
			      int trigger, int polarity)
{
	struct irq_cfg *cfg;
	struct IO_APIC_route_entry entry;
	unsigned int dest;

	if (!IO_APIC_IRQ(irq))
		return;

	cfg = desc->chip_data;

	/*
	 * For legacy irqs, cfg->domain starts with cpu 0 for legacy
	 * controllers like 8259. Now that IO-APIC can handle this irq, update
	 * the cfg->domain.
	 */
	if (irq < nr_legacy_irqs && cpumask_test_cpu(0, cfg->domain))
		apic->vector_allocation_domain(0, cfg->domain);

	if (assign_irq_vector(irq, cfg, apic->target_cpus()))
		return;

	dest = apic->cpu_mask_to_apicid_and(cfg->domain, apic->target_cpus());

	apic_printk(APIC_VERBOSE,KERN_DEBUG
		    "IOAPIC[%d]: Set routing entry (%d-%d -> 0x%x -> "
		    "IRQ %d Mode:%i Active:%i)\n",
		    apic_id, mp_ioapics[apic_id].apicid, pin, cfg->vector,
		    irq, trigger, polarity);


	if (setup_ioapic_entry(mp_ioapics[apic_id].apicid, irq, &entry,
			       dest, trigger, polarity, cfg->vector, pin)) {
		printk("Failed to setup ioapic entry for ioapic  %d, pin %d\n",
		       mp_ioapics[apic_id].apicid, pin);
		__clear_irq_vector(irq, cfg);
		return;
	}

	ioapic_register_intr(irq, desc, trigger);
	if (irq < nr_legacy_irqs)
		disable_8259A_irq(irq);

	ioapic_write_entry(apic_id, pin, entry);
}

static struct {
	DECLARE_BITMAP(pin_programmed, MP_MAX_IOAPIC_PIN + 1);
} mp_ioapic_routing[MAX_IO_APICS];

static void __init setup_IO_APIC_irqs(void)
{
	int apic_id = 0, pin, idx, irq;
	int notcon = 0;
	struct irq_desc *desc;
	struct irq_cfg *cfg;
	int node = cpu_to_node(boot_cpu_id);

	apic_printk(APIC_VERBOSE, KERN_DEBUG "init IO_APIC IRQs\n");

#ifdef CONFIG_ACPI
	if (!acpi_disabled && acpi_ioapic) {
		apic_id = mp_find_ioapic(0);
		if (apic_id < 0)
			apic_id = 0;
	}
#endif

	for (pin = 0; pin < nr_ioapic_registers[apic_id]; pin++) {
		idx = find_irq_entry(apic_id, pin, mp_INT);
		if (idx == -1) {
			if (!notcon) {
				notcon = 1;
				apic_printk(APIC_VERBOSE,
					KERN_DEBUG " %d-%d",
					mp_ioapics[apic_id].apicid, pin);
			} else
				apic_printk(APIC_VERBOSE, " %d-%d",
					mp_ioapics[apic_id].apicid, pin);
			continue;
		}
		if (notcon) {
			apic_printk(APIC_VERBOSE,
				" (apicid-pin) not connected\n");
			notcon = 0;
		}

		irq = pin_2_irq(idx, apic_id, pin);

		/*
		 * Skip the timer IRQ if there's a quirk handler
		 * installed and if it returns 1:
		 */
		if (apic->multi_timer_check &&
				apic->multi_timer_check(apic_id, irq))
			continue;

		desc = irq_to_desc_alloc_node(irq, node);
		if (!desc) {
			printk(KERN_INFO "can not get irq_desc for %d\n", irq);
			continue;
		}
		cfg = desc->chip_data;
		add_pin_to_irq_node(cfg, node, apic_id, pin);
		/*
		 * don't mark it in pin_programmed, so later acpi could
		 * set it correctly when irq < 16
		 */
		setup_IO_APIC_irq(apic_id, pin, irq, desc,
				irq_trigger(idx), irq_polarity(idx));
	}

	if (notcon)
		apic_printk(APIC_VERBOSE,
			" (apicid-pin) not connected\n");
}

/*
 * for the gsit that is not in first ioapic
 * but could not use acpi_register_gsi()
 * like some special sci in IBM x3330
 */
void setup_IO_APIC_irq_extra(u32 gsi)
{
	int apic_id = 0, pin, idx, irq;
	int node = cpu_to_node(boot_cpu_id);
	struct irq_desc *desc;
	struct irq_cfg *cfg;

	/*
	 * Convert 'gsi' to 'ioapic.pin'.
	 */
	apic_id = mp_find_ioapic(gsi);
	if (apic_id < 0)
		return;

	pin = mp_find_ioapic_pin(apic_id, gsi);
	idx = find_irq_entry(apic_id, pin, mp_INT);
	if (idx == -1)
		return;

	irq = pin_2_irq(idx, apic_id, pin);
#ifdef CONFIG_SPARSE_IRQ
	desc = irq_to_desc(irq);
	if (desc)
		return;
#endif
	desc = irq_to_desc_alloc_node(irq, node);
	if (!desc) {
		printk(KERN_INFO "can not get irq_desc for %d\n", irq);
		return;
	}

	cfg = desc->chip_data;
	add_pin_to_irq_node(cfg, node, apic_id, pin);

	if (test_bit(pin, mp_ioapic_routing[apic_id].pin_programmed)) {
		pr_debug("Pin %d-%d already programmed\n",
			 mp_ioapics[apic_id].apicid, pin);
		return;
	}
	set_bit(pin, mp_ioapic_routing[apic_id].pin_programmed);

	setup_IO_APIC_irq(apic_id, pin, irq, desc,
			irq_trigger(idx), irq_polarity(idx));
}

/*
 * Set up the timer pin, possibly with the 8259A-master behind.
 */
static void __init setup_timer_IRQ0_pin(unsigned int apic_id, unsigned int pin,
					int vector)
{
	struct IO_APIC_route_entry entry;

	if (intr_remapping_enabled)
		return;

	memset(&entry, 0, sizeof(entry));

	/*
	 * We use logical delivery to get the timer IRQ
	 * to the first CPU.
	 */
	entry.dest_mode = apic->irq_dest_mode;
	entry.mask = 0;			/* don't mask IRQ for edge */
	entry.dest = apic->cpu_mask_to_apicid(apic->target_cpus());
	entry.delivery_mode = apic->irq_delivery_mode;
	entry.polarity = 0;
	entry.trigger = 0;
	entry.vector = vector;

	/*
	 * The timer IRQ doesn't have to know that behind the
	 * scene we may have a 8259A-master in AEOI mode ...
	 */
	set_irq_chip_and_handler_name(0, &ioapic_chip, handle_edge_irq, "edge");

	/*
	 * Add it to the IO-APIC irq-routing table:
	 */
	ioapic_write_entry(apic_id, pin, entry);
}


__apicdebuginit(void) print_IO_APIC(void)
{
	int apic, i;
	union IO_APIC_reg_00 reg_00;
	union IO_APIC_reg_01 reg_01;
	union IO_APIC_reg_02 reg_02;
	union IO_APIC_reg_03 reg_03;
	unsigned long flags;
	struct irq_cfg *cfg;
	struct irq_desc *desc;
	unsigned int irq;

	printk(KERN_DEBUG "number of MP IRQ sources: %d.\n", mp_irq_entries);
	for (i = 0; i < nr_ioapics; i++)
		printk(KERN_DEBUG "number of IO-APIC #%d registers: %d.\n",
		       mp_ioapics[i].apicid, nr_ioapic_registers[i]);

	/*
	 * We are a bit conservative about what we expect.  We have to
	 * know about every hardware change ASAP.
	 */
	printk(KERN_INFO "testing the IO APIC.......................\n");

	for (apic = 0; apic < nr_ioapics; apic++) {

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_00.raw = io_apic_read(apic, 0);
	reg_01.raw = io_apic_read(apic, 1);
	if (reg_01.bits.version >= 0x10)
		reg_02.raw = io_apic_read(apic, 2);
	if (reg_01.bits.version >= 0x20)
		reg_03.raw = io_apic_read(apic, 3);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	printk("\n");
	printk(KERN_DEBUG "IO APIC #%d......\n", mp_ioapics[apic].apicid);
	printk(KERN_DEBUG ".... register #00: %08X\n", reg_00.raw);
	printk(KERN_DEBUG ".......    : physical APIC id: %02X\n", reg_00.bits.ID);
	printk(KERN_DEBUG ".......    : Delivery Type: %X\n", reg_00.bits.delivery_type);
	printk(KERN_DEBUG ".......    : LTS          : %X\n", reg_00.bits.LTS);

	printk(KERN_DEBUG ".... register #01: %08X\n", *(int *)&reg_01);
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
	for_each_irq_desc(irq, desc) {
		struct irq_pin_list *entry;

		cfg = desc->chip_data;
		entry = cfg->irq_2_pin;
		if (!entry)
			continue;
		printk(KERN_DEBUG "IRQ%d ", irq);
		for_each_irq_pin(entry, cfg->irq_2_pin)
			printk("-> %d:%d", entry->apic, entry->pin);
		printk("\n");
	}

	printk(KERN_INFO ".................................... done.\n");

	return;
}

__apicdebuginit(void) print_APIC_field(int base)
{
	int i;

	printk(KERN_DEBUG);

	for (i = 0; i < 8; i++)
		printk(KERN_CONT "%08x", apic_read(base + i*0x10));

	printk(KERN_CONT "\n");
}

__apicdebuginit(void) print_local_APIC(void *dummy)
{
	unsigned int i, v, ver, maxlvt;
	u64 icr;

	printk(KERN_DEBUG "printing local APIC contents on CPU#%d/%d:\n",
		smp_processor_id(), hard_smp_processor_id());
	v = apic_read(APIC_ID);
	printk(KERN_INFO "... APIC ID:      %08x (%01x)\n", v, read_apic_id());
	v = apic_read(APIC_LVR);
	printk(KERN_INFO "... APIC VERSION: %08x\n", v);
	ver = GET_APIC_VERSION(v);
	maxlvt = lapic_get_maxlvt();

	v = apic_read(APIC_TASKPRI);
	printk(KERN_DEBUG "... APIC TASKPRI: %08x (%02x)\n", v, v & APIC_TPRI_MASK);

	if (APIC_INTEGRATED(ver)) {                     /* !82489DX */
		if (!APIC_XAPIC(ver)) {
			v = apic_read(APIC_ARBPRI);
			printk(KERN_DEBUG "... APIC ARBPRI: %08x (%02x)\n", v,
			       v & APIC_ARBPRI_MASK);
		}
		v = apic_read(APIC_PROCPRI);
		printk(KERN_DEBUG "... APIC PROCPRI: %08x\n", v);
	}

	/*
	 * Remote read supported only in the 82489DX and local APIC for
	 * Pentium processors.
	 */
	if (!APIC_INTEGRATED(ver) || maxlvt == 3) {
		v = apic_read(APIC_RRR);
		printk(KERN_DEBUG "... APIC RRR: %08x\n", v);
	}

	v = apic_read(APIC_LDR);
	printk(KERN_DEBUG "... APIC LDR: %08x\n", v);
	if (!x2apic_enabled()) {
		v = apic_read(APIC_DFR);
		printk(KERN_DEBUG "... APIC DFR: %08x\n", v);
	}
	v = apic_read(APIC_SPIV);
	printk(KERN_DEBUG "... APIC SPIV: %08x\n", v);

	printk(KERN_DEBUG "... APIC ISR field:\n");
	print_APIC_field(APIC_ISR);
	printk(KERN_DEBUG "... APIC TMR field:\n");
	print_APIC_field(APIC_TMR);
	printk(KERN_DEBUG "... APIC IRR field:\n");
	print_APIC_field(APIC_IRR);

	if (APIC_INTEGRATED(ver)) {             /* !82489DX */
		if (maxlvt > 3)         /* Due to the Pentium erratum 3AP. */
			apic_write(APIC_ESR, 0);

		v = apic_read(APIC_ESR);
		printk(KERN_DEBUG "... APIC ESR: %08x\n", v);
	}

	icr = apic_icr_read();
	printk(KERN_DEBUG "... APIC ICR: %08x\n", (u32)icr);
	printk(KERN_DEBUG "... APIC ICR2: %08x\n", (u32)(icr >> 32));

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

	if (boot_cpu_has(X86_FEATURE_EXTAPIC)) {
		v = apic_read(APIC_EFEAT);
		maxlvt = (v >> 16) & 0xff;
		printk(KERN_DEBUG "... APIC EFEAT: %08x\n", v);
		v = apic_read(APIC_ECTRL);
		printk(KERN_DEBUG "... APIC ECTRL: %08x\n", v);
		for (i = 0; i < maxlvt; i++) {
			v = apic_read(APIC_EILVTn(i));
			printk(KERN_DEBUG "... APIC EILVT%d: %08x\n", i, v);
		}
	}
	printk("\n");
}

__apicdebuginit(void) print_local_APICs(int maxcpu)
{
	int cpu;

	if (!maxcpu)
		return;

	preempt_disable();
	for_each_online_cpu(cpu) {
		if (cpu >= maxcpu)
			break;
		smp_call_function_single(cpu, print_local_APIC, NULL, 1);
	}
	preempt_enable();
}

__apicdebuginit(void) print_PIC(void)
{
	unsigned int v;
	unsigned long flags;

	if (!nr_legacy_irqs)
		return;

	printk(KERN_DEBUG "\nprinting PIC contents\n");

	raw_spin_lock_irqsave(&i8259A_lock, flags);

	v = inb(0xa1) << 8 | inb(0x21);
	printk(KERN_DEBUG "... PIC  IMR: %04x\n", v);

	v = inb(0xa0) << 8 | inb(0x20);
	printk(KERN_DEBUG "... PIC  IRR: %04x\n", v);

	outb(0x0b,0xa0);
	outb(0x0b,0x20);
	v = inb(0xa0) << 8 | inb(0x20);
	outb(0x0a,0xa0);
	outb(0x0a,0x20);

	raw_spin_unlock_irqrestore(&i8259A_lock, flags);

	printk(KERN_DEBUG "... PIC  ISR: %04x\n", v);

	v = inb(0x4d1) << 8 | inb(0x4d0);
	printk(KERN_DEBUG "... PIC ELCR: %04x\n", v);
}

static int __initdata show_lapic = 1;
static __init int setup_show_lapic(char *arg)
{
	int num = -1;

	if (strcmp(arg, "all") == 0) {
		show_lapic = CONFIG_NR_CPUS;
	} else {
		get_option(&arg, &num);
		if (num >= 0)
			show_lapic = num;
	}

	return 1;
}
__setup("show_lapic=", setup_show_lapic);

__apicdebuginit(int) print_ICs(void)
{
	if (apic_verbosity == APIC_QUIET)
		return 0;

	print_PIC();

	/* don't print out if apic is not there */
	if (!cpu_has_apic && !apic_from_smp_config())
		return 0;

	print_local_APICs(show_lapic);
	print_IO_APIC();

	return 0;
}

fs_initcall(print_ICs);


/* Where if anywhere is the i8259 connect in external int mode */
static struct { int pin, apic; } ioapic_i8259 = { -1, -1 };

void __init enable_IO_APIC(void)
{
	union IO_APIC_reg_01 reg_01;
	int i8259_apic, i8259_pin;
	int apic;
	unsigned long flags;

	/*
	 * The number of IO-APIC IRQ registers (== #pins):
	 */
	for (apic = 0; apic < nr_ioapics; apic++) {
		raw_spin_lock_irqsave(&ioapic_lock, flags);
		reg_01.raw = io_apic_read(apic, 1);
		raw_spin_unlock_irqrestore(&ioapic_lock, flags);
		nr_ioapic_registers[apic] = reg_01.bits.entries+1;
	}

	if (!nr_legacy_irqs)
		return;

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

	if (!nr_legacy_irqs)
		return;

	/*
	 * If the i8259 is routed through an IOAPIC
	 * Put that IOAPIC in virtual wire mode
	 * so legacy interrupts can be delivered.
	 *
	 * With interrupt-remapping, for now we will use virtual wire A mode,
	 * as virtual wire B is little complex (need to configure both
	 * IOAPIC RTE aswell as interrupt-remapping table entry).
	 * As this gets called during crash dump, keep this simple for now.
	 */
	if (ioapic_i8259.pin != -1 && !intr_remapping_enabled) {
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
		entry.dest            = read_apic_id();

		/*
		 * Add it to the IO-APIC irq-routing table:
		 */
		ioapic_write_entry(ioapic_i8259.apic, ioapic_i8259.pin, entry);
	}

	/*
	 * Use virtual wire A mode when interrupt remapping is enabled.
	 */
	if (cpu_has_apic || apic_from_smp_config())
		disconnect_bsp_APIC(!intr_remapping_enabled &&
				ioapic_i8259.pin != -1);
}

#ifdef CONFIG_X86_32
/*
 * function to set the IO-APIC physical IDs based on the
 * values stored in the MPC table.
 *
 * by Matt Domsch <Matt_Domsch@dell.com>  Tue Dec 21 12:25:05 CST 1999
 */

void __init setup_ioapic_ids_from_mpc(void)
{
	union IO_APIC_reg_00 reg_00;
	physid_mask_t phys_id_present_map;
	int apic_id;
	int i;
	unsigned char old_id;
	unsigned long flags;

	if (acpi_ioapic)
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
	apic->ioapic_phys_id_map(&phys_cpu_present_map, &phys_id_present_map);

	/*
	 * Set the IOAPIC ID to the value stored in the MPC table.
	 */
	for (apic_id = 0; apic_id < nr_ioapics; apic_id++) {

		/* Read the register 0 value */
		raw_spin_lock_irqsave(&ioapic_lock, flags);
		reg_00.raw = io_apic_read(apic_id, 0);
		raw_spin_unlock_irqrestore(&ioapic_lock, flags);

		old_id = mp_ioapics[apic_id].apicid;

		if (mp_ioapics[apic_id].apicid >= get_physical_broadcast()) {
			printk(KERN_ERR "BIOS bug, IO-APIC#%d ID is %d in the MPC table!...\n",
				apic_id, mp_ioapics[apic_id].apicid);
			printk(KERN_ERR "... fixing up to %d. (tell your hw vendor)\n",
				reg_00.bits.ID);
			mp_ioapics[apic_id].apicid = reg_00.bits.ID;
		}

		/*
		 * Sanity check, is the ID really free? Every APIC in a
		 * system must have a unique ID or we get lots of nice
		 * 'stuck on smp_invalidate_needed IPI wait' messages.
		 */
		if (apic->check_apicid_used(&phys_id_present_map,
					mp_ioapics[apic_id].apicid)) {
			printk(KERN_ERR "BIOS bug, IO-APIC#%d ID %d is already used!...\n",
				apic_id, mp_ioapics[apic_id].apicid);
			for (i = 0; i < get_physical_broadcast(); i++)
				if (!physid_isset(i, phys_id_present_map))
					break;
			if (i >= get_physical_broadcast())
				panic("Max APIC ID exceeded!\n");
			printk(KERN_ERR "... fixing up to %d. (tell your hw vendor)\n",
				i);
			physid_set(i, phys_id_present_map);
			mp_ioapics[apic_id].apicid = i;
		} else {
			physid_mask_t tmp;
			apic->apicid_to_cpu_present(mp_ioapics[apic_id].apicid, &tmp);
			apic_printk(APIC_VERBOSE, "Setting %d in the "
					"phys_id_present_map\n",
					mp_ioapics[apic_id].apicid);
			physids_or(phys_id_present_map, phys_id_present_map, tmp);
		}


		/*
		 * We need to adjust the IRQ routing table
		 * if the ID changed.
		 */
		if (old_id != mp_ioapics[apic_id].apicid)
			for (i = 0; i < mp_irq_entries; i++)
				if (mp_irqs[i].dstapic == old_id)
					mp_irqs[i].dstapic
						= mp_ioapics[apic_id].apicid;

		/*
		 * Read the right value from the MPC table and
		 * write it into the ID register.
		 */
		apic_printk(APIC_VERBOSE, KERN_INFO
			"...changing IO-APIC physical APIC ID to %d ...",
			mp_ioapics[apic_id].apicid);

		reg_00.bits.ID = mp_ioapics[apic_id].apicid;
		raw_spin_lock_irqsave(&ioapic_lock, flags);
		io_apic_write(apic_id, 0, reg_00.raw);
		raw_spin_unlock_irqrestore(&ioapic_lock, flags);

		/*
		 * Sanity check
		 */
		raw_spin_lock_irqsave(&ioapic_lock, flags);
		reg_00.raw = io_apic_read(apic_id, 0);
		raw_spin_unlock_irqrestore(&ioapic_lock, flags);
		if (reg_00.bits.ID != mp_ioapics[apic_id].apicid)
			printk("could not set ID!\n");
		else
			apic_printk(APIC_VERBOSE, " ok.\n");
	}
}
#endif

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
	struct irq_cfg *cfg;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	if (irq < nr_legacy_irqs) {
		disable_8259A_irq(irq);
		if (i8259A_irq_pending(irq))
			was_pending = 1;
	}
	cfg = irq_cfg(irq);
	__unmask_IO_APIC_irq(cfg);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return was_pending;
}

static int ioapic_retrigger_irq(unsigned int irq)
{

	struct irq_cfg *cfg = irq_cfg(irq);
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);
	apic->send_IPI_mask(cpumask_of(cpumask_first(cfg->domain)), cfg->vector);
	raw_spin_unlock_irqrestore(&vector_lock, flags);

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
void send_cleanup_vector(struct irq_cfg *cfg)
{
	cpumask_var_t cleanup_mask;

	if (unlikely(!alloc_cpumask_var(&cleanup_mask, GFP_ATOMIC))) {
		unsigned int i;
		for_each_cpu_and(i, cfg->old_domain, cpu_online_mask)
			apic->send_IPI_mask(cpumask_of(i), IRQ_MOVE_CLEANUP_VECTOR);
	} else {
		cpumask_and(cleanup_mask, cfg->old_domain, cpu_online_mask);
		apic->send_IPI_mask(cleanup_mask, IRQ_MOVE_CLEANUP_VECTOR);
		free_cpumask_var(cleanup_mask);
	}
	cfg->move_in_progress = 0;
}

static void __target_IO_APIC_irq(unsigned int irq, unsigned int dest, struct irq_cfg *cfg)
{
	int apic, pin;
	struct irq_pin_list *entry;
	u8 vector = cfg->vector;

	for_each_irq_pin(entry, cfg->irq_2_pin) {
		unsigned int reg;

		apic = entry->apic;
		pin = entry->pin;
		/*
		 * With interrupt-remapping, destination information comes
		 * from interrupt-remapping table entry.
		 */
		if (!irq_remapped(irq))
			io_apic_write(apic, 0x11 + pin*2, dest);
		reg = io_apic_read(apic, 0x10 + pin*2);
		reg &= ~IO_APIC_REDIR_VECTOR_MASK;
		reg |= vector;
		io_apic_modify(apic, 0x10 + pin*2, reg);
	}
}

/*
 * Either sets desc->affinity to a valid value, and returns
 * ->cpu_mask_to_apicid of that in dest_id, or returns -1 and
 * leaves desc->affinity untouched.
 */
unsigned int
set_desc_affinity(struct irq_desc *desc, const struct cpumask *mask,
		  unsigned int *dest_id)
{
	struct irq_cfg *cfg;
	unsigned int irq;

	if (!cpumask_intersects(mask, cpu_online_mask))
		return -1;

	irq = desc->irq;
	cfg = desc->chip_data;
	if (assign_irq_vector(irq, cfg, mask))
		return -1;

	cpumask_copy(desc->affinity, mask);

	*dest_id = apic->cpu_mask_to_apicid_and(desc->affinity, cfg->domain);
	return 0;
}

static int
set_ioapic_affinity_irq_desc(struct irq_desc *desc, const struct cpumask *mask)
{
	struct irq_cfg *cfg;
	unsigned long flags;
	unsigned int dest;
	unsigned int irq;
	int ret = -1;

	irq = desc->irq;
	cfg = desc->chip_data;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	ret = set_desc_affinity(desc, mask, &dest);
	if (!ret) {
		/* Only the high 8 bits are valid. */
		dest = SET_APIC_LOGICAL_ID(dest);
		__target_IO_APIC_irq(irq, dest, cfg);
	}
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return ret;
}

static int
set_ioapic_affinity_irq(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);

	return set_ioapic_affinity_irq_desc(desc, mask);
}

#ifdef CONFIG_INTR_REMAP

/*
 * Migrate the IO-APIC irq in the presence of intr-remapping.
 *
 * For both level and edge triggered, irq migration is a simple atomic
 * update(of vector and cpu destination) of IRTE and flush the hardware cache.
 *
 * For level triggered, we eliminate the io-apic RTE modification (with the
 * updated vector information), by using a virtual vector (io-apic pin number).
 * Real vector that is used for interrupting cpu will be coming from
 * the interrupt-remapping table entry.
 */
static int
migrate_ioapic_irq_desc(struct irq_desc *desc, const struct cpumask *mask)
{
	struct irq_cfg *cfg;
	struct irte irte;
	unsigned int dest;
	unsigned int irq;
	int ret = -1;

	if (!cpumask_intersects(mask, cpu_online_mask))
		return ret;

	irq = desc->irq;
	if (get_irte(irq, &irte))
		return ret;

	cfg = desc->chip_data;
	if (assign_irq_vector(irq, cfg, mask))
		return ret;

	dest = apic->cpu_mask_to_apicid_and(cfg->domain, mask);

	irte.vector = cfg->vector;
	irte.dest_id = IRTE_DEST(dest);

	/*
	 * Modified the IRTE and flushes the Interrupt entry cache.
	 */
	modify_irte(irq, &irte);

	if (cfg->move_in_progress)
		send_cleanup_vector(cfg);

	cpumask_copy(desc->affinity, mask);

	return 0;
}

/*
 * Migrates the IRQ destination in the process context.
 */
static int set_ir_ioapic_affinity_irq_desc(struct irq_desc *desc,
					    const struct cpumask *mask)
{
	return migrate_ioapic_irq_desc(desc, mask);
}
static int set_ir_ioapic_affinity_irq(unsigned int irq,
				       const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);

	return set_ir_ioapic_affinity_irq_desc(desc, mask);
}
#else
static inline int set_ir_ioapic_affinity_irq_desc(struct irq_desc *desc,
						   const struct cpumask *mask)
{
	return 0;
}
#endif

asmlinkage void smp_irq_move_cleanup_interrupt(void)
{
	unsigned vector, me;

	ack_APIC_irq();
	exit_idle();
	irq_enter();

	me = smp_processor_id();
	for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS; vector++) {
		unsigned int irq;
		unsigned int irr;
		struct irq_desc *desc;
		struct irq_cfg *cfg;
		irq = __get_cpu_var(vector_irq)[vector];

		if (irq == -1)
			continue;

		desc = irq_to_desc(irq);
		if (!desc)
			continue;

		cfg = irq_cfg(irq);
		raw_spin_lock(&desc->lock);

		/*
		 * Check if the irq migration is in progress. If so, we
		 * haven't received the cleanup request yet for this irq.
		 */
		if (cfg->move_in_progress)
			goto unlock;

		if (vector == cfg->vector && cpumask_test_cpu(me, cfg->domain))
			goto unlock;

		irr = apic_read(APIC_IRR + (vector / 32 * 0x10));
		/*
		 * Check if the vector that needs to be cleanedup is
		 * registered at the cpu's IRR. If so, then this is not
		 * the best time to clean it up. Lets clean it up in the
		 * next attempt by sending another IRQ_MOVE_CLEANUP_VECTOR
		 * to myself.
		 */
		if (irr  & (1 << (vector % 32))) {
			apic->send_IPI_self(IRQ_MOVE_CLEANUP_VECTOR);
			goto unlock;
		}
		__get_cpu_var(vector_irq)[vector] = -1;
unlock:
		raw_spin_unlock(&desc->lock);
	}

	irq_exit();
}

static void __irq_complete_move(struct irq_desc **descp, unsigned vector)
{
	struct irq_desc *desc = *descp;
	struct irq_cfg *cfg = desc->chip_data;
	unsigned me;

	if (likely(!cfg->move_in_progress))
		return;

	me = smp_processor_id();

	if (vector == cfg->vector && cpumask_test_cpu(me, cfg->domain))
		send_cleanup_vector(cfg);
}

static void irq_complete_move(struct irq_desc **descp)
{
	__irq_complete_move(descp, ~get_irq_regs()->orig_ax);
}

void irq_force_complete_move(int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg = desc->chip_data;

	__irq_complete_move(&desc, cfg->vector);
}
#else
static inline void irq_complete_move(struct irq_desc **descp) {}
#endif

static void ack_apic_edge(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	irq_complete_move(&desc);
	move_native_irq(irq);
	ack_APIC_irq();
}

atomic_t irq_mis_count;

/*
 * IO-APIC versions below 0x20 don't support EOI register.
 * For the record, here is the information about various versions:
 *     0Xh     82489DX
 *     1Xh     I/OAPIC or I/O(x)APIC which are not PCI 2.2 Compliant
 *     2Xh     I/O(x)APIC which is PCI 2.2 Compliant
 *     30h-FFh Reserved
 *
 * Some of the Intel ICH Specs (ICH2 to ICH5) documents the io-apic
 * version as 0x2. This is an error with documentation and these ICH chips
 * use io-apic's of version 0x20.
 *
 * For IO-APIC's with EOI register, we use that to do an explicit EOI.
 * Otherwise, we simulate the EOI message manually by changing the trigger
 * mode to edge and then back to level, with RTE being masked during this.
*/
static void __eoi_ioapic_irq(unsigned int irq, struct irq_cfg *cfg)
{
	struct irq_pin_list *entry;

	for_each_irq_pin(entry, cfg->irq_2_pin) {
		if (mp_ioapics[entry->apic].apicver >= 0x20) {
			/*
			 * Intr-remapping uses pin number as the virtual vector
			 * in the RTE. Actual vector is programmed in
			 * intr-remapping table entry. Hence for the io-apic
			 * EOI we use the pin number.
			 */
			if (irq_remapped(irq))
				io_apic_eoi(entry->apic, entry->pin);
			else
				io_apic_eoi(entry->apic, cfg->vector);
		} else {
			__mask_and_edge_IO_APIC_irq(entry);
			__unmask_and_level_IO_APIC_irq(entry);
		}
	}
}

static void eoi_ioapic_irq(struct irq_desc *desc)
{
	struct irq_cfg *cfg;
	unsigned long flags;
	unsigned int irq;

	irq = desc->irq;
	cfg = desc->chip_data;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	__eoi_ioapic_irq(irq, cfg);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void ack_apic_level(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	unsigned long v;
	int i;
	struct irq_cfg *cfg;
	int do_unmask_irq = 0;

	irq_complete_move(&desc);
#ifdef CONFIG_GENERIC_PENDING_IRQ
	/* If we are moving the irq we need to mask it */
	if (unlikely(desc->status & IRQ_MOVE_PENDING)) {
		do_unmask_irq = 1;
		mask_IO_APIC_irq_desc(desc);
	}
#endif

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
	 *
	 * Also in the case when cpu goes offline, fixup_irqs() will forward
	 * any unhandled interrupt on the offlined cpu to the new cpu
	 * destination that is handling the corresponding interrupt. This
	 * interrupt forwarding is done via IPI's. Hence, in this case also
	 * level-triggered io-apic interrupt will be seen as an edge
	 * interrupt in the IRR. And we can't rely on the cpu's EOI
	 * to be broadcasted to the IO-APIC's which will clear the remoteIRR
	 * corresponding to the level-triggered interrupt. Hence on IO-APIC's
	 * supporting EOI register, we do an explicit EOI to clear the
	 * remote IRR and on IO-APIC's which don't have an EOI register,
	 * we use the above logic (mask+edge followed by unmask+level) from
	 * Manfred Spraul to clear the remote IRR.
	 */
	cfg = desc->chip_data;
	i = cfg->vector;
	v = apic_read(APIC_TMR + ((i & ~0x1f) >> 1));

	/*
	 * We must acknowledge the irq before we move it or the acknowledge will
	 * not propagate properly.
	 */
	ack_APIC_irq();

	/*
	 * Tail end of clearing remote IRR bit (either by delivering the EOI
	 * message via io-apic EOI register write or simulating it using
	 * mask+edge followed by unnask+level logic) manually when the
	 * level triggered interrupt is seen as the edge triggered interrupt
	 * at the cpu.
	 */
	if (!(v & (1 << (i & 0x1f)))) {
		atomic_inc(&irq_mis_count);

		eoi_ioapic_irq(desc);
	}

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
		cfg = desc->chip_data;
		if (!io_apic_level_ack_pending(cfg))
			move_masked_irq(irq);
		unmask_IO_APIC_irq_desc(desc);
	}
}

#ifdef CONFIG_INTR_REMAP
static void ir_ack_apic_edge(unsigned int irq)
{
	ack_APIC_irq();
}

static void ir_ack_apic_level(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	ack_APIC_irq();
	eoi_ioapic_irq(desc);
}
#endif /* CONFIG_INTR_REMAP */

static struct irq_chip ioapic_chip __read_mostly = {
	.name		= "IO-APIC",
	.startup	= startup_ioapic_irq,
	.mask		= mask_IO_APIC_irq,
	.unmask		= unmask_IO_APIC_irq,
	.ack		= ack_apic_edge,
	.eoi		= ack_apic_level,
#ifdef CONFIG_SMP
	.set_affinity	= set_ioapic_affinity_irq,
#endif
	.retrigger	= ioapic_retrigger_irq,
};

static struct irq_chip ir_ioapic_chip __read_mostly = {
	.name		= "IR-IO-APIC",
	.startup	= startup_ioapic_irq,
	.mask		= mask_IO_APIC_irq,
	.unmask		= unmask_IO_APIC_irq,
#ifdef CONFIG_INTR_REMAP
	.ack		= ir_ack_apic_edge,
	.eoi		= ir_ack_apic_level,
#ifdef CONFIG_SMP
	.set_affinity	= set_ir_ioapic_affinity_irq,
#endif
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
	for_each_irq_desc(irq, desc) {
		cfg = desc->chip_data;
		if (IO_APIC_IRQ(irq) && cfg && !cfg->vector) {
			/*
			 * Hmm.. We don't have an entry for this,
			 * so default to an old-fashioned 8259
			 * interrupt if we can..
			 */
			if (irq < nr_legacy_irqs)
				make_8259A_irq(irq);
			else
				/* Strange. Oh, well.. */
				desc->chip = &no_irq_chip;
		}
	}
}

/*
 * The local APIC irq-chip implementation:
 */

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

static void ack_lapic_irq(unsigned int irq)
{
	ack_APIC_irq();
}

static struct irq_chip lapic_chip __read_mostly = {
	.name		= "local-APIC",
	.mask		= mask_lapic_irq,
	.unmask		= unmask_lapic_irq,
	.ack		= ack_lapic_irq,
};

static void lapic_register_intr(int irq, struct irq_desc *desc)
{
	desc->status &= ~IRQ_LEVEL;
	set_irq_chip_and_handler_name(irq, &lapic_chip, handle_edge_irq,
				      "edge");
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

static int disable_timer_pin_1 __initdata;
/* Actually the next is obsolete, but keep it for paranoid reasons -AK */
static int __init disable_timer_pin_setup(char *arg)
{
	disable_timer_pin_1 = 1;
	return 0;
}
early_param("disable_timer_pin_1", disable_timer_pin_setup);

int timer_through_8259 __initdata;

/*
 * This code may look a bit paranoid, but it's supposed to cooperate with
 * a wide range of boards and BIOS bugs.  Fortunately only the timer IRQ
 * is so screwy.  Thanks to Brian Perkins for testing/hacking this beast
 * fanatically on his truly buggy board.
 *
 * FIXME: really need to revamp this for all platforms.
 */
static inline void __init check_timer(void)
{
	struct irq_desc *desc = irq_to_desc(0);
	struct irq_cfg *cfg = desc->chip_data;
	int node = cpu_to_node(boot_cpu_id);
	int apic1, pin1, apic2, pin2;
	unsigned long flags;
	int no_pin1 = 0;

	local_irq_save(flags);

	/*
	 * get/set the timer IRQ vector:
	 */
	disable_8259A_irq(0);
	assign_irq_vector(0, cfg, apic->target_cpus());

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
#ifdef CONFIG_X86_32
	{
		unsigned int ver;

		ver = apic_read(APIC_LVR);
		ver = GET_APIC_VERSION(ver);
		timer_ack = (nmi_watchdog == NMI_IO_APIC && !APIC_INTEGRATED(ver));
	}
#endif

	pin1  = find_isa_irq_pin(0, mp_INT);
	apic1 = find_isa_irq_apic(0, mp_INT);
	pin2  = ioapic_i8259.pin;
	apic2 = ioapic_i8259.apic;

	apic_printk(APIC_QUIET, KERN_INFO "..TIMER: vector=0x%02X "
		    "apic1=%d pin1=%d apic2=%d pin2=%d\n",
		    cfg->vector, apic1, pin1, apic2, pin2);

	/*
	 * Some BIOS writers are clueless and report the ExtINTA
	 * I/O APIC input from the cascaded 8259A as the timer
	 * interrupt input.  So just in case, if only one pin
	 * was found above, try it both directly and through the
	 * 8259A.
	 */
	if (pin1 == -1) {
		if (intr_remapping_enabled)
			panic("BIOS bug: timer not connected to IO-APIC");
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
			add_pin_to_irq_node(cfg, node, apic1, pin1);
			setup_timer_IRQ0_pin(apic1, pin1, cfg->vector);
		} else {
			/* for edge trigger, setup_IO_APIC_irq already
			 * leave it unmasked.
			 * so only need to unmask if it is level-trigger
			 * do we really have level trigger timer?
			 */
			int idx;
			idx = find_irq_entry(apic1, pin1, mp_INT);
			if (idx != -1 && irq_trigger(idx))
				unmask_IO_APIC_irq_desc(desc);
		}
		if (timer_irq_works()) {
			if (nmi_watchdog == NMI_IO_APIC) {
				setup_nmi();
				enable_8259A_irq(0);
			}
			if (disable_timer_pin_1 > 0)
				clear_IO_APIC_pin(0, pin1);
			goto out;
		}
		if (intr_remapping_enabled)
			panic("timer doesn't work through Interrupt-remapped IO-APIC");
		local_irq_disable();
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
		replace_pin_at_irq_node(cfg, node, apic1, pin1, apic2, pin2);
		setup_timer_IRQ0_pin(apic2, pin2, cfg->vector);
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
		local_irq_disable();
		disable_8259A_irq(0);
		clear_IO_APIC_pin(apic2, pin2);
		apic_printk(APIC_QUIET, KERN_INFO "....... failed.\n");
	}

	if (nmi_watchdog == NMI_IO_APIC) {
		apic_printk(APIC_QUIET, KERN_WARNING "timer doesn't work "
			    "through the IO-APIC - disabling NMI Watchdog!\n");
		nmi_watchdog = NMI_NONE;
	}
#ifdef CONFIG_X86_32
	timer_ack = 0;
#endif

	apic_printk(APIC_QUIET, KERN_INFO
		    "...trying to set up timer as Virtual Wire IRQ...\n");

	lapic_register_intr(0, desc);
	apic_write(APIC_LVT0, APIC_DM_FIXED | cfg->vector);	/* Fixed mode */
	enable_8259A_irq(0);

	if (timer_irq_works()) {
		apic_printk(APIC_QUIET, KERN_INFO "..... works.\n");
		goto out;
	}
	local_irq_disable();
	disable_8259A_irq(0);
	apic_write(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_FIXED | cfg->vector);
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
	local_irq_disable();
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
#define PIC_IRQS	(1UL << PIC_CASCADE_IR)

void __init setup_IO_APIC(void)
{

	/*
	 * calling enable_IO_APIC() is moved to setup_local_APIC for BP
	 */
	io_apic_irqs = nr_legacy_irqs ? ~PIC_IRQS : ~0UL;

	apic_printk(APIC_VERBOSE, "ENABLING IO-APIC IRQs\n");
	/*
         * Set up IO-APIC IRQ routing.
         */
	x86_init.mpparse.setup_ioapic_ids();

	sync_Arb_IDs();
	setup_IO_APIC_irqs();
	init_IO_APIC_traps();
	if (nr_legacy_irqs)
		check_timer();
}

/*
 *      Called after all the initialization is done. If we didnt find any
 *      APIC bugs then we can allow the modify fast path
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

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_00.raw = io_apic_read(dev->id, 0);
	if (reg_00.bits.ID != mp_ioapics[dev->id].apicid) {
		reg_00.bits.ID = mp_ioapics[dev->id].apicid;
		io_apic_write(dev->id, 0, reg_00.raw);
	}
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
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
unsigned int create_irq_nr(unsigned int irq_want, int node)
{
	/* Allocate an unused irq */
	unsigned int irq;
	unsigned int new;
	unsigned long flags;
	struct irq_cfg *cfg_new = NULL;
	struct irq_desc *desc_new = NULL;

	irq = 0;
	if (irq_want < nr_irqs_gsi)
		irq_want = nr_irqs_gsi;

	raw_spin_lock_irqsave(&vector_lock, flags);
	for (new = irq_want; new < nr_irqs; new++) {
		desc_new = irq_to_desc_alloc_node(new, node);
		if (!desc_new) {
			printk(KERN_INFO "can not get irq_desc for %d\n", new);
			continue;
		}
		cfg_new = desc_new->chip_data;

		if (cfg_new->vector != 0)
			continue;

		desc_new = move_irq_desc(desc_new, node);
		cfg_new = desc_new->chip_data;

		if (__assign_irq_vector(new, cfg_new, apic->target_cpus()) == 0)
			irq = new;
		break;
	}
	raw_spin_unlock_irqrestore(&vector_lock, flags);

	if (irq > 0)
		dynamic_irq_init_keep_chip_data(irq);

	return irq;
}

int create_irq(void)
{
	int node = cpu_to_node(boot_cpu_id);
	unsigned int irq_want;
	int irq;

	irq_want = nr_irqs_gsi;
	irq = create_irq_nr(irq_want, node);

	if (irq == 0)
		irq = -1;

	return irq;
}

void destroy_irq(unsigned int irq)
{
	unsigned long flags;

	dynamic_irq_cleanup_keep_chip_data(irq);

	free_irte(irq);
	raw_spin_lock_irqsave(&vector_lock, flags);
	__clear_irq_vector(irq, get_irq_chip_data(irq));
	raw_spin_unlock_irqrestore(&vector_lock, flags);
}

/*
 * MSI message composition
 */
#ifdef CONFIG_PCI_MSI
static int msi_compose_msg(struct pci_dev *pdev, unsigned int irq,
			   struct msi_msg *msg, u8 hpet_id)
{
	struct irq_cfg *cfg;
	int err;
	unsigned dest;

	if (disable_apic)
		return -ENXIO;

	cfg = irq_cfg(irq);
	err = assign_irq_vector(irq, cfg, apic->target_cpus());
	if (err)
		return err;

	dest = apic->cpu_mask_to_apicid_and(cfg->domain, apic->target_cpus());

	if (irq_remapped(irq)) {
		struct irte irte;
		int ir_index;
		u16 sub_handle;

		ir_index = map_irq_to_irte_handle(irq, &sub_handle);
		BUG_ON(ir_index == -1);

		memset (&irte, 0, sizeof(irte));

		irte.present = 1;
		irte.dst_mode = apic->irq_dest_mode;
		irte.trigger_mode = 0; /* edge */
		irte.dlvry_mode = apic->irq_delivery_mode;
		irte.vector = cfg->vector;
		irte.dest_id = IRTE_DEST(dest);

		/* Set source-id of interrupt request */
		if (pdev)
			set_msi_sid(&irte, pdev);
		else
			set_hpet_sid(&irte, hpet_id);

		modify_irte(irq, &irte);

		msg->address_hi = MSI_ADDR_BASE_HI;
		msg->data = sub_handle;
		msg->address_lo = MSI_ADDR_BASE_LO | MSI_ADDR_IR_EXT_INT |
				  MSI_ADDR_IR_SHV |
				  MSI_ADDR_IR_INDEX1(ir_index) |
				  MSI_ADDR_IR_INDEX2(ir_index);
	} else {
		if (x2apic_enabled())
			msg->address_hi = MSI_ADDR_BASE_HI |
					  MSI_ADDR_EXT_DEST_ID(dest);
		else
			msg->address_hi = MSI_ADDR_BASE_HI;

		msg->address_lo =
			MSI_ADDR_BASE_LO |
			((apic->irq_dest_mode == 0) ?
				MSI_ADDR_DEST_MODE_PHYSICAL:
				MSI_ADDR_DEST_MODE_LOGICAL) |
			((apic->irq_delivery_mode != dest_LowestPrio) ?
				MSI_ADDR_REDIRECTION_CPU:
				MSI_ADDR_REDIRECTION_LOWPRI) |
			MSI_ADDR_DEST_ID(dest);

		msg->data =
			MSI_DATA_TRIGGER_EDGE |
			MSI_DATA_LEVEL_ASSERT |
			((apic->irq_delivery_mode != dest_LowestPrio) ?
				MSI_DATA_DELIVERY_FIXED:
				MSI_DATA_DELIVERY_LOWPRI) |
			MSI_DATA_VECTOR(cfg->vector);
	}
	return err;
}

#ifdef CONFIG_SMP
static int set_msi_irq_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg;
	struct msi_msg msg;
	unsigned int dest;

	if (set_desc_affinity(desc, mask, &dest))
		return -1;

	cfg = desc->chip_data;

	read_msi_msg_desc(desc, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	write_msi_msg_desc(desc, &msg);

	return 0;
}
#ifdef CONFIG_INTR_REMAP
/*
 * Migrate the MSI irq to another cpumask. This migration is
 * done in the process context using interrupt-remapping hardware.
 */
static int
ir_set_msi_irq_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg = desc->chip_data;
	unsigned int dest;
	struct irte irte;

	if (get_irte(irq, &irte))
		return -1;

	if (set_desc_affinity(desc, mask, &dest))
		return -1;

	irte.vector = cfg->vector;
	irte.dest_id = IRTE_DEST(dest);

	/*
	 * atomically update the IRTE with the new destination and vector.
	 */
	modify_irte(irq, &irte);

	/*
	 * After this point, all the interrupts will start arriving
	 * at the new destination. So, time to cleanup the previous
	 * vector allocation.
	 */
	if (cfg->move_in_progress)
		send_cleanup_vector(cfg);

	return 0;
}

#endif
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

static struct irq_chip msi_ir_chip = {
	.name		= "IR-PCI-MSI",
	.unmask		= unmask_msi_irq,
	.mask		= mask_msi_irq,
#ifdef CONFIG_INTR_REMAP
	.ack		= ir_ack_apic_edge,
#ifdef CONFIG_SMP
	.set_affinity	= ir_set_msi_irq_affinity,
#endif
#endif
	.retrigger	= ioapic_retrigger_irq,
};

/*
 * Map the PCI dev to the corresponding remapping hardware unit
 * and allocate 'nvec' consecutive interrupt-remapping table entries
 * in it.
 */
static int msi_alloc_irte(struct pci_dev *dev, int irq, int nvec)
{
	struct intel_iommu *iommu;
	int index;

	iommu = map_dev_to_ir(dev);
	if (!iommu) {
		printk(KERN_ERR
		       "Unable to map PCI %s to iommu\n", pci_name(dev));
		return -ENOENT;
	}

	index = alloc_irte(iommu, irq, nvec);
	if (index < 0) {
		printk(KERN_ERR
		       "Unable to allocate %d IRTE for PCI %s\n", nvec,
		       pci_name(dev));
		return -ENOSPC;
	}
	return index;
}

static int setup_msi_irq(struct pci_dev *dev, struct msi_desc *msidesc, int irq)
{
	int ret;
	struct msi_msg msg;

	ret = msi_compose_msg(dev, irq, &msg, -1);
	if (ret < 0)
		return ret;

	set_irq_msi(irq, msidesc);
	write_msi_msg(irq, &msg);

	if (irq_remapped(irq)) {
		struct irq_desc *desc = irq_to_desc(irq);
		/*
		 * irq migration in process context
		 */
		desc->status |= IRQ_MOVE_PCNTXT;
		set_irq_chip_and_handler_name(irq, &msi_ir_chip, handle_edge_irq, "edge");
	} else
		set_irq_chip_and_handler_name(irq, &msi_chip, handle_edge_irq, "edge");

	dev_printk(KERN_DEBUG, &dev->dev, "irq %d for MSI/MSI-X\n", irq);

	return 0;
}

int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	unsigned int irq;
	int ret, sub_handle;
	struct msi_desc *msidesc;
	unsigned int irq_want;
	struct intel_iommu *iommu = NULL;
	int index = 0;
	int node;

	/* x86 doesn't support multiple MSI yet */
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	node = dev_to_node(&dev->dev);
	irq_want = nr_irqs_gsi;
	sub_handle = 0;
	list_for_each_entry(msidesc, &dev->msi_list, list) {
		irq = create_irq_nr(irq_want, node);
		if (irq == 0)
			return -1;
		irq_want = irq + 1;
		if (!intr_remapping_enabled)
			goto no_ir;

		if (!sub_handle) {
			/*
			 * allocate the consecutive block of IRTE's
			 * for 'nvec'
			 */
			index = msi_alloc_irte(dev, irq, nvec);
			if (index < 0) {
				ret = index;
				goto error;
			}
		} else {
			iommu = map_dev_to_ir(dev);
			if (!iommu) {
				ret = -ENOENT;
				goto error;
			}
			/*
			 * setup the mapping between the irq and the IRTE
			 * base index, the sub_handle pointing to the
			 * appropriate interrupt remap table entry.
			 */
			set_irte_irq(irq, iommu, index, sub_handle);
		}
no_ir:
		ret = setup_msi_irq(dev, msidesc, irq);
		if (ret < 0)
			goto error;
		sub_handle++;
	}
	return 0;

error:
	destroy_irq(irq);
	return ret;
}

void arch_teardown_msi_irq(unsigned int irq)
{
	destroy_irq(irq);
}

#if defined (CONFIG_DMAR) || defined (CONFIG_INTR_REMAP)
#ifdef CONFIG_SMP
static int dmar_msi_set_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg;
	struct msi_msg msg;
	unsigned int dest;

	if (set_desc_affinity(desc, mask, &dest))
		return -1;

	cfg = desc->chip_data;

	dmar_msi_read(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	dmar_msi_write(irq, &msg);

	return 0;
}

#endif /* CONFIG_SMP */

static struct irq_chip dmar_msi_type = {
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

	ret = msi_compose_msg(NULL, irq, &msg, -1);
	if (ret < 0)
		return ret;
	dmar_msi_write(irq, &msg);
	set_irq_chip_and_handler_name(irq, &dmar_msi_type, handle_edge_irq,
		"edge");
	return 0;
}
#endif

#ifdef CONFIG_HPET_TIMER

#ifdef CONFIG_SMP
static int hpet_msi_set_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg;
	struct msi_msg msg;
	unsigned int dest;

	if (set_desc_affinity(desc, mask, &dest))
		return -1;

	cfg = desc->chip_data;

	hpet_msi_read(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	hpet_msi_write(irq, &msg);

	return 0;
}

#endif /* CONFIG_SMP */

static struct irq_chip ir_hpet_msi_type = {
	.name = "IR-HPET_MSI",
	.unmask = hpet_msi_unmask,
	.mask = hpet_msi_mask,
#ifdef CONFIG_INTR_REMAP
	.ack = ir_ack_apic_edge,
#ifdef CONFIG_SMP
	.set_affinity = ir_set_msi_irq_affinity,
#endif
#endif
	.retrigger = ioapic_retrigger_irq,
};

static struct irq_chip hpet_msi_type = {
	.name = "HPET_MSI",
	.unmask = hpet_msi_unmask,
	.mask = hpet_msi_mask,
	.ack = ack_apic_edge,
#ifdef CONFIG_SMP
	.set_affinity = hpet_msi_set_affinity,
#endif
	.retrigger = ioapic_retrigger_irq,
};

int arch_setup_hpet_msi(unsigned int irq, unsigned int id)
{
	int ret;
	struct msi_msg msg;
	struct irq_desc *desc = irq_to_desc(irq);

	if (intr_remapping_enabled) {
		struct intel_iommu *iommu = map_hpet_to_ir(id);
		int index;

		if (!iommu)
			return -1;

		index = alloc_irte(iommu, irq, 1);
		if (index < 0)
			return -1;
	}

	ret = msi_compose_msg(NULL, irq, &msg, id);
	if (ret < 0)
		return ret;

	hpet_msi_write(irq, &msg);
	desc->status |= IRQ_MOVE_PCNTXT;
	if (irq_remapped(irq))
		set_irq_chip_and_handler_name(irq, &ir_hpet_msi_type,
					      handle_edge_irq, "edge");
	else
		set_irq_chip_and_handler_name(irq, &hpet_msi_type,
					      handle_edge_irq, "edge");

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

static int set_ht_irq_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg;
	unsigned int dest;

	if (set_desc_affinity(desc, mask, &dest))
		return -1;

	cfg = desc->chip_data;

	target_ht_irq(irq, dest, cfg->vector);

	return 0;
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
	struct irq_cfg *cfg;
	int err;

	if (disable_apic)
		return -ENXIO;

	cfg = irq_cfg(irq);
	err = assign_irq_vector(irq, cfg, apic->target_cpus());
	if (!err) {
		struct ht_irq_msg msg;
		unsigned dest;

		dest = apic->cpu_mask_to_apicid_and(cfg->domain,
						    apic->target_cpus());

		msg.address_hi = HT_IRQ_HIGH_DEST_ID(dest);

		msg.address_lo =
			HT_IRQ_LOW_BASE |
			HT_IRQ_LOW_DEST_ID(dest) |
			HT_IRQ_LOW_VECTOR(cfg->vector) |
			((apic->irq_dest_mode == 0) ?
				HT_IRQ_LOW_DM_PHYSICAL :
				HT_IRQ_LOW_DM_LOGICAL) |
			HT_IRQ_LOW_RQEOI_EDGE |
			((apic->irq_delivery_mode != dest_LowestPrio) ?
				HT_IRQ_LOW_MT_FIXED :
				HT_IRQ_LOW_MT_ARBITRATED) |
			HT_IRQ_LOW_IRQ_MASKED;

		write_ht_irq_msg(irq, &msg);

		set_irq_chip_and_handler_name(irq, &ht_irq_chip,
					      handle_edge_irq, "edge");

		dev_printk(KERN_DEBUG, &dev->dev, "irq %d for HT\n", irq);
	}
	return err;
}
#endif /* CONFIG_HT_IRQ */

int __init io_apic_get_redir_entries (int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return reg_01.bits.entries;
}

void __init probe_nr_irqs_gsi(void)
{
	int nr = 0;

	nr = acpi_probe_gsi();
	if (nr > nr_irqs_gsi) {
		nr_irqs_gsi = nr;
	} else {
		/* for acpi=off or acpi is not compiled in */
		int idx;

		nr = 0;
		for (idx = 0; idx < nr_ioapics; idx++)
			nr += io_apic_get_redir_entries(idx) + 1;

		if (nr > nr_irqs_gsi)
			nr_irqs_gsi = nr;
	}

	printk(KERN_DEBUG "nr_irqs_gsi: %d\n", nr_irqs_gsi);
}

static int __io_apic_set_pci_routing(struct device *dev, int irq,
				struct io_apic_irq_attr *irq_attr)
{
	struct irq_desc *desc;
	struct irq_cfg *cfg;
	int node;
	int ioapic, pin;
	int trigger, polarity;

	ioapic = irq_attr->ioapic;
	if (!IO_APIC_IRQ(irq)) {
		apic_printk(APIC_QUIET,KERN_ERR "IOAPIC[%d]: Invalid reference to IRQ 0\n",
			ioapic);
		return -EINVAL;
	}

	if (dev)
		node = dev_to_node(dev);
	else
		node = cpu_to_node(boot_cpu_id);

	desc = irq_to_desc_alloc_node(irq, node);
	if (!desc) {
		printk(KERN_INFO "can not get irq_desc %d\n", irq);
		return 0;
	}

	pin = irq_attr->ioapic_pin;
	trigger = irq_attr->trigger;
	polarity = irq_attr->polarity;

	/*
	 * IRQs < 16 are already in the irq_2_pin[] map
	 */
	if (irq >= nr_legacy_irqs) {
		cfg = desc->chip_data;
		if (add_pin_to_irq_node_nopanic(cfg, node, ioapic, pin)) {
			printk(KERN_INFO "can not add pin %d for irq %d\n",
				pin, irq);
			return 0;
		}
	}

	setup_IO_APIC_irq(ioapic, pin, irq, desc, trigger, polarity);

	return 0;
}

int io_apic_set_pci_routing(struct device *dev, int irq,
				struct io_apic_irq_attr *irq_attr)
{
	int ioapic, pin;
	/*
	 * Avoid pin reprogramming.  PRTs typically include entries
	 * with redundant pin->gsi mappings (but unique PCI devices);
	 * we only program the IOAPIC on the first.
	 */
	ioapic = irq_attr->ioapic;
	pin = irq_attr->ioapic_pin;
	if (test_bit(pin, mp_ioapic_routing[ioapic].pin_programmed)) {
		pr_debug("Pin %d-%d already programmed\n",
			 mp_ioapics[ioapic].apicid, pin);
		return 0;
	}
	set_bit(pin, mp_ioapic_routing[ioapic].pin_programmed);

	return __io_apic_set_pci_routing(dev, irq, irq_attr);
}

u8 __init io_apic_unique_id(u8 id)
{
#ifdef CONFIG_X86_32
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) &&
	    !APIC_XAPIC(apic_version[boot_cpu_physical_apicid]))
		return io_apic_get_unique_id(nr_ioapics, id);
	else
		return id;
#else
	int i;
	DECLARE_BITMAP(used, 256);

	bitmap_zero(used, 256);
	for (i = 0; i < nr_ioapics; i++) {
		struct mpc_ioapic *ia = &mp_ioapics[i];
		__set_bit(ia->apicid, used);
	}
	if (!test_bit(id, used))
		return id;
	return find_first_zero_bit(used, 256);
#endif
}

#ifdef CONFIG_X86_32
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
		apic->ioapic_phys_id_map(&phys_cpu_present_map, &apic_id_map);

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_00.raw = io_apic_read(ioapic, 0);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	if (apic_id >= get_physical_broadcast()) {
		printk(KERN_WARNING "IOAPIC[%d]: Invalid apic_id %d, trying "
			"%d\n", ioapic, apic_id, reg_00.bits.ID);
		apic_id = reg_00.bits.ID;
	}

	/*
	 * Every APIC in a system must have a unique ID or we get lots of nice
	 * 'stuck on smp_invalidate_needed IPI wait' messages.
	 */
	if (apic->check_apicid_used(&apic_id_map, apic_id)) {

		for (i = 0; i < get_physical_broadcast(); i++) {
			if (!apic->check_apicid_used(&apic_id_map, i))
				break;
		}

		if (i == get_physical_broadcast())
			panic("Max apic_id exceeded!\n");

		printk(KERN_WARNING "IOAPIC[%d]: apic_id %d already used, "
			"trying %d\n", ioapic, apic_id, i);

		apic_id = i;
	}

	apic->apicid_to_cpu_present(apic_id, &tmp);
	physids_or(apic_id_map, apic_id_map, tmp);

	if (reg_00.bits.ID != apic_id) {
		reg_00.bits.ID = apic_id;

		raw_spin_lock_irqsave(&ioapic_lock, flags);
		io_apic_write(ioapic, 0, reg_00.raw);
		reg_00.raw = io_apic_read(ioapic, 0);
		raw_spin_unlock_irqrestore(&ioapic_lock, flags);

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
#endif

int __init io_apic_get_version(int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return reg_01.bits.version;
}

int acpi_get_override_irq(int bus_irq, int *trigger, int *polarity)
{
	int i;

	if (skip_ioapic_setup)
		return -1;

	for (i = 0; i < mp_irq_entries; i++)
		if (mp_irqs[i].irqtype == mp_INT &&
		    mp_irqs[i].srcbusirq == bus_irq)
			break;
	if (i >= mp_irq_entries)
		return -1;

	*trigger = irq_trigger(i);
	*polarity = irq_polarity(i);
	return 0;
}

/*
 * This function currently is only a helper for the i386 smp boot process where
 * we need to reprogram the ioredtbls to cater for the cpus which have come online
 * so mask in all cases should simply be apic->target_cpus()
 */
#ifdef CONFIG_SMP
void __init setup_ioapic_dest(void)
{
	int pin, ioapic = 0, irq, irq_entry;
	struct irq_desc *desc;
	const struct cpumask *mask;

	if (skip_ioapic_setup == 1)
		return;

#ifdef CONFIG_ACPI
	if (!acpi_disabled && acpi_ioapic) {
		ioapic = mp_find_ioapic(0);
		if (ioapic < 0)
			ioapic = 0;
	}
#endif

	for (pin = 0; pin < nr_ioapic_registers[ioapic]; pin++) {
		irq_entry = find_irq_entry(ioapic, pin, mp_INT);
		if (irq_entry == -1)
			continue;
		irq = pin_2_irq(irq_entry, ioapic, pin);

		desc = irq_to_desc(irq);

		/*
		 * Honour affinities which have been set in early boot
		 */
		if (desc->status &
		    (IRQ_NO_BALANCING | IRQ_AFFINITY_SET))
			mask = desc->affinity;
		else
			mask = apic->target_cpus();

		if (intr_remapping_enabled)
			set_ir_ioapic_affinity_irq_desc(desc, mask);
		else
			set_ioapic_affinity_irq_desc(desc, mask);
	}

}
#endif

#define IOAPIC_RESOURCE_NAME_SIZE 11

static struct resource *ioapic_resources;

static struct resource * __init ioapic_setup_resources(int nr_ioapics)
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

	mem += sizeof(struct resource) * nr_ioapics;

	for (i = 0; i < nr_ioapics; i++) {
		res[i].name = mem;
		res[i].flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		snprintf(mem, IOAPIC_RESOURCE_NAME_SIZE, "IOAPIC %u", i);
		mem += IOAPIC_RESOURCE_NAME_SIZE;
	}

	ioapic_resources = res;

	return res;
}

void __init ioapic_init_mappings(void)
{
	unsigned long ioapic_phys, idx = FIX_IO_APIC_BASE_0;
	struct resource *ioapic_res;
	int i;

	ioapic_res = ioapic_setup_resources(nr_ioapics);
	for (i = 0; i < nr_ioapics; i++) {
		if (smp_found_config) {
			ioapic_phys = mp_ioapics[i].apicaddr;
#ifdef CONFIG_X86_32
			if (!ioapic_phys) {
				printk(KERN_ERR
				       "WARNING: bogus zero IO-APIC "
				       "address found in MPTABLE, "
				       "disabling IO/APIC support!\n");
				smp_found_config = 0;
				skip_ioapic_setup = 1;
				goto fake_ioapic_page;
			}
#endif
		} else {
#ifdef CONFIG_X86_32
fake_ioapic_page:
#endif
			ioapic_phys = (unsigned long)alloc_bootmem_pages(PAGE_SIZE);
			ioapic_phys = __pa(ioapic_phys);
		}
		set_fixmap_nocache(idx, ioapic_phys);
		apic_printk(APIC_VERBOSE, "mapped IOAPIC to %08lx (%08lx)\n",
			__fix_to_virt(idx) + (ioapic_phys & ~PAGE_MASK),
			ioapic_phys);
		idx++;

		ioapic_res->start = ioapic_phys;
		ioapic_res->end = ioapic_phys + IO_APIC_SLOT_SIZE - 1;
		ioapic_res++;
	}
}

void __init ioapic_insert_resources(void)
{
	int i;
	struct resource *r = ioapic_resources;

	if (!r) {
		if (nr_ioapics > 0)
			printk(KERN_ERR
				"IO APIC resources couldn't be allocated.\n");
		return;
	}

	for (i = 0; i < nr_ioapics; i++) {
		insert_resource(&iomem_resource, r);
		r++;
	}
}

int mp_find_ioapic(int gsi)
{
	int i = 0;

	/* Find the IOAPIC that manages this GSI. */
	for (i = 0; i < nr_ioapics; i++) {
		if ((gsi >= mp_gsi_routing[i].gsi_base)
		    && (gsi <= mp_gsi_routing[i].gsi_end))
			return i;
	}

	printk(KERN_ERR "ERROR: Unable to locate IOAPIC for GSI %d\n", gsi);
	return -1;
}

int mp_find_ioapic_pin(int ioapic, int gsi)
{
	if (WARN_ON(ioapic == -1))
		return -1;
	if (WARN_ON(gsi > mp_gsi_routing[ioapic].gsi_end))
		return -1;

	return gsi - mp_gsi_routing[ioapic].gsi_base;
}

static int bad_ioapic(unsigned long address)
{
	if (nr_ioapics >= MAX_IO_APICS) {
		printk(KERN_WARNING "WARING: Max # of I/O APICs (%d) exceeded "
		       "(found %d), skipping\n", MAX_IO_APICS, nr_ioapics);
		return 1;
	}
	if (!address) {
		printk(KERN_WARNING "WARNING: Bogus (zero) I/O APIC address"
		       " found in table, skipping!\n");
		return 1;
	}
	return 0;
}

void __init mp_register_ioapic(int id, u32 address, u32 gsi_base)
{
	int idx = 0;

	if (bad_ioapic(address))
		return;

	idx = nr_ioapics;

	mp_ioapics[idx].type = MP_IOAPIC;
	mp_ioapics[idx].flags = MPC_APIC_USABLE;
	mp_ioapics[idx].apicaddr = address;

	set_fixmap_nocache(FIX_IO_APIC_BASE_0 + idx, address);
	mp_ioapics[idx].apicid = io_apic_unique_id(id);
	mp_ioapics[idx].apicver = io_apic_get_version(idx);

	/*
	 * Build basic GSI lookup table to facilitate gsi->io_apic lookups
	 * and to prevent reprogramming of IOAPIC pins (PCI GSIs).
	 */
	mp_gsi_routing[idx].gsi_base = gsi_base;
	mp_gsi_routing[idx].gsi_end = gsi_base +
	    io_apic_get_redir_entries(idx);

	printk(KERN_INFO "IOAPIC[%d]: apic_id %d, version %d, address 0x%x, "
	       "GSI %d-%d\n", idx, mp_ioapics[idx].apicid,
	       mp_ioapics[idx].apicver, mp_ioapics[idx].apicaddr,
	       mp_gsi_routing[idx].gsi_base, mp_gsi_routing[idx].gsi_end);

	nr_ioapics++;
}
