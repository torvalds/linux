/*
 * wifi_power.h
 *
 * WIFI power control.
 *
 * Yongle Lai
 */

#ifndef WIFI_POWER_H
#define WIFI_POWER_H

#define WIFI_GPIO_POWER_CONTROL 1

#if (WIFI_GPIO_POWER_CONTROL == 1)

#include <mach/gpio.h>
#include <mach/iomux.h>

#define POWER_NOT_USE_GPIO		0
#define POWER_USE_GPIO				1

#define POWER_GPIO_NOT_IOMUX	0
#define POWER_GPIO_IOMUX			1

#define GPIO_SWITCH_OFF				0
#define GPIO_SWITCH_ON				1

struct wifi_power
{
	u8 use_gpio;			/* If uses GPIO to control wifi power supply. 0 - no, 1 - yes. */
	u8 gpio_iomux;		/* If the GPIO is iomux. 0 - no, 1 - yes. */
	char *iomux_name;	/* IOMUX name */
	u8	iomux_value;	/* IOMUX value - which function is choosen. */
	u8	gpio_id;			/* GPIO number */
	u8	sensi_level;	/* GPIO sensitive level. */
};

/*
 * Power supply via control GPIO.
 */
#define POWER_VIA_GPIO			1		/*  */
#define POWER_GPIO_MUTEX		1								/* 1 - GPIO is a io mutexed IO */
#define POWER_GPIO_MUTEX_NAME		GPIOH6_IQ_SEL_NAME
#define POWER_GPIO_MUTEXT_VAL		IOMUXB_GPIO1_D6
#define POWER_GPIO_ID						GPIOPortH_Pin6
#define POWER_LEVEL_SENSITIVE		GPIO_HIGH

int wifi_turn_on_card(void);
int wifi_turn_off_card(void);
int wifi_power_up_wifi(void);
int wifi_power_down_wifi(void);
int wifi_power_reset(void);

#endif /* WIFI_GPIO_POWER_CONTROL */

#endif /* WIFI_POWER_H */
