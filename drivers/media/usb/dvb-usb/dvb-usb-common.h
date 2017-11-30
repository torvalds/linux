/* SPDX-License-Identifier: GPL-2.0 */
/* dvb-usb-common.h is part of the DVB USB library.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@posteo.de)
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

#define deb_info(args...)  dprintk(dvb_usb_debug,0x001,args)
#define deb_xfer(args...)  dprintk(dvb_usb_debug,0x002,args)
#define deb_pll(args...)   dprintk(dvb_usb_debug,0x004,args)
#define deb_ts(args...)    dprintk(dvb_usb_debug,0x008,args)
#define deb_err(args...)   dprintk(dvb_usb_debug,0x010,args)
#define deb_rc(args...)    dprintk(dvb_usb_debug,0x020,args)
#define deb_fw(args...)    dprintk(dvb_usb_debug,0x040,args)
#define deb_mem(args...)   dprintk(dvb_usb_debug,0x080,args)
#define deb_uxfer(args...) dprintk(dvb_usb_debug,0x100,args)

/* commonly used  methods */
extern int dvb_usb_download_firmware(struct usb_device *, struct dvb_usb_device_properties *);

extern int dvb_usb_device_power_ctrl(struct dvb_usb_device *d, int onoff);

extern int usb_urb_init(struct usb_data_stream *stream, struct usb_data_stream_properties *props);
extern int usb_urb_exit(struct usb_data_stream *stream);
extern int usb_urb_submit(struct usb_data_stream *stream);
extern int usb_urb_kill(struct usb_data_stream *stream);

extern int dvb_usb_adapter_stream_init(struct dvb_usb_adapter *adap);
extern int dvb_usb_adapter_stream_exit(struct dvb_usb_adapter *adap);

extern int dvb_usb_i2c_init(struct dvb_usb_device *);
extern int dvb_usb_i2c_exit(struct dvb_usb_device *);

extern int dvb_usb_adapter_dvb_init(struct dvb_usb_adapter *adap,
				    short *adapter_nums);
extern int dvb_usb_adapter_dvb_exit(struct dvb_usb_adapter *adap);
extern int dvb_usb_adapter_frontend_init(struct dvb_usb_adapter *adap);
extern int dvb_usb_adapter_frontend_exit(struct dvb_usb_adapter *adap);

extern int dvb_usb_remote_init(struct dvb_usb_device *);
extern int dvb_usb_remote_exit(struct dvb_usb_device *);

#endif
