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
