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

#include "wifi_power.h"
#if (WIFI_GPIO_POWER_CONTROL == 1)
struct wifi_power power_gpio = 
{
	POWER_USE_GPIO, POWER_GPIO_IOMUX, GPIOH7_HSADCCLK_SEL_NAME,
	IOMUXB_GPIO1_D7, RK2818_PIN_PH7, GPIO_HIGH
	
};

struct wifi_power power_save_gpio = 
{
	0, 0, 0, 0, 0, 0
}; 

int wifi_gpio_operate(struct wifi_power *gpio, int flag)
{
	int sensitive;

	if (gpio->use_gpio == POWER_NOT_USE_GPIO)
		return 0;	
	if (gpio->gpio_iomux == POWER_GPIO_IOMUX)
	{
		rk2818_mux_api_set(gpio->iomux_name, gpio->iomux_value);
	}
	
	if (flag == GPIO_SWITCH_ON)
		sensitive = gpio->sensi_level;
	else
		sensitive = 1 - gpio->sensi_level;
	gpio_request(gpio->gpio_id, "wifi_power");	
	gpio_direction_output(gpio->gpio_id,sensitive);
	return 0;
}


int wifi_turn_on_card(void)
{
	
	if (wifi_gpio_operate(&power_gpio, GPIO_SWITCH_ON) != 0)
	{
		printk("Couldn't set GPIO [ON] successfully for power supply.\n");
		return -1;
	}
	
	return 0;
}

int wifi_turn_off_card(void)
{
	
	if (wifi_gpio_operate(&power_gpio, GPIO_SWITCH_OFF) != 0)
	{
		printk("Couldn't set GPIO [OFF] successfully for power supply.\n");
		return -1;
	}
	
	return 0;
}

int wifi_power_up_wifi(void)
{

	if (wifi_gpio_operate(&power_save_gpio, GPIO_SWITCH_ON) != 0)
	{
		printk("Couldn't set GPIO [ON] successfully for power up.\n");
		return -1;
	}
	mdelay(5);
	
	if (wifi_gpio_operate(&power_save_gpio, GPIO_SWITCH_OFF) != 0)
	{
		printk("Couldn't set GPIO [ON] successfully for power up.\n");
		return -1;
	}
	msleep(150);

	if (wifi_gpio_operate(&power_save_gpio, GPIO_SWITCH_ON) != 0)
	{
		printk("Couldn't set GPIO [ON] successfully for power up.\n");
		return -1;
	}
	msleep(50);

	return 0;
}

int wifi_power_down_wifi(void)
{
	if (wifi_gpio_operate(&power_save_gpio, GPIO_SWITCH_OFF) != 0)
	{
		printk("Couldn't set GPIO [OFF] successfully for power down.\n");
		return -1;
	}
	
	return 0;
}

#endif /* WIFI_GPIO_POWER_CONTROL */

