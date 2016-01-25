/*
 * Copyright (C) 2010-2014 Michael Krufky (mkrufky@linuxtv.org)
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */

#ifndef _DVB_USB_MXL111SF_H_
#define _DVB_USB_MXL111SF_H_

#ifdef DVB_USB_LOG_PREFIX
#undef DVB_USB_LOG_PREFIX
#endif
#define DVB_USB_LOG_PREFIX "mxl111sf"
#include "dvb_usb.h"
#include <media/tveeprom.h>
#include <media/media-entity.h>

#define MXL_EP1_REG_READ     1
#define MXL_EP2_REG_WRITE    2
#define MXL_EP3_INTERRUPT    3
#define MXL_EP4_MPEG2        4
#define MXL_EP5_I2S          5
#define MXL_EP6_656          6
#define MXL_EP6_MPEG2        6

#ifdef USING_ENUM_mxl111sf_current_mode
enum mxl111sf_current_mode {
	mxl_mode_dvbt = MXL_EP4_MPEG2,
	mxl_mode_mh   = MXL_EP5_I2S,
	mxl_mode_atsc = MXL_EP6_MPEG2,
};
#endif

enum mxl111sf_gpio_port_expander {
	mxl111sf_gpio_hw,
	mxl111sf_PCA9534,
};

struct mxl111sf_adap_state {
	int alt_mode;
	int gpio_mode;
	int device_mode;
	int ep6_clockphase;
	int (*fe_init)(struct dvb_frontend *);
	int (*fe_sleep)(struct dvb_frontend *);
};

struct mxl111sf_state {
	struct dvb_usb_device *d;

	enum mxl111sf_gpio_port_expander gpio_port_expander;
	u8 port_expander_addr;

	u8 chip_id;
	u8 chip_ver;
#define MXL111SF_V6     1
#define MXL111SF_V8_100 2
#define MXL111SF_V8_200 3
	u8 chip_rev;

#ifdef USING_ENUM_mxl111sf_current_mode
	enum mxl111sf_current_mode current_mode;
#endif

#define MXL_TUNER_MODE         0
#define MXL_SOC_MODE           1
#define MXL_DEV_MODE_MASK      0x01
#if 1
	int device_mode;
#endif
	/* use usb alt setting 1 for EP4 ISOC transfer (dvb-t),
				     EP5 BULK transfer (atsc-mh),
				     EP6 BULK transfer (atsc/qam),
	   use usb alt setting 2 for EP4 BULK transfer (dvb-t),
				     EP5 ISOC transfer (atsc-mh),
				     EP6 ISOC transfer (atsc/qam),
	 */
	int alt_mode;
	int gpio_mode;
	struct tveeprom tv;

	struct mutex fe_lock;
	u8 num_frontends;
	struct mxl111sf_adap_state adap_state[3];
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	struct media_entity tuner;
	struct media_pad tuner_pads[2];
#endif
};

int mxl111sf_read_reg(struct mxl111sf_state *state, u8 addr, u8 *data);
int mxl111sf_write_reg(struct mxl111sf_state *state, u8 addr, u8 data);

struct mxl111sf_reg_ctrl_info {
	u8 addr;
	u8 mask;
	u8 data;
};

int mxl111sf_write_reg_mask(struct mxl111sf_state *state,
			    u8 addr, u8 mask, u8 data);
int mxl111sf_ctrl_program_regs(struct mxl111sf_state *state,
			       struct mxl111sf_reg_ctrl_info *ctrl_reg_info);

/* needed for hardware i2c functions in mxl111sf-i2c.c:
 * mxl111sf_i2c_send_data / mxl111sf_i2c_get_data */
int mxl111sf_ctrl_msg(struct dvb_usb_device *d,
		      u8 cmd, u8 *wbuf, int wlen, u8 *rbuf, int rlen);

#define mxl_printk(kern, fmt, arg...) \
	printk(kern "%s: " fmt "\n", __func__, ##arg)

#define mxl_info(fmt, arg...) \
	mxl_printk(KERN_INFO, fmt, ##arg)

extern int dvb_usb_mxl111sf_debug;
#define mxl_debug(fmt, arg...) \
	if (dvb_usb_mxl111sf_debug) \
		mxl_printk(KERN_DEBUG, fmt, ##arg)

#define MXL_I2C_DBG 0x04
#define MXL_ADV_DBG 0x10
#define mxl_debug_adv(fmt, arg...) \
	if (dvb_usb_mxl111sf_debug & MXL_ADV_DBG) \
		mxl_printk(KERN_DEBUG, fmt, ##arg)

#define mxl_i2c(fmt, arg...) \
	if (dvb_usb_mxl111sf_debug & MXL_I2C_DBG) \
		mxl_printk(KERN_DEBUG, fmt, ##arg)

#define mxl_i2c_adv(fmt, arg...) \
	if ((dvb_usb_mxl111sf_debug & (MXL_I2C_DBG | MXL_ADV_DBG)) == \
		(MXL_I2C_DBG | MXL_ADV_DBG)) \
			mxl_printk(KERN_DEBUG, fmt, ##arg)

/* The following allows the mxl_fail() macro defined below to work
 * in externel modules, such as mxl111sf-tuner.ko, even though
 * dvb_usb_mxl111sf_debug is not defined within those modules */
#if (defined(__MXL111SF_TUNER_H__)) || (defined(__MXL111SF_DEMOD_H__))
#define MXL_ADV_DEBUG_ENABLED MXL_ADV_DBG
#else
#define MXL_ADV_DEBUG_ENABLED dvb_usb_mxl111sf_debug
#endif

#define mxl_fail(ret)							\
({									\
	int __ret;							\
	__ret = (ret < 0);						\
	if ((__ret) && (MXL_ADV_DEBUG_ENABLED & MXL_ADV_DBG))		\
		mxl_printk(KERN_ERR, "error %d on line %d",		\
			   ret, __LINE__);				\
	__ret;								\
})

#endif /* _DVB_USB_MXL111SF_H_ */
