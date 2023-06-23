/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2022 William Breathitt Gray */
#ifndef _I8255_H_
#define _I8255_H_

#include <linux/spinlock.h>
#include <linux/types.h>

/**
 * struct i8255 - Intel 8255 register structure
 * @port:	Port A, B, and C
 * @control:	Control register
 */
struct i8255 {
	u8 port[3];
	u8 control;
};

/**
 * struct i8255_state - Intel 8255 state structure
 * @lock:		synchronization lock for accessing device state
 * @control_state:	Control register state
 */
struct i8255_state {
	spinlock_t lock;
	u8 control_state;
};

void i8255_direction_input(struct i8255 __iomem *ppi, struct i8255_state *state,
			   unsigned long offset);
void i8255_direction_output(struct i8255 __iomem *ppi,
			    struct i8255_state *state, unsigned long offset,
			    unsigned long value);
int i8255_get(struct i8255 __iomem *ppi, unsigned long offset);
int i8255_get_direction(const struct i8255_state *state, unsigned long offset);
void i8255_get_multiple(struct i8255 __iomem *ppi, const unsigned long *mask,
			unsigned long *bits, unsigned long ngpio);
void i8255_mode0_output(struct i8255 __iomem *const ppi);
void i8255_set(struct i8255 __iomem *ppi, struct i8255_state *state,
	       unsigned long offset, unsigned long value);
void i8255_set_multiple(struct i8255 __iomem *ppi, struct i8255_state *state,
			const unsigned long *mask, const unsigned long *bits,
			unsigned long ngpio);
void i8255_state_init(struct i8255_state *const state, unsigned long nbanks);

#endif /* _I8255_H_ */
