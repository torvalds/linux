/*
 * SBE 2T3E3 synchronous serial card driver for Linux
 *
 * Copyright (C) 2009-2010 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This code is based on a driver written by SBE Inc.
 */

#ifndef CTRL_H
#define CTRL_H

#define SBE_2T3E3_OFF					0
#define SBE_2T3E3_ON					1

#define SBE_2T3E3_LED_NONE				0
#define SBE_2T3E3_LED_GREEN				1
#define SBE_2T3E3_LED_YELLOW				2

#define SBE_2T3E3_CABLE_LENGTH_LESS_THAN_255_FEET	0
#define SBE_2T3E3_CABLE_LENGTH_GREATER_THAN_255_FEET	1

#define SBE_2T3E3_CRC_16				0
#define SBE_2T3E3_CRC_32				1

#define SBE_2T3E3_PANEL_FRONT				0
#define SBE_2T3E3_PANEL_REAR				1

#define SBE_2T3E3_FRAME_MODE_HDLC			0
#define SBE_2T3E3_FRAME_MODE_TRANSPARENT		1
#define SBE_2T3E3_FRAME_MODE_RAW			2

#define SBE_2T3E3_FRAME_TYPE_E3_G751			0
#define SBE_2T3E3_FRAME_TYPE_E3_G832			1
#define SBE_2T3E3_FRAME_TYPE_T3_CBIT			2
#define SBE_2T3E3_FRAME_TYPE_T3_M13			3

#define SBE_2T3E3_FRACTIONAL_MODE_NONE			0
#define SBE_2T3E3_FRACTIONAL_MODE_0			1
#define SBE_2T3E3_FRACTIONAL_MODE_1			2
#define SBE_2T3E3_FRACTIONAL_MODE_2			3

#define SBE_2T3E3_SCRAMBLER_OFF				0
#define SBE_2T3E3_SCRAMBLER_LARSCOM			1
#define SBE_2T3E3_SCRAMBLER_ADC_KENTROX_DIGITAL		2

#define SBE_2T3E3_TIMING_LOCAL				0
#define SBE_2T3E3_TIMING_LOOP				1

#define SBE_2T3E3_LOOPBACK_NONE				0
#define SBE_2T3E3_LOOPBACK_ETHERNET			1
#define SBE_2T3E3_LOOPBACK_FRAMER			2
#define SBE_2T3E3_LOOPBACK_LIU_DIGITAL			3
#define SBE_2T3E3_LOOPBACK_LIU_ANALOG			4
#define SBE_2T3E3_LOOPBACK_LIU_REMOTE			5

#define SBE_2T3E3_PAD_COUNT_1				1
#define SBE_2T3E3_PAD_COUNT_2				2
#define SBE_2T3E3_PAD_COUNT_3				3
#define SBE_2T3E3_PAD_COUNT_4				4

#define SBE_2T3E3_CHIP_21143				0
#define SBE_2T3E3_CHIP_CPLD				1
#define SBE_2T3E3_CHIP_FRAMER				2
#define SBE_2T3E3_CHIP_LIU				3

#define SBE_2T3E3_LOG_LEVEL_NONE			0
#define SBE_2T3E3_LOG_LEVEL_ERROR			1
#define SBE_2T3E3_LOG_LEVEL_WARNING			2
#define SBE_2T3E3_LOG_LEVEL_INFO			3

/* commands */
#define SBE_2T3E3_PORT_GET				0
#define SBE_2T3E3_PORT_SET				1
#define SBE_2T3E3_PORT_GET_STATS			2
#define SBE_2T3E3_PORT_DEL_STATS			3
#define SBE_2T3E3_PORT_READ_REGS			4
#define SBE_2T3E3_LOG_LEVEL				5
#define SBE_2T3E3_PORT_WRITE_REGS			6

#define NG_SBE_2T3E3_NODE_TYPE  "sbe2T3E3"
#define NG_SBE_2T3E3_COOKIE     0x03800891

struct t3e3_param {
	u_int8_t frame_mode;		/* FRAME_MODE_* */
	u_int8_t crc;			/* CRC_* */
	u_int8_t receiver_on;		/* ON/OFF */
	u_int8_t transmitter_on;	/* ON/OFF */
	u_int8_t frame_type;		/* FRAME_TYPE_* */
	u_int8_t panel;			/* PANEL_* */
	u_int8_t line_build_out;	/* ON/OFF */
	u_int8_t receive_equalization;	/* ON/OFF */
	u_int8_t transmit_all_ones;	/* ON/OFF */
	u_int8_t loopback;		/* LOOPBACK_* */
	u_int8_t clock_source;		/* TIMING_* */
	u_int8_t scrambler;		/* SCRAMBLER_* */
	u_int8_t pad_count;		/* PAD_COUNT_* */
	u_int8_t log_level;		/* LOG_LEVEL_* - unused */
	u_int8_t fractional_mode;	/* FRACTIONAL_MODE_* */
	u_int8_t bandwidth_start;	/* 0-255 */
	u_int8_t bandwidth_stop;	/* 0-255 */
};

struct t3e3_stats {
	u_int64_t in_bytes;
	u32 in_packets, in_dropped;
	u32 in_errors, in_error_desc, in_error_coll, in_error_drib,
		in_error_crc, in_error_mii;
	u_int64_t out_bytes;
	u32 out_packets, out_dropped;
	u32 out_errors, out_error_jab, out_error_lost_carr,
		out_error_no_carr, out_error_link_fail, out_error_underflow,
		out_error_dereferred;
	u_int8_t LOC, LOF, OOF, LOS, AIS, FERF, IDLE, AIC, FEAC;
	u_int16_t FEBE_code;
	u32 LCV, FRAMING_BIT, PARITY_ERROR, FEBE_count, CP_BIT;
};


struct t3e3_resp {
	union {
		struct t3e3_param param;
		struct t3e3_stats stats;
		u32 data;
	} u;
};

#endif /* CTRL_H */
