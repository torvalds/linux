/*
 * AD7476/5/7/8 (A) SPI ADC driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */
#ifndef IIO_ADC_AD7476_H_
#define IIO_ADC_AD7476_H_

#define RES_MASK(bits)	((1 << (bits)) - 1)

/*
 * TODO: struct ad7476_platform_data needs to go into include/linux/iio
 */

struct ad7476_platform_data {
	u16				vref_mv;
};

struct ad7476_chip_info {
	u16				int_vref_mv;
	struct iio_chan_spec		channel[2];
};

struct ad7476_state {
	struct iio_dev			*indio_dev;
	struct spi_device		*spi;
	const struct ad7476_chip_info	*chip_info;
	struct regulator		*reg;
	size_t				d_size;
	u16				int_vref_mv;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	unsigned char			data[2] ____cacheline_aligned;
};

enum ad7476_supported_device_ids {
	ID_AD7466,
	ID_AD7467,
	ID_AD7468,
	ID_AD7475,
	ID_AD7476,
	ID_AD7477,
	ID_AD7478,
	ID_AD7495
};

#ifdef CONFIG_IIO_RING_BUFFER
int ad7476_scan_from_ring(struct ad7476_state *st);
int ad7476_register_ring_funcs_and_init(struct iio_dev *indio_dev);
void ad7476_ring_cleanup(struct iio_dev *indio_dev);
#else /* CONFIG_IIO_RING_BUFFER */
static inline int ad7476_scan_from_ring(struct ad7476_state *st)
{
	return 0;
}

static inline int
ad7476_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void ad7476_ring_cleanup(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* IIO_ADC_AD7476_H_ */
