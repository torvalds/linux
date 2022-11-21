/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * hardware abstraction/register access for HopeRf rf69 radio module
 *
 * Copyright (C) 2016 Wolf-Entwicklungen
 *	Marcus Wolf <linux@wolf-entwicklungen.de>
 */
#ifndef RF69_H
#define RF69_H

#include "rf69_enum.h"
#include "rf69_registers.h"

/* NOTE: Modifying FREQUENCY value impacts CE certification */
#define F_OSC		32000000	/* Hz */
#define FREQUENCY	433920000	/* Hz */
#define FIFO_SIZE	66		/* bytes */
#define FIFO_THRESHOLD	15		/* bytes */

u8 rf69_read_reg(struct spi_device *spi, u8 addr);
int rf69_get_version(struct spi_device *spi);
int rf69_set_mode(struct spi_device *spi, enum mode mode);
int rf69_set_data_mode(struct spi_device *spi, u8 data_mode);
int rf69_set_modulation(struct spi_device *spi, enum modulation modulation);
int rf69_set_modulation_shaping(struct spi_device *spi,
				enum mod_shaping mod_shaping);
int rf69_set_bit_rate(struct spi_device *spi, u16 bit_rate);
int rf69_set_deviation(struct spi_device *spi, u32 deviation);
int rf69_set_frequency(struct spi_device *spi, u32 frequency);
int rf69_enable_amplifier(struct spi_device *spi, u8 amplifier_mask);
int rf69_disable_amplifier(struct spi_device *spi, u8 amplifier_mask);
int rf69_set_output_power_level(struct spi_device *spi, u8 power_level);
int rf69_set_pa_ramp(struct spi_device *spi, enum pa_ramp pa_ramp);
int rf69_set_antenna_impedance(struct spi_device *spi,
			       enum antenna_impedance antenna_impedance);
int rf69_set_lna_gain(struct spi_device *spi, enum lna_gain lna_gain);
int rf69_set_bandwidth(struct spi_device *spi, enum mantisse mantisse,
		       u8 exponent);
int rf69_set_bandwidth_during_afc(struct spi_device *spi,
				  enum mantisse mantisse,
				  u8 exponent);
int rf69_set_ook_threshold_dec(struct spi_device *spi,
			       enum threshold_decrement threshold_decrement);
int rf69_set_dio_mapping(struct spi_device *spi, u8 dio_number, u8 value);
int rf69_set_rssi_threshold(struct spi_device *spi, u8 threshold);
int rf69_set_preamble_length(struct spi_device *spi, u16 preamble_length);
int rf69_enable_sync(struct spi_device *spi);
int rf69_disable_sync(struct spi_device *spi);
int rf69_set_fifo_fill_condition(struct spi_device *spi,
				 enum fifo_fill_condition fifo_fill_condition);
int rf69_set_sync_size(struct spi_device *spi, u8 sync_size);
int rf69_set_sync_values(struct spi_device *spi, u8 sync_values[8]);
int rf69_set_packet_format(struct spi_device *spi,
			   enum packet_format packet_format);
int rf69_enable_crc(struct spi_device *spi);
int rf69_disable_crc(struct spi_device *spi);
int rf69_set_address_filtering(struct spi_device *spi,
			       enum address_filtering address_filtering);
int rf69_set_payload_length(struct spi_device *spi, u8 payload_length);
int rf69_set_node_address(struct spi_device *spi, u8 node_address);
int rf69_set_broadcast_address(struct spi_device *spi, u8 broadcast_address);
int rf69_set_tx_start_condition(struct spi_device *spi,
				enum tx_start_condition tx_start_condition);
int rf69_set_fifo_threshold(struct spi_device *spi, u8 threshold);
int rf69_set_dagc(struct spi_device *spi, enum dagc dagc);

int rf69_read_fifo(struct spi_device *spi, u8 *buffer, unsigned int size);
int rf69_write_fifo(struct spi_device *spi, u8 *buffer, unsigned int size);

#endif
