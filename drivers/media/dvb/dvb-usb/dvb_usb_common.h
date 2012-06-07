/* dvb-usb-common.h is part of the DVB USB library.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * a header file containing prototypes and types for internal use of the
 * dvb-usb-lib
 */
#ifndef DVB_USB_COMMON_H
#define DVB_USB_COMMON_H

#include "dvb_usb.h"

extern int dvb_usb_disable_rc_polling;

/* commonly used  methods */
extern int dvb_usb_device_power_ctrl(struct dvb_usb_device *d, int onoff);

extern int usb_urb_init(struct usb_data_stream *stream,
		struct usb_data_stream_properties *props);
extern int usb_urb_exit(struct usb_data_stream *stream);
extern int usb_urb_submit(struct usb_data_stream *stream,
		struct usb_data_stream_properties *props);
extern int usb_urb_kill(struct usb_data_stream *stream);

extern int dvb_usb_adapter_stream_init(struct dvb_usb_adapter *adap);
extern int dvb_usb_adapter_stream_exit(struct dvb_usb_adapter *adap);

extern int dvb_usb_adapter_dvb_init(struct dvb_usb_adapter *adap);
extern int dvb_usb_adapter_dvb_exit(struct dvb_usb_adapter *adap);
extern int dvb_usb_adapter_frontend_init(struct dvb_usb_adapter *adap);
extern int dvb_usb_adapter_frontend_exit(struct dvb_usb_adapter *adap);

extern int dvb_usb_remote_init(struct dvb_usb_device *);
extern int dvb_usb_remote_exit(struct dvb_usb_device *);

#endif
