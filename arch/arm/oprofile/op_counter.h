/**
 * @file op_counter.h
 *
 * @remark Copyright 2004 Oprofile Authors
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo
 */

#ifndef OP_COUNTER_H
#define OP_COUNTER_H

#define OP_MAX_COUNTER 5

/* Per performance monitor configuration as set via
 * oprofilefs.
 */
struct op_counter_config {
	unsigned long count;
	unsigned long enabled;
	unsigned long event;
	unsigned long unit_mask;
	unsigned long kernel;
	unsigned long user;
};

extern struct op_counter_config counter_config[];

#endif /* OP_COUNTER_H */
