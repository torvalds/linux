/*
 * eeprom specific definitions for mac80211 Prism54 drivers
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007-2009, Christian Lamparter <chunkeey@web.de>
 *
 * Based on:
 * - the islsm (softmac prism54) driver, which is:
 *   Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * - LMAC API interface header file for STLC4560 (lmac_longbow.h)
 *   Copyright (C) 2007 Conexant Systems, Inc.
 *
 * - islmvc driver
 *   Copyright (C) 2001 Intersil Americas Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EEPROM_H
#define EEPROM_H

/* PDA defines are Copyright (C) 2005 Nokia Corporation (taken from islsm_pda.h) */

struct pda_entry {
	__le16 len;	/* includes both code and data */
	__le16 code;
	u8 data[0];
} __packed;

struct eeprom_pda_wrap {
	__le32 magic;
	__le16 pad;
	__le16 len;
	__le32 arm_opcode;
	u8 data[0];
} __packed;

struct p54_iq_autocal_entry {
	__le16 iq_param[4];
} __packed;

struct pda_iq_autocal_entry {
	__le16 freq;
	struct p54_iq_autocal_entry params;
} __packed;

struct pda_channel_output_limit {
	__le16 freq;
	u8 val_bpsk;
	u8 val_qpsk;
	u8 val_16qam;
	u8 val_64qam;
	u8 rate_set_mask;
	u8 rate_set_size;
} __packed;

struct pda_pa_curve_data_sample_rev0 {
	u8 rf_power;
	u8 pa_detector;
	u8 pcv;
} __packed;

struct pda_pa_curve_data_sample_rev1 {
	u8 rf_power;
	u8 pa_detector;
	u8 data_barker;
	u8 data_bpsk;
	u8 data_qpsk;
	u8 data_16qam;
	u8 data_64qam;
} __packed;

struct pda_pa_curve_data {
	u8 cal_method_rev;
	u8 channels;
	u8 points_per_channel;
	u8 padding;
	u8 data[0];
} __packed;

struct pda_rssi_cal_entry {
	__le16 mul;
	__le16 add;
} __packed;

struct pda_country {
	u8 regdomain;
	u8 alpha2[2];
	u8 flags;
} __packed;

struct pda_antenna_gain {
	struct {
		u8 gain_5GHz;	/* 0.25 dBi units */
		u8 gain_2GHz;	/* 0.25 dBi units */
	} __packed antenna[0];
} __packed;

struct pda_custom_wrapper {
	__le16 entries;
	__le16 entry_size;
	__le16 offset;
	__le16 len;
	u8 data[0];
} __packed;

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
#define PDR_NIC_RAM_SIZE			0x0005
#define PDR_RFMODEM_SUP_RANGE			0x0006
#define PDR_PRISM_MAC_SUP_RANGE			0x0007
#define PDR_NIC_ID				0x0008

#define PDR_MAC_ADDRESS				0x0101
#define PDR_REGULATORY_DOMAIN_LIST		0x0103 /* obsolete */
#define PDR_ALLOWED_CHAN_SET			0x0104
#define PDR_DEFAULT_CHAN			0x0105
#define PDR_TEMPERATURE_TYPE			0x0107

#define PDR_IFR_SETTING				0x0200
#define PDR_RFR_SETTING				0x0201
#define PDR_3861_BASELINE_REG_SETTINGS		0x0202
#define PDR_3861_SHADOW_REG_SETTINGS		0x0203
#define PDR_3861_IFRF_REG_SETTINGS		0x0204

#define PDR_3861_CHAN_CALIB_SET_POINTS		0x0300
#define PDR_3861_CHAN_CALIB_INTEGRATOR		0x0301

#define PDR_3842_PRISM_II_NIC_CONFIG		0x0400
#define PDR_PRISM_USB_ID			0x0401
#define PDR_PRISM_PCI_ID			0x0402
#define PDR_PRISM_PCI_IF_CONFIG			0x0403
#define PDR_PRISM_PCI_PM_CONFIG			0x0404

#define PDR_3861_MF_TEST_CHAN_SET_POINTS	0x0900
#define PDR_3861_MF_TEST_CHAN_INTEGRATORS	0x0901

/* ARM range (0x1000 - 0x1fff) */
#define PDR_COUNTRY_INFORMATION			0x1000 /* obsolete */
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
#define PDR_BASEBAND_REGISTERS				0x8000
#define PDR_PER_CHANNEL_BASEBAND_REGISTERS		0x8001

/* used by our modificated eeprom image */
#define PDR_RSSI_LINEAR_APPROXIMATION_CUSTOM		0xDEAD
#define PDR_PRISM_PA_CAL_OUTPUT_POWER_LIMITS_CUSTOM	0xBEEF
#define PDR_PRISM_PA_CAL_CURVE_DATA_CUSTOM		0xB05D

/* Interface Definitions */
#define PDR_INTERFACE_ROLE_SERVER	0x0000
#define PDR_INTERFACE_ROLE_CLIENT	0x0001

/* PDR definitions for default country & country list */
#define PDR_COUNTRY_CERT_CODE		0x80
#define PDR_COUNTRY_CERT_CODE_REAL	0x00
#define PDR_COUNTRY_CERT_CODE_PSEUDO	0x80
#define PDR_COUNTRY_CERT_BAND		0x40
#define PDR_COUNTRY_CERT_BAND_2GHZ	0x00
#define PDR_COUNTRY_CERT_BAND_5GHZ	0x40
#define PDR_COUNTRY_CERT_IODOOR		0x30
#define PDR_COUNTRY_CERT_IODOOR_BOTH	0x00
#define PDR_COUNTRY_CERT_IODOOR_INDOOR	0x20
#define PDR_COUNTRY_CERT_IODOOR_OUTDOOR	0x30
#define PDR_COUNTRY_CERT_INDEX		0x0f

/* Specific LMAC FW/HW variant definitions */
#define PDR_SYNTH_FRONTEND_MASK		0x0007
#define PDR_SYNTH_FRONTEND_DUETTE3	0x0001
#define PDR_SYNTH_FRONTEND_DUETTE2	0x0002
#define PDR_SYNTH_FRONTEND_FRISBEE	0x0003
#define PDR_SYNTH_FRONTEND_XBOW		0x0004
#define PDR_SYNTH_FRONTEND_LONGBOW	0x0005
#define PDR_SYNTH_IQ_CAL_MASK		0x0018
#define PDR_SYNTH_IQ_CAL_PA_DETECTOR	0x0000
#define PDR_SYNTH_IQ_CAL_DISABLED	0x0008
#define PDR_SYNTH_IQ_CAL_ZIF		0x0010
#define PDR_SYNTH_FAA_SWITCH_MASK	0x0020
#define PDR_SYNTH_FAA_SWITCH_ENABLED	0x0020
#define PDR_SYNTH_24_GHZ_MASK		0x0040
#define PDR_SYNTH_24_GHZ_DISABLED	0x0040
#define PDR_SYNTH_5_GHZ_MASK		0x0080
#define PDR_SYNTH_5_GHZ_DISABLED	0x0080
#define PDR_SYNTH_RX_DIV_MASK		0x0100
#define PDR_SYNTH_RX_DIV_SUPPORTED	0x0100
#define PDR_SYNTH_TX_DIV_MASK		0x0200
#define PDR_SYNTH_TX_DIV_SUPPORTED	0x0200
#define PDR_SYNTH_ASM_MASK		0x0400
#define PDR_SYNTH_ASM_XSWON		0x0400

#endif /* EEPROM_H */
