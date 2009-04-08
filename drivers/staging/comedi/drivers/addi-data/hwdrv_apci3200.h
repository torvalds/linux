/*
 * Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data-com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

// Card Specific information
#define APCI3200_BOARD_VENDOR_ID                 0x15B8
//#define APCI3200_ADDRESS_RANGE                   264

int MODULE_NO;
struct {
	INT i_Gain;
	INT i_Polarity;
	INT i_OffsetRange;
	INT i_Coupling;
	INT i_SingleDiff;
	INT i_AutoCalibration;
	UINT ui_ReloadValue;
	UINT ui_TimeUnitReloadVal;
	INT i_Interrupt;
	INT i_ModuleSelection;
} Config_Parameters_Module1, Config_Parameters_Module2,
    Config_Parameters_Module3, Config_Parameters_Module4;

//ANALOG INPUT RANGE
static const struct comedi_lrange range_apci3200_ai = { 8, {
						     BIP_RANGE(10),
						     BIP_RANGE(5),
						     BIP_RANGE(2),
						     BIP_RANGE(1),
						     UNI_RANGE(10),
						     UNI_RANGE(5),
						     UNI_RANGE(2),
						     UNI_RANGE(1)
						     }
};

static const struct comedi_lrange range_apci3300_ai = { 4, {
						     UNI_RANGE(10),
						     UNI_RANGE(5),
						     UNI_RANGE(2),
						     UNI_RANGE(1)
						     }
};

//Analog Input related Defines
#define APCI3200_AI_OFFSET_GAIN                  0
#define APCI3200_AI_SC_TEST                      4
#define APCI3200_AI_IRQ                          8
#define APCI3200_AI_AUTOCAL                      12
#define APCI3200_RELOAD_CONV_TIME_VAL            32
#define APCI3200_CONV_TIME_TIME_BASE             36
#define APCI3200_RELOAD_DELAY_TIME_VAL           40
#define APCI3200_DELAY_TIME_TIME_BASE            44
#define APCI3200_AI_MODULE1                      0
#define APCI3200_AI_MODULE2                      64
#define APCI3200_AI_MODULE3                      128
#define APCI3200_AI_MODULE4                      192
#define TRUE                                     1
#define FALSE                                    0
#define APCI3200_AI_EOSIRQ                       16
#define APCI3200_AI_EOS                          20
#define APCI3200_AI_CHAN_ID                      24
#define APCI3200_AI_CHAN_VAL                     28
#define ANALOG_INPUT                             0
#define TEMPERATURE                              1
#define RESISTANCE                               2

#define ENABLE_EXT_TRIG                          1
#define ENABLE_EXT_GATE                          2
#define ENABLE_EXT_TRIG_GATE                     3

#define APCI3200_MAXVOLT                         2.5
#define ADDIDATA_GREATER_THAN_TEST               0
#define ADDIDATA_LESS_THAN_TEST                  1

#define ADDIDATA_UNIPOLAR                        1
#define ADDIDATA_BIPOLAR                         2

//BEGIN JK 21.10.2004: APCI-3200 / APCI-3300 Reading of EEPROM values
#define MAX_MODULE				4
//END JK 21.10.2004: APCI-3200 / APCI-3300 Reading of EEPROM values

typedef struct {
	ULONG ul_NumberOfValue;
	ULONG *pul_ResistanceValue;
	ULONG *pul_TemperatureValue;
} str_ADDIDATA_RTDStruct, *pstr_ADDIDATA_RTDStruct;

//BEGIN JK 21.10.2004: APCI-3200 / APCI-3300 Reading of EEPROM values
typedef struct {
	// Begin JK 05/08/2003 change for Linux
	unsigned long ul_CurrentSourceCJC;
	unsigned long ul_CurrentSource[5];
	// End JK 05/08/2003 change for Linux

	// Begin CG 15/02/02 Rev 1.0 -> Rev 1.1 : Add Header Type 1
	unsigned long ul_GainFactor[8];	// Gain Factor
	unsigned int w_GainValue[10];
	// End CG 15/02/02 Rev 1.0 -> Rev 1.1 : Add Header Type 1
} str_Module;
//END JK 21.10.2004: APCI-3200 / APCI-3300 Reading of EEPROM values

//BEGIN JK 06.07.04: Management of sevrals boards
typedef struct {
	INT i_CJCAvailable;
	INT i_CJCPolarity;
	INT i_CJCGain;
	INT i_InterruptFlag;
	INT i_ADDIDATAPolarity;
	INT i_ADDIDATAGain;
	INT i_AutoCalibration;
	INT i_ADDIDATAConversionTime;
	INT i_ADDIDATAConversionTimeUnit;
	INT i_ADDIDATAType;
	INT i_ChannelNo;
	INT i_ChannelCount;
	INT i_ScanType;
	INT i_FirstChannel;
	INT i_LastChannel;
	INT i_Sum;
	INT i_Offset;
	UINT ui_Channel_num;
	INT i_Count;
	INT i_Initialised;
	//UINT ui_InterruptChannelValue[96]; //Buffer
	UINT ui_InterruptChannelValue[144];	//Buffer
	BYTE b_StructInitialized;
	//Begin JK 19.10.2004: APCI-3200 Driver update 0.7.57 -> 0.7.68
	unsigned int ui_ScanValueArray[7 + 12];	// 7 is the maximal number of channels
	//End JK 19.10.2004: APCI-3200 Driver update 0.7.57 -> 0.7.68

	//Begin JK 21.10.2004: APCI-3200 / APCI-3300 Reading of EEPROM values
	INT i_ConnectionType;
	INT i_NbrOfModule;
	str_Module s_Module[MAX_MODULE];
	//End JK 21.10.2004: APCI-3200 / APCI-3300 Reading of EEPROM values
} str_BoardInfos;
//END JK 06.07.04: Management of sevrals boards

// Hardware Layer  functions for Apci3200

//AI

INT i_APCI3200_ConfigAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);
INT i_APCI3200_ReadAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data);
INT i_APCI3200_InsnWriteReleaseAnalogInput(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   struct comedi_insn *insn, unsigned int *data);
INT i_APCI3200_InsnBits_AnalogInput_Test(struct comedi_device *dev,
					 struct comedi_subdevice *s,
					 struct comedi_insn *insn, unsigned int *data);
INT i_APCI3200_StopCyclicAcquisition(struct comedi_device *dev, struct comedi_subdevice *s);
INT i_APCI3200_InterruptHandleEos(struct comedi_device *dev);
INT i_APCI3200_CommandTestAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
				      struct comedi_cmd *cmd);
INT i_APCI3200_CommandAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s);
INT i_APCI3200_ReadDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data);
//Interrupt
void v_APCI3200_Interrupt(int irq, void *d);
int i_APCI3200_InterruptHandleEos(struct comedi_device *dev);
//Reset functions
INT i_APCI3200_Reset(struct comedi_device *dev);

int i_APCI3200_ReadCJCCalOffset(struct comedi_device *dev, unsigned int *data);
int i_APCI3200_ReadCJCValue(struct comedi_device *dev, unsigned int *data);
int i_APCI3200_ReadCalibrationGainValue(struct comedi_device *dev, UINT *data);
int i_APCI3200_ReadCalibrationOffsetValue(struct comedi_device *dev, UINT *data);
int i_APCI3200_Read1AnalogInputChannel(struct comedi_device *dev,
				       struct comedi_subdevice *s, struct comedi_insn *insn,
				       unsigned int *data);
int i_APCI3200_ReadCJCCalGain(struct comedi_device *dev, unsigned int *data);
