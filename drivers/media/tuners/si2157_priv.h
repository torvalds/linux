/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Silicon Labs Si2146/2147/2148/2157/2158 silicon tuner driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 */

#ifndef SI2157_PRIV_H
#define SI2157_PRIV_H

#include <linux/firmware.h>
#include <media/v4l2-mc.h>
#include "si2157.h"

enum si2157_pads {
	SI2157_PAD_RF_INPUT,
	SI2157_PAD_VID_OUT,
	SI2157_PAD_AUD_OUT,
	SI2157_NUM_PADS
};

/* state struct */
struct si2157_dev {
	struct mutex i2c_mutex;
	struct dvb_frontend *fe;
	unsigned int active:1;
	unsigned int inversion:1;
	unsigned int dont_load_firmware:1;
	u8 part_id;
	u8 if_port;
	u32 if_frequency;
	u32 bandwidth;
	u32 frequency;
	struct delayed_work stat_work;

#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_device	*mdev;
	struct media_entity	ent;
	struct media_pad	pad[SI2157_NUM_PADS];
#endif

};

enum si2157_part_id {
	SI2141 = 41,
	SI2146 = 46,
	SI2147 = 47,
	SI2148 = 48,
	SI2157 = 57,
	SI2158 = 58,
	SI2177 = 77,
};

struct si2157_tuner_info {
	enum si2157_part_id	part_id;
	unsigned char		rom_id;
	bool			required;
	const char		*fw_name, *fw_alt_name;
};

/* firmware command struct */
#define SI2157_ARGLEN      30
struct si2157_cmd {
	u8 args[SI2157_ARGLEN];
	unsigned wlen;
	unsigned rlen;
};

#define SUPPORTS_1700KHz(dev) (((dev)->part_id == SI2141) || \
			       ((dev)->part_id == SI2147) || \
			       ((dev)->part_id == SI2157) || \
			       ((dev)->part_id == SI2177))

#define SUPPORTS_ATV_IF(dev) (((dev)->part_id == SI2157) || \
			      ((dev)->part_id == SI2158))

/* Old firmware namespace */
#define SI2158_A20_FIRMWARE "dvb-tuner-si2158-a20-01.fw"
#define SI2141_A10_FIRMWARE "dvb-tuner-si2141-a10-01.fw"
#define SI2157_A30_FIRMWARE "dvb-tuner-si2157-a30-01.fw"

/* New firmware namespace */
#define SI2141_60_FIRMWARE "dvb_driver_si2141_rom60.fw"
#define SI2141_61_FIRMWARE "dvb_driver_si2141_rom61.fw"
#define SI2146_11_FIRMWARE "dvb_driver_si2146_rom11.fw"
#define SI2147_50_FIRMWARE "dvb_driver_si2147_rom50.fw"
#define SI2148_32_FIRMWARE "dvb_driver_si2148_rom32.fw"
#define SI2148_33_FIRMWARE "dvb_driver_si2148_rom33.fw"
#define SI2157_50_FIRMWARE "dvb_driver_si2157_rom50.fw"
#define SI2158_50_FIRMWARE "dvb_driver_si2178_rom50.fw"
#define SI2158_51_FIRMWARE "dvb_driver_si2158_rom51.fw"
#define SI2177_50_FIRMWARE "dvb_driver_si2177_rom50.fw"

#endif
