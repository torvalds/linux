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
  +-------------------------------+---------------------------------------+
  | Project     : APCI-1500       | Compiler   : GCC                      |
  | Module name : hwdrv_apci1500.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-------------------------------+---------------------------------------+
  | Description :   Hardware Layer Acces For APCI-1500                    |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +----------+-----------+------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |          |           |                                                |
  |          |           |                                                |
  |          |           |                                                |
  +----------+-----------+------------------------------------------------+
*/
#include "hwdrv_apci1500.h"

int i_TimerCounter1Init = 0;
int i_TimerCounter2Init = 0;
int i_WatchdogCounter3Init = 0;
int i_Event1Status = 0, i_Event2Status = 0;
int i_TimerCounterWatchdogInterrupt = 0;
int i_Logic = 0, i_CounterLogic = 0;
int i_InterruptMask = 0;
int i_InputChannel = 0;
int i_TimerCounter1Enabled = 0, i_TimerCounter2Enabled =
	0, i_WatchdogCounter3Enabled = 0;

/*
  +----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_ConfigDigitalInputEvent                 |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : An event can be generated for each port.               |
|                     The first event is related to the first 8 channels     |
|                     (port 1) and the second to the following 6 channels    |
|                     (port 2). An interrupt is generated when one or both   |
|                     events have occurred                                   |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data     : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|			  data[0]            :Number of the input port on        |
|                                         which the event will take place    |
|                                         (1 or 2)
                      data[1]            : The event logic for port 1 has    |
|                                            three possibilities             |
|                                        :0  APCI1500_AND       :This logic  |
|                                                                links       |
|                                                                the inputs  |
|                                                                with an AND |
|                                                                logic.      |
|                                          1 APCI1500_OR        :This logic  |
|                                                                links       |
|                                                                the inputs  |
|                                                                with a      |
|                                                                OR logic.   |
|                                          2    APCI1500_OR_PRIORITY        |
|								:This logic                          |
|                                                                links       |
|                                                                the inputs  |
|                                                                with a      |
|                                                                priority    |
|                                                                OR logic.   |
|                                                                Input 1     |
|                                                                has the     |
|                                                                highest     |
|                                                                priority    |
|                                                                level and   |
|                                                                input   8   |
|                                                                the smallest|
|                                            For the second port the user has|
|                                            1 possibility:                  |
|                                            APCI1500_OR        :This logic  |
|                                                                links       |
|                                                                the inputs  |
|                                                                with a      |
|                                                                polarity    |
|                                                                OR logic    |
|                     data[2]              : These 8-character word for port1|
|                                            and 6-character word for port 2 |
|                                            give the mask of the event.     |
|                                            Each place gives the state      |
|                                            of the input channels and can   |
|                                            have one of these six characters|
|                                                     |
|                                       0  : This input must be on 0         |
|                                       1  : This input must be on 1         |
|                                       2  : This input reacts to            |
|                                            a falling edge                  |
|                                       3  : This input reacts to a          |
|                                            rising edge                     |
|                                       4  : This input reacts to both edges |
|
|								5  : This input is not               |
|                                            used for event   				 |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

int i_APCI1500_ConfigDigitalInputEvent(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_PatternPolarity = 0, i_PatternTransition = 0, i_PatternMask = 0;
	int i_MaxChannel = 0, i_Count = 0, i_EventMask = 0;
	int i_PatternTransitionCount = 0, i_RegValue;
	int i;

      /*************************************************/
	/* Selects the master interrupt control register */
      /*************************************************/
	outb(APCI1500_RW_MASTER_INTERRUPT_CONTROL,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
      /**********************************************/
	/* Disables  the main interrupt on the board */
      /**********************************************/
	outb(0x00, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	if (data[0] == 1) {
		i_MaxChannel = 8;
	}			/*  if (data[0] == 1) */
	else {
		if (data[0] == 2) {
			i_MaxChannel = 6;
		}		/*  if(data[0]==2) */
		else {
			printk("\nThe specified port event  does not exist\n");
			return -EINVAL;
		}		/* else if(data[0]==2) */
	}			/* else  if (data[0] == 1) */
	switch (data[1]) {
	case 0:
		data[1] = APCI1500_AND;
		break;
	case 1:
		data[1] = APCI1500_OR;
		break;
	case 2:
		data[1] = APCI1500_OR_PRIORITY;
		break;
	default:
		printk("\nThe specified interrupt logic does not exist\n");
		return -EINVAL;
	}			/* switch(data[1]); */

	i_Logic = data[1];
	for (i_Count = i_MaxChannel, i = 0; i_Count > 0; i_Count--, i++) {
		i_EventMask = data[2 + i];
		switch (i_EventMask) {
		case 0:
			i_PatternMask =
				i_PatternMask | (1 << (i_MaxChannel - i_Count));
			break;
		case 1:
			i_PatternMask =
				i_PatternMask | (1 << (i_MaxChannel - i_Count));
			i_PatternPolarity =
				i_PatternPolarity | (1 << (i_MaxChannel -
					i_Count));
			break;
		case 2:
			i_PatternMask =
				i_PatternMask | (1 << (i_MaxChannel - i_Count));
			i_PatternTransition =
				i_PatternTransition | (1 << (i_MaxChannel -
					i_Count));
			break;
		case 3:
			i_PatternMask =
				i_PatternMask | (1 << (i_MaxChannel - i_Count));
			i_PatternPolarity =
				i_PatternPolarity | (1 << (i_MaxChannel -
					i_Count));
			i_PatternTransition =
				i_PatternTransition | (1 << (i_MaxChannel -
					i_Count));
			break;
		case 4:
			i_PatternTransition =
				i_PatternTransition | (1 << (i_MaxChannel -
					i_Count));
			break;
		case 5:
			break;
		default:
			printk("\nThe option indicated in the event mask does not exist\n");
			return -EINVAL;
		}		/*  switch(i_EventMask) */
	}			/* for (i_Count = i_MaxChannel; i_Count >0;i_Count --) */

	if (data[0] == 1) {
		    /****************************/
		/* Test the interrupt logic */
		    /****************************/

		if (data[1] == APCI1500_AND ||
			data[1] == APCI1500_OR ||
			data[1] == APCI1500_OR_PRIORITY) {
		       /**************************************/
			/* Tests if a transition was declared */
			/* for a OR PRIORITY logic            */
		       /**************************************/

			if (data[1] == APCI1500_OR_PRIORITY
				&& i_PatternTransition != 0) {
			      /********************************************/
				/* Transition error on an OR PRIORITY logic */
			      /********************************************/
				printk("\nTransition error on an OR PRIORITY logic\n");
				return -EINVAL;
			}	/*  if (data[1]== APCI1500_OR_PRIORITY && i_PatternTransition != 0) */

		       /*************************************/
			/* Tests if more than one transition */
			/* was declared for an AND logic     */
		       /*************************************/

			if (data[1] == APCI1500_AND) {
				for (i_Count = 0; i_Count < 8; i_Count++) {
					i_PatternTransitionCount =
						i_PatternTransitionCount +
						((i_PatternTransition >>
							i_Count) & 0x1);

				}	/* for (i_Count = 0; i_Count < 8; i_Count++) */

				if (i_PatternTransitionCount > 1) {
				  /****************************************/
					/* Transition error on an AND logic     */
				  /****************************************/
					printk("\n Transition error on an AND logic\n");
					return -EINVAL;
				}	/*  if (i_PatternTransitionCount > 1) */
			}	/*  if (data[1]== APCI1500_AND) */

			    /*****************************************************************/
			/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
			    /*****************************************************************/
			outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			/******************/
			/* Disable Port A */
			    /******************/
			outb(0xF0,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			/**********************************************/
			/* Selects the polarity register of port 1    */
			    /**********************************************/
			outb(APCI1500_RW_PORT_A_PATTERN_POLARITY,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(i_PatternPolarity,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

			/*********************************************/
			/* Selects the pattern mask register of      */
			/* port 1                                    */
			    /*********************************************/
			outb(APCI1500_RW_PORT_A_PATTERN_MASK,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(i_PatternMask,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			/********************************************/
			/* Selects the pattern transition register  */
			/* of port 1                                */
			    /********************************************/
			outb(APCI1500_RW_PORT_A_PATTERN_TRANSITION,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(i_PatternTransition,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		      /******************************************/
			/* Selects the mode specification mask    */
			/* register of port 1                     */
			  /******************************************/
			outb(APCI1500_RW_PORT_A_SPECIFICATION,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_RegValue =
				inb(devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		      /******************************************/
			/* Selects the mode specification mask    */
			/* register of port 1                     */
			  /******************************************/
			outb(APCI1500_RW_PORT_A_SPECIFICATION,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		      /**********************/
			/* Port A new mode    */
			  /**********************/

			i_RegValue = (i_RegValue & 0xF9) | data[1] | 0x9;
			outb(i_RegValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

			i_Event1Status = 1;

		      /*****************************************************************/
			/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
			  /*****************************************************************/

			outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		      /*****************/
			/* Enable Port A */
			  /*****************/
			outb(0xF4,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		}		/*  if(data[1]==APCI1500_AND||data[1]==APCI1500_OR||data[1]==APCI1500_OR_PRIORITY) */
		else {
			printk("\nThe choice for interrupt logic does not exist\n");
			return -EINVAL;
		}		/*  else }// if(data[1]==APCI1500_AND||data[1]==APCI1500_OR||data[1]==APCI1500_OR_PRIORITY) */
	}			/*    if (data[0]== 1) */

		 /************************************/
	/* Test if event setting for port 2 */
		 /************************************/

	if (data[0] == 2) {
		    /************************/
		/* Test the event logic */
		    /************************/

		if (data[1] == APCI1500_OR) {
		       /*****************************************************************/
			/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
		       /*****************************************************************/
			outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		       /******************/
			/* Disable Port B */
		       /******************/
			outb(0x74,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		       /****************************************/
			/* Selects the mode specification mask  */
			/* register of port B                   */
		       /****************************************/
			outb(APCI1500_RW_PORT_B_SPECIFICATION,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_RegValue =
				inb(devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		       /******************************************/
			/* Selects the mode specification mask    */
			/* register of port B                     */
		       /******************************************/
			outb(APCI1500_RW_PORT_B_SPECIFICATION,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_RegValue = i_RegValue & 0xF9;
			outb(i_RegValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		       /**********************************/
			/* Selects error channels 1 and 2 */
		       /**********************************/

			i_PatternMask = (i_PatternMask | 0xC0);
			i_PatternPolarity = (i_PatternPolarity | 0xC0);
			i_PatternTransition = (i_PatternTransition | 0xC0);

		       /**********************************************/
			/* Selects the polarity register of port 2    */
		       /**********************************************/
			outb(APCI1500_RW_PORT_B_PATTERN_POLARITY,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(i_PatternPolarity,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		       /**********************************************/
			/* Selects the pattern transition register    */
			/* of port 2                                  */
		       /**********************************************/
			outb(APCI1500_RW_PORT_B_PATTERN_TRANSITION,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(i_PatternTransition,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		       /**********************************************/
			/* Selects the pattern Mask register    */
			/* of port 2                                  */
		       /**********************************************/

			outb(APCI1500_RW_PORT_B_PATTERN_MASK,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(i_PatternMask,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		       /******************************************/
			/* Selects the mode specification mask    */
			/* register of port 2                     */
		       /******************************************/
			outb(APCI1500_RW_PORT_B_SPECIFICATION,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_RegValue =
				inb(devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		       /******************************************/
			/* Selects the mode specification mask    */
			/* register of port 2                     */
		       /******************************************/
			outb(APCI1500_RW_PORT_B_SPECIFICATION,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_RegValue = (i_RegValue & 0xF9) | 4;
			outb(i_RegValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

			i_Event2Status = 1;
		       /*****************************************************************/
			/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
		       /*****************************************************************/

			outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		       /*****************/
			/* Enable Port B */
		       /*****************/

			outb(0xF4,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		}		/*   if (data[1] == APCI1500_OR) */
		else {
			printk("\nThe choice for interrupt logic does not exist\n");
			return -EINVAL;
		}		/* elseif (data[1] == APCI1500_OR) */
	}			/* if(data[0]==2) */

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_StartStopInputEvent                     |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              :  Allows or disallows a port event                      |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|		              unsigned int ui_Channel : Channel number to read       |
|                     unsigned int *data          : Data Pointer to read status  |
                      data[0]                 :0 Start input event
                                               1 Stop input event
                      data[1]                 :No of port (1 or 2)
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI1500_StartStopInputEvent(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	int i_Event1InterruptStatus = 0, i_Event2InterruptStatus =
		0, i_RegValue;
	switch (data[0]) {
	case START:
	      /*************************/
		/* Tests the port number */
	      /*************************/

		if (data[1] == 1 || data[1] == 2) {
		  /***************************/
			/* Test if port 1 selected */
		  /***************************/

			if (data[1] == 1) {
		    /*****************************/
				/* Test if event initialised */
		    /*****************************/
				if (i_Event1Status == 1) {
		       /*****************************************************************/
					/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
		       /*****************************************************************/
					outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /******************/
					/* Disable Port A */
		       /******************/
					outb(0xF0,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
		       /***************************************************/
					/* Selects the command and status register of      */
					/* port 1                                          */
		       /***************************************************/
					outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /*************************************/
					/* Allows the pattern interrupt      */
		       /*************************************/
					outb(0xC0,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
		       /*****************************************************************/
					/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
		       /*****************************************************************/
					outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /*****************/
					/* Enable Port A */
		       /*****************/
					outb(0xF4,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
					i_Event1InterruptStatus = 1;
					outb(APCI1500_RW_PORT_A_SPECIFICATION,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
					i_RegValue =
						inb(devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);

					/* Selects the master interrupt control register */
		       /*************************************************/
					outb(APCI1500_RW_MASTER_INTERRUPT_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /**********************************************/
					/* Authorizes the main interrupt on the board */
		       /**********************************************/
					outb(0xD0,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);

				}	/*  if(i_Event1Status==1) */
				else {
					printk("\nEvent 1 not initialised\n");
					return -EINVAL;
				}	/* else if(i_Event1Status==1) */
			}	/* if (data[1]==1) */
			if (data[1] == 2) {

				if (i_Event2Status == 1) {
			    /*****************************************************************/
					/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
			    /*****************************************************************/
					outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /******************/
					/* Disable Port B */
		       /******************/
					outb(0x74,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
		       /***************************************************/
					/* Selects the command and status register of      */
					/* port 2                                          */
		       /***************************************************/
					outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /*************************************/
					/* Allows the pattern interrupt      */
		       /*************************************/
					outb(0xC0,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
		       /*****************************************************************/
					/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
		       /*****************************************************************/
					outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /*****************/
					/* Enable Port B */
		       /*****************/
					outb(0xF4,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);

					/* Selects the master interrupt control register */
		       /*************************************************/
					outb(APCI1500_RW_MASTER_INTERRUPT_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /**********************************************/
					/* Authorizes the main interrupt on the board */
		       /**********************************************/
					outb(0xD0,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
					i_Event2InterruptStatus = 1;
				}	/*  if(i_Event2Status==1) */
				else {
					printk("\nEvent 2 not initialised\n");
					return -EINVAL;
				}	/* else if(i_Event2Status==1) */
			}	/*  if(data[1]==2) */
		}		/*  if (data[1] == 1 || data[0] == 2) */
		else {
			printk("\nThe port parameter is in error\n");
			return -EINVAL;
		}		/* else if (data[1] == 1 || data[0] == 2) */

		break;

	case STOP:
		  /*************************/
		/* Tests the port number */
		  /*************************/

		if (data[1] == 1 || data[1] == 2) {
		  /***************************/
			/* Test if port 1 selected */
		  /***************************/

			if (data[1] == 1) {
		    /*****************************/
				/* Test if event initialised */
		    /*****************************/
				if (i_Event1Status == 1) {
		       /*****************************************************************/
					/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
		       /*****************************************************************/
					outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /******************/
					/* Disable Port A */
		       /******************/
					outb(0xF0,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
		       /***************************************************/
					/* Selects the command and status register of      */
					/* port 1                                          */
		       /***************************************************/
					outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /*************************************/
					/* Inhibits the pattern interrupt      */
		       /*************************************/
					outb(0xE0,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
		       /*****************************************************************/
					/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
		       /*****************************************************************/
					outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /*****************/
					/* Enable Port A */
		       /*****************/
					outb(0xF4,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
					i_Event1InterruptStatus = 0;
				}	/*  if(i_Event1Status==1) */
				else {
					printk("\nEvent 1 not initialised\n");
					return -EINVAL;
				}	/* else if(i_Event1Status==1) */
			}	/* if (data[1]==1) */
			if (data[1] == 2) {
			 /*****************************/
				/* Test if event initialised */
			 /*****************************/
				if (i_Event2Status == 1) {
			  /*****************************************************************/
					/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
			  /*****************************************************************/
					outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
			  /******************/
					/* Disable Port B */
			  /******************/
					outb(0x74,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
			  /***************************************************/
					/* Selects the command and status register of      */
					/* port 2                                         */
			  /***************************************************/
					outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /*************************************/
					/* Inhibits the pattern interrupt      */
		       /*************************************/
					outb(0xE0,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
		       /*****************************************************************/
					/* Selects the APCI1500_RW_MASTER_CONFIGURATION_CONTROL register */
		       /*****************************************************************/
					outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		       /*****************/
					/* Enable Port B */
		       /*****************/
					outb(0xF4,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
					i_Event2InterruptStatus = 0;
				}	/*  if(i_Event2Status==1) */
				else {
					printk("\nEvent 2 not initialised\n");
					return -EINVAL;
				}	/* else if(i_Event2Status==1) */
			}	/* if(data[1]==2) */

		}		/*  if (data[1] == 1 || data[1] == 2) */
		else {
			printk("\nThe port parameter is in error\n");
			return -EINVAL;
		}		/* else if (data[1] == 1 || data[1] == 2) */
		break;
	default:
		printk("\nThe option of START/STOP logic does not exist\n");
		return -EINVAL;
	}			/* switch(data[0]) */

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_Initialisation                          |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Return the status of the digital input                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|		              unsigned int ui_Channel : Channel number to read       |
|                     unsigned int *data          : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI1500_Initialisation(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	int i_DummyRead = 0;
    /******************/
	/* Software reset */
    /******************/
	i_DummyRead = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	i_DummyRead = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(1, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

 /*****************************************************/
	/* Selects the master configuration control register */
 /*****************************************************/
	outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0xF4, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	/*****************************************************/
	/* Selects the mode specification register of port A */
	/*****************************************************/
	outb(APCI1500_RW_PORT_A_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0x10, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	/* Selects the data path polarity register of port A */
	outb(APCI1500_RW_PORT_A_DATA_PCITCH_POLARITY,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* High level of port A means 1 */
	outb(0xFF, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	/* Selects the data direction register of port A */
	outb(APCI1500_RW_PORT_A_DATA_DIRECTION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* All bits used as inputs */
	outb(0xFF, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of port A */
	outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/*  Selects the command and status register of port A */
	outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates the interrupt management of port A:  */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the handshake specification register of port A */
	outb(APCI1500_RW_PORT_A_HANDSHAKE_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes the register */
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	 /*****************************************************/
	/* Selects the mode specification register of port B */
	 /*****************************************************/
	outb(APCI1500_RW_PORT_B_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0x10, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the data path polarity register of port B */
	outb(APCI1500_RW_PORT_B_DATA_PCITCH_POLARITY,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* A high level of port B means 1 */
	outb(0x7F, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the data direction register of port B */
	outb(APCI1500_RW_PORT_B_DATA_DIRECTION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* All bits used as inputs */
	outb(0xFF, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of port B */
	outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of port B */
	outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates the interrupt management of port B:         */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the handshake specification register of port B */
	outb(APCI1500_RW_PORT_B_HANDSHAKE_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes the register */
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	   /*****************************************************/
	/* Selects the data path polarity register of port C */
	   /*****************************************************/
	outb(APCI1500_RW_PORT_C_DATA_PCITCH_POLARITY,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* High level of port C means 1 */
	outb(0x9, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the data direction register of port C */
	outb(APCI1500_RW_PORT_C_DATA_DIRECTION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* All bits used as inputs except channel 1 */
	outb(0x0E, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the special IO register of port C */
	outb(APCI1500_RW_PORT_C_SPECIAL_IO_CONTROL,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes it */
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	   /******************************************************/
	/* Selects the command and status register of timer 1 */
	   /******************************************************/
	outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of timer 1 */
	outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates the interrupt management of timer 1         */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	   /******************************************************/
	/* Selects the command and status register of timer 2 */
	   /******************************************************/
	outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of timer 2 */
	outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates Timer 2 interrupt management:               */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	  /******************************************************/
	/* Selects the command and status register of timer 3 */
	  /******************************************************/
	outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of Timer 3 */
	outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates interrupt management of timer 3:            */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	 /*************************************************/
	/* Selects the master interrupt control register */
	 /*************************************************/
	outb(APCI1500_RW_MASTER_INTERRUPT_CONTROL,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes all interrupts */
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_ReadMoreDigitalInput                    |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                     struct comedi_insn *insn,unsigned int *data)                      |
+----------------------------------------------------------------------------+
| Task              : Return the status of the Requested digital inputs      |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     unsigned int ui_NoOfChannels    : No Of Channels To be Read    |
|                      unsigned int *data             : Data Pointer
                      data[0]                 : 0 Read a single channel
                                                1 read a port value
                      data[1]                 : port value
+----------------------------------------------------------------------------+
| Output Parameters :	--	data[0]    :The read status value
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

int i_APCI1500_ReadMoreDigitalInput(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	unsigned int ui_PortValue = data[1];
	unsigned int ui_Mask = 0;
	unsigned int ui_Channel;
	unsigned int ui_TmpValue = 0;
	ui_Channel = CR_CHAN(insn->chanspec);

	switch (data[0]) {
	case 0:
		if (ui_Channel >= 0 && ui_Channel <= 15) {
			ui_TmpValue =
				(unsigned int) inw(devpriv->i_IobaseAddon +
				APCI1500_DIGITAL_IP);
			*data = (ui_TmpValue >> ui_Channel) & 0x1;
		}		/* if(ui_Channel >= 0 && ui_Channel <=15) */
		else {
			printk("\nThe channel specification are in error\n");
			return -EINVAL;	/*  "sorry channel spec wrong " */
		}		/* else if(ui_Channel >= 0 && ui_Channel <=15) */
		break;
	case 1:

		*data = (unsigned int) inw(devpriv->i_IobaseAddon +
			APCI1500_DIGITAL_IP);
		switch (ui_Channel) {
		case 2:
			ui_Mask = 3;
			*data = (*data >> (2 * ui_PortValue)) & ui_Mask;
			break;
		case 4:
			ui_Mask = 15;
			*data = (*data >> (4 * ui_PortValue)) & ui_Mask;
			break;
		case 8:
			ui_Mask = 255;
			*data = (*data >> (8 * ui_PortValue)) & ui_Mask;
			break;
		case 15:
			break;

		default:
			printk("\nSpecified channel cannot be read \n");
			return -EINVAL;	/*  "sorry channel spec wrong " */
			break;
		}		/* switch(ui_Channel) */
		break;
	default:
		printk("\nThe specified functionality does not exist\n");
		return -EINVAL;
	}			/* switch(data[0]) */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_ConfigDigitalOutputErrorInterrupt
                      (struct comedi_device *dev,struct comedi_subdevice *s struct comedi_insn
                      *insn,unsigned int *data)                                  |
|				                                                     |
+----------------------------------------------------------------------------+
| Task              : Configures the digital output memory and the digital
                      output error interrupt                                 |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains         |
|                                          configuration parameters as below |
|                      struct comedi_subdevice *s,   :pointer to subdevice structure
                       struct comedi_insn *insn      :pointer to insn structure                                                                                                                |
|					  data[0]  :1:Memory on                          |
|					            0:Memory off                         |
                              data[1]  :1 Enable the voltage error interrupt
|							   :0 Disable the voltage error interrupt 		                                                                                                    |
|																	 |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI1500_ConfigDigitalOutputErrorInterrupt(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	devpriv->b_OutputMemoryStatus = data[0];
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_WriteDigitalOutput                      |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Writes port value  To the selected port                |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     unsigned int ui_NoOfChannels    : No Of Channels To Write      |
|                     unsigned int *data              : Data Pointer to read status  |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

int i_APCI1500_WriteDigitalOutput(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	static unsigned int ui_Temp = 0;
	unsigned int ui_Temp1;

	unsigned int ui_NoOfChannel = CR_CHAN(insn->chanspec);	/*  get the channel */

	if (!devpriv->b_OutputMemoryStatus) {
		ui_Temp = 0;

	}			/* if(!devpriv->b_OutputMemoryStatus ) */
	if (data[3] == 0) {
		if (data[1] == 0) {
			data[0] = (data[0] << ui_NoOfChannel) | ui_Temp;
			outw(data[0],
				devpriv->i_IobaseAddon + APCI1500_DIGITAL_OP);
		}		/* if(data[1]==0) */
		else {
			if (data[1] == 1) {
				switch (ui_NoOfChannel) {

				case 2:
					data[0] =
						(data[0] << (2 *
							data[2])) | ui_Temp;
					break;

				case 4:
					data[0] =
						(data[0] << (4 *
							data[2])) | ui_Temp;
					break;

				case 8:
					data[0] =
						(data[0] << (8 *
							data[2])) | ui_Temp;
					break;

				case 15:
					data[0] = data[0] | ui_Temp;
					break;

				default:
					comedi_error(dev, " chan spec wrong");
					return -EINVAL;	/*  "sorry channel spec wrong " */

				}	/* switch(ui_NoOfChannels) */

				outw(data[0],
					devpriv->i_IobaseAddon +
					APCI1500_DIGITAL_OP);
			}	/*  if(data[1]==1) */
			else {
				printk("\nSpecified channel not supported\n");
			}	/* else if(data[1]==1) */
		}		/* elseif(data[1]==0) */
	}			/* if(data[3]==0) */
	else {
		if (data[3] == 1) {
			if (data[1] == 0) {
				data[0] = ~data[0] & 0x1;
				ui_Temp1 = 1;
				ui_Temp1 = ui_Temp1 << ui_NoOfChannel;
				ui_Temp = ui_Temp | ui_Temp1;
				data[0] =
					(data[0] << ui_NoOfChannel) ^
					0xffffffff;
				data[0] = data[0] & ui_Temp;
				outw(data[0],
					devpriv->i_IobaseAddon +
					APCI1500_DIGITAL_OP);
			}	/* if(data[1]==0) */
			else {
				if (data[1] == 1) {
					switch (ui_NoOfChannel) {

					case 2:
						data[0] = ~data[0] & 0x3;
						ui_Temp1 = 3;
						ui_Temp1 =
							ui_Temp1 << 2 * data[2];
						ui_Temp = ui_Temp | ui_Temp1;
						data[0] =
							((data[0] << (2 *
									data
									[2])) ^
							0xffffffff) & ui_Temp;
						break;

					case 4:
						data[0] = ~data[0] & 0xf;
						ui_Temp1 = 15;
						ui_Temp1 =
							ui_Temp1 << 4 * data[2];
						ui_Temp = ui_Temp | ui_Temp1;
						data[0] =
							((data[0] << (4 *
									data
									[2])) ^
							0xffffffff) & ui_Temp;
						break;

					case 8:
						data[0] = ~data[0] & 0xff;
						ui_Temp1 = 255;
						ui_Temp1 =
							ui_Temp1 << 8 * data[2];
						ui_Temp = ui_Temp | ui_Temp1;
						data[0] =
							((data[0] << (8 *
									data
									[2])) ^
							0xffffffff) & ui_Temp;
						break;

					case 15:
						break;

					default:
						comedi_error(dev,
							" chan spec wrong");
						return -EINVAL;	/*  "sorry channel spec wrong " */

					}	/* switch(ui_NoOfChannels) */

					outw(data[0],
						devpriv->i_IobaseAddon +
						APCI1500_DIGITAL_OP);
				}	/*  if(data[1]==1) */
				else {
					printk("\nSpecified channel not supported\n");
				}	/* else if(data[1]==1) */
			}	/* elseif(data[1]==0) */
		}		/* if(data[3]==1); */
		else {
			printk("\nSpecified functionality does not exist\n");
			return -EINVAL;
		}		/* if else data[3]==1) */
	}			/* if else data[3]==0) */
	ui_Temp = data[0];
	return (insn->n);;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_ConfigCounterTimerWatchdog(comedi_device
                   *dev,struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)|
|				                                                     |
+----------------------------------------------------------------------------+
| Task              : Configures The Watchdog                                |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev      : Driver handle                |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure
                      struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data         : Data Pointer to read status                                                       data[0]                : 2     APCI1500_1_8_KHZ
|                                              1     APCI1500_3_6_KHZ        |
|                                              0     APCI1500_115_KHZ
                      data[1]                : 0     Counter1/Timer1
                                               1     Counter2/Timer2
                                               2     Counter3/Watchdog
                      data[2]                : 0     Counter
                                               1     Timer/Watchdog
                      data[3]                :         This parameter has    |
|                                                      two meanings.         |
|                                                    - If the counter/timer  |
|                                                      is used as a counter  |
|                                                      the limit value of    |
|                                                      the counter is given  |
|                                                                            |
|                                                    - If the counter/timer  |
|                                                      is used as a timer,   |
|                                                      the divider factor    |
|                                                      for the output is     |
|                                                      given.
                       data[4]                 : 0    APCI1500_CONTINUOUS
                                                 1    APCI1500_SINGLE
                       data[5]                 : 0    Software Trigger
                                                 1    Hardware Trigger

                       data[6]                  :0    Software gate
                                                 1    Hardware gate
                       data[7]                  :0    Interrupt Disable
                                                 1    Interrupt Enable
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

int i_APCI1500_ConfigCounterTimerWatchdog(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_TimerCounterMode, i_MasterConfiguration;

	devpriv->tsk_Current = current;

/* Selection of the input clock */
	if (data[0] == 0 || data[0] == 1 || data[0] == 2) {
		outw(data[0], devpriv->i_IobaseAddon + APCI1500_CLK_SELECT);
	}			/*  if(data[0]==0||data[0]==1||data[0]==2) */
	else {
		if (data[0] != 3) {
			printk("\nThe option for input clock selection does not exist\n");
			return -EINVAL;
		}		/*  if(data[0]!=3) */
	}			/* elseif(data[0]==0||data[0]==1||data[0]==2) */
	/* Select the counter/timer */
	switch (data[1]) {
	case COUNTER1:
		/* selecting counter or timer */
		switch (data[2]) {
		case 0:
			data[2] = APCI1500_COUNTER;
			break;
		case 1:
			data[2] = APCI1500_TIMER;
			break;
		default:
			printk("\nThis choice is not a timer nor a counter\n");
			return -EINVAL;
		}		/*  switch(data[2]) */

		/* Selecting  single or continuous mode */
		switch (data[4]) {
		case 0:
			data[4] = APCI1500_CONTINUOUS;
			break;
		case 1:
			data[4] = APCI1500_SINGLE;
			break;
		default:
			printk("\nThis option for single/continuous mode does not exist\n");
			return -EINVAL;
		}		/*  switch(data[4]) */

		i_TimerCounterMode = data[2] | data[4] | 7;
			 /*************************/
		/* Test the reload value */
			 /*************************/

		if ((data[3] >= 0) && (data[3] <= 65535)) {
			if (data[7] == APCI1500_ENABLE
				|| data[7] == APCI1500_DISABLE) {

				/************************************************/
				/* Selects the mode register of timer/counter 1 */
				/************************************************/
				outb(APCI1500_RW_CPT_TMR1_MODE_SPECIFICATION,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				/***********************/
				/* Writes the new mode */
				/***********************/
				outb(i_TimerCounterMode,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				/****************************************************/
				/* Selects the constant register of timer/counter 1 */
				/****************************************************/

				outb(APCI1500_RW_CPT_TMR1_TIME_CST_LOW,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				  /*************************/
				/* Writes the low value  */
				  /*************************/

				outb(data[3],
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				   /****************************************************/
				/* Selects the constant register of timer/counter 1 */
				   /****************************************************/

				outb(APCI1500_RW_CPT_TMR1_TIME_CST_HIGH,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				  /**************************/
				/* Writes the high value  */
				  /**************************/

				data[3] = data[3] >> 8;
				outb(data[3],
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				     /*********************************************/
				/* Selects the master configuration register */
				     /*********************************************/

				outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				     /**********************/
				/* Reads the register */
				     /**********************/

				i_MasterConfiguration =
					inb(devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				       /********************************************************/
				/* Enables timer/counter 1 and triggers timer/counter 1 */
				       /********************************************************/

				i_MasterConfiguration =
					i_MasterConfiguration | 0x40;

				    /*********************************************/
				/* Selects the master configuration register */
				    /*********************************************/
				outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				      /********************************/
				/* Writes the new configuration */
				      /********************************/
				outb(i_MasterConfiguration,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
					 /****************************************/
				/* Selects the commands register of     */
				/* timer/counter 1                      */
					 /****************************************/

				outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				       /***************************/
				/* Disable timer/counter 1 */
				       /***************************/

				outb(0x0,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
					  /****************************************/
				/* Selects the commands register of     */
				/* timer/counter 1                      */
					  /****************************************/
				outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				      /***************************/
				/* Trigger timer/counter 1 */
				      /***************************/
				outb(0x2,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
			}	/* if(data[7]== APCI1500_ENABLE ||data[7]== APCI1500_DISABLE) */
			else {
				printk("\nError in selection of interrupt enable or disable\n");
				return -EINVAL;
			}	/* elseif(data[7]== APCI1500_ENABLE ||data[7]== APCI1500_DISABLE) */
		}		/*  if ((data[3]>= 0) && (data[3] <= 65535)) */
		else {
			printk("\nError in selection of reload value\n");
			return -EINVAL;
		}		/* else if ((data[3]>= 0) && (data[3] <= 65535)) */
		i_TimerCounterWatchdogInterrupt = data[7];
		i_TimerCounter1Init = 1;
		break;

	case COUNTER2:		/* selecting counter or timer */
		switch (data[2]) {
		case 0:
			data[2] = APCI1500_COUNTER;
			break;
		case 1:
			data[2] = APCI1500_TIMER;
			break;
		default:
			printk("\nThis choice is not a timer nor a counter\n");
			return -EINVAL;
		}		/*  switch(data[2]) */

		/* Selecting  single or continuous mode */
		switch (data[4]) {
		case 0:
			data[4] = APCI1500_CONTINUOUS;
			break;
		case 1:
			data[4] = APCI1500_SINGLE;
			break;
		default:
			printk("\nThis option for single/continuous mode does not exist\n");
			return -EINVAL;
		}		/*  switch(data[4]) */

		/* Selecting  software or hardware trigger */
		switch (data[5]) {
		case 0:
			data[5] = APCI1500_SOFTWARE_TRIGGER;
			break;
		case 1:
			data[5] = APCI1500_HARDWARE_TRIGGER;
			break;
		default:
			printk("\nThis choice for software or hardware trigger does not exist\n");
			return -EINVAL;
		}		/*  switch(data[5]) */

		/* Selecting  software or hardware gate */
		switch (data[6]) {
		case 0:
			data[6] = APCI1500_SOFTWARE_GATE;
			break;
		case 1:
			data[6] = APCI1500_HARDWARE_GATE;
			break;
		default:
			printk("\nThis choice for software or hardware gate does not exist\n");
			return -EINVAL;
		}		/*  switch(data[6]) */

		i_TimerCounterMode = data[2] | data[4] | data[5] | data[6] | 7;

			     /*************************/
		/* Test the reload value */
			     /*************************/

		if ((data[3] >= 0) && (data[3] <= 65535)) {
			if (data[7] == APCI1500_ENABLE
				|| data[7] == APCI1500_DISABLE) {

				/************************************************/
				/* Selects the mode register of timer/counter 2 */
				/************************************************/
				outb(APCI1500_RW_CPT_TMR2_MODE_SPECIFICATION,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				/***********************/
				/* Writes the new mode */
				/***********************/
				outb(i_TimerCounterMode,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				/****************************************************/
				/* Selects the constant register of timer/counter 2 */
				/****************************************************/

				outb(APCI1500_RW_CPT_TMR2_TIME_CST_LOW,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				  /*************************/
				/* Writes the low value  */
				  /*************************/

				outb(data[3],
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				   /****************************************************/
				/* Selects the constant register of timer/counter 2 */
				   /****************************************************/

				outb(APCI1500_RW_CPT_TMR2_TIME_CST_HIGH,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				  /**************************/
				/* Writes the high value  */
				  /**************************/

				data[3] = data[3] >> 8;
				outb(data[3],
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				     /*********************************************/
				/* Selects the master configuration register */
				     /*********************************************/

				outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				     /**********************/
				/* Reads the register */
				     /**********************/

				i_MasterConfiguration =
					inb(devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				       /********************************************************/
				/* Enables timer/counter 2 and triggers timer/counter 2 */
				       /********************************************************/

				i_MasterConfiguration =
					i_MasterConfiguration | 0x20;

				    /*********************************************/
				/* Selects the master configuration register */
				    /*********************************************/
				outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				      /********************************/
				/* Writes the new configuration */
				      /********************************/
				outb(i_MasterConfiguration,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
					 /****************************************/
				/* Selects the commands register of     */
				/* timer/counter 2                      */
					 /****************************************/

				outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				       /***************************/
				/* Disable timer/counter 2 */
				       /***************************/

				outb(0x0,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
					  /****************************************/
				/* Selects the commands register of     */
				/* timer/counter 2                      */
					  /****************************************/
				outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				      /***************************/
				/* Trigger timer/counter 1 */
				      /***************************/
				outb(0x2,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
			}	/* if(data[7]== APCI1500_ENABLE ||data[7]== APCI1500_DISABLE) */
			else {
				printk("\nError in selection of interrupt enable or disable\n");
				return -EINVAL;
			}	/* elseif(data[7]== APCI1500_ENABLE ||data[7]== APCI1500_DISABLE) */
		}		/*  if ((data[3]>= 0) && (data[3] <= 65535)) */
		else {
			printk("\nError in selection of reload value\n");
			return -EINVAL;
		}		/* else if ((data[3]>= 0) && (data[3] <= 65535)) */
		i_TimerCounterWatchdogInterrupt = data[7];
		i_TimerCounter2Init = 1;
		break;

	case COUNTER3:		/* selecting counter or watchdog */
		switch (data[2]) {
		case 0:
			data[2] = APCI1500_COUNTER;
			break;
		case 1:
			data[2] = APCI1500_WATCHDOG;
			break;
		default:
			printk("\nThis choice is not a watchdog nor a counter\n");
			return -EINVAL;
		}		/*  switch(data[2]) */

		/* Selecting  single or continuous mode */
		switch (data[4]) {
		case 0:
			data[4] = APCI1500_CONTINUOUS;
			break;
		case 1:
			data[4] = APCI1500_SINGLE;
			break;
		default:
			printk("\nThis option for single/continuous mode does not exist\n");
			return -EINVAL;
		}		/*  switch(data[4]) */

		/* Selecting  software or hardware gate */
		switch (data[6]) {
		case 0:
			data[6] = APCI1500_SOFTWARE_GATE;
			break;
		case 1:
			data[6] = APCI1500_HARDWARE_GATE;
			break;
		default:
			printk("\nThis choice for software or hardware gate does not exist\n");
			return -EINVAL;
		}		/*  switch(data[6]) */

		      /*****************************/
		/* Test if used for watchdog */
			  /*****************************/

		if (data[2] == APCI1500_WATCHDOG) {
			     /*****************************/
			/* - Enables the output line */
			/* - Enables retrigger       */
			/* - Pulses output           */
			     /*****************************/
			i_TimerCounterMode = data[2] | data[4] | 0x54;
		}		/* if (data[2] == APCI1500_WATCHDOG) */
		else {
			i_TimerCounterMode = data[2] | data[4] | data[6] | 7;
		}		/* elseif (data[2] == APCI1500_WATCHDOG) */
				 /*************************/
		/* Test the reload value */
			     /*************************/

		if ((data[3] >= 0) && (data[3] <= 65535)) {
			if (data[7] == APCI1500_ENABLE
				|| data[7] == APCI1500_DISABLE) {

				/************************************************/
				/* Selects the mode register of watchdog/counter 3 */
				/************************************************/
				outb(APCI1500_RW_CPT_TMR3_MODE_SPECIFICATION,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				/***********************/
				/* Writes the new mode */
				/***********************/
				outb(i_TimerCounterMode,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				/****************************************************/
				/* Selects the constant register of watchdog/counter 3 */
				/****************************************************/

				outb(APCI1500_RW_CPT_TMR3_TIME_CST_LOW,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				  /*************************/
				/* Writes the low value  */
				  /*************************/

				outb(data[3],
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				   /****************************************************/
				/* Selects the constant register of watchdog/counter 3 */
				   /****************************************************/

				outb(APCI1500_RW_CPT_TMR3_TIME_CST_HIGH,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				  /**************************/
				/* Writes the high value  */
				  /**************************/

				data[3] = data[3] >> 8;
				outb(data[3],
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				     /*********************************************/
				/* Selects the master configuration register */
				     /*********************************************/

				outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				     /**********************/
				/* Reads the register */
				     /**********************/

				i_MasterConfiguration =
					inb(devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				       /********************************************************/
				/* Enables watchdog/counter 3 and triggers watchdog/counter 3 */
				       /********************************************************/

				i_MasterConfiguration =
					i_MasterConfiguration | 0x10;

				    /*********************************************/
				/* Selects the master configuration register */
				    /*********************************************/
				outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				      /********************************/
				/* Writes the new configuration */
				      /********************************/
				outb(i_MasterConfiguration,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				      /********************/
				/* Test if COUNTER */
					  /********************/
				if (data[2] == APCI1500_COUNTER) {

					    /*************************************/
					/* Selects the command register of   */
					/* watchdog/counter 3                */
						 /*************************************/
					outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
					      /*************************************************/
					/* Disable the  watchdog/counter 3 and starts it */
						  /*************************************************/
					outb(0x0,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);

					      /*************************************/
					/* Selects the command register of   */
					/* watchdog/counter 3                */
						  /*************************************/

					outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
					     /*************************************************/
					/* Trigger the  watchdog/counter 3 and starts it */
						 /*************************************************/
					outb(0x2,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);

				}	/* elseif(data[2]==APCI1500_COUNTER) */

			}	/* if(data[7]== APCI1500_ENABLE ||data[7]== APCI1500_DISABLE) */
			else {
				printk("\nError in selection of interrupt enable or disable\n");
				return -EINVAL;
			}	/* elseif(data[7]== APCI1500_ENABLE ||data[7]== APCI1500_DISABLE) */
		}		/*  if ((data[3]>= 0) && (data[3] <= 65535)) */
		else {
			printk("\nError in selection of reload value\n");
			return -EINVAL;
		}		/* else if ((data[3]>= 0) && (data[3] <= 65535)) */
		i_TimerCounterWatchdogInterrupt = data[7];
		i_WatchdogCounter3Init = 1;
		break;

	default:
		printk("\nThe specified counter\timer option does not exist\n");
	}			/* switch(data[1]) */
	i_CounterLogic = data[2];
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_StartStopTriggerTimerCounterWatchdog      |
|				(struct comedi_device *dev,struct comedi_subdevice *s,
                         struct comedi_insn *insn,unsigned int *data);                  |
+----------------------------------------------------------------------------+
| Task              : Start / Stop or trigger the timer counter or Watchdog  |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev     : Driver handle                 |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure
                      struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data         : Data Pointer to read status   |
                      data[0]                : 0     Counter1/Timer1
                                               1     Counter2/Timer2
                                               2     Counter3/Watchdog
                      data[1]                : 0     start
                                               1     stop
                                               2     Trigger
                      data[2]                : 0     Counter
                                               1     Timer/Watchdog
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI1500_StartStopTriggerTimerCounterWatchdog(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_CommandAndStatusValue;

	switch (data[0]) {
	case COUNTER1:
		switch (data[1]) {
		case START:
			if (i_TimerCounter1Init == 1) {
				if (i_TimerCounterWatchdogInterrupt == 1) {
					i_CommandAndStatusValue = 0xC4;	/* Enable the interrupt */
				}	/*  if(i_TimerCounterWatchdogInterrupt==1) */
				else {
					i_CommandAndStatusValue = 0xE4;	/* disable the interrupt */
				}	/* elseif(i_TimerCounterWatchdogInterrupt==1) */
					      /**************************/
				/* Starts timer/counter 1 */
					      /**************************/
				i_TimerCounter1Enabled = 1;
						/********************************************/
				/* Selects the commands and status register */
						/********************************************/
				outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				outb(i_CommandAndStatusValue,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
			}	/* if( i_TimerCounter1Init==1) */
			else {
				printk("\nCounter/Timer1 not configured\n");
				return -EINVAL;
			}
			break;

		case STOP:

					      /**************************/
			/* Stop timer/counter 1 */
					      /**************************/

						/********************************************/
			/* Selects the commands and status register */
						/********************************************/
			outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(0x00,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_TimerCounter1Enabled = 0;
			break;

		case TRIGGER:
			if (i_TimerCounter1Init == 1) {
				if (i_TimerCounter1Enabled == 1) {
						 /************************/
					/* Set Trigger and gate */
						 /************************/

					i_CommandAndStatusValue = 0x6;
				}	/* if( i_TimerCounter1Enabled==1) */
				else {
						   /***************/
					/* Set Trigger */
						   /***************/

					i_CommandAndStatusValue = 0x2;
				}	/* elseif(i_TimerCounter1Enabled==1) */

						/********************************************/
				/* Selects the commands and status register */
						/********************************************/
				outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				outb(i_CommandAndStatusValue,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
			}	/* if( i_TimerCounter1Init==1) */
			else {
				printk("\nCounter/Timer1 not configured\n");
				return -EINVAL;
			}
			break;

		default:
			printk("\nThe specified option for start/stop/trigger does not exist\n");
			return -EINVAL;
		}		/* switch(data[1]) */
		break;

	case COUNTER2:
		switch (data[1]) {
		case START:
			if (i_TimerCounter2Init == 1) {
				if (i_TimerCounterWatchdogInterrupt == 1) {
					i_CommandAndStatusValue = 0xC4;	/* Enable the interrupt */
				}	/*  if(i_TimerCounterWatchdogInterrupt==1) */
				else {
					i_CommandAndStatusValue = 0xE4;	/* disable the interrupt */
				}	/* elseif(i_TimerCounterWatchdogInterrupt==1) */
					      /**************************/
				/* Starts timer/counter 2 */
					      /**************************/
				i_TimerCounter2Enabled = 1;
						/********************************************/
				/* Selects the commands and status register */
						/********************************************/
				outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				outb(i_CommandAndStatusValue,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
			}	/* if( i_TimerCounter2Init==1) */
			else {
				printk("\nCounter/Timer2 not configured\n");
				return -EINVAL;
			}
			break;

		case STOP:

					      /**************************/
			/* Stop timer/counter 2 */
					      /**************************/

						/********************************************/
			/* Selects the commands and status register */
						/********************************************/
			outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(0x00,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_TimerCounter2Enabled = 0;
			break;
		case TRIGGER:
			if (i_TimerCounter2Init == 1) {
				if (i_TimerCounter2Enabled == 1) {
						 /************************/
					/* Set Trigger and gate */
						 /************************/

					i_CommandAndStatusValue = 0x6;
				}	/* if( i_TimerCounter2Enabled==1) */
				else {
						   /***************/
					/* Set Trigger */
						   /***************/

					i_CommandAndStatusValue = 0x2;
				}	/* elseif(i_TimerCounter2Enabled==1) */

						/********************************************/
				/* Selects the commands and status register */
						/********************************************/
				outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				outb(i_CommandAndStatusValue,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
			}	/* if( i_TimerCounter2Init==1) */
			else {
				printk("\nCounter/Timer2 not configured\n");
				return -EINVAL;
			}
			break;
		default:
			printk("\nThe specified option for start/stop/trigger does not exist\n");
			return -EINVAL;
		}		/* switch(data[1]) */
		break;
	case COUNTER3:
		switch (data[1]) {
		case START:
			if (i_WatchdogCounter3Init == 1) {

				if (i_TimerCounterWatchdogInterrupt == 1) {
					i_CommandAndStatusValue = 0xC4;	/* Enable the interrupt */
				}	/*  if(i_TimerCounterWatchdogInterrupt==1) */
				else {
					i_CommandAndStatusValue = 0xE4;	/* disable the interrupt */
				}	/* elseif(i_TimerCounterWatchdogInterrupt==1) */
					      /**************************/
				/* Starts Watchdog/counter 3 */
					      /**************************/
				i_WatchdogCounter3Enabled = 1;
						/********************************************/
				/* Selects the commands and status register */
						/********************************************/
				outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				outb(i_CommandAndStatusValue,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

			}	/*  if( i_WatchdogCounter3init==1) */
			else {
				printk("\nWatchdog/Counter3 not configured\n");
				return -EINVAL;
			}
			break;

		case STOP:

					      /**************************/
			/* Stop Watchdog/counter 3 */
					      /**************************/

						/********************************************/
			/* Selects the commands and status register */
						/********************************************/
			outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(0x00,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_WatchdogCounter3Enabled = 0;
			break;

		case TRIGGER:
			switch (data[2]) {
			case 0:	/* triggering counter 3 */
				if (i_WatchdogCounter3Init == 1) {
					if (i_WatchdogCounter3Enabled == 1) {
							       /************************/
						/* Set Trigger and gate */
							       /************************/

						i_CommandAndStatusValue = 0x6;
					}	/* if( i_WatchdogCounter3Enabled==1) */
					else {
							   /***************/
						/* Set Trigger */
							   /***************/

						i_CommandAndStatusValue = 0x2;
					}	/* elseif(i_WatchdogCounter3Enabled==1) */

						/********************************************/
					/* Selects the commands and status register */
						/********************************************/
					outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
					outb(i_CommandAndStatusValue,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
				}	/* if( i_WatchdogCounter3Init==1) */
				else {
					printk("\nCounter3 not configured\n");
					return -EINVAL;
				}
				break;
			case 1:
				/* triggering Watchdog 3 */
				if (i_WatchdogCounter3Init == 1) {

						/********************************************/
					/* Selects the commands and status register */
						/********************************************/
					outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
					outb(0x6,
						devpriv->iobase +
						APCI1500_Z8536_CONTROL_REGISTER);
				}	/* if( i_WatchdogCounter3Init==1) */
				else {
					printk("\nWatchdog 3 not configured\n");
					return -EINVAL;
				}
				break;
			default:
				printk("\nWrong choice of watchdog/counter3\n");
				return -EINVAL;
			}	/* switch(data[2]) */
			break;
		default:
			printk("\nThe specified option for start/stop/trigger does not exist\n");
			return -EINVAL;
		}		/* switch(data[1]) */
		break;
	default:
		printk("\nThe specified choice for counter/watchdog/timer does not exist\n");
		return -EINVAL;
	}			/* switch(data[0]) */
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_ReadCounterTimerWatchdog                |
|			(struct comedi_device *dev,struct comedi_subdevice *s,struct comedi_insn *insn,
                    unsigned int *data); 	                                     |
+----------------------------------------------------------------------------+
| Task              : Read The Watchdog                                      |
+----------------------------------------------------------------------------+
| Input Parameters  :   struct comedi_device *dev      : Driver handle              |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure
                      struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data          : Data Pointer to read status  |
                      data[0]                : 0     Counter1/Timer1
                                               1     Counter2/Timer2
                                               2     Counter3/Watchdog

+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

int i_APCI1500_ReadCounterTimerWatchdog(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_CommandAndStatusValue;
	switch (data[0]) {
	case COUNTER1:
		/* Read counter/timer1 */
		if (i_TimerCounter1Init == 1) {
			if (i_TimerCounter1Enabled == 1) {
		  /************************/
				/* Set RCC and gate */
		  /************************/

				i_CommandAndStatusValue = 0xC;
			}	/* if( i_TimerCounter1Init==1) */
			else {
		    /***************/
				/* Set RCC */
		    /***************/

				i_CommandAndStatusValue = 0x8;
			}	/* elseif(i_TimerCounter1Init==1) */

		/********************************************/
			/* Selects the commands and status register */
		/********************************************/
			outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(i_CommandAndStatusValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		 /***************************************/
			/* Selects the counter register (high) */
		 /***************************************/
			outb(APCI1500_R_CPT_TMR1_VALUE_HIGH,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			data[0] =
				inb(devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			data[0] = data[0] << 8;
			data[0] = data[0] & 0xff00;
			outb(APCI1500_R_CPT_TMR1_VALUE_LOW,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			data[0] =
				data[0] | inb(devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		}		/* if( i_TimerCounter1Init==1) */
		else {
			printk("\nTimer/Counter1 not configured\n");
			return -EINVAL;
		}		/* elseif( i_TimerCounter1Init==1) */
		break;
	case COUNTER2:
		/* Read counter/timer2 */
		if (i_TimerCounter2Init == 1) {
			if (i_TimerCounter2Enabled == 1) {
		  /************************/
				/* Set RCC and gate */
		  /************************/

				i_CommandAndStatusValue = 0xC;
			}	/* if( i_TimerCounter2Init==1) */
			else {
		    /***************/
				/* Set RCC */
		    /***************/

				i_CommandAndStatusValue = 0x8;
			}	/* elseif(i_TimerCounter2Init==1) */

		/********************************************/
			/* Selects the commands and status register */
		/********************************************/
			outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(i_CommandAndStatusValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		 /***************************************/
			/* Selects the counter register (high) */
		 /***************************************/
			outb(APCI1500_R_CPT_TMR2_VALUE_HIGH,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			data[0] =
				inb(devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			data[0] = data[0] << 8;
			data[0] = data[0] & 0xff00;
			outb(APCI1500_R_CPT_TMR2_VALUE_LOW,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			data[0] =
				data[0] | inb(devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		}		/* if( i_TimerCounter2Init==1) */
		else {
			printk("\nTimer/Counter2 not configured\n");
			return -EINVAL;
		}		/* elseif( i_TimerCounter2Init==1) */
		break;
	case COUNTER3:
		/* Read counter/watchdog2 */
		if (i_WatchdogCounter3Init == 1) {
			if (i_WatchdogCounter3Enabled == 1) {
		  /************************/
				/* Set RCC and gate */
		  /************************/

				i_CommandAndStatusValue = 0xC;
			}	/* if( i_TimerCounter2Init==1) */
			else {
		    /***************/
				/* Set RCC */
		    /***************/

				i_CommandAndStatusValue = 0x8;
			}	/* elseif(i_WatchdogCounter3Init==1) */

		/********************************************/
			/* Selects the commands and status register */
		/********************************************/
			outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			outb(i_CommandAndStatusValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);

		 /***************************************/
			/* Selects the counter register (high) */
		 /***************************************/
			outb(APCI1500_R_CPT_TMR3_VALUE_HIGH,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			data[0] =
				inb(devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			data[0] = data[0] << 8;
			data[0] = data[0] & 0xff00;
			outb(APCI1500_R_CPT_TMR3_VALUE_LOW,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			data[0] =
				data[0] | inb(devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		}		/* if( i_WatchdogCounter3Init==1) */
		else {
			printk("\nWatchdogCounter3 not configured\n");
			return -EINVAL;
		}		/* elseif( i_WatchdogCounter3Init==1) */
		break;
	default:
		printk("\nThe choice of timer/counter/watchdog does not exist\n");
		return -EINVAL;
	}			/* switch(data[0]) */

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int  i_APCI1500_ReadInterruptMask                      |
|			(struct comedi_device *dev,struct comedi_subdevice *s,struct comedi_insn *insn,
                    unsigned int *data); 	                                     |
+----------------------------------------------------------------------------+
| Task              : Read the interrupt mask                                |
+----------------------------------------------------------------------------+
| Input Parameters  :   struct comedi_device *dev      : Driver handle              |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure
                      struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data          : Data Pointer to read status  |


+----------------------------------------------------------------------------+
| Output Parameters :	--	data[0]:The interrupt mask value												                           data[1]:Channel no
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI1500_ReadInterruptMask(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	data[0] = i_InterruptMask;
	data[1] = i_InputChannel;
	i_InterruptMask = 0;
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int  i_APCI1500_ConfigureInterrupt                     |
|			(struct comedi_device *dev,struct comedi_subdevice *s,struct comedi_insn *insn,
                    unsigned int *data); 	                                     |
+----------------------------------------------------------------------------+
| Task              : Configures the interrupt registers                     |
+----------------------------------------------------------------------------+
| Input Parameters  :   struct comedi_device *dev      : Driver handle              |
|                     struct comedi_subdevice *s,   :pointer to subdevice structure
                      struct comedi_insn *insn      :pointer to insn structure      |
|                     unsigned int *data          : Data Pointer                 |


+----------------------------------------------------------------------------+
| Output Parameters :	--
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
int i_APCI1500_ConfigureInterrupt(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	unsigned int ui_Status;
	int i_RegValue;
	int i_Constant;
	devpriv->tsk_Current = current;
	outl(0x0, devpriv->i_IobaseAmcc + 0x38);
	if (data[0] == 1) {
		i_Constant = 0xC0;
	}			/* if(data[0]==1) */
	else {
		if (data[0] == 0) {
			i_Constant = 0x00;
		}		/* if{data[0]==0) */
		else {
			printk("\nThe parameter passed to driver is in error for enabling the voltage interrupt\n");
			return -EINVAL;
		}		/* else if(data[0]==0) */
	}			/* elseif(data[0]==1) */

	 /*****************************************************/
	/* Selects the mode specification register of port B */
	 /*****************************************************/
	outb(APCI1500_RW_PORT_B_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	i_RegValue = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(APCI1500_RW_PORT_B_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
      /*********************************************/
	/* Writes the new configuration (APCI1500_OR) */
      /*********************************************/
	i_RegValue = (i_RegValue & 0xF9) | APCI1500_OR;

	outb(i_RegValue, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
       /*****************************************************/
	/* Selects the command and status register of port B */
       /*****************************************************/
	outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/*****************************************/
	/* Authorises the interrupt on the board */
	/*****************************************/
	outb(0xC0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/***************************************************/
	/* Selects the pattern polarity register of port B */
	/***************************************************/
	outb(APCI1500_RW_PORT_B_PATTERN_POLARITY,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(i_Constant, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/*****************************************************/
	/* Selects the pattern transition register of port B */
	/*****************************************************/
	outb(APCI1500_RW_PORT_B_PATTERN_TRANSITION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(i_Constant, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/***********************************************/
	/* Selects the pattern mask register of port B */
	/***********************************************/
	outb(APCI1500_RW_PORT_B_PATTERN_MASK,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(i_Constant, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	/*****************************************************/
	/* Selects the command and status register of port A */
	/*****************************************************/
	outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	i_RegValue = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	 /***********************************/
	/* Deletes the interrupt of port A */
	 /***********************************/

	i_RegValue = (i_RegValue & 0x0F) | 0x20;
	outb(i_RegValue, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/*****************************************************/
	/* Selects the command and status register of port  B */
	/*****************************************************/
	outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	i_RegValue = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	 /***********************************/
	/* Deletes the interrupt of port B */
	 /***********************************/

	i_RegValue = (i_RegValue & 0x0F) | 0x20;
	outb(i_RegValue, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	/*****************************************************/
	/* Selects the command and status register of timer 1 */
	/*****************************************************/
	outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	i_RegValue = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	 /***********************************/
	/* Deletes the interrupt of timer 1 */
	 /***********************************/

	i_RegValue = (i_RegValue & 0x0F) | 0x20;
	outb(i_RegValue, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	 /*****************************************************/
	/* Selects the command and status register of timer 2 */
	/*****************************************************/
	outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	i_RegValue = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	 /***********************************/
	/* Deletes the interrupt of timer 2 */
	 /***********************************/

	i_RegValue = (i_RegValue & 0x0F) | 0x20;
	outb(i_RegValue, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	/*****************************************************/
	/* Selects the command and status register of timer 3 */
	/*****************************************************/
	outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	i_RegValue = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	 /***********************************/
	/* Deletes the interrupt of timer 3 */
	 /***********************************/

	i_RegValue = (i_RegValue & 0x0F) | 0x20;
	outb(i_RegValue, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	 /*************************************************/
	/* Selects the master interrupt control register */
	 /*************************************************/
	outb(APCI1500_RW_MASTER_INTERRUPT_CONTROL,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/**********************************************/
	/* Authorizes the main interrupt on the board */
	/**********************************************/
	outb(0xD0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

      /***************************/
	/* Enables the PCI interrupt */
      /*****************************/
	outl(0x3000, devpriv->i_IobaseAmcc + 0x38);
	ui_Status = inl(devpriv->i_IobaseAmcc + 0x10);
	ui_Status = inl(devpriv->i_IobaseAmcc + 0x38);
	outl(0x23000, devpriv->i_IobaseAmcc + 0x38);

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : static void v_APCI1500_Interrupt					     |
|					  (int irq , void *d)      |
+----------------------------------------------------------------------------+
| Task              : Interrupt handler                                      |
+----------------------------------------------------------------------------+
| Input Parameters  : int irq                 : irq number                   |
|                     void *d                 : void pointer                 |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static void v_APCI1500_Interrupt(int irq, void *d)
{

	struct comedi_device *dev = d;
	unsigned int ui_InterruptStatus = 0;
	int i_RegValue = 0;
	i_InterruptMask = 0;

 /***********************************/
	/* Read the board interrupt status */
 /***********************************/
	ui_InterruptStatus = inl(devpriv->i_IobaseAmcc + 0x38);

  /***************************************/
	/* Test if board generated a interrupt */
  /***************************************/
	if ((ui_InterruptStatus & 0x800000) == 0x800000) {
      /************************/
		/* Disable all Interrupt */
      /************************/
      /*************************************************/
		/* Selects the master interrupt control register */
      /*************************************************/
		/* outb(APCI1500_RW_MASTER_INTERRUPT_CONTROL,devpriv->iobase+APCI1500_Z8536_CONTROL_REGISTER); */
	/**********************************************/
		/* Disables  the main interrupt on the board */
	/**********************************************/
		/* outb(0x00,devpriv->iobase+APCI1500_Z8536_CONTROL_REGISTER); */

   /*****************************************************/
		/* Selects the command and status register of port A */
   /*****************************************************/
		outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS,
			devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		i_RegValue =
			inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		if ((i_RegValue & 0x60) == 0x60) {
	   /*****************************************************/
			/* Selects the command and status register of port A */
	   /*****************************************************/
			outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
	    /***********************************/
			/* Deletes the interrupt of port A */
	    /***********************************/
			i_RegValue = (i_RegValue & 0x0F) | 0x20;
			outb(i_RegValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_InterruptMask = i_InterruptMask | 1;
			if (i_Logic == APCI1500_OR_PRIORITY) {
				outb(APCI1500_RW_PORT_A_SPECIFICATION,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				i_RegValue =
					inb(devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

	      /***************************************************/
				/* Selects the interrupt vector register of port A */
	      /***************************************************/
				outb(APCI1500_RW_PORT_A_INTERRUPT_CONTROL,
					devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);
				i_RegValue =
					inb(devpriv->iobase +
					APCI1500_Z8536_CONTROL_REGISTER);

				i_InputChannel = 1 + (i_RegValue >> 1);

			}	/*  if(i_Logic==APCI1500_OR_PRIORITY) */
			else {
				i_InputChannel = 0;
			}	/* elseif(i_Logic==APCI1500_OR_PRIORITY) */
		}		/*  if ((i_RegValue & 0x60) == 0x60) */

	   /*****************************************************/
		/* Selects the command and status register of port B */
	   /*****************************************************/
		outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
			devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		i_RegValue =
			inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		if ((i_RegValue & 0x60) == 0x60) {
	     /*****************************************************/
			/* Selects the command and status register of port B */
	     /*****************************************************/
			outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
	     /***********************************/
			/* Deletes the interrupt of port B */
	     /***********************************/
			i_RegValue = (i_RegValue & 0x0F) | 0x20;
			outb(i_RegValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			printk("\n\n\n");
	     /****************/
			/* Reads port B */
	     /****************/
			i_RegValue =
				inb((unsigned int) devpriv->iobase +
				APCI1500_Z8536_PORT_B);

			i_RegValue = i_RegValue & 0xC0;
	      /**************************************/
			/* Tests if this is an external error */
	      /**************************************/

			if (i_RegValue) {
				/* Disable the interrupt */
		     /*****************************************************/
				/* Selects the command and status register of port B */
		     /*****************************************************/
				outl(0x0, devpriv->i_IobaseAmcc + 0x38);

				if (i_RegValue & 0x80) {
					i_InterruptMask =
						i_InterruptMask | 0x40;
				}	/* if (i_RegValue & 0x80) */

				if (i_RegValue & 0x40) {
					i_InterruptMask =
						i_InterruptMask | 0x80;
				}	/* if (i_RegValue & 0x40) */
			}	/*  if (i_RegValue) */
			else {
				i_InterruptMask = i_InterruptMask | 2;
			}	/*  if (i_RegValue) */
		}		/* if ((i_RegValue & 0x60) == 0x60) */

		/*****************************************************/
		/* Selects the command and status register of timer 1 */
		/*****************************************************/
		outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
			devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		i_RegValue =
			inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		if ((i_RegValue & 0x60) == 0x60) {
		   /*****************************************************/
			/* Selects the command and status register of timer 1 */
		   /*****************************************************/
			outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		   /***********************************/
			/* Deletes the interrupt of timer 1 */
		   /***********************************/
			i_RegValue = (i_RegValue & 0x0F) | 0x20;
			outb(i_RegValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_InterruptMask = i_InterruptMask | 4;
		}		/*  if ((i_RegValue & 0x60) == 0x60) */
		/*****************************************************/
		/* Selects the command and status register of timer 2 */
		/*****************************************************/
		outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
			devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		i_RegValue =
			inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		if ((i_RegValue & 0x60) == 0x60) {
		   /*****************************************************/
			/* Selects the command and status register of timer 2 */
		   /*****************************************************/
			outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		   /***********************************/
			/* Deletes the interrupt of timer 2 */
		   /***********************************/
			i_RegValue = (i_RegValue & 0x0F) | 0x20;
			outb(i_RegValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			i_InterruptMask = i_InterruptMask | 8;
		}		/*  if ((i_RegValue & 0x60) == 0x60) */

		/*****************************************************/
		/* Selects the command and status register of timer 3 */
		/*****************************************************/
		outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
			devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		i_RegValue =
			inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
		if ((i_RegValue & 0x60) == 0x60) {
		   /*****************************************************/
			/* Selects the command and status register of timer 3 */
		   /*****************************************************/
			outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
		   /***********************************/
			/* Deletes the interrupt of timer 3 */
		   /***********************************/
			i_RegValue = (i_RegValue & 0x0F) | 0x20;
			outb(i_RegValue,
				devpriv->iobase +
				APCI1500_Z8536_CONTROL_REGISTER);
			if (i_CounterLogic == APCI1500_COUNTER) {
				i_InterruptMask = i_InterruptMask | 0x10;
			}	/* if(i_CounterLogic==APCI1500_COUNTER) */
			else {
				i_InterruptMask = i_InterruptMask | 0x20;
			}
		}		/*  if ((i_RegValue & 0x60) == 0x60) */

		send_sig(SIGIO, devpriv->tsk_Current, 0);	/*  send signal to the sample */
	       /***********************/
		/* Enable all Interrupts */
	       /***********************/

	       /*************************************************/
		/* Selects the master interrupt control register */
	       /*************************************************/
		outb(APCI1500_RW_MASTER_INTERRUPT_CONTROL,
			devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	       /**********************************************/
		/* Authorizes the main interrupt on the board */
	       /**********************************************/
		outb(0xD0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	}			/*   if ((ui_InterruptStatus & 0x800000) == 0x800000) */
	else {
		printk("\nInterrupt from unknown source\n");

	}			/* else if ((ui_InterruptStatus & 0x800000) == 0x800000) */
	return;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI1500_Reset(struct comedi_device *dev)               |                                                       |
+----------------------------------------------------------------------------+
| Task              :resets all the registers                                |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      :                                                        |
|			                                                         |
+----------------------------------------------------------------------------+
*/

int i_APCI1500_Reset(struct comedi_device * dev)
{
	int i_DummyRead = 0;
	i_TimerCounter1Init = 0;
	i_TimerCounter2Init = 0;
	i_WatchdogCounter3Init = 0;
	i_Event1Status = 0;
	i_Event2Status = 0;
	i_TimerCounterWatchdogInterrupt = 0;
	i_Logic = 0;
	i_CounterLogic = 0;
	i_InterruptMask = 0;
	i_InputChannel = 0;;
	i_TimerCounter1Enabled = 0;
	i_TimerCounter2Enabled = 0;
	i_WatchdogCounter3Enabled = 0;

    /******************/
	/* Software reset */
    /******************/
	i_DummyRead = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	i_DummyRead = inb(devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(1, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

 /*****************************************************/
	/* Selects the master configuration control register */
 /*****************************************************/
	outb(APCI1500_RW_MASTER_CONFIGURATION_CONTROL,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0xF4, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	/*****************************************************/
	/* Selects the mode specification register of port A */
	/*****************************************************/
	outb(APCI1500_RW_PORT_A_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0x10, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	/* Selects the data path polarity register of port A */
	outb(APCI1500_RW_PORT_A_DATA_PCITCH_POLARITY,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* High level of port A means 1 */
	outb(0xFF, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	/* Selects the data direction register of port A */
	outb(APCI1500_RW_PORT_A_DATA_DIRECTION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* All bits used as inputs */
	outb(0xFF, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of port A */
	outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/*  Selects the command and status register of port A */
	outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates the interrupt management of port A:  */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the handshake specification register of port A */
	outb(APCI1500_RW_PORT_A_HANDSHAKE_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes the register */
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	 /*****************************************************/
	/* Selects the mode specification register of port B */
	 /*****************************************************/
	outb(APCI1500_RW_PORT_B_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	outb(0x10, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the data path polarity register of port B */
	outb(APCI1500_RW_PORT_B_DATA_PCITCH_POLARITY,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* A high level of port B means 1 */
	outb(0x7F, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the data direction register of port B */
	outb(APCI1500_RW_PORT_B_DATA_DIRECTION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* All bits used as inputs */
	outb(0xFF, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of port B */
	outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of port B */
	outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates the interrupt management of port B:         */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the handshake specification register of port B */
	outb(APCI1500_RW_PORT_B_HANDSHAKE_SPECIFICATION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes the register */
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);

	   /*****************************************************/
	/* Selects the data path polarity register of port C */
	   /*****************************************************/
	outb(APCI1500_RW_PORT_C_DATA_PCITCH_POLARITY,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* High level of port C means 1 */
	outb(0x9, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the data direction register of port C */
	outb(APCI1500_RW_PORT_C_DATA_DIRECTION,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* All bits used as inputs except channel 1 */
	outb(0x0E, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the special IO register of port C */
	outb(APCI1500_RW_PORT_C_SPECIAL_IO_CONTROL,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes it */
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	   /******************************************************/
	/* Selects the command and status register of timer 1 */
	   /******************************************************/
	outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of timer 1 */
	outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates the interrupt management of timer 1         */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	   /******************************************************/
	/* Selects the command and status register of timer 2 */
	   /******************************************************/
	outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of timer 2 */
	outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates Timer 2 interrupt management:               */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	  /******************************************************/
	/* Selects the command and status register of timer 3 */
	  /******************************************************/
	outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes IP and IUS */
	outb(0x20, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Selects the command and status register of Timer 3 */
	outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deactivates interrupt management of timer 3:            */
	outb(0xE0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	 /*************************************************/
	/* Selects the master interrupt control register */
	 /*************************************************/
	outb(APCI1500_RW_MASTER_INTERRUPT_CONTROL,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* Deletes all interrupts */
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	/* reset all the digital outputs */
	outw(0x0, devpriv->i_IobaseAddon + APCI1500_DIGITAL_OP);
/*******************************/
/* Disable the board interrupt */
/*******************************/
 /*************************************************/
	/* Selects the master interrupt control register */
 /*************************************************/
	outb(APCI1500_RW_MASTER_INTERRUPT_CONTROL,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/****************************/
/* Deactivates all interrupts */
/******************************/
	outb(0, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
 /*****************************************************/
	/* Selects the command and status register of port A */
 /*****************************************************/
	outb(APCI1500_RW_PORT_A_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/****************************/
/* Deactivates all interrupts */
/******************************/
	outb(0x00, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/*****************************************************/
	/* Selects the command and status register of port B */
 /*****************************************************/
	outb(APCI1500_RW_PORT_B_COMMAND_AND_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/****************************/
/* Deactivates all interrupts */
/******************************/
	outb(0x00, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/*****************************************************/
	/* Selects the command and status register of timer 1 */
 /*****************************************************/
	outb(APCI1500_RW_CPT_TMR1_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/****************************/
/* Deactivates all interrupts */
/******************************/
	outb(0x00, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/*****************************************************/
	/* Selects the command and status register of timer 2 */
 /*****************************************************/
	outb(APCI1500_RW_CPT_TMR2_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/****************************/
/* Deactivates all interrupts */
/******************************/
	outb(0x00, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/*****************************************************/
/* Selects the command and status register of timer 3*/
/*****************************************************/
	outb(APCI1500_RW_CPT_TMR3_CMD_STATUS,
		devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
/****************************/
/* Deactivates all interrupts */
/******************************/
	outb(0x00, devpriv->iobase + APCI1500_Z8536_CONTROL_REGISTER);
	return 0;
}
