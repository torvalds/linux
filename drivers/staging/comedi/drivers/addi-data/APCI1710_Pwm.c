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
/*

  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstraße 3       D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-----------------------------------------------------------------------+
  | Project     : API APCI1710    | Compiler : gcc                        |
  | Module name : PWM.C           | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date     :  02/12/2002                |
  +-----------------------------------------------------------------------+
  | Description :   APCI-1710 Wulse wide modulation module                |
  |                                                                       |
  |                                                                       |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +-----------------------------------------------------------------------+
  | 08/05/00 | Guinot C  | - 0400/0228 All Function in RING 0             |
  |          |           |   available                                    |
  +-----------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
|                               Included files                               |
+----------------------------------------------------------------------------+
*/

#include "APCI1710_Pwm.h"

/*
+----------------------------------------------------------------------------+
| Function Name     :INT i_APCI1710_InsnConfigPWM(struct comedi_device *dev,
struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)                        |
+----------------------------------------------------------------------------+
| Task              : Pwm Init and Get Pwm Initialisation                    |
+----------------------------------------------------------------------------+
| Input Parameters  :
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnConfigPWM(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	BYTE b_ConfigType;
	INT i_ReturnValue = 0;
	b_ConfigType = CR_CHAN(insn->chanspec);

	switch (b_ConfigType) {
	case APCI1710_PWM_INIT:
		i_ReturnValue = i_APCI1710_InitPWM(dev, (BYTE) CR_AREF(insn->chanspec),	//  b_ModulNbr
			(BYTE) data[0],	//b_PWM
			(BYTE) data[1],	// b_ClockSelection
			(BYTE) data[2],	// b_TimingUnit
			(ULONG) data[3],	//ul_LowTiming
			(ULONG) data[4],	//ul_HighTiming
			(PULONG) & data[0],	//pul_RealLowTiming
			(PULONG) & data[1]	//pul_RealHighTiming
			);
		break;

	case APCI1710_PWM_GETINITDATA:
		i_ReturnValue = i_APCI1710_GetPWMInitialisation(dev, (BYTE) CR_AREF(insn->chanspec),	// b_ModulNbr
			(BYTE) data[0],	//b_PWM
			(PBYTE) & data[0],	//pb_TimingUnit
			(PULONG) & data[1],	//pul_LowTiming
			(PULONG) & data[2],	//pul_HighTiming
			(PBYTE) & data[3],	// pb_StartLevel
			(PBYTE) & data[4],	// pb_StopMode
			(PBYTE) & data[5],	// pb_StopLevel
			(PBYTE) & data[6],	// pb_ExternGate
			(PBYTE) & data[7],	// pb_InterruptEnable
			(PBYTE) & data[8]	// pb_Enable
			);
		break;

	default:
		printk(" Config Parameter Wrong\n");
	}

	if (i_ReturnValue >= 0)
		i_ReturnValue = insn->n;
	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_InitPWM                               |
|                                       (BYTE_     b_BoardHandle,            |
|                                        BYTE_     b_ModulNbr,               |
|                                        BYTE_     b_PWM,                    |
|                                        BYTE_     b_ClockSelection,         |
|                                        BYTE_     b_TimingUnit,             |
|                                        ULONG_   ul_LowTiming,              |
|                                        ULONG_   ul_HighTiming,             |
|                                        PULONG_ pul_RealLowTiming,          |
|                                        PULONG_ pul_RealHighTiming)         |
+----------------------------------------------------------------------------+
| Task              : Configure the selected PWM (b_PWM) from selected module|
|                     (b_ModulNbr). The ul_LowTiming, ul_HighTiming and      |
|                     ul_TimingUnit determine the low/high timing base for   |
|                     the period. pul_RealLowTiming, pul_RealHighTiming      |
|                     return the real timing value.                          |
|                     You must calling this function be for you call any     |
|                     other function witch access of the PWM.                |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_     b_BoardHandle    : Handle of board APCI-1710 |
|                     BYTE_     b_ModulNbr       : Module number to configure|
|                                                  (0 to 3)                  |
|                     BYTE_     b_PWM            : Selected PWM (0 or 1).    |
|                     BYTE_     b_ClockSelection : Selection from PCI bus    |
|                                                  clock                     |
|                                                   - APCI1710_30MHZ :       |
|                                                     The PC have a 30 MHz   |
|                                                     PCI bus clock          |
|                                                   - APCI1710_33MHZ :       |
|                                                     The PC have a 33 MHz   |
|                                                     PCI bus clock          |
|                                                   - APCI1710_40MHZ         |
|                                                     The APCI-1710 have a   |
|                                                     integrated 40Mhz       |
|                                                     quartz.                |
|                     BYTE_     b_TimingUnit     : Base timing Unit (0 to 4) |
|                                                       0 : ns               |
|                                                       1 : æs               |
|                                                       2 : ms               |
|                                                       3 : s                |
|                                                       4 : mn               |
|                     ULONG_    ul_LowTiming     : Low base timing value.    |
|                     ULONG_    ul_HighTiming    : High base timing value.   |
+----------------------------------------------------------------------------+
| Output Parameters : PULONG_  pul_RealLowTiming  : Real low base timing     |
|                                                   value.                   |
|                     PULONG_  pul_RealHighTiming : Real high base timing    |
|                                                   value.                   |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: Module selection wrong                              |
|                    -3: The module is not a PWM module                      |
|                    -4: PWM selection is wrong                              |
|                    -5: The selected input clock is wrong                   |
|                    -6: Timing Unit selection is wrong                      |
|                    -7: Low base timing selection is wrong                  |
|                    -8: High base timing selection is wrong                 |
|                    -9: You can not used the 40MHz clock selection with     |
|                        this board                                          |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InitPWM(struct comedi_device * dev,
	BYTE b_ModulNbr,
	BYTE b_PWM,
	BYTE b_ClockSelection,
	BYTE b_TimingUnit,
	ULONG ul_LowTiming,
	ULONG ul_HighTiming,
	PULONG pul_RealLowTiming, PULONG pul_RealHighTiming)
{
	INT i_ReturnValue = 0;
	ULONG ul_LowTimerValue = 0;
	ULONG ul_HighTimerValue = 0;
	DWORD dw_Command;
	double d_RealLowTiming = 0;
	double d_RealHighTiming = 0;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***************/
		/* Test if PWM */
	   /***************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_PWM) {
	      /**************************/
			/* Test the PWM selection */
	      /**************************/

			if (b_PWM <= 1) {
		 /******************/
				/* Test the clock */
		 /******************/

				if ((b_ClockSelection == APCI1710_30MHZ) ||
					(b_ClockSelection == APCI1710_33MHZ) ||
					(b_ClockSelection == APCI1710_40MHZ)) {
		    /************************/
					/* Test the timing unit */
		    /************************/

					if (b_TimingUnit <= 4) {
		       /*********************************/
						/* Test the low timing selection */
		       /*********************************/

						if (((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 0)
								&& (ul_LowTiming
									>= 266)
								&& (ul_LowTiming
									<=
									0xFFFFFFFFUL))
							|| ((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 1)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									571230650UL))
							|| ((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 2)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									571230UL))
							|| ((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 3)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									571UL))
							|| ((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 4)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<= 9UL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 0)
								&& (ul_LowTiming
									>= 242)
								&& (ul_LowTiming
									<=
									0xFFFFFFFFUL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 1)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									519691043UL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 2)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									519691UL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 3)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									520UL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 4)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<= 8UL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 0)
								&& (ul_LowTiming
									>= 200)
								&& (ul_LowTiming
									<=
									0xFFFFFFFFUL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 1)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									429496729UL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 2)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									429496UL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 3)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									429UL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 4)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									7UL))) {
			  /**********************************/
							/* Test the High timing selection */
			  /**********************************/

							if (((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 0) && (ul_HighTiming >= 266) && (ul_HighTiming <= 0xFFFFFFFFUL)) || ((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 1) && (ul_HighTiming >= 1) && (ul_HighTiming <= 571230650UL)) || ((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 2) && (ul_HighTiming >= 1) && (ul_HighTiming <= 571230UL)) || ((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 3) && (ul_HighTiming >= 1) && (ul_HighTiming <= 571UL)) || ((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 4) && (ul_HighTiming >= 1) && (ul_HighTiming <= 9UL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 0) && (ul_HighTiming >= 242) && (ul_HighTiming <= 0xFFFFFFFFUL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 1) && (ul_HighTiming >= 1) && (ul_HighTiming <= 519691043UL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 2) && (ul_HighTiming >= 1) && (ul_HighTiming <= 519691UL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 3) && (ul_HighTiming >= 1) && (ul_HighTiming <= 520UL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 4) && (ul_HighTiming >= 1) && (ul_HighTiming <= 8UL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 0) && (ul_HighTiming >= 200) && (ul_HighTiming <= 0xFFFFFFFFUL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 1) && (ul_HighTiming >= 1) && (ul_HighTiming <= 429496729UL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 2) && (ul_HighTiming >= 1) && (ul_HighTiming <= 429496UL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 3) && (ul_HighTiming >= 1) && (ul_HighTiming <= 429UL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 4) && (ul_HighTiming >= 1) && (ul_HighTiming <= 7UL))) {
			     /**************************/
								/* Test the board version */
			     /**************************/

								if (((b_ClockSelection == APCI1710_40MHZ) && (devpriv->s_BoardInfos.b_BoardVersion > 0)) || (b_ClockSelection != APCI1710_40MHZ)) {

				/************************************/
									/* Calculate the low division fator */
				/************************************/

									fpu_begin
										();

									switch (b_TimingUnit) {
				   /******/
										/* ns */
				   /******/

									case 0:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_LowTimerValue
											=
											(ULONG)
											(ul_LowTiming
											*
											(0.00025 * b_ClockSelection));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_LowTiming * (0.00025 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
											ul_LowTimerValue
												=
												ul_LowTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealLowTiming
											=
											(ULONG)
											(ul_LowTimerValue
											/
											(0.00025 * (double)b_ClockSelection));
										d_RealLowTiming
											=
											(double)
											ul_LowTimerValue
											/
											(0.00025
											*
											(double)
											b_ClockSelection);

										if ((double)((double)ul_LowTimerValue / (0.00025 * (double)b_ClockSelection)) >= (double)((double)*pul_RealLowTiming + 0.5)) {
											*pul_RealLowTiming
												=
												*pul_RealLowTiming
												+
												1;
										}

										ul_LowTiming
											=
											ul_LowTiming
											-
											1;
										ul_LowTimerValue
											=
											ul_LowTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_LowTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_LowTimerValue)
												*
												1.007752288);
										}

										break;

				   /******/
										/* æs */
				   /******/

									case 1:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_LowTimerValue
											=
											(ULONG)
											(ul_LowTiming
											*
											(0.25 * b_ClockSelection));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_LowTiming * (0.25 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
											ul_LowTimerValue
												=
												ul_LowTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealLowTiming
											=
											(ULONG)
											(ul_LowTimerValue
											/
											(0.25 * (double)b_ClockSelection));
										d_RealLowTiming
											=
											(double)
											ul_LowTimerValue
											/
											(
											(double)
											0.25
											*
											(double)
											b_ClockSelection);

										if ((double)((double)ul_LowTimerValue / (0.25 * (double)b_ClockSelection)) >= (double)((double)*pul_RealLowTiming + 0.5)) {
											*pul_RealLowTiming
												=
												*pul_RealLowTiming
												+
												1;
										}

										ul_LowTiming
											=
											ul_LowTiming
											-
											1;
										ul_LowTimerValue
											=
											ul_LowTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_LowTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_LowTimerValue)
												*
												1.007752288);
										}

										break;

				   /******/
										/* ms */
				   /******/

									case 2:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_LowTimerValue
											=
											ul_LowTiming
											*
											(250.0
											*
											b_ClockSelection);

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_LowTiming * (250.0 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
											ul_LowTimerValue
												=
												ul_LowTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealLowTiming
											=
											(ULONG)
											(ul_LowTimerValue
											/
											(250.0 * (double)b_ClockSelection));
										d_RealLowTiming
											=
											(double)
											ul_LowTimerValue
											/
											(250.0
											*
											(double)
											b_ClockSelection);

										if ((double)((double)ul_LowTimerValue / (250.0 * (double)b_ClockSelection)) >= (double)((double)*pul_RealLowTiming + 0.5)) {
											*pul_RealLowTiming
												=
												*pul_RealLowTiming
												+
												1;
										}

										ul_LowTiming
											=
											ul_LowTiming
											-
											1;
										ul_LowTimerValue
											=
											ul_LowTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_LowTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_LowTimerValue)
												*
												1.007752288);
										}

										break;

				   /*****/
										/* s */
				   /*****/

									case 3:
					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_LowTimerValue
											=
											(ULONG)
											(ul_LowTiming
											*
											(250000.0
												*
												b_ClockSelection));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_LowTiming * (250000.0 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
											ul_LowTimerValue
												=
												ul_LowTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealLowTiming
											=
											(ULONG)
											(ul_LowTimerValue
											/
											(250000.0
												*
												(double)
												b_ClockSelection));
										d_RealLowTiming
											=
											(double)
											ul_LowTimerValue
											/
											(250000.0
											*
											(double)
											b_ClockSelection);

										if ((double)((double)ul_LowTimerValue / (250000.0 * (double)b_ClockSelection)) >= (double)((double)*pul_RealLowTiming + 0.5)) {
											*pul_RealLowTiming
												=
												*pul_RealLowTiming
												+
												1;
										}

										ul_LowTiming
											=
											ul_LowTiming
											-
											1;
										ul_LowTimerValue
											=
											ul_LowTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_LowTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_LowTimerValue)
												*
												1.007752288);
										}

										break;

				   /******/
										/* mn */
				   /******/

									case 4:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_LowTimerValue
											=
											(ULONG)
											(
											(ul_LowTiming
												*
												60)
											*
											(250000.0
												*
												b_ClockSelection));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)(ul_LowTiming * 60.0) * (250000.0 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
											ul_LowTimerValue
												=
												ul_LowTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealLowTiming
											=
											(ULONG)
											(ul_LowTimerValue
											/
											(250000.0
												*
												(double)
												b_ClockSelection))
											/
											60;
										d_RealLowTiming
											=
											(
											(double)
											ul_LowTimerValue
											/
											(250000.0
												*
												(double)
												b_ClockSelection))
											/
											60.0;

										if ((double)(((double)ul_LowTimerValue / (250000.0 * (double)b_ClockSelection)) / 60.0) >= (double)((double)*pul_RealLowTiming + 0.5)) {
											*pul_RealLowTiming
												=
												*pul_RealLowTiming
												+
												1;
										}

										ul_LowTiming
											=
											ul_LowTiming
											-
											1;
										ul_LowTimerValue
											=
											ul_LowTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_LowTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_LowTimerValue)
												*
												1.007752288);
										}

										break;
									}

				/*************************************/
									/* Calculate the high division fator */
				/*************************************/

									switch (b_TimingUnit) {
				   /******/
										/* ns */
				   /******/

									case 0:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_HighTimerValue
											=
											(ULONG)
											(ul_HighTiming
											*
											(0.00025 * b_ClockSelection));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_HighTiming * (0.00025 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
											ul_HighTimerValue
												=
												ul_HighTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealHighTiming
											=
											(ULONG)
											(ul_HighTimerValue
											/
											(0.00025 * (double)b_ClockSelection));
										d_RealHighTiming
											=
											(double)
											ul_HighTimerValue
											/
											(0.00025
											*
											(double)
											b_ClockSelection);

										if ((double)((double)ul_HighTimerValue / (0.00025 * (double)b_ClockSelection)) >= (double)((double)*pul_RealHighTiming + 0.5)) {
											*pul_RealHighTiming
												=
												*pul_RealHighTiming
												+
												1;
										}

										ul_HighTiming
											=
											ul_HighTiming
											-
											1;
										ul_HighTimerValue
											=
											ul_HighTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_HighTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_HighTimerValue)
												*
												1.007752288);
										}

										break;

				   /******/
										/* æs */
				   /******/

									case 1:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_HighTimerValue
											=
											(ULONG)
											(ul_HighTiming
											*
											(0.25 * b_ClockSelection));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_HighTiming * (0.25 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
											ul_HighTimerValue
												=
												ul_HighTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealHighTiming
											=
											(ULONG)
											(ul_HighTimerValue
											/
											(0.25 * (double)b_ClockSelection));
										d_RealHighTiming
											=
											(double)
											ul_HighTimerValue
											/
											(
											(double)
											0.25
											*
											(double)
											b_ClockSelection);

										if ((double)((double)ul_HighTimerValue / (0.25 * (double)b_ClockSelection)) >= (double)((double)*pul_RealHighTiming + 0.5)) {
											*pul_RealHighTiming
												=
												*pul_RealHighTiming
												+
												1;
										}

										ul_HighTiming
											=
											ul_HighTiming
											-
											1;
										ul_HighTimerValue
											=
											ul_HighTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_HighTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_HighTimerValue)
												*
												1.007752288);
										}

										break;

				   /******/
										/* ms */
				   /******/

									case 2:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_HighTimerValue
											=
											ul_HighTiming
											*
											(250.0
											*
											b_ClockSelection);

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_HighTiming * (250.0 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
											ul_HighTimerValue
												=
												ul_HighTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealHighTiming
											=
											(ULONG)
											(ul_HighTimerValue
											/
											(250.0 * (double)b_ClockSelection));
										d_RealHighTiming
											=
											(double)
											ul_HighTimerValue
											/
											(250.0
											*
											(double)
											b_ClockSelection);

										if ((double)((double)ul_HighTimerValue / (250.0 * (double)b_ClockSelection)) >= (double)((double)*pul_RealHighTiming + 0.5)) {
											*pul_RealHighTiming
												=
												*pul_RealHighTiming
												+
												1;
										}

										ul_HighTiming
											=
											ul_HighTiming
											-
											1;
										ul_HighTimerValue
											=
											ul_HighTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_HighTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_HighTimerValue)
												*
												1.007752288);
										}

										break;

				   /*****/
										/* s */
				   /*****/

									case 3:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_HighTimerValue
											=
											(ULONG)
											(ul_HighTiming
											*
											(250000.0
												*
												b_ClockSelection));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_HighTiming * (250000.0 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
											ul_HighTimerValue
												=
												ul_HighTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealHighTiming
											=
											(ULONG)
											(ul_HighTimerValue
											/
											(250000.0
												*
												(double)
												b_ClockSelection));
										d_RealHighTiming
											=
											(double)
											ul_HighTimerValue
											/
											(250000.0
											*
											(double)
											b_ClockSelection);

										if ((double)((double)ul_HighTimerValue / (250000.0 * (double)b_ClockSelection)) >= (double)((double)*pul_RealHighTiming + 0.5)) {
											*pul_RealHighTiming
												=
												*pul_RealHighTiming
												+
												1;
										}

										ul_HighTiming
											=
											ul_HighTiming
											-
											1;
										ul_HighTimerValue
											=
											ul_HighTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_HighTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_HighTimerValue)
												*
												1.007752288);
										}

										break;

				   /******/
										/* mn */
				   /******/

									case 4:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_HighTimerValue
											=
											(ULONG)
											(
											(ul_HighTiming
												*
												60)
											*
											(250000.0
												*
												b_ClockSelection));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)(ul_HighTiming * 60.0) * (250000.0 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
											ul_HighTimerValue
												=
												ul_HighTimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										*pul_RealHighTiming
											=
											(ULONG)
											(ul_HighTimerValue
											/
											(250000.0
												*
												(double)
												b_ClockSelection))
											/
											60;
										d_RealHighTiming
											=
											(
											(double)
											ul_HighTimerValue
											/
											(250000.0
												*
												(double)
												b_ClockSelection))
											/
											60.0;

										if ((double)(((double)ul_HighTimerValue / (250000.0 * (double)b_ClockSelection)) / 60.0) >= (double)((double)*pul_RealHighTiming + 0.5)) {
											*pul_RealHighTiming
												=
												*pul_RealHighTiming
												+
												1;
										}

										ul_HighTiming
											=
											ul_HighTiming
											-
											1;
										ul_HighTimerValue
											=
											ul_HighTimerValue
											-
											2;

										if (b_ClockSelection != APCI1710_40MHZ) {
											ul_HighTimerValue
												=
												(ULONG)
												(
												(double)
												(ul_HighTimerValue)
												*
												1.007752288);
										}

										break;
									}

									fpu_end();
				/****************************/
									/* Save the clock selection */
				/****************************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_PWMModuleInfo.
										b_ClockSelection
										=
										b_ClockSelection;

				/************************/
									/* Save the timing unit */
				/************************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_PWMModuleInfo.
										s_PWMInfo
										[b_PWM].
										b_TimingUnit
										=
										b_TimingUnit;

				/****************************/
									/* Save the low base timing */
				/****************************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_PWMModuleInfo.
										s_PWMInfo
										[b_PWM].
										d_LowTiming
										=
										d_RealLowTiming;

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_PWMModuleInfo.
										s_PWMInfo
										[b_PWM].
										ul_RealLowTiming
										=
										*pul_RealLowTiming;

				/****************************/
									/* Save the high base timing */
				/****************************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_PWMModuleInfo.
										s_PWMInfo
										[b_PWM].
										d_HighTiming
										=
										d_RealHighTiming;

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_PWMModuleInfo.
										s_PWMInfo
										[b_PWM].
										ul_RealHighTiming
										=
										*pul_RealHighTiming;

				/************************/
									/* Write the low timing */
				/************************/

									outl(ul_LowTimerValue, devpriv->s_BoardInfos.ui_Address + 0 + (20 * b_PWM) + (64 * b_ModulNbr));

				/*************************/
									/* Write the high timing */
				/*************************/

									outl(ul_HighTimerValue, devpriv->s_BoardInfos.ui_Address + 4 + (20 * b_PWM) + (64 * b_ModulNbr));

				/***************************/
									/* Set the clock selection */
				/***************************/

									dw_Command
										=
										inl
										(devpriv->
										s_BoardInfos.
										ui_Address
										+
										8
										+
										(20 * b_PWM) + (64 * b_ModulNbr));

									dw_Command
										=
										dw_Command
										&
										0x7F;

									if (b_ClockSelection == APCI1710_40MHZ) {
										dw_Command
											=
											dw_Command
											|
											0x80;
									}

				/***************************/
									/* Set the clock selection */
				/***************************/

									outl(dw_Command, devpriv->s_BoardInfos.ui_Address + 8 + (20 * b_PWM) + (64 * b_ModulNbr));

				/*************/
									/* PWM init. */
				/*************/
									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_PWMModuleInfo.
										s_PWMInfo
										[b_PWM].
										b_PWMInit
										=
										1;
								} else {
				/***************************************************/
									/* You can not used the 40MHz clock selection with */
									/* this board                                      */
				/***************************************************/
									DPRINTK("You can not used the 40MHz clock selection with this board\n");
									i_ReturnValue
										=
										-9;
								}
							} else {
			     /***************************************/
								/* High base timing selection is wrong */
			     /***************************************/
								DPRINTK("High base timing selection is wrong\n");
								i_ReturnValue =
									-8;
							}
						} else {
			  /**************************************/
							/* Low base timing selection is wrong */
			  /**************************************/
							DPRINTK("Low base timing selection is wrong\n");
							i_ReturnValue = -7;
						}
					}	// if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4))
					else {
		       /**********************************/
						/* Timing unit selection is wrong */
		       /**********************************/
						DPRINTK("Timing unit selection is wrong\n");
						i_ReturnValue = -6;
					}	// if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4))
				}	// if ((b_ClockSelection == APCI1710_30MHZ) || (b_ClockSelection == APCI1710_33MHZ) || (b_ClockSelection == APCI1710_40MHZ))
				else {
		    /*******************************/
					/* The selected clock is wrong */
		    /*******************************/
					DPRINTK("The selected clock is wrong\n");
					i_ReturnValue = -5;
				}	// if ((b_ClockSelection == APCI1710_30MHZ) || (b_ClockSelection == APCI1710_33MHZ) || (b_ClockSelection == APCI1710_40MHZ))
			}	// if (b_PWM >= 0 && b_PWM <= 1)
			else {
		 /******************************/
				/* Tor PWM selection is wrong */
		 /******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_PWM >= 0 && b_PWM <= 1)
		} else {
	      /**********************************/
			/* The module is not a PWM module */
	      /**********************************/
			DPRINTK("The module is not a PWM module\n");
			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/
		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_GetPWMInitialisation                  |
|                                       (BYTE_     b_BoardHandle,            |
|                                        BYTE_     b_ModulNbr,               |
|                                        BYTE_     b_PWM,                    |
|                                        PBYTE_   pb_TimingUnit,             |
|                                        PULONG_ pul_LowTiming,              |
|                                        PULONG_ pul_HighTiming,             |
|                                        PBYTE_   pb_StartLevel,             |
|                                        PBYTE_   pb_StopMode,               |
|                                        PBYTE_   pb_StopLevel,              |
|                                        PBYTE_   pb_ExternGate,             |
|                                        PBYTE_   pb_InterruptEnable,        |
|                                        PBYTE_   pb_Enable)                 |
+----------------------------------------------------------------------------+
| Task              : Return the PWM (b_PWM) initialisation from selected    |
|                     module (b_ModulNbr). You must calling the              |
|                     "i_APCI1710_InitPWM" function be for you call this     |
|                     function.                                              |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_ b_BoardHandle : Handle of board APCI-1710        |
|                     BYTE_ b_ModulNbr    : Selected module number (0 to 3)  |
|                     BYTE_ b_PWM         : Selected PWM (0 or 1)            |
+----------------------------------------------------------------------------+
| Output Parameters : PBYTE_  pb_TimingUnit      : Base timing Unit (0 to 4) |
|                                                       0 : ns               |
|                                                       1 : æs               |
|                                                       2 : ms               |
|                                                       3 : s                |
|                                                       4 : mn               |
|                     PULONG_ pul_LowTiming      : Low base timing value.    |
|                     PULONG_ pul_HighTiming     : High base timing value.   |
|                     PBYTE_  pb_StartLevel      : Start period level        |
|                                                  selection                 |
|                                                       0 : The period start |
|                                                           with a low level |
|                                                       1 : The period start |
|                                                           with a high level|
|                     PBYTE_  pb_StopMode        : Stop mode selection       |
|                                                  0 : The PWM is stopped    |
|                                                      directly after the    |
|                                                     "i_APCI1710_DisablePWM"|
|                                                      function and break the|
|                                                      last period           |
|                                                  1 : After the             |
|                                                     "i_APCI1710_DisablePWM"|
|                                                      function the PWM is   |
|                                                      stopped at the end    |
|                                                      from last period cycle|
|                     PBYTE_  pb_StopLevel        : Stop PWM level selection |
|                                                    0 : The output signal   |
|                                                        keep the level after|
|                                                        the                 |
|                                                     "i_APCI1710_DisablePWM"|
|                                                        function            |
|                                                    1 : The output signal is|
|                                                        set to low after the|
|                                                     "i_APCI1710_DisablePWM"|
|                                                        function            |
|                                                    2 : The output signal is|
|                                                        set to high after   |
|                                                        the                 |
|                                                     "i_APCI1710_DisablePWM"|
|                                                        function            |
|                     PBYTE_  pb_ExternGate      : Extern gate action        |
|                                                  selection                 |
|                                                   0 : Extern gate signal   |
|                                                       not used.            |
|                                                   1 : Extern gate signal   |
|                                                       used.                |
|                     PBYTE_  pb_InterruptEnable : Enable or disable the PWM |
|                                                  interrupt.                |
|                                                  - APCI1710_ENABLE :       |
|                                                    Enable the PWM interrupt|
|                                                    A interrupt occur after |
|                                                    each period             |
|                                                  - APCI1710_DISABLE :      |
|                                                    Disable the PWM         |
|                                                    interrupt               |
|                     PBYTE_  pb_Enable          : Indicate if the PWM is    |
|                                                  enabled or no             |
|                                                       0 : PWM not enabled  |
|                                                       1 : PWM enabled      |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a PWM module                     |
|                     -4: PWM selection is wrong                             |
|                     -5: PWM not initialised see function                   |
|                         "i_APCI1710_InitPWM"                               |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_GetPWMInitialisation(struct comedi_device * dev,
	BYTE b_ModulNbr,
	BYTE b_PWM,
	PBYTE pb_TimingUnit,
	PULONG pul_LowTiming,
	PULONG pul_HighTiming,
	PBYTE pb_StartLevel,
	PBYTE pb_StopMode,
	PBYTE pb_StopLevel,
	PBYTE pb_ExternGate, PBYTE pb_InterruptEnable, PBYTE pb_Enable)
{
	INT i_ReturnValue = 0;
	DWORD dw_Status;
	DWORD dw_Command;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***************/
		/* Test if PWM */
	   /***************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_PWM) {
	      /**************************/
			/* Test the PWM selection */
	      /**************************/

			if (b_PWM <= 1) {
		 /***************************/
				/* Test if PWM initialised */
		 /***************************/

				dw_Status = inl(devpriv->s_BoardInfos.
					ui_Address + 12 + (20 * b_PWM) +
					(64 * b_ModulNbr));

				if (dw_Status & 0x10) {
		    /***********************/
					/* Read the low timing */
		    /***********************/

					*pul_LowTiming =
						inl(devpriv->s_BoardInfos.
						ui_Address + 0 + (20 * b_PWM) +
						(64 * b_ModulNbr));

		    /************************/
					/* Read the high timing */
		    /************************/

					*pul_HighTiming =
						inl(devpriv->s_BoardInfos.
						ui_Address + 4 + (20 * b_PWM) +
						(64 * b_ModulNbr));

		    /********************/
					/* Read the command */
		    /********************/

					dw_Command = inl(devpriv->s_BoardInfos.
						ui_Address + 8 + (20 * b_PWM) +
						(64 * b_ModulNbr));

					*pb_StartLevel =
						(BYTE) ((dw_Command >> 5) & 1);
					*pb_StopMode =
						(BYTE) ((dw_Command >> 0) & 1);
					*pb_StopLevel =
						(BYTE) ((dw_Command >> 1) & 1);
					*pb_ExternGate =
						(BYTE) ((dw_Command >> 4) & 1);
					*pb_InterruptEnable =
						(BYTE) ((dw_Command >> 3) & 1);

					if (*pb_StopLevel) {
						*pb_StopLevel =
							*pb_StopLevel +
							(BYTE) ((dw_Command >>
								2) & 1);
					}

		    /********************/
					/* Read the command */
		    /********************/

					dw_Command = inl(devpriv->s_BoardInfos.
						ui_Address + 8 + (20 * b_PWM) +
						(64 * b_ModulNbr));

					*pb_Enable =
						(BYTE) ((dw_Command >> 0) & 1);

					*pb_TimingUnit = devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_PWMModuleInfo.
						s_PWMInfo[b_PWM].b_TimingUnit;
				}	// if (dw_Status & 0x10)
				else {
		    /***********************/
					/* PWM not initialised */
		    /***********************/
					DPRINTK("PWM not initialised\n");
					i_ReturnValue = -5;
				}	// if (dw_Status & 0x10)
			}	// if (b_PWM >= 0 && b_PWM <= 1)
			else {
		 /******************************/
				/* Tor PWM selection is wrong */
		 /******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_PWM >= 0 && b_PWM <= 1)
		} else {
	      /**********************************/
			/* The module is not a PWM module */
	      /**********************************/
			DPRINTK("The module is not a PWM module\n");
			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/
		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     :INT i_APCI1710_InsnWritePWM(struct comedi_device *dev,
struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)                        |
+----------------------------------------------------------------------------+
| Task              : Pwm Enable Disable and Set New Timing                  |
+----------------------------------------------------------------------------+
| Input Parameters  :
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnWritePWM(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	BYTE b_WriteType;
	INT i_ReturnValue = 0;
	b_WriteType = CR_CHAN(insn->chanspec);

	switch (b_WriteType) {
	case APCI1710_PWM_ENABLE:
		i_ReturnValue = i_APCI1710_EnablePWM(dev,
			(BYTE) CR_AREF(insn->chanspec),
			(BYTE) data[0],
			(BYTE) data[1],
			(BYTE) data[2],
			(BYTE) data[3], (BYTE) data[4], (BYTE) data[5]);
		break;

	case APCI1710_PWM_DISABLE:
		i_ReturnValue = i_APCI1710_DisablePWM(dev,
			(BYTE) CR_AREF(insn->chanspec), (BYTE) data[0]);
		break;

	case APCI1710_PWM_NEWTIMING:
		i_ReturnValue = i_APCI1710_SetNewPWMTiming(dev,
			(BYTE) CR_AREF(insn->chanspec),
			(BYTE) data[0],
			(BYTE) data[1], (ULONG) data[2], (ULONG) data[3]);
		break;

	default:
		printk("Write Config Parameter Wrong\n");
	}

	if (i_ReturnValue >= 0)
		i_ReturnValue = insn->n;
	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_EnablePWM                         |
|                                       (BYTE_  b_BoardHandle,               |
|                                        BYTE_  b_ModulNbr,                  |
|                                        BYTE_  b_PWM,                       |
|                                        BYTE_  b_StartLevel,                |
|                                        BYTE_  b_StopMode,                  |
|                                        BYTE_  b_StopLevel,                 |
|                                        BYTE_  b_ExternGate,                |
|                                        BYTE_  b_InterruptEnable)           |
+----------------------------------------------------------------------------+
| Task              : Enable the selected PWM (b_PWM) from selected module   |
|                     (b_ModulNbr). You must calling the "i_APCI1710_InitPWM"|
|                     function be for you call this function.                |
|                     If you enable the PWM interrupt, the PWM generate a    |
|                     interrupt after each period.                           |
|                     See function "i_APCI1710_SetBoardIntRoutineX" and the  |
|                     Interrupt mask description chapter.                    |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_ b_BoardHandle     : Handle of board APCI-1710    |
|                     BYTE_ b_ModulNbr        : Selected module number       |
|                                               (0 to 3)                     |
|                     BYTE_ b_PWM             : Selected PWM (0 or 1)        |
|                     BYTE_ b_StartLevel      : Start period level selection |
|                                                0 : The period start with a |
|                                                    low level               |
|                                                1 : The period start with a |
|                                                    high level              |
|                     BYTE_ b_StopMode        : Stop mode selection          |
|                                                0 : The PWM is stopped      |
|                                                    directly after the      |
|                                                    "i_APCI1710_DisablePWM" |
|                                                    function and break the  |
|                                                    last period             |
|                                                1 : After the               |
|                                                    "i_APCI1710_DisablePWM" |
|                                                     function the PWM is    |
|                                                     stopped at the end from|
|                                                     last period cycle.     |
|                     BYTE_ b_StopLevel       : Stop PWM level selection     |
|                                                0 : The output signal keep  |
|                                                    the level after the     |
|                                                    "i_APCI1710_DisablePWM" |
|                                                    function                |
|                                                1 : The output signal is set|
|                                                    to low after the        |
|                                                    "i_APCI1710_DisablePWM" |
|                                                    function                |
|                                                2 : The output signal is set|
|                                                    to high after the       |
|                                                    "i_APCI1710_DisablePWM" |
|                                                    function                |
|                     BYTE_ b_ExternGate      : Extern gate action selection |
|                                                0 : Extern gate signal not  |
|                                                    used.                   |
|                                                1 : Extern gate signal used.|
|                     BYTE_ b_InterruptEnable : Enable or disable the PWM    |
|                                               interrupt.                   |
|                                               - APCI1710_ENABLE :          |
|                                                 Enable the PWM interrupt   |
|                                                 A interrupt occur after    |
|                                                 each period                |
|                                               - APCI1710_DISABLE :         |
|                                                 Disable the PWM interrupt  |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0:  No error                                           |
|                    -1:  The handle parameter of the board is wrong         |
|                    -2:  Module selection wrong                             |
|                    -3:  The module is not a PWM module                     |
|                    -4:  PWM selection is wrong                             |
|                    -5:  PWM not initialised see function                   |
|                         "i_APCI1710_InitPWM"                               |
|                    -6:  PWM start level selection is wrong                 |
|                    -7:  PWM stop mode selection is wrong                   |
|                    -8:  PWM stop level selection is wrong                  |
|                    -9:  Extern gate signal selection is wrong              |
|                    -10: Interrupt parameter is wrong                       |
|                    -11: Interrupt function not initialised.                |
|                         See function "i_APCI1710_SetBoardIntRoutineX"      |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_EnablePWM(struct comedi_device * dev,
	BYTE b_ModulNbr,
	BYTE b_PWM,
	BYTE b_StartLevel,
	BYTE b_StopMode,
	BYTE b_StopLevel, BYTE b_ExternGate, BYTE b_InterruptEnable)
{
	INT i_ReturnValue = 0;
	DWORD dw_Status;
	DWORD dw_Command;

	devpriv->tsk_Current = current;	// Save the current process task structure
	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***************/
		/* Test if PWM */
	   /***************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_PWM) {
	      /**************************/
			/* Test the PWM selection */
	      /**************************/

			if (b_PWM <= 1) {
		 /***************************/
				/* Test if PWM initialised */
		 /***************************/

				dw_Status = inl(devpriv->s_BoardInfos.
					ui_Address + 12 + (20 * b_PWM) +
					(64 * b_ModulNbr));

				if (dw_Status & 0x10) {
		    /**********************************/
					/* Test the start level selection */
		    /**********************************/

					if (b_StartLevel <= 1) {
		       /**********************/
						/* Test the stop mode */
		       /**********************/

						if (b_StopMode <= 1) {
			  /***********************/
							/* Test the stop level */
			  /***********************/

							if (b_StopLevel <= 2) {
			     /*****************************/
								/* Test the extern gate mode */
			     /*****************************/

								if (b_ExternGate
									<= 1) {
				/*****************************/
									/* Test the interrupt action */
				/*****************************/

									if (b_InterruptEnable == APCI1710_ENABLE || b_InterruptEnable == APCI1710_DISABLE) {
				   /******************************************/
										/* Test if interrupt function initialised */
				   /******************************************/

				      /********************/
										/* Read the command */
				      /********************/

										dw_Command
											=
											inl
											(devpriv->
											s_BoardInfos.
											ui_Address
											+
											8
											+
											(20 * b_PWM) + (64 * b_ModulNbr));

										dw_Command
											=
											dw_Command
											&
											0x80;

				      /********************/
										/* Make the command */
				      /********************/

										dw_Command
											=
											dw_Command
											|
											b_StopMode
											|
											(b_InterruptEnable
											<<
											3)
											|
											(b_ExternGate
											<<
											4)
											|
											(b_StartLevel
											<<
											5);

										if (b_StopLevel & 3) {
											dw_Command
												=
												dw_Command
												|
												2;

											if (b_StopLevel & 2) {
												dw_Command
													=
													dw_Command
													|
													4;
											}
										}

										devpriv->
											s_ModuleInfo
											[b_ModulNbr].
											s_PWMModuleInfo.
											s_PWMInfo
											[b_PWM].
											b_InterruptEnable
											=
											b_InterruptEnable;

				      /*******************/
										/* Set the command */
				      /*******************/

										outl(dw_Command, devpriv->s_BoardInfos.ui_Address + 8 + (20 * b_PWM) + (64 * b_ModulNbr));

				      /******************/
										/* Enable the PWM */
				      /******************/
										outl(1, devpriv->s_BoardInfos.ui_Address + 12 + (20 * b_PWM) + (64 * b_ModulNbr));
									}	// if (b_InterruptEnable == APCI1710_ENABLE || b_InterruptEnable == APCI1710_DISABLE)
									else {
				   /********************************/
										/* Interrupt parameter is wrong */
				   /********************************/
										DPRINTK("Interrupt parameter is wrong\n");
										i_ReturnValue
											=
											-10;
									}	// if (b_InterruptEnable == APCI1710_ENABLE || b_InterruptEnable == APCI1710_DISABLE)
								}	// if (b_ExternGate >= 0 && b_ExternGate <= 1)
								else {
				/*****************************************/
									/* Extern gate signal selection is wrong */
				/*****************************************/
									DPRINTK("Extern gate signal selection is wrong\n");
									i_ReturnValue
										=
										-9;
								}	// if (b_ExternGate >= 0 && b_ExternGate <= 1)
							}	// if (b_StopLevel >= 0 && b_StopLevel <= 2)
							else {
			     /*************************************/
								/* PWM stop level selection is wrong */
			     /*************************************/
								DPRINTK("PWM stop level selection is wrong\n");
								i_ReturnValue =
									-8;
							}	// if (b_StopLevel >= 0 && b_StopLevel <= 2)
						}	// if (b_StopMode >= 0 && b_StopMode <= 1)
						else {
			  /************************************/
							/* PWM stop mode selection is wrong */
			  /************************************/
							DPRINTK("PWM stop mode selection is wrong\n");
							i_ReturnValue = -7;
						}	// if (b_StopMode >= 0 && b_StopMode <= 1)
					}	// if (b_StartLevel >= 0 && b_StartLevel <= 1)
					else {
		       /**************************************/
						/* PWM start level selection is wrong */
		       /**************************************/
						DPRINTK("PWM start level selection is wrong\n");
						i_ReturnValue = -6;
					}	// if (b_StartLevel >= 0 && b_StartLevel <= 1)
				}	// if (dw_Status & 0x10)
				else {
		    /***********************/
					/* PWM not initialised */
		    /***********************/
					DPRINTK("PWM not initialised\n");
					i_ReturnValue = -5;
				}	// if (dw_Status & 0x10)
			}	// if (b_PWM >= 0 && b_PWM <= 1)
			else {
		 /******************************/
				/* Tor PWM selection is wrong */
		 /******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_PWM >= 0 && b_PWM <= 1)
		} else {
	      /**********************************/
			/* The module is not a PWM module */
	      /**********************************/
			DPRINTK("The module is not a PWM module\n");
			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/
		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_DisablePWM (BYTE_  b_BoardHandle,     |
|                                                  BYTE_  b_ModulNbr,        |
|                                                  BYTE_  b_PWM)             |
+----------------------------------------------------------------------------+
| Task              : Disable the selected PWM (b_PWM) from selected module  |
|                     (b_ModulNbr). The output signal level depend of the    |
|                     initialisation by the "i_APCI1710_EnablePWM".          |
|                     See the b_StartLevel, b_StopMode and b_StopLevel       |
|                     parameters from this function.                         |
+----------------------------------------------------------------------------+
| Input Parameters  :BYTE_ b_BoardHandle : Handle of board APCI-1710         |
|                    BYTE_ b_ModulNbr    : Selected module number (0 to 3)   |
|                    BYTE_ b_PWM         : Selected PWM (0 or 1)             |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a PWM module                     |
|                     -4: PWM selection is wrong                             |
|                     -5: PWM not initialised see function                   |
|                         "i_APCI1710_InitPWM"                               |
|                     -6: PWM not enabled see function                       |
|                         "i_APCI1710_EnablePWM"                             |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_DisablePWM(struct comedi_device * dev, BYTE b_ModulNbr, BYTE b_PWM)
{
	INT i_ReturnValue = 0;
	DWORD dw_Status;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***************/
		/* Test if PWM */
	   /***************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_PWM) {
	      /**************************/
			/* Test the PWM selection */
	      /**************************/

			if (b_PWM <= 1) {
		 /***************************/
				/* Test if PWM initialised */
		 /***************************/

				dw_Status = inl(devpriv->s_BoardInfos.
					ui_Address + 12 + (20 * b_PWM) +
					(64 * b_ModulNbr));

				if (dw_Status & 0x10) {
		    /***********************/
					/* Test if PWM enabled */
		    /***********************/

					if (dw_Status & 0x1) {
		       /*******************/
						/* Disable the PWM */
		       /*******************/
						outl(0, devpriv->s_BoardInfos.
							ui_Address + 12 +
							(20 * b_PWM) +
							(64 * b_ModulNbr));
					}	// if (dw_Status & 0x1)
					else {
		       /*******************/
						/* PWM not enabled */
		       /*******************/
						DPRINTK("PWM not enabled\n");
						i_ReturnValue = -6;
					}	// if (dw_Status & 0x1)
				}	// if (dw_Status & 0x10)
				else {
		    /***********************/
					/* PWM not initialised */
		    /***********************/
					DPRINTK(" PWM not initialised\n");
					i_ReturnValue = -5;
				}	// if (dw_Status & 0x10)
			}	// if (b_PWM >= 0 && b_PWM <= 1)
			else {
		 /******************************/
				/* Tor PWM selection is wrong */
		 /******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_PWM >= 0 && b_PWM <= 1)
		} else {
	      /**********************************/
			/* The module is not a PWM module */
	      /**********************************/
			DPRINTK("The module is not a PWM module\n");
			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/
		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_SetNewPWMTiming                       |
|                                       (BYTE_     b_BoardHandle,            |
|                                        BYTE_     b_ModulNbr,               |
|                                        BYTE_     b_PWM,                    |
|                                        BYTE_     b_ClockSelection,         |
|                                        BYTE_     b_TimingUnit,             |
|                                        ULONG_   ul_LowTiming,              |
|                                        ULONG_   ul_HighTiming)             |
+----------------------------------------------------------------------------+
| Task              : Set a new timing. The ul_LowTiming, ul_HighTiming and  |
|                     ul_TimingUnit determine the low/high timing base for   |
|                     the period.                                            |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_     b_BoardHandle    : Handle of board APCI-1710 |
|                     BYTE_     b_ModulNbr       : Module number to configure|
|                                                  (0 to 3)                  |
|                     BYTE_     b_PWM            : Selected PWM (0 or 1).    |
|                     BYTE_     b_TimingUnit     : Base timing Unit (0 to 4) |
|                                                       0 : ns               |
|                                                       1 : æs               |
|                                                       2 : ms               |
|                                                       3 : s                |
|                                                       4 : mn               |
|                     ULONG_    ul_LowTiming     : Low base timing value.    |
|                     ULONG_    ul_HighTiming    : High base timing value.   |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: Module selection wrong                              |
|                    -3: The module is not a PWM module                      |
|                    -4: PWM selection is wrong                              |
|                    -5: PWM not initialised                                 |
|                    -6: Timing Unit selection is wrong                      |
|                    -7: Low base timing selection is wrong                  |
|                    -8: High base timing selection is wrong                 |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_SetNewPWMTiming(struct comedi_device * dev,
	BYTE b_ModulNbr,
	BYTE b_PWM, BYTE b_TimingUnit, ULONG ul_LowTiming, ULONG ul_HighTiming)
{
	BYTE b_ClockSelection;
	INT i_ReturnValue = 0;
	ULONG ul_LowTimerValue = 0;
	ULONG ul_HighTimerValue = 0;
	ULONG ul_RealLowTiming = 0;
	ULONG ul_RealHighTiming = 0;
	DWORD dw_Status;
	DWORD dw_Command;
	double d_RealLowTiming = 0;
	double d_RealHighTiming = 0;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***************/
		/* Test if PWM */
	   /***************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_PWM) {
	      /**************************/
			/* Test the PWM selection */
	      /**************************/

			if (b_PWM <= 1) {
		 /***************************/
				/* Test if PWM initialised */
		 /***************************/

				dw_Status = inl(devpriv->s_BoardInfos.
					ui_Address + 12 + (20 * b_PWM) +
					(64 * b_ModulNbr));

				if (dw_Status & 0x10) {
					b_ClockSelection = devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_PWMModuleInfo.
						b_ClockSelection;

		    /************************/
					/* Test the timing unit */
		    /************************/

					if (b_TimingUnit <= 4) {
		       /*********************************/
						/* Test the low timing selection */
		       /*********************************/

						if (((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 0)
								&& (ul_LowTiming
									>= 266)
								&& (ul_LowTiming
									<=
									0xFFFFFFFFUL))
							|| ((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 1)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									571230650UL))
							|| ((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 2)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									571230UL))
							|| ((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 3)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									571UL))
							|| ((b_ClockSelection ==
									APCI1710_30MHZ)
								&& (b_TimingUnit
									== 4)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<= 9UL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 0)
								&& (ul_LowTiming
									>= 242)
								&& (ul_LowTiming
									<=
									0xFFFFFFFFUL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 1)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									519691043UL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 2)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									519691UL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 3)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									520UL))
							|| ((b_ClockSelection ==
									APCI1710_33MHZ)
								&& (b_TimingUnit
									== 4)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<= 8UL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 0)
								&& (ul_LowTiming
									>= 200)
								&& (ul_LowTiming
									<=
									0xFFFFFFFFUL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 1)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									429496729UL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 2)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									429496UL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 3)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									429UL))
							|| ((b_ClockSelection ==
									APCI1710_40MHZ)
								&& (b_TimingUnit
									== 4)
								&& (ul_LowTiming
									>= 1)
								&& (ul_LowTiming
									<=
									7UL))) {
			  /**********************************/
							/* Test the High timing selection */
			  /**********************************/

							if (((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 0) && (ul_HighTiming >= 266) && (ul_HighTiming <= 0xFFFFFFFFUL)) || ((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 1) && (ul_HighTiming >= 1) && (ul_HighTiming <= 571230650UL)) || ((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 2) && (ul_HighTiming >= 1) && (ul_HighTiming <= 571230UL)) || ((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 3) && (ul_HighTiming >= 1) && (ul_HighTiming <= 571UL)) || ((b_ClockSelection == APCI1710_30MHZ) && (b_TimingUnit == 4) && (ul_HighTiming >= 1) && (ul_HighTiming <= 9UL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 0) && (ul_HighTiming >= 242) && (ul_HighTiming <= 0xFFFFFFFFUL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 1) && (ul_HighTiming >= 1) && (ul_HighTiming <= 519691043UL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 2) && (ul_HighTiming >= 1) && (ul_HighTiming <= 519691UL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 3) && (ul_HighTiming >= 1) && (ul_HighTiming <= 520UL)) || ((b_ClockSelection == APCI1710_33MHZ) && (b_TimingUnit == 4) && (ul_HighTiming >= 1) && (ul_HighTiming <= 8UL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 0) && (ul_HighTiming >= 200) && (ul_HighTiming <= 0xFFFFFFFFUL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 1) && (ul_HighTiming >= 1) && (ul_HighTiming <= 429496729UL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 2) && (ul_HighTiming >= 1) && (ul_HighTiming <= 429496UL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 3) && (ul_HighTiming >= 1) && (ul_HighTiming <= 429UL)) || ((b_ClockSelection == APCI1710_40MHZ) && (b_TimingUnit == 4) && (ul_HighTiming >= 1) && (ul_HighTiming <= 7UL))) {
			     /************************************/
								/* Calculate the low division fator */
			     /************************************/

								fpu_begin();
								switch (b_TimingUnit) {
				/******/
									/* ns */
				/******/

								case 0:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_LowTimerValue
										=
										(ULONG)
										(ul_LowTiming
										*
										(0.00025 * b_ClockSelection));

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)ul_LowTiming * (0.00025 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
										ul_LowTimerValue
											=
											ul_LowTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealLowTiming
										=
										(ULONG)
										(ul_LowTimerValue
										/
										(0.00025 * (double)b_ClockSelection));
									d_RealLowTiming
										=
										(double)
										ul_LowTimerValue
										/
										(0.00025
										*
										(double)
										b_ClockSelection);

									if ((double)((double)ul_LowTimerValue / (0.00025 * (double)b_ClockSelection)) >= (double)((double)ul_RealLowTiming + 0.5)) {
										ul_RealLowTiming
											=
											ul_RealLowTiming
											+
											1;
									}

									ul_LowTiming
										=
										ul_LowTiming
										-
										1;
									ul_LowTimerValue
										=
										ul_LowTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_LowTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_LowTimerValue)
											*
											1.007752288);
									}

									break;

				/******/
									/* æs */
				/******/

								case 1:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_LowTimerValue
										=
										(ULONG)
										(ul_LowTiming
										*
										(0.25 * b_ClockSelection));

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)ul_LowTiming * (0.25 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
										ul_LowTimerValue
											=
											ul_LowTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealLowTiming
										=
										(ULONG)
										(ul_LowTimerValue
										/
										(0.25 * (double)b_ClockSelection));
									d_RealLowTiming
										=
										(double)
										ul_LowTimerValue
										/
										(
										(double)
										0.25
										*
										(double)
										b_ClockSelection);

									if ((double)((double)ul_LowTimerValue / (0.25 * (double)b_ClockSelection)) >= (double)((double)ul_RealLowTiming + 0.5)) {
										ul_RealLowTiming
											=
											ul_RealLowTiming
											+
											1;
									}

									ul_LowTiming
										=
										ul_LowTiming
										-
										1;
									ul_LowTimerValue
										=
										ul_LowTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_LowTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_LowTimerValue)
											*
											1.007752288);
									}

									break;

				/******/
									/* ms */
				/******/

								case 2:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_LowTimerValue
										=
										ul_LowTiming
										*
										(250.0
										*
										b_ClockSelection);

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)ul_LowTiming * (250.0 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
										ul_LowTimerValue
											=
											ul_LowTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealLowTiming
										=
										(ULONG)
										(ul_LowTimerValue
										/
										(250.0 * (double)b_ClockSelection));
									d_RealLowTiming
										=
										(double)
										ul_LowTimerValue
										/
										(250.0
										*
										(double)
										b_ClockSelection);

									if ((double)((double)ul_LowTimerValue / (250.0 * (double)b_ClockSelection)) >= (double)((double)ul_RealLowTiming + 0.5)) {
										ul_RealLowTiming
											=
											ul_RealLowTiming
											+
											1;
									}

									ul_LowTiming
										=
										ul_LowTiming
										-
										1;
									ul_LowTimerValue
										=
										ul_LowTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_LowTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_LowTimerValue)
											*
											1.007752288);
									}

									break;

				/*****/
									/* s */
				/*****/

								case 3:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_LowTimerValue
										=
										(ULONG)
										(ul_LowTiming
										*
										(250000.0
											*
											b_ClockSelection));

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)ul_LowTiming * (250000.0 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
										ul_LowTimerValue
											=
											ul_LowTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealLowTiming
										=
										(ULONG)
										(ul_LowTimerValue
										/
										(250000.0
											*
											(double)
											b_ClockSelection));
									d_RealLowTiming
										=
										(double)
										ul_LowTimerValue
										/
										(250000.0
										*
										(double)
										b_ClockSelection);

									if ((double)((double)ul_LowTimerValue / (250000.0 * (double)b_ClockSelection)) >= (double)((double)ul_RealLowTiming + 0.5)) {
										ul_RealLowTiming
											=
											ul_RealLowTiming
											+
											1;
									}

									ul_LowTiming
										=
										ul_LowTiming
										-
										1;
									ul_LowTimerValue
										=
										ul_LowTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_LowTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_LowTimerValue)
											*
											1.007752288);
									}

									break;

				/******/
									/* mn */
				/******/

								case 4:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_LowTimerValue
										=
										(ULONG)
										(
										(ul_LowTiming
											*
											60)
										*
										(250000.0
											*
											b_ClockSelection));

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)(ul_LowTiming * 60.0) * (250000.0 * (double)b_ClockSelection)) >= ((double)((double)ul_LowTimerValue + 0.5))) {
										ul_LowTimerValue
											=
											ul_LowTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealLowTiming
										=
										(ULONG)
										(ul_LowTimerValue
										/
										(250000.0
											*
											(double)
											b_ClockSelection))
										/
										60;
									d_RealLowTiming
										=
										(
										(double)
										ul_LowTimerValue
										/
										(250000.0
											*
											(double)
											b_ClockSelection))
										/
										60.0;

									if ((double)(((double)ul_LowTimerValue / (250000.0 * (double)b_ClockSelection)) / 60.0) >= (double)((double)ul_RealLowTiming + 0.5)) {
										ul_RealLowTiming
											=
											ul_RealLowTiming
											+
											1;
									}

									ul_LowTiming
										=
										ul_LowTiming
										-
										1;
									ul_LowTimerValue
										=
										ul_LowTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_LowTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_LowTimerValue)
											*
											1.007752288);
									}

									break;
								}

			     /*************************************/
								/* Calculate the high division fator */
			     /*************************************/

								switch (b_TimingUnit) {
				/******/
									/* ns */
				/******/

								case 0:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_HighTimerValue
										=
										(ULONG)
										(ul_HighTiming
										*
										(0.00025 * b_ClockSelection));

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)ul_HighTiming * (0.00025 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
										ul_HighTimerValue
											=
											ul_HighTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealHighTiming
										=
										(ULONG)
										(ul_HighTimerValue
										/
										(0.00025 * (double)b_ClockSelection));
									d_RealHighTiming
										=
										(double)
										ul_HighTimerValue
										/
										(0.00025
										*
										(double)
										b_ClockSelection);

									if ((double)((double)ul_HighTimerValue / (0.00025 * (double)b_ClockSelection)) >= (double)((double)ul_RealHighTiming + 0.5)) {
										ul_RealHighTiming
											=
											ul_RealHighTiming
											+
											1;
									}

									ul_HighTiming
										=
										ul_HighTiming
										-
										1;
									ul_HighTimerValue
										=
										ul_HighTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_HighTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_HighTimerValue)
											*
											1.007752288);
									}

									break;

				/******/
									/* æs */
				/******/

								case 1:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_HighTimerValue
										=
										(ULONG)
										(ul_HighTiming
										*
										(0.25 * b_ClockSelection));

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)ul_HighTiming * (0.25 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
										ul_HighTimerValue
											=
											ul_HighTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealHighTiming
										=
										(ULONG)
										(ul_HighTimerValue
										/
										(0.25 * (double)b_ClockSelection));
									d_RealHighTiming
										=
										(double)
										ul_HighTimerValue
										/
										(
										(double)
										0.25
										*
										(double)
										b_ClockSelection);

									if ((double)((double)ul_HighTimerValue / (0.25 * (double)b_ClockSelection)) >= (double)((double)ul_RealHighTiming + 0.5)) {
										ul_RealHighTiming
											=
											ul_RealHighTiming
											+
											1;
									}

									ul_HighTiming
										=
										ul_HighTiming
										-
										1;
									ul_HighTimerValue
										=
										ul_HighTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_HighTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_HighTimerValue)
											*
											1.007752288);
									}

									break;

				/******/
									/* ms */
				/******/

								case 2:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_HighTimerValue
										=
										ul_HighTiming
										*
										(250.0
										*
										b_ClockSelection);

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)ul_HighTiming * (250.0 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
										ul_HighTimerValue
											=
											ul_HighTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealHighTiming
										=
										(ULONG)
										(ul_HighTimerValue
										/
										(250.0 * (double)b_ClockSelection));
									d_RealHighTiming
										=
										(double)
										ul_HighTimerValue
										/
										(250.0
										*
										(double)
										b_ClockSelection);

									if ((double)((double)ul_HighTimerValue / (250.0 * (double)b_ClockSelection)) >= (double)((double)ul_RealHighTiming + 0.5)) {
										ul_RealHighTiming
											=
											ul_RealHighTiming
											+
											1;
									}

									ul_HighTiming
										=
										ul_HighTiming
										-
										1;
									ul_HighTimerValue
										=
										ul_HighTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_HighTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_HighTimerValue)
											*
											1.007752288);
									}

									break;

				/*****/
									/* s */
				/*****/

								case 3:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_HighTimerValue
										=
										(ULONG)
										(ul_HighTiming
										*
										(250000.0
											*
											b_ClockSelection));

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)ul_HighTiming * (250000.0 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
										ul_HighTimerValue
											=
											ul_HighTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealHighTiming
										=
										(ULONG)
										(ul_HighTimerValue
										/
										(250000.0
											*
											(double)
											b_ClockSelection));
									d_RealHighTiming
										=
										(double)
										ul_HighTimerValue
										/
										(250000.0
										*
										(double)
										b_ClockSelection);

									if ((double)((double)ul_HighTimerValue / (250000.0 * (double)b_ClockSelection)) >= (double)((double)ul_RealHighTiming + 0.5)) {
										ul_RealHighTiming
											=
											ul_RealHighTiming
											+
											1;
									}

									ul_HighTiming
										=
										ul_HighTiming
										-
										1;
									ul_HighTimerValue
										=
										ul_HighTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_HighTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_HighTimerValue)
											*
											1.007752288);
									}

									break;

				/******/
									/* mn */
				/******/

								case 4:

					/******************/
									/* Timer 0 factor */
					/******************/

									ul_HighTimerValue
										=
										(ULONG)
										(
										(ul_HighTiming
											*
											60)
										*
										(250000.0
											*
											b_ClockSelection));

					/*******************/
									/* Round the value */
					/*******************/

									if ((double)((double)(ul_HighTiming * 60.0) * (250000.0 * (double)b_ClockSelection)) >= ((double)((double)ul_HighTimerValue + 0.5))) {
										ul_HighTimerValue
											=
											ul_HighTimerValue
											+
											1;
									}

					/*****************************/
									/* Calculate the real timing */
					/*****************************/

									ul_RealHighTiming
										=
										(ULONG)
										(ul_HighTimerValue
										/
										(250000.0
											*
											(double)
											b_ClockSelection))
										/
										60;
									d_RealHighTiming
										=
										(
										(double)
										ul_HighTimerValue
										/
										(250000.0
											*
											(double)
											b_ClockSelection))
										/
										60.0;

									if ((double)(((double)ul_HighTimerValue / (250000.0 * (double)b_ClockSelection)) / 60.0) >= (double)((double)ul_RealHighTiming + 0.5)) {
										ul_RealHighTiming
											=
											ul_RealHighTiming
											+
											1;
									}

									ul_HighTiming
										=
										ul_HighTiming
										-
										1;
									ul_HighTimerValue
										=
										ul_HighTimerValue
										-
										2;

									if (b_ClockSelection != APCI1710_40MHZ) {
										ul_HighTimerValue
											=
											(ULONG)
											(
											(double)
											(ul_HighTimerValue)
											*
											1.007752288);
									}

									break;
								}

								fpu_end();

			     /************************/
								/* Save the timing unit */
			     /************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PWMModuleInfo.
									s_PWMInfo
									[b_PWM].
									b_TimingUnit
									=
									b_TimingUnit;

			     /****************************/
								/* Save the low base timing */
			     /****************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PWMModuleInfo.
									s_PWMInfo
									[b_PWM].
									d_LowTiming
									=
									d_RealLowTiming;

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PWMModuleInfo.
									s_PWMInfo
									[b_PWM].
									ul_RealLowTiming
									=
									ul_RealLowTiming;

			     /****************************/
								/* Save the high base timing */
			     /****************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PWMModuleInfo.
									s_PWMInfo
									[b_PWM].
									d_HighTiming
									=
									d_RealHighTiming;

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PWMModuleInfo.
									s_PWMInfo
									[b_PWM].
									ul_RealHighTiming
									=
									ul_RealHighTiming;

			     /************************/
								/* Write the low timing */
			     /************************/

								outl(ul_LowTimerValue, devpriv->s_BoardInfos.ui_Address + 0 + (20 * b_PWM) + (64 * b_ModulNbr));

			     /*************************/
								/* Write the high timing */
			     /*************************/

								outl(ul_HighTimerValue, devpriv->s_BoardInfos.ui_Address + 4 + (20 * b_PWM) + (64 * b_ModulNbr));

			     /***************************/
								/* Set the clock selection */
			     /***************************/

								dw_Command =
									inl
									(devpriv->
									s_BoardInfos.
									ui_Address
									+ 8 +
									(20 * b_PWM) + (64 * b_ModulNbr));

								dw_Command =
									dw_Command
									& 0x7F;

								if (b_ClockSelection == APCI1710_40MHZ) {
									dw_Command
										=
										dw_Command
										|
										0x80;
								}

			     /***************************/
								/* Set the clock selection */
			     /***************************/

								outl(dw_Command,
									devpriv->
									s_BoardInfos.
									ui_Address
									+ 8 +
									(20 * b_PWM) + (64 * b_ModulNbr));
							} else {
			     /***************************************/
								/* High base timing selection is wrong */
			     /***************************************/
								DPRINTK("High base timing selection is wrong\n");
								i_ReturnValue =
									-8;
							}
						} else {
			  /**************************************/
							/* Low base timing selection is wrong */
			  /**************************************/
							DPRINTK("Low base timing selection is wrong\n");
							i_ReturnValue = -7;
						}
					}	// if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4))
					else {
		       /**********************************/
						/* Timing unit selection is wrong */
		       /**********************************/
						DPRINTK("Timing unit selection is wrong\n");
						i_ReturnValue = -6;
					}	// if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4))
				}	// if (dw_Status & 0x10)
				else {
		    /***********************/
					/* PWM not initialised */
		    /***********************/
					DPRINTK("PWM not initialised\n");
					i_ReturnValue = -5;
				}	// if (dw_Status & 0x10)
			}	// if (b_PWM >= 0 && b_PWM <= 1)
			else {
		 /******************************/
				/* Tor PWM selection is wrong */
		 /******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_PWM >= 0 && b_PWM <= 1)
		} else {
	      /**********************************/
			/* The module is not a PWM module */
	      /**********************************/
			DPRINTK("The module is not a PWM module\n");
			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/
		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_GetPWMStatus                          |
|                               (BYTE_    b_BoardHandle,                     |
|                                BYTE_    b_ModulNbr,                        |
|                                BYTE_    b_PWM,                             |
|                                PBYTE_  pb_PWMOutputStatus,                 |
|                                PBYTE_  pb_ExternGateStatus)                |
+----------------------------------------------------------------------------+
| Task              : Return the status from selected PWM (b_PWM) from       |
|                     selected module (b_ModulNbr).                          |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_  b_BoardHandle : Handle of board APCI-1710       |
|                     BYTE_  b_PWM         : Selected PWM (0 or 1)           |
|                     BYTE_  b_ModulNbr    : Selected module number (0 to 3)
	b_ModulNbr			=(BYTE)  CR_AREF(insn->chanspec);
	b_PWM				=(BYTE)  data[0];

 |
+----------------------------------------------------------------------------+
| Output Parameters : PBYTE_  pb_PWMOutputStatus  : Return the PWM output    |
|                                                   level status.            |
|                                                    0 : The PWM output level|
|                                                        is low.             |
|                                                    1 : The PWM output level|
|                                                        is high.            |
|                     PBYTE_  pb_ExternGateStatus : Return the extern gate   |
|                                                   level status.            |
|                                                    0 : The extern gate is  |
|                                                        low.                |
|                                                    1 : The extern gate is  |
|                                                        high.
    pb_PWMOutputStatus	=(PBYTE) data[0];
	pb_ExternGateStatus =(PBYTE) data[1];             |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a PWM module                     |
|                     -4: PWM selection is wrong                             |
|                     -5: PWM not initialised see function                   |
|                         "i_APCI1710_InitPWM"                               |
|                     -6: PWM not enabled see function "i_APCI1710_EnablePWM"|
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnReadGetPWMStatus(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	INT i_ReturnValue = 0;
	DWORD dw_Status;

	BYTE b_ModulNbr;
	BYTE b_PWM;
	PBYTE pb_PWMOutputStatus;
	PBYTE pb_ExternGateStatus;

	i_ReturnValue = insn->n;
	b_ModulNbr = (BYTE) CR_AREF(insn->chanspec);
	b_PWM = (BYTE) CR_CHAN(insn->chanspec);
	pb_PWMOutputStatus = (PBYTE) & data[0];
	pb_ExternGateStatus = (PBYTE) & data[1];

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***************/
		/* Test if PWM */
	   /***************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_PWM) {
	      /**************************/
			/* Test the PWM selection */
	      /**************************/

			if (b_PWM <= 1) {
		 /***************************/
				/* Test if PWM initialised */
		 /***************************/

				dw_Status = inl(devpriv->s_BoardInfos.
					ui_Address + 12 + (20 * b_PWM) +
					(64 * b_ModulNbr));

				if (dw_Status & 0x10) {
		    /***********************/
					/* Test if PWM enabled */
		    /***********************/

					if (dw_Status & 0x1) {
						*pb_PWMOutputStatus =
							(BYTE) ((dw_Status >> 7)
							& 1);
						*pb_ExternGateStatus =
							(BYTE) ((dw_Status >> 6)
							& 1);
					}	// if (dw_Status & 0x1)
					else {
		       /*******************/
						/* PWM not enabled */
		       /*******************/

						DPRINTK("PWM not enabled \n");
						i_ReturnValue = -6;
					}	// if (dw_Status & 0x1)
				}	// if (dw_Status & 0x10)
				else {
		    /***********************/
					/* PWM not initialised */
		    /***********************/

					DPRINTK("PWM not initialised\n");
					i_ReturnValue = -5;
				}	// if (dw_Status & 0x10)
			}	// if (b_PWM >= 0 && b_PWM <= 1)
			else {
		 /******************************/
				/* Tor PWM selection is wrong */
		 /******************************/

				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_PWM >= 0 && b_PWM <= 1)
		} else {
	      /**********************************/
			/* The module is not a PWM module */
	      /**********************************/

			DPRINTK("The module is not a PWM module\n");
			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/

		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

INT i_APCI1710_InsnBitsReadPWMInterrupt(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	data[0] = devpriv->s_InterruptParameters.
		s_FIFOInterruptParameters[devpriv->
		s_InterruptParameters.ui_Read].b_OldModuleMask;
	data[1] = devpriv->s_InterruptParameters.
		s_FIFOInterruptParameters[devpriv->
		s_InterruptParameters.ui_Read].ul_OldInterruptMask;
	data[2] = devpriv->s_InterruptParameters.
		s_FIFOInterruptParameters[devpriv->
		s_InterruptParameters.ui_Read].ul_OldCounterLatchValue;

			     /**************************/
	/* Increment the read FIFO */
			     /***************************/

	devpriv->
		s_InterruptParameters.
		ui_Read = (devpriv->
		s_InterruptParameters.ui_Read + 1) % APCI1710_SAVE_INTERRUPT;

	return insn->n;

}
