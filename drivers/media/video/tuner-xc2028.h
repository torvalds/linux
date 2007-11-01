/* tuner-xc2028
 *
 * Copyright (c) 2007 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNU General Public License v2
 */

#ifndef __TUNER_XC2028_H__
#define __TUNER_XC2028_H__

#include "dvb_frontend.h"

#define XC2028_DEFAULT_FIRMWARE "xc3028-v27.fw"

enum xc2028_firm_type {
	XC2028_FIRM_NORMAL,
	XC2028_FIRM_MTS,
};

struct xc2028_ctrl {
	enum xc2028_firm_type	type;
	char			*fname;
	int			max_len;
};

/* xc2028 commands for callback */
#define XC2028_TUNER_RESET	0
#define XC2028_RESET_CLK	1

#if defined(CONFIG_TUNER_XC2028) || (defined(CONFIG_TUNER_XC2028_MODULE) && defined(MODULE))
int xc2028_attach(struct dvb_frontend *fe, struct i2c_adapter* i2c_adap,
		  u8 i2c_addr, struct device *dev, void *video_dev,
		  int (*tuner_callback) (void *dev, int command,int arg));

#else
static inline int xc2028_attach(struct dvb_frontend *fe,
		  struct i2c_adapter* i2c_adap,
		  u8 i2c_addr, struct device *dev, void *video_dev,
		  int (*tuner_callback) (void *dev, int command,int arg))
{
	printk(KERN_INFO "%s: not probed - driver disabled by Kconfig\n",
	       __FUNCTION__);
	return -EINVAL;
}
#endif

#endif /* __TUNER_XC2028_H__ */
