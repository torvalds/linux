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
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

typedef NORET_TYPE void (*relocate_new_kernel_t)(
				unsigned long indirection_page,
				unsigned long reboot_code_buffer,
				unsigned long start_address,
				unsigned long vbr_reg) ATTRIB_NORET;

extern const unsigned char relocate_new_kernel[];
extern const unsigned int relocate_new_kernel_size;
extern void *gdb_vbr_vector;

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
NORET_TYPE void machine_kexec(struct kimage *image)
{

	unsigned long page_list;
	unsigned long reboot_code_buffer;
	unsigned long vbr_reg;
	relocate_new_kernel_t rnk;

#if defined(CONFIG_SH_STANDARD_BIOS)
	vbr_reg = ((unsigned long )gdb_vbr_vector) - 0x100;
#else
	vbr_reg = 0x80000000;  // dummy
#endif
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

	/* now call it */
	rnk = (relocate_new_kernel_t) reboot_code_buffer;
	(*rnk)(page_list, reboot_code_buffer, image->start, vbr_reg);
}

/* crashkernel=size@addr specifies the location to reserve for
 * a crash kernel.  By reserving this memory we guarantee
 * that linux never sets it up as a DMA target.
 * Useful for holding code to do something appropriate
 * after a kernel panic.
 */
static int __init parse_crashkernel(char *arg)
{
	unsigned long size, base;
	size = memparse(arg, &arg);
	if (*arg == '@') {
		base = memparse(arg+1, &arg);
		/* FIXME: Do I want a sanity check
		 * to validate the memory range?
		 */
		crashk_res.start = base;
		crashk_res.end   = base + size - 1;
	}
	return 0;
}
early_param("crashkernel", parse_crashkernel);
