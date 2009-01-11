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
#include <asm/uv/uv_hub.h>
#include <asm/uv/uv_irq.h>

#include <mach_ipi.h>
#include <mach_apic.h>
#include <mach_apicdef.h>

#define __apicdebuginit(type) static type __init

/*
 *      Is the SiS APIC rmw bug present ?
 *      -1 = don't know, 0 = no, 1 = yes
 */
int sis_apic_bug = -1;

static DEFINE_SPINLOCK(ioapic_lock);
static DEFINE_SPINLOCK(vector_lock);

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

int skip_ioapic_setup;

static int __init parse_noapic(char *str)
{
	/* disable IO-APIC */
	disable_ioapic_setup();
	return 0;
}
early_param("noapic", parse_noapic);

struct irq_pin_list;

/*
 * This is performance-critical, we want to do it O(1)
 *
 * the indexing order of this array favors 1:1 mappings
 * between pins and IRQs.
 */

struct irq_pin_list {
	int apic, pin;
	struct irq_pin_list *next;
};

static struct irq_pin_list *get_one_free_irq_2_pin(int cpu)
{
	struct irq_pin_list *pin;
	int node;

	node = cpu_to_node(cpu);

	pin = kzalloc_node(sizeof(*pin), GFP_ATOMIC, node);

	return pin;
}

struct irq_cfg {
	struct irq_pin_list *irq_2_pin;
	cpumask_var_t domain;
	cpumask_var_t old_domain;
	unsigned move_cleanup_count;
	u8 vector;
	u8 move_in_progress : 1;
#ifdef CONFIG_NUMA_MIGRATE_IRQ_DESC
	u8 move_desc_pending : 1;
#endif
};

/* irq_cfg is indexed by the sum of all RTEs in all I/O APICs. */
#ifdef CONFIG_SPARSE_IRQ
static struct irq_cfg irq_cfgx[] = {
#else
static struct irq_cfg irq_cfgx[NR_IRQS] = {
#endif
	[0]  = { .vector = IRQ0_VECTOR,  },
	[1]  = { .vector = IRQ1_VECTOR,  },
	[2]  = { .vector = IRQ2_VECTOR,  },
	[3]  = { .vector = IRQ3_VECTOR,  },
	[4]  = { .vector = IRQ4_VECTOR,  },
	[5]  = { .vector = IRQ5_VECTOR,  },
	[6]  = { .vector = IRQ6_VECTOR,  },
	[7]  = { .vector = IRQ7_VECTOR,  },
	[8]  = { .vector = IRQ8_VECTOR,  },
	[9]  = { .vector = IRQ9_VECTOR,  },
	[10] = { .vector = IRQ10_VECTOR, },
	[11] = { .vector = IRQ11_VECTOR, },
	[12] = { .vector = IRQ12_VECTOR, },
	[13] = { .vector = IRQ13_VECTOR, },
	[14] = { .vector = IRQ14_VECTOR, },
	[15] = { .vector = IRQ15_VECTOR, },
};

int __init arch_early_irq_init(void)
{
	struct irq_cfg *cfg;
	struct irq_desc *desc;
	int count;
	int i;

	cfg = irq_cfgx;
	count = ARRAY_SIZE(irq_cfgx);

	for (i = 0; i < count; i++) {
		desc = irq_to_desc(i);
		desc->chip_data = &cfg[i];
		alloc_bootmem_cpumask_var(&cfg[i].domain);
		alloc_bootmem_cpumask_var(&cfg[i].old_domain);
		if (i < NR_IRQS_LEGACY)
			cpumask_setall(cfg[i].domain);
	}

	return 0;
}

#ifdef CONFIG_SPARSE_IRQ
static struct irq_cfg *irq_cfg(unsigned int irq)
{
	struct irq_cfg *cfg = NULL;
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	if (desc)
		cfg = desc->chip_data;

	return cfg;
}

static struct irq_cfg *get_one_free_irq_cfg(int cpu)
{
	struct irq_cfg *cfg;
	int node;

	node = cpu_to_node(cpu);

	cfg = kzalloc_node(sizeof(*cfg), GFP_ATOMIC, node);
	if (cfg) {
		if (!alloc_cpumask_var_node(&cfg->domain, GFP_ATOMIC, node)) {
			kfree(cfg);
			cfg = NULL;
		} else if (!alloc_cpumask_var_node(&cfg->old_domain,
							  GFP_ATOMIC, node)) {
			free_cpumask_var(cfg->domain);
			kfree(cfg);
			cfg = NULL;
		} else {
			cpumask_clear(cfg->domain);
			cpumask_clear(cfg->old_domain);
		}
	}

	return cfg;
}

int arch_init_chip_data(struct irq_desc *desc, int cpu)
{
	struct irq_cfg *cfg;

	cfg = desc->chip_data;
	if (!cfg) {
		desc->chip_data = get_one_free_irq_cfg(cpu);
		if (!desc->chip_data) {
			printk(KERN_ERR "can not alloc irq_cfg\n");
			BUG_ON(1);
		}
	}

	return 0;
}

#ifdef CONFIG_NUMA_MIGRATE_IRQ_DESC

static void
init_copy_irq_2_pin(struct irq_cfg *old_cfg, struct irq_cfg *cfg, int cpu)
{
	struct irq_pin_list *old_entry, *head, *tail, *entry;

	cfg->irq_2_pin = NULL;
	old_entry = old_cfg->irq_2_pin;
	if (!old_entry)
		return;

	entry = get_one_free_irq_2_pin(cpu);
	if (!entry)
		return;

	entry->apic	= old_entry->apic;
	entry->pin	= old_entry->pin;
	head		= entry;
	tail		= entry;
	old_entry	= old_entry->next;
	while (old_entry) {
		entry = get_one_free_irq_2_pin(cpu);
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
				 struct irq_desc *desc, int cpu)
{
	struct irq_cfg *cfg;
	struct irq_cfg *old_cfg;

	cfg = get_one_free_irq_cfg(cpu);

	if (!cfg)
		return;

	desc->chip_data = cfg;

	old_cfg = old_desc->chip_data;

	memcpy(cfg, old_cfg, sizeof(struct irq_cfg));

	init_copy_irq_2_pin(old_cfg, cfg, cpu);
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

static void
set_extra_move_desc(struct irq_desc *desc, const struct cpumask *mask)
{
	struct irq_cfg *cfg = desc->chip_data;

	if (!cfg->move_in_progress) {
		/* it means that domain is not changed */
		if (!cpumask_intersects(&desc->affinity, mask))
			cfg->move_desc_pending = 1;
	}
}
#endif

#else
static struct irq_cfg *irq_cfg(unsigned int irq)
{
	return irq < nr_irqs ? irq_cfgx + irq : NULL;
}

#endif

#ifndef CONFIG_NUMA_MIGRATE_IRQ_DESC
static inline void
set_extra_move_desc(struct irq_desc *desc, const struct cpumask *mask)
{
}
#endif

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
	struct io_apic __iomem *io_apic = io_apic_base(apic);

	if (sis_apic_bug)
		writel(reg, &io_apic->index);
	writel(value, &io_apic->data);
}

static bool io_apic_level_ack_pending(struct irq_cfg *cfg)
{
	struct irq_pin_list *entry;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	entry = cfg->irq_2_pin;
	for (;;) {
		unsigned int reg;
		int pin;

		if (!entry)
			break;
		pin = entry->pin;
		reg = io_apic_read(entry->apic, 0x10 + pin*2);
		/* Is the remote IRR bit set? */
		if (reg & IO_APIC_REDIR_REMOTE_IRR) {
			spin_unlock_irqrestore(&ioapic_lock, flags);
			return true;
		}
		if (!entry->next)
			break;
		entry = entry->next;
	}
	spin_unlock_irqrestore(&ioapic_lock, flags);

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
static void send_cleanup_vector(struct irq_cfg *cfg)
{
	cpumask_var_t cleanup_mask;

	if (unlikely(!alloc_cpumask_var(&cleanup_mask, GFP_ATOMIC))) {
		unsigned int i;
		cfg->move_cleanup_count = 0;
		for_each_cpu_and(i, cfg->old_domain, cpu_online_mask)
			cfg->move_cleanup_count++;
		for_each_cpu_and(i, cfg->old_domain, cpu_online_mask)
			send_IPI_mask(cpumask_of(i), IRQ_MOVE_CLEANUP_VECTOR);
	} else {
		cpumask_and(cleanup_mask, cfg->old_domain, cpu_online_mask);
		cfg->move_cleanup_count = cpumask_weight(cleanup_mask);
		send_IPI_mask(cleanup_mask, IRQ_MOVE_CLEANUP_VECTOR);
		free_cpumask_var(cleanup_mask);
	}
	cfg->move_in_progress = 0;
}

static void __target_IO_APIC_irq(unsigned int irq, unsigned int dest, struct irq_cfg *cfg)
{
	int apic, pin;
	struct irq_pin_list *entry;
	u8 vector = cfg->vector;

	entry = cfg->irq_2_pin;
	for (;;) {
		unsigned int reg;

		if (!entry)
			break;

		apic = entry->apic;
		pin = entry->pin;
#ifdef CONFIG_INTR_REMAP
		/*
		 * With interrupt-remapping, destination information comes
		 * from interrupt-remapping table entry.
		 */
		if (!irq_remapped(irq))
			io_apic_write(apic, 0x11 + pin*2, dest);
#else
		io_apic_write(apic, 0x11 + pin*2, dest);
#endif
		reg = io_apic_read(apic, 0x10 + pin*2);
		reg &= ~IO_APIC_REDIR_VECTOR_MASK;
		reg |= vector;
		io_apic_modify(apic, 0x10 + pin*2, reg);
		if (!entry->next)
			break;
		entry = entry->next;
	}
}

static int
assign_irq_vector(int irq, struct irq_cfg *cfg, const struct cpumask *mask);

/*
 * Either sets desc->affinity to a valid value, and returns cpu_mask_to_apicid
 * of that, or returns BAD_APICID and leaves desc->affinity untouched.
 */
static unsigned int
set_desc_affinity(struct irq_desc *desc, const struct cpumask *mask)
{
	struct irq_cfg *cfg;
	unsigned int irq;

	if (!cpumask_intersects(mask, cpu_online_mask))
		return BAD_APICID;

	irq = desc->irq;
	cfg = desc->chip_data;
	if (assign_irq_vector(irq, cfg, mask))
		return BAD_APICID;

	cpumask_and(&desc->affinity, cfg->domain, mask);
	set_extra_move_desc(desc, mask);
	return cpu_mask_to_apicid_and(&desc->affinity, cpu_online_mask);
}

static void
set_ioapic_affinity_irq_desc(struct irq_desc *desc, const struct cpumask *mask)
{
	struct irq_cfg *cfg;
	unsigned long flags;
	unsigned int dest;
	unsigned int irq;

	irq = desc->irq;
	cfg = desc->chip_data;

	spin_lock_irqsave(&ioapic_lock, flags);
	dest = set_desc_affinity(desc, mask);
	if (dest != BAD_APICID) {
		/* Only the high 8 bits are valid. */
		dest = SET_APIC_LOGICAL_ID(dest);
		__target_IO_APIC_irq(irq, dest, cfg);
	}
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void
set_ioapic_affinity_irq(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);

	set_ioapic_affinity_irq_desc(desc, mask);
}
#endif /* CONFIG_SMP */

/*
 * The common case is 1:1 IRQ<->pin mappings. Sometimes there are
 * shared ISA-space IRQs, so we have to support them. We are super
 * fast in the common case, and fast for shared ISA-space IRQs.
 */
static void add_pin_to_irq_cpu(struct irq_cfg *cfg, int cpu, int apic, int pin)
{
	struct irq_pin_list *entry;

	entry = cfg->irq_2_pin;
	if (!entry) {
		entry = get_one_free_irq_2_pin(cpu);
		if (!entry) {
			printk(KERN_ERR "can not alloc irq_2_pin to add %d - %d\n",
					apic, pin);
			return;
		}
		cfg->irq_2_pin = entry;
		entry->apic = apic;
		entry->pin = pin;
		return;
	}

	while (entry->next) {
		/* not again, please */
		if (entry->apic == apic && entry->pin == pin)
			return;

		entry = entry->next;
	}

	entry->next = get_one_free_irq_2_pin(cpu);
	entry = entry->next;
	entry->apic = apic;
	entry->pin = pin;
}

/*
 * Reroute an IRQ to a different pin.
 */
static void __init replace_pin_at_irq_cpu(struct irq_cfg *cfg, int cpu,
				      int oldapic, int oldpin,
				      int newapic, int newpin)
{
	struct irq_pin_list *entry = cfg->irq_2_pin;
	int replaced = 0;

	while (entry) {
		if (entry->apic == oldapic && entry->pin == oldpin) {
			entry->apic = newapic;
			entry->pin = newpin;
			replaced = 1;
			/* every one is different, right? */
			break;
		}
		entry = entry->next;
	}

	/* why? call replace before add? */
	if (!replaced)
		add_pin_to_irq_cpu(cfg, cpu, newapic, newpin);
}

static inline void io_apic_modify_irq(struct irq_cfg *cfg,
				int mask_and, int mask_or,
				void (*final)(struct irq_pin_list *entry))
{
	int pin;
	struct irq_pin_list *entry;

	for (entry = cfg->irq_2_pin; entry != NULL; entry = entry->next) {
		unsigned int reg;
		pin = entry->pin;
		reg = io_apic_read(entry->apic, 0x10 + pin * 2);
		reg &= mask_and;
		reg |= mask_or;
		io_apic_modify(entry->apic, 0x10 + pin * 2, reg);
		if (final)
			final(entry);
	}
}

static void __unmask_IO_APIC_irq(struct irq_cfg *cfg)
{
	io_apic_modify_irq(cfg, ~IO_APIC_REDIR_MASKED, 0, NULL);
}

#ifdef CONFIG_X86_64
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
#else /* CONFIG_X86_32 */
static void __mask_IO_APIC_irq(struct irq_cfg *cfg)
{
	io_apic_modify_irq(cfg, ~0, IO_APIC_REDIR_MASKED, NULL);
}

static void __mask_and_edge_IO_APIC_irq(struct irq_cfg *cfg)
{
	io_apic_modify_irq(cfg, ~IO_APIC_REDIR_LEVEL_TRIGGER,
			IO_APIC_REDIR_MASKED, NULL);
}

static void __unmask_and_level_IO_APIC_irq(struct irq_cfg *cfg)
{
	io_apic_modify_irq(cfg, ~IO_APIC_REDIR_MASKED,
			IO_APIC_REDIR_LEVEL_TRIGGER, NULL);
}
#endif /* CONFIG_X86_32 */

static void mask_IO_APIC_irq_desc(struct irq_desc *desc)
{
	struct irq_cfg *cfg = desc->chip_data;
	unsigned long flags;

	BUG_ON(!cfg);

	spin_lock_irqsave(&ioapic_lock, flags);
	__mask_IO_APIC_irq(cfg);
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void unmask_IO_APIC_irq_desc(struct irq_desc *desc)
{
	struct irq_cfg *cfg = desc->chip_data;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	__unmask_IO_APIC_irq(cfg);
	spin_unlock_irqrestore(&ioapic_lock, flags);
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

#if !defined(CONFIG_SMP) && defined(CONFIG_X86_32)
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
#endif /* !CONFIG_SMP && CONFIG_X86_32*/

#ifdef CONFIG_X86_32
/*
 * support for broken MP BIOSs, enables hand-redirection of PIRQ0-7 to
 * specific CPU-side IRQs.
 */

#define MAX_PIRQS 8
static int pirq_entries [MAX_PIRQS];
static int pirqs_enabled;

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
#endif /* CONFIG_X86_32 */

#ifdef CONFIG_INTR_REMAP
/* I/O APIC RTE contents at the OS boot up */
static struct IO_APIC_route_entry *early_ioapic_entries[MAX_IO_APICS];

/*
 * Saves and masks all the unmasked IO-APIC RTE's
 */
int save_mask_IO_APIC_setup(void)
{
	union IO_APIC_reg_01 reg_01;
	unsigned long flags;
	int apic, pin;

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
		early_ioapic_entries[apic] =
			kzalloc(sizeof(struct IO_APIC_route_entry) *
				nr_ioapic_registers[apic], GFP_KERNEL);
		if (!early_ioapic_entries[apic])
			goto nomem;
	}

	for (apic = 0; apic < nr_ioapics; apic++)
		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++) {
			struct IO_APIC_route_entry entry;

			entry = early_ioapic_entries[apic][pin] =
				ioapic_read_entry(apic, pin);
			if (!entry.mask) {
				entry.mask = 1;
				ioapic_write_entry(apic, pin, entry);
			}
		}

	return 0;

nomem:
	while (apic >= 0)
		kfree(early_ioapic_entries[apic--]);
	memset(early_ioapic_entries, 0,
		ARRAY_SIZE(early_ioapic_entries));

	return -ENOMEM;
}

void restore_IO_APIC_setup(void)
{
	int apic, pin;

	for (apic = 0; apic < nr_ioapics; apic++) {
		if (!early_ioapic_entries[apic])
			break;
		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++)
			ioapic_write_entry(apic, pin,
					   early_ioapic_entries[apic][pin]);
		kfree(early_ioapic_entries[apic]);
		early_ioapic_entries[apic] = NULL;
	}
}

void reinit_intr_remapped_IO_APIC(int intr_remapping)
{
	/*
	 * for now plain restore of previous settings.
	 * TBD: In the case of OS enabling interrupt-remapping,
	 * IO-APIC RTE's need to be setup to point to interrupt-remapping
	 * table entries. for now, do a plain restore, and wait for
	 * the setup_IO_APIC_irqs() to do proper initialization.
	 */
	restore_IO_APIC_setup();
}
#endif

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
		for(apic = 0; apic < nr_ioapics; apic++) {
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

	apic_printk(APIC_DEBUG, "querying PCI -> IRQ mapping bus:%d, slot:%d, pin:%d.\n",
		bus, slot, pin);
	if (test_bit(bus, mp_bus_not_pci)) {
		apic_printk(APIC_VERBOSE, "PCI BIOS passed nonexistent PCI bus %d!\n", bus);
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
			int irq = pin_2_irq(i,apic,mp_irqs[i].mp_dstirq);

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

#if defined(CONFIG_EISA) || defined(CONFIG_MCA)
/*
 * EISA Edge/Level control register, ELCR
 */
static int EISA_ELCR(unsigned int irq)
{
	if (irq < NR_IRQS_LEGACY) {
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
	switch (mp_irqs[idx].mp_irqflag & 3)
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
	int bus = mp_irqs[idx].mp_srcbus;
	int trigger;

	/*
	 * Determine IRQ trigger mode (edge or level sensitive):
	 */
	switch ((mp_irqs[idx].mp_irqflag>>2) & 3)
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
	int bus = mp_irqs[idx].mp_srcbus;

	/*
	 * Debugging check, we are in big trouble if this message pops up!
	 */
	if (mp_irqs[idx].mp_dstirq != pin)
		printk(KERN_ERR "broken BIOS or MPTABLE parser, ayiee!!\n");

	if (test_bit(bus, mp_bus_not_pci)) {
		irq = mp_irqs[idx].mp_srcbusirq;
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

void lock_vector_lock(void)
{
	/* Used to the online set of cpus does not change
	 * during assign_irq_vector.
	 */
	spin_lock(&vector_lock);
}

void unlock_vector_lock(void)
{
	spin_unlock(&vector_lock);
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
	static int current_vector = FIRST_DEVICE_VECTOR, current_offset = 0;
	unsigned int old_vector;
	int cpu, err;
	cpumask_var_t tmp_mask;

	if ((cfg->move_in_progress) || cfg->move_cleanup_count)
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

		vector_allocation_domain(cpu, tmp_mask);

		vector = current_vector;
		offset = current_offset;
next:
		vector += 8;
		if (vector >= first_system_vector) {
			/* If out of vectors on large boxen, must share them. */
			offset = (offset + 1) % 8;
			vector = FIRST_DEVICE_VECTOR + offset;
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

static int
assign_irq_vector(int irq, struct irq_cfg *cfg, const struct cpumask *mask)
{
	int err;
	unsigned long flags;

	spin_lock_irqsave(&vector_lock, flags);
	err = __assign_irq_vector(irq, cfg, mask);
	spin_unlock_irqrestore(&vector_lock, flags);
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
	/* This function must be called with vector_lock held */
	int irq, vector;
	struct irq_cfg *cfg;
	struct irq_desc *desc;

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
}

static struct irq_chip ioapic_chip;
#ifdef CONFIG_INTR_REMAP
static struct irq_chip ir_ioapic_chip;
#endif

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

#ifdef CONFIG_INTR_REMAP
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
#endif
	if ((trigger == IOAPIC_AUTO && IO_APIC_irq_trigger(irq)) ||
	    trigger == IOAPIC_LEVEL)
		set_irq_chip_and_handler_name(irq, &ioapic_chip,
					      handle_fasteoi_irq,
					      "fasteoi");
	else
		set_irq_chip_and_handler_name(irq, &ioapic_chip,
					      handle_edge_irq, "edge");
}

static int setup_ioapic_entry(int apic, int irq,
			      struct IO_APIC_route_entry *entry,
			      unsigned int destination, int trigger,
			      int polarity, int vector)
{
	/*
	 * add it to the IO-APIC irq-routing table:
	 */
	memset(entry,0,sizeof(*entry));

#ifdef CONFIG_INTR_REMAP
	if (intr_remapping_enabled) {
		struct intel_iommu *iommu = map_ioapic_to_ir(apic);
		struct irte irte;
		struct IR_IO_APIC_route_entry *ir_entry =
			(struct IR_IO_APIC_route_entry *) entry;
		int index;

		if (!iommu)
			panic("No mapping iommu for ioapic %d\n", apic);

		index = alloc_irte(iommu, irq, 1);
		if (index < 0)
			panic("Failed to allocate IRTE for ioapic %d\n", apic);

		memset(&irte, 0, sizeof(irte));

		irte.present = 1;
		irte.dst_mode = INT_DEST_MODE;
		irte.trigger_mode = trigger;
		irte.dlvry_mode = INT_DELIVERY_MODE;
		irte.vector = vector;
		irte.dest_id = IRTE_DEST(destination);

		modify_irte(irq, &irte);

		ir_entry->index2 = (index >> 15) & 0x1;
		ir_entry->zero = 0;
		ir_entry->format = 1;
		ir_entry->index = (index & 0x7fff);
	} else
#endif
	{
		entry->delivery_mode = INT_DELIVERY_MODE;
		entry->dest_mode = INT_DEST_MODE;
		entry->dest = destination;
	}

	entry->mask = 0;				/* enable IRQ */
	entry->trigger = trigger;
	entry->polarity = polarity;
	entry->vector = vector;

	/* Mask level triggered irqs.
	 * Use IRQ_DELAYED_DISABLE for edge triggered irqs.
	 */
	if (trigger)
		entry->mask = 1;
	return 0;
}

static void setup_IO_APIC_irq(int apic, int pin, unsigned int irq, struct irq_desc *desc,
			      int trigger, int polarity)
{
	struct irq_cfg *cfg;
	struct IO_APIC_route_entry entry;
	unsigned int dest;

	if (!IO_APIC_IRQ(irq))
		return;

	cfg = desc->chip_data;

	if (assign_irq_vector(irq, cfg, TARGET_CPUS))
		return;

	dest = cpu_mask_to_apicid_and(cfg->domain, TARGET_CPUS);

	apic_printk(APIC_VERBOSE,KERN_DEBUG
		    "IOAPIC[%d]: Set routing entry (%d-%d -> 0x%x -> "
		    "IRQ %d Mode:%i Active:%i)\n",
		    apic, mp_ioapics[apic].mp_apicid, pin, cfg->vector,
		    irq, trigger, polarity);


	if (setup_ioapic_entry(mp_ioapics[apic].mp_apicid, irq, &entry,
			       dest, trigger, polarity, cfg->vector)) {
		printk("Failed to setup ioapic entry for ioapic  %d, pin %d\n",
		       mp_ioapics[apic].mp_apicid, pin);
		__clear_irq_vector(irq, cfg);
		return;
	}

	ioapic_register_intr(irq, desc, trigger);
	if (irq < NR_IRQS_LEGACY)
		disable_8259A_irq(irq);

	ioapic_write_entry(apic, pin, entry);
}

static void __init setup_IO_APIC_irqs(void)
{
	int apic, pin, idx, irq;
	int notcon = 0;
	struct irq_desc *desc;
	struct irq_cfg *cfg;
	int cpu = boot_cpu_id;

	apic_printk(APIC_VERBOSE, KERN_DEBUG "init IO_APIC IRQs\n");

	for (apic = 0; apic < nr_ioapics; apic++) {
		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++) {

			idx = find_irq_entry(apic, pin, mp_INT);
			if (idx == -1) {
				if (!notcon) {
					notcon = 1;
					apic_printk(APIC_VERBOSE,
						KERN_DEBUG " %d-%d",
						mp_ioapics[apic].mp_apicid,
						pin);
				} else
					apic_printk(APIC_VERBOSE, " %d-%d",
						mp_ioapics[apic].mp_apicid,
						pin);
				continue;
			}
			if (notcon) {
				apic_printk(APIC_VERBOSE,
					" (apicid-pin) not connected\n");
				notcon = 0;
			}

			irq = pin_2_irq(idx, apic, pin);
#ifdef CONFIG_X86_32
			if (multi_timer_check(apic, irq))
				continue;
#endif
			desc = irq_to_desc_alloc_cpu(irq, cpu);
			if (!desc) {
				printk(KERN_INFO "can not get irq_desc for %d\n", irq);
				continue;
			}
			cfg = desc->chip_data;
			add_pin_to_irq_cpu(cfg, cpu, apic, pin);

			setup_IO_APIC_irq(apic, pin, irq, desc,
					irq_trigger(idx), irq_polarity(idx));
		}
	}

	if (notcon)
		apic_printk(APIC_VERBOSE,
			" (apicid-pin) not connected\n");
}

/*
 * Set up the timer pin, possibly with the 8259A-master behind.
 */
static void __init setup_timer_IRQ0_pin(unsigned int apic, unsigned int pin,
					int vector)
{
	struct IO_APIC_route_entry entry;

#ifdef CONFIG_INTR_REMAP
	if (intr_remapping_enabled)
		return;
#endif

	memset(&entry, 0, sizeof(entry));

	/*
	 * We use logical delivery to get the timer IRQ
	 * to the first CPU.
	 */
	entry.dest_mode = INT_DEST_MODE;
	entry.mask = 1;					/* mask IRQ now */
	entry.dest = cpu_mask_to_apicid(TARGET_CPUS);
	entry.delivery_mode = INT_DELIVERY_MODE;
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
	struct irq_cfg *cfg;
	struct irq_desc *desc;
	unsigned int irq;

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

	printk("\n");
	printk(KERN_DEBUG "IO APIC #%d......\n", mp_ioapics[apic].mp_apicid);
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
		for (;;) {
			printk("-> %d:%d", entry->apic, entry->pin);
			if (!entry->next)
				break;
			entry = entry->next;
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
	print_APIC_bitfield(APIC_ISR);
	printk(KERN_DEBUG "... APIC TMR field:\n");
	print_APIC_bitfield(APIC_TMR);
	printk(KERN_DEBUG "... APIC IRR field:\n");
	print_APIC_bitfield(APIC_IRR);

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
	printk("\n");
}

__apicdebuginit(void) print_all_local_APICs(void)
{
	int cpu;

	preempt_disable();
	for_each_online_cpu(cpu)
		smp_call_function_single(cpu, print_local_APIC, NULL, 1);
	preempt_enable();
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

__apicdebuginit(int) print_all_ICs(void)
{
	print_PIC();
	print_all_local_APICs();
	print_IO_APIC();

	return 0;
}

fs_initcall(print_all_ICs);


/* Where if anywhere is the i8259 connect in external int mode */
static struct { int pin, apic; } ioapic_i8259 = { -1, -1 };

void __init enable_IO_APIC(void)
{
	union IO_APIC_reg_01 reg_01;
	int i8259_apic, i8259_pin;
	int apic;
	unsigned long flags;

#ifdef CONFIG_X86_32
	int i;
	if (!pirqs_enabled)
		for (i = 0; i < MAX_PIRQS; i++)
			pirq_entries[i] = -1;
#endif

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
		entry.dest            = read_apic_id();

		/*
		 * Add it to the IO-APIC irq-routing table:
		 */
		ioapic_write_entry(ioapic_i8259.apic, ioapic_i8259.pin, entry);
	}

	disconnect_bsp_APIC(ioapic_i8259.pin != -1);
}

#ifdef CONFIG_X86_32
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

	spin_lock_irqsave(&ioapic_lock, flags);
	if (irq < NR_IRQS_LEGACY) {
		disable_8259A_irq(irq);
		if (i8259A_irq_pending(irq))
			was_pending = 1;
	}
	cfg = irq_cfg(irq);
	__unmask_IO_APIC_irq(cfg);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return was_pending;
}

#ifdef CONFIG_X86_64
static int ioapic_retrigger_irq(unsigned int irq)
{

	struct irq_cfg *cfg = irq_cfg(irq);
	unsigned long flags;

	spin_lock_irqsave(&vector_lock, flags);
	send_IPI_mask(cpumask_of(cpumask_first(cfg->domain)), cfg->vector);
	spin_unlock_irqrestore(&vector_lock, flags);

	return 1;
}
#else
static int ioapic_retrigger_irq(unsigned int irq)
{
	send_IPI_self(irq_cfg(irq)->vector);

	return 1;
}
#endif

/*
 * Level and edge triggered IO-APIC interrupts need different handling,
 * so we use two separate IRQ descriptors. Edge triggered IRQs can be
 * handled with the level-triggered descriptor, but that one has slightly
 * more overhead. Level-triggered interrupts cannot be handled with the
 * edge-triggered handler, without risking IRQ storms and other ugly
 * races.
 */

#ifdef CONFIG_SMP

#ifdef CONFIG_INTR_REMAP
static void ir_irq_migration(struct work_struct *work);

static DECLARE_DELAYED_WORK(ir_migration_work, ir_irq_migration);

/*
 * Migrate the IO-APIC irq in the presence of intr-remapping.
 *
 * For edge triggered, irq migration is a simple atomic update(of vector
 * and cpu destination) of IRTE and flush the hardware cache.
 *
 * For level triggered, we need to modify the io-apic RTE aswell with the update
 * vector information, along with modifying IRTE with vector and destination.
 * So irq migration for level triggered is little  bit more complex compared to
 * edge triggered migration. But the good news is, we use the same algorithm
 * for level triggered migration as we have today, only difference being,
 * we now initiate the irq migration from process context instead of the
 * interrupt context.
 *
 * In future, when we do a directed EOI (combined with cpu EOI broadcast
 * suppression) to the IO-APIC, level triggered irq migration will also be
 * as simple as edge triggered migration and we can do the irq migration
 * with a simple atomic update to IO-APIC RTE.
 */
static void
migrate_ioapic_irq_desc(struct irq_desc *desc, const struct cpumask *mask)
{
	struct irq_cfg *cfg;
	struct irte irte;
	int modify_ioapic_rte;
	unsigned int dest;
	unsigned long flags;
	unsigned int irq;

	if (!cpumask_intersects(mask, cpu_online_mask))
		return;

	irq = desc->irq;
	if (get_irte(irq, &irte))
		return;

	cfg = desc->chip_data;
	if (assign_irq_vector(irq, cfg, mask))
		return;

	set_extra_move_desc(desc, mask);

	dest = cpu_mask_to_apicid_and(cfg->domain, mask);

	modify_ioapic_rte = desc->status & IRQ_LEVEL;
	if (modify_ioapic_rte) {
		spin_lock_irqsave(&ioapic_lock, flags);
		__target_IO_APIC_irq(irq, dest, cfg);
		spin_unlock_irqrestore(&ioapic_lock, flags);
	}

	irte.vector = cfg->vector;
	irte.dest_id = IRTE_DEST(dest);

	/*
	 * Modified the IRTE and flushes the Interrupt entry cache.
	 */
	modify_irte(irq, &irte);

	if (cfg->move_in_progress)
		send_cleanup_vector(cfg);

	cpumask_copy(&desc->affinity, mask);
}

static int migrate_irq_remapped_level_desc(struct irq_desc *desc)
{
	int ret = -1;
	struct irq_cfg *cfg = desc->chip_data;

	mask_IO_APIC_irq_desc(desc);

	if (io_apic_level_ack_pending(cfg)) {
		/*
		 * Interrupt in progress. Migrating irq now will change the
		 * vector information in the IO-APIC RTE and that will confuse
		 * the EOI broadcast performed by cpu.
		 * So, delay the irq migration to the next instance.
		 */
		schedule_delayed_work(&ir_migration_work, 1);
		goto unmask;
	}

	/* everthing is clear. we have right of way */
	migrate_ioapic_irq_desc(desc, &desc->pending_mask);

	ret = 0;
	desc->status &= ~IRQ_MOVE_PENDING;
	cpumask_clear(&desc->pending_mask);

unmask:
	unmask_IO_APIC_irq_desc(desc);

	return ret;
}

static void ir_irq_migration(struct work_struct *work)
{
	unsigned int irq;
	struct irq_desc *desc;

	for_each_irq_desc(irq, desc) {
		if (desc->status & IRQ_MOVE_PENDING) {
			unsigned long flags;

			spin_lock_irqsave(&desc->lock, flags);
			if (!desc->chip->set_affinity ||
			    !(desc->status & IRQ_MOVE_PENDING)) {
				desc->status &= ~IRQ_MOVE_PENDING;
				spin_unlock_irqrestore(&desc->lock, flags);
				continue;
			}

			desc->chip->set_affinity(irq, &desc->pending_mask);
			spin_unlock_irqrestore(&desc->lock, flags);
		}
	}
}

/*
 * Migrates the IRQ destination in the process context.
 */
static void set_ir_ioapic_affinity_irq_desc(struct irq_desc *desc,
					    const struct cpumask *mask)
{
	if (desc->status & IRQ_LEVEL) {
		desc->status |= IRQ_MOVE_PENDING;
		cpumask_copy(&desc->pending_mask, mask);
		migrate_irq_remapped_level_desc(desc);
		return;
	}

	migrate_ioapic_irq_desc(desc, mask);
}
static void set_ir_ioapic_affinity_irq(unsigned int irq,
				       const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);

	set_ir_ioapic_affinity_irq_desc(desc, mask);
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
		struct irq_desc *desc;
		struct irq_cfg *cfg;
		irq = __get_cpu_var(vector_irq)[vector];

		if (irq == -1)
			continue;

		desc = irq_to_desc(irq);
		if (!desc)
			continue;

		cfg = irq_cfg(irq);
		spin_lock(&desc->lock);
		if (!cfg->move_cleanup_count)
			goto unlock;

		if (vector == cfg->vector && cpumask_test_cpu(me, cfg->domain))
			goto unlock;

		__get_cpu_var(vector_irq)[vector] = -1;
		cfg->move_cleanup_count--;
unlock:
		spin_unlock(&desc->lock);
	}

	irq_exit();
}

static void irq_complete_move(struct irq_desc **descp)
{
	struct irq_desc *desc = *descp;
	struct irq_cfg *cfg = desc->chip_data;
	unsigned vector, me;

	if (likely(!cfg->move_in_progress)) {
#ifdef CONFIG_NUMA_MIGRATE_IRQ_DESC
		if (likely(!cfg->move_desc_pending))
			return;

		/* domain has not changed, but affinity did */
		me = smp_processor_id();
		if (cpu_isset(me, desc->affinity)) {
			*descp = desc = move_irq_desc(desc, me);
			/* get the new one */
			cfg = desc->chip_data;
			cfg->move_desc_pending = 0;
		}
#endif
		return;
	}

	vector = ~get_irq_regs()->orig_ax;
	me = smp_processor_id();
#ifdef CONFIG_NUMA_MIGRATE_IRQ_DESC
		*descp = desc = move_irq_desc(desc, me);
		/* get the new one */
		cfg = desc->chip_data;
#endif

	if (vector == cfg->vector && cpumask_test_cpu(me, cfg->domain))
		send_cleanup_vector(cfg);
}
#else
static inline void irq_complete_move(struct irq_desc **descp) {}
#endif

#ifdef CONFIG_INTR_REMAP
static void ack_x2apic_level(unsigned int irq)
{
	ack_x2APIC_irq();
}

static void ack_x2apic_edge(unsigned int irq)
{
	ack_x2APIC_irq();
}

#endif

static void ack_apic_edge(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	irq_complete_move(&desc);
	move_native_irq(irq);
	ack_APIC_irq();
}

atomic_t irq_mis_count;

static void ack_apic_level(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

#ifdef CONFIG_X86_32
	unsigned long v;
	int i;
#endif
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

#ifdef CONFIG_X86_32
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
	cfg = desc->chip_data;
	i = cfg->vector;

	v = apic_read(APIC_TMR + ((i & ~0x1f) >> 1));
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
		cfg = desc->chip_data;
		if (!io_apic_level_ack_pending(cfg))
			move_masked_irq(irq);
		unmask_IO_APIC_irq_desc(desc);
	}

#ifdef CONFIG_X86_32
	if (!(v & (1 << (i & 0x1f)))) {
		atomic_inc(&irq_mis_count);
		spin_lock(&ioapic_lock);
		__mask_and_edge_IO_APIC_irq(cfg);
		__unmask_and_level_IO_APIC_irq(cfg);
		spin_unlock(&ioapic_lock);
	}
#endif
}

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

#ifdef CONFIG_INTR_REMAP
static struct irq_chip ir_ioapic_chip __read_mostly = {
	.name		= "IR-IO-APIC",
	.startup	= startup_ioapic_irq,
	.mask		= mask_IO_APIC_irq,
	.unmask		= unmask_IO_APIC_irq,
	.ack		= ack_x2apic_edge,
	.eoi		= ack_x2apic_level,
#ifdef CONFIG_SMP
	.set_affinity	= set_ir_ioapic_affinity_irq,
#endif
	.retrigger	= ioapic_retrigger_irq,
};
#endif

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
			if (irq < NR_IRQS_LEGACY)
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
	int cpu = boot_cpu_id;
	int apic1, pin1, apic2, pin2;
	unsigned long flags;
	unsigned int ver;
	int no_pin1 = 0;

	local_irq_save(flags);

	ver = apic_read(APIC_LVR);
	ver = GET_APIC_VERSION(ver);

	/*
	 * get/set the timer IRQ vector:
	 */
	disable_8259A_irq(0);
	assign_irq_vector(0, cfg, TARGET_CPUS);

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
	timer_ack = (nmi_watchdog == NMI_IO_APIC && !APIC_INTEGRATED(ver));
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
#ifdef CONFIG_INTR_REMAP
		if (intr_remapping_enabled)
			panic("BIOS bug: timer not connected to IO-APIC");
#endif
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
			add_pin_to_irq_cpu(cfg, cpu, apic1, pin1);
			setup_timer_IRQ0_pin(apic1, pin1, cfg->vector);
		}
		unmask_IO_APIC_irq_desc(desc);
		if (timer_irq_works()) {
			if (nmi_watchdog == NMI_IO_APIC) {
				setup_nmi();
				enable_8259A_irq(0);
			}
			if (disable_timer_pin_1 > 0)
				clear_IO_APIC_pin(0, pin1);
			goto out;
		}
#ifdef CONFIG_INTR_REMAP
		if (intr_remapping_enabled)
			panic("timer doesn't work through Interrupt-remapped IO-APIC");
#endif
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
		replace_pin_at_irq_cpu(cfg, cpu, apic1, pin1, apic2, pin2);
		setup_timer_IRQ0_pin(apic2, pin2, cfg->vector);
		unmask_IO_APIC_irq_desc(desc);
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

#ifdef CONFIG_X86_32
	enable_IO_APIC();
#else
	/*
	 * calling enable_IO_APIC() is moved to setup_local_APIC for BP
	 */
#endif

	io_apic_irqs = ~PIC_IRQS;

	apic_printk(APIC_VERBOSE, "ENABLING IO-APIC IRQs\n");
	/*
         * Set up IO-APIC IRQ routing.
         */
#ifdef CONFIG_X86_32
	if (!acpi_ioapic)
		setup_ioapic_ids_from_mpc();
#endif
	sync_Arb_IDs();
	setup_IO_APIC_irqs();
	init_IO_APIC_traps();
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
unsigned int create_irq_nr(unsigned int irq_want)
{
	/* Allocate an unused irq */
	unsigned int irq;
	unsigned int new;
	unsigned long flags;
	struct irq_cfg *cfg_new = NULL;
	int cpu = boot_cpu_id;
	struct irq_desc *desc_new = NULL;

	irq = 0;
	spin_lock_irqsave(&vector_lock, flags);
	for (new = irq_want; new < NR_IRQS; new++) {
		if (platform_legacy_irq(new))
			continue;

		desc_new = irq_to_desc_alloc_cpu(new, cpu);
		if (!desc_new) {
			printk(KERN_INFO "can not get irq_desc for %d\n", new);
			continue;
		}
		cfg_new = desc_new->chip_data;

		if (cfg_new->vector != 0)
			continue;
		if (__assign_irq_vector(new, cfg_new, TARGET_CPUS) == 0)
			irq = new;
		break;
	}
	spin_unlock_irqrestore(&vector_lock, flags);

	if (irq > 0) {
		dynamic_irq_init(irq);
		/* restore it, in case dynamic_irq_init clear it */
		if (desc_new)
			desc_new->chip_data = cfg_new;
	}
	return irq;
}

static int nr_irqs_gsi = NR_IRQS_LEGACY;
int create_irq(void)
{
	unsigned int irq_want;
	int irq;

	irq_want = nr_irqs_gsi;
	irq = create_irq_nr(irq_want);

	if (irq == 0)
		irq = -1;

	return irq;
}

void destroy_irq(unsigned int irq)
{
	unsigned long flags;
	struct irq_cfg *cfg;
	struct irq_desc *desc;

	/* store it, in case dynamic_irq_cleanup clear it */
	desc = irq_to_desc(irq);
	cfg = desc->chip_data;
	dynamic_irq_cleanup(irq);
	/* connect back irq_cfg */
	if (desc)
		desc->chip_data = cfg;

#ifdef CONFIG_INTR_REMAP
	free_irte(irq);
#endif
	spin_lock_irqsave(&vector_lock, flags);
	__clear_irq_vector(irq, cfg);
	spin_unlock_irqrestore(&vector_lock, flags);
}

/*
 * MSI message composition
 */
#ifdef CONFIG_PCI_MSI
static int msi_compose_msg(struct pci_dev *pdev, unsigned int irq, struct msi_msg *msg)
{
	struct irq_cfg *cfg;
	int err;
	unsigned dest;

	cfg = irq_cfg(irq);
	err = assign_irq_vector(irq, cfg, TARGET_CPUS);
	if (err)
		return err;

	dest = cpu_mask_to_apicid_and(cfg->domain, TARGET_CPUS);

#ifdef CONFIG_INTR_REMAP
	if (irq_remapped(irq)) {
		struct irte irte;
		int ir_index;
		u16 sub_handle;

		ir_index = map_irq_to_irte_handle(irq, &sub_handle);
		BUG_ON(ir_index == -1);

		memset (&irte, 0, sizeof(irte));

		irte.present = 1;
		irte.dst_mode = INT_DEST_MODE;
		irte.trigger_mode = 0; /* edge */
		irte.dlvry_mode = INT_DELIVERY_MODE;
		irte.vector = cfg->vector;
		irte.dest_id = IRTE_DEST(dest);

		modify_irte(irq, &irte);

		msg->address_hi = MSI_ADDR_BASE_HI;
		msg->data = sub_handle;
		msg->address_lo = MSI_ADDR_BASE_LO | MSI_ADDR_IR_EXT_INT |
				  MSI_ADDR_IR_SHV |
				  MSI_ADDR_IR_INDEX1(ir_index) |
				  MSI_ADDR_IR_INDEX2(ir_index);
	} else
#endif
	{
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
static void set_msi_irq_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg;
	struct msi_msg msg;
	unsigned int dest;

	dest = set_desc_affinity(desc, mask);
	if (dest == BAD_APICID)
		return;

	cfg = desc->chip_data;

	read_msi_msg_desc(desc, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	write_msi_msg_desc(desc, &msg);
}
#ifdef CONFIG_INTR_REMAP
/*
 * Migrate the MSI irq to another cpumask. This migration is
 * done in the process context using interrupt-remapping hardware.
 */
static void
ir_set_msi_irq_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg = desc->chip_data;
	unsigned int dest;
	struct irte irte;

	if (get_irte(irq, &irte))
		return;

	dest = set_desc_affinity(desc, mask);
	if (dest == BAD_APICID)
		return;

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

#ifdef CONFIG_INTR_REMAP
static struct irq_chip msi_ir_chip = {
	.name		= "IR-PCI-MSI",
	.unmask		= unmask_msi_irq,
	.mask		= mask_msi_irq,
	.ack		= ack_x2apic_edge,
#ifdef CONFIG_SMP
	.set_affinity	= ir_set_msi_irq_affinity,
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
#endif

static int setup_msi_irq(struct pci_dev *dev, struct msi_desc *msidesc, int irq)
{
	int ret;
	struct msi_msg msg;

	ret = msi_compose_msg(dev, irq, &msg);
	if (ret < 0)
		return ret;

	set_irq_msi(irq, msidesc);
	write_msi_msg(irq, &msg);

#ifdef CONFIG_INTR_REMAP
	if (irq_remapped(irq)) {
		struct irq_desc *desc = irq_to_desc(irq);
		/*
		 * irq migration in process context
		 */
		desc->status |= IRQ_MOVE_PCNTXT;
		set_irq_chip_and_handler_name(irq, &msi_ir_chip, handle_edge_irq, "edge");
	} else
#endif
		set_irq_chip_and_handler_name(irq, &msi_chip, handle_edge_irq, "edge");

	dev_printk(KERN_DEBUG, &dev->dev, "irq %d for MSI/MSI-X\n", irq);

	return 0;
}

int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *msidesc)
{
	unsigned int irq;
	int ret;
	unsigned int irq_want;

	irq_want = nr_irqs_gsi;
	irq = create_irq_nr(irq_want);
	if (irq == 0)
		return -1;

#ifdef CONFIG_INTR_REMAP
	if (!intr_remapping_enabled)
		goto no_ir;

	ret = msi_alloc_irte(dev, irq, 1);
	if (ret < 0)
		goto error;
no_ir:
#endif
	ret = setup_msi_irq(dev, msidesc, irq);
	if (ret < 0) {
		destroy_irq(irq);
		return ret;
	}
	return 0;

#ifdef CONFIG_INTR_REMAP
error:
	destroy_irq(irq);
	return ret;
#endif
}

int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	unsigned int irq;
	int ret, sub_handle;
	struct msi_desc *msidesc;
	unsigned int irq_want;

#ifdef CONFIG_INTR_REMAP
	struct intel_iommu *iommu = 0;
	int index = 0;
#endif

	irq_want = nr_irqs_gsi;
	sub_handle = 0;
	list_for_each_entry(msidesc, &dev->msi_list, list) {
		irq = create_irq_nr(irq_want);
		irq_want++;
		if (irq == 0)
			return -1;
#ifdef CONFIG_INTR_REMAP
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
#endif
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

#ifdef CONFIG_DMAR
#ifdef CONFIG_SMP
static void dmar_msi_set_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg;
	struct msi_msg msg;
	unsigned int dest;

	dest = set_desc_affinity(desc, mask);
	if (dest == BAD_APICID)
		return;

	cfg = desc->chip_data;

	dmar_msi_read(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	dmar_msi_write(irq, &msg);
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

#ifdef CONFIG_HPET_TIMER

#ifdef CONFIG_SMP
static void hpet_msi_set_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg;
	struct msi_msg msg;
	unsigned int dest;

	dest = set_desc_affinity(desc, mask);
	if (dest == BAD_APICID)
		return;

	cfg = desc->chip_data;

	hpet_msi_read(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	hpet_msi_write(irq, &msg);
}

#endif /* CONFIG_SMP */

struct irq_chip hpet_msi_type = {
	.name = "HPET_MSI",
	.unmask = hpet_msi_unmask,
	.mask = hpet_msi_mask,
	.ack = ack_apic_edge,
#ifdef CONFIG_SMP
	.set_affinity = hpet_msi_set_affinity,
#endif
	.retrigger = ioapic_retrigger_irq,
};

int arch_setup_hpet_msi(unsigned int irq)
{
	int ret;
	struct msi_msg msg;

	ret = msi_compose_msg(NULL, irq, &msg);
	if (ret < 0)
		return ret;

	hpet_msi_write(irq, &msg);
	set_irq_chip_and_handler_name(irq, &hpet_msi_type, handle_edge_irq,
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

static void set_ht_irq_affinity(unsigned int irq, const struct cpumask *mask)
{
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_cfg *cfg;
	unsigned int dest;

	dest = set_desc_affinity(desc, mask);
	if (dest == BAD_APICID)
		return;

	cfg = desc->chip_data;

	target_ht_irq(irq, dest, cfg->vector);
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

	cfg = irq_cfg(irq);
	err = assign_irq_vector(irq, cfg, TARGET_CPUS);
	if (!err) {
		struct ht_irq_msg msg;
		unsigned dest;

		dest = cpu_mask_to_apicid_and(cfg->domain, TARGET_CPUS);

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

		dev_printk(KERN_DEBUG, &dev->dev, "irq %d for HT\n", irq);
	}
	return err;
}
#endif /* CONFIG_HT_IRQ */

#ifdef CONFIG_X86_64
/*
 * Re-target the irq to the specified CPU and enable the specified MMR located
 * on the specified blade to allow the sending of MSIs to the specified CPU.
 */
int arch_enable_uv_irq(char *irq_name, unsigned int irq, int cpu, int mmr_blade,
		       unsigned long mmr_offset)
{
	const struct cpumask *eligible_cpu = cpumask_of(cpu);
	struct irq_cfg *cfg;
	int mmr_pnode;
	unsigned long mmr_value;
	struct uv_IO_APIC_route_entry *entry;
	unsigned long flags;
	int err;

	cfg = irq_cfg(irq);

	err = assign_irq_vector(irq, cfg, eligible_cpu);
	if (err != 0)
		return err;

	spin_lock_irqsave(&vector_lock, flags);
	set_irq_chip_and_handler_name(irq, &uv_irq_chip, handle_percpu_irq,
				      irq_name);
	spin_unlock_irqrestore(&vector_lock, flags);

	mmr_value = 0;
	entry = (struct uv_IO_APIC_route_entry *)&mmr_value;
	BUG_ON(sizeof(struct uv_IO_APIC_route_entry) != sizeof(unsigned long));

	entry->vector = cfg->vector;
	entry->delivery_mode = INT_DELIVERY_MODE;
	entry->dest_mode = INT_DEST_MODE;
	entry->polarity = 0;
	entry->trigger = 0;
	entry->mask = 0;
	entry->dest = cpu_mask_to_apicid(eligible_cpu);

	mmr_pnode = uv_blade_to_pnode(mmr_blade);
	uv_write_global_mmr64(mmr_pnode, mmr_offset, mmr_value);

	return irq;
}

/*
 * Disable the specified MMR located on the specified blade so that MSIs are
 * longer allowed to be sent.
 */
void arch_disable_uv_irq(int mmr_blade, unsigned long mmr_offset)
{
	unsigned long mmr_value;
	struct uv_IO_APIC_route_entry *entry;
	int mmr_pnode;

	mmr_value = 0;
	entry = (struct uv_IO_APIC_route_entry *)&mmr_value;
	BUG_ON(sizeof(struct uv_IO_APIC_route_entry) != sizeof(unsigned long));

	entry->mask = 1;

	mmr_pnode = uv_blade_to_pnode(mmr_blade);
	uv_write_global_mmr64(mmr_pnode, mmr_offset, mmr_value);
}
#endif /* CONFIG_X86_64 */

int __init io_apic_get_redir_entries (int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return reg_01.bits.entries;
}

void __init probe_nr_irqs_gsi(void)
{
	int idx;
	int nr = 0;

	for (idx = 0; idx < nr_ioapics; idx++)
		nr += io_apic_get_redir_entries(idx) + 1;

	if (nr > nr_irqs_gsi)
		nr_irqs_gsi = nr;
}

/* --------------------------------------------------------------------------
                          ACPI-based IOAPIC Configuration
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI

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
#endif

int io_apic_set_pci_routing (int ioapic, int pin, int irq, int triggering, int polarity)
{
	struct irq_desc *desc;
	struct irq_cfg *cfg;
	int cpu = boot_cpu_id;

	if (!IO_APIC_IRQ(irq)) {
		apic_printk(APIC_QUIET,KERN_ERR "IOAPIC[%d]: Invalid reference to IRQ 0\n",
			ioapic);
		return -EINVAL;
	}

	desc = irq_to_desc_alloc_cpu(irq, cpu);
	if (!desc) {
		printk(KERN_INFO "can not get irq_desc %d\n", irq);
		return 0;
	}

	/*
	 * IRQs < 16 are already in the irq_2_pin[] map
	 */
	if (irq >= NR_IRQS_LEGACY) {
		cfg = desc->chip_data;
		add_pin_to_irq_cpu(cfg, cpu, ioapic, pin);
	}

	setup_IO_APIC_irq(ioapic, pin, irq, desc, triggering, polarity);

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

/*
 * This function currently is only a helper for the i386 smp boot process where
 * we need to reprogram the ioredtbls to cater for the cpus which have come online
 * so mask in all cases should simply be TARGET_CPUS
 */
#ifdef CONFIG_SMP
void __init setup_ioapic_dest(void)
{
	int pin, ioapic, irq, irq_entry;
	struct irq_desc *desc;
	struct irq_cfg *cfg;
	const struct cpumask *mask;

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
			desc = irq_to_desc(irq);
			cfg = desc->chip_data;
			if (!cfg->vector) {
				setup_IO_APIC_irq(ioapic, pin, irq, desc,
						  irq_trigger(irq_entry),
						  irq_polarity(irq_entry));
				continue;

			}

			/*
			 * Honour affinities which have been set in early boot
			 */
			if (desc->status &
			    (IRQ_NO_BALANCING | IRQ_AFFINITY_SET))
				mask = &desc->affinity;
			else
				mask = TARGET_CPUS;

#ifdef CONFIG_INTR_REMAP
			if (intr_remapping_enabled)
				set_ir_ioapic_affinity_irq_desc(desc, mask);
			else
#endif
				set_ioapic_affinity_irq_desc(desc, mask);
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
			ioapic_phys = mp_ioapics[i].mp_apicaddr;
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
			ioapic_phys = (unsigned long)
				alloc_bootmem_pages(PAGE_SIZE);
			ioapic_phys = __pa(ioapic_phys);
		}
		set_fixmap_nocache(idx, ioapic_phys);
		apic_printk(APIC_VERBOSE,
			    "mapped IOAPIC to %08lx (%08lx)\n",
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
