
/*
 *
 Copyright (c) Eicon Networks, 2002.
 *
 This source file is supplied for the use with
 Eicon Networks range of DIVA Server Adapters.
 *
 Eicon File Revision :    2.1
 *
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
 *
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
 implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.
 *
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef CAPIDTMF_H_
#define CAPIDTMF_H_
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
#define CAPIDTMF_TONE_GROUP_COUNT            2
#define CAPIDTMF_LOW_GROUP_FREQUENCIES       4
#define CAPIDTMF_HIGH_GROUP_FREQUENCIES      4
#define DSPDTMF_RX_SENSITIVITY_LOW_DEFAULT	50	/* -52 dBm */
#define DSPDTMF_RX_SENSITIVITY_HIGH_DEFAULT	50	/* -52 dBm */
#define DSPDTMF_RX_HIGH_EXCEEDING_LOW_DEFAULT	10	/* dB */
#define DSPDTMF_RX_LOW_EXCEEDING_HIGH_DEFAULT	10	/* dB */
#define DSPDTMF_RX_HARMONICS_SEL_DEFAULT	12	/* dB */
#define CAPIDTMF_RECV_BASE_FREQUENCY_COUNT   (CAPIDTMF_LOW_GROUP_FREQUENCIES + CAPIDTMF_HIGH_GROUP_FREQUENCIES)
#define CAPIDTMF_RECV_GUARD_FREQUENCY_COUNT  8
#define CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT  (CAPIDTMF_RECV_BASE_FREQUENCY_COUNT + CAPIDTMF_RECV_GUARD_FREQUENCY_COUNT)
#define CAPIDTMF_RECV_POSITIVE_COEFF_COUNT   16
#define CAPIDTMF_RECV_NEGATIVE_COEFF_COUNT   (CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT - CAPIDTMF_RECV_POSITIVE_COEFF_COUNT)
#define CAPIDTMF_RECV_ACCUMULATE_CYCLES      205
#define CAPIDTMF_RECV_FUNDAMENTAL_OFFSET     (0xff35L * 2)
#define CAPIDTMF_RECV_FUNDAMENTAL_DECREMENT  (0x0028L * 2)
#define CAPIDTMF_RECV_DIGIT_BUFFER_SIZE      32
#define CAPIDTMF_RECV_STATE_IDLE             0x00
#define CAPIDTMF_RECV_STATE_DTMF_ACTIVE      0x01
typedef struct tag_capidtmf_recv_state
{
	byte digit_buffer[CAPIDTMF_RECV_DIGIT_BUFFER_SIZE];
	word digit_write_pos;
	word digit_read_pos;
	word indication_state;
	word indication_state_ack;
	long goertzel_buffer[2][CAPIDTMF_RECV_TOTAL_FREQUENCY_COUNT];
	word min_gap_duration;
	word min_digit_duration;
	word cycle_counter;
	word current_digit_on_time;
	word current_digit_off_time;
	byte current_digit_value;
	byte state;
} t_capidtmf_recv_state;
typedef struct tag_capidtmf_state
{
	byte ulaw;
	t_capidtmf_recv_state recv;
} t_capidtmf_state;
word capidtmf_recv_indication(t_capidtmf_state *p_state, byte *buffer);
void capidtmf_recv_block(t_capidtmf_state *p_state, byte *buffer, word length);
void capidtmf_init(t_capidtmf_state *p_state, byte ulaw);
void capidtmf_recv_enable(t_capidtmf_state *p_state, word min_digit_duration, word min_gap_duration);
void capidtmf_recv_disable(t_capidtmf_state *p_state);
#define capidtmf_indication(p_state, buffer)  (((p_state)->recv.indication_state != (p_state)->recv.indication_state_ack) ? capidtmf_recv_indication(p_state, buffer) : 0)
#define capidtmf_recv_process_block(p_state, buffer, length)  { if ((p_state)->recv.state != CAPIDTMF_RECV_STATE_IDLE) capidtmf_recv_block(p_state, buffer, length); }
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
#endif
