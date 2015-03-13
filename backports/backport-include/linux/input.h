#ifndef __BACKPORT_INPUT_H
#define __BACKPORT_INPUT_H
#include_next <linux/input.h>

#ifndef KEY_WIMAX
#define KEY_WIMAX		246
#endif

#ifndef KEY_WPS_BUTTON
#define KEY_WPS_BUTTON		0x211
#endif

#ifndef KEY_RFKILL
#define KEY_RFKILL		247
#endif

#ifndef SW_RFKILL_ALL
#define SW_RFKILL_ALL           0x03
#endif

#endif /* __BACKPORT_INPUT_H */
