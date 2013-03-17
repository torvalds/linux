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

/*
 * HDMI
 */
#define HDMI_CTRL			(hdmi_base + 0x004)
#define HDMI_INT_CTRL			(hdmi_base + 0x008)
#define HDMI_HPD			(hdmi_base + 0x00c) /* INT_STATUS? */
#define HDMI_VIDEO_CTRL			(hdmi_base + 0x010)
#define HDMI_VIDEO_H			(hdmi_base + 0x014)
#define HDMI_VIDEO_V			(hdmi_base + 0x016)
#define HDMI_VIDEO_HBP			(hdmi_base + 0x018)
#define HDMI_VIDEO_VBP			(hdmi_base + 0x01a)
#define HDMI_VIDEO_HFP			(hdmi_base + 0x01c)
#define HDMI_VIDEO_VFP			(hdmi_base + 0x01e)
#define HDMI_VIDEO_HSPW			(hdmi_base + 0x020)
#define HDMI_VIDEO_VPSW			(hdmi_base + 0x022)
#define HDMI_VIDEO_POLARITY		(hdmi_base + 0x024)
#define HDMI_TX_CLOCK			(hdmi_base + 0x026)
#define HDMI_AUDIO_CTRL			(hdmi_base + 0x040)
#define HDMI_AUDIO_UNKNOWN_0		(hdmi_base + 0x044) /* Unknown */
#define HDMI_AUDIO_LAYOUT		(hdmi_base + 0x048)
#define HDMI_AUDIO_UNKNOWN_1		(hdmi_base + 0x04c) /* Unknown */
#define HDMI_AUDIO_CTS			(hdmi_base + 0x050)
#define HDMI_AUDIO_ACR_N		(hdmi_base + 0x054)
#define HDMI_AUDIO_CH_STATUS0		(hdmi_base + 0x058)
#define HDMI_AUDIO_CH_STATUS1		(hdmi_base + 0x05c)
#define HDMI_AVI_INFOFRAME		(hdmi_base + 0x080)
#define HDMI_AUDIO_INFOFRAME		(hdmi_base + 0x0a0)
#define HDMI_QCP_PACKET			(hdmi_base + 0x0e0)
#define HDMI_TX_DRIVER			(hdmi_base + 0x200)
#define HDMI_CEC			(hdmi_base + 0x214)
#define HDMI_VENDOR_INFOFRAME		(hdmi_base + 0x240)
#define HDMI_PACKET_CONFIG		(hdmi_base + 0x2f0)
#define HDMI_UNKNOWN			(hdmi_base + 0x300) /* Unknown */

#define HDMI_I2C_GENERAL		(hdmi_base + 0x500)
#define HDMI_I2C_ADDR			(hdmi_base + 0x504)
#define HDMI_I2C_STATUS			(hdmi_base + 0x50C)
#define HDMI_I2C_GENERAL_2		(hdmi_base + 0x510)
#define HDMI_I2C_DATA			(hdmi_base + 0x518)
#define HDMI_I2C_DATA_LENGTH		(hdmi_base + 0x51c)
#define HDMI_I2C_CMD			(hdmi_base + 0x520)
#define HDMI_I2C_UNKNOWN_0		(hdmi_base + 0x524) /* Unknown */
#define HDMI_I2C_CLK			(hdmi_base + 0x528)
#define HDMI_I2C_LINE_CTRL		(hdmi_base + 0x540)
#define HDMI_I2C_UNKNOWN_1		(hdmi_base + 0x5f0) /* Unknown */

