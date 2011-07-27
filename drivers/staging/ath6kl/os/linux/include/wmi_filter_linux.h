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

#ifndef _WMI_FILTER_LINUX_H_
#define  _WMI_FILTER_LINUX_H_

/*
 * sioctl_filter - Standard ioctl
 * pioctl_filter - Priv ioctl
 * xioctl_filter - eXtended ioctl
 *
 * ---- Possible values for the WMI filter ---------------
 * (0) - Block this cmd always (or) not implemented
 * (INFRA_NETWORK) - Allow this cmd only in STA mode
 * (ADHOC_NETWORK) - Allow this cmd only in IBSS mode
 * (AP_NETWORK) -    Allow this cmd only in AP mode
 * (INFRA_NETWORK | ADHOC_NETWORK) - Block this cmd in AP mode
 * (ADHOC_NETWORK | AP_NETWORK) -    Block this cmd in STA mode
 * (INFRA_NETWORK | AP_NETWORK) -    Block this cmd in IBSS mode
 * (INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK)- allow only when mode is set
 * (0xFF) - Allow this cmd always irrespective of mode
 */

u8 sioctl_filter[] = {
(AP_NETWORK),                                   /* SIOCSIWCOMMIT   0x8B00   */
(0xFF),                                         /* SIOCGIWNAME     0x8B01   */
(0),                                            /* SIOCSIWNWID     0x8B02   */
(0),                                            /* SIOCGIWNWID     0x8B03   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWFREQ     0x8B04   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWFREQ     0x8B05   */
(0xFF),                                         /* SIOCSIWMODE     0x8B06   */
(0xFF),                                         /* SIOCGIWMODE     0x8B07   */
(0),                                            /* SIOCSIWSENS     0x8B08   */
(0),                                            /* SIOCGIWSENS     0x8B09   */
(0),                                            /* SIOCSIWRANGE    0x8B0A   */
(0xFF),                                         /* SIOCGIWRANGE    0x8B0B   */
(0),                                            /* SIOCSIWPRIV     0x8B0C   */
(0),                                            /* SIOCGIWPRIV     0x8B0D   */
(0),                                            /* SIOCSIWSTATS    0x8B0E   */
(0),                                            /* SIOCGIWSTATS    0x8B0F   */
(0),                                            /* SIOCSIWSPY      0x8B10   */
(0),                                            /* SIOCGIWSPY      0x8B11   */
(0),                                            /* SIOCSIWTHRSPY   0x8B12   */
(0),                                            /* SIOCGIWTHRSPY   0x8B13   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWAP       0x8B14   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWAP       0x8B15   */
#if (WIRELESS_EXT >= 18)
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCSIWMLME     0X8B16   */
#else
(0),                                            /* Dummy           0        */
#endif /* WIRELESS_EXT */
(0),                                            /* SIOCGIWAPLIST   0x8B17   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCSIWSCAN     0x8B18   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCGIWSCAN     0x8B19   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWESSID    0x8B1A   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWESSID    0x8B1B   */
(0),                                            /* SIOCSIWNICKN    0x8B1C   */
(0),                                            /* SIOCGIWNICKN    0x8B1D   */
(0),                                            /* Dummy           0        */
(0),                                            /* Dummy           0        */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWRATE     0x8B20   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWRATE     0x8B21   */
(0),                                            /* SIOCSIWRTS      0x8B22   */
(0),                                            /* SIOCGIWRTS      0x8B23   */
(0),                                            /* SIOCSIWFRAG     0x8B24   */
(0),                                            /* SIOCGIWFRAG     0x8B25   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWTXPOW    0x8B26   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWTXPOW    0x8B27   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCSIWRETRY    0x8B28   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCGIWRETRY    0x8B29   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWENCODE   0x8B2A   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWENCODE   0x8B2B   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWPOWER    0x8B2C   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWPOWER    0x8B2D   */
};



u8 pioctl_filter[] = {
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* IEEE80211_IOCTL_SETPARAM             (SIOCIWFIRSTPRIV+0)     */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* IEEE80211_IOCTL_SETKEY               (SIOCIWFIRSTPRIV+1)     */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* IEEE80211_IOCTL_DELKEY               (SIOCIWFIRSTPRIV+2)     */
(AP_NETWORK),                                   /* IEEE80211_IOCTL_SETMLME              (SIOCIWFIRSTPRIV+3)     */
(INFRA_NETWORK),                                /* IEEE80211_IOCTL_ADDPMKID             (SIOCIWFIRSTPRIV+4)     */
(0),                                            /* IEEE80211_IOCTL_SETOPTIE             (SIOCIWFIRSTPRIV+5)     */
(0),                                            /*                                      (SIOCIWFIRSTPRIV+6)     */
(0),                                            /*                                      (SIOCIWFIRSTPRIV+7)     */
(0),                                            /*                                      (SIOCIWFIRSTPRIV+8)     */
(0),                                            /*                                      (SIOCIWFIRSTPRIV+9)     */
(0),                                            /* IEEE80211_IOCTL_LASTONE              (SIOCIWFIRSTPRIV+10)    */
(0xFF),                                         /* AR6000_IOCTL_WMI_GETREV              (SIOCIWFIRSTPRIV+11)    */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_IOCTL_WMI_SETPWR              (SIOCIWFIRSTPRIV+12)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SETSCAN             (SIOCIWFIRSTPRIV+13)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SETLISTENINT        (SIOCIWFIRSTPRIV+14)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SETBSSFILTER        (SIOCIWFIRSTPRIV+15)    */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_IOCTL_WMI_SET_CHANNELPARAMS   (SIOCIWFIRSTPRIV+16)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_PROBEDSSID      (SIOCIWFIRSTPRIV+17)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_PMPARAMS        (SIOCIWFIRSTPRIV+18)    */
(INFRA_NETWORK),                                /* AR6000_IOCTL_WMI_SET_BADAP           (SIOCIWFIRSTPRIV+19)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_GET_QOS_QUEUE       (SIOCIWFIRSTPRIV+20)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_CREATE_QOS          (SIOCIWFIRSTPRIV+21)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_DELETE_QOS          (SIOCIWFIRSTPRIV+22)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_SNRTHRESHOLD    (SIOCIWFIRSTPRIV+23)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_ERROR_REPORT_BITMASK (SIOCIWFIRSTPRIV+24)*/
(0xFF),                                         /* AR6000_IOCTL_WMI_GET_TARGET_STATS    (SIOCIWFIRSTPRIV+25)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_ASSOC_INFO      (SIOCIWFIRSTPRIV+26)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_ACCESS_PARAMS   (SIOCIWFIRSTPRIV+27)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_BMISS_TIME      (SIOCIWFIRSTPRIV+28)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_DISC_TIMEOUT    (SIOCIWFIRSTPRIV+29)    */
(ADHOC_NETWORK),                                /* AR6000_IOCTL_WMI_SET_IBSS_PM_CAPS    (SIOCIWFIRSTPRIV+30)    */
};



u8 xioctl_filter[] = {
(0xFF),                                         /* Dummy                                           0    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_DONE                          1    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_READ_MEMORY                   2    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_WRITE_MEMORY                  3    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_EXECUTE                       4    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_SET_APP_START                 5    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_READ_SOC_REGISTER             6    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_WRITE_SOC_REGISTER            7    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_TEST                          8    */
(0xFF),                                         /* AR6000_XIOCTL_UNUSED9                           9    */
(0xFF),                                         /* AR6000_XIOCTL_UNUSED10                          10   */
(0xFF),                                         /* AR6000_XIOCTL_UNUSED11                          11   */
(0xFF),                                         /* AR6000_XIOCTL_FORCE_TARGET_RESET                12   */
(0xFF),                                         /* AR6000_XIOCTL_HTC_RAW_OPEN                      13   */
(0xFF),                                         /* AR6000_XIOCTL_HTC_RAW_CLOSE                     14   */
(0xFF),                                         /* AR6000_XIOCTL_HTC_RAW_READ                      15   */
(0xFF),                                         /* AR6000_XIOCTL_HTC_RAW_WRITE                     16   */
(0xFF),                                         /* AR6000_XIOCTL_CHECK_TARGET_READY                17   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_OUTPUT_SET                   18   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_INPUT_GET                    19   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_REGISTER_SET                 20   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_REGISTER_GET                 21   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_INTR_ACK                     22   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_INTR_WAIT                    23   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_SET_ADHOC_BSSID                   24   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_SET_OPT_MODE                      25   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_OPT_SEND_FRAME                    26   */
(ADHOC_NETWORK | AP_NETWORK),                   /* AR6000_XIOCTL_SET_BEACON_INTVAL                 27   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* IEEE80211_IOCTL_SETAUTHALG                      28   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_SET_VOICE_PKT_SIZE                29   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_SET_MAX_SP                        30   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_ROAM_TBL                  31   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_ROAM_CTRL                 32   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTRL_WMI_SET_POWERSAVE_TIMERS         33   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTRL_WMI_GET_POWER_MODE               34   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTRL_WMI_SET_WLAN_STATE               35   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_ROAM_DATA                 36   */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SETRETRYLIMITS                37   */
(0xFF),                                         /* AR6000_XIOCTL_TCMD_CONT_TX                      38   */
(0xFF),                                         /* AR6000_XIOCTL_TCMD_CONT_RX                      39   */
(0xFF),                                         /* AR6000_XIOCTL_TCMD_PM                           40   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_STARTSCAN                     41   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SETFIXRATES                   42   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_GETFIXRATES                   43   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_RSSITHRESHOLD             44   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_CLR_RSSISNR                   45   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_LQTHRESHOLD               46   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_RTS                       47   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_LPREAMBLE                 48   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_AUTHMODE                  49   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_REASSOCMODE               50   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_WMM                       51   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_HB_CHALLENGE_RESP_PARAMS  52   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_HB_CHALLENGE_RESP         53   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_GET_RD                        54   */
(0xFF),                                         /* AR6000_XIOCTL_DIAG_READ                         55   */
(0xFF),                                         /* AR6000_XIOCTL_DIAG_WRITE                        56   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_TXOP                      57   */
(INFRA_NETWORK),                                /* AR6000_XIOCTL_USER_SETKEYS                      58   */
(INFRA_NETWORK),                                /* AR6000_XIOCTL_WMI_SET_KEEPALIVE                 59   */
(INFRA_NETWORK),                                /* AR6000_XIOCTL_WMI_GET_KEEPALIVE                 60   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_ROMPATCH_INSTALL              61   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_ROMPATCH_UNINSTALL            62   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_ROMPATCH_ACTIVATE             63   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_ROMPATCH_DEACTIVATE           64   */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_APPIE                     65   */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_MGMT_FRM_RX_FILTER        66   */
(0xFF),                                         /* AR6000_XIOCTL_DBGLOG_CFG_MODULE                 67   */
(0xFF),                                         /* AR6000_XIOCTL_DBGLOG_GET_DEBUG_LOGS             68   */
(0xFF),                                         /* Dummy                                           69   */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_WSC_STATUS                70   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BT_STATUS                 71   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BT_PARAMS                 72   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_HOST_SLEEP_MODE           73   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_WOW_MODE                  74   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_WOW_LIST                  75   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_ADD_WOW_PATTERN               76   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_DEL_WOW_PATTERN               77   */
(0xFF),                                         /* AR6000_XIOCTL_TARGET_INFO                       78   */
(0xFF),                                         /* AR6000_XIOCTL_DUMP_HTC_CREDIT_STATE             79   */
(0xFF),                                         /* AR6000_XIOCTL_TRAFFIC_ACTIVITY_CHANGE           80   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_CONNECT_CTRL_FLAGS        81   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_AKMP_PARAMS               82   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_PMKID_LIST                83   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_PMKID_LIST                84   */
(0xFF),                                         /* Dummy                                           85   */
(0xFF),                                         /* Dummy                                           86   */
(0xFF),                                         /* Dummy                                           87   */
(0xFF),                                         /* Dummy                                           88   */
(0xFF),                                         /* Dummy                                           89   */
(0xFF),                                         /* AR6000_XIOCTL_UNUSED90                          90   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_LZ_STREAM_START               91   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_LZ_DATA                       92   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_CFG                          93   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_ADDR_SET                     94   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_START                        95   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_STOP                         96   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_COUNT_GET                    97   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_ABORT_SCAN                    98   */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_STA_LIST                   99   */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_HIDDEN_SSID                    100  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_NUM_STA                    101  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_ACL_MAC                    102  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_ACL_LIST                   103  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_COMMIT_CONFIG                  104  */
(AP_NETWORK),                                   /* IEEE80211_IOCTL_GETWPAIE                        105  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_CONN_INACT_TIME                106  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_PROT_SCAN_TIME                 107  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_WMI_SET_COUNTRY                   108  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_DTIM                       109  */
(0xFF),                                         /* AR6000_XIOCTL_WMI_TARGET_EVENT_REPORT           110  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_SET_IP                            111  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_ACL_POLICY                 112  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_INTRA_BSS_COMM                 113  */
(0xFF),                                         /* AR6000_XIOCTL_DUMP_MODULE_DEBUG_INFO            114  */
(0xFF),                                         /* AR6000_XIOCTL_MODULE_DEBUG_SET_MASK             115  */
(0xFF),                                         /* AR6000_XIOCTL_MODULE_DEBUG_GET_MASK             116  */
(0xFF),                                         /* AR6000_XIOCTL_DUMP_RCV_AGGR_STATS               117  */
(0xFF),                                         /* AR6000_XIOCTL_SET_HT_CAP                        118  */
(0xFF),                                         /* AR6000_XIOCTL_SET_HT_OP                         119  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_STAT                       120  */
(0xFF),                                         /* AR6000_XIOCTL_SET_TX_SELECT_RATES               121  */
(0xFF),                                         /* AR6000_XIOCTL_SETUP_AGGR                        122  */
(0xFF),                                         /* AR6000_XIOCTL_ALLOW_AGGR                        123  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_HIDDEN_SSID                124  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_COUNTRY                    125  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_WMODE                      126  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_DTIM                       127  */
(AP_NETWORK | ADHOC_NETWORK),                   /* AR6000_XIOCTL_AP_GET_BINTVL                     128  */
(0xFF),                                         /* AR6000_XIOCTL_AP_GET_RTS                        129  */
(0xFF),                                         /* AR6000_XIOCTL_DELE_AGGR                         130  */
(0xFF),                                         /* AR6000_XIOCTL_FETCH_TARGET_REGS                 131  */
(0xFF),                                         /* AR6000_XIOCTL_HCI_CMD                           132  */
(0xFF),                                         /* AR6000_XIOCTL_ACL_DATA(used to be used for PAL) 133  */
(0xFF),                                         /* AR6000_XIOCTL_WLAN_CONN_PRECEDENCE              134  */
(AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_11BG_RATESET               135  */
(0xFF),
(0xFF),
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BTCOEX_FE_ANT             138  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BTCOEX_COLOCATED_BT_DEV   139  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG  140  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BTCOEX_SCO_CONFIG         141  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BTCOEX_A2DP_CONFIG        142  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BTCOEX_ACLCOEX_CONFIG     143  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BTCOEX_DEBUG              144  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BT_OPERATING_STATUS       145  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_BTCOEX_CONFIG             146  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_BTCOEX_GET_STATS          147  */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_QOS_SUPP                  148  */
(0xFF),                                         /* AR6000_XIOCTL_GET_WLAN_SLEEP_STATE              149  */
(0xFF),                                         /* AR6000_XIOCTL_SET_BT_HW_POWER_STATE             150  */
(0xFF),                                         /* AR6000_XIOCTL_GET_BT_HW_POWER_STATE             151  */
(0xFF),                                         /* AR6000_XIOCTL_ADD_AP_INTERFACE                  152  */
(0xFF),                                         /* AR6000_XIOCTL_REMOVE_AP_INTERFACE               153  */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_TX_SGI_PARAM              154  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_WPA_OFFLOAD_STATE         155  */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_PASSPHRASE                156  */
(0xFF),
(0xFF),
(0xFF),
(0xFF),
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_EXCESS_TX_RETRY_THRES     161  */
};

#endif /*_WMI_FILTER_LINUX_H_*/
