/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Fitipower FC0012 & FC0013 tuner driver - common defines
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 */

#ifndef _FC001X_COMMON_H_
#define _FC001X_COMMON_H_

enum fc001x_xtal_freq {
	FC_XTAL_27_MHZ,		/* 27000000 */
	FC_XTAL_28_8_MHZ,	/* 28800000 */
	FC_XTAL_36_MHZ,		/* 36000000 */
};

/*
 * enum fc001x_fe_callback_commands - Frontend callbacks
 *
 * @FC_FE_CALLBACK_VHF_ENABLE: enable VHF or UHF
 */
enum fc001x_fe_callback_commands {
	FC_FE_CALLBACK_VHF_ENABLE,
};

#endif
