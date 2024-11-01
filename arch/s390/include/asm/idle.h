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
#include <linux/seqlock.h>

struct s390_idle_data {
	seqcount_t seqcount;
	unsigned long idle_count;
	unsigned long idle_time;
	unsigned long clock_idle_enter;
	unsigned long clock_idle_exit;
	unsigned long timer_idle_enter;
	unsigned long timer_idle_exit;
	unsigned long mt_cycles_enter[8];
};

extern struct device_attribute dev_attr_idle_count;
extern struct device_attribute dev_attr_idle_time_us;

void psw_idle(struct s390_idle_data *data, unsigned long psw_mask);
void psw_idle_exit(void);

#endif /* _S390_IDLE_H */
