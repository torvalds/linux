#ifndef SPI_ADIS16204_H_
#define SPI_ADIS16204_H_

#define ADIS16204_STARTUP_DELAY	220 /* ms */

#define ADIS16204_READ_REG(a)    a
#define ADIS16204_WRITE_REG(a) ((a) | 0x80)

#define ADIS16204_FLASH_CNT      0x00 /* Flash memory write count */
#define ADIS16204_SUPPLY_OUT     0x02 /* Output, power supply */
#define ADIS16204_XACCL_OUT      0x04 /* Output, x-axis accelerometer */
#define ADIS16204_YACCL_OUT      0x06 /* Output, y-axis accelerometer */
#define ADIS16204_AUX_ADC        0x08 /* Output, auxiliary ADC input */
#define ADIS16204_TEMP_OUT       0x0A /* Output, temperature */
#define ADIS16204_X_PEAK_OUT     0x0C /* Twos complement */
#define ADIS16204_Y_PEAK_OUT     0x0E /* Twos complement */
#define ADIS16204_XACCL_NULL     0x10 /* Calibration, x-axis acceleration offset null */
#define ADIS16204_YACCL_NULL     0x12 /* Calibration, y-axis acceleration offset null */
#define ADIS16204_XACCL_SCALE    0x14 /* X-axis scale factor calibration register */
#define ADIS16204_YACCL_SCALE    0x16 /* Y-axis scale factor calibration register */
#define ADIS16204_XY_RSS_OUT     0x18 /* XY combined acceleration (RSS) */
#define ADIS16204_XY_PEAK_OUT    0x1A /* Peak, XY combined output (RSS) */
#define ADIS16204_CAP_BUF_1      0x1C /* Capture buffer output register 1 */
#define ADIS16204_CAP_BUF_2      0x1E /* Capture buffer output register 2 */
#define ADIS16204_ALM_MAG1       0x20 /* Alarm 1 amplitude threshold */
#define ADIS16204_ALM_MAG2       0x22 /* Alarm 2 amplitude threshold */
#define ADIS16204_ALM_CTRL       0x28 /* Alarm control */
#define ADIS16204_CAPT_PNTR      0x2A /* Capture register address pointer */
#define ADIS16204_AUX_DAC        0x30 /* Auxiliary DAC data */
#define ADIS16204_GPIO_CTRL      0x32 /* General-purpose digital input/output control */
#define ADIS16204_MSC_CTRL       0x34 /* Miscellaneous control */
#define ADIS16204_SMPL_PRD       0x36 /* Internal sample period (rate) control */
#define ADIS16204_AVG_CNT        0x38 /* Operation, filter configuration */
#define ADIS16204_SLP_CNT        0x3A /* Operation, sleep mode control */
#define ADIS16204_DIAG_STAT      0x3C /* Diagnostics, system status register */
#define ADIS16204_GLOB_CMD       0x3E /* Operation, system command register */

#define ADIS16204_OUTPUTS        5

/* MSC_CTRL */
#define ADIS16204_MSC_CTRL_PWRUP_SELF_TEST	(1 << 10) /* Self-test at power-on: 1 = disabled, 0 = enabled */
#define ADIS16204_MSC_CTRL_SELF_TEST_EN	        (1 << 8)  /* Self-test enable */
#define ADIS16204_MSC_CTRL_DATA_RDY_EN	        (1 << 2)  /* Data-ready enable: 1 = enabled, 0 = disabled */
#define ADIS16204_MSC_CTRL_ACTIVE_HIGH	        (1 << 1)  /* Data-ready polarity: 1 = active high, 0 = active low */
#define ADIS16204_MSC_CTRL_DATA_RDY_DIO2	(1 << 0)  /* Data-ready line selection: 1 = DIO2, 0 = DIO1 */

/* DIAG_STAT */
#define ADIS16204_DIAG_STAT_ALARM2        (1<<9) /* Alarm 2 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16204_DIAG_STAT_ALARM1        (1<<8) /* Alarm 1 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16204_DIAG_STAT_SELFTEST_FAIL (1<<5) /* Self-test diagnostic error flag: 1 = error condition,
						0 = normal operation */
#define ADIS16204_DIAG_STAT_SPI_FAIL	  (1<<3) /* SPI communications failure */
#define ADIS16204_DIAG_STAT_FLASH_UPT	  (1<<2) /* Flash update failure */
#define ADIS16204_DIAG_STAT_POWER_HIGH	  (1<<1) /* Power supply above 3.625 V */
#define ADIS16204_DIAG_STAT_POWER_LOW	  (1<<0) /* Power supply below 2.975 V */

/* GLOB_CMD */
#define ADIS16204_GLOB_CMD_SW_RESET	(1<<7)
#define ADIS16204_GLOB_CMD_CLEAR_STAT	(1<<4)
#define ADIS16204_GLOB_CMD_FACTORY_CAL	(1<<1)

#define ADIS16204_MAX_TX 24
#define ADIS16204_MAX_RX 24

#define ADIS16204_ERROR_ACTIVE          (1<<14)

/**
 * struct adis16204_state - device instance specific data
 * @us:			actual spi_device
 * @indio_dev:		industrial I/O device structure
 * @trig:		data ready trigger registered with iio
 * @tx:			transmit buffer
 * @rx:			receive buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16204_state {
	struct spi_device		*us;
	struct iio_dev			*indio_dev;
	struct iio_trigger		*trig;
	u8				*tx;
	u8				*rx;
	struct mutex			buf_lock;
};

int adis16204_set_irq(struct iio_dev *indio_dev, bool enable);

enum adis16204_scan {
	ADIS16204_SCAN_SUPPLY,
	ADIS16204_SCAN_ACC_X,
	ADIS16204_SCAN_ACC_Y,
	ADIS16204_SCAN_AUX_ADC,
	ADIS16204_SCAN_TEMP,
};

#ifdef CONFIG_IIO_RING_BUFFER
void adis16204_remove_trigger(struct iio_dev *indio_dev);
int adis16204_probe_trigger(struct iio_dev *indio_dev);

ssize_t adis16204_read_data_from_ring(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);

int adis16204_configure_ring(struct iio_dev *indio_dev);
void adis16204_unconfigure_ring(struct iio_dev *indio_dev);

#else /* CONFIG_IIO_RING_BUFFER */

static inline void adis16204_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int adis16204_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
adis16204_read_data_from_ring(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static int adis16204_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis16204_unconfigure_ring(struct iio_dev *indio_dev)
{
}

#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* SPI_ADIS16204_H_ */
