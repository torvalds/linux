/*
    comedi/drivers/cb_pcidas64.c
    This is a driver for the ComputerBoards/MeasurementComputing PCI-DAS
    64xx, 60xx, and 4020 cards.

    Author:  Frank Mori Hess <fmhess@users.sourceforge.net>
    Copyright (C) 2001, 2002 Frank Mori Hess

    Thanks also go to the following people:

    Steve Rosenbluth, for providing the source code for
    his pci-das6402 driver, and source code for working QNX pci-6402
    drivers by Greg Laird and Mariusz Bogacz.  None of the code was
    used directly here, but it was useful as an additional source of
    documentation on how to program the boards.

    John Sims, for much testing and feedback on pcidas-4020 support.

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

/*
 * Driver: cb_pcidas64
 * Description: MeasurementComputing PCI-DAS64xx, 60XX, and 4020 series
 *   with the PLX 9080 PCI controller
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Status: works
 * Updated: Fri, 02 Nov 2012 18:58:55 +0000
 * Devices: [Measurement Computing] PCI-DAS6402/16 (cb_pcidas64),
 *   PCI-DAS6402/12, PCI-DAS64/M1/16, PCI-DAS64/M2/16,
 *   PCI-DAS64/M3/16, PCI-DAS6402/16/JR, PCI-DAS64/M1/16/JR,
 *   PCI-DAS64/M2/16/JR, PCI-DAS64/M3/16/JR, PCI-DAS64/M1/14,
 *   PCI-DAS64/M2/14, PCI-DAS64/M3/14, PCI-DAS6013, PCI-DAS6014,
 *   PCI-DAS6023, PCI-DAS6025, PCI-DAS6030,
 *   PCI-DAS6031, PCI-DAS6032, PCI-DAS6033, PCI-DAS6034,
 *   PCI-DAS6035, PCI-DAS6036, PCI-DAS6040, PCI-DAS6052,
 *   PCI-DAS6070, PCI-DAS6071, PCI-DAS4020/12
 *
 * Configuration options:
 *   None.
 *
 * Manual attachment of PCI cards with the comedi_config utility is not
 * supported by this driver; they are attached automatically.
 *
 * These boards may be autocalibrated with the comedi_calibrate utility.
 *
 * To select the bnc trigger input on the 4020 (instead of the dio input),
 * specify a nonzero channel in the chanspec.  If you wish to use an external
 * master clock on the 4020, you may do so by setting the scan_begin_src
 * to TRIG_OTHER, and using an INSN_CONFIG_TIMER_1 configuration insn
 * to configure the divisor to use for the external clock.
 *
 * Some devices are not identified because the PCI device IDs are not yet
 * known. If you have such a board, please let the maintainers know.
 */

/*

TODO:
	make it return error if user attempts an ai command that uses the
	external queue, and an ao command simultaneously user counter subdevice
	there are a number of boards this driver will support when they are
	fully released, but does not yet since the pci device id numbers
	are not yet available.

	support prescaled 100khz clock for slow pacing (not available on 6000
	series?)

	make ao fifo size adjustable like ai fifo
*/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "8253.h"
#include "8255.h"
#include "plx9080.h"
#include "comedi_fc.h"

#define TIMER_BASE 25		/*  40MHz master clock */
/* 100kHz 'prescaled' clock for slow acquisition,
 * maybe I'll support this someday */
#define PRESCALED_TIMER_BASE	10000
#define DMA_BUFFER_SIZE 0x1000

/* maximum value that can be loaded into board's 24-bit counters*/
static const int max_counter_value = 0xffffff;

/* PCI-DAS64xxx base addresses */

/* devpriv->main_iobase registers */
enum write_only_registers {
	INTR_ENABLE_REG = 0x0,	/*  interrupt enable register */
	HW_CONFIG_REG = 0x2,	/*  hardware config register */
	DAQ_SYNC_REG = 0xc,
	DAQ_ATRIG_LOW_4020_REG = 0xc,
	ADC_CONTROL0_REG = 0x10,	/*  adc control register 0 */
	ADC_CONTROL1_REG = 0x12,	/*  adc control register 1 */
	CALIBRATION_REG = 0x14,
	/*  lower 16 bits of adc sample interval counter */
	ADC_SAMPLE_INTERVAL_LOWER_REG = 0x16,
	/*  upper 8 bits of adc sample interval counter */
	ADC_SAMPLE_INTERVAL_UPPER_REG = 0x18,
	/*  lower 16 bits of delay interval counter */
	ADC_DELAY_INTERVAL_LOWER_REG = 0x1a,
	/*  upper 8 bits of delay interval counter */
	ADC_DELAY_INTERVAL_UPPER_REG = 0x1c,
	/*  lower 16 bits of hardware conversion/scan counter */
	ADC_COUNT_LOWER_REG = 0x1e,
	/*  upper 8 bits of hardware conversion/scan counter */
	ADC_COUNT_UPPER_REG = 0x20,
	ADC_START_REG = 0x22,	/*  software trigger to start acquisition */
	ADC_CONVERT_REG = 0x24,	/*  initiates single conversion */
	ADC_QUEUE_CLEAR_REG = 0x26,	/*  clears adc queue */
	ADC_QUEUE_LOAD_REG = 0x28,	/*  loads adc queue */
	ADC_BUFFER_CLEAR_REG = 0x2a,
	/*  high channel for internal queue, use adc_chan_bits() inline above */
	ADC_QUEUE_HIGH_REG = 0x2c,
	DAC_CONTROL0_REG = 0x50,	/*  dac control register 0 */
	DAC_CONTROL1_REG = 0x52,	/*  dac control register 0 */
	/*  lower 16 bits of dac sample interval counter */
	DAC_SAMPLE_INTERVAL_LOWER_REG = 0x54,
	/*  upper 8 bits of dac sample interval counter */
	DAC_SAMPLE_INTERVAL_UPPER_REG = 0x56,
	DAC_SELECT_REG = 0x60,
	DAC_START_REG = 0x64,
	DAC_BUFFER_CLEAR_REG = 0x66,	/*  clear dac buffer */
};

static inline unsigned int dac_convert_reg(unsigned int channel)
{
	return 0x70 + (2 * (channel & 0x1));
}

static inline unsigned int dac_lsb_4020_reg(unsigned int channel)
{
	return 0x70 + (4 * (channel & 0x1));
}

static inline unsigned int dac_msb_4020_reg(unsigned int channel)
{
	return 0x72 + (4 * (channel & 0x1));
}

enum read_only_registers {
	/*  hardware status register,
	 *  reading this apparently clears pending interrupts as well */
	HW_STATUS_REG = 0x0,
	PIPE1_READ_REG = 0x4,
	ADC_READ_PNTR_REG = 0x8,
	LOWER_XFER_REG = 0x10,
	ADC_WRITE_PNTR_REG = 0xc,
	PREPOST_REG = 0x14,
};

enum read_write_registers {
	I8255_4020_REG = 0x48,	/*  8255 offset, for 4020 only */
	/*  external channel/gain queue, uses same bits as ADC_QUEUE_LOAD_REG */
	ADC_QUEUE_FIFO_REG = 0x100,
	ADC_FIFO_REG = 0x200,	/* adc data fifo */
	/* dac data fifo, has weird interactions with external channel queue */
	DAC_FIFO_REG = 0x300,
};

/* dev->mmio registers */
enum dio_counter_registers {
	DIO_8255_OFFSET = 0x0,
	DO_REG = 0x20,
	DI_REG = 0x28,
	DIO_DIRECTION_60XX_REG = 0x40,
	DIO_DATA_60XX_REG = 0x48,
};

/* bit definitions for write-only registers */

enum intr_enable_contents {
	ADC_INTR_SRC_MASK = 0x3,	/*  adc interrupt source mask */
	ADC_INTR_QFULL_BITS = 0x0,	/*  interrupt fifo quarter full */
	ADC_INTR_EOC_BITS = 0x1,	/*  interrupt end of conversion */
	ADC_INTR_EOSCAN_BITS = 0x2,	/*  interrupt end of scan */
	ADC_INTR_EOSEQ_BITS = 0x3,	/*  interrupt end of sequence mask */
	EN_ADC_INTR_SRC_BIT = 0x4,	/*  enable adc interrupt source */
	EN_ADC_DONE_INTR_BIT = 0x8,	/*  enable adc acquisition done intr */
	DAC_INTR_SRC_MASK = 0x30,
	DAC_INTR_QEMPTY_BITS = 0x0,
	DAC_INTR_HIGH_CHAN_BITS = 0x10,
	EN_DAC_INTR_SRC_BIT = 0x40,	/*  enable dac interrupt source */
	EN_DAC_DONE_INTR_BIT = 0x80,
	EN_ADC_ACTIVE_INTR_BIT = 0x200,	/*  enable adc active interrupt */
	EN_ADC_STOP_INTR_BIT = 0x400,	/*  enable adc stop trigger interrupt */
	EN_DAC_ACTIVE_INTR_BIT = 0x800,	/*  enable dac active interrupt */
	EN_DAC_UNDERRUN_BIT = 0x4000,	/*  enable dac underrun status bit */
	EN_ADC_OVERRUN_BIT = 0x8000,	/*  enable adc overrun status bit */
};

enum hw_config_contents {
	MASTER_CLOCK_4020_MASK = 0x3,	/*  master clock source mask for 4020 */
	INTERNAL_CLOCK_4020_BITS = 0x1,	/*  use 40 MHz internal master clock */
	BNC_CLOCK_4020_BITS = 0x2,	/*  use BNC input for master clock */
	EXT_CLOCK_4020_BITS = 0x3,	/*  use dio input for master clock */
	EXT_QUEUE_BIT = 0x200,		/*  use external channel/gain queue */
	/*  use 225 nanosec strobe when loading dac instead of 50 nanosec */
	SLOW_DAC_BIT = 0x400,
	/*  bit with unknown function yet given as default value in pci-das64
	 *  manual */
	HW_CONFIG_DUMMY_BITS = 0x2000,
	/*  bit selects channels 1/0 for analog input/output, otherwise 0/1 */
	DMA_CH_SELECT_BIT = 0x8000,
	FIFO_SIZE_REG = 0x4,		/*  allows adjustment of fifo sizes */
	DAC_FIFO_SIZE_MASK = 0xff00,	/*  bits that set dac fifo size */
	DAC_FIFO_BITS = 0xf800,		/*  8k sample ao fifo */
};
#define DAC_FIFO_SIZE 0x2000

enum daq_atrig_low_4020_contents {
	/*  use trig/ext clk bnc input for analog gate signal */
	EXT_AGATE_BNC_BIT = 0x8000,
	/*  use trig/ext clk bnc input for external stop trigger signal */
	EXT_STOP_TRIG_BNC_BIT = 0x4000,
	/*  use trig/ext clk bnc input for external start trigger signal */
	EXT_START_TRIG_BNC_BIT = 0x2000,
};

static inline uint16_t analog_trig_low_threshold_bits(uint16_t threshold)
{
	return threshold & 0xfff;
}

enum adc_control0_contents {
	ADC_GATE_SRC_MASK = 0x3,	/*  bits that select gate */
	ADC_SOFT_GATE_BITS = 0x1,	/*  software gate */
	ADC_EXT_GATE_BITS = 0x2,	/*  external digital gate */
	ADC_ANALOG_GATE_BITS = 0x3,	/*  analog level gate */
	ADC_GATE_LEVEL_BIT = 0x4,	/*  level-sensitive gate (for digital) */
	ADC_GATE_POLARITY_BIT = 0x8,	/*  gate active low */
	ADC_START_TRIG_SOFT_BITS = 0x10,
	ADC_START_TRIG_EXT_BITS = 0x20,
	ADC_START_TRIG_ANALOG_BITS = 0x30,
	ADC_START_TRIG_MASK = 0x30,
	ADC_START_TRIG_FALLING_BIT = 0x40,	/*  trig 1 uses falling edge */
	/*  external pacing uses falling edge */
	ADC_EXT_CONV_FALLING_BIT = 0x800,
	/*  enable hardware scan counter */
	ADC_SAMPLE_COUNTER_EN_BIT = 0x1000,
	ADC_DMA_DISABLE_BIT = 0x4000,	/*  disables dma */
	ADC_ENABLE_BIT = 0x8000,	/*  master adc enable */
};

enum adc_control1_contents {
	/*  should be set for boards with > 16 channels */
	ADC_QUEUE_CONFIG_BIT = 0x1,
	CONVERT_POLARITY_BIT = 0x10,
	EOC_POLARITY_BIT = 0x20,
	ADC_SW_GATE_BIT = 0x40,	/*  software gate of adc */
	ADC_DITHER_BIT = 0x200,	/*  turn on extra noise for dithering */
	RETRIGGER_BIT = 0x800,
	ADC_LO_CHANNEL_4020_MASK = 0x300,
	ADC_HI_CHANNEL_4020_MASK = 0xc00,
	TWO_CHANNEL_4020_BITS = 0x1000,	/*  two channel mode for 4020 */
	FOUR_CHANNEL_4020_BITS = 0x2000, /*  four channel mode for 4020 */
	CHANNEL_MODE_4020_MASK = 0x3000,
	ADC_MODE_MASK = 0xf000,
};

static inline uint16_t adc_lo_chan_4020_bits(unsigned int channel)
{
	return (channel & 0x3) << 8;
};

static inline uint16_t adc_hi_chan_4020_bits(unsigned int channel)
{
	return (channel & 0x3) << 10;
};

static inline uint16_t adc_mode_bits(unsigned int mode)
{
	return (mode & 0xf) << 12;
};

enum calibration_contents {
	SELECT_8800_BIT = 0x1,
	SELECT_8402_64XX_BIT = 0x2,
	SELECT_1590_60XX_BIT = 0x2,
	CAL_EN_64XX_BIT = 0x40,	/*  calibration enable for 64xx series */
	SERIAL_DATA_IN_BIT = 0x80,
	SERIAL_CLOCK_BIT = 0x100,
	CAL_EN_60XX_BIT = 0x200, /*  calibration enable for 60xx series */
	CAL_GAIN_BIT = 0x800,
};

/* calibration sources for 6025 are:
 *  0 : ground
 *  1 : 10V
 *  2 : 5V
 *  3 : 0.5V
 *  4 : 0.05V
 *  5 : ground
 *  6 : dac channel 0
 *  7 : dac channel 1
 */

static inline uint16_t adc_src_bits(unsigned int source)
{
	return (source & 0xf) << 3;
};

static inline uint16_t adc_convert_chan_4020_bits(unsigned int channel)
{
	return (channel & 0x3) << 8;
};

enum adc_queue_load_contents {
	UNIP_BIT = 0x800,	/*  unipolar/bipolar bit */
	ADC_SE_DIFF_BIT = 0x1000,	/*  single-ended/ differential bit */
	/*  non-referenced single-ended (common-mode input) */
	ADC_COMMON_BIT = 0x2000,
	QUEUE_EOSEQ_BIT = 0x4000,	/*  queue end of sequence */
	QUEUE_EOSCAN_BIT = 0x8000,	/*  queue end of scan */
};

static inline uint16_t adc_chan_bits(unsigned int channel)
{
	return channel & 0x3f;
};

enum dac_control0_contents {
	DAC_ENABLE_BIT = 0x8000,	/*  dac controller enable bit */
	DAC_CYCLIC_STOP_BIT = 0x4000,
	DAC_WAVEFORM_MODE_BIT = 0x100,
	DAC_EXT_UPDATE_FALLING_BIT = 0x80,
	DAC_EXT_UPDATE_ENABLE_BIT = 0x40,
	WAVEFORM_TRIG_MASK = 0x30,
	WAVEFORM_TRIG_DISABLED_BITS = 0x0,
	WAVEFORM_TRIG_SOFT_BITS = 0x10,
	WAVEFORM_TRIG_EXT_BITS = 0x20,
	WAVEFORM_TRIG_ADC1_BITS = 0x30,
	WAVEFORM_TRIG_FALLING_BIT = 0x8,
	WAVEFORM_GATE_LEVEL_BIT = 0x4,
	WAVEFORM_GATE_ENABLE_BIT = 0x2,
	WAVEFORM_GATE_SELECT_BIT = 0x1,
};

enum dac_control1_contents {
	DAC_WRITE_POLARITY_BIT = 0x800,	/* board-dependent setting */
	DAC1_EXT_REF_BIT = 0x200,
	DAC0_EXT_REF_BIT = 0x100,
	DAC_OUTPUT_ENABLE_BIT = 0x80,	/*  dac output enable bit */
	DAC_UPDATE_POLARITY_BIT = 0x40,	/* board-dependent setting */
	DAC_SW_GATE_BIT = 0x20,
	DAC1_UNIPOLAR_BIT = 0x8,
	DAC0_UNIPOLAR_BIT = 0x2,
};

/* bit definitions for read-only registers */
enum hw_status_contents {
	DAC_UNDERRUN_BIT = 0x1,
	ADC_OVERRUN_BIT = 0x2,
	DAC_ACTIVE_BIT = 0x4,
	ADC_ACTIVE_BIT = 0x8,
	DAC_INTR_PENDING_BIT = 0x10,
	ADC_INTR_PENDING_BIT = 0x20,
	DAC_DONE_BIT = 0x40,
	ADC_DONE_BIT = 0x80,
	EXT_INTR_PENDING_BIT = 0x100,
	ADC_STOP_BIT = 0x200,
};

static inline uint16_t pipe_full_bits(uint16_t hw_status_bits)
{
	return (hw_status_bits >> 10) & 0x3;
};

static inline unsigned int dma_chain_flag_bits(uint16_t prepost_bits)
{
	return (prepost_bits >> 6) & 0x3;
}

static inline unsigned int adc_upper_read_ptr_code(uint16_t prepost_bits)
{
	return (prepost_bits >> 12) & 0x3;
}

static inline unsigned int adc_upper_write_ptr_code(uint16_t prepost_bits)
{
	return (prepost_bits >> 14) & 0x3;
}

/* I2C addresses for 4020 */
enum i2c_addresses {
	RANGE_CAL_I2C_ADDR = 0x20,
	CALDAC0_I2C_ADDR = 0xc,
	CALDAC1_I2C_ADDR = 0xd,
};

enum range_cal_i2c_contents {
	/*  bits that set what source the adc converter measures */
	ADC_SRC_4020_MASK = 0x70,
	/*  make bnc trig/ext clock threshold 0V instead of 2.5V */
	BNC_TRIG_THRESHOLD_0V_BIT = 0x80,
};

static inline uint8_t adc_src_4020_bits(unsigned int source)
{
	return (source << 4) & ADC_SRC_4020_MASK;
};

static inline uint8_t attenuate_bit(unsigned int channel)
{
	/*  attenuate channel (+-5V input range) */
	return 1 << (channel & 0x3);
};

/* analog input ranges for 64xx boards */
static const struct comedi_lrange ai_ranges_64xx = {
	8, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

/* analog input ranges for 60xx boards */
static const struct comedi_lrange ai_ranges_60xx = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05)
	}
};

/* analog input ranges for 6030, etc boards */
static const struct comedi_lrange ai_ranges_6030 = {
	14, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.2),
		BIP_RANGE(0.1),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1),
		UNI_RANGE(0.5),
		UNI_RANGE(0.2),
		UNI_RANGE(0.1)
	}
};

/* analog input ranges for 6052, etc boards */
static const struct comedi_lrange ai_ranges_6052 = {
	15, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.25),
		BIP_RANGE(0.1),
		BIP_RANGE(0.05),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1),
		UNI_RANGE(0.5),
		UNI_RANGE(0.2),
		UNI_RANGE(0.1)
	}
};

/* analog input ranges for 4020 board */
static const struct comedi_lrange ai_ranges_4020 = {
	2, {
		BIP_RANGE(5),
		BIP_RANGE(1)
	}
};

/* analog output ranges */
static const struct comedi_lrange ao_ranges_64xx = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(10)
	}
};

static const int ao_range_code_64xx[] = {
	0x0,
	0x1,
	0x2,
	0x3,
};

static const int ao_range_code_60xx[] = {
	0x0,
};

static const struct comedi_lrange ao_ranges_6030 = {
	2, {
		BIP_RANGE(10),
		UNI_RANGE(10)
	}
};

static const int ao_range_code_6030[] = {
	0x0,
	0x2,
};

static const struct comedi_lrange ao_ranges_4020 = {
	2, {
		BIP_RANGE(5),
		BIP_RANGE(10)
	}
};

static const int ao_range_code_4020[] = {
	0x1,
	0x0,
};

enum register_layout {
	LAYOUT_60XX,
	LAYOUT_64XX,
	LAYOUT_4020,
};

struct hw_fifo_info {
	unsigned int num_segments;
	unsigned int max_segment_length;
	unsigned int sample_packing_ratio;
	uint16_t fifo_size_reg_mask;
};

enum pcidas64_boardid {
	BOARD_PCIDAS6402_16,
	BOARD_PCIDAS6402_12,
	BOARD_PCIDAS64_M1_16,
	BOARD_PCIDAS64_M2_16,
	BOARD_PCIDAS64_M3_16,
	BOARD_PCIDAS6013,
	BOARD_PCIDAS6014,
	BOARD_PCIDAS6023,
	BOARD_PCIDAS6025,
	BOARD_PCIDAS6030,
	BOARD_PCIDAS6031,
	BOARD_PCIDAS6032,
	BOARD_PCIDAS6033,
	BOARD_PCIDAS6034,
	BOARD_PCIDAS6035,
	BOARD_PCIDAS6036,
	BOARD_PCIDAS6040,
	BOARD_PCIDAS6052,
	BOARD_PCIDAS6070,
	BOARD_PCIDAS6071,
	BOARD_PCIDAS4020_12,
	BOARD_PCIDAS6402_16_JR,
	BOARD_PCIDAS64_M1_16_JR,
	BOARD_PCIDAS64_M2_16_JR,
	BOARD_PCIDAS64_M3_16_JR,
	BOARD_PCIDAS64_M1_14,
	BOARD_PCIDAS64_M2_14,
	BOARD_PCIDAS64_M3_14,
};

struct pcidas64_board {
	const char *name;
	int ai_se_chans;	/*  number of ai inputs in single-ended mode */
	int ai_bits;		/*  analog input resolution */
	int ai_speed;		/*  fastest conversion period in ns */
	const struct comedi_lrange *ai_range_table;
	int ao_nchan;		/*  number of analog out channels */
	int ao_bits;		/*  analog output resolution */
	int ao_scan_speed;	/*  analog output scan speed */
	const struct comedi_lrange *ao_range_table;
	const int *ao_range_code;
	const struct hw_fifo_info *const ai_fifo;
	/*  different board families have slightly different registers */
	enum register_layout layout;
	unsigned has_8255:1;
};

static const struct hw_fifo_info ai_fifo_4020 = {
	.num_segments = 2,
	.max_segment_length = 0x8000,
	.sample_packing_ratio = 2,
	.fifo_size_reg_mask = 0x7f,
};

static const struct hw_fifo_info ai_fifo_64xx = {
	.num_segments = 4,
	.max_segment_length = 0x800,
	.sample_packing_ratio = 1,
	.fifo_size_reg_mask = 0x3f,
};

static const struct hw_fifo_info ai_fifo_60xx = {
	.num_segments = 4,
	.max_segment_length = 0x800,
	.sample_packing_ratio = 1,
	.fifo_size_reg_mask = 0x7f,
};

/* maximum number of dma transfers we will chain together into a ring
 * (and the maximum number of dma buffers we maintain) */
#define MAX_AI_DMA_RING_COUNT (0x80000 / DMA_BUFFER_SIZE)
#define MIN_AI_DMA_RING_COUNT (0x10000 / DMA_BUFFER_SIZE)
#define AO_DMA_RING_COUNT (0x10000 / DMA_BUFFER_SIZE)
static inline unsigned int ai_dma_ring_count(const struct pcidas64_board *board)
{
	if (board->layout == LAYOUT_4020)
		return MAX_AI_DMA_RING_COUNT;

	return MIN_AI_DMA_RING_COUNT;
}

static const int bytes_in_sample = 2;

static const struct pcidas64_board pcidas64_boards[] = {
	[BOARD_PCIDAS6402_16] = {
		.name		= "pci-das6402/16",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 5000,
		.ao_nchan	= 2,
		.ao_bits	= 16,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ao_range_table	= &ao_ranges_64xx,
		.ao_range_code	= ao_range_code_64xx,
		.ai_fifo	= &ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS6402_12] = {
		.name		= "pci-das6402/12",	/*  XXX check */
		.ai_se_chans	= 64,
		.ai_bits	= 12,
		.ai_speed	= 5000,
		.ao_nchan	= 2,
		.ao_bits	= 12,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ao_range_table	= &ao_ranges_64xx,
		.ao_range_code	= ao_range_code_64xx,
		.ai_fifo	= &ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS64_M1_16] = {
		.name		= "pci-das64/m1/16",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 1000,
		.ao_nchan	= 2,
		.ao_bits	= 16,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ao_range_table	= &ao_ranges_64xx,
		.ao_range_code	= ao_range_code_64xx,
		.ai_fifo	= &ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS64_M2_16] = {
		.name = "pci-das64/m2/16",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 500,
		.ao_nchan	= 2,
		.ao_bits	= 16,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ao_range_table	= &ao_ranges_64xx,
		.ao_range_code	= ao_range_code_64xx,
		.ai_fifo	= &ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS64_M3_16] = {
		.name		= "pci-das64/m3/16",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 333,
		.ao_nchan	= 2,
		.ao_bits	= 16,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ao_range_table	= &ao_ranges_64xx,
		.ao_range_code	= ao_range_code_64xx,
		.ai_fifo	= &ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS6013] = {
		.name		= "pci-das6013",
		.ai_se_chans	= 16,
		.ai_bits	= 16,
		.ai_speed	= 5000,
		.ao_nchan	= 0,
		.ao_bits	= 16,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_60xx,
		.ao_range_table	= &range_bipolar10,
		.ao_range_code	= ao_range_code_60xx,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6014] = {
		.name		= "pci-das6014",
		.ai_se_chans	= 16,
		.ai_bits	= 16,
		.ai_speed	= 5000,
		.ao_nchan	= 2,
		.ao_bits	= 16,
		.ao_scan_speed	= 100000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_60xx,
		.ao_range_table	= &range_bipolar10,
		.ao_range_code	= ao_range_code_60xx,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6023] = {
		.name		= "pci-das6023",
		.ai_se_chans	= 16,
		.ai_bits	= 12,
		.ai_speed	= 5000,
		.ao_nchan	= 0,
		.ao_scan_speed	= 100000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_60xx,
		.ao_range_table	= &range_bipolar10,
		.ao_range_code	= ao_range_code_60xx,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS6025] = {
		.name		= "pci-das6025",
		.ai_se_chans	= 16,
		.ai_bits	= 12,
		.ai_speed	= 5000,
		.ao_nchan	= 2,
		.ao_bits	= 12,
		.ao_scan_speed	= 100000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_60xx,
		.ao_range_table	= &range_bipolar10,
		.ao_range_code	= ao_range_code_60xx,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS6030] = {
		.name		= "pci-das6030",
		.ai_se_chans	= 16,
		.ai_bits	= 16,
		.ai_speed	= 10000,
		.ao_nchan	= 2,
		.ao_bits	= 16,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_6030,
		.ao_range_table	= &ao_ranges_6030,
		.ao_range_code	= ao_range_code_6030,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6031] = {
		.name		= "pci-das6031",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 10000,
		.ao_nchan	= 2,
		.ao_bits	= 16,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_6030,
		.ao_range_table	= &ao_ranges_6030,
		.ao_range_code	= ao_range_code_6030,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6032] = {
		.name		= "pci-das6032",
		.ai_se_chans	= 16,
		.ai_bits	= 16,
		.ai_speed	= 10000,
		.ao_nchan	= 0,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_6030,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6033] = {
		.name		= "pci-das6033",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 10000,
		.ao_nchan	= 0,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_6030,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6034] = {
		.name		= "pci-das6034",
		.ai_se_chans	= 16,
		.ai_bits	= 16,
		.ai_speed	= 5000,
		.ao_nchan	= 0,
		.ao_scan_speed	= 0,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_60xx,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6035] = {
		.name		= "pci-das6035",
		.ai_se_chans	= 16,
		.ai_bits	= 16,
		.ai_speed	= 5000,
		.ao_nchan	= 2,
		.ao_bits	= 12,
		.ao_scan_speed	= 100000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_60xx,
		.ao_range_table	= &range_bipolar10,
		.ao_range_code	= ao_range_code_60xx,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6036] = {
		.name		= "pci-das6036",
		.ai_se_chans	= 16,
		.ai_bits	= 16,
		.ai_speed	= 5000,
		.ao_nchan	= 2,
		.ao_bits	= 16,
		.ao_scan_speed	= 100000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_60xx,
		.ao_range_table	= &range_bipolar10,
		.ao_range_code	= ao_range_code_60xx,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6040] = {
		.name		= "pci-das6040",
		.ai_se_chans	= 16,
		.ai_bits	= 12,
		.ai_speed	= 2000,
		.ao_nchan	= 2,
		.ao_bits	= 12,
		.ao_scan_speed	= 1000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_6052,
		.ao_range_table	= &ao_ranges_6030,
		.ao_range_code	= ao_range_code_6030,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6052] = {
		.name		= "pci-das6052",
		.ai_se_chans	= 16,
		.ai_bits	= 16,
		.ai_speed	= 3333,
		.ao_nchan	= 2,
		.ao_bits	= 16,
		.ao_scan_speed	= 3333,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_6052,
		.ao_range_table	= &ao_ranges_6030,
		.ao_range_code	= ao_range_code_6030,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6070] = {
		.name		= "pci-das6070",
		.ai_se_chans	= 16,
		.ai_bits	= 12,
		.ai_speed	= 800,
		.ao_nchan	= 2,
		.ao_bits	= 12,
		.ao_scan_speed	= 1000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_6052,
		.ao_range_table	= &ao_ranges_6030,
		.ao_range_code	= ao_range_code_6030,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS6071] = {
		.name		= "pci-das6071",
		.ai_se_chans	= 64,
		.ai_bits	= 12,
		.ai_speed	= 800,
		.ao_nchan	= 2,
		.ao_bits	= 12,
		.ao_scan_speed	= 1000,
		.layout		= LAYOUT_60XX,
		.ai_range_table	= &ai_ranges_6052,
		.ao_range_table	= &ao_ranges_6030,
		.ao_range_code	= ao_range_code_6030,
		.ai_fifo	= &ai_fifo_60xx,
		.has_8255	= 0,
	},
	[BOARD_PCIDAS4020_12] = {
		.name		= "pci-das4020/12",
		.ai_se_chans	= 4,
		.ai_bits	= 12,
		.ai_speed	= 50,
		.ao_bits	= 12,
		.ao_nchan	= 2,
		.ao_scan_speed	= 0,	/*  no hardware pacing on ao */
		.layout		= LAYOUT_4020,
		.ai_range_table	= &ai_ranges_4020,
		.ao_range_table	= &ao_ranges_4020,
		.ao_range_code	= ao_range_code_4020,
		.ai_fifo	= &ai_fifo_4020,
		.has_8255	= 1,
	},
#if 0
	/*
	 * The device id for these boards is unknown
	 */

	[BOARD_PCIDAS6402_16_JR] = {
		.name		= "pci-das6402/16/jr",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 5000,
		.ao_nchan	= 0,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ai_fifo	= ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS64_M1_16_JR] = {
		.name		= "pci-das64/m1/16/jr",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 1000,
		.ao_nchan	= 0,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ai_fifo	= ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS64_M2_16_JR] = {
		.name = "pci-das64/m2/16/jr",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 500,
		.ao_nchan	= 0,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ai_fifo	= ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS64_M3_16_JR] = {
		.name		= "pci-das64/m3/16/jr",
		.ai_se_chans	= 64,
		.ai_bits	= 16,
		.ai_speed	= 333,
		.ao_nchan	= 0,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ai_fifo	= ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS64_M1_14] = {
		.name		= "pci-das64/m1/14",
		.ai_se_chans	= 64,
		.ai_bits	= 14,
		.ai_speed	= 1000,
		.ao_nchan	= 2,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ai_fifo	= ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS64_M2_14] = {
		.name		= "pci-das64/m2/14",
		.ai_se_chans	= 64,
		.ai_bits	= 14,
		.ai_speed	= 500,
		.ao_nchan	= 2,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ai_fifo	= ai_fifo_64xx,
		.has_8255	= 1,
	},
	[BOARD_PCIDAS64_M3_14] = {
		.name		= "pci-das64/m3/14",
		.ai_se_chans	= 64,
		.ai_bits	= 14,
		.ai_speed	= 333,
		.ao_nchan	= 2,
		.ao_scan_speed	= 10000,
		.layout		= LAYOUT_64XX,
		.ai_range_table	= &ai_ranges_64xx,
		.ai_fifo	= ai_fifo_64xx,
		.has_8255	= 1,
	},
#endif
};

static inline unsigned short se_diff_bit_6xxx(struct comedi_device *dev,
					      int use_differential)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);

	if ((thisboard->layout == LAYOUT_64XX && !use_differential) ||
	    (thisboard->layout == LAYOUT_60XX && use_differential))
		return ADC_SE_DIFF_BIT;

	return 0;
}

struct ext_clock_info {
	/*  master clock divisor to use for scans with external master clock */
	unsigned int divisor;
	/*  chanspec for master clock input when used as scan begin src */
	unsigned int chanspec;
};

/* this structure is for data unique to this hardware driver. */
struct pcidas64_private {
	/*  base addresses (physical) */
	resource_size_t main_phys_iobase;
	resource_size_t dio_counter_phys_iobase;
	/*  base addresses (ioremapped) */
	void __iomem *plx9080_iobase;
	void __iomem *main_iobase;
	/*  local address (used by dma controller) */
	uint32_t local0_iobase;
	uint32_t local1_iobase;
	/*  number of analog input samples remaining */
	unsigned int ai_count;
	/*  dma buffers for analog input */
	uint16_t *ai_buffer[MAX_AI_DMA_RING_COUNT];
	/*  physical addresses of ai dma buffers */
	dma_addr_t ai_buffer_bus_addr[MAX_AI_DMA_RING_COUNT];
	/*  array of ai dma descriptors read by plx9080,
	 *  allocated to get proper alignment */
	struct plx_dma_desc *ai_dma_desc;
	/*  physical address of ai dma descriptor array */
	dma_addr_t ai_dma_desc_bus_addr;
	/*  index of the ai dma descriptor/buffer
	 *  that is currently being used */
	unsigned int ai_dma_index;
	/*  dma buffers for analog output */
	uint16_t *ao_buffer[AO_DMA_RING_COUNT];
	/*  physical addresses of ao dma buffers */
	dma_addr_t ao_buffer_bus_addr[AO_DMA_RING_COUNT];
	struct plx_dma_desc *ao_dma_desc;
	dma_addr_t ao_dma_desc_bus_addr;
	/*  keeps track of buffer where the next ao sample should go */
	unsigned int ao_dma_index;
	/*  number of analog output samples remaining */
	unsigned long ao_count;
	/*  remember what the analog outputs are set to, to allow readback */
	unsigned int ao_value[2];
	unsigned int hw_revision;	/*  stc chip hardware revision number */
	/*  last bits sent to INTR_ENABLE_REG register */
	unsigned int intr_enable_bits;
	/*  last bits sent to ADC_CONTROL1_REG register */
	uint16_t adc_control1_bits;
	/*  last bits sent to FIFO_SIZE_REG register */
	uint16_t fifo_size_bits;
	/*  last bits sent to HW_CONFIG_REG register */
	uint16_t hw_config_bits;
	uint16_t dac_control1_bits;
	/*  last bits written to plx9080 control register */
	uint32_t plx_control_bits;
	/*  last bits written to plx interrupt control and status register */
	uint32_t plx_intcsr_bits;
	/*  index of calibration source readable through ai ch0 */
	int calibration_source;
	/*  bits written to i2c calibration/range register */
	uint8_t i2c_cal_range_bits;
	/*  configure digital triggers to trigger on falling edge */
	unsigned int ext_trig_falling;
	/*  states of various devices stored to enable read-back */
	unsigned int ad8402_state[2];
	unsigned int caldac_state[8];
	short ai_cmd_running;
	unsigned int ai_fifo_segment_length;
	struct ext_clock_info ext_clock;
	unsigned short ao_bounce_buffer[DAC_FIFO_SIZE];
};

static unsigned int ai_range_bits_6xxx(const struct comedi_device *dev,
				       unsigned int range_index)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	const struct comedi_krange *range =
		&thisboard->ai_range_table->range[range_index];
	unsigned int bits = 0;

	switch (range->max) {
	case 10000000:
		bits = 0x000;
		break;
	case 5000000:
		bits = 0x100;
		break;
	case 2000000:
	case 2500000:
		bits = 0x200;
		break;
	case 1000000:
	case 1250000:
		bits = 0x300;
		break;
	case 500000:
		bits = 0x400;
		break;
	case 200000:
	case 250000:
		bits = 0x500;
		break;
	case 100000:
		bits = 0x600;
		break;
	case 50000:
		bits = 0x700;
		break;
	default:
		dev_err(dev->class_dev, "bug! in %s\n", __func__);
		break;
	}
	if (range->min == 0)
		bits += 0x900;
	return bits;
}

static unsigned int hw_revision(const struct comedi_device *dev,
				uint16_t hw_status_bits)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);

	if (thisboard->layout == LAYOUT_4020)
		return (hw_status_bits >> 13) & 0x7;

	return (hw_status_bits >> 12) & 0xf;
}

static void set_dac_range_bits(struct comedi_device *dev,
			       uint16_t *bits, unsigned int channel,
			       unsigned int range)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	unsigned int code = thisboard->ao_range_code[range];

	if (channel > 1)
		dev_err(dev->class_dev, "bug! bad channel?\n");
	if (code & ~0x3)
		dev_err(dev->class_dev, "bug! bad range code?\n");

	*bits &= ~(0x3 << (2 * channel));
	*bits |= code << (2 * channel);
};

static inline int ao_cmd_is_supported(const struct pcidas64_board *board)
{
	return board->ao_nchan && board->layout != LAYOUT_4020;
}

static void abort_dma(struct comedi_device *dev, unsigned int channel)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned long flags;

	/*  spinlock for plx dma control/status reg */
	spin_lock_irqsave(&dev->spinlock, flags);

	plx9080_abort_dma(devpriv->plx9080_iobase, channel);

	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static void disable_plx_interrupts(struct comedi_device *dev)
{
	struct pcidas64_private *devpriv = dev->private;

	devpriv->plx_intcsr_bits = 0;
	writel(devpriv->plx_intcsr_bits,
	       devpriv->plx9080_iobase + PLX_INTRCS_REG);
}

static void disable_ai_interrupts(struct comedi_device *dev)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->intr_enable_bits &=
		~EN_ADC_INTR_SRC_BIT & ~EN_ADC_DONE_INTR_BIT &
		~EN_ADC_ACTIVE_INTR_BIT & ~EN_ADC_STOP_INTR_BIT &
		~EN_ADC_OVERRUN_BIT & ~ADC_INTR_SRC_MASK;
	writew(devpriv->intr_enable_bits,
	       devpriv->main_iobase + INTR_ENABLE_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static void enable_ai_interrupts(struct comedi_device *dev,
				 const struct comedi_cmd *cmd)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	uint32_t bits;
	unsigned long flags;

	bits = EN_ADC_OVERRUN_BIT | EN_ADC_DONE_INTR_BIT |
	       EN_ADC_ACTIVE_INTR_BIT | EN_ADC_STOP_INTR_BIT;
	/*  Use pio transfer and interrupt on end of conversion
	 *  if TRIG_WAKE_EOS flag is set. */
	if (cmd->flags & TRIG_WAKE_EOS) {
		/*  4020 doesn't support pio transfers except for fifo dregs */
		if (thisboard->layout != LAYOUT_4020)
			bits |= ADC_INTR_EOSCAN_BITS | EN_ADC_INTR_SRC_BIT;
	}
	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->intr_enable_bits |= bits;
	writew(devpriv->intr_enable_bits,
	       devpriv->main_iobase + INTR_ENABLE_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);
}

/* initialize plx9080 chip */
static void init_plx9080(struct comedi_device *dev)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	uint32_t bits;
	void __iomem *plx_iobase = devpriv->plx9080_iobase;

	devpriv->plx_control_bits =
		readl(devpriv->plx9080_iobase + PLX_CONTROL_REG);

#ifdef __BIG_ENDIAN
	bits = BIGEND_DMA0 | BIGEND_DMA1;
#else
	bits = 0;
#endif
	writel(bits, devpriv->plx9080_iobase + PLX_BIGEND_REG);

	disable_plx_interrupts(dev);

	abort_dma(dev, 0);
	abort_dma(dev, 1);

	/*  configure dma0 mode */
	bits = 0;
	/*  enable ready input, not sure if this is necessary */
	bits |= PLX_DMA_EN_READYIN_BIT;
	/*  enable bterm, not sure if this is necessary */
	bits |= PLX_EN_BTERM_BIT;
	/*  enable dma chaining */
	bits |= PLX_EN_CHAIN_BIT;
	/*  enable interrupt on dma done
	 *  (probably don't need this, since chain never finishes) */
	bits |= PLX_EN_DMA_DONE_INTR_BIT;
	/*  don't increment local address during transfers
	 *  (we are transferring from a fixed fifo register) */
	bits |= PLX_LOCAL_ADDR_CONST_BIT;
	/*  route dma interrupt to pci bus */
	bits |= PLX_DMA_INTR_PCI_BIT;
	/*  enable demand mode */
	bits |= PLX_DEMAND_MODE_BIT;
	/*  enable local burst mode */
	bits |= PLX_DMA_LOCAL_BURST_EN_BIT;
	/*  4020 uses 32 bit dma */
	if (thisboard->layout == LAYOUT_4020)
		bits |= PLX_LOCAL_BUS_32_WIDE_BITS;
	else		/*  localspace0 bus is 16 bits wide */
		bits |= PLX_LOCAL_BUS_16_WIDE_BITS;
	writel(bits, plx_iobase + PLX_DMA1_MODE_REG);
	if (ao_cmd_is_supported(thisboard))
		writel(bits, plx_iobase + PLX_DMA0_MODE_REG);

	/*  enable interrupts on plx 9080 */
	devpriv->plx_intcsr_bits |=
	    ICS_AERR | ICS_PERR | ICS_PIE | ICS_PLIE | ICS_PAIE | ICS_LIE |
	    ICS_DMA0_E | ICS_DMA1_E;
	writel(devpriv->plx_intcsr_bits,
	       devpriv->plx9080_iobase + PLX_INTRCS_REG);
}

static void disable_ai_pacing(struct comedi_device *dev)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned long flags;

	disable_ai_interrupts(dev);

	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->adc_control1_bits &= ~ADC_SW_GATE_BIT;
	writew(devpriv->adc_control1_bits,
	       devpriv->main_iobase + ADC_CONTROL1_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/* disable pacing, triggering, etc */
	writew(ADC_DMA_DISABLE_BIT | ADC_SOFT_GATE_BITS | ADC_GATE_LEVEL_BIT,
	       devpriv->main_iobase + ADC_CONTROL0_REG);
}

static int set_ai_fifo_segment_length(struct comedi_device *dev,
				      unsigned int num_entries)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	static const int increment_size = 0x100;
	const struct hw_fifo_info *const fifo = thisboard->ai_fifo;
	unsigned int num_increments;
	uint16_t bits;

	if (num_entries < increment_size)
		num_entries = increment_size;
	if (num_entries > fifo->max_segment_length)
		num_entries = fifo->max_segment_length;

	/*  1 == 256 entries, 2 == 512 entries, etc */
	num_increments = (num_entries + increment_size / 2) / increment_size;

	bits = (~(num_increments - 1)) & fifo->fifo_size_reg_mask;
	devpriv->fifo_size_bits &= ~fifo->fifo_size_reg_mask;
	devpriv->fifo_size_bits |= bits;
	writew(devpriv->fifo_size_bits,
	       devpriv->main_iobase + FIFO_SIZE_REG);

	devpriv->ai_fifo_segment_length = num_increments * increment_size;

	return devpriv->ai_fifo_segment_length;
}

/* adjusts the size of hardware fifo (which determines block size for dma xfers) */
static int set_ai_fifo_size(struct comedi_device *dev, unsigned int num_samples)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	unsigned int num_fifo_entries;
	int retval;
	const struct hw_fifo_info *const fifo = thisboard->ai_fifo;

	num_fifo_entries = num_samples / fifo->sample_packing_ratio;

	retval = set_ai_fifo_segment_length(dev,
					    num_fifo_entries /
					    fifo->num_segments);
	if (retval < 0)
		return retval;

	num_samples = retval * fifo->num_segments * fifo->sample_packing_ratio;

	return num_samples;
}

/* query length of fifo */
static unsigned int ai_fifo_size(struct comedi_device *dev)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;

	return devpriv->ai_fifo_segment_length *
	       thisboard->ai_fifo->num_segments *
	       thisboard->ai_fifo->sample_packing_ratio;
}

static void init_stc_registers(struct comedi_device *dev)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	uint16_t bits;
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);

	/*  bit should be set for 6025,
	 *  although docs say boards with <= 16 chans should be cleared XXX */
	if (1)
		devpriv->adc_control1_bits |= ADC_QUEUE_CONFIG_BIT;
	writew(devpriv->adc_control1_bits,
	       devpriv->main_iobase + ADC_CONTROL1_REG);

	/*  6402/16 manual says this register must be initialized to 0xff? */
	writew(0xff, devpriv->main_iobase + ADC_SAMPLE_INTERVAL_UPPER_REG);

	bits = SLOW_DAC_BIT | DMA_CH_SELECT_BIT;
	if (thisboard->layout == LAYOUT_4020)
		bits |= INTERNAL_CLOCK_4020_BITS;
	devpriv->hw_config_bits |= bits;
	writew(devpriv->hw_config_bits,
	       devpriv->main_iobase + HW_CONFIG_REG);

	writew(0, devpriv->main_iobase + DAQ_SYNC_REG);
	writew(0, devpriv->main_iobase + CALIBRATION_REG);

	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  set fifos to maximum size */
	devpriv->fifo_size_bits |= DAC_FIFO_BITS;
	set_ai_fifo_segment_length(dev,
				   thisboard->ai_fifo->max_segment_length);

	devpriv->dac_control1_bits = DAC_OUTPUT_ENABLE_BIT;
	devpriv->intr_enable_bits =
		/* EN_DAC_INTR_SRC_BIT | DAC_INTR_QEMPTY_BITS | */
		EN_DAC_DONE_INTR_BIT | EN_DAC_UNDERRUN_BIT;
	writew(devpriv->intr_enable_bits,
	       devpriv->main_iobase + INTR_ENABLE_REG);

	disable_ai_pacing(dev);
};

static int alloc_and_init_dma_members(struct comedi_device *dev)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct pcidas64_private *devpriv = dev->private;
	int i;

	/*  allocate pci dma buffers */
	for (i = 0; i < ai_dma_ring_count(thisboard); i++) {
		devpriv->ai_buffer[i] =
			pci_alloc_consistent(pcidev, DMA_BUFFER_SIZE,
					     &devpriv->ai_buffer_bus_addr[i]);
		if (devpriv->ai_buffer[i] == NULL)
			return -ENOMEM;

	}
	for (i = 0; i < AO_DMA_RING_COUNT; i++) {
		if (ao_cmd_is_supported(thisboard)) {
			devpriv->ao_buffer[i] =
				pci_alloc_consistent(pcidev, DMA_BUFFER_SIZE,
						     &devpriv->
						      ao_buffer_bus_addr[i]);
			if (devpriv->ao_buffer[i] == NULL)
				return -ENOMEM;

		}
	}
	/*  allocate dma descriptors */
	devpriv->ai_dma_desc =
		pci_alloc_consistent(pcidev, sizeof(struct plx_dma_desc) *
				     ai_dma_ring_count(thisboard),
				     &devpriv->ai_dma_desc_bus_addr);
	if (devpriv->ai_dma_desc == NULL)
		return -ENOMEM;

	if (ao_cmd_is_supported(thisboard)) {
		devpriv->ao_dma_desc =
			pci_alloc_consistent(pcidev,
					     sizeof(struct plx_dma_desc) *
					     AO_DMA_RING_COUNT,
					     &devpriv->ao_dma_desc_bus_addr);
		if (devpriv->ao_dma_desc == NULL)
			return -ENOMEM;
	}
	/*  initialize dma descriptors */
	for (i = 0; i < ai_dma_ring_count(thisboard); i++) {
		devpriv->ai_dma_desc[i].pci_start_addr =
			cpu_to_le32(devpriv->ai_buffer_bus_addr[i]);
		if (thisboard->layout == LAYOUT_4020)
			devpriv->ai_dma_desc[i].local_start_addr =
				cpu_to_le32(devpriv->local1_iobase +
					    ADC_FIFO_REG);
		else
			devpriv->ai_dma_desc[i].local_start_addr =
				cpu_to_le32(devpriv->local0_iobase +
					    ADC_FIFO_REG);
		devpriv->ai_dma_desc[i].transfer_size = cpu_to_le32(0);
		devpriv->ai_dma_desc[i].next =
			cpu_to_le32((devpriv->ai_dma_desc_bus_addr +
				     ((i + 1) % ai_dma_ring_count(thisboard)) *
				     sizeof(devpriv->ai_dma_desc[0])) |
				    PLX_DESC_IN_PCI_BIT | PLX_INTR_TERM_COUNT |
				    PLX_XFER_LOCAL_TO_PCI);
	}
	if (ao_cmd_is_supported(thisboard)) {
		for (i = 0; i < AO_DMA_RING_COUNT; i++) {
			devpriv->ao_dma_desc[i].pci_start_addr =
				cpu_to_le32(devpriv->ao_buffer_bus_addr[i]);
			devpriv->ao_dma_desc[i].local_start_addr =
				cpu_to_le32(devpriv->local0_iobase +
					    DAC_FIFO_REG);
			devpriv->ao_dma_desc[i].transfer_size = cpu_to_le32(0);
			devpriv->ao_dma_desc[i].next =
				cpu_to_le32((devpriv->ao_dma_desc_bus_addr +
					     ((i + 1) % (AO_DMA_RING_COUNT)) *
					     sizeof(devpriv->ao_dma_desc[0])) |
					    PLX_DESC_IN_PCI_BIT |
					    PLX_INTR_TERM_COUNT);
		}
	}
	return 0;
}

static inline void warn_external_queue(struct comedi_device *dev)
{
	dev_err(dev->class_dev,
		"AO command and AI external channel queue cannot be used simultaneously\n");
	dev_err(dev->class_dev,
		"Use internal AI channel queue (channels must be consecutive and use same range/aref)\n");
}

/* Their i2c requires a huge delay on setting clock or data high for some reason */
static const int i2c_high_udelay = 1000;
static const int i2c_low_udelay = 10;

/* set i2c data line high or low */
static void i2c_set_sda(struct comedi_device *dev, int state)
{
	struct pcidas64_private *devpriv = dev->private;
	static const int data_bit = CTL_EE_W;
	void __iomem *plx_control_addr = devpriv->plx9080_iobase +
					 PLX_CONTROL_REG;

	if (state) {
		/*  set data line high */
		devpriv->plx_control_bits &= ~data_bit;
		writel(devpriv->plx_control_bits, plx_control_addr);
		udelay(i2c_high_udelay);
	} else {		/*  set data line low */

		devpriv->plx_control_bits |= data_bit;
		writel(devpriv->plx_control_bits, plx_control_addr);
		udelay(i2c_low_udelay);
	}
}

/* set i2c clock line high or low */
static void i2c_set_scl(struct comedi_device *dev, int state)
{
	struct pcidas64_private *devpriv = dev->private;
	static const int clock_bit = CTL_USERO;
	void __iomem *plx_control_addr = devpriv->plx9080_iobase +
					 PLX_CONTROL_REG;

	if (state) {
		/*  set clock line high */
		devpriv->plx_control_bits &= ~clock_bit;
		writel(devpriv->plx_control_bits, plx_control_addr);
		udelay(i2c_high_udelay);
	} else {		/*  set clock line low */

		devpriv->plx_control_bits |= clock_bit;
		writel(devpriv->plx_control_bits, plx_control_addr);
		udelay(i2c_low_udelay);
	}
}

static void i2c_write_byte(struct comedi_device *dev, uint8_t byte)
{
	uint8_t bit;
	unsigned int num_bits = 8;

	for (bit = 1 << (num_bits - 1); bit; bit >>= 1) {
		i2c_set_scl(dev, 0);
		if ((byte & bit))
			i2c_set_sda(dev, 1);
		else
			i2c_set_sda(dev, 0);
		i2c_set_scl(dev, 1);
	}
}

/* we can't really read the lines, so fake it */
static int i2c_read_ack(struct comedi_device *dev)
{
	i2c_set_scl(dev, 0);
	i2c_set_sda(dev, 1);
	i2c_set_scl(dev, 1);

	return 0;		/*  return fake acknowledge bit */
}

/* send start bit */
static void i2c_start(struct comedi_device *dev)
{
	i2c_set_scl(dev, 1);
	i2c_set_sda(dev, 1);
	i2c_set_sda(dev, 0);
}

/* send stop bit */
static void i2c_stop(struct comedi_device *dev)
{
	i2c_set_scl(dev, 0);
	i2c_set_sda(dev, 0);
	i2c_set_scl(dev, 1);
	i2c_set_sda(dev, 1);
}

static void i2c_write(struct comedi_device *dev, unsigned int address,
		      const uint8_t *data, unsigned int length)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int i;
	uint8_t bitstream;
	static const int read_bit = 0x1;

	/* XXX need mutex to prevent simultaneous attempts to access
	 * eeprom and i2c bus */

	/*  make sure we dont send anything to eeprom */
	devpriv->plx_control_bits &= ~CTL_EE_CS;

	i2c_stop(dev);
	i2c_start(dev);

	/*  send address and write bit */
	bitstream = (address << 1) & ~read_bit;
	i2c_write_byte(dev, bitstream);

	/*  get acknowledge */
	if (i2c_read_ack(dev) != 0) {
		dev_err(dev->class_dev, "%s failed: no acknowledge\n",
			__func__);
		i2c_stop(dev);
		return;
	}
	/*  write data bytes */
	for (i = 0; i < length; i++) {
		i2c_write_byte(dev, data[i]);
		if (i2c_read_ack(dev) != 0) {
			dev_err(dev->class_dev, "%s failed: no acknowledge\n",
				__func__);
			i2c_stop(dev);
			return;
		}
	}
	i2c_stop(dev);
}

static int cb_pcidas64_ai_eoc(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned long context)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	unsigned int status;

	status = readw(devpriv->main_iobase + HW_STATUS_REG);
	if (thisboard->layout == LAYOUT_4020) {
		status = readw(devpriv->main_iobase + ADC_WRITE_PNTR_REG);
		if (status)
			return 0;
	} else {
		if (pipe_full_bits(status))
			return 0;
	}
	return -EBUSY;
}

static int ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
		    struct comedi_insn *insn, unsigned int *data)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	unsigned int bits = 0, n;
	unsigned int channel, range, aref;
	unsigned long flags;
	int ret;

	channel = CR_CHAN(insn->chanspec);
	range = CR_RANGE(insn->chanspec);
	aref = CR_AREF(insn->chanspec);

	/*  disable card's analog input interrupt sources and pacing */
	/*  4020 generates dac done interrupts even though they are disabled */
	disable_ai_pacing(dev);

	spin_lock_irqsave(&dev->spinlock, flags);
	if (insn->chanspec & CR_ALT_FILTER)
		devpriv->adc_control1_bits |= ADC_DITHER_BIT;
	else
		devpriv->adc_control1_bits &= ~ADC_DITHER_BIT;
	writew(devpriv->adc_control1_bits,
	       devpriv->main_iobase + ADC_CONTROL1_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	if (thisboard->layout != LAYOUT_4020) {
		/*  use internal queue */
		devpriv->hw_config_bits &= ~EXT_QUEUE_BIT;
		writew(devpriv->hw_config_bits,
		       devpriv->main_iobase + HW_CONFIG_REG);

		/*  ALT_SOURCE is internal calibration reference */
		if (insn->chanspec & CR_ALT_SOURCE) {
			unsigned int cal_en_bit;

			if (thisboard->layout == LAYOUT_60XX)
				cal_en_bit = CAL_EN_60XX_BIT;
			else
				cal_en_bit = CAL_EN_64XX_BIT;
			/*  select internal reference source to connect
			 *  to channel 0 */
			writew(cal_en_bit |
			       adc_src_bits(devpriv->calibration_source),
			       devpriv->main_iobase + CALIBRATION_REG);
		} else {
			/*  make sure internal calibration source
			 *  is turned off */
			writew(0, devpriv->main_iobase + CALIBRATION_REG);
		}
		/*  load internal queue */
		bits = 0;
		/*  set gain */
		bits |= ai_range_bits_6xxx(dev, CR_RANGE(insn->chanspec));
		/*  set single-ended / differential */
		bits |= se_diff_bit_6xxx(dev, aref == AREF_DIFF);
		if (aref == AREF_COMMON)
			bits |= ADC_COMMON_BIT;
		bits |= adc_chan_bits(channel);
		/*  set stop channel */
		writew(adc_chan_bits(channel),
		       devpriv->main_iobase + ADC_QUEUE_HIGH_REG);
		/*  set start channel, and rest of settings */
		writew(bits, devpriv->main_iobase + ADC_QUEUE_LOAD_REG);
	} else {
		uint8_t old_cal_range_bits = devpriv->i2c_cal_range_bits;

		devpriv->i2c_cal_range_bits &= ~ADC_SRC_4020_MASK;
		if (insn->chanspec & CR_ALT_SOURCE) {
			devpriv->i2c_cal_range_bits |=
				adc_src_4020_bits(devpriv->calibration_source);
		} else {	/* select BNC inputs */
			devpriv->i2c_cal_range_bits |= adc_src_4020_bits(4);
		}
		/*  select range */
		if (range == 0)
			devpriv->i2c_cal_range_bits |= attenuate_bit(channel);
		else
			devpriv->i2c_cal_range_bits &= ~attenuate_bit(channel);
		/*  update calibration/range i2c register only if necessary,
		 *  as it is very slow */
		if (old_cal_range_bits != devpriv->i2c_cal_range_bits) {
			uint8_t i2c_data = devpriv->i2c_cal_range_bits;

			i2c_write(dev, RANGE_CAL_I2C_ADDR, &i2c_data,
				  sizeof(i2c_data));
		}

		/* 4020 manual asks that sample interval register to be set
		 * before writing to convert register.
		 * Using somewhat arbitrary setting of 4 master clock ticks
		 * = 0.1 usec */
		writew(0, devpriv->main_iobase + ADC_SAMPLE_INTERVAL_UPPER_REG);
		writew(2, devpriv->main_iobase + ADC_SAMPLE_INTERVAL_LOWER_REG);
	}

	for (n = 0; n < insn->n; n++) {

		/*  clear adc buffer (inside loop for 4020 sake) */
		writew(0, devpriv->main_iobase + ADC_BUFFER_CLEAR_REG);

		/* trigger conversion, bits sent only matter for 4020 */
		writew(adc_convert_chan_4020_bits(CR_CHAN(insn->chanspec)),
		       devpriv->main_iobase + ADC_CONVERT_REG);

		/*  wait for data */
		ret = comedi_timeout(dev, s, insn, cb_pcidas64_ai_eoc, 0);
		if (ret)
			return ret;

		if (thisboard->layout == LAYOUT_4020)
			data[n] = readl(dev->mmio + ADC_FIFO_REG) & 0xffff;
		else
			data[n] = readw(devpriv->main_iobase + PIPE1_READ_REG);
	}

	return n;
}

static int ai_config_calibration_source(struct comedi_device *dev,
					unsigned int *data)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	unsigned int source = data[1];
	int num_calibration_sources;

	if (thisboard->layout == LAYOUT_60XX)
		num_calibration_sources = 16;
	else
		num_calibration_sources = 8;
	if (source >= num_calibration_sources) {
		dev_dbg(dev->class_dev, "invalid calibration source: %i\n",
			source);
		return -EINVAL;
	}

	devpriv->calibration_source = source;

	return 2;
}

static int ai_config_block_size(struct comedi_device *dev, unsigned int *data)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	int fifo_size;
	const struct hw_fifo_info *const fifo = thisboard->ai_fifo;
	unsigned int block_size, requested_block_size;
	int retval;

	requested_block_size = data[1];

	if (requested_block_size) {
		fifo_size = requested_block_size * fifo->num_segments /
			    bytes_in_sample;

		retval = set_ai_fifo_size(dev, fifo_size);
		if (retval < 0)
			return retval;

	}

	block_size = ai_fifo_size(dev) / fifo->num_segments * bytes_in_sample;

	data[1] = block_size;

	return 2;
}

static int ai_config_master_clock_4020(struct comedi_device *dev,
				       unsigned int *data)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int divisor = data[4];
	int retval = 0;

	if (divisor < 2) {
		divisor = 2;
		retval = -EAGAIN;
	}

	switch (data[1]) {
	case COMEDI_EV_SCAN_BEGIN:
		devpriv->ext_clock.divisor = divisor;
		devpriv->ext_clock.chanspec = data[2];
		break;
	default:
		return -EINVAL;
	}

	data[4] = divisor;

	return retval ? retval : 5;
}

/* XXX could add support for 60xx series */
static int ai_config_master_clock(struct comedi_device *dev, unsigned int *data)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);

	switch (thisboard->layout) {
	case LAYOUT_4020:
		return ai_config_master_clock_4020(dev, data);
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ai_config_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	int id = data[0];

	switch (id) {
	case INSN_CONFIG_ALT_SOURCE:
		return ai_config_calibration_source(dev, data);
	case INSN_CONFIG_BLOCK_SIZE:
		return ai_config_block_size(dev, data);
	case INSN_CONFIG_TIMER_1:
		return ai_config_master_clock(dev, data);
	default:
		return -EINVAL;
	}
	return -EINVAL;
}

/* Gets nearest achievable timing given master clock speed, does not
 * take into account possible minimum/maximum divisor values.  Used
 * by other timing checking functions. */
static unsigned int get_divisor(unsigned int ns, unsigned int flags)
{
	unsigned int divisor;

	switch (flags & TRIG_ROUND_MASK) {
	case TRIG_ROUND_UP:
		divisor = (ns + TIMER_BASE - 1) / TIMER_BASE;
		break;
	case TRIG_ROUND_DOWN:
		divisor = ns / TIMER_BASE;
		break;
	case TRIG_ROUND_NEAREST:
	default:
		divisor = (ns + TIMER_BASE / 2) / TIMER_BASE;
		break;
	}
	return divisor;
}

/* utility function that rounds desired timing to an achievable time, and
 * sets cmd members appropriately.
 * adc paces conversions from master clock by dividing by (x + 3) where x is 24 bit number
 */
static void check_adc_timing(struct comedi_device *dev, struct comedi_cmd *cmd)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	unsigned int convert_divisor = 0, scan_divisor;
	static const int min_convert_divisor = 3;
	static const int max_convert_divisor =
		max_counter_value + min_convert_divisor;
	static const int min_scan_divisor_4020 = 2;
	unsigned long long max_scan_divisor, min_scan_divisor;

	if (cmd->convert_src == TRIG_TIMER) {
		if (thisboard->layout == LAYOUT_4020) {
			cmd->convert_arg = 0;
		} else {
			convert_divisor = get_divisor(cmd->convert_arg,
						      cmd->flags);
			if (convert_divisor > max_convert_divisor)
				convert_divisor = max_convert_divisor;
			if (convert_divisor < min_convert_divisor)
				convert_divisor = min_convert_divisor;
			cmd->convert_arg = convert_divisor * TIMER_BASE;
		}
	} else if (cmd->convert_src == TRIG_NOW) {
		cmd->convert_arg = 0;
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		scan_divisor = get_divisor(cmd->scan_begin_arg, cmd->flags);
		if (cmd->convert_src == TRIG_TIMER) {
			/*  XXX check for integer overflows */
			min_scan_divisor = convert_divisor * cmd->chanlist_len;
			max_scan_divisor =
				(convert_divisor * cmd->chanlist_len - 1) +
				max_counter_value;
		} else {
			min_scan_divisor = min_scan_divisor_4020;
			max_scan_divisor = max_counter_value + min_scan_divisor;
		}
		if (scan_divisor > max_scan_divisor)
			scan_divisor = max_scan_divisor;
		if (scan_divisor < min_scan_divisor)
			scan_divisor = min_scan_divisor;
		cmd->scan_begin_arg = scan_divisor * TIMER_BASE;
	}
}

static int cb_pcidas64_ai_check_chanlist(struct comedi_device *dev,
					 struct comedi_subdevice *s,
					 struct comedi_cmd *cmd)
{
	const struct pcidas64_board *board = comedi_board(dev);
	unsigned int aref0 = CR_AREF(cmd->chanlist[0]);
	int i;

	for (i = 1; i < cmd->chanlist_len; i++) {
		unsigned int aref = CR_AREF(cmd->chanlist[i]);

		if (aref != aref0) {
			dev_dbg(dev->class_dev,
				"all elements in chanlist must use the same analog reference\n");
			return -EINVAL;
		}
	}

	if (board->layout == LAYOUT_4020) {
		unsigned int chan0 = CR_CHAN(cmd->chanlist[0]);

		for (i = 1; i < cmd->chanlist_len; i++) {
			unsigned int chan = CR_CHAN(cmd->chanlist[i]);

			if (chan != (chan0 + i)) {
				dev_dbg(dev->class_dev,
					"chanlist must use consecutive channels\n");
				return -EINVAL;
			}
		}
		if (cmd->chanlist_len == 3) {
			dev_dbg(dev->class_dev,
				"chanlist cannot be 3 channels long, use 1, 2, or 4 channels\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int ai_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
		      struct comedi_cmd *cmd)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	int err = 0;
	unsigned int tmp_arg, tmp_arg2;
	unsigned int triggers;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);

	triggers = TRIG_TIMER;
	if (thisboard->layout == LAYOUT_4020)
		triggers |= TRIG_OTHER;
	else
		triggers |= TRIG_FOLLOW;
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, triggers);

	triggers = TRIG_TIMER;
	if (thisboard->layout == LAYOUT_4020)
		triggers |= TRIG_NOW;
	else
		triggers |= TRIG_EXT;
	err |= cfc_check_trigger_src(&cmd->convert_src, triggers);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src,
				     TRIG_COUNT | TRIG_EXT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (cmd->convert_src == TRIG_EXT && cmd->scan_begin_src == TRIG_TIMER)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	switch (cmd->start_src) {
	case TRIG_NOW:
		err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		/*
		 * start_arg is the CR_CHAN | CR_INVERT of the
		 * external trigger.
		 */
		break;
	}

	if (cmd->convert_src == TRIG_TIMER) {
		if (thisboard->layout == LAYOUT_4020) {
			err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
		} else {
			err |= cfc_check_trigger_arg_min(&cmd->convert_arg,
							 thisboard->ai_speed);
			/* if scans are timed faster than conversion rate allows */
			if (cmd->scan_begin_src == TRIG_TIMER)
				err |= cfc_check_trigger_arg_min(
						&cmd->scan_begin_arg,
						cmd->convert_arg *
						cmd->chanlist_len);
		}
	}

	err |= cfc_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	switch (cmd->stop_src) {
	case TRIG_EXT:
		break;
	case TRIG_COUNT:
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
		break;
	case TRIG_NONE:
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		tmp_arg = cmd->convert_arg;
		tmp_arg2 = cmd->scan_begin_arg;
		check_adc_timing(dev, cmd);
		if (tmp_arg != cmd->convert_arg)
			err++;
		if (tmp_arg2 != cmd->scan_begin_arg)
			err++;
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= cb_pcidas64_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int use_hw_sample_counter(struct comedi_cmd *cmd)
{
/* disable for now until I work out a race */
	return 0;

	if (cmd->stop_src == TRIG_COUNT && cmd->stop_arg <= max_counter_value)
		return 1;

	return 0;
}

static void setup_sample_counters(struct comedi_device *dev,
				  struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;

	if (cmd->stop_src == TRIG_COUNT) {
		/*  set software count */
		devpriv->ai_count = cmd->stop_arg * cmd->chanlist_len;
	}
	/*  load hardware conversion counter */
	if (use_hw_sample_counter(cmd)) {
		writew(cmd->stop_arg & 0xffff,
		       devpriv->main_iobase + ADC_COUNT_LOWER_REG);
		writew((cmd->stop_arg >> 16) & 0xff,
		       devpriv->main_iobase + ADC_COUNT_UPPER_REG);
	} else {
		writew(1, devpriv->main_iobase + ADC_COUNT_LOWER_REG);
	}
}

static inline unsigned int dma_transfer_size(struct comedi_device *dev)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	unsigned int num_samples;

	num_samples = devpriv->ai_fifo_segment_length *
		      thisboard->ai_fifo->sample_packing_ratio;
	if (num_samples > DMA_BUFFER_SIZE / sizeof(uint16_t))
		num_samples = DMA_BUFFER_SIZE / sizeof(uint16_t);

	return num_samples;
}

static uint32_t ai_convert_counter_6xxx(const struct comedi_device *dev,
					const struct comedi_cmd *cmd)
{
	/*  supposed to load counter with desired divisor minus 3 */
	return cmd->convert_arg / TIMER_BASE - 3;
}

static uint32_t ai_scan_counter_6xxx(struct comedi_device *dev,
				     struct comedi_cmd *cmd)
{
	uint32_t count;

	/*  figure out how long we need to delay at end of scan */
	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		count = (cmd->scan_begin_arg -
			 (cmd->convert_arg * (cmd->chanlist_len - 1))) /
			TIMER_BASE;
		break;
	case TRIG_FOLLOW:
		count = cmd->convert_arg / TIMER_BASE;
		break;
	default:
		return 0;
	}
	return count - 3;
}

static uint32_t ai_convert_counter_4020(struct comedi_device *dev,
					struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int divisor;

	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		divisor = cmd->scan_begin_arg / TIMER_BASE;
		break;
	case TRIG_OTHER:
		divisor = devpriv->ext_clock.divisor;
		break;
	default:		/*  should never happen */
		dev_err(dev->class_dev, "bug! failed to set ai pacing!\n");
		divisor = 1000;
		break;
	}

	/*  supposed to load counter with desired divisor minus 2 for 4020 */
	return divisor - 2;
}

static void select_master_clock_4020(struct comedi_device *dev,
				     const struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;

	/*  select internal/external master clock */
	devpriv->hw_config_bits &= ~MASTER_CLOCK_4020_MASK;
	if (cmd->scan_begin_src == TRIG_OTHER) {
		int chanspec = devpriv->ext_clock.chanspec;

		if (CR_CHAN(chanspec))
			devpriv->hw_config_bits |= BNC_CLOCK_4020_BITS;
		else
			devpriv->hw_config_bits |= EXT_CLOCK_4020_BITS;
	} else {
		devpriv->hw_config_bits |= INTERNAL_CLOCK_4020_BITS;
	}
	writew(devpriv->hw_config_bits,
	       devpriv->main_iobase + HW_CONFIG_REG);
}

static void select_master_clock(struct comedi_device *dev,
				const struct comedi_cmd *cmd)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);

	switch (thisboard->layout) {
	case LAYOUT_4020:
		select_master_clock_4020(dev, cmd);
		break;
	default:
		break;
	}
}

static inline void dma_start_sync(struct comedi_device *dev,
				  unsigned int channel)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned long flags;

	/*  spinlock for plx dma control/status reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	if (channel)
		writeb(PLX_DMA_EN_BIT | PLX_DMA_START_BIT |
		       PLX_CLEAR_DMA_INTR_BIT,
		       devpriv->plx9080_iobase + PLX_DMA1_CS_REG);
	else
		writeb(PLX_DMA_EN_BIT | PLX_DMA_START_BIT |
		       PLX_CLEAR_DMA_INTR_BIT,
		       devpriv->plx9080_iobase + PLX_DMA0_CS_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static void set_ai_pacing(struct comedi_device *dev, struct comedi_cmd *cmd)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	uint32_t convert_counter = 0, scan_counter = 0;

	check_adc_timing(dev, cmd);

	select_master_clock(dev, cmd);

	if (thisboard->layout == LAYOUT_4020) {
		convert_counter = ai_convert_counter_4020(dev, cmd);
	} else {
		convert_counter = ai_convert_counter_6xxx(dev, cmd);
		scan_counter = ai_scan_counter_6xxx(dev, cmd);
	}

	/*  load lower 16 bits of convert interval */
	writew(convert_counter & 0xffff,
	       devpriv->main_iobase + ADC_SAMPLE_INTERVAL_LOWER_REG);
	/*  load upper 8 bits of convert interval */
	writew((convert_counter >> 16) & 0xff,
	       devpriv->main_iobase + ADC_SAMPLE_INTERVAL_UPPER_REG);
	/*  load lower 16 bits of scan delay */
	writew(scan_counter & 0xffff,
	       devpriv->main_iobase + ADC_DELAY_INTERVAL_LOWER_REG);
	/*  load upper 8 bits of scan delay */
	writew((scan_counter >> 16) & 0xff,
	       devpriv->main_iobase + ADC_DELAY_INTERVAL_UPPER_REG);
}

static int use_internal_queue_6xxx(const struct comedi_cmd *cmd)
{
	int i;

	for (i = 0; i + 1 < cmd->chanlist_len; i++) {
		if (CR_CHAN(cmd->chanlist[i + 1]) !=
		    CR_CHAN(cmd->chanlist[i]) + 1)
			return 0;
		if (CR_RANGE(cmd->chanlist[i + 1]) !=
		    CR_RANGE(cmd->chanlist[i]))
			return 0;
		if (CR_AREF(cmd->chanlist[i + 1]) != CR_AREF(cmd->chanlist[i]))
			return 0;
	}
	return 1;
}

static int setup_channel_queue(struct comedi_device *dev,
			       const struct comedi_cmd *cmd)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	unsigned short bits;
	int i;

	if (thisboard->layout != LAYOUT_4020) {
		if (use_internal_queue_6xxx(cmd)) {
			devpriv->hw_config_bits &= ~EXT_QUEUE_BIT;
			writew(devpriv->hw_config_bits,
			       devpriv->main_iobase + HW_CONFIG_REG);
			bits = 0;
			/*  set channel */
			bits |= adc_chan_bits(CR_CHAN(cmd->chanlist[0]));
			/*  set gain */
			bits |= ai_range_bits_6xxx(dev,
						   CR_RANGE(cmd->chanlist[0]));
			/*  set single-ended / differential */
			bits |= se_diff_bit_6xxx(dev,
						 CR_AREF(cmd->chanlist[0]) ==
						 AREF_DIFF);
			if (CR_AREF(cmd->chanlist[0]) == AREF_COMMON)
				bits |= ADC_COMMON_BIT;
			/*  set stop channel */
			writew(adc_chan_bits
			       (CR_CHAN(cmd->chanlist[cmd->chanlist_len - 1])),
			       devpriv->main_iobase + ADC_QUEUE_HIGH_REG);
			/*  set start channel, and rest of settings */
			writew(bits,
			       devpriv->main_iobase + ADC_QUEUE_LOAD_REG);
		} else {
			/*  use external queue */
			if (dev->write_subdev && dev->write_subdev->busy) {
				warn_external_queue(dev);
				return -EBUSY;
			}
			devpriv->hw_config_bits |= EXT_QUEUE_BIT;
			writew(devpriv->hw_config_bits,
			       devpriv->main_iobase + HW_CONFIG_REG);
			/*  clear DAC buffer to prevent weird interactions */
			writew(0,
			       devpriv->main_iobase + DAC_BUFFER_CLEAR_REG);
			/*  clear queue pointer */
			writew(0, devpriv->main_iobase + ADC_QUEUE_CLEAR_REG);
			/*  load external queue */
			for (i = 0; i < cmd->chanlist_len; i++) {
				bits = 0;
				/*  set channel */
				bits |= adc_chan_bits(CR_CHAN(cmd->
							      chanlist[i]));
				/*  set gain */
				bits |= ai_range_bits_6xxx(dev,
							   CR_RANGE(cmd->
								    chanlist
								    [i]));
				/*  set single-ended / differential */
				bits |= se_diff_bit_6xxx(dev,
							 CR_AREF(cmd->
								 chanlist[i]) ==
							 AREF_DIFF);
				if (CR_AREF(cmd->chanlist[i]) == AREF_COMMON)
					bits |= ADC_COMMON_BIT;
				/*  mark end of queue */
				if (i == cmd->chanlist_len - 1)
					bits |= QUEUE_EOSCAN_BIT |
						QUEUE_EOSEQ_BIT;
				writew(bits,
				       devpriv->main_iobase +
				       ADC_QUEUE_FIFO_REG);
			}
			/* doing a queue clear is not specified in board docs,
			 * but required for reliable operation */
			writew(0, devpriv->main_iobase + ADC_QUEUE_CLEAR_REG);
			/*  prime queue holding register */
			writew(0, devpriv->main_iobase + ADC_QUEUE_LOAD_REG);
		}
	} else {
		unsigned short old_cal_range_bits = devpriv->i2c_cal_range_bits;

		devpriv->i2c_cal_range_bits &= ~ADC_SRC_4020_MASK;
		/* select BNC inputs */
		devpriv->i2c_cal_range_bits |= adc_src_4020_bits(4);
		/*  select ranges */
		for (i = 0; i < cmd->chanlist_len; i++) {
			unsigned int channel = CR_CHAN(cmd->chanlist[i]);
			unsigned int range = CR_RANGE(cmd->chanlist[i]);

			if (range == 0)
				devpriv->i2c_cal_range_bits |=
					attenuate_bit(channel);
			else
				devpriv->i2c_cal_range_bits &=
					~attenuate_bit(channel);
		}
		/*  update calibration/range i2c register only if necessary,
		 *  as it is very slow */
		if (old_cal_range_bits != devpriv->i2c_cal_range_bits) {
			uint8_t i2c_data = devpriv->i2c_cal_range_bits;

			i2c_write(dev, RANGE_CAL_I2C_ADDR, &i2c_data,
				  sizeof(i2c_data));
		}
	}
	return 0;
}

static inline void load_first_dma_descriptor(struct comedi_device *dev,
					     unsigned int dma_channel,
					     unsigned int descriptor_bits)
{
	struct pcidas64_private *devpriv = dev->private;

	/* The transfer size, pci address, and local address registers
	 * are supposedly unused during chained dma,
	 * but I have found that left over values from last operation
	 * occasionally cause problems with transfer of first dma
	 * block.  Initializing them to zero seems to fix the problem. */
	if (dma_channel) {
		writel(0,
		       devpriv->plx9080_iobase + PLX_DMA1_TRANSFER_SIZE_REG);
		writel(0, devpriv->plx9080_iobase + PLX_DMA1_PCI_ADDRESS_REG);
		writel(0,
		       devpriv->plx9080_iobase + PLX_DMA1_LOCAL_ADDRESS_REG);
		writel(descriptor_bits,
		       devpriv->plx9080_iobase + PLX_DMA1_DESCRIPTOR_REG);
	} else {
		writel(0,
		       devpriv->plx9080_iobase + PLX_DMA0_TRANSFER_SIZE_REG);
		writel(0, devpriv->plx9080_iobase + PLX_DMA0_PCI_ADDRESS_REG);
		writel(0,
		       devpriv->plx9080_iobase + PLX_DMA0_LOCAL_ADDRESS_REG);
		writel(descriptor_bits,
		       devpriv->plx9080_iobase + PLX_DMA0_DESCRIPTOR_REG);
	}
}

static int ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	uint32_t bits;
	unsigned int i;
	unsigned long flags;
	int retval;

	disable_ai_pacing(dev);
	abort_dma(dev, 1);

	retval = setup_channel_queue(dev, cmd);
	if (retval < 0)
		return retval;

	/*  make sure internal calibration source is turned off */
	writew(0, devpriv->main_iobase + CALIBRATION_REG);

	set_ai_pacing(dev, cmd);

	setup_sample_counters(dev, cmd);

	enable_ai_interrupts(dev, cmd);

	spin_lock_irqsave(&dev->spinlock, flags);
	/* set mode, allow conversions through software gate */
	devpriv->adc_control1_bits |= ADC_SW_GATE_BIT;
	devpriv->adc_control1_bits &= ~ADC_DITHER_BIT;
	if (thisboard->layout != LAYOUT_4020) {
		devpriv->adc_control1_bits &= ~ADC_MODE_MASK;
		if (cmd->convert_src == TRIG_EXT)
			/*  good old mode 13 */
			devpriv->adc_control1_bits |= adc_mode_bits(13);
		else
			/*  mode 8.  What else could you need? */
			devpriv->adc_control1_bits |= adc_mode_bits(8);
	} else {
		devpriv->adc_control1_bits &= ~CHANNEL_MODE_4020_MASK;
		if (cmd->chanlist_len == 4)
			devpriv->adc_control1_bits |= FOUR_CHANNEL_4020_BITS;
		else if (cmd->chanlist_len == 2)
			devpriv->adc_control1_bits |= TWO_CHANNEL_4020_BITS;
		devpriv->adc_control1_bits &= ~ADC_LO_CHANNEL_4020_MASK;
		devpriv->adc_control1_bits |=
			adc_lo_chan_4020_bits(CR_CHAN(cmd->chanlist[0]));
		devpriv->adc_control1_bits &= ~ADC_HI_CHANNEL_4020_MASK;
		devpriv->adc_control1_bits |=
			adc_hi_chan_4020_bits(CR_CHAN(cmd->chanlist
						      [cmd->chanlist_len - 1]));
	}
	writew(devpriv->adc_control1_bits,
	       devpriv->main_iobase + ADC_CONTROL1_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  clear adc buffer */
	writew(0, devpriv->main_iobase + ADC_BUFFER_CLEAR_REG);

	if ((cmd->flags & TRIG_WAKE_EOS) == 0 ||
	    thisboard->layout == LAYOUT_4020) {
		devpriv->ai_dma_index = 0;

		/*  set dma transfer size */
		for (i = 0; i < ai_dma_ring_count(thisboard); i++)
			devpriv->ai_dma_desc[i].transfer_size =
				cpu_to_le32(dma_transfer_size(dev) *
					    sizeof(uint16_t));

		/*  give location of first dma descriptor */
		load_first_dma_descriptor(dev, 1,
					  devpriv->ai_dma_desc_bus_addr |
					  PLX_DESC_IN_PCI_BIT |
					  PLX_INTR_TERM_COUNT |
					  PLX_XFER_LOCAL_TO_PCI);

		dma_start_sync(dev, 1);
	}

	if (thisboard->layout == LAYOUT_4020) {
		/* set source for external triggers */
		bits = 0;
		if (cmd->start_src == TRIG_EXT && CR_CHAN(cmd->start_arg))
			bits |= EXT_START_TRIG_BNC_BIT;
		if (cmd->stop_src == TRIG_EXT && CR_CHAN(cmd->stop_arg))
			bits |= EXT_STOP_TRIG_BNC_BIT;
		writew(bits, devpriv->main_iobase + DAQ_ATRIG_LOW_4020_REG);
	}

	spin_lock_irqsave(&dev->spinlock, flags);

	/* enable pacing, triggering, etc */
	bits = ADC_ENABLE_BIT | ADC_SOFT_GATE_BITS | ADC_GATE_LEVEL_BIT;
	if (cmd->flags & TRIG_WAKE_EOS)
		bits |= ADC_DMA_DISABLE_BIT;
	/*  set start trigger */
	if (cmd->start_src == TRIG_EXT) {
		bits |= ADC_START_TRIG_EXT_BITS;
		if (cmd->start_arg & CR_INVERT)
			bits |= ADC_START_TRIG_FALLING_BIT;
	} else if (cmd->start_src == TRIG_NOW)
		bits |= ADC_START_TRIG_SOFT_BITS;
	if (use_hw_sample_counter(cmd))
		bits |= ADC_SAMPLE_COUNTER_EN_BIT;
	writew(bits, devpriv->main_iobase + ADC_CONTROL0_REG);

	devpriv->ai_cmd_running = 1;

	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  start acquisition */
	if (cmd->start_src == TRIG_NOW)
		writew(0, devpriv->main_iobase + ADC_START_REG);

	return 0;
}

/* read num_samples from 16 bit wide ai fifo */
static void pio_drain_ai_fifo_16(struct comedi_device *dev)
{
	struct pcidas64_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int i;
	uint16_t prepost_bits;
	int read_segment, read_index, write_segment, write_index;
	int num_samples;

	do {
		/*  get least significant 15 bits */
		read_index = readw(devpriv->main_iobase + ADC_READ_PNTR_REG) &
			     0x7fff;
		write_index = readw(devpriv->main_iobase + ADC_WRITE_PNTR_REG) &
			      0x7fff;
		/* Get most significant bits (grey code).
		 * Different boards use different code so use a scheme
		 * that doesn't depend on encoding.  This read must
		 * occur after reading least significant 15 bits to avoid race
		 * with fifo switching to next segment. */
		prepost_bits = readw(devpriv->main_iobase + PREPOST_REG);

		/* if read and write pointers are not on the same fifo segment,
		 * read to the end of the read segment */
		read_segment = adc_upper_read_ptr_code(prepost_bits);
		write_segment = adc_upper_write_ptr_code(prepost_bits);

		if (read_segment != write_segment)
			num_samples =
				devpriv->ai_fifo_segment_length - read_index;
		else
			num_samples = write_index - read_index;

		if (cmd->stop_src == TRIG_COUNT) {
			if (devpriv->ai_count == 0)
				break;
			if (num_samples > devpriv->ai_count)
				num_samples = devpriv->ai_count;

			devpriv->ai_count -= num_samples;
		}

		if (num_samples < 0) {
			dev_err(dev->class_dev,
				"cb_pcidas64: bug! num_samples < 0\n");
			break;
		}

		for (i = 0; i < num_samples; i++) {
			cfc_write_to_buffer(s,
					    readw(devpriv->main_iobase +
						  ADC_FIFO_REG));
		}

	} while (read_segment != write_segment);
}

/* Read from 32 bit wide ai fifo of 4020 - deal with insane grey coding of
 * pointers.  The pci-4020 hardware only supports dma transfers (it only
 * supports the use of pio for draining the last remaining points from the
 * fifo when a data acquisition operation has completed).
 */
static void pio_drain_ai_fifo_32(struct comedi_device *dev)
{
	struct pcidas64_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int i;
	unsigned int max_transfer = 100000;
	uint32_t fifo_data;
	int write_code =
		readw(devpriv->main_iobase + ADC_WRITE_PNTR_REG) & 0x7fff;
	int read_code =
		readw(devpriv->main_iobase + ADC_READ_PNTR_REG) & 0x7fff;

	if (cmd->stop_src == TRIG_COUNT) {
		if (max_transfer > devpriv->ai_count)
			max_transfer = devpriv->ai_count;

	}
	for (i = 0; read_code != write_code && i < max_transfer;) {
		fifo_data = readl(dev->mmio + ADC_FIFO_REG);
		cfc_write_to_buffer(s, fifo_data & 0xffff);
		i++;
		if (i < max_transfer) {
			cfc_write_to_buffer(s, (fifo_data >> 16) & 0xffff);
			i++;
		}
		read_code = readw(devpriv->main_iobase + ADC_READ_PNTR_REG) &
			    0x7fff;
	}
	devpriv->ai_count -= i;
}

/* empty fifo */
static void pio_drain_ai_fifo(struct comedi_device *dev)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);

	if (thisboard->layout == LAYOUT_4020)
		pio_drain_ai_fifo_32(dev);
	else
		pio_drain_ai_fifo_16(dev);
}

static void drain_dma_buffers(struct comedi_device *dev, unsigned int channel)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	struct comedi_async *async = dev->read_subdev->async;
	struct comedi_cmd *cmd = &async->cmd;
	uint32_t next_transfer_addr;
	int j;
	int num_samples = 0;
	void __iomem *pci_addr_reg;

	if (channel)
		pci_addr_reg =
		    devpriv->plx9080_iobase + PLX_DMA1_PCI_ADDRESS_REG;
	else
		pci_addr_reg =
		    devpriv->plx9080_iobase + PLX_DMA0_PCI_ADDRESS_REG;

	/*  loop until we have read all the full buffers */
	for (j = 0, next_transfer_addr = readl(pci_addr_reg);
	     (next_transfer_addr <
	      devpriv->ai_buffer_bus_addr[devpriv->ai_dma_index] ||
	      next_transfer_addr >=
	      devpriv->ai_buffer_bus_addr[devpriv->ai_dma_index] +
	      DMA_BUFFER_SIZE) && j < ai_dma_ring_count(thisboard); j++) {
		/*  transfer data from dma buffer to comedi buffer */
		num_samples = dma_transfer_size(dev);
		if (cmd->stop_src == TRIG_COUNT) {
			if (num_samples > devpriv->ai_count)
				num_samples = devpriv->ai_count;
			devpriv->ai_count -= num_samples;
		}
		cfc_write_array_to_buffer(dev->read_subdev,
					  devpriv->ai_buffer[devpriv->
							     ai_dma_index],
					  num_samples * sizeof(uint16_t));
		devpriv->ai_dma_index = (devpriv->ai_dma_index + 1) %
					ai_dma_ring_count(thisboard);
	}
	/* XXX check for dma ring buffer overrun
	 * (use end-of-chain bit to mark last unused buffer) */
}

static void handle_ai_interrupt(struct comedi_device *dev,
				unsigned short status,
				unsigned int plx_status)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	uint8_t dma1_status;
	unsigned long flags;

	/*  check for fifo overrun */
	if (status & ADC_OVERRUN_BIT) {
		dev_err(dev->class_dev, "fifo overrun\n");
		async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
	}
	/*  spin lock makes sure no one else changes plx dma control reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	dma1_status = readb(devpriv->plx9080_iobase + PLX_DMA1_CS_REG);
	if (plx_status & ICS_DMA1_A) {	/*  dma chan 1 interrupt */
		writeb((dma1_status & PLX_DMA_EN_BIT) | PLX_CLEAR_DMA_INTR_BIT,
		       devpriv->plx9080_iobase + PLX_DMA1_CS_REG);

		if (dma1_status & PLX_DMA_EN_BIT)
			drain_dma_buffers(dev, 1);
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/*  drain fifo with pio */
	if ((status & ADC_DONE_BIT) ||
	    ((cmd->flags & TRIG_WAKE_EOS) &&
	     (status & ADC_INTR_PENDING_BIT) &&
	     (thisboard->layout != LAYOUT_4020))) {
		spin_lock_irqsave(&dev->spinlock, flags);
		if (devpriv->ai_cmd_running) {
			spin_unlock_irqrestore(&dev->spinlock, flags);
			pio_drain_ai_fifo(dev);
		} else
			spin_unlock_irqrestore(&dev->spinlock, flags);
	}
	/*  if we are have all the data, then quit */
	if ((cmd->stop_src == TRIG_COUNT && (int)devpriv->ai_count <= 0) ||
	    (cmd->stop_src == TRIG_EXT && (status & ADC_STOP_BIT))) {
		async->events |= COMEDI_CB_EOA;
	}

	cfc_handle_events(dev, s);
}

static inline unsigned int prev_ao_dma_index(struct comedi_device *dev)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int buffer_index;

	if (devpriv->ao_dma_index == 0)
		buffer_index = AO_DMA_RING_COUNT - 1;
	else
		buffer_index = devpriv->ao_dma_index - 1;
	return buffer_index;
}

static int last_ao_dma_load_completed(struct comedi_device *dev)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int buffer_index;
	unsigned int transfer_address;
	unsigned short dma_status;

	buffer_index = prev_ao_dma_index(dev);
	dma_status = readb(devpriv->plx9080_iobase + PLX_DMA0_CS_REG);
	if ((dma_status & PLX_DMA_DONE_BIT) == 0)
		return 0;

	transfer_address =
		readl(devpriv->plx9080_iobase + PLX_DMA0_PCI_ADDRESS_REG);
	if (transfer_address != devpriv->ao_buffer_bus_addr[buffer_index])
		return 0;

	return 1;
}

static int ao_stopped_by_error(struct comedi_device *dev,
			       const struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;

	if (cmd->stop_src == TRIG_NONE)
		return 1;
	if (cmd->stop_src == TRIG_COUNT) {
		if (devpriv->ao_count)
			return 1;
		if (last_ao_dma_load_completed(dev) == 0)
			return 1;
	}
	return 0;
}

static inline int ao_dma_needs_restart(struct comedi_device *dev,
				       unsigned short dma_status)
{
	if ((dma_status & PLX_DMA_DONE_BIT) == 0 ||
	    (dma_status & PLX_DMA_EN_BIT) == 0)
		return 0;
	if (last_ao_dma_load_completed(dev))
		return 0;

	return 1;
}

static void restart_ao_dma(struct comedi_device *dev)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int dma_desc_bits;

	dma_desc_bits =
		readl(devpriv->plx9080_iobase + PLX_DMA0_DESCRIPTOR_REG);
	dma_desc_bits &= ~PLX_END_OF_CHAIN_BIT;
	load_first_dma_descriptor(dev, 0, dma_desc_bits);

	dma_start_sync(dev, 0);
}

static unsigned int load_ao_dma_buffer(struct comedi_device *dev,
				       const struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int num_bytes, buffer_index, prev_buffer_index;
	unsigned int next_bits;

	buffer_index = devpriv->ao_dma_index;
	prev_buffer_index = prev_ao_dma_index(dev);

	num_bytes = comedi_buf_read_n_available(dev->write_subdev);
	if (num_bytes > DMA_BUFFER_SIZE)
		num_bytes = DMA_BUFFER_SIZE;
	if (cmd->stop_src == TRIG_COUNT && num_bytes > devpriv->ao_count)
		num_bytes = devpriv->ao_count;
	num_bytes -= num_bytes % bytes_in_sample;

	if (num_bytes == 0)
		return 0;

	num_bytes = cfc_read_array_from_buffer(dev->write_subdev,
					       devpriv->
					       ao_buffer[buffer_index],
					       num_bytes);
	devpriv->ao_dma_desc[buffer_index].transfer_size =
		cpu_to_le32(num_bytes);
	/* set end of chain bit so we catch underruns */
	next_bits = le32_to_cpu(devpriv->ao_dma_desc[buffer_index].next);
	next_bits |= PLX_END_OF_CHAIN_BIT;
	devpriv->ao_dma_desc[buffer_index].next = cpu_to_le32(next_bits);
	/* clear end of chain bit on previous buffer now that we have set it
	 * for the last buffer */
	next_bits = le32_to_cpu(devpriv->ao_dma_desc[prev_buffer_index].next);
	next_bits &= ~PLX_END_OF_CHAIN_BIT;
	devpriv->ao_dma_desc[prev_buffer_index].next = cpu_to_le32(next_bits);

	devpriv->ao_dma_index = (buffer_index + 1) % AO_DMA_RING_COUNT;
	devpriv->ao_count -= num_bytes;

	return num_bytes;
}

static void load_ao_dma(struct comedi_device *dev, const struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int num_bytes;
	unsigned int next_transfer_addr;
	void __iomem *pci_addr_reg =
		devpriv->plx9080_iobase + PLX_DMA0_PCI_ADDRESS_REG;
	unsigned int buffer_index;

	do {
		buffer_index = devpriv->ao_dma_index;
		/* don't overwrite data that hasn't been transferred yet */
		next_transfer_addr = readl(pci_addr_reg);
		if (next_transfer_addr >=
		    devpriv->ao_buffer_bus_addr[buffer_index] &&
		    next_transfer_addr <
		    devpriv->ao_buffer_bus_addr[buffer_index] +
		    DMA_BUFFER_SIZE)
			return;
		num_bytes = load_ao_dma_buffer(dev, cmd);
	} while (num_bytes >= DMA_BUFFER_SIZE);
}

static void handle_ao_interrupt(struct comedi_device *dev,
				unsigned short status, unsigned int plx_status)
{
	struct pcidas64_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->write_subdev;
	struct comedi_async *async;
	struct comedi_cmd *cmd;
	uint8_t dma0_status;
	unsigned long flags;

	/* board might not support ao, in which case write_subdev is NULL */
	if (s == NULL)
		return;
	async = s->async;
	cmd = &async->cmd;

	/*  spin lock makes sure no one else changes plx dma control reg */
	spin_lock_irqsave(&dev->spinlock, flags);
	dma0_status = readb(devpriv->plx9080_iobase + PLX_DMA0_CS_REG);
	if (plx_status & ICS_DMA0_A) {	/*  dma chan 0 interrupt */
		if ((dma0_status & PLX_DMA_EN_BIT) &&
		    !(dma0_status & PLX_DMA_DONE_BIT))
			writeb(PLX_DMA_EN_BIT | PLX_CLEAR_DMA_INTR_BIT,
			       devpriv->plx9080_iobase + PLX_DMA0_CS_REG);
		else
			writeb(PLX_CLEAR_DMA_INTR_BIT,
			       devpriv->plx9080_iobase + PLX_DMA0_CS_REG);
		spin_unlock_irqrestore(&dev->spinlock, flags);
		if (dma0_status & PLX_DMA_EN_BIT) {
			load_ao_dma(dev, cmd);
			/* try to recover from dma end-of-chain event */
			if (ao_dma_needs_restart(dev, dma0_status))
				restart_ao_dma(dev);
		}
	} else {
		spin_unlock_irqrestore(&dev->spinlock, flags);
	}

	if ((status & DAC_DONE_BIT)) {
		async->events |= COMEDI_CB_EOA;
		if (ao_stopped_by_error(dev, cmd))
			async->events |= COMEDI_CB_ERROR;
	}
	cfc_handle_events(dev, s);
}

static irqreturn_t handle_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pcidas64_private *devpriv = dev->private;
	unsigned short status;
	uint32_t plx_status;
	uint32_t plx_bits;

	plx_status = readl(devpriv->plx9080_iobase + PLX_INTRCS_REG);
	status = readw(devpriv->main_iobase + HW_STATUS_REG);

	/* an interrupt before all the postconfig stuff gets done could
	 * cause a NULL dereference if we continue through the
	 * interrupt handler */
	if (!dev->attached)
		return IRQ_HANDLED;

	handle_ai_interrupt(dev, status, plx_status);
	handle_ao_interrupt(dev, status, plx_status);

	/*  clear possible plx9080 interrupt sources */
	if (plx_status & ICS_LDIA) {	/*  clear local doorbell interrupt */
		plx_bits = readl(devpriv->plx9080_iobase + PLX_DBR_OUT_REG);
		writel(plx_bits, devpriv->plx9080_iobase + PLX_DBR_OUT_REG);
	}

	return IRQ_HANDLED;
}

static int ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);
	if (devpriv->ai_cmd_running == 0) {
		spin_unlock_irqrestore(&dev->spinlock, flags);
		return 0;
	}
	devpriv->ai_cmd_running = 0;
	spin_unlock_irqrestore(&dev->spinlock, flags);

	disable_ai_pacing(dev);

	abort_dma(dev, 1);

	return 0;
}

static int ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
		    struct comedi_insn *insn, unsigned int *data)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	int range = CR_RANGE(insn->chanspec);

	/*  do some initializing */
	writew(0, devpriv->main_iobase + DAC_CONTROL0_REG);

	/*  set range */
	set_dac_range_bits(dev, &devpriv->dac_control1_bits, chan, range);
	writew(devpriv->dac_control1_bits,
	       devpriv->main_iobase + DAC_CONTROL1_REG);

	/*  write to channel */
	if (thisboard->layout == LAYOUT_4020) {
		writew(data[0] & 0xff,
		       devpriv->main_iobase + dac_lsb_4020_reg(chan));
		writew((data[0] >> 8) & 0xf,
		       devpriv->main_iobase + dac_msb_4020_reg(chan));
	} else {
		writew(data[0], devpriv->main_iobase + dac_convert_reg(chan));
	}

	/*  remember output value */
	devpriv->ao_value[chan] = data[0];

	return 1;
}

static int ao_readback_insn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct pcidas64_private *devpriv = dev->private;

	data[0] = devpriv->ao_value[CR_CHAN(insn->chanspec)];

	return 1;
}

static void set_dac_control0_reg(struct comedi_device *dev,
				 const struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int bits = DAC_ENABLE_BIT | WAVEFORM_GATE_LEVEL_BIT |
			    WAVEFORM_GATE_ENABLE_BIT | WAVEFORM_GATE_SELECT_BIT;

	if (cmd->start_src == TRIG_EXT) {
		bits |= WAVEFORM_TRIG_EXT_BITS;
		if (cmd->start_arg & CR_INVERT)
			bits |= WAVEFORM_TRIG_FALLING_BIT;
	} else {
		bits |= WAVEFORM_TRIG_SOFT_BITS;
	}
	if (cmd->scan_begin_src == TRIG_EXT) {
		bits |= DAC_EXT_UPDATE_ENABLE_BIT;
		if (cmd->scan_begin_arg & CR_INVERT)
			bits |= DAC_EXT_UPDATE_FALLING_BIT;
	}
	writew(bits, devpriv->main_iobase + DAC_CONTROL0_REG);
}

static void set_dac_control1_reg(struct comedi_device *dev,
				 const struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;
	int i;

	for (i = 0; i < cmd->chanlist_len; i++) {
		int channel, range;

		channel = CR_CHAN(cmd->chanlist[i]);
		range = CR_RANGE(cmd->chanlist[i]);
		set_dac_range_bits(dev, &devpriv->dac_control1_bits, channel,
				   range);
	}
	devpriv->dac_control1_bits |= DAC_SW_GATE_BIT;
	writew(devpriv->dac_control1_bits,
	       devpriv->main_iobase + DAC_CONTROL1_REG);
}

static void set_dac_select_reg(struct comedi_device *dev,
			       const struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;
	uint16_t bits;
	unsigned int first_channel, last_channel;

	first_channel = CR_CHAN(cmd->chanlist[0]);
	last_channel = CR_CHAN(cmd->chanlist[cmd->chanlist_len - 1]);
	if (last_channel < first_channel)
		dev_err(dev->class_dev,
			"bug! last ao channel < first ao channel\n");

	bits = (first_channel & 0x7) | (last_channel & 0x7) << 3;

	writew(bits, devpriv->main_iobase + DAC_SELECT_REG);
}

static unsigned int get_ao_divisor(unsigned int ns, unsigned int flags)
{
	return get_divisor(ns, flags) - 2;
}

static void set_dac_interval_regs(struct comedi_device *dev,
				  const struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int divisor;

	if (cmd->scan_begin_src != TRIG_TIMER)
		return;

	divisor = get_ao_divisor(cmd->scan_begin_arg, cmd->flags);
	if (divisor > max_counter_value) {
		dev_err(dev->class_dev, "bug! ao divisor too big\n");
		divisor = max_counter_value;
	}
	writew(divisor & 0xffff,
	       devpriv->main_iobase + DAC_SAMPLE_INTERVAL_LOWER_REG);
	writew((divisor >> 16) & 0xff,
	       devpriv->main_iobase + DAC_SAMPLE_INTERVAL_UPPER_REG);
}

static int prep_ao_dma(struct comedi_device *dev, const struct comedi_cmd *cmd)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int num_bytes;
	int i;

	/* clear queue pointer too, since external queue has
	 * weird interactions with ao fifo */
	writew(0, devpriv->main_iobase + ADC_QUEUE_CLEAR_REG);
	writew(0, devpriv->main_iobase + DAC_BUFFER_CLEAR_REG);

	num_bytes = (DAC_FIFO_SIZE / 2) * bytes_in_sample;
	if (cmd->stop_src == TRIG_COUNT &&
	    num_bytes / bytes_in_sample > devpriv->ao_count)
		num_bytes = devpriv->ao_count * bytes_in_sample;
	num_bytes = cfc_read_array_from_buffer(dev->write_subdev,
					       devpriv->ao_bounce_buffer,
					       num_bytes);
	for (i = 0; i < num_bytes / bytes_in_sample; i++) {
		writew(devpriv->ao_bounce_buffer[i],
		       devpriv->main_iobase + DAC_FIFO_REG);
	}
	devpriv->ao_count -= num_bytes / bytes_in_sample;
	if (cmd->stop_src == TRIG_COUNT && devpriv->ao_count == 0)
		return 0;
	num_bytes = load_ao_dma_buffer(dev, cmd);
	if (num_bytes == 0)
		return -1;
	load_ao_dma(dev, cmd);

	dma_start_sync(dev, 0);

	return 0;
}

static inline int external_ai_queue_in_use(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   struct comedi_cmd *cmd)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);

	if (s->busy)
		return 0;
	if (thisboard->layout == LAYOUT_4020)
		return 0;
	else if (use_internal_queue_6xxx(cmd))
		return 0;
	return 1;
}

static int ao_inttrig(struct comedi_device *dev, struct comedi_subdevice *s,
		      unsigned int trig_num)
{
	struct pcidas64_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int retval;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	retval = prep_ao_dma(dev, cmd);
	if (retval < 0)
		return -EPIPE;

	set_dac_control0_reg(dev, cmd);

	if (cmd->start_src == TRIG_INT)
		writew(0, devpriv->main_iobase + DAC_START_REG);

	s->async->inttrig = NULL;

	return 0;
}

static int ao_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcidas64_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	if (external_ai_queue_in_use(dev, s, cmd)) {
		warn_external_queue(dev);
		return -EBUSY;
	}
	/* disable analog output system during setup */
	writew(0x0, devpriv->main_iobase + DAC_CONTROL0_REG);

	devpriv->ao_dma_index = 0;
	devpriv->ao_count = cmd->stop_arg * cmd->chanlist_len;

	set_dac_select_reg(dev, cmd);
	set_dac_interval_regs(dev, cmd);
	load_first_dma_descriptor(dev, 0, devpriv->ao_dma_desc_bus_addr |
				  PLX_DESC_IN_PCI_BIT | PLX_INTR_TERM_COUNT);

	set_dac_control1_reg(dev, cmd);
	s->async->inttrig = ao_inttrig;

	return 0;
}

static int cb_pcidas64_ao_check_chanlist(struct comedi_device *dev,
					 struct comedi_subdevice *s,
					 struct comedi_cmd *cmd)
{
	unsigned int chan0 = CR_CHAN(cmd->chanlist[0]);
	int i;

	for (i = 1; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);

		if (chan != (chan0 + i)) {
			dev_dbg(dev->class_dev,
				"chanlist must use consecutive channels\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int ao_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
		      struct comedi_cmd *cmd)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	int err = 0;
	unsigned int tmp_arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_INT | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
				     TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);

	/* Step 2b : and mutually compatible */

	if (cmd->convert_src == TRIG_EXT && cmd->scan_begin_src == TRIG_TIMER)
		err |= -EINVAL;
	if (cmd->stop_src != TRIG_COUNT &&
	    cmd->stop_src != TRIG_NONE && cmd->stop_src != TRIG_EXT)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						 thisboard->ao_scan_speed);
		if (get_ao_divisor(cmd->scan_begin_arg, cmd->flags) >
		    max_counter_value) {
			cmd->scan_begin_arg = (max_counter_value + 2) *
					      TIMER_BASE;
			err |= -EINVAL;
		}
	}

	err |= cfc_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp_arg = cmd->scan_begin_arg;
		cmd->scan_begin_arg = get_divisor(cmd->scan_begin_arg,
						  cmd->flags) * TIMER_BASE;
		if (tmp_arg != cmd->scan_begin_arg)
			err++;
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= cb_pcidas64_ao_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int ao_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcidas64_private *devpriv = dev->private;

	writew(0x0, devpriv->main_iobase + DAC_CONTROL0_REG);
	abort_dma(dev, 0);
	return 0;
}

static int dio_callback(int dir, int port, int data, unsigned long arg)
{
	void __iomem *iobase = (void __iomem *)arg;

	if (dir) {
		writeb(data, iobase + port);
		return 0;
	}
	return readb(iobase + port);
}

static int dio_callback_4020(int dir, int port, int data, unsigned long arg)
{
	void __iomem *iobase = (void __iomem *)arg;

	if (dir) {
		writew(data, iobase + 2 * port);
		return 0;
	}
	return readw(iobase + 2 * port);
}

static int di_rbits(struct comedi_device *dev, struct comedi_subdevice *s,
		    struct comedi_insn *insn, unsigned int *data)
{
	unsigned int bits;

	bits = readb(dev->mmio + DI_REG);
	bits &= 0xf;
	data[1] = bits;
	data[0] = 0;

	return insn->n;
}

static int do_wbits(struct comedi_device *dev,
		    struct comedi_subdevice *s,
		    struct comedi_insn *insn,
		    unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		writeb(s->state, dev->mmio + DO_REG);

	data[1] = s->state;

	return insn->n;
}

static int dio_60xx_config_insn(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	writeb(s->io_bits, dev->mmio + DIO_DIRECTION_60XX_REG);

	return insn->n;
}

static int dio_60xx_wbits(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		writeb(s->state, dev->mmio + DIO_DATA_60XX_REG);

	data[1] = readb(dev->mmio + DIO_DATA_60XX_REG);

	return insn->n;
}

/* pci-6025 8800 caldac:
 * address 0 == dac channel 0 offset
 * address 1 == dac channel 0 gain
 * address 2 == dac channel 1 offset
 * address 3 == dac channel 1 gain
 * address 4 == fine adc offset
 * address 5 == coarse adc offset
 * address 6 == coarse adc gain
 * address 7 == fine adc gain
 */
/* pci-6402/16 uses all 8 channels for dac:
 * address 0 == dac channel 0 fine gain
 * address 1 == dac channel 0 coarse gain
 * address 2 == dac channel 0 coarse offset
 * address 3 == dac channel 1 coarse offset
 * address 4 == dac channel 1 fine gain
 * address 5 == dac channel 1 coarse gain
 * address 6 == dac channel 0 fine offset
 * address 7 == dac channel 1 fine offset
*/

static int caldac_8800_write(struct comedi_device *dev, unsigned int address,
			     uint8_t value)
{
	struct pcidas64_private *devpriv = dev->private;
	static const int num_caldac_channels = 8;
	static const int bitstream_length = 11;
	unsigned int bitstream = ((address & 0x7) << 8) | value;
	unsigned int bit, register_bits;
	static const int caldac_8800_udelay = 1;

	if (address >= num_caldac_channels) {
		dev_err(dev->class_dev, "illegal caldac channel\n");
		return -1;
	}
	for (bit = 1 << (bitstream_length - 1); bit; bit >>= 1) {
		register_bits = 0;
		if (bitstream & bit)
			register_bits |= SERIAL_DATA_IN_BIT;
		udelay(caldac_8800_udelay);
		writew(register_bits, devpriv->main_iobase + CALIBRATION_REG);
		register_bits |= SERIAL_CLOCK_BIT;
		udelay(caldac_8800_udelay);
		writew(register_bits, devpriv->main_iobase + CALIBRATION_REG);
	}
	udelay(caldac_8800_udelay);
	writew(SELECT_8800_BIT, devpriv->main_iobase + CALIBRATION_REG);
	udelay(caldac_8800_udelay);
	writew(0, devpriv->main_iobase + CALIBRATION_REG);
	udelay(caldac_8800_udelay);
	return 0;
}

/* 4020 caldacs */
static int caldac_i2c_write(struct comedi_device *dev,
			    unsigned int caldac_channel, unsigned int value)
{
	uint8_t serial_bytes[3];
	uint8_t i2c_addr;
	enum pointer_bits {
		/*  manual has gain and offset bits switched */
		OFFSET_0_2 = 0x1,
		GAIN_0_2 = 0x2,
		OFFSET_1_3 = 0x4,
		GAIN_1_3 = 0x8,
	};
	enum data_bits {
		NOT_CLEAR_REGISTERS = 0x20,
	};

	switch (caldac_channel) {
	case 0:		/*  chan 0 offset */
		i2c_addr = CALDAC0_I2C_ADDR;
		serial_bytes[0] = OFFSET_0_2;
		break;
	case 1:		/*  chan 1 offset */
		i2c_addr = CALDAC0_I2C_ADDR;
		serial_bytes[0] = OFFSET_1_3;
		break;
	case 2:		/*  chan 2 offset */
		i2c_addr = CALDAC1_I2C_ADDR;
		serial_bytes[0] = OFFSET_0_2;
		break;
	case 3:		/*  chan 3 offset */
		i2c_addr = CALDAC1_I2C_ADDR;
		serial_bytes[0] = OFFSET_1_3;
		break;
	case 4:		/*  chan 0 gain */
		i2c_addr = CALDAC0_I2C_ADDR;
		serial_bytes[0] = GAIN_0_2;
		break;
	case 5:		/*  chan 1 gain */
		i2c_addr = CALDAC0_I2C_ADDR;
		serial_bytes[0] = GAIN_1_3;
		break;
	case 6:		/*  chan 2 gain */
		i2c_addr = CALDAC1_I2C_ADDR;
		serial_bytes[0] = GAIN_0_2;
		break;
	case 7:		/*  chan 3 gain */
		i2c_addr = CALDAC1_I2C_ADDR;
		serial_bytes[0] = GAIN_1_3;
		break;
	default:
		dev_err(dev->class_dev, "invalid caldac channel\n");
		return -1;
	}
	serial_bytes[1] = NOT_CLEAR_REGISTERS | ((value >> 8) & 0xf);
	serial_bytes[2] = value & 0xff;
	i2c_write(dev, i2c_addr, serial_bytes, 3);
	return 0;
}

static void caldac_write(struct comedi_device *dev, unsigned int channel,
			 unsigned int value)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;

	devpriv->caldac_state[channel] = value;

	switch (thisboard->layout) {
	case LAYOUT_60XX:
	case LAYOUT_64XX:
		caldac_8800_write(dev, channel, value);
		break;
	case LAYOUT_4020:
		caldac_i2c_write(dev, channel, value);
		break;
	default:
		break;
	}
}

static int calib_write_insn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct pcidas64_private *devpriv = dev->private;
	int channel = CR_CHAN(insn->chanspec);

	/* return immediately if setting hasn't changed, since
	 * programming these things is slow */
	if (devpriv->caldac_state[channel] == data[0])
		return 1;

	caldac_write(dev, channel, data[0]);

	return 1;
}

static int calib_read_insn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int channel = CR_CHAN(insn->chanspec);

	data[0] = devpriv->caldac_state[channel];

	return 1;
}

static void ad8402_write(struct comedi_device *dev, unsigned int channel,
			 unsigned int value)
{
	struct pcidas64_private *devpriv = dev->private;
	static const int bitstream_length = 10;
	unsigned int bit, register_bits;
	unsigned int bitstream = ((channel & 0x3) << 8) | (value & 0xff);
	static const int ad8402_udelay = 1;

	devpriv->ad8402_state[channel] = value;

	register_bits = SELECT_8402_64XX_BIT;
	udelay(ad8402_udelay);
	writew(register_bits, devpriv->main_iobase + CALIBRATION_REG);

	for (bit = 1 << (bitstream_length - 1); bit; bit >>= 1) {
		if (bitstream & bit)
			register_bits |= SERIAL_DATA_IN_BIT;
		else
			register_bits &= ~SERIAL_DATA_IN_BIT;
		udelay(ad8402_udelay);
		writew(register_bits, devpriv->main_iobase + CALIBRATION_REG);
		udelay(ad8402_udelay);
		writew(register_bits | SERIAL_CLOCK_BIT,
		       devpriv->main_iobase + CALIBRATION_REG);
	}

	udelay(ad8402_udelay);
	writew(0, devpriv->main_iobase + CALIBRATION_REG);
}

/* for pci-das6402/16, channel 0 is analog input gain and channel 1 is offset */
static int ad8402_write_insn(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	struct pcidas64_private *devpriv = dev->private;
	int channel = CR_CHAN(insn->chanspec);

	/* return immediately if setting hasn't changed, since
	 * programming these things is slow */
	if (devpriv->ad8402_state[channel] == data[0])
		return 1;

	devpriv->ad8402_state[channel] = data[0];

	ad8402_write(dev, channel, data[0]);

	return 1;
}

static int ad8402_read_insn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct pcidas64_private *devpriv = dev->private;
	unsigned int channel = CR_CHAN(insn->chanspec);

	data[0] = devpriv->ad8402_state[channel];

	return 1;
}

static uint16_t read_eeprom(struct comedi_device *dev, uint8_t address)
{
	struct pcidas64_private *devpriv = dev->private;
	static const int bitstream_length = 11;
	static const int read_command = 0x6;
	unsigned int bitstream = (read_command << 8) | address;
	unsigned int bit;
	void __iomem * const plx_control_addr =
		devpriv->plx9080_iobase + PLX_CONTROL_REG;
	uint16_t value;
	static const int value_length = 16;
	static const int eeprom_udelay = 1;

	udelay(eeprom_udelay);
	devpriv->plx_control_bits &= ~CTL_EE_CLK & ~CTL_EE_CS;
	/*  make sure we don't send anything to the i2c bus on 4020 */
	devpriv->plx_control_bits |= CTL_USERO;
	writel(devpriv->plx_control_bits, plx_control_addr);
	/*  activate serial eeprom */
	udelay(eeprom_udelay);
	devpriv->plx_control_bits |= CTL_EE_CS;
	writel(devpriv->plx_control_bits, plx_control_addr);

	/*  write read command and desired memory address */
	for (bit = 1 << (bitstream_length - 1); bit; bit >>= 1) {
		/*  set bit to be written */
		udelay(eeprom_udelay);
		if (bitstream & bit)
			devpriv->plx_control_bits |= CTL_EE_W;
		else
			devpriv->plx_control_bits &= ~CTL_EE_W;
		writel(devpriv->plx_control_bits, plx_control_addr);
		/*  clock in bit */
		udelay(eeprom_udelay);
		devpriv->plx_control_bits |= CTL_EE_CLK;
		writel(devpriv->plx_control_bits, plx_control_addr);
		udelay(eeprom_udelay);
		devpriv->plx_control_bits &= ~CTL_EE_CLK;
		writel(devpriv->plx_control_bits, plx_control_addr);
	}
	/*  read back value from eeprom memory location */
	value = 0;
	for (bit = 1 << (value_length - 1); bit; bit >>= 1) {
		/*  clock out bit */
		udelay(eeprom_udelay);
		devpriv->plx_control_bits |= CTL_EE_CLK;
		writel(devpriv->plx_control_bits, plx_control_addr);
		udelay(eeprom_udelay);
		devpriv->plx_control_bits &= ~CTL_EE_CLK;
		writel(devpriv->plx_control_bits, plx_control_addr);
		udelay(eeprom_udelay);
		if (readl(plx_control_addr) & CTL_EE_R)
			value |= bit;
	}

	/*  deactivate eeprom serial input */
	udelay(eeprom_udelay);
	devpriv->plx_control_bits &= ~CTL_EE_CS;
	writel(devpriv->plx_control_bits, plx_control_addr);

	return value;
}

static int eeprom_read_insn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	data[0] = read_eeprom(dev, CR_CHAN(insn->chanspec));

	return 1;
}

/* Allocate and initialize the subdevice structures.
 */
static int setup_subdevices(struct comedi_device *dev)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pcidas64_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	void __iomem *dio_8255_iobase;
	int i;
	int ret;

	ret = comedi_alloc_subdevices(dev, 10);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog input subdevice */
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_DITHER | SDF_CMD_READ;
	if (thisboard->layout == LAYOUT_60XX)
		s->subdev_flags |= SDF_COMMON | SDF_DIFF;
	else if (thisboard->layout == LAYOUT_64XX)
		s->subdev_flags |= SDF_DIFF;
	/* XXX Number of inputs in differential mode is ignored */
	s->n_chan = thisboard->ai_se_chans;
	s->len_chanlist = 0x2000;
	s->maxdata = (1 << thisboard->ai_bits) - 1;
	s->range_table = thisboard->ai_range_table;
	s->insn_read = ai_rinsn;
	s->insn_config = ai_config_insn;
	s->do_cmd = ai_cmd;
	s->do_cmdtest = ai_cmdtest;
	s->cancel = ai_cancel;
	if (thisboard->layout == LAYOUT_4020) {
		uint8_t data;
		/*  set adc to read from inputs
		 *  (not internal calibration sources) */
		devpriv->i2c_cal_range_bits = adc_src_4020_bits(4);
		/*  set channels to +-5 volt input ranges */
		for (i = 0; i < s->n_chan; i++)
			devpriv->i2c_cal_range_bits |= attenuate_bit(i);
		data = devpriv->i2c_cal_range_bits;
		i2c_write(dev, RANGE_CAL_I2C_ADDR, &data, sizeof(data));
	}

	/* analog output subdevice */
	s = &dev->subdevices[1];
	if (thisboard->ao_nchan) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE |
				  SDF_GROUND | SDF_CMD_WRITE;
		s->n_chan = thisboard->ao_nchan;
		s->maxdata = (1 << thisboard->ao_bits) - 1;
		s->range_table = thisboard->ao_range_table;
		s->insn_read = ao_readback_insn;
		s->insn_write = ao_winsn;
		if (ao_cmd_is_supported(thisboard)) {
			dev->write_subdev = s;
			s->do_cmdtest = ao_cmdtest;
			s->do_cmd = ao_cmd;
			s->len_chanlist = thisboard->ao_nchan;
			s->cancel = ao_cancel;
		}
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  digital input */
	s = &dev->subdevices[2];
	if (thisboard->layout == LAYOUT_64XX) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE;
		s->n_chan = 4;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = di_rbits;
	} else
		s->type = COMEDI_SUBD_UNUSED;

	/*  digital output */
	if (thisboard->layout == LAYOUT_64XX) {
		s = &dev->subdevices[3];
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
		s->n_chan = 4;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = do_wbits;
	} else
		s->type = COMEDI_SUBD_UNUSED;

	/* 8255 */
	s = &dev->subdevices[4];
	if (thisboard->has_8255) {
		if (thisboard->layout == LAYOUT_4020) {
			dio_8255_iobase = devpriv->main_iobase + I8255_4020_REG;
			ret = subdev_8255_init(dev, s, dio_callback_4020,
					       (unsigned long)dio_8255_iobase);
		} else {
			dio_8255_iobase = dev->mmio + DIO_8255_OFFSET;
			ret = subdev_8255_init(dev, s, dio_callback,
					       (unsigned long)dio_8255_iobase);
		}
		if (ret)
			return ret;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  8 channel dio for 60xx */
	s = &dev->subdevices[5];
	if (thisboard->layout == LAYOUT_60XX) {
		s->type = COMEDI_SUBD_DIO;
		s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
		s->n_chan = 8;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_config = dio_60xx_config_insn;
		s->insn_bits = dio_60xx_wbits;
	} else
		s->type = COMEDI_SUBD_UNUSED;

	/*  caldac */
	s = &dev->subdevices[6];
	s->type = COMEDI_SUBD_CALIB;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
	s->n_chan = 8;
	if (thisboard->layout == LAYOUT_4020)
		s->maxdata = 0xfff;
	else
		s->maxdata = 0xff;
	s->insn_read = calib_read_insn;
	s->insn_write = calib_write_insn;
	for (i = 0; i < s->n_chan; i++)
		caldac_write(dev, i, s->maxdata / 2);

	/*  2 channel ad8402 potentiometer */
	s = &dev->subdevices[7];
	if (thisboard->layout == LAYOUT_64XX) {
		s->type = COMEDI_SUBD_CALIB;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
		s->n_chan = 2;
		s->insn_read = ad8402_read_insn;
		s->insn_write = ad8402_write_insn;
		s->maxdata = 0xff;
		for (i = 0; i < s->n_chan; i++)
			ad8402_write(dev, i, s->maxdata / 2);
	} else
		s->type = COMEDI_SUBD_UNUSED;

	/* serial EEPROM, if present */
	s = &dev->subdevices[8];
	if (readl(devpriv->plx9080_iobase + PLX_CONTROL_REG) & CTL_EECHK) {
		s->type = COMEDI_SUBD_MEMORY;
		s->subdev_flags = SDF_READABLE | SDF_INTERNAL;
		s->n_chan = 128;
		s->maxdata = 0xffff;
		s->insn_read = eeprom_read_insn;
	} else
		s->type = COMEDI_SUBD_UNUSED;

	/*  user counter subd XXX */
	s = &dev->subdevices[9];
	s->type = COMEDI_SUBD_UNUSED;

	return 0;
}

static int auto_attach(struct comedi_device *dev,
		       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct pcidas64_board *thisboard = NULL;
	struct pcidas64_private *devpriv;
	uint32_t local_range, local_decode;
	int retval;

	if (context < ARRAY_SIZE(pcidas64_boards))
		thisboard = &pcidas64_boards[context];
	if (!thisboard)
		return -ENODEV;
	dev->board_ptr = thisboard;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	retval = comedi_pci_enable(dev);
	if (retval)
		return retval;
	pci_set_master(pcidev);

	/* Initialize dev->board_name */
	dev->board_name = thisboard->name;

	devpriv->main_phys_iobase = pci_resource_start(pcidev, 2);
	devpriv->dio_counter_phys_iobase = pci_resource_start(pcidev, 3);

	devpriv->plx9080_iobase = pci_ioremap_bar(pcidev, 0);
	devpriv->main_iobase = pci_ioremap_bar(pcidev, 2);
	dev->mmio = pci_ioremap_bar(pcidev, 3);

	if (!devpriv->plx9080_iobase || !devpriv->main_iobase || !dev->mmio) {
		dev_warn(dev->class_dev, "failed to remap io memory\n");
		return -ENOMEM;
	}

	/*  figure out what local addresses are */
	local_range = readl(devpriv->plx9080_iobase + PLX_LAS0RNG_REG) &
		      LRNG_MEM_MASK;
	local_decode = readl(devpriv->plx9080_iobase + PLX_LAS0MAP_REG) &
		       local_range & LMAP_MEM_MASK;
	devpriv->local0_iobase = ((uint32_t)devpriv->main_phys_iobase &
				  ~local_range) | local_decode;
	local_range = readl(devpriv->plx9080_iobase + PLX_LAS1RNG_REG) &
		      LRNG_MEM_MASK;
	local_decode = readl(devpriv->plx9080_iobase + PLX_LAS1MAP_REG) &
		       local_range & LMAP_MEM_MASK;
	devpriv->local1_iobase = ((uint32_t)devpriv->dio_counter_phys_iobase &
				  ~local_range) | local_decode;

	retval = alloc_and_init_dma_members(dev);
	if (retval < 0)
		return retval;

	devpriv->hw_revision =
		hw_revision(dev, readw(devpriv->main_iobase + HW_STATUS_REG));
	dev_dbg(dev->class_dev, "stc hardware revision %i\n",
		devpriv->hw_revision);
	init_plx9080(dev);
	init_stc_registers(dev);

	retval = request_irq(pcidev->irq, handle_interrupt, IRQF_SHARED,
			     dev->board_name, dev);
	if (retval) {
		dev_dbg(dev->class_dev, "unable to allocate irq %u\n",
			pcidev->irq);
		return retval;
	}
	dev->irq = pcidev->irq;
	dev_dbg(dev->class_dev, "irq %u\n", dev->irq);

	retval = setup_subdevices(dev);
	if (retval < 0)
		return retval;

	return 0;
}

static void detach(struct comedi_device *dev)
{
	const struct pcidas64_board *thisboard = comedi_board(dev);
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct pcidas64_private *devpriv = dev->private;
	unsigned int i;

	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (pcidev) {
			if (devpriv->plx9080_iobase) {
				disable_plx_interrupts(dev);
				iounmap(devpriv->plx9080_iobase);
			}
			if (devpriv->main_iobase)
				iounmap(devpriv->main_iobase);
			if (dev->mmio)
				iounmap(dev->mmio);
			/*  free pci dma buffers */
			for (i = 0; i < ai_dma_ring_count(thisboard); i++) {
				if (devpriv->ai_buffer[i])
					pci_free_consistent(pcidev,
						DMA_BUFFER_SIZE,
						devpriv->ai_buffer[i],
						devpriv->ai_buffer_bus_addr[i]);
			}
			for (i = 0; i < AO_DMA_RING_COUNT; i++) {
				if (devpriv->ao_buffer[i])
					pci_free_consistent(pcidev,
						DMA_BUFFER_SIZE,
						devpriv->ao_buffer[i],
						devpriv->ao_buffer_bus_addr[i]);
			}
			/*  free dma descriptors */
			if (devpriv->ai_dma_desc)
				pci_free_consistent(pcidev,
					sizeof(struct plx_dma_desc) *
					ai_dma_ring_count(thisboard),
					devpriv->ai_dma_desc,
					devpriv->ai_dma_desc_bus_addr);
			if (devpriv->ao_dma_desc)
				pci_free_consistent(pcidev,
					sizeof(struct plx_dma_desc) *
					AO_DMA_RING_COUNT,
					devpriv->ao_dma_desc,
					devpriv->ao_dma_desc_bus_addr);
		}
	}
	comedi_pci_disable(dev);
}

static struct comedi_driver cb_pcidas64_driver = {
	.driver_name	= "cb_pcidas64",
	.module		= THIS_MODULE,
	.auto_attach	= auto_attach,
	.detach		= detach,
};

static int cb_pcidas64_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &cb_pcidas64_driver,
				      id->driver_data);
}

static const struct pci_device_id cb_pcidas64_pci_table[] = {
	{ PCI_VDEVICE(CB, 0x001d), BOARD_PCIDAS6402_16 },
	{ PCI_VDEVICE(CB, 0x001e), BOARD_PCIDAS6402_12 },
	{ PCI_VDEVICE(CB, 0x0035), BOARD_PCIDAS64_M1_16 },
	{ PCI_VDEVICE(CB, 0x0036), BOARD_PCIDAS64_M2_16 },
	{ PCI_VDEVICE(CB, 0x0037), BOARD_PCIDAS64_M3_16 },
	{ PCI_VDEVICE(CB, 0x0052), BOARD_PCIDAS4020_12 },
	{ PCI_VDEVICE(CB, 0x005d), BOARD_PCIDAS6023 },
	{ PCI_VDEVICE(CB, 0x005e), BOARD_PCIDAS6025 },
	{ PCI_VDEVICE(CB, 0x005f), BOARD_PCIDAS6030 },
	{ PCI_VDEVICE(CB, 0x0060), BOARD_PCIDAS6031 },
	{ PCI_VDEVICE(CB, 0x0061), BOARD_PCIDAS6032 },
	{ PCI_VDEVICE(CB, 0x0062), BOARD_PCIDAS6033 },
	{ PCI_VDEVICE(CB, 0x0063), BOARD_PCIDAS6034 },
	{ PCI_VDEVICE(CB, 0x0064), BOARD_PCIDAS6035 },
	{ PCI_VDEVICE(CB, 0x0065), BOARD_PCIDAS6040 },
	{ PCI_VDEVICE(CB, 0x0066), BOARD_PCIDAS6052 },
	{ PCI_VDEVICE(CB, 0x0067), BOARD_PCIDAS6070 },
	{ PCI_VDEVICE(CB, 0x0068), BOARD_PCIDAS6071 },
	{ PCI_VDEVICE(CB, 0x006f), BOARD_PCIDAS6036 },
	{ PCI_VDEVICE(CB, 0x0078), BOARD_PCIDAS6013 },
	{ PCI_VDEVICE(CB, 0x0079), BOARD_PCIDAS6014 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cb_pcidas64_pci_table);

static struct pci_driver cb_pcidas64_pci_driver = {
	.name		= "cb_pcidas64",
	.id_table	= cb_pcidas64_pci_table,
	.probe		= cb_pcidas64_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(cb_pcidas64_driver, cb_pcidas64_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
