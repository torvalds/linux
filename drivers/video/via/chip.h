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
#ifndef __CHIP_H__
#define __CHIP_H__

#include "global.h"

/***************************************/
/* Definition Graphic Chip Information */
/***************************************/

#define     PCI_VIA_VENDOR_ID       0x1106

/* Define VIA Graphic Chip Name */
#define     UNICHROME_CLE266        1
#define     UNICHROME_CLE266_DID    0x3122
#define     CLE266_REVISION_AX      0x0A
#define     CLE266_REVISION_CX      0x0C

#define     UNICHROME_K400          2
#define     UNICHROME_K400_DID      0x7205

#define     UNICHROME_K800          3
#define     UNICHROME_K800_DID      0x3108

#define     UNICHROME_PM800         4
#define     UNICHROME_PM800_DID     0x3118

#define     UNICHROME_CN700         5
#define     UNICHROME_CN700_DID     0x3344

#define     UNICHROME_CX700         6
#define     UNICHROME_CX700_DID     0x3157
#define     CX700_REVISION_700      0x0
#define     CX700_REVISION_700M     0x1
#define     CX700_REVISION_700M2    0x2

#define     UNICHROME_CN750         7
#define     UNICHROME_CN750_DID     0x3225

#define     UNICHROME_K8M890        8
#define     UNICHROME_K8M890_DID    0x3230

#define     UNICHROME_P4M890        9
#define     UNICHROME_P4M890_DID    0x3343

#define     UNICHROME_P4M900        10
#define     UNICHROME_P4M900_DID    0x3371

#define     UNICHROME_VX800         11
#define     UNICHROME_VX800_DID     0x1122

#define     UNICHROME_VX855         12
#define     UNICHROME_VX855_DID     0x5122

/**************************************************/
/* Definition TMDS Trasmitter Information         */
/**************************************************/

/* Definition TMDS Trasmitter Index */
#define     NON_TMDS_TRANSMITTER    0x00
#define     VT1632_TMDS             0x01
#define     INTEGRATED_TMDS         0x42

/* Definition TMDS Trasmitter I2C Slave Address */
#define     VT1632_TMDS_I2C_ADDR    0x10

/**************************************************/
/* Definition LVDS Trasmitter Information         */
/**************************************************/

/* Definition LVDS Trasmitter Index */
#define     NON_LVDS_TRANSMITTER    0x00
#define     VT1631_LVDS             0x01
#define     VT1636_LVDS             0x0E
#define     INTEGRATED_LVDS         0x41

/* Definition Digital Transmitter Mode */
#define     TX_DATA_12_BITS         0x01
#define     TX_DATA_24_BITS         0x02
#define     TX_DATA_DDR_MODE        0x04
#define     TX_DATA_SDR_MODE        0x08

/* Definition LVDS Trasmitter I2C Slave Address */
#define     VT1631_LVDS_I2C_ADDR    0x70
#define     VT3271_LVDS_I2C_ADDR    0x80
#define     VT1636_LVDS_I2C_ADDR    0x80

struct tmds_chip_information {
	int tmds_chip_name;
	int tmds_chip_slave_addr;
	int dvi_panel_id;
	int data_mode;
	int output_interface;
	int i2c_port;
	int device_type;
};

struct lvds_chip_information {
	int lvds_chip_name;
	int lvds_chip_slave_addr;
	int data_mode;
	int output_interface;
	int i2c_port;
};

struct chip_information {
	int gfx_chip_name;
	int gfx_chip_revision;
	struct tmds_chip_information tmds_chip_info;
	struct lvds_chip_information lvds_chip_info;
	struct lvds_chip_information lvds_chip_info2;
};

struct crt_setting_information {
	int iga_path;
	int h_active;
	int v_active;
	int bpp;
	int refresh_rate;
};

struct tmds_setting_information {
	int iga_path;
	int h_active;
	int v_active;
	int bpp;
	int refresh_rate;
	int get_dvi_size_method;
	int max_pixel_clock;
	int dvi_panel_size;
	int dvi_panel_hres;
	int dvi_panel_vres;
	int native_size;
};

struct lvds_setting_information {
	int iga_path;
	int h_active;
	int v_active;
	int bpp;
	int refresh_rate;
	int get_lcd_size_method;
	int lcd_panel_id;
	int lcd_panel_size;
	int lcd_panel_hres;
	int lcd_panel_vres;
	int display_method;
	int device_lcd_dualedge;
	int LCDDithering;
	int lcd_mode;
	u32 vclk;		/*panel mode clock value */
};

struct GFX_DPA_SETTING {
	int ClkRangeIndex;
	u8 DVP0;		/* CR96[3:0] */
	u8 DVP0DataDri_S1;	/* SR2A[5]   */
	u8 DVP0DataDri_S;	/* SR1B[1]   */
	u8 DVP0ClockDri_S1;	/* SR2A[4]   */
	u8 DVP0ClockDri_S;	/* SR1E[2]   */
	u8 DVP1;		/* CR9B[3:0] */
	u8 DVP1Driving;		/* SR65[3:0], Data and Clock driving */
	u8 DFPHigh;		/* CR97[3:0] */
	u8 DFPLow;		/* CR99[3:0] */

};

struct VT1636_DPA_SETTING {
	int PanelSizeID;
	u8 CLK_SEL_ST1;
	u8 CLK_SEL_ST2;
};
#endif /* __CHIP_H__ */
