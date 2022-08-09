// SPDX-License-Identifier: GPL-2.0-only
/*
 * PPC64 code to handle Linux booting another kernel.
 *
 * Copyright (C) 2004-2005, IBM Corp.
 *
 * Created by: Milton D Miller II
 */


#include <linux/kexec.h>
#include <linux/smp.h>
#include <linux/thread_info.h>
#include <linux/init_task.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/hardirq.h>

#include <asm/page.h>
#include <asm/current.h>
#include <asm/machdep.h>
#include <asm/cacheflush.h>
#include <asm/firmware.h>
#include <asm/paca.h>
#include <asm/mmu.h>
#include <asm/sections.h>	/* _end */
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/hw_breakpoint.h>
#include <asm/asm-prototypes.h>
#include <asm/svm.h>
#include <asm/ultravisor.h>

int default_machine_kexec_prepare(struct kimage *image)
{
	int i;
	unsigned long begin, end;	/* limits of segment */
	unsigned long low, high;	/* limits of blocked memory range */
	struct device_node *node;
	const unsigned long *basep;
	const unsigned int *sizep;

	/*
	 * Since we use the kernel fault handlers and paging code to
	 * handle the virtual mode, we must make sure no destination
	 * overlaps kernel static data or bss.
	 */
	for (i = 0; i < image->nr_segments; i++)
		if (image->segment[i].mem < __pa(_end))
			return -ETXTBSY;

	/* We also should not overwrite the tce tables */
	for_each_node_by_type(node, "pci") {
		basep = of_get_property(node, "linux,tce-base", NULL);
		sizep = of_get_property(node, "linux,tce-size", NULL);
		if (basep == NULL || sizep == NULL)
			continue;

		low = *basep;
		high = low + (*sizep);

		for (i = 0; i < image->nr_segments; i++) {
			begin = image->segment[i].mem;
			end = begin + image->segment[i].memsz;

			if ((begin < high) && (end > low)) {
				of_node_put(node);
				return -ETXTBSY;
			}
		}
	}

	return 0;
}

/* Called during kexec sequence with MMU off */
static notrace void copy_segments(unsigned long ind)
{
	unsigned long entry;
	unsigned long *ptr;
	void *dest;
	void *addr;

	/*
	 * We rely on kexec_load to create a lists that properly
	 * initializes these pointers before they are used.
	 * We will still crash if the list is wrong, but at least
	 * the compiler will be quiet.
	 */
	ptr = NULL;
	dest = NULL;

	for (entry = ind; !(entry & IND_DONE); entry = *ptr++) {
		addr = __va(entry & PAGE_MASK);

		switch (entry & IND_FLAGS) {
		case IND_DESTINATION:
			dest = addr;
			break;
		case IND_INDIRECTION:
			ptr = addr;
			break;
		case IND_SOURCE:
			copy_page(dest, addr);
			dest += PAGE_SIZE;
		}
	}
}

/* Called during kexec sequence with MMU off */
notrace void kexec_copy_flush(struct kimage *image)
{
	long i, nr_segments = image->nr_segments;
	struct  kexec_segment ranges[KEXEC_SEGMENT_MAX];

	/* save the ranges on the stack to efficiently flush the icache */
	memcpy(ranges, image->segment, sizeof(ranges));

	/*
	 * After this call we may not use anything allocated in dynamic
	 * memory, including *image.
	 *
	 * Only globals and the stack are allowed.
	 */
	copy_segments(image->head);

	/*
	 * we need to clear the icache for all dest pages sometime,
	 * including ones that were in place on the original copy
	 */
	for (i = 0; i < nr_segments; i++)
		flush_icache_range((unsigned long)__va(ranges[i].mem),
			(unsigned long)__va(ranges[i].mem + ranges[i].memsz));
}

#ifdef CONFIG_SMP

static int kexec_all_irq_disabled = 0;

static void kexec_smp_down(void *arg)
{
	local_irq_disable();
	hard_irq_disable();

	mb(); /* make sure our irqs are disabled before we say they are */
	get_paca()->kexec_state = KEXEC_STATE_IRQS_OFF;
	while(kexec_all_irq_disabled == 0)
		cpu_relax();
	mb(); /* make sure all irqs are disabled before this */
	hw_breakpoint_disable();
	/*
	 * Now every CPU has IRQs off, we can clear out any pending
	 * IPIs and be sure that no more will come in after this.
	 */
	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(0, 1);

	reset_sprs();

	kexec_smp_wait();
	/* NOTREACHED */
}

static void kexec_prepare_cpus_wait(int wait_state)
{
	int my_cpu, i, notified=-1;

	hw_breakpoint_disable();
	my_cpu = get_cpu();
	/* Make sure each CPU has at least made it to the state we need.
	 *
	 * FIXME: There is a (slim) chance of a problem if not all of the CPUs
	 * are correctly onlined.  If somehow we start a CPU on boot with RTAS
	 * start-cpu, but somehow that CPU doesn't write callin_cpu_map[] in
	 * time, the boot CPU will timeout.  If it does eventually execute
	 * stuff, the secondary will start up (paca_ptrs[]->cpu_start was
	 * written) and get into a peculiar state.
	 * If the platform supports smp_ops->take_timebase(), the secondary CPU
	 * will probably be spinning in there.  If not (i.e. pseries), the
	 * secondary will continue on and try to online itself/idle/etc. If it
	 * survives that, we need to find these
	 * possible-but-not-online-but-should-be CPUs and chaperone them into
	 * kexec_smp_wait().
	 */
	for_each_online_cpu(i) {
		if (i == my_cpu)
			continue;

		while (paca_ptrs[i]->kexec_state < wait_state) {
			barrier();
			if (i != notified) {
				printk(KERN_INFO "kexec: waiting for cpu %d "
				       "(physical %d) to enter %i state\n",
				       i, paca_ptrs[i]->hw_cpu_id, wait_state);
				notified = i;
			}
		}
	}
	mb();
}

/*
 * We need to make sure each present CPU is online.  The next kernel will scan
 * the device tree and assume primary threads are online and query secondary
 * threads via RTAS to online them if required.  If we don't online primary
 * threads, they will be stuck.  However, we also online secondary threads as we
 * may be using 'cede offline'.  In this case RTAS doesn't see the secondary
 * threads as offline -- and again, these CPUs will be stuck.
 *
 * So, we online all CPUs that should be running, including secondary threads.
 */
static void wake_offline_cpus(void)
{
	int cpu = 0;

	for_each_present_cpu(cpu) {
		if (!cpu_online(cpu)) {
			printk(KERN_INFO "kexec: Waking offline cpu %d.\n",
			       cpu);
			WARN_ON(add_cpu(cpu));
		}
	}
}

static void kexec_prepare_cpus(void)
{
	wake_offline_cpus();
	smp_call_function(kexec_smp_down, NULL, /* wait */0);
	local_irq_disable();
	hard_irq_disable();

	mb(); /* make sure IRQs are disabled before we say they are */
	get_paca()->kexec_state = KEXEC_STATE_IRQS_OFF;

	kexec_prepare_cpus_wait(KEXEC_STATE_IRQS_OFF);
	/* we are sure every CPU has IRQs off at this point */
	kexec_all_irq_disabled = 1;

	/*
	 * Before removing MMU mappings make sure all CPUs have entered real
	 * mode:
	 */
	kexec_prepare_cpus_wait(KEXEC_STATE_REAL_MODE);

	/* after we tell the others to go down */
	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(0, 0);

	put_cpu();
}

#else /* ! SMP */

static void kexec_prepare_cpus(void)
{
	/*
	 * move the secondarys to us so that we can copy
	 * the new kernel 0-0x100 safely
	 *
	 * do this if kexec in setup.c ?
	 *
	 * We need to release the cpus if we are ever going from an
	 * UP to an SMP kernel.
	 */
	smp_release_cpus();
	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(0, 0);
	local_irq_disable();
	hard_irq_disable();
}

#endif /* SMP */

/*
 * kexec thread structure and stack.
 *
 * We need to make sure that this is 16384-byte aligned due to the
 * way process stacks are handled.  It also must be statically allocated
 * or allocated as part of the kimage, because everything else may be
 * overwritten when we copy the kexec image.  We piggyback on the
 * "init_task" linker section here to statically allocate a stack.
 *
 * We could use a smaller stack if we don't care about anything using
 * current, but that audit has not been performed.
 */
static union thread_union kexec_stack __init_task_data =
	{ };

/*
 * For similar reasons to the stack above, the kexecing CPU needs to be on a
 * static PACA; we switch to kexec_paca.
 */
struct paca_struct kexec_paca;

/* Our assembly helper, in misc_64.S */
extern void kexec_sequence(void *newstack, unsigned long start,
			   void *image, void *control,
			   void (*clear_all)(void),
			   bool copy_with_mmu_off) __noreturn;

/* too late to fail here */
void default_machine_kexec(struct kimage *image)
{
	bool copy_with_mmu_off;

	/* prepare control code if any */

	/*
        * If the kexec boot is the normal one, need to shutdown other cpus
        * into our wait loop and quiesce interrupts.
        * Otherwise, in the case of crashed mode (crashing_cpu >= 0),
        * stopping other CPUs and collecting their pt_regs is done before
        * using debugger IPI.
        */

	if (!kdump_in_progress())
		kexec_prepare_cpus();

	printk("kexec: Starting switchover sequence.\n");

	/* switch to a staticly allocated stack.  Based on irq stack code.
	 * We setup preempt_count to avoid using VMX in memcpy.
	 * XXX: the task struct will likely be invalid once we do the copy!
	 */
	current_thread_info()->flags = 0;
	current_thread_info()->preempt_count = HARDIRQ_OFFSET;

	/* We need a static PACA, too; copy this CPU's PACA over and switch to
	 * it. Also poison per_cpu_offset and NULL lppaca to catch anyone using
	 * non-static data.
	 */
	memcpy(&kexec_paca, get_paca(), sizeof(struct paca_struct));
	kexec_paca.data_offset = 0xedeaddeadeeeeeeeUL;
#ifdef CONFIG_PPC_PSERIES
	kexec_paca.lppaca_ptr = NULL;
#endif

	if (is_secure_guest() && !(image->preserve_context ||
				   image->type == KEXEC_TYPE_CRASH)) {
		uv_unshare_all_pages();
		printk("kexec: Unshared all shared pages.\n");
	}

	paca_ptrs[kexec_paca.paca_index] = &kexec_paca;

	setup_paca(&kexec_paca);

	/*
	 * The lppaca should be unregistered at this point so the HV won't
	 * touch it. In the case of a crash, none of the lppacas are
	 * unregistered so there is not much we can do about it here.
	 */

	/*
	 * On Book3S, the copy must happen with the MMU off if we are either
	 * using Radix page tables or we are not in an LPAR since we can
	 * overwrite the page tables while copying.
	 *
	 * In an LPAR, we keep the MMU on otherwise we can't access beyond
	 * the RMA. On BookE there is no real MMU off mode, so we have to
	 * keep it enabled as well (but then we have bolted TLB entries).
	 */
#ifdef CONFIG_PPC_BOOK3E
	copy_with_mmu_off = false;
#else
	copy_with_mmu_off = radix_enabled() ||
		!(firmware_has_feature(FW_FEATURE_LPAR) ||
		  firmware_has_feature(FW_FEATURE_PS3_LV1));
#endif

	/* Some things are best done in assembly.  Finding globals with
	 * a toc is easier in C, so pass in what we can.
	 */
	kexec_sequence(&kexec_stack, image->start, image,
		       page_address(image->control_code_page),
		       mmu_cleanup_all, copy_with_mmu_off);
	/* NOTREACHED */
}

#ifdef CONFIG_PPC_BOOK3S_64
/* Values we need to export to the second kernel via the device tree. */
static unsigned long htab_base;
static unsigned long htab_size;

static struct property htab_base_prop = {
	.name = "linux,htab-base",
	.length = sizeof(unsigned long),
	.value = &htab_base,
};

static struct property htab_size_prop = {
	.name = "linux,htab-size",
	.length = sizeof(unsigned long),
	.value = &htab_size,
};

static int __init export_htab_values(void)
{
	struct device_node *node;

	/* On machines with no htab htab_address is NULL */
	if (!htab_address)
		return -ENODEV;

	node = of_find_node_by_path("/chosen");
	if (!node)
		return -ENODEV;

	/* remove any stale propertys so ours can be found */
	of_remove_property(node, of_find_property(node, htab_base_prop.name, NULL));
	of_remove_property(node, of_find_property(node, htab_size_prop.name, NULL));

	htab_base = cpu_to_be64(__pa(htab_address));
	of_add_property(node, &htab_base_prop);
	htab_size = cpu_to_be64(htab_size_bytes);
	of_add_property(node, &htab_size_prop);

	of_node_put(node);
	return 0;
}
late_initcall(export_htab_values);
#endif /* CONFIG_PPC_BOOK3S_64 */
