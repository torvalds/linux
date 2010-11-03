/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

	ADDI-DATA GmbH
	Dieselstrasse 3
	D-77833 Ottersweier
	Tel: +19(0)7223/9493-0
	Fax: +49(0)7223/9493-92
	http://www.addi-data.com
	info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You should also find the complete GPL in the COPYING file accompanying this source code.

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

int i_APCI1710_InsnConfigPWM(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned char b_ConfigType;
	int i_ReturnValue = 0;
	b_ConfigType = CR_CHAN(insn->chanspec);

	switch (b_ConfigType) {
	case APCI1710_PWM_INIT:
		i_ReturnValue = i_APCI1710_InitPWM(dev, (unsigned char) CR_AREF(insn->chanspec),	/*   b_ModulNbr */
			(unsigned char) data[0],	/* b_PWM */
			(unsigned char) data[1],	/*  b_ClockSelection */
			(unsigned char) data[2],	/*  b_TimingUnit */
			(unsigned int) data[3],	/* ul_LowTiming */
			(unsigned int) data[4],	/* ul_HighTiming */
			(unsigned int *) &data[0],	/* pul_RealLowTiming */
			(unsigned int *) &data[1]	/* pul_RealHighTiming */
			);
		break;

	case APCI1710_PWM_GETINITDATA:
		i_ReturnValue = i_APCI1710_GetPWMInitialisation(dev, (unsigned char) CR_AREF(insn->chanspec),	/*  b_ModulNbr */
			(unsigned char) data[0],	/* b_PWM */
			(unsigned char *) &data[0],	/* pb_TimingUnit */
			(unsigned int *) &data[1],	/* pul_LowTiming */
			(unsigned int *) &data[2],	/* pul_HighTiming */
			(unsigned char *) &data[3],	/*  pb_StartLevel */
			(unsigned char *) &data[4],	/*  pb_StopMode */
			(unsigned char *) &data[5],	/*  pb_StopLevel */
			(unsigned char *) &data[6],	/*  pb_ExternGate */
			(unsigned char *) &data[7],	/*  pb_InterruptEnable */
			(unsigned char *) &data[8]	/*  pb_Enable */
			);
		break;

	default:
		printk(" Config Parameter Wrong\n");
	}

	if (i_ReturnValue >= 0)
		i_ReturnValue = insn->n;
	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_InitPWM                               |
|                                       (unsigned char_     b_BoardHandle,            |
|                                        unsigned char_     b_ModulNbr,               |
|                                        unsigned char_     b_PWM,                    |
|                                        unsigned char_     b_ClockSelection,         |
|                                        unsigned char_     b_TimingUnit,             |
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
| Input Parameters  : unsigned char_     b_BoardHandle    : Handle of board APCI-1710 |
|                     unsigned char_     b_ModulNbr       : Module number to configure|
|                                                  (0 to 3)                  |
|                     unsigned char_     b_PWM            : Selected PWM (0 or 1).    |
|                     unsigned char_     b_ClockSelection : Selection from PCI bus    |
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
|                     unsigned char_     b_TimingUnit     : Base timing Unit (0 to 4) |
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

int i_APCI1710_InitPWM(struct comedi_device *dev,
	unsigned char b_ModulNbr,
	unsigned char b_PWM,
	unsigned char b_ClockSelection,
	unsigned char b_TimingUnit,
	unsigned int ul_LowTiming,
	unsigned int ul_HighTiming,
	unsigned int *pul_RealLowTiming, unsigned int *pul_RealHighTiming)
{
	int i_ReturnValue = 0;
	unsigned int ul_LowTimerValue = 0;
	unsigned int ul_HighTimerValue = 0;
	unsigned int dw_Command;
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
											(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
											(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
											(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
											(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
											(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
											(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
											(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
											(unsigned int)
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
											(unsigned int)
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
												(unsigned int)
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
					}	/*  if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4)) */
					else {
						/**********************************/
						/* Timing unit selection is wrong */
						/**********************************/
						DPRINTK("Timing unit selection is wrong\n");
						i_ReturnValue = -6;
					}	/*  if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4)) */
				}	/*  if ((b_ClockSelection == APCI1710_30MHZ) || (b_ClockSelection == APCI1710_33MHZ) || (b_ClockSelection == APCI1710_40MHZ)) */
				else {
					/*******************************/
					/* The selected clock is wrong */
					/*******************************/
					DPRINTK("The selected clock is wrong\n");
					i_ReturnValue = -5;
				}	/*  if ((b_ClockSelection == APCI1710_30MHZ) || (b_ClockSelection == APCI1710_33MHZ) || (b_ClockSelection == APCI1710_40MHZ)) */
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
			else {
				/******************************/
				/* Tor PWM selection is wrong */
				/******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
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

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_GetPWMInitialisation                  |
|                                       (unsigned char_     b_BoardHandle,            |
|                                        unsigned char_     b_ModulNbr,               |
|                                        unsigned char_     b_PWM,                    |
|                                        unsigned char *_   pb_TimingUnit,             |
|                                        PULONG_ pul_LowTiming,              |
|                                        PULONG_ pul_HighTiming,             |
|                                        unsigned char *_   pb_StartLevel,             |
|                                        unsigned char *_   pb_StopMode,               |
|                                        unsigned char *_   pb_StopLevel,              |
|                                        unsigned char *_   pb_ExternGate,             |
|                                        unsigned char *_   pb_InterruptEnable,        |
|                                        unsigned char *_   pb_Enable)                 |
+----------------------------------------------------------------------------+
| Task              : Return the PWM (b_PWM) initialisation from selected    |
|                     module (b_ModulNbr). You must calling the              |
|                     "i_APCI1710_InitPWM" function be for you call this     |
|                     function.                                              |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle : Handle of board APCI-1710        |
|                     unsigned char_ b_ModulNbr    : Selected module number (0 to 3)  |
|                     unsigned char_ b_PWM         : Selected PWM (0 or 1)            |
+----------------------------------------------------------------------------+
| Output Parameters : unsigned char *_  pb_TimingUnit      : Base timing Unit (0 to 4) |
|                                                       0 : ns               |
|                                                       1 : æs               |
|                                                       2 : ms               |
|                                                       3 : s                |
|                                                       4 : mn               |
|                     PULONG_ pul_LowTiming      : Low base timing value.    |
|                     PULONG_ pul_HighTiming     : High base timing value.   |
|                     unsigned char *_  pb_StartLevel      : Start period level        |
|                                                  selection                 |
|                                                       0 : The period start |
|                                                           with a low level |
|                                                       1 : The period start |
|                                                           with a high level|
|                     unsigned char *_  pb_StopMode        : Stop mode selection       |
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
|                     unsigned char *_  pb_StopLevel        : Stop PWM level selection |
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
|                     unsigned char *_  pb_ExternGate      : Extern gate action        |
|                                                  selection                 |
|                                                   0 : Extern gate signal   |
|                                                       not used.            |
|                                                   1 : Extern gate signal   |
|                                                       used.                |
|                     unsigned char *_  pb_InterruptEnable : Enable or disable the PWM |
|                                                  interrupt.                |
|                                                  - APCI1710_ENABLE :       |
|                                                    Enable the PWM interrupt|
|                                                    A interrupt occur after |
|                                                    each period             |
|                                                  - APCI1710_DISABLE :      |
|                                                    Disable the PWM         |
|                                                    interrupt               |
|                     unsigned char *_  pb_Enable          : Indicate if the PWM is    |
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

int i_APCI1710_GetPWMInitialisation(struct comedi_device *dev,
	unsigned char b_ModulNbr,
	unsigned char b_PWM,
	unsigned char *pb_TimingUnit,
	unsigned int *pul_LowTiming,
	unsigned int *pul_HighTiming,
	unsigned char *pb_StartLevel,
	unsigned char *pb_StopMode,
	unsigned char *pb_StopLevel,
	unsigned char *pb_ExternGate, unsigned char *pb_InterruptEnable, unsigned char *pb_Enable)
{
	int i_ReturnValue = 0;
	unsigned int dw_Status;
	unsigned int dw_Command;

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
						(unsigned char) ((dw_Command >> 5) & 1);
					*pb_StopMode =
						(unsigned char) ((dw_Command >> 0) & 1);
					*pb_StopLevel =
						(unsigned char) ((dw_Command >> 1) & 1);
					*pb_ExternGate =
						(unsigned char) ((dw_Command >> 4) & 1);
					*pb_InterruptEnable =
						(unsigned char) ((dw_Command >> 3) & 1);

					if (*pb_StopLevel) {
						*pb_StopLevel =
							*pb_StopLevel +
							(unsigned char) ((dw_Command >>
								2) & 1);
					}

					/********************/
					/* Read the command */
					/********************/

					dw_Command = inl(devpriv->s_BoardInfos.
						ui_Address + 8 + (20 * b_PWM) +
						(64 * b_ModulNbr));

					*pb_Enable =
						(unsigned char) ((dw_Command >> 0) & 1);

					*pb_TimingUnit = devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_PWMModuleInfo.
						s_PWMInfo[b_PWM].b_TimingUnit;
				}	/*  if (dw_Status & 0x10) */
				else {
					/***********************/
					/* PWM not initialised */
					/***********************/
					DPRINTK("PWM not initialised\n");
					i_ReturnValue = -5;
				}	/*  if (dw_Status & 0x10) */
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
			else {
				/******************************/
				/* Tor PWM selection is wrong */
				/******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
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

	return i_ReturnValue;
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

int i_APCI1710_InsnWritePWM(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	unsigned char b_WriteType;
	int i_ReturnValue = 0;
	b_WriteType = CR_CHAN(insn->chanspec);

	switch (b_WriteType) {
	case APCI1710_PWM_ENABLE:
		i_ReturnValue = i_APCI1710_EnablePWM(dev,
			(unsigned char) CR_AREF(insn->chanspec),
			(unsigned char) data[0],
			(unsigned char) data[1],
			(unsigned char) data[2],
			(unsigned char) data[3], (unsigned char) data[4], (unsigned char) data[5]);
		break;

	case APCI1710_PWM_DISABLE:
		i_ReturnValue = i_APCI1710_DisablePWM(dev,
			(unsigned char) CR_AREF(insn->chanspec), (unsigned char) data[0]);
		break;

	case APCI1710_PWM_NEWTIMING:
		i_ReturnValue = i_APCI1710_SetNewPWMTiming(dev,
			(unsigned char) CR_AREF(insn->chanspec),
			(unsigned char) data[0],
			(unsigned char) data[1], (unsigned int) data[2], (unsigned int) data[3]);
		break;

	default:
		printk("Write Config Parameter Wrong\n");
	}

	if (i_ReturnValue >= 0)
		i_ReturnValue = insn->n;
	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_EnablePWM                         |
|                                       (unsigned char_  b_BoardHandle,               |
|                                        unsigned char_  b_ModulNbr,                  |
|                                        unsigned char_  b_PWM,                       |
|                                        unsigned char_  b_StartLevel,                |
|                                        unsigned char_  b_StopMode,                  |
|                                        unsigned char_  b_StopLevel,                 |
|                                        unsigned char_  b_ExternGate,                |
|                                        unsigned char_  b_InterruptEnable)           |
+----------------------------------------------------------------------------+
| Task              : Enable the selected PWM (b_PWM) from selected module   |
|                     (b_ModulNbr). You must calling the "i_APCI1710_InitPWM"|
|                     function be for you call this function.                |
|                     If you enable the PWM interrupt, the PWM generate a    |
|                     interrupt after each period.                           |
|                     See function "i_APCI1710_SetBoardIntRoutineX" and the  |
|                     Interrupt mask description chapter.                    |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle     : Handle of board APCI-1710    |
|                     unsigned char_ b_ModulNbr        : Selected module number       |
|                                               (0 to 3)                     |
|                     unsigned char_ b_PWM             : Selected PWM (0 or 1)        |
|                     unsigned char_ b_StartLevel      : Start period level selection |
|                                                0 : The period start with a |
|                                                    low level               |
|                                                1 : The period start with a |
|                                                    high level              |
|                     unsigned char_ b_StopMode        : Stop mode selection          |
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
|                     unsigned char_ b_StopLevel       : Stop PWM level selection     |
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
|                     unsigned char_ b_ExternGate      : Extern gate action selection |
|                                                0 : Extern gate signal not  |
|                                                    used.                   |
|                                                1 : Extern gate signal used.|
|                     unsigned char_ b_InterruptEnable : Enable or disable the PWM    |
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

int i_APCI1710_EnablePWM(struct comedi_device *dev,
	unsigned char b_ModulNbr,
	unsigned char b_PWM,
	unsigned char b_StartLevel,
	unsigned char b_StopMode,
	unsigned char b_StopLevel, unsigned char b_ExternGate, unsigned char b_InterruptEnable)
{
	int i_ReturnValue = 0;
	unsigned int dw_Status;
	unsigned int dw_Command;

	devpriv->tsk_Current = current;	/*  Save the current process task structure */
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
									}	/*  if (b_InterruptEnable == APCI1710_ENABLE || b_InterruptEnable == APCI1710_DISABLE) */
									else {
										/********************************/
										/* Interrupt parameter is wrong */
										/********************************/
										DPRINTK("Interrupt parameter is wrong\n");
										i_ReturnValue
											=
											-10;
									}	/*  if (b_InterruptEnable == APCI1710_ENABLE || b_InterruptEnable == APCI1710_DISABLE) */
								}	/*  if (b_ExternGate >= 0 && b_ExternGate <= 1) */
								else {
									/*****************************************/
									/* Extern gate signal selection is wrong */
									/*****************************************/
									DPRINTK("Extern gate signal selection is wrong\n");
									i_ReturnValue
										=
										-9;
								}	/*  if (b_ExternGate >= 0 && b_ExternGate <= 1) */
							}	/*  if (b_StopLevel >= 0 && b_StopLevel <= 2) */
							else {
								/*************************************/
								/* PWM stop level selection is wrong */
								/*************************************/
								DPRINTK("PWM stop level selection is wrong\n");
								i_ReturnValue =
									-8;
							}	/*  if (b_StopLevel >= 0 && b_StopLevel <= 2) */
						}	/*  if (b_StopMode >= 0 && b_StopMode <= 1) */
						else {
							/************************************/
							/* PWM stop mode selection is wrong */
							/************************************/
							DPRINTK("PWM stop mode selection is wrong\n");
							i_ReturnValue = -7;
						}	/*  if (b_StopMode >= 0 && b_StopMode <= 1) */
					}	/*  if (b_StartLevel >= 0 && b_StartLevel <= 1) */
					else {
						/**************************************/
						/* PWM start level selection is wrong */
						/**************************************/
						DPRINTK("PWM start level selection is wrong\n");
						i_ReturnValue = -6;
					}	/*  if (b_StartLevel >= 0 && b_StartLevel <= 1) */
				}	/*  if (dw_Status & 0x10) */
				else {
					/***********************/
					/* PWM not initialised */
					/***********************/
					DPRINTK("PWM not initialised\n");
					i_ReturnValue = -5;
				}	/*  if (dw_Status & 0x10) */
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
			else {
				/******************************/
				/* Tor PWM selection is wrong */
				/******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
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

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_DisablePWM (unsigned char_  b_BoardHandle,     |
|                                                  unsigned char_  b_ModulNbr,        |
|                                                  unsigned char_  b_PWM)             |
+----------------------------------------------------------------------------+
| Task              : Disable the selected PWM (b_PWM) from selected module  |
|                     (b_ModulNbr). The output signal level depend of the    |
|                     initialisation by the "i_APCI1710_EnablePWM".          |
|                     See the b_StartLevel, b_StopMode and b_StopLevel       |
|                     parameters from this function.                         |
+----------------------------------------------------------------------------+
| Input Parameters  :BYTE_ b_BoardHandle : Handle of board APCI-1710         |
|                    unsigned char_ b_ModulNbr    : Selected module number (0 to 3)   |
|                    unsigned char_ b_PWM         : Selected PWM (0 or 1)             |
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

int i_APCI1710_DisablePWM(struct comedi_device *dev, unsigned char b_ModulNbr, unsigned char b_PWM)
{
	int i_ReturnValue = 0;
	unsigned int dw_Status;

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
					}	/*  if (dw_Status & 0x1) */
					else {
						/*******************/
						/* PWM not enabled */
						/*******************/
						DPRINTK("PWM not enabled\n");
						i_ReturnValue = -6;
					}	/*  if (dw_Status & 0x1) */
				}	/*  if (dw_Status & 0x10) */
				else {
					/***********************/
					/* PWM not initialised */
					/***********************/
					DPRINTK(" PWM not initialised\n");
					i_ReturnValue = -5;
				}	/*  if (dw_Status & 0x10) */
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
			else {
				/******************************/
				/* Tor PWM selection is wrong */
				/******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
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

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_SetNewPWMTiming                       |
|                                       (unsigned char_     b_BoardHandle,            |
|                                        unsigned char_     b_ModulNbr,               |
|                                        unsigned char_     b_PWM,                    |
|                                        unsigned char_     b_ClockSelection,         |
|                                        unsigned char_     b_TimingUnit,             |
|                                        ULONG_   ul_LowTiming,              |
|                                        ULONG_   ul_HighTiming)             |
+----------------------------------------------------------------------------+
| Task              : Set a new timing. The ul_LowTiming, ul_HighTiming and  |
|                     ul_TimingUnit determine the low/high timing base for   |
|                     the period.                                            |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_     b_BoardHandle    : Handle of board APCI-1710 |
|                     unsigned char_     b_ModulNbr       : Module number to configure|
|                                                  (0 to 3)                  |
|                     unsigned char_     b_PWM            : Selected PWM (0 or 1).    |
|                     unsigned char_     b_TimingUnit     : Base timing Unit (0 to 4) |
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

int i_APCI1710_SetNewPWMTiming(struct comedi_device *dev,
	unsigned char b_ModulNbr,
	unsigned char b_PWM, unsigned char b_TimingUnit, unsigned int ul_LowTiming, unsigned int ul_HighTiming)
{
	unsigned char b_ClockSelection;
	int i_ReturnValue = 0;
	unsigned int ul_LowTimerValue = 0;
	unsigned int ul_HighTimerValue = 0;
	unsigned int ul_RealLowTiming = 0;
	unsigned int ul_RealHighTiming = 0;
	unsigned int dw_Status;
	unsigned int dw_Command;
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
										(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
										(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
										(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
										(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
										(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
										(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
										(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
										(unsigned int)
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
										(unsigned int)
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
											(unsigned int)
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
					}	/*  if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4)) */
					else {
						/**********************************/
						/* Timing unit selection is wrong */
						/**********************************/
						DPRINTK("Timing unit selection is wrong\n");
						i_ReturnValue = -6;
					}	/*  if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4)) */
				}	/*  if (dw_Status & 0x10) */
				else {
					/***********************/
					/* PWM not initialised */
					/***********************/
					DPRINTK("PWM not initialised\n");
					i_ReturnValue = -5;
				}	/*  if (dw_Status & 0x10) */
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
			else {
				/******************************/
				/* Tor PWM selection is wrong */
				/******************************/
				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
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

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_GetPWMStatus                          |
|                               (unsigned char_    b_BoardHandle,                     |
|                                unsigned char_    b_ModulNbr,                        |
|                                unsigned char_    b_PWM,                             |
|                                unsigned char *_  pb_PWMOutputStatus,                 |
|                                unsigned char *_  pb_ExternGateStatus)                |
+----------------------------------------------------------------------------+
| Task              : Return the status from selected PWM (b_PWM) from       |
|                     selected module (b_ModulNbr).                          |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_  b_BoardHandle : Handle of board APCI-1710       |
|                     unsigned char_  b_PWM         : Selected PWM (0 or 1)           |
|                     unsigned char_  b_ModulNbr    : Selected module number (0 to 3)
	b_ModulNbr			=(unsigned char)  CR_AREF(insn->chanspec);
	b_PWM				=(unsigned char)  data[0];

 |
+----------------------------------------------------------------------------+
| Output Parameters : unsigned char *_  pb_PWMOutputStatus  : Return the PWM output    |
|                                                   level status.            |
|                                                    0 : The PWM output level|
|                                                        is low.             |
|                                                    1 : The PWM output level|
|                                                        is high.            |
|                     unsigned char *_  pb_ExternGateStatus : Return the extern gate   |
|                                                   level status.            |
|                                                    0 : The extern gate is  |
|                                                        low.                |
|                                                    1 : The extern gate is  |
|                                                        high.
    pb_PWMOutputStatus	=(unsigned char *) data[0];
	pb_ExternGateStatus =(unsigned char *) data[1];             |
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

int i_APCI1710_InsnReadGetPWMStatus(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = 0;
	unsigned int dw_Status;

	unsigned char b_ModulNbr;
	unsigned char b_PWM;
	unsigned char *pb_PWMOutputStatus;
	unsigned char *pb_ExternGateStatus;

	i_ReturnValue = insn->n;
	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_PWM = (unsigned char) CR_CHAN(insn->chanspec);
	pb_PWMOutputStatus = (unsigned char *) &data[0];
	pb_ExternGateStatus = (unsigned char *) &data[1];

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
							(unsigned char) ((dw_Status >> 7)
							& 1);
						*pb_ExternGateStatus =
							(unsigned char) ((dw_Status >> 6)
							& 1);
					}	/*  if (dw_Status & 0x1) */
					else {
						/*******************/
						/* PWM not enabled */
						/*******************/

						DPRINTK("PWM not enabled \n");
						i_ReturnValue = -6;
					}	/*  if (dw_Status & 0x1) */
				}	/*  if (dw_Status & 0x10) */
				else {
					/***********************/
					/* PWM not initialised */
					/***********************/

					DPRINTK("PWM not initialised\n");
					i_ReturnValue = -5;
				}	/*  if (dw_Status & 0x10) */
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
			else {
				/******************************/
				/* Tor PWM selection is wrong */
				/******************************/

				DPRINTK("Tor PWM selection is wrong\n");
				i_ReturnValue = -4;
			}	/*  if (b_PWM >= 0 && b_PWM <= 1) */
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

	return i_ReturnValue;
}

int i_APCI1710_InsnBitsReadPWMInterrupt(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
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
