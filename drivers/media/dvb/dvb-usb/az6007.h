#ifndef _DVB_USB_AZ6007_H_
#define _DVB_USB_AZ6007_H_

#define DVB_USB_LOG_PREFIX "az6007"
#include "dvb-usb.h"


extern int dvb_usb_az6007_debug;
#define deb_info(args...) dprintk(dvb_usb_az6007_debug,0x01,args)
#define deb_xfer(args...) dprintk(dvb_usb_az6007_debug,0x02,args)
#define deb_rc(args...)   dprintk(dvb_usb_az6007_debug,0x04,args)
#define deb_fe(args...)   dprintk(dvb_usb_az6007_debug,0x08,args)


extern int vp702x_usb_out_op(struct dvb_usb_device *d, u8 *o, int olen, u8 *i, int ilen, int msec);
extern int vp702x_usb_in_op(struct dvb_usb_device *d, u8 req, u16 value, u16 index, u8 *b, int blen);

#endif
