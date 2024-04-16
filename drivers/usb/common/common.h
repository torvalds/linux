/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_USB_COMMON_H
#define __LINUX_USB_COMMON_H

#if defined(CONFIG_USB_LED_TRIG)
void ledtrig_usb_init(void);
void ledtrig_usb_exit(void);
#else
static inline void ledtrig_usb_init(void) { }
static inline void ledtrig_usb_exit(void) { }
#endif

#endif	/* __LINUX_USB_COMMON_H */
