/* dvb-usb-common.h is part of the DVB USB library.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * a header file containing prototypes and types for internal use of the dvb-usb-lib
 */
#ifndef _DVB_USB_COMMON_H_
#define _DVB_USB_COMMON_H_

#define DVB_USB_LOG_PREFIX "dvb-usb"
#include "dvb-usb.h"

extern int dvb_usb_debug;
extern int dvb_usb_disable_rc_polling;

#define deb_info(args...) dprintk(dvb_usb_debug,0x01,args)
#define deb_xfer(args...) dprintk(dvb_usb_debug,0x02,args)
#define deb_pll(args...)  dprintk(dvb_usb_debug,0x04,args)
#define deb_ts(args...)   dprintk(dvb_usb_debug,0x08,args)
#define deb_err(args...)  dprintk(dvb_usb_debug,0x10,args)
#define deb_rc(args...)   dprintk(dvb_usb_debug,0x20,args)
#define deb_fw(args...)   dprintk(dvb_usb_debug,0x40,args)
#define deb_mem(args...)  dprintk(dvb_usb_debug,0x80,args)

/* commonly used  methods */
extern int usb_cypress_load_firmware(struct usb_device *, const char *, int);

extern int dvb_usb_urb_submit(struct dvb_usb_device *);
extern int dvb_usb_urb_kill(struct dvb_usb_device *);
extern int dvb_usb_urb_init(struct dvb_usb_device *);
extern int dvb_usb_urb_exit(struct dvb_usb_device *);

extern int dvb_usb_i2c_init(struct dvb_usb_device *);
extern int dvb_usb_i2c_exit(struct dvb_usb_device *);

extern int dvb_usb_dvb_init(struct dvb_usb_device *);
extern int dvb_usb_dvb_exit(struct dvb_usb_device *);

extern int dvb_usb_fe_init(struct dvb_usb_device *);
extern int dvb_usb_fe_exit(struct dvb_usb_device *);

extern int dvb_usb_remote_init(struct dvb_usb_device *);
extern int dvb_usb_remote_exit(struct dvb_usb_device *);

#endif
