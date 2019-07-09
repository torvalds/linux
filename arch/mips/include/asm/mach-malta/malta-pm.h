/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#ifndef __ASM_MIPS_MACH_MALTA_PM_H__
#define __ASM_MIPS_MACH_MALTA_PM_H__

#include <asm/mips-boards/piix4.h>

#ifdef CONFIG_MIPS_MALTA_PM

/**
 * mips_pm_suspend - enter a suspend state
 * @state: the state to enter, one of PIIX4_FUNC3IO_PMCNTRL_SUS_TYP_*
 *
 * Enters a suspend state via the Malta's PIIX4. If the state to be entered
 * is one which loses context (eg. SOFF) then this function will never
 * return.
 */
extern int mips_pm_suspend(unsigned state);

#else /* !CONFIG_MIPS_MALTA_PM */

static inline int mips_pm_suspend(unsigned state)
{
	return -EINVAL;
}

#endif /* !CONFIG_MIPS_MALTA_PM */

#endif /* __ASM_MIPS_MACH_MALTA_PM_H__ */
