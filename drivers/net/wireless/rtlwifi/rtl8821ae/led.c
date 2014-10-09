/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../pci.h"
#include "reg.h"
#include "led.h"

static void _rtl8821ae_init_led(struct ieee80211_hw *hw,
				struct rtl_led *pled,
				enum rtl_led_pin ledpin)
{
	pled->hw = hw;
	pled->ledpin = ledpin;
	pled->ledon = false;
}

void rtl8821ae_sw_led_on(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	u8 ledcfg;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD,
		 "LedAddr:%X ledpin=%d\n", REG_LEDCFG2, pled->ledpin);

	switch (pled->ledpin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		ledcfg = rtl_read_byte(rtlpriv, REG_LEDCFG2);
		ledcfg &= ~BIT(6);
		rtl_write_byte(rtlpriv,
			       REG_LEDCFG2, (ledcfg & 0xf0) | BIT(5));
		break;
	case LED_PIN_LED1:
		ledcfg = rtl_read_byte(rtlpriv, REG_LEDCFG1);
		rtl_write_byte(rtlpriv, REG_LEDCFG1, ledcfg & 0x10);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case not process\n");
		break;
	}
	pled->ledon = true;
}

void rtl8812ae_sw_led_on(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	u16	ledreg = REG_LEDCFG1;
	u8	ledcfg = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (pled->ledpin) {
	case LED_PIN_LED0:
		ledreg = REG_LEDCFG1;
		break;

	case LED_PIN_LED1:
		ledreg = REG_LEDCFG2;
		break;

	case LED_PIN_GPIO0:
	default:
		break;
	}

	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD,
		 "In SwLedOn, LedAddr:%X LEDPIN=%d\n",
		 ledreg, pled->ledpin);

	ledcfg =  rtl_read_byte(rtlpriv, ledreg);
	ledcfg |= BIT(5); /*Set 0x4c[21]*/
	ledcfg &= ~(BIT(7) | BIT(6) | BIT(3) | BIT(2) | BIT(1) | BIT(0));
		/*Clear 0x4c[23:22] and 0x4c[19:16]*/
	rtl_write_byte(rtlpriv, ledreg, ledcfg); /*SW control led0 on.*/
	pled->ledon = true;
}

void rtl8821ae_sw_led_off(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	u8 ledcfg;

	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD,
		 "LedAddr:%X ledpin=%d\n", REG_LEDCFG2, pled->ledpin);

	ledcfg = rtl_read_byte(rtlpriv, REG_LEDCFG2);

	switch (pled->ledpin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		ledcfg &= 0xf0;
		if (pcipriv->ledctl.led_opendrain) {
			ledcfg &= 0x90; /* Set to software control. */
			rtl_write_byte(rtlpriv, REG_LEDCFG2, (ledcfg|BIT(3)));
			ledcfg = rtl_read_byte(rtlpriv, REG_MAC_PINMUX_CFG);
			ledcfg &= 0xFE;
			rtl_write_byte(rtlpriv, REG_MAC_PINMUX_CFG, ledcfg);
		} else {
			ledcfg &= ~BIT(6);
			rtl_write_byte(rtlpriv, REG_LEDCFG2,
				       (ledcfg | BIT(3) | BIT(5)));
		}
		break;
	case LED_PIN_LED1:
		ledcfg = rtl_read_byte(rtlpriv, REG_LEDCFG1);
		ledcfg &= 0x10; /* Set to software control. */
		rtl_write_byte(rtlpriv, REG_LEDCFG1, ledcfg|BIT(3));
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case not process\n");
		break;
	}
	pled->ledon = false;
}

void rtl8812ae_sw_led_off(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	u16 ledreg = REG_LEDCFG1;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);

	switch (pled->ledpin) {
	case LED_PIN_LED0:
		ledreg = REG_LEDCFG1;
		break;

	case LED_PIN_LED1:
		ledreg = REG_LEDCFG2;
		break;

	case LED_PIN_GPIO0:
	default:
		break;
	}

	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD,
		 "In SwLedOff,LedAddr:%X LEDPIN=%d\n",
		 ledreg, pled->ledpin);
	/*Open-drain arrangement for controlling the LED*/
	if (pcipriv->ledctl.led_opendrain) {
		u8 ledcfg = rtl_read_byte(rtlpriv, ledreg);

		ledreg &= 0xd0; /* Set to software control.*/
		rtl_write_byte(rtlpriv, ledreg, (ledcfg | BIT(3)));

		/*Open-drain arrangement*/
		ledcfg = rtl_read_byte(rtlpriv, REG_MAC_PINMUX_CFG);
		ledcfg &= 0xFE;/*Set GPIO[8] to input mode*/
		rtl_write_byte(rtlpriv, REG_MAC_PINMUX_CFG, ledcfg);
	} else {
		rtl_write_byte(rtlpriv, ledreg, 0x28);
	}

	pled->ledon = false;
}

void rtl8821ae_init_sw_leds(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);

	_rtl8821ae_init_led(hw, &pcipriv->ledctl.sw_led0, LED_PIN_LED0);
	_rtl8821ae_init_led(hw, &pcipriv->ledctl.sw_led1, LED_PIN_LED1);
}

static void _rtl8821ae_sw_led_control(struct ieee80211_hw *hw,
				      enum led_ctl_mode ledaction)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_led *pLed0 = &pcipriv->ledctl.sw_led0;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	switch (ledaction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8812ae_sw_led_on(hw, pLed0);
		else
			rtl8821ae_sw_led_on(hw, pLed0);
		break;
	case LED_CTL_POWER_OFF:
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8812ae_sw_led_off(hw, pLed0);
		else
			rtl8821ae_sw_led_off(hw, pLed0);
		break;
	default:
		break;
	}
}

void rtl8821ae_led_control(struct ieee80211_hw *hw,
			   enum led_ctl_mode ledaction)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	if ((ppsc->rfoff_reason > RF_CHANGE_BY_PS) &&
	    (ledaction == LED_CTL_TX ||
	     ledaction == LED_CTL_RX ||
	     ledaction == LED_CTL_SITE_SURVEY ||
	     ledaction == LED_CTL_LINK ||
	     ledaction == LED_CTL_NO_LINK ||
	     ledaction == LED_CTL_START_TO_LINK ||
	     ledaction == LED_CTL_POWER_ON)) {
		return;
	}
	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD, "ledaction %d,\n",
		 ledaction);
	_rtl8821ae_sw_led_control(hw, ledaction);
}
