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

//
//	Description:
//		Implementation of LED blinking behavior.
//		It toggle off LED and schedule corresponding timer if necessary.
//
void
SwLedBlink(
	PLED_USB			pLed
	)
{
	_adapter			*padapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	u8				bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,( "Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	// Determine if we shall change LED state again.
	pLed->BlinkTimes--;
	switch(pLed->CurrLedState)
	{

	case LED_BLINK_NORMAL:
		if(pLed->BlinkTimes == 0)
		{
			bStopBlinking = _TRUE;
		}
		break;

	case LED_BLINK_StartToBlink:
		if( check_fwstate(pmlmepriv, _FW_LINKED) && check_fwstate(pmlmepriv, WIFI_STATION_STATE) )
		{
			bStopBlinking = _TRUE;
		}
		if( check_fwstate(pmlmepriv, _FW_LINKED) &&
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) )
		{
			bStopBlinking = _TRUE;
		}
		else if(pLed->BlinkTimes == 0)
		{
			bStopBlinking = _TRUE;
		}
		break;

	case LED_BLINK_WPS:
		if( pLed->BlinkTimes == 0 )
		{
			bStopBlinking = _TRUE;
		}
		break;


	default:
		bStopBlinking = _TRUE;
		break;

	}

	if(bStopBlinking)
	{
		if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
		{
			SwLedOff(padapter, pLed);
		}
		else if( (check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE) && (pLed->bLedOn == _FALSE))
		{
			SwLedOn(padapter, pLed);
		}
		else if( (check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE) &&  pLed->bLedOn == _TRUE)
		{
			SwLedOff(padapter, pLed);
		}

		pLed->BlinkTimes = 0;
		pLed->bLedBlinkInProgress = _FALSE;
	}
	else
	{
		// Assign LED state to toggle.
		if( pLed->BlinkingLedState == RTW_LED_ON )
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;

		// Schedule a timer to toggle LED state.
		switch( pLed->CurrLedState )
		{
		case LED_BLINK_NORMAL:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			break;

		case LED_BLINK_SLOWLY:
		case LED_BLINK_StartToBlink:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			break;

		case LED_BLINK_WPS:
			{
				if( pLed->BlinkingLedState == RTW_LED_ON )
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LONG_INTERVAL);
				else
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LONG_INTERVAL);
			}
			break;

		default:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			break;
		}
	}
}

void
SwLedBlink1(
	PLED_USB			pLed
	)
{
	_adapter				*padapter = pLed->padapter;
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(padapter);
	struct led_priv		*ledpriv = &(padapter->ledpriv);
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
	PLED_USB 			pLed1 = &(ledpriv->SwLed1);
	u8					bStopBlinking = _FALSE;

	u32 uLedBlinkNoLinkInterval = LED_BLINK_NO_LINK_INTERVAL_ALPHA; //add by ylb 20121012 for customer led for alpha
	if(pHalData->CustomerID == RT_CID_819x_ALPHA_Dlink)
		uLedBlinkNoLinkInterval= LED_BLINK_NO_LINK_INTERVAL_ALPHA_500MS;
	
	if(pHalData->CustomerID == RT_CID_819x_CAMEO)
		pLed = &(ledpriv->SwLed1);

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,( "Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}


	if(pHalData->CustomerID == RT_CID_DEFAULT)
	{
		if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
		{
			if(!pLed1->bSWLedCtrl)
			{
				SwLedOn(padapter, pLed1);
				pLed1->bSWLedCtrl = _TRUE;
			}
			else if(!pLed1->bLedOn)
				SwLedOn(padapter, pLed1);
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (): turn on pLed1\n"));
		}
		else
		{
			if(!pLed1->bSWLedCtrl)
			{
				SwLedOff(padapter, pLed1);
				pLed1->bSWLedCtrl = _TRUE;
			}
			else if(pLed1->bLedOn)
				SwLedOff(padapter, pLed1);
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (): turn off pLed1\n"));
		}
	}

	switch(pLed->CurrLedState)
	{
		case LED_BLINK_SLOWLY:
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), uLedBlinkNoLinkInterval);//change by ylb 20121012 for customer led for alpha
			break;

		case LED_BLINK_NORMAL:
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
			break;

		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				{
					pLed->bLedLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_NORMAL;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));

				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE)
				{
					pLed->bLedNoLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), uLedBlinkNoLinkInterval);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				{
					pLed->bLedLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_NORMAL;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE)
				{
					pLed->bLedNoLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), uLedBlinkNoLinkInterval);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->BlinkTimes = 0;
				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_WPS:
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			break;

		case LED_BLINK_WPS_STOP:	//WPS success
			if(pLed->BlinkingLedState == RTW_LED_ON)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA);
				bStopBlinking = _FALSE;
			}
			else
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->bLedLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_NORMAL;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}
			break;

		default:
			break;
	}

}

void
SwLedBlink2(
	PLED_USB			pLed
	)
{
	_adapter				*padapter = pLed->padapter;
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
	u8					bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON)
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch(pLed->CurrLedState)
	{
		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					SwLedOn(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("stop scan blink CurrLedState %d\n", pLed->CurrLedState));

				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					SwLedOff(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("stop scan blink CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					SwLedOn(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("stop CurrLedState %d\n", pLed->CurrLedState));

				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					SwLedOff(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("stop CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		default:
			break;
	}

}

void
SwLedBlink3(
	PLED_USB			pLed
	)
{
	_adapter			*padapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	u8				bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		if(pLed->CurrLedState != LED_BLINK_WPS_STOP)
			SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch(pLed->CurrLedState)
	{
		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					if( !pLed->bLedOn )
						SwLedOn(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if( pLed->bLedOn )
						SwLedOff(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
				 	if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;

					if( !pLed->bLedOn )
						SwLedOn(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;

					if( pLed->bLedOn )
						SwLedOff(padapter, pLed);


					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_WPS:
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			break;

		case LED_BLINK_WPS_STOP:	//WPS success
			if(pLed->BlinkingLedState == RTW_LED_ON)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA);
				bStopBlinking = _FALSE;
			}
			else
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					SwLedOn(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}
			break;


		default:
			break;
	}

}


void
SwLedBlink4(
	PLED_USB			pLed
	)
{
	_adapter			*padapter = pLed->padapter;
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	PLED_USB 		pLed1 = &(ledpriv->SwLed1);
	u8				bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	if(!pLed1->bLedWPSBlinkInProgress && pLed1->BlinkingLedState == LED_UNKNOWN)
	{
		pLed1->BlinkingLedState = RTW_LED_OFF;
		pLed1->CurrLedState = RTW_LED_OFF;
		SwLedOff(padapter, pLed1);
	}

	switch(pLed->CurrLedState)
	{
		case LED_BLINK_SLOWLY:
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			break;

		case LED_BLINK_StartToBlink:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			break;

		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _FALSE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
				}
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->bLedNoLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
				}
				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_WPS:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			break;

		case LED_BLINK_WPS_STOP:	//WPS authentication fail
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;

			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			break;

		case LED_BLINK_WPS_STOP_OVERLAP:	//WPS session overlap
			pLed->BlinkTimes--;
			if(pLed->BlinkTimes == 0)
			{
				if(pLed->bLedOn)
				{
					pLed->BlinkTimes = 1;
				}
				else
				{
					bStopBlinking = _TRUE;
				}
			}

			if(bStopBlinking)
			{
				pLed->BlinkTimes = 10;
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
			}
			else
			{
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;

				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			break;

		case LED_BLINK_ALWAYS_ON:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->bLedNoLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
				}
				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("RFOff Status \n"));
					SwLedOff(padapter, pLed);
				}
				else
				{
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedBlink4 CurrLedState %d\n", pLed->CurrLedState));


}

void
SwLedBlink5(
	PLED_USB			pLed
	)
{
	_adapter			*padapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	u8				bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch(pLed->CurrLedState)
	{
		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if(pLed->bLedOn)
						SwLedOff(padapter, pLed);
				}
				else
				{		pLed->CurrLedState = RTW_LED_ON;
						pLed->BlinkingLedState = RTW_LED_ON;
						if(!pLed->bLedOn)
							_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}

				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;


		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if(pLed->bLedOn)
						SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					if(!pLed->bLedOn)
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}

				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedBlink5 CurrLedState %d\n", pLed->CurrLedState));


}

void
SwLedBlink6(
	PLED_USB			pLed
	)
{
	_adapter			*padapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	u8				bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("<==== blink6\n"));
}

void
SwLedBlink7(
	PLED_USB			pLed
	)
{
	PADAPTER Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		if(pLed->CurrLedState != LED_BLINK_WPS_STOP)
			SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch(pLed->CurrLedState)
	{
		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(Adapter, pLed);
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					if( !pLed->bLedOn )
						SwLedOn(Adapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if( pLed->bLedOn )
						SwLedOff(Adapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));					
				}
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
				 	if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_NETGEAR);
				}
			}
			break;

		case LED_BLINK_WPS:
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_NETGEAR);
			break;

		case LED_BLINK_WPS_STOP:	//WPS success
			if(pLed->BlinkingLedState == RTW_LED_ON)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_NETGEAR);
				bStopBlinking = _FALSE;
			}
			else
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					SwLedOn(Adapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}
			break;


		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("<==== blink7\n"));

}

void
SwLedBlink8(
	PLED_USB			pLed
	)
{
	PADAPTER Adapter = pLed->padapter;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes blink8(%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes blink8(%d): turn off\n", pLed->BlinkTimes));
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("<==== blink8\n"));

}

//page added for Belkin AC950. 20120813
void
SwLedBlink9(
	PLED_USB			pLed
	)
{
	PADAPTER Adapter = pLed->padapter; 
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON ) 
	{
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else 
	{
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}	
	//DBG_871X("%s, pLed->CurrLedState=%d, pLed->BlinkingLedState=%d \n", __FUNCTION__, pLed->CurrLedState, pLed->BlinkingLedState);


	switch(pLed->CurrLedState)
	{
		case RTW_LED_ON:
			SwLedOn(Adapter, pLed);
			break;
			
		case RTW_LED_OFF:
			SwLedOff(Adapter, pLed);
			break;
			
		case LED_BLINK_SLOWLY:			
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF; 
			else
				pLed->BlinkingLedState = RTW_LED_ON; 
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			break;

		case LED_BLINK_StartToBlink:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);		
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			break;			
			
		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(Adapter, pLed);
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
				{
					pLed->bLedLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));					
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE)
				{
					pLed->bLedNoLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF; 
					else
						pLed->BlinkingLedState = RTW_LED_ON; 
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));					
				}
				pLed->BlinkTimes = 0;
				pLed->bLedBlinkInProgress = _FALSE;	
			}
			else
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
					 if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF; 
					else
						pLed->BlinkingLedState = RTW_LED_ON; 
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else 
				{
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF; 
					else
						pLed->BlinkingLedState = RTW_LED_ON; 
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
				pLed->bLedBlinkInProgress = _FALSE;	
			}
			else
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF; 
					else
						pLed->BlinkingLedState = RTW_LED_ON; 
					
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_WPS:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF; 
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			break;

		case LED_BLINK_WPS_STOP:	//WPS authentication fail
			if( pLed->bLedOn )			
				pLed->BlinkingLedState = RTW_LED_OFF; 			
			else			
				pLed->BlinkingLedState = RTW_LED_ON; 
			
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			break;

		case LED_BLINK_WPS_STOP_OVERLAP:	//WPS session overlap		
			pLed->BlinkTimes--;
			pLed->BlinkCounter --;
			if(pLed->BlinkCounter == 0)
			{
				pLed->BlinkingLedState = RTW_LED_OFF; 
				pLed->CurrLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			else
			{
				if(pLed->BlinkTimes == 0)
				{
					if(pLed->bLedOn)
					{
						pLed->BlinkTimes = 1;							
					}
					else
					{
						bStopBlinking = _TRUE;
					}
				}

				if(bStopBlinking)
				{				
					pLed->BlinkTimes = 10;			
					pLed->BlinkingLedState = RTW_LED_ON; 							
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);								
				}
				else
				{
					if( pLed->bLedOn )			
						pLed->BlinkingLedState = RTW_LED_OFF;			
					else			
						pLed->BlinkingLedState = RTW_LED_ON; 
					
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);						
				}
			}
			break;
			
		case LED_BLINK_ALWAYS_ON:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else 
				{
					if(IS_HARDWARE_TYPE_8812AU(Adapter))
					{
						pLed->BlinkingLedState = RTW_LED_ON; 
						pLed->CurrLedState = LED_BLINK_ALWAYS_ON;
					}
					else
					{
						pLed->bLedNoLinkBlinkInProgress = _TRUE;
						pLed->CurrLedState = LED_BLINK_SLOWLY;
						if( pLed->bLedOn )
							pLed->BlinkingLedState = RTW_LED_OFF; 
						else
							pLed->BlinkingLedState = RTW_LED_ON; 
					}
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
				}
				pLed->bLedBlinkInProgress = _FALSE;	
			}
			else
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("RFOff Status \n"));
					SwLedOff(Adapter, pLed);
				}
				else
				{
					if(IS_HARDWARE_TYPE_8812AU(Adapter))
					{
						pLed->BlinkingLedState = RTW_LED_ON; 
					}
					else
					{
						if( pLed->bLedOn )
							pLed->BlinkingLedState = RTW_LED_OFF; 
						else
							pLed->BlinkingLedState = RTW_LED_ON; 
					}
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_LINK_IN_PROCESS:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF; 
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ON_BELKIN);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_OFF_BELKIN);
			}
			break;

		case LED_BLINK_AUTH_ERROR:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking == _FALSE)
			{
				if( pLed->bLedOn )
				{
					pLed->BlinkingLedState = RTW_LED_OFF; 
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_ERROR_INTERVAL_BELKIN);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON; 
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_ERROR_INTERVAL_BELKIN);
				}
			}
			else
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_ERROR_INTERVAL_BELKIN);
			}
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedBlink9 CurrLedState %d\n", pLed->CurrLedState));					
}

//page added for Netgear A6200V2. 20120827
void
SwLedBlink10(
	PLED_USB			pLed
	)
{
	PADAPTER Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}


	switch(pLed->CurrLedState)
	{
		case RTW_LED_ON:
			SwLedOn(Adapter, pLed);
			break;

		case RTW_LED_OFF:
			SwLedOff(Adapter, pLed);
			break;

		case LED_BLINK_SLOWLY:			
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			break;

		case LED_BLINK_StartToBlink:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			break;

		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on )
				{
					SwLedOff(Adapter, pLed);
				}
				else if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				{
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;

					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->BlinkTimes = 0;
				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
					if( pLed->bLedOn )
					{
						pLed->BlinkingLedState = RTW_LED_OFF;
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_NETGEAR);
					}
					else
					{
						pLed->BlinkingLedState = RTW_LED_ON;
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_SLOWLY_INTERVAL_NETGEAR+LED_BLINK_LINK_INTERVAL_NETGEAR);
					}
				}
			}
			break;

		case LED_BLINK_WPS:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_NETGEAR);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL+LED_BLINK_LINK_INTERVAL_NETGEAR);
			}
			break;

		case LED_BLINK_WPS_STOP:	//WPS authentication fail
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else	
				pLed->BlinkingLedState = RTW_LED_ON;

			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			break;

		case LED_BLINK_WPS_STOP_OVERLAP:	//WPS session overlap
			pLed->BlinkTimes--;
			pLed->BlinkCounter --;
			if(pLed->BlinkCounter == 0)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				pLed->CurrLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			else
			{
				if(pLed->BlinkTimes == 0)
				{
					if(pLed->bLedOn)
					{
						pLed->BlinkTimes = 1;
					}
					else
					{
						bStopBlinking = _TRUE;
					}
				}

				if(bStopBlinking)
				{
					pLed->BlinkTimes = 10;
					pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
				}
				else
				{
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else	
						pLed->BlinkingLedState = RTW_LED_ON;

					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
				}
			}
			break;

		case LED_BLINK_ALWAYS_ON:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
					if(IS_HARDWARE_TYPE_8812AU(Adapter))
					{
						pLed->BlinkingLedState = RTW_LED_ON;
						pLed->CurrLedState = LED_BLINK_ALWAYS_ON;
					}
					else
					{
						pLed->bLedNoLinkBlinkInProgress = _TRUE;
						pLed->CurrLedState = LED_BLINK_SLOWLY;
						if( pLed->bLedOn )
							pLed->BlinkingLedState = RTW_LED_OFF;
						else
							pLed->BlinkingLedState = RTW_LED_ON;
					}
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
				}
				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("RFOff Status \n"));
					SwLedOff(Adapter, pLed);
				}
				else
				{
					if(IS_HARDWARE_TYPE_8812AU(Adapter))
					{
						pLed->BlinkingLedState = RTW_LED_ON;
					}
					else
					{
						if( pLed->bLedOn )
							pLed->BlinkingLedState = RTW_LED_OFF;
						else
							pLed->BlinkingLedState = RTW_LED_ON;
					}
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_LINK_IN_PROCESS:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ON_BELKIN);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_OFF_BELKIN);
			}
			break;

		case LED_BLINK_AUTH_ERROR:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking == _FALSE)
			{
				if( pLed->bLedOn )
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_ERROR_INTERVAL_BELKIN);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON; 
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_ERROR_INTERVAL_BELKIN);
				}
			}
			else
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_ERROR_INTERVAL_BELKIN);
			}
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedBlink10 CurrLedState %d\n", pLed->CurrLedState));

}

void
SwLedBlink11(
	PLED_USB			pLed
	)
{
	PADAPTER Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch(pLed->CurrLedState)
	{
		case LED_BLINK_TXRX:
			if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
			{
				SwLedOff(Adapter, pLed);
			}
			else
			{
				 if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}

			break;

		case LED_BLINK_WPS:
			if(pLed->BlinkTimes == 5)
			{
				SwLedOn(Adapter, pLed);
				_set_timer(&(pLed->BlinkTimer), LED_CM11_LINK_ON_INTERVEL);
			}
			else
			{
				if( pLed->bLedOn )
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					_set_timer(&(pLed->BlinkTimer), LED_CM11_BLINK_INTERVAL);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_CM11_BLINK_INTERVAL);
				}
			}
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking == _TRUE)
				pLed->BlinkTimes = 5;
			break;

		case LED_BLINK_WPS_STOP:	//WPS authentication fail
			if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
			{
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}
			else
			{
				pLed->CurrLedState = RTW_LED_ON;
				pLed->BlinkingLedState = RTW_LED_ON;
				SwLedOn(Adapter, pLed);
				_set_timer(&(pLed->BlinkTimer), 0);
			}
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedBlink5 CurrLedState %d\n", pLed->CurrLedState));
}

void
SwLedBlink12(
	PLED_USB			pLed
	)
{
	PADAPTER Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%ld): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%ld): turn off\n", pLed->BlinkTimes));
	}

	switch(pLed->CurrLedState)
	{
		case LED_BLINK_SLOWLY:
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}

			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if(pLed->bLedOn)
						SwLedOff(Adapter, pLed);
				}
				else
				{
					pLed->bLedNoLinkBlinkInProgress = _TRUE;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
				}

				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
					 if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedBlink8 CurrLedState %d\n", pLed->CurrLedState));


}

VOID
SwLedBlink13(
	IN PLED_USB			pLed
	)
{
	PADAPTER Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;
	static u8	LinkBlinkCnt=0;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON ) 
	{
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else 
	{
		if(pLed->CurrLedState != LED_BLINK_WPS_STOP)
			SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}	
	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("!!! SwLedBlink13 CurrLedState %d, bLedWPSBlinkInProgress %d, bLedBlinkInProgress %d\n", pLed->CurrLedState,pLed->bLedWPSBlinkInProgress,pLed->bLedBlinkInProgress));
	switch(pLed->CurrLedState)
	{			
		case LED_BLINK_LINK_IN_PROCESS:
			if(!pLed->bLedWPSBlinkInProgress)
				LinkBlinkCnt++;
			
			if(LinkBlinkCnt>15)
			{
				LinkBlinkCnt=0;
				pLed->bLedBlinkInProgress = _FALSE;
				break;
			}
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF; 
				_set_timer(&(pLed->BlinkTimer), 500);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed->BlinkTimer), 500);
			}

			break;

		case LED_BLINK_WPS:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF; 
				_set_timer(&(pLed->BlinkTimer), LED_WPS_BLINK_ON_INTERVAL_NETGEAR);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed->BlinkTimer), LED_WPS_BLINK_OFF_INTERVAL_NETGEAR);
			}
			
			break;

		case LED_BLINK_WPS_STOP:	//WPS success
			SwLedOff(Adapter, pLed);	
			pLed->bLedWPSBlinkInProgress = _FALSE;	
			break;
					
		default:
			LinkBlinkCnt=0;
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("<==== blink13\n"));

}

VOID
SwLedBlink14(
	IN PLED_USB			pLed
	)
{
	PADAPTER Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;
	static u8	LinkBlinkCnt=0;

	// Change LED according to BlinkingLedState specified.
	if( pLed->BlinkingLedState == RTW_LED_ON )
	{
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else 
	{
		if(pLed->CurrLedState != LED_BLINK_WPS_STOP)
			SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}	
	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("!!! SwLedBlink14 CurrLedState %d, bLedWPSBlinkInProgress %d, bLedBlinkInProgress %d\n", pLed->CurrLedState,pLed->bLedWPSBlinkInProgress,pLed->bLedBlinkInProgress));
	switch(pLed->CurrLedState)
	{			
		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else 
				{
					SwLedOn(Adapter, pLed);
				}
				pLed->bLedBlinkInProgress = _FALSE;	
			}
			else
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
					if( pLed->bLedOn )
					{
						pLed->BlinkingLedState = RTW_LED_OFF;
						if (IS_HARDWARE_TYPE_8812AU(Adapter))
							_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
						else
							_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
					}
					else
					{
						pLed->BlinkingLedState = RTW_LED_ON;
						if (IS_HARDWARE_TYPE_8812AU(Adapter))
							_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
						else
							_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
					}
				}
			}

			break;
					
		default:
			LinkBlinkCnt=0;
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("<==== blink14\n"));
}

VOID
SwLedBlink15(
	IN PLED_USB			pLed
	)
{
	PADAPTER Adapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN bStopBlinking = _FALSE;
	static u8	LinkBlinkCnt=0;
	// Change LED according to BlinkingLedState specified.
	
	if( pLed->BlinkingLedState == RTW_LED_ON ) 
	{
		SwLedOn(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else 
	{
		if(pLed->CurrLedState != LED_BLINK_WPS_STOP)
			SwLedOff(Adapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}	
	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("!!! SwLedBlink15 CurrLedState %d, bLedWPSBlinkInProgress %d, bLedBlinkInProgress %d\n", pLed->CurrLedState,pLed->bLedWPSBlinkInProgress,pLed->bLedBlinkInProgress));
	switch(pLed->CurrLedState)
	{
		case LED_BLINK_WPS:
			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF; 
				_set_timer(&(pLed->BlinkTimer), LED_WPS_BLINK_ON_INTERVAL_DLINK);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed->BlinkTimer), LED_WPS_BLINK_OFF_INTERVAL_DLINK);
			}
			break;
	
		case LED_BLINK_WPS_STOP:	//WPS success
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("blink15, LED_BLINK_WPS_STOP  BlinkingLedState %d\n",pLed->BlinkingLedState));
			
			if(pLed->BlinkingLedState == RTW_LED_OFF)
			{
				pLed->bLedWPSBlinkInProgress = _FALSE;	
				return;
			}
			
			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			pLed->BlinkingLedState = RTW_LED_OFF;	

			_set_timer(&(pLed->BlinkTimer), LED_WPS_BLINK_LINKED_ON_INTERVAL_DLINK);			
			break;

		case LED_BLINK_NO_LINK:
			{
				static BOOLEAN		bLedOn=_TRUE;
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("blink15, LED_NO_LINK_BLINK  bLedOn %d\n",bLedOn));
				if(bLedOn)
				{
					bLedOn=_FALSE;
					pLed->BlinkingLedState = RTW_LED_OFF;
				}
				else
				{
					bLedOn=_TRUE;
					pLed->BlinkingLedState = RTW_LED_ON;
				}
				pLed->bLedBlinkInProgress = _TRUE;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL);
			}
			break;

		case LED_BLINK_LINK_IDEL:
			{
				static BOOLEAN		bLedOn=_TRUE;
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("blink15, LED_BLINK_LINK_IDEL  bLedOn %d\n",bLedOn));
				if(bLedOn)
				{
					bLedOn=_FALSE;
					pLed->BlinkingLedState = RTW_LED_OFF;
				}
				else
				{
					bLedOn=_TRUE;
					pLed->BlinkingLedState = RTW_LED_ON;
					
				}
				pLed->bLedBlinkInProgress = _TRUE;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_IDEL_INTERVAL);
			}	
			break;

		case LED_BLINK_SCAN:
			{
				static u8	BlinkTime=0;
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("blink15, LED_SCAN_BLINK  bLedOn %d\n",BlinkTime));
				if(BlinkTime %2==0)
				{
					pLed->BlinkingLedState = RTW_LED_ON;
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
				}
				BlinkTime ++;

				if(BlinkTime<24)
				{
					pLed->bLedBlinkInProgress = _TRUE;
					
					if(pLed->BlinkingLedState == RTW_LED_ON)
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_OFF_INTERVAL);
					else
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_ON_INTERVAL);
				}
				else
				{
					//if(pLed->OLDLedState ==LED_NO_LINK_BLINK)
					if(check_fwstate(pmlmepriv, _FW_LINKED)== _FALSE)
					{
						pLed->CurrLedState = LED_BLINK_NO_LINK;
						pLed->BlinkingLedState = RTW_LED_ON;	

						_set_timer(&(pLed->BlinkTimer), 100);
					}
					BlinkTime =0;
				}
			}
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if( pLed->BlinkTimes == 0 )
			{
				bStopBlinking = _TRUE;
			}
			if(bStopBlinking)
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
					SwLedOn(Adapter, pLed);
				}
				pLed->bLedBlinkInProgress = _FALSE;
			}
			else
			{
				if( adapter_to_pwrctl(Adapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(Adapter)->rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(Adapter, pLed);
				}
				else
				{
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF; 
					else
						pLed->BlinkingLedState = RTW_LED_ON; 
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		default:
			LinkBlinkCnt=0;
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("<==== blink15\n"));
}

//
//	Description:
//		Handler function of LED Blinking.
//		We dispatch acture LED blink action according to LedStrategy.
//
void BlinkHandler(PLED_USB pLed)
{
	_adapter		*padapter = pLed->padapter;
	struct led_priv	*ledpriv = &(padapter->ledpriv);

	//DBG_871X("%s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);

	if( (padapter->bSurpriseRemoved == _TRUE) || (padapter->hw_init_completed == _FALSE))	
	{
		//DBG_871X("%s bSurpriseRemoved:%d, bDriverStopped:%d\n", __FUNCTION__, padapter->bSurpriseRemoved, padapter->bDriverStopped);
		return;
	}

	switch(ledpriv->LedStrategy)
	{
		case SW_LED_MODE0:
			SwLedBlink(pLed);
			break;
		
		case SW_LED_MODE1:
			SwLedBlink1(pLed);
			break;
		
		case SW_LED_MODE2:
			SwLedBlink2(pLed);
			break;
			
		case SW_LED_MODE3:
			SwLedBlink3(pLed);
			break;

		case SW_LED_MODE4:
			SwLedBlink4(pLed);
			break;			

		case SW_LED_MODE5:
			SwLedBlink5(pLed);
			break;

		case SW_LED_MODE6:
			SwLedBlink6(pLed);
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

		case SW_LED_MODE13:
			SwLedBlink13(pLed);
			break;

		case SW_LED_MODE14:
			SwLedBlink14(pLed);
			break;

		case SW_LED_MODE15:
			SwLedBlink15(pLed);
			break;

		default:
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("BlinkWorkItemCallback 0x%x \n", ledpriv->LedStrategy));
			//SwLedBlink(pLed);
			break;
	}
}

//
//	Description:
//		Callback function of LED BlinkTimer, 
//		it just schedules to corresponding BlinkWorkItem/led_blink_hdl
//
void BlinkTimerCallback(void *data)
{
	PLED_USB	 pLed = (PLED_USB)data;
	_adapter		*padapter = pLed->padapter;

	//DBG_871X("%s\n", __FUNCTION__);

	if( (padapter->bSurpriseRemoved == _TRUE) || (padapter->hw_init_completed == _FALSE))	
	{
		//DBG_871X("%s bSurpriseRemoved:%d, bDriverStopped:%d\n", __FUNCTION__, padapter->bSurpriseRemoved, padapter->bDriverStopped);
		return;
	}

	#ifdef CONFIG_LED_HANDLED_BY_CMD_THREAD
	rtw_led_blink_cmd(padapter, (PVOID)pLed);
	#else
	if(ATOMIC_READ(&pLed->bCancelWorkItem) == _FALSE)
		_set_workitem(&(pLed->BlinkWorkItem));
	#endif
}

//
//	Description:
//		Callback function of LED BlinkWorkItem.
//		We dispatch acture LED blink action according to LedStrategy.
//
void BlinkWorkItemCallback(_workitem *work)
{
	PLED_USB	 pLed = container_of(work, LED_USB, BlinkWorkItem);
	BlinkHandler(pLed);
}

static void
SwLedControlMode0(
	_adapter		*padapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	PLED_USB	pLed = &(ledpriv->SwLed1);

	// Decide led state
	switch(LedAction)
	{
	case LED_CTL_TX:
	case LED_CTL_RX:
		if( pLed->bLedBlinkInProgress == _FALSE )
		{
			pLed->bLedBlinkInProgress = _TRUE;

			pLed->CurrLedState = LED_BLINK_NORMAL;
			pLed->BlinkTimes = 2;

			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
		}
		break;

	case LED_CTL_START_TO_LINK:
		if( pLed->bLedBlinkInProgress == _FALSE )
		{
			pLed->bLedBlinkInProgress = _TRUE;

			pLed->CurrLedState = LED_BLINK_StartToBlink;
			pLed->BlinkTimes = 24;

			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
		}
		else
		{
			pLed->CurrLedState = LED_BLINK_StartToBlink;
		}
		break;

	case LED_CTL_LINK:
		pLed->CurrLedState = RTW_LED_ON;
		if( pLed->bLedBlinkInProgress == _FALSE )
		{
			pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), 0);
		}
		break;

	case LED_CTL_NO_LINK:
		pLed->CurrLedState = RTW_LED_OFF;
		if( pLed->bLedBlinkInProgress == _FALSE )
		{
			pLed->BlinkingLedState = RTW_LED_OFF;
			_set_timer(&(pLed->BlinkTimer), 0);
		}
		break;

	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = RTW_LED_OFF;
		if(pLed->bLedBlinkInProgress)
		{
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		SwLedOff(padapter, pLed);
		break;

	case LED_CTL_START_WPS:
		if( pLed->bLedBlinkInProgress == _FALSE || pLed->CurrLedState == RTW_LED_ON)
		{
			pLed->bLedBlinkInProgress = _TRUE;

			pLed->CurrLedState = LED_BLINK_WPS;
			pLed->BlinkTimes = 20;

			if( pLed->bLedOn )
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LONG_INTERVAL);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LONG_INTERVAL);
			}
		}
		break;

	case LED_CTL_STOP_WPS:
		if(pLed->bLedBlinkInProgress)
		{
			pLed->CurrLedState = RTW_LED_OFF;
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = _FALSE;
		}
		break;


	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led %d\n", pLed->CurrLedState));

}

 //ALPHA, added by chiyoko, 20090106
static void
SwLedControlMode1(
	_adapter		*padapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv		*ledpriv = &(padapter->ledpriv);
	PLED_USB			pLed = &(ledpriv->SwLed0);
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(padapter);

	u32 uLedBlinkNoLinkInterval = LED_BLINK_NO_LINK_INTERVAL_ALPHA; //add by ylb 20121012 for customer led for alpha
	if(pHalData->CustomerID == RT_CID_819x_ALPHA_Dlink)
		uLedBlinkNoLinkInterval= LED_BLINK_NO_LINK_INTERVAL_ALPHA_500MS;	
	
	if(pHalData->CustomerID == RT_CID_819x_CAMEO)
		pLed = &(ledpriv->SwLed1);

	switch(LedAction)
	{
		case LED_CTL_POWER_ON:
		case LED_CTL_START_TO_LINK:
		case LED_CTL_NO_LINK:
			if( pLed->bLedNoLinkBlinkInProgress == _FALSE )
			{
				if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
				if( pLed->bLedLinkBlinkInProgress == _TRUE )
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedLinkBlinkInProgress = _FALSE;
				}
	 			if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
	 			}

				pLed->bLedNoLinkBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), uLedBlinkNoLinkInterval);//change by ylb 20121012 for customer led for alpha
			}
			break;

		case LED_CTL_LINK:
			if( pLed->bLedLinkBlinkInProgress == _FALSE )
			{
				if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
				if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
	 			}
				pLed->bLedLinkBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_SITE_SURVEY:
			 if((pmlmepriv->LinkDetectInfo.bBusyTraffic) && (check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE))
			 	;
			 else if(pLed->bLedScanBlinkInProgress ==_FALSE)
			 {
			 	if(IS_LED_WPS_BLINKING(pLed))
					return;

	  			if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				if( pLed->bLedLinkBlinkInProgress == _TRUE )
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					 pLed->bLedLinkBlinkInProgress = _FALSE;
				}
	 			if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				pLed->bLedScanBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;

				if (adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on && adapter_to_pwrctl(padapter)->rfoff_reason == RF_CHANGE_BY_IPS)
					_set_timer(&(pLed->BlinkTimer), LED_INITIAL_INTERVAL);
				else					
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);

			 }
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if(pLed->bLedBlinkInProgress ==_FALSE)
			{
				if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
				if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				if( pLed->bLedLinkBlinkInProgress == _TRUE )
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedLinkBlinkInProgress = _FALSE;
				}
				pLed->bLedBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:
			 if(pLed->bLedWPSBlinkInProgress ==_FALSE)
			 {
				if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				if( pLed->bLedLinkBlinkInProgress == _TRUE )
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					 pLed->bLedLinkBlinkInProgress = _FALSE;
				}
				if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				if(pLed->bLedScanBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = _FALSE;
				}
				pLed->bLedWPSBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_WPS;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			 }
			break;


		case LED_CTL_STOP_WPS:
			if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedNoLinkBlinkInProgress = _FALSE;
			}
			if( pLed->bLedLinkBlinkInProgress == _TRUE )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				 pLed->bLedLinkBlinkInProgress = _FALSE;
			}
			if(pLed->bLedBlinkInProgress ==_TRUE)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if(pLed->bLedScanBlinkInProgress ==_TRUE)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
			}
			else
			{
				pLed->bLedWPSBlinkInProgress = _TRUE;
			}

			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			if(pLed->bLedOn)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 0);
			}
			break;

		case LED_CTL_STOP_WPS_FAIL:
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			pLed->bLedNoLinkBlinkInProgress = _TRUE;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), uLedBlinkNoLinkInterval);//change by ylb 20121012 for customer led for alpha
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if( pLed->bLedNoLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedNoLinkBlinkInProgress = _FALSE;
			}
			if( pLed->bLedLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedLinkBlinkInProgress = _FALSE;
			}
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}

			SwLedOff(padapter, pLed);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led %d\n", pLed->CurrLedState));
}

 //Arcadyan/Sitecom , added by chiyoko, 20090216
static void
SwLedControlMode2(
	_adapter				*padapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	PLED_USB 		pLed = &(ledpriv->SwLed0);

	switch(LedAction)
	{
		case LED_CTL_SITE_SURVEY:
			 if(pmlmepriv->LinkDetectInfo.bBusyTraffic)
			 	;
			 else if(pLed->bLedScanBlinkInProgress ==_FALSE)
			 {
			 	if(IS_LED_WPS_BLINKING(pLed))
					return;

	 			if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				pLed->bLedScanBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			 }
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
	 		if((pLed->bLedBlinkInProgress ==_FALSE) && (check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE))
	  		{
	  		  	if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}

				pLed->bLedBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_LINK:
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:
			if(pLed->bLedWPSBlinkInProgress ==_FALSE)
			{
				if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				if(pLed->bLedScanBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = _FALSE;
				}
				pLed->bLedWPSBlinkInProgress = _TRUE;
				pLed->CurrLedState = RTW_LED_ON;
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 0);
			 }
			break;

		case LED_CTL_STOP_WPS:
			pLed->bLedWPSBlinkInProgress = _FALSE;
			if(adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on)
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), 0);
			}
			else
			{
				pLed->CurrLedState = RTW_LED_ON;
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 0);
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
			}
			break;

		case LED_CTL_STOP_WPS_FAIL:
			pLed->bLedWPSBlinkInProgress = _FALSE;
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			_set_timer(&(pLed->BlinkTimer), 0);
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
			break;

		case LED_CTL_START_TO_LINK:
		case LED_CTL_NO_LINK:
			if(!IS_LED_BLINKING(pLed))
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), 0);
			}
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			if( pLed->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			SwLedOff(padapter, pLed);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
}

  //COREGA, added by chiyoko, 20090316
 static void
 SwLedControlMode3(
	_adapter				*padapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	PLED_USB		pLed = &(ledpriv->SwLed0);

	switch(LedAction)
	{
		case LED_CTL_SITE_SURVEY:
			if(pmlmepriv->LinkDetectInfo.bBusyTraffic)
				;
			else if(pLed->bLedScanBlinkInProgress ==_FALSE)
			{
				if(IS_LED_WPS_BLINKING(pLed))
					return;

				if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				pLed->bLedScanBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
	 		if((pLed->bLedBlinkInProgress ==_FALSE) && (check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE))
	  		{
	  		  	if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}

				pLed->bLedBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_LINK:
			if(IS_LED_WPS_BLINKING(pLed))
				return;

			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:
			if(pLed->bLedWPSBlinkInProgress ==_FALSE)
			{
				if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				if(pLed->bLedScanBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = _FALSE;
				}
				pLed->bLedWPSBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_WPS;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_STOP_WPS:
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}
			else
			{
				pLed->bLedWPSBlinkInProgress = _TRUE;
			}

			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			if(pLed->bLedOn)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 0);
			}

			break;

		case LED_CTL_STOP_WPS_FAIL:
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_START_TO_LINK:
		case LED_CTL_NO_LINK:
			if(!IS_LED_BLINKING(pLed))
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), 0);
			}
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			if( pLed->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			SwLedOff(padapter, pLed);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
}


 //Edimax-Belkin, added by chiyoko, 20090413
static void
SwLedControlMode4(
	_adapter				*padapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	PLED_USB		pLed = &(ledpriv->SwLed0);
	PLED_USB		pLed1 = &(ledpriv->SwLed1);

	switch(LedAction)
	{
		case LED_CTL_START_TO_LINK:
			if(pLed1->bLedWPSBlinkInProgress)
			{
				pLed1->bLedWPSBlinkInProgress = _FALSE;
				_cancel_timer_ex(&(pLed1->BlinkTimer));

				pLed1->BlinkingLedState = RTW_LED_OFF;
				pLed1->CurrLedState = RTW_LED_OFF;

				if(pLed1->bLedOn)
					_set_timer(&(pLed->BlinkTimer), 0);
			}

			if( pLed->bLedStartToLinkBlinkInProgress == _FALSE )
			{
				if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
	 			if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
	 			}
	 			if(pLed->bLedNoLinkBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
	 			}

				pLed->bLedStartToLinkBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_StartToBlink;
				if( pLed->bLedOn )
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
				}
			}
			break;

		case LED_CTL_LINK:
		case LED_CTL_NO_LINK:
			//LED1 settings
			if(LedAction == LED_CTL_LINK)
			{
				if(pLed1->bLedWPSBlinkInProgress)
				{
					pLed1->bLedWPSBlinkInProgress = _FALSE;
					_cancel_timer_ex(&(pLed1->BlinkTimer));

					pLed1->BlinkingLedState = RTW_LED_OFF;
					pLed1->CurrLedState = RTW_LED_OFF;

					if(pLed1->bLedOn)
						_set_timer(&(pLed->BlinkTimer), 0);
				}
			}

			if( pLed->bLedNoLinkBlinkInProgress == _FALSE )
			{
				if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
	 			if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
	 			}

				pLed->bLedNoLinkBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_SITE_SURVEY:
			if((pmlmepriv->LinkDetectInfo.bBusyTraffic) && (check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE))
				;
			else if(pLed->bLedScanBlinkInProgress ==_FALSE)
			{
				if(IS_LED_WPS_BLINKING(pLed))
					return;

				if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				pLed->bLedScanBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
	 		if(pLed->bLedBlinkInProgress ==_FALSE)
	  		{
	  		  	if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
	  		  	if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				pLed->bLedBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:
			if(pLed1->bLedWPSBlinkInProgress)
			{
				pLed1->bLedWPSBlinkInProgress = _FALSE;
				_cancel_timer_ex(&(pLed1->BlinkTimer));

				pLed1->BlinkingLedState = RTW_LED_OFF;
				pLed1->CurrLedState = RTW_LED_OFF;

				if(pLed1->bLedOn)
					_set_timer(&(pLed->BlinkTimer), 0);
			}

			if(pLed->bLedWPSBlinkInProgress ==_FALSE)
			{
				if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				if(pLed->bLedScanBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = _FALSE;
				}
				pLed->bLedWPSBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_WPS;
				if( pLed->bLedOn )
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
				}
			}
			break;

		case LED_CTL_STOP_WPS:	//WPS connect success
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			pLed->bLedNoLinkBlinkInProgress = _TRUE;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);

			break;

		case LED_CTL_STOP_WPS_FAIL:		//WPS authentication fail
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			pLed->bLedNoLinkBlinkInProgress = _TRUE;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);

			//LED1 settings
			if(pLed1->bLedWPSBlinkInProgress)
				_cancel_timer_ex(&(pLed1->BlinkTimer));
			else
				pLed1->bLedWPSBlinkInProgress = _TRUE;

			pLed1->CurrLedState = LED_BLINK_WPS_STOP;
			if( pLed1->bLedOn )
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);

			break;

		case LED_CTL_STOP_WPS_FAIL_OVERLAP:	//WPS session overlap
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			pLed->bLedNoLinkBlinkInProgress = _TRUE;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);

			//LED1 settings
			if(pLed1->bLedWPSBlinkInProgress)
				_cancel_timer_ex(&(pLed1->BlinkTimer));
			else
				pLed1->bLedWPSBlinkInProgress = _TRUE;

			pLed1->CurrLedState = LED_BLINK_WPS_STOP_OVERLAP;
			pLed1->BlinkTimes = 10;
			if( pLed1->bLedOn )
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);

			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;

			if( pLed->bLedNoLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedNoLinkBlinkInProgress = _FALSE;
			}
			if( pLed->bLedLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedLinkBlinkInProgress = _FALSE;
			}
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			if( pLed->bLedStartToLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedStartToLinkBlinkInProgress = _FALSE;
			}

			if( pLed1->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed1->BlinkTimer));
				pLed1->bLedWPSBlinkInProgress = _FALSE;
			}

			pLed1->BlinkingLedState = LED_UNKNOWN;
			SwLedOff(padapter, pLed);
			SwLedOff(padapter, pLed1);
			break;

		case LED_CTL_CONNECTION_NO_TRANSFER:
			 if(pLed->bLedBlinkInProgress == _FALSE)
			 {
				if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				pLed->bLedBlinkInProgress = _TRUE;

			 	pLed->CurrLedState = LED_BLINK_ALWAYS_ON;
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			}
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led %d\n", pLed->CurrLedState));
}



 //Sercomm-Belkin, added by chiyoko, 20090415
static void
SwLedControlMode5(
	_adapter				*padapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PLED_USB		pLed = &(ledpriv->SwLed0);

	if(pHalData->CustomerID == RT_CID_819x_CAMEO)
		pLed = &(ledpriv->SwLed1);

	switch(LedAction)
	{
		case LED_CTL_POWER_ON:
		case LED_CTL_NO_LINK:
		case LED_CTL_LINK: 	//solid blue
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_SITE_SURVEY:
			if((pmlmepriv->LinkDetectInfo.bBusyTraffic) && (check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE))
				;
			else if(pLed->bLedScanBlinkInProgress ==_FALSE)
			{
				if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				pLed->bLedScanBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
	 		if(pLed->bLedBlinkInProgress ==_FALSE)
	  		{
	  		  	if(pLed->CurrLedState == LED_BLINK_SCAN)
				{
					return;
				}
				pLed->bLedBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;

			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}

			SwLedOff(padapter, pLed);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led %d\n", pLed->CurrLedState));
}

 //WNC-Corega, added by chiyoko, 20090902
static void
SwLedControlMode6(
	_adapter				*padapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	PLED_USB	pLed0 = &(ledpriv->SwLed0);

	switch(LedAction)
	{
		case LED_CTL_POWER_ON:
		case LED_CTL_LINK:
		case LED_CTL_NO_LINK:
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->CurrLedState = RTW_LED_ON;
			pLed0->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed0->BlinkTimer), 0);
			break;

		case LED_CTL_POWER_OFF:
			SwLedOff(padapter, pLed0);
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("ledcontrol 6 Led %d\n", pLed0->CurrLedState));
}

//Netgear, added by sinda, 2011/11/11
 void
 SwLedControlMode7(
	 PADAPTER			 Adapter,
	 LED_CTL_MODE		 LedAction
 )
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	PLED_USB	pLed = &(ledpriv->SwLed0);
	
	switch(LedAction)
	{		
		case LED_CTL_SITE_SURVEY:
			if(pmlmepriv->LinkDetectInfo.bBusyTraffic)
				;
			else if(pLed->bLedScanBlinkInProgress == _FALSE)
			{
				if(IS_LED_WPS_BLINKING(pLed))
					return;

	 			if(pLed->bLedBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				pLed->bLedScanBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 6;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_NETGEAR);
			}
			break;

		case LED_CTL_LINK:
			if(IS_LED_WPS_BLINKING(pLed))
				return;

			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}			

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:
			if(pLed->bLedWPSBlinkInProgress ==_FALSE)
			{
				if(pLed->bLedBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				if(pLed->bLedScanBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = _FALSE;
				}
				pLed->bLedWPSBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_WPS;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_NETGEAR);
			}	
			break;

		case LED_CTL_STOP_WPS:
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;				
			}						
			else
			{
				pLed->bLedWPSBlinkInProgress = _TRUE;
			}

			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			if(pLed->bLedOn)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_NETGEAR);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 0);
			}					

			break;


		case LED_CTL_STOP_WPS_FAIL:			
		case LED_CTL_STOP_WPS_FAIL_OVERLAP:	//WPS session overlap			
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_START_TO_LINK:
		case LED_CTL_NO_LINK:
			if(!IS_LED_BLINKING(pLed))
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;		
				_set_timer(&(pLed->BlinkTimer), 0);
			}
			break;

		case LED_CTL_POWER_OFF:
		case LED_CTL_POWER_ON:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}
			if( pLed->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("LEd control mode 7 CurrLedState %d\n", pLed->CurrLedState));
}

void
SwLedControlMode8(
	PADAPTER			Adapter,
	LED_CTL_MODE		LedAction
	)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	PLED_USB	pLed0 = &(ledpriv->SwLed0);

	switch(LedAction)
	{
		case LED_CTL_LINK:
			_cancel_timer_ex(&(pLed0->BlinkTimer));			
			pLed0->CurrLedState = RTW_LED_ON;
			pLed0->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed0->BlinkTimer), 0);
			break;

		case LED_CTL_NO_LINK:
			_cancel_timer_ex(&(pLed0->BlinkTimer));
			pLed0->CurrLedState = RTW_LED_OFF;
			pLed0->BlinkingLedState = RTW_LED_OFF;				
			_set_timer(&(pLed0->BlinkTimer), 0);	
			break;

		case LED_CTL_POWER_OFF:
			SwLedOff(Adapter, pLed0);
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 8 %d\n", pLed0->CurrLedState));

}

//page added for Belkin AC950, 20120813
void
SwLedControlMode9(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	PLED_USB	pLed = &(ledpriv->SwLed0);
	PLED_USB	pLed1 = &(ledpriv->SwLed1);
	PLED_USB	pLed2 = &(ledpriv->SwLed2);
	BOOLEAN  bWPSOverLap = _FALSE;
	//DBG_871X("LedAction=%d \n", LedAction);
	switch(LedAction)
	{		
		case LED_CTL_START_TO_LINK:	
			if(pLed2->bLedBlinkInProgress == _FALSE)
			{
				pLed2->bLedBlinkInProgress = _TRUE;
				pLed2->BlinkingLedState = RTW_LED_ON;
				pLed2->CurrLedState = LED_BLINK_LINK_IN_PROCESS;

				_set_timer(&(pLed2->BlinkTimer), 0);	
			}
			break;		

		case LED_CTL_LINK:			
		case LED_CTL_NO_LINK:
			//LED1 settings
			if(LedAction == LED_CTL_NO_LINK)
			{
				//if(pMgntInfo->AuthStatus == AUTH_STATUS_FAILED)
				if(0)
				{
					pLed1->CurrLedState = LED_BLINK_AUTH_ERROR;
						if( pLed1->bLedOn )
							pLed1->BlinkingLedState = RTW_LED_OFF; 
						else
							pLed1->BlinkingLedState = RTW_LED_ON; 
						_set_timer(&(pLed1->BlinkTimer), 0);
				} 
				else
				{
					pLed1->CurrLedState = RTW_LED_OFF;
					pLed1->BlinkingLedState = RTW_LED_OFF; 
					if( pLed1->bLedOn )
						_set_timer(&(pLed1->BlinkTimer), 0);
				}
			}
			else
			{
				pLed1->CurrLedState = RTW_LED_OFF;
				pLed1->BlinkingLedState = RTW_LED_OFF; 
				if( pLed1->bLedOn )
					_set_timer(&(pLed1->BlinkTimer), 0); 
			}
						
			//LED2 settings
			if(LedAction == LED_CTL_LINK)
			{
				if(Adapter->securitypriv.dot11PrivacyAlgrthm != _NO_PRIVACY_)
				{
					if(pLed2->bLedBlinkInProgress ==_TRUE)
					{	
						_cancel_timer_ex(&(pLed2->BlinkTimer));
						pLed2->bLedBlinkInProgress = _FALSE;
		 			}
					pLed2->CurrLedState = RTW_LED_ON;
					pLed2->bLedNoLinkBlinkInProgress = _TRUE;
					if(!pLed2->bLedOn)
						_set_timer(&(pLed2->BlinkTimer), 0); 	
				}
				else
				{
					if(pLed2->bLedWPSBlinkInProgress != _TRUE)
					{
						pLed2->CurrLedState = RTW_LED_OFF;
						pLed2->BlinkingLedState = RTW_LED_OFF;
						if(pLed2->bLedOn)
							_set_timer(&(pLed2->BlinkTimer), 0);
					}
				}
			}
			else //NO_LINK
			{
				if(pLed2->bLedWPSBlinkInProgress == _FALSE)
				{
					pLed2->CurrLedState = RTW_LED_OFF;
					pLed2->BlinkingLedState = RTW_LED_OFF;
					if(pLed2->bLedOn)
						_set_timer(&(pLed2->BlinkTimer), 0); 		
				}
			}

			//LED0 settings			
			if( pLed->bLedNoLinkBlinkInProgress == _FALSE )
			{
				if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
	 			if(pLed->bLedBlinkInProgress == _TRUE)
				{	
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
	 			}
				
				pLed->bLedNoLinkBlinkInProgress = _TRUE;
				if(IS_HARDWARE_TYPE_8812AU(Adapter))
				{
					if(LedAction == LED_CTL_LINK)
					{
						pLed->BlinkingLedState = RTW_LED_ON; 
						pLed->CurrLedState = LED_BLINK_SLOWLY;
					}
					else
					{
						pLed->CurrLedState = LED_BLINK_SLOWLY;
						if( pLed->bLedOn )
							pLed->BlinkingLedState = RTW_LED_OFF; 
						else
							pLed->BlinkingLedState = RTW_LED_ON; 
					}
				}
				else
				{
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if( pLed->bLedOn )
						pLed->BlinkingLedState = RTW_LED_OFF; 
					else
						pLed->BlinkingLedState = RTW_LED_ON; 
				}
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			}
			
			break;		

		case LED_CTL_SITE_SURVEY:
			 if((pmlmepriv->LinkDetectInfo.bBusyTraffic) && (check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE))
			 	;
			 else //if(pLed->bLedScanBlinkInProgress ==FALSE)
			 {
				if(IS_LED_WPS_BLINKING(pLed))
					return;
				
	  			if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
	 			if(pLed->bLedBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				pLed->bLedScanBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF; 
				else
					pLed->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);

			 }
			break;
		
		case LED_CTL_TX:
		case LED_CTL_RX:
	 		if(pLed->bLedBlinkInProgress == _FALSE)
	  		{
	  		  	if(pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
	  		  	if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				pLed->bLedBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF; 
				else
					pLed->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:
			pLed2->bLedBlinkInProgress = _TRUE;
			pLed2->BlinkingLedState = RTW_LED_ON;
			pLed2->CurrLedState = LED_BLINK_LINK_IN_PROCESS;
			pLed2->bLedWPSBlinkInProgress = _TRUE;

			_set_timer(&(pLed2->BlinkTimer), 500);
			 
			break;
		
		case LED_CTL_STOP_WPS:	//WPS connect success	
			//LED2 settings
			if(pLed2->bLedWPSBlinkInProgress == _TRUE)
			{	
				_cancel_timer_ex(&(pLed2->BlinkTimer));
				pLed2->bLedBlinkInProgress = _FALSE;
				pLed2->bLedWPSBlinkInProgress = _FALSE;
 			}
			pLed2->CurrLedState = RTW_LED_ON;
			pLed2->bLedNoLinkBlinkInProgress = _TRUE;
			if(!pLed2->bLedOn)
				_set_timer(&(pLed2->BlinkTimer), 0); 	

			//LED1 settings
			_cancel_timer_ex(&(pLed1->BlinkTimer));
			pLed1->CurrLedState = RTW_LED_OFF;
			pLed1->BlinkingLedState = RTW_LED_OFF; 
			if( pLed1->bLedOn )
				_set_timer(&(pLed1->BlinkTimer), 0);	
			
				
			break;		

		case LED_CTL_STOP_WPS_FAIL:		//WPS authentication fail	
			//LED1 settings
			//if(bWPSOverLap == _FALSE)
			{
				pLed1->CurrLedState = LED_BLINK_AUTH_ERROR;
				pLed1->BlinkTimes = 50;
				if( pLed1->bLedOn )
					pLed1->BlinkingLedState = RTW_LED_OFF; 
				else
					pLed1->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed1->BlinkTimer), 0);
			}
			//else
			//{
			//	bWPSOverLap = _FALSE;
			//	pLed1->CurrLedState = RTW_LED_OFF;
			//	pLed1->BlinkingLedState = RTW_LED_OFF; 
			//	_set_timer(&(pLed1->BlinkTimer), 0);
			//}

			//LED2 settings
			pLed2->CurrLedState = RTW_LED_OFF;
			pLed2->BlinkingLedState = RTW_LED_OFF; 
			pLed2->bLedWPSBlinkInProgress = _FALSE;
			if( pLed2->bLedOn )
				_set_timer(&(pLed2->BlinkTimer), 0);
						
			break;				

		case LED_CTL_STOP_WPS_FAIL_OVERLAP:	//WPS session overlap
			//LED1 settings
			bWPSOverLap = _TRUE;
			pLed1->CurrLedState = LED_BLINK_WPS_STOP_OVERLAP;
			pLed1->BlinkTimes = 10;
			pLed1->BlinkCounter = 50;
			if( pLed1->bLedOn )
				pLed1->BlinkingLedState = RTW_LED_OFF; 
			else
				pLed1->BlinkingLedState = RTW_LED_ON; 
			_set_timer(&(pLed1->BlinkTimer), 0);

			//LED2 settings
			pLed2->CurrLedState = RTW_LED_OFF;
			pLed2->BlinkingLedState = RTW_LED_OFF;
			pLed2->bLedWPSBlinkInProgress = _FALSE;
			if( pLed2->bLedOn )
				_set_timer(&(pLed2->BlinkTimer), 0);
			
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF; 
			
			if( pLed->bLedNoLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedNoLinkBlinkInProgress = _FALSE;
			}
			if( pLed->bLedLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedLinkBlinkInProgress = _FALSE;
			}
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}	
			if( pLed->bLedStartToLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedStartToLinkBlinkInProgress = _FALSE;
			}			

			if( pLed1->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed1->BlinkTimer));
				pLed1->bLedWPSBlinkInProgress = _FALSE;
			}


			pLed1->BlinkingLedState = LED_UNKNOWN;				
			SwLedOff(Adapter, pLed);
			SwLedOff(Adapter, pLed1);			
			break;

		case LED_CTL_CONNECTION_NO_TRANSFER:
			 if(pLed->bLedBlinkInProgress == _FALSE)
			 {
			 	  if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				pLed->bLedBlinkInProgress = _TRUE;

			 	pLed->CurrLedState = LED_BLINK_ALWAYS_ON;
				pLed->BlinkingLedState = RTW_LED_ON; 
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			 }
			break;
	
		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 9 Led %d\n", pLed->CurrLedState));
}

//page added for Netgear A6200V2, 20120827
void
SwLedControlMode10(
	PADAPTER			Adapter,
	LED_CTL_MODE		LedAction
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	PLED_USB	pLed = &(ledpriv->SwLed0);
	PLED_USB	pLed1 = &(ledpriv->SwLed1);

	switch(LedAction)
	{
		case LED_CTL_START_TO_LINK:
			if(pLed1->bLedBlinkInProgress == _FALSE)
			{
				pLed1->bLedBlinkInProgress = _TRUE;
				pLed1->BlinkingLedState = RTW_LED_ON;
				pLed1->CurrLedState = LED_BLINK_LINK_IN_PROCESS;

				_set_timer(&(pLed1->BlinkTimer), 0);
			}
			break;

		case LED_CTL_LINK:			
		case LED_CTL_NO_LINK:
			if(LedAction == LED_CTL_LINK)
			{
				if(pLed->bLedWPSBlinkInProgress == _TRUE || pLed1->bLedWPSBlinkInProgress == _TRUE)
					;
				else
				{
					if(pHalData->CurrentBandType == BAND_ON_2_4G)
					//LED0 settings
					{
						pLed->CurrLedState = RTW_LED_ON;
						pLed->BlinkingLedState = RTW_LED_ON;
						if(pLed->bLedBlinkInProgress == _TRUE)
						{	
							_cancel_timer_ex(&(pLed->BlinkTimer));
							pLed->bLedBlinkInProgress = _FALSE;
			 			}
						_set_timer(&(pLed->BlinkTimer), 0);

						pLed1->CurrLedState = RTW_LED_OFF;
						pLed1->BlinkingLedState = RTW_LED_OFF;
						_set_timer(&(pLed1->BlinkTimer), 0);
					}
					else if(pHalData->CurrentBandType == BAND_ON_5G)
					//LED1 settings
					{
						pLed1->CurrLedState = RTW_LED_ON;
						pLed1->BlinkingLedState = RTW_LED_ON;
						if(pLed1->bLedBlinkInProgress == _TRUE)
						{	
							_cancel_timer_ex(&(pLed1->BlinkTimer));
							pLed1->bLedBlinkInProgress = _FALSE;
			 			}
						_set_timer(&(pLed1->BlinkTimer), 0);

						pLed->CurrLedState = RTW_LED_OFF;
						pLed->BlinkingLedState = RTW_LED_OFF;
						_set_timer(&(pLed->BlinkTimer), 0);
					}
				}
			}
			else if(LedAction == LED_CTL_NO_LINK)   //TODO by page
			{
				if(pLed->bLedWPSBlinkInProgress == _TRUE || pLed1->bLedWPSBlinkInProgress == _TRUE)
					;
				else
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if( pLed->bLedOn )
						_set_timer(&(pLed->BlinkTimer), 0);

					pLed1->CurrLedState = RTW_LED_OFF;
					pLed1->BlinkingLedState = RTW_LED_OFF;
					if( pLed1->bLedOn )
						_set_timer(&(pLed1->BlinkTimer), 0);
				}
			}

			break;		

		case LED_CTL_SITE_SURVEY:
			 if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
			 	;                                                                  //don't blink when media connect
			 else //if(pLed->bLedScanBlinkInProgress ==FALSE)
			 {
				if(IS_LED_WPS_BLINKING(pLed) || IS_LED_WPS_BLINKING(pLed1))
					return;

	  			if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
	 			if(pLed->bLedBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				pLed->bLedScanBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 12;
				pLed->BlinkingLedState = LED_BLINK_SCAN;
				_set_timer(&(pLed->BlinkTimer), 0);

				if(pLed1->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed1->BlinkTimer));
					pLed1->bLedNoLinkBlinkInProgress = _FALSE;
				}
	 			if(pLed1->bLedBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed1->BlinkTimer));
					pLed1->bLedBlinkInProgress = _FALSE;
				}
				pLed1->bLedScanBlinkInProgress = _TRUE;
				pLed1->CurrLedState = LED_BLINK_SCAN;
				pLed1->BlinkTimes = 12;
				pLed1->BlinkingLedState = LED_BLINK_SCAN;
				_set_timer(&(pLed1->BlinkTimer), LED_BLINK_LINK_SLOWLY_INTERVAL_NETGEAR);

			}
			break;

		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:
			//LED0 settings
			if(pLed->bLedBlinkInProgress == _FALSE)
			{
				pLed->bLedBlinkInProgress = _TRUE;
				pLed->bLedWPSBlinkInProgress = _TRUE;
				pLed->BlinkingLedState = LED_BLINK_WPS;
				pLed->CurrLedState = LED_BLINK_WPS;
				_set_timer(&(pLed->BlinkTimer), 0);
			}

			//LED1 settings
			if(pLed1->bLedBlinkInProgress == _FALSE)
			{
				pLed1->bLedBlinkInProgress = _TRUE;
				pLed1->bLedWPSBlinkInProgress = _TRUE;
				pLed1->BlinkingLedState = LED_BLINK_WPS;
				pLed1->CurrLedState = LED_BLINK_WPS;
				_set_timer(&(pLed1->BlinkTimer), LED_BLINK_NORMAL_INTERVAL+LED_BLINK_LINK_INTERVAL_NETGEAR);
			}


			break;

		case LED_CTL_STOP_WPS:	//WPS connect success
			if(pHalData->CurrentBandType == BAND_ON_2_4G)
			//LED0 settings
			{
				pLed->bLedWPSBlinkInProgress = _FALSE;
				pLed->CurrLedState = RTW_LED_ON;
				pLed->BlinkingLedState = RTW_LED_ON;
				if(pLed->bLedBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
	 			}
				_set_timer(&(pLed->BlinkTimer), 0);

				pLed1->CurrLedState = RTW_LED_OFF;
				pLed1->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed1->BlinkTimer), 0);
			}
			else if(pHalData->CurrentBandType == BAND_ON_5G)
			//LED1 settings
			{
				pLed1->bLedWPSBlinkInProgress = _FALSE;
				pLed1->CurrLedState = RTW_LED_ON;
				pLed1->BlinkingLedState = RTW_LED_ON;
				if(pLed1->bLedBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed1->BlinkTimer));
					pLed1->bLedBlinkInProgress = _FALSE;
	 			}
				_set_timer(&(pLed1->BlinkTimer), 0);

				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), 0);
			}

			break;

		case LED_CTL_STOP_WPS_FAIL:		//WPS authentication fail	
			//LED1 settings
			pLed1->bLedWPSBlinkInProgress = _FALSE;
			pLed1->CurrLedState = RTW_LED_OFF;
			pLed1->BlinkingLedState = RTW_LED_OFF; 
			_set_timer(&(pLed1->BlinkTimer), 0);

			//LED0 settings
			pLed->bLedWPSBlinkInProgress = _FALSE;
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if( pLed->bLedOn )
				_set_timer(&(pLed->BlinkTimer), 0);

			break;


		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 10 Led %d\n", pLed->CurrLedState));
}

 //Edimax-ASUS, added by Page, 20121221
void
SwLedControlMode11(
	PADAPTER			Adapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	PLED_USB	pLed = &(ledpriv->SwLed0);

	switch(LedAction)
	{	
		case LED_CTL_POWER_ON:
		case LED_CTL_START_TO_LINK:	
		case LED_CTL_NO_LINK:
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_LINK:
			if( pLed->bLedBlinkInProgress == _TRUE )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			pLed->bLedBlinkInProgress = _TRUE;
			pLed->CurrLedState = LED_BLINK_TXRX;
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF; 
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			break;

		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:
			if(pLed->bLedBlinkInProgress == _TRUE)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			pLed->bLedWPSBlinkInProgress = _TRUE;
			pLed->bLedBlinkInProgress = _TRUE;
			pLed->CurrLedState = LED_BLINK_WPS;
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			pLed->BlinkTimes = 5;
			_set_timer(&(pLed->BlinkTimer), 0);

			break;


		case LED_CTL_STOP_WPS:
		case LED_CTL_STOP_WPS_FAIL:
			if(pLed->bLedBlinkInProgress == _TRUE)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;

			if( pLed->bLedNoLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedNoLinkBlinkInProgress = _FALSE;
			}
			if( pLed->bLedLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedLinkBlinkInProgress = _FALSE;
			}
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}

			SwLedOff(Adapter, pLed);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led mode 1 CurrLedState %d\n", pLed->CurrLedState));
}

// page added for NEC

VOID
SwLedControlMode12(
	PADAPTER			Adapter,
	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	PLED_USB	pLed = &(ledpriv->SwLed0);

	switch(LedAction)
	{
		case LED_CTL_POWER_ON:
		case LED_CTL_NO_LINK:
		case LED_CTL_LINK:
		case LED_CTL_SITE_SURVEY:

			if( pLed->bLedNoLinkBlinkInProgress == _FALSE )
			{
	 			if(pLed->bLedBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
	 			}

				pLed->bLedNoLinkBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
	 		if(pLed->bLedBlinkInProgress == _FALSE)
	  		{
	 			if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = _FALSE;
				}
				pLed->bLedBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if( pLed->bLedOn )
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;

			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}

			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}

 			if(pLed->bLedNoLinkBlinkInProgress == _TRUE)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedNoLinkBlinkInProgress = _FALSE;
			}

			SwLedOff(Adapter, pLed);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SWLed12 %d\n", pLed->CurrLedState));
}

// Maddest add for NETGEAR R6100

VOID
SwLedControlMode13(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	PLED_USB	pLed = &(ledpriv->SwLed0);

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 13 CurrLedState %d, LedAction %d\n", pLed->CurrLedState,LedAction));
	switch(LedAction)
	{		
		case LED_CTL_LINK:
			if(pLed->bLedWPSBlinkInProgress)
			{
				return;	
			}

			
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}			

			_set_timer(&(pLed->BlinkTimer), 0);
			break;			

		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:	
			if(pLed->bLedWPSBlinkInProgress == _FALSE)
			{
				if(pLed->bLedBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				if(pLed->bLedScanBlinkInProgress == _TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = _FALSE;
				}				
				pLed->bLedWPSBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_WPS;
				if( pLed->bLedOn )
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					_set_timer(&(pLed->BlinkTimer), LED_WPS_BLINK_OFF_INTERVAL_NETGEAR);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON; 
					_set_timer(&(pLed->BlinkTimer), LED_WPS_BLINK_ON_INTERVAL_NETGEAR);
				}
			}	
			break;
			
		case LED_CTL_STOP_WPS:	
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));							
				pLed->bLedWPSBlinkInProgress = _FALSE;				
			}						
			else
			{
				pLed->bLedWPSBlinkInProgress = _TRUE;
			}
			
			pLed->bLedWPSBlinkInProgress = _FALSE;		
			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			if(pLed->bLedOn)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;			
					
				_set_timer(&(pLed->BlinkTimer), 0);
			}					

			break;		

			
		case LED_CTL_STOP_WPS_FAIL: 		
		case LED_CTL_STOP_WPS_FAIL_OVERLAP: //WPS session overlap
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));							
				pLed->bLedWPSBlinkInProgress = _FALSE;				
			}			

			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;				
			_set_timer(&(pLed->BlinkTimer), 0);	
			break;				

		case LED_CTL_START_TO_LINK:	
			if((pLed->bLedBlinkInProgress == _FALSE) && (pLed->bLedWPSBlinkInProgress == _FALSE)) 
			{
				pLed->bLedBlinkInProgress = _TRUE;
				pLed->BlinkingLedState = RTW_LED_ON;
				pLed->CurrLedState = LED_BLINK_LINK_IN_PROCESS;

				_set_timer(&(pLed->BlinkTimer), 0);
			}
			break;	

		case LED_CTL_NO_LINK:

			if(pLed->bLedWPSBlinkInProgress)
			{
				return;	
			}
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}			
			//if(!IS_LED_BLINKING(pLed))
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;				
				_set_timer(&(pLed->BlinkTimer), 0);				
			}
			break;
			
		case LED_CTL_POWER_OFF:
		case LED_CTL_POWER_ON:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}			
			if( pLed->bLedWPSBlinkInProgress )
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}

			if (LedAction == LED_CTL_POWER_ON)
				_set_timer(&(pLed->BlinkTimer), 0);
			else
				SwLedOff(Adapter, pLed);
			break;
			
		default:
			break;

	}

	
}

// Maddest add for DNI Buffalo

VOID
SwLedControlMode14(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	PLED_USB	pLed = &(ledpriv->SwLed0);

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 14 CurrLedState %d, LedAction %d\n", pLed->CurrLedState,LedAction));
	switch(LedAction)
	{				
		case LED_CTL_POWER_OFF:
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 14 LED_CTL_POWER_OFF\n"));
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}			
			SwLedOff(Adapter, pLed);	
			break;

		case LED_CTL_POWER_ON:
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 14 LED_CTL_POWER_ON\n"));
			SwLedOn(Adapter, pLed);
			break;

		case LED_CTL_LINK:
		case LED_CTL_NO_LINK:
			if (IS_HARDWARE_TYPE_8812AU(Adapter))
				SwLedOn(Adapter, pLed);
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
	 		if(pLed->bLedBlinkInProgress ==_FALSE)
	  		{
				pLed->bLedBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if( pLed->bLedOn )
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					if (IS_HARDWARE_TYPE_8812AU(Adapter))
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
					else
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON;
					if (IS_HARDWARE_TYPE_8812AU(Adapter))
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
					else
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		default:
			break;
	}
}

// Maddest add for Dlink

VOID
SwLedControlMode15(
	IN	PADAPTER			Adapter,
	IN	LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(Adapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	PLED_USB	pLed = &(ledpriv->SwLed0);

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 15 CurrLedState %d, LedAction %d\n", pLed->CurrLedState,LedAction));
	switch(LedAction)
	{		
		case LED_CTL_START_WPS: //wait until xinpin finish
		case LED_CTL_START_WPS_BOTTON:	
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 15 LED_CTL_START_WPS\n"));
			if(pLed->bLedWPSBlinkInProgress ==_FALSE)
			{
				if(pLed->bLedBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = _FALSE;
				}
				if(pLed->bLedScanBlinkInProgress ==_TRUE)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = _FALSE;
				}				
				pLed->bLedWPSBlinkInProgress = _TRUE;
				pLed->CurrLedState = LED_BLINK_WPS;
				if( pLed->bLedOn )
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					_set_timer(&(pLed->BlinkTimer), LED_WPS_BLINK_OFF_INTERVAL_NETGEAR);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON; 
					_set_timer(&(pLed->BlinkTimer), LED_WPS_BLINK_ON_INTERVAL_NETGEAR);
				}
			}	
			break;
			
		case LED_CTL_STOP_WPS:	
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 15 LED_CTL_STOP_WPS\n"));
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));										
			}						
			
			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			//if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
			{
				pLed->BlinkingLedState = RTW_LED_ON;			
					
				_set_timer(&(pLed->BlinkTimer), 0);
			}					

			break;

		case LED_CTL_STOP_WPS_FAIL: 		
		case LED_CTL_STOP_WPS_FAIL_OVERLAP: //WPS session overlap
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 15 LED_CTL_STOP_WPS_FAIL\n"));
			if(pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = _FALSE;
			}			

			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_NO_LINK:
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 15 LED_CTL_NO_LINK\n"));
			if(pLed->bLedWPSBlinkInProgress)
			{
				return; 
			}

			/*if(Adapter->securitypriv.dot11PrivacyAlgrthm > _NO_PRIVACY_)
			{
				if(SecIsTxKeyInstalled(Adapter, pMgntInfo->Bssid))
				{
				}
				else
				{
					if(pMgntInfo->LEDAssocState ==LED_ASSOC_SECURITY_BEGIN)
						return; 
				}
			}*/

			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			if( pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = _FALSE;
			}			
			//if(!IS_LED_BLINKING(pLed))
			{
				pLed->CurrLedState = LED_BLINK_NO_LINK;
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 30);
			}
			break;

		case LED_CTL_LINK: 
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led control mode 15 LED_CTL_LINK\n"));

			if(pLed->bLedWPSBlinkInProgress)
			{
				return; 
			}

			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}

			pLed->CurrLedState = LED_BLINK_LINK_IDEL;
			pLed->BlinkingLedState = RTW_LED_ON;

			_set_timer(&(pLed->BlinkTimer), 30);
			break;	

		case LED_CTL_SITE_SURVEY	:
			if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				return;

			if(pLed->bLedWPSBlinkInProgress ==_TRUE)
				return;
			
			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}
			pLed->CurrLedState = LED_BLINK_SCAN;
			pLed->BlinkingLedState = RTW_LED_ON;				
			_set_timer(&(pLed->BlinkTimer), 0);	
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if(pLed->bLedWPSBlinkInProgress)
			{
				return; 
			}

			if( pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = _FALSE;
			}			

			pLed->bLedBlinkInProgress = _TRUE;
			pLed->CurrLedState = LED_BLINK_TXRX;
			pLed->BlinkTimes = 2;
			if( pLed->bLedOn )
				pLed->BlinkingLedState = RTW_LED_OFF; 
			else
				pLed->BlinkingLedState = RTW_LED_ON; 
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			break;			

		default:
			break;
	}
}

void
LedControlUSB(
	_adapter				*padapter,
	LED_CTL_MODE		LedAction
	)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);

#if(MP_DRIVER == 1)
	if (padapter->registrypriv.mp_mode == 1)
		return;
#endif

       if( (padapter->bSurpriseRemoved == _TRUE) ||(padapter->hw_init_completed == _FALSE) )
       {
             return;
       }

	if( ledpriv->bRegUseLed == _FALSE)
		return;

	//if(priv->bInHctTest)
	//	return;

#ifdef CONFIG_CONCURRENT_MODE
	// Only do led action for PRIMARY_ADAPTER
	if (padapter->adapter_type != PRIMARY_ADAPTER)
		return;
#endif

	if( (adapter_to_pwrctl(padapter)->rf_pwrstate != rf_on &&
		adapter_to_pwrctl(padapter)->rfoff_reason > RF_CHANGE_BY_PS) &&
		(LedAction == LED_CTL_TX || LedAction == LED_CTL_RX ||
		 LedAction == LED_CTL_SITE_SURVEY ||
		 LedAction == LED_CTL_LINK ||
		 LedAction == LED_CTL_NO_LINK ||
		 LedAction == LED_CTL_POWER_ON) )
	{
		return;
	}

	switch(ledpriv->LedStrategy)
	{
		case SW_LED_MODE0:
			SwLedControlMode0(padapter, LedAction);
			break;

		case SW_LED_MODE1:
			SwLedControlMode1(padapter, LedAction);
			break;

		case SW_LED_MODE2:
			SwLedControlMode2(padapter, LedAction);
                	break;

		case SW_LED_MODE3:
			SwLedControlMode3(padapter, LedAction);
			break;

		case SW_LED_MODE4:
			SwLedControlMode4(padapter, LedAction);
			break;

		case SW_LED_MODE5:
			SwLedControlMode5(padapter, LedAction);
			break;

		case SW_LED_MODE6:
			SwLedControlMode6(padapter, LedAction);
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

		case SW_LED_MODE13:
			SwLedControlMode13(padapter, LedAction);
			break;

		case SW_LED_MODE14:
			SwLedControlMode14(padapter, LedAction);
			break;

		case SW_LED_MODE15:
			SwLedControlMode15(padapter, LedAction);
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("LedStrategy:%d, LedAction %d\n", ledpriv->LedStrategy,LedAction));
}

//
//	Description:
//		Reset status of LED_871x object.
//
void ResetLedStatus(PLED_USB pLed) {

	pLed->CurrLedState = RTW_LED_OFF; // Current LED state.
	pLed->bLedOn = _FALSE; // true if LED is ON, false if LED is OFF.

	pLed->bLedBlinkInProgress = _FALSE; // true if it is blinking, false o.w..
	pLed->bLedWPSBlinkInProgress = _FALSE;
	
	pLed->BlinkTimes = 0; // Number of times to toggle led state for blinking.
	pLed->BlinkCounter = 0;
	pLed->BlinkingLedState = LED_UNKNOWN; // Next state for blinking, either RTW_LED_ON or RTW_LED_OFF are.

	pLed->bLedNoLinkBlinkInProgress = _FALSE;
	pLed->bLedLinkBlinkInProgress = _FALSE;
	pLed->bLedStartToLinkBlinkInProgress = _FALSE;
	pLed->bLedScanBlinkInProgress = _FALSE;
}

 //
//	Description:
//		Initialize an LED_871x object.
//
void
InitLed(
	_adapter			*padapter,
	PLED_USB		pLed,
	LED_PIN			LedPin
	)
{
	pLed->padapter = padapter;
	pLed->LedPin = LedPin;

	ResetLedStatus(pLed);
	ATOMIC_SET(&pLed->bCancelWorkItem, _FALSE);
	_init_timer(&(pLed->BlinkTimer), padapter->pnetdev, BlinkTimerCallback, pLed);
	_init_workitem(&(pLed->BlinkWorkItem), BlinkWorkItemCallback, pLed);
}


//
//	Description:
//		DeInitialize an LED_871x object.
//
void
DeInitLed(
	PLED_USB		pLed
	)
{
	ATOMIC_SET(&pLed->bCancelWorkItem, _TRUE);
	_cancel_workitem_sync(&(pLed->BlinkWorkItem));
	_cancel_timer_ex(&(pLed->BlinkTimer));
	ResetLedStatus(pLed);
}


