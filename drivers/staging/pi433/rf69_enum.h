/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * enumerations for HopeRf rf69 radio module
 *
 * Copyright (C) 2016 Wolf-Entwicklungen
 *	Marcus Wolf <linux@wolf-entwicklungen.de>
 */

#ifndef RF69_ENUM_H
#define RF69_ENUM_H

enum mode {
	mode_sleep,
	standby,
	synthesizer,
	transmit,
	receive
};

enum modulation {
	OOK,
	FSK,
	UNDEF
};

enum mod_shaping {
	SHAPING_OFF,
	SHAPING_1_0,
	SHAPING_0_5,
	SHAPING_0_3,
	SHAPING_BR,
	SHAPING_2BR
};

enum pa_ramp {
	ramp3400,
	ramp2000,
	ramp1000,
	ramp500,
	ramp250,
	ramp125,
	ramp100,
	ramp62,
	ramp50,
	ramp40,
	ramp31,
	ramp25,
	ramp20,
	ramp15,
	ramp12,
	ramp10
};

enum antenna_impedance {
	fifty_ohm,
	two_hundred_ohm
};

enum lna_gain {
	automatic,
	max,
	max_minus_6,
	max_minus_12,
	max_minus_24,
	max_minus_36,
	max_minus_48,
	undefined
};

enum mantisse {
	mantisse16,
	mantisse20,
	mantisse24
};

enum threshold_decrement {
	dec_every8th,
	dec_every4th,
	dec_every2nd,
	dec_once,
	dec_twice,
	dec_4times,
	dec_8times,
	dec_16times
};

enum flag {
	mode_switch_completed,
	ready_to_receive,
	ready_to_send,
	pll_locked,
	rssi_exceeded_threshold,
	timeout,
	automode,
	sync_address_match,
	fifo_full,
//	fifo_not_empty, collision with next enum; replaced by following enum...
	fifo_empty,
	fifo_level_below_threshold,
	fifo_overrun,
	packet_sent,
	payload_ready,
	crc_ok,
	battery_low
};

enum fifo_fill_condition {
	after_sync_interrupt,
	always
};

enum packet_format {
	/*
	 * Used when the size of payload is fixed in advance. This mode of
	 * operation may be of interest to minimize RF overhead by 1 byte as
	 * no length byte field is required
	 */
	packet_length_fix,
	/*
	 * Used when the size of payload isn't known in advance. It requires the
	 * transmitter to send the length byte in each packet so the receiver
	 * would know how to operate properly
	 */
	packet_length_var
};

enum tx_start_condition {
	/* the number of bytes in the FIFO exceeds FIFO_THRESHOLD */
	fifo_level,
	/* at least one byte in the FIFO */
	fifo_not_empty
};

enum address_filtering {
	filtering_off,
	node_address,
	node_or_broadcast_address
};

enum dagc {
	normal_mode,
	improve,
	improve_for_low_modulation_index
};

#endif
