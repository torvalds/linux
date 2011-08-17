//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _ATHDRV_LINUX_H
#define _ATHDRV_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif


/*
 * There are two types of ioctl's here: Standard ioctls and
 * eXtended ioctls.  All extended ioctls (XIOCTL) are multiplexed
 * off of the single ioctl command, AR6000_IOCTL_EXTENDED.  The
 * arguments for every XIOCTL starts with a 32-bit command word
 * that is used to select which extended ioctl is in use.  After
 * the command word are command-specific arguments.
 */

/* Linux standard Wireless Extensions, private ioctl interfaces */
#define IEEE80211_IOCTL_SETPARAM             (SIOCIWFIRSTPRIV+0)
#define IEEE80211_IOCTL_SETKEY               (SIOCIWFIRSTPRIV+1)
#define IEEE80211_IOCTL_DELKEY               (SIOCIWFIRSTPRIV+2)
#define IEEE80211_IOCTL_SETMLME              (SIOCIWFIRSTPRIV+3)
#define IEEE80211_IOCTL_ADDPMKID             (SIOCIWFIRSTPRIV+4)
#define IEEE80211_IOCTL_SETOPTIE             (SIOCIWFIRSTPRIV+5)
//#define IEEE80211_IOCTL_GETPARAM             (SIOCIWFIRSTPRIV+6)
//#define IEEE80211_IOCTL_SETWMMPARAMS         (SIOCIWFIRSTPRIV+7)
//#define IEEE80211_IOCTL_GETWMMPARAMS         (SIOCIWFIRSTPRIV+8)
//#define IEEE80211_IOCTL_GETOPTIE             (SIOCIWFIRSTPRIV+9)
//#define IEEE80211_IOCTL_SETAUTHALG           (SIOCIWFIRSTPRIV+10)
#define IEEE80211_IOCTL_LASTONE              (SIOCIWFIRSTPRIV+10)



/*                      ====WMI Ioctls====                                    */
/*
 *
 * Many ioctls simply provide WMI services to application code:
 * an application makes such an ioctl call with a set of arguments
 * that are packaged into the corresponding WMI message, and sent
 * to the Target.
 */

#define AR6000_IOCTL_WMI_GETREV              (SIOCIWFIRSTPRIV+11)
/*
 * arguments:
 *   ar6000_version *revision
 */

#define AR6000_IOCTL_WMI_SETPWR              (SIOCIWFIRSTPRIV+12)
/*
 * arguments:
 *   WMI_POWER_MODE_CMD pwrModeCmd (see include/wmi.h)
 * uses: WMI_SET_POWER_MODE_CMDID
 */

#define AR6000_IOCTL_WMI_SETSCAN             (SIOCIWFIRSTPRIV+13)
/*
 * arguments:
 *   WMI_SCAN_PARAMS_CMD scanParams (see include/wmi.h)
 * uses: WMI_SET_SCAN_PARAMS_CMDID
 */

#define AR6000_IOCTL_WMI_SETLISTENINT        (SIOCIWFIRSTPRIV+14)
/*
 * arguments:
 *   UINT32 listenInterval
 * uses: WMI_SET_LISTEN_INT_CMDID
 */

#define AR6000_IOCTL_WMI_SETBSSFILTER        (SIOCIWFIRSTPRIV+15)
/*
 * arguments:
 *   WMI_BSS_FILTER filter (see include/wmi.h)
 * uses: WMI_SET_BSS_FILTER_CMDID
 */

#define AR6000_IOCTL_WMI_SET_CHANNELPARAMS   (SIOCIWFIRSTPRIV+16)
/*
 * arguments:
 *   WMI_CHANNEL_PARAMS_CMD chParams
 * uses: WMI_SET_CHANNEL_PARAMS_CMDID
 */

#define AR6000_IOCTL_WMI_SET_PROBEDSSID      (SIOCIWFIRSTPRIV+17)
/*
 * arguments:
 *   WMI_PROBED_SSID_CMD probedSsids (see include/wmi.h)
 * uses: WMI_SETPROBED_SSID_CMDID
 */

#define AR6000_IOCTL_WMI_SET_PMPARAMS        (SIOCIWFIRSTPRIV+18)
/*
 * arguments:
 *   WMI_POWER_PARAMS_CMD powerParams (see include/wmi.h)
 * uses: WMI_SET_POWER_PARAMS_CMDID
 */

#define AR6000_IOCTL_WMI_SET_BADAP           (SIOCIWFIRSTPRIV+19)
/*
 * arguments:
 *   WMI_ADD_BAD_AP_CMD badAPs (see include/wmi.h)
 * uses: WMI_ADD_BAD_AP_CMDID
 */

#define AR6000_IOCTL_WMI_GET_QOS_QUEUE       (SIOCIWFIRSTPRIV+20)
/*
 * arguments:
 *   ar6000_queuereq queueRequest (see below)
 */

#define AR6000_IOCTL_WMI_CREATE_QOS          (SIOCIWFIRSTPRIV+21)
/*
 * arguments:
 *   WMI_CREATE_PSTREAM createPstreamCmd (see include/wmi.h)
 * uses: WMI_CREATE_PSTREAM_CMDID
 */

#define AR6000_IOCTL_WMI_DELETE_QOS          (SIOCIWFIRSTPRIV+22)
/*
 * arguments:
 *   WMI_DELETE_PSTREAM_CMD deletePstreamCmd (see include/wmi.h)
 * uses: WMI_DELETE_PSTREAM_CMDID
 */

#define AR6000_IOCTL_WMI_SET_SNRTHRESHOLD   (SIOCIWFIRSTPRIV+23)
/*
 * arguments:
 *   WMI_SNR_THRESHOLD_PARAMS_CMD thresholdParams (see include/wmi.h)
 * uses: WMI_SNR_THRESHOLD_PARAMS_CMDID
 */

#define AR6000_IOCTL_WMI_SET_ERROR_REPORT_BITMASK (SIOCIWFIRSTPRIV+24)
/*
 * arguments:
 *   WMI_TARGET_ERROR_REPORT_BITMASK errorReportBitMask (see include/wmi.h)
 * uses: WMI_TARGET_ERROR_REPORT_BITMASK_CMDID
 */

#define AR6000_IOCTL_WMI_GET_TARGET_STATS    (SIOCIWFIRSTPRIV+25)
/*
 * arguments:
 *   TARGET_STATS *targetStats (see below)
 * uses: WMI_GET_STATISTICS_CMDID
 */

#define AR6000_IOCTL_WMI_SET_ASSOC_INFO      (SIOCIWFIRSTPRIV+26)
/*
 * arguments:
 *   WMI_SET_ASSOC_INFO_CMD setAssocInfoCmd
 * uses: WMI_SET_ASSOC_INFO_CMDID
 */

#define AR6000_IOCTL_WMI_SET_ACCESS_PARAMS   (SIOCIWFIRSTPRIV+27)
/*
 * arguments:
 *   WMI_SET_ACCESS_PARAMS_CMD setAccessParams (see include/wmi.h)
 * uses: WMI_SET_ACCESS_PARAMS_CMDID
 */

#define AR6000_IOCTL_WMI_SET_BMISS_TIME      (SIOCIWFIRSTPRIV+28)
/*
 * arguments:
 *   UINT32 beaconMissTime
 * uses: WMI_SET_BMISS_TIME_CMDID
 */

#define AR6000_IOCTL_WMI_SET_DISC_TIMEOUT    (SIOCIWFIRSTPRIV+29)
/*
 * arguments:
 *   WMI_DISC_TIMEOUT_CMD disconnectTimeoutCmd (see include/wmi.h)
 * uses: WMI_SET_DISC_TIMEOUT_CMDID
 */

#define AR6000_IOCTL_WMI_SET_IBSS_PM_CAPS    (SIOCIWFIRSTPRIV+30)
/*
 * arguments:
 *   WMI_IBSS_PM_CAPS_CMD ibssPowerMgmtCapsCmd
 * uses: WMI_SET_IBSS_PM_CAPS_CMDID
 */

/*
 * There is a very small space available for driver-private
 * wireless ioctls.  In order to circumvent this limitation,
 * we multiplex a bunch of ioctls (XIOCTLs) on top of a
 * single AR6000_IOCTL_EXTENDED ioctl.
 */
#define AR6000_IOCTL_EXTENDED                (SIOCIWFIRSTPRIV+31)


/*                         ====BMI Extended Ioctls====                        */

#define AR6000_XIOCTL_BMI_DONE                                  1
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_BMI_DONE)
 * uses: BMI_DONE
 */

#define AR6000_XIOCTL_BMI_READ_MEMORY                           2
/*
 * arguments:
 *   union {
 *     struct {
 *       UINT32 cmd (AR6000_XIOCTL_BMI_READ_MEMORY)
 *       UINT32 address
 *       UINT32 length
 *     }
 *     char results[length]
 *   }
 * uses: BMI_READ_MEMORY
 */

#define AR6000_XIOCTL_BMI_WRITE_MEMORY                          3
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_BMI_WRITE_MEMORY)
 *   UINT32 address
 *   UINT32 length
 *   char data[length]
 * uses: BMI_WRITE_MEMORY
 */

#define AR6000_XIOCTL_BMI_EXECUTE                               4
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_BMI_EXECUTE)
 *   UINT32 TargetAddress
 *   UINT32 parameter
 * uses: BMI_EXECUTE
 */

#define AR6000_XIOCTL_BMI_SET_APP_START                         5
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_BMI_SET_APP_START)
 *   UINT32 TargetAddress
 * uses: BMI_SET_APP_START
 */

#define AR6000_XIOCTL_BMI_READ_SOC_REGISTER                     6
/*
 * arguments:
 *   union {
 *     struct {
 *       UINT32 cmd (AR6000_XIOCTL_BMI_READ_SOC_REGISTER)
 *       UINT32 TargetAddress, 32-bit aligned
 *     }
 *     UINT32 result
 *   }
 * uses: BMI_READ_SOC_REGISTER
 */

#define AR6000_XIOCTL_BMI_WRITE_SOC_REGISTER                    7
/*
 * arguments:
 *     struct {
 *       UINT32 cmd (AR6000_XIOCTL_BMI_WRITE_SOC_REGISTER)
 *       UINT32 TargetAddress, 32-bit aligned
 *       UINT32 newValue
 *     }
 * uses: BMI_WRITE_SOC_REGISTER
 */

#define AR6000_XIOCTL_BMI_TEST                                  8
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_BMI_TEST)
 *   UINT32 address
 *   UINT32 length
 *   UINT32 count
 */



/* Historical Host-side DataSet support */
#define AR6000_XIOCTL_UNUSED9                                   9
#define AR6000_XIOCTL_UNUSED10                                  10
#define AR6000_XIOCTL_UNUSED11                                  11

/*                      ====Misc Extended Ioctls====                          */

#define AR6000_XIOCTL_FORCE_TARGET_RESET                        12
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_FORCE_TARGET_RESET)
 */


#ifdef HTC_RAW_INTERFACE
/* HTC Raw Interface Ioctls */
#define AR6000_XIOCTL_HTC_RAW_OPEN                              13
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_HTC_RAW_OPEN)
 */

#define AR6000_XIOCTL_HTC_RAW_CLOSE                             14
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_HTC_RAW_CLOSE)
 */

#define AR6000_XIOCTL_HTC_RAW_READ                              15
/*
 * arguments:
 *   union {
 *     struct {
 *       UINT32 cmd (AR6000_XIOCTL_HTC_RAW_READ)
 *       UINT32 mailboxID
 *       UINT32 length
 *     }
 *     results[length]
 *   }
 */

#define AR6000_XIOCTL_HTC_RAW_WRITE                             16
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_HTC_RAW_WRITE)
 *   UINT32 mailboxID
 *   UINT32 length
 *   char buffer[length]
 */
#endif /* HTC_RAW_INTERFACE */

#define AR6000_XIOCTL_CHECK_TARGET_READY                        17
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_CHECK_TARGET_READY)
 */



/*                ====GPIO (General Purpose I/O) Extended Ioctls====          */

#define AR6000_XIOCTL_GPIO_OUTPUT_SET                           18
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_GPIO_OUTPUT_SET)
 *   ar6000_gpio_output_set_cmd_s (see below)
 * uses: WMIX_GPIO_OUTPUT_SET_CMDID
 */

#define AR6000_XIOCTL_GPIO_INPUT_GET                            19
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_GPIO_INPUT_GET)
 * uses: WMIX_GPIO_INPUT_GET_CMDID
 */

#define AR6000_XIOCTL_GPIO_REGISTER_SET                         20
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_GPIO_REGISTER_SET)
 *   ar6000_gpio_register_cmd_s (see below)
 * uses: WMIX_GPIO_REGISTER_SET_CMDID
 */

#define AR6000_XIOCTL_GPIO_REGISTER_GET                         21
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_GPIO_REGISTER_GET)
 *   ar6000_gpio_register_cmd_s (see below)
 * uses: WMIX_GPIO_REGISTER_GET_CMDID
 */

#define AR6000_XIOCTL_GPIO_INTR_ACK                             22
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_GPIO_INTR_ACK)
 *   ar6000_cpio_intr_ack_cmd_s (see below)
 * uses: WMIX_GPIO_INTR_ACK_CMDID
 */

#define AR6000_XIOCTL_GPIO_INTR_WAIT                            23
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_GPIO_INTR_WAIT)
 */



/*                    ====more wireless commands====                          */

#define AR6000_XIOCTL_SET_ADHOC_BSSID                           24
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_SET_ADHOC_BSSID)
 *   WMI_SET_ADHOC_BSSID_CMD setAdHocBssidCmd (see include/wmi.h)
 */

#define AR6000_XIOCTL_SET_OPT_MODE                              25
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_SET_OPT_MODE)
 *   WMI_SET_OPT_MODE_CMD setOptModeCmd (see include/wmi.h)
 * uses: WMI_SET_OPT_MODE_CMDID
 */

#define AR6000_XIOCTL_OPT_SEND_FRAME                            26
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_OPT_SEND_FRAME)
 *   WMI_OPT_TX_FRAME_CMD optTxFrameCmd (see include/wmi.h)
 * uses: WMI_OPT_TX_FRAME_CMDID
 */

#define AR6000_XIOCTL_SET_BEACON_INTVAL                         27
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_SET_BEACON_INTVAL)
 *   WMI_BEACON_INT_CMD beaconIntCmd (see include/wmi.h)
 * uses: WMI_SET_BEACON_INT_CMDID
 */


#define IEEE80211_IOCTL_SETAUTHALG                              28


#define AR6000_XIOCTL_SET_VOICE_PKT_SIZE                        29
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_SET_VOICE_PKT_SIZE)
 *   WMI_SET_VOICE_PKT_SIZE_CMD setVoicePktSizeCmd (see include/wmi.h)
 * uses: WMI_SET_VOICE_PKT_SIZE_CMDID
 */


#define AR6000_XIOCTL_SET_MAX_SP                                30
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_SET_MAX_SP)
 *   WMI_SET_MAX_SP_LEN_CMD maxSPLen(see include/wmi.h)
 * uses: WMI_SET_MAX_SP_LEN_CMDID
 */

#define AR6000_XIOCTL_WMI_GET_ROAM_TBL                          31

#define AR6000_XIOCTL_WMI_SET_ROAM_CTRL                         32

#define AR6000_XIOCTRL_WMI_SET_POWERSAVE_TIMERS                 33


/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTRL_WMI_SET_POWERSAVE_TIMERS)
 *   WMI_SET_POWERSAVE_TIMERS_CMD powerSaveTimers(see include/wmi.h)
 *   WMI_SET_POWERSAVE_TIMERS_CMDID
 */

#define AR6000_XIOCTRL_WMI_GET_POWER_MODE                        34
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTRL_WMI_GET_POWER_MODE)
 */

#define AR6000_XIOCTRL_WMI_SET_WLAN_STATE                       35
typedef enum {
    WLAN_DISABLED,
    WLAN_ENABLED
} AR6000_WLAN_STATE;
/*
 * arguments:
 * enable/disable
 */

#define AR6000_XIOCTL_WMI_GET_ROAM_DATA                         36

#define AR6000_XIOCTL_WMI_SETRETRYLIMITS                37
/*
 * arguments:
 *   WMI_SET_RETRY_LIMITS_CMD ibssSetRetryLimitsCmd
 * uses: WMI_SET_RETRY_LIMITS_CMDID
 */

#ifdef CONFIG_HOST_TCMD_SUPPORT
/*       ====extended commands for radio test ====                          */

#define AR6000_XIOCTL_TCMD_CONT_TX                      38
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_TCMD_CONT_TX)
 *   WMI_TCMD_CONT_TX_CMD contTxCmd (see include/wmi.h)
 * uses: WMI_TCMD_CONT_TX_CMDID
 */

#define AR6000_XIOCTL_TCMD_CONT_RX                      39
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_TCMD_CONT_RX)
 *   WMI_TCMD_CONT_RX_CMD rxCmd (see include/wmi.h)
 * uses: WMI_TCMD_CONT_RX_CMDID
 */

#define AR6000_XIOCTL_TCMD_PM                           40
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_TCMD_PM)
 *   WMI_TCMD_PM_CMD pmCmd (see include/wmi.h)
 * uses: WMI_TCMD_PM_CMDID
 */

#endif /* CONFIG_HOST_TCMD_SUPPORT */

#define AR6000_XIOCTL_WMI_STARTSCAN                     41
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_WMI_STARTSCAN)
 *   UINT8  scanType
 *   UINT8  scanConnected
 *   u32 forceFgScan
 * uses: WMI_START_SCAN_CMDID
 */

#define AR6000_XIOCTL_WMI_SETFIXRATES                   42

#define AR6000_XIOCTL_WMI_GETFIXRATES                   43


#define AR6000_XIOCTL_WMI_SET_RSSITHRESHOLD             44
/*
 * arguments:
 *   WMI_RSSI_THRESHOLD_PARAMS_CMD thresholdParams (see include/wmi.h)
 * uses: WMI_RSSI_THRESHOLD_PARAMS_CMDID
 */

#define AR6000_XIOCTL_WMI_CLR_RSSISNR                   45
/*
 * arguments:
 *   WMI_CLR_RSSISNR_CMD thresholdParams (see include/wmi.h)
 * uses: WMI_CLR_RSSISNR_CMDID
 */

#define AR6000_XIOCTL_WMI_SET_LQTHRESHOLD               46
/*
 * arguments:
 *   WMI_LQ_THRESHOLD_PARAMS_CMD thresholdParams (see include/wmi.h)
 * uses: WMI_LQ_THRESHOLD_PARAMS_CMDID
 */

#define AR6000_XIOCTL_WMI_SET_RTS                        47
/*
 * arguments:
 *   WMI_SET_RTS_MODE_CMD (see include/wmi.h)
 * uses: WMI_SET_RTS_MODE_CMDID
 */

#define AR6000_XIOCTL_WMI_SET_LPREAMBLE                 48

#define AR6000_XIOCTL_WMI_SET_AUTHMODE                  49
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_WMI_SET_AUTHMODE)
 *   UINT8  mode
 * uses: WMI_SET_RECONNECT_AUTH_MODE_CMDID
 */

#define AR6000_XIOCTL_WMI_SET_REASSOCMODE               50

/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_WMI_SET_WMM)
 *   UINT8  mode
 * uses: WMI_SET_WMM_CMDID
 */
#define AR6000_XIOCTL_WMI_SET_WMM                       51

/*
 * arguments:
 * UINT32 cmd (AR6000_XIOCTL_WMI_SET_HB_CHALLENGE_RESP_PARAMS)
 * UINT32 frequency
 * UINT8  threshold
 */
#define AR6000_XIOCTL_WMI_SET_HB_CHALLENGE_RESP_PARAMS  52

/*
 * arguments:
 * UINT32 cmd (AR6000_XIOCTL_WMI_GET_HB_CHALLENGE_RESP)
 * UINT32 cookie
 */
#define AR6000_XIOCTL_WMI_GET_HB_CHALLENGE_RESP         53

/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_WMI_GET_RD)
 *   UINT32 regDomain
 */
#define AR6000_XIOCTL_WMI_GET_RD                        54

#define AR6000_XIOCTL_DIAG_READ                         55

#define AR6000_XIOCTL_DIAG_WRITE                        56

/*
 * arguments cmd (AR6000_XIOCTL_SET_TXOP)
 * WMI_TXOP_CFG  txopEnable
 */
#define AR6000_XIOCTL_WMI_SET_TXOP                      57

/*
 * arguments:
 * UINT32 cmd (AR6000_XIOCTL_USER_SETKEYS)
 * UINT32 keyOpCtrl
 * uses struct ar6000_user_setkeys_info
 */
#define AR6000_XIOCTL_USER_SETKEYS                      58

#define AR6000_XIOCTL_WMI_SET_KEEPALIVE                 59
/*
 * arguments:
 *   UINT8 cmd (AR6000_XIOCTL_WMI_SET_KEEPALIVE)
 *   UINT8 keepaliveInterval
 * uses: WMI_SET_KEEPALIVE_CMDID
 */

#define AR6000_XIOCTL_WMI_GET_KEEPALIVE                 60
/*
 * arguments:
 *   UINT8 cmd (AR6000_XIOCTL_WMI_GET_KEEPALIVE)
 *   UINT8 keepaliveInterval
 *   u32 configured
 * uses: WMI_GET_KEEPALIVE_CMDID
 */

/*               ====ROM Patching Extended Ioctls====                       */

#define AR6000_XIOCTL_BMI_ROMPATCH_INSTALL              61
/*
 * arguments:
 *     union {
 *       struct {
 *         UINT32 cmd (AR6000_XIOCTL_BMI_ROMPATCH_INSTALL)
 *         UINT32 ROM Address
 *         UINT32 RAM Address
 *         UINT32 number of bytes
 *         UINT32 activate? (0 or 1)
 *       }
 *       u32 resulting rompatch ID
 *     }
 * uses: BMI_ROMPATCH_INSTALL
 */

#define AR6000_XIOCTL_BMI_ROMPATCH_UNINSTALL            62
/*
 * arguments:
 *     struct {
 *       UINT32 cmd (AR6000_XIOCTL_BMI_ROMPATCH_UNINSTALL)
 *       UINT32 rompatch ID
 *     }
 * uses: BMI_ROMPATCH_UNINSTALL
 */

#define AR6000_XIOCTL_BMI_ROMPATCH_ACTIVATE             63
/*
 * arguments:
 *     struct {
 *       UINT32 cmd (AR6000_XIOCTL_BMI_ROMPATCH_ACTIVATE)
 *       UINT32 rompatch count
 *       UINT32 rompatch IDs[rompatch count]
 *     }
 * uses: BMI_ROMPATCH_ACTIVATE
 */

#define AR6000_XIOCTL_BMI_ROMPATCH_DEACTIVATE           64
/*
 * arguments:
 *     struct {
 *       UINT32 cmd (AR6000_XIOCTL_BMI_ROMPATCH_DEACTIVATE)
 *       UINT32 rompatch count
 *       UINT32 rompatch IDs[rompatch count]
 *     }
 * uses: BMI_ROMPATCH_DEACTIVATE
 */

#define AR6000_XIOCTL_WMI_SET_APPIE             65
/*
 * arguments:
 *      struct {
 *          UINT32 cmd (AR6000_XIOCTL_WMI_SET_APPIE)
 *          UINT32  app_frmtype;
 *          UINT32  app_buflen;
 *          UINT8   app_buf[];
 *      }
 */
#define AR6000_XIOCTL_WMI_SET_MGMT_FRM_RX_FILTER    66
/*
 * arguments:
 *      u32 filter_type;
 */

#define AR6000_XIOCTL_DBGLOG_CFG_MODULE             67

#define AR6000_XIOCTL_DBGLOG_GET_DEBUG_LOGS         68

#define AR6000_XIOCTL_WMI_SET_WSC_STATUS            70
/*
 * arguments:
 *      u32 wsc_status;
 *            (WSC_REG_INACTIVE or WSC_REG_ACTIVE)
 */

/*
 * arguments:
 *      struct {
 *          u8 streamType;
 *          u8 status;
 *      }
 * uses: WMI_SET_BT_STATUS_CMDID
 */
#define AR6000_XIOCTL_WMI_SET_BT_STATUS             71

/*
 * arguments:
 *      struct {
 *           u8 paramType;
 *           union {
 *               u8 noSCOPkts;
 *               BT_PARAMS_A2DP a2dpParams;
 *               BT_COEX_REGS regs;
 *           };
 *      }
 * uses: WMI_SET_BT_PARAM_CMDID
 */
#define AR6000_XIOCTL_WMI_SET_BT_PARAMS             72

#define AR6000_XIOCTL_WMI_SET_HOST_SLEEP_MODE       73
#define AR6000_XIOCTL_WMI_SET_WOW_MODE              74
#define AR6000_XIOCTL_WMI_GET_WOW_LIST              75
#define AR6000_XIOCTL_WMI_ADD_WOW_PATTERN           76
#define AR6000_XIOCTL_WMI_DEL_WOW_PATTERN           77



#define AR6000_XIOCTL_TARGET_INFO                   78
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_TARGET_INFO)
 *   u32 TargetVersion (returned)
 *   u32 TargetType    (returned)
 * (See also bmi_msg.h target_ver and target_type)
 */

#define AR6000_XIOCTL_DUMP_HTC_CREDIT_STATE         79
/*
 * arguments:
 *      none
 */

#define AR6000_XIOCTL_TRAFFIC_ACTIVITY_CHANGE       80
/*
 * This ioctl is used to emulate traffic activity
 * timeouts.  Activity/inactivity will trigger the driver
 * to re-balance credits.
 *
 * arguments:
 *      ar6000_traffic_activity_change
 */

#define AR6000_XIOCTL_WMI_SET_CONNECT_CTRL_FLAGS    81
/*
 * This ioctl is used to set the connect control flags
 *
 * arguments:
 *      u32 connectCtrlFlags
 */

#define AR6000_XIOCTL_WMI_SET_AKMP_PARAMS              82
/*
 * This IOCTL sets any Authentication,Key Management and Protection
 * related parameters. This is used along with the information set in
 * Connect Command.
 * Currently this enables Multiple PMKIDs to an AP.
 *
 * arguments:
 *      struct {
 *          u32 akmpInfo;
 *      }
 * uses: WMI_SET_AKMP_PARAMS_CMD
 */

#define AR6000_XIOCTL_WMI_GET_PMKID_LIST            83

#define AR6000_XIOCTL_WMI_SET_PMKID_LIST            84
/*
 * This IOCTL is used to set a list of PMKIDs. This list of
 * PMKIDs is used in the [Re]AssocReq Frame. This list is used
 * only if the MultiPMKID option is enabled via the
 * AR6000_XIOCTL_WMI_SET_AKMP_PARAMS  IOCTL.
 *
 * arguments:
 *      struct {
 *          u32 numPMKID;
 *          WMI_PMKID   pmkidList[WMI_MAX_PMKID_CACHE];
 *      }
 * uses: WMI_SET_PMKIDLIST_CMD
 */

#define AR6000_XIOCTL_WMI_SET_PARAMS                85
#define AR6000_XIOCTL_WMI_SET_MCAST_FILTER     86
#define AR6000_XIOCTL_WMI_DEL_MCAST_FILTER     87


/* Historical DSETPATCH support for INI patches */
#define AR6000_XIOCTL_UNUSED90                      90


/* Support LZ-compressed firmware download */
#define AR6000_XIOCTL_BMI_LZ_STREAM_START           91
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_BMI_LZ_STREAM_START)
 *   UINT32 address
 * uses: BMI_LZ_STREAM_START
 */

#define AR6000_XIOCTL_BMI_LZ_DATA                   92
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_BMI_LZ_DATA)
 *   UINT32 length
 *   char data[length]
 * uses: BMI_LZ_DATA
 */

#define AR6000_XIOCTL_PROF_CFG                      93
/*
 * arguments:
 *   u32 period
 *   u32 nbins
 */

#define AR6000_XIOCTL_PROF_ADDR_SET                 94
/*
 * arguments:
 *   u32 Target address
 */

#define AR6000_XIOCTL_PROF_START                    95

#define AR6000_XIOCTL_PROF_STOP                     96

#define AR6000_XIOCTL_PROF_COUNT_GET                97

#define AR6000_XIOCTL_WMI_ABORT_SCAN                98

/*
 * AP mode
 */
#define AR6000_XIOCTL_AP_GET_STA_LIST               99

#define AR6000_XIOCTL_AP_HIDDEN_SSID                100

#define AR6000_XIOCTL_AP_SET_NUM_STA                101

#define AR6000_XIOCTL_AP_SET_ACL_MAC                102

#define AR6000_XIOCTL_AP_GET_ACL_LIST               103

#define AR6000_XIOCTL_AP_COMMIT_CONFIG              104

#define IEEE80211_IOCTL_GETWPAIE                    105

#define AR6000_XIOCTL_AP_CONN_INACT_TIME            106

#define AR6000_XIOCTL_AP_PROT_SCAN_TIME             107

#define AR6000_XIOCTL_AP_SET_COUNTRY                108

#define AR6000_XIOCTL_AP_SET_DTIM                   109




#define AR6000_XIOCTL_WMI_TARGET_EVENT_REPORT       110

#define AR6000_XIOCTL_SET_IP                        111

#define AR6000_XIOCTL_AP_SET_ACL_POLICY             112

#define AR6000_XIOCTL_AP_INTRA_BSS_COMM             113

#define AR6000_XIOCTL_DUMP_MODULE_DEBUG_INFO        114

#define AR6000_XIOCTL_MODULE_DEBUG_SET_MASK         115

#define AR6000_XIOCTL_MODULE_DEBUG_GET_MASK         116

#define AR6000_XIOCTL_DUMP_RCV_AGGR_STATS           117

#define AR6000_XIOCTL_SET_HT_CAP                    118

#define AR6000_XIOCTL_SET_HT_OP                     119

#define AR6000_XIOCTL_AP_GET_STAT                   120

#define AR6000_XIOCTL_SET_TX_SELECT_RATES           121

#define AR6000_XIOCTL_SETUP_AGGR                    122

#define AR6000_XIOCTL_ALLOW_AGGR                    123

#define AR6000_XIOCTL_AP_GET_HIDDEN_SSID            124

#define AR6000_XIOCTL_AP_GET_COUNTRY                125

#define AR6000_XIOCTL_AP_GET_WMODE                  126

#define AR6000_XIOCTL_AP_GET_DTIM                   127

#define AR6000_XIOCTL_AP_GET_BINTVL                 128

#define AR6000_XIOCTL_AP_GET_RTS                    129

#define AR6000_XIOCTL_DELE_AGGR                     130

#define AR6000_XIOCTL_FETCH_TARGET_REGS             131

#define AR6000_XIOCTL_HCI_CMD                       132

#define AR6000_XIOCTL_ACL_DATA                      133 /* used to be used for PAL */

#define AR6000_XIOCTL_WLAN_CONN_PRECEDENCE          134

#define AR6000_XIOCTL_AP_SET_11BG_RATESET           135

/*
 * arguments:
 *   WMI_AP_PS_CMD apPsCmd
 * uses: WMI_AP_PS_CMDID
 */

#define AR6000_XIOCTL_WMI_SET_AP_PS                 136

#define AR6000_XIOCTL_WMI_MCAST_FILTER              137

#define AR6000_XIOCTL_WMI_SET_BTCOEX_FE_ANT					138

#define AR6000_XIOCTL_WMI_SET_BTCOEX_COLOCATED_BT_DEV			139

#define AR6000_XIOCTL_WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG 	140

#define AR6000_XIOCTL_WMI_SET_BTCOEX_SCO_CONFIG				141

#define AR6000_XIOCTL_WMI_SET_BTCOEX_A2DP_CONFIG				142

#define AR6000_XIOCTL_WMI_SET_BTCOEX_ACLCOEX_CONFIG			143

#define AR6000_XIOCTL_WMI_SET_BTCOEX_DEBUG						144

#define AR6000_XIOCTL_WMI_SET_BT_OPERATING_STATUS   			145

#define AR6000_XIOCTL_WMI_GET_BTCOEX_CONFIG					146

#define AR6000_XIOCTL_WMI_GET_BTCOEX_STATS						147
/*
 * arguments:
 *   UINT32 cmd (AR6000_XIOCTL_WMI_SET_QOS_SUPP)
 *   UINT8  mode
 * uses: WMI_SET_QOS_SUPP_CMDID
 */
#define AR6000_XIOCTL_WMI_SET_QOS_SUPP                  148 

#define AR6000_XIOCTL_GET_WLAN_SLEEP_STATE              149

#define AR6000_XIOCTL_SET_BT_HW_POWER_STATE             150 

#define AR6000_XIOCTL_GET_BT_HW_POWER_STATE             151

#define AR6000_XIOCTL_ADD_AP_INTERFACE                  152

#define AR6000_XIOCTL_REMOVE_AP_INTERFACE               153

#define AR6000_XIOCTL_WMI_SET_TX_SGI_PARAM              154

#define AR6000_XIOCTL_WMI_SET_EXCESS_TX_RETRY_THRES     161

/* used by AR6000_IOCTL_WMI_GETREV */
struct ar6000_version {
    u32 host_ver;
    u32 target_ver;
    u32 wlan_ver;
    u32 abi_ver;
};

/* used by AR6000_IOCTL_WMI_GET_QOS_QUEUE */
struct ar6000_queuereq {
    u8 trafficClass;
    u16 activeTsids;
};

/* used by AR6000_IOCTL_WMI_GET_TARGET_STATS */
typedef struct targetStats_t {
    u64 tx_packets;
    u64 tx_bytes;
    u64 tx_unicast_pkts;
    u64 tx_unicast_bytes;
    u64 tx_multicast_pkts;
    u64 tx_multicast_bytes;
    u64 tx_broadcast_pkts;
    u64 tx_broadcast_bytes;
    u64 tx_rts_success_cnt;
    u64 tx_packet_per_ac[4];

    u64 tx_errors;
    u64 tx_failed_cnt;
    u64 tx_retry_cnt;
    u64 tx_mult_retry_cnt;
    u64 tx_rts_fail_cnt;

    u64 rx_packets;
    u64 rx_bytes;
    u64 rx_unicast_pkts;
    u64 rx_unicast_bytes;
    u64 rx_multicast_pkts;
    u64 rx_multicast_bytes;
    u64 rx_broadcast_pkts;
    u64 rx_broadcast_bytes;
    u64 rx_fragment_pkt;

    u64 rx_errors;
    u64 rx_crcerr;
    u64 rx_key_cache_miss;
    u64 rx_decrypt_err;
    u64 rx_duplicate_frames;

    u64 tkip_local_mic_failure;
    u64 tkip_counter_measures_invoked;
    u64 tkip_replays;
    u64 tkip_format_errors;
    u64 ccmp_format_errors;
    u64 ccmp_replays;

    u64 power_save_failure_cnt;

    u64 cs_bmiss_cnt;
    u64 cs_lowRssi_cnt;
    u64 cs_connect_cnt;
    u64 cs_disconnect_cnt;

    s32 tx_unicast_rate;
    s32 rx_unicast_rate;

    u32 lq_val;

    u32 wow_num_pkts_dropped;
    u16 wow_num_events_discarded;

    s16 noise_floor_calibation;
    s16 cs_rssi;
    s16 cs_aveBeacon_rssi;
    u8 cs_aveBeacon_snr;
    u8 cs_lastRoam_msec;
    u8 cs_snr;

    u8 wow_num_host_pkt_wakeups;
    u8 wow_num_host_event_wakeups;

    u32 arp_received;
    u32 arp_matched;
    u32 arp_replied;
}TARGET_STATS;

typedef struct targetStats_cmd_t {
    TARGET_STATS targetStats;
    int clearStats;
} TARGET_STATS_CMD;

/* used by AR6000_XIOCTL_USER_SETKEYS */

/*
 * Setting this bit to 1 doesnot initialize the RSC on the firmware
 */
#define AR6000_XIOCTL_USER_SETKEYS_RSC_CTRL    1
#define AR6000_USER_SETKEYS_RSC_UNCHANGED     0x00000002

struct ar6000_user_setkeys_info {
    u32 keyOpCtrl;  /* Bit Map of Key Mgmt Ctrl Flags */
}; /* XXX: unused !? */

/* used by AR6000_XIOCTL_GPIO_OUTPUT_SET */
struct ar6000_gpio_output_set_cmd_s {
    u32 set_mask;
    u32 clear_mask;
    u32 enable_mask;
    u32 disable_mask;
};

/*
 * used by AR6000_XIOCTL_GPIO_REGISTER_GET and AR6000_XIOCTL_GPIO_REGISTER_SET
 */
struct ar6000_gpio_register_cmd_s {
    u32 gpioreg_id;
    u32 value;
};

/* used by AR6000_XIOCTL_GPIO_INTR_ACK */
struct ar6000_gpio_intr_ack_cmd_s {
    u32 ack_mask;
};

/* used by AR6000_XIOCTL_GPIO_INTR_WAIT */
struct ar6000_gpio_intr_wait_cmd_s {
    u32 intr_mask;
    u32 input_values;
};

/* used by the AR6000_XIOCTL_DBGLOG_CFG_MODULE */
typedef struct ar6000_dbglog_module_config_s {
    u32 valid;
    u16 mmask;
    u16 tsr;
    u32   rep;
    u16 size;
} DBGLOG_MODULE_CONFIG;

typedef struct user_rssi_thold_t {
    s16 tag;
    s16 rssi;
} USER_RSSI_THOLD;

typedef struct user_rssi_params_t {
    u8 weight;
    u32 pollTime;
    USER_RSSI_THOLD    tholds[12];
} USER_RSSI_PARAMS;

typedef struct ar6000_get_btcoex_config_cmd_t{
	u32 btProfileType;
	u32 linkId;
 }AR6000_GET_BTCOEX_CONFIG_CMD;

typedef struct ar6000_btcoex_config_t {
    AR6000_GET_BTCOEX_CONFIG_CMD  configCmd;
    u32 *configEvent;
} AR6000_BTCOEX_CONFIG;

typedef struct ar6000_btcoex_stats_t {
    u32 *statsEvent;
 }AR6000_BTCOEX_STATS;
/*
 * Host driver may have some config parameters. Typically, these
 * config params are one time config parameters. These could
 * correspond to any of the underlying modules. Host driver exposes
 * an api for the underlying modules to get this config.
 */
#define AR6000_DRIVER_CFG_BASE                  0x8000

/* Should driver perform wlan node caching? */
#define AR6000_DRIVER_CFG_GET_WLANNODECACHING   0x8001
/*Should we log raw WMI msgs */
#define AR6000_DRIVER_CFG_LOG_RAW_WMI_MSGS      0x8002

/* used by AR6000_XIOCTL_DIAG_READ & AR6000_XIOCTL_DIAG_WRITE */
struct ar6000_diag_window_cmd_s {
    unsigned int addr;
    unsigned int value;
};


struct ar6000_traffic_activity_change {
    u32 StreamID;   /* stream ID to indicate activity change */
    u32 Active;     /* active (1) or inactive (0) */
};

/* Used with AR6000_XIOCTL_PROF_COUNT_GET */
struct prof_count_s {
    u32 addr;       /* bin start address */
    u32 count;      /* hit count */
};


/* used by AR6000_XIOCTL_MODULE_DEBUG_SET_MASK */
/*         AR6000_XIOCTL_MODULE_DEBUG_GET_MASK */
/*         AR6000_XIOCTL_DUMP_MODULE_DEBUG_INFO */
struct drv_debug_module_s {
    char modulename[128];   /* name of module */
    u32 mask;              /* new mask to set .. or .. current mask */
};


/* All HCI related rx events are sent up to the host app
 * via a wmi event id. It can contain ACL data or HCI event, 
 * based on which it will be de-multiplexed.
 */
typedef enum {
    PAL_HCI_EVENT = 0,
    PAL_HCI_RX_DATA,
} WMI_PAL_EVENT_INFO;


#ifdef __cplusplus
}
#endif
#endif
