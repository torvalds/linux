/*
 * Common time accounting prototypes and such for all ppc machines.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __POWERPC_ACCOUNTING_H
#define __POWERPC_ACCOUNTING_H

/* Stuff for accurate time accounting */
struct cpu_accounting_data {
	/* Accumulated cputime values to flush on ticks*/
	unsigned long utime;
	unsigned long stime;
	unsigned long utime_scaled;
	unsigned long stime_scaled;
	unsigned long gtime;
	unsigned long hardirq_time;
	unsigned long softirq_time;
	unsigned long steal_time;
	unsigned long idle_time;
	/* Internal counters */
	unsigned long starttime;	/* TB value snapshot */
	unsigned long starttime_user;	/* TB value on exit to usermode */
	unsigned long startspurr;	/* SPURR value snapshot */
	unsigned long utime_sspurr;	/* ->user_time when ->startspurr set */
};

#endif
