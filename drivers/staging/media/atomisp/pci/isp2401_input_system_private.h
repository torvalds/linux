/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
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

#ifndef __INPUT_SYSTEM_2401_PRIVATE_H_INCLUDED__
#define __INPUT_SYSTEM_2401_PRIVATE_H_INCLUDED__

#include "input_system_public.h"

#include "device_access.h"	/* ia_css_device_load_uint32 */

#include "assert_support.h" /* assert */
#include "print_support.h" /* print */

/* Load the register value */
static inline hrt_data ibuf_ctrl_reg_load(const ibuf_ctrl_ID_t ID,
					  const hrt_address reg)
{
	assert(ID < N_IBUF_CTRL_ID);
	assert(IBUF_CTRL_BASE[ID] != (hrt_address)-1);
	return ia_css_device_load_uint32(IBUF_CTRL_BASE[ID] + reg * sizeof(hrt_data));
}

/* Store a value to the register */
static inline void ibuf_ctrl_reg_store(const ibuf_ctrl_ID_t ID,
				       const hrt_address reg,
				       const hrt_data value)
{
	assert(ID < N_IBUF_CTRL_ID);
	assert(IBUF_CTRL_BASE[ID] != (hrt_address)-1);

	ia_css_device_store_uint32(IBUF_CTRL_BASE[ID] + reg * sizeof(hrt_data), value);
}

/* Get the state of the ibuf-controller process */
static inline void ibuf_ctrl_get_proc_state(const ibuf_ctrl_ID_t ID,
					    const u32 proc_id,
					    ibuf_ctrl_proc_state_t *state)
{
	hrt_address reg_bank_offset;

	reg_bank_offset =
	    _IBUF_CNTRL_PROC_REG_ALIGN * (1 + proc_id);

	state->num_items =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_NUM_ITEMS_PER_STORE);

	state->num_stores =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_NUM_STORES_PER_FRAME);

	state->dma_channel =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_DMA_CHANNEL);

	state->dma_command =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_DMA_CMD);

	state->ibuf_st_addr =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_BUFFER_START_ADDRESS);

	state->ibuf_stride =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_BUFFER_STRIDE);

	state->ibuf_end_addr =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_BUFFER_END_ADDRESS);

	state->dest_st_addr =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_DEST_START_ADDRESS);

	state->dest_stride =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_DEST_STRIDE);

	state->dest_end_addr =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_DEST_END_ADDRESS);

	state->sync_frame =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_SYNC_FRAME);

	state->sync_command =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_STR2MMIO_SYNC_CMD);

	state->store_command =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_STR2MMIO_STORE_CMD);

	state->shift_returned_items =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_SHIFT_ITEMS);

	state->elems_ibuf =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_ELEMS_P_WORD_IBUF);

	state->elems_dest =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_ELEMS_P_WORD_DEST);

	state->cur_stores =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_CUR_STORES);

	state->cur_acks =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_CUR_ACKS);

	state->cur_s2m_ibuf_addr =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_CUR_S2M_IBUF_ADDR);

	state->cur_dma_ibuf_addr =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_CUR_DMA_IBUF_ADDR);

	state->cur_dma_dest_addr =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_CUR_DMA_DEST_ADDR);

	state->cur_isp_dest_addr =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_CUR_ISP_DEST_ADDR);

	state->dma_cmds_send =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_CUR_NR_DMA_CMDS_SEND);

	state->main_cntrl_state =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_MAIN_CNTRL_STATE);

	state->dma_sync_state =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_DMA_SYNC_STATE);

	state->isp_sync_state =
	    ibuf_ctrl_reg_load(ID, reg_bank_offset + _IBUF_CNTRL_ISP_SYNC_STATE);
}

/* Get the ibuf-controller state. */
static inline void ibuf_ctrl_get_state(const ibuf_ctrl_ID_t ID,
				       ibuf_ctrl_state_t *state)
{
	u32 i;

	state->recalc_words =
	    ibuf_ctrl_reg_load(ID, _IBUF_CNTRL_RECALC_WORDS_STATUS);
	state->arbiters =
	    ibuf_ctrl_reg_load(ID, _IBUF_CNTRL_ARBITERS_STATUS);

	/*
	 * Get the values of the register-set per
	 * ibuf-controller process.
	 */
	for (i = 0; i < N_IBUF_CTRL_PROCS[ID]; i++) {
		ibuf_ctrl_get_proc_state(
		    ID,
		    i,
		    &state->proc_state[i]);
	}
}

/* Dump the ibuf-controller state */
static inline void ibuf_ctrl_dump_state(const ibuf_ctrl_ID_t ID,
					ibuf_ctrl_state_t *state)
{
	u32 i;

	ia_css_print("IBUF controller ID %d recalculate words 0x%x\n", ID,
		     state->recalc_words);
	ia_css_print("IBUF controller ID %d arbiters 0x%x\n", ID, state->arbiters);

	/*
	 * Dump the values of the register-set per
	 * ibuf-controller process.
	 */
	for (i = 0; i < N_IBUF_CTRL_PROCS[ID]; i++) {
		ia_css_print("IBUF controller ID %d Process ID %d num_items 0x%x\n", ID, i,
			     state->proc_state[i].num_items);
		ia_css_print("IBUF controller ID %d Process ID %d num_stores 0x%x\n", ID, i,
			     state->proc_state[i].num_stores);
		ia_css_print("IBUF controller ID %d Process ID %d dma_channel 0x%x\n", ID, i,
			     state->proc_state[i].dma_channel);
		ia_css_print("IBUF controller ID %d Process ID %d dma_command 0x%x\n", ID, i,
			     state->proc_state[i].dma_command);
		ia_css_print("IBUF controller ID %d Process ID %d ibuf_st_addr 0x%x\n", ID, i,
			     state->proc_state[i].ibuf_st_addr);
		ia_css_print("IBUF controller ID %d Process ID %d ibuf_stride 0x%x\n", ID, i,
			     state->proc_state[i].ibuf_stride);
		ia_css_print("IBUF controller ID %d Process ID %d ibuf_end_addr 0x%x\n", ID, i,
			     state->proc_state[i].ibuf_end_addr);
		ia_css_print("IBUF controller ID %d Process ID %d dest_st_addr 0x%x\n", ID, i,
			     state->proc_state[i].dest_st_addr);
		ia_css_print("IBUF controller ID %d Process ID %d dest_stride 0x%x\n", ID, i,
			     state->proc_state[i].dest_stride);
		ia_css_print("IBUF controller ID %d Process ID %d dest_end_addr 0x%x\n", ID, i,
			     state->proc_state[i].dest_end_addr);
		ia_css_print("IBUF controller ID %d Process ID %d sync_frame 0x%x\n", ID, i,
			     state->proc_state[i].sync_frame);
		ia_css_print("IBUF controller ID %d Process ID %d sync_command 0x%x\n", ID, i,
			     state->proc_state[i].sync_command);
		ia_css_print("IBUF controller ID %d Process ID %d store_command 0x%x\n", ID, i,
			     state->proc_state[i].store_command);
		ia_css_print("IBUF controller ID %d Process ID %d shift_returned_items 0x%x\n",
			     ID, i,
			     state->proc_state[i].shift_returned_items);
		ia_css_print("IBUF controller ID %d Process ID %d elems_ibuf 0x%x\n", ID, i,
			     state->proc_state[i].elems_ibuf);
		ia_css_print("IBUF controller ID %d Process ID %d elems_dest 0x%x\n", ID, i,
			     state->proc_state[i].elems_dest);
		ia_css_print("IBUF controller ID %d Process ID %d cur_stores 0x%x\n", ID, i,
			     state->proc_state[i].cur_stores);
		ia_css_print("IBUF controller ID %d Process ID %d cur_acks 0x%x\n", ID, i,
			     state->proc_state[i].cur_acks);
		ia_css_print("IBUF controller ID %d Process ID %d cur_s2m_ibuf_addr 0x%x\n", ID,
			     i,
			     state->proc_state[i].cur_s2m_ibuf_addr);
		ia_css_print("IBUF controller ID %d Process ID %d cur_dma_ibuf_addr 0x%x\n", ID,
			     i,
			     state->proc_state[i].cur_dma_ibuf_addr);
		ia_css_print("IBUF controller ID %d Process ID %d cur_dma_dest_addr 0x%x\n", ID,
			     i,
			     state->proc_state[i].cur_dma_dest_addr);
		ia_css_print("IBUF controller ID %d Process ID %d cur_isp_dest_addr 0x%x\n", ID,
			     i,
			     state->proc_state[i].cur_isp_dest_addr);
		ia_css_print("IBUF controller ID %d Process ID %d dma_cmds_send 0x%x\n", ID, i,
			     state->proc_state[i].dma_cmds_send);
		ia_css_print("IBUF controller ID %d Process ID %d main_cntrl_state 0x%x\n", ID,
			     i,
			     state->proc_state[i].main_cntrl_state);
		ia_css_print("IBUF controller ID %d Process ID %d dma_sync_state 0x%x\n", ID, i,
			     state->proc_state[i].dma_sync_state);
		ia_css_print("IBUF controller ID %d Process ID %d isp_sync_state 0x%x\n", ID, i,
			     state->proc_state[i].isp_sync_state);
	}
}

#endif /* __INPUT_SYSTEM_PRIVATE_H_INCLUDED__ */
