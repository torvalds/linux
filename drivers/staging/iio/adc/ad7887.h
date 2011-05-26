/*
 * AD7887 SPI ADC driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */
#ifndef IIO_ADC_AD7887_H_
#define IIO_ADC_AD7887_H_

#define AD7887_REF_DIS		(1 << 5) /* on-chip reference disable */
#define AD7887_DUAL		(1 << 4) /* dual-channel mode */
#define AD7887_CH_AIN1		(1 << 3) /* convert on channel 1, DUAL=1 */
#define AD7887_CH_AIN0		(0 << 3) /* convert on channel 0, DUAL=0,1 */
#define AD7887_PM_MODE1		(0)	 /* CS based shutdown */
#define AD7887_PM_MODE2		(1)	 /* full on */
#define AD7887_PM_MODE3		(2)	 /* auto shutdown after conversion */
#define AD7887_PM_MODE4		(3)	 /* standby mode */

enum ad7887_channels {
	AD7887_CH0,
	AD7887_CH0_CH1,
	AD7887_CH1,
};

#define RES_MASK(bits)	((1 << (bits)) - 1) /* TODO: move this into a common header */

/*
 * TODO: struct ad7887_platform_data needs to go into include/linux/iio
 */

struct ad7887_platform_data {
	/* External Vref voltage applied */
	u16				vref_mv;
	/*
	 * AD7887:
	 * In single channel mode en_dual = flase, AIN1/Vref pins assumes its
	 * Vref function. In dual channel mode en_dual = true, AIN1 becomes the
	 * second input channel, and Vref is internally connected to Vdd.
	 */
	bool				en_dual;
	/*
	 * AD7887:
	 * use_onchip_ref = true, the Vref is internally connected to the 2.500V
	 * Voltage reference. If use_onchip_ref = false, the reference voltage
	 * is supplied by AIN1/Vref
	 */
	bool				use_onchip_ref;
};

struct ad7887_chip_info {
	u8				bits;		/* number of ADC bits */
	u8				storagebits;	/* number of bits read from the ADC */
	u8				left_shift;	/* number of bits the sample must be shifted */
	char				sign;		/* [s]igned or [u]nsigned */
	u16				int_vref_mv;	/* internal reference voltage */
};

struct ad7887_state {
	struct iio_dev			*indio_dev;
	struct spi_device		*spi;
	const struct ad7887_chip_info	*chip_info;
	struct regulator		*reg;
	struct work_struct		poll_work;
	atomic_t			protect_ring;
	size_t				d_size;
	u16				int_vref_mv;
	bool				en_dual;
	struct spi_transfer		xfer[4];
	struct spi_message		msg[3];
	struct spi_message		*ring_msg;
	unsigned char			tx_cmd_buf[8];

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */

	unsigned char			data[4] ____cacheline_aligned;
};

enum ad7887_supported_device_ids {
	ID_AD7887
};

#ifdef CONFIG_IIO_RING_BUFFER
int ad7887_scan_from_ring(struct ad7887_state *st, long mask);
int ad7887_register_ring_funcs_and_init(struct iio_dev *indio_dev);
void ad7887_ring_cleanup(struct iio_dev *indio_dev);
#else /* CONFIG_IIO_RING_BUFFER */
static inline int ad7887_scan_from_ring(struct ad7887_state *st, long mask)
{
	return 0;
}

static inline int
ad7887_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void ad7887_ring_cleanup(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* IIO_ADC_AD7887_H_ */
