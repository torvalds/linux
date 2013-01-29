#ifndef SPI_ADIS16260_H_
#define SPI_ADIS16260_H_

#include "adis16260_platform_data.h"
#include <linux/iio/imu/adis.h>

#define ADIS16260_STARTUP_DELAY	220 /* ms */

#define ADIS16260_FLASH_CNT  0x00 /* Flash memory write count */
#define ADIS16260_SUPPLY_OUT 0x02 /* Power supply measurement */
#define ADIS16260_GYRO_OUT   0x04 /* X-axis gyroscope output */
#define ADIS16260_AUX_ADC    0x0A /* analog input channel measurement */
#define ADIS16260_TEMP_OUT   0x0C /* internal temperature measurement */
#define ADIS16260_ANGL_OUT   0x0E /* angle displacement */
#define ADIS16260_GYRO_OFF   0x14 /* Calibration, offset/bias adjustment */
#define ADIS16260_GYRO_SCALE 0x16 /* Calibration, scale adjustment */
#define ADIS16260_ALM_MAG1   0x20 /* Alarm 1 magnitude/polarity setting */
#define ADIS16260_ALM_MAG2   0x22 /* Alarm 2 magnitude/polarity setting */
#define ADIS16260_ALM_SMPL1  0x24 /* Alarm 1 dynamic rate of change setting */
#define ADIS16260_ALM_SMPL2  0x26 /* Alarm 2 dynamic rate of change setting */
#define ADIS16260_ALM_CTRL   0x28 /* Alarm control */
#define ADIS16260_AUX_DAC    0x30 /* Auxiliary DAC data */
#define ADIS16260_GPIO_CTRL  0x32 /* Control, digital I/O line */
#define ADIS16260_MSC_CTRL   0x34 /* Control, data ready, self-test settings */
#define ADIS16260_SMPL_PRD   0x36 /* Control, internal sample rate */
#define ADIS16260_SENS_AVG   0x38 /* Control, dynamic range, filtering */
#define ADIS16260_SLP_CNT    0x3A /* Control, sleep mode initiation */
#define ADIS16260_DIAG_STAT  0x3C /* Diagnostic, error flags */
#define ADIS16260_GLOB_CMD   0x3E /* Control, global commands */
#define ADIS16260_LOT_ID1    0x52 /* Lot Identification Code 1 */
#define ADIS16260_LOT_ID2    0x54 /* Lot Identification Code 2 */
#define ADIS16260_PROD_ID    0x56 /* Product identifier;
				   * convert to decimal = 16,265/16,260 */
#define ADIS16260_SERIAL_NUM 0x58 /* Serial number */

#define ADIS16260_ERROR_ACTIVE			(1<<14)
#define ADIS16260_NEW_DATA			(1<<15)

/* MSC_CTRL */
#define ADIS16260_MSC_CTRL_MEM_TEST		(1<<11)
/* Internal self-test enable */
#define ADIS16260_MSC_CTRL_INT_SELF_TEST	(1<<10)
#define ADIS16260_MSC_CTRL_NEG_SELF_TEST	(1<<9)
#define ADIS16260_MSC_CTRL_POS_SELF_TEST	(1<<8)
#define ADIS16260_MSC_CTRL_DATA_RDY_EN		(1<<2)
#define ADIS16260_MSC_CTRL_DATA_RDY_POL_HIGH	(1<<1)
#define ADIS16260_MSC_CTRL_DATA_RDY_DIO2	(1<<0)

/* SMPL_PRD */
/* Time base (tB): 0 = 1.953 ms, 1 = 60.54 ms */
#define ADIS16260_SMPL_PRD_TIME_BASE	(1<<7)
#define ADIS16260_SMPL_PRD_DIV_MASK	0x7F

/* SLP_CNT */
#define ADIS16260_SLP_CNT_POWER_OFF     0x80

/* DIAG_STAT */
#define ADIS16260_DIAG_STAT_ALARM2	(1<<9)
#define ADIS16260_DIAG_STAT_ALARM1	(1<<8)
#define ADIS16260_DIAG_STAT_FLASH_CHK_BIT	6
#define ADIS16260_DIAG_STAT_SELF_TEST_BIT	5
#define ADIS16260_DIAG_STAT_OVERFLOW_BIT	4
#define ADIS16260_DIAG_STAT_SPI_FAIL_BIT	3
#define ADIS16260_DIAG_STAT_FLASH_UPT_BIT	2
#define ADIS16260_DIAG_STAT_POWER_HIGH_BIT	1
#define ADIS16260_DIAG_STAT_POWER_LOW_BIT	0

/* GLOB_CMD */
#define ADIS16260_GLOB_CMD_SW_RESET	(1<<7)
#define ADIS16260_GLOB_CMD_FLASH_UPD	(1<<3)
#define ADIS16260_GLOB_CMD_DAC_LATCH	(1<<2)
#define ADIS16260_GLOB_CMD_FAC_CALIB	(1<<1)
#define ADIS16260_GLOB_CMD_AUTO_NULL	(1<<0)

#define ADIS16260_SPI_SLOW	(u32)(300 * 1000)
#define ADIS16260_SPI_BURST	(u32)(1000 * 1000)
#define ADIS16260_SPI_FAST	(u32)(2000 * 1000)

/**
 * struct adis16260_state - device instance specific data
 * @negate:		negate the scale parameter
 **/
struct adis16260_state {
	unsigned	negate:1;
	struct adis	adis;
};

/* At the moment triggers are only used for ring buffer
 * filling. This may change!
 */

#define ADIS16260_SCAN_GYRO	0
#define ADIS16260_SCAN_SUPPLY	1
#define ADIS16260_SCAN_AUX_ADC	2
#define ADIS16260_SCAN_TEMP	3
#define ADIS16260_SCAN_ANGL	4

#endif /* SPI_ADIS16260_H_ */
