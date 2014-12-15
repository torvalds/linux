/*
 * gpio.h  --  GPIO Driver for Krosspower axp199 PMIC
 *
 * Copyright 2011 Krosspower Microelectronics PLC
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef _LINUX_AXP_GPIO_H_
#define _LINUX_AXP_GPIO_H_

/*
 * GPIO Registers.
 */
/*    AXP19   */
#define AXP19_GPIO0_CFG                   (POWER19_GPIO0_CTL)
#define AXP19_GPIO1_CFG                   (POWER19_GPIO1_CTL)
#define AXP19_GPIO2_CFG                   (POWER19_GPIO2_CTL)
#define AXP19_GPIO34_CFG                  (POWER19_SENSE_CTL)
#define AXP19_GPIO5_CFG                   (POWER19_RSTO_CTL)
#define AXP19_GPIO67_CFG0                 (POWER19_GPIO67_CFG)
#define AXP19_GPIO67_CFG1                 (POWER19_GPIO67_CTL)

#define AXP19_GPIO012_STATE               (POWER19_GPIO012_SIGNAL)
#define AXP19_GPIO34_STATE                (POWER19_SENSE_SIGNAL)
#define AXP19_GPIO5_STATE                 (POWER19_RSTO_CTL)
#define AXP19_GPIO67_STATE                (POWER19_GPIO67_CTL)


/*    AXP20   */
#define AXP20_GPIO0_CFG                   (POWER20_GPIO0_CTL)
#define AXP20_GPIO1_CFG                   (POWER20_GPIO1_CTL)
#define AXP20_GPIO2_CFG                   (POWER20_GPIO2_CTL)
#define AXP20_GPIO3_CFG                   (POWER20_GPIO3_CTL)

#define AXP20_GPIO012_STATE               (POWER20_GPIO012_SIGNAL)

extern int axp_gpio_set_io(int gpio, int io_state);
extern int axp_gpio_get_io(int gpio, int *io_state);
extern int axp_gpio_set_value(int gpio, int value);
extern int axp_gpio_get_value(int gpio, int *value);

typedef enum {
	AXP_GPIO0	=	0,
	AXP_GPIO1,
	AXP_GPIO2,
	AXP_GPIO3,
	AXP_GPIO_NULL,
} axp_gpio;

typedef enum {
	AXP_GPIO_INPUT = 0,
	AXP_GPIO_OUTPUT,
} axp_gpio_dir;

typedef enum {
	AXP_GPIO_LOW=	0,
	AXP_GPIO_HIGH,
} axp_gpio_level;

typedef struct axp_gpio_cfg{
	axp_gpio gpio;
	axp_gpio_dir dir;
	axp_gpio_level level;
}axp_gpio_cfg_t;

#define AXPGPIO_CFG_END_ITEM {.gpio=AXP_GPIO_NULL}

#endif
