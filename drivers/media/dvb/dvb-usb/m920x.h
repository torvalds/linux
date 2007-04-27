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

Guess at API of the I2C function:
I2C operation is done one byte at a time with USB control messages.  The
index the messages is sent to is made up of a set of flags that control
the I2C bus state:
0x80:  Send START condition.  After a START condition, one would normally
       always send the 7-bit slave I2C address as the 7 MSB, followed by
       the read/write bit as the LSB.
0x40:  Send STOP condition.  This should be set on the last byte of an
       I2C transaction.
0x20:  Read a byte from the slave.  As opposed to writing a byte to the
       slave.  The slave will normally not produce any data unless you
       set the R/W bit to 1 when sending the slave's address after the
       START condition.
0x01:  Respond with ACK, as opposed to a NACK.  For a multi-byte read,
       the master should send an ACK, that is pull SDA low during the 9th
       clock cycle, after every byte but the last.  This flags only makes
       sense when bit 0x20 is set, indicating a read.

What any other bits might mean, or how to get the slave's ACK/NACK
response to a write, is unknown.
*/

struct m9206_state {
	u16 filters[M9206_MAX_FILTERS];
	int filtering_enabled;
	int rep_count;
};
#endif
