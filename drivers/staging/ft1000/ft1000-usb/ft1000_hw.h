
#ifndef _FT1000_HW_H_
#define _FT1000_HW_H_

#include "ft1000_usb.h"

extern u16 ft1000_read_register(struct usb_device *dev, u16 *Data, u8 nRegIndx);
extern u16 ft1000_write_register(struct usb_device *dev, u16 value, u8 nRegIndx);

#endif
