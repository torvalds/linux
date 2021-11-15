// SPDX-License-Identifier: GPL-2.0-only
/*
 * kexec for arm64
 *
 * Copyright (C) Linaro.
 * Copyright (C) Huawei Futurewei Technologies.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/page-flags.h>
#include <linux/set_memory.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/cpu_ops.h>
#include <asm/daifflags.h>
#include <asm/memory.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/sections.h>
#include <asm/trans_pgd.h>

/**
 * kexec_image_info - For debugging output.
 */
#define kexec_image_info(_i) _kexec_image_info(__func__, __LINE__, _i)
static void _kexec_image_info(const char *func, int line,
	const struct kimage *kimage)
{
	unsigned long i;

	pr_debug("%s:%d:\n", func, line);
	pr_debug("  kexec kimage info:\n");
	pr_debug("    type:        %d\n", kimage->type);
	pr_debug("    start:       %lx\n", kimage->start);
	pr_debug("    head:        %lx\n", kimage->head);
	pr_debug("    nr_segments: %lu\n", kimage->nr_segments);
	pr_debug("    dtb_mem: %pa\n", &kimage->arch.dtb_mem);
	pr_debug("    kern_reloc: %pa\n", &kimage->arch.kern_reloc);
	pr_debug("    el2_vectors: %pa\n", &kimage->arch.el2_vectors);

	for (i = 0; i < kimage->nr_segments; i++) {
		pr_debug("      segment[%lu]: %016lx - %016lx, 0x%lx bytes, %lu pages\n",
			i,
			kimage->segment[i].mem,
			kimage->segment[i].mem + kimage->segment[i].memsz,
			kimage->segment[i].memsz,
			kimage->segment[i].memsz /  PAGE_SIZE);
	}
}

void machine_kexec_cleanup(struct kimage *kimage)
{
	/* Empty routine needed to avoid build errors. */
}

/**
 * machine_kexec_prepare - Prepare for a kexec reboot.
 *
 * Called from the core kexec code when a kernel image is loaded.
 * Forbid loading a kexec kernel if we have no way of hotplugging cpus or cpus
 * are stuck in the kernel. This avoids a panic once we hit machine_kexec().
 */
int machine_kexec_prepare(struct kimage *kimage)
{
	if (kimage->type != KEXEC_TYPE_CRASH && cpus_are_stuck_in_kernel()) {
		pr_err("Can't kexec: CPUs are stuck in the kernel.\n");
		return -EBUSY;
	}

	return 0;
}

/**
 * kexec_segment_flush - Helper to flush the kimage segments to PoC.
 */
static void kexec_segment_flush(const struct kimage *kimage)
{
	unsigned long i;

	pr_debug("%s:\n", __func__);

	for (i = 0; i < kimage->nr_segments; i++) {
		pr_debug("  segment[%lu]: %016lx - %016lx, 0x%lx bytes, %lu pages\n",
			i,
			kimage->segment[i].mem,
			kimage->segment[i].mem + kimage->segment[i].memsz,
			kimage->segment[i].memsz,
			kimage->segment[i].memsz /  PAGE_SIZE);

		dcache_clean_inval_poc(
			(unsigned long)phys_to_virt(kimage->segment[i].mem),
			(unsigned long)phys_to_virt(kimage->segment[i].mem) +
				kimage->segment[i].memsz);
	}
}

/* Allocates pages for kexec page table */
static void *kexec_page_alloc(void *arg)
{
	struct kimage *kimage = (struct kimage *)arg;
	struct page *page = kimage_alloc_control_pages(kimage, 0);

	if (!page)
		return NULL;

	memset(page_address(page), 0, PAGE_SIZE);

	return page_address(page);
}

int machine_kexec_post_load(struct kimage *kimage)
{
	int rc;
	pgd_t *trans_pgd;
	void *reloc_code = page_to_virt(kimage->control_code_page);
	long reloc_size;
	struct trans_pgd_info info = {
		.trans_alloc_page	= kexec_page_alloc,
		.trans_alloc_arg	= kimage,
	};

	/* If in place, relocation is not used, only flush next kernel */
	if (kimage->head & IND_DONE) {
		kexec_segment_flush(kimage);
		kexec_image_info(kimage);
		return 0;
	}

	kimage->arch.el2_vectors = 0;
	if (is_hyp_nvhe()) {
		rc = trans_pgd_copy_el2_vectors(&info,
						&kimage->arch.el2_vectors);
		if (rc)
			return rc;
	}

	/* Create a copy of the linear map */
	trans_pgd = kexec_page_alloc(kimage);
	if (!trans_pgd)
		return -ENOMEM;
	rc = trans_pgd_create_copy(&info, &trans_pgd, PAGE_OFFSET, PAGE_END);
	if (rc)
		return rc;
	kimage->arch.ttbr1 = __pa(trans_pgd);
	kimage->arch.zero_page = __pa(empty_zero_page);

	reloc_size = __relocate_new_kernel_end - __relocate_new_kernel_start;
	memcpy(reloc_code, __relocate_new_kernel_start, reloc_size);
	kimage->arch.kern_reloc = __pa(reloc_code);
	rc = trans_pgd_idmap_page(&info, &kimage->arch.ttbr0,
				  &kimage->arch.t0sz, reloc_code);
	if (rc)
		return rc;
	kimage->arch.phys_offset = virt_to_phys(kimage) - (long)kimage;

	/* Flush the reloc_code in preparation for its execution. */
	dcache_clean_inval_poc((unsigned long)reloc_code,
			       (unsigned long)reloc_code + reloc_size);
	icache_inval_pou((uintptr_t)reloc_code,
			 (uintptr_t)reloc_code + reloc_size);
	kexec_image_info(kimage);

	return 0;
}

/**
 * machine_kexec - Do the kexec reboot.
 *
 * Called from the core kexec code for a sys_reboot with LINUX_REBOOT_CMD_KEXEC.
 */
void machine_kexec(struct kimage *kimage)
{
	bool in_kexec_crash = (kimage == kexec_crash_image);
	bool stuck_cpus = cpus_are_stuck_in_kernel();

	/*
	 * New cpus may have become stuck_in_kernel after we loaded the image.
	 */
	BUG_ON(!in_kexec_crash && (stuck_cpus || (num_online_cpus() > 1)));
	WARN(in_kexec_crash && (stuck_cpus || smp_crash_stop_failed()),
		"Some CPUs may be stale, kdump will be unreliable.\n");

	pr_info("Bye!\n");

	local_daif_mask();

	/*
	 * Both restart and kernel_reloc will shutdown the MMU, disable data
	 * caches. However, restart will start new kernel or purgatory directly,
	 * kernel_reloc contains the body of arm64_relocate_new_kernel
	 * In kexec case, kimage->start points to purgatory assuming that
	 * kernel entry and dtb address are embedded in purgatory by
	 * userspace (kexec-tools).
	 * In kexec_file case, the kernel starts directly without purgatory.
	 */
	if (kimage->head & IND_DONE) {
		typeof(cpu_soft_restart) *restart;

		cpu_install_idmap();
		restart = (void *)__pa_symbol(function_nocfi(cpu_soft_restart));
		restart(is_hyp_nvhe(), kimage->start, kimage->arch.dtb_mem,
			0, 0);
	} else {
		void (*kernel_reloc)(struct kimage *kimage);

		if (is_hyp_nvhe())
			__hyp_set_vectors(kimage->arch.el2_vectors);
		cpu_install_ttbr0(kimage->arch.ttbr0, kimage->arch.t0sz);
		kernel_reloc = (void *)kimage->arch.kern_reloc;
		kernel_reloc(kimage);
	}

	BUG(); /* Should never get here. */
}

static void machine_kexec_mask_interrupts(void)
{
	unsigned int i;
	struct irq_desc *desc;

	for_each_irq_desc(i, desc) {
		struct irq_chip *chip;
		int ret;

		chip = irq_desc_get_chip(desc);
		if (!chip)
			continue;

		/*
		 * First try to remove the active state. If this
		 * fails, try to EOI the interrupt.
		 */
		ret = irq_set_irqchip_state(i, IRQCHIP_STATE_ACTIVE, false);

		if (ret && irqd_irq_inprogress(&desc->irq_data) &&
		    chip->irq_eoi)
			chip->irq_eoi(&desc->irq_data);

		if (chip->irq_mask)
			chip->irq_mask(&desc->irq_data);

		if (chip->irq_disable && !irqd_irq_disabled(&desc->irq_data))
			chip->irq_disable(&desc->irq_data);
	}
}

/**
 * machine_crash_shutdown - shutdown non-crashing cpus and save registers
 */
void machine_crash_shutdown(struct pt_regs *regs)
{
	local_irq_disable();

	/* shutdown non-crashing cpus */
	crash_smp_send_stop();

	/* for crashing cpu */
	crash_save_cpu(regs, smp_processor_id());
	machine_kexec_mask_interrupts();

	pr_info("Starting crashdump kernel...\n");
}

void arch_kexec_protect_crashkres(void)
{
	int i;

	for (i = 0; i < kexec_crash_image->nr_segments; i++)
		set_memory_valid(
			__phys_to_virt(kexec_crash_image->segment[i].mem),
			kexec_crash_image->segment[i].memsz >> PAGE_SHIFT, 0);
}

void arch_kexec_unprotect_crashkres(void)
{
	int i;

	for (i = 0; i < kexec_crash_image->nr_segments; i++)
		set_memory_valid(
			__phys_to_virt(kexec_crash_image->segment[i].mem),
			kexec_crash_image->segment[i].memsz >> PAGE_SHIFT, 1);
}

#ifdef CONFIG_HIBERNATION
/*
 * To preserve the crash dump kernel image, the relevant memory segments
 * should be mapped again around the hibernation.
 */
void crash_prepare_suspend(void)
{
	if (kexec_crash_image)
		arch_kexec_unprotect_crashkres();
}

void crash_post_resume(void)
{
	if (kexec_crash_image)
		arch_kexec_protect_crashkres();
}

/*
 * crash_is_nosave
 *
 * Return true only if a page is part of reserved memory for crash dump kernel,
 * but does not hold any data of loaded kernel image.
 *
 * Note that all the pages in crash dump kernel memory have been initially
 * marked as Reserved as memory was allocated via memblock_reserve().
 *
 * In hibernation, the pages which are Reserved and yet "nosave" are excluded
 * from the hibernation iamge. crash_is_nosave() does thich check for crash
 * dump kernel and will reduce the total size of hibernation image.
 */

bool crash_is_nosave(unsigned long pfn)
{
	int i;
	phys_addr_t addr;

	if (!crashk_res.end)
		return false;

	/* in reserved memory? */
	addr = __pfn_to_phys(pfn);
	if ((addr < crashk_res.start) || (crashk_res.end < addr))
		return false;

	if (!kexec_crash_image)
		return true;

	/* not part of loaded kernel image? */
	for (i = 0; i < kexec_crash_image->nr_segments; i++)
		if (addr >= kexec_crash_image->segment[i].mem &&
				addr < (kexec_crash_image->segment[i].mem +
					kexec_crash_image->segment[i].memsz))
			return false;

	return true;
}

void crash_free_reserved_phys_range(unsigned long begin, unsigned long end)
{
	unsigned long addr;
	struct page *page;

	for (addr = begin; addr < end; addr += PAGE_SIZE) {
		page = phys_to_page(addr);
		free_reserved_page(page);
	}
}
#endif /* CONFIG_HIBERNATION */
