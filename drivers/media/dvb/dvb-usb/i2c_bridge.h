#ifndef __I2C_BRIDGE_H
#define __I2C_BRIDGE_H

/**

@file

@brief   I2C bridge module

I2C bridge module contains I2C forwarding function pointers.

*/





/// I2C bridge module pre-definition
typedef struct I2C_BRIDGE_MODULE_TAG I2C_BRIDGE_MODULE;





/**

@brief   I2C reading command forwarding function pointer

Tuner upper level functions will use I2C_BRIDGE_FP_FORWARD_I2C_READING_CMD() to send tuner I2C reading command through
demod.


@param [in]    pI2cBridge      The I2C bridge module pointer
@param [in]    DeviceAddr       I2C device address in 8-bit format
@param [out]   pReadingBytes   Pointer to an allocated memory for storing reading bytes
@param [in]    ByteNum         Reading byte number


@retval   FUNCTION_SUCCESS   Forwarding I2C reading command successfully.
@retval   FUNCTION_ERROR     Forwarding I2C reading command unsuccessfully.


@note
	-# Demod building function will set I2C_BRIDGE_FP_FORWARD_I2C_READING_CMD() with the corresponding function.

*/
typedef int
(*I2C_BRIDGE_FP_FORWARD_I2C_READING_CMD)(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);





/**

@brief   I2C writing command forwarding function pointer

Tuner upper level functions will use I2C_BRIDGE_FP_FORWARD_I2C_WRITING_CMD() to send tuner I2C writing command through
demod.


@param [in]    pI2cBridge      The I2C bridge module pointer
@param [in]    DeviceAddr       I2C device address in 8-bit format
@param [out]   pWritingBytes   Pointer to writing bytes
@param [in]    ByteNum         Writing byte number


@retval   FUNCTION_SUCCESS   Forwarding I2C writing command successfully.
@retval   FUNCTION_ERROR     Forwarding I2C writing command unsuccessfully.


@note
	-# Demod building function will set I2C_BRIDGE_FP_FORWARD_I2C_WRITING_CMD() with the corresponding function.

*/
typedef int
(*I2C_BRIDGE_FP_FORWARD_I2C_WRITING_CMD)(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);





/// I2C bridge module structure
struct I2C_BRIDGE_MODULE_TAG
{
	// Private variables
	void *pPrivateData;


	// I2C bridge function pointers
	I2C_BRIDGE_FP_FORWARD_I2C_READING_CMD   ForwardI2cReadingCmd;	///<   I2C reading command forwading function pointer
	I2C_BRIDGE_FP_FORWARD_I2C_WRITING_CMD   ForwardI2cWritingCmd;   ///<   I2C writing command forwading function pointer

};

















#endif
