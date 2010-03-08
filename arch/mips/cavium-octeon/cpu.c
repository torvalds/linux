/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009 Wind River Systems,
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/notifier.h>
#include <linux/prefetch.h>
#include <linux/sched.h>

#include <asm/cop2.h>
#include <asm/current.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/octeon/octeon.h>

static int cnmips_cu2_call(struct notifier_block *nfb, unsigned long action,
	void *data)
{
	unsigned long flags;
	unsigned int status;

	switch (action) {
	case CU2_EXCEPTION:
		prefetch(&current->thread.cp2);
		local_irq_save(flags);
		KSTK_STATUS(current) |= ST0_CU2;
		status = read_c0_status();
		write_c0_status(status | ST0_CU2);
		octeon_cop2_restore(&(current->thread.cp2));
		write_c0_status(status & ~ST0_CU2);
		local_irq_restore(flags);

		return NOTIFY_BAD;	/* Don't call default notifier */
	}

	return NOTIFY_OK;		/* Let default notifier send signals */
}

static struct notifier_block cnmips_cu2_notifier = {
	.notifier_call = cnmips_cu2_call,
};

static int cnmips_cu2_setup(void)
{
	return register_cu2_notifier(&cnmips_cu2_notifier);
}
early_initcall(cnmips_cu2_setup);
