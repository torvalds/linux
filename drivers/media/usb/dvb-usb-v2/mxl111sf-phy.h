/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  mxl111sf-phy.h - driver for the MaxLinear MXL111SF
 *
 *  Copyright (C) 2010-2014 Michael Krufky <mkrufky@linuxtv.org>
 */

#ifndef _DVB_USB_MXL111SF_PHY_H_
#define _DVB_USB_MXL111SF_PHY_H_

#include "mxl111sf.h"

int mxl1x1sf_soft_reset(struct mxl111sf_state *state);
int mxl1x1sf_set_device_mode(struct mxl111sf_state *state, int mode);
int mxl1x1sf_top_master_ctrl(struct mxl111sf_state *state, int onoff);
int mxl111sf_disable_656_port(struct mxl111sf_state *state);
int mxl111sf_init_tuner_demod(struct mxl111sf_state *state);
int mxl111sf_enable_usb_output(struct mxl111sf_state *state);
int mxl111sf_config_mpeg_in(struct mxl111sf_state *state,
			    unsigned int parallel_serial,
			    unsigned int msb_lsb_1st,
			    unsigned int clock_phase,
			    unsigned int mpeg_valid_pol,
			    unsigned int mpeg_sync_pol);
int mxl111sf_config_i2s(struct mxl111sf_state *state,
			u8 msb_start_pos, u8 data_width);
int mxl111sf_init_i2s_port(struct mxl111sf_state *state, u8 sample_size);
int mxl111sf_disable_i2s_port(struct mxl111sf_state *state);
int mxl111sf_config_spi(struct mxl111sf_state *state, int onoff);
int mxl111sf_idac_config(struct mxl111sf_state *state,
			 u8 control_mode, u8 current_setting,
			 u8 current_value, u8 hysteresis_value);

#endif /* _DVB_USB_MXL111SF_PHY_H_ */
