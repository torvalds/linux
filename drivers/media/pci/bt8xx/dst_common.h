/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Frontend-driver for TwinHan DST Frontend

	Copyright (C) 2003 Jamie Honan
	Copyright (C) 2004, 2005 Manu Abraham (manu@kromtek.com)

*/

#ifndef DST_COMMON_H
#define DST_COMMON_H

#include <linux/dvb/frontend.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include "bt878.h"

#include "dst_ca.h"


#define NO_DELAY		0
#define LONG_DELAY		1
#define DEVICE_INIT		2

#define DELAY			1

#define DST_TYPE_IS_SAT		0
#define DST_TYPE_IS_TERR	1
#define DST_TYPE_IS_CABLE	2
#define DST_TYPE_IS_ATSC	3

#define DST_TYPE_HAS_TS188	1
#define DST_TYPE_HAS_TS204	2
#define DST_TYPE_HAS_SYMDIV	4
#define DST_TYPE_HAS_FW_1	8
#define DST_TYPE_HAS_FW_2	16
#define DST_TYPE_HAS_FW_3	32
#define DST_TYPE_HAS_FW_BUILD	64
#define DST_TYPE_HAS_OBS_REGS	128
#define DST_TYPE_HAS_INC_COUNT	256
#define DST_TYPE_HAS_MULTI_FE	512
#define DST_TYPE_HAS_NEWTUNE_2	1024
#define DST_TYPE_HAS_DBOARD	2048
#define DST_TYPE_HAS_VLF	4096

/*	Card capability list	*/

#define DST_TYPE_HAS_MAC	1
#define DST_TYPE_HAS_DISEQC3	2
#define DST_TYPE_HAS_DISEQC4	4
#define DST_TYPE_HAS_DISEQC5	8
#define DST_TYPE_HAS_MOTO	16
#define DST_TYPE_HAS_CA		32
#define	DST_TYPE_HAS_ANALOG	64	/*	Analog inputs	*/
#define DST_TYPE_HAS_SESSION	128

#define TUNER_TYPE_MULTI	1
#define TUNER_TYPE_UNKNOWN	2
/*	DVB-S		*/
#define TUNER_TYPE_L64724	4
#define TUNER_TYPE_STV0299	8
#define TUNER_TYPE_MB86A15	16

/*	DVB-T		*/
#define TUNER_TYPE_TDA10046	32

/*	ATSC		*/
#define TUNER_TYPE_NXT200x	64


#define RDC_8820_PIO_0_DISABLE	0
#define RDC_8820_PIO_0_ENABLE	1
#define RDC_8820_INT		2
#define RDC_8820_RESET		4

/*	DST Communication	*/
#define GET_REPLY		1
#define NO_REPLY		0

#define GET_ACK			1
#define FIXED_COMM		8

#define ACK			0xff

struct dst_state {

	struct i2c_adapter* i2c;

	struct bt878* bt;

	/* configuration settings */
	const struct dst_config* config;

	struct dvb_frontend frontend;

	/* private ASIC data */
	u8 tx_tuna[10];
	u8 rx_tuna[10];
	u8 rxbuffer[10];
	u8 diseq_flags;
	u8 dst_type;
	u32 type_flags;
	u32 frequency;		/* intermediate frequency in kHz for QPSK */
	enum fe_spectral_inversion inversion;
	u32 symbol_rate;	/* symbol rate in Symbols per second */
	enum fe_code_rate fec;
	enum fe_sec_voltage voltage;
	enum fe_sec_tone_mode tone;
	u32 decode_freq;
	u8 decode_lock;
	u16 decode_strength;
	u16 decode_snr;
	unsigned long cur_jiff;
	u8 k22;
	u32 bandwidth;
	u32 dst_hw_cap;
	u8 dst_fw_version;
	enum fe_sec_mini_cmd minicmd;
	enum fe_modulation modulation;
	u8 messages[256];
	u8 mac_address[8];
	u8 fw_version[8];
	u8 card_info[8];
	u8 vendor[8];
	u8 board_info[8];
	u32 tuner_type;
	char *tuner_name;
	struct mutex dst_mutex;
	char fw_name[8];
	struct dvb_device *dst_ca;
};

struct tuner_types {
	u32 tuner_type;
	char *tuner_name;
	char *board_name;
	char *fw_name;
};

struct dst_types {
	char *device_id;
	int offset;
	u8 dst_type;
	u32 type_flags;
	u32 dst_feature;
	u32 tuner_type;
};

struct dst_config
{
	/* the ASIC i2c address */
	u8 demod_address;
};

int rdc_reset_state(struct dst_state *state);

int dst_wait_dst_ready(struct dst_state *state, u8 delay_mode);
int dst_pio_disable(struct dst_state *state);
int dst_error_recovery(struct dst_state* state);
int dst_error_bailout(struct dst_state *state);
int dst_comm_init(struct dst_state* state);

int write_dst(struct dst_state *state, u8 * data, u8 len);
int read_dst(struct dst_state *state, u8 * ret, u8 len);
u8 dst_check_sum(u8 * buf, u32 len);
struct dst_state* dst_attach(struct dst_state* state, struct dvb_adapter *dvb_adapter);
struct dvb_device *dst_ca_attach(struct dst_state *state, struct dvb_adapter *dvb_adapter);


#endif // DST_COMMON_H
