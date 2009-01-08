/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : wlan_defs.h                                           */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains WLAN definitions.                          */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/

#ifndef _WLAN_H
#define _WLAN_H


#define ZM_EXTERNAL_ALLOC_BUF               0
#define ZM_INTERNAL_ALLOC_BUF               1

#define ZM_SIZE_OF_CTRL_SET                 8
#define ZM_SIZE_OF_IV                       4
#define ZM_SIZE_OF_EXT_IV                   4
#define ZM_SIZE_OF_MIC                      8
#define ZM_SIZE_OF_CCX_MIC                  8
#define ZM_SIZE_OF_WLAN_DATA_HEADER         24
#define ZM_SIZE_OF_QOS_CTRL                 2

/* Header definition */
#define ZM_SIZE_OF_WLAN_WDS_HEADER          32
#define ZM_SIZE_OF_SNAP_HEADER              8

#define ZM_WLAN_HEADER_A1_OFFSET            4
#define ZM_WLAN_HEADER_A2_OFFSET            10
#define ZM_WLAN_HEADER_A3_OFFSET            16
#define ZM_WLAN_HEADER_A4_OFFSET            24
#define ZM_WLAN_HEADER_IV_OFFSET            24
#define ZM_SIZE_OF_WLAN_DATA_HEADER         24

/* Port definition */
#define ZM_PORT_DISABLED                    0
#define ZM_PORT_ENABLED                     1

/* Frame Type */
#define ZM_WLAN_MANAGEMENT_FRAME            0x0
#define ZM_WLAN_CONTROL_FRAME               0x4
#define ZM_WLAN_DATA_FRAME                  0x8

/* Frame Subtype */
#define ZM_WLAN_FRAME_TYPE_ASOCREQ          0x00
#define ZM_WLAN_FRAME_TYPE_ASOCRSP          0x10
#define ZM_WLAN_FRAME_TYPE_REASOCREQ        0x20
#define ZM_WLAN_FRAME_TYPE_REASOCRSP        0x30
#define ZM_WLAN_FRAME_TYPE_PROBEREQ         0x40
#define ZM_WLAN_FRAME_TYPE_PROBERSP         0x50
/* 0x60, 0x70 => Reserved */
#define ZM_WLAN_FRAME_TYPE_BEACON           0x80
#define ZM_WLAN_FRAME_TYPE_ATIM             0x90
#define ZM_WLAN_FRAME_TYPE_DISASOC          0xA0
#define ZM_WLAN_FRAME_TYPE_AUTH             0xB0
#define ZM_WLAN_FRAME_TYPE_DEAUTH           0xC0
#define ZM_WLAN_FRAME_TYPE_ACTION			0xD0

/* Frame type and subtype */
#define ZM_WLAN_FRAME_TYPE_NULL             0x48
#define ZM_WLAN_FRAME_TYPE_BAR              0x84
#define ZM_WLAN_FRAME_TYPE_BA               0x94
#define ZM_WLAN_FRAME_TYPE_PSPOLL           0xA4
#define ZM_WLAN_FRAME_TYPE_RTS              0xB4
#define ZM_WLAN_FRAME_TYPE_CTS              0xC4
#define ZM_WLAN_FRAME_TYPE_QOS_NULL         0xC8

/* action frame */
#define ZM_WLAN_SPECTRUM_MANAGEMENT_ACTION_FRAME    0
#define ZM_WLAN_QOS_ACTION_FRAME            1
#define ZM_WLAN_DLS_ACTION_FRAME            2
#define ZM_WLAN_BLOCK_ACK_ACTION_FRAME      3
/* block ack action frame*/
#define ZM_WLAN_ADDBA_REQUEST_FRAME         0
#define ZM_WLAN_ADDBA_RESPONSE_FRAME        1
#define ZM_WLAN_DELBA_FRAME                 2

/* Element ID */
#define ZM_WLAN_EID_SSID                    0
#define ZM_WLAN_EID_SUPPORT_RATE            1
#define ZM_WLAN_EID_FH                      2
#define ZM_WLAN_EID_DS                      3
#define ZM_WLAN_EID_CFS                     4
#define ZM_WLAN_EID_TIM                     5
#define ZM_WLAN_EID_IBSS                    6
#define ZM_WLAN_EID_COUNTRY                 7
/* reserved 8-15 */
#define ZM_WLAN_EID_CHALLENGE               16
/* reserved 17-31 */
#define ZM_WLAN_EID_POWER_CONSTRAINT        32
#define ZM_WLAN_EID_POWER_CAPABILITY        33
#define ZM_WLAN_EID_TPC_REQUEST             34
#define ZM_WLAN_EID_TPC_REPORT              35
#define ZM_WLAN_EID_SUPPORTED_CHANNELS      36
#define ZM_WLAN_EID_CHANNEL_SWITCH_ANNOUNCE 37
#define ZM_WLAN_EID_MEASUREMENT_REQUEST     38
#define ZM_WLAN_EID_MEASUREMENT_REPORT      39
#define ZM_WLAN_EID_QUIET                   40
#define ZM_WLAN_EID_IBSS_DFS                41
#define ZM_WLAN_EID_ERP                     42
#define ZM_WLAN_PREN2_EID_HTCAPABILITY      45
#define ZM_WLAN_EID_RSN_IE                  48
#define ZM_WLAN_EID_EXTENDED_RATE           50
#define ZM_WLAN_EID_HT_CAPABILITY           51
#define ZM_WLAN_EID_EXTENDED_HT_CAPABILITY  52
#define ZM_WLAN_EID_NEW_EXT_CHANNEL_OFFSET  53
#define ZM_WLAN_PREN2_EID_HTINFORMATION     61
#define ZM_WLAN_PREN2_EID_SECONDCHOFFSET    62
#ifdef ZM_ENABLE_CENC
#define ZM_WLAN_EID_CENC_IE                 68
#endif //ZM_ENABLE_CENC
#define ZM_WLAN_EID_VENDOR_PRIVATE          221     /* Vendor private space; must demux OUI */
#define ZM_WLAN_EID_WPA_IE                  221
#define ZM_WLAN_EID_WPS_IE                  221
#define ZM_WLAN_EID_WIFI_IE                 221

/* ERP information element */
#define ZM_WLAN_NON_ERP_PRESENT_BIT         0x1
#define ZM_WLAN_USE_PROTECTION_BIT          0x2
#define ZM_WLAN_BARKER_PREAMBLE_MODE_BIT    0x4

/* Channel frequency, in MHz */
#define ZM_CH_G_1                          2412
#define ZM_CH_G_2                          2417
#define ZM_CH_G_3                          2422
#define ZM_CH_G_4                          2427
#define ZM_CH_G_5                          2432
#define ZM_CH_G_6                          2437
#define ZM_CH_G_7                          2442
#define ZM_CH_G_8                          2447
#define ZM_CH_G_9                          2452
#define ZM_CH_G_10                         2457
#define ZM_CH_G_11                         2462
#define ZM_CH_G_12                         2467
#define ZM_CH_G_13                         2472
#define ZM_CH_G_14                         2484
#define ZM_CH_A_184                        4920
#define ZM_CH_A_188                        4940
#define ZM_CH_A_192                        4960
#define ZM_CH_A_196                        4980
#define ZM_CH_A_8                          5040
#define ZM_CH_A_12                         5060
#define ZM_CH_A_16                         5080
#define ZM_CH_A_36                         5180
#define ZM_CH_A_40                         5200
#define ZM_CH_A_44                         5220
#define ZM_CH_A_48                         5240
#define ZM_CH_A_52                         5260
#define ZM_CH_A_56                         5280
#define ZM_CH_A_60                         5300
#define ZM_CH_A_64                         5320
#define ZM_CH_A_100                        5500
#define ZM_CH_A_104                        5520
#define ZM_CH_A_108                        5540
#define ZM_CH_A_112                        5560
#define ZM_CH_A_116                        5580
#define ZM_CH_A_120                        5600
#define ZM_CH_A_124                        5620
#define ZM_CH_A_128                        5640
#define ZM_CH_A_132                        5660
#define ZM_CH_A_136                        5680
#define ZM_CH_A_140                        5700
#define ZM_CH_A_149                        5745
#define ZM_CH_A_153                        5765
#define ZM_CH_A_157                        5785
#define ZM_CH_A_161                        5805
#define ZM_CH_A_165                        5825


/* AP : STA table => STA Type */
#define ZM_11B_STA                          0x0
#define ZM_11G_STA                          0x2
#define ZM_11N_STA                          0x4

/* AP : timeout */
#define ZM_MS_PER_TICK                      10
#define ZM_TICK_PER_SECOND                  (1000/ZM_MS_PER_TICK)
#define ZM_TICK_PER_MINUTE                  (60*1000/ZM_MS_PER_TICK)
#define ZM_PREAUTH_TIMEOUT_MS               1000 /* 1 sec */
#define ZM_AUTH_TIMEOUT_MS                  1000 /* 1 sec */

/* Error code */
#define ZM_SUCCESS                          0
#define ZM_ERR_TX_PORT_DISABLED             1
#define ZM_ERR_BUFFER_DMA_ADDR              2
#define ZM_ERR_FREE_TXD_EXHAUSTED           3
#define ZM_ERR_TX_BUFFER_UNAVAILABLE        4
#define ZM_ERR_BCMC_PS_BUFFER_UNAVAILABLE   5
#define ZM_ERR_UNI_PS_BUFFER_UNAVAILABLE    6
#define ZM_ERR_EXCEED_PRIORITY_THRESHOLD    7
#define ZM_ERR_VMMQ_FULL                    8
#define ZM_ERR_FLUSH_PS_QUEUE               9
#define ZM_ERR_CMD_INT_MISSED               15 /* Polling cmd int timeout*/
/* Rx */
#define ZM_ERR_RX_FRAME_TYPE                20
#define ZM_ERR_MIN_RX_ENCRYPT_FRAME_LENGTH  21
#define ZM_ERR_MIN_RX_FRAME_LENGTH          22
#define ZM_ERR_MAX_RX_FRAME_LENGTH          23
#define ZM_ERR_RX_DUPLICATE                 24
#define ZM_ERR_RX_SRC_ADDR_IS_OWN_MAC       25
#define ZM_ERR_MIN_RX_PROTOCOL_VERSION      26
#define ZM_ERR_WPA_GK_NOT_INSTALLED         27
#define ZM_ERR_STA_NOT_ASSOCIATED           28
#define ZM_ERR_DATA_BEFORE_CONNECTED        29
#define ZM_ERR_DATA_NOT_ENCRYPTED           30
#define ZM_ERR_DATA_BSSID_NOT_MATCHED       31
#define ZM_ERR_RX_BAR_FRAME                 32
#define ZM_ERR_OUT_OF_ORDER_NULL_DATA       33

/* ZFI */
#define ZM_ERR_INVALID_TX_RATE              40
#define ZM_ERR_WDS_PORT_ID                  41

/* QUEUE */
#define ZM_ERR_QUEUE_FULL                   50
#define ZM_ERR_STA_UAPSD_QUEUE_FULL         51
#define ZM_ERR_AP_UAPSD_QUEUE_FULL          52

/* Maximum Rx frame length */
#if ZM_LARGEPAYLOAD_TEST == 1
#define ZM_WLAN_MAX_RX_SIZE                 16384
#else
#define ZM_WLAN_MAX_RX_SIZE                 8192
#endif

/* PCI DMA test error code */
#define ZM_ERR_INTERRUPT_MISSED             100
#define ZM_ERR_OWN_BIT_NOT_CLEARED          101
#define ZM_ERR_RX_SEQ_NUMBER                102
#define ZM_ERR_RX_LENGTH                    103
#define ZM_ERR_RX_DATA                      104
#define ZM_ERR_RX_DESCRIPTOR_NUM            105
/* Common register test error code */
#define ZM_ERR_REGISTER_ACCESS              110 /* Register R/W test fail*/
#define ZM_ERR_CLEAR_INTERRUPT_FLAG         111
#define ZM_ERR_COMMAND_RESPONSE             112
#define ZM_ERR_INTERRUPT_GENERATE           113
#define ZM_ERR_INTERRUPT_ACK                114
#define ZM_ERR_SCRATCH_ACCESS               115
#define ZM_ERR_INTERRUPT_MASK_ACCESS        116
#define ZM_ERR_SHARE_MEMORY_PCI_ACCESS      117
#define ZM_ERR_SHARE_MEMORY_FW_ACCESS       118
#define ZM_ERR_SHARE_MEMORY_DISABLE         119
#define ZM_ERR_SHARE_MEMORY_TEST_RESPONSE   120

/* Firmware Download error code */
#define ZM_ERR_FIRMWARE_DOWNLOAD_TIMEOUT    150
#define ZM_ERR_FIRMWARE_DOWNLOAD_INT_FLAG   151
#define ZM_ERR_FIRMWARE_READY_TIMEOUT       152
#define ZM_ERR_FIRMWARE_WRONG_TYPE          153

/* Debug */
#define ZM_LV_0     0//Debug level 0, Disable debug message
#define ZM_LV_1     1//Debug level 1, Show minimum information
#define ZM_LV_2     2//Debug level 2, Show medium message
#define ZM_LV_3     3//Debug level 3, Show all

#define ZM_SCANMSG_LEV  ZM_LV_1
#define ZM_TXMSG_LEV    ZM_LV_0//ZM_LV_0
#define ZM_RXMSG_LEV    ZM_LV_0
#define ZM_MMMSG_LEV    ZM_LV_0
#define ZM_DESMSG_LEV   ZM_LV_0//ZM_LV_0
#define ZM_BUFMSG_LEV   ZM_LV_0//ZM_LV_1
#define ZM_INITMSG_LEV  ZM_LV_0

#define zm_msg0_scan(lv, msg) if (ZM_SCANMSG_LEV >= lv) \
        {zm_debug_msg0(msg);}
#define zm_msg1_scan(lv, msg, val) if (ZM_SCANMSG_LEV >= lv) \
        {zm_debug_msg1(msg, val);}
#define zm_msg2_scan(lv, msg, val) if (ZM_SCANMSG_LEV >= lv) \
        {zm_debug_msg2(msg, val);}

#define zm_msg0_tx(lv, msg) if (ZM_TXMSG_LEV >= lv) \
        {zm_debug_msg0(msg);}
#define zm_msg1_tx(lv, msg, val) if (ZM_TXMSG_LEV >= lv) \
        {zm_debug_msg1(msg, val);}
#define zm_msg2_tx(lv, msg, val) if (ZM_TXMSG_LEV >= lv) \
        {zm_debug_msg2(msg, val);}

#define zm_msg0_rx(lv, msg) if (ZM_RXMSG_LEV >= lv) \
        {zm_debug_msg0(msg);}
#define zm_msg1_rx(lv, msg, val) if (ZM_RXMSG_LEV >= lv) \
        {zm_debug_msg1(msg, val);}
#define zm_msg2_rx(lv, msg, val) if (ZM_RXMSG_LEV >= lv) \
        {zm_debug_msg2(msg, val);}

#define zm_msg0_mm(lv, msg) if (ZM_MMMSG_LEV >= lv) \
        {zm_debug_msg0(msg);}
#define zm_msg1_mm(lv, msg, val) if (ZM_MMMSG_LEV >= lv) \
        {zm_debug_msg1(msg, val);}
#define zm_msg2_mm(lv, msg, val) if (ZM_MMMSG_LEV >= lv) \
        {zm_debug_msg2(msg, val);}

#define zm_msg0_des(lv, msg) if (ZM_DESMSG_LEV >= lv) \
        {zm_debug_msg0(msg);}
#define zm_msg1_des(lv, msg, val) if (ZM_DESMSG_LEV >= lv) \
        {zm_debug_msg1(msg, val);}
#define zm_msg2_des(lv, msg, val) if (ZM_DESMSG_LEV >= lv) \
        {zm_debug_msg2(msg, val);}

#define zm_msg0_buf(lv, msg) if (ZM_BUFMSG_LEV >= lv) \
        {zm_debug_msg0(msg);}
#define zm_msg1_buf(lv, msg, val) if (ZM_BUFMSG_LEV >= lv) \
        {zm_debug_msg1(msg, val);}
#define zm_msg2_buf(lv, msg, val) if (ZM_BUFMSG_LEV >= lv) \
        {zm_debug_msg2(msg, val);}

#define zm_msg0_init(lv, msg) if (ZM_INITMSG_LEV >= lv) \
        {zm_debug_msg0(msg);}
#define zm_msg1_init(lv, msg, val) if (ZM_INITMSG_LEV >= lv) \
        {zm_debug_msg1(msg, val);}
#define zm_msg2_init(lv, msg, val) if (ZM_INITMSG_LEV >= lv) \
        {zm_debug_msg2(msg, val);}

#define ZM_MAX_AP_SUPPORT                   2  /* Must <= 8 */
#define ZM_MAX_WDS_SUPPORT                  6  /* Must <= 6 */
#define ZM_MAX_STA_SUPPORT                  16 /* Must <= 64 */

/* STA table state */
#define ZM_STATE_AUTH                       1
#define ZM_STATE_PREAUTH                    2
#define ZM_STATE_ASOC                       3

/* Rate set */
#define ZM_RATE_SET_CCK                     0
#define ZM_RATE_SET_OFDM                    1

/* HT PT */
#define ZM_PREAMBLE_TYPE_MIXED_MODE         0
#define ZM_PREAMBLE_TYPE_GREEN_FIELD        1

/* HT bandwidth */
#define ZM_BANDWIDTH_20MHZ                  0
#define ZM_BANDWIDTH_40MHZ                  1

/* MIC status */
#define ZM_MIC_SUCCESS                      0
#define ZM_MIC_FAILURE                      1

/* ICV status */
#define ZM_ICV_SUCCESS                      0
#define ZM_ICV_FAILURE                      1

/* definition check */
#if (ZM_MAX_AP_SUPPORT > 8)
definition error, ZM_MAX_AP_SUPPORT > 8
#endif
#if (ZM_MAX_AP_SUPPORT > 64)
definition error, ZM_MAX_STA_SUPPORT > 64
#endif

/*  Transmission Rate information */

/* WLAN frame format */
#define ZM_PLCP_HEADER_SIZE          5
#define ZM_ETHERNET_ADDRESS_LENGTH   6
#define ZM_TIMESTAMP_OFFSET          0
#define ZM_BEACON_INTERVAL_OFFSET    8
#define ZM_CAPABILITY_OFFSET        10

/* Reason Code */
/* An unsolicited notification management frame of       */
/* type Disassocation or Deauthentication was generated. */
#ifdef ZM_REASON_CODE
#define ZM_WLAN_REASON_CODE_UNSPECIFIED   1
#define ZM_WLAN_FRAME_DISASOC_DEAUTH_REASON_CODE  24
#endif

struct zsWlanManagementFrameHeader
{
    //u8_t      plcpHdr[ZM_PLCP_HEADER_SIZE];
    u8_t        frameCtrl[2];
    u8_t        duration[2];
    u8_t        da[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        sa[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        bssid[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        seqCtrl[2];
    u8_t        body[1];
};

struct zsWlanProbeRspFrameHeader
{
    //u8_t      plcpHdr[ZM_PLCP_HEADER_SIZE];
    u8_t        frameCtrl[2];
    u8_t        duration[2];
    u8_t        da[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        sa[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        bssid[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        seqCtrl[2];
    u8_t        timeStamp[8];
    u8_t        beaconInterval[2];
    u8_t        capability[2];
    u8_t        ssid[ZM_MAX_SSID_LENGTH + 2];   // EID(1) + Length(1) + SSID(32)
} ;

#define zsWlanBeaconFrameHeader zsWlanProbeRspFrameHeader

struct zsWlanAuthFrameHeader
{
    //u8_t      plcpHdr[ZM_PLCP_HEADER_SIZE];
    u8_t        frameCtrl[2];
    u8_t        duration[2];
    u8_t        address1[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        address2[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        address3[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        seqCtrl[2];
    u16_t       algo;
    u16_t       seq;
    u16_t       status;
    u8_t        challengeText[255]; // the first 2 bytes are information ID, length
};

struct zsWlanAssoFrameHeader
{
    //u8_t      plcpHdr[PLCP_HEADER_SIZE];
    u8_t        frameCtrl[2];
    u8_t        duration[2];
    u8_t        address1[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        address2[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        address3[ZM_ETHERNET_ADDRESS_LENGTH];
    u8_t        seqCtrl[2];
    u8_t        capability[2];
    u16_t       status;
    u16_t       aid;
    //u8_t        supportedRates[10];
};

struct zsFrag
{
    zbuf_t* buf[16];
    u16_t bufType[16];
    u16_t seq[16];
    u8_t flag[16];

};

//================================
// Hardware related definitions
//================================
#define ZM_MAC_REG_BASE                         0x1c3000

#define ZM_MAC_REG_ATIM_WINDOW                  (ZM_MAC_REG_BASE + 0x51C)
#define ZM_MAC_REG_BCN_PERIOD                   (ZM_MAC_REG_BASE + 0x520)
#define ZM_MAC_REG_PRETBTT                      (ZM_MAC_REG_BASE + 0x524)

#define ZM_MAC_REG_MAC_ADDR_L                   (ZM_MAC_REG_BASE + 0x610)
#define ZM_MAC_REG_MAC_ADDR_H                   (ZM_MAC_REG_BASE + 0x614)

#define ZM_MAC_REG_GROUP_HASH_TBL_L             (ZM_MAC_REG_BASE + 0x624)
#define ZM_MAC_REG_GROUP_HASH_TBL_H             (ZM_MAC_REG_BASE + 0x628)

#define ZM_MAC_REG_BASIC_RATE                   (ZM_MAC_REG_BASE + 0x630)
#define ZM_MAC_REG_MANDATORY_RATE               (ZM_MAC_REG_BASE + 0x634)
#define ZM_MAC_REG_RTS_CTS_RATE                 (ZM_MAC_REG_BASE + 0x638)
#define ZM_MAC_REG_BACKOFF_PROTECT              (ZM_MAC_REG_BASE + 0x63c)
#define ZM_MAC_REG_RX_THRESHOLD                 (ZM_MAC_REG_BASE + 0x640)
#define ZM_MAC_REG_RX_PE_DELAY                  (ZM_MAC_REG_BASE + 0x64C)

#define ZM_MAC_REG_DYNAMIC_SIFS_ACK             (ZM_MAC_REG_BASE + 0x658)
#define ZM_MAC_REG_SNIFFER                      (ZM_MAC_REG_BASE + 0x674)
#define ZM_MAC_REG_TX_UNDERRUN		            (ZM_MAC_REG_BASE + 0x688)
#define ZM_MAC_REG_RX_TOTAL			            (ZM_MAC_REG_BASE + 0x6A0)
#define ZM_MAC_REG_RX_CRC32			            (ZM_MAC_REG_BASE + 0x6A4)
#define ZM_MAC_REG_RX_CRC16			            (ZM_MAC_REG_BASE + 0x6A8)
#define ZM_MAC_REG_RX_ERR_UNI		            (ZM_MAC_REG_BASE + 0x6AC)
#define ZM_MAC_REG_RX_OVERRUN		            (ZM_MAC_REG_BASE + 0x6B0)
#define ZM_MAC_REG_RX_ERR_MUL		            (ZM_MAC_REG_BASE + 0x6BC)
#define ZM_MAC_REG_TX_RETRY			            (ZM_MAC_REG_BASE + 0x6CC)
#define ZM_MAC_REG_TX_TOTAL			            (ZM_MAC_REG_BASE + 0x6F4)


#define ZM_MAC_REG_ACK_EXTENSION                (ZM_MAC_REG_BASE + 0x690)
#define ZM_MAC_REG_EIFS_AND_SIFS                (ZM_MAC_REG_BASE + 0x698)

#define ZM_MAC_REG_SLOT_TIME                    (ZM_MAC_REG_BASE + 0x6F0)

#define ZM_MAC_REG_ROLL_CALL_TBL_L              (ZM_MAC_REG_BASE + 0x704)
#define ZM_MAC_REG_ROLL_CALL_TBL_H              (ZM_MAC_REG_BASE + 0x708)

#define ZM_MAC_REG_AC0_CW                       (ZM_MAC_REG_BASE + 0xB00)
#define ZM_MAC_REG_AC1_CW                       (ZM_MAC_REG_BASE + 0xB04)
#define ZM_MAC_REG_AC2_CW                       (ZM_MAC_REG_BASE + 0xB08)
#define ZM_MAC_REG_AC3_CW                       (ZM_MAC_REG_BASE + 0xB0C)
#define ZM_MAC_REG_AC4_CW                       (ZM_MAC_REG_BASE + 0xB10)
#define ZM_MAC_REG_AC1_AC0_AIFS                 (ZM_MAC_REG_BASE + 0xB14)
#define ZM_MAC_REG_AC3_AC2_AIFS                 (ZM_MAC_REG_BASE + 0xB18)

#define ZM_MAC_REG_RETRY_MAX                    (ZM_MAC_REG_BASE + 0xB28)

#define ZM_MAC_REG_TXOP_NOT_ENOUGH_INDICATION   (ZM_MAC_REG_BASE + 0xB30)

#define ZM_MAC_REG_AC1_AC0_TXOP                 (ZM_MAC_REG_BASE + 0xB44)
#define ZM_MAC_REG_AC3_AC2_TXOP                 (ZM_MAC_REG_BASE + 0xB48)

#define ZM_MAC_REG_ACK_TABLE                    (ZM_MAC_REG_BASE + 0xC00)

#define ZM_MAC_REG_BCN_ADDR                     (ZM_MAC_REG_BASE + 0xD84)
#define ZM_MAC_REG_BCN_LENGTH                   (ZM_MAC_REG_BASE + 0xD88)

#define ZM_MAC_REG_BCN_PLCP                     (ZM_MAC_REG_BASE + 0xD90)
#define ZM_MAC_REG_BCN_CTRL                     (ZM_MAC_REG_BASE + 0xD94)

#define ZM_MAC_REG_BCN_HT1                      (ZM_MAC_REG_BASE + 0xDA0)
#define ZM_MAC_REG_BCN_HT2                      (ZM_MAC_REG_BASE + 0xDA4)


#define ZM_RX_STATUS_IS_MIC_FAIL(rxStatus) rxStatus->Tail.Data.ErrorIndication & ZM_BIT_6

//================================
//================================

#ifdef ZM_ENABLE_NATIVE_WIFI
#define ZM_80211_FRAME_HEADER_LEN           24
#define ZM_80211_FRAME_TYPE_OFFSET          30    // ZM_80211_FRAME_HEADER_LEN + SNAP
#define ZM_80211_FRAME_IP_OFFSET            32    // ZM_80211_FRAME_HEADER_LEN + SNAP + TYPE
#else
#define ZM_80211_FRAME_HEADER_LEN           14
#define ZM_80211_FRAME_TYPE_OFFSET          12    // ZM_80211_FRAME_HEADER_LEN + SNAP
#define ZM_80211_FRAME_IP_OFFSET            14    // ZM_80211_FRAME_HEADER_LEN + SNAP + TYPE
#endif

#define ZM_BSS_INFO_VALID_BIT      0x01
#define ZM_BSS_INFO_UPDATED_BIT    0x02





#define ZM_ERROR_INDICATION_RX_TIMEOUT      0x01
#define ZM_ERROR_INDICATION_OVERRUN         0x02
#define ZM_ERROR_INDICATION_DECRYPT_ERROR   0x04
#define ZM_ERROR_INDICATION_CRC32_ERROR     0x08
#define ZM_ERROR_INDICATION_ADDR_NOT_MATCH  0x10
#define ZM_ERROR_INDICATION_CRC16_ERROR     0x20
#define ZM_ERROR_INDICATION_MIC_ERROR       0x40

#define ZM_RXMAC_STATUS_MOD_TYPE_CCK        0x00
#define ZM_RXMAC_STATUS_MOD_TYPE_OFDM       0x01
#define ZM_RXMAC_STATUS_MOD_TYPE_HT_OFDM    0x02
#define ZM_RXMAC_STATUS_MOD_TYPE_DL_OFDM    0x03
#define ZM_RXMAC_STATUS_TOTAL_ERROR         0x80





#define ZM_MAX_LED_NUMBER       2

#define ZM_LED_DISABLE_MODE     0x0
#define ZM_LED_LINK_MODE        0x1
#define ZM_LED_LINK_TR_MODE     0x2
#define ZM_LED_TR_ON_MODE       0x3
#define ZM_LED_TR_OFF_MODE      0x4

#define ZM_LED_CTRL_FLAG_ALPHA      0x1

struct zsLedStruct
{
    u32_t   counter;
    u32_t   counter100ms;
    u16_t   ledLinkState;
    u16_t   ledMode[ZM_MAX_LED_NUMBER];
    u32_t   txTraffic;
    u32_t   rxTraffic;
    u8_t    LEDCtrlType;
    u8_t    LEDCtrlFlag;         // Control Flag for vendors
    u8_t    LEDCtrlFlagFromReg;  // Control Flag for vendors in registry
};


//HAL+ capability bits definition
#define ZM_HP_CAP_11N                   0x1
#define ZM_HP_CAP_11N_ONE_TX_STREAM     0x2
#define ZM_HP_CAP_2G                    0x4
#define ZM_HP_CAP_5G                    0x8

#endif /* #ifndef _WLAN_H */
