// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2014  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "reg.h"
#include "led.h"

void rtl92ee_sw_led_on(struct ieee80211_hw *hw, enum rtl_led_pin pin)
{
	u32 ledcfg;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD,
		"LedAddr:%X ledpin=%d\n", REG_LEDCFG2, pin);

	switch (pin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		ledcfg = rtl_read_dword(rtlpriv , REG_GPIO_PIN_CTRL);
		ledcfg &= ~BIT(13);
		ledcfg |= BIT(21);
		ledcfg &= ~BIT(29);

		rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, ledcfg);

		break;
	case LED_PIN_LED1:

		break;
	default:
		rtl_dbg(rtlpriv, COMP_ERR, DBG_LOUD,
			"switch case %#x not processed\n", pin);
		break;
	}
}

void rtl92ee_sw_led_off(struct ieee80211_hw *hw, enum rtl_led_pin pin)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 ledcfg;

	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD,
		"LedAddr:%X ledpin=%d\n", REG_LEDCFG2, pin);

	switch (pin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:

		ledcfg = rtl_read_dword(rtlpriv , REG_GPIO_PIN_CTRL);
		ledcfg |= ~BIT(21);
		ledcfg &= ~BIT(29);
		rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, ledcfg);

		break;
	case LED_PIN_LED1:

		break;
	default:
		rtl_dbg(rtlpriv, COMP_ERR, DBG_LOUD,
			"switch case %#x not processed\n", pin);
		break;
	}
}

static void _rtl92ee_sw_led_control(struct ieee80211_hw *hw,
				    enum led_ctl_mode ledaction)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	enum rtl_led_pin pin0 = rtlpriv->ledctl.sw_led0;

	switch (ledaction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		rtl92ee_sw_led_on(hw, pin0);
		break;
	case LED_CTL_POWER_OFF:
		rtl92ee_sw_led_off(hw, pin0);
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
	rtl_dbg(rtlpriv, COMP_LED, DBG_TRACE, "ledaction %d,\n", ledaction);
	_rtl92ee_sw_led_control(hw, ledaction);
}
