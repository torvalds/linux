// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "reg.h"
#include "led.h"

void rtl92se_sw_led_on(struct ieee80211_hw *hw, enum rtl_led_pin pin)
{
	u8 ledcfg;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD, "LedAddr:%X ledpin=%d\n",
		LEDCFG, pin);

	ledcfg = rtl_read_byte(rtlpriv, LEDCFG);

	switch (pin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		rtl_write_byte(rtlpriv, LEDCFG, ledcfg & 0xf0);
		break;
	case LED_PIN_LED1:
		rtl_write_byte(rtlpriv, LEDCFG, ledcfg & 0x0f);
		break;
	default:
		pr_err("switch case %#x not processed\n", pin);
		break;
	}
}

void rtl92se_sw_led_off(struct ieee80211_hw *hw, enum rtl_led_pin pin)
{
	struct rtl_priv *rtlpriv;
	u8 ledcfg;

	rtlpriv = rtl_priv(hw);
	if (!rtlpriv || rtlpriv->max_fw_size)
		return;
	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD, "LedAddr:%X ledpin=%d\n",
		LEDCFG, pin);

	ledcfg = rtl_read_byte(rtlpriv, LEDCFG);

	switch (pin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		ledcfg &= 0xf0;
		if (rtlpriv->ledctl.led_opendrain)
			rtl_write_byte(rtlpriv, LEDCFG, (ledcfg | BIT(1)));
		else
			rtl_write_byte(rtlpriv, LEDCFG, (ledcfg | BIT(3)));
		break;
	case LED_PIN_LED1:
		ledcfg &= 0x0f;
		rtl_write_byte(rtlpriv, LEDCFG, (ledcfg | BIT(3)));
		break;
	default:
		pr_err("switch case %#x not processed\n", pin);
		break;
	}
}

static void _rtl92se_sw_led_control(struct ieee80211_hw *hw,
				    enum led_ctl_mode ledaction)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	enum rtl_led_pin pin0 = rtlpriv->ledctl.sw_led0;

	switch (ledaction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		rtl92se_sw_led_on(hw, pin0);
		break;
	case LED_CTL_POWER_OFF:
		rtl92se_sw_led_off(hw, pin0);
		break;
	default:
		break;
	}
}

void rtl92se_led_control(struct ieee80211_hw *hw, enum led_ctl_mode ledaction)
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
	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD, "ledaction %d\n", ledaction);

	_rtl92se_sw_led_control(hw, ledaction);
}

