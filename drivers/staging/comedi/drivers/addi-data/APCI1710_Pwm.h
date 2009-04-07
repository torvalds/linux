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

INT i_APCI1710_InsnConfigPWM(struct comedi_device *dev, struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data);

INT i_APCI1710_InitPWM(struct comedi_device *dev,
		       BYTE b_ModulNbr,
		       BYTE b_PWM,
		       BYTE b_ClockSelection,
		       BYTE b_TimingUnit,
		       ULONG ul_LowTiming,
		       ULONG ul_HighTiming,
		       PULONG pul_RealLowTiming, PULONG pul_RealHighTiming);

INT i_APCI1710_GetPWMInitialisation(struct comedi_device *dev,
				    BYTE b_ModulNbr,
				    BYTE b_PWM,
				    PBYTE pb_TimingUnit,
				    PULONG pul_LowTiming,
				    PULONG pul_HighTiming,
				    PBYTE pb_StartLevel,
				    PBYTE pb_StopMode,
				    PBYTE pb_StopLevel,
				    PBYTE pb_ExternGate,
				    PBYTE pb_InterruptEnable, PBYTE pb_Enable);

INT i_APCI1710_InsnWritePWM(struct comedi_device *dev, struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);

INT i_APCI1710_EnablePWM(struct comedi_device *dev,
			 BYTE b_ModulNbr,
			 BYTE b_PWM,
			 BYTE b_StartLevel,
			 BYTE b_StopMode,
			 BYTE b_StopLevel, BYTE b_ExternGate,
			 BYTE b_InterruptEnable);

INT i_APCI1710_SetNewPWMTiming(struct comedi_device *dev,
			       BYTE b_ModulNbr,
			       BYTE b_PWM, BYTE b_TimingUnit,
			       ULONG ul_LowTiming, ULONG ul_HighTiming);

INT i_APCI1710_DisablePWM(struct comedi_device *dev, BYTE b_ModulNbr, BYTE b_PWM);

INT i_APCI1710_InsnReadGetPWMStatus(struct comedi_device *dev, struct comedi_subdevice *s,
				    struct comedi_insn *insn, unsigned int *data);

INT i_APCI1710_InsnBitsReadPWMInterrupt(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn, unsigned int *data);
