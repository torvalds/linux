/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WSM host interface (HI) interface for ST-Ericsson CW1200 mac80211 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * Based on CW1200 UMAC WSM API, which is
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Stewart Mathers <stewart.mathers@stericsson.com>
 */

#ifndef CW1200_WSM_H_INCLUDED
#define CW1200_WSM_H_INCLUDED

#include <linux/spinlock.h>

struct cw1200_common;

/* Bands */
/* Radio band 2.412 -2.484 GHz. */
#define WSM_PHY_BAND_2_4G		(0)

/* Radio band 4.9375-5.8250 GHz. */
#define WSM_PHY_BAND_5G			(1)

/* Transmit rates */
/* 1   Mbps            ERP-DSSS */
#define WSM_TRANSMIT_RATE_1		(0)

/* 2   Mbps            ERP-DSSS */
#define WSM_TRANSMIT_RATE_2		(1)

/* 5.5 Mbps            ERP-CCK */
#define WSM_TRANSMIT_RATE_5		(2)

/* 11  Mbps            ERP-CCK */
#define WSM_TRANSMIT_RATE_11		(3)

/* 22  Mbps            ERP-PBCC (Not supported) */
/* #define WSM_TRANSMIT_RATE_22		(4) */

/* 33  Mbps            ERP-PBCC (Not supported) */
/* #define WSM_TRANSMIT_RATE_33		(5) */

/* 6   Mbps   (3 Mbps) ERP-OFDM, BPSK coding rate 1/2 */
#define WSM_TRANSMIT_RATE_6		(6)

/* 9   Mbps (4.5 Mbps) ERP-OFDM, BPSK coding rate 3/4 */
#define WSM_TRANSMIT_RATE_9		(7)

/* 12  Mbps  (6 Mbps)  ERP-OFDM, QPSK coding rate 1/2 */
#define WSM_TRANSMIT_RATE_12		(8)

/* 18  Mbps  (9 Mbps)  ERP-OFDM, QPSK coding rate 3/4 */
#define WSM_TRANSMIT_RATE_18		(9)

/* 24  Mbps (12 Mbps)  ERP-OFDM, 16QAM coding rate 1/2 */
#define WSM_TRANSMIT_RATE_24		(10)

/* 36  Mbps (18 Mbps)  ERP-OFDM, 16QAM coding rate 3/4 */
#define WSM_TRANSMIT_RATE_36		(11)

/* 48  Mbps (24 Mbps)  ERP-OFDM, 64QAM coding rate 1/2 */
#define WSM_TRANSMIT_RATE_48		(12)

/* 54  Mbps (27 Mbps)  ERP-OFDM, 64QAM coding rate 3/4 */
#define WSM_TRANSMIT_RATE_54		(13)

/* 6.5 Mbps            HT-OFDM, BPSK coding rate 1/2 */
#define WSM_TRANSMIT_RATE_HT_6		(14)

/* 13  Mbps            HT-OFDM, QPSK coding rate 1/2 */
#define WSM_TRANSMIT_RATE_HT_13		(15)

/* 19.5 Mbps           HT-OFDM, QPSK coding rate 3/4 */
#define WSM_TRANSMIT_RATE_HT_19		(16)

/* 26  Mbps            HT-OFDM, 16QAM coding rate 1/2 */
#define WSM_TRANSMIT_RATE_HT_26		(17)

/* 39  Mbps            HT-OFDM, 16QAM coding rate 3/4 */
#define WSM_TRANSMIT_RATE_HT_39		(18)

/* 52  Mbps            HT-OFDM, 64QAM coding rate 2/3 */
#define WSM_TRANSMIT_RATE_HT_52		(19)

/* 58.5 Mbps           HT-OFDM, 64QAM coding rate 3/4 */
#define WSM_TRANSMIT_RATE_HT_58		(20)

/* 65  Mbps            HT-OFDM, 64QAM coding rate 5/6 */
#define WSM_TRANSMIT_RATE_HT_65		(21)

/* Scan types */
/* Foreground scan */
#define WSM_SCAN_TYPE_FOREGROUND	(0)

/* Background scan */
#define WSM_SCAN_TYPE_BACKGROUND	(1)

/* Auto scan */
#define WSM_SCAN_TYPE_AUTO		(2)

/* Scan flags */
/* Forced background scan means if the station cannot */
/* enter the power-save mode, it shall force to perform a */
/* background scan. Only valid when ScanType is */
/* background scan. */
#define WSM_SCAN_FLAG_FORCE_BACKGROUND	(BIT(0))

/* The WLAN device scans one channel at a time so */
/* that disturbance to the data traffic is minimized. */
#define WSM_SCAN_FLAG_SPLIT_METHOD	(BIT(1))

/* Preamble Type. Long if not set. */
#define WSM_SCAN_FLAG_SHORT_PREAMBLE	(BIT(2))

/* 11n Tx Mode. Mixed if not set. */
#define WSM_SCAN_FLAG_11N_GREENFIELD	(BIT(3))

/* Scan constraints */
/* Maximum number of channels to be scanned. */
#define WSM_SCAN_MAX_NUM_OF_CHANNELS	(48)

/* The maximum number of SSIDs that the device can scan for. */
#define WSM_SCAN_MAX_NUM_OF_SSIDS	(2)

/* Power management modes */
/* 802.11 Active mode */
#define WSM_PSM_ACTIVE			(0)

/* 802.11 PS mode */
#define WSM_PSM_PS			BIT(0)

/* Fast Power Save bit */
#define WSM_PSM_FAST_PS_FLAG		BIT(7)

/* Dynamic aka Fast power save */
#define WSM_PSM_FAST_PS			(BIT(0) | BIT(7))

/* Undetermined */
/* Note : Undetermined status is reported when the */
/* NULL data frame used to advertise the PM mode to */
/* the AP at Pre or Post Background Scan is not Acknowledged */
#define WSM_PSM_UNKNOWN			BIT(1)

/* Queue IDs */
/* best effort/legacy */
#define WSM_QUEUE_BEST_EFFORT		(0)

/* background */
#define WSM_QUEUE_BACKGROUND		(1)

/* video */
#define WSM_QUEUE_VIDEO			(2)

/* voice */
#define WSM_QUEUE_VOICE			(3)

/* HT TX parameters */
/* Non-HT */
#define WSM_HT_TX_NON_HT		(0)

/* Mixed format */
#define WSM_HT_TX_MIXED			(1)

/* Greenfield format */
#define WSM_HT_TX_GREENFIELD		(2)

/* STBC allowed */
#define WSM_HT_TX_STBC			(BIT(7))

/* EPTA prioirty flags for BT Coex */
/* default epta priority */
#define WSM_EPTA_PRIORITY_DEFAULT	4
/* use for normal data */
#define WSM_EPTA_PRIORITY_DATA		4
/* use for connect/disconnect/roaming*/
#define WSM_EPTA_PRIORITY_MGT		5
/* use for action frames */
#define WSM_EPTA_PRIORITY_ACTION	5
/* use for AC_VI data */
#define WSM_EPTA_PRIORITY_VIDEO		5
/* use for AC_VO data */
#define WSM_EPTA_PRIORITY_VOICE		6
/* use for EAPOL exchange */
#define WSM_EPTA_PRIORITY_EAPOL		7

/* TX status */
/* Frame was sent aggregated */
/* Only valid for WSM_SUCCESS status. */
#define WSM_TX_STATUS_AGGREGATION	(BIT(0))

/* Host should requeue this frame later. */
/* Valid only when status is WSM_REQUEUE. */
#define WSM_TX_STATUS_REQUEUE		(BIT(1))

/* Normal Ack */
#define WSM_TX_STATUS_NORMAL_ACK	(0<<2)

/* No Ack */
#define WSM_TX_STATUS_NO_ACK		(1<<2)

/* No explicit acknowledgement */
#define WSM_TX_STATUS_NO_EXPLICIT_ACK	(2<<2)

/* Block Ack */
/* Only valid for WSM_SUCCESS status. */
#define WSM_TX_STATUS_BLOCK_ACK		(3<<2)

/* RX status */
/* Unencrypted */
#define WSM_RX_STATUS_UNENCRYPTED	(0<<0)

/* WEP */
#define WSM_RX_STATUS_WEP		(1<<0)

/* TKIP */
#define WSM_RX_STATUS_TKIP		(2<<0)

/* AES */
#define WSM_RX_STATUS_AES		(3<<0)

/* WAPI */
#define WSM_RX_STATUS_WAPI		(4<<0)

/* Macro to fetch encryption subfield. */
#define WSM_RX_STATUS_ENCRYPTION(status) ((status) & 0x07)

/* Frame was part of an aggregation */
#define WSM_RX_STATUS_AGGREGATE		(BIT(3))

/* Frame was first in the aggregation */
#define WSM_RX_STATUS_AGGREGATE_FIRST	(BIT(4))

/* Frame was last in the aggregation */
#define WSM_RX_STATUS_AGGREGATE_LAST	(BIT(5))

/* Indicates a defragmented frame */
#define WSM_RX_STATUS_DEFRAGMENTED	(BIT(6))

/* Indicates a Beacon frame */
#define WSM_RX_STATUS_BEACON		(BIT(7))

/* Indicates STA bit beacon TIM field */
#define WSM_RX_STATUS_TIM		(BIT(8))

/* Indicates Beacon frame's virtual bitmap contains multicast bit */
#define WSM_RX_STATUS_MULTICAST		(BIT(9))

/* Indicates frame contains a matching SSID */
#define WSM_RX_STATUS_MATCHING_SSID	(BIT(10))

/* Indicates frame contains a matching BSSI */
#define WSM_RX_STATUS_MATCHING_BSSI	(BIT(11))

/* Indicates More bit set in Framectl field */
#define WSM_RX_STATUS_MORE_DATA		(BIT(12))

/* Indicates frame received during a measurement process */
#define WSM_RX_STATUS_MEASUREMENT	(BIT(13))

/* Indicates frame received as an HT packet */
#define WSM_RX_STATUS_HT		(BIT(14))

/* Indicates frame received with STBC */
#define WSM_RX_STATUS_STBC		(BIT(15))

/* Indicates Address 1 field matches dot11StationId */
#define WSM_RX_STATUS_ADDRESS1		(BIT(16))

/* Indicates Group address present in the Address 1 field */
#define WSM_RX_STATUS_GROUP		(BIT(17))

/* Indicates Broadcast address present in the Address 1 field */
#define WSM_RX_STATUS_BROADCAST		(BIT(18))

/* Indicates group key used with encrypted frames */
#define WSM_RX_STATUS_GROUP_KEY		(BIT(19))

/* Macro to fetch encryption key index. */
#define WSM_RX_STATUS_KEY_IDX(status)	(((status >> 20)) & 0x0F)

/* Indicates TSF inclusion after 802.11 frame body */
#define WSM_RX_STATUS_TSF_INCLUDED	(BIT(24))

/* Frame Control field starts at Frame offset + 2 */
#define WSM_TX_2BYTES_SHIFT		(BIT(7))

/* Join mode */
/* IBSS */
#define WSM_JOIN_MODE_IBSS		(0)

/* BSS */
#define WSM_JOIN_MODE_BSS		(1)

/* PLCP preamble type */
/* For long preamble */
#define WSM_JOIN_PREAMBLE_LONG		(0)

/* For short preamble (Long for 1Mbps) */
#define WSM_JOIN_PREAMBLE_SHORT		(1)

/* For short preamble (Long for 1 and 2Mbps) */
#define WSM_JOIN_PREAMBLE_SHORT_2	(2)

/* Join flags */
/* Unsynchronized */
#define WSM_JOIN_FLAGS_UNSYNCRONIZED	BIT(0)
/* The BSS owner is a P2P GO */
#define WSM_JOIN_FLAGS_P2P_GO		BIT(1)
/* Force to join BSS with the BSSID and the
 * SSID specified without waiting for beacons. The
 * ProbeForJoin parameter is ignored.
 */
#define WSM_JOIN_FLAGS_FORCE		BIT(2)
/* Give probe request/response higher
 * priority over the BT traffic
 */
#define WSM_JOIN_FLAGS_PRIO		BIT(3)
/* Issue immediate join confirmation and use
 * join complete to notify about completion
 */
#define WSM_JOIN_FLAGS_FORCE_WITH_COMPLETE_IND BIT(5)

/* Key types */
#define WSM_KEY_TYPE_WEP_DEFAULT	(0)
#define WSM_KEY_TYPE_WEP_PAIRWISE	(1)
#define WSM_KEY_TYPE_TKIP_GROUP		(2)
#define WSM_KEY_TYPE_TKIP_PAIRWISE	(3)
#define WSM_KEY_TYPE_AES_GROUP		(4)
#define WSM_KEY_TYPE_AES_PAIRWISE	(5)
#define WSM_KEY_TYPE_WAPI_GROUP		(6)
#define WSM_KEY_TYPE_WAPI_PAIRWISE	(7)

/* Key indexes */
#define WSM_KEY_MAX_INDEX		(10)

/* ACK policy */
#define WSM_ACK_POLICY_NORMAL		(0)
#define WSM_ACK_POLICY_NO_ACK		(1)

/* Start modes */
#define WSM_START_MODE_AP		(0)	/* Mini AP */
#define WSM_START_MODE_P2P_GO		(1)	/* P2P GO */
#define WSM_START_MODE_P2P_DEV		(2)	/* P2P device */

/* SetAssociationMode MIB flags */
#define WSM_ASSOCIATION_MODE_USE_PREAMBLE_TYPE		(BIT(0))
#define WSM_ASSOCIATION_MODE_USE_HT_MODE		(BIT(1))
#define WSM_ASSOCIATION_MODE_USE_BASIC_RATE_SET		(BIT(2))
#define WSM_ASSOCIATION_MODE_USE_MPDU_START_SPACING	(BIT(3))
#define WSM_ASSOCIATION_MODE_SNOOP_ASSOC_FRAMES		(BIT(4))

/* RcpiRssiThreshold MIB flags */
#define WSM_RCPI_RSSI_THRESHOLD_ENABLE	(BIT(0))
#define WSM_RCPI_RSSI_USE_RSSI		(BIT(1))
#define WSM_RCPI_RSSI_DONT_USE_UPPER	(BIT(2))
#define WSM_RCPI_RSSI_DONT_USE_LOWER	(BIT(3))

/* Update-ie constants */
#define WSM_UPDATE_IE_BEACON		(BIT(0))
#define WSM_UPDATE_IE_PROBE_RESP	(BIT(1))
#define WSM_UPDATE_IE_PROBE_REQ		(BIT(2))

/* WSM events */
/* Error */
#define WSM_EVENT_ERROR			(0)

/* BSS lost */
#define WSM_EVENT_BSS_LOST		(1)

/* BSS regained */
#define WSM_EVENT_BSS_REGAINED		(2)

/* Radar detected */
#define WSM_EVENT_RADAR_DETECTED	(3)

/* RCPI or RSSI threshold triggered */
#define WSM_EVENT_RCPI_RSSI		(4)

/* BT inactive */
#define WSM_EVENT_BT_INACTIVE		(5)

/* BT active */
#define WSM_EVENT_BT_ACTIVE		(6)

/* MIB IDs */
/* 4.1  dot11StationId */
#define WSM_MIB_ID_DOT11_STATION_ID		0x0000

/* 4.2  dot11MaxtransmitMsduLifeTime */
#define WSM_MIB_ID_DOT11_MAX_TRANSMIT_LIFTIME	0x0001

/* 4.3  dot11MaxReceiveLifeTime */
#define WSM_MIB_ID_DOT11_MAX_RECEIVE_LIFETIME	0x0002

/* 4.4  dot11SlotTime */
#define WSM_MIB_ID_DOT11_SLOT_TIME		0x0003

/* 4.5  dot11GroupAddressesTable */
#define WSM_MIB_ID_DOT11_GROUP_ADDRESSES_TABLE	0x0004
#define WSM_MAX_GRP_ADDRTABLE_ENTRIES		8

/* 4.6  dot11WepDefaultKeyId */
#define WSM_MIB_ID_DOT11_WEP_DEFAULT_KEY_ID	0x0005

/* 4.7  dot11CurrentTxPowerLevel */
#define WSM_MIB_ID_DOT11_CURRENT_TX_POWER_LEVEL	0x0006

/* 4.8  dot11RTSThreshold */
#define WSM_MIB_ID_DOT11_RTS_THRESHOLD		0x0007

/* 4.9  NonErpProtection */
#define WSM_MIB_ID_NON_ERP_PROTECTION		0x1000

/* 4.10 ArpIpAddressesTable */
#define WSM_MIB_ID_ARP_IP_ADDRESSES_TABLE	0x1001
#define WSM_MAX_ARP_IP_ADDRTABLE_ENTRIES	1

/* 4.11 TemplateFrame */
#define WSM_MIB_ID_TEMPLATE_FRAME		0x1002

/* 4.12 RxFilter */
#define WSM_MIB_ID_RX_FILTER			0x1003

/* 4.13 BeaconFilterTable */
#define WSM_MIB_ID_BEACON_FILTER_TABLE		0x1004

/* 4.14 BeaconFilterEnable */
#define WSM_MIB_ID_BEACON_FILTER_ENABLE		0x1005

/* 4.15 OperationalPowerMode */
#define WSM_MIB_ID_OPERATIONAL_POWER_MODE	0x1006

/* 4.16 BeaconWakeUpPeriod */
#define WSM_MIB_ID_BEACON_WAKEUP_PERIOD		0x1007

/* 4.17 RcpiRssiThreshold */
#define WSM_MIB_ID_RCPI_RSSI_THRESHOLD		0x1009

/* 4.18 StatisticsTable */
#define WSM_MIB_ID_STATISTICS_TABLE		0x100A

/* 4.19 IbssPsConfig */
#define WSM_MIB_ID_IBSS_PS_CONFIG		0x100B

/* 4.20 CountersTable */
#define WSM_MIB_ID_COUNTERS_TABLE		0x100C

/* 4.21 BlockAckPolicy */
#define WSM_MIB_ID_BLOCK_ACK_POLICY		0x100E

/* 4.22 OverrideInternalTxRate */
#define WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE	0x100F

/* 4.23 SetAssociationMode */
#define WSM_MIB_ID_SET_ASSOCIATION_MODE		0x1010

/* 4.24 UpdateEptaConfigData */
#define WSM_MIB_ID_UPDATE_EPTA_CONFIG_DATA	0x1011

/* 4.25 SelectCcaMethod */
#define WSM_MIB_ID_SELECT_CCA_METHOD		0x1012

/* 4.26 SetUpasdInformation */
#define WSM_MIB_ID_SET_UAPSD_INFORMATION	0x1013

/* 4.27 SetAutoCalibrationMode  WBF00004073 */
#define WSM_MIB_ID_SET_AUTO_CALIBRATION_MODE	0x1015

/* 4.28 SetTxRateRetryPolicy */
#define WSM_MIB_ID_SET_TX_RATE_RETRY_POLICY	0x1016

/* 4.29 SetHostMessageTypeFilter */
#define WSM_MIB_ID_SET_HOST_MSG_TYPE_FILTER	0x1017

/* 4.30 P2PFindInfo */
#define WSM_MIB_ID_P2P_FIND_INFO		0x1018

/* 4.31 P2PPsModeInfo */
#define WSM_MIB_ID_P2P_PS_MODE_INFO		0x1019

/* 4.32 SetEtherTypeDataFrameFilter */
#define WSM_MIB_ID_SET_ETHERTYPE_DATAFRAME_FILTER 0x101A

/* 4.33 SetUDPPortDataFrameFilter */
#define WSM_MIB_ID_SET_UDPPORT_DATAFRAME_FILTER	0x101B

/* 4.34 SetMagicDataFrameFilter */
#define WSM_MIB_ID_SET_MAGIC_DATAFRAME_FILTER	0x101C

/* 4.35 P2PDeviceInfo */
#define WSM_MIB_ID_P2P_DEVICE_INFO		0x101D

/* 4.36 SetWCDMABand */
#define WSM_MIB_ID_SET_WCDMA_BAND		0x101E

/* 4.37 GroupTxSequenceCounter */
#define WSM_MIB_ID_GRP_SEQ_COUNTER		0x101F

/* 4.38 ProtectedMgmtPolicy */
#define WSM_MIB_ID_PROTECTED_MGMT_POLICY	0x1020

/* 4.39 SetHtProtection */
#define WSM_MIB_ID_SET_HT_PROTECTION		0x1021

/* 4.40 GPIO Command */
#define WSM_MIB_ID_GPIO_COMMAND			0x1022

/* 4.41 TSF Counter Value */
#define WSM_MIB_ID_TSF_COUNTER			0x1023

/* Test Purposes Only */
#define WSM_MIB_ID_BLOCK_ACK_INFO		0x100D

/* 4.42 UseMultiTxConfMessage */
#define WSM_MIB_USE_MULTI_TX_CONF		0x1024

/* 4.43 Keep-alive period */
#define WSM_MIB_ID_KEEP_ALIVE_PERIOD		0x1025

/* 4.44 Disable BSSID filter */
#define WSM_MIB_ID_DISABLE_BSSID_FILTER		0x1026

/* Frame template types */
#define WSM_FRAME_TYPE_PROBE_REQUEST	(0)
#define WSM_FRAME_TYPE_BEACON		(1)
#define WSM_FRAME_TYPE_NULL		(2)
#define WSM_FRAME_TYPE_QOS_NULL		(3)
#define WSM_FRAME_TYPE_PS_POLL		(4)
#define WSM_FRAME_TYPE_PROBE_RESPONSE	(5)

#define WSM_FRAME_GREENFIELD		(0x80)	/* See 4.11 */

/* Status */
/* The WSM firmware has completed a request */
/* successfully. */
#define WSM_STATUS_SUCCESS              (0)

/* This is a generic failure code if other error codes do */
/* not apply. */
#define WSM_STATUS_FAILURE              (1)

/* A request contains one or more invalid parameters. */
#define WSM_INVALID_PARAMETER           (2)

/* The request cannot perform because the device is in */
/* an inappropriate mode. */
#define WSM_ACCESS_DENIED               (3)

/* The frame received includes a decryption error. */
#define WSM_STATUS_DECRYPTFAILURE       (4)

/* A MIC failure is detected in the received packets. */
#define WSM_STATUS_MICFAILURE           (5)

/* The transmit request failed due to retry limit being */
/* exceeded. */
#define WSM_STATUS_RETRY_EXCEEDED       (6)

/* The transmit request failed due to MSDU life time */
/* being exceeded. */
#define WSM_STATUS_TX_LIFETIME_EXCEEDED (7)

/* The link to the AP is lost. */
#define WSM_STATUS_LINK_LOST            (8)

/* No key was found for the encrypted frame */
#define WSM_STATUS_NO_KEY_FOUND         (9)

/* Jammer was detected when transmitting this frame */
#define WSM_STATUS_JAMMER_DETECTED      (10)

/* The message should be requeued later. */
/* This is applicable only to Transmit */
#define WSM_REQUEUE                     (11)

/* Advanced filtering options */
#define WSM_MAX_FILTER_ELEMENTS		(4)

#define WSM_FILTER_ACTION_IGNORE	(0)
#define WSM_FILTER_ACTION_FILTER_IN	(1)
#define WSM_FILTER_ACTION_FILTER_OUT	(2)

#define WSM_FILTER_PORT_TYPE_DST	(0)
#define WSM_FILTER_PORT_TYPE_SRC	(1)

/* Actual header of WSM messages */
struct wsm_hdr {
	__le16 len;
	__le16 id;
};

#define WSM_TX_SEQ_MAX			(7)
#define WSM_TX_SEQ(seq)			\
		((seq & WSM_TX_SEQ_MAX) << 13)
#define WSM_TX_LINK_ID_MAX		(0x0F)
#define WSM_TX_LINK_ID(link_id)		\
		((link_id & WSM_TX_LINK_ID_MAX) << 6)

#define MAX_BEACON_SKIP_TIME_MS 1000

#define WSM_CMD_LAST_CHANCE_TIMEOUT (HZ * 3 / 2)

/* ******************************************************************** */
/* WSM capability							*/

#define WSM_STARTUP_IND_ID 0x0801

struct wsm_startup_ind {
	u16 input_buffers;
	u16 input_buffer_size;
	u16 status;
	u16 hw_id;
	u16 hw_subid;
	u16 fw_cap;
	u16 fw_type;
	u16 fw_api;
	u16 fw_build;
	u16 fw_ver;
	char fw_label[128];
	u32 config[4];
};

/* ******************************************************************** */
/* WSM commands								*/

/* 3.1 */
#define WSM_CONFIGURATION_REQ_ID 0x0009
#define WSM_CONFIGURATION_RESP_ID 0x0409

struct wsm_tx_power_range {
	int min_power_level;
	int max_power_level;
	u32 stepping;
};

struct wsm_configuration {
	/* [in] */ u32 dot11MaxTransmitMsduLifeTime;
	/* [in] */ u32 dot11MaxReceiveLifeTime;
	/* [in] */ u32 dot11RtsThreshold;
	/* [in, out] */ u8 *dot11StationId;
	/* [in] */ const void *dpdData;
	/* [in] */ size_t dpdData_size;
	/* [out] */ u8 dot11FrequencyBandsSupported;
	/* [out] */ u32 supportedRateMask;
	/* [out] */ struct wsm_tx_power_range txPowerRange[2];
};

int wsm_configuration(struct cw1200_common *priv,
		      struct wsm_configuration *arg);

/* 3.3 */
#define WSM_RESET_REQ_ID 0x000A
#define WSM_RESET_RESP_ID 0x040A
struct wsm_reset {
	/* [in] */ int link_id;
	/* [in] */ bool reset_statistics;
};

int wsm_reset(struct cw1200_common *priv, const struct wsm_reset *arg);

/* 3.5 */
#define WSM_READ_MIB_REQ_ID 0x0005
#define WSM_READ_MIB_RESP_ID 0x0405
int wsm_read_mib(struct cw1200_common *priv, u16 mib_id, void *buf,
		 size_t buf_size);

/* 3.7 */
#define WSM_WRITE_MIB_REQ_ID 0x0006
#define WSM_WRITE_MIB_RESP_ID 0x0406
int wsm_write_mib(struct cw1200_common *priv, u16 mib_id, void *buf,
		  size_t buf_size);

/* 3.9 */
#define WSM_START_SCAN_REQ_ID 0x0007
#define WSM_START_SCAN_RESP_ID 0x0407

struct wsm_ssid {
	u8 ssid[32];
	u32 length;
};

struct wsm_scan_ch {
	u16 number;
	u32 min_chan_time;
	u32 max_chan_time;
	u32 tx_power_level;
};

struct wsm_scan {
	/* WSM_PHY_BAND_... */
	u8 band;

	/* WSM_SCAN_TYPE_... */
	u8 type;

	/* WSM_SCAN_FLAG_... */
	u8 flags;

	/* WSM_TRANSMIT_RATE_... */
	u8 max_tx_rate;

	/* Interval period in TUs that the device shall the re- */
	/* execute the requested scan. Max value supported by the device */
	/* is 256s. */
	u32 auto_scan_interval;

	/* Number of probe requests (per SSID) sent to one (1) */
	/* channel. Zero (0) means that none is send, which */
	/* means that a passive scan is to be done. Value */
	/* greater than zero (0) means that an active scan is to */
	/* be done. */
	u32 num_probes;

	/* Number of channels to be scanned. */
	/* Maximum value is WSM_SCAN_MAX_NUM_OF_CHANNELS. */
	u8 num_channels;

	/* Number of SSID provided in the scan command (this */
	/* is zero (0) in broadcast scan) */
	/* The maximum number of SSIDs is WSM_SCAN_MAX_NUM_OF_SSIDS. */
	u8 num_ssids;

	/* The delay time (in microseconds) period */
	/* before sending a probe-request. */
	u8 probe_delay;

	/* SSIDs to be scanned [numOfSSIDs]; */
	struct wsm_ssid *ssids;

	/* Channels to be scanned [numOfChannels]; */
	struct wsm_scan_ch *ch;
};

int wsm_scan(struct cw1200_common *priv, const struct wsm_scan *arg);

/* 3.11 */
#define WSM_STOP_SCAN_REQ_ID 0x0008
#define WSM_STOP_SCAN_RESP_ID 0x0408
int wsm_stop_scan(struct cw1200_common *priv);

/* 3.13 */
#define WSM_SCAN_COMPLETE_IND_ID 0x0806
struct wsm_scan_complete {
	/* WSM_STATUS_... */
	u32 status;

	/* WSM_PSM_... */
	u8 psm;

	/* Number of channels that the scan operation completed. */
	u8 num_channels;
};

/* 3.14 */
#define WSM_TX_CONFIRM_IND_ID 0x0404
#define WSM_MULTI_TX_CONFIRM_ID 0x041E

struct wsm_tx_confirm {
	/* Packet identifier used in wsm_tx. */
	u32 packet_id;

	/* WSM_STATUS_... */
	u32 status;

	/* WSM_TRANSMIT_RATE_... */
	u8 tx_rate;

	/* The number of times the frame was transmitted */
	/* without receiving an acknowledgement. */
	u8 ack_failures;

	/* WSM_TX_STATUS_... */
	u16 flags;

	/* The total time in microseconds that the frame spent in */
	/* the WLAN device before transmission as completed. */
	u32 media_delay;

	/* The total time in microseconds that the frame spent in */
	/* the WLAN device before transmission was started. */
	u32 tx_queue_delay;
};

/* 3.15 */

/* Note that ideology of wsm_tx struct is different against the rest of
 * WSM API. wsm_hdr is /not/ a caller-adapted struct to be used as an input
 * argument for WSM call, but a prepared bytestream to be sent to firmware.
 * It is filled partly in cw1200_tx, partly in low-level WSM code.
 * Please pay attention once again: ideology is different.
 *
 * Legend:
 * - [in]: cw1200_tx must fill this field.
 * - [wsm]: the field is filled by low-level WSM.
 */
struct wsm_tx {
	/* common WSM header */
	struct wsm_hdr hdr;

	/* Packet identifier that meant to be used in completion. */
	u32 packet_id;  /* Note this is actually a cookie */

	/* WSM_TRANSMIT_RATE_... */
	u8 max_tx_rate;

	/* WSM_QUEUE_... */
	u8 queue_id;

	/* True: another packet is pending on the host for transmission. */
	u8 more;

	/* Bit 0 = 0 - Start expiry time from first Tx attempt (default) */
	/* Bit 0 = 1 - Start expiry time from receipt of Tx Request */
	/* Bits 3:1  - PTA Priority */
	/* Bits 6:4  - Tx Rate Retry Policy */
	/* Bit 7 - Reserved */
	u8 flags;

	/* Should be 0. */
	u32 reserved;

	/* The elapsed time in TUs, after the initial transmission */
	/* of an MSDU, after which further attempts to transmit */
	/* the MSDU shall be terminated. Overrides the global */
	/* dot11MaxTransmitMsduLifeTime setting [optional] */
	/* Device will set the default value if this is 0. */
	__le32 expire_time;

	/* WSM_HT_TX_... */
	__le32 ht_tx_parameters;
} __packed;

/* = sizeof(generic hi hdr) + sizeof(wsm hdr) + sizeof(alignment) */
#define WSM_TX_EXTRA_HEADROOM (28)

/* 3.16 */
#define WSM_RECEIVE_IND_ID 0x0804

struct wsm_rx {
	/* WSM_STATUS_... */
	u32 status;

	/* Specifies the channel of the received packet. */
	u16 channel_number;

	/* WSM_TRANSMIT_RATE_... */
	u8 rx_rate;

	/* This value is expressed in signed Q8.0 format for */
	/* RSSI and unsigned Q7.1 format for RCPI. */
	u8 rcpi_rssi;

	/* WSM_RX_STATUS_... */
	u32 flags;
};

/* = sizeof(generic hi hdr) + sizeof(wsm hdr) */
#define WSM_RX_EXTRA_HEADROOM (16)

/* 3.17 */
struct wsm_event {
	/* WSM_STATUS_... */
	/* [out] */ u32 id;

	/* Indication parameters. */
	/* For error indication, this shall be a 32-bit WSM status. */
	/* For RCPI or RSSI indication, this should be an 8-bit */
	/* RCPI or RSSI value. */
	/* [out] */ u32 data;
};

struct cw1200_wsm_event {
	struct list_head link;
	struct wsm_event evt;
};

/* 3.18 - 3.22 */
/* Measurement. Skipped for now. Irrelevent. */

typedef void (*wsm_event_cb) (struct cw1200_common *priv,
			      struct wsm_event *arg);

/* 3.23 */
#define WSM_JOIN_REQ_ID 0x000B
#define WSM_JOIN_RESP_ID 0x040B

struct wsm_join {
	/* WSM_JOIN_MODE_... */
	u8 mode;

	/* WSM_PHY_BAND_... */
	u8 band;

	/* Specifies the channel number to join. The channel */
	/* number will be mapped to an actual frequency */
	/* according to the band */
	u16 channel_number;

	/* Specifies the BSSID of the BSS or IBSS to be joined */
	/* or the IBSS to be started. */
	u8 bssid[6];

	/* ATIM window of IBSS */
	/* When ATIM window is zero the initiated IBSS does */
	/* not support power saving. */
	u16 atim_window;

	/* WSM_JOIN_PREAMBLE_... */
	u8 preamble_type;

	/* Specifies if a probe request should be send with the */
	/* specified SSID when joining to the network. */
	u8 probe_for_join;

	/* DTIM Period (In multiples of beacon interval) */
	u8 dtim_period;

	/* WSM_JOIN_FLAGS_... */
	u8 flags;

	/* Length of the SSID */
	u32 ssid_len;

	/* Specifies the SSID of the IBSS to join or start */
	u8 ssid[32];

	/* Specifies the time between TBTTs in TUs */
	u32 beacon_interval;

	/* A bit mask that defines the BSS basic rate set. */
	u32 basic_rate_set;
};

struct wsm_join_cnf {
	u32 status;

	/* Minimum transmission power level in units of 0.1dBm */
	u32 min_power_level;

	/* Maximum transmission power level in units of 0.1dBm */
	u32 max_power_level;
};

int wsm_join(struct cw1200_common *priv, struct wsm_join *arg);

/* 3.24 */
struct wsm_join_complete {
	/* WSM_STATUS_... */
	u32 status;
};

/* 3.25 */
#define WSM_SET_PM_REQ_ID 0x0010
#define WSM_SET_PM_RESP_ID 0x0410
struct wsm_set_pm {
	/* WSM_PSM_... */
	u8 mode;

	/* in unit of 500us; 0 to use default */
	u8 fast_psm_idle_period;

	/* in unit of 500us; 0 to use default */
	u8 ap_psm_change_period;

	/* in unit of 500us; 0 to disable auto-pspoll */
	u8 min_auto_pspoll_period;
};

int wsm_set_pm(struct cw1200_common *priv, const struct wsm_set_pm *arg);

/* 3.27 */
struct wsm_set_pm_complete {
	u8 psm;			/* WSM_PSM_... */
};

/* 3.28 */
#define WSM_SET_BSS_PARAMS_REQ_ID 0x0011
#define WSM_SET_BSS_PARAMS_RESP_ID 0x0411
struct wsm_set_bss_params {
	/* This resets the beacon loss counters only */
	u8 reset_beacon_loss;

	/* The number of lost consecutive beacons after which */
	/* the WLAN device should indicate the BSS-Lost event */
	/* to the WLAN host driver. */
	u8 beacon_lost_count;

	/* The AID received during the association process. */
	u16 aid;

	/* The operational rate set mask */
	u32 operational_rate_set;
};

int wsm_set_bss_params(struct cw1200_common *priv,
		       const struct wsm_set_bss_params *arg);

/* 3.30 */
#define WSM_ADD_KEY_REQ_ID         0x000C
#define WSM_ADD_KEY_RESP_ID        0x040C
struct wsm_add_key {
	u8 type;		/* WSM_KEY_TYPE_... */
	u8 index;		/* Key entry index: 0 -- WSM_KEY_MAX_INDEX */
	u16 reserved;
	union {
		struct {
			u8 peer[6];	/* MAC address of the peer station */
			u8 reserved;
			u8 keylen;		/* Key length in bytes */
			u8 keydata[16];		/* Key data */
		} __packed wep_pairwise;
		struct {
			u8 keyid;	/* Unique per key identifier (0..3) */
			u8 keylen;		/* Key length in bytes */
			u16 reserved;
			u8 keydata[16];		/* Key data */
		} __packed wep_group;
		struct {
			u8 peer[6];	/* MAC address of the peer station */
			u16 reserved;
			u8 keydata[16];	/* TKIP key data */
			u8 rx_mic_key[8];		/* Rx MIC key */
			u8 tx_mic_key[8];		/* Tx MIC key */
		} __packed tkip_pairwise;
		struct {
			u8 keydata[16];	/* TKIP key data */
			u8 rx_mic_key[8];		/* Rx MIC key */
			u8 keyid;		/* Key ID */
			u8 reserved[3];
			u8 rx_seqnum[8];	/* Receive Sequence Counter */
		} __packed tkip_group;
		struct {
			u8 peer[6];	/* MAC address of the peer station */
			u16 reserved;
			u8 keydata[16];	/* AES key data */
		} __packed aes_pairwise;
		struct {
			u8 keydata[16];	/* AES key data */
			u8 keyid;		/* Key ID */
			u8 reserved[3];
			u8 rx_seqnum[8];	/* Receive Sequence Counter */
		} __packed aes_group;
		struct {
			u8 peer[6];	/* MAC address of the peer station */
			u8 keyid;		/* Key ID */
			u8 reserved;
			u8 keydata[16];	/* WAPI key data */
			u8 mic_key[16];	/* MIC key data */
		} __packed wapi_pairwise;
		struct {
			u8 keydata[16];	/* WAPI key data */
			u8 mic_key[16];	/* MIC key data */
			u8 keyid;		/* Key ID */
			u8 reserved[3];
		} __packed wapi_group;
	} __packed;
} __packed;

int wsm_add_key(struct cw1200_common *priv, const struct wsm_add_key *arg);

/* 3.32 */
#define WSM_REMOVE_KEY_REQ_ID         0x000D
#define WSM_REMOVE_KEY_RESP_ID        0x040D
struct wsm_remove_key {
	u8 index; /* Key entry index : 0-10 */
};

int wsm_remove_key(struct cw1200_common *priv,
		   const struct wsm_remove_key *arg);

/* 3.34 */
struct wsm_set_tx_queue_params {
	/* WSM_ACK_POLICY_... */
	u8 ackPolicy;

	/* Medium Time of TSPEC (in 32us units) allowed per */
	/* One Second Averaging Period for this queue. */
	u16 allowedMediumTime;

	/* dot11MaxTransmitMsduLifetime to be used for the */
	/* specified queue. */
	u32 maxTransmitLifetime;
};

struct wsm_tx_queue_params {
	/* NOTE: index is a linux queue id. */
	struct wsm_set_tx_queue_params params[4];
};


#define WSM_TX_QUEUE_SET(queue_params, queue, ack_policy, allowed_time,\
		max_life_time)	\
do {							\
	struct wsm_set_tx_queue_params *p = &(queue_params)->params[queue]; \
	p->ackPolicy = (ack_policy);				\
	p->allowedMediumTime = (allowed_time);				\
	p->maxTransmitLifetime = (max_life_time);			\
} while (0)

int wsm_set_tx_queue_params(struct cw1200_common *priv,
			    const struct wsm_set_tx_queue_params *arg, u8 id);

/* 3.36 */
#define WSM_EDCA_PARAMS_REQ_ID 0x0013
#define WSM_EDCA_PARAMS_RESP_ID 0x0413
struct wsm_edca_queue_params {
	/* CWmin (in slots) for the access class. */
	u16 cwmin;

	/* CWmax (in slots) for the access class. */
	u16 cwmax;

	/* AIFS (in slots) for the access class. */
	u16 aifns;

	/* TX OP Limit (in microseconds) for the access class. */
	u16 txop_limit;

	/* dot11MaxReceiveLifetime to be used for the specified */
	/* the access class. Overrides the global */
	/* dot11MaxReceiveLifetime value */
	u32 max_rx_lifetime;
};

struct wsm_edca_params {
	/* NOTE: index is a linux queue id. */
	struct wsm_edca_queue_params params[4];
	bool uapsd_enable[4];
};

#define TXOP_UNIT 32
#define WSM_EDCA_SET(__edca, __queue, __aifs, __cw_min, __cw_max, __txop, __lifetime,\
		     __uapsd) \
	do {							\
		struct wsm_edca_queue_params *p = &(__edca)->params[__queue]; \
		p->cwmin = __cw_min;					\
		p->cwmax = __cw_max;					\
		p->aifns = __aifs;					\
		p->txop_limit = ((__txop) * TXOP_UNIT);			\
		p->max_rx_lifetime = __lifetime;			\
		(__edca)->uapsd_enable[__queue] = (__uapsd);		\
	} while (0)

int wsm_set_edca_params(struct cw1200_common *priv,
			const struct wsm_edca_params *arg);

int wsm_set_uapsd_param(struct cw1200_common *priv,
			const struct wsm_edca_params *arg);

/* 3.38 */
/* Set-System info. Skipped for now. Irrelevent. */

/* 3.40 */
#define WSM_SWITCH_CHANNEL_REQ_ID 0x0016
#define WSM_SWITCH_CHANNEL_RESP_ID 0x0416

struct wsm_switch_channel {
	/* 1 - means the STA shall not transmit any further */
	/* frames until the channel switch has completed */
	u8 mode;

	/* Number of TBTTs until channel switch occurs. */
	/* 0 - indicates switch shall occur at any time */
	/* 1 - occurs immediately before the next TBTT */
	u8 switch_count;

	/* The new channel number to switch to. */
	/* Note this is defined as per section 2.7. */
	u16 channel_number;
};

int wsm_switch_channel(struct cw1200_common *priv,
		       const struct wsm_switch_channel *arg);

#define WSM_START_REQ_ID 0x0017
#define WSM_START_RESP_ID 0x0417

struct wsm_start {
	/* WSM_START_MODE_... */
	/* [in] */ u8 mode;

	/* WSM_PHY_BAND_... */
	/* [in] */ u8 band;

	/* Channel number */
	/* [in] */ u16 channel_number;

	/* Client Traffic window in units of TU */
	/* Valid only when mode == ..._P2P */
	/* [in] */ u32 ct_window;

	/* Interval between two consecutive */
	/* beacon transmissions in TU. */
	/* [in] */ u32 beacon_interval;

	/* DTIM period in terms of beacon intervals */
	/* [in] */ u8 dtim_period;

	/* WSM_JOIN_PREAMBLE_... */
	/* [in] */ u8 preamble;

	/* The delay time (in microseconds) period */
	/* before sending a probe-request. */
	/* [in] */ u8 probe_delay;

	/* Length of the SSID */
	/* [in] */ u8 ssid_len;

	/* SSID of the BSS or P2P_GO to be started now. */
	/* [in] */ u8 ssid[32];

	/* The basic supported rates for the MiniAP. */
	/* [in] */ u32 basic_rate_set;
};

int wsm_start(struct cw1200_common *priv, const struct wsm_start *arg);

#define WSM_BEACON_TRANSMIT_REQ_ID 0x0018
#define WSM_BEACON_TRANSMIT_RESP_ID 0x0418

struct wsm_beacon_transmit {
	/* 1: enable; 0: disable */
	/* [in] */ u8 enable_beaconing;
};

int wsm_beacon_transmit(struct cw1200_common *priv,
			const struct wsm_beacon_transmit *arg);

int wsm_start_find(struct cw1200_common *priv);

int wsm_stop_find(struct cw1200_common *priv);

struct wsm_suspend_resume {
	/* See 3.52 */
	/* Link ID */
	/* [out] */ int link_id;
	/* Stop sending further Tx requests down to device for this link */
	/* [out] */ bool stop;
	/* Transmit multicast Frames */
	/* [out] */ bool multicast;
	/* The AC on which Tx to be suspended /resumed. */
	/* This is applicable only for U-APSD */
	/* WSM_QUEUE_... */
	/* [out] */ int queue;
};

/* 3.54 Update-IE request. */
struct wsm_update_ie {
	/* WSM_UPDATE_IE_... */
	/* [in] */ u16 what;
	/* [in] */ u16 count;
	/* [in] */ u8 *ies;
	/* [in] */ size_t length;
};

int wsm_update_ie(struct cw1200_common *priv,
		  const struct wsm_update_ie *arg);

/* 3.56 */
struct wsm_map_link {
	/* MAC address of the remote device */
	/* [in] */ u8 mac_addr[6];
	/* [in] */ u8 link_id;
};

int wsm_map_link(struct cw1200_common *priv, const struct wsm_map_link *arg);

/* ******************************************************************** */
/* MIB shortcats							*/

static inline int wsm_set_output_power(struct cw1200_common *priv,
				       int power_level)
{
	__le32 val = __cpu_to_le32(power_level);
	return wsm_write_mib(priv, WSM_MIB_ID_DOT11_CURRENT_TX_POWER_LEVEL,
			     &val, sizeof(val));
}

static inline int wsm_set_beacon_wakeup_period(struct cw1200_common *priv,
					       unsigned dtim_interval,
					       unsigned listen_interval)
{
	struct {
		u8 numBeaconPeriods;
		u8 reserved;
		__le16 listenInterval;
	} val = {
		dtim_interval, 0, __cpu_to_le16(listen_interval)
	};

	if (dtim_interval > 0xFF || listen_interval > 0xFFFF)
		return -EINVAL;
	else
		return wsm_write_mib(priv, WSM_MIB_ID_BEACON_WAKEUP_PERIOD,
				     &val, sizeof(val));
}

struct wsm_rcpi_rssi_threshold {
	u8 rssiRcpiMode;	/* WSM_RCPI_RSSI_... */
	u8 lowerThreshold;
	u8 upperThreshold;
	u8 rollingAverageCount;
};

static inline int wsm_set_rcpi_rssi_threshold(struct cw1200_common *priv,
					struct wsm_rcpi_rssi_threshold *arg)
{
	return wsm_write_mib(priv, WSM_MIB_ID_RCPI_RSSI_THRESHOLD, arg,
			     sizeof(*arg));
}

struct wsm_mib_counters_table {
	__le32 plcp_errors;
	__le32 fcs_errors;
	__le32 tx_packets;
	__le32 rx_packets;
	__le32 rx_packet_errors;
	__le32 rx_decryption_failures;
	__le32 rx_mic_failures;
	__le32 rx_no_key_failures;
	__le32 tx_multicast_frames;
	__le32 tx_frames_success;
	__le32 tx_frame_failures;
	__le32 tx_frames_retried;
	__le32 tx_frames_multi_retried;
	__le32 rx_frame_duplicates;
	__le32 rts_success;
	__le32 rts_failures;
	__le32 ack_failures;
	__le32 rx_multicast_frames;
	__le32 rx_frames_success;
	__le32 rx_cmac_icv_errors;
	__le32 rx_cmac_replays;
	__le32 rx_mgmt_ccmp_replays;
} __packed;

static inline int wsm_get_counters_table(struct cw1200_common *priv,
					 struct wsm_mib_counters_table *arg)
{
	return wsm_read_mib(priv, WSM_MIB_ID_COUNTERS_TABLE,
			    arg, sizeof(*arg));
}

static inline int wsm_get_station_id(struct cw1200_common *priv, u8 *mac)
{
	return wsm_read_mib(priv, WSM_MIB_ID_DOT11_STATION_ID, mac, ETH_ALEN);
}

struct wsm_rx_filter {
	bool promiscuous;
	bool bssid;
	bool fcs;
	bool probeResponder;
};

static inline int wsm_set_rx_filter(struct cw1200_common *priv,
				    const struct wsm_rx_filter *arg)
{
	__le32 val = 0;
	if (arg->promiscuous)
		val |= __cpu_to_le32(BIT(0));
	if (arg->bssid)
		val |= __cpu_to_le32(BIT(1));
	if (arg->fcs)
		val |= __cpu_to_le32(BIT(2));
	if (arg->probeResponder)
		val |= __cpu_to_le32(BIT(3));
	return wsm_write_mib(priv, WSM_MIB_ID_RX_FILTER, &val, sizeof(val));
}

int wsm_set_probe_responder(struct cw1200_common *priv, bool enable);

#define WSM_BEACON_FILTER_IE_HAS_CHANGED	BIT(0)
#define WSM_BEACON_FILTER_IE_NO_LONGER_PRESENT	BIT(1)
#define WSM_BEACON_FILTER_IE_HAS_APPEARED	BIT(2)

struct wsm_beacon_filter_table_entry {
	u8	ie_id;
	u8	flags;
	u8	oui[3];
	u8	match_data[3];
} __packed;

struct wsm_mib_beacon_filter_table {
	__le32 num;
	struct wsm_beacon_filter_table_entry entry[10];
} __packed;

static inline int wsm_set_beacon_filter_table(struct cw1200_common *priv,
					      struct wsm_mib_beacon_filter_table *ft)
{
	size_t size = __le32_to_cpu(ft->num) *
		     sizeof(struct wsm_beacon_filter_table_entry) +
		     sizeof(__le32);

	return wsm_write_mib(priv, WSM_MIB_ID_BEACON_FILTER_TABLE, ft, size);
}

#define WSM_BEACON_FILTER_ENABLE	BIT(0) /* Enable/disable beacon filtering */
#define WSM_BEACON_FILTER_AUTO_ERP	BIT(1) /* If 1 FW will handle ERP IE changes internally */

struct wsm_beacon_filter_control {
	int enabled;
	int bcn_count;
};

static inline int wsm_beacon_filter_control(struct cw1200_common *priv,
					struct wsm_beacon_filter_control *arg)
{
	struct {
		__le32 enabled;
		__le32 bcn_count;
	} val;
	val.enabled = __cpu_to_le32(arg->enabled);
	val.bcn_count = __cpu_to_le32(arg->bcn_count);
	return wsm_write_mib(priv, WSM_MIB_ID_BEACON_FILTER_ENABLE, &val,
			     sizeof(val));
}

enum wsm_power_mode {
	wsm_power_mode_active = 0,
	wsm_power_mode_doze = 1,
	wsm_power_mode_quiescent = 2,
};

struct wsm_operational_mode {
	enum wsm_power_mode power_mode;
	int disable_more_flag_usage;
	int perform_ant_diversity;
};

static inline int wsm_set_operational_mode(struct cw1200_common *priv,
					const struct wsm_operational_mode *arg)
{
	u8 val = arg->power_mode;
	if (arg->disable_more_flag_usage)
		val |= BIT(4);
	if (arg->perform_ant_diversity)
		val |= BIT(5);
	return wsm_write_mib(priv, WSM_MIB_ID_OPERATIONAL_POWER_MODE, &val,
			     sizeof(val));
}

struct wsm_template_frame {
	u8 frame_type;
	u8 rate;
	struct sk_buff *skb;
};

static inline int wsm_set_template_frame(struct cw1200_common *priv,
					 struct wsm_template_frame *arg)
{
	int ret;
	u8 *p = skb_push(arg->skb, 4);
	p[0] = arg->frame_type;
	p[1] = arg->rate;
	((__le16 *)p)[1] = __cpu_to_le16(arg->skb->len - 4);
	ret = wsm_write_mib(priv, WSM_MIB_ID_TEMPLATE_FRAME, p, arg->skb->len);
	skb_pull(arg->skb, 4);
	return ret;
}


struct wsm_protected_mgmt_policy {
	bool protectedMgmtEnable;
	bool unprotectedMgmtFramesAllowed;
	bool encryptionForAuthFrame;
};

static inline int wsm_set_protected_mgmt_policy(struct cw1200_common *priv,
		struct wsm_protected_mgmt_policy *arg)
{
	__le32 val = 0;
	int ret;
	if (arg->protectedMgmtEnable)
		val |= __cpu_to_le32(BIT(0));
	if (arg->unprotectedMgmtFramesAllowed)
		val |= __cpu_to_le32(BIT(1));
	if (arg->encryptionForAuthFrame)
		val |= __cpu_to_le32(BIT(2));
	ret = wsm_write_mib(priv, WSM_MIB_ID_PROTECTED_MGMT_POLICY,
			&val, sizeof(val));
	return ret;
}

struct wsm_mib_block_ack_policy {
	u8 tx_tid;
	u8 reserved1;
	u8 rx_tid;
	u8 reserved2;
} __packed;

static inline int wsm_set_block_ack_policy(struct cw1200_common *priv,
					   u8 tx_tid_policy,
					   u8 rx_tid_policy)
{
	struct wsm_mib_block_ack_policy val = {
		.tx_tid = tx_tid_policy,
		.rx_tid = rx_tid_policy,
	};
	return wsm_write_mib(priv, WSM_MIB_ID_BLOCK_ACK_POLICY, &val,
			     sizeof(val));
}

struct wsm_mib_association_mode {
	u8 flags;		/* WSM_ASSOCIATION_MODE_... */
	u8 preamble;	/* WSM_JOIN_PREAMBLE_... */
	u8 greenfield;	/* 1 for greenfield */
	u8 mpdu_start_spacing;
	__le32 basic_rate_set;
} __packed;

static inline int wsm_set_association_mode(struct cw1200_common *priv,
					   struct wsm_mib_association_mode *arg)
{
	return wsm_write_mib(priv, WSM_MIB_ID_SET_ASSOCIATION_MODE, arg,
			     sizeof(*arg));
}

#define WSM_TX_RATE_POLICY_FLAG_TERMINATE_WHEN_FINISHED BIT(2)
#define WSM_TX_RATE_POLICY_FLAG_COUNT_INITIAL_TRANSMIT BIT(3)
struct wsm_tx_rate_retry_policy {
	u8 index;
	u8 short_retries;
	u8 long_retries;
	/* BIT(2) - Terminate retries when Tx rate retry policy
	 *          finishes.
	 * BIT(3) - Count initial frame transmission as part of
	 *          rate retry counting but not as a retry
	 *          attempt
	 */
	u8 flags;
	u8 rate_recoveries;
	u8 reserved[3];
	__le32 rate_count_indices[3];
} __packed;

struct wsm_set_tx_rate_retry_policy {
	u8 num;
	u8 reserved[3];
	struct wsm_tx_rate_retry_policy tbl[8];
} __packed;

static inline int wsm_set_tx_rate_retry_policy(struct cw1200_common *priv,
				struct wsm_set_tx_rate_retry_policy *arg)
{
	size_t size = 4 + arg->num * sizeof(struct wsm_tx_rate_retry_policy);
	return wsm_write_mib(priv, WSM_MIB_ID_SET_TX_RATE_RETRY_POLICY, arg,
			     size);
}

/* 4.32 SetEtherTypeDataFrameFilter */
struct wsm_ether_type_filter_hdr {
	u8 num;		/* Up to WSM_MAX_FILTER_ELEMENTS */
	u8 reserved[3];
} __packed;

struct wsm_ether_type_filter {
	u8 action;	/* WSM_FILTER_ACTION_XXX */
	u8 reserved;
	__le16 type;	/* Type of ethernet frame */
} __packed;

static inline int wsm_set_ether_type_filter(struct cw1200_common *priv,
				struct wsm_ether_type_filter_hdr *arg)
{
	size_t size = sizeof(struct wsm_ether_type_filter_hdr) +
		arg->num * sizeof(struct wsm_ether_type_filter);
	return wsm_write_mib(priv, WSM_MIB_ID_SET_ETHERTYPE_DATAFRAME_FILTER,
		arg, size);
}

/* 4.33 SetUDPPortDataFrameFilter */
struct wsm_udp_port_filter_hdr {
	u8 num;		/* Up to WSM_MAX_FILTER_ELEMENTS */
	u8 reserved[3];
} __packed;

struct wsm_udp_port_filter {
	u8 action;	/* WSM_FILTER_ACTION_XXX */
	u8 type;		/* WSM_FILTER_PORT_TYPE_XXX */
	__le16 port;		/* Port number */
} __packed;

static inline int wsm_set_udp_port_filter(struct cw1200_common *priv,
				struct wsm_udp_port_filter_hdr *arg)
{
	size_t size = sizeof(struct wsm_udp_port_filter_hdr) +
		arg->num * sizeof(struct wsm_udp_port_filter);
	return wsm_write_mib(priv, WSM_MIB_ID_SET_UDPPORT_DATAFRAME_FILTER,
		arg, size);
}

/* Undocumented MIBs: */
/* 4.35 P2PDeviceInfo */
#define D11_MAX_SSID_LEN		(32)

struct wsm_p2p_device_type {
	__le16 category_id;
	u8 oui[4];
	__le16 subcategory_id;
} __packed;

struct wsm_p2p_device_info {
	struct wsm_p2p_device_type primaryDevice;
	u8 reserved1[3];
	u8 devname_size;
	u8 local_devname[D11_MAX_SSID_LEN];
	u8 reserved2[3];
	u8 num_secdev_supported;
	struct wsm_p2p_device_type secdevs[];
} __packed;

/* 4.36 SetWCDMABand - WO */
struct wsm_cdma_band {
	u8 wcdma_band;
	u8 reserved[3];
} __packed;

/* 4.37 GroupTxSequenceCounter - RO */
struct wsm_group_tx_seq {
	__le32 bits_47_16;
	__le16 bits_15_00;
	__le16 reserved;
} __packed;

/* 4.39 SetHtProtection - WO */
#define WSM_DUAL_CTS_PROT_ENB		(1 << 0)
#define WSM_NON_GREENFIELD_STA_PRESENT  (1 << 1)
#define WSM_HT_PROT_MODE__NO_PROT	(0 << 2)
#define WSM_HT_PROT_MODE__NON_MEMBER	(1 << 2)
#define WSM_HT_PROT_MODE__20_MHZ	(2 << 2)
#define WSM_HT_PROT_MODE__NON_HT_MIXED	(3 << 2)
#define WSM_LSIG_TXOP_PROT_FULL		(1 << 4)
#define WSM_LARGE_L_LENGTH_PROT		(1 << 5)

struct wsm_ht_protection {
	__le32 flags;
} __packed;

/* 4.40 GPIO Command - R/W */
#define WSM_GPIO_COMMAND_SETUP	0
#define WSM_GPIO_COMMAND_READ	1
#define WSM_GPIO_COMMAND_WRITE	2
#define WSM_GPIO_COMMAND_RESET	3
#define WSM_GPIO_ALL_PINS	0xFF

struct wsm_gpio_command {
	u8 command;
	u8 pin;
	__le16 config;
} __packed;

/* 4.41 TSFCounter - RO */
struct wsm_tsf_counter {
	__le64 tsf_counter;
} __packed;

/* 4.43 Keep alive period */
struct wsm_keep_alive_period {
	__le16 period;
	u8 reserved[2];
} __packed;

static inline int wsm_keep_alive_period(struct cw1200_common *priv,
					int period)
{
	struct wsm_keep_alive_period arg = {
		.period = __cpu_to_le16(period),
	};
	return wsm_write_mib(priv, WSM_MIB_ID_KEEP_ALIVE_PERIOD,
			&arg, sizeof(arg));
};

/* BSSID filtering */
struct wsm_set_bssid_filtering {
	u8 filter;
	u8 reserved[3];
} __packed;

static inline int wsm_set_bssid_filtering(struct cw1200_common *priv,
					  bool enabled)
{
	struct wsm_set_bssid_filtering arg = {
		.filter = !enabled,
	};
	return wsm_write_mib(priv, WSM_MIB_ID_DISABLE_BSSID_FILTER,
			&arg, sizeof(arg));
}

/* Multicast filtering - 4.5 */
struct wsm_mib_multicast_filter {
	__le32 enable;
	__le32 num_addrs;
	u8 macaddrs[WSM_MAX_GRP_ADDRTABLE_ENTRIES][ETH_ALEN];
} __packed;

static inline int wsm_set_multicast_filter(struct cw1200_common *priv,
					   struct wsm_mib_multicast_filter *fp)
{
	return wsm_write_mib(priv, WSM_MIB_ID_DOT11_GROUP_ADDRESSES_TABLE,
			     fp, sizeof(*fp));
}

/* ARP IPv4 filtering - 4.10 */
struct wsm_mib_arp_ipv4_filter {
	__le32 enable;
	__be32 ipv4addrs[WSM_MAX_ARP_IP_ADDRTABLE_ENTRIES];
} __packed;

static inline int wsm_set_arp_ipv4_filter(struct cw1200_common *priv,
					  struct wsm_mib_arp_ipv4_filter *fp)
{
	return wsm_write_mib(priv, WSM_MIB_ID_ARP_IP_ADDRESSES_TABLE,
			    fp, sizeof(*fp));
}

/* P2P Power Save Mode Info - 4.31 */
struct wsm_p2p_ps_modeinfo {
	u8	opp_ps_ct_window;
	u8	count;
	u8	reserved;
	u8	dtim_count;
	__le32	duration;
	__le32	interval;
	__le32	start_time;
} __packed;

static inline int wsm_set_p2p_ps_modeinfo(struct cw1200_common *priv,
					  struct wsm_p2p_ps_modeinfo *mi)
{
	return wsm_write_mib(priv, WSM_MIB_ID_P2P_PS_MODE_INFO,
			     mi, sizeof(*mi));
}

static inline int wsm_get_p2p_ps_modeinfo(struct cw1200_common *priv,
					  struct wsm_p2p_ps_modeinfo *mi)
{
	return wsm_read_mib(priv, WSM_MIB_ID_P2P_PS_MODE_INFO,
			    mi, sizeof(*mi));
}

/* UseMultiTxConfMessage */

static inline int wsm_use_multi_tx_conf(struct cw1200_common *priv,
					bool enabled)
{
	__le32 arg = enabled ? __cpu_to_le32(1) : 0;

	return wsm_write_mib(priv, WSM_MIB_USE_MULTI_TX_CONF,
			&arg, sizeof(arg));
}


/* 4.26 SetUpasdInformation */
struct wsm_uapsd_info {
	__le16 uapsd_flags;
	__le16 min_auto_trigger_interval;
	__le16 max_auto_trigger_interval;
	__le16 auto_trigger_step;
};

static inline int wsm_set_uapsd_info(struct cw1200_common *priv,
				     struct wsm_uapsd_info *arg)
{
	return wsm_write_mib(priv, WSM_MIB_ID_SET_UAPSD_INFORMATION,
				arg, sizeof(*arg));
}

/* 4.22 OverrideInternalTxRate */
struct wsm_override_internal_txrate {
	u8 internalTxRate;
	u8 nonErpInternalTxRate;
	u8 reserved[2];
} __packed;

static inline int wsm_set_override_internal_txrate(struct cw1200_common *priv,
				     struct wsm_override_internal_txrate *arg)
{
	return wsm_write_mib(priv, WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE,
				arg, sizeof(*arg));
}

/* ******************************************************************** */
/* WSM TX port control							*/

void wsm_lock_tx(struct cw1200_common *priv);
void wsm_lock_tx_async(struct cw1200_common *priv);
bool wsm_flush_tx(struct cw1200_common *priv);
void wsm_unlock_tx(struct cw1200_common *priv);

/* ******************************************************************** */
/* WSM / BH API								*/

int wsm_handle_exception(struct cw1200_common *priv, u8 *data, size_t len);
int wsm_handle_rx(struct cw1200_common *priv, u16 id, struct wsm_hdr *wsm,
		  struct sk_buff **skb_p);

/* ******************************************************************** */
/* wsm_buf API								*/

struct wsm_buf {
	u8 *begin;
	u8 *data;
	u8 *end;
};

void wsm_buf_init(struct wsm_buf *buf);
void wsm_buf_deinit(struct wsm_buf *buf);

/* ******************************************************************** */
/* wsm_cmd API								*/

struct wsm_cmd {
	spinlock_t lock; /* Protect structure from multiple access */
	int done;
	u8 *ptr;
	size_t len;
	void *arg;
	int ret;
	u16 cmd;
};

/* ******************************************************************** */
/* WSM TX buffer access							*/

int wsm_get_tx(struct cw1200_common *priv, u8 **data,
	       size_t *tx_len, int *burst);
void wsm_txed(struct cw1200_common *priv, u8 *data);

/* ******************************************************************** */
/* Queue mapping: WSM <---> linux					*/
/* Linux: VO VI BE BK							*/
/* WSM:   BE BK VI VO							*/

static inline u8 wsm_queue_id_to_linux(u8 queue_id)
{
	static const u8 queue_mapping[] = {
		2, 3, 1, 0
	};
	return queue_mapping[queue_id];
}

static inline u8 wsm_queue_id_to_wsm(u8 queue_id)
{
	static const u8 queue_mapping[] = {
		3, 2, 0, 1
	};
	return queue_mapping[queue_id];
}

#endif /* CW1200_HWIO_H_INCLUDED */
