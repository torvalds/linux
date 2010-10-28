#ifndef SPI_ADIS16080_H_
#define SPI_ADIS16080_H_

#define ADIS16080_DIN_CODE   4 /* Output data format setting. 0: Twos complement. 1: Offset binary. */
#define ADIS16080_DIN_GYRO   (0 << 10) /* Gyroscope output */
#define ADIS16080_DIN_TEMP   (1 << 10) /* Temperature output */
#define ADIS16080_DIN_AIN1   (2 << 10)
#define ADIS16080_DIN_AIN2   (3 << 10)
#define ADIS16080_DIN_WRITE  (1 << 15) /* 1: Write contents on DIN to control register.
					* 0: No changes to control register.
					*/

#define ADIS16080_MAX_TX     2
#define ADIS16080_MAX_RX     2

/**
 * struct adis16080_state - device instance specific data
 * @us:			actual spi_device to write data
 * @work_trigger_to_ring: bh for triggered event handling
 * @inter:		used to check if new interrupt has been triggered
 * @last_timestamp:	passing timestamp from th to bh of interrupt handler
 * @indio_dev:		industrial I/O device structure
 * @trig:		data ready trigger registered with iio
 * @tx:			transmit buffer
 * @rx:			recieve buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16080_state {
	struct spi_device		*us;
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

enum adis16080_scan {
	ADIS16080_SCAN_GYRO,
	ADIS16080_SCAN_TEMP,
	ADIS16080_SCAN_ADC_1,
	ADIS16080_SCAN_ADC_2,
};

void adis16080_remove_trigger(struct iio_dev *indio_dev);
int adis16080_probe_trigger(struct iio_dev *indio_dev);

ssize_t adis16080_read_data_from_ring(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);


int adis16080_configure_ring(struct iio_dev *indio_dev);
void adis16080_unconfigure_ring(struct iio_dev *indio_dev);

int adis16080_initialize_ring(struct iio_ring_buffer *ring);
void adis16080_uninitialize_ring(struct iio_ring_buffer *ring);
#else /* CONFIG_IIO_RING_BUFFER */

static inline void adis16080_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int adis16080_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
adis16080_read_data_from_ring(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static int adis16080_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis16080_unconfigure_ring(struct iio_dev *indio_dev)
{
}

static inline int adis16080_initialize_ring(struct iio_ring_buffer *ring)
{
	return 0;
}

static inline void adis16080_uninitialize_ring(struct iio_ring_buffer *ring)
{
}

#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* SPI_ADIS16080_H_ */
