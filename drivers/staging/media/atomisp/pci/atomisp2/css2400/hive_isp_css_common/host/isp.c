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

#include <system_global.h>
#include "isp.h"

#ifndef __INLINE_ISP__
#include "isp_private.h"
#endif /* __INLINE_ISP__ */

#include "assert_support.h"
#include "platform_support.h"			/* hrt_sleep() */

void cnd_isp_irq_enable(
	const isp_ID_t		ID,
	const bool		cnd)
{
	if (cnd) {
		isp_ctrl_setbit(ID, ISP_IRQ_READY_REG, ISP_IRQ_READY_BIT);
/* Enabling the IRQ immediately triggers an interrupt, clear it */
		isp_ctrl_setbit(ID, ISP_IRQ_CLEAR_REG, ISP_IRQ_CLEAR_BIT);
	} else {
		isp_ctrl_clearbit(ID, ISP_IRQ_READY_REG,
			ISP_IRQ_READY_BIT);
	}
	return;
}

void isp_get_state(
	const isp_ID_t		ID,
	isp_state_t			*state,
	isp_stall_t			*stall)
{
	hrt_data sc = isp_ctrl_load(ID, ISP_SC_REG);

	assert(state != NULL);
	assert(stall != NULL);

#if defined(_hrt_sysmem_ident_address)
	/* Patch to avoid compiler unused symbol warning in C_RUN build */
	(void)__hrt_sysmem_ident_address;
	(void)_hrt_sysmem_map_var;
#endif

	state->pc = isp_ctrl_load(ID, ISP_PC_REG);
	state->status_register = sc;
	state->is_broken = isp_ctrl_getbit(ID, ISP_SC_REG, ISP_BROKEN_BIT);
	state->is_idle = isp_ctrl_getbit(ID, ISP_SC_REG, ISP_IDLE_BIT);
	state->is_sleeping = isp_ctrl_getbit(ID, ISP_SC_REG, ISP_SLEEPING_BIT);
	state->is_stalling = isp_ctrl_getbit(ID, ISP_SC_REG, ISP_STALLING_BIT);
	stall->stat_ctrl =
		!isp_ctrl_getbit(ID, ISP_CTRL_SINK_REG, ISP_CTRL_SINK_BIT);
	stall->pmem =
		!isp_ctrl_getbit(ID, ISP_PMEM_SINK_REG, ISP_PMEM_SINK_BIT);
	stall->dmem =
		!isp_ctrl_getbit(ID, ISP_DMEM_SINK_REG, ISP_DMEM_SINK_BIT);
	stall->vmem =
		!isp_ctrl_getbit(ID, ISP_VMEM_SINK_REG, ISP_VMEM_SINK_BIT);
	stall->fifo0 =
		!isp_ctrl_getbit(ID, ISP_FIFO0_SINK_REG, ISP_FIFO0_SINK_BIT);
	stall->fifo1 =
		!isp_ctrl_getbit(ID, ISP_FIFO1_SINK_REG, ISP_FIFO1_SINK_BIT);
	stall->fifo2 =
		!isp_ctrl_getbit(ID, ISP_FIFO2_SINK_REG, ISP_FIFO2_SINK_BIT);
	stall->fifo3 =
		!isp_ctrl_getbit(ID, ISP_FIFO3_SINK_REG, ISP_FIFO3_SINK_BIT);
	stall->fifo4 =
		!isp_ctrl_getbit(ID, ISP_FIFO4_SINK_REG, ISP_FIFO4_SINK_BIT);
	stall->fifo5 =
		!isp_ctrl_getbit(ID, ISP_FIFO5_SINK_REG, ISP_FIFO5_SINK_BIT);
	stall->fifo6 =
		!isp_ctrl_getbit(ID, ISP_FIFO6_SINK_REG, ISP_FIFO6_SINK_BIT);
	stall->vamem1 =
		!isp_ctrl_getbit(ID, ISP_VAMEM1_SINK_REG, ISP_VAMEM1_SINK_BIT);
	stall->vamem2 =
		!isp_ctrl_getbit(ID, ISP_VAMEM2_SINK_REG, ISP_VAMEM2_SINK_BIT);
	stall->vamem3 =
		!isp_ctrl_getbit(ID, ISP_VAMEM3_SINK_REG, ISP_VAMEM3_SINK_BIT);
	stall->hmem =
		!isp_ctrl_getbit(ID, ISP_HMEM_SINK_REG, ISP_HMEM_SINK_BIT);
/*
	stall->icache_master =
		!isp_ctrl_getbit(ID, ISP_ICACHE_MT_SINK_REG,
			ISP_ICACHE_MT_SINK_BIT);
 */
	return;
}

/* ISP functions to control the ISP state from the host, even in crun. */

/* Inspect readiness of an ISP indexed by ID */
unsigned isp_is_ready(isp_ID_t ID)
{
	assert (ID < N_ISP_ID);
	return isp_ctrl_getbit(ID, ISP_SC_REG, ISP_IDLE_BIT);
}

/* Inspect sleeping of an ISP indexed by ID */
unsigned isp_is_sleeping(isp_ID_t ID)
{
	assert (ID < N_ISP_ID);
	return isp_ctrl_getbit(ID, ISP_SC_REG, ISP_SLEEPING_BIT);
}

/* To be called by the host immediately before starting ISP ID. */
void isp_start(isp_ID_t ID)
{
	assert (ID < N_ISP_ID);
}

/* Wake up ISP ID. */
void isp_wake(isp_ID_t ID)
{
	assert (ID < N_ISP_ID);
	isp_ctrl_setbit(ID, ISP_SC_REG, ISP_START_BIT);
	hrt_sleep();
}

