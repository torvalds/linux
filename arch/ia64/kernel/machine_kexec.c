/*
 * arch/ia64/kernel/machine_kexec.c
 *
 * Handle transition of Linux booting another kernel
 * Copyright (C) 2005 Hewlett-Packard Development Comapny, L.P.
 * Copyright (C) 2005 Khalid Aziz <khalid.aziz@hp.com>
 * Copyright (C) 2006 Intel Corp, Zou Nan hai <nanhai.zou@intel.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/cpu.h>
#include <linux/irq.h>
#include <asm/mmu_context.h>
#include <asm/setup.h>
#include <asm/delay.h>
#include <asm/meminit.h>

typedef NORET_TYPE void (*relocate_new_kernel_t)(
					unsigned long indirection_page,
					unsigned long start_address,
					struct ia64_boot_param *boot_param,
					unsigned long pal_addr) ATTRIB_NORET;

struct kimage *ia64_kimage;

struct resource efi_memmap_res = {
        .name  = "EFI Memory Map",
        .start = 0,
        .end   = 0,
        .flags = IORESOURCE_BUSY | IORESOURCE_MEM
};

struct resource boot_param_res = {
        .name  = "Boot parameter",
        .start = 0,
        .end   = 0,
        .flags = IORESOURCE_BUSY | IORESOURCE_MEM
};


/*
 * Do what every setup is needed on image and the
 * reboot code buffer to allow us to avoid allocations
 * later.
 */
int machine_kexec_prepare(struct kimage *image)
{
	void *control_code_buffer;
	const unsigned long *func;

	func = (unsigned long *)&relocate_new_kernel;
	/* Pre-load control code buffer to minimize work in kexec path */
	control_code_buffer = page_address(image->control_code_page);
	memcpy((void *)control_code_buffer, (const void *)func[0],
			relocate_new_kernel_size);
	flush_icache_range((unsigned long)control_code_buffer,
			(unsigned long)control_code_buffer + relocate_new_kernel_size);
	ia64_kimage = image;

	return 0;
}

void machine_kexec_cleanup(struct kimage *image)
{
}

void machine_shutdown(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		if (cpu != smp_processor_id())
			cpu_down(cpu);
	}
	kexec_disable_iosapic();
}

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 */
extern void *efi_get_pal_addr(void);
static void ia64_machine_kexec(struct unw_frame_info *info, void *arg)
{
	struct kimage *image = arg;
	relocate_new_kernel_t rnk;
	void *pal_addr = efi_get_pal_addr();
	unsigned long code_addr = (unsigned long)page_address(image->control_code_page);
	unsigned long vector;
	int ii;

	if (image->type == KEXEC_TYPE_CRASH) {
		crash_save_this_cpu();
		current->thread.ksp = (__u64)info->sw - 16;
	}

	/* Interrupts aren't acceptable while we reboot */
	local_irq_disable();

	/* Mask CMC and Performance Monitor interrupts */
	ia64_setreg(_IA64_REG_CR_PMV, 1 << 16);
	ia64_setreg(_IA64_REG_CR_CMCV, 1 << 16);

	/* Mask ITV and Local Redirect Registers */
	ia64_set_itv(1 << 16);
	ia64_set_lrr0(1 << 16);
	ia64_set_lrr1(1 << 16);

	/* terminate possible nested in-service interrupts */
	for (ii = 0; ii < 16; ii++)
		ia64_eoi();

	/* unmask TPR and clear any pending interrupts */
	ia64_setreg(_IA64_REG_CR_TPR, 0);
	ia64_srlz_d();
	vector = ia64_get_ivr();
	while (vector != IA64_SPURIOUS_INT_VECTOR) {
		ia64_eoi();
		vector = ia64_get_ivr();
	}
	platform_kernel_launch_event();
	rnk = (relocate_new_kernel_t)&code_addr;
	(*rnk)(image->head, image->start, ia64_boot_param,
		     GRANULEROUNDDOWN((unsigned long) pal_addr));
	BUG();
}

void machine_kexec(struct kimage *image)
{
	unw_init_running(ia64_machine_kexec, image);
	for(;;);
}
