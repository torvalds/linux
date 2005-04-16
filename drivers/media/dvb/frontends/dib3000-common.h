/*
 * .h-files for the common use of the frontend drivers made by DiBcom
 * DiBcom 3000M-B/C, 3000P
 *
 * DiBcom (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * based on GPL code from DibCom, which has
 *
 * Copyright (C) 2004 Amaury Demol for DiBcom (ademol@dibcom.fr)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * Acknowledgements
 *
 *  Amaury Demol (ademol@dibcom.fr) from DiBcom for providing specs and driver
 *  sources, on which this driver (and the dvb-dibusb) are based.
 *
 * see Documentation/dvb/README.dibusb for more information
 *
 */

#ifndef DIB3000_COMMON_H
#define DIB3000_COMMON_H

#include "dvb_frontend.h"
#include "dib3000.h"

/* info and err, taken from usb.h, if there is anything available like by default. */
#define err(format, arg...)  printk(KERN_ERR     "dib3000: " format "\n" , ## arg)
#define info(format, arg...) printk(KERN_INFO    "dib3000: " format "\n" , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "dib3000: " format "\n" , ## arg)

/* frontend state */
struct dib3000_state {
	struct i2c_adapter* i2c;

	struct dvb_frontend_ops ops;

/* configuration settings */
	struct dib3000_config config;

	struct dvb_frontend frontend;
	int timing_offset;
	int timing_offset_comp_done;

	fe_bandwidth_t last_tuned_bw;
	u32 last_tuned_freq;
};

/* commonly used methods by the dib3000mb/mc/p frontend */
extern int dib3000_read_reg(struct dib3000_state *state, u16 reg);
extern int dib3000_write_reg(struct dib3000_state *state, u16 reg, u16 val);

extern int dib3000_search_status(u16 irq,u16 lock);

/* handy shortcuts */
#define rd(reg) dib3000_read_reg(state,reg)

#define wr(reg,val) if (dib3000_write_reg(state,reg,val)) \
	{ err("while sending 0x%04x to 0x%04x.",val,reg); return -EREMOTEIO; }

#define wr_foreach(a,v) { int i; \
	if (sizeof(a) != sizeof(v)) \
		err("sizeof: %zu %zu is different",sizeof(a),sizeof(v));\
	for (i=0; i < sizeof(a)/sizeof(u16); i++) \
		wr(a[i],v[i]); \
	}

#define set_or(reg,val) wr(reg,rd(reg) | val)

#define set_and(reg,val) wr(reg,rd(reg) & val)


/* debug */

#ifdef CONFIG_DVB_DIBCOM_DEBUG
#define dprintk(level,args...) \
    do { if ((debug & level)) { printk(args); } } while (0)
#else
#define dprintk(args...) do { } while (0)
#endif

/* mask for enabling a specific pid for the pid_filter */
#define DIB3000_ACTIVATE_PID_FILTERING	(0x2000)

/* common values for tuning */
#define DIB3000_ALPHA_0					(     0)
#define DIB3000_ALPHA_1					(     1)
#define DIB3000_ALPHA_2					(     2)
#define DIB3000_ALPHA_4					(     4)

#define DIB3000_CONSTELLATION_QPSK		(     0)
#define DIB3000_CONSTELLATION_16QAM		(     1)
#define DIB3000_CONSTELLATION_64QAM		(     2)

#define DIB3000_GUARD_TIME_1_32			(     0)
#define DIB3000_GUARD_TIME_1_16			(     1)
#define DIB3000_GUARD_TIME_1_8			(     2)
#define DIB3000_GUARD_TIME_1_4			(     3)

#define DIB3000_TRANSMISSION_MODE_2K	(     0)
#define DIB3000_TRANSMISSION_MODE_8K	(     1)

#define DIB3000_SELECT_LP				(     0)
#define DIB3000_SELECT_HP				(     1)

#define DIB3000_FEC_1_2					(     1)
#define DIB3000_FEC_2_3					(     2)
#define DIB3000_FEC_3_4					(     3)
#define DIB3000_FEC_5_6					(     5)
#define DIB3000_FEC_7_8					(     7)

#define DIB3000_HRCH_OFF				(     0)
#define DIB3000_HRCH_ON					(     1)

#define DIB3000_DDS_INVERSION_OFF		(     0)
#define DIB3000_DDS_INVERSION_ON		(     1)

#define DIB3000_TUNER_WRITE_ENABLE(a)	(0xffff & (a << 8))
#define DIB3000_TUNER_WRITE_DISABLE(a)	(0xffff & ((a << 8) | (1 << 7)))

/* for auto search */
extern u16 dib3000_seq[2][2][2];

#define DIB3000_REG_MANUFACTOR_ID		(  1025)
#define DIB3000_I2C_ID_DIBCOM			(0x01b3)

#define DIB3000_REG_DEVICE_ID			(  1026)
#define DIB3000MB_DEVICE_ID				(0x3000)
#define DIB3000MC_DEVICE_ID				(0x3001)
#define DIB3000P_DEVICE_ID				(0x3002)

#endif // DIB3000_COMMON_H
