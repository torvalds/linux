#ifndef _ADE7759_H
#define _ADE7759_H

#define ADE7759_WAVEFORM  0x01
#define ADE7759_AENERGY   0x02
#define ADE7759_RSTENERGY 0x03
#define ADE7759_STATUS    0x04
#define ADE7759_RSTSTATUS 0x05
#define ADE7759_MODE      0x06
#define ADE7759_CFDEN     0x07
#define ADE7759_CH1OS     0x08
#define ADE7759_CH2OS     0x09
#define ADE7759_GAIN      0x0A
#define ADE7759_APGAIN    0x0B
#define ADE7759_PHCAL     0x0C
#define ADE7759_APOS      0x0D
#define ADE7759_ZXTOUT    0x0E
#define ADE7759_SAGCYC    0x0F
#define ADE7759_IRQEN     0x10
#define ADE7759_SAGLVL    0x11
#define ADE7759_TEMP      0x12
#define ADE7759_LINECYC   0x13
#define ADE7759_LENERGY   0x14
#define ADE7759_CFNUM     0x15
#define ADE7759_CHKSUM    0x1E
#define ADE7759_DIEREV    0x1F

#define ADE7759_READ_REG(a)    a
#define ADE7759_WRITE_REG(a) ((a) | 0x80)

#define ADE7759_MAX_TX    6
#define ADE7759_MAX_RX    6
#define ADE7759_STARTUP_DELAY 1

#define ADE7759_SPI_SLOW	(u32)(300 * 1000)
#define ADE7759_SPI_BURST	(u32)(1000 * 1000)
#define ADE7759_SPI_FAST	(u32)(2000 * 1000)

#define DRIVER_NAME		"ade7759"

/**
 * struct ade7759_state - device instance specific data
 * @us:			actual spi_device
 * @work_trigger_to_ring: bh for triggered event handling
 * @inter:		used to check if new interrupt has been triggered
 * @last_timestamp:	passing timestamp from th to bh of interrupt handler
 * @indio_dev:		industrial I/O device structure
 * @trig:		data ready trigger registered with iio
 * @tx:			transmit buffer
 * @rx:			recieve buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct ade7759_state {
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

enum ade7759_scan {
	ADE7759_SCAN_ACTIVE_POWER,
	ADE7759_SCAN_CH1_CH2,
	ADE7759_SCAN_CH1,
	ADE7759_SCAN_CH2,
};

void ade7759_remove_trigger(struct iio_dev *indio_dev);
int ade7759_probe_trigger(struct iio_dev *indio_dev);

ssize_t ade7759_read_data_from_ring(struct device *dev,
		struct device_attribute *attr,
		char *buf);


int ade7759_configure_ring(struct iio_dev *indio_dev);
void ade7759_unconfigure_ring(struct iio_dev *indio_dev);

int ade7759_initialize_ring(struct iio_ring_buffer *ring);
void ade7759_uninitialize_ring(struct iio_ring_buffer *ring);
#else /* CONFIG_IIO_RING_BUFFER */

static inline void ade7759_remove_trigger(struct iio_dev *indio_dev)
{
}
static inline int ade7759_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
ade7759_read_data_from_ring(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return 0;
}

static int ade7759_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}
static inline void ade7759_unconfigure_ring(struct iio_dev *indio_dev)
{
}
static inline int ade7759_initialize_ring(struct iio_ring_buffer *ring)
{
	return 0;
}
static inline void ade7759_uninitialize_ring(struct iio_ring_buffer *ring)
{
}
#endif /* CONFIG_IIO_RING_BUFFER */

#endif
