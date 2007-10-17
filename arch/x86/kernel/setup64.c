/* 
 * X86-64 specific CPU setup.
 * Copyright (C) 1995  Linus Torvalds
 * Copyright 2001, 2002, 2003 SuSE Labs / Andi Kleen.
 * See setup.c for older changelog.
 */ 
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/i387.h>
#include <asm/percpu.h>
#include <asm/proto.h>
#include <asm/sections.h>
#include <asm/setup.h>

struct boot_params __initdata boot_params;

cpumask_t cpu_initialized __cpuinitdata = CPU_MASK_NONE;

struct x8664_pda *_cpu_pda[NR_CPUS] __read_mostly;
EXPORT_SYMBOL(_cpu_pda);
struct x8664_pda boot_cpu_pda[NR_CPUS] __cacheline_aligned;

struct desc_ptr idt_descr = { 256 * 16 - 1, (unsigned long) idt_table };

char boot_cpu_stack[IRQSTACKSIZE] __attribute__((section(".bss.page_aligned")));

unsigned long __supported_pte_mask __read_mostly = ~0UL;
static int do_not_nx __cpuinitdata = 0;

/* noexec=on|off
Control non executable mappings for 64bit processes.

on	Enable(default)
off	Disable
*/ 
static int __init nonx_setup(char *str)
{
	if (!str)
		return -EINVAL;
	if (!strncmp(str, "on", 2)) {
                __supported_pte_mask |= _PAGE_NX; 
 		do_not_nx = 0; 
	} else if (!strncmp(str, "off", 3)) {
		do_not_nx = 1;
		__supported_pte_mask &= ~_PAGE_NX;
        }
	return 0;
} 
early_param("noexec", nonx_setup);

int force_personality32 = 0; 

/* noexec32=on|off
Control non executable heap for 32bit processes.
To control the stack too use noexec=off

on	PROT_READ does not imply PROT_EXEC for 32bit processes
off	PROT_READ implies PROT_EXEC (default)
*/
static int __init nonx32_setup(char *str)
{
	if (!strcmp(str, "on"))
		force_personality32 &= ~READ_IMPLIES_EXEC;
	else if (!strcmp(str, "off"))
		force_personality32 |= READ_IMPLIES_EXEC;
	return 1;
}
__setup("noexec32=", nonx32_setup);

/*
 * Great future plan:
 * Declare PDA itself and support (irqstack,tss,pgd) as per cpu data.
 * Always point %gs to its beginning
 */
void __init setup_per_cpu_areas(void)
{ 
	int i;
	unsigned long size;

#ifdef CONFIG_HOTPLUG_CPU
	prefill_possible_map();
#endif

	/* Copy section for each CPU (we discard the original) */
	size = PERCPU_ENOUGH_ROOM;

	printk(KERN_INFO "PERCPU: Allocating %lu bytes of per cpu data\n", size);
	for_each_cpu_mask (i, cpu_possible_map) {
		char *ptr;

		if (!NODE_DATA(cpu_to_node(i))) {
			printk("cpu with no node %d, num_online_nodes %d\n",
			       i, num_online_nodes());
			ptr = alloc_bootmem_pages(size);
		} else { 
			ptr = alloc_bootmem_pages_node(NODE_DATA(cpu_to_node(i)), size);
		}
		if (!ptr)
			panic("Cannot allocate cpu data for CPU %d\n", i);
		cpu_pda(i)->data_offset = ptr - __per_cpu_start;
		memcpy(ptr, __per_cpu_start, __per_cpu_end - __per_cpu_start);
	}
} 

void pda_init(int cpu)
{ 
	struct x8664_pda *pda = cpu_pda(cpu);

	/* Setup up data that may be needed in __get_free_pages early */
	asm volatile("movl %0,%%fs ; movl %0,%%gs" :: "r" (0)); 
	/* Memory clobbers used to order PDA accessed */
	mb();
	wrmsrl(MSR_GS_BASE, pda);
	mb();

	pda->cpunumber = cpu; 
	pda->irqcount = -1;
	pda->kernelstack = 
		(unsigned long)stack_thread_info() - PDA_STACKOFFSET + THREAD_SIZE; 
	pda->active_mm = &init_mm;
	pda->mmu_state = 0;

	if (cpu == 0) {
		/* others are initialized in smpboot.c */
		pda->pcurrent = &init_task;
		pda->irqstackptr = boot_cpu_stack; 
	} else {
		pda->irqstackptr = (char *)
			__get_free_pages(GFP_ATOMIC, IRQSTACK_ORDER);
		if (!pda->irqstackptr)
			panic("cannot allocate irqstack for cpu %d", cpu); 
	}


	pda->irqstackptr += IRQSTACKSIZE-64;
} 

char boot_exception_stacks[(N_EXCEPTION_STACKS - 1) * EXCEPTION_STKSZ + DEBUG_STKSZ]
__attribute__((section(".bss.page_aligned")));

extern asmlinkage void ignore_sysret(void);

/* May not be marked __init: used by software suspend */
void syscall_init(void)
{
	/* 
	 * LSTAR and STAR live in a bit strange symbiosis.
	 * They both write to the same internal register. STAR allows to set CS/DS
	 * but only a 32bit target. LSTAR sets the 64bit rip. 	 
	 */ 
	wrmsrl(MSR_STAR,  ((u64)__USER32_CS)<<48  | ((u64)__KERNEL_CS)<<32); 
	wrmsrl(MSR_LSTAR, system_call); 
	wrmsrl(MSR_CSTAR, ignore_sysret);

#ifdef CONFIG_IA32_EMULATION   		
	syscall32_cpu_init ();
#endif

	/* Flags to clear on syscall */
	wrmsrl(MSR_SYSCALL_MASK, EF_TF|EF_DF|EF_IE|0x3000); 
}

void __cpuinit check_efer(void)
{
	unsigned long efer;

	rdmsrl(MSR_EFER, efer); 
        if (!(efer & EFER_NX) || do_not_nx) { 
                __supported_pte_mask &= ~_PAGE_NX; 
        }       
}

unsigned long kernel_eflags;

/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 * A lot of state is already set up in PDA init.
 */
void __cpuinit cpu_init (void)
{
	int cpu = stack_smp_processor_id();
	struct tss_struct *t = &per_cpu(init_tss, cpu);
	struct orig_ist *orig_ist = &per_cpu(orig_ist, cpu);
	unsigned long v; 
	char *estacks = NULL; 
	struct task_struct *me;
	int i;

	/* CPU 0 is initialised in head64.c */
	if (cpu != 0) {
		pda_init(cpu);
	} else 
		estacks = boot_exception_stacks; 

	me = current;

	if (cpu_test_and_set(cpu, cpu_initialized))
		panic("CPU#%d already initialized!\n", cpu);

	printk("Initializing CPU#%d\n", cpu);

	clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	/*
	 * Initialize the per-CPU GDT with the boot GDT,
	 * and set up the GDT descriptor:
	 */
	if (cpu)
 		memcpy(cpu_gdt(cpu), cpu_gdt_table, GDT_SIZE);

	cpu_gdt_descr[cpu].size = GDT_SIZE;
	asm volatile("lgdt %0" :: "m" (cpu_gdt_descr[cpu]));
	asm volatile("lidt %0" :: "m" (idt_descr));

	memset(me->thread.tls_array, 0, GDT_ENTRY_TLS_ENTRIES * 8);
	syscall_init();

	wrmsrl(MSR_FS_BASE, 0);
	wrmsrl(MSR_KERNEL_GS_BASE, 0);
	barrier(); 

	check_efer();

	/*
	 * set up and load the per-CPU TSS
	 */
	for (v = 0; v < N_EXCEPTION_STACKS; v++) {
		static const unsigned int order[N_EXCEPTION_STACKS] = {
			[0 ... N_EXCEPTION_STACKS - 1] = EXCEPTION_STACK_ORDER,
			[DEBUG_STACK - 1] = DEBUG_STACK_ORDER
		};
		if (cpu) {
			estacks = (char *)__get_free_pages(GFP_ATOMIC, order[v]);
			if (!estacks)
				panic("Cannot allocate exception stack %ld %d\n",
				      v, cpu); 
		}
		estacks += PAGE_SIZE << order[v];
		orig_ist->ist[v] = t->ist[v] = (unsigned long)estacks;
	}

	t->io_bitmap_base = offsetof(struct tss_struct, io_bitmap);
	/*
	 * <= is required because the CPU will access up to
	 * 8 bits beyond the end of the IO permission bitmap.
	 */
	for (i = 0; i <= IO_BITMAP_LONGS; i++)
		t->io_bitmap[i] = ~0UL;

	atomic_inc(&init_mm.mm_count);
	me->active_mm = &init_mm;
	if (me->mm)
		BUG();
	enter_lazy_tlb(&init_mm, me);

	set_tss_desc(cpu, t);
	load_TR_desc();
	load_LDT(&init_mm.context);

	/*
	 * Clear all 6 debug registers:
	 */

	set_debugreg(0UL, 0);
	set_debugreg(0UL, 1);
	set_debugreg(0UL, 2);
	set_debugreg(0UL, 3);
	set_debugreg(0UL, 6);
	set_debugreg(0UL, 7);

	fpu_init(); 

	raw_local_save_flags(kernel_eflags);
}
