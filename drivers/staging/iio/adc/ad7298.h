/*
 * AD7298 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef IIO_ADC_AD7298_H_
#define IIO_ADC_AD7298_H_

#define AD7298_WRITE	(1 << 15) /* write to the control register */
#define AD7298_REPEAT	(1 << 14) /* repeated conversion enable */
#define AD7298_CH(x)	(1 << (13 - (x))) /* channel select */
#define AD7298_TSENSE	(1 << 5) /* temperature conversion enable */
#define AD7298_EXTREF	(1 << 2) /* external reference enable */
#define AD7298_TAVG	(1 << 1) /* temperature sensor averaging enable */
#define AD7298_PDD	(1 << 0) /* partial power down enable */

#define AD7298_CH_MASK	(AD7298_CH0 | AD7298_CH1 | AD7298_CH2 | AD7298_CH3 | \
			AD7298_CH4 | AD7298_CH5 | AD7298_CH6 | AD7298_CH7)

#define AD7298_MAX_CHAN		8
#define AD7298_BITS		12
#define AD7298_STORAGE_BITS	16
#define AD7298_INTREF_mV	2500

#define RES_MASK(bits)	((1 << (bits)) - 1)

/*
 * TODO: struct ad7298_platform_data needs to go into include/linux/iio
 */

struct ad7298_platform_data {
	/* External Vref voltage applied */
	u16				vref_mv;
};

struct ad7298_state {
	struct iio_dev			*indio_dev;
	struct spi_device		*spi;
	struct regulator		*reg;
	struct work_struct		poll_work;
	atomic_t			protect_ring;
	size_t				d_size;
	u16				int_vref_mv;
	unsigned			ext_ref;
	struct spi_transfer		ring_xfer[10];
	struct spi_transfer		scan_single_xfer[3];
	struct spi_message		ring_msg;
	struct spi_message		scan_single_msg;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	unsigned short			rx_buf[8] ____cacheline_aligned;
	unsigned short			tx_buf[2];
};

#ifdef CONFIG_IIO_RING_BUFFER
int ad7298_scan_from_ring(struct ad7298_state *st, long ch);
int ad7298_register_ring_funcs_and_init(struct iio_dev *indio_dev);
void ad7298_ring_cleanup(struct iio_dev *indio_dev);
#else /* CONFIG_IIO_RING_BUFFER */
static inline int ad7298_scan_from_ring(struct ad7298_state *st, long ch)
{
	return 0;
}

static inline int
ad7298_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void ad7298_ring_cleanup(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* IIO_ADC_AD7298_H_ */
