#ifndef SPI_ADIS16203_H_
#define SPI_ADIS16203_H_

#define ADIS16203_STARTUP_DELAY	220 /* ms */

#define ADIS16203_READ_REG(a)    a
#define ADIS16203_WRITE_REG(a) ((a) | 0x80)

#define ADIS16203_FLASH_CNT      0x00 /* Flash memory write count */
#define ADIS16203_SUPPLY_OUT     0x02 /* Output, power supply */
#define ADIS16203_AUX_ADC        0x08 /* Output, auxiliary ADC input */
#define ADIS16203_TEMP_OUT       0x0A /* Output, temperature */
#define ADIS16203_XINCL_OUT      0x0C /* Output, x-axis inclination */
#define ADIS16203_YINCL_OUT      0x0E /* Output, y-axis inclination */
#define ADIS16203_INCL_NULL      0x18 /* Incline null calibration */
#define ADIS16203_ALM_MAG1       0x20 /* Alarm 1 amplitude threshold */
#define ADIS16203_ALM_MAG2       0x22 /* Alarm 2 amplitude threshold */
#define ADIS16203_ALM_SMPL1      0x24 /* Alarm 1, sample period */
#define ADIS16203_ALM_SMPL2      0x26 /* Alarm 2, sample period */
#define ADIS16203_ALM_CTRL       0x28 /* Alarm control */
#define ADIS16203_AUX_DAC        0x30 /* Auxiliary DAC data */
#define ADIS16203_GPIO_CTRL      0x32 /* General-purpose digital input/output control */
#define ADIS16203_MSC_CTRL       0x34 /* Miscellaneous control */
#define ADIS16203_SMPL_PRD       0x36 /* Internal sample period (rate) control */
#define ADIS16203_AVG_CNT        0x38 /* Operation, filter configuration */
#define ADIS16203_SLP_CNT        0x3A /* Operation, sleep mode control */
#define ADIS16203_DIAG_STAT      0x3C /* Diagnostics, system status register */
#define ADIS16203_GLOB_CMD       0x3E /* Operation, system command register */

#define ADIS16203_OUTPUTS        5

/* MSC_CTRL */
#define ADIS16203_MSC_CTRL_PWRUP_SELF_TEST	(1 << 10) /* Self-test at power-on: 1 = disabled, 0 = enabled */
#define ADIS16203_MSC_CTRL_REVERSE_ROT_EN	(1 << 9)  /* Reverses rotation of both inclination outputs */
#define ADIS16203_MSC_CTRL_SELF_TEST_EN	        (1 << 8)  /* Self-test enable */
#define ADIS16203_MSC_CTRL_DATA_RDY_EN	        (1 << 2)  /* Data-ready enable: 1 = enabled, 0 = disabled */
#define ADIS16203_MSC_CTRL_ACTIVE_HIGH	        (1 << 1)  /* Data-ready polarity: 1 = active high, 0 = active low */
#define ADIS16203_MSC_CTRL_DATA_RDY_DIO1	(1 << 0)  /* Data-ready line selection: 1 = DIO1, 0 = DIO0 */

/* DIAG_STAT */
#define ADIS16203_DIAG_STAT_ALARM2        (1<<9) /* Alarm 2 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16203_DIAG_STAT_ALARM1        (1<<8) /* Alarm 1 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16203_DIAG_STAT_SELFTEST_FAIL (1<<5) /* Self-test diagnostic error flag */
#define ADIS16203_DIAG_STAT_SPI_FAIL	  (1<<3) /* SPI communications failure */
#define ADIS16203_DIAG_STAT_FLASH_UPT	  (1<<2) /* Flash update failure */
#define ADIS16203_DIAG_STAT_POWER_HIGH	  (1<<1) /* Power supply above 3.625 V */
#define ADIS16203_DIAG_STAT_POWER_LOW	  (1<<0) /* Power supply below 3.15 V */

/* GLOB_CMD */
#define ADIS16203_GLOB_CMD_SW_RESET	(1<<7)
#define ADIS16203_GLOB_CMD_CLEAR_STAT	(1<<4)
#define ADIS16203_GLOB_CMD_FACTORY_CAL	(1<<1)

#define ADIS16203_MAX_TX 12
#define ADIS16203_MAX_RX 10

#define ADIS16203_ERROR_ACTIVE          (1<<14)

/**
 * struct adis16203_state - device instance specific data
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
struct adis16203_state {
	struct spi_device		*us;
	struct work_struct		work_trigger_to_ring;
	s64				last_timestamp;
	struct iio_dev			*indio_dev;
	struct iio_trigger		*trig;
	u8				*tx;
	u8				*rx;
	struct mutex			buf_lock;
};

int adis16203_set_irq(struct device *dev, bool enable);

#ifdef CONFIG_IIO_RING_BUFFER
enum adis16203_scan {
	ADIS16203_SCAN_SUPPLY,
	ADIS16203_SCAN_AUX_ADC,
	ADIS16203_SCAN_TEMP,
	ADIS16203_SCAN_INCLI_X,
	ADIS16203_SCAN_INCLI_Y,
};

void adis16203_remove_trigger(struct iio_dev *indio_dev);
int adis16203_probe_trigger(struct iio_dev *indio_dev);

ssize_t adis16203_read_data_from_ring(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);

int adis16203_configure_ring(struct iio_dev *indio_dev);
void adis16203_unconfigure_ring(struct iio_dev *indio_dev);

int adis16203_initialize_ring(struct iio_ring_buffer *ring);
void adis16203_uninitialize_ring(struct iio_ring_buffer *ring);
#else /* CONFIG_IIO_RING_BUFFER */

static inline void adis16203_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int adis16203_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
adis16203_read_data_from_ring(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static int adis16203_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis16203_unconfigure_ring(struct iio_dev *indio_dev)
{
}

static inline int adis16203_initialize_ring(struct iio_ring_buffer *ring)
{
	return 0;
}

static inline void adis16203_uninitialize_ring(struct iio_ring_buffer *ring)
{
}

#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* SPI_ADIS16203_H_ */
