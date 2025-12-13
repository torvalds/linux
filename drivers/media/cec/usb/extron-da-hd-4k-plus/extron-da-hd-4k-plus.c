// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021-2024 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

/*
 * Currently this driver does not fully support the serial port of the
 * Extron, only the USB port is fully supported.
 *
 * Issues specific to using the serial port instead of the USB since the
 * serial port doesn't detect if the device is powered off:
 *
 * - Some periodic ping mechanism is needed to detect when the Extron is
 *   powered off and when it is powered on again.
 * - What to do when it is powered off and the driver is modprobed? Keep
 *   trying to contact the Extron indefinitely?
 */

#include <linux/completion.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/time.h>

#include "extron-da-hd-4k-plus.h"

MODULE_AUTHOR("Hans Verkuil <hverkuil@kernel.org>");
MODULE_DESCRIPTION("Extron DA HD 4K PLUS HDMI CEC driver");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-1)");

static unsigned int vendor_id;
module_param(vendor_id, uint, 0444);
MODULE_PARM_DESC(vendor_id, "CEC Vendor ID");

static char manufacturer_name[4];
module_param_string(manufacturer_name, manufacturer_name,
		    sizeof(manufacturer_name), 0644);
MODULE_PARM_DESC(manufacturer_name,
		 "EDID Vendor String (3 uppercase characters)");

static bool hpd_never_low;
module_param(hpd_never_low, bool, 0644);
MODULE_PARM_DESC(hpd_never_low, "Input HPD will never go low (1), or go low if all output HPDs are low (0, default)");

#define EXTRON_TIMEOUT_SECS 6

static const u8 hdmi_edid[256] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
	0x31, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x20, 0x01, 0x03, 0x80, 0x60, 0x36, 0x78,
	0x0f, 0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26,
	0x0f, 0x50, 0x54, 0x20, 0x00, 0x00, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a,
	0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
	0x45, 0x00, 0xc0, 0x1c, 0x32, 0x00, 0x00, 0x1e,
	0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x55, 0x18,
	0x87, 0x11, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x68,
	0x64, 0x6d, 0x69, 0x2d, 0x31, 0x30, 0x38, 0x30,
	0x70, 0x36, 0x30, 0x0a, 0x00, 0x00, 0x00, 0xfe,
	0x00, 0x73, 0x65, 0x72, 0x69, 0x6f, 0x0a, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x95,

	0x02, 0x03, 0x1b, 0xf1, 0x42, 0x10, 0x01, 0x23,
	0x09, 0x07, 0x07, 0x83, 0x01, 0x00, 0x00, 0x68,
	0x03, 0x0c, 0x00, 0x10, 0x00, 0x00, 0x21, 0x01,
	0xe2, 0x00, 0xca, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x89,
};

static const u8 hdmi_edid_4k_300[256] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
	0x31, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x20, 0x01, 0x03, 0x80, 0x60, 0x36, 0x78,
	0x0f, 0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26,
	0x0f, 0x50, 0x54, 0x20, 0x00, 0x00, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a,
	0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
	0x45, 0x00, 0xc0, 0x1c, 0x32, 0x00, 0x00, 0x1e,
	0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x55, 0x18,
	0x87, 0x3c, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x68,
	0x64, 0x6d, 0x69, 0x2d, 0x34, 0x6b, 0x2d, 0x36,
	0x30, 0x30, 0x0a, 0x20, 0x00, 0x00, 0x00, 0xfe,
	0x00, 0x73, 0x65, 0x72, 0x69, 0x6f, 0x0a, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x87,

	0x02, 0x03, 0x1f, 0xf1, 0x43, 0x10, 0x5f, 0x01,
	0x23, 0x09, 0x07, 0x07, 0x83, 0x01, 0x00, 0x00,
	0x6b, 0x03, 0x0c, 0x00, 0x10, 0x00, 0x00, 0x3c,
	0x21, 0x00, 0x20, 0x01, 0xe2, 0x00, 0xca, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc6,
};

static const u8 hdmi_edid_4k_600[256] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
	0x31, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x20, 0x01, 0x03, 0x80, 0x60, 0x36, 0x78,
	0x0f, 0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26,
	0x0f, 0x50, 0x54, 0x20, 0x00, 0x00, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x08, 0xe8,
	0x00, 0x30, 0xf2, 0x70, 0x5a, 0x80, 0xb0, 0x58,
	0x8a, 0x00, 0xc0, 0x1c, 0x32, 0x00, 0x00, 0x1e,
	0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x55, 0x18,
	0x87, 0x3c, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x68,
	0x64, 0x6d, 0x69, 0x2d, 0x34, 0x6b, 0x2d, 0x36,
	0x30, 0x30, 0x0a, 0x20, 0x00, 0x00, 0x00, 0xfe,
	0x00, 0x73, 0x65, 0x72, 0x69, 0x6f, 0x0a, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x4c,

	0x02, 0x03, 0x28, 0xf1, 0x44, 0x61, 0x5f, 0x10,
	0x01, 0x23, 0x09, 0x07, 0x07, 0x83, 0x01, 0x00,
	0x00, 0x6b, 0x03, 0x0c, 0x00, 0x10, 0x00, 0x00,
	0x3c, 0x21, 0x00, 0x20, 0x01, 0x67, 0xd8, 0x5d,
	0xc4, 0x01, 0x78, 0x00, 0x00, 0xe2, 0x00, 0xca,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82,
};

static int extron_send_byte(struct serio *serio, char byte)
{
	int err, i;

	for (i = 0; i < 100; i++) {
		err = serio_write(serio, byte);
		if (!err)
			break;
		usleep_range(80, 120);
	}
	if (err)
		dev_warn(&serio->dev, "unable to write byte after 100 attempts\n");
	return err ? -EIO : 0;
}

static int extron_send_len(struct serio *serio, const char *command,
			   const unsigned char *bin, unsigned int len)
{
	int err = 0;

	for (; !err && *command; command++)
		err = extron_send_byte(serio, *command);
	if (!err)
		err = extron_send_byte(serio, '\r');
	if (bin)
		for (; !err && len; len--)
			err = extron_send_byte(serio, *bin++);
	return err;
}

static int extron_send_and_wait_len(struct extron *extron, struct extron_port *port,
				    const char *cmd, const unsigned char *bin,
				    unsigned int len, const char *response)
{
	int timeout = EXTRON_TIMEOUT_SECS * HZ;
	int err;

	if (debug) {
		if (response)
			dev_info(extron->dev, "transmit %s (response: %s)\n",
				 cmd, response);
		else
			dev_info(extron->dev, "transmit %s\n", cmd);
	}

	mutex_lock(&extron->serio_lock);
	if (port) {
		init_completion(&port->cmd_done);
		port->cmd_error = 0;
		port->response = response;
	} else {
		init_completion(&extron->cmd_done);
		extron->cmd_error = 0;
		extron->response = response;
	}
	err = extron_send_len(extron->serio, cmd, bin, len);

	if (!err && response &&
	    !wait_for_completion_timeout(port ? &port->cmd_done : &extron->cmd_done, timeout)) {
		dev_info(extron->dev, "transmit %s failed with %s (expected: %s)\n",
			 cmd, extron->reply, response);
		err = -ETIMEDOUT;
	}

	if (!err && response && (port ? port->cmd_error : extron->cmd_error)) {
		dev_info(extron->dev, "transmit %s failed with E%02u (expected: %s)\n",
			 cmd, port ? port->cmd_error : extron->cmd_error, response);
		if (port)
			port->cmd_error = 0;
		else
			extron->cmd_error = 0;
		err = -EPROTO;
	}
	if (port)
		port->response = NULL;
	else
		extron->response = NULL;
	mutex_unlock(&extron->serio_lock);
	return err;
}

static int extron_send_and_wait(struct extron *extron, struct extron_port *port,
				const char *cmd, const char *response)
{
	return extron_send_and_wait_len(extron, port, cmd, NULL, 0, response);
}

static void extron_parse_edid(struct extron_port *port)
{
	const u8 *edid = port->edid;
	unsigned int i, end;
	u8 d;

	port->has_4kp30 = false;
	port->has_4kp60 = false;
	port->has_qy = false;
	port->has_qs = false;
	/* Store Established Timings 1 and 2 */
	port->est_i = edid[0x23];
	port->est_ii = edid[0x24];

	// Check DTDs in base block
	for (i = 0; i < 4; i++) {
		const u8 *dtd = edid + 0x36 + i * 18;
		unsigned int w, h;
		unsigned int mhz;
		u64 pclk;

		if (!dtd[0] && !dtd[1])
			continue;
		w = dtd[2] + ((dtd[4] & 0xf0) << 4);
		h = dtd[5] + ((dtd[7] & 0xf0) << 4);
		if (w != 3840 || h != 2160)
			continue;

		w += dtd[3] + ((dtd[4] & 0x0f) << 8);
		h += dtd[6] + ((dtd[7] & 0x0f) << 8);
		pclk = dtd[0] + (dtd[1] << 8);
		pclk *= 100000;
		mhz = div_u64(pclk, w * h);
		if (mhz >= 297)
			port->has_4kp30 = true;
		if (mhz >= 594)
			port->has_4kp60 = true;
	}

	if (port->edid_blocks == 1)
		return;

	edid += 128;

	/* Return if not a CTA-861 extension block */
	if (edid[0] != 0x02 || edid[1] != 0x03)
		return;

	/* search Video Data Block (tag 2) */
	d = edid[2] & 0x7f;
	/* Check if there are Data Blocks */
	if (d <= 4)
		return;

	i = 4;
	end = d;

	do {
		u8 tag = edid[i] >> 5;
		u8 len = edid[i] & 0x1f;

		/* Avoid buffer overrun in case the EDID is malformed */
		if (i + len + 1 > 0x7f)
			return;

		switch (tag) {
		case 2: /* Video Data Block */
			/* Search for VIC 97 */
			if (memchr(edid + i + 1, 97, len))
				port->has_4kp60 = true;
			/* Search for VIC 95 */
			if (memchr(edid + i + 1, 95, len))
				port->has_4kp30 = true;
			break;

		case 7: /* Use Extended Tag */
			switch (edid[i + 1]) {
			case 0: /* Video Capability Data Block */
				if (edid[i + 2] & 0x80)
					port->has_qy = true;
				if (edid[i + 2] & 0x40)
					port->has_qs = true;
				break;
			}
			break;
		}
		i += len + 1;
	} while (i < end);
}

static int get_edid_tag_location(const u8 *edid, unsigned int size,
				 u8 want_tag, u8 ext_tag)
{
	unsigned int offset = 128;
	int i, end;
	u8 d;

	edid += offset;

	/* Return if not a CTA-861 extension block */
	if (size < 256 || edid[0] != 0x02 || edid[1] != 0x03)
		return -ENOENT;

	/* search tag */
	d = edid[0x02] & 0x7f;
	if (d <= 4)
		return -ENOENT;

	i = 0x04;
	end = 0x00 + d;

	do {
		unsigned char tag = edid[i] >> 5;
		unsigned char len = edid[i] & 0x1f;

		if (tag != want_tag || i + len > end) {
			i += len + 1;
			continue;
		}

		if (tag < 7 || (len >= 1 && edid[i + 1] == ext_tag))
			return offset + i;
		i += len + 1;
	} while (i < end);
	return -ENOENT;
}

static void extron_edid_crc(u8 *edid)
{
	u8 sum = 0;
	int offset;

	/* Update CRC */
	for (offset = 0; offset < 127; offset++)
		sum += edid[offset];
	edid[127] = 256 - sum;
}

/*
 * Fill in EDID string. As per VESA EDID-1.3, strings are at most 13 chars
 * long. If shorter then add a 0x0a character after the string and pad the
 * remainder with spaces.
 */
static void extron_set_edid_string(u8 *start, const char *s)
{
	const unsigned int max_len = 13;
	int len = strlen(s);

	memset(start, ' ', max_len);
	if (len > max_len)
		len = max_len;
	memcpy(start, s, len);
	if (len < max_len)
		start[len] = 0x0a;
}

static void extron_update_edid(struct extron_port *port, unsigned int blocks)
{
	int offset;
	u8 c1, c2;

	c1 = ((manufacturer_name[0] - '@') << 2) |
		(((manufacturer_name[1] - '@') >> 3) & 0x03);
	c2 = (((manufacturer_name[1] - '@') & 0x07) << 5) |
		((manufacturer_name[2] - '@') & 0x1f);

	port->edid_tmp[8] = c1;
	port->edid_tmp[9] = c2;

	/* Set Established Timings, but always enable VGA */
	port->edid_tmp[0x23] = port->est_i | 0x20;
	port->edid_tmp[0x24] = port->est_ii;

	/* Set the Monitor Name to the unit name */
	extron_set_edid_string(port->edid_tmp + 0x5f, port->extron->unit_name);
	/* Set the ASCII String to the CEC adapter name */
	extron_set_edid_string(port->edid_tmp + 0x71, port->adap->name);

	extron_edid_crc(port->edid_tmp);

	/* Find Video Capability Data Block */
	offset = get_edid_tag_location(port->edid_tmp, blocks * 128, 7, 0);
	if (offset > 0) {
		port->edid_tmp[offset + 2] &= ~0xc0;
		if (port->has_qy)
			port->edid_tmp[offset + 2] |= 0x80;
		if (port->has_qs)
			port->edid_tmp[offset + 2] |= 0x40;
	}

	extron_edid_crc(port->edid_tmp + 128);
}

static int extron_write_edid(struct extron_port *port,
			     const u8 *edid, unsigned int blocks)
{
	struct extron *extron = port->extron;
	u16 phys_addr = CEC_PHYS_ADDR_INVALID;
	int ret;

	if (cec_get_edid_spa_location(edid, blocks * 128))
		phys_addr = 0;

	if (mutex_lock_interruptible(&extron->edid_lock))
		return -EINTR;

	memcpy(port->edid_tmp, edid, blocks * 128);

	if (manufacturer_name[0])
		extron_update_edid(port, blocks);

	ret = extron_send_and_wait_len(port->extron, port, "W+UF256,in.bin",
				       port->edid_tmp, sizeof(port->edid_tmp),
				       "Upl");
	if (ret)
		goto unlock;
	ret = extron_send_and_wait(port->extron, port, "WI1,in.binEDID",
				   "EdidI01");
	if (ret)
		goto unlock;

	port->edid_blocks = blocks;
	memcpy(port->edid, port->edid_tmp, blocks * 128);
	port->read_edid = true;
	mutex_unlock(&extron->edid_lock);

	cec_s_phys_addr(port->adap, phys_addr, false);
	return 0;

unlock:
	mutex_unlock(&extron->edid_lock);
	return ret;
}

static void update_edid_work(struct work_struct *w)
{
	struct extron *extron = container_of(w, struct extron,
					     work_update_edid.work);
	struct extron_port *in = extron->ports[extron->num_out_ports];
	struct extron_port *p;
	bool has_edid = false;
	bool has_4kp30 = true;
	bool has_4kp60 = true;
	bool has_qy = true;
	bool has_qs = true;
	u8 est_i = 0xff;
	u8 est_ii = 0xff;
	unsigned int out;

	for (out = 0; has_4kp60 && out < extron->num_out_ports; out++) {
		p = extron->ports[out];
		if (p->read_edid) {
			has_4kp60 = p->has_4kp60;
			est_i &= p->est_i;
			est_ii &= p->est_ii;
			has_edid = true;
		}
	}
	for (out = 0; has_4kp30 && out < extron->num_out_ports; out++)
		if (extron->ports[out]->read_edid)
			has_4kp30 = extron->ports[out]->has_4kp30;

	for (out = 0; has_qy && out < extron->num_out_ports; out++)
		if (extron->ports[out]->read_edid)
			has_qy = extron->ports[out]->has_qy;

	for (out = 0; has_qs && out < extron->num_out_ports; out++)
		if (extron->ports[out]->read_edid)
			has_qs = extron->ports[out]->has_qs;

	/* exit if no output port had an EDID */
	if (!has_edid)
		return;

	/* exit if the input EDID properties remained unchanged */
	if (has_4kp60 == in->has_4kp60 && has_4kp30 == in->has_4kp30 &&
	    has_qy == in->has_qy && has_qs == in->has_qs &&
	    est_i == in->est_i && est_ii == in->est_ii)
		return;

	in->has_4kp60 = has_4kp60;
	in->has_4kp30 = has_4kp30;
	in->has_qy = has_qy;
	in->has_qs = has_qs;
	in->est_i = est_i;
	in->est_ii = est_ii;
	extron_write_edid(extron->ports[extron->num_out_ports],
			  has_4kp60 ? hdmi_edid_4k_600 :
			  (has_4kp30 ? hdmi_edid_4k_300 : hdmi_edid), 2);
}

static void extron_read_edid(struct extron_port *port)
{
	struct extron *extron = port->extron;
	char cmd[10], reply[10];
	unsigned int idx;

	idx = port->port.port + (port->is_input ? 0 : extron->num_in_ports);
	snprintf(cmd, sizeof(cmd), "WR%uEDID", idx);
	snprintf(reply, sizeof(reply), "EdidR%u", idx);
	if (mutex_lock_interruptible(&extron->edid_lock))
		return;
	if (port->read_edid)
		goto unlock;
	extron->edid_bytes_read = 0;
	extron->edid_port = port;
	port->edid_blocks = 0;
	if (!port->has_edid)
		goto no_edid;

	extron->edid_reading = true;

	if (!extron_send_and_wait(extron, port, cmd, reply))
		wait_for_completion_killable_timeout(&extron->edid_completion,
						     msecs_to_jiffies(1000));
	if (port->edid_blocks) {
		extron_parse_edid(port);
		port->read_edid = true;
		if (!port->is_input)
			v4l2_ctrl_s_ctrl(port->ctrl_tx_edid_present, 1);
	}
no_edid:
	extron->edid_reading = false;
unlock:
	mutex_unlock(&extron->edid_lock);
	cancel_delayed_work_sync(&extron->work_update_edid);
	if (manufacturer_name[0])
		schedule_delayed_work(&extron->work_update_edid,
				      msecs_to_jiffies(1000));
}

static void extron_irq_work_handler(struct work_struct *work)
{
	struct extron_port *port =
		container_of(work, struct extron_port, irq_work);
	struct extron *extron = port->extron;
	unsigned long flags;
	bool update_pa;
	u16 pa;
	bool update_has_signal;
	bool has_signal;
	bool update_has_edid;
	bool has_edid;
	u32 status;

	spin_lock_irqsave(&port->msg_lock, flags);
	while (port->rx_msg_num) {
		spin_unlock_irqrestore(&port->msg_lock, flags);
		cec_received_msg(port->adap,
				 &port->rx_msg[port->rx_msg_cur_idx]);
		spin_lock_irqsave(&port->msg_lock, flags);
		if (port->rx_msg_num)
			port->rx_msg_num--;
		port->rx_msg_cur_idx =
			(port->rx_msg_cur_idx + 1) % NUM_MSGS;
	}
	update_pa = port->update_phys_addr;
	pa = port->phys_addr;
	port->update_phys_addr = false;
	update_has_signal = port->update_has_signal;
	has_signal = port->has_signal;
	port->update_has_signal = false;
	update_has_edid = port->update_has_edid;
	has_edid = port->has_edid;
	port->update_has_edid = false;
	status = port->tx_done_status;
	port->tx_done_status = 0;
	spin_unlock_irqrestore(&port->msg_lock, flags);

	if (status)
		cec_transmit_done(port->adap, status, 0, 0, 0, 0);

	if (update_has_signal && port->is_input)
		v4l2_ctrl_s_ctrl(port->ctrl_rx_power_present, has_signal);

	if (update_has_edid && !port->is_input) {
		v4l2_ctrl_s_ctrl(port->ctrl_tx_hotplug,
				 port->has_edid);
		if (port->has_edid) {
			port->port.found_sink = true;
			port->port.lost_sink_ts = ktime_set(0, 0);
		} else {
			port->port.lost_sink_ts = ktime_get();
		}
		if (!has_edid) {
			port->edid_blocks = 0;
			port->read_edid = false;
			if (extron->edid_reading && !has_edid &&
			    extron->edid_port == port)
				extron->edid_reading = false;
			v4l2_ctrl_s_ctrl(port->ctrl_tx_edid_present, 0);
		} else if (!extron->edid_reading || extron->edid_port != port) {
			extron_read_edid(port);
		}
	}
	if (update_pa)
		cec_s_phys_addr(port->adap, pa, false);
}

static void extron_process_received(struct extron_port *port, const char *data)
{
	struct cec_msg msg = {};
	unsigned int len = strlen(data);
	unsigned long irq_flags;
	unsigned int idx;

	if (!port || port->disconnected)
		return;

	if (len < 5 || (len - 2) % 3 || data[len - 2] != '*')
		goto malformed;

	while (*data != '*') {
		int v = hex2bin(&msg.msg[msg.len], data + 1, 1);

		if (*data != '%' || v)
			goto malformed;
		msg.len++;
		data += 3;
	}

	spin_lock_irqsave(&port->msg_lock, irq_flags);
	idx = (port->rx_msg_cur_idx + port->rx_msg_num) %
		NUM_MSGS;
	if (port->rx_msg_num == NUM_MSGS) {
		dev_warn(port->dev,
			 "message queue is full, dropping %*ph\n",
			 msg.len, msg.msg);
		spin_unlock_irqrestore(&port->msg_lock,
				       irq_flags);
		return;
	}
	port->rx_msg_num++;
	port->rx_msg[idx] = msg;
	spin_unlock_irqrestore(&port->msg_lock, irq_flags);
	if (!port->disconnected)
		schedule_work(&port->irq_work);
	return;

malformed:
	dev_info(port->extron->dev, "malformed msg received: '%s'\n", data);
}

static void extron_port_signal_change(struct extron_port *port, bool has_sig)
{
	unsigned long irq_flags;
	bool update = false;

	if (!port)
		return;

	spin_lock_irqsave(&port->msg_lock, irq_flags);
	if (!port->update_has_signal && port->has_signal != has_sig) {
		port->update_has_signal = true;
		update = true;
	}
	port->has_signal = has_sig;
	spin_unlock_irqrestore(&port->msg_lock, irq_flags);
	if (update && !port->disconnected)
		schedule_work(&port->irq_work);
}

static void extron_process_signal_change(struct extron *extron, const char *data)
{
	unsigned int i;

	extron_port_signal_change(extron->ports[extron->num_out_ports],
				  data[0] == '1');
	for (i = 0; i < extron->num_out_ports; i++)
		extron_port_signal_change(extron->ports[i],
					  data[2 + 2 * i] != '0');
}

static void extron_port_edid_change(struct extron_port *port, bool has_edid)
{
	unsigned long irq_flags;
	bool update = false;

	if (!port)
		return;

	spin_lock_irqsave(&port->msg_lock, irq_flags);
	if (!port->update_has_edid && port->has_edid != has_edid) {
		port->update_has_edid = true;
		update = true;
	}
	port->has_edid = has_edid;
	spin_unlock_irqrestore(&port->msg_lock, irq_flags);
	if (update && !port->disconnected)
		schedule_work(&port->irq_work);
}

static void extron_process_edid_change(struct extron *extron, const char *data)
{
	unsigned int i;

	/*
	 * Do nothing if the Extron isn't ready yet. Trying to do this
	 * while the Extron firmware is still settling will fail.
	 */
	if (!extron->is_ready)
		return;

	for (i = 0; i < extron->num_out_ports; i++)
		extron_port_edid_change(extron->ports[i],
					data[2 + 2 * i] != '0');
}

static void extron_phys_addr_change(struct extron_port *port, u16 pa)
{
	unsigned long irq_flags;
	bool update = false;

	if (!port)
		return;

	spin_lock_irqsave(&port->msg_lock, irq_flags);
	if (!port->update_phys_addr && port->phys_addr != pa) {
		update = true;
		port->update_phys_addr = true;
	}
	port->phys_addr = pa;
	spin_unlock_irqrestore(&port->msg_lock, irq_flags);
	if (update && !port->disconnected)
		schedule_work(&port->irq_work);
}

static void extron_process_tx_done(struct extron_port *port, char status)
{
	unsigned long irq_flags;
	unsigned int tx_status;

	if (!port)
		return;

	switch (status) {
	case '0':
		tx_status = CEC_TX_STATUS_NACK | CEC_TX_STATUS_MAX_RETRIES;
		break;
	case '1':
		tx_status = CEC_TX_STATUS_OK;
		break;
	default:
		tx_status = CEC_TX_STATUS_ERROR;
		break;
	}
	spin_lock_irqsave(&port->msg_lock, irq_flags);
	port->tx_done_status = tx_status;
	spin_unlock_irqrestore(&port->msg_lock, irq_flags);
	if (!port->disconnected)
		schedule_work(&port->irq_work);
}

static void extron_add_edid(struct extron_port *port, const char *hex)
{
	struct extron *extron = port ? port->extron : NULL;

	if (!port || port != extron->edid_port)
		return;
	while (extron->edid_bytes_read < sizeof(port->edid) && *hex) {
		int err = hex2bin(&port->edid[extron->edid_bytes_read], hex, 1);

		if (err) {
			extron->edid_reading = false;
			complete(&extron->edid_completion);
			break;
		}
		extron->edid_bytes_read++;
		hex += 2;
	}
	if (extron->edid_bytes_read == 128 &&
	    port->edid[126] == 0) {
		/* There are no extension blocks, we're done */
		port->edid_blocks = 1;
		extron->edid_reading = false;
		complete(&extron->edid_completion);
	}
	if (extron->edid_bytes_read < sizeof(port->edid))
		return;
	if (!*hex)
		port->edid_blocks = 2;
	extron->edid_reading = false;
	complete(&extron->edid_completion);
}

static irqreturn_t extron_interrupt(struct serio *serio, unsigned char data,
				    unsigned int flags)
{
	struct extron *extron = serio_get_drvdata(serio);
	struct extron_port *port = NULL;
	bool found_response;
	unsigned int p;

	if (data == '\r' || data == '\n') {
		if (extron->idx == 0)
			return IRQ_HANDLED;
		memcpy(extron->data, extron->buf, extron->idx);
		extron->len = extron->idx;
		extron->data[extron->len] = 0;
		if (debug)
			dev_info(extron->dev, "received %s\n", extron->data);
		extron->idx = 0;
		if (!memcmp(extron->data, "Sig", 3) &&
		    extron->data[4] == '*') {
			extron_process_signal_change(extron, extron->data + 3);
		} else if (!memcmp(extron->data, "Hdcp", 4) &&
			   extron->data[5] == '*') {
			extron_process_edid_change(extron, extron->data + 4);
		} else if (!memcmp(extron->data, "DcecI", 5) &&
			   extron->data[5] >= '1' &&
			   extron->data[5] < '1' + extron->num_in_ports) {
			unsigned int p = extron->data[5] - '1';

			p += extron->num_out_ports;
			extron_process_tx_done(extron->ports[p],
					       extron->data[extron->len - 1]);
		} else if (!memcmp(extron->data, "Ceci", 4) &&
			   extron->data[4] >= '1' &&
			   extron->data[4] < '1' + extron->num_in_ports &&
			   extron->data[5] == '*') {
			unsigned int p = extron->data[4] - '1';

			p += extron->num_out_ports;
			extron_process_received(extron->ports[p],
						extron->data + 6);
		} else if (!memcmp(extron->data, "DcecO", 5) &&
			   extron->data[5] >= '1' &&
			   extron->data[5] < '1' + extron->num_out_ports) {
			unsigned int p = extron->data[5] - '1';

			extron_process_tx_done(extron->ports[p],
					       extron->data[extron->len - 1]);
		} else if (!memcmp(extron->data, "Ceco", 4) &&
			   extron->data[4] >= '1' &&
			   extron->data[4] < '1' + extron->num_out_ports &&
			   extron->data[5] == '*') {
			unsigned int p = extron->data[4] - '1';

			extron_process_received(extron->ports[p],
						extron->data + 6);
		} else if (!memcmp(extron->data, "Pceco", 5) &&
			   extron->data[5] >= '1' &&
			   extron->data[5] < '1' + extron->num_out_ports) {
			unsigned int p = extron->data[5] - '1';
			unsigned int tmp_pa[2] = { 0xff, 0xff };

			if (sscanf(extron->data + 7, "%%%02x%%%02x",
				   &tmp_pa[0], &tmp_pa[1]) == 2)
				extron_phys_addr_change(extron->ports[p],
							tmp_pa[0] << 8 | tmp_pa[1]);
		} else if (!memcmp(extron->data, "Pceci", 5) &&
			   extron->data[5] >= '1' &&
			   extron->data[5] < '1' + extron->num_in_ports) {
			unsigned int p = extron->data[5] - '1';
			unsigned int tmp_pa[2] = { 0xff, 0xff };

			p += extron->num_out_ports;
			if (sscanf(extron->data + 7, "%%%02x%%%02x",
				   &tmp_pa[0], &tmp_pa[1]) == 2)
				extron_phys_addr_change(extron->ports[p],
							tmp_pa[0] << 8 | tmp_pa[1]);
		} else if (!memcmp(extron->data, "EdidR", 5) &&
			   extron->data[5] >= '1' &&
			   extron->data[5] < '1' + extron->num_ports &&
			   extron->data[6] == '*') {
			unsigned int p = extron->data[5] - '1';

			if (p)
				p--;
			else
				p = extron->num_out_ports;
			extron_add_edid(extron->ports[p], extron->data + 7);
		} else if (extron->edid_reading && extron->len == 32 &&
			   extron->edid_port) {
			extron_add_edid(extron->edid_port, extron->data);
		}

		found_response = false;
		if (extron->response &&
		    !strncmp(extron->response, extron->data,
			     strlen(extron->response)))
			found_response = true;

		for (p = 0; !found_response && p < extron->num_ports; p++) {
			port = extron->ports[p];
			if (port && port->response &&
			    !strncmp(port->response, extron->data,
				     strlen(port->response)))
				found_response = true;
		}

		if (!found_response && extron->response &&
		    extron->data[0] == 'E' &&
		    isdigit(extron->data[1]) &&
		    isdigit(extron->data[2]) &&
		    !extron->data[3]) {
			extron->cmd_error = (extron->data[1] - '0') * 10 +
				extron->data[2] - '0';
			extron->response = NULL;
			complete(&extron->cmd_done);
		}

		if (!found_response)
			return IRQ_HANDLED;

		memcpy(extron->reply, extron->data, extron->len);
		extron->reply[extron->len] = 0;
		if (!port) {
			extron->response = NULL;
			complete(&extron->cmd_done);
		} else {
			port->response = NULL;
			complete(&port->cmd_done);
		}
		return IRQ_HANDLED;
	}

	if (extron->idx >= DATA_SIZE - 1) {
		dev_info(extron->dev,
			 "throwing away %d bytes of garbage\n", extron->idx);
		extron->idx = 0;
	}
	extron->buf[extron->idx++] = (char)data;
	return IRQ_HANDLED;
}

static int extron_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct extron_port *port = cec_get_drvdata(adap);

	return (port->disconnected && enable) ? -ENODEV : 0;
}

static int extron_cec_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	struct extron_port *port = cec_get_drvdata(adap);
	char cmd[26];
	char resp[25];
	u8 la = log_addr == CEC_LOG_ADDR_INVALID ? 15 : log_addr;
	int err;

	if (port->disconnected)
		return -ENODEV;
	snprintf(cmd, sizeof(cmd), "W%c%u*%uLCEC",
		 port->direction, port->port.port, la);
	snprintf(resp, sizeof(resp), "Lcec%c%u*%u",
		 port->direction, port->port.port, la);
	err = extron_send_and_wait(port->extron, port, cmd, resp);
	return log_addr != CEC_LOG_ADDR_INVALID && err ? err : 0;
}

static int extron_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				    u32 signal_free_time, struct cec_msg *msg)
{
	struct extron_port *port = cec_get_drvdata(adap);
	char buf[(CEC_MAX_MSG_SIZE - 1) * 3 + 1];
	char cmd[sizeof(buf) + 14];
	unsigned int i;

	if (port->disconnected)
		return -ENODEV;
	buf[0] = 0;
	for (i = 0; i < msg->len - 1; i++)
		sprintf(buf + i * 3, "%%%02X", msg->msg[i + 1]);
	snprintf(cmd, sizeof(cmd), "W%c%u*%u*%u*%sDCEC",
		 port->direction, port->port.port,
		 cec_msg_initiator(msg), cec_msg_destination(msg), buf);
	return extron_send_and_wait(port->extron, port, cmd, NULL);
}

static void extron_cec_adap_unconfigured(struct cec_adapter *adap)
{
	struct extron_port *port = cec_get_drvdata(adap);

	if (port->disconnected)
		return;
	if (debug)
		dev_info(port->extron->dev, "unconfigured port %d (%s)\n",
			 port->port.port,
			 port->extron->splitter.is_standby ? "Off" : "On");
	if (!port->is_input)
		cec_splitter_unconfigured_output(&port->port);
}

static void extron_cec_configured(struct cec_adapter *adap)
{
	struct extron_port *port = cec_get_drvdata(adap);

	if (port->disconnected)
		return;
	if (debug)
		dev_info(port->extron->dev, "configured port %d (%s)\n",
			 port->port.port,
			 port->extron->splitter.is_standby ? "Off" : "On");
	if (!port->is_input)
		cec_splitter_configured_output(&port->port);
}

static void extron_cec_adap_nb_transmit_canceled(struct cec_adapter *adap,
						 const struct cec_msg *msg)
{
	struct extron_port *port = cec_get_drvdata(adap);
	struct cec_adapter *input_adap;

	if (!vendor_id)
		return;
	if (port->disconnected || port->is_input)
		return;
	input_adap = port->extron->ports[port->extron->num_out_ports]->adap;
	cec_splitter_nb_transmit_canceled_output(&port->port, msg, input_adap);
}

static int extron_received(struct cec_adapter *adap, struct cec_msg *msg)
{
	struct extron_port *port = cec_get_drvdata(adap);

	if (!vendor_id)
		return -ENOMSG;
	if (port->disconnected)
		return -ENOMSG;
	if (port->is_input)
		return cec_splitter_received_input(&port->port, msg);
	return cec_splitter_received_output(&port->port, msg,
					    port->extron->ports[port->extron->num_out_ports]->adap);
}

#define log_printf(adap, file, fmt, arg...) \
	do { \
		if (file) \
			seq_printf((file), fmt, ## arg); \
		else \
			pr_info("cec-%s: " fmt, (adap)->name, ## arg);	\
	} while (0)

static const char * const pwr_state[] = {
	"on",
	"standby",
	"to on",
	"to standby",
};

static void extron_adap_status_port(struct extron_port *port, struct seq_file *file)
{
	struct cec_adapter *adap = port->adap;

	if (port->disconnected) {
		log_printf(adap, file,
			   "\tport %u: disconnected\n", port->port.port);
		return;
	}
	if (port->is_input)
		log_printf(adap, file,
			   "\tport %u: %s signal, %s edid, %s 4kp30, %s 4kp60, %sQS/%sQY, is %s\n",
			   port->port.port,
			   port->has_signal ? "has" : "no",
			   port->has_edid ? "has" : "no",
			   port->has_4kp30 ? "has" : "no",
			   port->has_4kp60 ? "has" : "no",
			   port->has_qs ? "" : "no ",
			   port->has_qy ? "" : "no ",
			   !port->port.adap->is_configured ? "not configured" :
				pwr_state[port->extron->splitter.is_standby]);
	else
		log_printf(adap, file,
			   "\tport %u: %s sink, %s signal, %s edid, %s 4kp30, %s 4kp60, %sQS/%sQY, is %sactive source, is %s\n",
			   port->port.port,
			   port->port.found_sink ? "found" : "no",
			   port->has_signal ? "has" : "no",
			   port->has_edid ? "has" : "no",
			   port->has_4kp30 ? "has" : "no",
			   port->has_4kp60 ? "has" : "no",
			   port->has_qs ? "" : "no ",
			   port->has_qy ? "" : "no ",
			   port->port.is_active_source ? "" : "not ",
			   !port->port.adap->is_configured ? "not configured" :
			   pwr_state[port->port.power_status & 3]);
	if (port->port.out_give_device_power_status_seq)
		log_printf(adap, file,
			   "\tport %u: querying power status (%u, %lldms)\n",
			   port->port.port,
			   port->port.out_give_device_power_status_seq & ~(1 << 31),
			   ktime_ms_delta(ktime_get(),
					  port->port.out_give_device_power_status_ts));
	if (port->port.out_request_current_latency_seq)
		log_printf(adap, file,
			   "\tport %u: querying latency (%u, %lldms)\n",
			   port->port.port,
			   port->port.out_request_current_latency_seq & ~(1 << 31),
			   ktime_ms_delta(ktime_get(),
					  port->port.out_request_current_latency_ts));
}

static void extron_adap_status(struct cec_adapter *adap, struct seq_file *file)
{
	struct extron_port *port = cec_get_drvdata(adap);
	struct extron *extron = port->extron;
	unsigned int i;

	log_printf(adap, file, "name: %s type: %s\n",
		   extron->unit_name, extron->unit_type);
	log_printf(adap, file, "model: 60-160%c-01 (1 input, %u outputs)\n",
		   '6' + extron->num_out_ports / 2, extron->num_out_ports);
	log_printf(adap, file, "firmware version: %s CEC engine version: %s\n",
		   extron->unit_fw_version, extron->unit_cec_engine_version);
	if (extron->hpd_never_low)
		log_printf(adap, file, "always keep input HPD high\n");
	else
		log_printf(adap, file,
			   "pull input HPD low if all output HPDs are low\n");
	if (vendor_id)
		log_printf(adap, file,
			   "splitter vendor ID: 0x%06x\n", vendor_id);
	if (manufacturer_name[0])
		log_printf(adap, file, "splitter manufacturer name: %s\n",
			   manufacturer_name);
	log_printf(adap, file, "splitter power status: %s\n",
		   pwr_state[extron->splitter.is_standby]);
	log_printf(adap, file, "%s port: %d (%s)\n",
		   port->is_input ? "input" : "output",
		   port->port.port, port->name);
	log_printf(adap, file, "splitter input port:\n");
	extron_adap_status_port(extron->ports[extron->num_out_ports], file);

	log_printf(adap, file, "splitter output ports:\n");
	for (i = 0; i < extron->num_out_ports; i++)
		extron_adap_status_port(extron->ports[i], file);

	if (!port->has_edid || !port->read_edid)
		return;

	for (i = 0; i < port->edid_blocks * 128; i += 16) {
		if (i % 128 == 0)
			log_printf(adap, file, "\n");
		log_printf(adap, file, "EDID: %*ph\n", 16, port->edid + i);
	}
}

static const struct cec_adap_ops extron_cec_adap_ops = {
	.adap_enable = extron_cec_adap_enable,
	.adap_log_addr = extron_cec_adap_log_addr,
	.adap_transmit = extron_cec_adap_transmit,
	.adap_nb_transmit_canceled = extron_cec_adap_nb_transmit_canceled,
	.adap_unconfigured = extron_cec_adap_unconfigured,
	.adap_status = extron_adap_status,
	.configured = extron_cec_configured,
	.received = extron_received,
};

static int extron_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct extron_port *port = video_drvdata(file);

	strscpy(cap->driver, "extron-da-hd-4k-plus-cec", sizeof(cap->driver));
	strscpy(cap->card, cap->driver, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "serio:%s", port->name);
	return 0;
}

static int extron_enum_input(struct file *file, void *priv, struct v4l2_input *inp)
{
	struct extron_port *port = video_drvdata(file);

	if (inp->index)
		return -EINVAL;
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	snprintf(inp->name, sizeof(inp->name), "HDMI IN %u", port->port.port);
	inp->status = v4l2_ctrl_g_ctrl(port->ctrl_rx_power_present) ?
		0 : V4L2_IN_ST_NO_SIGNAL;
	return 0;
}

static int extron_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int extron_s_input(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int extron_enum_output(struct file *file, void *priv, struct v4l2_output *out)
{
	struct extron_port *port = video_drvdata(file);

	if (out->index)
		return -EINVAL;
	out->type = V4L2_OUTPUT_TYPE_ANALOG;
	snprintf(out->name, sizeof(out->name), "HDMI OUT %u", port->port.port);
	return 0;
}

static int extron_g_output(struct file *file, void *priv, unsigned int *o)
{
	*o = 0;
	return 0;
}

static int extron_s_output(struct file *file, void *priv, unsigned int o)
{
	return o ? -EINVAL : 0;
}

static int extron_g_edid(struct file *file, void *priv,
			 struct v4l2_edid *edid)
{
	struct extron_port *port = video_drvdata(file);

	memset(edid->reserved, 0, sizeof(edid->reserved));
	if (port->disconnected)
		return -ENODEV;
	if (edid->pad)
		return -EINVAL;
	if (!port->has_edid)
		return -ENODATA;
	if (!port->read_edid)
		extron_read_edid(port);
	if (!port->read_edid)
		return -ENODATA;
	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = port->edid_blocks;
		return 0;
	}
	if (edid->start_block >= port->edid_blocks)
		return -EINVAL;
	if (edid->blocks > port->edid_blocks - edid->start_block)
		edid->blocks = port->edid_blocks - edid->start_block;
	memcpy(edid->edid, port->edid + edid->start_block * 128, edid->blocks * 128);
	return 0;
}

static int extron_s_edid(struct file *file, void *priv, struct v4l2_edid *edid)
{
	struct extron_port *port = video_drvdata(file);

	memset(edid->reserved, 0, sizeof(edid->reserved));
	if (port->disconnected)
		return -ENODEV;
	if (edid->pad)
		return -EINVAL;

	/* Unfortunately it is not possible to clear the EDID */
	if (edid->blocks == 0)
		return -EINVAL;

	if (edid->blocks > MAX_EDID_BLOCKS) {
		edid->blocks = MAX_EDID_BLOCKS;
		return -E2BIG;
	}

	if (cec_get_edid_spa_location(edid->edid, edid->blocks * 128))
		v4l2_set_edid_phys_addr(edid->edid, edid->blocks * 128, 0);
	extron_parse_edid(port);
	return extron_write_edid(port, edid->edid, edid->blocks);
}

static int extron_log_status(struct file *file, void *priv)
{
	struct extron_port *port = video_drvdata(file);

	extron_adap_status(port->adap, NULL);
	return v4l2_ctrl_log_status(file, priv);
}

static const struct v4l2_ioctl_ops extron_ioctl_ops = {
	.vidioc_querycap		= extron_querycap,
	.vidioc_enum_input		= extron_enum_input,
	.vidioc_g_input			= extron_g_input,
	.vidioc_s_input			= extron_s_input,
	.vidioc_enum_output		= extron_enum_output,
	.vidioc_g_output		= extron_g_output,
	.vidioc_s_output		= extron_s_output,
	.vidioc_g_edid			= extron_g_edid,
	.vidioc_s_edid			= extron_s_edid,
	.vidioc_log_status		= extron_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations extron_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= v4l2_fh_release,
	.poll		= v4l2_ctrl_poll,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct video_device extron_videodev = {
	.name		= "extron-da-hd-4k-plus-cec",
	.vfl_dir	= VFL_DIR_RX,
	.fops		= &extron_fops,
	.ioctl_ops	= &extron_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
};

static void extron_disconnect(struct serio *serio)
{
	struct extron *extron = serio_get_drvdata(serio);
	unsigned int p;

	kthread_stop(extron->kthread_setup);

	for (p = 0; p < extron->num_ports; p++) {
		struct extron_port *port = extron->ports[p];

		if (!port)
			continue;
		port->disconnected = true;
		cancel_work_sync(&port->irq_work);
	}
	cancel_delayed_work_sync(&extron->work_update_edid);
	for (p = 0; p < extron->num_ports; p++) {
		struct extron_port *port = extron->ports[p];

		if (!port)
			continue;

		if (port->cec_was_registered) {
			if (cec_is_registered(port->adap))
				cec_unregister_adapter(port->adap);
			/*
			 * After registering the adapter, the
			 * extron_setup_thread() function took an extra
			 * reference to the device. We call the corresponding
			 * put here.
			 */
			cec_put_device(port->adap);
		} else {
			cec_delete_adapter(port->adap);
		}
		video_unregister_device(&port->vdev);
	}

	complete(&extron->edid_completion);

	for (p = 0; p < extron->num_ports; p++) {
		struct extron_port *port = extron->ports[p];

		if (!port)
			continue;
		v4l2_ctrl_handler_free(&port->hdl);
		mutex_destroy(&port->video_lock);
		kfree(port);
	}
	mutex_destroy(&extron->edid_lock);
	mutex_destroy(&extron->serio_lock);
	extron->serio = NULL;
	serio_set_drvdata(serio, NULL);
	serio_close(serio);
}

static int extron_setup(struct extron *extron)
{
	struct serio *serio = extron->serio;
	struct extron_port *port;
	u8 *reply = extron->reply;
	unsigned int p;
	unsigned int major, minor;
	int err;

	/*
	 * Attempt to disable CEC: avoid received CEC messages
	 * from interfering with the other serial port traffic.
	 */
	extron_send_and_wait(extron, NULL, "WI1*0CCEC", NULL);
	extron_send_and_wait(extron, NULL, "WO0*CCEC", NULL);

	/* Obtain unit part number */
	err = extron_send_and_wait(extron, NULL, "N", "Pno");
	if (err)
		return err;
	dev_info(extron->dev, "Unit part number: %s\n", reply + 3);
	if (strcmp(reply + 3, "60-1607-01") &&
	    strcmp(reply + 3, "60-1608-01") &&
	    strcmp(reply + 3, "60-1609-01")) {
		dev_err(extron->dev, "Unsupported model\n");
		return -ENODEV;
	}
	/* Up to 6 output ports and one input port */
	extron->num_out_ports = 2 * (reply[9] - '6');
	extron->splitter.num_out_ports = extron->num_out_ports;
	extron->splitter.ports = extron->splitter_ports;
	extron->splitter.dev = extron->dev;
	extron->num_in_ports = 1;
	extron->num_ports = extron->num_out_ports + extron->num_in_ports;
	dev_info(extron->dev, "Unit output ports: %d\n", extron->num_out_ports);
	dev_info(extron->dev, "Unit input ports: %d\n", extron->num_in_ports);

	err = extron_send_and_wait(extron, NULL, "W CN", "Ipn ");
	if (err)
		return err;
	dev_info(extron->dev, "Unit name: %s\n", reply + 4);
	strscpy(extron->unit_name, reply + 4, sizeof(extron->unit_name));

	err = extron_send_and_wait(extron, NULL, "*Q", "Bld");
	if (err)
		return err;
	dev_info(extron->dev, "Unit FW Version: %s\n", reply + 3);
	strscpy(extron->unit_fw_version, reply + 3,
		sizeof(extron->unit_fw_version));
	if (sscanf(reply + 3, "%u.%u.", &major, &minor) < 2 ||
	    major < 1 || minor < 2) {
		dev_err(extron->dev,
			"Unsupported FW version (only 1.02 or up is supported)\n");
		return -ENODEV;
	}

	err = extron_send_and_wait(extron, NULL, "2i", "Inf02*");
	if (err)
		return err;
	dev_info(extron->dev, "Unit Type: %s\n", reply + 6);
	strscpy(extron->unit_type, reply + 6, sizeof(extron->unit_type));

	err = extron_send_and_wait(extron, NULL, "39Q", "Ver39*");
	if (err)
		return err;
	dev_info(extron->dev, "CEC Engine Version: %s\n", reply + 6);
	strscpy(extron->unit_cec_engine_version, reply + 6,
		sizeof(extron->unit_cec_engine_version));

	/* Disable CEC */
	err = extron_send_and_wait(extron, NULL, "WI1*0CCEC", "CcecI1*");
	if (err)
		return err;
	err = extron_send_and_wait(extron, NULL, "WO0*CCEC", "CcecO0");
	if (err)
		return err;

	extron->hpd_never_low = hpd_never_low;

	/* Pull input port HPD low if all output ports also have a low HPD */
	if (hpd_never_low) {
		dev_info(extron->dev, "Always keep input HPD high\n");
	} else {
		dev_info(extron->dev, "Pull input HPD low if all output HPDs are low\n");
		extron_send_and_wait(extron, NULL, "W1ihpd", "Ihpd1");
	}

	for (p = 0; p < extron->num_ports; p++) {
		u32 caps = CEC_CAP_DEFAULTS | CEC_CAP_MONITOR_ALL;

		if (vendor_id)
			caps &= ~CEC_CAP_LOG_ADDRS;
		port = kzalloc(sizeof(*port), GFP_KERNEL);
		if (!port)
			return -ENOMEM;

		INIT_WORK(&port->irq_work, extron_irq_work_handler);
		spin_lock_init(&port->msg_lock);
		mutex_init(&port->video_lock);
		port->extron = extron;
		port->is_input = p >= extron->num_out_ports;
		port->direction = port->is_input ? 'I' : 'O';
		port->port.port = 1 + (port->is_input ? p - extron->num_out_ports : p);
		port->port.splitter = &extron->splitter;
		port->phys_addr = CEC_PHYS_ADDR_INVALID;
		snprintf(port->name, sizeof(port->name), "%s-%s-%u",
			 dev_name(&serio->dev), port->is_input ? "in" : "out",
			 port->port.port);

		port->dev = extron->dev;
		port->adap = cec_allocate_adapter(&extron_cec_adap_ops, port,
						  port->name, caps, 1);
		err = PTR_ERR_OR_ZERO(port->adap);
		if (err < 0) {
			kfree(port);
			return err;
		}

		port->adap->xfer_timeout_ms = EXTRON_TIMEOUT_SECS * 1000;
		port->port.adap = port->adap;
		port->vdev = extron_videodev;
		port->vdev.lock = &port->video_lock;
		port->vdev.v4l2_dev = &extron->v4l2_dev;
		port->vdev.ctrl_handler = &port->hdl;
		port->vdev.device_caps = V4L2_CAP_EDID;
		video_set_drvdata(&port->vdev, port);

		v4l2_ctrl_handler_init(&port->hdl, 2);

		if (port->is_input) {
			port->vdev.vfl_dir = VFL_DIR_RX;
			port->ctrl_rx_power_present =
				v4l2_ctrl_new_std(&port->hdl, NULL,
						  V4L2_CID_DV_RX_POWER_PRESENT,
						  0, 1, 0, 0);
			port->has_edid = true;
		} else {
			port->vdev.vfl_dir = VFL_DIR_TX;
			port->ctrl_tx_hotplug =
				v4l2_ctrl_new_std(&port->hdl, NULL,
						  V4L2_CID_DV_TX_HOTPLUG,
						  0, 1, 0, 0);
			port->ctrl_tx_edid_present =
				v4l2_ctrl_new_std(&port->hdl, NULL,
						  V4L2_CID_DV_TX_EDID_PRESENT,
						  0, 1, 0, 0);
		}

		err = port->hdl.error;
		if (err < 0) {
			cec_delete_adapter(port->adap);
			kfree(port);
			return err;
		}
		extron->ports[p] = port;
		extron->splitter_ports[p] = &port->port;
		if (port->is_input && manufacturer_name[0])
			extron_write_edid(port, hdmi_edid, 2);
	}

	/* Enable CEC (manual mode, i.e. controlled by the driver) */
	err = extron_send_and_wait(extron, NULL, "WI1*20CCEC", "CcecI1*");
	if (err)
		return err;

	err = extron_send_and_wait(extron, NULL, "WO20*CCEC", "CcecO20");
	if (err)
		return err;

	/* Set logical addresses to 15 */
	err = extron_send_and_wait(extron, NULL, "WI1*15LCEC", "LcecI1*15");
	if (err)
		return err;

	for (p = 0; p < extron->num_out_ports; p++) {
		char cmd[20];
		char resp[20];

		snprintf(cmd, sizeof(cmd), "WO%u*15LCEC", p + 1);
		snprintf(resp, sizeof(resp), "LcecO%u*15", p + 1);
		err = extron_send_and_wait(extron, extron->ports[p], cmd, resp);
		if (err)
			return err;
	}

	/*
	 * The Extron is now ready for operation. Specifically it is now
	 * possible to retrieve EDIDs.
	 */
	extron->is_ready = true;

	/* Query HDCP and Signal states, used to update the initial state */
	err = extron_send_and_wait(extron, NULL, "WHDCP", "Hdcp");
	if (err)
		return err;

	return extron_send_and_wait(extron, NULL, "WLS", "Sig");
}

static int extron_setup_thread(void *_extron)
{
	struct extron *extron = _extron;
	struct extron_port *port;
	unsigned int p;
	bool poll_splitter = false;
	bool was_connected = true;
	int err;

	while (1) {
		if (kthread_should_stop())
			return 0;
		err = extron_send_and_wait(extron, NULL, "W3CV", "Vrb3");
		// that should make it possible to detect a serio disconnect
		// here by stopping the workqueue
		if (err >= 0)
			break;
		was_connected = false;
		ssleep(1);
	}

	/*
	 * If the Extron was not connected at probe() time, i.e. it just got
	 * powered up and while the serial port is working, the firmware is
	 * still booting up, then wait 10 seconds for the firmware to settle.
	 *
	 * Trying to continue too soon means that some commands will not
	 * work yet.
	 */
	if (!was_connected)
		ssleep(10);

	err = extron_setup(extron);
	if (err)
		goto disable_ports;

	for (p = 0; p < extron->num_ports; p++) {
		struct cec_log_addrs log_addrs = {};

		port = extron->ports[p];
		if (port->is_input && manufacturer_name[0])
			v4l2_disable_ioctl(&port->vdev, VIDIOC_S_EDID);
		err = video_register_device(&port->vdev, VFL_TYPE_VIDEO, -1);
		if (err) {
			v4l2_err(&extron->v4l2_dev, "Failed to register video device\n");
			goto disable_ports;
		}

		err = cec_register_adapter(port->adap, extron->dev);
		if (err < 0)
			goto disable_ports;
		port->dev = &port->adap->devnode.dev;
		port->cec_was_registered = true;
		/*
		 * This driver is unusual in that the whole setup takes place
		 * in a thread since it can take such a long time before the
		 * Extron Splitter boots up, and you do not want to block the
		 * probe function on this driver. In addition, as soon as
		 * CEC adapters come online, they can be used, and you cannot
		 * just unregister them again if an error occurs, since that
		 * can delete the underlying CEC adapter, which might already
		 * be in use.
		 *
		 * So we take an additional reference to the adapter. This
		 * allows us to unregister the device node if needed, without
		 * deleting the actual adapter.
		 *
		 * In the disconnect function we will do the corresponding
		 * put call to ensure the adapter is deleted.
		 */
		cec_get_device(port->adap);

		/*
		 * If vendor_id wasn't set, then userspace configures the
		 * CEC devices. Otherwise the driver configures the CEC
		 * devices as TV (input) and Playback (outputs) devices
		 * and the driver processes all CEC messages.
		 */
		if (!vendor_id)
			continue;

		log_addrs.cec_version = CEC_OP_CEC_VERSION_2_0;
		log_addrs.num_log_addrs = 1;
		log_addrs.vendor_id = vendor_id;
		if (port->is_input) {
			strscpy(log_addrs.osd_name, "Splitter In",
				sizeof(log_addrs.osd_name));
			log_addrs.log_addr_type[0] = CEC_LOG_ADDR_TYPE_TV;
			log_addrs.primary_device_type[0] = CEC_OP_PRIM_DEVTYPE_TV;
			log_addrs.all_device_types[0] = CEC_OP_ALL_DEVTYPE_TV;
		} else {
			snprintf(log_addrs.osd_name, sizeof(log_addrs.osd_name),
				 "Splitter Out%u", port->port.port);
			log_addrs.log_addr_type[0] = CEC_LOG_ADDR_TYPE_PLAYBACK;
			log_addrs.primary_device_type[0] = CEC_OP_PRIM_DEVTYPE_PLAYBACK;
			log_addrs.all_device_types[0] = CEC_OP_ALL_DEVTYPE_PLAYBACK;
		}
		err = cec_s_log_addrs(port->adap, &log_addrs, false);
		if (err < 0)
			goto disable_ports;
	}
	poll_splitter = true;

	port = extron->ports[extron->num_out_ports];
	while (!kthread_should_stop()) {
		ssleep(1);
		if (hpd_never_low != extron->hpd_never_low) {
			/*
			 * Keep input port HPD high at all times, or pull it low
			 * if all output ports also have a low HPD
			 */
			if (hpd_never_low) {
				dev_info(extron->dev, "Always keep input HPD high\n");
				extron_send_and_wait(extron, NULL, "W0ihpd", "Ihpd0");
			} else {
				dev_info(extron->dev, "Pull input HPD low if all output HPDs are low\n");
				extron_send_and_wait(extron, NULL, "W1ihpd", "Ihpd1");
			}
			extron->hpd_never_low = hpd_never_low;
		}
		if (poll_splitter &&
		    cec_splitter_poll(&extron->splitter, port->adap, debug) &&
		    manufacturer_name[0]) {
			/*
			 * Sinks were lost, so see if the input edid needs to
			 * be updated.
			 */
			cancel_delayed_work_sync(&extron->work_update_edid);
			schedule_delayed_work(&extron->work_update_edid,
					      msecs_to_jiffies(1000));
		}
	}
	return 0;

disable_ports:
	extron->is_ready = false;
	for (p = 0; p < extron->num_ports; p++) {
		struct extron_port *port = extron->ports[p];

		if (!port)
			continue;
		port->disconnected = true;
		cancel_work_sync(&port->irq_work);
		video_unregister_device(&port->vdev);
		if (port->cec_was_registered)
			cec_unregister_adapter(port->adap);
	}
	cancel_delayed_work_sync(&extron->work_update_edid);
	complete(&extron->edid_completion);
	dev_err(extron->dev, "Setup failed with error %d\n", err);
	while (!kthread_should_stop())
		ssleep(1);
	return err;
}

static int extron_connect(struct serio *serio, struct serio_driver *drv)
{
	struct extron *extron;
	int err = -ENOMEM;

	if (manufacturer_name[0] &&
	    (!isupper(manufacturer_name[0]) ||
	     !isupper(manufacturer_name[1]) ||
	     !isupper(manufacturer_name[2]))) {
		dev_warn(&serio->dev, "ignoring invalid manufacturer name\n");
		manufacturer_name[0] = 0;
	}

	extron = kzalloc(sizeof(*extron), GFP_KERNEL);

	if (!extron)
		return -ENOMEM;

	extron->serio = serio;
	extron->dev = &serio->dev;
	mutex_init(&extron->serio_lock);
	mutex_init(&extron->edid_lock);
	INIT_DELAYED_WORK(&extron->work_update_edid, update_edid_work);

	err = v4l2_device_register(extron->dev, &extron->v4l2_dev);
	if (err)
		goto free_device;

	err = serio_open(serio, drv);
	if (err)
		goto unreg_v4l2_dev;

	serio_set_drvdata(serio, extron);
	init_completion(&extron->edid_completion);

	extron->kthread_setup = kthread_run(extron_setup_thread, extron,
					    "extron-da-hd-4k-plus-cec-%s", dev_name(&serio->dev));
	if (!IS_ERR(extron->kthread_setup))
		return 0;

	dev_err(extron->dev, "kthread_run() failed\n");
	err = PTR_ERR(extron->kthread_setup);

	extron->serio = NULL;
	serio_set_drvdata(serio, NULL);
	serio_close(serio);
unreg_v4l2_dev:
	v4l2_device_unregister(&extron->v4l2_dev);
free_device:
	mutex_destroy(&extron->edid_lock);
	mutex_destroy(&extron->serio_lock);
	kfree(extron);
	return err;
}

static const struct serio_device_id extron_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_EXTRON_DA_HD_4K_PLUS,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, extron_serio_ids);

static struct serio_driver extron_drv = {
	.driver		= {
		.name	= "extron-da-hd-4k-plus-cec",
	},
	.description	= "Extron DA HD 4K PLUS HDMI CEC driver",
	.id_table	= extron_serio_ids,
	.interrupt	= extron_interrupt,
	.connect	= extron_connect,
	.disconnect	= extron_disconnect,
};

module_serio_driver(extron_drv);
