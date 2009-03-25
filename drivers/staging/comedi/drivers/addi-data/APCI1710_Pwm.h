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

#define APCI1710_30MHZ		30
#define APCI1710_33MHZ		33
#define APCI1710_40MHZ		40

#define APCI1710_PWM_INIT		0
#define APCI1710_PWM_GETINITDATA	1

#define APCI1710_PWM_DISABLE		0
#define APCI1710_PWM_ENABLE		1
#define APCI1710_PWM_NEWTIMING		2

int i_APCI1710_InsnConfigPWM(struct comedi_device *dev, struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data);

int i_APCI1710_InitPWM(struct comedi_device *dev,
		       unsigned char b_ModulNbr,
		       unsigned char b_PWM,
		       unsigned char b_ClockSelection,
		       unsigned char b_TimingUnit,
		       unsigned int ul_LowTiming,
		       unsigned int ul_HighTiming,
		       unsigned int * pul_RealLowTiming, unsigned int * pul_RealHighTiming);

int i_APCI1710_GetPWMInitialisation(struct comedi_device *dev,
				    unsigned char b_ModulNbr,
				    unsigned char b_PWM,
				    unsigned char * pb_TimingUnit,
				    unsigned int * pul_LowTiming,
				    unsigned int * pul_HighTiming,
				    unsigned char * pb_StartLevel,
				    unsigned char * pb_StopMode,
				    unsigned char * pb_StopLevel,
				    unsigned char * pb_ExternGate,
				    unsigned char * pb_InterruptEnable, unsigned char * pb_Enable);

int i_APCI1710_InsnWritePWM(struct comedi_device *dev, struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);

int i_APCI1710_EnablePWM(struct comedi_device *dev,
			 unsigned char b_ModulNbr,
			 unsigned char b_PWM,
			 unsigned char b_StartLevel,
			 unsigned char b_StopMode,
			 unsigned char b_StopLevel, unsigned char b_ExternGate,
			 unsigned char b_InterruptEnable);

int i_APCI1710_SetNewPWMTiming(struct comedi_device *dev,
			       unsigned char b_ModulNbr,
			       unsigned char b_PWM, unsigned char b_TimingUnit,
			       unsigned int ul_LowTiming, unsigned int ul_HighTiming);

int i_APCI1710_DisablePWM(struct comedi_device *dev, unsigned char b_ModulNbr, unsigned char b_PWM);

int i_APCI1710_InsnReadGetPWMStatus(struct comedi_device *dev, struct comedi_subdevice *s,
				    struct comedi_insn *insn, unsigned int *data);

int i_APCI1710_InsnBitsReadPWMInterrupt(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn, unsigned int *data);
