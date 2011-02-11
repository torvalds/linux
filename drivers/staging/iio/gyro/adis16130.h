#ifndef SPI_ADIS16130_H_
#define SPI_ADIS16130_H_

#define ADIS16130_CON         0x0
#define ADIS16130_CON_RD      (1 << 6)
#define ADIS16130_IOP         0x1

/* 1 = data-ready signal low when unread data on all channels; */
#define ADIS16130_IOP_ALL_RDY (1 << 3)
#define ADIS16130_IOP_SYNC    (1 << 0) /* 1 = synchronization enabled */
#define ADIS16130_RATEDATA    0x8 /* Gyroscope output, rate of rotation */
#define ADIS16130_TEMPDATA    0xA /* Temperature output */
#define ADIS16130_RATECS      0x28 /* Gyroscope channel setup */
#define ADIS16130_RATECS_EN   (1 << 3) /* 1 = channel enable; */
#define ADIS16130_TEMPCS      0x2A /* Temperature channel setup */
#define ADIS16130_TEMPCS_EN   (1 << 3)
#define ADIS16130_RATECONV    0x30
#define ADIS16130_TEMPCONV    0x32
#define ADIS16130_MODE        0x38
#define ADIS16130_MODE_24BIT  (1 << 1) /* 1 = 24-bit resolution; */

#define ADIS16130_MAX_TX     4
#define ADIS16130_MAX_RX     4

/**
 * struct adis16130_state - device instance specific data
 * @us:			actual spi_device to write data
 * @indio_dev:		industrial I/O device structure
 * @tx:			transmit buffer
 * @rx:			recieve buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16130_state {
	struct spi_device		*us;
	struct iio_dev			*indio_dev;
	u8				*tx;
	u8				*rx;
	u32                             mode; /* 1: 24bits mode 0:16bits mode */
	struct mutex			buf_lock;
};

#endif /* SPI_ADIS16130_H_ */
