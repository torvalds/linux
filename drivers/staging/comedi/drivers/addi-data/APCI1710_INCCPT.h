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

#define APCI1710_16BIT_COUNTER			0x10
#define APCI1710_32BIT_COUNTER			0x0
#define APCI1710_QUADRUPLE_MODE			0x0
#define APCI1710_DOUBLE_MODE			0x3
#define APCI1710_SIMPLE_MODE			0xF
#define APCI1710_DIRECT_MODE			0x80
#define APCI1710_HYSTERESIS_ON			0x60
#define APCI1710_HYSTERESIS_OFF			0x0
#define APCI1710_INCREMENT			0x60
#define APCI1710_DECREMENT			0x0
#define APCI1710_LATCH_COUNTER			0x1
#define APCI1710_CLEAR_COUNTER			0x0
#define APCI1710_LOW				0x0
#define APCI1710_HIGH				0x1

/*********************/
/* Version 0600-0229 */
/*********************/
#define APCI1710_HIGH_EDGE_CLEAR_COUNTER		0x0
#define APCI1710_HIGH_EDGE_LATCH_COUNTER		0x1
#define APCI1710_LOW_EDGE_CLEAR_COUNTER			0x2
#define APCI1710_LOW_EDGE_LATCH_COUNTER			0x3
#define APCI1710_HIGH_EDGE_LATCH_AND_CLEAR_COUNTER	0x4
#define APCI1710_LOW_EDGE_LATCH_AND_CLEAR_COUNTER	0x5
#define APCI1710_SOURCE_0				0x0
#define APCI1710_SOURCE_1				0x1

#define APCI1710_30MHZ				30
#define APCI1710_33MHZ				33
#define APCI1710_40MHZ				40

#define APCI1710_ENABLE_LATCH_INT    		0x80
#define APCI1710_DISABLE_LATCH_INT   		(~APCI1710_ENABLE_LATCH_INT)

#define APCI1710_INDEX_LATCH_COUNTER		0x10
#define APCI1710_INDEX_AUTO_MODE		0x8
#define APCI1710_ENABLE_INDEX			0x4
#define APCI1710_DISABLE_INDEX			(~APCI1710_ENABLE_INDEX)
#define APCI1710_ENABLE_LATCH_AND_CLEAR		0x8
#define APCI1710_DISABLE_LATCH_AND_CLEAR	(~APCI1710_ENABLE_LATCH_AND_CLEAR)
#define APCI1710_SET_LOW_INDEX_LEVEL		0x4
#define APCI1710_SET_HIGH_INDEX_LEVEL		(~APCI1710_SET_LOW_INDEX_LEVEL)
#define APCI1710_INVERT_INDEX_RFERENCE		0x2
#define APCI1710_DEFAULT_INDEX_RFERENCE         (~APCI1710_INVERT_INDEX_RFERENCE)

#define APCI1710_ENABLE_INDEX_INT		0x1
#define APCI1710_DISABLE_INDEX_INT		(~APCI1710_ENABLE_INDEX_INT)

#define APCI1710_ENABLE_FREQUENCY		0x4
#define APCI1710_DISABLE_FREQUENCY		(~APCI1710_ENABLE_FREQUENCY)

#define APCI1710_ENABLE_FREQUENCY_INT		0x8
#define APCI1710_DISABLE_FREQUENCY_INT		(~APCI1710_ENABLE_FREQUENCY_INT)

#define APCI1710_ENABLE_40MHZ_FREQUENCY		0x40
#define APCI1710_DISABLE_40MHZ_FREQUENCY	(~APCI1710_ENABLE_40MHZ_FREQUENCY)

#define APCI1710_ENABLE_40MHZ_FILTER		0x80
#define APCI1710_DISABLE_40MHZ_FILTER		(~APCI1710_ENABLE_40MHZ_FILTER)

#define APCI1710_ENABLE_COMPARE_INT		0x2
#define APCI1710_DISABLE_COMPARE_INT		(~APCI1710_ENABLE_COMPARE_INT)

#define APCI1710_ENABLE_INDEX_ACTION		0x20
#define APCI1710_DISABLE_INDEX_ACTION		(~APCI1710_ENABLE_INDEX_ACTION)
#define APCI1710_REFERENCE_HIGH			0x40
#define APCI1710_REFERENCE_LOW			(~APCI1710_REFERENCE_HIGH)

#define APCI1710_TOR_GATE_LOW			0x40
#define APCI1710_TOR_GATE_HIGH			(~APCI1710_TOR_GATE_LOW)

/* INSN CONFIG */
#define	APCI1710_INCCPT_INITCOUNTER				100
#define APCI1710_INCCPT_COUNTERAUTOTEST				101
#define APCI1710_INCCPT_INITINDEX				102
#define APCI1710_INCCPT_INITREFERENCE				103
#define APCI1710_INCCPT_INITEXTERNALSTROBE			104
#define APCI1710_INCCPT_INITCOMPARELOGIC			105
#define APCI1710_INCCPT_INITFREQUENCYMEASUREMENT		106

/* INSN READ */
#define APCI1710_INCCPT_READLATCHREGISTERSTATUS			200
#define APCI1710_INCCPT_READLATCHREGISTERVALUE			201
#define APCI1710_INCCPT_READ16BITCOUNTERVALUE			202
#define APCI1710_INCCPT_READ32BITCOUNTERVALUE			203
#define APCI1710_INCCPT_GETINDEXSTATUS				204
#define APCI1710_INCCPT_GETREFERENCESTATUS			205
#define APCI1710_INCCPT_GETUASSTATUS				206
#define APCI1710_INCCPT_GETCBSTATUS				207
#define APCI1710_INCCPT_GET16BITCBSTATUS			208
#define APCI1710_INCCPT_GETUDSTATUS				209
#define APCI1710_INCCPT_GETINTERRUPTUDLATCHEDSTATUS		210
#define APCI1710_INCCPT_READFREQUENCYMEASUREMENT		211
#define APCI1710_INCCPT_READINTERRUPT				212

/* INSN BITS */
#define APCI1710_INCCPT_CLEARCOUNTERVALUE			300
#define APCI1710_INCCPT_CLEARALLCOUNTERVALUE			301
#define APCI1710_INCCPT_SETINPUTFILTER				302
#define APCI1710_INCCPT_LATCHCOUNTER				303
#define APCI1710_INCCPT_SETINDEXANDREFERENCESOURCE		304
#define APCI1710_INCCPT_SETDIGITALCHLON				305
#define APCI1710_INCCPT_SETDIGITALCHLOFF			306

/* INSN WRITE */
#define APCI1710_INCCPT_ENABLELATCHINTERRUPT			400
#define APCI1710_INCCPT_DISABLELATCHINTERRUPT			401
#define APCI1710_INCCPT_WRITE16BITCOUNTERVALUE			402
#define APCI1710_INCCPT_WRITE32BITCOUNTERVALUE			403
#define APCI1710_INCCPT_ENABLEINDEX				404
#define APCI1710_INCCPT_DISABLEINDEX				405
#define APCI1710_INCCPT_ENABLECOMPARELOGIC			406
#define APCI1710_INCCPT_DISABLECOMPARELOGIC			407
#define APCI1710_INCCPT_ENABLEFREQUENCYMEASUREMENT		408
#define APCI1710_INCCPT_DISABLEFREQUENCYMEASUREMENT		409

/************ Main Functions *************/
int i_APCI1710_InsnConfigINCCPT(struct comedi_device *dev, struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int * data);

int i_APCI1710_InsnBitsINCCPT(struct comedi_device *dev, struct comedi_subdevice * s,
			      struct comedi_insn *insn, unsigned int * data);

int i_APCI1710_InsnWriteINCCPT(struct comedi_device *dev, struct comedi_subdevice * s,
			       struct comedi_insn *insn, unsigned int * data);

int i_APCI1710_InsnReadINCCPT(struct comedi_device *dev, struct comedi_subdevice * s,
			      struct comedi_insn *insn, unsigned int * data);

/*********** Supplementary Functions********/

/* INSN CONFIG */
int i_APCI1710_InitCounter(struct comedi_device *dev,
			   unsigned char b_ModulNbr,
			   unsigned char b_CounterRange,
			   unsigned char b_FirstCounterModus,
			   unsigned char b_FirstCounterOption,
			   unsigned char b_SecondCounterModus,
			   unsigned char b_SecondCounterOption);

int i_APCI1710_CounterAutoTest(struct comedi_device *dev, unsigned char * pb_TestStatus);

int i_APCI1710_InitIndex(struct comedi_device *dev,
			 unsigned char b_ModulNbr,
			 unsigned char b_ReferenceAction,
			 unsigned char b_IndexOperation, unsigned char b_AutoMode,
			 unsigned char b_InterruptEnable);

int i_APCI1710_InitReference(struct comedi_device *dev,
			     unsigned char b_ModulNbr, unsigned char b_ReferenceLevel);

int i_APCI1710_InitExternalStrobe(struct comedi_device *dev,
				  unsigned char b_ModulNbr, unsigned char b_ExternalStrobe,
				  unsigned char b_ExternalStrobeLevel);

int i_APCI1710_InitCompareLogic(struct comedi_device *dev,
				unsigned char b_ModulNbr, unsigned int ui_CompareValue);

int i_APCI1710_InitFrequencyMeasurement(struct comedi_device *dev,
					unsigned char b_ModulNbr,
					unsigned char b_PCIInputClock,
					unsigned char b_TimingUnity,
					unsigned int ul_TimingInterval,
					unsigned int *pul_RealTimingInterval);

/* INSN BITS */
int i_APCI1710_ClearCounterValue(struct comedi_device *dev, unsigned char b_ModulNbr);

int i_APCI1710_ClearAllCounterValue(struct comedi_device *dev);

int i_APCI1710_SetInputFilter(struct comedi_device *dev,
			      unsigned char b_ModulNbr, unsigned char b_PCIInputClock,
			      unsigned char b_Filter);

int i_APCI1710_LatchCounter(struct comedi_device *dev,
			    unsigned char b_ModulNbr, unsigned char b_LatchReg);

int i_APCI1710_SetIndexAndReferenceSource(struct comedi_device *dev,
					  unsigned char b_ModulNbr,
					  unsigned char b_SourceSelection);

int i_APCI1710_SetDigitalChlOn(struct comedi_device *dev, unsigned char b_ModulNbr);

int i_APCI1710_SetDigitalChlOff(struct comedi_device *dev, unsigned char b_ModulNbr);

/* INSN WRITE */
int i_APCI1710_EnableLatchInterrupt(struct comedi_device *dev, unsigned char b_ModulNbr);

int i_APCI1710_DisableLatchInterrupt(struct comedi_device *dev, unsigned char b_ModulNbr);

int i_APCI1710_Write16BitCounterValue(struct comedi_device *dev,
				      unsigned char b_ModulNbr, unsigned char b_SelectedCounter,
				      unsigned int ui_WriteValue);

int i_APCI1710_Write32BitCounterValue(struct comedi_device *dev,
				      unsigned char b_ModulNbr, unsigned int ul_WriteValue);

int i_APCI1710_EnableIndex(struct comedi_device *dev, unsigned char b_ModulNbr);

int i_APCI1710_DisableIndex(struct comedi_device *dev, unsigned char b_ModulNbr);

int i_APCI1710_EnableCompareLogic(struct comedi_device *dev, unsigned char b_ModulNbr);

int i_APCI1710_DisableCompareLogic(struct comedi_device *dev, unsigned char b_ModulNbr);

int i_APCI1710_EnableFrequencyMeasurement(struct comedi_device *dev,
					  unsigned char b_ModulNbr,
					  unsigned char b_InterruptEnable);

int i_APCI1710_DisableFrequencyMeasurement(struct comedi_device *dev,
					   unsigned char b_ModulNbr);

/* INSN READ */
int i_APCI1710_ReadLatchRegisterStatus(struct comedi_device *dev,
				       unsigned char b_ModulNbr, unsigned char b_LatchReg,
				       unsigned char *pb_LatchStatus);

int i_APCI1710_ReadLatchRegisterValue(struct comedi_device *dev,
				      unsigned char b_ModulNbr, unsigned char b_LatchReg,
				      unsigned int *pul_LatchValue);

int i_APCI1710_Read16BitCounterValue(struct comedi_device *dev,
				     unsigned char b_ModulNbr, unsigned char b_SelectedCounter,
				     unsigned int *pui_CounterValue);

int i_APCI1710_Read32BitCounterValue(struct comedi_device *dev,
				     unsigned char b_ModulNbr, unsigned int *pul_CounterValue);

int i_APCI1710_GetIndexStatus(struct comedi_device *dev,
			      unsigned char b_ModulNbr, unsigned char *pb_IndexStatus);

int i_APCI1710_GetReferenceStatus(struct comedi_device *dev,
				  unsigned char b_ModulNbr, unsigned char *pb_ReferenceStatus);

int i_APCI1710_GetUASStatus(struct comedi_device *dev,
			    unsigned char b_ModulNbr, unsigned char *pb_UASStatus);

int i_APCI1710_GetCBStatus(struct comedi_device *dev,
			   unsigned char b_ModulNbr, unsigned char *pb_CBStatus);

int i_APCI1710_Get16BitCBStatus(struct comedi_device *dev,
				unsigned char b_ModulNbr, unsigned char *pb_CBStatusCounter0,
				unsigned char *pb_CBStatusCounter1);

int i_APCI1710_GetUDStatus(struct comedi_device *dev,
			   unsigned char b_ModulNbr, unsigned char *pb_UDStatus);

int i_APCI1710_GetInterruptUDLatchedStatus(struct comedi_device *dev,
					   unsigned char b_ModulNbr, unsigned char *pb_UDStatus);

int i_APCI1710_ReadFrequencyMeasurement(struct comedi_device *dev,
					unsigned char b_ModulNbr,
					unsigned char *pb_Status, unsigned char *pb_UDStatus,
					unsigned int *pul_ReadValue);
