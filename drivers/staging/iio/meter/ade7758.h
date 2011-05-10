#ifndef _ADE7758_H
#define _ADE7758_H

#define ADE7758_AWATTHR   0x01
#define ADE7758_BWATTHR   0x02
#define ADE7758_CWATTHR   0x03
#define ADE7758_AVARHR    0x04
#define ADE7758_BVARHR    0x05
#define ADE7758_CVARHR    0x06
#define ADE7758_AVAHR     0x07
#define ADE7758_BVAHR     0x08
#define ADE7758_CVAHR     0x09
#define ADE7758_AIRMS     0x0A
#define ADE7758_BIRMS     0x0B
#define ADE7758_CIRMS     0x0C
#define ADE7758_AVRMS     0x0D
#define ADE7758_BVRMS     0x0E
#define ADE7758_CVRMS     0x0F
#define ADE7758_FREQ      0x10
#define ADE7758_TEMP      0x11
#define ADE7758_WFORM     0x12
#define ADE7758_OPMODE    0x13
#define ADE7758_MMODE     0x14
#define ADE7758_WAVMODE   0x15
#define ADE7758_COMPMODE  0x16
#define ADE7758_LCYCMODE  0x17
#define ADE7758_MASK      0x18
#define ADE7758_STATUS    0x19
#define ADE7758_RSTATUS   0x1A
#define ADE7758_ZXTOUT    0x1B
#define ADE7758_LINECYC   0x1C
#define ADE7758_SAGCYC    0x1D
#define ADE7758_SAGLVL    0x1E
#define ADE7758_VPINTLVL  0x1F
#define ADE7758_IPINTLVL  0x20
#define ADE7758_VPEAK     0x21
#define ADE7758_IPEAK     0x22
#define ADE7758_GAIN      0x23
#define ADE7758_AVRMSGAIN 0x24
#define ADE7758_BVRMSGAIN 0x25
#define ADE7758_CVRMSGAIN 0x26
#define ADE7758_AIGAIN    0x27
#define ADE7758_BIGAIN    0x28
#define ADE7758_CIGAIN    0x29
#define ADE7758_AWG       0x2A
#define ADE7758_BWG       0x2B
#define ADE7758_CWG       0x2C
#define ADE7758_AVARG     0x2D
#define ADE7758_BVARG     0x2E
#define ADE7758_CVARG     0x2F
#define ADE7758_AVAG      0x30
#define ADE7758_BVAG      0x31
#define ADE7758_CVAG      0x32
#define ADE7758_AVRMSOS   0x33
#define ADE7758_BVRMSOS   0x34
#define ADE7758_CVRMSOS   0x35
#define ADE7758_AIRMSOS   0x36
#define ADE7758_BIRMSOS   0x37
#define ADE7758_CIRMSOS   0x38
#define ADE7758_AWAITOS   0x39
#define ADE7758_BWAITOS   0x3A
#define ADE7758_CWAITOS   0x3B
#define ADE7758_AVAROS    0x3C
#define ADE7758_BVAROS    0x3D
#define ADE7758_CVAROS    0x3E
#define ADE7758_APHCAL    0x3F
#define ADE7758_BPHCAL    0x40
#define ADE7758_CPHCAL    0x41
#define ADE7758_WDIV      0x42
#define ADE7758_VADIV     0x44
#define ADE7758_VARDIV    0x43
#define ADE7758_APCFNUM   0x45
#define ADE7758_APCFDEN   0x46
#define ADE7758_VARCFNUM  0x47
#define ADE7758_VARCFDEN  0x48
#define ADE7758_CHKSUM    0x7E
#define ADE7758_VERSION   0x7F

#define ADE7758_READ_REG(a)    a
#define ADE7758_WRITE_REG(a) ((a) | 0x80)

#define ADE7758_MAX_TX    8
#define ADE7758_MAX_RX    4
#define ADE7758_STARTUP_DELAY 1

#define ADE7758_SPI_SLOW	(u32)(300 * 1000)
#define ADE7758_SPI_BURST	(u32)(1000 * 1000)
#define ADE7758_SPI_FAST	(u32)(2000 * 1000)

#define DRIVER_NAME		"ade7758"

/**
 * struct ade7758_state - device instance specific data
 * @us:			actual spi_device
 * @work_trigger_to_ring: bh for triggered event handling
 * @inter:		used to check if new interrupt has been triggered
 * @last_timestamp:	passing timestamp from th to bh of interrupt handler
 * @indio_dev:		industrial I/O device structure
 * @trig:		data ready trigger registered with iio
 * @tx:			transmit buffer
 * @rx:			receive buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct ade7758_state {
	struct spi_device		*us;
	struct work_struct		work_trigger_to_ring;
	s64				last_timestamp;
	struct iio_dev			*indio_dev;
	struct iio_trigger		*trig;
	u8				*tx;
	u8				*rx;
	struct mutex			buf_lock;
};
#ifdef CONFIG_IIO_RING_BUFFER
/* At the moment triggers are only used for ring buffer
 * filling. This may change!
 */

enum ade7758_scan {
	ADE7758_SCAN_WFORM,
};

void ade7758_remove_trigger(struct iio_dev *indio_dev);
int ade7758_probe_trigger(struct iio_dev *indio_dev);

ssize_t ade7758_read_data_from_ring(struct device *dev,
		struct device_attribute *attr,
		char *buf);


int ade7758_configure_ring(struct iio_dev *indio_dev);
void ade7758_unconfigure_ring(struct iio_dev *indio_dev);

int ade7758_initialize_ring(struct iio_ring_buffer *ring);
void ade7758_uninitialize_ring(struct iio_ring_buffer *ring);
int ade7758_set_irq(struct device *dev, bool enable);
#else /* CONFIG_IIO_RING_BUFFER */

static inline void ade7758_remove_trigger(struct iio_dev *indio_dev)
{
}
static inline int ade7758_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
ade7758_read_data_from_ring(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return 0;
}

static int ade7758_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}
static inline void ade7758_unconfigure_ring(struct iio_dev *indio_dev)
{
}
static inline int ade7758_initialize_ring(struct iio_ring_buffer *ring)
{
	return 0;
}
static inline void ade7758_uninitialize_ring(struct iio_ring_buffer *ring)
{
}
#endif /* CONFIG_IIO_RING_BUFFER */

#endif
