/*
 * arch/arm/plat-spear/include/plat/system.h
 *
 * SPEAr platform specific architecture functions
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_SYSTEM_H
#define __PLAT_SYSTEM_H

static inline void arch_idle(void)
{
	/*
	 * This should do all the clock switching
	 * and wait for interrupt tricks
	 */
	cpu_do_idle();
}

#endif /* __PLAT_SYSTEM_H */
