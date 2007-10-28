/* -*- mode: c; c-basic-offset: 8 -*- */

/* Copyright (C) 1999,2001
 *
 * Author: J.E.J.Bottomley@HansenPartnership.com
 *
 * linux/arch/i386/kernel/voyager_smp.c
 *
 * This file provides all the same external entries as smp.c but uses
 * the voyager hal to provide the functionality
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/mc146818rtc.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bootmem.h>
#include <linux/completion.h>
#include <asm/desc.h>
#include <asm/voyager.h>
#include <asm/vic.h>
#include <asm/mtrr.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/arch_hooks.h>

/* TLB state -- visible externally, indexed physically */
DEFINE_PER_CPU_SHARED_ALIGNED(struct tlb_state, cpu_tlbstate) = { &init_mm, 0 };

/* CPU IRQ affinity -- set to all ones initially */
static unsigned long cpu_irq_affinity[NR_CPUS] __cacheline_aligned = { [0 ... NR_CPUS-1]  = ~0UL };

/* per CPU data structure (for /proc/cpuinfo et al), visible externally
 * indexed physically */
DEFINE_PER_CPU_SHARED_ALIGNED(struct cpuinfo_x86, cpu_info);
EXPORT_PER_CPU_SYMBOL(cpu_info);

/* physical ID of the CPU used to boot the system */
unsigned char boot_cpu_id;

/* The memory line addresses for the Quad CPIs */
struct voyager_qic_cpi *voyager_quad_cpi_addr[NR_CPUS] __cacheline_aligned;

/* The masks for the Extended VIC processors, filled in by cat_init */
__u32 voyager_extended_vic_processors = 0;

/* Masks for the extended Quad processors which cannot be VIC booted */
__u32 voyager_allowed_boot_processors = 0;

/* The mask for the Quad Processors (both extended and non-extended) */
__u32 voyager_quad_processors = 0;

/* Total count of live CPUs, used in process.c to display
 * the CPU information and in irq.c for the per CPU irq
 * activity count.  Finally exported by i386_ksyms.c */
static int voyager_extended_cpus = 1;

/* Have we found an SMP box - used by time.c to do the profiling
   interrupt for timeslicing; do not set to 1 until the per CPU timer
   interrupt is active */
int smp_found_config = 0;

/* Used for the invalidate map that's also checked in the spinlock */
static volatile unsigned long smp_invalidate_needed;

/* Bitmask of currently online CPUs - used by setup.c for
   /proc/cpuinfo, visible externally but still physical */
cpumask_t cpu_online_map = CPU_MASK_NONE;
EXPORT_SYMBOL(cpu_online_map);

/* Bitmask of CPUs present in the system - exported by i386_syms.c, used
 * by scheduler but indexed physically */
cpumask_t phys_cpu_present_map = CPU_MASK_NONE;


/* The internal functions */
static void send_CPI(__u32 cpuset, __u8 cpi);
static void ack_CPI(__u8 cpi);
static int ack_QIC_CPI(__u8 cpi);
static void ack_special_QIC_CPI(__u8 cpi);
static void ack_VIC_CPI(__u8 cpi);
static void send_CPI_allbutself(__u8 cpi);
static void mask_vic_irq(unsigned int irq);
static void unmask_vic_irq(unsigned int irq);
static unsigned int startup_vic_irq(unsigned int irq);
static void enable_local_vic_irq(unsigned int irq);
static void disable_local_vic_irq(unsigned int irq);
static void before_handle_vic_irq(unsigned int irq);
static void after_handle_vic_irq(unsigned int irq);
static void set_vic_irq_affinity(unsigned int irq, cpumask_t mask);
static void ack_vic_irq(unsigned int irq);
static void vic_enable_cpi(void);
static void do_boot_cpu(__u8 cpuid);
static void do_quad_bootstrap(void);

int hard_smp_processor_id(void);
int safe_smp_processor_id(void);

/* Inline functions */
static inline void
send_one_QIC_CPI(__u8 cpu, __u8 cpi)
{
	voyager_quad_cpi_addr[cpu]->qic_cpi[cpi].cpi =
		(smp_processor_id() << 16) + cpi;
}

static inline void
send_QIC_CPI(__u32 cpuset, __u8 cpi)
{
	int cpu;

	for_each_online_cpu(cpu) {
		if(cpuset & (1<<cpu)) {
#ifdef VOYAGER_DEBUG
			if(!cpu_isset(cpu, cpu_online_map))
				VDEBUG(("CPU%d sending cpi %d to CPU%d not in cpu_online_map\n", hard_smp_processor_id(), cpi, cpu));
#endif
			send_one_QIC_CPI(cpu, cpi - QIC_CPI_OFFSET);
		}
	}
}

static inline void
wrapper_smp_local_timer_interrupt(void)
{
	irq_enter();
	smp_local_timer_interrupt();
	irq_exit();
}

static inline void
send_one_CPI(__u8 cpu, __u8 cpi)
{
	if(voyager_quad_processors & (1<<cpu))
		send_one_QIC_CPI(cpu, cpi - QIC_CPI_OFFSET);
	else
		send_CPI(1<<cpu, cpi);
}

static inline void
send_CPI_allbutself(__u8 cpi)
{
	__u8 cpu = smp_processor_id();
	__u32 mask = cpus_addr(cpu_online_map)[0] & ~(1 << cpu);
	send_CPI(mask, cpi);
}

static inline int
is_cpu_quad(void)
{
	__u8 cpumask = inb(VIC_PROC_WHO_AM_I);
	return ((cpumask & QUAD_IDENTIFIER) == QUAD_IDENTIFIER);
}

static inline int
is_cpu_extended(void)
{
	__u8 cpu = hard_smp_processor_id();

	return(voyager_extended_vic_processors & (1<<cpu));
}

static inline int
is_cpu_vic_boot(void)
{
	__u8 cpu = hard_smp_processor_id();

	return(voyager_extended_vic_processors
	       & voyager_allowed_boot_processors & (1<<cpu));
}


static inline void
ack_CPI(__u8 cpi)
{
	switch(cpi) {
	case VIC_CPU_BOOT_CPI:
		if(is_cpu_quad() && !is_cpu_vic_boot())
			ack_QIC_CPI(cpi);
		else
			ack_VIC_CPI(cpi);
		break;
	case VIC_SYS_INT:
	case VIC_CMN_INT: 
		/* These are slightly strange.  Even on the Quad card,
		 * They are vectored as VIC CPIs */
		if(is_cpu_quad())
			ack_special_QIC_CPI(cpi);
		else
			ack_VIC_CPI(cpi);
		break;
	default:
		printk("VOYAGER ERROR: CPI%d is in common CPI code\n", cpi);
		break;
	}
}

/* local variables */

/* The VIC IRQ descriptors -- these look almost identical to the
 * 8259 IRQs except that masks and things must be kept per processor
 */
static struct irq_chip vic_chip = {
	.name		= "VIC",
	.startup	= startup_vic_irq,
	.mask		= mask_vic_irq,
	.unmask		= unmask_vic_irq,
	.set_affinity	= set_vic_irq_affinity,
};

/* used to count up as CPUs are brought on line (starts at 0) */
static int cpucount = 0;

/* steal a page from the bottom of memory for the trampoline and
 * squirrel its address away here.  This will be in kernel virtual
 * space */
static __u32 trampoline_base;

/* The per cpu profile stuff - used in smp_local_timer_interrupt */
static DEFINE_PER_CPU(int, prof_multiplier) = 1;
static DEFINE_PER_CPU(int, prof_old_multiplier) = 1;
static DEFINE_PER_CPU(int, prof_counter) =  1;

/* the map used to check if a CPU has booted */
static __u32 cpu_booted_map;

/* the synchronize flag used to hold all secondary CPUs spinning in
 * a tight loop until the boot sequence is ready for them */
static cpumask_t smp_commenced_mask = CPU_MASK_NONE;

/* This is for the new dynamic CPU boot code */
cpumask_t cpu_callin_map = CPU_MASK_NONE;
cpumask_t cpu_callout_map = CPU_MASK_NONE;
EXPORT_SYMBOL(cpu_callout_map);
cpumask_t cpu_possible_map = CPU_MASK_NONE;
EXPORT_SYMBOL(cpu_possible_map);

/* The per processor IRQ masks (these are usually kept in sync) */
static __u16 vic_irq_mask[NR_CPUS] __cacheline_aligned;

/* the list of IRQs to be enabled by the VIC_ENABLE_IRQ_CPI */
static __u16 vic_irq_enable_mask[NR_CPUS] __cacheline_aligned = { 0 };

/* Lock for enable/disable of VIC interrupts */
static  __cacheline_aligned DEFINE_SPINLOCK(vic_irq_lock);

/* The boot processor is correctly set up in PC mode when it 
 * comes up, but the secondaries need their master/slave 8259
 * pairs initializing correctly */

/* Interrupt counters (per cpu) and total - used to try to
 * even up the interrupt handling routines */
static long vic_intr_total = 0;
static long vic_intr_count[NR_CPUS] __cacheline_aligned = { 0 };
static unsigned long vic_tick[NR_CPUS] __cacheline_aligned = { 0 };

/* Since we can only use CPI0, we fake all the other CPIs */
static unsigned long vic_cpi_mailbox[NR_CPUS] __cacheline_aligned;

/* debugging routine to read the isr of the cpu's pic */
static inline __u16
vic_read_isr(void)
{
	__u16 isr;

	outb(0x0b, 0xa0);
	isr = inb(0xa0) << 8;
	outb(0x0b, 0x20);
	isr |= inb(0x20);

	return isr;
}

static __init void
qic_setup(void)
{
	if(!is_cpu_quad()) {
		/* not a quad, no setup */
		return;
	}
	outb(QIC_DEFAULT_MASK0, QIC_MASK_REGISTER0);
	outb(QIC_CPI_ENABLE, QIC_MASK_REGISTER1);
	
	if(is_cpu_extended()) {
		/* the QIC duplicate of the VIC base register */
		outb(VIC_DEFAULT_CPI_BASE, QIC_VIC_CPI_BASE_REGISTER);
		outb(QIC_DEFAULT_CPI_BASE, QIC_CPI_BASE_REGISTER);

		/* FIXME: should set up the QIC timer and memory parity
		 * error vectors here */
	}
}

static __init void
vic_setup_pic(void)
{
	outb(1, VIC_REDIRECT_REGISTER_1);
	/* clear the claim registers for dynamic routing */
	outb(0, VIC_CLAIM_REGISTER_0);
	outb(0, VIC_CLAIM_REGISTER_1);

	outb(0, VIC_PRIORITY_REGISTER);
	/* Set the Primary and Secondary Microchannel vector
	 * bases to be the same as the ordinary interrupts
	 *
	 * FIXME: This would be more efficient using separate
	 * vectors. */
	outb(FIRST_EXTERNAL_VECTOR, VIC_PRIMARY_MC_BASE);
	outb(FIRST_EXTERNAL_VECTOR, VIC_SECONDARY_MC_BASE);
	/* Now initiallise the master PIC belonging to this CPU by
	 * sending the four ICWs */

	/* ICW1: level triggered, ICW4 needed */
	outb(0x19, 0x20);

	/* ICW2: vector base */
	outb(FIRST_EXTERNAL_VECTOR, 0x21);

	/* ICW3: slave at line 2 */
	outb(0x04, 0x21);

	/* ICW4: 8086 mode */
	outb(0x01, 0x21);

	/* now the same for the slave PIC */

	/* ICW1: level trigger, ICW4 needed */
	outb(0x19, 0xA0);

	/* ICW2: slave vector base */
	outb(FIRST_EXTERNAL_VECTOR + 8, 0xA1);
	
	/* ICW3: slave ID */
	outb(0x02, 0xA1);

	/* ICW4: 8086 mode */
	outb(0x01, 0xA1);
}

static void
do_quad_bootstrap(void)
{
	if(is_cpu_quad() && is_cpu_vic_boot()) {
		int i;
		unsigned long flags;
		__u8 cpuid = hard_smp_processor_id();

		local_irq_save(flags);

		for(i = 0; i<4; i++) {
			/* FIXME: this would be >>3 &0x7 on the 32 way */
			if(((cpuid >> 2) & 0x03) == i)
				/* don't lower our own mask! */
				continue;

			/* masquerade as local Quad CPU */
			outb(QIC_CPUID_ENABLE | i, QIC_PROCESSOR_ID);
			/* enable the startup CPI */
			outb(QIC_BOOT_CPI_MASK, QIC_MASK_REGISTER1);
			/* restore cpu id */
			outb(0, QIC_PROCESSOR_ID);
		}
		local_irq_restore(flags);
	}
}


/* Set up all the basic stuff: read the SMP config and make all the
 * SMP information reflect only the boot cpu.  All others will be
 * brought on-line later. */
void __init 
find_smp_config(void)
{
	int i;

	boot_cpu_id = hard_smp_processor_id();

	printk("VOYAGER SMP: Boot cpu is %d\n", boot_cpu_id);

	/* initialize the CPU structures (moved from smp_boot_cpus) */
	for(i=0; i<NR_CPUS; i++) {
		cpu_irq_affinity[i] = ~0;
	}
	cpu_online_map = cpumask_of_cpu(boot_cpu_id);

	/* The boot CPU must be extended */
	voyager_extended_vic_processors = 1<<boot_cpu_id;
	/* initially, all of the first 8 CPUs can boot */
	voyager_allowed_boot_processors = 0xff;
	/* set up everything for just this CPU, we can alter
	 * this as we start the other CPUs later */
	/* now get the CPU disposition from the extended CMOS */
	cpus_addr(phys_cpu_present_map)[0] = voyager_extended_cmos_read(VOYAGER_PROCESSOR_PRESENT_MASK);
	cpus_addr(phys_cpu_present_map)[0] |= voyager_extended_cmos_read(VOYAGER_PROCESSOR_PRESENT_MASK + 1) << 8;
	cpus_addr(phys_cpu_present_map)[0] |= voyager_extended_cmos_read(VOYAGER_PROCESSOR_PRESENT_MASK + 2) << 16;
	cpus_addr(phys_cpu_present_map)[0] |= voyager_extended_cmos_read(VOYAGER_PROCESSOR_PRESENT_MASK + 3) << 24;
	cpu_possible_map = phys_cpu_present_map;
	printk("VOYAGER SMP: phys_cpu_present_map = 0x%lx\n", cpus_addr(phys_cpu_present_map)[0]);
	/* Here we set up the VIC to enable SMP */
	/* enable the CPIs by writing the base vector to their register */
	outb(VIC_DEFAULT_CPI_BASE, VIC_CPI_BASE_REGISTER);
	outb(1, VIC_REDIRECT_REGISTER_1);
	/* set the claim registers for static routing --- Boot CPU gets
	 * all interrupts untill all other CPUs started */
	outb(0xff, VIC_CLAIM_REGISTER_0);
	outb(0xff, VIC_CLAIM_REGISTER_1);
	/* Set the Primary and Secondary Microchannel vector
	 * bases to be the same as the ordinary interrupts
	 *
	 * FIXME: This would be more efficient using separate
	 * vectors. */
	outb(FIRST_EXTERNAL_VECTOR, VIC_PRIMARY_MC_BASE);
	outb(FIRST_EXTERNAL_VECTOR, VIC_SECONDARY_MC_BASE);

	/* Finally tell the firmware that we're driving */
	outb(inb(VOYAGER_SUS_IN_CONTROL_PORT) | VOYAGER_IN_CONTROL_FLAG,
	     VOYAGER_SUS_IN_CONTROL_PORT);

	current_thread_info()->cpu = boot_cpu_id;
	x86_write_percpu(cpu_number, boot_cpu_id);
}

/*
 *	The bootstrap kernel entry code has set these up. Save them
 *	for a given CPU, id is physical */
void __init
smp_store_cpu_info(int id)
{
	struct cpuinfo_x86 *c = &cpu_data(id);

	*c = boot_cpu_data;

	identify_secondary_cpu(c);
}

/* set up the trampoline and return the physical address of the code */
static __u32 __init
setup_trampoline(void)
{
	/* these two are global symbols in trampoline.S */
	extern const __u8 trampoline_end[];
	extern const __u8 trampoline_data[];

	memcpy((__u8 *)trampoline_base, trampoline_data,
	       trampoline_end - trampoline_data);
	return virt_to_phys((__u8 *)trampoline_base);
}

/* Routine initially called when a non-boot CPU is brought online */
static void __init
start_secondary(void *unused)
{
	__u8 cpuid = hard_smp_processor_id();
	/* external functions not defined in the headers */
	extern void calibrate_delay(void);

	cpu_init();

	/* OK, we're in the routine */
	ack_CPI(VIC_CPU_BOOT_CPI);

	/* setup the 8259 master slave pair belonging to this CPU ---
         * we won't actually receive any until the boot CPU
         * relinquishes it's static routing mask */
	vic_setup_pic();

	qic_setup();

	if(is_cpu_quad() && !is_cpu_vic_boot()) {
		/* clear the boot CPI */
		__u8 dummy;

		dummy = voyager_quad_cpi_addr[cpuid]->qic_cpi[VIC_CPU_BOOT_CPI].cpi;
		printk("read dummy %d\n", dummy);
	}

	/* lower the mask to receive CPIs */
	vic_enable_cpi();

	VDEBUG(("VOYAGER SMP: CPU%d, stack at about %p\n", cpuid, &cpuid));

	/* enable interrupts */
	local_irq_enable();

	/* get our bogomips */
	calibrate_delay();

	/* save our processor parameters */
	smp_store_cpu_info(cpuid);

	/* if we're a quad, we may need to bootstrap other CPUs */
	do_quad_bootstrap();

	/* FIXME: this is rather a poor hack to prevent the CPU
	 * activating softirqs while it's supposed to be waiting for
	 * permission to proceed.  Without this, the new per CPU stuff
	 * in the softirqs will fail */
	local_irq_disable();
	cpu_set(cpuid, cpu_callin_map);

	/* signal that we're done */
	cpu_booted_map = 1;

	while (!cpu_isset(cpuid, smp_commenced_mask))
		rep_nop();
	local_irq_enable();

	local_flush_tlb();

	cpu_set(cpuid, cpu_online_map);
	wmb();
	cpu_idle();
}


/* Routine to kick start the given CPU and wait for it to report ready
 * (or timeout in startup).  When this routine returns, the requested
 * CPU is either fully running and configured or known to be dead.
 *
 * We call this routine sequentially 1 CPU at a time, so no need for
 * locking */

static void __init
do_boot_cpu(__u8 cpu)
{
	struct task_struct *idle;
	int timeout;
	unsigned long flags;
	int quad_boot = (1<<cpu) & voyager_quad_processors 
		& ~( voyager_extended_vic_processors
		     & voyager_allowed_boot_processors);

	/* This is an area in head.S which was used to set up the
	 * initial kernel stack.  We need to alter this to give the
	 * booting CPU a new stack (taken from its idle process) */
	extern struct {
		__u8 *esp;
		unsigned short ss;
	} stack_start;
	/* This is the format of the CPI IDT gate (in real mode) which
	 * we're hijacking to boot the CPU */
	union 	IDTFormat {
		struct seg {
			__u16	Offset;
			__u16	Segment;
		} idt;
		__u32 val;
	} hijack_source;

	__u32 *hijack_vector;
	__u32 start_phys_address = setup_trampoline();

	/* There's a clever trick to this: The linux trampoline is
	 * compiled to begin at absolute location zero, so make the
	 * address zero but have the data segment selector compensate
	 * for the actual address */
	hijack_source.idt.Offset = start_phys_address & 0x000F;
	hijack_source.idt.Segment = (start_phys_address >> 4) & 0xFFFF;

	cpucount++;
	alternatives_smp_switch(1);

	idle = fork_idle(cpu);
	if(IS_ERR(idle))
		panic("failed fork for CPU%d", cpu);
	idle->thread.eip = (unsigned long) start_secondary;
	/* init_tasks (in sched.c) is indexed logically */
	stack_start.esp = (void *) idle->thread.esp;

	init_gdt(cpu);
 	per_cpu(current_task, cpu) = idle;
	early_gdt_descr.address = (unsigned long)get_cpu_gdt_table(cpu);
	irq_ctx_init(cpu);

	/* Note: Don't modify initial ss override */
	VDEBUG(("VOYAGER SMP: Booting CPU%d at 0x%lx[%x:%x], stack %p\n", cpu, 
		(unsigned long)hijack_source.val, hijack_source.idt.Segment,
		hijack_source.idt.Offset, stack_start.esp));

	/* init lowmem identity mapping */
	clone_pgd_range(swapper_pg_dir, swapper_pg_dir + USER_PGD_PTRS,
			min_t(unsigned long, KERNEL_PGD_PTRS, USER_PGD_PTRS));
	flush_tlb_all();

	if(quad_boot) {
		printk("CPU %d: non extended Quad boot\n", cpu);
		hijack_vector = (__u32 *)phys_to_virt((VIC_CPU_BOOT_CPI + QIC_DEFAULT_CPI_BASE)*4);
		*hijack_vector = hijack_source.val;
	} else {
		printk("CPU%d: extended VIC boot\n", cpu);
		hijack_vector = (__u32 *)phys_to_virt((VIC_CPU_BOOT_CPI + VIC_DEFAULT_CPI_BASE)*4);
		*hijack_vector = hijack_source.val;
		/* VIC errata, may also receive interrupt at this address */
		hijack_vector = (__u32 *)phys_to_virt((VIC_CPU_BOOT_ERRATA_CPI + VIC_DEFAULT_CPI_BASE)*4);
		*hijack_vector = hijack_source.val;
	}
	/* All non-boot CPUs start with interrupts fully masked.  Need
	 * to lower the mask of the CPI we're about to send.  We do
	 * this in the VIC by masquerading as the processor we're
	 * about to boot and lowering its interrupt mask */
	local_irq_save(flags);
	if(quad_boot) {
		send_one_QIC_CPI(cpu, VIC_CPU_BOOT_CPI);
	} else {
		outb(VIC_CPU_MASQUERADE_ENABLE | cpu, VIC_PROCESSOR_ID);
		/* here we're altering registers belonging to `cpu' */
		
		outb(VIC_BOOT_INTERRUPT_MASK, 0x21);
		/* now go back to our original identity */
		outb(boot_cpu_id, VIC_PROCESSOR_ID);

		/* and boot the CPU */

		send_CPI((1<<cpu), VIC_CPU_BOOT_CPI);
	}
	cpu_booted_map = 0;
	local_irq_restore(flags);

	/* now wait for it to become ready (or timeout) */
	for(timeout = 0; timeout < 50000; timeout++) {
		if(cpu_booted_map)
			break;
		udelay(100);
	}
	/* reset the page table */
	zap_low_mappings();
	  
	if (cpu_booted_map) {
		VDEBUG(("CPU%d: Booted successfully, back in CPU %d\n",
			cpu, smp_processor_id()));
	
		printk("CPU%d: ", cpu);
		print_cpu_info(&cpu_data(cpu));
		wmb();
		cpu_set(cpu, cpu_callout_map);
		cpu_set(cpu, cpu_present_map);
	}
	else {
		printk("CPU%d FAILED TO BOOT: ", cpu);
		if (*((volatile unsigned char *)phys_to_virt(start_phys_address))==0xA5)
			printk("Stuck.\n");
		else
			printk("Not responding.\n");
		
		cpucount--;
	}
}

void __init
smp_boot_cpus(void)
{
	int i;

	/* CAT BUS initialisation must be done after the memory */
	/* FIXME: The L4 has a catbus too, it just needs to be
	 * accessed in a totally different way */
	if(voyager_level == 5) {
		voyager_cat_init();

		/* now that the cat has probed the Voyager System Bus, sanity
		 * check the cpu map */
		if( ((voyager_quad_processors | voyager_extended_vic_processors)
		     & cpus_addr(phys_cpu_present_map)[0]) != cpus_addr(phys_cpu_present_map)[0]) {
			/* should panic */
			printk("\n\n***WARNING*** Sanity check of CPU present map FAILED\n");
		}
	} else if(voyager_level == 4)
		voyager_extended_vic_processors = cpus_addr(phys_cpu_present_map)[0];

	/* this sets up the idle task to run on the current cpu */
	voyager_extended_cpus = 1;
	/* Remove the global_irq_holder setting, it triggers a BUG() on
	 * schedule at the moment */
	//global_irq_holder = boot_cpu_id;

	/* FIXME: Need to do something about this but currently only works
	 * on CPUs with a tsc which none of mine have. 
	smp_tune_scheduling();
	 */
	smp_store_cpu_info(boot_cpu_id);
	printk("CPU%d: ", boot_cpu_id);
	print_cpu_info(&cpu_data(boot_cpu_id));

	if(is_cpu_quad()) {
		/* booting on a Quad CPU */
		printk("VOYAGER SMP: Boot CPU is Quad\n");
		qic_setup();
		do_quad_bootstrap();
	}

	/* enable our own CPIs */
	vic_enable_cpi();

	cpu_set(boot_cpu_id, cpu_online_map);
	cpu_set(boot_cpu_id, cpu_callout_map);
	
	/* loop over all the extended VIC CPUs and boot them.  The 
	 * Quad CPUs must be bootstrapped by their extended VIC cpu */
	for(i = 0; i < NR_CPUS; i++) {
		if(i == boot_cpu_id || !cpu_isset(i, phys_cpu_present_map))
			continue;
		do_boot_cpu(i);
		/* This udelay seems to be needed for the Quad boots
		 * don't remove unless you know what you're doing */
		udelay(1000);
	}
	/* we could compute the total bogomips here, but why bother?,
	 * Code added from smpboot.c */
	{
		unsigned long bogosum = 0;
		for (i = 0; i < NR_CPUS; i++)
			if (cpu_isset(i, cpu_online_map))
				bogosum += cpu_data(i).loops_per_jiffy;
		printk(KERN_INFO "Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
			cpucount+1,
			bogosum/(500000/HZ),
			(bogosum/(5000/HZ))%100);
	}
	voyager_extended_cpus = hweight32(voyager_extended_vic_processors);
	printk("VOYAGER: Extended (interrupt handling CPUs): %d, non-extended: %d\n", voyager_extended_cpus, num_booting_cpus() - voyager_extended_cpus);
	/* that's it, switch to symmetric mode */
	outb(0, VIC_PRIORITY_REGISTER);
	outb(0, VIC_CLAIM_REGISTER_0);
	outb(0, VIC_CLAIM_REGISTER_1);
	
	VDEBUG(("VOYAGER SMP: Booted with %d CPUs\n", num_booting_cpus()));
}

/* Reload the secondary CPUs task structure (this function does not
 * return ) */
void __init 
initialize_secondary(void)
{
#if 0
	// AC kernels only
	set_current(hard_get_current());
#endif

	/*
	 * We don't actually need to load the full TSS,
	 * basically just the stack pointer and the eip.
	 */

	asm volatile(
		"movl %0,%%esp\n\t"
		"jmp *%1"
		:
		:"r" (current->thread.esp),"r" (current->thread.eip));
}

/* handle a Voyager SYS_INT -- If we don't, the base board will
 * panic the system.
 *
 * System interrupts occur because some problem was detected on the
 * various busses.  To find out what you have to probe all the
 * hardware via the CAT bus.  FIXME: At the moment we do nothing. */
fastcall void
smp_vic_sys_interrupt(struct pt_regs *regs)
{
	ack_CPI(VIC_SYS_INT);
	printk("Voyager SYSTEM INTERRUPT\n");	
}

/* Handle a voyager CMN_INT; These interrupts occur either because of
 * a system status change or because a single bit memory error
 * occurred.  FIXME: At the moment, ignore all this. */
fastcall void
smp_vic_cmn_interrupt(struct pt_regs *regs)
{
	static __u8 in_cmn_int = 0;
	static DEFINE_SPINLOCK(cmn_int_lock);

	/* common ints are broadcast, so make sure we only do this once */
	_raw_spin_lock(&cmn_int_lock);
	if(in_cmn_int)
		goto unlock_end;

	in_cmn_int++;
	_raw_spin_unlock(&cmn_int_lock);

	VDEBUG(("Voyager COMMON INTERRUPT\n"));

	if(voyager_level == 5)
		voyager_cat_do_common_interrupt();

	_raw_spin_lock(&cmn_int_lock);
	in_cmn_int = 0;
 unlock_end:
	_raw_spin_unlock(&cmn_int_lock);
	ack_CPI(VIC_CMN_INT);
}

/*
 * Reschedule call back. Nothing to do, all the work is done
 * automatically when we return from the interrupt.  */
static void
smp_reschedule_interrupt(void)
{
	/* do nothing */
}

static struct mm_struct * flush_mm;
static unsigned long flush_va;
static DEFINE_SPINLOCK(tlbstate_lock);
#define FLUSH_ALL	0xffffffff

/*
 * We cannot call mmdrop() because we are in interrupt context, 
 * instead update mm->cpu_vm_mask.
 *
 * We need to reload %cr3 since the page tables may be going
 * away from under us..
 */
static inline void
leave_mm (unsigned long cpu)
{
	if (per_cpu(cpu_tlbstate, cpu).state == TLBSTATE_OK)
		BUG();
	cpu_clear(cpu, per_cpu(cpu_tlbstate, cpu).active_mm->cpu_vm_mask);
	load_cr3(swapper_pg_dir);
}


/*
 * Invalidate call-back
 */
static void 
smp_invalidate_interrupt(void)
{
	__u8 cpu = smp_processor_id();

	if (!test_bit(cpu, &smp_invalidate_needed))
		return;
	/* This will flood messages.  Don't uncomment unless you see
	 * Problems with cross cpu invalidation
	VDEBUG(("VOYAGER SMP: CPU%d received INVALIDATE_CPI\n",
		smp_processor_id()));
	*/

	if (flush_mm == per_cpu(cpu_tlbstate, cpu).active_mm) {
		if (per_cpu(cpu_tlbstate, cpu).state == TLBSTATE_OK) {
			if (flush_va == FLUSH_ALL)
				local_flush_tlb();
			else
				__flush_tlb_one(flush_va);
		} else
			leave_mm(cpu);
	}
	smp_mb__before_clear_bit();
	clear_bit(cpu, &smp_invalidate_needed);
	smp_mb__after_clear_bit();
}

/* All the new flush operations for 2.4 */


/* This routine is called with a physical cpu mask */
static void
voyager_flush_tlb_others (unsigned long cpumask, struct mm_struct *mm,
			  unsigned long va)
{
	int stuck = 50000;

	if (!cpumask)
		BUG();
	if ((cpumask & cpus_addr(cpu_online_map)[0]) != cpumask)
		BUG();
	if (cpumask & (1 << smp_processor_id()))
		BUG();
	if (!mm)
		BUG();

	spin_lock(&tlbstate_lock);
	
	flush_mm = mm;
	flush_va = va;
	atomic_set_mask(cpumask, &smp_invalidate_needed);
	/*
	 * We have to send the CPI only to
	 * CPUs affected.
	 */
	send_CPI(cpumask, VIC_INVALIDATE_CPI);

	while (smp_invalidate_needed) {
		mb();
		if(--stuck == 0) {
			printk("***WARNING*** Stuck doing invalidate CPI (CPU%d)\n", smp_processor_id());
			break;
		}
	}

	/* Uncomment only to debug invalidation problems
	VDEBUG(("VOYAGER SMP: Completed invalidate CPI (CPU%d)\n", cpu));
	*/

	flush_mm = NULL;
	flush_va = 0;
	spin_unlock(&tlbstate_lock);
}

void
flush_tlb_current_task(void)
{
	struct mm_struct *mm = current->mm;
	unsigned long cpu_mask;

	preempt_disable();

	cpu_mask = cpus_addr(mm->cpu_vm_mask)[0] & ~(1 << smp_processor_id());
	local_flush_tlb();
	if (cpu_mask)
		voyager_flush_tlb_others(cpu_mask, mm, FLUSH_ALL);

	preempt_enable();
}


void
flush_tlb_mm (struct mm_struct * mm)
{
	unsigned long cpu_mask;

	preempt_disable();

	cpu_mask = cpus_addr(mm->cpu_vm_mask)[0] & ~(1 << smp_processor_id());

	if (current->active_mm == mm) {
		if (current->mm)
			local_flush_tlb();
		else
			leave_mm(smp_processor_id());
	}
	if (cpu_mask)
		voyager_flush_tlb_others(cpu_mask, mm, FLUSH_ALL);

	preempt_enable();
}

void flush_tlb_page(struct vm_area_struct * vma, unsigned long va)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long cpu_mask;

	preempt_disable();

	cpu_mask = cpus_addr(mm->cpu_vm_mask)[0] & ~(1 << smp_processor_id());
	if (current->active_mm == mm) {
		if(current->mm)
			__flush_tlb_one(va);
		 else
		 	leave_mm(smp_processor_id());
	}

	if (cpu_mask)
		voyager_flush_tlb_others(cpu_mask, mm, va);

	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_page);

/* enable the requested IRQs */
static void
smp_enable_irq_interrupt(void)
{
	__u8 irq;
	__u8 cpu = get_cpu();

	VDEBUG(("VOYAGER SMP: CPU%d enabling irq mask 0x%x\n", cpu,
	       vic_irq_enable_mask[cpu]));

	spin_lock(&vic_irq_lock);
	for(irq = 0; irq < 16; irq++) {
		if(vic_irq_enable_mask[cpu] & (1<<irq))
			enable_local_vic_irq(irq);
	}
	vic_irq_enable_mask[cpu] = 0;
	spin_unlock(&vic_irq_lock);

	put_cpu_no_resched();
}
	
/*
 *	CPU halt call-back
 */
static void
smp_stop_cpu_function(void *dummy)
{
	VDEBUG(("VOYAGER SMP: CPU%d is STOPPING\n", smp_processor_id()));
	cpu_clear(smp_processor_id(), cpu_online_map);
	local_irq_disable();
	for(;;)
		halt();
}

static DEFINE_SPINLOCK(call_lock);

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	volatile unsigned long started;
	volatile unsigned long finished;
	int wait;
};

static struct call_data_struct * call_data;

/* execute a thread on a new CPU.  The function to be called must be
 * previously set up.  This is used to schedule a function for
 * execution on all CPUs - set up the function then broadcast a
 * function_interrupt CPI to come here on each CPU */
static void
smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	/* must take copy of wait because call_data may be replaced
	 * unless the function is waiting for us to finish */
	int wait = call_data->wait;
	__u8 cpu = smp_processor_id();

	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	mb();
	if(!test_and_clear_bit(cpu, &call_data->started)) {
		/* If the bit wasn't set, this could be a replay */
		printk(KERN_WARNING "VOYAGER SMP: CPU %d received call funtion with no call pending\n", cpu);
		return;
	}
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	irq_enter();
	(*func)(info);
	__get_cpu_var(irq_stat).irq_call_count++;
	irq_exit();
	if (wait) {
		mb();
		clear_bit(cpu, &call_data->finished);
	}
}

static int
voyager_smp_call_function_mask (cpumask_t cpumask,
				void (*func) (void *info), void *info,
				int wait)
{
	struct call_data_struct data;
	u32 mask = cpus_addr(cpumask)[0];

	mask &= ~(1<<smp_processor_id());

	if (!mask)
		return 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	data.func = func;
	data.info = info;
	data.started = mask;
	data.wait = wait;
	if (wait)
		data.finished = mask;

	spin_lock(&call_lock);
	call_data = &data;
	wmb();
	/* Send a message to all other CPUs and wait for them to respond */
	send_CPI(mask, VIC_CALL_FUNCTION_CPI);

	/* Wait for response */
	while (data.started)
		barrier();

	if (wait)
		while (data.finished)
			barrier();

	spin_unlock(&call_lock);

	return 0;
}

/* Sorry about the name.  In an APIC based system, the APICs
 * themselves are programmed to send a timer interrupt.  This is used
 * by linux to reschedule the processor.  Voyager doesn't have this,
 * so we use the system clock to interrupt one processor, which in
 * turn, broadcasts a timer CPI to all the others --- we receive that
 * CPI here.  We don't use this actually for counting so losing
 * ticks doesn't matter 
 *
 * FIXME: For those CPUs which actually have a local APIC, we could
 * try to use it to trigger this interrupt instead of having to
 * broadcast the timer tick.  Unfortunately, all my pentium DYADs have
 * no local APIC, so I can't do this
 *
 * This function is currently a placeholder and is unused in the code */
fastcall void 
smp_apic_timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	wrapper_smp_local_timer_interrupt();
	set_irq_regs(old_regs);
}

/* All of the QUAD interrupt GATES */
fastcall void
smp_qic_timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	ack_QIC_CPI(QIC_TIMER_CPI);
	wrapper_smp_local_timer_interrupt();
	set_irq_regs(old_regs);
}

fastcall void
smp_qic_invalidate_interrupt(struct pt_regs *regs)
{
	ack_QIC_CPI(QIC_INVALIDATE_CPI);
	smp_invalidate_interrupt();
}

fastcall void
smp_qic_reschedule_interrupt(struct pt_regs *regs)
{
	ack_QIC_CPI(QIC_RESCHEDULE_CPI);
	smp_reschedule_interrupt();
}

fastcall void
smp_qic_enable_irq_interrupt(struct pt_regs *regs)
{
	ack_QIC_CPI(QIC_ENABLE_IRQ_CPI);
	smp_enable_irq_interrupt();
}

fastcall void
smp_qic_call_function_interrupt(struct pt_regs *regs)
{
	ack_QIC_CPI(QIC_CALL_FUNCTION_CPI);
	smp_call_function_interrupt();
}

fastcall void
smp_vic_cpi_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	__u8 cpu = smp_processor_id();

	if(is_cpu_quad())
		ack_QIC_CPI(VIC_CPI_LEVEL0);
	else
		ack_VIC_CPI(VIC_CPI_LEVEL0);

	if(test_and_clear_bit(VIC_TIMER_CPI, &vic_cpi_mailbox[cpu]))
		wrapper_smp_local_timer_interrupt();
	if(test_and_clear_bit(VIC_INVALIDATE_CPI, &vic_cpi_mailbox[cpu]))
		smp_invalidate_interrupt();
	if(test_and_clear_bit(VIC_RESCHEDULE_CPI, &vic_cpi_mailbox[cpu]))
		smp_reschedule_interrupt();
	if(test_and_clear_bit(VIC_ENABLE_IRQ_CPI, &vic_cpi_mailbox[cpu]))
		smp_enable_irq_interrupt();
	if(test_and_clear_bit(VIC_CALL_FUNCTION_CPI, &vic_cpi_mailbox[cpu]))
		smp_call_function_interrupt();
	set_irq_regs(old_regs);
}

static void
do_flush_tlb_all(void* info)
{
	unsigned long cpu = smp_processor_id();

	__flush_tlb_all();
	if (per_cpu(cpu_tlbstate, cpu).state == TLBSTATE_LAZY)
		leave_mm(cpu);
}


/* flush the TLB of every active CPU in the system */
void
flush_tlb_all(void)
{
	on_each_cpu(do_flush_tlb_all, 0, 1, 1);
}

/* used to set up the trampoline for other CPUs when the memory manager
 * is sorted out */
void __init
smp_alloc_memory(void)
{
	trampoline_base = (__u32)alloc_bootmem_low_pages(PAGE_SIZE);
	if(__pa(trampoline_base) >= 0x93000)
		BUG();
}

/* send a reschedule CPI to one CPU by physical CPU number*/
static void
voyager_smp_send_reschedule(int cpu)
{
	send_one_CPI(cpu, VIC_RESCHEDULE_CPI);
}


int
hard_smp_processor_id(void)
{
	__u8 i;
	__u8 cpumask = inb(VIC_PROC_WHO_AM_I);
	if((cpumask & QUAD_IDENTIFIER) == QUAD_IDENTIFIER)
		return cpumask & 0x1F;

	for(i = 0; i < 8; i++) {
		if(cpumask & (1<<i))
			return i;
	}
	printk("** WARNING ** Illegal cpuid returned by VIC: %d", cpumask);
	return 0;
}

int
safe_smp_processor_id(void)
{
	return hard_smp_processor_id();
}

/* broadcast a halt to all other CPUs */
static void
voyager_smp_send_stop(void)
{
	smp_call_function(smp_stop_cpu_function, NULL, 1, 1);
}

/* this function is triggered in time.c when a clock tick fires
 * we need to re-broadcast the tick to all CPUs */
void
smp_vic_timer_interrupt(void)
{
	send_CPI_allbutself(VIC_TIMER_CPI);
	smp_local_timer_interrupt();
}

/* local (per CPU) timer interrupt.  It does both profiling and
 * process statistics/rescheduling.
 *
 * We do profiling in every local tick, statistics/rescheduling
 * happen only every 'profiling multiplier' ticks. The default
 * multiplier is 1 and it can be changed by writing the new multiplier
 * value into /proc/profile.
 */
void
smp_local_timer_interrupt(void)
{
	int cpu = smp_processor_id();
	long weight;

	profile_tick(CPU_PROFILING);
	if (--per_cpu(prof_counter, cpu) <= 0) {
		/*
		 * The multiplier may have changed since the last time we got
		 * to this point as a result of the user writing to
		 * /proc/profile. In this case we need to adjust the APIC
		 * timer accordingly.
		 *
		 * Interrupts are already masked off at this point.
		 */
		per_cpu(prof_counter,cpu) = per_cpu(prof_multiplier, cpu);
		if (per_cpu(prof_counter, cpu) !=
					per_cpu(prof_old_multiplier, cpu)) {
			/* FIXME: need to update the vic timer tick here */
			per_cpu(prof_old_multiplier, cpu) =
						per_cpu(prof_counter, cpu);
		}

		update_process_times(user_mode_vm(get_irq_regs()));
	}

	if( ((1<<cpu) & voyager_extended_vic_processors) == 0)
		/* only extended VIC processors participate in
		 * interrupt distribution */
		return;

	/*
	 * We take the 'long' return path, and there every subsystem
	 * grabs the appropriate locks (kernel lock/ irq lock).
	 *
	 * we might want to decouple profiling from the 'long path',
	 * and do the profiling totally in assembly.
	 *
	 * Currently this isn't too much of an issue (performance wise),
	 * we can take more than 100K local irqs per second on a 100 MHz P5.
	 */

	if((++vic_tick[cpu] & 0x7) != 0)
		return;
	/* get here every 16 ticks (about every 1/6 of a second) */

	/* Change our priority to give someone else a chance at getting
         * the IRQ. The algorithm goes like this:
	 *
	 * In the VIC, the dynamically routed interrupt is always
	 * handled by the lowest priority eligible (i.e. receiving
	 * interrupts) CPU.  If >1 eligible CPUs are equal lowest, the
	 * lowest processor number gets it.
	 *
	 * The priority of a CPU is controlled by a special per-CPU
	 * VIC priority register which is 3 bits wide 0 being lowest
	 * and 7 highest priority..
	 *
	 * Therefore we subtract the average number of interrupts from
	 * the number we've fielded.  If this number is negative, we
	 * lower the activity count and if it is positive, we raise
	 * it.
	 *
	 * I'm afraid this still leads to odd looking interrupt counts:
	 * the totals are all roughly equal, but the individual ones
	 * look rather skewed.
	 *
	 * FIXME: This algorithm is total crap when mixed with SMP
	 * affinity code since we now try to even up the interrupt
	 * counts when an affinity binding is keeping them on a
	 * particular CPU*/
	weight = (vic_intr_count[cpu]*voyager_extended_cpus
		  - vic_intr_total) >> 4;
	weight += 4;
	if(weight > 7)
		weight = 7;
	if(weight < 0)
		weight = 0;
	
	outb((__u8)weight, VIC_PRIORITY_REGISTER);

#ifdef VOYAGER_DEBUG
	if((vic_tick[cpu] & 0xFFF) == 0) {
		/* print this message roughly every 25 secs */
		printk("VOYAGER SMP: vic_tick[%d] = %lu, weight = %ld\n",
		       cpu, vic_tick[cpu], weight);
	}
#endif
}

/* setup the profiling timer */
int 
setup_profiling_timer(unsigned int multiplier)
{
	int i;

	if ( (!multiplier))
		return -EINVAL;

	/* 
	 * Set the new multiplier for each CPU. CPUs don't start using the
	 * new values until the next timer interrupt in which they do process
	 * accounting.
	 */
	for (i = 0; i < NR_CPUS; ++i)
		per_cpu(prof_multiplier, i) = multiplier;

	return 0;
}

/* This is a bit of a mess, but forced on us by the genirq changes
 * there's no genirq handler that really does what voyager wants
 * so hack it up with the simple IRQ handler */
static void fastcall
handle_vic_irq(unsigned int irq, struct irq_desc *desc)
{
	before_handle_vic_irq(irq);
	handle_simple_irq(irq, desc);
	after_handle_vic_irq(irq);
}


/*  The CPIs are handled in the per cpu 8259s, so they must be
 *  enabled to be received: FIX: enabling the CPIs in the early
 *  boot sequence interferes with bug checking; enable them later
 *  on in smp_init */
#define VIC_SET_GATE(cpi, vector) \
	set_intr_gate((cpi) + VIC_DEFAULT_CPI_BASE, (vector))
#define QIC_SET_GATE(cpi, vector) \
	set_intr_gate((cpi) + QIC_DEFAULT_CPI_BASE, (vector))

void __init
smp_intr_init(void)
{
	int i;

	/* initialize the per cpu irq mask to all disabled */
	for(i = 0; i < NR_CPUS; i++)
		vic_irq_mask[i] = 0xFFFF;

	VIC_SET_GATE(VIC_CPI_LEVEL0, vic_cpi_interrupt);

	VIC_SET_GATE(VIC_SYS_INT, vic_sys_interrupt);
	VIC_SET_GATE(VIC_CMN_INT, vic_cmn_interrupt);

	QIC_SET_GATE(QIC_TIMER_CPI, qic_timer_interrupt);
	QIC_SET_GATE(QIC_INVALIDATE_CPI, qic_invalidate_interrupt);
	QIC_SET_GATE(QIC_RESCHEDULE_CPI, qic_reschedule_interrupt);
	QIC_SET_GATE(QIC_ENABLE_IRQ_CPI, qic_enable_irq_interrupt);
	QIC_SET_GATE(QIC_CALL_FUNCTION_CPI, qic_call_function_interrupt);
	

	/* now put the VIC descriptor into the first 48 IRQs 
	 *
	 * This is for later: first 16 correspond to PC IRQs; next 16
	 * are Primary MC IRQs and final 16 are Secondary MC IRQs */
	for(i = 0; i < 48; i++)
		set_irq_chip_and_handler(i, &vic_chip, handle_vic_irq);
}

/* send a CPI at level cpi to a set of cpus in cpuset (set 1 bit per
 * processor to receive CPI */
static void
send_CPI(__u32 cpuset, __u8 cpi)
{
	int cpu;
	__u32 quad_cpuset = (cpuset & voyager_quad_processors);

	if(cpi < VIC_START_FAKE_CPI) {
		/* fake CPI are only used for booting, so send to the 
		 * extended quads as well---Quads must be VIC booted */
		outb((__u8)(cpuset), VIC_CPI_Registers[cpi]);
		return;
	}
	if(quad_cpuset)
		send_QIC_CPI(quad_cpuset, cpi);
	cpuset &= ~quad_cpuset;
	cpuset &= 0xff;		/* only first 8 CPUs vaild for VIC CPI */
	if(cpuset == 0)
		return;
	for_each_online_cpu(cpu) {
		if(cpuset & (1<<cpu))
			set_bit(cpi, &vic_cpi_mailbox[cpu]);
	}
	if(cpuset)
		outb((__u8)cpuset, VIC_CPI_Registers[VIC_CPI_LEVEL0]);
}

/* Acknowledge receipt of CPI in the QIC, clear in QIC hardware and
 * set the cache line to shared by reading it.
 *
 * DON'T make this inline otherwise the cache line read will be
 * optimised away
 * */
static int
ack_QIC_CPI(__u8 cpi) {
	__u8 cpu = hard_smp_processor_id();

	cpi &= 7;

	outb(1<<cpi, QIC_INTERRUPT_CLEAR1);
	return voyager_quad_cpi_addr[cpu]->qic_cpi[cpi].cpi;
}

static void
ack_special_QIC_CPI(__u8 cpi)
{
	switch(cpi) {
	case VIC_CMN_INT:
		outb(QIC_CMN_INT, QIC_INTERRUPT_CLEAR0);
		break;
	case VIC_SYS_INT:
		outb(QIC_SYS_INT, QIC_INTERRUPT_CLEAR0);
		break;
	}
	/* also clear at the VIC, just in case (nop for non-extended proc) */
	ack_VIC_CPI(cpi);
}

/* Acknowledge receipt of CPI in the VIC (essentially an EOI) */
static void
ack_VIC_CPI(__u8 cpi)
{
#ifdef VOYAGER_DEBUG
	unsigned long flags;
	__u16 isr;
	__u8 cpu = smp_processor_id();

	local_irq_save(flags);
	isr = vic_read_isr();
	if((isr & (1<<(cpi &7))) == 0) {
		printk("VOYAGER SMP: CPU%d lost CPI%d\n", cpu, cpi);
	}
#endif
	/* send specific EOI; the two system interrupts have
	 * bit 4 set for a separate vector but behave as the
	 * corresponding 3 bit intr */
	outb_p(0x60|(cpi & 7),0x20);

#ifdef VOYAGER_DEBUG
	if((vic_read_isr() & (1<<(cpi &7))) != 0) {
		printk("VOYAGER SMP: CPU%d still asserting CPI%d\n", cpu, cpi);
	}
	local_irq_restore(flags);
#endif
}

/* cribbed with thanks from irq.c */
#define __byte(x,y) 	(((unsigned char *)&(y))[x])
#define cached_21(cpu)	(__byte(0,vic_irq_mask[cpu]))
#define cached_A1(cpu)	(__byte(1,vic_irq_mask[cpu]))

static unsigned int
startup_vic_irq(unsigned int irq)
{
	unmask_vic_irq(irq);

	return 0;
}

/* The enable and disable routines.  This is where we run into
 * conflicting architectural philosophy.  Fundamentally, the voyager
 * architecture does not expect to have to disable interrupts globally
 * (the IRQ controllers belong to each CPU).  The processor masquerade
 * which is used to start the system shouldn't be used in a running OS
 * since it will cause great confusion if two separate CPUs drive to
 * the same IRQ controller (I know, I've tried it).
 *
 * The solution is a variant on the NCR lazy SPL design:
 *
 * 1) To disable an interrupt, do nothing (other than set the
 *    IRQ_DISABLED flag).  This dares the interrupt actually to arrive.
 *
 * 2) If the interrupt dares to come in, raise the local mask against
 *    it (this will result in all the CPU masks being raised
 *    eventually).
 *
 * 3) To enable the interrupt, lower the mask on the local CPU and
 *    broadcast an Interrupt enable CPI which causes all other CPUs to
 *    adjust their masks accordingly.  */

static void
unmask_vic_irq(unsigned int irq)
{
	/* linux doesn't to processor-irq affinity, so enable on
	 * all CPUs we know about */
	int cpu = smp_processor_id(), real_cpu;
	__u16 mask = (1<<irq);
	__u32 processorList = 0;
	unsigned long flags;

	VDEBUG(("VOYAGER: unmask_vic_irq(%d) CPU%d affinity 0x%lx\n",
		irq, cpu, cpu_irq_affinity[cpu]));
	spin_lock_irqsave(&vic_irq_lock, flags);
	for_each_online_cpu(real_cpu) {
		if(!(voyager_extended_vic_processors & (1<<real_cpu)))
			continue;
		if(!(cpu_irq_affinity[real_cpu] & mask)) {
			/* irq has no affinity for this CPU, ignore */
			continue;
		}
		if(real_cpu == cpu) {
			enable_local_vic_irq(irq);
		}
		else if(vic_irq_mask[real_cpu] & mask) {
			vic_irq_enable_mask[real_cpu] |= mask;
			processorList |= (1<<real_cpu);
		}
	}
	spin_unlock_irqrestore(&vic_irq_lock, flags);
	if(processorList)
		send_CPI(processorList, VIC_ENABLE_IRQ_CPI);
}

static void
mask_vic_irq(unsigned int irq)
{
	/* lazy disable, do nothing */
}

static void
enable_local_vic_irq(unsigned int irq)
{
	__u8 cpu = smp_processor_id();
	__u16 mask = ~(1 << irq);
	__u16 old_mask = vic_irq_mask[cpu];

	vic_irq_mask[cpu] &= mask;
	if(vic_irq_mask[cpu] == old_mask)
		return;

	VDEBUG(("VOYAGER DEBUG: Enabling irq %d in hardware on CPU %d\n",
		irq, cpu));

	if (irq & 8) {
		outb_p(cached_A1(cpu),0xA1);
		(void)inb_p(0xA1);
	}
	else {
		outb_p(cached_21(cpu),0x21);
		(void)inb_p(0x21);
	}
}

static void
disable_local_vic_irq(unsigned int irq)
{
	__u8 cpu = smp_processor_id();
	__u16 mask = (1 << irq);
	__u16 old_mask = vic_irq_mask[cpu];

	if(irq == 7)
		return;

	vic_irq_mask[cpu] |= mask;
	if(old_mask == vic_irq_mask[cpu])
		return;

	VDEBUG(("VOYAGER DEBUG: Disabling irq %d in hardware on CPU %d\n",
		irq, cpu));

	if (irq & 8) {
		outb_p(cached_A1(cpu),0xA1);
		(void)inb_p(0xA1);
	}
	else {
		outb_p(cached_21(cpu),0x21);
		(void)inb_p(0x21);
	}
}

/* The VIC is level triggered, so the ack can only be issued after the
 * interrupt completes.  However, we do Voyager lazy interrupt
 * handling here: It is an extremely expensive operation to mask an
 * interrupt in the vic, so we merely set a flag (IRQ_DISABLED).  If
 * this interrupt actually comes in, then we mask and ack here to push
 * the interrupt off to another CPU */
static void
before_handle_vic_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	__u8 cpu = smp_processor_id();

	_raw_spin_lock(&vic_irq_lock);
	vic_intr_total++;
	vic_intr_count[cpu]++;

	if(!(cpu_irq_affinity[cpu] & (1<<irq))) {
		/* The irq is not in our affinity mask, push it off
		 * onto another CPU */
		VDEBUG(("VOYAGER DEBUG: affinity triggered disable of irq %d on cpu %d\n",
			irq, cpu));
		disable_local_vic_irq(irq);
		/* set IRQ_INPROGRESS to prevent the handler in irq.c from
		 * actually calling the interrupt routine */
		desc->status |= IRQ_REPLAY | IRQ_INPROGRESS;
	} else if(desc->status & IRQ_DISABLED) {
		/* Damn, the interrupt actually arrived, do the lazy
		 * disable thing. The interrupt routine in irq.c will
		 * not handle a IRQ_DISABLED interrupt, so nothing more
		 * need be done here */
		VDEBUG(("VOYAGER DEBUG: lazy disable of irq %d on CPU %d\n",
			irq, cpu));
		disable_local_vic_irq(irq);
		desc->status |= IRQ_REPLAY;
	} else {
		desc->status &= ~IRQ_REPLAY;
	}

	_raw_spin_unlock(&vic_irq_lock);
}

/* Finish the VIC interrupt: basically mask */
static void
after_handle_vic_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;

	_raw_spin_lock(&vic_irq_lock);
	{
		unsigned int status = desc->status & ~IRQ_INPROGRESS;
#ifdef VOYAGER_DEBUG
		__u16 isr;
#endif

		desc->status = status;
		if ((status & IRQ_DISABLED))
			disable_local_vic_irq(irq);
#ifdef VOYAGER_DEBUG
		/* DEBUG: before we ack, check what's in progress */
		isr = vic_read_isr();
		if((isr & (1<<irq) && !(status & IRQ_REPLAY)) == 0) {
			int i;
			__u8 cpu = smp_processor_id();
			__u8 real_cpu;
			int mask; /* Um... initialize me??? --RR */

			printk("VOYAGER SMP: CPU%d lost interrupt %d\n",
			       cpu, irq);
			for_each_possible_cpu(real_cpu, mask) {

				outb(VIC_CPU_MASQUERADE_ENABLE | real_cpu,
				     VIC_PROCESSOR_ID);
				isr = vic_read_isr();
				if(isr & (1<<irq)) {
					printk("VOYAGER SMP: CPU%d ack irq %d\n",
					       real_cpu, irq);
					ack_vic_irq(irq);
				}
				outb(cpu, VIC_PROCESSOR_ID);
			}
		}
#endif /* VOYAGER_DEBUG */
		/* as soon as we ack, the interrupt is eligible for
		 * receipt by another CPU so everything must be in
		 * order here  */
		ack_vic_irq(irq);
		if(status & IRQ_REPLAY) {
			/* replay is set if we disable the interrupt
			 * in the before_handle_vic_irq() routine, so
			 * clear the in progress bit here to allow the
			 * next CPU to handle this correctly */
			desc->status &= ~(IRQ_REPLAY | IRQ_INPROGRESS);
		}
#ifdef VOYAGER_DEBUG
		isr = vic_read_isr();
		if((isr & (1<<irq)) != 0)
			printk("VOYAGER SMP: after_handle_vic_irq() after ack irq=%d, isr=0x%x\n",
			       irq, isr);
#endif /* VOYAGER_DEBUG */
	}
	_raw_spin_unlock(&vic_irq_lock);

	/* All code after this point is out of the main path - the IRQ
	 * may be intercepted by another CPU if reasserted */
}


/* Linux processor - interrupt affinity manipulations.
 *
 * For each processor, we maintain a 32 bit irq affinity mask.
 * Initially it is set to all 1's so every processor accepts every
 * interrupt.  In this call, we change the processor's affinity mask:
 *
 * Change from enable to disable:
 *
 * If the interrupt ever comes in to the processor, we will disable it
 * and ack it to push it off to another CPU, so just accept the mask here.
 *
 * Change from disable to enable:
 *
 * change the mask and then do an interrupt enable CPI to re-enable on
 * the selected processors */

void
set_vic_irq_affinity(unsigned int irq, cpumask_t mask)
{
	/* Only extended processors handle interrupts */
	unsigned long real_mask;
	unsigned long irq_mask = 1 << irq;
	int cpu;

	real_mask = cpus_addr(mask)[0] & voyager_extended_vic_processors;
	
	if(cpus_addr(mask)[0] == 0)
		/* can't have no CPUs to accept the interrupt -- extremely
		 * bad things will happen */
		return;

	if(irq == 0)
		/* can't change the affinity of the timer IRQ.  This
		 * is due to the constraint in the voyager
		 * architecture that the CPI also comes in on and IRQ
		 * line and we have chosen IRQ0 for this.  If you
		 * raise the mask on this interrupt, the processor
		 * will no-longer be able to accept VIC CPIs */
		return;

	if(irq >= 32) 
		/* You can only have 32 interrupts in a voyager system
		 * (and 32 only if you have a secondary microchannel
		 * bus) */
		return;

	for_each_online_cpu(cpu) {
		unsigned long cpu_mask = 1 << cpu;
		
		if(cpu_mask & real_mask) {
			/* enable the interrupt for this cpu */
			cpu_irq_affinity[cpu] |= irq_mask;
		} else {
			/* disable the interrupt for this cpu */
			cpu_irq_affinity[cpu] &= ~irq_mask;
		}
	}
	/* this is magic, we now have the correct affinity maps, so
	 * enable the interrupt.  This will send an enable CPI to
	 * those CPUs who need to enable it in their local masks,
	 * causing them to correct for the new affinity . If the
	 * interrupt is currently globally disabled, it will simply be
	 * disabled again as it comes in (voyager lazy disable).  If
	 * the affinity map is tightened to disable the interrupt on a
	 * cpu, it will be pushed off when it comes in */
	unmask_vic_irq(irq);
}

static void
ack_vic_irq(unsigned int irq)
{
	if (irq & 8) {
		outb(0x62,0x20);	/* Specific EOI to cascade */
		outb(0x60|(irq & 7),0xA0);
	} else {
		outb(0x60 | (irq & 7),0x20);
	}
}

/* enable the CPIs.  In the VIC, the CPIs are delivered by the 8259
 * but are not vectored by it.  This means that the 8259 mask must be
 * lowered to receive them */
static __init void
vic_enable_cpi(void)
{
	__u8 cpu = smp_processor_id();
	
	/* just take a copy of the current mask (nop for boot cpu) */
	vic_irq_mask[cpu] = vic_irq_mask[boot_cpu_id];

	enable_local_vic_irq(VIC_CPI_LEVEL0);
	enable_local_vic_irq(VIC_CPI_LEVEL1);
	/* for sys int and cmn int */
	enable_local_vic_irq(7);

	if(is_cpu_quad()) {
		outb(QIC_DEFAULT_MASK0, QIC_MASK_REGISTER0);
		outb(QIC_CPI_ENABLE, QIC_MASK_REGISTER1);
		VDEBUG(("VOYAGER SMP: QIC ENABLE CPI: CPU%d: MASK 0x%x\n",
			cpu, QIC_CPI_ENABLE));
	}

	VDEBUG(("VOYAGER SMP: ENABLE CPI: CPU%d: MASK 0x%x\n",
		cpu, vic_irq_mask[cpu]));
}

void
voyager_smp_dump()
{
	int old_cpu = smp_processor_id(), cpu;

	/* dump the interrupt masks of each processor */
	for_each_online_cpu(cpu) {
		__u16 imr, isr, irr;
		unsigned long flags;

		local_irq_save(flags);
		outb(VIC_CPU_MASQUERADE_ENABLE | cpu, VIC_PROCESSOR_ID);
		imr = (inb(0xa1) << 8) | inb(0x21);
		outb(0x0a, 0xa0);
		irr = inb(0xa0) << 8;
		outb(0x0a, 0x20);
		irr |= inb(0x20);
		outb(0x0b, 0xa0);
		isr = inb(0xa0) << 8;
		outb(0x0b, 0x20);
		isr |= inb(0x20);
		outb(old_cpu, VIC_PROCESSOR_ID);
		local_irq_restore(flags);
		printk("\tCPU%d: mask=0x%x, IMR=0x%x, IRR=0x%x, ISR=0x%x\n",
		       cpu, vic_irq_mask[cpu], imr, irr, isr);
#if 0
		/* These lines are put in to try to unstick an un ack'd irq */
		if(isr != 0) {
			int irq;
			for(irq=0; irq<16; irq++) {
				if(isr & (1<<irq)) {
					printk("\tCPU%d: ack irq %d\n",
					       cpu, irq);
					local_irq_save(flags);
					outb(VIC_CPU_MASQUERADE_ENABLE | cpu,
					     VIC_PROCESSOR_ID);
					ack_vic_irq(irq);
					outb(old_cpu, VIC_PROCESSOR_ID);
					local_irq_restore(flags);
				}
			}
		}
#endif
	}
}

void
smp_voyager_power_off(void *dummy)
{
	if(smp_processor_id() == boot_cpu_id) 
		voyager_power_off();
	else
		smp_stop_cpu_function(NULL);
}

static void __init
voyager_smp_prepare_cpus(unsigned int max_cpus)
{
	/* FIXME: ignore max_cpus for now */
	smp_boot_cpus();
}

static void __devinit voyager_smp_prepare_boot_cpu(void)
{
	init_gdt(smp_processor_id());
	switch_to_new_gdt();

	cpu_set(smp_processor_id(), cpu_online_map);
	cpu_set(smp_processor_id(), cpu_callout_map);
	cpu_set(smp_processor_id(), cpu_possible_map);
	cpu_set(smp_processor_id(), cpu_present_map);
}

static int __devinit
voyager_cpu_up(unsigned int cpu)
{
	/* This only works at boot for x86.  See "rewrite" above. */
	if (cpu_isset(cpu, smp_commenced_mask))
		return -ENOSYS;

	/* In case one didn't come up */
	if (!cpu_isset(cpu, cpu_callin_map))
		return -EIO;
	/* Unleash the CPU! */
	cpu_set(cpu, smp_commenced_mask);
	while (!cpu_isset(cpu, cpu_online_map))
		mb();
	return 0;
}

static void __init
voyager_smp_cpus_done(unsigned int max_cpus)
{
	zap_low_mappings();
}

void __init
smp_setup_processor_id(void)
{
	current_thread_info()->cpu = hard_smp_processor_id();
	x86_write_percpu(cpu_number, hard_smp_processor_id());
}

struct smp_ops smp_ops = {
	.smp_prepare_boot_cpu = voyager_smp_prepare_boot_cpu,
	.smp_prepare_cpus = voyager_smp_prepare_cpus,
	.cpu_up = voyager_cpu_up,
	.smp_cpus_done = voyager_smp_cpus_done,

	.smp_send_stop = voyager_smp_send_stop,
	.smp_send_reschedule = voyager_smp_send_reschedule,
	.smp_call_function_mask = voyager_smp_call_function_mask,
};
