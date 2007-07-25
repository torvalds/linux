/*
 * File:         arch/blackfin/oprofile/op_blackfin.h
 * Based on:
 * Author:       Anton Blanchard <anton@au.ibm.com>
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef OP_BLACKFIN_H
#define OP_BLACKFIN_H 1

#define OP_MAX_COUNTER 2

#include <asm/blackfin.h>

/* Per-counter configuration as set via oprofilefs.  */
struct op_counter_config {
	unsigned long valid;
	unsigned long enabled;
	unsigned long event;
	unsigned long count;
	unsigned long kernel;
	unsigned long user;
	unsigned long unit_mask;
};

/* System-wide configuration as set via oprofilefs.  */
struct op_system_config {
	unsigned long enable_kernel;
	unsigned long enable_user;
};

/* Per-arch configuration */
struct op_bfin533_model {
	int (*reg_setup) (struct op_counter_config *);
	int (*start) (struct op_counter_config *);
	void (*stop) (void);
	int num_counters;
	char *name;
};

extern struct op_bfin533_model op_model_bfin533;

static inline unsigned int ctr_read(void)
{
	unsigned int tmp;

	tmp = bfin_read_PFCTL();
	CSYNC();

	return tmp;
}

static inline void ctr_write(unsigned int val)
{
	bfin_write_PFCTL(val);
	CSYNC();
}

static inline void count_read(unsigned int *count)
{
	count[0] = bfin_read_PFCNTR0();
	count[1] = bfin_read_PFCNTR1();
	CSYNC();
}

static inline void count_write(unsigned int *count)
{
	bfin_write_PFCNTR0(count[0]);
	bfin_write_PFCNTR1(count[1]);
	CSYNC();
}

extern int pm_overflow_handler(int irq, struct pt_regs *regs);

#endif
