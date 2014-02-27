/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/include/gl_os.h#2 $
*/

/*! \file   gl_os.h
    \brief  List the external reference to OS for GLUE Layer.

    In this file we define the data structure - GLUE_INFO_T to store those objects
    we acquired from OS - e.g. TIMER, SPINLOCK, NET DEVICE ... . And all the
    external reference (header file, extern func() ..) to OS for GLUE Layer should
    also list down here.
*/



/*
** $Log: gl_os.h $
** 
** 08 20 2012 yuche.tsai
** NULL
** Fix possible KE issue.
** 
** 08 20 2012 yuche.tsai
** [ALPS00339327] [Rose][6575JB][BSP Package][Free Test][KE][WIFI]There is no response when you tap the turn off/on button,wait a minutes, the device will reboot automatically and "KE" will pop up.
** Fix possible KE when netlink operate mgmt frame register.
 *
 * 04 12 2012 terry.wu
 * NULL
 * Add AEE message support
 * 1) Show AEE warning(red screen) if SDIO access error occurs

 *
 * 03 02 2012 terry.wu
 * NULL
 * Enable CFG80211 Support.
 *
 * 01 05 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Adding the related ioctl / wlan oid function to set the Tx power cfg.
 *
 * 12 13 2011 cm.chang
 * [WCXRP00001136] [All Wi-Fi][Driver] Add wake lock for pending timer
 * Add wake lock if timer timeout value is smaller than 5 seconds
 *
 * 11 18 2011 yuche.tsai
 * NULL
 * CONFIG P2P support RSSI query, default turned off.
 *
 * 11 16 2011 yuche.tsai
 * NULL
 * Avoid using work thread.
 *
 * 11 11 2011 yuche.tsai
 * NULL
 * Fix work thread cancel issue.
 *
 * 10 12 2011 wh.su
 * [WCXRP00001036] [MT6620 Wi-Fi][Driver][FW] Adding the 802.11w code for MFP
 * adding the 802.11w related function and define .
 *
 * 09 29 2011 terry.wu
 * NULL
 * Show DRV_NAME by chip id.
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 03 29 2011 cp.wu
 * [WCXRP00000598] [MT6620 Wi-Fi][Driver] Implementation of interface for communicating with user space process for RESET_START and RESET_END events
 * implement kernel-to-userspace communication via generic netlink socket for whole-chip resetting mechanism
 *
 * 03 21 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * portability improvement
 *
 * 03 17 2011 chinglan.wang
 * [WCXRP00000570] [MT6620 Wi-Fi][Driver] Add Wi-Fi Protected Setup v2.0 feature
 * .
 *
 * 03 03 2011 jeffrey.chang
 * [WCXRP00000512] [MT6620 Wi-Fi][Driver] modify the net device relative functions to support the H/W multiple queue
 * support concurrent network
 *
 * 03 03 2011 jeffrey.chang
 * [WCXRP00000512] [MT6620 Wi-Fi][Driver] modify the net device relative functions to support the H/W multiple queue
 * modify net device relative functions to support multiple H/W queues
 *
 * 03 02 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * Add security check code.
 *
 * 02 21 2011 cp.wu
 * [WCXRP00000482] [MT6620 Wi-Fi][Driver] Simplify logic for checking NVRAM existence in driver domain
 * simplify logic for checking NVRAM existence only once.
 *
 * 02 16 2011 jeffrey.chang
 * NULL
 * Add query ipv4 and ipv6 address during early suspend and late resume
 *
 * 02 10 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Fix kernel API change issue.
 * Before ALPS 2.2 (2.2 included), kfifo_alloc() is
 * struct kfifo *kfifo_alloc(unsigned int size, gfp_t gfp_mask, spinlock_t *lock);
 * After ALPS 2.3, kfifo_alloc() is changed to
 * int kfifo_alloc(struct kfifo *fifo, unsigned int size, gfp_t gfp_mask);
 *
 * 02 09 2011 wh.su
 * [WCXRP00000433] [MT6620 Wi-Fi][Driver] Remove WAPI structure define for avoid P2P module with structure miss-align pointer issue
 * always pre-allio WAPI related structure for align p2p module.
 *
 * 02 09 2011 terry.wu
 * [WCXRP00000383] [MT6620 Wi-Fi][Driver] Separate WiFi and P2P driver into two modules
 * Halt p2p module init and exit until TxThread finished p2p register and unregister.
 *
 * 02 01 2011 cm.chang
 * [WCXRP00000415] [MT6620 Wi-Fi][Driver] Check if any memory leakage happens when uninitializing in DGB mode
 * .
 *
 * 01 27 2011 cm.chang
 * [WCXRP00000402] [MT6620 Wi-Fi][Driver] Enable MCR read/write by iwpriv by default
 * .
 *
 * 01 12 2011 cp.wu
 * [WCXRP00000357] [MT6620 Wi-Fi][Driver][Bluetooth over Wi-Fi] add another net device interface for BT AMP
 * implementation of separate BT_OVER_WIFI data path.
 *
 * 01 12 2011 cp.wu
 * [WCXRP00000356] [MT6620 Wi-Fi][Driver] fill mac header length for security frames 'cause hardware header translation needs such information
 * fill mac header length information for 802.1x frames.
 *
 * 01 11 2011 chinglan.wang
 * NULL
 * Modify to reslove the CR :[ALPS00028994] Use WEP security to connect Marvell 11N AP.  Connection establish successfully.
 * Use the WPS function to connect AP, the privacy bit always is set to 1.
 *
 * 01 10 2011 cp.wu
 * [WCXRP00000349] [MT6620 Wi-Fi][Driver] make kalIoctl() of linux port as a thread safe API to avoid potential issues due to multiple access
 * use mutex to protect kalIoctl() for thread safe.
 *
 * 01 05 2011 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface for supporting Wi-Fi Direct Service Discovery
 * ioctl implementations for P2P Service Discovery
 *
 * 11 04 2010 wh.su
 * [WCXRP00000164] [MT6620 Wi-Fi][Driver] Support the p2p random SSID
 * adding the p2p random ssid support.
 *
 * 10 18 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000086] [MT6620 Wi-Fi][Driver] The mac address is all zero at android
 * complete implementation of Android NVRAM access
 *
 * 09 28 2010 wh.su
 * NULL
 * [WCXRP00000069][MT6620 Wi-Fi][Driver] Fix some code for phase 1 P2P Demo.
 *
 * 09 23 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * add skeleton for NVRAM integration
 *
 * 09 13 2010 cp.wu
 * NULL
 * add waitq for poll() and read().
 *
 * 09 07 2010 wh.su
 * NULL
 * adding the code for beacon/probe req/ probe rsp wsc ie at p2p.
 *
 * 09 06 2010 wh.su
 * NULL
 * let the p2p can set the privacy bit at beacon and rsn ie at assoc req at key handshake state.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 09 01 2010 wh.su
 * NULL
 * adding the wapi support for integration test.
 *
 * 08 31 2010 kevin.huang
 * NULL
 * Use LINK LIST operation to process SCAN result
 *
 * 08 23 2010 cp.wu
 * NULL
 * revise constant definitions to be matched with implementation (original cmd-event definition is deprecated)
 *
 * 08 16 2010 cp.wu
 * NULL
 * P2P packets are now marked when being queued into driver, and identified later without checking MAC address
 *
 * 08 16 2010 cp.wu
 * NULL
 * revised implementation of Wi-Fi Direct io controls.
 *
 * 08 11 2010 cp.wu
 * NULL
 * 1) do not use in-stack variable for beacon updating. (for MAUI porting)
 * 2) extending scanning result to 64 instead of 48
 *
 * 08 06 2010 cp.wu
 * NULL
 * driver hook modifications corresponding to ioctl interface change.
 *
 * 08 03 2010 cp.wu
 * NULL
 * [Wi-Fi Direct] add framework for driver hooks
 *
 * 08 02 2010 jeffrey.chang
 * NULL
 * 1) modify tx service thread to avoid busy looping
 * 2) add spin lock declartion for linux build
 *
 * 07 23 2010 jeffrey.chang
 *
 * add new KAL api
 *
 * 07 22 2010 jeffrey.chang
 *
 * modify tx thread and remove some spinlock
 *
 * 07 19 2010 jeffrey.chang
 *
 * add security frame pending count
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
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
 * 05 07 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * prevent supplicant accessing driver during resume
 *
 * 05 07 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add basic framework for implementating P2P driver hook.
 *
 * 05 05 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * change variable names for multiple physical link to match with coding convention
 *
 * 04 27 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * identify BT Over Wi-Fi Security frame and mark it as 802.1X frame
 *
 * 04 27 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add multiple physical link support
 *
 * 04 27 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * 1) fix firmware download bug
 * 2) remove query statistics for acelerating firmware download
 *
 * 04 27 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * follow Linux's firmware framework, and remove unused kal API
 *
 * 04 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * surpress compiler warning
 *
 * 04 19 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * supporting power management
 *
 * 04 14 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * pvInformationBuffer and u4InformationBufferLength are no longer in glue
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * 2) command sequence number is now increased atomically
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * rWlanInfo should be placed at adapter rather than glue due to most operations
 *  *  *  *  *  *  *  *  *  * are done in adapter layer.
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * Tag the packet for QoS on Tx path
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * (1)deliver the kalOidComplete status to upper layer
 *  * (2) fix spin lock
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * add timeout check in the kalOidComplete
 *
 * 04 06 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * improve none-glue code portability
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * code refine: fgTestMode should be at adapter rather than glue due to the device/fw is also involved
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate direct access for prGlueInfo->fgIsCardRemoved in non-glue layer
 *
 * 03 30 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * emulate NDIS Pending OID facility
 *
 * 03 26 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * [WPD00003826] Initial import for Linux port
 * adding firmware download related data type
 *
 * 03 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) correct OID_802_11_CONFIGURATION with frequency setting behavior.
 *  *  *  * the frequency is used for adhoc connection only
 *  *  *  * 2) update with SD1 v0.9 CMD/EVENT documentation
 *
 * 03 25 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add Bluetooth-over-Wifi frame header check
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
**  \main\maintrunk.MT5921\30 2009-10-20 17:38:31 GMT mtk01090
**  Refine driver unloading and clean up procedure. Block requests, stop main thread and clean up queued requests, and then stop hw.
**  \main\maintrunk.MT5921\29 2009-10-08 10:33:33 GMT mtk01090
**  Avoid accessing private data of net_device directly. Replace with netdev_priv(). Add more checking for input parameters and pointers.
**  \main\maintrunk.MT5921\28 2009-09-28 20:19:26 GMT mtk01090
**  Add private ioctl to carry OID structures. Restructure public/private ioctl interfaces to Linux kernel.
**  \main\maintrunk.MT5921\27 2009-08-18 22:57:12 GMT mtk01090
**  Add Linux SDIO (with mmc core) support.
**  Add Linux 2.6.21, 2.6.25, 2.6.26.
**  Fix compile warning in Linux.
**  \main\maintrunk.MT5921\26 2009-07-06 21:42:25 GMT mtk01088
**  fixed the compiling error at linux
**  \main\maintrunk.MT5921\25 2009-07-06 20:51:46 GMT mtk01088
**  adding the wapi 1x ether type define
**  \main\maintrunk.MT5921\24 2009-06-23 23:19:18 GMT mtk01090
**  Add build option BUILD_USE_EEPROM and compile option CFG_SUPPORT_EXT_CONFIG for NVRAM support
**  \main\maintrunk.MT5921\23 2009-02-07 15:05:06 GMT mtk01088
**  add the privacy flag to ingo driver the supplicant selected ap's security
**  \main\maintrunk.MT5921\22 2009-02-05 15:34:09 GMT mtk01088
**  fixed the compiling error for using bits marco for only one parameter
**  \main\maintrunk.MT5921\21 2009-01-22 13:02:13 GMT mtk01088
**  data frame is or not 802.1x value share with tid, using the same reserved byte, provide the function to set and get
**  \main\maintrunk.MT5921\20 2008-10-24 12:04:16 GMT mtk01088
**  move the config.h from precomp.h to here for lint check
**  \main\maintrunk.MT5921\19 2008-09-22 23:19:02 GMT mtk01461
**  Update driver for code review
**  \main\maintrunk.MT5921\18 2008-09-05 17:25:13 GMT mtk01461
**  Update Driver for Code Review
**  \main\maintrunk.MT5921\17 2008-08-01 13:32:47 GMT mtk01084
**  Prevent redundent driver assertion in driver logic when BUS is detached
**  \main\maintrunk.MT5921\16 2008-05-30 14:41:43 GMT mtk01461
**  Remove WMM Assoc Flag in KAL
**  \main\maintrunk.MT5921\15 2008-05-29 14:16:25 GMT mtk01084
**  remoev un-used variable
**  \main\maintrunk.MT5921\14 2008-05-03 15:17:14 GMT mtk01461
**  Add Media Status variable in Glue Layer
**  \main\maintrunk.MT5921\13 2008-04-24 11:58:41 GMT mtk01461
**  change threshold to 256
**  \main\maintrunk.MT5921\12 2008-03-11 14:51:05 GMT mtk01461
**  Remove redundant GL_CONN_INFO_T
**  \main\maintrunk.MT5921\11 2008-01-07 15:07:41 GMT mtk01461
**  Adjust the netif stop threshold to 150
**  \main\maintrunk.MT5921\10 2007-11-26 19:43:46 GMT mtk01461
**  Add OS_TIMESTAMP macro
**
**  \main\maintrunk.MT5921\9 2007-11-07 18:38:38 GMT mtk01461
**  Move definition
**  \main\maintrunk.MT5921\8 2007-11-02 01:04:00 GMT mtk01461
**  Unify TX Path for Normal and IBSS Power Save + IBSS neighbor learning
** Revision 1.5  2007/07/12 11:04:28  MTK01084
** update macro to delay for ms order
**
** Revision 1.4  2007/07/05 07:25:34  MTK01461
** Add Linux initial code, modify doc, add 11BB, RF init code
**
** Revision 1.3  2007/06/27 02:18:51  MTK01461
** Update SCAN_FSM, Initial(Can Load Module), Proc(Can do Reg R/W), TX API
**
** Revision 1.2  2007/06/25 06:16:24  MTK01461
** Update illustrations, gl_init.c, gl_kal.c, gl_kal.h, gl_os.h and RX API
**
*/

#ifndef _GL_OS_H
#define _GL_OS_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
/*------------------------------------------------------------------------------
 * Flags for LINUX(OS) dependent
 *------------------------------------------------------------------------------
 */
#define CFG_MAX_WLAN_DEVICES                1 /* number of wlan card will coexist*/

#define CFG_MAX_TXQ_NUM                     4 /* number of tx queue for support multi-queue h/w  */


#define CFG_USE_SPIN_LOCK_BOTTOM_HALF       0 /* 1: Enable use of SPIN LOCK Bottom Half for LINUX
                                                 0: Disable - use SPIN LOCK IRQ SAVE instead */

#define CFG_TX_PADDING_SMALL_ETH_PACKET     0 /* 1: Enable - Drop ethernet packet if it < 14 bytes.
                                                             And pad ethernet packet with dummy 0 if it < 60 bytes.
                                                 0: Disable */

#define CFG_TX_STOP_NETIF_QUEUE_THRESHOLD   256 /* packets */

#define CFG_TX_STOP_NETIF_PER_QUEUE_THRESHOLD   512  /* packets */
#define CFG_TX_START_NETIF_PER_QUEUE_THRESHOLD  128  /* packets */


#define ETH_P_1X                            0x888E
#define IPTOS_PREC_OFFSET                   5
#define USER_PRIORITY_DEFAULT               0

#define ETH_WPI_1X                         0x88B4

#define ETH_HLEN                                14
#define ETH_TYPE_LEN_OFFSET                     12
#define ETH_P_IP                                0x0800
#define ETH_P_1X                                0x888E
#define ETH_P_PRE_1X                            0x88C7

#define IPVERSION                               4
#define IP_HEADER_LEN                           20

#define IPVH_VERSION_OFFSET                     4 // For Little-Endian
#define IPVH_VERSION_MASK                       0xF0
#define IPTOS_PREC_OFFSET                       5
#define IPTOS_PREC_MASK                         0xE0

#define SOURCE_PORT_LEN                         2
/* NOTE(Kevin): Without IP Option Length */
#define LOOK_AHEAD_LEN                          (ETH_HLEN + IP_HEADER_LEN + SOURCE_PORT_LEN)

/* 802.2 LLC/SNAP */
#define ETH_LLC_OFFSET                          (ETH_HLEN)
#define ETH_LLC_LEN                             3
#define ETH_LLC_DSAP_SNAP                       0xAA
#define ETH_LLC_SSAP_SNAP                       0xAA
#define ETH_LLC_CONTROL_UNNUMBERED_INFORMATION  0x03

/* Bluetooth SNAP */
#define ETH_SNAP_OFFSET                         (ETH_HLEN + ETH_LLC_LEN)
#define ETH_SNAP_LEN                            5
#define ETH_SNAP_BT_SIG_OUI_0                   0x00
#define ETH_SNAP_BT_SIG_OUI_1                   0x19
#define ETH_SNAP_BT_SIG_OUI_2                   0x58

#define BOW_PROTOCOL_ID_SECURITY_FRAME          0x0003


#if defined(MT6620)
    #define CHIP_NAME    "MT6620"
#elif defined(MT5931)
    #define CHIP_NAME    "MT5931"
#elif defined(MT6628)
    #define CHIP_NAME    "MT6628"
#endif

#define DRV_NAME "["CHIP_NAME"]: "

/* Define if target platform is Android.
 * It should already be defined in Android kernel source
 */
#ifndef CONFIG_ANDROID
#define CONFIG_ANDROID      0
#endif

/* for CFG80211 IE buffering mechanism */
#define CFG_CFG80211_IE_BUF_LEN     (512)

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include <linux/version.h>      /* constant of kernel version */

#include <linux/kernel.h>       /* bitops.h */

#include <linux/timer.h>        /* struct timer_list */
#include <linux/jiffies.h>      /* jiffies */
#include <linux/delay.h>        /* udelay and mdelay macro */

#if CONFIG_ANDROID
#include <linux/wakelock.h>
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 12)
#include <linux/irq.h>          /* IRQT_FALLING */
#include <linux/hardirq.h>      /*for in_interrupt*/
#endif

#include <linux/netdevice.h>    /* struct net_device, struct net_device_stats */
#include <linux/etherdevice.h>  /* for eth_type_trans() function */
#include <linux/wireless.h>     /* struct iw_statistics */
#include <linux/if_arp.h>
#include <linux/inetdevice.h>   /* struct in_device */

#include <linux/ip.h>           /* struct iphdr */

#include <linux/string.h>       /* for memcpy()/memset() function */
#include <linux/stddef.h>       /* for offsetof() macro */

#include <linux/proc_fs.h>      /* The proc filesystem constants/structures */

#include <linux/rtnetlink.h>    /* for rtnl_lock() and rtnl_unlock() */
#include <linux/kthread.h>      /* kthread_should_stop(), kthread_run() */
#include <asm/uaccess.h>        /* for copy_from_user() */
#include <linux/fs.h>           /* for firmware download */
#include <linux/vmalloc.h>

#include <linux/kfifo.h>        /* for kfifo interface */
#include <linux/cdev.h>         /* for cdev interface */

#include <linux/firmware.h>     /* for firmware download */

#if defined(_HIF_SDIO)
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#endif

#include <linux/random.h>

#include <linux/lockdep.h>

#include <asm/io.h>             /* readw and writew */

#if WIRELESS_EXT > 12
#include <net/iw_handler.h>
#endif

#include "version.h"
#include "config.h"

#if CFG_ENABLE_WIFI_DIRECT_CFG_80211
#include <linux/wireless.h>
#include <net/cfg80211.h>
#endif

#include <linux/module.h>

#include "gl_typedef.h"
#include "typedef.h"
#include "queue.h"
#include "gl_kal.h"
#if CFG_CHIP_RESET_SUPPORT
    #include "gl_rst.h"
#endif
#include "hif.h"


#include "debug.h"

#include "wlan_lib.h"
#include "wlan_oid.h"

#if CFG_ENABLE_AEE_MSG
#include <linux/aee.h>
#endif

extern BOOLEAN fgIsBusAccessFailed;

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define GLUE_FLAG_HALT          BIT(0)
#define GLUE_FLAG_INT           BIT(1)
#define GLUE_FLAG_OID           BIT(2)
#define GLUE_FLAG_TIMEOUT       BIT(3)
#define GLUE_FLAG_TXREQ         BIT(4)
#define GLUE_FLAG_SUB_MOD_INIT  BIT(5)
#define GLUE_FLAG_SUB_MOD_EXIT  BIT(6)
#define GLUE_FLAG_SUB_MOD_MULTICAST  BIT(7)
#define GLUE_FLAG_FRAME_FILTER      BIT(8)
#define GLUE_FLAG_HALT_BIT          (0)
#define GLUE_FLAG_INT_BIT           (1)
#define GLUE_FLAG_OID_BIT           (2)
#define GLUE_FLAG_TIMEOUT_BIT       (3)
#define GLUE_FLAG_TXREQ_BIT         (4)
#define GLUE_FLAG_SUB_MOD_INIT_BIT  (5)
#define GLUE_FLAG_SUB_MOD_EXIT_BIT  (6)
#define GLUE_FLAG_SUB_MOD_MULTICAST_BIT  (7)
#define GLUE_FLAG_FRAME_FILTER_BIT  (8)



#define GLUE_BOW_KFIFO_DEPTH        (1024)
//#define GLUE_BOW_DEVICE_NAME        "MT6620 802.11 AMP"
#define GLUE_BOW_DEVICE_NAME        "ampc0"


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef struct _GL_WPA_INFO_T {
    UINT_32 u4WpaVersion;
    UINT_32 u4KeyMgmt;
    UINT_32 u4CipherGroup;
    UINT_32 u4CipherPairwise;
    UINT_32 u4AuthAlg;
    BOOLEAN fgPrivacyInvoke;
#if CFG_SUPPORT_802_11W
    UINT_32 u4Mfp;
#endif
} GL_WPA_INFO_T, *P_GL_WPA_INFO_T;

typedef enum _ENUM_RSSI_TRIGGER_TYPE {
    ENUM_RSSI_TRIGGER_NONE,
    ENUM_RSSI_TRIGGER_GREATER,
    ENUM_RSSI_TRIGGER_LESS,
    ENUM_RSSI_TRIGGER_TRIGGERED,
    ENUM_RSSI_TRIGGER_NUM
} ENUM_RSSI_TRIGGER_TYPE;

#if CFG_ENABLE_WIFI_DIRECT
typedef enum _ENUM_SUB_MODULE_IDX_T {
    P2P_MODULE = 0,
    SUB_MODULE_NUM
} ENUM_SUB_MODULE_IDX_T;

typedef enum _ENUM_NET_REG_STATE_T {
    ENUM_NET_REG_STATE_UNREGISTERED,
    ENUM_NET_REG_STATE_REGISTERING,
    ENUM_NET_REG_STATE_REGISTERED,
    ENUM_NET_REG_STATE_UNREGISTERING,
    ENUM_NET_REG_STATE_NUM
} ENUM_NET_REG_STATE_T;

#endif

typedef struct _GL_IO_REQ_T {
    QUE_ENTRY_T             rQueEntry;
    //wait_queue_head_t       cmdwait_q;
    BOOL                    fgRead;
    BOOL                    fgWaitResp;
#if CFG_ENABLE_WIFI_DIRECT
    BOOL                    fgIsP2pOid;
#endif
    P_ADAPTER_T             prAdapter;
    PFN_OID_HANDLER_FUNC    pfnOidHandler;
    PVOID                   pvInfoBuf;
    UINT_32                 u4InfoBufLen;
    PUINT_32                pu4QryInfoLen;
    WLAN_STATUS             rStatus;
    UINT_32                 u4Flag;
} GL_IO_REQ_T, *P_GL_IO_REQ_T;

#if CFG_ENABLE_BT_OVER_WIFI
typedef struct _GL_BOW_INFO {
    BOOLEAN                 fgIsRegistered;
    dev_t                   u4DeviceNumber; /* dynamic device number */
//    struct kfifo            *prKfifo;       /* for buffering indicated events */
    struct kfifo                      rKfifo;   /* for buffering indicated events */
    spinlock_t              rSpinLock;      /* spin lock for kfifo */
    struct cdev             cdev;
    UINT_32                 u4FreqInKHz;    /* frequency */

    UINT_8                  aucRole[CFG_BOW_PHYSICAL_LINK_NUM];  /* 0: Responder, 1: Initiator */
    ENUM_BOW_DEVICE_STATE   aeState[CFG_BOW_PHYSICAL_LINK_NUM];
    PARAM_MAC_ADDRESS       arPeerAddr[CFG_BOW_PHYSICAL_LINK_NUM];

    wait_queue_head_t       outq;

    #if CFG_BOW_SEPARATE_DATA_PATH
    /* Device handle */
    struct net_device           *prDevHandler;
    BOOLEAN                     fgIsNetRegistered;
    #endif

} GL_BOW_INFO, *P_GL_BOW_INFO;
#endif

/*
* type definition of pointer to p2p structure
*/
typedef struct _GL_P2P_INFO_T   GL_P2P_INFO_T, *P_GL_P2P_INFO_T;

struct _GLUE_INFO_T {
    /* Device handle */
    struct net_device *prDevHandler;

    /* Device Index(index of arWlanDevInfo[]) */
    INT_32 i4DevIdx;

    /* Device statistics */
    struct net_device_stats rNetDevStats;

    /* Wireless statistics struct net_device */
    struct iw_statistics rIwStats;

    /* spinlock to sync power save mechanism */
    spinlock_t rSpinLock[SPIN_LOCK_NUM];

    /* semaphore for ioctl */
    struct semaphore ioctl_sem;

    UINT_32 u4Flag; /* GLUE_FLAG_XXX */
    UINT_32 u4PendFlag;
    //UINT_32 u4TimeoutFlag;
    UINT_32 u4OidCompleteFlag;
    UINT_32 u4ReadyFlag;  /* check if card is ready */

    /* Number of pending frames, also used for debuging if any frame is
     * missing during the process of unloading Driver.
     *
     * NOTE(Kevin): In Linux, we also use this variable as the threshold
     * for manipulating the netif_stop(wake)_queue() func.
     */
    INT_32             ai4TxPendingFrameNumPerQueue[4][CFG_MAX_TXQ_NUM];
    INT_32             i4TxPendingFrameNum;
    INT_32             i4TxPendingSecurityFrameNum;

    /* current IO request for kalIoctl */
    GL_IO_REQ_T         OidEntry;

    /* registry info*/
    REG_INFO_T rRegInfo;

    /* firmware */
    struct firmware     *prFw;

    /* Host interface related information */
    /* defined in related hif header file */
    GL_HIF_INFO_T       rHifInfo;

    /*! \brief wext wpa related information */
    GL_WPA_INFO_T       rWpaInfo;


    /* Pointer to ADAPTER_T - main data structure of internal protocol stack */
    P_ADAPTER_T         prAdapter;

#ifdef WLAN_INCLUDE_PROC
    struct proc_dir_entry *pProcRoot;
#endif /* WLAN_INCLUDE_PROC */

    /* Indicated media state */
    ENUM_PARAM_MEDIA_STATE_T eParamMediaStateIndicated;

    /* Device power state D0~D3 */
    PARAM_DEVICE_POWER_STATE ePowerState;

    struct completion rScanComp; /* indicate scan complete */
    struct completion rHaltComp; /* indicate main thread halt complete */
    struct completion rPendComp; /* indicate main thread halt complete */
#if CFG_ENABLE_WIFI_DIRECT
    struct completion rSubModComp; /*indicate sub module init or exit complete*/
#endif
    WLAN_STATUS             rPendStatus;

    QUE_T                   rTxQueue;


    /* OID related */
    QUE_T                   rCmdQueue;
    //PVOID                   pvInformationBuffer;
    //UINT_32                 u4InformationBufferLength;
    //PVOID                   pvOidEntry;
    //PUINT_8                 pucIOReqBuff;
    //QUE_T                   rIOReqQueue;
    //QUE_T                   rFreeIOReqQueue;

    wait_queue_head_t       waitq;
    struct task_struct 	    *main_thread;

    struct timer_list tickfn;


#if CFG_SUPPORT_EXT_CONFIG
    UINT_16     au2ExtCfg[256];  /* NVRAM data buffer */
    UINT_32     u4ExtCfgLength;  /* 0 means data is NOT valid */
#endif

#if 1//CFG_SUPPORT_WAPI
    /* Should be large than the PARAM_WAPI_ASSOC_INFO_T */
    UINT_8                  aucWapiAssocInfoIEs[42];
    UINT_16                 u2WapiAssocInfoIESz;
#endif

#if CFG_ENABLE_BT_OVER_WIFI
    GL_BOW_INFO             rBowInfo;
#endif

#if CFG_ENABLE_WIFI_DIRECT
    P_GL_P2P_INFO_T         prP2PInfo;
#if CFG_SUPPORT_P2P_RSSI_QUERY
    /* Wireless statistics struct net_device */
    struct iw_statistics    rP2pIwStats;
#endif
#endif
    BOOLEAN                 fgWpsActive;
	UINT_8                  aucWSCIE[500]; /*for probe req*/
    UINT_16                 u2WSCIELen;
	UINT_8                  aucWSCAssocInfoIE[200]; /*for Assoc req*/
    UINT_16                 u2WSCAssocInfoIELen;

    /* NVRAM availability */
    BOOLEAN                 fgNvramAvailable;

    BOOLEAN                 fgMcrAccessAllowed;

    /* MAC Address Overriden by IOCTL */
    BOOLEAN                 fgIsMacAddrOverride;
    PARAM_MAC_ADDRESS       rMacAddrOverride;

    SET_TXPWR_CTRL_T        rTxPwr;

    /* for cfg80211 scan done indication */
    struct cfg80211_scan_request    *prScanRequest;

    /* to indicate registered or not */
    BOOLEAN                 fgIsRegistered;

    /* for cfg80211 connected indication */
    UINT_32                 u4RspIeLength;
    UINT_8                  aucRspIe[CFG_CFG80211_IE_BUF_LEN];

    UINT_32                 u4ReqIeLength;
    UINT_8                  aucReqIe[CFG_CFG80211_IE_BUF_LEN];
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
    /* linux 2.4 */
    typedef void (*PFN_WLANISR)(int irq, void *dev_id, struct pt_regs *regs);
#else
    typedef irqreturn_t (*PFN_WLANISR)(int irq, void *dev_id, struct pt_regs *regs);
#endif

typedef void (*PFN_LINUX_TIMER_FUNC)(unsigned long);


/* generic sub module init/exit handler
*   now, we only have one sub module, p2p
*/
#if CFG_ENABLE_WIFI_DIRECT
typedef BOOLEAN (*SUB_MODULE_INIT)(P_GLUE_INFO_T prGlueInfo);
typedef BOOLEAN (*SUB_MODULE_EXIT)(P_GLUE_INFO_T prGlueInfo);

typedef struct _SUB_MODULE_HANDLER {
    SUB_MODULE_INIT subModInit;
    SUB_MODULE_EXIT subModExit;
    BOOLEAN fgIsInited;
} SUB_MODULE_HANDLER, *P_SUB_MODULE_HANDLER;

#endif

#if CONFIG_NL80211_TESTMODE

typedef struct _NL80211_DRIVER_TEST_MODE_PARAMS {
	UINT_32  index;
	UINT_32  buflen;
} NL80211_DRIVER_TEST_MODE_PARAMS, *P_NL80211_DRIVER_TEST_MODE_PARAMS;

/*SW CMD */
typedef struct _NL80211_DRIVER_SW_CMD_PARAMS {
    NL80211_DRIVER_TEST_MODE_PARAMS hdr;
    UINT_8           set;
    unsigned long    adr;
    unsigned long    data;
}NL80211_DRIVER_SW_CMD_PARAMS, *P_NL80211_DRIVER_SW_CMD_PARAMS;

struct iw_encode_exts {
    __u32   ext_flags;                      /*!< IW_ENCODE_EXT_* */
    __u8    tx_seq[IW_ENCODE_SEQ_MAX_SIZE]; /*!< LSB first */
    __u8    rx_seq[IW_ENCODE_SEQ_MAX_SIZE]; /*!< LSB first */
    __u8    addr[MAC_ADDR_LEN];   /*!< ff:ff:ff:ff:ff:ff for broadcast/multicast
                                                          *   (group) keys or unicast address for
                                                          *   individual keys */
    __u16   alg;            /*!< IW_ENCODE_ALG_* */
    __u16   key_len;
    __u8    key[32];
};

/*SET KEY EXT */
typedef struct _NL80211_DRIVER_SET_KEY_EXTS {
    NL80211_DRIVER_TEST_MODE_PARAMS hdr;
    UINT_8     key_index;
    UINT_8     key_len;
    struct iw_encode_exts ext;
}NL80211_DRIVER_SET_KEY_EXTS, *P_NL80211_DRIVER_SET_KEY_EXTS;


#endif

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
/*----------------------------------------------------------------------------*/
/* Macros of SPIN LOCK operations for using in Glue Layer                     */
/*----------------------------------------------------------------------------*/
#if CFG_USE_SPIN_LOCK_BOTTOM_HALF
    #define GLUE_SPIN_LOCK_DECLARATION()
    #define GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, rLockCategory)   \
            { \
                if (rLockCategory < SPIN_LOCK_NUM) \
                spin_lock_bh(&(prGlueInfo->rSpinLock[rLockCategory])); \
            }
    #define GLUE_RELEASE_SPIN_LOCK(prGlueInfo, rLockCategory)   \
            { \
                if (rLockCategory < SPIN_LOCK_NUM) \
                spin_unlock_bh(&(prGlueInfo->rSpinLock[rLockCategory])); \
            }
#else /* !CFG_USE_SPIN_LOCK_BOTTOM_HALF */
    #define GLUE_SPIN_LOCK_DECLARATION()                        UINT_32 __u4Flags = 0
    #define GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, rLockCategory)   \
            { \
                if (rLockCategory < SPIN_LOCK_NUM) \
                spin_lock_irqsave(&(prGlueInfo)->rSpinLock[rLockCategory], __u4Flags); \
            }
    #define GLUE_RELEASE_SPIN_LOCK(prGlueInfo, rLockCategory)   \
            { \
                if (rLockCategory < SPIN_LOCK_NUM) \
                spin_unlock_irqrestore(&(prGlueInfo->rSpinLock[rLockCategory]), __u4Flags); \
            }
#endif /* !CFG_USE_SPIN_LOCK_BOTTOM_HALF */


/*----------------------------------------------------------------------------*/
/* Macros for accessing Reserved Fields of native packet                      */
/*----------------------------------------------------------------------------*/
#define GLUE_GET_PKT_QUEUE_ENTRY(_p)    \
            (&( ((struct sk_buff *)(_p))->cb[0] ))

#define GLUE_GET_PKT_DESCRIPTOR(_prQueueEntry)  \
            ((P_NATIVE_PACKET) ((UINT_32)_prQueueEntry - offsetof(struct sk_buff, cb[0])) )

#define  GLUE_SET_PKT_FLAG_802_11(_p)  \
            (*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) |= BIT(7))

#define GLUE_SET_PKT_FLAG_1X(_p)  \
            (*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) |= BIT(6))

#define GLUE_SET_PKT_FLAG_PAL(_p)  \
            (*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) |= BIT(5))

#define GLUE_SET_PKT_FLAG_P2P(_p)  \
            (*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) |= BIT(4))



#define GLUE_SET_PKT_TID(_p, _tid)  \
            (*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) |= (((UINT_8)((_tid) & (BITS(0,3))))))


#define GLUE_SET_PKT_FRAME_LEN(_p, _u2PayloadLen) \
            (*((PUINT_16) &( ((struct sk_buff *)(_p))->cb[6] )) = (UINT_16)(_u2PayloadLen))

#define GLUE_GET_PKT_FRAME_LEN(_p)    \
            (*((PUINT_16) &( ((struct sk_buff *)(_p))->cb[6] )) )


#define  GLUE_GET_PKT_IS_802_11(_p)        \
            ((*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) ) & (BIT(7)))

#define  GLUE_GET_PKT_IS_1X(_p)        \
            ((*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) ) & (BIT(6)))

#define GLUE_GET_PKT_TID(_p)        \
            ((*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) ) & (BITS(0,3)))


#define GLUE_GET_PKT_IS_PAL(_p)        \
            ((*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) ) & (BIT(5)))

#define GLUE_GET_PKT_IS_P2P(_p)        \
            ((*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[4] )) ) & (BIT(4)))


#define GLUE_SET_PKT_HEADER_LEN(_p, _ucMacHeaderLen)    \
            (*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[5] )) = (UINT_8)(_ucMacHeaderLen))

#define GLUE_GET_PKT_HEADER_LEN(_p) \
            (*((PUINT_8) &( ((struct sk_buff *)(_p))->cb[5] )) )

#define GLUE_SET_PKT_ARRIVAL_TIME(_p, _rSysTime) \
            (*((POS_SYSTIME) &( ((struct sk_buff *)(_p))->cb[8] )) = (OS_SYSTIME)(_rSysTime))

#define GLUE_GET_PKT_ARRIVAL_TIME(_p)    \
            (*((POS_SYSTIME) &( ((struct sk_buff *)(_p))->cb[8] )) )

/* Check validity of prDev, private data, and pointers */
#define GLUE_CHK_DEV(prDev) \
    ((prDev && *((P_GLUE_INFO_T *) netdev_priv(prDev))) ? TRUE : FALSE)

#define GLUE_CHK_PR2(prDev, pr2) \
    ((GLUE_CHK_DEV(prDev) && pr2) ? TRUE : FALSE)

#define GLUE_CHK_PR3(prDev, pr2, pr3) \
    ((GLUE_CHK_PR2(prDev, pr2) && pr3) ? TRUE : FALSE)

#define GLUE_CHK_PR4(prDev, pr2, pr3, pr4) \
    ((GLUE_CHK_PR3(prDev, pr2, pr3) && pr4) ? TRUE : FALSE)

#define GLUE_SET_EVENT(pr) \
	kalSetEvent(pr)

#define GLUE_INC_REF_CNT(_refCount)     atomic_inc((atomic_t *)&(_refCount))
#define GLUE_DEC_REF_CNT(_refCount)     atomic_dec((atomic_t *)&(_refCount))


#define DbgPrint(...)
/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
#ifdef WLAN_INCLUDE_PROC
INT_32
procRemoveProcfs (
    struct net_device *prDev,
    char *pucDevName
    );

INT_32
procInitProcfs (
    struct net_device *prDev,
    char *pucDevName
    );
#endif /* WLAN_INCLUDE_PROC */

#if CFG_ENABLE_BT_OVER_WIFI
BOOLEAN
glRegisterAmpc (
    P_GLUE_INFO_T prGlueInfo
    );

BOOLEAN
glUnregisterAmpc (
    P_GLUE_INFO_T prGlueInfo
    );
#endif

#if CFG_ENABLE_WIFI_DIRECT

VOID
wlanSubModRunInit(
    P_GLUE_INFO_T prGlueInfo
    );

VOID
wlanSubModRunExit(
    P_GLUE_INFO_T prGlueInfo
    );

BOOLEAN
wlanSubModInit(
    P_GLUE_INFO_T prGlueInfo
    );

BOOLEAN
wlanSubModExit(
    P_GLUE_INFO_T prGlueInfo
    );

VOID
wlanSubModRegisterInitExit(
    SUB_MODULE_INIT rSubModInit,
    SUB_MODULE_EXIT rSubModExit,
    ENUM_SUB_MODULE_IDX_T eSubModIdx
    );

BOOLEAN
wlanExportGlueInfo(
    P_GLUE_INFO_T *prGlueInfoExpAddr
    );

BOOLEAN
wlanIsLaunched(
    VOID
    );

void
p2pSetMulticastListWorkQueueWrapper(
    P_GLUE_INFO_T prGlueInfo
    );



#endif

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _GL_OS_H */

