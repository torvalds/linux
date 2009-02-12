/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

        ADDI-DATA GmbH
        Dieselstrasse 3
        D-77833 Ottersweier
        Tel: +19(0)7223/9493-0
        Fax: +49(0)7223/9493-92
        http://www.addi-data-com
        info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You shoud also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/

#define APCI1710_30MHZ           30
#define APCI1710_33MHZ           33
#define APCI1710_40MHZ           40

#define APCI1710_PWM_INIT			0
#define APCI1710_PWM_GETINITDATA	1

#define APCI1710_PWM_DISABLE        0
#define APCI1710_PWM_ENABLE			1
#define APCI1710_PWM_NEWTIMING      2

INT i_APCI1710_InsnConfigPWM(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);

INT i_APCI1710_InitPWM(comedi_device * dev,
	BYTE b_ModulNbr,
	BYTE b_PWM,
	BYTE b_ClockSelection,
	BYTE b_TimingUnit,
	ULONG ul_LowTiming,
	ULONG ul_HighTiming,
	PULONG pul_RealLowTiming, PULONG pul_RealHighTiming);

INT i_APCI1710_GetPWMInitialisation(comedi_device * dev,
	BYTE b_ModulNbr,
	BYTE b_PWM,
	PBYTE pb_TimingUnit,
	PULONG pul_LowTiming,
	PULONG pul_HighTiming,
	PBYTE pb_StartLevel,
	PBYTE pb_StopMode,
	PBYTE pb_StopLevel,
	PBYTE pb_ExternGate, PBYTE pb_InterruptEnable, PBYTE pb_Enable);

INT i_APCI1710_InsnWritePWM(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);

INT i_APCI1710_EnablePWM(comedi_device * dev,
	BYTE b_ModulNbr,
	BYTE b_PWM,
	BYTE b_StartLevel,
	BYTE b_StopMode,
	BYTE b_StopLevel, BYTE b_ExternGate, BYTE b_InterruptEnable);

INT i_APCI1710_SetNewPWMTiming(comedi_device * dev,
	BYTE b_ModulNbr,
	BYTE b_PWM, BYTE b_TimingUnit, ULONG ul_LowTiming, ULONG ul_HighTiming);

INT i_APCI1710_DisablePWM(comedi_device * dev, BYTE b_ModulNbr, BYTE b_PWM);

INT i_APCI1710_InsnReadGetPWMStatus(comedi_device * dev, comedi_subdevice * s,
	comedi_insn * insn, lsampl_t * data);

INT i_APCI1710_InsnBitsReadPWMInterrupt(comedi_device * dev,
	comedi_subdevice * s, comedi_insn * insn, lsampl_t * data);
