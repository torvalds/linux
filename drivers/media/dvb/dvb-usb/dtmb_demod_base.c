/**

@file

@brief   DTMB demod default function definition

DTMB demod default functions.

*/
#include "rtl2832u_io.h"
#include "dtmb_demod_base.h"





/**

@see   DTMB_DEMOD_FP_SET_REG_PAGE

*/
int
dtmb_demod_addr_8bit_default_SetRegPage(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long PageNo
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned char DeviceAddr;
	unsigned char WritingBytes[LEN_2_BYTE];


	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Get demod device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Set demod register page with page number.
	// Note: The I2C format of demod register page setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + DTMB_DEMOD_PAGE_REG_ADDR + PageNo + stop_bit
	WritingBytes[0] = DTMB_DEMOD_PAGE_REG_ADDR;
	WritingBytes[1] = (unsigned char)PageNo;

	if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, WritingBytes, LEN_2_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;

	// temp, because page register is write-only.
	pDemod->CurrentPageNo = PageNo;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





// Internal function:
// Set register bytes separately.
int
internal_dtmb_demod_addr_8bit_SetRegBytesSeparately(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	)
{
#if 0
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned long i;

	unsigned char DeviceAddr;
	unsigned char WritingBuffer[LEN_2_BYTE];



	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Set demod register bytes with writing bytes.
	// Note: Set demod register bytes one by one.
	for(i = 0; i < ByteNum; i++)
	{
		// Set writing buffer.
		// Note: The I2C format of demod register byte setting is as follows:
		//       start_bit + (DeviceAddr | writing_bit) + RegWritingAddr + writing_bytes (WritingByteNum bytes) + stop_bit
		WritingBuffer[0] = (unsigned char)(RegStartAddr + i);
		WritingBuffer[1] = pWritingBytes[i];

		// Set demod register bytes with writing buffer.
		if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, WritingBuffer, LEN_2_BYTE) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
#endif
	BASE_INTERFACE_MODULE *pBaseInterface;
        struct dvb_usb_device	*d;
	unsigned int i;
	unsigned char DeviceAddr;
	//unsigned char WritingBuffer[LEN_2_BYTE];
	
	unsigned long PageNo = pDemod->CurrentPageNo;
		
	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;

	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);

	// Get user defined data pointer of base interface structure for context.
	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);
	
	
	// Set demod register bytes with writing bytes.
	// Note: Set demod register bytes one by one.
	for(i = 0; i < ByteNum; i++)
	{

		// Set demod register bytes with writing buffer.
			if(write_demod_register(d, DeviceAddr, PageNo,  RegStartAddr+i,  (unsigned char*)pWritingBytes+i, LEN_1_BYTE))
			goto error_status_set_demod_registers;	
	
	}


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





// Internal function:
// Set register bytes continuously.
int
internal_dtmb_demod_addr_8bit_SetRegBytesContinuously(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned long i, j;

	unsigned char DeviceAddr;
	unsigned char WritingBuffer[I2C_BUFFER_LEN];
	unsigned long WritingByteNum, WritingByteNumMax, WritingByteNumRem;
	unsigned char RegWritingAddr;



	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Calculate maximum writing byte number.
	WritingByteNumMax = pBaseInterface->I2cWritingByteNumMax - LEN_1_BYTE;


	// Set demod register bytes with writing bytes.
	// Note: Set demod register bytes considering maximum writing byte number.
	for(i = 0; i < ByteNum; i += WritingByteNumMax)
	{
		// Set register writing address.
		RegWritingAddr = (unsigned char)(RegStartAddr + i);

		// Calculate remainder writing byte number.
		WritingByteNumRem = ByteNum - i;

		// Determine writing byte number.
		WritingByteNum = (WritingByteNumRem > WritingByteNumMax) ? WritingByteNumMax : WritingByteNumRem;


		// Set writing buffer.
		// Note: The I2C format of demod register byte setting is as follows:
		//       start_bit + (DeviceAddr | writing_bit) + RegWritingAddr + writing_bytes (WritingByteNum bytes) + stop_bit
		WritingBuffer[0] = RegWritingAddr;

		for(j = 0; j < WritingByteNum; j++)
			WritingBuffer[LEN_1_BYTE + j] = pWritingBytes[i + j];


		// Set demod register bytes with writing buffer.
		if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, WritingBuffer, WritingByteNum + LEN_1_BYTE) !=
			FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_SET_REG_BYTES

*/
int
dtmb_demod_addr_8bit_default_SetRegBytes(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	)
{
	unsigned long CurrentPageNo;		// temp, because page register is write-only.



	// Get page register number.
	CurrentPageNo = pDemod->CurrentPageNo;		// temp, because page register is write-only.


	// Set register bytes according to page register number.
	switch(CurrentPageNo)
	{
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:

			if(internal_dtmb_demod_addr_8bit_SetRegBytesSeparately(pDemod, RegStartAddr, pWritingBytes, ByteNum) != FUNCTION_SUCCESS)
				goto error_status_set_demod_registers;

			break;


		default:
			
			goto error_status_set_demod_registers;
			
			break;
	}


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





// Internal function:
// Get register bytes separately.
int
internal_dtmb_demod_addr_8bit_GetRegBytesSeparately(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
#if 0
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned long i;

	unsigned char DeviceAddr;
	unsigned char RegAddr;
	unsigned char RegByte;



	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Get demod register bytes.
	// Note: Get demod register bytes one by one.
	for(i = 0; i < ByteNum; i++)
	{
		// Set demod register reading address.
		// Note: The I2C format of demod register reading address setting is as follows:
		//       start_bit + (DeviceAddr | writing_bit) + RegReadingAddr + stop_bit
		RegAddr = (unsigned char)(RegStartAddr + i);

		if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, &RegAddr, LEN_1_BYTE) != FUNCTION_SUCCESS)
			goto error_status_set_demod_register_reading_address;

		// Get demod register bytes.
		// Note: The I2C format of demod register byte getting is as follows:
		//       start_bit + (DeviceAddr | reading_bit) + reading_bytes (ReadingByteNum bytes) + stop_bit
		if(pBaseInterface->I2cRead(pBaseInterface, DeviceAddr, &RegByte, LEN_1_BYTE) != FUNCTION_SUCCESS)
			goto error_status_get_demod_registers;

		// Copy register byte to reading bytes.
		pReadingBytes[i] = RegByte;
	}


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_set_demod_register_reading_address:
	return FUNCTION_ERROR;
#endif
	BASE_INTERFACE_MODULE *pBaseInterface;
        struct dvb_usb_device	*d;
	unsigned int i;

	unsigned char DeviceAddr;

	unsigned long PageNo = pDemod->CurrentPageNo;
		
	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;

	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);

	// Get user defined data pointer of base interface structure for context.
	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);


	// Get demod register bytes.
	// Note: Get demod register bytes one by one.
	for(i = 0; i < ByteNum; i++)
	{

		// Get demod register bytes.
		if(read_demod_register(d, DeviceAddr, PageNo,  RegStartAddr+i,  pReadingBytes+i, LEN_1_BYTE))
			goto error_status_get_demod_registers;	



		
	}


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
//error_status_set_demod_register_reading_address:
	return FUNCTION_ERROR;
}





// Internal function:
// Get register bytes continuously.
int
internal_dtmb_demod_addr_8bit_GetRegBytesContinuously(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
#if 0
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned long i;
	unsigned char DeviceAddr;
	unsigned long ReadingByteNum, ReadingByteNumMax, ReadingByteNumRem;
	unsigned char RegReadingAddr;



	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Calculate maximum reading byte number.
	ReadingByteNumMax = pBaseInterface->I2cReadingByteNumMax;


	// Get demod register bytes.
	// Note: Get demod register bytes considering maximum reading byte number.
	for(i = 0; i < ByteNum; i += ReadingByteNumMax)
	{
		// Set register reading address.
		RegReadingAddr = (unsigned char)(RegStartAddr + i);

		// Calculate remainder reading byte number.
		ReadingByteNumRem = ByteNum - i;

		// Determine reading byte number.
		ReadingByteNum = (ReadingByteNumRem > ReadingByteNumMax) ? ReadingByteNumMax : ReadingByteNumRem;


		// Set demod register reading address.
		// Note: The I2C format of demod register reading address setting is as follows:
		//       start_bit + (DeviceAddr | writing_bit) + RegReadingAddr + stop_bit
		if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, &RegReadingAddr, LEN_1_BYTE) != FUNCTION_SUCCESS)
			goto error_status_set_demod_register_reading_address;

		// Get demod register bytes.
		// Note: The I2C format of demod register byte getting is as follows:
		//       start_bit + (DeviceAddr | reading_bit) + reading_bytes (ReadingByteNum bytes) + stop_bit
		if(pBaseInterface->I2cRead(pBaseInterface, DeviceAddr, &pReadingBytes[i], ReadingByteNum) != FUNCTION_SUCCESS)
			goto error_status_get_demod_registers;
	}


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_set_demod_register_reading_address:
	return FUNCTION_ERROR;
#endif
	BASE_INTERFACE_MODULE *pBaseInterface;
        struct dvb_usb_device	*d;
	
	unsigned char DeviceAddr;
	
	unsigned long PageNo = pDemod->CurrentPageNo;
		
	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;

	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);

	// Get user defined data pointer of base interface structure for context.
	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);


	// Get demod register bytes.
	if(read_demod_register(d, DeviceAddr, PageNo,  RegStartAddr,  pReadingBytes, ByteNum))
		goto error_status_get_demod_registers;	

	return FUNCTION_SUCCESS;


error_status_get_demod_registers:

	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_REG_BYTES

*/
int
dtmb_demod_addr_8bit_default_GetRegBytes(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
	unsigned long CurrentPageNo;		// temp, because page register is write-only.



	// Get page register number.
	CurrentPageNo = pDemod->CurrentPageNo;		// temp, because page register is write-only.


	// Get register bytes according to page register number.
	switch(CurrentPageNo)
	{
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:

			if(internal_dtmb_demod_addr_8bit_GetRegBytesSeparately(pDemod, RegStartAddr, pReadingBytes, ByteNum) != FUNCTION_SUCCESS)
				goto error_status_get_demod_registers;

			break;


		case 5:
		case 6:
		case 7:
		case 8:
		case 9:

			if(internal_dtmb_demod_addr_8bit_GetRegBytesContinuously(pDemod, RegStartAddr, pReadingBytes, ByteNum) != FUNCTION_SUCCESS)
				goto error_status_get_demod_registers;

			break;


		default:
			
			goto error_status_get_demod_registers;
			
			break;
	}


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_SET_REG_MASK_BITS

*/
int
dtmb_demod_addr_8bit_default_SetRegMaskBits(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	)
{
	int i;

	unsigned char ReadingBytes[LEN_4_BYTE];
	unsigned char WritingBytes[LEN_4_BYTE];

	unsigned char ByteNum;
	unsigned long Mask;
	unsigned char Shift;

	unsigned long Value;


	// Calculate writing byte number according to MSB.
	ByteNum = Msb / BYTE_BIT_NUM + LEN_1_BYTE;


	// Generate mask and shift according to MSB and LSB.
	Mask = 0;

	for(i = Lsb; i < (unsigned char)(Msb + 1); i++)
		Mask |= 0x1 << i;

	Shift = Lsb;


	// Get demod register bytes according to register start adddress and byte number.
	if(pDemod->RegAccess.Addr8Bit.GetRegBytes(pDemod, RegStartAddr, ReadingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Combine reading bytes into an unsigned integer value.
	// Note: Put lower address byte on value LSB.
	//       Put upper address byte on value MSB.
	Value = 0;

	for(i = 0; i < ByteNum; i++)
		Value |= (unsigned long)ReadingBytes[i] << (BYTE_SHIFT * i);


	// Reserve unsigned integer value unmask bit with mask and inlay writing value into it.
	Value &= ~Mask;
	Value |= (WritingValue << Shift) & Mask;


	// Separate unsigned integer value into writing bytes.
	// Note: Pick up lower address byte from value LSB.
	//       Pick up upper address byte from value MSB.
	for(i = 0; i < ByteNum; i++)
		WritingBytes[i] = (unsigned char)((Value >> (BYTE_SHIFT * i)) & BYTE_MASK);


	// Write demod register bytes with writing bytes.
	if(pDemod->RegAccess.Addr8Bit.SetRegBytes(pDemod, RegStartAddr, WritingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_REG_MASK_BITS

*/
int
dtmb_demod_addr_8bit_default_GetRegMaskBits(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	)
{
	int i;

	unsigned char ReadingBytes[LEN_4_BYTE];

	unsigned char ByteNum;
	unsigned long Mask;
	unsigned char Shift;

	unsigned long Value;


	// Calculate writing byte number according to MSB.
	ByteNum = Msb / BYTE_BIT_NUM + LEN_1_BYTE;


	// Generate mask and shift according to MSB and LSB.
	Mask = 0;

	for(i = Lsb; i < (unsigned char)(Msb + 1); i++)
		Mask |= 0x1 << i;

	Shift = Lsb;


	// Get demod register bytes according to register start adddress and byte number.
	if(pDemod->RegAccess.Addr8Bit.GetRegBytes(pDemod, RegStartAddr, ReadingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Combine reading bytes into an unsigned integer value.
	// Note: Put lower address byte on value LSB.
	//       Put upper address byte on value MSB.
	Value = 0;

	for(i = 0; i < ByteNum; i++)
		Value |= (unsigned long)ReadingBytes[i] << (BYTE_SHIFT * i);


	// Get register bits from unsigned integaer value with mask and shift
	*pReadingValue = (Value & Mask) >> Shift;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_SET_REG_BITS

*/
int
dtmb_demod_addr_8bit_default_SetRegBits(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	)
{
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;


	// Check if register bit name is available.
	if(pDemod->RegTable.Addr8Bit[RegBitName].IsAvailable == NO)
		goto error_status_register_bit_name;


	// Get register start address, MSB, and LSB from register table with register bit name key.
	RegStartAddr = pDemod->RegTable.Addr8Bit[RegBitName].RegStartAddr;
	Msb          = pDemod->RegTable.Addr8Bit[RegBitName].Msb;
	Lsb          = pDemod->RegTable.Addr8Bit[RegBitName].Lsb;


	// Set register mask bits.
	if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, WritingValue) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_register_bit_name:
error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_REG_BITS

*/
int
dtmb_demod_addr_8bit_default_GetRegBits(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	)
{
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;


	// Check if register bit name is available.
	if(pDemod->RegTable.Addr8Bit[RegBitName].IsAvailable == NO)
		goto error_status_register_bit_name;


	// Get register start address, MSB, and LSB from register table with register bit name key.
	RegStartAddr = pDemod->RegTable.Addr8Bit[RegBitName].RegStartAddr;
	Msb          = pDemod->RegTable.Addr8Bit[RegBitName].Msb;
	Lsb          = pDemod->RegTable.Addr8Bit[RegBitName].Lsb;


	// Get register mask bits.
	if(pDemod->RegAccess.Addr8Bit.GetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, pReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	return FUNCTION_SUCCESS;


error_status_register_bit_name:
error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_SET_REG_BITS_WITH_PAGE

*/
int
dtmb_demod_addr_8bit_default_SetRegBitsWithPage(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	)
{
	unsigned long PageNo;


	// Get register page number from register table with register bit name key.
	PageNo = pDemod->RegTable.Addr8Bit[RegBitName].PageNo;


	// Set register page number.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, PageNo) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set register mask bits with register bit name key.
	if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, RegBitName, WritingValue) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_REG_BITS_WITH_PAGE

*/
int
dtmb_demod_addr_8bit_default_GetRegBitsWithPage(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	)
{
	unsigned long PageNo;


	// Get register page number from register table with register bit name key.
	PageNo = pDemod->RegTable.Addr8Bit[RegBitName].PageNo;


	// Set register page number.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, PageNo) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Get register mask bits with register bit name key.
	if(pDemod->RegAccess.Addr8Bit.GetRegBits(pDemod, RegBitName, pReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_SET_REG_BYTES

*/
int
dtmb_demod_addr_16bit_default_SetRegBytes(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned long i, j;

	unsigned char DeviceAddr;
	unsigned char WritingBuffer[I2C_BUFFER_LEN];
	unsigned long WritingByteNum, WritingByteNumMax, WritingByteNumRem;
	unsigned short RegWritingAddr;



	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Calculate maximum writing byte number.
	WritingByteNumMax = pBaseInterface->I2cWritingByteNumMax - LEN_2_BYTE;


	// Set demod register bytes with writing bytes.
	// Note: Set demod register bytes considering maximum writing byte number.
	for(i = 0; i < ByteNum; i += WritingByteNumMax)
	{
		// Set register writing address.
		RegWritingAddr = (unsigned short)(RegStartAddr + i);

		// Calculate remainder writing byte number.
		WritingByteNumRem = ByteNum - i;

		// Determine writing byte number.
		WritingByteNum = (WritingByteNumRem > WritingByteNumMax) ? WritingByteNumMax : WritingByteNumRem;


		// Set writing buffer.
		// Note: The I2C format of demod register byte setting is as follows:
		//       start_bit + (DeviceAddr | writing_bit) + RegWritingAddrMsb + RegWritingAddrLsb +
		//       writing_bytes (WritingByteNum bytes) + stop_bit
		WritingBuffer[0] = (RegWritingAddr >> BYTE_SHIFT) & BYTE_MASK;
		WritingBuffer[1] = RegWritingAddr & BYTE_MASK;

		for(j = 0; j < WritingByteNum; j++)
			WritingBuffer[LEN_2_BYTE + j] = pWritingBytes[i + j];


		// Set demod register bytes with writing buffer.
		if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, WritingBuffer, WritingByteNum + LEN_2_BYTE) !=
			FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





// Internal function:
// Get register bytes normally.
int
internal_dtmb_demod_addr_16bit_GetRegBytesNormally(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned long i;
	unsigned char DeviceAddr;
	unsigned long ReadingByteNum, ReadingByteNumMax, ReadingByteNumRem;
	unsigned short RegReadingAddr;
	unsigned char WritingBuffer[LEN_2_BYTE];



	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Calculate maximum reading byte number.
	ReadingByteNumMax = pBaseInterface->I2cReadingByteNumMax;


	// Get demod register bytes.
	// Note: Get demod register bytes considering maximum reading byte number.
	for(i = 0; i < ByteNum; i += ReadingByteNumMax)
	{
		// Set register reading address.
		RegReadingAddr = (unsigned short)(RegStartAddr + i);

		// Calculate remainder reading byte number.
		ReadingByteNumRem = ByteNum - i;

		// Determine reading byte number.
		ReadingByteNum = (ReadingByteNumRem > ReadingByteNumMax) ? ReadingByteNumMax : ReadingByteNumRem;


		// Set demod register reading address.
		// Note: The I2C format of demod register reading address setting is as follows:
		//       start_bit + (DeviceAddr | writing_bit) + RegReadingAddrMsb + RegReadingAddrLsb + stop_bit
		WritingBuffer[0] = (RegReadingAddr >> BYTE_SHIFT) & BYTE_MASK;
		WritingBuffer[1] = RegReadingAddr & BYTE_MASK;

		if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, WritingBuffer, LEN_2_BYTE) != FUNCTION_SUCCESS)
			goto error_status_set_demod_register_reading_address;

		// Get demod register bytes.
		// Note: The I2C format of demod register byte getting is as follows:
		//       start_bit + (DeviceAddr | reading_bit) + reading_bytes (ReadingByteNum bytes) + stop_bit
		if(pBaseInterface->I2cRead(pBaseInterface, DeviceAddr, &pReadingBytes[i], ReadingByteNum) != FUNCTION_SUCCESS)
			goto error_status_get_demod_registers;
	}


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_set_demod_register_reading_address:
	return FUNCTION_ERROR;
}





// Internal function:
// Get register bytes with freeze.

#define DTMB_I2C_REG_DEBUG_PAGE_ADDR		0xc708
#define DTMB_I2C_REG_DEBUG_ADDR_ADDR		0xc709
#define DTMB_I2C_REG_DEBUG_FREEZE_ADDR		0xc70a
#define DTMB_I2C_REG_DEBUG_BYTE_ADDR		0xc70b

int
internal_dtmb_demod_addr_16bit_GetRegBytesWithFreeze(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned long i;
	unsigned char DeviceAddr;

	unsigned char WritingBuffer[LEN_4_BYTE];
	unsigned char ReadingBuffer[LEN_4_BYTE];



	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);



	// Note: The I2C format of demod register byte setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + RegWritingAddrMsb + RegWritingAddrLsb + writing_bytes (WritingByteNum bytes) + stop_bit

	// Note: The I2C format of demod register byte getting is as follows:
	//       start_bit + (DeviceAddr | reading_bit) + reading_bytes (ReadingByteNum bytes) + stop_bit

	// Set I2C_REG_DEBUG_PAGE and I2C_REG_DEBUG_ADDR with register start address.
	// Note: 1. Set I2C_REG_DEBUG_PAGE with register start address [11:8].
	//       2. Set I2C_REG_DEBUG_ADDR with register start address [7:0].
	WritingBuffer[0] = (DTMB_I2C_REG_DEBUG_PAGE_ADDR >> BYTE_SHIFT) & BYTE_MASK;
	WritingBuffer[1] = DTMB_I2C_REG_DEBUG_PAGE_ADDR & BYTE_MASK;
	WritingBuffer[2] = (RegStartAddr >> BYTE_SHIFT) & HEX_DIGIT_MASK;
	WritingBuffer[3] = RegStartAddr & BYTE_MASK;

	if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, WritingBuffer, LEN_4_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set I2C_REG_DEBUG_FREEZE with 0x1.
	WritingBuffer[0] = (DTMB_I2C_REG_DEBUG_FREEZE_ADDR >> BYTE_SHIFT) & BYTE_MASK;
	WritingBuffer[1] = DTMB_I2C_REG_DEBUG_FREEZE_ADDR & BYTE_MASK;
	WritingBuffer[2] = 0x1;

	if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, WritingBuffer, LEN_3_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Get I2C_REG_DEBUG_BYTE 4 bytes.
	if(internal_dtmb_demod_addr_16bit_GetRegBytesNormally(pDemod, DTMB_I2C_REG_DEBUG_BYTE_ADDR, ReadingBuffer, LEN_4_BYTE) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Arrange reading bytes from big-endian to little-endian.
	// Note: 1. The bytes format reading from I2C_REG_DEBUG_BYTE is big-endian.
	//       2. The bytes format we needs is little-endian.
	for(i = 0; i < ByteNum; i++)
	{
		// Set reading bytes.
		pReadingBytes[i] = ReadingBuffer[LEN_3_BYTE - i];
	}


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_REG_BYTES

*/
int
dtmb_demod_addr_16bit_default_GetRegBytes(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
	unsigned char RegStartAddrMsb;



	// Get regiser start address MSB.
	RegStartAddrMsb = (RegStartAddr >> BYTE_SHIFT) & BYTE_MASK;


	// Get register bytes according to page register number.
	switch(RegStartAddrMsb)
	{
		case 0xc0:
		case 0xc1:
		case 0xc2:
		case 0xc3:
		case 0xc4:
		case 0xc5:
		case 0xc6:
		case 0xc7:
		case 0xe0:
		case 0xe1:
		case 0xe2:
		case 0xe3:
		case 0xe4:
		case 0xf0:

			if(internal_dtmb_demod_addr_16bit_GetRegBytesNormally(pDemod, RegStartAddr, pReadingBytes, ByteNum) != FUNCTION_SUCCESS)
				goto error_status_get_demod_registers;

			break;


		case 0xc8:
		case 0xc9:
		case 0xca:
		case 0xcb:
		case 0xcc:
		case 0xcd:

			if(internal_dtmb_demod_addr_16bit_GetRegBytesWithFreeze(pDemod, RegStartAddr, pReadingBytes, ByteNum) != FUNCTION_SUCCESS)
				goto error_status_get_demod_registers;

			break;


		default:
			
			goto error_status_get_demod_registers;
			
			break;
	}


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_SET_REG_MASK_BITS

*/
int
dtmb_demod_addr_16bit_default_SetRegMaskBits(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	)
{
	int i;

	unsigned char ReadingBytes[LEN_4_BYTE];
	unsigned char WritingBytes[LEN_4_BYTE];

	unsigned char ByteNum;
	unsigned long Mask;
	unsigned char Shift;

	unsigned long Value;


	// Calculate writing byte number according to MSB.
	ByteNum = Msb / BYTE_BIT_NUM + LEN_1_BYTE;


	// Generate mask and shift according to MSB and LSB.
	Mask = 0;

	for(i = Lsb; i < (unsigned char)(Msb + 1); i++)
		Mask |= 0x1 << i;

	Shift = Lsb;


	// Get demod register bytes according to register start adddress and byte number.
	if(pDemod->RegAccess.Addr16Bit.GetRegBytes(pDemod, RegStartAddr, ReadingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Combine reading bytes into an unsigned integer value.
	// Note: Put lower address byte on value LSB.
	//       Put upper address byte on value MSB.
	Value = 0;

	for(i = 0; i < ByteNum; i++)
		Value |= (unsigned long)ReadingBytes[i] << (BYTE_SHIFT * i);


	// Reserve unsigned integer value unmask bit with mask and inlay writing value into it.
	Value &= ~Mask;
	Value |= (WritingValue << Shift) & Mask;


	// Separate unsigned integer value into writing bytes.
	// Note: Pick up lower address byte from value LSB.
	//       Pick up upper address byte from value MSB.
	for(i = 0; i < ByteNum; i++)
		WritingBytes[i] = (unsigned char)((Value >> (BYTE_SHIFT * i)) & BYTE_MASK);


	// Write demod register bytes with writing bytes.
	if(pDemod->RegAccess.Addr16Bit.SetRegBytes(pDemod, RegStartAddr, WritingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_REG_MASK_BITS

*/
int
dtmb_demod_addr_16bit_default_GetRegMaskBits(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	)
{
	int i;

	unsigned char ReadingBytes[LEN_4_BYTE];

	unsigned char ByteNum;
	unsigned long Mask;
	unsigned char Shift;

	unsigned long Value;


	// Calculate writing byte number according to MSB.
	ByteNum = Msb / BYTE_BIT_NUM + LEN_1_BYTE;


	// Generate mask and shift according to MSB and LSB.
	Mask = 0;

	for(i = Lsb; i < (unsigned char)(Msb + 1); i++)
		Mask |= 0x1 << i;

	Shift = Lsb;


	// Get demod register bytes according to register start adddress and byte number.
	if(pDemod->RegAccess.Addr16Bit.GetRegBytes(pDemod, RegStartAddr, ReadingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Combine reading bytes into an unsigned integer value.
	// Note: Put lower address byte on value LSB.
	//       Put upper address byte on value MSB.
	Value = 0;

	for(i = 0; i < ByteNum; i++)
		Value |= (unsigned long)ReadingBytes[i] << (BYTE_SHIFT * i);


	// Get register bits from unsigned integaer value with mask and shift
	*pReadingValue = (Value & Mask) >> Shift;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_SET_REG_BITS

*/
int
dtmb_demod_addr_16bit_default_SetRegBits(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	)
{
	unsigned short RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;


	// Check if register bit name is available.
	if(pDemod->RegTable.Addr16Bit[RegBitName].IsAvailable == NO)
		goto error_status_register_bit_name;


	// Get register start address, MSB, and LSB from register table with register bit name key.
	RegStartAddr = pDemod->RegTable.Addr16Bit[RegBitName].RegStartAddr;
	Msb          = pDemod->RegTable.Addr16Bit[RegBitName].Msb;
	Lsb          = pDemod->RegTable.Addr16Bit[RegBitName].Lsb;


	// Set register mask bits.
	if(pDemod->RegAccess.Addr16Bit.SetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, WritingValue) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_register_bit_name:
error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_REG_BITS

*/
int
dtmb_demod_addr_16bit_default_GetRegBits(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	)
{
	unsigned short RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;


	// Check if register bit name is available.
	if(pDemod->RegTable.Addr16Bit[RegBitName].IsAvailable == NO)
		goto error_status_register_bit_name;


	// Get register start address, MSB, and LSB from register table with register bit name key.
	RegStartAddr = pDemod->RegTable.Addr16Bit[RegBitName].RegStartAddr;
	Msb          = pDemod->RegTable.Addr16Bit[RegBitName].Msb;
	Lsb          = pDemod->RegTable.Addr16Bit[RegBitName].Lsb;


	// Get register mask bits.
	if(pDemod->RegAccess.Addr16Bit.GetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, pReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	return FUNCTION_SUCCESS;


error_status_register_bit_name:
error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_DEMOD_TYPE

*/
void
dtmb_demod_default_GetDemodType(
	DTMB_DEMOD_MODULE *pDemod,
	int *pDemodType
	)
{
	// Get demod type from demod module.
	*pDemodType = pDemod->DemodType;


	return;
}





/**

@see   DTMB_DEMOD_FP_GET_DEVICE_ADDR

*/
void
dtmb_demod_default_GetDeviceAddr(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char *pDeviceAddr
	)
{
	// Get demod I2C device address from demod module.
	*pDeviceAddr = pDemod->DeviceAddr;


	return;
}





/**

@see   DTMB_DEMOD_FP_GET_CRYSTAL_FREQ_HZ

*/
void
dtmb_demod_default_GetCrystalFreqHz(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pCrystalFreqHz
	)
{
	// Get demod crystal frequency in Hz from demod module.
	*pCrystalFreqHz = pDemod->CrystalFreqHz;


	return;
}





/**

@see   DTMB_DEMOD_FP_GET_IF_FREQ_HZ

*/
int
dtmb_demod_default_GetIfFreqHz(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pIfFreqHz
	)
{
	// Get demod IF frequency in Hz from demod module.
	if(pDemod->IsIfFreqHzSet != YES)
		goto error_status_get_demod_if_frequency;

	*pIfFreqHz = pDemod->IfFreqHz;


	return FUNCTION_SUCCESS;


error_status_get_demod_if_frequency:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_SPECTRUM_MODE

*/
int
dtmb_demod_default_GetSpectrumMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pSpectrumMode
	)
{
	// Get demod spectrum mode from demod module.
	if(pDemod->IsSpectrumModeSet != YES)
		goto error_status_get_demod_spectrum_mode;

	*pSpectrumMode = pDemod->SpectrumMode;


	return FUNCTION_SUCCESS;


error_status_get_demod_spectrum_mode:
	return FUNCTION_ERROR;
}












