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

/* Card Specific information */
#define APCI035_BOARD_VENDOR_ID		0x15B8
#define APCI035_ADDRESS_RANGE		255

int i_TW_Number;
struct {
	int i_Gain;
	int i_Polarity;
	int i_OffsetRange;
	int i_Coupling;
	int i_SingleDiff;
	int i_AutoCalibration;
	UINT ui_ReloadValue;
	UINT ui_TimeUnitReloadVal;
	int i_Interrupt;
	int i_ModuleSelection;
} Config_Parameters_Main;

/* ANALOG INPUT RANGE */
struct comedi_lrange range_apci035_ai = { 8, {
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

/* Timer / Watchdog Related Defines */
#define APCI035_TCW_SYNC_ENABLEDISABLE	0
#define APCI035_TCW_RELOAD_VALUE	4
#define APCI035_TCW_TIMEBASE		8
#define APCI035_TCW_PROG		12
#define APCI035_TCW_TRIG_STATUS		16
#define APCI035_TCW_IRQ			20
#define APCI035_TCW_WARN_TIMEVAL	24
#define APCI035_TCW_WARN_TIMEBASE	28

#define ADDIDATA_TIMER			0
/* #define ADDIDATA_WATCHDOG		1 */

#define APCI035_TW1                               0
#define APCI035_TW2                               32
#define APCI035_TW3                               64
#define APCI035_TW4                               96

#define APCI035_AI_OFFSET                        0
#define APCI035_TEMP                             128
#define APCI035_ALR_SEQ                          4
#define APCI035_START_STOP_INDEX                 8
#define APCI035_ALR_START_STOP                   12
#define APCI035_ALR_IRQ                          16
#define APCI035_EOS                              20
#define APCI035_CHAN_NO                          24
#define APCI035_CHAN_VAL                         28
#define APCI035_CONV_TIME_TIME_BASE	36
#define APCI035_RELOAD_CONV_TIME_VAL	32
#define APCI035_DELAY_TIME_TIME_BASE	44
#define APCI035_RELOAD_DELAY_TIME_VAL	40
#define ENABLE_EXT_TRIG			1
#define ENABLE_EXT_GATE			2
#define ENABLE_EXT_TRIG_GATE		3

#define ANALOG_INPUT			0
#define TEMPERATURE			1
#define RESISTANCE			2

#define ADDIDATA_GREATER_THAN_TEST	0
#define ADDIDATA_LESS_THAN_TEST		1

#define APCI035_MAXVOLT                         2.5

#define ADDIDATA_UNIPOLAR                        1
#define ADDIDATA_BIPOLAR                         2

/* ADDIDATA Enable Disable */
#define ADDIDATA_ENABLE				1
#define ADDIDATA_DISABLE			0

/* Hardware Layer functions for Apci035 */

/* TIMER */
/* timer value is passed as u seconds */
int i_APCI035_ConfigTimerWatchdog(struct comedi_device *dev, struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data);
int i_APCI035_StartStopWriteTimerWatchdog(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn, unsigned int *data);
int i_APCI035_ReadTimerWatchdog(struct comedi_device *dev, struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data);

/* Temperature Related Defines (Analog Input Subdevice) */

int i_APCI035_ConfigAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data);
int i_APCI035_ReadAnalogInput(struct comedi_device *dev, struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);

/* Interrupt */
static void v_APCI035_Interrupt(int irq, void *d);

/* Reset functions */
int i_APCI035_Reset(struct comedi_device *dev);
