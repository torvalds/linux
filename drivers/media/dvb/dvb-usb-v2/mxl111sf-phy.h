/*
 *  mxl111sf-phy.h - driver for the MaxLinear MXL111SF
 *
 *  Copyright (C) 2010 Michael Krufky <mkrufky@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
