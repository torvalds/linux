/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/CFG_Wifi_File.h#1
*/

/*! \file   CFG_Wifi_File.h
    \brief  Collection of NVRAM structure used for YuSu project

    In this file we collect all compiler flags and detail the driver behavior if
    enable/disable such switch or adjust numeric parameters.
*/

/*
** Log: CFG_Wifi_File.h
 *
 * 09 08 2011 cm.chang
 * [WCXRP00000969] [MT6620 Wi-Fi][Driver][FW] Channel list for 5G band based on country code
 * Use new fields ucChannelListMap and ucChannelListIndex in NVRAM
 *
 * 08 31 2011 cm.chang
 * [WCXRP00000969] [MT6620 Wi-Fi][Driver][FW] Channel list for 5G band based on country code
 * .
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * add MT6628-specific definitions.
 *
 * 08 09 2011 cp.wu
 * [WCXRP00000702] [MT5931][Driver] Modify initialization sequence for E1 ASIC
 * [WCXRP00000913] [MT6620 Wi-Fi] create repository of source code dedicated for MT6620 E6 ASIC
 * add CCK-DSSS TX-PWR control field in NVRAM and CMD definition for MT5931-MP
 *
 * 05 27 2011 cp.wu
 * [WCXRP00000749] [MT6620 Wi-Fi][Driver] Add band edge tx power control to Wi-Fi NVRAM
 * update NVRAM data structure definition.
 *
 * 03 10 2011 cp.wu
 * [WCXRP00000532] [MT6620 Wi-Fi][Driver] Migrate NVRAM configuration procedures from MT6620 E2 to MT6620 E3
 * deprecate configuration used by MT6620 E2
 *
 * 10 26 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * [WCXRP00000137] [MT6620 Wi-Fi] [FW] Support NIC capability query command
 * 1) update NVRAM content template to ver 1.02
 * 2) add compile option for querying NIC capability (default: off)
 * 3) modify AIS 5GHz support to run-time option, which could be turned on by registry or NVRAM setting
 * 4) correct auto-rate compiler error under linux (treat warning as error)
 * 5) simplify usage of NVRAM and REG_INFO_T
 * 6) add version checking between driver and firmware
 *
 * 10 25 2010 cp.wu
 * [WCXRP00000133] [MT6620 Wi-Fi] [FW][Driver] Change TX power offset band definition
 * follow-up for CMD_5G_PWR_OFFSET_T definition change
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * 1) add NVRAM access API
 * 2) fake scanning result when NVRAM doesn't exist and/or version mismatch. (off by compiler option)
 * 3) add OID implementation for NVRAM read/write service
 *
 * 09 23 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * add skeleton for NVRAM integration
 *
*/

#ifndef _CFG_WIFI_FILE_H
#define _CFG_WIFI_FILE_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "gl_typedef.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
/* duplicated from nic_cmd_event.h to avoid header dependency */
typedef struct _TX_PWR_PARAM_T {
	INT_8 cTxPwr2G4Cck;	/* signed, in unit of 0.5dBm */
	INT_8 acReserved[3];	/* form MT6628 acReserved[0]=cTxPwr2G4Dsss */
	INT_8 cTxPwr2G4OFDM_BPSK;
	INT_8 cTxPwr2G4OFDM_QPSK;
	INT_8 cTxPwr2G4OFDM_16QAM;
	INT_8 cTxPwr2G4OFDM_Reserved;
	INT_8 cTxPwr2G4OFDM_48Mbps;
	INT_8 cTxPwr2G4OFDM_54Mbps;

	INT_8 cTxPwr2G4HT20_BPSK;
	INT_8 cTxPwr2G4HT20_QPSK;
	INT_8 cTxPwr2G4HT20_16QAM;
	INT_8 cTxPwr2G4HT20_MCS5;
	INT_8 cTxPwr2G4HT20_MCS6;
	INT_8 cTxPwr2G4HT20_MCS7;

	INT_8 cTxPwr2G4HT40_BPSK;
	INT_8 cTxPwr2G4HT40_QPSK;
	INT_8 cTxPwr2G4HT40_16QAM;
	INT_8 cTxPwr2G4HT40_MCS5;
	INT_8 cTxPwr2G4HT40_MCS6;
	INT_8 cTxPwr2G4HT40_MCS7;

	INT_8 cTxPwr5GOFDM_BPSK;
	INT_8 cTxPwr5GOFDM_QPSK;
	INT_8 cTxPwr5GOFDM_16QAM;
	INT_8 cTxPwr5GOFDM_Reserved;
	INT_8 cTxPwr5GOFDM_48Mbps;
	INT_8 cTxPwr5GOFDM_54Mbps;

	INT_8 cTxPwr5GHT20_BPSK;
	INT_8 cTxPwr5GHT20_QPSK;
	INT_8 cTxPwr5GHT20_16QAM;
	INT_8 cTxPwr5GHT20_MCS5;
	INT_8 cTxPwr5GHT20_MCS6;
	INT_8 cTxPwr5GHT20_MCS7;

	INT_8 cTxPwr5GHT40_BPSK;
	INT_8 cTxPwr5GHT40_QPSK;
	INT_8 cTxPwr5GHT40_16QAM;
	INT_8 cTxPwr5GHT40_MCS5;
	INT_8 cTxPwr5GHT40_MCS6;
	INT_8 cTxPwr5GHT40_MCS7;
} TX_PWR_PARAM_T, *P_TX_PWR_PARAM_T;

typedef struct _PWR_5G_OFFSET_T {
	INT_8 cOffsetBand0;	/* 4.915-4.980G */
	INT_8 cOffsetBand1;	/* 5.000-5.080G */
	INT_8 cOffsetBand2;	/* 5.160-5.180G */
	INT_8 cOffsetBand3;	/* 5.200-5.280G */
	INT_8 cOffsetBand4;	/* 5.300-5.340G */
	INT_8 cOffsetBand5;	/* 5.500-5.580G */
	INT_8 cOffsetBand6;	/* 5.600-5.680G */
	INT_8 cOffsetBand7;	/* 5.700-5.825G */
} PWR_5G_OFFSET_T, *P_PWR_5G_OFFSET_T;

typedef struct _PWR_PARAM_T {
	UINT_32 au4Data[28];
	UINT_32 u4RefValue1;
	UINT_32 u4RefValue2;
} PWR_PARAM_T, *P_PWR_PARAM_T;

typedef struct _MT6620_CFG_PARAM_STRUCT {
	/* 256 bytes of MP data */
	UINT_16 u2Part1OwnVersion;
	UINT_16 u2Part1PeerVersion;
	UINT_8 aucMacAddress[6];
	UINT_8 aucCountryCode[2];
	TX_PWR_PARAM_T rTxPwr;
	UINT_8 aucEFUSE[144];
	UINT_8 ucTxPwrValid;
	UINT_8 ucSupport5GBand;
	UINT_8 fg2G4BandEdgePwrUsed;
	INT_8 cBandEdgeMaxPwrCCK;
	INT_8 cBandEdgeMaxPwrOFDM20;
	INT_8 cBandEdgeMaxPwrOFDM40;

	UINT_8 ucRegChannelListMap;
	UINT_8 ucRegChannelListIndex;
	UINT_8 aucRegSubbandInfo[36];

	UINT_8 aucReserved2[256 - 240];

	/* 256 bytes of function data */
	UINT_16 u2Part2OwnVersion;
	UINT_16 u2Part2PeerVersion;
	UINT_8 uc2G4BwFixed20M;
	UINT_8 uc5GBwFixed20M;
	UINT_8 ucEnable5GBand;
	UINT_8 aucPreTailReserved;
	UINT_8 uc2GRssiCompensation;
	UINT_8 uc5GRssiCompensation;
	UINT_8 fgRssiCompensationValidbit;
	UINT_8 ucRxAntennanumber;
	UINT_8 aucTailReserved[256 - 12];
} MT6620_CFG_PARAM_STRUCT, *P_MT6620_CFG_PARAM_STRUCT, WIFI_CFG_PARAM_STRUCT, *P_WIFI_CFG_PARAM_STRUCT;

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#ifndef DATA_STRUCT_INSPECTING_ASSERT
#define DATA_STRUCT_INSPECTING_ASSERT(expr) \
{ \
	switch (0) {case 0: case (expr): default:; } \
}
#endif

#define CFG_FILE_WIFI_REC_SIZE    sizeof(WIFI_CFG_PARAM_STRUCT)

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#ifndef _lint
/* We don't have to call following function to inspect the data structure.
 * It will check automatically while at compile time.
 * We'll need this to guarantee the same member order in different structures
 * to simply handling effort in some functions.
 */
static inline VOID nvramOffsetCheck(VOID)
{
	DATA_STRUCT_INSPECTING_ASSERT(OFFSET_OF(WIFI_CFG_PARAM_STRUCT, u2Part2OwnVersion) == 256);

	DATA_STRUCT_INSPECTING_ASSERT(sizeof(WIFI_CFG_PARAM_STRUCT) == 512);

	DATA_STRUCT_INSPECTING_ASSERT((OFFSET_OF(WIFI_CFG_PARAM_STRUCT, aucEFUSE) & 0x0001) == 0);

	DATA_STRUCT_INSPECTING_ASSERT((OFFSET_OF(WIFI_CFG_PARAM_STRUCT, aucRegSubbandInfo) & 0x0001) == 0);
}
#endif

#endif /* _CFG_WIFI_FILE_H */
