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

#include "hdmi_core.h"

void DDC_Init(void)
{
	__inf("DDC_Init\n");

	HDMI_WUINT32(0x500, 0x80000001);
	hdmi_delay_ms(1);

#if 0
	while (HDMI_RUINT32(0x500) & 0x1)
		;
#endif

	/* N = 5,M=1 Fscl= Ftmds/2/10/2^N/(M+1) */
	HDMI_WUINT32(0x528, 0x0d);

	/* ddc address  0x60 */
	//HDMI_WUINT8(0x506, 0x60);

	/* slave address  0xa0 */
	//HDMI_WUINT8(0x504, 0xa0 >> 1);

	/* enable analog sda/scl pad */
	HDMI_WUINT32(0x540, (0 << 12) + (3 << 8));

	//send_ini_sequence();
}

#if 0
void send_ini_sequence()
{
	int i, j;

	set_wbit(HDMI_BASE + 0x524, BIT3);
	for (i = 0; i < 9; i++) {
		for (j = 0; j < 200; j++) //for simulation, delete it
			;
		clr_wbit(HDMI_BASE + 0x524, BIT2);

		for (j = 0; j < 200; j++) //for simulation, delete it
			;
		set_wbit(HDMI_BASE + 0x524, BIT2);
	}

	clr_wbit(HDMI_BASE + 0x524, BIT3);
	clr_wbit(HDMI_BASE + 0x524, BIT1);
}
#endif

__s32 DDC_Read(char cmd, char pointer, char offset, int nbyte, char *pbuf)
{
	__u8 i = 0;
	__u8 n = 0;
	__u8 off = offset;
	__s32 reg_val;
	__u32 begin_ms, end_ms;

	__inf("DDC_Read\n");

	while (nbyte > 0) {
		if (nbyte > 16)
			n = 16;
		else
			n = nbyte;
		nbyte = nbyte - n;

		reg_val = HDMI_RUINT32(0x500);
		reg_val &= 0xfffffeff;
		HDMI_WUINT32(0x500, reg_val); /* set FIFO read */

		HDMI_WUINT32(0x504, (pointer << 24) + (0x60 << 16) +
			     (off << 8) + (0xa0 >> 1));

		reg_val = HDMI_RUINT32(0x510);
		reg_val |= 0x80000000;
		HDMI_WUINT32(0x510, reg_val); /* FIFO address clear */

		HDMI_WUINT32(0x51c, n);	/* nbyte to access */
		HDMI_WUINT32(0x520, cmd); /* set cmd type */

		reg_val = HDMI_RUINT32(0x500);
		reg_val |= 0x40000000;
		HDMI_WUINT32(0x500, reg_val); /* start and cmd */

		off += n;

		begin_ms = (jiffies * 1000) / HZ;
		while (HDMI_RUINT32(0x500) & 0x40000000) {
			end_ms = (jiffies * 1000) / HZ;
			if ((end_ms - begin_ms) > 1000) {
				__wrn("ddc read timeout\n");
				return -1;
			}
		}

		for (i = 0; i < n; i++)
			*pbuf++ = HDMI_RUINT8(0x518);
	}

	return 0;
}

static void
GetEDIDData(__u8 block, __u8 *buf)
{
	__u8 i;
	__u8 *pbuf = buf + 128 * block;
	__u8 offset = (block & 0x01) ? 128 : 0;

	DDC_Read(Explicit_Offset_Address_E_DDC_Read, block >> 1, offset, 128,
		 pbuf);

	__inf("Sink : EDID bank %d:\n", block);

	__inf(" 0   1   2   3   4   5   6   7   8   9   "
	      "A   B   C   D   E   F\n");
	__inf(" ======================================================="
	      "========================================\n");

	for (i = 0; i < 8; i++) {
		__inf(" %2.2x  %2.2x  %2.2x  %2.2x  %2.2x  %2.2x  %2.2x"
		      "  %2.2x  %2.2x  %2.2x  %2.2x  %2.2x  %2.2x  "
		      "%2.2x  %2.2x  %2.2x\n",
		      pbuf[i * 16 + 0], pbuf[i * 16 + 1], pbuf[i * 16 + 2],
		      pbuf[i * 16 + 3], pbuf[i * 16 + 4], pbuf[i * 16 + 5],
		      pbuf[i * 16 + 6], pbuf[i * 16 + 7], pbuf[i * 16 + 8],
		      pbuf[i * 16 + 9], pbuf[i * 16 + 10], pbuf[i * 16 + 11],
		      pbuf[i * 16 + 12], pbuf[i * 16 + 13], pbuf[i * 16 + 14],
		      pbuf[i * 16 + 15]);
	}
	__inf(" ======================================================="
	      "========================================\n");

	return;
}

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
		__inf("EDID block %d checksum error\n", block);
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
		__inf("EDID block0 header error\n");
		return -1;
	}
	return 0;
}

static __s32
EDID_Version_Check(__u8 *pbuf)
{
	__inf("EDID version: %d.%d ", pbuf[0x12], pbuf[0x13]);
	if ((pbuf[0x12] != 0x01) || (pbuf[0x13] != 0x03)) {
		__inf("Unsupport EDID format,EDID parsing exit\n");
		return -1;
	}
	return 0;
}

static __s32
Parse_DTD_Block(__u8 *pbuf)
{
	__u32 pclk, sizex, Hblanking, sizey, Vblanking, Hsync_offset,
	    Hsync_plus, Vsync_offset, Vsync_plus, H_image_size, V_image_size,
	    H_Border, V_Border, pixels_total, frame_rate;
	pclk = ((__u32) pbuf[1] << 8) + pbuf[0];
	sizex = (((__u32) pbuf[4] << 4) & 0x0f00) + pbuf[2];
	Hblanking = (((__u32) pbuf[4] << 8) & 0x0f00) + pbuf[3];
	sizey = (((__u32) pbuf[7] << 4) & 0x0f00) + pbuf[5];
	Vblanking = (((__u32) pbuf[7] << 8) & 0x0f00) + pbuf[6];
	Hsync_offset = (((__u32) pbuf[11] << 2) & 0x0300) + pbuf[8];
	Hsync_plus = (((__u32) pbuf[11] << 4) & 0x0300) + pbuf[9];
	Vsync_offset = (((__u32) pbuf[11] << 2) & 0x0030) + (pbuf[10] >> 4);
	Vsync_plus = (((__u32) pbuf[11] << 4) & 0x0030) + (pbuf[8] & 0x0f);
	H_image_size = (((__u32) pbuf[14] << 4) & 0x0f00) + pbuf[12];
	V_image_size = (((__u32) pbuf[14] << 8) & 0x0f00) + pbuf[13];
	H_Border = pbuf[15];
	V_Border = pbuf[16];

	pixels_total = (sizex + Hblanking) * (sizey + Vblanking);

	if ((pbuf[0] == 0) && (pbuf[1] == 0) && (pbuf[2] == 0))
		return 0;

	if (pixels_total == 0)
		return 0;
	else
		frame_rate = (pclk * 10000) / pixels_total;

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

	__inf("PCLK=%d\tXsize=%d\tYsize=%d\tFrame_rate=%d\n",
	      pclk * 10000, sizex, sizey, frame_rate);

	return 0;
}

static __s32
Parse_VideoData_Block(__u8 *pbuf, __u8 size)
{
	int i = 0;
	while (i < size) {
		Device_Support_VIC[pbuf[i] & 0x7f] = 1;
		if (pbuf[i] & 0x80)
			__inf("Parse_VideoData_Block: VIC %d(native) support\n",
			      pbuf[i] & 0x7f);
		else
			__inf("Parse_VideoData_Block: VIC %d support\n",
			      pbuf[i]);
		i++;
	}
	return 0;
}

static __s32
Parse_AudioData_Block(__u8 *pbuf, __u8 size)
{
	__u8 sum = 0;

	while (sum < size) {
		if ((pbuf[sum] & 0xf8) == 0x08) {
			__inf("Parse_AudioData_Block: max channel=%d\n",
			      (pbuf[sum] & 0x7) + 1);
			__inf("Parse_AudioData_Block: SampleRate code=%x\n",
			      pbuf[sum + 1]);
			__inf("Parse_AudioData_Block: WordLen code=%x\n",
			      pbuf[sum + 2]);
		}
		sum += 3;
	}
	return 0;
}

static __s32
Parse_HDMI_VSDB(__u8 *pbuf, __u8 size)
{
	__u8 index = 8;

	/* check if it's HDMI VSDB */
	if ((pbuf[0] == 0x03) && (pbuf[1] == 0x0c) && (pbuf[2] == 0x00))
		__inf("Find HDMI Vendor Specific DataBlock\n");
	else
		return 0;

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
		__inf("3D_present\n");
	} else {
		return 0;
	}

	if (((pbuf[index] & 0x60) == 1) || ((pbuf[index] & 0x60) == 2))
		__inf("3D_multi_present\n");

	index += (pbuf[index + 1] & 0xe0) + 2;
	if (index > (size + 1))
		return 0;

	__inf("3D_multi_present byte(%2.2x,%2.2x)\n", pbuf[index],
	      pbuf[index + 1]);

	return 0;
}

/*
 * collect the EDID ucdata of segment 0
 *
 * Yergh. Break this up! --libv.
 */
__s32 ParseEDID(void)
{
	__u8 BlockCount;
	__u32 i, offset;

	__inf("ParseEDID\n");

	memset(Device_Support_VIC, 0, sizeof(Device_Support_VIC));
	memset(EDID_Buf, 0, sizeof(EDID_Buf));

	DDC_Init();

	GetEDIDData(0, EDID_Buf);

	if (EDID_CheckSum(0, EDID_Buf) != 0)
		return 0;

	if (EDID_Header_Check(EDID_Buf) != 0)
		return 0;

	if (EDID_Version_Check(EDID_Buf) != 0)
		return 0;

	Parse_DTD_Block(EDID_Buf + 0x36);

	Parse_DTD_Block(EDID_Buf + 0x48);

	BlockCount = EDID_Buf[0x7E];

	if (BlockCount > 0) {
		if (BlockCount > 4)
			BlockCount = 4;

		for (i = 1; i <= BlockCount; i++) {
			GetEDIDData(i, EDID_Buf);
			if (EDID_CheckSum(i, EDID_Buf) != 0)
				return 0;

			if ((EDID_Buf[0x80 * i + 0] == 2)
			    /* && (EDID_Buf[0x80 * i + 1] == 1) */) {

				offset = EDID_Buf[0x80 * i + 2];
				/* deal with reserved data block */
				if (offset > 4)	{
					__u8 bsum = 4;
					while (bsum < offset) {
						__u8 tag = EDID_Buf[0x80 * i + bsum] >> 5;
						__u8 len = EDID_Buf[0x80 * i + bsum] & 0x1f;
						if ((len > 0) && ((bsum + len + 1) > offset)) {
							__inf("len or bsum size error\n");
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
					__inf("no data in reserved block%d\n", i);
				}

				if (offset >= 4) { /* deal with 18-byte timing block */
					while (offset < (0x80 - 18)) {
						Parse_DTD_Block(EDID_Buf + 0x80 * i + offset);
						offset += 18;
					}
				} else {
					__inf("no datail timing in block%d\n", i);
				}
			}

		}
	}

	return 0;

}
