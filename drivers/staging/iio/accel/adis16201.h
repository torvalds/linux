#ifndef SPI_ADIS16201_H_
#define SPI_ADIS16201_H_

#define ADIS16201_STARTUP_DELAY	220 /* ms */

#define ADIS16201_FLASH_CNT      0x00 /* Flash memory write count */
#define ADIS16201_SUPPLY_OUT     0x02 /* Output, power supply */
#define ADIS16201_XACCL_OUT      0x04 /* Output, x-axis accelerometer */
#define ADIS16201_YACCL_OUT      0x06 /* Output, y-axis accelerometer */
#define ADIS16201_AUX_ADC        0x08 /* Output, auxiliary ADC input */
#define ADIS16201_TEMP_OUT       0x0A /* Output, temperature */
#define ADIS16201_XINCL_OUT      0x0C /* Output, x-axis inclination */
#define ADIS16201_YINCL_OUT      0x0E /* Output, y-axis inclination */
#define ADIS16201_XACCL_OFFS     0x10 /* Calibration, x-axis acceleration offset */
#define ADIS16201_YACCL_OFFS     0x12 /* Calibration, y-axis acceleration offset */
#define ADIS16201_XACCL_SCALE    0x14 /* x-axis acceleration scale factor */
#define ADIS16201_YACCL_SCALE    0x16 /* y-axis acceleration scale factor */
#define ADIS16201_XINCL_OFFS     0x18 /* Calibration, x-axis inclination offset */
#define ADIS16201_YINCL_OFFS     0x1A /* Calibration, y-axis inclination offset */
#define ADIS16201_XINCL_SCALE    0x1C /* x-axis inclination scale factor */
#define ADIS16201_YINCL_SCALE    0x1E /* y-axis inclination scale factor */
#define ADIS16201_ALM_MAG1       0x20 /* Alarm 1 amplitude threshold */
#define ADIS16201_ALM_MAG2       0x22 /* Alarm 2 amplitude threshold */
#define ADIS16201_ALM_SMPL1      0x24 /* Alarm 1, sample period */
#define ADIS16201_ALM_SMPL2      0x26 /* Alarm 2, sample period */
#define ADIS16201_ALM_CTRL       0x28 /* Alarm control */
#define ADIS16201_AUX_DAC        0x30 /* Auxiliary DAC data */
#define ADIS16201_GPIO_CTRL      0x32 /* General-purpose digital input/output control */
#define ADIS16201_MSC_CTRL       0x34 /* Miscellaneous control */
#define ADIS16201_SMPL_PRD       0x36 /* Internal sample period (rate) control */
#define ADIS16201_AVG_CNT        0x38 /* Operation, filter configuration */
#define ADIS16201_SLP_CNT        0x3A /* Operation, sleep mode control */
#define ADIS16201_DIAG_STAT      0x3C /* Diagnostics, system status register */
#define ADIS16201_GLOB_CMD       0x3E /* Operation, system command register */

/* MSC_CTRL */
#define ADIS16201_MSC_CTRL_SELF_TEST_EN	        BIT(8)  /* Self-test enable */
#define ADIS16201_MSC_CTRL_DATA_RDY_EN	        BIT(2)  /* Data-ready enable: 1 = enabled, 0 = disabled */
#define ADIS16201_MSC_CTRL_ACTIVE_HIGH	        BIT(1)  /* Data-ready polarity: 1 = active high, 0 = active low */
#define ADIS16201_MSC_CTRL_DATA_RDY_DIO1	BIT(0)  /* Data-ready line selection: 1 = DIO1, 0 = DIO0 */

/* DIAG_STAT */
#define ADIS16201_DIAG_STAT_ALARM2        BIT(9) /* Alarm 2 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16201_DIAG_STAT_ALARM1        BIT(8) /* Alarm 1 status: 1 = alarm active, 0 = alarm inactive */
#define ADIS16201_DIAG_STAT_SPI_FAIL_BIT   3 /* SPI communications failure */
#define ADIS16201_DIAG_STAT_FLASH_UPT_BIT  2 /* Flash update failure */
#define ADIS16201_DIAG_STAT_POWER_HIGH_BIT 1 /* Power supply above 3.625 V */
#define ADIS16201_DIAG_STAT_POWER_LOW_BIT  0 /* Power supply below 3.15 V */

/* GLOB_CMD */
#define ADIS16201_GLOB_CMD_SW_RESET	BIT(7)
#define ADIS16201_GLOB_CMD_FACTORY_CAL	BIT(1)

#define ADIS16201_ERROR_ACTIVE          BIT(14)

enum adis16201_scan {
	ADIS16201_SCAN_ACC_X,
	ADIS16201_SCAN_ACC_Y,
	ADIS16201_SCAN_INCLI_X,
	ADIS16201_SCAN_INCLI_Y,
	ADIS16201_SCAN_SUPPLY,
	ADIS16201_SCAN_AUX_ADC,
	ADIS16201_SCAN_TEMP,
};

#endif /* SPI_ADIS16201_H_ */
