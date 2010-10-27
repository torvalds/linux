/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
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
 *************************************************************************

	Module Name:
	rtmp_phy.h

	Abstract:
	Ralink Wireless Chip PHY(BBP/RF) related definition & structures

	Revision History:
	Who			When		  What
	--------	----------	  ----------------------------------------------
*/

#ifndef __RTMP_PHY_H__
#define __RTMP_PHY_H__

/*
	RF sections
*/
#define RF_R00			0
#define RF_R01			1
#define RF_R02			2
#define RF_R03			3
#define RF_R04			4
#define RF_R05			5
#define RF_R06			6
#define RF_R07			7
#define RF_R08			8
#define RF_R09			9
#define RF_R10			10
#define RF_R11			11
#define RF_R12			12
#define RF_R13			13
#define RF_R14			14
#define RF_R15			15
#define RF_R16			16
#define RF_R17			17
#define RF_R18			18
#define RF_R19			19
#define RF_R20			20
#define RF_R21			21
#define RF_R22			22
#define RF_R23			23
#define RF_R24			24
#define RF_R25			25
#define RF_R26			26
#define RF_R27			27
#define RF_R28			28
#define RF_R29			29
#define RF_R30			30
#define RF_R31			31

/* value domain of pAd->RfIcType */
#define RFIC_2820                   1	/* 2.4G 2T3R */
#define RFIC_2850                   2	/* 2.4G/5G 2T3R */
#define RFIC_2720                   3	/* 2.4G 1T2R */
#define RFIC_2750                   4	/* 2.4G/5G 1T2R */
#define RFIC_3020                   5	/* 2.4G 1T1R */
#define RFIC_2020                   6	/* 2.4G B/G */
#define RFIC_3021                   7	/* 2.4G 1T2R */
#define RFIC_3022                   8	/* 2.4G 2T2R */
#define RFIC_3052                   9	/* 2.4G/5G 2T2R */

/*
	BBP sections
*/
#define BBP_R0			0	/* version */
#define BBP_R1			1	/* TSSI */
#define BBP_R2			2	/* TX configure */
#define BBP_R3			3
#define BBP_R4			4
#define BBP_R5			5
#define BBP_R6			6
#define BBP_R14			14	/* RX configure */
#define BBP_R16			16
#define BBP_R17			17	/* RX sensibility */
#define BBP_R18			18
#define BBP_R21			21
#define BBP_R22			22
#define BBP_R24			24
#define BBP_R25			25
#define BBP_R26			26
#define BBP_R27			27
#define BBP_R31			31
#define BBP_R49			49	/*TSSI */
#define BBP_R50			50
#define BBP_R51			51
#define BBP_R52			52
#define BBP_R55			55
#define BBP_R62			62	/* Rx SQ0 Threshold HIGH */
#define BBP_R63			63
#define BBP_R64			64
#define BBP_R65			65
#define BBP_R66			66
#define BBP_R67			67
#define BBP_R68			68
#define BBP_R69			69
#define BBP_R70			70	/* Rx AGC SQ CCK Xcorr threshold */
#define BBP_R73			73
#define BBP_R75			75
#define BBP_R77			77
#define BBP_R78			78
#define BBP_R79			79
#define BBP_R80			80
#define BBP_R81			81
#define BBP_R82			82
#define BBP_R83			83
#define BBP_R84			84
#define BBP_R86			86
#define BBP_R91			91
#define BBP_R92			92
#define BBP_R94			94	/* Tx Gain Control */
#define BBP_R103		103
#define BBP_R105		105
#define BBP_R106		106
#define BBP_R113		113
#define BBP_R114		114
#define BBP_R115		115
#define BBP_R116		116
#define BBP_R117		117
#define BBP_R118		118
#define BBP_R119		119
#define BBP_R120		120
#define BBP_R121		121
#define BBP_R122		122
#define BBP_R123		123
#ifdef RT30xx
#define BBP_R138		138	/* add by johnli, RF power sequence setup, ADC dynamic on/off control */
#endif /* RT30xx // */

#define BBPR94_DEFAULT	0x06	/* Add 1 value will gain 1db */

/* */
/* BBP & RF are using indirect access. Before write any value into it. */
/* We have to make sure there is no outstanding command pending via checking busy bit. */
/* */
#define MAX_BUSY_COUNT  100	/* Number of retry before failing access BBP & RF indirect register */

/*#define PHY_TR_SWITCH_TIME          5  // usec */

/*#define BBP_R17_LOW_SENSIBILITY     0x50 */
/*#define BBP_R17_MID_SENSIBILITY     0x41 */
/*#define BBP_R17_DYNAMIC_UP_BOUND    0x40 */

#define RSSI_FOR_VERY_LOW_SENSIBILITY   -35
#define RSSI_FOR_LOW_SENSIBILITY		-58
#define RSSI_FOR_MID_LOW_SENSIBILITY	-80
#define RSSI_FOR_MID_SENSIBILITY		-90

/*****************************************************************************
	RF register Read/Write marco definition
 *****************************************************************************/
#ifdef RTMP_MAC_PCI
#define RTMP_RF_IO_WRITE32(_A, _V)                  \
{											\
	if ((_A)->bPCIclkOff == FALSE) {				\
		PHY_CSR4_STRUC  _value;                          \
		unsigned long           _busyCnt = 0;                    \
											\
		do {                                            \
			RTMP_IO_READ32((_A), RF_CSR_CFG0, &_value.word);  \
			if (_value.field.Busy == IDLE)               \
				break;                                  \
			_busyCnt++;                                  \
		} while (_busyCnt < MAX_BUSY_COUNT);			\
		if (_busyCnt < MAX_BUSY_COUNT) {			\
			RTMP_IO_WRITE32((_A), RF_CSR_CFG0, (_V));          \
		}                                               \
	}								\
}
#endif /* RTMP_MAC_PCI // */
#ifdef RTMP_MAC_USB
#define RTMP_RF_IO_WRITE32(_A, _V)                 RTUSBWriteRFRegister(_A, _V)
#endif /* RTMP_MAC_USB // */

#ifdef RT30xx
#define RTMP_RF_IO_READ8_BY_REG_ID(_A, _I, _pV)    RT30xxReadRFRegister(_A, _I, _pV)
#define RTMP_RF_IO_WRITE8_BY_REG_ID(_A, _I, _V)    RT30xxWriteRFRegister(_A, _I, _V)
#endif /* RT30xx // */

/*****************************************************************************
	BBP register Read/Write marco definitions.
	we read/write the bbp value by register's ID.
	Generate PER to test BA
 *****************************************************************************/
#ifdef RTMP_MAC_PCI
/*
	basic marco for BBP read operation.
	_pAd: the data structure pointer of struct rt_rtmp_adapter
	_bbpID : the bbp register ID
	_pV: data pointer used to save the value of queried bbp register.
	_bViaMCU: if we need access the bbp via the MCU.
*/
#define RTMP_BBP_IO_READ8(_pAd, _bbpID, _pV, _bViaMCU)			\
	do {								\
		BBP_CSR_CFG_STRUC  BbpCsr;				\
		int   _busyCnt, _secCnt, _regID;			\
									\
		_regID = ((_bViaMCU) == TRUE ? H2M_BBP_AGENT : BBP_CSR_CFG); \
		for (_busyCnt = 0; _busyCnt < MAX_BUSY_COUNT; _busyCnt++) { \
			RTMP_IO_READ32(_pAd, _regID, &BbpCsr.word);	\
			if (BbpCsr.field.Busy == BUSY)                  \
				continue;                               \
			BbpCsr.word = 0;                                \
			BbpCsr.field.fRead = 1;                         \
			BbpCsr.field.BBP_RW_MODE = 1;                   \
			BbpCsr.field.Busy = 1;                          \
			BbpCsr.field.RegNum = _bbpID;                   \
			RTMP_IO_WRITE32(_pAd, _regID, BbpCsr.word);     \
			if ((_bViaMCU) == TRUE) {			\
			    AsicSendCommandToMcu(_pAd, 0x80, 0xff, 0x0, 0x0); \
			    RTMPusecDelay(1000);	\
			}						\
			for (_secCnt = 0; _secCnt < MAX_BUSY_COUNT; _secCnt++) { \
				RTMP_IO_READ32(_pAd, _regID, &BbpCsr.word); \
				if (BbpCsr.field.Busy == IDLE)		\
					break;				\
			}						\
			if ((BbpCsr.field.Busy == IDLE) &&		\
				(BbpCsr.field.RegNum == _bbpID)) {	\
				*(_pV) = (u8)BbpCsr.field.Value;	\
				break;					\
			}						\
		}							\
		if (BbpCsr.field.Busy == BUSY) {			\
			DBGPRINT_ERR(("BBP(viaMCU=%d) read R%d fail\n", (_bViaMCU), _bbpID));	\
			*(_pV) = (_pAd)->BbpWriteLatch[_bbpID];               \
			if ((_bViaMCU) == TRUE) {			\
				RTMP_IO_READ32(_pAd, _regID, &BbpCsr.word);				\
				BbpCsr.field.Busy = 0;                          \
				RTMP_IO_WRITE32(_pAd, _regID, BbpCsr.word);				\
			}				\
		}													\
	} while (0)

/*
	This marco used for the BBP read operation which didn't need via MCU.
*/
#define BBP_IO_READ8_BY_REG_ID(_A, _I, _pV)			\
	RTMP_BBP_IO_READ8((_A), (_I), (_pV), FALSE)

/*
	This marco used for the BBP read operation which need via MCU.
	But for some chipset which didn't have mcu (e.g., RBUS based chipset), we
	will use this function too and didn't access the bbp register via the MCU.
*/
/* Read BBP register by register's ID. Generate PER to test BA */
#define RTMP_BBP_IO_READ8_BY_REG_ID(_A, _I, _pV)						\
{																		\
	BBP_CSR_CFG_STRUC	BbpCsr;											\
	int					i, k;			\
	BOOLEAN					brc;			\
	BbpCsr.field.Busy = IDLE;			\
	if ((IS_RT3090((_A)) || IS_RT3572((_A)) || IS_RT3390((_A)))  \
		&& ((_A)->StaCfg.PSControl.field.rt30xxPowerMode == 3)	\
		&& ((_A)->StaCfg.PSControl.field.EnableNewPS == TRUE)	\
		&& ((_A)->bPCIclkOff == FALSE)	\
		&& ((_A)->brt30xxBanMcuCmd == FALSE)) {			\
		for (i = 0; i < MAX_BUSY_COUNT; i++) {			\
			RTMP_IO_READ32(_A, H2M_BBP_AGENT, &BbpCsr.word); \
			if (BbpCsr.field.Busy == BUSY) {		\
				continue;				\
			}						\
			BbpCsr.word = 0;				\
			BbpCsr.field.fRead = 1;				\
			BbpCsr.field.BBP_RW_MODE = 1;			\
			BbpCsr.field.Busy = 1;				\
			BbpCsr.field.RegNum = _I;			\
			RTMP_IO_WRITE32(_A, H2M_BBP_AGENT, BbpCsr.word); \
			brc = AsicSendCommandToMcu(_A, 0x80, 0xff, 0x0, 0x0); \
			if (brc == TRUE) {				\
				for (k = 0; k < MAX_BUSY_COUNT; k++) {	\
					RTMP_IO_READ32(_A, H2M_BBP_AGENT, &BbpCsr.word); \
					if (BbpCsr.field.Busy == IDLE)	\
						break;			\
				}					\
				if ((BbpCsr.field.Busy == IDLE) &&	\
					(BbpCsr.field.RegNum == _I)) {	\
					*(_pV) = (u8)BbpCsr.field.Value; \
					break;				\
				}					\
			} else {					\
				BbpCsr.field.Busy = 0;											\
				RTMP_IO_WRITE32(_A, H2M_BBP_AGENT, BbpCsr.word);				\
			}																\
		}																	\
	}	\
	else if (!((IS_RT3090((_A)) || IS_RT3572((_A)) || IS_RT3390((_A))) && ((_A)->StaCfg.PSControl.field.rt30xxPowerMode == 3)	\
		&& ((_A)->StaCfg.PSControl.field.EnableNewPS == TRUE))	\
		&& ((_A)->bPCIclkOff == FALSE)) {			\
		for (i = 0; i < MAX_BUSY_COUNT; i++) {			\
			RTMP_IO_READ32(_A, H2M_BBP_AGENT, &BbpCsr.word); \
			if (BbpCsr.field.Busy == BUSY) {		\
				continue;				\
			}						\
			BbpCsr.word = 0;				\
			BbpCsr.field.fRead = 1;				\
			BbpCsr.field.BBP_RW_MODE = 1;			\
			BbpCsr.field.Busy = 1;				\
			BbpCsr.field.RegNum = _I;			\
			RTMP_IO_WRITE32(_A, H2M_BBP_AGENT, BbpCsr.word); \
			AsicSendCommandToMcu(_A, 0x80, 0xff, 0x0, 0x0);	\
			for (k = 0; k < MAX_BUSY_COUNT; k++) {		\
				RTMP_IO_READ32(_A, H2M_BBP_AGENT, &BbpCsr.word); \
				if (BbpCsr.field.Busy == IDLE)		\
					break;				\
			}						\
			if ((BbpCsr.field.Busy == IDLE) &&		\
				(BbpCsr.field.RegNum == _I)) {		\
				*(_pV) = (u8)BbpCsr.field.Value;	\
				break;					\
			}						\
		}							\
	} else {							\
		DBGPRINT_ERR((" , brt30xxBanMcuCmd = %d, Read BBP %d \n", (_A)->brt30xxBanMcuCmd, (_I)));	\
		*(_pV) = (_A)->BbpWriteLatch[_I];			\
	}								\
	if ((BbpCsr.field.Busy == BUSY) || ((_A)->bPCIclkOff == TRUE)) { \
		DBGPRINT_ERR(("BBP read R%d=0x%x fail\n", _I, BbpCsr.word)); \
		*(_pV) = (_A)->BbpWriteLatch[_I];			\
	}								\
}

/*
	basic marco for BBP write operation.
	_pAd: the data structure pointer of struct rt_rtmp_adapter
	_bbpID : the bbp register ID
	_pV: data used to save the value of queried bbp register.
	_bViaMCU: if we need access the bbp via the MCU.
*/
#define RTMP_BBP_IO_WRITE8(_pAd, _bbpID, _pV, _bViaMCU)			\
	do {								\
		BBP_CSR_CFG_STRUC  BbpCsr;                             \
		int             _busyCnt, _regID;			\
									\
		_regID = ((_bViaMCU) == TRUE ? H2M_BBP_AGENT : BBP_CSR_CFG);	\
		for (_busyCnt = 0; _busyCnt < MAX_BUSY_COUNT; _busyCnt++) { \
			RTMP_IO_READ32((_pAd), BBP_CSR_CFG, &BbpCsr.word);     \
			if (BbpCsr.field.Busy == BUSY)			\
				continue;				\
			BbpCsr.word = 0;				\
			BbpCsr.field.fRead = 0;				\
			BbpCsr.field.BBP_RW_MODE = 1;			\
			BbpCsr.field.Busy = 1;				\
			BbpCsr.field.Value = _pV;			\
			BbpCsr.field.RegNum = _bbpID;			\
			RTMP_IO_WRITE32((_pAd), BBP_CSR_CFG, BbpCsr.word); \
			if ((_bViaMCU) == TRUE) {			\
				AsicSendCommandToMcu(_pAd, 0x80, 0xff, 0x0, 0x0); \
				if ((_pAd)->OpMode == OPMODE_AP)	\
					RTMPusecDelay(1000);		\
			}						\
			(_pAd)->BbpWriteLatch[_bbpID] = _pV;		\
			break;						\
		}							\
		if (_busyCnt == MAX_BUSY_COUNT) {			\
			DBGPRINT_ERR(("BBP write R%d fail\n", _bbpID));	\
			if ((_bViaMCU) == TRUE) {			\
				RTMP_IO_READ32(_pAd, H2M_BBP_AGENT, &BbpCsr.word);	\
				BbpCsr.field.Busy = 0;			\
				RTMP_IO_WRITE32(_pAd, H2M_BBP_AGENT, BbpCsr.word);	\
			}						\
		}							\
	} while (0)

/*
	This marco used for the BBP write operation which didn't need via MCU.
*/
#define BBP_IO_WRITE8_BY_REG_ID(_A, _I, _pV)			\
	RTMP_BBP_IO_WRITE8((_A), (_I), (_pV), FALSE)

/*
	This marco used for the BBP write operation which need via MCU.
	But for some chipset which didn't have mcu (e.g., RBUS based chipset), we
	will use this function too and didn't access the bbp register via the MCU.
*/
/* Write BBP register by register's ID & value */
#define RTMP_BBP_IO_WRITE8_BY_REG_ID(_A, _I, _V)			\
{									\
	BBP_CSR_CFG_STRUC	BbpCsr;					\
	int					BusyCnt = 0;		\
	BOOLEAN					brc;			\
	if (_I < MAX_NUM_OF_BBP_LATCH) {				\
		if ((IS_RT3090((_A)) || IS_RT3572((_A)) || IS_RT3390((_A))) \
			&& ((_A)->StaCfg.PSControl.field.rt30xxPowerMode == 3)	\
			&& ((_A)->StaCfg.PSControl.field.EnableNewPS == TRUE)	\
			&& ((_A)->bPCIclkOff == FALSE)	\
			&& ((_A)->brt30xxBanMcuCmd == FALSE)) {		\
			if (_A->AccessBBPFailCount > 20) {		\
				AsicResetBBPAgent(_A);			\
				_A->AccessBBPFailCount = 0;		\
			}						\
			for (BusyCnt = 0; BusyCnt < MAX_BUSY_COUNT; BusyCnt++) { \
				RTMP_IO_READ32(_A, H2M_BBP_AGENT, &BbpCsr.word);				\
				if (BbpCsr.field.Busy == BUSY)									\
					continue;													\
				BbpCsr.word = 0;												\
				BbpCsr.field.fRead = 0;											\
				BbpCsr.field.BBP_RW_MODE = 1;									\
				BbpCsr.field.Busy = 1;											\
				BbpCsr.field.Value = _V;										\
				BbpCsr.field.RegNum = _I;										\
				RTMP_IO_WRITE32(_A, H2M_BBP_AGENT, BbpCsr.word);				\
				brc = AsicSendCommandToMcu(_A, 0x80, 0xff, 0x0, 0x0);					\
				if (brc == TRUE) {			\
					(_A)->BbpWriteLatch[_I] = _V;									\
				} else {				\
					BbpCsr.field.Busy = 0;											\
					RTMP_IO_WRITE32(_A, H2M_BBP_AGENT, BbpCsr.word);				\
				}																\
				break;															\
			}																	\
		}																	\
		else if (!((IS_RT3090((_A)) || IS_RT3572((_A)) || IS_RT3390((_A))) \
			&& ((_A)->StaCfg.PSControl.field.rt30xxPowerMode == 3)	\
			&& ((_A)->StaCfg.PSControl.field.EnableNewPS == TRUE))	\
			&& ((_A)->bPCIclkOff == FALSE)) { 		\
			if (_A->AccessBBPFailCount > 20) {		\
				AsicResetBBPAgent(_A);			\
				_A->AccessBBPFailCount = 0;		\
			}						\
			for (BusyCnt = 0; BusyCnt < MAX_BUSY_COUNT; BusyCnt++) { \
				RTMP_IO_READ32(_A, H2M_BBP_AGENT, &BbpCsr.word);				\
				if (BbpCsr.field.Busy == BUSY)									\
					continue;													\
				BbpCsr.word = 0;												\
				BbpCsr.field.fRead = 0;											\
				BbpCsr.field.BBP_RW_MODE = 1;									\
				BbpCsr.field.Busy = 1;											\
				BbpCsr.field.Value = _V;										\
				BbpCsr.field.RegNum = _I;										\
				RTMP_IO_WRITE32(_A, H2M_BBP_AGENT, BbpCsr.word);				\
				AsicSendCommandToMcu(_A, 0x80, 0xff, 0x0, 0x0);					\
				(_A)->BbpWriteLatch[_I] = _V;									\
				break;															\
			}																	\
		} else {						\
			DBGPRINT_ERR(("  brt30xxBanMcuCmd = %d. Write BBP %d \n",  (_A)->brt30xxBanMcuCmd, (_I)));	\
		}																	\
		if ((BusyCnt == MAX_BUSY_COUNT) || ((_A)->bPCIclkOff == TRUE)) { \
			if (BusyCnt == MAX_BUSY_COUNT)				\
				(_A)->AccessBBPFailCount++;					\
			DBGPRINT_ERR(("BBP write R%d=0x%x fail. BusyCnt= %d.bPCIclkOff = %d. \n", _I, BbpCsr.word, BusyCnt, (_A)->bPCIclkOff));	\
		}																	\
	} else {							\
		DBGPRINT_ERR(("****** BBP_Write_Latch Buffer exceeds max boundry ****** \n"));	\
	}																		\
}
#endif /* RTMP_MAC_PCI // */

#ifdef RTMP_MAC_USB
#define RTMP_BBP_IO_READ8_BY_REG_ID(_A, _I, _pV)   RTUSBReadBBPRegister(_A, _I, _pV)
#define RTMP_BBP_IO_WRITE8_BY_REG_ID(_A, _I, _V)   RTUSBWriteBBPRegister(_A, _I, _V)

#define BBP_IO_WRITE8_BY_REG_ID(_A, _I, _V)			RTUSBWriteBBPRegister(_A, _I, _V)
#define BBP_IO_READ8_BY_REG_ID(_A, _I, _pV)		RTUSBReadBBPRegister(_A, _I, _pV)
#endif /* RTMP_MAC_USB // */

#ifdef RT30xx
#define RTMP_ASIC_MMPS_DISABLE(_pAd)							\
	do {															\
		u32 _macData; \
		u8 _bbpData = 0; \
		/* disable MMPS BBP control register */						\
		RTMP_BBP_IO_READ8_BY_REG_ID(_pAd, BBP_R3, &_bbpData);	\
		_bbpData &= ~(0x04);	/*bit 2*/								\
		RTMP_BBP_IO_WRITE8_BY_REG_ID(_pAd, BBP_R3, _bbpData);	\
																\
		/* disable MMPS MAC control register */						\
		RTMP_IO_READ32(_pAd, 0x1210, &_macData);				\
		_macData &= ~(0x09);	/*bit 0, 3*/							\
		RTMP_IO_WRITE32(_pAd, 0x1210, _macData);				\
	} while (0)

#define RTMP_ASIC_MMPS_ENABLE(_pAd)							\
	do {															\
		u32 _macData; \
		u8 _bbpData = 0; \
		/* enable MMPS BBP control register */						\
		RTMP_BBP_IO_READ8_BY_REG_ID(_pAd, BBP_R3, &_bbpData);	\
		_bbpData |= (0x04);	/*bit 2*/								\
		RTMP_BBP_IO_WRITE8_BY_REG_ID(_pAd, BBP_R3, _bbpData);	\
																\
		/* enable MMPS MAC control register */						\
		RTMP_IO_READ32(_pAd, 0x1210, &_macData);				\
		_macData |= (0x09);	/*bit 0, 3*/							\
		RTMP_IO_WRITE32(_pAd, 0x1210, _macData);				\
	} while (0)

#endif /* RT30xx // */

#endif /* __RTMP_PHY_H__ // */
