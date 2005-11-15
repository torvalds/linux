/* cx25840 API header
 *
 * Copyright (C) 2003-2004 Chris Kennedy
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _CX25840_H_
#define _CX25840_H_


#include <linux/videodev2.h>
#include <linux/i2c.h>

extern int cx25840_debug;

#define cx25840_dbg(fmt, arg...) do { if (cx25840_debug) \
	printk(KERN_INFO "%s debug %d-%04x: " fmt, client->driver->name, \
	       i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)

#define cx25840_err(fmt, arg...) do { \
	printk(KERN_ERR "%s %d-%04x: " fmt, client->driver->name, \
	       i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)

#define cx25840_info(fmt, arg...) do { \
	printk(KERN_INFO "%s %d-%04x: " fmt, client->driver->name, \
	       i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)

#define CX25840_CID_CARDTYPE (V4L2_CID_PRIVATE_BASE+0)

enum cx25840_cardtype {
	CARDTYPE_PVR150,
	CARDTYPE_PG600
};

enum cx25840_input {
	CX25840_TUNER,
	CX25840_COMPOSITE0,
	CX25840_COMPOSITE1,
	CX25840_SVIDEO0,
	CX25840_SVIDEO1
};

struct cx25840_state {
	enum cx25840_cardtype cardtype;
	enum cx25840_input input;
	int audio_input;
	enum v4l2_audio_clock_freq audclk_freq;
};

/* ----------------------------------------------------------------------- */
/* cx25850-core.c 							   */
int cx25840_write(struct i2c_client *client, u16 addr, u8 value);
int cx25840_write4(struct i2c_client *client, u16 addr, u32 value);
u8 cx25840_read(struct i2c_client *client, u16 addr);
u32 cx25840_read4(struct i2c_client *client, u16 addr);
int cx25840_and_or(struct i2c_client *client, u16 addr, u8 mask, u8 value);
v4l2_std_id cx25840_get_v4lstd(struct i2c_client *client);

/* ----------------------------------------------------------------------- */
/* cx25850-firmware.c                                                      */
int cx25840_loadfw(struct i2c_client *client);

/* ----------------------------------------------------------------------- */
/* cx25850-audio.c                                                         */
int cx25840_audio(struct i2c_client *client, unsigned int cmd, void *arg);

/* ----------------------------------------------------------------------- */
/* cx25850-vbi.c                                                           */
void cx25840_vbi_setup(struct i2c_client *client);
int cx25840_vbi(struct i2c_client *client, unsigned int cmd, void *arg);

#endif
