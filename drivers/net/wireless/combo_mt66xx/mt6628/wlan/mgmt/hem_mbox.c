/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/mgmt/hem_mbox.c#3 $
*/

/*! \file   "hem_mbox.c"
    \brief

*/



/*
** $Log: hem_mbox.c $
** 
** 08 31 2012 yuche.tsai
** [ALPS00349585] [6577JB][WiFi direct][KE]Establish p2p connection while both device have connected to AP previously,one device reboots automatically with KE
** Fix possible KE when concurrent & disconnect.
** 
** 07 26 2012 yuche.tsai
** [ALPS00324337] [ALPS.JB][Hot-Spot] Driver update for Hot-Spot
** Update driver code of ALPS.JB for hot-spot.
** 
** 07 19 2012 yuche.tsai
** NULL
** Code update for JB.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 05 03 2012 cp.wu
 * [WCXRP00001231] [MT6620 Wi-Fi][MT5931][Driver] Correct SCAN_V2 related debugging facilities within hem_mbox.c
 * correct for debug message string table by adding missed scan_v2 related definitions.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Sync CFG80211 modification from branch 2,2.
 *
 * 01 17 2012 yuche.tsai
 * NULL
 * Update mgmt frame filter setting.
 * Please also update FW 2.1
 *
 * 01 13 2012 yuche.tsai
 * NULL
 * WiFi Hot Spot Tethering for ICS ALPHA testing version.
 *
 * 11 24 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Adjust code for DBG and CONFIG_XLOG.
 *
 * 11 15 2011 cm.chang
 * NULL
 * Add exception handle for NULL function pointer of mailbox message
 *
 * 11 11 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * modify the xlog related code.
 *
 * 11 02 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * adding the code for XLOG.
 *
 * 07 18 2011 cp.wu
 * [WCXRP00000858] [MT5931][Driver][Firmware] Add support for scan to search for more than one SSID in a single scanning request
 * add framework in driver domain for supporting new SCAN_REQ_V2 for more than 1 SSID support as well as uProbeDelay in NDIS 6.x driver model
 *
 * 06 07 2011 yuche.tsai
 * [WCXRP00000696] [Volunteer Patch][MT6620][Driver] Infinite loop issue when RX invitation response.[WCXRP00000763] [Volunteer Patch][MT6620][Driver] RX Service Discovery Frame under AP mode Issue
 * Add invitation support.
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 03 29 2011 cm.chang
 * [WCXRP00000606] [MT6620 Wi-Fi][Driver][FW] Fix klocwork warning
 * As CR title
 *
 * 02 24 2011 cp.wu
 * [WCXRP00000490] [MT6620 Wi-Fi][Driver][Win32] modify kalMsleep() implementation because NdisMSleep() won't sleep long enough for specified interval such as 500ms
 * modify cnm_timer and hem_mbox APIs to be thread safe to ease invoking restrictions
 *
 * 02 15 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Update bowString and channel grant.
 *
 * 01 26 2011 cm.chang
 * [WCXRP00000395] [MT6620 Wi-Fi][Driver][FW] Search STA_REC with additional net type index argument
 * Allocate system RAM if fixed message or mgmt buffer is not available
 *
 * 01 26 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * .
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Fix Compile Error when DBG is disabled.
 *
 * 01 24 2011 cp.wu
 * [WCXRP00000382] [MT6620 Wi-Fi][Driver] Track forwarding packet number with notifying tx thread for serving
 * 1. add an extra counter for tracking pending forward frames.
 * 2. notify TX service thread as well when there is pending forward frame
 * 3. correct build errors leaded by introduction of Wi-Fi direct separation module
 *
 * 12 08 2010 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Support concurrent networks.
 *
 * 11 08 2010 cm.chang
 * [WCXRP00000169] [MT6620 Wi-Fi][Driver][FW] Remove unused CNM recover message ID
 * Remove CNM channel reover message ID
 *
 * 11 01 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000150] [MT6620 Wi-Fi][Driver] Add implementation for querying current TX rate from firmware auto rate module
 * 1) Query link speed (TX rate) from firmware directly with buffering mechanism to reduce overhead
 * 2) Remove CNM CH-RECOVER event handling
 * 3) cfg read/write API renamed with kal prefix for unified naming rules.
 *
 * 10 08 2010 wh.su
 * [WCXRP00000085] [MT6620 Wif-Fi] [Driver] update the modified p2p state machine
 * update the frog's new p2p state machine.
 *
 * 09 28 2010 wh.su
 * NULL
 * [WCXRP00000069][MT6620 Wi-Fi][Driver] Fix some code for phase 1 P2P Demo.
 *
 * 09 16 2010 cm.chang
 * NULL
 * Remove unused message ID
 *
 * 08 30 2010 cp.wu
 * NULL
 * eliminate klockwork errors
 *
 * 08 26 2010 yuche.tsai
 * NULL
 * Add P2P Connection Abort Event Message handler.
 *
 * 08 25 2010 george.huang
 * NULL
 * update OID/ registry control path for PM related settings
 *
 * 08 24 2010 yarco.yang
 * NULL
 * Fixed Driver ASSERT at mboxInitMsgMap()
 *
 * 08 24 2010 chinghwa.yu
 * NULL
 * Update for MID_SCN_BOW_SCAN_DONE mboxDummy.
 * Update saa_fsm for BOW.
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Add CFG_ENABLE_BT_OVER_WIFI.
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Update for BOW.
 *
 * 08 16 2010 cp.wu
 * NULL
 * add interface for RLM to trigger OBSS-SCAN.
 *
 * 08 16 2010 yuche.tsai
 * NULL
 * Add debug message for newly add P2P message.
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Add some function entry for P2P FSM under provisioning phase..
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Add some events to P2P Module.
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Add message box event for P2P device switch on & device discovery.
 *
 * 08 04 2010 cp.wu
 * NULL
 * remove unused mailbox message definitions.
 *
 * 08 02 2010 yuche.tsai
 * NULL
 * P2P Group Negotiation Code Check in.
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * message table should not be commented out by compilation option without modifying header file
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * Add Ad-Hoc support to AIS-FSM
 *
 * 07 19 2010 yuche.tsai
 *
 * Add wifi direct scan done callback.
 *
 * 07 09 2010 cp.wu
 *
 * change handler of MID_MNY_CNM_CONNECTION_ABORT from NULL to mboxDummy.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Rename MID_MNY_CNM_CH_RELEASE to MID_MNY_CNM_CH_ABORT
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * AIS-FSM integration with CNM channel request messages
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * implementation of DRV-SCN and related mailbox message handling.
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Modify CNM message handler for new flow
 *
 * 06 15 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * enable currently migrated message call-backs.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * restore utility function invoking via hem_mbox to direct calls
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add buildable & linkable ais_fsm.c
 *
 * related reference are still waiting to be resolved
 *
 * 06 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * hem_mbox is migrated.
 *
 * 06 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add hem_mbox.c and cnm_mem.h (but disabled some feature) for further migration
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Fix file merge error
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 04 29 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Removed MID_RXM_MQM_QOS_ACTION_FRAME
 *
 * 04 29 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Removed MID_RXM_MQM_BA_ACTION_FRAME
 *
 * 04 27 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * MID_RXM_MQM_BA_ACTION_FRAME
 *
 * 03 30 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support 2.4G OBSS scan
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support
 *
 *  *  *  *  *  *  *  *  *  *  *  *  *  * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 03 05 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Develop partial DPD code
 *
 * 02 11 2010 tehuang.liu
 * [BORA00000569][WIFISYS] Phase 2 Integration Test
 * Updated arMsgMapTable for MID_RXM_MQM_QOS_ACTION_FRAME
 *
 * 01 11 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add Deauth and Disassoc Handler
 *
 * Dec 9 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add hemRunEventScanDone() to arMsgMapTable[]
 *
 * Dec 4 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix mboxDummy() didn't free prMsgHdr
 *
 * Dec 3 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add saaAisJoinComplete event handler
 *
 * Dec 2 2009 MTK02468
 * [BORA00000337] To check in codes for FPGA emulation
 * Fixed the handler function name in arMsgMapTable for MID_RXM_MQM_BA_ACTION_FRAME
 *
 * Dec 2 2009 MTK02468
 * [BORA00000337] To check in codes for FPGA emulation
 * Added MID_RXM_MQM_BA_ACTION_FRAME to MsgMapTable
 *
 * Nov 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Revise MSG Handler (remove dummy and add for SAA)
 *
 * Nov 16 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add aisFsmRunEventAbort() event handler
 *
 * Nov 11 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix typo
 *
 * Nov 10 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add more MSG_HNDL_ENTRY_T to avoid ASSERT() in mboxInitMsgMap()
 *
 * Nov 5 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add SCN message and function entry to arMsgMapTable[]
 *
 * Nov 2 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix sorting algorithm in mboxInitMsgMap()
 *
 * Oct 28 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
**
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

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/
#if DBG
/*lint -save -e64 Type mismatch */
static PUINT_8 apucDebugMsg[] = {
    (PUINT_8)DISP_STRING("MID_MNY_CNM_CH_REQ"),
    (PUINT_8)DISP_STRING("MID_MNY_CNM_CH_ABORT"),
    (PUINT_8)DISP_STRING("MID_CNM_AIS_CH_GRANT"),
    (PUINT_8)DISP_STRING("MID_CNM_P2P_CH_GRANT"),
    (PUINT_8)DISP_STRING("MID_CNM_BOW_CH_GRANT"),

    (PUINT_8)DISP_STRING("MID_AIS_SCN_SCAN_REQ"),
    (PUINT_8)DISP_STRING("MID_AIS_SCN_SCAN_REQ_V2"),
    (PUINT_8)DISP_STRING("MID_AIS_SCN_SCAN_CANCEL"),
    (PUINT_8)DISP_STRING("MID_P2P_SCN_SCAN_REQ"),
    (PUINT_8)DISP_STRING("MID_P2P_SCN_SCAN_REQ_V2"),
    (PUINT_8)DISP_STRING("MID_P2P_SCN_SCAN_CANCEL"),
    (PUINT_8)DISP_STRING("MID_BOW_SCN_SCAN_REQ"),
    (PUINT_8)DISP_STRING("MID_BOW_SCN_SCAN_REQ_V2"),
    (PUINT_8)DISP_STRING("MID_BOW_SCN_SCAN_CANCEL"),
    (PUINT_8)DISP_STRING("MID_RLM_SCN_SCAN_REQ"),
    (PUINT_8)DISP_STRING("MID_RLM_SCN_SCAN_REQ_V2"),
    (PUINT_8)DISP_STRING("MID_RLM_SCN_SCAN_CANCEL"),
    (PUINT_8)DISP_STRING("MID_SCN_AIS_SCAN_DONE"),
    (PUINT_8)DISP_STRING("MID_SCN_P2P_SCAN_DONE"),
    (PUINT_8)DISP_STRING("MID_SCN_BOW_SCAN_DONE"),
    (PUINT_8)DISP_STRING("MID_SCN_RLM_SCAN_DONE"),

    (PUINT_8)DISP_STRING("MID_OID_AIS_FSM_JOIN_REQ"),
    (PUINT_8)DISP_STRING("MID_OID_AIS_FSM_ABORT"),
    (PUINT_8)DISP_STRING("MID_AIS_SAA_FSM_START"),
    (PUINT_8)DISP_STRING("MID_AIS_SAA_FSM_ABORT"),
    (PUINT_8)DISP_STRING("MID_SAA_AIS_JOIN_COMPLETE"),

#if CFG_ENABLE_BT_OVER_WIFI
    (PUINT_8)DISP_STRING("MID_BOW_SAA_FSM_START"),
    (PUINT_8)DISP_STRING("MID_BOW_SAA_FSM_ABORT"),
    (PUINT_8)DISP_STRING("MID_SAA_BOW_JOIN_COMPLETE"),
#endif

#if CFG_ENABLE_WIFI_DIRECT
    (PUINT_8)DISP_STRING("MID_P2P_SAA_FSM_START"),
    (PUINT_8)DISP_STRING("MID_P2P_SAA_FSM_ABORT"),
    (PUINT_8)DISP_STRING("MID_SAA_P2P_JOIN_COMPLETE"),

    (PUINT_8)DISP_STRING("MID_MNY_P2P_FUN_SWITCH"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_DEVICE_DISCOVERY"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_CONNECTION_REQ"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_CONNECTION_ABORT"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_BEACON_UPDATE"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_STOP_AP"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_CHNL_REQ"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_CHNL_ABORT"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_MGMT_TX"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_GROUP_DISSOLVE"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_MGMT_FRAME_REGISTER"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_NET_DEV_REGISTER"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_START_AP"),
    (PUINT_8)DISP_STRING("MID_MNY_P2P_UPDATE_IE_BUF"),
 #endif

#if CFG_SUPPORT_ADHOC
    //(PUINT_8)DISP_STRING("MID_AIS_CNM_CREATE_IBSS_REQ"),
    //(PUINT_8)DISP_STRING("MID_CNM_AIS_CREATE_IBSS_GRANT"),
    //(PUINT_8)DISP_STRING("MID_AIS_CNM_MERGE_IBSS_REQ"),
    //(PUINT_8)DISP_STRING("MID_CNM_AIS_MERGE_IBSS_GRANT"),
    (PUINT_8)DISP_STRING("MID_SCN_AIS_FOUND_IBSS"),
#endif /* CFG_SUPPORT_ADHOC */

    (PUINT_8)DISP_STRING("MID_SAA_AIS_FSM_ABORT")
};
/*lint -restore */
#endif /* DBG */

/* This message entry will be re-ordered based on the message ID order
 * by invoking mboxInitMsgMap()
 */
static MSG_HNDL_ENTRY_T arMsgMapTable[] = {
    { MID_MNY_CNM_CH_REQ,           cnmChMngrRequestPrivilege               },
    { MID_MNY_CNM_CH_ABORT,         cnmChMngrAbortPrivilege                 },
    { MID_CNM_AIS_CH_GRANT,         aisFsmRunEventChGrant                   },
#if CFG_ENABLE_WIFI_DIRECT
    { MID_CNM_P2P_CH_GRANT,         p2pFsmRunEventChGrant                   }, /*set in gl_p2p_init.c*/
#else
    { MID_CNM_P2P_CH_GRANT,         mboxDummy                               },
#endif

#if CFG_ENABLE_BT_OVER_WIFI
    { MID_CNM_BOW_CH_GRANT,         bowRunEventChGrant                             },
#else
    { MID_CNM_BOW_CH_GRANT,         mboxDummy                               },
#endif

    /*--------------------------------------------------*/
    /* SCN Module Mailbox Messages                      */
    /*--------------------------------------------------*/
    { MID_AIS_SCN_SCAN_REQ,         scnFsmMsgStart                          },
    { MID_AIS_SCN_SCAN_REQ_V2,      scnFsmMsgStart                          },
    { MID_AIS_SCN_SCAN_CANCEL,      scnFsmMsgAbort                          },
    { MID_P2P_SCN_SCAN_REQ,         scnFsmMsgStart                          },
    { MID_P2P_SCN_SCAN_REQ_V2,      scnFsmMsgStart                          },
    { MID_P2P_SCN_SCAN_CANCEL,      scnFsmMsgAbort                          },
    { MID_BOW_SCN_SCAN_REQ,         scnFsmMsgStart                          },
    { MID_BOW_SCN_SCAN_REQ_V2,      scnFsmMsgStart                          },
    { MID_BOW_SCN_SCAN_CANCEL,      scnFsmMsgAbort                          },
    { MID_RLM_SCN_SCAN_REQ,         scnFsmMsgStart                          },
    { MID_RLM_SCN_SCAN_REQ_V2,      scnFsmMsgStart                          },
    { MID_RLM_SCN_SCAN_CANCEL,      scnFsmMsgAbort                          },
    { MID_SCN_AIS_SCAN_DONE,        aisFsmRunEventScanDone                  },
#if CFG_ENABLE_WIFI_DIRECT
    { MID_SCN_P2P_SCAN_DONE,        p2pFsmRunEventScanDone                  }, /*set in gl_p2p_init.c*/
#else
    { MID_SCN_P2P_SCAN_DONE,        mboxDummy                               },
#endif

#if CFG_ENABLE_BT_OVER_WIFI
    { MID_SCN_BOW_SCAN_DONE,        bowResponderScanDone                               },
#else
    { MID_SCN_BOW_SCAN_DONE,        mboxDummy                               },
#endif
    { MID_SCN_RLM_SCAN_DONE,        rlmObssScanDone                         },

    /*--------------------------------------------------*/
    /* AIS Module Mailbox Messages                      */
    /*--------------------------------------------------*/
    { MID_OID_AIS_FSM_JOIN_REQ,     aisFsmRunEventAbort                     },
    { MID_OID_AIS_FSM_ABORT,        aisFsmRunEventAbort                     },
    { MID_AIS_SAA_FSM_START,        saaFsmRunEventStart                     },
    { MID_AIS_SAA_FSM_ABORT,        saaFsmRunEventAbort                     },
    { MID_SAA_AIS_JOIN_COMPLETE,    aisFsmRunEventJoinComplete              },

#if CFG_ENABLE_BT_OVER_WIFI
    /*--------------------------------------------------*/
    /* BOW Module Mailbox Messages                      */
    /*--------------------------------------------------*/
    { MID_BOW_SAA_FSM_START,        saaFsmRunEventStart                     },
    { MID_BOW_SAA_FSM_ABORT,        saaFsmRunEventAbort                     },
    { MID_SAA_BOW_JOIN_COMPLETE,    bowFsmRunEventJoinComplete              },
#endif

#if CFG_ENABLE_WIFI_DIRECT  /*set in gl_p2p_init.c*/
    { MID_P2P_SAA_FSM_START,        saaFsmRunEventStart                     },
    { MID_P2P_SAA_FSM_ABORT,        saaFsmRunEventAbort                     },
    { MID_SAA_P2P_JOIN_COMPLETE,    p2pFsmRunEventJoinComplete              },// TODO: p2pFsmRunEventJoinComplete

    { MID_MNY_P2P_FUN_SWITCH,       p2pFsmRunEventSwitchOPMode              },
    { MID_MNY_P2P_DEVICE_DISCOVERY, p2pFsmRunEventScanRequest               },
    { MID_MNY_P2P_CONNECTION_REQ,   p2pFsmRunEventConnectionRequest         },
    { MID_MNY_P2P_CONNECTION_ABORT, p2pFsmRunEventConnectionAbort           },
    { MID_MNY_P2P_BEACON_UPDATE,    p2pFsmRunEventBeaconUpdate              },
    { MID_MNY_P2P_STOP_AP,          p2pFsmRunEventStopAP                    },
    { MID_MNY_P2P_CHNL_REQ,         p2pFsmRunEventChannelRequest            },
    { MID_MNY_P2P_CHNL_ABORT,       p2pFsmRunEventChannelAbort              },
    { MID_MNY_P2P_MGMT_TX,          p2pFsmRunEventMgmtFrameTx               },
    { MID_MNY_P2P_GROUP_DISSOLVE,   p2pFsmRunEventDissolve                  },
    { MID_MNY_P2P_MGMT_FRAME_REGISTER, p2pFsmRunEventMgmtFrameRegister      },
    { MID_MNY_P2P_NET_DEV_REGISTER, p2pFsmRunEventNetDeviceRegister         },
    { MID_MNY_P2P_START_AP,         p2pFsmRunEventStartAP                   },
    { MID_MNY_P2P_MGMT_FRAME_UPDATE,    p2pFsmRunEventUpdateMgmtFrame       },
#if CFG_SUPPORT_WFD
    { MID_MNY_P2P_WFD_CFG_UPDATE, p2pFsmRunEventWfdSettingUpdate           },
#endif

#endif

#if CFG_SUPPORT_ADHOC
    { MID_SCN_AIS_FOUND_IBSS,       aisFsmRunEventFoundIBSSPeer             },
#endif /* CFG_SUPPORT_ADHOC */

    { MID_SAA_AIS_FSM_ABORT,        aisFsmRunEventAbort                     }
};

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#if DBG
#define MBOX_HNDL_MSG(prAdapter, prMsg) do { \
        ASSERT(arMsgMapTable[prMsg->eMsgId].pfMsgHndl); \
        if (arMsgMapTable[prMsg->eMsgId].pfMsgHndl) { \
            DBGLOG(CNM, LOUD, ("DO MSG [%d: %s]\n", prMsg->eMsgId, apucDebugMsg[prMsg->eMsgId])); \
            arMsgMapTable[prMsg->eMsgId].pfMsgHndl(prAdapter, prMsg); \
        } \
        else { \
            DBGLOG(CNM, ERROR, ("NULL fptr for MSG [%d]\n", prMsg->eMsgId)); \
            cnmMemFree(prAdapter, prMsg); \
        } \
} while (0)
#else
#define MBOX_HNDL_MSG(prAdapter, prMsg) do { \
        ASSERT(arMsgMapTable[prMsg->eMsgId].pfMsgHndl); \
        if (arMsgMapTable[prMsg->eMsgId].pfMsgHndl) { \
            DBGLOG(CNM, LOUD, ("DO MSG [%d]\n", prMsg->eMsgId)); \
            arMsgMapTable[prMsg->eMsgId].pfMsgHndl(prAdapter, prMsg); \
        } \
        else { \
            DBGLOG(CNM, ERROR, ("NULL fptr for MSG [%d]\n", prMsg->eMsgId)); \
            cnmMemFree(prAdapter, prMsg); \
        } \
} while (0)
#endif
/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/


/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
mboxInitMsgMap (
    VOID
    )
{
    UINT_32             i, idx;
    MSG_HNDL_ENTRY_T    rTempEntry;

    ASSERT((sizeof(arMsgMapTable) / sizeof(MSG_HNDL_ENTRY_T)) == MID_TOTAL_NUM);

    for (i = 0; i < MID_TOTAL_NUM; i++) {
        if (arMsgMapTable[i].eMsgId == (ENUM_MSG_ID_T) i) {
            continue;
        }
        for (idx = i + 1; idx < MID_TOTAL_NUM; idx++) {
            if (arMsgMapTable[idx].eMsgId == (ENUM_MSG_ID_T) i) {
                break;
            }
        }
        ASSERT(idx < MID_TOTAL_NUM);
        if (idx >= MID_TOTAL_NUM) {
            continue;
        }

        /* Swap target entry and current entry */
        rTempEntry.eMsgId = arMsgMapTable[idx].eMsgId;
        rTempEntry.pfMsgHndl= arMsgMapTable[idx].pfMsgHndl;

        arMsgMapTable[idx].eMsgId = arMsgMapTable[i].eMsgId;
        arMsgMapTable[idx].pfMsgHndl = arMsgMapTable[i].pfMsgHndl;

        arMsgMapTable[i].eMsgId = rTempEntry.eMsgId;
        arMsgMapTable[i].pfMsgHndl = rTempEntry.pfMsgHndl;
    }

    /* Verify the correctness of final message map */
    for (i = 0; i < MID_TOTAL_NUM; i++) {
        ASSERT(arMsgMapTable[i].eMsgId == (ENUM_MSG_ID_T) i);
        while (arMsgMapTable[i].eMsgId != (ENUM_MSG_ID_T) i);
    }

}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
mboxSetup (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_MBOX_ID_T eMboxId
    )
{
    P_MBOX_T prMbox;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(eMboxId < MBOX_ID_TOTAL_NUM);
    ASSERT(prAdapter);

    prMbox = &(prAdapter->arMbox[eMboxId]);

    KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_MAILBOX);
    LINK_INITIALIZE(&prMbox->rLinkHead);
    KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_MAILBOX);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
mboxSendMsg (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_MBOX_ID_T eMboxId,
    IN P_MSG_HDR_T prMsg,
    IN EUNM_MSG_SEND_METHOD_T eMethod
    )
{
    P_MBOX_T    prMbox;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(eMboxId < MBOX_ID_TOTAL_NUM);
    ASSERT(prMsg);
    ASSERT(prAdapter);

    prMbox = &(prAdapter->arMbox[eMboxId]);

    switch (eMethod) {
    case MSG_SEND_METHOD_BUF:
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_MAILBOX);
        LINK_INSERT_TAIL(&prMbox->rLinkHead, &prMsg->rLinkEntry);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_MAILBOX);

        // to wake up main service thread
        GLUE_SET_EVENT(prAdapter->prGlueInfo);

        break;

    case MSG_SEND_METHOD_UNBUF:
        MBOX_HNDL_MSG(prAdapter, prMsg);
        break;

    default:
        ASSERT(0);
        break;
    }
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
mboxRcvAllMsg (
    IN P_ADAPTER_T prAdapter,
    ENUM_MBOX_ID_T eMboxId
    )
{
    P_MBOX_T        prMbox;
    P_MSG_HDR_T     prMsg;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(eMboxId < MBOX_ID_TOTAL_NUM);
    ASSERT(prAdapter);

    prMbox = &(prAdapter->arMbox[eMboxId]);

    while (!LINK_IS_EMPTY(&prMbox->rLinkHead) ) {
        KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_MAILBOX);
        LINK_REMOVE_HEAD(&prMbox->rLinkHead, prMsg, P_MSG_HDR_T);
        KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_MAILBOX);

        ASSERT(prMsg);
        MBOX_HNDL_MSG(prAdapter, prMsg);
    }

}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
mboxInitialize (
    IN P_ADAPTER_T prAdapter
    )
{
    UINT_32     i;

    ASSERT(prAdapter);

    /* Initialize Mailbox */
    mboxInitMsgMap();

    /* Setup/initialize each mailbox */
    for (i = 0; i < MBOX_ID_TOTAL_NUM; i++) {
        mboxSetup(prAdapter, i);
    }

}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
mboxDestroy (
    IN P_ADAPTER_T prAdapter
    )
{
    P_MBOX_T        prMbox;
    P_MSG_HDR_T     prMsg;
    UINT_8          i;
    KAL_SPIN_LOCK_DECLARATION();

    ASSERT(prAdapter);

    for (i = 0; i < MBOX_ID_TOTAL_NUM; i++) {
        prMbox = &(prAdapter->arMbox[i]);

        while (!LINK_IS_EMPTY(&prMbox->rLinkHead) ) {
            KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_MAILBOX);
            LINK_REMOVE_HEAD(&prMbox->rLinkHead, prMsg, P_MSG_HDR_T);
            KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_MAILBOX);

            ASSERT(prMsg);
            cnmMemFree(prAdapter, prMsg);
        }
    }
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This is dummy function to prevent empty arMsgMapTable[] for compiling.
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
mboxDummy (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    )
{
    ASSERT(prAdapter);

    cnmMemFree(prAdapter, prMsgHdr);

    return;
}

