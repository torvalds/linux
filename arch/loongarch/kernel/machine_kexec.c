// SPDX-License-Identifier: GPL-2.0-only
/*
 * machine_kexec.c for kexec
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/kexec.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/libfdt.h>
#include <linux/of_fdt.h>

#include <asm/bootinfo.h>
#include <asm/cacheflush.h>
#include <asm/page.h>

/* 0x100000 ~ 0x200000 is safe */
#define KEXEC_CONTROL_CODE	TO_CACHE(0x100000UL)
#define KEXEC_CMDLINE_ADDR	TO_CACHE(0x108000UL)

static unsigned long reboot_code_buffer;

#ifdef CONFIG_SMP
static void (*relocated_kexec_smp_wait)(void *);
atomic_t kexec_ready_to_reboot = ATOMIC_INIT(0);
#endif

static unsigned long efi_boot;
static unsigned long cmdline_ptr;
static unsigned long systable_ptr;
static unsigned long start_addr;
static unsigned long first_ind_entry;

static void kexec_image_info(const struct kimage *kimage)
{
	unsigned long i;

	pr_debug("kexec kimage info:\n");
	pr_debug("\ttype:        %d\n", kimage->type);
	pr_debug("\tstart:       %lx\n", kimage->start);
	pr_debug("\thead:        %lx\n", kimage->head);
	pr_debug("\tnr_segments: %lu\n", kimage->nr_segments);

	for (i = 0; i < kimage->nr_segments; i++) {
		pr_debug("\t    segment[%lu]: %016lx - %016lx", i,
			kimage->segment[i].mem,
			kimage->segment[i].mem + kimage->segment[i].memsz);
		pr_debug("\t\t0x%lx bytes, %lu pages\n",
			(unsigned long)kimage->segment[i].memsz,
			(unsigned long)kimage->segment[i].memsz /  PAGE_SIZE);
	}
}

int machine_kexec_prepare(struct kimage *kimage)
{
	int i;
	char *bootloader = "kexec";
	void *cmdline_ptr = (void *)KEXEC_CMDLINE_ADDR;

	kexec_image_info(kimage);

	kimage->arch.efi_boot = fw_arg0;
	kimage->arch.systable_ptr = fw_arg2;

	/* Find the command line */
	for (i = 0; i < kimage->nr_segments; i++) {
		if (!strncmp(bootloader, (char __user *)kimage->segment[i].buf, strlen(bootloader))) {
			if (!copy_from_user(cmdline_ptr, kimage->segment[i].buf, COMMAND_LINE_SIZE))
				kimage->arch.cmdline_ptr = (unsigned long)cmdline_ptr;
			break;
		}
	}

	if (!kimage->arch.cmdline_ptr) {
		pr_err("Command line not included in the provided image\n");
		return -EINVAL;
	}

	/* kexec need a safe page to save reboot_code_buffer */
	kimage->control_code_page = virt_to_page((void *)KEXEC_CONTROL_CODE);

	reboot_code_buffer = (unsigned long)page_address(kimage->control_code_page);
	memcpy((void *)reboot_code_buffer, relocate_new_kernel, relocate_new_kernel_size);

#ifdef CONFIG_SMP
	/* All secondary cpus now may jump to kexec_smp_wait cycle */
	relocated_kexec_smp_wait = reboot_code_buffer + (void *)(kexec_smp_wait - relocate_new_kernel);
#endif

	return 0;
}

void machine_kexec_cleanup(struct kimage *kimage)
{
}

void kexec_reboot(void)
{
	do_kexec_t do_kexec = NULL;

	/*
	 * We know we were online, and there will be no incoming IPIs at
	 * this point.
	 */
	set_cpu_online(smp_processor_id(), true);

	/* Ensure remote CPUs observe that we're online before rebooting. */
	smp_mb__after_atomic();

	/*
	 * Make sure we get correct instructions written by the
	 * machine_kexec_prepare() CPU.
	 */
	__asm__ __volatile__ ("\tibar 0\n"::);

#ifdef CONFIG_SMP
	/* All secondary cpus go to kexec_smp_wait */
	if (smp_processor_id() > 0) {
		relocated_kexec_smp_wait(NULL);
		unreachable();
	}
#endif

	do_kexec = (void *)reboot_code_buffer;
	do_kexec(efi_boot, cmdline_ptr, systable_ptr, start_addr, first_ind_entry);

	unreachable();
}


#ifdef CONFIG_SMP
static void kexec_shutdown_secondary(void *regs)
{
	int cpu = smp_processor_id();

	if (!cpu_online(cpu))
		return;

	/* We won't be sent IPIs any more. */
	set_cpu_online(cpu, false);

	local_irq_disable();
	while (!atomic_read(&kexec_ready_to_reboot))
		cpu_relax();

	kexec_reboot();
}
#endif

void machine_shutdown(void)
{
	int cpu;

	/* All CPUs go to reboot_code_buffer */
	for_each_possible_cpu(cpu)
		if (!cpu_online(cpu))
			cpu_device_up(get_cpu_device(cpu));

#ifdef CONFIG_SMP
	smp_call_function(kexec_shutdown_secondary, NULL, 0);
#endif
}

void machine_crash_shutdown(struct pt_regs *regs)
{
}

void machine_kexec(struct kimage *image)
{
	unsigned long entry, *ptr;
	struct kimage_arch *internal = &image->arch;

	efi_boot = internal->efi_boot;
	cmdline_ptr = internal->cmdline_ptr;
	systable_ptr = internal->systable_ptr;

	start_addr = (unsigned long)phys_to_virt(image->start);

	first_ind_entry = (unsigned long)phys_to_virt(image->head & PAGE_MASK);

	/*
	 * The generic kexec code builds a page list with physical
	 * addresses. they are directly accessible through XKPRANGE
	 * hence the phys_to_virt() call.
	 */
	for (ptr = &image->head; (entry = *ptr) && !(entry & IND_DONE);
	     ptr = (entry & IND_INDIRECTION) ?
	       phys_to_virt(entry & PAGE_MASK) : ptr + 1) {
		if (*ptr & IND_SOURCE || *ptr & IND_INDIRECTION ||
		    *ptr & IND_DESTINATION)
			*ptr = (unsigned long) phys_to_virt(*ptr);
	}

	/* Mark offline before disabling local irq. */
	set_cpu_online(smp_processor_id(), false);

	/* We do not want to be bothered. */
	local_irq_disable();

	pr_notice("EFI boot flag 0x%lx\n", efi_boot);
	pr_notice("Command line at 0x%lx\n", cmdline_ptr);
	pr_notice("System table at 0x%lx\n", systable_ptr);
	pr_notice("We will call new kernel at 0x%lx\n", start_addr);
	pr_notice("Bye ...\n");

	/* Make reboot code buffer available to the boot CPU. */
	flush_cache_all();

#ifdef CONFIG_SMP
	atomic_set(&kexec_ready_to_reboot, 1);
#endif

	kexec_reboot();
}
