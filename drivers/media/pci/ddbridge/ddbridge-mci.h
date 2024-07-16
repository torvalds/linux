/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ddbridge-mci.h: Digital Devices micro code interface
 *
 * Copyright (C) 2017-2018 Digital Devices GmbH
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 */

#ifndef _DDBRIDGE_MCI_H_
#define _DDBRIDGE_MCI_H_

#define MCI_DEMOD_MAX                       8
#define MCI_TUNER_MAX                       4
#define DEMOD_UNUSED                        (0xFF)

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

/*
 * IQMode is only available on MaxSX8 on a single tuner
 *
 * IQ_MODE_SAMPLES
 *       sampling rate is 1550/24 MHz (64.583 MHz)
 *       channel agc is frozen, to allow stitching the FFT results together
 *
 * IQ_MODE_VTM
 *       sampling rate is the supplied symbolrate
 *       channel agc is active
 *
 * in both cases down sampling is done with a RRC Filter (currently fixed to
 * alpha = 0.05) which causes some (ca 5%) aliasing at the edges from
 * outside the spectrum
 */

#define SX8_TSCONFIG_TSHEADER               (0x00000004)
#define SX8_TSCONFIG_BURST                  (0x00000008)

#define SX8_TSCONFIG_BURSTSIZE_MASK         (0x00000030)
#define SX8_TSCONFIG_BURSTSIZE_2K           (0x00000000)
#define SX8_TSCONFIG_BURSTSIZE_4K           (0x00000010)
#define SX8_TSCONFIG_BURSTSIZE_8K           (0x00000020)
#define SX8_TSCONFIG_BURSTSIZE_16K          (0x00000030)

#define SX8_DEMOD_STOPPED        (0)
#define SX8_DEMOD_IQ_MODE        (1)
#define SX8_DEMOD_WAIT_SIGNAL    (2)
#define SX8_DEMOD_WAIT_MATYPE    (3)
#define SX8_DEMOD_TIMEOUT        (14)
#define SX8_DEMOD_LOCKED         (15)

#define MCI_CMD_STOP             (0x01)
#define MCI_CMD_GETSTATUS        (0x02)
#define MCI_CMD_GETSIGNALINFO    (0x03)
#define MCI_CMD_RFPOWER          (0x04)

#define MCI_CMD_SEARCH_DVBS      (0x10)

#define MCI_CMD_GET_IQSYMBOL     (0x30)

#define SX8_CMD_INPUT_ENABLE     (0x40)
#define SX8_CMD_INPUT_DISABLE    (0x41)
#define SX8_CMD_START_IQ         (0x42)
#define SX8_CMD_STOP_IQ          (0x43)
#define SX8_CMD_ENABLE_IQOUTPUT  (0x44)
#define SX8_CMD_DISABLE_IQOUTPUT (0x45)

#define MCI_STATUS_OK            (0x00)
#define MCI_STATUS_UNSUPPORTED   (0x80)
#define MCI_STATUS_RETRY         (0xFD)
#define MCI_STATUS_NOT_READY     (0xFE)
#define MCI_STATUS_ERROR         (0xFF)

#define MCI_SUCCESS(status)      ((status & MCI_STATUS_UNSUPPORTED) == 0)

struct mci_command {
	union {
		u32 command_word;
		struct {
			u8  command;
			u8  tuner;
			u8  demod;
			u8  output;
		};
	};
	union {
		u32 params[31];
		struct {
			/*
			 * Bit 0: DVB-S Enabled
			 * Bit 1: DVB-S2 Enabled
			 * Bit 7: InputStreamID
			 */
			u8  flags;
			/*
			 * Bit 0: QPSK,
			 * Bit 1: 8PSK/8APSK
			 * Bit 2: 16APSK
			 * Bit 3: 32APSK
			 * Bit 4: 64APSK
			 * Bit 5: 128APSK
			 * Bit 6: 256APSK
			 */
			u8  s2_modulation_mask;
			u8  rsvd1;
			u8  retry;
			u32 frequency;
			u32 symbol_rate;
			u8  input_stream_id;
			u8  rsvd2[3];
			u32 scrambling_sequence_index;
			u32 frequency_range;
		} dvbs2_search;

		struct {
			u8  tap;
			u8  rsvd;
			u16 point;
		} get_iq_symbol;

		struct {
			/*
			 * Bit 0: 0=VTM/1=SCAN
			 * Bit 1: Set Gain
			 */
			u8  flags;
			u8  roll_off;
			u8  rsvd1;
			u8  rsvd2;
			u32 frequency;
			u32 symbol_rate; /* Only in VTM mode */
			u16 gain;
		} sx8_start_iq;

		struct {
			/*
			 * Bit 1:0 = STVVGLNA Gain.
			 *   0 = AGC, 1 = 0dB, 2 = Minimum, 3 = Maximum
			 */
			u8  flags;
		} sx8_input_enable;
	};
};

struct mci_result {
	union {
		u32 status_word;
		struct {
			u8  status;
			u8  mode;
			u16 time;
		};
	};
	union {
		u32 result[27];
		struct {
			/* 1 = DVB-S, 2 = DVB-S2X */
			u8  standard;
			/* puncture rate for DVB-S */
			u8  pls_code;
			/* 2-0: rolloff */
			u8  roll_off;
			u8  rsvd;
			/* actual frequency in Hz */
			u32 frequency;
			/* actual symbolrate in Hz */
			u32 symbol_rate;
			/* channel power in dBm x 100 */
			s16 channel_power;
			/* band power in dBm x 100 */
			s16 band_power;
			/*
			 * SNR in dB x 100
			 * Note: negative values are valid in DVB-S2
			 */
			s16 signal_to_noise;
			s16 rsvd2;
			/*
			 * Counter for packet errors
			 * (set to 0 on start command)
			 */
			u32 packet_errors;
			/* Bit error rate: PreRS in DVB-S, PreBCH in DVB-S2X */
			u32 ber_numerator;
			u32 ber_denominator;
		} dvbs2_signal_info;

		struct {
			s16 i;
			s16 q;
		} iq_symbol;
	};
	u32 version[4];
};

struct mci_base {
	struct list_head     mci_list;
	void                *key;
	struct ddb_link     *link;
	struct completion    completion;
	struct device       *dev;
	struct mutex         tuner_lock; /* concurrent tuner access lock */
	struct mutex         mci_lock; /* concurrent MCI access lock */
	int                  count;
	int                  type;
};

struct mci {
	struct mci_base     *base;
	struct dvb_frontend  fe;
	int                  nr;
	int                  demod;
	int                  tuner;
};

struct mci_cfg {
	int                  type;
	struct dvb_frontend_ops *fe_ops;
	u32                  base_size;
	u32                  state_size;
	int (*init)(struct mci *mci);
	int (*base_init)(struct mci_base *mci_base);
	int (*set_input)(struct dvb_frontend *fe, int input);
};

/* defined in ddbridge-sx8.c */
extern const struct mci_cfg ddb_max_sx8_cfg;

int ddb_mci_cmd(struct mci *state, struct mci_command *command,
		struct mci_result *result);
int ddb_mci_config(struct mci *state, u32 config);

struct dvb_frontend
*ddb_mci_attach(struct ddb_input *input, struct mci_cfg *cfg, int nr,
		int (**fn_set_input)(struct dvb_frontend *fe, int input));

#endif /* _DDBRIDGE_MCI_H_ */
