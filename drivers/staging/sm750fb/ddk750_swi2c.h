/*
 *         Copyright (c) 2007 by Silicon Motion, Inc. (SMI)
 *
 *  All rights are reserved. Reproduction or in part is prohibited
 *  without the written consent of the copyright owner.
 *
 *  swi2c.h --- SM750/SM718 DDK
 *  This file contains the definitions for i2c using software
 *  implementation.
 *
 */

#ifndef _SWI2C_H_
#define _SWI2C_H_

/* Default i2c CLK and Data GPIO. These are the default i2c pins */
#define DEFAULT_I2C_SCL                     30
#define DEFAULT_I2C_SDA                     31

/*
 * This function initializes the i2c attributes and bus
 *
 * Parameters:
 *      i2cClkGPIO  - The GPIO pin to be used as i2c SCL
 *      i2cDataGPIO - The GPIO pin to be used as i2c SDA
 *
 * Return Value:
 *      -1   - Fail to initialize the i2c
 *       0   - Success
 */
long sm750_sw_i2c_init(unsigned char clk_gpio, unsigned char data_gpio);

/*
 *  This function reads the slave device's register
 *
 *  Parameters:
 *      addr   - i2c Slave device address which register
 *                        to be read from
 *      reg    - Slave device's register to be read
 *
 *  Return Value:
 *      Register value
 */
unsigned char sm750_sw_i2c_read_reg(unsigned char addr, unsigned char reg);

/*
 *  This function writes a value to the slave device's register
 *
 *  Parameters:
 *      addr            - i2c Slave device address which register
 *                        to be written
 *      reg             - Slave device's register to be written
 *      data            - Data to be written to the register
 *
 *  Result:
 *          0   - Success
 *         -1   - Fail
 */
long sm750_sw_i2c_write_reg(unsigned char addr,
			    unsigned char reg,
			    unsigned char data);

#endif  /* _SWI2C_H_ */
