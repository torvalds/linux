/*
 * machine_kexec.c - handle transition of Linux booting another kernel
 * Copyright (C) 2002-2003 Eric Biederman  <ebiederm@xmission.com>
 *
 * GameCube/ppc32 port Copyright (C) 2004 Albert Herranz
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/mm.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/hw_irq.h>
#include <asm/cacheflush.h>
#include <asm/machdep.h>

typedef NORET_TYPE void (*relocate_new_kernel_t)(
				unsigned long indirection_page,
				unsigned long reboot_code_buffer,
				unsigned long start_address) ATTRIB_NORET;

const extern unsigned char relocate_new_kernel[];
const extern unsigned int relocate_new_kernel_size;

/*
 * Provide a dummy crash_notes definition while crash dump arrives to ppc.
 * This prevents breakage of crash_notes attribute in kernel/ksysfs.c.
 */
void *crash_notes = NULL;

void machine_shutdown(void)
{
	if (ppc_md.machine_shutdown)
		ppc_md.machine_shutdown();
}

void machine_crash_shutdown(struct pt_regs *regs)
{
	if (ppc_md.machine_crash_shutdown)
		ppc_md.machine_crash_shutdown();
}

/*
 * Do what every setup is needed on image and the
 * reboot code buffer to allow us to avoid allocations
 * later.
 */
int machine_kexec_prepare(struct kimage *image)
{
	if (ppc_md.machine_kexec_prepare)
		return ppc_md.machine_kexec_prepare(image);
	/*
	 * Fail if platform doesn't provide its own machine_kexec_prepare
	 * implementation.
	 */
	return -ENOSYS;
}

void machine_kexec_cleanup(struct kimage *image)
{
	if (ppc_md.machine_kexec_cleanup)
		ppc_md.machine_kexec_cleanup(image);
}

/*
 * Do not allocate memory (or fail in any way) in machine_kexec().
 * We are past the point of no return, committed to rebooting now.
 */
NORET_TYPE void machine_kexec(struct kimage *image)
{
	if (ppc_md.machine_kexec)
		ppc_md.machine_kexec(image);
	else {
		/*
		 * Fall back to normal restart if platform doesn't provide
		 * its own kexec function, and user insist to kexec...
		 */
		machine_restart(NULL);
	}
	for(;;);
}

/*
 * This is a generic machine_kexec function suitable at least for
 * non-OpenFirmware embedded platforms.
 * It merely copies the image relocation code to the control page and
 * jumps to it.
 * A platform specific function may just call this one.
 */
void machine_kexec_simple(struct kimage *image)
{
	unsigned long page_list;
	unsigned long reboot_code_buffer, reboot_code_buffer_phys;
	relocate_new_kernel_t rnk;

	/* Interrupts aren't acceptable while we reboot */
	local_irq_disable();

	page_list = image->head;

	/* we need both effective and real address here */
	reboot_code_buffer =
			(unsigned long)page_address(image->control_code_page);
	reboot_code_buffer_phys = virt_to_phys((void *)reboot_code_buffer);

	/* copy our kernel relocation code to the control code page */
	memcpy((void *)reboot_code_buffer, relocate_new_kernel,
						relocate_new_kernel_size);

	flush_icache_range(reboot_code_buffer,
				reboot_code_buffer + KEXEC_CONTROL_CODE_SIZE);
	printk(KERN_INFO "Bye!\n");

	/* now call it */
	rnk = (relocate_new_kernel_t) reboot_code_buffer;
	(*rnk)(page_list, reboot_code_buffer_phys, image->start);
}

