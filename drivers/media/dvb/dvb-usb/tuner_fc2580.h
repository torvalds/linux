#ifndef __TUNER_FC2580_H
#define __TUNER_FC2580_H

/**

@file

@brief   FC2580 tuner module declaration

One can manipulate FC2580 tuner through FC2580 module.
FC2580 module is derived from tuner module.



@par Example:
@code

// The example is the same as the tuner example in tuner_base.h except the listed lines.



#include "tuner_fc2580.h"


...



int main(void)
{
	TUNER_MODULE        *pTuner;
	FC2580_EXTRA_MODULE *pTunerExtra;

	TUNER_MODULE          TunerModuleMemory;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;
	I2C_BRIDGE_MODULE     I2cBridgeModuleMemory;

	unsigned long BandwidthMode;


	...



	// Build FC2580 tuner module.
	BuildFc2580Module(
		&pTuner,
		&TunerModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0xac,								// I2C device address is 0xac in 8-bit format.
		CRYSTAL_FREQ_16384000HZ,			// Crystal frequency is 16.384 MHz.
		FC2580_AGC_INTERNAL					// The FC2580 AGC mode is internal AGC mode.
		);





	// Get FC2580 tuner extra module.
	pTunerExtra = (T2266_EXTRA_MODULE *)(pTuner->pExtra);





	// ==== Initialize tuner and set its parameters =====

	...

	// Set FC2580 bandwidth.
	pTunerExtra->SetBandwidthMode(pTuner, FC2580_BANDWIDTH_6MHZ);





	// ==== Get tuner information =====

	...

	// Get FC2580 bandwidth.
	pTunerExtra->GetBandwidthMode(pTuner, &BandwidthMode);



	// See the example for other tuner functions in tuner_base.h


	return 0;
}


@endcode

*/





#include "tuner_base.h"





// The following context is source code provided by FCI.





// FCI source code - fc2580_driver_v1400_r.h


/*==============================================================================
    FILE NAME   : FC2580_driver_v1400_r.h  
    
    VERSION     : 1.400_r
    
    UPDATE      : September 22. 2008
				
==============================================================================*/ 

/*==============================================================================

  Chip ID of FC2580 is 0x56 or 0xAC(including I2C write bit)

==============================================================================*/


#define	BORDER_FREQ	2600000	//2.6GHz : The border frequency which determines whether Low VCO or High VCO is used
#define USE_EXT_CLK	0	//0 : Use internal XTAL Oscillator / 1 : Use External Clock input
#define OFS_RSSI 57

typedef enum {
	FC2580_UHF_BAND,
	FC2580_L_BAND,
	FC2580_VHF_BAND,
	FC2580_NO_BAND
} fc2580_band_type;

typedef enum {
	FC2580_FCI_FAIL,
	FC2580_FCI_SUCCESS
} fc2580_fci_result_type;

/*==============================================================================
		i2c command write							EXTERNAL FUNCTION

  This function is a generic function which write a byte into fc2580's
 specific address.

  <input parameter>

  slave_id
	i2c id of slave chip
	type : byte
	
  addr
	memory address of slave chip
	type : byte

  data
	target data
	type : byte

==============================================================================*/
//extern fc2580_fci_result_type i2c_write( unsigned char slave_id, unsigned char addr, unsigned char *data, unsigned char n );

/*==============================================================================
		i2c command write							EXTERNAL FUNCTION

  This function is a generic function which gets called to read data from
 slave chip's target memory address.

  <input parameter>

  slave_id
	i2c id of slave chip
	type : byte
	
  addr
	memory address of slave chip
	type : byte

  <return value>
  data
	a byte of data read out of target address 'addr' of slave chip
	type : byte

==============================================================================*/
//extern fc2580_fci_result_type i2c_read( unsigned char slave_id, unsigned char addr, unsigned char *read_data, unsigned char n );

/*==============================================================================
		milisecond delay function					EXTERNAL FUNCTION

  This function is a generic function which write a byte into fc2580's
 specific address.

  <input parameter>

  a
	length of wanted delay in milisecond unit

==============================================================================*/
extern void fc2580_wait_msec(TUNER_MODULE *pTuner, int a);



/*==============================================================================
       fc2580 i2c command write

  This function is a generic function which write a byte into fc2580's
 specific address.

  <input parameter>

  addr
	fc2580's memory address
	type : byte

  data
	target data
	type : byte

==============================================================================*/
fc2580_fci_result_type fc2580_i2c_write( TUNER_MODULE *pTuner, unsigned char addr, unsigned char data );

/*==============================================================================
       fc2580 i2c data read

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
fc2580_fci_result_type fc2580_i2c_read( TUNER_MODULE *pTuner, unsigned char addr, unsigned char *read_data );

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
fc2580_fci_result_type fc2580_set_init( TUNER_MODULE *pTuner, int ifagc_mode, unsigned int freq_xtal );

/*==============================================================================
       fc2580 frequency setting

  This function is a generic function which gets called to change LO Frequency

  of fc2580 in DVB-H mode or L-Band TDMB mode

  <input parameter>

  f_lo
	Value of target LO Frequency in 'kHz' unit
	ex) 2.6GHz = 2600000

==============================================================================*/
fc2580_fci_result_type fc2580_set_freq( TUNER_MODULE *pTuner, unsigned int f_lo, unsigned int freq_xtal );


/*==============================================================================
       fc2580 filter BW setting

  This function is a generic function which gets called to change Bandwidth

  frequency of fc2580's channel selection filter

  <input parameter>

  filter_bw
    1 : 1.53MHz(TDMB)
	6 : 6MHz
	7 : 7MHz
	8 : 7.8MHz


==============================================================================*/
fc2580_fci_result_type fc2580_set_filter( TUNER_MODULE *pTuner, unsigned char filter_bw, unsigned int freq_xtal );

/*==============================================================================
       fc2580 RSSI function

  This function is a generic function which returns fc2580's
  
  current RSSI value.

  
  

==============================================================================*/
//int fc2580_get_rssi(void); 

/*==============================================================================
       fc2580 Xtal frequency Setting

  This function is a generic function which sets 
  
  the frequency of xtal.
  
  <input parameter>
  
  frequency
  	frequency value of internal(external) Xtal(clock) in kHz unit.

==============================================================================*/
//void fc2580_set_freq_xtal(unsigned int frequency);

























// The following context is FC2580 tuner API source code





// Definitions

// AGC mode
enum FC2580_AGC_MODE
{
	FC2580_AGC_INTERNAL = 1,
	FC2580_AGC_EXTERNAL = 2,
};


// Bandwidth mode
enum FC2580_BANDWIDTH_MODE
{
	FC2580_BANDWIDTH_1530000HZ = 1,
	FC2580_BANDWIDTH_6000000HZ = 6,
	FC2580_BANDWIDTH_7000000HZ = 7,
	FC2580_BANDWIDTH_8000000HZ = 8,
};





// Builder
void
BuildFc2580Module(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz,
	int AgcMode
	);





// Manipulaing functions
void
fc2580_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	);

void
fc2580_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	);

int
fc2580_Initialize(
	TUNER_MODULE *pTuner
	);

int
fc2580_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	);

int
fc2580_GetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	);





// Extra manipulaing functions
int
fc2580_SetBandwidthMode(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

int
fc2580_GetBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);













#endif
