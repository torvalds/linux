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

struct ad7476_chip_info {
	unsigned int			int_vref_uv;
	struct iio_chan_spec		channel[2];
};

struct ad7476_state {
	struct spi_device		*spi;
	const struct ad7476_chip_info	*chip_info;
	struct regulator		*reg;
	struct spi_transfer		xfer;
	struct spi_message		msg;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 * Make the buffer large enough for one 16 bit sample and one 64 bit
	 * aligned 64 bit timestamp.
	 */
	unsigned char data[ALIGN(2, sizeof(s64)) + sizeof(s64)]
			____cacheline_aligned;
};

enum ad7476_supported_device_ids {
	ID_AD7466,
	ID_AD7467,
	ID_AD7468,
	ID_AD7495
};

#ifdef CONFIG_IIO_BUFFER
int ad7476_register_ring_funcs_and_init(struct iio_dev *indio_dev);
void ad7476_ring_cleanup(struct iio_dev *indio_dev);
#else /* CONFIG_IIO_BUFFER */

static inline int
ad7476_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void ad7476_ring_cleanup(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_IIO_BUFFER */
#endif /* IIO_ADC_AD7476_H_ */
