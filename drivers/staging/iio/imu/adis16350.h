#ifndef SPI_ADIS16350_H_
#define SPI_ADIS16350_H_

#define ADIS16350_STARTUP_DELAY	220 /* ms */

#define ADIS16350_READ_REG(a)    a
#define ADIS16350_WRITE_REG(a) ((a) | 0x80)

#define ADIS16350_FLASH_CNT  0x00 /* Flash memory write count */
#define ADIS16350_SUPPLY_OUT 0x02 /* Power supply measurement */
#define ADIS16350_XGYRO_OUT 0x04 /* X-axis gyroscope output */
#define ADIS16350_YGYRO_OUT 0x06 /* Y-axis gyroscope output */
#define ADIS16350_ZGYRO_OUT 0x08 /* Z-axis gyroscope output */
#define ADIS16350_XACCL_OUT 0x0A /* X-axis accelerometer output */
#define ADIS16350_YACCL_OUT 0x0C /* Y-axis accelerometer output */
#define ADIS16350_ZACCL_OUT 0x0E /* Z-axis accelerometer output */
#define ADIS16350_XTEMP_OUT 0x10 /* X-axis gyroscope temperature measurement */
#define ADIS16350_YTEMP_OUT 0x12 /* Y-axis gyroscope temperature measurement */
#define ADIS16350_ZTEMP_OUT 0x14 /* Z-axis gyroscope temperature measurement */
#define ADIS16350_AUX_ADC   0x16 /* Auxiliary ADC measurement */

/* Calibration parameters */
#define ADIS16350_XGYRO_OFF 0x1A /* X-axis gyroscope bias offset factor */
#define ADIS16350_YGYRO_OFF 0x1C /* Y-axis gyroscope bias offset factor */
#define ADIS16350_ZGYRO_OFF 0x1E /* Z-axis gyroscope bias offset factor */
#define ADIS16350_XACCL_OFF 0x20 /* X-axis acceleration bias offset factor */
#define ADIS16350_YACCL_OFF 0x22 /* Y-axis acceleration bias offset factor */
#define ADIS16350_ZACCL_OFF 0x24 /* Z-axis acceleration bias offset factor */

#define ADIS16350_GPIO_CTRL 0x32 /* Auxiliary digital input/output control */
#define ADIS16350_MSC_CTRL  0x34 /* Miscellaneous control */
#define ADIS16350_SMPL_PRD  0x36 /* Internal sample period (rate) control */
#define ADIS16350_SENS_AVG  0x38 /* Dynamic range and digital filter control */
#define ADIS16350_SLP_CNT   0x3A /* Sleep mode control */
#define ADIS16350_DIAG_STAT 0x3C /* System status */

/* Alarm functions */
#define ADIS16350_GLOB_CMD  0x3E /* System command */
#define ADIS16350_ALM_MAG1  0x26 /* Alarm 1 amplitude threshold */
#define ADIS16350_ALM_MAG2  0x28 /* Alarm 2 amplitude threshold */
#define ADIS16350_ALM_SMPL1 0x2A /* Alarm 1 sample size */
#define ADIS16350_ALM_SMPL2 0x2C /* Alarm 2 sample size */
#define ADIS16350_ALM_CTRL  0x2E /* Alarm control */
#define ADIS16350_AUX_DAC   0x30 /* Auxiliary DAC data */

#define ADIS16350_ERROR_ACTIVE			(1<<14)
#define ADIS16350_NEW_DATA			(1<<15)

/* MSC_CTRL */
#define ADIS16350_MSC_CTRL_MEM_TEST		(1<<11)
#define ADIS16350_MSC_CTRL_INT_SELF_TEST	(1<<10)
#define ADIS16350_MSC_CTRL_NEG_SELF_TEST	(1<<9)
#define ADIS16350_MSC_CTRL_POS_SELF_TEST	(1<<8)
#define ADIS16350_MSC_CTRL_GYRO_BIAS		(1<<7)
#define ADIS16350_MSC_CTRL_ACCL_ALIGN		(1<<6)
#define ADIS16350_MSC_CTRL_DATA_RDY_EN		(1<<2)
#define ADIS16350_MSC_CTRL_DATA_RDY_POL_HIGH	(1<<1)
#define ADIS16350_MSC_CTRL_DATA_RDY_DIO2	(1<<0)

/* SMPL_PRD */
#define ADIS16350_SMPL_PRD_TIME_BASE	(1<<7)
#define ADIS16350_SMPL_PRD_DIV_MASK	0x7F

/* DIAG_STAT */
#define ADIS16350_DIAG_STAT_ZACCL_FAIL	(1<<15)
#define ADIS16350_DIAG_STAT_YACCL_FAIL	(1<<14)
#define ADIS16350_DIAG_STAT_XACCL_FAIL	(1<<13)
#define ADIS16350_DIAG_STAT_XGYRO_FAIL	(1<<12)
#define ADIS16350_DIAG_STAT_YGYRO_FAIL	(1<<11)
#define ADIS16350_DIAG_STAT_ZGYRO_FAIL	(1<<10)
#define ADIS16350_DIAG_STAT_ALARM2	(1<<9)
#define ADIS16350_DIAG_STAT_ALARM1	(1<<8)
#define ADIS16350_DIAG_STAT_FLASH_CHK	(1<<6)
#define ADIS16350_DIAG_STAT_SELF_TEST	(1<<5)
#define ADIS16350_DIAG_STAT_OVERFLOW	(1<<4)
#define ADIS16350_DIAG_STAT_SPI_FAIL	(1<<3)
#define ADIS16350_DIAG_STAT_FLASH_UPT	(1<<2)
#define ADIS16350_DIAG_STAT_POWER_HIGH	(1<<1)
#define ADIS16350_DIAG_STAT_POWER_LOW	(1<<0)

/* GLOB_CMD */
#define ADIS16350_GLOB_CMD_SW_RESET	(1<<7)
#define ADIS16350_GLOB_CMD_P_AUTO_NULL	(1<<4)
#define ADIS16350_GLOB_CMD_FLASH_UPD	(1<<3)
#define ADIS16350_GLOB_CMD_DAC_LATCH	(1<<2)
#define ADIS16350_GLOB_CMD_FAC_CALIB	(1<<1)
#define ADIS16350_GLOB_CMD_AUTO_NULL	(1<<0)

/* SLP_CNT */
#define ADIS16350_SLP_CNT_POWER_OFF	(1<<8)

#define ADIS16350_MAX_TX 24
#define ADIS16350_MAX_RX 24

#define ADIS16350_SPI_SLOW	(u32)(300 * 1000)
#define ADIS16350_SPI_BURST	(u32)(1000 * 1000)
#define ADIS16350_SPI_FAST	(u32)(2000 * 1000)

/**
 * struct adis16350_state - device instance specific data
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
struct adis16350_state {
	struct spi_device		*us;
	struct work_struct		work_trigger_to_ring;
	s64				last_timestamp;
	struct iio_dev			*indio_dev;
	struct iio_trigger		*trig;
	u8				*tx;
	u8				*rx;
	struct mutex			buf_lock;
};

int adis16350_set_irq(struct device *dev, bool enable);

#ifdef CONFIG_IIO_RING_BUFFER

#define ADIS16350_SCAN_SUPPLY	0
#define ADIS16350_SCAN_GYRO_X	1
#define ADIS16350_SCAN_GYRO_Y	2
#define ADIS16350_SCAN_GYRO_Z	3
#define ADIS16350_SCAN_ACC_X	4
#define ADIS16350_SCAN_ACC_Y	5
#define ADIS16350_SCAN_ACC_Z	6
#define ADIS16350_SCAN_TEMP_X	7
#define ADIS16350_SCAN_TEMP_Y	8
#define ADIS16350_SCAN_TEMP_Z	9
#define ADIS16350_SCAN_ADC_0	10

void adis16350_remove_trigger(struct iio_dev *indio_dev);
int adis16350_probe_trigger(struct iio_dev *indio_dev);

ssize_t adis16350_read_data_from_ring(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);


int adis16350_configure_ring(struct iio_dev *indio_dev);
void adis16350_unconfigure_ring(struct iio_dev *indio_dev);

#else /* CONFIG_IIO_RING_BUFFER */

static inline void adis16350_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int adis16350_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
adis16350_read_data_from_ring(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static inline int adis16350_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis16350_unconfigure_ring(struct iio_dev *indio_dev)
{
}
#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* SPI_ADIS16350_H_ */
