/**

@file

@brief   FC2580 tuner module definition

One can manipulate FC2580 tuner through FC2580 module.
FC2580 module is derived from tuner module.

*/


#include "tuner_fc2580.h"





/**

@brief   FC2580 tuner module builder

Use BuildFc2580Module() to build FC2580 module, set all module function pointers with the corresponding functions,
and initialize module private variables.


@param [in]   ppTuner                      Pointer to FC2580 tuner module pointer
@param [in]   pTunerModuleMemory           Pointer to an allocated tuner module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   FC2580 I2C device address
@param [in]   CrystalFreqHz                FC2580 crystal frequency in Hz
@param [in]   AgcMode                      FC2580 AGC mode


@note
	-# One should call BuildFc2580Module() to build FC2580 module before using it.

*/
void
BuildFc2580Module(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz,
	int AgcMode
	)
{
	TUNER_MODULE *pTuner;
	FC2580_EXTRA_MODULE *pExtra;



	// Set tuner module pointer.
	*ppTuner = pTunerModuleMemory;

	// Get tuner module.
	pTuner = *ppTuner;

	// Set base interface module pointer and I2C bridge module pointer.
	pTuner->pBaseInterface = pBaseInterfaceModuleMemory;
	pTuner->pI2cBridge     = pI2cBridgeModuleMemory;

	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc2580);



	// Set tuner type.
	pTuner->TunerType = TUNER_TYPE_FC2580;

	// Set tuner I2C device address.
	pTuner->DeviceAddr = DeviceAddr;


	// Initialize tuner parameter setting status.
	pTuner->IsRfFreqHzSet = NO;


	// Set tuner module manipulating function pointers.
	pTuner->GetTunerType  = fc2580_GetTunerType;
	pTuner->GetDeviceAddr = fc2580_GetDeviceAddr;

	pTuner->Initialize    = fc2580_Initialize;
	pTuner->SetRfFreqHz   = fc2580_SetRfFreqHz;
	pTuner->GetRfFreqHz   = fc2580_GetRfFreqHz;


	// Initialize tuner extra module variables.
	pExtra->CrystalFreqHz      = CrystalFreqHz;
	pExtra->AgcMode            = AgcMode;
	pExtra->IsBandwidthModeSet = NO;

	// Set tuner extra module function pointers.
	pExtra->SetBandwidthMode = fc2580_SetBandwidthMode;
	pExtra->GetBandwidthMode = fc2580_GetBandwidthMode;


	return;
}





/**

@see   TUNER_FP_GET_TUNER_TYPE

*/
void
fc2580_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	)
{
	// Get tuner type from tuner module.
	*pTunerType = pTuner->TunerType;


	return;
}





/**

@see   TUNER_FP_GET_DEVICE_ADDR

*/
void
fc2580_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	)
{
	// Get tuner I2C device address from tuner module.
	*pDeviceAddr = pTuner->DeviceAddr;


	return;
}





/**

@see   TUNER_FP_INITIALIZE

*/
int
fc2580_Initialize(
	TUNER_MODULE *pTuner
	)
{
	FC2580_EXTRA_MODULE *pExtra;
	int AgcMode;
	unsigned int CrystalFreqKhz;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc2580);


	// Get AGC mode.
	AgcMode = pExtra->AgcMode;

	// Initialize tuner with AGC mode.
	// Note: CrystalFreqKhz = round(CrystalFreqHz / 1000)
	CrystalFreqKhz = (unsigned int)((pExtra->CrystalFreqHz + 500) / 1000);

	if(fc2580_set_init(pTuner, AgcMode, CrystalFreqKhz) != FC2580_FCI_SUCCESS)
		goto error_status_initialize_tuner;


	return FUNCTION_SUCCESS;


error_status_initialize_tuner:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_SET_RF_FREQ_HZ

*/
int
fc2580_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	)
{
	FC2580_EXTRA_MODULE *pExtra;
	unsigned int RfFreqKhz;
	unsigned int CrystalFreqKhz;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc2580);


	// Set tuner RF frequency in KHz.
	// Note: RfFreqKhz = round(RfFreqHz / 1000)
	//       CrystalFreqKhz = round(CrystalFreqHz / 1000)
	RfFreqKhz = (unsigned int)((RfFreqHz + 500) / 1000);
	CrystalFreqKhz = (unsigned int)((pExtra->CrystalFreqHz + 500) / 1000);

	if(fc2580_set_freq(pTuner, RfFreqKhz, CrystalFreqKhz) != FC2580_FCI_SUCCESS)
		goto error_status_set_tuner_rf_frequency;


	// Set tuner RF frequency parameter.
	pTuner->RfFreqHz      = RfFreqHz;
	pTuner->IsRfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_rf_frequency:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_GET_RF_FREQ_HZ

*/
int
fc2580_GetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	)
{
	// Get tuner RF frequency in Hz from tuner module.
	if(pTuner->IsRfFreqHzSet != YES)
		goto error_status_get_tuner_rf_frequency;

	*pRfFreqHz = pTuner->RfFreqHz;


	return FUNCTION_SUCCESS;


error_status_get_tuner_rf_frequency:
	return FUNCTION_ERROR;
}





/**

@brief   Set FC2580 tuner bandwidth mode.

*/
int
fc2580_SetBandwidthMode(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	)
{
	FC2580_EXTRA_MODULE *pExtra;
	unsigned int CrystalFreqKhz;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc2580);


	// Set tuner bandwidth mode.
	// Note: CrystalFreqKhz = round(CrystalFreqHz / 1000)
	CrystalFreqKhz = (unsigned int)((pExtra->CrystalFreqHz + 500) / 1000);

	if(fc2580_set_filter(pTuner, (unsigned char)BandwidthMode, CrystalFreqKhz) != FC2580_FCI_SUCCESS)
		goto error_status_set_tuner_bandwidth_mode;


	// Set tuner bandwidth mode parameter.
	pExtra->BandwidthMode      = BandwidthMode;
	pExtra->IsBandwidthModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_bandwidth_mode:
	return FUNCTION_ERROR;
}





/**

@brief   Get FC2580 tuner bandwidth mode.

*/
int
fc2580_GetBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	)
{
	FC2580_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc2580);


	// Get tuner bandwidth mode from tuner module.
	if(pExtra->IsBandwidthModeSet != YES)
		goto error_status_get_tuner_bandwidth_mode;

	*pBandwidthMode = pExtra->BandwidthMode;


	return FUNCTION_SUCCESS;


error_status_get_tuner_bandwidth_mode:
	return FUNCTION_ERROR;
}























// The following context is source code provided by FCI.





// FCI source code - fc2580_driver_v14011_r.c


/*==============================================================================
    FILE NAME   : FC2580_driver_v1400_r.c
    
    VERSION     : 1.400_r
    
    UPDATE      : September 22. 2008
				
==============================================================================*/ 

/*==============================================================================

  Chip ID of FC2580 is 0x56 or 0xAC(including I2C write bit)

==============================================================================*/

//#include "fc2580_driver_v1400_r.h"

//fc2580_band_type curr_band = FC2580_NO_BAND;
//unsigned int freq_xtal = 16384;


/*==============================================================================
		milisecond delay function					EXTERNAL FUNCTION

  This function is a generic function which write a byte into fc2580's
 specific address.

  <input parameter>

  a
	length of wanted delay in milisecond unit

==============================================================================*/
void fc2580_wait_msec(TUNER_MODULE *pTuner, int a)
{
	BASE_INTERFACE_MODULE *pBaseInterface;


	// Get base interface.
	pBaseInterface = pTuner->pBaseInterface;


	// Wait time in millisecond.
	pBaseInterface->WaitMs(pBaseInterface, (unsigned long)a);


	return;
}

/*==============================================================================

           fc2580 i2c write

  This function is a generic function which write a byte into fc2580's
 specific address.

  <input parameter>

  addr
	fc2580's memory address\
	type : byte

  data
	target data
	type : byte

==============================================================================*/
fc2580_fci_result_type fc2580_i2c_write( TUNER_MODULE *pTuner, unsigned char addr, unsigned char data )
{
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;
	unsigned char WritingBytes[LEN_2_BYTE];


	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Set writing bytes.
	// Note: The I2C format of tuner register byte setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + addr + data + stop_bit
	WritingBytes[0] = addr;
	WritingBytes[1] = data;

	// Set tuner register bytes with writing buffer.
	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, WritingBytes, LEN_2_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return FC2580_FCI_SUCCESS;


error_status_set_tuner_registers:
	return FC2580_FCI_FAIL;
};

/*==============================================================================

           fc2580 i2c read

  This function is a generic function which gets called to read data from
 fc2580's target memory address.

  <input parameter>

  addr
	fc2580's memory address
	type : byte


  <return value>
  data
	a byte of data read out of target address 'addr'
	type : byte

==============================================================================*/
fc2580_fci_result_type fc2580_i2c_read( TUNER_MODULE *pTuner, unsigned char addr, unsigned char *read_data )
{
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;


	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Set tuner register reading address.
	// Note: The I2C format of tuner register reading address setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + addr + stop_bit
	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, &addr, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_register_reading_address;

	// Get tuner register byte.
	// Note: The I2C format of tuner register byte getting is as follows:
	//       start_bit + (DeviceAddr | reading_bit) + read_data + stop_bit
	if(pI2cBridge->ForwardI2cReadingCmd(pI2cBridge, DeviceAddr, read_data, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;


	return FC2580_FCI_SUCCESS;


error_status_get_tuner_registers:
error_status_set_tuner_register_reading_address:
	return FC2580_FCI_FAIL;
};



/*==============================================================================
       fc2580 I2C Test

  This function is a generic function which tests I2C interface's availability

  by reading out it's I2C id data from reg. address '0x01'.

  <input parameter>

  None
  
  <return value>
  int
	1 : success - communication is avilable
	0 : fail - communication is unavailable
  

==============================================================================*/
//int fc2580_i2c_test( void )
//{
//	return ( fc2580_i2c_read( 0x01 ) == 0x56 )? 0x01 : 0x00;
//}




/*==============================================================================
       fc2580 initial setting

  This function is a generic function which gets called to initialize

  fc2580 in DVB-H mode or L-Band TDMB mode

  <input parameter>

  ifagc_mode
    type : integer
	1 : Internal AGC
	2 : Voltage Control Mode

==============================================================================*/
fc2580_fci_result_type fc2580_set_init( TUNER_MODULE *pTuner, int ifagc_mode, unsigned int freq_xtal )
{
	fc2580_fci_result_type result = FC2580_FCI_SUCCESS;

	result &= fc2580_i2c_write(pTuner, 0x00, 0x00);	/*** Confidential ***/
	result &= fc2580_i2c_write(pTuner, 0x12, 0x86);
	result &= fc2580_i2c_write(pTuner, 0x14, 0x5C);
	result &= fc2580_i2c_write(pTuner, 0x16, 0x3C);
	result &= fc2580_i2c_write(pTuner, 0x1F, 0xD2);
	result &= fc2580_i2c_write(pTuner, 0x09, 0xD7);
	result &= fc2580_i2c_write(pTuner, 0x0B, 0xD5);
	result &= fc2580_i2c_write(pTuner, 0x0C, 0x32);
	result &= fc2580_i2c_write(pTuner, 0x0E, 0x43);
	result &= fc2580_i2c_write(pTuner, 0x21, 0x0A);
	result &= fc2580_i2c_write(pTuner, 0x22, 0x82);
	if( ifagc_mode == 1 )
	{
		result &= fc2580_i2c_write(pTuner, 0x45, 0x10);	//internal AGC
		result &= fc2580_i2c_write(pTuner, 0x4C, 0x00);	//HOLD_AGC polarity
	}
	else if( ifagc_mode == 2 )
	{
		result &= fc2580_i2c_write(pTuner, 0x45, 0x20);	//Voltage Control Mode
		result &= fc2580_i2c_write(pTuner, 0x4C, 0x02);	//HOLD_AGC polarity
	}
	result &= fc2580_i2c_write(pTuner, 0x3F, 0x88);
	result &= fc2580_i2c_write(pTuner, 0x02, 0x0E);
	result &= fc2580_i2c_write(pTuner, 0x58, 0x14);
	result &= fc2580_set_filter(pTuner, 8, freq_xtal);	//BW = 7.8MHz

	return result;
}


/*==============================================================================
       fc2580 frequency setting

  This function is a generic function which gets called to change LO Frequency

  of fc2580 in DVB-H mode or L-Band TDMB mode

  <input parameter>
  freq_xtal: kHz

  f_lo
	Value of target LO Frequency in 'kHz' unit
	ex) 2.6GHz = 2600000

==============================================================================*/
fc2580_fci_result_type fc2580_set_freq( TUNER_MODULE *pTuner, unsigned int f_lo, unsigned int freq_xtal )
{
	unsigned int f_diff, f_diff_shifted, n_val, k_val;
	unsigned int f_vco, r_val, f_comp;
	unsigned char pre_shift_bits = 4;// number of preshift to prevent overflow in shifting f_diff to f_diff_shifted
	unsigned char data_0x18;
	unsigned char data_0x02 = (USE_EXT_CLK<<5)|0x0E;
	
	fc2580_band_type band = ( f_lo > 1000000 )? FC2580_L_BAND : ( f_lo > 400000 )? FC2580_UHF_BAND : FC2580_VHF_BAND;

	fc2580_fci_result_type result = FC2580_FCI_SUCCESS;

	f_vco = ( band == FC2580_UHF_BAND )? f_lo * 4 : (( band == FC2580_L_BAND )? f_lo * 2 : f_lo * 12);
	r_val = ( f_vco >= 2*76*freq_xtal )? 1 : ( f_vco >= 76*freq_xtal )? 2 : 4;
	f_comp = freq_xtal/r_val;
	n_val =	( f_vco / 2 ) / f_comp;
	
	f_diff = f_vco - 2* f_comp * n_val;
	f_diff_shifted = f_diff << ( 20 - pre_shift_bits );
	k_val = f_diff_shifted / ( ( 2* f_comp ) >> pre_shift_bits );
	
	if( f_diff_shifted - k_val * ( ( 2* f_comp ) >> pre_shift_bits ) >= ( f_comp >> pre_shift_bits ) )
	k_val = k_val + 1;
	
	if( f_vco >= BORDER_FREQ )	//Select VCO Band
		data_0x02 = data_0x02 | 0x08;	//0x02[3] = 1;
	else
		data_0x02 = data_0x02 & 0xF7;	//0x02[3] = 0;
	
//	if( band != curr_band ) {
		switch(band)
		{
			case FC2580_UHF_BAND:
				data_0x02 = (data_0x02 & 0x3F);

				result &= fc2580_i2c_write(pTuner, 0x25, 0xF0);
				result &= fc2580_i2c_write(pTuner, 0x27, 0x77);
				result &= fc2580_i2c_write(pTuner, 0x28, 0x53);
				result &= fc2580_i2c_write(pTuner, 0x29, 0x60);
				result &= fc2580_i2c_write(pTuner, 0x30, 0x09);
				result &= fc2580_i2c_write(pTuner, 0x50, 0x8C);
				result &= fc2580_i2c_write(pTuner, 0x53, 0x50);

				if( f_lo < 538000 )
					result &= fc2580_i2c_write(pTuner, 0x5F, 0x13);
				else					
					result &= fc2580_i2c_write(pTuner, 0x5F, 0x15);

				if( f_lo < 538000 )
				{
					result &= fc2580_i2c_write(pTuner, 0x61, 0x07);
					result &= fc2580_i2c_write(pTuner, 0x62, 0x06);
					result &= fc2580_i2c_write(pTuner, 0x67, 0x06);
					result &= fc2580_i2c_write(pTuner, 0x68, 0x08);
					result &= fc2580_i2c_write(pTuner, 0x69, 0x10);
					result &= fc2580_i2c_write(pTuner, 0x6A, 0x12);
				}
				else if( f_lo < 794000 )
				{
					result &= fc2580_i2c_write(pTuner, 0x61, 0x03);
					result &= fc2580_i2c_write(pTuner, 0x62, 0x03);
					result &= fc2580_i2c_write(pTuner, 0x67, 0x03);  //ACI improve
					result &= fc2580_i2c_write(pTuner, 0x68, 0x05);  //ACI improve
					result &= fc2580_i2c_write(pTuner, 0x69, 0x0C);
					result &= fc2580_i2c_write(pTuner, 0x6A, 0x0E);
				}
				else
				{
					result &= fc2580_i2c_write(pTuner, 0x61, 0x07);
					result &= fc2580_i2c_write(pTuner, 0x62, 0x06);
					result &= fc2580_i2c_write(pTuner, 0x67, 0x07);
					result &= fc2580_i2c_write(pTuner, 0x68, 0x09);
					result &= fc2580_i2c_write(pTuner, 0x69, 0x10);
					result &= fc2580_i2c_write(pTuner, 0x6A, 0x12);
				}

				result &= fc2580_i2c_write(pTuner, 0x63, 0x15);

				result &= fc2580_i2c_write(pTuner, 0x6B, 0x0B);
				result &= fc2580_i2c_write(pTuner, 0x6C, 0x0C);
				result &= fc2580_i2c_write(pTuner, 0x6D, 0x78);
				result &= fc2580_i2c_write(pTuner, 0x6E, 0x32);
				result &= fc2580_i2c_write(pTuner, 0x6F, 0x14);
				result &= fc2580_set_filter(pTuner, 8, freq_xtal);	//BW = 7.8MHz
				break;
			case FC2580_VHF_BAND:
				data_0x02 = (data_0x02 & 0x3F) | 0x80;
				result &= fc2580_i2c_write(pTuner, 0x27, 0x77);
				result &= fc2580_i2c_write(pTuner, 0x28, 0x33);
				result &= fc2580_i2c_write(pTuner, 0x29, 0x40);
				result &= fc2580_i2c_write(pTuner, 0x30, 0x09);
				result &= fc2580_i2c_write(pTuner, 0x50, 0x8C);
				result &= fc2580_i2c_write(pTuner, 0x53, 0x50);
				result &= fc2580_i2c_write(pTuner, 0x5F, 0x0F);
				result &= fc2580_i2c_write(pTuner, 0x61, 0x07);
				result &= fc2580_i2c_write(pTuner, 0x62, 0x00);
				result &= fc2580_i2c_write(pTuner, 0x63, 0x15);
				result &= fc2580_i2c_write(pTuner, 0x67, 0x03);
				result &= fc2580_i2c_write(pTuner, 0x68, 0x05);
				result &= fc2580_i2c_write(pTuner, 0x69, 0x10);
				result &= fc2580_i2c_write(pTuner, 0x6A, 0x12);
				result &= fc2580_i2c_write(pTuner, 0x6B, 0x08);
				result &= fc2580_i2c_write(pTuner, 0x6C, 0x0A);
				result &= fc2580_i2c_write(pTuner, 0x6D, 0x78);
				result &= fc2580_i2c_write(pTuner, 0x6E, 0x32);
				result &= fc2580_i2c_write(pTuner, 0x6F, 0x54);
				result &= fc2580_set_filter(pTuner, 7, freq_xtal);	//BW = 6.8MHz
				break;
			case FC2580_L_BAND:
				data_0x02 = (data_0x02 & 0x3F) | 0x40;
				result &= fc2580_i2c_write(pTuner, 0x2B, 0x70);
				result &= fc2580_i2c_write(pTuner, 0x2C, 0x37);
				result &= fc2580_i2c_write(pTuner, 0x2D, 0xE7);
				result &= fc2580_i2c_write(pTuner, 0x30, 0x09);
				result &= fc2580_i2c_write(pTuner, 0x44, 0x20);
				result &= fc2580_i2c_write(pTuner, 0x50, 0x8C);
				result &= fc2580_i2c_write(pTuner, 0x53, 0x50);
				result &= fc2580_i2c_write(pTuner, 0x5F, 0x0F);
				result &= fc2580_i2c_write(pTuner, 0x61, 0x0F);
				result &= fc2580_i2c_write(pTuner, 0x62, 0x00);
				result &= fc2580_i2c_write(pTuner, 0x63, 0x13);
				result &= fc2580_i2c_write(pTuner, 0x67, 0x00);
				result &= fc2580_i2c_write(pTuner, 0x68, 0x02);
				result &= fc2580_i2c_write(pTuner, 0x69, 0x0C);
				result &= fc2580_i2c_write(pTuner, 0x6A, 0x0E);
				result &= fc2580_i2c_write(pTuner, 0x6B, 0x08);
				result &= fc2580_i2c_write(pTuner, 0x6C, 0x0A);
				result &= fc2580_i2c_write(pTuner, 0x6D, 0xA0);
				result &= fc2580_i2c_write(pTuner, 0x6E, 0x50);
				result &= fc2580_i2c_write(pTuner, 0x6F, 0x14);
				result &= fc2580_set_filter(pTuner, 1, freq_xtal);	//BW = 1.53MHz
				break;
			default:
				break;
		}
//		curr_band = band;
//	}

	//A command about AGC clock's pre-divide ratio
	if( freq_xtal >= 28000 )
		result &= fc2580_i2c_write(pTuner, 0x4B, 0x22 );

	//Commands about VCO Band and PLL setting.
	result &= fc2580_i2c_write(pTuner, 0x02, data_0x02);
	data_0x18 = ( ( r_val == 1 )? 0x00 : ( ( r_val == 2 )? 0x10 : 0x20 ) ) + (unsigned char)(k_val >> 16);
	result &= fc2580_i2c_write(pTuner, 0x18, data_0x18);						//Load 'R' value and high part of 'K' values
	result &= fc2580_i2c_write(pTuner, 0x1A, (unsigned char)( k_val >> 8 ) );	//Load middle part of 'K' value
	result &= fc2580_i2c_write(pTuner, 0x1B, (unsigned char)( k_val ) );		//Load lower part of 'K' value
	result &= fc2580_i2c_write(pTuner, 0x1C, (unsigned char)( n_val ) );		//Load 'N' value

	//A command about UHF LNA Load Cap
	if( band == FC2580_UHF_BAND )
		result &= fc2580_i2c_write(pTuner, 0x2D, ( f_lo <= (unsigned int)794000 )? 0x9F : 0x8F );	//LNA_OUT_CAP
	

	return result;
}


/*==============================================================================
       fc2580 filter BW setting

  This function is a generic function which gets called to change Bandwidth

  frequency of fc2580's channel selection filter

  <input parameter>
  freq_xtal: kHz

  filter_bw
    1 : 1.53MHz(TDMB)
	6 : 6MHz   (Bandwidth 6MHz)
	7 : 6.8MHz (Bandwidth 7MHz)
	8 : 7.8MHz (Bandwidth 8MHz)
	

==============================================================================*/
fc2580_fci_result_type fc2580_set_filter( TUNER_MODULE *pTuner, unsigned char filter_bw, unsigned int freq_xtal )
{
	unsigned char	cal_mon, i;
	fc2580_fci_result_type result = FC2580_FCI_SUCCESS;

	if(filter_bw == 1)
	{
		result &= fc2580_i2c_write(pTuner, 0x36, 0x1C);
		result &= fc2580_i2c_write(pTuner, 0x37, (unsigned char)(4151*freq_xtal/1000000) );
		result &= fc2580_i2c_write(pTuner, 0x39, 0x00);
		result &= fc2580_i2c_write(pTuner, 0x2E, 0x09);
	}
	if(filter_bw == 6)
	{
		result &= fc2580_i2c_write(pTuner, 0x36, 0x18);
		result &= fc2580_i2c_write(pTuner, 0x37, (unsigned char)(4400*freq_xtal/1000000) );
		result &= fc2580_i2c_write(pTuner, 0x39, 0x00);
		result &= fc2580_i2c_write(pTuner, 0x2E, 0x09);
	}
	else if(filter_bw == 7)
	{
		result &= fc2580_i2c_write(pTuner, 0x36, 0x18);
		result &= fc2580_i2c_write(pTuner, 0x37, (unsigned char)(3910*freq_xtal/1000000) );
		result &= fc2580_i2c_write(pTuner, 0x39, 0x80);
		result &= fc2580_i2c_write(pTuner, 0x2E, 0x09);
	}
	else if(filter_bw == 8)
	{
		result &= fc2580_i2c_write(pTuner, 0x36, 0x18);
		result &= fc2580_i2c_write(pTuner, 0x37, (unsigned char)(3300*freq_xtal/1000000) );
		result &= fc2580_i2c_write(pTuner, 0x39, 0x80);
		result &= fc2580_i2c_write(pTuner, 0x2E, 0x09);
	}

	
	for(i=0; i<5; i++)
	{
		fc2580_wait_msec(pTuner, 5);//wait 5ms
		result &= fc2580_i2c_read(pTuner, 0x2F, &cal_mon);
		if( (cal_mon & 0xC0) != 0xC0)
		{
			result &= fc2580_i2c_write(pTuner, 0x2E, 0x01);
			result &= fc2580_i2c_write(pTuner, 0x2E, 0x09);
		}
		else
			break;
	}

	result &= fc2580_i2c_write(pTuner, 0x2E, 0x01);

	return result;
}

/*==============================================================================
       fc2580 RSSI function

  This function is a generic function which returns fc2580's
  
  current RSSI value.

  <input parameter>
	none

  <return value>
  int
  	rssi : estimated input power.

==============================================================================*/
//int fc2580_get_rssi(void) {
//	
//	unsigned char s_lna, s_rfvga, s_cfs, s_ifvga;
//	int ofs_lna, ofs_rfvga, ofs_csf, ofs_ifvga, rssi;
//
//	fc2580_i2c_read(0x71, &s_lna );
//	fc2580_i2c_read(0x72, &s_rfvga );
//	fc2580_i2c_read(0x73, &s_cfs );
//	fc2580_i2c_read(0x74, &s_ifvga );
//	
//
//	ofs_lna = 
//			(curr_band==FC2580_UHF_BAND)?
//				(s_lna==0)? 0 :
//				(s_lna==1)? -6 :
//				(s_lna==2)? -17 :
//				(s_lna==3)? -22 : -30 :
//			(curr_band==FC2580_VHF_BAND)?
//				(s_lna==0)? 0 :
//				(s_lna==1)? -6 :
//				(s_lna==2)? -19 :
//				(s_lna==3)? -24 : -32 :
//			(curr_band==FC2580_L_BAND)?
//				(s_lna==0)? 0 :
//				(s_lna==1)? -6 :
//				(s_lna==2)? -11 :
//				(s_lna==3)? -16 : -34 :
//			0;//FC2580_NO_BAND
//	ofs_rfvga = -s_rfvga+((s_rfvga>=11)? 1 : 0) + ((s_rfvga>=18)? 1 : 0);
//	ofs_csf = -6*s_cfs;
//	ofs_ifvga = s_ifvga/4;
//
//	return rssi = ofs_lna+ofs_rfvga+ofs_csf+ofs_ifvga+OFS_RSSI;
//				
//}

/*==============================================================================
       fc2580 Xtal frequency Setting

  This function is a generic function which sets 
  
  the frequency of xtal.
  
  <input parameter>
  
  frequency
  	frequency value of internal(external) Xtal(clock) in kHz unit.

==============================================================================*/
//void fc2580_set_freq_xtal(unsigned int frequency) {
//
//	freq_xtal = frequency;
//
//}











