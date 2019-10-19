/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  mxl111sf-gpio.h - driver for the MaxLinear MXL111SF
 *
 *  Copyright (C) 2010-2014 Michael Krufky <mkrufky@linuxtv.org>
 */

#ifndef _DVB_USB_MXL111SF_GPIO_H_
#define _DVB_USB_MXL111SF_GPIO_H_

#include "mxl111sf.h"

int mxl111sf_set_gpio(struct mxl111sf_state *state, int gpio, int val);
int mxl111sf_init_port_expander(struct mxl111sf_state *state);

#define MXL111SF_GPIO_MOD_DVBT	0
#define MXL111SF_GPIO_MOD_MH	1
#define MXL111SF_GPIO_MOD_ATSC	2
int mxl111sf_gpio_mode_switch(struct mxl111sf_state *state, unsigned int mode);

enum mxl111sf_mux_config {
	PIN_MUX_DEFAULT = 0,
	PIN_MUX_TS_OUT_PARALLEL,
	PIN_MUX_TS_OUT_SERIAL,
	PIN_MUX_GPIO_MODE,
	PIN_MUX_TS_SERIAL_IN_MODE_0,
	PIN_MUX_TS_SERIAL_IN_MODE_1,
	PIN_MUX_TS_SPI_IN_MODE_0,
	PIN_MUX_TS_SPI_IN_MODE_1,
	PIN_MUX_TS_PARALLEL_IN,
	PIN_MUX_BT656_I2S_MODE,
};

int mxl111sf_config_pin_mux_modes(struct mxl111sf_state *state,
				  enum mxl111sf_mux_config pin_mux_config);

#endif /* _DVB_USB_MXL111SF_GPIO_H_ */
