/*
 *  mxl111sf-tuner.h - driver for the MaxLinear MXL111SF CMOS tuner
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

#ifndef __MXL111SF_TUNER_H__
#define __MXL111SF_TUNER_H__

#include <linux/kconfig.h>
#include "dvb_frontend.h"
#include "mxl111sf.h"

enum mxl_if_freq {
#if 0
	MXL_IF_LO    = 0x00, /* other IF < 9MHz */
#endif
	MXL_IF_4_0   = 0x01, /* 4.0   MHz */
	MXL_IF_4_5   = 0x02, /* 4.5   MHz */
	MXL_IF_4_57  = 0x03, /* 4.57  MHz */
	MXL_IF_5_0   = 0x04, /* 5.0   MHz */
	MXL_IF_5_38  = 0x05, /* 5.38  MHz */
	MXL_IF_6_0   = 0x06, /* 6.0   MHz */
	MXL_IF_6_28  = 0x07, /* 6.28  MHz */
	MXL_IF_7_2   = 0x08, /* 7.2   MHz */
	MXL_IF_35_25 = 0x09, /* 35.25 MHz */
	MXL_IF_36    = 0x0a, /* 36    MHz */
	MXL_IF_36_15 = 0x0b, /* 36.15 MHz */
	MXL_IF_44    = 0x0c, /* 44    MHz */
#if 0
	MXL_IF_HI    = 0x0f, /* other IF > 35 MHz and < 45 MHz */
#endif
};

struct mxl111sf_tuner_config {
	enum mxl_if_freq if_freq;
	unsigned int invert_spectrum:1;

	int (*read_reg)(struct mxl111sf_state *state, u8 addr, u8 *data);
	int (*write_reg)(struct mxl111sf_state *state, u8 addr, u8 data);
	int (*program_regs)(struct mxl111sf_state *state,
			    struct mxl111sf_reg_ctrl_info *ctrl_reg_info);
	int (*top_master_ctrl)(struct mxl111sf_state *state, int onoff);
	int (*ant_hunt)(struct dvb_frontend *fe);
};

/* ------------------------------------------------------------------------ */

#if IS_ENABLED(CONFIG_DVB_USB_MXL111SF)
extern
struct dvb_frontend *mxl111sf_tuner_attach(struct dvb_frontend *fe,
					   struct mxl111sf_state *mxl_state,
					   struct mxl111sf_tuner_config *cfg);
#else
static inline
struct dvb_frontend *mxl111sf_tuner_attach(struct dvb_frontend *fe,
					   struct mxl111sf_state *mxl_state,
					   struct mxl111sf_tuner_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* __MXL111SF_TUNER_H__ */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

