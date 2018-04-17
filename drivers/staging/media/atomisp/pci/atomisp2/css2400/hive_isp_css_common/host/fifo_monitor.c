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

#include "fifo_monitor.h"

#include <type_support.h>
#include "device_access.h"

#include <hrt/bits.h>

#include "gp_device.h"

#include "assert_support.h"

#ifndef __INLINE_FIFO_MONITOR__
#define STORAGE_CLASS_FIFO_MONITOR_DATA static const
#else
#define STORAGE_CLASS_FIFO_MONITOR_DATA const
#endif /* __INLINE_FIFO_MONITOR__ */

STORAGE_CLASS_FIFO_MONITOR_DATA unsigned int FIFO_SWITCH_ADDR[N_FIFO_SWITCH] = {
	_REG_GP_SWITCH_IF_ADDR,
	_REG_GP_SWITCH_GDC1_ADDR,
	_REG_GP_SWITCH_GDC2_ADDR};

#ifndef __INLINE_FIFO_MONITOR__
#include "fifo_monitor_private.h"
#endif /* __INLINE_FIFO_MONITOR__ */

static inline bool fifo_monitor_status_valid (
	const fifo_monitor_ID_t		ID,
	const unsigned int			reg,
	const unsigned int			port_id);

static inline bool fifo_monitor_status_accept(
	const fifo_monitor_ID_t		ID,
	const unsigned int			reg,
	const unsigned int			port_id);


void fifo_channel_get_state(
	const fifo_monitor_ID_t		ID,
	const fifo_channel_t		channel_id,
	fifo_channel_state_t		*state)
{
	assert(channel_id < N_FIFO_CHANNEL);
	assert(state != NULL);

	switch (channel_id) {
	case FIFO_CHANNEL_ISP0_TO_SP0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_SP); /* ISP_STR_MON_PORT_ISP2SP */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_SP);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_ISP); /* ISP_STR_MON_PORT_SP2ISP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_ISP);
		break;
	case FIFO_CHANNEL_SP0_TO_ISP0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_ISP); /* ISP_STR_MON_PORT_SP2ISP */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_ISP);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_SP); /* ISP_STR_MON_PORT_ISP2SP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_SP);
		break;
	case FIFO_CHANNEL_ISP0_TO_IF0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_PIF_A); /* ISP_STR_MON_PORT_ISP2PIFA */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_PIF_A);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_PIF_A); /* MOD_STR_MON_PORT_CELLS2PIFA */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_PIF_A);
		break;
	case FIFO_CHANNEL_IF0_TO_ISP0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_PIF_A); /* MOD_STR_MON_PORT_PIFA2CELLS */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_PIF_A);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_PIF_A); /* ISP_STR_MON_PORT_PIFA2ISP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_PIF_A);
		break;
	case FIFO_CHANNEL_ISP0_TO_IF1:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_PIF_B); /* ISP_STR_MON_PORT_ISP2PIFA */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_PIF_B);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_PIF_B); /* MOD_STR_MON_PORT_CELLS2PIFB */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_PIF_B);
		break;
	case FIFO_CHANNEL_IF1_TO_ISP0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_PIF_B); /* MOD_STR_MON_PORT_PIFB2CELLS */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_PIF_B);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_PIF_B); /* ISP_STR_MON_PORT_PIFB2ISP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_PIF_B);
		break;
	case FIFO_CHANNEL_ISP0_TO_DMA0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_DMA); /* ISP_STR_MON_PORT_ISP2DMA */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_DMA);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_DMA_FR_ISP); /* MOD_STR_MON_PORT_ISP2DMA */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_DMA_FR_ISP);
		break;
	case FIFO_CHANNEL_DMA0_TO_ISP0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_DMA2ISP); /* MOD_STR_MON_PORT_DMA2ISP */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_DMA2ISP);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_DMA); /* ISP_STR_MON_PORT_DMA2ISP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_DMA);
		break;
	case FIFO_CHANNEL_ISP0_TO_GDC0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_GDC); /* ISP_STR_MON_PORT_ISP2GDC1 */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_GDC);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_GDC); /* MOD_STR_MON_PORT_CELLS2GDC1 */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_GDC);
		break;
	case FIFO_CHANNEL_GDC0_TO_ISP0:
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_GDC); /* MOD_STR_MON_PORT_GDC12CELLS */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_GDC);
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_GDC); /* ISP_STR_MON_PORT_GDC12ISP */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_GDC);
		break;
	case FIFO_CHANNEL_ISP0_TO_GDC1:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_ISP2GDC2);
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_ISP2GDC2);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_CELLS2GDC2);
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_CELLS2GDC2);
		break;
	case FIFO_CHANNEL_GDC1_TO_ISP0:
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_GDC22CELLS);
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_GDC22CELLS);
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_GDC22ISP);
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_GDC22ISP);
		break;
	case FIFO_CHANNEL_ISP0_TO_HOST0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_GPD); /* ISP_STR_MON_PORT_ISP2GPD */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_SND_GPD);
		{
		hrt_data	value = ia_css_device_load_uint32(0x0000000000380014ULL);
		state->fifo_valid  = !_hrt_get_bit(value, 0);
		state->sink_accept = false; /* no monitor connected */
		}
		break;
	case FIFO_CHANNEL_HOST0_TO_ISP0:
		{
		hrt_data	value = ia_css_device_load_uint32(0x000000000038001CULL);
		state->fifo_valid  = false; /* no monitor connected */
		state->sink_accept = !_hrt_get_bit(value, 0);
		}
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_GPD); /* ISP_STR_MON_PORT_FA2ISP */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_ISP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_GPD);
		break;
	case FIFO_CHANNEL_SP0_TO_IF0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_PIF_A); /* SP_STR_MON_PORT_SP2PIFA */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_PIF_A);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_PIF_A); /* MOD_STR_MON_PORT_CELLS2PIFA */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_PIF_A);
		break;
	case FIFO_CHANNEL_IF0_TO_SP0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_PIF_A); /* MOD_STR_MON_PORT_PIFA2CELLS */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_PIF_A);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_PIF_A); /* SP_STR_MON_PORT_PIFA2SP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_PIF_A);
		break;
	case FIFO_CHANNEL_SP0_TO_IF1:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_PIF_B); /* SP_STR_MON_PORT_SP2PIFB */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_PIF_B);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_PIF_B); /* MOD_STR_MON_PORT_CELLS2PIFB */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_PIF_B);
		break;
	case FIFO_CHANNEL_IF1_TO_SP0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_PIF_B); /* MOD_STR_MON_PORT_PIFB2CELLS */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_PIF_B);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_PIF_B); /* SP_STR_MON_PORT_PIFB2SP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			ISP_STR_MON_PORT_RCV_PIF_B);
		break;
	case FIFO_CHANNEL_SP0_TO_IF2:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_SIF); /* SP_STR_MON_PORT_SP2SIF */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_SIF);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_SIF); /* MOD_STR_MON_PORT_SP2SIF */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_SIF);
		break;
	case FIFO_CHANNEL_IF2_TO_SP0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_SIF); /* MOD_STR_MON_PORT_SIF2SP */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_SIF);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_SIF); /* SP_STR_MON_PORT_SIF2SP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_SIF);
		break;
	case FIFO_CHANNEL_SP0_TO_DMA0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_DMA); /* SP_STR_MON_PORT_SP2DMA */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_DMA);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_DMA_FR_SP); /* MOD_STR_MON_PORT_SP2DMA */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_DMA_FR_SP);
		break;
	case FIFO_CHANNEL_DMA0_TO_SP0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_DMA2SP); /* MOD_STR_MON_PORT_DMA2SP */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_DMA2SP);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_DMA); /* SP_STR_MON_PORT_DMA2SP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_DMA);
		break;
	case FIFO_CHANNEL_SP0_TO_GDC0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_B_IDX,
			SP_STR_MON_PORT_B_SP2GDC1);
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_B_IDX,
			SP_STR_MON_PORT_B_SP2GDC1);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_CELLS2GDC1);
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_CELLS2GDC1);
		break;
	case FIFO_CHANNEL_GDC0_TO_SP0:
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_GDC12CELLS);
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_GDC12CELLS);
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_B_IDX,
			SP_STR_MON_PORT_B_GDC12SP);
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_B_IDX,
			SP_STR_MON_PORT_B_GDC12SP);
		break;
	case FIFO_CHANNEL_SP0_TO_GDC1:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_B_IDX,
			SP_STR_MON_PORT_B_SP2GDC2);
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_B_IDX,
			SP_STR_MON_PORT_B_SP2GDC2);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_CELLS2GDC2);
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_CELLS2GDC2);
		break;
	case FIFO_CHANNEL_GDC1_TO_SP0:
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_GDC22CELLS);
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_GDC22CELLS);
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_B_IDX,
			SP_STR_MON_PORT_B_GDC22SP);
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_B_IDX,
			SP_STR_MON_PORT_B_GDC22SP);
		break;
	case FIFO_CHANNEL_SP0_TO_HOST0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_GPD); /* SP_STR_MON_PORT_SP2GPD */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_GPD);
		{
		hrt_data	value = ia_css_device_load_uint32(0x0000000000380010ULL);
		state->fifo_valid  = !_hrt_get_bit(value, 0);
		state->sink_accept = false; /* no monitor connected */
		}
		break;
	case FIFO_CHANNEL_HOST0_TO_SP0:
		{
		hrt_data	value = ia_css_device_load_uint32(0x0000000000380018ULL);
		state->fifo_valid  = false; /* no monitor connected */
		state->sink_accept = !_hrt_get_bit(value, 0);
		}
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_GPD); /* SP_STR_MON_PORT_FA2SP */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_GPD);
		break;
	case FIFO_CHANNEL_SP0_TO_STREAM2MEM0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_MC); /* SP_STR_MON_PORT_SP2MC */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SND_MC);
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_MC); /* MOD_STR_MON_PORT_SP2MC */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_RCV_MC);
		break;
	case FIFO_CHANNEL_STREAM2MEM0_TO_SP0:
		state->fifo_valid  = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_MC); /* SP_STR_MON_PORT_MC2SP */
		state->sink_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_MOD_STREAM_STAT_IDX,
			MOD_STR_MON_PORT_SND_MC);
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_MC); /* MOD_STR_MON_PORT_MC2SP */
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_RCV_MC);
		break;
	case FIFO_CHANNEL_SP0_TO_INPUT_SYSTEM0:
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SP2ISYS);
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_SP2ISYS);
		state->fifo_valid  = false;
		state->sink_accept = false;
		break;
	case FIFO_CHANNEL_INPUT_SYSTEM0_TO_SP0:
		state->fifo_valid  = false;
		state->sink_accept = false;
		state->src_valid   = fifo_monitor_status_valid(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_ISYS2SP);
		state->fifo_accept = fifo_monitor_status_accept(ID,
			HIVE_GP_REGS_SP_STREAM_STAT_IDX,
			SP_STR_MON_PORT_ISYS2SP);
		break;
	default:
		assert(0);
		break;
	}

	return;
}

void fifo_switch_get_state(
	const fifo_monitor_ID_t		ID,
	const fifo_switch_t			switch_id,
	fifo_switch_state_t			*state)
{
	hrt_data		data = (hrt_data)-1;

	assert(ID == FIFO_MONITOR0_ID);
	assert(switch_id < N_FIFO_SWITCH);
	assert(state != NULL);

	(void)ID;

	data = gp_device_reg_load(GP_DEVICE0_ID, FIFO_SWITCH_ADDR[switch_id]);

	state->is_none = (data == HIVE_ISP_CSS_STREAM_SWITCH_NONE);
	state->is_sp = (data == HIVE_ISP_CSS_STREAM_SWITCH_SP);
	state->is_isp = (data == HIVE_ISP_CSS_STREAM_SWITCH_ISP);

	return;
}

void fifo_monitor_get_state(
	const fifo_monitor_ID_t		ID,
	fifo_monitor_state_t		*state)
{
	fifo_channel_t	ch_id;
	fifo_switch_t	sw_id;

	assert(ID < N_FIFO_MONITOR_ID);
	assert(state != NULL);

	for (ch_id = 0; ch_id < N_FIFO_CHANNEL; ch_id++) {
		fifo_channel_get_state(ID, ch_id,
			&(state->fifo_channels[ch_id]));
	}

	for (sw_id = 0; sw_id < N_FIFO_SWITCH; sw_id++) {
		fifo_switch_get_state(ID, sw_id,
			&(state->fifo_switches[sw_id]));
	}
	return;
}

static inline bool fifo_monitor_status_valid (
	const fifo_monitor_ID_t		ID,
	const unsigned int			reg,
	const unsigned int			port_id)
{
	hrt_data	data = fifo_monitor_reg_load(ID, reg);

	return (data >> (((port_id * 2) + _hive_str_mon_valid_offset))) & 0x1;
}

static inline bool fifo_monitor_status_accept(
	const fifo_monitor_ID_t		ID,
	const unsigned int			reg,
	const unsigned int			port_id)
{
	hrt_data	data = fifo_monitor_reg_load(ID, reg);

	return (data >> (((port_id * 2) + _hive_str_mon_accept_offset))) & 0x1;
}
