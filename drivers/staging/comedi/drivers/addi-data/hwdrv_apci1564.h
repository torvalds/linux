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

/*********      Definitions for APCI-1564 card  *****/

#define APCI1564_BOARD_VENDOR_ID                0x15B8
#define APCI1564_ADDRESS_RANGE                  128

//DIGITAL INPUT-OUTPUT DEFINE
// Input defines
#define APCI1564_DIGITAL_IP                     0x04
#define APCI1564_DIGITAL_IP_INTERRUPT_MODE1     4
#define APCI1564_DIGITAL_IP_INTERRUPT_MODE2     8
#define APCI1564_DIGITAL_IP_IRQ                 16

// Output defines
#define APCI1564_DIGITAL_OP                 	0x18
#define APCI1564_DIGITAL_OP_RW               	0
#define APCI1564_DIGITAL_OP_INTERRUPT           4
#define APCI1564_DIGITAL_OP_IRQ                 12

//Digital Input IRQ Function Selection
#define ADDIDATA_OR                             0
#define ADDIDATA_AND                            1

//Digital Input Interrupt Status
#define APCI1564_DIGITAL_IP_INTERRUPT_STATUS    12

//Digital Output Interrupt Status
#define APCI1564_DIGITAL_OP_INTERRUPT_STATUS    8

//Digital Input Interrupt Enable Disable.
#define APCI1564_DIGITAL_IP_INTERRUPT_ENABLE    0x4
#define APCI1564_DIGITAL_IP_INTERRUPT_DISABLE   0xFFFFFFFB

//Digital Output Interrupt Enable Disable.
#define APCI1564_DIGITAL_OP_VCC_INTERRUPT_ENABLE   0x1
#define APCI1564_DIGITAL_OP_VCC_INTERRUPT_DISABLE  0xFFFFFFFE
#define APCI1564_DIGITAL_OP_CC_INTERRUPT_ENABLE    0x2
#define APCI1564_DIGITAL_OP_CC_INTERRUPT_DISABLE   0xFFFFFFFD

//ADDIDATA Enable Disable

#define ADDIDATA_ENABLE                            1
#define ADDIDATA_DISABLE                           0

// TIMER COUNTER WATCHDOG DEFINES

#define ADDIDATA_TIMER                             0
#define ADDIDATA_COUNTER                           1
#define ADDIDATA_WATCHDOG                          2
#define APCI1564_DIGITAL_OP_WATCHDOG               0x28
#define APCI1564_TIMER                             0x48
#define APCI1564_COUNTER1                          0x0
#define APCI1564_COUNTER2                          0x20
#define APCI1564_COUNTER3                          0x40
#define APCI1564_COUNTER4                          0x60
#define APCI1564_TCW_SYNC_ENABLEDISABLE            0
#define APCI1564_TCW_RELOAD_VALUE                  4
#define APCI1564_TCW_TIMEBASE                      8
#define APCI1564_TCW_PROG                          12
#define APCI1564_TCW_TRIG_STATUS                   16
#define APCI1564_TCW_IRQ                           20
#define APCI1564_TCW_WARN_TIMEVAL                  24
#define APCI1564_TCW_WARN_TIMEBASE                 28

// Hardware Layer  functions for Apci1564

//DI
// for di read
INT i_APCI1564_ConfigDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data);
INT i_APCI1564_Read1DigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);
INT i_APCI1564_ReadMoreDigitalInput(struct comedi_device *dev, struct comedi_subdevice *s,
				    struct comedi_insn *insn, unsigned int *data);

//DO
int i_APCI1564_ConfigDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data);
INT i_APCI1564_WriteDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data);
INT i_APCI1564_ReadDigitalOutput(struct comedi_device *dev, struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);
int i_APCI1564_ReadInterruptStatus(struct comedi_device *dev, struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data);

// TIMER
// timer value is passed as u seconds
INT i_APCI1564_ConfigTimerCounterWatchdog(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn, unsigned int *data);
int i_APCI1564_StartStopWriteTimerCounterWatchdog(struct comedi_device *dev,
						  struct comedi_subdevice *s,
						  struct comedi_insn *insn,
						  unsigned int *data);
int i_APCI1564_ReadTimerCounterWatchdog(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn, unsigned int *data);

// INTERRUPT
static void v_APCI1564_Interrupt(int irq, void *d);

// RESET
INT i_APCI1564_Reset(struct comedi_device *dev);
