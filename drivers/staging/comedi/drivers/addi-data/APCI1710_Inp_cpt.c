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
  | Module name : Inp_CPT.C       | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date     :  02/12/2002                |
  +-----------------------------------------------------------------------+
  | Description :   APCI-1710 pulse encoder module                        |
  |                                                                       |
  |                                                                       |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |          |           |                                                |
  |----------|-----------|------------------------------------------------|
  | 08/05/00 | Guinot C  | - 0400/0228 All Function in RING 0             |
  |          |           |   available                                    |
  +-----------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
|                               Included files                               |
+----------------------------------------------------------------------------+
*/

#include "APCI1710_Inp_cpt.h"

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_InitPulseEncoder                      |
|                               (unsigned char_          b_BoardHandle,               |
|                                unsigned char_          b_ModulNbr,                  |
|                                unsigned char_          b_PulseEncoderNbr,           |
|                                unsigned char_          b_InputLevelSelection,       |
|                                unsigned char_          b_TriggerOutputAction,       |
|                                ULONG_        ul_StartValue)                |
+----------------------------------------------------------------------------+
| Task              : Configure the pulse encoder operating mode selected via|
|                     b_ModulNbr and b_PulseEncoderNbr. The pulse encoder    |
|                     after each pulse decrement the counter value from 1.   |
|                                                                            |
|                     You must calling this function be for you call any     |
|                     other function witch access of pulse encoders.         |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle         : Handle of board APCI-1710|
|                     unsigned char_ b_ModulNbr            : Module number to         |
|                                                   configure (0 to 3)       |
|                     unsigned char_ b_PulseEncoderNbr     : Pulse encoder selection  |
|                                                   (0 to 3)                 |
|                     unsigned char_ b_InputLevelSelection : Input level selection    |
|                                                   (0 or 1)                 |
|                                                       0 : Set pulse encoder|
|                                                           count the the low|
|                                                           level pulse.     |
|                                                       1 : Set pulse encoder|
|                                                           count the the    |
|                                                           high level pulse.|
|                     unsigned char_ b_TriggerOutputAction : Digital TRIGGER output   |
|                                                   action                   |
|                                                       0 : No action        |
|                                                       1 : Set the trigger  |
|                                                           output to "1"    |
|                                                           (high) after the |
|                                                           passage from 1 to|
|                                                           0 from pulse     |
|                                                           encoder.         |
|                                                       2 : Set the trigger  |
|                                                           output to "0"    |
|                                                           (low) after the  |
|                                                           passage from 1 to|
|                                                           0 from pulse     |
|                                                           encoder          |
|                     ULONG_ ul_StartValue        : Pulse encoder start value|
|                                                   (1 to 4294967295)
	b_ModulNbr				=(unsigned char) CR_AREF(insn->chanspec);
	b_PulseEncoderNbr		=(unsigned char) data[0];
	b_InputLevelSelection	=(unsigned char) data[1];
	b_TriggerOutputAction	=(unsigned char) data[2];
	ul_StartValue			=(unsigned int) data[3];
       |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: The module is not a pulse encoder module            |
|                    -3: Pulse encoder selection is wrong                    |
|                    -4: Input level selection is wrong                      |
|                    -5: Digital TRIGGER output action selection is wrong    |
|                    -6: Pulse encoder start value is wrong                  |
+----------------------------------------------------------------------------+
*/

int i_APCI1710_InsnConfigInitPulseEncoder(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = 0;
	unsigned int dw_IntRegister;

	unsigned char b_ModulNbr;
	unsigned char b_PulseEncoderNbr;
	unsigned char b_InputLevelSelection;
	unsigned char b_TriggerOutputAction;
	unsigned int ul_StartValue;

	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_PulseEncoderNbr = (unsigned char) data[0];
	b_InputLevelSelection = (unsigned char) data[1];
	b_TriggerOutputAction = (unsigned char) data[2];
	ul_StartValue = (unsigned int) data[3];

	i_ReturnValue = insn->n;

	/***********************************/
	/* Test the selected module number */
	/***********************************/

	if (b_ModulNbr <= 3) {
	   /*************************/
		/* Test if pulse encoder */
	   /*************************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				APCI1710_PULSE_ENCODER) ==
			APCI1710_PULSE_ENCODER) {
	      /******************************************/
			/* Test the selected pulse encoder number */
	      /******************************************/

			if (b_PulseEncoderNbr <= 3) {
		 /************************/
				/* Test the input level */
		 /************************/

				if ((b_InputLevelSelection == 0)
					|| (b_InputLevelSelection == 1)) {
		    /*******************************************/
					/* Test the ouput TRIGGER action selection */
		    /*******************************************/

					if ((b_TriggerOutputAction <= 2)
						|| (b_PulseEncoderNbr > 0)) {
						if (ul_StartValue > 1) {

							dw_IntRegister =
								inl(devpriv->
								s_BoardInfos.
								ui_Address +
								20 +
								(64 * b_ModulNbr));

			  /***********************/
							/* Set the start value */
			  /***********************/

							outl(ul_StartValue,
								devpriv->
								s_BoardInfos.
								ui_Address +
								(b_PulseEncoderNbr
									* 4) +
								(64 * b_ModulNbr));

			  /***********************/
							/* Set the input level */
			  /***********************/
							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_PulseEncoderModuleInfo.
								dw_SetRegister =
								(devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_PulseEncoderModuleInfo.
								dw_SetRegister &
								(0xFFFFFFFFUL -
									(1UL << (8 + b_PulseEncoderNbr)))) | ((1UL & (~b_InputLevelSelection)) << (8 + b_PulseEncoderNbr));

			  /*******************************/
							/* Test if output trigger used */
			  /*******************************/

							if ((b_TriggerOutputAction > 0) && (b_PulseEncoderNbr > 1)) {
			     /****************************/
								/* Enable the output action */
			     /****************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									=
									devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									| (1UL
									<< (4 + b_PulseEncoderNbr));

			     /*********************************/
								/* Set the output TRIGGER action */
			     /*********************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									=
									(devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									&
									(0xFFFFFFFFUL
										-
										(1UL << (12 + b_PulseEncoderNbr)))) | ((1UL & (b_TriggerOutputAction - 1)) << (12 + b_PulseEncoderNbr));
							} else {
			     /*****************************/
								/* Disable the output action */
			     /*****************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									=
									devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									&
									(0xFFFFFFFFUL
									-
									(1UL << (4 + b_PulseEncoderNbr)));
							}

			  /*************************/
							/* Set the configuration */
			  /*************************/

							outl(devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_PulseEncoderModuleInfo.
								dw_SetRegister,
								devpriv->
								s_BoardInfos.
								ui_Address +
								20 +
								(64 * b_ModulNbr));

							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_PulseEncoderModuleInfo.
								s_PulseEncoderInfo
								[b_PulseEncoderNbr].
								b_PulseEncoderInit
								= 1;
						} else {
			  /**************************************/
							/* Pulse encoder start value is wrong */
			  /**************************************/

							DPRINTK("Pulse encoder start value is wrong\n");
							i_ReturnValue = -6;
						}
					} else {
		       /****************************************************/
						/* Digital TRIGGER output action selection is wrong */
		       /****************************************************/

						DPRINTK("Digital TRIGGER output action selection is wrong\n");
						i_ReturnValue = -5;
					}
				} else {
		    /**********************************/
					/* Input level selection is wrong */
		    /**********************************/

					DPRINTK("Input level selection is wrong\n");
					i_ReturnValue = -4;
				}
			} else {
		 /************************************/
				/* Pulse encoder selection is wrong */
		 /************************************/

				DPRINTK("Pulse encoder selection is wrong\n");
				i_ReturnValue = -3;
			}
		} else {
	      /********************************************/
			/* The module is not a pulse encoder module */
	      /********************************************/

			DPRINTK("The module is not a pulse encoder module\n");
			i_ReturnValue = -2;
		}
	} else {
	   /********************************************/
		/* The module is not a pulse encoder module */
	   /********************************************/

		DPRINTK("The module is not a pulse encoder module\n");
		i_ReturnValue = -2;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_EnablePulseEncoder                    |
|                                       (unsigned char_  b_BoardHandle,               |
|                                        unsigned char_  b_ModulNbr,                  |
|                                        unsigned char_  b_PulseEncoderNbr,           |
|                                        unsigned char_  b_CycleSelection,            |
|                                        unsigned char_  b_InterruptHandling)         |
+----------------------------------------------------------------------------+
| Task              : Enableor disable  the selected pulse encoder (b_PulseEncoderNbr)  |
|                     from selected module (b_ModulNbr). Each input pulse    |
|                     decrement the pulse encoder counter value from 1.      |
|                     If you enabled the interrupt (b_InterruptHandling), a  |
|                     interrupt is generated when the pulse encoder has run  |
|                     down.                                                  |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_   b_BoardHandle       : Handle of board APCI-1710|
|                     unsigned char_   b_ModulNbr          : Module number to         |
|                                                   configure (0 to 3)       |
|                     unsigned char_   b_PulseEncoderNbr   : Pulse encoder selection  |
|                                                   (0 to 3)                 |
|                     unsigned char_   b_CycleSelection    : APCI1710_CONTINUOUS:     |
|                                                       Each time the        |
|                                                       counting value is set|
|                                                       on "0", the pulse    |
|                                                       encoder load the     |
|                                                       start value after    |
|                                                       the next pulse.      |
|                                                   APCI1710_SINGLE:         |
|                                                       If the counter is set|
|                                                       on "0", the pulse    |
|                                                       encoder is stopped.  |
|                     unsigned char_   b_InterruptHandling : Interrupts can be        |
|                                                   generated, when the pulse|
|                                                   encoder has run down.    |
|                                                   With this parameter the  |
|                                                   user decides if          |
|                                                   interrupts are used or   |
|                                                   not.                     |
|                                                     APCI1710_ENABLE:       |
|                                                     Interrupts are enabled |
|                                                     APCI1710_DISABLE:      |
|                                                     Interrupts are disabled

	b_ModulNbr			=(unsigned char) CR_AREF(insn->chanspec);
	b_Action			=(unsigned char) data[0];
	b_PulseEncoderNbr	=(unsigned char) data[1];
	b_CycleSelection	=(unsigned char) data[2];
	b_InterruptHandling	=(unsigned char) data[3];|
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection is wrong                          |
|                     -3: Pulse encoder selection is wrong                   |
|                     -4: Pulse encoder not initialised.                     |
|                         See function "i_APCI1710_InitPulseEncoder"         |
|                     -5: Cycle selection mode is wrong                      |
|                     -6: Interrupt handling mode is wrong                   |
|                     -7: Interrupt routine not installed.                   |
|                         See function "i_APCI1710_SetBoardIntRoutineX"      |
+----------------------------------------------------------------------------+
*/

int i_APCI1710_InsnWriteEnableDisablePulseEncoder(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = 0;
	unsigned char b_ModulNbr;
	unsigned char b_PulseEncoderNbr;
	unsigned char b_CycleSelection;
	unsigned char b_InterruptHandling;
	unsigned char b_Action;

	i_ReturnValue = insn->n;
	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_Action = (unsigned char) data[0];
	b_PulseEncoderNbr = (unsigned char) data[1];
	b_CycleSelection = (unsigned char) data[2];
	b_InterruptHandling = (unsigned char) data[3];

	/***********************************/
	/* Test the selected module number */
	/***********************************/

	if (b_ModulNbr <= 3) {
	   /******************************************/
		/* Test the selected pulse encoder number */
	   /******************************************/

		if (b_PulseEncoderNbr <= 3) {
	      /*************************************/
			/* Test if pulse encoder initialised */
	      /*************************************/

			if (devpriv->s_ModuleInfo[b_ModulNbr].
				s_PulseEncoderModuleInfo.
				s_PulseEncoderInfo[b_PulseEncoderNbr].
				b_PulseEncoderInit == 1) {
				switch (b_Action) {

				case APCI1710_ENABLE:
		 /****************************/
					/* Test the cycle selection */
		 /****************************/

					if (b_CycleSelection ==
						APCI1710_CONTINUOUS
						|| b_CycleSelection ==
						APCI1710_SINGLE) {
		    /*******************************/
						/* Test the interrupt handling */
		    /*******************************/

						if (b_InterruptHandling ==
							APCI1710_ENABLE
							|| b_InterruptHandling
							== APCI1710_DISABLE) {
		       /******************************/
							/* Test if interrupt not used */
		       /******************************/

							if (b_InterruptHandling
								==
								APCI1710_DISABLE)
							{
			  /*************************/
								/* Disable the interrupt */
			  /*************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									=
									devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									&
									(0xFFFFFFFFUL
									-
									(1UL << b_PulseEncoderNbr));
							} else {

			     /************************/
								/* Enable the interrupt */
			     /************************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									=
									devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister
									| (1UL
									<<
									b_PulseEncoderNbr);
								devpriv->tsk_Current = current;	/*  Save the current process task structure */

							}

							if (i_ReturnValue >= 0) {
			  /***********************************/
								/* Enable or disable the interrupt */
			  /***********************************/

								outl(devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_SetRegister,
									devpriv->
									s_BoardInfos.
									ui_Address
									+ 20 +
									(64 * b_ModulNbr));

			  /****************************/
								/* Enable the pulse encoder */
			  /****************************/
								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_ControlRegister
									=
									devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_ControlRegister
									| (1UL
									<<
									b_PulseEncoderNbr);

			  /**********************/
								/* Set the cycle mode */
			  /**********************/

								devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_ControlRegister
									=
									(devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_ControlRegister
									&
									(0xFFFFFFFFUL
										-
										(1 << (b_PulseEncoderNbr + 4)))) | ((b_CycleSelection & 1UL) << (4 + b_PulseEncoderNbr));

			  /****************************/
								/* Enable the pulse encoder */
			  /****************************/

								outl(devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_PulseEncoderModuleInfo.
									dw_ControlRegister,
									devpriv->
									s_BoardInfos.
									ui_Address
									+ 16 +
									(64 * b_ModulNbr));
							}
						} else {
		       /************************************/
							/* Interrupt handling mode is wrong */
		       /************************************/

							DPRINTK("Interrupt handling mode is wrong\n");
							i_ReturnValue = -6;
						}
					} else {
		    /*********************************/
						/* Cycle selection mode is wrong */
		    /*********************************/

						DPRINTK("Cycle selection mode is wrong\n");
						i_ReturnValue = -5;
					}
					break;

				case APCI1710_DISABLE:
					devpriv->s_ModuleInfo[b_ModulNbr].
						s_PulseEncoderModuleInfo.
						dw_ControlRegister =
						devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_PulseEncoderModuleInfo.
						dw_ControlRegister &
						(0xFFFFFFFFUL -
						(1UL << b_PulseEncoderNbr));

		 /*****************************/
					/* Disable the pulse encoder */
		 /*****************************/

					outl(devpriv->s_ModuleInfo[b_ModulNbr].
						s_PulseEncoderModuleInfo.
						dw_ControlRegister,
						devpriv->s_BoardInfos.
						ui_Address + 16 +
						(64 * b_ModulNbr));

					break;
				}	/*  switch End */

			} else {
		 /*********************************/
				/* Pulse encoder not initialised */
		 /*********************************/

				DPRINTK("Pulse encoder not initialised\n");
				i_ReturnValue = -4;
			}
		} else {
	      /************************************/
			/* Pulse encoder selection is wrong */
	      /************************************/

			DPRINTK("Pulse encoder selection is wrong\n");
			i_ReturnValue = -3;
		}
	} else {
	   /*****************************/
		/* Module selection is wrong */
	   /*****************************/

		DPRINTK("Module selection is wrong\n");
		i_ReturnValue = -2;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_ReadPulseEncoderStatus                |
|                                       (unsigned char_  b_BoardHandle,               |
|                                        unsigned char_  b_ModulNbr,                  |
|                                        unsigned char_  b_PulseEncoderNbr,           |
|                                        unsigned char *_ pb_Status)                   |
+----------------------------------------------------------------------------+
| Task    APCI1710_PULSEENCODER_READ          : Reads the pulse encoder status
											and valuefrom selected pulse     |
|                     encoder (b_PulseEncoderNbr) from selected module       |
|                     (b_ModulNbr).                                          |
+----------------------------------------------------------------------------+
	unsigned char   b_Type; data[0]
   APCI1710_PULSEENCODER_WRITE
 Writes a 32-bit value (ul_WriteValue) into the selected|
|                     pulse encoder (b_PulseEncoderNbr) from selected module |
|                     (b_ModulNbr). This operation set the new start pulse   |
|                     encoder value.
 APCI1710_PULSEENCODER_READ
| Input Parameters  : unsigned char_   b_BoardHandle       : Handle of board APCI-1710|
|            CRAREF()         unsigned char_   b_ModulNbr          : Module number to         |
|                                                   configure (0 to 3)       |
|              data[1]       unsigned char_   b_PulseEncoderNbr   : Pulse encoder selection  |
|                                                   (0 to 3)
   APCI1710_PULSEENCODER_WRITE
				data[2]		ULONG_ ul_WriteValue        : 32-bit value to be       |
|                                                   written             |
+----------------------------------------------------------------------------+
| Output Parameters : unsigned char *_ pb_Status            : Pulse encoder status.    |
|                                                       0 : No overflow occur|
|                                                       1 : Overflow occur
						PULONG_ pul_ReadValue       : Pulse encoder value      |  |
+----------------------------------------------------------------------------+
| Return Value      :  0: No error                                           |
|                     -1: The handle parameter of the board is wrong         |
|                     -2: Module selection is wrong                          |
|                     -3: Pulse encoder selection is wrong                   |
|                     -4: Pulse encoder not initialised.                     |
|                         See function "i_APCI1710_InitPulseEncoder"         |
+----------------------------------------------------------------------------+
*/

/*_INT_   i_APCI1710_ReadPulseEncoderStatus       (unsigned char_   b_BoardHandle,
						 unsigned char_   b_ModulNbr,
						 unsigned char_   b_PulseEncoderNbr,

						 unsigned char *_ pb_Status)
						 */
int i_APCI1710_InsnBitsReadWritePulseEncoder(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = 0;
	unsigned int dw_StatusRegister;
	unsigned char b_ModulNbr;
	unsigned char b_PulseEncoderNbr;
	unsigned char *pb_Status;
	unsigned char b_Type;
	unsigned int *pul_ReadValue;
	unsigned int ul_WriteValue;

	i_ReturnValue = insn->n;
	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_Type = (unsigned char) data[0];
	b_PulseEncoderNbr = (unsigned char) data[1];
	pb_Status = (unsigned char *) & data[0];
	pul_ReadValue = (unsigned int *) & data[1];

	/***********************************/
	/* Test the selected module number */
	/***********************************/

	if (b_ModulNbr <= 3) {
	   /******************************************/
		/* Test the selected pulse encoder number */
	   /******************************************/

		if (b_PulseEncoderNbr <= 3) {
	      /*************************************/
			/* Test if pulse encoder initialised */
	      /*************************************/

			if (devpriv->s_ModuleInfo[b_ModulNbr].
				s_PulseEncoderModuleInfo.
				s_PulseEncoderInfo[b_PulseEncoderNbr].
				b_PulseEncoderInit == 1) {

				switch (b_Type) {
				case APCI1710_PULSEENCODER_READ:
		 /****************************/
					/* Read the status register */
		 /****************************/

					dw_StatusRegister =
						inl(devpriv->s_BoardInfos.
						ui_Address + 16 +
						(64 * b_ModulNbr));

					devpriv->s_ModuleInfo[b_ModulNbr].
						s_PulseEncoderModuleInfo.
						dw_StatusRegister = devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_PulseEncoderModuleInfo.
						dw_StatusRegister |
						dw_StatusRegister;

					*pb_Status =
						(unsigned char) (devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_PulseEncoderModuleInfo.
						dw_StatusRegister >> (1 +
							b_PulseEncoderNbr)) & 1;

					devpriv->s_ModuleInfo[b_ModulNbr].
						s_PulseEncoderModuleInfo.
						dw_StatusRegister =
						devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_PulseEncoderModuleInfo.
						dw_StatusRegister &
						(0xFFFFFFFFUL - (1 << (1 +
								b_PulseEncoderNbr)));

		 /******************/
					/* Read the value */
		 /******************/

					*pul_ReadValue =
						inl(devpriv->s_BoardInfos.
						ui_Address +
						(4 * b_PulseEncoderNbr) +
						(64 * b_ModulNbr));
					break;

				case APCI1710_PULSEENCODER_WRITE:
					ul_WriteValue = (unsigned int) data[2];
			/*******************/
					/* Write the value */
			/*******************/

					outl(ul_WriteValue,
						devpriv->s_BoardInfos.
						ui_Address +
						(4 * b_PulseEncoderNbr) +
						(64 * b_ModulNbr));

				}	/* end of switch */
			} else {
		 /*********************************/
				/* Pulse encoder not initialised */
		 /*********************************/

				DPRINTK("Pulse encoder not initialised\n");
				i_ReturnValue = -4;
			}
		} else {
	      /************************************/
			/* Pulse encoder selection is wrong */
	      /************************************/

			DPRINTK("Pulse encoder selection is wrong\n");
			i_ReturnValue = -3;
		}
	} else {
	   /*****************************/
		/* Module selection is wrong */
	   /*****************************/

		DPRINTK("Module selection is wrong\n");
		i_ReturnValue = -2;
	}

	return i_ReturnValue;
}

int i_APCI1710_InsnReadInterruptPulseEncoder(struct comedi_device *dev,
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

	/***************************/
	/* Increment the read FIFO */
	/***************************/

	devpriv->s_InterruptParameters.
		ui_Read = (devpriv->
		s_InterruptParameters.ui_Read + 1) % APCI1710_SAVE_INTERRUPT;

	return insn->n;

}
