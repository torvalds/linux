// SPDX-License-Identifier: GPL-2.0-only
/*
 * PPC32 code to handle Linux booting another kernel.
 *
 * Copyright (C) 2002-2003 Eric Biederman  <ebiederm@xmission.com>
 * GameCube/ppc32 port Copyright (C) 2004 Albert Herranz
 * Copyright (C) 2005 IBM Corporation.
 */

#include <linux/irq.h>
#include <linux/kexec.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <asm/cacheflush.h>
#include <asm/hw_irq.h>
#include <asm/io.h>

typedef void (*relocate_new_kernel_t)(
				unsigned long indirection_page,
				unsigned long reboot_code_buffer,
				unsigned long start_address) __noreturn;

/*
 * This is a generic machine_kexec function suitable at least for
 * non-OpenFirmware embedded platforms.
 * It merely copies the image relocation code to the control page and
 * jumps to it.
 * A platform specific function may just call this one.
 */
void default_machine_kexec(struct kimage *image)
{
	extern const unsigned int relocate_new_kernel_size;
	unsigned long page_list;
	unsigned long reboot_code_buffer, reboot_code_buffer_phys;
	relocate_new_kernel_t rnk;

	/* Interrupts aren't acceptable while we reboot */
	local_irq_disable();

	/* mask each interrupt so we are in a more sane state for the
	 * kexec kernel */
	machine_kexec_mask_interrupts();

	page_list = image->head;

	/* we need both effective and real address here */
	reboot_code_buffer =
			(unsigned long)page_address(image->control_code_page);
	reboot_code_buffer_phys = virt_to_phys((void *)reboot_code_buffer);

	/* copy our kernel relocation code to the control code page */
	memcpy((void *)reboot_code_buffer, relocate_new_kernel,
						relocate_new_kernel_size);

	flush_icache_range(reboot_code_buffer,
				reboot_code_buffer + KEXEC_CONTROL_PAGE_SIZE);
	printk(KERN_INFO "Bye!\n");

	if (!IS_ENABLED(CONFIG_PPC_85xx) && !IS_ENABLED(CONFIG_44x))
		relocate_new_kernel(page_list, reboot_code_buffer_phys, image->start);

	/* now call it */
	rnk = (relocate_new_kernel_t) reboot_code_buffer;
	(*rnk)(page_list, reboot_code_buffer_phys, image->start);
}

int machine_kexec_prepare(struct kimage *image)
{
	return 0;
}
