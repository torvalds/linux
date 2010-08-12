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
#if defined(CONFIG_MACH_RAHO)
#include <linux/i2c.h>
#include <mach/spi_fpga.h>
#elif defined(CONFIG_MACH_RK2818INFO)
/* include gpio definition header file */
#endif
#include "wifi_power.h"

#if (WIFI_GPIO_POWER_CONTROL == 1)
#if defined(CONFIG_MACH_RAHO)
struct wifi_power power_gpio = 
{
	POWER_USE_GPIO, 0, 0, 0, SPI_GPIO_P1_06, SPI_GPIO_HIGH
};

struct wifi_power power_save_gpio = 
{
	POWER_USE_GPIO, 0, 0, 0, SPI_GPIO_P1_03, SPI_GPIO_HIGH
}; 
#elif defined(CONFIG_MACH_RK2818INFO)
struct wifi_power power_gpio = {
       0,0,0,0,0,0
};

struct wifi_power power_save_gpio = {
       0,0,0,0,0,0
}; 
#endif

int wifi_gpio_operate(struct wifi_power *gpio, int flag)
{
	int sensitive;

	if (gpio->use_gpio == POWER_NOT_USE_GPIO)
		return 0;	
	if (gpio->gpio_iomux == POWER_GPIO_IOMUX)
	{
		rk2818_mux_api_set(gpio->iomux_name, gpio->iomux_value);
	}

#if defined(CONFIG_MACH_RAHO)	
        spi_gpio_set_pindirection(gpio->gpio_id, SPI_GPIO_OUT); 
#elif defined(CONFIG_MACH_RK2818INFO)
        /* set pin direction */
#endif

	if (flag == GPIO_SWITCH_ON)
		sensitive = gpio->sensi_level;
	else
		sensitive = 1 - gpio->sensi_level;

#if defined(CONFIG_MACH_RAHO)
        spi_gpio_set_pinlevel(gpio->gpio_id, sensitive);
#elif defined(CONFIG_MACH_RK2818INFO)
       /* set pin level */ 
#endif

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

