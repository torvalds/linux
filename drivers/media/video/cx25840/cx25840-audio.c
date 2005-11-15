/* cx25840 audio functions
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


#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <media/audiochip.h>
#include <media/v4l2-common.h>

#include "cx25840.h"

inline static int set_audclk_freq(struct i2c_client *client,
				 enum v4l2_audio_clock_freq freq)
{
	struct cx25840_state *state = i2c_get_clientdata(client);

	/* assert soft reset */
	cx25840_and_or(client, 0x810, ~0x1, 0x01);

	/* common for all inputs and rates */
	/* SA_MCLK_SEL=1, SA_MCLK_DIV=0x10 */
	cx25840_write(client, 0x127, 0x50);

	switch (state->audio_input) {
	case AUDIO_TUNER:
		switch (freq) {
		case V4L2_AUDCLK_32_KHZ:
			/* VID_PLL and AUX_PLL */
			cx25840_write4(client, 0x108, 0x0f040610);

			/* AUX_PLL_FRAC */
			cx25840_write4(client, 0x110, 0xee39bb01);

			/* src3/4/6_ctl = 0x0801f77f */
			cx25840_write4(client, 0x900, 0x7ff70108);
			cx25840_write4(client, 0x904, 0x7ff70108);
			cx25840_write4(client, 0x90c, 0x7ff70108);
			break;

		case V4L2_AUDCLK_441_KHZ:
			/* VID_PLL and AUX_PLL */
			cx25840_write4(client, 0x108, 0x0f040910);

			/* AUX_PLL_FRAC */
			cx25840_write4(client, 0x110, 0xd66bec00);

			/* src3/4/6_ctl = 0x08016d59 */
			cx25840_write4(client, 0x900, 0x596d0108);
			cx25840_write4(client, 0x904, 0x596d0108);
			cx25840_write4(client, 0x90c, 0x596d0108);
			break;

		case V4L2_AUDCLK_48_KHZ:
			/* VID_PLL and AUX_PLL */
			cx25840_write4(client, 0x108, 0x0f040a10);

			/* AUX_PLL_FRAC */
			cx25840_write4(client, 0x110, 0xe5d69800);

			/* src3/4/6_ctl = 0x08014faa */
			cx25840_write4(client, 0x900, 0xaa4f0108);
			cx25840_write4(client, 0x904, 0xaa4f0108);
			cx25840_write4(client, 0x90c, 0xaa4f0108);
			break;
		}
		break;

	case AUDIO_EXTERN_1:
	case AUDIO_EXTERN_2:
	case AUDIO_INTERN:
	case AUDIO_RADIO:
		switch (freq) {
		case V4L2_AUDCLK_32_KHZ:
			/* VID_PLL and AUX_PLL */
			cx25840_write4(client, 0x108, 0x0f04081e);

			/* AUX_PLL_FRAC */
			cx25840_write4(client, 0x110, 0x69082a01);

			/* src1_ctl = 0x08010000 */
			cx25840_write4(client, 0x8f8, 0x00000108);

			/* src3/4/6_ctl = 0x08020000 */
			cx25840_write4(client, 0x900, 0x00000208);
			cx25840_write4(client, 0x904, 0x00000208);
			cx25840_write4(client, 0x90c, 0x00000208);

			/* SA_MCLK_SEL=1, SA_MCLK_DIV=0x14 */
			cx25840_write(client, 0x127, 0x54);
			break;

		case V4L2_AUDCLK_441_KHZ:
			/* VID_PLL and AUX_PLL */
			cx25840_write4(client, 0x108, 0x0f040918);

			/* AUX_PLL_FRAC */
			cx25840_write4(client, 0x110, 0xd66bec00);

			/* src1_ctl = 0x08010000 */
			cx25840_write4(client, 0x8f8, 0xcd600108);

			/* src3/4/6_ctl = 0x08020000 */
			cx25840_write4(client, 0x900, 0x85730108);
			cx25840_write4(client, 0x904, 0x85730108);
			cx25840_write4(client, 0x90c, 0x85730108);
			break;

		case V4L2_AUDCLK_48_KHZ:
			/* VID_PLL and AUX_PLL */
			cx25840_write4(client, 0x108, 0x0f040a18);

			/* AUX_PLL_FRAC */
			cx25840_write4(client, 0x110, 0xe5d69800);

			/* src1_ctl = 0x08010000 */
			cx25840_write4(client, 0x8f8, 0x00800108);

			/* src3/4/6_ctl = 0x08020000 */
			cx25840_write4(client, 0x900, 0x55550108);
			cx25840_write4(client, 0x904, 0x55550108);
			cx25840_write4(client, 0x90c, 0x55550108);
			break;
		}
		break;
	}

	/* deassert soft reset */
	cx25840_and_or(client, 0x810, ~0x1, 0x00);

	state->audclk_freq = freq;

	return 0;
}

static int set_input(struct i2c_client *client, int audio_input)
{
	struct cx25840_state *state = i2c_get_clientdata(client);

	cx25840_dbg("set audio input (%d)\n", audio_input);

	/* stop microcontroller */
	cx25840_and_or(client, 0x803, ~0x10, 0);

	/* Mute everything to prevent the PFFT! */
	cx25840_write(client, 0x8d3, 0x1f);

	switch (audio_input) {
	case AUDIO_TUNER:
		/* Set Path1 to Analog Demod Main Channel */
		cx25840_write4(client, 0x8d0, 0x7038061f);

		/* When the microcontroller detects the
		 * audio format, it will unmute the lines */
		cx25840_and_or(client, 0x803, ~0x10, 0x10);
		break;

	case AUDIO_EXTERN_1:
	case AUDIO_EXTERN_2:
	case AUDIO_INTERN:
	case AUDIO_RADIO:
		/* Set Path1 to Serial Audio Input */
		cx25840_write4(client, 0x8d0, 0x12100101);

		/* The microcontroller should not be started for the
		 * non-tuner inputs: autodetection is specific for
		 * TV audio. */
		break;

	default:
		cx25840_dbg("Invalid audio input selection %d\n", audio_input);
		return -EINVAL;
	}

	state->audio_input = audio_input;

	return set_audclk_freq(client, state->audclk_freq);
}

inline static int get_volume(struct i2c_client *client)
{
	/* Volume runs +18dB to -96dB in 1/2dB steps
	 * change to fit the msp3400 -114dB to +12dB range */

	/* check PATH1_VOLUME */
	int vol = 228 - cx25840_read(client, 0x8d4);
	vol = (vol / 2) + 23;
	return vol << 9;
}

inline static void set_volume(struct i2c_client *client, int volume)
{
	/* First convert the volume to msp3400 values (0-127) */
	int vol = volume >> 9;
	/* now scale it up to cx25840 values
	 * -114dB to -96dB maps to 0
	 * this should be 19, but in my testing that was 4dB too loud */
	if (vol <= 23) {
		vol = 0;
	} else {
		vol -= 23;
	}

	/* PATH1_VOLUME */
	cx25840_write(client, 0x8d4, 228 - (vol * 2));
}

inline static int get_bass(struct i2c_client *client)
{
	/* bass is 49 steps +12dB to -12dB */

	/* check PATH1_EQ_BASS_VOL */
	int bass = cx25840_read(client, 0x8d9) & 0x3f;
	bass = (((48 - bass) * 0xffff) + 47) / 48;
	return bass;
}

inline static void set_bass(struct i2c_client *client, int bass)
{
	/* PATH1_EQ_BASS_VOL */
	cx25840_and_or(client, 0x8d9, ~0x3f, 48 - (bass * 48 / 0xffff));
}

inline static int get_treble(struct i2c_client *client)
{
	/* treble is 49 steps +12dB to -12dB */

	/* check PATH1_EQ_TREBLE_VOL */
	int treble = cx25840_read(client, 0x8db) & 0x3f;
	treble = (((48 - treble) * 0xffff) + 47) / 48;
	return treble;
}

inline static void set_treble(struct i2c_client *client, int treble)
{
	/* PATH1_EQ_TREBLE_VOL */
	cx25840_and_or(client, 0x8db, ~0x3f, 48 - (treble * 48 / 0xffff));
}

inline static int get_balance(struct i2c_client *client)
{
	/* balance is 7 bit, 0 to -96dB */

	/* check PATH1_BAL_LEVEL */
	int balance = cx25840_read(client, 0x8d5) & 0x7f;
	/* check PATH1_BAL_LEFT */
	if ((cx25840_read(client, 0x8d5) & 0x80) == 0)
		balance = 0x80 - balance;
	else
		balance = 0x80 + balance;
	return balance << 8;
}

inline static void set_balance(struct i2c_client *client, int balance)
{
	int bal = balance >> 8;
	if (bal > 0x80) {
		/* PATH1_BAL_LEFT */
		cx25840_and_or(client, 0x8d5, 0x7f, 0x80);
		/* PATH1_BAL_LEVEL */
		cx25840_and_or(client, 0x8d5, ~0x7f, bal & 0x7f);
	} else {
		/* PATH1_BAL_LEFT */
		cx25840_and_or(client, 0x8d5, 0x7f, 0x00);
		/* PATH1_BAL_LEVEL */
		cx25840_and_or(client, 0x8d5, ~0x7f, 0x80 - bal);
	}
}

inline static int get_mute(struct i2c_client *client)
{
	/* check SRC1_MUTE_EN */
	return cx25840_read(client, 0x8d3) & 0x2 ? 1 : 0;
}

inline static void set_mute(struct i2c_client *client, int mute)
{
	struct cx25840_state *state = i2c_get_clientdata(client);

	if (state->audio_input == AUDIO_TUNER) {
		/* Must turn off microcontroller in order to mute sound.
		 * Not sure if this is the best method, but it does work.
		 * If the microcontroller is running, then it will undo any
		 * changes to the mute register. */
		if (mute) {
			/* disable microcontroller */
			cx25840_and_or(client, 0x803, ~0x10, 0x00);
			cx25840_write(client, 0x8d3, 0x1f);
		} else {
			/* enable microcontroller */
			cx25840_and_or(client, 0x803, ~0x10, 0x10);
		}
	} else {
		/* SRC1_MUTE_EN */
		cx25840_and_or(client, 0x8d3, ~0x2, mute ? 0x02 : 0x00);
	}
}

int cx25840_audio(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct v4l2_control *ctrl = arg;

	switch (cmd) {
	case AUDC_SET_INPUT:
		return set_input(client, *(int *)arg);
	case VIDIOC_INT_AUDIO_CLOCK_FREQ:
		return set_audclk_freq(client, *(enum v4l2_audio_clock_freq *)arg);
	case VIDIOC_G_CTRL:
		switch (ctrl->id) {
		case V4L2_CID_AUDIO_VOLUME:
			ctrl->value = get_volume(client);
			break;
		case V4L2_CID_AUDIO_BASS:
			ctrl->value = get_bass(client);
			break;
		case V4L2_CID_AUDIO_TREBLE:
			ctrl->value = get_treble(client);
			break;
		case V4L2_CID_AUDIO_BALANCE:
			ctrl->value = get_balance(client);
			break;
		case V4L2_CID_AUDIO_MUTE:
			ctrl->value = get_mute(client);
			break;
		default:
			return -EINVAL;
		}
		break;
	case VIDIOC_S_CTRL:
		switch (ctrl->id) {
		case V4L2_CID_AUDIO_VOLUME:
			set_volume(client, ctrl->value);
			break;
		case V4L2_CID_AUDIO_BASS:
			set_bass(client, ctrl->value);
			break;
		case V4L2_CID_AUDIO_TREBLE:
			set_treble(client, ctrl->value);
			break;
		case V4L2_CID_AUDIO_BALANCE:
			set_balance(client, ctrl->value);
			break;
		case V4L2_CID_AUDIO_MUTE:
			set_mute(client, ctrl->value);
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
