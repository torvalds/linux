// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "reg.h"
#include "led.h"

static void _rtl92se_init_led(struct ieee80211_hw *hw,
			      struct rtl_led *pled, enum rtl_led_pin ledpin)
{
	pled->hw = hw;
	pled->ledpin = ledpin;
	pled->ledon = false;
}

void rtl92se_init_sw_leds(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	_rtl92se_init_led(hw, &rtlpriv->ledctl.sw_led0, LED_PIN_LED0);
	_rtl92se_init_led(hw, &rtlpriv->ledctl.sw_led1, LED_PIN_LED1);
}

void rtl92se_sw_led_on(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	u8 ledcfg;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD, "LedAddr:%X ledpin=%d\n",
		 LEDCFG, pled->ledpin);

	ledcfg = rtl_read_byte(rtlpriv, LEDCFG);

	switch (pled->ledpin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		rtl_write_byte(rtlpriv, LEDCFG, ledcfg & 0xf0);
		break;
	case LED_PIN_LED1:
		rtl_write_byte(rtlpriv, LEDCFG, ledcfg & 0x0f);
		break;
	default:
		pr_err("switch case %#x not processed\n",
		       pled->ledpin);
		break;
	}
	pled->ledon = true;
}

void rtl92se_sw_led_off(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	struct rtl_priv *rtlpriv;
	u8 ledcfg;

	rtlpriv = rtl_priv(hw);
	if (!rtlpriv || rtlpriv->max_fw_size)
		return;
	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD, "LedAddr:%X ledpin=%d\n",
		 LEDCFG, pled->ledpin);

	ledcfg = rtl_read_byte(rtlpriv, LEDCFG);

	switch (pled->ledpin) {
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
		pr_err("switch case %#x not processed\n",
		       pled->ledpin);
		break;
	}
	pled->ledon = false;
}

static void _rtl92se_sw_led_control(struct ieee80211_hw *hw,
				    enum led_ctl_mode ledaction)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_led *pled0 = &rtlpriv->ledctl.sw_led0;

	switch (ledaction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		rtl92se_sw_led_on(hw, pled0);
		break;
	case LED_CTL_POWER_OFF:
		rtl92se_sw_led_off(hw, pled0);
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
	RT_TRACE(rtlpriv, COMP_LED, DBG_LOUD, "ledaction %d\n", ledaction);

	_rtl92se_sw_led_control(hw, ledaction);
}

