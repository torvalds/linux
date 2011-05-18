/*
 * sca3000.c -- support VTI sca3000 series accelerometers
 *              via SPI
 *
 * Copyright (c) 2007 Jonathan Cameron <jic23@cam.ac.uk>
 *
 * Partly based upon tle62x0.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Initial mode is direct measurement.
 *
 * Untested things
 *
 * Temperature reading (the e05 I'm testing with doesn't have a sensor)
 *
 * Free fall detection mode - supported but untested as I'm not droping my
 * dubious wire rig far enough to test it.
 *
 * Unsupported as yet
 *
 * Time stamping of data from ring. Various ideas on how to do this but none
 * are remotely simple. Suggestions welcome.
 *
 * Individual enabling disabling of channels going into ring buffer
 *
 * Overflow handling (this is signaled for all but 8 bit ring buffer mode.)
 *
 * Motion detector using AND combinations of signals.
 *
 * Note: Be very careful about not touching an register bytes marked
 * as reserved on the data sheet. They really mean it as changing convents of
 * some will cause the device to lock up.
 *
 * Known issues - on rare occasions the interrupts lock up. Not sure why as yet.
 * Can probably alleviate this by reading the interrupt register on start, but
 * that is really just brushing the problem under the carpet.
 */
#define SCA3000_WRITE_REG(a) (((a) << 2) | 0x02)
#define SCA3000_READ_REG(a) ((a) << 2)

#define SCA3000_REG_ADDR_REVID			0x00
#define SCA3000_REVID_MAJOR_MASK		0xf0
#define SCA3000_REVID_MINOR_MASK		0x0f

#define SCA3000_REG_ADDR_STATUS			0x02
#define SCA3000_LOCKED				0x20
#define SCA3000_EEPROM_CS_ERROR			0x02
#define SCA3000_SPI_FRAME_ERROR			0x01

/* All reads done using register decrement so no need to directly access LSBs */
#define SCA3000_REG_ADDR_X_MSB			0x05
#define SCA3000_REG_ADDR_Y_MSB			0x07
#define SCA3000_REG_ADDR_Z_MSB			0x09

#define SCA3000_REG_ADDR_RING_OUT		0x0f

/* Temp read untested - the e05 doesn't have the sensor */
#define SCA3000_REG_ADDR_TEMP_MSB		0x13

#define SCA3000_REG_ADDR_MODE			0x14
#define SCA3000_MODE_PROT_MASK			0x28

#define SCA3000_RING_BUF_ENABLE			0x80
#define SCA3000_RING_BUF_8BIT			0x40
/* Free fall detection triggers an interrupt if the acceleration
 * is below a threshold for equivalent of 25cm drop
 */
#define SCA3000_FREE_FALL_DETECT		0x10
#define SCA3000_MEAS_MODE_NORMAL		0x00
#define SCA3000_MEAS_MODE_OP_1			0x01
#define SCA3000_MEAS_MODE_OP_2			0x02

/* In motion detection mode the accelerations are band pass filtered
 * (aprox 1 - 25Hz) and then a programmable threshold used to trigger
 * and interrupt.
 */
#define SCA3000_MEAS_MODE_MOT_DET		0x03

#define SCA3000_REG_ADDR_BUF_COUNT		0x15

#define SCA3000_REG_ADDR_INT_STATUS		0x16

#define SCA3000_INT_STATUS_THREE_QUARTERS	0x80
#define SCA3000_INT_STATUS_HALF			0x40

#define SCA3000_INT_STATUS_FREE_FALL		0x08
#define SCA3000_INT_STATUS_Y_TRIGGER		0x04
#define SCA3000_INT_STATUS_X_TRIGGER		0x02
#define SCA3000_INT_STATUS_Z_TRIGGER		0x01

/* Used to allow access to multiplexed registers */
#define SCA3000_REG_ADDR_CTRL_SEL		0x18
/* Only available for SCA3000-D03 and SCA3000-D01 */
#define SCA3000_REG_CTRL_SEL_I2C_DISABLE	0x01
#define SCA3000_REG_CTRL_SEL_MD_CTRL		0x02
#define SCA3000_REG_CTRL_SEL_MD_Y_TH		0x03
#define SCA3000_REG_CTRL_SEL_MD_X_TH		0x04
#define SCA3000_REG_CTRL_SEL_MD_Z_TH		0x05
/* BE VERY CAREFUL WITH THIS, IF 3 BITS ARE NOT SET the device
   will not function */
#define SCA3000_REG_CTRL_SEL_OUT_CTRL		0x0B
#define SCA3000_OUT_CTRL_PROT_MASK		0xE0
#define SCA3000_OUT_CTRL_BUF_X_EN		0x10
#define SCA3000_OUT_CTRL_BUF_Y_EN		0x08
#define SCA3000_OUT_CTRL_BUF_Z_EN		0x04
#define SCA3000_OUT_CTRL_BUF_DIV_4		0x02
#define SCA3000_OUT_CTRL_BUF_DIV_2		0x01

/* Control which motion detector interrupts are on.
 * For now only OR combinations are supported.x
 */
#define SCA3000_MD_CTRL_PROT_MASK		0xC0
#define SCA3000_MD_CTRL_OR_Y			0x01
#define SCA3000_MD_CTRL_OR_X			0x02
#define SCA3000_MD_CTRL_OR_Z			0x04
/* Currently unsupported */
#define SCA3000_MD_CTRL_AND_Y			0x08
#define SCA3000_MD_CTRL_AND_X			0x10
#define SAC3000_MD_CTRL_AND_Z			0x20

/* Some control registers of complex access methods requiring this register to
 * be used to remove a lock.
 */
#define SCA3000_REG_ADDR_UNLOCK			0x1e

#define SCA3000_REG_ADDR_INT_MASK		0x21
#define SCA3000_INT_MASK_PROT_MASK		0x1C

#define SCA3000_INT_MASK_RING_THREE_QUARTER	0x80
#define SCA3000_INT_MASK_RING_HALF		0x40

#define SCA3000_INT_MASK_ALL_INTS		0x02
#define SCA3000_INT_MASK_ACTIVE_HIGH		0x01
#define SCA3000_INT_MASK_ACTIVE_LOW		0x00

/* Values of mulipexed registers (write to ctrl_data after select) */
#define SCA3000_REG_ADDR_CTRL_DATA		0x22

/* Measurement modes available on some sca3000 series chips. Code assumes others
 * may become available in the future.
 *
 * Bypass - Bypass the low-pass filter in the signal channel so as to increase
 *          signal bandwidth.
 *
 * Narrow - Narrow low-pass filtering of the signal channel and half output
 *          data rate by decimation.
 *
 * Wide - Widen low-pass filtering of signal channel to increase bandwidth
 */
#define SCA3000_OP_MODE_BYPASS			0x01
#define SCA3000_OP_MODE_NARROW			0x02
#define SCA3000_OP_MODE_WIDE			0x04
#define SCA3000_MAX_TX 6
#define SCA3000_MAX_RX 2

/**
 * struct sca3000_state - device instance state information
 * @us:			the associated spi device
 * @info:			chip variant information
 * @indio_dev:			device information used by the IIO core
 * @interrupt_handler_ws:	event interrupt handler for all events
 * @last_timestamp:		the timestamp of the last event
 * @mo_det_use_count:		reference counter for the motion detection unit
 * @lock:			lock used to protect elements of sca3000_state
 *				and the underlying device state.
 * @bpse:			number of bits per scan element
 * @tx:			dma-able transmit buffer
 * @rx:			dma-able receive buffer
 **/
struct sca3000_state {
	struct spi_device		*us;
	const struct sca3000_chip_info	*info;
	struct iio_dev			*indio_dev;
	struct work_struct		interrupt_handler_ws;
	s64				last_timestamp;
	int				mo_det_use_count;
	struct mutex			lock;
	int				bpse;
	/* Can these share a cacheline ? */
	u8				rx[2] ____cacheline_aligned;
	u8				tx[6] ____cacheline_aligned;
};

/**
 * struct sca3000_chip_info - model dependent parameters
 * @scale:			scale * 10^-6
 * @temp_output:		some devices have temperature sensors.
 * @measurement_mode_freq:	normal mode sampling frequency
 * @option_mode_1:		first optional mode. Not all models have one
 * @option_mode_1_freq:		option mode 1 sampling frequency
 * @option_mode_2:		second optional mode. Not all chips have one
 * @option_mode_2_freq:		option mode 2 sampling frequency
 *
 * This structure is used to hold information about the functionality of a given
 * sca3000 variant.
 **/
struct sca3000_chip_info {
	unsigned int		scale;
	bool			temp_output;
	int			measurement_mode_freq;
	int			option_mode_1;
	int			option_mode_1_freq;
	int			option_mode_2;
	int			option_mode_2_freq;
	int			mot_det_mult_xz[6];
	int			mot_det_mult_y[7];
};

int sca3000_read_data_short(struct sca3000_state *st,
			    u8 reg_address_high,
			    int len);

/**
 * sca3000_write_reg() write a single register
 * @address:	address of register on chip
 * @val:	value to be written to register
 *
 * The main lock must be held.
 **/
int sca3000_write_reg(struct sca3000_state *st, u8 address, u8 val);

#ifdef CONFIG_IIO_RING_BUFFER
/**
 * sca3000_register_ring_funcs() setup the ring state change functions
 **/
void sca3000_register_ring_funcs(struct iio_dev *indio_dev);

/**
 * sca3000_configure_ring() - allocate and configure ring buffer
 * @indio_dev: iio-core device whose ring is to be configured
 *
 * The hardware ring buffer needs far fewer ring buffer functions than
 * a software one as a lot of things are handled automatically.
 * This function also tells the iio core that our device supports a
 * hardware ring buffer mode.
 **/
int sca3000_configure_ring(struct iio_dev *indio_dev);

/**
 * sca3000_unconfigure_ring() - deallocate the ring buffer
 * @indio_dev: iio-core device whose ring we are freeing
 **/
void sca3000_unconfigure_ring(struct iio_dev *indio_dev);

/**
 * sca3000_ring_int_process() handles ring related event pushing and escalation
 * @val:	the event code
 **/
void sca3000_ring_int_process(u8 val, struct iio_ring_buffer *ring);

#else
static inline void sca3000_register_ring_funcs(struct iio_dev *indio_dev)
{
}

static inline
int sca3000_register_ring_access_and_init(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void sca3000_ring_int_process(u8 val, void *ring)
{
}

#endif

