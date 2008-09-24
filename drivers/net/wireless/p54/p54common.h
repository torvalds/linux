#ifndef PRISM54COMMON_H
#define PRISM54COMMON_H

/*
 * Common code specific definitions for mac80211 Prism54 drivers
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007, Christian Lamparter <chunkeey@web.de>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct bootrec {
	__le32 code;
	__le32 len;
	u32 data[0];
} __attribute__((packed));

struct bootrec_exp_if {
	__le16 role;
	__le16 if_id;
	__le16 variant;
	__le16 btm_compat;
	__le16 top_compat;
} __attribute__((packed));

#define BR_CODE_MIN			0x80000000
#define BR_CODE_COMPONENT_ID		0x80000001
#define BR_CODE_COMPONENT_VERSION	0x80000002
#define BR_CODE_DEPENDENT_IF		0x80000003
#define BR_CODE_EXPOSED_IF		0x80000004
#define BR_CODE_DESCR			0x80000101
#define BR_CODE_MAX			0x8FFFFFFF
#define BR_CODE_END_OF_BRA		0xFF0000FF
#define LEGACY_BR_CODE_END_OF_BRA	0xFFFFFFFF

#define FW_FMAC 0x464d4143
#define FW_LM86 0x4c4d3836
#define FW_LM87 0x4c4d3837
#define FW_LM20 0x4c4d3230

/* PDA defines are Copyright (C) 2005 Nokia Corporation (taken from islsm_pda.h) */

struct pda_entry {
	__le16 len;	/* includes both code and data */
	__le16 code;
	u8 data[0];
} __attribute__ ((packed));

struct eeprom_pda_wrap {
	__le32 magic;
	__le16 pad;
	__le16 len;
	__le32 arm_opcode;
	u8 data[0];
} __attribute__ ((packed));

struct pda_iq_autocal_entry {
        __le16 freq;
        __le16 iq_param[4];
} __attribute__ ((packed));

struct pda_channel_output_limit {
	__le16 freq;
	u8 val_bpsk;
	u8 val_qpsk;
	u8 val_16qam;
	u8 val_64qam;
	u8 rate_set_mask;
	u8 rate_set_size;
} __attribute__ ((packed));

struct pda_pa_curve_data_sample_rev0 {
	u8 rf_power;
	u8 pa_detector;
	u8 pcv;
} __attribute__ ((packed));

struct pda_pa_curve_data_sample_rev1 {
	u8 rf_power;
	u8 pa_detector;
	u8 data_barker;
	u8 data_bpsk;
	u8 data_qpsk;
	u8 data_16qam;
	u8 data_64qam;
	u8 padding;
} __attribute__ ((packed));

struct pda_pa_curve_data {
	u8 cal_method_rev;
	u8 channels;
	u8 points_per_channel;
	u8 padding;
	u8 data[0];
} __attribute__ ((packed));

/*
 * this defines the PDR codes used to build PDAs as defined in document
 * number 553155. The current implementation mirrors version 1.1 of the
 * document and lists only PDRs supported by the ARM platform.
 */

/* common and choice range (0x0000 - 0x0fff) */
#define PDR_END					0x0000
#define PDR_MANUFACTURING_PART_NUMBER		0x0001
#define PDR_PDA_VERSION				0x0002
#define PDR_NIC_SERIAL_NUMBER			0x0003

#define PDR_MAC_ADDRESS				0x0101
#define PDR_REGULATORY_DOMAIN_LIST		0x0103
#define PDR_TEMPERATURE_TYPE			0x0107

#define PDR_PRISM_PCI_IDENTIFIER		0x0402

/* ARM range (0x1000 - 0x1fff) */
#define PDR_COUNTRY_INFORMATION			0x1000
#define PDR_INTERFACE_LIST			0x1001
#define PDR_HARDWARE_PLATFORM_COMPONENT_ID	0x1002
#define PDR_OEM_NAME				0x1003
#define PDR_PRODUCT_NAME			0x1004
#define PDR_UTF8_OEM_NAME			0x1005
#define PDR_UTF8_PRODUCT_NAME			0x1006
#define PDR_COUNTRY_LIST			0x1007
#define PDR_DEFAULT_COUNTRY			0x1008

#define PDR_ANTENNA_GAIN			0x1100

#define PDR_PRISM_INDIGO_PA_CALIBRATION_DATA	0x1901
#define PDR_RSSI_LINEAR_APPROXIMATION		0x1902
#define PDR_PRISM_PA_CAL_OUTPUT_POWER_LIMITS	0x1903
#define PDR_PRISM_PA_CAL_CURVE_DATA		0x1904
#define PDR_RSSI_LINEAR_APPROXIMATION_DUAL_BAND	0x1905
#define PDR_PRISM_ZIF_TX_IQ_CALIBRATION		0x1906
#define PDR_REGULATORY_POWER_LIMITS		0x1907
#define PDR_RSSI_LINEAR_APPROXIMATION_EXTENDED	0x1908
#define PDR_RADIATED_TRANSMISSION_CORRECTION	0x1909
#define PDR_PRISM_TX_IQ_CALIBRATION		0x190a

/* reserved range (0x2000 - 0x7fff) */

/* customer range (0x8000 - 0xffff) */
#define PDR_BASEBAND_REGISTERS			0x8000
#define PDR_PER_CHANNEL_BASEBAND_REGISTERS	0x8001

/* stored in skb->cb */
struct memrecord {
	u32 start_addr;
	u32 end_addr;
};

struct p54_eeprom_lm86 {
	__le16 offset;
	__le16 len;
	u8 data[0];
} __attribute__ ((packed));

struct p54_rx_hdr {
	__le16 magic;
	__le16 len;
	__le16 freq;
	u8 antenna;
	u8 rate;
	u8 rssi;
	u8 quality;
	u16 unknown2;
	__le64 timestamp;
	u8 data[0];
} __attribute__ ((packed));

struct p54_frame_sent_hdr {
	u8 status;
	u8 retries;
	__le16 ack_rssi;
	__le16 seq;
	u16 rate;
} __attribute__ ((packed));

struct p54_tx_control_allocdata {
	u8 rateset[8];
	u8 unalloc0[2];
	u8 key_type;
	u8 key_len;
	u8 key[16];
	u8 hw_queue;
	u8 unalloc1[9];
	u8 tx_antenna;
	u8 output_power;
	u8 cts_rate;
	u8 unalloc2[3];
	u8 align[0];
} __attribute__ ((packed));

struct p54_tx_control_filter {
	__le16 filter_type;
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	u8 antenna;
	u8 debug;
	__le32 magic3;
	u8 rates[8];	// FIXME: what's this for?
	__le32 rx_addr;
	__le16 max_rx;
	__le16 rxhw;
	__le16 magic8;
	__le16 magic9;
} __attribute__ ((packed));

struct p54_tx_control_channel {
	__le16 magic1;
	__le16 magic2;
	u8 padding1[20];
	struct pda_iq_autocal_entry iq_autocal;
	u8 pa_points_per_curve;
	u8 val_barker;
	u8 val_bpsk;
	u8 val_qpsk;
	u8 val_16qam;
	u8 val_64qam;
	struct pda_pa_curve_data_sample_rev1 curve_data[0];
	/* additional padding/data after curve_data */
} __attribute__ ((packed));

struct p54_tx_control_led {
	__le16 mode;
	__le16 led_temporary;
	__le16 led_permanent;
	__le16 duration;
} __attribute__ ((packed));

struct p54_tx_vdcf_queues {
	__le16 aifs;
	__le16 cwmin;
	__le16 cwmax;
	__le16 txop;
} __attribute__ ((packed));

struct p54_tx_control_vdcf {
	u8 padding;
	u8 slottime;
	u8 magic1;
	u8 magic2;
	struct p54_tx_vdcf_queues queue[8];
	u8 pad2[4];
	__le16 frameburst;
} __attribute__ ((packed));

#endif /* PRISM54COMMON_H */
