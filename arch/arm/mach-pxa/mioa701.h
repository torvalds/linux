/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MIOA701_H_
#define _MIOA701_H_

#define MIO_CFG_IN(pin, af)		\
	((MFP_CFG_DEFAULT & ~(MFP_AF_MASK | MFP_DIR_MASK)) |\
	 (MFP_PIN(pin) | MFP_##af | MFP_DIR_IN))

#define MIO_CFG_OUT(pin, af, state)	\
	((MFP_CFG_DEFAULT & ~(MFP_AF_MASK | MFP_DIR_MASK | MFP_LPM_STATE_MASK)) |\
	 (MFP_PIN(pin) | MFP_##af | MFP_DIR_OUT | MFP_LPM_##state))

/* Global GPIOs */
#define GPIO9_CHARGE_EN				9
#define GPIO18_POWEROFF				18
#define GPIO87_LCD_POWER			87
#define GPIO96_AC_DETECT			96
#define GPIO80_MAYBE_CHARGE_VDROP		80	/* Drop of 88mV */

/* USB */
#define GPIO13_nUSB_DETECT			13
#define GPIO22_USB_ENABLE			22

/* SDIO bits */
#define GPIO78_SDIO_RO				78
#define GPIO15_SDIO_INSERT			15
#define GPIO91_SDIO_EN				91

/* Bluetooth */
#define GPIO14_BT_nACTIVITY			14
#define GPIO83_BT_ON				83
#define GPIO77_BT_UNKNOWN1			77
#define GPIO86_BT_MAYBE_nRESET			86

/* GPS */
#define GPIO23_GPS_UNKNOWN1			23
#define GPIO26_GPS_ON				26
#define GPIO27_GPS_RESET			27
#define GPIO106_GPS_UNKNOWN2			106
#define GPIO107_GPS_UNKNOWN3			107

/* GSM */
#define GPIO24_GSM_MOD_RESET_CMD		24
#define GPIO88_GSM_nMOD_ON_CMD			88
#define GPIO90_GSM_nMOD_OFF_CMD			90
#define GPIO114_GSM_nMOD_DTE_UART_STATE 	114
#define GPIO25_GSM_MOD_ON_STATE			25
#define GPIO113_GSM_EVENT			113

/* SOUND */
#define GPIO12_HPJACK_INSERT			12

/* LEDS */
#define GPIO10_LED_nCharging			10
#define GPIO97_LED_nBlue			97
#define GPIO98_LED_nOrange			98
#define GPIO82_LED_nVibra			82
#define GPIO115_LED_nKeyboard			115

/* Keyboard */
#define GPIO0_KEY_POWER				0
#define GPIO93_KEY_VOLUME_UP			93
#define GPIO94_KEY_VOLUME_DOWN			94

/* Camera */
#define GPIO56_MT9M111_nOE			56

extern struct input_dev *mioa701_evdev;
extern void mioa701_gpio_lpm_set(unsigned long mfp_pin);

/* Assembler externals mioa701_bootresume.S */
extern u32 mioa701_bootstrap;
extern u32 mioa701_jumpaddr;
extern u32 mioa701_bootstrap_lg;

#endif /* _MIOA701_H */
