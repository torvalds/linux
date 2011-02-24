#ifndef SPI_ADIS16251_H_
#define SPI_ADIS16251_H_

#define ADIS16251_STARTUP_DELAY	220 /* ms */

#define ADIS16251_READ_REG(a)    a
#define ADIS16251_WRITE_REG(a) ((a) | 0x80)

#define ADIS16251_ENDURANCE  0x00 /* Flash memory write count */
#define ADIS16251_SUPPLY_OUT 0x02 /* Power supply measurement */
#define ADIS16251_GYRO_OUT   0x04 /* X-axis gyroscope output */
#define ADIS16251_AUX_ADC    0x0A /* analog input channel measurement */
#define ADIS16251_TEMP_OUT   0x0C /* internal temperature measurement */
#define ADIS16251_ANGL_OUT   0x0E /* angle displacement */
#define ADIS16251_GYRO_OFF   0x14 /* Calibration, offset/bias adjustment */
#define ADIS16251_GYRO_SCALE 0x16 /* Calibration, scale adjustment */
#define ADIS16251_ALM_MAG1   0x20 /* Alarm 1 magnitude/polarity setting */
#define ADIS16251_ALM_MAG2   0x22 /* Alarm 2 magnitude/polarity setting */
#define ADIS16251_ALM_SMPL1  0x24 /* Alarm 1 dynamic rate of change setting */
#define ADIS16251_ALM_SMPL2  0x26 /* Alarm 2 dynamic rate of change setting */
#define ADIS16251_ALM_CTRL   0x28 /* Alarm control */
#define ADIS16251_AUX_DAC    0x30 /* Auxiliary DAC data */
#define ADIS16251_GPIO_CTRL  0x32 /* Control, digital I/O line */
#define ADIS16251_MSC_CTRL   0x34 /* Control, data ready, self-test settings */
#define ADIS16251_SMPL_PRD   0x36 /* Control, internal sample rate */
#define ADIS16251_SENS_AVG   0x38 /* Control, dynamic range, filtering */
#define ADIS16251_SLP_CNT    0x3A /* Control, sleep mode initiation */
#define ADIS16251_DIAG_STAT  0x3C /* Diagnostic, error flags */
#define ADIS16251_GLOB_CMD   0x3E /* Control, global commands */

#define ADIS16251_ERROR_ACTIVE			(1<<14)
#define ADIS16251_NEW_DATA			(1<<14)

/* MSC_CTRL */
#define ADIS16251_MSC_CTRL_INT_SELF_TEST	(1<<10) /* Internal self-test enable */
#define ADIS16251_MSC_CTRL_NEG_SELF_TEST	(1<<9)
#define ADIS16251_MSC_CTRL_POS_SELF_TEST	(1<<8)
#define ADIS16251_MSC_CTRL_DATA_RDY_EN		(1<<2)
#define ADIS16251_MSC_CTRL_DATA_RDY_POL_HIGH	(1<<1)
#define ADIS16251_MSC_CTRL_DATA_RDY_DIO2	(1<<0)

/* SMPL_PRD */
#define ADIS16251_SMPL_PRD_TIME_BASE	(1<<7) /* Time base (tB): 0 = 1.953 ms, 1 = 60.54 ms */
#define ADIS16251_SMPL_PRD_DIV_MASK	0x7F

/* SLP_CNT */
#define ADIS16251_SLP_CNT_POWER_OFF     0x80

/* DIAG_STAT */
#define ADIS16251_DIAG_STAT_ALARM2	(1<<9)
#define ADIS16251_DIAG_STAT_ALARM1	(1<<8)
#define ADIS16251_DIAG_STAT_SELF_TEST	(1<<5)
#define ADIS16251_DIAG_STAT_OVERFLOW	(1<<4)
#define ADIS16251_DIAG_STAT_SPI_FAIL	(1<<3)
#define ADIS16251_DIAG_STAT_FLASH_UPT	(1<<2)
#define ADIS16251_DIAG_STAT_POWER_HIGH	(1<<1)
#define ADIS16251_DIAG_STAT_POWER_LOW	(1<<0)

#define ADIS16251_DIAG_STAT_ERR_MASK (ADIS16251_DIAG_STAT_ALARM2 | \
				      ADIS16251_DIAG_STAT_ALARM1 | \
				      ADIS16251_DIAG_STAT_SELF_TEST | \
				      ADIS16251_DIAG_STAT_OVERFLOW | \
				      ADIS16251_DIAG_STAT_SPI_FAIL | \
				      ADIS16251_DIAG_STAT_FLASH_UPT | \
				      ADIS16251_DIAG_STAT_POWER_HIGH | \
				      ADIS16251_DIAG_STAT_POWER_LOW)

/* GLOB_CMD */
#define ADIS16251_GLOB_CMD_SW_RESET	(1<<7)
#define ADIS16251_GLOB_CMD_FLASH_UPD	(1<<3)
#define ADIS16251_GLOB_CMD_DAC_LATCH	(1<<2)
#define ADIS16251_GLOB_CMD_FAC_CALIB	(1<<1)
#define ADIS16251_GLOB_CMD_AUTO_NULL	(1<<0)

#define ADIS16251_MAX_TX 24
#define ADIS16251_MAX_RX 24

#define ADIS16251_SPI_SLOW	(u32)(300 * 1000)
#define ADIS16251_SPI_BURST	(u32)(1000 * 1000)
#define ADIS16251_SPI_FAST	(u32)(2000 * 1000)

/**
 * struct adis16251_state - device instance specific data
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
struct adis16251_state {
	struct spi_device		*us;
	struct work_struct		work_trigger_to_ring;
	s64				last_timestamp;
	struct iio_dev			*indio_dev;
	struct iio_trigger		*trig;
	u8				*tx;
	u8				*rx;
	struct mutex			buf_lock;
};

int adis16251_spi_write_reg_8(struct device *dev,
			      u8 reg_address,
			      u8 val);

int adis16251_spi_read_burst(struct device *dev, u8 *rx);

int adis16251_spi_read_sequence(struct device *dev,
				      u8 *tx, u8 *rx, int num);

int adis16251_set_irq(struct device *dev, bool enable);

int adis16251_reset(struct device *dev);

int adis16251_stop_device(struct device *dev);

int adis16251_check_status(struct device *dev);

#if defined(CONFIG_IIO_RING_BUFFER) && defined(THIS_HAS_RING_BUFFER_SUPPORT)
/* At the moment triggers are only used for ring buffer
 * filling. This may change!
 */

enum adis16251_scan {
	ADIS16251_SCAN_SUPPLY,
	ADIS16251_SCAN_GYRO,
	ADIS16251_SCAN_TEMP,
	ADIS16251_SCAN_ADC_0,
};

void adis16251_remove_trigger(struct iio_dev *indio_dev);
int adis16251_probe_trigger(struct iio_dev *indio_dev);

ssize_t adis16251_read_data_from_ring(struct device *dev,
				      struct device_attribute *attr,
				      char *buf);


int adis16251_configure_ring(struct iio_dev *indio_dev);
void adis16251_unconfigure_ring(struct iio_dev *indio_dev);

int adis16251_initialize_ring(struct iio_ring_buffer *ring);
void adis16251_uninitialize_ring(struct iio_ring_buffer *ring);
#else /* CONFIG_IIO_RING_BUFFER */

static inline void adis16251_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int adis16251_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline ssize_t
adis16251_read_data_from_ring(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return 0;
}

static int adis16251_configure_ring(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis16251_unconfigure_ring(struct iio_dev *indio_dev)
{
}

static inline int adis16251_initialize_ring(struct iio_ring_buffer *ring)
{
	return 0;
}

static inline void adis16251_uninitialize_ring(struct iio_ring_buffer *ring)
{
}

#endif /* CONFIG_IIO_RING_BUFFER */
#endif /* SPI_ADIS16251_H_ */
