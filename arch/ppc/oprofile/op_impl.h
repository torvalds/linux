/*
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * Based on alpha version.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef OP_IMPL_H
#define OP_IMPL_H 1

#define OP_MAX_COUNTER 8

/* Per-counter configuration as set via oprofilefs.  */
struct op_counter_config {
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
struct op_ppc32_model {
	void (*reg_setup) (struct op_counter_config *,
			   struct op_system_config *,
			   int num_counters);
	void (*start) (struct op_counter_config *);
	void (*stop) (void);
	void (*handle_interrupt) (struct pt_regs *,
				  struct op_counter_config *);
	int num_counters;
};

#endif /* OP_IMPL_H */
