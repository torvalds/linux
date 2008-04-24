/*
 * Code to handle transition of Linux booting another kernel.
 *
 * Copyright (C) 2002-2003 Eric Biederman  <ebiederm@xmission.com>
 * GameCube/ppc32 port Copyright (C) 2004 Albert Herranz
 * Copyright (C) 2005 IBM Corporation.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/kexec.h>
#include <linux/reboot.h>
#include <linux/threads.h>
#include <linux/lmb.h>
#include <asm/machdep.h>
#include <asm/prom.h>

void machine_crash_shutdown(struct pt_regs *regs)
{
	if (ppc_md.machine_crash_shutdown)
		ppc_md.machine_crash_shutdown(regs);
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

void __init reserve_crashkernel(void)
{
	unsigned long long crash_size, crash_base;
	int ret;

	/* this is necessary because of lmb_phys_mem_size() */
	lmb_analyze();

	/* use common parsing */
	ret = parse_crashkernel(boot_command_line, lmb_phys_mem_size(),
			&crash_size, &crash_base);
	if (ret == 0 && crash_size > 0) {
		if (crash_base == 0)
			crash_base = KDUMP_KERNELBASE;
		crashk_res.start = crash_base;
	} else {
		/* handle the device tree */
		crash_size = crashk_res.end - crashk_res.start + 1;
	}

	if (crash_size == 0)
		return;

	/* We might have got these values via the command line or the
	 * device tree, either way sanitise them now. */

	if (crashk_res.start != KDUMP_KERNELBASE)
		printk("Crash kernel location must be 0x%x\n",
				KDUMP_KERNELBASE);

	crashk_res.start = KDUMP_KERNELBASE;
	crash_size = PAGE_ALIGN(crash_size);
	crashk_res.end = crashk_res.start + crash_size - 1;

	/* Crash kernel trumps memory limit */
	if (memory_limit && memory_limit <= crashk_res.end) {
		memory_limit = crashk_res.end + 1;
		printk("Adjusted memory limit for crashkernel, now 0x%lx\n",
				memory_limit);
	}

	printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
			"for crashkernel (System RAM: %ldMB)\n",
			(unsigned long)(crash_size >> 20),
			(unsigned long)(crashk_res.start >> 20),
			(unsigned long)(lmb_phys_mem_size() >> 20));

	lmb_reserve(crashk_res.start, crash_size);
}

int overlaps_crashkernel(unsigned long start, unsigned long size)
{
	return (start + size) > crashk_res.start && start <= crashk_res.end;
}
