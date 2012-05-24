/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef _AS10X_TYPES_H_
#define _AS10X_TYPES_H_

#include "as10x_handle.h"

/*********************************/
/*       MACRO DEFINITIONS       */
/*********************************/

/* bandwidth constant values */
#define BW_5_MHZ		0x00
#define BW_6_MHZ		0x01
#define BW_7_MHZ		0x02
#define BW_8_MHZ		0x03

/* hierarchy priority selection values */
#define HIER_NO_PRIORITY	0x00
#define HIER_LOW_PRIORITY	0x01
#define HIER_HIGH_PRIORITY	0x02

/* constellation available values */
#define CONST_QPSK		0x00
#define CONST_QAM16		0x01
#define CONST_QAM64		0x02
#define CONST_UNKNOWN		0xFF

/* hierarchy available values */
#define HIER_NONE		0x00
#define HIER_ALPHA_1		0x01
#define HIER_ALPHA_2		0x02
#define HIER_ALPHA_4		0x03
#define HIER_UNKNOWN		0xFF

/* interleaving available values */
#define INTLV_NATIVE		0x00
#define INTLV_IN_DEPTH		0x01
#define INTLV_UNKNOWN		0xFF

/* code rate available values */
#define CODE_RATE_1_2		0x00
#define CODE_RATE_2_3		0x01
#define CODE_RATE_3_4		0x02
#define CODE_RATE_5_6		0x03
#define CODE_RATE_7_8		0x04
#define CODE_RATE_UNKNOWN	0xFF

/* guard interval available values */
#define GUARD_INT_1_32		0x00
#define GUARD_INT_1_16		0x01
#define GUARD_INT_1_8		0x02
#define GUARD_INT_1_4		0x03
#define GUARD_UNKNOWN		0xFF

/* transmission mode available values */
#define TRANS_MODE_2K		0x00
#define TRANS_MODE_8K		0x01
#define TRANS_MODE_4K		0x02
#define TRANS_MODE_UNKNOWN	0xFF

/* DVBH signalling available values */
#define TIMESLICING_PRESENT	0x01
#define MPE_FEC_PRESENT		0x02

/* tune state available */
#define TUNE_STATUS_NOT_TUNED		0x00
#define TUNE_STATUS_IDLE		0x01
#define TUNE_STATUS_LOCKING		0x02
#define TUNE_STATUS_SIGNAL_DVB_OK	0x03
#define TUNE_STATUS_STREAM_DETECTED	0x04
#define TUNE_STATUS_STREAM_TUNED	0x05
#define TUNE_STATUS_ERROR		0xFF

/* available TS FID filter types */
#define TS_PID_TYPE_TS		0
#define TS_PID_TYPE_PSI_SI	1
#define TS_PID_TYPE_MPE		2

/* number of echos available */
#define MAX_ECHOS	15

/* Context types */
#define CONTEXT_LNA			1010
#define CONTEXT_ELNA_HYSTERESIS		4003
#define CONTEXT_ELNA_GAIN		4004
#define CONTEXT_MER_THRESHOLD		5005
#define CONTEXT_MER_OFFSET		5006
#define CONTEXT_IR_STATE		7000
#define CONTEXT_TSOUT_MSB_FIRST		7004
#define CONTEXT_TSOUT_FALLING_EDGE	7005

/* Configuration modes */
#define CFG_MODE_ON	0
#define CFG_MODE_OFF	1
#define CFG_MODE_AUTO	2

struct as10x_tps {
	uint8_t modulation;
	uint8_t hierarchy;
	uint8_t interleaving_mode;
	uint8_t code_rate_HP;
	uint8_t code_rate_LP;
	uint8_t guard_interval;
	uint8_t transmission_mode;
	uint8_t DVBH_mask_HP;
	uint8_t DVBH_mask_LP;
	uint16_t cell_ID;
} __packed;

struct as10x_tune_args {
	/* frequency */
	uint32_t freq;
	/* bandwidth */
	uint8_t bandwidth;
	/* hierarchy selection */
	uint8_t hier_select;
	/* constellation */
	uint8_t modulation;
	/* hierarchy */
	uint8_t hierarchy;
	/* interleaving mode */
	uint8_t interleaving_mode;
	/* code rate */
	uint8_t code_rate;
	/* guard interval */
	uint8_t guard_interval;
	/* transmission mode */
	uint8_t transmission_mode;
} __packed;

struct as10x_tune_status {
	/* tune status */
	uint8_t tune_state;
	/* signal strength */
	int16_t signal_strength;
	/* packet error rate 10^-4 */
	uint16_t PER;
	/* bit error rate 10^-4 */
	uint16_t BER;
} __packed;

struct as10x_demod_stats {
	/* frame counter */
	uint32_t frame_count;
	/* Bad frame counter */
	uint32_t bad_frame_count;
	/* Number of wrong bytes fixed by Reed-Solomon */
	uint32_t bytes_fixed_by_rs;
	/* Averaged MER */
	uint16_t mer;
	/* statistics calculation state indicator (started or not) */
	uint8_t has_started;
} __packed;

struct as10x_ts_filter {
	uint16_t pid;  /* valid PID value 0x00 : 0x2000 */
	uint8_t  type; /* Red TS_PID_TYPE_<N> values */
	uint8_t  idx;  /* index in filtering table */
} __packed;

struct as10x_register_value {
	uint8_t mode;
	union {
		uint8_t  value8;   /* 8 bit value */
		uint16_t value16;  /* 16 bit value */
		uint32_t value32;  /* 32 bit value */
	} __packed u;
} __packed;

struct as10x_register_addr {
	/* register addr */
	uint32_t addr;
	/* register mode access */
	uint8_t mode;
};

#endif
