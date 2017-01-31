/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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

static void _rtl92ce_init_led(struct ieee80211_hw *hw,
			      struct rtl_led *pled, enum rtl_led_pin ledpin)
{
	pled->hw = hw;
	pled->ledpin = ledpin;
	pled->ledon = false;
}

void rtl92ce_sw_led_on(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	u8 ledcfg;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD, "LedAddr:%X ledpin=%d\n",
		 REG_LEDCFG2, pled->ledpin);

	ledcfg = rtl_read_byte(rtlpriv, REG_LEDCFG2);

	switch (pled->ledpin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		rtl_write_byte(rtlpriv,
			       REG_LEDCFG2, (ledcfg & 0xf0) | BIT(5) | BIT(6));
		break;
	case LED_PIN_LED1:
		rtl_write_byte(rtlpriv, REG_LEDCFG2, (ledcfg & 0x0f) | BIT(5));
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case %#x not processed\n", pled->ledpin);
		break;
	}
	pled->ledon = true;
}

void rtl92ce_sw_led_off(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	u8 ledcfg;

	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD, "LedAddr:%X ledpin=%d\n",
		 REG_LEDCFG2, pled->ledpin);

	ledcfg = rtl_read_byte(rtlpriv, REG_LEDCFG2);

	switch (pled->ledpin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		ledcfg &= 0xf0;
		if (pcipriv->ledctl.led_opendrain)
			rtl_write_byte(rtlpriv, REG_LEDCFG2,
				       (ledcfg | BIT(1) | BIT(5) | BIT(6)));
		else
			rtl_write_byte(rtlpriv, REG_LEDCFG2,
				       (ledcfg | BIT(3) | BIT(5) | BIT(6)));
		break;
	case LED_PIN_LED1:
		ledcfg &= 0x0f;
		rtl_write_byte(rtlpriv, REG_LEDCFG2, (ledcfg | BIT(3)));
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case %#x not processed\n", pled->ledpin);
		break;
	}
	pled->ledon = false;
}

void rtl92ce_init_sw_leds(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	_rtl92ce_init_led(hw, &(pcipriv->ledctl.sw_led0), LED_PIN_LED0);
	_rtl92ce_init_led(hw, &(pcipriv->ledctl.sw_led1), LED_PIN_LED1);
}

static void _rtl92ce_sw_led_control(struct ieee80211_hw *hw,
				    enum led_ctl_mode ledaction)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_led *pLed0 = &(pcipriv->ledctl.sw_led0);
	switch (ledaction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		rtl92ce_sw_led_on(hw, pLed0);
		break;
	case LED_CTL_POWER_OFF:
		rtl92ce_sw_led_off(hw, pLed0);
		break;
	default:
		break;
	}
}

void rtl92ce_led_control(struct ieee80211_hw *hw,
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
	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD, "ledaction %d\n",
		 ledaction);
	_rtl92ce_sw_led_control(hw, ledaction);
}
