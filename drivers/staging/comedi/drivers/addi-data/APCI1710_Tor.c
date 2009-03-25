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
  | Module name : TOR.C           | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date     :  02/12/2002                |
  +-----------------------------------------------------------------------+
  | Description :   APCI-1710 tor counter module                          |
  |                                                                       |
  |                                                                       |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  | 27/01/99 | S. Weber  | 40 MHz implementation                          |
  +-----------------------------------------------------------------------+
  | 28/04/00 | S. Weber  | Simple,double and quadruple mode implementation|
  |          |           | Extern clock implementation                    |
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

#include "APCI1710_Tor.h"

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_     i_APCI1710_InitTorCounter                    |
|                                       (unsigned char_     b_BoardHandle,            |
|                                        unsigned char_     b_ModulNbr,               |
|                                        unsigned char_     b_TorCounter,             |
|                                        unsigned char_     b_PCIInputClock,          |
|                                        unsigned char_     b_TimingUnit,             |
|                                        ULONG_   ul_TimingInterval,         |
|                                        PULONG_ pul_RealTimingInterval)     |
+----------------------------------------------------------------------------+
| Task              : Configure the selected tor counter (b_TorCounter)      |
|                     from selected module (b_ModulNbr).                     |
|                     The ul_TimingInterval and ul_TimingUnit determine the  |
|                     timing base for the measurement.                       |
|                     The pul_RealTimingInterval return the real timing      |
|                     value. You must calling this function be for you call  |
|                     any other function witch access of the tor counter.    |
|                                                                            |
+----------------------------------------------------------------------------+
| Input Parameters  :    |
|
		CR_AREF	unsigned char_   b_ModulNbr       : Module number to configure  |
|                                                (0 to 3)                    |
|           data[0] unsigned char_   b_TorCounter     : Tor counter selection       |
|                                                (0 or 1).                   |
|           data[1] unsigned char_   b_PCIInputClock  : Selection from PCI bus clock|
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
|                                                - APCI1710_GATE_INPUT       |
|                                                  Used the gate input for   |
|						   the base clock. If you    |
|						   have selected this option,|
|						   than it is not possibl to |
|						   used the gate input for   |
|						   enabled the acquisition   |
|           data[2] unsigned char_   b_TimingUnit    : Base timing unit (0 to 4)    |
|                                                 0 : ns                     |
|                                                 1 : µs                     |
|                                                 2 : ms                     |
|                                                 3 : s                      |
|                                                 4 : mn                     |
|           data[3]          ULONG_ ul_TimingInterval : Base timing value.          |
+----------------------------------------------------------------------------+
| Output Parameters : PULONG_  pul_RealTimingInterval : Real  base timing    |
|                     data[0]                                  value.               |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a tor counter module             |
|                     -4: Tor counter selection is wrong                     |
|                     -5: The selected PCI input clock is wrong              |
|                     -6: Timing unit selection is wrong                     |
|                     -7: Base timing selection is wrong                     |
|                     -8: You can not used the 40MHz clock selection wich    |
|                         this board                                         |
|                     -9: You can not used the 40MHz clock selection wich    |
|                         this TOR version                                   |
+----------------------------------------------------------------------------+
*/

int i_APCI1710_InsnConfigInitTorCounter(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_ReturnValue = 0;
	ULONG ul_TimerValue = 0;
	DWORD dw_Command;
	double d_RealTimingInterval = 0;
	unsigned char b_ModulNbr;
	unsigned char b_TorCounter;
	unsigned char b_PCIInputClock;
	unsigned char b_TimingUnit;
	ULONG ul_TimingInterval;
	ULONG ul_RealTimingInterval = 0;

	i_ReturnValue = insn->n;
	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);

	b_TorCounter = (unsigned char) data[0];
	b_PCIInputClock = (unsigned char) data[1];
	b_TimingUnit = (unsigned char) data[2];
	ul_TimingInterval = (ULONG) data[3];
	printk("INPUT clock %d\n", b_PCIInputClock);

		/**************************/
	/* Test the module number */
		/**************************/

	if (b_ModulNbr < 4) {
		/***********************/
		/* Test if tor counter */
		/***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_TOR_COUNTER) {
	      /**********************************/
			/* Test the tor counter selection */
	      /**********************************/

			if (b_TorCounter <= 1) {
		 /**************************/
				/* Test the PCI bus clock */
		 /**************************/

				if ((b_PCIInputClock == APCI1710_30MHZ) ||
					(b_PCIInputClock == APCI1710_33MHZ) ||
					(b_PCIInputClock == APCI1710_40MHZ) ||
					(b_PCIInputClock ==
						APCI1710_GATE_INPUT)) {
		    /************************/
					/* Test the timing unit */
		    /************************/

					if ((b_TimingUnit <= 4)
						|| (b_PCIInputClock ==
							APCI1710_GATE_INPUT)) {
		       /**********************************/
						/* Test the base timing selection */
		       /**********************************/

						if (((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 0) && (ul_TimingInterval >= 133) && (ul_TimingInterval <= 0xFFFFFFFFUL)) || ((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 1) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 571230650UL)) || ((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 2) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 571230UL)) || ((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 3) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 571UL)) || ((b_PCIInputClock == APCI1710_30MHZ) && (b_TimingUnit == 4) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 9UL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 0) && (ul_TimingInterval >= 121) && (ul_TimingInterval <= 0xFFFFFFFFUL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 1) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 519691043UL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 2) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 519691UL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 3) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 520UL)) || ((b_PCIInputClock == APCI1710_33MHZ) && (b_TimingUnit == 4) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 8UL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 0) && (ul_TimingInterval >= 100) && (ul_TimingInterval <= 0xFFFFFFFFUL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 1) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 429496729UL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 2) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 429496UL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 3) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 429UL)) || ((b_PCIInputClock == APCI1710_40MHZ) && (b_TimingUnit == 4) && (ul_TimingInterval >= 1) && (ul_TimingInterval <= 7UL)) || ((b_PCIInputClock == APCI1710_GATE_INPUT) && (ul_TimingInterval >= 2))) {
				/**************************/
							/* Test the board version */
				/**************************/

							if (((b_PCIInputClock == APCI1710_40MHZ) && (devpriv->s_BoardInfos.b_BoardVersion > 0)) || (b_PCIInputClock != APCI1710_40MHZ)) {
			     /************************/
								/* Test the TOR version */
			     /************************/

								if (((b_PCIInputClock == APCI1710_40MHZ) && ((devpriv->s_BoardInfos.dw_MolduleConfiguration[b_ModulNbr] & 0xFFFF) >= 0x3131)) || ((b_PCIInputClock == APCI1710_GATE_INPUT) && ((devpriv->s_BoardInfos.dw_MolduleConfiguration[b_ModulNbr] & 0xFFFF) >= 0x3132)) || (b_PCIInputClock == APCI1710_30MHZ) || (b_PCIInputClock == APCI1710_33MHZ)) {
				/*********************************/
									/* Test if not extern clock used */
				/*********************************/

									if (b_PCIInputClock != APCI1710_GATE_INPUT) {
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
												(0.00025 * b_PCIInputClock));

					      /*******************/
											/* Round the value */
					      /*******************/

											if ((double)((double)ul_TimingInterval * (0.00025 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
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
												(0.00025 * (double)b_PCIInputClock));
											d_RealTimingInterval
												=
												(double)
												ul_TimerValue
												/
												(0.00025
												*
												(double)
												b_PCIInputClock);

											if ((double)((double)ul_TimerValue / (0.00025 * (double)b_PCIInputClock)) >= (double)((double)ul_RealTimingInterval + 0.5)) {
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

											ul_TimerValue
												=
												(ULONG)
												(ul_TimingInterval
												*
												(0.25 * b_PCIInputClock));

					      /*******************/
											/* Round the value */
					      /*******************/

											if ((double)((double)ul_TimingInterval * (0.25 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
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
												(0.25 * (double)b_PCIInputClock));
											d_RealTimingInterval
												=
												(double)
												ul_TimerValue
												/
												(
												(double)
												0.25
												*
												(double)
												b_PCIInputClock);

											if ((double)((double)ul_TimerValue / (0.25 * (double)b_PCIInputClock)) >= (double)((double)ul_RealTimingInterval + 0.5)) {
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

											ul_TimerValue
												=
												ul_TimingInterval
												*
												(250.0
												*
												b_PCIInputClock);

					      /*******************/
											/* Round the value */
					      /*******************/

											if ((double)((double)ul_TimingInterval * (250.0 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
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
												(250.0 * (double)b_PCIInputClock));
											d_RealTimingInterval
												=
												(double)
												ul_TimerValue
												/
												(250.0
												*
												(double)
												b_PCIInputClock);

											if ((double)((double)ul_TimerValue / (250.0 * (double)b_PCIInputClock)) >= (double)((double)ul_RealTimingInterval + 0.5)) {
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

											ul_TimerValue
												=
												(ULONG)
												(ul_TimingInterval
												*
												(250000.0
													*
													b_PCIInputClock));

					      /*******************/
											/* Round the value */
					      /*******************/

											if ((double)((double)ul_TimingInterval * (250000.0 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
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
												(250000.0
													*
													(double)
													b_PCIInputClock));
											d_RealTimingInterval
												=
												(double)
												ul_TimerValue
												/
												(250000.0
												*
												(double)
												b_PCIInputClock);

											if ((double)((double)ul_TimerValue / (250000.0 * (double)b_PCIInputClock)) >= (double)((double)ul_RealTimingInterval + 0.5)) {
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

											ul_TimerValue
												=
												(ULONG)
												(
												(ul_TimingInterval
													*
													60)
												*
												(250000.0
													*
													b_PCIInputClock));

					      /*******************/
											/* Round the value */
					      /*******************/

											if ((double)((double)(ul_TimingInterval * 60.0) * (250000.0 * (double)b_PCIInputClock)) >= ((double)((double)ul_TimerValue + 0.5))) {
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
												(250000.0
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
												(250000.0
													*
													(double)
													b_PCIInputClock))
												/
												60.0;

											if ((double)(((double)ul_TimerValue / (250000.0 * (double)b_PCIInputClock)) / 60.0) >= (double)((double)ul_RealTimingInterval + 0.5)) {
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
													1.007752288);
											}

											break;
										}

										fpu_end();
									}	// if (b_PCIInputClock != APCI1710_GATE_INPUT)
									else {
				   /*************************************************************/
										/* 2 Clock used for the overflow and the reload from counter */
				   /*************************************************************/

										ul_TimerValue
											=
											ul_TimingInterval
											-
											2;
									}	// if (b_PCIInputClock != APCI1710_GATE_INPUT)

				/****************************/
									/* Save the PCI input clock */
				/****************************/
									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_TorCounterModuleInfo.
										b_PCIInputClock
										=
										b_PCIInputClock;

				/************************/
									/* Save the timing unit */
				/************************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_TorCounterModuleInfo.
										s_TorCounterInfo
										[b_TorCounter].
										b_TimingUnit
										=
										b_TimingUnit;

				/************************/
									/* Save the base timing */
				/************************/
									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_TorCounterModuleInfo.
										s_TorCounterInfo
										[b_TorCounter].
										d_TimingInterval
										=
										d_RealTimingInterval;

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_TorCounterModuleInfo.
										s_TorCounterInfo
										[b_TorCounter].
										ul_RealTimingInterval
										=
										ul_RealTimingInterval;

				/*******************/
									/* Get the command */
				/*******************/

									dw_Command
										=
										inl
										(devpriv->
										s_BoardInfos.
										ui_Address
										+
										4
										+
										(16 * b_TorCounter) + (64 * b_ModulNbr));

									dw_Command
										=
										(dw_Command
										>>
										4)
										&
										0xF;

				/******************/
									/* Test if 40 MHz */
				/******************/

									if (b_PCIInputClock == APCI1710_40MHZ) {
				   /****************************/
										/* Set the 40 MHz selection */
				   /****************************/

										dw_Command
											=
											dw_Command
											|
											0x10;
									}

				/*****************************/
									/* Test if extern clock used */
				/*****************************/

									if (b_PCIInputClock == APCI1710_GATE_INPUT) {
				   /****************************/
										/* Set the 40 MHz selection */
				   /****************************/

										dw_Command
											=
											dw_Command
											|
											0x20;
									}

				/*************************/
									/* Write the new command */
				/*************************/

									outl(dw_Command, devpriv->s_BoardInfos.ui_Address + 4 + (16 * b_TorCounter) + (64 * b_ModulNbr));

				/*******************/
									/* Disable the tor */
				/*******************/

									outl(0, devpriv->s_BoardInfos.ui_Address + 8 + (16 * b_TorCounter) + (64 * b_ModulNbr));
				/*************************/
									/* Set the timer 1 value */
				/*************************/

									outl(ul_TimerValue, devpriv->s_BoardInfos.ui_Address + 0 + (16 * b_TorCounter) + (64 * b_ModulNbr));

				/*********************/
									/* Tor counter init. */
				/*********************/

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_TorCounterModuleInfo.
										s_TorCounterInfo
										[b_TorCounter].
										b_TorCounterInit
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
		       /**********************************/
						/* Timing unit selection is wrong */
		       /**********************************/

						DPRINTK("Timing unit selection is wrong\n");
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
			}	// if (b_TorCounterMode >= 0 && b_TorCounterMode <= 7)
			else {
		 /**********************************/
				/* Tor Counter selection is wrong */
		 /**********************************/

				DPRINTK("Tor Counter selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_TorCounterMode >= 0 && b_TorCounterMode <= 7)
		} else {
	      /******************************************/
			/* The module is not a tor counter module */
	      /******************************************/

			DPRINTK("The module is not a tor counter module\n");
			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/

		DPRINTK("Module number error\n");
		i_ReturnValue = -2;
	}
	data[0] = (unsigned int) ul_RealTimingInterval;
	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_EnableTorCounter                      |
|                                               (unsigned char_ b_BoardHandle,        |
|                                                unsigned char_ b_ModulNbr,           |
|						 unsigned char_ b_TorCounter,         |
|						 unsigned char_ b_InputMode,          |
|						 unsigned char_ b_ExternGate,         |
|                                                unsigned char_ b_CycleMode,          |
|                                                unsigned char_ b_InterruptEnable)    |
+----------------------------------------------------------------------------+
| Task              : Enable the tor counter (b_TorCounter) from selected    |
|		      module (b_ModulNbr). You must calling the              |
|                     "i_APCI1710_InitTorCounter" function be for you call   |
|		      this function.                                         |
|                     If you enable the tor counter interrupt, the           |
|                     tor counter generate a interrupt after the timing cycle|
|                     See function "i_APCI1710_SetBoardIntRoutineX" and the  |
|                     Interrupt mask description chapter from this manual.   |
|                     The b_CycleMode parameter determine if you will        |
|                     measured a single or more cycle.                       |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle  : Handle of board APCI-1710       |
|                     unsigned char_ b_ModulNbr     : Selected module number (0 to 3) |
|                     unsigned char_ b_TorCounter   : Tor counter selection (0 or 1). |
|		      unsigned char_ b_InputMode    : Input signal level selection    |
|						0 : Tor count each low level |
|						1 : Tor count each high level|
|		      unsigned char_ b_ExternGate   : Extern gate action selection    |
|						0 : Extern gate signal not   |
|						    used                     |
|						1 : Extern gate signal used. |
|						    If you selected the      |
|						    single mode, each high   |
|						    level signal start the   |
|						    counter.                 |
|						    If you selected the      |
|						    continuous mode, the     |
|						    first high level signal  |
|						    start the tor counter    |
|									     |
|					      APCI1710_TOR_QUADRUPLE _MODE : |
|					      In the quadruple mode, the edge|
|					      analysis circuit generates a   |
|					      counting pulse from each edge  |
|					      of 2 signals which are phase   |
|					      shifted in relation to each    |
|					      other.                         |
|					      The gate input is used for the |
|					      signal B                       |
|									     |
|					      APCI1710_TOR_DOUBLE_MODE:      |
|					      Functions in the same way as   |
|					      the quadruple mode, except that|
|					      only two of the four edges are |
|					      analysed per period.           |
|					      The gate input is used for the |
|					      signal B                       |
|									     |
|					      APCI1710_TOR_SIMPLE_MODE:      |
|					      Functions in the same way as   |
|					      the quadruple mode, except that|
|					      only one of the four edges is  |
|					      analysed per period.           |
|					      The gate input is used for the |
|					      signal B                       |
|									     |
|                     unsigned char_ b_CycleMode    : Selected the tor counter        |
|                                            acquisition mode                |
|                     unsigned char_ b_InterruptEnable : Enable or disable the        |
|                                               tor counter interrupt.       |
|                                               APCI1710_ENABLE:             |
|                                               Enable the tor counter       |
|                                               interrupt                    |
|                                               APCI1710_DISABLE:            |
|                                               Disable the tor counter      |
|                                               interrupt                    |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a tor counter module             |
|                     -4: Tor counter selection is wrong                     |
|                     -5: Tor counter not initialised see function           |
|                         "i_APCI1710_InitTorCounter"                        |
|                     -6: Tor input signal selection is wrong                |
|                     -7: Extern gate signal mode is wrong                   |
|                     -8: Tor counter acquisition mode cycle is wrong        |
|                     -9: Interrupt parameter is wrong                       |
|                     -10:Interrupt function not initialised.                |
|                         See function "i_APCI1710_SetBoardIntRoutineX"      |
+----------------------------------------------------------------------------+
*/
/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_DisableTorCounter                     |
|                                               (unsigned char_  b_BoardHandle,       |
|                                                unsigned char_  b_ModulNbr,          |
|						 unsigned char_  b_TorCounter)        |
+----------------------------------------------------------------------------+
| Task              : Disable the tor counter (b_TorCounter) from selected   |
|		      module (b_ModulNbr). If you disable the tor counter    |
|		      after a start cycle occur and you restart the tor      |
|		      counter witch the " i_APCI1710_EnableTorCounter"       |
|		      function, the status register is cleared               |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle  : Handle of board APCI-1710       |
|                     unsigned char_ b_ModulNbr     : Selected module number (0 to 3) |
|                     unsigned char_ b_TorCounter   : Tor counter selection (0 or 1). |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a tor counter module             |
|                     -4: Tor counter selection is wrong                     |
|                     -5: Tor counter not initialised see function           |
|                         "i_APCI1710_InitTorCounter"                        |
|                     -6: Tor counter not enabled see function               |
|                         "i_APCI1710_EnableTorCounter"                      |
+----------------------------------------------------------------------------+
*/

int i_APCI1710_InsnWriteEnableDisableTorCounter(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_ReturnValue = 0;
	DWORD dw_Status;
	DWORD dw_DummyRead;
	DWORD dw_ConfigReg;
	unsigned char b_ModulNbr, b_Action;
	unsigned char b_TorCounter;
	unsigned char b_InputMode;
	unsigned char b_ExternGate;
	unsigned char b_CycleMode;
	unsigned char b_InterruptEnable;

	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_Action = (unsigned char) data[0];	// enable or disable
	b_TorCounter = (unsigned char) data[1];
	b_InputMode = (unsigned char) data[2];
	b_ExternGate = (unsigned char) data[3];
	b_CycleMode = (unsigned char) data[4];
	b_InterruptEnable = (unsigned char) data[5];
	i_ReturnValue = insn->n;;
	devpriv->tsk_Current = current;	// Save the current process task structure
	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if tor counter */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_TOR_COUNTER) {
	      /**********************************/
			/* Test the tor counter selection */
	      /**********************************/

			if (b_TorCounter <= 1) {
				switch (b_Action)	// Enable or Disable
				{
				case APCI1710_ENABLE:
		 /***********************************/
					/* Test if tor counter initialised */
		 /***********************************/

					dw_Status =
						inl(devpriv->s_BoardInfos.
						ui_Address + 8 +
						(16 * b_TorCounter) +
						(64 * b_ModulNbr));

					if (dw_Status & 0x10) {
		    /******************************/
						/* Test the input signal mode */
		    /******************************/

						if (b_InputMode == 0 ||
							b_InputMode == 1 ||
							b_InputMode ==
							APCI1710_TOR_SIMPLE_MODE
							|| b_InputMode ==
							APCI1710_TOR_DOUBLE_MODE
							|| b_InputMode ==
							APCI1710_TOR_QUADRUPLE_MODE)
						{
		       /************************************/
							/* Test the extern gate signal mode */
		       /************************************/

							if (b_ExternGate == 0
								|| b_ExternGate
								== 1
								|| b_InputMode >
								1) {
			  /*********************************/
								/* Test the cycle mode parameter */
			  /*********************************/

								if ((b_CycleMode == APCI1710_SINGLE) || (b_CycleMode == APCI1710_CONTINUOUS)) {
			     /***************************/
									/* Test the interrupt flag */
			     /***************************/

									if ((b_InterruptEnable == APCI1710_ENABLE) || (b_InterruptEnable == APCI1710_DISABLE)) {

				   /***************************/
										/* Save the interrupt mode */
				   /***************************/

										devpriv->
											s_ModuleInfo
											[b_ModulNbr].
											s_TorCounterModuleInfo.
											s_TorCounterInfo
											[b_TorCounter].
											b_InterruptEnable
											=
											b_InterruptEnable;

				   /*******************/
										/* Get the command */
				   /*******************/

										dw_ConfigReg
											=
											inl
											(devpriv->
											s_BoardInfos.
											ui_Address
											+
											4
											+
											(16 * b_TorCounter) + (64 * b_ModulNbr));

										dw_ConfigReg
											=
											(dw_ConfigReg
											>>
											4)
											&
											0x30;

				   /********************************/
										/* Test if not direct mode used */
				   /********************************/

										if (b_InputMode > 1) {
				      /*******************************/
											/* Extern gate can not be used */
				      /*******************************/

											b_ExternGate
												=
												0;

				      /*******************************************/
											/* Enable the extern gate for the Signal B */
				      /*******************************************/

											dw_ConfigReg
												=
												dw_ConfigReg
												|
												0x40;

				      /***********************/
											/* Test if simple mode */
				      /***********************/

											if (b_InputMode == APCI1710_TOR_SIMPLE_MODE) {
					 /**************************/
												/* Enable the sinple mode */
					 /**************************/

												dw_ConfigReg
													=
													dw_ConfigReg
													|
													0x780;

											}	// if (b_InputMode == APCI1710_TOR_SIMPLE_MODE)

				      /***********************/
											/* Test if double mode */
				      /***********************/

											if (b_InputMode == APCI1710_TOR_DOUBLE_MODE) {
					 /**************************/
												/* Enable the double mode */
					 /**************************/

												dw_ConfigReg
													=
													dw_ConfigReg
													|
													0x180;

											}	// if (b_InputMode == APCI1710_TOR_DOUBLE_MODE)

											b_InputMode
												=
												0;
										}	// if (b_InputMode > 1)

				   /*******************/
										/* Set the command */
				   /*******************/

										dw_ConfigReg
											=
											dw_ConfigReg
											|
											b_CycleMode
											|
											(b_InterruptEnable
											*
											2)
											|
											(b_InputMode
											*
											4)
											|
											(b_ExternGate
											*
											8);

				   /*****************************/
										/* Clear the status register */
				   /*****************************/

										dw_DummyRead
											=
											inl
											(devpriv->
											s_BoardInfos.
											ui_Address
											+
											0
											+
											(16 * b_TorCounter) + (64 * b_ModulNbr));

				   /***************************************/
										/* Clear the interrupt status register */
				   /***************************************/

										dw_DummyRead
											=
											inl
											(devpriv->
											s_BoardInfos.
											ui_Address
											+
											12
											+
											(16 * b_TorCounter) + (64 * b_ModulNbr));

				   /********************/
										/* Set the commando */
				   /********************/

										outl(dw_ConfigReg, devpriv->s_BoardInfos.ui_Address + 4 + (16 * b_TorCounter) + (64 * b_ModulNbr));

				   /****************/
										/* Set the gate */
				   /****************/

										outl(1, devpriv->s_BoardInfos.ui_Address + 8 + (16 * b_TorCounter) + (64 * b_ModulNbr));

									}	// if ((b_InterruptEnable == APCI1710_ENABLE) || (b_InterruptEnable == APCI1710_DISABLE))
									else {
				/********************************/
										/* Interrupt parameter is wrong */
				/********************************/

										DPRINTK("Interrupt parameter is wrong\n");
										i_ReturnValue
											=
											-9;
									}	// if ((b_InterruptEnable == APCI1710_ENABLE) || (b_InterruptEnable == APCI1710_DISABLE))
								}	// if ((b_CycleMode == APCI1710_SINGLE) || (b_CycleMode == APCI1710_CONTINUOUS))
								else {
			     /***********************************************/
									/* Tor counter acquisition mode cycle is wrong */
			     /***********************************************/

									DPRINTK("Tor counter acquisition mode cycle is wrong\n");
									i_ReturnValue
										=
										-8;
								}	// if ((b_CycleMode == APCI1710_SINGLE) || (b_CycleMode == APCI1710_CONTINUOUS))
							}	// if (b_ExternGate >= 0 && b_ExternGate <= 1)
							else {
			  /***********************************/
								/* Extern gate input mode is wrong */
			  /***********************************/

								DPRINTK("Extern gate input mode is wrong\n");
								i_ReturnValue =
									-7;
							}	// if (b_ExternGate >= 0 && b_ExternGate <= 1)
						}	// if (b_InputMode >= 0 && b_InputMode <= 1)
						else {
		       /***************************************/
							/* Tor input signal selection is wrong */
		       /***************************************/

							DPRINTK("Tor input signal selection is wrong\n");
							i_ReturnValue = -6;
						}
					} else {
		    /*******************************/
						/* Tor counter not initialised */
		    /*******************************/

						DPRINTK("Tor counter not initialised\n");
						i_ReturnValue = -5;
					}
					break;

				case APCI1710_DISABLE:
			 /***********************************/
					/* Test if tor counter initialised */
		 /***********************************/

					dw_Status = inl(devpriv->s_BoardInfos.
						ui_Address + 8 +
						(16 * b_TorCounter) +
						(64 * b_ModulNbr));

		 /*******************************/
					/* Test if counter initialised */
		 /*******************************/

					if (dw_Status & 0x10) {
		    /***************************/
						/* Test if counter enabled */
		    /***************************/

						if (dw_Status & 0x1) {
		       /****************************/
							/* Clear the interrupt mode */
		       /****************************/
							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_TorCounterModuleInfo.
								s_TorCounterInfo
								[b_TorCounter].
								b_InterruptEnable
								=
								APCI1710_DISABLE;

		       /******************/
							/* Clear the gate */
		       /******************/

							outl(0, devpriv->
								s_BoardInfos.
								ui_Address + 8 +
								(16 * b_TorCounter) + (64 * b_ModulNbr));
						}	// if (dw_Status & 0x1)
						else {
		       /***************************/
							/* Tor counter not enabled */
		       /***************************/

							DPRINTK("Tor counter not enabled \n");
							i_ReturnValue = -6;
						}	// if (dw_Status & 0x1)
					}	// if (dw_Status & 0x10)
					else {
		    /*******************************/
						/* Tor counter not initialised */
		    /*******************************/

						DPRINTK("Tor counter not initialised\n");
						i_ReturnValue = -5;
					}	// // if (dw_Status & 0x10)

				}	// switch
			}	// if (b_TorCounter <= 1)
			else {
		 /**********************************/
				/* Tor counter selection is wrong */
		 /**********************************/

				DPRINTK("Tor counter selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_TorCounter <= 1)
		} else {
	      /******************************************/
			/* The module is not a tor counter module */
	      /******************************************/

			DPRINTK("The module is not a tor counter module \n");
			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/

		DPRINTK("Module number error \n");
		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_GetTorCounterInitialisation           |
|                                               (unsigned char_     b_BoardHandle,    |
|                                                unsigned char_     b_ModulNbr,       |
|						 unsigned char_     b_TorCounter,     |
|                                        	 unsigned char *_   pb_TimingUnit,     |
|                                        	 PULONG_ pul_TimingInterval, |
|						 unsigned char *_   pb_InputMode,      |
|						 unsigned char *_   pb_ExternGate,     |
|                                                unsigned char *_   pb_CycleMode,      |
|						 unsigned char *_   pb_Enable,         |
|                                                unsigned char *_   pb_InterruptEnable)|
+----------------------------------------------------------------------------+
| Task              : Enable the tor counter (b_TorCounter) from selected    |
|		      module (b_ModulNbr). You must calling the              |
|                     "i_APCI1710_InitTorCounter" function be for you call   |
|		      this function.                                         |
|                     If you enable the tor counter interrupt, the           |
|                     tor counter generate a interrupt after the timing cycle|
|                     See function "i_APCI1710_SetBoardIntRoutineX" and the  |
|                     Interrupt mask description chapter from this manual.   |
|                     The b_CycleMode parameter determine if you will        |
|                     measured a single or more cycle.                       |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle  : Handle of board APCI-1710       |
|                     unsigned char_ b_ModulNbr     : Selected module number (0 to 3) |
|                     unsigned char_ b_TorCounter   : Tor counter selection (0 or 1)

	b_ModulNbr			=	CR_AREF(insn->chanspec);
	b_TorCounter		=	CR_CHAN(insn->chanspec);
. |
+----------------------------------------------------------------------------+
| Output Parameters : unsigned char *_  pb_TimingUnit    : Base timing unit (0 to 4)   |
|                                                 0 : ns                     |
|                                                 1 : µs                     |
|                                                 2 : ms                     |
|                                                 3 : s                      |
|                                                 4 : mn                     |
|                     PULONG_ pul_TimingInterval : Base timing value.        |
|		      unsigned char *_ pb_InputMode        : Input signal level        |
|						   selection  		     |
|						0 : Tor count each low level |
|						1 : Tor count each high level|
|		      unsigned char *_ pb_ExternGate	: Extern gate action         |
|						  selection                  |
|						  0 : Extern gate signal not |
|						      used                   |
|						  1 : Extern gate signal used|
|                     unsigned char *_ pb_CycleMode       : Tor counter acquisition    |
|						  mode           	     |
|		      unsigned char *_ pb_Enable		: Indicate if the tor counter|
|						  is enabled or no           |
|						  0 : Tor counter disabled   |
|						  1 : Tor counter enabled    |
|                     unsigned char *_ pb_InterruptEnable : Enable or disable the      |
|                                                 tor counter interrupt.     |
|                                                 APCI1710_ENABLE:           |
|                                                 Enable the tor counter     |
|                                                 interrupt                  |
|                                                 APCI1710_DISABLE:          |
|                                                 Disable the tor counter    |
|                                                 interrupt
	pb_TimingUnit		=	(unsigned char *) &data[0];
	pul_TimingInterval	=  (PULONG) &data[1];
	pb_InputMode		=	(unsigned char *) &data[2];
	pb_ExternGate		=	(unsigned char *) &data[3];
	pb_CycleMode		=	(unsigned char *) &data[4];
	pb_Enable			=	(unsigned char *) &data[5];
	pb_InterruptEnable	=	(unsigned char *) &data[6];
                 |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a tor counter module             |
|                     -4: Tor counter selection is wrong                     |
|                     -5: Tor counter not initialised see function           |
|                         "i_APCI1710_InitTorCounter"                        |
+----------------------------------------------------------------------------+
*/

int i_APCI1710_InsnReadGetTorCounterInitialisation(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_ReturnValue = 0;
	DWORD dw_Status;
	unsigned char b_ModulNbr;
	unsigned char b_TorCounter;
	unsigned char * pb_TimingUnit;
	PULONG pul_TimingInterval;
	unsigned char * pb_InputMode;
	unsigned char * pb_ExternGate;
	unsigned char * pb_CycleMode;
	unsigned char * pb_Enable;
	unsigned char * pb_InterruptEnable;

	i_ReturnValue = insn->n;
	b_ModulNbr = CR_AREF(insn->chanspec);
	b_TorCounter = CR_CHAN(insn->chanspec);

	pb_TimingUnit = (unsigned char *) & data[0];
	pul_TimingInterval = (PULONG) & data[1];
	pb_InputMode = (unsigned char *) & data[2];
	pb_ExternGate = (unsigned char *) & data[3];
	pb_CycleMode = (unsigned char *) & data[4];
	pb_Enable = (unsigned char *) & data[5];
	pb_InterruptEnable = (unsigned char *) & data[6];

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if tor counter */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_TOR_COUNTER) {
	      /**********************************/
			/* Test the tor counter selection */
	      /**********************************/

			if (b_TorCounter <= 1) {

		 /***********************************/
				/* Test if tor counter initialised */
		 /***********************************/

				dw_Status = inl(devpriv->s_BoardInfos.
					ui_Address + 8 + (16 * b_TorCounter) +
					(64 * b_ModulNbr));

				if (dw_Status & 0x10) {
					*pb_Enable = dw_Status & 1;

		    /********************/
					/* Get the commando */
		    /********************/

					dw_Status = inl(devpriv->s_BoardInfos.
						ui_Address + 4 +
						(16 * b_TorCounter) +
						(64 * b_ModulNbr));

					*pb_CycleMode =
						(unsigned char) ((dw_Status >> 4) & 1);
					*pb_InterruptEnable =
						(unsigned char) ((dw_Status >> 5) & 1);

		    /******************************************************/
					/* Test if extern gate used for clock or for signal B */
		    /******************************************************/

					if (dw_Status & 0x600) {
		       /*****************************************/
						/* Test if extern gate used for signal B */
		       /*****************************************/

						if (dw_Status & 0x400) {
			  /***********************/
							/* Test if simple mode */
			  /***********************/

							if ((dw_Status & 0x7800)
								== 0x7800) {
								*pb_InputMode =
									APCI1710_TOR_SIMPLE_MODE;
							}

			  /***********************/
							/* Test if double mode */
			  /***********************/

							if ((dw_Status & 0x7800)
								== 0x1800) {
								*pb_InputMode =
									APCI1710_TOR_DOUBLE_MODE;
							}

			  /**************************/
							/* Test if quadruple mode */
			  /**************************/

							if ((dw_Status & 0x7800)
								== 0x0000) {
								*pb_InputMode =
									APCI1710_TOR_QUADRUPLE_MODE;
							}
						}	// if (dw_Status & 0x400)
						else {
							*pb_InputMode = 1;
						}	// // if (dw_Status & 0x400)

		       /************************/
						/* Extern gate not used */
		       /************************/

						*pb_ExternGate = 0;
					}	// if (dw_Status & 0x600)
					else {
						*pb_InputMode =
							(unsigned char) ((dw_Status >> 6)
							& 1);
						*pb_ExternGate =
							(unsigned char) ((dw_Status >> 7)
							& 1);
					}	// if (dw_Status & 0x600)

					*pb_TimingUnit =
						devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_TorCounterModuleInfo.
						s_TorCounterInfo[b_TorCounter].
						b_TimingUnit;

					*pul_TimingInterval =
						devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_TorCounterModuleInfo.
						s_TorCounterInfo[b_TorCounter].
						ul_RealTimingInterval;
				} else {
		    /*******************************/
					/* Tor counter not initialised */
		    /*******************************/

					DPRINTK("Tor counter not initialised\n");
					i_ReturnValue = -5;
				}

			}	// if (b_TorCounter <= 1)
			else {
		 /**********************************/
				/* Tor counter selection is wrong */
		 /**********************************/

				DPRINTK("Tor counter selection is wrong \n");
				i_ReturnValue = -4;
			}	// if (b_TorCounter <= 1)
		} else {
	      /******************************************/
			/* The module is not a tor counter module */
	      /******************************************/

			DPRINTK("The module is not a tor counter module\n");
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
| Function Name     : _INT_ i_APCI1710_ReadTorCounterValue                   |
|                               (unsigned char_     b_BoardHandle,                    |
|                                unsigned char_     b_ModulNbr,                       |
|				 unsigned char_     b_TorCounter,                     |
|                                unsigned int_    ui_TimeOut,                        |
|                                unsigned char *_   pb_TorCounterStatus,               |
|                                PULONG_ pul_TorCounterValue)                |
+----------------------------------------------------------------------------+
| Task        	case APCI1710_TOR_GETPROGRESSSTATUS: Return the tor counter
(b_TorCounter) status (pb_TorCounterStatus) from selected tor counter        |
|		      module (b_ModulNbr).

				 case APCI1710_TOR_GETCOUNTERVALUE :
  Return the tor counter (b_TorCounter) status           |
|		      (pb_TorCounterStatus) and the timing value             |
|		      (pul_TorCounterValue) after a conting cycle stop       |
|                     from selected tor counter module (b_ModulNbr).         |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle  : Handle of board APCI-1710       |
|                     unsigned char_ b_ModulNbr     : Selected module number (0 to 3) |
|                     unsigned char_ b_TorCounter   : Tor counter selection (0 or 1).
	b_ModulNbr    = CR_AREF(insn->chanspec);
	b_ReadType    = (unsigned char) data[0];
	b_TorCounter  =	(unsigned char) data[1];
	ui_TimeOut	  = (unsigned int) data[2]; |
+----------------------------------------------------------------------------+
| Output Parameters : unsigned char *_  pb_TorCounterStatus : Return the tor counter   |
|                                                    status.                 |
|                                               0 : Conting cycle not started|
|                                                   Software gate not set.   |
|                                               1 : Conting cycle started.   |
|                                                   Software gate set.       |
|                                               2 : Conting cycle stopped.   |
|                                                   The conting cycle is     |
|                                                   terminate.               |
|                                               3 : A overflow occur. You    |
|                                                   must change the base     |
|                                                   timing witch the         |
|                                                   function                 |
|                                                 "i_APCI1710_InitTorCounter"|
|						4 : Timeeout occur           |
|                     PULONG  pul_TorCounterValue  : Tor counter value.
	pb_TorCounterStatus=(unsigned char *) &data[0];
	pul_TorCounterValue=(PULONG) &data[1];    |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection wrong                             |
|                     -3: The module is not a tor counter module             |
|                     -4: Tor counter selection is wrong                     |
|                     -5: Tor counter not initialised see function           |
|                         "i_APCI1710_InitTorCounter"                        |
|                     -6: Tor counter not enabled see function               |
|                         "i_APCI1710_EnableTorCounter"                      |
|                     -7: Timeout parameter is wrong (0 to 65535)            |
+----------------------------------------------------------------------------+
*/

int i_APCI1710_InsnBitsGetTorCounterProgressStatusAndValue(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_ReturnValue = 0;
	DWORD dw_Status;
	DWORD dw_TimeOut = 0;

	unsigned char b_ModulNbr;
	unsigned char b_TorCounter;
	unsigned char b_ReadType;
	unsigned int ui_TimeOut;
	unsigned char * pb_TorCounterStatus;
	PULONG pul_TorCounterValue;

	i_ReturnValue = insn->n;
	b_ModulNbr = CR_AREF(insn->chanspec);
	b_ReadType = (unsigned char) data[0];
	b_TorCounter = (unsigned char) data[1];
	ui_TimeOut = (unsigned int) data[2];
	pb_TorCounterStatus = (unsigned char *) & data[0];
	pul_TorCounterValue = (PULONG) & data[1];

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ReadType == APCI1710_TOR_READINTERRUPT) {

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

		return insn->n;
	}

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if tor counter */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_TOR_COUNTER) {
	      /**********************************/
			/* Test the tor counter selection */
	      /**********************************/

			if (b_TorCounter <= 1) {
		 /***********************************/
				/* Test if tor counter initialised */
		 /***********************************/

				dw_Status = inl(devpriv->s_BoardInfos.
					ui_Address + 8 + (16 * b_TorCounter) +
					(64 * b_ModulNbr));

		 /*******************************/
				/* Test if counter initialised */
		 /*******************************/

				if (dw_Status & 0x10) {
		    /***************************/
					/* Test if counter enabled */
		    /***************************/

					if (dw_Status & 0x1) {

						switch (b_ReadType) {

						case APCI1710_TOR_GETPROGRESSSTATUS:
		       /*******************/
							/* Read the status */
		       /*******************/

							dw_Status =
								inl(devpriv->
								s_BoardInfos.
								ui_Address + 4 +
								(16 * b_TorCounter) + (64 * b_ModulNbr));

							dw_Status =
								dw_Status & 0xF;

		       /*****************/
							/* Test if start */
		       /*****************/

							if (dw_Status & 1) {
								if (dw_Status &
									2) {
									if (dw_Status & 4) {
				/************************/
										/* Tor counter owerflow */
				/************************/

										*pb_TorCounterStatus
											=
											3;
									} else {
				/***********************/
										/* Tor counter started */
				/***********************/

										*pb_TorCounterStatus
											=
											2;
									}
								} else {
			     /***********************/
									/* Tor counter started */
			     /***********************/

									*pb_TorCounterStatus
										=
										1;
								}
							} else {
			  /***************************/
								/* Tor counter not started */
			  /***************************/

								*pb_TorCounterStatus
									= 0;
							}
							break;

						case APCI1710_TOR_GETCOUNTERVALUE:

		       /*****************************/
							/* Test the timout parameter */
		       /*****************************/

							if ((ui_TimeOut >= 0)
								&& (ui_TimeOut
									<=
									65535UL))
							{
								for (;;) {
			     /*******************/
									/* Read the status */
			     /*******************/

									dw_Status
										=
										inl
										(devpriv->
										s_BoardInfos.
										ui_Address
										+
										4
										+
										(16 * b_TorCounter) + (64 * b_ModulNbr));
			     /********************/
									/* Test if overflow */
			     /********************/

									if ((dw_Status & 4) == 4) {
				/******************/
										/* Overflow occur */
				/******************/

										*pb_TorCounterStatus
											=
											3;

				/******************/
										/* Read the value */
				/******************/

										*pul_TorCounterValue
											=
											inl
											(devpriv->
											s_BoardInfos.
											ui_Address
											+
											0
											+
											(16 * b_TorCounter) + (64 * b_ModulNbr));
										break;
									}	// if ((dw_Status & 4) == 4)
									else {
				/*******************************/
										/* Test if measurement stopped */
				/*******************************/

										if ((dw_Status & 2) == 2) {
				   /***********************/
											/* A stop signal occur */
				   /***********************/

											*pb_TorCounterStatus
												=
												2;

				   /******************/
											/* Read the value */
				   /******************/

											*pul_TorCounterValue
												=
												inl
												(devpriv->
												s_BoardInfos.
												ui_Address
												+
												0
												+
												(16 * b_TorCounter) + (64 * b_ModulNbr));

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

												*pb_TorCounterStatus
													=
													1;
											}	// if ((dw_Status & 1) == 1)
											else {
				      /***************************/
												/* Measurement not started */
				      /***************************/

												*pb_TorCounterStatus
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

										dw_TimeOut
											=
											dw_TimeOut
											+
											1;

										mdelay(1000);
									}
								}	// for (;;)

			  /*************************/
								/* Test if timeout occur */
			  /*************************/

								if ((*pb_TorCounterStatus != 3) && (dw_TimeOut == ui_TimeOut) && (ui_TimeOut != 0)) {
			     /*****************/
									/* Timeout occur */
			     /*****************/

									*pb_TorCounterStatus
										=
										4;
								}
							} else {
			  /******************************/
								/* Timeout parameter is wrong */
			  /******************************/

								DPRINTK("Timeout parameter is wrong\n");
								i_ReturnValue =
									-7;
							}
							break;

						default:
							printk("Inputs wrong\n");
						}	// switch end
					}	// if (dw_Status & 0x1)
					else {
		       /***************************/
						/* Tor counter not enabled */
		       /***************************/

						DPRINTK("Tor counter not enabled\n");
						i_ReturnValue = -6;
					}	// if (dw_Status & 0x1)
				} else {
		    /*******************************/
					/* Tor counter not initialised */
		    /*******************************/

					DPRINTK("Tor counter not initialised\n");
					i_ReturnValue = -5;
				}
			}	// if (b_TorCounter <= 1)
			else {
		 /**********************************/
				/* Tor counter selection is wrong */
		 /**********************************/

				DPRINTK("Tor counter selection is wrong\n");
				i_ReturnValue = -4;
			}	// if (b_TorCounter <= 1)
		} else {
	      /******************************************/
			/* The module is not a tor counter module */
	      /******************************************/

			DPRINTK("The module is not a tor counter module\n");
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
