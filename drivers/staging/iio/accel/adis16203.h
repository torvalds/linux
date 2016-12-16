#ifndef SPI_ADIS16203_H_
#define SPI_ADIS16203_H_

#define ADIS16203_STARTUP_DELAY	220 /* ms */

/* Flash memory write count */
#define ADIS16203_FLASH_CNT      0x00

/* Output, power supply */
#define ADIS16203_SUPPLY_OUT     0x02

/* Output, auxiliary ADC input */
#define ADIS16203_AUX_ADC        0x08

/* Output, temperature */
#define ADIS16203_TEMP_OUT       0x0A

/* Output, x-axis inclination */
#define ADIS16203_XINCL_OUT      0x0C

/* Output, y-axis inclination */
#define ADIS16203_YINCL_OUT      0x0E

/* Incline null calibration */
#define ADIS16203_INCL_NULL      0x18

/* Alarm 1 amplitude threshold */
#define ADIS16203_ALM_MAG1       0x20

/* Alarm 2 amplitude threshold */
#define ADIS16203_ALM_MAG2       0x22

/* Alarm 1, sample period */
#define ADIS16203_ALM_SMPL1      0x24

/* Alarm 2, sample period */
#define ADIS16203_ALM_SMPL2      0x26

/* Alarm control */
#define ADIS16203_ALM_CTRL       0x28

/* Auxiliary DAC data */
#define ADIS16203_AUX_DAC        0x30

/* General-purpose digital input/output control */
#define ADIS16203_GPIO_CTRL      0x32

/* Miscellaneous control */
#define ADIS16203_MSC_CTRL       0x34

/* Internal sample period (rate) control */
#define ADIS16203_SMPL_PRD       0x36

/* Operation, filter configuration */
#define ADIS16203_AVG_CNT        0x38

/* Operation, sleep mode control */
#define ADIS16203_SLP_CNT        0x3A

/* Diagnostics, system status register */
#define ADIS16203_DIAG_STAT      0x3C

/* Operation, system command register */
#define ADIS16203_GLOB_CMD       0x3E

/* MSC_CTRL */

/* Self-test at power-on: 1 = disabled, 0 = enabled */
#define ADIS16203_MSC_CTRL_PWRUP_SELF_TEST	BIT(10)

/* Reverses rotation of both inclination outputs */
#define ADIS16203_MSC_CTRL_REVERSE_ROT_EN	BIT(9)

/* Self-test enable */
#define ADIS16203_MSC_CTRL_SELF_TEST_EN	        BIT(8)

/* Data-ready enable: 1 = enabled, 0 = disabled */
#define ADIS16203_MSC_CTRL_DATA_RDY_EN	        BIT(2)

/* Data-ready polarity: 1 = active high, 0 = active low */
#define ADIS16203_MSC_CTRL_ACTIVE_HIGH	        BIT(1)

/* Data-ready line selection: 1 = DIO1, 0 = DIO0 */
#define ADIS16203_MSC_CTRL_DATA_RDY_DIO1	BIT(0)

/* DIAG_STAT */

/* Alarm 2 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16203_DIAG_STAT_ALARM2        BIT(9)

/* Alarm 1 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16203_DIAG_STAT_ALARM1        BIT(8)

/* Self-test diagnostic error flag */
#define ADIS16203_DIAG_STAT_SELFTEST_FAIL_BIT 5

/* SPI communications failure */
#define ADIS16203_DIAG_STAT_SPI_FAIL_BIT      3

/* Flash update failure */
#define ADIS16203_DIAG_STAT_FLASH_UPT_BIT     2

/* Power supply above 3.625 V */
#define ADIS16203_DIAG_STAT_POWER_HIGH_BIT    1

/* Power supply below 3.15 V */
#define ADIS16203_DIAG_STAT_POWER_LOW_BIT     0

/* GLOB_CMD */

#define ADIS16203_GLOB_CMD_SW_RESET	BIT(7)
#define ADIS16203_GLOB_CMD_CLEAR_STAT	BIT(4)
#define ADIS16203_GLOB_CMD_FACTORY_CAL	BIT(1)

#define ADIS16203_ERROR_ACTIVE          BIT(14)

enum adis16203_scan {
	ADIS16203_SCAN_INCLI_X,
	ADIS16203_SCAN_INCLI_Y,
	ADIS16203_SCAN_SUPPLY,
	ADIS16203_SCAN_AUX_ADC,
	ADIS16203_SCAN_TEMP,
};

#endif /* SPI_ADIS16203_H_ */
