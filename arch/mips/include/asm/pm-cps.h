/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_PM_CPS_H__
#define __MIPS_ASM_PM_CPS_H__

/*
 * The CM & CPC can only handle coherence & power control on a per-core basis,
 * thus in an MT system the VPEs within each core are coupled and can only
 * enter or exit states requiring CM or CPC assistance in unison.
 */
#ifdef CONFIG_MIPS_MT
# define coupled_coherence cpu_has_mipsmt
#else
# define coupled_coherence 0
#endif

/* Enumeration of possible PM states */
enum cps_pm_state {
	CPS_PM_NC_WAIT,		/* MIPS wait instruction, non-coherent */
	CPS_PM_CLOCK_GATED,	/* Core clock gated */
	CPS_PM_POWER_GATED,	/* Core power gated */
	CPS_PM_STATE_COUNT,
};

/**
 * cps_pm_support_state - determine whether the system supports a PM state
 * @state: the state to test for support
 *
 * Returns true if the system supports the given state, otherwise false.
 */
extern bool cps_pm_support_state(enum cps_pm_state state);

/**
 * cps_pm_enter_state - enter a PM state
 * @state: the state to enter
 *
 * Enter the given PM state. If coupled_coherence is non-zero then it is
 * expected that this function be called at approximately the same time on
 * each coupled CPU. Returns 0 on successful entry & exit, otherwise -errno.
 */
extern int cps_pm_enter_state(enum cps_pm_state state);

#endif /* __MIPS_ASM_PM_CPS_H__ */
