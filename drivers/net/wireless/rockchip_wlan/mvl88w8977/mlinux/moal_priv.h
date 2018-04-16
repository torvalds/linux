
/** @file moal_priv.h
 *
 * @brief This file contains definition for extended private IOCTL call.
 *
 * Copyright (C) 2008-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

/********************************************************
Change log:
    10/31/2008: initial version
********************************************************/

#ifndef _WOAL_PRIV_H_
#define _WOAL_PRIV_H_

/** 2K bytes */
#define WOAL_2K_BYTES       2000

/** PRIVATE CMD ID */
#define WOAL_IOCTL                  (SIOCIWFIRSTPRIV)	/* 0x8BE0 defined in wireless.h */

/** Private command ID to set one int/get word char */
#define WOAL_SETONEINT_GETWORDCHAR  (WOAL_IOCTL + 1)
/** Private command ID to get version */
#define WOAL_VERSION                1
/** Private command ID to get extended version */
#define WOAL_VEREXT                 2

/** Private command ID to set/get none */
#define WOAL_SETNONE_GETNONE        (WOAL_IOCTL + 2)
/** Private command ID for warm reset */
#define WOAL_WARMRESET              1

/**
 * Linux Kernels later 3.9 use CONFIG_PM_RUNTIME instead of
 * CONFIG_USB_SUSPEND
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
#ifdef CONFIG_PM
#ifndef CONFIG_USB_SUSPEND
#define CONFIG_USB_SUSPEND
#endif
#endif
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0) */
#ifdef CONFIG_PM_RUNTIME
#ifndef CONFIG_USB_SUSPEND
#define CONFIG_USB_SUSPEND
#endif
#endif
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0) */
#endif

/** Private command ID to clear 11d chan table */
#define WOAL_11D_CLR_CHAN_TABLE     4

/** Private command ID to set/get sixteen int */
#define WOAL_SET_GET_SIXTEEN_INT    (WOAL_IOCTL + 3)
/** Private command ID to set/get TX power configurations */
#define WOAL_TX_POWERCFG            1
#ifdef DEBUG_LEVEL1
/** Private command ID to set/get driver debug */
#define WOAL_DRV_DBG                2
#endif
/** Private command ID to set/get beacon interval */
#define WOAL_BEACON_INTERVAL        3
/** Private command ID to set/get ATIM window */
#define WOAL_ATIM_WINDOW            4
/** Private command ID to get RSSI */
#define WOAL_SIGNAL                 5
/** Private command ID to set/get Deep Sleep mode */
#define WOAL_DEEP_SLEEP             7
/** Private command ID for 11n ht configration */
#define WOAL_11N_TX_CFG             8
/** Private command ID for 11n usr ht configration */
#define WOAL_11N_HTCAP_CFG          9
/** Private command ID for TX Aggregation */
#define WOAL_PRIO_TBL               10
/** Private command ID for Updating ADDBA variables */
#define WOAL_ADDBA_UPDT             11
/** Private command ID to set/get Host Sleep configuration */
#define WOAL_HS_CFG                 12
/** Private command ID to set Host Sleep parameters */
#define WOAL_HS_SETPARA             13
/** Private command ID to read/write registers */
#define WOAL_REG_READ_WRITE         14
/** Private command ID to set/get band/adhocband */
#define WOAL_BAND_CFG               15
/** Private command ID for TX Aggregation */
#define WOAL_11N_AMSDU_AGGR_CTRL    17
/** Private command ID to set/get Inactivity timeout */
#define WOAL_INACTIVITY_TIMEOUT_EXT 18
/** Private command ID to turn on/off sdio clock */
#define WOAL_SDIO_CLOCK             19
/** Private command ID to read/write Command 52 */
#define WOAL_CMD_52RDWR             20
/** Private command ID to set/get scan configuration parameter */
#define WOAL_SCAN_CFG               21
/** Private command ID to set/get PS configuration parameter */
#define WOAL_PS_CFG                 22
/** Private command ID to read/write memory */
#define WOAL_MEM_READ_WRITE         23
#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
/** Private command ID to control SDIO MP-A */
#define WOAL_SDIO_MPA_CTRL          25
#endif
/** Private command ID for Updating ADDBA variables */
#define WOAL_ADDBA_REJECT           27
/** Private command ID to set/get sleep parameters */
#define WOAL_SLEEP_PARAMS           28
/** Private command ID to set/get network monitor */
#define WOAL_NET_MONITOR            30
/** Private command ID to set/get TX BF capabilities */
#define WOAL_TX_BF_CAP              31
#if defined(DFS_TESTING_SUPPORT)
/** Private command ID to set/get dfs testing settings */
#define WOAL_DFS_TESTING            33
#endif
/** Private command ID to set/get CFP table codes */
#define WOAL_CFP_CODE               34
/** Private command ID to set/get tx/rx antenna */
#define WOAL_SET_GET_TX_RX_ANT      35
/** Private command ID to set/get management frame passthru mask */
#define WOAL_MGMT_FRAME_CTRL        36

/** Private command ID to configure gpio independent reset */
#define WOAL_IND_RST_CFG            37

/** Private command ID to set one int/get one int */
#define WOAL_SETONEINT_GETONEINT    (WOAL_IOCTL + 5)
/** Private command ID to set/get Tx rate */
#define WOAL_SET_GET_TXRATE         1
/** Private command ID to set/get region code */
#define WOAL_SET_GET_REGIONCODE     2
/** Private command ID to turn on/off radio */
#define WOAL_SET_RADIO              3
/** Private command ID to enable WMM */
#define WOAL_WMM_ENABLE             4
/** Private command ID to enable 802.11D */
#define WOAL_11D_ENABLE             5
/** Private command ID to set/get QoS configuration */
#define WOAL_SET_GET_QOS_CFG        7
#if defined(REASSOCIATION)
/** Private command ID to set/get reassociation setting */
#define WOAL_SET_GET_REASSOC        9
#endif /* REASSOCIATION */
/** Private command ID for Updating Transmit buffer configration */
#define WOAL_TXBUF_CFG              10
/** Private command ID to set/get WWS mode */
#define	WOAL_SET_GET_WWS_CFG        12
/** Private command ID to set/get sleep period */
#define WOAL_SLEEP_PD               13
/** Private command ID to set/get firmware wakeup method */
#define WOAL_FW_WAKEUP_METHOD       15
/** Private command ID to set/get auth type */
#define WOAL_AUTH_TYPE              18
/** Private command ID to set/get port control */
#define WOAL_PORT_CTRL              19
/** Private command ID for coalesced status */
#define WOAL_COALESCING_STATUS      20
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
/** Private command ID for set/get BSS role */
#define WOAL_SET_GET_BSS_ROLE       21
#endif
#endif
/** Private command ID for set/get 11h local power constraint */
#define WOAL_SET_GET_11H_LOCAL_PWR_CONSTRAINT 22
/** Private command ID to set/get MAC control */
#define WOAL_MAC_CONTROL            24
/** Private command ID to get thermal value */
#define WOAL_THERMAL                25

/** Private command ID to get log */
#define WOALGETLOG                  (WOAL_IOCTL + 7)

/** Private command ID to set a wext address variable */
#define WOAL_SETADDR_GETNONE        (WOAL_IOCTL + 8)
/** Private command ID to send deauthentication */
#define WOAL_DEAUTH                 1

/** Private command to get/set 256 chars */
#define WOAL_SET_GET_256_CHAR       (WOAL_IOCTL + 9)
/** Private command to read/write passphrase */
#define WOAL_PASSPHRASE             1
/** Private command to get/set Ad-Hoc AES */
#define WOAL_ADHOC_AES              2
#define WOAL_ASSOCIATE              3
/** Private command ID to get WMM queue status */
#define WOAL_WMM_QUEUE_STATUS       4
/** Private command ID to get Traffic stream status */
#define WOAL_WMM_TS_STATUS          5
#define WOAL_IP_ADDRESS             7
/** Private command ID to set/get TX bemaforming */
#define WOAL_TX_BF_CFG              8

/** Get log buffer size */
#define GETLOG_BUFSIZE              512

/** Private command ID to set none/get twelve chars*/
#define WOAL_SETNONE_GETTWELVE_CHAR (WOAL_IOCTL + 11)
/** Private command ID for WPS session */
#define WOAL_WPS_SESSION            1

/** Private command ID to set none/get four int */
#define WOAL_SETNONE_GET_FOUR_INT   (WOAL_IOCTL + 13)
/** Private command ID to get data rates */
#define WOAL_DATA_RATE              1
/** Private command ID to get E-Supplicant mode */
#define WOAL_ESUPP_MODE             2

/** Private command to get/set 64 ints */
#define WOAL_SET_GET_64_INT         (WOAL_IOCTL + 15)
/** Private command ID to set/get ECL system clock */
#define WOAL_ECL_SYS_CLOCK          1

/** Private command ID for hostcmd */
#define WOAL_HOST_CMD               (WOAL_IOCTL + 17)

/** Private command ID for arpfilter */
#define WOAL_ARP_FILTER             (WOAL_IOCTL + 19)

/** Private command ID to set ints and get chars */
#define WOAL_SET_INTS_GET_CHARS     (WOAL_IOCTL + 21)
/** Private command ID to read EEPROM data */
#define WOAL_READ_EEPROM            1

/** Private command ID to set/get 2K bytes */
#define WOAL_SET_GET_2K_BYTES       (WOAL_IOCTL + 23)

/** Private command ID to read/write Command 53 */
#define WOAL_CMD_53RDWR             2

/** Private command ID for setuserscan */
#define WOAL_SET_USER_SCAN          3
/** Private command ID for getscantable */
#define WOAL_GET_SCAN_TABLE         4
/** Private command ID for setuserscanext: async without wait */
#define WOAL_SET_USER_SCAN_EXT      5

/** Private command ID to request ADDTS */
#define WOAL_WMM_ADDTS              7
/** Private command ID to request DELTS */
#define WOAL_WMM_DELTS              8
/** Private command ID to queue configuration */
#define WOAL_WMM_QUEUE_CONFIG       9
/** Private command ID to queue stats */
#define WOAL_WMM_QUEUE_STATS        10
/** Private command ID to Bypass auth packet */
#define WOAL_BYPASSED_PACKET		11

#ifdef UAP_WEXT
/** The following command IDs are for Froyo app */
/** Private command ID to start driver */
#define WOAL_FROYO_START            (WOAL_IOCTL + 28)
/** Private command ID to reload FW */
#define WOAL_FROYO_WL_FW_RELOAD     (WOAL_IOCTL + 29)
/** Private command ID to stop driver */
#define WOAL_FROYO_STOP             (WOAL_IOCTL + 30)
#endif

/**
 * iwpriv ioctl handlers
 */
static const struct iw_priv_args woal_private_args[] = {
	{
	 WOAL_SETONEINT_GETWORDCHAR,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_CHAR | 128,
	 ""},
	{
	 WOAL_VERSION,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_CHAR | 128,
	 "version"},
	{
	 WOAL_VEREXT,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_CHAR | 128,
	 "verext"},
	{
	 WOAL_SETNONE_GETNONE,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_NONE,
	 ""},
	{
	 WOAL_WARMRESET,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_NONE,
	 "warmreset"},
	{
	 WOAL_SETONEINT_GETONEINT,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 ""},
	{
	 WOAL_SET_GET_TXRATE,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "txratecfg"},
	{
	 WOAL_SET_GET_REGIONCODE,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "regioncode"},
	{
	 WOAL_SET_RADIO,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "radioctrl"},
	{
	 WOAL_WMM_ENABLE,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "wmmcfg"},
	{
	 WOAL_11D_ENABLE,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "11dcfg"},
	{
	 WOAL_11D_CLR_CHAN_TABLE,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_NONE,
	 "11dclrtbl"},
	{
	 WOAL_SET_GET_QOS_CFG,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "qoscfg"},
	{
	 WOAL_SET_GET_WWS_CFG,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "wwscfg"},
#if defined(REASSOCIATION)
	{
	 WOAL_SET_GET_REASSOC,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "reassoctrl"},
#endif
	{
	 WOAL_TXBUF_CFG,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "txbufcfg"},
	{
	 WOAL_SLEEP_PD,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "sleeppd"},
	{
	 WOAL_FW_WAKEUP_METHOD,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "fwwakeupmethod"},
	{
	 WOAL_AUTH_TYPE,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "authtype"},
	{
	 WOAL_PORT_CTRL,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "port_ctrl"},
	{
	 WOAL_COALESCING_STATUS,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "coalesce_status"},
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
	{
	 WOAL_SET_GET_BSS_ROLE,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "bssrole"},
#endif
#endif
	{
	 WOAL_SET_GET_11H_LOCAL_PWR_CONSTRAINT,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "powercons"},
	{
	 WOAL_MAC_CONTROL,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "macctrl"},
	{
	 WOAL_THERMAL,
	 IW_PRIV_TYPE_INT | 1,
	 IW_PRIV_TYPE_INT | 1,
	 "thermal"},
	{
	 WOAL_SET_GET_SIXTEEN_INT,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 ""},
	{
	 WOAL_TX_POWERCFG,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "txpowercfg"},
#ifdef DEBUG_LEVEL1
	{
	 WOAL_DRV_DBG,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "drvdbg"},
#endif
	{
	 WOAL_BEACON_INTERVAL,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "bcninterval"},
	{
	 WOAL_ATIM_WINDOW,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "atimwindow"},
	{
	 WOAL_SIGNAL,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "getsignal"},
	{
	 WOAL_DEEP_SLEEP,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "deepsleep",
	 },
	{
	 WOAL_11N_TX_CFG,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "httxcfg"},
	{
	 WOAL_11N_HTCAP_CFG,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "htcapinfo"},
	{
	 WOAL_PRIO_TBL,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "aggrpriotbl"},
	{
	 WOAL_11N_AMSDU_AGGR_CTRL,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "amsduaggrctrl"},
	{
	 WOAL_ADDBA_UPDT,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "addbapara"},
	{
	 WOAL_ADDBA_REJECT,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "addbareject"},
	{
	 WOAL_TX_BF_CAP,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "httxbfcap"},
	{
	 WOAL_HS_CFG,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "hscfg"},
	{
	 WOAL_HS_SETPARA,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "hssetpara"},
	{
	 WOAL_REG_READ_WRITE,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "regrdwr"},
	{
	 WOAL_BAND_CFG,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "bandcfg"},
	{
	 WOAL_INACTIVITY_TIMEOUT_EXT,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "inactivityto"},
	{
	 WOAL_SDIO_CLOCK,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "sdioclock"},
	{
	 WOAL_CMD_52RDWR,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "sdcmd52rw"},
	{
	 WOAL_SCAN_CFG,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "scancfg"},
	{
	 WOAL_PS_CFG,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "pscfg"},
	{
	 WOAL_MEM_READ_WRITE,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "memrdwr"},
#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
	{
	 WOAL_SDIO_MPA_CTRL,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "mpactrl"},
#endif
	{
	 WOAL_SLEEP_PARAMS,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "sleepparams"},
	{
	 WOAL_NET_MONITOR,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "netmon"},
#if defined(DFS_TESTING_SUPPORT)
	{
	 WOAL_DFS_TESTING,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "dfstesting"},
#endif
	{
	 WOAL_MGMT_FRAME_CTRL,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "mgmtframectrl"},
	{
	 WOAL_CFP_CODE,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "cfpcode"},
	{
	 WOAL_SET_GET_TX_RX_ANT,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "antcfg"},
	{
	 WOAL_IND_RST_CFG,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_INT | 16,
	 "indrstcfg"},
	{
	 WOALGETLOG,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_CHAR | GETLOG_BUFSIZE,
	 "getlog"},
	{
	 WOAL_SETADDR_GETNONE,
	 IW_PRIV_TYPE_ADDR | 1,
	 IW_PRIV_TYPE_NONE,
	 ""},
	{
	 WOAL_DEAUTH,
	 IW_PRIV_TYPE_ADDR | 1,
	 IW_PRIV_TYPE_NONE,
	 "deauth"},
	{
	 WOAL_SET_GET_256_CHAR,
	 IW_PRIV_TYPE_CHAR | 256,
	 IW_PRIV_TYPE_CHAR | 256,
	 ""},
	{
	 WOAL_PASSPHRASE,
	 IW_PRIV_TYPE_CHAR | 256,
	 IW_PRIV_TYPE_CHAR | 256,
	 "passphrase"},
	{
	 WOAL_ADHOC_AES,
	 IW_PRIV_TYPE_CHAR | 256,
	 IW_PRIV_TYPE_CHAR | 256,
	 "adhocaes"},
	{
	 WOAL_ASSOCIATE,
	 IW_PRIV_TYPE_CHAR | 256,
	 IW_PRIV_TYPE_CHAR | 256,
	 "associate"},
	{
	 WOAL_WMM_QUEUE_STATUS,
	 IW_PRIV_TYPE_CHAR | 256,
	 IW_PRIV_TYPE_CHAR | 256,
	 "qstatus"},
	{
	 WOAL_WMM_TS_STATUS,
	 IW_PRIV_TYPE_CHAR | 256,
	 IW_PRIV_TYPE_CHAR | 256,
	 "ts_status"},
	{
	 WOAL_IP_ADDRESS,
	 IW_PRIV_TYPE_CHAR | 256,
	 IW_PRIV_TYPE_CHAR | 256,
	 "ipaddr"},
	{
	 WOAL_TX_BF_CFG,
	 IW_PRIV_TYPE_CHAR | 256,
	 IW_PRIV_TYPE_CHAR | 256,
	 "httxbfcfg"},
	{
	 WOAL_SETNONE_GETTWELVE_CHAR,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_CHAR | 12,
	 ""},
	{
	 WOAL_WPS_SESSION,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_CHAR | 12,
	 "wpssession"},
	{
	 WOAL_SETNONE_GET_FOUR_INT,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | 4,
	 ""},
	{
	 WOAL_DATA_RATE,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | 4,
	 "getdatarate"},
	{
	 WOAL_ESUPP_MODE,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | 4,
	 "esuppmode"},
	{
	 WOAL_SET_GET_64_INT,
	 IW_PRIV_TYPE_INT | 64,
	 IW_PRIV_TYPE_INT | 64,
	 ""},
	{
	 WOAL_ECL_SYS_CLOCK,
	 IW_PRIV_TYPE_INT | 64,
	 IW_PRIV_TYPE_INT | 64,
	 "sysclock"},
	{
	 WOAL_HOST_CMD,
	 IW_PRIV_TYPE_BYTE | 2047,
	 IW_PRIV_TYPE_BYTE | 2047,
	 "hostcmd"},
	{
	 WOAL_ARP_FILTER,
	 IW_PRIV_TYPE_BYTE | 2047,
	 IW_PRIV_TYPE_BYTE | 2047,
	 "arpfilter"},
	{
	 WOAL_SET_INTS_GET_CHARS,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_BYTE | 256,
	 ""},
	{
	 WOAL_READ_EEPROM,
	 IW_PRIV_TYPE_INT | 16,
	 IW_PRIV_TYPE_BYTE | 256,
	 "rdeeprom"},
	{
	 WOAL_SET_GET_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 ""},
	{
	 WOAL_CMD_53RDWR,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 "sdcmd53rw"},
	{
	 WOAL_SET_USER_SCAN,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 "setuserscan"},
	{
	 WOAL_GET_SCAN_TABLE,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 "getscantable"},
	{
	 WOAL_SET_USER_SCAN_EXT,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 "setuserscanext"},
	{
	 WOAL_WMM_ADDTS,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 "addts"},
	{
	 WOAL_WMM_DELTS,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 "delts"},
	{
	 WOAL_WMM_QUEUE_CONFIG,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 "qconfig"},
	{
	 WOAL_WMM_QUEUE_STATS,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 "qstats"},
	{
	 WOAL_BYPASSED_PACKET,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 IW_PRIV_TYPE_BYTE | WOAL_2K_BYTES,
	 "pb_bypass"},
#ifdef UAP_WEXT
	{
	 WOAL_FROYO_START,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_NONE,
	 "START"},
	{
	 WOAL_FROYO_STOP,
	 IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_NONE,
	 "STOP"},
	{
	 WOAL_FROYO_WL_FW_RELOAD,
	 IW_PRIV_TYPE_CHAR | 256,
	 IW_PRIV_TYPE_CHAR | 256,
	 "WL_FW_RELOAD"},
#endif
};

/** moal_802_11_rates  */
typedef struct _moal_802_11_rates {
	/** Num of rates */
	t_u8 num_of_rates;
	/** Rates */
	t_u8 rates[MLAN_SUPPORTED_RATES];
} moal_802_11_rates;

#if defined(STA_WEXT) || defined(UAP_WEXT)
int woal_wext_do_ioctl(struct net_device *dev, struct ifreq *req, int cmd);
#endif

#endif /* _WOAL_PRIV_H_ */
