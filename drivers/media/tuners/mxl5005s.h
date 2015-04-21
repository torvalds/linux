/*
    MaxLinear MXL5005S VSB/QAM/DVBT tuner driver

    Copyright (C) 2008 MaxLinear
    Copyright (C) 2008 Steven Toth <stoth@linuxtv.org>

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

#ifndef __MXL5005S_H
#define __MXL5005S_H

#include <linux/kconfig.h>

#include <linux/i2c.h>
#include "dvb_frontend.h"

struct mxl5005s_config {

	/* 7 bit i2c address */
	u8 i2c_address;

#define IF_FREQ_4570000HZ    4570000
#define IF_FREQ_4571429HZ    4571429
#define IF_FREQ_5380000HZ    5380000
#define IF_FREQ_36000000HZ  36000000
#define IF_FREQ_36125000HZ  36125000
#define IF_FREQ_36166667HZ  36166667
#define IF_FREQ_44000000HZ  44000000
	u32 if_freq;

#define CRYSTAL_FREQ_4000000HZ    4000000
#define CRYSTAL_FREQ_16000000HZ  16000000
#define CRYSTAL_FREQ_25000000HZ  25000000
#define CRYSTAL_FREQ_28800000HZ  28800000
	u32 xtal_freq;

#define MXL_DUAL_AGC   0
#define MXL_SINGLE_AGC 1
	u8 agc_mode;

#define MXL_TF_DEFAULT	0
#define MXL_TF_OFF	1
#define MXL_TF_C	2
#define MXL_TF_C_H	3
#define MXL_TF_D	4
#define MXL_TF_D_L	5
#define MXL_TF_E	6
#define MXL_TF_F	7
#define MXL_TF_E_2	8
#define MXL_TF_E_NA	9
#define MXL_TF_G	10
	u8 tracking_filter;

#define MXL_RSSI_DISABLE	0
#define MXL_RSSI_ENABLE		1
	u8 rssi_enable;

#define MXL_CAP_SEL_DISABLE	0
#define MXL_CAP_SEL_ENABLE	1
	u8 cap_select;

#define MXL_DIV_OUT_1	0
#define MXL_DIV_OUT_4	1
	u8 div_out;

#define MXL_CLOCK_OUT_DISABLE	0
#define MXL_CLOCK_OUT_ENABLE	1
	u8 clock_out;

#define MXL5005S_IF_OUTPUT_LOAD_200_OHM 200
#define MXL5005S_IF_OUTPUT_LOAD_300_OHM 300
	u32 output_load;

#define MXL5005S_TOP_5P5   55
#define MXL5005S_TOP_7P2   72
#define MXL5005S_TOP_9P2   92
#define MXL5005S_TOP_11P0 110
#define MXL5005S_TOP_12P9 129
#define MXL5005S_TOP_14P7 147
#define MXL5005S_TOP_16P8 168
#define MXL5005S_TOP_19P4 194
#define MXL5005S_TOP_21P2 212
#define MXL5005S_TOP_23P2 232
#define MXL5005S_TOP_25P2 252
#define MXL5005S_TOP_27P1 271
#define MXL5005S_TOP_29P2 292
#define MXL5005S_TOP_31P7 317
#define MXL5005S_TOP_34P9 349
	u32 top;

#define MXL_ANALOG_MODE  0
#define MXL_DIGITAL_MODE 1
	u8 mod_mode;

#define MXL_ZERO_IF 0
#define MXL_LOW_IF  1
	u8 if_mode;

	/* Some boards need to override the built-in logic for determining
	   the gain when in QAM mode (the HVR-1600 is one such case) */
	u8 qam_gain;

	/* Stuff I don't know what to do with */
	u8 AgcMasterByte;
};

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_MXL5005S)
extern struct dvb_frontend *mxl5005s_attach(struct dvb_frontend *fe,
					    struct i2c_adapter *i2c,
					    struct mxl5005s_config *config);
#else
static inline struct dvb_frontend *mxl5005s_attach(struct dvb_frontend *fe,
					    struct i2c_adapter *i2c,
					    struct mxl5005s_config *config)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_TUNER_MXL5005S */

#endif /* __MXL5005S_H */

