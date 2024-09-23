// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
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
