#ifndef _ADE7753_H
#define _ADE7753_H

#define ADE7753_WAVEFORM   0x01
#define ADE7753_AENERGY    0x02
#define ADE7753_RAENERGY   0x03
#define ADE7753_LAENERGY   0x04
#define ADE7753_VAENERGY   0x05
#define ADE7753_RVAENERGY  0x06
#define ADE7753_LVAENERGY  0x07
#define ADE7753_LVARENERGY 0x08
#define ADE7753_MODE       0x09
#define ADE7753_IRQEN      0x0A
#define ADE7753_STATUS     0x0B
#define ADE7753_RSTSTATUS  0x0C
#define ADE7753_CH1OS      0x0D
#define ADE7753_CH2OS      0x0E
#define ADE7753_GAIN       0x0F
#define ADE7753_PHCAL      0x10
#define ADE7753_APOS       0x11
#define ADE7753_WGAIN      0x12
#define ADE7753_WDIV       0x13
#define ADE7753_CFNUM      0x14
#define ADE7753_CFDEN      0x15
#define ADE7753_IRMS       0x16
#define ADE7753_VRMS       0x17
#define ADE7753_IRMSOS     0x18
#define ADE7753_VRMSOS     0x19
#define ADE7753_VAGAIN     0x1A
#define ADE7753_VADIV      0x1B
#define ADE7753_LINECYC    0x1C
#define ADE7753_ZXTOUT     0x1D
#define ADE7753_SAGCYC     0x1E
#define ADE7753_SAGLVL     0x1F
#define ADE7753_IPKLVL     0x20
#define ADE7753_VPKLVL     0x21
#define ADE7753_IPEAK      0x22
#define ADE7753_RSTIPEAK   0x23
#define ADE7753_VPEAK      0x24
#define ADE7753_RSTVPEAK   0x25
#define ADE7753_TEMP       0x26
#define ADE7753_PERIOD     0x27
#define ADE7753_TMODE      0x3D
#define ADE7753_CHKSUM     0x3E
#define ADE7753_DIEREV     0x3F

#define ADE7753_READ_REG(a)    a
#define ADE7753_WRITE_REG(a) ((a) | 0x80)

#define ADE7753_MAX_TX    4
#define ADE7753_MAX_RX    4
#define ADE7753_STARTUP_DELAY 1

#define ADE7753_SPI_SLOW	(u32)(300 * 1000)
#define ADE7753_SPI_BURST	(u32)(1000 * 1000)
#define ADE7753_SPI_FAST	(u32)(2000 * 1000)

#define DRIVER_NAME		"ade7753"

/**
 * struct ade7753_state - device instance specific data
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
struct ade7753_state {
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

enum ade7753_scan {
	ADE7753_SCAN_ACTIVE_POWER,
	ADE7753_SCAN_CH1,
	ADE7753_SCAN_CH2,
};

void ade7753_remove_trigger(struct iio_dev *indio_dev);
int ade7753_probe_trigger(struct iio_dev *indio_dev);

ssize_t ade7753_read_data_from_ring(struct device *dev,
		struct device_attribute *attr,
		char *buf);


int ade7753_configure_ring(struct iio_dev *indio_dev);
void ade7753_unconfigure_ring(struct iio_dev *indio_dev);

int ade7753_initialize_ring(struct iio_ring_buffer *ring);
void ade7753_uninitialize_ring(struct iio_ring_buffer *ring);
#else /* CONFIG_IIO_RING_BUFFER */

static inline void ade7753_remove_trigger(struct iio_dev *indio_dev)
{
}
static inline int ade7753_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
ade7753_read_data_from_ring(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return 0;
}

static int ade7753_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}
static inline void ade7753_unconfigure_ring(struct iio_dev *indio_dev)
{
}
static inline int ade7753_initialize_ring(struct iio_ring_buffer *ring)
{
	return 0;
}
static inline void ade7753_uninitialize_ring(struct iio_ring_buffer *ring)
{
}
#endif /* CONFIG_IIO_RING_BUFFER */

#endif
