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

#ifdef CONFIG_RALINK_RT3070

int wifi_change_usb_mode = 0;
int usb_wifi_status = 0;

void wifi_usb_init(void)
{
	wifi_change_usb_mode = 0;
	usb_wifi_status = 0;
}

#define DONT_SWITCH_USB 0  /* Don't switch USB automaticately. */
#define WIFI_USE_OTG	1  /* WiFi will be connected to USB OTG. */
#define WIFI_USE_HOST11	2  /* WiFi will be connected to USB HOST 1.1. */

#define WIFI_USE_IFACE	2

#if (WIFI_USE_IFACE == WIFI_USE_OTG)

#define USB_NORMAL 			0
#define USB_FORCE_HOST 		1
#define USB_FORCE_DEVICE 	2

extern int usb_force_usb_for_wifi(int mode);

/*
 * Change USB mode to HOST.
 */
int wifi_activate_usb(void)
{
	wifi_turn_on_card(WIFI_CHIP_RT3070);
	
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

extern int usb_switch_usb_host11_for_wifi(int enabled);

int wifi_deactivate_usb(void)
{
	usb_switch_usb_host11_for_wifi(0);
	msleep(1000);
	
	wifi_turn_off_card();
	msleep(100);

	return 0;
}

int wifi_activate_usb(void)
{
	wifi_turn_on_card(WIFI_CHIP_RT3070);
	msleep(100);

	usb_switch_usb_host11_for_wifi(1);
	msleep(1000);

	return 0;
}

#else

int wifi_deactivate_usb(void)
{
	return 0;
}

int wifi_activate_usb(void)
{
	return 0;
}
#endif

#endif /* CONFIG_RALINK_RT3070 */

#endif /* WIFI_GPIO_POWER_CONTROL */

