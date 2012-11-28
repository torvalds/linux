/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/precomp.h#2 $
*/

/*! \file   precomp.h
    \brief  Collection of most compiler flags are described here.

    In this file we collect all compiler flags and detail the driver behavior if
    enable/disable such switch or adjust numeric parameters.
*/



/*
** $Log: precomp.h $
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 01 05 2012 tsaiyuan.hsu
 * [WCXRP00001157] [MT6620 Wi-Fi][FW][DRV] add timing measurement support for 802.11v
 * add timing measurement support for 802.11v.
 *
 * 08 15 2011 cp.wu
 * [WCXRP00000851] [MT6628 Wi-Fi][Driver] Add HIFSYS related definition to driver source tree
 * add MT6628-specific definitions.
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 03 15 2011 eddie.chen
 * [WCXRP00000554] [MT6620 Wi-Fi][DRV] Add sw control debug counter
 * Add sw debug counter for QM.
 *
 * 01 27 2011 tsaiyuan.hsu
 * [WCXRP00000392] [MT6620 Wi-Fi][Driver] Add Roaming Support
 * add roaming fsm
 * 1. not support 11r, only use strength of signal to determine roaming.
 * 2. not enable CFG_SUPPORT_ROAMING until completion of full test.
 * 3. in 6620, adopt work-around to avoid sign extension problem of cck of hw
 * 4. assume that change of link quality in smooth way.
 *
 * 12 13 2010 cp.wu
 * [WCXRP00000260] [MT6620 Wi-Fi][Driver][Firmware] Create V1.1 branch for both firmware and driver
 * create branch for Wi-Fi driver v1.1
 *
 * 10 07 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * add firmware download for MT5931.
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * 1) add NVRAM access API
 * 2) fake scanning result when NVRAM doesn't exist and/or version mismatch. (off by compiler option)
 * 3) add OID implementation for NVRAM read/write service
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * Isolate P2P related function for Hardware Software Bundle
 *
 * 09 14 2010 chinghwa.yu
 * NULL
 * Fix BOW_FSM_INFO_T dependence.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 07 20 2010 wh.su
 *
 * adding the wapi code.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Check draft RLM code for HT cap
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Modify CNM message handler for new flow
 *
 * 06 28 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * 1st draft code for RLM module
 *
 * 06 19 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * consdier the concurrent network setting.
 *
 * 06 18 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * migration the security related function from firmware.
 *
 * 06 18 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * migration from MT6620 firmware.
 *
 * 06 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add hem_mbox.c and cnm_mem.h (but disabled some feature) for further migration
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge cnm_scan.h and hem_mbox.h
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge wifi_var.h, precomp.h, cnm_timer.h (data type only)
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * 1) add timeout handler mechanism for pending command packets
 * 2) add p2p add/removal key
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  *  *  *  *  *  *  *  * 2) command sequence number is now increased atomically
 *  *  *  *  *  *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 03 16 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * build up basic data structure and definitions to support BT-over-WiFi
 *
 * 02 08 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * prepare for implementing fw download logic
 *
 * 01 27 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * .
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-12-08 11:30:58 GMT mtk02752
**  add rftest.h for implementing RF test mode in driver land
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-11-23 22:02:00 GMT mtk02468
**  Added que_mgt.h
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-10-13 21:58:36 GMT mtk01084
**  update for new macro define
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-21 09:40:11 GMT mtk01461
**  Add nic_cmd_event.h
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-04-17 20:00:26 GMT mtk01461
**  Add cmd_buf.h
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-19 18:32:44 GMT mtk01084
**  update for basic power management functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-16 09:08:25 GMT mtk01461
**  Update TX PATH API
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:11:38 GMT mtk01426
**  Init for develop
**
*/

#ifndef _PRECOMP_H
#define _PRECOMP_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "gl_os.h" // Include "config.h"

#if CFG_ENABLE_WIFI_DIRECT
#include "gl_p2p_os.h"
#endif

#include "debug.h"

#include "link.h"
#include "queue.h"


/*------------------------------------------------------------------------------
 * .\include\mgmt
 *------------------------------------------------------------------------------
 */
#include "wlan_typedef.h"

#include "mac.h"

/* Dependency:  mac.h (MAC_ADDR_LEN) */
#include "wlan_def.h"

#if CFG_SUPPORT_SWCR
#include "swcr.h"
#endif

/*------------------------------------------------------------------------------
 * .\include\nic
 *------------------------------------------------------------------------------
 */
/* Dependency:  wlan_def.h (ENUM_NETWORK_TYPE_T) */
#include "cmd_buf.h"

/* Dependency:  mac.h (MAC_ADDR_LEN) */
#include "nic_cmd_event.h"

/* Dependency:  nic_cmd_event.h (P_EVENT_CONNECTION_STATUS) */
#include "nic.h"

#include "nic_init_cmd_event.h"

#include "hif_rx.h"
#include "hif_tx.h"

#include "nic_tx.h"

/* Dependency:  hif_rx.h (P_HIF_RX_HEADER_T) */
#include "nic_rx.h"

#include "que_mgt.h"

#if CFG_ENABLE_WIFI_DIRECT
#include "p2p_typedef.h"
#include "p2p_cmd_buf.h"
#include "p2p_nic_cmd_event.h"
#include "p2p_mac.h"
#include "p2p_nic.h"
#endif


/*------------------------------------------------------------------------------
 * .\include\mgmt
 *------------------------------------------------------------------------------
 */

#include "hem_mbox.h"

#include "scan.h"
#include "bss.h"

#include "wlan_lib.h"
#include "wlan_oid.h"
#include "wlan_bow.h"

#if CFG_ENABLE_WIFI_DIRECT
#include "wlan_p2p.h"
#endif

#include "hal.h"

#if defined(MT6620)
    #include "mt6620_reg.h"
#elif defined(MT5931)
    #include "mt5931_reg.h"
#elif defined(MT6628)
    #include "mt6628_reg.h"
#endif

#include "rlm.h"
#include "rlm_domain.h"
#include "rlm_protection.h"
#include "rlm_obss.h"
#include "rate.h"
#if CFG_SUPPORT_802_11V
#include "wnm.h"
#endif

#include "aa_fsm.h"

#include "cnm_timer.h"


#if CFG_ENABLE_BT_OVER_WIFI
#include "bow.h"
#include "bow_fsm.h"
#endif

#include "pwr_mgt.h"


#include "cnm.h"
/* Dependency:  aa_fsm.h (ENUM_AA_STATE_T), p2p_fsm.h (WPS_ATTRI_MAX_LEN_DEVICE_NAME) */
#include "cnm_mem.h"
#include "cnm_scan.h"


#if CFG_ENABLE_WIFI_DIRECT
#include "p2p_rlm_obss.h"
#include "p2p_bss.h"
#include "p2p.h"
#include "p2p_fsm.h"
#include "p2p_scan.h"
#include "p2p_state.h"
#include "p2p_func.h"
#include "p2p_rlm.h"
#include "p2p_assoc.h"
#include "p2p_ie.h"
#endif

#include "privacy.h"

#include "mib.h"

#include "auth.h"
#include "assoc.h"

#if CFG_SUPPORT_ROAMING
#include "roaming_fsm.h"
#endif /* CFG_SUPPORT_ROAMING */

#include "ais_fsm.h"


#include "adapter.h"


#include "que_mgt.h"
#include "rftest.h"


#if CFG_RSN_MIGRATION
#include "rsn.h"
#include "sec_fsm.h"
#endif

#if CFG_SUPPORT_WAPI
#include "wapi.h"
#endif

/*------------------------------------------------------------------------------
 * NVRAM structure
 *------------------------------------------------------------------------------
 */
#include "CFG_Wifi_File.h"

#if CFG_ENABLE_WIFI_DIRECT
#include "gl_p2p_kal.h"
#endif

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

#endif /* _PRECOMP_H */


