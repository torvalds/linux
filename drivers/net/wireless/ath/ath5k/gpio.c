/*
 * Copyright (c) 2004-2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2008 Nick Kossifidis <mickflemm@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/****************\
  GPIO Functions
\****************/

#include "ath5k.h"
#include "reg.h"
#include "debug.h"


/**
 * DOC: GPIO/LED functions
 *
 * Here we control the 6 bidirectional GPIO pins provided by the hw.
 * We can set a GPIO pin to be an input or an output pin on GPIO control
 * register and then read or set its status from GPIO data input/output
 * registers.
 *
 * We also control the two LED pins provided by the hw, LED_0 is our
 * "power" LED and LED_1 is our "network activity" LED but many scenarios
 * are available from hw. Vendors might also provide LEDs connected to the
 * GPIO pins, we handle them through the LED subsystem on led.c
 */


/**
 * ath5k_hw_set_ledstate() - Set led state
 * @ah: The &struct ath5k_hw
 * @state: One of AR5K_LED_*
 *
 * Used to set the LED blinking state. This only
 * works for the LED connected to the LED_0, LED_1 pins,
 * not the GPIO based.
 */
void
ath5k_hw_set_ledstate(struct ath5k_hw *ah, unsigned int state)
{
	u32 led;
	/*5210 has different led mode handling*/
	u32 led_5210;

	/*Reset led status*/
	if (ah->ah_version != AR5K_AR5210)
		AR5K_REG_DISABLE_BITS(ah, AR5K_PCICFG,
			AR5K_PCICFG_LEDMODE |  AR5K_PCICFG_LED);
	else
		AR5K_REG_DISABLE_BITS(ah, AR5K_PCICFG, AR5K_PCICFG_LED);

	/*
	 * Some blinking values, define at your wish
	 */
	switch (state) {
	case AR5K_LED_SCAN:
	case AR5K_LED_AUTH:
		led = AR5K_PCICFG_LEDMODE_PROP | AR5K_PCICFG_LED_PEND;
		led_5210 = AR5K_PCICFG_LED_PEND | AR5K_PCICFG_LED_BCTL;
		break;

	case AR5K_LED_INIT:
		led = AR5K_PCICFG_LEDMODE_PROP | AR5K_PCICFG_LED_NONE;
		led_5210 = AR5K_PCICFG_LED_PEND;
		break;

	case AR5K_LED_ASSOC:
	case AR5K_LED_RUN:
		led = AR5K_PCICFG_LEDMODE_PROP | AR5K_PCICFG_LED_ASSOC;
		led_5210 = AR5K_PCICFG_LED_ASSOC;
		break;

	default:
		led = AR5K_PCICFG_LEDMODE_PROM | AR5K_PCICFG_LED_NONE;
		led_5210 = AR5K_PCICFG_LED_PEND;
		break;
	}

	/*Write new status to the register*/
	if (ah->ah_version != AR5K_AR5210)
		AR5K_REG_ENABLE_BITS(ah, AR5K_PCICFG, led);
	else
		AR5K_REG_ENABLE_BITS(ah, AR5K_PCICFG, led_5210);
}

/**
 * ath5k_hw_set_gpio_input() - Set GPIO inputs
 * @ah: The &struct ath5k_hw
 * @gpio: GPIO pin to set as input
 */
int
ath5k_hw_set_gpio_input(struct ath5k_hw *ah, u32 gpio)
{
	if (gpio >= AR5K_NUM_GPIO)
		return -EINVAL;

	ath5k_hw_reg_write(ah,
		(ath5k_hw_reg_read(ah, AR5K_GPIOCR) & ~AR5K_GPIOCR_OUT(gpio))
		| AR5K_GPIOCR_IN(gpio), AR5K_GPIOCR);

	return 0;
}

/**
 * ath5k_hw_set_gpio_output() - Set GPIO outputs
 * @ah: The &struct ath5k_hw
 * @gpio: The GPIO pin to set as output
 */
int
ath5k_hw_set_gpio_output(struct ath5k_hw *ah, u32 gpio)
{
	if (gpio >= AR5K_NUM_GPIO)
		return -EINVAL;

	ath5k_hw_reg_write(ah,
		(ath5k_hw_reg_read(ah, AR5K_GPIOCR) & ~AR5K_GPIOCR_OUT(gpio))
		| AR5K_GPIOCR_OUT(gpio), AR5K_GPIOCR);

	return 0;
}

/**
 * ath5k_hw_get_gpio() - Get GPIO state
 * @ah: The &struct ath5k_hw
 * @gpio: The GPIO pin to read
 */
u32
ath5k_hw_get_gpio(struct ath5k_hw *ah, u32 gpio)
{
	if (gpio >= AR5K_NUM_GPIO)
		return 0xffffffff;

	/* GPIO input magic */
	return ((ath5k_hw_reg_read(ah, AR5K_GPIODI) & AR5K_GPIODI_M) >> gpio) &
		0x1;
}

/**
 * ath5k_hw_set_gpio() - Set GPIO state
 * @ah: The &struct ath5k_hw
 * @gpio: The GPIO pin to set
 * @val: Value to set (boolean)
 */
int
ath5k_hw_set_gpio(struct ath5k_hw *ah, u32 gpio, u32 val)
{
	u32 data;

	if (gpio >= AR5K_NUM_GPIO)
		return -EINVAL;

	/* GPIO output magic */
	data = ath5k_hw_reg_read(ah, AR5K_GPIODO);

	data &= ~(1 << gpio);
	data |= (val & 1) << gpio;

	ath5k_hw_reg_write(ah, data, AR5K_GPIODO);

	return 0;
}

/**
 * ath5k_hw_set_gpio_intr() - Initialize the GPIO interrupt (RFKill switch)
 * @ah: The &struct ath5k_hw
 * @gpio: The GPIO pin to use
 * @interrupt_level: True to generate interrupt on active pin (high)
 *
 * This function is used to set up the GPIO interrupt for the hw RFKill switch.
 * That switch is connected to a GPIO pin and it's number is stored on EEPROM.
 * It can either open or close the circuit to indicate that we should disable
 * RF/Wireless to save power (we also get that from EEPROM).
 */
void
ath5k_hw_set_gpio_intr(struct ath5k_hw *ah, unsigned int gpio,
		u32 interrupt_level)
{
	u32 data;

	if (gpio >= AR5K_NUM_GPIO)
		return;

	/*
	 * Set the GPIO interrupt
	 */
	data = (ath5k_hw_reg_read(ah, AR5K_GPIOCR) &
		~(AR5K_GPIOCR_INT_SEL(gpio) | AR5K_GPIOCR_INT_SELH |
		AR5K_GPIOCR_INT_ENA | AR5K_GPIOCR_OUT(gpio))) |
		(AR5K_GPIOCR_INT_SEL(gpio) | AR5K_GPIOCR_INT_ENA);

	ath5k_hw_reg_write(ah, interrupt_level ? data :
		(data | AR5K_GPIOCR_INT_SELH), AR5K_GPIOCR);

	ah->ah_imr |= AR5K_IMR_GPIO;

	/* Enable GPIO interrupts */
	AR5K_REG_ENABLE_BITS(ah, AR5K_PIMR, AR5K_IMR_GPIO);
}

