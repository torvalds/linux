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
	rtmp_chip.h

	Abstract:
	Ralink Wireless Chip related definition & structures

	Revision History:
	Who			When		  What
	--------	----------	  ----------------------------------------------
*/

#ifndef	__RTMP_CHIP_H__
#define	__RTMP_CHIP_H__

#include "rtmp_type.h"

#ifdef RT2860
#include "chip/rt2860.h"
#endif /* RT2860 // */
#ifdef RT2870
#include "chip/rt2870.h"
#endif /* RT2870 // */
#ifdef RT3070
#include "chip/rt3070.h"
#endif /* RT3070 // */
#ifdef RT3090
#include "chip/rt3090.h"
#endif /* RT3090 // */

/* We will have a cost down version which mac version is 0x3090xxxx */
/* */
/* RT3090A facts */
/* */
/* a) 2.4 GHz */
/* b) Replacement for RT3090 */
/* c) Internal LNA */
/* d) Interference over channel #14 */
/* e) New BBP features (e.g., SIG re-modulation) */
/* */
#define IS_RT3090A(_pAd)				((((_pAd)->MACVersion & 0xffff0000) == 0x30900000))

/* We will have a cost down version which mac version is 0x3090xxxx */
#define IS_RT3090(_pAd)				((((_pAd)->MACVersion & 0xffff0000) == 0x30710000) || (IS_RT3090A(_pAd)))

#define IS_RT3070(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x30700000)
#define IS_RT3071(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x30710000)
#define IS_RT2070(_pAd)		(((_pAd)->RfIcType == RFIC_2020) || ((_pAd)->EFuseTag == 0x27))

#define IS_RT30xx(_pAd)		(((_pAd)->MACVersion & 0xfff00000) == 0x30700000||IS_RT3090A(_pAd))
/*#define IS_RT305X(_pAd)               ((_pAd)->MACVersion == 0x28720200) */

/* RT3572, 3592, 3562, 3062 share the same MAC version */
#define IS_RT3572(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x35720000)
#define IS_VERSION_BEFORE_F(_pAd)			(((_pAd)->MACVersion&0xffff) <= 0x0211)
/* F version is 0x0212, E version is 0x0211. 309x can save more power after F version. */
#define IS_VERSION_AFTER_F(_pAd)			((((_pAd)->MACVersion&0xffff) >= 0x0212) || (((_pAd)->b3090ESpecialChip == TRUE)))
/* */
/* RT3390 facts */
/* */
/* a) Base on RT3090 (RF IC: RT3020) */
/* b) 2.4 GHz */
/* c) 1x1 */
/* d) Single chip */
/* e) Internal components: PA and LNA */
/* */
/*RT3390,RT3370 */
#define IS_RT3390(_pAd)				(((_pAd)->MACVersion & 0xFFFF0000) == 0x33900000)

/* ------------------------------------------------------ */
/* PCI registers - base address 0x0000 */
/* ------------------------------------------------------ */
#define CHIP_PCI_CFG		0x0000
#define CHIP_PCI_EECTRL		0x0004
#define CHIP_PCI_MCUCTRL	0x0008

#define OPT_14			0x114

#define RETRY_LIMIT		10

/* ------------------------------------------------------ */
/* BBP & RF     definition */
/* ------------------------------------------------------ */
#define	BUSY		                1
#define	IDLE		                0

/*------------------------------------------------------------------------- */
/* EEPROM definition */
/*------------------------------------------------------------------------- */
#define EEDO                        0x08
#define EEDI                        0x04
#define EECS                        0x02
#define EESK                        0x01
#define EERL                        0x80

#define EEPROM_WRITE_OPCODE         0x05
#define EEPROM_READ_OPCODE          0x06
#define EEPROM_EWDS_OPCODE          0x10
#define EEPROM_EWEN_OPCODE          0x13

#define NUM_EEPROM_BBP_PARMS		19	/* Include NIC Config 0, 1, CR, TX ALC step, BBPs */
#define NUM_EEPROM_TX_G_PARMS		7
#define EEPROM_NIC1_OFFSET          0x34	/* The address is from NIC config 0, not BBP register ID */
#define EEPROM_NIC2_OFFSET          0x36	/* The address is from NIC config 0, not BBP register ID */
#define EEPROM_BBP_BASE_OFFSET		0xf0	/* The address is from NIC config 0, not BBP register ID */
#define EEPROM_G_TX_PWR_OFFSET		0x52
#define EEPROM_G_TX2_PWR_OFFSET		0x60
#define EEPROM_LED1_OFFSET			0x3c
#define EEPROM_LED2_OFFSET			0x3e
#define EEPROM_LED3_OFFSET			0x40
#define EEPROM_LNA_OFFSET			0x44
#define EEPROM_RSSI_BG_OFFSET		0x46
#define EEPROM_TXMIXER_GAIN_2_4G	0x48
#define EEPROM_RSSI_A_OFFSET		0x4a
#define EEPROM_TXMIXER_GAIN_5G		0x4c
#define EEPROM_DEFINE_MAX_TXPWR		0x4e
#define EEPROM_TXPOWER_BYRATE_20MHZ_2_4G	0xde	/* 20MHZ 2.4G tx power. */
#define EEPROM_TXPOWER_BYRATE_40MHZ_2_4G	0xee	/* 40MHZ 2.4G tx power. */
#define EEPROM_TXPOWER_BYRATE_20MHZ_5G		0xfa	/* 20MHZ 5G tx power. */
#define EEPROM_TXPOWER_BYRATE_40MHZ_5G		0x10a	/* 40MHZ 5G tx power. */
#define EEPROM_A_TX_PWR_OFFSET      0x78
#define EEPROM_A_TX2_PWR_OFFSET      0xa6
/*#define EEPROM_Japan_TX_PWR_OFFSET      0x90 // 802.11j */
/*#define EEPROM_Japan_TX2_PWR_OFFSET      0xbe */
/*#define EEPROM_TSSI_REF_OFFSET        0x54 */
/*#define EEPROM_TSSI_DELTA_OFFSET      0x24 */
/*#define EEPROM_CCK_TX_PWR_OFFSET  0x62 */
/*#define EEPROM_CALIBRATE_OFFSET       0x7c */
#define EEPROM_VERSION_OFFSET       0x02
#define EEPROM_FREQ_OFFSET			0x3a
#define EEPROM_TXPOWER_BYRATE	0xde	/* 20MHZ power. */
#define EEPROM_TXPOWER_DELTA		0x50	/* 20MHZ AND 40 MHZ use different power. This is delta in 40MHZ. */
#define VALID_EEPROM_VERSION        1

/*
  *   EEPROM operation related marcos
  */
#define RT28xx_EEPROM_READ16(_pAd, _offset, _value)			\
	(_pAd)->chipOps.eeread((struct rt_rtmp_adapter *)(_pAd), (u16)(_offset), (u16 *)&(_value))

/* ------------------------------------------------------------------- */
/*  E2PROM data layout */
/* ------------------------------------------------------------------- */

/* */
/* MCU_LEDCS: MCU LED Control Setting. */
/* */
typedef union _MCU_LEDCS_STRUC {
	struct {
		u8 LedMode:7;
		u8 Polarity:1;
	} field;
	u8 word;
} MCU_LEDCS_STRUC, *PMCU_LEDCS_STRUC;

/* */
/* EEPROM antenna select format */
/* */
typedef union _EEPROM_ANTENNA_STRUC {
	struct {
		u16 RxPath:4;	/* 1: 1R, 2: 2R, 3: 3R */
		u16 TxPath:4;	/* 1: 1T, 2: 2T */
		u16 RfIcType:4;	/* see E2PROM document */
		u16 Rsv:4;
	} field;
	u16 word;
} EEPROM_ANTENNA_STRUC, *PEEPROM_ANTENNA_STRUC;

typedef union _EEPROM_NIC_CINFIG2_STRUC {
	struct {
		u16 HardwareRadioControl:1;	/* 1:enable, 0:disable */
		u16 DynamicTxAgcControl:1;	/* */
		u16 ExternalLNAForG:1;	/* */
		u16 ExternalLNAForA:1;	/* external LNA enable for 2.4G */
		u16 CardbusAcceleration:1;	/* ! NOTE: 0 - enable, 1 - disable */
		u16 BW40MSidebandForG:1;
		u16 BW40MSidebandForA:1;
		u16 EnableWPSPBC:1;	/* WPS PBC Control bit */
		u16 BW40MAvailForG:1;	/* 0:enable, 1:disable */
		u16 BW40MAvailForA:1;	/* 0:enable, 1:disable */
		u16 Rsv1:1;	/* must be 0 */
		u16 AntDiversity:1;	/* Antenna diversity */
		u16 Rsv2:3;	/* must be 0 */
		u16 DACTestBit:1;	/* control if driver should patch the DAC issue */
	} field;
	u16 word;
} EEPROM_NIC_CONFIG2_STRUC, *PEEPROM_NIC_CONFIG2_STRUC;

/* */
/* TX_PWR Value valid range 0xFA(-6) ~ 0x24(36) */
/* */
typedef union _EEPROM_TX_PWR_STRUC {
	struct {
		char Byte0;	/* Low Byte */
		char Byte1;	/* High Byte */
	} field;
	u16 word;
} EEPROM_TX_PWR_STRUC, *PEEPROM_TX_PWR_STRUC;

typedef union _EEPROM_VERSION_STRUC {
	struct {
		u8 FaeReleaseNumber;	/* Low Byte */
		u8 Version;	/* High Byte */
	} field;
	u16 word;
} EEPROM_VERSION_STRUC, *PEEPROM_VERSION_STRUC;

typedef union _EEPROM_LED_STRUC {
	struct {
		u16 PolarityRDY_G:1;	/* Polarity RDY_G setting. */
		u16 PolarityRDY_A:1;	/* Polarity RDY_A setting. */
		u16 PolarityACT:1;	/* Polarity ACT setting. */
		u16 PolarityGPIO_0:1;	/* Polarity GPIO#0 setting. */
		u16 PolarityGPIO_1:1;	/* Polarity GPIO#1 setting. */
		u16 PolarityGPIO_2:1;	/* Polarity GPIO#2 setting. */
		u16 PolarityGPIO_3:1;	/* Polarity GPIO#3 setting. */
		u16 PolarityGPIO_4:1;	/* Polarity GPIO#4 setting. */
		u16 LedMode:5;	/* Led mode. */
		u16 Rsvd:3;	/* Reserved */
	} field;
	u16 word;
} EEPROM_LED_STRUC, *PEEPROM_LED_STRUC;

typedef union _EEPROM_TXPOWER_DELTA_STRUC {
	struct {
		u8 DeltaValue:6;	/* Tx Power dalta value (MAX=4) */
		u8 Type:1;	/* 1: plus the delta value, 0: minus the delta value */
		u8 TxPowerEnable:1;	/* Enable */
	} field;
	u8 value;
} EEPROM_TXPOWER_DELTA_STRUC, *PEEPROM_TXPOWER_DELTA_STRUC;

#endif /* __RTMP_CHIP_H__ // */
