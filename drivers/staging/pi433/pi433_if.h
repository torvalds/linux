/*
 * include/linux/TODO
 *
 * userspace interface for pi433 radio module
 *
 * Pi433 is a 433MHz radio module for the Raspberry Pi.
 * It is based on the HopeRf Module RFM69CW. Therefore inside of this
 * driver, you'll find an abstraction of the rf69 chip.
 *
 * If needed, this driver could be extended, to also support other
 * devices, basing on HopeRfs rf69.
 *
 * The driver can also be extended, to support other modules of
 * HopeRf with a similar interace - e. g. RFM69HCW, RFM12, RFM95, ...
 * Copyright (C) 2016 Wolf-Entwicklungen
 *	Marcus Wolf <linux@wolf-entwicklungen.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef PI433_H
#define PI433_H

#include <linux/types.h>
#include "rf69_enum.h"

/*---------------------------------------------------------------------------*/

enum option_on_off {
	OPTION_OFF,
	OPTION_ON
};

/* IOCTL structs and commands */

/**
 * struct pi433_tx_config - describes the configuration of the radio module for sending
 * @frequency:
 * @bit_rate:
 * @modulation:
 * @data_mode:
 * @preamble_length:
 * @sync_pattern:
 * @tx_start_condition:
 * @payload_length:
 * @repetitions:
 *
 * ATTENTION:
 * If the contents of 'pi433_tx_config' ever change
 * incompatibly, then the ioctl number (see define below) must change.
 *
 * NOTE: struct layout is the same in 64bit and 32bit userspace.
 */
#define PI433_TX_CFG_IOCTL_NR	0
struct pi433_tx_cfg {
	__u32			frequency;
	__u16			bit_rate;
	__u32			dev_frequency;
	enum modulation		modulation;
	enum mod_shaping	mod_shaping;

	enum paRamp		pa_ramp;

	enum txStartCondition	tx_start_condition;

	__u16			repetitions;

	/* packet format */
	enum option_on_off	enable_preamble;
	enum option_on_off	enable_sync;
	enum option_on_off	enable_length_byte;
	enum option_on_off	enable_address_byte;
	enum option_on_off	enable_crc;

	__u16			preamble_length;
	__u8			sync_length;
	__u8			fixed_message_length;

	__u8			sync_pattern[8];
	__u8			address_byte;
};

/**
 * struct pi433_rx_config - describes the configuration of the radio module for sending
 * @frequency:
 * @bit_rate:
 * @modulation:
 * @data_mode:
 * @preamble_length:
 * @sync_pattern:
 * @tx_start_condition:
 * @payload_length:
 * @repetitions:
 *
 * ATTENTION:
 * If the contents of 'pi433_rx_config' ever change
 * incompatibly, then the ioctl number (see define below) must change
 *
 * NOTE: struct layout is the same in 64bit and 32bit userspace.
 */
#define PI433_RX_CFG_IOCTL_NR	1
struct pi433_rx_cfg {
	__u32			frequency;
	__u16			bit_rate;
	__u32			dev_frequency;

	enum modulation		modulation;

	__u8			rssi_threshold;
	enum thresholdDecrement	threshold_decrement;
	enum antennaImpedance	antenna_impedance;
	enum lnaGain		lna_gain;
	enum mantisse		bw_mantisse;	/* normal: 0x50 */
	__u8			bw_exponent;	/* during AFC: 0x8b */
	enum dagc		dagc;

	/* packet format */
	enum option_on_off	enable_sync;
	enum option_on_off	enable_length_byte;	  /* should be used in combination with sync, only */
	enum addressFiltering	enable_address_filtering; /* operational with sync, only */
	enum option_on_off	enable_crc;		  /* only operational, if sync on and fixed length or length byte is used */

	__u8			sync_length;
	__u8			fixed_message_length;
	__u32			bytes_to_drop;

	__u8			sync_pattern[8];
	__u8			node_address;
	__u8			broadcast_address;
};

#define PI433_IOC_MAGIC			'r'

#define PI433_IOC_RD_TX_CFG	_IOR(PI433_IOC_MAGIC, PI433_TX_CFG_IOCTL_NR, char[sizeof(struct pi433_tx_cfg)])
#define PI433_IOC_WR_TX_CFG	_IOW(PI433_IOC_MAGIC, PI433_TX_CFG_IOCTL_NR, char[sizeof(struct pi433_tx_cfg)])

#define PI433_IOC_RD_RX_CFG	_IOR(PI433_IOC_MAGIC, PI433_RX_CFG_IOCTL_NR, char[sizeof(struct pi433_rx_cfg)])
#define PI433_IOC_WR_RX_CFG	_IOW(PI433_IOC_MAGIC, PI433_RX_CFG_IOCTL_NR, char[sizeof(struct pi433_rx_cfg)])

#endif /* PI433_H */
