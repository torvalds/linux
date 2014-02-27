/*
** $Id: @(#) gl_p2p.c@@
*/

/*! \file   gl_p2p.c
    \brief  Main routines of Linux driver interface for Wi-Fi Direct

    This file contains the main routines of Linux driver for MediaTek Inc. 802.11
    Wireless LAN Adapters.
*/



/*
** $Log: gl_p2p.c $
** 
** 09 12 2012 wcpadmin
** [ALPS00276400] Remove MTK copyright and legal header on GPL/LGPL related packages
** .
** 
** 08 17 2012 yuche.tsai
** NULL
** Fix compile warning.
** 
** 08 16 2012 yuche.tsai
** NULL
** Fix compile warning.
** 
** 08 14 2012 yuche.tsai
** NULL
** FPB from ALPS.JB to phase 2 release.
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
 * Fix compile error for JB.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Let netdev bring up.
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 01 09 2012 terry.wu
 * [WCXRP00001166] [Wi-Fi] [Driver] cfg80211 integration for p2p newtork
 * cfg80211 integration for p2p network.
 *
 * 12 19 2011 terry.wu
 * [WCXRP00001142] [Wi-Fi] [P2P Driver] XOR local admin bit to generate p2p net device MAC
 * XOR local administrated bit to generate net device MAC of p2p network.
 *
 * 12 02 2011 yuche.tsai
 * NULL
 * Fix possible KE when unload p2p.
 *
 * 11 24 2011 yuche.tsai
 * NULL
 * Fix P2P IOCTL of multicast address bug, add low power driver stop control.
 *
 * 11 22 2011 yuche.tsai
 * NULL
 * Update RSSI link quality of P2P Network query method. (Bug fix)
 *
 * 11 19 2011 yuche.tsai
 * NULL
 * Add RSSI support for P2P network.
 *
 * 11 16 2011 yuche.tsai
 * [WCXRP00001107] [Volunteer Patch][Driver] Large Network Type index assert in FW issue.
 * Avoid using work thread in set p2p multicast address callback.
 *
 * 11 11 2011 yuche.tsai
 * NULL
 * Fix work thread cancel issue.
 *
 * 11 11 2011 yuche.tsai
 * NULL
 * Fix default device name issue.
 *
 * 11 08 2011 yuche.tsai
 * [WCXRP00001094] [Volunteer Patch][Driver] Driver version & supplicant version query & set support for service discovery version check.
 * Add support for driver version query & p2p supplicant verseion set.
 * For new service discovery mechanism sync.
 *
 * 11 07 2011 yuche.tsai
 * NULL
 * [ALPS 00087243] KE in worker thread.
 * The multicast address list is scheduled in worker thread.
 * Before the worker thread is excuted, if P2P is unloaded, a KE may occur.
 *
 * 10 26 2011 terry.wu
 * [WCXRP00001066] [MT6620 Wi-Fi] [P2P Driver] Fix P2P Oid Issue
 * Fix some P2P OID functions didn't raise its flag "fgIsP2pOid" issue.
 *
 * 10 25 2011 cm.chang
 * [WCXRP00001058] [All Wi-Fi][Driver] Fix sta_rec's phyTypeSet and OBSS scan in AP mode
 * .
 *
 * 10 18 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Support Channle Query.
 *
 * 10 18 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * New 2.1 branch

 *
 * 08 26 2011 yuche.tsai
 * NULL
 * Fix bug of parsing secondary device list type issue.
 *
 * 08 24 2011 yuche.tsai
 * [WCXRP00000919] [Volunteer Patch][WiFi Direct][Driver] Invitation New Feature.
 * Invitation Abort.
 *
 * 08 23 2011 yuche.tsai
 * NULL
 * Fix multicast address list issue of P2P.
 *
 * 08 22 2011 chinglan.wang
 * NULL
 * Fix invitation indication bug..
 *
 * 08 16 2011 cp.wu
 * [WCXRP00000934] [MT6620 Wi-Fi][Driver][P2P] Wi-Fi hot spot with auto sparse channel residence
 * auto channel decision for 2.4GHz hot spot mode
 *
 * 08 16 2011 chinglan.wang
 * NULL
 * Add the group id information in the invitation indication.
 *
 * 08 09 2011 yuche.tsai
 * [WCXRP00000919] [Volunteer Patch][WiFi Direct][Driver] Invitation New Feature.
 * Invitation Feature add on.
 *
 * 08 05 2011 yuche.tsai
 * [WCXRP00000856] [Volunteer Patch][WiFi Direct][Driver] MT6620 WiFi Direct IOT Issue with BCM solution.
 * Add Password ID check for quick connection.
 * Also modify some connection policy.
 *
 * 07 18 2011 chinglan.wang
 * NULL
 * Add IOC_P2P_GO_WSC_IE (p2p capability).
 *
 * 06 14 2011 yuche.tsai
 * NULL
 * Add compile flag to disable persistent group support.
 *
 * 05 04 2011 chinglan.wang
 * [WCXRP00000698] [MT6620 Wi-Fi][P2P][Driver] Add p2p invitation command for the p2p driver
 * .
 *
 * 05 02 2011 yuche.tsai
 * [WCXRP00000693] [Volunteer Patch][MT6620][Driver] Clear Formation Flag after TX lifetime timeout.
 * Clear formation flag after formation timeout.
 *
 * 04 22 2011 george.huang
 * [WCXRP00000621] [MT6620 Wi-Fi][Driver] Support P2P supplicant to set power mode
 * .
 *
 * 04 21 2011 george.huang
 * [WCXRP00000621] [MT6620 Wi-Fi][Driver] Support P2P supplicant to set power mode
 * 1. Revise P2P power mode setting.
 * 2. Revise fast-PS for concurrent
 *
 * 04 19 2011 wh.su
 * NULL
 * Adding length check before doing WPA RSN IE parsing for scan results indicate.
 *
 * 04 14 2011 yuche.tsai
 * [WCXRP00000646] [Volunteer Patch][MT6620][FW/Driver] Sigma Test Modification for some test case.
 * Connection flow refine for Sigma test.
 *
 * 04 08 2011 yuche.tsai
 * [WCXRP00000624] [Volunteer Patch][MT6620][Driver] Add device discoverability support for GO.
 * Add device discoverability support.
 *
 * 04 08 2011 george.huang
 * [WCXRP00000621] [MT6620 Wi-Fi][Driver] Support P2P supplicant to set power mode
 * separate settings of P2P and AIS
 *
 * 04 07 2011 terry.wu
 * [WCXRP00000619] [MT6620 Wi-Fi][Driver] fix kernel panic may occur when removing wlan
 * Fix kernel panic may occur when removing wlan driver.
 *
 * 03 31 2011 wh.su
 * [WCXRP00000614] [MT6620 Wi-Fi][Driver] P2P: Update beacon content while setting WSC IE
 * Update the wsc ie to beacon content.
 *
 * 03 25 2011 wh.su
 * NULL
 * add the sample code for set power mode and get power mode.
 *
 * 03 25 2011 yuche.tsai
 * NULL
 * Improve some error handleing.
 *
 * 03 22 2011 george.huang
 * [WCXRP00000504] [MT6620 Wi-Fi][FW] Support Sigma CAPI for power saving related command
 * link with supplicant commands
 *
 * 03 22 2011 yuche.tsai
 * [WCXRP00000584] [Volunteer Patch][MT6620][Driver] Add beacon timeout support for WiFi Direct.
 * Modify formation policy.
 *
 * 03 22 2011 yuche.tsai
 * NULL
 * Modify formation policy setting.
 *
 * 03 18 2011 yuche.tsai
 * [WCXRP00000574] [Volunteer Patch][MT6620][Driver] Modify P2P FSM Connection Flow
 * Modify connection flow after Group Formation Complete, or device connect to a GO.
 * Instead of request channel & connect directly, we use scan to allocate channel bandwidth & connect after RX BCN.
 *
 * 03 15 2011 wh.su
 * [WCXRP00000563] [MT6620 Wi-Fi] [P2P] Set local config method while set password Id ready
 * set lccal config method method while set password Id ready.
 *
 * 03 15 2011 yuche.tsai
 * [WCXRP00000560] [Volunteer Patch][MT6620][Driver] P2P Connection from UI using KEY/DISPLAY issue
 * Fix some configure method issue.
 *
 * 03 15 2011 jeffrey.chang
 * [WCXRP00000558] [MT6620 Wi-Fi][MT6620 Wi-Fi][Driver] refine the queue selection algorithm for WMM
 * refine queue_select function
 *
 * 03 13 2011 wh.su
 * [WCXRP00000530] [MT6620 Wi-Fi] [Driver] skip doing p2pRunEventAAAComplete after send assoc response Tx Done
 * add code for avoid compiling warning.
 *
 * 03 10 2011 yuche.tsai
 * NULL
 * Add P2P API.
 *
 * 03 10 2011 terry.wu
 * [WCXRP00000505] [MT6620 Wi-Fi][Driver/FW] WiFi Direct Integration
 * Remove unnecessary assert and message.
 *
 * 03 08 2011 wh.su
 * [WCXRP00000488] [MT6620 Wi-Fi][Driver] Support the SIGMA set p2p parameter to driver
 * support the power save related p2p setting.
 *
 * 03 07 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * rename the define to anti_pviracy.
 *
 * 03 05 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * add the code to get the check rsponse and indicate to app.
 *
 * 03 03 2011 jeffrey.chang
 * [WCXRP00000512] [MT6620 Wi-Fi][Driver] modify the net device relative functions to support the H/W multiple queue
 * support concurrent network
 *
 * 03 03 2011 jeffrey.chang
 * [WCXRP00000512] [MT6620 Wi-Fi][Driver] modify the net device relative functions to support the H/W multiple queue
 * modify P2P's netdevice  functions to support multiple H/W queues
 *
 * 03 03 2011 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface for supporting Wi-Fi Direct Service Discovery
 * for get request, the buffer length to be copied is header + payload.
 *
 * 03 02 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * add code to let the beacon and probe response for Auto GO WSC .
 *
 * 03 02 2011 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface for supporting Wi-Fi Direct Service Discovery
 * add a missed break.
 *
 * 03 01 2011 yuche.tsai
 * [WCXRP00000501] [Volunteer Patch][MT6620][Driver] No common channel issue when doing GO formation
 * Update channel issue when doing GO formation..
 *
 * 02 25 2011 wh.su
 * [WCXRP00000488] [MT6620 Wi-Fi][Driver] Support the SIGMA set p2p parameter to driver
 * add the Operation channel setting.
 *
 * 02 23 2011 wh.su
 * [WCXRP00000488] [MT6620 Wi-Fi][Driver] Support the SIGMA set p2p parameter to driver
 * fixed the set int ioctl set index and value map to driver issue.
 *
 * 02 22 2011 wh.su
 * [WCXRP00000488] [MT6620 Wi-Fi][Driver] Support the SIGMA set p2p parameter to driver
 * adding the ioctl set int from supplicant, and can used to set the p2p paramters
 *
 * 02 21 2011 terry.wu
 * [WCXRP00000476] [MT6620 Wi-Fi][Driver] Clean P2P scan list while removing P2P
 * Clean P2P scan list while removing P2P.
 *
 * 02 18 2011 wh.su
 * [WCXRP00000471] [MT6620 Wi-Fi][Driver] Add P2P Provison discovery append Config Method attribute at WSC IE
 * fixed the ioctl setting that index not map to spec defined config method.
 *
 * 02 17 2011 wh.su
 * [WCXRP00000471] [MT6620 Wi-Fi][Driver] Add P2P Provison discovery append Config Method attribute at WSC IE
 * append the WSC IE config method attribute at provision discovery request.
 *
 * 02 17 2011 wh.su
 * [WCXRP00000448] [MT6620 Wi-Fi][Driver] Fixed WSC IE not send out at probe request
 * modify the structure pointer for set WSC IE.
 *
 * 02 16 2011 wh.su
 * [WCXRP00000448] [MT6620 Wi-Fi][Driver] Fixed WSC IE not send out at probe request
 * fixed the probe request send out without WSC IE issue (at P2P).
 *
 * 02 09 2011 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface for supporting Wi-Fi Direct Service Discovery
 * fix typo
 *
 * 02 09 2011 yuche.tsai
 * [WCXRP00000431] [Volunteer Patch][MT6620][Driver] Add MLME support for deauthentication under AP(Hot-Spot) mode.
 * Add Support for MLME deauthentication for Hot-Spot.
 *
 * 01 25 2011 terry.wu
 * [WCXRP00000393] [MT6620 Wi-Fi][Driver] Add new module insert parameter
 * Add a new module parameter to indicate current runnig mode, P2P or AP.
 *
 * 01 12 2011 yuche.tsai
 * [WCXRP00000352] [Volunteer Patch][MT6620][Driver] P2P Statsion Record Client List Issue
 * 1. Modify Channel Acquire Time of AP mode from 5s to 1s.
 * 2. Call cnmP2pIsPermit() before active P2P network.
 * 3. Add channel selection support for AP mode.
 *
 * 01 05 2011 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface for supporting Wi-Fi Direct Service Discovery
 * ioctl implementations for P2P Service Discovery
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc and vmalloc implementations to ease physically continous memory demands
 * separate kalMemAlloc() into virtually-continous and physically-continous type to ease slab system pressure
 *
 * 12 22 2010 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface for supporting Wi-Fi Direct Service Discovery
 * 1. header file restructure for more clear module isolation
 * 2. add function interface definition for implementing Service Discovery callbacks
 *
 * 12 15 2010 cp.wu
 * NULL
 * invoke nicEnableInterrupt() before leaving from wlanAdapterStart()
 *
 * 12 08 2010 yuche.tsai
 * [WCXRP00000245] [MT6620][Driver] Invitation & Provision Discovery Feature Check-in
 * [WCXRP000000245][MT6620][Driver] Invitation Request Feature Add
 *
 * 11 30 2010 yuche.tsai
 * NULL
 * Invitation & Provision Discovery Indication.
 *
 * 11 17 2010 wh.su
 * [WCXRP00000164] [MT6620 Wi-Fi][Driver] Support the p2p random SSID[WCXRP00000179] [MT6620 Wi-Fi][FW] Set the Tx lowest rate at wlan table for normal operation
 * fixed some ASSERT check.
 *
 * 11 04 2010 wh.su
 * [WCXRP00000164] [MT6620 Wi-Fi][Driver] Support the p2p random SSID
 * adding the p2p random ssid support.
 *
 * 10 20 2010 wh.su
 * [WCXRP00000124] [MT6620 Wi-Fi] [Driver] Support the dissolve P2P Group
 * Add the code to support disconnect p2p group
 *
 * 10 04 2010 wh.su
 * [WCXRP00000081] [MT6620][Driver] Fix the compiling error at WinXP while enable P2P
 * add a kal function for set cipher.
 *
 * 10 04 2010 wh.su
 * [WCXRP00000081] [MT6620][Driver] Fix the compiling error at WinXP while enable P2P
 * fixed compiling error while enable p2p.
 *
 * 09 28 2010 wh.su
 * NULL
 * [WCXRP00000069][MT6620 Wi-Fi][Driver] Fix some code for phase 1 P2P Demo.
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000054] [MT6620 Wi-Fi][Driver] Restructure driver for second Interface
 * Isolate P2P related function for Hardware Software Bundle
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * Eliminate Linux Compile Warning
 *
 * 09 10 2010 george.huang
 * NULL
 * update iwpriv LP related
 *
 * 09 10 2010 wh.su
 * NULL
 * fixed the compiling error at win XP.
 *
 * 09 09 2010 cp.wu
 * NULL
 * add WPS/WPA/RSN IE for Wi-Fi Direct scanning result.
 *
 * 09 07 2010 wh.su
 * NULL
 * adding the code for beacon/probe req/ probe rsp wsc ie at p2p.
 *
 * 09 06 2010 wh.su
 * NULL
 * let the p2p can set the privacy bit at beacon and rsn ie at assoc req at key handshake state.
 *
 * 08 25 2010 cp.wu
 * NULL
 * add netdev_ops(NDO) for linux kernel 2.6.31 or greater
 *
 * 08 23 2010 cp.wu
 * NULL
 * revise constant definitions to be matched with implementation (original cmd-event definition is deprecated)
 *
 * 08 20 2010 cp.wu
 * NULL
 * correct typo.
 *
 * 08 20 2010 yuche.tsai
 * NULL
 * Invert Connection request provision status parameter.
 *
 * 08 19 2010 cp.wu
 * NULL
 * add set mac address interface for further possibilities of wpa_supplicant overriding interface address.
 *
 * 08 18 2010 cp.wu
 * NULL
 * modify pwp ioctls attribution by removing FIXED_SIZE.
 *
 * 08 18 2010 jeffrey.chang
 * NULL
 * support multi-function sdio
 *
 * 08 17 2010 cp.wu
 * NULL
 * correct p2p net device registration with NULL pointer access issue.
 *
 * 08 16 2010 cp.wu
 * NULL
 * P2P packets are now marked when being queued into driver, and identified later without checking MAC address
 *
 * 08 16 2010 cp.wu
 * NULL
 * add subroutines for P2P to set multicast list.
 *
 * 08 16 2010 george.huang
 * NULL
 * add wext handlers to link P2P set PS profile/ network address function (TBD)
 *
 * 08 16 2010 cp.wu
 * NULL
 * revised implementation of Wi-Fi Direct io controls.
 *
 * 08 12 2010 cp.wu
 * NULL
 * follow-up with ioctl interface update for Wi-Fi Direct application
 *
 * 08 06 2010 cp.wu
 * NULL
 * driver hook modifications corresponding to ioctl interface change.
 *
 * 08 03 2010 cp.wu
 * NULL
 * add basic support for ioctl of getting scan result. (only address and SSID are reporterd though)
 *
 * 08 03 2010 cp.wu
 * NULL
 * [Wi-Fi Direct Driver Hook] change event indication API to be consistent with supplicant
 *
 * 08 03 2010 cp.wu
 * NULL
 * surpress compilation warning.
 *
 * 08 03 2010 cp.wu
 * NULL
 * [Wi-Fi Direct] add framework for driver hooks
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 23 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * p2p interface revised to be sync. with HAL
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 06 01 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add ioctl to configure scan mode for p2p connection
 *
 * 05 31 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add cfg80211 interface, which is to replace WE, for further extension
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * implement private io controls for Wi-Fi Direct
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * implement get scan result.
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add basic handling framework for wireless extension ioctls.
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * 1) add timeout handler mechanism for pending command packets
 * 2) add p2p add/removal key
 *
 * 05 14 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * implement wireless extension ioctls in iw_handler form.
 *
 * 05 14 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add ioctl framework for Wi-Fi Direct by reusing wireless extension ioctls as well
 *
 * 05 11 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * p2p ioctls revised.
 *
 * 05 11 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add ioctl for controlling p2p scan phase parameters
 *
 * 05 10 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * implement basic wi-fi direct framework
 *
 * 05 07 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add basic framework for implementating P2P driver hook.
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
#include "gl_os.h"
#include "debug.h"
#include "wlan_lib.h"
#include "gl_wext.h"
#include <linux/poll.h>

#include <linux/kmod.h>
//#include <net/cfg80211.h>
#include "gl_p2p_ioctl.h"

#include "precomp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define ARGV_MAX_NUM        (4)

/*For CFG80211 - wiphy parameters*/
#define MAX_SCAN_LIST_NUM   (1)
#define MAX_SCAN_IE_LEN     (512)

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

#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
/* for cfg80211 - frequency table */
static struct ieee80211_channel mtk_2ghz_channels[] = {
    CHAN2G(1, 2412, 0),
    CHAN2G(2, 2417, 0),
    CHAN2G(3, 2422, 0),
    CHAN2G(4, 2427, 0),
    CHAN2G(5, 2432, 0),
    CHAN2G(6, 2437, 0),
    CHAN2G(7, 2442, 0),
    CHAN2G(8, 2447, 0),
    CHAN2G(9, 2452, 0),
    CHAN2G(10, 2457, 0),
    CHAN2G(11, 2462, 0),
    CHAN2G(12, 2467, 0),
    CHAN2G(13, 2472, 0),
    CHAN2G(14, 2484, 0),
};

static struct ieee80211_channel mtk_5ghz_a_channels[] = {
    CHAN5G(34, 0),      CHAN5G(36, 0),
    CHAN5G(38, 0),      CHAN5G(40, 0),
    CHAN5G(42, 0),      CHAN5G(44, 0),
    CHAN5G(46, 0),      CHAN5G(48, 0),
    CHAN5G(52, 0),      CHAN5G(56, 0),
    CHAN5G(60, 0),      CHAN5G(64, 0),
    CHAN5G(100, 0),     CHAN5G(104, 0),
    CHAN5G(108, 0),     CHAN5G(112, 0),
    CHAN5G(116, 0),     CHAN5G(120, 0),
    CHAN5G(124, 0),     CHAN5G(128, 0),
    CHAN5G(132, 0),     CHAN5G(136, 0),
    CHAN5G(140, 0),     CHAN5G(149, 0),
    CHAN5G(153, 0),     CHAN5G(157, 0),
    CHAN5G(161, 0),     CHAN5G(165, 0),
    CHAN5G(169, 0),     CHAN5G(173, 0),
    CHAN5G(184, 0),     CHAN5G(188, 0),
    CHAN5G(192, 0),     CHAN5G(196, 0),
    CHAN5G(200, 0),     CHAN5G(204, 0),
    CHAN5G(208, 0),     CHAN5G(212, 0),
    CHAN5G(216, 0),
};

/* for cfg80211 - rate table */
static struct ieee80211_rate mtk_rates[] = {
    RATETAB_ENT(10,   0x1000,   0),
    RATETAB_ENT(20,   0x1001,   0),
    RATETAB_ENT(55,   0x1002,   0),
    RATETAB_ENT(110,  0x1003,   0), /* 802.11b */
    RATETAB_ENT(60,   0x2000,   0),
    RATETAB_ENT(90,   0x2001,   0),
    RATETAB_ENT(120,  0x2002,   0),
    RATETAB_ENT(180,  0x2003,   0),
    RATETAB_ENT(240,  0x2004,   0),
    RATETAB_ENT(360,  0x2005,   0),
    RATETAB_ENT(480,  0x2006,   0),
    RATETAB_ENT(540,  0x2007,   0), /* 802.11a/g */
};

#define mtk_a_rates         (mtk_rates + 4)
#define mtk_a_rates_size    (sizeof(mtk_rates) / sizeof(mtk_rates[0]) - 4)
#define mtk_g_rates         (mtk_rates + 0)
#define mtk_g_rates_size    (sizeof(mtk_rates) / sizeof(mtk_rates[0]) - 0)

#define MT6620_MCS_INFO                                     \
{                                                           \
        /* MCS1~7*/                                        \
        .rx_mask        = {0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0},\
        .rx_highest     = 0,                                \
        .tx_params      = IEEE80211_HT_MCS_TX_DEFINED,      \
}

#if 0
/*Bandwidth 20Mhz Only*/
#define MT6620_HT_CAP                                       \
{                                                           \
        .ht_supported   = true,                             \
        .cap            = IEEE80211_HT_CAP_SM_PS            \
                        | IEEE80211_HT_CAP_GRN_FLD          \
                        | IEEE80211_HT_CAP_SGI_20,          \
        .ampdu_factor   = IEEE80211_HT_MAX_AMPDU_64K,       \
        .ampdu_density  = IEEE80211_HT_MPDU_DENSITY_NONE,   \
        .mcs            = MT6620_MCS_INFO,                  \
}
#else
/*Bandwidth 20/40Mhz*/
#define MT6620_HT_CAP                                       \
{                                                           \
        .ht_supported   = true,                             \
        .cap            = IEEE80211_HT_CAP_SUP_WIDTH_20_40  \
                        | IEEE80211_HT_CAP_SM_PS            \
                        | IEEE80211_HT_CAP_GRN_FLD          \
                        | IEEE80211_HT_CAP_SGI_20           \
                        | IEEE80211_HT_CAP_SGI_40,          \
        .ampdu_factor   = IEEE80211_HT_MAX_AMPDU_64K,       \
        .ampdu_density  = IEEE80211_HT_MPDU_DENSITY_NONE,   \
        .mcs            = MT6620_MCS_INFO,                  \
}
#endif

static struct ieee80211_supported_band mtk_band_2ghz = {
    .band       = IEEE80211_BAND_2GHZ,
    .channels   = mtk_2ghz_channels,
    .n_channels = ARRAY_SIZE(mtk_2ghz_channels),
    .bitrates   = mtk_g_rates,
    .n_bitrates = mtk_g_rates_size,
    .ht_cap     = MT6620_HT_CAP,
};

static struct ieee80211_supported_band mtk_band_5ghz = {
    .band       = IEEE80211_BAND_5GHZ,
    .channels   = mtk_5ghz_a_channels,
    .n_channels = ARRAY_SIZE(mtk_5ghz_a_channels),
    .bitrates   = mtk_a_rates,
    .n_bitrates = mtk_a_rates_size,
    .ht_cap     = MT6620_HT_CAP,
};

static const UINT_32 cipher_suites[] = {
	/* keep WEP first, it may be removed below */
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,

	/* keep last -- depends on hw flags! */
	WLAN_CIPHER_SUITE_AES_CMAC
};

static struct cfg80211_ops mtk_p2p_config_ops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32) && (CFG_ENABLE_WIFI_DIRECT_CFG_80211 != 0)
    /* Froyo */
    .change_virtual_intf    = mtk_p2p_cfg80211_change_iface,      // 1 st
    .change_bss = mtk_p2p_cfg80211_change_bss,
    .scan = mtk_p2p_cfg80211_scan,
    .remain_on_channel = mtk_p2p_cfg80211_remain_on_channel,
    .cancel_remain_on_channel = mtk_p2p_cfg80211_cancel_remain_on_channel,
    .mgmt_tx = mtk_p2p_cfg80211_mgmt_tx,
    .connect = mtk_p2p_cfg80211_connect,
    .disconnect = mtk_p2p_cfg80211_disconnect,
    .deauth = mtk_p2p_cfg80211_deauth,
    .disassoc = mtk_p2p_cfg80211_disassoc,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
    .start_ap = mtk_p2p_cfg80211_start_ap,
    .change_beacon = mtk_p2p_cfg80211_change_beacon,
    .stop_ap = mtk_p2p_cfg80211_stop_ap,
#else
    .add_beacon = mtk_p2p_cfg80211_add_set_beacon,
    .set_beacon = mtk_p2p_cfg80211_add_set_beacon,
    .del_beacon = mtk_p2p_cfg80211_stop_ap,
#endif
    .set_wiphy_params = mtk_p2p_cfg80211_set_wiphy_params,
    .del_station = mtk_p2p_cfg80211_del_station,
    .set_channel = mtk_p2p_cfg80211_set_channel,
    .set_bitrate_mask = mtk_p2p_cfg80211_set_bitrate_mask,
    .mgmt_frame_register = mtk_p2p_cfg80211_mgmt_frame_register,
    .get_station            = mtk_p2p_cfg80211_get_station,
    // ================
    .add_key                = mtk_p2p_cfg80211_add_key,
    .get_key                = mtk_p2p_cfg80211_get_key,
    .del_key                = mtk_p2p_cfg80211_del_key,
    .set_default_key        = mtk_p2p_cfg80211_set_default_key,
    .join_ibss              = mtk_p2p_cfg80211_join_ibss,
    .leave_ibss             = mtk_p2p_cfg80211_leave_ibss,
    .set_tx_power           = mtk_p2p_cfg80211_set_txpower,
    .get_tx_power           = mtk_p2p_cfg80211_get_txpower,
    .set_power_mgmt         = mtk_p2p_cfg80211_set_power_mgmt,
    #ifdef CONFIG_NL80211_TESTMODE
    .testmode_cmd           = mtk_p2p_cfg80211_testmode_cmd,
    #endif
#endif
};



/* There isn't a lot of sense in it, but you can transmit anything you like */
static const struct ieee80211_txrx_stypes
mtk_cfg80211_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_ADHOC] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_STATION] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_AP] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_AP_VLAN] = {
		/* copy AP */
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_P2P_GO] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	}
};



#endif

/* the legacy wireless extension stuff */
static const iw_handler rP2PIwStandardHandler[] = {
    [SIOCGIWPRIV        - SIOCIWFIRST]  = mtk_p2p_wext_get_priv,
    [SIOCGIWSCAN        - SIOCIWFIRST]  = mtk_p2p_wext_discovery_results,
    [SIOCSIWESSID       - SIOCIWFIRST]  = mtk_p2p_wext_reconnect,
    [SIOCSIWAUTH        - SIOCIWFIRST]  = mtk_p2p_wext_set_auth,
    [SIOCSIWENCODEEXT   - SIOCIWFIRST]  = mtk_p2p_wext_set_key,
    [SIOCSIWPOWER       - SIOCIWFIRST]  = mtk_p2p_wext_set_powermode,
    [SIOCGIWPOWER       - SIOCIWFIRST]  = mtk_p2p_wext_get_powermode,
    [SIOCSIWTXPOW       - SIOCIWFIRST]  = mtk_p2p_wext_set_txpow,
#if CFG_SUPPORT_P2P_RSSI_QUERY
    [SIOCGIWSTATS       - SIOCIWFIRST]  = mtk_p2p_wext_get_rssi,
#endif
    [SIOCSIWMLME        - SIOCIWFIRST]  = mtk_p2p_wext_mlme_handler,
};

static const iw_handler rP2PIwPrivHandler[] = {
    [IOC_P2P_CFG_DEVICE                 - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_set_local_dev_info,
    [IOC_P2P_PROVISION_COMPLETE         - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_set_provision_complete,
    [IOC_P2P_START_STOP_DISCOVERY       - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_start_stop_discovery,
    [IOC_P2P_DISCOVERY_RESULTS          - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_discovery_results,
    [IOC_P2P_WSC_BEACON_PROBE_RSP_IE    - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_wsc_ie,
    [IOC_P2P_CONNECT_DISCONNECT         - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_connect_disconnect,
    [IOC_P2P_PASSWORD_READY             - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_password_ready,
//    [IOC_P2P_SET_PWR_MGMT_PARAM         - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_set_pm_param,
    [IOC_P2P_SET_INT                    - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_set_int,
    [IOC_P2P_GET_STRUCT                 - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_get_struct,
    [IOC_P2P_SET_STRUCT                 - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_set_struct,
    [IOC_P2P_GET_REQ_DEVICE_INFO        - SIOCIWFIRSTPRIV]  = mtk_p2p_wext_request_dev_info,
};


static const struct iw_priv_args rP2PIwPrivTable[] = {
    {
        .cmd = IOC_P2P_CFG_DEVICE,
        .set_args = IW_PRIV_TYPE_BYTE | (__u16)sizeof(IW_P2P_CFG_DEVICE_TYPE),
        .get_args = IW_PRIV_TYPE_NONE,
        .name = "P2P_CFG_DEVICE"
    },
    {
        .cmd = IOC_P2P_START_STOP_DISCOVERY,
        .set_args = IW_PRIV_TYPE_BYTE | (__u16)sizeof(IW_P2P_REQ_DEVICE_TYPE),
        .get_args = IW_PRIV_TYPE_NONE,
        .name = "P2P_DISCOVERY"
    },
    {
        .cmd = IOC_P2P_DISCOVERY_RESULTS,
        .set_args = IW_PRIV_TYPE_NONE,
        .get_args = IW_PRIV_TYPE_NONE,
        .name = "P2P_RESULT"
    },
    {
        .cmd = IOC_P2P_WSC_BEACON_PROBE_RSP_IE,
        .set_args = IW_PRIV_TYPE_BYTE | (__u16)sizeof(IW_P2P_HOSTAPD_PARAM),
        .get_args = IW_PRIV_TYPE_NONE,
        .name = "P2P_WSC_IE"
    },
    {
        .cmd = IOC_P2P_CONNECT_DISCONNECT,
        .set_args = IW_PRIV_TYPE_BYTE | (__u16)sizeof(IW_P2P_CONNECT_DEVICE),
        .get_args = IW_PRIV_TYPE_NONE,
        .name = "P2P_CONNECT"
    },
    {
        .cmd = IOC_P2P_PASSWORD_READY,
        .set_args = IW_PRIV_TYPE_BYTE | (__u16)sizeof(IW_P2P_PASSWORD_READY),
        .get_args = IW_PRIV_TYPE_NONE,
        .name = "P2P_PASSWD_RDY"
    },
    {
        .cmd = IOC_P2P_GET_STRUCT,
        .set_args = IW_PRIV_TYPE_NONE,
        .get_args = 256,
        .name = "P2P_GET_STRUCT"
    },
    {
        .cmd = IOC_P2P_SET_STRUCT,
        .set_args = 256,
        .get_args = IW_PRIV_TYPE_NONE,
        .name = "P2P_SET_STRUCT"
    },
    {
        .cmd = IOC_P2P_GET_REQ_DEVICE_INFO,
        .set_args = IW_PRIV_TYPE_NONE,
        .get_args = IW_PRIV_TYPE_BYTE | (__u16)sizeof(IW_P2P_DEVICE_REQ),
        .name = "P2P_GET_REQDEV"
    },
    {
        /* SET STRUCT sub-ioctls commands */
        .cmd = PRIV_CMD_OID,
        .set_args = 256,
        .get_args = IW_PRIV_TYPE_NONE,
        .name = "set_oid"
    },
    {
        /* GET STRUCT sub-ioctls commands */
        .cmd = PRIV_CMD_OID,
        .set_args = IW_PRIV_TYPE_NONE,
        .get_args = 256,
        .name = "get_oid"
    }
};

const struct iw_handler_def mtk_p2p_wext_handler_def = {
    .num_standard       = (__u16)sizeof(rP2PIwStandardHandler)/sizeof(iw_handler),
    .num_private        = (__u16)sizeof(rP2PIwPrivHandler)/sizeof(iw_handler),
    .num_private_args   = (__u16)sizeof(rP2PIwPrivTable)/sizeof(struct iw_priv_args),
    .standard           = rP2PIwStandardHandler,
    .private            = rP2PIwPrivHandler,
    .private_args       = rP2PIwPrivTable,
#if CFG_SUPPORT_P2P_RSSI_QUERY
    .get_wireless_stats = mtk_p2p_wext_get_wireless_stats,
#else
    .get_wireless_stats = NULL,
#endif
};

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/* for IE Searching */
extern BOOLEAN
wextSrchDesiredWPAIE (
    IN  PUINT_8         pucIEStart,
    IN  INT_32          i4TotalIeLen,
    IN  UINT_8          ucDesiredElemId,
    OUT PUINT_8         *ppucDesiredIE
    );

#if CFG_SUPPORT_WPS
extern BOOLEAN
wextSrchDesiredWPSIE (
    IN PUINT_8 pucIEStart,
    IN INT_32 i4TotalIeLen,
    IN UINT_8 ucDesiredElemId,
    OUT PUINT_8 *ppucDesiredIE
    );
#endif

/* Net Device Hooks */
static int
p2pOpen(
    IN struct net_device *prDev
    );

static int
p2pStop(
    IN struct net_device *prDev
    );

static struct net_device_stats *
p2pGetStats (
    IN struct net_device *prDev
    );

static void
p2pSetMulticastList(
    IN struct net_device *prDev
    );

static int
p2pHardStartXmit(
    IN struct sk_buff *prSkb,
    IN struct net_device *prDev
    );

static int
p2pDoIOCTL(
    struct net_device *prDev,
    struct ifreq *prIFReq,
    int i4Cmd
    );

static int
p2pSetMACAddress(
    IN struct net_device *prDev,
    void *addr
    );


/*----------------------------------------------------------------------------*/
/*!
* \brief Override the implementation of select queue
*
* \param[in] dev Pointer to struct net_device
* \param[in] skb Pointer to struct skb_buff
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/

unsigned int _p2p_cfg80211_classify8021d(struct sk_buff *skb)
{
    unsigned int dscp = 0;

    /* skb->priority values from 256->263 are magic values
     * directly indicate a specific 802.1d priority.  This is
     * to allow 802.1d priority to be passed directly in from
     * tags
     */

    if (skb->priority >= 256 && skb->priority <= 263) {
        return skb->priority - 256;
    }
    switch (skb->protocol) {
        case htons(ETH_P_IP):
            dscp = ip_hdr(skb)->tos & 0xfc;
            break;
    }
    return dscp >> 5;
}


static const UINT_16 au16Wlan1dToQueueIdx[8] = { 1, 0, 0, 1, 2, 2, 3, 3 };

static UINT_16
p2pSelectQueue(
    struct net_device *dev,
    struct sk_buff *skb)
{
    skb->priority = _p2p_cfg80211_classify8021d(skb);

    return au16Wlan1dToQueueIdx[skb->priority];
}

static struct net_device *g_P2pPrDev;


/*----------------------------------------------------------------------------*/
/*!
* \brief A function for prDev->init
*
* \param[in] prDev      Pointer to struct net_device.
*
* \retval 0         The execution of wlanInit succeeds.
* \retval -ENXIO    No such device.
*/
/*----------------------------------------------------------------------------*/
static int
p2pInit(
    struct net_device *prDev
    )
{
    if (!prDev) {
        return -ENXIO;
    }

    return 0; /* success */
} /* end of p2pInit() */


/*----------------------------------------------------------------------------*/
/*!
* \brief A function for prDev->uninit
*
* \param[in] prDev      Pointer to struct net_device.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
static void
p2pUninit (
    IN struct net_device *prDev
    )
{


    return;
} /* end of p2pUninit() */






#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
static const struct net_device_ops p2p_netdev_ops = {
    .ndo_open               = p2pOpen,
    .ndo_stop               = p2pStop,
    .ndo_set_mac_address    = p2pSetMACAddress,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
    .ndo_set_rx_mode = p2pSetMulticastList,
#else
    .ndo_set_multicast_list = p2pSetMulticastList,
#endif
    .ndo_get_stats          = p2pGetStats,
    .ndo_do_ioctl           = p2pDoIOCTL,
    .ndo_start_xmit         = p2pHardStartXmit,
    .ndo_select_queue =  p2pSelectQueue,
    .ndo_init		= p2pInit,
    .ndo_uninit         = p2pUninit,
};


#endif


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief Allocate memory for P2P_INFO, GL_P2P_INFO, P2P_CONNECTION_SETTINGS
*                                          P2P_SPECIFIC_BSS_INFO, P2P_FSM_INFO
*
* \param[in] prGlueInfo      Pointer to glue info
*
* \return   TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
p2PAllocInfo (
    IN P_GLUE_INFO_T prGlueInfo
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_WIFI_VAR_T prWifiVar = NULL;

    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    prWifiVar = &(prAdapter->rWifiVar);

    ASSERT(prAdapter);
    ASSERT(prWifiVar);

    do {
        if (prGlueInfo == NULL) {
            break;
        }

        if (prGlueInfo->prP2PInfo == NULL) {
            /*alloc memory for p2p info */
            prGlueInfo->prP2PInfo =
                kalMemAlloc(sizeof(GL_P2P_INFO_T), VIR_MEM_TYPE);
            prAdapter->prP2pInfo =
                kalMemAlloc(sizeof(P2P_INFO_T), VIR_MEM_TYPE);
            prWifiVar->prP2PConnSettings =
                kalMemAlloc(sizeof(P2P_CONNECTION_SETTINGS_T),VIR_MEM_TYPE);
            prWifiVar->prP2pFsmInfo =
                kalMemAlloc(sizeof(P2P_FSM_INFO_T),VIR_MEM_TYPE);
            prWifiVar->prP2pSpecificBssInfo =
                kalMemAlloc(sizeof(P2P_SPECIFIC_BSS_INFO_T),VIR_MEM_TYPE);
        }
        else {
            ASSERT(prAdapter->prP2pInfo != NULL);
            ASSERT(prWifiVar->prP2PConnSettings != NULL);
            ASSERT(prWifiVar->prP2pFsmInfo != NULL);
            ASSERT(prWifiVar->prP2pSpecificBssInfo != NULL);
        }
        /*MUST set memory to 0 */
        kalMemZero(prGlueInfo->prP2PInfo, sizeof(GL_P2P_INFO_T));
        kalMemZero(prAdapter->prP2pInfo, sizeof(P2P_INFO_T));
        kalMemZero(prWifiVar->prP2PConnSettings, sizeof(P2P_CONNECTION_SETTINGS_T));
        kalMemZero(prWifiVar->prP2pFsmInfo, sizeof(P2P_FSM_INFO_T));
        kalMemZero(prWifiVar->prP2pSpecificBssInfo, sizeof(P2P_SPECIFIC_BSS_INFO_T));
        
    } while (FALSE);
        

    /* chk if alloc successful or not*/
    if (prGlueInfo->prP2PInfo &&
            prAdapter->prP2pInfo &&
            prWifiVar->prP2PConnSettings &&
            prWifiVar->prP2pFsmInfo &&
            prWifiVar->prP2pSpecificBssInfo) {
        return TRUE;
    }
    else {

        if (prWifiVar->prP2pSpecificBssInfo) {
            kalMemFree(prWifiVar->prP2pSpecificBssInfo, VIR_MEM_TYPE, sizeof(P2P_SPECIFIC_BSS_INFO_T));

            prWifiVar->prP2pSpecificBssInfo = NULL;
        }
        if (prWifiVar->prP2pFsmInfo) {
            kalMemFree(prWifiVar->prP2pFsmInfo, VIR_MEM_TYPE, sizeof(P2P_FSM_INFO_T));

            prWifiVar->prP2pFsmInfo = NULL;
        }
        if (prWifiVar->prP2PConnSettings) {
            kalMemFree(prWifiVar->prP2PConnSettings, VIR_MEM_TYPE, sizeof(P2P_CONNECTION_SETTINGS_T));

            prWifiVar->prP2PConnSettings = NULL;
        }
        if (prGlueInfo->prP2PInfo) {
            kalMemFree(prGlueInfo->prP2PInfo, VIR_MEM_TYPE, sizeof(GL_P2P_INFO_T));

            prGlueInfo->prP2PInfo = NULL;
        }
        if (prAdapter->prP2pInfo) {
            kalMemFree(prAdapter->prP2pInfo, VIR_MEM_TYPE, sizeof(P2P_INFO_T));

            prAdapter->prP2pInfo = NULL;
        }
        return FALSE;
    }

}


/*----------------------------------------------------------------------------*/
/*!
* \brief Free memory for P2P_INFO, GL_P2P_INFO, P2P_CONNECTION_SETTINGS
*                                          P2P_SPECIFIC_BSS_INFO, P2P_FSM_INFO
*
* \param[in] prGlueInfo      Pointer to glue info
*
* \return   TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
p2PFreeInfo(
    P_GLUE_INFO_T prGlueInfo
    )
{

    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prAdapter);

    /* free memory after p2p module is ALREADY unregistered */
    if(prGlueInfo->prAdapter->fgIsP2PRegistered == FALSE) {

        kalMemFree(prGlueInfo->prAdapter->prP2pInfo, VIR_MEM_TYPE, sizeof(P2P_INFO_T));
        kalMemFree(prGlueInfo->prP2PInfo, VIR_MEM_TYPE, sizeof(GL_P2P_INFO_T));
        kalMemFree(prGlueInfo->prAdapter->rWifiVar.prP2PConnSettings, VIR_MEM_TYPE, sizeof(P2P_CONNECTION_SETTINGS_T));
        kalMemFree(prGlueInfo->prAdapter->rWifiVar.prP2pFsmInfo, VIR_MEM_TYPE, sizeof(P2P_FSM_INFO_T));
        kalMemFree(prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo, VIR_MEM_TYPE, sizeof(P2P_SPECIFIC_BSS_INFO_T));

        /*Reomve p2p bss scan list*/
        scanRemoveAllP2pBssDesc(prGlueInfo->prAdapter);

        /*reset all pointer to NULL */
        prGlueInfo->prP2PInfo = NULL;
        prGlueInfo->prAdapter->prP2pInfo = NULL;
        prGlueInfo->prAdapter->rWifiVar.prP2PConnSettings = NULL;
        prGlueInfo->prAdapter->rWifiVar.prP2pFsmInfo = NULL;
        prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo = NULL;

        return TRUE;
    }
    else {
        return FALSE;
    }

}

/*----------------------------------------------------------------------------*/
/*!
* \brief Enable Channel  for cfg80211 for Wi-Fi Direct based on current country code
*
* \param[in] prGlueInfo      Pointer to glue info
*
* \return   TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
VOID
p2pEnableChannel(
    PUINT_8 pucChIdx,
    UINT_8 ucChannelNum,
    struct ieee80211_channel *mtk_channels,
    UINT_8 mtk_channel_sz
)
{
    UINT_8 ucCurChIdx = *pucChIdx;

    while(TRUE) {
        (*pucChIdx)++;
        (*pucChIdx) %= mtk_channel_sz;

        if(ucChannelNum == mtk_channels[*pucChIdx].hw_value) {
            mtk_channels[*pucChIdx].flags &= ~IEEE80211_CHAN_DISABLED;
            break;
        }

        if(*pucChIdx == ucCurChIdx) {
            printk(KERN_ALERT DRV_NAME "Orphan channel [%d]\n", ucChannelNum);
            break;
        }
    }
}


BOOLEAN
p2pNetRegister(
    P_GLUE_INFO_T prGlueInfo,
    BOOLEAN fgIsRtnlLockAcquired
    )
{
    BOOLEAN fgDoRegister = FALSE;
    BOOLEAN fgRollbackRtnlLock = FALSE;
    BOOLEAN ret;

    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prAdapter);


    GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
    if(prGlueInfo->prAdapter->rP2PNetRegState == ENUM_NET_REG_STATE_UNREGISTERED) {
        prGlueInfo->prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_REGISTERING;
        fgDoRegister = TRUE;
    }
    GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

    if(!fgDoRegister) {
        return TRUE;
    }

    if(fgIsRtnlLockAcquired && rtnl_is_locked()) {
        fgRollbackRtnlLock = TRUE;
        rtnl_unlock();
    }

    /* Here are functions which need rtnl_lock */
    wiphy_register(prGlueInfo->prP2PInfo->wdev.wiphy);

    /* net device initialize */
    netif_carrier_off(prGlueInfo->prP2PInfo->prDevHandler);
    netif_tx_stop_all_queues(prGlueInfo->prP2PInfo->prDevHandler);

    /* register for net device */
    if (register_netdev(prGlueInfo->prP2PInfo->prDevHandler) < 0) {
        printk(KERN_ALERT DRV_NAME "unable to register netdevice for p2p\n");

        free_netdev(prGlueInfo->prP2PInfo->prDevHandler);

        ret = FALSE;
    }
    else {
        prGlueInfo->prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_REGISTERED;
        ret = TRUE;
    }

    if(fgRollbackRtnlLock) {
        rtnl_lock();
    }

    return ret;
}

BOOLEAN
p2pNetUnregister(
    P_GLUE_INFO_T prGlueInfo,
    BOOLEAN fgIsRtnlLockAcquired
    )
{
    BOOLEAN fgDoUnregister = FALSE;
    BOOLEAN fgRollbackRtnlLock = FALSE;

    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prGlueInfo);
    ASSERT(prGlueInfo->prAdapter);

    GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
    if(prGlueInfo->prAdapter->rP2PNetRegState == ENUM_NET_REG_STATE_REGISTERED) {
        prGlueInfo->prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_UNREGISTERING;
        fgDoUnregister = TRUE;
    }
    GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);

    if(!fgDoUnregister) {
        return TRUE;
    }

    /* prepare for removal */
    if(netif_carrier_ok(prGlueInfo->prP2PInfo->prDevHandler)) {
        netif_carrier_off(prGlueInfo->prP2PInfo->prDevHandler);
    }

    netif_tx_stop_all_queues(prGlueInfo->prP2PInfo->prDevHandler);

    if(fgIsRtnlLockAcquired && rtnl_is_locked()) {
        fgRollbackRtnlLock = TRUE;
        rtnl_unlock();
    }
    /* Here are functions which need rtnl_lock */

    unregister_netdev(prGlueInfo->prP2PInfo->prDevHandler);

    wiphy_unregister(prGlueInfo->prP2PInfo->wdev.wiphy);

    if(fgRollbackRtnlLock) {
        rtnl_lock();
    }

    prGlueInfo->prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_UNREGISTERED;

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Update Channel table for cfg80211 for Wi-Fi Direct based on current country code
*
* \param[in] prGlueInfo      Pointer to glue info
*
* \return   TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
VOID
p2pUpdateChannelTableByDomain(
    P_GLUE_INFO_T prGlueInfo
    )
{
    UINT_8 i, uc2gChIdx, uc5gChIdx;
    UINT_8 ucMaxChannelNum = ARRAY_SIZE(mtk_2ghz_channels) + ARRAY_SIZE(mtk_5ghz_a_channels);
    UINT_8 ucNumOfChannel = ucMaxChannelNum;
    RF_CHANNEL_INFO_T aucChannelList[ucMaxChannelNum];

    uc2gChIdx = uc5gChIdx = 0;

    // 1. Disable all channel
    for(i = 0; i < ARRAY_SIZE(mtk_2ghz_channels); i++) {
        mtk_2ghz_channels[i].flags |= IEEE80211_CHAN_DISABLED;
    }
    for(i = 0; i < ARRAY_SIZE(mtk_5ghz_a_channels); i++) {
        mtk_5ghz_a_channels[i].flags |= IEEE80211_CHAN_DISABLED;
    }

    // 2. Get current domain channel list
    rlmDomainGetChnlList(prGlueInfo->prAdapter, BAND_NULL, ucMaxChannelNum, &ucNumOfChannel, aucChannelList);

    // 3. Enable specific channel based on domain channel list
    for(i = 0; i < ucNumOfChannel; i++) {
        switch(aucChannelList[i].eBand) {
            case BAND_2G4:
                p2pEnableChannel(&uc2gChIdx, aucChannelList[i].ucChannelNum, mtk_2ghz_channels, ARRAY_SIZE(mtk_2ghz_channels));
                break;

            case BAND_5G:
                p2pEnableChannel(&uc5gChIdx, aucChannelList[i].ucChannelNum, mtk_5ghz_a_channels, ARRAY_SIZE(mtk_5ghz_a_channels));
                break;

            default:
                printk(KERN_ALERT DRV_NAME "Unknow band.\n");
                break;

        }
    }

}

/*----------------------------------------------------------------------------*/
/*!
* \brief Register for cfg80211 for Wi-Fi Direct
*
* \param[in] prGlueInfo      Pointer to glue info
*
* \return   TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
glRegisterP2P(
    P_GLUE_INFO_T prGlueInfo,
    const char *prDevName,
    BOOLEAN fgIsApMode
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GL_HIF_INFO_T prHif = NULL;
    PARAM_MAC_ADDRESS rMacAddr;
#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
    struct device *prDev;
#endif

    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    prHif = &prGlueInfo->rHifInfo;
    ASSERT(prHif);

    printk("glRegisterP2P\n");

    /*0. allocate p2pinfo */
    if(!p2PAllocInfo(prGlueInfo)) {
        printk(KERN_ALERT DRV_NAME "Allocate memory for p2p FAILED\n");
        ASSERT(0);
        return FALSE;
    }
#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
    /* 1. allocate WIPHY */
    prGlueInfo->prP2PInfo->wdev.wiphy = wiphy_new(&mtk_p2p_config_ops, sizeof(P_GLUE_INFO_T));
    if (!prGlueInfo->prP2PInfo->wdev.wiphy) {
        printk(KERN_ALERT DRV_NAME "unable to allocate wiphy for p2p\n");

        goto err_alloc_wiphy;
    }

    /* 1.1 fill wiphy parameters */
#if MTK_WCN_HIF_SDIO
    mtk_wcn_hif_sdio_get_dev(prHif->cltCtx, &prDev);
    if(!prDev) {
        printk(KERN_ALERT DRV_NAME "unable to get struct dev for p2p\n");
    }
#else
    prDev = &(prHif->func->dev);
#endif
    set_wiphy_dev(prGlueInfo->prP2PInfo->wdev.wiphy, prDev);

    prGlueInfo->prP2PInfo->wdev.wiphy->interface_modes = BIT(NL80211_IFTYPE_AP)
                                                       | BIT(NL80211_IFTYPE_P2P_CLIENT)
                                                       | BIT(NL80211_IFTYPE_P2P_GO)
                                                       | BIT(NL80211_IFTYPE_STATION);

    p2pUpdateChannelTableByDomain(prGlueInfo);
    prGlueInfo->prP2PInfo->wdev.wiphy->bands[IEEE80211_BAND_2GHZ] = &mtk_band_2ghz;
    if(prAdapter->fgEnable5GBand) {
        prGlueInfo->prP2PInfo->wdev.wiphy->bands[IEEE80211_BAND_5GHZ] = &mtk_band_5ghz;
    }

    prGlueInfo->prP2PInfo->wdev.wiphy->mgmt_stypes = mtk_cfg80211_default_mgmt_stypes;
    prGlueInfo->prP2PInfo->wdev.wiphy->max_remain_on_channel_duration = 5000;
    prGlueInfo->prP2PInfo->wdev.wiphy->n_cipher_suites = 5;
	prGlueInfo->prP2PInfo->wdev.wiphy->cipher_suites = (const u32*)cipher_suites;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	prGlueInfo->prP2PInfo->wdev.wiphy->flags = WIPHY_FLAG_CUSTOM_REGULATORY | WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
#else
	prGlueInfo->prP2PInfo->wdev.wiphy->flags = WIPHY_FLAG_CUSTOM_REGULATORY;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
    prGlueInfo->prP2PInfo->wdev.wiphy->max_scan_ssids = MAX_SCAN_LIST_NUM;
    prGlueInfo->prP2PInfo->wdev.wiphy->max_scan_ie_len = MAX_SCAN_IE_LEN;
    prGlueInfo->prP2PInfo->wdev.wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
#endif

#if 0
    /* 2. Register WIPHY */
    if(wiphy_register(prGlueInfo->prP2PInfo->wdev.wiphy) < 0) {
        printk(KERN_ALERT DRV_NAME "Couldn't register wiphy device for p2p\n");

        goto err_reg_wiphy;
    }
#endif

    /* 2.1 set priv as pointer to glue structure */
    *((P_GLUE_INFO_T *) wiphy_priv(prGlueInfo->prP2PInfo->wdev.wiphy)) = prGlueInfo;

    /* 2.2 wdev initialization */
    if(fgIsApMode) {
        prGlueInfo->prP2PInfo->wdev.iftype = NL80211_IFTYPE_AP;
    } else {
        prGlueInfo->prP2PInfo->wdev.iftype = NL80211_IFTYPE_P2P_CLIENT;
    }

#endif /* CFG_ENABLE_WIFI_DIRECT_CFG_80211 */

    /* 3. allocate netdev */
    prGlueInfo->prP2PInfo->prDevHandler = alloc_netdev_mq(sizeof(P_GLUE_INFO_T), prDevName, ether_setup, CFG_MAX_TXQ_NUM);
    if (!prGlueInfo->prP2PInfo->prDevHandler) {
        printk(KERN_ALERT DRV_NAME "unable to allocate netdevice for p2p\n");

        printk("unable to allocate netdevice for p2p\n");

        goto err_alloc_netdev;
    }

    /* 4. setup netdev */
    /* 4.1 Point to shared glue structure */
    *((P_GLUE_INFO_T *) netdev_priv(prGlueInfo->prP2PInfo->prDevHandler)) = prGlueInfo;

    /* 4.2 fill hardware address */
    COPY_MAC_ADDR(rMacAddr, prAdapter->rMyMacAddr);
    rMacAddr[0] ^= 0x2; // change to local administrated address
    memcpy(prGlueInfo->prP2PInfo->prDevHandler->dev_addr, rMacAddr, ETH_ALEN);
    memcpy(prGlueInfo->prP2PInfo->prDevHandler->perm_addr, prGlueInfo->prP2PInfo->prDevHandler->dev_addr, ETH_ALEN);

    /* 4.3 register callback functions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
    prGlueInfo->prP2PInfo->prDevHandler->netdev_ops           = &p2p_netdev_ops;
#else
    prGlueInfo->prP2PInfo->prDevHandler->open                 = p2pOpen;
    prGlueInfo->prP2PInfo->prDevHandler->stop                 = p2pStop;
    prGlueInfo->prP2PInfo->prDevHandler->get_stats            = p2pGetStats;
    prGlueInfo->prP2PInfo->prDevHandler->set_multicast_list   = p2pSetMulticastList;
    prGlueInfo->prP2PInfo->prDevHandler->hard_start_xmit      = p2pHardStartXmit;
    prGlueInfo->prP2PInfo->prDevHandler->do_ioctl             = p2pDoIOCTL;
    prGlueInfo->prP2PInfo->prDevHandler->set_mac_address      = p2pSetMACAddress;
#endif
//    prGlueInfo->prP2PInfo->prDevHandler->wireless_handlers    = &mtk_p2p_wext_handler_def;


#if (MTK_WCN_HIF_SDIO == 0)
    SET_NETDEV_DEV(prGlueInfo->prP2PInfo->prDevHandler, &(prHif->func->dev));
#endif


#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
    prGlueInfo->prP2PInfo->prDevHandler->ieee80211_ptr        = &(prGlueInfo->prP2PInfo->wdev);
    prGlueInfo->prP2PInfo->wdev.netdev                        = prGlueInfo->prP2PInfo->prDevHandler;
#endif

#if CFG_TCP_IP_CHKSUM_OFFLOAD
    prGlueInfo->prP2PInfo->prDevHandler->features = NETIF_F_IP_CSUM;
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */

#if 0
    /* 7. net device initialize */
    netif_carrier_off(prGlueInfo->prP2PInfo->prDevHandler);
    netif_tx_stop_all_queues(prGlueInfo->prP2PInfo->prDevHandler);

    /* 8. register for net device */
    if (register_netdev(prGlueInfo->prP2PInfo->prDevHandler) < 0) {
        printk(KERN_ALERT DRV_NAME "unable to register netdevice for p2p\n");

        goto err_reg_netdev;
    }
#endif

    /* 8. set p2p net device register state */
    prGlueInfo->prAdapter->rP2PNetRegState = ENUM_NET_REG_STATE_UNREGISTERED;

    /* 9. setup running mode*/
    prGlueInfo->prAdapter->rWifiVar.prP2pFsmInfo->fgIsApMode = fgIsApMode;

    /* 10. finish */
    p2pFsmInit(prAdapter);

    p2pFuncInitConnectionSettings(prAdapter, prAdapter->rWifiVar.prP2PConnSettings);

    /* Active network too early would cause HW not able to sleep. 
         * Defer the network active time.
         */
//    nicActivateNetwork(prAdapter, NETWORK_TYPE_P2P_INDEX);

    return TRUE;
#if 0
err_reg_netdev:
    free_netdev(prGlueInfo->prP2PInfo->prDevHandler);
#endif
err_alloc_netdev:
#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
//    wiphy_unregister(prGlueInfo->prP2PInfo->wdev.wiphy);

//err_reg_wiphy:
    wiphy_free(prGlueInfo->prP2PInfo->wdev.wiphy);
    prGlueInfo->prP2PInfo->wdev.wiphy = NULL;

err_alloc_wiphy:
#endif

    return FALSE;
} /* end of glRegisterP2P() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Unregister Net Device for Wi-Fi Direct
*
* \param[in] prGlueInfo      Pointer to glue info
*
* \return   TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
glUnregisterP2P(
    P_GLUE_INFO_T prGlueInfo
    )
{
    ASSERT(prGlueInfo);

    p2pFsmUninit(prGlueInfo->prAdapter);

    nicDeactivateNetwork(prGlueInfo->prAdapter, NETWORK_TYPE_P2P_INDEX);

#if 0
    /* Release command, mgmt and security frame belong to P2P network in
         *   prGlueInfo->prCmdQue
         *   prAdapter->rPendingCmdQueue
         *   prAdapter->rTxCtrl.rTxMgmtTxingQueue
         *  To ensure there is no pending CmdDone/TxDone handler to be executed after p2p module is removed.
         */

    /* Clear CmdQue*/
    kalClearMgmtFramesByNetType(prGlueInfo, NETWORK_TYPE_P2P_INDEX);
    kalClearSecurityFramesByNetType(prGlueInfo, NETWORK_TYPE_P2P_INDEX);
    /* Clear PendingCmdQue*/
    wlanReleasePendingCMDbyNetwork(prGlueInfo->prAdapter, NETWORK_TYPE_P2P_INDEX);
    /* Clear PendingTxMsdu */
    nicFreePendingTxMsduInfoByNetwork(prGlueInfo->prAdapter, NETWORK_TYPE_P2P_INDEX);
#endif

#if 0
    /* prepare for removal */
    if(netif_carrier_ok(prGlueInfo->prP2PInfo->prDevHandler)) {
        netif_carrier_off(prGlueInfo->prP2PInfo->prDevHandler);
    }

    netif_tx_stop_all_queues(prGlueInfo->prP2PInfo->prDevHandler);

    /* netdevice unregistration & free */
    unregister_netdev(prGlueInfo->prP2PInfo->prDevHandler);
#endif
    free_netdev(prGlueInfo->prP2PInfo->prDevHandler);
    prGlueInfo->prP2PInfo->prDevHandler = NULL;

#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
#if 0
    wiphy_unregister(prGlueInfo->prP2PInfo->wdev.wiphy);
#endif
    wiphy_free(prGlueInfo->prP2PInfo->wdev.wiphy);
    prGlueInfo->prP2PInfo->wdev.wiphy = NULL;
#endif

    /* Free p2p memory */
#if 1
    if(!p2PFreeInfo(prGlueInfo)) {
        printk(KERN_ALERT DRV_NAME "Free memory for p2p FAILED\n");
        ASSERT(0);
        return FALSE;
    }
#endif
    return TRUE;

} /* end of glUnregisterP2P() */


/*----------------------------------------------------------------------------*/
/*!
 * \brief A function for stop p2p fsm immediate
 *
 * \param[in] prGlueInfo      Pointer to struct P_GLUE_INFO_T.
 *
 * \retval TRUE     The execution succeeds.
 * \retval FALSE   The execution failed.
 */
/*----------------------------------------------------------------------------*/
BOOLEAN
p2pStopImmediate(
    P_GLUE_INFO_T prGlueInfo
    )
{
//    P_ADAPTER_T prAdapter = NULL;
//    P_MSG_P2P_FUNCTION_SWITCH_T prFuncSwitch;

    ASSERT(prGlueInfo);

//    prAdapter = prGlueInfo->prAdapter;
//    ASSERT(prAdapter);

    /* 1. stop TX queue */
    netif_tx_stop_all_queues(prGlueInfo->prP2PInfo->prDevHandler);


#if 0
    /* 2. switch P2P-FSM off */
    /* 2.1 allocate for message */
    prFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T) cnmMemAlloc(
            prAdapter,
            RAM_TYPE_MSG,
            sizeof(MSG_P2P_FUNCTION_SWITCH_T));

    if (!prFuncSwitch) {
        ASSERT(0); // Can't trigger P2P FSM
        printk(KERN_ALERT DRV_NAME "Allocate for p2p mesasage FAILED\n");
        //return -ENOMEM;
    }

    /* 2.2 fill message */
    prFuncSwitch->rMsgHdr.eMsgId = MID_MNY_P2P_FUN_SWITCH;
    prFuncSwitch->fgIsFuncOn = FALSE;

    /* 2.3 send message */
    mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prFuncSwitch,
            MSG_SEND_METHOD_UNBUF);

#endif

    /* 3. stop queue and turn off carrier */
    prGlueInfo->prP2PInfo->eState = PARAM_MEDIA_STATE_DISCONNECTED;

    return TRUE;
} /* end of p2pStop() */


/* Net Device Hooks */
/*----------------------------------------------------------------------------*/
/*!
 * \brief A function for net_device open (ifup)
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \retval 0     The execution succeeds.
 * \retval < 0   The execution failed.
 */
/*----------------------------------------------------------------------------*/
static int
p2pOpen(
    IN struct net_device *prDev
    )
{
//    P_GLUE_INFO_T prGlueInfo = NULL;
//    P_ADAPTER_T prAdapter = NULL;
//    P_MSG_P2P_FUNCTION_SWITCH_T prFuncSwitch;

    ASSERT(prDev);

#if 0   // Move after device name set. (mtk_p2p_set_local_dev_info)
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    /* 1. switch P2P-FSM on */
    /* 1.1 allocate for message */
    prFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T) cnmMemAlloc(prAdapter,
            RAM_TYPE_MSG,
            sizeof(MSG_P2P_FUNCTION_SWITCH_T));

    if (!prFuncSwitch) {
        ASSERT(0); // Can't trigger P2P FSM
        return -ENOMEM;
    }

    /* 1.2 fill message */
    prFuncSwitch->rMsgHdr.eMsgId = MID_MNY_P2P_FUN_SWITCH;
    prFuncSwitch->fgIsFuncOn = TRUE;

    /* 1.3 send message */
    mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prFuncSwitch,
            MSG_SEND_METHOD_BUF);
#endif

    /* 2. carrier on & start TX queue */
    netif_carrier_on(prDev);
    netif_tx_start_all_queues(prDev);

    return 0; /* success */
} /* end of p2pOpen() */


/*----------------------------------------------------------------------------*/
/*!
 * \brief A function for net_device stop (ifdown)
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \retval 0     The execution succeeds.
 * \retval < 0   The execution failed.
 */
/*----------------------------------------------------------------------------*/
static int
p2pStop(
    IN struct net_device *prDev
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
 //   P_ADAPTER_T prAdapter = NULL;
//    P_MSG_P2P_FUNCTION_SWITCH_T prFuncSwitch;
	P_GL_P2P_INFO_T prGlueP2pInfo = (P_GL_P2P_INFO_T)NULL;

    struct cfg80211_scan_request *prScanRequest = NULL;
    GLUE_SPIN_LOCK_DECLARATION();
    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

	prGlueP2pInfo = prGlueInfo->prP2PInfo;
	ASSERT(prGlueP2pInfo);

	/* CFG80211 down */
		GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
		if(prGlueP2pInfo->prScanRequest != NULL) {
			prScanRequest = prGlueP2pInfo->prScanRequest;
			prGlueP2pInfo->prScanRequest = NULL;
		}
		GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_NET_DEV);
	
		if(prScanRequest) {
			cfg80211_scan_done(prScanRequest, TRUE);
		}
	
#if 0

    /* 1. stop TX queue */
    netif_tx_stop_all_queues(prDev);

    /* 2. switch P2P-FSM off */
    /* 2.1 allocate for message */
    prFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T) cnmMemAlloc(prAdapter,
            RAM_TYPE_MSG,
            sizeof(MSG_P2P_FUNCTION_SWITCH_T));

    if (!prFuncSwitch) {
        ASSERT(0); // Can't trigger P2P FSM
        return -ENOMEM;
    }

    /* 2.2 fill message */
    prFuncSwitch->rMsgHdr.eMsgId = MID_MNY_P2P_FUN_SWITCH;
    prFuncSwitch->fgIsFuncOn = FALSE;

    /* 2.3 send message */
    mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prFuncSwitch,
            MSG_SEND_METHOD_BUF);
#endif
    /* 3. stop queue and turn off carrier */
    prGlueInfo->prP2PInfo->eState = PARAM_MEDIA_STATE_DISCONNECTED;

    netif_tx_stop_all_queues(prDev);
    if(netif_carrier_ok(prDev)) {
        netif_carrier_off(prDev);
    }

    return 0;
} /* end of p2pStop() */


/*----------------------------------------------------------------------------*/
/*!
 * \brief A method of struct net_device, to get the network interface statistical
 *        information.
 *
 * Whenever an application needs to get statistics for the interface, this method
 * is called. This happens, for example, when ifconfig or netstat -i is run.
 *
 * \param[in] prDev      Pointer to struct net_device.
 *
 * \return net_device_stats buffer pointer.
 */
/*----------------------------------------------------------------------------*/
struct net_device_stats *
p2pGetStats (
    IN struct net_device *prDev
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));

#if 0 // frog temp fix
    //@FIXME
    //prDev->stats.rx_packets = 0;
    //prDev->stats.tx_packets = 0;
    prDev->stats.tx_errors = 0;
    prDev->stats.rx_errors = 0;
    //prDev->stats.rx_bytes = 0;
    //prDev->stats.tx_bytes = 0;
    prDev->stats.multicast = 0;

    return &prDev->stats;

#else
    //prGlueInfo->prP2PInfo->rNetDevStats.rx_packets = 0;
    //prGlueInfo->prP2PInfo->rNetDevStats.tx_packets = 0;
    prGlueInfo->prP2PInfo->rNetDevStats.tx_errors  = 0;
    prGlueInfo->prP2PInfo->rNetDevStats.rx_errors  = 0;
    //prGlueInfo->prP2PInfo->rNetDevStats.rx_bytes   = 0;
    //prGlueInfo->prP2PInfo->rNetDevStats.tx_bytes   = 0;
    //prGlueInfo->prP2PInfo->rNetDevStats.rx_errors  = 0;
    prGlueInfo->prP2PInfo->rNetDevStats.multicast  = 0;

    return &prGlueInfo->prP2PInfo->rNetDevStats;
#endif
} /* end of p2pGetStats() */




static void
p2pSetMulticastList (
    IN struct net_device *prDev
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;

    prGlueInfo = (NULL != prDev) ? *((P_GLUE_INFO_T *) netdev_priv(prDev)) : NULL;

    ASSERT(prDev);
    ASSERT(prGlueInfo);
    if (!prDev || !prGlueInfo) {
        printk(KERN_WARNING DRV_NAME" abnormal dev or skb: prDev(0x%p), prGlueInfo(0x%p)\n", prDev, prGlueInfo);
        return;
    }


    g_P2pPrDev = prDev;

    //4  Mark HALT, notify main thread to finish current job
    prGlueInfo->u4Flag |= GLUE_FLAG_SUB_MOD_MULTICAST;
    /* wake up main thread */
    wake_up_interruptible(&prGlueInfo->waitq);

} /* p2pSetMulticastList */


/*----------------------------------------------------------------------------*/
/*!
 * \brief This function is to set multicast list and set rx mode.
 *
 * \param[in] prDev  Pointer to struct net_device
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
void
mtk_p2p_wext_set_Multicastlist (
    P_GLUE_INFO_T prGlueInfo
    )
{
    UINT_32 u4SetInfoLen = 0;
    struct net_device *prDev = g_P2pPrDev;

    prGlueInfo = (NULL != prDev) ? *((P_GLUE_INFO_T *) netdev_priv(prDev)) : NULL;

    ASSERT(prDev);
    ASSERT(prGlueInfo);
    if (!prDev || !prGlueInfo) {
        printk(KERN_WARNING DRV_NAME" abnormal dev or skb: prDev(0x%p), prGlueInfo(0x%p)\n", prDev, prGlueInfo);
        return;
    }


    if (prDev->flags & IFF_PROMISC) {
        prGlueInfo->prP2PInfo->u4PacketFilter |= PARAM_PACKET_FILTER_PROMISCUOUS;
    }

    if (prDev->flags & IFF_BROADCAST) {
        prGlueInfo->prP2PInfo->u4PacketFilter |= PARAM_PACKET_FILTER_BROADCAST;
    }

    if (prDev->flags & IFF_MULTICAST) {
        if ((prDev->flags & IFF_ALLMULTI) ||
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
                (netdev_mc_count(prDev) > MAX_NUM_GROUP_ADDR)) {
#else
                (prDev->mc_count > MAX_NUM_GROUP_ADDR)) {
#endif
            prGlueInfo->prP2PInfo->u4PacketFilter |= PARAM_PACKET_FILTER_ALL_MULTICAST;
        }
        else {
            prGlueInfo->prP2PInfo->u4PacketFilter |= PARAM_PACKET_FILTER_MULTICAST;
        }
    }

    if (prGlueInfo->prP2PInfo->u4PacketFilter & PARAM_PACKET_FILTER_MULTICAST) {
        /* Prepare multicast address list */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
        struct netdev_hw_addr *ha;
#else
        struct dev_mc_list *prMcList;
#endif
        UINT_32 i = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
                netdev_for_each_mc_addr(ha, prDev) {
                    if(i < MAX_NUM_GROUP_ADDR) {
                        COPY_MAC_ADDR(&(prGlueInfo->prP2PInfo->aucMCAddrList[i]), ha->addr);
                        i++;
                    }
                }
#else
                for (i = 0, prMcList = prDev->mc_list;
                     (prMcList) && (i < prDev->mc_count) && (i < MAX_NUM_GROUP_ADDR);
                     i++, prMcList = prMcList->next) {
                    COPY_MAC_ADDR(&(prGlueInfo->prP2PInfo->aucMCAddrList[i]), prMcList->dmi_addr);
                }
#endif

                DBGLOG(P2P, TRACE, ("SEt Multicast Address List\n"));

                if (i >= MAX_NUM_GROUP_ADDR) {
                    return;
                }
                wlanoidSetP2PMulticastList(prGlueInfo->prAdapter,
                                &(prGlueInfo->prP2PInfo->aucMCAddrList[0]),
                                (i * ETH_ALEN),
                                &u4SetInfoLen);

    }

    return;
} /* end of p2pSetMulticastList() */


/*----------------------------------------------------------------------------*/
/*!
 * * \brief This function is TX entry point of NET DEVICE.
 * *
 * * \param[in] prSkb  Pointer of the sk_buff to be sent
 * * \param[in] prDev  Pointer to struct net_device
 * *
 * * \retval NETDEV_TX_OK - on success.
 * * \retval NETDEV_TX_BUSY - on failure, packet will be discarded by upper layer.
 * */
/*----------------------------------------------------------------------------*/
int
p2pHardStartXmit(
    IN struct sk_buff *prSkb,
    IN struct net_device *prDev
    )
{
    P_QUE_ENTRY_T prQueueEntry = NULL;
    P_QUE_T prTxQueue = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    UINT_16 u2QueueIdx = 0;

    GLUE_SPIN_LOCK_DECLARATION();

    ASSERT(prSkb);
    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));

    if (prGlueInfo->u4Flag & GLUE_FLAG_HALT) {
        printk(KERN_INFO DRV_NAME"GLUE_FLAG_HALT skip tx\n");
        dev_kfree_skb(prSkb);
        return NETDEV_TX_OK;
    }

    // mark as P2P packets
    GLUE_SET_PKT_FLAG_P2P(prSkb);
#if CFG_ENABLE_PKT_LIFETIME_PROFILE    
    GLUE_SET_PKT_ARRIVAL_TIME(prSkb, kalGetTimeTick());    
#endif

    prQueueEntry = (P_QUE_ENTRY_T) GLUE_GET_PKT_QUEUE_ENTRY(prSkb);
    prTxQueue = &prGlueInfo->rTxQueue;

    if (wlanProcessSecurityFrame(prGlueInfo->prAdapter, (P_NATIVE_PACKET) prSkb) == FALSE) {

        u2QueueIdx = skb_get_queue_mapping(prSkb);
        ASSERT(u2QueueIdx < CFG_MAX_TXQ_NUM);

        if (u2QueueIdx >= CFG_MAX_TXQ_NUM) {
            printk(KERN_INFO DRV_NAME"Incorrect queue index, skip this frame\n");
            dev_kfree_skb(prSkb);
            return NETDEV_TX_OK;
        }
        GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);
        QUEUE_INSERT_TAIL(prTxQueue, prQueueEntry);
        GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);

        GLUE_INC_REF_CNT(prGlueInfo->i4TxPendingFrameNum);
        GLUE_INC_REF_CNT(prGlueInfo->ai4TxPendingFrameNumPerQueue[NETWORK_TYPE_P2P_INDEX][u2QueueIdx]);

        if (prGlueInfo->ai4TxPendingFrameNumPerQueue[NETWORK_TYPE_P2P_INDEX][u2QueueIdx] >= CFG_TX_STOP_NETIF_PER_QUEUE_THRESHOLD) {
            netif_stop_subqueue(prDev, u2QueueIdx);
        }
    }
    else {
        GLUE_INC_REF_CNT(prGlueInfo->i4TxPendingSecurityFrameNum);
    }

    kalSetEvent(prGlueInfo);

    /* Statistic usage. */
    prGlueInfo->prP2PInfo->rNetDevStats.tx_bytes += prSkb->len;
	prGlueInfo->prP2PInfo->rNetDevStats.tx_packets++;
    //prDev->stats.tx_packets++;

    return NETDEV_TX_OK;
} /* end of p2pHardStartXmit() */


/*----------------------------------------------------------------------------*/
/*!
 * \brief A method of struct net_device, a primary SOCKET interface to configure
 *        the interface lively. Handle an ioctl call on one of our devices.
 *        Everything Linux ioctl specific is done here. Then we pass the contents
 *        of the ifr->data to the request message handler.
 *
 * \param[in] prDev      Linux kernel netdevice
 *
 * \param[in] prIFReq    Our private ioctl request structure, typed for the generic
 *                       struct ifreq so we can use ptr to function
 *
 * \param[in] cmd        Command ID
 *
 * \retval WLAN_STATUS_SUCCESS The IOCTL command is executed successfully.
 * \retval OTHER The execution of IOCTL command is failed.
 */
/*----------------------------------------------------------------------------*/
int
p2pDoIOCTL(
    struct net_device *prDev,
    struct ifreq *prIFReq,
    int i4Cmd
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    int ret = 0;
    char *prExtraBuf = NULL;
    UINT_32 u4ExtraSize = 0;
    struct iwreq *prIwReq = (struct iwreq *)prIFReq;
    struct iw_request_info rIwReqInfo;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));

    if (prGlueInfo->u4ReadyFlag == 0) {
        // adapter not ready yet
        return -EINVAL;
    }

    // fill rIwReqInfo
    rIwReqInfo.cmd = (__u16)i4Cmd;
    rIwReqInfo.flags = 0;

    switch(i4Cmd) {
    case SIOCSIWENCODEEXT:
        /* Set Encryption Material after 4-way handshaking is done */
        if (prIwReq->u.encoding.pointer) {
            u4ExtraSize = prIwReq->u.encoding.length;
            prExtraBuf = kalMemAlloc(u4ExtraSize, VIR_MEM_TYPE);

            if (!prExtraBuf) {
                ret = -ENOMEM;
                break;
            }

            if (copy_from_user(prExtraBuf,
                        prIwReq->u.encoding.pointer,
                        prIwReq->u.encoding.length)) {
                ret = -EFAULT;
            }
        }
        else if (prIwReq->u.encoding.length != 0) {
            ret = -EINVAL;
            break;
        }

        if(ret == 0) {
            ret = mtk_p2p_wext_set_key(prDev, &rIwReqInfo, &(prIwReq->u), prExtraBuf);
        }

        kalMemFree(prExtraBuf, VIR_MEM_TYPE, u4ExtraSize);
        prExtraBuf = NULL;
        break;

    case SIOCSIWMLME:
        /* IW_MLME_DISASSOC used for disconnection*/
        if (prIwReq->u.data.length != sizeof(struct iw_mlme)) {
            printk(KERN_INFO "MLME buffer strange:%d\n", prIwReq->u.data.length);
            ret = -EINVAL;
            break;
        }

        if (!prIwReq->u.data.pointer) {
            ret = -EINVAL;
            break;
        }

        prExtraBuf = kalMemAlloc(sizeof(struct iw_mlme), VIR_MEM_TYPE);
        if (!prExtraBuf) {
            ret = - ENOMEM;
            break;
        }

        if (copy_from_user(prExtraBuf, prIwReq->u.data.pointer, sizeof(struct iw_mlme))) {
            ret = -EFAULT;
        }
        else {
            ret = mtk_p2p_wext_mlme_handler(prDev, &rIwReqInfo, &(prIwReq->u), prExtraBuf);
        }

        kalMemFree(prExtraBuf, VIR_MEM_TYPE, sizeof(struct iw_mlme));
        prExtraBuf = NULL;
        break;

    case SIOCGIWPRIV:
        /* This ioctl is used to list all IW privilege ioctls */
        ret = mtk_p2p_wext_get_priv(prDev, &rIwReqInfo, &(prIwReq->u), NULL);
        break;

    case SIOCGIWSCAN:
        ret = mtk_p2p_wext_discovery_results(prDev, &rIwReqInfo, &(prIwReq->u), NULL);
        break;

    case SIOCSIWAUTH:
        ret = mtk_p2p_wext_set_auth(prDev, &rIwReqInfo, &(prIwReq->u), NULL);
        break;

    case IOC_P2P_CFG_DEVICE:
    case IOC_P2P_PROVISION_COMPLETE:
    case IOC_P2P_START_STOP_DISCOVERY:
    case IOC_P2P_DISCOVERY_RESULTS:
    case IOC_P2P_WSC_BEACON_PROBE_RSP_IE:
    case IOC_P2P_CONNECT_DISCONNECT:
    case IOC_P2P_PASSWORD_READY:
    case IOC_P2P_GET_STRUCT:
    case IOC_P2P_SET_STRUCT:
    case IOC_P2P_GET_REQ_DEVICE_INFO:
        ret = rP2PIwPrivHandler[i4Cmd - SIOCIWFIRSTPRIV](prDev, &rIwReqInfo, &(prIwReq->u), (char *)&(prIwReq->u));
        break;
#if CFG_SUPPORT_P2P_RSSI_QUERY
    case SIOCGIWSTATS:
        ret = mtk_p2p_wext_get_rssi(prDev, &rIwReqInfo, &(prIwReq->u), NULL);
        break;
#endif
    default:
        ret = -ENOTTY;
    }

    return ret;
} /* end of p2pDoIOCTL() */


/*----------------------------------------------------------------------------*/
/*!
 * \brief To report the private supported IOCTLs table to user space.
 *
 * \param[in] prDev Net device requested.
 * \param[out] prIfReq Pointer to ifreq structure, content is copied back to
 *                  user space buffer in gl_iwpriv_table.
 *
 * \retval 0 For success.
 * \retval -E2BIG For user's buffer size is too small.
 * \retval -EFAULT For fail.
 *
 */
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_get_priv (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    struct iw_point *prData= (struct iw_point *)&wrqu->data;
    UINT_16 u2BufferSize = 0;

    ASSERT(prDev);

    u2BufferSize = prData->length;

    /* update our private table size */
    prData->length = (__u16)sizeof(rP2PIwPrivTable)/sizeof(struct iw_priv_args);

    if (u2BufferSize < prData->length) {
        return -E2BIG;
    }

    if (prData->length) {
        if (copy_to_user(prData->pointer, rP2PIwPrivTable, sizeof(rP2PIwPrivTable))) {
            return -EFAULT;
        }
    }

    return 0;
} /* end of mtk_p2p_wext_get_priv() */


/*----------------------------------------------------------------------------*/
/*!
 * \brief To indicate P2P-FSM for re-associate to the connecting device
 *
 * \param[in] prDev      Net device requested.
 * \param[inout] wrqu    Pointer to iwreq_data
 *
 * \retval 0 For success.
 * \retval -EFAULT For fail.
 *
 */
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_reconnect (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
#if 0
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_MSG_HDR_T prMsgHdr;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    prMsgHdr = (P_MSG_HDR_T) cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_HDR_T));
    if (!prMsgHdr) {
        ASSERT(0); // Can't trigger P2P FSM
        return -ENOMEM;
    }

    /* 1.2 fill message */

    DBGLOG(P2P, TRACE, ("mtk_p2p_wext_reconnect: P2P Reconnect\n"));

    /* 1.3 send message */
    mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prMsgHdr,
            MSG_SEND_METHOD_BUF);
#endif
    return 0;
} /* end of mtk_p2p_wext_reconnect() */




/*----------------------------------------------------------------------------*/
/*!
* \brief MLME command handler
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_mlme_handler(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
#if 0
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    struct iw_mlme *mlme = (struct iw_mlme *)extra;
    P_MSG_P2P_CONNECTION_ABORT_T prMsgP2PConnAbt = (P_MSG_P2P_CONNECTION_ABORT_T)NULL;
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);

    DBGLOG(P2P, TRACE, ("mtk_p2p_wext_mlme_handler:\n"));

    switch (mlme->cmd) {
    case IW_MLME_DISASSOC:
        prMsgP2PConnAbt = (P_MSG_HDR_T) cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_CONNECTION_ABORT_T));
        if (!prMsgP2PConnAbt) {
            ASSERT(0); // Can't trigger P2P FSM
            return -ENOMEM;
        }

        COPY_MAC_ADDR(prMsgP2PConnAbt->aucTargetID, mlme->addr.sa_data);

        prMsgP2PConnAbt->u2ReasonCode = mlme->reason_code;


        if (EQUAL_MAC_ADDR(prMsgP2PConnAbt->aucTargetID, prP2pBssInfo->aucOwnMacAddr)) {
            DBGLOG(P2P, TRACE, ("P2P Connection Abort:\n"));

            /* 1.2 fill message */
            prMsgP2PConnAbt->rMsgHdr.eMsgId = MID_MNY_P2P_CONNECTION_ABORT;
        }
        else {
            DBGLOG(P2P, TRACE, ("P2P Connection Pause:\n"));

            /* 1.2 fill message */
        }

        /* 1.3 send message */
        mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prMsgP2PConnAbt,
            MSG_SEND_METHOD_BUF);

        break;

    default:
        return -EOPNOTSUPP;
    }
#endif
    return 0;
} /* end of mtk_p2p_wext_mlme_handler() */


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_PROVISION_COMPLETE)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_provision_complete(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
#if 0
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    struct iw_point *prData= (struct iw_point *)&wrqu->data;
    P_MSG_HDR_T prMsgHdr;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    switch(prData->flags) {
    case P2P_PROVISIONING_SUCCESS:
        prMsgHdr = (P_MSG_HDR_T) cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_HDR_T));
        if (!prMsgHdr) {
            ASSERT(0); // Can't trigger P2P FSM
            return -ENOMEM;
        }

        /* 1.2 fill message */

        prGlueInfo->prP2PInfo->u4CipherPairwise = IW_AUTH_CIPHER_CCMP;

        /* 1.3 send message */
        mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prMsgHdr,
            MSG_SEND_METHOD_BUF);

        break;

    case P2P_PROVISIONING_FAIL:

        break;

    default:
        return -EOPNOTSUPP;
    }
#endif

    return 0;
} /* end of mtk_p2p_wext_set_provision_complete() */



/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_START_STOP_DISCOVERY)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_start_stop_discovery(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
#if 0
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    struct iw_point *prData= (struct iw_point *)&wrqu->data;
    P_IW_P2P_REQ_DEVICE_TYPE prReqDeviceType = (P_IW_P2P_REQ_DEVICE_TYPE) extra;
    UINT_8 au4IeBuf[MAX_IE_LENGTH];
    P_MSG_HDR_T prMsgHdr;
    P_MSG_P2P_DEVICE_DISCOVER_T prDiscoverMsg;
    P_P2P_CONNECTION_SETTINGS_T prConnSettings;
    UINT_8 aucNullAddr[] = NULL_MAC_ADDR;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    prConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

    if(prData->flags == P2P_STOP_DISCOVERY) {
        prMsgHdr = (P_MSG_HDR_T) cnmMemAlloc(prAdapter,
            RAM_TYPE_MSG,
            sizeof(MSG_HDR_T));

        if (!prMsgHdr) {
            ASSERT(0); // Can't trigger P2P FSM
            return -ENOMEM;
        }


        mboxSendMsg(prAdapter,
                MBOX_ID_0,
                (P_MSG_HDR_T) prMsgHdr,
                MSG_SEND_METHOD_BUF);
    }
    else if(prData->flags == P2P_START_DISCOVERY) {

        /* retrieve IE for Probe Response */
        if(prReqDeviceType->probe_rsp_len > 0) {
            if(prReqDeviceType->probe_rsp_len <= MAX_IE_LENGTH) {
                if(copy_from_user(prGlueInfo->prP2PInfo->aucWSCIE[2], prReqDeviceType->probe_rsp_ie, prReqDeviceType->probe_rsp_len)) {
                    return -EFAULT;
                }
                prGlueInfo->prP2PInfo->u2WSCIELen[2] = prReqDeviceType->probe_rsp_len;
            }
            else {
                return -E2BIG;
            }
        }

        /* retrieve IE for Probe Request */
        if(prReqDeviceType->probe_req_len > 0) {
            if(prReqDeviceType->probe_req_len <= MAX_IE_LENGTH) {
                if(copy_from_user(prGlueInfo->prP2PInfo->aucWSCIE[1], prReqDeviceType->probe_req_ie, prReqDeviceType->probe_req_len)) {
                    return -EFAULT;
                }
                prGlueInfo->prP2PInfo->u2WSCIELen[1] = prReqDeviceType->probe_req_len;
            }
            else {
                return -E2BIG;
            }
        }
        /* update IE for Probe Request */

        if(prReqDeviceType->scan_type == P2P_LISTEN) {
            /* update listening parameter */

            /* @TODO: update prConnSettings for Probe Response IE */
        }
        else {
            // indicate P2P-FSM with MID_MNY_P2P_DEVICE_DISCOVERY
            prDiscoverMsg = (P_MSG_P2P_DEVICE_DISCOVER_T) cnmMemAlloc(prAdapter,
                    RAM_TYPE_MSG,
                    sizeof(MSG_P2P_DEVICE_DISCOVER_T));

            if (!prDiscoverMsg) {
                ASSERT(0); // Can't trigger P2P FSM
                return -ENOMEM;
            }

            prDiscoverMsg->rMsgHdr.eMsgId = MID_MNY_P2P_DEVICE_DISCOVERY;
            prDiscoverMsg->u4DevDiscoverTime = 0; // unlimited
            prDiscoverMsg->fgIsSpecificType = TRUE;
            prDiscoverMsg->rTargetDeviceType.u2CategoryID = *(PUINT_16)(&(prReqDeviceType->pri_device_type[0]));
            prDiscoverMsg->rTargetDeviceType.u2SubCategoryID = *(PUINT_16)(&(prReqDeviceType->pri_device_type[6]));;
            COPY_MAC_ADDR(prDiscoverMsg->aucTargetDeviceID, aucNullAddr);

            /* @FIXME: parameter to be refined, where to pass IE buffer ? */
            mboxSendMsg(prAdapter,
                    MBOX_ID_0,
                    (P_MSG_HDR_T) prDiscoverMsg,
                    MSG_SEND_METHOD_BUF);
        }
    }
    else {
        return -EINVAL;
    }
#endif

    return 0;
} /* end of mtk_p2p_wext_start_stop_discovery() */



/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_SET_INT)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Setting parameters not support.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_invitation_request (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    int i4Status = 0;
#if 0
    P_ADAPTER_T prAdapter = (P_ADAPTER_T)NULL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    struct iw_point *prData = (struct iw_point*)&wrqu->data;
    P_IW_P2P_IOCTL_INVITATION_STRUCT prIoctlInvitation = (P_IW_P2P_IOCTL_INVITATION_STRUCT)NULL;

    do {
        if ((prDev == NULL) || (extra == NULL)) {
            ASSERT(FALSE);
            i4Status = -EINVAL;
            break;
        }


        prGlueInfo = *((P_GLUE_INFO_T *)netdev_priv(prDev));
        prIoctlInvitation = (P_IW_P2P_IOCTL_INVITATION_STRUCT)extra;

        if (prGlueInfo == NULL) {
            i4Status = -EINVAL;
            break;
        }


        prAdapter = prGlueInfo->prAdapter;

        if (prAdapter == NULL) {
            i4Status = -EINVAL;
            break;
        }


        if (prIoctlInvitation->ucReinvoke == 1) {
            // TODO: Set Group ID
            p2pFuncSetGroupID(prAdapter, prIoctlInvitation->aucGroupID, prIoctlInvitation->aucSsid, prIoctlInvitation->u4SsidLen);
        }

        else {
            P_MSG_P2P_INVITATION_REQUEST_T prMsgP2PInvitationReq = (P_MSG_P2P_INVITATION_REQUEST_T)NULL;

            // TODO: Do Invitation.
            prMsgP2PInvitationReq = (P_MSG_P2P_INVITATION_REQUEST_T) cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_INVITATION_REQUEST_T));
            if (!prMsgP2PInvitationReq) {
                ASSERT(0); // Can't trigger P2P FSM
                i4Status = -ENOMEM;
                break;
            }

            /* 1.2 fill message */
            kalMemCopy(prMsgP2PInvitationReq->aucDeviceID, prIoctlInvitation->aucDeviceID, MAC_ADDR_LEN);

            DBGLOG(P2P, TRACE, ("mtk_p2p_wext_invitation_request: P2P Invitation Req\n"));

            /* 1.3 send message */
            mboxSendMsg(prAdapter,
                    MBOX_ID_0,
                    (P_MSG_HDR_T)prMsgP2PInvitationReq,
                    MSG_SEND_METHOD_BUF);

        }



    } while (FALSE);
#endif

    return i4Status;

}
/* mtk_p2p_wext_invitation_request */


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_SET_INT)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Setting parameters not support.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_invitation_abort (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    int i4Status = 0;
#if 0
    P_ADAPTER_T prAdapter = (P_ADAPTER_T)NULL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    struct iw_point *prData = (struct iw_point*)&wrqu->data;
    P_IW_P2P_IOCTL_ABORT_INVITATION prIoctlInvitationAbort = (P_IW_P2P_IOCTL_ABORT_INVITATION)NULL;

    UINT_8 bssid[MAC_ADDR_LEN];

    do {
        if ((prDev == NULL) || (extra == NULL)) {
            ASSERT(FALSE);
            i4Status = -EINVAL;
            break;
        }


        prGlueInfo = *((P_GLUE_INFO_T *)netdev_priv(prDev));
        prIoctlInvitationAbort = (P_IW_P2P_IOCTL_ABORT_INVITATION)extra;

        if (prGlueInfo == NULL) {
            i4Status = -EINVAL;
            break;
        }


        prAdapter = prGlueInfo->prAdapter;

        if (prAdapter == NULL) {
            i4Status = -EINVAL;
            break;
        }
        else {
            P_MSG_P2P_INVITATION_REQUEST_T prMsgP2PInvitationAbort = (P_MSG_P2P_INVITATION_REQUEST_T)NULL;

            prMsgP2PInvitationAbort = (P_MSG_P2P_INVITATION_REQUEST_T) cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_INVITATION_REQUEST_T));

            if (!prMsgP2PInvitationAbort) {
                ASSERT(0); // Can't trigger P2P FSM
                i4Status = -ENOMEM;
                break;
            }


            /* 1.2 fill message */
            kalMemCopy(prMsgP2PInvitationAbort->aucDeviceID, prIoctlInvitationAbort->dev_addr, MAC_ADDR_LEN);

            DBGLOG(P2P, TRACE, ("mtk_p2p_wext_invitation_request: P2P Invitation Req\n"));

            /* 1.3 send message */
            mboxSendMsg(prAdapter,
                            MBOX_ID_0,
                            (P_MSG_HDR_T)prMsgP2PInvitationAbort,
                            MSG_SEND_METHOD_BUF);

        }


    } while (FALSE);
#endif

    return i4Status;

}
/* mtk_p2p_wext_invitation_abort */



/*----------------------------------------------------------------------------*/
/*!
 * \brief To override p2p interface address
 *
 * \param[in] prDev Net device requested.
 * \param[in] addr  Pointer to address
 *
 * \retval 0 For success.
 * \retval -E2BIG For user's buffer size is too small.
 * \retval -EFAULT For fail.
 *
 */
/*----------------------------------------------------------------------------*/
int
p2pSetMACAddress(
    IN struct net_device *prDev,
    void *addr
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    //@FIXME
    return eth_mac_addr(prDev, addr);
}







/*----------------------------------------------------------------------------*/
/*!
* \brief To set encryption cipher suite
*
* \param[in] prDev Net device requested.
* \param[out]
*
* \retval 0 Success.
* \retval -EINVAL Invalid parameter
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_auth (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    struct iw_param *prAuth = (struct iw_param *)wrqu;

    ASSERT(prDev);
    ASSERT(prAuth);
    if (FALSE == GLUE_CHK_PR2(prDev, prAuth)) {
        return -EINVAL;
    }

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));

    /* Save information to glue info and process later when ssid is set. */
    switch(prAuth->flags & IW_AUTH_INDEX) {
    case IW_AUTH_WPA_VERSION:
        break;
    case IW_AUTH_CIPHER_PAIRWISE:
        prGlueInfo->prP2PInfo->u4CipherPairwise = prAuth->value;
       break;
    case IW_AUTH_CIPHER_GROUP:
    case IW_AUTH_KEY_MGMT:
    case IW_AUTH_TKIP_COUNTERMEASURES:
    case IW_AUTH_DROP_UNENCRYPTED:
    case IW_AUTH_80211_AUTH_ALG:
    case IW_AUTH_WPA_ENABLED:
    case IW_AUTH_RX_UNENCRYPTED_EAPOL:
    case IW_AUTH_ROAMING_CONTROL:
    case IW_AUTH_PRIVACY_INVOKED:
    default:
        //@TODO
        break;
    }

    return 0;
} /* end of mtk_p2p_wext_set_auth() */


/*----------------------------------------------------------------------------*/
/*!
* \brief To set encryption cipher and key.
*
* \param[in] prDev Net device requested.
* \param[out] prIfReq Pointer to ifreq structure, content is copied back to
*                  user space buffer in gl_iwpriv_table.
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note Securiry information is stored in pEnc.
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_key(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    int ret = 0;
    struct iw_encode_ext *prIWEncExt;
    struct iw_point *prEnc;
    char *prExtraBuf = NULL;
    UINT_32 u4ExtraSize = 0;
    UINT_8 keyStructBuf[100];
    P_PARAM_REMOVE_KEY_T prRemoveKey = (P_PARAM_REMOVE_KEY_T) keyStructBuf;
    P_PARAM_KEY_T prKey = (P_PARAM_KEY_T) keyStructBuf;
    P_GLUE_INFO_T prGlueInfo;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));

    do {
        if (wrqu->encoding.pointer) {
            u4ExtraSize = wrqu->encoding.length;
            prExtraBuf = kalMemAlloc(u4ExtraSize, VIR_MEM_TYPE);

            if (!prExtraBuf) {
                ret = -ENOMEM;
                break;
            }

            if (copy_from_user(prExtraBuf,
                        wrqu->encoding.pointer,
                        wrqu->encoding.length)) {
                ret = -EFAULT;
                break;
            }
        }
        else if (wrqu->encoding.length != 0) {
            ret = -EINVAL;
            break;
        }

        prEnc =  &wrqu->encoding;
        prIWEncExt = (struct iw_encode_ext *) prExtraBuf;

        if (GLUE_CHK_PR3(prDev, prEnc, prExtraBuf) == TRUE) {
            memset(keyStructBuf, 0, sizeof(keyStructBuf));

            if ((prEnc->flags & IW_ENCODE_MODE) == IW_ENCODE_DISABLED) { // Key Removal
                prRemoveKey->u4Length = sizeof(*prRemoveKey);
                memcpy(prRemoveKey->arBSSID, prIWEncExt->addr.sa_data, 6);

                rStatus = kalIoctl(prGlueInfo,
                        wlanoidSetRemoveP2PKey,
                        prRemoveKey,
                        prRemoveKey->u4Length,
                        FALSE,
                        FALSE,
                        TRUE,
                        TRUE,
                        &u4BufLen);

                if(rStatus != WLAN_STATUS_SUCCESS)
                    ret = -EFAULT;
            }
            else {
                if(prIWEncExt->alg == IW_ENCODE_ALG_CCMP) {
                    /* KeyID */
                    prKey->u4KeyIndex = (prEnc->flags & IW_ENCODE_INDEX) ?
                        ((prEnc->flags & IW_ENCODE_INDEX) - 1) : 0;
                    if (prKey->u4KeyIndex <= 3) {
                        /* bit(31) and bit(30) are shared by pKey and pRemoveKey */
                        /* Tx Key Bit(31)*/
                        if (prIWEncExt->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
                            prKey->u4KeyIndex |= 0x1UL << 31;
                        }

                        /* Pairwise Key Bit(30) */
                        if (prIWEncExt->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
                            /* group key */
                        }
                        else {
                            /* pairwise key */
                            prKey->u4KeyIndex |= 0x1UL << 30;
                        }

                        /* Rx SC Bit(29) */
                        if (prIWEncExt->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
                            prKey->u4KeyIndex |= 0x1UL << 29;
                            memcpy(&prKey->rKeyRSC, prIWEncExt->rx_seq, IW_ENCODE_SEQ_MAX_SIZE);
                        }

                        /* BSSID */
                        memcpy(prKey->arBSSID, prIWEncExt->addr.sa_data, 6);
                        memcpy(prKey->aucKeyMaterial, prIWEncExt->key, prIWEncExt->key_len);

                        prKey->u4KeyLength = prIWEncExt->key_len;
                        prKey->u4Length = ((UINT_32)&(((P_PARAM_KEY_T)0)->aucKeyMaterial)) + prKey->u4KeyLength;

                        rStatus = kalIoctl(prGlueInfo,
                                wlanoidSetAddP2PKey,
                                prKey,
                                prKey->u4Length,
                                FALSE,
                                FALSE,
                                TRUE,
                                TRUE,
                                &u4BufLen);

                        if (rStatus != WLAN_STATUS_SUCCESS) {
                            ret = -EFAULT;
                        }
                    }
                    else {
                        ret = -EINVAL;
                    }
                }
                else {
                    ret = -EINVAL;
                }
            }
        }
        else
            ret = -EINVAL;

    } while(FALSE);

    if (prExtraBuf) {
        kalMemFree(prExtraBuf, VIR_MEM_TYPE, u4ExtraSize);
        prExtraBuf = NULL;
    }

    return ret;
} /* end of mtk_p2p_wext_set_key() */




/*----------------------------------------------------------------------------*/
/*!
* \brief set the p2p gc power mode
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_powermode(
    IN struct net_device *prNetDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    //printk("set_powermode = %d, value = %d\n", wrqu->power.disabled, wrqu->power.value);
    struct iw_param *prPower = (struct iw_param*)&wrqu->power;
#if 1
    PARAM_POWER_MODE ePowerMode;
    INT_32 i4PowerValue;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prPower);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prPower)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    //printk(KERN_INFO "wext_set_power value(%d) disabled(%d) flag(0x%x)\n",
    //  prPower->value, prPower->disabled, prPower->flags);

    if(prPower->disabled){
        ePowerMode = Param_PowerModeCAM;
    }
    else {
        i4PowerValue = prPower->value;
#if WIRELESS_EXT < 21
        i4PowerValue /= 1000000;
#endif
        if (i4PowerValue == 0) {
            ePowerMode = Param_PowerModeCAM;
        } else if (i4PowerValue == 1) {
            ePowerMode = Param_PowerModeMAX_PSP;
        } else if (i4PowerValue == 2) {
            ePowerMode = Param_PowerModeFast_PSP;
        }
        else {
            printk(KERN_DEBUG "%s(): unsupported power management mode value = %d.\n",
                __FUNCTION__,
                prPower->value);

            return -EINVAL;
    }
    }


    rStatus = kalIoctl(prGlueInfo,
        wlanoidSetP2pPowerSaveProfile,
        &ePowerMode,
        sizeof(ePowerMode),
        FALSE,
        FALSE,
        TRUE,
        TRUE,
        &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        //printk(KERN_INFO DRV_NAME"wlanoidSet802dot11PowerSaveProfile fail 0x%lx\n", rStatus);
        return -EFAULT;
    }

#endif

    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief get the p2p gc power mode
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_get_powermode(
    IN struct net_device *prNetDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    //printk("mtk_p2p_wext_get_powermode\n");
    //wrqu->power.disabled = 0;
    //wrqu->power.value = 1;

    struct iw_param *prPower = (struct iw_param*)&wrqu->power;
    PARAM_POWER_MODE ePowerMode;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prPower);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prPower)) {
        return -EINVAL;
    }

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));
    ASSERT(prGlueInfo);


#if 1
    rStatus = kalIoctl(prGlueInfo,
            wlanoidQueryP2pPowerSaveProfile,
            &ePowerMode,
            sizeof(ePowerMode),
            TRUE,
            FALSE,
            FALSE,
            TRUE,
            &u4BufLen);
#else
    rStatus = wlanQueryInformation(prGlueInfo->prAdapter,
        wlanoidQueryP2pPowerSaveProfile,
        &ePowerMode,
        sizeof(ePowerMode),
        &u4BufLen);
#endif

    prPower->value = 0;
    prPower->disabled = 1;

    if (Param_PowerModeCAM == ePowerMode) {
        prPower->value = 0;
        prPower->disabled = 1;
    }
    else if (Param_PowerModeMAX_PSP == ePowerMode ) {
        prPower->value = 1;
        prPower->disabled = 0;
    }
    else if (Param_PowerModeFast_PSP == ePowerMode ) {
        prPower->value = 2;
        prPower->disabled = 0;
    }

    prPower->flags = IW_POWER_PERIOD | IW_POWER_RELATIVE;
#if WIRELESS_EXT < 21
    prPower->value *= 1000000;
#endif


    return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_CFG_DEVICE)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_local_dev_info(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_IW_P2P_CFG_DEVICE_TYPE prDeviceCfg = (P_IW_P2P_CFG_DEVICE_TYPE) extra;
    P_P2P_CONNECTION_SETTINGS_T prConnSettings;
    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    //P_MSG_P2P_FUNCTION_SWITCH_T prFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T)NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    prConnSettings = prAdapter->rWifiVar.prP2PConnSettings;
    prP2pSpecificBssInfo = prAdapter->rWifiVar.prP2pSpecificBssInfo;

    // update connection settings for P2P-FSM
    // 1. update SSID
    if(prDeviceCfg->ssid_len > ELEM_MAX_LEN_SSID) {
        prConnSettings->ucSSIDLen = ELEM_MAX_LEN_SSID;
    }
    else {
        prConnSettings->ucSSIDLen = prDeviceCfg->ssid_len;
    }

    if(copy_from_user(prConnSettings->aucSSID, prDeviceCfg->ssid, prConnSettings->ucSSIDLen)) {
        return -EFAULT;
    }

    // 2. update device type (WPS IE)
    kalMemCopy(&(prConnSettings->rPrimaryDevTypeBE), &(prDeviceCfg->pri_device_type), sizeof(DEVICE_TYPE_T));
#if P2P_MAX_SUPPORTED_SEC_DEV_TYPE_COUNT
    kalMemCopy(&(prConnSettings->arSecondaryDevTypeBE[0]), &(prDeviceCfg->snd_device_type), sizeof(DEVICE_TYPE_T));
#endif

    // 3. update device name
    if(prDeviceCfg->device_name_len > WPS_ATTRI_MAX_LEN_DEVICE_NAME) {
        prConnSettings->ucDevNameLen = WPS_ATTRI_MAX_LEN_DEVICE_NAME;
    }
    else {
        prConnSettings->ucDevNameLen = prDeviceCfg->device_name_len;
    }
    if(copy_from_user(prConnSettings->aucDevName, prDeviceCfg->device_name, prConnSettings->ucDevNameLen)) {
        return -EFAULT;
    }

    // 4. update GO intent
    prConnSettings->ucGoIntent = prDeviceCfg->intend;


    /* Preferred channel bandwidth */
    prAdapter->rWifiVar.rConnSettings.uc2G4BandwidthMode =
        prDeviceCfg->ch_width ? CONFIG_BW_20_40M : CONFIG_BW_20M;
    prAdapter->rWifiVar.rConnSettings.uc5GBandwidthMode =
        prDeviceCfg->ch_width ? CONFIG_BW_20_40M : CONFIG_BW_20M;

#if 0
    /* 1. switch P2P-FSM on */
    /* 1.1 allocate for message */
    prFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T) cnmMemAlloc(prAdapter,
            RAM_TYPE_MSG,
            sizeof(MSG_P2P_FUNCTION_SWITCH_T));

    if (!prFuncSwitch) {
        ASSERT(0); // Can't trigger P2P FSM
        return -ENOMEM;
    }

    /* 1.2 fill message */
    prFuncSwitch->rMsgHdr.eMsgId = MID_MNY_P2P_FUN_SWITCH;
    prFuncSwitch->fgIsFuncOn = TRUE;

    /* 1.3 send message */
    mboxSendMsg(prAdapter,
                MBOX_ID_0,
                (P_MSG_HDR_T) prFuncSwitch,
                MSG_SEND_METHOD_BUF);
#endif
    return 0;
} /* end of mtk_p2p_wext_set_local_dev_info() */






/*----------------------------------------------------------------------------*/
/*!
 * \brief I/O Control handler for both
 *          IOC_P2P_START_STOP_DISCOVERY & SIOCGIWSCAN
 *
 * \param[in] prDev      Net device requested.
 * \param[inout] wrqu    Pointer to iwreq_data
 *
 * \retval 0 Success.
 * \retval -EFAULT Setting parameters to driver fail.
 * \retval -EOPNOTSUPP Key size not supported.
 *
 * \note
 */
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_discovery_results(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    struct iw_event iwe;
    char *current_ev = extra;
    UINT_32 i;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_ADAPTER_T prAdapter = NULL;
    P_P2P_INFO_T prP2PInfo = (P_P2P_INFO_T)NULL;
    P_EVENT_P2P_DEV_DISCOVER_RESULT_T prTargetResult = (P_EVENT_P2P_DEV_DISCOVER_RESULT_T)NULL;
    P_PARAM_VARIABLE_IE_T prDesiredIE = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    prP2PInfo = prAdapter->prP2pInfo;

    for(i = 0 ; i < prP2PInfo->u4DeviceNum ; i++) {
        prTargetResult = &prP2PInfo->arP2pDiscoverResult[i];

        /* SIOCGIWAP */
        iwe.cmd = SIOCGIWAP;
        iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
        memcpy(iwe.u.ap_addr.sa_data,
                prTargetResult->aucInterfaceAddr,
                6);

        current_ev = iwe_stream_add_event(info, current_ev,
                extra + IW_SCAN_MAX_DATA,
                &iwe, IW_EV_ADDR_LEN);


        /* SIOCGIWESSID */
        iwe.cmd = SIOCGIWESSID;
        iwe.u.data.flags = 1;
        iwe.u.data.length = prTargetResult->u2NameLength;

        current_ev = iwe_stream_add_point(info, current_ev,
                extra + IW_SCAN_MAX_DATA,
                &iwe, prTargetResult->aucName);

        /* IWEVGENIE for WPA IE */
        if(prTargetResult->u2IELength <= 600 && wextSrchDesiredWPAIE(prTargetResult->pucIeBuf,
                    prTargetResult->u2IELength,
                    0xDD,
                    (PUINT_8 *)&prDesiredIE)) {

            iwe.cmd = IWEVGENIE;
            iwe.u.data.flags = 1;
            iwe.u.data.length = 2 + (__u16)prDesiredIE->ucLength;

            current_ev = iwe_stream_add_point(info, current_ev,
                    extra + IW_SCAN_MAX_DATA,
                    &iwe, (char *)prDesiredIE);
        }

#if CFG_SUPPORT_WPS

        /* IWEVGENIE for WPS IE */
        if((prTargetResult->u2IELength <= 600) && wextSrchDesiredWPSIE(prTargetResult->pucIeBuf,
                    prTargetResult->u2IELength,
                    0xDD,
                    (PUINT_8 *)&prDesiredIE)) {

            iwe.cmd = IWEVGENIE;
            iwe.u.data.flags = 1;
            iwe.u.data.length = 2 + (__u16)prDesiredIE->ucLength;

            current_ev = iwe_stream_add_point(info, current_ev,
                    extra + IW_SCAN_MAX_DATA,
                    &iwe, (char *)prDesiredIE);
        }

#endif

        /* IWEVGENIE for RSN IE */
        if((prTargetResult->u2IELength <= 600) && wextSrchDesiredWPAIE(prTargetResult->pucIeBuf,
                    prTargetResult->u2IELength,
                    0x30,
                    (PUINT_8 *)&prDesiredIE)) {

            iwe.cmd = IWEVGENIE;
            iwe.u.data.flags = 1;
            iwe.u.data.length = 2 + (__u16)prDesiredIE->ucLength;

            current_ev = iwe_stream_add_point(info, current_ev,
                    extra + IW_SCAN_MAX_DATA,
                    &iwe, (char *)prDesiredIE);
        }

        /* IOC_P2P_GO_WSC_IE */
#if 1
        /* device capability */
        if (1) {
            UINT_8 data[40];

            iwe.cmd = IWEVCUSTOM;
            iwe.u.data.flags = 0;
            iwe.u.data.length = 8 + sizeof("p2p_cap=");

            snprintf(data, iwe.u.data.length, "p2p_cap=%02x%02x%02x%02x%c",
                prTargetResult->ucDeviceCapabilityBitmap, prTargetResult->ucGroupCapabilityBitmap,
                (UINT_8)prTargetResult->u2ConfigMethod, (UINT_8)(prTargetResult->u2ConfigMethod >> 8), '\0' );
                current_ev = iwe_stream_add_point(info, current_ev,
                        extra + IW_SCAN_MAX_DATA,
                        &iwe, (char *)data);

            //printk("%s\n", data);
            kalMemZero(data, 40);

            iwe.cmd = IWEVCUSTOM;
            iwe.u.data.flags = 0;
            iwe.u.data.length = 12 + sizeof("p2p_dev_type=");

            snprintf(data, iwe.u.data.length, "p2p_dev_type=%02x%02x%02x%02x%02x%02x%c",
                (UINT_8)prTargetResult->rPriDevType.u2CategoryID,(UINT_8)prTargetResult->rPriDevType.u2SubCategoryID,
                (UINT_8)prTargetResult->arSecDevType[0].u2CategoryID,(UINT_8)prTargetResult->arSecDevType[0].u2SubCategoryID,
                (UINT_8)prTargetResult->arSecDevType[1].u2CategoryID,(UINT_8)prTargetResult->arSecDevType[1].u2SubCategoryID,
                '\0');
                current_ev = iwe_stream_add_point(info, current_ev,
                        extra + IW_SCAN_MAX_DATA,
                        &iwe, (char *)data);
            //printk("%s\n", data);

			kalMemZero(data, 40);

            iwe.cmd = IWEVCUSTOM;
            iwe.u.data.flags = 0;
            iwe.u.data.length = 17 + sizeof("p2p_grp_bssid=");

            snprintf(data, iwe.u.data.length, "p2p_grp_bssid="MACSTR"%c",
                MAC2STR(prTargetResult->aucBSSID), '\0');
                current_ev = iwe_stream_add_point(info, current_ev,
                        extra + IW_SCAN_MAX_DATA,
                        &iwe, (char *)data);
            //printk("%s\n", data);

        }
#endif
    }

    /* Length of data */
    wrqu->data.length = (current_ev - extra);
    wrqu->data.flags = 0;

    return 0;
} /* end of mtk_p2p_wext_discovery_results() */


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_WSC_BEACON_PROBE_RSP_IE)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_wsc_ie(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_IW_P2P_HOSTAPD_PARAM prHostapdParam = (P_IW_P2P_HOSTAPD_PARAM)extra;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *)netdev_priv(prDev));
    ASSERT(prGlueInfo);

    if (prHostapdParam->len > 0) {
        if (prHostapdParam->len <= MAX_WSC_IE_LENGTH) {
            if (copy_from_user(prGlueInfo->prP2PInfo->aucWSCIE[0], prHostapdParam->data, prHostapdParam->len)) {
                return -EFAULT;
            }
            if (copy_from_user(prGlueInfo->prP2PInfo->aucWSCIE[2], prHostapdParam->data, prHostapdParam->len)) {
                return -EFAULT;
            }
        }
        else {
            return -E2BIG;
        }
    }

    prGlueInfo->prP2PInfo->u2WSCIELen[0] = prHostapdParam->len;
    prGlueInfo->prP2PInfo->u2WSCIELen[2] = prHostapdParam->len;

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    bssUpdateBeaconContent(prAdapter, NETWORK_TYPE_P2P_INDEX);

    //@TODO: send message to P2P-FSM

    return 0;
} /* end of mtk_p2p_wext_wsc_ie() */


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_CONNECT_DISCONNECT)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_connect_disconnect(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    struct iw_point *prData= (struct iw_point *)&wrqu->data;
//    P_IW_P2P_CONNECT_DEVICE prConnectDevice = (P_IW_P2P_CONNECT_DEVICE)extra;
//    P_MSG_HDR_T prMsgHdr;
//    P_MSG_P2P_CONNECTION_REQUEST_T prMsgP2PConnReq;
//    P_MSG_P2P_CONNECTION_ABORT_T prMsgP2PConnAbt;
//    UINT_8 aucBCAddr[] = BC_MAC_ADDR;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    if (prData->flags == P2P_CONNECT) {
#if 0
        // indicate P2P-FSM with MID_MNY_P2P_CONNECTION_REQ
        prMsgP2PConnReq = (P_MSG_P2P_CONNECTION_REQUEST_T) cnmMemAlloc(prAdapter,
            RAM_TYPE_MSG,
            sizeof(MSG_P2P_CONNECTION_REQUEST_T));

        if (!prMsgP2PConnReq) {
            ASSERT(0); // Can't trigger P2P FSM
            return -ENOMEM;
        }


        mboxSendMsg(prAdapter,
                MBOX_ID_0,
                (P_MSG_HDR_T) prMsgP2PConnReq,
                MSG_SEND_METHOD_BUF);
#endif
    }
    else if(prData->flags == P2P_DISCONNECT) {
#if 0
        // indicate P2P-FSM with MID_MNY_P2P_CONNECTION_ABORT
        prMsgP2PConnAbt = (P_MSG_HDR_T) cnmMemAlloc(prAdapter,
            RAM_TYPE_MSG,
            sizeof(MSG_P2P_CONNECTION_ABORT_T));

        if (!prMsgP2PConnAbt) {
            ASSERT(0); // Can't trigger P2P FSM
            return -ENOMEM;
        }

        COPY_MAC_ADDR(prMsgP2PConnAbt->aucTargetID, prConnectDevice->sta_addr);

        prMsgP2PConnAbt->rMsgHdr.eMsgId = MID_MNY_P2P_CONNECTION_ABORT;

        mboxSendMsg(prAdapter,
                MBOX_ID_0,
                (P_MSG_HDR_T) prMsgP2PConnAbt,
                MSG_SEND_METHOD_BUF);
#endif
    }
    else {
        return -EINVAL;
    }

    return 0;
} /* end of mtk_p2p_wext_connect_disconnect() */


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_PASSWORD_READY)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_password_ready(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_IW_P2P_PASSWORD_READY prPasswordReady = (P_IW_P2P_PASSWORD_READY)extra;
    P_P2P_CONNECTION_SETTINGS_T prConnSettings;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *)netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    prConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

    /* retrieve IE for Probe Request */
    if (prPasswordReady->probe_req_len > 0) {
        if (prPasswordReady->probe_req_len <= MAX_WSC_IE_LENGTH) {
            if (copy_from_user(prGlueInfo->prP2PInfo->aucWSCIE[1], prPasswordReady->probe_req_ie, prPasswordReady->probe_req_len)) {
                return -EFAULT;
            }
        }
        else {
            return -E2BIG;
        }
    }

    prGlueInfo->prP2PInfo->u2WSCIELen[1] = prPasswordReady->probe_req_len;

    /* retrieve IE for Probe Response */
    if (prPasswordReady->probe_rsp_len > 0) {
        if (prPasswordReady->probe_rsp_len <= MAX_WSC_IE_LENGTH) {
            if (copy_from_user(prGlueInfo->prP2PInfo->aucWSCIE[2], prPasswordReady->probe_rsp_ie, prPasswordReady->probe_rsp_len)) {
                return -EFAULT;
            }
        }
        else {
            return -E2BIG;
        }
    }

    prGlueInfo->prP2PInfo->u2WSCIELen[2] = prPasswordReady->probe_rsp_len;

    switch (prPasswordReady->active_config_method) {
      case 1:
          prConnSettings->u2LocalConfigMethod = WPS_ATTRI_CFG_METHOD_PUSH_BUTTON;
          break;
      case 2:
          prConnSettings->u2LocalConfigMethod = WPS_ATTRI_CFG_METHOD_KEYPAD;
          break;
      case 3:
          prConnSettings->u2LocalConfigMethod = WPS_ATTRI_CFG_METHOD_DISPLAY;
          break;
      default:
         break;
    }

    prConnSettings->fgIsPasswordIDRdy = TRUE;
    return 0;
} /* end of mtk_p2p_wext_password_ready() */


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_GET_REQ_DEVICE_INFO)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_request_dev_info(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_IW_P2P_DEVICE_REQ prDeviceReq = (P_IW_P2P_DEVICE_REQ)extra;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    // specify data length
    wrqu->data.length = sizeof(IW_P2P_DEVICE_REQ);

    // copy to upper-layer supplied buffer
    kalMemCopy(prDeviceReq->name, prGlueInfo->prP2PInfo->aucConnReqDevName, prGlueInfo->prP2PInfo->u4ConnReqNameLength);
    prDeviceReq->name_len = prGlueInfo->prP2PInfo->u4ConnReqNameLength;
    prDeviceReq->name[prDeviceReq->name_len]='\0';
    COPY_MAC_ADDR(prDeviceReq->device_addr, prGlueInfo->prP2PInfo->rConnReqPeerAddr);
    prDeviceReq->device_type = prGlueInfo->prP2PInfo->ucConnReqDevType;
    prDeviceReq->config_method = prGlueInfo->prP2PInfo->i4ConnReqConfigMethod;
    prDeviceReq->active_config_method = prGlueInfo->prP2PInfo->i4ConnReqActiveConfigMethod;

    return 0;
} /* end of mtk_p2p_wext_request_dev_info() */


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_GET_STRUCT)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_invitation_indicate(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_IW_P2P_IOCTL_INVITATION_INDICATE prInvIndicate = (P_IW_P2P_IOCTL_INVITATION_INDICATE)extra;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    // specify data length
    wrqu->data.length = sizeof(IW_P2P_IOCTL_INVITATION_INDICATE);

    // copy to upper-layer supplied buffer
    kalMemCopy(prInvIndicate->dev_name, prGlueInfo->prP2PInfo->aucConnReqDevName, prGlueInfo->prP2PInfo->u4ConnReqNameLength);
    kalMemCopy(prInvIndicate->group_bssid, prGlueInfo->prP2PInfo->rConnReqGroupAddr, MAC_ADDR_LEN);
    prInvIndicate->name_len = prGlueInfo->prP2PInfo->u4ConnReqNameLength;
    prInvIndicate->dev_name[prInvIndicate->name_len]='\0';
    COPY_MAC_ADDR(prInvIndicate->dev_addr, prGlueInfo->prP2PInfo->rConnReqPeerAddr);
    prInvIndicate->config_method = prGlueInfo->prP2PInfo->i4ConnReqConfigMethod;
    prInvIndicate->operating_channel = prGlueInfo->prP2PInfo->ucOperatingChnl;
    prInvIndicate->invitation_type = prGlueInfo->prP2PInfo->ucInvitationType;

    return 0;
} /* end of mtk_p2p_wext_invitation_indicate() */


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_GET_STRUCT)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_invitation_status(
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    P_IW_P2P_IOCTL_INVITATION_STATUS prInvStatus = (P_IW_P2P_IOCTL_INVITATION_STATUS)extra;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    // specify data length
    wrqu->data.length = sizeof(IW_P2P_IOCTL_INVITATION_STATUS);

    // copy to upper-layer supplied buffer
    prInvStatus->status_code = prGlueInfo->prP2PInfo->u4InvStatus;

    return 0;
} /* end of mtk_p2p_wext_invitation_status() */


/*----------------------------------------------------------------------------*/
/*!
* \brief indicate an event to supplicant for device found
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
*
* \retval TRUE  Success.
* \retval FALSE Failure
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
kalP2PIndicateFound(
    IN P_GLUE_INFO_T    prGlueInfo
    )
{
    union iwreq_data evt;
    UINT_8 aucBuffer[IW_CUSTOM_MAX];

    ASSERT(prGlueInfo);

    memset(&evt, 0, sizeof(evt));

    snprintf(aucBuffer, IW_CUSTOM_MAX-1, "P2P_DVC_FND");
    evt.data.length = strlen(aucBuffer);

    /* indicate IWEVP2PDVCFND event */
    wireless_send_event(prGlueInfo->prP2PInfo->prDevHandler,
            IWEVCUSTOM,
            &evt,
            aucBuffer);

    return FALSE;
} /* end of kalP2PIndicateFound() */

int
mtk_p2p_wext_set_network_address (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    //@TODO: invoke wlan_p2p functions
#if 0
                        rStatus = kalIoctl(prGlueInfo,
                                (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetP2pNetworkAddress,
                                prKey,
                                prKey->u4Length,
                                FALSE,
                                FALSE,
                                TRUE,
                                &u4BufLen);
#endif

    return 0;

}

int
mtk_p2p_wext_set_ps_profile (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    //@TODO: invoke wlan_p2p functions
#if 0
                        rStatus = kalIoctl(prGlueInfo,
                                (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetP2pPowerSaveProfile,
                                prKey,
                                prKey->u4Length,
                                FALSE,
                                FALSE,
                                TRUE,
                                &u4BufLen);
#endif

    return 0;

}

int
mtk_p2p_wext_set_pm_param (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    P_ADAPTER_T prAdapter = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prAdapter = prGlueInfo->prAdapter;
    ASSERT(prAdapter);

    //@TODO: invoke wlan_p2p functions
#if 0
                        rStatus = kalIoctl(prGlueInfo,
                                (PFN_OID_HANDLER_FUNC_REQ)wlanoidSetP2pPowerSaveProfile,
                                prKey,
                                prKey->u4Length,
                                FALSE,
                                FALSE,
                                TRUE,
                                &u4BufLen);
#endif

    return 0;

}



/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_SET_INT)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Setting parameters not support.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_start_formation (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    int i4Status = 0;
    P_ADAPTER_T prAdapter = (P_ADAPTER_T)NULL;
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
//    struct iw_point *prData = (struct iw_point*)&wrqu->data;
    P_IW_P2P_IOCTL_START_FORMATION prIoctlStartFormation = (P_IW_P2P_IOCTL_START_FORMATION)NULL;

    do {
        if ((prDev == NULL) || (extra == NULL)) {
            ASSERT(FALSE);
            i4Status = -EINVAL;
            break;
        }


        prGlueInfo = *((P_GLUE_INFO_T *)netdev_priv(prDev));
        prIoctlStartFormation = (P_IW_P2P_IOCTL_START_FORMATION)extra;

        if (prGlueInfo == NULL) {
            i4Status = -EINVAL;
            break;
        }


        prAdapter = prGlueInfo->prAdapter;

        if (prAdapter == NULL) {
            i4Status = -EINVAL;
            break;
        }


    } while (FALSE);

    return i4Status;

}
/* mtk_p2p_wext_start_formation */


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_SET_INT)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Setting parameters not support.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_int (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    int                         status = 0;
    UINT_32                     u4SubCmd = 0;
    P_GLUE_INFO_T               prGlueInfo = NULL;
    UINT_32                     index;
    INT_32                      value;
    PUINT_32                    pu4IntBuf;
    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
    UINT_32 u4Leng;

    ASSERT(prDev);
    ASSERT(wrqu);

    //printk("mtk_p2p_wext_set_int\n");
    pu4IntBuf = (PUINT_32) extra;

    if (FALSE == GLUE_CHK_PR2(prDev, wrqu)) {
        return -EINVAL;
    }

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    prP2pSpecificBssInfo = prGlueInfo->prAdapter->rWifiVar.prP2pSpecificBssInfo;
    prP2pConnSettings = prGlueInfo->prAdapter->rWifiVar.prP2PConnSettings;
    prP2pFsmInfo =  prGlueInfo->prAdapter->rWifiVar.prP2pFsmInfo;

    u4SubCmd = (UINT_32) wrqu->mode;
    index = pu4IntBuf[1];
    value = pu4IntBuf[2];

    printk("set parameter, u4SubCmd=%d idx=%d value=%lu\n", (INT_16)u4SubCmd, (INT_16)index, value);

    switch (u4SubCmd) {
      case PRIV_CMD_INT_P2P_SET:
        switch (index) {
          case 0: /* Listen CH */
            {
                UINT_8 ucSuggestChnl = 0;

                prP2pConnSettings->ucListenChnl = value;

              // 20110920 - frog: User configurations are placed in ConnSettings.
              if (rlmFuncFindAvailableChannel(prGlueInfo->prAdapter, value, &ucSuggestChnl, TRUE, TRUE)) {
                 prP2pSpecificBssInfo->ucListenChannel = value;
              }
              else {
                 prP2pSpecificBssInfo->ucListenChannel = ucSuggestChnl;
              }


              break;
            }
          case 1: /* P2p mode */
              break;
          case 4: /* Noa duration */
                prP2pSpecificBssInfo->rNoaParam.u4NoaDurationMs = value;
                // only to apply setting when setting NOA count
                //status = mtk_p2p_wext_set_noa_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rNoaParam);
              break;
          case 5: /* Noa interval */
                prP2pSpecificBssInfo->rNoaParam.u4NoaIntervalMs = value;
                // only to apply setting when setting NOA count
                //status = mtk_p2p_wext_set_noa_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rNoaParam);
              break;
          case 6: /* Noa count */
                prP2pSpecificBssInfo->rNoaParam.u4NoaCount = value;
                status = mtk_p2p_wext_set_noa_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rNoaParam);
              break;
          case 100: /* Oper CH */
              // 20110920 - frog: User configurations are placed in ConnSettings.
              prP2pConnSettings->ucOperatingChnl = value;
              break;
          case 101: /* Local config Method, for P2P SDK */
              //prP2pConnSettings->u2LocalConfigMethod;
              break;
          case 102: /* Sigma P2p reset */
              kalMemZero(prP2pConnSettings->aucTargetDevAddr, MAC_ADDR_LEN);
              //prP2pConnSettings->eConnectionPolicy = ENUM_P2P_CONNECTION_POLICY_AUTO;
              break;
          case 103: /* WPS MODE */
              kalP2PSetWscMode(prGlueInfo, value);
              break;
          case 104: /* P2p send persence, duration */
              break;
          case 105: /* P2p send persence, interval */
              break;
          case 106: /* P2P set sleep  */
                value = 1;
                kalIoctl(prGlueInfo,
                    wlanoidSetP2pPowerSaveProfile,
                    &value,
                    sizeof(value),
                    FALSE,
                    FALSE,
                    TRUE,
                    TRUE,
                    &u4Leng);
              break;
          case 107: /* P2P set opps, CTWindowl */
                prP2pSpecificBssInfo->rOppPsParam.u4CTwindowMs = value;
                status = mtk_p2p_wext_set_oppps_param(prDev, info, wrqu, (char *)&prP2pSpecificBssInfo->rOppPsParam);
              break;
          case 108: /* p2p_set_power_save */
                kalIoctl(prGlueInfo,
                    wlanoidSetP2pPowerSaveProfile,
                    &value,
                    sizeof(value),
                    FALSE,
                    FALSE,
                    TRUE,
                    TRUE,
                    &u4Leng);

              break;

          default:
              break;
        }
        break;
      default:
        break;
    }

    return status;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_SET_STRUCT)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_struct (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    int                         status = 0;
    UINT_32                     u4SubCmd = 0;
    P_GLUE_INFO_T               prGlueInfo = NULL;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = NULL;

    ASSERT(prDev);
    ASSERT(wrqu);

    if (FALSE == GLUE_CHK_PR2(prDev, wrqu)) {
        return -EINVAL;
    }

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    u4SubCmd = (UINT_32) wrqu->data.flags;

    kalMemZero(&prGlueInfo->prP2PInfo->aucOidBuf[0],
        sizeof(prGlueInfo->prP2PInfo->aucOidBuf));

    switch (u4SubCmd) {
    case PRIV_CMD_OID:
        if (copy_from_user(&(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                    wrqu->data.pointer,
                    wrqu->data.length)) {
            status = -EFAULT;
            break;
        }

        if (!kalMemCmp(&(prGlueInfo->prP2PInfo->aucOidBuf[0]), extra, wrqu->data.length)) {
            printk(KERN_INFO DRV_NAME"extra buffer is valid\n");
        }
        else {
            printk(KERN_INFO DRV_NAME"extra 0x%p\n", extra);
        }

        prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT) (&(prGlueInfo->prP2PInfo->aucOidBuf[0]));
        switch(prP2PReq->u4CmdId) {
        case P2P_CMD_ID_SEND_SD_RESPONSE:
            status = mtk_p2p_wext_send_service_discovery_response(prDev, info, wrqu, (char *)prP2PReq);
            break;

        case P2P_CMD_ID_SEND_SD_REQUEST:
            status = mtk_p2p_wext_send_service_discovery_request(prDev, info, wrqu, (char *)prP2PReq);
            break;

        case P2P_CMD_ID_TERMINATE_SD_PHASE:
            status = mtk_p2p_wext_terminate_service_discovery_phase(prDev, info, wrqu, (char *)prP2PReq);
            break;

        case P2P_CMD_ID_INVITATION:
            if (prP2PReq->inBufferLength == sizeof(IW_P2P_IOCTL_INVITATION_STRUCT)) {
//                status = mtk_p2p_wext_invitation_request(prDev, info, wrqu, (char *)(prP2PReq->aucBuffer));
            }
            break;

        case P2P_CMD_ID_INVITATION_ABORT:
            if (prP2PReq->inBufferLength == sizeof(IW_P2P_IOCTL_ABORT_INVITATION)) {
//                status = mtk_p2p_wext_invitation_abort(prDev, info, wrqu, (char *)(prP2PReq->aucBuffer));
            }
            break;

        case P2P_CMD_ID_START_FORMATION:
            if (prP2PReq->inBufferLength == sizeof(IW_P2P_IOCTL_START_FORMATION)) {
                status = mtk_p2p_wext_start_formation(prDev, info, wrqu, (char *)(prP2PReq->aucBuffer));
            }
            break;
        default:
            status = -EOPNOTSUPP;
        }

        break;
#if CFG_SUPPORT_ANTI_PIRACY
    case PRIV_SEC_CHECK_OID:
        if (wrqu->data.length > 256) {
            status = -EOPNOTSUPP;
            break;
        }
        if (copy_from_user(&(prGlueInfo->prP2PInfo->aucSecCheck[0]),
                    wrqu->data.pointer,
                    wrqu->data.length)) {
            status = -EFAULT;
            break;
        }

        if (!kalMemCmp(&(prGlueInfo->prP2PInfo->aucSecCheck[0]), extra, wrqu->data.length)) {
            printk(KERN_INFO DRV_NAME"extra buffer is valid\n");
        }
        else {
            printk(KERN_INFO DRV_NAME"extra 0x%p\n", extra);
        }
        prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT) (&(prGlueInfo->prP2PInfo->aucSecCheck[0]));

        switch(prP2PReq->u4CmdId) {
        case P2P_CMD_ID_SEC_CHECK:
            status = mtk_p2p_wext_set_sec_check_request(prDev, info, wrqu, (char *)prP2PReq);
            break;
	default:
            status = -EOPNOTSUPP;
        }
        break;
#endif
    case PRIV_CMD_P2P_VERSION:
        if (copy_from_user(&(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                    wrqu->data.pointer,
                    wrqu->data.length)) {
            status = -EFAULT;
            break;
        }

        if (!kalMemCmp(&(prGlueInfo->prP2PInfo->aucOidBuf[0]), extra, wrqu->data.length)) {
            printk(KERN_INFO DRV_NAME"extra buffer is valid\n");
        }
        else {
            printk(KERN_INFO DRV_NAME"extra 0x%p\n", extra);
        }

        prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT) (&(prGlueInfo->prP2PInfo->aucOidBuf[0]));
        switch (prP2PReq->u4CmdId) {
        case P2P_CMD_ID_P2P_VERSION:
            status = mtk_p2p_wext_set_p2p_version(prDev, info, wrqu, (char *)prP2PReq);
            break;
        default:
            status = -EOPNOTSUPP;
            break;
        }
        break;
    default:
        status = -EOPNOTSUPP;
        break;
    }

    return status;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler (IOC_P2P_GET_STRUCT)
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_get_struct (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    int                         status = 0;
    UINT_32                     u4SubCmd = 0;
    P_GLUE_INFO_T               prGlueInfo = NULL;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = NULL;

    ASSERT(prDev);
    ASSERT(wrqu);

    if (!prDev || !wrqu) {
        printk(KERN_INFO DRV_NAME "%s(): invalid param(0x%p, 0x%p)\n",
                __func__,
                prDev,
                wrqu);
        return -EINVAL;
    }

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    u4SubCmd = (UINT_32) wrqu->data.flags;

    kalMemZero(&(prGlueInfo->prP2PInfo->aucOidBuf[0]),
        sizeof(prGlueInfo->prP2PInfo->aucOidBuf));

    switch (u4SubCmd) {
    case PRIV_CMD_OID:
        if (copy_from_user(&(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                    wrqu->data.pointer,
                    sizeof(IW_P2P_TRANSPORT_STRUCT))) {
            printk(KERN_NOTICE "%s() copy_from_user oidBuf fail\n", __func__);
            return -EFAULT;
        }

        prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT) (&(prGlueInfo->prP2PInfo->aucOidBuf[0]));

        switch(prP2PReq->u4CmdId) {
        case P2P_CMD_ID_GET_SD_REQUEST:
            status = mtk_p2p_wext_get_service_discovery_request(prDev, info, wrqu, (char *)prP2PReq);
            break;

        case P2P_CMD_ID_GET_SD_RESPONSE:
            status = mtk_p2p_wext_get_service_discovery_response(prDev, info, wrqu, (char *)prP2PReq);
            break;

        case P2P_CMD_ID_INVITATION_INDICATE:
        {
            status = mtk_p2p_wext_invitation_indicate(prDev, info, wrqu, (char *)(prP2PReq->aucBuffer));
            prP2PReq->outBufferLength = wrqu->data.length;
            if (copy_to_user(wrqu->data.pointer,
                                    &(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                                    wrqu->data.length + OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
                printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
                return -EIO;
            }
            else {
                return 0;
            }
            break;
        }
        case P2P_CMD_ID_INVITATION_STATUS:
        {
            status = mtk_p2p_wext_invitation_status(prDev, info, wrqu, (char *)(prP2PReq->aucBuffer));
            prP2PReq->outBufferLength = wrqu->data.length;
            if (copy_to_user(wrqu->data.pointer,
                                &(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                                wrqu->data.length + OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
                printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
                return -EIO;
            }
            else {
                return 0;
            }
            break;
        }
        case P2P_CMD_ID_GET_CH_LIST:
        {
            UINT_16 i;
            UINT_8 NumOfChannel = 50;
            RF_CHANNEL_INFO_T aucChannelList[50];
            UINT_8 ucMaxChannelNum = 50;
            PUINT_8 pucChnlList = (PUINT_8)prP2PReq->aucBuffer;

            kalGetChnlList(prGlueInfo, BAND_NULL, ucMaxChannelNum, &NumOfChannel, aucChannelList);
            if (NumOfChannel > 50)
                NumOfChannel = 50;
            prP2PReq->outBufferLength = NumOfChannel;

            for (i=0; i<NumOfChannel; i++) {
#if 0
                // 20120208 frog: modify to avoid clockwork warning.
                prP2PReq->aucBuffer[i] = aucChannelList[i].ucChannelNum;
#else
                *pucChnlList = aucChannelList[i].ucChannelNum;
                pucChnlList++;
#endif
            }
            if(copy_to_user(wrqu->data.pointer,
                        &(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                        NumOfChannel + OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
                printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
                return -EIO;
            }
            else {
                return 0;
            }
            break;
        }

        case P2P_CMD_ID_GET_OP_CH:
        {
            prP2PReq->inBufferLength = 4;

            status = wlanoidQueryP2pOpChannel(prGlueInfo->prAdapter,
                                        prP2PReq->aucBuffer,
                                        prP2PReq->inBufferLength,
                                        &prP2PReq->outBufferLength);

            if (status == 0) { // WLAN_STATUS_SUCCESS
                if (copy_to_user(wrqu->data.pointer,
                        &(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                        prP2PReq->outBufferLength + OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
                    printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
                    return -EIO;
                }
            }
            else {
                if (copy_to_user(wrqu->data.pointer,
                        &(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                        OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
                    printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
                    return -EIO;
                }
            }
            break;
        }

        default:
            status = -EOPNOTSUPP;
        }

        break;
#if CFG_SUPPORT_ANTI_PIRACY
    case PRIV_SEC_CHECK_OID:
        if (wrqu->data.length > 256) {
            status = -EOPNOTSUPP;
            break;
        }
        if (copy_from_user(&(prGlueInfo->prP2PInfo->aucSecCheck[0]),
                    wrqu->data.pointer,
                    sizeof(IW_P2P_TRANSPORT_STRUCT))) {
            printk(KERN_NOTICE "%s() copy_from_user oidBuf fail\n", __func__);
            return -EFAULT;
        }

        prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT) (&(prGlueInfo->prP2PInfo->aucSecCheck[0]));

        switch(prP2PReq->u4CmdId) {
            case P2P_CMD_ID_SEC_CHECK:
                status = mtk_p2p_wext_get_sec_check_response(prDev, info, wrqu, (char *)prP2PReq);
                break;
            default:
                status = -EOPNOTSUPP;
        }
        break;
#endif
    case PRIV_CMD_P2P_VERSION:
        if (copy_from_user(&(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                    wrqu->data.pointer,
                    sizeof(IW_P2P_TRANSPORT_STRUCT))) {
            printk(KERN_NOTICE "%s() copy_from_user oidBuf fail\n", __func__);
            return -EFAULT;
        }

        prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT) (&(prGlueInfo->prP2PInfo->aucOidBuf[0]));

        switch (prP2PReq->u4CmdId) {
        case P2P_CMD_ID_P2P_VERSION:
            status = mtk_p2p_wext_get_p2p_version(prDev, info, wrqu, (char *)prP2PReq);
            break;
        default:
            status = -EOPNOTSUPP;
            break;
        }


        /* Copy queried data to user. */
        if (status == 0) {  // WLAN_STATUS_SUCCESS
            if(copy_to_user(wrqu->data.pointer,
                        &(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                        prP2PReq->outBufferLength + OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
                printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
                return -EIO;
            }
        }

        else {
            if(copy_to_user(wrqu->data.pointer,
                        &(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                        OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
                printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
                return -EIO;
            }
        }

        break;
    default:
        return -EOPNOTSUPP;
    }

    return status;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler for
*        getting service discovery request frame from driver
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_get_service_discovery_request (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4QueryInfoLen;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
            wlanoidGetP2PSDRequest,
            prP2PReq->aucBuffer,
            prP2PReq->outBufferLength,
            TRUE,
            FALSE,
            TRUE,
            TRUE,
            &u4QueryInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        prP2PReq->outBufferLength = u4QueryInfoLen;

        if(copy_to_user(wrqu->data.pointer,
                    &(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                    u4QueryInfoLen + OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
            printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
            return -EIO;
        }
        else {
            return 0;
        }
    }
}


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler for
*        getting service discovery response frame from driver
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_get_service_discovery_response (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4QueryInfoLen;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
            wlanoidGetP2PSDResponse,
            prP2PReq->aucBuffer,
            prP2PReq->outBufferLength,
            TRUE,
            FALSE,
            TRUE,
            TRUE,
            &u4QueryInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        prP2PReq->outBufferLength = u4QueryInfoLen;

        if(copy_to_user(wrqu->data.pointer,
                    &(prGlueInfo->prP2PInfo->aucOidBuf[0]),
                    u4QueryInfoLen + OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
            printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
            return -EIO;
        }
        else {
            return 0;
        }
    }
}


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler for
*        sending service discovery request frame
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_send_service_discovery_request (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4SetInfoLen;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSendP2PSDRequest,
            prP2PReq->aucBuffer,
            prP2PReq->inBufferLength,
            FALSE,
            FALSE,
            TRUE,
            TRUE,
            &u4SetInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        return 0;
    }
}


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler for
*        sending service discovery response frame
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_send_service_discovery_response (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4SetInfoLen;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSendP2PSDResponse,
            prP2PReq->aucBuffer,
            prP2PReq->inBufferLength,
            FALSE,
            FALSE,
            TRUE,
            TRUE,
            &u4SetInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        return 0;
    }
}

#if CFG_SUPPORT_ANTI_PIRACY
/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler for
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_sec_check_request (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4SetInfoLen;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetSecCheckRequest,
            prP2PReq->aucBuffer,
            prP2PReq->inBufferLength,
            FALSE,
            FALSE,
            TRUE,
            TRUE,
            &u4SetInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        return 0;
    }
}


/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler for
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_get_sec_check_response (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4QueryInfoLen;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    printk("mtk_p2p_wext_get_sec_check_response\n");
    rStatus = kalIoctl(prGlueInfo,
            wlanoidGetSecCheckResponse,
            prP2PReq->aucBuffer,
            prP2PReq->outBufferLength,
            TRUE,
            FALSE,
            TRUE,
            TRUE,
            &u4QueryInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        prP2PReq->outBufferLength = u4QueryInfoLen;

        if(copy_to_user(wrqu->data.pointer,
                    prP2PReq->aucBuffer,
                    u4QueryInfoLen + OFFSET_OF(IW_P2P_TRANSPORT_STRUCT, aucBuffer))) {
            printk(KERN_NOTICE "%s() copy_to_user() fail\n", __func__);
            return -EIO;
        }
        else {
            return 0;
        }
    }
}
#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler for
*        terminating service discovery phase
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_terminate_service_discovery_phase (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4SetInfoLen;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetP2PTerminateSDPhase,
            prP2PReq->aucBuffer,
            prP2PReq->inBufferLength,
            FALSE,
            FALSE,
            TRUE,
            TRUE,
            &u4SetInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        return 0;
    }
}

/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler for
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_noa_param (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4SetInfoLen;
    //P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_PARAM_CUSTOM_NOA_PARAM_STRUC_T prNoaParam = (P_PARAM_CUSTOM_NOA_PARAM_STRUC_T)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    printk("mtk_p2p_wext_set_noa_param\n");

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetNoaParam,
            prNoaParam, //prP2PReq->aucBuffer,
            sizeof(PARAM_CUSTOM_NOA_PARAM_STRUC_T),//prP2PReq->inBufferLength,
            FALSE,
            FALSE,
            TRUE,
            TRUE,
            &u4SetInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        return 0;
    }
}

/*----------------------------------------------------------------------------*/
/*!
* \brief P2P Private I/O Control handler for
*
* \param[in] prDev      Net device requested.
* \param[inout] wrqu    Pointer to iwreq_data
*
* \retval 0 Success.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
mtk_p2p_wext_set_oppps_param (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4SetInfoLen;
//    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_PARAM_CUSTOM_OPPPS_PARAM_STRUC_T prOppPsParam = (P_PARAM_CUSTOM_OPPPS_PARAM_STRUC_T)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

     printk("mtk_p2p_wext_set_oppps_param\n");

   rStatus = kalIoctl(prGlueInfo,
            wlanoidSetOppPsParam,
            prOppPsParam, //prP2PReq->aucBuffer,
            sizeof(PARAM_CUSTOM_OPPPS_PARAM_STRUC_T), //prP2PReq->inBufferLength,
            FALSE,
            FALSE,
            TRUE,
            TRUE,
            &u4SetInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        return 0;
    }
}


int
mtk_p2p_wext_set_p2p_version (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS    rStatus = WLAN_STATUS_SUCCESS;
    P_GLUE_INFO_T   prGlueInfo = NULL;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    UINT_32                     u4SetInfoLen;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
                        wlanoidSetP2pSupplicantVersion,
                        prP2PReq->aucBuffer,
                        prP2PReq->inBufferLength,
                        FALSE,
                        FALSE,
                        TRUE,
                        TRUE,
                        &u4SetInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        return rStatus;
    }


}
/* mtk_p2p_wext_set_p2p_version */

int
mtk_p2p_wext_get_p2p_version (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4QueryInfoLen;
    P_IW_P2P_TRANSPORT_STRUCT   prP2PReq = (P_IW_P2P_TRANSPORT_STRUCT)extra;
    P_GLUE_INFO_T               prGlueInfo = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    rStatus = kalIoctl(prGlueInfo,
                wlanoidQueryP2pVersion,
                prP2PReq->aucBuffer,
                prP2PReq->outBufferLength,
                TRUE,
                FALSE,
                TRUE,
                TRUE,
                &u4QueryInfoLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        return rStatus;
    }


} /* mtk_p2p_wext_get_p2p_version */

#if CFG_SUPPORT_P2P_RSSI_QUERY

int
mtk_p2p_wext_get_rssi (
    IN struct net_device *prDev,
    IN struct iw_request_info *info,
    IN OUT union iwreq_data *wrqu,
    IN OUT char *extra
    )
{
    WLAN_STATUS                 rStatus = WLAN_STATUS_SUCCESS;
    UINT_32                     u4QueryInfoLen;
    struct iw_point *prData= (struct iw_point *)&wrqu->data;
    UINT_16 u2BufferSize = 0;
    P_GLUE_INFO_T               prGlueInfo = NULL;
    INT_32 i4Rssi;
    struct iw_statistics *pStats = NULL;

    ASSERT(prDev);

    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
    ASSERT(prGlueInfo);

    if (!prGlueInfo) {
        rStatus = WLAN_STATUS_FAILURE;
        goto stat_out;
    }

    pStats = (struct iw_statistics *) (&(prGlueInfo->rP2pIwStats));

    rStatus = kalIoctl(prGlueInfo,
                wlanoidQueryP2pRssi,
                &i4Rssi,
                sizeof(i4Rssi),
                TRUE,
                TRUE,
                TRUE,
                TRUE,
                &u4QueryInfoLen);

    u2BufferSize = prData->length;

    if (u2BufferSize < sizeof(struct iw_statistics)) {
        return -E2BIG;
    }


    if (copy_to_user(prData->pointer, pStats, sizeof(struct iw_statistics))) {
        rStatus = WLAN_STATUS_FAILURE;
    }


stat_out:

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }
    else {
        return rStatus;
    }

} /* mtk_p2p_wext_get_rssi */

struct iw_statistics *
mtk_p2p_wext_get_wireless_stats (
    struct net_device *prDev
    )
{
    WLAN_STATUS rStatus = WLAN_STATUS_FAILURE;
    P_GLUE_INFO_T prGlueInfo = NULL;
    struct iw_statistics *pStats = NULL;
    INT_32 i4Rssi;
    UINT_32 bufLen = 0;

    prGlueInfo = *((P_GLUE_INFO_T *)netdev_priv(prDev));
     ASSERT(prGlueInfo);
    if (!prGlueInfo) {
        goto stat_out;
    }

    pStats = (struct iw_statistics *) (&(prGlueInfo->rP2pIwStats));

    if (!prDev || !netif_carrier_ok(prDev)) {
        /* network not connected */
        goto stat_out;
    }

    rStatus = kalIoctl(prGlueInfo,
        wlanoidQueryP2pRssi,
        &i4Rssi,
        sizeof(i4Rssi),
        TRUE,
        TRUE,
        TRUE,
        TRUE,
        &bufLen);

stat_out:
    return pStats;
} /* mtk_p2p_wext_get_wireless_stats */


#endif /* CFG_SUPPORT_P2P_RSSI_QUERY */


int
mtk_p2p_wext_set_txpow (
    IN struct net_device *prDev,
    IN struct iw_request_info *prIwrInfo,
    IN OUT union iwreq_data *prTxPow,
    IN char *pcExtra
    )
{
    P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T)NULL;
    P_ADAPTER_T prAdapter = (P_ADAPTER_T)NULL;
#if 0
    P_MSG_P2P_FUNCTION_SWITCH_T prMsgFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T)NULL;
#endif
    int i4Ret = 0;

    ASSERT(prDev);
    ASSERT(prTxPow);

    do {
        if ((!prDev) || (!prTxPow)) {
            i4Ret = -EINVAL;
            break;
        }


        prGlueInfo = *((P_GLUE_INFO_T *)netdev_priv(prDev));

        if (!prGlueInfo) {
            i4Ret = -EINVAL;
            break;
        }


        prAdapter = prGlueInfo->prAdapter;
#if 0
        prMsgFuncSwitch = (P_MSG_P2P_FUNCTION_SWITCH_T)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_P2P_FUNCTION_SWITCH_T));
        if (!prMsgFuncSwitch) {
            ASSERT(0);
            return -ENOMEM;
        }

        prMsgFuncSwitch->rMsgHdr.eMsgId = MID_MNY_P2P_FUN_SWITCH;

        if (prTxPow->disabled) {
            /* Dissolve. */
            prMsgFuncSwitch->fgIsFuncOn = FALSE;
        }
        else {

            /* Re-enable function. */
            prMsgFuncSwitch->fgIsFuncOn = TRUE;
        }

        /* 1.3 send message */
        mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prMsgFuncSwitch,
            MSG_SEND_METHOD_BUF);
#endif

    } while (FALSE);

    return i4Ret;
} /* mtk_p2p_wext_set_txpow */


