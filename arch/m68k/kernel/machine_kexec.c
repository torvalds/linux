// SPDX-License-Identifier: GPL-2.0
/*
 * machine_kexec.c - handle transition of Linux booting another kernel
 */
#include <linux/compiler.h>
#include <linux/kexec.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/reboot.h>

#include <asm/cacheflush.h>
#include <asm/page.h>
#include <asm/setup.h>

extern const unsigned char relocate_new_kernel[];
extern const size_t relocate_new_kernel_size;

int machine_kexec_prepare(struct kimage *kimage)
{
	return 0;
}

void machine_kexec_cleanup(struct kimage *kimage)
{
}

void machine_shutdown(void)
{
}

void machine_crash_shutdown(struct pt_regs *regs)
{
}

typedef void (*relocate_kernel_t)(unsigned long ptr,
				  unsigned long start,
				  unsigned long cpu_mmu_flags) __noreturn;

void machine_kexec(struct kimage *image)
{
	void *reboot_code_buffer;
	unsigned long cpu_mmu_flags;

	reboot_code_buffer = page_address(image->control_code_page);

	memcpy(reboot_code_buffer, relocate_new_kernel,
	       relocate_new_kernel_size);

	/*
	 * we do not want to be bothered.
	 */
	local_irq_disable();

	pr_info("Will call new kernel at 0x%08lx. Bye...\n", image->start);
	__flush_cache_all();
	cpu_mmu_flags = m68k_cputype | m68k_mmutype << 8;
	((relocate_kernel_t) reboot_code_buffer)(image->head & PAGE_MASK,
						 image->start,
						 cpu_mmu_flags);
}
