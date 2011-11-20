/*
 * wifi_power.c
 *
 * Power control for WIFI module.
 *
 * There are Power supply and Power Up/Down controls for WIFI typically.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

#include "wifi_power.h"

#if (WIFI_GPIO_POWER_CONTROL == 1)

int wifi_change_usb_mode = 0;
int usb_wifi_status = 0;

void wifi_usb_init(void)
{
	wifi_change_usb_mode = 0;
	usb_wifi_status = 0;
}

#if (WIFI_USE_IFACE == WIFI_USE_OTG)

#define USB_NORMAL        0
#define USB_FORCE_HOST    1
#define USB_FORCE_DEVICE  2

extern int usb_force_usb_for_wifi(int mode);

/*
 * Change USB mode to HOST.
 */
int wifi_activate_usb(void)
{
	wifi_turn_on_card(WIFI_CHIP_RTL8192C);
	
	wifi_change_usb_mode = usb_force_usb_for_wifi(USB_FORCE_HOST);
	msleep(1000);
	
	usb_wifi_status = 1;
	
	return 0;
}

/*
 * Change USB mode to be original.
 */
int wifi_deactivate_usb(void)
{
	if (wifi_change_usb_mode == 1)
	{
		usb_force_usb_for_wifi(USB_FORCE_DEVICE);
		msleep(1000);
		usb_force_usb_for_wifi(USB_NORMAL);
		msleep(1000);
	}
	wifi_turn_off_card();

	usb_wifi_status = 0;

	return 0;
}

#elif (WIFI_USE_IFACE == WIFI_USE_HOST11)

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
extern int usb_switch_usb_host11_for_wifi(int enabled);
#endif

int wifi_deactivate_usb(void)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	usb_switch_usb_host11_for_wifi(0);
	msleep(1000);
#endif
	
	wifi_turn_off_card();
	msleep(100);

	return 0;
}

int wifi_activate_usb(void)
{
	wifi_turn_on_card(WIFI_CHIP_RTL8192C);
	msleep(100);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	usb_switch_usb_host11_for_wifi(1);
	msleep(1000);
#endif

	return 0;
}

#else

int wifi_deactivate_usb(void)
{
	wifi_turn_off_card();
	msleep(1000);
	return 0;
}

int wifi_activate_usb(void)
{
	wifi_turn_on_card(WIFI_CHIP_RTL8192C);
	msleep(1000);
	return 0;
}
#endif

#endif /* WIFI_GPIO_POWER_CONTROL */

