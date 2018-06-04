// SPDX-License-Identifier: GPL-2.0
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
 *
 * Historical information which is worth to be preserved:
 *
 * - SiS APIC rmw bug:
 *
 *	We used to have a workaround for a bug in SiS chips which
 *	required to rewrite the index register for a read-modify-write
 *	operation as the chip lost the index information which was
 *	setup for the read already. We cache the data now, so that
 *	workaround has been removed.
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
#include <linux/export.h>
#include <linux/syscore_ops.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>	/* time_after() */
#include <linux/slab.h>
#include <linux/bootmem.h>

#include <asm/irqdomain.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/cpu.h>
#include <asm/desc.h>
#include <asm/proto.h>
#include <asm/acpi.h>
#include <asm/dma.h>
#include <asm/timer.h>
#include <asm/i8259.h>
#include <asm/setup.h>
#include <asm/irq_remapping.h>
#include <asm/hw_irq.h>

#include <asm/apic.h>

#define	for_each_ioapic(idx)		\
	for ((idx) = 0; (idx) < nr_ioapics; (idx)++)
#define	for_each_ioapic_reverse(idx)	\
	for ((idx) = nr_ioapics - 1; (idx) >= 0; (idx)--)
#define	for_each_pin(idx, pin)		\
	for ((pin) = 0; (pin) < ioapics[(idx)].nr_registers; (pin)++)
#define	for_each_ioapic_pin(idx, pin)	\
	for_each_ioapic((idx))		\
		for_each_pin((idx), (pin))
#define for_each_irq_pin(entry, head) \
	list_for_each_entry(entry, &head, list)

static DEFINE_RAW_SPINLOCK(ioapic_lock);
static DEFINE_MUTEX(ioapic_mutex);
static unsigned int ioapic_dynirq_base;
static int ioapic_initialized;

struct irq_pin_list {
	struct list_head list;
	int apic, pin;
};

struct mp_chip_data {
	struct list_head irq_2_pin;
	struct IO_APIC_route_entry entry;
	int trigger;
	int polarity;
	u32 count;
	bool isa_irq;
};

struct mp_ioapic_gsi {
	u32 gsi_base;
	u32 gsi_end;
};

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
	struct ioapic_domain_cfg irqdomain_cfg;
	struct irq_domain *irqdomain;
	struct resource *iomem_res;
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

static inline struct mp_ioapic_gsi *mp_ioapic_gsi_routing(int ioapic_idx)
{
	return &ioapics[ioapic_idx].gsi_config;
}

static inline int mp_ioapic_pin_count(int ioapic)
{
	struct mp_ioapic_gsi *gsi_cfg = mp_ioapic_gsi_routing(ioapic);

	return gsi_cfg->gsi_end - gsi_cfg->gsi_base + 1;
}

static inline u32 mp_pin_to_gsi(int ioapic, int pin)
{
	return mp_ioapic_gsi_routing(ioapic)->gsi_base + pin;
}

static inline bool mp_is_legacy_irq(int irq)
{
	return irq >= 0 && irq < nr_legacy_irqs();
}

/*
 * Initialize all legacy IRQs and all pins on the first IOAPIC
 * if we have legacy interrupt controller. Kernel boot option "pirq="
 * may rely on non-legacy pins on the first IOAPIC.
 */
static inline int mp_init_irq_at_boot(int ioapic, int irq)
{
	if (!nr_legacy_irqs())
		return 0;

	return ioapic == 0 || mp_is_legacy_irq(irq);
}

static inline struct irq_domain *mp_ioapic_irqdomain(int ioapic)
{
	return ioapics[ioapic].irqdomain;
}

int nr_ioapics;

/* The one past the highest gsi number used */
u32 gsi_top;

/* MP IRQ source entries */
struct mpc_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* # of MP IRQ source entries */
int mp_irq_entries;

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

static void alloc_ioapic_saved_registers(int idx)
{
	size_t size;

	if (ioapics[idx].saved_registers)
		return;

	size = sizeof(struct IO_APIC_route_entry) * ioapics[idx].nr_registers;
	ioapics[idx].saved_registers = kzalloc(size, GFP_KERNEL);
	if (!ioapics[idx].saved_registers)
		pr_err("IOAPIC %d: suspend/resume impossible!\n", idx);
}

static void free_ioapic_saved_registers(int idx)
{
	kfree(ioapics[idx].saved_registers);
	ioapics[idx].saved_registers = NULL;
}

int __init arch_early_ioapic_init(void)
{
	int i;

	if (!nr_legacy_irqs())
		io_apic_irqs = ~0UL;

	for_each_ioapic(i)
		alloc_ioapic_saved_registers(i);

	return 0;
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

static inline void io_apic_eoi(unsigned int apic, unsigned int vector)
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

static void io_apic_write(unsigned int apic, unsigned int reg,
			  unsigned int value)
{
	struct io_apic __iomem *io_apic = io_apic_base(apic);

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
	union entry_union eu = { .entry.mask = IOAPIC_MASKED };

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
static int __add_pin_to_irq_node(struct mp_chip_data *data,
				 int node, int apic, int pin)
{
	struct irq_pin_list *entry;

	/* don't allow duplicates */
	for_each_irq_pin(entry, data->irq_2_pin)
		if (entry->apic == apic && entry->pin == pin)
			return 0;

	entry = kzalloc_node(sizeof(struct irq_pin_list), GFP_ATOMIC, node);
	if (!entry) {
		pr_err("can not alloc irq_pin_list (%d,%d,%d)\n",
		       node, apic, pin);
		return -ENOMEM;
	}
	entry->apic = apic;
	entry->pin = pin;
	list_add_tail(&entry->list, &data->irq_2_pin);

	return 0;
}

static void __remove_pin_from_irq(struct mp_chip_data *data, int apic, int pin)
{
	struct irq_pin_list *tmp, *entry;

	list_for_each_entry_safe(entry, tmp, &data->irq_2_pin, list)
		if (entry->apic == apic && entry->pin == pin) {
			list_del(&entry->list);
			kfree(entry);
			return;
		}
}

static void add_pin_to_irq_node(struct mp_chip_data *data,
				int node, int apic, int pin)
{
	if (__add_pin_to_irq_node(data, node, apic, pin))
		panic("IO-APIC: failed to add irq-pin. Can not proceed\n");
}

/*
 * Reroute an IRQ to a different pin.
 */
static void __init replace_pin_at_irq_node(struct mp_chip_data *data, int node,
					   int oldapic, int oldpin,
					   int newapic, int newpin)
{
	struct irq_pin_list *entry;

	for_each_irq_pin(entry, data->irq_2_pin) {
		if (entry->apic == oldapic && entry->pin == oldpin) {
			entry->apic = newapic;
			entry->pin = newpin;
			/* every one is different, right? */
			return;
		}
	}

	/* old apic/pin didn't exist, so just add new ones */
	add_pin_to_irq_node(data, node, newapic, newpin);
}

static void io_apic_modify_irq(struct mp_chip_data *data,
			       int mask_and, int mask_or,
			       void (*final)(struct irq_pin_list *entry))
{
	union entry_union eu;
	struct irq_pin_list *entry;

	eu.entry = data->entry;
	eu.w1 &= mask_and;
	eu.w1 |= mask_or;
	data->entry = eu.entry;

	for_each_irq_pin(entry, data->irq_2_pin) {
		io_apic_write(entry->apic, 0x10 + 2 * entry->pin, eu.w1);
		if (final)
			final(entry);
	}
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

static void mask_ioapic_irq(struct irq_data *irq_data)
{
	struct mp_chip_data *data = irq_data->chip_data;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_modify_irq(data, ~0, IO_APIC_REDIR_MASKED, &io_apic_sync);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void __unmask_ioapic(struct mp_chip_data *data)
{
	io_apic_modify_irq(data, ~IO_APIC_REDIR_MASKED, 0, NULL);
}

static void unmask_ioapic_irq(struct irq_data *irq_data)
{
	struct mp_chip_data *data = irq_data->chip_data;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	__unmask_ioapic(data);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
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
static void __eoi_ioapic_pin(int apic, int pin, int vector)
{
	if (mpc_ioapic_ver(apic) >= 0x20) {
		io_apic_eoi(apic, vector);
	} else {
		struct IO_APIC_route_entry entry, entry1;

		entry = entry1 = __ioapic_read_entry(apic, pin);

		/*
		 * Mask the entry and change the trigger mode to edge.
		 */
		entry1.mask = IOAPIC_MASKED;
		entry1.trigger = IOAPIC_EDGE;

		__ioapic_write_entry(apic, pin, entry1);

		/*
		 * Restore the previous level triggered entry.
		 */
		__ioapic_write_entry(apic, pin, entry);
	}
}

static void eoi_ioapic_pin(int vector, struct mp_chip_data *data)
{
	unsigned long flags;
	struct irq_pin_list *entry;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	for_each_irq_pin(entry, data->irq_2_pin)
		__eoi_ioapic_pin(entry->apic, entry->pin, vector);
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
	if (entry.mask == IOAPIC_UNMASKED) {
		entry.mask = IOAPIC_MASKED;
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
		if (entry.trigger == IOAPIC_EDGE) {
			entry.trigger = IOAPIC_LEVEL;
			ioapic_write_entry(apic, pin, entry);
		}
		raw_spin_lock_irqsave(&ioapic_lock, flags);
		__eoi_ioapic_pin(apic, pin, entry.vector);
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

void clear_IO_APIC (void)
{
	int apic, pin;

	for_each_ioapic_pin(apic, pin)
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

	for_each_ioapic(apic) {
		if (!ioapics[apic].saved_registers) {
			err = -ENOMEM;
			continue;
		}

		for_each_pin(apic, pin)
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

	for_each_ioapic(apic) {
		if (!ioapics[apic].saved_registers)
			continue;

		for_each_pin(apic, pin) {
			struct IO_APIC_route_entry entry;

			entry = ioapics[apic].saved_registers[pin];
			if (entry.mask == IOAPIC_UNMASKED) {
				entry.mask = IOAPIC_MASKED;
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

	for_each_ioapic(apic) {
		if (!ioapics[apic].saved_registers)
			continue;

		for_each_pin(apic, pin)
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

		for_each_ioapic(ioapic_idx)
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
	if (irq < nr_legacy_irqs()) {
		unsigned int port = 0x4d0 + (irq >> 3);
		return (inb(port) >> (irq & 7)) & 1;
	}
	apic_printk(APIC_VERBOSE, KERN_INFO
			"Broken MPtable reports ISA irq %d\n", irq);
	return 0;
}

#endif

/* ISA interrupts are always active high edge triggered,
 * when listed as conforming in the MP table. */

#define default_ISA_trigger(idx)	(IOAPIC_EDGE)
#define default_ISA_polarity(idx)	(IOAPIC_POL_HIGH)

/* EISA interrupts are always polarity zero and can be edge or level
 * trigger depending on the ELCR value.  If an interrupt is listed as
 * EISA conforming in the MP table, that means its trigger type must
 * be read in from the ELCR */

#define default_EISA_trigger(idx)	(EISA_ELCR(mp_irqs[idx].srcbusirq))
#define default_EISA_polarity(idx)	default_ISA_polarity(idx)

/* PCI interrupts are always active low level triggered,
 * when listed as conforming in the MP table. */

#define default_PCI_trigger(idx)	(IOAPIC_LEVEL)
#define default_PCI_polarity(idx)	(IOAPIC_POL_LOW)

static int irq_polarity(int idx)
{
	int bus = mp_irqs[idx].srcbus;

	/*
	 * Determine IRQ line polarity (high active or low active):
	 */
	switch (mp_irqs[idx].irqflag & MP_IRQPOL_MASK) {
	case MP_IRQPOL_DEFAULT:
		/* conforms to spec, ie. bus-type dependent polarity */
		if (test_bit(bus, mp_bus_not_pci))
			return default_ISA_polarity(idx);
		else
			return default_PCI_polarity(idx);
	case MP_IRQPOL_ACTIVE_HIGH:
		return IOAPIC_POL_HIGH;
	case MP_IRQPOL_RESERVED:
		pr_warn("IOAPIC: Invalid polarity: 2, defaulting to low\n");
	case MP_IRQPOL_ACTIVE_LOW:
	default: /* Pointless default required due to do gcc stupidity */
		return IOAPIC_POL_LOW;
	}
}

#ifdef CONFIG_EISA
static int eisa_irq_trigger(int idx, int bus, int trigger)
{
	switch (mp_bus_id_to_type[bus]) {
	case MP_BUS_PCI:
	case MP_BUS_ISA:
		return trigger;
	case MP_BUS_EISA:
		return default_EISA_trigger(idx);
	}
	pr_warn("IOAPIC: Invalid srcbus: %d defaulting to level\n", bus);
	return IOAPIC_LEVEL;
}
#else
static inline int eisa_irq_trigger(int idx, int bus, int trigger)
{
	return trigger;
}
#endif

static int irq_trigger(int idx)
{
	int bus = mp_irqs[idx].srcbus;
	int trigger;

	/*
	 * Determine IRQ trigger mode (edge or level sensitive):
	 */
	switch (mp_irqs[idx].irqflag & MP_IRQTRIG_MASK) {
	case MP_IRQTRIG_DEFAULT:
		/* conforms to spec, ie. bus-type dependent trigger mode */
		if (test_bit(bus, mp_bus_not_pci))
			trigger = default_ISA_trigger(idx);
		else
			trigger = default_PCI_trigger(idx);
		/* Take EISA into account */
		return eisa_irq_trigger(idx, bus, trigger);
	case MP_IRQTRIG_EDGE:
		return IOAPIC_EDGE;
	case MP_IRQTRIG_RESERVED:
		pr_warn("IOAPIC: Invalid trigger mode 2 defaulting to level\n");
	case MP_IRQTRIG_LEVEL:
	default: /* Pointless default required due to do gcc stupidity */
		return IOAPIC_LEVEL;
	}
}

void ioapic_set_alloc_attr(struct irq_alloc_info *info, int node,
			   int trigger, int polarity)
{
	init_irq_alloc_info(info, NULL);
	info->type = X86_IRQ_ALLOC_TYPE_IOAPIC;
	info->ioapic_node = node;
	info->ioapic_trigger = trigger;
	info->ioapic_polarity = polarity;
	info->ioapic_valid = 1;
}

#ifndef CONFIG_ACPI
int acpi_get_override_irq(u32 gsi, int *trigger, int *polarity);
#endif

static void ioapic_copy_alloc_attr(struct irq_alloc_info *dst,
				   struct irq_alloc_info *src,
				   u32 gsi, int ioapic_idx, int pin)
{
	int trigger, polarity;

	copy_irq_alloc_info(dst, src);
	dst->type = X86_IRQ_ALLOC_TYPE_IOAPIC;
	dst->ioapic_id = mpc_ioapic_id(ioapic_idx);
	dst->ioapic_pin = pin;
	dst->ioapic_valid = 1;
	if (src && src->ioapic_valid) {
		dst->ioapic_node = src->ioapic_node;
		dst->ioapic_trigger = src->ioapic_trigger;
		dst->ioapic_polarity = src->ioapic_polarity;
	} else {
		dst->ioapic_node = NUMA_NO_NODE;
		if (acpi_get_override_irq(gsi, &trigger, &polarity) >= 0) {
			dst->ioapic_trigger = trigger;
			dst->ioapic_polarity = polarity;
		} else {
			/*
			 * PCI interrupts are always active low level
			 * triggered.
			 */
			dst->ioapic_trigger = IOAPIC_LEVEL;
			dst->ioapic_polarity = IOAPIC_POL_LOW;
		}
	}
}

static int ioapic_alloc_attr_node(struct irq_alloc_info *info)
{
	return (info && info->ioapic_valid) ? info->ioapic_node : NUMA_NO_NODE;
}

static void mp_register_handler(unsigned int irq, unsigned long trigger)
{
	irq_flow_handler_t hdl;
	bool fasteoi;

	if (trigger) {
		irq_set_status_flags(irq, IRQ_LEVEL);
		fasteoi = true;
	} else {
		irq_clear_status_flags(irq, IRQ_LEVEL);
		fasteoi = false;
	}

	hdl = fasteoi ? handle_fasteoi_irq : handle_edge_irq;
	__irq_set_handler(irq, hdl, 0, fasteoi ? "fasteoi" : "edge");
}

static bool mp_check_pin_attr(int irq, struct irq_alloc_info *info)
{
	struct mp_chip_data *data = irq_get_chip_data(irq);

	/*
	 * setup_IO_APIC_irqs() programs all legacy IRQs with default trigger
	 * and polarity attirbutes. So allow the first user to reprogram the
	 * pin with real trigger and polarity attributes.
	 */
	if (irq < nr_legacy_irqs() && data->count == 1) {
		if (info->ioapic_trigger != data->trigger)
			mp_register_handler(irq, info->ioapic_trigger);
		data->entry.trigger = data->trigger = info->ioapic_trigger;
		data->entry.polarity = data->polarity = info->ioapic_polarity;
	}

	return data->trigger == info->ioapic_trigger &&
	       data->polarity == info->ioapic_polarity;
}

static int alloc_irq_from_domain(struct irq_domain *domain, int ioapic, u32 gsi,
				 struct irq_alloc_info *info)
{
	bool legacy = false;
	int irq = -1;
	int type = ioapics[ioapic].irqdomain_cfg.type;

	switch (type) {
	case IOAPIC_DOMAIN_LEGACY:
		/*
		 * Dynamically allocate IRQ number for non-ISA IRQs in the first
		 * 16 GSIs on some weird platforms.
		 */
		if (!ioapic_initialized || gsi >= nr_legacy_irqs())
			irq = gsi;
		legacy = mp_is_legacy_irq(irq);
		break;
	case IOAPIC_DOMAIN_STRICT:
		irq = gsi;
		break;
	case IOAPIC_DOMAIN_DYNAMIC:
		break;
	default:
		WARN(1, "ioapic: unknown irqdomain type %d\n", type);
		return -1;
	}

	return __irq_domain_alloc_irqs(domain, irq, 1,
				       ioapic_alloc_attr_node(info),
				       info, legacy, NULL);
}

/*
 * Need special handling for ISA IRQs because there may be multiple IOAPIC pins
 * sharing the same ISA IRQ number and irqdomain only supports 1:1 mapping
 * between IOAPIC pin and IRQ number. A typical IOAPIC has 24 pins, pin 0-15 are
 * used for legacy IRQs and pin 16-23 are used for PCI IRQs (PIRQ A-H).
 * When ACPI is disabled, only legacy IRQ numbers (IRQ0-15) are available, and
 * some BIOSes may use MP Interrupt Source records to override IRQ numbers for
 * PIRQs instead of reprogramming the interrupt routing logic. Thus there may be
 * multiple pins sharing the same legacy IRQ number when ACPI is disabled.
 */
static int alloc_isa_irq_from_domain(struct irq_domain *domain,
				     int irq, int ioapic, int pin,
				     struct irq_alloc_info *info)
{
	struct mp_chip_data *data;
	struct irq_data *irq_data = irq_get_irq_data(irq);
	int node = ioapic_alloc_attr_node(info);

	/*
	 * Legacy ISA IRQ has already been allocated, just add pin to
	 * the pin list assoicated with this IRQ and program the IOAPIC
	 * entry. The IOAPIC entry
	 */
	if (irq_data && irq_data->parent_data) {
		if (!mp_check_pin_attr(irq, info))
			return -EBUSY;
		if (__add_pin_to_irq_node(irq_data->chip_data, node, ioapic,
					  info->ioapic_pin))
			return -ENOMEM;
	} else {
		info->flags |= X86_IRQ_ALLOC_LEGACY;
		irq = __irq_domain_alloc_irqs(domain, irq, 1, node, info, true,
					      NULL);
		if (irq >= 0) {
			irq_data = irq_domain_get_irq_data(domain, irq);
			data = irq_data->chip_data;
			data->isa_irq = true;
		}
	}

	return irq;
}

static int mp_map_pin_to_irq(u32 gsi, int idx, int ioapic, int pin,
			     unsigned int flags, struct irq_alloc_info *info)
{
	int irq;
	bool legacy = false;
	struct irq_alloc_info tmp;
	struct mp_chip_data *data;
	struct irq_domain *domain = mp_ioapic_irqdomain(ioapic);

	if (!domain)
		return -ENOSYS;

	if (idx >= 0 && test_bit(mp_irqs[idx].srcbus, mp_bus_not_pci)) {
		irq = mp_irqs[idx].srcbusirq;
		legacy = mp_is_legacy_irq(irq);
	}

	mutex_lock(&ioapic_mutex);
	if (!(flags & IOAPIC_MAP_ALLOC)) {
		if (!legacy) {
			irq = irq_find_mapping(domain, pin);
			if (irq == 0)
				irq = -ENOENT;
		}
	} else {
		ioapic_copy_alloc_attr(&tmp, info, gsi, ioapic, pin);
		if (legacy)
			irq = alloc_isa_irq_from_domain(domain, irq,
							ioapic, pin, &tmp);
		else if ((irq = irq_find_mapping(domain, pin)) == 0)
			irq = alloc_irq_from_domain(domain, ioapic, gsi, &tmp);
		else if (!mp_check_pin_attr(irq, &tmp))
			irq = -EBUSY;
		if (irq >= 0) {
			data = irq_get_chip_data(irq);
			data->count++;
		}
	}
	mutex_unlock(&ioapic_mutex);

	return irq;
}

static int pin_2_irq(int idx, int ioapic, int pin, unsigned int flags)
{
	u32 gsi = mp_pin_to_gsi(ioapic, pin);

	/*
	 * Debugging check, we are in big trouble if this message pops up!
	 */
	if (mp_irqs[idx].dstirq != pin)
		pr_err("broken BIOS or MPTABLE parser, ayiee!!\n");

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
				int irq = pirq_entries[pin-16];
				apic_printk(APIC_VERBOSE, KERN_DEBUG
						"using PIRQ%d -> IRQ %d\n",
						pin-16, irq);
				return irq;
			}
		}
	}
#endif

	return  mp_map_pin_to_irq(gsi, idx, ioapic, pin, flags, NULL);
}

int mp_map_gsi_to_irq(u32 gsi, unsigned int flags, struct irq_alloc_info *info)
{
	int ioapic, pin, idx;

	ioapic = mp_find_ioapic(gsi);
	if (ioapic < 0)
		return -ENODEV;

	pin = mp_find_ioapic_pin(ioapic, gsi);
	idx = find_irq_entry(ioapic, pin, mp_INT);
	if ((flags & IOAPIC_MAP_CHECK) && idx < 0)
		return -ENODEV;

	return mp_map_pin_to_irq(gsi, idx, ioapic, pin, flags, info);
}

void mp_unmap_irq(int irq)
{
	struct irq_data *irq_data = irq_get_irq_data(irq);
	struct mp_chip_data *data;

	if (!irq_data || !irq_data->domain)
		return;

	data = irq_data->chip_data;
	if (!data || data->isa_irq)
		return;

	mutex_lock(&ioapic_mutex);
	if (--data->count == 0)
		irq_domain_free_irqs(irq, 1);
	mutex_unlock(&ioapic_mutex);
}

/*
 * Find a specific PCI IRQ entry.
 * Not an __init, possibly needed by modules
 */
int IO_APIC_get_PCI_irq_vector(int bus, int slot, int pin)
{
	int irq, i, best_ioapic = -1, best_idx = -1;

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
		int ioapic_idx, found = 0;

		if (bus != lbus || mp_irqs[i].irqtype != mp_INT ||
		    slot != ((mp_irqs[i].srcbusirq >> 2) & 0x1f))
			continue;

		for_each_ioapic(ioapic_idx)
			if (mpc_ioapic_id(ioapic_idx) == mp_irqs[i].dstapic ||
			    mp_irqs[i].dstapic == MP_APIC_ALL) {
				found = 1;
				break;
			}
		if (!found)
			continue;

		/* Skip ISA IRQs */
		irq = pin_2_irq(i, ioapic_idx, mp_irqs[i].dstirq, 0);
		if (irq > 0 && !IO_APIC_IRQ(irq))
			continue;

		if (pin == (mp_irqs[i].srcbusirq & 3)) {
			best_idx = i;
			best_ioapic = ioapic_idx;
			goto out;
		}

		/*
		 * Use the first all-but-pin matching entry as a
		 * best-guess fuzzy result for broken mptables.
		 */
		if (best_idx < 0) {
			best_idx = i;
			best_ioapic = ioapic_idx;
		}
	}
	if (best_idx < 0)
		return -1;

out:
	return pin_2_irq(best_idx, best_ioapic, mp_irqs[best_idx].dstirq,
			 IOAPIC_MAP_ALLOC);
}
EXPORT_SYMBOL(IO_APIC_get_PCI_irq_vector);

static struct irq_chip ioapic_chip, ioapic_ir_chip;

static void __init setup_IO_APIC_irqs(void)
{
	unsigned int ioapic, pin;
	int idx;

	apic_printk(APIC_VERBOSE, KERN_DEBUG "init IO_APIC IRQs\n");

	for_each_ioapic_pin(ioapic, pin) {
		idx = find_irq_entry(ioapic, pin, mp_INT);
		if (idx < 0)
			apic_printk(APIC_VERBOSE,
				    KERN_DEBUG " apic %d pin %d not connected\n",
				    mpc_ioapic_id(ioapic), pin);
		else
			pin_2_irq(idx, ioapic, pin,
				  ioapic ? 0 : IOAPIC_MAP_ALLOC);
	}
}

void ioapic_zap_locks(void)
{
	raw_spin_lock_init(&ioapic_lock);
}

static void io_apic_print_entries(unsigned int apic, unsigned int nr_entries)
{
	int i;
	char buf[256];
	struct IO_APIC_route_entry entry;
	struct IR_IO_APIC_route_entry *ir_entry = (void *)&entry;

	printk(KERN_DEBUG "IOAPIC %d:\n", apic);
	for (i = 0; i <= nr_entries; i++) {
		entry = ioapic_read_entry(apic, i);
		snprintf(buf, sizeof(buf),
			 " pin%02x, %s, %s, %s, V(%02X), IRR(%1d), S(%1d)",
			 i,
			 entry.mask == IOAPIC_MASKED ? "disabled" : "enabled ",
			 entry.trigger == IOAPIC_LEVEL ? "level" : "edge ",
			 entry.polarity == IOAPIC_POL_LOW ? "low " : "high",
			 entry.vector, entry.irr, entry.delivery_status);
		if (ir_entry->format)
			printk(KERN_DEBUG "%s, remapped, I(%04X),  Z(%X)\n",
			       buf, (ir_entry->index2 << 15) | ir_entry->index,
			       ir_entry->zero);
		else
			printk(KERN_DEBUG "%s, %s, D(%02X), M(%1d)\n",
			       buf,
			       entry.dest_mode == IOAPIC_DEST_MODE_LOGICAL ?
			       "logical " : "physical",
			       entry.dest, entry.delivery_mode);
	}
}

static void __init print_IO_APIC(int ioapic_idx)
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
	io_apic_print_entries(ioapic_idx, reg_01.bits.entries);
}

void __init print_IO_APICs(void)
{
	int ioapic_idx;
	unsigned int irq;

	printk(KERN_DEBUG "number of MP IRQ sources: %d.\n", mp_irq_entries);
	for_each_ioapic(ioapic_idx)
		printk(KERN_DEBUG "number of IO-APIC #%d registers: %d.\n",
		       mpc_ioapic_id(ioapic_idx),
		       ioapics[ioapic_idx].nr_registers);

	/*
	 * We are a bit conservative about what we expect.  We have to
	 * know about every hardware change ASAP.
	 */
	printk(KERN_INFO "testing the IO APIC.......................\n");

	for_each_ioapic(ioapic_idx)
		print_IO_APIC(ioapic_idx);

	printk(KERN_DEBUG "IRQ to pin mappings:\n");
	for_each_active_irq(irq) {
		struct irq_pin_list *entry;
		struct irq_chip *chip;
		struct mp_chip_data *data;

		chip = irq_get_chip(irq);
		if (chip != &ioapic_chip && chip != &ioapic_ir_chip)
			continue;
		data = irq_get_chip_data(irq);
		if (!data)
			continue;
		if (list_empty(&data->irq_2_pin))
			continue;

		printk(KERN_DEBUG "IRQ%d ", irq);
		for_each_irq_pin(entry, data->irq_2_pin)
			pr_cont("-> %d:%d", entry->apic, entry->pin);
		pr_cont("\n");
	}

	printk(KERN_INFO ".................................... done.\n");
}

/* Where if anywhere is the i8259 connect in external int mode */
static struct { int pin, apic; } ioapic_i8259 = { -1, -1 };

void __init enable_IO_APIC(void)
{
	int i8259_apic, i8259_pin;
	int apic, pin;

	if (skip_ioapic_setup)
		nr_ioapics = 0;

	if (!nr_legacy_irqs() || !nr_ioapics)
		return;

	for_each_ioapic_pin(apic, pin) {
		/* See if any of the pins is in ExtINT mode */
		struct IO_APIC_route_entry entry = ioapic_read_entry(apic, pin);

		/* If the interrupt line is enabled and in ExtInt mode
		 * I have found the pin where the i8259 is connected.
		 */
		if ((entry.mask == 0) && (entry.delivery_mode == dest_ExtINT)) {
			ioapic_i8259.apic = apic;
			ioapic_i8259.pin  = pin;
			goto found_i8259;
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

void native_restore_boot_irq_mode(void)
{
	/*
	 * If the i8259 is routed through an IOAPIC
	 * Put that IOAPIC in virtual wire mode
	 * so legacy interrupts can be delivered.
	 */
	if (ioapic_i8259.pin != -1) {
		struct IO_APIC_route_entry entry;

		memset(&entry, 0, sizeof(entry));
		entry.mask		= IOAPIC_UNMASKED;
		entry.trigger		= IOAPIC_EDGE;
		entry.polarity		= IOAPIC_POL_HIGH;
		entry.dest_mode		= IOAPIC_DEST_MODE_PHYSICAL;
		entry.delivery_mode	= dest_ExtINT;
		entry.dest		= read_apic_id();

		/*
		 * Add it to the IO-APIC irq-routing table:
		 */
		ioapic_write_entry(ioapic_i8259.apic, ioapic_i8259.pin, entry);
	}

	if (boot_cpu_has(X86_FEATURE_APIC) || apic_from_smp_config())
		disconnect_bsp_APIC(ioapic_i8259.pin != -1);
}

void restore_boot_irq_mode(void)
{
	if (!nr_legacy_irqs())
		return;

	x86_apic_ops.restore();
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
	for_each_ioapic(ioapic_idx) {
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
		|| APIC_XAPIC(boot_cpu_apic_version))
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

static void __init delay_with_tsc(void)
{
	unsigned long long start, now;
	unsigned long end = jiffies + 4;

	start = rdtsc();

	/*
	 * We don't know the TSC frequency yet, but waiting for
	 * 40000000000/HZ TSC cycles is safe:
	 * 4 GHz == 10 jiffies
	 * 1 GHz == 40 jiffies
	 */
	do {
		rep_nop();
		now = rdtsc();
	} while ((now - start) < 40000000000ULL / HZ &&
		time_before_eq(jiffies, end));
}

static void __init delay_without_tsc(void)
{
	unsigned long end = jiffies + 4;
	int band = 1;

	/*
	 * We don't know any frequency yet, but waiting for
	 * 40940000000/HZ cycles is safe:
	 * 4 GHz == 10 jiffies
	 * 1 GHz == 40 jiffies
	 * 1 << 1 + 1 << 2 +...+ 1 << 11 = 4094
	 */
	do {
		__delay(((1U << band++) * 10000000UL) / HZ);
	} while (band < 12 && time_before_eq(jiffies, end));
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

	if (no_timer_check)
		return 1;

	local_save_flags(flags);
	local_irq_enable();

	if (boot_cpu_has(X86_FEATURE_TSC))
		delay_with_tsc();
	else
		delay_without_tsc();

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
	if (irq < nr_legacy_irqs()) {
		legacy_pic->mask(irq);
		if (legacy_pic->irq_pending(irq))
			was_pending = 1;
	}
	__unmask_ioapic(data->chip_data);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return was_pending;
}

atomic_t irq_mis_count;

#ifdef CONFIG_GENERIC_PENDING_IRQ
static bool io_apic_level_ack_pending(struct mp_chip_data *data)
{
	struct irq_pin_list *entry;
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	for_each_irq_pin(entry, data->irq_2_pin) {
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

static inline bool ioapic_irqd_mask(struct irq_data *data)
{
	/* If we are moving the irq we need to mask it */
	if (unlikely(irqd_is_setaffinity_pending(data))) {
		mask_ioapic_irq(data);
		return true;
	}
	return false;
}

static inline void ioapic_irqd_unmask(struct irq_data *data, bool masked)
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
		if (!io_apic_level_ack_pending(data->chip_data))
			irq_move_masked_irq(data);
		unmask_ioapic_irq(data);
	}
}
#else
static inline bool ioapic_irqd_mask(struct irq_data *data)
{
	return false;
}
static inline void ioapic_irqd_unmask(struct irq_data *data, bool masked)
{
}
#endif

static void ioapic_ack_level(struct irq_data *irq_data)
{
	struct irq_cfg *cfg = irqd_cfg(irq_data);
	unsigned long v;
	bool masked;
	int i;

	irq_complete_move(cfg);
	masked = ioapic_irqd_mask(irq_data);

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
		eoi_ioapic_pin(cfg->vector, irq_data->chip_data);
	}

	ioapic_irqd_unmask(irq_data, masked);
}

static void ioapic_ir_ack_level(struct irq_data *irq_data)
{
	struct mp_chip_data *data = irq_data->chip_data;

	/*
	 * Intr-remapping uses pin number as the virtual vector
	 * in the RTE. Actual vector is programmed in
	 * intr-remapping table entry. Hence for the io-apic
	 * EOI we use the pin number.
	 */
	apic_ack_irq(irq_data);
	eoi_ioapic_pin(data->entry.vector, data);
}

static void ioapic_configure_entry(struct irq_data *irqd)
{
	struct mp_chip_data *mpd = irqd->chip_data;
	struct irq_cfg *cfg = irqd_cfg(irqd);
	struct irq_pin_list *entry;

	/*
	 * Only update when the parent is the vector domain, don't touch it
	 * if the parent is the remapping domain. Check the installed
	 * ioapic chip to verify that.
	 */
	if (irqd->chip == &ioapic_chip) {
		mpd->entry.dest = cfg->dest_apicid;
		mpd->entry.vector = cfg->vector;
	}
	for_each_irq_pin(entry, mpd->irq_2_pin)
		__ioapic_write_entry(entry->apic, entry->pin, mpd->entry);
}

static int ioapic_set_affinity(struct irq_data *irq_data,
			       const struct cpumask *mask, bool force)
{
	struct irq_data *parent = irq_data->parent_data;
	unsigned long flags;
	int ret;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	raw_spin_lock_irqsave(&ioapic_lock, flags);
	if (ret >= 0 && ret != IRQ_SET_MASK_OK_DONE)
		ioapic_configure_entry(irq_data);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);

	return ret;
}

static struct irq_chip ioapic_chip __read_mostly = {
	.name			= "IO-APIC",
	.irq_startup		= startup_ioapic_irq,
	.irq_mask		= mask_ioapic_irq,
	.irq_unmask		= unmask_ioapic_irq,
	.irq_ack		= irq_chip_ack_parent,
	.irq_eoi		= ioapic_ack_level,
	.irq_set_affinity	= ioapic_set_affinity,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

static struct irq_chip ioapic_ir_chip __read_mostly = {
	.name			= "IR-IO-APIC",
	.irq_startup		= startup_ioapic_irq,
	.irq_mask		= mask_ioapic_irq,
	.irq_unmask		= unmask_ioapic_irq,
	.irq_ack		= irq_chip_ack_parent,
	.irq_eoi		= ioapic_ir_ack_level,
	.irq_set_affinity	= ioapic_set_affinity,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

static inline void init_IO_APIC_traps(void)
{
	struct irq_cfg *cfg;
	unsigned int irq;

	for_each_active_irq(irq) {
		cfg = irq_cfg(irq);
		if (IO_APIC_IRQ(irq) && cfg && !cfg->vector) {
			/*
			 * Hmm.. We don't have an entry for this,
			 * so default to an old-fashioned 8259
			 * interrupt if we can..
			 */
			if (irq < nr_legacy_irqs())
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

	entry1.dest_mode = IOAPIC_DEST_MODE_PHYSICAL;
	entry1.mask = IOAPIC_UNMASKED;
	entry1.dest = hard_smp_processor_id();
	entry1.delivery_mode = dest_ExtINT;
	entry1.polarity = entry0.polarity;
	entry1.trigger = IOAPIC_EDGE;
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

static int mp_alloc_timer_irq(int ioapic, int pin)
{
	int irq = -1;
	struct irq_domain *domain = mp_ioapic_irqdomain(ioapic);

	if (domain) {
		struct irq_alloc_info info;

		ioapic_set_alloc_attr(&info, NUMA_NO_NODE, 0, 0);
		info.ioapic_id = mpc_ioapic_id(ioapic);
		info.ioapic_pin = pin;
		mutex_lock(&ioapic_mutex);
		irq = alloc_isa_irq_from_domain(domain, 0, ioapic, pin, &info);
		mutex_unlock(&ioapic_mutex);
	}

	return irq;
}

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
	struct irq_data *irq_data = irq_get_irq_data(0);
	struct mp_chip_data *data = irq_data->chip_data;
	struct irq_cfg *cfg = irqd_cfg(irq_data);
	int node = cpu_to_node(0);
	int apic1, pin1, apic2, pin2;
	unsigned long flags;
	int no_pin1 = 0;

	local_irq_save(flags);

	/*
	 * get/set the timer IRQ vector:
	 */
	legacy_pic->mask(0);

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
		/* Ok, does IRQ0 through the IOAPIC work? */
		if (no_pin1) {
			mp_alloc_timer_irq(apic1, pin1);
		} else {
			/*
			 * for edge trigger, it's already unmasked,
			 * so only need to unmask if it is level-trigger
			 * do we really have level trigger timer?
			 */
			int idx;
			idx = find_irq_entry(apic1, pin1, mp_INT);
			if (idx != -1 && irq_trigger(idx))
				unmask_ioapic_irq(irq_get_irq_data(0));
		}
		irq_domain_deactivate_irq(irq_data);
		irq_domain_activate_irq(irq_data, false);
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
		replace_pin_at_irq_node(data, node, apic1, pin1, apic2, pin2);
		irq_domain_deactivate_irq(irq_data);
		irq_domain_activate_irq(irq_data, false);
		legacy_pic->unmask(0);
		if (timer_irq_works()) {
			apic_printk(APIC_QUIET, KERN_INFO "....... works.\n");
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
	if (apic_is_x2apic_enabled())
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

static int mp_irqdomain_create(int ioapic)
{
	struct irq_alloc_info info;
	struct irq_domain *parent;
	int hwirqs = mp_ioapic_pin_count(ioapic);
	struct ioapic *ip = &ioapics[ioapic];
	struct ioapic_domain_cfg *cfg = &ip->irqdomain_cfg;
	struct mp_ioapic_gsi *gsi_cfg = mp_ioapic_gsi_routing(ioapic);
	struct fwnode_handle *fn;
	char *name = "IO-APIC";

	if (cfg->type == IOAPIC_DOMAIN_INVALID)
		return 0;

	init_irq_alloc_info(&info, NULL);
	info.type = X86_IRQ_ALLOC_TYPE_IOAPIC;
	info.ioapic_id = mpc_ioapic_id(ioapic);
	parent = irq_remapping_get_ir_irq_domain(&info);
	if (!parent)
		parent = x86_vector_domain;
	else
		name = "IO-APIC-IR";

	/* Handle device tree enumerated APICs proper */
	if (cfg->dev) {
		fn = of_node_to_fwnode(cfg->dev);
	} else {
		fn = irq_domain_alloc_named_id_fwnode(name, ioapic);
		if (!fn)
			return -ENOMEM;
	}

	ip->irqdomain = irq_domain_create_linear(fn, hwirqs, cfg->ops,
						 (void *)(long)ioapic);

	/* Release fw handle if it was allocated above */
	if (!cfg->dev)
		irq_domain_free_fwnode(fn);

	if (!ip->irqdomain)
		return -ENOMEM;

	ip->irqdomain->parent = parent;

	if (cfg->type == IOAPIC_DOMAIN_LEGACY ||
	    cfg->type == IOAPIC_DOMAIN_STRICT)
		ioapic_dynirq_base = max(ioapic_dynirq_base,
					 gsi_cfg->gsi_end + 1);

	return 0;
}

static void ioapic_destroy_irqdomain(int idx)
{
	if (ioapics[idx].irqdomain) {
		irq_domain_remove(ioapics[idx].irqdomain);
		ioapics[idx].irqdomain = NULL;
	}
}

void __init setup_IO_APIC(void)
{
	int ioapic;

	if (skip_ioapic_setup || !nr_ioapics)
		return;

	io_apic_irqs = nr_legacy_irqs() ? ~PIC_IRQS : ~0UL;

	apic_printk(APIC_VERBOSE, "ENABLING IO-APIC IRQs\n");
	for_each_ioapic(ioapic)
		BUG_ON(mp_irqdomain_create(ioapic));

	/*
         * Set up IO-APIC IRQ routing.
         */
	x86_init.mpparse.setup_ioapic_ids();

	sync_Arb_IDs();
	setup_IO_APIC_irqs();
	init_IO_APIC_traps();
	if (nr_legacy_irqs())
		check_timer();

	ioapic_initialized = 1;
}

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

	for_each_ioapic_reverse(ioapic_idx)
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

static int io_apic_get_redir_entries(int ioapic)
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

unsigned int arch_dynirq_lower_bound(unsigned int from)
{
	/*
	 * dmar_alloc_hwirq() may be called before setup_IO_APIC(), so use
	 * gsi_top if ioapic_dynirq_base hasn't been initialized yet.
	 */
	return ioapic_initialized ? ioapic_dynirq_base : gsi_top;
}

#ifdef CONFIG_X86_32
static int io_apic_get_unique_id(int ioapic, int apic_id)
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

static u8 io_apic_unique_id(int idx, u8 id)
{
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) &&
	    !APIC_XAPIC(boot_cpu_apic_version))
		return io_apic_get_unique_id(idx, id);
	else
		return id;
}
#else
static u8 io_apic_unique_id(int idx, u8 id)
{
	union IO_APIC_reg_00 reg_00;
	DECLARE_BITMAP(used, 256);
	unsigned long flags;
	u8 new_id;
	int i;

	bitmap_zero(used, 256);
	for_each_ioapic(i)
		__set_bit(mpc_ioapic_id(i), used);

	/* Hand out the requested id if available */
	if (!test_bit(id, used))
		return id;

	/*
	 * Read the current id from the ioapic and keep it if
	 * available.
	 */
	raw_spin_lock_irqsave(&ioapic_lock, flags);
	reg_00.raw = io_apic_read(idx, 0);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
	new_id = reg_00.bits.ID;
	if (!test_bit(new_id, used)) {
		apic_printk(APIC_VERBOSE, KERN_INFO
			"IOAPIC[%d]: Using reg apic_id %d instead of %d\n",
			 idx, new_id, id);
		return new_id;
	}

	/*
	 * Get the next free id and write it to the ioapic.
	 */
	new_id = find_first_zero_bit(used, 256);
	reg_00.bits.ID = new_id;
	raw_spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_write(idx, 0, reg_00.raw);
	reg_00.raw = io_apic_read(idx, 0);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
	/* Sanity check */
	BUG_ON(reg_00.bits.ID != new_id);

	return new_id;
}
#endif

static int io_apic_get_version(int ioapic)
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
 * This function updates target affinity of IOAPIC interrupts to include
 * the CPUs which came online during SMP bringup.
 */
#define IOAPIC_RESOURCE_NAME_SIZE 11

static struct resource *ioapic_resources;

static struct resource * __init ioapic_setup_resources(void)
{
	unsigned long n;
	struct resource *res;
	char *mem;
	int i;

	if (nr_ioapics == 0)
		return NULL;

	n = IOAPIC_RESOURCE_NAME_SIZE + sizeof(struct resource);
	n *= nr_ioapics;

	mem = alloc_bootmem(n);
	res = (void *)mem;

	mem += sizeof(struct resource) * nr_ioapics;

	for_each_ioapic(i) {
		res[i].name = mem;
		res[i].flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		snprintf(mem, IOAPIC_RESOURCE_NAME_SIZE, "IOAPIC %u", i);
		mem += IOAPIC_RESOURCE_NAME_SIZE;
		ioapics[i].iomem_res = &res[i];
	}

	ioapic_resources = res;

	return res;
}

void __init io_apic_init_mappings(void)
{
	unsigned long ioapic_phys, idx = FIX_IO_APIC_BASE_0;
	struct resource *ioapic_res;
	int i;

	ioapic_res = ioapic_setup_resources();
	for_each_ioapic(i) {
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

	for_each_ioapic(i) {
		insert_resource(&iomem_resource, r);
		r++;
	}
}

int mp_find_ioapic(u32 gsi)
{
	int i;

	if (nr_ioapics == 0)
		return -1;

	/* Find the IOAPIC that manages this GSI. */
	for_each_ioapic(i) {
		struct mp_ioapic_gsi *gsi_cfg = mp_ioapic_gsi_routing(i);
		if (gsi >= gsi_cfg->gsi_base && gsi <= gsi_cfg->gsi_end)
			return i;
	}

	printk(KERN_ERR "ERROR: Unable to locate IOAPIC for GSI %d\n", gsi);
	return -1;
}

int mp_find_ioapic_pin(int ioapic, u32 gsi)
{
	struct mp_ioapic_gsi *gsi_cfg;

	if (WARN_ON(ioapic < 0))
		return -1;

	gsi_cfg = mp_ioapic_gsi_routing(ioapic);
	if (WARN_ON(gsi > gsi_cfg->gsi_end))
		return -1;

	return gsi - gsi_cfg->gsi_base;
}

static int bad_ioapic_register(int idx)
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

static int find_free_ioapic_entry(void)
{
	int idx;

	for (idx = 0; idx < MAX_IO_APICS; idx++)
		if (ioapics[idx].nr_registers == 0)
			return idx;

	return MAX_IO_APICS;
}

/**
 * mp_register_ioapic - Register an IOAPIC device
 * @id:		hardware IOAPIC ID
 * @address:	physical address of IOAPIC register area
 * @gsi_base:	base of GSI associated with the IOAPIC
 * @cfg:	configuration information for the IOAPIC
 */
int mp_register_ioapic(int id, u32 address, u32 gsi_base,
		       struct ioapic_domain_cfg *cfg)
{
	bool hotplug = !!ioapic_initialized;
	struct mp_ioapic_gsi *gsi_cfg;
	int idx, ioapic, entries;
	u32 gsi_end;

	if (!address) {
		pr_warn("Bogus (zero) I/O APIC address found, skipping!\n");
		return -EINVAL;
	}
	for_each_ioapic(ioapic)
		if (ioapics[ioapic].mp_config.apicaddr == address) {
			pr_warn("address 0x%x conflicts with IOAPIC%d\n",
				address, ioapic);
			return -EEXIST;
		}

	idx = find_free_ioapic_entry();
	if (idx >= MAX_IO_APICS) {
		pr_warn("Max # of I/O APICs (%d) exceeded (found %d), skipping\n",
			MAX_IO_APICS, idx);
		return -ENOSPC;
	}

	ioapics[idx].mp_config.type = MP_IOAPIC;
	ioapics[idx].mp_config.flags = MPC_APIC_USABLE;
	ioapics[idx].mp_config.apicaddr = address;

	set_fixmap_nocache(FIX_IO_APIC_BASE_0 + idx, address);
	if (bad_ioapic_register(idx)) {
		clear_fixmap(FIX_IO_APIC_BASE_0 + idx);
		return -ENODEV;
	}

	ioapics[idx].mp_config.apicid = io_apic_unique_id(idx, id);
	ioapics[idx].mp_config.apicver = io_apic_get_version(idx);

	/*
	 * Build basic GSI lookup table to facilitate gsi->io_apic lookups
	 * and to prevent reprogramming of IOAPIC pins (PCI GSIs).
	 */
	entries = io_apic_get_redir_entries(idx);
	gsi_end = gsi_base + entries - 1;
	for_each_ioapic(ioapic) {
		gsi_cfg = mp_ioapic_gsi_routing(ioapic);
		if ((gsi_base >= gsi_cfg->gsi_base &&
		     gsi_base <= gsi_cfg->gsi_end) ||
		    (gsi_end >= gsi_cfg->gsi_base &&
		     gsi_end <= gsi_cfg->gsi_end)) {
			pr_warn("GSI range [%u-%u] for new IOAPIC conflicts with GSI[%u-%u]\n",
				gsi_base, gsi_end,
				gsi_cfg->gsi_base, gsi_cfg->gsi_end);
			clear_fixmap(FIX_IO_APIC_BASE_0 + idx);
			return -ENOSPC;
		}
	}
	gsi_cfg = mp_ioapic_gsi_routing(idx);
	gsi_cfg->gsi_base = gsi_base;
	gsi_cfg->gsi_end = gsi_end;

	ioapics[idx].irqdomain = NULL;
	ioapics[idx].irqdomain_cfg = *cfg;

	/*
	 * If mp_register_ioapic() is called during early boot stage when
	 * walking ACPI/SFI/DT tables, it's too early to create irqdomain,
	 * we are still using bootmem allocator. So delay it to setup_IO_APIC().
	 */
	if (hotplug) {
		if (mp_irqdomain_create(idx)) {
			clear_fixmap(FIX_IO_APIC_BASE_0 + idx);
			return -ENOMEM;
		}
		alloc_ioapic_saved_registers(idx);
	}

	if (gsi_cfg->gsi_end >= gsi_top)
		gsi_top = gsi_cfg->gsi_end + 1;
	if (nr_ioapics <= idx)
		nr_ioapics = idx + 1;

	/* Set nr_registers to mark entry present */
	ioapics[idx].nr_registers = entries;

	pr_info("IOAPIC[%d]: apic_id %d, version %d, address 0x%x, GSI %d-%d\n",
		idx, mpc_ioapic_id(idx),
		mpc_ioapic_ver(idx), mpc_ioapic_addr(idx),
		gsi_cfg->gsi_base, gsi_cfg->gsi_end);

	return 0;
}

int mp_unregister_ioapic(u32 gsi_base)
{
	int ioapic, pin;
	int found = 0;

	for_each_ioapic(ioapic)
		if (ioapics[ioapic].gsi_config.gsi_base == gsi_base) {
			found = 1;
			break;
		}
	if (!found) {
		pr_warn("can't find IOAPIC for GSI %d\n", gsi_base);
		return -ENODEV;
	}

	for_each_pin(ioapic, pin) {
		u32 gsi = mp_pin_to_gsi(ioapic, pin);
		int irq = mp_map_gsi_to_irq(gsi, 0, NULL);
		struct mp_chip_data *data;

		if (irq >= 0) {
			data = irq_get_chip_data(irq);
			if (data && data->count) {
				pr_warn("pin%d on IOAPIC%d is still in use.\n",
					pin, ioapic);
				return -EBUSY;
			}
		}
	}

	/* Mark entry not present */
	ioapics[ioapic].nr_registers  = 0;
	ioapic_destroy_irqdomain(ioapic);
	free_ioapic_saved_registers(ioapic);
	if (ioapics[ioapic].iomem_res)
		release_resource(ioapics[ioapic].iomem_res);
	clear_fixmap(FIX_IO_APIC_BASE_0 + ioapic);
	memset(&ioapics[ioapic], 0, sizeof(ioapics[ioapic]));

	return 0;
}

int mp_ioapic_registered(u32 gsi_base)
{
	int ioapic;

	for_each_ioapic(ioapic)
		if (ioapics[ioapic].gsi_config.gsi_base == gsi_base)
			return 1;

	return 0;
}

static void mp_irqdomain_get_attr(u32 gsi, struct mp_chip_data *data,
				  struct irq_alloc_info *info)
{
	if (info && info->ioapic_valid) {
		data->trigger = info->ioapic_trigger;
		data->polarity = info->ioapic_polarity;
	} else if (acpi_get_override_irq(gsi, &data->trigger,
					 &data->polarity) < 0) {
		/* PCI interrupts are always active low level triggered. */
		data->trigger = IOAPIC_LEVEL;
		data->polarity = IOAPIC_POL_LOW;
	}
}

static void mp_setup_entry(struct irq_cfg *cfg, struct mp_chip_data *data,
			   struct IO_APIC_route_entry *entry)
{
	memset(entry, 0, sizeof(*entry));
	entry->delivery_mode = apic->irq_delivery_mode;
	entry->dest_mode     = apic->irq_dest_mode;
	entry->dest	     = cfg->dest_apicid;
	entry->vector	     = cfg->vector;
	entry->trigger	     = data->trigger;
	entry->polarity	     = data->polarity;
	/*
	 * Mask level triggered irqs. Edge triggered irqs are masked
	 * by the irq core code in case they fire.
	 */
	if (data->trigger == IOAPIC_LEVEL)
		entry->mask = IOAPIC_MASKED;
	else
		entry->mask = IOAPIC_UNMASKED;
}

int mp_irqdomain_alloc(struct irq_domain *domain, unsigned int virq,
		       unsigned int nr_irqs, void *arg)
{
	int ret, ioapic, pin;
	struct irq_cfg *cfg;
	struct irq_data *irq_data;
	struct mp_chip_data *data;
	struct irq_alloc_info *info = arg;
	unsigned long flags;

	if (!info || nr_irqs > 1)
		return -EINVAL;
	irq_data = irq_domain_get_irq_data(domain, virq);
	if (!irq_data)
		return -EINVAL;

	ioapic = mp_irqdomain_ioapic_idx(domain);
	pin = info->ioapic_pin;
	if (irq_find_mapping(domain, (irq_hw_number_t)pin) > 0)
		return -EEXIST;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	info->ioapic_entry = &data->entry;
	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, info);
	if (ret < 0) {
		kfree(data);
		return ret;
	}

	INIT_LIST_HEAD(&data->irq_2_pin);
	irq_data->hwirq = info->ioapic_pin;
	irq_data->chip = (domain->parent == x86_vector_domain) ?
			  &ioapic_chip : &ioapic_ir_chip;
	irq_data->chip_data = data;
	mp_irqdomain_get_attr(mp_pin_to_gsi(ioapic, pin), data, info);

	cfg = irqd_cfg(irq_data);
	add_pin_to_irq_node(data, ioapic_alloc_attr_node(info), ioapic, pin);

	local_irq_save(flags);
	if (info->ioapic_entry)
		mp_setup_entry(cfg, data, info->ioapic_entry);
	mp_register_handler(virq, data->trigger);
	if (virq < nr_legacy_irqs())
		legacy_pic->mask(virq);
	local_irq_restore(flags);

	apic_printk(APIC_VERBOSE, KERN_DEBUG
		    "IOAPIC[%d]: Set routing entry (%d-%d -> 0x%x -> IRQ %d Mode:%i Active:%i Dest:%d)\n",
		    ioapic, mpc_ioapic_id(ioapic), pin, cfg->vector,
		    virq, data->trigger, data->polarity, cfg->dest_apicid);

	return 0;
}

void mp_irqdomain_free(struct irq_domain *domain, unsigned int virq,
		       unsigned int nr_irqs)
{
	struct irq_data *irq_data;
	struct mp_chip_data *data;

	BUG_ON(nr_irqs != 1);
	irq_data = irq_domain_get_irq_data(domain, virq);
	if (irq_data && irq_data->chip_data) {
		data = irq_data->chip_data;
		__remove_pin_from_irq(data, mp_irqdomain_ioapic_idx(domain),
				      (int)irq_data->hwirq);
		WARN_ON(!list_empty(&data->irq_2_pin));
		kfree(irq_data->chip_data);
	}
	irq_domain_free_irqs_top(domain, virq, nr_irqs);
}

int mp_irqdomain_activate(struct irq_domain *domain,
			  struct irq_data *irq_data, bool reserve)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&ioapic_lock, flags);
	ioapic_configure_entry(irq_data);
	raw_spin_unlock_irqrestore(&ioapic_lock, flags);
	return 0;
}

void mp_irqdomain_deactivate(struct irq_domain *domain,
			     struct irq_data *irq_data)
{
	/* It won't be called for IRQ with multiple IOAPIC pins associated */
	ioapic_mask_entry(mp_irqdomain_ioapic_idx(domain),
			  (int)irq_data->hwirq);
}

int mp_irqdomain_ioapic_idx(struct irq_domain *domain)
{
	return (int)(long)domain->host_data;
}

const struct irq_domain_ops mp_ioapic_irqdomain_ops = {
	.alloc		= mp_irqdomain_alloc,
	.free		= mp_irqdomain_free,
	.activate	= mp_irqdomain_activate,
	.deactivate	= mp_irqdomain_deactivate,
};
