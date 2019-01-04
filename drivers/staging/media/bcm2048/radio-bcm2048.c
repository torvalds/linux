/*
 * drivers/staging/media/radio-bcm2048.c
 *
 * Driver for I2C Broadcom BCM2048 FM Radio Receiver:
 *
 * Copyright (C) Nokia Corporation
 * Contact: Eero Nurkkala <ext-eero.nurkkala@nokia.com>
 *
 * Copyright (C) Nils Faerber <nils.faerber@kernelconcepts.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

/*
 * History:
 *		Eero Nurkkala <ext-eero.nurkkala@nokia.com>
 *		Version 0.0.1
 *		- Initial implementation
 * 2010-02-21	Nils Faerber <nils.faerber@kernelconcepts.de>
 *		Version 0.0.2
 *		- Add support for interrupt driven rds data reading
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include "radio-bcm2048.h"

/* driver definitions */
#define BCM2048_DRIVER_AUTHOR	"Eero Nurkkala <ext-eero.nurkkala@nokia.com>"
#define BCM2048_DRIVER_NAME	BCM2048_NAME
#define BCM2048_DRIVER_CARD	"Broadcom bcm2048 FM Radio Receiver"
#define BCM2048_DRIVER_DESC	"I2C driver for BCM2048 FM Radio Receiver"

/* I2C Control Registers */
#define BCM2048_I2C_FM_RDS_SYSTEM	0x00
#define BCM2048_I2C_FM_CTRL		0x01
#define BCM2048_I2C_RDS_CTRL0		0x02
#define BCM2048_I2C_RDS_CTRL1		0x03
#define BCM2048_I2C_FM_AUDIO_PAUSE	0x04
#define BCM2048_I2C_FM_AUDIO_CTRL0	0x05
#define BCM2048_I2C_FM_AUDIO_CTRL1	0x06
#define BCM2048_I2C_FM_SEARCH_CTRL0	0x07
#define BCM2048_I2C_FM_SEARCH_CTRL1	0x08
#define BCM2048_I2C_FM_SEARCH_TUNE_MODE	0x09
#define BCM2048_I2C_FM_FREQ0		0x0a
#define BCM2048_I2C_FM_FREQ1		0x0b
#define BCM2048_I2C_FM_AF_FREQ0		0x0c
#define BCM2048_I2C_FM_AF_FREQ1		0x0d
#define BCM2048_I2C_FM_CARRIER		0x0e
#define BCM2048_I2C_FM_RSSI		0x0f
#define BCM2048_I2C_FM_RDS_MASK0	0x10
#define BCM2048_I2C_FM_RDS_MASK1	0x11
#define BCM2048_I2C_FM_RDS_FLAG0	0x12
#define BCM2048_I2C_FM_RDS_FLAG1	0x13
#define BCM2048_I2C_RDS_WLINE		0x14
#define BCM2048_I2C_RDS_BLKB_MATCH0	0x16
#define BCM2048_I2C_RDS_BLKB_MATCH1	0x17
#define BCM2048_I2C_RDS_BLKB_MASK0	0x18
#define BCM2048_I2C_RDS_BLKB_MASK1	0x19
#define BCM2048_I2C_RDS_PI_MATCH0	0x1a
#define BCM2048_I2C_RDS_PI_MATCH1	0x1b
#define BCM2048_I2C_RDS_PI_MASK0	0x1c
#define BCM2048_I2C_RDS_PI_MASK1	0x1d
#define BCM2048_I2C_SPARE1		0x20
#define BCM2048_I2C_SPARE2		0x21
#define BCM2048_I2C_FM_RDS_REV		0x28
#define BCM2048_I2C_SLAVE_CONFIGURATION	0x29
#define BCM2048_I2C_RDS_DATA		0x80
#define BCM2048_I2C_FM_BEST_TUNE_MODE	0x90

/* BCM2048_I2C_FM_RDS_SYSTEM */
#define BCM2048_FM_ON			0x01
#define BCM2048_RDS_ON			0x02

/* BCM2048_I2C_FM_CTRL */
#define BCM2048_BAND_SELECT			0x01
#define BCM2048_STEREO_MONO_AUTO_SELECT		0x02
#define BCM2048_STEREO_MONO_MANUAL_SELECT	0x04
#define BCM2048_STEREO_MONO_BLEND_SWITCH	0x08
#define BCM2048_HI_LO_INJECTION			0x10

/* BCM2048_I2C_RDS_CTRL0 */
#define BCM2048_RBDS_RDS_SELECT		0x01
#define BCM2048_FLUSH_FIFO		0x02

/* BCM2048_I2C_FM_AUDIO_PAUSE */
#define BCM2048_AUDIO_PAUSE_RSSI_TRESH	0x0f
#define BCM2048_AUDIO_PAUSE_DURATION	0xf0

/* BCM2048_I2C_FM_AUDIO_CTRL0 */
#define BCM2048_RF_MUTE			0x01
#define BCM2048_MANUAL_MUTE		0x02
#define BCM2048_DAC_OUTPUT_LEFT		0x04
#define BCM2048_DAC_OUTPUT_RIGHT	0x08
#define BCM2048_AUDIO_ROUTE_DAC		0x10
#define BCM2048_AUDIO_ROUTE_I2S		0x20
#define BCM2048_DE_EMPHASIS_SELECT	0x40
#define BCM2048_AUDIO_BANDWIDTH_SELECT	0x80

/* BCM2048_I2C_FM_SEARCH_CTRL0 */
#define BCM2048_SEARCH_RSSI_THRESHOLD	0x7f
#define BCM2048_SEARCH_DIRECTION	0x80

/* BCM2048_I2C_FM_SEARCH_TUNE_MODE */
#define BCM2048_FM_AUTO_SEARCH		0x03

/* BCM2048_I2C_FM_RSSI */
#define BCM2048_RSSI_VALUE		0xff

/* BCM2048_I2C_FM_RDS_MASK0 */
/* BCM2048_I2C_FM_RDS_MASK1 */
#define BCM2048_FM_FLAG_SEARCH_TUNE_FINISHED	0x01
#define BCM2048_FM_FLAG_SEARCH_TUNE_FAIL	0x02
#define BCM2048_FM_FLAG_RSSI_LOW		0x04
#define BCM2048_FM_FLAG_CARRIER_ERROR_HIGH	0x08
#define BCM2048_FM_FLAG_AUDIO_PAUSE_INDICATION	0x10
#define BCM2048_FLAG_STEREO_DETECTED		0x20
#define BCM2048_FLAG_STEREO_ACTIVE		0x40

/* BCM2048_I2C_RDS_DATA */
#define BCM2048_SLAVE_ADDRESS			0x3f
#define BCM2048_SLAVE_ENABLE			0x80

/* BCM2048_I2C_FM_BEST_TUNE_MODE */
#define BCM2048_BEST_TUNE_MODE			0x80

#define BCM2048_FM_FLAG_SEARCH_TUNE_FINISHED	0x01
#define BCM2048_FM_FLAG_SEARCH_TUNE_FAIL	0x02
#define BCM2048_FM_FLAG_RSSI_LOW		0x04
#define BCM2048_FM_FLAG_CARRIER_ERROR_HIGH	0x08
#define BCM2048_FM_FLAG_AUDIO_PAUSE_INDICATION	0x10
#define BCM2048_FLAG_STEREO_DETECTED		0x20
#define BCM2048_FLAG_STEREO_ACTIVE		0x40

#define BCM2048_RDS_FLAG_FIFO_WLINE		0x02
#define BCM2048_RDS_FLAG_B_BLOCK_MATCH		0x08
#define BCM2048_RDS_FLAG_SYNC_LOST		0x10
#define BCM2048_RDS_FLAG_PI_MATCH		0x20

#define BCM2048_RDS_MARK_END_BYTE0		0x7C
#define BCM2048_RDS_MARK_END_BYTEN		0xFF

#define BCM2048_FM_FLAGS_ALL	(FM_FLAG_SEARCH_TUNE_FINISHED | \
				 FM_FLAG_SEARCH_TUNE_FAIL | \
				 FM_FLAG_RSSI_LOW | \
				 FM_FLAG_CARRIER_ERROR_HIGH | \
				 FM_FLAG_AUDIO_PAUSE_INDICATION | \
				 FLAG_STEREO_DETECTED | FLAG_STEREO_ACTIVE)

#define BCM2048_RDS_FLAGS_ALL	(RDS_FLAG_FIFO_WLINE | \
				 RDS_FLAG_B_BLOCK_MATCH | \
				 RDS_FLAG_SYNC_LOST | RDS_FLAG_PI_MATCH)

#define BCM2048_DEFAULT_TIMEOUT		1500
#define BCM2048_AUTO_SEARCH_TIMEOUT	3000

#define BCM2048_FREQDEV_UNIT		10000
#define BCM2048_FREQV4L2_MULTI		625
#define dev_to_v4l2(f)	(((f) * BCM2048_FREQDEV_UNIT) / BCM2048_FREQV4L2_MULTI)
#define v4l2_to_dev(f)	(((f) * BCM2048_FREQV4L2_MULTI) / BCM2048_FREQDEV_UNIT)

#define msb(x)                  ((u8)((u16)(x) >> 8))
#define lsb(x)                  ((u8)((u16)(x) &  0x00FF))
#define compose_u16(msb, lsb)	(((u16)(msb) << 8) | (lsb))

#define BCM2048_DEFAULT_POWERING_DELAY	20
#define BCM2048_DEFAULT_REGION		0x02
#define BCM2048_DEFAULT_MUTE		0x01
#define BCM2048_DEFAULT_RSSI_THRESHOLD	0x64
#define BCM2048_DEFAULT_RDS_WLINE	0x7E

#define BCM2048_FM_SEARCH_INACTIVE	0x00
#define BCM2048_FM_PRE_SET_MODE		0x01
#define BCM2048_FM_AUTO_SEARCH_MODE	0x02
#define BCM2048_FM_AF_JUMP_MODE		0x03

#define BCM2048_FREQUENCY_BASE		64000

#define BCM2048_POWER_ON		0x01
#define BCM2048_POWER_OFF		0x00

#define BCM2048_ITEM_ENABLED		0x01
#define BCM2048_SEARCH_DIRECTION_UP	0x01

#define BCM2048_DE_EMPHASIS_75us	75
#define BCM2048_DE_EMPHASIS_50us	50

#define BCM2048_SCAN_FAIL		0x00
#define BCM2048_SCAN_OK			0x01

#define BCM2048_FREQ_ERROR_FLOOR	-20
#define BCM2048_FREQ_ERROR_ROOF		20

/* -60 dB is reported as full signal strength */
#define BCM2048_RSSI_LEVEL_BASE		-60
#define BCM2048_RSSI_LEVEL_ROOF		-100
#define BCM2048_RSSI_LEVEL_ROOF_NEG	100
#define BCM2048_SIGNAL_MULTIPLIER	(0xFFFF / \
					 (BCM2048_RSSI_LEVEL_ROOF_NEG + \
					  BCM2048_RSSI_LEVEL_BASE))

#define BCM2048_RDS_FIFO_DUPLE_SIZE	0x03
#define BCM2048_RDS_CRC_MASK		0x0F
#define BCM2048_RDS_CRC_NONE		0x00
#define BCM2048_RDS_CRC_MAX_2BITS	0x04
#define BCM2048_RDS_CRC_LEAST_2BITS	0x08
#define BCM2048_RDS_CRC_UNRECOVARABLE	0x0C

#define BCM2048_RDS_BLOCK_MASK		0xF0
#define BCM2048_RDS_BLOCK_A		0x00
#define BCM2048_RDS_BLOCK_B		0x10
#define BCM2048_RDS_BLOCK_C		0x20
#define BCM2048_RDS_BLOCK_D		0x30
#define BCM2048_RDS_BLOCK_C_SCORED	0x40
#define BCM2048_RDS_BLOCK_E		0x60

#define BCM2048_RDS_RT			0x20
#define BCM2048_RDS_PS			0x00

#define BCM2048_RDS_GROUP_AB_MASK	0x08
#define BCM2048_RDS_GROUP_A		0x00
#define BCM2048_RDS_GROUP_B		0x08

#define BCM2048_RDS_RT_AB_MASK		0x10
#define BCM2048_RDS_RT_A		0x00
#define BCM2048_RDS_RT_B		0x10
#define BCM2048_RDS_RT_INDEX		0x0F

#define BCM2048_RDS_PS_INDEX		0x03

struct rds_info {
	u16 rds_pi;
#define BCM2048_MAX_RDS_RT (64 + 1)
	u8 rds_rt[BCM2048_MAX_RDS_RT];
	u8 rds_rt_group_b;
	u8 rds_rt_ab;
#define BCM2048_MAX_RDS_PS (8 + 1)
	u8 rds_ps[BCM2048_MAX_RDS_PS];
	u8 rds_ps_group;
	u8 rds_ps_group_cnt;
#define BCM2048_MAX_RDS_RADIO_TEXT 255
	u8 radio_text[BCM2048_MAX_RDS_RADIO_TEXT + 3];
	u8 text_len;
};

struct region_info {
	u32 bottom_frequency;
	u32 top_frequency;
	u8 deemphasis;
	u8 channel_spacing;
	u8 region;
};

struct bcm2048_device {
	struct i2c_client *client;
	struct video_device videodev;
	struct work_struct work;
	struct completion compl;
	struct mutex mutex;
	struct bcm2048_platform_data *platform_data;
	struct rds_info rds_info;
	struct region_info region_info;
	u16 frequency;
	u8 cache_fm_rds_system;
	u8 cache_fm_ctrl;
	u8 cache_fm_audio_ctrl0;
	u8 cache_fm_search_ctrl0;
	u8 power_state;
	u8 rds_state;
	u8 fifo_size;
	u8 scan_state;
	u8 mute_state;

	/* for rds data device read */
	wait_queue_head_t read_queue;
	unsigned int users;
	unsigned char rds_data_available;
	unsigned int rd_index;
};

static int radio_nr = -1;	/* radio device minor (-1 ==> auto assign) */
module_param(radio_nr, int, 0000);
MODULE_PARM_DESC(radio_nr,
		 "Minor number for radio device (-1 ==> auto assign)");

static const struct region_info region_configs[] = {
	/* USA */
	{
		.channel_spacing	= 20,
		.bottom_frequency	= 87500,
		.top_frequency		= 108000,
		.deemphasis		= 75,
		.region			= 0,
	},
	/* Australia */
	{
		.channel_spacing	= 20,
		.bottom_frequency	= 87500,
		.top_frequency		= 108000,
		.deemphasis		= 50,
		.region			= 1,
	},
	/* Europe */
	{
		.channel_spacing	= 10,
		.bottom_frequency	= 87500,
		.top_frequency		= 108000,
		.deemphasis		= 50,
		.region			= 2,
	},
	/* Japan */
	{
		.channel_spacing	= 10,
		.bottom_frequency	= 76000,
		.top_frequency		= 90000,
		.deemphasis		= 50,
		.region			= 3,
	},
};

/*
 *	I2C Interface read / write
 */
static int bcm2048_send_command(struct bcm2048_device *bdev, unsigned int reg,
				unsigned int value)
{
	struct i2c_client *client = bdev->client;
	u8 data[2];

	if (!bdev->power_state) {
		dev_err(&bdev->client->dev, "bcm2048: chip not powered!\n");
		return -EIO;
	}

	data[0] = reg & 0xff;
	data[1] = value & 0xff;

	if (i2c_master_send(client, data, 2) == 2)
		return 0;

	dev_err(&bdev->client->dev, "BCM I2C error!\n");
	dev_err(&bdev->client->dev, "Is Bluetooth up and running?\n");
	return -EIO;
}

static int bcm2048_recv_command(struct bcm2048_device *bdev, unsigned int reg,
				u8 *value)
{
	struct i2c_client *client = bdev->client;

	if (!bdev->power_state) {
		dev_err(&bdev->client->dev, "bcm2048: chip not powered!\n");
		return -EIO;
	}

	value[0] = i2c_smbus_read_byte_data(client, reg & 0xff);

	return 0;
}

static int bcm2048_recv_duples(struct bcm2048_device *bdev, unsigned int reg,
			       u8 *value, u8 duples)
{
	struct i2c_client *client = bdev->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg[2];
	u8 buf;

	if (!bdev->power_state) {
		dev_err(&bdev->client->dev, "bcm2048: chip not powered!\n");
		return -EIO;
	}

	buf = reg & 0xff;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags & I2C_M_TEN;
	msg[0].len = 1;
	msg[0].buf = &buf;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags & I2C_M_TEN;
	msg[1].flags |= I2C_M_RD;
	msg[1].len = duples;
	msg[1].buf = value;

	return i2c_transfer(adap, msg, 2);
}

/*
 *	BCM2048 - I2C register programming helpers
 */
static int bcm2048_set_power_state(struct bcm2048_device *bdev, u8 power)
{
	int err = 0;

	mutex_lock(&bdev->mutex);

	if (power) {
		bdev->power_state = BCM2048_POWER_ON;
		bdev->cache_fm_rds_system |= BCM2048_FM_ON;
	} else {
		bdev->cache_fm_rds_system &= ~BCM2048_FM_ON;
	}

	/*
	 * Warning! FM cannot be turned off because then
	 * the I2C communications get ruined!
	 * Comment off the "if (power)" when the chip works!
	 */
	if (power)
		err = bcm2048_send_command(bdev, BCM2048_I2C_FM_RDS_SYSTEM,
					   bdev->cache_fm_rds_system);
	msleep(BCM2048_DEFAULT_POWERING_DELAY);

	if (!power)
		bdev->power_state = BCM2048_POWER_OFF;

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_power_state(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_RDS_SYSTEM, &value);

	mutex_unlock(&bdev->mutex);

	if (!err && (value & BCM2048_FM_ON))
		return BCM2048_POWER_ON;

	return err;
}

static int bcm2048_set_rds_no_lock(struct bcm2048_device *bdev, u8 rds_on)
{
	int err;
	u8 flags;

	bdev->cache_fm_rds_system &= ~BCM2048_RDS_ON;

	if (rds_on) {
		bdev->cache_fm_rds_system |= BCM2048_RDS_ON;
		bdev->rds_state = BCM2048_RDS_ON;
		flags =	BCM2048_RDS_FLAG_FIFO_WLINE;
		err = bcm2048_send_command(bdev, BCM2048_I2C_FM_RDS_MASK1,
					   flags);
	} else {
		flags =	0;
		bdev->rds_state = 0;
		err = bcm2048_send_command(bdev, BCM2048_I2C_FM_RDS_MASK1,
					   flags);
		memset(&bdev->rds_info, 0, sizeof(bdev->rds_info));
	}
	if (err)
		return err;

	return bcm2048_send_command(bdev, BCM2048_I2C_FM_RDS_SYSTEM,
				    bdev->cache_fm_rds_system);
}

static int bcm2048_get_rds_no_lock(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_RDS_SYSTEM, &value);

	if (!err && (value & BCM2048_RDS_ON))
		return BCM2048_ITEM_ENABLED;

	return err;
}

static int bcm2048_set_rds(struct bcm2048_device *bdev, u8 rds_on)
{
	int err;

	mutex_lock(&bdev->mutex);

	err = bcm2048_set_rds_no_lock(bdev, rds_on);

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_rds(struct bcm2048_device *bdev)
{
	int err;

	mutex_lock(&bdev->mutex);

	err = bcm2048_get_rds_no_lock(bdev);

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_rds_pi(struct bcm2048_device *bdev)
{
	return bdev->rds_info.rds_pi;
}

static int bcm2048_set_fm_automatic_stereo_mono(struct bcm2048_device *bdev,
						u8 enabled)
{
	int err;

	mutex_lock(&bdev->mutex);

	bdev->cache_fm_ctrl &= ~BCM2048_STEREO_MONO_AUTO_SELECT;

	if (enabled)
		bdev->cache_fm_ctrl |= BCM2048_STEREO_MONO_AUTO_SELECT;

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_CTRL,
				   bdev->cache_fm_ctrl);

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_set_fm_hi_lo_injection(struct bcm2048_device *bdev,
					  u8 hi_lo)
{
	int err;

	mutex_lock(&bdev->mutex);

	bdev->cache_fm_ctrl &= ~BCM2048_HI_LO_INJECTION;

	if (hi_lo)
		bdev->cache_fm_ctrl |= BCM2048_HI_LO_INJECTION;

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_CTRL,
				   bdev->cache_fm_ctrl);

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_fm_hi_lo_injection(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_CTRL, &value);

	mutex_unlock(&bdev->mutex);

	if (!err && (value & BCM2048_HI_LO_INJECTION))
		return BCM2048_ITEM_ENABLED;

	return err;
}

static int bcm2048_set_fm_frequency(struct bcm2048_device *bdev, u32 frequency)
{
	int err;

	if (frequency < bdev->region_info.bottom_frequency ||
	    frequency > bdev->region_info.top_frequency)
		return -EDOM;

	frequency -= BCM2048_FREQUENCY_BASE;

	mutex_lock(&bdev->mutex);

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_FREQ0, lsb(frequency));
	err |= bcm2048_send_command(bdev, BCM2048_I2C_FM_FREQ1,
				    msb(frequency));

	if (!err)
		bdev->frequency = frequency;

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_fm_frequency(struct bcm2048_device *bdev)
{
	int err;
	u8 lsb = 0, msb = 0;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_FREQ0, &lsb);
	err |= bcm2048_recv_command(bdev, BCM2048_I2C_FM_FREQ1, &msb);

	mutex_unlock(&bdev->mutex);

	if (err)
		return err;

	err = compose_u16(msb, lsb);
	err += BCM2048_FREQUENCY_BASE;

	return err;
}

static int bcm2048_set_fm_af_frequency(struct bcm2048_device *bdev,
				       u32 frequency)
{
	int err;

	if (frequency < bdev->region_info.bottom_frequency ||
	    frequency > bdev->region_info.top_frequency)
		return -EDOM;

	frequency -= BCM2048_FREQUENCY_BASE;

	mutex_lock(&bdev->mutex);

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_AF_FREQ0,
				   lsb(frequency));
	err |= bcm2048_send_command(bdev, BCM2048_I2C_FM_AF_FREQ1,
				    msb(frequency));
	if (!err)
		bdev->frequency = frequency;

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_fm_af_frequency(struct bcm2048_device *bdev)
{
	int err;
	u8 lsb = 0, msb = 0;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_AF_FREQ0, &lsb);
	err |= bcm2048_recv_command(bdev, BCM2048_I2C_FM_AF_FREQ1, &msb);

	mutex_unlock(&bdev->mutex);

	if (err)
		return err;

	err = compose_u16(msb, lsb);
	err += BCM2048_FREQUENCY_BASE;

	return err;
}

static int bcm2048_set_fm_deemphasis(struct bcm2048_device *bdev, int d)
{
	int err;
	u8 deemphasis;

	if (d == BCM2048_DE_EMPHASIS_75us)
		deemphasis = BCM2048_DE_EMPHASIS_SELECT;
	else
		deemphasis = 0;

	mutex_lock(&bdev->mutex);

	bdev->cache_fm_audio_ctrl0 &= ~BCM2048_DE_EMPHASIS_SELECT;
	bdev->cache_fm_audio_ctrl0 |= deemphasis;

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_AUDIO_CTRL0,
				   bdev->cache_fm_audio_ctrl0);

	if (!err)
		bdev->region_info.deemphasis = d;

	mutex_unlock(&bdev->mutex);

	return err;
}

static int bcm2048_get_fm_deemphasis(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_AUDIO_CTRL0, &value);

	mutex_unlock(&bdev->mutex);

	if (!err) {
		if (value & BCM2048_DE_EMPHASIS_SELECT)
			return BCM2048_DE_EMPHASIS_75us;

		return BCM2048_DE_EMPHASIS_50us;
	}

	return err;
}

static int bcm2048_set_region(struct bcm2048_device *bdev, u8 region)
{
	int err;
	u32 new_frequency = 0;

	if (region >= ARRAY_SIZE(region_configs))
		return -EINVAL;

	mutex_lock(&bdev->mutex);
	bdev->region_info = region_configs[region];

	if (region_configs[region].bottom_frequency < 87500)
		bdev->cache_fm_ctrl |= BCM2048_BAND_SELECT;
	else
		bdev->cache_fm_ctrl &= ~BCM2048_BAND_SELECT;

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_CTRL,
				   bdev->cache_fm_ctrl);
	if (err) {
		mutex_unlock(&bdev->mutex);
		goto done;
	}
	mutex_unlock(&bdev->mutex);

	if (bdev->frequency < region_configs[region].bottom_frequency ||
	    bdev->frequency > region_configs[region].top_frequency)
		new_frequency = region_configs[region].bottom_frequency;

	if (new_frequency > 0) {
		err = bcm2048_set_fm_frequency(bdev, new_frequency);

		if (err)
			goto done;
	}

	err = bcm2048_set_fm_deemphasis(bdev,
					region_configs[region].deemphasis);

done:
	return err;
}

static int bcm2048_get_region(struct bcm2048_device *bdev)
{
	int err;

	mutex_lock(&bdev->mutex);
	err = bdev->region_info.region;
	mutex_unlock(&bdev->mutex);

	return err;
}

static int bcm2048_set_mute(struct bcm2048_device *bdev, u16 mute)
{
	int err;

	mutex_lock(&bdev->mutex);

	bdev->cache_fm_audio_ctrl0 &= ~(BCM2048_RF_MUTE | BCM2048_MANUAL_MUTE);

	if (mute)
		bdev->cache_fm_audio_ctrl0 |= (BCM2048_RF_MUTE |
					       BCM2048_MANUAL_MUTE);

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_AUDIO_CTRL0,
				   bdev->cache_fm_audio_ctrl0);

	if (!err)
		bdev->mute_state = mute;

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_mute(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	if (bdev->power_state) {
		err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_AUDIO_CTRL0,
					   &value);
		if (!err)
			err = value & (BCM2048_RF_MUTE | BCM2048_MANUAL_MUTE);
	} else {
		err = bdev->mute_state;
	}

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_set_audio_route(struct bcm2048_device *bdev, u8 route)
{
	int err;

	mutex_lock(&bdev->mutex);

	route &= (BCM2048_AUDIO_ROUTE_DAC | BCM2048_AUDIO_ROUTE_I2S);
	bdev->cache_fm_audio_ctrl0 &= ~(BCM2048_AUDIO_ROUTE_DAC |
					BCM2048_AUDIO_ROUTE_I2S);
	bdev->cache_fm_audio_ctrl0 |= route;

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_AUDIO_CTRL0,
				   bdev->cache_fm_audio_ctrl0);

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_audio_route(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_AUDIO_CTRL0, &value);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return value & (BCM2048_AUDIO_ROUTE_DAC |
				BCM2048_AUDIO_ROUTE_I2S);

	return err;
}

static int bcm2048_set_dac_output(struct bcm2048_device *bdev, u8 channels)
{
	int err;

	mutex_lock(&bdev->mutex);

	bdev->cache_fm_audio_ctrl0 &= ~(BCM2048_DAC_OUTPUT_LEFT |
					BCM2048_DAC_OUTPUT_RIGHT);
	bdev->cache_fm_audio_ctrl0 |= channels;

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_AUDIO_CTRL0,
				   bdev->cache_fm_audio_ctrl0);

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_dac_output(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_AUDIO_CTRL0, &value);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return value & (BCM2048_DAC_OUTPUT_LEFT |
				BCM2048_DAC_OUTPUT_RIGHT);

	return err;
}

static int bcm2048_set_fm_search_rssi_threshold(struct bcm2048_device *bdev,
						u8 threshold)
{
	int err;

	mutex_lock(&bdev->mutex);

	threshold &= BCM2048_SEARCH_RSSI_THRESHOLD;
	bdev->cache_fm_search_ctrl0 &= ~BCM2048_SEARCH_RSSI_THRESHOLD;
	bdev->cache_fm_search_ctrl0 |= threshold;

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_SEARCH_CTRL0,
				   bdev->cache_fm_search_ctrl0);

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_fm_search_rssi_threshold(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_SEARCH_CTRL0, &value);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return value & BCM2048_SEARCH_RSSI_THRESHOLD;

	return err;
}

static int bcm2048_set_fm_search_mode_direction(struct bcm2048_device *bdev,
						u8 direction)
{
	int err;

	mutex_lock(&bdev->mutex);

	bdev->cache_fm_search_ctrl0 &= ~BCM2048_SEARCH_DIRECTION;

	if (direction)
		bdev->cache_fm_search_ctrl0 |= BCM2048_SEARCH_DIRECTION;

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_SEARCH_CTRL0,
				   bdev->cache_fm_search_ctrl0);

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_fm_search_mode_direction(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_SEARCH_CTRL0, &value);

	mutex_unlock(&bdev->mutex);

	if (!err && (value & BCM2048_SEARCH_DIRECTION))
		return BCM2048_SEARCH_DIRECTION_UP;

	return err;
}

static int bcm2048_set_fm_search_tune_mode(struct bcm2048_device *bdev,
					   u8 mode)
{
	int err, timeout, restart_rds = 0;
	u8 value, flags;

	value = mode & BCM2048_FM_AUTO_SEARCH;

	flags =	BCM2048_FM_FLAG_SEARCH_TUNE_FINISHED |
		BCM2048_FM_FLAG_SEARCH_TUNE_FAIL;

	mutex_lock(&bdev->mutex);

	/*
	 * If RDS is enabled, and frequency is changed, RDS quits working.
	 * Thus, always restart RDS if it's enabled. Moreover, RDS must
	 * not be enabled while changing the frequency because it can
	 * provide a race to the mutex from the workqueue handler if RDS
	 * IRQ occurs while waiting for frequency changed IRQ.
	 */
	if (bcm2048_get_rds_no_lock(bdev)) {
		err = bcm2048_set_rds_no_lock(bdev, 0);
		if (err)
			goto unlock;
		restart_rds = 1;
	}

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_RDS_MASK0, flags);

	if (err)
		goto unlock;

	bcm2048_send_command(bdev, BCM2048_I2C_FM_SEARCH_TUNE_MODE, value);

	if (mode != BCM2048_FM_AUTO_SEARCH_MODE)
		timeout = BCM2048_DEFAULT_TIMEOUT;
	else
		timeout = BCM2048_AUTO_SEARCH_TIMEOUT;

	if (!wait_for_completion_timeout(&bdev->compl,
					 msecs_to_jiffies(timeout)))
		dev_err(&bdev->client->dev, "IRQ timeout.\n");

	if (value)
		if (!bdev->scan_state)
			err = -EIO;

unlock:
	if (restart_rds)
		err |= bcm2048_set_rds_no_lock(bdev, 1);

	mutex_unlock(&bdev->mutex);

	return err;
}

static int bcm2048_get_fm_search_tune_mode(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_SEARCH_TUNE_MODE,
				   &value);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return value & BCM2048_FM_AUTO_SEARCH;

	return err;
}

static int bcm2048_set_rds_b_block_mask(struct bcm2048_device *bdev, u16 mask)
{
	int err;

	mutex_lock(&bdev->mutex);

	err = bcm2048_send_command(bdev, BCM2048_I2C_RDS_BLKB_MASK0,
				   lsb(mask));
	err |= bcm2048_send_command(bdev, BCM2048_I2C_RDS_BLKB_MASK1,
				    msb(mask));

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_rds_b_block_mask(struct bcm2048_device *bdev)
{
	int err;
	u8 lsb = 0, msb = 0;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_RDS_BLKB_MASK0, &lsb);
	err |= bcm2048_recv_command(bdev, BCM2048_I2C_RDS_BLKB_MASK1, &msb);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return compose_u16(msb, lsb);

	return err;
}

static int bcm2048_set_rds_b_block_match(struct bcm2048_device *bdev,
					 u16 match)
{
	int err;

	mutex_lock(&bdev->mutex);

	err = bcm2048_send_command(bdev, BCM2048_I2C_RDS_BLKB_MATCH0,
				   lsb(match));
	err |= bcm2048_send_command(bdev, BCM2048_I2C_RDS_BLKB_MATCH1,
				    msb(match));

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_rds_b_block_match(struct bcm2048_device *bdev)
{
	int err;
	u8 lsb = 0, msb = 0;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_RDS_BLKB_MATCH0, &lsb);
	err |= bcm2048_recv_command(bdev, BCM2048_I2C_RDS_BLKB_MATCH1, &msb);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return compose_u16(msb, lsb);

	return err;
}

static int bcm2048_set_rds_pi_mask(struct bcm2048_device *bdev, u16 mask)
{
	int err;

	mutex_lock(&bdev->mutex);

	err = bcm2048_send_command(bdev, BCM2048_I2C_RDS_PI_MASK0, lsb(mask));
	err |= bcm2048_send_command(bdev, BCM2048_I2C_RDS_PI_MASK1, msb(mask));

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_rds_pi_mask(struct bcm2048_device *bdev)
{
	int err;
	u8 lsb = 0, msb = 0;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_RDS_PI_MASK0, &lsb);
	err |= bcm2048_recv_command(bdev, BCM2048_I2C_RDS_PI_MASK1, &msb);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return compose_u16(msb, lsb);

	return err;
}

static int bcm2048_set_rds_pi_match(struct bcm2048_device *bdev, u16 match)
{
	int err;

	mutex_lock(&bdev->mutex);

	err = bcm2048_send_command(bdev, BCM2048_I2C_RDS_PI_MATCH0,
				   lsb(match));
	err |= bcm2048_send_command(bdev, BCM2048_I2C_RDS_PI_MATCH1,
				    msb(match));

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_rds_pi_match(struct bcm2048_device *bdev)
{
	int err;
	u8 lsb = 0, msb = 0;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_RDS_PI_MATCH0, &lsb);
	err |= bcm2048_recv_command(bdev, BCM2048_I2C_RDS_PI_MATCH1, &msb);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return compose_u16(msb, lsb);

	return err;
}

static int bcm2048_set_fm_rds_mask(struct bcm2048_device *bdev, u16 mask)
{
	int err;

	mutex_lock(&bdev->mutex);

	err = bcm2048_send_command(bdev, BCM2048_I2C_FM_RDS_MASK0, lsb(mask));
	err |= bcm2048_send_command(bdev, BCM2048_I2C_FM_RDS_MASK1, msb(mask));

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_fm_rds_mask(struct bcm2048_device *bdev)
{
	int err;
	u8 value0 = 0, value1 = 0;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_RDS_MASK0, &value0);
	err |= bcm2048_recv_command(bdev, BCM2048_I2C_FM_RDS_MASK1, &value1);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return compose_u16(value1, value0);

	return err;
}

static int bcm2048_get_fm_rds_flags(struct bcm2048_device *bdev)
{
	int err;
	u8 value0 = 0, value1 = 0;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_RDS_FLAG0, &value0);
	err |= bcm2048_recv_command(bdev, BCM2048_I2C_FM_RDS_FLAG1, &value1);

	mutex_unlock(&bdev->mutex);

	if (!err)
		return compose_u16(value1, value0);

	return err;
}

static int bcm2048_get_region_bottom_frequency(struct bcm2048_device *bdev)
{
	return bdev->region_info.bottom_frequency;
}

static int bcm2048_get_region_top_frequency(struct bcm2048_device *bdev)
{
	return bdev->region_info.top_frequency;
}

static int bcm2048_set_fm_best_tune_mode(struct bcm2048_device *bdev, u8 mode)
{
	int err;
	u8 value = 0;

	mutex_lock(&bdev->mutex);

	/* Perform read as the manual indicates */
	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_BEST_TUNE_MODE,
				   &value);
	value &= ~BCM2048_BEST_TUNE_MODE;

	if (mode)
		value |= BCM2048_BEST_TUNE_MODE;
	err |= bcm2048_send_command(bdev, BCM2048_I2C_FM_BEST_TUNE_MODE,
				    value);

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_fm_best_tune_mode(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_BEST_TUNE_MODE,
				   &value);

	mutex_unlock(&bdev->mutex);

	if (!err && (value & BCM2048_BEST_TUNE_MODE))
		return BCM2048_ITEM_ENABLED;

	return err;
}

static int bcm2048_get_fm_carrier_error(struct bcm2048_device *bdev)
{
	int err = 0;
	s8 value;

	mutex_lock(&bdev->mutex);
	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_CARRIER, &value);
	mutex_unlock(&bdev->mutex);

	if (!err)
		return value;

	return err;
}

static int bcm2048_get_fm_rssi(struct bcm2048_device *bdev)
{
	int err;
	s8 value;

	mutex_lock(&bdev->mutex);
	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_RSSI, &value);
	mutex_unlock(&bdev->mutex);

	if (!err)
		return value;

	return err;
}

static int bcm2048_set_rds_wline(struct bcm2048_device *bdev, u8 wline)
{
	int err;

	mutex_lock(&bdev->mutex);

	err = bcm2048_send_command(bdev, BCM2048_I2C_RDS_WLINE, wline);

	if (!err)
		bdev->fifo_size = wline;

	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_rds_wline(struct bcm2048_device *bdev)
{
	int err;
	u8 value;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_RDS_WLINE, &value);

	mutex_unlock(&bdev->mutex);

	if (!err) {
		bdev->fifo_size = value;
		return value;
	}

	return err;
}

static int bcm2048_checkrev(struct bcm2048_device *bdev)
{
	int err;
	u8 version;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_command(bdev, BCM2048_I2C_FM_RDS_REV, &version);

	mutex_unlock(&bdev->mutex);

	if (!err) {
		dev_info(&bdev->client->dev, "BCM2048 Version 0x%x\n",
			 version);
		return version;
	}

	return err;
}

static int bcm2048_get_rds_rt(struct bcm2048_device *bdev, char *data)
{
	int err = 0, i, j = 0, ce = 0, cr = 0;
	char data_buffer[BCM2048_MAX_RDS_RT + 1];

	mutex_lock(&bdev->mutex);

	if (!bdev->rds_info.text_len) {
		err = -EINVAL;
		goto unlock;
	}

	for (i = 0; i < BCM2048_MAX_RDS_RT; i++) {
		if (bdev->rds_info.rds_rt[i]) {
			ce = i;
			/* Skip the carriage return */
			if (bdev->rds_info.rds_rt[i] != 0x0d) {
				data_buffer[j++] = bdev->rds_info.rds_rt[i];
			} else {
				cr = i;
				break;
			}
		}
	}

	if (j <= BCM2048_MAX_RDS_RT)
		data_buffer[j] = 0;

	for (i = 0; i < BCM2048_MAX_RDS_RT; i++) {
		if (!bdev->rds_info.rds_rt[i]) {
			if (cr && (i < cr)) {
				err = -EBUSY;
				goto unlock;
			}
			if (i < ce) {
				if (cr && (i >= cr))
					break;
				err = -EBUSY;
				goto unlock;
			}
		}
	}

	memcpy(data, data_buffer, sizeof(data_buffer));

unlock:
	mutex_unlock(&bdev->mutex);
	return err;
}

static int bcm2048_get_rds_ps(struct bcm2048_device *bdev, char *data)
{
	int err = 0, i, j = 0;
	char data_buffer[BCM2048_MAX_RDS_PS + 1];

	mutex_lock(&bdev->mutex);

	if (!bdev->rds_info.text_len) {
		err = -EINVAL;
		goto unlock;
	}

	for (i = 0; i < BCM2048_MAX_RDS_PS; i++) {
		if (bdev->rds_info.rds_ps[i]) {
			data_buffer[j++] = bdev->rds_info.rds_ps[i];
		} else {
			if (i < (BCM2048_MAX_RDS_PS - 1)) {
				err = -EBUSY;
				goto unlock;
			}
		}
	}

	if (j <= BCM2048_MAX_RDS_PS)
		data_buffer[j] = 0;

	memcpy(data, data_buffer, sizeof(data_buffer));

unlock:
	mutex_unlock(&bdev->mutex);
	return err;
}

static void bcm2048_parse_rds_pi(struct bcm2048_device *bdev)
{
	int i, cnt = 0;
	u16 pi;

	for (i = 0; i < bdev->fifo_size; i += BCM2048_RDS_FIFO_DUPLE_SIZE) {
		/* Block A match, only data without crc errors taken */
		if (bdev->rds_info.radio_text[i] == BCM2048_RDS_BLOCK_A) {
			pi = (bdev->rds_info.radio_text[i + 1] << 8) +
				bdev->rds_info.radio_text[i + 2];

			if (!bdev->rds_info.rds_pi) {
				bdev->rds_info.rds_pi = pi;
				return;
			}
			if (pi != bdev->rds_info.rds_pi) {
				cnt++;
				if (cnt > 3) {
					bdev->rds_info.rds_pi = pi;
					cnt = 0;
				}
			} else {
				cnt = 0;
			}
		}
	}
}

static int bcm2048_rds_block_crc(struct bcm2048_device *bdev, int i)
{
	return bdev->rds_info.radio_text[i] & BCM2048_RDS_CRC_MASK;
}

static void bcm2048_parse_rds_rt_block(struct bcm2048_device *bdev, int i,
				       int index, int crc)
{
	/* Good data will overwrite poor data */
	if (crc) {
		if (!bdev->rds_info.rds_rt[index])
			bdev->rds_info.rds_rt[index] =
				bdev->rds_info.radio_text[i + 1];
		if (!bdev->rds_info.rds_rt[index + 1])
			bdev->rds_info.rds_rt[index + 1] =
				bdev->rds_info.radio_text[i + 2];
	} else {
		bdev->rds_info.rds_rt[index] =
			bdev->rds_info.radio_text[i + 1];
		bdev->rds_info.rds_rt[index + 1] =
			bdev->rds_info.radio_text[i + 2];
	}
}

static int bcm2048_parse_rt_match_b(struct bcm2048_device *bdev, int i)
{
	int crc, rt_id, rt_group_b, rt_ab, index = 0;

	crc = bcm2048_rds_block_crc(bdev, i);

	if (crc == BCM2048_RDS_CRC_UNRECOVARABLE)
		return -EIO;

	if ((bdev->rds_info.radio_text[i] & BCM2048_RDS_BLOCK_MASK) ==
	    BCM2048_RDS_BLOCK_B) {
		rt_id = bdev->rds_info.radio_text[i + 1] &
			BCM2048_RDS_BLOCK_MASK;
		rt_group_b = bdev->rds_info.radio_text[i + 1] &
			BCM2048_RDS_GROUP_AB_MASK;
		rt_ab = bdev->rds_info.radio_text[i + 2] &
				BCM2048_RDS_RT_AB_MASK;

		if (rt_group_b != bdev->rds_info.rds_rt_group_b) {
			memset(bdev->rds_info.rds_rt, 0,
			       sizeof(bdev->rds_info.rds_rt));
			bdev->rds_info.rds_rt_group_b = rt_group_b;
		}

		if (rt_id == BCM2048_RDS_RT) {
			/* A to B or (vice versa), means: clear screen */
			if (rt_ab != bdev->rds_info.rds_rt_ab) {
				memset(bdev->rds_info.rds_rt, 0,
				       sizeof(bdev->rds_info.rds_rt));
				bdev->rds_info.rds_rt_ab = rt_ab;
			}

			index = bdev->rds_info.radio_text[i + 2] &
					BCM2048_RDS_RT_INDEX;

			if (bdev->rds_info.rds_rt_group_b)
				index <<= 1;
			else
				index <<= 2;

			return index;
		}
	}

	return -EIO;
}

static int bcm2048_parse_rt_match_c(struct bcm2048_device *bdev, int i,
				    int index)
{
	int crc;

	crc = bcm2048_rds_block_crc(bdev, i);

	if (crc == BCM2048_RDS_CRC_UNRECOVARABLE)
		return 0;

	if ((index + 2) >= BCM2048_MAX_RDS_RT) {
		dev_err(&bdev->client->dev,
			"Incorrect index = %d\n", index);
		return 0;
	}

	if ((bdev->rds_info.radio_text[i] & BCM2048_RDS_BLOCK_MASK) ==
		BCM2048_RDS_BLOCK_C) {
		if (bdev->rds_info.rds_rt_group_b)
			return 1;
		bcm2048_parse_rds_rt_block(bdev, i, index, crc);
		return 1;
	}

	return 0;
}

static void bcm2048_parse_rt_match_d(struct bcm2048_device *bdev, int i,
				     int index)
{
	int crc;

	crc = bcm2048_rds_block_crc(bdev, i);

	if (crc == BCM2048_RDS_CRC_UNRECOVARABLE)
		return;

	if ((index + 4) >= BCM2048_MAX_RDS_RT) {
		dev_err(&bdev->client->dev,
			"Incorrect index = %d\n", index);
		return;
	}

	if ((bdev->rds_info.radio_text[i] & BCM2048_RDS_BLOCK_MASK) ==
	    BCM2048_RDS_BLOCK_D)
		bcm2048_parse_rds_rt_block(bdev, i, index + 2, crc);
}

static void bcm2048_parse_rds_rt(struct bcm2048_device *bdev)
{
	int i, index = 0, crc, match_b = 0, match_c = 0, match_d = 0;

	for (i = 0; i < bdev->fifo_size; i += BCM2048_RDS_FIFO_DUPLE_SIZE) {
		if (match_b) {
			match_b = 0;
			index = bcm2048_parse_rt_match_b(bdev, i);
			if (index >= 0 && index <= (BCM2048_MAX_RDS_RT - 5))
				match_c = 1;
			continue;
		} else if (match_c) {
			match_c = 0;
			if (bcm2048_parse_rt_match_c(bdev, i, index))
				match_d = 1;
			continue;
		} else if (match_d) {
			match_d = 0;
			bcm2048_parse_rt_match_d(bdev, i, index);
			continue;
		}

		/* Skip erroneous blocks due to messed up A block altogether */
		if ((bdev->rds_info.radio_text[i] & BCM2048_RDS_BLOCK_MASK) ==
		    BCM2048_RDS_BLOCK_A) {
			crc = bcm2048_rds_block_crc(bdev, i);
			if (crc == BCM2048_RDS_CRC_UNRECOVARABLE)
				continue;
			/* Synchronize to a good RDS PI */
			if (((bdev->rds_info.radio_text[i + 1] << 8) +
			    bdev->rds_info.radio_text[i + 2]) ==
			    bdev->rds_info.rds_pi)
				match_b = 1;
		}
	}
}

static void bcm2048_parse_rds_ps_block(struct bcm2048_device *bdev, int i,
				       int index, int crc)
{
	/* Good data will overwrite poor data */
	if (crc) {
		if (!bdev->rds_info.rds_ps[index])
			bdev->rds_info.rds_ps[index] =
				bdev->rds_info.radio_text[i + 1];
		if (!bdev->rds_info.rds_ps[index + 1])
			bdev->rds_info.rds_ps[index + 1] =
				bdev->rds_info.radio_text[i + 2];
	} else {
		bdev->rds_info.rds_ps[index] =
			bdev->rds_info.radio_text[i + 1];
		bdev->rds_info.rds_ps[index + 1] =
			bdev->rds_info.radio_text[i + 2];
	}
}

static int bcm2048_parse_ps_match_c(struct bcm2048_device *bdev, int i,
				    int index)
{
	int crc;

	crc = bcm2048_rds_block_crc(bdev, i);

	if (crc == BCM2048_RDS_CRC_UNRECOVARABLE)
		return 0;

	if ((bdev->rds_info.radio_text[i] & BCM2048_RDS_BLOCK_MASK) ==
	    BCM2048_RDS_BLOCK_C)
		return 1;

	return 0;
}

static void bcm2048_parse_ps_match_d(struct bcm2048_device *bdev, int i,
				     int index)
{
	int crc;

	crc = bcm2048_rds_block_crc(bdev, i);

	if (crc == BCM2048_RDS_CRC_UNRECOVARABLE)
		return;

	if ((bdev->rds_info.radio_text[i] & BCM2048_RDS_BLOCK_MASK) ==
	    BCM2048_RDS_BLOCK_D)
		bcm2048_parse_rds_ps_block(bdev, i, index, crc);
}

static int bcm2048_parse_ps_match_b(struct bcm2048_device *bdev, int i)
{
	int crc, index, ps_id, ps_group;

	crc = bcm2048_rds_block_crc(bdev, i);

	if (crc == BCM2048_RDS_CRC_UNRECOVARABLE)
		return -EIO;

	/* Block B Radio PS match */
	if ((bdev->rds_info.radio_text[i] & BCM2048_RDS_BLOCK_MASK) ==
	    BCM2048_RDS_BLOCK_B) {
		ps_id = bdev->rds_info.radio_text[i + 1] &
			BCM2048_RDS_BLOCK_MASK;
		ps_group = bdev->rds_info.radio_text[i + 1] &
			BCM2048_RDS_GROUP_AB_MASK;

		/*
		 * Poor RSSI will lead to RDS data corruption
		 * So using 3 (same) sequential values to justify major changes
		 */
		if (ps_group != bdev->rds_info.rds_ps_group) {
			if (crc == BCM2048_RDS_CRC_NONE) {
				bdev->rds_info.rds_ps_group_cnt++;
				if (bdev->rds_info.rds_ps_group_cnt > 2) {
					bdev->rds_info.rds_ps_group = ps_group;
					bdev->rds_info.rds_ps_group_cnt	= 0;
					dev_err(&bdev->client->dev,
						"RDS PS Group change!\n");
				} else {
					return -EIO;
				}
			} else {
				bdev->rds_info.rds_ps_group_cnt = 0;
			}
		}

		if (ps_id == BCM2048_RDS_PS) {
			index = bdev->rds_info.radio_text[i + 2] &
				BCM2048_RDS_PS_INDEX;
			index <<= 1;
			return index;
		}
	}

	return -EIO;
}

static void bcm2048_parse_rds_ps(struct bcm2048_device *bdev)
{
	int i, index = 0, crc, match_b = 0, match_c = 0, match_d = 0;

	for (i = 0; i < bdev->fifo_size; i += BCM2048_RDS_FIFO_DUPLE_SIZE) {
		if (match_b) {
			match_b = 0;
			index = bcm2048_parse_ps_match_b(bdev, i);
			if (index >= 0 && index < (BCM2048_MAX_RDS_PS - 1))
				match_c = 1;
			continue;
		} else if (match_c) {
			match_c = 0;
			if (bcm2048_parse_ps_match_c(bdev, i, index))
				match_d = 1;
			continue;
		} else if (match_d) {
			match_d = 0;
			bcm2048_parse_ps_match_d(bdev, i, index);
			continue;
		}

		/* Skip erroneous blocks due to messed up A block altogether */
		if ((bdev->rds_info.radio_text[i] & BCM2048_RDS_BLOCK_MASK) ==
		    BCM2048_RDS_BLOCK_A) {
			crc = bcm2048_rds_block_crc(bdev, i);
			if (crc == BCM2048_RDS_CRC_UNRECOVARABLE)
				continue;
			/* Synchronize to a good RDS PI */
			if (((bdev->rds_info.radio_text[i + 1] << 8) +
			    bdev->rds_info.radio_text[i + 2]) ==
			    bdev->rds_info.rds_pi)
				match_b = 1;
		}
	}
}

static void bcm2048_rds_fifo_receive(struct bcm2048_device *bdev)
{
	int err;

	mutex_lock(&bdev->mutex);

	err = bcm2048_recv_duples(bdev, BCM2048_I2C_RDS_DATA,
				  bdev->rds_info.radio_text, bdev->fifo_size);
	if (err != 2) {
		dev_err(&bdev->client->dev, "RDS Read problem\n");
		mutex_unlock(&bdev->mutex);
		return;
	}

	bdev->rds_info.text_len = bdev->fifo_size;

	bcm2048_parse_rds_pi(bdev);
	bcm2048_parse_rds_rt(bdev);
	bcm2048_parse_rds_ps(bdev);

	mutex_unlock(&bdev->mutex);

	wake_up_interruptible(&bdev->read_queue);
}

static int bcm2048_get_rds_data(struct bcm2048_device *bdev, char *data)
{
	int err = 0, i, p = 0;
	char *data_buffer;

	mutex_lock(&bdev->mutex);

	if (!bdev->rds_info.text_len) {
		err = -EINVAL;
		goto unlock;
	}

	data_buffer = kcalloc(BCM2048_MAX_RDS_RADIO_TEXT, 5, GFP_KERNEL);
	if (!data_buffer) {
		err = -ENOMEM;
		goto unlock;
	}

	for (i = 0; i < bdev->rds_info.text_len; i++) {
		p += sprintf(data_buffer + p, "%x ",
			     bdev->rds_info.radio_text[i]);
	}

	memcpy(data, data_buffer, p);
	kfree(data_buffer);

unlock:
	mutex_unlock(&bdev->mutex);
	return err;
}

/*
 *	BCM2048 default initialization sequence
 */
static int bcm2048_init(struct bcm2048_device *bdev)
{
	int err;

	err = bcm2048_set_power_state(bdev, BCM2048_POWER_ON);
	if (err < 0)
		goto exit;

	err = bcm2048_set_audio_route(bdev, BCM2048_AUDIO_ROUTE_DAC);
	if (err < 0)
		goto exit;

	err = bcm2048_set_dac_output(bdev, BCM2048_DAC_OUTPUT_LEFT |
				     BCM2048_DAC_OUTPUT_RIGHT);

exit:
	return err;
}

/*
 *	BCM2048 default deinitialization sequence
 */
static int bcm2048_deinit(struct bcm2048_device *bdev)
{
	int err;

	err = bcm2048_set_audio_route(bdev, 0);
	if (err < 0)
		return err;

	err = bcm2048_set_dac_output(bdev, 0);
	if (err < 0)
		return err;

	return bcm2048_set_power_state(bdev, BCM2048_POWER_OFF);
}

/*
 *	BCM2048 probe sequence
 */
static int bcm2048_probe(struct bcm2048_device *bdev)
{
	int err;

	err = bcm2048_set_power_state(bdev, BCM2048_POWER_ON);
	if (err < 0)
		goto unlock;

	err = bcm2048_checkrev(bdev);
	if (err < 0)
		goto unlock;

	err = bcm2048_set_mute(bdev, BCM2048_DEFAULT_MUTE);
	if (err < 0)
		goto unlock;

	err = bcm2048_set_region(bdev, BCM2048_DEFAULT_REGION);
	if (err < 0)
		goto unlock;

	err = bcm2048_set_fm_search_rssi_threshold(bdev,
					BCM2048_DEFAULT_RSSI_THRESHOLD);
	if (err < 0)
		goto unlock;

	err = bcm2048_set_fm_automatic_stereo_mono(bdev, BCM2048_ITEM_ENABLED);
	if (err < 0)
		goto unlock;

	err = bcm2048_get_rds_wline(bdev);
	if (err < BCM2048_DEFAULT_RDS_WLINE)
		err = bcm2048_set_rds_wline(bdev, BCM2048_DEFAULT_RDS_WLINE);
	if (err < 0)
		goto unlock;

	err = bcm2048_set_power_state(bdev, BCM2048_POWER_OFF);

	init_waitqueue_head(&bdev->read_queue);
	bdev->rds_data_available = 0;
	bdev->rd_index = 0;
	bdev->users = 0;

unlock:
	return err;
}

/*
 *	BCM2048 workqueue handler
 */
static void bcm2048_work(struct work_struct *work)
{
	struct bcm2048_device *bdev;
	u8 flag_lsb = 0, flag_msb = 0, flags;

	bdev = container_of(work, struct bcm2048_device, work);
	bcm2048_recv_command(bdev, BCM2048_I2C_FM_RDS_FLAG0, &flag_lsb);
	bcm2048_recv_command(bdev, BCM2048_I2C_FM_RDS_FLAG1, &flag_msb);

	if (flag_lsb & (BCM2048_FM_FLAG_SEARCH_TUNE_FINISHED |
			BCM2048_FM_FLAG_SEARCH_TUNE_FAIL)) {
		if (flag_lsb & BCM2048_FM_FLAG_SEARCH_TUNE_FAIL)
			bdev->scan_state = BCM2048_SCAN_FAIL;
		else
			bdev->scan_state = BCM2048_SCAN_OK;

		complete(&bdev->compl);
	}

	if (flag_msb & BCM2048_RDS_FLAG_FIFO_WLINE) {
		bcm2048_rds_fifo_receive(bdev);
		if (bdev->rds_state) {
			flags =	BCM2048_RDS_FLAG_FIFO_WLINE;
			bcm2048_send_command(bdev, BCM2048_I2C_FM_RDS_MASK1,
					     flags);
		}
		bdev->rds_data_available = 1;
		bdev->rd_index = 0; /* new data, new start */
	}
}

/*
 *	BCM2048 interrupt handler
 */
static irqreturn_t bcm2048_handler(int irq, void *dev)
{
	struct bcm2048_device *bdev = dev;

	dev_dbg(&bdev->client->dev, "IRQ called, queuing work\n");
	if (bdev->power_state)
		schedule_work(&bdev->work);

	return IRQ_HANDLED;
}

/*
 *	BCM2048 sysfs interface definitions
 */
#define property_write(prop, type, mask, check)				\
static ssize_t bcm2048_##prop##_write(struct device *dev,		\
					struct device_attribute *attr,	\
					const char *buf,		\
					size_t count)			\
{									\
	struct bcm2048_device *bdev = dev_get_drvdata(dev);		\
	type value;							\
	int err;							\
									\
	if (!bdev)							\
		return -ENODEV;						\
									\
	if (sscanf(buf, mask, &value) != 1)				\
		return -EINVAL;						\
									\
	if (check)							\
		return -EDOM;						\
									\
	err = bcm2048_set_##prop(bdev, value);				\
									\
	return err < 0 ? err : count;					\
}

#define property_read(prop, mask)					\
static ssize_t bcm2048_##prop##_read(struct device *dev,		\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct bcm2048_device *bdev = dev_get_drvdata(dev);		\
	int value;							\
									\
	if (!bdev)							\
		return -ENODEV;						\
									\
	value = bcm2048_get_##prop(bdev);				\
									\
	if (value >= 0)							\
		value = sprintf(buf, mask "\n", value);			\
									\
	return value;							\
}

#define property_signed_read(prop, size, mask)				\
static ssize_t bcm2048_##prop##_read(struct device *dev,		\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct bcm2048_device *bdev = dev_get_drvdata(dev);		\
	size value;							\
									\
	if (!bdev)							\
		return -ENODEV;						\
									\
	value = bcm2048_get_##prop(bdev);				\
									\
	return sprintf(buf, mask "\n", value);				\
}

#define DEFINE_SYSFS_PROPERTY(prop, prop_type, mask, check)		\
property_write(prop, prop_type, mask, check)				\
property_read(prop, mask)						\

#define property_str_read(prop, size)					\
static ssize_t bcm2048_##prop##_read(struct device *dev,		\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct bcm2048_device *bdev = dev_get_drvdata(dev);		\
	int count;							\
	u8 *out;							\
									\
	if (!bdev)							\
		return -ENODEV;						\
									\
	out = kzalloc((size) + 1, GFP_KERNEL);				\
	if (!out)							\
		return -ENOMEM;						\
									\
	bcm2048_get_##prop(bdev, out);					\
	count = sprintf(buf, "%s\n", out);				\
									\
	kfree(out);							\
									\
	return count;							\
}

DEFINE_SYSFS_PROPERTY(power_state, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(mute, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(audio_route, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(dac_output, unsigned int, "%u", 0)

DEFINE_SYSFS_PROPERTY(fm_hi_lo_injection, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(fm_frequency, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(fm_af_frequency, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(fm_deemphasis, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(fm_rds_mask, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(fm_best_tune_mode, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(fm_search_rssi_threshold, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(fm_search_mode_direction, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(fm_search_tune_mode, unsigned int, "%u", value > 3)

DEFINE_SYSFS_PROPERTY(rds, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(rds_b_block_mask, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(rds_b_block_match, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(rds_pi_mask, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(rds_pi_match, unsigned int, "%u", 0)
DEFINE_SYSFS_PROPERTY(rds_wline, unsigned int, "%u", 0)
property_read(rds_pi, "%x")
property_str_read(rds_rt, (BCM2048_MAX_RDS_RT + 1))
property_str_read(rds_ps, (BCM2048_MAX_RDS_PS + 1))

property_read(fm_rds_flags, "%u")
property_str_read(rds_data, BCM2048_MAX_RDS_RADIO_TEXT * 5)

property_read(region_bottom_frequency, "%u")
property_read(region_top_frequency, "%u")
property_signed_read(fm_carrier_error, int, "%d")
property_signed_read(fm_rssi, int, "%d")
DEFINE_SYSFS_PROPERTY(region, unsigned int, "%u", 0)

static struct device_attribute attrs[] = {
	__ATTR(power_state, 0644, bcm2048_power_state_read,
	       bcm2048_power_state_write),
	__ATTR(mute, 0644, bcm2048_mute_read,
	       bcm2048_mute_write),
	__ATTR(audio_route, 0644, bcm2048_audio_route_read,
	       bcm2048_audio_route_write),
	__ATTR(dac_output, 0644, bcm2048_dac_output_read,
	       bcm2048_dac_output_write),
	__ATTR(fm_hi_lo_injection, 0644,
	       bcm2048_fm_hi_lo_injection_read,
	       bcm2048_fm_hi_lo_injection_write),
	__ATTR(fm_frequency, 0644, bcm2048_fm_frequency_read,
	       bcm2048_fm_frequency_write),
	__ATTR(fm_af_frequency, 0644,
	       bcm2048_fm_af_frequency_read,
	       bcm2048_fm_af_frequency_write),
	__ATTR(fm_deemphasis, 0644, bcm2048_fm_deemphasis_read,
	       bcm2048_fm_deemphasis_write),
	__ATTR(fm_rds_mask, 0644, bcm2048_fm_rds_mask_read,
	       bcm2048_fm_rds_mask_write),
	__ATTR(fm_best_tune_mode, 0644,
	       bcm2048_fm_best_tune_mode_read,
	       bcm2048_fm_best_tune_mode_write),
	__ATTR(fm_search_rssi_threshold, 0644,
	       bcm2048_fm_search_rssi_threshold_read,
	       bcm2048_fm_search_rssi_threshold_write),
	__ATTR(fm_search_mode_direction, 0644,
	       bcm2048_fm_search_mode_direction_read,
	       bcm2048_fm_search_mode_direction_write),
	__ATTR(fm_search_tune_mode, 0644,
	       bcm2048_fm_search_tune_mode_read,
	       bcm2048_fm_search_tune_mode_write),
	__ATTR(rds, 0644, bcm2048_rds_read,
	       bcm2048_rds_write),
	__ATTR(rds_b_block_mask, 0644,
	       bcm2048_rds_b_block_mask_read,
	       bcm2048_rds_b_block_mask_write),
	__ATTR(rds_b_block_match, 0644,
	       bcm2048_rds_b_block_match_read,
	       bcm2048_rds_b_block_match_write),
	__ATTR(rds_pi_mask, 0644, bcm2048_rds_pi_mask_read,
	       bcm2048_rds_pi_mask_write),
	__ATTR(rds_pi_match, 0644, bcm2048_rds_pi_match_read,
	       bcm2048_rds_pi_match_write),
	__ATTR(rds_wline, 0644, bcm2048_rds_wline_read,
	       bcm2048_rds_wline_write),
	__ATTR(rds_pi, 0444, bcm2048_rds_pi_read, NULL),
	__ATTR(rds_rt, 0444, bcm2048_rds_rt_read, NULL),
	__ATTR(rds_ps, 0444, bcm2048_rds_ps_read, NULL),
	__ATTR(fm_rds_flags, 0444, bcm2048_fm_rds_flags_read, NULL),
	__ATTR(region_bottom_frequency, 0444,
	       bcm2048_region_bottom_frequency_read, NULL),
	__ATTR(region_top_frequency, 0444,
	       bcm2048_region_top_frequency_read, NULL),
	__ATTR(fm_carrier_error, 0444,
	       bcm2048_fm_carrier_error_read, NULL),
	__ATTR(fm_rssi, 0444,
	       bcm2048_fm_rssi_read, NULL),
	__ATTR(region, 0644, bcm2048_region_read,
	       bcm2048_region_write),
	__ATTR(rds_data, 0444, bcm2048_rds_data_read, NULL),
};

static int bcm2048_sysfs_unregister_properties(struct bcm2048_device *bdev,
					       int size)
{
	int i;

	for (i = 0; i < size; i++)
		device_remove_file(&bdev->client->dev, &attrs[i]);

	return 0;
}

static int bcm2048_sysfs_register_properties(struct bcm2048_device *bdev)
{
	int err = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(attrs); i++) {
		if (device_create_file(&bdev->client->dev, &attrs[i]) != 0) {
			dev_err(&bdev->client->dev,
				"could not register sysfs entry\n");
			err = -EBUSY;
			bcm2048_sysfs_unregister_properties(bdev, i);
			break;
		}
	}

	return err;
}

static int bcm2048_fops_open(struct file *file)
{
	struct bcm2048_device *bdev = video_drvdata(file);

	bdev->users++;
	bdev->rd_index = 0;
	bdev->rds_data_available = 0;

	return 0;
}

static int bcm2048_fops_release(struct file *file)
{
	struct bcm2048_device *bdev = video_drvdata(file);

	bdev->users--;

	return 0;
}

static __poll_t bcm2048_fops_poll(struct file *file,
				  struct poll_table_struct *pts)
{
	struct bcm2048_device *bdev = video_drvdata(file);
	__poll_t retval = 0;

	poll_wait(file, &bdev->read_queue, pts);

	if (bdev->rds_data_available)
		retval = EPOLLIN | EPOLLRDNORM;

	return retval;
}

static ssize_t bcm2048_fops_read(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct bcm2048_device *bdev = video_drvdata(file);
	int i;
	int retval = 0;

	/* we return at least 3 bytes, one block */
	count = (count / 3) * 3; /* only multiples of 3 */
	if (count < 3)
		return -ENOBUFS;

	while (!bdev->rds_data_available) {
		if (file->f_flags & O_NONBLOCK) {
			retval = -EWOULDBLOCK;
			goto done;
		}
		/* interruptible_sleep_on(&bdev->read_queue); */
		if (wait_event_interruptible(bdev->read_queue,
					     bdev->rds_data_available) < 0) {
			retval = -EINTR;
			goto done;
		}
	}

	mutex_lock(&bdev->mutex);
	/* copy data to userspace */
	i = bdev->fifo_size - bdev->rd_index;
	if (count > i)
		count = (i / 3) * 3;

	i = 0;
	while (i < count) {
		unsigned char tmpbuf[3];

		tmpbuf[i] = bdev->rds_info.radio_text[bdev->rd_index + i + 2];
		tmpbuf[i + 1] =
			bdev->rds_info.radio_text[bdev->rd_index + i + 1];
		tmpbuf[i + 2] =
			(bdev->rds_info.radio_text[bdev->rd_index + i] &
			 0xf0) >> 4;
		if ((bdev->rds_info.radio_text[bdev->rd_index + i] &
		    BCM2048_RDS_CRC_MASK) == BCM2048_RDS_CRC_UNRECOVARABLE)
			tmpbuf[i + 2] |= 0x80;
		if (copy_to_user(buf + i, tmpbuf, 3)) {
			retval = -EFAULT;
			break;
		}
		i += 3;
	}

	bdev->rd_index += i;
	if (bdev->rd_index >= bdev->fifo_size)
		bdev->rds_data_available = 0;

	mutex_unlock(&bdev->mutex);
	if (retval == 0)
		retval = i;

done:
	return retval;
}

/*
 *	bcm2048_fops - file operations interface
 */
static const struct v4l2_file_operations bcm2048_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	/* for RDS read support */
	.open		= bcm2048_fops_open,
	.release	= bcm2048_fops_release,
	.read		= bcm2048_fops_read,
	.poll		= bcm2048_fops_poll
};

/*
 *	Video4Linux Interface
 */
static struct v4l2_queryctrl bcm2048_v4l2_queryctrl[] = {
	{
		.id		= V4L2_CID_AUDIO_VOLUME,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_BALANCE,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_BASS,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_TREBLE,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_MUTE,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Mute",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 1,
	},
	{
		.id		= V4L2_CID_AUDIO_LOUDNESS,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
};

static int bcm2048_vidioc_querycap(struct file *file, void *priv,
				   struct v4l2_capability *capability)
{
	struct bcm2048_device *bdev = video_get_drvdata(video_devdata(file));

	strscpy(capability->driver, BCM2048_DRIVER_NAME,
		sizeof(capability->driver));
	strscpy(capability->card, BCM2048_DRIVER_CARD,
		sizeof(capability->card));
	snprintf(capability->bus_info, 32, "I2C: 0x%X", bdev->client->addr);
	capability->device_caps = V4L2_CAP_TUNER | V4L2_CAP_RADIO |
					V4L2_CAP_HW_FREQ_SEEK;
	capability->capabilities = capability->device_caps |
		V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int bcm2048_vidioc_g_input(struct file *filp, void *priv,
				  unsigned int *i)
{
	*i = 0;

	return 0;
}

static int bcm2048_vidioc_s_input(struct file *filp, void *priv,
				  unsigned int i)
{
	if (i)
		return -EINVAL;

	return 0;
}

static int bcm2048_vidioc_queryctrl(struct file *file, void *priv,
				    struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bcm2048_v4l2_queryctrl); i++) {
		if (qc->id && qc->id == bcm2048_v4l2_queryctrl[i].id) {
			*qc = bcm2048_v4l2_queryctrl[i];
			return 0;
		}
	}

	return -EINVAL;
}

static int bcm2048_vidioc_g_ctrl(struct file *file, void *priv,
				 struct v4l2_control *ctrl)
{
	struct bcm2048_device *bdev = video_get_drvdata(video_devdata(file));
	int err = 0;

	if (!bdev)
		return -ENODEV;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		err = bcm2048_get_mute(bdev);
		if (err >= 0)
			ctrl->value = err;
		break;
	}

	return err;
}

static int bcm2048_vidioc_s_ctrl(struct file *file, void *priv,
				 struct v4l2_control *ctrl)
{
	struct bcm2048_device *bdev = video_get_drvdata(video_devdata(file));
	int err = 0;

	if (!bdev)
		return -ENODEV;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value) {
			if (bdev->power_state) {
				err = bcm2048_set_mute(bdev, ctrl->value);
				err |= bcm2048_deinit(bdev);
			}
		} else {
			if (!bdev->power_state) {
				err = bcm2048_init(bdev);
				err |= bcm2048_set_mute(bdev, ctrl->value);
			}
		}
		break;
	}

	return err;
}

static int bcm2048_vidioc_g_audio(struct file *file, void *priv,
				  struct v4l2_audio *audio)
{
	if (audio->index > 1)
		return -EINVAL;

	strncpy(audio->name, "Radio", 32);
	audio->capability = V4L2_AUDCAP_STEREO;

	return 0;
}

static int bcm2048_vidioc_s_audio(struct file *file, void *priv,
				  const struct v4l2_audio *audio)
{
	if (audio->index != 0)
		return -EINVAL;

	return 0;
}

static int bcm2048_vidioc_g_tuner(struct file *file, void *priv,
				  struct v4l2_tuner *tuner)
{
	struct bcm2048_device *bdev = video_get_drvdata(video_devdata(file));
	s8 f_error;
	s8 rssi;

	if (!bdev)
		return -ENODEV;

	if (tuner->index > 0)
		return -EINVAL;

	strncpy(tuner->name, "FM Receiver", 32);
	tuner->type = V4L2_TUNER_RADIO;
	tuner->rangelow =
		dev_to_v4l2(bcm2048_get_region_bottom_frequency(bdev));
	tuner->rangehigh =
		dev_to_v4l2(bcm2048_get_region_top_frequency(bdev));
	tuner->rxsubchans = V4L2_TUNER_SUB_STEREO;
	tuner->capability = V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LOW;
	tuner->audmode = V4L2_TUNER_MODE_STEREO;
	tuner->afc = 0;
	if (bdev->power_state) {
		/*
		 * Report frequencies with high carrier errors to have zero
		 * signal level
		 */
		f_error = bcm2048_get_fm_carrier_error(bdev);
		if (f_error < BCM2048_FREQ_ERROR_FLOOR ||
		    f_error > BCM2048_FREQ_ERROR_ROOF) {
			tuner->signal = 0;
		} else {
			/*
			 * RSSI level -60 dB is defined to report full
			 * signal strength
			 */
			rssi = bcm2048_get_fm_rssi(bdev);
			if (rssi >= BCM2048_RSSI_LEVEL_BASE) {
				tuner->signal = 0xFFFF;
			} else if (rssi > BCM2048_RSSI_LEVEL_ROOF) {
				tuner->signal = (rssi +
						 BCM2048_RSSI_LEVEL_ROOF_NEG)
						 * BCM2048_SIGNAL_MULTIPLIER;
			} else {
				tuner->signal = 0;
			}
		}
	} else {
		tuner->signal = 0;
	}

	return 0;
}

static int bcm2048_vidioc_s_tuner(struct file *file, void *priv,
				  const struct v4l2_tuner *tuner)
{
	struct bcm2048_device *bdev = video_get_drvdata(video_devdata(file));

	if (!bdev)
		return -ENODEV;

	if (tuner->index > 0)
		return -EINVAL;

	return 0;
}

static int bcm2048_vidioc_g_frequency(struct file *file, void *priv,
				      struct v4l2_frequency *freq)
{
	struct bcm2048_device *bdev = video_get_drvdata(video_devdata(file));
	int err = 0;
	int f;

	if (!bdev->power_state)
		return -ENODEV;

	freq->type = V4L2_TUNER_RADIO;
	f = bcm2048_get_fm_frequency(bdev);

	if (f < 0)
		err = f;
	else
		freq->frequency = dev_to_v4l2(f);

	return err;
}

static int bcm2048_vidioc_s_frequency(struct file *file, void *priv,
				      const struct v4l2_frequency *freq)
{
	struct bcm2048_device *bdev = video_get_drvdata(video_devdata(file));
	int err;

	if (freq->type != V4L2_TUNER_RADIO)
		return -EINVAL;

	if (!bdev->power_state)
		return -ENODEV;

	err = bcm2048_set_fm_frequency(bdev, v4l2_to_dev(freq->frequency));
	err |= bcm2048_set_fm_search_tune_mode(bdev, BCM2048_FM_PRE_SET_MODE);

	return err;
}

static int bcm2048_vidioc_s_hw_freq_seek(struct file *file, void *priv,
					 const struct v4l2_hw_freq_seek *seek)
{
	struct bcm2048_device *bdev = video_get_drvdata(video_devdata(file));
	int err;

	if (!bdev->power_state)
		return -ENODEV;

	if ((seek->tuner != 0) || (seek->type != V4L2_TUNER_RADIO))
		return -EINVAL;

	err = bcm2048_set_fm_search_mode_direction(bdev, seek->seek_upward);
	err |= bcm2048_set_fm_search_tune_mode(bdev,
					       BCM2048_FM_AUTO_SEARCH_MODE);

	return err;
}

static const struct v4l2_ioctl_ops bcm2048_ioctl_ops = {
	.vidioc_querycap	= bcm2048_vidioc_querycap,
	.vidioc_g_input		= bcm2048_vidioc_g_input,
	.vidioc_s_input		= bcm2048_vidioc_s_input,
	.vidioc_queryctrl	= bcm2048_vidioc_queryctrl,
	.vidioc_g_ctrl		= bcm2048_vidioc_g_ctrl,
	.vidioc_s_ctrl		= bcm2048_vidioc_s_ctrl,
	.vidioc_g_audio		= bcm2048_vidioc_g_audio,
	.vidioc_s_audio		= bcm2048_vidioc_s_audio,
	.vidioc_g_tuner		= bcm2048_vidioc_g_tuner,
	.vidioc_s_tuner		= bcm2048_vidioc_s_tuner,
	.vidioc_g_frequency	= bcm2048_vidioc_g_frequency,
	.vidioc_s_frequency	= bcm2048_vidioc_s_frequency,
	.vidioc_s_hw_freq_seek  = bcm2048_vidioc_s_hw_freq_seek,
};

/*
 * bcm2048_viddev_template - video device interface
 */
static const struct video_device bcm2048_viddev_template = {
	.fops			= &bcm2048_fops,
	.name			= BCM2048_DRIVER_NAME,
	.release		= video_device_release_empty,
	.ioctl_ops		= &bcm2048_ioctl_ops,
};

/*
 *	I2C driver interface
 */
static int bcm2048_i2c_driver_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	struct bcm2048_device *bdev;
	int err;

	bdev = kzalloc(sizeof(*bdev), GFP_KERNEL);
	if (!bdev) {
		err = -ENOMEM;
		goto exit;
	}

	bdev->client = client;
	i2c_set_clientdata(client, bdev);
	mutex_init(&bdev->mutex);
	init_completion(&bdev->compl);
	INIT_WORK(&bdev->work, bcm2048_work);

	if (client->irq) {
		err = request_irq(client->irq,
				  bcm2048_handler, IRQF_TRIGGER_FALLING,
				  client->name, bdev);
		if (err < 0) {
			dev_err(&client->dev, "Could not request IRQ\n");
			goto free_bdev;
		}
		dev_dbg(&client->dev, "IRQ requested.\n");
	} else {
		dev_dbg(&client->dev, "IRQ not configured. Using timeouts.\n");
	}

	bdev->videodev = bcm2048_viddev_template;
	video_set_drvdata(&bdev->videodev, bdev);
	if (video_register_device(&bdev->videodev, VFL_TYPE_RADIO, radio_nr)) {
		dev_dbg(&client->dev, "Could not register video device.\n");
		err = -EIO;
		goto free_irq;
	}

	err = bcm2048_sysfs_register_properties(bdev);
	if (err < 0) {
		dev_dbg(&client->dev, "Could not register sysfs interface.\n");
		goto free_registration;
	}

	err = bcm2048_probe(bdev);
	if (err < 0) {
		dev_dbg(&client->dev, "Failed to probe device information.\n");
		goto free_sysfs;
	}

	return 0;

free_sysfs:
	bcm2048_sysfs_unregister_properties(bdev, ARRAY_SIZE(attrs));
free_registration:
	video_unregister_device(&bdev->videodev);
free_irq:
	if (client->irq)
		free_irq(client->irq, bdev);
free_bdev:
	i2c_set_clientdata(client, NULL);
	kfree(bdev);
exit:
	return err;
}

static int bcm2048_i2c_driver_remove(struct i2c_client *client)
{
	struct bcm2048_device *bdev = i2c_get_clientdata(client);

	if (!client->adapter)
		return -ENODEV;

	if (bdev) {
		bcm2048_sysfs_unregister_properties(bdev, ARRAY_SIZE(attrs));
		video_unregister_device(&bdev->videodev);

		if (bdev->power_state)
			bcm2048_set_power_state(bdev, BCM2048_POWER_OFF);

		if (client->irq > 0)
			free_irq(client->irq, bdev);

		cancel_work_sync(&bdev->work);

		kfree(bdev);
	}

	return 0;
}

/*
 *	bcm2048_i2c_driver - i2c driver interface
 */
static const struct i2c_device_id bcm2048_id[] = {
	{ "bcm2048", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, bcm2048_id);

static struct i2c_driver bcm2048_i2c_driver = {
	.driver		= {
		.name	= BCM2048_DRIVER_NAME,
	},
	.probe		= bcm2048_i2c_driver_probe,
	.remove		= bcm2048_i2c_driver_remove,
	.id_table	= bcm2048_id,
};

module_i2c_driver(bcm2048_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(BCM2048_DRIVER_AUTHOR);
MODULE_DESCRIPTION(BCM2048_DRIVER_DESC);
MODULE_VERSION("0.0.2");
