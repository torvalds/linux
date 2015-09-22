#define USE_HW_I2C
#ifdef USE_HW_I2C
#include "ddk750_help.h"
#include "ddk750_reg.h"
#include "ddk750_hwi2c.h"
#include "ddk750_power.h"

#define MAX_HWI2C_FIFO                  16
#define HWI2C_WAIT_TIMEOUT              0xF0000


int hwI2CInit(
unsigned char busSpeedMode
)
{
	unsigned int value;

	/* Enable GPIO 30 & 31 as IIC clock & data */
	value = PEEK32(GPIO_MUX);

	value = FIELD_SET(value, GPIO_MUX, 30, I2C) |
			  FIELD_SET(0, GPIO_MUX, 31, I2C);
	POKE32(GPIO_MUX, value);

	/* Enable Hardware I2C power.
	 TODO: Check if we need to enable GPIO power?
	 */
	enableI2C(1);

	/* Enable the I2C Controller and set the bus speed mode */
	value = PEEK32(I2C_CTRL);
	if (busSpeedMode == 0)
		value = FIELD_SET(value, I2C_CTRL, MODE, STANDARD);
	else
		value = FIELD_SET(value, I2C_CTRL, MODE, FAST);
	value = FIELD_SET(value, I2C_CTRL, EN, ENABLE);
	POKE32(I2C_CTRL, value);

	return 0;
}


void hwI2CClose(void)
{
	unsigned int value;

	/* Disable I2C controller */
	value = PEEK32(I2C_CTRL);
	value = FIELD_SET(value, I2C_CTRL, EN, DISABLE);
	POKE32(I2C_CTRL, value);

	/* Disable I2C Power */
	enableI2C(0);

	/* Set GPIO 30 & 31 back as GPIO pins */
	value = PEEK32(GPIO_MUX);
	value = FIELD_SET(value, GPIO_MUX, 30, GPIO);
	value = FIELD_SET(value, GPIO_MUX, 31, GPIO);
	POKE32(GPIO_MUX, value);
}


static long hwI2CWaitTXDone(void)
{
	unsigned int timeout;

	/* Wait until the transfer is completed. */
	timeout = HWI2C_WAIT_TIMEOUT;
	while ((FIELD_GET(PEEK32(I2C_STATUS), I2C_STATUS, TX) != I2C_STATUS_TX_COMPLETED) &&
	       (timeout != 0))
		timeout--;

	if (timeout == 0)
		return (-1);

	return 0;
}



/*
 *  This function writes data to the i2c slave device registers.
 *
 *  Parameters:
 *      deviceAddress   - i2c Slave device address
 *      length          - Total number of bytes to be written to the device
 *      pBuffer         - The buffer that contains the data to be written to the
 *                     i2c device.
 *
 *  Return Value:
 *      Total number of bytes those are actually written.
 */
static unsigned int hwI2CWriteData(
	unsigned char deviceAddress,
	unsigned int length,
	unsigned char *pBuffer
)
{
	unsigned char count, i;
	unsigned int totalBytes = 0;

	/* Set the Device Address */
	POKE32(I2C_SLAVE_ADDRESS, deviceAddress & ~0x01);

	/* Write data.
	 * Note:
	 *      Only 16 byte can be accessed per i2c start instruction.
	 */
	do {
		/* Reset I2C by writing 0 to I2C_RESET register to clear the previous status. */
		POKE32(I2C_RESET, 0);

		/* Set the number of bytes to be written */
		if (length < MAX_HWI2C_FIFO)
			count = length - 1;
		else
			count = MAX_HWI2C_FIFO - 1;
		POKE32(I2C_BYTE_COUNT, count);

		/* Move the data to the I2C data register */
		for (i = 0; i <= count; i++)
			POKE32(I2C_DATA0 + i, *pBuffer++);

		/* Start the I2C */
		POKE32(I2C_CTRL, FIELD_SET(PEEK32(I2C_CTRL), I2C_CTRL, CTRL, START));

		/* Wait until the transfer is completed. */
		if (hwI2CWaitTXDone() != 0)
			break;

		/* Substract length */
		length -= (count + 1);

		/* Total byte written */
		totalBytes += (count + 1);

	} while (length > 0);

	return totalBytes;
}




/*
 *  This function reads data from the slave device and stores them
 *  in the given buffer
 *
 *  Parameters:
 *      deviceAddress   - i2c Slave device address
 *      length          - Total number of bytes to be read
 *      pBuffer         - Pointer to a buffer to be filled with the data read
 *                     from the slave device. It has to be the same size as the
 *                     length to make sure that it can keep all the data read.
 *
 *  Return Value:
 *      Total number of actual bytes read from the slave device
 */
static unsigned int hwI2CReadData(
	unsigned char deviceAddress,
	unsigned int length,
	unsigned char *pBuffer
)
{
	unsigned char count, i;
	unsigned int totalBytes = 0;

	/* Set the Device Address */
	POKE32(I2C_SLAVE_ADDRESS, deviceAddress | 0x01);

	/* Read data and save them to the buffer.
	 * Note:
	 *      Only 16 byte can be accessed per i2c start instruction.
	 */
	do {
		/* Reset I2C by writing 0 to I2C_RESET register to clear all the status. */
		POKE32(I2C_RESET, 0);

		/* Set the number of bytes to be read */
		if (length <= MAX_HWI2C_FIFO)
			count = length - 1;
		else
			count = MAX_HWI2C_FIFO - 1;
		POKE32(I2C_BYTE_COUNT, count);

		/* Start the I2C */
		POKE32(I2C_CTRL, FIELD_SET(PEEK32(I2C_CTRL), I2C_CTRL, CTRL, START));

		/* Wait until transaction done. */
		if (hwI2CWaitTXDone() != 0)
			break;

		/* Save the data to the given buffer */
		for (i = 0; i <= count; i++)
			*pBuffer++ = PEEK32(I2C_DATA0 + i);

		/* Substract length by 16 */
		length -= (count + 1);

		/* Number of bytes read. */
		totalBytes += (count + 1);

	} while (length > 0);

	return totalBytes;
}




/*
 *  This function reads the slave device's register
 *
 *  Parameters:
 *      deviceAddress   - i2c Slave device address which register
 *                        to be read from
 *      registerIndex   - Slave device's register to be read
 *
 *  Return Value:
 *      Register value
 */
unsigned char hwI2CReadReg(
	unsigned char deviceAddress,
	unsigned char registerIndex
)
{
	unsigned char value = (0xFF);

	if (hwI2CWriteData(deviceAddress, 1, &registerIndex) == 1)
		hwI2CReadData(deviceAddress, 1, &value);

	return value;
}





/*
 *  This function writes a value to the slave device's register
 *
 *  Parameters:
 *      deviceAddress   - i2c Slave device address which register
 *                        to be written
 *      registerIndex   - Slave device's register to be written
 *      data            - Data to be written to the register
 *
 *  Result:
 *          0   - Success
 *         -1   - Fail
 */
int hwI2CWriteReg(
	unsigned char deviceAddress,
	unsigned char registerIndex,
	unsigned char data
)
{
	unsigned char value[2];

	value[0] = registerIndex;
	value[1] = data;
	if (hwI2CWriteData(deviceAddress, 2, value) == 2)
		return 0;

	return (-1);
}


#endif
