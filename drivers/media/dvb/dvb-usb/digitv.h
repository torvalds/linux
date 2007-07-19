#ifndef _DVB_USB_DIGITV_H_
#define _DVB_USB_DIGITV_H_

#define DVB_USB_LOG_PREFIX "digitv"
#include "dvb-usb.h"

struct digitv_state {
    int is_nxt6000;
};

extern int dvb_usb_digitv_debug;
#define deb_rc(args...)   dprintk(dvb_usb_digitv_debug,0x01,args)

/* protocol (from usblogging and the SDK:
 *
 * Always 7 bytes bulk message(s) for controlling
 *
 * First byte describes the command. Reads are 2 consecutive transfer (as always).
 *
 * General structure:
 *
 * write or first message of a read:
 * <cmdbyte> VV <len> B0 B1 B2 B3
 *
 * second message of a read
 * <cmdbyte> VV <len> R0 R1 R2 R3
 *
 * whereas 0 < len <= 4
 *
 * I2C address is stored somewhere inside the device.
 *
 * 0x01 read from EEPROM
 *  VV = offset; B* = 0; R* = value(s)
 *
 * 0x02 read register of the COFDM
 *  VV = register; B* = 0; R* = value(s)
 *
 * 0x05 write register of the COFDM
 *  VV = register; B* = value(s);
 *
 * 0x06 write to the tuner (only for NXT6000)
 *  VV = 0; B* = PLL data; len = 4;
 *
 * 0x03 read remote control
 *  VV = 0; B* = 0; len = 4; R* = key
 *
 * 0x07 write to the remote (don't know why one should this, resetting ?)
 *  VV = 0; B* = key; len = 4;
 *
 * 0x08 write remote type
 *  VV = 0; B[0] = 0x01, len = 4
 *
 * 0x09 write device init
 *  TODO
 */
#define USB_READ_EEPROM         1

#define USB_READ_COFDM          2
#define USB_WRITE_COFDM         5

#define USB_WRITE_TUNER         6

#define USB_READ_REMOTE         3
#define USB_WRITE_REMOTE        7
#define USB_WRITE_REMOTE_TYPE   8

#define USB_DEV_INIT            9

#endif
