#ifndef __BACKPORT_HID_H
#define __BACKPORT_HID_H
#include_next <linux/hid.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
#define hid_ignore LINUX_BACKPORT(hid_ignore)
extern bool hid_ignore(struct hid_device *);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
#define HID_TYPE_USBNONE 2
#endif

#ifndef HID_QUIRK_NO_IGNORE
#define HID_QUIRK_NO_IGNORE                    0x40000000
#endif

#ifndef HID_QUIRK_HIDDEV_FORCE
#define HID_QUIRK_HIDDEV_FORCE                 0x00000010
#endif

#ifndef HID_QUIRK_IGNORE
#define HID_QUIRK_IGNORE                       0x00000004
#endif

#ifndef HID_USB_DEVICE
#define HID_USB_DEVICE(ven, prod)                              \
	.bus = BUS_USB, .vendor = (ven), .product = (prod)
#endif

#ifndef HID_BLUETOOTH_DEVICE
#define HID_BLUETOOTH_DEVICE(ven, prod)                                        \
	.bus = BUS_BLUETOOTH, .vendor = (ven), .product = (prod)
#endif

#ifndef hid_printk
#define hid_printk(level, hid, fmt, arg...)		\
	dev_printk(level, &(hid)->dev, fmt, ##arg)
#endif

#ifndef hid_emerg
#define hid_emerg(hid, fmt, arg...)			\
	dev_emerg(&(hid)->dev, fmt, ##arg)
#endif

#ifndef hid_crit
#define hid_crit(hid, fmt, arg...)			\
	dev_crit(&(hid)->dev, fmt, ##arg)
#endif

#ifndef hid_alert
#define hid_alert(hid, fmt, arg...)			\
	dev_alert(&(hid)->dev, fmt, ##arg)
#endif

#ifndef hid_err
#define hid_err(hid, fmt, arg...)			\
	dev_err(&(hid)->dev, fmt, ##arg)
#endif

#ifndef hid_notice
#define hid_notice(hid, fmt, arg...)			\
	dev_notice(&(hid)->dev, fmt, ##arg)
#endif

#ifndef hid_warn
#define hid_warn(hid, fmt, arg...)			\
	dev_warn(&(hid)->dev, fmt, ##arg)
#endif

#ifndef hid_info
#define hid_info(hid, fmt, arg...)			\
	dev_info(&(hid)->dev, fmt, ##arg)
#endif

#ifndef hid_dbg
#define hid_dbg(hid, fmt, arg...)			\
	dev_dbg(&(hid)->dev, fmt, ##arg)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0)
#define hid_alloc_report_buf LINUX_BACKPORT(hid_alloc_report_buf)
u8 *hid_alloc_report_buf(struct hid_report *report, gfp_t flags);
#endif

#endif /* __BACKPORT_HID_H */
