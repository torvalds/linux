// SPDX-License-Identifier: GPL-2.0+
/*
 * abstraction of the spi interface of HopeRf rf69 radio module
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

/* enable prosa debug info */
#undef DEBUG
/* enable print of values on reg access */
#undef DEBUG_VALUES
/* enable print of values on fifo access */
#undef DEBUG_FIFO_ACCESS

#include <linux/types.h>
#include <linux/spi/spi.h>

#include "rf69.h"
#include "rf69_registers.h"

#define F_OSC	  32000000 /* in Hz */
#define FIFO_SIZE 66	   /* in byte */

/*-------------------------------------------------------------------------*/

static u8 rf69_read_reg(struct spi_device *spi, u8 addr)
{
	int retval;

	retval = spi_w8r8(spi, addr);

#ifdef DEBUG_VALUES
	if (retval < 0)
		/*
		 * should never happen, since we already checked,
		 * that module is connected. Therefore no error
		 * handling, just an optional error message...
		 */
		dev_dbg(&spi->dev, "read 0x%x FAILED\n", addr);
	else
		dev_dbg(&spi->dev, "read 0x%x from reg 0x%x\n", retval, addr);
#endif

	return retval;
}

static int rf69_write_reg(struct spi_device *spi, u8 addr, u8 value)
{
	int retval;
	char buffer[2];

	buffer[0] = addr | WRITE_BIT;
	buffer[1] = value;

	retval = spi_write(spi, &buffer, 2);

#ifdef DEBUG_VALUES
	if (retval < 0)
		/*
		 * should never happen, since we already checked,
		 * that module is connected. Therefore no error
		 * handling, just an optional error message...
		 */
		dev_dbg(&spi->dev, "write 0x%x to 0x%x FAILED\n", value, addr);
	else
		dev_dbg(&spi->dev, "wrote 0x%x to reg 0x%x\n", value, addr);
#endif

	return retval;
}

/*-------------------------------------------------------------------------*/

static int rf69_set_bit(struct spi_device *spi, u8 reg, u8 mask)
{
	u8 tmp;

	tmp = rf69_read_reg(spi, reg);
	tmp = tmp | mask;
	return rf69_write_reg(spi, reg, tmp);
}

static int rf69_clear_bit(struct spi_device *spi, u8 reg, u8 mask)
{
	u8 tmp;

	tmp = rf69_read_reg(spi, reg);
	tmp = tmp & ~mask;
	return rf69_write_reg(spi, reg, tmp);
}

static inline int rf69_read_mod_write(struct spi_device *spi, u8 reg,
				      u8 mask, u8 value)
{
	u8 tmp;

	tmp = rf69_read_reg(spi, reg);
	tmp = (tmp & ~mask) | value;
	return rf69_write_reg(spi, reg, tmp);
}

/*-------------------------------------------------------------------------*/

int rf69_set_mode(struct spi_device *spi, enum mode mode)
{
	static const u8 mode_map[] = {
		[transmit] = OPMODE_MODE_TRANSMIT,
		[receive] = OPMODE_MODE_RECEIVE,
		[synthesizer] = OPMODE_MODE_SYNTHESIZER,
		[standby] = OPMODE_MODE_STANDBY,
		[mode_sleep] = OPMODE_MODE_SLEEP,
	};

	if (unlikely(mode >= ARRAY_SIZE(mode_map))) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	return rf69_read_mod_write(spi, REG_OPMODE, MASK_OPMODE_MODE,
				   mode_map[mode]);

	/*
	 * we are using packet mode, so this check is not really needed
	 * but waiting for mode ready is necessary when going from sleep
	 * because the FIFO may not be immediately available from previous mode
	 * while (_mode == RF69_MODE_SLEEP && (READ_REG(REG_IRQFLAGS1) &
		  RF_IRQFLAGS1_MODEREADY) == 0x00); // Wait for ModeReady
	 */
}

int rf69_set_data_mode(struct spi_device *spi, u8 data_mode)
{
	return rf69_read_mod_write(spi, REG_DATAMODUL, MASK_DATAMODUL_MODE,
				   data_mode);
}

int rf69_set_modulation(struct spi_device *spi, enum modulation modulation)
{
	static const u8 modulation_map[] = {
		[OOK] = DATAMODUL_MODULATION_TYPE_OOK,
		[FSK] = DATAMODUL_MODULATION_TYPE_FSK,
	};

	if (unlikely(modulation >= ARRAY_SIZE(modulation_map))) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	return rf69_read_mod_write(spi, REG_DATAMODUL,
				   MASK_DATAMODUL_MODULATION_TYPE,
				   modulation_map[modulation]);
}

static enum modulation rf69_get_modulation(struct spi_device *spi)
{
	u8 modulation_reg;

	modulation_reg = rf69_read_reg(spi, REG_DATAMODUL);

	switch (modulation_reg & MASK_DATAMODUL_MODULATION_TYPE) {
	case DATAMODUL_MODULATION_TYPE_OOK:
		return OOK;
	case DATAMODUL_MODULATION_TYPE_FSK:
		return FSK;
	default:
		return UNDEF;
	}
}

int rf69_set_modulation_shaping(struct spi_device *spi,
				enum mod_shaping mod_shaping)
{
	switch (rf69_get_modulation(spi)) {
	case FSK:
		switch (mod_shaping) {
		case SHAPING_OFF:
			return rf69_read_mod_write(spi, REG_DATAMODUL,
						   MASK_DATAMODUL_MODULATION_SHAPE,
						   DATAMODUL_MODULATION_SHAPE_NONE);
		case SHAPING_1_0:
			return rf69_read_mod_write(spi, REG_DATAMODUL,
						   MASK_DATAMODUL_MODULATION_SHAPE,
						   DATAMODUL_MODULATION_SHAPE_1_0);
		case SHAPING_0_5:
			return rf69_read_mod_write(spi, REG_DATAMODUL,
						   MASK_DATAMODUL_MODULATION_SHAPE,
						   DATAMODUL_MODULATION_SHAPE_0_5);
		case SHAPING_0_3:
			return rf69_read_mod_write(spi, REG_DATAMODUL,
						   MASK_DATAMODUL_MODULATION_SHAPE,
						   DATAMODUL_MODULATION_SHAPE_0_3);
		default:
			dev_dbg(&spi->dev, "set: illegal input param");
			return -EINVAL;
		}
	case OOK:
		switch (mod_shaping) {
		case SHAPING_OFF:
			return rf69_read_mod_write(spi, REG_DATAMODUL,
						   MASK_DATAMODUL_MODULATION_SHAPE,
						   DATAMODUL_MODULATION_SHAPE_NONE);
		case SHAPING_BR:
			return rf69_read_mod_write(spi, REG_DATAMODUL,
						   MASK_DATAMODUL_MODULATION_SHAPE,
						   DATAMODUL_MODULATION_SHAPE_BR);
		case SHAPING_2BR:
			return rf69_read_mod_write(spi, REG_DATAMODUL,
						   MASK_DATAMODUL_MODULATION_SHAPE,
						   DATAMODUL_MODULATION_SHAPE_2BR);
		default:
			dev_dbg(&spi->dev, "set: illegal input param");
			return -EINVAL;
		}
	default:
		dev_dbg(&spi->dev, "set: modulation undefined");
		return -EINVAL;
	}
}

int rf69_set_bit_rate(struct spi_device *spi, u16 bit_rate)
{
	int retval;
	u32 bit_rate_min;
	u32 bit_rate_reg;
	u8 msb;
	u8 lsb;

	// check input value
	bit_rate_min = F_OSC / 8388608; // 8388608 = 2^23;
	if (bit_rate < bit_rate_min) {
		dev_dbg(&spi->dev, "setBitRate: illegal input param");
		return -EINVAL;
	}

	// calculate reg settings
	bit_rate_reg = (F_OSC / bit_rate);

	msb = (bit_rate_reg & 0xff00) >> 8;
	lsb = (bit_rate_reg & 0xff);

	// transmit to RF 69
	retval = rf69_write_reg(spi, REG_BITRATE_MSB, msb);
	if (retval)
		return retval;
	retval = rf69_write_reg(spi, REG_BITRATE_LSB, lsb);
	if (retval)
		return retval;

	return 0;
}

int rf69_set_deviation(struct spi_device *spi, u32 deviation)
{
	int retval;
	u64 f_reg;
	u64 f_step;
	u8 msb;
	u8 lsb;
	u64 factor = 1000000; // to improve precision of calculation

	// TODO: Dependency to bitrate
	if (deviation < 600 || deviation > 500000) {
		dev_dbg(&spi->dev, "set_deviation: illegal input param");
		return -EINVAL;
	}

	// calculat f step
	f_step = F_OSC * factor;
	do_div(f_step, 524288); //  524288 = 2^19

	// calculate register settings
	f_reg = deviation * factor;
	do_div(f_reg, f_step);

	msb = (f_reg & 0xff00) >> 8;
	lsb = (f_reg & 0xff);

	// check msb
	if (msb & ~FDEVMASB_MASK) {
		dev_dbg(&spi->dev, "set_deviation: err in calc of msb");
		return -EINVAL;
	}

	// write to chip
	retval = rf69_write_reg(spi, REG_FDEV_MSB, msb);
	if (retval)
		return retval;
	retval = rf69_write_reg(spi, REG_FDEV_LSB, lsb);
	if (retval)
		return retval;

	return 0;
}

int rf69_set_frequency(struct spi_device *spi, u32 frequency)
{
	int retval;
	u32 f_max;
	u64 f_reg;
	u64 f_step;
	u8 msb;
	u8 mid;
	u8 lsb;
	u64 factor = 1000000; // to improve precision of calculation

	// calculat f step
	f_step = F_OSC * factor;
	do_div(f_step, 524288); //  524288 = 2^19

	// check input value
	f_max = div_u64(f_step * 8388608, factor);
	if (frequency > f_max) {
		dev_dbg(&spi->dev, "setFrequency: illegal input param");
		return -EINVAL;
	}

	// calculate reg settings
	f_reg = frequency * factor;
	do_div(f_reg, f_step);

	msb = (f_reg & 0xff0000) >> 16;
	mid = (f_reg & 0xff00)   >>  8;
	lsb = (f_reg & 0xff);

	// write to chip
	retval = rf69_write_reg(spi, REG_FRF_MSB, msb);
	if (retval)
		return retval;
	retval = rf69_write_reg(spi, REG_FRF_MID, mid);
	if (retval)
		return retval;
	retval = rf69_write_reg(spi, REG_FRF_LSB, lsb);
	if (retval)
		return retval;

	return 0;
}

int rf69_enable_amplifier(struct spi_device *spi, u8 amplifier_mask)
{
	return rf69_set_bit(spi, REG_PALEVEL, amplifier_mask);
}

int rf69_disable_amplifier(struct spi_device *spi, u8 amplifier_mask)
{
	return rf69_clear_bit(spi, REG_PALEVEL, amplifier_mask);
}

int rf69_set_output_power_level(struct spi_device *spi, u8 power_level)
{
	// TODO: Dependency to PA0,1,2 setting
	power_level += 18;

	// check input value
	if (power_level > 0x1f) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	// write value
	return rf69_read_mod_write(spi, REG_PALEVEL, MASK_PALEVEL_OUTPUT_POWER,
				   power_level);
}

int rf69_set_pa_ramp(struct spi_device *spi, enum pa_ramp pa_ramp)
{
	static const u8 pa_ramp_map[] = {
		[ramp3400] = PARAMP_3400,
		[ramp2000] = PARAMP_2000,
		[ramp1000] = PARAMP_1000,
		[ramp500] = PARAMP_500,
		[ramp250] = PARAMP_250,
		[ramp125] = PARAMP_125,
		[ramp100] = PARAMP_100,
		[ramp62] = PARAMP_62,
		[ramp50] = PARAMP_50,
		[ramp40] = PARAMP_40,
		[ramp31] = PARAMP_31,
		[ramp25] = PARAMP_25,
		[ramp20] = PARAMP_20,
		[ramp15] = PARAMP_15,
		[ramp10] = PARAMP_10,
	};

	if (unlikely(pa_ramp >= ARRAY_SIZE(pa_ramp_map))) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	return rf69_write_reg(spi, REG_PARAMP, pa_ramp_map[pa_ramp]);
}

int rf69_set_antenna_impedance(struct spi_device *spi,
			       enum antenna_impedance antenna_impedance)
{
	switch (antenna_impedance) {
	case fifty_ohm:
		return rf69_clear_bit(spi, REG_LNA, MASK_LNA_ZIN);
	case two_hundred_ohm:
		return rf69_set_bit(spi, REG_LNA, MASK_LNA_ZIN);
	default:
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}
}

int rf69_set_lna_gain(struct spi_device *spi, enum lna_gain lna_gain)
{
	static const u8 lna_gain_map[] = {
		[automatic] = LNA_GAIN_AUTO,
		[max] = LNA_GAIN_MAX,
		[max_minus_6] = LNA_GAIN_MAX_MINUS_6,
		[max_minus_12] = LNA_GAIN_MAX_MINUS_12,
		[max_minus_24] = LNA_GAIN_MAX_MINUS_24,
		[max_minus_36] = LNA_GAIN_MAX_MINUS_36,
		[max_minus_48] = LNA_GAIN_MAX_MINUS_48,
	};

	if (unlikely(lna_gain >= ARRAY_SIZE(lna_gain_map))) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	return rf69_read_mod_write(spi, REG_LNA, MASK_LNA_GAIN,
				   lna_gain_map[lna_gain]);
}

static int rf69_set_bandwidth_intern(struct spi_device *spi, u8 reg,
				     enum mantisse mantisse, u8 exponent)
{
	u8 bandwidth;

	// check value for mantisse and exponent
	if (exponent > 7) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	if ((mantisse != mantisse16) &&
	    (mantisse != mantisse20) &&
	    (mantisse != mantisse24)) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	// read old value
	bandwidth = rf69_read_reg(spi, reg);

	// "delete" mantisse and exponent = just keep the DCC setting
	bandwidth = bandwidth & MASK_BW_DCC_FREQ;

	// add new mantisse
	switch (mantisse) {
	case mantisse16:
		bandwidth = bandwidth | BW_MANT_16;
		break;
	case mantisse20:
		bandwidth = bandwidth | BW_MANT_20;
		break;
	case mantisse24:
		bandwidth = bandwidth | BW_MANT_24;
		break;
	}

	// add new exponent
	bandwidth = bandwidth | exponent;

	// write back
	return rf69_write_reg(spi, reg, bandwidth);
}

int rf69_set_bandwidth(struct spi_device *spi, enum mantisse mantisse,
		       u8 exponent)
{
	return rf69_set_bandwidth_intern(spi, REG_RXBW, mantisse, exponent);
}

int rf69_set_bandwidth_during_afc(struct spi_device *spi,
				  enum mantisse mantisse,
				  u8 exponent)
{
	return rf69_set_bandwidth_intern(spi, REG_AFCBW, mantisse, exponent);
}

int rf69_set_ook_threshold_dec(struct spi_device *spi,
			       enum threshold_decrement threshold_decrement)
{
	static const u8 td_map[] = {
		[dec_every8th] = OOKPEAK_THRESHDEC_EVERY_8TH,
		[dec_every4th] = OOKPEAK_THRESHDEC_EVERY_4TH,
		[dec_every2nd] = OOKPEAK_THRESHDEC_EVERY_2ND,
		[dec_once] = OOKPEAK_THRESHDEC_ONCE,
		[dec_twice] = OOKPEAK_THRESHDEC_TWICE,
		[dec_4times] = OOKPEAK_THRESHDEC_4_TIMES,
		[dec_8times] = OOKPEAK_THRESHDEC_8_TIMES,
		[dec_16times] = OOKPEAK_THRESHDEC_16_TIMES,
	};

	if (unlikely(threshold_decrement >= ARRAY_SIZE(td_map))) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	return rf69_read_mod_write(spi, REG_OOKPEAK, MASK_OOKPEAK_THRESDEC,
				   td_map[threshold_decrement]);
}

int rf69_set_dio_mapping(struct spi_device *spi, u8 dio_number, u8 value)
{
	u8 mask;
	u8 shift;
	u8 dio_addr;
	u8 dio_value;

	switch (dio_number) {
	case 0:
		mask = MASK_DIO0;
		shift = SHIFT_DIO0;
		dio_addr = REG_DIOMAPPING1;
		break;
	case 1:
		mask = MASK_DIO1;
		shift = SHIFT_DIO1;
		dio_addr = REG_DIOMAPPING1;
		break;
	case 2:
		mask = MASK_DIO2;
		shift = SHIFT_DIO2;
		dio_addr = REG_DIOMAPPING1;
		break;
	case 3:
		mask = MASK_DIO3;
		shift = SHIFT_DIO3;
		dio_addr = REG_DIOMAPPING1;
		break;
	case 4:
		mask = MASK_DIO4;
		shift = SHIFT_DIO4;
		dio_addr = REG_DIOMAPPING2;
		break;
	case 5:
		mask = MASK_DIO5;
		shift = SHIFT_DIO5;
		dio_addr = REG_DIOMAPPING2;
		break;
	default:
	dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	// read reg
	dio_value = rf69_read_reg(spi, dio_addr);
	// delete old value
	dio_value = dio_value & ~mask;
	// add new value
	dio_value = dio_value | value << shift;
	// write back
	return rf69_write_reg(spi, dio_addr, dio_value);
}

bool rf69_get_flag(struct spi_device *spi, enum flag flag)
{
	switch (flag) {
	case mode_switch_completed:
		return (rf69_read_reg(spi, REG_IRQFLAGS1) & MASK_IRQFLAGS1_MODE_READY);
	case ready_to_receive:
		return (rf69_read_reg(spi, REG_IRQFLAGS1) & MASK_IRQFLAGS1_RX_READY);
	case ready_to_send:
		return (rf69_read_reg(spi, REG_IRQFLAGS1) & MASK_IRQFLAGS1_TX_READY);
	case pll_locked:
		return (rf69_read_reg(spi, REG_IRQFLAGS1) & MASK_IRQFLAGS1_PLL_LOCK);
	case rssi_exceeded_threshold:
		return (rf69_read_reg(spi, REG_IRQFLAGS1) & MASK_IRQFLAGS1_RSSI);
	case timeout:
		return (rf69_read_reg(spi, REG_IRQFLAGS1) & MASK_IRQFLAGS1_TIMEOUT);
	case automode:
		return (rf69_read_reg(spi, REG_IRQFLAGS1) & MASK_IRQFLAGS1_AUTOMODE);
	case sync_address_match:
		return (rf69_read_reg(spi, REG_IRQFLAGS1) & MASK_IRQFLAGS1_SYNC_ADDRESS_MATCH);
	case fifo_full:
		return (rf69_read_reg(spi, REG_IRQFLAGS2) & MASK_IRQFLAGS2_FIFO_FULL);
/*
 *	case fifo_not_empty:
 *		return (rf69_read_reg(spi, REG_IRQFLAGS2) & MASK_IRQFLAGS2_FIFO_NOT_EMPTY);
 */
	case fifo_empty:
		return !(rf69_read_reg(spi, REG_IRQFLAGS2) & MASK_IRQFLAGS2_FIFO_NOT_EMPTY);
	case fifo_level_below_threshold:
		return (rf69_read_reg(spi, REG_IRQFLAGS2) & MASK_IRQFLAGS2_FIFO_LEVEL);
	case fifo_overrun:
		return (rf69_read_reg(spi, REG_IRQFLAGS2) & MASK_IRQFLAGS2_FIFO_OVERRUN);
	case packet_sent:
		return (rf69_read_reg(spi, REG_IRQFLAGS2) & MASK_IRQFLAGS2_PACKET_SENT);
	case payload_ready:
		return (rf69_read_reg(spi, REG_IRQFLAGS2) & MASK_IRQFLAGS2_PAYLOAD_READY);
	case crc_ok:
		return (rf69_read_reg(spi, REG_IRQFLAGS2) & MASK_IRQFLAGS2_CRC_OK);
	case battery_low:
		return (rf69_read_reg(spi, REG_IRQFLAGS2) & MASK_IRQFLAGS2_LOW_BAT);
	default:			 return false;
	}
}

int rf69_set_rssi_threshold(struct spi_device *spi, u8 threshold)
{
	/* no value check needed - u8 exactly matches register size */

	return rf69_write_reg(spi, REG_RSSITHRESH, threshold);
}

int rf69_set_preamble_length(struct spi_device *spi, u16 preamble_length)
{
	int retval;
	u8 msb, lsb;

	/* no value check needed - u16 exactly matches register size */

	/* calculate reg settings */
	msb = (preamble_length & 0xff00) >> 8;
	lsb = (preamble_length & 0xff);

	/* transmit to chip */
	retval = rf69_write_reg(spi, REG_PREAMBLE_MSB, msb);
	if (retval)
		return retval;
	retval = rf69_write_reg(spi, REG_PREAMBLE_LSB, lsb);

	return retval;
}

int rf69_enable_sync(struct spi_device *spi)
{
	return rf69_set_bit(spi, REG_SYNC_CONFIG, MASK_SYNC_CONFIG_SYNC_ON);
}

int rf69_disable_sync(struct spi_device *spi)
{
	return rf69_clear_bit(spi, REG_SYNC_CONFIG, MASK_SYNC_CONFIG_SYNC_ON);
}

int rf69_set_fifo_fill_condition(struct spi_device *spi,
				 enum fifo_fill_condition fifo_fill_condition)
{
	switch (fifo_fill_condition) {
	case always:
		return rf69_set_bit(spi, REG_SYNC_CONFIG,
				    MASK_SYNC_CONFIG_FIFO_FILL_CONDITION);
	case after_sync_interrupt:
		return rf69_clear_bit(spi, REG_SYNC_CONFIG,
				      MASK_SYNC_CONFIG_FIFO_FILL_CONDITION);
	default:
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}
}

int rf69_set_sync_size(struct spi_device *spi, u8 sync_size)
{
	// check input value
	if (sync_size > 0x07) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	// write value
	return rf69_read_mod_write(spi, REG_SYNC_CONFIG,
				   MASK_SYNC_CONFIG_SYNC_SIZE,
				   (sync_size << 3));
}

int rf69_set_sync_values(struct spi_device *spi, u8 sync_values[8])
{
	int retval = 0;

	retval += rf69_write_reg(spi, REG_SYNCVALUE1, sync_values[0]);
	retval += rf69_write_reg(spi, REG_SYNCVALUE2, sync_values[1]);
	retval += rf69_write_reg(spi, REG_SYNCVALUE3, sync_values[2]);
	retval += rf69_write_reg(spi, REG_SYNCVALUE4, sync_values[3]);
	retval += rf69_write_reg(spi, REG_SYNCVALUE5, sync_values[4]);
	retval += rf69_write_reg(spi, REG_SYNCVALUE6, sync_values[5]);
	retval += rf69_write_reg(spi, REG_SYNCVALUE7, sync_values[6]);
	retval += rf69_write_reg(spi, REG_SYNCVALUE8, sync_values[7]);

	return retval;
}

int rf69_set_packet_format(struct spi_device *spi,
			   enum packet_format packet_format)
{
	switch (packet_format) {
	case packet_length_var:
		return rf69_set_bit(spi, REG_PACKETCONFIG1,
				    MASK_PACKETCONFIG1_PAKET_FORMAT_VARIABLE);
	case packet_length_fix:
		return rf69_clear_bit(spi, REG_PACKETCONFIG1,
				      MASK_PACKETCONFIG1_PAKET_FORMAT_VARIABLE);
	default:
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}
}

int rf69_enable_crc(struct spi_device *spi)
{
	return rf69_set_bit(spi, REG_PACKETCONFIG1, MASK_PACKETCONFIG1_CRC_ON);
}

int rf69_disable_crc(struct spi_device *spi)
{
	return rf69_clear_bit(spi, REG_PACKETCONFIG1, MASK_PACKETCONFIG1_CRC_ON);
}

int rf69_set_address_filtering(struct spi_device *spi,
			       enum address_filtering address_filtering)
{
	static const u8 af_map[] = {
		[filtering_off] = PACKETCONFIG1_ADDRESSFILTERING_OFF,
		[node_address] = PACKETCONFIG1_ADDRESSFILTERING_NODE,
		[node_or_broadcast_address] =
			PACKETCONFIG1_ADDRESSFILTERING_NODEBROADCAST,
	};

	if (unlikely(address_filtering >= ARRAY_SIZE(af_map))) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	return rf69_read_mod_write(spi, REG_PACKETCONFIG1,
				   MASK_PACKETCONFIG1_ADDRESSFILTERING,
				   af_map[address_filtering]);
}

int rf69_set_payload_length(struct spi_device *spi, u8 payload_length)
{
	return rf69_write_reg(spi, REG_PAYLOAD_LENGTH, payload_length);
}

int rf69_set_node_address(struct spi_device *spi, u8 node_address)
{
	return rf69_write_reg(spi, REG_NODEADRS, node_address);
}

int rf69_set_broadcast_address(struct spi_device *spi, u8 broadcast_address)
{
	return rf69_write_reg(spi, REG_BROADCASTADRS, broadcast_address);
}

int rf69_set_tx_start_condition(struct spi_device *spi,
				enum tx_start_condition tx_start_condition)
{
	switch (tx_start_condition) {
	case fifo_level:
		return rf69_clear_bit(spi, REG_FIFO_THRESH,
				      MASK_FIFO_THRESH_TXSTART);
	case fifo_not_empty:
		return rf69_set_bit(spi, REG_FIFO_THRESH,
				    MASK_FIFO_THRESH_TXSTART);
	default:
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}
}

int rf69_set_fifo_threshold(struct spi_device *spi, u8 threshold)
{
	int retval;

	/* check input value */
	if (threshold & 0x80) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	/* write value */
	retval = rf69_read_mod_write(spi, REG_FIFO_THRESH,
				     MASK_FIFO_THRESH_VALUE,
				     threshold);
	if (retval)
		return retval;

	/*
	 * access the fifo to activate new threshold
	 * retval (mis-) used as buffer here
	 */
	return rf69_read_fifo(spi, (u8 *)&retval, 1);
}

int rf69_set_dagc(struct spi_device *spi, enum dagc dagc)
{
	static const u8 dagc_map[] = {
		[normal_mode] = DAGC_NORMAL,
		[improve] = DAGC_IMPROVED_LOWBETA0,
		[improve_for_low_modulation_index] = DAGC_IMPROVED_LOWBETA1,
	};

	if (unlikely(dagc >= ARRAY_SIZE(dagc_map))) {
		dev_dbg(&spi->dev, "set: illegal input param");
		return -EINVAL;
	}

	return rf69_write_reg(spi, REG_TESTDAGC, dagc_map[dagc]);
}

/*-------------------------------------------------------------------------*/

int rf69_read_fifo(struct spi_device *spi, u8 *buffer, unsigned int size)
{
#ifdef DEBUG_FIFO_ACCESS
	int i;
#endif
	struct spi_transfer transfer;
	u8 local_buffer[FIFO_SIZE + 1];
	int retval;

	if (size > FIFO_SIZE) {
		dev_dbg(&spi->dev,
			"read fifo: passed in buffer bigger then internal buffer\n");
		return -EMSGSIZE;
	}

	/* prepare a bidirectional transfer */
	local_buffer[0] = REG_FIFO;
	memset(&transfer, 0, sizeof(transfer));
	transfer.tx_buf = local_buffer;
	transfer.rx_buf = local_buffer;
	transfer.len	= size + 1;

	retval = spi_sync_transfer(spi, &transfer, 1);

#ifdef DEBUG_FIFO_ACCESS
	for (i = 0; i < size; i++)
		dev_dbg(&spi->dev, "%d - 0x%x\n", i, local_buffer[i + 1]);
#endif

	memcpy(buffer, &local_buffer[1], size);

	return retval;
}

int rf69_write_fifo(struct spi_device *spi, u8 *buffer, unsigned int size)
{
#ifdef DEBUG_FIFO_ACCESS
	int i;
#endif
	u8 local_buffer[FIFO_SIZE + 1];

	if (size > FIFO_SIZE) {
		dev_dbg(&spi->dev,
			"read fifo: passed in buffer bigger then internal buffer\n");
		return -EMSGSIZE;
	}

	local_buffer[0] = REG_FIFO | WRITE_BIT;
	memcpy(&local_buffer[1], buffer, size);

#ifdef DEBUG_FIFO_ACCESS
	for (i = 0; i < size; i++)
		dev_dbg(&spi->dev, "0x%x\n", buffer[i]);
#endif

	return spi_write(spi, local_buffer, size + 1);
}

