/*
 * wifi_power.c
 *
 * Yongle Lai @ Rockchip Fuzhou @ 20100303.
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

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
#include <asm/arch/gpio.h>
#include <asm/arch/iomux.h>
#else
#include <mach/gpio.h>
#include <mach/iomux.h>
#endif
#include <mach/board.h>


#if (WIFI_GPIO_POWER_CONTROL == 1)

extern struct wifi_power power_gpio;
extern struct wifi_power power_save_gpio;
extern struct wifi_power power_reset_gpio;

extern int rk29sdk_wifi_power(int on);

#define OS_IOMUX(name, value) rk29_mux_api_set((name), (value));

int wifi_gpio_operate(struct wifi_power *gpio, int flag)
{
	int sensitive;
	
	if (gpio->use_gpio == POWER_NOT_USE_GPIO)
		return 0;
	
	if (gpio->gpio_iomux == POWER_GPIO_IOMUX)
	{
		OS_IOMUX(gpio->iomux_name, gpio->iomux_value);
	}
	
	if (flag == GPIO_SWITCH_ON)
		sensitive = gpio->sensi_level;
	else
		sensitive = 1 - gpio->sensi_level;
		
	if (gpio->use_gpio == POWER_USE_EXT_GPIO)
	{
		wifi_extgpio_operation(gpio->gpio_id, sensitive);
	}
	else
	{
		int ret;

		ret = gpio_request(gpio->gpio_id, NULL);
		if (ret != 0)
			printk("Request GPIO for WIFI POWER error!\n");

		gpio_direction_output(gpio->gpio_id, sensitive);
		gpio_set_value(gpio->gpio_id, sensitive);

		gpio_free(gpio->gpio_id);
	}

	return 0;
}

/*
 * WiFi power up sequence
 */
int wifi_turn_on_rtl8192c_card(void)
{
#ifdef CONFIG_MACH_RK2928_A720
        rk2928_usb_wifi_on();
#else
	//wifi_gpio_operate(&power_gpio, GPIO_SWITCH_ON);
        rk29sdk_wifi_power(1);
#endif
	if (power_gpio.use_gpio != POWER_NOT_USE_GPIO)
		msleep(1000);
	
	return 0;
}

int wifi_turn_on_card(int module)
{
	wifi_turn_on_callback();
	
	wifi_turn_on_rtl8192c_card();
	
	return 0;
}

int wifi_turn_off_card(void)
{
#ifdef CONFIG_MACH_RK2928_A720
        rk2928_usb_wifi_off();
#else
	//wifi_gpio_operate(&power_gpio, GPIO_SWITCH_OFF);
        rk29sdk_wifi_power(0);
#endif
	msleep(5);

	wifi_turn_off_callback();
	
	return 0;
}

void rockchip_wifi_shutdown(void)
{
	printk("rockchip_wifi_shutdown....\n");

	wifi_turn_off_card();
}
EXPORT_SYMBOL(rockchip_wifi_shutdown);

#endif /* WIFI_GPIO_POWER_CONTROL */

