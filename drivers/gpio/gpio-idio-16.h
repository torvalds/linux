/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2022 William Breathitt Gray */
#ifndef _IDIO_16_H_
#define _IDIO_16_H_

#include <linux/spinlock.h>
#include <linux/types.h>

/**
 * struct idio_16 - IDIO-16 registers structure
 * @out0_7:	Read: FET Drive Outputs 0-7
 *		Write: FET Drive Outputs 0-7
 * @in0_7:	Read: Isolated Inputs 0-7
 *		Write: Clear Interrupt
 * @irq_ctl:	Read: Enable IRQ
 *		Write: Disable IRQ
 * @filter_ctl:	Read: Activate Input Filters 0-15
 *		Write: Deactivate Input Filters 0-15
 * @out8_15:	Read: FET Drive Outputs 8-15
 *		Write: FET Drive Outputs 8-15
 * @in8_15:	Read: Isolated Inputs 8-15
 *		Write: Unused
 * @irq_status:	Read: Interrupt status
 *		Write: Unused
 */
struct idio_16 {
	u8 out0_7;
	u8 in0_7;
	u8 irq_ctl;
	u8 filter_ctl;
	u8 out8_15;
	u8 in8_15;
	u8 irq_status;
};

#define IDIO_16_NOUT 16

/**
 * struct idio_16_state - IDIO-16 state structure
 * @lock:	synchronization lock for accessing device state
 * @out_state:	output signals state
 */
struct idio_16_state {
	spinlock_t lock;
	DECLARE_BITMAP(out_state, IDIO_16_NOUT);
};

/**
 * idio_16_get_direction - get the I/O direction for a signal offset
 * @offset:	offset of signal to get direction
 *
 * Returns the signal direction (0=output, 1=input) for the signal at @offset.
 */
static inline int idio_16_get_direction(const unsigned long offset)
{
	return (offset >= IDIO_16_NOUT) ? 1 : 0;
}

int idio_16_get(struct idio_16 __iomem *reg, struct idio_16_state *state,
		unsigned long offset);
void idio_16_get_multiple(struct idio_16 __iomem *reg,
			  struct idio_16_state *state,
			  const unsigned long *mask, unsigned long *bits);
void idio_16_set(struct idio_16 __iomem *reg, struct idio_16_state *state,
		 unsigned long offset, unsigned long value);
void idio_16_set_multiple(struct idio_16 __iomem *reg,
			  struct idio_16_state *state,
			  const unsigned long *mask, const unsigned long *bits);
void idio_16_state_init(struct idio_16_state *state);

#endif /* _IDIO_16_H_ */
