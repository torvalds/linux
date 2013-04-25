/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#include	"rt_config.h"

#ifdef LED_CONTROL_SUPPORT



/*
	========================================================================
	
	Routine Description:
		Set LED Status

	Arguments:
		pAd						Pointer to our adapter
		Status					LED Status

	Return Value:
		None

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	Note:
	
	========================================================================
*/
VOID RTMPSetLEDStatus(
	IN PRTMP_ADAPTER 	pAd, 
	IN UCHAR			Status)
{
	/*ULONG			data; */
	UCHAR			LinkStatus = 0;
	UCHAR			LedMode;
	UCHAR			MCUCmd = 0;
	BOOLEAN 		bIgnored = FALSE;

#ifdef RALINK_ATE
	/*
		In ATE mode of RT2860 AP/STA, we have erased 8051 firmware.
		So LED mode is not supported when ATE is running.
	*/
	if (!IS_RT3572(pAd))
	{
		if (ATE_ON(pAd))
			return;
	}
#endif /* RALINK_ATE */

#ifdef RTMP_MAC_USB
#ifdef STATS_COUNT_SUPPORT
	if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
		return;
#endif /* STATS_COUNT_SUPPORT */
#endif /* RTMP_MAC_USB */


	LedMode = LED_MODE(pAd);
	switch (Status)
	{
		case LED_LINK_DOWN:
			LinkStatus = LINK_STATUS_LINK_DOWN;
			pAd->LedCntl.LedIndicatorStrength = 0;
			MCUCmd = MCU_SET_LED_MODE;
			break;
		case LED_LINK_UP:
			if (pAd->CommonCfg.Channel > 14)
				LinkStatus = LINK_STATUS_ABAND_LINK_UP;
			else
				LinkStatus = LINK_STATUS_GBAND_LINK_UP;

			MCUCmd = MCU_SET_LED_MODE;
			break;
		case LED_RADIO_ON:
			LinkStatus = LINK_STATUS_RADIO_ON;
			MCUCmd = MCU_SET_LED_MODE;
			break;
		case LED_HALT: 
			LedMode = 0; /* Driver sets MAC register and MAC controls LED */
		case LED_RADIO_OFF:
			LinkStatus = LINK_STATUS_RADIO_OFF;
			MCUCmd = MCU_SET_LED_MODE;
			break;
		case LED_WPS:
			LinkStatus = LINK_STATUS_WPS;
			MCUCmd = MCU_SET_LED_MODE;
			break;
		case LED_ON_SITE_SURVEY:
			LinkStatus = LINK_STATUS_ON_SITE_SURVEY;
			MCUCmd = MCU_SET_LED_MODE;
			break;
		case LED_POWER_UP:
			LinkStatus = LINK_STATUS_POWER_UP;
			MCUCmd = MCU_SET_LED_MODE;
			break;
#ifdef RALINK_ATE
#endif /* RALINK_ATE */
		default:
			DBGPRINT(RT_DEBUG_WARN, ("RTMPSetLED::Unknown Status 0x%x\n", Status));
			break;
	}

	if (MCUCmd)
	{
		AsicSendCommandToMcu(pAd, MCUCmd, 0xff, LedMode, LinkStatus);
		DBGPRINT(RT_DEBUG_TRACE, ("%s: MCUCmd:0x%x, LED Mode:0x%x, LinkStatus:0x%x\n", __FUNCTION__, MCUCmd, LedMode, LinkStatus)); 
	}
	
    /* */
	/* Keep LED status for LED SiteSurvey mode. */
	/* After SiteSurvey, we will set the LED mode to previous status. */
	/* */
	if ((Status != LED_ON_SITE_SURVEY) && (Status != LED_POWER_UP) && (bIgnored == FALSE))
		pAd->LedCntl.LedStatus = Status;
    
}


/*
	========================================================================
	
	Routine Description:
		Set LED Signal Stregth 

	Arguments:
		pAd						Pointer to our adapter
		Dbm						Signal Stregth

	Return Value:
		None

	IRQL = PASSIVE_LEVEL
	
	Note:
		Can be run on any IRQL level. 

		According to Microsoft Zero Config Wireless Signal Stregth definition as belows.
		<= -90  No Signal
		<= -81  Very Low
		<= -71  Low
		<= -67  Good
		<= -57  Very Good
		 > -57  Excellent		
	========================================================================
*/
VOID RTMPSetSignalLED(
	IN PRTMP_ADAPTER 	pAd, 
	IN NDIS_802_11_RSSI Dbm)
{
	UCHAR		nLed = 0;


#ifdef RTMP_MAC_USB
#ifdef STATS_COUNT_SUPPORT
	if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
		return;
#endif /* STATS_COUNT_SUPPORT */
#endif /* RTMP_MAC_USB */


	if (pAd->LedCntl.MCULedCntl.field.LedMode == LED_MODE_SIGNAL_STREGTH)
	{
		if (Dbm <= -90)
			nLed = 0;
		else if (Dbm <= -81)
			nLed = 1;
		else if (Dbm <= -71)
			nLed = 3;
		else if (Dbm <= -67)
			nLed = 7;
		else if (Dbm <= -57)
			nLed = 15;
		else 
			nLed = 31;

		/* */
		/* Update Signal Stregth to firmware if changed. */
		/* */
		if (pAd->LedCntl.LedIndicatorStrength != nLed)
		{
			AsicSendCommandToMcu(pAd, MCU_SET_LED_GPIO_SIGNAL_CFG, 0xff, nLed, pAd->LedCntl.MCULedCntl.field.Polarity);
			pAd->LedCntl.LedIndicatorStrength = nLed;
		}
	}
}




void RTMPGetLEDSetting(IN RTMP_ADAPTER *pAd)
{
	USHORT Value;
	PLED_CONTROL pLedCntl = &pAd->LedCntl;
	{
		RT28xx_EEPROM_READ16(pAd, EEPROM_FREQ_OFFSET, Value);
		pLedCntl->MCULedCntl.word = (Value >> 8);
		RT28xx_EEPROM_READ16(pAd, EEPROM_LEDAG_CONF_OFFSET, Value);
		pLedCntl->LedAGCfg= Value;
		RT28xx_EEPROM_READ16(pAd, EEPROM_LEDACT_CONF_OFFSET, Value);
		pLedCntl->LedACTCfg = Value;
		RT28xx_EEPROM_READ16(pAd, EEPROM_LED_POLARITY_OFFSET, Value);
		pLedCntl->LedPolarity = Value;
	}
}


void RTMPStartLEDMode(IN RTMP_ADAPTER *pAd)
{
}


void RTMPInitLEDMode(IN RTMP_ADAPTER *pAd)
{
	PLED_CONTROL pLedCntl = &pAd->LedCntl;

	if (pLedCntl->MCULedCntl.word == 0xFF)
	{
		pLedCntl->MCULedCntl.word = 0x01;
		pLedCntl->LedAGCfg = 0x5555;
		pLedCntl->LedACTCfg= 0x2221;

#ifdef RTMP_MAC_USB
		pLedCntl->LedPolarity = 0x5627; 
#endif /* RTMP_MAC_USB */
	}
	
	AsicSendCommandToMcu(pAd, MCU_SET_LED_AG_CFG, 0xff, (UCHAR)pLedCntl->LedAGCfg, (UCHAR)(pLedCntl->LedAGCfg >> 8));
	AsicSendCommandToMcu(pAd, MCU_SET_LED_ACT_CFG, 0xff, (UCHAR)pLedCntl->LedACTCfg, (UCHAR)(pLedCntl->LedACTCfg >> 8));
	AsicSendCommandToMcu(pAd, MCU_SET_LED_POLARITY, 0xff, (UCHAR)pLedCntl->LedPolarity, (UCHAR)(pLedCntl->LedPolarity >> 8));
	AsicSendCommandToMcu(pAd, MCU_SET_LED_GPIO_SIGNAL_CFG, 0xff, 0, pLedCntl->MCULedCntl.field.Polarity);

	pAd->LedCntl.LedIndicatorStrength = 0xFF;
	RTMPSetSignalLED(pAd, -100);	/* Force signal strength Led to be turned off, before link up */

	RTMPStartLEDMode(pAd);
}


inline void RTMPExitLEDMode(IN RTMP_ADAPTER *pAd)
{

	RTMPSetLED(pAd, LED_LINK_DOWN);

	return;
}

#endif /* LED_CONTROL_SUPPORT */
