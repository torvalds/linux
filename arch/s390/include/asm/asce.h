/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_ASCE_H
#define _ASM_S390_ASCE_H

#include <linux/thread_info.h>
#include <linux/irqflags.h>
#include <asm/lowcore.h>
#include <asm/ctlreg.h>

static inline bool enable_sacf_uaccess(void)
{
	unsigned long flags;

	if (test_thread_flag(TIF_ASCE_PRIMARY))
		return true;
	local_irq_save(flags);
	local_ctl_load(1, &get_lowcore()->kernel_asce);
	set_thread_flag(TIF_ASCE_PRIMARY);
	local_irq_restore(flags);
	return false;
}

static inline void disable_sacf_uaccess(bool previous)
{
	unsigned long flags;

	if (previous)
		return;
	local_irq_save(flags);
	local_ctl_load(1, &get_lowcore()->user_asce);
	clear_thread_flag(TIF_ASCE_PRIMARY);
	local_irq_restore(flags);
}

#endif /* _ASM_S390_ASCE_H */
