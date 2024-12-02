/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Common time accounting prototypes and such for all ppc machines.
 */

#ifndef __POWERPC_ACCOUNTING_H
#define __POWERPC_ACCOUNTING_H

/* Stuff for accurate time accounting */
struct cpu_accounting_data {
	/* Accumulated cputime values to flush on ticks*/
	unsigned long utime;
	unsigned long stime;
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
	unsigned long utime_scaled;
	unsigned long stime_scaled;
#endif
	unsigned long gtime;
	unsigned long hardirq_time;
	unsigned long softirq_time;
	unsigned long steal_time;
	unsigned long idle_time;
	/* Internal counters */
	unsigned long starttime;	/* TB value snapshot */
	unsigned long starttime_user;	/* TB value on exit to usermode */
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
	unsigned long startspurr;	/* SPURR value snapshot */
	unsigned long utime_sspurr;	/* ->user_time when ->startspurr set */
#endif
};

#endif
