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


#ifndef	__RTMP_CHIP_H__
#define	__RTMP_CHIP_H__

#include "rtmp_type.h"

struct _RTMP_ADAPTER;




#ifdef RT3070
#include "chip/rt3070.h"
#endif /* RT3070 */






#ifdef RT3370
#include "chip/rt3370.h"
#endif /* RT3370 */




#ifdef RT5350
#include "chip/rt5350.h"
#endif /* RT5350 */

#ifdef RT3572
#include "chip/rt28xx.h"
#endif /* RT3572 */

#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
#include "chip/rt5390.h"
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */

#define IS_RT3090A(_pAd)    ((((_pAd)->MACVersion & 0xffff0000) == 0x30900000))

/* We will have a cost down version which mac version is 0x3090xxxx */
#define IS_RT3090(_pAd)     ((((_pAd)->MACVersion & 0xffff0000) == 0x30710000) || (IS_RT3090A(_pAd)))

#define IS_RT3070(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x30700000)
#define IS_RT3071(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x30710000)
#define IS_RT2070(_pAd)		(((_pAd)->RfIcType == RFIC_2020) || ((_pAd)->EFuseTag == 0x27))

#define IS_RT2860(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x28600000)
#define IS_RT2872(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x28720000)

#define IS_RT30xx(_pAd)		(((_pAd)->MACVersion & 0xfff00000) == 0x30700000||IS_RT3090A(_pAd)||IS_RT3390(_pAd))
/*#define IS_RT305X(_pAd)		((_pAd)->MACVersion == 0x28720200) */
#define IS_RT3052(_pAd)		(((_pAd)->MACVersion == 0x28720200) && (_pAd->Antenna.field.TxPath == 2))
#define IS_RT3050(_pAd)		(((_pAd)->MACVersion == 0x28720200) && ((_pAd)->RfIcType == RFIC_3020))
#define IS_RT3350(_pAd)		(((_pAd)->MACVersion == 0x28720200) && ((_pAd)->RfIcType == RFIC_3320))
#define IS_RT3352(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x33520000)
#define IS_RT5350(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x53500000)

/*
	RT3050: MAC_CSR0  [ Ver:Rev=0x28720200]
			RF IC Type: 5
			pAd->Antenna.field.TxPath = 1
			pAd->CommonCfg.CN: 33335452
			pAd->CommonCfg.CID = 102

	RT3350: MAC_CSR0  [ Ver:Rev=0x28720200]
			RF IC Type: 11
			pAd->Antenna.field.TxPath = 1
			pAd->CommonCfg.CN: 33335452
			pAd->CommonCfg.CID = 102
*/

/* RT3572, 3592, 3562, 3062 share the same MAC version */
#define IS_RT3572(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x35720000)

/* Check if it is RT3xxx, or Specified ID in registry for debug */
#define IS_DEV_RT3xxx(_pAd)( \
	(_pAd->DeviceID == NIC3090_PCIe_DEVICE_ID) || \
	(_pAd->DeviceID == NIC3091_PCIe_DEVICE_ID) || \
	(_pAd->DeviceID == NIC3092_PCIe_DEVICE_ID) || \
	(_pAd->DeviceID == NIC3592_PCIe_DEVICE_ID) || \
	((_pAd->DeviceID == NIC3593_PCI_OR_PCIe_DEVICE_ID) && (RT3593OverPCIe(_pAd))) \
)

#define IS_RT2883(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x28830000)
#define IS_RT3883(_pAd)		(((_pAd)->MACVersion & 0xffff0000) == 0x38830000)
#define IS_VERSION_BEFORE_F(_pAd)			(((_pAd)->MACVersion&0xffff) <= 0x0211)
/* F version is 0x0212, E version is 0x0211. 309x can save more power after F version. */
#define IS_VERSION_AFTER_F(_pAd)			((((_pAd)->MACVersion&0xffff) >= 0x0212) || (((_pAd)->b3090ESpecialChip == TRUE)))

/* 3593 */
#define IS_RT3593(_pAd) (((_pAd)->MACVersion & 0xFFFF0000) == 0x35930000)


/* RT5392 */
#define IS_RT5392(_pAd)   ((_pAd->MACVersion & 0xFFFF0000) == 0x53920000) /* Include RT5392, RT5372 and RT5362 */

/* RT5390 */
#define IS_RT5390(_pAd)   ((((_pAd)->MACVersion & 0xFFFF0000) == 0x53900000) ||IS_RT5392(_pAd))	/* Include RT5390,  RT5370, RT5392, RT5372 and RT5362 */

/* RT5390F */

#define IS_RT5390F(_pAd)	((IS_RT5390(_pAd)) && (((_pAd)->MACVersion & 0x0000FFFF) >= 0x0502))


/* PCIe interface NIC */

#define IS_MINI_CARD(_pAd) ((_pAd)->Antenna.field.BoardType == BOARD_TYPE_MINI_CARD)

/* 5390U (5370 using PCIe interface) */

#define IS_RT5390U(_pAd)   (IS_MINI_CARD(_pAd) && ((_pAd)->MACVersion & 0xFFFF0000) == 0x53900000)

/* RT5390BC8 (WiFi + BT) */



/* RT5390D */

#define IS_RT5390D(_pAd)	((IS_RT5390(_pAd)) && (((_pAd)->MACVersion & 0x0000FFFF) >= 0x0502))


/* RT5392C */

#define IS_RT5392C(_pAd)	((IS_RT5392(_pAd)) && (((_pAd)->MACVersion & 0x0000FFFF) >= 0x0222)) /* Include RT5392, RT5372 and RT5362 */

/* RT3592BC8 (WiFi + BT) */


/* Dual-band NIC (RF/BBP/MAC are in the same chip.) */

#define IS_RT_NEW_DUAL_BAND_NIC(_pAd) ((FALSE))


/* Is the NIC dual-band NIC? */

#define IS_DUAL_BAND_NIC(_pAd) (((_pAd->RfIcType == RFIC_2850) || (_pAd->RfIcType == RFIC_2750) || (_pAd->RfIcType == RFIC_3052)		\
								|| (_pAd->RfIcType == RFIC_3053) || (_pAd->RfIcType == RFIC_2853) || (_pAd->RfIcType == RFIC_3853) 	\
								|| IS_RT_NEW_DUAL_BAND_NIC(_pAd)) && !IS_RT5390(_pAd))


/* RT3593 over PCIe bus */
#define RT3593OverPCIe(_pAd) (IS_RT3593(_pAd) && (_pAd->CommonCfg.bPCIeBus == TRUE))

/* RT3593 over PCI bus */
#define RT3593OverPCI(_pAd) (IS_RT3593(_pAd) && (_pAd->CommonCfg.bPCIeBus == FALSE))

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
/* BBP & RF	definition */
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

#define VALID_EEPROM_VERSION        1
#define EEPROM_VERSION_OFFSET       0x02
#define EEPROM_NIC1_OFFSET          0x34	/* The address is from NIC config 0, not BBP register ID */
#define EEPROM_NIC2_OFFSET          0x36	/* The address is from NIC config 1, not BBP register ID */


#define EEPROM_COUNTRY_REGION			0x38

#define EEPROM_DEFINE_MAX_TXPWR			0x4e

#define EEPROM_FREQ_OFFSET			0x3a
#define EEPROM_LEDAG_CONF_OFFSET	0x3c
#define EEPROM_LEDACT_CONF_OFFSET	0x3e
#define EEPROM_LED_POLARITY_OFFSET	0x40

#define EEPROM_LNA_OFFSET			0x44

#define EEPROM_RSSI_BG_OFFSET			0x46
#define EEPROM_RSSI_A_OFFSET			0x4a
#define EEPROM_TXMIXER_GAIN_2_4G		0x48
#define EEPROM_TXMIXER_GAIN_5G			0x4c

#define EEPROM_TXPOWER_DELTA			0x50	/* 20MHZ AND 40 MHZ use different power. This is delta in 40MHZ. */

#define EEPROM_G_TX_PWR_OFFSET			0x52
#define EEPROM_G_TX2_PWR_OFFSET			0x60

#define EEPROM_G_TSSI_BOUND1			0x6e
#define EEPROM_G_TSSI_BOUND2			0x70
#define EEPROM_G_TSSI_BOUND3			0x72
#define EEPROM_G_TSSI_BOUND4			0x74
#define EEPROM_G_TSSI_BOUND5			0x76

#define EEPROM_A_TX_PWR_OFFSET      		0x78
#define EEPROM_A_TX2_PWR_OFFSET			0xa6

#define EEPROM_A_TSSI_BOUND1		0xd4
#define EEPROM_A_TSSI_BOUND2		0xd6
#define EEPROM_A_TSSI_BOUND3		0xd8
#define EEPROM_A_TSSI_BOUND4		0xda
#define EEPROM_A_TSSI_BOUND5		0xdc

#define EEPROM_ITXBF_CAL_RX0			0x1a0
#define EEPROM_ITXBF_CAL_TX0			0x1a2
#define EEPROM_ITXBF_CAL_RX1			0x1a4
#define EEPROM_ITXBF_CAL_TX1			0x1a6
#define EEPROM_ITXBF_CAL_RX2			0x1a8
#define EEPROM_ITXBF_CAL_TX2			0x1aa

#define EEPROM_TXPOWER_BYRATE 			0xde	/* 20MHZ power. */
#define EEPROM_TXPOWER_BYRATE_20MHZ_2_4G	0xde	/* 20MHZ 2.4G tx power. */
#define EEPROM_TXPOWER_BYRATE_40MHZ_2_4G	0xee	/* 40MHZ 2.4G tx power. */
#define EEPROM_TXPOWER_BYRATE_20MHZ_5G		0xfa	/* 20MHZ 5G tx power. */
#define EEPROM_TXPOWER_BYRATE_40MHZ_5G		0x10a	/* 40MHZ 5G tx power. */

#define EEPROM_BBP_BASE_OFFSET			0xf0	/* The address is from NIC config 0, not BBP register ID */

/* */
/* Bit mask for the Tx ALC and the Tx fine power control */
/* */
#define GET_TX_ALC_BIT_MASK					0x1F	/* Valid: 0~31, and in 0.5dB step */
#define GET_TX_FINE_POWER_CTRL_BIT_MASK	0xE0	/* Valid: 0~4, and in 0.1dB step */
#define NUMBER_OF_BITS_FOR_TX_ALC			5	/* The length, in bit, of the Tx ALC field */


/* TSSI gain and TSSI attenuation */

#define EEPROM_TSSI_GAIN_AND_ATTENUATION	0x76

/*#define EEPROM_Japan_TX_PWR_OFFSET      0x90 // 802.11j */
/*#define EEPROM_Japan_TX2_PWR_OFFSET      0xbe */
/*#define EEPROM_TSSI_REF_OFFSET	0x54 */
/*#define EEPROM_TSSI_DELTA_OFFSET	0x24 */
/*#define EEPROM_CCK_TX_PWR_OFFSET  0x62 */
/*#define EEPROM_CALIBRATE_OFFSET	0x7c */

#define EEPROM_NIC_CFG1_OFFSET		0
#define EEPROM_NIC_CFG2_OFFSET		1
#define EEPROM_NIC_CFG3_OFFSET		2
#define EEPROM_COUNTRY_REG_OFFSET	3
#define EEPROM_BBP_ARRAY_OFFSET		4

#ifdef RTMP_INTERNAL_TX_ALC
/* */
/* The TSSI over OFDM 54Mbps */
/* */
#define EEPROM_TSSI_OVER_OFDM_54		0x6E

/* */
/* The TSSI value/step (0.5 dB/unit) */
/* */
#define EEPROM_TSSI_STEP_OVER_2DOT4G	0x77

/* */
/* Per-channel Tx power offset (for the extended TSSI mode) */
/* */
#define EEPROM_TX_POWER_OFFSET_OVER_CH_1	0x6F
#define EEPROM_TX_POWER_OFFSET_OVER_CH_3	0x70
#define EEPROM_TX_POWER_OFFSET_OVER_CH_5	0x71
#define EEPROM_TX_POWER_OFFSET_OVER_CH_7	0x72
#define EEPROM_TX_POWER_OFFSET_OVER_CH_9	0x73
#define EEPROM_TX_POWER_OFFSET_OVER_CH_11	0x74
#define EEPROM_TX_POWER_OFFSET_OVER_CH_13	0x75

/* */
/* Tx power configuration (bit3:0 for Tx0 power setting and bit7:4 for Tx1 power setting) */
/* */
#define EEPROM_CCK_MCS0_MCS1				0xDE
#define EEPROM_CCK_MCS2_MCS3				0xDF
#define EEPROM_OFDM_MCS0_MCS1				0xE0
#define EEPROM_OFDM_MCS2_MCS3				0xE1
#define EEPROM_OFDM_MCS4_MCS5				0xE2
#define EEPROM_OFDM_MCS6_MCS7				0xE3
#define EEPROM_HT_MCS0_MCS1				0xE4
#define EEPROM_HT_MCS2_MCS3				0xE5
#define EEPROM_HT_MCS4_MCS5				0xE6
#define EEPROM_HT_MCS6_MCS7				0xE7
#define EEPROM_HT_MCS8_MCS9                     0xE8
#define EEPROM_HT_MCS10_MCS11                   0xE9
#define EEPROM_HT_MCS12_MCS13                   0xEA
#define EEPROM_HT_MCS14_MCS15                   0xEB
#define EEPROM_HT_USING_STBC_MCS0_MCS1	0xEC
#define EEPROM_HT_USING_STBC_MCS2_MCS3	0xED
#define EEPROM_HT_USING_STBC_MCS4_MCS5	0xEE
#define EEPROM_HT_USING_STBC_MCS6_MCS7	0xEF

/* */
/* Bit mask for the Tx ALC and the Tx fine power control */
/* */

#define DEFAULT_BBP_TX_FINE_POWER_CTRL 0

CHAR GetDesiredTSSI(
	IN struct _RTMP_ADAPTER		*pAd);
#endif /* RTMP_INTERNAL_TX_ALC */


#ifdef RT33xx
#define EEPROM_EVM_RF09  0x120
#define EEPROM_EVM_RF19  0x122
#define EEPROM_EVM_RF21  0x124
#define EEPROM_EVM_RF29  0x128
#endif /* RT33xx */

/*
  *   EEPROM operation related marcos
  */
#define RT28xx_EEPROM_READ16(_pAd, _offset, _value)			\
	(_pAd)->chipOps.eeread((RTMP_ADAPTER *)(_pAd), (USHORT)(_offset), (PUSHORT)&(_value))

#define RT28xx_EEPROM_WRITE16(_pAd, _offset, _value)		\
	(_pAd)->chipOps.eewrite((RTMP_ADAPTER *)(_pAd), (USHORT)(_offset), (USHORT)(_value))

/* ------------------------------------------------------------------- */
/*  E2PROM data layout */
/* ------------------------------------------------------------------- */

/* Board type */

#define BOARD_TYPE_MINI_CARD	0/* Mini card */
#define BOARD_TYPE_USB_PEN		1/* USB pen */


/* */
/* EEPROM antenna select format */
/* */
#ifdef RT_BIG_ENDIAN
typedef union _EEPROM_ANTENNA_STRUC {
	struct {
		USHORT 		RssiIndicationMode:1; // RSSI indication mode
		USHORT Rsv:1;
		USHORT BoardType:2; // 0: mini card; 1: USB pen		
		USHORT RfIcType:4;	/* see E2PROM document */
		USHORT TxPath:4;	/* 1: 1T, 2: 2T */
		USHORT RxPath:4;	/* 1: 1R, 2: 2R, 3: 3R */
	} field;
	USHORT word;
} EEPROM_ANTENNA_STRUC, *PEEPROM_ANTENNA_STRUC;
#else
typedef union _EEPROM_ANTENNA_STRUC {
	struct {
		USHORT RxPath:4;	/* 1: 1R, 2: 2R, 3: 3R */
		USHORT TxPath:4;	/* 1: 1T, 2: 2T */
		USHORT RfIcType:4;	/* see E2PROM document */
		USHORT BoardType:2; // 0: mini card; 1: USB pen
		USHORT Rsv:1;
		USHORT 		RssiIndicationMode:1; // RSSI indication mode		
	} field;
	USHORT word;
} EEPROM_ANTENNA_STRUC, *PEEPROM_ANTENNA_STRUC;
#endif

#ifdef RT_BIG_ENDIAN
typedef union _EEPROM_NIC_CINFIG2_STRUC {
	struct {
		USHORT DACTestBit:1;	/* control if driver should patch the DAC issue */
		USHORT CoexBit:1;
		USHORT bInternalTxALC:1;	/* Internal Tx ALC */
		USHORT AntOpt:1;	/* Fix Antenna Option: 0:Main; 1: Aux */
		USHORT AntDiversity:1;	/* Antenna diversity */
		USHORT Rsv1:1;	/* must be 0 */
		USHORT BW40MAvailForA:1;	/* 0:enable, 1:disable */
		USHORT BW40MAvailForG:1;	/* 0:enable, 1:disable */
		USHORT EnableWPSPBC:1;	/* WPS PBC Control bit */
		USHORT BW40MSidebandForA:1;
		USHORT BW40MSidebandForG:1;
		USHORT CardbusAcceleration:1;	/* !!! NOTE: 0 - enable, 1 - disable */
		USHORT ExternalLNAForA:1;	/* external LNA enable for 5G */
		USHORT ExternalLNAForG:1;	/* external LNA enable for 2.4G */
		USHORT DynamicTxAgcControl:1;	/* */
		USHORT HardwareRadioControl:1;	/* Whether RF is controlled by driver or HW. 1:enable hw control, 0:disable */
	} field;
	USHORT word;
} EEPROM_NIC_CONFIG2_STRUC, *PEEPROM_NIC_CONFIG2_STRUC;
#else
typedef union _EEPROM_NIC_CINFIG2_STRUC {
	struct {
		USHORT HardwareRadioControl:1;	/* 1:enable, 0:disable */
		USHORT DynamicTxAgcControl:1;	/* */
		USHORT ExternalLNAForG:1;	/* */
		USHORT ExternalLNAForA:1;	/* external LNA enable for 2.4G */
		USHORT CardbusAcceleration:1;	/* !!! NOTE: 0 - enable, 1 - disable */
		USHORT BW40MSidebandForG:1;
		USHORT BW40MSidebandForA:1;
		USHORT EnableWPSPBC:1;	/* WPS PBC Control bit */
		USHORT BW40MAvailForG:1;	/* 0:enable, 1:disable */
		USHORT BW40MAvailForA:1;	/* 0:enable, 1:disable */
		USHORT Rsv1:1;	/* must be 0 */
		USHORT AntDiversity:1;	/* Antenna diversity */
		USHORT AntOpt:1;	/* Fix Antenna Option: 0:Main; 1: Aux */
		USHORT bInternalTxALC:1;	/* Internal Tx ALC */
		USHORT CoexBit:1;
		USHORT DACTestBit:1;	/* control if driver should patch the DAC issue */
	} field;
	USHORT word;
} EEPROM_NIC_CONFIG2_STRUC, *PEEPROM_NIC_CONFIG2_STRUC;
#endif


/* */
/* TX_PWR Value valid range 0xFA(-6) ~ 0x24(36) */
/* */
#ifdef RT_BIG_ENDIAN
typedef union _EEPROM_TX_PWR_STRUC {
	struct {
		signed char Byte1;	/* High Byte */
		signed char Byte0;	/* Low Byte */
	} field;
	USHORT word;
} EEPROM_TX_PWR_STRUC, *PEEPROM_TX_PWR_STRUC;
#else
typedef union _EEPROM_TX_PWR_STRUC {
	struct {
		signed char Byte0;	/* Low Byte */
		signed char Byte1;	/* High Byte */
	} field;
	USHORT word;
} EEPROM_TX_PWR_STRUC, *PEEPROM_TX_PWR_STRUC;
#endif

#ifdef RT_BIG_ENDIAN
typedef union _EEPROM_VERSION_STRUC {
	struct {
		UCHAR Version;	/* High Byte */
		UCHAR FaeReleaseNumber;	/* Low Byte */
	} field;
	USHORT word;
} EEPROM_VERSION_STRUC, *PEEPROM_VERSION_STRUC;
#else
typedef union _EEPROM_VERSION_STRUC {
	struct {
		UCHAR FaeReleaseNumber;	/* Low Byte */
		UCHAR Version;	/* High Byte */
	} field;
	USHORT word;
} EEPROM_VERSION_STRUC, *PEEPROM_VERSION_STRUC;
#endif

#ifdef RT_BIG_ENDIAN
typedef union _EEPROM_LED_STRUC {
	struct {
		USHORT Rsvd:3;	/* Reserved */
		USHORT LedMode:5;	/* Led mode. */
		USHORT PolarityGPIO_4:1;	/* Polarity GPIO#4 setting. */
		USHORT PolarityGPIO_3:1;	/* Polarity GPIO#3 setting. */
		USHORT PolarityGPIO_2:1;	/* Polarity GPIO#2 setting. */
		USHORT PolarityGPIO_1:1;	/* Polarity GPIO#1 setting. */
		USHORT PolarityGPIO_0:1;	/* Polarity GPIO#0 setting. */
		USHORT PolarityACT:1;	/* Polarity ACT setting. */
		USHORT PolarityRDY_A:1;	/* Polarity RDY_A setting. */
		USHORT PolarityRDY_G:1;	/* Polarity RDY_G setting. */
	} field;
	USHORT word;
} EEPROM_LED_STRUC, *PEEPROM_LED_STRUC;
#else
typedef union _EEPROM_LED_STRUC {
	struct {
		USHORT PolarityRDY_G:1;	/* Polarity RDY_G setting. */
		USHORT PolarityRDY_A:1;	/* Polarity RDY_A setting. */
		USHORT PolarityACT:1;	/* Polarity ACT setting. */
		USHORT PolarityGPIO_0:1;	/* Polarity GPIO#0 setting. */
		USHORT PolarityGPIO_1:1;	/* Polarity GPIO#1 setting. */
		USHORT PolarityGPIO_2:1;	/* Polarity GPIO#2 setting. */
		USHORT PolarityGPIO_3:1;	/* Polarity GPIO#3 setting. */
		USHORT PolarityGPIO_4:1;	/* Polarity GPIO#4 setting. */
		USHORT LedMode:5;	/* Led mode. */
		USHORT Rsvd:3;	/* Reserved */
	} field;
	USHORT word;
} EEPROM_LED_STRUC, *PEEPROM_LED_STRUC;
#endif

#ifdef RT_BIG_ENDIAN
typedef union _EEPROM_TXPOWER_DELTA_STRUC {
	struct {
		UCHAR TxPowerEnable:1;	/* Enable */
		UCHAR Type:1;	/* 1: plus the delta value, 0: minus the delta value */
		UCHAR DeltaValue:6;	/* Tx Power dalta value (MAX=4) */
	} field;
	UCHAR value;
} EEPROM_TXPOWER_DELTA_STRUC, *PEEPROM_TXPOWER_DELTA_STRUC;
#else
typedef union _EEPROM_TXPOWER_DELTA_STRUC {
	struct {
		UCHAR DeltaValue:6;	/* Tx Power dalta value (MAX=4) */
		UCHAR Type:1;	/* 1: plus the delta value, 0: minus the delta value */
		UCHAR TxPowerEnable:1;	/* Enable */
	} field;
	UCHAR value;
} EEPROM_TXPOWER_DELTA_STRUC, *PEEPROM_TXPOWER_DELTA_STRUC;
#endif


#ifdef RT_BIG_ENDIAN
typedef union _EEPROM_TX_PWR_OFFSET_STRUC
{
	struct
	{
		UCHAR	Byte1;	/* High Byte */
		UCHAR	Byte0;	/* Low Byte */
	} field;
	
	USHORT		word;
} EEPROM_TX_PWR_OFFSET_STRUC, *PEEPROM_TX_PWR_OFFSET_STRUC;
#else
typedef union _EEPROM_TX_PWR_OFFSET_STRUC
{
	struct
	{
		UCHAR	Byte0;	/* Low Byte */
		UCHAR	Byte1;	/* High Byte */
	} field;

	USHORT		word;
} EEPROM_TX_PWR_OFFSET_STRUC, *PEEPROM_TX_PWR_OFFSET_STRUC;
#endif // RT_BIG_ENDIAN //
/*
	2860: 28xx
	2870: 28xx

	30xx:
		3090
		3070
		2070 3070

	33xx:	30xx
		3390 3090
		3370 3070

	35xx:	30xx
		3572, 2870, 28xx
		3062, 2860, 28xx
		3562, 2860, 28xx

	3593, 28xx, 30xx, 35xx

	< Note: 3050, 3052, 3350 can not be compiled simultaneously. >
	305x:
		3052
		3050
		3350, 3050

	3352: 305x

	2880: 28xx
	2883:
	3883:
*/

struct _RTMP_CHIP_CAP_ {
	/* register */
	REG_PAIR *pRFRegTable;
	REG_PAIR *pBBPRegTable;
	UCHAR bbpRegTbSize;

	UINT32 MaxNumOfRfId;
	UINT32 MaxNumOfBbpId;

#define RF_REG_WT_METHOD_NONE			0
#define RF_REG_WT_METHOD_STEP_ON		1
	UCHAR RfReg17WtMethod;

	/* beacon */
	BOOLEAN FlgIsSupSpecBcnBuf;	/* SPECIFIC_BCN_BUF_SUPPORT */
	UINT8 BcnMaxNum;	/* software use */
	UINT8 BcnMaxHwNum;	/* hardware limitation */
	UINT8 WcidHwRsvNum;	/* hardware available WCID number */
	UINT16 BcnMaxHwSize;	/* hardware maximum beacon size */
	UINT16 BcnBase[HW_BEACON_MAX_NUM];	/* hardware beacon base address */

	/* function */
	/* use UINT8, not bit-or to speed up driver */
	BOOLEAN FlgIsHwWapiSup;

	/* signal */
#define SNR_FORMULA1		0	/* ((0xeb     - pAd->StaCfg.LastSNR0) * 3) / 16; */
#define SNR_FORMULA2		1	/* (pAd->StaCfg.LastSNR0 * 3 + 8) >> 4; */
#define SNR_FORMULA3		2	/* (pAd->StaCfg.LastSNR0) * 3) / 16; */
	UINT8 SnrFormula;

#ifdef RTMP_INTERNAL_TX_ALC
	UINT8 TxAlcTxPowerUpperBound;
	UINT8 TxAlcMaxMCS;
#endif /* RTMP_INTERNAL_TX_ALC */

#ifdef RTMP_EFUSE_SUPPORT
	UINT16 EFUSE_USAGE_MAP_START;
	UINT16 EFUSE_USAGE_MAP_END;
	UINT8 EFUSE_USAGE_MAP_SIZE;
#endif				/* RTMP_EFUSE_SUPPORT */

	BOOLEAN	FlgIsVcoReCalSup;
	BOOLEAN FlgIsHwAntennaDiversitySup;
};

struct _RTMP_CHIP_OP_ {
	/*  Calibration access related callback functions */
	int (*eeinit)(struct _RTMP_ADAPTER *pAd);
	int (*eeread)(struct _RTMP_ADAPTER *pAd, USHORT offset, PUSHORT pValue);
	int (*eewrite)(struct _RTMP_ADAPTER *pAd, USHORT offset, USHORT value);

	/* MCU related callback functions */
	int (*loadFirmware)(struct _RTMP_ADAPTER *pAd);
	int (*eraseFirmware)(struct _RTMP_ADAPTER *pAd);
	int (*sendCommandToMcu)(struct _RTMP_ADAPTER *pAd, UCHAR cmd, UCHAR token, UCHAR arg0, UCHAR arg1, BOOLEAN FlgIsNeedLocked);	/* int (*sendCommandToMcu)(RTMP_ADAPTER *pAd, UCHAR cmd, UCHAR token, UCHAR arg0, UCHAR arg1); */

	void (*AsicRfInit)(struct _RTMP_ADAPTER *pAd);
	void (*AsicBbpInit)(struct _RTMP_ADAPTER *pAd);
	void (*AsicMacInit)(struct _RTMP_ADAPTER *pAd);

	void (*AsicRfTurnOn)(struct _RTMP_ADAPTER *pAd);
	void (*AsicRfTurnOff)(struct _RTMP_ADAPTER *pAd);
	void (*AsicReverseRfFromSleepMode)(struct _RTMP_ADAPTER *pAd, BOOLEAN FlgIsInitState);
	void (*AsicHaltAction)(struct _RTMP_ADAPTER *pAd);
	void (*AsicEeBufferInit)(struct _RTMP_ADAPTER *pAd);

	/* Power save */
	VOID (*EnableAPMIMOPS)(IN struct _RTMP_ADAPTER *pAd, IN BOOLEAN ReduceCorePower);
	VOID (*DisableAPMIMOPS)(IN struct _RTMP_ADAPTER *pAd);

	/* Chip tuning */
	VOID (*RxSensitivityTuning)(IN struct _RTMP_ADAPTER *pAd);

	/* MAC */
	VOID (*ChipResumeMsduTransmission)(IN struct _RTMP_ADAPTER *pAd);

	/* BBP adjust */
	VOID (*ChipBBPAdjust)(IN struct _RTMP_ADAPTER *pAd);
	UCHAR (*ChipStaBBPAdjust)(
				IN struct _RTMP_ADAPTER *pAd,
				IN CHAR					Rssi,
				IN UCHAR				R66);

	/* Channel */
	VOID (*RTMPSetAGCInitValue)(
				IN struct _RTMP_ADAPTER *pAd,
				IN UCHAR				BandWidth);
	VOID (*ChipSwitchChannel)(
				IN struct _RTMP_ADAPTER *pAd,
				IN UCHAR				Channel,
				IN BOOLEAN				bScan);


	/* TX ALC */
	VOID (*InitDesiredTSSITable)(IN struct _RTMP_ADAPTER *pAd);
	int (*ATETssiCalibration)(
				IN struct _RTMP_ADAPTER 	*pAd,
				IN PSTRING				arg);
	int (*ATETssiCalibrationExtend)(
				IN struct _RTMP_ADAPTER 	*pAd,
				IN PSTRING				arg);
	VOID (*AsicTxAlcGetAutoAgcOffset)(
				IN struct _RTMP_ADAPTER	*pAd,
				IN PCHAR				pDeltaPwr,
				IN PCHAR				pTotalDeltaPwr,
				IN PCHAR				pAgcCompensate,
				IN PUCHAR				pBbpR49);

	int (*ATEReadExternalTSSI)(
				IN struct _RTMP_ADAPTER 	*pAd,
				IN PSTRING				arg);

	/* Antenna */
	VOID (*AsicAntennaDefaultReset)(
				IN struct _RTMP_ADAPTER	*pAd,
				IN EEPROM_ANTENNA_STRUC	*pAntenna);

	VOID (*SetRxAnt)(
				IN struct _RTMP_ADAPTER	*pAd,
				IN UCHAR			Ant);

	/* EEPROM */
	VOID (*NICInitAsicFromEEPROM)(IN struct _RTMP_ADAPTER *pAd);

	/* Vendor Specific */
	VOID (*VdrTuning1)(IN struct _RTMP_ADAPTER *pAd);

	/* Frequence Calibration */
	VOID (*AsicFreqCalInit)(IN struct _RTMP_ADAPTER *pAd);
	VOID (*AsicFreqCalStop)(IN struct _RTMP_ADAPTER *pAd);
	VOID (*AsicFreqCal)(IN struct _RTMP_ADAPTER *pAd);
	CHAR (*AsicFreqOffsetGet)(
				IN struct _RTMP_ADAPTER	*pAd,
				IN struct _RXWI_STRUC	*pRxWI);

	VOID (*AsicVCOCalibration)(IN struct _RTMP_ADAPTER *pAd);

	/* Others */
	VOID (*NetDevNickNameInit)(IN struct _RTMP_ADAPTER *pAd);

	/* Others */
	VOID (*ChipSpecific)(
				IN struct _RTMP_ADAPTER	*pAd,
				IN UINT32				StateId,
				IN UINT32				FuncId,
				IN VOID					*pData,
				IN ULONG				Data);
	VOID (*AsicResetBbpAgent)(IN struct _RTMP_ADAPTER *pAd);
};

#define RTMP_CHIP_ENABLE_AP_MIMOPS(__pAd, __ReduceCorePower)				\
		if (__pAd->chipOps.EnableAPMIMOPS != NULL)							\
			__pAd->chipOps.EnableAPMIMOPS(__pAd, __ReduceCorePower)

#define RTMP_CHIP_DISABLE_AP_MIMOPS(__pAd)									\
		if (__pAd->chipOps.DisableAPMIMOPS != NULL)							\
			__pAd->chipOps.DisableAPMIMOPS(__pAd)

#define RTMP_CHIP_RX_SENSITIVITY_TUNING(__pAd)								\
		if (__pAd->chipOps.RxSensitivityTuning != NULL)						\
			__pAd->chipOps.RxSensitivityTuning(__pAd)

#define RTMP_VDR_TUNING1(__pAd)												\
		if (__pAd->chipOps.VdrTuning1 != NULL)								\
			__pAd->chipOps.VdrTuning1(__pAd)

#define RTMP_CHIP_MSDU_TRANSMISSION_RESUME(__pAd)							\
		if (__pAd->chipOps.ChipResumeMsduTransmission != NULL)				\
			__pAd->chipOps.ChipResumeMsduTransmission(__pAd)

#define RTMP_CHIP_ASIC_BBP_ADJUST(__pAd)									\
		if (__pAd->chipOps.ChipBBPAdjust != NULL)							\
			__pAd->chipOps.ChipBBPAdjust(__pAd)

#define RTMP_CHIP_ASIC_STA_BBP_ADJUST(__pAd, __Rssi, __R66)					\
		if (__pAd->chipOps.ChipStaBBPAdjust != NULL)						\
			__R66 = __pAd->chipOps.ChipStaBBPAdjust(__pAd, __Rssi, __R66)

#define RTMP_CHIP_ASIC_SWITCH_CHANNEL(__pAd, __Channel, __bScan)			\
		if (__pAd->chipOps.ChipSwitchChannel != NULL)						\
			__pAd->chipOps.ChipSwitchChannel(__pAd, __Channel, __bScan);	\
		else																\
			DBGPRINT(RT_DEBUG_ERROR, ("No switch channel function!!!\n"))

#define RTMP_CHIP_ASIC_TSSI_TABLE_INIT(__pAd)								\
		if (__pAd->chipOps.InitDesiredTSSITable != NULL)					\
			__pAd->chipOps.InitDesiredTSSITable(__pAd)

#define RTMP_CHIP_ATE_TSSI_CALIBRATION(__pAd, __pData)					\
		if (__pAd->chipOps.ATETssiCalibration != NULL)					\
			__pAd->chipOps.ATETssiCalibration(__pAd, __pData)

#define RTMP_CHIP_ATE_TSSI_CALIBRATION_EXTEND(__pAd, __pData)			\
		if (__pAd->chipOps.ATETssiCalibrationExtend != NULL)				\
			__pAd->chipOps.ATETssiCalibrationExtend(__pAd, __pData)	

#define RTMP_CHIP_ATE_READ_EXTERNAL_TSSI(__pAd, __pData)					\
		if (__pAd->chipOps.ATEReadExternalTSSI != NULL)					\
			__pAd->chipOps.ATEReadExternalTSSI(__pAd, __pData)	

#define RTMP_CHIP_ASIC_AUTO_AGC_OFFSET_GET(									\
		__pAd, __pDeltaPwr, __pTotalDeltaPwr, __pAgcCompensate, __pBbpR49)	\
		if (__pAd->chipOps.AsicTxAlcGetAutoAgcOffset != NULL)				\
			__pAd->chipOps.AsicTxAlcGetAutoAgcOffset(						\
		__pAd, __pDeltaPwr, __pTotalDeltaPwr, __pAgcCompensate, __pBbpR49)

#define RTMP_CHIP_ASIC_AGC_INIT_VALUE_SET(__pAd, __Bandwidth)				\
		if (__pAd->chipOps.RTMPSetAGCInitValue != NULL)						\
			__pAd->chipOps.RTMPSetAGCInitValue(__pAd, __Bandwidth)

#define RTMP_CHIP_ASIC_FREQ_CAL_INIT(__pAd)									\
		if (__pAd->chipOps.AsicFreqCalInit != NULL)							\
			__pAd->chipOps.AsicFreqCalInit(__pAd)

#define RTMP_CHIP_ASIC_FREQ_CAL_STOP(__pAd)									\
		if (__pAd->chipOps.AsicFreqCalStop != NULL)							\
			__pAd->chipOps.AsicFreqCalStop(__pAd)

#define RTMP_CHIP_ASIC_FREQ_CAL(__pAd)										\
		if (__pAd->chipOps.AsicFreqCal != NULL)								\
			 __pAd->chipOps.AsicFreqCal(__pAd)

#define RTMP_CHIP_ASIC_FREQ_OFFSET_GET(__pAd, __pRxWI, __Offset)			\
		if (__pAd->chipOps.AsicFreqOffsetGet != NULL)						\
			__Offset = __pAd->chipOps.AsicFreqOffsetGet(__pAd, __pRxWI)

#define RTMP_CHIP_ASIC_VCO_CAL(__pAd)										\
		if (__pAd->chipOps.AsicVCOCalibration != NULL)						\
			__pAd->chipOps.AsicVCOCalibration(__pAd)

#define RTMP_CHIP_ANTENNA_INFO_DEFAULT_RESET(__pAd, __pAntenna)				\
		if (__pAd->chipOps.AsicAntennaDefaultReset != NULL)					\
			__pAd->chipOps.AsicAntennaDefaultReset(__pAd, __pAntenna)

#define RTMP_NET_DEV_NICKNAME_INIT(__pAd)									\
		if (__pAd->chipOps.NetDevNickNameInit != NULL)						\
			__pAd->chipOps.NetDevNickNameInit(__pAd)

#define RTMP_EEPROM_ASIC_INIT(__pAd)										\
		if (__pAd->chipOps.NICInitAsicFromEEPROM != NULL)					\
			__pAd->chipOps.NICInitAsicFromEEPROM(__pAd)

#define RTMP_CHIP_SPECIFIC(__pAd, __StateId, __FuncId, __pData, __Data)		\
		if (__pAd->chipOps.ChipSpecific != NULL)							\
			__pAd->chipOps.ChipSpecific(__pAd, __StateId, __FuncId, __pData, __Data)

#define RTMP_CHIP_ASIC_RESET_BBP_AGENT(									\
		__pAd)	\
		if (__pAd->chipOps.AsicResetBbpAgent != NULL)				\
			__pAd->chipOps.AsicResetBbpAgent(						\
		__pAd)

/*
	Used in RTMP_CHIP_SPECIFIC(), FuncId

	Note: These definitions only can be used "once".

	EX: You can not use RTMP_CHIP_SPEC_WLAN_MODE_CHANGE in a() and b().
	If you want to use RTMP_CHIP_SPEC_WLAN_MODE_CHANGE in b(), you need to
	create another function name. Or for chip1, RTMP_CHIP_SPEC_WLAN_MODE_CHANGE
	can be used in a() and b() but for chip2, RTMP_CHIP_SPEC_WLAN_MODE_CHANGE
	can only be used in a(). It will be confused for different chips.
*/
/* RT305x */
#define RTMP_CHIP_SPEC_STATE_WMODE_CMD							0x00000001
#define RTMP_CHIP_SPEC_WLAN_MODE_CHANGE							0x00000001

#define RTMP_CHIP_SPEC_STATE_INIT								0x00000002
#define RTMP_CHIP_SPEC_INITIALIZATION							0x00000001

#define RTMP_CHIP_SEPC_STATE_HT_SET								0x00000003
#define RTMP_CHIP_SPEC_HT_MODE_CHANGE							0x00000001

#define RTMP_CHIP_SPEC_STATE_AP_PERIODIC						0x00000004
#define RTMP_CHIP_SPEC_HIGH_POWER_PATCH_AP						0x00000001

#define RTMP_CHIP_SPEC_STATE_STA_PERIODIC						0x00000005
#define RTMP_CHIP_SPEC_HIGH_POWER_PATCH_STA						0x00000001

/* function prototype */
VOID RtmpChipOpsHook(
	IN VOID *pCB);

VOID RtmpChipBcnSpecInit(
	IN struct _RTMP_ADAPTER *pAd);

UINT32 SetHWAntennaDivsersity(
	IN struct _RTMP_ADAPTER		*pAd,
	IN BOOLEAN					Enable);

VOID AsicGetTxPowerOffset(
	IN struct _RTMP_ADAPTER		*pAd,
	IN PULONG					TxPwr);

/* global variable */
extern FREQUENCY_ITEM RtmpFreqItems3020[];
extern FREQUENCY_ITEM FreqItems3020_Xtal20M[];
extern UCHAR NUM_OF_3020_CHNL;
extern FREQUENCY_ITEM *FreqItems3020;
extern RTMP_RF_REGS RF2850RegTable[];
extern UCHAR NUM_OF_2850_CHNL;

#endif /* __RTMP_CHIP_H__ */
