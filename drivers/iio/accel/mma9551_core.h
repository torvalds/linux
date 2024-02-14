/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common code for Freescale MMA955x Intelligent Sensor Platform drivers
 * Copyright (c) 2014, Intel Corporation.
 */

#ifndef _MMA9551_CORE_H_
#define _MMA9551_CORE_H_

/* Applications IDs */
#define MMA9551_APPID_VERSION		0x00
#define MMA9551_APPID_GPIO		0x03
#define MMA9551_APPID_AFE		0x06
#define MMA9551_APPID_TILT		0x0B
#define MMA9551_APPID_SLEEP_WAKE	0x12
#define MMA9551_APPID_PEDOMETER	        0x15
#define MMA9551_APPID_RSC		0x17
#define MMA9551_APPID_NONE		0xff

/* Reset/Suspend/Clear application app masks */
#define MMA9551_RSC_PED			BIT(21)

#define MMA9551_AUTO_SUSPEND_DELAY_MS	2000

enum mma9551_gpio_pin {
	mma9551_gpio6 = 0,
	mma9551_gpio7,
	mma9551_gpio8,
	mma9551_gpio9,
	mma9551_gpio_max = mma9551_gpio9,
};

#define MMA9551_ACCEL_CHANNEL(axis) {				\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

int mma9551_read_config_byte(struct i2c_client *client, u8 app_id,
			     u16 reg, u8 *val);
int mma9551_write_config_byte(struct i2c_client *client, u8 app_id,
			      u16 reg, u8 val);
int mma9551_read_status_byte(struct i2c_client *client, u8 app_id,
			     u16 reg, u8 *val);
int mma9551_read_config_word(struct i2c_client *client, u8 app_id,
			     u16 reg, u16 *val);
int mma9551_write_config_word(struct i2c_client *client, u8 app_id,
			      u16 reg, u16 val);
int mma9551_read_status_word(struct i2c_client *client, u8 app_id,
			     u16 reg, u16 *val);
int mma9551_read_config_words(struct i2c_client *client, u8 app_id,
			      u16 reg, u8 len, u16 *buf);
int mma9551_read_status_words(struct i2c_client *client, u8 app_id,
			      u16 reg, u8 len, u16 *buf);
int mma9551_write_config_words(struct i2c_client *client, u8 app_id,
			       u16 reg, u8 len, u16 *buf);
int mma9551_update_config_bits(struct i2c_client *client, u8 app_id,
			       u16 reg, u8 mask, u8 val);
int mma9551_gpio_config(struct i2c_client *client, enum mma9551_gpio_pin pin,
			u8 app_id, u8 bitnum, int polarity);
int mma9551_read_version(struct i2c_client *client);
int mma9551_set_device_state(struct i2c_client *client, bool enable);
int mma9551_set_power_state(struct i2c_client *client, bool on);
void mma9551_sleep(int freq);
int mma9551_read_accel_chan(struct i2c_client *client,
			    const struct iio_chan_spec *chan,
			    int *val, int *val2);
int mma9551_read_accel_scale(int *val, int *val2);
int mma9551_app_reset(struct i2c_client *client, u32 app_mask);

#endif /* _MMA9551_CORE_H_ */
