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
	unsigned long long idle_count;
	unsigned long long idle_time;
	unsigned long long clock_idle_enter;
	unsigned long long clock_idle_exit;
	unsigned long long timer_idle_enter;
	unsigned long long timer_idle_exit;
};

extern struct device_attribute dev_attr_idle_count;
extern struct device_attribute dev_attr_idle_time_us;

void psw_idle(struct s390_idle_data *, unsigned long);

#endif /* _S390_IDLE_H */
