// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/ia64/kernel/machine_kexec.c
 *
 * Handle transition of Linux booting another kernel
 * Copyright (C) 2005 Hewlett-Packard Development Comapny, L.P.
 * Copyright (C) 2005 Khalid Aziz <khalid.aziz@hp.com>
 * Copyright (C) 2006 Intel Corp, Zou Nan hai <nanhai.zou@intel.com>
 */

#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/cpu.h>
#include <linux/irq.h>
#include <linux/efi.h>
#include <linux/numa.h>
#include <linux/mmzone.h>

#include <asm/efi.h>
#include <asm/numa.h>
#include <asm/mmu_context.h>
#include <asm/setup.h>
#include <asm/delay.h>
#include <asm/meminit.h>
#include <asm/processor.h>
#include <asm/sal.h>
#include <asm/mca.h>

typedef void (*relocate_new_kernel_t)(
					unsigned long indirection_page,
					unsigned long start_address,
					struct ia64_boot_param *boot_param,
					unsigned long pal_addr) __noreturn;

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

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 */
static void ia64_machine_kexec(struct unw_frame_info *info, void *arg)
{
	struct kimage *image = arg;
	relocate_new_kernel_t rnk;
	void *pal_addr = efi_get_pal_addr();
	unsigned long code_addr;
	int ii;
	u64 fp, gp;
	ia64_fptr_t *init_handler = (ia64_fptr_t *)ia64_os_init_on_kdump;

	BUG_ON(!image);
	code_addr = (unsigned long)page_address(image->control_code_page);
	if (image->type == KEXEC_TYPE_CRASH) {
		crash_save_this_cpu();
		current->thread.ksp = (__u64)info->sw - 16;

		/* Register noop init handler */
		fp = ia64_tpa(init_handler->fp);
		gp = ia64_tpa(ia64_getreg(_IA64_REG_GP));
		ia64_sal_set_vectors(SAL_VECTOR_OS_INIT, fp, gp, 0, fp, gp, 0);
	} else {
		/* Unregister init handlers of current kernel */
		ia64_sal_set_vectors(SAL_VECTOR_OS_INIT, 0, 0, 0, 0, 0, 0);
	}

	/* Unregister mca handler - No more recovery on current kernel */
	ia64_sal_set_vectors(SAL_VECTOR_OS_MCA, 0, 0, 0, 0, 0, 0);

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
	while (ia64_get_ivr() != IA64_SPURIOUS_INT_VECTOR)
		ia64_eoi();
	rnk = (relocate_new_kernel_t)&code_addr;
	(*rnk)(image->head, image->start, ia64_boot_param,
		     GRANULEROUNDDOWN((unsigned long) pal_addr));
	BUG();
}

void machine_kexec(struct kimage *image)
{
	BUG_ON(!image);
	unw_init_running(ia64_machine_kexec, image);
	for(;;);
}

void arch_crash_save_vmcoreinfo(void)
{
#if defined(CONFIG_DISCONTIGMEM) || defined(CONFIG_SPARSEMEM)
	VMCOREINFO_SYMBOL(pgdat_list);
	VMCOREINFO_LENGTH(pgdat_list, MAX_NUMNODES);
#endif
#ifdef CONFIG_NUMA
	VMCOREINFO_SYMBOL(node_memblk);
	VMCOREINFO_LENGTH(node_memblk, NR_NODE_MEMBLKS);
	VMCOREINFO_STRUCT_SIZE(node_memblk_s);
	VMCOREINFO_OFFSET(node_memblk_s, start_paddr);
	VMCOREINFO_OFFSET(node_memblk_s, size);
#endif
#if CONFIG_PGTABLE_LEVELS == 3
	VMCOREINFO_CONFIG(PGTABLE_3);
#elif CONFIG_PGTABLE_LEVELS == 4
	VMCOREINFO_CONFIG(PGTABLE_4);
#endif
}

