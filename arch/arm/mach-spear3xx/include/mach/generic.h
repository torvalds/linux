/*
 * arch/arm/mach-spear3xx/generic.h
 *
 * SPEAr3XX machine family generic header file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_GENERIC_H
#define __MACH_GENERIC_H

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <plat/padmux.h>

/* spear3xx declarations */
/*
 * Each GPT has 2 timer channels
 * Following GPT channels will be used as clock source and clockevent
 */
#define SPEAR_GPT0_BASE		SPEAR3XX_ML1_TMR_BASE
#define SPEAR_GPT0_CHAN0_IRQ	IRQ_CPU_GPT1_1
#define SPEAR_GPT0_CHAN1_IRQ	IRQ_CPU_GPT1_2

/* Add spear3xx family device structure declarations here */
extern struct amba_device gpio_device;
extern struct amba_device uart_device;
extern struct sys_timer spear3xx_timer;

/* Add spear3xx family function declarations here */
void __init clk_init(void);
void __init spear_setup_timer(void);
void __init spear3xx_map_io(void);
void __init spear3xx_init_irq(void);
void __init spear3xx_init(void);

/* pad mux declarations */
#define PMX_FIRDA_MASK		(1 << 14)
#define PMX_I2C_MASK		(1 << 13)
#define PMX_SSP_CS_MASK		(1 << 12)
#define PMX_SSP_MASK		(1 << 11)
#define PMX_MII_MASK		(1 << 10)
#define PMX_GPIO_PIN0_MASK	(1 << 9)
#define PMX_GPIO_PIN1_MASK	(1 << 8)
#define PMX_GPIO_PIN2_MASK	(1 << 7)
#define PMX_GPIO_PIN3_MASK	(1 << 6)
#define PMX_GPIO_PIN4_MASK	(1 << 5)
#define PMX_GPIO_PIN5_MASK	(1 << 4)
#define PMX_UART0_MODEM_MASK	(1 << 3)
#define PMX_UART0_MASK		(1 << 2)
#define PMX_TIMER_3_4_MASK	(1 << 1)
#define PMX_TIMER_1_2_MASK	(1 << 0)

/* pad mux devices */
extern struct pmx_dev pmx_firda;
extern struct pmx_dev pmx_i2c;
extern struct pmx_dev pmx_ssp_cs;
extern struct pmx_dev pmx_ssp;
extern struct pmx_dev pmx_mii;
extern struct pmx_dev pmx_gpio_pin0;
extern struct pmx_dev pmx_gpio_pin1;
extern struct pmx_dev pmx_gpio_pin2;
extern struct pmx_dev pmx_gpio_pin3;
extern struct pmx_dev pmx_gpio_pin4;
extern struct pmx_dev pmx_gpio_pin5;
extern struct pmx_dev pmx_uart0_modem;
extern struct pmx_dev pmx_uart0;
extern struct pmx_dev pmx_timer_3_4;
extern struct pmx_dev pmx_timer_1_2;

#if defined(CONFIG_MACH_SPEAR310) || defined(CONFIG_MACH_SPEAR320)
/* padmux plgpio devices */
extern struct pmx_dev pmx_plgpio_0_1;
extern struct pmx_dev pmx_plgpio_2_3;
extern struct pmx_dev pmx_plgpio_4_5;
extern struct pmx_dev pmx_plgpio_6_9;
extern struct pmx_dev pmx_plgpio_10_27;
extern struct pmx_dev pmx_plgpio_28;
extern struct pmx_dev pmx_plgpio_29;
extern struct pmx_dev pmx_plgpio_30;
extern struct pmx_dev pmx_plgpio_31;
extern struct pmx_dev pmx_plgpio_32;
extern struct pmx_dev pmx_plgpio_33;
extern struct pmx_dev pmx_plgpio_34_36;
extern struct pmx_dev pmx_plgpio_37_42;
extern struct pmx_dev pmx_plgpio_43_44_47_48;
extern struct pmx_dev pmx_plgpio_45_46_49_50;
#endif

extern struct pmx_driver pmx_driver;

/* spear300 declarations */
#ifdef CONFIG_MACH_SPEAR300
/* Add spear300 machine device structure declarations here */
extern struct amba_device gpio1_device;

/* pad mux modes */
extern struct pmx_mode nand_mode;
extern struct pmx_mode nor_mode;
extern struct pmx_mode photo_frame_mode;
extern struct pmx_mode lend_ip_phone_mode;
extern struct pmx_mode hend_ip_phone_mode;
extern struct pmx_mode lend_wifi_phone_mode;
extern struct pmx_mode hend_wifi_phone_mode;
extern struct pmx_mode ata_pabx_wi2s_mode;
extern struct pmx_mode ata_pabx_i2s_mode;
extern struct pmx_mode caml_lcdw_mode;
extern struct pmx_mode camu_lcd_mode;
extern struct pmx_mode camu_wlcd_mode;
extern struct pmx_mode caml_lcd_mode;

/* pad mux devices */
extern struct pmx_dev pmx_fsmc_2_chips;
extern struct pmx_dev pmx_fsmc_4_chips;
extern struct pmx_dev pmx_keyboard;
extern struct pmx_dev pmx_clcd;
extern struct pmx_dev pmx_telecom_gpio;
extern struct pmx_dev pmx_telecom_tdm;
extern struct pmx_dev pmx_telecom_spi_cs_i2c_clk;
extern struct pmx_dev pmx_telecom_camera;
extern struct pmx_dev pmx_telecom_dac;
extern struct pmx_dev pmx_telecom_i2s;
extern struct pmx_dev pmx_telecom_boot_pins;
extern struct pmx_dev pmx_telecom_sdhci_4bit;
extern struct pmx_dev pmx_telecom_sdhci_8bit;
extern struct pmx_dev pmx_gpio1;

/* Add spear300 machine function declarations here */
void __init spear300_init(void);

#endif /* CONFIG_MACH_SPEAR300 */

/* spear310 declarations */
#ifdef CONFIG_MACH_SPEAR310
/* Add spear310 machine device structure declarations here */

/* pad mux devices */
extern struct pmx_dev pmx_emi_cs_0_1_4_5;
extern struct pmx_dev pmx_emi_cs_2_3;
extern struct pmx_dev pmx_uart1;
extern struct pmx_dev pmx_uart2;
extern struct pmx_dev pmx_uart3_4_5;
extern struct pmx_dev pmx_fsmc;
extern struct pmx_dev pmx_rs485_0_1;
extern struct pmx_dev pmx_tdm0;

/* Add spear310 machine function declarations here */
void __init spear310_init(void);

#endif /* CONFIG_MACH_SPEAR310 */

/* spear320 declarations */
#ifdef CONFIG_MACH_SPEAR320
/* Add spear320 machine device structure declarations here */

/* pad mux modes */
extern struct pmx_mode auto_net_smii_mode;
extern struct pmx_mode auto_net_mii_mode;
extern struct pmx_mode auto_exp_mode;
extern struct pmx_mode small_printers_mode;

/* pad mux devices */
extern struct pmx_dev pmx_clcd;
extern struct pmx_dev pmx_emi;
extern struct pmx_dev pmx_fsmc;
extern struct pmx_dev pmx_spp;
extern struct pmx_dev pmx_sdhci;
extern struct pmx_dev pmx_i2s;
extern struct pmx_dev pmx_uart1;
extern struct pmx_dev pmx_uart1_modem;
extern struct pmx_dev pmx_uart2;
extern struct pmx_dev pmx_touchscreen;
extern struct pmx_dev pmx_can;
extern struct pmx_dev pmx_sdhci_led;
extern struct pmx_dev pmx_pwm0;
extern struct pmx_dev pmx_pwm1;
extern struct pmx_dev pmx_pwm2;
extern struct pmx_dev pmx_pwm3;
extern struct pmx_dev pmx_ssp1;
extern struct pmx_dev pmx_ssp2;
extern struct pmx_dev pmx_mii1;
extern struct pmx_dev pmx_smii0;
extern struct pmx_dev pmx_smii1;
extern struct pmx_dev pmx_i2c1;

/* Add spear320 machine function declarations here */
void __init spear320_init(void);

#endif /* CONFIG_MACH_SPEAR320 */

#endif /* __MACH_GENERIC_H */
