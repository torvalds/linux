// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  cx18 interrupt handling
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@kernel.org>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-irq.h"
#include "cx18-mailbox.h"
#include "cx18-scb.h"

static void xpu_ack(struct cx18 *cx, u32 sw2)
{
	if (sw2 & IRQ_CPU_TO_EPU_ACK)
		wake_up(&cx->mb_cpu_waitq);
	if (sw2 & IRQ_APU_TO_EPU_ACK)
		wake_up(&cx->mb_apu_waitq);
}

static void epu_cmd(struct cx18 *cx, u32 sw1)
{
	if (sw1 & IRQ_CPU_TO_EPU)
		cx18_api_epu_cmd_irq(cx, CPU);
	if (sw1 & IRQ_APU_TO_EPU)
		cx18_api_epu_cmd_irq(cx, APU);
}

irqreturn_t cx18_irq_handler(int irq, void *dev_id)
{
	struct cx18 *cx = dev_id;
	u32 sw1, sw2, hw2;

	sw1 = cx18_read_reg(cx, SW1_INT_STATUS) & cx->sw1_irq_mask;
	sw2 = cx18_read_reg(cx, SW2_INT_STATUS) & cx->sw2_irq_mask;
	hw2 = cx18_read_reg(cx, HW2_INT_CLR_STATUS) & cx->hw2_irq_mask;

	if (sw1)
		cx18_write_reg_expect(cx, sw1, SW1_INT_STATUS, ~sw1, sw1);
	if (sw2)
		cx18_write_reg_expect(cx, sw2, SW2_INT_STATUS, ~sw2, sw2);
	if (hw2)
		cx18_write_reg_expect(cx, hw2, HW2_INT_CLR_STATUS, ~hw2, hw2);

	if (sw1 || sw2 || hw2)
		CX18_DEBUG_HI_IRQ("received interrupts SW1: %x	SW2: %x  HW2: %x\n",
				  sw1, sw2, hw2);

	/*
	 * SW1 responses have to happen first.  The sending XPU times out the
	 * incoming mailboxes on us rather rapidly.
	 */
	if (sw1)
		epu_cmd(cx, sw1);

	/* To do: interrupt-based I2C handling
	if (hw2 & (HW2_I2C1_INT|HW2_I2C2_INT)) {
	}
	*/

	if (sw2)
		xpu_ack(cx, sw2);

	return (sw1 || sw2 || hw2) ? IRQ_HANDLED : IRQ_NONE;
}
