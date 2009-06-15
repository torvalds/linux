/*
 * GPIO and IRQ definitions for HP iPAQ hx4700
 *
 * Copyright (c) 2008 Philipp Zabel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _HX4700_H_
#define _HX4700_H_

#include <linux/gpio.h>
#include <linux/mfd/asic3.h>

#define HX4700_ASIC3_GPIO_BASE	NR_BUILTIN_GPIO
#define HX4700_EGPIO_BASE	(HX4700_ASIC3_GPIO_BASE + ASIC3_NUM_GPIOS)

/*
 * PXA GPIOs
 */

#define GPIO0_HX4700_nKEY_POWER			0
#define GPIO12_HX4700_ASIC3_IRQ			12
#define GPIO13_HX4700_W3220_IRQ			13
#define GPIO14_HX4700_nWLAN_IRQ			14
#define GPIO18_HX4700_RDY			18
#define GPIO22_HX4700_LCD_RL			22
#define GPIO27_HX4700_CODEC_ON			27
#define GPIO32_HX4700_RS232_ON			32
#define GPIO52_HX4700_CPU_nBATT_FAULT		52
#define GPIO58_HX4700_TSC2046_nPENIRQ		58
#define GPIO59_HX4700_LCD_PC1			59
#define GPIO60_HX4700_CF_RNB			60
#define GPIO61_HX4700_W3220_nRESET		61
#define GPIO62_HX4700_LCD_nRESET		62
#define GPIO63_HX4700_CPU_SS_nRESET		63
#define GPIO65_HX4700_TSC2046_PEN_PU		65
#define GPIO66_HX4700_ASIC3_nSDIO_IRQ		66
#define GPIO67_HX4700_EUART_PS			67
#define GPIO70_HX4700_LCD_SLIN1			70
#define GPIO71_HX4700_ASIC3_nRESET		71
#define GPIO72_HX4700_BQ24022_nCHARGE_EN	72
#define GPIO73_HX4700_LCD_UD_1			73
#define GPIO75_HX4700_EARPHONE_nDET		75
#define GPIO76_HX4700_USBC_PUEN			76
#define GPIO81_HX4700_CPU_GP_nRESET		81
#define GPIO82_HX4700_EUART_RESET		82
#define GPIO83_HX4700_WLAN_nRESET		83
#define GPIO84_HX4700_LCD_SQN			84
#define GPIO85_HX4700_nPCE1			85
#define GPIO88_HX4700_TSC2046_CS		88
#define GPIO91_HX4700_FLASH_VPEN		91
#define GPIO92_HX4700_HP_DRIVER			92
#define GPIO93_HX4700_EUART_INT			93
#define GPIO94_HX4700_KEY_MAIL			94
#define GPIO95_HX4700_BATT_OFF			95
#define GPIO96_HX4700_BQ24022_ISET2		96
#define GPIO97_HX4700_nBL_DETECT		97
#define GPIO99_HX4700_KEY_CONTACTS		99
#define GPIO100_HX4700_AUTO_SENSE		100 /* BL auto brightness */
#define GPIO102_HX4700_SYNAPTICS_POWER_ON	102
#define GPIO103_HX4700_SYNAPTICS_INT		103
#define GPIO105_HX4700_nIR_ON			105
#define GPIO106_HX4700_CPU_BT_nRESET		106
#define GPIO107_HX4700_SPK_nSD			107
#define GPIO109_HX4700_CODEC_nPDN		109
#define GPIO110_HX4700_LCD_LVDD_3V3_ON		110
#define GPIO111_HX4700_LCD_AVDD_3V3_ON		111
#define GPIO112_HX4700_LCD_N2V7_7V3_ON		112
#define GPIO114_HX4700_CF_RESET			114
#define GPIO116_HX4700_CPU_HW_nRESET		116

/*
 * ASIC3 GPIOs
 */

#define GPIOC_BASE		(HX4700_ASIC3_GPIO_BASE + 32)
#define GPIOD_BASE		(HX4700_ASIC3_GPIO_BASE + 48)

#define GPIOC0_LED_RED		(GPIOC_BASE + 0)
#define GPIOC1_LED_GREEN	(GPIOC_BASE + 1)
#define GPIOC2_LED_BLUE		(GPIOC_BASE + 2)
#define GPIOC3_nSD_CS		(GPIOC_BASE + 3)
#define GPIOC4_CF_nCD		(GPIOC_BASE + 4)	/* Input */
#define GPIOC5_nCIOW		(GPIOC_BASE + 5)	/* Output, to CF */
#define GPIOC6_nCIOR		(GPIOC_BASE + 6)	/* Output, to CF */
#define GPIOC7_nPCE1		(GPIOC_BASE + 7)	/* Input, from CPU */
#define GPIOC8_nPCE2		(GPIOC_BASE + 8)	/* Input, from CPU */
#define GPIOC9_nPOE		(GPIOC_BASE + 9)	/* Input, from CPU */
#define GPIOC10_CF_nPWE		(GPIOC_BASE + 10)	/* Input */
#define GPIOC11_PSKTSEL		(GPIOC_BASE + 11)	/* Input, from CPU */
#define GPIOC12_nPREG		(GPIOC_BASE + 12)	/* Input, from CPU */
#define GPIOC13_nPWAIT		(GPIOC_BASE + 13)	/* Output, to CPU */
#define GPIOC14_nPIOIS16	(GPIOC_BASE + 14)	/* Output, to CPU */
#define GPIOC15_nPIOR		(GPIOC_BASE + 15)	/* Input, from CPU */

#define GPIOD0_CPU_SS_INT	(GPIOD_BASE + 0)	/* Input */
#define GPIOD1_nKEY_CALENDAR	(GPIOD_BASE + 1)
#define GPIOD2_BLUETOOTH_WAKEUP	(GPIOD_BASE + 2)
#define GPIOD3_nKEY_HOME	(GPIOD_BASE + 3)
#define GPIOD4_CF_nCD		(GPIOD_BASE + 4)	/* Input, from CF */
#define GPIOD5_nPIO		(GPIOD_BASE + 5)	/* Input */
#define GPIOD6_nKEY_RECORD	(GPIOD_BASE + 6)
#define GPIOD7_nSDIO_DETECT	(GPIOD_BASE + 7)
#define GPIOD8_COM_DCD		(GPIOD_BASE + 8)	/* Input */
#define GPIOD9_nAC_IN		(GPIOD_BASE + 9)
#define GPIOD10_nSDIO_IRQ	(GPIOD_BASE + 10)	/* Input */
#define GPIOD11_nCIOIS16	(GPIOD_BASE + 11)	/* Input, from CF */
#define GPIOD12_nCWAIT		(GPIOD_BASE + 12)	/* Input, from CF */
#define GPIOD13_CF_RNB		(GPIOD_BASE + 13)	/* Input */
#define GPIOD14_nUSBC_DETECT	(GPIOD_BASE + 14)
#define GPIOD15_nPIOW		(GPIOD_BASE + 15)	/* Input, from CPU */

/*
 * EGPIOs
 */

#define EGPIO0_VCC_3V3_EN	(HX4700_EGPIO_BASE + 0)	/* WLAN support chip */
#define EGPIO1_WL_VREG_EN	(HX4700_EGPIO_BASE + 1)	/* WLAN power */
#define EGPIO2_VCC_2V1_WL_EN	(HX4700_EGPIO_BASE + 2)	/* unused */
#define EGPIO3_SS_PWR_ON	(HX4700_EGPIO_BASE + 3)	/* smart slot power */
#define EGPIO4_CF_3V3_ON	(HX4700_EGPIO_BASE + 4)	/* CF 3.3V enable */
#define EGPIO5_BT_3V3_ON	(HX4700_EGPIO_BASE + 5)	/* BT 3.3V enable */
#define EGPIO6_WL1V8_EN		(HX4700_EGPIO_BASE + 6)	/* WLAN 1.8V enable */
#define EGPIO7_VCC_3V3_WL_EN	(HX4700_EGPIO_BASE + 7)	/* WLAN 3.3V enable */
#define EGPIO8_USB_3V3_ON	(HX4700_EGPIO_BASE + 8)	/* unused */

#endif /* _HX4700_H_ */
