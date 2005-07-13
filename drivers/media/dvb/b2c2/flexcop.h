/*
 * This file is part of linux driver the digital TV devices equipped with B2C2 FlexcopII(b)/III
 *
 * flexcop.h - private header file for all flexcop-chip-source files.
 *
 * see flexcop.c for copyright information.
 */
#ifndef __FLEXCOP_H__
#define __FLEXCOP_H___

#define FC_LOG_PREFIX "b2c2-flexcop"
#include "flexcop-common.h"

extern int b2c2_flexcop_debug;

/* debug */
#ifdef CONFIG_DVB_B2C2_FLEXCOP_DEBUG
#define dprintk(level,args...) \
	do { if ((b2c2_flexcop_debug & level)) printk(args); } while (0)
#else
#define dprintk(level,args...)
#endif

#define deb_info(args...)  dprintk(0x01,args)
#define deb_tuner(args...) dprintk(0x02,args)
#define deb_i2c(args...)   dprintk(0x04,args)
#define deb_ts(args...)    dprintk(0x08,args)
#define deb_sram(args...)  dprintk(0x10,args)
#define deb_rdump(args...)  dprintk(0x20,args)

#endif
