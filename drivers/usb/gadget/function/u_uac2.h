// SPDX-License-Identifier: GPL-2.0
/*
 * u_uac2.h
 *
 * Utility definitions for UAC2 function
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#ifndef U_UAC2_H
#define U_UAC2_H

#include <linux/usb/composite.h>

#define UAC2_DEF_PCHMASK 0x3
#define UAC2_DEF_PSRATE 48000
#define UAC2_DEF_PSSIZE 2
#define UAC2_DEF_CCHMASK 0x3
#define UAC2_DEF_CSRATE 64000
#define UAC2_DEF_CSSIZE 2
#define UAC2_DEF_REQ_NUM 2

struct f_uac2_opts {
	struct usb_function_instance	func_inst;
	int				p_chmask;
	int				p_srate;
	int				p_ssize;
	int				c_chmask;
	int				c_srate;
	int				c_ssize;
	int				req_number;
	bool				bound;

	struct mutex			lock;
	int				refcnt;
};

#endif
