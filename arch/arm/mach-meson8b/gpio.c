
/*
 * Driver for the AMLOGIC  GPIO
 *
 * Copyright (c) AMLOGIC CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/consumer.h>
#include <mach/am_regs.h>
#include <plat/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/amlogic/gpio-amlogic.h>
#define AO 10
#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif
unsigned p_gpio_oen_addr[]={
	P_PREG_PAD_GPIO0_EN_N,
	P_PREG_PAD_GPIO1_EN_N,
	P_PREG_PAD_GPIO2_EN_N,
	P_PREG_PAD_GPIO3_EN_N,
	P_PREG_PAD_GPIO4_EN_N,
	P_PREG_PAD_GPIO5_EN_N,
	P_AO_GPIO_O_EN_N,
};
static unsigned p_gpio_output_addr[]={
	P_PREG_PAD_GPIO0_O,
	P_PREG_PAD_GPIO1_O,
	P_PREG_PAD_GPIO2_O,
	P_PREG_PAD_GPIO3_O,
	P_PREG_PAD_GPIO4_O,
	P_PREG_PAD_GPIO5_O,
	P_AO_GPIO_O_EN_N,
};
static unsigned p_gpio_input_addr[]={
	P_PREG_PAD_GPIO0_I,
	P_PREG_PAD_GPIO1_I,
	P_PREG_PAD_GPIO2_I,
	P_PREG_PAD_GPIO3_I,
	P_PREG_PAD_GPIO4_I,
	P_PREG_PAD_GPIO5_I,
	P_AO_GPIO_I,
};
extern int m8b_pin_to_pullup(unsigned int pin ,unsigned int *reg,unsigned int *bit,unsigned int *bit_en);

extern struct amlogic_set_pullup pullup_ops;
extern unsigned p_pull_up_addr[];
extern unsigned p_pull_upen_addr[];
extern unsigned int p_pin_mux_reg_addr[];
extern int gpio_irq;
extern int gpio_flag;
#define NONE 0xffffffff
//#define debug
#ifdef debug
	#define gpio_print(...) printk(__VA_ARGS__)
#else 
	#define gpio_print(...)
#endif
//gpio subsystem set pictrl subsystem gpio owner
enum gpio_reg_type
{
	INPUT_REG,
	OUTPUT_REG,
	OUTPUTEN_REG
};

#define PIN_MAP(pin,reg,bit) \
{ \
	.num=pin, \
	.name=#pin, \
	.out_en_reg_bit=GPIO_REG_BIT(reg,bit), \
	.out_value_reg_bit=GPIO_REG_BIT(reg,bit), \
	.input_value_reg_bit=GPIO_REG_BIT(reg,bit), \
}
#define PIN_AOMAP(pin,en_reg,en_bit,out_reg,out_bit,in_reg,in_bit) \
{ \
	.num=pin, \
	.name=#pin, \
	.out_en_reg_bit=GPIO_REG_BIT(en_reg,en_bit), \
	.out_value_reg_bit=GPIO_REG_BIT(out_reg,out_bit), \
	.input_value_reg_bit=GPIO_REG_BIT(in_reg,in_bit), \
	.gpio_owner=NULL, \
}

#define P_PIN_MUX_REG(reg,bit) ((reg<<5)|bit)
static unsigned int gpio_to_pin[][6]={
	[GPIOX_0]={P_PIN_MUX_REG(8,5),P_PIN_MUX_REG(5,14),P_PIN_MUX_REG(9,24),NONE,NONE,NONE,},
	[GPIOX_1]={P_PIN_MUX_REG(8,4),P_PIN_MUX_REG(5,13),P_PIN_MUX_REG(9,23),NONE,NONE,NONE,},
	[GPIOX_2]={P_PIN_MUX_REG(8,3),P_PIN_MUX_REG(5,13),P_PIN_MUX_REG(9,22),NONE,NONE,NONE,},
	[GPIOX_3]={P_PIN_MUX_REG(8,2),P_PIN_MUX_REG(5,13),P_PIN_MUX_REG(9,21),NONE,NONE,NONE,},
	[GPIOX_4]={P_PIN_MUX_REG(5,29),P_PIN_MUX_REG(5,12),P_PIN_MUX_REG(3,30),P_PIN_MUX_REG(4,17),NONE,NONE,},
	[GPIOX_5]={P_PIN_MUX_REG(5,28),P_PIN_MUX_REG(5,12),P_PIN_MUX_REG(3,29),P_PIN_MUX_REG(4,16),NONE,NONE,},
	[GPIOX_6]={P_PIN_MUX_REG(5,28),P_PIN_MUX_REG(5,12),P_PIN_MUX_REG(3,28),P_PIN_MUX_REG(4,15),P_PIN_MUX_REG(5,9),NONE,},
	[GPIOX_7]={P_PIN_MUX_REG(5,28),P_PIN_MUX_REG(5,12),P_PIN_MUX_REG(3,27),P_PIN_MUX_REG(4,14),P_PIN_MUX_REG(5,8),NONE,},
	[GPIOX_8]={P_PIN_MUX_REG(8,1),P_PIN_MUX_REG(5,11),P_PIN_MUX_REG(6,19),P_PIN_MUX_REG(4,22),P_PIN_MUX_REG(3,6),NONE,},
	[GPIOX_9]={P_PIN_MUX_REG(8,0),P_PIN_MUX_REG(5,10),P_PIN_MUX_REG(6,18),P_PIN_MUX_REG(4,24),P_PIN_MUX_REG(3,7),NONE,},
	[GPIOX_10]={P_PIN_MUX_REG(3,22),P_PIN_MUX_REG(7,31),P_PIN_MUX_REG(9,19),P_PIN_MUX_REG(6,17),P_PIN_MUX_REG(4,23),P_PIN_MUX_REG(3,8),},
	[GPIOX_11]={P_PIN_MUX_REG(3,20),P_PIN_MUX_REG(7,30),P_PIN_MUX_REG(3,14),P_PIN_MUX_REG(2,3),NONE,NONE,},
	[GPIOX_12]={P_PIN_MUX_REG(4,13),P_PIN_MUX_REG(3,13),NONE,NONE,NONE,NONE,},
	[GPIOX_13]={P_PIN_MUX_REG(4,12),P_PIN_MUX_REG(3,12),NONE,NONE,NONE,NONE,},
	[GPIOX_14]={P_PIN_MUX_REG(4,11),P_PIN_MUX_REG(3,12),NONE,NONE,NONE,NONE,},
	[GPIOX_15]={P_PIN_MUX_REG(4,10),P_PIN_MUX_REG(3,12),NONE,NONE,NONE,NONE,},
	[GPIOX_16]={P_PIN_MUX_REG(4,9),P_PIN_MUX_REG(3,12),P_PIN_MUX_REG(4,21),P_PIN_MUX_REG(4,5),NONE,NONE,},
	[GPIOX_17]={P_PIN_MUX_REG(4,8),P_PIN_MUX_REG(3,12),P_PIN_MUX_REG(4,20),P_PIN_MUX_REG(4,4),NONE,NONE,},
	[GPIOX_18]={P_PIN_MUX_REG(4,7),P_PIN_MUX_REG(3,12),P_PIN_MUX_REG(4,19),NONE,NONE,NONE,},
	[GPIOX_19]={P_PIN_MUX_REG(4,6),P_PIN_MUX_REG(3,12),P_PIN_MUX_REG(4,18),NONE,NONE,NONE,},
	[GPIOX_20]={P_PIN_MUX_REG(6,16),P_PIN_MUX_REG(4,25),P_PIN_MUX_REG(3,9),NONE,NONE,NONE,},
	[GPIOX_21]={NONE,NONE,NONE,NONE,NONE,NONE,},
	[GPIOY_0]={P_PIN_MUX_REG(3,2),P_PIN_MUX_REG(9,9),NONE,NONE,NONE,NONE,},
	[GPIOY_1]={P_PIN_MUX_REG(3,1),P_PIN_MUX_REG(9,8),NONE,NONE,NONE,NONE,},
	[GPIOY_2]={P_PIN_MUX_REG(1,17),P_PIN_MUX_REG(1,8),P_PIN_MUX_REG(1,9),NONE,NONE,NONE,},
	[GPIOY_3]={P_PIN_MUX_REG(1,16),P_PIN_MUX_REG(1,7),P_PIN_MUX_REG(3,18),NONE,NONE,NONE,},
	[GPIOY_4]={P_PIN_MUX_REG(1,19),P_PIN_MUX_REG(1,10),P_PIN_MUX_REG(3,3),NONE,NONE,NONE,},
	[GPIOY_5]={P_PIN_MUX_REG(1,18),P_PIN_MUX_REG(1,5),P_PIN_MUX_REG(1,11),NONE,NONE,NONE,},
	[GPIOY_6]={P_PIN_MUX_REG(3,5),P_PIN_MUX_REG(9,6),NONE,NONE,NONE,NONE,},
	[GPIOY_7]={P_PIN_MUX_REG(3,5),P_PIN_MUX_REG(9,5),NONE,NONE,NONE,NONE,},
	[GPIOY_8]={P_PIN_MUX_REG(3,0),P_PIN_MUX_REG(9,4),NONE,NONE,NONE,NONE,},
	[GPIOY_9]={P_PIN_MUX_REG(3,4),P_PIN_MUX_REG(9,3),NONE,NONE,NONE,NONE,},
	[GPIOY_10]={P_PIN_MUX_REG(3,5),P_PIN_MUX_REG(9,3),NONE,NONE,NONE,NONE,},
	[GPIOY_11]={P_PIN_MUX_REG(3,5),P_PIN_MUX_REG(9,3),NONE,NONE,NONE,NONE,},
	[GPIOY_12]={P_PIN_MUX_REG(3,5),P_PIN_MUX_REG(9,3),NONE,NONE,NONE,NONE,},
	[GPIOY_13]={P_PIN_MUX_REG(3,5),P_PIN_MUX_REG(5,7),P_PIN_MUX_REG(9,3),NONE,NONE,NONE,},
	[GPIOY_14]={P_PIN_MUX_REG(3,5),P_PIN_MUX_REG(5,6),P_PIN_MUX_REG(9,2),NONE,NONE,NONE,},
	[GPIOY_15]={P_PIN_MUX_REG(9,15),NONE,NONE,NONE,NONE,NONE,},
	[GPIOY_16]={P_PIN_MUX_REG(9,14),P_PIN_MUX_REG(7,29),NONE,NONE,NONE,NONE,},
	[GPIODV_0]={P_PIN_MUX_REG(0,1),P_PIN_MUX_REG(8,27),P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,0),NONE,NONE,},
	[GPIODV_1]={P_PIN_MUX_REG(0,1),P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,1),NONE,NONE,NONE,},
	[GPIODV_2]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,2),NONE,NONE,NONE,NONE,},
	[GPIODV_3]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,3),NONE,NONE,NONE,NONE,},
	[GPIODV_4]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,4),NONE,NONE,NONE,NONE,},
	[GPIODV_5]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,5),NONE,NONE,NONE,NONE,},
	[GPIODV_6]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,6),NONE,NONE,NONE,NONE,},
	[GPIODV_7]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,7),NONE,NONE,NONE,NONE,},
	[GPIODV_8]={P_PIN_MUX_REG(0,3),P_PIN_MUX_REG(8,26),P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,8),NONE,NONE,},
	[GPIODV_9]={P_PIN_MUX_REG(0,3),P_PIN_MUX_REG(7,28),P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,9),P_PIN_MUX_REG(3,24),NONE,},
	[GPIODV_10]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,10),NONE,NONE,NONE,NONE,},
	[GPIODV_11]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,11),NONE,NONE,NONE,NONE,},
	[GPIODV_12]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,12),NONE,NONE,NONE,NONE,},
	[GPIODV_13]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,13),NONE,NONE,NONE,NONE,},
	[GPIODV_14]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,14),NONE,NONE,NONE,NONE,},
	[GPIODV_15]={P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,15),NONE,NONE,NONE,NONE,},
	[GPIODV_16]={P_PIN_MUX_REG(0,5),P_PIN_MUX_REG(8,25),P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,16),NONE,NONE,},
	[GPIODV_17]={P_PIN_MUX_REG(0,5),P_PIN_MUX_REG(0,6),P_PIN_MUX_REG(7,17),NONE,NONE,NONE,},
	[GPIODV_18]={P_PIN_MUX_REG(0,6),NONE,NONE,NONE,NONE,NONE,},
	[GPIODV_19]={P_PIN_MUX_REG(0,6),NONE,NONE,NONE,NONE,NONE,},
	[GPIODV_20]={P_PIN_MUX_REG(0,6),NONE,NONE,NONE,NONE,NONE,},
	[GPIODV_21]={P_PIN_MUX_REG(0,6),NONE,NONE,NONE,NONE,NONE,},
	[GPIODV_22]={P_PIN_MUX_REG(0,6),NONE,NONE,NONE,NONE,NONE,},
	[GPIODV_23]={P_PIN_MUX_REG(0,6),NONE,NONE,NONE,NONE,NONE,},
	[GPIODV_24]={P_PIN_MUX_REG(0,19),P_PIN_MUX_REG(0,21),P_PIN_MUX_REG(8,24),P_PIN_MUX_REG(0,9),P_PIN_MUX_REG(6,23),P_PIN_MUX_REG(9,31),},
	[GPIODV_25]={P_PIN_MUX_REG(0,18),P_PIN_MUX_REG(0,20),P_PIN_MUX_REG(8,23),P_PIN_MUX_REG(0,8),P_PIN_MUX_REG(6,22),P_PIN_MUX_REG(9,30),},
	[GPIODV_26]={P_PIN_MUX_REG(8,22),P_PIN_MUX_REG(8,21),P_PIN_MUX_REG(8,20),P_PIN_MUX_REG(0,7),P_PIN_MUX_REG(6,21),P_PIN_MUX_REG(9,29),},
	[GPIODV_27]={P_PIN_MUX_REG(8,19),P_PIN_MUX_REG(8,28),P_PIN_MUX_REG(0,10),P_PIN_MUX_REG(6,20),P_PIN_MUX_REG(9,28),NONE,},
	[GPIODV_28]={P_PIN_MUX_REG(3,26),P_PIN_MUX_REG(7,27),P_PIN_MUX_REG(9,27),NONE,NONE,NONE,},
	[GPIODV_29]={P_PIN_MUX_REG(3,25),P_PIN_MUX_REG(7,26),P_PIN_MUX_REG(7,25),P_PIN_MUX_REG(9,26),NONE,NONE,},
	[CARD_0]={P_PIN_MUX_REG(2,14),P_PIN_MUX_REG(2,6),NONE,NONE,NONE,NONE,},
	[CARD_1]={P_PIN_MUX_REG(2,15),P_PIN_MUX_REG(2,7),NONE,NONE,NONE,NONE,},
	[CARD_2]={P_PIN_MUX_REG(2,11),P_PIN_MUX_REG(2,5),NONE,NONE,NONE,NONE,},
	[CARD_3]={P_PIN_MUX_REG(2,10),P_PIN_MUX_REG(2,4),NONE,NONE,NONE,NONE,},
	[CARD_4]={P_PIN_MUX_REG(2,12),P_PIN_MUX_REG(2,6),P_PIN_MUX_REG(8,10),P_PIN_MUX_REG(8,8),NONE,NONE,},
	[CARD_5]={P_PIN_MUX_REG(2,13),P_PIN_MUX_REG(2,6),P_PIN_MUX_REG(8,9),P_PIN_MUX_REG(8,7),NONE,NONE,},
	[CARD_6]={NONE,NONE,NONE,NONE,NONE,NONE,},
	[BOOT_0]={P_PIN_MUX_REG(2,26),P_PIN_MUX_REG(4,30),P_PIN_MUX_REG(6,29),NONE,NONE,NONE,},
	[BOOT_1]={P_PIN_MUX_REG(2,26),P_PIN_MUX_REG(4,29),P_PIN_MUX_REG(6,28),NONE,NONE,NONE,},
	[BOOT_2]={P_PIN_MUX_REG(2,26),P_PIN_MUX_REG(4,29),P_PIN_MUX_REG(6,27),NONE,NONE,NONE,},
	[BOOT_3]={P_PIN_MUX_REG(2,26),P_PIN_MUX_REG(4,29),P_PIN_MUX_REG(6,26),NONE,NONE,NONE,},
	[BOOT_4]={P_PIN_MUX_REG(2,26),P_PIN_MUX_REG(4,28),NONE,NONE,NONE,NONE,},
	[BOOT_5]={P_PIN_MUX_REG(2,26),P_PIN_MUX_REG(4,28),NONE,NONE,NONE,NONE,},
	[BOOT_6]={P_PIN_MUX_REG(2,26),P_PIN_MUX_REG(4,28),NONE,NONE,NONE,NONE,},
	[BOOT_7]={P_PIN_MUX_REG(2,26),P_PIN_MUX_REG(4,28),NONE,NONE,NONE,NONE,},
	[BOOT_8]={P_PIN_MUX_REG(2,25),P_PIN_MUX_REG(7,19),P_PIN_MUX_REG(6,31),NONE,NONE,NONE,},
	[BOOT_9]={P_PIN_MUX_REG(2,24),NONE,NONE,NONE,NONE,NONE,},
	[BOOT_10]={P_PIN_MUX_REG(2,17),P_PIN_MUX_REG(7,18),P_PIN_MUX_REG(6,30),NONE,NONE,NONE,},
	[BOOT_11]={P_PIN_MUX_REG(2,21),P_PIN_MUX_REG(5,1),NONE,NONE,NONE,NONE,},
	[BOOT_12]={P_PIN_MUX_REG(2,20),P_PIN_MUX_REG(5,3),NONE,NONE,NONE,NONE,},
	[BOOT_13]={P_PIN_MUX_REG(2,19),P_PIN_MUX_REG(5,2),NONE,NONE,NONE,NONE,},
	[BOOT_14]={P_PIN_MUX_REG(2,18),NONE,NONE,NONE,NONE,NONE,},
	[BOOT_15]={P_PIN_MUX_REG(2,27),NONE,NONE,NONE,NONE,NONE,},
	[BOOT_16]={P_PIN_MUX_REG(2,23),P_PIN_MUX_REG(4,27),P_PIN_MUX_REG(6,25),NONE,NONE,NONE,},
	[BOOT_17]={P_PIN_MUX_REG(2,22),P_PIN_MUX_REG(4,26),P_PIN_MUX_REG(6,24),NONE,NONE,NONE,},
	[BOOT_18]={P_PIN_MUX_REG(2,28),P_PIN_MUX_REG(5,0),NONE,NONE,NONE,NONE,},
	[GPIOH_0]={P_PIN_MUX_REG(1,26),P_PIN_MUX_REG(8,11),P_PIN_MUX_REG(1,4),NONE,NONE,NONE,},
	[GPIOH_1]={P_PIN_MUX_REG(1,25),P_PIN_MUX_REG(8,12),P_PIN_MUX_REG(1,3),P_PIN_MUX_REG(1,2),NONE,NONE,},
	[GPIOH_2]={P_PIN_MUX_REG(1,24),P_PIN_MUX_REG(1,1),NONE,NONE,NONE,NONE,},
	[GPIOH_3]={P_PIN_MUX_REG(1,23),P_PIN_MUX_REG(9,13),P_PIN_MUX_REG(1,0),P_PIN_MUX_REG(5,27),NONE,NONE,},
	[GPIOH_4]={P_PIN_MUX_REG(9,12),P_PIN_MUX_REG(8,18),P_PIN_MUX_REG(5,26),NONE,NONE,NONE,},
	[GPIOH_5]={P_PIN_MUX_REG(7,21),P_PIN_MUX_REG(9,11),P_PIN_MUX_REG(8,18),P_PIN_MUX_REG(5,25),P_PIN_MUX_REG(6,15),NONE,},
	[GPIOH_6]={P_PIN_MUX_REG(7,20),P_PIN_MUX_REG(9,10),P_PIN_MUX_REG(8,17),P_PIN_MUX_REG(5,24),P_PIN_MUX_REG(6,14),NONE,},
	[GPIOH_7]={P_PIN_MUX_REG(8,17),P_PIN_MUX_REG(4,3),P_PIN_MUX_REG(6,13),NONE,NONE,NONE,},
	[GPIOH_8]={P_PIN_MUX_REG(8,16),P_PIN_MUX_REG(4,2),P_PIN_MUX_REG(6,12),NONE,NONE,NONE,},
	[GPIOH_9]={P_PIN_MUX_REG(4,1),P_PIN_MUX_REG(8,16),P_PIN_MUX_REG(6,11),NONE,NONE,NONE,},
	[GPIOAO_0]={P_PIN_MUX_REG(AO,12),P_PIN_MUX_REG(AO,26),NONE,NONE,NONE,NONE,},
	[GPIOAO_1]={P_PIN_MUX_REG(AO,11),P_PIN_MUX_REG(AO,25),NONE,NONE,NONE,NONE,},
	[GPIOAO_2]={P_PIN_MUX_REG(AO,10),P_PIN_MUX_REG(AO,8),NONE,NONE,NONE,NONE,},
	[GPIOAO_3]={P_PIN_MUX_REG(AO,9),P_PIN_MUX_REG(AO,22),P_PIN_MUX_REG(AO,7),NONE,NONE,NONE,},
	[GPIOAO_4]={P_PIN_MUX_REG(AO,6),P_PIN_MUX_REG(AO,2),P_PIN_MUX_REG(AO,24),NONE,NONE,NONE,},
	[GPIOAO_5]={P_PIN_MUX_REG(AO,5),P_PIN_MUX_REG(AO,1),P_PIN_MUX_REG(AO,23),NONE,NONE,NONE,},
	[GPIOAO_6]={P_PIN_MUX_REG(AO,18),P_PIN_MUX_REG(AO,16),P_PIN_MUX_REG(1,13),NONE,NONE,NONE,},
	[GPIOAO_7]={P_PIN_MUX_REG(AO,0),P_PIN_MUX_REG(AO,21),NONE,NONE,NONE,NONE,},
	[GPIOAO_8]={P_PIN_MUX_REG(AO,30),NONE,NONE,NONE,NONE,NONE,},
	[GPIOAO_9]={P_PIN_MUX_REG(AO,29),P_PIN_MUX_REG(1,15),NONE,NONE,NONE,NONE,},
	[GPIOAO_10]={P_PIN_MUX_REG(AO,28),P_PIN_MUX_REG(1,14),NONE,NONE,NONE,NONE,},
	[GPIOAO_11]={P_PIN_MUX_REG(AO,27),NONE,NONE,NONE,NONE,NONE,},
	[GPIOAO_12]={P_PIN_MUX_REG(AO,17),NONE,NONE,NONE,NONE,NONE,},
	[GPIOAO_13]={P_PIN_MUX_REG(AO,31),P_PIN_MUX_REG(AO,19),NONE,NONE,NONE,NONE,},
	[DIF_TTL_0_P]={P_PIN_MUX_REG(0,24),P_PIN_MUX_REG(6,0),NONE,NONE,NONE,NONE,},
	[DIF_TTL_0_N]={P_PIN_MUX_REG(0,24),P_PIN_MUX_REG(6,1),NONE,NONE,NONE,NONE,},
	[DIF_TTL_1_P]={P_PIN_MUX_REG(0,24),P_PIN_MUX_REG(6,2),NONE,NONE,NONE,NONE,},
	[DIF_TTL_1_N]={P_PIN_MUX_REG(0,24),P_PIN_MUX_REG(6,3),NONE,NONE,NONE,NONE,},
	[DIF_TTL_2_P]={P_PIN_MUX_REG(0,25),P_PIN_MUX_REG(6,4),P_PIN_MUX_REG(7,22),NONE,NONE,NONE,},
	[DIF_TTL_2_N]={P_PIN_MUX_REG(0,25),P_PIN_MUX_REG(6,5),P_PIN_MUX_REG(7,23),NONE,NONE,NONE,},
	[DIF_TTL_3_P]={P_PIN_MUX_REG(0,25),P_PIN_MUX_REG(6,6),NONE,NONE,NONE,NONE,},
	[DIF_TTL_3_N]={P_PIN_MUX_REG(0,25),P_PIN_MUX_REG(6,8),NONE,NONE,NONE,NONE,},
	[DIF_TTL_4_P]={P_PIN_MUX_REG(0,25),P_PIN_MUX_REG(6,9),NONE,NONE,NONE,NONE,},
	[DIF_TTL_4_N]={P_PIN_MUX_REG(0,25),P_PIN_MUX_REG(6,10),NONE,NONE,NONE,NONE,},
	[HDMI_TTL_0_P]={P_PIN_MUX_REG(0,22),NONE,NONE,NONE,NONE,NONE,},
	[HDMI_TTL_0_N]={P_PIN_MUX_REG(0,22),NONE,NONE,NONE,NONE,NONE,},
	[HDMI_TTL_1_P]={P_PIN_MUX_REG(0,22),NONE,NONE,NONE,NONE,NONE,},
	[HDMI_TTL_1_N]={P_PIN_MUX_REG(0,22),NONE,NONE,NONE,NONE,NONE,},
	[HDMI_TTL_2_P]={P_PIN_MUX_REG(0,22),NONE,NONE,NONE,NONE,NONE,},
	[HDMI_TTL_2_N]={P_PIN_MUX_REG(0,22),NONE,NONE,NONE,NONE,NONE,},
	[HDMI_TTL_CK_P]={P_PIN_MUX_REG(0,23),NONE,NONE,NONE,NONE,NONE,},
	[HDMI_TTL_CK_N]={P_PIN_MUX_REG(0,23),NONE,NONE,NONE,NONE,NONE,},
	[GPIO_BSD_EN]={NONE,NONE,NONE,NONE,NONE,NONE,},
	[GPIO_TEST_N]={P_PIN_MUX_REG(10,19),NONE,NONE,NONE,NONE,NONE,},
};
struct amlogic_gpio_desc amlogic_pins[]=
{
	PIN_AOMAP(GPIOAO_0,6,0,6,16,6,0),
	PIN_AOMAP(GPIOAO_1,6,1,6,17,6,1),
	PIN_AOMAP(GPIOAO_2,6,2,6,18,6,2),
	PIN_AOMAP(GPIOAO_3,6,3,6,19,6,3),
	PIN_AOMAP(GPIOAO_4,6,4,6,20,6,4),
	PIN_AOMAP(GPIOAO_5,6,5,6,21,6,5),
	PIN_AOMAP(GPIOAO_6,6,6,6,22,6,6),
	PIN_AOMAP(GPIOAO_7,6,7,6,23,6,7),
	PIN_AOMAP(GPIOAO_8,6,8,6,24,6,8),
	PIN_AOMAP(GPIOAO_9,6,9,6,25,6,9),
	PIN_AOMAP(GPIOAO_10,6,10,6,26,6,10),
	PIN_AOMAP(GPIOAO_11,6,11,6,27,6,11),
	PIN_AOMAP(GPIOAO_12,6,12,6,28,6,12),
	PIN_AOMAP(GPIOAO_13,6,13,6,29,6,13),
	PIN_MAP(GPIOH_0,3,19),
	PIN_MAP(GPIOH_1,3,20),
	PIN_MAP(GPIOH_2,3,21),
	PIN_MAP(GPIOH_3,3,22),
	PIN_MAP(GPIOH_4,3,23),
	PIN_MAP(GPIOH_5,3,24),
	PIN_MAP(GPIOH_6,3,25),
	PIN_MAP(GPIOH_7,3,26),
	PIN_MAP(GPIOH_8,3,27),
	PIN_MAP(GPIOH_9,3,28),
	PIN_MAP(BOOT_0,3,0),
	PIN_MAP(BOOT_1,3,1),
	PIN_MAP(BOOT_2,3,2),
	PIN_MAP(BOOT_3,3,3),
	PIN_MAP(BOOT_4,3,4),
	PIN_MAP(BOOT_5,3,5),
	PIN_MAP(BOOT_6,3,6),
	PIN_MAP(BOOT_7,3,7),
	PIN_MAP(BOOT_8,3,8),
	PIN_MAP(BOOT_9,3,9),
	PIN_MAP(BOOT_10,3,10),
	PIN_MAP(BOOT_11,3,11),
	PIN_MAP(BOOT_12,3,12),
	PIN_MAP(BOOT_13,3,13),
	PIN_MAP(BOOT_14,3,14),
	PIN_MAP(BOOT_15,3,15),
	PIN_MAP(BOOT_16,3,16),
	PIN_MAP(BOOT_17,3,17),
	PIN_MAP(BOOT_18,3,18),
	PIN_MAP(CARD_0,0,22),
	PIN_MAP(CARD_1,0,23),
	PIN_MAP(CARD_2,0,24),
	PIN_MAP(CARD_3,0,25),
	PIN_MAP(CARD_4,0,26),
	PIN_MAP(CARD_5,0,27),
	PIN_MAP(CARD_6,0,28),
	PIN_MAP(GPIODV_0,2,0),
	PIN_MAP(GPIODV_1,2,1),
	PIN_MAP(GPIODV_2,2,2),
	PIN_MAP(GPIODV_3,2,3),
	PIN_MAP(GPIODV_4,2,4),
	PIN_MAP(GPIODV_5,2,5),
	PIN_MAP(GPIODV_6,2,6),
	PIN_MAP(GPIODV_7,2,7),
	PIN_MAP(GPIODV_8,2,8),
	PIN_MAP(GPIODV_9,2,9),
	PIN_MAP(GPIODV_10,2,10),
	PIN_MAP(GPIODV_11,2,11),
	PIN_MAP(GPIODV_12,2,12),
	PIN_MAP(GPIODV_13,2,13),
	PIN_MAP(GPIODV_14,2,14),
	PIN_MAP(GPIODV_15,2,15),
	PIN_MAP(GPIODV_16,2,16),
	PIN_MAP(GPIODV_17,2,17),
	PIN_MAP(GPIODV_18,2,18),
	PIN_MAP(GPIODV_19,2,19),
	PIN_MAP(GPIODV_20,2,20),
	PIN_MAP(GPIODV_21,2,21),
	PIN_MAP(GPIODV_22,2,22),
	PIN_MAP(GPIODV_23,2,23),
	PIN_MAP(GPIODV_24,2,24),
	PIN_MAP(GPIODV_25,2,25),
	PIN_MAP(GPIODV_26,2,26),
	PIN_MAP(GPIODV_27,2,27),
	PIN_MAP(GPIODV_28,2,28),
	PIN_MAP(GPIODV_29,2,29),
	PIN_MAP(GPIOY_0,1,0),
	PIN_MAP(GPIOY_1,1,1),
	PIN_MAP(GPIOY_2,1,2),
	PIN_MAP(GPIOY_3,1,3),
	PIN_MAP(GPIOY_4,1,4),
	PIN_MAP(GPIOY_5,1,5),
	PIN_MAP(GPIOY_6,1,6),
	PIN_MAP(GPIOY_7,1,7),
	PIN_MAP(GPIOY_8,1,8),
	PIN_MAP(GPIOY_9,1,9),
	PIN_MAP(GPIOY_10,1,10),
	PIN_MAP(GPIOY_11,1,11),
	PIN_MAP(GPIOY_12,1,12),
	PIN_MAP(GPIOY_13,1,13),
	PIN_MAP(GPIOY_14,1,14),
	PIN_MAP(GPIOY_15,1,15),
	PIN_MAP(GPIOY_16,1,16),
	PIN_MAP(GPIOX_0,0,0),
	PIN_MAP(GPIOX_1,0,1),
	PIN_MAP(GPIOX_2,0,2),
	PIN_MAP(GPIOX_3,0,3),
	PIN_MAP(GPIOX_4,0,4),
	PIN_MAP(GPIOX_5,0,5),
	PIN_MAP(GPIOX_6,0,6),
	PIN_MAP(GPIOX_7,0,7),
	PIN_MAP(GPIOX_8,0,8),
	PIN_MAP(GPIOX_9,0,9),
	PIN_MAP(GPIOX_10,0,10),
	PIN_MAP(GPIOX_11,0,11),
	PIN_MAP(GPIOX_12,0,12),
	PIN_MAP(GPIOX_13,0,13),
	PIN_MAP(GPIOX_14,0,14),
	PIN_MAP(GPIOX_15,0,15),
	PIN_MAP(GPIOX_16,0,16),
	PIN_MAP(GPIOX_17,0,17),
	PIN_MAP(GPIOX_18,0,18),
	PIN_MAP(GPIOX_19,0,19),
	PIN_MAP(GPIOX_20,0,20),
	PIN_MAP(GPIOX_21,0,21),
	PIN_MAP(DIF_TTL_0_P,4,12),
	PIN_MAP(DIF_TTL_0_N,4,13),
	PIN_MAP(DIF_TTL_1_P,4,14),
	PIN_MAP(DIF_TTL_1_N,4,15),
	PIN_MAP(DIF_TTL_2_P,4,16),
	PIN_MAP(DIF_TTL_2_N,4,17),
	PIN_MAP(DIF_TTL_3_P,4,18),
	PIN_MAP(DIF_TTL_3_N,4,19),
	PIN_MAP(DIF_TTL_4_P,4,20),
	PIN_MAP(DIF_TTL_4_N,4,21),
	PIN_MAP(HDMI_TTL_0_P,4,22),
	PIN_MAP(HDMI_TTL_0_N,4,23),
	PIN_MAP(HDMI_TTL_1_P,4,24),
	PIN_MAP(HDMI_TTL_1_N,4,25),
	PIN_MAP(HDMI_TTL_2_P,4,26),
	PIN_MAP(HDMI_TTL_2_N,4,27),
	PIN_MAP(HDMI_TTL_CK_P,4,28),
	PIN_MAP(HDMI_TTL_CK_N,4,29),
	PIN_AOMAP(GPIO_BSD_EN,0,30,0,31,0,0x1f),
	PIN_AOMAP(GPIO_TEST_N,0,0,4,31,0,0),
};
int gpio_amlogic_requst(struct gpio_chip *chip,unsigned offset)
{
	int ret;
	unsigned int i,reg,bit;
	unsigned int *gpio_reg=&gpio_to_pin[offset][0];
	ret=pinctrl_request_gpio(offset);
	gpio_print("==%s==%d\n",__FUNCTION__,__LINE__);
	if(!ret){
		for(i=0;i<sizeof(gpio_to_pin[offset])/sizeof(gpio_to_pin[offset][0]);i++){
			if(gpio_reg[i]!=NONE)
			{
				reg=GPIO_REG(gpio_reg[i]);
				bit=GPIO_BIT(gpio_reg[i]);
				aml_clr_reg32_mask(p_pin_mux_reg_addr[reg],1<<bit);
				gpio_print("clr reg=%d,bit =%d\n",reg,bit);
			}
		}
	}
	return ret;
}
/* amlogic request gpio interface*/

void	 gpio_amlogic_free(struct gpio_chip *chip,unsigned offset)
{	
	 pinctrl_free_gpio(offset);
	return;
}
int gpio_amlogic_to_irq(struct gpio_chip *chip,unsigned offset)
{
	unsigned reg,start_bit;
	unsigned irq_bank=gpio_flag&0x7;
	unsigned filter=(gpio_flag>>8)&0x7;
	unsigned irq_type=(gpio_flag>>16)&0x3;
	unsigned type[]={0x0, 	/*GPIO_IRQ_HIGH*/
				0x10000, /*GPIO_IRQ_LOW*/
				0x1,  	/*GPIO_IRQ_RISING*/
				0x10001, /*GPIO_IRQ_FALLING*/
				};
	 /*set trigger type*/
	if(offset>GPIOX_21)
		return -1;
	aml_clrset_reg32_bits(P_GPIO_INTR_EDGE_POL,0x10001<<irq_bank,type[irq_type]<<irq_bank);
	printk(" reg:%x,clearmask=%x,setmask=%x\n",(P_GPIO_INTR_EDGE_POL&0xffff)>>2,0x10001<<irq_bank,(aml_read_reg32(P_GPIO_INTR_EDGE_POL)>>irq_bank)&0x10001);
	/*select pin*/
	reg=irq_bank<4?P_GPIO_INTR_GPIO_SEL0:P_GPIO_INTR_GPIO_SEL1;
	start_bit=(irq_bank&3)*8;
	aml_clrset_reg32_bits(reg,0xff<<start_bit,amlogic_pins[offset].num<<start_bit);
	printk("reg:%x,clearmask=%x,set pin=%d\n",(reg&0xffff)>>2,0xff<<start_bit,(aml_read_reg32(reg)>>start_bit)&0xff);
	/*set filter*/
	start_bit=(irq_bank)*4;
	aml_clrset_reg32_bits(P_GPIO_INTR_FILTER_SEL0,0x7<<start_bit,filter<<start_bit);
	printk("reg:%x,clearmask=%x,setmask=%x\n",(P_GPIO_INTR_FILTER_SEL0&0xffff)>>2,0x7<<start_bit,(aml_read_reg32(P_GPIO_INTR_FILTER_SEL0)>>start_bit)&0x7);
	return 0;
}

int gpio_amlogic_direction_input(struct gpio_chip *chip,unsigned offset)
{
	unsigned int reg,bit;
	gpio_print("==%s==%d\n",__FUNCTION__,__LINE__);
	reg=GPIO_REG(amlogic_pins[offset].out_en_reg_bit);
	bit=GPIO_BIT(amlogic_pins[offset].out_en_reg_bit);
	aml_set_reg32_mask(p_gpio_oen_addr[reg],1<<bit);
	return 0;
}

int gpio_amlogic_get(struct gpio_chip *chip,unsigned offset)
{
	unsigned int reg,bit;
	gpio_print("==%s==%d\n",__FUNCTION__,__LINE__);
	reg=GPIO_REG(amlogic_pins[offset].input_value_reg_bit);
	bit=GPIO_BIT(amlogic_pins[offset].input_value_reg_bit);
	return aml_get_reg32_bits(p_gpio_input_addr[reg],bit,1);
}

int gpio_amlogic_direction_output(struct gpio_chip *chip,unsigned offset, int value)
{
	unsigned int reg,bit;
	if(offset==GPIO_BSD_EN){
		aml_clr_reg32_mask(P_PREG_PAD_GPIO0_O,1<<29);
#ifndef CONFIG_MESON_TRUSTZONE
		aml_set_reg32_mask(P_AO_SECURE_REG0,1<<0);
#else
		meson_secure_reg_write(P_AO_SECURE_REG0, meson_secure_reg_read(P_AO_SECURE_REG0) | (1<<0));
#endif
		if(value)
			aml_set_reg32_mask(P_PREG_PAD_GPIO0_O,1<<31);//out put high
		else
			aml_clr_reg32_mask(P_PREG_PAD_GPIO0_O,1<<31);//out put low
		aml_clr_reg32_mask(P_PREG_PAD_GPIO0_O,1<<30);//out put enable
		return 0;
	}
	if(offset==GPIO_TEST_N){
		if(value)
			aml_set_reg32_mask(P_AO_GPIO_O_EN_N,1<<31);//out put high
		else
			aml_clr_reg32_mask(P_AO_GPIO_O_EN_N,1<<31);//out put low
#ifndef CONFIG_MESON_TRUSTZONE
		aml_set_reg32_mask(P_AO_SECURE_REG0,1);// out put enable
#else
		meson_secure_reg_write(P_AO_SECURE_REG0, meson_secure_reg_read(P_AO_SECURE_REG0) | (1<<0));
#endif
		return 0;
	}
	if(value){
		reg=GPIO_REG(amlogic_pins[offset].out_value_reg_bit);
		bit=GPIO_BIT(amlogic_pins[offset].out_value_reg_bit);
		aml_set_reg32_mask(p_gpio_output_addr[reg],1<<bit);
		gpio_print("out reg=%x,value=%x\n",p_gpio_output_addr[reg],aml_read_reg32(p_gpio_output_addr[reg]));
	}
	else{
		reg=GPIO_REG(amlogic_pins[offset].out_value_reg_bit);
		bit=GPIO_BIT(amlogic_pins[offset].out_value_reg_bit);
		aml_clr_reg32_mask(p_gpio_output_addr[reg],1<<bit);
		gpio_print("out reg=%x,value=%x\n",p_gpio_output_addr[reg],aml_read_reg32(p_gpio_output_addr[reg]));
	}
	reg=GPIO_REG(amlogic_pins[offset].out_en_reg_bit);
	bit=GPIO_BIT(amlogic_pins[offset].out_en_reg_bit);
	aml_clr_reg32_mask(p_gpio_oen_addr[reg],1<<bit);
	gpio_print("==%s==%d\n",__FUNCTION__,__LINE__);
	gpio_print("oen reg=%x,value=%x\n",p_gpio_oen_addr[reg],aml_read_reg32(p_gpio_oen_addr[reg]));
	gpio_print("value=%d\n",value);
	return 0;
}
void	gpio_amlogic_set(struct gpio_chip *chip,unsigned offset, int value)
{
	unsigned int reg,bit;
	reg=GPIO_REG(amlogic_pins[offset].out_value_reg_bit);
	bit=GPIO_BIT(amlogic_pins[offset].out_value_reg_bit);
	gpio_print("==%s==%d\n",__FUNCTION__,__LINE__);
	if(value)
		aml_set_reg32_mask(p_gpio_output_addr[reg],1<<bit);
	else
		aml_clr_reg32_mask(p_gpio_output_addr[reg],1<<bit);
}
int gpio_amlogic_name_to_num(const char *name)
{
	int i,tmp=100,num=0;
	int len=0;
	char *p=NULL;
	char *start=NULL;
	if(!name)
		return -1;
	if(!strcmp(name,"GPIO_BSD_EN"))
		return GPIO_BSD_EN;
	if(!strcmp(name,"GPIO_TEST_N"))
		return GPIO_TEST_N;
	if (sscanf(name, "DIF_TTL_%d_P", &num))
		return DIF_TTL_0_P+num*2;
	if (sscanf(name, "DIF_TTL_%d_N", &num))
		return DIF_TTL_0_N+num*2;
	if (sscanf(name, "HDMI_TTL_%d_P", &num))
		return HDMI_TTL_0_P+num*2;
	if (sscanf(name, "HDMI_TTL_%d_N", &num))
		return HDMI_TTL_0_N+num*2;
	if(!strcmp(name,"HDMI_TTL_CK_P"))
		return HDMI_TTL_CK_P;
	if(!strcmp(name,"HDMI_TTL_CK_N"))
		return HDMI_TTL_CK_N;
	len=strlen(name);
	p=kzalloc(len+1,GFP_KERNEL);
	start=p;
	if(!p)
	{
		printk("%s:malloc error\n",__func__);
		return -1;
	}
	p=strcpy(p,name);
	for(i=0;i<len;p++,i++){		
		if(*p=='_'){
			*p='\0';
			tmp=i;
		}
		if(i>tmp&&*p>='0'&&*p<='9')
			num=num*10+*p-'0';
	}
	p=start;
	if(!strcmp(p,"GPIOAO"))
		num=num+0;
	else if(!strcmp(p,"GPIOH"))
		num=num+14;
	else if(!strcmp(p,"BOOT"))
		num=num+24;
	else if(!strcmp(p,"CARD"))
		num=num+43;
	else if(!strcmp(p,"GPIODV"))
		num=num+50;
	else if(!strcmp(p,"GPIOY"))
		num=num+80;
	else if(!strcmp(p,"GPIOX"))
		num=num+97;
	else
		num= -1;	
	kzfree(start);
	return num;
}

static struct gpio_chip amlogic_gpio_chip={
	.request=gpio_amlogic_requst,
	.free=gpio_amlogic_free,
	.direction_input=gpio_amlogic_direction_input,
	.get=gpio_amlogic_get,
	.direction_output=gpio_amlogic_direction_output,
	.set=gpio_amlogic_set,
	.to_irq=gpio_amlogic_to_irq,
};


static const struct of_device_id amlogic_gpio_match[] = 
{
	{
	.compatible = "amlogic,m8b-gpio",
	},
	{ },
};
struct amlogic_gpio_platform_data
{
	unsigned int base;
	unsigned ngpios;
	struct device_node	*of_node; /* associated device tree node */
};

static int m8b_set_pullup(unsigned int pin,unsigned int val,unsigned int pullen)
{
	unsigned int reg=0,bit=0,bit_en=0,ret;
	ret=m8b_pin_to_pullup(pin,&reg,&bit,&bit_en);
	if(!ret)
	{
		if(pullen){
			if(!ret)
			{
				if(val)
					aml_set_reg32_mask(p_pull_up_addr[reg],1<<bit);
				else
					aml_clr_reg32_mask(p_pull_up_addr[reg],1<<bit);
			}
			aml_set_reg32_mask(p_pull_upen_addr[reg],1<<bit_en);
		}
		else
			aml_clr_reg32_mask(p_pull_upen_addr[reg],1<<bit_en);
	}
	return ret;
}

//#define gpio_dump
//#define pull_dump
//#define dire_dump
static int amlogic_gpio_probe(struct platform_device *pdev)
{
#ifdef CONFIG_OF_GPIO
		amlogic_gpio_chip.of_node = pdev->dev.of_node;
#endif

	amlogic_gpio_chip.base=0;
	amlogic_gpio_chip.ngpio=ARRAY_SIZE(amlogic_pins);
	gpiochip_add(&amlogic_gpio_chip);
	pullup_ops.meson_set_pullup=m8b_set_pullup;
	dev_info(&pdev->dev, "Probed amlogic GPIO driver\n");
#ifdef gpio_dump
	int gi;
	for(gi=0;gi<GPIO_MAX;gi++)
		printk("%s,amlogic_pins[%d]=%d,%d,out en reg=%x,bit=%d,out val reg=%x,bit=%d,input reg=%x,bit=%d\n",
		amlogic_pins[gi].name,gi,amlogic_pins[gi].num,
		gpio_amlogic_name_to_num(amlogic_pins[gi].name),
		(p_gpio_oen_addr[GPIO_REG(amlogic_pins[gi].out_en_reg_bit)]&0xffff)>>2,
		GPIO_BIT(amlogic_pins[gi].out_en_reg_bit),
		(p_gpio_output_addr[GPIO_REG(amlogic_pins[gi].out_value_reg_bit)]&0xffff)>>2,
		GPIO_BIT(amlogic_pins[gi].out_value_reg_bit),
		(p_gpio_input_addr[GPIO_REG(amlogic_pins[gi].input_value_reg_bit)]&0xffff)>>2,
		GPIO_BIT(amlogic_pins[gi].input_value_reg_bit)
	);
#endif
#ifdef irq_dump

	for(i=GPIO_IRQ0;i<GPIO_IRQ7+1;i++){
		gpio_flag=AML_GPIO_IRQ(i,FILTER_NUM7,GPIO_IRQ_HIGH);
		gpio_amlogic_to_irq(NULL,50);
	}
	for(i=GPIO_IRQ_HIGH;i<GPIO_IRQ_FALLING+1;i++){
		gpio_flag=AML_GPIO_IRQ(GPIO_IRQ0,FILTER_NUM7,i);
		gpio_amlogic_to_irq(NULL,50);
	}
	
#endif
#ifdef pull_dump

	int preg,pbit,penbit,pi;
	for(pi=0;pi<GPIO_TEST_N;pi++){
		m8b_pin_to_pullup(pi,&preg,&pbit,&penbit);
		printk("%s \t,pull up en reg:%x \t,enbit:%d \t,==,pull up reg:%x \t,bit:%d \t\n",
			amlogic_pins[pi].name,
			(p_pull_upen_addr[preg]&0xffff)>>2,
			penbit,
			(p_pull_up_addr[preg]&0xffff)>>2,
			pbit);
	}
#endif
#ifdef dire_dump
extern int m8b_pin_map_to_direction(unsigned int pin,unsigned int *reg,unsigned int *bit);
	int dreg,dbit,di;
	for(di=0;di<GPIO_TEST_N;di++){
		m8b_pin_map_to_direction(di,&dreg,&dbit);
		printk("%s \t,output en reg:%x \t,enbit:%d \t\n",
			amlogic_pins[di].name,
			(p_gpio_oen_addr[dreg]&0xffff)>>2,
			dbit);
	}
#endif
	return 0;
}



static struct platform_driver amlogic_gpio_driver = {
	.probe		= amlogic_gpio_probe,
	.driver		= {
		.name	= "amlogic_gpio",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(amlogic_gpio_match),
	},
};

/*
 * gpio driver register needs to be done before
 * machine_init functions access gpio APIs.
 * Hence amlogic_gpio_drv_reg() is a postcore_initcall.
 */
static int __init amlogic_gpio_drv_reg(void)
{
	return platform_driver_register(&amlogic_gpio_driver);
}
postcore_initcall(amlogic_gpio_drv_reg);
