#ifndef SPI_ADXRS450_H_
#define SPI_ADXRS450_H_

#define ADXRS450_STARTUP_DELAY	50 /* ms */

/* The MSB for the spi commands */
#define ADXRS450_SENSOR_DATA    0x20
#define ADXRS450_WRITE_DATA	0x40
#define ADXRS450_READ_DATA	0x80

#define ADXRS450_RATE1	0x00	/* Rate Registers */
#define ADXRS450_TEMP1	0x02	/* Temperature Registers */
#define ADXRS450_LOCST1	0x04	/* Low CST Memory Registers */
#define ADXRS450_HICST1	0x06	/* High CST Memory Registers */
#define ADXRS450_QUAD1	0x08	/* Quad Memory Registers */
#define ADXRS450_FAULT1	0x0A	/* Fault Registers */
#define ADXRS450_PID1	0x0C	/* Part ID Register 1 */
#define ADXRS450_SNH	0x0E	/* Serial Number Registers, 4 bytes */
#define ADXRS450_SNL	0x10
#define ADXRS450_DNC1	0x12	/* Dynamic Null Correction Registers */
/* Check bits */
#define ADXRS450_P	0x01
#define ADXRS450_CHK	0x02
#define ADXRS450_CST	0x04
#define ADXRS450_PWR	0x08
#define ADXRS450_POR	0x10
#define ADXRS450_NVM	0x20
#define ADXRS450_Q	0x40
#define ADXRS450_PLL	0x80
#define ADXRS450_UV	0x100
#define ADXRS450_OV	0x200
#define ADXRS450_AMP	0x400
#define ADXRS450_FAIL	0x800

#define ADXRS450_WRERR_MASK	(0x7 << 29)

#define ADXRS450_MAX_RX 4
#define ADXRS450_MAX_TX 4

#define ADXRS450_GET_ST(a)	((a >> 26) & 0x3)

enum {
	ID_ADXRS450,
	ID_ADXRS453,
};

/**
 * struct adxrs450_state - device instance specific data
 * @us:			actual spi_device
 * @buf_lock:		mutex to protect tx and rx
 * @tx:			transmit buffer
 * @rx:			receive buffer
 **/
struct adxrs450_state {
	struct spi_device	*us;
	struct mutex		buf_lock;
	u8			tx[ADXRS450_MAX_RX] ____cacheline_aligned;
	u8			rx[ADXRS450_MAX_TX];

};

#endif /* SPI_ADXRS450_H_ */
