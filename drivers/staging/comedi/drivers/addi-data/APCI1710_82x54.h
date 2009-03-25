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

#define APCI1710_PCI_BUS_CLOCK 			0
#define APCI1710_FRONT_CONNECTOR_INPUT 		1
#define APCI1710_TIMER_READVALUE		0
#define APCI1710_TIMER_GETOUTPUTLEVEL		1
#define APCI1710_TIMER_GETPROGRESSSTATUS	2
#define APCI1710_TIMER_WRITEVALUE		3

#define APCI1710_TIMER_READINTERRUPT		1
#define APCI1710_TIMER_READALLTIMER		2

/* BEGIN JK 27.10.03 : Add the possibility to use a 40 Mhz quartz */
#ifndef APCI1710_10MHZ
#define APCI1710_10MHZ	10
#endif
/* END JK 27.10.03 : Add the possibility to use a 40 Mhz quartz */

/*
 * 82X54 TIMER INISIALISATION FUNCTION
 */
INT i_APCI1710_InsnConfigInitTimer(struct comedi_device *dev, struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data);

INT i_APCI1710_InsnWriteEnableDisableTimer(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   struct comedi_insn *insn, unsigned int *data);

/*
 * 82X54 READ FUNCTION
 */
INT i_APCI1710_InsnReadAllTimerValue(struct comedi_device *dev, struct comedi_subdevice *s,
				     struct comedi_insn *insn, unsigned int *data);

INT i_APCI1710_InsnBitsTimer(struct comedi_device *dev, struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data);

/*
 * 82X54 READ & WRITE FUNCTION
 */
INT i_APCI1710_ReadTimerValue(struct comedi_device *dev,
			      unsigned char b_ModulNbr, unsigned char b_TimerNbr,
			      PULONG pul_TimerValue);

INT i_APCI1710_GetTimerOutputLevel(struct comedi_device *dev,
				   unsigned char b_ModulNbr, unsigned char b_TimerNbr,
				   unsigned char * pb_OutputLevel);

INT i_APCI1710_GetTimerProgressStatus(struct comedi_device *dev,
				      unsigned char b_ModulNbr, unsigned char b_TimerNbr,
				      unsigned char * pb_TimerStatus);

/*
 * 82X54 WRITE FUNCTION
 */
INT i_APCI1710_WriteTimerValue(struct comedi_device *dev,
			       unsigned char b_ModulNbr, unsigned char b_TimerNbr,
			       ULONG ul_WriteValue);
