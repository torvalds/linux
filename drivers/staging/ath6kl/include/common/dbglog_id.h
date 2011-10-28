//------------------------------------------------------------------------------
// <copyright file="dbglog_id.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#ifndef _DBGLOG_ID_H_
#define _DBGLOG_ID_H_

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * The nomenclature for the debug identifiers is MODULE_DESCRIPTION.
 * Please ensure that the definition of any new debugid introduced is captured
 * between the <MODULE>_DBGID_DEFINITION_START and 
 * <MODULE>_DBGID_DEFINITION_END defines. The structure is required for the 
 * parser to correctly pick up the values for different debug identifiers.
 */

/* INF debug identifier definitions */
#define INF_DBGID_DEFINITION_START
#define INF_ASSERTION_FAILED                          1
#define INF_TARGET_ID                                 2
#define INF_DBGID_DEFINITION_END

/* WMI debug identifier definitions */
#define WMI_DBGID_DEFINITION_START
#define WMI_CMD_RX_XTND_PKT_TOO_SHORT                 1
#define WMI_EXTENDED_CMD_NOT_HANDLED                  2
#define WMI_CMD_RX_PKT_TOO_SHORT                      3
#define WMI_CALLING_WMI_EXTENSION_FN                  4
#define WMI_CMD_NOT_HANDLED                           5
#define WMI_IN_SYNC                                   6
#define WMI_TARGET_WMI_SYNC_CMD                       7
#define WMI_SET_SNR_THRESHOLD_PARAMS                  8
#define WMI_SET_RSSI_THRESHOLD_PARAMS                 9
#define WMI_SET_LQ_TRESHOLD_PARAMS                   10
#define WMI_TARGET_CREATE_PSTREAM_CMD                11
#define WMI_WI_DTM_INUSE                             12
#define WMI_TARGET_DELETE_PSTREAM_CMD                13
#define WMI_TARGET_IMPLICIT_DELETE_PSTREAM_CMD       14
#define WMI_TARGET_GET_BIT_RATE_CMD                  15
#define WMI_GET_RATE_MASK_CMD_FIX_RATE_MASK_IS       16
#define WMI_TARGET_GET_AVAILABLE_CHANNELS_CMD        17
#define WMI_TARGET_GET_TX_PWR_CMD                    18
#define WMI_FREE_EVBUF_WMIBUF                        19
#define WMI_FREE_EVBUF_DATABUF                       20
#define WMI_FREE_EVBUF_BADFLAG                       21
#define WMI_HTC_RX_ERROR_DATA_PACKET                 22
#define WMI_HTC_RX_SYNC_PAUSING_FOR_MBOX             23
#define WMI_INCORRECT_WMI_DATA_HDR_DROPPING_PKT      24
#define WMI_SENDING_READY_EVENT                      25
#define WMI_SETPOWER_MDOE_TO_MAXPERF                 26
#define WMI_SETPOWER_MDOE_TO_REC                     27
#define WMI_BSSINFO_EVENT_FROM                       28
#define WMI_TARGET_GET_STATS_CMD                     29
#define WMI_SENDING_SCAN_COMPLETE_EVENT              30
#define WMI_SENDING_RSSI_INDB_THRESHOLD_EVENT        31
#define WMI_SENDING_RSSI_INDBM_THRESHOLD_EVENT       32
#define WMI_SENDING_LINK_QUALITY_THRESHOLD_EVENT     33
#define WMI_SENDING_ERROR_REPORT_EVENT               34
#define WMI_SENDING_CAC_EVENT                        35
#define WMI_TARGET_GET_ROAM_TABLE_CMD                36
#define WMI_TARGET_GET_ROAM_DATA_CMD                 37
#define WMI_SENDING_GPIO_INTR_EVENT                  38
#define WMI_SENDING_GPIO_ACK_EVENT                   39
#define WMI_SENDING_GPIO_DATA_EVENT                  40
#define WMI_CMD_RX                                   41
#define WMI_CMD_RX_XTND                              42
#define WMI_EVENT_SEND                               43
#define WMI_EVENT_SEND_XTND                          44
#define WMI_CMD_PARAMS_DUMP_START                    45
#define WMI_CMD_PARAMS_DUMP_END                      46
#define WMI_CMD_PARAMS                               47
#define WMI_DBGID_DEFINITION_END

/* MISC debug identifier definitions */
#define MISC_DBGID_DEFINITION_START
#define MISC_WLAN_SCHEDULER_EVENT_REGISTER_ERROR     1
#define TLPM_INIT                                    2
#define TLPM_FILTER_POWER_STATE                      3
#define TLPM_NOTIFY_NOT_IDLE                         4
#define TLPM_TIMEOUT_IDLE_HANDLER                    5
#define TLPM_TIMEOUT_WAKEUP_HANDLER                  6
#define TLPM_WAKEUP_SIGNAL_HANDLER                   7
#define TLPM_UNEXPECTED_GPIO_INTR_ERROR              8
#define TLPM_BREAK_ON_NOT_RECEIVED_ERROR             9
#define TLPM_BREAK_OFF_NOT_RECIVED_ERROR             10
#define TLPM_ACK_GPIO_INTR                           11
#define TLPM_ON                                      12
#define TLPM_OFF                                     13
#define TLPM_WAKEUP_FROM_HOST                        14
#define TLPM_WAKEUP_FROM_BT                          15 
#define TLPM_TX_BREAK_RECIVED                        16
#define TLPM_IDLE_TIMER_NOT_RUNNING                  17
#define MISC_DBGID_DEFINITION_END
    
/* TXRX debug identifier definitions */
#define TXRX_TXBUF_DBGID_DEFINITION_START
#define TXRX_TXBUF_ALLOCATE_BUF                      1
#define TXRX_TXBUF_QUEUE_BUF_TO_MBOX                 2
#define TXRX_TXBUF_QUEUE_BUF_TO_TXQ                  3
#define TXRX_TXBUF_TXQ_DEPTH                         4   
#define TXRX_TXBUF_IBSS_QUEUE_TO_SFQ                 5
#define TXRX_TXBUF_IBSS_QUEUE_TO_TXQ_FRM_SFQ         6
#define TXRX_TXBUF_INITIALIZE_TIMER                  7
#define TXRX_TXBUF_ARM_TIMER                         8
#define TXRX_TXBUF_DISARM_TIMER                      9
#define TXRX_TXBUF_UNINITIALIZE_TIMER                10
#define TXRX_TXBUF_DBGID_DEFINITION_END
 
#define TXRX_RXBUF_DBGID_DEFINITION_START    
#define TXRX_RXBUF_ALLOCATE_BUF                      1
#define TXRX_RXBUF_QUEUE_TO_HOST                     2
#define TXRX_RXBUF_QUEUE_TO_WLAN                     3
#define TXRX_RXBUF_ZERO_LEN_BUF                      4
#define TXRX_RXBUF_QUEUE_TO_HOST_LASTBUF_IN_RXCHAIN  5
#define TXRX_RXBUF_LASTBUF_IN_RXCHAIN_ZEROBUF        6
#define TXRX_RXBUF_QUEUE_EMPTY_QUEUE_TO_WLAN         7
#define TXRX_RXBUF_SEND_TO_RECV_MGMT                 8
#define TXRX_RXBUF_SEND_TO_IEEE_LAYER                9
#define TXRX_RXBUF_REQUEUE_ERROR                     10
#define TXRX_RXBUF_DBGID_DEFINITION_END

#define TXRX_MGMTBUF_DBGID_DEFINITION_START 
#define TXRX_MGMTBUF_ALLOCATE_BUF                    1
#define TXRX_MGMTBUF_ALLOCATE_SM_BUF                 2    
#define TXRX_MGMTBUF_ALLOCATE_RMBUF                  3
#define TXRX_MGMTBUF_GET_BUF                         4
#define TXRX_MGMTBUF_GET_SM_BUF                      5
#define TXRX_MGMTBUF_QUEUE_BUF_TO_TXQ                6
#define TXRX_MGMTBUF_REAPED_BUF                      7
#define TXRX_MGMTBUF_REAPED_SM_BUF                   8
#define TXRX_MGMTBUF_WAIT_FOR_TXQ_DRAIN              9
#define TXRX_MGMTBUF_WAIT_FOR_TXQ_SFQ_DRAIN          10
#define TXRX_MGMTBUF_ENQUEUE_INTO_DATA_SFQ           11
#define TXRX_MGMTBUF_DEQUEUE_FROM_DATA_SFQ           12
#define TXRX_MGMTBUF_PAUSE_DATA_TXQ                  13
#define TXRX_MGMTBUF_RESUME_DATA_TXQ                 14
#define TXRX_MGMTBUF_WAIT_FORTXQ_DRAIN_TIMEOUT       15
#define TXRX_MGMTBUF_DRAINQ                          16
#define TXRX_MGMTBUF_INDICATE_Q_DRAINED              17
#define TXRX_MGMTBUF_ENQUEUE_INTO_HW_SFQ             18
#define TXRX_MGMTBUF_DEQUEUE_FROM_HW_SFQ             19
#define TXRX_MGMTBUF_PAUSE_HW_TXQ                    20
#define TXRX_MGMTBUF_RESUME_HW_TXQ                   21
#define TXRX_MGMTBUF_TEAR_DOWN_BA                    22
#define TXRX_MGMTBUF_PROCESS_ADDBA_REQ               23
#define TXRX_MGMTBUF_PROCESS_DELBA                   24
#define TXRX_MGMTBUF_PERFORM_BA                      25
#define TXRX_MGMTBUF_WLAN_RESET_ON_ERROR             26 
#define TXRX_MGMTBUF_DBGID_DEFINITION_END

/* PM (Power Module) debug identifier definitions */
#define PM_DBGID_DEFINITION_START
#define PM_INIT                                      1
#define PM_ENABLE                                    2
#define PM_SET_STATE                                 3
#define PM_SET_POWERMODE                             4
#define PM_CONN_NOTIFY                               5
#define PM_REF_COUNT_NEGATIVE                        6
#define PM_INFRA_STA_APSD_ENABLE                     7
#define PM_INFRA_STA_UPDATE_APSD_STATE               8
#define PM_CHAN_OP_REQ                               9
#define PM_SET_MY_BEACON_POLICY                      10
#define PM_SET_ALL_BEACON_POLICY                     11
#define PM_INFRA_STA_SET_PM_PARAMS1                  12
#define PM_INFRA_STA_SET_PM_PARAMS2                  13
#define PM_ADHOC_SET_PM_CAPS_FAIL                    14
#define PM_ADHOC_UNKNOWN_IBSS_ATTRIB_ID              15
#define PM_ADHOC_SET_PM_PARAMS                       16
#define PM_ADHOC_STATE1                              18
#define PM_ADHOC_STATE2                              19
#define PM_ADHOC_CONN_MAP                            20 
#define PM_FAKE_SLEEP                                21
#define PM_AP_STATE1                                 22
#define PM_AP_SET_PM_PARAMS                          23
#define PM_DBGID_DEFINITION_END

/* Wake on Wireless debug identifier definitions */
#define WOW_DBGID_DEFINITION_START
#define WOW_INIT                                        1
#define WOW_GET_CONFIG_DSET                             2   
#define WOW_NO_CONFIG_DSET                              3
#define WOW_INVALID_CONFIG_DSET                         4
#define WOW_USE_DEFAULT_CONFIG                          5
#define WOW_SETUP_GPIO                                  6
#define WOW_INIT_DONE                                   7
#define WOW_SET_GPIO_PIN                                8
#define WOW_CLEAR_GPIO_PIN                              9
#define WOW_SET_WOW_MODE_CMD                            10
#define WOW_SET_HOST_MODE_CMD                           11  
#define WOW_ADD_WOW_PATTERN_CMD                         12    
#define WOW_NEW_WOW_PATTERN_AT_INDEX                    13    
#define WOW_DEL_WOW_PATTERN_CMD                         14    
#define WOW_LIST_CONTAINS_PATTERNS                      15    
#define WOW_GET_WOW_LIST_CMD                            16 
#define WOW_INVALID_FILTER_ID                           17
#define WOW_INVALID_FILTER_LISTID                       18
#define WOW_NO_VALID_FILTER_AT_ID                       19
#define WOW_NO_VALID_LIST_AT_ID                         20
#define WOW_NUM_PATTERNS_EXCEEDED                       21
#define WOW_NUM_LISTS_EXCEEDED                          22
#define WOW_GET_WOW_STATS                               23
#define WOW_CLEAR_WOW_STATS                             24
#define WOW_WAKEUP_HOST                                 25
#define WOW_EVENT_WAKEUP_HOST                           26
#define WOW_EVENT_DISCARD                               27
#define WOW_PATTERN_MATCH                               28
#define WOW_PATTERN_NOT_MATCH                           29
#define WOW_PATTERN_NOT_MATCH_OFFSET                    30
#define WOW_DISABLED_HOST_ASLEEP                        31
#define WOW_ENABLED_HOST_ASLEEP_NO_PATTERNS             32
#define WOW_ENABLED_HOST_ASLEEP_NO_MATCH_FOUND          33
#define WOW_DBGID_DEFINITION_END

/* WHAL debug identifier definitions */
#define WHAL_DBGID_DEFINITION_START
#define WHAL_ERROR_ANI_CONTROL                      1
#define WHAL_ERROR_CHIP_TEST1                       2
#define WHAL_ERROR_CHIP_TEST2                       3
#define WHAL_ERROR_EEPROM_CHECKSUM                  4
#define WHAL_ERROR_EEPROM_MACADDR                   5
#define WHAL_ERROR_INTERRUPT_HIU                    6
#define WHAL_ERROR_KEYCACHE_RESET                   7
#define WHAL_ERROR_KEYCACHE_SET                     8 
#define WHAL_ERROR_KEYCACHE_TYPE                    9
#define WHAL_ERROR_KEYCACHE_TKIPENTRY              10
#define WHAL_ERROR_KEYCACHE_WEPLENGTH              11
#define WHAL_ERROR_PHY_INVALID_CHANNEL             12
#define WHAL_ERROR_POWER_AWAKE                     13
#define WHAL_ERROR_POWER_SET                       14
#define WHAL_ERROR_RECV_STOPDMA                    15
#define WHAL_ERROR_RECV_STOPPCU                    16
#define WHAL_ERROR_RESET_CHANNF1                   17
#define WHAL_ERROR_RESET_CHANNF2                   18
#define WHAL_ERROR_RESET_PM                        19
#define WHAL_ERROR_RESET_OFFSETCAL                 20
#define WHAL_ERROR_RESET_RFGRANT                   21
#define WHAL_ERROR_RESET_RXFRAME                   22
#define WHAL_ERROR_RESET_STOPDMA                   23
#define WHAL_ERROR_RESET_RECOVER                   24
#define WHAL_ERROR_XMIT_COMPUTE                    25
#define WHAL_ERROR_XMIT_NOQUEUE                    26
#define WHAL_ERROR_XMIT_ACTIVEQUEUE                27
#define WHAL_ERROR_XMIT_BADTYPE                    28
#define WHAL_ERROR_XMIT_STOPDMA                    29
#define WHAL_ERROR_INTERRUPT_BB_PANIC              30 
#define WHAL_ERROR_RESET_TXIQCAL                   31 
#define WHAL_ERROR_PAPRD_MAXGAIN_ABOVE_WINDOW      32 
#define WHAL_DBGID_DEFINITION_END

/* DC debug identifier definitions */
#define DC_DBGID_DEFINITION_START
#define DC_SCAN_CHAN_START                          1
#define DC_SCAN_CHAN_FINISH                         2
#define DC_BEACON_RECEIVE7                          3
#define DC_SSID_PROBE_CB                            4
#define DC_SEND_NEXT_SSID_PROBE                     5
#define DC_START_SEARCH                             6
#define DC_CANCEL_SEARCH_CB                         7
#define DC_STOP_SEARCH                              8
#define DC_END_SEARCH                               9
#define DC_MIN_CHDWELL_TIMEOUT                     10
#define DC_START_SEARCH_CANCELED                   11
#define DC_SET_POWER_MODE                          12
#define DC_INIT                                    13
#define DC_SEARCH_OPPORTUNITY                      14
#define DC_RECEIVED_ANY_BEACON                     15
#define DC_RECEIVED_MY_BEACON                      16
#define DC_PROFILE_IS_ADHOC_BUT_BSS_IS_INFRA       17
#define DC_PS_ENABLED_BUT_ATHEROS_IE_ABSENT        18
#define DC_BSS_ADHOC_CHANNEL_NOT_ALLOWED           19
#define DC_SET_BEACON_UPDATE                       20
#define DC_BEACON_UPDATE_COMPLETE                  21
#define DC_END_SEARCH_BEACON_UPDATE_COMP_CB        22
#define DC_BSSINFO_EVENT_DROPPED                   23
#define DC_IEEEPS_ENABLED_BUT_ATIM_ABSENT          24 
#define DC_DBGID_DEFINITION_END

/* CO debug identifier definitions */
#define CO_DBGID_DEFINITION_START
#define CO_INIT                                     1
#define CO_ACQUIRE_LOCK                             2
#define CO_START_OP1                                3
#define CO_START_OP2                                4
#define CO_DRAIN_TX_COMPLETE_CB                     5
#define CO_CHANGE_CHANNEL_CB                        6
#define CO_RETURN_TO_HOME_CHANNEL                   7
#define CO_FINISH_OP_TIMEOUT                        8
#define CO_OP_END                                   9
#define CO_CANCEL_OP                               10
#define CO_CHANGE_CHANNEL                          11
#define CO_RELEASE_LOCK                            12
#define CO_CHANGE_STATE                            13
#define CO_DBGID_DEFINITION_END

/* RO debug identifier definitions */
#define RO_DBGID_DEFINITION_START
#define RO_REFRESH_ROAM_TABLE                       1
#define RO_UPDATE_ROAM_CANDIDATE                    2
#define RO_UPDATE_ROAM_CANDIDATE_CB                 3
#define RO_UPDATE_ROAM_CANDIDATE_FINISH             4
#define RO_REFRESH_ROAM_TABLE_DONE                  5
#define RO_PERIODIC_SEARCH_CB                       6
#define RO_PERIODIC_SEARCH_TIMEOUT                  7
#define RO_INIT                                     8
#define RO_BMISS_STATE1                             9
#define RO_BMISS_STATE2                            10
#define RO_SET_PERIODIC_SEARCH_ENABLE              11
#define RO_SET_PERIODIC_SEARCH_DISABLE             12
#define RO_ENABLE_SQ_THRESHOLD                     13
#define RO_DISABLE_SQ_THRESHOLD                    14
#define RO_ADD_BSS_TO_ROAM_TABLE                   15
#define RO_SET_PERIODIC_SEARCH_MODE                16
#define RO_CONFIGURE_SQ_THRESHOLD1                 17
#define RO_CONFIGURE_SQ_THRESHOLD2                 18
#define RO_CONFIGURE_SQ_PARAMS                     19
#define RO_LOW_SIGNAL_QUALITY_EVENT                20
#define RO_HIGH_SIGNAL_QUALITY_EVENT               21
#define RO_REMOVE_BSS_FROM_ROAM_TABLE              22
#define RO_UPDATE_CONNECTION_STATE_METRIC          23
#define RO_DBGID_DEFINITION_END

/* CM debug identifier definitions */
#define CM_DBGID_DEFINITION_START
#define CM_INITIATE_HANDOFF                         1
#define CM_INITIATE_HANDOFF_CB                      2
#define CM_CONNECT_EVENT                            3
#define CM_DISCONNECT_EVENT                         4
#define CM_INIT                                     5
#define CM_HANDOFF_SOURCE                           6
#define CM_SET_HANDOFF_TRIGGERS                     7
#define CM_CONNECT_REQUEST                          8
#define CM_CONNECT_REQUEST_CB                       9
#define CM_CONTINUE_SCAN_CB                         10 
#define CM_DBGID_DEFINITION_END


/* mgmt debug identifier definitions */
#define MGMT_DBGID_DEFINITION_START
#define KEYMGMT_CONNECTION_INIT                     1
#define KEYMGMT_CONNECTION_COMPLETE                 2
#define KEYMGMT_CONNECTION_CLOSE                    3
#define KEYMGMT_ADD_KEY                             4
#define MLME_NEW_STATE                              5
#define MLME_CONN_INIT                              6
#define MLME_CONN_COMPLETE                          7
#define MLME_CONN_CLOSE                             8 
#define MGMT_DBGID_DEFINITION_END

/* TMR debug identifier definitions */
#define TMR_DBGID_DEFINITION_START
#define TMR_HANG_DETECTED                           1
#define TMR_WDT_TRIGGERED                           2
#define TMR_WDT_RESET                               3
#define TMR_HANDLER_ENTRY                           4
#define TMR_HANDLER_EXIT                            5
#define TMR_SAVED_START                             6
#define TMR_SAVED_END                               7
#define TMR_DBGID_DEFINITION_END

/* BTCOEX debug identifier definitions */
#define BTCOEX_DBGID_DEFINITION_START
#define BTCOEX_STATUS_CMD                           1
#define BTCOEX_PARAMS_CMD                           2
#define BTCOEX_ANT_CONFIG                           3
#define BTCOEX_COLOCATED_BT_DEVICE                  4
#define BTCOEX_CLOSE_RANGE_SCO_ON                   5
#define BTCOEX_CLOSE_RANGE_SCO_OFF                  6
#define BTCOEX_CLOSE_RANGE_A2DP_ON                  7
#define BTCOEX_CLOSE_RANGE_A2DP_OFF                 8
#define BTCOEX_A2DP_PROTECT_ON                      9
#define BTCOEX_A2DP_PROTECT_OFF                     10
#define BTCOEX_SCO_PROTECT_ON                       11
#define BTCOEX_SCO_PROTECT_OFF                      12
#define BTCOEX_CLOSE_RANGE_DETECTOR_START           13
#define BTCOEX_CLOSE_RANGE_DETECTOR_STOP            14
#define BTCOEX_CLOSE_RANGE_TOGGLE                   15
#define BTCOEX_CLOSE_RANGE_TOGGLE_RSSI_LRCNT        16
#define BTCOEX_CLOSE_RANGE_RSSI_THRESH              17
#define BTCOEX_CLOSE_RANGE_LOW_RATE_THRESH          18
#define BTCOEX_PTA_PRI_INTR_HANDLER  		        19
#define BTCOEX_PSPOLL_QUEUED						20
#define BTCOEX_PSPOLL_COMPLETE						21
#define BTCOEX_DBG_PM_AWAKE							22
#define BTCOEX_DBG_PM_SLEEP							23
#define BTCOEX_DBG_SCO_COEX_ON						24
#define BTCOEX_SCO_DATARECEIVE						25
#define BTCOEX_INTR_INIT							26
#define BTCOEX_PTA_PRI_DIFF							27
#define BTCOEX_TIM_NOTIFICATION						28
#define BTCOEX_SCO_WAKEUP_ON_DATA					29
#define BTCOEX_SCO_SLEEP							30
#define BTCOEX_SET_WEIGHTS							31
#define BTCOEX_SCO_DATARECEIVE_LATENCY_VAL			32
#define BTCOEX_SCO_MEASURE_TIME_DIFF				33
#define BTCOEX_SET_EOL_VAL							34
#define BTCOEX_OPT_DETECT_HANDLER					35
#define BTCOEX_SCO_TOGGLE_STATE						36
#define BTCOEX_SCO_STOMP							37
#define BTCOEX_NULL_COMP_CALLBACK					38
#define BTCOEX_RX_INCOMING							39
#define BTCOEX_RX_INCOMING_CTL						40
#define BTCOEX_RX_INCOMING_MGMT						41
#define BTCOEX_RX_INCOMING_DATA						42
#define BTCOEX_RTS_RECEPTION						43
#define BTCOEX_FRAME_PRI_LOW_RATE_THRES				44
#define BTCOEX_PM_FAKE_SLEEP						45
#define BTCOEX_ACL_COEX_STATUS						46
#define BTCOEX_ACL_COEX_DETECTION					47
#define BTCOEX_A2DP_COEX_STATUS						48
#define BTCOEX_SCO_STATUS							49
#define BTCOEX_WAKEUP_ON_DATA						50
#define BTCOEX_DATARECEIVE							51
#define BTCOEX_GET_MAX_AGGR_SIZE					53
#define BTCOEX_MAX_AGGR_AVAIL_TIME					54
#define BTCOEX_DBG_WBTIMER_INTR						55
#define BTCOEX_DBG_SCO_SYNC                         57
#define BTCOEX_UPLINK_QUEUED_RATE  					59
#define BTCOEX_DBG_UPLINK_ENABLE_EOL				60
#define BTCOEX_UPLINK_FRAME_DURATION				61
#define BTCOEX_UPLINK_SET_EOL						62
#define BTCOEX_DBG_EOL_EXPIRED						63
#define BTCOEX_DBG_DATA_COMPLETE					64
#define BTCOEX_UPLINK_QUEUED_TIMESTAMP				65
#define BTCOEX_DBG_DATA_COMPLETE_TIME				66
#define BTCOEX_DBG_A2DP_ROLE_IS_SLAVE               67
#define BTCOEX_DBG_A2DP_ROLE_IS_MASTER              68
#define BTCOEX_DBG_UPLINK_SEQ_NUM					69
#define BTCOEX_UPLINK_AGGR_SEQ						70
#define BTCOEX_DBG_TX_COMP_SEQ_NO					71
#define BTCOEX_DBG_MAX_AGGR_PAUSE_STATE				72
#define BTCOEX_DBG_ACL_TRAFFIC                      73
#define BTCOEX_CURR_AGGR_PROP						74
#define BTCOEX_DBG_SCO_GET_PER_TIME_DIFF 			75
#define BTCOEX_PSPOLL_PROCESS						76
#define BTCOEX_RETURN_FROM_MAC						77
#define BTCOEX_FREED_REQUEUED_CNT					78
#define BTCOEX_DBG_TOGGLE_LOW_RATES					79
#define BTCOEX_MAC_GOES_TO_SLEEP    				80
#define BTCOEX_DBG_A2DP_NO_SYNC                     81
#define BTCOEX_RETURN_FROM_MAC_HOLD_Q_INFO			82
#define BTCOEX_RETURN_FROM_MAC_AC					83
#define BTCOEX_DBG_DTIM_RECV                        84
#define BTCOEX_IS_PRE_UPDATE						86
#define BTCOEX_ENQUEUED_BIT_MAP						87
#define BTCOEX_TX_COMPLETE_FIRST_DESC_STATS			88
#define BTCOEX_UPLINK_DESC							89
#define BTCOEX_SCO_GET_PER_FIRST_FRM_TIMESTAMP		90
#define BTCOEX_DBG_RECV_ACK							94
#define BTCOEX_DBG_ADDBA_INDICATION                 95
#define BTCOEX_TX_COMPLETE_EOL_FAILED				96
#define BTCOEX_DBG_A2DP_USAGE_COMPLETE  			97
#define BTCOEX_DBG_A2DP_STOMP_FOR_BCN_HANDLER		98
#define BTCOEX_DBG_A2DP_SYNC_INTR                   99
#define BTCOEX_DBG_A2DP_STOMP_FOR_BCN_RECEPTION	   100
#define BTCOEX_FORM_AGGR_CURR_AGGR				   101
#define BTCOEX_DBG_TOGGLE_A2DP_BURST_CNT           102
#define BTCOEX_DBG_BT_TRAFFIC   				   103
#define BTCOEX_DBG_STOMP_BT_TRAFFIC 			   104
#define BTCOEX_RECV_NULL                           105
#define BTCOEX_DBG_A2DP_MASTER_BT_END			   106
#define BTCOEX_DBG_A2DP_BT_START				   107
#define BTCOEX_DBG_A2DP_SLAVE_BT_END			   108
#define BTCOEX_DBG_A2DP_STOMP_BT				   109
#define BTCOEX_DBG_GO_TO_SLEEP					   110
#define BTCOEX_DBG_A2DP_PKT						   111
#define BTCOEX_DBG_A2DP_PSPOLL_DATA_RECV		   112
#define BTCOEX_DBG_A2DP_NULL					   113
#define BTCOEX_DBG_UPLINK_DATA					   114
#define BTCOEX_DBG_A2DP_STOMP_LOW_PRIO_NULL		   115
#define BTCOEX_DBG_ADD_BA_RESP_TIMEOUT			   116
#define BTCOEX_DBG_TXQ_STATE					   117
#define BTCOEX_DBG_ALLOW_SCAN					   118
#define BTCOEX_DBG_SCAN_REQUEST					   119
#define BTCOEX_A2DP_SLEEP						   127
#define BTCOEX_DBG_DATA_ACTIV_TIMEOUT			   128
#define BTCOEX_DBG_SWITCH_TO_PSPOLL_ON_MODE		   129
#define BTCOEX_DBG_SWITCH_TO_PSPOLL_OFF_MODE 	   130
#define BTCOEX_DATARECEIVE_AGGR					   131
#define BTCOEX_DBG_DATA_RECV_SLEEPING_PENDING	   132
#define BTCOEX_DBG_DATARESP_TIMEOUT				   133
#define BTCOEX_BDG_BMISS						   134
#define BTCOEX_DBG_DATA_RECV_WAKEUP_TIM			   135
#define BTCOEX_DBG_SECOND_BMISS					   136
#define BTCOEX_DBG_SET_WLAN_STATE				   138
#define BTCOEX_BDG_FIRST_BMISS					   139
#define BTCOEX_DBG_A2DP_CHAN_OP					   140
#define BTCOEX_DBG_A2DP_INTR					   141
#define BTCOEX_DBG_BT_INQUIRY					   142
#define BTCOEX_DBG_BT_INQUIRY_DATA_FETCH		   143
#define BTCOEX_DBG_POST_INQUIRY_FINISH			   144
#define BTCOEX_DBG_SCO_OPT_MODE_TIMER_HANDLER	   145
#define BTCOEX_DBG_NULL_FRAME_SLEEP				   146
#define BTCOEX_DBG_NULL_FRAME_AWAKE				   147
#define BTCOEX_DBG_SET_AGGR_SIZE				   152
#define BTCOEX_DBG_TEAR_BA_TIMEOUT				   153
#define BTCOEX_DBG_MGMT_FRAME_SEQ_NO			   154
#define BTCOEX_DBG_SCO_STOMP_HIGH_PRI			   155
#define BTCOEX_DBG_COLOCATED_BT_DEV				   156
#define BTCOEX_DBG_FE_ANT_TYPE				       157
#define BTCOEX_DBG_BT_INQUIRY_CMD				   158
#define BTCOEX_DBG_SCO_CONFIG					   159
#define BTCOEX_DBG_SCO_PSPOLL_CONFIG			   160
#define BTCOEX_DBG_SCO_OPTMODE_CONFIG		       161
#define BTCOEX_DBG_A2DP_CONFIG				       162
#define BTCOEX_DBG_A2DP_PSPOLL_CONFIG		       163
#define BTCOEX_DBG_A2DP_OPTMODE_CONFIG		       164
#define BTCOEX_DBG_ACLCOEX_CONFIG			       165
#define BTCOEX_DBG_ACLCOEX_PSPOLL_CONFIG		   166
#define BTCOEX_DBG_ACLCOEX_OPTMODE_CONFIG	       167
#define BTCOEX_DBG_DEBUG_CMD					   168
#define BTCOEX_DBG_SET_BT_OPERATING_STATUS		   169
#define BTCOEX_DBG_GET_CONFIG					   170
#define BTCOEX_DBG_GET_STATS					   171
#define BTCOEX_DBG_BT_OPERATING_STATUS			   172
#define BTCOEX_DBG_PERFORM_RECONNECT               173
#define BTCOEX_DBG_ACL_WLAN_MED                    175
#define BTCOEX_DBG_ACL_BT_MED                      176
#define BTCOEX_DBG_WLAN_CONNECT                    177
#define BTCOEX_DBG_A2DP_DUAL_START                 178
#define BTCOEX_DBG_PMAWAKE_NOTIFY                  179
#define BTCOEX_DBG_BEACON_SCAN_ENABLE              180
#define BTCOEX_DBG_BEACON_SCAN_DISABLE             181
#define BTCOEX_DBG_RX_NOTIFY                       182
#define BTCOEX_SCO_GET_PER_SECOND_FRM_TIMESTAMP    183
#define BTCOEX_DBG_TXQ_DETAILS                     184
#define BTCOEX_DBG_SCO_STOMP_LOW_PRI               185
#define BTCOEX_DBG_A2DP_FORCE_SCAN                 186
#define BTCOEX_DBG_DTIM_STOMP_COMP                 187
#define BTCOEX_ACL_PRESENCE_TIMER                  188
#define BTCOEX_DBGID_DEFINITION_END

#ifdef __cplusplus
}
#endif

#endif /* _DBGLOG_ID_H_ */
