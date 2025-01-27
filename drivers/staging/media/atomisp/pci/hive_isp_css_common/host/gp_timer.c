// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
 */

#include <type_support.h> /*uint32_t */
#include "gp_timer.h"   /*system_local.h,
			  gp_timer_public.h*/

#ifndef __INLINE_GP_TIMER__
#include "gp_timer_private.h"  /*device_access.h*/
#endif /* __INLINE_GP_TIMER__ */
#include "system_local.h"

/* FIXME: not sure if reg_load(), reg_store() should be API.
 */
static uint32_t
gp_timer_reg_load(uint32_t reg);

static void
gp_timer_reg_store(u32 reg, uint32_t value);

static uint32_t
gp_timer_reg_load(uint32_t reg)
{
	return ia_css_device_load_uint32(
		   GP_TIMER_BASE +
		   (reg * sizeof(uint32_t)));
}

static void
gp_timer_reg_store(u32 reg, uint32_t value)
{
	ia_css_device_store_uint32((GP_TIMER_BASE +
				    (reg * sizeof(uint32_t))),
				   value);
}

void gp_timer_init(gp_timer_ID_t ID)
{
	/* set_overall_enable*/
	gp_timer_reg_store(_REG_GP_TIMER_OVERALL_ENABLE, 1);

	/*set enable*/
	gp_timer_reg_store(_REG_GP_TIMER_ENABLE_ID(ID), 1);

	/* set signal select */
	gp_timer_reg_store(_REG_GP_TIMER_SIGNAL_SELECT_ID(ID), GP_TIMER_SIGNAL_SELECT);

	/*set count type */
	gp_timer_reg_store(_REG_GP_TIMER_COUNT_TYPE_ID(ID), GP_TIMER_COUNT_TYPE_LOW);

	/*reset gp timer */
	gp_timer_reg_store(_REG_GP_TIMER_RESET_REG, 0xFF);
}

uint32_t
gp_timer_read(gp_timer_ID_t ID)
{
	return	gp_timer_reg_load(_REG_GP_TIMER_VALUE_ID(ID));
}
