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


#ifndef __RT_ATE_H__
#define __RT_ATE_H__

#ifdef RALINK_QA
typedef struct ate_racfghdr {
 	UINT32		magic_no;
	USHORT		command_type;
	USHORT		command_id;
	USHORT		length;
	USHORT		sequence;
	USHORT		status;
	UCHAR		data[2046];
}  __attribute__((packed))RACFGHDR, *pRACFGHDR;

/* Eth QA RACFG Command */
#define RACFG_MAGIC_NO			0x18142880
/* command id with Cmd Type == 0x0005(for iNIC)/0x0008(for others) */
#define RACFG_CMD_RF_WRITE_ALL			0x0000
#define RACFG_CMD_E2PROM_READ16			0x0001
#define RACFG_CMD_E2PROM_WRITE16		0x0002
#define RACFG_CMD_E2PROM_READ_ALL		0x0003
#define RACFG_CMD_E2PROM_WRITE_ALL		0x0004
#define RACFG_CMD_IO_READ				0x0005
#define RACFG_CMD_IO_WRITE				0x0006
#define RACFG_CMD_IO_READ_BULK			0x0007
#define RACFG_CMD_BBP_READ8				0x0008
#define RACFG_CMD_BBP_WRITE8			0x0009
#define RACFG_CMD_BBP_READ_ALL			0x000a
#define RACFG_CMD_GET_COUNTER			0x000b
#define RACFG_CMD_CLEAR_COUNTER			0x000c

#define RACFG_CMD_RSV1					0x000d
#define RACFG_CMD_RSV2					0x000e
#define RACFG_CMD_RSV3					0x000f

#define RACFG_CMD_TX_START				0x0010
#define RACFG_CMD_GET_TX_STATUS			0x0011
#define RACFG_CMD_TX_STOP				0x0012
#define RACFG_CMD_RX_START				0x0013
#define RACFG_CMD_RX_STOP				0x0014
#define RACFG_CMD_GET_NOISE_LEVEL		0x0015

#define RACFG_CMD_ATE_START				0x0080
#define RACFG_CMD_ATE_STOP				0x0081

#define RACFG_CMD_ATE_START_TX_CARRIER		0x0100
#define RACFG_CMD_ATE_START_TX_CONT			0x0101
#define RACFG_CMD_ATE_START_TX_FRAME		0x0102
#define RACFG_CMD_ATE_SET_BW	            0x0103
#define RACFG_CMD_ATE_SET_TX_POWER0	        0x0104
#define RACFG_CMD_ATE_SET_TX_POWER1			0x0105
#define RACFG_CMD_ATE_SET_FREQ_OFFSET		0x0106
#define RACFG_CMD_ATE_GET_STATISTICS		0x0107
#define RACFG_CMD_ATE_RESET_COUNTER			0x0108
#define RACFG_CMD_ATE_SEL_TX_ANTENNA		0x0109
#define RACFG_CMD_ATE_SEL_RX_ANTENNA		0x010a
#define RACFG_CMD_ATE_SET_PREAMBLE			0x010b
#define RACFG_CMD_ATE_SET_CHANNEL			0x010c
#define RACFG_CMD_ATE_SET_ADDR1				0x010d
#define RACFG_CMD_ATE_SET_ADDR2				0x010e
#define RACFG_CMD_ATE_SET_ADDR3				0x010f
#define RACFG_CMD_ATE_SET_RATE				0x0110
#define RACFG_CMD_ATE_SET_TX_FRAME_LEN		0x0111
#define RACFG_CMD_ATE_SET_TX_FRAME_COUNT	0x0112
#define RACFG_CMD_ATE_START_RX_FRAME		0x0113
#define RACFG_CMD_ATE_E2PROM_READ_BULK		0x0114
#define RACFG_CMD_ATE_E2PROM_WRITE_BULK		0x0115
#define RACFG_CMD_ATE_IO_WRITE_BULK			0x0116
#define RACFG_CMD_ATE_BBP_READ_BULK			0x0117
#define RACFG_CMD_ATE_BBP_WRITE_BULK		0x0118
#define RACFG_CMD_ATE_RF_READ_BULK			0x0119
#define RACFG_CMD_ATE_RF_WRITE_BULK			0x011a
#define RACFG_CMD_ATE_SET_TX_POWER2			0x011b
#if defined (RT3883) || defined (RT3352) || defined (RT5350)
#define RACFG_CMD_ATE_RUN_CPUBUSY	0x0202
#endif /* defined (RT3883) || defined (RT3352) || defined (RT5350) */

/* QA RACFG Command for ate test from localhost */
#define RACFG_CMD_ATE_SHOW_PARAM 0xff00

/* ATE export paramters to uppler layer */
typedef struct __ATE_EX_PARAM
{
	unsigned char mode;
	char TxPower0;
	char TxPower1;
#ifdef DOT11N_SS3_SUPPORT
	char TxPower2;
#endif /* DOT11N_SS3_SUPPORT */
	char TxAntennaSel;
	char RxAntennaSel;
	unsigned char DA[MAC_ADDR_LEN];
	unsigned char SA[MAC_ADDR_LEN];
	unsigned char BSSID[MAC_ADDR_LEN];
	unsigned char MCS;
	unsigned char PhyMode;
	BOOLEAN ShortGI;
	BOOLEAN BW;
	unsigned int Channel;
	unsigned int TxLength;
	unsigned int TxCount;
	unsigned int RFFreqOffset;
	unsigned int IPG;
	unsigned int RxTotalCnt;
	unsigned int RxCntPerSec;
	char LastSNR0;
	char LastSNR1;
	char LastSNR2;
	char LastRssi0;
	char LastRssi1;
	char LastRssi2;
	char AvgRssi0;
	char AvgRssi1;
	char AvgRssi2;
	short AvgRssi0X8;
	short AvgRssi1X8;
	short AvgRssi2X8;
}ATE_EX_PARAM, *pATE_EX_PARAM;

#endif /* RALINK_QA */

#define	LEN_OF_ARG 16
#define ATE_ON(_p)              (((_p)->ate.Mode) != ATE_STOP)


#ifdef RTMP_MAC_USB
#define ATE_BBP_IO_READ8_BY_REG_ID(_A, _I, _pV)    RTMP_BBP_IO_READ8_BY_REG_ID(_A, _I, _pV)
#define ATE_BBP_IO_WRITE8_BY_REG_ID(_A, _I, _V)    RTMP_BBP_IO_WRITE8_BY_REG_ID(_A, _I, _V)

#define BULK_OUT_LOCK(pLock, IrqFlags)	\
		if(1 /*!(in_interrupt() & 0xffff0000)*/)	\
			RTMP_IRQ_LOCK((pLock), IrqFlags);
		
#define BULK_OUT_UNLOCK(pLock, IrqFlags)	\
		if(1 /*!(in_interrupt() & 0xffff0000)*/)	\
			RTMP_IRQ_UNLOCK((pLock), IrqFlags);

VOID ATE_RTUSBBulkOutDataPacket(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			BulkOutPipeId);

VOID ATE_RTUSBCancelPendingBulkInIRP(
	IN	PRTMP_ADAPTER	pAd);


VOID ATEResetBulkIn(
	IN PRTMP_ADAPTER	pAd);

INT ATEResetBulkOut(
	IN PRTMP_ADAPTER	pAd);
#endif /* RTMP_MAC_USB */

#ifdef RALINK_QA
VOID ATE_QA_Statistics(
	IN PRTMP_ADAPTER		pAd,
	IN PRXWI_STRUC			pRxWI,
	IN PRT28XX_RXD_STRUC    p28xxRxD,
	IN PHEADER_802_11		pHeader);
	
INT RtmpDoAte(
	IN	PRTMP_ADAPTER	pAd, 
	IN	RTMP_IOCTL_INPUT_STRUCT	*wrq,
	IN	PSTRING	wrq_name);

INT Set_TxStop_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_RxStop_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

#ifdef DBG
INT Set_EERead_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_EEWrite_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_BBPRead_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_BBPWrite_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_RFWrite_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);
#endif /* DBG */ 
#endif /* RALINK_QA */


#ifdef RTMP_RF_RW_SUPPORT
#define ATE_RF_IO_READ8_BY_REG_ID(_A, _I, _pV)     RT30xxReadRFRegister(_A, _I, _pV)
#define ATE_RF_IO_WRITE8_BY_REG_ID(_A, _I, _V)     RT30xxWriteRFRegister(_A, _I, _V)
#endif /* RTMP_RF_RW_SUPPORT */

INT Set_ATE_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_DA_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_SA_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_BSSID_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_CHANNEL_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

#ifdef RTMP_INTERNAL_TX_ALC
INT Set_ATE_TSSI_CALIBRATION_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);

INT Set_ATE_TSSI_CALIBRATION_EX_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);
#endif /* RTMP_INTERNAL_TX_ALC */

#ifdef RTMP_TEMPERATURE_COMPENSATION
INT Set_ATE_READ_EXTERNAL_TSSI_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);
#endif /* RTMP_TEMPERATURE_COMPENSATION */

INT	Set_ATE_TX_POWER0_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_TX_POWER1_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);

#ifdef DOT11N_SS3_SUPPORT
INT	Set_ATE_TX_POWER2_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);
#endif /* DOT11N_SS3_SUPPORT */

INT	Set_ATE_TX_Antenna_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);

INT	Set_ATE_RX_Antenna_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);

#ifdef RT3350
INT	Set_ATE_PA_Bias_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);
#endif /* RT3350 */

INT	Set_ATE_TX_FREQOFFSET_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_TX_BW_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_TX_LENGTH_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_TX_COUNT_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_TX_MCS_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);

INT	Set_ATE_TX_MODE_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);

INT	Set_ATE_TX_GI_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);


INT	Set_ATE_RX_FER_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_ATE_Read_RF_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

#ifndef RTMP_RF_RW_SUPPORT
INT Set_ATE_Write_RF1_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_ATE_Write_RF2_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_ATE_Write_RF3_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_ATE_Write_RF4_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);
#endif /* RTMP_RF_RW_SUPPORT */

INT Set_ATE_Load_E2P_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_ATE_Read_E2P_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

#ifdef LED_CONTROL_SUPPORT
#endif /* LED_CONTROL_SUPPORT */

INT	Set_ATE_AUTO_ALC_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_IPG_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT Set_ATE_Payload_Proc(
    IN  PRTMP_ADAPTER   pAd, 
    IN  PSTRING         arg);

#ifdef TXBF_SUPPORT
INT	Set_ATE_TX_BF_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);
#endif /* TXBF_SUPPORT */

INT	Set_ATE_Show_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

INT	Set_ATE_Help_Proc(
	IN	PRTMP_ADAPTER	pAd, 
	IN	PSTRING			arg);

VOID ATEAsicAdjustTxPower(
	IN PRTMP_ADAPTER pAd);

VOID ATESampleRssi(
	IN PRTMP_ADAPTER	pAd,
	IN PRXWI_STRUC		pRxWI);	

#ifdef RT33xx
INT Set_ATE_TX_EVM_CALIBRATION_Show_Proc(
 IN PRTMP_ADAPTER pAd,
 IN PSTRING   arg);
 
INT Set_ATE_TX_EVM_CALIBRATION_Proc(
 IN PRTMP_ADAPTER pAd,
 IN PSTRING   arg);
 
INT Set_ATE_TX_EVM_CALIBRATION_Fill_Proc(
 IN PRTMP_ADAPTER pAd,
 IN PSTRING   arg);
#endif /* RT33xx */
#endif /* __RT_ATE_H__ */
