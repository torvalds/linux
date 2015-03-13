#ifndef __BACKPORT_MOD_DEVICETABLE_H
#define __BACKPORT_MOD_DEVICETABLE_H
#include_next <linux/mod_devicetable.h>

#ifndef HID_BUS_ANY
#define HID_BUS_ANY                            0xffff
#endif

#ifndef HID_GROUP_ANY
#define HID_GROUP_ANY                          0x0000
#endif

#ifndef HID_ANY_ID
#define HID_ANY_ID                             (~0)
#endif

#endif /* __BACKPORT_MOD_DEVICETABLE_H */
