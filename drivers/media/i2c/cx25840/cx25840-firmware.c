/* cx25840 firmware functions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/firmware.h>
#include <media/v4l2-common.h>
#include <media/drv-intf/cx25840.h>

#include "cx25840-core.h"

/*
 * Mike Isely <isely@pobox.com> - The FWSEND parameter controls the
 * size of the firmware chunks sent down the I2C bus to the chip.
 * Previously this had been set to 1024 but unfortunately some I2C
 * implementations can't transfer data in such big gulps.
 * Specifically, the pvrusb2 driver has a hard limit of around 60
 * bytes, due to the encapsulation there of I2C traffic into USB
 * messages.  So we have to significantly reduce this parameter.
 */
#define FWSEND 48

#define FWDEV(x) &((x)->dev)

static char *firmware = "";

module_param(firmware, charp, 0444);

MODULE_PARM_DESC(firmware, "Firmware image to load");

static void start_fw_load(struct i2c_client *client)
{
	/* DL_ADDR_LB=0 DL_ADDR_HB=0 */
	cx25840_write(client, 0x800, 0x00);
	cx25840_write(client, 0x801, 0x00);
	// DL_MAP=3 DL_AUTO_INC=0 DL_ENABLE=1
	cx25840_write(client, 0x803, 0x0b);
	/* AUTO_INC_DIS=1 */
	cx25840_write(client, 0x000, 0x20);
}

static void end_fw_load(struct i2c_client *client)
{
	/* AUTO_INC_DIS=0 */
	cx25840_write(client, 0x000, 0x00);
	/* DL_ENABLE=0 */
	cx25840_write(client, 0x803, 0x03);
}

#define CX2388x_FIRMWARE "v4l-cx23885-avcore-01.fw"
#define CX231xx_FIRMWARE "v4l-cx231xx-avcore-01.fw"
#define CX25840_FIRMWARE "v4l-cx25840.fw"

static const char *get_fw_name(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));

	if (firmware[0])
		return firmware;
	if (is_cx2388x(state))
		return CX2388x_FIRMWARE;
	if (is_cx231xx(state))
		return CX231xx_FIRMWARE;
	return CX25840_FIRMWARE;
}

static int check_fw_load(struct i2c_client *client, int size)
{
	/* DL_ADDR_HB DL_ADDR_LB */
	int s = cx25840_read(client, 0x801) << 8;
	s |= cx25840_read(client, 0x800);

	if (size != s) {
		v4l_err(client, "firmware %s load failed\n",
				get_fw_name(client));
		return -EINVAL;
	}

	v4l_info(client, "loaded %s firmware (%d bytes)\n",
			get_fw_name(client), size);
	return 0;
}

static int fw_write(struct i2c_client *client, const u8 *data, int size)
{
	if (i2c_master_send(client, data, size) < size) {
		v4l_err(client, "firmware load i2c failure\n");
		return -ENOSYS;
	}

	return 0;
}

int cx25840_loadfw(struct i2c_client *client)
{
	struct cx25840_state *state = to_state(i2c_get_clientdata(client));
	const struct firmware *fw = NULL;
	u8 buffer[FWSEND];
	const u8 *ptr;
	const char *fwname = get_fw_name(client);
	int size, retval;
	int max_buf_size = FWSEND;
	u32 gpio_oe = 0, gpio_da = 0;

	if (is_cx2388x(state)) {
		/* Preserve the GPIO OE and output bits */
		gpio_oe = cx25840_read(client, 0x160);
		gpio_da = cx25840_read(client, 0x164);
	}

	/* cx231xx cannot accept more than 16 bytes at a time */
	if (is_cx231xx(state) && max_buf_size > 16)
		max_buf_size = 16;

	if (request_firmware(&fw, fwname, FWDEV(client)) != 0) {
		v4l_err(client, "unable to open firmware %s\n", fwname);
		return -EINVAL;
	}

	start_fw_load(client);

	buffer[0] = 0x08;
	buffer[1] = 0x02;

	size = fw->size;
	ptr = fw->data;
	while (size > 0) {
		int len = min(max_buf_size - 2, size);

		memcpy(buffer + 2, ptr, len);

		retval = fw_write(client, buffer, len + 2);

		if (retval < 0) {
			release_firmware(fw);
			return retval;
		}

		size -= len;
		ptr += len;
	}

	end_fw_load(client);

	size = fw->size;
	release_firmware(fw);

	if (is_cx2388x(state)) {
		/* Restore GPIO configuration after f/w load */
		cx25840_write(client, 0x160, gpio_oe);
		cx25840_write(client, 0x164, gpio_da);
	}

	return check_fw_load(client, size);
}

MODULE_FIRMWARE(CX2388x_FIRMWARE);
MODULE_FIRMWARE(CX231xx_FIRMWARE);
MODULE_FIRMWARE(CX25840_FIRMWARE);

