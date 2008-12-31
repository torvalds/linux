/*
 *  cx18 ADEC firmware functions
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include <linux/firmware.h>

#define CX18_AUDIO_ENABLE 0xc72014
#define FWFILE "v4l-cx23418-dig.fw"

int cx18_av_loadfw(struct cx18 *cx)
{
	const struct firmware *fw = NULL;
	u32 size;
	u32 v;
	const u8 *ptr;
	int i;
	int retries1 = 0;

	if (request_firmware(&fw, FWFILE, &cx->dev->dev) != 0) {
		CX18_ERR("unable to open firmware %s\n", FWFILE);
		return -EINVAL;
	}

	/* The firmware load often has byte errors, so allow for several
	   retries, both at byte level and at the firmware load level. */
	while (retries1 < 5) {
		cx18_av_write4_expect(cx, CXADEC_CHIP_CTRL, 0x00010000,
					  0x00008430, 0xffffffff); /* cx25843 */
		cx18_av_write_expect(cx, CXADEC_STD_DET_CTL, 0xf6, 0xf6, 0xff);

		/* Reset the Mako core, Register is alias of CXADEC_CHIP_CTRL */
		cx18_av_write4_expect(cx, 0x8100, 0x00010000,
					  0x00008430, 0xffffffff); /* cx25843 */

		/* Put the 8051 in reset and enable firmware upload */
		cx18_av_write4_noretry(cx, CXADEC_DL_CTL, 0x0F000000);

		ptr = fw->data;
		size = fw->size;

		for (i = 0; i < size; i++) {
			u32 dl_control = 0x0F000000 | i | ((u32)ptr[i] << 16);
			u32 value = 0;
			int retries2;
			int unrec_err = 0;

			for (retries2 = 0; retries2 < CX18_MAX_MMIO_WR_RETRIES;
			     retries2++) {
				cx18_av_write4_noretry(cx, CXADEC_DL_CTL,
						       dl_control);
				udelay(10);
				value = cx18_av_read4(cx, CXADEC_DL_CTL);
				if (value == dl_control)
					break;
				/* Check if we can correct the byte by changing
				   the address.  We can only write the lower
				   address byte of the address. */
				if ((value & 0x3F00) != (dl_control & 0x3F00)) {
					unrec_err = 1;
					break;
				}
			}
			if (unrec_err || retries2 >= CX18_MAX_MMIO_WR_RETRIES)
				break;
		}
		if (i == size)
			break;
		retries1++;
	}
	if (retries1 >= 5) {
		CX18_ERR("unable to load firmware %s\n", FWFILE);
		release_firmware(fw);
		return -EIO;
	}

	cx18_av_write4_expect(cx, CXADEC_DL_CTL,
				0x13000000 | fw->size, 0x13000000, 0x13000000);

	/* Output to the 416 */
	cx18_av_and_or4(cx, CXADEC_PIN_CTRL1, ~0, 0x78000);

	/* Audio input control 1 set to Sony mode */
	/* Audio output input 2 is 0 for slave operation input */
	/* 0xC4000914[5]: 0 = left sample on WS=0, 1 = left sample on WS=1 */
	/* 0xC4000914[7]: 0 = Philips mode, 1 = Sony mode (1st SCK rising edge
	   after WS transition for first bit of audio word. */
	cx18_av_write4(cx, CXADEC_I2S_IN_CTL, 0x000000A0);

	/* Audio output control 1 is set to Sony mode */
	/* Audio output control 2 is set to 1 for master mode */
	/* 0xC4000918[5]: 0 = left sample on WS=0, 1 = left sample on WS=1 */
	/* 0xC4000918[7]: 0 = Philips mode, 1 = Sony mode (1st SCK rising edge
	   after WS transition for first bit of audio word. */
	/* 0xC4000918[8]: 0 = slave operation, 1 = master (SCK_OUT and WS_OUT
	   are generated) */
	cx18_av_write4(cx, CXADEC_I2S_OUT_CTL, 0x000001A0);

	/* set alt I2s master clock to /16 and enable alt divider i2s
	   passthrough */
	cx18_av_write4(cx, CXADEC_PIN_CFG3, 0x5000B687);

	cx18_av_write4_expect(cx, CXADEC_STD_DET_CTL, 0x000000F6, 0x000000F6,
								  0x3F00FFFF);
	/* CxDevWrReg(CXADEC_STD_DET_CTL, 0x000000FF); */

	/* Set bit 0 in register 0x9CC to signify that this is MiniMe. */
	/* Register 0x09CC is defined by the Merlin firmware, and doesn't
	   have a name in the spec. */
	cx18_av_write4(cx, 0x09CC, 1);

	v = cx18_read_reg(cx, CX18_AUDIO_ENABLE);
	/* If bit 11 is 1, clear bit 10 */
	if (v & 0x800)
		cx18_write_reg(cx, v & 0xFFFFFBFF, CX18_AUDIO_ENABLE);

	/* Enable WW auto audio standard detection */
	v = cx18_av_read4(cx, CXADEC_STD_DET_CTL);
	v |= 0xFF;   /* Auto by default */
	v |= 0x400;  /* Stereo by default */
	v |= 0x14000000;
	cx18_av_write4_expect(cx, CXADEC_STD_DET_CTL, v, v, 0x3F00FFFF);

	release_firmware(fw);

	CX18_INFO("loaded %s firmware (%d bytes)\n", FWFILE, size);
	return 0;
}
