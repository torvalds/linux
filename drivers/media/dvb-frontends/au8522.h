/*
    Auvitek AU8522 QAM/8VSB demodulator driver

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

#ifndef __AU8522_H__
#define __AU8522_H__

#include <linux/kconfig.h>
#include <linux/dvb/frontend.h>

enum au8522_if_freq {
	AU8522_IF_6MHZ = 0,
	AU8522_IF_4MHZ,
	AU8522_IF_3_25MHZ,
};

struct au8522_led_config {
	u16 vsb8_strong;
	u16 qam64_strong;
	u16 qam256_strong;

	u16 gpio_output;
	/* unset hi bits, set low bits */
	u16 gpio_output_enable;
	u16 gpio_output_disable;

	u16 gpio_leds;
	u8 *led_states;
	unsigned int num_led_states;
};

struct au8522_config {
	/* the demodulator's i2c address */
	u8 demod_address;

	/* Return lock status based on tuner lock, or demod lock */
#define AU8522_TUNERLOCKING 0
#define AU8522_DEMODLOCKING 1
	u8 status_mode;

	struct au8522_led_config *led_cfg;

	enum au8522_if_freq vsb_if;
	enum au8522_if_freq qam_if;
};

#if IS_REACHABLE(CONFIG_DVB_AU8522_DTV)
extern struct dvb_frontend *au8522_attach(const struct au8522_config *config,
					  struct i2c_adapter *i2c);
#else
static inline
struct dvb_frontend *au8522_attach(const struct au8522_config *config,
				   struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_AU8522 */

/* Other modes may need to be added later */
enum au8522_video_input {
	AU8522_COMPOSITE_CH1 = 1,
	AU8522_COMPOSITE_CH2,
	AU8522_COMPOSITE_CH3,
	AU8522_COMPOSITE_CH4,
	AU8522_COMPOSITE_CH4_SIF,
	AU8522_SVIDEO_CH13,
	AU8522_SVIDEO_CH24,
};

enum au8522_audio_input {
	AU8522_AUDIO_NONE,
	AU8522_AUDIO_SIF,
};

#endif /* __AU8522_H__ */
