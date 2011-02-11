#ifndef SPI_ADIS16080_H_
#define SPI_ADIS16080_H_

/* Output data format setting. 0: Twos complement. 1: Offset binary. */
#define ADIS16080_DIN_CODE   4
#define ADIS16080_DIN_GYRO   (0 << 10) /* Gyroscope output */
#define ADIS16080_DIN_TEMP   (1 << 10) /* Temperature output */
#define ADIS16080_DIN_AIN1   (2 << 10)
#define ADIS16080_DIN_AIN2   (3 << 10)

/*
 * 1: Write contents on DIN to control register.
 * 0: No changes to control register.
 */

#define ADIS16080_DIN_WRITE  (1 << 15)

#define ADIS16080_MAX_TX     2
#define ADIS16080_MAX_RX     2

/**
 * struct adis16080_state - device instance specific data
 * @us:			actual spi_device to write data
 * @indio_dev:		industrial I/O device structure
 * @tx:			transmit buffer
 * @rx:			recieve buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16080_state {
	struct spi_device		*us;
	struct iio_dev			*indio_dev;
	u8				*tx;
	u8				*rx;
	struct mutex			buf_lock;
};
#endif /* SPI_ADIS16080_H_ */
