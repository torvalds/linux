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
#include <linux/config.h>
#include <linux/smp_lock.h>
#include <linux/mc146818rtc.h>
#include <linux/compiler.h>
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/sysdev.h>

#include <asm/io.h>
#include <asm/smp.h>
#include <asm/desc.h>
#include <asm/timer.h>
#include <asm/i8259.h>

#include <mach_apic.h>

#include "io_ports.h"

int (*ioapic_renumber_irq)(int ioapic, int irq);
atomic_t irq_mis_count;

/* Where if anywhere is the i8259 connect in external int mode */
static struct { int pin, apic; } ioapic_i8259 = { -1, -1 };

static DEFINE_SPINLOCK(ioapic_lock);

/*
 *	Is the SiS APIC rmw bug present ?
 *	-1 = don't know, 0 = no, 1 = yes
 */
int sis_apic_bug = -1;

/*
 * # of IRQ routing registers
 */
int nr_ioapic_registers[MAX_IO_APICS];

int disable_timer_pin_1 __initdata;

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
	int apic, pin, next;
} irq_2_pin[PIN_MAP_SIZE];

int vector_irq[NR_VECTORS] __read_mostly = { [0 ... NR_VECTORS - 1] = -1};
#ifdef CONFIG_PCI_MSI
#define vector_to_irq(vector) 	\
	(platform_legacy_irq(vector) ? vector : vector_irq[vector])
#else
#define vector_to_irq(vector)	(vector)
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

	while (entry->next)
		entry = irq_2_pin + entry->next;

	if (entry->pin != -1) {
		entry->next = first_free_entry;
		entry = irq_2_pin + entry->next;
		if (++first_free_entry >= PIN_MAP_SIZE)
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

static void __modify_IO_APIC_irq (unsigned int irq, unsigned long enable, unsigned long disable)
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
static void __mask_IO_APIC_irq (unsigned int irq)
{
	__modify_IO_APIC_irq(irq, 0x00010000, 0);
}

/* mask = 0 */
static void __unmask_IO_APIC_irq (unsigned int irq)
{
	__modify_IO_APIC_irq(irq, 0, 0x00010000);
}

/* mask = 1, trigger = 0 */
static void __mask_and_edge_IO_APIC_irq (unsigned int irq)
{
	__modify_IO_APIC_irq(irq, 0x00010000, 0x00008000);
}

/* mask = 0, trigger = 1 */
static void __unmask_and_level_IO_APIC_irq (unsigned int irq)
{
	__modify_IO_APIC_irq(irq, 0x00008000, 0x00010000);
}

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
	unsigned long flags;
	
	/* Check delivery_mode to be sure we're not clearing an SMI pin */
	spin_lock_irqsave(&ioapic_lock, flags);
	*(((int*)&entry) + 0) = io_apic_read(apic, 0x10 + 2 * pin);
	*(((int*)&entry) + 1) = io_apic_read(apic, 0x11 + 2 * pin);
	spin_unlock_irqrestore(&ioapic_lock, flags);
	if (entry.delivery_mode == dest_SMI)
		return;

	/*
	 * Disable it in the IO-APIC irq-routing table:
	 */
	memset(&entry, 0, sizeof(entry));
	entry.mask = 1;
	spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_write(apic, 0x10 + 2 * pin, *(((int *)&entry) + 0));
	io_apic_write(apic, 0x11 + 2 * pin, *(((int *)&entry) + 1));
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

static void clear_IO_APIC (void)
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
	set_irq_info(irq, cpumask);
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

#if defined(CONFIG_IRQBALANCE)
# include <asm/processor.h>	/* kernel_thread() */
# include <linux/kernel_stat.h>	/* kstat */
# include <linux/slab.h>		/* kmalloc() */
# include <linux/timer.h>	/* time_after() */
 
# ifdef CONFIG_BALANCED_IRQ_DEBUG
#  define TDprintk(x...) do { printk("<%ld:%s:%d>: ", jiffies, __FILE__, __LINE__); printk(x); } while (0)
#  define Dprintk(x...) do { TDprintk(x); } while (0)
# else
#  define TDprintk(x...) 
#  define Dprintk(x...) 
# endif


#define IRQBALANCE_CHECK_ARCH -999
static int irqbalance_disabled = IRQBALANCE_CHECK_ARCH;
static int physical_balance = 0;

static struct irq_cpu_info {
	unsigned long * last_irq;
	unsigned long * irq_delta;
	unsigned long irq;
} irq_cpu_data[NR_CPUS];

#define CPU_IRQ(cpu)		(irq_cpu_data[cpu].irq)
#define LAST_CPU_IRQ(cpu,irq)   (irq_cpu_data[cpu].last_irq[irq])
#define IRQ_DELTA(cpu,irq) 	(irq_cpu_data[cpu].irq_delta[irq])

#define IDLE_ENOUGH(cpu,now) \
	(idle_cpu(cpu) && ((now) - per_cpu(irq_stat, (cpu)).idle_timestamp > 1))

#define IRQ_ALLOWED(cpu, allowed_mask)	cpu_isset(cpu, allowed_mask)

#define CPU_TO_PACKAGEINDEX(i) (first_cpu(cpu_sibling_map[i]))

#define MAX_BALANCED_IRQ_INTERVAL	(5*HZ)
#define MIN_BALANCED_IRQ_INTERVAL	(HZ/2)
#define BALANCED_IRQ_MORE_DELTA		(HZ/10)
#define BALANCED_IRQ_LESS_DELTA		(HZ)

static long balanced_irq_interval = MAX_BALANCED_IRQ_INTERVAL;

static unsigned long move(int curr_cpu, cpumask_t allowed_mask,
			unsigned long now, int direction)
{
	int search_idle = 1;
	int cpu = curr_cpu;

	goto inside;

	do {
		if (unlikely(cpu == curr_cpu))
			search_idle = 0;
inside:
		if (direction == 1) {
			cpu++;
			if (cpu >= NR_CPUS)
				cpu = 0;
		} else {
			cpu--;
			if (cpu == -1)
				cpu = NR_CPUS-1;
		}
	} while (!cpu_online(cpu) || !IRQ_ALLOWED(cpu,allowed_mask) ||
			(search_idle && !IDLE_ENOUGH(cpu,now)));

	return cpu;
}

static inline void balance_irq(int cpu, int irq)
{
	unsigned long now = jiffies;
	cpumask_t allowed_mask;
	unsigned int new_cpu;
		
	if (irqbalance_disabled)
		return; 

	cpus_and(allowed_mask, cpu_online_map, irq_affinity[irq]);
	new_cpu = move(cpu, allowed_mask, now, 1);
	if (cpu != new_cpu) {
		set_pending_irq(irq, cpumask_of_cpu(new_cpu));
	}
}

static inline void rotate_irqs_among_cpus(unsigned long useful_load_threshold)
{
	int i, j;
	Dprintk("Rotating IRQs among CPUs.\n");
	for (i = 0; i < NR_CPUS; i++) {
		for (j = 0; cpu_online(i) && (j < NR_IRQS); j++) {
			if (!irq_desc[j].action)
				continue;
			/* Is it a significant load ?  */
			if (IRQ_DELTA(CPU_TO_PACKAGEINDEX(i),j) <
						useful_load_threshold)
				continue;
			balance_irq(i, j);
		}
	}
	balanced_irq_interval = max((long)MIN_BALANCED_IRQ_INTERVAL,
		balanced_irq_interval - BALANCED_IRQ_LESS_DELTA);	
	return;
}

static void do_irq_balance(void)
{
	int i, j;
	unsigned long max_cpu_irq = 0, min_cpu_irq = (~0);
	unsigned long move_this_load = 0;
	int max_loaded = 0, min_loaded = 0;
	int load;
	unsigned long useful_load_threshold = balanced_irq_interval + 10;
	int selected_irq;
	int tmp_loaded, first_attempt = 1;
	unsigned long tmp_cpu_irq;
	unsigned long imbalance = 0;
	cpumask_t allowed_mask, target_cpu_mask, tmp;

	for (i = 0; i < NR_CPUS; i++) {
		int package_index;
		CPU_IRQ(i) = 0;
		if (!cpu_online(i))
			continue;
		package_index = CPU_TO_PACKAGEINDEX(i);
		for (j = 0; j < NR_IRQS; j++) {
			unsigned long value_now, delta;
			/* Is this an active IRQ? */
			if (!irq_desc[j].action)
				continue;
			if ( package_index == i )
				IRQ_DELTA(package_index,j) = 0;
			/* Determine the total count per processor per IRQ */
			value_now = (unsigned long) kstat_cpu(i).irqs[j];

			/* Determine the activity per processor per IRQ */
			delta = value_now - LAST_CPU_IRQ(i,j);

			/* Update last_cpu_irq[][] for the next time */
			LAST_CPU_IRQ(i,j) = value_now;

			/* Ignore IRQs whose rate is less than the clock */
			if (delta < useful_load_threshold)
				continue;
			/* update the load for the processor or package total */
			IRQ_DELTA(package_index,j) += delta;

			/* Keep track of the higher numbered sibling as well */
			if (i != package_index)
				CPU_IRQ(i) += delta;
			/*
			 * We have sibling A and sibling B in the package
			 *
			 * cpu_irq[A] = load for cpu A + load for cpu B
			 * cpu_irq[B] = load for cpu B
			 */
			CPU_IRQ(package_index) += delta;
		}
	}
	/* Find the least loaded processor package */
	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_online(i))
			continue;
		if (i != CPU_TO_PACKAGEINDEX(i))
			continue;
		if (min_cpu_irq > CPU_IRQ(i)) {
			min_cpu_irq = CPU_IRQ(i);
			min_loaded = i;
		}
	}
	max_cpu_irq = ULONG_MAX;

tryanothercpu:
	/* Look for heaviest loaded processor.
	 * We may come back to get the next heaviest loaded processor.
	 * Skip processors with trivial loads.
	 */
	tmp_cpu_irq = 0;
	tmp_loaded = -1;
	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_online(i))
			continue;
		if (i != CPU_TO_PACKAGEINDEX(i))
			continue;
		if (max_cpu_irq <= CPU_IRQ(i)) 
			continue;
		if (tmp_cpu_irq < CPU_IRQ(i)) {
			tmp_cpu_irq = CPU_IRQ(i);
			tmp_loaded = i;
		}
	}

	if (tmp_loaded == -1) {
 	 /* In the case of small number of heavy interrupt sources, 
	  * loading some of the cpus too much. We use Ingo's original 
	  * approach to rotate them around.
	  */
		if (!first_attempt && imbalance >= useful_load_threshold) {
			rotate_irqs_among_cpus(useful_load_threshold);
			return;
		}
		goto not_worth_the_effort;
	}
	
	first_attempt = 0;		/* heaviest search */
	max_cpu_irq = tmp_cpu_irq;	/* load */
	max_loaded = tmp_loaded;	/* processor */
	imbalance = (max_cpu_irq - min_cpu_irq) / 2;
	
	Dprintk("max_loaded cpu = %d\n", max_loaded);
	Dprintk("min_loaded cpu = %d\n", min_loaded);
	Dprintk("max_cpu_irq load = %ld\n", max_cpu_irq);
	Dprintk("min_cpu_irq load = %ld\n", min_cpu_irq);
	Dprintk("load imbalance = %lu\n", imbalance);

	/* if imbalance is less than approx 10% of max load, then
	 * observe diminishing returns action. - quit
	 */
	if (imbalance < (max_cpu_irq >> 3)) {
		Dprintk("Imbalance too trivial\n");
		goto not_worth_the_effort;
	}

tryanotherirq:
	/* if we select an IRQ to move that can't go where we want, then
	 * see if there is another one to try.
	 */
	move_this_load = 0;
	selected_irq = -1;
	for (j = 0; j < NR_IRQS; j++) {
		/* Is this an active IRQ? */
		if (!irq_desc[j].action)
			continue;
		if (imbalance <= IRQ_DELTA(max_loaded,j))
			continue;
		/* Try to find the IRQ that is closest to the imbalance
		 * without going over.
		 */
		if (move_this_load < IRQ_DELTA(max_loaded,j)) {
			move_this_load = IRQ_DELTA(max_loaded,j);
			selected_irq = j;
		}
	}
	if (selected_irq == -1) {
		goto tryanothercpu;
	}

	imbalance = move_this_load;
	
	/* For physical_balance case, we accumlated both load
	 * values in the one of the siblings cpu_irq[],
	 * to use the same code for physical and logical processors
	 * as much as possible. 
	 *
	 * NOTE: the cpu_irq[] array holds the sum of the load for
	 * sibling A and sibling B in the slot for the lowest numbered
	 * sibling (A), _AND_ the load for sibling B in the slot for
	 * the higher numbered sibling.
	 *
	 * We seek the least loaded sibling by making the comparison
	 * (A+B)/2 vs B
	 */
	load = CPU_IRQ(min_loaded) >> 1;
	for_each_cpu_mask(j, cpu_sibling_map[min_loaded]) {
		if (load > CPU_IRQ(j)) {
			/* This won't change cpu_sibling_map[min_loaded] */
			load = CPU_IRQ(j);
			min_loaded = j;
		}
	}

	cpus_and(allowed_mask, cpu_online_map, irq_affinity[selected_irq]);
	target_cpu_mask = cpumask_of_cpu(min_loaded);
	cpus_and(tmp, target_cpu_mask, allowed_mask);

	if (!cpus_empty(tmp)) {

		Dprintk("irq = %d moved to cpu = %d\n",
				selected_irq, min_loaded);
		/* mark for change destination */
		set_pending_irq(selected_irq, cpumask_of_cpu(min_loaded));

		/* Since we made a change, come back sooner to 
		 * check for more variation.
		 */
		balanced_irq_interval = max((long)MIN_BALANCED_IRQ_INTERVAL,
			balanced_irq_interval - BALANCED_IRQ_LESS_DELTA);	
		return;
	}
	goto tryanotherirq;

not_worth_the_effort:
	/*
	 * if we did not find an IRQ to move, then adjust the time interval
	 * upward
	 */
	balanced_irq_interval = min((long)MAX_BALANCED_IRQ_INTERVAL,
		balanced_irq_interval + BALANCED_IRQ_MORE_DELTA);	
	Dprintk("IRQ worth rotating not found\n");
	return;
}

static int balanced_irq(void *unused)
{
	int i;
	unsigned long prev_balance_time = jiffies;
	long time_remaining = balanced_irq_interval;

	daemonize("kirqd");
	
	/* push everything to CPU 0 to give us a starting point.  */
	for (i = 0 ; i < NR_IRQS ; i++) {
		pending_irq_cpumask[i] = cpumask_of_cpu(0);
		set_pending_irq(i, cpumask_of_cpu(0));
	}

	for ( ; ; ) {
		time_remaining = schedule_timeout_interruptible(time_remaining);
		try_to_freeze();
		if (time_after(jiffies,
				prev_balance_time+balanced_irq_interval)) {
			preempt_disable();
			do_irq_balance();
			prev_balance_time = jiffies;
			time_remaining = balanced_irq_interval;
			preempt_enable();
		}
	}
	return 0;
}

static int __init balanced_irq_init(void)
{
	int i;
	struct cpuinfo_x86 *c;
	cpumask_t tmp;

	cpus_shift_right(tmp, cpu_online_map, 2);
        c = &boot_cpu_data;
	/* When not overwritten by the command line ask subarchitecture. */
	if (irqbalance_disabled == IRQBALANCE_CHECK_ARCH)
		irqbalance_disabled = NO_BALANCE_IRQ;
	if (irqbalance_disabled)
		return 0;
	
	 /* disable irqbalance completely if there is only one processor online */
	if (num_online_cpus() < 2) {
		irqbalance_disabled = 1;
		return 0;
	}
	/*
	 * Enable physical balance only if more than 1 physical processor
	 * is present
	 */
	if (smp_num_siblings > 1 && !cpus_empty(tmp))
		physical_balance = 1;

	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_online(i))
			continue;
		irq_cpu_data[i].irq_delta = kmalloc(sizeof(unsigned long) * NR_IRQS, GFP_KERNEL);
		irq_cpu_data[i].last_irq = kmalloc(sizeof(unsigned long) * NR_IRQS, GFP_KERNEL);
		if (irq_cpu_data[i].irq_delta == NULL || irq_cpu_data[i].last_irq == NULL) {
			printk(KERN_ERR "balanced_irq_init: out of memory");
			goto failed;
		}
		memset(irq_cpu_data[i].irq_delta,0,sizeof(unsigned long) * NR_IRQS);
		memset(irq_cpu_data[i].last_irq,0,sizeof(unsigned long) * NR_IRQS);
	}
	
	printk(KERN_INFO "Starting balanced_irq\n");
	if (kernel_thread(balanced_irq, NULL, CLONE_KERNEL) >= 0) 
		return 0;
	else 
		printk(KERN_ERR "balanced_irq_init: failed to spawn balanced_irq");
failed:
	for (i = 0; i < NR_CPUS; i++) {
		kfree(irq_cpu_data[i].irq_delta);
		kfree(irq_cpu_data[i].last_irq);
	}
	return 0;
}

int __init irqbalance_disable(char *str)
{
	irqbalance_disabled = 1;
	return 0;
}

__setup("noirqbalance", irqbalance_disable);

late_initcall(balanced_irq_init);
#endif /* CONFIG_IRQBALANCE */
#endif /* CONFIG_SMP */

#ifndef CONFIG_SMP
void fastcall send_IPI_self(int vector)
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
	apic_write_around(APIC_ICR, cfg);
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

static int __init ioapic_setup(char *str)
{
	skip_ioapic_setup = 1;
	return 1;
}

__setup("noapic", ioapic_setup);

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

		if ((mp_bus_id_to_type[lbus] == MP_BUS_ISA ||
		     mp_bus_id_to_type[lbus] == MP_BUS_EISA ||
		     mp_bus_id_to_type[lbus] == MP_BUS_MCA ||
		     mp_bus_id_to_type[lbus] == MP_BUS_NEC98
		    ) &&
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

		if ((mp_bus_id_to_type[lbus] == MP_BUS_ISA ||
		     mp_bus_id_to_type[lbus] == MP_BUS_EISA ||
		     mp_bus_id_to_type[lbus] == MP_BUS_MCA ||
		     mp_bus_id_to_type[lbus] == MP_BUS_NEC98
		    ) &&
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

	apic_printk(APIC_DEBUG, "querying PCI -> IRQ mapping bus:%d, "
		"slot:%d, pin:%d.\n", bus, slot, pin);
	if (mp_bus_id_to_pci_bus[bus] == -1) {
		printk(KERN_WARNING "PCI BIOS passed nonexistent PCI bus %d!\n", bus);
		return -1;
	}
	for (i = 0; i < mp_irq_entries; i++) {
		int lbus = mp_irqs[i].mpc_srcbus;

		for (apic = 0; apic < nr_ioapics; apic++)
			if (mp_ioapics[apic].mpc_apicid == mp_irqs[i].mpc_dstapic ||
			    mp_irqs[i].mpc_dstapic == MP_APIC_ALL)
				break;

		if ((mp_bus_id_to_type[lbus] == MP_BUS_PCI) &&
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

/* EISA interrupts are always polarity zero and can be edge or level
 * trigger depending on the ELCR value.  If an interrupt is listed as
 * EISA conforming in the MP table, that means its trigger type must
 * be read in from the ELCR */

#define default_EISA_trigger(idx)	(EISA_ELCR(mp_irqs[idx].mpc_srcbusirq))
#define default_EISA_polarity(idx)	(0)

/* ISA interrupts are always polarity zero edge triggered,
 * when listed as conforming in the MP table. */

#define default_ISA_trigger(idx)	(0)
#define default_ISA_polarity(idx)	(0)

/* PCI interrupts are always polarity one level triggered,
 * when listed as conforming in the MP table. */

#define default_PCI_trigger(idx)	(1)
#define default_PCI_polarity(idx)	(1)

/* MCA interrupts are always polarity zero level triggered,
 * when listed as conforming in the MP table. */

#define default_MCA_trigger(idx)	(1)
#define default_MCA_polarity(idx)	(0)

/* NEC98 interrupts are always polarity zero edge triggered,
 * when listed as conforming in the MP table. */

#define default_NEC98_trigger(idx)     (0)
#define default_NEC98_polarity(idx)    (0)

static int __init MPBIOS_polarity(int idx)
{
	int bus = mp_irqs[idx].mpc_srcbus;
	int polarity;

	/*
	 * Determine IRQ line polarity (high active or low active):
	 */
	switch (mp_irqs[idx].mpc_irqflag & 3)
	{
		case 0: /* conforms, ie. bus-type dependent polarity */
		{
			switch (mp_bus_id_to_type[bus])
			{
				case MP_BUS_ISA: /* ISA pin */
				{
					polarity = default_ISA_polarity(idx);
					break;
				}
				case MP_BUS_EISA: /* EISA pin */
				{
					polarity = default_EISA_polarity(idx);
					break;
				}
				case MP_BUS_PCI: /* PCI pin */
				{
					polarity = default_PCI_polarity(idx);
					break;
				}
				case MP_BUS_MCA: /* MCA pin */
				{
					polarity = default_MCA_polarity(idx);
					break;
				}
				case MP_BUS_NEC98: /* NEC 98 pin */
				{
					polarity = default_NEC98_polarity(idx);
					break;
				}
				default:
				{
					printk(KERN_WARNING "broken BIOS!!\n");
					polarity = 1;
					break;
				}
			}
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
	int bus = mp_irqs[idx].mpc_srcbus;
	int trigger;

	/*
	 * Determine IRQ trigger mode (edge or level sensitive):
	 */
	switch ((mp_irqs[idx].mpc_irqflag>>2) & 3)
	{
		case 0: /* conforms, ie. bus-type dependent */
		{
			switch (mp_bus_id_to_type[bus])
			{
				case MP_BUS_ISA: /* ISA pin */
				{
					trigger = default_ISA_trigger(idx);
					break;
				}
				case MP_BUS_EISA: /* EISA pin */
				{
					trigger = default_EISA_trigger(idx);
					break;
				}
				case MP_BUS_PCI: /* PCI pin */
				{
					trigger = default_PCI_trigger(idx);
					break;
				}
				case MP_BUS_MCA: /* MCA pin */
				{
					trigger = default_MCA_trigger(idx);
					break;
				}
				case MP_BUS_NEC98: /* NEC 98 pin */
				{
					trigger = default_NEC98_trigger(idx);
					break;
				}
				default:
				{
					printk(KERN_WARNING "broken BIOS!!\n");
					trigger = 1;
					break;
				}
			}
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
	int bus = mp_irqs[idx].mpc_srcbus;

	/*
	 * Debugging check, we are in big trouble if this message pops up!
	 */
	if (mp_irqs[idx].mpc_dstirq != pin)
		printk(KERN_ERR "broken BIOS or MPTABLE parser, ayiee!!\n");

	switch (mp_bus_id_to_type[bus])
	{
		case MP_BUS_ISA: /* ISA pin */
		case MP_BUS_EISA:
		case MP_BUS_MCA:
		case MP_BUS_NEC98:
		{
			irq = mp_irqs[idx].mpc_srcbusirq;
			break;
		}
		case MP_BUS_PCI: /* PCI pin */
		{
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

			break;
		}
		default:
		{
			printk(KERN_ERR "unknown bus type %d.\n",bus); 
			irq = 0;
			break;
		}
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
			idx = find_irq_entry(apic,pin,mp_INT);
			if ((idx != -1) && (irq == pin_2_irq(idx,apic,pin)))
				return irq_trigger(idx);
		}
	}
	/*
	 * nonexistent IRQs are edge default
	 */
	return 0;
}

/* irq_vectors is indexed by the sum of all RTEs in all I/O APICs. */
u8 irq_vector[NR_IRQ_VECTORS] __read_mostly = { FIRST_DEVICE_VECTOR , 0 };

int assign_irq_vector(int irq)
{
	static int current_vector = FIRST_DEVICE_VECTOR, offset = 0;

	BUG_ON(irq >= NR_IRQ_VECTORS);
	if (irq != AUTO_ASSIGN && IO_APIC_VECTOR(irq) > 0)
		return IO_APIC_VECTOR(irq);
next:
	current_vector += 8;
	if (current_vector == SYSCALL_VECTOR)
		goto next;

	if (current_vector >= FIRST_SYSTEM_VECTOR) {
		offset++;
		if (!(offset%8))
			return -ENOSPC;
		current_vector = FIRST_DEVICE_VECTOR + offset;
	}

	vector_irq[current_vector] = irq;
	if (irq != AUTO_ASSIGN)
		IO_APIC_VECTOR(irq) = current_vector;

	return current_vector;
}

static struct hw_interrupt_type ioapic_level_type;
static struct hw_interrupt_type ioapic_edge_type;

#define IOAPIC_AUTO	-1
#define IOAPIC_EDGE	0
#define IOAPIC_LEVEL	1

static inline void ioapic_register_intr(int irq, int vector, unsigned long trigger)
{
	if (use_pci_vector() && !platform_legacy_irq(irq)) {
		if ((trigger == IOAPIC_AUTO && IO_APIC_irq_trigger(irq)) ||
				trigger == IOAPIC_LEVEL)
			irq_desc[vector].handler = &ioapic_level_type;
		else
			irq_desc[vector].handler = &ioapic_edge_type;
		set_intr_gate(vector, interrupt[vector]);
	} else	{
		if ((trigger == IOAPIC_AUTO && IO_APIC_irq_trigger(irq)) ||
				trigger == IOAPIC_LEVEL)
			irq_desc[irq].handler = &ioapic_level_type;
		else
			irq_desc[irq].handler = &ioapic_edge_type;
		set_intr_gate(vector, interrupt[irq]);
	}
}

static void __init setup_IO_APIC_irqs(void)
{
	struct IO_APIC_route_entry entry;
	int apic, pin, idx, irq, first_notcon = 1, vector;
	unsigned long flags;

	apic_printk(APIC_VERBOSE, KERN_DEBUG "init IO_APIC IRQs\n");

	for (apic = 0; apic < nr_ioapics; apic++) {
	for (pin = 0; pin < nr_ioapic_registers[apic]; pin++) {

		/*
		 * add it to the IO-APIC irq-routing table:
		 */
		memset(&entry,0,sizeof(entry));

		entry.delivery_mode = INT_DELIVERY_MODE;
		entry.dest_mode = INT_DEST_MODE;
		entry.mask = 0;				/* enable IRQ */
		entry.dest.logical.logical_dest = 
					cpu_mask_to_apicid(TARGET_CPUS);

		idx = find_irq_entry(apic,pin,mp_INT);
		if (idx == -1) {
			if (first_notcon) {
				apic_printk(APIC_VERBOSE, KERN_DEBUG
						" IO-APIC (apicid-pin) %d-%d",
						mp_ioapics[apic].mpc_apicid,
						pin);
				first_notcon = 0;
			} else
				apic_printk(APIC_VERBOSE, ", %d-%d",
					mp_ioapics[apic].mpc_apicid, pin);
			continue;
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
		spin_lock_irqsave(&ioapic_lock, flags);
		io_apic_write(apic, 0x11+2*pin, *(((int *)&entry)+1));
		io_apic_write(apic, 0x10+2*pin, *(((int *)&entry)+0));
		set_native_irq_info(irq, TARGET_CPUS);
		spin_unlock_irqrestore(&ioapic_lock, flags);
	}
	}

	if (!first_notcon)
		apic_printk(APIC_VERBOSE, " not connected.\n");
}

/*
 * Set up the 8259A-master output pin:
 */
static void __init setup_ExtINT_IRQ0_pin(unsigned int apic, unsigned int pin, int vector)
{
	struct IO_APIC_route_entry entry;
	unsigned long flags;

	memset(&entry,0,sizeof(entry));

	disable_8259A_irq(0);

	/* mask LVT0 */
	apic_write_around(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_EXTINT);

	/*
	 * We use logical delivery to get the timer IRQ
	 * to the first CPU.
	 */
	entry.dest_mode = INT_DEST_MODE;
	entry.mask = 0;					/* unmask IRQ now */
	entry.dest.logical.logical_dest = cpu_mask_to_apicid(TARGET_CPUS);
	entry.delivery_mode = INT_DELIVERY_MODE;
	entry.polarity = 0;
	entry.trigger = 0;
	entry.vector = vector;

	/*
	 * The timer IRQ doesn't have to know that behind the
	 * scene we have a 8259A-master in AEOI mode ...
	 */
	irq_desc[0].handler = &ioapic_edge_type;

	/*
	 * Add it to the IO-APIC irq-routing table:
	 */
	spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_write(apic, 0x11+2*pin, *(((int *)&entry)+1));
	io_apic_write(apic, 0x10+2*pin, *(((int *)&entry)+0));
	spin_unlock_irqrestore(&ioapic_lock, flags);

	enable_8259A_irq(0);
}

static inline void UNEXPECTED_IO_APIC(void)
{
}

void __init print_IO_APIC(void)
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
	if (reg_01.bits.version >= 0x20)
		reg_03.raw = io_apic_read(apic, 3);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	printk(KERN_DEBUG "IO APIC #%d......\n", mp_ioapics[apic].mpc_apicid);
	printk(KERN_DEBUG ".... register #00: %08X\n", reg_00.raw);
	printk(KERN_DEBUG ".......    : physical APIC id: %02X\n", reg_00.bits.ID);
	printk(KERN_DEBUG ".......    : Delivery Type: %X\n", reg_00.bits.delivery_type);
	printk(KERN_DEBUG ".......    : LTS          : %X\n", reg_00.bits.LTS);
	if (reg_00.bits.ID >= get_physical_broadcast())
		UNEXPECTED_IO_APIC();
	if (reg_00.bits.__reserved_1 || reg_00.bits.__reserved_2)
		UNEXPECTED_IO_APIC();

	printk(KERN_DEBUG ".... register #01: %08X\n", reg_01.raw);
	printk(KERN_DEBUG ".......     : max redirection entries: %04X\n", reg_01.bits.entries);
	if (	(reg_01.bits.entries != 0x0f) && /* older (Neptune) boards */
		(reg_01.bits.entries != 0x17) && /* typical ISA+PCI boards */
		(reg_01.bits.entries != 0x1b) && /* Compaq Proliant boards */
		(reg_01.bits.entries != 0x1f) && /* dual Xeon boards */
		(reg_01.bits.entries != 0x22) && /* bigger Xeon boards */
		(reg_01.bits.entries != 0x2E) &&
		(reg_01.bits.entries != 0x3F)
	)
		UNEXPECTED_IO_APIC();

	printk(KERN_DEBUG ".......     : PRQ implemented: %X\n", reg_01.bits.PRQ);
	printk(KERN_DEBUG ".......     : IO APIC version: %04X\n", reg_01.bits.version);
	if (	(reg_01.bits.version != 0x01) && /* 82489DX IO-APICs */
		(reg_01.bits.version != 0x10) && /* oldest IO-APICs */
		(reg_01.bits.version != 0x11) && /* Pentium/Pro IO-APICs */
		(reg_01.bits.version != 0x13) && /* Xeon IO-APICs */
		(reg_01.bits.version != 0x20)    /* Intel P64H (82806 AA) */
	)
		UNEXPECTED_IO_APIC();
	if (reg_01.bits.__reserved_1 || reg_01.bits.__reserved_2)
		UNEXPECTED_IO_APIC();

	/*
	 * Some Intel chipsets with IO APIC VERSION of 0x1? don't have reg_02,
	 * but the value of reg_02 is read as the previous read register
	 * value, so ignore it if reg_02 == reg_01.
	 */
	if (reg_01.bits.version >= 0x10 && reg_02.raw != reg_01.raw) {
		printk(KERN_DEBUG ".... register #02: %08X\n", reg_02.raw);
		printk(KERN_DEBUG ".......     : arbitration: %02X\n", reg_02.bits.arbitration);
		if (reg_02.bits.__reserved_1 || reg_02.bits.__reserved_2)
			UNEXPECTED_IO_APIC();
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
		if (reg_03.bits.__reserved_1)
			UNEXPECTED_IO_APIC();
	}

	printk(KERN_DEBUG ".... IRQ redirection table:\n");

	printk(KERN_DEBUG " NR Log Phy Mask Trig IRR Pol"
			  " Stat Dest Deli Vect:   \n");

	for (i = 0; i <= reg_01.bits.entries; i++) {
		struct IO_APIC_route_entry entry;

		spin_lock_irqsave(&ioapic_lock, flags);
		*(((int *)&entry)+0) = io_apic_read(apic, 0x10+i*2);
		*(((int *)&entry)+1) = io_apic_read(apic, 0x11+i*2);
		spin_unlock_irqrestore(&ioapic_lock, flags);

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
	if (use_pci_vector())
		printk(KERN_INFO "Using vector-based indexing\n");
	printk(KERN_DEBUG "IRQ to pin mappings:\n");
	for (i = 0; i < NR_IRQS; i++) {
		struct irq_pin_list *entry = irq_2_pin + i;
		if (entry->pin < 0)
			continue;
 		if (use_pci_vector() && !platform_legacy_irq(i))
			printk(KERN_DEBUG "IRQ%d ", IO_APIC_VECTOR(i));
		else
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

static void print_APIC_bitfield (int base)
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

void /*__init*/ print_local_APIC(void * dummy)
{
	unsigned int v, ver, maxlvt;

	if (apic_verbosity == APIC_QUIET)
		return;

	printk("\n" KERN_DEBUG "printing local APIC contents on CPU#%d/%d:\n",
		smp_processor_id(), hard_smp_processor_id());
	v = apic_read(APIC_ID);
	printk(KERN_INFO "... APIC ID:      %08x (%01x)\n", v, GET_APIC_ID(v));
	v = apic_read(APIC_LVR);
	printk(KERN_INFO "... APIC VERSION: %08x\n", v);
	ver = GET_APIC_VERSION(v);
	maxlvt = get_maxlvt();

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

void /*__init*/ print_PIC(void)
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

static void __init enable_IO_APIC(void)
{
	union IO_APIC_reg_01 reg_01;
	int i8259_apic, i8259_pin;
	int i, apic;
	unsigned long flags;

	for (i = 0; i < PIN_MAP_SIZE; i++) {
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
	for(apic = 0; apic < nr_ioapics; apic++) {
		int pin;
		/* See if any of the pins is in ExtINT mode */
		for (pin = 0; pin < nr_ioapic_registers[apic]; pin++) {
			struct IO_APIC_route_entry entry;
			spin_lock_irqsave(&ioapic_lock, flags);
			*(((int *)&entry) + 0) = io_apic_read(apic, 0x10 + 2 * pin);
			*(((int *)&entry) + 1) = io_apic_read(apic, 0x11 + 2 * pin);
			spin_unlock_irqrestore(&ioapic_lock, flags);


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
		unsigned long flags;

		memset(&entry, 0, sizeof(entry));
		entry.mask            = 0; /* Enabled */
		entry.trigger         = 0; /* Edge */
		entry.irr             = 0;
		entry.polarity        = 0; /* High */
		entry.delivery_status = 0;
		entry.dest_mode       = 0; /* Physical */
		entry.delivery_mode   = dest_ExtINT; /* ExtInt */
		entry.vector          = 0;
		entry.dest.physical.physical_dest =
					GET_APIC_ID(apic_read(APIC_ID));

		/*
		 * Add it to the IO-APIC irq-routing table:
		 */
		spin_lock_irqsave(&ioapic_lock, flags);
		io_apic_write(ioapic_i8259.apic, 0x11+2*ioapic_i8259.pin,
			*(((int *)&entry)+1));
		io_apic_write(ioapic_i8259.apic, 0x10+2*ioapic_i8259.pin,
			*(((int *)&entry)+0));
		spin_unlock_irqrestore(&ioapic_lock, flags);
	}
	disconnect_bsp_APIC(ioapic_i8259.pin != -1);
}

/*
 * function to set the IO-APIC physical IDs based on the
 * values stored in the MPC table.
 *
 * by Matt Domsch <Matt_Domsch@dell.com>  Tue Dec 21 12:25:05 CST 1999
 */

#ifndef CONFIG_X86_NUMAQ
static void __init setup_ioapic_ids_from_mpc(void)
{
	union IO_APIC_reg_00 reg_00;
	physid_mask_t phys_id_present_map;
	int apic;
	int i;
	unsigned char old_id;
	unsigned long flags;

	/*
	 * Don't check I/O APIC IDs for xAPIC systems.  They have
	 * no meaning without the serial APIC bus.
	 */
	if (!(boot_cpu_data.x86_vendor == X86_VENDOR_INTEL && boot_cpu_data.x86 < 15))
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
		
		old_id = mp_ioapics[apic].mpc_apicid;

		if (mp_ioapics[apic].mpc_apicid >= get_physical_broadcast()) {
			printk(KERN_ERR "BIOS bug, IO-APIC#%d ID is %d in the MPC table!...\n",
				apic, mp_ioapics[apic].mpc_apicid);
			printk(KERN_ERR "... fixing up to %d. (tell your hw vendor)\n",
				reg_00.bits.ID);
			mp_ioapics[apic].mpc_apicid = reg_00.bits.ID;
		}

		/*
		 * Sanity check, is the ID really free? Every APIC in a
		 * system must have a unique ID or we get lots of nice
		 * 'stuck on smp_invalidate_needed IPI wait' messages.
		 */
		if (check_apicid_used(phys_id_present_map,
					mp_ioapics[apic].mpc_apicid)) {
			printk(KERN_ERR "BIOS bug, IO-APIC#%d ID %d is already used!...\n",
				apic, mp_ioapics[apic].mpc_apicid);
			for (i = 0; i < get_physical_broadcast(); i++)
				if (!physid_isset(i, phys_id_present_map))
					break;
			if (i >= get_physical_broadcast())
				panic("Max APIC ID exceeded!\n");
			printk(KERN_ERR "... fixing up to %d. (tell your hw vendor)\n",
				i);
			physid_set(i, phys_id_present_map);
			mp_ioapics[apic].mpc_apicid = i;
		} else {
			physid_mask_t tmp;
			tmp = apicid_to_cpu_present(mp_ioapics[apic].mpc_apicid);
			apic_printk(APIC_VERBOSE, "Setting %d in the "
					"phys_id_present_map\n",
					mp_ioapics[apic].mpc_apicid);
			physids_or(phys_id_present_map, phys_id_present_map, tmp);
		}


		/*
		 * We need to adjust the IRQ routing table
		 * if the ID changed.
		 */
		if (old_id != mp_ioapics[apic].mpc_apicid)
			for (i = 0; i < mp_irq_entries; i++)
				if (mp_irqs[i].mpc_dstapic == old_id)
					mp_irqs[i].mpc_dstapic
						= mp_ioapics[apic].mpc_apicid;

		/*
		 * Read the right value from the MPC table and
		 * write it into the ID register.
	 	 */
		apic_printk(APIC_VERBOSE, KERN_INFO
			"...changing IO-APIC physical APIC ID to %d ...",
			mp_ioapics[apic].mpc_apicid);

		reg_00.bits.ID = mp_ioapics[apic].mpc_apicid;
		spin_lock_irqsave(&ioapic_lock, flags);
		io_apic_write(apic, 0, reg_00.raw);
		spin_unlock_irqrestore(&ioapic_lock, flags);

		/*
		 * Sanity check
		 */
		spin_lock_irqsave(&ioapic_lock, flags);
		reg_00.raw = io_apic_read(apic, 0);
		spin_unlock_irqrestore(&ioapic_lock, flags);
		if (reg_00.bits.ID != mp_ioapics[apic].mpc_apicid)
			printk("could not set ID!\n");
		else
			apic_printk(APIC_VERBOSE, " ok.\n");
	}
}
#else
static void __init setup_ioapic_ids_from_mpc(void) { }
#endif

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

	local_irq_enable();
	/* Let ten ticks pass... */
	mdelay((10 * 1000) / HZ);

	/*
	 * Expect a few ticks at least, to be sure some possible
	 * glue logic does not lock up after one or two first
	 * ticks in a non-ExtINT mode.  Also the local APIC
	 * might have cached one ExtINT interrupt.  Finally, at
	 * least one tick may be lost due to delays.
	 */
	if (jiffies - t1 > 4)
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
static unsigned int startup_edge_ioapic_irq(unsigned int irq)
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

/*
 * Once we have recorded IRQ_PENDING already, we can mask the
 * interrupt for real. This prevents IRQ storms from unhandled
 * devices.
 */
static void ack_edge_ioapic_irq(unsigned int irq)
{
	move_irq(irq);
	if ((irq_desc[irq].status & (IRQ_PENDING | IRQ_DISABLED))
					== (IRQ_PENDING | IRQ_DISABLED))
		mask_IO_APIC_irq(irq);
	ack_APIC_irq();
}

/*
 * Level triggered interrupts can just be masked,
 * and shutting down and starting up the interrupt
 * is the same as enabling and disabling them -- except
 * with a startup need to return a "was pending" value.
 *
 * Level triggered interrupts are special because we
 * do not touch any IO-APIC register while handling
 * them. We ack the APIC in the end-IRQ handler, not
 * in the start-IRQ-handler. Protection against reentrance
 * from the same interrupt is still provided, both by the
 * generic IRQ layer and by the fact that an unacked local
 * APIC does not accept IRQs.
 */
static unsigned int startup_level_ioapic_irq (unsigned int irq)
{
	unmask_IO_APIC_irq(irq);

	return 0; /* don't check for pending */
}

static void end_level_ioapic_irq (unsigned int irq)
{
	unsigned long v;
	int i;

	move_irq(irq);
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
	i = IO_APIC_VECTOR(irq);

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

#ifdef CONFIG_PCI_MSI
static unsigned int startup_edge_ioapic_vector(unsigned int vector)
{
	int irq = vector_to_irq(vector);

	return startup_edge_ioapic_irq(irq);
}

static void ack_edge_ioapic_vector(unsigned int vector)
{
	int irq = vector_to_irq(vector);

	move_native_irq(vector);
	ack_edge_ioapic_irq(irq);
}

static unsigned int startup_level_ioapic_vector (unsigned int vector)
{
	int irq = vector_to_irq(vector);

	return startup_level_ioapic_irq (irq);
}

static void end_level_ioapic_vector (unsigned int vector)
{
	int irq = vector_to_irq(vector);

	move_native_irq(vector);
	end_level_ioapic_irq(irq);
}

static void mask_IO_APIC_vector (unsigned int vector)
{
	int irq = vector_to_irq(vector);

	mask_IO_APIC_irq(irq);
}

static void unmask_IO_APIC_vector (unsigned int vector)
{
	int irq = vector_to_irq(vector);

	unmask_IO_APIC_irq(irq);
}

#ifdef CONFIG_SMP
static void set_ioapic_affinity_vector (unsigned int vector,
					cpumask_t cpu_mask)
{
	int irq = vector_to_irq(vector);

	set_native_irq_info(vector, cpu_mask);
	set_ioapic_affinity_irq(irq, cpu_mask);
}
#endif
#endif

/*
 * Level and edge triggered IO-APIC interrupts need different handling,
 * so we use two separate IRQ descriptors. Edge triggered IRQs can be
 * handled with the level-triggered descriptor, but that one has slightly
 * more overhead. Level-triggered interrupts cannot be handled with the
 * edge-triggered handler, without risking IRQ storms and other ugly
 * races.
 */
static struct hw_interrupt_type ioapic_edge_type __read_mostly = {
	.typename 	= "IO-APIC-edge",
	.startup 	= startup_edge_ioapic,
	.shutdown 	= shutdown_edge_ioapic,
	.enable 	= enable_edge_ioapic,
	.disable 	= disable_edge_ioapic,
	.ack 		= ack_edge_ioapic,
	.end 		= end_edge_ioapic,
#ifdef CONFIG_SMP
	.set_affinity 	= set_ioapic_affinity,
#endif
};

static struct hw_interrupt_type ioapic_level_type __read_mostly = {
	.typename 	= "IO-APIC-level",
	.startup 	= startup_level_ioapic,
	.shutdown 	= shutdown_level_ioapic,
	.enable 	= enable_level_ioapic,
	.disable 	= disable_level_ioapic,
	.ack 		= mask_and_ack_level_ioapic,
	.end 		= end_level_ioapic,
#ifdef CONFIG_SMP
	.set_affinity 	= set_ioapic_affinity,
#endif
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
		int tmp = irq;
		if (use_pci_vector()) {
			if (!platform_legacy_irq(tmp))
				if ((tmp = vector_to_irq(tmp)) == -1)
					continue;
		}
		if (IO_APIC_IRQ(tmp) && !IO_APIC_VECTOR(tmp)) {
			/*
			 * Hmm.. We don't have an entry for this,
			 * so default to an old-fashioned 8259
			 * interrupt if we can..
			 */
			if (irq < 16)
				make_8259A_irq(irq);
			else
				/* Strange. Oh, well.. */
				irq_desc[irq].handler = &no_irq_type;
		}
	}
}

static void enable_lapic_irq (unsigned int irq)
{
	unsigned long v;

	v = apic_read(APIC_LVT0);
	apic_write_around(APIC_LVT0, v & ~APIC_LVT_MASKED);
}

static void disable_lapic_irq (unsigned int irq)
{
	unsigned long v;

	v = apic_read(APIC_LVT0);
	apic_write_around(APIC_LVT0, v | APIC_LVT_MASKED);
}

static void ack_lapic_irq (unsigned int irq)
{
	ack_APIC_irq();
}

static void end_lapic_irq (unsigned int i) { /* nothing */ }

static struct hw_interrupt_type lapic_irq_type __read_mostly = {
	.typename 	= "local-APIC-edge",
	.startup 	= NULL, /* startup_irq() not used for IRQ0 */
	.shutdown 	= NULL, /* shutdown_irq() not used for IRQ0 */
	.enable 	= enable_lapic_irq,
	.disable 	= disable_lapic_irq,
	.ack 		= ack_lapic_irq,
	.end 		= end_lapic_irq
};

static void setup_nmi (void)
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

	on_each_cpu(enable_NMI_through_LVT0, NULL, 1, 1);

	apic_printk(APIC_VERBOSE, " done.\n");
}

/*
 * This looks a bit hackish but it's about the only one way of sending
 * a few INTA cycles to 8259As and any associated glue logic.  ICR does
 * not support the ExtINT mode, unfortunately.  We need to send these
 * cycles as some i82489DX-based boards have glue logic that keeps the
 * 8259A interrupt line asserted until INTA.  --macro
 */
static inline void unlock_ExtINT_logic(void)
{
	int apic, pin, i;
	struct IO_APIC_route_entry entry0, entry1;
	unsigned char save_control, save_freq_select;
	unsigned long flags;

	pin  = find_isa_irq_pin(8, mp_INT);
	apic = find_isa_irq_apic(8, mp_INT);
	if (pin == -1)
		return;

	spin_lock_irqsave(&ioapic_lock, flags);
	*(((int *)&entry0) + 1) = io_apic_read(apic, 0x11 + 2 * pin);
	*(((int *)&entry0) + 0) = io_apic_read(apic, 0x10 + 2 * pin);
	spin_unlock_irqrestore(&ioapic_lock, flags);
	clear_IO_APIC_pin(apic, pin);

	memset(&entry1, 0, sizeof(entry1));

	entry1.dest_mode = 0;			/* physical delivery */
	entry1.mask = 0;			/* unmask IRQ now */
	entry1.dest.physical.physical_dest = hard_smp_processor_id();
	entry1.delivery_mode = dest_ExtINT;
	entry1.polarity = entry0.polarity;
	entry1.trigger = 0;
	entry1.vector = 0;

	spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_write(apic, 0x11 + 2 * pin, *(((int *)&entry1) + 1));
	io_apic_write(apic, 0x10 + 2 * pin, *(((int *)&entry1) + 0));
	spin_unlock_irqrestore(&ioapic_lock, flags);

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

	spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_write(apic, 0x11 + 2 * pin, *(((int *)&entry0) + 1));
	io_apic_write(apic, 0x10 + 2 * pin, *(((int *)&entry0) + 0));
	spin_unlock_irqrestore(&ioapic_lock, flags);
}

/*
 * This code may look a bit paranoid, but it's supposed to cooperate with
 * a wide range of boards and BIOS bugs.  Fortunately only the timer IRQ
 * is so screwy.  Thanks to Brian Perkins for testing/hacking this beast
 * fanatically on his truly buggy board.
 */
static inline void check_timer(void)
{
	int apic1, pin1, apic2, pin2;
	int vector;

	/*
	 * get/set the timer IRQ vector:
	 */
	disable_8259A_irq(0);
	vector = assign_irq_vector(0);
	set_intr_gate(vector, interrupt[0]);

	/*
	 * Subtle, code in do_timer_interrupt() expects an AEOI
	 * mode for the 8259A whenever interrupts are routed
	 * through I/O APICs.  Also IRQ0 has to be enabled in
	 * the 8259A which implies the virtual wire has to be
	 * disabled in the local APIC.
	 */
	apic_write_around(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_EXTINT);
	init_8259A(1);
	timer_ack = 1;
	enable_8259A_irq(0);

	pin1  = find_isa_irq_pin(0, mp_INT);
	apic1 = find_isa_irq_apic(0, mp_INT);
	pin2  = ioapic_i8259.pin;
	apic2 = ioapic_i8259.apic;

	printk(KERN_INFO "..TIMER: vector=0x%02X apic1=%d pin1=%d apic2=%d pin2=%d\n",
		vector, apic1, pin1, apic2, pin2);

	if (pin1 != -1) {
		/*
		 * Ok, does IRQ0 through the IOAPIC work?
		 */
		unmask_IO_APIC_irq(0);
		if (timer_irq_works()) {
			if (nmi_watchdog == NMI_IO_APIC) {
				disable_8259A_irq(0);
				setup_nmi();
				enable_8259A_irq(0);
			}
			if (disable_timer_pin_1 > 0)
				clear_IO_APIC_pin(0, pin1);
			return;
		}
		clear_IO_APIC_pin(apic1, pin1);
		printk(KERN_ERR "..MP-BIOS bug: 8254 timer not connected to "
				"IO-APIC\n");
	}

	printk(KERN_INFO "...trying to set up timer (IRQ0) through the 8259A ... ");
	if (pin2 != -1) {
		printk("\n..... (found pin %d) ...", pin2);
		/*
		 * legacy devices should be connected to IO APIC #0
		 */
		setup_ExtINT_IRQ0_pin(apic2, pin2, vector);
		if (timer_irq_works()) {
			printk("works.\n");
			if (pin1 != -1)
				replace_pin_at_irq(0, apic1, pin1, apic2, pin2);
			else
				add_pin_to_irq(0, apic2, pin2);
			if (nmi_watchdog == NMI_IO_APIC) {
				setup_nmi();
			}
			return;
		}
		/*
		 * Cleanup, just in case ...
		 */
		clear_IO_APIC_pin(apic2, pin2);
	}
	printk(" failed.\n");

	if (nmi_watchdog == NMI_IO_APIC) {
		printk(KERN_WARNING "timer doesn't work through the IO-APIC - disabling NMI Watchdog!\n");
		nmi_watchdog = 0;
	}

	printk(KERN_INFO "...trying to set up timer as Virtual Wire IRQ...");

	disable_8259A_irq(0);
	irq_desc[0].handler = &lapic_irq_type;
	apic_write_around(APIC_LVT0, APIC_DM_FIXED | vector);	/* Fixed mode */
	enable_8259A_irq(0);

	if (timer_irq_works()) {
		printk(" works.\n");
		return;
	}
	apic_write_around(APIC_LVT0, APIC_LVT_MASKED | APIC_DM_FIXED | vector);
	printk(" failed.\n");

	printk(KERN_INFO "...trying to set up timer as ExtINT IRQ...");

	timer_ack = 0;
	init_8259A(0);
	make_8259A_irq(0);
	apic_write_around(APIC_LVT0, APIC_DM_EXTINT);

	unlock_ExtINT_logic();

	if (timer_irq_works()) {
		printk(" works.\n");
		return;
	}
	printk(" failed :(.\n");
	panic("IO-APIC + timer doesn't work!  Boot with apic=debug and send a "
		"report.  Then try booting with the 'noapic' option");
}

/*
 *
 * IRQ's that are handled by the PIC in the MPS IOAPIC case.
 * - IRQ2 is the cascade IRQ, and cannot be a io-apic IRQ.
 *   Linux doesn't really care, as it's not actually used
 *   for any interrupt handling anyway.
 */
#define PIC_IRQS	(1 << PIC_CASCADE_IR)

void __init setup_IO_APIC(void)
{
	enable_IO_APIC();

	if (acpi_ioapic)
		io_apic_irqs = ~0;	/* all IRQs go through IOAPIC */
	else
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
	if (!acpi_ioapic)
		print_IO_APIC();
}

/*
 *	Called after all the initialization is done. If we didnt find any
 *	APIC bugs then we can allow the modify fast path
 */
 
static int __init io_apic_bug_finalize(void)
{
	if(sis_apic_bug == -1)
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
	unsigned long flags;
	int i;
	
	data = container_of(dev, struct sysfs_ioapic_data, dev);
	entry = data->entry;
	spin_lock_irqsave(&ioapic_lock, flags);
	for (i = 0; i < nr_ioapic_registers[dev->id]; i ++, entry ++ ) {
		*(((int *)entry) + 1) = io_apic_read(dev->id, 0x11 + 2 * i);
		*(((int *)entry) + 0) = io_apic_read(dev->id, 0x10 + 2 * i);
	}
	spin_unlock_irqrestore(&ioapic_lock, flags);

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
	for (i = 0; i < nr_ioapic_registers[dev->id]; i ++, entry ++ ) {
		io_apic_write(dev->id, 0x11+2*i, *(((int *)entry)+1));
		io_apic_write(dev->id, 0x10+2*i, *(((int *)entry)+0));
	}
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return 0;
}

static struct sysdev_class ioapic_sysdev_class = {
	set_kset_name("ioapic"),
	.suspend = ioapic_suspend,
	.resume = ioapic_resume,
};

static int __init ioapic_init_sysfs(void)
{
	struct sys_device * dev;
	int i, size, error = 0;

	error = sysdev_class_register(&ioapic_sysdev_class);
	if (error)
		return error;

	for (i = 0; i < nr_ioapics; i++ ) {
		size = sizeof(struct sys_device) + nr_ioapic_registers[i] 
			* sizeof(struct IO_APIC_route_entry);
		mp_ioapic_data[i] = kmalloc(size, GFP_KERNEL);
		if (!mp_ioapic_data[i]) {
			printk(KERN_ERR "Can't suspend/resume IOAPIC %d\n", i);
			continue;
		}
		memset(mp_ioapic_data[i], 0, size);
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

/* --------------------------------------------------------------------------
                          ACPI-based IOAPIC Configuration
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI

int __init io_apic_get_unique_id (int ioapic, int apic_id)
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


int __init io_apic_get_version (int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return reg_01.bits.version;
}


int __init io_apic_get_redir_entries (int ioapic)
{
	union IO_APIC_reg_01	reg_01;
	unsigned long flags;

	spin_lock_irqsave(&ioapic_lock, flags);
	reg_01.raw = io_apic_read(ioapic, 1);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return reg_01.bits.entries;
}


int io_apic_set_pci_routing (int ioapic, int pin, int irq, int edge_level, int active_high_low)
{
	struct IO_APIC_route_entry entry;
	unsigned long flags;

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

	memset(&entry,0,sizeof(entry));

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
		mp_ioapics[ioapic].mpc_apicid, pin, entry.vector, irq,
		edge_level, active_high_low);

	ioapic_register_intr(irq, entry.vector, edge_level);

	if (!ioapic && (irq < 16))
		disable_8259A_irq(irq);

	spin_lock_irqsave(&ioapic_lock, flags);
	io_apic_write(ioapic, 0x11+2*pin, *(((int *)&entry)+1));
	io_apic_write(ioapic, 0x10+2*pin, *(((int *)&entry)+0));
	set_native_irq_info(use_pci_vector() ? entry.vector : irq, TARGET_CPUS);
	spin_unlock_irqrestore(&ioapic_lock, flags);

	return 0;
}

#endif /* CONFIG_ACPI */
