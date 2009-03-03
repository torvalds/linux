/*
   cx231xx_avcore.c - driver for Conexant Cx23100/101/102
		      USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>

   This program contains the specific code to control the avdecoder chip and
   other related usb control functions for cx231xx based chipset.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/mutex.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-chip-ident.h>

#include "cx231xx.h"

/******************************************************************************
 *            C O L I B R I - B L O C K    C O N T R O L   functions          *
 ********************************************************************* ********/
int cx231xx_colibri_init_super_block(struct cx231xx *dev, u32 ref_count)
{
	int status = 0;
	u8 temp = 0;
	u32 colibri_power_status = 0;
	int i = 0;

	/* super block initialize */
	temp = (u8) (ref_count & 0xff);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					SUP_BLK_TUNE2, 2, temp, 1);

	status = cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				       SUP_BLK_TUNE2, 2,
				       &colibri_power_status, 1);

	temp = (u8) ((ref_count & 0x300) >> 8);
	temp |= 0x40;
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					SUP_BLK_TUNE1, 2, temp, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					SUP_BLK_PLL2, 2, 0x0f, 1);

	/* enable pll     */
	while (colibri_power_status != 0x18) {
		status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
						SUP_BLK_PWRDN, 2, 0x18, 1);
		status = cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					       SUP_BLK_PWRDN, 2,
					       &colibri_power_status, 1);
		colibri_power_status &= 0xff;
		if (status < 0) {
			cx231xx_info(": Init Super Block failed in sending/receiving cmds\n");
			break;
		}
		i++;
		if (i == 10) {
			cx231xx_info(": Init Super Block force break in loop !!!!\n");
			status = -1;
			break;
		}
	}

	if (status < 0)
		return status;

	/* start tuning filter */
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					SUP_BLK_TUNE3, 2, 0x40, 1);
	msleep(5);

	/* exit tuning */
	status =
	    cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS, SUP_BLK_TUNE3,
				   2, 0x00, 1);

	return status;
}

int cx231xx_colibri_init_channels(struct cx231xx *dev)
{
	int status = 0;

	/* power up all 3 channels, clear pd_buffer */
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_PWRDN_CLAMP_CH1, 2, 0x00, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_PWRDN_CLAMP_CH2, 2, 0x00, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_PWRDN_CLAMP_CH3, 2, 0x00, 1);

	/* Enable quantizer calibration */
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					ADC_COM_QUANT, 2, 0x02, 1);

	/* channel initialize, force modulator (fb) reset */
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_FB_FRCRST_CH1, 2, 0x17, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_FB_FRCRST_CH2, 2, 0x17, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_FB_FRCRST_CH3, 2, 0x17, 1);

	/* start quantilizer calibration  */
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_CAL_ATEST_CH1, 2, 0x10, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_CAL_ATEST_CH2, 2, 0x10, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_CAL_ATEST_CH3, 2, 0x10, 1);
	msleep(5);

	/* exit modulator (fb) reset */
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_FB_FRCRST_CH1, 2, 0x07, 1);
	status =  cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_FB_FRCRST_CH2, 2, 0x07, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_FB_FRCRST_CH3, 2, 0x07, 1);

	/* enable the pre_clamp in each channel for single-ended input */
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_NTF_PRECLMP_EN_CH1, 2, 0xf0, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_NTF_PRECLMP_EN_CH2, 2, 0xf0, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_NTF_PRECLMP_EN_CH3, 2, 0xf0, 1);

	/* use diode instead of resistor, so set term_en to 0, res_en to 0  */
	status = cx231xx_reg_mask_write(dev, Colibri_DEVICE_ADDRESS, 8,
				   ADC_QGAIN_RES_TRM_CH1, 3, 7, 0x00);
	status = cx231xx_reg_mask_write(dev, Colibri_DEVICE_ADDRESS, 8,
				   ADC_QGAIN_RES_TRM_CH2, 3, 7, 0x00);
	status = cx231xx_reg_mask_write(dev, Colibri_DEVICE_ADDRESS, 8,
				   ADC_QGAIN_RES_TRM_CH3, 3, 7, 0x00);

	/* dynamic element matching off */
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_DCSERVO_DEM_CH1, 2, 0x03, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_DCSERVO_DEM_CH2, 2, 0x03, 1);
	status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_DCSERVO_DEM_CH3, 2, 0x03, 1);

	return status;
}

int cx231xx_colibri_setup_AFE_for_baseband(struct cx231xx *dev)
{
	u32 c_value = 0;
	int status = 0;

	status =
	    cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				  ADC_PWRDN_CLAMP_CH2, 2, &c_value, 1);
	c_value &= (~(0x50));
	status =
	    cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
				   ADC_PWRDN_CLAMP_CH2, 2, c_value, 1);

	return status;
}

/*
	we have 3 channel
	channel 1 ----- pin 1  to pin4(in reg is 1-4)
	channel 2 ----- pin 5  to pin8(in reg is 5-8)
	channel 3 ----- pin 9 to pin 12(in reg is 9-11)
*/
int cx231xx_colibri_set_input_mux(struct cx231xx *dev, u32 input_mux)
{
	u8 ch1_setting = (u8) input_mux;
	u8 ch2_setting = (u8) (input_mux >> 8);
	u8 ch3_setting = (u8) (input_mux >> 16);
	int status = 0;
	u32 value = 0;

	if (ch1_setting != 0) {
		status =
		    cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					  ADC_INPUT_CH1, 2, &value, 1);
		value &= (!INPUT_SEL_MASK);
		value |= (ch1_setting - 1) << 4;
		value &= 0xff;
		status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					   ADC_INPUT_CH1, 2, value, 1);
	}

	if (ch2_setting != 0) {
		status =
		    cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					  ADC_INPUT_CH2, 2, &value, 1);
		value &= (!INPUT_SEL_MASK);
		value |= (ch2_setting - 1) << 4;
		value &= 0xff;
		status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					   ADC_INPUT_CH2, 2, value, 1);
	}

	/* For ch3_setting, the value to put in the register is
	   7 less than the input number */
	if (ch3_setting != 0) {
		status = cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					  ADC_INPUT_CH3, 2, &value, 1);
		value &= (!INPUT_SEL_MASK);
		value |= (ch3_setting - 1) << 4;
		value &= 0xff;
		status = cx231xx_write_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					   ADC_INPUT_CH3, 2, value, 1);
	}

	return status;
}

int cx231xx_colibri_set_mode(struct cx231xx *dev, enum AFE_MODE mode)
{
	int status = 0;

	switch (mode) {
	case AFE_MODE_LOW_IF:
		/* SetupAFEforLowIF();  */
		break;
	case AFE_MODE_BASEBAND:
		status = cx231xx_colibri_setup_AFE_for_baseband(dev);
		break;
	case AFE_MODE_EU_HI_IF:
		/* SetupAFEforEuHiIF(); */
		break;
	case AFE_MODE_US_HI_IF:
		/* SetupAFEforUsHiIF(); */
		break;
	case AFE_MODE_JAPAN_HI_IF:
		/* SetupAFEforJapanHiIF(); */
		break;
	}

	if ((mode != dev->colibri_mode) && (dev->video_input == CX231XX_VMUX_TELEVISION))
		status = cx231xx_colibri_adjust_ref_count(dev,
						     CX231XX_VMUX_TELEVISION);

	dev->colibri_mode = mode;

	return status;
}

/* For power saving in the EVK */
int cx231xx_colibri_update_power_control(struct cx231xx *dev, AV_MODE avmode)
{
	u32 colibri_power_status = 0;
	int status = 0;

	switch (dev->model) {
	case CX231XX_BOARD_CNXT_RDE_250:
	case CX231XX_BOARD_CNXT_RDU_250:

		if (avmode == POLARIS_AVMODE_ANALOGT_TV) {
			while (colibri_power_status != 0x18) {
				status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							SUP_BLK_PWRDN, 2,
							0x18, 1);
				status = cx231xx_read_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							SUP_BLK_PWRDN, 2,
							&colibri_power_status,
							1);
				if (status < 0)
					break;
			}

			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   ADC_PWRDN_CLAMP_CH1, 2, 0x00,
						   1);
			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   ADC_PWRDN_CLAMP_CH2, 2, 0x00,
						   1);
			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   ADC_PWRDN_CLAMP_CH3, 2, 0x00,
						   1);
		} else if (avmode == POLARIS_AVMODE_DIGITAL) {
			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   ADC_PWRDN_CLAMP_CH1, 2, 0x70,
						   1);
			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   ADC_PWRDN_CLAMP_CH2, 2, 0x70,
						   1);
			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   ADC_PWRDN_CLAMP_CH3, 2, 0x70,
						   1);

			status = cx231xx_read_i2c_data(dev,
						  Colibri_DEVICE_ADDRESS,
						  SUP_BLK_PWRDN, 2,
						  &colibri_power_status, 1);
			colibri_power_status |= 0x07;
			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   SUP_BLK_PWRDN, 2,
						   colibri_power_status, 1);
		} else if (avmode == POLARIS_AVMODE_ENXTERNAL_AV) {

			while (colibri_power_status != 0x18) {
				status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							SUP_BLK_PWRDN, 2,
							0x18, 1);
				status = cx231xx_read_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							SUP_BLK_PWRDN, 2,
							&colibri_power_status,
							1);
				if (status < 0)
					break;
			}

			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   ADC_PWRDN_CLAMP_CH1, 2, 0x00,
						   1);
			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   ADC_PWRDN_CLAMP_CH2, 2, 0x00,
						   1);
			status = cx231xx_write_i2c_data(dev,
						   Colibri_DEVICE_ADDRESS,
						   ADC_PWRDN_CLAMP_CH3, 2, 0x00,
						   1);
		} else {
			cx231xx_info("Invalid AV mode input\n");
			status = -1;
		}
		break;
	default:
		if (avmode == POLARIS_AVMODE_ANALOGT_TV) {
			while (colibri_power_status != 0x18) {
				status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							SUP_BLK_PWRDN, 2,
							0x18, 1);
				status = cx231xx_read_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							SUP_BLK_PWRDN, 2,
							&colibri_power_status,
							1);
				if (status < 0)
					break;
			}

			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							ADC_PWRDN_CLAMP_CH1, 2,
							0x40, 1);
			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							ADC_PWRDN_CLAMP_CH2, 2,
							0x40, 1);
			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							ADC_PWRDN_CLAMP_CH3, 2,
							0x00, 1);
		} else if (avmode == POLARIS_AVMODE_DIGITAL) {
			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							ADC_PWRDN_CLAMP_CH1, 2,
							0x70, 1);
			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							ADC_PWRDN_CLAMP_CH2, 2,
							0x70, 1);
			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							ADC_PWRDN_CLAMP_CH3, 2,
							0x70, 1);

			status = cx231xx_read_i2c_data(dev,
						       Colibri_DEVICE_ADDRESS,
						       SUP_BLK_PWRDN, 2,
						       &colibri_power_status,
						       1);
			colibri_power_status |= 0x07;
			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							SUP_BLK_PWRDN, 2,
							colibri_power_status,
							1);
		} else if (avmode == POLARIS_AVMODE_ENXTERNAL_AV) {
			while (colibri_power_status != 0x18) {
				status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							SUP_BLK_PWRDN, 2,
							0x18, 1);
				status = cx231xx_read_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							SUP_BLK_PWRDN, 2,
							&colibri_power_status,
							1);
				if (status < 0)
					break;
			}

			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							ADC_PWRDN_CLAMP_CH1, 2,
							0x00, 1);
			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							ADC_PWRDN_CLAMP_CH2, 2,
							0x00, 1);
			status = cx231xx_write_i2c_data(dev,
							Colibri_DEVICE_ADDRESS,
							ADC_PWRDN_CLAMP_CH3, 2,
							0x40, 1);
		} else {
			cx231xx_info("Invalid AV mode input\n");
			status = -1;
		}
	}			/* switch  */

	return status;
}

int cx231xx_colibri_adjust_ref_count(struct cx231xx *dev, u32 video_input)
{
	u32 input_mode = 0;
	u32 ntf_mode = 0;
	int status = 0;

	dev->video_input = video_input;

	if (video_input == CX231XX_VMUX_TELEVISION) {
		status = cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					  ADC_INPUT_CH3, 2, &input_mode, 1);
		status = cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					  ADC_NTF_PRECLMP_EN_CH3, 2, &ntf_mode,
					  1);
	} else {
		status = cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					  ADC_INPUT_CH1, 2, &input_mode, 1);
		status = cx231xx_read_i2c_data(dev, Colibri_DEVICE_ADDRESS,
					  ADC_NTF_PRECLMP_EN_CH1, 2, &ntf_mode,
					  1);
	}

	input_mode = (ntf_mode & 0x3) | ((input_mode & 0x6) << 1);

	switch (input_mode) {
	case SINGLE_ENDED:
		dev->colibri_ref_count = 0x23C;
		break;
	case LOW_IF:
		dev->colibri_ref_count = 0x24C;
		break;
	case EU_IF:
		dev->colibri_ref_count = 0x258;
		break;
	case US_IF:
		dev->colibri_ref_count = 0x260;
		break;
	default:
		break;
	}

	status = cx231xx_colibri_init_super_block(dev, dev->colibri_ref_count);

	return status;
}

/******************************************************************************
 *     V I D E O / A U D I O    D E C O D E R    C O N T R O L   functions    *
 ******************************************++**********************************/
int cx231xx_set_video_input_mux(struct cx231xx *dev, u8 input)
{
	int status = 0;

	switch (INPUT(input)->type) {
	case CX231XX_VMUX_COMPOSITE1:
	case CX231XX_VMUX_SVIDEO:
		if ((dev->current_pcb_config.type == USB_BUS_POWER) &&
		    (dev->power_mode != POLARIS_AVMODE_ENXTERNAL_AV)) {
			/* External AV */
			status = cx231xx_set_power_mode(dev,
					POLARIS_AVMODE_ENXTERNAL_AV);
			if (status < 0) {
				cx231xx_errdev("%s: cx231xx_set_power_mode : Failed to set Power - errCode [%d]!\n",
				     __func__, status);
				return status;
			}
		}
		status = cx231xx_set_decoder_video_input(dev,
							 INPUT(input)->type,
							 INPUT(input)->vmux);
		break;
	case CX231XX_VMUX_TELEVISION:
	case CX231XX_VMUX_CABLE:
		if ((dev->current_pcb_config.type == USB_BUS_POWER) &&
		    (dev->power_mode != POLARIS_AVMODE_ANALOGT_TV)) {
			/* Tuner */
			status = cx231xx_set_power_mode(dev,
						POLARIS_AVMODE_ANALOGT_TV);
			if (status < 0) {
				cx231xx_errdev("%s: cx231xx_set_power_mode : Failed to set Power - errCode [%d]!\n",
				     __func__, status);
				return status;
			}
		}
		status = cx231xx_set_decoder_video_input(dev,
							CX231XX_VMUX_COMPOSITE1,
							INPUT(input)->vmux);
		break;
	default:
		cx231xx_errdev("%s: cx231xx_set_power_mode : Unknown Input %d !\n",
		     __func__, INPUT(input)->type);
		break;
	}

	/* save the selection */
	dev->video_input = input;

	return status;
}

int cx231xx_set_decoder_video_input(struct cx231xx *dev, u8 pin_type, u8 input)
{
	int status = 0;
	u32 value = 0;

	if (pin_type != dev->video_input) {
		status = cx231xx_colibri_adjust_ref_count(dev, pin_type);
		if (status < 0) {
			cx231xx_errdev("%s: cx231xx_colibri_adjust_ref_count :Failed to set Colibri input mux - errCode [%d]!\n",
			     __func__, status);
			return status;
		}
	}

	/* call colibri block to set video inputs */
	status = cx231xx_colibri_set_input_mux(dev, input);
	if (status < 0) {
		cx231xx_errdev("%s: cx231xx_colibri_set_input_mux :Failed to set Colibri input mux - errCode [%d]!\n",
		     __func__, status);
		return status;
	}

	switch (pin_type) {
	case CX231XX_VMUX_COMPOSITE1:
		status = cx231xx_read_i2c_data(dev,
						HAMMERHEAD_I2C_ADDRESS,
						AFE_CTRL, 2, &value, 4);
		value |= (0 << 13) | (1 << 4);
		value &= ~(1 << 5);

		value &= (~(0x1ff8000));	/* set [24:23] [22:15] to 0  */
		value |= 0x1000000;	/* set FUNC_MODE[24:23] = 2 IF_MOD[22:15] = 0  */
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
						AFE_CTRL, 2, value, 4);

		status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
						OUT_CTRL1, 2, &value, 4);
		value |= (1 << 7);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
						OUT_CTRL1, 2, value, 4);

		/* Set vip 1.1 output mode */
		status = cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							OUT_CTRL1,
							FLD_OUT_MODE,
							OUT_MODE_VIP11);

		/* Tell DIF object to go to baseband mode  */
		status = cx231xx_dif_set_standard(dev, DIF_USE_BASEBAND);
		if (status < 0) {
			cx231xx_errdev("%s: cx231xx_dif set to By pass mode - errCode [%d]!\n",
				__func__, status);
			return status;
		}

		/* Read the DFE_CTRL1 register */
		status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
						DFE_CTRL1, 2, &value, 4);

		/* enable the VBI_GATE_EN */
		value |= FLD_VBI_GATE_EN;

		/* Enable the auto-VGA enable */
		value |= FLD_VGA_AUTO_EN;

		/* Write it back */
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
						DFE_CTRL1, 2, value, 4);

		/* Disable auto config of registers */
		status = cx231xx_read_modify_write_i2c_dword(dev,
					HAMMERHEAD_I2C_ADDRESS,
					MODE_CTRL, FLD_ACFG_DIS,
					cx231xx_set_field(FLD_ACFG_DIS, 1));

		/* Set CVBS input mode */
		status = cx231xx_read_modify_write_i2c_dword(dev,
			HAMMERHEAD_I2C_ADDRESS,
			MODE_CTRL, FLD_INPUT_MODE,
			cx231xx_set_field(FLD_INPUT_MODE, INPUT_MODE_CVBS_0));
		break;
	case CX231XX_VMUX_SVIDEO:
		/* Disable the use of  DIF */

		status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					       AFE_CTRL, 2, &value, 4);

		value &= (~(0x1ff8000));	/* set [24:23] [22:15] to 0 */
		value |= 0x1000010;	/* set FUNC_MODE[24:23] = 2
						IF_MOD[22:15] = 0 DCR_BYP_CH2[4:4] = 1; */
		status = cx231xx_write_i2c_data(dev,
						HAMMERHEAD_I2C_ADDRESS,
						AFE_CTRL, 2, value, 4);

		/* Tell DIF object to go to baseband mode */
		status = cx231xx_dif_set_standard(dev, DIF_USE_BASEBAND);
		if (status < 0) {
			cx231xx_errdev("%s: cx231xx_dif set to By pass mode - errCode [%d]!\n",
				__func__, status);
			return status;
		}

		/* Read the DFE_CTRL1 register */
		status = cx231xx_read_i2c_data(dev,
					       HAMMERHEAD_I2C_ADDRESS,
					       DFE_CTRL1, 2, &value, 4);

		/* enable the VBI_GATE_EN */
		value |= FLD_VBI_GATE_EN;

		/* Enable the auto-VGA enable */
		value |= FLD_VGA_AUTO_EN;

		/* Write it back */
		status = cx231xx_write_i2c_data(dev,
						HAMMERHEAD_I2C_ADDRESS,
						DFE_CTRL1, 2, value, 4);

		/* Disable auto config of registers  */
		status =  cx231xx_read_modify_write_i2c_dword(dev,
					HAMMERHEAD_I2C_ADDRESS,
					MODE_CTRL, FLD_ACFG_DIS,
					cx231xx_set_field(FLD_ACFG_DIS, 1));

		/* Set YC input mode */
		status = cx231xx_read_modify_write_i2c_dword(dev,
			HAMMERHEAD_I2C_ADDRESS,
			MODE_CTRL,
			FLD_INPUT_MODE,
			cx231xx_set_field(FLD_INPUT_MODE, INPUT_MODE_YC_1));

		/* Chroma to ADC2 */
		status = cx231xx_read_i2c_data(dev,
						HAMMERHEAD_I2C_ADDRESS,
						AFE_CTRL, 2, &value, 4);
		value |= FLD_CHROMA_IN_SEL;	/* set the chroma in select */

		/* Clear VGA_SEL_CH2 and VGA_SEL_CH3 (bits 7 and 8)
		   This sets them to use video
		   rather than audio.  Only one of the two will be in use. */
		value &= ~(FLD_VGA_SEL_CH2 | FLD_VGA_SEL_CH3);

		status = cx231xx_write_i2c_data(dev,
						HAMMERHEAD_I2C_ADDRESS,
						AFE_CTRL, 2, value, 4);

		status = cx231xx_colibri_set_mode(dev, AFE_MODE_BASEBAND);
		break;
	case CX231XX_VMUX_TELEVISION:
	case CX231XX_VMUX_CABLE:
	default:
		switch (dev->model) {
		case CX231XX_BOARD_CNXT_RDE_250:
		case CX231XX_BOARD_CNXT_RDU_250:
			/* Disable the use of  DIF   */

			status = cx231xx_read_i2c_data(dev,
						       HAMMERHEAD_I2C_ADDRESS,
						       AFE_CTRL, 2,
						       &value, 4);
			value |= (0 << 13) | (1 << 4);
			value &= ~(1 << 5);

			value &= (~(0x1FF8000));	/* set [24:23] [22:15] to 0 */
			value |= 0x1000000;	/* set FUNC_MODE[24:23] = 2 IF_MOD[22:15] = 0 */
			status = cx231xx_write_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							AFE_CTRL, 2,
							value, 4);

			status = cx231xx_read_i2c_data(dev,
						       HAMMERHEAD_I2C_ADDRESS,
						       OUT_CTRL1, 2,
						       &value, 4);
			value |= (1 << 7);
			status = cx231xx_write_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							OUT_CTRL1, 2,
							value, 4);

			/* Set vip 1.1 output mode */
			status = cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							OUT_CTRL1, FLD_OUT_MODE,
							OUT_MODE_VIP11);

			/* Tell DIF object to go to baseband mode */
			status = cx231xx_dif_set_standard(dev,
							  DIF_USE_BASEBAND);
			if (status < 0) {
				cx231xx_errdev("%s: cx231xx_dif set to By pass mode - errCode [%d]!\n",
					__func__, status);
				return status;
			}

			/* Read the DFE_CTRL1 register */
			status = cx231xx_read_i2c_data(dev,
						       HAMMERHEAD_I2C_ADDRESS,
						       DFE_CTRL1, 2,
						       &value, 4);

			/* enable the VBI_GATE_EN */
			value |= FLD_VBI_GATE_EN;

			/* Enable the auto-VGA enable */
			value |= FLD_VGA_AUTO_EN;

			/* Write it back */
			status = cx231xx_write_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							DFE_CTRL1, 2,
							value, 4);

			/* Disable auto config of registers */
			status = cx231xx_read_modify_write_i2c_dword(dev,
					HAMMERHEAD_I2C_ADDRESS,
					MODE_CTRL, FLD_ACFG_DIS,
					cx231xx_set_field(FLD_ACFG_DIS, 1));

			/* Set CVBS input mode */
			status = cx231xx_read_modify_write_i2c_dword(dev,
				HAMMERHEAD_I2C_ADDRESS,
				MODE_CTRL, FLD_INPUT_MODE,
				cx231xx_set_field(FLD_INPUT_MODE, INPUT_MODE_CVBS_0));
			break;
		default:
			/* Enable the DIF for the tuner */

			/* Reinitialize the DIF */
			status = cx231xx_dif_set_standard(dev, dev->norm);
			if (status < 0) {
				cx231xx_errdev("%s: cx231xx_dif set to By pass mode - errCode [%d]!\n",
					__func__, status);
				return status;
			}

			/* Make sure bypass is cleared */
			status = cx231xx_read_i2c_data(dev,
						      HAMMERHEAD_I2C_ADDRESS,
						      DIF_MISC_CTRL,
						      2, &value, 4);

			/* Clear the bypass bit */
			value &= ~FLD_DIF_DIF_BYPASS;

			/* Enable the use of the DIF block */
			status = cx231xx_write_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							DIF_MISC_CTRL,
							2, value, 4);

			/* Read the DFE_CTRL1 register */
			status = cx231xx_read_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							DFE_CTRL1, 2,
							&value, 4);

			/* Disable the VBI_GATE_EN */
			value &= ~FLD_VBI_GATE_EN;

			/* Enable the auto-VGA enable, AGC, and
			   set the skip count to 2 */
			value |= FLD_VGA_AUTO_EN | FLD_AGC_AUTO_EN | 0x00200000;

			/* Write it back */
			status = cx231xx_write_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							DFE_CTRL1, 2,
							value, 4);

			/* Wait 15 ms */
			msleep(1);

			/* Disable the auto-VGA enable AGC */
			value &= ~(FLD_VGA_AUTO_EN);

			/* Write it back */
			status = cx231xx_write_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							DFE_CTRL1, 2,
							value, 4);

			/* Enable Polaris B0 AGC output */
			status = cx231xx_read_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							PIN_CTRL, 2,
							&value, 4);
			value |= (FLD_OEF_AGC_RF) |
				 (FLD_OEF_AGC_IFVGA) |
				 (FLD_OEF_AGC_IF);
			status = cx231xx_write_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							PIN_CTRL, 2,
							value, 4);

			/* Set vip 1.1 output mode */
			status = cx231xx_read_modify_write_i2c_dword(dev,
						HAMMERHEAD_I2C_ADDRESS,
						OUT_CTRL1, FLD_OUT_MODE,
						OUT_MODE_VIP11);

			/* Disable auto config of registers */
			status = cx231xx_read_modify_write_i2c_dword(dev,
					HAMMERHEAD_I2C_ADDRESS,
					MODE_CTRL, FLD_ACFG_DIS,
					cx231xx_set_field(FLD_ACFG_DIS, 1));

			/* Set CVBS input mode */
			status = cx231xx_read_modify_write_i2c_dword(dev,
				HAMMERHEAD_I2C_ADDRESS,
				MODE_CTRL, FLD_INPUT_MODE,
				cx231xx_set_field(FLD_INPUT_MODE, INPUT_MODE_CVBS_0));

			/* Set some bits in AFE_CTRL so that channel 2 or 3 is ready to receive audio */
			/* Clear clamp for channels 2 and 3      (bit 16-17) */
			/* Clear droop comp                      (bit 19-20) */
			/* Set VGA_SEL (for audio control)       (bit 7-8) */
			status = cx231xx_read_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							AFE_CTRL, 2,
							&value, 4);

			value |= FLD_VGA_SEL_CH3 | FLD_VGA_SEL_CH2;

			status = cx231xx_write_i2c_data(dev,
							HAMMERHEAD_I2C_ADDRESS,
							AFE_CTRL, 2,
							value, 4);
			break;

		}
		break;
	}

	/* Set raw VBI mode */
	status = cx231xx_read_modify_write_i2c_dword(dev,
				HAMMERHEAD_I2C_ADDRESS,
				OUT_CTRL1, FLD_VBIHACTRAW_EN,
				cx231xx_set_field(FLD_VBIHACTRAW_EN, 1));

	status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
				       OUT_CTRL1, 2,
				       &value, 4);
	if (value & 0x02) {
		value |= (1 << 19);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   OUT_CTRL1, 2, value, 4);
	}

	return status;
}

/*
 * Handle any video-mode specific overrides that are different on a per video standards
 * basis after touching the MODE_CTRL register which resets many values for autodetect
 */
int cx231xx_do_mode_ctrl_overrides(struct cx231xx *dev)
{
	int status = 0;

	cx231xx_info("do_mode_ctrl_overrides : 0x%x\n",
		     (unsigned int)dev->norm);

	/* Change the DFE_CTRL3 bp_percent to fix flagging */
	status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					DFE_CTRL3, 2,
					0xCD3F0280, 4);

	if (dev->norm & (V4L2_STD_NTSC_M | V4L2_STD_NTSC_M_JP | V4L2_STD_PAL_M)) {
		cx231xx_info("do_mode_ctrl_overrides NTSC\n");

		/* Move the close caption lines out of active video,
		   adjust the active video start point */
		status = cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							VERT_TIM_CTRL,
							FLD_VBLANK_CNT, 0x18);
		status = cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							VERT_TIM_CTRL,
							FLD_VACTIVE_CNT,
							0x1E6000);
		status = cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							VERT_TIM_CTRL,
							FLD_V656BLANK_CNT,
							0x1E000000);

		status = cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							HORIZ_TIM_CTRL,
							FLD_HBLANK_CNT,
							cx231xx_set_field
							(FLD_HBLANK_CNT, 0x79));
	} else if (dev->norm & (V4L2_STD_PAL_B | V4L2_STD_PAL_G |
				V4L2_STD_PAL_D | V4L2_STD_PAL_I |
				V4L2_STD_PAL_N | V4L2_STD_PAL_Nc)) {
		cx231xx_info("do_mode_ctrl_overrides PAL\n");
		status = cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							VERT_TIM_CTRL,
							FLD_VBLANK_CNT, 0x24);
		/* Adjust the active video horizontal start point */
		status = cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							HORIZ_TIM_CTRL,
							FLD_HBLANK_CNT,
							cx231xx_set_field
							(FLD_HBLANK_CNT, 0x85));
	} else if (dev->norm & (V4L2_STD_SECAM_B  | V4L2_STD_SECAM_D |
				V4L2_STD_SECAM_G  | V4L2_STD_SECAM_K |
				V4L2_STD_SECAM_K1 | V4L2_STD_SECAM_L |
				V4L2_STD_SECAM_LC)) {
		cx231xx_info("do_mode_ctrl_overrides SECAM\n");
		status =  cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							VERT_TIM_CTRL,
							FLD_VBLANK_CNT, 0x24);
		/* Adjust the active video horizontal start point */
		status = cx231xx_read_modify_write_i2c_dword(dev,
							HAMMERHEAD_I2C_ADDRESS,
							HORIZ_TIM_CTRL,
							FLD_HBLANK_CNT,
							cx231xx_set_field
							(FLD_HBLANK_CNT, 0x85));
	}

	return status;
}

int cx231xx_set_audio_input(struct cx231xx *dev, u8 input)
{
	int status = 0;
	enum AUDIO_INPUT ainput = AUDIO_INPUT_LINE;

	switch (INPUT(input)->amux) {
	case CX231XX_AMUX_VIDEO:
		ainput = AUDIO_INPUT_TUNER_TV;
		break;
	case CX231XX_AMUX_LINE_IN:
		status = cx231xx_flatiron_set_audio_input(dev, input);
		ainput = AUDIO_INPUT_LINE;
		break;
	default:
		break;
	}

	status = cx231xx_set_audio_decoder_input(dev, ainput);

	return status;
}

int cx231xx_set_audio_decoder_input(struct cx231xx *dev,
				    enum AUDIO_INPUT audio_input)
{
	u32 dwval;
	int status;
	u32 gen_ctrl;
	u32 value = 0;

	/* Put it in soft reset   */
	status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
				       GENERAL_CTL, 2, &gen_ctrl, 1);
	gen_ctrl |= 1;
	status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					GENERAL_CTL, 2, gen_ctrl, 1);

	switch (audio_input) {
	case AUDIO_INPUT_LINE:
		/* setup AUD_IO control from Merlin paralle output */
		value = cx231xx_set_field(FLD_AUD_CHAN1_SRC,
					  AUD_CHAN_SRC_PARALLEL);
		status = cx231xx_write_i2c_data(dev,
						HAMMERHEAD_I2C_ADDRESS,
						AUD_IO_CTRL, 2, value, 4);

		/* setup input to Merlin, SRC2 connect to AC97
		   bypass upsample-by-2, slave mode, sony mode, left justify
		   adr 091c, dat 01000000 */
		status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					       AC97_CTL,
					  2, &dwval, 4);

		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   AC97_CTL, 2,
					   (dwval | FLD_AC97_UP2X_BYPASS), 4);

		/* select the parallel1 and SRC3 */
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
				BAND_OUT_SEL, 2,
				cx231xx_set_field(FLD_SRC3_IN_SEL, 0x0) |
				cx231xx_set_field(FLD_SRC3_CLK_SEL, 0x0) |
				cx231xx_set_field(FLD_PARALLEL1_SRC_SEL, 0x0),
				4);

		/* unmute all, AC97 in, independence mode
		   adr 08d0, data 0x00063073 */
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   PATH1_CTL1, 2, 0x00063073, 4);

		/* set AVC maximum threshold, adr 08d4, dat ffff0024 */
		status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					  PATH1_VOL_CTL, 2, &dwval, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   PATH1_VOL_CTL, 2,
					   (dwval | FLD_PATH1_AVC_THRESHOLD),
					   4);

		/* set SC maximum threshold, adr 08ec, dat ffffb3a3 */
		status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					  PATH1_SC_CTL, 2, &dwval, 4);
		status =  cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   PATH1_SC_CTL, 2,
					   (dwval | FLD_PATH1_SC_THRESHOLD), 4);
		break;

	case AUDIO_INPUT_TUNER_TV:
	default:

		/* Setup SRC sources and clocks */
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
			BAND_OUT_SEL, 2,
			cx231xx_set_field(FLD_SRC6_IN_SEL, 0x00)         |
			cx231xx_set_field(FLD_SRC6_CLK_SEL, 0x01)        |
			cx231xx_set_field(FLD_SRC5_IN_SEL, 0x00)         |
			cx231xx_set_field(FLD_SRC5_CLK_SEL, 0x02)        |
			cx231xx_set_field(FLD_SRC4_IN_SEL, 0x02)         |
			cx231xx_set_field(FLD_SRC4_CLK_SEL, 0x03)        |
			cx231xx_set_field(FLD_SRC3_IN_SEL, 0x00)         |
			cx231xx_set_field(FLD_SRC3_CLK_SEL, 0x00)        |
			cx231xx_set_field(FLD_BASEBAND_BYPASS_CTL, 0x00) |
			cx231xx_set_field(FLD_AC97_SRC_SEL, 0x03)        |
			cx231xx_set_field(FLD_I2S_SRC_SEL, 0x00)         |
			cx231xx_set_field(FLD_PARALLEL2_SRC_SEL, 0x02)   |
			cx231xx_set_field(FLD_PARALLEL1_SRC_SEL, 0x01), 4);

		/* Setup the AUD_IO control */
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
			AUD_IO_CTRL, 2,
			cx231xx_set_field(FLD_I2S_PORT_DIR, 0x00)  |
			cx231xx_set_field(FLD_I2S_OUT_SRC, 0x00)   |
			cx231xx_set_field(FLD_AUD_CHAN3_SRC, 0x00) |
			cx231xx_set_field(FLD_AUD_CHAN2_SRC, 0x00) |
			cx231xx_set_field(FLD_AUD_CHAN1_SRC, 0x03), 4);

		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   PATH1_CTL1, 2, 0x1F063870, 4);

		/* setAudioStandard(_audio_standard); */

		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   PATH1_CTL1, 2, 0x00063870, 4);
		switch (dev->model) {
		case CX231XX_BOARD_CNXT_RDE_250:
		case CX231XX_BOARD_CNXT_RDU_250:
			status = cx231xx_read_modify_write_i2c_dword(dev,
					HAMMERHEAD_I2C_ADDRESS,
					CHIP_CTRL,
					FLD_SIF_EN,
					cx231xx_set_field(FLD_SIF_EN, 1));
			break;
		default:
			break;
		}
		break;

	case AUDIO_INPUT_TUNER_FM:
		/*  use SIF for FM radio
		   setupFM();
		   setAudioStandard(_audio_standard);
		 */
		break;

	case AUDIO_INPUT_MUTE:
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   PATH1_CTL1, 2, 0x1F011012, 4);
		break;
	}

	/* Take it out of soft reset */
	status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
				       GENERAL_CTL, 2,  &gen_ctrl, 1);
	gen_ctrl &= ~1;
	status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					GENERAL_CTL, 2, gen_ctrl, 1);

	return status;
}

/* Set resolution of the video */
int cx231xx_resolution_set(struct cx231xx *dev)
{
	int width, height;
	u32 hscale, vscale;
	int status = 0;

	width = dev->width;
	height = dev->height;

	get_scale(dev, width, height, &hscale, &vscale);

	/* set horzontal scale */
	status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					HSCALE_CTRL, 2, hscale, 4);

	/* set vertical scale */
	status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					VSCALE_CTRL, 2, vscale, 4);

	return status;
}

/******************************************************************************
 *                    C H I P Specific  C O N T R O L   functions             *
 ******************************************************************************/
int cx231xx_init_ctrl_pin_status(struct cx231xx *dev)
{
	u32 value;
	int status = 0;

	status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS, PIN_CTRL,
				       2, &value, 4);
	value |= (~dev->board.ctl_pin_status_mask);
	status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS, PIN_CTRL,
					2, value, 4);

	return status;
}

int cx231xx_set_agc_analog_digital_mux_select(struct cx231xx *dev,
					      u8 analog_or_digital)
{
	int status = 0;

	/* first set the direction to output */
	status = cx231xx_set_gpio_direction(dev,
					    dev->board.
					    agc_analog_digital_select_gpio, 1);

	/* 0 - demod ; 1 - Analog mode */
	status = cx231xx_set_gpio_value(dev,
				   dev->board.agc_analog_digital_select_gpio,
				   analog_or_digital);

	return status;
}

int cx231xx_enable_i2c_for_tuner(struct cx231xx *dev, u8 I2CIndex)
{
	u8 value[4] = { 0, 0, 0, 0 };
	int status = 0;

	cx231xx_info("Changing the i2c port for tuner to %d\n", I2CIndex);

	status = cx231xx_read_ctrl_reg(dev, VRT_GET_REGISTER,
				       PWR_CTL_EN, value, 4);
	if (status < 0)
		return status;

	if (I2CIndex == I2C_1) {
		if (value[0] & I2C_DEMOD_EN) {
			value[0] &= ~I2C_DEMOD_EN;
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
						   PWR_CTL_EN, value, 4);
		}
	} else {
		if (!(value[0] & I2C_DEMOD_EN)) {
			value[0] |= I2C_DEMOD_EN;
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
						   PWR_CTL_EN, value, 4);
		}
	}

	return status;

}

/******************************************************************************
 *                 D I F - B L O C K    C O N T R O L   functions             *
 ******************************************************************************/
int cx231xx_dif_configure_C2HH_for_low_IF(struct cx231xx *dev, u32 mode,
					  u32 function_mode, u32 standard)
{
	int status = 0;

	if (mode == V4L2_TUNER_RADIO) {
		/* C2HH */
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
				AFE_CTRL_C2HH_SRC_CTRL, 30, 31, 0x1);	/* lo if big signal */
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
				AFE_CTRL_C2HH_SRC_CTRL, 23, 24, function_mode);	/* FUNC_MODE = DIF */
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
				AFE_CTRL_C2HH_SRC_CTRL, 15, 22, 0xFF);	/* IF_MODE */
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
				AFE_CTRL_C2HH_SRC_CTRL, 9, 9, 0x1);	/* no inv */
	} else {
		switch (standard) {
		case V4L2_STD_NTSC_M:	/* 75 IRE Setup */
		case V4L2_STD_NTSC_M_JP:	/* Japan,  0 IRE Setup */
		case V4L2_STD_PAL_M:
		case V4L2_STD_PAL_N:
		case V4L2_STD_PAL_Nc:
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 30, 31, 0x1);	/* lo if big signal */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 23, 24,
					function_mode);	/* FUNC_MODE = DIF */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 15, 22, 0xb);	/* IF_MODE */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 9, 9, 0x1);	/* no inv */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AUD_IO_CTRL, 0, 31, 0x00000003);	/* 0x124, AUD_CHAN1_SRC = 0x3 */
			break;

		case V4L2_STD_PAL_B:
		case V4L2_STD_PAL_G:
			/* C2HH setup */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 30, 31, 0x1);	/* lo if big signal */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 23, 24,
					function_mode);	/* FUNC_MODE = DIF */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 15, 22, 0xE);	/* IF_MODE */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 9, 9, 0x1);	/* no inv */
			break;

		case V4L2_STD_PAL_D:
		case V4L2_STD_PAL_I:
		case V4L2_STD_SECAM_L:
		case V4L2_STD_SECAM_LC:
		case V4L2_STD_SECAM_B:
		case V4L2_STD_SECAM_D:
		case V4L2_STD_SECAM_G:
		case V4L2_STD_SECAM_K:
		case V4L2_STD_SECAM_K1:
			/* C2HH setup */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 30, 31, 0x1);	/* lo if big signal */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 23, 24,
					function_mode);	/* FUNC_MODE = DIF */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 15, 22, 0xF);	/* IF_MODE */
			status = cx231xx_reg_mask_write(dev,
					HAMMERHEAD_I2C_ADDRESS, 32,
					AFE_CTRL_C2HH_SRC_CTRL, 9, 9, 0x1);	/* no inv */
			break;

		case DIF_USE_BASEBAND:
		default:
			/* do nothing to config C2HH for baseband */
			break;
		}
	}

	return status;
}

int cx231xx_dif_set_standard(struct cx231xx *dev, u32 standard)
{
	int status = 0;
	u32 dif_misc_ctrl_value = 0;
	u32 func_mode = 0;

	cx231xx_info("%s: setStandard to %x\n", __func__, standard);

	status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
				       DIF_MISC_CTRL, 2, &dif_misc_ctrl_value,
				       4);
	if (standard != DIF_USE_BASEBAND)
		dev->norm = standard;

	switch (dev->model) {
	case CX231XX_BOARD_CNXT_RDE_250:
	case CX231XX_BOARD_CNXT_RDU_250:
		func_mode = 0x03;
		break;
	default:
		func_mode = 0x01;
	}

	status = cx231xx_dif_configure_C2HH_for_low_IF(dev, dev->active_mode,
						  func_mode, standard);

	if (standard == DIF_USE_BASEBAND) {	/* base band */
		/* There is a different SRC_PHASE_INC value
		   for baseband vs. DIF */
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
						DIF_SRC_PHASE_INC, 2, 0xDF7DF83,
						4);
		status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					       DIF_MISC_CTRL, 2,
					       &dif_misc_ctrl_value, 4);
		dif_misc_ctrl_value |= FLD_DIF_DIF_BYPASS;
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
						DIF_MISC_CTRL, 2,
						dif_misc_ctrl_value, 4);

	} else if (standard & (V4L2_STD_PAL_B | V4L2_STD_PAL_G)) {

		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL, 0, 31, 0x6503bc0c);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL1, 0, 31, 0xbd038c85);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL2, 0, 31, 0x1db4640a);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL3, 0, 31, 0x00008800);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_REF, 0, 31, 0x444C1380);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_IF, 0, 31, 0xDA302600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_INT, 0, 31, 0xDA261700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_RF, 0, 31, 0xDA262600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_INT_CURRENT, 0, 31,
					   0x26001700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_RF_CURRENT, 0, 31,
					   0x00002660);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VIDEO_AGC_CTRL, 0, 31,
					   0x72500800);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VID_AUD_OVERRIDE, 0, 31,
					   0x27000100);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AV_SEP_CTRL, 0, 31, 0x3F3530EC);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_COMP_FLT_CTRL, 0, 31,
					   0x00A653A8);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_PHASE_INC, 0, 31,
					   0x1befbf06);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_GAIN_CONTROL, 0, 31,
					   0x000035e8);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_RPT_VARIANCE, 0, 31, 0x00000000);
		/* Save the Spec Inversion value */
		dif_misc_ctrl_value &= FLD_DIF_SPEC_INV;
		dif_misc_ctrl_value |= 0x3a013F11;

	} else if (standard & V4L2_STD_PAL_D) {
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL, 0, 31, 0x6503bc0c);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL1, 0, 31, 0xbd038c85);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL2, 0, 31, 0x1db4640a);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL3, 0, 31, 0x00008800);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_REF, 0, 31, 0x444C1380);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_IF, 0, 31, 0xDA302600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_INT, 0, 31, 0xDA261700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_RF, 0, 31, 0xDA262600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_INT_CURRENT, 0, 31,
					   0x26001700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_RF_CURRENT, 0, 31,
					   0x00002660);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VIDEO_AGC_CTRL, 0, 31,
					   0x72500800);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VID_AUD_OVERRIDE, 0, 31,
					   0x27000100);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AV_SEP_CTRL, 0, 31, 0x3F3934EA);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_COMP_FLT_CTRL, 0, 31,
					   0x00000000);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_PHASE_INC, 0, 31,
					   0x1befbf06);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_GAIN_CONTROL, 0, 31,
					   0x000035e8);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_RPT_VARIANCE, 0, 31, 0x00000000);
		/* Save the Spec Inversion value */
		dif_misc_ctrl_value &= FLD_DIF_SPEC_INV;
		dif_misc_ctrl_value |= 0x3a023F11;

	} else if (standard & V4L2_STD_PAL_I) {

		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL, 0, 31, 0x6503bc0c);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL1, 0, 31, 0xbd038c85);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL2, 0, 31, 0x1db4640a);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL3, 0, 31, 0x00008800);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_REF, 0, 31, 0x444C1380);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_IF, 0, 31, 0xDA302600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_INT, 0, 31, 0xDA261700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_RF, 0, 31, 0xDA262600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_INT_CURRENT, 0, 31,
					   0x26001700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_RF_CURRENT, 0, 31,
					   0x00002660);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VIDEO_AGC_CTRL, 0, 31,
					   0x72500800);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VID_AUD_OVERRIDE, 0, 31,
					   0x27000100);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AV_SEP_CTRL, 0, 31, 0x5F39A934);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_COMP_FLT_CTRL, 0, 31,
					   0x00000000);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_PHASE_INC, 0, 31,
					   0x1befbf06);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_GAIN_CONTROL, 0, 31,
					   0x000035e8);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_RPT_VARIANCE, 0, 31, 0x00000000);
		/* Save the Spec Inversion value */
		dif_misc_ctrl_value &= FLD_DIF_SPEC_INV;
		dif_misc_ctrl_value |= 0x3a033F11;

	} else if (standard & V4L2_STD_PAL_M) {
		/* improved Low Frequency Phase Noise */
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
						DIF_PLL_CTRL, 2, 0xFF01FF0C, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
						DIF_PLL_CTRL1, 2, 0xbd038c85,
						4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL2, 2, 0x1db4640a, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL3, 2, 0x00008800, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_IF_REF, 2, 0x444C1380, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_IF_INT_CURRENT, 2,
					   0x26001700, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_RF_CURRENT, 2, 0x00002660,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_VIDEO_AGC_CTRL, 2, 0x72500800,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_VID_AUD_OVERRIDE, 2, 0x27000100,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AV_SEP_CTRL, 2, 0x012c405d, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_COMP_FLT_CTRL, 2, 0x009f50c1, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_SRC_PHASE_INC, 2, 0x1befbf06, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_SRC_GAIN_CONTROL, 2, 0x000035e8,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_SOFT_RST_CTRL_REVB, 2,
					   0x00000000, 4);

		/* Save the Spec Inversion value */
		dif_misc_ctrl_value &= FLD_DIF_SPEC_INV;
		dif_misc_ctrl_value |= 0x3A0A3F10;

	} else if (standard & (V4L2_STD_PAL_N | V4L2_STD_PAL_Nc)) {

		/* improved Low Frequency Phase Noise */
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL, 2, 0xFF01FF0C, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL1, 2, 0xbd038c85, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL2, 2, 0x1db4640a, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL3, 2, 0x00008800, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_IF_REF, 2, 0x444C1380, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_IF_INT_CURRENT, 2,
					   0x26001700, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_RF_CURRENT, 2, 0x00002660,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_VIDEO_AGC_CTRL, 2, 0x72500800,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_VID_AUD_OVERRIDE, 2, 0x27000100,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AV_SEP_CTRL, 2, 0x012c405d, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_COMP_FLT_CTRL, 2, 0x009f50c1, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_SRC_PHASE_INC, 2, 0x1befbf06, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_SRC_GAIN_CONTROL, 2, 0x000035e8,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_SOFT_RST_CTRL_REVB, 2,
					   0x00000000, 4);

		/* Save the Spec Inversion value */
		dif_misc_ctrl_value &= FLD_DIF_SPEC_INV;
		dif_misc_ctrl_value = 0x3A093F10;

	} else if (standard &
		   (V4L2_STD_SECAM_B | V4L2_STD_SECAM_D | V4L2_STD_SECAM_G |
		    V4L2_STD_SECAM_K | V4L2_STD_SECAM_K1)) {

		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL, 0, 31, 0x6503bc0c);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL1, 0, 31, 0xbd038c85);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL2, 0, 31, 0x1db4640a);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL3, 0, 31, 0x00008800);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_REF, 0, 31, 0x888C0380);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_IF, 0, 31, 0xe0262600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_INT, 0, 31, 0xc2171700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_RF, 0, 31, 0xc2262600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_INT_CURRENT, 0, 31,
					   0x26001700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_RF_CURRENT, 0, 31,
					   0x00002660);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VID_AUD_OVERRIDE, 0, 31,
					   0x27000100);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AV_SEP_CTRL, 0, 31, 0x3F3530ec);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_COMP_FLT_CTRL, 0, 31,
					   0x00000000);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_PHASE_INC, 0, 31,
					   0x1befbf06);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_GAIN_CONTROL, 0, 31,
					   0x000035e8);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_RPT_VARIANCE, 0, 31, 0x00000000);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VIDEO_AGC_CTRL, 0, 31,
					   0xf4000000);

		/* Save the Spec Inversion value */
		dif_misc_ctrl_value &= FLD_DIF_SPEC_INV;
		dif_misc_ctrl_value |= 0x3a023F11;

	} else if (standard & (V4L2_STD_SECAM_L | V4L2_STD_SECAM_LC)) {

		/* Is it SECAM_L1? */
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL, 0, 31, 0x6503bc0c);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL1, 0, 31, 0xbd038c85);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL2, 0, 31, 0x1db4640a);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_PLL_CTRL3, 0, 31, 0x00008800);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_REF, 0, 31, 0x888C0380);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_IF, 0, 31, 0xe0262600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_INT, 0, 31, 0xc2171700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_CTRL_RF, 0, 31, 0xc2262600);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_IF_INT_CURRENT, 0, 31,
					   0x26001700);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AGC_RF_CURRENT, 0, 31,
					   0x00002660);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VID_AUD_OVERRIDE, 0, 31,
					   0x27000100);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_AV_SEP_CTRL, 0, 31, 0x3F3530ec);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_COMP_FLT_CTRL, 0, 31,
					   0x00000000);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_PHASE_INC, 0, 31,
					   0x1befbf06);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_SRC_GAIN_CONTROL, 0, 31,
					   0x000035e8);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_RPT_VARIANCE, 0, 31, 0x00000000);
		status = cx231xx_reg_mask_write(dev, HAMMERHEAD_I2C_ADDRESS, 32,
					   DIF_VIDEO_AGC_CTRL, 0, 31,
					   0xf2560000);

		/* Save the Spec Inversion value */
		dif_misc_ctrl_value &= FLD_DIF_SPEC_INV;
		dif_misc_ctrl_value |= 0x3a023F11;

	} else {
		/* V4L2_STD_NTSC_M (75 IRE Setup) Or
		   V4L2_STD_NTSC_M_JP (Japan,  0 IRE Setup) */

		/* For NTSC the centre frequency of video coming out of
		   sidewinder is around 7.1MHz or 3.6MHz depending on the
		   spectral inversion. so for a non spectrally inverted channel
		   the pll freq word is 0x03420c49
		 */

		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL, 2, 0x6503BC0C, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL1, 2, 0xBD038C85, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL2, 2, 0x1DB4640A, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_PLL_CTRL3, 2, 0x00008800, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_IF_REF, 2, 0x444C0380, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_IF_INT_CURRENT, 2,
					   0x26001700, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_RF_CURRENT, 2, 0x00002660,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_VIDEO_AGC_CTRL, 2, 0x04000800,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_VID_AUD_OVERRIDE, 2, 0x27000100,
					   4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AV_SEP_CTRL, 2, 0x01296e1f, 4);

		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_COMP_FLT_CTRL, 2, 0x009f50c1, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_SRC_PHASE_INC, 2, 0x1befbf06, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_SRC_GAIN_CONTROL, 2, 0x000035e8,
					   4);

		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_CTRL_IF, 2, 0xC2262600, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_CTRL_INT, 2, 0xC2262600, 4);
		status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					   DIF_AGC_CTRL_RF, 2, 0xC2262600, 4);

		/* Save the Spec Inversion value */
		dif_misc_ctrl_value &= FLD_DIF_SPEC_INV;
		dif_misc_ctrl_value |= 0x3a003F10;

	}

	/* The AGC values should be the same for all standards,
	   AUD_SRC_SEL[19] should always be disabled    */
	dif_misc_ctrl_value &= ~FLD_DIF_AUD_SRC_SEL;

	/* It is still possible to get Set Standard calls even when we
	   are in FM mode.
	   This is done to override the value for FM. */
	if (dev->active_mode == V4L2_TUNER_RADIO)
		dif_misc_ctrl_value = 0x7a080000;

	/* Write the calculated value for misc ontrol register      */
	status =
	    cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS, DIF_MISC_CTRL,
				   2, dif_misc_ctrl_value, 4);

	return status;
}

int cx231xx_tuner_pre_channel_change(struct cx231xx *dev)
{
	int status = 0;
	u32 dwval;

	/* Set the RF and IF k_agc values to 3 */
	status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
				       DIF_AGC_IF_REF, 2, &dwval, 4);
	dwval &= ~(FLD_DIF_K_AGC_RF | FLD_DIF_K_AGC_IF);
	dwval |= 0x33000000;

	status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					DIF_AGC_IF_REF, 2, dwval, 4);

	return status;
}

int cx231xx_tuner_post_channel_change(struct cx231xx *dev)
{
	int status = 0;
	u32 dwval;

	/* Set the RF and IF k_agc values to 4 for PAL/NTSC and 8 for SECAM */
	status = cx231xx_read_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
				       DIF_AGC_IF_REF, 2, &dwval, 4);
	dwval &= ~(FLD_DIF_K_AGC_RF | FLD_DIF_K_AGC_IF);

	if (dev->norm & (V4L2_STD_SECAM_L | V4L2_STD_SECAM_B |
			 V4L2_STD_SECAM_D))
		dwval |= 0x88000000;
	else
		dwval |= 0x44000000;

	status = cx231xx_write_i2c_data(dev, HAMMERHEAD_I2C_ADDRESS,
					DIF_AGC_IF_REF, 2, dwval, 4);

	return status;
}

/******************************************************************************
 *        F L A T I R O N - B L O C K    C O N T R O L   functions            *
 ******************************************************************************/
int cx231xx_flatiron_initialize(struct cx231xx *dev)
{
	int status = 0;
	u32 value;

	status = cx231xx_read_i2c_data(dev, Flatrion_DEVICE_ADDRESS,
				       CH_PWR_CTRL1, 1, &value, 1);
	/* enables clock to delta-sigma and decimation filter */
	value |= 0x80;
	status = cx231xx_write_i2c_data(dev, Flatrion_DEVICE_ADDRESS,
					CH_PWR_CTRL1, 1, value, 1);
	/* power up all channel */
	status = cx231xx_write_i2c_data(dev, Flatrion_DEVICE_ADDRESS,
					CH_PWR_CTRL2, 1, 0x00, 1);

	return status;
}

int cx231xx_flatiron_update_power_control(struct cx231xx *dev, AV_MODE avmode)
{
	int status = 0;
	u32 value = 0;

	if (avmode != POLARIS_AVMODE_ENXTERNAL_AV) {
		status = cx231xx_read_i2c_data(dev, Flatrion_DEVICE_ADDRESS,
					  CH_PWR_CTRL2, 1, &value, 1);
		value |= 0xfe;
		status = cx231xx_write_i2c_data(dev, Flatrion_DEVICE_ADDRESS,
						CH_PWR_CTRL2, 1, value, 1);
	} else {
		status = cx231xx_write_i2c_data(dev, Flatrion_DEVICE_ADDRESS,
						CH_PWR_CTRL2, 1, 0x00, 1);
	}

	return status;
}

/* set flatiron for audio input types */
int cx231xx_flatiron_set_audio_input(struct cx231xx *dev, u8 audio_input)
{
	int status = 0;

	switch (audio_input) {
	case CX231XX_AMUX_LINE_IN:
		status = cx231xx_write_i2c_data(dev, Flatrion_DEVICE_ADDRESS,
						CH_PWR_CTRL2, 1, 0x00, 1);
		status = cx231xx_write_i2c_data(dev, Flatrion_DEVICE_ADDRESS,
						CH_PWR_CTRL1, 1, 0x80, 1);
		break;
	case CX231XX_AMUX_VIDEO:
	default:
		break;
	}

	dev->ctl_ainput = audio_input;

	return status;
}

/******************************************************************************
 *                  P O W E R      C O N T R O L   functions                  *
 ******************************************************************************/
int cx231xx_set_power_mode(struct cx231xx *dev, AV_MODE mode)
{
	u8 value[4] = { 0, 0, 0, 0 };
	u32 tmp = 0;
	int status = 0;

	if (dev->power_mode != mode)
		dev->power_mode = mode;
	else {
		cx231xx_info(" setPowerMode::mode = %d, No Change req.\n",
			     mode);
		return 0;
	}

	cx231xx_info(" setPowerMode::mode = %d\n", mode);

	status = cx231xx_read_ctrl_reg(dev, VRT_GET_REGISTER, PWR_CTL_EN, value,
				       4);
	if (status < 0)
		return status;

	tmp = *((u32 *) value);

	switch (mode) {
	case POLARIS_AVMODE_ENXTERNAL_AV:

		tmp &= (~PWR_MODE_MASK);

		tmp |= PWR_AV_EN;
		value[0] = (u8) tmp;
		value[1] = (u8) (tmp >> 8);
		value[2] = (u8) (tmp >> 16);
		value[3] = (u8) (tmp >> 24);
		status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
						PWR_CTL_EN, value, 4);
		msleep(PWR_SLEEP_INTERVAL);

		tmp |= PWR_ISO_EN;
		value[0] = (u8) tmp;
		value[1] = (u8) (tmp >> 8);
		value[2] = (u8) (tmp >> 16);
		value[3] = (u8) (tmp >> 24);
		status =
		    cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER, PWR_CTL_EN,
					   value, 4);
		msleep(PWR_SLEEP_INTERVAL);

		tmp |= POLARIS_AVMODE_ENXTERNAL_AV;
		value[0] = (u8) tmp;
		value[1] = (u8) (tmp >> 8);
		value[2] = (u8) (tmp >> 16);
		value[3] = (u8) (tmp >> 24);
		status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
						PWR_CTL_EN, value, 4);

		dev->xc_fw_load_done = 0;	/* reset state of xceive tuner */
		break;

	case POLARIS_AVMODE_ANALOGT_TV:

		tmp &= (~PWR_DEMOD_EN);
		tmp |= (I2C_DEMOD_EN);
		value[0] = (u8) tmp;
		value[1] = (u8) (tmp >> 8);
		value[2] = (u8) (tmp >> 16);
		value[3] = (u8) (tmp >> 24);
		status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
						PWR_CTL_EN, value, 4);
		msleep(PWR_SLEEP_INTERVAL);

		if (!(tmp & PWR_TUNER_EN)) {
			tmp |= (PWR_TUNER_EN);
			value[0] = (u8) tmp;
			value[1] = (u8) (tmp >> 8);
			value[2] = (u8) (tmp >> 16);
			value[3] = (u8) (tmp >> 24);
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
							PWR_CTL_EN, value, 4);
			msleep(PWR_SLEEP_INTERVAL);
		}

		if (!(tmp & PWR_AV_EN)) {
			tmp |= PWR_AV_EN;
			value[0] = (u8) tmp;
			value[1] = (u8) (tmp >> 8);
			value[2] = (u8) (tmp >> 16);
			value[3] = (u8) (tmp >> 24);
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
							PWR_CTL_EN, value, 4);
			msleep(PWR_SLEEP_INTERVAL);
		}
		if (!(tmp & PWR_ISO_EN)) {
			tmp |= PWR_ISO_EN;
			value[0] = (u8) tmp;
			value[1] = (u8) (tmp >> 8);
			value[2] = (u8) (tmp >> 16);
			value[3] = (u8) (tmp >> 24);
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
							PWR_CTL_EN, value, 4);
			msleep(PWR_SLEEP_INTERVAL);
		}

		if (!(tmp & POLARIS_AVMODE_ANALOGT_TV)) {
			tmp |= POLARIS_AVMODE_ANALOGT_TV;
			value[0] = (u8) tmp;
			value[1] = (u8) (tmp >> 8);
			value[2] = (u8) (tmp >> 16);
			value[3] = (u8) (tmp >> 24);
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
							PWR_CTL_EN, value, 4);
			msleep(PWR_SLEEP_INTERVAL);
		}

		if ((dev->model == CX231XX_BOARD_CNXT_RDE_250) ||
		    (dev->model == CX231XX_BOARD_CNXT_RDU_250)) {
			/* tuner path to channel 1 from port 3 */
			cx231xx_enable_i2c_for_tuner(dev, I2C_3);

			if (dev->cx231xx_reset_analog_tuner)
				dev->cx231xx_reset_analog_tuner(dev);
		}
		break;

	case POLARIS_AVMODE_DIGITAL:
		if (!(tmp & PWR_TUNER_EN)) {
			tmp |= (PWR_TUNER_EN);
			value[0] = (u8) tmp;
			value[1] = (u8) (tmp >> 8);
			value[2] = (u8) (tmp >> 16);
			value[3] = (u8) (tmp >> 24);
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
							PWR_CTL_EN, value, 4);
			msleep(PWR_SLEEP_INTERVAL);
		}
		if (!(tmp & PWR_AV_EN)) {
			tmp |= PWR_AV_EN;
			value[0] = (u8) tmp;
			value[1] = (u8) (tmp >> 8);
			value[2] = (u8) (tmp >> 16);
			value[3] = (u8) (tmp >> 24);
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
							PWR_CTL_EN, value, 4);
			msleep(PWR_SLEEP_INTERVAL);
		}
		if (!(tmp & PWR_ISO_EN)) {
			tmp |= PWR_ISO_EN;
			value[0] = (u8) tmp;
			value[1] = (u8) (tmp >> 8);
			value[2] = (u8) (tmp >> 16);
			value[3] = (u8) (tmp >> 24);
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
							PWR_CTL_EN, value, 4);
			msleep(PWR_SLEEP_INTERVAL);
		}

		tmp |= POLARIS_AVMODE_DIGITAL | I2C_DEMOD_EN;
		value[0] = (u8) tmp;
		value[1] = (u8) (tmp >> 8);
		value[2] = (u8) (tmp >> 16);
		value[3] = (u8) (tmp >> 24);
		status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
						PWR_CTL_EN, value, 4);
		msleep(PWR_SLEEP_INTERVAL);

		if (!(tmp & PWR_DEMOD_EN)) {
			tmp |= PWR_DEMOD_EN;
			value[0] = (u8) tmp;
			value[1] = (u8) (tmp >> 8);
			value[2] = (u8) (tmp >> 16);
			value[3] = (u8) (tmp >> 24);
			status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
							PWR_CTL_EN, value, 4);
			msleep(PWR_SLEEP_INTERVAL);
		}

		if ((dev->model == CX231XX_BOARD_CNXT_RDE_250) ||
		    (dev->model == CX231XX_BOARD_CNXT_RDU_250)) {
			/* tuner path to channel 1 from port 3 */
			cx231xx_enable_i2c_for_tuner(dev, I2C_3);

			if (dev->cx231xx_reset_analog_tuner)
				dev->cx231xx_reset_analog_tuner(dev);
		}
		break;

	default:
		break;
	}

	msleep(PWR_SLEEP_INTERVAL);

	/* For power saving, only enable Pwr_resetout_n
	   when digital TV is selected. */
	if (mode == POLARIS_AVMODE_DIGITAL) {
		tmp |= PWR_RESETOUT_EN;
		value[0] = (u8) tmp;
		value[1] = (u8) (tmp >> 8);
		value[2] = (u8) (tmp >> 16);
		value[3] = (u8) (tmp >> 24);
		status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER,
						PWR_CTL_EN, value, 4);
		msleep(PWR_SLEEP_INTERVAL);
	}

	/* update power control for colibri */
	status = cx231xx_colibri_update_power_control(dev, mode);

	/* update power control for flatiron */
	status = cx231xx_flatiron_update_power_control(dev, mode);

	status = cx231xx_read_ctrl_reg(dev, VRT_GET_REGISTER, PWR_CTL_EN, value,
				       4);
	cx231xx_info(" The data of PWR_CTL_EN register 0x74=0x%0x,0x%0x,0x%0x,0x%0x\n",
		     value[0], value[1], value[2], value[3]);

	return status;
}

int cx231xx_power_suspend(struct cx231xx *dev)
{
	u8 value[4] = { 0, 0, 0, 0 };
	u32 tmp = 0;
	int status = 0;

	status = cx231xx_read_ctrl_reg(dev, VRT_GET_REGISTER, PWR_CTL_EN,
				       value, 4);
	if (status > 0)
		return status;

	tmp = *((u32 *) value);
	tmp &= (~PWR_MODE_MASK);

	value[0] = (u8) tmp;
	value[1] = (u8) (tmp >> 8);
	value[2] = (u8) (tmp >> 16);
	value[3] = (u8) (tmp >> 24);
	status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER, PWR_CTL_EN,
					value, 4);

	return status;
}

/******************************************************************************
 *                  S T R E A M    C O N T R O L   functions                  *
 ******************************************************************************/
int cx231xx_start_stream(struct cx231xx *dev, u32 ep_mask)
{
	u8 value[4] = { 0x0, 0x0, 0x0, 0x0 };
	u32 tmp = 0;
	int status = 0;

	cx231xx_info("cx231xx_start_stream():: ep_mask = %x\n", ep_mask);
	status = cx231xx_read_ctrl_reg(dev, VRT_GET_REGISTER, EP_MODE_SET,
				       value, 4);
	if (status < 0)
		return status;

	tmp = *((u32 *) value);
	tmp |= ep_mask;
	value[0] = (u8) tmp;
	value[1] = (u8) (tmp >> 8);
	value[2] = (u8) (tmp >> 16);
	value[3] = (u8) (tmp >> 24);

	status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER, EP_MODE_SET,
					value, 4);

	return status;
}

int cx231xx_stop_stream(struct cx231xx *dev, u32 ep_mask)
{
	u8 value[4] = { 0x0, 0x0, 0x0, 0x0 };
	u32 tmp = 0;
	int status = 0;

	cx231xx_info("cx231xx_stop_stream():: ep_mask = %x\n", ep_mask);
	status =
	    cx231xx_read_ctrl_reg(dev, VRT_GET_REGISTER, EP_MODE_SET, value, 4);
	if (status < 0)
		return status;

	tmp = *((u32 *) value);
	tmp &= (~ep_mask);
	value[0] = (u8) tmp;
	value[1] = (u8) (tmp >> 8);
	value[2] = (u8) (tmp >> 16);
	value[3] = (u8) (tmp >> 24);

	status = cx231xx_write_ctrl_reg(dev, VRT_SET_REGISTER, EP_MODE_SET,
					value, 4);

	return status;
}

int cx231xx_initialize_stream_xfer(struct cx231xx *dev, u32 media_type)
{
	int status = 0;

	if (dev->udev->speed == USB_SPEED_HIGH) {
		switch (media_type) {
		case 81:	/* audio */
			cx231xx_info("%s: Audio enter HANC\n", __func__);
			status =
			    cx231xx_mode_register(dev, TS_MODE_REG, 0x9300);
			break;

		case 2:	/* vbi */
			cx231xx_info("%s: set vanc registers\n", __func__);
			status = cx231xx_mode_register(dev, TS_MODE_REG, 0x300);
			break;

		case 3:	/* sliced cc */
			cx231xx_info("%s: set hanc registers\n", __func__);
			status =
			    cx231xx_mode_register(dev, TS_MODE_REG, 0x1300);
			break;

		case 0:	/* video */
			cx231xx_info("%s: set video registers\n", __func__);
			status = cx231xx_mode_register(dev, TS_MODE_REG, 0x100);
			break;

		case 4:	/* ts1 */
			cx231xx_info("%s: set ts1 registers\n", __func__);
			status = cx231xx_mode_register(dev, TS_MODE_REG, 0x101);
			status = cx231xx_mode_register(dev, TS1_CFG_REG, 0x400);
			break;
		case 6:	/* ts1 parallel mode */
			cx231xx_info("%s: set ts1 parrallel mode registers\n",
				     __func__);
			status = cx231xx_mode_register(dev, TS_MODE_REG, 0x100);
			status = cx231xx_mode_register(dev, TS1_CFG_REG, 0x400);
			break;
		}
	} else {
		status = cx231xx_mode_register(dev, TS_MODE_REG, 0x101);
	}

	return status;
}

int cx231xx_capture_start(struct cx231xx *dev, int start, u8 media_type)
{
	int rc;
	u32 ep_mask = -1;
	PPCB_CONFIG pcb_config;

	/* get EP for media type */
	pcb_config = &dev->current_pcb_config;

	if (pcb_config->config_num == 1) {
		switch (media_type) {
		case 0:	/* Video */
			ep_mask = ENABLE_EP4;	/* ep4  [00:1000] */
			break;
		case 1:	/* Audio */
			ep_mask = ENABLE_EP3;	/* ep3  [00:0100] */
			break;
		case 2:	/* Vbi */
			ep_mask = ENABLE_EP5;	/* ep5 [01:0000] */
			break;
		case 3:	/* Sliced_cc */
			ep_mask = ENABLE_EP6;	/* ep6 [10:0000] */
			break;
		case 4:	/* ts1 */
		case 6:	/* ts1 parallel mode */
			ep_mask = ENABLE_EP1;	/* ep1 [00:0001] */
			break;
		case 5:	/* ts2 */
			ep_mask = ENABLE_EP2;	/* ep2 [00:0010] */
			break;
		}

	} else if (pcb_config->config_num > 1) {
		switch (media_type) {
		case 0:	/* Video */
			ep_mask = ENABLE_EP4;	/* ep4  [00:1000] */
			break;
		case 1:	/* Audio */
			ep_mask = ENABLE_EP3;	/* ep3  [00:0100] */
			break;
		case 2:	/* Vbi */
			ep_mask = ENABLE_EP5;	/* ep5 [01:0000] */
			break;
		case 3:	/* Sliced_cc */
			ep_mask = ENABLE_EP6;	/* ep6 [10:0000] */
			break;
		case 4:	/* ts1 */
		case 6:	/* ts1 parallel mode */
			ep_mask = ENABLE_EP1;	/* ep1 [00:0001] */
			break;
		case 5:	/* ts2 */
			ep_mask = ENABLE_EP2;	/* ep2 [00:0010] */
			break;
		}

	}

	if (start) {
		rc = cx231xx_initialize_stream_xfer(dev, media_type);

		if (rc < 0)
			return rc;

		/* enable video capture */
		if (ep_mask > 0)
			rc = cx231xx_start_stream(dev, ep_mask);
	} else {
		/* disable video capture */
		if (ep_mask > 0)
			rc = cx231xx_stop_stream(dev, ep_mask);
	}


	return rc;
}
EXPORT_SYMBOL_GPL(cx231xx_capture_start);

/*****************************************************************************
*                   G P I O   B I T control functions                        *
******************************************************************************/
int cx231xx_set_gpio_bit(struct cx231xx *dev, u32 gpio_bit, u8 * gpio_val)
{
	int status = 0;

	status = cx231xx_send_gpio_cmd(dev, gpio_bit, gpio_val, 4, 0, 0);

	return status;
}

int cx231xx_get_gpio_bit(struct cx231xx *dev, u32 gpio_bit, u8 * gpio_val)
{
	int status = 0;

	status = cx231xx_send_gpio_cmd(dev, gpio_bit, gpio_val, 4, 0, 1);

	return status;
}

/*
* cx231xx_set_gpio_direction
*      Sets the direction of the GPIO pin to input or output
*
* Parameters :
*      pin_number : The GPIO Pin number to program the direction for
*                   from 0 to 31
*      pin_value : The Direction of the GPIO Pin under reference.
*                      0 = Input direction
*                      1 = Output direction
*/
int cx231xx_set_gpio_direction(struct cx231xx *dev,
			       int pin_number, int pin_value)
{
	int status = 0;
	u32 value = 0;

	/* Check for valid pin_number - if 32 , bail out */
	if (pin_number >= 32)
		return -EINVAL;

	/* input */
	if (pin_value == 0)
		value = dev->gpio_dir & (~(1 << pin_number));	/* clear */
	else
		value = dev->gpio_dir | (1 << pin_number);

	status = cx231xx_set_gpio_bit(dev, value, (u8 *) &dev->gpio_val);

	/* cache the value for future */
	dev->gpio_dir = value;

	return status;
}

/*
* SetGpioPinLogicValue
*      Sets the value of the GPIO pin to Logic high or low. The Pin under
*      reference should ALREADY BE SET IN OUTPUT MODE !!!!!!!!!
*
* Parameters :
*      pin_number : The GPIO Pin number to program the direction for
*      pin_value : The value of the GPIO Pin under reference.
*                      0 = set it to 0
*                      1 = set it to 1
*/
int cx231xx_set_gpio_value(struct cx231xx *dev, int pin_number, int pin_value)
{
	int status = 0;
	u32 value = 0;

	/* Check for valid pin_number - if 0xFF , bail out */
	if (pin_number >= 32)
		return -EINVAL;

	/* first do a sanity check - if the Pin is not output, make it output */
	if ((dev->gpio_dir & (1 << pin_number)) == 0x00) {
		/* It was in input mode */
		value = dev->gpio_dir | (1 << pin_number);
		dev->gpio_dir = value;
		status = cx231xx_set_gpio_bit(dev, dev->gpio_dir,
					      (u8 *) &dev->gpio_val);
		value = 0;
	}

	if (pin_value == 0)
		value = dev->gpio_val & (~(1 << pin_number));
	else
		value = dev->gpio_val | (1 << pin_number);

	/* store the value */
	dev->gpio_val = value;

	/* toggle bit0 of GP_IO */
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	return status;
}

/*****************************************************************************
*                      G P I O I2C related functions                         *
******************************************************************************/
int cx231xx_gpio_i2c_start(struct cx231xx *dev)
{
	int status = 0;

	/* set SCL to output 1 ; set SDA to output 1 */
	dev->gpio_dir |= 1 << dev->board.tuner_scl_gpio;
	dev->gpio_dir |= 1 << dev->board.tuner_sda_gpio;
	dev->gpio_val |= 1 << dev->board.tuner_scl_gpio;
	dev->gpio_val |= 1 << dev->board.tuner_sda_gpio;

	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);
	if (status < 0)
		return -EINVAL;

	/* set SCL to output 1; set SDA to output 0 */
	dev->gpio_val |= 1 << dev->board.tuner_scl_gpio;
	dev->gpio_val &= ~(1 << dev->board.tuner_sda_gpio);

	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);
	if (status < 0)
		return -EINVAL;

	/* set SCL to output 0; set SDA to output 0      */
	dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
	dev->gpio_val &= ~(1 << dev->board.tuner_sda_gpio);

	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);
	if (status < 0)
		return -EINVAL;

	return status;
}

int cx231xx_gpio_i2c_end(struct cx231xx *dev)
{
	int status = 0;

	/* set SCL to output 0; set SDA to output 0      */
	dev->gpio_dir |= 1 << dev->board.tuner_scl_gpio;
	dev->gpio_dir |= 1 << dev->board.tuner_sda_gpio;

	dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
	dev->gpio_val &= ~(1 << dev->board.tuner_sda_gpio);

	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);
	if (status < 0)
		return -EINVAL;

	/* set SCL to output 1; set SDA to output 0      */
	dev->gpio_val |= 1 << dev->board.tuner_scl_gpio;
	dev->gpio_val &= ~(1 << dev->board.tuner_sda_gpio);

	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);
	if (status < 0)
		return -EINVAL;

	/* set SCL to input ,release SCL cable control
	   set SDA to input ,release SDA cable control */
	dev->gpio_dir &= ~(1 << dev->board.tuner_scl_gpio);
	dev->gpio_dir &= ~(1 << dev->board.tuner_sda_gpio);

	status =
	    cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);
	if (status < 0)
		return -EINVAL;

	return status;
}

int cx231xx_gpio_i2c_write_byte(struct cx231xx *dev, u8 data)
{
	int status = 0;
	u8 i;

	/* set SCL to output ; set SDA to output */
	dev->gpio_dir |= 1 << dev->board.tuner_scl_gpio;
	dev->gpio_dir |= 1 << dev->board.tuner_sda_gpio;

	for (i = 0; i < 8; i++) {
		if (((data << i) & 0x80) == 0) {
			/* set SCL to output 0; set SDA to output 0     */
			dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
			dev->gpio_val &= ~(1 << dev->board.tuner_sda_gpio);
			status = cx231xx_set_gpio_bit(dev, dev->gpio_dir,
						      (u8 *)&dev->gpio_val);

			/* set SCL to output 1; set SDA to output 0     */
			dev->gpio_val |= 1 << dev->board.tuner_scl_gpio;
			status = cx231xx_set_gpio_bit(dev, dev->gpio_dir,
						      (u8 *)&dev->gpio_val);

			/* set SCL to output 0; set SDA to output 0     */
			dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
			status = cx231xx_set_gpio_bit(dev, dev->gpio_dir,
						      (u8 *)&dev->gpio_val);
		} else {
			/* set SCL to output 0; set SDA to output 1     */
			dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
			dev->gpio_val |= 1 << dev->board.tuner_sda_gpio;
			status = cx231xx_set_gpio_bit(dev, dev->gpio_dir,
						      (u8 *)&dev->gpio_val);

			/* set SCL to output 1; set SDA to output 1     */
			dev->gpio_val |= 1 << dev->board.tuner_scl_gpio;
			status = cx231xx_set_gpio_bit(dev, dev->gpio_dir,
						      (u8 *)&dev->gpio_val);

			/* set SCL to output 0; set SDA to output 1     */
			dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
			status = cx231xx_set_gpio_bit(dev, dev->gpio_dir,
						      (u8 *)&dev->gpio_val);
		}
	}
	return status;
}

int cx231xx_gpio_i2c_read_byte(struct cx231xx *dev, u8 * buf)
{
	u8 value = 0;
	int status = 0;
	u32 gpio_logic_value = 0;
	u8 i;

	/* read byte */
	for (i = 0; i < 8; i++) {	/* send write I2c addr */

		/* set SCL to output 0; set SDA to input */
		dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
		status = cx231xx_set_gpio_bit(dev, dev->gpio_dir,
					      (u8 *)&dev->gpio_val);

		/* set SCL to output 1; set SDA to input */
		dev->gpio_val |= 1 << dev->board.tuner_scl_gpio;
		status = cx231xx_set_gpio_bit(dev, dev->gpio_dir,
					      (u8 *)&dev->gpio_val);

		/* get SDA data bit */
		gpio_logic_value = dev->gpio_val;
		status = cx231xx_get_gpio_bit(dev, dev->gpio_dir,
					      (u8 *)&dev->gpio_val);
		if ((dev->gpio_val & (1 << dev->board.tuner_sda_gpio)) != 0)
			value |= (1 << (8 - i - 1));

		dev->gpio_val = gpio_logic_value;
	}

	/* set SCL to output 0,finish the read latest SCL signal.
	   !!!set SDA to input, never to modify SDA direction at
	   the same times */
	dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	/* store the value */
	*buf = value & 0xff;

	return status;
}

int cx231xx_gpio_i2c_read_ack(struct cx231xx *dev)
{
	int status = 0;
	u32 gpio_logic_value = 0;
	int nCnt = 10;
	int nInit = nCnt;

	/* clock stretch; set SCL to input; set SDA to input;
	   get SCL value till SCL = 1 */
	dev->gpio_dir &= ~(1 << dev->board.tuner_sda_gpio);
	dev->gpio_dir &= ~(1 << dev->board.tuner_scl_gpio);

	gpio_logic_value = dev->gpio_val;
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	do {
		msleep(2);
		status = cx231xx_get_gpio_bit(dev, dev->gpio_dir,
					      (u8 *)&dev->gpio_val);
		nCnt--;
	} while (((dev->gpio_val & (1 << dev->board.tuner_scl_gpio)) == 0) && (nCnt > 0));

	if (nCnt == 0)
		cx231xx_info("No ACK after %d msec for clock stretch. GPIO I2C operation failed!",
			     nInit * 10);

	/* readAck
	   throuth clock stretch ,slave has given a SCL signal,
	   so the SDA data can be directly read.  */
	status = cx231xx_get_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	if ((dev->gpio_val & 1 << dev->board.tuner_sda_gpio) == 0) {
		dev->gpio_val = gpio_logic_value;
		dev->gpio_val &= ~(1 << dev->board.tuner_sda_gpio);
		status = 0;
	} else {
		dev->gpio_val = gpio_logic_value;
		dev->gpio_val |= (1 << dev->board.tuner_sda_gpio);
	}

	/* read SDA end, set the SCL to output 0, after this operation,
	   SDA direction can be changed. */
	dev->gpio_val = gpio_logic_value;
	dev->gpio_dir |= (1 << dev->board.tuner_scl_gpio);
	dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	return status;
}

int cx231xx_gpio_i2c_write_ack(struct cx231xx *dev)
{
	int status = 0;

	/* set SDA to ouput */
	dev->gpio_dir |= 1 << dev->board.tuner_sda_gpio;
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	/* set SCL = 0 (output); set SDA = 0 (output) */
	dev->gpio_val &= ~(1 << dev->board.tuner_sda_gpio);
	dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	/* set SCL = 1 (output); set SDA = 0 (output) */
	dev->gpio_val |= 1 << dev->board.tuner_scl_gpio;
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	/* set SCL = 0 (output); set SDA = 0 (output) */
	dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	/* set SDA to input,and then the slave will read data from SDA. */
	dev->gpio_dir &= ~(1 << dev->board.tuner_sda_gpio);
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	return status;
}

int cx231xx_gpio_i2c_write_nak(struct cx231xx *dev)
{
	int status = 0;

	/* set scl to output ; set sda to input */
	dev->gpio_dir |= 1 << dev->board.tuner_scl_gpio;
	dev->gpio_dir &= ~(1 << dev->board.tuner_sda_gpio);
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	/* set scl to output 0; set sda to input */
	dev->gpio_val &= ~(1 << dev->board.tuner_scl_gpio);
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	/* set scl to output 1; set sda to input */
	dev->gpio_val |= 1 << dev->board.tuner_scl_gpio;
	status = cx231xx_set_gpio_bit(dev, dev->gpio_dir, (u8 *)&dev->gpio_val);

	return status;
}

/*****************************************************************************
*                      G P I O I2C related functions                         *
******************************************************************************/
/* cx231xx_gpio_i2c_read
 * Function to read data from gpio based I2C interface
 */
int cx231xx_gpio_i2c_read(struct cx231xx *dev, u8 dev_addr, u8 * buf, u8 len)
{
	int status = 0;
	int i = 0;

	/* get the lock */
	mutex_lock(&dev->gpio_i2c_lock);

	/* start */
	status = cx231xx_gpio_i2c_start(dev);

	/* write dev_addr */
	status = cx231xx_gpio_i2c_write_byte(dev, (dev_addr << 1) + 1);

	/* readAck */
	status = cx231xx_gpio_i2c_read_ack(dev);

	/* read data */
	for (i = 0; i < len; i++) {
		/* read data */
		buf[i] = 0;
		status = cx231xx_gpio_i2c_read_byte(dev, &buf[i]);

		if ((i + 1) != len) {
			/* only do write ack if we more length */
			status = cx231xx_gpio_i2c_write_ack(dev);
		}
	}

	/* write NAK - inform reads are complete */
	status = cx231xx_gpio_i2c_write_nak(dev);

	/* write end */
	status = cx231xx_gpio_i2c_end(dev);

	/* release the lock */
	mutex_unlock(&dev->gpio_i2c_lock);

	return status;
}

/* cx231xx_gpio_i2c_write
 * Function to write data to gpio based I2C interface
 */
int cx231xx_gpio_i2c_write(struct cx231xx *dev, u8 dev_addr, u8 * buf, u8 len)
{
	int status = 0;
	int i = 0;

	/* get the lock */
	mutex_lock(&dev->gpio_i2c_lock);

	/* start */
	status = cx231xx_gpio_i2c_start(dev);

	/* write dev_addr */
	status = cx231xx_gpio_i2c_write_byte(dev, dev_addr << 1);

	/* read Ack */
	status = cx231xx_gpio_i2c_read_ack(dev);

	for (i = 0; i < len; i++) {
		/* Write data */
		status = cx231xx_gpio_i2c_write_byte(dev, buf[i]);

		/* read Ack */
		status = cx231xx_gpio_i2c_read_ack(dev);
	}

	/* write End */
	status = cx231xx_gpio_i2c_end(dev);

	/* release the lock */
	mutex_unlock(&dev->gpio_i2c_lock);

	return 0;
}
