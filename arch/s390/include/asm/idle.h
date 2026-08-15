/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright IBM Corp. 2014
 *
 *  Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _S390_IDLE_H
#define _S390_IDLE_H

#include <linux/percpu-defs.h>
#include <linux/types.h>
#include <asm/tod_types.h>

struct s390_idle_data {
#ifdef CONFIG_NO_HZ_COMMON
	bool	      in_idle;
#endif
	unsigned long timer_idle_enter;
	unsigned long mt_cycles_enter[8];
	union tod_clock clock_idle_enter;
	union tod_clock clock_idle_exit;
};

DECLARE_PER_CPU(struct s390_idle_data, s390_idle);

#endif /* _S390_IDLE_H */
