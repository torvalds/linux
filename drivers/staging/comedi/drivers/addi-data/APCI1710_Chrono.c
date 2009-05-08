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
  | Module name : CHRONO.C        | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date     :  02/12/2002                |
  +-----------------------------------------------------------------------+
  | Description :   APCI-1710 chronometer module                          |
  |                                                                       |
  |                                                                       |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  | 29/06/98 | S. Weber  | Digital input / output implementation          |
  |----------|-----------|------------------------------------------------|
  | 08/05/00 | Guinot C  | - 0400/0228 All Function in RING 0             |
  |          |           |   available                                    |
  +-----------------------------------------------------------------------+
  |          |           |                                                |
  |          |           |                                                |
  +-----------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
|                               Included files                               |
+----------------------------------------------------------------------------+
*/
#include "APCI1710_Chrono.h"

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_InitChrono                        |
|                                       (BYTE_     b_BoardHandle,            |
|                                        BYTE_     b_ModulNbr,               |
|                                        BYTE_     b_ChronoMode,             |
|                                        BYTE_     b_PCIInputClock,          |
|                                        BYTE_     b_TimingUnit,             |
|                                        ULONG_   ul_TimingInterval,         |
|                                        PULONG_ pul_RealTimingInterval)

+----------------------------------------------------------------------------+
| Task              : Configure the chronometer operating mode (b_ChronoMode)|
|                     from selected module (b_ModulNbr).                     |
|                     The ul_TimingInterval and ul_TimingUnit determine the  |
|                     timing base for the measurement.                       |
|                     The pul_RealTimingInterval return the real timing      |
|                     value. You must calling this function be for you call  |
|                     any other function witch access of the chronometer.    |
|                                                                            |
|                     Witch this functionality from the APCI-1710 you have   |
|                     the possibility to measure the timing witch two event. |
|                                                                            |
|                     The mode 0 and 1 is appropriate for period measurement.|
|                     The mode 2 and 3 is appropriate for frequent           |
|                     measurement.                                           |
|                     The mode 4 to 7 is appropriate for measuring the timing|
|                     between  two event.                                    |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_   b_BoardHandle    : Handle of board APCI-1710   |
| BYTE_   b_ModulNbr  CR_AREF(insn->chanspec)  : Module number to configure  |
|                                                (0 to 3)                    |
| BYTE_   b_ChronoMode				data[0]    : Chronometer action mode     |
|                                                (0 to 7).                   |
| BYTE_   b_PCIInputClock			data[1] : Selection from PCI bus clock|
|                                                - APCI1710_30MHZ :          |
|                                                  The PC have a PCI bus     |
|                                                  clock from 30 MHz         |
|                                                - APCI1710_33MHZ :          |
|                                                  The PC have a PCI bus     |
|                                                  clock from 33 MHz         |
|                                                - APCI1710_40MHZ            |
|                                                  The APCI-1710 have a      |
|                                                  integrated 40Mhz          |
|                                                  quartz.                   |
|               BYTE_   b_TimingUnit	data[2]    : Base timing unity (0 to 4) |
|                                                 0 : ns                     |
|                                                 1 : µs                     |
|                                                 2 : ms                     |
|                                                 3 : s                      |
|                                                 4 : mn                     |
|         ULONG_ ul_TimingInterval : data[3]	 Base timing value.          |
+----------------------------------------------------------------------------+
| Output Parameters : PULONG_  pul_RealTimingInterval : Real  base timing    |
|                                                       value.
|                     data[0]
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a Chronometer module             |
|                     -4: Chronometer mode selection is wrong                |
|                     -5: The selected PCI input clock is wrong              |
|                     -6: Timing unity selection is wrong                    |
|                     -7: Base timing selection is wrong                     |
|                     -8: You can not used the 40MHz clock selection wich    |
|                         this board                                         |
|                     -9: You can not used the 40MHz clock selection wich    |
|                         this CHRONOS version                               |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnConfigInitChrono(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	INT i_ReturnValue = 0;
	ULONG ul_TimerValue = 0;
	ULONG ul_TimingInterval = 0;
	ULONG ul_RealTimingInterval = 0;
	double d_RealTimingInterval = 0;
	DWORD dw_ModeArray[8] =
		{ 0x01, 0x05, 0x00, 0x04, 0x02, 0x0E, 0x0A, 0x06 };
	BYTE b_ModulNbr, b_ChronoMode, b_PCIInputClock, b_TimingUnit;

	b_ModulNbr = CR_AREF(insn->chanspec);
	b_ChronoMode = (BYTE) data[0];
	b_PCIInputClock = (BYTE) data[1];
	b_TimingUnit = (BYTE) data[2];
	ul_TimingInterval = (ULONG) data[3];
	i_ReturnValue = insn->n;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if chronometer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_CHRONOMETER) {
	      /*****************************/
			/* Test the chronometer mode */
	      /*****************************/

			if (b_ChronoMode <= 7) {
		 /**************************/
				/* Test the PCI bus clock */
		 /**************************/

				if ((b_PCIInputClock == APCI1710_30MHZ) ||
					(b_PCIInputClock == APCI1710_33MHZ) ||
					(b_PCIInputClock == APCI1710_40MHZ)) {
		    /*************************/
					/* Test the timing unity */
		    /*************************/

					if (b_TimingUnit <= 4) {
		       /**********************************/
						/* Test the base timing selection */
		       /**********************************/

						if (((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 0) && (ul_TimingInterval >= 66) && (ul_TimingInterval <= 0xFFFFFFFFUL)) || ((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 1) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 143165576UL)) || ((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 2) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 143165UL)) || ((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 3) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 143UL)) || ((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 4) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 2UL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 0) && (ul_TimingInterval >= 60) && (ul_TimingInterval <= 0xFFFFFFFFUL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 1) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 130150240UL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 2) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 130150UL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 3) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 130UL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 4) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 2UL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 0) && (ul_TimingInterval >= 50) && (ul_TimingInterval <= 0xFFFFFFFFUL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 1) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 107374182UL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 2) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 107374UL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 3) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 107UL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 4) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 1UL))) {
			  /**************************/
							/* Test the board version */
			  /**************************/

							if (((b_PCIInputClock == APCI1710_40MHZ) && (devpriv->s_BoardInfos.b_BoardVersion > 0)) || (b_PCIInputClock != APCI1710_40MHZ)) {
			     /************************/
								/* Test the TOR version */
			     /************************/

								if (((b_PCIInputClock == APCI1710_40MHZ) && ((devpriv->s_BoardInfos.dw_MolduleConfiguration[b_ModulNbr] & 0xFFFF) >= 0x3131)) || (b_PCIInputClock != APCI1710_40MHZ)) {
									fpu_begin
										();

				/****************************************/
									/* Calculate the timer 0 division fator */
				/****************************************/

									switch (b_TimingUnit) {
				   /******/
										/* ns */
				   /******/

									case 0:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_TimerValue
											=
											(ULONG)
											(ul_TimingInterval
											*
											(0.001 * b_PCIInputClock));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_TimingInterval * (0.001 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
											ul_TimerValue
												=
												ul_TimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										ul_RealTimingInterval
											=
											(ULONG)
											(ul_TimerValue
											/
											(0.001 * (double)b_PCIInputClock));
										d_RealTimingInterval
											=
											(double)
											ul_TimerValue
											/
											(0.001
											*
											(double)
											b_PCIInputClock);

										if ((double)((double)ul_TimerValue / (0.001 * (double)b_PCIInputClock)) >= (double)((double)ul_RealTimingInterval + 0.5)) {
											ul_RealTimingInterval
												=
												ul_RealTimingInterval
												+
												1;
										}

										ul_TimingInterval
											=
											ul_TimingInterval
											-
											1;
										ul_TimerValue
											=
											ul_TimerValue
											-
											2;
										if (b_PCIInputClock != APCI1710_40MHZ) {
											ul_TimerValue
												=
												(ULONG)
												(
												(double)
												(ul_TimerValue)
												*
												0.99392);
										}

										break;

				   /******/
										/* æs */
				   /******/

									case 1:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_TimerValue
											=
											(ULONG)
											(ul_TimingInterval
											*
											(1.0 * b_PCIInputClock));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_TimingInterval * (1.0 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
											ul_TimerValue
												=
												ul_TimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										ul_RealTimingInterval
											=
											(ULONG)
											(ul_TimerValue
											/
											(1.0 * (double)b_PCIInputClock));
										d_RealTimingInterval
											=
											(double)
											ul_TimerValue
											/
											(
											(double)
											1.0
											*
											(double)
											b_PCIInputClock);

										if ((double)((double)ul_TimerValue / (1.0 * (double)b_PCIInputClock)) >= (double)((double)ul_RealTimingInterval + 0.5)) {
											ul_RealTimingInterval
												=
												ul_RealTimingInterval
												+
												1;
										}

										ul_TimingInterval
											=
											ul_TimingInterval
											-
											1;
										ul_TimerValue
											=
											ul_TimerValue
											-
											2;
										if (b_PCIInputClock != APCI1710_40MHZ) {
											ul_TimerValue
												=
												(ULONG)
												(
												(double)
												(ul_TimerValue)
												*
												0.99392);
										}

										break;

				   /******/
										/* ms */
				   /******/

									case 2:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_TimerValue
											=
											ul_TimingInterval
											*
											(1000
											*
											b_PCIInputClock);

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_TimingInterval * (1000.0 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
											ul_TimerValue
												=
												ul_TimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										ul_RealTimingInterval
											=
											(ULONG)
											(ul_TimerValue
											/
											(1000.0 * (double)b_PCIInputClock));
										d_RealTimingInterval
											=
											(double)
											ul_TimerValue
											/
											(1000.0
											*
											(double)
											b_PCIInputClock);

										if ((double)((double)ul_TimerValue / (1000.0 * (double)b_PCIInputClock)) >= (double)((double)ul_RealTimingInterval + 0.5)) {
											ul_RealTimingInterval
												=
												ul_RealTimingInterval
												+
												1;
										}

										ul_TimingInterval
											=
											ul_TimingInterval
											-
											1;
										ul_TimerValue
											=
											ul_TimerValue
											-
											2;
										if (b_PCIInputClock != APCI1710_40MHZ) {
											ul_TimerValue
												=
												(ULONG)
												(
												(double)
												(ul_TimerValue)
												*
												0.99392);
										}

										break;

				   /*****/
										/* s */
				   /*****/

									case 3:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_TimerValue
											=
											(ULONG)
											(ul_TimingInterval
											*
											(1000000.0
												*
												b_PCIInputClock));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)ul_TimingInterval * (1000000.0 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
											ul_TimerValue
												=
												ul_TimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										ul_RealTimingInterval
											=
											(ULONG)
											(ul_TimerValue
											/
											(1000000.0
												*
												(double)
												b_PCIInputClock));
										d_RealTimingInterval
											=
											(double)
											ul_TimerValue
											/
											(1000000.0
											*
											(double)
											b_PCIInputClock);

										if ((double)((double)ul_TimerValue / (1000000.0 * (double)b_PCIInputClock)) >= (double)((double)ul_RealTimingInterval + 0.5)) {
											ul_RealTimingInterval
												=
												ul_RealTimingInterval
												+
												1;
										}

										ul_TimingInterval
											=
											ul_TimingInterval
											-
											1;
										ul_TimerValue
											=
											ul_TimerValue
											-
											2;
										if (b_PCIInputClock != APCI1710_40MHZ) {
											ul_TimerValue
												=
												(ULONG)
												(
												(double)
												(ul_TimerValue)
												*
												0.99392);
										}

										break;

				   /******/
										/* mn */
				   /******/

									case 4:

					   /******************/
										/* Timer 0 factor */
					   /******************/

										ul_TimerValue
											=
											(ULONG)
											(
											(ul_TimingInterval
												*
												60)
											*
											(1000000.0
												*
												b_PCIInputClock));

					   /*******************/
										/* Round the value */
					   /*******************/

										if ((double)((double)(ul_TimingInterval * 60.0) * (1000000.0 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
											ul_TimerValue
												=
												ul_TimerValue
												+
												1;
										}

					   /*****************************/
										/* Calculate the real timing */
					   /*****************************/

										ul_RealTimingInterval
											=
											(ULONG)
											(ul_TimerValue
											/
											(1000000.0
												*
												(double)
												b_PCIInputClock))
											/
											60;
										d_RealTimingInterval
											=
											(
											(double)
											ul_TimerValue
											/
											(0.001 * (double)b_PCIInputClock)) / 60.0;

										if ((double)(((double)ul_TimerValue / (1000000.0 * (double)b_PCIInputClock)) / 60.0) >= (double)((double)ul_RealTimingInterval + 0.5)) {
											ul_RealTimingInterval
												=
												ul_RealTimingInterval
												+
												1;
										}

										ul_TimingInterval
											=
											ul_TimingInterval
											-
											1;
										ul_TimerValue
											=
											ul_TimerValue
											-
											2;
										if (b_PCIInputClock != APCI1710_40MHZ) {
											ul_TimerValue
												=
												(ULONG)
												(
												(double)
												(ul_TimerValue)
												*
												0.99392);
										}

										break;
									}

									fpu_end();

				/****************************/
									/* Save the PCI input clock */
				/****************************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_ChronoModuleInfo.
										b_PCIInputClock
										=
										b_PCIInputClock;

				/*************************/
									/* Save the timing unity */
				/*************************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_ChronoModuleInfo.
										b_TimingUnit
										=
										b_TimingUnit;

				/************************/
									/* Save the base timing */
				/************************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_ChronoModuleInfo.
										d_TimingInterval
										=
										d_RealTimingInterval;

				/****************************/
									/* Set the chronometer mode */
				/****************************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_ChronoModuleInfo.
										dw_ConfigReg
										=
										dw_ModeArray
										[b_ChronoMode];

				/***********************/
									/* Test if 40 MHz used */
				/***********************/

									if (b_PCIInputClock == APCI1710_40MHZ) {
										devpriv->
											s_ModuleInfo
											[b_ModulNbr].
											s_ChronoModuleInfo.
											dw_ConfigReg
											=
											devpriv->
											s_ModuleInfo
											[b_ModulNbr].
											s_ChronoModuleInfo.
											dw_ConfigReg
											|
											0x80;
									}

									outl(devpriv->s_ModuleInfo[b_ModulNbr].s_ChronoModuleInfo.dw_ConfigReg, devpriv->s_BoardInfos.ui_Address + 16 + (64 * b_ModulNbr));

				/***********************/
									/* Write timer 0 value */
				/***********************/

									outl(ul_TimerValue, devpriv->s_BoardInfos.ui_Address + (64 * b_ModulNbr));

				/*********************/
									/* Chronometer init. */
				/*********************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_ChronoModuleInfo.
										b_ChronoInit
										=
										1;
								} else {
				/***********************************************/
									/* TOR version error for 40MHz clock selection */
				/***********************************************/

									DPRINTK("TOR version error for 40MHz clock selection\n");
									i_ReturnValue
										=
										-9;
								}
							} else {
			     /**************************************************************/
								/* You can not used the 40MHz clock selection wich this board */
			     /**************************************************************/

								DPRINTK("You can not used the 40MHz clock selection wich this board\n");
								i_ReturnValue =
									-8;
							}
						} else {
			  /**********************************/
							/* Base timing selection is wrong */
			  /**********************************/

							DPRINTK("Base timing selection is wrong\n");
							i_ReturnValue = -7;
						}
					}	// if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4))
					else {
		       /***********************************/
						/* Timing unity selection is wrong */
		       /***********************************/

						DPRINTK("Timing unity selection is wrong\n");
						i_ReturnValue = -6;
					}	// if ((b_TimingUnit >= 0) && (b_TimingUnit <= 4))
				}	// if ((b_PCIInputClock == APCI1710_30MHZ) || (b_PCIInputClock == APCI1710_33MHZ))
				else {
		    /*****************************************/
					/* The selected PCI input clock is wrong */
		    /*****************************************/

					DPRINTK("The selected PCI input clock is wrong\n");
					i_ReturnValue = -5;
				}	// if ((b_PCIInputClock == APCI1710_30MHZ) || (b_PCIInputClock == APCI1710_33MHZ))
			}	// if (b_ChronoMode >= 0 && b_ChronoMode <= 7)
			else {
		 /***************************************/
				/* Chronometer mode selection is wrong */
		 /***************************************/

				DPRINTK("Chronometer mode selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_ChronoMode >= 0 && b_ChronoMode <= 7)
		} else {
	      /******************************************/
			/* The module is not a Chronometer module */
	      /******************************************/

			DPRINTK("The module is not a Chronometer module\n");
			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/

		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}
	data[0] = ul_RealTimingInterval;
	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_EnableChrono                          |
|                                               (BYTE_ b_BoardHandle,        |
|                                                BYTE_ b_ModulNbr,           |
|                                                BYTE_ b_CycleMode,          |
|                                                BYTE_ b_InterruptEnable)
INT i_APCI1710_InsnWriteEnableDisableChrono(struct comedi_device *dev,
struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)						 |
+----------------------------------------------------------------------------+
| Task              : Enable the chronometer from selected module            |
|                     (b_ModulNbr). You must calling the                     |
|                     "i_APCI1710_InitChrono" function be for you call this  |
|                     function.                                              |
|                     If you enable the chronometer interrupt, the           |
|                     chronometer generate a interrupt after the stop signal.|
|                     See function "i_APCI1710_SetBoardIntRoutineX" and the  |
|                     Interrupt mask description chapter from this manual.   |
|                     The b_CycleMode parameter determine if you will        |
|                     measured a single or more cycle.

|					  Disable the chronometer from selected module           |
|                     (b_ModulNbr). If you disable the chronometer after a   |
|                     start signal occur and you restart the chronometer     |
|                     witch the " i_APCI1710_EnableChrono" function, if no   |
|                     stop signal occur this start signal is ignored.
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_ b_BoardHandle  : Handle of board APCI-1710       |
|                     BYTE_ b_ModulNbr   CR_AREF(chanspec)  : Selected module number (0 to 3) |
                                  data[0]  ENABle/Disable chrono
|                     BYTE_ b_CycleMode    : Selected the chronometer        |
|                                  data[1]           acquisition mode                |
|                     BYTE_ b_InterruptEnable : Enable or disable the        |
|                                   data[2]            chronometer interrupt.       |
|                                               APCI1710_ENABLE:             |
|                                               Enable the chronometer       |
|                                               interrupt                    |
|                                               APCI1710_DISABLE:            |
|                                               Disable the chronometer      |
|                                               interrupt                    |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a Chronometer module             |
|                     -4: Chronometer not initialised see function           |
|                         "i_APCI1710_InitChrono"                            |
|                     -5: Chronometer acquisition mode cycle is wrong        |
|                     -6: Interrupt parameter is wrong                       |
|                     -7: Interrupt function not initialised.                |
|                         See function "i_APCI1710_SetBoardIntRoutineX"
                      -8: data[0] wrong input    |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnWriteEnableDisableChrono(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	INT i_ReturnValue = 0;
	BYTE b_ModulNbr, b_CycleMode, b_InterruptEnable, b_Action;
	b_ModulNbr = CR_AREF(insn->chanspec);
	b_Action = (BYTE) data[0];
	b_CycleMode = (BYTE) data[1];
	b_InterruptEnable = (BYTE) data[2];
	i_ReturnValue = insn->n;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if chronometer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_CHRONOMETER) {
	      /***********************************/
			/* Test if chronometer initialised */
	      /***********************************/

			if (devpriv->s_ModuleInfo[b_ModulNbr].
				s_ChronoModuleInfo.b_ChronoInit == 1) {

				switch (b_Action) {

				case APCI1710_ENABLE:

		 /*********************************/
					/* Test the cycle mode parameter */
		 /*********************************/

					if ((b_CycleMode == APCI1710_SINGLE)
						|| (b_CycleMode ==
							APCI1710_CONTINUOUS)) {
		    /***************************/
						/* Test the interrupt flag */
		    /***************************/

						if ((b_InterruptEnable ==
								APCI1710_ENABLE)
							|| (b_InterruptEnable ==
								APCI1710_DISABLE))
						{

			  /***************************/
							/* Save the interrupt flag */
			  /***************************/

							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_ChronoModuleInfo.
								b_InterruptMask
								=
								b_InterruptEnable;

			  /***********************/
							/* Save the cycle mode */
			  /***********************/

							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_ChronoModuleInfo.
								b_CycleMode =
								b_CycleMode;

							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_ChronoModuleInfo.
								dw_ConfigReg =
								(devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_ChronoModuleInfo.
								dw_ConfigReg &
								0x8F) | ((1 &
									b_InterruptEnable)
								<< 5) | ((1 &
									b_CycleMode)
								<< 6) | 0x10;

			  /*****************************/
							/* Test if interrupt enabled */
			  /*****************************/

							if (b_InterruptEnable ==
								APCI1710_ENABLE)
							{
			     /****************************/
								/* Clear the interrupt flag */
			     /****************************/

								outl(devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_ChronoModuleInfo.
									dw_ConfigReg,
									devpriv->
									s_BoardInfos.
									ui_Address
									+ 32 +
									(64 * b_ModulNbr));
								devpriv->tsk_Current = current;	// Save the current process task structure
							}

			  /***********************************/
							/* Enable or disable the interrupt */
							/* Enable the chronometer          */
			  /***********************************/

							outl(devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_ChronoModuleInfo.
								dw_ConfigReg,
								devpriv->
								s_BoardInfos.
								ui_Address +
								16 +
								(64 * b_ModulNbr));

			  /*************************/
							/* Clear status register */
			  /*************************/

							outl(0, devpriv->
								s_BoardInfos.
								ui_Address +
								36 +
								(64 * b_ModulNbr));

						}	// if ((b_InterruptEnable == APCI1710_ENABLE) || (b_InterruptEnable == APCI1710_DISABLE))
						else {
		       /********************************/
							/* Interrupt parameter is wrong */
		       /********************************/

							DPRINTK("Interrupt parameter is wrong\n");
							i_ReturnValue = -6;
						}	// if ((b_InterruptEnable == APCI1710_ENABLE) || (b_InterruptEnable == APCI1710_DISABLE))
					}	// if ((b_CycleMode == APCI1710_SINGLE) || (b_CycleMode == APCI1710_CONTINUOUS))
					else {
		    /***********************************************/
						/* Chronometer acquisition mode cycle is wrong */
		    /***********************************************/

						DPRINTK("Chronometer acquisition mode cycle is wrong\n");
						i_ReturnValue = -5;
					}	// if ((b_CycleMode == APCI1710_SINGLE) || (b_CycleMode == APCI1710_CONTINUOUS))
					break;

				case APCI1710_DISABLE:

					devpriv->s_ModuleInfo[b_ModulNbr].
						s_ChronoModuleInfo.
						b_InterruptMask = 0;

					devpriv->s_ModuleInfo[b_ModulNbr].
						s_ChronoModuleInfo.
						dw_ConfigReg =
						devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_ChronoModuleInfo.
						dw_ConfigReg & 0x2F;

		 /***************************/
					/* Disable the interrupt   */
					/* Disable the chronometer */
		 /***************************/

					outl(devpriv->s_ModuleInfo[b_ModulNbr].
						s_ChronoModuleInfo.dw_ConfigReg,
						devpriv->s_BoardInfos.
						ui_Address + 16 +
						(64 * b_ModulNbr));

		 /***************************/
					/* Test if continuous mode */
		 /***************************/

					if (devpriv->s_ModuleInfo[b_ModulNbr].
						s_ChronoModuleInfo.
						b_CycleMode ==
						APCI1710_CONTINUOUS) {
		    /*************************/
						/* Clear status register */
		    /*************************/

						outl(0, devpriv->s_BoardInfos.
							ui_Address + 36 +
							(64 * b_ModulNbr));
					}
					break;

				default:
					DPRINTK("Inputs wrong! Enable or Disable chrono\n");
					i_ReturnValue = -8;
				}	// switch ENABLE/DISABLE
			} else {
		 /*******************************/
				/* Chronometer not initialised */
		 /*******************************/

				DPRINTK("Chronometer not initialised\n");
				i_ReturnValue = -4;
			}
		} else {
	      /******************************************/
			/* The module is not a Chronometer module */
	      /******************************************/

			DPRINTK("The module is not a Chronometer module\n");
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
| Function Name     :INT	i_APCI1710_InsnReadChrono(struct comedi_device *dev,struct comedi_subdevice *s,
struct comedi_insn *insn,unsigned int *data)                   |
+----------------------------------------------------------------------------+
| Task              : Read  functions for Timer                                     |
+----------------------------------------------------------------------------+
| Input Parameters  :
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnReadChrono(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	BYTE b_ReadType;
	INT i_ReturnValue = insn->n;

	b_ReadType = CR_CHAN(insn->chanspec);

	switch (b_ReadType) {
	case APCI1710_CHRONO_PROGRESS_STATUS:
		i_ReturnValue = i_APCI1710_GetChronoProgressStatus(dev,
			(BYTE) CR_AREF(insn->chanspec), (PBYTE) & data[0]);
		break;

	case APCI1710_CHRONO_READVALUE:
		i_ReturnValue = i_APCI1710_ReadChronoValue(dev,
			(BYTE) CR_AREF(insn->chanspec),
			(UINT) insn->unused[0],
			(PBYTE) & data[0], (PULONG) & data[1]);
		break;

	case APCI1710_CHRONO_CONVERTVALUE:
		i_ReturnValue = i_APCI1710_ConvertChronoValue(dev,
			(BYTE) CR_AREF(insn->chanspec),
			(ULONG) insn->unused[0],
			(PULONG) & data[0],
			(PBYTE) & data[1],
			(PBYTE) & data[2],
			(PUINT) & data[3],
			(PUINT) & data[4], (PUINT) & data[5]);
		break;

	case APCI1710_CHRONO_READINTERRUPT:
		printk("In Chrono Read Interrupt\n");

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
			s_InterruptParameters.
			ui_Read + 1) % APCI1710_SAVE_INTERRUPT;
		break;

	default:
		printk("ReadType Parameter wrong\n");
	}

	if (i_ReturnValue >= 0)
		i_ReturnValue = insn->n;
	return (i_ReturnValue);

}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_GetChronoProgressStatus               |
|                               (BYTE_    b_BoardHandle,                     |
|                                BYTE_    b_ModulNbr,                        |
|                                PBYTE_  pb_ChronoStatus)                    |
+----------------------------------------------------------------------------+
| Task              : Return the chronometer status (pb_ChronoStatus) from   |
|                     selected chronometer module (b_ModulNbr).              |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_ b_BoardHandle  : Handle of board APCI-1710       |
|                     BYTE_ b_ModulNbr     : Selected module number (0 to 3) |
+----------------------------------------------------------------------------+
| Output Parameters : PULONG_  pb_ChronoStatus : Return the chronometer      |
|                                                status.                     |
|                                                0 : Measurement not started.|
|                                                    No start signal occur.  |
|                                                1 : Measurement started.    |
|                                                    A start signal occur.   |
|                                                2 : Measurement stopped.    |
|                                                    A stop signal occur.    |
|                                                    The measurement is      |
|                                                    terminate.              |
|                                                3: A overflow occur. You    |
|                                                   must change the base     |
|                                                   timing witch the         |
|                                                   function                 |
|                                                   "i_APCI1710_InitChrono"  |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a Chronometer module             |
|                     -4: Chronometer not initialised see function           |
|                         "i_APCI1710_InitChrono"                            |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_GetChronoProgressStatus(struct comedi_device * dev,
	BYTE b_ModulNbr, PBYTE pb_ChronoStatus)
{
	INT i_ReturnValue = 0;
	DWORD dw_Status;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if chronometer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_CHRONOMETER) {
	      /***********************************/
			/* Test if chronometer initialised */
	      /***********************************/

			if (devpriv->
				s_ModuleInfo[b_ModulNbr].
				s_ChronoModuleInfo.b_ChronoInit == 1) {

				dw_Status = inl(devpriv->s_BoardInfos.
					ui_Address + 8 + (64 * b_ModulNbr));

		 /********************/
				/* Test if overflow */
		 /********************/

				if ((dw_Status & 8) == 8) {
		    /******************/
					/* Overflow occur */
		    /******************/

					*pb_ChronoStatus = 3;
				}	// if ((dw_Status & 8) == 8)
				else {
		    /*******************************/
					/* Test if measurement stopped */
		    /*******************************/

					if ((dw_Status & 2) == 2) {
		       /***********************/
						/* A stop signal occur */
		       /***********************/

						*pb_ChronoStatus = 2;
					}	// if ((dw_Status & 2) == 2)
					else {
		       /*******************************/
						/* Test if measurement started */
		       /*******************************/

						if ((dw_Status & 1) == 1) {
			  /************************/
							/* A start signal occur */
			  /************************/

							*pb_ChronoStatus = 1;
						}	// if ((dw_Status & 1) == 1)
						else {
			  /***************************/
							/* Measurement not started */
			  /***************************/

							*pb_ChronoStatus = 0;
						}	// if ((dw_Status & 1) == 1)
					}	// if ((dw_Status & 2) == 2)
				}	// if ((dw_Status & 8) == 8)
			} else {
		 /*******************************/
				/* Chronometer not initialised */
		 /*******************************/
				DPRINTK("Chronometer not initialised\n");
				i_ReturnValue = -4;
			}
		} else {
	      /******************************************/
			/* The module is not a Chronometer module */
	      /******************************************/
			DPRINTK("The module is not a Chronometer module\n");
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
| Function Name     : _INT_ i_APCI1710_ReadChronoValue                       |
|                               (BYTE_     b_BoardHandle,                    |
|                                BYTE_     b_ModulNbr,                       |
|                                UINT_    ui_TimeOut,                        |
|                                PBYTE_   pb_ChronoStatus,                   |
|                                PULONG_ pul_ChronoValue)                    |
+----------------------------------------------------------------------------+
| Task              : Return the chronometer status (pb_ChronoStatus) and the|
|                     timing value (pul_ChronoValue) after a stop signal     |
|                     occur from selected chronometer module (b_ModulNbr).   |
|                     This function are only avaible if you have disabled    |
|                     the interrupt functionality. See function              |
|                     "i_APCI1710_EnableChrono" and the Interrupt mask       |
|                     description chapter.                                   |
|                     You can test the chronometer status witch the          |
|                     "i_APCI1710_GetChronoProgressStatus" function.         |
|                                                                            |
|                     The returned value from pul_ChronoValue parameter is   |
|                     not real measured timing.                              |
|                     You must used the "i_APCI1710_ConvertChronoValue"      |
|                     function or make this operation for calculate the      |
|                     timing:                                                |
|                                                                            |
|                     Timing = pul_ChronoValue * pul_RealTimingInterval.     |
|                                                                            |
|                     pul_RealTimingInterval is the returned parameter from  |
|                     "i_APCI1710_InitChrono" function and the time unity is |
|                     the b_TimingUnit from "i_APCI1710_InitChrono" function|
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_ b_BoardHandle  : Handle of board APCI-1710       |
|                     BYTE_ b_ModulNbr     : Selected module number (0 to 3) |
+----------------------------------------------------------------------------+
| Output Parameters : PULONG_  pb_ChronoStatus : Return the chronometer      |
|                                                status.                     |
|                                                0 : Measurement not started.|
|                                                    No start signal occur.  |
|                                                1 : Measurement started.    |
|                                                    A start signal occur.   |
|                                                2 : Measurement stopped.    |
|                                                    A stop signal occur.    |
|                                                    The measurement is      |
|                                                    terminate.              |
|                                                3: A overflow occur. You    |
|                                                   must change the base     |
|                                                   timing witch the         |
|                                                   function                 |
|                                                   "i_APCI1710_InitChrono"  |
|                     PULONG  pul_ChronoValue  : Chronometer timing value.   |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a Chronometer module             |
|                     -4: Chronometer not initialised see function           |
|                         "i_APCI1710_InitChrono"                            |
|                     -5: Timeout parameter is wrong (0 to 65535)            |
|                     -6: Interrupt routine installed. You can not read      |
|                         directly the chronometer measured timing.          |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_ReadChronoValue(struct comedi_device * dev,
	BYTE b_ModulNbr,
	UINT ui_TimeOut, PBYTE pb_ChronoStatus, PULONG pul_ChronoValue)
{
	INT i_ReturnValue = 0;
	DWORD dw_Status;
	DWORD dw_TimeOut = 0;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if chronometer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_CHRONOMETER) {
	      /***********************************/
			/* Test if chronometer initialised */
	      /***********************************/

			if (devpriv->
				s_ModuleInfo[b_ModulNbr].
				s_ChronoModuleInfo.b_ChronoInit == 1) {
		 /*****************************/
				/* Test the timout parameter */
		 /*****************************/

				if ((ui_TimeOut >= 0)
					&& (ui_TimeOut <= 65535UL)) {

					for (;;) {
			  /*******************/
						/* Read the status */
			  /*******************/

						dw_Status =
							inl(devpriv->
							s_BoardInfos.
							ui_Address + 8 +
							(64 * b_ModulNbr));

			  /********************/
						/* Test if overflow */
			  /********************/

						if ((dw_Status & 8) == 8) {
			     /******************/
							/* Overflow occur */
			     /******************/

							*pb_ChronoStatus = 3;

			     /***************************/
							/* Test if continuous mode */
			     /***************************/

							if (devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_ChronoModuleInfo.
								b_CycleMode ==
								APCI1710_CONTINUOUS)
							{
				/*************************/
								/* Clear status register */
				/*************************/

								outl(0, devpriv->s_BoardInfos.ui_Address + 36 + (64 * b_ModulNbr));
							}

							break;
						}	// if ((dw_Status & 8) == 8)
						else {
			     /*******************************/
							/* Test if measurement stopped */
			     /*******************************/

							if ((dw_Status & 2) ==
								2) {
				/***********************/
								/* A stop signal occur */
				/***********************/

								*pb_ChronoStatus
									= 2;

				/***************************/
								/* Test if continnous mode */
				/***************************/

								if (devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_ChronoModuleInfo.
									b_CycleMode
									==
									APCI1710_CONTINUOUS)
								{
				   /*************************/
									/* Clear status register */
				   /*************************/

									outl(0, devpriv->s_BoardInfos.ui_Address + 36 + (64 * b_ModulNbr));
								}
								break;
							}	// if ((dw_Status & 2) == 2)
							else {
				/*******************************/
								/* Test if measurement started */
				/*******************************/

								if ((dw_Status & 1) == 1) {
				   /************************/
									/* A start signal occur */
				   /************************/

									*pb_ChronoStatus
										=
										1;
								}	// if ((dw_Status & 1) == 1)
								else {
				   /***************************/
									/* Measurement not started */
				   /***************************/

									*pb_ChronoStatus
										=
										0;
								}	// if ((dw_Status & 1) == 1)
							}	// if ((dw_Status & 2) == 2)
						}	// if ((dw_Status & 8) == 8)

						if (dw_TimeOut == ui_TimeOut) {
			     /*****************/
							/* Timeout occur */
			     /*****************/

							break;
						} else {
			     /*************************/
							/* Increment the timeout */
			     /*************************/

							dw_TimeOut =
								dw_TimeOut + 1;
							mdelay(1000);

						}
					}	// for (;;)

		       /*****************************/
					/* Test if stop signal occur */
		       /*****************************/

					if (*pb_ChronoStatus == 2) {
			  /**********************************/
						/* Read the measured timing value */
			  /**********************************/

						*pul_ChronoValue =
							inl(devpriv->
							s_BoardInfos.
							ui_Address + 4 +
							(64 * b_ModulNbr));

						if (*pul_ChronoValue != 0) {
							*pul_ChronoValue =
								*pul_ChronoValue
								- 1;
						}
					} else {
			  /*************************/
						/* Test if timeout occur */
			  /*************************/

						if ((*pb_ChronoStatus != 3)
							&& (dw_TimeOut ==
								ui_TimeOut)
							&& (ui_TimeOut != 0)) {
			     /*****************/
							/* Timeout occur */
			     /*****************/

							*pb_ChronoStatus = 4;
						}
					}

				} else {
		    /******************************/
					/* Timeout parameter is wrong */
		    /******************************/
					DPRINTK("Timeout parameter is wrong\n");
					i_ReturnValue = -5;
				}
			} else {
		 /*******************************/
				/* Chronometer not initialised */
		 /*******************************/
				DPRINTK("Chronometer not initialised\n");
				i_ReturnValue = -4;
			}
		} else {
	      /******************************************/
			/* The module is not a Chronometer module */
	      /******************************************/
			DPRINTK("The module is not a Chronometer module\n");
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
| Function Name     : _INT_ i_APCI1710_ConvertChronoValue                    |
|                               (BYTE_     b_BoardHandle,                    |
|                                BYTE_     b_ModulNbr,                       |
|                                ULONG_   ul_ChronoValue,                    |
|                                PULONG_ pul_Hour,                           |
|                                PBYTE_   pb_Minute,                         |
|                                PBYTE_   pb_Second,                         |
|                                PUINT_  pui_MilliSecond,                    |
|                                PUINT_  pui_MicroSecond,                    |
|                                PUINT_  pui_NanoSecond)                     |
+----------------------------------------------------------------------------+
| Task              : Convert the chronometer measured timing                |
|                     (ul_ChronoValue) in to h, mn, s, ms, µs, ns.           |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_   b_BoardHandle : Handle of board APCI-1710      |
|                     BYTE_   b_ModulNbr    : Selected module number (0 to 3)|
|                     ULONG_ ul_ChronoValue : Measured chronometer timing    |
|                                             value.                         |
|                                             See"i_APCI1710_ReadChronoValue"|
+----------------------------------------------------------------------------+
| Output Parameters : PULONG_   pul_Hour        : Chronometer timing hour    |
|                     PBYTE_     pb_Minute      : Chronometer timing minute  |
|                     PBYTE_     pb_Second      : Chronometer timing second  |
|                     PUINT_    pui_MilliSecond  : Chronometer timing mini   |
|                                                 second                     |
|                     PUINT_    pui_MicroSecond : Chronometer timing micro   |
|                                                 second                     |
|                     PUINT_    pui_NanoSecond  : Chronometer timing nano    |
|                                                 second                     |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a Chronometer module             |
|                     -4: Chronometer not initialised see function           |
|                         "i_APCI1710_InitChrono"                            |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_ConvertChronoValue(struct comedi_device * dev,
	BYTE b_ModulNbr,
	ULONG ul_ChronoValue,
	PULONG pul_Hour,
	PBYTE pb_Minute,
	PBYTE pb_Second,
	PUINT pui_MilliSecond, PUINT pui_MicroSecond, PUINT pui_NanoSecond)
{
	INT i_ReturnValue = 0;
	double d_Hour;
	double d_Minute;
	double d_Second;
	double d_MilliSecond;
	double d_MicroSecond;
	double d_NanoSecond;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if chronometer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_CHRONOMETER) {
	      /***********************************/
			/* Test if chronometer initialised */
	      /***********************************/

			if (devpriv->
				s_ModuleInfo[b_ModulNbr].
				s_ChronoModuleInfo.b_ChronoInit == 1) {
				fpu_begin();

				d_Hour = (double)ul_ChronoValue *(double)
					devpriv->s_ModuleInfo[b_ModulNbr].
					s_ChronoModuleInfo.d_TimingInterval;

				switch (devpriv->
					s_ModuleInfo[b_ModulNbr].
					s_ChronoModuleInfo.b_TimingUnit) {
				case 0:
					d_Hour = d_Hour / (double)1000.0;

				case 1:
					d_Hour = d_Hour / (double)1000.0;

				case 2:
					d_Hour = d_Hour / (double)1000.0;

				case 3:
					d_Hour = d_Hour / (double)60.0;

				case 4:
			    /**********************/
					/* Calculate the hour */
			    /**********************/

					d_Hour = d_Hour / (double)60.0;
					*pul_Hour = (ULONG) d_Hour;

			    /************************/
					/* Calculate the minute */
			    /************************/

					d_Minute = d_Hour - *pul_Hour;
					d_Minute = d_Minute * 60;
					*pb_Minute = (BYTE) d_Minute;

			    /************************/
					/* Calculate the second */
			    /************************/

					d_Second = d_Minute - *pb_Minute;
					d_Second = d_Second * 60;
					*pb_Second = (BYTE) d_Second;

			    /*****************************/
					/* Calculate the mini second */
			    /*****************************/

					d_MilliSecond = d_Second - *pb_Second;
					d_MilliSecond = d_MilliSecond * 1000;
					*pui_MilliSecond = (UINT) d_MilliSecond;

			    /******************************/
					/* Calculate the micro second */
			    /******************************/

					d_MicroSecond =
						d_MilliSecond -
						*pui_MilliSecond;
					d_MicroSecond = d_MicroSecond * 1000;
					*pui_MicroSecond = (UINT) d_MicroSecond;

			    /******************************/
					/* Calculate the micro second */
			    /******************************/

					d_NanoSecond =
						d_MicroSecond -
						*pui_MicroSecond;
					d_NanoSecond = d_NanoSecond * 1000;
					*pui_NanoSecond = (UINT) d_NanoSecond;
					break;
				}

				fpu_end();
			} else {
		 /*******************************/
				/* Chronometer not initialised */
		 /*******************************/
				DPRINTK("Chronometer not initialised\n");
				i_ReturnValue = -4;
			}
		} else {
	      /******************************************/
			/* The module is not a Chronometer module */
	      /******************************************/
			DPRINTK("The module is not a Chronometer module\n");
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
| Function Name     : INT i_APCI1710_InsnBitsChronoDigitalIO(struct comedi_device *dev,struct comedi_subdevice *s,
	struct comedi_insn *insn,unsigned int *data)                    |
+----------------------------------------------------------------------------+
| Task              : Sets the output witch has been passed with the         |
|                     parameter b_Channel. Setting an output means setting an|
|                     output high.                                           |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_ b_BoardHandle   : Handle of board APCI-1710      |
|                     BYTE_ b_ModulNbr      : Selected module number (0 to 3)|
|                     BYTE_ b_OutputChannel : Selection from digital output  |
|                           CR_CHAN()                  channel (0 to 2)               |
|                                              0 : Channel H                 |
|                                              1 : Channel A                 |
|                                              2 : Channel B                 |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a Chronometer module             |
|                     -4: The selected digital output is wrong               |
|                     -5: Chronometer not initialised see function           |
|                         "i_APCI1710_InitChrono"                            |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_SetChronoChlOff                       |
|                               (BYTE_  b_BoardHandle,                       |
|                                BYTE_  b_ModulNbr,                          |
|                                BYTE_  b_OutputChannel)                     |
+----------------------------------------------------------------------------+
| Task              : Resets the output witch has been passed with the       |
|                     parameter b_Channel. Resetting an output means setting |
|                     an output low.                                         |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_ b_BoardHandle   : Handle of board APCI-1710
                        data[0] : Chl ON, Chl OFF , Chl Read , Port Read

|                     BYTE_ b_ModulNbr  CR_AREF    : Selected module number (0 to 3)|
|                     BYTE_ b_OutputChannel CR_CHAN : Selection from digital output  |
|                                             channel (0 to 2)               |
|                                              0 : Channel H                 |
|                                              1 : Channel A                 |
|                                              2 : Channel B                 |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a Chronometer module             |
|                     -4: The selected digital output is wrong               |
|                     -5: Chronometer not initialised see function           |
|                         "i_APCI1710_InitChrono"                            |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_ReadChronoChlValue                    |
|                               (BYTE_   b_BoardHandle,                      |
|                                BYTE_   b_ModulNbr,                         |
|                                BYTE_   b_InputChannel,                     |
|                                PBYTE_ pb_ChannelStatus)                    |
+----------------------------------------------------------------------------+
| Task              : Return the status from selected digital input          |
|                     (b_InputChannel) from selected chronometer             |
|                     module (b_ModulNbr).                                   |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_ b_BoardHandle   : Handle of board APCI-1710      |
|                     BYTE_ b_ModulNbr      : Selected module number (0 to 3)|
|                     BYTE_ b_InputChannel  : Selection from digital input   |
|                                             channel (0 to 2)               |
|                                   CR_CHAN()             0 : Channel E               |
|                                                1 : Channel F               |
|                                                2 : Channel G               |
+----------------------------------------------------------------------------+
| Output Parameters : PBYTE_ pb_ChannelStatus : Digital input channel status.|
|                                data[0]                0 : Channel is not active   |
|                                                1 : Channel is active       |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a Chronometer module             |
|                     -4: The selected digital input is wrong                |
|                     -5: Chronometer not initialised see function           |
|                         "i_APCI1710_InitChrono"                            |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_ReadChronoPortValue                   |
|                               (BYTE_   b_BoardHandle,                      |
|                                BYTE_   b_ModulNbr,                         |
|                                PBYTE_ pb_PortValue)                        |
+----------------------------------------------------------------------------+
| Task              : Return the status from digital inputs port from        |
|                     selected  (b_ModulNbr) chronometer module.             |
+----------------------------------------------------------------------------+
| Input Parameters  : BYTE_ b_BoardHandle   : Handle of board APCI-1710      |
|                     BYTE_ b_ModulNbr      : Selected module number (0 to 3)|
+----------------------------------------------------------------------------+
| Output Parameters : PBYTE_ pb_PortValue   : Digital inputs port status.
|                     data[0]
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a Chronometer module             |
|                     -4: Chronometer not initialised see function           |
|                         "i_APCI1710_InitChrono"                            |
+----------------------------------------------------------------------------+
*/

INT i_APCI1710_InsnBitsChronoDigitalIO(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	INT i_ReturnValue = 0;
	BYTE b_ModulNbr, b_OutputChannel, b_InputChannel, b_IOType;
	DWORD dw_Status;
	PBYTE pb_ChannelStatus;
	PBYTE pb_PortValue;

	b_ModulNbr = CR_AREF(insn->chanspec);
	i_ReturnValue = insn->n;
	b_IOType = (BYTE) data[0];

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if chronometer */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_CHRONOMETER) {
	      /***********************************/
			/* Test if chronometer initialised */
	      /***********************************/

			if (devpriv->s_ModuleInfo[b_ModulNbr].
				s_ChronoModuleInfo.b_ChronoInit == 1) {
		 /***********************************/
				/* Test the digital output channel */
		 /***********************************/
				switch (b_IOType) {

				case APCI1710_CHRONO_SET_CHANNELOFF:

					b_OutputChannel =
						(BYTE) CR_CHAN(insn->chanspec);
					if (b_OutputChannel <= 2) {

						outl(0, devpriv->s_BoardInfos.
							ui_Address + 20 +
							(b_OutputChannel * 4) +
							(64 * b_ModulNbr));
					}	// if ((b_OutputChannel >= 0) && (b_OutputChannel <= 2))
					else {
		    /****************************************/
						/* The selected digital output is wrong */
		    /****************************************/

						DPRINTK("The selected digital output is wrong\n");
						i_ReturnValue = -4;

					}	// if ((b_OutputChannel >= 0) && (b_OutputChannel <= 2))

					break;

				case APCI1710_CHRONO_SET_CHANNELON:

					b_OutputChannel =
						(BYTE) CR_CHAN(insn->chanspec);
					if (b_OutputChannel <= 2) {

						outl(1, devpriv->s_BoardInfos.
							ui_Address + 20 +
							(b_OutputChannel * 4) +
							(64 * b_ModulNbr));
					}	// if ((b_OutputChannel >= 0) && (b_OutputChannel <= 2))
					else {
		    /****************************************/
						/* The selected digital output is wrong */
		    /****************************************/

						DPRINTK("The selected digital output is wrong\n");
						i_ReturnValue = -4;

					}	// if ((b_OutputChannel >= 0) && (b_OutputChannel <= 2))

					break;

				case APCI1710_CHRONO_READ_CHANNEL:
		 /**********************************/
					/* Test the digital input channel */
		 /**********************************/
					pb_ChannelStatus = (PBYTE) & data[0];
					b_InputChannel =
						(BYTE) CR_CHAN(insn->chanspec);

					if (b_InputChannel <= 2) {

						dw_Status =
							inl(devpriv->
							s_BoardInfos.
							ui_Address + 12 +
							(64 * b_ModulNbr));

						*pb_ChannelStatus =
							(BYTE) (((dw_Status >>
									b_InputChannel)
								& 1) ^ 1);
					}	// if ((b_InputChannel >= 0) && (b_InputChannel <= 2))
					else {
		    /***************************************/
						/* The selected digital input is wrong */
		    /***************************************/

						DPRINTK("The selected digital input is wrong\n");
						i_ReturnValue = -4;
					}	// if ((b_InputChannel >= 0) && (b_InputChannel <= 2))

					break;

				case APCI1710_CHRONO_READ_PORT:

					pb_PortValue = (PBYTE) & data[0];

					dw_Status =
						inl(devpriv->s_BoardInfos.
						ui_Address + 12 +
						(64 * b_ModulNbr));

					*pb_PortValue =
						(BYTE) ((dw_Status & 0x7) ^ 7);
					break;
				}
			} else {
		 /*******************************/
				/* Chronometer not initialised */
		 /*******************************/

				DPRINTK("Chronometer not initialised\n");
				i_ReturnValue = -5;
			}
		} else {
	      /******************************************/
			/* The module is not a Chronometer module */
	      /******************************************/

			DPRINTK("The module is not a Chronometer module\n");
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
