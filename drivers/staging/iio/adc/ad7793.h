/*
 * AD7792/AD7793 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */
#ifndef IIO_ADC_AD7793_H_
#define IIO_ADC_AD7793_H_

/*
 * TODO: struct ad7793_platform_data needs to go into include/linux/iio
 */

/* Registers */
#define AD7793_REG_COMM		0 /* Communications Register (WO, 8-bit) */
#define AD7793_REG_STAT		0 /* Status Register	     (RO, 8-bit) */
#define AD7793_REG_MODE		1 /* Mode Register	     (RW, 16-bit */
#define AD7793_REG_CONF		2 /* Configuration Register  (RW, 16-bit) */
#define AD7793_REG_DATA		3 /* Data Register	     (RO, 16-/24-bit) */
#define AD7793_REG_ID		4 /* ID Register	     (RO, 8-bit) */
#define AD7793_REG_IO		5 /* IO Register	     (RO, 8-bit) */
#define AD7793_REG_OFFSET	6 /* Offset Register	     (RW, 16-bit
				   * (AD7792)/24-bit (AD7793)) */
#define AD7793_REG_FULLSALE	7 /* Full-Scale Register
				   * (RW, 16-bit (AD7792)/24-bit (AD7793)) */

/* Communications Register Bit Designations (AD7793_REG_COMM) */
#define AD7793_COMM_WEN		(1 << 7) /* Write Enable */
#define AD7793_COMM_WRITE	(0 << 6) /* Write Operation */
#define AD7793_COMM_READ	(1 << 6) /* Read Operation */
#define AD7793_COMM_ADDR(x)	(((x) & 0x7) << 3) /* Register Address */
#define AD7793_COMM_CREAD	(1 << 2) /* Continuous Read of Data Register */

/* Status Register Bit Designations (AD7793_REG_STAT) */
#define AD7793_STAT_RDY		(1 << 7) /* Ready */
#define AD7793_STAT_ERR		(1 << 6) /* Error (Overrange, Underrange) */
#define AD7793_STAT_CH3		(1 << 2) /* Channel 3 */
#define AD7793_STAT_CH2		(1 << 1) /* Channel 2 */
#define AD7793_STAT_CH1		(1 << 0) /* Channel 1 */

/* Mode Register Bit Designations (AD7793_REG_MODE) */
#define AD7793_MODE_SEL(x)	(((x) & 0x7) << 13) /* Operation Mode Select */
#define AD7793_MODE_CLKSRC(x)	(((x) & 0x3) << 6) /* ADC Clock Source Select */
#define AD7793_MODE_RATE(x)	((x) & 0xF) /* Filter Update Rate Select */

#define AD7793_MODE_CONT		0 /* Continuous Conversion Mode */
#define AD7793_MODE_SINGLE		1 /* Single Conversion Mode */
#define AD7793_MODE_IDLE		2 /* Idle Mode */
#define AD7793_MODE_PWRDN		3 /* Power-Down Mode */
#define AD7793_MODE_CAL_INT_ZERO	4 /* Internal Zero-Scale Calibration */
#define AD7793_MODE_CAL_INT_FULL	5 /* Internal Full-Scale Calibration */
#define AD7793_MODE_CAL_SYS_ZERO	6 /* System Zero-Scale Calibration */
#define AD7793_MODE_CAL_SYS_FULL	7 /* System Full-Scale Calibration */

#define AD7793_CLK_INT		0 /* Internal 64 kHz Clock not
				   * available at the CLK pin */
#define AD7793_CLK_INT_CO	1 /* Internal 64 kHz Clock available
				   * at the CLK pin */
#define AD7793_CLK_EXT		2 /* External 64 kHz Clock */
#define AD7793_CLK_EXT_DIV2	3 /* External Clock divided by 2 */

/* Configuration Register Bit Designations (AD7793_REG_CONF) */
#define AD7793_CONF_VBIAS(x)	(((x) & 0x3) << 14) /* Bias Voltage
						     * Generator Enable */
#define AD7793_CONF_BO_EN	(1 << 13) /* Burnout Current Enable */
#define AD7793_CONF_UNIPOLAR	(1 << 12) /* Unipolar/Bipolar Enable */
#define AD7793_CONF_BOOST	(1 << 11) /* Boost Enable */
#define AD7793_CONF_GAIN(x)	(((x) & 0x7) << 8) /* Gain Select */
#define AD7793_CONF_REFSEL	(1 << 7) /* INT/EXT Reference Select */
#define AD7793_CONF_BUF		(1 << 4) /* Buffered Mode Enable */
#define AD7793_CONF_CHAN(x)	((x) & 0x7) /* Channel select */

#define AD7793_CH_AIN1P_AIN1M	0 /* AIN1(+) - AIN1(-) */
#define AD7793_CH_AIN2P_AIN2M	1 /* AIN2(+) - AIN2(-) */
#define AD7793_CH_AIN3P_AIN3M	2 /* AIN3(+) - AIN3(-) */
#define AD7793_CH_AIN1M_AIN1M	3 /* AIN1(-) - AIN1(-) */
#define AD7793_CH_TEMP		6 /* Temp Sensor */
#define AD7793_CH_AVDD_MONITOR	7 /* AVDD Monitor */

/* ID Register Bit Designations (AD7793_REG_ID) */
#define AD7792_ID		0xA
#define AD7793_ID		0xB
#define AD7793_ID_MASK		0xF

/* IO (Excitation Current Sources) Register Bit Designations (AD7793_REG_IO) */
#define AD7793_IO_IEXC1_IOUT1_IEXC2_IOUT2	0 /* IEXC1 connect to IOUT1,
						   * IEXC2 connect to IOUT2 */
#define AD7793_IO_IEXC1_IOUT2_IEXC2_IOUT1	1 /* IEXC1 connect to IOUT2,
						   * IEXC2 connect to IOUT1 */
#define AD7793_IO_IEXC1_IEXC2_IOUT1		2 /* Both current sources
						   * IEXC1,2 connect to IOUT1 */
#define AD7793_IO_IEXC1_IEXC2_IOUT2		3 /* Both current sources
						   * IEXC1,2 connect to IOUT2 */

#define AD7793_IO_IXCEN_10uA	(1 << 0) /* Excitation Current 10uA */
#define AD7793_IO_IXCEN_210uA	(2 << 0) /* Excitation Current 210uA */
#define AD7793_IO_IXCEN_1mA	(3 << 0) /* Excitation Current 1mA */

struct ad7793_platform_data {
	u16			vref_mv;
	u16			mode;
	u16			conf;
	u8			io;
};

#endif /* IIO_ADC_AD7793_H_ */
