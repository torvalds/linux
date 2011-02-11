#ifndef SPI_ADIS16060_H_
#define SPI_ADIS16060_H_

#define ADIS16060_GYRO       0x20 /* Measure Angular Rate (Gyro) */
#define ADIS16060_SUPPLY_OUT 0x10 /* Measure Temperature */
#define ADIS16060_AIN2       0x80 /* Measure AIN2 */
#define ADIS16060_AIN1       0x40 /* Measure AIN1 */
#define ADIS16060_TEMP_OUT   0x22 /* Set Positive Self-Test and Output for Angular Rate */
#define ADIS16060_ANGL_OUT   0x21 /* Set Negative Self-Test and Output for Angular Rate */

#define ADIS16060_MAX_TX     3
#define ADIS16060_MAX_RX     3

/**
 * struct adis16060_state - device instance specific data
 * @us_w:			actual spi_device to write data
 * @indio_dev:		industrial I/O device structure
 * @tx:			transmit buffer
 * @rx:			recieve buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16060_state {
	struct spi_device		*us_w;
	struct spi_device		*us_r;
	struct iio_dev			*indio_dev;
	u8				*tx;
	u8				*rx;
	struct mutex			buf_lock;
};

#endif /* SPI_ADIS16060_H_ */
