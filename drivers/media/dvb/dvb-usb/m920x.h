#ifndef _DVB_USB_M920X_H_
#define _DVB_USB_M920X_H_

#define DVB_USB_LOG_PREFIX "m920x"
#include "dvb-usb.h"

#define deb_rc(args...)   dprintk(dvb_usb_m920x_debug,0x01,args)

#define M9206_CORE	0x22
#define M9206_RC_STATE	0xff51
#define M9206_RC_KEY	0xff52
#define M9206_RC_INIT1	0xff54
#define M9206_RC_INIT2	0xff55
#define M9206_FW_GO	0xff69

#define M9206_I2C	0x23
#define M9206_FILTER	0x25
#define M9206_FW	0x30

#define M9206_MAX_FILTERS 8

/*
sequences found in logs:
[index value]
0x80 write addr
(0x00 out byte)*
0x40 out byte

0x80 write addr
(0x00 out byte)*
0x80 read addr
(0x21 in byte)*
0x60 in byte

this sequence works:
0x80 read addr
(0x21 in byte)*
0x60 in byte

_my guess_:
0x80: begin i2c transfer using address. value=address<<1|(reading?1:0)
0x00: write byte
0x21: read byte, more to follow
0x40: write last byte of message sequence
0x60: read last byte of message sequence
 */

struct m9206_state {
	u16 filters[M9206_MAX_FILTERS];
	int filtering_enabled;
	int rep_count;
};
#endif
