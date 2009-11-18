/*
 * PPC64 code to handle Linux booting another kernel.
 *
 * Copyright (C) 2004-2005, IBM Corp.
 *
 * Created by: Milton D Miller II
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */


#include <linux/kexec.h>
#include <linux/smp.h>
#include <linux/thread_info.h>
#include <linux/init_task.h>
#include <linux/errno.h>

#include <asm/page.h>
#include <asm/current.h>
#include <asm/machdep.h>
#include <asm/cacheflush.h>
#include <asm/paca.h>
#include <asm/mmu.h>
#include <asm/sections.h>	/* _end */
#include <asm/prom.h>
#include <asm/smp.h>

int default_machine_kexec_prepare(struct kimage *image)
{
	int i;
	unsigned long begin, end;	/* limits of segment */
	unsigned long low, high;	/* limits of blocked memory range */
	struct device_node *node;
	const unsigned long *basep;
	const unsigned int *sizep;

	if (!ppc_md.hpte_clear_all)
		return -ENOENT;

	/*
	 * Since we use the kernel fault handlers and paging code to
	 * handle the virtual mode, we must make sure no destination
	 * overlaps kernel static data or bss.
	 */
	for (i = 0; i < image->nr_segments; i++)
		if (image->segment[i].mem < __pa(_end))
			return -ETXTBSY;

	/*
	 * For non-LPAR, we absolutely can not overwrite the mmu hash
	 * table, since we are still using the bolted entries in it to
	 * do the copy.  Check that here.
	 *
	 * It is safe if the end is below the start of the blocked
	 * region (end <= low), or if the beginning is after the
	 * end of the blocked region (begin >= high).  Use the
	 * boolean identity !(a || b)  === (!a && !b).
	 */
	if (htab_address) {
		low = __pa(htab_address);
		high = low + htab_size_bytes;

		for (i = 0; i < image->nr_segments; i++) {
			begin = image->segment[i].mem;
			end = begin + image->segment[i].memsz;

			if ((begin < high) && (end > low))
				return -ETXTBSY;
		}
	}

	/* We also should not overwrite the tce tables */
	for (node = of_find_node_by_type(NULL, "pci"); node != NULL;
			node = of_find_node_by_type(node, "pci")) {
		basep = of_get_property(node, "linux,tce-base", NULL);
		sizep = of_get_property(node, "linux,tce-size", NULL);
		if (basep == NULL || sizep == NULL)
			continue;

		low = *basep;
		high = low + (*sizep);

		for (i = 0; i < image->nr_segments; i++) {
			begin = image->segment[i].mem;
			end = begin + image->segment[i].memsz;

			if ((begin < high) && (end > low))
				return -ETXTBSY;
		}
	}

	return 0;
}

#define IND_FLAGS (IND_DESTINATION | IND_INDIRECTION | IND_DONE | IND_SOURCE)

static void copy_segments(unsigned long ind)
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

void kexec_copy_flush(struct kimage *image)
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

/* FIXME: we should schedule this function to be called on all cpus based
 * on calling the interrupts, but we would like to call it off irq level
 * so that the interrupt controller is clean.
 */
static void kexec_smp_down(void *arg)
{
	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(0, 1);

	local_irq_disable();
	kexec_smp_wait();
	/* NOTREACHED */
}

static void kexec_prepare_cpus(void)
{
	int my_cpu, i, notified=-1;

	smp_call_function(kexec_smp_down, NULL, /* wait */0);
	my_cpu = get_cpu();

	/* check the others cpus are now down (via paca hw cpu id == -1) */
	for (i=0; i < NR_CPUS; i++) {
		if (i == my_cpu)
			continue;

		while (paca[i].hw_cpu_id != -1) {
			barrier();
			if (!cpu_possible(i)) {
				printk("kexec: cpu %d hw_cpu_id %d is not"
						" possible, ignoring\n",
						i, paca[i].hw_cpu_id);
				break;
			}
			if (!cpu_online(i)) {
				/* Fixme: this can be spinning in
				 * pSeries_secondary_wait with a paca
				 * waiting for it to go online.
				 */
				printk("kexec: cpu %d hw_cpu_id %d is not"
						" online, ignoring\n",
						i, paca[i].hw_cpu_id);
				break;
			}
			if (i != notified) {
				printk( "kexec: waiting for cpu %d (physical"
						" %d) to go down\n",
						i, paca[i].hw_cpu_id);
				notified = i;
			}
		}
	}

	/* after we tell the others to go down */
	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(0, 0);

	put_cpu();

	local_irq_disable();
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

/* Our assembly helper, in kexec_stub.S */
extern NORET_TYPE void kexec_sequence(void *newstack, unsigned long start,
					void *image, void *control,
					void (*clear_all)(void)) ATTRIB_NORET;

/* too late to fail here */
void default_machine_kexec(struct kimage *image)
{
	/* prepare control code if any */

	/*
        * If the kexec boot is the normal one, need to shutdown other cpus
        * into our wait loop and quiesce interrupts.
        * Otherwise, in the case of crashed mode (crashing_cpu >= 0),
        * stopping other CPUs and collecting their pt_regs is done before
        * using debugger IPI.
        */

	if (crashing_cpu == -1)
		kexec_prepare_cpus();

	/* switch to a staticly allocated stack.  Based on irq stack code.
	 * XXX: the task struct will likely be invalid once we do the copy!
	 */
	kexec_stack.thread_info.task = current_thread_info()->task;
	kexec_stack.thread_info.flags = 0;

	/* Some things are best done in assembly.  Finding globals with
	 * a toc is easier in C, so pass in what we can.
	 */
	kexec_sequence(&kexec_stack, image->start, image,
			page_address(image->control_code_page),
			ppc_md.hpte_clear_all);
	/* NOTREACHED */
}

/* Values we need to export to the second kernel via the device tree. */
static unsigned long htab_base;

static struct property htab_base_prop = {
	.name = "linux,htab-base",
	.length = sizeof(unsigned long),
	.value = &htab_base,
};

static struct property htab_size_prop = {
	.name = "linux,htab-size",
	.length = sizeof(unsigned long),
	.value = &htab_size_bytes,
};

static int __init export_htab_values(void)
{
	struct device_node *node;
	struct property *prop;

	/* On machines with no htab htab_address is NULL */
	if (!htab_address)
		return -ENODEV;

	node = of_find_node_by_path("/chosen");
	if (!node)
		return -ENODEV;

	/* remove any stale propertys so ours can be found */
	prop = of_find_property(node, htab_base_prop.name, NULL);
	if (prop)
		prom_remove_property(node, prop);
	prop = of_find_property(node, htab_size_prop.name, NULL);
	if (prop)
		prom_remove_property(node, prop);

	htab_base = __pa(htab_address);
	prom_add_property(node, &htab_base_prop);
	prom_add_property(node, &htab_size_prop);

	of_node_put(node);
	return 0;
}
late_initcall(export_htab_values);
