/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright IBM Corp. 2014
 *
 *  Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _S390_IDLE_H
#define _S390_IDLE_H

#include <linux/types.h>
#include <linux/device.h>

struct s390_idle_data {
	unsigned long idle_count;
	unsigned long idle_time;
	unsigned long clock_idle_enter;
	unsigned long timer_idle_enter;
	unsigned long mt_cycles_enter[8];
};

DECLARE_PER_CPU(struct s390_idle_data, s390_idle);

extern struct device_attribute dev_attr_idle_count;
extern struct device_attribute dev_attr_idle_time_us;

#endif /* _S390_IDLE_H */
