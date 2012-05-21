/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 * Copyright (C) 2010 Devin Heitmueller <dheitmueller@kernellabs.com>
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
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/firmware.h>

#include "as102_drv.h"
#include "as102_fw.h"

char as102_st_fw1[] = "as102_data1_st.hex";
char as102_st_fw2[] = "as102_data2_st.hex";
char as102_dt_fw1[] = "as102_data1_dt.hex";
char as102_dt_fw2[] = "as102_data2_dt.hex";

static unsigned char atohx(unsigned char *dst, char *src)
{
	unsigned char value = 0;

	char msb = tolower(*src) - '0';
	char lsb = tolower(*(src + 1)) - '0';

	if (msb > 9)
		msb -= 7;
	if (lsb > 9)
		lsb -= 7;

	*dst = value = ((msb & 0xF) << 4) | (lsb & 0xF);
	return value;
}

/*
 * Parse INTEL HEX firmware file to extract address and data.
 */
static int parse_hex_line(unsigned char *fw_data, unsigned char *addr,
			  unsigned char *data, int *dataLength,
			  unsigned char *addr_has_changed) {

	int count = 0;
	unsigned char *src, dst;

	if (*fw_data++ != ':') {
		pr_err("invalid firmware file\n");
		return -EFAULT;
	}

	/* locate end of line */
	for (src = fw_data; *src != '\n'; src += 2) {
		atohx(&dst, src);
		/* parse line to split addr / data */
		switch (count) {
		case 0:
			*dataLength = dst;
			break;
		case 1:
			addr[2] = dst;
			break;
		case 2:
			addr[3] = dst;
			break;
		case 3:
			/* check if data is an address */
			if (dst == 0x04)
				*addr_has_changed = 1;
			else
				*addr_has_changed = 0;
			break;
		case  4:
		case  5:
			if (*addr_has_changed)
				addr[(count - 4)] = dst;
			else
				data[(count - 4)] = dst;
			break;
		default:
			data[(count - 4)] = dst;
			break;
		}
		count++;
	}

	/* return read value + ':' + '\n' */
	return (count * 2) + 2;
}

static int as102_firmware_upload(struct as10x_bus_adapter_t *bus_adap,
				 unsigned char *cmd,
				 const struct firmware *firmware) {

	struct as10x_fw_pkt_t fw_pkt;
	int total_read_bytes = 0, errno = 0;
	unsigned char addr_has_changed = 0;

	ENTER();

	for (total_read_bytes = 0; total_read_bytes < firmware->size; ) {
		int read_bytes = 0, data_len = 0;

		/* parse intel hex line */
		read_bytes = parse_hex_line(
				(u8 *) (firmware->data + total_read_bytes),
				fw_pkt.raw.address,
				fw_pkt.raw.data,
				&data_len,
				&addr_has_changed);

		if (read_bytes <= 0)
			goto error;

		/* detect the end of file */
		total_read_bytes += read_bytes;
		if (total_read_bytes == firmware->size) {
			fw_pkt.u.request[0] = 0x00;
			fw_pkt.u.request[1] = 0x03;

			/* send EOF command */
			errno = bus_adap->ops->upload_fw_pkt(bus_adap,
							     (uint8_t *)
							     &fw_pkt, 2, 0);
			if (errno < 0)
				goto error;
		} else {
			if (!addr_has_changed) {
				/* prepare command to send */
				fw_pkt.u.request[0] = 0x00;
				fw_pkt.u.request[1] = 0x01;

				data_len += sizeof(fw_pkt.u.request);
				data_len += sizeof(fw_pkt.raw.address);

				/* send cmd to device */
				errno = bus_adap->ops->upload_fw_pkt(bus_adap,
								     (uint8_t *)
								     &fw_pkt,
								     data_len,
								     0);
				if (errno < 0)
					goto error;
			}
		}
	}
error:
	LEAVE();
	return (errno == 0) ? total_read_bytes : errno;
}

int as102_fw_upload(struct as10x_bus_adapter_t *bus_adap)
{
	int errno = -EFAULT;
	const struct firmware *firmware = NULL;
	unsigned char *cmd_buf = NULL;
	char *fw1, *fw2;
	struct usb_device *dev = bus_adap->usb_dev;

	ENTER();

	/* select fw file to upload */
	if (dual_tuner) {
		fw1 = as102_dt_fw1;
		fw2 = as102_dt_fw2;
	} else {
		fw1 = as102_st_fw1;
		fw2 = as102_st_fw2;
	}

	/* allocate buffer to store firmware upload command and data */
	cmd_buf = kzalloc(MAX_FW_PKT_SIZE, GFP_KERNEL);
	if (cmd_buf == NULL) {
		errno = -ENOMEM;
		goto error;
	}

	/* request kernel to locate firmware file: part1 */
	errno = request_firmware(&firmware, fw1, &dev->dev);
	if (errno < 0) {
		pr_err("%s: unable to locate firmware file: %s\n",
		       DRIVER_NAME, fw1);
		goto error;
	}

	/* initiate firmware upload */
	errno = as102_firmware_upload(bus_adap, cmd_buf, firmware);
	if (errno < 0) {
		pr_err("%s: error during firmware upload part1\n",
		       DRIVER_NAME);
		goto error;
	}

	pr_info("%s: firmware: %s loaded with success\n",
		DRIVER_NAME, fw1);
	release_firmware(firmware);

	/* wait for boot to complete */
	mdelay(100);

	/* request kernel to locate firmware file: part2 */
	errno = request_firmware(&firmware, fw2, &dev->dev);
	if (errno < 0) {
		pr_err("%s: unable to locate firmware file: %s\n",
		       DRIVER_NAME, fw2);
		goto error;
	}

	/* initiate firmware upload */
	errno = as102_firmware_upload(bus_adap, cmd_buf, firmware);
	if (errno < 0) {
		pr_err("%s: error during firmware upload part2\n",
		       DRIVER_NAME);
		goto error;
	}

	pr_info("%s: firmware: %s loaded with success\n",
		DRIVER_NAME, fw2);
error:
	/* free data buffer */
	kfree(cmd_buf);
	/* release firmware if needed */
	if (firmware != NULL)
		release_firmware(firmware);

	LEAVE();
	return errno;
}
