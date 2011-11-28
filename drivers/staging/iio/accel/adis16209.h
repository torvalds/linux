#ifndef SPI_ADIS16209_H_
#define SPI_ADIS16209_H_

#define ADIS16209_STARTUP_DELAY	220 /* ms */

#define ADIS16209_READ_REG(a)    a
#define ADIS16209_WRITE_REG(a) ((a) | 0x80)

/* Flash memory write count */
#define ADIS16209_FLASH_CNT      0x00
/* Output, power supply */
#define ADIS16209_SUPPLY_OUT     0x02
/* Output, x-axis accelerometer */
#define ADIS16209_XACCL_OUT      0x04
/* Output, y-axis accelerometer */
#define ADIS16209_YACCL_OUT      0x06
/* Output, auxiliary ADC input */
#define ADIS16209_AUX_ADC        0x08
/* Output, temperature */
#define ADIS16209_TEMP_OUT       0x0A
/* Output, x-axis inclination */
#define ADIS16209_XINCL_OUT      0x0C
/* Output, y-axis inclination */
#define ADIS16209_YINCL_OUT      0x0E
/* Output, +/-180 vertical rotational position */
#define ADIS16209_ROT_OUT        0x10
/* Calibration, x-axis acceleration offset null */
#define ADIS16209_XACCL_NULL     0x12
/* Calibration, y-axis acceleration offset null */
#define ADIS16209_YACCL_NULL     0x14
/* Calibration, x-axis inclination offset null */
#define ADIS16209_XINCL_NULL     0x16
/* Calibration, y-axis inclination offset null */
#define ADIS16209_YINCL_NULL     0x18
/* Calibration, vertical rotation offset null */
#define ADIS16209_ROT_NULL       0x1A
/* Alarm 1 amplitude threshold */
#define ADIS16209_ALM_MAG1       0x20
/* Alarm 2 amplitude threshold */
#define ADIS16209_ALM_MAG2       0x22
/* Alarm 1, sample period */
#define ADIS16209_ALM_SMPL1      0x24
/* Alarm 2, sample period */
#define ADIS16209_ALM_SMPL2      0x26
/* Alarm control */
#define ADIS16209_ALM_CTRL       0x28
/* Auxiliary DAC data */
#define ADIS16209_AUX_DAC        0x30
/* General-purpose digital input/output control */
#define ADIS16209_GPIO_CTRL      0x32
/* Miscellaneous control */
#define ADIS16209_MSC_CTRL       0x34
/* Internal sample period (rate) control */
#define ADIS16209_SMPL_PRD       0x36
/* Operation, filter configuration */
#define ADIS16209_AVG_CNT        0x38
/* Operation, sleep mode control */
#define ADIS16209_SLP_CNT        0x3A
/* Diagnostics, system status register */
#define ADIS16209_DIAG_STAT      0x3C
/* Operation, system command register */
#define ADIS16209_GLOB_CMD       0x3E

#define ADIS16209_OUTPUTS        8

/* MSC_CTRL */
/* Self-test at power-on: 1 = disabled, 0 = enabled */
#define ADIS16209_MSC_CTRL_PWRUP_SELF_TEST	(1 << 10)
/* Self-test enable */
#define ADIS16209_MSC_CTRL_SELF_TEST_EN	        (1 << 8)
/* Data-ready enable: 1 = enabled, 0 = disabled */
#define ADIS16209_MSC_CTRL_DATA_RDY_EN	        (1 << 2)
/* Data-ready polarity: 1 = active high, 0 = active low */
#define ADIS16209_MSC_CTRL_ACTIVE_HIGH	        (1 << 1)
/* Data-ready line selection: 1 = DIO2, 0 = DIO1 */
#define ADIS16209_MSC_CTRL_DATA_RDY_DIO2	(1 << 0)

/* DIAG_STAT */
/* Alarm 2 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16209_DIAG_STAT_ALARM2        (1<<9)
/* Alarm 1 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16209_DIAG_STAT_ALARM1        (1<<8)
/* Self-test diagnostic error flag: 1 = error condition, 0 = normal operation */
#define ADIS16209_DIAG_STAT_SELFTEST_FAIL (1<<5)
/* SPI communications failure */
#define ADIS16209_DIAG_STAT_SPI_FAIL	  (1<<3)
/* Flash update failure */
#define ADIS16209_DIAG_STAT_FLASH_UPT	  (1<<2)
/* Power supply above 3.625 V */
#define ADIS16209_DIAG_STAT_POWER_HIGH	  (1<<1)
/* Power supply below 3.15 V */
#define ADIS16209_DIAG_STAT_POWER_LOW	  (1<<0)

/* GLOB_CMD */
#define ADIS16209_GLOB_CMD_SW_RESET	(1<<7)
#define ADIS16209_GLOB_CMD_CLEAR_STAT	(1<<4)
#define ADIS16209_GLOB_CMD_FACTORY_CAL	(1<<1)

#define ADIS16209_MAX_TX 24
#define ADIS16209_MAX_RX 24

#define ADIS16209_ERROR_ACTIVE          (1<<14)

/**
 * struct adis16209_state - device instance specific data
 * @us:			actual spi_device
 * @trig:		data ready trigger registered with iio
 * @tx:			transmit buffer
 * @rx:			receive buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16209_state {
	struct spi_device	*us;
	struct iio_trigger	*trig;
	struct mutex		buf_lock;
	u8			tx[ADIS16209_MAX_TX] ____cacheline_aligned;
	u8			rx[ADIS16209_MAX_RX];
};

int adis16209_set_irq(struct iio_dev *indio_dev, bool enable);

#define ADIS16209_SCAN_SUPPLY	0
#define ADIS16209_SCAN_ACC_X	1
#define ADIS16209_SCAN_ACC_Y	2
#define ADIS16209_SCAN_AUX_ADC	3
#define ADIS16209_SCAN_TEMP	4
#define ADIS16209_SCAN_INCLI_X	5
#define ADIS16209_SCAN_INCLI_Y	6
#define ADIS16209_SCAN_ROT	7

#ifdef CONFIG_IIO_BUFFER

void adis16209_remove_trigger(struct iio_dev *indio_dev);
int adis16209_probe_trigger(struct iio_dev *indio_dev);

ssize_t adis16209_read_data_from_ring(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);

int adis16209_configure_ring(struct iio_dev *indio_dev);
void adis16209_unconfigure_ring(struct iio_dev *indio_dev);

#else /* CONFIG_IIO_BUFFER */

static inline void adis16209_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int adis16209_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
adis16209_read_data_from_ring(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static int adis16209_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis16209_unconfigure_ring(struct iio_dev *indio_dev)
{
}

#endif /* CONFIG_IIO_BUFFER */
#endif /* SPI_ADIS16209_H_ */
