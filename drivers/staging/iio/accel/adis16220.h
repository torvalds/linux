#ifndef SPI_ADIS16220_H_
#define SPI_ADIS16220_H_

#include <linux/iio/imu/adis.h>

#define ADIS16220_STARTUP_DELAY	220 /* ms */

/* Flash memory write count */
#define ADIS16220_FLASH_CNT     0x00

/* Control, acceleration offset adjustment control */
#define ADIS16220_ACCL_NULL     0x02

/* Control, AIN1 offset adjustment control */
#define ADIS16220_AIN1_NULL     0x04

/* Control, AIN2 offset adjustment control */
#define ADIS16220_AIN2_NULL     0x06

/* Output, power supply during capture */
#define ADIS16220_CAPT_SUPPLY   0x0A

/* Output, temperature during capture */
#define ADIS16220_CAPT_TEMP     0x0C

/* Output, peak acceleration during capture */
#define ADIS16220_CAPT_PEAKA    0x0E

/* Output, peak AIN1 level during capture */
#define ADIS16220_CAPT_PEAK1    0x10

/* Output, peak AIN2 level during capture */
#define ADIS16220_CAPT_PEAK2    0x12

/* Output, capture buffer for acceleration */
#define ADIS16220_CAPT_BUFA     0x14

/* Output, capture buffer for AIN1 */
#define ADIS16220_CAPT_BUF1     0x16

/* Output, capture buffer for AIN2 */
#define ADIS16220_CAPT_BUF2     0x18

/* Control, capture buffer address pointer */
#define ADIS16220_CAPT_PNTR     0x1A

/* Control, capture control register */
#define ADIS16220_CAPT_CTRL     0x1C

/* Control, capture period (automatic mode) */
#define ADIS16220_CAPT_PRD      0x1E

/* Control, Alarm A, acceleration peak threshold */
#define ADIS16220_ALM_MAGA      0x20

/* Control, Alarm 1, AIN1 peak threshold */
#define ADIS16220_ALM_MAG1      0x22

/* Control, Alarm 2, AIN2 peak threshold */
#define ADIS16220_ALM_MAG2      0x24

/* Control, Alarm S, peak threshold */
#define ADIS16220_ALM_MAGS      0x26

/* Control, alarm configuration register */
#define ADIS16220_ALM_CTRL      0x28

/* Control, general I/O configuration */
#define ADIS16220_GPIO_CTRL     0x32

/* Control, self-test control, AIN configuration */
#define ADIS16220_MSC_CTRL      0x34

/* Control, digital I/O configuration */
#define ADIS16220_DIO_CTRL      0x36

/* Control, filter configuration */
#define ADIS16220_AVG_CNT       0x38

/* Status, system status */
#define ADIS16220_DIAG_STAT     0x3C

/* Control, system commands */
#define ADIS16220_GLOB_CMD      0x3E

/* Status, self-test response */
#define ADIS16220_ST_DELTA      0x40

/* Lot Identification Code 1 */
#define ADIS16220_LOT_ID1       0x52

/* Lot Identification Code 2 */
#define ADIS16220_LOT_ID2       0x54

/* Product identifier; convert to decimal = 16220 */
#define ADIS16220_PROD_ID       0x56

/* Serial number */
#define ADIS16220_SERIAL_NUM    0x58

#define ADIS16220_CAPTURE_SIZE  2048

/* MSC_CTRL */

#define ADIS16220_MSC_CTRL_SELF_TEST_EN	        BIT(8)
#define ADIS16220_MSC_CTRL_POWER_SUP_COM_AIN1	BIT(1)
#define ADIS16220_MSC_CTRL_POWER_SUP_COM_AIN2	BIT(0)

/* DIO_CTRL */

#define ADIS16220_MSC_CTRL_DIO2_BUSY_IND     (BIT(5) | BIT(4))
#define ADIS16220_MSC_CTRL_DIO1_BUSY_IND     (BIT(3) | BIT(2))
#define ADIS16220_MSC_CTRL_DIO2_ACT_HIGH     BIT(1)
#define ADIS16220_MSC_CTRL_DIO1_ACT_HIGH     BIT(0)

/* DIAG_STAT */

/* AIN2 sample > ALM_MAG2 */
#define ADIS16220_DIAG_STAT_ALM_MAG2    BIT(14)

/* AIN1 sample > ALM_MAG1 */
#define ADIS16220_DIAG_STAT_ALM_MAG1    BIT(13)

/* Acceleration sample > ALM_MAGA */
#define ADIS16220_DIAG_STAT_ALM_MAGA    BIT(12)

/* Error condition programmed into ALM_MAGS[11:0] and ALM_CTRL[5:4] is true */
#define ADIS16220_DIAG_STAT_ALM_MAGS    BIT(11)

/* |Peak value in AIN2 data capture| > ALM_MAG2 */
#define ADIS16220_DIAG_STAT_PEAK_AIN2   BIT(10)

/* |Peak value in AIN1 data capture| > ALM_MAG1 */
#define ADIS16220_DIAG_STAT_PEAK_AIN1   BIT(9)

/* |Peak value in acceleration data capture| > ALM_MAGA */
#define ADIS16220_DIAG_STAT_PEAK_ACCEL  BIT(8)

/* Data ready, capture complete */
#define ADIS16220_DIAG_STAT_DATA_RDY    BIT(7)

#define ADIS16220_DIAG_STAT_FLASH_CHK	BIT(6)

#define ADIS16220_DIAG_STAT_SELF_TEST	BIT(5)

/* Capture period violation/interruption */
#define ADIS16220_DIAG_STAT_VIOLATION_BIT	4

/* SPI communications failure */
#define ADIS16220_DIAG_STAT_SPI_FAIL_BIT	3

/* Flash update failure */
#define ADIS16220_DIAG_STAT_FLASH_UPT_BIT	2

/* Power supply above 3.625 V */
#define ADIS16220_DIAG_STAT_POWER_HIGH_BIT	1

/* Power supply below 3.15 V */
#define ADIS16220_DIAG_STAT_POWER_LOW_BIT	0

/* GLOB_CMD */

#define ADIS16220_GLOB_CMD_SW_RESET	BIT(7)
#define ADIS16220_GLOB_CMD_SELF_TEST	BIT(2)
#define ADIS16220_GLOB_CMD_PWR_DOWN	BIT(1)

#define ADIS16220_MAX_TX 2048
#define ADIS16220_MAX_RX 2048

#define ADIS16220_SPI_BURST	(u32)(1000 * 1000)
#define ADIS16220_SPI_FAST	(u32)(2000 * 1000)

/**
 * struct adis16220_state - device instance specific data
 * @adis:		adis device
 * @tx:			transmit buffer
 * @rx:			receive buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16220_state {
	struct adis adis;

	struct mutex		buf_lock;
	u8			tx[ADIS16220_MAX_TX] ____cacheline_aligned;
	u8			rx[ADIS16220_MAX_RX];
};

#endif /* SPI_ADIS16220_H_ */
