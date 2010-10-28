#ifndef SPI_ADIS16130_H_
#define SPI_ADIS16130_H_

#define ADIS16130_CON         0x0
#define ADIS16130_CON_RD      (1 << 6)
#define ADIS16130_IOP         0x1
#define ADIS16130_IOP_ALL_RDY (1 << 3) /* 1 = data-ready signal low when unread data on all channels; */
#define ADIS16130_IOP_SYNC    (1 << 0) /* 1 = synchronization enabled */
#define ADIS16130_RATEDATA    0x8 /* Gyroscope output, rate of rotation */
#define ADIS16130_TEMPDATA    0xA /* Temperature output */
#define ADIS16130_RATECS      0x28 /* Gyroscope channel setup */
#define ADIS16130_RATECS_EN   (1 << 3) /* 1 = channel enable; */
#define ADIS16130_TEMPCS      0x2A /* Temperature channel setup */
#define ADIS16130_TEMPCS_EN   (1 << 3)
#define ADIS16130_RATECONV    0x30
#define ADIS16130_TEMPCONV    0x32
#define ADIS16130_MODE        0x38
#define ADIS16130_MODE_24BIT  (1 << 1) /* 1 = 24-bit resolution; */

#define ADIS16130_MAX_TX     4
#define ADIS16130_MAX_RX     4

/**
 * struct adis16130_state - device instance specific data
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
struct adis16130_state {
	struct spi_device		*us;
	struct work_struct		work_trigger_to_ring;
	s64				last_timestamp;
	struct iio_dev			*indio_dev;
	struct iio_trigger		*trig;
	u8				*tx;
	u8				*rx;
	u32                             mode; /* 1: 24bits mode 0:16bits mode */
	struct mutex			buf_lock;
};

#if defined(CONFIG_IIO_RING_BUFFER) && defined(THIS_HAS_RING_BUFFER_SUPPORT)
/* At the moment triggers are only used for ring buffer
 * filling. This may change!
 */

enum adis16130_scan {
	ADIS16130_SCAN_GYRO,
	ADIS16130_SCAN_TEMP,
};

void adis16130_remove_trigger(struct iio_dev *indio_dev);
int adis16130_probe_trigger(struct iio_dev *indio_dev);

ssize_t adis16130_read_data_from_ring(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);


int adis16130_configure_ring(struct iio_dev *indio_dev);
void adis16130_unconfigure_ring(struct iio_dev *indio_dev);

int adis16130_initialize_ring(struct iio_ring_buffer *ring);
void adis16130_uninitialize_ring(struct iio_ring_buffer *ring);
#else /* CONFIG_IIO_RING_BUFFER */

static inline void adis16130_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int adis16130_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
adis16130_read_data_from_ring(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static int adis16130_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis16130_unconfigure_ring(struct iio_dev *indio_dev)
{
}

static inline int adis16130_initialize_ring(struct iio_ring_buffer *ring)
{
	return 0;
}

static inline void adis16130_uninitialize_ring(struct iio_ring_buffer *ring)
{
}

#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* SPI_ADIS16130_H_ */
