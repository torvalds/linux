/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include <drv_types.h>
#include <hal_data.h>

/*
 *	Description:
 *		Turn on LED according to LedPin specified.
 *   */
VOID
HwLedBlink(
	IN	PADAPTER			Adapter,
	IN	PLED_PCIE			pLed
)
{


	switch (pLed->LedPin) {
	case LED_PIN_GPIO0:
		break;

	case LED_PIN_LED0:
		/* rtw_write8(Adapter, LED0Cfg, 0x2); */
		break;

	case LED_PIN_LED1:
		/* rtw_write8(Adapter, LED1Cfg, 0x2); */
		break;

	default:
		break;
	}

	pLed->bLedOn = _TRUE;
}

/*
 *	Description:
 *		Implement LED blinking behavior for SW_LED_MODE0.
 *		It toggle off LED and schedule corresponding timer if necessary.
 *   */
VOID
SwLedBlink(
	IN PLED_PCIE		pLed
)
{
	PADAPTER		Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	/* Determine if we shall change LED state again. */
	pLed->BlinkTimes--;
	switch (pLed->CurrLedState) {
	case LED_BLINK_NORMAL:
	case LED_BLINK_TXRX:
	case LED_BLINK_RUNTOP:
		if (pLed->BlinkTimes == 0)
			bStopBlinking = _TRUE;
		break;

	case LED_BLINK_SCAN:
		if (((check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED)) ||
		     (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))) &&     /* Linked. */
		    (!check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) && /* Not in scan stage. */
		    (pLed->BlinkTimes % 2 == 0)) /* Even */
			bStopBlinking = _TRUE;
		break;

	case LED_BLINK_NO_LINK:
	case LED_BLINK_StartToBlink:
		if (check_fwstate(pmlmepriv, _FW_LINKED) && check_fwstate(pmlmepriv, WIFI_STATION_STATE))
			bStopBlinking = _TRUE;
		else if ((check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) &&
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)))
			bStopBlinking = _TRUE;
		else if (pLed->BlinkTimes == 0)
			bStopBlinking = _TRUE;
		break;

	case LED_BLINK_CAMEO:
		if (check_fwstate(pmlmepriv, _FW_LINKED) && check_fwstate(pmlmepriv, WIFI_STATION_STATE))
			bStopBlinking = _TRUE;
		else if (check_fwstate(pmlmepriv, _FW_LINKED) &&
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)))
			bStopBlinking = _TRUE;
		break;

	default:
		bStopBlinking = _TRUE;
		break;
	}

	if (bStopBlinking) {
		if (adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on)
			SwLedOff(Adapter, pLed);
		else if (pLed->CurrLedState == LED_BLINK_TXRX)
			SwLedOff(Adapter, pLed);
		else if (pLed->CurrLedState == LED_BLINK_RUNTOP)
			SwLedOff(Adapter, pLed);
		else if ((check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) && pLed->bLedOn == _FALSE)
			SwLedOn(Adapter, pLed);
		else if ((check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE) &&  pLed->bLedOn == _TRUE)
			SwLedOff(Adapter, pLed);

		pLed->BlinkTimes = 0;
		pLed->bLedBlinkInProgress = _FALSE;
	} else {
		/* Assign LED state to toggle. */
		if (pLed->BlinkingLedState == RTW_LED_ON)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;

		/* Schedule a timer to toggle LED state. */
		switch (pLed->CurrLedState) {
		case LED_BLINK_NORMAL:
		case LED_BLINK_TXRX:
		case LED_BLINK_StartToBlink:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			break;

		case LED_BLINK_SLOWLY:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			break;

		case LED_BLINK_SCAN:
		case LED_BLINK_NO_LINK:
			if (pLed->bLedOn)
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			else
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_OFF_INTERVAL);
			break;

		case LED_BLINK_RUNTOP:
			_set_timer(&(pLed->BlinkTimer), LED_RunTop_BLINK_INTERVAL);
			break;

		case LED_BLINK_CAMEO:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_PORNET);
			break;

		default:
			/* RTW_INFO("SwLedCm2Blink(): unexpected state!\n"); */
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			break;
		}
	}
}

VOID
SwLedBlink5(
	IN PLED_PCIE		pLed
)
{
	PADAPTER		Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch (pLed->CurrLedState) {
	case RTW_LED_OFF:
		SwLedOff(Adapter, pLed);
		break;

	case LED_BLINK_SLOWLY:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_NETTRONIX);
		break;

	case LED_BLINK_NORMAL:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = _TRUE;
		if (bStopBlinking) {
			if (adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on)
				SwLedOff(Adapter, pLed);
			else {
				pLed->bLedSlowBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_NETTRONIX);
			}
			pLed->BlinkTimes = 0;
			pLed->bLedBlinkInProgress = _FALSE;
		} else {
			if (adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on)
				SwLedOff(Adapter, pLed);
			else {
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;

				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL_NETTRONIX);
			}
		}
		break;

	default:
		break;
	}

}


VOID
SwLedBlink6(
	IN PLED_PCIE		pLed
)
{
	PADAPTER		Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch (pLed->CurrLedState) {
	case RTW_LED_OFF:
		SwLedOff(Adapter, pLed);
		break;

	case LED_BLINK_SLOWLY:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_PORNET);
		break;

	case LED_BLINK_NORMAL:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = _TRUE;
		if (bStopBlinking) {
			if (adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on)
				SwLedOff(Adapter, pLed);
			else {
				pLed->bLedSlowBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_PORNET);
			}
			pLed->BlinkTimes = 0;
			pLed->bLedBlinkInProgress = _FALSE;
		} else {
			if (adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on)
				SwLedOff(Adapter, pLed);
			else {
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;

				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL_PORNET);
			}
		}
		break;

	default:
		break;
	}

}

VOID
SwLedBlink7(
	IN PLED_PCIE		pLed
)
{
	PADAPTER		Adapter = pLed->padapter;

	SwLedOn(Adapter, pLed);
	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
}



/*
 *	Description:
 *		Implement LED blinking behavior for SW_LED_MODE8.
 *		It toggle off LED and schedule corresponding timer if necessary.
 *   */
VOID
SwLedBlink8(
	IN PLED_PCIE		pLed
)
{
	PADAPTER		Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("SwLedBlink8 CurrLedAction %d,\n", pLed->CurrLedState));

	/* Determine if we shall change LED state again. */
	if (pLed->CurrLedState != LED_BLINK_NO_LINK)
		pLed->BlinkTimes--;

	switch (pLed->CurrLedState) {
	case LED_BLINK_NORMAL:
	case LED_BLINK_SCAN:
		if (pLed->BlinkTimes == 0)
			bStopBlinking = _TRUE;
		break;

	default:
		break;
	}

	if (bStopBlinking) {
		if (adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS) {
			pLed->CurrLedState = RTW_LED_OFF;
			SwLedOff(Adapter, pLed);
		} else {
			pLed->CurrLedState = RTW_LED_ON;
			SwLedOn(Adapter, pLed);
		}

		pLed->BlinkTimes = 0;
		pLed->bLedBlinkInProgress = _FALSE;
	} else {
		/* Assign LED state to toggle. */
		if (pLed->BlinkingLedState == RTW_LED_ON)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;

		/* Schedule a timer to toggle LED state. */
		switch (pLed->CurrLedState) {
		case LED_BLINK_NORMAL:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			break;

		default:
			/* RTW_INFO("SwLedCm8Blink(): unexpected state!\n"); */
			break;
		}
	}
}

VOID
SwLedBlink9(
	IN PLED_PCIE		pLed
)
{
	PADAPTER		Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	/* Determine if we shall change LED state again. */
	if (pLed->CurrLedState != LED_BLINK_NO_LINK)
		pLed->BlinkTimes--;

	switch (pLed->CurrLedState) {
	case LED_BLINK_NORMAL:
	case LED_BLINK_SCAN:
		if (pLed->BlinkTimes == 0)
			bStopBlinking = _TRUE;
		break;

	case LED_BLINK_NO_LINK:
		if (check_fwstate(pmlmepriv, _FW_LINKED) && check_fwstate(pmlmepriv, WIFI_STATION_STATE))
			bStopBlinking = _TRUE;
		else if (check_fwstate(pmlmepriv, _FW_LINKED) &&
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)))
			bStopBlinking = _TRUE;
		break;

	default:
		break;
	}

	if (bStopBlinking) {
		if (adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS) {
			pLed->CurrLedState = RTW_LED_OFF;
			SwLedOff(Adapter, pLed);
		} else if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
			pLed->CurrLedState = RTW_LED_ON;
			SwLedOn(Adapter, pLed);
		} else if (check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE) {
			pLed->CurrLedState = LED_BLINK_NO_LINK;
			if (pLed->bLedOn)
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			else
				_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
		}

		pLed->BlinkTimes = 0;
		if (pLed->CurrLedState != LED_BLINK_NO_LINK)
			pLed->bLedBlinkInProgress = _FALSE;
	} else {
		/* Assign LED state to toggle. */
		if (pLed->BlinkingLedState == RTW_LED_ON)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;

		/* Schedule a timer to toggle LED state. */
		switch (pLed->CurrLedState) {
		case LED_BLINK_NORMAL:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_FAST_INTERVAL_BITLAND);
			break;

		case LED_BLINK_SCAN:
		case LED_BLINK_NO_LINK:
			if (pLed->bLedOn)
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			else
				_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
			break;

		default:
			/* RTW_INFO("SwLedCm2Blink(): unexpected state!\n"); */
			break;
		}
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("SwLedBlink9 CurrLedAction %d,\n", pLed->CurrLedState));

}


VOID
SwLedBlink10(
	IN PLED_PCIE		pLed
)
{
	PADAPTER		Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	/* Determine if we shall change LED state again. */
	if (pLed->CurrLedState != LED_BLINK_NO_LINK)
		pLed->BlinkTimes--;

	switch (pLed->CurrLedState) {
	case LED_BLINK_NORMAL:
	case LED_BLINK_SCAN:
		if (pLed->BlinkTimes == 0)
			bStopBlinking = _TRUE;
		break;
	default:
		break;
	}

	if (bStopBlinking) {
		pLed->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed);

		pLed->BlinkTimes = 0;
		pLed->bLedBlinkInProgress = _FALSE;
	} else {
		/* Assign LED state to toggle. */
		if (pLed->BlinkingLedState == RTW_LED_ON)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;

		/* Schedule a timer to toggle LED state. */
		switch (pLed->CurrLedState) {
		case LED_BLINK_NORMAL:
		case LED_BLINK_SCAN:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_FAST_INTERVAL_BITLAND);
			break;

		default:
			/* RT_ASSERT(_FALSE, ("SwLedCm2Blink(): unexpected state!\n")); */
			break;
		}
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("SwLedBlink10 CurrLedAction %d,\n", pLed->CurrLedState));

}


VOID
SwLedBlink11(
	IN PLED_PCIE		pLed
)
{
	PADAPTER		Adapter = pLed->padapter;
	BOOLEAN bStopBlinking = _FALSE;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->bLedBlinkInProgress == _TRUE) {
		if (pLed->BlinkingLedState == RTW_LED_ON) {
			SwLedOn(Adapter, pLed);
			RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%ld): turn on\n", pLed->BlinkTimes));
		} else {
			SwLedOff(Adapter, pLed);
			RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%ld): turn off\n", pLed->BlinkTimes));
		}
	}

	/* Determine if we shall change LED state again. */
	if (pLed->CurrLedState != LED_BLINK_NO_LINK)
		pLed->BlinkTimes--;

	switch (pLed->CurrLedState) {
	case RTW_LED_ON:
		bStopBlinking = _TRUE;	/* LED on for 3 seconds */
	default:
		break;
	}

	if (bStopBlinking) {
		pLed->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed);

		pLed->BlinkTimes = 0;
		pLed->bLedBlinkInProgress = _FALSE;
	} else {
		/* Assign LED state to toggle. */
		if (pLed->BlinkingLedState == RTW_LED_ON)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;

		/* Schedule a timer to toggle LED state. */
		switch (pLed->CurrLedState) {
		case LED_BLINK_XAVI:
			_set_timer(&(pLed->BlinkTimer), LED_CM11_BLINK_INTERVAL);
			break;

		default:
			/* RT_ASSERT(_FALSE, ("SwLedCm11Blink(): unexpected state!\n")); */
			break;
		}
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("SwLedBlink11 CurrLedAction %d,\n", pLed->CurrLedState));

}


VOID
SwLedBlink12(
	IN PLED_PCIE		pLed
)
{
	PADAPTER		Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("SwLedBlink12 Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("SwLedBlink12 Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	/* Determine if we shall change LED state again. */
	if (pLed->CurrLedState != LED_BLINK_NO_LINK && pLed->CurrLedState != LED_BLINK_Azurewave_5Mbps
	    && pLed->CurrLedState != LED_BLINK_Azurewave_10Mbps && pLed->CurrLedState != LED_BLINK_Azurewave_20Mbps
	    && pLed->CurrLedState != LED_BLINK_Azurewave_40Mbps && pLed->CurrLedState != LED_BLINK_Azurewave_80Mbps
	    && pLed->CurrLedState != LED_BLINK_Azurewave_MAXMbps)
		pLed->BlinkTimes--;

	switch (pLed->CurrLedState) {
	case LED_BLINK_NORMAL:
	case LED_BLINK_SCAN:
		if (pLed->BlinkTimes == 0)
			bStopBlinking = _TRUE;
		break;

	case LED_BLINK_NO_LINK:
		if (check_fwstate(pmlmepriv, _FW_LINKED) && check_fwstate(pmlmepriv, WIFI_STATION_STATE))
			bStopBlinking = _TRUE;
		else if (check_fwstate(pmlmepriv, _FW_LINKED) &&
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)))
			bStopBlinking = _TRUE;
		break;

	case LED_BLINK_Azurewave_5Mbps:
	case LED_BLINK_Azurewave_10Mbps:
	case LED_BLINK_Azurewave_20Mbps:
	case LED_BLINK_Azurewave_40Mbps:
	case LED_BLINK_Azurewave_80Mbps:
	case LED_BLINK_Azurewave_MAXMbps:
	/* RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedBlink12 pTurboCa->TxThroughput (%d) pTurboCa->RxThroughput (%d)\n", pTurboCa->TxThroughput, pTurboCa->RxThroughput)); */
	/* if(pTurboCa->TxThroughput + pTurboCa->RxThroughput == 0) */
	/*	bStopBlinking = _TRUE; */

	default:
		break;
	}

	if (bStopBlinking) {
		if (adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS) {
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			SwLedOff(Adapter, pLed);
		} else if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			SwLedOn(Adapter, pLed);
		} else if (check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE) {
			pLed->CurrLedState = LED_BLINK_NO_LINK;
			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
			}
		}

		pLed->BlinkTimes = 0;
		if (pLed->CurrLedState != LED_BLINK_NO_LINK)
			pLed->bLedBlinkInProgress = _FALSE;
	} else {
		/* Assign LED state to toggle. */
		if (pLed->BlinkingLedState == RTW_LED_ON)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;

		/* Schedule a timer to toggle LED state. */
		switch (pLed->CurrLedState) {
		case LED_BLINK_Azurewave_5Mbps:
			_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_5Mbps);
			break;

		case LED_BLINK_Azurewave_10Mbps:
			_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_10Mbps);
			break;

		case LED_BLINK_Azurewave_20Mbps:
			_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_20Mbps);
			break;

		case LED_BLINK_Azurewave_40Mbps:
			_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_40Mbps);
			break;

		case LED_BLINK_Azurewave_80Mbps:
			_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_80Mbps);
			break;

		case LED_BLINK_Azurewave_MAXMbps:
			_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_MAXMbps);
			break;

		case LED_BLINK_SCAN:
		case LED_BLINK_NO_LINK:
			if (pLed->bLedOn)
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			else
				_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
			break;

		default:
			/* RT_ASSERT(_FALSE, ("SwLedCm12Blink(): unexpected state!\n")); */
			break;
		}
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("SwLedBlink12 stopblink %d CurrLedAction %d BlinkingLedState %d,\n", bStopBlinking, pLed->CurrLedState, pLed->BlinkingLedState));

}

/*
 *	Description:
 *		Handler function of LED Blinking.
 *		We dispatch acture LED blink action according to LedStrategy.
 *   */
void BlinkHandler(PLED_PCIE pLed)
{
	_adapter			*padapter = pLed->padapter;
	struct led_priv	*ledpriv = &(padapter->ledpriv);

	if (RTW_CANNOT_RUN(padapter))
		return;

	if (IS_HARDWARE_TYPE_8188E(padapter) ||
	    IS_HARDWARE_TYPE_JAGUAR(padapter) ||
	    IS_HARDWARE_TYPE_8723B(padapter) ||
	    IS_HARDWARE_TYPE_8192E(padapter))
		return;

	switch (ledpriv->LedStrategy) {
	case SW_LED_MODE1:
		/* SwLedBlink(pLed); */
		break;
	case SW_LED_MODE2:
		/* SwLedBlink(pLed); */
		break;
	case SW_LED_MODE3:
		/* SwLedBlink(pLed); */
		break;
	case SW_LED_MODE5:
		/* SwLedBlink5(pLed); */
		break;
	case SW_LED_MODE6:
		/* SwLedBlink6(pLed); */
		break;
	case SW_LED_MODE7:
		SwLedBlink7(pLed);
		break;
	case SW_LED_MODE8:
		SwLedBlink8(pLed);
		break;

	case SW_LED_MODE9:
		SwLedBlink9(pLed);
		break;

	case SW_LED_MODE10:
		SwLedBlink10(pLed);
		break;

	case SW_LED_MODE11:
		SwLedBlink11(pLed);
		break;

	case SW_LED_MODE12:
		SwLedBlink12(pLed);
		break;

	default:
		/* SwLedBlink(pLed); */
		break;
	}
}

/*
 *	Description:
 *		Callback function of LED BlinkTimer,
 *		it just schedules to corresponding BlinkWorkItem/led_blink_hdl
 *   */
void BlinkTimerCallback(void *data)
{
	PLED_PCIE	 pLed = (PLED_PCIE)data;
	_adapter		*padapter = pLed->padapter;

	/* RTW_INFO("%s\n", __FUNCTION__); */

	if (RTW_CANNOT_RUN(padapter) || (!rtw_is_hw_init_completed(padapter))) {
		/*RTW_INFO("%s bDriverStopped:%s, bSurpriseRemoved:%s\n"
			, __func__
			, rtw_is_drv_stopped(padapter)?"True":"False"
			, rtw_is_surprise_removed(padapter)?"True":"False" );
		*/
		return;
	}

#ifdef CONFIG_LED_HANDLED_BY_CMD_THREAD
	rtw_led_blink_cmd(padapter, pLed);
#else
	BlinkHandler(pLed);
#endif
}

/*
 *	Description:
 *		Implement each led action for SW_LED_MODE0. */
VOID
SwLedControlMode0(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE pLed0 = &(ledpriv->SwLed0);
	PLED_PCIE pLed1 = &(ledpriv->SwLed1);

	switch (LedAction) {
	case LED_CTL_TX:
	case LED_CTL_RX:
		break;

	case LED_CTL_LINK:
		pLed0->CurrLedState = RTW_LED_ON;
		SwLedOn(Adapter, pLed0);

		pLed1->CurrLedState = LED_BLINK_NORMAL;
		HwLedBlink(Adapter, pLed1);
		break;

	case LED_CTL_POWER_ON:
		pLed0->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed0);

		pLed1->CurrLedState = LED_BLINK_NORMAL;
		HwLedBlink(Adapter, pLed1);

		break;

	case LED_CTL_POWER_OFF:
		pLed0->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed0);

		pLed1->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed1);
		break;

	case LED_CTL_SITE_SURVEY:
		break;

	case LED_CTL_NO_LINK:
		pLed0->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed0);

		pLed1->CurrLedState = LED_BLINK_NORMAL;
		HwLedBlink(Adapter, pLed1);
		break;

	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Led0 %d Led1 %d\n", pLed0->CurrLedState, pLed1->CurrLedState));
}


VOID
SwLedControlMode1(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	PLED_PCIE	pLed = &(ledpriv->SwLed1);

	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed->bLedBlinkInProgress == _FALSE) {
			pLed->bLedBlinkInProgress = _TRUE;

			pLed->CurrLedState = LED_BLINK_NORMAL;
			pLed->BlinkTimes = 2;

			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
		}
		break;

	case LED_CTL_SITE_SURVEY:
		if (pLed->bLedBlinkInProgress == _FALSE) {
			pLed->bLedBlinkInProgress = _TRUE;

			if ((check_fwstate(pmlmepriv, _FW_LINKED) && check_fwstate(pmlmepriv, WIFI_STATION_STATE)) ||
			    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))) {
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 4;
			} else {
				pLed->CurrLedState = LED_BLINK_NO_LINK;
				pLed->BlinkTimes = 24;
			}

			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_OFF_INTERVAL);
			}
		} else {
			if (pLed->CurrLedState != LED_BLINK_NO_LINK) {
				if ((check_fwstate(pmlmepriv, _FW_LINKED) && check_fwstate(pmlmepriv, WIFI_STATION_STATE)) ||
				    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)))
					pLed->CurrLedState = LED_BLINK_SCAN;
				else
					pLed->CurrLedState = LED_BLINK_NO_LINK;
			}
		}
		break;

	case LED_CTL_NO_LINK:
		if (pLed->bLedBlinkInProgress == _FALSE) {
			pLed->bLedBlinkInProgress = _TRUE;

			pLed->CurrLedState = LED_BLINK_NO_LINK;
			pLed->BlinkTimes = 24;

			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_OFF_INTERVAL);
			}
		} else
			pLed->CurrLedState = LED_BLINK_NO_LINK;
		break;

	case LED_CTL_LINK:
		pLed->CurrLedState = RTW_LED_ON;
		if (pLed->bLedBlinkInProgress == _FALSE)
			SwLedOn(Adapter, pLed);
		break;

	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = RTW_LED_OFF;
		if (pLed->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed);
		break;

	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Led %d\n", pLed->CurrLedState));
}

VOID
SwLedControlMode2(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	PLED_PCIE pLed0 = &(ledpriv->SwLed0);
	PLED_PCIE pLed1 = &(ledpriv->SwLed1);

	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_POWER_ON:
		pLed0->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed0);

		pLed1->CurrLedState = LED_BLINK_CAMEO;
		if (pLed1->bLedBlinkInProgress == _FALSE) {
			pLed1->bLedBlinkInProgress = _TRUE;

			pLed1->BlinkTimes = 6;

			if (pLed1->bLedOn)
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed1->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_PORNET);
		}
		break;

	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed0->bLedBlinkInProgress == _FALSE) {
			pLed0->bLedBlinkInProgress = _TRUE;

			pLed0->CurrLedState = LED_BLINK_TXRX;
			pLed0->BlinkTimes = 2;

			if (pLed0->bLedOn)
				pLed0->BlinkingLedState = RTW_LED_OFF;
			else
				pLed0->BlinkingLedState = RTW_LED_ON;

			_set_timer(&(pLed0->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
		}
		break;

	case LED_CTL_NO_LINK:
		pLed1->CurrLedState = LED_BLINK_CAMEO;
		if (pLed1->bLedBlinkInProgress == _FALSE) {
			pLed1->bLedBlinkInProgress = _TRUE;

			pLed1->BlinkTimes = 6;

			if (pLed1->bLedOn)
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed1->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_PORNET);
		}
		break;

	case LED_CTL_LINK:
		pLed1->CurrLedState = RTW_LED_ON;
		if (pLed1->bLedBlinkInProgress == _FALSE)
			SwLedOn(Adapter, pLed1);
		break;

	case LED_CTL_POWER_OFF:
		pLed0->CurrLedState = RTW_LED_OFF;
		pLed1->CurrLedState = RTW_LED_OFF;
		if (pLed0->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedBlinkInProgress = _FALSE;
		}
		if (pLed1->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed1->BlinkTimer));
			pLed1->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed0);
		SwLedOff(Adapter, pLed1);
		break;

	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Led0 %d, Led1 %d\n", pLed0->CurrLedState, pLed1->CurrLedState));
}



VOID
SwLedControlMode3(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE pLed0 = &(ledpriv->SwLed0);
	PLED_PCIE pLed1 = &(ledpriv->SwLed1);

	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_POWER_ON:
		pLed0->CurrLedState = RTW_LED_ON;
		SwLedOn(Adapter, pLed0);
		pLed1->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed1);
		break;

	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed1->bLedBlinkInProgress == _FALSE) {
			pLed1->bLedBlinkInProgress = _TRUE;

			pLed1->CurrLedState = LED_BLINK_RUNTOP;
			pLed1->BlinkTimes = 2;

			if (pLed1->bLedOn)
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;

			_set_timer(&(pLed1->BlinkTimer), LED_RunTop_BLINK_INTERVAL);
		}
		break;

	case LED_CTL_POWER_OFF:
		pLed0->CurrLedState = RTW_LED_OFF;
		pLed1->CurrLedState = RTW_LED_OFF;
		if (pLed0->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedBlinkInProgress = _FALSE;
		}
		if (pLed1->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed1->BlinkTimer));
			pLed1->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed0);
		SwLedOff(Adapter, pLed1);
		break;

	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Led0 %d, Led1 %d\n", pLed0->CurrLedState, pLed1->CurrLedState));
}


VOID
SwLedControlMode4(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE pLed0 = &(ledpriv->SwLed0);
	PLED_PCIE pLed1 = &(ledpriv->SwLed1);

	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_POWER_ON:
		pLed1->CurrLedState = RTW_LED_ON;
		SwLedOn(Adapter, pLed1);
		pLed0->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed0);
		break;

	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed0->bLedBlinkInProgress == _FALSE) {
			pLed0->bLedBlinkInProgress = _TRUE;

			pLed0->CurrLedState = LED_BLINK_RUNTOP;
			pLed0->BlinkTimes = 2;

			if (pLed0->bLedOn)
				pLed0->BlinkingLedState = RTW_LED_OFF;
			else
				pLed0->BlinkingLedState = RTW_LED_ON;

			_set_timer(&(pLed0->BlinkTimer), LED_RunTop_BLINK_INTERVAL);
		}
		break;

	case LED_CTL_POWER_OFF:
		pLed0->CurrLedState = RTW_LED_OFF;
		pLed1->CurrLedState = RTW_LED_OFF;
		if (pLed0->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedBlinkInProgress = _FALSE;
		}
		if (pLed1->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed1->BlinkTimer));
			pLed1->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed0);
		SwLedOff(Adapter, pLed1);
		break;

	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Led0 %d, Led1 %d\n", pLed0->CurrLedState, pLed1->CurrLedState));
}

/* added by vivi, for led new mode */
VOID
SwLedControlMode5(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE pLed0 = &(ledpriv->SwLed0);
	PLED_PCIE pLed1 = &(ledpriv->SwLed1);
	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_START_TO_LINK:
	case LED_CTL_NO_LINK:
		pLed1->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed1);


		if (pLed0->bLedSlowBlinkInProgress == _FALSE) {
			pLed0->bLedSlowBlinkInProgress = _TRUE;
			pLed0->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed0->bLedOn)
				pLed0->BlinkingLedState = RTW_LED_OFF;
			else
				pLed0->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed0->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_NETTRONIX);
		}

		break;

	case LED_CTL_TX:
	case LED_CTL_RX:
		pLed1->CurrLedState = RTW_LED_ON;
		SwLedOn(Adapter, pLed1);

		if (pLed0->bLedBlinkInProgress == _FALSE) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedSlowBlinkInProgress = _FALSE;
			pLed0->bLedBlinkInProgress = _TRUE;
			pLed0->CurrLedState = LED_BLINK_NORMAL;
			pLed0->BlinkTimes = 2;

			if (pLed0->bLedOn)
				pLed0->BlinkingLedState = RTW_LED_OFF;
			else
				pLed0->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed0->BlinkTimer), LED_BLINK_NORMAL_INTERVAL_NETTRONIX);
		}
		break;

	case LED_CTL_LINK:
		pLed1->CurrLedState = RTW_LED_ON;
		SwLedOn(Adapter, pLed1);

		if (pLed0->bLedSlowBlinkInProgress == _FALSE) {
			pLed0->bLedSlowBlinkInProgress = _TRUE;
			pLed0->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed0->bLedOn)
				pLed0->BlinkingLedState = RTW_LED_OFF;
			else
				pLed0->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed0->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_NETTRONIX);
		}
		break;


	case LED_CTL_POWER_OFF:
		pLed0->CurrLedState = RTW_LED_OFF;
		pLed1->CurrLedState = RTW_LED_OFF;
		if (pLed0->bLedSlowBlinkInProgress == _TRUE) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedSlowBlinkInProgress = _FALSE;
		}
		if (pLed0->bLedBlinkInProgress == _TRUE) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed0);
		SwLedOff(Adapter, pLed1);
		break;

	default:
		break;
	}


}

/* added by vivi, for led new mode */
VOID
SwLedControlMode6(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE pLed0 = &(ledpriv->SwLed0);
	PLED_PCIE pLed1 = &(ledpriv->SwLed1);


	switch (LedAction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_START_TO_LINK:
	case LED_CTL_NO_LINK:
	case LED_CTL_LINK:
	case LED_CTL_SITE_SURVEY:
		pLed1->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed1);

		if (pLed0->bLedSlowBlinkInProgress == _FALSE) {
			pLed0->bLedSlowBlinkInProgress = _TRUE;
			pLed0->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed0->bLedOn)
				pLed0->BlinkingLedState = RTW_LED_OFF;
			else
				pLed0->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed0->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL_PORNET);
		}
		break;

	case LED_CTL_TX:
	case LED_CTL_RX:
		pLed1->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed1);
		if (pLed0->bLedBlinkInProgress == _FALSE) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedSlowBlinkInProgress = _FALSE;
			pLed0->bLedBlinkInProgress = _TRUE;
			pLed0->CurrLedState = LED_BLINK_NORMAL;
			pLed0->BlinkTimes = 2;
			if (pLed0->bLedOn)
				pLed0->BlinkingLedState = RTW_LED_OFF;
			else
				pLed0->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed0->BlinkTimer), LED_BLINK_NORMAL_INTERVAL_PORNET);
		}
		break;

	case LED_CTL_POWER_OFF:
		pLed1->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed1);

		pLed0->CurrLedState = RTW_LED_OFF;
		if (pLed0->bLedSlowBlinkInProgress == _TRUE) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedSlowBlinkInProgress = _FALSE;
		}
		if (pLed0->bLedBlinkInProgress == _TRUE) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed0);
		break;

	default:
		break;

	}
}


/* added by chiyokolin, for Lenovo */
VOID
SwLedControlMode7(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE	pLed0 = &(ledpriv->SwLed0);

	switch (LedAction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		SwLedOn(Adapter, pLed0);
		break;

	case LED_CTL_POWER_OFF:
		SwLedOff(Adapter, pLed0);
		break;

	default:
		break;
	}
}

/* added by chiyokolin, for QMI */
VOID
SwLedControlMode8(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE pLed = &(ledpriv->SwLed0);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);

	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed->bLedBlinkInProgress == _FALSE && (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)) {
			pLed->bLedBlinkInProgress = _TRUE;

			pLed->CurrLedState = LED_BLINK_NORMAL;
			pLed->BlinkTimes = 2;

			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
		}
		break;

	case LED_CTL_SITE_SURVEY:
	case LED_CTL_POWER_ON:
	case LED_CTL_NO_LINK:
	case LED_CTL_LINK:
		pLed->CurrLedState = RTW_LED_ON;
		if (pLed->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		SwLedOn(Adapter, pLed);
		break;

	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = RTW_LED_OFF;
		if (pLed->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed);
		break;

	default:
		break;
	}
}

/* added by chiyokolin, for MSI */
VOID
SwLedControlMode9(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE pLed = &(ledpriv->SwLed0);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);

	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed->bLedBlinkInProgress == _FALSE && (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)) {
			pLed->bLedBlinkInProgress = _TRUE;

			pLed->CurrLedState = LED_BLINK_NORMAL;
			pLed->BlinkTimes = 2;

			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_FAST_INTERVAL_BITLAND);
		}
		break;

	case LED_CTL_SITE_SURVEY:
		if (pLed->bLedBlinkInProgress == _FALSE) {
			pLed->bLedBlinkInProgress = _TRUE;
			pLed->CurrLedState = LED_BLINK_SCAN;
			pLed->BlinkTimes = 2;

			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
			}
		} else if (pLed->CurrLedState != LED_BLINK_SCAN) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->CurrLedState = LED_BLINK_SCAN;
			pLed->BlinkTimes = 2;

			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
			}
		}
		break;

	case LED_CTL_POWER_ON:
	case LED_CTL_NO_LINK:
		if (pLed->bLedBlinkInProgress == _FALSE) {
			pLed->bLedBlinkInProgress = _TRUE;

			pLed->CurrLedState = LED_BLINK_NO_LINK;
			pLed->BlinkTimes = 24;

			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
			}
		} else if (pLed->CurrLedState != LED_BLINK_SCAN && pLed->CurrLedState != LED_BLINK_NO_LINK) {
			pLed->CurrLedState = LED_BLINK_NO_LINK;
			pLed->BlinkTimes = 24;

			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
			}
		}
		break;

	case LED_CTL_LINK:
		pLed->CurrLedState = RTW_LED_ON;
		if (pLed->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		SwLedOn(Adapter, pLed);
		break;

	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = RTW_LED_OFF;
		if (pLed->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed);
		break;

	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Ledcontrol 9 current led state %d,\n", pLed->CurrLedState));

}


/* added by chiyokolin, for Edimax-ASUS */
VOID
SwLedControlMode10(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE	pLed0 = &(ledpriv->SwLed0);
	PLED_PCIE	pLed1 = &(ledpriv->SwLed1);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);

	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed1->bLedBlinkInProgress == _FALSE && pLed1->bLedWPSBlinkInProgress == _FALSE &&
		    (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)) {
			pLed1->bLedBlinkInProgress = _TRUE;

			pLed1->CurrLedState = LED_BLINK_NORMAL;
			pLed1->BlinkTimes = 2;

			if (pLed1->bLedOn)
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed1->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
		}
		break;

	case LED_CTL_SITE_SURVEY:
		if (pLed1->bLedBlinkInProgress == _FALSE && pLed1->bLedWPSBlinkInProgress == _FALSE) {
			pLed1->bLedBlinkInProgress = _TRUE;
			pLed1->CurrLedState = LED_BLINK_SCAN;
			pLed1->BlinkTimes = 12;

			if (pLed1->bLedOn) {
				pLed1->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed1->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			} else {
				pLed1->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed1->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
		} else if (pLed1->CurrLedState != LED_BLINK_SCAN && pLed1->bLedWPSBlinkInProgress == _FALSE) {
			_cancel_timer_ex(&(pLed1->BlinkTimer));
			pLed1->CurrLedState = LED_BLINK_SCAN;
			pLed1->BlinkTimes = 24;

			if (pLed1->bLedOn) {
				pLed1->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed1->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			} else {
				pLed1->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed1->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
		}
		break;

	case LED_CTL_START_WPS:
	case LED_CTL_START_WPS_BOTTON:
		pLed1->CurrLedState = RTW_LED_ON;
		if (pLed1->bLedBlinkInProgress == _TRUE) {
			_cancel_timer_ex(&(pLed1->BlinkTimer));
			pLed1->bLedBlinkInProgress = _FALSE;
		}

		if (pLed1->bLedWPSBlinkInProgress == _FALSE) {
			pLed1->bLedWPSBlinkInProgress = _TRUE;
			SwLedOn(Adapter, pLed1);
		}
		break;

	case	LED_CTL_STOP_WPS:
	case	LED_CTL_STOP_WPS_FAIL:
	case	LED_CTL_STOP_WPS_FAIL_OVERLAP:
		if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
			pLed0->CurrLedState = RTW_LED_ON;
			if (pLed0->bLedBlinkInProgress) {
				_cancel_timer_ex(&(pLed0->BlinkTimer));
				pLed0->bLedBlinkInProgress = _FALSE;
			}
			SwLedOn(Adapter, pLed0);
		} else {
			pLed0->CurrLedState = RTW_LED_OFF;
			if (pLed0->bLedBlinkInProgress) {
				_cancel_timer_ex(&(pLed0->BlinkTimer));
				pLed0->bLedBlinkInProgress = _FALSE;
			}
			SwLedOff(Adapter, pLed0);
		}

		pLed1->CurrLedState = RTW_LED_OFF;
		if (pLed1->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed1->BlinkTimer));
			pLed1->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed1);

		pLed1->bLedWPSBlinkInProgress = _FALSE;

		break;

	case LED_CTL_LINK:
		pLed0->CurrLedState = RTW_LED_ON;
		if (pLed0->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedBlinkInProgress = _FALSE;
		}
		SwLedOn(Adapter, pLed0);
		break;

	case LED_CTL_NO_LINK:
		if (pLed1->bLedWPSBlinkInProgress == _TRUE) {
			SwLedOn(Adapter, pLed1);
			break;
		}

		if (pLed1->CurrLedState == LED_BLINK_SCAN)
			break;

		pLed0->CurrLedState = RTW_LED_OFF;
		if (pLed0->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed0);

		pLed1->CurrLedState = RTW_LED_OFF;
		if (pLed1->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed1->BlinkTimer));
			pLed1->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed1);

		break;


	case LED_CTL_POWER_ON:
	case LED_CTL_POWER_OFF:
		if (pLed1->bLedWPSBlinkInProgress == _TRUE) {
			SwLedOn(Adapter, pLed1);
			break;
		}
		pLed0->CurrLedState = RTW_LED_OFF;
		if (pLed0->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed0);

		pLed1->CurrLedState = RTW_LED_OFF;
		if (pLed1->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed1->BlinkTimer));
			pLed1->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed1);

		break;

	default:
		break;

	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Ledcontrol 10 current led0 state %d led1 state %d,\n", pLed0->CurrLedState, pLed1->CurrLedState));

}


/* added by hpfan, for Xavi */
VOID
SwLedControlMode11(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE	pLed = &(ledpriv->SwLed0);

	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_START_WPS:
	case LED_CTL_START_WPS_BOTTON:
		pLed->bLedWPSBlinkInProgress = _TRUE;
		if (pLed->bLedBlinkInProgress == _FALSE) {
			pLed->bLedBlinkInProgress = _TRUE;
			pLed->CurrLedState = LED_BLINK_XAVI;

			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_CM11_BLINK_INTERVAL);
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_CM11_BLINK_INTERVAL);
			}
		}
		break;

	case LED_CTL_STOP_WPS:
	case LED_CTL_STOP_WPS_FAIL:
	case LED_CTL_STOP_WPS_FAIL_OVERLAP:
		pLed->bLedWPSBlinkInProgress = _FALSE;
		if (pLed->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
			pLed->CurrLedState = RTW_LED_OFF;
		}
		SwLedOff(Adapter, pLed);
		break;

	case LED_CTL_LINK:
		if (pLed->bLedWPSBlinkInProgress)
			break;

		if (pLed->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
			pLed->CurrLedState = RTW_LED_ON;

			if (!pLed->bLedOn)
				SwLedOn(Adapter, pLed);
		} else {
			pLed->CurrLedState = RTW_LED_ON;
			SwLedOn(Adapter, pLed);
		}

		_set_timer(&(pLed->BlinkTimer), LED_CM11_LINK_ON_INTERVEL);
		pLed->BlinkingLedState = RTW_LED_OFF;
		break;

	case LED_CTL_NO_LINK:
		if (pLed->bLedWPSBlinkInProgress)
			break;

		if (pLed->bLedBlinkInProgress == _TRUE) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		pLed->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed);
		break;

	case LED_CTL_POWER_ON:
	case LED_CTL_POWER_OFF:
		if (pLed->bLedBlinkInProgress == _TRUE) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}

		pLed->CurrLedState = RTW_LED_OFF;
		SwLedOff(Adapter, pLed);
		break;

	default:
		break;

	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Ledcontrol 11 current led state %d\n", pLed->CurrLedState));

}

/* added by chiyokolin, for Azurewave */
VOID
SwLedControlMode12(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_PCIE	pLed = &(ledpriv->SwLed0);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	LED_STATE	LedState = LED_UNKNOWN;

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("====>Ledcontrol 12 current led0 state %d\n", pLed->CurrLedState));

	/* Decide led state */
	switch (LedAction) {
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
			if (pLed->CurrLedState == LED_BLINK_SCAN)
				break;

			pLed->BlinkTimes = 0;

			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;

			/*if(pTurboCa->TotalThroughput <= 5)
				LedState = LED_BLINK_Azurewave_5Mbps;
			else if(pTurboCa->TotalThroughput <= 10)
				LedState = LED_BLINK_Azurewave_10Mbps;
			else if(pTurboCa->TotalThroughput <=20)
				LedState = LED_BLINK_Azurewave_20Mbps;
			else if(pTurboCa->TotalThroughput <=40)
				LedState = LED_BLINK_Azurewave_40Mbps;
			else if(pTurboCa->TotalThroughput <=80)
				LedState = LED_BLINK_Azurewave_80Mbps;
			else*/
			LedState = LED_BLINK_Azurewave_MAXMbps;

			if (pLed->bLedBlinkInProgress == _FALSE || pLed->CurrLedState != LedState) {
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->CurrLedState = LedState;
				pLed->bLedBlinkInProgress = _TRUE;

				switch (LedState) {
				case LED_BLINK_Azurewave_5Mbps:
					_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_5Mbps);
					break;

				case LED_BLINK_Azurewave_10Mbps:
					_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_10Mbps);
					break;

				case LED_BLINK_Azurewave_20Mbps:
					_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_20Mbps);
					break;

				case LED_BLINK_Azurewave_40Mbps:
					_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_40Mbps);
					break;

				case LED_BLINK_Azurewave_80Mbps:
					_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_80Mbps);
					break;

				case LED_BLINK_Azurewave_MAXMbps:
					_set_timer(&(pLed->BlinkTimer), LED_CM12_BLINK_INTERVAL_MAXMbps);
					break;

				default:
					break;
				}
			}
		}

		break;

	case LED_CTL_SITE_SURVEY:
	case LED_CTL_START_WPS:
	case LED_CTL_START_WPS_BOTTON:
		if (pLed->bLedBlinkInProgress == _FALSE)
			pLed->bLedBlinkInProgress = _TRUE;
		else if (pLed->CurrLedState != LED_BLINK_SCAN)
			_cancel_timer_ex(&(pLed->BlinkTimer));

		pLed->CurrLedState = LED_BLINK_SCAN;
		pLed->BlinkTimes = 2;

		if (pLed->bLedOn) {
			pLed->BlinkingLedState = RTW_LED_OFF;
			_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
		} else {
			pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
		}
		break;

	case LED_CTL_LINK:
		pLed->CurrLedState = RTW_LED_ON;
		if (pLed->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		SwLedOn(Adapter, pLed);
		break;

	case LED_CTL_NO_LINK:
	case LED_CTL_POWER_ON:
		if (pLed->CurrLedState == LED_BLINK_SCAN)
			break;

		pLed->CurrLedState = LED_BLINK_NO_LINK;
		pLed->bLedBlinkInProgress = _TRUE;

		if (pLed->bLedOn) {
			pLed->BlinkingLedState = RTW_LED_OFF;
			_set_timer(&(pLed->BlinkTimer), LED_CM2_BLINK_ON_INTERVAL);
		} else {
			pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_CM8_BLINK_OFF_INTERVAL);
		}
		break;

	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = RTW_LED_OFF;
		if (pLed->bLedBlinkInProgress) {
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(Adapter, pLed);
		break;

	default:
		break;

	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("<====Ledcontrol 12 current led0 state %d\n", pLed->CurrLedState));

}

void
LedControlPCIE(
	_adapter				*padapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);

#if (MP_DRIVER == 1)
	if (padapter->registrypriv.mp_mode == 1)
		return;
#endif

	if (RTW_CANNOT_RUN(padapter) || (!rtw_is_hw_init_completed(padapter)))
		return;

	/* if(priv->bInHctTest) */
	/*	return; */

#ifdef CONFIG_CONCURRENT_MODE
	/* Only do led action for PRIMARY_ADAPTER */
	if (padapter->adapter_type != PRIMARY_ADAPTER)
		return;
#endif

	if ((adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS) &&
	    (LedAction == LED_CTL_TX ||
	     LedAction == LED_CTL_RX ||
	     LedAction == LED_CTL_SITE_SURVEY ||
	     LedAction == LED_CTL_LINK ||
	     LedAction == LED_CTL_NO_LINK ||
	     LedAction == LED_CTL_START_TO_LINK ||
	     LedAction == LED_CTL_POWER_ON)) {
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("LedControlPCIE(): RfOffReason=0x%x\n", adapter_to_pwrctl(padapter)->rfoff_reason));
		return;
	}

	switch (ledpriv->LedStrategy) {
	case SW_LED_MODE0:
		/* SwLedControlMode0(padapter, LedAction); */
		break;

	case SW_LED_MODE1:
		/* SwLedControlMode1(padapter, LedAction); */
		break;

	case SW_LED_MODE2:
		/* SwLedControlMode2(padapter, LedAction); */
		break;

	case SW_LED_MODE3:
		/* SwLedControlMode3(padapter, LedAction); */
		break;

	case SW_LED_MODE4:
		/* SwLedControlMode4(padapter, LedAction); */
		break;

	case SW_LED_MODE5:
		/* SwLedControlMode5(padapter, LedAction); */
		break;

	case SW_LED_MODE6:
		/* SwLedControlMode6(padapter, LedAction); */
		break;

	case SW_LED_MODE7:
		SwLedControlMode7(padapter, LedAction);
		break;

	case SW_LED_MODE8:
		SwLedControlMode8(padapter, LedAction);
		break;

	case SW_LED_MODE9:
		SwLedControlMode9(padapter, LedAction);
		break;

	case SW_LED_MODE10:
		SwLedControlMode10(padapter, LedAction);
		break;

	case SW_LED_MODE11:
		SwLedControlMode11(padapter, LedAction);
		break;

	case SW_LED_MODE12:
		SwLedControlMode12(padapter, LedAction);
		break;

	default:
		break;
	}
}

/*-----------------------------------------------------------------------------
 * Function:	gen_RefreshLedState()
 *
 * Overview:	When we call the function, media status is no link. It must be in SW/HW
 *			radio off. Or IPS state. If IPS no link we will turn on LED, otherwise, we must turn off.
 *			After MAC IO reset, we must write LED control 0x2f2 again.
 *
 * Input:		IN	PADAPTER			Adapter)
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	03/27/2009	MHC		Create for LED judge only~!!
 *
 *---------------------------------------------------------------------------*/
VOID
gen_RefreshLedState(
	IN	PADAPTER			Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv	*pwrctrlpriv = adapter_to_pwrctl(Adapter);
	struct led_priv	*pledpriv = &(Adapter->ledpriv);
	PLED_PCIE		pLed0 = &(Adapter->ledpriv.SwLed0);

	RTW_INFO("gen_RefreshLedState:() pwrctrlpriv->rfoff_reason=%x\n", pwrctrlpriv->rfoff_reason);

	if (Adapter->bDriverIsGoingToUnload) {
		switch (pledpriv->LedStrategy) {
		case SW_LED_MODE9:
		case SW_LED_MODE10:
			rtw_led_control(Adapter, LED_CTL_POWER_OFF);
			break;

		default:
			/* Turn off LED if RF is not ON. */
			SwLedOff(Adapter, pLed0);
			break;
		}
	} else if (pwrctrlpriv->rfoff_reason == RF_CHANGE_BY_IPS) {
		switch (pledpriv->LedStrategy) {
		case SW_LED_MODE7:
			SwLedOn(Adapter, pLed0);
			break;

		case SW_LED_MODE8:
		case SW_LED_MODE9:
			rtw_led_control(Adapter, LED_CTL_NO_LINK);
			break;

		default:
			SwLedOn(Adapter, pLed0);
			break;
		}
	} else if (pwrctrlpriv->rfoff_reason == RF_CHANGE_BY_INIT) {
		switch (pledpriv->LedStrategy) {
		case SW_LED_MODE7:
			SwLedOn(Adapter, pLed0);
			break;

		case SW_LED_MODE9:
			rtw_led_control(Adapter, LED_CTL_NO_LINK);
			break;

		default:
			SwLedOn(Adapter, pLed0);
			break;

		}
	} else {	/* SW/HW radio off */

		switch (pledpriv->LedStrategy) {
		case SW_LED_MODE9:
			rtw_led_control(Adapter, LED_CTL_POWER_OFF);
			break;

		default:
			/* Turn off LED if RF is not ON. */
			SwLedOff(Adapter, pLed0);
			break;
		}
	}

}

/*
 *	Description:
 *		Reset status of LED_871x object.
 *   */
void ResetLedStatus(PLED_PCIE pLed)
{

	pLed->CurrLedState = RTW_LED_OFF; /* Current LED state. */
	pLed->bLedOn = _FALSE; /* true if LED is ON, false if LED is OFF. */

	pLed->bLedBlinkInProgress = _FALSE; /* true if it is blinking, false o.w.. */
	pLed->bLedWPSBlinkInProgress = _FALSE;
	pLed->bLedSlowBlinkInProgress = _FALSE;

	pLed->BlinkTimes = 0; /* Number of times to toggle led state for blinking. */
	pLed->BlinkingLedState = LED_UNKNOWN; /* Next state for blinking, either RTW_LED_ON or RTW_LED_OFF are. */
}

/*
*	Description:
*		Initialize an LED_871x object.
*   */
void
InitLed(
	_adapter			*padapter,
	PLED_PCIE		pLed,
	LED_PIN			LedPin
)
{
	pLed->padapter = padapter;
	pLed->LedPin = LedPin;

	ResetLedStatus(pLed);

	_init_timer(&(pLed->BlinkTimer), padapter->pnetdev, BlinkTimerCallback, pLed);
}


/*
 *	Description:
 *		DeInitialize an LED_871x object.
 *   */
void
DeInitLed(
	PLED_PCIE		pLed
)
{
	_cancel_timer_ex(&(pLed->BlinkTimer));
	ResetLedStatus(pLed);
}
