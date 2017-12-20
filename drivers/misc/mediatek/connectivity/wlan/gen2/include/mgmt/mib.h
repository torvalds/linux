/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/mgmt/mib.h#1
*/

/*! \file  mib.h
    \brief This file contains the IEEE 802.11 family related MIB definition
	   for MediaTek 802.11 Wireless LAN Adapters.
*/

/*
** Log: mib.h
 *
 * 11 08 2010 wh.su
 * [WCXRP00000171] [MT6620 Wi-Fi][Driver] Add message check code same behavior as mt5921
 * add the message check code from mt5921.
 *
 * 07 24 2010 wh.su
 *
 * .support the Wi-Fi RSN
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add aa_fsm.h, ais_fsm.h, bss.h, mib.h and scan.h.
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
 *
 * Nov 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
*/

#ifndef _MIB_H
#define _MIB_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                         D A T A   T Y P E S
********************************************************************************
*/
/* Entry in SMT AuthenticationAlgorithms Table: dot11AuthenticationAlgorithmsEntry */
typedef struct _DOT11_AUTHENTICATION_ALGORITHMS_ENTRY {
	BOOLEAN dot11AuthenticationAlgorithmsEnable;	/* dot11AuthenticationAlgorithmsEntry 3 */
} DOT11_AUTHENTICATION_ALGORITHMS_ENTRY, *P_DOT11_AUTHENTICATION_ALGORITHMS_ENTRY;

/* Entry in SMT dot11RSNAConfigPairwiseCiphersTalbe Table: dot11RSNAConfigPairwiseCiphersEntry */
typedef struct _DOT11_RSNA_CONFIG_PAIRWISE_CIPHERS_ENTRY {
	UINT_32 dot11RSNAConfigPairwiseCipher;	/* dot11RSNAConfigPairwiseCiphersEntry 2 */
	BOOLEAN dot11RSNAConfigPairwiseCipherEnabled;	/* dot11RSNAConfigPairwiseCiphersEntry 3 */
} DOT11_RSNA_CONFIG_PAIRWISE_CIPHERS_ENTRY, *P_DOT11_RSNA_CONFIG_PAIRWISE_CIPHERS_ENTRY;

/* Entry in SMT dot11RSNAConfigAuthenticationSuitesTalbe Table: dot11RSNAConfigAuthenticationSuitesEntry */
typedef struct _DOT11_RSNA_CONFIG_AUTHENTICATION_SUITES_ENTRY {
	UINT_32 dot11RSNAConfigAuthenticationSuite;	/* dot11RSNAConfigAuthenticationSuitesEntry 2 */
	BOOLEAN dot11RSNAConfigAuthenticationSuiteEnabled;	/* dot11RSNAConfigAuthenticationSuitesEntry 3 */
} DOT11_RSNA_CONFIG_AUTHENTICATION_SUITES_ENTRY, *P_DOT11_RSNA_CONFIG_AUTHENTICATION_SUITES_ENTRY;

/* ----- IEEE 802.11 MIB Major sections ----- */
typedef struct _IEEE_802_11_MIB_T {
	/* dot11PrivacyTable                            (dot11smt 5) */
	UINT_8 dot11WEPDefaultKeyID;	/* dot11PrivacyEntry 2 */
	BOOLEAN dot11TranmitKeyAvailable;
	UINT_32 dot11WEPICVErrorCount;	/* dot11PrivacyEntry 5 */
	UINT_32 dot11WEPExcludedCount;	/* dot11PrivacyEntry 6 */

	/* dot11RSNAConfigTable                         (dot11smt 8) */
	UINT_32 dot11RSNAConfigGroupCipher;	/* dot11RSNAConfigEntry 4 */

	/* dot11RSNAConfigPairwiseCiphersTable          (dot11smt 9) */
	DOT11_RSNA_CONFIG_PAIRWISE_CIPHERS_ENTRY dot11RSNAConfigPairwiseCiphersTable[MAX_NUM_SUPPORTED_CIPHER_SUITES];

	/* dot11RSNAConfigAuthenticationSuitesTable     (dot11smt 10) */
	 DOT11_RSNA_CONFIG_AUTHENTICATION_SUITES_ENTRY
	    dot11RSNAConfigAuthenticationSuitesTable[MAX_NUM_SUPPORTED_AKM_SUITES];

#if 0				/* SUPPORT_WAPI */
	BOOLEAN fgWapiKeyInstalled;
	PARAM_WPI_KEY_T rWapiPairwiseKey[2];
	BOOLEAN fgPairwiseKeyUsed[2];
	UINT_8 ucWpiActivedPWKey;	/* Must be 0 or 1, by wapi spec */
	PARAM_WPI_KEY_T rWapiGroupKey[2];
	BOOLEAN fgGroupKeyUsed[2];
#endif
} IEEE_802_11_MIB_T, *P_IEEE_802_11_MIB_T;

/* ------------------ IEEE 802.11 non HT PHY characteristics ---------------- */
typedef const struct _NON_HT_PHY_ATTRIBUTE_T {
	UINT_16 u2SupportedRateSet;

	BOOLEAN fgIsShortPreambleOptionImplemented;

	BOOLEAN fgIsShortSlotTimeOptionImplemented;

} NON_HT_PHY_ATTRIBUTE_T, *P_NON_HT_PHY_ATTRIBUTE_T;

typedef const struct _NON_HT_ADHOC_MODE_ATTRIBUTE_T {

	ENUM_PHY_TYPE_INDEX_T ePhyTypeIndex;

	UINT_16 u2BSSBasicRateSet;

} NON_HT_ADHOC_MODE_ATTRIBUTE_T, *P_NON_HT_ADHOC_MODE_ATTRIBUTE_T;

typedef NON_HT_ADHOC_MODE_ATTRIBUTE_T NON_HT_AP_MODE_ATTRIBUTE_T;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
extern NON_HT_PHY_ATTRIBUTE_T rNonHTPhyAttributes[];
extern NON_HT_ADHOC_MODE_ATTRIBUTE_T rNonHTAdHocModeAttributes[];
extern NON_HT_AP_MODE_ATTRIBUTE_T rNonHTApModeAttributes[];

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _MIB_H */
