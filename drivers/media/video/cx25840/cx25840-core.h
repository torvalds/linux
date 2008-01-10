/* cx25840 internal API header
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

#ifndef _CX25840_CORE_H_
#define _CX25840_CORE_H_


#include <linux/videodev2.h>
#include <linux/i2c.h>

extern int cx25840_debug;

/* ENABLE_PVR150_WORKAROUND activates a workaround for a hardware bug that is
   present in Hauppauge PVR-150 (and possibly PVR-500) cards that have
   certain NTSC tuners (tveeprom tuner model numbers 85, 99 and 112). The
   audio autodetect fails on some channels for these models and the workaround
   is to select the audio standard explicitly. Many thanks to Hauppauge for
   providing this information. */
#define CX25840_CID_ENABLE_PVR150_WORKAROUND (V4L2_CID_PRIVATE_BASE+0)

struct cx25840_state {
	struct i2c_client *c;
	int pvr150_workaround;
	int radio;
	enum cx25840_video_input vid_input;
	enum cx25840_audio_input aud_input;
	u32 audclk_freq;
	int audmode;
	int unmute_volume; /* -1 if not muted */
	int vbi_line_offset;
	u32 id;
	u32 rev;
	int is_cx25836;
	int is_cx23885;
	int is_initialized;
	wait_queue_head_t fw_wait;    /* wake up when the fw load is finished */
	struct work_struct fw_work;   /* work entry for fw load */
};

/* ----------------------------------------------------------------------- */
/* cx25850-core.c 							   */
int cx25840_write(struct i2c_client *client, u16 addr, u8 value);
int cx25840_write4(struct i2c_client *client, u16 addr, u32 value);
u8 cx25840_read(struct i2c_client *client, u16 addr);
u32 cx25840_read4(struct i2c_client *client, u16 addr);
int cx25840_and_or(struct i2c_client *client, u16 addr, unsigned mask, u8 value);
v4l2_std_id cx25840_get_v4lstd(struct i2c_client *client);

/* ----------------------------------------------------------------------- */
/* cx25850-firmware.c                                                      */
int cx25840_loadfw(struct i2c_client *client);

/* ----------------------------------------------------------------------- */
/* cx25850-audio.c                                                         */
int cx25840_audio(struct i2c_client *client, unsigned int cmd, void *arg);
void cx25840_audio_set_path(struct i2c_client *client);

/* ----------------------------------------------------------------------- */
/* cx25850-vbi.c                                                           */
void cx25840_vbi_setup(struct i2c_client *client);
int cx25840_vbi(struct i2c_client *client, unsigned int cmd, void *arg);

#endif
