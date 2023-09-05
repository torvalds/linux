// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2010  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "reg.h"
#include "led.h"

void rtl8821ae_sw_led_on(struct ieee80211_hw *hw, enum rtl_led_pin pin)
{
	u8 ledcfg;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD,
		"LedAddr:%X ledpin=%d\n", REG_LEDCFG2, pin);

	switch (pin) {
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
		rtl_dbg(rtlpriv, COMP_ERR, DBG_LOUD,
			"switch case %#x not processed\n", pin);
		break;
	}
}

void rtl8812ae_sw_led_on(struct ieee80211_hw *hw, enum rtl_led_pin pin)
{
	u16	ledreg = REG_LEDCFG1;
	u8	ledcfg = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (pin) {
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

	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD,
		"In SwLedOn, LedAddr:%X LEDPIN=%d\n",
		ledreg, pin);

	ledcfg =  rtl_read_byte(rtlpriv, ledreg);
	ledcfg |= BIT(5); /*Set 0x4c[21]*/
	ledcfg &= ~(BIT(7) | BIT(6) | BIT(3) | BIT(2) | BIT(1) | BIT(0));
		/*Clear 0x4c[23:22] and 0x4c[19:16]*/
	rtl_write_byte(rtlpriv, ledreg, ledcfg); /*SW control led0 on.*/
}

void rtl8821ae_sw_led_off(struct ieee80211_hw *hw, enum rtl_led_pin pin)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 ledcfg;

	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD,
		"LedAddr:%X ledpin=%d\n", REG_LEDCFG2, pin);

	ledcfg = rtl_read_byte(rtlpriv, REG_LEDCFG2);

	switch (pin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		ledcfg &= 0xf0;
		if (rtlpriv->ledctl.led_opendrain) {
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
		rtl_dbg(rtlpriv, COMP_ERR, DBG_LOUD,
			"switch case %#x not processed\n", pin);
		break;
	}
}

void rtl8812ae_sw_led_off(struct ieee80211_hw *hw, enum rtl_led_pin pin)
{
	u16 ledreg = REG_LEDCFG1;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (pin) {
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

	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD,
		"In SwLedOff,LedAddr:%X LEDPIN=%d\n",
		ledreg, pin);
	/*Open-drain arrangement for controlling the LED*/
	if (rtlpriv->ledctl.led_opendrain) {
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
}

static void _rtl8821ae_sw_led_control(struct ieee80211_hw *hw,
				      enum led_ctl_mode ledaction)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	enum rtl_led_pin pin0 = rtlpriv->ledctl.sw_led0;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	switch (ledaction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8812ae_sw_led_on(hw, pin0);
		else
			rtl8821ae_sw_led_on(hw, pin0);
		break;
	case LED_CTL_POWER_OFF:
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8812ae_sw_led_off(hw, pin0);
		else
			rtl8821ae_sw_led_off(hw, pin0);
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
	rtl_dbg(rtlpriv, COMP_LED, DBG_LOUD, "ledaction %d,\n",
		ledaction);
	_rtl8821ae_sw_led_control(hw, ledaction);
}
