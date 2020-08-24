/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#include <drv_types.h>
#include <hal_data.h>

#ifdef CONFIG_RTW_LED
void dump_led_config(void *sel, _adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct led_priv	*ledpriv = adapter_to_led(adapter);
	int i;

	RTW_PRINT_SEL(sel, "strategy:%u\n", ledpriv->LedStrategy);
#ifdef CONFIG_RTW_SW_LED
	RTW_PRINT_SEL(sel, "bRegUseLed:%u\n", ledpriv->bRegUseLed);
	RTW_PRINT_SEL(sel, "iface_en_mask:0x%02X\n", ledpriv->iface_en_mask);
	for (i = 0; i < dvobj->iface_nums; i++)
		RTW_PRINT_SEL(sel, "ctl_en_mask[%d]:0x%08X\n", i, ledpriv->ctl_en_mask[i]);
#endif
}

void rtw_led_set_strategy(_adapter *adapter, u8 strategy)
{
	struct led_priv *ledpriv = adapter_to_led(adapter);
	_adapter *pri_adapter = GET_PRIMARY_ADAPTER(adapter);

#ifndef CONFIG_RTW_SW_LED
	if (IS_SW_LED_STRATEGY(strategy)) {
		RTW_WARN("CONFIG_RTW_SW_LED is not defined\n");
		return;
	}
#endif

#ifdef CONFIG_RTW_SW_LED
	if (!ledpriv->bRegUseLed)
		return;
#endif

	if (ledpriv->LedStrategy == strategy)
		return;

	if (IS_HW_LED_STRATEGY(strategy) || IS_HW_LED_STRATEGY(ledpriv->LedStrategy)) {
		RTW_WARN("switching on/off HW_LED strategy is not supported\n");
		return;
	}

	ledpriv->LedStrategy = strategy;

#ifdef CONFIG_RTW_SW_LED
	rtw_hal_sw_led_deinit(pri_adapter);
#endif

	rtw_led_control(pri_adapter, RTW_LED_OFF);
}

#ifdef CONFIG_RTW_SW_LED
#if CONFIG_RTW_SW_LED_TRX_DA_CLASSIFY
void rtw_sw_led_blink_uc_trx_only(LED_DATA *led)
{
	_adapter *adapter = led->padapter;
	BOOLEAN bStopBlinking = _FALSE;

	if (led->BlinkingLedState == RTW_LED_ON)
		SwLedOn(adapter, led);
	else
		SwLedOff(adapter, led);

	switch (led->CurrLedState) {
	case RTW_LED_ON:
		SwLedOn(adapter, led);
		break;

	case RTW_LED_OFF:
		SwLedOff(adapter, led);
		break;

	case LED_BLINK_TXRX:
		led->BlinkTimes--;
		if (led->BlinkTimes == 0)
			bStopBlinking = _TRUE;

		if (adapter_to_pwrctl(adapter)->rf_pwrstate != rf_on
			&& adapter_to_pwrctl(adapter)->rfoff_reason > RF_CHANGE_BY_PS
		) {
			SwLedOff(adapter, led);
			led->bLedBlinkInProgress = _FALSE;
		} else {
			if (led->bLedOn)
				led->BlinkingLedState = RTW_LED_OFF;
			else
				led->BlinkingLedState = RTW_LED_ON;
			
			if (bStopBlinking) {
				led->CurrLedState = RTW_LED_OFF;
				led->bLedBlinkInProgress = _FALSE;
			}
			_set_timer(&(led->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
		}
		break;

	default:
		break;
	}
}

void rtw_sw_led_ctl_mode_uc_trx_only(_adapter *adapter, LED_CTL_MODE ctl)
{
	struct led_priv	*ledpriv = adapter_to_led(adapter);
	LED_DATA *led = &(ledpriv->SwLed0);
	LED_DATA *led1 = &(ledpriv->SwLed1);
	LED_DATA *led2 = &(ledpriv->SwLed2);

	switch (ctl) {
	case LED_CTL_UC_TX:
	case LED_CTL_UC_RX:
		if (led->bLedBlinkInProgress == _FALSE) {
			led->bLedBlinkInProgress = _TRUE;
			led->CurrLedState = LED_BLINK_TXRX;
			led->BlinkTimes = 2;
			if (led->bLedOn)
				led->BlinkingLedState = RTW_LED_OFF;
			else
				led->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(led->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
		}
		break;

	case LED_CTL_POWER_OFF:
		led->CurrLedState = RTW_LED_OFF;
		led->BlinkingLedState = RTW_LED_OFF;

		if (led->bLedBlinkInProgress) {
			_cancel_timer_ex(&(led->BlinkTimer));
			led->bLedBlinkInProgress = _FALSE;
		}

		SwLedOff(adapter, led);
		SwLedOff(adapter, led1);
		SwLedOff(adapter, led2);
		break;

	default:
		break;
	}
}
#endif /* CONFIG_RTW_SW_LED_TRX_DA_CLASSIFY */

void rtw_led_control(_adapter *adapter, LED_CTL_MODE ctl)
{
	struct led_priv	*ledpriv = adapter_to_led(adapter);

	if (ledpriv->LedControlHandler) {
		#if CONFIG_RTW_SW_LED_TRX_DA_CLASSIFY
		if (ledpriv->LedStrategy != SW_LED_MODE_UC_TRX_ONLY) {
			if (ctl == LED_CTL_UC_TX || ctl == LED_CTL_BMC_TX) {
				if (ledpriv->ctl_en_mask[adapter->iface_id] & BIT(LED_CTL_TX))
					ctl = LED_CTL_TX; /* transform specific TX ctl to general TX ctl */
			} else if (ctl == LED_CTL_UC_RX || ctl == LED_CTL_BMC_RX) {
				if (ledpriv->ctl_en_mask[adapter->iface_id] & BIT(LED_CTL_RX))
					ctl = LED_CTL_RX; /* transform specific RX ctl to general RX ctl */
			}
		}
		#endif

		if ((ledpriv->iface_en_mask & BIT(adapter->iface_id))
			&& (ledpriv->ctl_en_mask[adapter->iface_id] & BIT(ctl)))
			ledpriv->LedControlHandler(adapter, ctl);
	}
}

void rtw_led_tx_control(_adapter *adapter, const u8 *da)
{
#if CONFIG_RTW_SW_LED_TRX_DA_CLASSIFY
	if (IS_MCAST(da))
		rtw_led_control(adapter, LED_CTL_BMC_TX);
	else
		rtw_led_control(adapter, LED_CTL_UC_TX);
#else
	rtw_led_control(adapter, LED_CTL_TX);
#endif
}

void rtw_led_rx_control(_adapter *adapter, const u8 *da)
{
#if CONFIG_RTW_SW_LED_TRX_DA_CLASSIFY
	if (IS_MCAST(da))
		rtw_led_control(adapter, LED_CTL_BMC_RX);
	else
		rtw_led_control(adapter, LED_CTL_UC_RX);
#else
	rtw_led_control(adapter, LED_CTL_RX);
#endif
}

void rtw_led_set_iface_en(_adapter *adapter, u8 en)
{
	struct led_priv *ledpriv = adapter_to_led(adapter);

	if (en)
		ledpriv->iface_en_mask |= BIT(adapter->iface_id);
	else
		ledpriv->iface_en_mask &= ~BIT(adapter->iface_id);
}

void rtw_led_set_iface_en_mask(_adapter *adapter, u8 mask)
{
	struct led_priv *ledpriv = adapter_to_led(adapter);

	ledpriv->iface_en_mask = mask;
}

void rtw_led_set_ctl_en_mask(_adapter *adapter, u32 ctl_mask)
{
	struct led_priv *ledpriv = adapter_to_led(adapter);
	
#if CONFIG_RTW_SW_LED_TRX_DA_CLASSIFY
	if (ctl_mask & BIT(LED_CTL_TX))
		ctl_mask |= BIT(LED_CTL_UC_TX) | BIT(LED_CTL_BMC_TX);
	if (ctl_mask & BIT(LED_CTL_RX))
		ctl_mask |= BIT(LED_CTL_UC_RX) | BIT(LED_CTL_BMC_RX);
#endif

	ledpriv->ctl_en_mask[adapter->iface_id] = ctl_mask;
}

void rtw_led_set_ctl_en_mask_primary(_adapter *adapter)
{
	rtw_led_set_ctl_en_mask(adapter, 0xFFFFFFFF);
}

void rtw_led_set_ctl_en_mask_virtual(_adapter *adapter)
{
	rtw_led_set_ctl_en_mask(adapter
		, BIT(LED_CTL_POWER_ON) | BIT(LED_CTL_POWER_OFF)
		| BIT(LED_CTL_TX) | BIT(LED_CTL_RX)
	);
}
#endif /* CONFIG_RTW_SW_LED */

#endif /* CONFIG_RTW_LED */

