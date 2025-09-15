// SPDX-License-Identifier: GPL-2.0
/*
 * machine_kexec.c - handle transition of Linux booting another kernel
 * Copyright (C) 2002-2003 Eric Biederman  <ebiederm@xmission.com>
 *
 * GameCube/ppc32 port Copyright (C) 2004 Albert Herranz
 * LANDISK/sh4 supported by kogiidena
 */
#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/numa.h>
#include <linux/ftrace.h>
#include <linux/suspend.h>
#include <linux/memblock.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/cacheflush.h>
#include <asm/sh_bios.h>
#include <asm/reboot.h>

typedef void (*relocate_new_kernel_t)(unsigned long indirection_page,
				      unsigned long reboot_code_buffer,
				      unsigned long start_address);

extern const unsigned char relocate_new_kernel[];
extern const unsigned int relocate_new_kernel_size;
extern void *vbr_base;

void native_machine_crash_shutdown(struct pt_regs *regs)
{
	/* Nothing to do for UP, but definitely broken for SMP.. */
}

/*
 * Do what every setup is needed on image and the
 * reboot code buffer to allow us to avoid allocations
 * later.
 */
int machine_kexec_prepare(struct kimage *image)
{
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

	sh_bios_vbr_reload();

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

void __init reserve_crashkernel(void)
{
	unsigned long long crash_size, crash_base;
	int ret;

	if (!IS_ENABLED(CONFIG_CRASH_RESERVE))
		return;

	ret = parse_crashkernel(boot_command_line, memblock_phys_mem_size(),
			&crash_size, &crash_base, NULL, NULL, NULL);
	if (ret == 0 && crash_size > 0) {
		crashk_res.start = crash_base;
		crashk_res.end = crash_base + crash_size - 1;
	}

	if (crashk_res.end == crashk_res.start)
		goto disable;

	crash_size = PAGE_ALIGN(resource_size(&crashk_res));
	if (!crashk_res.start) {
		unsigned long max = memblock_end_of_DRAM() - memory_limit;
		crashk_res.start = memblock_phys_alloc_range(crash_size,
							     PAGE_SIZE, 0, max);
		if (!crashk_res.start) {
			pr_err("crashkernel allocation failed\n");
			goto disable;
		}
	} else {
		ret = memblock_reserve(crashk_res.start, crash_size);
		if (unlikely(ret < 0)) {
			pr_err("crashkernel reservation failed - "
			       "memory is in use\n");
			goto disable;
		}
	}

	crashk_res.end = crashk_res.start + crash_size - 1;

	/*
	 * Crash kernel trumps memory limit
	 */
	if ((memblock_end_of_DRAM() - memory_limit) <= crashk_res.end) {
		memory_limit = 0;
		pr_info("Disabled memory limit for crashkernel\n");
	}

	pr_info("Reserving %ldMB of memory at 0x%08lx "
		"for crashkernel (System RAM: %ldMB)\n",
		(unsigned long)(crash_size >> 20),
		(unsigned long)(crashk_res.start),
		(unsigned long)(memblock_phys_mem_size() >> 20));

	return;

disable:
	crashk_res.start = crashk_res.end = 0;
}
