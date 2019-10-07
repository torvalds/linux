// SPDX-License-Identifier: GPL-2.0
/*
 *         Copyright (c) 2007 by Silicon Motion, Inc. (SMI)
 *
 *  swi2c.c --- SM750/SM718 DDK
 *  This file contains the source code for I2C using software
 *  implementation.
 */

#include "ddk750_chip.h"
#include "ddk750_reg.h"
#include "ddk750_swi2c.h"
#include "ddk750_power.h"

/*
 * I2C Software Master Driver:
 * ===========================
 * Each i2c cycle is split into 4 sections. Each of these section marks
 * a point in time where the SCL or SDA may be changed.
 *
 * 1 Cycle == |  Section I. |  Section 2. |  Section 3. |  Section 4. |
 *            +-------------+-------------+-------------+-------------+
 *            | SCL set LOW |SCL no change| SCL set HIGH|SCL no change|
 *
 *                                          ____________ _____________
 * SCL == XXXX _____________ ____________ /
 *
 * I.e. the SCL may only be changed in section 1. and section 3. while
 * the SDA may only be changed in section 2. and section 4. The table
 * below gives the changes for these 2 lines in the varios sections.
 *
 * Section changes Table:
 * ======================
 * blank = no change, L = set bit LOW, H = set bit HIGH
 *
 *                                | 1.| 2.| 3.| 4.|
 *                 ---------------+---+---+---+---+
 *                 Tx Start   SDA |   | H |   | L |
 *                            SCL | L |   | H |   |
 *                 ---------------+---+---+---+---+
 *                 Tx Stop    SDA |   | L |   | H |
 *                            SCL | L |   | H |   |
 *                 ---------------+---+---+---+---+
 *                 Tx bit H   SDA |   | H |   |   |
 *                            SCL | L |   | H |   |
 *                 ---------------+---+---+---+---+
 *                 Tx bit L   SDA |   | L |   |   |
 *                            SCL | L |   | H |   |
 *                 ---------------+---+---+---+---+
 *
 */

/* GPIO pins used for this I2C. It ranges from 0 to 63. */
static unsigned char sw_i2c_clk_gpio = DEFAULT_I2C_SCL;
static unsigned char sw_i2c_data_gpio = DEFAULT_I2C_SDA;

/*
 *  Below is the variable declaration for the GPIO pin register usage
 *  for the i2c Clock and i2c Data.
 *
 *  Note:
 *      Notice that the GPIO usage for the i2c clock and i2c Data are
 *      separated. This is to make this code flexible enough when
 *      two separate GPIO pins for the clock and data are located
 *      in two different GPIO register set (worst case).
 */

/* i2c Clock GPIO Register usage */
static unsigned long sw_i2c_clk_gpio_mux_reg = GPIO_MUX;
static unsigned long sw_i2c_clk_gpio_data_reg = GPIO_DATA;
static unsigned long sw_i2c_clk_gpio_data_dir_reg = GPIO_DATA_DIRECTION;

/* i2c Data GPIO Register usage */
static unsigned long sw_i2c_data_gpio_mux_reg = GPIO_MUX;
static unsigned long sw_i2c_data_gpio_data_reg = GPIO_DATA;
static unsigned long sw_i2c_data_gpio_data_dir_reg = GPIO_DATA_DIRECTION;

/*
 *  This function puts a delay between command
 */
static void sw_i2c_wait(void)
{
	/* find a bug:
	 * peekIO method works well before suspend/resume
	 * but after suspend, peekIO(0x3ce,0x61) & 0x10
	 * always be non-zero,which makes the while loop
	 * never finish.
	 * use non-ultimate for loop below is safe
	 */

    /* Change wait algorithm to use PCI bus clock,
     * it's more reliable than counter loop ..
     * write 0x61 to 0x3ce and read from 0x3cf
     */
	int i, tmp;

	for (i = 0; i < 600; i++) {
		tmp = i;
		tmp += i;
	}
}

/*
 *  This function set/reset the SCL GPIO pin
 *
 *  Parameters:
 *      value    - Bit value to set to the SCL or SDA (0 = low, 1 = high)
 *
 *  Notes:
 *      When setting SCL to high, just set the GPIO as input where the pull up
 *      resistor will pull the signal up. Do not use software to pull up the
 *      signal because the i2c will fail when other device try to drive the
 *      signal due to SM50x will drive the signal to always high.
 */
static void sw_i2c_scl(unsigned char value)
{
	unsigned long gpio_data;
	unsigned long gpio_dir;

	gpio_dir = peek32(sw_i2c_clk_gpio_data_dir_reg);
	if (value) {    /* High */
		/*
		 * Set direction as input. This will automatically
		 * pull the signal up.
		 */
		gpio_dir &= ~(1 << sw_i2c_clk_gpio);
		poke32(sw_i2c_clk_gpio_data_dir_reg, gpio_dir);
	} else {        /* Low */
		/* Set the signal down */
		gpio_data = peek32(sw_i2c_clk_gpio_data_reg);
		gpio_data &= ~(1 << sw_i2c_clk_gpio);
		poke32(sw_i2c_clk_gpio_data_reg, gpio_data);

		/* Set direction as output */
		gpio_dir |= (1 << sw_i2c_clk_gpio);
		poke32(sw_i2c_clk_gpio_data_dir_reg, gpio_dir);
	}
}

/*
 *  This function set/reset the SDA GPIO pin
 *
 *  Parameters:
 *      value    - Bit value to set to the SCL or SDA (0 = low, 1 = high)
 *
 *  Notes:
 *      When setting SCL to high, just set the GPIO as input where the pull up
 *      resistor will pull the signal up. Do not use software to pull up the
 *      signal because the i2c will fail when other device try to drive the
 *      signal due to SM50x will drive the signal to always high.
 */
static void sw_i2c_sda(unsigned char value)
{
	unsigned long gpio_data;
	unsigned long gpio_dir;

	gpio_dir = peek32(sw_i2c_data_gpio_data_dir_reg);
	if (value) {    /* High */
		/*
		 * Set direction as input. This will automatically
		 * pull the signal up.
		 */
		gpio_dir &= ~(1 << sw_i2c_data_gpio);
		poke32(sw_i2c_data_gpio_data_dir_reg, gpio_dir);
	} else {        /* Low */
		/* Set the signal down */
		gpio_data = peek32(sw_i2c_data_gpio_data_reg);
		gpio_data &= ~(1 << sw_i2c_data_gpio);
		poke32(sw_i2c_data_gpio_data_reg, gpio_data);

		/* Set direction as output */
		gpio_dir |= (1 << sw_i2c_data_gpio);
		poke32(sw_i2c_data_gpio_data_dir_reg, gpio_dir);
	}
}

/*
 *  This function read the data from the SDA GPIO pin
 *
 *  Return Value:
 *      The SDA data bit sent by the Slave
 */
static unsigned char sw_i2c_read_sda(void)
{
	unsigned long gpio_dir;
	unsigned long gpio_data;
	unsigned long dir_mask = 1 << sw_i2c_data_gpio;

	/* Make sure that the direction is input (High) */
	gpio_dir = peek32(sw_i2c_data_gpio_data_dir_reg);
	if ((gpio_dir & dir_mask) != ~dir_mask) {
		gpio_dir &= ~(1 << sw_i2c_data_gpio);
		poke32(sw_i2c_data_gpio_data_dir_reg, gpio_dir);
	}

	/* Now read the SDA line */
	gpio_data = peek32(sw_i2c_data_gpio_data_reg);
	if (gpio_data & (1 << sw_i2c_data_gpio))
		return 1;
	else
		return 0;
}

/*
 *  This function sends ACK signal
 */
static void sw_i2c_ack(void)
{
	return;  /* Single byte read is ok without it. */
}

/*
 *  This function sends the start command to the slave device
 */
static void sw_i2c_start(void)
{
	/* Start I2C */
	sw_i2c_sda(1);
	sw_i2c_scl(1);
	sw_i2c_sda(0);
}

/*
 *  This function sends the stop command to the slave device
 */
static void sw_i2c_stop(void)
{
	/* Stop the I2C */
	sw_i2c_scl(1);
	sw_i2c_sda(0);
	sw_i2c_sda(1);
}

/*
 *  This function writes one byte to the slave device
 *
 *  Parameters:
 *      data    - Data to be write to the slave device
 *
 *  Return Value:
 *       0   - Success
 *      -1   - Fail to write byte
 */
static long sw_i2c_write_byte(unsigned char data)
{
	unsigned char value = data;
	int i;

	/* Sending the data bit by bit */
	for (i = 0; i < 8; i++) {
		/* Set SCL to low */
		sw_i2c_scl(0);

		/* Send data bit */
		if ((value & 0x80) != 0)
			sw_i2c_sda(1);
		else
			sw_i2c_sda(0);

		sw_i2c_wait();

		/* Toggle clk line to one */
		sw_i2c_scl(1);
		sw_i2c_wait();

		/* Shift byte to be sent */
		value = value << 1;
	}

	/* Set the SCL Low and SDA High (prepare to get input) */
	sw_i2c_scl(0);
	sw_i2c_sda(1);

	/* Set the SCL High for ack */
	sw_i2c_wait();
	sw_i2c_scl(1);
	sw_i2c_wait();

	/* Read SDA, until SDA==0 */
	for (i = 0; i < 0xff; i++) {
		if (!sw_i2c_read_sda())
			break;

		sw_i2c_scl(0);
		sw_i2c_wait();
		sw_i2c_scl(1);
		sw_i2c_wait();
	}

	/* Set the SCL Low and SDA High */
	sw_i2c_scl(0);
	sw_i2c_sda(1);

	if (i < 0xff)
		return 0;
	else
		return -1;
}

/*
 *  This function reads one byte from the slave device
 *
 *  Parameters:
 *      ack    - Flag to indicate either to send the acknowledge
 *            message to the slave device or not
 *
 *  Return Value:
 *      One byte data read from the Slave device
 */
static unsigned char sw_i2c_read_byte(unsigned char ack)
{
	int i;
	unsigned char data = 0;

	for (i = 7; i >= 0; i--) {
		/* Set the SCL to Low and SDA to High (Input) */
		sw_i2c_scl(0);
		sw_i2c_sda(1);
		sw_i2c_wait();

		/* Set the SCL High */
		sw_i2c_scl(1);
		sw_i2c_wait();

		/* Read data bits from SDA */
		data |= (sw_i2c_read_sda() << i);
	}

	if (ack)
		sw_i2c_ack();

	/* Set the SCL Low and SDA High */
	sw_i2c_scl(0);
	sw_i2c_sda(1);

	return data;
}

/*
 * This function initializes GPIO port for SW I2C communication.
 *
 * Parameters:
 *      clk_gpio      - The GPIO pin to be used as i2c SCL
 *      data_gpio     - The GPIO pin to be used as i2c SDA
 *
 * Return Value:
 *      -1   - Fail to initialize the i2c
 *       0   - Success
 */
static long sm750le_i2c_init(unsigned char clk_gpio, unsigned char data_gpio)
{
	int i;

	/* Initialize the GPIO pin for the i2c Clock Register */
	sw_i2c_clk_gpio_data_reg = GPIO_DATA_SM750LE;
	sw_i2c_clk_gpio_data_dir_reg = GPIO_DATA_DIRECTION_SM750LE;

	/* Initialize the Clock GPIO Offset */
	sw_i2c_clk_gpio = clk_gpio;

	/* Initialize the GPIO pin for the i2c Data Register */
	sw_i2c_data_gpio_data_reg = GPIO_DATA_SM750LE;
	sw_i2c_data_gpio_data_dir_reg = GPIO_DATA_DIRECTION_SM750LE;

	/* Initialize the Data GPIO Offset */
	sw_i2c_data_gpio = data_gpio;

	/* Note that SM750LE don't have GPIO MUX and power is always on */

	/* Clear the i2c lines. */
	for (i = 0; i < 9; i++)
		sw_i2c_stop();

	return 0;
}

/*
 * This function initializes the i2c attributes and bus
 *
 * Parameters:
 *      clk_gpio      - The GPIO pin to be used as i2c SCL
 *      data_gpio     - The GPIO pin to be used as i2c SDA
 *
 * Return Value:
 *      -1   - Fail to initialize the i2c
 *       0   - Success
 */
long sm750_sw_i2c_init(unsigned char clk_gpio, unsigned char data_gpio)
{
	int i;

	/*
	 * Return 0 if the GPIO pins to be used is out of range. The
	 * range is only from [0..63]
	 */
	if ((clk_gpio > 31) || (data_gpio > 31))
		return -1;

	if (sm750_get_chip_type() == SM750LE)
		return sm750le_i2c_init(clk_gpio, data_gpio);

	/* Initialize the GPIO pin for the i2c Clock Register */
	sw_i2c_clk_gpio_mux_reg = GPIO_MUX;
	sw_i2c_clk_gpio_data_reg = GPIO_DATA;
	sw_i2c_clk_gpio_data_dir_reg = GPIO_DATA_DIRECTION;

	/* Initialize the Clock GPIO Offset */
	sw_i2c_clk_gpio = clk_gpio;

	/* Initialize the GPIO pin for the i2c Data Register */
	sw_i2c_data_gpio_mux_reg = GPIO_MUX;
	sw_i2c_data_gpio_data_reg = GPIO_DATA;
	sw_i2c_data_gpio_data_dir_reg = GPIO_DATA_DIRECTION;

	/* Initialize the Data GPIO Offset */
	sw_i2c_data_gpio = data_gpio;

	/* Enable the GPIO pins for the i2c Clock and Data (GPIO MUX) */
	poke32(sw_i2c_clk_gpio_mux_reg,
	       peek32(sw_i2c_clk_gpio_mux_reg) & ~(1 << sw_i2c_clk_gpio));
	poke32(sw_i2c_data_gpio_mux_reg,
	       peek32(sw_i2c_data_gpio_mux_reg) & ~(1 << sw_i2c_data_gpio));

	/* Enable GPIO power */
	sm750_enable_gpio(1);

	/* Clear the i2c lines. */
	for (i = 0; i < 9; i++)
		sw_i2c_stop();

	return 0;
}

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
unsigned char sm750_sw_i2c_read_reg(unsigned char addr, unsigned char reg)
{
	unsigned char data;

	/* Send the Start signal */
	sw_i2c_start();

	/* Send the device address */
	sw_i2c_write_byte(addr);

	/* Send the register index */
	sw_i2c_write_byte(reg);

	/* Get the bus again and get the data from the device read address */
	sw_i2c_start();
	sw_i2c_write_byte(addr + 1);
	data = sw_i2c_read_byte(1);

	/* Stop swI2C and release the bus */
	sw_i2c_stop();

	return data;
}

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
			    unsigned char data)
{
	long ret = 0;

	/* Send the Start signal */
	sw_i2c_start();

	/* Send the device address and read the data. All should return success
	 * in order for the writing processed to be successful
	 */
	if ((sw_i2c_write_byte(addr) != 0) ||
	    (sw_i2c_write_byte(reg) != 0) ||
	    (sw_i2c_write_byte(data) != 0)) {
		ret = -1;
	}

	/* Stop i2c and release the bus */
	sw_i2c_stop();

	return ret;
}
