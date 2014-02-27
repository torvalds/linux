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

/*
 * GPIO to control LDO/DCDC.
 *
 * 用于控制WIFI的电源，通常是3.3V和1.8V，可能1.2V也在其中。
 *
 * 如果是扩展IO，请参考下面的例子:
 *   POWER_USE_EXT_GPIO, 0, 0, 0, PCA9554_Pin1, GPIO_HIGH
 */
struct wifi_power power_gpio = 
{
	0, 0, 0, 0, 0, 0
};

/*
 * GPIO to control WIFI PowerDOWN/RESET.
 *
 * 控制WIFI的PowerDown脚。有些模组PowerDown脚是和Reset脚短接在一起。
 */
struct wifi_power power_save_gpio = 
{
	0, 0, 0, 0, 0, 0
};

/*
 * GPIO to reset WIFI. Keep this as NULL normally.
 *
 * 控制WIFI的Reset脚，通常WiFi模组没有用到这个引脚。
 */
struct wifi_power power_reset_gpio = 
{
	0, 0, 0, 0, 0, 0
};

/*
 * 在WIFI被上电前，会调用这个函数。
 */
void wifi_turn_on_callback(void)
{
}

/*
 * 在WIFI被下电后，会调用这个函数。
 */
void wifi_turn_off_callback(void)
{
}

/*
 * If external GPIO chip such as PCA9554 is being used, please
 * implement the following 2 function.
 *
 * id:   is GPIO identifier, such as GPIOPortF_Pin0, or external 
 *       name defined in struct wifi_power.
 * sens: the value should be set to GPIO, usually is GPIO_HIGH or GPIO_LOW.
 *
 * 如果有用扩展GPIO来控制WIFI，请实现下面的函数:
 * 函数的功能是：控制指定的IO口id，使其状态切换为要求的sens状态。
 * id  : 是IO的标识号，以整数的形式标识。
 * sens: 是要求的IO状态，为高或低。
 */
void wifi_extgpio_operation(u8 id, u8 sens)
{
	//pca955x_gpio_direction_output(id, sens);
}

/*
 * 在系统中如果要调用WIFI的IO控制，将WIFI下电，可以调用如下接口：
 *   void rockchip_wifi_shutdown(void);
 * 但注意需要在宏WIFI_GPIO_POWER_CONTROL的控制下。
 */

/*
 * For USB WiFi, we need to switch USB mode.
 */

#if defined(CONFIG_RALINK_RT3070) || defined(CONFIG_REALTEK_RTL8192)

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
	wifi_turn_on_card(WIFI_CHIP_RT3070);
	msleep(100);
	
	usb_switch_usb_host11_for_wifi(0);
	msleep(1000);
	
	return 0;
}

int wifi_activate_usb(void)
{
	usb_switch_usb_host11_for_wifi(1);
	msleep(1000);

	wifi_turn_off_card();
	
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

