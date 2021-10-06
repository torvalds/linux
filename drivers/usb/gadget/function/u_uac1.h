/* SPDX-License-Identifier: GPL-2.0 */
/*
 * u_uac1.h - Utility definitions for UAC1 function
 *
 * Copyright (C) 2016 Ruslan Bilovol <ruslan.bilovol@gmail.com>
 */

#ifndef __U_UAC1_H
#define __U_UAC1_H

#include <linux/usb/composite.h>

#define UAC1_OUT_EP_MAX_PACKET_SIZE	200
#define UAC1_DEF_CCHMASK	0x3
#define UAC1_DEF_CSRATE		48000
#define UAC1_DEF_CSSIZE		2
#define UAC1_DEF_PCHMASK	0x3
#define UAC1_DEF_PSRATE		48000
#define UAC1_DEF_PSSIZE		2
#define UAC1_DEF_REQ_NUM	2
#define UAC1_DEF_INT_REQ_NUM	10

#define UAC1_DEF_MUTE_PRESENT	1
#define UAC1_DEF_VOLUME_PRESENT 1
#define UAC1_DEF_MIN_DB		(-100*256)	/* -100 dB */
#define UAC1_DEF_MAX_DB		0		/* 0 dB */
#define UAC1_DEF_RES_DB		(1*256)	/* 1 dB */


struct f_uac1_opts {
	struct usb_function_instance	func_inst;
	int				c_chmask;
	int				c_srate;
	int				c_ssize;
	int				p_chmask;
	int				p_srate;
	int				p_ssize;

	bool			p_mute_present;
	bool			p_volume_present;
	s16				p_volume_min;
	s16				p_volume_max;
	s16				p_volume_res;

	bool			c_mute_present;
	bool			c_volume_present;
	s16				c_volume_min;
	s16				c_volume_max;
	s16				c_volume_res;

	int				req_number;
	unsigned			bound:1;

	struct mutex			lock;
	int				refcnt;
};

#endif /* __U_UAC1_H */
