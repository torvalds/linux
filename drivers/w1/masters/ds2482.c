/**
 * ds2482.c - provides i2c to w1-master bridge(s)
 * Copyright (C) 2005  Ben Gardner <bgardner@wabtec.com>
 *
 * The DS2482 is a sensor chip made by Dallas Semiconductor (Maxim).
 * It is a I2C to 1-wire bridge.
 * There are two variations: -100 and -800, which have 1 or 8 1-wire ports.
 * The complete datasheet can be obtained from MAXIM's website at:
 *   http://www.maxim-ic.com/quick_view2.cfm/qv_pk/4382
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <asm/delay.h>

#include "../w1.h"
#include "../w1_int.h"

/**
 * The DS2482 registers - there are 3 registers that are addressed by a read
 * pointer. The read pointer is set by the last command executed.
 *
 * To read the data, issue a register read for any address
 */
#define DS2482_CMD_RESET		0xF0	/* No param */
#define DS2482_CMD_SET_READ_PTR		0xE1	/* Param: DS2482_PTR_CODE_xxx */
#define DS2482_CMD_CHANNEL_SELECT	0xC3	/* Param: Channel byte - DS2482-800 only */
#define DS2482_CMD_WRITE_CONFIG		0xD2	/* Param: Config byte */
#define DS2482_CMD_1WIRE_RESET		0xB4	/* Param: None */
#define DS2482_CMD_1WIRE_SINGLE_BIT	0x87	/* Param: Bit byte (bit7) */
#define DS2482_CMD_1WIRE_WRITE_BYTE	0xA5	/* Param: Data byte */
#define DS2482_CMD_1WIRE_READ_BYTE	0x96	/* Param: None */
/* Note to read the byte, Set the ReadPtr to Data then read (any addr) */
#define DS2482_CMD_1WIRE_TRIPLET	0x78	/* Param: Dir byte (bit7) */

/* Values for DS2482_CMD_SET_READ_PTR */
#define DS2482_PTR_CODE_STATUS		0xF0
#define DS2482_PTR_CODE_DATA		0xE1
#define DS2482_PTR_CODE_CHANNEL		0xD2	/* DS2482-800 only */
#define DS2482_PTR_CODE_CONFIG		0xC3

/**
 * Configure Register bit definitions
 * The top 4 bits always read 0.
 * To write, the top nibble must be the 1's compl. of the low nibble.
 */
#define DS2482_REG_CFG_1WS		0x08	/* 1-wire speed */
#define DS2482_REG_CFG_SPU		0x04	/* strong pull-up */
#define DS2482_REG_CFG_PPM		0x02	/* presence pulse masking */
#define DS2482_REG_CFG_APU		0x01	/* active pull-up */


/**
 * Write and verify codes for the CHANNEL_SELECT command (DS2482-800 only).
 * To set the channel, write the value at the index of the channel.
 * Read and compare against the corresponding value to verify the change.
 */
static const u8 ds2482_chan_wr[8] =
	{ 0xF0, 0xE1, 0xD2, 0xC3, 0xB4, 0xA5, 0x96, 0x87 };
static const u8 ds2482_chan_rd[8] =
	{ 0xB8, 0xB1, 0xAA, 0xA3, 0x9C, 0x95, 0x8E, 0x87 };


/**
 * Status Register bit definitions (read only)
 */
#define DS2482_REG_STS_DIR		0x80
#define DS2482_REG_STS_TSB		0x40
#define DS2482_REG_STS_SBR		0x20
#define DS2482_REG_STS_RST		0x10
#define DS2482_REG_STS_LL		0x08
#define DS2482_REG_STS_SD		0x04
#define DS2482_REG_STS_PPD		0x02
#define DS2482_REG_STS_1WB		0x01


static int ds2482_probe(struct i2c_client *client,
			const struct i2c_device_id *id);
static int ds2482_remove(struct i2c_client *client);


/**
 * Driver data (common to all clients)
 */
static const struct i2c_device_id ds2482_id[] = {
	{ "ds2482", 0 },
	{ }
};

static struct i2c_driver ds2482_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "ds2482",
	},
	.probe		= ds2482_probe,
	.remove		= ds2482_remove,
	.id_table	= ds2482_id,
};

/*
 * Client data (each client gets its own)
 */

struct ds2482_data;

struct ds2482_w1_chan {
	struct ds2482_data	*pdev;
	u8			channel;
	struct w1_bus_master	w1_bm;
};

struct ds2482_data {
	struct i2c_client	*client;
	struct mutex		access_lock;

	/* 1-wire interface(s) */
	int			w1_count;	/* 1 or 8 */
	struct ds2482_w1_chan	w1_ch[8];

	/* per-device values */
	u8			channel;
	u8			read_prt;	/* see DS2482_PTR_CODE_xxx */
	u8			reg_config;
};


/**
 * Helper to calculate values for configuration register
 * @param conf the raw config value
 * @return the value w/ complements that can be written to register
 */
static inline u8 ds2482_calculate_config(u8 conf)
{
	return conf | ((~conf & 0x0f) << 4);
}


/**
 * Sets the read pointer.
 * @param pdev		The ds2482 client pointer
 * @param read_ptr	see DS2482_PTR_CODE_xxx above
 * @return -1 on failure, 0 on success
 */
static inline int ds2482_select_register(struct ds2482_data *pdev, u8 read_ptr)
{
	if (pdev->read_prt != read_ptr) {
		if (i2c_smbus_write_byte_data(pdev->client,
					      DS2482_CMD_SET_READ_PTR,
					      read_ptr) < 0)
			return -1;

		pdev->read_prt = read_ptr;
	}
	return 0;
}

/**
 * Sends a command without a parameter
 * @param pdev	The ds2482 client pointer
 * @param cmd	DS2482_CMD_RESET,
 *		DS2482_CMD_1WIRE_RESET,
 *		DS2482_CMD_1WIRE_READ_BYTE
 * @return -1 on failure, 0 on success
 */
static inline int ds2482_send_cmd(struct ds2482_data *pdev, u8 cmd)
{
	if (i2c_smbus_write_byte(pdev->client, cmd) < 0)
		return -1;

	pdev->read_prt = DS2482_PTR_CODE_STATUS;
	return 0;
}

/**
 * Sends a command with a parameter
 * @param pdev	The ds2482 client pointer
 * @param cmd	DS2482_CMD_WRITE_CONFIG,
 *		DS2482_CMD_1WIRE_SINGLE_BIT,
 *		DS2482_CMD_1WIRE_WRITE_BYTE,
 *		DS2482_CMD_1WIRE_TRIPLET
 * @param byte	The data to send
 * @return -1 on failure, 0 on success
 */
static inline int ds2482_send_cmd_data(struct ds2482_data *pdev,
				       u8 cmd, u8 byte)
{
	if (i2c_smbus_write_byte_data(pdev->client, cmd, byte) < 0)
		return -1;

	/* all cmds leave in STATUS, except CONFIG */
	pdev->read_prt = (cmd != DS2482_CMD_WRITE_CONFIG) ?
			 DS2482_PTR_CODE_STATUS : DS2482_PTR_CODE_CONFIG;
	return 0;
}


/*
 * 1-Wire interface code
 */

#define DS2482_WAIT_IDLE_TIMEOUT	100

/**
 * Waits until the 1-wire interface is idle (not busy)
 *
 * @param pdev Pointer to the device structure
 * @return the last value read from status or -1 (failure)
 */
static int ds2482_wait_1wire_idle(struct ds2482_data *pdev)
{
	int temp = -1;
	int retries = 0;

	if (!ds2482_select_register(pdev, DS2482_PTR_CODE_STATUS)) {
		do {
			temp = i2c_smbus_read_byte(pdev->client);
		} while ((temp >= 0) && (temp & DS2482_REG_STS_1WB) &&
			 (++retries < DS2482_WAIT_IDLE_TIMEOUT));
	}

	if (retries >= DS2482_WAIT_IDLE_TIMEOUT)
		pr_err("%s: timeout on channel %d\n",
		       __func__, pdev->channel);

	return temp;
}

/**
 * Selects a w1 channel.
 * The 1-wire interface must be idle before calling this function.
 *
 * @param pdev		The ds2482 client pointer
 * @param channel	0-7
 * @return		-1 (failure) or 0 (success)
 */
static int ds2482_set_channel(struct ds2482_data *pdev, u8 channel)
{
	if (i2c_smbus_write_byte_data(pdev->client, DS2482_CMD_CHANNEL_SELECT,
				      ds2482_chan_wr[channel]) < 0)
		return -1;

	pdev->read_prt = DS2482_PTR_CODE_CHANNEL;
	pdev->channel = -1;
	if (i2c_smbus_read_byte(pdev->client) == ds2482_chan_rd[channel]) {
		pdev->channel = channel;
		return 0;
	}
	return -1;
}


/**
 * Performs the touch-bit function, which writes a 0 or 1 and reads the level.
 *
 * @param data	The ds2482 channel pointer
 * @param bit	The level to write: 0 or non-zero
 * @return	The level read: 0 or 1
 */
static u8 ds2482_w1_touch_bit(void *data, u8 bit)
{
	struct ds2482_w1_chan *pchan = data;
	struct ds2482_data    *pdev = pchan->pdev;
	int status = -1;

	mutex_lock(&pdev->access_lock);

	/* Select the channel */
	ds2482_wait_1wire_idle(pdev);
	if (pdev->w1_count > 1)
		ds2482_set_channel(pdev, pchan->channel);

	/* Send the touch command, wait until 1WB == 0, return the status */
	if (!ds2482_send_cmd_data(pdev, DS2482_CMD_1WIRE_SINGLE_BIT,
				  bit ? 0xFF : 0))
		status = ds2482_wait_1wire_idle(pdev);

	mutex_unlock(&pdev->access_lock);

	return (status & DS2482_REG_STS_SBR) ? 1 : 0;
}

/**
 * Performs the triplet function, which reads two bits and writes a bit.
 * The bit written is determined by the two reads:
 *   00 => dbit, 01 => 0, 10 => 1
 *
 * @param data	The ds2482 channel pointer
 * @param dbit	The direction to choose if both branches are valid
 * @return	b0=read1 b1=read2 b3=bit written
 */
static u8 ds2482_w1_triplet(void *data, u8 dbit)
{
	struct ds2482_w1_chan *pchan = data;
	struct ds2482_data    *pdev = pchan->pdev;
	int status = (3 << 5);

	mutex_lock(&pdev->access_lock);

	/* Select the channel */
	ds2482_wait_1wire_idle(pdev);
	if (pdev->w1_count > 1)
		ds2482_set_channel(pdev, pchan->channel);

	/* Send the triplet command, wait until 1WB == 0, return the status */
	if (!ds2482_send_cmd_data(pdev, DS2482_CMD_1WIRE_TRIPLET,
				  dbit ? 0xFF : 0))
		status = ds2482_wait_1wire_idle(pdev);

	mutex_unlock(&pdev->access_lock);

	/* Decode the status */
	return (status >> 5);
}

/**
 * Performs the write byte function.
 *
 * @param data	The ds2482 channel pointer
 * @param byte	The value to write
 */
static void ds2482_w1_write_byte(void *data, u8 byte)
{
	struct ds2482_w1_chan *pchan = data;
	struct ds2482_data    *pdev = pchan->pdev;

	mutex_lock(&pdev->access_lock);

	/* Select the channel */
	ds2482_wait_1wire_idle(pdev);
	if (pdev->w1_count > 1)
		ds2482_set_channel(pdev, pchan->channel);

	/* Send the write byte command */
	ds2482_send_cmd_data(pdev, DS2482_CMD_1WIRE_WRITE_BYTE, byte);

	mutex_unlock(&pdev->access_lock);
}

/**
 * Performs the read byte function.
 *
 * @param data	The ds2482 channel pointer
 * @return	The value read
 */
static u8 ds2482_w1_read_byte(void *data)
{
	struct ds2482_w1_chan *pchan = data;
	struct ds2482_data    *pdev = pchan->pdev;
	int result;

	mutex_lock(&pdev->access_lock);

	/* Select the channel */
	ds2482_wait_1wire_idle(pdev);
	if (pdev->w1_count > 1)
		ds2482_set_channel(pdev, pchan->channel);

	/* Send the read byte command */
	ds2482_send_cmd(pdev, DS2482_CMD_1WIRE_READ_BYTE);

	/* Wait until 1WB == 0 */
	ds2482_wait_1wire_idle(pdev);

	/* Select the data register */
	ds2482_select_register(pdev, DS2482_PTR_CODE_DATA);

	/* Read the data byte */
	result = i2c_smbus_read_byte(pdev->client);

	mutex_unlock(&pdev->access_lock);

	return result;
}


/**
 * Sends a reset on the 1-wire interface
 *
 * @param data	The ds2482 channel pointer
 * @return	0=Device present, 1=No device present or error
 */
static u8 ds2482_w1_reset_bus(void *data)
{
	struct ds2482_w1_chan *pchan = data;
	struct ds2482_data    *pdev = pchan->pdev;
	int err;
	u8 retval = 1;

	mutex_lock(&pdev->access_lock);

	/* Select the channel */
	ds2482_wait_1wire_idle(pdev);
	if (pdev->w1_count > 1)
		ds2482_set_channel(pdev, pchan->channel);

	/* Send the reset command */
	err = ds2482_send_cmd(pdev, DS2482_CMD_1WIRE_RESET);
	if (err >= 0) {
		/* Wait until the reset is complete */
		err = ds2482_wait_1wire_idle(pdev);
		retval = !(err & DS2482_REG_STS_PPD);

		/* If the chip did reset since detect, re-config it */
		if (err & DS2482_REG_STS_RST)
			ds2482_send_cmd_data(pdev, DS2482_CMD_WRITE_CONFIG,
					     ds2482_calculate_config(0x00));
	}

	mutex_unlock(&pdev->access_lock);

	return retval;
}

static u8 ds2482_w1_set_pullup(void *data, int delay)
{
	struct ds2482_w1_chan *pchan = data;
	struct ds2482_data    *pdev = pchan->pdev;
	u8 retval = 1;

	/* if delay is non-zero activate the pullup,
	 * the strong pullup will be automatically deactivated
	 * by the master, so do not explicitly deactive it
	 */
	if (delay) {
		/* both waits are crucial, otherwise devices might not be
		 * powered long enough, causing e.g. a w1_therm sensor to
		 * provide wrong conversion results
		 */
		ds2482_wait_1wire_idle(pdev);
		/* note: it seems like both SPU and APU have to be set! */
		retval = ds2482_send_cmd_data(pdev, DS2482_CMD_WRITE_CONFIG,
			ds2482_calculate_config(DS2482_REG_CFG_SPU |
						DS2482_REG_CFG_APU));
		ds2482_wait_1wire_idle(pdev);
	}

	return retval;
}


static int ds2482_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ds2482_data *data;
	int err = -ENODEV;
	int temp1;
	int idx;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA |
				     I2C_FUNC_SMBUS_BYTE))
		return -ENODEV;

	if (!(data = kzalloc(sizeof(struct ds2482_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	data->client = client;
	i2c_set_clientdata(client, data);

	/* Reset the device (sets the read_ptr to status) */
	if (ds2482_send_cmd(data, DS2482_CMD_RESET) < 0) {
		dev_warn(&client->dev, "DS2482 reset failed.\n");
		goto exit_free;
	}

	/* Sleep at least 525ns to allow the reset to complete */
	ndelay(525);

	/* Read the status byte - only reset bit and line should be set */
	temp1 = i2c_smbus_read_byte(client);
	if (temp1 != (DS2482_REG_STS_LL | DS2482_REG_STS_RST)) {
		dev_warn(&client->dev, "DS2482 reset status "
			 "0x%02X - not a DS2482\n", temp1);
		goto exit_free;
	}

	/* Detect the 8-port version */
	data->w1_count = 1;
	if (ds2482_set_channel(data, 7) == 0)
		data->w1_count = 8;

	/* Set all config items to 0 (off) */
	ds2482_send_cmd_data(data, DS2482_CMD_WRITE_CONFIG,
		ds2482_calculate_config(0x00));

	mutex_init(&data->access_lock);

	/* Register 1-wire interface(s) */
	for (idx = 0; idx < data->w1_count; idx++) {
		data->w1_ch[idx].pdev = data;
		data->w1_ch[idx].channel = idx;

		/* Populate all the w1 bus master stuff */
		data->w1_ch[idx].w1_bm.data       = &data->w1_ch[idx];
		data->w1_ch[idx].w1_bm.read_byte  = ds2482_w1_read_byte;
		data->w1_ch[idx].w1_bm.write_byte = ds2482_w1_write_byte;
		data->w1_ch[idx].w1_bm.touch_bit  = ds2482_w1_touch_bit;
		data->w1_ch[idx].w1_bm.triplet    = ds2482_w1_triplet;
		data->w1_ch[idx].w1_bm.reset_bus  = ds2482_w1_reset_bus;
		data->w1_ch[idx].w1_bm.set_pullup = ds2482_w1_set_pullup;

		err = w1_add_master_device(&data->w1_ch[idx].w1_bm);
		if (err) {
			data->w1_ch[idx].pdev = NULL;
			goto exit_w1_remove;
		}
	}

	return 0;

exit_w1_remove:
	for (idx = 0; idx < data->w1_count; idx++) {
		if (data->w1_ch[idx].pdev != NULL)
			w1_remove_master_device(&data->w1_ch[idx].w1_bm);
	}
exit_free:
	kfree(data);
exit:
	return err;
}

static int ds2482_remove(struct i2c_client *client)
{
	struct ds2482_data   *data = i2c_get_clientdata(client);
	int idx;

	/* Unregister the 1-wire bridge(s) */
	for (idx = 0; idx < data->w1_count; idx++) {
		if (data->w1_ch[idx].pdev != NULL)
			w1_remove_master_device(&data->w1_ch[idx].w1_bm);
	}

	/* Free the memory */
	kfree(data);
	return 0;
}

module_i2c_driver(ds2482_driver);

MODULE_AUTHOR("Ben Gardner <bgardner@wabtec.com>");
MODULE_DESCRIPTION("DS2482 driver");
MODULE_LICENSE("GPL");
