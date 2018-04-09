/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ddbridge-mci.h: Digital Devices micro code interface
 *
 * Copyright (C) 2017 Digital Devices GmbH
 *                    Marcus Metzler <mocm@metzlerbros.de>
 *                    Ralph Metzler <rjkm@metzlerbros.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _DDBRIDGE_MCI_H_
#define _DDBRIDGE_MCI_H_

#define MCI_CONTROL                         (0x500)
#define MCI_COMMAND                         (0x600)
#define MCI_RESULT                          (0x680)

#define MCI_COMMAND_SIZE                    (0x80)
#define MCI_RESULT_SIZE                     (0x80)

#define MCI_CONTROL_START_COMMAND           (0x00000001)
#define MCI_CONTROL_ENABLE_DONE_INTERRUPT   (0x00000002)
#define MCI_CONTROL_RESET                   (0x00008000)
#define MCI_CONTROL_READY                   (0x00010000)

#define SX8_TSCONFIG                        (0x280)

#define SX8_TSCONFIG_MODE_MASK              (0x00000003)
#define SX8_TSCONFIG_MODE_OFF               (0x00000000)
#define SX8_TSCONFIG_MODE_NORMAL            (0x00000001)
#define SX8_TSCONFIG_MODE_IQ                (0x00000003)

#define SX8_TSCONFIG_TSHEADER               (0x00000004)
#define SX8_TSCONFIG_BURST                  (0x00000008)

#define SX8_TSCONFIG_BURSTSIZE_MASK         (0x00000030)
#define SX8_TSCONFIG_BURSTSIZE_2K           (0x00000000)
#define SX8_TSCONFIG_BURSTSIZE_4K           (0x00000010)
#define SX8_TSCONFIG_BURSTSIZE_8K           (0x00000020)
#define SX8_TSCONFIG_BURSTSIZE_16K          (0x00000030)

#define SX8_DEMOD_STOPPED       (0)
#define SX8_DEMOD_IQ_MODE       (1)
#define SX8_DEMOD_WAIT_SIGNAL   (2)
#define SX8_DEMOD_WAIT_MATYPE   (3)
#define SX8_DEMOD_TIMEOUT       (14)
#define SX8_DEMOD_LOCKED        (15)

#define MCI_CMD_STOP            (0x01)
#define MCI_CMD_GETSTATUS       (0x02)
#define MCI_CMD_GETSIGNALINFO   (0x03)
#define MCI_CMD_RFPOWER         (0x04)

#define MCI_CMD_SEARCH_DVBS     (0x10)

#define MCI_CMD_GET_IQSYMBOL    (0x30)

#define SX8_CMD_INPUT_ENABLE    (0x40)
#define SX8_CMD_INPUT_DISABLE   (0x41)
#define SX8_CMD_START_IQ        (0x42)
#define SX8_CMD_STOP_IQ         (0x43)
#define SX8_CMD_SELECT_IQOUT    (0x44)
#define SX8_CMD_SELECT_TSOUT    (0x45)

#define SX8_ERROR_UNSUPPORTED   (0x80)

#define SX8_SUCCESS(status)     (status < SX8_ERROR_UNSUPPORTED)

#define SX8_CMD_DIAG_READ8      (0xE0)
#define SX8_CMD_DIAG_READ32     (0xE1)
#define SX8_CMD_DIAG_WRITE8     (0xE2)
#define SX8_CMD_DIAG_WRITE32    (0xE3)

#define SX8_CMD_DIAG_READRF     (0xE8)
#define SX8_CMD_DIAG_WRITERF    (0xE9)

struct mci_command {
	union {
		u32 command_word;
		struct {
			u8 command;
			u8 tuner;
			u8 demod;
			u8 output;
		};
	};
	union {
		u32 params[31];
		struct {
			u8  flags;
			u8  s2_modulation_mask;
			u8  rsvd1;
			u8  retry;
			u32 frequency;
			u32 symbol_rate;
			u8  input_stream_id;
			u8  rsvd2[3];
			u32 scrambling_sequence_index;
		} dvbs2_search;
	};
};

struct mci_result {
	union {
		u32 status_word;
		struct {
			u8 status;
			u8 rsvd;
			u16 time;
		};
	};
	union {
		u32 result[27];
		struct {
			u8  standard;
			/* puncture rate for DVB-S */
			u8  pls_code;
			/* 7-6: rolloff, 5-2: rsrvd, 1:short, 0:pilots */
			u8  roll_off;
			u8  rsvd;
			u32 frequency;
			u32 symbol_rate;
			s16 channel_power;
			s16 band_power;
			s16 signal_to_noise;
			s16 rsvd2;
			u32 packet_errors;
			u32 ber_numerator;
			u32 ber_denominator;
		} dvbs2_signal_info;
		struct {
			u8 i_symbol;
			u8 q_symbol;
		} dvbs2_signal_iq;
	};
	u32 version[4];
};

struct dvb_frontend
*ddb_mci_attach(struct ddb_input *input,
		int mci_type, int nr,
		int (**fn_set_input)(struct dvb_frontend *, int));

#endif /* _DDBRIDGE_MCI_H_ */
