/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "sp.h"

#ifndef __INLINE_SP__
#include "sp_private.h"
#endif /* __INLINE_SP__ */

#include "assert_support.h"

void cnd_sp_irq_enable(
	const sp_ID_t		ID,
	const bool		cnd)
{
	if (cnd) {
		sp_ctrl_setbit(ID, SP_IRQ_READY_REG, SP_IRQ_READY_BIT);
/* Enabling the IRQ immediately triggers an interrupt, clear it */
		sp_ctrl_setbit(ID, SP_IRQ_CLEAR_REG, SP_IRQ_CLEAR_BIT);
	} else {
		sp_ctrl_clearbit(ID, SP_IRQ_READY_REG, SP_IRQ_READY_BIT);
	}
}

void sp_get_state(
	const sp_ID_t			ID,
	sp_state_t				*state,
	sp_stall_t				*stall)
{
	hrt_data sc = sp_ctrl_load(ID, SP_SC_REG);

	assert(state != NULL);
	assert(stall != NULL);

	state->pc = sp_ctrl_load(ID, SP_PC_REG);
	state->status_register = sc;
	state->is_broken   = (sc & (1U << SP_BROKEN_BIT)) != 0;
	state->is_idle     = (sc & (1U << SP_IDLE_BIT)) != 0;
	state->is_sleeping = (sc & (1U << SP_SLEEPING_BIT)) != 0;
	state->is_stalling = (sc & (1U << SP_STALLING_BIT)) != 0;
	stall->fifo0 =
		!sp_ctrl_getbit(ID, SP_FIFO0_SINK_REG, SP_FIFO0_SINK_BIT);
	stall->fifo1 =
		!sp_ctrl_getbit(ID, SP_FIFO1_SINK_REG, SP_FIFO1_SINK_BIT);
	stall->fifo2 =
		!sp_ctrl_getbit(ID, SP_FIFO2_SINK_REG, SP_FIFO2_SINK_BIT);
	stall->fifo3 =
		!sp_ctrl_getbit(ID, SP_FIFO3_SINK_REG, SP_FIFO3_SINK_BIT);
	stall->fifo4 =
		!sp_ctrl_getbit(ID, SP_FIFO4_SINK_REG, SP_FIFO4_SINK_BIT);
	stall->fifo5 =
		!sp_ctrl_getbit(ID, SP_FIFO5_SINK_REG, SP_FIFO5_SINK_BIT);
	stall->fifo6 =
		!sp_ctrl_getbit(ID, SP_FIFO6_SINK_REG, SP_FIFO6_SINK_BIT);
	stall->fifo7 =
		!sp_ctrl_getbit(ID, SP_FIFO7_SINK_REG, SP_FIFO7_SINK_BIT);
	stall->fifo8 =
		!sp_ctrl_getbit(ID, SP_FIFO8_SINK_REG, SP_FIFO8_SINK_BIT);
	stall->fifo9 =
		!sp_ctrl_getbit(ID, SP_FIFO9_SINK_REG, SP_FIFO9_SINK_BIT);
	stall->fifoa =
		!sp_ctrl_getbit(ID, SP_FIFOA_SINK_REG, SP_FIFOA_SINK_BIT);
	stall->dmem =
		!sp_ctrl_getbit(ID, SP_DMEM_SINK_REG, SP_DMEM_SINK_BIT);
	stall->control_master =
		!sp_ctrl_getbit(ID, SP_CTRL_MT_SINK_REG, SP_CTRL_MT_SINK_BIT);
	stall->icache_master =
		!sp_ctrl_getbit(ID, SP_ICACHE_MT_SINK_REG,
			SP_ICACHE_MT_SINK_BIT);
}
