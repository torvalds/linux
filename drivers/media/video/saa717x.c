/*
 * saa717x - Philips SAA717xHL video decoder driver
 *
 * Based on the saa7115 driver
 *
 * Changes by Ohta Kyuma <alpha292@bremen.or.jp>
 *    - Apply to SAA717x,NEC uPD64031,uPD64083. (1/31/2004)
 *
 * Changes by T.Adachi (tadachi@tadachi-net.com)
 *    - support audio, video scaler etc, and checked the initialize sequence.
 *
 * Cleaned up by Hans Verkuil <hverkuil@xs4all.nl>
 *
 * Note: this is a reversed engineered driver based on captures from
 * the I2C bus under Windows. This chip is very similar to the saa7134,
 * though. Unfortunately, this driver is currently only working for NTSC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <media/v4l2-device.h>
#include <media/v4l2-i2c-drv.h>

MODULE_DESCRIPTION("Philips SAA717x audio/video decoder driver");
MODULE_AUTHOR("K. Ohta, T. Adachi, Hans Verkuil");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */

struct saa717x_state {
	struct v4l2_subdev sd;
	v4l2_std_id std;
	int input;
	int enable;
	int radio;
	int bright;
	int contrast;
	int hue;
	int sat;
	int playback;
	int audio;
	int tuner_audio_mode;
	int audio_main_mute;
	int audio_main_vol_r;
	int audio_main_vol_l;
	u16 audio_main_bass;
	u16 audio_main_treble;
	u16 audio_main_volume;
	u16 audio_main_balance;
	int audio_input;
};

static inline struct saa717x_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct saa717x_state, sd);
}

/* ----------------------------------------------------------------------- */

/* for audio mode */
#define TUNER_AUDIO_MONO   	0  /* LL */
#define TUNER_AUDIO_STEREO 	1  /* LR */
#define TUNER_AUDIO_LANG1  	2  /* LL */
#define TUNER_AUDIO_LANG2  	3  /* RR */

#define SAA717X_NTSC_WIDTH   	(704)
#define SAA717X_NTSC_HEIGHT  	(480)

/* ----------------------------------------------------------------------- */

static int saa717x_write(struct v4l2_subdev *sd, u32 reg, u32 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *adap = client->adapter;
	int fw_addr = reg == 0x454 || (reg >= 0x464 && reg <= 0x478) || reg == 0x480 || reg == 0x488;
	unsigned char mm1[6];
	struct i2c_msg msg;

	msg.flags = 0;
	msg.addr = client->addr;
	mm1[0] = (reg >> 8) & 0xff;
	mm1[1] = reg & 0xff;

	if (fw_addr) {
		mm1[4] = (value >> 16) & 0xff;
		mm1[3] = (value >> 8) & 0xff;
		mm1[2] = value & 0xff;
	} else {
		mm1[2] = value & 0xff;
	}
	msg.len = fw_addr ? 5 : 3; /* Long Registers have *only* three bytes! */
	msg.buf = mm1;
	v4l2_dbg(2, debug, sd, "wrote:  reg 0x%03x=%08x\n", reg, value);
	return i2c_transfer(adap, &msg, 1) == 1;
}

static void saa717x_write_regs(struct v4l2_subdev *sd, u32 *data)
{
	while (data[0] || data[1]) {
		saa717x_write(sd, data[0], data[1]);
		data += 2;
	}
}

static u32 saa717x_read(struct v4l2_subdev *sd, u32 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *adap = client->adapter;
	int fw_addr = (reg >= 0x404 && reg <= 0x4b8) || reg == 0x528;
	unsigned char mm1[2];
	unsigned char mm2[4] = { 0, 0, 0, 0 };
	struct i2c_msg msgs[2];
	u32 value;

	msgs[0].flags = 0;
	msgs[1].flags = I2C_M_RD;
	msgs[0].addr = msgs[1].addr = client->addr;
	mm1[0] = (reg >> 8) & 0xff;
	mm1[1] = reg & 0xff;
	msgs[0].len = 2;
	msgs[0].buf = mm1;
	msgs[1].len = fw_addr ? 3 : 1; /* Multibyte Registers contains *only* 3 bytes */
	msgs[1].buf = mm2;
	i2c_transfer(adap, msgs, 2);

	if (fw_addr)
		value = (mm2[2] & 0xff)  | ((mm2[1] & 0xff) >> 8) | ((mm2[0] & 0xff) >> 16);
	else
		value = mm2[0] & 0xff;

	v4l2_dbg(2, debug, sd, "read:  reg 0x%03x=0x%08x\n", reg, value);
	return value;
}

/* ----------------------------------------------------------------------- */

static u32 reg_init_initialize[] =
{
	/* from linux driver */
	0x101, 0x008, /* Increment delay */

	0x103, 0x000, /* Analog input control 2 */
	0x104, 0x090, /* Analog input control 3 */
	0x105, 0x090, /* Analog input control 4 */
	0x106, 0x0eb, /* Horizontal sync start */
	0x107, 0x0e0, /* Horizontal sync stop */
	0x109, 0x055, /* Luminance control */

	0x10f, 0x02a, /* Chroma gain control */
	0x110, 0x000, /* Chroma control 2 */

	0x114, 0x045, /* analog/ADC */

	0x118, 0x040, /* RAW data gain */
	0x119, 0x080, /* RAW data offset */

	0x044, 0x000, /* VBI horizontal input window start (L) TASK A */
	0x045, 0x000, /* VBI horizontal input window start (H) TASK A */
	0x046, 0x0cf, /* VBI horizontal input window stop (L) TASK A */
	0x047, 0x002, /* VBI horizontal input window stop (H) TASK A */

	0x049, 0x000, /* VBI vertical input window start (H) TASK A */

	0x04c, 0x0d0, /* VBI horizontal output length (L) TASK A */
	0x04d, 0x002, /* VBI horizontal output length (H) TASK A */

	0x064, 0x080, /* Lumina brightness TASK A */
	0x065, 0x040, /* Luminance contrast TASK A */
	0x066, 0x040, /* Chroma saturation TASK A */
	/* 067H: Reserved */
	0x068, 0x000, /* VBI horizontal scaling increment (L) TASK A */
	0x069, 0x004, /* VBI horizontal scaling increment (H) TASK A */
	0x06a, 0x000, /* VBI phase offset TASK A */

	0x06e, 0x000, /* Horizontal phase offset Luma TASK A */
	0x06f, 0x000, /* Horizontal phase offset Chroma TASK A */

	0x072, 0x000, /* Vertical filter mode TASK A */

	0x084, 0x000, /* VBI horizontal input window start (L) TAKS B */
	0x085, 0x000, /* VBI horizontal input window start (H) TAKS B */
	0x086, 0x0cf, /* VBI horizontal input window stop (L) TAKS B */
	0x087, 0x002, /* VBI horizontal input window stop (H) TAKS B */

	0x089, 0x000, /* VBI vertical input window start (H) TAKS B */

	0x08c, 0x0d0, /* VBI horizontal output length (L) TASK B */
	0x08d, 0x002, /* VBI horizontal output length (H) TASK B */

	0x0a4, 0x080, /* Lumina brightness TASK B */
	0x0a5, 0x040, /* Luminance contrast TASK B */
	0x0a6, 0x040, /* Chroma saturation TASK B */
	/* 0A7H reserved */
	0x0a8, 0x000, /* VBI horizontal scaling increment (L) TASK B */
	0x0a9, 0x004, /* VBI horizontal scaling increment (H) TASK B */
	0x0aa, 0x000, /* VBI phase offset TASK B */

	0x0ae, 0x000, /* Horizontal phase offset Luma TASK B */
	0x0af, 0x000, /*Horizontal phase offset Chroma TASK B */

	0x0b2, 0x000, /* Vertical filter mode TASK B */

	0x00c, 0x000, /* Start point GREEN path */
	0x00d, 0x000, /* Start point BLUE path */
	0x00e, 0x000, /* Start point RED path */

	0x010, 0x010, /* GREEN path gamma curve --- */
	0x011, 0x020,
	0x012, 0x030,
	0x013, 0x040,
	0x014, 0x050,
	0x015, 0x060,
	0x016, 0x070,
	0x017, 0x080,
	0x018, 0x090,
	0x019, 0x0a0,
	0x01a, 0x0b0,
	0x01b, 0x0c0,
	0x01c, 0x0d0,
	0x01d, 0x0e0,
	0x01e, 0x0f0,
	0x01f, 0x0ff, /* --- GREEN path gamma curve */

	0x020, 0x010, /* BLUE path gamma curve --- */
	0x021, 0x020,
	0x022, 0x030,
	0x023, 0x040,
	0x024, 0x050,
	0x025, 0x060,
	0x026, 0x070,
	0x027, 0x080,
	0x028, 0x090,
	0x029, 0x0a0,
	0x02a, 0x0b0,
	0x02b, 0x0c0,
	0x02c, 0x0d0,
	0x02d, 0x0e0,
	0x02e, 0x0f0,
	0x02f, 0x0ff, /* --- BLUE path gamma curve */

	0x030, 0x010, /* RED path gamma curve --- */
	0x031, 0x020,
	0x032, 0x030,
	0x033, 0x040,
	0x034, 0x050,
	0x035, 0x060,
	0x036, 0x070,
	0x037, 0x080,
	0x038, 0x090,
	0x039, 0x0a0,
	0x03a, 0x0b0,
	0x03b, 0x0c0,
	0x03c, 0x0d0,
	0x03d, 0x0e0,
	0x03e, 0x0f0,
	0x03f, 0x0ff, /* --- RED path gamma curve */

	0x109, 0x085, /* Luminance control  */

	/**** from app start ****/
	0x584, 0x000, /* AGC gain control */
	0x585, 0x000, /* Program count */
	0x586, 0x003, /* Status reset */
	0x588, 0x0ff, /* Number of audio samples (L) */
	0x589, 0x00f, /* Number of audio samples (M) */
	0x58a, 0x000, /* Number of audio samples (H) */
	0x58b, 0x000, /* Audio select */
	0x58c, 0x010, /* Audio channel assign1 */
	0x58d, 0x032, /* Audio channel assign2 */
	0x58e, 0x054, /* Audio channel assign3 */
	0x58f, 0x023, /* Audio format */
	0x590, 0x000, /* SIF control */

	0x595, 0x000, /* ?? */
	0x596, 0x000, /* ?? */
	0x597, 0x000, /* ?? */

	0x464, 0x00, /* Digital input crossbar1 */

	0x46c, 0xbbbb10, /* Digital output selection1-3 */
	0x470, 0x101010, /* Digital output selection4-6 */

	0x478, 0x00, /* Sound feature control */

	0x474, 0x18, /* Softmute control */

	0x454, 0x0425b9, /* Sound Easy programming(reset) */
	0x454, 0x042539, /* Sound Easy programming(reset) */


	/**** common setting( of DVD play, including scaler commands) ****/
	0x042, 0x003, /* Data path configuration for VBI (TASK A) */

	0x082, 0x003, /* Data path configuration for VBI (TASK B) */

	0x108, 0x0f8, /* Sync control */
	0x2a9, 0x0fd, /* ??? */
	0x102, 0x089, /* select video input "mode 9" */
	0x111, 0x000, /* Mode/delay control */

	0x10e, 0x00a, /* Chroma control 1 */

	0x594, 0x002, /* SIF, analog I/O select */

	0x454, 0x0425b9, /* Sound  */
	0x454, 0x042539,

	0x111, 0x000,
	0x10e, 0x00a,
	0x464, 0x000,
	0x300, 0x000,
	0x301, 0x006,
	0x302, 0x000,
	0x303, 0x006,
	0x308, 0x040,
	0x309, 0x000,
	0x30a, 0x000,
	0x30b, 0x000,
	0x000, 0x002,
	0x001, 0x000,
	0x002, 0x000,
	0x003, 0x000,
	0x004, 0x033,
	0x040, 0x01d,
	0x041, 0x001,
	0x042, 0x004,
	0x043, 0x000,
	0x080, 0x01e,
	0x081, 0x001,
	0x082, 0x004,
	0x083, 0x000,
	0x190, 0x018,
	0x115, 0x000,
	0x116, 0x012,
	0x117, 0x018,
	0x04a, 0x011,
	0x08a, 0x011,
	0x04b, 0x000,
	0x08b, 0x000,
	0x048, 0x000,
	0x088, 0x000,
	0x04e, 0x012,
	0x08e, 0x012,
	0x058, 0x012,
	0x098, 0x012,
	0x059, 0x000,
	0x099, 0x000,
	0x05a, 0x003,
	0x09a, 0x003,
	0x05b, 0x001,
	0x09b, 0x001,
	0x054, 0x008,
	0x094, 0x008,
	0x055, 0x000,
	0x095, 0x000,
	0x056, 0x0c7,
	0x096, 0x0c7,
	0x057, 0x002,
	0x097, 0x002,
	0x0ff, 0x0ff,
	0x060, 0x001,
	0x0a0, 0x001,
	0x061, 0x000,
	0x0a1, 0x000,
	0x062, 0x000,
	0x0a2, 0x000,
	0x063, 0x000,
	0x0a3, 0x000,
	0x070, 0x000,
	0x0b0, 0x000,
	0x071, 0x004,
	0x0b1, 0x004,
	0x06c, 0x0e9,
	0x0ac, 0x0e9,
	0x06d, 0x003,
	0x0ad, 0x003,
	0x05c, 0x0d0,
	0x09c, 0x0d0,
	0x05d, 0x002,
	0x09d, 0x002,
	0x05e, 0x0f2,
	0x09e, 0x0f2,
	0x05f, 0x000,
	0x09f, 0x000,
	0x074, 0x000,
	0x0b4, 0x000,
	0x075, 0x000,
	0x0b5, 0x000,
	0x076, 0x000,
	0x0b6, 0x000,
	0x077, 0x000,
	0x0b7, 0x000,
	0x195, 0x008,
	0x0ff, 0x0ff,
	0x108, 0x0f8,
	0x111, 0x000,
	0x10e, 0x00a,
	0x2a9, 0x0fd,
	0x464, 0x001,
	0x454, 0x042135,
	0x598, 0x0e7,
	0x599, 0x07d,
	0x59a, 0x018,
	0x59c, 0x066,
	0x59d, 0x090,
	0x59e, 0x001,
	0x584, 0x000,
	0x585, 0x000,
	0x586, 0x003,
	0x588, 0x0ff,
	0x589, 0x00f,
	0x58a, 0x000,
	0x58b, 0x000,
	0x58c, 0x010,
	0x58d, 0x032,
	0x58e, 0x054,
	0x58f, 0x023,
	0x590, 0x000,
	0x595, 0x000,
	0x596, 0x000,
	0x597, 0x000,
	0x464, 0x000,
	0x46c, 0xbbbb10,
	0x470, 0x101010,


	0x478, 0x000,
	0x474, 0x018,
	0x454, 0x042135,
	0x598, 0x0e7,
	0x599, 0x07d,
	0x59a, 0x018,
	0x59c, 0x066,
	0x59d, 0x090,
	0x59e, 0x001,
	0x584, 0x000,
	0x585, 0x000,
	0x586, 0x003,
	0x588, 0x0ff,
	0x589, 0x00f,
	0x58a, 0x000,
	0x58b, 0x000,
	0x58c, 0x010,
	0x58d, 0x032,
	0x58e, 0x054,
	0x58f, 0x023,
	0x590, 0x000,
	0x595, 0x000,
	0x596, 0x000,
	0x597, 0x000,
	0x464, 0x000,
	0x46c, 0xbbbb10,
	0x470, 0x101010,

	0x478, 0x000,
	0x474, 0x018,
	0x454, 0x042135,
	0x598, 0x0e7,
	0x599, 0x07d,
	0x59a, 0x018,
	0x59c, 0x066,
	0x59d, 0x090,
	0x59e, 0x001,
	0x584, 0x000,
	0x585, 0x000,
	0x586, 0x003,
	0x588, 0x0ff,
	0x589, 0x00f,
	0x58a, 0x000,
	0x58b, 0x000,
	0x58c, 0x010,
	0x58d, 0x032,
	0x58e, 0x054,
	0x58f, 0x023,
	0x590, 0x000,
	0x595, 0x000,
	0x596, 0x000,
	0x597, 0x000,
	0x464, 0x000,
	0x46c, 0xbbbb10,
	0x470, 0x101010,
	0x478, 0x000,
	0x474, 0x018,
	0x454, 0x042135,
	0x193, 0x000,
	0x300, 0x000,
	0x301, 0x006,
	0x302, 0x000,
	0x303, 0x006,
	0x308, 0x040,
	0x309, 0x000,
	0x30a, 0x000,
	0x30b, 0x000,
	0x000, 0x002,
	0x001, 0x000,
	0x002, 0x000,
	0x003, 0x000,
	0x004, 0x033,
	0x040, 0x01d,
	0x041, 0x001,
	0x042, 0x004,
	0x043, 0x000,
	0x080, 0x01e,
	0x081, 0x001,
	0x082, 0x004,
	0x083, 0x000,
	0x190, 0x018,
	0x115, 0x000,
	0x116, 0x012,
	0x117, 0x018,
	0x04a, 0x011,
	0x08a, 0x011,
	0x04b, 0x000,
	0x08b, 0x000,
	0x048, 0x000,
	0x088, 0x000,
	0x04e, 0x012,
	0x08e, 0x012,
	0x058, 0x012,
	0x098, 0x012,
	0x059, 0x000,
	0x099, 0x000,
	0x05a, 0x003,
	0x09a, 0x003,
	0x05b, 0x001,
	0x09b, 0x001,
	0x054, 0x008,
	0x094, 0x008,
	0x055, 0x000,
	0x095, 0x000,
	0x056, 0x0c7,
	0x096, 0x0c7,
	0x057, 0x002,
	0x097, 0x002,
	0x060, 0x001,
	0x0a0, 0x001,
	0x061, 0x000,
	0x0a1, 0x000,
	0x062, 0x000,
	0x0a2, 0x000,
	0x063, 0x000,
	0x0a3, 0x000,
	0x070, 0x000,
	0x0b0, 0x000,
	0x071, 0x004,
	0x0b1, 0x004,
	0x06c, 0x0e9,
	0x0ac, 0x0e9,
	0x06d, 0x003,
	0x0ad, 0x003,
	0x05c, 0x0d0,
	0x09c, 0x0d0,
	0x05d, 0x002,
	0x09d, 0x002,
	0x05e, 0x0f2,
	0x09e, 0x0f2,
	0x05f, 0x000,
	0x09f, 0x000,
	0x074, 0x000,
	0x0b4, 0x000,
	0x075, 0x000,
	0x0b5, 0x000,
	0x076, 0x000,
	0x0b6, 0x000,
	0x077, 0x000,
	0x0b7, 0x000,
	0x195, 0x008,
	0x598, 0x0e7,
	0x599, 0x07d,
	0x59a, 0x018,
	0x59c, 0x066,
	0x59d, 0x090,
	0x59e, 0x001,
	0x584, 0x000,
	0x585, 0x000,
	0x586, 0x003,
	0x588, 0x0ff,
	0x589, 0x00f,
	0x58a, 0x000,
	0x58b, 0x000,
	0x58c, 0x010,
	0x58d, 0x032,
	0x58e, 0x054,
	0x58f, 0x023,
	0x590, 0x000,
	0x595, 0x000,
	0x596, 0x000,
	0x597, 0x000,
	0x464, 0x000,
	0x46c, 0xbbbb10,
	0x470, 0x101010,
	0x478, 0x000,
	0x474, 0x018,
	0x454, 0x042135,
	0x193, 0x0a6,
	0x108, 0x0f8,
	0x042, 0x003,
	0x082, 0x003,
	0x454, 0x0425b9,
	0x454, 0x042539,
	0x193, 0x000,
	0x193, 0x0a6,
	0x464, 0x000,

	0, 0
};

/* Tuner */
static u32 reg_init_tuner_input[] = {
	0x108, 0x0f8, /* Sync control */
	0x111, 0x000, /* Mode/delay control */
	0x10e, 0x00a, /* Chroma control 1 */
	0, 0
};

/* Composite */
static u32 reg_init_composite_input[] = {
	0x108, 0x0e8, /* Sync control */
	0x111, 0x000, /* Mode/delay control */
	0x10e, 0x04a, /* Chroma control 1 */
	0, 0
};

/* S-Video */
static u32 reg_init_svideo_input[] = {
	0x108, 0x0e8, /* Sync control */
	0x111, 0x000, /* Mode/delay control */
	0x10e, 0x04a, /* Chroma control 1 */
	0, 0
};

static u32 reg_set_audio_template[4][2] =
{
	{ /* for MONO
		tadachi 6/29 DMA audio output select?
		Register 0x46c
		7-4: DMA2, 3-0: DMA1 ch. DMA4, DMA3 DMA2, DMA1
		0: MAIN left,  1: MAIN right
		2: AUX1 left,  3: AUX1 right
		4: AUX2 left,  5: AUX2 right
		6: DPL left,   7: DPL  right
		8: DPL center, 9: DPL surround
		A: monitor output, B: digital sense */
		0xbbbb00,

		/* tadachi 6/29 DAC and I2S output select?
		   Register 0x470
		   7-4:DAC right ch. 3-0:DAC left ch.
		   I2S1 right,left  I2S2 right,left */
		0x00,
	},
	{ /* for STEREO */
		0xbbbb10, 0x101010,
	},
	{ /* for LANG1 */
		0xbbbb00, 0x00,
	},
	{ /* for LANG2/SAP */
		0xbbbb11, 0x111111,
	}
};


/* Get detected audio flags (from saa7134 driver) */
static void get_inf_dev_status(struct v4l2_subdev *sd,
		int *dual_flag, int *stereo_flag)
{
	u32 reg_data3;

	static char *stdres[0x20] = {
		[0x00] = "no standard detected",
		[0x01] = "B/G (in progress)",
		[0x02] = "D/K (in progress)",
		[0x03] = "M (in progress)",

		[0x04] = "B/G A2",
		[0x05] = "B/G NICAM",
		[0x06] = "D/K A2 (1)",
		[0x07] = "D/K A2 (2)",
		[0x08] = "D/K A2 (3)",
		[0x09] = "D/K NICAM",
		[0x0a] = "L NICAM",
		[0x0b] = "I NICAM",

		[0x0c] = "M Korea",
		[0x0d] = "M BTSC ",
		[0x0e] = "M EIAJ",

		[0x0f] = "FM radio / IF 10.7 / 50 deemp",
		[0x10] = "FM radio / IF 10.7 / 75 deemp",
		[0x11] = "FM radio / IF sel / 50 deemp",
		[0x12] = "FM radio / IF sel / 75 deemp",

		[0x13 ... 0x1e] = "unknown",
		[0x1f] = "??? [in progress]",
	};


	*dual_flag = *stereo_flag = 0;

	/* (demdec status: 0x528) */

	/* read current status */
	reg_data3 = saa717x_read(sd, 0x0528);

	v4l2_dbg(1, debug, sd, "tvaudio thread status: 0x%x [%s%s%s]\n",
		reg_data3, stdres[reg_data3 & 0x1f],
		(reg_data3 & 0x000020) ? ",stereo" : "",
		(reg_data3 & 0x000040) ? ",dual"   : "");
	v4l2_dbg(1, debug, sd, "detailed status: "
		"%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s\n",
		(reg_data3 & 0x000080) ? " A2/EIAJ pilot tone "     : "",
		(reg_data3 & 0x000100) ? " A2/EIAJ dual "           : "",
		(reg_data3 & 0x000200) ? " A2/EIAJ stereo "         : "",
		(reg_data3 & 0x000400) ? " A2/EIAJ noise mute "     : "",

		(reg_data3 & 0x000800) ? " BTSC/FM radio pilot "    : "",
		(reg_data3 & 0x001000) ? " SAP carrier "            : "",
		(reg_data3 & 0x002000) ? " BTSC stereo noise mute " : "",
		(reg_data3 & 0x004000) ? " SAP noise mute "         : "",
		(reg_data3 & 0x008000) ? " VDSP "                   : "",

		(reg_data3 & 0x010000) ? " NICST "                  : "",
		(reg_data3 & 0x020000) ? " NICDU "                  : "",
		(reg_data3 & 0x040000) ? " NICAM muted "            : "",
		(reg_data3 & 0x080000) ? " NICAM reserve sound "    : "",

		(reg_data3 & 0x100000) ? " init done "              : "");

	if (reg_data3 & 0x000220) {
		v4l2_dbg(1, debug, sd, "ST!!!\n");
		*stereo_flag = 1;
	}

	if (reg_data3 & 0x000140) {
		v4l2_dbg(1, debug, sd, "DUAL!!!\n");
		*dual_flag = 1;
	}
}

/* regs write to set audio mode */
static void set_audio_mode(struct v4l2_subdev *sd, int audio_mode)
{
	v4l2_dbg(1, debug, sd, "writing registers to set audio mode by set %d\n",
			audio_mode);

	saa717x_write(sd, 0x46c, reg_set_audio_template[audio_mode][0]);
	saa717x_write(sd, 0x470, reg_set_audio_template[audio_mode][1]);
}

/* write regs to video output level (bright,contrast,hue,sat) */
static void set_video_output_level_regs(struct v4l2_subdev *sd,
		struct saa717x_state *decoder)
{
	/* brightness ffh (bright) - 80h (ITU level) - 00h (dark) */
	saa717x_write(sd, 0x10a, decoder->bright);

	/* contrast 7fh (max: 1.984) - 44h (ITU) - 40h (1.0) -
	   0h (luminance off) 40: i2c dump
	   c0h (-1.0 inverse chrominance)
	   80h (-2.0 inverse chrominance) */
	saa717x_write(sd, 0x10b, decoder->contrast);

	/* saturation? 7fh(max)-40h(ITU)-0h(color off)
	   c0h (-1.0 inverse chrominance)
	   80h (-2.0 inverse chrominance) */
	saa717x_write(sd, 0x10c, decoder->sat);

	/* color hue (phase) control
	   7fh (+178.6) - 0h (0 normal) - 80h (-180.0) */
	saa717x_write(sd, 0x10d, decoder->hue);
}

/* write regs to set audio volume, bass and treble */
static int set_audio_regs(struct v4l2_subdev *sd,
		struct saa717x_state *decoder)
{
	u8 mute = 0xac; /* -84 dB */
	u32 val;
	unsigned int work_l, work_r;

	/* set SIF analog I/O select */
	saa717x_write(sd, 0x0594, decoder->audio_input);
	v4l2_dbg(1, debug, sd, "set audio input %d\n",
			decoder->audio_input);

	/* normalize ( 65535 to 0 -> 24 to -40 (not -84)) */
	work_l = (min(65536 - decoder->audio_main_balance, 32768) * decoder->audio_main_volume) / 32768;
	work_r = (min(decoder->audio_main_balance, (u16)32768) * decoder->audio_main_volume) / 32768;
	decoder->audio_main_vol_l = (long)work_l * (24 - (-40)) / 65535 - 40;
	decoder->audio_main_vol_r = (long)work_r * (24 - (-40)) / 65535 - 40;

	/* set main volume */
	/* main volume L[7-0],R[7-0],0x00  24=24dB,-83dB, -84(mute) */
	/*    def:0dB->6dB(MPG600GR) */
	/* if mute is on, set mute */
	if (decoder->audio_main_mute) {
		val = mute | (mute << 8);
	} else {
		val = (u8)decoder->audio_main_vol_l |
			((u8)decoder->audio_main_vol_r << 8);
	}

	saa717x_write(sd, 0x480, val);

	/* bass and treble; go to another function */
	/* set bass and treble */
	val = decoder->audio_main_bass | (decoder->audio_main_treble << 8);
	saa717x_write(sd, 0x488, val);
	return 0;
}

/********** scaling staff ***********/
static void set_h_prescale(struct v4l2_subdev *sd,
		int task, int prescale)
{
	static const struct {
		int xpsc;
		int xacl;
		int xc2_1;
		int xdcg;
		int vpfy;
	} vals[] = {
		/* XPSC XACL XC2_1 XDCG VPFY */
		{    1,   0,    0,    0,   0 },
		{    2,   2,    1,    2,   2 },
		{    3,   4,    1,    3,   2 },
		{    4,   8,    1,    4,   2 },
		{    5,   8,    1,    4,   2 },
		{    6,   8,    1,    4,   3 },
		{    7,   8,    1,    4,   3 },
		{    8,  15,    0,    4,   3 },
		{    9,  15,    0,    4,   3 },
		{   10,  16,    1,    5,   3 },
	};
	static const int count = ARRAY_SIZE(vals);
	int i, task_shift;

	task_shift = task * 0x40;
	for (i = 0; i < count; i++)
		if (vals[i].xpsc == prescale)
			break;
	if (i == count)
		return;

	/* horizonal prescaling */
	saa717x_write(sd, 0x60 + task_shift, vals[i].xpsc);
	/* accumulation length */
	saa717x_write(sd, 0x61 + task_shift, vals[i].xacl);
	/* level control */
	saa717x_write(sd, 0x62 + task_shift,
			(vals[i].xc2_1 << 3) | vals[i].xdcg);
	/*FIR prefilter control */
	saa717x_write(sd, 0x63 + task_shift,
			(vals[i].vpfy << 2) | vals[i].vpfy);
}

/********** scaling staff ***********/
static void set_v_scale(struct v4l2_subdev *sd, int task, int yscale)
{
	int task_shift;

	task_shift = task * 0x40;
	/* Vertical scaling ratio (LOW) */
	saa717x_write(sd, 0x70 + task_shift, yscale & 0xff);
	/* Vertical scaling ratio (HI) */
	saa717x_write(sd, 0x71 + task_shift, yscale >> 8);
}

static int saa717x_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct saa717x_state *state = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (ctrl->value < 0 || ctrl->value > 255) {
			v4l2_err(sd, "invalid brightness setting %d\n", ctrl->value);
			return -ERANGE;
		}

		state->bright = ctrl->value;
		v4l2_dbg(1, debug, sd, "bright:%d\n", state->bright);
		saa717x_write(sd, 0x10a, state->bright);
		break;

	case V4L2_CID_CONTRAST:
		if (ctrl->value < 0 || ctrl->value > 127) {
			v4l2_err(sd, "invalid contrast setting %d\n", ctrl->value);
			return -ERANGE;
		}

		state->contrast = ctrl->value;
		v4l2_dbg(1, debug, sd, "contrast:%d\n", state->contrast);
		saa717x_write(sd, 0x10b, state->contrast);
		break;

	case V4L2_CID_SATURATION:
		if (ctrl->value < 0 || ctrl->value > 127) {
			v4l2_err(sd, "invalid saturation setting %d\n", ctrl->value);
			return -ERANGE;
		}

		state->sat = ctrl->value;
		v4l2_dbg(1, debug, sd, "sat:%d\n", state->sat);
		saa717x_write(sd, 0x10c, state->sat);
		break;

	case V4L2_CID_HUE:
		if (ctrl->value < -128 || ctrl->value > 127) {
			v4l2_err(sd, "invalid hue setting %d\n", ctrl->value);
			return -ERANGE;
		}

		state->hue = ctrl->value;
		v4l2_dbg(1, debug, sd, "hue:%d\n", state->hue);
		saa717x_write(sd, 0x10d, state->hue);
		break;

	case V4L2_CID_AUDIO_MUTE:
		state->audio_main_mute = ctrl->value;
		set_audio_regs(sd, state);
		break;

	case V4L2_CID_AUDIO_VOLUME:
		state->audio_main_volume = ctrl->value;
		set_audio_regs(sd, state);
		break;

	case V4L2_CID_AUDIO_BALANCE:
		state->audio_main_balance = ctrl->value;
		set_audio_regs(sd, state);
		break;

	case V4L2_CID_AUDIO_TREBLE:
		state->audio_main_treble = ctrl->value;
		set_audio_regs(sd, state);
		break;

	case V4L2_CID_AUDIO_BASS:
		state->audio_main_bass = ctrl->value;
		set_audio_regs(sd, state);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int saa717x_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct saa717x_state *state = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = state->bright;
		break;

	case V4L2_CID_CONTRAST:
		ctrl->value = state->contrast;
		break;

	case V4L2_CID_SATURATION:
		ctrl->value = state->sat;
		break;

	case V4L2_CID_HUE:
		ctrl->value = state->hue;
		break;

	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = state->audio_main_mute;
		break;

	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = state->audio_main_volume;
		break;

	case V4L2_CID_AUDIO_BALANCE:
		ctrl->value = state->audio_main_balance;
		break;

	case V4L2_CID_AUDIO_TREBLE:
		ctrl->value = state->audio_main_treble;
		break;

	case V4L2_CID_AUDIO_BASS:
		ctrl->value = state->audio_main_bass;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static struct v4l2_queryctrl saa717x_qctrl[] = {
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 128,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_CONTRAST,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 64,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_SATURATION,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 64,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_HUE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Hue",
		.minimum       = -128,
		.maximum       = 127,
		.step          = 1,
		.default_value = 0,
		.flags 	       = 0,
	}, {
		.id            = V4L2_CID_AUDIO_VOLUME,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535 / 100,
		.default_value = 58880,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_AUDIO_BALANCE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Balance",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535 / 100,
		.default_value = 32768,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_AUDIO_MUTE,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 1,
		.default_value = 1,
		.flags         = 0,
	}, {
		.id            = V4L2_CID_AUDIO_BASS,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Bass",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535 / 100,
		.default_value = 32768,
	}, {
		.id            = V4L2_CID_AUDIO_TREBLE,
		.type          = V4L2_CTRL_TYPE_INTEGER,
		.name          = "Treble",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535 / 100,
		.default_value = 32768,
	},
};

static int saa717x_s_video_routing(struct v4l2_subdev *sd,
				   u32 input, u32 output, u32 config)
{
	struct saa717x_state *decoder = to_state(sd);
	int is_tuner = input & 0x80;  /* tuner input flag */

	input &= 0x7f;

	v4l2_dbg(1, debug, sd, "decoder set input (%d)\n", input);
	/* inputs from 0-9 are available*/
	/* saa717x have mode0-mode9 but mode5 is reserved. */
	if (input > 9 || input == 5)
		return -EINVAL;

	if (decoder->input != input) {
		int input_line = input;

		decoder->input = input_line;
		v4l2_dbg(1, debug, sd,  "now setting %s input %d\n",
				input_line >= 6 ? "S-Video" : "Composite",
				input_line);

		/* select mode */
		saa717x_write(sd, 0x102,
				(saa717x_read(sd, 0x102) & 0xf0) |
				input_line);

		/* bypass chrominance trap for modes 6..9 */
		saa717x_write(sd, 0x109,
				(saa717x_read(sd, 0x109) & 0x7f) |
				(input_line < 6 ? 0x0 : 0x80));

		/* change audio_mode */
		if (is_tuner) {
			/* tuner */
			set_audio_mode(sd, decoder->tuner_audio_mode);
		} else {
			/* Force to STEREO mode if Composite or
			 * S-Video were chosen */
			set_audio_mode(sd, TUNER_AUDIO_STEREO);
		}
		/* change initialize procedure (Composite/S-Video) */
		if (is_tuner)
			saa717x_write_regs(sd, reg_init_tuner_input);
		else if (input_line >= 6)
			saa717x_write_regs(sd, reg_init_svideo_input);
		else
			saa717x_write_regs(sd, reg_init_composite_input);
	}

	return 0;
}

static int saa717x_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(saa717x_qctrl); i++)
		if (qc->id && qc->id == saa717x_qctrl[i].id) {
			memcpy(qc, &saa717x_qctrl[i], sizeof(*qc));
			return 0;
		}
	return -EINVAL;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int saa717x_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	reg->val = saa717x_read(sd, reg->reg);
	reg->size = 1;
	return 0;
}

static int saa717x_s_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 addr = reg->reg & 0xffff;
	u8 val = reg->val & 0xff;

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	saa717x_write(sd, addr, val);
	return 0;
}
#endif

static int saa717x_s_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
{
	struct v4l2_pix_format *pix;
	int prescale, h_scale, v_scale;

	pix = &fmt->fmt.pix;
	v4l2_dbg(1, debug, sd, "decoder set size\n");

	/* FIXME need better bounds checking here */
	if (pix->width < 1 || pix->width > 1440)
		return -EINVAL;
	if (pix->height < 1 || pix->height > 960)
		return -EINVAL;

	/* scaling setting */
	/* NTSC and interlace only */
	prescale = SAA717X_NTSC_WIDTH / pix->width;
	if (prescale == 0)
		prescale = 1;
	h_scale = 1024 * SAA717X_NTSC_WIDTH / prescale / pix->width;
	/* interlace */
	v_scale = 512 * 2 * SAA717X_NTSC_HEIGHT / pix->height;

	/* Horizontal prescaling etc */
	set_h_prescale(sd, 0, prescale);
	set_h_prescale(sd, 1, prescale);

	/* Horizontal scaling increment */
	/* TASK A */
	saa717x_write(sd, 0x6C, (u8)(h_scale & 0xFF));
	saa717x_write(sd, 0x6D, (u8)((h_scale >> 8) & 0xFF));
	/* TASK B */
	saa717x_write(sd, 0xAC, (u8)(h_scale & 0xFF));
	saa717x_write(sd, 0xAD, (u8)((h_scale >> 8) & 0xFF));

	/* Vertical prescaling etc */
	set_v_scale(sd, 0, v_scale);
	set_v_scale(sd, 1, v_scale);

	/* set video output size */
	/* video number of pixels at output */
	/* TASK A */
	saa717x_write(sd, 0x5C, (u8)(pix->width & 0xFF));
	saa717x_write(sd, 0x5D, (u8)((pix->width >> 8) & 0xFF));
	/* TASK B */
	saa717x_write(sd, 0x9C, (u8)(pix->width & 0xFF));
	saa717x_write(sd, 0x9D, (u8)((pix->width >> 8) & 0xFF));

	/* video number of lines at output */
	/* TASK A */
	saa717x_write(sd, 0x5E, (u8)(pix->height & 0xFF));
	saa717x_write(sd, 0x5F, (u8)((pix->height >> 8) & 0xFF));
	/* TASK B */
	saa717x_write(sd, 0x9E, (u8)(pix->height & 0xFF));
	saa717x_write(sd, 0x9F, (u8)((pix->height >> 8) & 0xFF));
	return 0;
}

static int saa717x_s_radio(struct v4l2_subdev *sd)
{
	struct saa717x_state *decoder = to_state(sd);

	decoder->radio = 1;
	return 0;
}

static int saa717x_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct saa717x_state *decoder = to_state(sd);

	v4l2_dbg(1, debug, sd, "decoder set norm ");
	v4l2_dbg(1, debug, sd, "(not yet implementd)\n");

	decoder->radio = 0;
	decoder->std = std;
	return 0;
}

static int saa717x_s_audio_routing(struct v4l2_subdev *sd,
				   u32 input, u32 output, u32 config)
{
	struct saa717x_state *decoder = to_state(sd);

	if (input < 3) { /* FIXME! --tadachi */
		decoder->audio_input = input;
		v4l2_dbg(1, debug, sd,
				"set decoder audio input to %d\n",
				decoder->audio_input);
		set_audio_regs(sd, decoder);
		return 0;
	}
	return -ERANGE;
}

static int saa717x_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct saa717x_state *decoder = to_state(sd);

	v4l2_dbg(1, debug, sd, "decoder %s output\n",
			enable ? "enable" : "disable");
	decoder->enable = enable;
	saa717x_write(sd, 0x193, enable ? 0xa6 : 0x26);
	return 0;
}

/* change audio mode */
static int saa717x_s_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct saa717x_state *decoder = to_state(sd);
	int audio_mode;
	char *mes[4] = {
		"MONO", "STEREO", "LANG1", "LANG2/SAP"
	};

	audio_mode = TUNER_AUDIO_STEREO;

	switch (vt->audmode) {
		case V4L2_TUNER_MODE_MONO:
			audio_mode = TUNER_AUDIO_MONO;
			break;
		case V4L2_TUNER_MODE_STEREO:
			audio_mode = TUNER_AUDIO_STEREO;
			break;
		case V4L2_TUNER_MODE_LANG2:
			audio_mode = TUNER_AUDIO_LANG2;
			break;
		case V4L2_TUNER_MODE_LANG1:
			audio_mode = TUNER_AUDIO_LANG1;
			break;
	}

	v4l2_dbg(1, debug, sd, "change audio mode to %s\n",
			mes[audio_mode]);
	decoder->tuner_audio_mode = audio_mode;
	/* The registers are not changed here. */
	/* See DECODER_ENABLE_OUTPUT section. */
	set_audio_mode(sd, decoder->tuner_audio_mode);
	return 0;
}

static int saa717x_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct saa717x_state *decoder = to_state(sd);
	int dual_f, stereo_f;

	if (decoder->radio)
		return 0;
	get_inf_dev_status(sd, &dual_f, &stereo_f);

	v4l2_dbg(1, debug, sd, "DETECT==st:%d dual:%d\n",
			stereo_f, dual_f);

	/* mono */
	if ((dual_f == 0) && (stereo_f == 0)) {
		vt->rxsubchans = V4L2_TUNER_SUB_MONO;
		v4l2_dbg(1, debug, sd, "DETECT==MONO\n");
	}

	/* stereo */
	if (stereo_f == 1) {
		if (vt->audmode == V4L2_TUNER_MODE_STEREO ||
				vt->audmode == V4L2_TUNER_MODE_LANG1) {
			vt->rxsubchans = V4L2_TUNER_SUB_STEREO;
			v4l2_dbg(1, debug, sd, "DETECT==ST(ST)\n");
		} else {
			vt->rxsubchans = V4L2_TUNER_SUB_MONO;
			v4l2_dbg(1, debug, sd, "DETECT==ST(MONO)\n");
		}
	}

	/* dual */
	if (dual_f == 1) {
		if (vt->audmode == V4L2_TUNER_MODE_LANG2) {
			vt->rxsubchans = V4L2_TUNER_SUB_LANG2 | V4L2_TUNER_SUB_MONO;
			v4l2_dbg(1, debug, sd, "DETECT==DUAL1\n");
		} else {
			vt->rxsubchans = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_MONO;
			v4l2_dbg(1, debug, sd, "DETECT==DUAL2\n");
		}
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops saa717x_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = saa717x_g_register,
	.s_register = saa717x_s_register,
#endif
	.queryctrl = saa717x_queryctrl,
	.g_ctrl = saa717x_g_ctrl,
	.s_ctrl = saa717x_s_ctrl,
	.s_std = saa717x_s_std,
};

static const struct v4l2_subdev_tuner_ops saa717x_tuner_ops = {
	.g_tuner = saa717x_g_tuner,
	.s_tuner = saa717x_s_tuner,
	.s_radio = saa717x_s_radio,
};

static const struct v4l2_subdev_video_ops saa717x_video_ops = {
	.s_routing = saa717x_s_video_routing,
	.s_fmt = saa717x_s_fmt,
	.s_stream = saa717x_s_stream,
};

static const struct v4l2_subdev_audio_ops saa717x_audio_ops = {
	.s_routing = saa717x_s_audio_routing,
};

static const struct v4l2_subdev_ops saa717x_ops = {
	.core = &saa717x_core_ops,
	.tuner = &saa717x_tuner_ops,
	.audio = &saa717x_audio_ops,
	.video = &saa717x_video_ops,
};

/* ----------------------------------------------------------------------- */


/* i2c implementation */

/* ----------------------------------------------------------------------- */
static int saa717x_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct saa717x_state *decoder;
	struct v4l2_subdev *sd;
	u8 id = 0;
	char *p = "";

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	decoder = kzalloc(sizeof(struct saa717x_state), GFP_KERNEL);
	if (decoder == NULL)
		return -ENOMEM;

	sd = &decoder->sd;
	v4l2_i2c_subdev_init(sd, client, &saa717x_ops);

	if (saa717x_write(sd, 0x5a4, 0xfe) &&
			saa717x_write(sd, 0x5a5, 0x0f) &&
			saa717x_write(sd, 0x5a6, 0x00) &&
			saa717x_write(sd, 0x5a7, 0x01))
		id = saa717x_read(sd, 0x5a0);
	if (id != 0xc2 && id != 0x32 && id != 0xf2 && id != 0x6c) {
		v4l2_dbg(1, debug, sd, "saa717x not found (id=%02x)\n", id);
		kfree(decoder);
		return -ENODEV;
	}
	if (id == 0xc2)
		p = "saa7173";
	else if (id == 0x32)
		p = "saa7174A";
	else if (id == 0x6c)
		p = "saa7174HL";
	else
		p = "saa7171";
	v4l2_info(sd, "%s found @ 0x%x (%s)\n", p,
			client->addr << 1, client->adapter->name);
	decoder->std = V4L2_STD_NTSC;
	decoder->input = -1;
	decoder->enable = 1;

	/* tune these parameters */
	decoder->bright = 0x80;
	decoder->contrast = 0x44;
	decoder->sat = 0x40;
	decoder->hue = 0x00;

	/* FIXME!! */
	decoder->playback = 0;	/* initially capture mode used */
	decoder->audio = 1; /* DECODER_AUDIO_48_KHZ */

	decoder->audio_input = 2; /* FIXME!! */

	decoder->tuner_audio_mode = TUNER_AUDIO_STEREO;
	/* set volume, bass and treble */
	decoder->audio_main_vol_l = 6;
	decoder->audio_main_vol_r = 6;
	decoder->audio_main_bass = 0;
	decoder->audio_main_treble = 0;
	decoder->audio_main_mute = 0;
	decoder->audio_main_balance = 32768;
	/* normalize (24 to -40 (not -84) -> 65535 to 0) */
	decoder->audio_main_volume =
		(decoder->audio_main_vol_r + 41) * 65535 / (24 - (-40));

	v4l2_dbg(1, debug, sd, "writing init values\n");

	/* FIXME!! */
	saa717x_write_regs(sd, reg_init_initialize);
	set_video_output_level_regs(sd, decoder);
	/* set bass,treble to 0db 20041101 K.Ohta */
	decoder->audio_main_bass = 0;
	decoder->audio_main_treble = 0;
	set_audio_regs(sd, decoder);

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(2*HZ);
	return 0;
}

static int saa717x_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id saa717x_id[] = {
	{ "saa717x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa717x_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "saa717x",
	.probe = saa717x_probe,
	.remove = saa717x_remove,
	.id_table = saa717x_id,
};
