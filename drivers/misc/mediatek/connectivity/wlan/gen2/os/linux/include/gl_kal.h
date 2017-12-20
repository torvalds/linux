/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/include/gl_kal.h#1
*/

/*! \file   gl_kal.h
    \brief  Declaration of KAL functions - kal*() which is provided by GLUE Layer.

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

/*
** Log: gl_kal.h
 *
 * 06 13 2012 yuche.tsai
 * NULL
 * Update maintrunk driver.
 * Add support for driver compose assoc request frame.
 *
 * 04 12 2012 terry.wu
 * NULL
 * Add AEE message support
 * 1) Show AEE warning(red screen) if SDIO access error occurs

 *
 * 03 02 2012 terry.wu
 * NULL
 * Snc CFG80211 modification for ICS migration from branch 2.2.
 *
 * 02 06 2012 wh.su
 * [WCXRP00001177] [MT6620 Wi-Fi][Driver][2.2] Adding the query channel filter for AP mode
 * adding the channel query filter for AP mode.
 *
 * 01 02 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Adding the proto type function for set_int set_tx_power and get int get_ch_list.
 *
 * 12 13 2011 cm.chang
 * [WCXRP00001136] [All Wi-Fi][Driver] Add wake lock for pending timer
 * Add wake lock if timer timeout value is smaller than 5 seconds
 *
 * 11 24 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * adjust the code for Non-DBG and no XLOG.
 *
 * 11 22 2011 cp.wu
 * [WCXRP00001120] [MT6620 Wi-Fi][Driver] Modify roaming to AIS state transition from synchronous to asynchronous
 * approach to avoid incomplete state termination
 * 1. change RDD related compile option brace position.
 * 2. when roaming is triggered, ask AIS to transit immediately only when AIS is in Normal TR state without join
 * timeout timer ticking
 * 3. otherwise, insert AIS_REQUEST into pending request queue
 *
 * 11 11 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * modify the xlog related code.
 *
 * 11 10 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Modify the QM xlog level and remove LOG_FUNC.
 *
 * 11 10 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Using the new XLOG define for dum Memory.
 *
 * 11 08 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Add xlog function.
 *
 * 11 08 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add debug counters, eCurPsProf, for PS.
 *
 * 11 08 2011 cm.chang
 * NULL
 * Add RLM and CNM debug message for XLOG
 *
 * 11 07 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add debug counters and periodically dump counters for debugging.
 *
 * 11 03 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Add dumpMemory8 at XLOG support.
 *
 * 11 02 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * adding the code for XLOG.
 *
 * 10 12 2011 wh.su
 * [WCXRP00001036] [MT6620 Wi-Fi][Driver][FW] Adding the 802.11w code for MFP
 * adding the 802.11w related function and define .
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 04 12 2011 cp.wu
 * [WCXRP00000635] [MT6620 Wi-Fi][Driver] Clear pending security frames when QM clear pending data frames for dedicated
 * network type
 * include link.h for linux's port.
 *
 * 04 12 2011 cp.wu
 * [WCXRP00000635] [MT6620 Wi-Fi][Driver] Clear pending security frames when QM clear pending data frames for dedicated
 * network type
 * clear pending security frames for dedicated network type when BSS is being deactivated/disconnected
 *
 * 04 01 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * 1. simplify config.h due to aggregation options could be also applied for eHPI/SPI interface
 * 2. use spin-lock instead of semaphore for protecting eHPI access because of possible access from ISR
 * 3. request_irq() API has some changes between linux kernel 2.6.12 and 2.6.26
 *
 * 03 16 2011 cp.wu
 * [WCXRP00000562] [MT6620 Wi-Fi][Driver] I/O buffer pre-allocation to avoid physically continuous memory shortage
 * after system running for a long period
 * 1. pre-allocate physical continuous buffer while module is being loaded
 * 2. use pre-allocated physical continuous buffer for TX/RX DMA transfer
 *
 * The windows part remained the same as before, but added similar APIs to hide the difference.
 *
 * 03 10 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Add BOW table.
 *
 * 03 07 2011 terry.wu
 * [WCXRP00000521] [MT6620 Wi-Fi][Driver] Remove non-standard debug message
 * Toggle non-standard debug messages to comments.
 *
 * 03 06 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Sync BOW Driver to latest person development branch version..
 *
 * 03 02 2011 cp.wu
 * [WCXRP00000503] [MT6620 Wi-Fi][Driver] Take RCPI brought by association response as initial RSSI right after
 * connection is built.
 * use RCPI brought by ASSOC-RESP after connection is built as initial RCPI to avoid using a uninitialized MAC-RX RCPI.
 *
 * 02 24 2011 cp.wu
 * [WCXRP00000490] [MT6620 Wi-Fi][Driver][Win32] modify kalMsleep() implementation because NdisMSleep() won't sleep
 * long enough for specified interval such as 500ms
 * modify cnm_timer and hem_mbox APIs to be thread safe to ease invoking restrictions
 *
 * 01 12 2011 cp.wu
 * [WCXRP00000357] [MT6620 Wi-Fi][Driver][Bluetooth over Wi-Fi] add another net device interface for BT AMP
 * implementation of separate BT_OVER_WIFI data path.
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc and vmalloc implementations to ease
 * physically continuous memory demands
 * separate kalMemAlloc() into virtually-continuous and physically-continuous type to ease slab system pressure
 *
 * 12 31 2010 cp.wu
 * [WCXRP00000335] [MT6620 Wi-Fi][Driver] change to use milliseconds sleep instead of delay to avoid blocking to system
 * scheduling
 * change to use msleep() and shorten waiting interval to reduce blocking to other task while Wi-Fi driver is being
 * loaded
 *
 * 12 31 2010 jeffrey.chang
 * [WCXRP00000332] [MT6620 Wi-Fi][Driver] add kal sleep function for delay which use blocking call
 * modify the implementation of kalDelay to msleep
 *
 * 12 22 2010 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface for supporting Wi-Fi Direct Service
 * Discovery
 * 1. header file restructure for more clear module isolation
 * 2. add function interface definition for implementing Service Discovery callbacks
 *
 * 11 30 2010 yuche.tsai
 * NULL
 * Invitation & Provision Discovery Indication.
 *
 * 11 26 2010 cp.wu
 * [WCXRP00000209] [MT6620 Wi-Fi][Driver] Modify NVRAM checking mechanism to warning only with necessary data field
 * checking
 * 1. NVRAM error is now treated as warning only, thus normal operation is still available but extra scan result used
 * to indicate user is attached
 * 2. DPD and TX-PWR are needed fields from now on, if these 2 fields are not available then warning message is shown
 *
 * 11 08 2010 cp.wu
 * [WCXRP00000166] [MT6620 Wi-Fi][Driver] use SDIO CMD52 for enabling/disabling interrupt to reduce transaction period
 * change to use CMD52 for enabling/disabling interrupt to reduce SDIO transaction time
 *
 * 11 01 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000150] [MT6620 Wi-Fi][Driver]
 * Add implementation for querying current TX rate from firmware auto rate module
 * 1) Query link speed (TX rate) from firmware directly with buffering mechanism to reduce overhead
 * 2) Remove CNM CH-RECOVER event handling
 * 3) cfg read/write API renamed with kal prefix for unified naming rules.
 *
 * 10 05 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check
 * 1) add NVRAM access API
 * 2) fake scanning result when NVRAM doesn't exist and/or version mismatch. (off by compiler option)
 * 3) add OID implementation for NVRAM read/write service
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
 * 09 21 2010 cp.wu
 * [WCXRP00000053] [MT6620 Wi-Fi][Driver] Reset incomplete and might leads to BSOD when entering RF test with AIS
 * associated
 * Do a complete reset with STA-REC null checking for RF test re-entry
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * Eliminate Linux Compile Warning
 *
 * 09 10 2010 wh.su
 * NULL
 * fixed the compiling error at win XP.
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
 * 08 06 2010 cp.wu
 * NULL
 * driver hook modifications corresponding to ioctl interface change.
 *
 * 08 03 2010 cp.wu
 * NULL
 * [Wi-Fi Direct Driver Hook] change event indication API to be consistent with supplicant
 *
 * 08 03 2010 cp.wu
 * NULL
 * [Wi-Fi Direct] add framework for driver hooks
 *
 * 08 02 2010 jeffrey.chang
 * NULL
 * modify kalSetEvent declaration
 *
 * 07 29 2010 cp.wu
 * NULL
 * simplify post-handling after TX_DONE interrupt is handled.
 *
 * 07 23 2010 cp.wu
 *
 * 1) re-enable AIS-FSM beacon timeout handling.
 * 2) scan done API revised
 *
 * 07 23 2010 jeffrey.chang
 *
 * fix kal header file
 *
 * 07 22 2010 jeffrey.chang
 *
 * use different spin lock for security frame
 *
 * 07 22 2010 jeffrey.chang
 *
 * add new spinlock
 *
 * 07 19 2010 jeffrey.chang
 *
 * add kal api for scanning done
 *
 * 07 19 2010 jeffrey.chang
 *
 * modify cmd/data path for new design
 *
 * 07 19 2010 jeffrey.chang
 *
 * add new kal api
 *
 * 07 19 2010 jeffrey.chang
 *
 * Linux port modification
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * change MAC address updating logic.
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * gl_kal merged
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add basic handling framework for wireless extension ioctls.
 *
 * 05 11 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add ioctl for controlling p2p scan phase parameters
 *
 * 05 10 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * fill network type field while doing frame identification.
 *
 * 05 10 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * implement basic wi-fi direct framework
 *
 * 05 07 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add basic framework for implementating P2P driver hook.
 *
 * 05 07 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * modify kalMemAlloc method
 *
 * 04 28 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * change prefix for data structure used to communicate with 802.11 PAL
 * to avoid ambiguous naming with firmware interface
 *
 * 04 27 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add multiple physical link support
 *
 * 04 27 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * follow Linux's firmware framework, and remove unused kal API
 *
 * 04 22 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * when acquiring driver-own, wait for up to 8 seconds.
 *
 * 04 22 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 *
 * 1) modify rx path code for supporting Wi-Fi direct
 * 2) modify config.h since Linux dont need to consider retaining packet
 *
 * 04 21 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * add for private ioctl support
 *
 * 04 20 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * don't need SPIN_LOCK_PWR_CTRL anymore, it will raise IRQL
 *  * and cause SdBusSubmitRequest running at DISPATCH_LEVEL as well.
 *
 * 04 14 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * information buffer for query oid/ioctl is now buffered in prCmdInfo
 *  *  *  *  *  *  *  * instead of glue-layer variable to improve multiple oid/ioctl capability
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *    capability
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * 2) command sequence number is now increased atomically
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 04 09 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * 1) add spinlock
 *  *  * 2) add KAPI for handling association info
 *
 * 04 09 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * adding firmware download KAPI
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * finish non-glue layer access to glue variables
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * accessing to firmware load/start address, and access to OID handling information
 *  *  *  * are now handled in glue layer
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * rWlanInfo should be placed at adapter rather than glue due to most operations
 *  *  *  *  *  *  *  *  * are done in adapter layer.
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate direct access to prGlueInfo->eParamMediaStateIndicated from non-glue layer
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add KAL API: kalFlushPendingTxPackets(), and take use of the API
 *
 * 04 06 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * eliminate direct access to prGlueInfo->rWlanInfo.eLinkAttr.ucMediaStreamMode from non-glue layer.
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
 * 1) for some OID, never do timeout expiration
 *  *  *  * 2) add 2 kal API for later integration
 *
 * 03 30 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * emulate NDIS Pending OID facility
 *
 * 03 26 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * [WPD00003826] Initial import for Linux port
 * adding firmware download KAPI
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
**  \main\maintrunk.MT5921\41 2009-09-28 20:19:23 GMT mtk01090
**  Add private ioctl to carry OID structures. Restructure public/private ioctl interfaces to Linux kernel.
**  \main\maintrunk.MT5921\40 2009-08-18 22:57:09 GMT mtk01090
**  Add Linux SDIO (with mmc core) support.
**  Add Linux 2.6.21, 2.6.25, 2.6.26.
**  Fix compile warning in Linux.
**  \main\maintrunk.MT5921\39 2009-06-23 23:19:15 GMT mtk01090
**  Add build option BUILD_USE_EEPROM and compile option CFG_SUPPORT_EXT_CONFIG for NVRAM support
**  \main\maintrunk.MT5921\38 2009-02-09 14:03:17 GMT mtk01090
**  Add KAL function kalDevSetPowerState(). It is not implemented yet. Only add an empty macro.
**
**  \main\maintrunk.MT5921\37 2009-01-22 13:05:59 GMT mtk01088
**  new defeine to got 1x value at packet reserved field
**  \main\maintrunk.MT5921\36 2008-12-08 16:15:02 GMT mtk01461
**  Add kalQueryValidBufferLength() macro
**  \main\maintrunk.MT5921\35 2008-11-13 20:33:15 GMT mtk01104
**  Remove lint warning
**  \main\maintrunk.MT5921\34 2008-10-22 11:05:52 GMT mtk01461
**  Remove unused macro
**  \main\maintrunk.MT5921\33 2008-10-16 15:48:17 GMT mtk01461
**  Update driver to fix lint warning
**  \main\maintrunk.MT5921\32 2008-09-02 11:50:51 GMT mtk01461
**  SPIN_LOCK_SDIO_DDK_TX_QUE
**  \main\maintrunk.MT5921\31 2008-08-29 15:58:30 GMT mtk01088
**  remove non-used function for code refine
**  \main\maintrunk.MT5921\30 2008-08-21 00:33:29 GMT mtk01461
**  Update for Driver Review
**  \main\maintrunk.MT5921\29 2008-06-19 13:29:14 GMT mtk01425
**  1. Add declaration of SPIN_LOCK_SDIO_DDK_TX_QUE and SPIN_LOCK_SDIO_DDK_RX_QUE
**  \main\maintrunk.MT5921\28 2008-05-30 20:27:34 GMT mtk01461
**  Rename KAL function
**  \main\maintrunk.MT5921\27 2008-05-30 14:42:05 GMT mtk01461
**  Remove WMM Assoc Flag in KAL
**  \main\maintrunk.MT5921\26 2008-05-29 14:15:18 GMT mtk01084
**  remove un-used function
**  \main\maintrunk.MT5921\25 2008-04-23 14:02:20 GMT mtk01084
**  modify KAL port access function prototype
**  \main\maintrunk.MT5921\24 2008-04-17 23:06:41 GMT mtk01461
**  Add iwpriv support for AdHocMode setting
**  \main\maintrunk.MT5921\23 2008-04-08 15:38:50 GMT mtk01084
**  add KAL function to setting pattern search function enable/ disable
**  \main\maintrunk.MT5921\22 2008-03-26 15:34:48 GMT mtk01461
**  Add update MAC address func
**  \main\maintrunk.MT5921\21 2008-03-18 15:56:15 GMT mtk01084
**  update ENUM_NIC_INITIAL_PARAM_E
**  \main\maintrunk.MT5921\20 2008-03-18 11:49:28 GMT mtk01084
**  update function for initial value access
**  \main\maintrunk.MT5921\19 2008-03-18 10:21:31 GMT mtk01088
**  use kal update associate request at linux
**  \main\maintrunk.MT5921\18 2008-03-14 18:03:41 GMT mtk01084
**  refine register and port access function
**  \main\maintrunk.MT5921\17 2008-03-11 14:51:02 GMT mtk01461
**  Add copy_to(from)_user macro
**  \main\maintrunk.MT5921\16 2008-03-06 23:42:21 GMT mtk01385
**  1. add Query Registry Mac address function.
**  \main\maintrunk.MT5921\15 2008-02-26 09:48:04 GMT mtk01084
**  modify KAL set network address/ checksum offload part
**  \main\maintrunk.MT5921\14 2008-01-09 17:54:58 GMT mtk01084
**  Modify the argument of kalQueryPacketInfo
**  \main\maintrunk.MT5921\13 2007-11-29 02:05:20 GMT mtk01461
**  Fix Windows RX multiple packet retain problem
**  \main\maintrunk.MT5921\12 2007-11-26 19:43:45 GMT mtk01461
**  Add OS_TIMESTAMP macro
**
**  \main\maintrunk.MT5921\11 2007-11-09 16:36:15 GMT mtk01425
**  1. Modify for CSUM offloading with Tx Fragment
**  \main\maintrunk.MT5921\10 2007-11-07 18:38:37 GMT mtk01461
**  Add Tx Fragmentation Support
**  \main\maintrunk.MT5921\9 2007-11-06 19:36:50 GMT mtk01088
**  add the WPS related code
**  \main\maintrunk.MT5921\8 2007-11-02 01:03:57 GMT mtk01461
**  Unify TX Path for Normal and IBSS Power Save + IBSS neighbor learning
** Revision 1.4  2007/07/05 07:25:33  MTK01461
** Add Linux initial code, modify doc, add 11BB, RF init code
**
** Revision 1.3  2007/06/27 02:18:50  MTK01461
** Update SCAN_FSM, Initial(Can Load Module), Proc(Can do Reg R/W), TX API
**
** Revision 1.2  2007/06/25 06:16:23  MTK01461
** Update illustrations, gl_init.c, gl_kal.c, gl_kal.h, gl_os.h and RX API
**
*/

#ifndef _GL_KAL_H
#define _GL_KAL_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "config.h"
#include "gl_typedef.h"
#include "gl_os.h"
#include "link.h"
#include "nic/mac.h"
#include "nic/wlan_def.h"
#include "wlan_lib.h"
#include "wlan_oid.h"
#include "gl_wext_priv.h"
#include <asm/div64.h>

#if CFG_ENABLE_BT_OVER_WIFI
#include "nic/bow.h"
#endif

#if DBG
extern int allocatedMemSize;
#endif

#if CFG_SUPPORT_MET_PROFILING
#include "linux/kallsyms.h"
#include <linux/ftrace_event.h>
#endif

extern BOOLEAN fgIsUnderSuspend;
extern UINT_32 TaskIsrCnt;
extern BOOLEAN fgIsResetting;
extern int wlanHardStartXmit(struct sk_buff *prSkb, struct net_device *prDev);
extern UINT_32 u4MemAllocCnt, u4MemFreeCnt;


extern struct delayed_work sched_workq;

#if defined(MT6620) && CFG_MULTI_ECOVER_SUPPORT
extern ENUM_WMTHWVER_TYPE_T mtk_wcn_wmt_hwver_get(VOID);
#endif

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/* #define USEC_PER_MSEC   (1000) */

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_SPIN_LOCK_CATEGORY_E {
	SPIN_LOCK_FSM = 0,

	/* FIX ME */
	SPIN_LOCK_RX_QUE,
	SPIN_LOCK_TX_QUE,
	SPIN_LOCK_CMD_QUE,
	SPIN_LOCK_TX_RESOURCE,
	SPIN_LOCK_CMD_RESOURCE,
	SPIN_LOCK_QM_TX_QUEUE,
	SPIN_LOCK_CMD_PENDING,
	SPIN_LOCK_CMD_SEQ_NUM,
	SPIN_LOCK_TX_MSDU_INFO_LIST,
	SPIN_LOCK_TXING_MGMT_LIST,
	SPIN_LOCK_TX_SEQ_NUM,
	SPIN_LOCK_TX_COUNT,
	SPIN_LOCK_TXS_COUNT,
	/* end    */
	SPIN_LOCK_TX,
	SPIN_LOCK_IO_REQ,
	SPIN_LOCK_INT,

	SPIN_LOCK_MGT_BUF,
	SPIN_LOCK_MSG_BUF,
	SPIN_LOCK_STA_REC,

	SPIN_LOCK_MAILBOX,
	SPIN_LOCK_TIMER,

	SPIN_LOCK_BOW_TABLE,

	SPIN_LOCK_EHPI_BUS,	/* only for EHPI */
	SPIN_LOCK_NET_DEV,
	SPIN_LOCK_NUM
} ENUM_SPIN_LOCK_CATEGORY_E;

/* event for assoc information update */
typedef struct _EVENT_ASSOC_INFO {
	UINT_8 ucAssocReq;	/* 1 for assoc req, 0 for assoc rsp */
	UINT_8 ucReassoc;	/* 0 for assoc, 1 for reassoc */
	UINT_16 u2Length;
	PUINT_8 pucIe;
} EVENT_ASSOC_INFO, *P_EVENT_ASSOC_INFO;

typedef enum _ENUM_KAL_NETWORK_TYPE_INDEX_T {
	KAL_NETWORK_TYPE_AIS_INDEX = 0,
#if CFG_ENABLE_WIFI_DIRECT
	KAL_NETWORK_TYPE_P2P_INDEX,
#endif
#if CFG_ENABLE_BT_OVER_WIFI
	KAL_NETWORK_TYPE_BOW_INDEX,
#endif
	KAL_NETWORK_TYPE_INDEX_NUM
} ENUM_KAL_NETWORK_TYPE_INDEX_T;

typedef enum _ENUM_KAL_MEM_ALLOCATION_TYPE_E {
	PHY_MEM_TYPE,		/* physically continuous */
	VIR_MEM_TYPE,		/* virtually continuous */
	MEM_TYPE_NUM
} ENUM_KAL_MEM_ALLOCATION_TYPE;

#if CONFIG_ANDROID		/* Defined in Android kernel source */
typedef struct wake_lock KAL_WAKE_LOCK_T, *P_KAL_WAKE_LOCK_T;
#else
typedef UINT_32 KAL_WAKE_LOCK_T, *P_KAL_WAKE_LOCK_T;
#endif

#if CFG_SUPPORT_AGPS_ASSIST
typedef enum _ENUM_MTK_AGPS_ATTR {
	MTK_ATTR_AGPS_INVALID,
	MTK_ATTR_AGPS_CMD,
	MTK_ATTR_AGPS_DATA,
	MTK_ATTR_AGPS_IFINDEX,
	MTK_ATTR_AGPS_IFNAME,
	MTK_ATTR_AGPS_MAX
} ENUM_MTK_CCX_ATTR;

typedef enum _ENUM_AGPS_EVENT {
	AGPS_EVENT_WLAN_ON,
	AGPS_EVENT_WLAN_OFF,
	AGPS_EVENT_WLAN_AP_LIST,
	WIFI_EVENT_CHIP_RESET,
} ENUM_CCX_EVENT;
BOOLEAN kalIndicateAgpsNotify(P_ADAPTER_T prAdapter, UINT_8 cmd, PUINT_8 data, UINT_16 dataLen);
#endif

struct KAL_HALT_CTRL_T {
	struct semaphore lock;
	struct task_struct *owner;
	BOOLEAN fgHalt;
	BOOLEAN fgHeldByKalIoctl;
	OS_SYSTIME u4HoldStart;
};
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
/* Macros of bit operation                                                    */
/*----------------------------------------------------------------------------*/
#define KAL_SET_BIT(bitOffset, value)             set_bit(bitOffset, &value)
#define KAL_CLR_BIT(bitOffset, value)             clear_bit(bitOffset, &value)
#define KAL_TEST_AND_CLEAR_BIT(bitOffset, value)  test_and_clear_bit(bitOffset, &value)
#define KAL_TEST_BIT(bitOffset, value)            test_bit(bitOffset, &value)

/*----------------------------------------------------------------------------*/
/* Macros of SPIN LOCK operations for using in Driver Layer                   */
/*----------------------------------------------------------------------------*/
#define KAL_SPIN_LOCK_DECLARATION()             unsigned long __u4Flags

#define KAL_ACQUIRE_SPIN_LOCK(_prAdapter, _rLockCategory)   \
	    kalAcquireSpinLock(((P_ADAPTER_T)_prAdapter)->prGlueInfo, _rLockCategory, &__u4Flags)

#define KAL_RELEASE_SPIN_LOCK(_prAdapter, _rLockCategory)   \
	    kalReleaseSpinLock(((P_ADAPTER_T)_prAdapter)->prGlueInfo, _rLockCategory, __u4Flags)

/*----------------------------------------------------------------------------*/
/* Macros for accessing Reserved Fields of native packet                      */
/*----------------------------------------------------------------------------*/
#define KAL_GET_PKT_QUEUE_ENTRY(_p)             GLUE_GET_PKT_QUEUE_ENTRY(_p)
#define KAL_GET_PKT_DESCRIPTOR(_prQueueEntry)   GLUE_GET_PKT_DESCRIPTOR(_prQueueEntry)
#define KAL_GET_PKT_TID(_p)                     GLUE_GET_PKT_TID(_p)
#define KAL_GET_PKT_IS1X(_p)                    GLUE_GET_PKT_IS1X(_p)
#define KAL_GET_PKT_HEADER_LEN(_p)              GLUE_GET_PKT_HEADER_LEN(_p)
#define KAL_GET_PKT_PAYLOAD_LEN(_p)             GLUE_GET_PKT_PAYLOAD_LEN(_p)
#define KAL_GET_PKT_ARRIVAL_TIME(_p)            GLUE_GET_PKT_ARRIVAL_TIME(_p)

/*----------------------------------------------------------------------------*/
/* Macros of wake_lock operations for using in Driver Layer                   */
/*----------------------------------------------------------------------------*/
#if CONFIG_ANDROID		/* Defined in Android kernel source */
#define KAL_WAKE_LOCK_INIT(_prAdapter, _prWakeLock, _pcName) \
	wake_lock_init(_prWakeLock, WAKE_LOCK_SUSPEND, _pcName)

#define KAL_WAKE_LOCK_DESTROY(_prAdapter, _prWakeLock) \
	wake_lock_destroy(_prWakeLock)

#define KAL_WAKE_LOCK(_prAdapter, _prWakeLock) \
	wake_lock(_prWakeLock)

#define KAL_WAKE_LOCK_TIMEOUT(_prAdapter, _prWakeLock, _u4Timeout) \
	wake_lock_timeout(_prWakeLock, _u4Timeout)

#define KAL_WAKE_UNLOCK(_prAdapter, _prWakeLock) \
	wake_unlock(_prWakeLock)

#else
#define KAL_WAKE_LOCK_INIT(_prAdapter, _prWakeLock, _pcName)
#define KAL_WAKE_LOCK_DESTROY(_prAdapter, _prWakeLock)
#define KAL_WAKE_LOCK(_prAdapter, _prWakeLock)
#define KAL_WAKE_LOCK_TIMEOUT(_prAdapter, _prWakeLock, _u4Timeout)
#define KAL_WAKE_UNLOCK(_prAdapter, _prWakeLock)
#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief Cache memory allocation
*
* \param[in] u4Size Required memory size.
* \param[in] eMemType  Memory allocation type
*
* \return Pointer to allocated memory
*         or NULL
*/
/*----------------------------------------------------------------------------*/
#if DBG
#define kalMemAlloc(u4Size, eMemType) ({ \
	void *pvAddr; \
	if (eMemType == PHY_MEM_TYPE) { \
		pvAddr = kmalloc(u4Size, GFP_KERNEL); \
	} \
	else { \
		pvAddr = vmalloc(u4Size); \
	} \
	if (pvAddr) {   \
		allocatedMemSize += u4Size;   \
		DBGLOG(INIT, INFO, "%p(%u) allocated (%s:%s)\n", \
			pvAddr, (UINT_32)u4Size, __FILE__, __func__); \
	} \
	pvAddr; \
})
#else
#define kalMemAlloc(u4Size, eMemType) ({ \
	void *pvAddr; \
	if (eMemType == PHY_MEM_TYPE) { \
		pvAddr = kmalloc(u4Size, GFP_KERNEL); \
	} \
	else { \
		pvAddr = vmalloc(u4Size); \
	} \
	pvAddr; \
})
#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief Free allocated cache memory
*
* \param[in] pvAddr Required memory size.
* \param[in] eMemType  Memory allocation type
* \param[in] u4Size Allocated memory size.
*
* \return -
*/
/*----------------------------------------------------------------------------*/
#if DBG
#define kalMemFree(pvAddr, eMemType, u4Size) \
{   \
	if (pvAddr) { \
		allocatedMemSize -= u4Size; \
		DBGLOG(INIT, INFO, "%p(%u) freed (%s:%s)\n", \
			pvAddr, (UINT_32)u4Size, __FILE__, __func__);  \
	} \
	if (eMemType == PHY_MEM_TYPE) { \
		kfree(pvAddr); \
	} \
	else { \
		vfree(pvAddr); \
	} \
}
#else
#define kalMemFree(pvAddr, eMemType, u4Size) \
{   \
	if (eMemType == PHY_MEM_TYPE) { \
		kfree(pvAddr); \
	} \
	else { \
		vfree(pvAddr); \
	} \
}
#endif

#define kalUdelay(u4USec)                           udelay(u4USec)

#define kalMdelay(u4MSec)                           mdelay(u4MSec)
#define kalMsleep(u4MSec)                           msleep(u4MSec)

/* Copy memory from user space to kernel space */
#define kalMemCopyFromUser(_pvTo, _pvFrom, _u4N)    copy_from_user(_pvTo, _pvFrom, _u4N)

/* Copy memory from kernel space to user space */
#define kalMemCopyToUser(_pvTo, _pvFrom, _u4N)      copy_to_user(_pvTo, _pvFrom, _u4N)

/* Copy memory block with specific size */
#define kalMemCopy(pvDst, pvSrc, u4Size)            memcpy(pvDst, pvSrc, u4Size)

/* Set memory block with specific pattern */
#define kalMemSet(pvAddr, ucPattern, u4Size)        memset(pvAddr, ucPattern, u4Size)

/* Compare two memory block with specific length.
 * Return zero if they are the same.
 */
#define kalMemCmp(pvAddr1, pvAddr2, u4Size)         memcmp(pvAddr1, pvAddr2, u4Size)

/* Zero specific memory block */
#define kalMemZero(pvAddr, u4Size)                  memset(pvAddr, 0, u4Size)

/* string operation */
#define kalStrCpy(dest, src)                         strcpy(dest, src)
#define kalStrnCpy(dest, src, n)                      strncpy(dest, src, n)
#define kalStrCmp(ct, cs)                            strcmp(ct, cs)
#define kalStrnCmp(ct, cs, n)                         strncmp(ct, cs, n)
#define kalStrChr(s, c)                              strchr(s, c)
#define kalStrrChr(s, c)                             strrchr(s, c)
#define kalStrnChr(s, n, c)                           strnchr(s, n, c)
#define kalStrLen(s)                                strlen(s)
#define kalStrnLen(s, b)                             strnlen(s, b)
//#define kalStrniCmp(s1, s2, n)                          strnicmp(s1, s2, n)
#define kalStrniCmp(s1, s2, n)                          strncasecmp(s1, s2, n)
#define strnicmp(s1, s2, n)                              strncasecmp(s1, s2, n)
/* #define kalStrtoul(cp, endp, base)                    simple_strtoul(cp, endp, base)
#define kalStrtol(cp, endp, base)                     simple_strtol(cp, endp, base) */
#define kalkStrtou32(cp, base, resp)                   kstrtou32(cp, base, resp)
#define kalkStrtos32(cp, base, resp)                    kstrtos32(cp, base, resp)
#define kalSnprintf(buf, size, fmt, ...)              snprintf(buf, size, fmt, __VA_ARGS__)
#define kalSprintf(buf, fmt, ...)                     sprintf(buf, fmt, __VA_ARGS__)
/* remove for AOSP */
/* #define kalSScanf(buf, fmt, ...)                      sscanf(buf, fmt, __VA_ARGS__) */
#define kalStrStr(ct, cs)                            strstr(ct, cs)
#define kalStrSep(s, ct)                            strsep(s, ct)
#define kalStrCat(dest, src)                         strcat(dest, src)

/* defined for wince sdio driver only */
#if defined(_HIF_SDIO)
#define kalDevSetPowerState(prGlueInfo, ePowerMode) glSetPowerState(prGlueInfo, ePowerMode)
#else
#define kalDevSetPowerState(prGlueInfo, ePowerMode)
#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief Notify OS with SendComplete event of the specific packet. Linux should
*        free packets here.
*
* \param[in] prGlueInfo     Pointer of GLUE Data Structure
* \param[in] pvPacket       Pointer of Packet Handle
* \param[in] status         Status Code for OS upper layer
*
* \return -
*/
/*----------------------------------------------------------------------------*/
#define kalSendComplete(prGlueInfo, pvPacket, status)   \
	    kalSendCompleteAndAwakeQueue(prGlueInfo, pvPacket)

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is used to locate the starting address of incoming ethernet
*        frame for skb.
*
* \param[in] prGlueInfo     Pointer of GLUE Data Structure
* \param[in] pvPacket       Pointer of Packet Handle
*
* \return starting address of ethernet frame buffer.
*/
/*----------------------------------------------------------------------------*/
#define kalQueryBufferPointer(prGlueInfo, pvPacket)     \
	    ((PUINT_8)((struct sk_buff *)pvPacket)->data)

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is used to query the length of valid buffer which is accessible during
*         port read/write.
*
* \param[in] prGlueInfo     Pointer of GLUE Data Structure
* \param[in] pvPacket       Pointer of Packet Handle
*
* \return starting address of ethernet frame buffer.
*/
/*----------------------------------------------------------------------------*/
#define kalQueryValidBufferLength(prGlueInfo, pvPacket)     \
	    ((UINT_32)((struct sk_buff *)pvPacket)->end -  \
	     (UINT_32)((struct sk_buff *)pvPacket)->data)

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is used to copy the entire frame from skb to the destination
*        address in the input parameter.
*
* \param[in] prGlueInfo     Pointer of GLUE Data Structure
* \param[in] pvPacket       Pointer of Packet Handle
* \param[in] pucDestBuffer  Destination Address
*
* \return -
*/
/*----------------------------------------------------------------------------*/
#define kalCopyFrame(prGlueInfo, pvPacket, pucDestBuffer)   \
	do {struct sk_buff *skb = (struct sk_buff *)pvPacket; \
	memcpy(pucDestBuffer, skb->data, skb->len); } while (0)

#define kalGetTimeTick()                            jiffies_to_msecs(jiffies)

#define kalPrint                                    pr_debug

#if !DBG
#define AIS_ERROR_LOGFUNC(_Fmt...)
#define AIS_WARN_LOGFUNC(_Fmt...)
#define AIS_INFO_LOGFUNC(_Fmt...)
#define AIS_STATE_LOGFUNC(_Fmt...)
#define AIS_EVENT_LOGFUNC(_Fmt...)
#define AIS_TRACE_LOGFUNC(_Fmt...)
#define AIS_LOUD_LOGFUNC(_Fmt...)
#define AIS_TEMP_LOGFUNC(_Fmt...)

#define INTR_ERROR_LOGFUNC(_Fmt...)
#define INTR_WARN_LOGFUNC(_Fmt...)
#define INTR_INFO_LOGFUNC(_Fmt...)
#define INTR_STATE_LOGFUNC(_Fmt...)
#define INTR_EVENT_LOGFUNC(_Fmt...)
#define INTR_TRACE_LOGFUNC(_Fmt...)
#define INTR_LOUD_LOGFUNC(_Fmt...)
#define INTR_TEMP_LOGFUNC(_Fmt...)

#define INIT_ERROR_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define INIT_WARN_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define INIT_INFO_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define INIT_STATE_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define INIT_EVENT_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define INIT_TRACE_LOGFUNC(_Fmt...)
#define INIT_LOUD_LOGFUNC(_Fmt...)
#define INIT_TEMP_LOGFUNC(_Fmt...)

#define AAA_ERROR_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define AAA_WARN_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define AAA_INFO_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define AAA_STATE_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define AAA_EVENT_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define AAA_TRACE_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define AAA_LOUD_LOGFUNC(_Fmt...)
#define AAA_TEMP_LOGFUNC(_Fmt...)

#define ROAMING_ERROR_LOGFUNC(_Fmt...)
#define ROAMING_WARN_LOGFUNC(_Fmt...)
#define ROAMING_INFO_LOGFUNC(_Fmt...)
#define ROAMING_STATE_LOGFUNC(_Fmt...)
#define ROAMING_EVENT_LOGFUNC(_Fmt...)
#define ROAMING_TRACE_LOGFUNC(_Fmt...)
#define ROAMING_LOUD_LOGFUNC(_Fmt...)
#define ROAMING_TEMP_LOGFUNC(_Fmt...)

#define REQ_ERROR_LOGFUNC(_Fmt...)
#define REQ_WARN_LOGFUNC(_Fmt...)
#define REQ_INFO_LOGFUNC(_Fmt...)
#define REQ_STATE_LOGFUNC(_Fmt...)
#define REQ_EVENT_LOGFUNC(_Fmt...)
#define REQ_TRACE_LOGFUNC(_Fmt...)
#define REQ_LOUD_LOGFUNC(_Fmt...)
#define REQ_TEMP_LOGFUNC(_Fmt...)

#define TX_ERROR_LOGFUNC(_Fmt...)
#define TX_WARN_LOGFUNC(_Fmt...)
#define TX_INFO_LOGFUNC(_Fmt...)
#define TX_STATE_LOGFUNC(_Fmt...)
#define TX_EVENT_LOGFUNC(_Fmt...)
#define TX_TRACE_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define TX_LOUD_LOGFUNC(_Fmt...)
#define TX_TEMP_LOGFUNC(_Fmt...)

#define RX_ERROR_LOGFUNC(_Fmt...)
#define RX_WARN_LOGFUNC(_Fmt...)
#define RX_INFO_LOGFUNC(_Fmt...)
#define RX_STATE_LOGFUNC(_Fmt...)
#define RX_EVENT_LOGFUNC(_Fmt...)
#define RX_TRACE_LOGFUNC(_Fmt...)
#define RX_LOUD_LOGFUNC(_Fmt...)
#define RX_TEMP_LOGFUNC(_Fmt...)

#define RFTEST_ERROR_LOGFUNC(_Fmt...)
#define RFTEST_WARN_LOGFUNC(_Fmt...)
#define RFTEST_INFO_LOGFUNC(_Fmt...)
#define RFTEST_STATE_LOGFUNC(_Fmt...)
#define RFTEST_EVENT_LOGFUNC(_Fmt...)
#define RFTEST_TRACE_LOGFUNC(_Fmt...)
#define RFTEST_LOUD_LOGFUNC(_Fmt...)
#define RFTEST_TEMP_LOGFUNC(_Fmt...)

#define EMU_ERROR_LOGFUNC(_Fmt...)
#define EMU_WARN_LOGFUNC(_Fmt...)
#define EMU_INFO_LOGFUNC(_Fmt...)
#define EMU_STATE_LOGFUNC(_Fmt...)
#define EMU_EVENT_LOGFUNC(_Fmt...)
#define EMU_TRACE_LOGFUNC(_Fmt...)
#define EMU_LOUD_LOGFUNC(_Fmt...)
#define EMU_TEMP_LOGFUNC(_Fmt...)

#define HEM_ERROR_LOGFUNC(_Fmt...)
#define HEM_WARN_LOGFUNC(_Fmt...)
#define HEM_INFO_LOGFUNC(_Fmt...)
#define HEM_STATE_LOGFUNC(_Fmt...)
#define HEM_EVENT_LOGFUNC(_Fmt...)
#define HEM_TRACE_LOGFUNC(_Fmt...)
#define HEM_LOUD_LOGFUNC(_Fmt...)
#define HEM_TEMP_LOGFUNC(_Fmt...)

#define RLM_ERROR_LOGFUNC(_Fmt...)
#define RLM_WARN_LOGFUNC(_Fmt...)
#define RLM_INFO_LOGFUNC(_Fmt...)
#define RLM_STATE_LOGFUNC(_Fmt...)
#define RLM_EVENT_LOGFUNC(_Fmt...)
#define RLM_TRACE_LOGFUNC(_Fmt...)
#define RLM_LOUD_LOGFUNC(_Fmt...)
#define RLM_TEMP_LOGFUNC(_Fmt...)

#define MEM_ERROR_LOGFUNC(_Fmt...)
#define MEM_WARN_LOGFUNC(_Fmt...)
#define MEM_INFO_LOGFUNC(_Fmt...)
#define MEM_STATE_LOGFUNC(_Fmt...)
#define MEM_EVENT_LOGFUNC(_Fmt...)
#define MEM_TRACE_LOGFUNC(_Fmt...)
#define MEM_LOUD_LOGFUNC(_Fmt...)
#define MEM_TEMP_LOGFUNC(_Fmt...)

#define CNM_ERROR_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define CNM_WARN_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define CNM_INFO_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define CNM_STATE_LOGFUNC(_Fmt...)
#define CNM_EVENT_LOGFUNC(_Fmt...)
#define CNM_TRACE_LOGFUNC(_Fmt...)
#define CNM_LOUD_LOGFUNC(_Fmt...)
#define CNM_TEMP_LOGFUNC(_Fmt...)

#define RSN_ERROR_LOGFUNC(_Fmt...)
#define RSN_WARN_LOGFUNC(_Fmt...)
#define RSN_INFO_LOGFUNC(_Fmt...)
#define RSN_STATE_LOGFUNC(_Fmt...)
#define RSN_EVENT_LOGFUNC(_Fmt...)
#define RSN_TRACE_LOGFUNC(_Fmt...)
#define RSN_LOUD_LOGFUNC(_Fmt...)
#define RSN_TEMP_LOGFUNC(_Fmt...)

#define BSS_ERROR_LOGFUNC(_Fmt...)
#define BSS_WARN_LOGFUNC(_Fmt...)
#define BSS_INFO_LOGFUNC(_Fmt...)
#define BSS_STATE_LOGFUNC(_Fmt...)
#define BSS_EVENT_LOGFUNC(_Fmt...)
#define BSS_TRACE_LOGFUNC(_Fmt...)
#define BSS_LOUD_LOGFUNC(_Fmt...)
#define BSS_TEMP_LOGFUNC(_Fmt...)

#define SCN_ERROR_LOGFUNC(_Fmt...)
#define SCN_WARN_LOGFUNC(_Fmt...)
#define SCN_INFO_LOGFUNC(_Fmt...)
#define SCN_STATE_LOGFUNC(_Fmt...)
#define SCN_EVENT_LOGFUNC(_Fmt...)
#define SCN_TRACE_LOGFUNC(_Fmt...)
#define SCN_LOUD_LOGFUNC(_Fmt...)
#define SCN_TEMP_LOGFUNC(_Fmt...)

#define SAA_ERROR_LOGFUNC(_Fmt...)
#define SAA_WARN_LOGFUNC(_Fmt...)
#define SAA_INFO_LOGFUNC(_Fmt...)
#define SAA_STATE_LOGFUNC(_Fmt...)
#define SAA_EVENT_LOGFUNC(_Fmt...)
#define SAA_TRACE_LOGFUNC(_Fmt...)
#define SAA_LOUD_LOGFUNC(_Fmt...)
#define SAA_TEMP_LOGFUNC(_Fmt...)

#define P2P_ERROR_LOGFUNC(_Fmt...)
#define P2P_WARN_LOGFUNC(_Fmt...)
#define P2P_INFO_LOGFUNC(_Fmt...)
#define P2P_STATE_LOGFUNC(_Fmt...)
#define P2P_EVENT_LOGFUNC(_Fmt...)
#define P2P_TRACE_LOGFUNC(_Fmt...)
#define P2P_LOUD_LOGFUNC(_Fmt...)
#define P2P_TEMP_LOGFUNC(_Fmt...)

#define QM_ERROR_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define QM_WARN_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define QM_INFO_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define QM_STATE_LOGFUNC(_Fmt...)
#define QM_EVENT_LOGFUNC(_Fmt...)
#define QM_TRACE_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define QM_LOUD_LOGFUNC(_Fmt...)
#define QM_TEMP_LOGFUNC(_Fmt...)

#define SEC_ERROR_LOGFUNC(_Fmt...)
#define SEC_WARN_LOGFUNC(_Fmt...)
#define SEC_INFO_LOGFUNC(_Fmt...)
#define SEC_STATE_LOGFUNC(_Fmt...)
#define SEC_EVENT_LOGFUNC(_Fmt...)
#define SEC_TRACE_LOGFUNC(_Fmt...)
#define SEC_LOUD_LOGFUNC(_Fmt...)
#define SEC_TEMP_LOGFUNC(_Fmt...)

#define BOW_ERROR_LOGFUNC(_Fmt...)
#define BOW_WARN_LOGFUNC(_Fmt...)
#define BOW_INFO_LOGFUNC(_Fmt...)
#define BOW_STATE_LOGFUNC(_Fmt...)
#define BOW_EVENT_LOGFUNC(_Fmt...)
#define BOW_TRACE_LOGFUNC(_Fmt...)
#define BOW_LOUD_LOGFUNC(_Fmt...)
#define BOW_TEMP_LOGFUNC(_Fmt...)

#define HAL_ERROR_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define HAL_WARN_LOGFUNC(_Fmt...)
#define HAL_INFO_LOGFUNC(_Fmt...)
#define HAL_STATE_LOGFUNC(_Fmt...)
#define HAL_EVENT_LOGFUNC(_Fmt...)
#define HAL_TRACE_LOGFUNC(_Fmt...)
#define HAL_LOUD_LOGFUNC(_Fmt...)
#define HAL_TEMP_LOGFUNC(_Fmt...)

#define WAPI_ERROR_LOGFUNC(_Fmt...)
#define WAPI_WARN_LOGFUNC(_Fmt...)
#define WAPI_INFO_LOGFUNC(_Fmt...)
#define WAPI_STATE_LOGFUNC(_Fmt...)
#define WAPI_EVENT_LOGFUNC(_Fmt...)
#define WAPI_TRACE_LOGFUNC(_Fmt...)
#define WAPI_LOUD_LOGFUNC(_Fmt...)
#define WAPI_TEMP_LOGFUNC(_Fmt...)

#define TDLS_ERROR_LOGFUNC(_Fmt...) kalPrint(_Fmt)
#define TDLS_WARN_LOGFUNC(_Fmt...)  kalPrint(_Fmt)
#define TDLS_INFO_LOGFUNC(_Fmt...)  kalPrint(_Fmt)
#define TDLS_STATE_LOGFUNC(_Fmt...)
#define TDLS_EVENT_LOGFUNC(_Fmt...)
#define TDLS_TRACE_LOGFUNC(_Fmt...)
#define TDLS_LOUD_LOGFUNC(_Fmt...)
#define TDLS_TEMP_LOGFUNC(_Fmt...)

#define SW1_ERROR_LOGFUNC(_Fmt...)
#define SW1_WARN_LOGFUNC(_Fmt...)
#define SW1_INFO_LOGFUNC(_Fmt...)
#define SW1_STATE_LOGFUNC(_Fmt...)
#define SW1_EVENT_LOGFUNC(_Fmt...)
#define SW1_TRACE_LOGFUNC(_Fmt...)
#define SW1_LOUD_LOGFUNC(_Fmt...)
#define SW1_TEMP_LOGFUNC(_Fmt...)

#define SW2_ERROR_LOGFUNC(_Fmt...)
#define SW2_WARN_LOGFUNC(_Fmt...)
#define SW2_INFO_LOGFUNC(_Fmt...)
#define SW2_STATE_LOGFUNC(_Fmt...)
#define SW2_EVENT_LOGFUNC(_Fmt...)
#define SW2_TRACE_LOGFUNC(_Fmt...)
#define SW2_LOUD_LOGFUNC(_Fmt...)
#define SW2_TEMP_LOGFUNC(_Fmt...)

#define SW3_ERROR_LOGFUNC(_Fmt...)
#define SW3_WARN_LOGFUNC(_Fmt...)
#define SW3_INFO_LOGFUNC(_Fmt...)
#define SW3_STATE_LOGFUNC(_Fmt...)
#define SW3_EVENT_LOGFUNC(_Fmt...)
#define SW3_TRACE_LOGFUNC(_Fmt...)
#define SW3_LOUD_LOGFUNC(_Fmt...)
#define SW3_TEMP_LOGFUNC(_Fmt...)

#define SW4_ERROR_LOGFUNC(_Fmt...)
#define SW4_WARN_LOGFUNC(_Fmt...)
#define SW4_INFO_LOGFUNC(_Fmt...)
#define SW4_STATE_LOGFUNC(_Fmt...)
#define SW4_EVENT_LOGFUNC(_Fmt...)
#define SW4_TRACE_LOGFUNC(_Fmt...)
#define SW4_LOUD_LOGFUNC(_Fmt...)
#define SW4_TEMP_LOGFUNC(_Fmt...)
#endif

#define kalBreakPoint() \
do { \
	BUG(); \
	panic("Oops"); \
} while (0)

#if CFG_ENABLE_AEE_MSG
#define kalSendAeeException                         aee_kernel_exception
#define kalSendAeeWarning                           aee_kernel_warning
#define kalSendAeeReminding                         aee_kernel_reminding
#else
#define kalSendAeeException(_module, _desc, ...)
#define kalSendAeeWarning(_module, _desc, ...)
#define kalSendAeeReminding(_module, _desc, ...)
#endif

#define PRINTF_ARG(...)                             __VA_ARGS__
#define SPRINTF(buf, arg)                           {buf += sprintf((char *)(buf), PRINTF_ARG arg); }

#define USEC_TO_SYSTIME(_usec)      ((_usec) / USEC_PER_MSEC)
#define MSEC_TO_SYSTIME(_msec)      (_msec)

#define MSEC_TO_JIFFIES(_msec)      msecs_to_jiffies(_msec)

#define KAL_HALT_LOCK_TIMEOUT_NORMAL_CASE		3000 /* 3s */
/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/* Routines in gl_kal.c                                                       */
/*----------------------------------------------------------------------------*/
VOID
kalAcquireSpinLock(IN P_GLUE_INFO_T prGlueInfo,
		   IN ENUM_SPIN_LOCK_CATEGORY_E rLockCategory, OUT unsigned long *pu4Flags);

VOID kalReleaseSpinLock(IN P_GLUE_INFO_T prGlueInfo, IN ENUM_SPIN_LOCK_CATEGORY_E rLockCategory, IN UINT_32 u4Flags);

VOID kalUpdateMACAddress(IN P_GLUE_INFO_T prGlueInfo, IN PUINT_8 pucMacAddr);

VOID kalPacketFree(IN P_GLUE_INFO_T prGlueInfo, IN PVOID pvPacket);

PVOID kalPacketAlloc(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Size, OUT PUINT_8 *ppucData);

VOID kalOsTimerInitialize(IN P_GLUE_INFO_T prGlueInfo, IN PVOID prTimerHandler);

BOOLEAN kalSetTimer(IN P_GLUE_INFO_T prGlueInfo, IN OS_SYSTIME rInterval);

WLAN_STATUS
kalProcessRxPacket(IN P_GLUE_INFO_T prGlueInfo, IN PVOID pvPacket, IN PUINT_8 pucPacketStart, IN UINT_32 u4PacketLen,
		   /* IN PBOOLEAN           pfgIsRetain, */
		   IN BOOLEAN fgIsRetain, IN ENUM_CSUM_RESULT_T aeCSUM[]
);

WLAN_STATUS kalRxIndicatePkts(IN P_GLUE_INFO_T prGlueInfo, IN PVOID apvPkts[], IN UINT_8 ucPktNum);

VOID
kalIndicateStatusAndComplete(IN P_GLUE_INFO_T prGlueInfo, IN WLAN_STATUS eStatus, IN PVOID pvBuf, IN UINT_32 u4BufLen);

VOID
kalUpdateReAssocReqInfo(IN P_GLUE_INFO_T prGlueInfo,
			IN PUINT_8 pucFrameBody, IN UINT_32 u4FrameBodyLen, IN BOOLEAN fgReassocRequest);

VOID kalUpdateReAssocRspInfo(IN P_GLUE_INFO_T prGlueInfo, IN PUINT_8 pucFrameBody, IN UINT_32 u4FrameBodyLen);

#if CFG_TX_FRAGMENT
BOOLEAN
kalQueryTxPacketHeader(IN P_GLUE_INFO_T prGlueInfo,
		       IN PVOID pvPacket, OUT PUINT_16 pu2EtherTypeLen, OUT PUINT_8 pucEthDestAddr);
#endif /* CFG_TX_FRAGMENT */

VOID kalSendCompleteAndAwakeQueue(IN P_GLUE_INFO_T prGlueInfo, IN PVOID pvPacket);

#if CFG_TCP_IP_CHKSUM_OFFLOAD
VOID kalQueryTxChksumOffloadParam(IN PVOID pvPacket, OUT PUINT_8 pucFlag);

VOID kalUpdateRxCSUMOffloadParam(IN PVOID pvPacket, IN ENUM_CSUM_RESULT_T eCSUM[]
);
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */

BOOLEAN kalRetrieveNetworkAddress(IN P_GLUE_INFO_T prGlueInfo, IN OUT PARAM_MAC_ADDRESS *prMacAddr);

VOID
kalReadyOnChannel(IN P_GLUE_INFO_T prGlueInfo,
		  IN UINT_64 u8Cookie,
		  IN ENUM_BAND_T eBand, IN ENUM_CHNL_EXT_T eSco, IN UINT_8 ucChannelNum, IN UINT_32 u4DurationMs);

VOID
kalRemainOnChannelExpired(IN P_GLUE_INFO_T prGlueInfo,
			  IN UINT_64 u8Cookie, IN ENUM_BAND_T eBand, IN ENUM_CHNL_EXT_T eSco, IN UINT_8 ucChannelNum);

VOID
kalIndicateMgmtTxStatus(IN P_GLUE_INFO_T prGlueInfo,
			IN UINT_64 u8Cookie, IN BOOLEAN fgIsAck, IN PUINT_8 pucFrameBuf, IN UINT_32 u4FrameLen);

VOID kalIndicateRxMgmtFrame(IN P_GLUE_INFO_T prGlueInfo, IN P_SW_RFB_T prSwRfb);

/*----------------------------------------------------------------------------*/
/* Routines in interface - ehpi/sdio.c                                                       */
/*----------------------------------------------------------------------------*/
BOOLEAN kalDevRegRead(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Register, OUT PUINT_32 pu4Value);

BOOLEAN kalDevRegWrite(P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Register, IN UINT_32 u4Value);

BOOLEAN
kalDevPortRead(IN P_GLUE_INFO_T prGlueInfo,
	       IN UINT_16 u2Port, IN UINT_32 u2Len, OUT PUINT_8 pucBuf, IN UINT_32 u2ValidOutBufSize);

BOOLEAN
kalDevPortWrite(P_GLUE_INFO_T prGlueInfo,
		IN UINT_16 u2Port, IN UINT_32 u2Len, IN PUINT_8 pucBuf, IN UINT_32 u2ValidInBufSize);

BOOLEAN kalDevWriteWithSdioCmd52(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Addr, IN UINT_8 ucData);

void kalDevLoopbkAuto(IN GLUE_INFO_T *GlueInfo);

#if CFG_SUPPORT_EXT_CONFIG
UINT_32 kalReadExtCfg(IN P_GLUE_INFO_T prGlueInfo);
#endif

BOOLEAN
kalQoSFrameClassifierAndPacketInfo(IN P_GLUE_INFO_T prGlueInfo,
				   IN P_NATIVE_PACKET prPacket,
				   OUT PUINT_8 pucPriorityParam,
				   OUT PUINT_32 pu4PacketLen,
				   OUT PUINT_8 pucEthDestAddr,
				   OUT PBOOLEAN pfgIs1X,
				   OUT PBOOLEAN pfgIsPAL, OUT PUINT_8 pucNetworkType,
				   OUT PVOID prGenUse);

VOID
kalOidComplete(IN P_GLUE_INFO_T prGlueInfo,
	       IN BOOLEAN fgSetQuery, IN UINT_32 u4SetQueryInfoLen, IN WLAN_STATUS rOidStatus);

WLAN_STATUS
kalIoctl(IN P_GLUE_INFO_T prGlueInfo,
	 IN PFN_OID_HANDLER_FUNC pfnOidHandler,
	 IN PVOID pvInfoBuf,
	 IN UINT_32 u4InfoBufLen,
	 IN BOOLEAN fgRead, IN BOOLEAN fgWaitResp, IN BOOLEAN fgCmd, IN BOOLEAN fgIsP2pOid, OUT PUINT_32 pu4QryInfoLen);

VOID kalHandleAssocInfo(IN P_GLUE_INFO_T prGlueInfo, IN P_EVENT_ASSOC_INFO prAssocInfo);

#if CFG_ENABLE_FW_DOWNLOAD

PVOID kalFirmwareImageMapping(IN P_GLUE_INFO_T prGlueInfo, OUT PPVOID ppvMapFileBuf, OUT PUINT_32 pu4FileLength);

VOID kalFirmwareImageUnmapping(IN P_GLUE_INFO_T prGlueInfo, IN PVOID prFwHandle, IN PVOID pvMapFileBuf);
#endif

/*----------------------------------------------------------------------------*/
/* Card Removal Check                                                         */
/*----------------------------------------------------------------------------*/
BOOLEAN kalIsCardRemoved(IN P_GLUE_INFO_T prGlueInfo);

/*----------------------------------------------------------------------------*/
/* TX                                                                         */
/*----------------------------------------------------------------------------*/
VOID kalFlushPendingTxPackets(IN P_GLUE_INFO_T prGlueInfo);

/*----------------------------------------------------------------------------*/
/* Media State Indication                                                     */
/*----------------------------------------------------------------------------*/
ENUM_PARAM_MEDIA_STATE_T kalGetMediaStateIndicated(IN P_GLUE_INFO_T prGlueInfo);

VOID kalSetMediaStateIndicated(IN P_GLUE_INFO_T prGlueInfo, IN ENUM_PARAM_MEDIA_STATE_T eParamMediaStateIndicate);

/*----------------------------------------------------------------------------*/
/* OID handling                                                               */
/*----------------------------------------------------------------------------*/
VOID kalOidCmdClearance(IN P_GLUE_INFO_T prGlueInfo);

VOID kalOidClearance(IN P_GLUE_INFO_T prGlueInfo);

VOID kalEnqueueCommand(IN P_GLUE_INFO_T prGlueInfo, IN P_QUE_ENTRY_T prQueueEntry);

#if CFG_ENABLE_BT_OVER_WIFI
/*----------------------------------------------------------------------------*/
/* Bluetooth over Wi-Fi handling                                              */
/*----------------------------------------------------------------------------*/
VOID kalIndicateBOWEvent(IN P_GLUE_INFO_T prGlueInfo, IN P_AMPC_EVENT prEvent);

ENUM_BOW_DEVICE_STATE kalGetBowState(IN P_GLUE_INFO_T prGlueInfo, IN PARAM_MAC_ADDRESS rPeerAddr);

BOOLEAN kalSetBowState(IN P_GLUE_INFO_T prGlueInfo, IN ENUM_BOW_DEVICE_STATE eBowState, PARAM_MAC_ADDRESS rPeerAddr);

ENUM_BOW_DEVICE_STATE kalGetBowGlobalState(IN P_GLUE_INFO_T prGlueInfo);

UINT_32 kalGetBowFreqInKHz(IN P_GLUE_INFO_T prGlueInfo);

UINT_8 kalGetBowRole(IN P_GLUE_INFO_T prGlueInfo, IN PARAM_MAC_ADDRESS rPeerAddr);

VOID kalSetBowRole(IN P_GLUE_INFO_T prGlueInfo, IN UINT_8 ucRole, IN PARAM_MAC_ADDRESS rPeerAddr);

UINT_8 kalGetBowAvailablePhysicalLinkCount(IN P_GLUE_INFO_T prGlueInfo);

#if CFG_BOW_SEPARATE_DATA_PATH
/*----------------------------------------------------------------------------*/
/* Bluetooth over Wi-Fi Net Device Init/Uninit                                */
/*----------------------------------------------------------------------------*/
BOOLEAN kalInitBowDevice(IN P_GLUE_INFO_T prGlueInfo, IN const char *prDevName);

BOOLEAN kalUninitBowDevice(IN P_GLUE_INFO_T prGlueInfo);
#endif /* CFG_BOW_SEPARATE_DATA_PATH */
#endif /* CFG_ENABLE_BT_OVER_WIFI */

/*----------------------------------------------------------------------------*/
/* Firmware Download Handling                                                 */
/*----------------------------------------------------------------------------*/
UINT_32 kalGetFwStartAddress(IN P_GLUE_INFO_T prGlueInfo);

UINT_32 kalGetFwLoadAddress(IN P_GLUE_INFO_T prGlueInfo);

/*----------------------------------------------------------------------------*/
/* Security Frame Clearance                                                   */
/*----------------------------------------------------------------------------*/
VOID kalClearSecurityFrames(IN P_GLUE_INFO_T prGlueInfo);

VOID kalClearSecurityFramesByNetType(IN P_GLUE_INFO_T prGlueInfo, IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx);

VOID kalSecurityFrameSendComplete(IN P_GLUE_INFO_T prGlueInfo, IN PVOID pvPacket, IN WLAN_STATUS rStatus);

/*----------------------------------------------------------------------------*/
/* Management Frame Clearance                                                 */
/*----------------------------------------------------------------------------*/
VOID kalClearMgmtFrames(IN P_GLUE_INFO_T prGlueInfo);

VOID kalClearMgmtFramesByNetType(IN P_GLUE_INFO_T prGlueInfo, IN ENUM_NETWORK_TYPE_INDEX_T eNetworkTypeIdx);

UINT_32 kalGetTxPendingFrameCount(IN P_GLUE_INFO_T prGlueInfo);

UINT_32 kalGetTxPendingCmdCount(IN P_GLUE_INFO_T prGlueInfo);

BOOLEAN kalSetTimer(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Interval);

BOOLEAN kalCancelTimer(IN P_GLUE_INFO_T prGlueInfo);

VOID kalScanDone(IN P_GLUE_INFO_T prGlueInfo, IN ENUM_KAL_NETWORK_TYPE_INDEX_T eNetTypeIdx, IN WLAN_STATUS status);

UINT_32 kalRandomNumber(VOID);

VOID kalTimeoutHandler(ULONG arg);

VOID kalSetEvent(P_GLUE_INFO_T pr);

/*----------------------------------------------------------------------------*/
/* NVRAM/Registry Service                                                     */
/*----------------------------------------------------------------------------*/
BOOLEAN kalIsConfigurationExist(IN P_GLUE_INFO_T prGlueInfo);

P_REG_INFO_T kalGetConfiguration(IN P_GLUE_INFO_T prGlueInfo);

VOID
kalGetConfigurationVersion(IN P_GLUE_INFO_T prGlueInfo,
			   OUT PUINT_16 pu2Part1CfgOwnVersion,
			   OUT PUINT_16 pu2Part1CfgPeerVersion,
			   OUT PUINT_16 pu2Part2CfgOwnVersion, OUT PUINT_16 pu2Part2CfgPeerVersion);

BOOLEAN kalCfgDataRead16(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Offset, OUT PUINT_16 pu2Data);

BOOLEAN kalCfgDataWrite16(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Offset, IN UINT_16 u2Data);

/*----------------------------------------------------------------------------*/
/* WSC Connection                                                     */
/*----------------------------------------------------------------------------*/
BOOLEAN kalWSCGetActiveState(IN P_GLUE_INFO_T prGlueInfo);

/*----------------------------------------------------------------------------*/
/* RSSI Updating                                                              */
/*----------------------------------------------------------------------------*/
VOID
kalUpdateRSSI(IN P_GLUE_INFO_T prGlueInfo,
	      IN ENUM_KAL_NETWORK_TYPE_INDEX_T eNetTypeIdx, IN INT_8 cRssi, IN INT_8 cLinkQuality);

/*----------------------------------------------------------------------------*/
/* I/O Buffer Pre-allocation                                                  */
/*----------------------------------------------------------------------------*/
BOOLEAN kalInitIOBuffer(VOID);

VOID kalUninitIOBuffer(VOID);

PVOID kalAllocateIOBuffer(IN UINT_32 u4AllocSize);

VOID kalReleaseIOBuffer(IN PVOID pvAddr, IN UINT_32 u4Size);

VOID
kalGetChannelList(IN P_GLUE_INFO_T prGlueInfo,
		  IN ENUM_BAND_T eSpecificBand,
		  IN UINT_8 ucMaxChannelNum, IN PUINT_8 pucNumOfChannel, IN P_RF_CHANNEL_INFO_T paucChannelList);

BOOLEAN kalIsAPmode(IN P_GLUE_INFO_T prGlueInfo);

ULONG kalIOPhyAddrGet(IN ULONG VirtAddr);

VOID kalDmaBufGet(OUT VOID **VirtAddr, OUT VOID **PhyAddr);

#if CFG_SUPPORT_802_11W
/*----------------------------------------------------------------------------*/
/* 802.11W                                                                    */
/*----------------------------------------------------------------------------*/
UINT_32 kalGetMfpSetting(IN P_GLUE_INFO_T prGlueInfo);
#endif

UINT_32 kalWriteToFile(const PUINT_8 pucPath, BOOLEAN fgDoAppend, PUINT_8 pucData, UINT_32 u4Size);

/*----------------------------------------------------------------------------*/
/* NL80211                                                                    */
/*----------------------------------------------------------------------------*/
VOID
kalIndicateBssInfo(IN P_GLUE_INFO_T prGlueInfo,
		   IN PUINT_8 pucFrameBuf, IN UINT_32 u4BufLen, IN UINT_8 ucChannelNum, IN INT_32 i4SignalStrength);

/*----------------------------------------------------------------------------*/
/* PNO Support                                                                */
/*----------------------------------------------------------------------------*/
VOID kalSchedScanResults(IN P_GLUE_INFO_T prGlueInfo);

VOID kalSchedScanStopped(IN P_GLUE_INFO_T prGlueInfo);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

int tx_thread(void *data);

VOID kalHifAhbKalWakeLockTimeout(IN P_GLUE_INFO_T prGlueInfo);
VOID kalMetProfilingStart(IN P_GLUE_INFO_T prGlueInfo, IN struct sk_buff *prSkb);
VOID kalMetProfilingFinish(IN P_ADAPTER_T prAdapter, IN P_MSDU_INFO_T prMsduInfo);
int kalMetInitProcfs(IN P_GLUE_INFO_T prGlueInfo);
int kalMetRemoveProcfs(void);

UINT_64 kalGetBootTime(void);

INT_32 kalReadToFile(const PUINT_8 pucPath, PUINT_8 pucData, UINT_32 u4Size, PUINT_32 pu4ReadSize);
#if CFG_SUPPORT_WAKEUP_REASON_DEBUG
BOOLEAN kalIsWakeupByWlan(P_ADAPTER_T  prAdapter);
#endif
INT_32 kalHaltLock(UINT_32 waitMs);
INT_32 kalHaltTryLock(VOID);
VOID kalHaltUnlock(VOID);
VOID kalSetHalted(BOOLEAN fgHalt);
BOOLEAN kalIsHalted(VOID);

INT32 kalPerMonInit(IN P_GLUE_INFO_T prGlueInfo);
INT32 kalPerMonDisable(IN P_GLUE_INFO_T prGlueInfo);
INT32 kalPerMonEnable(IN P_GLUE_INFO_T prGlueInfo);
INT32 kalPerMonStart(IN P_GLUE_INFO_T prGlueInfo);
INT32 kalPerMonStop(IN P_GLUE_INFO_T prGlueInfo);
INT32 kalPerMonDestroy(IN P_GLUE_INFO_T prGlueInfo);
VOID kalPerMonHandler(IN P_ADAPTER_T prAdapter, ULONG ulParam);
INT32 kalBoostCpu(UINT_32 core_num);

#endif /* _GL_KAL_H */
