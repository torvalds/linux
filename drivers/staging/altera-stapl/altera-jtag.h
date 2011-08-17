/*
 * altera-jtag.h
 *
 * altera FPGA driver
 *
 * Copyright (C) Altera Corporation 1998-2001
 * Copyright (C) 2010 NetUP Inc.
 * Copyright (C) 2010 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef ALTERA_JTAG_H
#define ALTERA_JTAG_H

/* Function Prototypes */
enum altera_jtag_state {
	ILLEGAL_JTAG_STATE = -1,
	RESET = 0,
	IDLE = 1,
	DRSELECT = 2,
	DRCAPTURE = 3,
	DRSHIFT = 4,
	DREXIT1 = 5,
	DRPAUSE = 6,
	DREXIT2 = 7,
	DRUPDATE = 8,
	IRSELECT = 9,
	IRCAPTURE = 10,
	IRSHIFT = 11,
	IREXIT1 = 12,
	IRPAUSE = 13,
	IREXIT2 = 14,
	IRUPDATE = 15

};

struct altera_jtag {
	/* Global variable to store the current JTAG state */
	enum altera_jtag_state jtag_state;

	/* Store current stop-state for DR and IR scan commands */
	enum altera_jtag_state drstop_state;
	enum altera_jtag_state irstop_state;

	/* Store current padding values */
	u32 dr_pre;
	u32 dr_post;
	u32 ir_pre;
	u32 ir_post;
	u32 dr_length;
	u32 ir_length;
	u8 *dr_pre_data;
	u8 *dr_post_data;
	u8 *ir_pre_data;
	u8 *ir_post_data;
	u8 *dr_buffer;
	u8 *ir_buffer;
};

#define ALTERA_STACK_SIZE 128
#define ALTERA_MESSAGE_LENGTH 1024

struct altera_state {
	struct altera_config	*config;
	struct altera_jtag	js;
	char			msg_buff[ALTERA_MESSAGE_LENGTH + 1];
	long			stack[ALTERA_STACK_SIZE];
};

int altera_jinit(struct altera_state *astate);
int altera_set_drstop(struct altera_jtag *js, enum altera_jtag_state state);
int altera_set_irstop(struct altera_jtag *js, enum altera_jtag_state state);
int altera_set_dr_pre(struct altera_jtag *js, u32 count, u32 start_index,
				u8 *preamble_data);
int altera_set_ir_pre(struct altera_jtag *js, u32 count, u32 start_index,
				u8 *preamble_data);
int altera_set_dr_post(struct altera_jtag *js, u32 count, u32 start_index,
				u8 *postamble_data);
int altera_set_ir_post(struct altera_jtag *js, u32 count, u32 start_index,
				u8 *postamble_data);
int altera_goto_jstate(struct altera_state *astate,
				enum altera_jtag_state state);
int altera_wait_cycles(struct altera_state *astate, s32 cycles,
				enum altera_jtag_state wait_state);
int altera_wait_msecs(struct altera_state *astate, s32 microseconds,
				enum altera_jtag_state wait_state);
int altera_irscan(struct altera_state *astate, u32 count,
				u8 *tdi_data, u32 start_index);
int altera_swap_ir(struct altera_state *astate,
				u32 count, u8 *in_data,
				u32 in_index, u8 *out_data,
				u32 out_index);
int altera_drscan(struct altera_state *astate, u32 count,
				u8 *tdi_data, u32 start_index);
int altera_swap_dr(struct altera_state *astate, u32 count,
				u8 *in_data, u32 in_index,
				u8 *out_data, u32 out_index);
void altera_free_buffers(struct altera_state *astate);
#endif /* ALTERA_JTAG_H */
