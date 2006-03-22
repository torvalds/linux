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

/* ENABLE_PVR150_WORKAROUND activates a workaround for a hardware bug that is
   present in Hauppauge PVR-150 (and possibly PVR-500) cards that have
   certain NTSC tuners (tveeprom tuner model numbers 85, 99 and 112). The
   audio autodetect fails on some channels for these models and the workaround
   is to select the audio standard explicitly. Many thanks to Hauppauge for
   providing this information. */
#define CX25840_CID_ENABLE_PVR150_WORKAROUND (V4L2_CID_PRIVATE_BASE+0)

enum cx25840_video_input {
	/* Composite video inputs In1-In8 */
	CX25840_COMPOSITE1 = 1,
	CX25840_COMPOSITE2,
	CX25840_COMPOSITE3,
	CX25840_COMPOSITE4,
	CX25840_COMPOSITE5,
	CX25840_COMPOSITE6,
	CX25840_COMPOSITE7,
	CX25840_COMPOSITE8,

	/* S-Video inputs consist of one luma input (In1-In4) ORed with one
	   chroma input (In5-In8) */
	CX25840_SVIDEO_LUMA1 = 0x10,
	CX25840_SVIDEO_LUMA2 = 0x20,
	CX25840_SVIDEO_LUMA3 = 0x30,
	CX25840_SVIDEO_LUMA4 = 0x40,
	CX25840_SVIDEO_CHROMA4 = 0x400,
	CX25840_SVIDEO_CHROMA5 = 0x500,
	CX25840_SVIDEO_CHROMA6 = 0x600,
	CX25840_SVIDEO_CHROMA7 = 0x700,
	CX25840_SVIDEO_CHROMA8 = 0x800,

	/* S-Video aliases for common luma/chroma combinations */
	CX25840_SVIDEO1 = 0x510,
	CX25840_SVIDEO2 = 0x620,
	CX25840_SVIDEO3 = 0x730,
	CX25840_SVIDEO4 = 0x840,
};

enum cx25840_audio_input {
	/* Audio inputs: serial or In4-In8 */
	CX25840_AUDIO_SERIAL,
	CX25840_AUDIO4 = 4,
	CX25840_AUDIO5,
	CX25840_AUDIO6,
	CX25840_AUDIO7,
	CX25840_AUDIO8,
};

struct cx25840_state {
	int pvr150_workaround;
	int radio;
	enum cx25840_video_input vid_input;
	enum cx25840_audio_input aud_input;
	u32 audclk_freq;
	int audmode;
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
void cx25840_audio_set_path(struct i2c_client *client);

/* ----------------------------------------------------------------------- */
/* cx25850-vbi.c                                                           */
void cx25840_vbi_setup(struct i2c_client *client);
int cx25840_vbi(struct i2c_client *client, unsigned int cmd, void *arg);

#endif
