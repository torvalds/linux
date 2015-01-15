/**

@file

@brief   DVB-T demod default function definition

DVB-T demod default functions.

*/

#include "dvbt_demod_base.h"





/**

@see   DVBT_DEMOD_FP_SET_REG_PAGE

*/
int
dvbt_demod_default_SetRegPage(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long PageNo
	)
{
#if 0
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned char DeviceAddr;
	unsigned char WritingBytes[LEN_2_BYTE];


	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Get demod device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Set demod register page with page number.
	// Note: The I2C format of demod register page setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + DVBT_DEMOD_PAGE_REG_ADDR + PageNo + stop_bit
	WritingBytes[0] = DVBT_DEMOD_PAGE_REG_ADDR;
	WritingBytes[1] = (unsigned char)PageNo;

	if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, WritingBytes, LEN_2_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
#endif
	BASE_INTERFACE_MODULE *pBaseInterface;

	struct dvb_usb_device	*d;

	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;
	
	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);  //add by chialing

	if( mutex_lock_interruptible(&d->usb_mutex) )	goto error;

	 pDemod->CurrentPageNo = PageNo;

	mutex_unlock(&d->usb_mutex);
	
	return FUNCTION_SUCCESS;

error:
	return FUNCTION_ERROR;

}





/**

@see   DVBT_DEMOD_FP_SET_REG_BYTES

*/
int
dvbt_demod_default_SetRegBytes(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned char ByteNum
	)
{
#if 0
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned int i, j;

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
		RegWritingAddr = RegStartAddr + i;

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
#endif
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned int i, j;

	unsigned char DeviceAddr;
	unsigned char WritingBuffer[I2C_BUFFER_LEN];
	unsigned char WritingByteNum, WritingByteNumMax, WritingByteNumRem;
	unsigned char RegWritingAddr;
	
	struct dvb_usb_device	*d;

	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;
	
	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);  //add by chialing

	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Calculate maximum writing byte number.
	WritingByteNumMax = pBaseInterface->I2cWritingByteNumMax - LEN_1_BYTE;


	// Set demod register bytes with writing bytes.
	// Note: Set demod register bytes considering maximum writing byte number.
	for(i = 0; i < ByteNum; i += WritingByteNumMax)
	{
		// Set register writing address.
		RegWritingAddr = RegStartAddr + i;

		// Calculate remainder writing byte number.
		WritingByteNumRem = ByteNum - i;

		// Determine writing byte number.
		WritingByteNum = (WritingByteNumRem > WritingByteNumMax) ? WritingByteNumMax : WritingByteNumRem;


		for(j = 0; j < WritingByteNum; j++)
			WritingBuffer[j] = pWritingBytes[i + j];

		// Set demod register bytes with writing buffer.
		if(write_demod_register( d, DeviceAddr, pDemod->CurrentPageNo, RegWritingAddr, WritingBuffer, WritingByteNum )) goto error;

		
	}


	return FUNCTION_SUCCESS;
error:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_REG_BYTES

*/
int
dvbt_demod_default_GetRegBytes(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned char ByteNum
	)
{
#if 0
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned int i;
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
		RegReadingAddr = RegStartAddr + i;

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

	unsigned int i;
	unsigned char DeviceAddr;
	unsigned char ReadingByteNum, ReadingByteNumMax, ReadingByteNumRem;
	unsigned char RegReadingAddr;

	struct dvb_usb_device	*d;

	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;

	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);  //add by chialing

	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);


	// Calculate maximum reading byte number.
	ReadingByteNumMax = pBaseInterface->I2cReadingByteNumMax;


	// Get demod register bytes.
	// Note: Get demod register bytes considering maximum reading byte number.
	for(i = 0; i < ByteNum; i += ReadingByteNumMax)
	{
		// Set register reading address.
		RegReadingAddr = RegStartAddr + i;

		// Calculate remainder reading byte number.
		ReadingByteNumRem = ByteNum - i;

		// Determine reading byte number.
		ReadingByteNum = (ReadingByteNumRem > ReadingByteNumMax) ? ReadingByteNumMax : ReadingByteNumRem;


		// Get demod register bytes.
		if(read_demod_register(d, DeviceAddr, pDemod->CurrentPageNo, RegReadingAddr, &pReadingBytes[i], ReadingByteNum)) goto error;

	}


	return FUNCTION_SUCCESS;

error:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_SET_REG_MASK_BITS

*/
int
dvbt_demod_default_SetRegMaskBits(
	DVBT_DEMOD_MODULE *pDemod,
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
	if(pDemod->GetRegBytes(pDemod, RegStartAddr, ReadingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Combine reading bytes into an unsigned integer value.
	// Note: Put lower address byte on value MSB.
	//       Put upper address byte on value LSB.
	Value = 0;

	for(i = 0; i < ByteNum; i++)
		Value |= (unsigned long)ReadingBytes[i] << (BYTE_SHIFT * (ByteNum - i -1));


	// Reserve unsigned integer value unmask bit with mask and inlay writing value into it.
	Value &= ~Mask;
	Value |= (WritingValue << Shift) & Mask;


	// Separate unsigned integer value into writing bytes.
	// Note: Pick up lower address byte from value MSB.
	//       Pick up upper address byte from value LSB.
	for(i = 0; i < ByteNum; i++)
		WritingBytes[i] = (unsigned char)((Value >> (BYTE_SHIFT * (ByteNum - i -1))) & BYTE_MASK);


	// Write demod register bytes with writing bytes.
	if(pDemod->SetRegBytes(pDemod, RegStartAddr, WritingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_REG_MASK_BITS

*/
int
dvbt_demod_default_GetRegMaskBits(
	DVBT_DEMOD_MODULE *pDemod,
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
	if(pDemod->GetRegBytes(pDemod, RegStartAddr, ReadingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Combine reading bytes into an unsigned integer value.
	// Note: Put lower address byte on value MSB.
	//       Put upper address byte on value LSB.
	Value = 0;

	for(i = 0; i < ByteNum; i++)
		Value |= (unsigned long)ReadingBytes[i] << (BYTE_SHIFT * (ByteNum - i -1));


	// Get register bits from unsigned integaer value with mask and shift
	*pReadingValue = (Value & Mask) >> Shift;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_SET_REG_BITS

*/
int
dvbt_demod_default_SetRegBits(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	)
{
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;


	// Check if register bit name is available.
	if(pDemod->RegTable[RegBitName].IsAvailable == NO)
		goto error_status_register_bit_name;


	// Get register start address, MSB, and LSB from register table with register bit name key.
	RegStartAddr = pDemod->RegTable[RegBitName].RegStartAddr;
	Msb          = pDemod->RegTable[RegBitName].Msb;
	Lsb          = pDemod->RegTable[RegBitName].Lsb;


	// Set register mask bits.
	if(pDemod->SetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, WritingValue) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_register_bit_name:
error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_REG_BITS

*/
int
dvbt_demod_default_GetRegBits(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	)
{
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;


	// Check if register bit name is available.
	if(pDemod->RegTable[RegBitName].IsAvailable == NO)
		goto error_status_register_bit_name;


	// Get register start address, MSB, and LSB from register table with register bit name key.
	RegStartAddr = pDemod->RegTable[RegBitName].RegStartAddr;
	Msb          = pDemod->RegTable[RegBitName].Msb;
	Lsb          = pDemod->RegTable[RegBitName].Lsb;


	// Get register mask bits.
	if(pDemod->GetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, pReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	return FUNCTION_SUCCESS;


error_status_register_bit_name:
error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_SET_REG_BITS_WITH_PAGE

*/
int
dvbt_demod_default_SetRegBitsWithPage(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	)
{
	unsigned long PageNo;


	// Get register page number from register table with register bit name key.
	PageNo = pDemod->RegTable[RegBitName].PageNo;


	// Set register page number.
	if(pDemod->SetRegPage(pDemod, PageNo) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set register mask bits with register bit name key.
	if(pDemod->SetRegBits(pDemod, RegBitName, WritingValue) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_REG_BITS_WITH_PAGE

*/
int
dvbt_demod_default_GetRegBitsWithPage(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	)
{
	unsigned long PageNo;


	// Get register page number from register table with register bit name key.
	PageNo = pDemod->RegTable[RegBitName].PageNo;


	// Set register page number.
	if(pDemod->SetRegPage(pDemod, PageNo) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Get register mask bits with register bit name key.
	if(pDemod->GetRegBits(pDemod, RegBitName, pReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_DEMOD_TYPE

*/
void
dvbt_demod_default_GetDemodType(
	DVBT_DEMOD_MODULE *pDemod,
	int *pDemodType
	)
{
	// Get demod type from demod module.
	*pDemodType = pDemod->DemodType;


	return;
}





/**

@see   DVBT_DEMOD_FP_GET_DEVICE_ADDR

*/
void
dvbt_demod_default_GetDeviceAddr(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char *pDeviceAddr
	)
{
	// Get demod I2C device address from demod module.
	*pDeviceAddr = pDemod->DeviceAddr;


	return;
}





/**

@see   DVBT_DEMOD_FP_GET_CRYSTAL_FREQ_HZ

*/
void
dvbt_demod_default_GetCrystalFreqHz(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pCrystalFreqHz
	)
{
	// Get demod crystal frequency in Hz from demod module.
	*pCrystalFreqHz = pDemod->CrystalFreqHz;


	return;
}





/**

@see   DVBT_DEMOD_FP_GET_BANDWIDTH_MODE

*/
int
dvbt_demod_default_GetBandwidthMode(
	DVBT_DEMOD_MODULE *pDemod,
	int *pBandwidthMode
	)
{
	// Get demod bandwidth mode from demod module.
	if(pDemod->IsBandwidthModeSet != YES)
		goto error_status_get_demod_bandwidth_mode;

	*pBandwidthMode = pDemod->BandwidthMode;


	return FUNCTION_SUCCESS;


error_status_get_demod_bandwidth_mode:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_IF_FREQ_HZ

*/
int
dvbt_demod_default_GetIfFreqHz(
	DVBT_DEMOD_MODULE *pDemod,
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

@see   DVBT_DEMOD_FP_GET_SPECTRUM_MODE

*/
int
dvbt_demod_default_GetSpectrumMode(
	DVBT_DEMOD_MODULE *pDemod,
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












