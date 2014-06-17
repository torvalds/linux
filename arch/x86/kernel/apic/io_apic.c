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
#include <linux/syscore_ops.h>
#include <linux/msi.h>
#include <linux/htirq.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>	/* time_after() */
#include <linux/slab.h>
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

static struct ioapic {
	/*
	 * # of IRQ routing registers
	 */
	int nr_registers;
	/*
	 * Saved state during suspend/resume, or while enabling intr-remap.
	 */
	struct IO_APIC_route_entry *saved_registers;
	/* I/O APIC config */
	struct mpc_ioapic mp_config;
	/* IO APIC gsi routing info */
	struct mp_ioapic_gsi  gsi_config;
	DECLARE_BITMAP(pin_programmed, MP_MAX_IOAPIC_PIN + 1);
} ioapics[MAX_IO_APICS];

#define mpc_ioapic_ver(ioapic_idx)	ioapics[ioapic_idx].mp_config.apicver

int mpc_ioapic_id(int ioapic_idx)
{
	return ioapics[ioapic_idx].mp_config.apicid;
}

unsigned int mpc_ioapic_addr(int ioapic_idx)
{
	return ioapics[ioapic_idx].mp_config.apicaddr;
}

struct mp_ioapic_gsi *mp_ioapic_gsi_routing(int ioapic_idx)
{
	return &ioapics[ioapic_idx].gsi_config;
}

int nr_ioapics;

/* The one past the highest gsi number used */
u32 gsi_top;

/* MP IRQ source entries */
struct mpc_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* # of MP IRQ source entries */
int mp_irq_entries;

/* GSI interrupts */
static int nr_irqs_gsi = NR_IRQS_LEGACY;

#ifdef CONFIG_EISA
int mp_bus_id_to_type[MAX_MP_BUSSES];
#endif

DECLARE_BITMAP(mp_bus_not_pci, MAX_MP_BUSSES);

int skip_ioapic_setup;

/**
 * disable_ioapic_support() - disables ioapic support at runtime
 */
void disable_ioapic_support(void)
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
	disable_ioapic_support();
	return 0;
}
early_param("noapic", parse_noapic);

static int io_apic_setup_irq_pin(unsigned int irq, int node,
				 struct io_apic_irq_attr *attr);

/* Will be called in mpparse/acpi/sfi codes for saving IRQ info */
void mp_save_irq(struct mpc_intsrc *m)
{
	int i;

	apic_printk(APIC_VERBOSE, "Int: type %d, pol %d, trig %d, bus %02x,"
		" IRQ %02x, APIC ID %x, APIC INT %02x\n",
		m->irqtype, m->irqflag & 3, (m->irqflag >> 2) & 3, m->srcbus,
		m->srcbusirq, m->dstapic, m->dstirq);

	for (i = 0; i < mp_irq_entries; i++) {
		if (!memcmp(&mp_irqs[i], m, sizeof(*m)))
			return;
	}

	memcpy(&mp_irqs[mp_irq_entries], m, sizeof(*m));
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!!\n");
}

struct irq_pin_list {
	int apic, pin;
	struct irq_pin_list *next;
};

static struct irq_pin_list *alloc_irq_pin_list(int node)
{
	return kzalloc_node(sizeof(struct irq_pin_list), GFP_KERNEL, node);
}


/* irq_cfg is indexed by the sum of all RTEs in all I/O APICs. */
static struct irq_cfg irq_cfgx[NR_IRQS_LEGACY];

int __init arch_early_irq_init(void)
{
	struct irq_cfg *cfg;
	int count, node, i;

	if (!legacy_pic->nr_legacy_irqs)
		io_apic_irqs = ~0UL;

	for (i = 0; i < nr_ioapics; i++) {
		ioapics[i].saved_registers =
			kzalloc(sizeof(struct IO_APIC_route_entry) *
				ioapics[i].nr_registers, GFP_KERNEL);
		if (!ioapics[i].saved_registers)
			pr_err("IOAPIC %d: suspend/resume impossible!\n", i);
	}

	cfg = irq_cfgx;
	count = ARRAY_SIZE(irq_cfgx);
	node = cpu_to_node(0);

	/* Make sure the legacy interrupts are marked in the bitmap */
	irq_reserve_irqs(0, legacy_pic->nr_legacy_irqs);

	for (i = 0; i < count; i++) {
		irq_set_chip_data(i, &cfg[i]);
		zalloc_cpumask_var_node(&cfg[i].domain, GFP_KERNEL, node);
		zalloc_cpumask_var_node(&cfg[i].old_domain, GFP_KERNEL, node);
		/*
		 * For legacy IRQ's, start with assigning irq0 to irq15 to
		 * IRQ0_VECTOR to IRQ15_VECTOR for all cpu's.
		 */
		if (i < legacy_pic->nr_legacy_irqs) {
			cfg[i].vector = IRQ0_VECTOR + i;
			cpumask_setall(cfg[i].domain);
		}
	}

	return 0;
}

static struct irq_cfg *irq_cfg(unsigned int irq)
{
	return irq_get_chip_data(irq);
}

static struct irq_cfg *alloc_irq_cfg(unsigned int irq, int node)
{
	struct irq_cfg *cfg;

	cfg = kzalloc_node(sizeof(*cfg), GFP_KERNEL, node);
	if (!cfg)
		return NULL;
	if (!zalloc_cpumask_var_node(&cfg->domain, GFP_KERNEL, node))
		goto out_cfg;
	if (!zalloc_cpumask_var_node(&cfg->old_domain, GFP_KERNEL, node))
		goto out_domain;
	return cfg;
out_domain:
	free_cpumask_var(cfg->domain);
out_cfg:
	kfree(cfg);
	return NULL;
}

static void free_irq_cfg(unsigned int at, struct irq_cfg *cfg)
{
	if (!cfg)
		return;
	irq_set_chip_data(at, NULL);
	free_cpumask_var(cfg->domain);
	free_cpumask_var(cfg->old_domain);
	kfree(cfg);
}

static struct irq_cfg *alloc_irq_and_cfg_at(unsigned int at, int node)
{
	int res = irq_alloc_desc_at(at, node);
	struct irq_cfg *cfg;

	if (res < 0) {
		if (res != -EEXIST)
			return NULL;
		cfg = irq_get_chip_data(at);
		if (cfg)
			return cfg;
	}

	cfg = alloc_irq_cfg(at, node);
	if (cfg)
		irq_set_chip_data(at, cfg);
	else
		irq_free_desc(at);
	return cfg;
}

static int alloc_irqs_from(unsigned int from, unsigned int count, int node)
{
	return irq_alloc_descs_from(from, count, node);
}

static void free_irq_at(unsigned int at, struct irq_cfg *cfg)
{
	free_irq_cfg(at, cfg);
	irq_free_desc(at);
}


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
		+ (mpc_ioapic_addr(idx) & ~PAGE_MASK);
}

void io_apic_eoi(unsigned int apic, unsigned int vector)
{
	struct io_apic __iomem *io_apic = io_apic_base(apic);
	writel(vector, &io_apic->eoi);
}

unsigned int native_io_apic_read(unsigned int apic, unsigned int reg)
{
	struct io_apic __iomem *io_apic = io_apic_base(apic);
	writel(reg, &io_apic->index);
	return readl(&io_apic->data);
}

void native_io_apic_write(unsigned int apic, unsigned int reg, unsigned int value)
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
void native_io_apic_modify(unsigned int apic, unsigned int reg, unsigned int value)
{
	struct io_apic __iomem *io_apic = io_apic_base(apic);

	if (sis_apic_bug)
		writel(reg, &io_apic->index);
	writel(value, &io_apic->data);
}

union entry_union {
	struct { u32 w1, w2; };
	struct IO_APIC_route_entry entry;
};

static struct IO_APIC_route_entry __ioapic_read_entry(int apic, int pin)
{
	union entry_union eu;

	eu.w1 = io_apic_read(apic, 0x10 + 2 * pin);
	eu.w2 = io_apic_read(apic, 0x11 + 2 * pin);

	return eu.entry;
}

static struct IO_APIC_route_entry ioapic_read_entry(int apic, int pin)
{
	union entry_union eu;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	eu.entry = __ioapic_read_entry(apic, pin);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return eu.entry;
}

/*
 * When we write a new IO APIC routing entry, we need to write the high
 * word first! If the mask bit in the low word is clear, we will enable
 * the interrupt, and we need to make sure the entry is fully populated
 * before that happens.
 */
static void __ioapic_write_entry(int apic, int pin, struct IO_APIC_route_entry e)
{
	union entry_union eu = {{0, 0}};

	eu.entry = e;
	io_apic_write(apic, 0x11 + 2*pin, eu.w2);
	io_apic_write(apic, 0x10 + 2*pin, eu.w1);
}

static void ioapic_write_entry(int apic, int pin, struct IO_APIC_route_entry e)
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
static int __add_pin_to_irq_node(struct irq_cfg *cfg, int node, int apic, int pin)
{
	struct irq_pin_list **last, *entry;

	/* don't allow duplicates */
	last = &cfg->irq_2_pin;
	for_each_irq_pin(entry, cfg->irq_2_pin) {
		if (entry->apic == apic && entry->pin == pin)
			return 0;
		last = &entry->next;
	}

	entry = alloc_irq_pin_list(node);
	if (!entry) {
		pr_err("can not alloc irq_pin_list (%d,%d,%d)\n",
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
	if (__add_pin_to_irq_node(cfg, node, apic, pin))
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

static void mask_ioapic(struct irq_cfg *cfg)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_modify_irq(cfg, ~0, IO_APIC_REDIR_MASKED, &io_apic_sync);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void mask_ioapic_irq(struct irq_data *data)
{
	mask_ioapic(data->chip_data);
}

static void __unmask_ioapic(struct irq_cfg *cfg)
{
	io_apic_modify_irq(cfg, ~IO_APIC_REDIR_MASKED, 0, NULL);
}

static void unmask_ioapic(struct irq_cfg *cfg)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	__unmask_ioapic(cfg);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void unmask_ioapic_irq(struct irq_data *data)
{
	unmask_ioapic(data->chip_data);
}

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
void native_eoi_ioapic_pin(int apic, int pin, int vector)
{
	if (mpc_ioapic_ver(apic) >= 0x20) {
		io_apic_eoi(apic, vector);
	} else {
		struct IO_APIC_route_entry entry, entry1;

		entry = entry1 = __ioapic_read_entry(apic, pin);

		/*
		 * Mask the entry and change the trigger mode to edge.
		 */
		entry1.mask = 1;
		entry1.trigger = IOAPIC_EDGE;

		__ioapic_write_entry(apic, pin, entry1);

		/*
		 * Restore the previous level triggered entry.
		 */
		__ioapic_write_entry(apic, pin, entry);
	}
}

void eoi_ioapic_irq(unsigned int irq, struct irq_cfg *cfg)
{
	struct irq_pin_list *entry;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	for_each_irq_pin(entry, cfg->irq_2_pin)
		x86_io_apic_ops.eoi_ioapic_pin(entry->apic, entry->pin,
					       cfg->vector);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void clear_IO_APIC_pin(unsigned int apic, unsigned int pin)
{
	struct IO_APIC_route_entry entry;

	/* Check delivery_mode to be sure we're not clearing an SMI pin */
	entry = ioapic_read_entry(apic, pin);
	if (entry.delivery_mode == dest_SMI)
		return;

	/*
	 * Make sure the entry is masked and re-read the contents to check
	 * if it is a level triggered pin and if the remote-IRR is set.
	 */
	if (!entry.mask) {
		entry.mask = 1;
		ioapic_write_entry(apic, pin, entry);
		entry = ioapic_read_entry(apic, pin);
	}

	if (entry.irr) {
		unsigned long flags;

		/*
		 * Make sure the trigger mode is set to level. Explicit EOI
		 * doesn't clear the remote-IRR if the trigger mode is not
		 * set to level.
		 */
		if (!entry.trigger) {
			entry.trigger = IOAPIC_LEVEL;
			ioapic_write_entry(apic, pin, entry);
		}

		raw_spin_lock_irqsave(&ioapic_lock, flags);
		x86_io_apic_ops.eoi_ioapic_pin(apic, pin, entry.vector);
		raw_spin_unlock_irqrestore(&ioapic_lock, flags);
	}

	/*
	 * Clear the rest of the bits in the IO-APIC RTE except for the mask
	 * bit.
	 */
	ioapic_mask_entry(apic, pin);
	entry = ioapic_read_entry(apic, pin);
	if (entry.irr)
		pr_err("Unable to reset IRR for apic: %d, pin :%d\n",
		       mpc_ioapic_id(apic), pin);
}

static void clear_IO_APIC (void)
{
	int apic, pin;

	for (apic = 0; apic < nr_ioapics; apic++)
		for (pin = 0; pin < ioapics[apic].nr_registers; pin++)
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

/*
 * Saves all the IO-APIC RTE's
 */
int save_ioapic_entries(void)
{
	int apic, pin;
	int err = 0;

	for (apic = 0; apic < nr_ioapics; apic++) {
		if (!ioapics[apic].saved_registers) {
			err = -ENOMEM;
			continue;
		}

		for (pin = 0; pin < ioapics[apic].nr_registers; pin++)
			ioapics[apic].saved_registers[pin] =
				ioapic_read_entry(apic, pin);
	}

	return err;
}

/*
 * Mask all IO APIC entries.
 */
void mask_ioapic_entries(void)
{
	int apic, pin;

	for (apic = 0; apic < nr_ioapics; apic++) {
		if (!ioapics[apic].saved_registers)
			continue;

		for (pin = 0; pin < ioapics[apic].nr_registers; pin++) {
			struct IO_APIC_route_entry entry;

			entry = ioapics[apic].saved_registers[pin];
			if (!entry.mask) {
				entry.mask = 1;
				ioapic_write_entry(apic, pin, entry);
			}
		}
	}
}

/*
 * Restore IO APIC entries which was saved in the ioapic structure.
 */
int restore_ioapic_entries(void)
{
	int apic, pin;

	for (apic = 0; apic < nr_ioapics; apic++) {
		if (!ioapics[apic].saved_registers)
			continue;

		for (pin = 0; pin < ioapics[apic].nr_registers; pin++)
			ioapic_write_entry(apic, pin,
					   ioapics[apic].saved_registers[pin]);
	}
	return 0;
}

/*
 * Find the IRQ entry number of a certain pin.
 */
static int find_irq_entry(int ioapic_idx, int pin, int type)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++)
		if (mp_irqs[i].irqtype == type &&
		    (mp_irqs[i].dstapic == mpc_ioapic_id(ioapic_idx) ||
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
		int ioapic_idx;

		for (ioapic_idx = 0; ioapic_idx < nr_ioapics; ioapic_idx++)
			if (mpc_ioapic_id(ioapic_idx) == mp_irqs[i].dstapic)
				return ioapic_idx;
	}

	return -1;
}

#ifdef CONFIG_EISA
/*
 * EISA Edge/Level control register, ELCR
 */
static int EISA_ELCR(unsigned int irq)
{
	if (irq < legacy_pic->nr_legacy_irqs) {
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

static int irq_polarity(int idx)
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
			pr_warn("broken BIOS!!\n");
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
			pr_warn("broken BIOS!!\n");
			polarity = 1;
			break;
		}
	}
	return polarity;
}

static int irq_trigger(int idx)
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
#ifdef CONFIG_EISA
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
				default:
				{
					pr_warn("broken BIOS!!\n");
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
			pr_warn("broken BIOS!!\n");
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
			pr_warn("broken BIOS!!\n");
			trigger = 0;
			break;
		}
	}
	return trigger;
}

static int pin_2_irq(int idx, int apic, int pin)
{
	int irq;
	int bus = mp_irqs[idx].srcbus;
	struct mp_ioapic_gsi *gsi_cfg = mp_ioapic_gsi_routing(apic);

	/*
	 * Debugging check, we are in big trouble if this message pops up!
	 */
	if (mp_irqs[idx].dstirq != pin)
		pr_err("broken BIOS or MPTABLE parser, ayiee!!\n");

	if (test_bit(bus, mp_bus_not_pci)) {
		irq = mp_irqs[idx].srcbusirq;
	} else {
		u32 gsi = gsi_cfg->gsi_base + pin;

		if (gsi >= NR_IRQS_LEGACY)
			irq = gsi;
		else
			irq = gsi_top + gsi;
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
	int ioapic_idx, i, best_guess = -1;

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

		for (ioapic_idx = 0; ioapic_idx < nr_ioapics; ioapic_idx++)
			if (mpc_ioapic_id(ioapic_idx) == mp_irqs[i].dstapic ||
			    mp_irqs[i].dstapic == MP_APIC_ALL)
				break;

		if (!test_bit(lbus, mp_bus_not_pci) &&
		    !mp_irqs[i].irqtype &&
		    (bus == lbus) &&
		    (slot == ((mp_irqs[i].srcbusirq >> 2) & 0x1f))) {
			int irq = pin_2_irq(i, ioapic_idx, mp_irqs[i].dstirq);

			if (!(ioapic_idx || IO_APIC_IRQ(irq)))
				continue;

			if (pin == (mp_irqs[i].srcbusirq & 3)) {
				set_io_apic_irq_attr(irq_attr, ioapic_idx,
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
				set_io_apic_irq_attr(irq_attr, ioapic_idx,
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
	static int current_offset = VECTOR_OFFSET_START % 16;
	int cpu, err;
	cpumask_var_t tmp_mask;

	if (cfg->move_in_progress)
		return -EBUSY;

	if (!alloc_cpumask_var(&tmp_mask, GFP_ATOMIC))
		return -ENOMEM;

	/* Only try and allocate irqs on cpus that are present */
	err = -ENOSPC;
	cpumask_clear(cfg->old_domain);
	cpu = cpumask_first_and(mask, cpu_online_mask);
	while (cpu < nr_cpu_ids) {
		int new_cpu, vector, offset;

		apic->vector_allocation_domain(cpu, tmp_mask, mask);

		if (cpumask_subset(tmp_mask, cfg->domain)) {
			err = 0;
			if (cpumask_equal(tmp_mask, cfg->domain))
				break;
			/*
			 * New cpumask using the vector is a proper subset of
			 * the current in use mask. So cleanup the vector
			 * allocation for the members that are not used anymore.
			 */
			cpumask_andnot(cfg->old_domain, cfg->domain, tmp_mask);
			cfg->move_in_progress =
			   cpumask_intersects(cfg->old_domain, cpu_online_mask);
			cpumask_and(cfg->domain, cfg->domain, tmp_mask);
			break;
		}

		vector = current_vector;
		offset = current_offset;
next:
		vector += 16;
		if (vector >= first_system_vector) {
			offset = (offset + 1) % 16;
			vector = FIRST_EXTERNAL_VECTOR + offset;
		}

		if (unlikely(current_vector == vector)) {
			cpumask_or(cfg->old_domain, cfg->old_domain, tmp_mask);
			cpumask_andnot(tmp_mask, mask, cfg->old_domain);
			cpu = cpumask_first_and(tmp_mask, cpu_online_mask);
			continue;
		}

		if (test_bit(vector, used_vectors))
			goto next;

		for_each_cpu_and(new_cpu, tmp_mask, cpu_online_mask) {
			if (per_cpu(vector_irq, new_cpu)[vector] > VECTOR_UNDEFINED)
				goto next;
		}
		/* Found one! */
		current_vector = vector;
		current_offset = offset;
		if (cfg->vector) {
			cpumask_copy(cfg->old_domain, cfg->domain);
			cfg->move_in_progress =
			   cpumask_intersects(cfg->old_domain, cpu_online_mask);
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
		per_cpu(vector_irq, cpu)[vector] = VECTOR_UNDEFINED;

	cfg->vector = 0;
	cpumask_clear(cfg->domain);

	if (likely(!cfg->move_in_progress))
		return;
	for_each_cpu_and(cpu, cfg->old_domain, cpu_online_mask) {
		for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS; vector++) {
			if (per_cpu(vector_irq, cpu)[vector] != irq)
				continue;
			per_cpu(vector_irq, cpu)[vector] = VECTOR_UNDEFINED;
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

	/*
	 * vector_lock will make sure that we don't run into irq vector
	 * assignments that might be happening on another cpu in parallel,
	 * while we setup our initial vector to irq mappings.
	 */
	raw_spin_lock(&vector_lock);
	/* Mark the inuse vectors */
	for_each_active_irq(irq) {
		cfg = irq_get_chip_data(irq);
		if (!cfg)
			continue;

		if (!cpumask_test_cpu(cpu, cfg->domain))
			continue;
		vector = cfg->vector;
		per_cpu(vector_irq, cpu)[vector] = irq;
	}
	/* Mark the free vectors */
	for (vector = 0; vector < NR_VECTORS; ++vector) {
		irq = per_cpu(vector_irq, cpu)[vector];
		if (irq <= VECTOR_UNDEFINED)
			continue;

		cfg = irq_cfg(irq);
		if (!cpumask_test_cpu(cpu, cfg->domain))
			per_cpu(vector_irq, cpu)[vector] = VECTOR_UNDEFINED;
	}
	raw_spin_unlock(&vector_lock);
}

static struct irq_chip ioapic_chip;

#ifdef CONFIG_X86_32
static inline int IO_APIC_irq_trigger(int irq)
{
	int apic, idx, pin;

	for (apic = 0; apic < nr_ioapics; apic++) {
		for (pin = 0; pin < ioapics[apic].nr_registers; pin++) {
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

static void ioapic_register_intr(unsigned int irq, struct irq_cfg *cfg,
				 unsigned long trigger)
{
	struct irq_chip *chip = &ioapic_chip;
	irq_flow_handler_t hdl;
	bool fasteoi;

	if ((trigger == IOAPIC_AUTO && IO_APIC_irq_trigger(irq)) ||
	    trigger == IOAPIC_LEVEL) {
		irq_set_status_flags(irq, IRQ_LEVEL);
		fasteoi = true;
	} else {
		irq_clear_status_flags(irq, IRQ_LEVEL);
		fasteoi = false;
	}

	if (setup_remapped_irq(irq, cfg, chip))
		fasteoi = trigger != 0;

	hdl = fasteoi ? handle_fasteoi_irq : handle_edge_irq;
	irq_set_chip_and_handler_name(irq, chip, hdl,
				      fasteoi ? "fasteoi" : "edge");
}

int native_setup_ioapic_entry(int irq, struct IO_APIC_route_entry *entry,
			      unsigned int destination, int vector,
			      struct io_apic_irq_attr *attr)
{
	memset(entry, 0, sizeof(*entry));

	entry->delivery_mode = apic->irq_delivery_mode;
	entry->dest_mode     = apic->irq_dest_mode;
	entry->dest	     = destination;
	entry->vector	     = vector;
	entry->mask	     = 0;			/* enable IRQ */
	entry->trigger	     = attr->trigger;
	entry->polarity	     = attr->polarity;

	/*
	 * Mask level triggered irqs.
	 * Use IRQ_DELAYED_DISABLE for edge triggered irqs.
	 */
	if (attr->trigger)
		entry->mask = 1;

	return 0;
}

static void setup_ioapic_irq(unsigned int irq, struct irq_cfg *cfg,
				struct io_apic_irq_attr *attr)
{
	struct IO_APIC_route_entry entry;
	unsigned int dest;

	if (!IO_APIC_IRQ(irq))
		return;

	if (assign_irq_vector(irq, cfg, apic->target_cpus()))
		return;

	if (apic->cpu_mask_to_apicid_and(cfg->domain, apic->target_cpus(),
					 &dest)) {
		pr_warn("Failed to obtain apicid for ioapic %d, pin %d\n",
			mpc_ioapic_id(attr->ioapic), attr->ioapic_pin);
		__clear_irq_vector(irq, cfg);

		return;
	}

	apic_printk(APIC_VERBOSE,KERN_DEBUG
		    "IOAPIC[%d]: Set routing entry (%d-%d -> 0x%x -> "
		    "IRQ %d Mode:%i Active:%i Dest:%d)\n",
		    attr->ioapic, mpc_ioapic_id(attr->ioapic), attr->ioapic_pin,
		    cfg->vector, irq, attr->trigger, attr->polarity, dest);

	if (x86_io_apic_ops.setup_entry(irq, &entry, dest, cfg->vector, attr)) {
		pr_warn("Failed to setup ioapic entry for ioapic  %d, pin %d\n",
			mpc_ioapic_id(attr->ioapic), attr->ioapic_pin);
		__clear_irq_vector(irq, cfg);

		return;
	}

	ioapic_register_intr(irq, cfg, attr->trigger);
	if (irq < legacy_pic->nr_legacy_irqs)
		legacy_pic->mask(irq);

	ioapic_write_entry(attr->ioapic, attr->ioapic_pin, entry);
}

static bool __init io_apic_pin_not_connected(int idx, int ioapic_idx, int pin)
{
	if (idx != -1)
		return false;

	apic_printk(APIC_VERBOSE, KERN_DEBUG " apic %d pin %d not connected\n",
		    mpc_ioapic_id(ioapic_idx), pin);
	return true;
}

static void __init __io_apic_setup_irqs(unsigned int ioapic_idx)
{
	int idx, node = cpu_to_node(0);
	struct io_apic_irq_attr attr;
	unsigned int pin, irq;

	for (pin = 0; pin < ioapics[ioapic_idx].nr_registers; pin++) {
		idx = find_irq_entry(ioapic_idx, pin, mp_INT);
		if (io_apic_pin_not_connected(idx, ioapic_idx, pin))
			continue;

		irq = pin_2_irq(idx, ioapic_idx, pin);

		if ((ioapic_idx > 0) && (irq > 16))
			continue;

		/*
		 * Skip the timer IRQ if there's a quirk handler
		 * installed and if it returns 1:
		 */
		if (apic->multi_timer_check &&
		    apic->multi_timer_check(ioapic_idx, irq))
			continue;

		set_io_apic_irq_attr(&attr, ioapic_idx, pin, irq_trigger(idx),
				     irq_polarity(idx));

		io_apic_setup_irq_pin(irq, node, &attr);
	}
}

static void __init setup_IO_APIC_irqs(void)
{
	unsigned int ioapic_idx;

	apic_printk(APIC_VERBOSE, KERN_DEBUG "init IO_APIC IRQs\n");

	for (ioapic_idx = 0; ioapic_idx < nr_ioapics; ioapic_idx++)
		__io_apic_setup_irqs(ioapic_idx);
}

/*
 * for the gsit that is not in first ioapic
 * but could not use acpi_register_gsi()
 * like some special sci in IBM x3330
 */
void setup_IO_APIC_irq_extra(u32 gsi)
{
	int ioapic_idx = 0, pin, idx, irq, node = cpu_to_node(0);
	struct io_apic_irq_attr attr;

	/*
	 * Convert 'gsi' to 'ioapic.pin'.
	 */
	ioapic_idx = mp_find_ioapic(gsi);
	if (ioapic_idx < 0)
		return;

	pin = mp_find_ioapic_pin(ioapic_idx, gsi);
	idx = find_irq_entry(ioapic_idx, pin, mp_INT);
	if (idx == -1)
		return;

	irq = pin_2_irq(idx, ioapic_idx, pin);

	/* Only handle the non legacy irqs on secondary ioapics */
	if (ioapic_idx == 0 || irq < NR_IRQS_LEGACY)
		return;

	set_io_apic_irq_attr(&attr, ioapic_idx, pin, irq_trigger(idx),
			     irq_polarity(idx));

	io_apic_setup_irq_pin_once(irq, node, &attr);
}

/*
 * Set up the timer pin, possibly with the 8259A-master behind.
 */
static void __init setup_timer_IRQ0_pin(unsigned int ioapic_idx,
					unsigned int pin, int vector)
{
	struct IO_APIC_route_entry entry;
	unsigned int dest;

	memset(&entry, 0, sizeof(entry));

	/*
	 * We use logical delivery to get the timer IRQ
	 * to the first CPU.
	 */
	if (unlikely(apic->cpu_mask_to_apicid_and(apic->target_cpus(),
						  apic->target_cpus(), &dest)))
		dest = BAD_APICID;

	entry.dest_mode = apic->irq_dest_mode;
	entry.mask = 0;			/* don't mask IRQ for edge */
	entry.dest = dest;
	entry.delivery_mode = apic->irq_delivery_mode;
	entry.polarity = 0;
	entry.trigger = 0;
	entry.vector = vector;

	/*
	 * The timer IRQ doesn't have to know that behind the
	 * scene we may have a 8259A-master in AEOI mode ...
	 */
	irq_set_chip_and_handler_name(0, &ioapic_chip, handle_edge_irq,
				      "edge");

	/*
	 * Add it to the IO-APIC irq-routing table:
	 */
	ioapic_write_entry(ioapic_idx, pin, entry);
}

void native_io_apic_print_entries(unsigned int apic, unsigned int nr_entries)
{
	int i;

	pr_debug(" NR Dst Mask Trig IRR Pol Stat Dmod Deli Vect:\n");

	for (i = 0; i <= nr_entries; i++) {
		struct IO_APIC_route_entry entry;

		entry = ioapic_read_entry(apic, i);

		pr_debug(" %02x %02X  ", i, entry.dest);
		pr_cont("%1d    %1d    %1d   %1d   %1d    "
			"%1d    %1d    %02X\n",
			entry.mask,
			entry.trigger,
			entry.irr,
			entry.polarity,
			entry.delivery_status,
			entry.dest_mode,
			entry.delivery_mode,
			entry.vector);
	}
}

void intel_ir_io_apic_print_entries(unsigned int apic,
				    unsigned int nr_entries)
{
	int i;

	pr_debug(" NR Indx Fmt Mask Trig IRR Pol Stat Indx2 Zero Vect:\n");

	for (i = 0; i <= nr_entries; i++) {
		struct IR_IO_APIC_route_entry *ir_entry;
		struct IO_APIC_route_entry entry;

		entry = ioapic_read_entry(apic, i);

		ir_entry = (struct IR_IO_APIC_route_entry *)&entry;

		pr_debug(" %02x %04X ", i, ir_entry->index);
		pr_cont("%1d   %1d    %1d    %1d   %1d   "
			"%1d    %1d     %X    %02X\n",
			ir_entry->format,
			ir_entry->mask,
			ir_entry->trigger,
			ir_entry->irr,
			ir_entry->polarity,
			ir_entry->delivery_status,
			ir_entry->index2,
			ir_entry->zero,
			ir_entry->vector);
	}
}

void ioapic_zap_locks(void)
{
	raw_spin_lock_init(&ioapic_lock);
}

__apicdebuginit(void) print_IO_APIC(int ioapic_idx)
{
	union IO_APIC_reg_00 reg_00;
	union IO_APIC_reg_01 reg_01;
	union IO_APIC_reg_02 reg_02;
	union IO_APIC_reg_03 reg_03;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_00.raw = io_apic_read(ioapic_idx, 0);
	reg_01.raw = io_apic_read(ioapic_idx, 1);
	if (reg_01.bits.version >= 0x10)
		reg_02.raw = io_apic_read(ioapic_idx, 2);
	if (reg_01.bits.version >= 0x20)
		reg_03.raw = io_apic_read(ioapic_idx, 3);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	printk(KERN_DEBUG "IO APIC #%d......\n", mpc_ioapic_id(ioapic_idx));
	printk(KERN_DEBUG ".... register #00: %08X\n", reg_00.raw);
	printk(KERN_DEBUG ".......    : physical APIC id: %02X\n", reg_00.bits.ID);
	printk(KERN_DEBUG ".......    : Delivery Type: %X\n", reg_00.bits.delivery_type);
	printk(KERN_DEBUG ".......    : LTS          : %X\n", reg_00.bits.LTS);

	printk(KERN_DEBUG ".... register #01: %08X\n", *(int *)&reg_01);
	printk(KERN_DEBUG ".......     : max redirection entries: %02X\n",
		reg_01.bits.entries);

	printk(KERN_DEBUG ".......     : PRQ implemented: %X\n", reg_01.bits.PRQ);
	printk(KERN_DEBUG ".......     : IO APIC version: %02X\n",
		reg_01.bits.version);

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

	x86_io_apic_ops.print_entries(ioapic_idx, reg_01.bits.entries);
}

__apicdebuginit(void) print_IO_APICs(void)
{
	int ioapic_idx;
	struct irq_cfg *cfg;
	unsigned int irq;
	struct irq_chip *chip;

	printk(KERN_DEBUG "number of MP IRQ sources: %d.\n", mp_irq_entries);
	for (ioapic_idx = 0; ioapic_idx < nr_ioapics; ioapic_idx++)
		printk(KERN_DEBUG "number of IO-APIC #%d registers: %d.\n",
		       mpc_ioapic_id(ioapic_idx),
		       ioapics[ioapic_idx].nr_registers);

	/*
	 * We are a bit conservative about what we expect.  We have to
	 * know about every hardware change ASAP.
	 */
	printk(KERN_INFO "testing the IO APIC.......................\n");

	for (ioapic_idx = 0; ioapic_idx < nr_ioapics; ioapic_idx++)
		print_IO_APIC(ioapic_idx);

	printk(KERN_DEBUG "IRQ to pin mappings:\n");
	for_each_active_irq(irq) {
		struct irq_pin_list *entry;

		chip = irq_get_chip(irq);
		if (chip != &ioapic_chip)
			continue;

		cfg = irq_get_chip_data(irq);
		if (!cfg)
			continue;
		entry = cfg->irq_2_pin;
		if (!entry)
			continue;
		printk(KERN_DEBUG "IRQ%d ", irq);
		for_each_irq_pin(entry, cfg->irq_2_pin)
			pr_cont("-> %d:%d", entry->apic, entry->pin);
		pr_cont("\n");
	}

	printk(KERN_INFO ".................................... done.\n");
}

__apicdebuginit(void) print_APIC_field(int base)
{
	int i;

	printk(KERN_DEBUG);

	for (i = 0; i < 8; i++)
		pr_cont("%08x", apic_read(base + i*0x10));

	pr_cont("\n");
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
	pr_cont("\n");
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

	if (!legacy_pic->nr_legacy_irqs)
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
	print_IO_APICs();

	return 0;
}

late_initcall(print_ICs);


/* Where if anywhere is the i8259 connect in external int mode */
static struct { int pin, apic; } ioapic_i8259 = { -1, -1 };

void __init enable_IO_APIC(void)
{
	int i8259_apic, i8259_pin;
	int apic;

	if (!legacy_pic->nr_legacy_irqs)
		return;

	for(apic = 0; apic < nr_ioapics; apic++) {
		int pin;
		/* See if any of the pins is in ExtINT mode */
		for (pin = 0; pin < ioapics[apic].nr_registers; pin++) {
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

void native_disable_io_apic(void)
{
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
		entry.dest            = read_apic_id();

		/*
		 * Add it to the IO-APIC irq-routing table:
		 */
		ioapic_write_entry(ioapic_i8259.apic, ioapic_i8259.pin, entry);
	}

	if (cpu_has_apic || apic_from_smp_config())
		disconnect_bsp_APIC(ioapic_i8259.pin != -1);

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

	if (!legacy_pic->nr_legacy_irqs)
		return;

	x86_io_apic_ops.disable();
}

#ifdef CONFIG_X86_32
/*
 * function to set the IO-APIC physical IDs based on the
 * values stored in the MPC table.
 *
 * by Matt Domsch <Matt_Domsch@dell.com>  Tue Dec 21 12:25:05 CST 1999
 */
void __init setup_ioapic_ids_from_mpc_nocheck(void)
{
	union IO_APIC_reg_00 reg_00;
	physid_mask_t phys_id_present_map;
	int ioapic_idx;
	int i;
	unsigned char old_id;
	unsigned long flags;

	/*
	 * This is broken; anything with a real cpu count has to
	 * circumvent this idiocy regardless.
	 */
	apic->ioapic_phys_id_map(&phys_cpu_present_map, &phys_id_present_map);

	/*
	 * Set the IOAPIC ID to the value stored in the MPC table.
	 */
	for (ioapic_idx = 0; ioapic_idx < nr_ioapics; ioapic_idx++) {
		/* Read the register 0 value */
		raw_spin_lock_irqsave(&ioapic_lock, flags);
		reg_00.raw = io_apic_read(ioapic_idx, 0);
		raw_spin_unlock_irqrestore(&ioapic_lock, flags);

		old_id = mpc_ioapic_id(ioapic_idx);

		if (mpc_ioapic_id(ioapic_idx) >= get_physical_broadcast()) {
			printk(KERN_ERR "BIOS bug, IO-APIC#%d ID is %d in the MPC table!...\n",
				ioapic_idx, mpc_ioapic_id(ioapic_idx));
			printk(KERN_ERR "... fixing up to %d. (tell your hw vendor)\n",
				reg_00.bits.ID);
			ioapics[ioapic_idx].mp_config.apicid = reg_00.bits.ID;
		}

		/*
		 * Sanity check, is the ID really free? Every APIC in a
		 * system must have a unique ID or we get lots of nice
		 * 'stuck on smp_invalidate_needed IPI wait' messages.
		 */
		if (apic->check_apicid_used(&phys_id_present_map,
					    mpc_ioapic_id(ioapic_idx))) {
			printk(KERN_ERR "BIOS bug, IO-APIC#%d ID %d is already used!...\n",
				ioapic_idx, mpc_ioapic_id(ioapic_idx));
			for (i = 0; i < get_physical_broadcast(); i++)
				if (!physid_isset(i, phys_id_present_map))
					break;
			if (i >= get_physical_broadcast())
				panic("Max APIC ID exceeded!\n");
			printk(KERN_ERR "... fixing up to %d. (tell your hw vendor)\n",
				i);
			physid_set(i, phys_id_present_map);
			ioapics[ioapic_idx].mp_config.apicid = i;
		} else {
			physid_mask_t tmp;
			apic->apicid_to_cpu_present(mpc_ioapic_id(ioapic_idx),
						    &tmp);
			apic_printk(APIC_VERBOSE, "Setting %d in the "
					"phys_id_present_map\n",
					mpc_ioapic_id(ioapic_idx));
			physids_or(phys_id_present_map, phys_id_present_map, tmp);
		}

		/*
		 * We need to adjust the IRQ routing table
		 * if the ID changed.
		 */
		if (old_id != mpc_ioapic_id(ioapic_idx))
			for (i = 0; i < mp_irq_entries; i++)
				if (mp_irqs[i].dstapic == old_id)
					mp_irqs[i].dstapic
						= mpc_ioapic_id(ioapic_idx);

		/*
		 * Update the ID register according to the right value
		 * from the MPC table if they are different.
		 */
		if (mpc_ioapic_id(ioapic_idx) == reg_00.bits.ID)
			continue;

		apic_printk(APIC_VERBOSE, KERN_INFO
			"...changing IO-APIC physical APIC ID to %d ...",
			mpc_ioapic_id(ioapic_idx));

		reg_00.bits.ID = mpc_ioapic_id(ioapic_idx);
		raw_spin_lock_irqsave(&ioapic_lock, flags);
		io_apic_write(ioapic_idx, 0, reg_00.raw);
		raw_spin_unlock_irqrestore(&ioapic_lock, flags);

		/*
		 * Sanity check
		 */
		raw_spin_lock_irqsave(&ioapic_lock, flags);
		reg_00.raw = io_apic_read(ioapic_idx, 0);
		raw_spin_unlock_irqrestore(&ioapic_lock, flags);
		if (reg_00.bits.ID != mpc_ioapic_id(ioapic_idx))
			pr_cont("could not set ID!\n");
		else
			apic_printk(APIC_VERBOSE, " ok.\n");
	}
}

void __init setup_ioapic_ids_from_mpc(void)
{

	if (acpi_ioapic)
		return;
	/*
	 * Don't check I/O APIC IDs for xAPIC systems.  They have
	 * no meaning without the serial APIC bus.
	 */
	if (!(boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		|| APIC_XAPIC(apic_version[boot_cpu_physical_apicid]))
		return;
	setup_ioapic_ids_from_mpc_nocheck();
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

static unsigned int startup_ioapic_irq(struct irq_data *data)
{
	int was_pending = 0, irq = data->irq;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	if (irq < legacy_pic->nr_legacy_irqs) {
		legacy_pic->mask(irq);
		if (legacy_pic->irq_pending(irq))
			was_pending = 1;
	}
	__unmask_ioapic(data->chip_data);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return was_pending;
}

static int ioapic_retrigger_irq(struct irq_data *data)
{
	struct irq_cfg *cfg = data->chip_data;
	unsigned long flags;
	int cpu;

	raw_spin_lock_irqsave(&vector_lock, flags);
	cpu = cpumask_first_and(cfg->domain, cpu_online_mask);
	apic->send_IPI_mask(cpumask_of(cpu), cfg->vector);
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

asmlinkage __visible void smp_irq_move_cleanup_interrupt(void)
{
	unsigned vector, me;

	ack_APIC_irq();
	irq_enter();
	exit_idle();

	me = smp_processor_id();
	for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS; vector++) {
		int irq;
		unsigned int irr;
		struct irq_desc *desc;
		struct irq_cfg *cfg;
		irq = __this_cpu_read(vector_irq[vector]);

		if (irq <= VECTOR_UNDEFINED)
			continue;

		desc = irq_to_desc(irq);
		if (!desc)
			continue;

		cfg = irq_cfg(irq);
		if (!cfg)
			continue;

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
		__this_cpu_write(vector_irq[vector], -1);
unlock:
		raw_spin_unlock(&desc->lock);
	}

	irq_exit();
}

static void __irq_complete_move(struct irq_cfg *cfg, unsigned vector)
{
	unsigned me;

	if (likely(!cfg->move_in_progress))
		return;

	me = smp_processor_id();

	if (vector == cfg->vector && cpumask_test_cpu(me, cfg->domain))
		send_cleanup_vector(cfg);
}

static void irq_complete_move(struct irq_cfg *cfg)
{
	__irq_complete_move(cfg, ~get_irq_regs()->orig_ax);
}

void irq_force_complete_move(int irq)
{
	struct irq_cfg *cfg = irq_get_chip_data(irq);

	if (!cfg)
		return;

	__irq_complete_move(cfg, cfg->vector);
}
#else
static inline void irq_complete_move(struct irq_cfg *cfg) { }
#endif

static void __target_IO_APIC_irq(unsigned int irq, unsigned int dest, struct irq_cfg *cfg)
{
	int apic, pin;
	struct irq_pin_list *entry;
	u8 vector = cfg->vector;

	for_each_irq_pin(entry, cfg->irq_2_pin) {
		unsigned int reg;

		apic = entry->apic;
		pin = entry->pin;

		io_apic_write(apic, 0x11 + pin*2, dest);
		reg = io_apic_read(apic, 0x10 + pin*2);
		reg &= ~IO_APIC_REDIR_VECTOR_MASK;
		reg |= vector;
		io_apic_modify(apic, 0x10 + pin*2, reg);
	}
}

/*
 * Either sets data->affinity to a valid value, and returns
 * ->cpu_mask_to_apicid of that in dest_id, or returns -1 and
 * leaves data->affinity untouched.
 */
int __ioapic_set_affinity(struct irq_data *data, const struct cpumask *mask,
			  unsigned int *dest_id)
{
	struct irq_cfg *cfg = data->chip_data;
	unsigned int irq = data->irq;
	int err;

	if (!config_enabled(CONFIG_SMP))
		return -1;

	if (!cpumask_intersects(mask, cpu_online_mask))
		return -EINVAL;

	err = assign_irq_vector(irq, cfg, mask);
	if (err)
		return err;

	err = apic->cpu_mask_to_apicid_and(mask, cfg->domain, dest_id);
	if (err) {
		if (assign_irq_vector(irq, cfg, data->affinity))
			pr_err("Failed to recover vector for irq %d\n", irq);
		return err;
	}

	cpumask_copy(data->affinity, mask);

	return 0;
}


int native_ioapic_set_affinity(struct irq_data *data,
			       const struct cpumask *mask,
			       bool force)
{
	unsigned int dest, irq = data->irq;
	unsigned long flags;
	int ret;

	if (!config_enabled(CONFIG_SMP))
		return -1;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	ret = __ioapic_set_affinity(data, mask, &dest);
	if (!ret) {
		/* Only the high 8 bits are valid. */
		dest = SET_APIC_LOGICAL_ID(dest);
		__target_IO_APIC_irq(irq, dest, data->chip_data);
		ret = IRQ_SET_MASK_OK_NOCOPY;
	}
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
	return ret;
}

static void ack_apic_edge(struct irq_data *data)
{
	irq_complete_move(data->chip_data);
	irq_move_irq(data);
	ack_APIC_irq();
}

atomic_t irq_mis_count;

#ifdef CONFIG_GENERIC_PENDING_IRQ
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

static inline bool ioapic_irqd_mask(struct irq_data *data, struct irq_cfg *cfg)
{
	/* If we are moving the irq we need to mask it */
	if (unlikely(irqd_is_setaffinity_pending(data))) {
		mask_ioapic(cfg);
		return true;
	}
	return false;
}

static inline void ioapic_irqd_unmask(struct irq_data *data,
				      struct irq_cfg *cfg, bool masked)
{
	if (unlikely(masked)) {
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
		if (!io_apic_level_ack_pending(cfg))
			irq_move_masked_irq(data);
		unmask_ioapic(cfg);
	}
}
#else
static inline bool ioapic_irqd_mask(struct irq_data *data, struct irq_cfg *cfg)
{
	return false;
}
static inline void ioapic_irqd_unmask(struct irq_data *data,
				      struct irq_cfg *cfg, bool masked)
{
}
#endif

static void ack_apic_level(struct irq_data *data)
{
	struct irq_cfg *cfg = data->chip_data;
	int i, irq = data->irq;
	unsigned long v;
	bool masked;

	irq_complete_move(cfg);
	masked = ioapic_irqd_mask(data, cfg);

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

		eoi_ioapic_irq(irq, cfg);
	}

	ioapic_irqd_unmask(data, cfg, masked);
}

static struct irq_chip ioapic_chip __read_mostly = {
	.name			= "IO-APIC",
	.irq_startup		= startup_ioapic_irq,
	.irq_mask		= mask_ioapic_irq,
	.irq_unmask		= unmask_ioapic_irq,
	.irq_ack		= ack_apic_edge,
	.irq_eoi		= ack_apic_level,
	.irq_set_affinity	= native_ioapic_set_affinity,
	.irq_retrigger		= ioapic_retrigger_irq,
};

static inline void init_IO_APIC_traps(void)
{
	struct irq_cfg *cfg;
	unsigned int irq;

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
	for_each_active_irq(irq) {
		cfg = irq_get_chip_data(irq);
		if (IO_APIC_IRQ(irq) && cfg && !cfg->vector) {
			/*
			 * Hmm.. We don't have an entry for this,
			 * so default to an old-fashioned 8259
			 * interrupt if we can..
			 */
			if (irq < legacy_pic->nr_legacy_irqs)
				legacy_pic->make_irq(irq);
			else
				/* Strange. Oh, well.. */
				irq_set_chip(irq, &no_irq_chip);
		}
	}
}

/*
 * The local APIC irq-chip implementation:
 */

static void mask_lapic_irq(struct irq_data *data)
{
	unsigned long v;

	v = apic_read(APIC_LVT0);
	apic_write(APIC_LVT0, v | APIC_LVT_MASKED);
}

static void unmask_lapic_irq(struct irq_data *data)
{
	unsigned long v;

	v = apic_read(APIC_LVT0);
	apic_write(APIC_LVT0, v & ~APIC_LVT_MASKED);
}

static void ack_lapic_irq(struct irq_data *data)
{
	ack_APIC_irq();
}

static struct irq_chip lapic_chip __read_mostly = {
	.name		= "local-APIC",
	.irq_mask	= mask_lapic_irq,
	.irq_unmask	= unmask_lapic_irq,
	.irq_ack	= ack_lapic_irq,
};

static void lapic_register_intr(int irq)
{
	irq_clear_status_flags(irq, IRQ_LEVEL);
	irq_set_chip_and_handler_name(irq, &lapic_chip, handle_edge_irq,
				      "edge");
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
	struct irq_cfg *cfg = irq_get_chip_data(0);
	int node = cpu_to_node(0);
	int apic1, pin1, apic2, pin2;
	unsigned long flags;
	int no_pin1 = 0;

	local_irq_save(flags);

	/*
	 * get/set the timer IRQ vector:
	 */
	legacy_pic->mask(0);
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
	legacy_pic->init(1);

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
		panic_if_irq_remap("BIOS bug: timer not connected to IO-APIC");
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
			/* for edge trigger, setup_ioapic_irq already
			 * leave it unmasked.
			 * so only need to unmask if it is level-trigger
			 * do we really have level trigger timer?
			 */
			int idx;
			idx = find_irq_entry(apic1, pin1, mp_INT);
			if (idx != -1 && irq_trigger(idx))
				unmask_ioapic(cfg);
		}
		if (timer_irq_works()) {
			if (disable_timer_pin_1 > 0)
				clear_IO_APIC_pin(0, pin1);
			goto out;
		}
		panic_if_irq_remap("timer doesn't work through Interrupt-remapped IO-APIC");
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
		legacy_pic->unmask(0);
		if (timer_irq_works()) {
			apic_printk(APIC_QUIET, KERN_INFO "....... works.\n");
			timer_through_8259 = 1;
			goto out;
		}
		/*
		 * Cleanup, just in case ...
		 */
		local_irq_disable();
		legacy_pic->mask(0);
		clear_IO_APIC_pin(apic2, pin2);
		apic_printk(APIC_QUIET, KERN_INFO "....... failed.\n");
	}

	apic_printk(APIC_QUIET, KERN_INFO
		    "...trying to set up timer as Virtual Wire IRQ...\n");

	lapic_register_intr(0);
	apic_write(APIC_LVT0, APIC_DM_FIXED | cfg->vector);	/* Fixed mode */
	legacy_pic->unmask(0);

	if (timer_irq_works()) {
		apic_printk(APIC_QUIET, KERN_INFO "..... works.\n");
		goto out;
	}
	local_irq_disable();
	legacy_pic->mask(0);
	apic_write(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_FIXED | cfg->vector);
	apic_printk(APIC_QUIET, KERN_INFO "..... failed.\n");

	apic_printk(APIC_QUIET, KERN_INFO
		    "...trying to set up timer as ExtINT IRQ...\n");

	legacy_pic->init(0);
	legacy_pic->make_irq(0);
	apic_write(APIC_LVT0, APIC_DM_EXTINT);

	unlock_ExtINT_logic();

	if (timer_irq_works()) {
		apic_printk(APIC_QUIET, KERN_INFO "..... works.\n");
		goto out;
	}
	local_irq_disable();
	apic_printk(APIC_QUIET, KERN_INFO "..... failed :(.\n");
	if (x2apic_preenabled)
		apic_printk(APIC_QUIET, KERN_INFO
			    "Perhaps problem with the pre-enabled x2apic mode\n"
			    "Try booting with x2apic and interrupt-remapping disabled in the bios.\n");
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
	io_apic_irqs = legacy_pic->nr_legacy_irqs ? ~PIC_IRQS : ~0UL;

	apic_printk(APIC_VERBOSE, "ENABLING IO-APIC IRQs\n");
	/*
         * Set up IO-APIC IRQ routing.
         */
	x86_init.mpparse.setup_ioapic_ids();

	sync_Arb_IDs();
	setup_IO_APIC_irqs();
	init_IO_APIC_traps();
	if (legacy_pic->nr_legacy_irqs)
		check_timer();
}

/*
 *      Called after all the initialization is done. If we didn't find any
 *      APIC bugs then we can allow the modify fast path
 */

static int __init io_apic_bug_finalize(void)
{
	if (sis_apic_bug == -1)
		sis_apic_bug = 0;
	return 0;
}

late_initcall(io_apic_bug_finalize);

static void resume_ioapic_id(int ioapic_idx)
{
	unsigned long flags;
	union IO_APIC_reg_00 reg_00;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_00.raw = io_apic_read(ioapic_idx, 0);
	if (reg_00.bits.ID != mpc_ioapic_id(ioapic_idx)) {
		reg_00.bits.ID = mpc_ioapic_id(ioapic_idx);
		io_apic_write(ioapic_idx, 0, reg_00.raw);
	}
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void ioapic_resume(void)
{
	int ioapic_idx;

	for (ioapic_idx = nr_ioapics - 1; ioapic_idx >= 0; ioapic_idx--)
		resume_ioapic_id(ioapic_idx);

	restore_ioapic_entries();
}

static struct syscore_ops ioapic_syscore_ops = {
	.suspend = save_ioapic_entries,
	.resume = ioapic_resume,
};

static int __init ioapic_init_ops(void)
{
	register_syscore_ops(&ioapic_syscore_ops);

	return 0;
}

device_initcall(ioapic_init_ops);

/*
 * Dynamic irq allocate and deallocation
 */
unsigned int __create_irqs(unsigned int from, unsigned int count, int node)
{
	struct irq_cfg **cfg;
	unsigned long flags;
	int irq, i;

	if (from < nr_irqs_gsi)
		from = nr_irqs_gsi;

	cfg = kzalloc_node(count * sizeof(cfg[0]), GFP_KERNEL, node);
	if (!cfg)
		return 0;

	irq = alloc_irqs_from(from, count, node);
	if (irq < 0)
		goto out_cfgs;

	for (i = 0; i < count; i++) {
		cfg[i] = alloc_irq_cfg(irq + i, node);
		if (!cfg[i])
			goto out_irqs;
	}

	raw_spin_lock_irqsave(&vector_lock, flags);
	for (i = 0; i < count; i++)
		if (__assign_irq_vector(irq + i, cfg[i], apic->target_cpus()))
			goto out_vecs;
	raw_spin_unlock_irqrestore(&vector_lock, flags);

	for (i = 0; i < count; i++) {
		irq_set_chip_data(irq + i, cfg[i]);
		irq_clear_status_flags(irq + i, IRQ_NOREQUEST);
	}

	kfree(cfg);
	return irq;

out_vecs:
	for (i--; i >= 0; i--)
		__clear_irq_vector(irq + i, cfg[i]);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
out_irqs:
	for (i = 0; i < count; i++)
		free_irq_at(irq + i, cfg[i]);
out_cfgs:
	kfree(cfg);
	return 0;
}

unsigned int create_irq_nr(unsigned int from, int node)
{
	return __create_irqs(from, 1, node);
}

int create_irq(void)
{
	int node = cpu_to_node(0);
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
	struct irq_cfg *cfg = irq_get_chip_data(irq);
	unsigned long flags;

	irq_set_status_flags(irq, IRQ_NOREQUEST|IRQ_NOPROBE);

	free_remapped_irq(irq);

	raw_spin_lock_irqsave(&vector_lock, flags);
	__clear_irq_vector(irq, cfg);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	free_irq_at(irq, cfg);
}

void destroy_irqs(unsigned int irq, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		destroy_irq(irq + i);
}

/*
 * MSI message composition
 */
void native_compose_msi_msg(struct pci_dev *pdev,
			    unsigned int irq, unsigned int dest,
			    struct msi_msg *msg, u8 hpet_id)
{
	struct irq_cfg *cfg = irq_cfg(irq);

	msg->address_hi = MSI_ADDR_BASE_HI;

	if (x2apic_enabled())
		msg->address_hi |= MSI_ADDR_EXT_DEST_ID(dest);

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

	err = apic->cpu_mask_to_apicid_and(cfg->domain,
					   apic->target_cpus(), &dest);
	if (err)
		return err;

	x86_msi.compose_msi_msg(pdev, irq, dest, msg, hpet_id);

	return 0;
}

static int
msi_set_affinity(struct irq_data *data, const struct cpumask *mask, bool force)
{
	struct irq_cfg *cfg = data->chip_data;
	struct msi_msg msg;
	unsigned int dest;

	if (__ioapic_set_affinity(data, mask, &dest))
		return -1;

	__get_cached_msi_msg(data->msi_desc, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	__write_msi_msg(data->msi_desc, &msg);

	return IRQ_SET_MASK_OK_NOCOPY;
}

/*
 * IRQ Chip for MSI PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI or MSI-X Capability Structure.
 */
static struct irq_chip msi_chip = {
	.name			= "PCI-MSI",
	.irq_unmask		= unmask_msi_irq,
	.irq_mask		= mask_msi_irq,
	.irq_ack		= ack_apic_edge,
	.irq_set_affinity	= msi_set_affinity,
	.irq_retrigger		= ioapic_retrigger_irq,
};

int setup_msi_irq(struct pci_dev *dev, struct msi_desc *msidesc,
		  unsigned int irq_base, unsigned int irq_offset)
{
	struct irq_chip *chip = &msi_chip;
	struct msi_msg msg;
	unsigned int irq = irq_base + irq_offset;
	int ret;

	ret = msi_compose_msg(dev, irq, &msg, -1);
	if (ret < 0)
		return ret;

	irq_set_msi_desc_off(irq_base, irq_offset, msidesc);

	/*
	 * MSI-X message is written per-IRQ, the offset is always 0.
	 * MSI message denotes a contiguous group of IRQs, written for 0th IRQ.
	 */
	if (!irq_offset)
		write_msi_msg(irq, &msg);

	setup_remapped_irq(irq, irq_get_chip_data(irq), chip);

	irq_set_chip_and_handler_name(irq, chip, handle_edge_irq, "edge");

	dev_printk(KERN_DEBUG, &dev->dev, "irq %d for MSI/MSI-X\n", irq);

	return 0;
}

int native_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	unsigned int irq, irq_want;
	struct msi_desc *msidesc;
	int node, ret;

	/* Multiple MSI vectors only supported with interrupt remapping */
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	node = dev_to_node(&dev->dev);
	irq_want = nr_irqs_gsi;
	list_for_each_entry(msidesc, &dev->msi_list, list) {
		irq = create_irq_nr(irq_want, node);
		if (irq == 0)
			return -ENOSPC;

		irq_want = irq + 1;

		ret = setup_msi_irq(dev, msidesc, irq, 0);
		if (ret < 0)
			goto error;
	}
	return 0;

error:
	destroy_irq(irq);
	return ret;
}

void native_teardown_msi_irq(unsigned int irq)
{
	destroy_irq(irq);
}

#ifdef CONFIG_DMAR_TABLE
static int
dmar_msi_set_affinity(struct irq_data *data, const struct cpumask *mask,
		      bool force)
{
	struct irq_cfg *cfg = data->chip_data;
	unsigned int dest, irq = data->irq;
	struct msi_msg msg;

	if (__ioapic_set_affinity(data, mask, &dest))
		return -1;

	dmar_msi_read(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);
	msg.address_hi = MSI_ADDR_BASE_HI | MSI_ADDR_EXT_DEST_ID(dest);

	dmar_msi_write(irq, &msg);

	return IRQ_SET_MASK_OK_NOCOPY;
}

static struct irq_chip dmar_msi_type = {
	.name			= "DMAR_MSI",
	.irq_unmask		= dmar_msi_unmask,
	.irq_mask		= dmar_msi_mask,
	.irq_ack		= ack_apic_edge,
	.irq_set_affinity	= dmar_msi_set_affinity,
	.irq_retrigger		= ioapic_retrigger_irq,
};

int arch_setup_dmar_msi(unsigned int irq)
{
	int ret;
	struct msi_msg msg;

	ret = msi_compose_msg(NULL, irq, &msg, -1);
	if (ret < 0)
		return ret;
	dmar_msi_write(irq, &msg);
	irq_set_chip_and_handler_name(irq, &dmar_msi_type, handle_edge_irq,
				      "edge");
	return 0;
}
#endif

#ifdef CONFIG_HPET_TIMER

static int hpet_msi_set_affinity(struct irq_data *data,
				 const struct cpumask *mask, bool force)
{
	struct irq_cfg *cfg = data->chip_data;
	struct msi_msg msg;
	unsigned int dest;

	if (__ioapic_set_affinity(data, mask, &dest))
		return -1;

	hpet_msi_read(data->handler_data, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	hpet_msi_write(data->handler_data, &msg);

	return IRQ_SET_MASK_OK_NOCOPY;
}

static struct irq_chip hpet_msi_type = {
	.name = "HPET_MSI",
	.irq_unmask = hpet_msi_unmask,
	.irq_mask = hpet_msi_mask,
	.irq_ack = ack_apic_edge,
	.irq_set_affinity = hpet_msi_set_affinity,
	.irq_retrigger = ioapic_retrigger_irq,
};

int default_setup_hpet_msi(unsigned int irq, unsigned int id)
{
	struct irq_chip *chip = &hpet_msi_type;
	struct msi_msg msg;
	int ret;

	ret = msi_compose_msg(NULL, irq, &msg, id);
	if (ret < 0)
		return ret;

	hpet_msi_write(irq_get_handler_data(irq), &msg);
	irq_set_status_flags(irq, IRQ_MOVE_PCNTXT);
	setup_remapped_irq(irq, irq_get_chip_data(irq), chip);

	irq_set_chip_and_handler_name(irq, chip, handle_edge_irq, "edge");
	return 0;
}
#endif

#endif /* CONFIG_PCI_MSI */
/*
 * Hypertransport interrupt support
 */
#ifdef CONFIG_HT_IRQ

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

static int
ht_set_affinity(struct irq_data *data, const struct cpumask *mask, bool force)
{
	struct irq_cfg *cfg = data->chip_data;
	unsigned int dest;

	if (__ioapic_set_affinity(data, mask, &dest))
		return -1;

	target_ht_irq(data->irq, dest, cfg->vector);
	return IRQ_SET_MASK_OK_NOCOPY;
}

static struct irq_chip ht_irq_chip = {
	.name			= "PCI-HT",
	.irq_mask		= mask_ht_irq,
	.irq_unmask		= unmask_ht_irq,
	.irq_ack		= ack_apic_edge,
	.irq_set_affinity	= ht_set_affinity,
	.irq_retrigger		= ioapic_retrigger_irq,
};

int arch_setup_ht_irq(unsigned int irq, struct pci_dev *dev)
{
	struct irq_cfg *cfg;
	struct ht_irq_msg msg;
	unsigned dest;
	int err;

	if (disable_apic)
		return -ENXIO;

	cfg = irq_cfg(irq);
	err = assign_irq_vector(irq, cfg, apic->target_cpus());
	if (err)
		return err;

	err = apic->cpu_mask_to_apicid_and(cfg->domain,
					   apic->target_cpus(), &dest);
	if (err)
		return err;

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

	irq_set_chip_and_handler_name(irq, &ht_irq_chip,
				      handle_edge_irq, "edge");

	dev_printk(KERN_DEBUG, &dev->dev, "irq %d for HT\n", irq);

	return 0;
}
#endif /* CONFIG_HT_IRQ */

static int
io_apic_setup_irq_pin(unsigned int irq, int node, struct io_apic_irq_attr *attr)
{
	struct irq_cfg *cfg = alloc_irq_and_cfg_at(irq, node);
	int ret;

	if (!cfg)
		return -EINVAL;
	ret = __add_pin_to_irq_node(cfg, node, attr->ioapic, attr->ioapic_pin);
	if (!ret)
		setup_ioapic_irq(irq, cfg, attr);
	return ret;
}

int io_apic_setup_irq_pin_once(unsigned int irq, int node,
			       struct io_apic_irq_attr *attr)
{
	unsigned int ioapic_idx = attr->ioapic, pin = attr->ioapic_pin;
	int ret;
	struct IO_APIC_route_entry orig_entry;

	/* Avoid redundant programming */
	if (test_bit(pin, ioapics[ioapic_idx].pin_programmed)) {
		pr_debug("Pin %d-%d already programmed\n", mpc_ioapic_id(ioapic_idx), pin);
		orig_entry = ioapic_read_entry(attr->ioapic, pin);
		if (attr->trigger == orig_entry.trigger && attr->polarity == orig_entry.polarity)
			return 0;
		return -EBUSY;
	}
	ret = io_apic_setup_irq_pin(irq, node, attr);
	if (!ret)
		set_bit(pin, ioapics[ioapic_idx].pin_programmed);
	return ret;
}

static int __init io_apic_get_redir_entries(int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	/* The register returns the maximum index redir index
	 * supported, which is one less than the total number of redir
	 * entries.
	 */
	return reg_01.bits.entries + 1;
}

static void __init probe_nr_irqs_gsi(void)
{
	int nr;

	nr = gsi_top + NR_IRQS_LEGACY;
	if (nr > nr_irqs_gsi)
		nr_irqs_gsi = nr;

	printk(KERN_DEBUG "nr_irqs_gsi: %d\n", nr_irqs_gsi);
}

int get_nr_irqs_gsi(void)
{
	return nr_irqs_gsi;
}

unsigned int arch_dynirq_lower_bound(unsigned int from)
{
	return from < nr_irqs_gsi ? nr_irqs_gsi : from;
}

int __init arch_probe_nr_irqs(void)
{
	int nr;

	if (nr_irqs > (NR_VECTORS * nr_cpu_ids))
		nr_irqs = NR_VECTORS * nr_cpu_ids;

	nr = nr_irqs_gsi + 8 * nr_cpu_ids;
#if defined(CONFIG_PCI_MSI) || defined(CONFIG_HT_IRQ)
	/*
	 * for MSI and HT dyn irq
	 */
	nr += nr_irqs_gsi * 16;
#endif
	if (nr < nr_irqs)
		nr_irqs = nr;

	return NR_IRQS_LEGACY;
}

int io_apic_set_pci_routing(struct device *dev, int irq,
			    struct io_apic_irq_attr *irq_attr)
{
	int node;

	if (!IO_APIC_IRQ(irq)) {
		apic_printk(APIC_QUIET,KERN_ERR "IOAPIC[%d]: Invalid reference to IRQ 0\n",
			    irq_attr->ioapic);
		return -EINVAL;
	}

	node = dev ? dev_to_node(dev) : cpu_to_node(0);

	return io_apic_setup_irq_pin_once(irq, node, irq_attr);
}

#ifdef CONFIG_X86_32
static int __init io_apic_get_unique_id(int ioapic, int apic_id)
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
			pr_err("IOAPIC[%d]: Unable to change apic_id!\n",
			       ioapic);
			return -1;
		}
	}

	apic_printk(APIC_VERBOSE, KERN_INFO
			"IOAPIC[%d]: Assigned apic_id %d\n", ioapic, apic_id);

	return apic_id;
}

static u8 __init io_apic_unique_id(u8 id)
{
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) &&
	    !APIC_XAPIC(apic_version[boot_cpu_physical_apicid]))
		return io_apic_get_unique_id(nr_ioapics, id);
	else
		return id;
}
#else
static u8 __init io_apic_unique_id(u8 id)
{
	int i;
	DECLARE_BITMAP(used, 256);

	bitmap_zero(used, 256);
	for (i = 0; i < nr_ioapics; i++) {
		__set_bit(mpc_ioapic_id(i), used);
	}
	if (!test_bit(id, used))
		return id;
	return find_first_zero_bit(used, 256);
}
#endif

static int __init io_apic_get_version(int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return reg_01.bits.version;
}

int acpi_get_override_irq(u32 gsi, int *trigger, int *polarity)
{
	int ioapic, pin, idx;

	if (skip_ioapic_setup)
		return -1;

	ioapic = mp_find_ioapic(gsi);
	if (ioapic < 0)
		return -1;

	pin = mp_find_ioapic_pin(ioapic, gsi);
	if (pin < 0)
		return -1;

	idx = find_irq_entry(ioapic, pin, mp_INT);
	if (idx < 0)
		return -1;

	*trigger = irq_trigger(idx);
	*polarity = irq_polarity(idx);
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
	int pin, ioapic, irq, irq_entry;
	const struct cpumask *mask;
	struct irq_data *idata;

	if (skip_ioapic_setup == 1)
		return;

	for (ioapic = 0; ioapic < nr_ioapics; ioapic++)
	for (pin = 0; pin < ioapics[ioapic].nr_registers; pin++) {
		irq_entry = find_irq_entry(ioapic, pin, mp_INT);
		if (irq_entry == -1)
			continue;
		irq = pin_2_irq(irq_entry, ioapic, pin);

		if ((ioapic > 0) && (irq > 16))
			continue;

		idata = irq_get_irq_data(irq);

		/*
		 * Honour affinities which have been set in early boot
		 */
		if (!irqd_can_balance(idata) || irqd_affinity_was_set(idata))
			mask = idata->affinity;
		else
			mask = apic->target_cpus();

		x86_io_apic_ops.set_affinity(idata, mask, false);
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

void __init native_io_apic_init_mappings(void)
{
	unsigned long ioapic_phys, idx = FIX_IO_APIC_BASE_0;
	struct resource *ioapic_res;
	int i;

	ioapic_res = ioapic_setup_resources(nr_ioapics);
	for (i = 0; i < nr_ioapics; i++) {
		if (smp_found_config) {
			ioapic_phys = mpc_ioapic_addr(i);
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

	probe_nr_irqs_gsi();
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

int mp_find_ioapic(u32 gsi)
{
	int i = 0;

	if (nr_ioapics == 0)
		return -1;

	/* Find the IOAPIC that manages this GSI. */
	for (i = 0; i < nr_ioapics; i++) {
		struct mp_ioapic_gsi *gsi_cfg = mp_ioapic_gsi_routing(i);
		if ((gsi >= gsi_cfg->gsi_base)
		    && (gsi <= gsi_cfg->gsi_end))
			return i;
	}

	printk(KERN_ERR "ERROR: Unable to locate IOAPIC for GSI %d\n", gsi);
	return -1;
}

int mp_find_ioapic_pin(int ioapic, u32 gsi)
{
	struct mp_ioapic_gsi *gsi_cfg;

	if (WARN_ON(ioapic == -1))
		return -1;

	gsi_cfg = mp_ioapic_gsi_routing(ioapic);
	if (WARN_ON(gsi > gsi_cfg->gsi_end))
		return -1;

	return gsi - gsi_cfg->gsi_base;
}

static __init int bad_ioapic(unsigned long address)
{
	if (nr_ioapics >= MAX_IO_APICS) {
		pr_warn("WARNING: Max # of I/O APICs (%d) exceeded (found %d), skipping\n",
			MAX_IO_APICS, nr_ioapics);
		return 1;
	}
	if (!address) {
		pr_warn("WARNING: Bogus (zero) I/O APIC address found in table, skipping!\n");
		return 1;
	}
	return 0;
}

static __init int bad_ioapic_register(int idx)
{
	union IO_APIC_reg_00 reg_00;
	union IO_APIC_reg_01 reg_01;
	union IO_APIC_reg_02 reg_02;

	reg_00.raw = io_apic_read(idx, 0);
	reg_01.raw = io_apic_read(idx, 1);
	reg_02.raw = io_apic_read(idx, 2);

	if (reg_00.raw == -1 && reg_01.raw == -1 && reg_02.raw == -1) {
		pr_warn("I/O APIC 0x%x registers return all ones, skipping!\n",
			mpc_ioapic_addr(idx));
		return 1;
	}

	return 0;
}

void __init mp_register_ioapic(int id, u32 address, u32 gsi_base)
{
	int idx = 0;
	int entries;
	struct mp_ioapic_gsi *gsi_cfg;

	if (bad_ioapic(address))
		return;

	idx = nr_ioapics;

	ioapics[idx].mp_config.type = MP_IOAPIC;
	ioapics[idx].mp_config.flags = MPC_APIC_USABLE;
	ioapics[idx].mp_config.apicaddr = address;

	set_fixmap_nocache(FIX_IO_APIC_BASE_0 + idx, address);

	if (bad_ioapic_register(idx)) {
		clear_fixmap(FIX_IO_APIC_BASE_0 + idx);
		return;
	}

	ioapics[idx].mp_config.apicid = io_apic_unique_id(id);
	ioapics[idx].mp_config.apicver = io_apic_get_version(idx);

	/*
	 * Build basic GSI lookup table to facilitate gsi->io_apic lookups
	 * and to prevent reprogramming of IOAPIC pins (PCI GSIs).
	 */
	entries = io_apic_get_redir_entries(idx);
	gsi_cfg = mp_ioapic_gsi_routing(idx);
	gsi_cfg->gsi_base = gsi_base;
	gsi_cfg->gsi_end = gsi_base + entries - 1;

	/*
	 * The number of IO-APIC IRQ registers (== #pins):
	 */
	ioapics[idx].nr_registers = entries;

	if (gsi_cfg->gsi_end >= gsi_top)
		gsi_top = gsi_cfg->gsi_end + 1;

	pr_info("IOAPIC[%d]: apic_id %d, version %d, address 0x%x, GSI %d-%d\n",
		idx, mpc_ioapic_id(idx),
		mpc_ioapic_ver(idx), mpc_ioapic_addr(idx),
		gsi_cfg->gsi_base, gsi_cfg->gsi_end);

	nr_ioapics++;
}

/* Enable IOAPIC early just for system timer */
void __init pre_init_apic_IRQ0(void)
{
	struct io_apic_irq_attr attr = { 0, 0, 0, 0 };

	printk(KERN_INFO "Early APIC setup for system timer0\n");
#ifndef CONFIG_SMP
	physid_set_mask_of_physid(boot_cpu_physical_apicid,
					 &phys_cpu_present_map);
#endif
	setup_local_APIC();

	io_apic_setup_irq_pin(0, 0, &attr);
	irq_set_chip_and_handler_name(0, &ioapic_chip, handle_edge_irq,
				      "edge");
}
