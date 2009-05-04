/*
 * machine_kexec.c - handle transition of Linux booting another kernel
 * Copyright (C) 2002-2003 Eric Biederman  <ebiederm@xmission.com>
 *
 * GameCube/ppc32 port Copyright (C) 2004 Albert Herranz
 * LANDISK/sh4 supported by kogiidena
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/numa.h>
#include <linux/ftrace.h>
#include <linux/suspend.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

typedef void (*relocate_new_kernel_t)(unsigned long indirection_page,
				      unsigned long reboot_code_buffer,
				      unsigned long start_address);

extern const unsigned char relocate_new_kernel[];
extern const unsigned int relocate_new_kernel_size;
extern void *gdb_vbr_vector;
extern void *vbr_base;

void machine_shutdown(void)
{
}

void machine_crash_shutdown(struct pt_regs *regs)
{
}

/*
 * Do what every setup is needed on image and the
 * reboot code buffer to allow us to avoid allocations
 * later.
 */
int machine_kexec_prepare(struct kimage *image)
{
	/* older versions of kexec-tools are passing
	 * the zImage entry point as a virtual address.
	 */
	if (image->start != PHYSADDR(image->start))
		return -EINVAL; /* upgrade your kexec-tools */

	return 0;
}

void machine_kexec_cleanup(struct kimage *image)
{
}

static void kexec_info(struct kimage *image)
{
        int i;
	printk("kexec information\n");
	for (i = 0; i < image->nr_segments; i++) {
	        printk("  segment[%d]: 0x%08x - 0x%08x (0x%08x)\n",
		       i,
		       (unsigned int)image->segment[i].mem,
		       (unsigned int)image->segment[i].mem +
				     image->segment[i].memsz,
		       (unsigned int)image->segment[i].memsz);
	}
	printk("  start     : 0x%08x\n\n", (unsigned int)image->start);
}

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 */
void machine_kexec(struct kimage *image)
{
	unsigned long page_list;
	unsigned long reboot_code_buffer;
	relocate_new_kernel_t rnk;
	unsigned long entry;
	unsigned long *ptr;
	int save_ftrace_enabled;

	/*
	 * Nicked from the mips version of machine_kexec():
	 * The generic kexec code builds a page list with physical
	 * addresses. Use phys_to_virt() to convert them to virtual.
	 */
	for (ptr = &image->head; (entry = *ptr) && !(entry & IND_DONE);
	     ptr = (entry & IND_INDIRECTION) ?
	       phys_to_virt(entry & PAGE_MASK) : ptr + 1) {
		if (*ptr & IND_SOURCE || *ptr & IND_INDIRECTION ||
		    *ptr & IND_DESTINATION)
			*ptr = (unsigned long) phys_to_virt(*ptr);
	}

#ifdef CONFIG_KEXEC_JUMP
	if (image->preserve_context)
		save_processor_state();
#endif

	save_ftrace_enabled = __ftrace_enabled_save();

	/* Interrupts aren't acceptable while we reboot */
	local_irq_disable();

	page_list = image->head;

	/* we need both effective and real address here */
	reboot_code_buffer =
			(unsigned long)page_address(image->control_code_page);

	/* copy our kernel relocation code to the control code page */
	memcpy((void *)reboot_code_buffer, relocate_new_kernel,
						relocate_new_kernel_size);

	kexec_info(image);
	flush_cache_all();

#if defined(CONFIG_SH_STANDARD_BIOS)
	asm volatile("ldc %0, vbr" :
		     : "r" (((unsigned long) gdb_vbr_vector) - 0x100)
		     : "memory");
#endif

	/* now call it */
	rnk = (relocate_new_kernel_t) reboot_code_buffer;
	(*rnk)(page_list, reboot_code_buffer,
	       (unsigned long)phys_to_virt(image->start));

#ifdef CONFIG_KEXEC_JUMP
	asm volatile("ldc %0, vbr" : : "r" (&vbr_base) : "memory");

	if (image->preserve_context)
		restore_processor_state();

	/* Convert page list back to physical addresses, what a mess. */
	for (ptr = &image->head; (entry = *ptr) && !(entry & IND_DONE);
	     ptr = (*ptr & IND_INDIRECTION) ?
	       phys_to_virt(*ptr & PAGE_MASK) : ptr + 1) {
		if (*ptr & IND_SOURCE || *ptr & IND_INDIRECTION ||
		    *ptr & IND_DESTINATION)
			*ptr = virt_to_phys(*ptr);
	}
#endif

	__ftrace_enabled_restore(save_ftrace_enabled);
}

void arch_crash_save_vmcoreinfo(void)
{
#ifdef CONFIG_NUMA
	VMCOREINFO_SYMBOL(node_data);
	VMCOREINFO_LENGTH(node_data, MAX_NUMNODES);
#endif
}
