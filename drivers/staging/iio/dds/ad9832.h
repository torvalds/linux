/*
 * AD9832 SPI DDS driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */
#ifndef IIO_DDS_AD9832_H_
#define IIO_DDS_AD9832_H_

/* Registers */

#define AD9832_FREQ0LL		0x0
#define AD9832_FREQ0HL		0x1
#define AD9832_FREQ0LM		0x2
#define AD9832_FREQ0HM		0x3
#define AD9832_FREQ1LL		0x4
#define AD9832_FREQ1HL		0x5
#define AD9832_FREQ1LM		0x6
#define AD9832_FREQ1HM		0x7
#define AD9832_PHASE0L		0x8
#define AD9832_PHASE0H		0x9
#define AD9832_PHASE1L		0xA
#define AD9832_PHASE1H		0xB
#define AD9832_PHASE2L		0xC
#define AD9832_PHASE2H		0xD
#define AD9832_PHASE3L		0xE
#define AD9832_PHASE3H		0xF

#define AD9832_PHASE_SYM	0x10
#define AD9832_FREQ_SYM		0x11
#define AD9832_PINCTRL_EN	0x12
#define AD9832_OUTPUT_EN	0x13

/* Command Control Bits */

#define AD9832_CMD_PHA8BITSW	0x1
#define AD9832_CMD_PHA16BITSW	0x0
#define AD9832_CMD_FRE8BITSW	0x3
#define AD9832_CMD_FRE16BITSW	0x2
#define AD9832_CMD_FPSELECT	0x6
#define AD9832_CMD_SYNCSELSRC	0x8
#define AD9832_CMD_SLEEPRESCLR	0xC

#define AD9832_FREQ		(1 << 11)
#define AD9832_PHASE(x)		(((x) & 3) << 9)
#define AD9832_SYNC		(1 << 13)
#define AD9832_SELSRC		(1 << 12)
#define AD9832_SLEEP		(1 << 13)
#define AD9832_RESET		(1 << 12)
#define AD9832_CLR		(1 << 11)
#define CMD_SHIFT		12
#define ADD_SHIFT		8
#define AD9832_FREQ_BITS	32
#define AD9832_PHASE_BITS	12
#define RES_MASK(bits)		((1 << (bits)) - 1)

/**
 * struct ad9832_state - driver instance specific data
 * @indio_dev:		the industrial I/O device
 * @spi:		spi_device
 * @reg:		supply regulator
 * @mclk:		external master clock
 * @ctrl_fp:		cached frequency/phase control word
 * @ctrl_ss:		cached sync/selsrc control word
 * @ctrl_src:		cached sleep/reset/clr word
 * @xfer:		default spi transfer
 * @msg:		default spi message
 * @freq_xfer:		tuning word spi transfer
 * @freq_msg:		tuning word spi message
 * @phase_xfer:		tuning word spi transfer
 * @phase_msg:		tuning word spi message
 * @data:		spi transmit buffer
 * @phase_data:		tuning word spi transmit buffer
 * @freq_data:		tuning word spi transmit buffer
 */

struct ad9832_state {
	struct iio_dev			*indio_dev;
	struct spi_device		*spi;
	struct regulator		*reg;
	unsigned long			mclk;
	unsigned short			ctrl_fp;
	unsigned short			ctrl_ss;
	unsigned short			ctrl_src;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	struct spi_transfer		freq_xfer[4];
	struct spi_message		freq_msg;
	struct spi_transfer		phase_xfer[2];
	struct spi_message		phase_msg;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	union {
		unsigned short		freq_data[4]____cacheline_aligned;
		unsigned short		phase_data[2];
		unsigned short		data;
	};
};

/*
 * TODO: struct ad9832_platform_data needs to go into include/linux/iio
 */

/**
 * struct ad9832_platform_data - platform specific information
 * @mclk:		master clock in Hz
 * @freq0:		power up freq0 tuning word in Hz
 * @freq1:		power up freq1 tuning word in Hz
 * @phase0:		power up phase0 value [0..4095] correlates with 0..2PI
 * @phase1:		power up phase1 value [0..4095] correlates with 0..2PI
 * @phase2:		power up phase2 value [0..4095] correlates with 0..2PI
 * @phase3:		power up phase3 value [0..4095] correlates with 0..2PI
 */

struct ad9832_platform_data {
	unsigned long		mclk;
	unsigned long		freq0;
	unsigned long		freq1;
	unsigned short		phase0;
	unsigned short		phase1;
	unsigned short		phase2;
	unsigned short		phase3;
};

#endif /* IIO_DDS_AD9832_H_ */
