/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  mxl5007t.h - driver for the MaxLinear MxL5007T silicon tuner
 *
 *  Copyright (C) 2008 Michael Krufky <mkrufky@linuxtv.org>
 */

#ifndef __MXL5007T_H__
#define __MXL5007T_H__

#include <media/dvb_frontend.h>

/* ------------------------------------------------------------------------- */

enum mxl5007t_if_freq {
	MxL_IF_4_MHZ,      /*  4000000 */
	MxL_IF_4_5_MHZ,    /*  4500000 */
	MxL_IF_4_57_MHZ,   /*  4570000 */
	MxL_IF_5_MHZ,      /*  5000000 */
	MxL_IF_5_38_MHZ,   /*  5380000 */
	MxL_IF_6_MHZ,      /*  6000000 */
	MxL_IF_6_28_MHZ,   /*  6280000 */
	MxL_IF_9_1915_MHZ, /*  9191500 */
	MxL_IF_35_25_MHZ,  /* 35250000 */
	MxL_IF_36_15_MHZ,  /* 36150000 */
	MxL_IF_44_MHZ,     /* 44000000 */
};

enum mxl5007t_xtal_freq {
	MxL_XTAL_16_MHZ,      /* 16000000 */
	MxL_XTAL_20_MHZ,      /* 20000000 */
	MxL_XTAL_20_25_MHZ,   /* 20250000 */
	MxL_XTAL_20_48_MHZ,   /* 20480000 */
	MxL_XTAL_24_MHZ,      /* 24000000 */
	MxL_XTAL_25_MHZ,      /* 25000000 */
	MxL_XTAL_25_14_MHZ,   /* 25140000 */
	MxL_XTAL_27_MHZ,      /* 27000000 */
	MxL_XTAL_28_8_MHZ,    /* 28800000 */
	MxL_XTAL_32_MHZ,      /* 32000000 */
	MxL_XTAL_40_MHZ,      /* 40000000 */
	MxL_XTAL_44_MHZ,      /* 44000000 */
	MxL_XTAL_48_MHZ,      /* 48000000 */
	MxL_XTAL_49_3811_MHZ, /* 49381100 */
};

enum mxl5007t_clkout_amp {
	MxL_CLKOUT_AMP_0_94V = 0,
	MxL_CLKOUT_AMP_0_53V = 1,
	MxL_CLKOUT_AMP_0_37V = 2,
	MxL_CLKOUT_AMP_0_28V = 3,
	MxL_CLKOUT_AMP_0_23V = 4,
	MxL_CLKOUT_AMP_0_20V = 5,
	MxL_CLKOUT_AMP_0_17V = 6,
	MxL_CLKOUT_AMP_0_15V = 7,
};

struct mxl5007t_config {
	s32 if_diff_out_level;
	enum mxl5007t_clkout_amp clk_out_amp;
	enum mxl5007t_xtal_freq xtal_freq_hz;
	enum mxl5007t_if_freq if_freq_hz;
	unsigned int invert_if:1;
	unsigned int loop_thru_enable:1;
	unsigned int clk_out_enable:1;
};

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_MXL5007T)
extern struct dvb_frontend *mxl5007t_attach(struct dvb_frontend *fe,
					    struct i2c_adapter *i2c, u8 addr,
					    struct mxl5007t_config *cfg);
#else
static inline struct dvb_frontend *mxl5007t_attach(struct dvb_frontend *fe,
						   struct i2c_adapter *i2c,
						   u8 addr,
						   struct mxl5007t_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* __MXL5007T_H__ */
