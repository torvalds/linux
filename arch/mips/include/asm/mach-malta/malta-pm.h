/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
