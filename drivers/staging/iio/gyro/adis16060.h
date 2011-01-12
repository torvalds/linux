#ifndef SPI_ADIS16060_H_
#define SPI_ADIS16060_H_

#define ADIS16060_GYRO       0x20 /* Measure Angular Rate (Gyro) */
#define ADIS16060_SUPPLY_OUT 0x10 /* Measure Temperature */
#define ADIS16060_AIN2       0x80 /* Measure AIN2 */
#define ADIS16060_AIN1       0x40 /* Measure AIN1 */
#define ADIS16060_TEMP_OUT   0x22 /* Set Positive Self-Test and Output for Angular Rate */
#define ADIS16060_ANGL_OUT   0x21 /* Set Negative Self-Test and Output for Angular Rate */

#define ADIS16060_MAX_TX     3
#define ADIS16060_MAX_RX     3

/**
 * struct adis16060_state - device instance specific data
 * @us_w:			actual spi_device to write data
 * @work_trigger_to_ring: bh for triggered event handling
 * @inter:		used to check if new interrupt has been triggered
 * @last_timestamp:	passing timestamp from th to bh of interrupt handler
 * @indio_dev:		industrial I/O device structure
 * @trig:		data ready trigger registered with iio
 * @tx:			transmit buffer
 * @rx:			recieve buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16060_state {
	struct spi_device		*us_w;
	struct spi_device		*us_r;
	struct work_struct		work_trigger_to_ring;
	s64				last_timestamp;
	struct iio_dev			*indio_dev;
	struct iio_trigger		*trig;
	u8				*tx;
	u8				*rx;
	struct mutex			buf_lock;
};

#if defined(CONFIG_IIO_RING_BUFFER) && defined(THIS_HAS_RING_BUFFER_SUPPORT)
/* At the moment triggers are only used for ring buffer
 * filling. This may change!
 */

enum adis16060_scan {
	ADIS16060_SCAN_GYRO,
	ADIS16060_SCAN_TEMP,
	ADIS16060_SCAN_ADC_1,
	ADIS16060_SCAN_ADC_2,
};

void adis16060_remove_trigger(struct iio_dev *indio_dev);
int adis16060_probe_trigger(struct iio_dev *indio_dev);

ssize_t adis16060_read_data_from_ring(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);


int adis16060_configure_ring(struct iio_dev *indio_dev);
void adis16060_unconfigure_ring(struct iio_dev *indio_dev);

int adis16060_initialize_ring(struct iio_ring_buffer *ring);
void adis16060_uninitialize_ring(struct iio_ring_buffer *ring);
#else /* CONFIG_IIO_RING_BUFFER */

static inline void adis16060_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int adis16060_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
adis16060_read_data_from_ring(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static int adis16060_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis16060_unconfigure_ring(struct iio_dev *indio_dev)
{
}

static inline int adis16060_initialize_ring(struct iio_ring_buffer *ring)
{
	return 0;
}

static inline void adis16060_uninitialize_ring(struct iio_ring_buffer *ring)
{
}

#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* SPI_ADIS16060_H_ */
