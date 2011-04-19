/*
 * altera-jtag.c
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

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <staging/altera.h>
#include "altera-exprt.h"
#include "altera-jtag.h"

#define	alt_jtag_io(a, b, c)\
		astate->config->jtag_io(astate->config->dev, a, b, c);

#define	alt_malloc(a)	kzalloc(a, GFP_KERNEL);

/*
 * This structure shows, for each JTAG state, which state is reached after
 * a single TCK clock cycle with TMS high or TMS low, respectively.  This
 * describes all possible state transitions in the JTAG state machine.
 */
struct altera_jtag_machine {
	enum altera_jtag_state tms_high;
	enum altera_jtag_state tms_low;
};

static const struct altera_jtag_machine altera_transitions[] = {
	/* RESET     */	{ RESET,	IDLE },
	/* IDLE      */	{ DRSELECT,	IDLE },
	/* DRSELECT  */	{ IRSELECT,	DRCAPTURE },
	/* DRCAPTURE */	{ DREXIT1,	DRSHIFT },
	/* DRSHIFT   */	{ DREXIT1,	DRSHIFT },
	/* DREXIT1   */	{ DRUPDATE,	DRPAUSE },
	/* DRPAUSE   */	{ DREXIT2,	DRPAUSE },
	/* DREXIT2   */	{ DRUPDATE,	DRSHIFT },
	/* DRUPDATE  */	{ DRSELECT,	IDLE },
	/* IRSELECT  */	{ RESET,	IRCAPTURE },
	/* IRCAPTURE */	{ IREXIT1,	IRSHIFT },
	/* IRSHIFT   */	{ IREXIT1,	IRSHIFT },
	/* IREXIT1   */	{ IRUPDATE,	IRPAUSE },
	/* IRPAUSE   */	{ IREXIT2,	IRPAUSE },
	/* IREXIT2   */	{ IRUPDATE,	IRSHIFT },
	/* IRUPDATE  */	{ DRSELECT,	IDLE }
};

/*
 * This table contains the TMS value to be used to take the NEXT STEP on
 * the path to the desired state.  The array index is the current state,
 * and the bit position is the desired endstate.  To find out which state
 * is used as the intermediate state, look up the TMS value in the
 * altera_transitions[] table.
 */
static const u16 altera_jtag_path_map[16] = {
	/* RST	RTI	SDRS	CDR	SDR	E1DR	PDR	E2DR */
	0x0001,	0xFFFD,	0xFE01,	0xFFE7,	0xFFEF,	0xFF0F,	0xFFBF,	0xFFFF,
	/* UDR	SIRS	CIR	SIR	E1IR	PIR	E2IR	UIR */
	0xFEFD,	0x0001,	0xF3FF,	0xF7FF,	0x87FF,	0xDFFF,	0xFFFF,	0x7FFD
};

/* Flag bits for alt_jtag_io() function */
#define TMS_HIGH   1
#define TMS_LOW    0
#define TDI_HIGH   1
#define TDI_LOW    0
#define READ_TDO   1
#define IGNORE_TDO 0

int altera_jinit(struct altera_state *astate)
{
	struct altera_jtag *js = &astate->js;

	/* initial JTAG state is unknown */
	js->jtag_state = ILLEGAL_JTAG_STATE;

	/* initialize to default state */
	js->drstop_state = IDLE;
	js->irstop_state = IDLE;
	js->dr_pre  = 0;
	js->dr_post = 0;
	js->ir_pre  = 0;
	js->ir_post = 0;
	js->dr_length    = 0;
	js->ir_length    = 0;

	js->dr_pre_data  = NULL;
	js->dr_post_data = NULL;
	js->ir_pre_data  = NULL;
	js->ir_post_data = NULL;
	js->dr_buffer	 = NULL;
	js->ir_buffer	 = NULL;

	return 0;
}

int altera_set_drstop(struct altera_jtag *js, enum altera_jtag_state state)
{
	js->drstop_state = state;

	return 0;
}

int altera_set_irstop(struct altera_jtag *js, enum altera_jtag_state state)
{
	js->irstop_state = state;

	return 0;
}

int altera_set_dr_pre(struct altera_jtag *js,
				u32 count, u32 start_index,
				u8 *preamble_data)
{
	int status = 0;
	u32 i;
	u32 j;

	if (count > js->dr_pre) {
		kfree(js->dr_pre_data);
		js->dr_pre_data = (u8 *)alt_malloc((count + 7) >> 3);
		if (js->dr_pre_data == NULL)
			status = -ENOMEM;
		else
			js->dr_pre = count;
	} else
		js->dr_pre = count;

	if (status == 0) {
		for (i = 0; i < count; ++i) {
			j = i + start_index;

			if (preamble_data == NULL)
				js->dr_pre_data[i >> 3] |= (1 << (i & 7));
			else {
				if (preamble_data[j >> 3] & (1 << (j & 7)))
					js->dr_pre_data[i >> 3] |=
							(1 << (i & 7));
				else
					js->dr_pre_data[i >> 3] &=
							~(u32)(1 << (i & 7));

			}
		}
	}

	return status;
}

int altera_set_ir_pre(struct altera_jtag *js, u32 count, u32 start_index,
							u8 *preamble_data)
{
	int status = 0;
	u32 i;
	u32 j;

	if (count > js->ir_pre) {
		kfree(js->ir_pre_data);
		js->ir_pre_data = (u8 *)alt_malloc((count + 7) >> 3);
		if (js->ir_pre_data == NULL)
			status = -ENOMEM;
		else
			js->ir_pre = count;

	} else
		js->ir_pre = count;

	if (status == 0) {
		for (i = 0; i < count; ++i) {
			j = i + start_index;
			if (preamble_data == NULL)
				js->ir_pre_data[i >> 3] |= (1 << (i & 7));
			else {
				if (preamble_data[j >> 3] & (1 << (j & 7)))
					js->ir_pre_data[i >> 3] |=
							(1 << (i & 7));
				else
					js->ir_pre_data[i >> 3] &=
							~(u32)(1 << (i & 7));

			}
		}
	}

	return status;
}

int altera_set_dr_post(struct altera_jtag *js, u32 count, u32 start_index,
						u8 *postamble_data)
{
	int status = 0;
	u32 i;
	u32 j;

	if (count > js->dr_post) {
		kfree(js->dr_post_data);
		js->dr_post_data = (u8 *)alt_malloc((count + 7) >> 3);

		if (js->dr_post_data == NULL)
			status = -ENOMEM;
		else
			js->dr_post = count;

	} else
		js->dr_post = count;

	if (status == 0) {
		for (i = 0; i < count; ++i) {
			j = i + start_index;

			if (postamble_data == NULL)
				js->dr_post_data[i >> 3] |= (1 << (i & 7));
			else {
				if (postamble_data[j >> 3] & (1 << (j & 7)))
					js->dr_post_data[i >> 3] |=
								(1 << (i & 7));
				else
					js->dr_post_data[i >> 3] &=
					    ~(u32)(1 << (i & 7));

			}
		}
	}

	return status;
}

int altera_set_ir_post(struct altera_jtag *js, u32 count, u32 start_index,
						u8 *postamble_data)
{
	int status = 0;
	u32 i;
	u32 j;

	if (count > js->ir_post) {
		kfree(js->ir_post_data);
		js->ir_post_data = (u8 *)alt_malloc((count + 7) >> 3);
		if (js->ir_post_data == NULL)
			status = -ENOMEM;
		else
			js->ir_post = count;

	} else
		js->ir_post = count;

	if (status != 0)
		return status;

	for (i = 0; i < count; ++i) {
		j = i + start_index;

		if (postamble_data == NULL)
			js->ir_post_data[i >> 3] |= (1 << (i & 7));
		else {
			if (postamble_data[j >> 3] & (1 << (j & 7)))
				js->ir_post_data[i >> 3] |= (1 << (i & 7));
			else
				js->ir_post_data[i >> 3] &=
				    ~(u32)(1 << (i & 7));

		}
	}

	return status;
}

static void altera_jreset_idle(struct altera_state *astate)
{
	struct altera_jtag *js = &astate->js;
	int i;
	/* Go to Test Logic Reset (no matter what the starting state may be) */
	for (i = 0; i < 5; ++i)
		alt_jtag_io(TMS_HIGH, TDI_LOW, IGNORE_TDO);

	/* Now step to Run Test / Idle */
	alt_jtag_io(TMS_LOW, TDI_LOW, IGNORE_TDO);
	js->jtag_state = IDLE;
}

int altera_goto_jstate(struct altera_state *astate,
					enum altera_jtag_state state)
{
	struct altera_jtag *js = &astate->js;
	int tms;
	int count = 0;
	int status = 0;

	if (js->jtag_state == ILLEGAL_JTAG_STATE)
		/* initialize JTAG chain to known state */
		altera_jreset_idle(astate);

	if (js->jtag_state == state) {
		/*
		 * We are already in the desired state.
		 * If it is a stable state, loop here.
		 * Otherwise do nothing (no clock cycles).
		 */
		if ((state == IDLE) || (state == DRSHIFT) ||
			(state == DRPAUSE) || (state == IRSHIFT) ||
				(state == IRPAUSE)) {
			alt_jtag_io(TMS_LOW, TDI_LOW, IGNORE_TDO);
		} else if (state == RESET)
			alt_jtag_io(TMS_HIGH, TDI_LOW, IGNORE_TDO);

	} else {
		while ((js->jtag_state != state) && (count < 9)) {
			/* Get TMS value to take a step toward desired state */
			tms = (altera_jtag_path_map[js->jtag_state] &
							(1 << state))
							? TMS_HIGH : TMS_LOW;

			/* Take a step */
			alt_jtag_io(tms, TDI_LOW, IGNORE_TDO);

			if (tms)
				js->jtag_state =
					altera_transitions[js->jtag_state].tms_high;
			else
				js->jtag_state =
					altera_transitions[js->jtag_state].tms_low;

			++count;
		}
	}

	if (js->jtag_state != state)
		status = -EREMOTEIO;

	return status;
}

int altera_wait_cycles(struct altera_state *astate,
					s32 cycles,
					enum altera_jtag_state wait_state)
{
	struct altera_jtag *js = &astate->js;
	int tms;
	s32 count;
	int status = 0;

	if (js->jtag_state != wait_state)
		status = altera_goto_jstate(astate, wait_state);

	if (status == 0) {
		/*
		 * Set TMS high to loop in RESET state
		 * Set TMS low to loop in any other stable state
		 */
		tms = (wait_state == RESET) ? TMS_HIGH : TMS_LOW;

		for (count = 0L; count < cycles; count++)
			alt_jtag_io(tms, TDI_LOW, IGNORE_TDO);

	}

	return status;
}

int altera_wait_msecs(struct altera_state *astate,
			s32 microseconds, enum altera_jtag_state wait_state)
/*
 * Causes JTAG hardware to sit in the specified stable
 * state for the specified duration of real time.  If
 * no JTAG operations have been performed yet, then only
 * a delay is performed.  This permits the WAIT USECS
 * statement to be used in VECTOR programs without causing
 * any JTAG operations.
 * Returns 0 for success, else appropriate error code.
 */
{
	struct altera_jtag *js = &astate->js;
	int status = 0;

	if ((js->jtag_state != ILLEGAL_JTAG_STATE) &&
	    (js->jtag_state != wait_state))
		status = altera_goto_jstate(astate, wait_state);

	if (status == 0)
		/* Wait for specified time interval */
		udelay(microseconds);

	return status;
}

static void altera_concatenate_data(u8 *buffer,
				u8 *preamble_data,
				u32 preamble_count,
				u8 *target_data,
				u32 start_index,
				u32 target_count,
				u8 *postamble_data,
				u32 postamble_count)
/*
 * Copies preamble data, target data, and postamble data
 * into one buffer for IR or DR scans.
 */
{
	u32 i, j, k;

	for (i = 0L; i < preamble_count; ++i) {
		if (preamble_data[i >> 3L] & (1L << (i & 7L)))
			buffer[i >> 3L] |= (1L << (i & 7L));
		else
			buffer[i >> 3L] &= ~(u32)(1L << (i & 7L));

	}

	j = start_index;
	k = preamble_count + target_count;
	for (; i < k; ++i, ++j) {
		if (target_data[j >> 3L] & (1L << (j & 7L)))
			buffer[i >> 3L] |= (1L << (i & 7L));
		else
			buffer[i >> 3L] &= ~(u32)(1L << (i & 7L));

	}

	j = 0L;
	k = preamble_count + target_count + postamble_count;
	for (; i < k; ++i, ++j) {
		if (postamble_data[j >> 3L] & (1L << (j & 7L)))
			buffer[i >> 3L] |= (1L << (i & 7L));
		else
			buffer[i >> 3L] &= ~(u32)(1L << (i & 7L));

	}
}

static int alt_jtag_drscan(struct altera_state *astate,
			int start_state,
			int count,
			u8 *tdi,
			u8 *tdo)
{
	int i = 0;
	int tdo_bit = 0;
	int status = 1;

	/* First go to DRSHIFT state */
	switch (start_state) {
	case 0:						/* IDLE */
		alt_jtag_io(1, 0, 0);	/* DRSELECT */
		alt_jtag_io(0, 0, 0);	/* DRCAPTURE */
		alt_jtag_io(0, 0, 0);	/* DRSHIFT */
		break;

	case 1:						/* DRPAUSE */
		alt_jtag_io(1, 0, 0);	/* DREXIT2 */
		alt_jtag_io(1, 0, 0);	/* DRUPDATE */
		alt_jtag_io(1, 0, 0);	/* DRSELECT */
		alt_jtag_io(0, 0, 0);	/* DRCAPTURE */
		alt_jtag_io(0, 0, 0);	/* DRSHIFT */
		break;

	case 2:						/* IRPAUSE */
		alt_jtag_io(1, 0, 0);	/* IREXIT2 */
		alt_jtag_io(1, 0, 0);	/* IRUPDATE */
		alt_jtag_io(1, 0, 0);	/* DRSELECT */
		alt_jtag_io(0, 0, 0);	/* DRCAPTURE */
		alt_jtag_io(0, 0, 0);	/* DRSHIFT */
		break;

	default:
		status = 0;
	}

	if (status) {
		/* loop in the SHIFT-DR state */
		for (i = 0; i < count; i++) {
			tdo_bit = alt_jtag_io(
					(i == count - 1),
					tdi[i >> 3] & (1 << (i & 7)),
					(tdo != NULL));

			if (tdo != NULL) {
				if (tdo_bit)
					tdo[i >> 3] |= (1 << (i & 7));
				else
					tdo[i >> 3] &= ~(u32)(1 << (i & 7));

			}
		}

		alt_jtag_io(0, 0, 0);	/* DRPAUSE */
	}

	return status;
}

static int alt_jtag_irscan(struct altera_state *astate,
		    int start_state,
		    int count,
		    u8 *tdi,
		    u8 *tdo)
{
	int i = 0;
	int tdo_bit = 0;
	int status = 1;

	/* First go to IRSHIFT state */
	switch (start_state) {
	case 0:						/* IDLE */
		alt_jtag_io(1, 0, 0);	/* DRSELECT */
		alt_jtag_io(1, 0, 0);	/* IRSELECT */
		alt_jtag_io(0, 0, 0);	/* IRCAPTURE */
		alt_jtag_io(0, 0, 0);	/* IRSHIFT */
		break;

	case 1:						/* DRPAUSE */
		alt_jtag_io(1, 0, 0);	/* DREXIT2 */
		alt_jtag_io(1, 0, 0);	/* DRUPDATE */
		alt_jtag_io(1, 0, 0);	/* DRSELECT */
		alt_jtag_io(1, 0, 0);	/* IRSELECT */
		alt_jtag_io(0, 0, 0);	/* IRCAPTURE */
		alt_jtag_io(0, 0, 0);	/* IRSHIFT */
		break;

	case 2:						/* IRPAUSE */
		alt_jtag_io(1, 0, 0);	/* IREXIT2 */
		alt_jtag_io(1, 0, 0);	/* IRUPDATE */
		alt_jtag_io(1, 0, 0);	/* DRSELECT */
		alt_jtag_io(1, 0, 0);	/* IRSELECT */
		alt_jtag_io(0, 0, 0);	/* IRCAPTURE */
		alt_jtag_io(0, 0, 0);	/* IRSHIFT */
		break;

	default:
		status = 0;
	}

	if (status) {
		/* loop in the SHIFT-IR state */
		for (i = 0; i < count; i++) {
			tdo_bit = alt_jtag_io(
				      (i == count - 1),
				      tdi[i >> 3] & (1 << (i & 7)),
				      (tdo != NULL));
			if (tdo != NULL) {
				if (tdo_bit)
					tdo[i >> 3] |= (1 << (i & 7));
				else
					tdo[i >> 3] &= ~(u32)(1 << (i & 7));

			}
		}

		alt_jtag_io(0, 0, 0);	/* IRPAUSE */
	}

	return status;
}

static void altera_extract_target_data(u8 *buffer,
				u8 *target_data,
				u32 start_index,
				u32 preamble_count,
				u32 target_count)
/*
 * Copies target data from scan buffer, filtering out
 * preamble and postamble data.
 */
{
	u32 i;
	u32 j;
	u32 k;

	j = preamble_count;
	k = start_index + target_count;
	for (i = start_index; i < k; ++i, ++j) {
		if (buffer[j >> 3] & (1 << (j & 7)))
			target_data[i >> 3] |= (1 << (i & 7));
		else
			target_data[i >> 3] &= ~(u32)(1 << (i & 7));

	}
}

int altera_irscan(struct altera_state *astate,
				u32 count,
				u8 *tdi_data,
				u32 start_index)
/* Shifts data into instruction register */
{
	struct altera_jtag *js = &astate->js;
	int start_code = 0;
	u32 alloc_chars = 0;
	u32 shift_count = js->ir_pre + count + js->ir_post;
	int status = 0;
	enum altera_jtag_state start_state = ILLEGAL_JTAG_STATE;

	switch (js->jtag_state) {
	case ILLEGAL_JTAG_STATE:
	case RESET:
	case IDLE:
		start_code = 0;
		start_state = IDLE;
		break;

	case DRSELECT:
	case DRCAPTURE:
	case DRSHIFT:
	case DREXIT1:
	case DRPAUSE:
	case DREXIT2:
	case DRUPDATE:
		start_code = 1;
		start_state = DRPAUSE;
		break;

	case IRSELECT:
	case IRCAPTURE:
	case IRSHIFT:
	case IREXIT1:
	case IRPAUSE:
	case IREXIT2:
	case IRUPDATE:
		start_code = 2;
		start_state = IRPAUSE;
		break;

	default:
		status = -EREMOTEIO;
		break;
	}

	if (status == 0)
		if (js->jtag_state != start_state)
			status = altera_goto_jstate(astate, start_state);

	if (status == 0) {
		if (shift_count > js->ir_length) {
			alloc_chars = (shift_count + 7) >> 3;
			kfree(js->ir_buffer);
			js->ir_buffer = (u8 *)alt_malloc(alloc_chars);
			if (js->ir_buffer == NULL)
				status = -ENOMEM;
			else
				js->ir_length = alloc_chars * 8;

		}
	}

	if (status == 0) {
		/*
		 * Copy preamble data, IR data,
		 * and postamble data into a buffer
		 */
		altera_concatenate_data(js->ir_buffer,
					js->ir_pre_data,
					js->ir_pre,
					tdi_data,
					start_index,
					count,
					js->ir_post_data,
					js->ir_post);
		/* Do the IRSCAN */
		alt_jtag_irscan(astate,
				start_code,
				shift_count,
				js->ir_buffer,
				NULL);

		/* alt_jtag_irscan() always ends in IRPAUSE state */
		js->jtag_state = IRPAUSE;
	}

	if (status == 0)
		if (js->irstop_state != IRPAUSE)
			status = altera_goto_jstate(astate, js->irstop_state);


	return status;
}

int altera_swap_ir(struct altera_state *astate,
			    u32 count,
			    u8 *in_data,
			    u32 in_index,
			    u8 *out_data,
			    u32 out_index)
/* Shifts data into instruction register, capturing output data */
{
	struct altera_jtag *js = &astate->js;
	int start_code = 0;
	u32 alloc_chars = 0;
	u32 shift_count = js->ir_pre + count + js->ir_post;
	int status = 0;
	enum altera_jtag_state start_state = ILLEGAL_JTAG_STATE;

	switch (js->jtag_state) {
	case ILLEGAL_JTAG_STATE:
	case RESET:
	case IDLE:
		start_code = 0;
		start_state = IDLE;
		break;

	case DRSELECT:
	case DRCAPTURE:
	case DRSHIFT:
	case DREXIT1:
	case DRPAUSE:
	case DREXIT2:
	case DRUPDATE:
		start_code = 1;
		start_state = DRPAUSE;
		break;

	case IRSELECT:
	case IRCAPTURE:
	case IRSHIFT:
	case IREXIT1:
	case IRPAUSE:
	case IREXIT2:
	case IRUPDATE:
		start_code = 2;
		start_state = IRPAUSE;
		break;

	default:
		status = -EREMOTEIO;
		break;
	}

	if (status == 0)
		if (js->jtag_state != start_state)
			status = altera_goto_jstate(astate, start_state);

	if (status == 0) {
		if (shift_count > js->ir_length) {
			alloc_chars = (shift_count + 7) >> 3;
			kfree(js->ir_buffer);
			js->ir_buffer = (u8 *)alt_malloc(alloc_chars);
			if (js->ir_buffer == NULL)
				status = -ENOMEM;
			else
				js->ir_length = alloc_chars * 8;

		}
	}

	if (status == 0) {
		/*
		 * Copy preamble data, IR data,
		 * and postamble data into a buffer
		 */
		altera_concatenate_data(js->ir_buffer,
					js->ir_pre_data,
					js->ir_pre,
					in_data,
					in_index,
					count,
					js->ir_post_data,
					js->ir_post);

		/* Do the IRSCAN */
		alt_jtag_irscan(astate,
				start_code,
				shift_count,
				js->ir_buffer,
				js->ir_buffer);

		/* alt_jtag_irscan() always ends in IRPAUSE state */
		js->jtag_state = IRPAUSE;
	}

	if (status == 0)
		if (js->irstop_state != IRPAUSE)
			status = altera_goto_jstate(astate, js->irstop_state);


	if (status == 0)
		/* Now extract the returned data from the buffer */
		altera_extract_target_data(js->ir_buffer,
					out_data, out_index,
					js->ir_pre, count);

	return status;
}

int altera_drscan(struct altera_state *astate,
				u32 count,
				u8 *tdi_data,
				u32 start_index)
/* Shifts data into data register (ignoring output data) */
{
	struct altera_jtag *js = &astate->js;
	int start_code = 0;
	u32 alloc_chars = 0;
	u32 shift_count = js->dr_pre + count + js->dr_post;
	int status = 0;
	enum altera_jtag_state start_state = ILLEGAL_JTAG_STATE;

	switch (js->jtag_state) {
	case ILLEGAL_JTAG_STATE:
	case RESET:
	case IDLE:
		start_code = 0;
		start_state = IDLE;
		break;

	case DRSELECT:
	case DRCAPTURE:
	case DRSHIFT:
	case DREXIT1:
	case DRPAUSE:
	case DREXIT2:
	case DRUPDATE:
		start_code = 1;
		start_state = DRPAUSE;
		break;

	case IRSELECT:
	case IRCAPTURE:
	case IRSHIFT:
	case IREXIT1:
	case IRPAUSE:
	case IREXIT2:
	case IRUPDATE:
		start_code = 2;
		start_state = IRPAUSE;
		break;

	default:
		status = -EREMOTEIO;
		break;
	}

	if (status == 0)
		if (js->jtag_state != start_state)
			status = altera_goto_jstate(astate, start_state);

	if (status == 0) {
		if (shift_count > js->dr_length) {
			alloc_chars = (shift_count + 7) >> 3;
			kfree(js->dr_buffer);
			js->dr_buffer = (u8 *)alt_malloc(alloc_chars);
			if (js->dr_buffer == NULL)
				status = -ENOMEM;
			else
				js->dr_length = alloc_chars * 8;

		}
	}

	if (status == 0) {
		/*
		 * Copy preamble data, DR data,
		 * and postamble data into a buffer
		 */
		altera_concatenate_data(js->dr_buffer,
					js->dr_pre_data,
					js->dr_pre,
					tdi_data,
					start_index,
					count,
					js->dr_post_data,
					js->dr_post);
		/* Do the DRSCAN */
		alt_jtag_drscan(astate, start_code, shift_count,
				js->dr_buffer, NULL);
		/* alt_jtag_drscan() always ends in DRPAUSE state */
		js->jtag_state = DRPAUSE;
	}

	if (status == 0)
		if (js->drstop_state != DRPAUSE)
			status = altera_goto_jstate(astate, js->drstop_state);

	return status;
}

int altera_swap_dr(struct altera_state *astate, u32 count,
				u8 *in_data, u32 in_index,
				u8 *out_data, u32 out_index)
/* Shifts data into data register, capturing output data */
{
	struct altera_jtag *js = &astate->js;
	int start_code = 0;
	u32 alloc_chars = 0;
	u32 shift_count = js->dr_pre + count + js->dr_post;
	int status = 0;
	enum altera_jtag_state start_state = ILLEGAL_JTAG_STATE;

	switch (js->jtag_state) {
	case ILLEGAL_JTAG_STATE:
	case RESET:
	case IDLE:
		start_code = 0;
		start_state = IDLE;
		break;

	case DRSELECT:
	case DRCAPTURE:
	case DRSHIFT:
	case DREXIT1:
	case DRPAUSE:
	case DREXIT2:
	case DRUPDATE:
		start_code = 1;
		start_state = DRPAUSE;
		break;

	case IRSELECT:
	case IRCAPTURE:
	case IRSHIFT:
	case IREXIT1:
	case IRPAUSE:
	case IREXIT2:
	case IRUPDATE:
		start_code = 2;
		start_state = IRPAUSE;
		break;

	default:
		status = -EREMOTEIO;
		break;
	}

	if (status == 0)
		if (js->jtag_state != start_state)
			status = altera_goto_jstate(astate, start_state);

	if (status == 0) {
		if (shift_count > js->dr_length) {
			alloc_chars = (shift_count + 7) >> 3;
			kfree(js->dr_buffer);
			js->dr_buffer = (u8 *)alt_malloc(alloc_chars);

			if (js->dr_buffer == NULL)
				status = -ENOMEM;
			else
				js->dr_length = alloc_chars * 8;

		}
	}

	if (status == 0) {
		/*
		 * Copy preamble data, DR data,
		 * and postamble data into a buffer
		 */
		altera_concatenate_data(js->dr_buffer,
				js->dr_pre_data,
				js->dr_pre,
				in_data,
				in_index,
				count,
				js->dr_post_data,
				js->dr_post);

		/* Do the DRSCAN */
		alt_jtag_drscan(astate,
				start_code,
				shift_count,
				js->dr_buffer,
				js->dr_buffer);

		/* alt_jtag_drscan() always ends in DRPAUSE state */
		js->jtag_state = DRPAUSE;
	}

	if (status == 0)
		if (js->drstop_state != DRPAUSE)
			status = altera_goto_jstate(astate, js->drstop_state);

	if (status == 0)
		/* Now extract the returned data from the buffer */
		altera_extract_target_data(js->dr_buffer,
					out_data,
					out_index,
					js->dr_pre,
					count);

	return status;
}

void altera_free_buffers(struct altera_state *astate)
{
	struct altera_jtag *js = &astate->js;
	/* If the JTAG interface was used, reset it to TLR */
	if (js->jtag_state != ILLEGAL_JTAG_STATE)
		altera_jreset_idle(astate);

	kfree(js->dr_pre_data);
	js->dr_pre_data = NULL;

	kfree(js->dr_post_data);
	js->dr_post_data = NULL;

	kfree(js->dr_buffer);
	js->dr_buffer = NULL;

	kfree(js->ir_pre_data);
	js->ir_pre_data = NULL;

	kfree(js->ir_post_data);
	js->ir_post_data = NULL;

	kfree(js->ir_buffer);
	js->ir_buffer = NULL;
}
