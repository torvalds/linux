/*
 * hardware abstraction/register access for HopeRf rf69 radio module
 *
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
#ifndef RF69_H
#define RF69_H

#include "rf69_enum.h"
#include "rf69_registers.h"

#define F_OSC		32000000  /* in Hz */
#define FREQUENCY	433920000 /* in Hz, modifying this value impacts CE certification */
#define FIFO_SIZE	66		/* in byte */
#define FIFO_THRESHOLD	15		/* in byte */

int rf69_set_mode(struct spi_device *spi, enum mode mode);
int rf69_set_data_mode(struct spi_device *spi, enum dataMode dataMode);
int rf69_set_modulation(struct spi_device *spi, enum modulation modulation);
enum modulation rf69_get_modulation(struct spi_device *spi);
int rf69_set_modulation_shaping(struct spi_device *spi, enum mod_shaping mod_shaping);
int rf69_set_bit_rate(struct spi_device *spi, u16 bitRate);
int rf69_set_deviation(struct spi_device *spi, u32 deviation);
int rf69_set_frequency(struct spi_device *spi, u32 frequency);
int rf69_set_amplifier_0(struct spi_device *spi,
			 enum option_on_off option_on_off);
int rf69_set_amplifier_1(struct spi_device *spi,
			 enum option_on_off option_on_off);
int rf69_set_amplifier_2(struct spi_device *spi,
			 enum option_on_off option_on_off);
int rf69_set_output_power_level(struct spi_device *spi, u8 powerLevel);
int rf69_set_pa_ramp(struct spi_device *spi, enum paRamp paRamp);
int rf69_set_antenna_impedance(struct spi_device *spi, enum antennaImpedance antennaImpedance);
int rf69_set_lna_gain(struct spi_device *spi, enum lnaGain lnaGain);
enum lnaGain rf69_get_lna_gain(struct spi_device *spi);
int rf69_set_dc_cut_off_frequency_intern(struct spi_device *spi, u8 reg, enum dccPercent dccPercent);
int rf69_set_dc_cut_off_frequency(struct spi_device *spi, enum dccPercent dccPercent);
int rf69_set_dc_cut_off_frequency_during_afc(struct spi_device *spi, enum dccPercent dccPercent);
int rf69_set_bandwidth(struct spi_device *spi, enum mantisse mantisse, u8 exponent);
int rf69_set_bandwidth_during_afc(struct spi_device *spi, enum mantisse mantisse, u8 exponent);
int rf69_set_ook_threshold_type(struct spi_device *spi, enum thresholdType thresholdType);
int rf69_set_ook_threshold_step(struct spi_device *spi, enum thresholdStep thresholdStep);
int rf69_set_ook_threshold_dec(struct spi_device *spi, enum thresholdDecrement thresholdDecrement);
int rf69_set_dio_mapping(struct spi_device *spi, u8 DIONumber, u8 value);
bool rf69_get_flag(struct spi_device *spi, enum flag flag);
int rf69_reset_flag(struct spi_device *spi, enum flag flag);
int rf69_set_rssi_threshold(struct spi_device *spi, u8 threshold);
int rf69_set_rx_start_timeout(struct spi_device *spi, u8 timeout);
int rf69_set_rssi_timeout(struct spi_device *spi, u8 timeout);
int rf69_set_preamble_length(struct spi_device *spi, u16 preambleLength);
int rf69_set_sync_enable(struct spi_device *spi,
			 enum option_on_off option_on_off);
int rf69_set_fifo_fill_condition(struct spi_device *spi, enum fifoFillCondition fifoFillCondition);
int rf69_set_sync_size(struct spi_device *spi, u8 sync_size);
int rf69_set_sync_tolerance(struct spi_device *spi, u8 syncTolerance);
int rf69_set_sync_values(struct spi_device *spi, u8 syncValues[8]);
int rf69_set_packet_format(struct spi_device *spi, enum packetFormat packetFormat);
int rf69_set_crc_enable(struct spi_device *spi,
			enum option_on_off option_on_off);
int rf69_set_adressFiltering(struct spi_device *spi, enum addressFiltering addressFiltering);
int rf69_set_payload_length(struct spi_device *spi, u8 payloadLength);
u8  rf69_get_payload_length(struct spi_device *spi);
int rf69_set_node_address(struct spi_device *spi, u8 nodeAddress);
int rf69_set_broadcast_address(struct spi_device *spi, u8 broadcastAddress);
int rf69_set_tx_start_condition(struct spi_device *spi, enum txStartCondition txStartCondition);
int rf69_set_fifo_threshold(struct spi_device *spi, u8 threshold);
int rf69_set_dagc(struct spi_device *spi, enum dagc dagc);

int rf69_read_fifo (struct spi_device *spi, u8 *buffer, unsigned int size);
int rf69_write_fifo(struct spi_device *spi, u8 *buffer, unsigned int size);

u8  rf69_read_reg (struct spi_device *spi, u8 addr);
int rf69_write_reg(struct spi_device *spi, u8 addr, u8 value);


#endif
