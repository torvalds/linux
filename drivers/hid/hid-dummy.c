#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/hid.h>

static int __init hid_dummy_init(void)
{
#ifdef CONFIG_HID_APPLE_MODULE
	HID_COMPAT_CALL_DRIVER(apple);
#endif
#ifdef CONFIG_HID_CYPRESS_MODULE
	HID_COMPAT_CALL_DRIVER(cypress);
#endif
#ifdef CONFIG_HID_LOGITECH_MODULE
	HID_COMPAT_CALL_DRIVER(logitech);
#endif
#ifdef CONFIG_HID_MICROSOFT_MODULE
	HID_COMPAT_CALL_DRIVER(microsoft);
#endif
#ifdef CONFIG_HID_SUNPLUS_MODULE
	HID_COMPAT_CALL_DRIVER(sunplus);
#endif

	return -EIO;
}
module_init(hid_dummy_init);

MODULE_LICENSE("GPL");
