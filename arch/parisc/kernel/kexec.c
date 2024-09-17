// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <asm/cacheflush.h>
#include <asm/sections.h>

extern void relocate_new_kernel(unsigned long head,
				unsigned long start,
				unsigned long phys);

extern const unsigned int relocate_new_kernel_size;
extern unsigned int kexec_initrd_start_offset;
extern unsigned int kexec_initrd_end_offset;
extern unsigned int kexec_cmdline_offset;
extern unsigned int kexec_free_mem_offset;

static void kexec_show_segment_info(const struct kimage *kimage,
				    unsigned long n)
{
	pr_debug("    segment[%lu]: %016lx - %016lx, 0x%lx bytes, %lu pages\n",
			n,
			kimage->segment[n].mem,
			kimage->segment[n].mem + kimage->segment[n].memsz,
			(unsigned long)kimage->segment[n].memsz,
			(unsigned long)kimage->segment[n].memsz /  PAGE_SIZE);
}

static void kexec_image_info(const struct kimage *kimage)
{
	unsigned long i;

	pr_debug("kexec kimage info:\n");
	pr_debug("  type:        %d\n", kimage->type);
	pr_debug("  start:       %lx\n", kimage->start);
	pr_debug("  head:        %lx\n", kimage->head);
	pr_debug("  nr_segments: %lu\n", kimage->nr_segments);

	for (i = 0; i < kimage->nr_segments; i++)
		kexec_show_segment_info(kimage, i);

#ifdef CONFIG_KEXEC_FILE
	if (kimage->file_mode) {
		pr_debug("cmdline: %.*s\n", (int)kimage->cmdline_buf_len,
			 kimage->cmdline_buf);
	}
#endif
}

void machine_kexec_cleanup(struct kimage *kimage)
{
}

void machine_crash_shutdown(struct pt_regs *regs)
{
}

void machine_shutdown(void)
{
	smp_send_stop();
	while (num_online_cpus() > 1) {
		cpu_relax();
		mdelay(1);
	}
}

void machine_kexec(struct kimage *image)
{
#ifdef CONFIG_64BIT
	Elf64_Fdesc desc;
#endif
	void (*reloc)(unsigned long head,
		      unsigned long start,
		      unsigned long phys);

	unsigned long phys = page_to_phys(image->control_code_page);
	void *virt = (void *)__fix_to_virt(FIX_TEXT_KEXEC);
	struct kimage_arch *arch = &image->arch;

	set_fixmap(FIX_TEXT_KEXEC, phys);

	flush_cache_all();

#ifdef CONFIG_64BIT
	reloc = (void *)&desc;
	desc.addr = (long long)virt;
#else
	reloc = (void *)virt;
#endif

	memcpy(virt, dereference_function_descriptor(relocate_new_kernel),
		relocate_new_kernel_size);

	*(unsigned long *)(virt + kexec_cmdline_offset) = arch->cmdline;
	*(unsigned long *)(virt + kexec_initrd_start_offset) = arch->initrd_start;
	*(unsigned long *)(virt + kexec_initrd_end_offset) = arch->initrd_end;
	*(unsigned long *)(virt + kexec_free_mem_offset) = PAGE0->mem_free;

	flush_cache_all();
	flush_tlb_all();
	local_irq_disable();

	reloc(image->head & PAGE_MASK, image->start, phys);
}

int machine_kexec_prepare(struct kimage *image)
{
	kexec_image_info(image);
	return 0;
}
