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


#ifndef __RT_LED_H__
#define __RT_LED_H__

/* LED MCU command */
#define MCU_SET_LED_MODE				0x50
#define MCU_SET_LED_GPIO_SIGNAL_CFG		0x51
#define MCU_SET_LED_AG_CFG 				0x52
#define MCU_SET_LED_ACT_CFG 			0x53
#define MCU_SET_LED_POLARITY			0x54

/* LED Mode */
#define LED_MODE(pAd) ((pAd)->LedCntl.MCULedCntl.field.LedMode & 0x7F)
#define LED_HW_CONTROL					19	/* set LED to controll by MAC registers instead of by firmware */
#define LED_MODE_DEFAULT            	0	/* value domain of pAd->LedCntl.LedMode and E2PROM */
#define LED_MODE_TWO_LED				1
#define LED_MODE_8SEC_SCAN				2	/* Same as LED mode 1; except that fast blink for 8sec when doing scanning. */
#define LED_MODE_SITE_SURVEY_SLOW_BLINK	3	/* Same as LED mode 1; except that make ACT slow blinking during site survey period and blink once at power-up. */
#define LED_MODE_WPS_LOW_POLARITY		4	/* Same as LED mode 1; except that make ACT steady on during WPS period */
#define LED_MODE_WPS_HIGH_POLARITY		5	/* Same as LED mode 1; except that make ACT steady on during WPS period */
/*#define LED_MODE_SIGNAL_STREGTH		8   // EEPROM define =8 */
#define LED_MODE_SIGNAL_STREGTH			0x40 /* EEPROM define = 64 */

/* Driver LED Status */
#define LED_LINK_DOWN		0
#define LED_LINK_UP			1
#define LED_RADIO_OFF		2
#define LED_RADIO_ON		3
#define LED_HALT			4
#define LED_WPS				5
#define LED_ON_SITE_SURVEY	6
#define LED_POWER_UP		7

/* MCU Led Link Status */
#define LINK_STATUS_LINK_DOWN		0x20
#define LINK_STATUS_ABAND_LINK_UP	0xa0
#define LINK_STATUS_GBAND_LINK_UP	0x60
#define LINK_STATUS_RADIO_ON		0x20
#define LINK_STATUS_RADIO_OFF		0x00
#define LINK_STATUS_WPS				0x10
#define LINK_STATUS_ON_SITE_SURVEY	0x08
#define LINK_STATUS_POWER_UP		0x04
#define LINK_STATUS_HW_CONTROL		0x00


#define ACTIVE_LOW 	0
#define ACTIVE_HIGH 1

/* */
/* MCU_LEDCS: MCU LED Control Setting. */
/* */
typedef union  _MCU_LEDCS_STRUC {
	struct	{
#ifdef RT_BIG_ENDIAN
		UCHAR		Polarity:1;
		UCHAR		LedMode:7;
#else
		UCHAR		LedMode:7;		
		UCHAR		Polarity:1;
#endif /* RT_BIG_ENDIAN */
	} field;
	UCHAR				word;
} MCU_LEDCS_STRUC, *PMCU_LEDCS_STRUC;

void RTMPGetLEDSetting(IN RTMP_ADAPTER *pAd);
void RTMPInitLEDMode(IN RTMP_ADAPTER *pAd);
void RTMPExitLEDMode(IN RTMP_ADAPTER *pAd);

VOID RTMPSetLEDStatus(
	IN PRTMP_ADAPTER 	pAd, 
	IN UCHAR			Status);

#ifdef RTMP_MAC_USB
#define RTMPSetLED(pAd, Status)	\
do{								\
	UCHAR LEDStatus;			\
	LEDStatus = Status;			\
	RTEnqueueInternalCmd(pAd, CMDTHREAD_SET_LED_STATUS, &LEDStatus, sizeof(LEDStatus));	\
}while(0)
	
#endif /* RTMP_MAC_USB */


VOID RTMPSetSignalLED(
	IN PRTMP_ADAPTER 	pAd, 
	IN NDIS_802_11_RSSI Dbm);



typedef struct _LED_CONTROL
{
	MCU_LEDCS_STRUC		MCULedCntl; /* LED Mode EEPROM 0x3b */
	USHORT				LedAGCfg;	/* LED A/G Configuration EEPROM 0x3c */
	USHORT				LedACTCfg;	/* LED ACT Configuration EEPROM 0x3e */
	USHORT				LedPolarity;/* LED A/G/ACT polarity EEPROM 0x40 */
	UCHAR				LedIndicatorStrength;
	UCHAR				RssiSingalstrengthOffet;
	BOOLEAN				bLedOnScanning;
	UCHAR				LedStatus;
}LED_CONTROL, *PLED_CONTROL;

void RTMPStartLEDMode(IN RTMP_ADAPTER *pAd);

#endif /* __RT_LED_H__ */

