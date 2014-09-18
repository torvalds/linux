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
#include "led.h"
#include "reg.h"

static void _rtl92ee_init_led(struct ieee80211_hw *hw,
			      struct rtl_led *pled, enum rtl_led_pin ledpin)
{
	pled->hw = hw;
	pled->ledpin = ledpin;
	pled->b_ledon = false;
}

void rtl92ee_sw_led_on(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	u32 ledcfg;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(COMP_LED, DBG_LOUD,
		 ("LedAddr:%X ledpin =%d\n", REG_LEDCFG2, pled->ledpin));

	switch (pled->ledpin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		ledcfg = rtl_read_dword(rtlpriv, REG_GPIO_PIN_CTRL) | BIT(21);
		ledcfg &= ~BIT(13) & ~BIT(29);
		rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, ledcfg);
		break;
	case LED_PIN_LED1:
		break;
	default:
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("switch case not process\n"));
		break;
	}
	pled->b_ledon = true;
}

void rtl92ee_sw_led_off(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 ledcfg;

	RT_TRACE(COMP_LED, DBG_LOUD,
		 ("LedAddr:%X ledpin =%d\n", REG_LEDCFG2, pled->ledpin));

	switch (pled->ledpin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		ledcfg = rtl_read_dword(rtlpriv , REG_GPIO_PIN_CTRL) | ~BIT(21);
		ledcfg &= ~BIT(29);
		rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, ledcfg);
		break;
	case LED_PIN_LED1:
		break;
	default:
		RT_TRACE(COMP_ERR, DBG_LOUD,
			 ("switch case not process\n"));
		break;
	}
	pled->b_ledon = false;
}

void rtl92ee_init_sw_leds(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	_rtl92ee_init_led(hw, &(pcipriv->ledctl.sw_led0), LED_PIN_LED0);
	_rtl92ee_init_led(hw, &(pcipriv->ledctl.sw_led1), LED_PIN_LED1);
}

static void _rtl92ee_sw_led_control(struct ieee80211_hw *hw,
				    enum led_ctl_mode ledaction)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_led *pLed0 = &(pcipriv->ledctl.sw_led0);
	switch (ledaction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		rtl92ee_sw_led_on(hw, pLed0);
		break;
	case LED_CTL_POWER_OFF:
		rtl92ee_sw_led_off(hw, pLed0);
		break;
	default:
		break;
	}
}

void rtl92ee_led_control(struct ieee80211_hw *hw, enum led_ctl_mode ledaction)
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
	RT_TRACE(COMP_LED, DBG_TRACE, ("ledaction %d,\n", ledaction));
	_rtl92ee_sw_led_control(hw, ledaction);
}
