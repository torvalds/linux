/*
 * arch/s390/kernel/machine_kexec.c
 *
 * Copyright IBM Corp. 2005,2006
 *
 * Author(s): Rolf Adelsberger,
 *	      Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/ftrace.h>
#include <asm/cio.h>
#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
#include <asm/smp.h>
#include <asm/reset.h>
#include <asm/ipl.h>

typedef void (*relocate_kernel_t)(kimage_entry_t *, unsigned long);

extern const unsigned char relocate_kernel[];
extern const unsigned long long relocate_kernel_len;

int machine_kexec_prepare(struct kimage *image)
{
	void *reboot_code_buffer;

	/* Can't replace kernel image since it is read-only. */
	if (ipl_flags & IPL_NSS_VALID)
		return -ENOSYS;

	/* We don't support anything but the default image type for now. */
	if (image->type != KEXEC_TYPE_DEFAULT)
		return -EINVAL;

	/* Get the destination where the assembler code should be copied to.*/
	reboot_code_buffer = (void *) page_to_phys(image->control_code_page);

	/* Then copy it */
	memcpy(reboot_code_buffer, relocate_kernel, relocate_kernel_len);
	return 0;
}

void machine_kexec_cleanup(struct kimage *image)
{
}

void machine_shutdown(void)
{
}

static void __machine_kexec(void *data)
{
	relocate_kernel_t data_mover;
	struct kimage *image = data;

	pfault_fini();
	s390_reset_system();

	data_mover = (relocate_kernel_t) page_to_phys(image->control_code_page);

	/* Call the moving routine */
	(*data_mover)(&image->head, image->start);
	for (;;);
}

void machine_kexec(struct kimage *image)
{
	tracer_disable();
	smp_send_stop();
	smp_switch_to_ipl_cpu(__machine_kexec, image);
}
