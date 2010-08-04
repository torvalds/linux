/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/via-core.h>
#include "global.h"

void viafb_get_device_support_state(u32 *support_state)
{
	*support_state = CRT_Device;

	if (viaparinfo->chip_info->tmds_chip_info.tmds_chip_name == VT1632_TMDS)
		*support_state |= DVI_Device;

	if (viaparinfo->chip_info->lvds_chip_info.lvds_chip_name == VT1631_LVDS)
		*support_state |= LCD_Device;
}

void viafb_get_device_connect_state(u32 *connect_state)
{
	bool mobile = false;

	*connect_state = CRT_Device;

	if (viafb_dvi_sense())
		*connect_state |= DVI_Device;

	viafb_lcd_get_mobile_state(&mobile);
	if (mobile)
		*connect_state |= LCD_Device;
}

bool viafb_lcd_get_support_expand_state(u32 xres, u32 yres)
{
	unsigned int support_state = 0;

	switch (viafb_lcd_panel_id) {
	case LCD_PANEL_ID0_640X480:
		if ((xres < 640) && (yres < 480))
			support_state = true;
		break;

	case LCD_PANEL_ID1_800X600:
		if ((xres < 800) && (yres < 600))
			support_state = true;
		break;

	case LCD_PANEL_ID2_1024X768:
		if ((xres < 1024) && (yres < 768))
			support_state = true;
		break;

	case LCD_PANEL_ID3_1280X768:
		if ((xres < 1280) && (yres < 768))
			support_state = true;
		break;

	case LCD_PANEL_ID4_1280X1024:
		if ((xres < 1280) && (yres < 1024))
			support_state = true;
		break;

	case LCD_PANEL_ID5_1400X1050:
		if ((xres < 1400) && (yres < 1050))
			support_state = true;
		break;

	case LCD_PANEL_ID6_1600X1200:
		if ((xres < 1600) && (yres < 1200))
			support_state = true;
		break;

	case LCD_PANEL_ID7_1366X768:
		if ((xres < 1366) && (yres < 768))
			support_state = true;
		break;

	case LCD_PANEL_ID8_1024X600:
		if ((xres < 1024) && (yres < 600))
			support_state = true;
		break;

	case LCD_PANEL_ID9_1280X800:
		if ((xres < 1280) && (yres < 800))
			support_state = true;
		break;

	case LCD_PANEL_IDA_800X480:
		if ((xres < 800) && (yres < 480))
			support_state = true;
		break;

	case LCD_PANEL_IDB_1360X768:
		if ((xres < 1360) && (yres < 768))
			support_state = true;
		break;

	case LCD_PANEL_IDC_480X640:
		if ((xres < 480) && (yres < 640))
			support_state = true;
		break;

	default:
		support_state = false;
		break;
	}

	return support_state;
}

/*====================================================================*/
/*                      Gamma Function Implementation*/
/*====================================================================*/

void viafb_set_gamma_table(int bpp, unsigned int *gamma_table)
{
	int i, sr1a;
	int active_device_amount = 0;
	int device_status = viafb_DeviceStatus;

	for (i = 0; i < sizeof(viafb_DeviceStatus) * 8; i++) {
		if (device_status & 1)
			active_device_amount++;
		device_status >>= 1;
	}

	/* 8 bpp mode can't adjust gamma */
	if (bpp == 8)
		return ;

	/* Enable Gamma */
	switch (viaparinfo->chip_info->gfx_chip_name) {
	case UNICHROME_CLE266:
	case UNICHROME_K400:
		viafb_write_reg_mask(SR16, VIASR, 0x80, BIT7);
		break;

	case UNICHROME_K800:
	case UNICHROME_PM800:
	case UNICHROME_CN700:
	case UNICHROME_CX700:
	case UNICHROME_K8M890:
	case UNICHROME_P4M890:
	case UNICHROME_P4M900:
		viafb_write_reg_mask(CR33, VIACR, 0x80, BIT7);
		break;
	}
	sr1a = (unsigned int)viafb_read_reg(VIASR, SR1A);
	viafb_write_reg_mask(SR1A, VIASR, 0x0, BIT0);

	/* Fill IGA1 Gamma Table */
	outb(0, LUT_INDEX_WRITE);
	for (i = 0; i < 256; i++) {
		outb(gamma_table[i] >> 16, LUT_DATA);
		outb(gamma_table[i] >> 8 & 0xFF, LUT_DATA);
		outb(gamma_table[i] & 0xFF, LUT_DATA);
	}

	/* If adjust Gamma value in SAMM, fill IGA1,
	   IGA2 Gamma table simultanous. */
	/* Switch to IGA2 Gamma Table */
	if ((active_device_amount > 1) &&
		!((viaparinfo->chip_info->gfx_chip_name ==
		UNICHROME_CLE266) &&
		(viaparinfo->chip_info->gfx_chip_revision < 15))) {
		viafb_write_reg_mask(SR1A, VIASR, 0x01, BIT0);
		viafb_write_reg_mask(CR6A, VIACR, 0x02, BIT1);

		/* Fill IGA2 Gamma Table */
		outb(0, LUT_INDEX_WRITE);
		for (i = 0; i < 256; i++) {
			outb(gamma_table[i] >> 16, LUT_DATA);
			outb(gamma_table[i] >> 8 & 0xFF, LUT_DATA);
			outb(gamma_table[i] & 0xFF, LUT_DATA);
		}
	}
	viafb_write_reg(SR1A, VIASR, sr1a);
}

void viafb_get_gamma_table(unsigned int *gamma_table)
{
	unsigned char color_r, color_g, color_b;
	unsigned char sr1a = 0;
	int i;

	/* Enable Gamma */
	switch (viaparinfo->chip_info->gfx_chip_name) {
	case UNICHROME_CLE266:
	case UNICHROME_K400:
		viafb_write_reg_mask(SR16, VIASR, 0x80, BIT7);
		break;

	case UNICHROME_K800:
	case UNICHROME_PM800:
	case UNICHROME_CN700:
	case UNICHROME_CX700:
	case UNICHROME_K8M890:
	case UNICHROME_P4M890:
	case UNICHROME_P4M900:
		viafb_write_reg_mask(CR33, VIACR, 0x80, BIT7);
		break;
	}
	sr1a = viafb_read_reg(VIASR, SR1A);
	viafb_write_reg_mask(SR1A, VIASR, 0x0, BIT0);

	/* Reading gamma table to get color value */
	outb(0, LUT_INDEX_READ);
	for (i = 0; i < 256; i++) {
		color_r = inb(LUT_DATA);
		color_g = inb(LUT_DATA);
		color_b = inb(LUT_DATA);
		gamma_table[i] =
		    ((((u32) color_r) << 16) |
		     (((u16) color_g) << 8)) | color_b;
	}
	viafb_write_reg(SR1A, VIASR, sr1a);
}

void viafb_get_gamma_support_state(int bpp, unsigned int *support_state)
{
	if (bpp == 8)
		*support_state = None_Device;
	else
		*support_state = CRT_Device | DVI_Device | LCD_Device;
}
