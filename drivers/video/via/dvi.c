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

static void tmds_register_write(int index, u8 data);
static int tmds_register_read(int index);
static int tmds_register_read_bytes(int index, u8 *buff, int buff_len);
static void __devinit dvi_get_panel_size_from_DDCv1(
	struct tmds_chip_information *tmds_chip,
	struct tmds_setting_information *tmds_setting);
static int viafb_dvi_query_EDID(void);

static inline bool check_tmds_chip(int device_id_subaddr, int device_id)
{
	return tmds_register_read(device_id_subaddr) == device_id;
}

void __devinit viafb_init_dvi_size(struct tmds_chip_information *tmds_chip,
	struct tmds_setting_information *tmds_setting)
{
	DEBUG_MSG(KERN_INFO "viafb_init_dvi_size()\n");

	viafb_dvi_sense();
	if (viafb_dvi_query_EDID() == 1)
		dvi_get_panel_size_from_DDCv1(tmds_chip, tmds_setting);

	return;
}

bool __devinit viafb_tmds_trasmitter_identify(void)
{
	unsigned char sr2a = 0, sr1e = 0, sr3e = 0;

	/* Turn on ouputting pad */
	switch (viaparinfo->chip_info->gfx_chip_name) {
	case UNICHROME_K8M890:
	    /*=* DFP Low Pad on *=*/
		sr2a = viafb_read_reg(VIASR, SR2A);
		viafb_write_reg_mask(SR2A, VIASR, 0x03, BIT0 + BIT1);
		break;

	case UNICHROME_P4M900:
	case UNICHROME_P4M890:
		/* DFP Low Pad on */
		sr2a = viafb_read_reg(VIASR, SR2A);
		viafb_write_reg_mask(SR2A, VIASR, 0x03, BIT0 + BIT1);
		/* DVP0 Pad on */
		sr1e = viafb_read_reg(VIASR, SR1E);
		viafb_write_reg_mask(SR1E, VIASR, 0xC0, BIT6 + BIT7);
		break;

	default:
	    /* DVP0/DVP1 Pad on */
		sr1e = viafb_read_reg(VIASR, SR1E);
		viafb_write_reg_mask(SR1E, VIASR, 0xF0, BIT4 +
			BIT5 + BIT6 + BIT7);
	    /* SR3E[1]Multi-function selection:
	    0 = Emulate I2C and DDC bus by GPIO2/3/4. */
		sr3e = viafb_read_reg(VIASR, SR3E);
		viafb_write_reg_mask(SR3E, VIASR, 0x0, BIT5);
		break;
	}

	/* Check for VT1632: */
	viaparinfo->chip_info->tmds_chip_info.tmds_chip_name = VT1632_TMDS;
	viaparinfo->chip_info->
		tmds_chip_info.tmds_chip_slave_addr = VT1632_TMDS_I2C_ADDR;
	viaparinfo->chip_info->tmds_chip_info.i2c_port = VIA_PORT_31;
	if (check_tmds_chip(VT1632_DEVICE_ID_REG, VT1632_DEVICE_ID)) {
		/*
		 * Currently only support 12bits,dual edge,add 24bits mode later
		 */
		tmds_register_write(0x08, 0x3b);

		DEBUG_MSG(KERN_INFO "\n VT1632 TMDS ! \n");
		DEBUG_MSG(KERN_INFO "\n %2d",
			  viaparinfo->chip_info->tmds_chip_info.tmds_chip_name);
		DEBUG_MSG(KERN_INFO "\n %2d",
			  viaparinfo->chip_info->tmds_chip_info.i2c_port);
		return true;
	} else {
		viaparinfo->chip_info->tmds_chip_info.i2c_port = VIA_PORT_2C;
		if (check_tmds_chip(VT1632_DEVICE_ID_REG, VT1632_DEVICE_ID)) {
			tmds_register_write(0x08, 0x3b);
			DEBUG_MSG(KERN_INFO "\n VT1632 TMDS ! \n");
			DEBUG_MSG(KERN_INFO "\n %2d",
				  viaparinfo->chip_info->
				  tmds_chip_info.tmds_chip_name);
			DEBUG_MSG(KERN_INFO "\n %2d",
				  viaparinfo->chip_info->
				  tmds_chip_info.i2c_port);
			return true;
		}
	}

	viaparinfo->chip_info->tmds_chip_info.tmds_chip_name = INTEGRATED_TMDS;

	if ((viaparinfo->chip_info->gfx_chip_name == UNICHROME_CX700) &&
	    ((viafb_display_hardware_layout == HW_LAYOUT_DVI_ONLY) ||
	     (viafb_display_hardware_layout == HW_LAYOUT_LCD_DVI))) {
		DEBUG_MSG(KERN_INFO "\n Integrated TMDS ! \n");
		return true;
	}

	switch (viaparinfo->chip_info->gfx_chip_name) {
	case UNICHROME_K8M890:
		viafb_write_reg(SR2A, VIASR, sr2a);
		break;

	case UNICHROME_P4M900:
	case UNICHROME_P4M890:
		viafb_write_reg(SR2A, VIASR, sr2a);
		viafb_write_reg(SR1E, VIASR, sr1e);
		break;

	default:
		viafb_write_reg(SR1E, VIASR, sr1e);
		viafb_write_reg(SR3E, VIASR, sr3e);
		break;
	}

	viaparinfo->chip_info->
		tmds_chip_info.tmds_chip_name = NON_TMDS_TRANSMITTER;
	viaparinfo->chip_info->tmds_chip_info.
		tmds_chip_slave_addr = VT1632_TMDS_I2C_ADDR;
	return false;
}

static void tmds_register_write(int index, u8 data)
{
	viafb_i2c_writebyte(viaparinfo->chip_info->tmds_chip_info.i2c_port,
			    viaparinfo->chip_info->tmds_chip_info.tmds_chip_slave_addr,
			    index, data);
}

static int tmds_register_read(int index)
{
	u8 data;

	viafb_i2c_readbyte(viaparinfo->chip_info->tmds_chip_info.i2c_port,
			   (u8) viaparinfo->chip_info->tmds_chip_info.tmds_chip_slave_addr,
			   (u8) index, &data);
	return data;
}

static int tmds_register_read_bytes(int index, u8 *buff, int buff_len)
{
	viafb_i2c_readbytes(viaparinfo->chip_info->tmds_chip_info.i2c_port,
			    (u8) viaparinfo->chip_info->tmds_chip_info.tmds_chip_slave_addr,
			    (u8) index, buff, buff_len);
	return 0;
}

/* DVI Set Mode */
void viafb_dvi_set_mode(const struct fb_var_screeninfo *var, int iga)
{
	struct fb_var_screeninfo dvi_var = *var;
	struct crt_mode_table *rb_mode;
	int maxPixelClock;

	maxPixelClock = viaparinfo->shared->tmds_setting_info.max_pixel_clock;
	if (maxPixelClock && PICOS2KHZ(var->pixclock) / 1000 > maxPixelClock) {
		rb_mode = viafb_get_best_rb_mode(var->xres, var->yres, 60);
		if (rb_mode)
			viafb_fill_var_timing_info(&dvi_var, rb_mode);
	}

	viafb_fill_crtc_timing(&dvi_var, iga);
}

/* Sense DVI Connector */
int viafb_dvi_sense(void)
{
	u8 RegSR1E = 0, RegSR3E = 0, RegCR6B = 0, RegCR91 = 0,
		RegCR93 = 0, RegCR9B = 0, data;
	int ret = false;

	DEBUG_MSG(KERN_INFO "viafb_dvi_sense!!\n");

	if (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266) {
		/* DI1 Pad on */
		RegSR1E = viafb_read_reg(VIASR, SR1E);
		viafb_write_reg(SR1E, VIASR, RegSR1E | 0x30);

		/* CR6B[0]VCK Input Selection: 1 = External clock. */
		RegCR6B = viafb_read_reg(VIACR, CR6B);
		viafb_write_reg(CR6B, VIACR, RegCR6B | 0x08);

		/* CR91[4] VDD On [3] Data On [2] VEE On [1] Back Light Off
		   [0] Software Control Power Sequence */
		RegCR91 = viafb_read_reg(VIACR, CR91);
		viafb_write_reg(CR91, VIACR, 0x1D);

		/* CR93[7] DI1 Data Source Selection: 1 = DSP2.
		   CR93[5] DI1 Clock Source: 1 = internal.
		   CR93[4] DI1 Clock Polarity.
		   CR93[3:1] DI1 Clock Adjust. CR93[0] DI1 enable */
		RegCR93 = viafb_read_reg(VIACR, CR93);
		viafb_write_reg(CR93, VIACR, 0x01);
	} else {
		/* DVP0/DVP1 Pad on */
		RegSR1E = viafb_read_reg(VIASR, SR1E);
		viafb_write_reg(SR1E, VIASR, RegSR1E | 0xF0);

		/* SR3E[1]Multi-function selection:
		   0 = Emulate I2C and DDC bus by GPIO2/3/4. */
		RegSR3E = viafb_read_reg(VIASR, SR3E);
		viafb_write_reg(SR3E, VIASR, RegSR3E & (~0x20));

		/* CR91[4] VDD On [3] Data On [2] VEE On [1] Back Light Off
		   [0] Software Control Power Sequence */
		RegCR91 = viafb_read_reg(VIACR, CR91);
		viafb_write_reg(CR91, VIACR, 0x1D);

		/*CR9B[4] DVP1 Data Source Selection: 1 = From secondary
		display.CR9B[2:0] DVP1 Clock Adjust */
		RegCR9B = viafb_read_reg(VIACR, CR9B);
		viafb_write_reg(CR9B, VIACR, 0x01);
	}

	data = (u8) tmds_register_read(0x09);
	if (data & 0x04)
		ret = true;

	if (ret == false) {
		if (viafb_dvi_query_EDID())
			ret = true;
	}

	/* Restore status */
	viafb_write_reg(SR1E, VIASR, RegSR1E);
	viafb_write_reg(CR91, VIACR, RegCR91);
	if (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266) {
		viafb_write_reg(CR6B, VIACR, RegCR6B);
		viafb_write_reg(CR93, VIACR, RegCR93);
	} else {
		viafb_write_reg(SR3E, VIASR, RegSR3E);
		viafb_write_reg(CR9B, VIACR, RegCR9B);
	}

	return ret;
}

/* Query Flat Panel's EDID Table Version Through DVI Connector */
static int viafb_dvi_query_EDID(void)
{
	u8 data0, data1;
	int restore;

	DEBUG_MSG(KERN_INFO "viafb_dvi_query_EDID!!\n");

	restore = viaparinfo->chip_info->tmds_chip_info.tmds_chip_slave_addr;
	viaparinfo->chip_info->tmds_chip_info.tmds_chip_slave_addr = 0xA0;

	data0 = (u8) tmds_register_read(0x00);
	data1 = (u8) tmds_register_read(0x01);
	if ((data0 == 0) && (data1 == 0xFF)) {
		viaparinfo->chip_info->
			tmds_chip_info.tmds_chip_slave_addr = restore;
		return EDID_VERSION_1;	/* Found EDID1 Table */
	}

	return false;
}

/* Get Panel Size Using EDID1 Table */
static void __devinit dvi_get_panel_size_from_DDCv1(
	struct tmds_chip_information *tmds_chip,
	struct tmds_setting_information *tmds_setting)
{
	int i, restore;
	unsigned char EDID_DATA[18];

	DEBUG_MSG(KERN_INFO "\n dvi_get_panel_size_from_DDCv1 \n");

	restore = tmds_chip->tmds_chip_slave_addr;
	tmds_chip->tmds_chip_slave_addr = 0xA0;
	for (i = 0x25; i < 0x6D; i++) {
		switch (i) {
		case 0x36:
		case 0x48:
		case 0x5A:
		case 0x6C:
			tmds_register_read_bytes(i, EDID_DATA, 10);
			if (!(EDID_DATA[0] || EDID_DATA[1])) {
				/* The first two byte must be zero. */
				if (EDID_DATA[3] == 0xFD) {
					/* To get max pixel clock. */
					tmds_setting->max_pixel_clock =
						EDID_DATA[9] * 10;
				}
			}
			break;

		default:
			break;
		}
	}

	DEBUG_MSG(KERN_INFO "DVI max pixelclock = %d\n",
		tmds_setting->max_pixel_clock);
	tmds_chip->tmds_chip_slave_addr = restore;
}

/* If Disable DVI, turn off pad */
void viafb_dvi_disable(void)
{
	if (viaparinfo->chip_info->
		tmds_chip_info.output_interface == INTERFACE_TMDS)
		/* Turn off TMDS power. */
		viafb_write_reg(CRD2, VIACR,
		viafb_read_reg(VIACR, CRD2) | 0x08);
}

static void dvi_patch_skew_dvp0(void)
{
	/* Reset data driving first: */
	viafb_write_reg_mask(SR1B, VIASR, 0, BIT1);
	viafb_write_reg_mask(SR2A, VIASR, 0, BIT4);

	switch (viaparinfo->chip_info->gfx_chip_name) {
	case UNICHROME_P4M890:
		{
			if ((viaparinfo->tmds_setting_info->h_active == 1600) &&
				(viaparinfo->tmds_setting_info->v_active ==
				1200))
				viafb_write_reg_mask(CR96, VIACR, 0x03,
					       BIT0 + BIT1 + BIT2);
			else
				viafb_write_reg_mask(CR96, VIACR, 0x07,
					       BIT0 + BIT1 + BIT2);
			break;
		}

	case UNICHROME_P4M900:
		{
			viafb_write_reg_mask(CR96, VIACR, 0x07,
				       BIT0 + BIT1 + BIT2 + BIT3);
			viafb_write_reg_mask(SR1B, VIASR, 0x02, BIT1);
			viafb_write_reg_mask(SR2A, VIASR, 0x10, BIT4);
			break;
		}

	default:
		{
			break;
		}
	}
}

static void dvi_patch_skew_dvp_low(void)
{
	switch (viaparinfo->chip_info->gfx_chip_name) {
	case UNICHROME_K8M890:
		{
			viafb_write_reg_mask(CR99, VIACR, 0x03, BIT0 + BIT1);
			break;
		}

	case UNICHROME_P4M900:
		{
			viafb_write_reg_mask(CR99, VIACR, 0x08,
				       BIT0 + BIT1 + BIT2 + BIT3);
			break;
		}

	case UNICHROME_P4M890:
		{
			viafb_write_reg_mask(CR99, VIACR, 0x0F,
				       BIT0 + BIT1 + BIT2 + BIT3);
			break;
		}

	default:
		{
			break;
		}
	}
}

/* If Enable DVI, turn off pad */
void viafb_dvi_enable(void)
{
	u8 data;

	switch (viaparinfo->chip_info->tmds_chip_info.output_interface) {
	case INTERFACE_DVP0:
		viafb_write_reg_mask(CR6B, VIACR, 0x01, BIT0);
		viafb_write_reg_mask(CR6C, VIACR, 0x21, BIT0 + BIT5);
		dvi_patch_skew_dvp0();
		if (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266)
			tmds_register_write(0x88, 0x3b);
		else
			/*clear CR91[5] to direct on display period
			   in the secondary diplay path */
			via_write_reg_mask(VIACR, 0x91, 0x00, 0x20);
		break;

	case INTERFACE_DVP1:
		if (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266)
			viafb_write_reg_mask(CR93, VIACR, 0x21, BIT0 + BIT5);

		/*fix dvi cann't be enabled with MB VT5718C4 - Al Zhang */
		if (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266)
			tmds_register_write(0x88, 0x3b);
		else
			/*clear CR91[5] to direct on display period
			  in the secondary diplay path */
			via_write_reg_mask(VIACR, 0x91, 0x00, 0x20);

		/*fix DVI cannot enable on EPIA-M board */
		if (viafb_platform_epia_dvi == 1) {
			viafb_write_reg_mask(CR91, VIACR, 0x1f, 0x1f);
			viafb_write_reg_mask(CR88, VIACR, 0x00, BIT6 + BIT0);
			if (viafb_bus_width == 24) {
				if (viafb_device_lcd_dualedge == 1)
					data = 0x3F;
				else
					data = 0x37;
				viafb_i2c_writebyte(viaparinfo->chip_info->
					tmds_chip_info.i2c_port,
					viaparinfo->chip_info->
					tmds_chip_info.tmds_chip_slave_addr,
					0x08, data);
			}
		}
		break;

	case INTERFACE_DFP_HIGH:
		if (viaparinfo->chip_info->gfx_chip_name != UNICHROME_CLE266)
			via_write_reg_mask(VIACR, CR97, 0x03, 0x03);

		via_write_reg_mask(VIACR, 0x91, 0x00, 0x20);
		break;

	case INTERFACE_DFP_LOW:
		if (viaparinfo->chip_info->gfx_chip_name == UNICHROME_CLE266)
			break;

		dvi_patch_skew_dvp_low();
		via_write_reg_mask(VIACR, 0x91, 0x00, 0x20);
		break;

	case INTERFACE_TMDS:
		/* Turn on Display period in the panel path. */
		viafb_write_reg_mask(CR91, VIACR, 0, BIT7);

		/* Turn on TMDS power. */
		viafb_write_reg_mask(CRD2, VIACR, 0, BIT3);
		break;
	}

	if (viaparinfo->tmds_setting_info->iga_path == IGA2) {
		/* Disable LCD Scaling */
		viafb_write_reg_mask(CR79, VIACR, 0x00, BIT0);
	}
}
