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

#define M9206_I2C_TUNER	0
#define M9206_I2C_DEMOD	1
#define M9206_I2C_MAX	2

struct m9206_state {
	u16 filters[M9206_MAX_FILTERS];
	int filtering_enabled;
	int rep_count;
	struct {
		unsigned char addr;
		unsigned char magic;
	}i2c_r[M9206_I2C_MAX];
};
#endif
