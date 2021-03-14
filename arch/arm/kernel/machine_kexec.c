// SPDX-License-Identifier: GPL-2.0
/*
 * machine_kexec.c - handle transition of Linux booting another kernel
 */

#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/kexec-internal.h>
#include <asm/fncpy.h>
#include <asm/mach-types.h>
#include <asm/smp_plat.h>
#include <asm/system_misc.h>
#include <asm/set_memory.h>

extern void relocate_new_kernel(void);
extern const unsigned int relocate_new_kernel_size;

static atomic_t waiting_for_crash_ipi;

/*
 * Provide a dummy crash_notes definition while crash dump arrives to arm.
 * This prevents breakage of crash_notes attribute in kernel/ksysfs.c.
 */

int machine_kexec_prepare(struct kimage *image)
{
	struct kexec_segment *current_segment;
	__be32 header;
	int i, err;

	image->arch.kernel_r2 = image->start - KEXEC_ARM_ZIMAGE_OFFSET
				     + KEXEC_ARM_ATAGS_OFFSET;

	/*
	 * Validate that if the current HW supports SMP, then the SW supports
	 * and implements CPU hotplug for the current HW. If not, we won't be
	 * able to kexec reliably, so fail the prepare operation.
	 */
	if (num_possible_cpus() > 1 && platform_can_secondary_boot() &&
	    !platform_can_cpu_hotplug())
		return -EINVAL;

	/*
	 * No segment at default ATAGs address. try to locate
	 * a dtb using magic.
	 */
	for (i = 0; i < image->nr_segments; i++) {
		current_segment = &image->segment[i];

		if (!memblock_is_region_memory(idmap_to_phys(current_segment->mem),
					       current_segment->memsz))
			return -EINVAL;

		err = get_user(header, (__be32*)current_segment->buf);
		if (err)
			return err;

		if (header == cpu_to_be32(OF_DT_HEADER))
			image->arch.kernel_r2 = current_segment->mem;
	}
	return 0;
}

void machine_kexec_cleanup(struct kimage *image)
{
}

void machine_crash_nonpanic_core(void *unused)
{
	struct pt_regs regs;

	crash_setup_regs(&regs, get_irq_regs());
	printk(KERN_DEBUG "CPU %u will stop doing anything useful since another CPU has crashed\n",
	       smp_processor_id());
	crash_save_cpu(&regs, smp_processor_id());
	flush_cache_all();

	set_cpu_online(smp_processor_id(), false);
	atomic_dec(&waiting_for_crash_ipi);

	while (1) {
		cpu_relax();
		wfe();
	}
}

void crash_smp_send_stop(void)
{
	static int cpus_stopped;
	unsigned long msecs;

	if (cpus_stopped)
		return;

	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);
	smp_call_function(machine_crash_nonpanic_core, NULL, false);
	msecs = 1000; /* Wait at most a second for the other cpus to stop */
	while ((atomic_read(&waiting_for_crash_ipi) > 0) && msecs) {
		mdelay(1);
		msecs--;
	}
	if (atomic_read(&waiting_for_crash_ipi) > 0)
		pr_warn("Non-crashing CPUs did not react to IPI\n");

	cpus_stopped = 1;
}

static void machine_kexec_mask_interrupts(void)
{
	unsigned int i;
	struct irq_desc *desc;

	for_each_irq_desc(i, desc) {
		struct irq_chip *chip;

		chip = irq_desc_get_chip(desc);
		if (!chip)
			continue;

		if (chip->irq_eoi && irqd_irq_inprogress(&desc->irq_data))
			chip->irq_eoi(&desc->irq_data);

		if (chip->irq_mask)
			chip->irq_mask(&desc->irq_data);

		if (chip->irq_disable && !irqd_irq_disabled(&desc->irq_data))
			chip->irq_disable(&desc->irq_data);
	}
}

void machine_crash_shutdown(struct pt_regs *regs)
{
	local_irq_disable();
	crash_smp_send_stop();

	crash_save_cpu(regs, smp_processor_id());
	machine_kexec_mask_interrupts();

	pr_info("Loading crashdump kernel...\n");
}

/*
 * Function pointer to optional machine-specific reinitialization
 */
void (*kexec_reinit)(void);

void machine_kexec(struct kimage *image)
{
	unsigned long page_list, reboot_entry_phys;
	struct kexec_relocate_data *data;
	void (*reboot_entry)(void);
	void *reboot_code_buffer;

	/*
	 * This can only happen if machine_shutdown() failed to disable some
	 * CPU, and that can only happen if the checks in
	 * machine_kexec_prepare() were not correct. If this fails, we can't
	 * reliably kexec anyway, so BUG_ON is appropriate.
	 */
	BUG_ON(num_online_cpus() > 1);

	page_list = image->head & PAGE_MASK;

	reboot_code_buffer = page_address(image->control_code_page);

	/* copy our kernel relocation code to the control code page */
	reboot_entry = fncpy(reboot_code_buffer,
			     &relocate_new_kernel,
			     relocate_new_kernel_size);

	data = reboot_code_buffer + relocate_new_kernel_size;
	data->kexec_start_address = image->start;
	data->kexec_indirection_page = page_list;
	data->kexec_mach_type = machine_arch_type;
	data->kexec_r2 = image->arch.kernel_r2;

	/* get the identity mapping physical address for the reboot code */
	reboot_entry_phys = virt_to_idmap(reboot_entry);

	pr_info("Bye!\n");

	if (kexec_reinit)
		kexec_reinit();

	soft_restart(reboot_entry_phys);
}

void arch_crash_save_vmcoreinfo(void)
{
#ifdef CONFIG_ARM_LPAE
	VMCOREINFO_CONFIG(ARM_LPAE);
#endif
}
