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
#include <asm/machdep.h>
#include <asm/lmb.h>

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

static int __init early_parse_crashk(char *p)
{
	unsigned long size;

	if (!p)
		return 1;

	size = memparse(p, &p);

	if (*p == '@')
		crashk_res.start = memparse(p + 1, &p);
	else
		crashk_res.start = KDUMP_KERNELBASE;

	crashk_res.end = crashk_res.start + size - 1;

	return 0;
}
early_param("crashkernel", early_parse_crashk);

void __init reserve_crashkernel(void)
{
	unsigned long size;

	if (crashk_res.start == 0)
		return;

	/* We might have got these values via the command line or the
	 * device tree, either way sanitise them now. */

	size = crashk_res.end - crashk_res.start + 1;

	if (crashk_res.start != KDUMP_KERNELBASE)
		printk("Crash kernel location must be 0x%x\n",
				KDUMP_KERNELBASE);

	crashk_res.start = KDUMP_KERNELBASE;
	size = PAGE_ALIGN(size);
	crashk_res.end = crashk_res.start + size - 1;

	/* Crash kernel trumps memory limit */
	if (memory_limit && memory_limit <= crashk_res.end) {
		memory_limit = crashk_res.end + 1;
		printk("Adjusted memory limit for crashkernel, now 0x%lx\n",
				memory_limit);
	}

	lmb_reserve(crashk_res.start, size);
}

int overlaps_crashkernel(unsigned long start, unsigned long size)
{
	return (start + size) > crashk_res.start && start <= crashk_res.end;
}
