/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/mgmt/bss.c#3 $
*/

/*! \file   "bss.c"
    \brief  This file contains the functions for creating BSS(AP)/IBSS(AdHoc).

    This file contains the functions for BSS(AP)/IBSS(AdHoc). We may create a BSS/IBSS
    network, or merge with exist IBSS network and sending Beacon Frame or reply
    the Probe Response Frame for received Probe Request Frame.
*/



/*
** $Log: bss.c $
** 
** 08 30 2012 chinglan.wang
** [ALPS00349664] [6577JB][WIFI] Phone can not connect to AP secured with AES via WPS in 802.11n Only
** .
** 
** 07 26 2012 yuche.tsai
** [ALPS00324337] [ALPS.JB][Hot-Spot] Driver update for Hot-Spot
** Update driver code of ALPS.JB for hot-spot.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Let netdev bring up.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 06 14 2012 chinglan.wang
 * NULL
 * Fix the losing of the HT IE in assoc request..
 *
 * 06 13 2012 yuche.tsai
 * NULL
 * Update maintrunk driver.
 * Add support for driver compose assoc request frame.
 *
 * 03 08 2012 yuche.tsai
 * NULL
 * Fix FW assert when start Hot-Spot.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Snc CFG80211 modification for ICS migration from branch 2.2.
 *
 * 01 20 2012 chinglan.wang
 * 03 02 2012 terry.wu
 * NULL
 * Fix the WPA-PSK TKIP and WPA2-PSK AES security mode bug.
 *
 * NULL
 * Sync CFG80211 modification from branch 2,2.
 *
 * 01 15 2012 yuche.tsai
 * NULL
 * Fix wrong basic rate issue.
 *
 * 01 13 2012 yuche.tsai
 * NULL
 * WiFi Hot Spot Tethering for ICS ALPHA testing version.
 *
 * 11 03 2011 cm.chang
 * [WCXRP00000997] [MT6620 Wi-Fi][Driver][FW] Handle change of BSS preamble type and slot time
 * Always set short slot time to TRUE initially in AP mode
 *
 * 11 03 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * change the DBGLOG for "\n" and "\r\n". LABEL to LOUD for XLOG
 *
 * 09 14 2011 yuche.tsai
 * NULL
 * Add P2P IE in assoc response.
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 04 12 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix the sta index in processing security frame
 * Simple flow control for TC4 to avoid mgt frames for PS STA to occupy the TC4
 * Add debug message.
 *
 * 04 08 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix for sigma
 *
 * 03 29 2011 eddie.chen
 * [WCXRP00000608] [MT6620 Wi-Fi][DRV] Change wmm parameters in beacon
 * Change wmm parameters in beacon.
 *
 * 03 29 2011 yuche.tsai
 * [WCXRP00000607] [Volunteer Patch][MT6620][Driver] Coding Style Fix for klocwork scan.
 * Fix klocwork issue.
 *
 * 03 19 2011 yuche.tsai
 * [WCXRP00000581] [Volunteer Patch][MT6620][Driver] P2P IE in Assoc Req Issue
 * Make assoc req to append P2P IE if wifi direct is enabled.
 *
 * 03 11 2011 chinglan.wang
 * [WCXRP00000537] [MT6620 Wi-Fi][Driver] Can not connect to 802.11b/g/n mixed AP with WEP security.
 * .
 *
 * 03 03 2011 george.huang
 * [WCXRP00000508] [MT6620 Wi-Fi][Driver] aware of beacon MSDU will be free, after BSS deactivated
 * .
 *
 * 03 03 2011 george.huang
 * [WCXRP00000508] [MT6620 Wi-Fi][Driver] aware of beacon MSDU will be free, after BSS deactivated
 * modify to handle if beacon MSDU been released when BSS deactivated
 *
 * 03 02 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * add code to let the beacon and probe response for Auto GO WSC .
 *
 * 03 02 2011 wh.su
 * [WCXRP00000448] [MT6620 Wi-Fi][Driver] Fixed WSC IE not send out at probe request
 * Add code to send beacon and probe response WSC IE at Auto GO.
 *
 * 02 17 2011 eddie.chen
 * [WCXRP00000458] [MT6620 Wi-Fi][Driver] BOW Concurrent - ProbeResp was exist in other channel
 * 1) Chnage GetFrameAction decision when BSS is absent.
 * 2) Check channel and resource in processing ProbeRequest
 *
 * 02 12 2011 yuche.tsai
 * [WCXRP00000441] [Volunteer Patch][MT6620][Driver] BoW can not create desired station type when Hot Spot is enabled.
 * bss should create station record type according to callers input.
 *
 * 02 11 2011 terry.wu
 * [WCXRP00000383] [MT6620 Wi-Fi][Driver] Separate WiFi and P2P driver into two modules
 * In p2p link function, check networktype before calling p2p function.
 *
 * 02 11 2011 terry.wu
 * [WCXRP00000383] [MT6620 Wi-Fi][Driver] Separate WiFi and P2P driver into two modules
 * Modify p2p link function to avoid assert.
 *
 * 01 26 2011 cm.chang
 * [WCXRP00000395] [MT6620 Wi-Fi][Driver][FW] Search STA_REC with additional net type index argument
 * .
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Change Station Type in Station Record, Modify MACRO definition for getting station type & network type index & Role.
 *
 * 01 25 2011 eddie.chen
 * [WCXRP00000385] [MT6620 Wi-Fi][DRV] Add destination decision for forwarding packets
 * Fix the compile error in windows.
 *
 * 01 24 2011 eddie.chen
 * [WCXRP00000385] [MT6620 Wi-Fi][DRV] Add destination decision for forwarding packets
 * Add destination decision in AP mode.
 *
 * 01 24 2011 terry.wu
 * [WCXRP00000383] [MT6620 Wi-Fi][Driver] Separate WiFi and P2P driver into two modules
 * .Fix typo and missing entry
 *
 * 12 30 2010 eddie.chen
 * [WCXRP00000322] Add WMM IE in beacon,

Add per station flow control when STA is in PS


 * Fix  prBssInfo->aucCWminLog to  prBssInfo->aucCWminLogForBcast
 *
 * 12 29 2010 eddie.chen
 * [WCXRP00000322] Add WMM IE in beacon,

Add per station flow control when STA is in PS


 * Add WMM parameter for broadcast.
 *
 * 12 29 2010 eddie.chen
 * [WCXRP00000322] Add WMM IE in beacon,
Add per station flow control when STA is in PS

 * 1) PS flow control event
 *
 * 2) WMM IE in beacon, assoc resp, probe resp
 *
 * 11 29 2010 cp.wu
 * [WCXRP00000210] [MT6620 Wi-Fi][Driver][FW] Set RCPI value in STA_REC for initial TX rate selection of auto-rate algorithm
 * update ucRcpi of STA_RECORD_T for AIS when
 * 1) Beacons for IBSS merge is received
 * 2) Associate Response for a connecting peer is received
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * use definition macro to replace hard-coded constant
 *
 * 10 08 2010 wh.su
 * [WCXRP00000085] [MT6620 Wif-Fi] [Driver] update the modified p2p state machine
 * update the frog's new p2p state machine.
 *
 * 09 28 2010 wh.su
 * NULL
 * [WCXRP00000069][MT6620 Wi-Fi][Driver] Fix some code for phase 1 P2P Demo.
 *
 * 09 27 2010 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000065] Update BoW design and settings
 * Update BCM/BoW design and settings.
 *
 * 09 16 2010 cm.chang
 * NULL
 * Change conditional compiling options for BOW
 *
 * 09 10 2010 cm.chang
 * NULL
 * Always update Beacon content if FW sync OBSS info
 *
 * 09 07 2010 wh.su
 * NULL
 * adding the code for beacon/probe req/ probe rsp wsc ie at p2p.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 31 2010 kevin.huang
 * NULL
 * Use LINK LIST operation to process SCAN result
 *
 * 08 30 2010 cp.wu
 * NULL
 * eliminate klockwork errors
 *
 * 08 29 2010 yuche.tsai
 * NULL
 * Finish SLT TX/RX & Rate Changing Support.
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 08 16 2010 yuche.tsai
 * NULL
 * Before composing Beacon IE, assign network type index for msdu info,
 * this information is needed by RLM module while composing some RLM related IE field.
 *
 * 08 16 2010 cp.wu
 * NULL
 * Replace CFG_SUPPORT_BOW by CFG_ENABLE_BT_OVER_WIFI.
 * There is no CFG_SUPPORT_BOW in driver domain source.
 *
 * 08 16 2010 kevin.huang
 * NULL
 * Refine AAA functions
 *
 * 08 12 2010 kevin.huang
 * NULL
 * Fix undefined pucDestAddr in bssUpdateBeaconContent()
 *
 * 08 12 2010 kevin.huang
 * NULL
 * Refine bssProcessProbeRequest() and bssSendBeaconProbeResponse()
 *
 * 08 11 2010 cp.wu
 * NULL
 * 1) do not use in-stack variable for beacon updating. (for MAUI porting)
 * 2) extending scanning result to 64 instead of 48
 *
 * 08 02 2010 yuche.tsai
 * NULL
 * P2P Group Negotiation Code Check in.
 *
 * 08 02 2010 george.huang
 * NULL
 * add WMM-PS test related OID/ CMD handlers
 *
 * 07 26 2010 yuche.tsai
 *
 * Add support to RX probe response for P2P.
 *
 * 07 20 2010 cp.wu
 *
 * 1) bugfix: do not stop timer for join after switched into normal_tr state, for providing chance for DHCP handshasking
 * 2) modify rsnPerformPolicySelection() invoking
 *
 * 07 19 2010 wh.su
 *
 * update for security supporting.
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * when IBSS is being merged-in, send command packet to PM for connected indication
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * Add Ad-Hoc support to AIS-FSM
 *
 * 07 14 2010 yarco.yang
 *
 * 1. Remove CFG_MQM_MIGRATION
 * 2. Add CMD_UPDATE_WMM_PARMS command
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 06 2010 george.huang
 * [WPD00001556]Basic power managemenet function
 * Update arguments for nicUpdateBeaconIETemplate()
 *
 * 06 29 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) sync to. CMD/EVENT document v0.03
 * 2) simplify DTIM period parsing in scan.c only, bss.c no longer parses it again.
 * 3) send command packet to indicate FW-PM after
 *     a) 1st beacon is received after AIS has connected to an AP
 *     b) IBSS-ALONE has been created
 *     c) IBSS-MERGE has occured
 *
 * 06 28 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * send MMPDU in basic rate.
 *
 * 06 25 2010 george.huang
 * [WPD00001556]Basic power managemenet function
 * Create beacon update path, with expose bssUpdateBeaconContent()
 *
 * 06 21 2010 yuche.tsai
 * [WPD00003839][MT6620 5931][P2P] Feature migration
 * Fix compile error while enable WIFI_DIRECT support.
 *
 * 06 21 2010 yarco.yang
 * [WPD00003837][MT6620]Data Path Refine
 * Support CFG_MQM_MIGRATION flag
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * enable RX management frame handling.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * specify correct value for management frames.
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * correct when ADHOC support is turned on.
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add scan.c.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add management dispatching function table.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * auth.c is migrated.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * fix compilation error when WIFI_DIRECT is turned on
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add bss.c.
 *
 * 06 04 2010 george.huang
 * [BORA00000678][MT6620]WiFi LP integration
 * [PM] Support U-APSD for STA mode
 *
 * 05 28 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add ClientList handling API - bssClearClientList, bssAddStaRecToClientList
 *
 * 05 24 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Update bssProcessProbeRequest() to avoid redundant SSID IE {0,0} for IOT.
 *
 * 05 21 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Refine txmInitWtblTxRateTable() - set TX initial rate according to AP's operation rate set
 *
 * 05 18 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Ad-hoc Beacon should not carry HT OP and OBSS IEs
 *
 * 05 14 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Use TX MGMT Frame API for sending PS NULL frame to avoid the TX Burst Mechanism in TX FW Frame API
 *
 * 05 14 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Seperate Beacon and ProbeResp IE array
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 04 28 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Removed the use of compiling flag MQM_WMM_PARSING
 *
 * 04 27 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add Set Slot Time and Beacon Timeout Support for AdHoc Mode
 *
 * 04 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * g_aprBssInfo[] depends on CFG_SUPPORT_P2P and CFG_SUPPORT_BOW
 *
 * 04 22 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * First draft code to support protection in AP mode
 *
 * 04 20 2010 kevin.huang
 * [BORA00000714][WIFISYS][New Feature]Beacon Timeout Support
 * Fix restart Beacon Timeout Func after connection diagnosis
 *
 * 04 19 2010 kevin.huang
 * [BORA00000714][WIFISYS][New Feature]Beacon Timeout Support
 * Add Beacon Timeout Support and will send Null frame to diagnose connection
 *
 * 04 16 2010 wh.su
 * [BORA00000680][MT6620] Support the statistic for Microsoft os query
 * adding the wpa-none for ibss beacon.
 *
 * 04 15 2010 wh.su
 * [BORA00000680][MT6620] Support the statistic for Microsoft os query
 * fixed the protected bit at cap info for ad-hoc.
 *
 * 03 18 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Rename the CFG flags
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 02 26 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Update outgoing beacon's TX data rate
 *
 * 02 23 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add DTIM count update while TX Beacon
 *
 * 02 05 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Modify code due to define - BAND_24G and specific BSS_INFO_T was changed
 *
 * 02 05 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Revise data structure to share the same BSS_INFO_T for avoiding coding error
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "precomp.h"

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
#if (CFG_SUPPORT_ADHOC) || (CFG_SUPPORT_AAA)
APPEND_VAR_IE_ENTRY_T txBcnIETable[] = {
    { (ELEM_HDR_LEN + (RATE_NUM - ELEM_MAX_LEN_SUP_RATES)), NULL,                           bssGenerateExtSuppRate_IE   }   /* 50 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_ERP),                    NULL,                           rlmRspGenerateErpIE         }   /* 42 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_CAP),                 NULL,                           rlmRspGenerateHtCapIE       }   /* 45 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_OP),                  NULL,                           rlmRspGenerateHtOpIE        }   /* 61 */
#if CFG_ENABLE_WIFI_DIRECT
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_OBSS_SCAN),              NULL,                           rlmRspGenerateObssScanIE    }   /* 74 */
#endif
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_EXT_CAP),                NULL,                           rlmRspGenerateExtCapIE      }   /* 127 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_WPA),                    NULL,                           rsnGenerateWpaNoneIE        }   /* 221 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_WMM_PARAM),              NULL,                           mqmGenerateWmmParamIE       }   /* 221 */
#if CFG_ENABLE_WIFI_DIRECT
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_WPA),                    NULL,                           rsnGenerateWPAIE            }   /* 221 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_RSN),                    NULL,                           rsnGenerateRSNIE            }   /* 48 */
#if 0 //LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
   ,{ 0,                                                    p2pFuncCalculateExtra_IELenForBeacon, p2pFuncGenerateExtra_IEForBeacon  }   /* 221 */
#else
   ,{ 0,                                                    p2pFuncCalculateP2p_IELenForBeacon, p2pFuncGenerateP2p_IEForBeacon  }   /* 221 */
   ,{ 0,                                                    p2pFuncCalculateWSC_IELenForBeacon, p2pFuncGenerateWSC_IEForBeacon      }   /* 221 */
#endif
#endif /* CFG_ENABLE_WIFI_DIRECT */
};


APPEND_VAR_IE_ENTRY_T txProbRspIETable[] = {
    { (ELEM_HDR_LEN + (RATE_NUM - ELEM_MAX_LEN_SUP_RATES)), NULL,                           bssGenerateExtSuppRate_IE   }   /* 50 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_ERP),                    NULL,                           rlmRspGenerateErpIE         }   /* 42 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_CAP),                 NULL,                           rlmRspGenerateHtCapIE       }   /* 45 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_OP),                  NULL,                           rlmRspGenerateHtOpIE        }   /* 61 */
#if CFG_ENABLE_WIFI_DIRECT
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_RSN),                    NULL,                           rsnGenerateRSNIE            }   /* 48 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_OBSS_SCAN),              NULL,                           rlmRspGenerateObssScanIE    }   /* 74 */
#endif
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_EXT_CAP),                NULL,                           rlmRspGenerateExtCapIE      }   /* 127 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_WPA),                    NULL,                           rsnGenerateWpaNoneIE        }   /* 221 */
   ,{ (ELEM_HDR_LEN + ELEM_MAX_LEN_WMM_PARAM),              NULL,                           mqmGenerateWmmParamIE       }   /* 221 */
};

#endif /* CFG_SUPPORT_ADHOC || CFG_SUPPORT_AAA */


/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/


/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/* Routines for all Operation Modes                                           */
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*!
* @brief This function will create or reset a STA_RECORD_T by given BSS_DESC_T for
*        Infrastructure or AdHoc Mode.
*
* @param[in] prAdapter              Pointer to the Adapter structure.
* @param[in] eStaType               Assign STA Type for this STA_RECORD_T
* @param[in] eNetTypeIndex          Assign Net Type Index for this STA_RECORD_T
* @param[in] prBssDesc              Received Beacon/ProbeResp from this STA
*
* @retval   Pointer to STA_RECORD_T
*/
/*----------------------------------------------------------------------------*/
P_STA_RECORD_T
bssCreateStaRecFromBssDesc (
    IN P_ADAPTER_T                  prAdapter,
    IN ENUM_STA_TYPE_T              eStaType,
    IN ENUM_NETWORK_TYPE_INDEX_T    eNetTypeIndex,
    IN P_BSS_DESC_T                 prBssDesc
    )
{
    P_STA_RECORD_T prStaRec;
    UINT_8 ucNonHTPhyTypeSet;


    ASSERT(prBssDesc);

    //4 <1> Get a valid STA_RECORD_T
    prStaRec = cnmGetStaRecByAddress(prAdapter, (UINT_8) eNetTypeIndex,
                                        prBssDesc->aucSrcAddr);
    if (!prStaRec) {

        prStaRec = cnmStaRecAlloc(prAdapter, (UINT_8) eNetTypeIndex);

        /* TODO(Kevin): Error handling of allocation of STA_RECORD_T for
         * exhausted case and do removal of unused STA_RECORD_T.
         */

        if(!prStaRec) {
            ASSERT(FALSE);
            return NULL;
        }

        ASSERT(prStaRec);

        prStaRec->ucJoinFailureCount = 0;
        /* TODO(Kevin): If this is an old entry, we may also reset the ucJoinFailureCount to 0.
         */

        COPY_MAC_ADDR(prStaRec->aucMacAddr, prBssDesc->aucSrcAddr);
    }


    //4 <2> Setup STA TYPE and NETWORK
    prStaRec->eStaType = eStaType;

    prStaRec->ucNetTypeIndex = eNetTypeIndex;


    //4 <3> Update information from BSS_DESC_T to current P_STA_RECORD_T
    prStaRec->u2CapInfo = prBssDesc->u2CapInfo;

    prStaRec->u2OperationalRateSet = prBssDesc->u2OperationalRateSet;
    prStaRec->u2BSSBasicRateSet = prBssDesc->u2BSSBasicRateSet;

    prStaRec->ucPhyTypeSet = prBssDesc->ucPhyTypeSet;

	if (!((prAdapter->rWifiVar.rConnSettings.eEncStatus == ENUM_ENCRYPTION3_ENABLED) ||
		(prAdapter->rWifiVar.rConnSettings.eEncStatus == ENUM_ENCRYPTION3_KEY_ABSENT) ||
		(prAdapter->rWifiVar.rConnSettings.eEncStatus == ENUM_ENCRYPTION_DISABLED) ||
		(prAdapter->prGlueInfo->u2WSCAssocInfoIELen) ||
		(prAdapter->prGlueInfo->u2WapiAssocInfoIESz))) {
		DBGLOG(BSS, INFO, ("Ignore the HT Bit for TKIP as pairwise cipher configed!\n"));
		prStaRec->ucPhyTypeSet &= ~PHY_TYPE_BIT_HT;
	}

    prStaRec->ucDesiredPhyTypeSet = prStaRec->ucPhyTypeSet & prAdapter->rWifiVar.ucAvailablePhyTypeSet;

    ucNonHTPhyTypeSet = prStaRec->ucDesiredPhyTypeSet & PHY_TYPE_SET_802_11ABG;

    /* Check for Target BSS's non HT Phy Types */
    if (ucNonHTPhyTypeSet) {

        if (ucNonHTPhyTypeSet & PHY_TYPE_BIT_ERP) {
            prStaRec->ucNonHTBasicPhyType = PHY_TYPE_ERP_INDEX;
        }
        else if (ucNonHTPhyTypeSet & PHY_TYPE_BIT_OFDM) {
            prStaRec->ucNonHTBasicPhyType = PHY_TYPE_OFDM_INDEX;
        }
        else /* if (ucNonHTPhyTypeSet & PHY_TYPE_HR_DSSS_INDEX) */ {
            prStaRec->ucNonHTBasicPhyType = PHY_TYPE_HR_DSSS_INDEX;
        }

        prStaRec->fgHasBasicPhyType = TRUE;
    }
    else {
        /* Use mandatory for 11N only BSS */
        ASSERT(prStaRec->ucPhyTypeSet & PHY_TYPE_SET_802_11N);

        {
            /* TODO(Kevin): which value should we set for 11n ? ERP ? */
            prStaRec->ucNonHTBasicPhyType = PHY_TYPE_HR_DSSS_INDEX;
        }

        prStaRec->fgHasBasicPhyType = FALSE;
    }

    /* Update non HT Desired Rate Set */
    {
        P_CONNECTION_SETTINGS_T prConnSettings;


        prConnSettings = &(prAdapter->rWifiVar.rConnSettings);

        prStaRec->u2DesiredNonHTRateSet =
            (prStaRec->u2OperationalRateSet & prConnSettings->u2DesiredNonHTRateSet);
    }

    //4 <4> Update information from BSS_DESC_T to current P_STA_RECORD_T
    if (IS_AP_STA(prStaRec)) {
        /* do not need to parse IE for DTIM,
         * which have been parsed before inserting into BSS_DESC_T
         */
        if (prBssDesc->ucDTIMPeriod) {
            prStaRec->ucDTIMPeriod = prBssDesc->ucDTIMPeriod;
        }
        else {
            prStaRec->ucDTIMPeriod = 0; // Means that TIM was not parsed.
        }
    }


    //4 <5> Update default value
    prStaRec->fgDiagnoseConnection = FALSE;


    //4 <6> Update default value for other Modules
    /* Determine fgIsWmmSupported and fgIsUapsdSupported in STA_REC */
    mqmProcessScanResult(prAdapter, prBssDesc, prStaRec);

    return prStaRec;

} /* end of bssCreateStaRecFromBssDesc() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function will compose the Null Data frame.
*
* @param[in] prAdapter              Pointer to the Adapter structure.
* @param[in] pucBuffer              Pointer to the frame buffer.
* @param[in] prStaRec               Pointer to the STA_RECORD_T.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssComposeNullFrame (
    IN P_ADAPTER_T      prAdapter,
    IN PUINT_8          pucBuffer,
    IN P_STA_RECORD_T   prStaRec
    )
{
    P_WLAN_MAC_HEADER_T prNullFrame;
    P_BSS_INFO_T prBssInfo;
    UINT_16 u2FrameCtrl;


    ASSERT(pucBuffer);
    ASSERT(prStaRec);
    ASSERT(prStaRec->ucNetTypeIndex < NETWORK_TYPE_INDEX_NUM);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[prStaRec->ucNetTypeIndex]);

    ASSERT(prBssInfo);

    prNullFrame = (P_WLAN_MAC_HEADER_T)pucBuffer;

    //4 <1> Decide the Frame Control Field
    u2FrameCtrl = MAC_FRAME_NULL;

    if (IS_AP_STA(prStaRec)) {
        u2FrameCtrl |= MASK_FC_TO_DS;

        if (prStaRec->fgSetPwrMgtBit) {
            u2FrameCtrl |= MASK_FC_PWR_MGT;
        }
    }
    else if (IS_CLIENT_STA(prStaRec)) {
        u2FrameCtrl |= MASK_FC_FROM_DS;
    }
    else if (IS_DLS_STA(prStaRec)) {
        /* TODO(Kevin) */
    }
    else {
        /* NOTE(Kevin): We won't send Null frame for IBSS */
        ASSERT(0);
        return;
    }

    //4 <2> Compose the Null frame
    /* Fill the Frame Control field. */
    //WLAN_SET_FIELD_16(&prNullFrame->u2FrameCtrl, u2FrameCtrl);
    prNullFrame->u2FrameCtrl = u2FrameCtrl; // NOTE(Kevin): Optimized for ARM

    /* Fill the Address 1 field with Target Peer Address. */
    COPY_MAC_ADDR(prNullFrame->aucAddr1, prStaRec->aucMacAddr);

    /* Fill the Address 2 field with our MAC Address. */
    COPY_MAC_ADDR(prNullFrame->aucAddr2, prBssInfo->aucOwnMacAddr);

    /* Fill the Address 3 field with Target BSSID. */
    COPY_MAC_ADDR(prNullFrame->aucAddr3, prBssInfo->aucBSSID);

    /* Clear the SEQ/FRAG_NO field(HW won't overide the FRAG_NO, so we need to clear it). */
    prNullFrame->u2SeqCtrl = 0;

    return;

} /* end of bssComposeNullFrameHeader() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function will compose the QoS Null Data frame.
*
* @param[in] prAdapter              Pointer to the Adapter structure.
* @param[in] pucBuffer              Pointer to the frame buffer.
* @param[in] prStaRec               Pointer to the STA_RECORD_T.
* @param[in] ucUP                   User Priority.
* @param[in] fgSetEOSP              Set the EOSP bit.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssComposeQoSNullFrame (
    IN P_ADAPTER_T      prAdapter,
    IN PUINT_8          pucBuffer,
    IN P_STA_RECORD_T   prStaRec,
    IN UINT_8           ucUP,
    IN BOOLEAN          fgSetEOSP
    )
{
    P_WLAN_MAC_HEADER_QOS_T prQoSNullFrame;
    P_BSS_INFO_T prBssInfo;
    UINT_16 u2FrameCtrl;
    UINT_16 u2QosControl;


    ASSERT(pucBuffer);
    ASSERT(prStaRec);
    ASSERT(prStaRec->ucNetTypeIndex < NETWORK_TYPE_INDEX_NUM);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[prStaRec->ucNetTypeIndex]);

    ASSERT(prBssInfo);

    prQoSNullFrame = (P_WLAN_MAC_HEADER_QOS_T)pucBuffer;

    //4 <1> Decide the Frame Control Field
    u2FrameCtrl = MAC_FRAME_QOS_NULL;

    if (IS_AP_STA(prStaRec)) {
        u2FrameCtrl |= MASK_FC_TO_DS;

        if (prStaRec->fgSetPwrMgtBit) {
            u2FrameCtrl |= MASK_FC_PWR_MGT;
        }
    }
    else if (IS_CLIENT_STA(prStaRec)) {
        u2FrameCtrl |= MASK_FC_FROM_DS;
    }
    else if (IS_DLS_STA(prStaRec)) {
        /* TODO(Kevin) */
    }
    else {
        /* NOTE(Kevin): We won't send QoS Null frame for IBSS */
        ASSERT(0);
        return;
    }

    //4 <2> Compose the QoS Null frame
    /* Fill the Frame Control field. */
    //WLAN_SET_FIELD_16(&prQoSNullFrame->u2FrameCtrl, u2FrameCtrl);
    prQoSNullFrame->u2FrameCtrl = u2FrameCtrl; // NOTE(Kevin): Optimized for ARM

    /* Fill the Address 1 field with Target Peer Address. */
    COPY_MAC_ADDR(prQoSNullFrame->aucAddr1, prStaRec->aucMacAddr);

    /* Fill the Address 2 field with our MAC Address. */
    COPY_MAC_ADDR(prQoSNullFrame->aucAddr2, prBssInfo->aucOwnMacAddr);

    /* Fill the Address 3 field with Target BSSID. */
    COPY_MAC_ADDR(prQoSNullFrame->aucAddr3, prBssInfo->aucBSSID);

    /* Clear the SEQ/FRAG_NO field(HW won't overide the FRAG_NO, so we need to clear it). */
    prQoSNullFrame->u2SeqCtrl = 0;

    u2QosControl = (UINT_16)(ucUP & WMM_QC_UP_MASK);

    if (fgSetEOSP) {
        u2QosControl |= WMM_QC_EOSP;
    }

    //WLAN_SET_FIELD_16(&prQoSNullFrame->u2QosCtrl, u2QosControl);
    prQoSNullFrame->u2QosCtrl = u2QosControl; // NOTE(Kevin): Optimized for ARM

    return;

} /* end of bssComposeQoSNullFrameHeader() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Send the Null Frame
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] prStaRec           Pointer to the STA_RECORD_T
* @param[in] pfTxDoneHandler    TX Done call back function
*
* @retval WLAN_STATUS_RESOURCE  No available resources to send frame.
* @retval WLAN_STATUS_SUCCESS   Succe]ss.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
bssSendNullFrame (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prStaRec,
    IN PFN_TX_DONE_HANDLER  pfTxDoneHandler
    )
{
    P_MSDU_INFO_T prMsduInfo;
    UINT_16 u2EstimatedFrameLen;


    //4 <1> Allocate a PKT_INFO_T for Null Frame
    /* Init with MGMT Header Length */
    u2EstimatedFrameLen = MAC_TX_RESERVED_FIELD + \
                          WLAN_MAC_HEADER_LEN;

    /* Allocate a MSDU_INFO_T */
    if ( (prMsduInfo = cnmMgtPktAlloc(prAdapter, u2EstimatedFrameLen)) == NULL) {
        DBGLOG(BSS, WARN, ("No PKT_INFO_T for sending Null Frame.\n"));
        return WLAN_STATUS_RESOURCES;
    }

    //4 <2> Compose Null frame in MSDU_INfO_T.
    bssComposeNullFrame(prAdapter,
            (PUINT_8)((UINT_32)prMsduInfo->prPacket + MAC_TX_RESERVED_FIELD),
            prStaRec);
#if 0
    //4 <3> Update information of MSDU_INFO_T
    TXM_SET_DATA_PACKET( \
    /* STA_REC ptr */       prStaRec, \
    /* MSDU_INFO ptr */     prMsduInfo, \
    /* MAC HDR ptr */       (prMsduInfo->pucBuffer + MAC_TX_RESERVED_FIELD), \
    /* MAC HDR length */    WLAN_MAC_HEADER_LEN, \
    /* PAYLOAD ptr */       (prMsduInfo->pucBuffer + MAC_TX_RESERVED_FIELD + WLAN_MAC_HEADER_LEN), \
    /* PAYLOAD length */    0, \
    /* Network Type Index */(UINT_8)prStaRec->ucNetTypeIndex, \
    /* TID */               0 /* BE: AC1 */, \
    /* Flag 802.11 */       TRUE,\
    /* Pkt arrival time */  0 /* TODO: Obtain the system time */, \
    /* Resource TC */       0 /* Irrelevant */, \
    /* Flag 802.1x */       FALSE,\
    /* TX-done callback */  pfTxDoneHandler, \
    /* PS forwarding type*/ PS_FORWARDING_TYPE_NON_PS, \
    /* PS Session ID */     0           /* Irrelevant */, \
    /* Flag fixed rate */   TRUE, \
    /* Fixed tx rate */     g_aprBssInfo[prStaRec->ucNetTypeIndex]->ucHwDefaultFixedRateCode, \
    /* Fixed-rate retry */  BSS_DEFAULT_CONN_TEST_NULL_FRAME_RETRY_LIMIT, \
    /* PAL LLH */           0 /* Irrelevant */,\
    /* ACL SN */            0 /* Irrelevant */,\
    /* Flag No Ack */       FALSE \
    );

    /* Terminate with a NULL pointer */
    NIC_HIF_TX_SET_NEXT_MSDU_INFO(prMsduInfo,NULL);

    /* TODO(Kevin): Also release the unused tail room of the composed MMPDU */

    /* Indicate the packet to TXM */
    //4 <4> Inform TXM to send this Null frame.
    txmSendFwDataPackets(prMsduInfo);
#endif

    prMsduInfo->eSrc = TX_PACKET_MGMT;
    prMsduInfo->ucPacketType = HIF_TX_PACKET_TYPE_DATA;
    prMsduInfo->ucStaRecIndex = prStaRec->ucIndex;
    prMsduInfo->ucNetworkType = prStaRec->ucNetTypeIndex;
    prMsduInfo->ucMacHeaderLength = WLAN_MAC_HEADER_LEN;
    prMsduInfo->fgIs802_1x = FALSE;
    prMsduInfo->fgIs802_11 = TRUE;
    prMsduInfo->u2FrameLength = WLAN_MAC_HEADER_LEN;
    prMsduInfo->ucTxSeqNum = nicIncreaseTxSeqNum(prAdapter);
    prMsduInfo->pfTxDoneHandler = pfTxDoneHandler;
    prMsduInfo->fgIsBasicRate = FALSE;

    //4 <4> Inform TXM  to send this Null frame.
    nicTxEnqueueMsdu(prAdapter, prMsduInfo);

    return WLAN_STATUS_SUCCESS;

} /* end of bssSendNullFrame() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Send the QoS Null Frame
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] prStaRec           Pointer to the STA_RECORD_T
* @param[in] pfTxDoneHandler    TX Done call back function
*
* @retval WLAN_STATUS_RESOURCE  No available resources to send frame.
* @retval WLAN_STATUS_SUCCESS   Success.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
bssSendQoSNullFrame (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prStaRec,
    IN UINT_8               ucUP,
    IN PFN_TX_DONE_HANDLER  pfTxDoneHandler
    )
{
    P_MSDU_INFO_T prMsduInfo;
    UINT_16 u2EstimatedFrameLen;


    //4 <1> Allocate a PKT_INFO_T for Null Frame
    /* Init with MGMT Header Length */
    u2EstimatedFrameLen = MAC_TX_RESERVED_FIELD + \
                          WLAN_MAC_HEADER_QOS_LEN;

    /* Allocate a MSDU_INFO_T */
    if ( (prMsduInfo = cnmMgtPktAlloc(prAdapter, u2EstimatedFrameLen)) == NULL) {
        DBGLOG(BSS, WARN, ("No PKT_INFO_T for sending Null Frame.\n"));
        return WLAN_STATUS_RESOURCES;
    }

    //4 <2> Compose Null frame in MSDU_INfO_T.
    bssComposeQoSNullFrame(prAdapter,
            (PUINT_8)((UINT_32)(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD),
            prStaRec,
            ucUP,
            FALSE);
#if 0
    //4 <3> Update information of MSDU_INFO_T
    TXM_SET_DATA_PACKET( \
    /* STA_REC ptr */       prStaRec, \
    /* MSDU_INFO ptr */     prMsduInfo, \
    /* MAC HDR ptr */       (prMsduInfo->pucBuffer + MAC_TX_RESERVED_FIELD), \
    /* MAC HDR length */    WLAN_MAC_HEADER_QOS_LEN, \
    /* PAYLOAD ptr */       (prMsduInfo->pucBuffer + MAC_TX_RESERVED_FIELD + WLAN_MAC_HEADER_QOS_LEN), \
    /* PAYLOAD length */    0, \
    /* Network Type Index */(UINT_8)prStaRec->ucNetTypeIndex, \
    /* TID */               0 /* BE: AC1 */, \
    /* Flag 802.11 */       TRUE,\
    /* Pkt arrival time */  0 /* TODO: Obtain the system time */, \
    /* Resource TC */       0 /* Irrelevant */, \
    /* Flag 802.1x */       FALSE,\
    /* TX-done callback */  pfTxDoneHandler, \
    /* PS forwarding type*/ PS_FORWARDING_TYPE_NON_PS, \
    /* PS Session ID */     0           /* Irrelevant */, \
    /* Flag fixed rate */   TRUE, \
    /* Fixed tx rate */     g_aprBssInfo[prStaRec->ucNetTypeIndex]->ucHwDefaultFixedRateCode, \
    /* Fixed-rate retry */  TXM_DEFAULT_DATA_FRAME_RETRY_LIMIT, \
    /* PAL LLH */           0 /* Irrelevant */,\
    /* ACL SN */            0 /* Irrelevant */,\
    /* Flag No Ack */       FALSE \
    );

    /* Terminate with a NULL pointer */
    NIC_HIF_TX_SET_NEXT_MSDU_INFO(prMsduInfo,NULL);

    /* TODO(Kevin): Also release the unused tail room of the composed MMPDU */

    /* Indicate the packet to TXM */
    //4 <4> Inform TXM to send this Null frame.
    txmSendFwDataPackets(prMsduInfo);
#endif

    prMsduInfo->eSrc = TX_PACKET_MGMT;
    prMsduInfo->ucPacketType = HIF_TX_PACKET_TYPE_MGMT;
    prMsduInfo->ucStaRecIndex = prStaRec->ucIndex;
    prMsduInfo->ucNetworkType = prStaRec->ucNetTypeIndex;
    prMsduInfo->ucMacHeaderLength = WLAN_MAC_HEADER_QOS_LEN;
    prMsduInfo->fgIs802_1x = FALSE;
    prMsduInfo->fgIs802_11 = TRUE;
    prMsduInfo->u2FrameLength = WLAN_MAC_HEADER_QOS_LEN;
    prMsduInfo->ucTxSeqNum = nicIncreaseTxSeqNum(prAdapter);
    prMsduInfo->pfTxDoneHandler = pfTxDoneHandler;
    prMsduInfo->fgIsBasicRate = TRUE;

    //4 <4> Inform TXM  to send this Null frame.
    nicTxEnqueueMsdu(prAdapter, prMsduInfo);

    return WLAN_STATUS_SUCCESS;

} /* end of bssSendQoSNullFrame() */


#if (CFG_SUPPORT_ADHOC) || (CFG_SUPPORT_AAA)
/*----------------------------------------------------------------------------*/
/* Routines for both IBSS(AdHoc) and BSS(AP)                                  */
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to generate Information Elements of Extended
*        Support Rate
*
* @param[in] prAdapter      Pointer to the Adapter structure.
* @param[in] prMsduInfo     Pointer to the composed MSDU_INFO_T.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssGenerateExtSuppRate_IE (
    IN P_ADAPTER_T      prAdapter,
    IN P_MSDU_INFO_T    prMsduInfo
    )
{
    P_BSS_INFO_T prBssInfo;
    PUINT_8 pucBuffer;
    UINT_8 ucExtSupRatesLen;


    ASSERT(prMsduInfo);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[prMsduInfo->ucNetworkType]);
    ASSERT(prBssInfo);

    pucBuffer = (PUINT_8)((UINT_32)prMsduInfo->prPacket +
                          (UINT_32)prMsduInfo->u2FrameLength);
    ASSERT(pucBuffer);

    if (prBssInfo->ucAllSupportedRatesLen > ELEM_MAX_LEN_SUP_RATES) {

        ucExtSupRatesLen = prBssInfo->ucAllSupportedRatesLen - ELEM_MAX_LEN_SUP_RATES;
    }
    else {
        ucExtSupRatesLen = 0;
    }

    /* Fill the Extended Supported Rates element. */
    if (ucExtSupRatesLen) {

        EXT_SUP_RATES_IE(pucBuffer)->ucId = ELEM_ID_EXTENDED_SUP_RATES;
        EXT_SUP_RATES_IE(pucBuffer)->ucLength = ucExtSupRatesLen;

        kalMemCopy(EXT_SUP_RATES_IE(pucBuffer)->aucExtSupportedRates,
                   &prBssInfo->aucAllSupportedRates[ELEM_MAX_LEN_SUP_RATES],
                   ucExtSupRatesLen);

        prMsduInfo->u2FrameLength += IE_SIZE(pucBuffer);
    }

    return;
} /* end of bssGenerateExtSuppRate_IE() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to compose Common Information Elements for Beacon
*        or Probe Response Frame.
*
* @param[in] prMsduInfo     Pointer to the composed MSDU_INFO_T.
* @param[in] prBssInfo      Pointer to the BSS_INFO_T.
* @param[in] pucDestAddr    Pointer to the Destination Address, if NULL, means Beacon.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssBuildBeaconProbeRespFrameCommonIEs (
    IN P_MSDU_INFO_T    prMsduInfo,
    IN P_BSS_INFO_T     prBssInfo,
    IN PUINT_8          pucDestAddr
    )
{
    PUINT_8 pucBuffer;
    UINT_8 ucSupRatesLen;


    ASSERT(prMsduInfo);
    ASSERT(prBssInfo);

    pucBuffer = (PUINT_8)((UINT_32)prMsduInfo->prPacket +
                          (UINT_32)prMsduInfo->u2FrameLength);
    ASSERT(pucBuffer);

    /* Compose the frame body of the Probe Response frame. */
    //4 <1> Fill the SSID element.
    SSID_IE(pucBuffer)->ucId = ELEM_ID_SSID;
    SSID_IE(pucBuffer)->ucLength = prBssInfo->ucSSIDLen;
    if (prBssInfo->ucSSIDLen) {
        kalMemCopy(SSID_IE(pucBuffer)->aucSSID, prBssInfo->aucSSID, prBssInfo->ucSSIDLen);
    }

    prMsduInfo->u2FrameLength += IE_SIZE(pucBuffer);
    pucBuffer += IE_SIZE(pucBuffer);


    //4 <2> Fill the Supported Rates element.
    if (prBssInfo->ucAllSupportedRatesLen > ELEM_MAX_LEN_SUP_RATES) {

        ucSupRatesLen = ELEM_MAX_LEN_SUP_RATES;
    }
    else {
        ucSupRatesLen = prBssInfo->ucAllSupportedRatesLen;
    }

    if (ucSupRatesLen) {
        SUP_RATES_IE(pucBuffer)->ucId = ELEM_ID_SUP_RATES;
        SUP_RATES_IE(pucBuffer)->ucLength = ucSupRatesLen;
        kalMemCopy(SUP_RATES_IE(pucBuffer)->aucSupportedRates,
                   prBssInfo->aucAllSupportedRates,
                   ucSupRatesLen);

        prMsduInfo->u2FrameLength += IE_SIZE(pucBuffer);
        pucBuffer += IE_SIZE(pucBuffer);
    }


    //4 <3> Fill the DS Parameter Set element.
    if (prBssInfo->eBand == BAND_2G4) {
        DS_PARAM_IE(pucBuffer)->ucId = ELEM_ID_DS_PARAM_SET;
        DS_PARAM_IE(pucBuffer)->ucLength = ELEM_MAX_LEN_DS_PARAMETER_SET;
        DS_PARAM_IE(pucBuffer)->ucCurrChnl = prBssInfo->ucPrimaryChannel;

        prMsduInfo->u2FrameLength += IE_SIZE(pucBuffer);
        pucBuffer += IE_SIZE(pucBuffer);
    }


    //4 <4> IBSS Parameter Set element, ID: 6
    if (prBssInfo->eCurrentOPMode == OP_MODE_IBSS) {
        IBSS_PARAM_IE(pucBuffer)->ucId = ELEM_ID_IBSS_PARAM_SET;
        IBSS_PARAM_IE(pucBuffer)->ucLength = ELEM_MAX_LEN_IBSS_PARAMETER_SET;
        WLAN_SET_FIELD_16(&(IBSS_PARAM_IE(pucBuffer)->u2ATIMWindow), prBssInfo->u2ATIMWindow);

        prMsduInfo->u2FrameLength += IE_SIZE(pucBuffer);
        pucBuffer += IE_SIZE(pucBuffer);
    }


    //4 <5> TIM element, ID: 5
    if ((!pucDestAddr) && // For Beacon only.
        (prBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT)) {

#if CFG_ENABLE_WIFI_DIRECT
        /*no fgIsP2PRegistered protect*/
        if (prBssInfo->ucNetTypeIndex == NETWORK_TYPE_P2P_INDEX) {
#if 0
            P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo;
            UINT_8 ucBitmapControl = 0;
            UINT_32 u4N1, u4N2;


            prP2pSpecificBssInfo = &(prAdapter->rWifiVar.rP2pSpecificBssInfo);

            // Clear existing value.
            prP2pSpecificBssInfo->ucBitmapCtrl = 0;
            kalMemZero(prP2pSpecificBssInfo->aucPartialVirtualBitmap,
                       sizeof(prP2pSpecificBssInfo->aucPartialVirtualBitmap));


            // IEEE 802.11 2007 - 7.3.2.6
            TIM_IE(pucBuffer)->ucId = ELEM_ID_TIM;
            TIM_IE(pucBuffer)->ucDTIMCount = prBssInfo->ucDTIMCount;
            TIM_IE(pucBuffer)->ucDTIMPeriod = prBssInfo->ucDTIMPeriod;

            // Setup DTIM Count for next TBTT.
            if (prBssInfo->ucDTIMCount == 0) {
                //3 *** pmQueryBufferedBCAST();
            }

            //3 *** pmQueryBufferedPSNode();
            /* TODO(Kevin): Call PM Module here to loop all STA_RECORD_Ts and it
             * will call bssSetTIMBitmap to toggle the Bitmap.
             */

            // Set Virtual Bitmap for UCAST
            u4N1 = (prP2pSpecificBssInfo->u2SmallestAID >> 4) << 1; // Find the largest even number.
            u4N2 = prP2pSpecificBssInfo->u2LargestAID >> 3; // Find the smallest number.

            ASSERT(u4N2 >= u4N1);

            kalMemCopy(TIM_IE(pucBuffer)->aucPartialVirtualMap,
                       &prP2pSpecificBssInfo->aucPartialVirtualBitmap[u4N1],
                       ((u4N2 - u4N1) + 1));

            // Set Virtual Bitmap for BMCAST
            // BMC bit only indicated when DTIM count == 0.
            if (prBssInfo->ucDTIMCount == 0) {
                ucBitmapControl = prP2pSpecificBssInfo->ucBitmapCtrl;
            }
            TIM_IE(pucBuffer)->ucBitmapControl = ucBitmapControl | (UINT_8)u4N1;

            TIM_IE(pucBuffer)->ucLength = ((u4N2 - u4N1) + 4);

            prMsduInfo->u2FrameLength += IE_SIZE(pucBuffer);
#else

            // IEEE 802.11 2007 - 7.3.2.6
            TIM_IE(pucBuffer)->ucId = ELEM_ID_TIM;
            TIM_IE(pucBuffer)->ucLength = (3 + MAX_LEN_TIM_PARTIAL_BMP)/*((u4N2 - u4N1) + 4)*/; // NOTE: fixed PVB length (AID is allocated from 8 ~ 15 only)
            TIM_IE(pucBuffer)->ucDTIMCount = 0/*prBssInfo->ucDTIMCount*/; // will be overwrite by FW
            TIM_IE(pucBuffer)->ucDTIMPeriod = prBssInfo->ucDTIMPeriod;
            TIM_IE(pucBuffer)->ucBitmapControl = 0/*ucBitmapControl | (UINT_8)u4N1*/; // will be overwrite by FW

            prMsduInfo->u2FrameLength += IE_SIZE(pucBuffer);

#endif

        }
        else
#endif /* CFG_ENABLE_WIFI_DIRECT */
        {
            /* NOTE(Kevin): 1. AIS - Didn't Support AP Mode.
             *              2. BOW - Didn't Support BCAST and PS.
             */
        }



    }

    return;
} /* end of bssBuildBeaconProbeRespFrameCommonIEs() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function will compose the Beacon/Probe Response frame header and
*        its fixed fields.
*
* @param[in] pucBuffer              Pointer to the frame buffer.
* @param[in] pucDestAddr            Pointer to the Destination Address, if NULL, means Beacon.
* @param[in] pucOwnMACAddress       Given Our MAC Address.
* @param[in] pucBSSID               Given BSSID of the BSS.
* @param[in] u2BeaconInterval       Given Beacon Interval.
* @param[in] u2CapInfo              Given Capability Info.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssComposeBeaconProbeRespFrameHeaderAndFF (
    IN PUINT_8      pucBuffer,
    IN PUINT_8      pucDestAddr,
    IN PUINT_8      pucOwnMACAddress,
    IN PUINT_8      pucBSSID,
    IN UINT_16      u2BeaconInterval,
    IN UINT_16      u2CapInfo
    )
{
    P_WLAN_BEACON_FRAME_T prBcnProbRspFrame;
    UINT_8 aucBCAddr[] = BC_MAC_ADDR;
    UINT_16 u2FrameCtrl;

    DEBUGFUNC("bssComposeBeaconProbeRespFrameHeaderAndFF");
    //DBGLOG(INIT, LOUD, ("\n"));


    ASSERT(pucBuffer);
    ASSERT(pucOwnMACAddress);
    ASSERT(pucBSSID);

    prBcnProbRspFrame = (P_WLAN_BEACON_FRAME_T)pucBuffer;

    //4 <1> Compose the frame header of the Beacon /ProbeResp frame.
    /* Fill the Frame Control field. */
    if (pucDestAddr) {
        u2FrameCtrl = MAC_FRAME_PROBE_RSP;
    }
    else {
        u2FrameCtrl = MAC_FRAME_BEACON;
        pucDestAddr = aucBCAddr;
    }
    //WLAN_SET_FIELD_16(&prBcnProbRspFrame->u2FrameCtrl, u2FrameCtrl);
    prBcnProbRspFrame->u2FrameCtrl = u2FrameCtrl; // NOTE(Kevin): Optimized for ARM

    /* Fill the DA field with BCAST MAC ADDR or TA of ProbeReq. */
    COPY_MAC_ADDR(prBcnProbRspFrame->aucDestAddr, pucDestAddr);

    /* Fill the SA field with our MAC Address. */
    COPY_MAC_ADDR(prBcnProbRspFrame->aucSrcAddr, pucOwnMACAddress);

    /* Fill the BSSID field with current BSSID. */
    COPY_MAC_ADDR(prBcnProbRspFrame->aucBSSID, pucBSSID);

    /* Clear the SEQ/FRAG_NO field(HW won't overide the FRAG_NO, so we need to clear it). */
    prBcnProbRspFrame->u2SeqCtrl = 0;


    //4 <2> Compose the frame body's common fixed field part of the Beacon /ProbeResp frame.
    /* MAC will update TimeStamp field */

    /* Fill the Beacon Interval field. */
    //WLAN_SET_FIELD_16(&prBcnProbRspFrame->u2BeaconInterval, u2BeaconInterval);
    prBcnProbRspFrame->u2BeaconInterval = u2BeaconInterval; // NOTE(Kevin): Optimized for ARM

    /* Fill the Capability Information field. */
    //WLAN_SET_FIELD_16(&prBcnProbRspFrame->u2CapInfo, u2CapInfo);
    prBcnProbRspFrame->u2CapInfo = u2CapInfo; // NOTE(Kevin): Optimized for ARM

    return;
} /* end of bssComposeBeaconProbeRespFrameHeaderAndFF() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Update the Beacon Frame Template to FW for AIS AdHoc and P2P GO.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] eNetTypeIndex      Specify which network reply the Probe Response.
*
* @retval WLAN_STATUS_SUCCESS   Success.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
bssUpdateBeaconContent (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex
    )
{
    P_BSS_INFO_T prBssInfo;
    P_MSDU_INFO_T prMsduInfo;
    P_WLAN_BEACON_FRAME_T prBcnFrame;
    UINT_32 i;

    DEBUGFUNC("bssUpdateBeaconContent");
    DBGLOG(INIT, LOUD, ("\n"));

    ASSERT(eNetTypeIndex < NETWORK_TYPE_INDEX_NUM);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetTypeIndex]);

    //4 <1> Allocate a PKT_INFO_T for Beacon Frame
    /* Allocate a MSDU_INFO_T */
    // For Beacon
    prMsduInfo = prBssInfo->prBeacon;

    // beacon prMsduInfo will be NULLify once BSS deactivated, so skip if it is
    if (prMsduInfo == NULL) {
        return WLAN_STATUS_SUCCESS;
    }

    //4 <2> Compose header
    bssComposeBeaconProbeRespFrameHeaderAndFF(
            (PUINT_8)((UINT_32)(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD),
            NULL,
            prBssInfo->aucOwnMacAddr,
            prBssInfo->aucBSSID,
            prBssInfo->u2BeaconInterval,
            prBssInfo->u2CapInfo);


    prMsduInfo->u2FrameLength = (WLAN_MAC_MGMT_HEADER_LEN +
        (TIMESTAMP_FIELD_LEN + BEACON_INTERVAL_FIELD_LEN + CAP_INFO_FIELD_LEN));

    prMsduInfo->ucNetworkType = eNetTypeIndex;

    //4 <3> Compose the frame body's Common IEs of the Beacon frame.
    bssBuildBeaconProbeRespFrameCommonIEs(prMsduInfo, prBssInfo, NULL);


    //4 <4> Compose IEs in MSDU_INFO_T

    /* Append IE for Beacon */
    for (i = 0; i < sizeof(txBcnIETable)/sizeof(APPEND_VAR_IE_ENTRY_T); i++) {
        if (txBcnIETable[i].pfnAppendIE) {
            txBcnIETable[i].pfnAppendIE(prAdapter, prMsduInfo);
        }
    }

    prBcnFrame = (P_WLAN_BEACON_FRAME_T)prMsduInfo->prPacket;

    return nicUpdateBeaconIETemplate(prAdapter,
            IE_UPD_METHOD_UPDATE_ALL,
            eNetTypeIndex,
            prBssInfo->u2CapInfo,
            (PUINT_8)prBcnFrame->aucInfoElem,
            prMsduInfo->u2FrameLength - OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem));


} /* end of bssUpdateBeaconContent() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Send the Beacon Frame(for BOW) or Probe Response Frame according to the given
*        Destination Address.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] eNetTypeIndex      Specify which network reply the Probe Response.
* @param[in] pucDestAddr        Pointer to the Destination Address to reply
* @param[in] u4ControlFlags     Control flags for information on Probe Response.
*
* @retval WLAN_STATUS_RESOURCE  No available resources to send frame.
* @retval WLAN_STATUS_SUCCESS   Success.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
bssSendBeaconProbeResponse (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN PUINT_8 pucDestAddr,
    IN UINT_32 u4ControlFlags
    )
{
    P_BSS_INFO_T prBssInfo;
    P_MSDU_INFO_T prMsduInfo;
    UINT_16 u2EstimatedFrameLen;
    UINT_16 u2EstimatedFixedIELen;
    UINT_16 u2EstimatedExtraIELen;
    P_APPEND_VAR_IE_ENTRY_T prIeArray = NULL;
    UINT_32 u4IeArraySize = 0;
    UINT_32 i;


    ASSERT(eNetTypeIndex < NETWORK_TYPE_INDEX_NUM);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetTypeIndex]);


    if (!pucDestAddr) { // For Beacon
        prIeArray = &txBcnIETable[0];
        u4IeArraySize = sizeof(txBcnIETable)/sizeof(APPEND_VAR_IE_ENTRY_T);
    }
    else {
        prIeArray = &txProbRspIETable[0];
        u4IeArraySize = sizeof(txProbRspIETable)/sizeof(APPEND_VAR_IE_ENTRY_T);
    }


    //4 <1> Allocate a PKT_INFO_T for Beacon /Probe Response Frame
    /* Allocate a MSDU_INFO_T */

    /* Init with MGMT Header Length + Length of Fixed Fields + Common IE Fields */
    u2EstimatedFrameLen = MAC_TX_RESERVED_FIELD + \
                          WLAN_MAC_MGMT_HEADER_LEN + \
                          TIMESTAMP_FIELD_LEN + \
                          BEACON_INTERVAL_FIELD_LEN + \
                          CAP_INFO_FIELD_LEN + \
                          (ELEM_HDR_LEN + ELEM_MAX_LEN_SSID) + \
                          (ELEM_HDR_LEN + ELEM_MAX_LEN_SUP_RATES) + \
                          (ELEM_HDR_LEN + ELEM_MAX_LEN_DS_PARAMETER_SET) + \
                          (ELEM_HDR_LEN + ELEM_MAX_LEN_IBSS_PARAMETER_SET) + \
                          (ELEM_HDR_LEN + (3 + MAX_LEN_TIM_PARTIAL_BMP));

    /* + Extra IE Length */
    u2EstimatedExtraIELen = 0;

    for (i = 0; i < u4IeArraySize; i++) {
        u2EstimatedFixedIELen = prIeArray[i].u2EstimatedFixedIELen;

        if (u2EstimatedFixedIELen) {
            u2EstimatedExtraIELen += u2EstimatedFixedIELen;
        }
        else {
            ASSERT(prIeArray[i].pfnCalculateVariableIELen);

            u2EstimatedExtraIELen += (UINT_16)
                prIeArray[i].pfnCalculateVariableIELen(prAdapter, eNetTypeIndex, NULL);
        }
    }

    u2EstimatedFrameLen += u2EstimatedExtraIELen;

    if ( (prMsduInfo = cnmMgtPktAlloc(prAdapter, u2EstimatedFrameLen)) == NULL) {
        DBGLOG(BSS, WARN, ("No PKT_INFO_T for sending %s.\n",
            ((!pucDestAddr)?"Beacon":"Probe Response")));
        return WLAN_STATUS_RESOURCES;
    }


    //4 <2> Compose Beacon/Probe Response frame header and fixed fields in MSDU_INfO_T.
    /* Compose Header and Fixed Field */
#if CFG_ENABLE_WIFI_DIRECT
    if (u4ControlFlags & BSS_PROBE_RESP_USE_P2P_DEV_ADDR) {
        if(prAdapter->fgIsP2PRegistered) {
            bssComposeBeaconProbeRespFrameHeaderAndFF(
                    (PUINT_8)((UINT_32)(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD),
                    pucDestAddr,
                    prAdapter->rWifiVar.aucDeviceAddress,
                    prAdapter->rWifiVar.aucDeviceAddress,
                    DOT11_BEACON_PERIOD_DEFAULT,
                    (prBssInfo->u2CapInfo & ~(CAP_INFO_ESS | CAP_INFO_IBSS)));
        }
    }
    else
#endif /* CFG_ENABLE_WIFI_DIRECT */
    {
        bssComposeBeaconProbeRespFrameHeaderAndFF(
                (PUINT_8)((UINT_32)(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD),
                pucDestAddr,
                prBssInfo->aucOwnMacAddr,
                prBssInfo->aucBSSID,
                prBssInfo->u2BeaconInterval,
                prBssInfo->u2CapInfo);
    }


    //4 <3> Update information of MSDU_INFO_T
    prMsduInfo->eSrc = TX_PACKET_MGMT;
    prMsduInfo->ucPacketType = HIF_TX_PACKET_TYPE_MGMT;
    prMsduInfo->ucStaRecIndex = 0xFF;
    prMsduInfo->ucNetworkType = (UINT_8)eNetTypeIndex;
    prMsduInfo->ucMacHeaderLength = WLAN_MAC_MGMT_HEADER_LEN;
    prMsduInfo->fgIs802_1x = FALSE;
    prMsduInfo->fgIs802_11 = TRUE;
    prMsduInfo->u2FrameLength = (WLAN_MAC_MGMT_HEADER_LEN +
        TIMESTAMP_FIELD_LEN + BEACON_INTERVAL_FIELD_LEN + CAP_INFO_FIELD_LEN);
    prMsduInfo->ucTxSeqNum = nicIncreaseTxSeqNum(prAdapter);
    prMsduInfo->pfTxDoneHandler = NULL;
    prMsduInfo->fgIsBasicRate = TRUE;


    //4 <4> Compose the frame body's Common IEs of the Beacon/ProbeResp  frame.
    bssBuildBeaconProbeRespFrameCommonIEs(prMsduInfo, prBssInfo, pucDestAddr);


    //4 <5> Compose IEs in MSDU_INFO_T

    /* Append IE */
    for (i = 0; i < u4IeArraySize; i++) {
        if (prIeArray[i].pfnAppendIE) {
            prIeArray[i].pfnAppendIE(prAdapter, prMsduInfo);
        }
    }

    /* TODO(Kevin): Also release the unused tail room of the composed MMPDU */

    //4 <6> Inform TXM  to send this Beacon /Probe Response frame.
    nicTxEnqueueMsdu(prAdapter, prMsduInfo);

    return WLAN_STATUS_SUCCESS;

} /* end of bssSendBeaconProbeResponse() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function will process the Rx Probe Request Frame and then send
*        back the corresponding Probe Response Frame if the specified conditions
*        were matched.
*
* @param[in] prAdapter          Pointer to the Adapter structure.
* @param[in] prSwRfb            Pointer to SW RFB data structure.
*
* @retval WLAN_STATUS_SUCCESS   Always return success
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
bssProcessProbeRequest (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    )
{
    P_WLAN_MAC_MGMT_HEADER_T prMgtHdr;
    P_BSS_INFO_T prBssInfo;
    ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex;
    UINT_8 aucBCBSSID[] = BC_BSSID;
    BOOLEAN fgIsBcBssid;
    BOOLEAN fgReplyProbeResp;
    UINT_32 u4CtrlFlagsForProbeResp = 0;
    ENUM_BAND_T             eBand;
    UINT_8                  ucHwChannelNum;


    ASSERT(prSwRfb);

    //4 <1> Parse Probe Req and Get BSSID
    prMgtHdr = (P_WLAN_MAC_MGMT_HEADER_T)prSwRfb->pvHeader;

    if (EQUAL_MAC_ADDR(aucBCBSSID, prMgtHdr->aucBSSID)) {
        fgIsBcBssid = TRUE;
    }
    else {
        fgIsBcBssid = FALSE;
    }


    //4 <2> Check network conditions before reply Probe Response Frame (Consider Concurrent)
    for (eNetTypeIndex = NETWORK_TYPE_AIS_INDEX; eNetTypeIndex < NETWORK_TYPE_INDEX_NUM; eNetTypeIndex++) {

        if (!IS_NET_ACTIVE(prAdapter, eNetTypeIndex)) {
            continue;
        }

        prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetTypeIndex]);

        if ((!fgIsBcBssid) &&
            UNEQUAL_MAC_ADDR(prBssInfo->aucBSSID, prMgtHdr->aucBSSID)) {
            continue;
        }

        eBand = HIF_RX_HDR_GET_RF_BAND(prSwRfb->prHifRxHdr);
        ucHwChannelNum = HIF_RX_HDR_GET_CHNL_NUM(prSwRfb->prHifRxHdr);

        if(prBssInfo->eBand != eBand) {
            continue;
        }

        if(prBssInfo->ucPrimaryChannel != ucHwChannelNum) {
            continue;
        }

        fgReplyProbeResp = FALSE;

        if (NETWORK_TYPE_AIS_INDEX == eNetTypeIndex) {

#if CFG_SUPPORT_ADHOC
            fgReplyProbeResp = aisValidateProbeReq(prAdapter, prSwRfb, &u4CtrlFlagsForProbeResp);
#endif
        }
#if CFG_ENABLE_WIFI_DIRECT
        else if ((prAdapter->fgIsP2PRegistered) &&
                 (NETWORK_TYPE_P2P_INDEX == eNetTypeIndex)) {

            fgReplyProbeResp = p2pFuncValidateProbeReq(prAdapter, prSwRfb, &u4CtrlFlagsForProbeResp);
        }
#endif
#if CFG_ENABLE_BT_OVER_WIFI
        else if (NETWORK_TYPE_BOW_INDEX == eNetTypeIndex) {

            fgReplyProbeResp = bowValidateProbeReq(prAdapter, prSwRfb, &u4CtrlFlagsForProbeResp);
        }
#endif

       if (fgReplyProbeResp) {
            if(nicTxGetFreeCmdCount(prAdapter) > (CFG_TX_MAX_CMD_PKT_NUM/2) ){
                /* Resource margin is enough */
                bssSendBeaconProbeResponse(prAdapter, eNetTypeIndex, prMgtHdr->aucSrcAddr, u4CtrlFlagsForProbeResp);
            }
        }
    }

    return WLAN_STATUS_SUCCESS;

} /* end of bssProcessProbeRequest() */


#if 0 // NOTE(Kevin): condition check should move to P2P_FSM.c
/*----------------------------------------------------------------------------*/
/*!
* @brief This function will process the Rx Probe Request Frame and then send
*        back the corresponding Probe Response Frame if the specified conditions
*        were matched.
*
* @param[in] prSwRfb            Pointer to SW RFB data structure.
*
* @retval WLAN_STATUS_SUCCESS   Always return success
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
bssProcessProbeRequest (
    IN P_ADAPTER_T  prAdapter,
    IN P_SW_RFB_T   prSwRfb
    )
{
    P_WLAN_MAC_MGMT_HEADER_T prMgtHdr;
    P_BSS_INFO_T prBssInfo;
    P_IE_SSID_T prIeSsid = (P_IE_SSID_T)NULL;
    P_IE_SUPPORTED_RATE_T prIeSupportedRate = (P_IE_SUPPORTED_RATE_T)NULL;
    P_IE_EXT_SUPPORTED_RATE_T prIeExtSupportedRate = (P_IE_EXT_SUPPORTED_RATE_T)NULL;
    PUINT_8 pucIE;
    UINT_16 u2IELength;
    UINT_16 u2Offset = 0;
    UINT_8 aucBCBSSID[] = BC_BSSID;
    ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex;
    BOOLEAN fgReplyProbeResp;
#if CFG_ENABLE_WIFI_DIRECT
    BOOLEAN fgP2PTargetDeviceFound;
    UINT_8 aucP2PWildcardSSID[] = P2P_WILDCARD_SSID;
#endif

    ASSERT(prSwRfb);

    //4 <1> Parse Probe Req and Get SSID IE ptr
    prMgtHdr = (P_WLAN_MAC_MGMT_HEADER_T)prSwRfb->pvHeader;

    u2IELength = prSwRfb->u2PacketLen - prSwRfb->u2HeaderLen;
    pucIE = (PUINT_8)((UINT_32)prSwRfb->pvHeader + prSwRfb->u2HeaderLen);

    prIeSsid = (P_IE_SSID_T)NULL;

    IE_FOR_EACH(pucIE, u2IELength, u2Offset) {
        switch (IE_ID(pucIE)) {
        case ELEM_ID_SSID:
            if ((!prIeSsid) &&
                (IE_LEN(pucIE) <= ELEM_MAX_LEN_SSID)) {
                prIeSsid = (P_IE_SSID_T)pucIE;
            }
            break;

        case ELEM_ID_SUP_RATES:
            /* NOTE(Kevin): Buffalo WHR-G54S's supported rate set IE exceed 8.
             * IE_LEN(pucIE) == 12, "1(B), 2(B), 5.5(B), 6(B), 9(B), 11(B),
             * 12(B), 18(B), 24(B), 36(B), 48(B), 54(B)"
             */
            // if (IE_LEN(pucIE) <= ELEM_MAX_LEN_SUP_RATES) {
            if (IE_LEN(pucIE) <= RATE_NUM) {
                prIeSupportedRate = SUP_RATES_IE(pucIE);
            }
            break;

        case ELEM_ID_EXTENDED_SUP_RATES:
            prIeExtSupportedRate = EXT_SUP_RATES_IE(pucIE);
            break;

#if CFG_ENABLE_WIFI_DIRECT
        // TODO: P2P IE & WCS IE parsing for P2P.
        case ELEM_ID_P2P:

            break;
#endif

        /* no default */
        }
    } /* end of IE_FOR_EACH */

    //4 <2> Check network conditions before reply Probe Response Frame (Consider Concurrent)
    for (eNetTypeIndex = NETWORK_TYPE_AIS_INDEX; eNetTypeIndex < NETWORK_TYPE_INDEX_NUM; eNetTypeIndex++) {

        if (!IS_NET_ACTIVE(prAdapter, eNetTypeIndex)) {
            continue;
        }

        prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetTypeIndex]);

        if (UNEQUAL_MAC_ADDR(aucBCBSSID, prMgtHdr->aucBSSID) &&
            UNEQUAL_MAC_ADDR(prBssInfo->aucBSSID, prMgtHdr->aucBSSID)) {
            /* BSSID not Wildcard BSSID. */
            continue;
        }

        fgReplyProbeResp = FALSE;

        if (NETWORK_TYPE_AIS_INDEX == eNetTypeIndex) {

            if (prBssInfo->eCurrentOPMode == OP_MODE_IBSS) {

                /* TODO(Kevin): Check if we are IBSS Master. */
                if (TRUE) {

                    if (prIeSsid) {
                        if ((prIeSsid->ucLength == BC_SSID_LEN) || /* WILDCARD SSID */
                            EQUAL_SSID(prBssInfo->aucSSID, prBssInfo->ucSSIDLen,
                                       prIeSsid->aucSSID, prIeSsid->ucLength)) {
                            fgReplyProbeResp = TRUE;
                        }
                    }
                }
            }
        }
#if CFG_ENABLE_WIFI_DIRECT
        else if (NETWORK_TYPE_P2P_INDEX == eNetTypeIndex) {

            // TODO(Kevin): Move following lines to p2p_fsm.c

            if ((prIeSsid) &&
                    ((prIeSsid->ucLength == BC_SSID_LEN) ||
                    (EQUAL_SSID(aucP2PWildcardSSID,
                        P2P_WILDCARD_SSID_LEN,
                        prIeSsid->aucSSID,
                        prIeSsid->ucLength)))) {
//                if (p2pFsmRunEventRxProbeRequestFrame(prAdapter, prMgtHdr->aucSrcAddr, pucIE, u2IELength)) {
                if (p2pFsmRunEventRxProbeRequestFrame(prAdapter, prSwRfb)) {
                    /* Extand channel request time & cancel scan request. */
                    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;

                    // TODO: RX probe request may not caused by LISTEN state.
                    // TODO: It can be GO.
                    /* Generally speaking, cancel a non-exist scan request is fine.
                      * We can check P2P FSM here for only LISTEN state.
                      */

                    P_MSG_SCN_SCAN_CANCEL prScanCancelMsg;

                    prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

                    /* Abort JOIN process. */
                    prScanCancelMsg = (P_MSG_SCN_SCAN_CANCEL)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_SCN_SCAN_CANCEL));
                    if (!prScanCancelMsg) {
                        ASSERT(0); // Can't abort SCN FSM
                        continue;
                    }

                    prScanCancelMsg->rMsgHdr.eMsgId = MID_P2P_SCN_SCAN_CANCEL;
                    prScanCancelMsg->ucSeqNum = prP2pFsmInfo->ucSeqNumOfScnMsg;
                    prScanCancelMsg->ucNetTypeIndex = (UINT_8)NETWORK_TYPE_P2P_INDEX;
                    prScanCancelMsg->fgIsChannelExt = TRUE;

                    mboxSendMsg(prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T) prScanCancelMsg,
                            MSG_SEND_METHOD_BUF);
              }
            }
            else {
                /* 1. Probe Request without SSID.
                  * 2. Probe Request with SSID not Wildcard SSID & not P2P Wildcard SSID.
                  */
                continue;
            }

#if 0 // Frog
            if (prAdapter->rWifiVar.prP2pFsmInfo->eCurrentState == P2P_STATE_LISTEN) {
                // P2P 2.4.1 - P2P Devices shall not respond to Probe Request frames which only contain 11b rates only.
                if (prIeSupportedRate || prIeExtSupportedRate) {
                    UINT_16 u2OperationalRateSet, u2BSSBasicRateSet;
                    BOOLEAN fgIsUnknownBssBasicRate;

                    rateGetRateSetFromIEs(prIeSupportedRate,
                                          prIeExtSupportedRate,
                                          &u2OperationalRateSet,
                                          &u2BSSBasicRateSet, /* Ignore any Basic Bit */
                                          &fgIsUnknownBssBasicRate);

                    if (u2OperationalRateSet & ~RATE_SET_HR_DSSS) {
                        continue;
                    }
                }
            }

            // TODO: Check channel time before first check point to:
            /* If Target device is selected:
              *     1. Send XXXX request frame.
              * else
              *     1. Send Probe Response frame.
              */

            if (prBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT) {
                /* TODO(Kevin): During PROVISION state, can we reply Probe Response ? */

                /* TODO(Kevin):
                           * If we are GO, accept legacy client --> accept Wildcard SSID
                           * If we are in Listen State, accept only P2P Device --> check P2P IE and WPS IE
                           */
                if (TRUE /* We are GO */) {
                    if (prIeSsid) {
                        UINT_8 aucSSID[]=P2P_WILDCARD_SSID;

                        if ((prIeSsid->ucLength == BC_SSID_LEN) || /* WILDCARD SSID */
                            EQUAL_SSID(prBssInfo->aucSSID, prBssInfo->ucSSIDLen,
                                       prIeSsid->aucSSID, prIeSsid->ucLength) ||
                            EQUAL_SSID(aucSSID, P2P_WILDCARD_SSID_LEN,
                                       prIeSsid->aucSSID, prIeSsid->ucLength)) {
                            fgReplyProbeResp = TRUE;
                        }
                    }
                }
//                else if (FALSE /* We are in Listen State */) {
//                }

                /* TODO(Kevin): Check P2P IE and WPS IE */
            }
#endif
        }
#endif
#if CFG_ENABLE_BT_OVER_WIFI
        else if (NETWORK_TYPE_BOW_INDEX == eNetTypeIndex) {

            if (prBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT) {
                /* TODO(Kevin): TBD */
            }
        }
#endif
        else {
            ASSERT(eNetTypeIndex < NETWORK_TYPE_INDEX_NUM);
        }

        if (fgReplyProbeResp) {
            bssSendBeaconProbeResponse(prAdapter, eNetTypeIndex, prMgtHdr->aucSrcAddr);
        }

    }

    return WLAN_STATUS_SUCCESS;

} /* end of bssProcessProbeRequest() */
#endif


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to clear the client list for AdHoc or AP Mode
*
* @param[in] prAdapter              Pointer to the Adapter structure.
* @param[in] prBssInfo              Given related BSS_INFO_T.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssClearClientList (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo
    )
{
    P_LINK_T prStaRecOfClientList;


    ASSERT(prBssInfo);

    prStaRecOfClientList = &prBssInfo->rStaRecOfClientList;

    if (!LINK_IS_EMPTY(prStaRecOfClientList)) {
        P_STA_RECORD_T prPeerStaRec;

        LINK_FOR_EACH_ENTRY(prPeerStaRec, prStaRecOfClientList, rLinkEntry, STA_RECORD_T) {
            cnmStaRecChangeState(prAdapter, prPeerStaRec, STA_STATE_1);
        }

        LINK_INITIALIZE(prStaRecOfClientList);
    }

    return;
} /* end of bssClearClientList() */



/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to Add a STA_RECORD_T to the client list for AdHoc or AP Mode
*
* @param[in] prAdapter              Pointer to the Adapter structure.
* @param[in] prBssInfo              Given related BSS_INFO_T.
* @param[in] prStaRec               Pointer to the STA_RECORD_T
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssAddStaRecToClientList (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN P_STA_RECORD_T prStaRec
    )
{
    P_LINK_T prStaRecOfClientList;


    ASSERT(prBssInfo);

    prStaRecOfClientList = &prBssInfo->rStaRecOfClientList;

    if (!LINK_IS_EMPTY(prStaRecOfClientList)) {
        P_STA_RECORD_T prCurrStaRec;

        LINK_FOR_EACH_ENTRY(prCurrStaRec, prStaRecOfClientList, rLinkEntry, STA_RECORD_T) {

            if (prCurrStaRec == prStaRec) {
                DBGLOG(BSS, WARN, ("Current Client List already contains that STA_RECORD_T["MACSTR"]\n",
                    MAC2STR(prStaRec->aucMacAddr)));
                return;
            }
        }
    }

    LINK_INSERT_TAIL(prStaRecOfClientList, &prStaRec->rLinkEntry);

    return;
} /* end of bssAddStaRecToClientList() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to Remove a STA_RECORD_T from the client list for AdHoc or AP Mode
*
* @param[in] prAdapter              Pointer to the Adapter structure.
* @param[in] prStaRec               Pointer to the STA_RECORD_T
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssRemoveStaRecFromClientList (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN P_STA_RECORD_T prStaRec
    )
{
    P_LINK_T prStaRecOfClientList;


    ASSERT(prBssInfo);

    prStaRecOfClientList = &prBssInfo->rStaRecOfClientList;

#if 0
    if (!LINK_IS_EMPTY(prStaRecOfClientList)) {
        P_STA_RECORD_T prCurrStaRec;

        LINK_FOR_EACH_ENTRY(prCurrStaRec, prStaRecOfClientList, rLinkEntry, STA_RECORD_T) {

            if (prCurrStaRec == prStaRec) {

                LINK_REMOVE_KNOWN_ENTRY(prStaRecOfClientList, &prStaRec->rLinkEntry);

                return;
            }
        }
    }
#endif
    if (!LINK_IS_EMPTY(prStaRecOfClientList)) {
           
           P_LINK_ENTRY_T prLinkEntry = (P_LINK_ENTRY_T)NULL;

           LINK_FOR_EACH(prLinkEntry, prStaRecOfClientList) {
                    if ((UINT_32)prStaRec == (UINT_32)prLinkEntry) {
                        LINK_REMOVE_KNOWN_ENTRY(prStaRecOfClientList, &prStaRec->rLinkEntry);
                       return;
                    }
           }
    }


    DBGLOG(BSS, INFO, ("Current Client List didn't contain that STA_RECORD_T["MACSTR"] before removing.\n",
        MAC2STR(prStaRec->aucMacAddr)));

    return;
} /* end of bssRemoveStaRecFromClientList() */
#endif /* CFG_SUPPORT_ADHOC || CFG_SUPPORT_AAA */


#if CFG_SUPPORT_ADHOC
/*----------------------------------------------------------------------------*/
/* Routines for IBSS(AdHoc) only                                              */
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to process Beacons from current Ad-Hoc network peers.
*        We also process Beacons from other Ad-Hoc network during SCAN. If it has
*        the same SSID and we'll decide to merge into it if it has a larger TSF.
*
* @param[in] prAdapter  Pointer to the Adapter structure.
* @param[in] prBssInfo  Pointer to the BSS_INFO_T.
* @param[in] prBSSDesc  Pointer to the BSS Descriptor.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
ibssProcessMatchedBeacon (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN P_BSS_DESC_T prBssDesc,
    IN UINT_8       ucRCPI
    )
{
    P_STA_RECORD_T prStaRec = NULL;

    BOOLEAN fgIsCheckCapability = FALSE;
    BOOLEAN fgIsCheckTSF = FALSE;
    BOOLEAN fgIsGoingMerging = FALSE;
    BOOLEAN fgIsSameBSSID;


    ASSERT(prBssInfo);
    ASSERT(prBssDesc);

    //4 <1> Process IBSS Beacon only after we create or merge with other IBSS.
    if (!prBssInfo->fgIsBeaconActivated) {
        return;
    }

    //4 <2> Get the STA_RECORD_T of TA.
    prStaRec = cnmGetStaRecByAddress(prAdapter,
                            (UINT_8) NETWORK_TYPE_AIS_INDEX,
                            prBssDesc->aucSrcAddr);

    fgIsSameBSSID = UNEQUAL_MAC_ADDR(prBssInfo->aucBSSID, prBssDesc->aucBSSID) ? FALSE : TRUE;


    //4 <3> IBSS Merge Decision Flow for Processing Beacon.
    if (fgIsSameBSSID) {

        /* Same BSSID:
         * Case I.  This is a new TA and it has decide to merged with us.
         *      a)  If fgIsMerging == FALSE - we will send msg to notify AIS.
         *      b)  If fgIsMerging == TRUE - already notify AIS.
         * Case II. This is an old TA and we've already merged together.
         */
        if (!prStaRec) {

            /* For Case I - Check this IBSS's capability first before adding this Sta Record. */
            fgIsCheckCapability = TRUE;

            /* If check is passed, then we perform merging with this new IBSS */
            fgIsGoingMerging = TRUE;

        }
        else {

            ASSERT((prStaRec->ucNetTypeIndex == NETWORK_TYPE_AIS_INDEX) &&
                IS_ADHOC_STA(prStaRec));

            if (prStaRec->ucStaState != STA_STATE_3) {

                if (!prStaRec->fgIsMerging) {

                    /* For Case I - Check this IBSS's capability first before adding this Sta Record. */
                    fgIsCheckCapability = TRUE;

                    /* If check is passed, then we perform merging with this new IBSS */
                    fgIsGoingMerging = TRUE;
                }
                else {
                    /* For Case II - Update rExpirationTime of Sta Record */
                    GET_CURRENT_SYSTIME(&prStaRec->rUpdateTime);
                }
            }
            else {
                /* For Case II - Update rExpirationTime of Sta Record */
                GET_CURRENT_SYSTIME(&prStaRec->rUpdateTime);
            }

        }
    }
    else {

        /* Unequal BSSID:
         * Case III. This is a new TA and we need to compare the TSF and get the winner.
         * Case IV.  This is an old TA and it merge into a new IBSS before we do the same thing.
         *           We need to compare the TSF to get the winner.
         * Case V.   This is an old TA and it restart a new IBSS. We also need to
         *           compare the TSF to get the winner.
         */

        /* For Case III, IV & V - We'll always check this new IBSS's capability first
         * before merging into new IBSS.
         */
        fgIsCheckCapability = TRUE;

        /* If check is passed, we need to perform TSF check to decide the major BSSID */
        fgIsCheckTSF = TRUE;

        /* For Case IV & V - We won't update rExpirationTime of Sta Record */
    }


    //4 <7> Check this BSS_DESC_T's capability.
    if (fgIsCheckCapability) {
        BOOLEAN fgIsCapabilityMatched = FALSE;

        do {
            if (!(prBssDesc->ucPhyTypeSet & (prAdapter->rWifiVar.ucAvailablePhyTypeSet))) {
                DBGLOG(BSS, LOUD,
                    ("IBSS MERGE: Ignore Peer MAC: "MACSTR" - Unsupported Phy.\n",
                     MAC2STR(prBssDesc->aucSrcAddr)));

                break;
            }

            if (prBssDesc->fgIsUnknownBssBasicRate) {
                DBGLOG(BSS, LOUD,
                    ("IBSS MERGE: Ignore Peer MAC: "MACSTR" - Unknown Basic Rate.\n",
                     MAC2STR(prBssDesc->aucSrcAddr)));

                break;
            }

            if (ibssCheckCapabilityForAdHocMode(prAdapter, prBssDesc) == WLAN_STATUS_FAILURE) {
                DBGLOG(BSS, LOUD,
                    ("IBSS MERGE: Ignore Peer MAC: "MACSTR" - Capability is not matched.\n",
                     MAC2STR(prBssDesc->aucSrcAddr)));

                break;
            }

            fgIsCapabilityMatched = TRUE;
        }
        while (FALSE);

        if (!fgIsCapabilityMatched) {

            if (prStaRec) {
                /* For Case II - We merge this STA_RECORD in RX Path.
                 *     Case IV & V - They change their BSSID after we merge with them.
                 */

                DBGLOG(BSS, LOUD,
                    ("IBSS MERGE: Ignore Peer MAC: "MACSTR" - Capability is not matched.\n",
                     MAC2STR(prBssDesc->aucSrcAddr)));
            }

            return;
        }

        DBGLOG(BSS, LOUD,
            ("IBSS MERGE: Peer MAC: "MACSTR" - Check capability was passed.\n",
             MAC2STR(prBssDesc->aucSrcAddr)));
    }


    if (fgIsCheckTSF) {
#if CFG_SLT_SUPPORT
        fgIsGoingMerging = TRUE;
#else
        if (prBssDesc->fgIsLargerTSF) {
            fgIsGoingMerging = TRUE;
        }
        else {
            return;
        }
#endif
    }


    if (fgIsGoingMerging) {
        P_MSG_AIS_IBSS_PEER_FOUND_T prAisIbssPeerFoundMsg;


        //4 <1> We will merge with to this BSS immediately.
        prBssDesc->fgIsConnecting = TRUE;
        prBssDesc->fgIsConnected = FALSE;

        //4 <2> Setup corresponding STA_RECORD_T
        prStaRec = bssCreateStaRecFromBssDesc(prAdapter,
                                              STA_TYPE_ADHOC_PEER,
                                              NETWORK_TYPE_AIS_INDEX,
                                              prBssDesc);

        if(!prStaRec) {
            // no memory ?
            return;
        }

        prStaRec->fgIsMerging = TRUE;

        /* update RCPI */
        prStaRec->ucRCPI = ucRCPI;

        //4 <3> Send Merge Msg to CNM to obtain the channel privilege.
        prAisIbssPeerFoundMsg = (P_MSG_AIS_IBSS_PEER_FOUND_T)
            cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_AIS_IBSS_PEER_FOUND_T));

        if (!prAisIbssPeerFoundMsg) {

            ASSERT(0); // Can't send Merge Msg
            return;
        }

        prAisIbssPeerFoundMsg->rMsgHdr.eMsgId = MID_SCN_AIS_FOUND_IBSS;
        prAisIbssPeerFoundMsg->ucNetTypeIndex = (UINT_8)NETWORK_TYPE_AIS_INDEX;
        prAisIbssPeerFoundMsg->prStaRec = prStaRec;

        /* Inform AIS to do STATE TRANSITION
         * For Case I - If AIS in IBSS_ALONE, let it jump to NORMAL_TR after we know the new member.
         * For Case III, IV - Now this new BSSID wins the TSF, follow it.
         */
        if (fgIsSameBSSID) {
            prAisIbssPeerFoundMsg->fgIsMergeIn = TRUE;
        }
        else {
#if CFG_SLT_SUPPORT
            prAisIbssPeerFoundMsg->fgIsMergeIn = TRUE;
#else
            prAisIbssPeerFoundMsg->fgIsMergeIn = (prBssDesc->fgIsLargerTSF) ? FALSE: TRUE;
#endif
        }

        mboxSendMsg(prAdapter,
                MBOX_ID_0,
                (P_MSG_HDR_T) prAisIbssPeerFoundMsg,
                MSG_SEND_METHOD_BUF);

    }

    return;
} /* end of ibssProcessMatchedBeacon() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function will check the Capability for Ad-Hoc to decide if we are
*        able to merge with(same capability).
*
* @param[in] prBSSDesc  Pointer to the BSS Descriptor.
*
* @retval WLAN_STATUS_FAILURE   Can't pass the check of Capability.
* @retval WLAN_STATUS_SUCCESS   Pass the check of Capability.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
ibssCheckCapabilityForAdHocMode (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_DESC_T prBssDesc
    )
{
    P_CONNECTION_SETTINGS_T prConnSettings;
    WLAN_STATUS rStatus = WLAN_STATUS_FAILURE;


    ASSERT(prBssDesc);
    prConnSettings = &(prAdapter->rWifiVar.rConnSettings);

    do {
        //4 <1> Check the BSS Basic Rate Set for current AdHoc Mode
        if ((prConnSettings->eAdHocMode == AD_HOC_MODE_11B) &&
            (prBssDesc->u2BSSBasicRateSet & ~RATE_SET_HR_DSSS)) {
            break;
        }
        else if ((prConnSettings->eAdHocMode == AD_HOC_MODE_11A) &&
            (prBssDesc->u2BSSBasicRateSet & ~RATE_SET_OFDM)) {
            break;
        }

        //4 <2> Check the Short Slot Time.
#if 0 // Do not check ShortSlotTime until Wi-Fi define such policy
        if (prConnSettings->eAdHocMode == AD_HOC_MODE_11G) {
            if (((prConnSettings->fgIsShortSlotTimeOptionEnable) &&
                 !(prBssDesc->u2CapInfo & CAP_INFO_SHORT_SLOT_TIME)) ||
                (!(prConnSettings->fgIsShortSlotTimeOptionEnable) &&
                 (prBssDesc->u2CapInfo & CAP_INFO_SHORT_SLOT_TIME))) {
                break;
            }
        }
#endif

        //4 <3> Check the ATIM window setting.
        if (prBssDesc->u2ATIMWindow) {
            DBGLOG(BSS, INFO, ("AdHoc PS was not supported(ATIM Window: %d)\n",
                prBssDesc->u2ATIMWindow));
            break;
        }

#if CFG_RSN_MIGRATION
        //4 <4> Check the Security setting.
        if (!rsnPerformPolicySelection(prAdapter, prBssDesc)) {
            break;
        }
#endif

        rStatus = WLAN_STATUS_SUCCESS;
    }
    while (FALSE);

    return rStatus;

} /* end of ibssCheckCapabilityForAdHocMode() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function will initial the BSS_INFO_T for IBSS Mode.
*
* @param[in] prBssInfo      Pointer to the BSS_INFO_T.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
ibssInitForAdHoc (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo
    )
{
    UINT_8 ucLowestBasicRateIndex;
    UINT_8 aucBSSID[MAC_ADDR_LEN];
    PUINT_16 pu2BSSID = (PUINT_16)&aucBSSID[0];
    UINT_32 i;


    ASSERT(prBssInfo);
    ASSERT(prBssInfo->eCurrentOPMode == OP_MODE_IBSS);


    //4 <1> Setup PHY Attributes and Basic Rate Set/Operational Rate Set
    prBssInfo->ucNonHTBasicPhyType = (UINT_8)
        rNonHTAdHocModeAttributes[prBssInfo->ucConfigAdHocAPMode].ePhyTypeIndex;
    prBssInfo->u2BSSBasicRateSet =
        rNonHTAdHocModeAttributes[prBssInfo->ucConfigAdHocAPMode].u2BSSBasicRateSet;


    prBssInfo->u2OperationalRateSet =
        rNonHTPhyAttributes[prBssInfo->ucNonHTBasicPhyType].u2SupportedRateSet;

    rateGetDataRatesFromRateSet(prBssInfo->u2OperationalRateSet,
                                prBssInfo->u2BSSBasicRateSet,
                                prBssInfo->aucAllSupportedRates,
                                &prBssInfo->ucAllSupportedRatesLen);

    //4 <2> Setup BSSID
    if (!prBssInfo->fgHoldSameBssidForIBSS) {

        for (i = 0; i < sizeof(aucBSSID)/sizeof(UINT_16); i++) {
            pu2BSSID[i] = (UINT_16)(kalRandomNumber() & 0xFFFF);
        }

        aucBSSID[0] &= ~0x01; // 7.1.3.3.3 - The individual/group bit of the address is set to 0.
        aucBSSID[0] |= 0x02; // 7.1.3.3.3 - The universal/local bit of the address is set to 1.

        COPY_MAC_ADDR(prBssInfo->aucBSSID, aucBSSID);
    }


    //4 <3> Setup Capability - Short Preamble
    if (rNonHTPhyAttributes[prBssInfo->ucNonHTBasicPhyType].fgIsShortPreambleOptionImplemented &&
        ((prAdapter->rWifiVar.ePreambleType == PREAMBLE_TYPE_SHORT) || /* Short Preamble Option Enable is TRUE */
         (prAdapter->rWifiVar.ePreambleType == PREAMBLE_TYPE_AUTO)) ) {

        prBssInfo->fgIsShortPreambleAllowed = TRUE;
        prBssInfo->fgUseShortPreamble = TRUE;
    }
    else {
        prBssInfo->fgIsShortPreambleAllowed = FALSE;
        prBssInfo->fgUseShortPreamble = FALSE;
    }


    //4 <4> Setup Capability - Short Slot Time
    // 7.3.1.4 For IBSS, the Short Slot Time subfield shall be set to 0.
    prBssInfo->fgUseShortSlotTime = FALSE; /* Set to FALSE for AdHoc */


    //4 <5> Compoase Capability
    prBssInfo->u2CapInfo = CAP_INFO_IBSS;

    if (prBssInfo->fgIsProtection) {
        prBssInfo->u2CapInfo |= CAP_INFO_PRIVACY;
    }

    if (prBssInfo->fgIsShortPreambleAllowed) {
        prBssInfo->u2CapInfo |= CAP_INFO_SHORT_PREAMBLE;
    }

    if (prBssInfo->fgUseShortSlotTime) {
        prBssInfo->u2CapInfo |= CAP_INFO_SHORT_SLOT_TIME;
    }


    //4 <6> Find Lowest Basic Rate Index for default TX Rate of MMPDU
    rateGetLowestRateIndexFromRateSet(prBssInfo->u2BSSBasicRateSet, &ucLowestBasicRateIndex);

    prBssInfo->ucHwDefaultFixedRateCode =
        aucRateIndex2RateCode[PREAMBLE_DEFAULT_LONG_NONE][ucLowestBasicRateIndex];

    return;
} /* end of ibssInitForAdHoc() */

#endif /* CFG_SUPPORT_ADHOC */


#if CFG_SUPPORT_AAA

/*----------------------------------------------------------------------------*/
/* Routines for BSS(AP) only                                                  */
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*!
* @brief This function will initial the BSS_INFO_T for AP Mode.
*
* @param[in] prBssInfo              Given related BSS_INFO_T.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssInitForAP (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN BOOLEAN fgIsRateUpdate
    )
{
    UINT_8 ucLowestBasicRateIndex;

    P_AC_QUE_PARMS_T          prACQueParms;

    ENUM_WMM_ACI_T eAci;

    UINT_8 auCWminLog2ForBcast[WMM_AC_INDEX_NUM] = {  4/*BE*/, 4 /*BK*/, 3/*VO*/, 2/*VI*/};
    UINT_8 auCWmaxLog2ForBcast[WMM_AC_INDEX_NUM] = { 10, 10, 4, 3};
    UINT_8 auAifsForBcast[WMM_AC_INDEX_NUM] = { 3, 7, 2, 2 };
    UINT_8 auTxopForBcast[WMM_AC_INDEX_NUM] = { 0, 0, 94, 47 }; /* If the AP is OFDM */

    UINT_8 auCWminLog2[WMM_AC_INDEX_NUM] = {  4 /*BE*/, 4 /*BK*/, 3 /*VO*/, 2 /*VI*/};
    UINT_8 auCWmaxLog2[WMM_AC_INDEX_NUM] = { 7, 10, 4, 3};
    UINT_8 auAifs[WMM_AC_INDEX_NUM] = { 3, 7, 1, 1 };
    UINT_8 auTxop[WMM_AC_INDEX_NUM] = { 0, 0, 94, 47 }; /* If the AP is OFDM */

    DEBUGFUNC("bssInitForAP");
    DBGLOG(BSS, LOUD,("\n"));

    ASSERT(prBssInfo);
    ASSERT((prBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT) || (prBssInfo->eCurrentOPMode == OP_MODE_BOW));

#if 0
    prAdapter->rWifiVar.rConnSettings.fgRxShortGIDisabled = TRUE;
    prAdapter->rWifiVar.rConnSettings.uc2G4BandwidthMode = CONFIG_BW_20M;
    prAdapter->rWifiVar.rConnSettings.uc5GBandwidthMode = CONFIG_BW_20M;
#endif


    //4 <1> Setup PHY Attributes and Basic Rate Set/Operational Rate Set
    prBssInfo->ucNonHTBasicPhyType = (UINT_8)
            rNonHTApModeAttributes[prBssInfo->ucConfigAdHocAPMode].ePhyTypeIndex;
    prBssInfo->u2BSSBasicRateSet =
            rNonHTApModeAttributes[prBssInfo->ucConfigAdHocAPMode].u2BSSBasicRateSet;


    prBssInfo->u2OperationalRateSet =
            rNonHTPhyAttributes[prBssInfo->ucNonHTBasicPhyType].u2SupportedRateSet;

    if (fgIsRateUpdate) {
            rateGetDataRatesFromRateSet(prBssInfo->u2OperationalRateSet,
                                        prBssInfo->u2BSSBasicRateSet,
                                        prBssInfo->aucAllSupportedRates,
                                        &prBssInfo->ucAllSupportedRatesLen);
    }

    //4 <2> Setup BSSID
    COPY_MAC_ADDR(prBssInfo->aucBSSID, prBssInfo->aucOwnMacAddr);


    //4 <3> Setup Capability - Short Preamble
    if (rNonHTPhyAttributes[prBssInfo->ucNonHTBasicPhyType].fgIsShortPreambleOptionImplemented &&
        ((prAdapter->rWifiVar.ePreambleType == PREAMBLE_TYPE_SHORT) || /* Short Preamble Option Enable is TRUE */
         (prAdapter->rWifiVar.ePreambleType == PREAMBLE_TYPE_AUTO)) ) {

        prBssInfo->fgIsShortPreambleAllowed = TRUE;
        prBssInfo->fgUseShortPreamble = TRUE;
    }
    else {
        prBssInfo->fgIsShortPreambleAllowed = FALSE;
        prBssInfo->fgUseShortPreamble = FALSE;
    }


    //4 <4> Setup Capability - Short Slot Time
    prBssInfo->fgUseShortSlotTime = TRUE;

    //4 <5> Compoase Capability
    prBssInfo->u2CapInfo = CAP_INFO_ESS;

    if (prBssInfo->fgIsProtection) {
        prBssInfo->u2CapInfo |= CAP_INFO_PRIVACY;
    }

    if (prBssInfo->fgIsShortPreambleAllowed) {
        prBssInfo->u2CapInfo |= CAP_INFO_SHORT_PREAMBLE;
    }

    if (prBssInfo->fgUseShortSlotTime) {
        prBssInfo->u2CapInfo |= CAP_INFO_SHORT_SLOT_TIME;
    }


    //4 <6> Find Lowest Basic Rate Index for default TX Rate of MMPDU
    rateGetLowestRateIndexFromRateSet(prBssInfo->u2BSSBasicRateSet, &ucLowestBasicRateIndex);

    prBssInfo->ucHwDefaultFixedRateCode =
        aucRateIndex2RateCode[PREAMBLE_DEFAULT_LONG_NONE][ucLowestBasicRateIndex];


    //4 <7> Fill the EDCA

    prACQueParms = prBssInfo->arACQueParmsForBcast;

    for(eAci = 0; eAci < WMM_AC_INDEX_NUM; eAci++){

        prACQueParms[eAci].fgIsACMSet = FALSE;
        prACQueParms[eAci].u2Aifsn = auAifsForBcast[eAci];
        prACQueParms[eAci].u2CWmin = BIT(auCWminLog2ForBcast[eAci])-1;
        prACQueParms[eAci].u2CWmax = BIT(auCWmaxLog2ForBcast[eAci])-1;
        prACQueParms[eAci].u2TxopLimit = auTxopForBcast[eAci];

        prBssInfo->aucCWminLog2ForBcast[eAci] = auCWminLog2ForBcast[eAci] ; /* used to send WMM IE */
        prBssInfo->aucCWmaxLog2ForBcast[eAci] = auCWmaxLog2ForBcast[eAci] ;

        DBGLOG(BSS, INFO, ("Bcast: eAci = %d, ACM = %d, Aifsn = %d, CWmin = %d, CWmax = %d, TxopLimit = %d\n",
                   eAci,prACQueParms[eAci].fgIsACMSet ,
                   prACQueParms[eAci].u2Aifsn,
                   prACQueParms[eAci].u2CWmin,
                   prACQueParms[eAci].u2CWmax,
                   prACQueParms[eAci].u2TxopLimit));

    }

    prACQueParms = prBssInfo->arACQueParms;

    for(eAci = 0; eAci < WMM_AC_INDEX_NUM; eAci++){

        prACQueParms[eAci].fgIsACMSet = FALSE;
        prACQueParms[eAci].u2Aifsn = auAifs[eAci];
        prACQueParms[eAci].u2CWmin = BIT(auCWminLog2[eAci])-1;
        prACQueParms[eAci].u2CWmax = BIT(auCWmaxLog2[eAci])-1;
        prACQueParms[eAci].u2TxopLimit = auTxop[eAci];

        DBGLOG(BSS, INFO, ("eAci = %d, ACM = %d, Aifsn = %d, CWmin = %d, CWmax = %d, TxopLimit = %d\n",
                   eAci,prACQueParms[eAci].fgIsACMSet ,
                   prACQueParms[eAci].u2Aifsn,
                   prACQueParms[eAci].u2CWmin,
                   prACQueParms[eAci].u2CWmax,
                   prACQueParms[eAci].u2TxopLimit));
    }

    /* Note: Caller should update the EDCA setting to HW by nicQmUpdateWmmParms() it there is no AIS network */
    /* Note: In E2, only 4 HW queues. The the Edca parameters should be folow by AIS network */
    /* Note: In E3, 8 HW queues.  the Wmm parameters should be updated to right queues  according to BSS */


    return;
} /* end of bssInitForAP() */

#if 0
/*----------------------------------------------------------------------------*/
/*!
* @brief Update DTIM Count
*
* @param[in] eNetTypeIndex      Specify which network to update
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssUpdateDTIMCount (
    IN P_ADAPTER_T  prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex
    )
{
    P_BSS_INFO_T prBssInfo;


    ASSERT(eNetTypeIndex < NETWORK_TYPE_INDEX_NUM);

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[eNetTypeIndex]);

    if (prBssInfo->eCurrentOPMode == OP_MODE_ACCESS_POINT) {

        // Setup DTIM Count for next TBTT.
        if (prBssInfo->ucDTIMCount > 0) {
            prBssInfo->ucDTIMCount--;
        }
        else {

            ASSERT(prBssInfo->ucDTIMPeriod > 0);

            prBssInfo->ucDTIMCount = prBssInfo->ucDTIMPeriod - 1;
        }
    }

    return;
} /* end of bssUpdateDTIMIE() */


/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to set the Virtual Bitmap in TIM Information Elements
*
* @param[in] prBssInfo      Pointer to the BSS_INFO_T.
* @param[in] u2AssocId      The association id to set in Virtual Bitmap.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
bssSetTIMBitmap (
    IN P_ADAPTER_T  prAdapter,
    IN P_BSS_INFO_T prBssInfo,
    IN UINT_16 u2AssocId
    )
{

    ASSERT(prBssInfo);

    if (prBssInfo->ucNetTypeIndex == NETWORK_TYPE_P2P_INDEX) {
        P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo;


        prP2pSpecificBssInfo = &(prAdapter->rWifiVar.rP2pSpecificBssInfo);

        /* Use Association ID == 0 for BMCAST indication */
        if (u2AssocId == 0) {

            prP2pSpecificBssInfo->ucBitmapCtrl |= (UINT_8)BIT(0);
        }
        else {
            PUINT_8 pucPartialVirtualBitmap;
            UINT_8 ucBitmapToSet;


            pucPartialVirtualBitmap = &prP2pSpecificBssInfo->aucPartialVirtualBitmap[(u2AssocId >> 3)]; // (u2AssocId / 8)
            ucBitmapToSet = (UINT_8) BIT((u2AssocId % 8));

            if (*pucPartialVirtualBitmap & ucBitmapToSet) {
                /* The virtual bitmap has been set */
                return;
            }

            *pucPartialVirtualBitmap |= ucBitmapToSet;

            // Update u2SmallestAID and u2LargestAID
            if ((u2AssocId < prP2pSpecificBssInfo->u2SmallestAID) ||
                (prP2pSpecificBssInfo->u2SmallestAID == 0)) {
                prP2pSpecificBssInfo->u2SmallestAID = u2AssocId;
            }

            if ((u2AssocId > prP2pSpecificBssInfo->u2LargestAID) ||
                (prP2pSpecificBssInfo->u2LargestAID == 0)) {
                prP2pSpecificBssInfo->u2LargestAID = u2AssocId;
            }
        }
    }

    return;
} /* end of bssSetTIMBitmap() */
#endif

#endif /* CFG_SUPPORT_AAA */


VOID
bssCreateStaRecFromAuth (
    IN P_ADAPTER_T  prAdapter
    )
{

}


VOID
bssUpdateStaRecFromAssocReq (
    IN P_ADAPTER_T  prAdapter
    )
{

}


