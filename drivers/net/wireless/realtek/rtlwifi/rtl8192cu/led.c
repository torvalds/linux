// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../usb.h"
#include "reg.h"
#include "led.h"

static void _rtl92cu_init_led(struct ieee80211_hw *hw,
			      struct rtl_led *pled, enum rtl_led_pin ledpin)
{
	pled->hw = hw;
	pled->ledpin = ledpin;
	pled->ledon = false;
}

static void rtl92cu_deinit_led(struct rtl_led *pled)
{
}

void rtl92cu_sw_led_on(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	u8 ledcfg;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD, "LedAddr:%X ledpin=%d\n",
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
		pr_err("switch case %#x not processed\n",
		       pled->ledpin);
		break;
	}
	pled->ledon = true;
}

void rtl92cu_sw_led_off(struct ieee80211_hw *hw, struct rtl_led *pled)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 ledcfg;

	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD, "LedAddr:%X ledpin=%d\n",
		REG_LEDCFG2, pled->ledpin);
	ledcfg = rtl_read_byte(rtlpriv, REG_LEDCFG2);
	switch (pled->ledpin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		ledcfg &= 0xf0;
		if (rtlpriv->ledctl.led_opendrain)
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
		pr_err("switch case %#x not processed\n",
		       pled->ledpin);
		break;
	}
	pled->ledon = false;
}

void rtl92cu_init_sw_leds(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	_rtl92cu_init_led(hw, &rtlpriv->ledctl.sw_led0, LED_PIN_LED0);
	_rtl92cu_init_led(hw, &rtlpriv->ledctl.sw_led1, LED_PIN_LED1);
}

void rtl92cu_deinit_sw_leds(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl92cu_deinit_led(&rtlpriv->ledctl.sw_led0);
	rtl92cu_deinit_led(&rtlpriv->ledctl.sw_led1);
}

static void _rtl92cu_sw_led_control(struct ieee80211_hw *hw,
				    enum led_ctl_mode ledaction)
{
}

void rtl92cu_led_control(struct ieee80211_hw *hw,
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
	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD, "ledaction %d\n", ledaction);
	_rtl92cu_sw_led_control(hw, ledaction);
}
