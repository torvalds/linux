/*
 * Copyright 2011-2012, Meador Inge, Mentor Graphics Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#ifndef _ASM_MPIC_MSGR_H
#define _ASM_MPIC_MSGR_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/smp.h>

struct mpic_msgr {
	u32 __iomem *base;
	u32 __iomem *mer;
	int irq;
	unsigned char in_use;
	raw_spinlock_t lock;
	int num;
};

/* Get a message register
 *
 * @reg_num:	the MPIC message register to get
 *
 * A pointer to the message register is returned.  If
 * the message register asked for is already in use, then
 * EBUSY is returned.  If the number given is not associated
 * with an actual message register, then ENODEV is returned.
 * Successfully getting the register marks it as in use.
 */
extern struct mpic_msgr *mpic_msgr_get(unsigned int reg_num);

/* Relinquish a message register
 *
 * @msgr:	the message register to return
 *
 * Disables the given message register and marks it as free.
 * After this call has completed successully the message
 * register is available to be acquired by a call to
 * mpic_msgr_get.
 */
extern void mpic_msgr_put(struct mpic_msgr *msgr);

/* Enable a message register
 *
 * @msgr:	the message register to enable
 *
 * The given message register is enabled for sending
 * messages.
 */
extern void mpic_msgr_enable(struct mpic_msgr *msgr);

/* Disable a message register
 *
 * @msgr:	the message register to disable
 *
 * The given message register is disabled for sending
 * messages.
 */
extern void mpic_msgr_disable(struct mpic_msgr *msgr);

/* Write a message to a message register
 *
 * @msgr:	the message register to write to
 * @message:	the message to write
 *
 * The given 32-bit message is written to the given message
 * register.  Writing to an enabled message registers fires
 * an interrupt.
 */
static inline void mpic_msgr_write(struct mpic_msgr *msgr, u32 message)
{
	out_be32(msgr->base, message);
}

/* Read a message from a message register
 *
 * @msgr:	the message register to read from
 *
 * Returns the 32-bit value currently in the given message register.
 * Upon reading the register any interrupts for that register are
 * cleared.
 */
static inline u32 mpic_msgr_read(struct mpic_msgr *msgr)
{
	return in_be32(msgr->base);
}

/* Clear a message register
 *
 * @msgr:	the message register to clear
 *
 * Clears any interrupts associated with the given message register.
 */
static inline void mpic_msgr_clear(struct mpic_msgr *msgr)
{
	(void) mpic_msgr_read(msgr);
}

/* Set the destination CPU for the message register
 *
 * @msgr:	the message register whose destination is to be set
 * @cpu_num:	the Linux CPU number to bind the message register to
 *
 * Note that the CPU number given is the CPU number used by the kernel
 * and *not* the actual hardware CPU number.
 */
static inline void mpic_msgr_set_destination(struct mpic_msgr *msgr,
					     u32 cpu_num)
{
	out_be32(msgr->base, 1 << get_hard_smp_processor_id(cpu_num));
}

/* Get the IRQ number for the message register
 * @msgr:	the message register whose IRQ is to be returned
 *
 * Returns the IRQ number associated with the given message register.
 * NO_IRQ is returned if this message register is not capable of
 * receiving interrupts.  What message register can and cannot receive
 * interrupts is specified in the device tree for the system.
 */
static inline int mpic_msgr_get_irq(struct mpic_msgr *msgr)
{
	return msgr->irq;
}

#endif
