/*
 * arch/s390/kernel/machine_kexec.c
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Author(s): Rolf Adelsberger <adelsberger@de.ibm.com>
 *
 */

/*
 * s390_machine_kexec.c - handle the transition of Linux booting another kernel
 * on the S390 architecture.
 */

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <asm/cio.h>
#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
#include <asm/smp.h>

static void kexec_halt_all_cpus(void *);

typedef void (*relocate_kernel_t) (kimage_entry_t *, unsigned long);

const extern unsigned char relocate_kernel[];
const extern unsigned long long relocate_kernel_len;

int
machine_kexec_prepare(struct kimage *image)
{
	unsigned long reboot_code_buffer;

	/* We don't support anything but the default image type for now. */
	if (image->type != KEXEC_TYPE_DEFAULT)
		return -EINVAL;

	/* Get the destination where the assembler code should be copied to.*/
	reboot_code_buffer = page_to_pfn(image->control_code_page)<<PAGE_SHIFT;

	/* Then copy it */
	memcpy((void *) reboot_code_buffer, relocate_kernel,
	       relocate_kernel_len);
	return 0;
}

void
machine_kexec_cleanup(struct kimage *image)
{
}

void
machine_shutdown(void)
{
	printk(KERN_INFO "kexec: machine_shutdown called\n");
}

NORET_TYPE void
machine_kexec(struct kimage *image)
{
	clear_all_subchannels();

	/* Disable lowcore protection */
	ctl_clear_bit(0,28);

	on_each_cpu(kexec_halt_all_cpus, image, 0, 0);
	for (;;);
}

extern void pfault_fini(void);

static void
kexec_halt_all_cpus(void *kernel_image)
{
	static atomic_t cpuid = ATOMIC_INIT(-1);
	int cpu;
	struct kimage *image;
	relocate_kernel_t data_mover;

#ifdef CONFIG_PFAULT
	if (MACHINE_IS_VM)
		pfault_fini();
#endif

	if (atomic_cmpxchg(&cpuid, -1, smp_processor_id()) != -1)
		signal_processor(smp_processor_id(), sigp_stop);

	/* Wait for all other cpus to enter stopped state */
	for_each_online_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		while (!smp_cpu_not_running(cpu))
			cpu_relax();
	}

	image = (struct kimage *) kernel_image;
	data_mover = (relocate_kernel_t)
		(page_to_pfn(image->control_code_page) << PAGE_SHIFT);

	/* Call the moving routine */
	(*data_mover) (&image->head, image->start);
}
