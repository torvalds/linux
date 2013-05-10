/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <sound/pcm.h>
#include <linux/fb.h>
#include "hdmi_core.h"
#include "../disp/dev_disp.h"
#include "../disp/sunxi_disp_regs.h"
#include "hdmi_cec.h"


/*
 * ParseEDID()
 * Check EDID check sum and EDID 1.3 extended segment.
 */
static __s32
EDID_CheckSum(__u8 block, __u8 *buf)
{
	__s32 i = 0, CheckSum = 0;
	__u8 *pbuf = buf + 128 * block;

	for (i = 0, CheckSum = 0; i < 128; i++) {
		CheckSum += pbuf[i];
		CheckSum &= 0xFF;
	}

	if (CheckSum != 0) {
		pr_info("EDID block %d checksum error\n", block);
		return -1;
	}
	return 0;
}

static __s32
EDID_Header_Check(__u8 *pbuf)
{
	if (pbuf[0] != 0x00 || pbuf[1] != 0xFF || pbuf[2] != 0xFF ||
	    pbuf[3] != 0xFF || pbuf[4] != 0xFF || pbuf[5] != 0xFF ||
	    pbuf[6] != 0xFF || pbuf[7] != 0x00) {
		pr_info("EDID block0 header error\n");
		return -1;
	}
	return 0;
}

static __s32
EDID_Version_Check(__u8 *pbuf)
{
	pr_info("EDID version: %d.%d\n", pbuf[0x12], pbuf[0x13]);
	if (pbuf[0x12] != 0x01) {
		pr_info("Unsupport EDID format,EDID parsing exit\n");
		return -1;
	}
	if (pbuf[0x13] < 3 && !(pbuf[0x18] & 0x02)) {
		pr_info("EDID revision < 3 and preferred timing feature bit "
			"not set, ignoring EDID info\n");
		return -1;
	}
	return 0;
}

struct pclk_override {
	struct __disp_video_timing video_timing;
	int pclk;
};

struct pclk_override pclk_override[] = {
	/* VIC PCLK  AVI_PR INPUTX INPUTY HT   HBP  HFP  HPSW VT   VBP VFP VPSW I  HS VS   override */
	{ { HDMI_EDID, 146250000, 0, 1680, 1050, 2240, 456, 104, 176, 1089, 36,  3, 6,  0, 0, 1 }, 146000000 },
	{ { 0, }, -1 }
};

static __s32
Parse_DTD_Block(__u8 *pbuf)
{
	__u32 i, dummy, pclk, sizex, Hblanking, sizey, Vblanking, Hsync_offset,
		Hsync_pulsew, Vsync_offset, Vsync_pulsew, H_image_size,
		V_image_size, H_Border, V_Border, pixels_total, frame_rate,
		Hsync, Vsync, HT, VT;
	pclk = (((__u32) pbuf[1] << 8) + pbuf[0]) * 10000;
	sizex = (((__u32) pbuf[4] << 4) & 0x0f00) + pbuf[2];
	Hblanking = (((__u32) pbuf[4] << 8) & 0x0f00) + pbuf[3];
	sizey = (((__u32) pbuf[7] << 4) & 0x0f00) + pbuf[5];
	Vblanking = (((__u32) pbuf[7] << 8) & 0x0f00) + pbuf[6];
	Hsync_offset = (((__u32) pbuf[11] << 2) & 0x0300) + pbuf[8];
	Hsync_pulsew = (((__u32) pbuf[11] << 4) & 0x0300) + pbuf[9];
	Vsync_offset = (((__u32) pbuf[11] << 2) & 0x0030) + (pbuf[10] >> 4);
	Vsync_pulsew = (((__u32) pbuf[11] << 4) & 0x0030) + (pbuf[10] & 0x0f);
	H_image_size = (((__u32) pbuf[14] << 4) & 0x0f00) + pbuf[12];
	V_image_size = (((__u32) pbuf[14] << 8) & 0x0f00) + pbuf[13];
	H_Border = pbuf[15];
	V_Border = pbuf[16];
	Hsync = (pbuf[17] & 0x02) >> 1;
	Vsync = (pbuf[17] & 0x04) >> 2;
	HT = sizex + Hblanking;
	VT = sizey + Vblanking;

	pixels_total = HT * VT;

	if ((pbuf[0] == 0) && (pbuf[1] == 0) && (pbuf[2] == 0))
		return 0;

	if (pixels_total == 0)
		return 0;
	else
		frame_rate = pclk / pixels_total;

	if ((frame_rate == 59) || (frame_rate == 60)) {
		if ((sizex == 720) && (sizey == 240))
			Device_Support_VIC[HDMI1440_480I] = 1;

		if ((sizex == 720) && (sizey == 480))
			Device_Support_VIC[HDMI480P] = 1;

		if ((sizex == 1280) && (sizey == 720))
			Device_Support_VIC[HDMI720P_60] = 1;

		if ((sizex == 1920) && (sizey == 540))
			Device_Support_VIC[HDMI1080I_60] = 1;

		if ((sizex == 1920) && (sizey == 1080))
			Device_Support_VIC[HDMI1080P_60] = 1;

	} else if ((frame_rate == 49) || (frame_rate == 50)) {
		if ((sizex == 720) && (sizey == 288))
			Device_Support_VIC[HDMI1440_576I] = 1;

		if ((sizex == 720) && (sizey == 576))
			Device_Support_VIC[HDMI576P] = 1;

		if ((sizex == 1280) && (sizey == 720))
			Device_Support_VIC[HDMI720P_50] = 1;

		if ((sizex == 1920) && (sizey == 540))
			Device_Support_VIC[HDMI1080I_50] = 1;

		if ((sizex == 1920) && (sizey == 1080))
			Device_Support_VIC[HDMI1080P_50] = 1;

	} else if ((frame_rate == 23) || (frame_rate == 24)) {
		if ((sizex == 1920) && (sizey == 1080))
			Device_Support_VIC[HDMI1080P_24] = 1;
	}

	pr_info("PCLK=%d X %d %d %d %d Y %d %d %d %d fr %d %s%s\n", pclk,
		sizex, sizex + Hsync_offset,
		sizex + Hsync_offset + Hsync_pulsew, HT,
		sizey, sizey + Vsync_offset,
		sizey + Vsync_offset + Vsync_pulsew, VT,
		frame_rate, Hsync ? "P" : "N", Vsync ? "P" : "N");

	/* Pick the first mode with a width which is a multiple of 8 and
	   a supported pixel-clock */
	if (Device_Support_VIC[HDMI_EDID] || (sizex & 7))
		return 0;

	video_timing[video_timing_edid].PCLK = pclk;
	video_timing[video_timing_edid].AVI_PR = 0;
	video_timing[video_timing_edid].INPUTX = sizex;
	video_timing[video_timing_edid].INPUTY = sizey;
	video_timing[video_timing_edid].HT = HT;
	video_timing[video_timing_edid].HBP = Hblanking - Hsync_offset;
	video_timing[video_timing_edid].HFP = Hsync_offset;
	video_timing[video_timing_edid].HPSW = Hsync_pulsew;
	video_timing[video_timing_edid].VT = VT;
	video_timing[video_timing_edid].VBP = Vblanking - Vsync_offset;
	video_timing[video_timing_edid].VFP = Vsync_offset;
	video_timing[video_timing_edid].VPSW = Vsync_pulsew;
	video_timing[video_timing_edid].I = (pbuf[17] & 0x80) >> 7;
	video_timing[video_timing_edid].HSYNC = Hsync;
	video_timing[video_timing_edid].VSYNC = Vsync;

	for (i = 0; pclk_override[i].pclk != -1; i++) {
		if (memcmp(&video_timing[video_timing_edid],
			   &pclk_override[i].video_timing,
			   sizeof(struct __disp_video_timing)) == 0) {
			pr_info("Patching %d pclk to %d\n", pclk,
				pclk_override[i].pclk);
			video_timing[video_timing_edid].PCLK =
				pclk_override[i].pclk;
			break;
		}
	}

	if (disp_get_pll_freq(video_timing[video_timing_edid].PCLK,
			      &dummy, &dummy) != 0)
		return 0;

	pr_info("Using above mode as preferred EDID mode\n");

	if (video_timing[video_timing_edid].I) {
		video_timing[video_timing_edid].INPUTY *= 2;
		video_timing[video_timing_edid].VT *= 2;

		/* Should VT be VT * 2 + 1, or VT * 2 ? */
		frame_rate = (frame_rate + 1) / 2;
		if ((HT * (VT * 2 + 1) * frame_rate) == pclk)
			video_timing[video_timing_edid].VT++;

		pr_info("Interlaced VT %d\n",
			video_timing[video_timing_edid].VT);
	}
	Device_Support_VIC[HDMI_EDID] = 1;

	return 0;
}

static __s32
Parse_VideoData_Block(__u8 *pbuf, __u8 size)
{
	int i = 0;
	while (i < size) {
		Device_Support_VIC[pbuf[i] & 0x7f] = 1;
		pr_info("Parse_VideoData_Block: VIC %d%s support\n",
			pbuf[i] & 0x7f, (pbuf[i] & 0x80) ? " (native)" : "");
		i++;
	}
	return 0;
}

static __s32
Parse_AudioData_Block(__u8 *pbuf, __u8 size)
{
	__u8 sum = 0;
	unsigned long rates = 0;

	while (sum < size) {
		if ((pbuf[sum] & 0xf8) == 0x08) {
			int c = (pbuf[sum] & 0x7) + 1;
			pr_info("Parse_AudioData_Block: max channel=%d\n", c);
			pr_info("Parse_AudioData_Block: SampleRate code=%x\n",
			      pbuf[sum + 1]);
			pr_info("Parse_AudioData_Block: WordLen code=%x\n",
			      pbuf[sum + 2]);
			/*
			 * If >= 2 channels and 16 bit is supported, then
			 * add the supported rates to our bitmap.
			 */
			if ((c >= 2) && (pbuf[sum + 2] & 0x01)) {
				if (pbuf[sum + 1] & 0x01)
					rates |= SNDRV_PCM_RATE_32000;
				if (pbuf[sum + 1] & 0x02)
					rates |= SNDRV_PCM_RATE_44100;
				if (pbuf[sum + 1] & 0x04)
					rates |= SNDRV_PCM_RATE_48000;
				if (pbuf[sum + 1] & 0x08)
					rates |= SNDRV_PCM_RATE_88200;
				if (pbuf[sum + 1] & 0x10)
					rates |= SNDRV_PCM_RATE_96000;
				if (pbuf[sum + 1] & 0x20)
					rates |= SNDRV_PCM_RATE_176400;
				if (pbuf[sum + 1] & 0x40)
					rates |= SNDRV_PCM_RATE_192000;
			}
		}
		sum += 3;
	}
	audio_info.supported_rates |= rates;
	return 0;
}

static __s32
Parse_HDMI_VSDB(__u8 *pbuf, __u8 size)
{
	__u8 index = 8;

	/* check if it's HDMI VSDB */
	if ((pbuf[0] == 0x03) && (pbuf[1] == 0x0c) && (pbuf[2] == 0x00))
		pr_info("Find HDMI Vendor Specific DataBlock\n");
	else
		return 0;

	cec_phy_addr = (((__u32)pbuf[3]) << 8) | pbuf[4];
	__inf("my phy addr is %x\n", cec_phy_addr);
	if (size <= 8)
		return 0;

	if ((pbuf[7] & 0x20) == 0)
		return 0;
	if ((pbuf[7] & 0x40) == 1)
		index = index + 2;
	if ((pbuf[7] & 0x80) == 1)
		index = index + 2;

	/* mandatary format support */
	if (pbuf[index] & 0x80)	{
		Device_Support_VIC[HDMI1080P_24_3D_FP] = 1;
		Device_Support_VIC[HDMI720P_50_3D_FP] = 1;
		Device_Support_VIC[HDMI720P_60_3D_FP] = 1;
		pr_info("3D_present\n");
	} else {
		return 0;
	}

	if (((pbuf[index] & 0x60) == 1) || ((pbuf[index] & 0x60) == 2))
		pr_info("3D_multi_present\n");

	index += (pbuf[index + 1] & 0xe0) + 2;
	if (index > (size + 1))
		return 0;

	__inf("3D_multi_present byte(%2.2x,%2.2x)\n", pbuf[index],
	      pbuf[index + 1]);

	return 0;
}

static __s32 ParseEDID_CEA861_extension_block(__u32 i, __u8 *EDID_Buf)
{
	__u32 offset;
	if (EDID_Buf[0x80 * i + 3] & 0x40) {
		audio_info.supported_rates |=
			SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000;
	}
	offset = EDID_Buf[0x80 * i + 2];
	/* deal with reserved data block */
	if (offset > 4)	{
		__u8 bsum = 4;
		while (bsum < offset) {
			__u8 tag = EDID_Buf[0x80 * i + bsum] >> 5;
			__u8 len = EDID_Buf[0x80 * i + bsum] & 0x1f;
			if ((len > 0) && ((bsum + len + 1) > offset)) {
				pr_info("len or bsum size error\n");
				return 0;
			} else {
				if (tag == 1) { /* ADB */
					Parse_AudioData_Block(EDID_Buf + 0x80 * i + bsum + 1, len);
				} else if (tag == 2) { /* VDB */
					Parse_VideoData_Block(EDID_Buf + 0x80 * i + bsum + 1, len);
				} else if (tag == 3) { /* vendor specific */
					Parse_HDMI_VSDB(EDID_Buf + 0x80 * i + bsum + 1, len);
				}
			}

			bsum += (len + 1);
		}
	} else {
		pr_info("no data in block%d\n", i);
	}

	if (offset >= 4) { /* deal with 18-byte timing block */
		while (offset < (0x80 - 18)) {
			Parse_DTD_Block(EDID_Buf + 0x80 * i + offset);
			offset += 18;
		}
	} else {
		pr_info("no DTD in block%d\n", i);
	}
	return 1;
}

#define DDC_ADDR 0x50
#define EDID_LENGTH 0x80
static int probe_ddc_edid(struct i2c_adapter *adapter,
		int block, unsigned char *buf)
{
	unsigned char start = block * EDID_LENGTH;
	struct i2c_msg msgs[] = {
		{
			.addr	= DDC_ADDR,
			.flags	= 0,
			.len	= 1,
			.buf	= &start,
		}, {
			.addr	= DDC_ADDR,
			.flags	= I2C_M_RD,
			.len	= EDID_LENGTH,
			.buf	= buf + start,
		}
	};

	if (!buf) {
		dev_warn(&adapter->dev, "unable to allocate memory for EDID "
			 "block.\n");
		return -EIO;
	}

	if (i2c_transfer(adapter, msgs, 2) == 2)
		return 0;

	dev_warn(&adapter->dev, "unable to read EDID block.\n");
	return -EIO;
}

/*
 * collect the EDID ucdata of segment 0
 */
__s32 ParseEDID(void)
{
	__u8 BlockCount;
	__u32 i;
	unsigned char *EDID_Buf = kmalloc(EDID_LENGTH*4, GFP_KERNEL);
	if (!EDID_Buf)
		return -ENOMEM;

	pr_info("ParseEDID\n");

	if (video_mode == HDMI_EDID) {
		/* HDMI_DEVICE_SUPPORT_VIC_SIZE - 1 so as to not overwrite
		   the currently in use timings with a new preferred mode! */
		memset(Device_Support_VIC, 0,
		       HDMI_DEVICE_SUPPORT_VIC_SIZE - 1);
	} else {
		memset(Device_Support_VIC, 0, HDMI_DEVICE_SUPPORT_VIC_SIZE);
	}

	if (probe_ddc_edid(&sunxi_hdmi_i2c_adapter, 0, EDID_Buf))
		goto ret;

	if (EDID_CheckSum(0, EDID_Buf) != 0)
		goto ret;

	if (EDID_Header_Check(EDID_Buf) != 0)
		goto ret;

	if (EDID_Version_Check(EDID_Buf) != 0)
		goto ret;

	Parse_DTD_Block(EDID_Buf + 0x36);

	Parse_DTD_Block(EDID_Buf + 0x48);

	BlockCount = EDID_Buf[0x7E] + 1;
	if (BlockCount > 5)
		BlockCount = 5;

	for (i = 1; i < BlockCount; i++) {
		if (probe_ddc_edid(&sunxi_hdmi_i2c_adapter, i, EDID_Buf))
			break;
		if (EDID_CheckSum(i, EDID_Buf) != 0) {
			BlockCount = i;
			break;
		}
	}

	hdmi_edid_received(EDID_Buf, BlockCount);

	for (i = 1; i < BlockCount; i++) {
		if (EDID_Buf[0x80 * i + 0] == 2) {
			if (!ParseEDID_CEA861_extension_block(i, EDID_Buf))
				goto ret;
		}
	}

ret:
	kfree(EDID_Buf);
	return 0;
}
