// SPDX-License-Identifier: GPL-2.0
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

#include <linux/delay.h>

#include <system_global.h>
#include "isp.h"

#ifndef __INLINE_ISP__
#include "isp_private.h"
#endif /* __INLINE_ISP__ */

#include "assert_support.h"

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

/* ISP functions to control the ISP state from the host, even in crun. */

/* Inspect readiness of an ISP indexed by ID */
unsigned int isp_is_ready(isp_ID_t ID)
{
	assert(ID < N_ISP_ID);
	return isp_ctrl_getbit(ID, ISP_SC_REG, ISP_IDLE_BIT);
}

/* Inspect sleeping of an ISP indexed by ID */
unsigned int isp_is_sleeping(isp_ID_t ID)
{
	assert(ID < N_ISP_ID);
	return isp_ctrl_getbit(ID, ISP_SC_REG, ISP_SLEEPING_BIT);
}

/* To be called by the host immediately before starting ISP ID. */
void isp_start(isp_ID_t ID)
{
	assert(ID < N_ISP_ID);
}

/* Wake up ISP ID. */
void isp_wake(isp_ID_t ID)
{
	assert(ID < N_ISP_ID);
	isp_ctrl_setbit(ID, ISP_SC_REG, ISP_START_BIT);
	udelay(1);
}
