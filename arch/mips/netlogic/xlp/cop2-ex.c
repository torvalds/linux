/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Broadcom Corporation.
 *
 * based on arch/mips/cavium-octeon/cpu.c
 * Copyright (C) 2009 Wind River Systems,
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/notifier.h>
#include <linux/prefetch.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>

#include <asm/cop2.h>
#include <asm/current.h>
#include <asm/mipsregs.h>
#include <asm/page.h>

#include <asm/netlogic/mips-extns.h>

/*
 * 64 bit ops are done in inline assembly to support 32 bit
 * compilation
 */
void nlm_cop2_save(struct nlm_cop2_state *r)
{
	asm volatile(
		".set	push\n"
		".set	noat\n"
		"dmfc2	$1, $0, 0\n"
		"sd	$1, 0(%1)\n"
		"dmfc2	$1, $0, 1\n"
		"sd	$1, 8(%1)\n"
		"dmfc2	$1, $0, 2\n"
		"sd	$1, 16(%1)\n"
		"dmfc2	$1, $0, 3\n"
		"sd	$1, 24(%1)\n"
		"dmfc2	$1, $1, 0\n"
		"sd	$1, 0(%2)\n"
		"dmfc2	$1, $1, 1\n"
		"sd	$1, 8(%2)\n"
		"dmfc2	$1, $1, 2\n"
		"sd	$1, 16(%2)\n"
		"dmfc2	$1, $1, 3\n"
		"sd	$1, 24(%2)\n"
		".set	pop\n"
		: "=m"(*r)
		: "r"(r->tx), "r"(r->rx));

	r->tx_msg_status = __read_32bit_c2_register($2, 0);
	r->rx_msg_status = __read_32bit_c2_register($3, 0) & 0x0fffffff;
}

void nlm_cop2_restore(struct nlm_cop2_state *r)
{
	u32 rstat;

	asm volatile(
		".set	push\n"
		".set	noat\n"
		"ld	$1, 0(%1)\n"
		"dmtc2	$1, $0, 0\n"
		"ld	$1, 8(%1)\n"
		"dmtc2	$1, $0, 1\n"
		"ld	$1, 16(%1)\n"
		"dmtc2	$1, $0, 2\n"
		"ld	$1, 24(%1)\n"
		"dmtc2	$1, $0, 3\n"
		"ld	$1, 0(%2)\n"
		"dmtc2	$1, $1, 0\n"
		"ld	$1, 8(%2)\n"
		"dmtc2	$1, $1, 1\n"
		"ld	$1, 16(%2)\n"
		"dmtc2	$1, $1, 2\n"
		"ld	$1, 24(%2)\n"
		"dmtc2	$1, $1, 3\n"
		".set	pop\n"
		: : "m"(*r), "r"(r->tx), "r"(r->rx));

	__write_32bit_c2_register($2, 0, r->tx_msg_status);
	rstat = __read_32bit_c2_register($3, 0) & 0xf0000000u;
	__write_32bit_c2_register($3, 0, r->rx_msg_status | rstat);
}

static int nlm_cu2_call(struct notifier_block *nfb, unsigned long action,
	void *data)
{
	unsigned long flags;
	unsigned int status;

	switch (action) {
	case CU2_EXCEPTION:
		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			break;
		local_irq_save(flags);
		KSTK_STATUS(current) |= ST0_CU2;
		status = read_c0_status();
		write_c0_status(status | ST0_CU2);
		nlm_cop2_restore(&(current->thread.cp2));
		write_c0_status(status & ~ST0_CU2);
		local_irq_restore(flags);
		pr_info("COP2 access enabled for pid %d (%s)\n",
					current->pid, current->comm);
		return NOTIFY_BAD;	/* Don't call default notifier */
	}

	return NOTIFY_OK;		/* Let default notifier send signals */
}

static int __init nlm_cu2_setup(void)
{
	return cu2_notifier(nlm_cu2_call, 0);
}
early_initcall(nlm_cu2_setup);
