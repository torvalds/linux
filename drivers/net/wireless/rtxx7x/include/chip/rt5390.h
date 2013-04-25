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


#ifndef __RT5390_H__
#define __RT5390_H__

#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)


#ifndef RTMP_RF_RW_SUPPORT
#error "For RT5390, you should define the compile flag -DRTMP_RF_RW_SUPPORT"
#endif

#ifndef RT30xx
#error "For RT5390, you should define the compile flag -DRT30xx"
#endif
#ifdef CARRIER_DETECTION_SUPPORT
#define TONE_RADAR_DETECT_SUPPORT
#define TONE_RADAR_DETECT_V2
#endif // CARRIER_DETECTION_SUPPORT //

#ifdef CONFIG_STA_SUPPORT
#endif // CONFIG_STA_SUPPORT //
extern REG_PAIR RF5390RegTable[];
extern UCHAR NUM_RF_5390_REG_PARMS;

#define BBP_REG_BF			BBP_R163 // TxBf control
#ifdef CONFIG_STA_SUPPORT
#endif // CONFIG_STA_SUPPORT //

#ifdef RTMP_FLASH_SUPPORT
#define EEPROM_DEFAULT_FILE_PATH                     "etc_ro/Wireless/RT2860AP/RT3092_PCIe_LNA_2T2R_ALC_V1_2.bin"
#define RF_OFFSET					0x48000
extern void RtmpFlashWrite(UCHAR * p, ULONG a, ULONG b);
extern void RtmpFlashRead(UCHAR * p, ULONG a, ULONG b);
#endif // RTMP_FLASH_SUPPORT //

/* Device ID & Vendor ID, these values should match EEPROM value */

#define NIC5390_PCIe_DEVICE_ID	0x5390
#define NIC539F_PCIe_DEVICE_ID  0x539F
#define NIC5392_PCIe_DEVICE_ID  0x5392
#define NIC5362_PCI_DEVICE_ID	0x5362
#define NIC5360_PCI_DEVICE_ID   0x5360

VOID RT5390HaltAction(
	IN struct _RTMP_ADAPTER			*pAd);


/* add by johnli, RF power sequence setup */
VOID RT5390LoadRFNormalModeSetup(
	IN struct _RTMP_ADAPTER			*pAd);

VOID RT5390LoadRFSleepModeSetup(
	IN struct _RTMP_ADAPTER			*pAd);

VOID RT5390ReverseRFSleepModeSetup(
	IN struct _RTMP_ADAPTER			*pAd,
	IN BOOLEAN			FlgIsInitState);
/* end johnli */


VOID RT5390_Init(
	IN struct _RTMP_ADAPTER			*pAd);

VOID NICInitRT5390BbpRegisters(
	IN struct _RTMP_ADAPTER		*pAd);

VOID NICInitRT5390MacRegisters(
	IN struct _RTMP_ADAPTER		*pAd);

VOID RT5390_AsicEeBufferInit(
	IN struct _RTMP_ADAPTER		*pAd);

VOID RT5390_RxSensitivityTuning(
	IN struct _RTMP_ADAPTER		*pAd);

UCHAR RT5390_ChipStaBBPAdjust(
	IN struct _RTMP_ADAPTER		*pAd,
	IN CHAR						Rssi,
	IN UCHAR					R66);

VOID RT5390_ChipBBPAdjust(
	IN struct _RTMP_ADAPTER		*pAd);

VOID RT5390_ChipSwitchChannel(
	IN struct _RTMP_ADAPTER 	*pAd,
	IN UCHAR					Channel,
	IN BOOLEAN					bScan);


#ifdef RTMP_INTERNAL_TX_ALC

VOID RT5390_InitDesiredTSSITable(
	IN struct _RTMP_ADAPTER		*pAd);

INT RT5390_ATETssiCalibration(
	IN struct _RTMP_ADAPTER		*pAd,
	IN PSTRING					arg);

INT RT5390_ATETssiCalibrationExtend(
	IN struct _RTMP_ADAPTER		*pAd,
	IN PSTRING					arg);

VOID RT5390_AsicTxAlcGetAutoAgcOffset(
	IN struct _RTMP_ADAPTER		*pAd,
	IN PCHAR					pDeltaPwr,
	IN PCHAR					pTotalDeltaPwr,
	IN PCHAR					pAgcCompensate,
	IN PUCHAR					pBbpR49);

UINT32 RT5390_GetDesiredTSSI(
	IN struct _RTMP_ADAPTER		*pAd);

LONG Rounding(
	IN struct _RTMP_ADAPTER		*pAd,
	IN LONG Integer, 
	IN LONG Fraction, 
	IN LONG DenominatorOfTssiRatio);

#endif /* RTMP_INTERNAL_TX_ALC */

#ifdef RTMP_TEMPERATURE_COMPENSATION
INT RT5392_ATEReadExternalTSSI(
	IN struct _RTMP_ADAPTER		*pAd,
	IN PSTRING					arg);
#endif /* RTMP_TEMPERATURE_COMPENSATION */

VOID RT5390_RTMPSetAGCInitValue(
	IN struct _RTMP_ADAPTER		*pAd,
	IN UCHAR					BandWidth);

VOID RT5390_ChipResumeMsduTransmission(
	IN struct _RTMP_ADAPTER		*pAd);

VOID RT5390_AsicResetBbpAgent(
	IN struct _RTMP_ADAPTER		*pAd);	

#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */

#endif /* __RT5390_H__ */

