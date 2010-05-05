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
#include <linux/via_i2c.h>
#include "global.h"

u8 viafb_gpio_i2c_read_lvds(struct lvds_setting_information
	*plvds_setting_info, struct lvds_chip_information *plvds_chip_info,
	u8 index)
{
	u8 data;

	viafb_i2c_readbyte(plvds_chip_info->i2c_port,
			   plvds_chip_info->lvds_chip_slave_addr, index, &data);
	return data;
}

void viafb_gpio_i2c_write_mask_lvds(struct lvds_setting_information
			      *plvds_setting_info, struct lvds_chip_information
			      *plvds_chip_info, struct IODATA io_data)
{
	int index, data;

	index = io_data.Index;
	data = viafb_gpio_i2c_read_lvds(plvds_setting_info, plvds_chip_info,
		index);
	data = (data & (~io_data.Mask)) | io_data.Data;

	viafb_i2c_writebyte(plvds_chip_info->i2c_port,
			    plvds_chip_info->lvds_chip_slave_addr, index, data);
}

void viafb_init_lvds_vt1636(struct lvds_setting_information
	*plvds_setting_info, struct lvds_chip_information *plvds_chip_info)
{
	int reg_num, i;

	/* Common settings: */
	reg_num = ARRAY_SIZE(COMMON_INIT_TBL_VT1636);

	for (i = 0; i < reg_num; i++) {
		viafb_gpio_i2c_write_mask_lvds(plvds_setting_info,
					 plvds_chip_info,
					 COMMON_INIT_TBL_VT1636[i]);
	}

	/* Input Data Mode Select */
	if (plvds_setting_info->device_lcd_dualedge) {
		viafb_gpio_i2c_write_mask_lvds(plvds_setting_info,
					 plvds_chip_info,
					 DUAL_CHANNEL_ENABLE_TBL_VT1636[0]);
	} else {
		viafb_gpio_i2c_write_mask_lvds(plvds_setting_info,
					 plvds_chip_info,
					 SINGLE_CHANNEL_ENABLE_TBL_VT1636[0]);
	}

	if (plvds_setting_info->LCDDithering) {
		viafb_gpio_i2c_write_mask_lvds(plvds_setting_info,
					 plvds_chip_info,
					 DITHERING_ENABLE_TBL_VT1636[0]);
	} else {
		viafb_gpio_i2c_write_mask_lvds(plvds_setting_info,
					 plvds_chip_info,
					 DITHERING_DISABLE_TBL_VT1636[0]);
	}
}

void viafb_enable_lvds_vt1636(struct lvds_setting_information
			*plvds_setting_info,
			struct lvds_chip_information *plvds_chip_info)
{

	viafb_gpio_i2c_write_mask_lvds(plvds_setting_info, plvds_chip_info,
				 VDD_ON_TBL_VT1636[0]);

	/* Pad on: */
	switch (plvds_chip_info->output_interface) {
	case INTERFACE_DVP0:
		{
			viafb_write_reg_mask(SR1E, VIASR, 0xC0, 0xC0);
			break;
		}

	case INTERFACE_DVP1:
		{
			viafb_write_reg_mask(SR1E, VIASR, 0x30, 0x30);
			break;
		}

	case INTERFACE_DFP_LOW:
		{
			viafb_write_reg_mask(SR2A, VIASR, 0x03, 0x03);
			break;
		}

	case INTERFACE_DFP_HIGH:
		{
			viafb_write_reg_mask(SR2A, VIASR, 0x03, 0x0C);
			break;
		}

	}
}

void viafb_disable_lvds_vt1636(struct lvds_setting_information
			 *plvds_setting_info,
			 struct lvds_chip_information *plvds_chip_info)
{

	viafb_gpio_i2c_write_mask_lvds(plvds_setting_info, plvds_chip_info,
				 VDD_OFF_TBL_VT1636[0]);

	/* Pad off: */
	switch (plvds_chip_info->output_interface) {
	case INTERFACE_DVP0:
		{
			viafb_write_reg_mask(SR1E, VIASR, 0x00, 0xC0);
			break;
		}

	case INTERFACE_DVP1:
		{
			viafb_write_reg_mask(SR1E, VIASR, 0x00, 0x30);
			break;
		}

	case INTERFACE_DFP_LOW:
		{
			viafb_write_reg_mask(SR2A, VIASR, 0x00, 0x03);
			break;
		}

	case INTERFACE_DFP_HIGH:
		{
			viafb_write_reg_mask(SR2A, VIASR, 0x00, 0x0C);
			break;
		}

	}
}

bool viafb_lvds_identify_vt1636(u8 i2c_adapter)
{
	u8 Buffer[2];

	DEBUG_MSG(KERN_INFO "viafb_lvds_identify_vt1636.\n");

	/* Sense VT1636 LVDS Transmiter */
	viaparinfo->chip_info->lvds_chip_info.lvds_chip_slave_addr =
		VT1636_LVDS_I2C_ADDR;

	/* Check vendor ID first: */
	if (viafb_i2c_readbyte(i2c_adapter, VT1636_LVDS_I2C_ADDR,
					0x00, &Buffer[0]))
		return false;
	viafb_i2c_readbyte(i2c_adapter, VT1636_LVDS_I2C_ADDR, 0x01, &Buffer[1]);

	if (!((Buffer[0] == 0x06) && (Buffer[1] == 0x11)))
		return false;

	/* Check Chip ID: */
	viafb_i2c_readbyte(i2c_adapter, VT1636_LVDS_I2C_ADDR, 0x02, &Buffer[0]);
	viafb_i2c_readbyte(i2c_adapter, VT1636_LVDS_I2C_ADDR, 0x03, &Buffer[1]);
	if ((Buffer[0] == 0x45) && (Buffer[1] == 0x33)) {
		viaparinfo->chip_info->lvds_chip_info.lvds_chip_name =
			VT1636_LVDS;
		return true;
	}

	return false;
}

static int get_clk_range_index(u32 Clk)
{
	if (Clk < DPA_CLK_30M)
		return DPA_CLK_RANGE_30M;
	else if (Clk < DPA_CLK_50M)
		return DPA_CLK_RANGE_30_50M;
	else if (Clk < DPA_CLK_70M)
		return DPA_CLK_RANGE_50_70M;
	else if (Clk < DPA_CLK_100M)
		return DPA_CLK_RANGE_70_100M;
	else if (Clk < DPA_CLK_150M)
		return DPA_CLK_RANGE_100_150M;
	else
		return DPA_CLK_RANGE_150M;
}

static int get_lvds_dpa_setting_index(int panel_size_id,
			     struct VT1636_DPA_SETTING *p_vt1636_dpasetting_tbl,
			       int tbl_size)
{
	int i;

	for (i = 0; i < tbl_size; i++) {
		if (panel_size_id == p_vt1636_dpasetting_tbl->PanelSizeID)
			return i;

		p_vt1636_dpasetting_tbl++;
	}

	return 0;
}

static void set_dpa_vt1636(struct lvds_setting_information
	*plvds_setting_info, struct lvds_chip_information *plvds_chip_info,
		    struct VT1636_DPA_SETTING *p_vt1636_dpa_setting)
{
	struct IODATA io_data;

	io_data.Index = 0x09;
	io_data.Mask = 0x1F;
	io_data.Data = p_vt1636_dpa_setting->CLK_SEL_ST1;
	viafb_gpio_i2c_write_mask_lvds(plvds_setting_info,
		plvds_chip_info, io_data);

	io_data.Index = 0x08;
	io_data.Mask = 0x0F;
	io_data.Data = p_vt1636_dpa_setting->CLK_SEL_ST2;
	viafb_gpio_i2c_write_mask_lvds(plvds_setting_info, plvds_chip_info,
		io_data);
}

void viafb_vt1636_patch_skew_on_vt3324(
	struct lvds_setting_information *plvds_setting_info,
	struct lvds_chip_information *plvds_chip_info)
{
	int index, size;

	DEBUG_MSG(KERN_INFO "viafb_vt1636_patch_skew_on_vt3324.\n");

	/* Graphics DPA settings: */
	index = get_clk_range_index(plvds_setting_info->vclk);
	viafb_set_dpa_gfx(plvds_chip_info->output_interface,
		    &GFX_DPA_SETTING_TBL_VT3324[index]);

	/* LVDS Transmitter DPA settings: */
	size = ARRAY_SIZE(VT1636_DPA_SETTING_TBL_VT3324);
	index =
	    get_lvds_dpa_setting_index(plvds_setting_info->lcd_panel_id,
				       VT1636_DPA_SETTING_TBL_VT3324, size);
	set_dpa_vt1636(plvds_setting_info, plvds_chip_info,
		       &VT1636_DPA_SETTING_TBL_VT3324[index]);
}

void viafb_vt1636_patch_skew_on_vt3327(
	struct lvds_setting_information *plvds_setting_info,
	struct lvds_chip_information *plvds_chip_info)
{
	int index, size;

	DEBUG_MSG(KERN_INFO "viafb_vt1636_patch_skew_on_vt3327.\n");

	/* Graphics DPA settings: */
	index = get_clk_range_index(plvds_setting_info->vclk);
	viafb_set_dpa_gfx(plvds_chip_info->output_interface,
		    &GFX_DPA_SETTING_TBL_VT3327[index]);

	/* LVDS Transmitter DPA settings: */
	size = ARRAY_SIZE(VT1636_DPA_SETTING_TBL_VT3327);
	index =
	    get_lvds_dpa_setting_index(plvds_setting_info->lcd_panel_id,
				       VT1636_DPA_SETTING_TBL_VT3327, size);
	set_dpa_vt1636(plvds_setting_info, plvds_chip_info,
		       &VT1636_DPA_SETTING_TBL_VT3327[index]);
}

void viafb_vt1636_patch_skew_on_vt3364(
	struct lvds_setting_information *plvds_setting_info,
	struct lvds_chip_information *plvds_chip_info)
{
	int index;

	DEBUG_MSG(KERN_INFO "viafb_vt1636_patch_skew_on_vt3364.\n");

	/* Graphics DPA settings: */
	index = get_clk_range_index(plvds_setting_info->vclk);
	viafb_set_dpa_gfx(plvds_chip_info->output_interface,
		    &GFX_DPA_SETTING_TBL_VT3364[index]);
}
