#ifndef SPI_ADIS16204_H_
#define SPI_ADIS16204_H_

#define ADIS16204_STARTUP_DELAY	220 /* ms */

/* Flash memory write count */
#define ADIS16204_FLASH_CNT      0x00

/* Output, power supply */
#define ADIS16204_SUPPLY_OUT     0x02

/* Output, x-axis accelerometer */
#define ADIS16204_XACCL_OUT      0x04

/* Output, y-axis accelerometer */
#define ADIS16204_YACCL_OUT      0x06

/* Output, auxiliary ADC input */
#define ADIS16204_AUX_ADC        0x08

/* Output, temperature */
#define ADIS16204_TEMP_OUT       0x0A

/* Twos complement */
#define ADIS16204_X_PEAK_OUT     0x0C
#define ADIS16204_Y_PEAK_OUT     0x0E

/* Calibration, x-axis acceleration offset null */
#define ADIS16204_XACCL_NULL     0x10

/* Calibration, y-axis acceleration offset null */
#define ADIS16204_YACCL_NULL     0x12

/* X-axis scale factor calibration register */
#define ADIS16204_XACCL_SCALE    0x14

/* Y-axis scale factor calibration register */
#define ADIS16204_YACCL_SCALE    0x16

/* XY combined acceleration (RSS) */
#define ADIS16204_XY_RSS_OUT     0x18

/* Peak, XY combined output (RSS) */
#define ADIS16204_XY_PEAK_OUT    0x1A

/* Capture buffer output register 1 */
#define ADIS16204_CAP_BUF_1      0x1C

/* Capture buffer output register 2 */
#define ADIS16204_CAP_BUF_2      0x1E

/* Alarm 1 amplitude threshold */
#define ADIS16204_ALM_MAG1       0x20

/* Alarm 2 amplitude threshold */
#define ADIS16204_ALM_MAG2       0x22

/* Alarm control */
#define ADIS16204_ALM_CTRL       0x28

/* Capture register address pointer */
#define ADIS16204_CAPT_PNTR      0x2A

/* Auxiliary DAC data */
#define ADIS16204_AUX_DAC        0x30

/* General-purpose digital input/output control */
#define ADIS16204_GPIO_CTRL      0x32

/* Miscellaneous control */
#define ADIS16204_MSC_CTRL       0x34

/* Internal sample period (rate) control */
#define ADIS16204_SMPL_PRD       0x36

/* Operation, filter configuration */
#define ADIS16204_AVG_CNT        0x38

/* Operation, sleep mode control */
#define ADIS16204_SLP_CNT        0x3A

/* Diagnostics, system status register */
#define ADIS16204_DIAG_STAT      0x3C

/* Operation, system command register */
#define ADIS16204_GLOB_CMD       0x3E

/* MSC_CTRL */

/* Self-test at power-on: 1 = disabled, 0 = enabled */
#define ADIS16204_MSC_CTRL_PWRUP_SELF_TEST	BIT(10)

/* Self-test enable */
#define ADIS16204_MSC_CTRL_SELF_TEST_EN	        BIT(8)

/* Data-ready enable: 1 = enabled, 0 = disabled */
#define ADIS16204_MSC_CTRL_DATA_RDY_EN	        BIT(2)

/* Data-ready polarity: 1 = active high, 0 = active low */
#define ADIS16204_MSC_CTRL_ACTIVE_HIGH	        BIT(1)

/* Data-ready line selection: 1 = DIO2, 0 = DIO1 */
#define ADIS16204_MSC_CTRL_DATA_RDY_DIO2	BIT(0)

/* DIAG_STAT */

/* Alarm 2 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16204_DIAG_STAT_ALARM2        BIT(9)

/* Alarm 1 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16204_DIAG_STAT_ALARM1        BIT(8)

/* Self-test diagnostic error flag: 1 = error condition, 0 = normal operation */
#define ADIS16204_DIAG_STAT_SELFTEST_FAIL_BIT 5

/* SPI communications failure */
#define ADIS16204_DIAG_STAT_SPI_FAIL_BIT      3

/* Flash update failure */
#define ADIS16204_DIAG_STAT_FLASH_UPT_BIT     2

/* Power supply above 3.625 V */
#define ADIS16204_DIAG_STAT_POWER_HIGH_BIT    1

/* Power supply below 2.975 V */
#define ADIS16204_DIAG_STAT_POWER_LOW_BIT     0

/* GLOB_CMD */

#define ADIS16204_GLOB_CMD_SW_RESET	BIT(7)
#define ADIS16204_GLOB_CMD_CLEAR_STAT	BIT(4)
#define ADIS16204_GLOB_CMD_FACTORY_CAL	BIT(1)

#define ADIS16204_ERROR_ACTIVE          BIT(14)

enum adis16204_scan {
	ADIS16204_SCAN_ACC_X,
	ADIS16204_SCAN_ACC_Y,
	ADIS16204_SCAN_ACC_XY,
	ADIS16204_SCAN_SUPPLY,
	ADIS16204_SCAN_AUX_ADC,
	ADIS16204_SCAN_TEMP,
};

#endif /* SPI_ADIS16204_H_ */
