/*
	Frontend-driver for TwinHan DST Frontend

	Copyright (C) 2003 Jamie Honan
	Copyright (C) 2004, 2005 Manu Abraham (manu@kromtek.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef DST_COMMON_H
#define DST_COMMON_H

#include <linux/smp_lock.h>
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

#define DST_TYPE_HAS_NEWTUNE	1
#define DST_TYPE_HAS_TS204	2
#define DST_TYPE_HAS_SYMDIV	4
#define DST_TYPE_HAS_FW_1	8
#define DST_TYPE_HAS_FW_2	16
#define DST_TYPE_HAS_FW_3	32
#define DST_TYPE_HAS_FW_BUILD	64
#define DST_TYPE_HAS_OBS_REGS	128
#define DST_TYPE_HAS_INC_COUNT	256
#define DST_TYPE_HAS_MULTI_FE	512

/*	Card capability list	*/

#define DST_TYPE_HAS_MAC	1
#define DST_TYPE_HAS_DISEQC3	2
#define DST_TYPE_HAS_DISEQC4	4
#define DST_TYPE_HAS_DISEQC5	8
#define DST_TYPE_HAS_MOTO	16
#define DST_TYPE_HAS_CA		32
#define	DST_TYPE_HAS_ANALOG	64	/*	Analog inputs	*/
#define DST_TYPE_HAS_SESSION	128

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
	fe_spectral_inversion_t inversion;
	u32 symbol_rate;	/* symbol rate in Symbols per second */
	fe_code_rate_t fec;
	fe_sec_voltage_t voltage;
	fe_sec_tone_mode_t tone;
	u32 decode_freq;
	u8 decode_lock;
	u16 decode_strength;
	u16 decode_snr;
	unsigned long cur_jiff;
	u8 k22;
	fe_bandwidth_t bandwidth;
	u32 dst_hw_cap;
	u8 dst_fw_version;
	fe_sec_mini_cmd_t minicmd;
	fe_modulation_t modulation;
	u8 messages[256];
	u8 mac_address[8];
	u8 fw_version[8];
	u8 card_info[8];
	u8 vendor[8];
	u8 board_info[8];

	struct mutex dst_mutex;
};

struct dst_types {
	char *device_id;
	int offset;
	u8 dst_type;
	u32 type_flags;
	u32 dst_feature;
};

struct dst_config
{
	/* the ASIC i2c address */
	u8 demod_address;
};

int rdc_reset_state(struct dst_state *state);
int rdc_8820_reset(struct dst_state *state);

int dst_wait_dst_ready(struct dst_state *state, u8 delay_mode);
int dst_pio_enable(struct dst_state *state);
int dst_pio_disable(struct dst_state *state);
int dst_error_recovery(struct dst_state* state);
int dst_error_bailout(struct dst_state *state);
int dst_comm_init(struct dst_state* state);

int write_dst(struct dst_state *state, u8 * data, u8 len);
int read_dst(struct dst_state *state, u8 * ret, u8 len);
u8 dst_check_sum(u8 * buf, u32 len);
struct dst_state* dst_attach(struct dst_state* state, struct dvb_adapter *dvb_adapter);
int dst_ca_attach(struct dst_state *state, struct dvb_adapter *dvb_adapter);
int dst_gpio_outb(struct dst_state* state, u32 mask, u32 enbb, u32 outhigh, int delay);

int dst_command(struct dst_state* state, u8 * data, u8 len);


#endif // DST_COMMON_H
