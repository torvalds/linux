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
  | Module name : DIG_IO.C        | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date     :  02/12/2002                |
  +-----------------------------------------------------------------------+
  | Description :   APCI-1710 digital I/O module                          |
  |                                                                       |
  |                                                                       |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  | 16/06/98 | S. Weber  | Digital input / output implementation          |
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
#include "APCI1710_Dig_io.h"

/*
+----------------------------------------------------------------------------+
| Function Name     : int i_APCI1710_InsnConfigDigitalIO(struct comedi_device *dev, |
|						struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)|
+----------------------------------------------------------------------------+
| Task              : Configure the digital I/O operating mode from selected |
|                     module  (b_ModulNbr). You must calling this function be|
|                     for you call any other function witch access of digital|
|                     I/O.                                                   |
+----------------------------------------------------------------------------+
| Input Parameters  :													     |
|                  unsigned char_ b_ModulNbr      data[0]: Module number to               |
|                                             configure (0 to 3)             |
|                     unsigned char_ b_ChannelAMode data[1]  : Channel A mode selection       |
|                                             0 : Channel used for digital   |
|                                                 input                      |
|                                             1 : Channel used for digital   |
|                                                 output                     |
|                     unsigned char_ b_ChannelBMode data[2] : Channel B mode selection       |
|                                             0 : Channel used for digital   |
|                                                 input                      |
|                                             1 : Channel used for digital   |
|                                                 output					 |
						data[0]	  memory on/off
Activates and deactivates the digital output memory.
						After having      |
|                 called up this function with memory on,the output you have previously|
|                     activated with the function are not reset
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: The module parameter is wrong                       |
|                    -3: The module is not a digital I/O module              |
|                    -4: Bi-directional channel A configuration error        |
|                    -5: Bi-directional channel B configuration error        |
+----------------------------------------------------------------------------+
*/

int i_APCI1710_InsnConfigDigitalIO(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	unsigned char b_ModulNbr, b_ChannelAMode, b_ChannelBMode;
	unsigned char b_MemoryOnOff, b_ConfigType;
	int i_ReturnValue = 0;
	unsigned int dw_WriteConfig = 0;

	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_ConfigType = (unsigned char) data[0];	// Memory or  Init
	b_ChannelAMode = (unsigned char) data[1];
	b_ChannelBMode = (unsigned char) data[2];
	b_MemoryOnOff = (unsigned char) data[1];	// if memory operation
	i_ReturnValue = insn->n;

		/**************************/
	/* Test the module number */
		/**************************/

	if (b_ModulNbr >= 4) {
		DPRINTK("Module Number invalid\n");
		i_ReturnValue = -2;
		return i_ReturnValue;
	}
	switch (b_ConfigType) {
	case APCI1710_DIGIO_MEMORYONOFF:

		if (b_MemoryOnOff)	// If Memory ON
		{
		 /****************************/
			/* Set the output memory on */
		 /****************************/

			devpriv->s_ModuleInfo[b_ModulNbr].
				s_DigitalIOInfo.b_OutputMemoryEnabled = 1;

		 /***************************/
			/* Clear the output memory */
		 /***************************/
			devpriv->s_ModuleInfo[b_ModulNbr].
				s_DigitalIOInfo.dw_OutputMemory = 0;
		} else		// If memory off
		{
		 /*****************************/
			/* Set the output memory off */
		 /*****************************/

			devpriv->s_ModuleInfo[b_ModulNbr].
				s_DigitalIOInfo.b_OutputMemoryEnabled = 0;
		}
		break;

	case APCI1710_DIGIO_INIT:

	/*******************************/
		/* Test if digital I/O counter */
	/*******************************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_DIGITAL_IO) {

	/***************************************************/
			/* Test the bi-directional channel A configuration */
	/***************************************************/

			if ((b_ChannelAMode == 0) || (b_ChannelAMode == 1)) {
	/***************************************************/
				/* Test the bi-directional channel B configuration */
	/***************************************************/

				if ((b_ChannelBMode == 0)
					|| (b_ChannelBMode == 1)) {
					devpriv->s_ModuleInfo[b_ModulNbr].
						s_DigitalIOInfo.b_DigitalInit =
						1;

	/********************************/
					/* Save channel A configuration */
	/********************************/

					devpriv->s_ModuleInfo[b_ModulNbr].
						s_DigitalIOInfo.
						b_ChannelAMode = b_ChannelAMode;

	/********************************/
					/* Save channel B configuration */
	/********************************/

					devpriv->s_ModuleInfo[b_ModulNbr].
						s_DigitalIOInfo.
						b_ChannelBMode = b_ChannelBMode;

	/*****************************************/
					/* Set the channel A and B configuration */
	/*****************************************/

					dw_WriteConfig =
						(unsigned int) (b_ChannelAMode |
						(b_ChannelBMode * 2));

	/***************************/
					/* Write the configuration */
	/***************************/

					outl(dw_WriteConfig,
						devpriv->s_BoardInfos.
						ui_Address + 4 +
						(64 * b_ModulNbr));

				} else {
	/************************************************/
					/* Bi-directional channel B configuration error */
	/************************************************/
					DPRINTK("Bi-directional channel B configuration error\n");
					i_ReturnValue = -5;
				}

			} else {
	/************************************************/
				/* Bi-directional channel A configuration error */
	/************************************************/
				DPRINTK("Bi-directional channel A configuration error\n");
				i_ReturnValue = -4;

			}

		} else {
	/******************************************/
			/* The module is not a digital I/O module */
	/******************************************/
			DPRINTK("The module is not a digital I/O module\n");
			i_ReturnValue = -3;
		}
	}			// end of Switch
	printk("Return Value %d\n", i_ReturnValue);
	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
|                            INPUT FUNCTIONS                                 |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+

|INT i_APCI1710_InsnReadDigitalIOChlValue(struct comedi_device *dev,comedi_subdevice
*s,	struct comedi_insn *insn,unsigned int *data)

+----------------------------------------------------------------------------+
| Task              : Read the status from selected digital I/O digital input|
|                     (b_InputChannel)                                       |
+----------------------------------------------------------------------------|


|
|  unsigned char_ b_ModulNbr  CR_AREF(chanspec)          : Selected module number   |
|                                                   (0 to 3)                 |
|  unsigned char_ b_InputChannel CR_CHAN(chanspec)        : Selection from digital   |
|                                                   input ( 0 to 6)          |
|                                                      0 : Channel C         |
|                                                      1 : Channel D         |
|                                                      2 : Channel E         |
|                                                      3 : Channel F         |
|                                                      4 : Channel G         |
|                                                      5 : Channel A         |
|                                                      6 : Channel B


	|
+----------------------------------------------------------------------------+
| Output Parameters :					 data[0]   : Digital input channel    |
|                                                   status                   |
|                                                   0 : Channle is not active|
|                                                   1 : Channle is active    |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: The module parameter is wrong                       |
|                    -3: The module is not a digital I/O module              |
|                    -4: The selected digital I/O digital input is wrong     |
|                    -5: Digital I/O not initialised                         |
|                    -6: The digital channel A is used for output            |
|                    -7: The digital channel B is used for output            |
+----------------------------------------------------------------------------+
*/

//_INT_   i_APCI1710_ReadDigitalIOChlValue      (unsigned char_    b_BoardHandle,
//                                             unsigned char_    b_ModulNbr,
//                                             unsigned char_    b_InputChannel,
//
//                                             unsigned char *_  pb_ChannelStatus)
int i_APCI1710_InsnReadDigitalIOChlValue(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_ReturnValue = 0;
	unsigned int dw_StatusReg;
	unsigned char b_ModulNbr, b_InputChannel;
	unsigned char * pb_ChannelStatus;
	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_InputChannel = (unsigned char) CR_CHAN(insn->chanspec);
	data[0] = 0;
	pb_ChannelStatus = (unsigned char *) & data[0];
	i_ReturnValue = insn->n;

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /*******************************/
		/* Test if digital I/O counter */
	   /*******************************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_DIGITAL_IO) {
	      /******************************************/
			/* Test the digital imnput channel number */
	      /******************************************/

			if (b_InputChannel <= 6) {
		 /**********************************************/
				/* Test if the digital I/O module initialised */
		 /**********************************************/

				if (devpriv->s_ModuleInfo[b_ModulNbr].
					s_DigitalIOInfo.b_DigitalInit == 1) {
		    /**********************************/
					/* Test if channel A or channel B */
		    /**********************************/

					if (b_InputChannel > 4) {
		       /*********************/
						/* Test if channel A */
		       /*********************/

						if (b_InputChannel == 5) {
			  /***************************/
							/* Test the channel A mode */
			  /***************************/

							if (devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_DigitalIOInfo.
								b_ChannelAMode
								!= 0) {
			     /********************************************/
								/* The digital channel A is used for output */
			     /********************************************/

								i_ReturnValue =
									-6;
							}
						}	// if (b_InputChannel == 5)
						else {
			  /***************************/
							/* Test the channel B mode */
			  /***************************/

							if (devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_DigitalIOInfo.
								b_ChannelBMode
								!= 0) {
			     /********************************************/
								/* The digital channel B is used for output */
			     /********************************************/

								i_ReturnValue =
									-7;
							}
						}	// if (b_InputChannel == 5)
					}	// if (b_InputChannel > 4)

		    /***********************/
					/* Test if error occur */
		    /***********************/

					if (i_ReturnValue >= 0) {
		       /**************************/
						/* Read all digital input */
		       /**************************/

						//INPDW (ps_APCI1710Variable->
						//   s_Board [b_BoardHandle].
						//   s_BoardInfos.
						//  ui_Address + (64 * b_ModulNbr),
						// &dw_StatusReg);

						dw_StatusReg =
							inl(devpriv->
							s_BoardInfos.
							ui_Address +
							(64 * b_ModulNbr));

						*pb_ChannelStatus =
							(unsigned char) ((dw_StatusReg ^
								0x1C) >>
							b_InputChannel) & 1;

					}	// if (i_ReturnValue == 0)
				} else {
		    /*******************************/
					/* Digital I/O not initialised */
		    /*******************************/
					DPRINTK("Digital I/O not initialised\n");
					i_ReturnValue = -5;
				}
			} else {
		 /********************************/
				/* Selected digital input error */
		 /********************************/
				DPRINTK("Selected digital input error\n");
				i_ReturnValue = -4;
			}
		} else {
	      /******************************************/
			/* The module is not a digital I/O module */
	      /******************************************/
			DPRINTK("The module is not a digital I/O module\n");
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
|                            OUTPUT FUNCTIONS                                |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : int i_APCI1710_InsnWriteDigitalIOChlOnOff(comedi_device
|*dev,struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data)

+----------------------------------------------------------------------------+
| Task              : Sets or resets the output witch has been passed with the         |
|                     parameter b_Channel. Setting an output means setting   |
|                     an ouput high.                                         |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle   : Handle of board APCI-1710      |
|                     unsigned char_ b_ModulNbr (aref )    : Selected module number (0 to 3)|
|                     unsigned char_ b_OutputChannel (CR_CHAN) : Selection from digital output  |
|                                             channel (0 to 2)               |
|                                                0 : Channel H               |
|                                                1 : Channel A               |
|                                                2 : Channel B               |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: The module parameter is wrong                       |
|                    -3: The module is not a digital I/O module              |
|                    -4: The selected digital output is wrong                |
|                    -5: digital I/O not initialised see function            |
|                        " i_APCI1710_InitDigitalIO"                         |
|                    -6: The digital channel A is used for input             |
|                    -7: The digital channel B is used for input
					 -8: Digital Output Memory OFF.                          |
|                        Use previously the function                         |
|                        "i_APCI1710_SetDigitalIOMemoryOn".            |
+----------------------------------------------------------------------------+
*/

//_INT_   i_APCI1710_SetDigitalIOChlOn    (unsigned char_ b_BoardHandle,
//                                       unsigned char_ b_ModulNbr,
//                                       unsigned char_ b_OutputChannel)
int i_APCI1710_InsnWriteDigitalIOChlOnOff(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_ReturnValue = 0;
	unsigned int dw_WriteValue = 0;
	unsigned char b_ModulNbr, b_OutputChannel;
	i_ReturnValue = insn->n;
	b_ModulNbr = CR_AREF(insn->chanspec);
	b_OutputChannel = CR_CHAN(insn->chanspec);

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /*******************************/
		/* Test if digital I/O counter */
	   /*******************************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_DIGITAL_IO) {
	      /**********************************************/
			/* Test if the digital I/O module initialised */
	      /**********************************************/

			if (devpriv->s_ModuleInfo[b_ModulNbr].
				s_DigitalIOInfo.b_DigitalInit == 1) {
		 /******************************************/
				/* Test the digital output channel number */
		 /******************************************/

				switch (b_OutputChannel) {
		    /*************/
					/* Channel H */
		    /*************/

				case 0:
					break;

		    /*************/
					/* Channel A */
		    /*************/

				case 1:
					if (devpriv->s_ModuleInfo[b_ModulNbr].
						s_DigitalIOInfo.
						b_ChannelAMode != 1) {
			    /*******************************************/
						/* The digital channel A is used for input */
			    /*******************************************/

						i_ReturnValue = -6;
					}
					break;

		    /*************/
					/* Channel B */
		    /*************/

				case 2:
					if (devpriv->s_ModuleInfo[b_ModulNbr].
						s_DigitalIOInfo.
						b_ChannelBMode != 1) {
			    /*******************************************/
						/* The digital channel B is used for input */
			    /*******************************************/

						i_ReturnValue = -7;
					}
					break;

				default:
			 /****************************************/
					/* The selected digital output is wrong */
			 /****************************************/

					i_ReturnValue = -4;
					break;
				}

		 /***********************/
				/* Test if error occur */
		 /***********************/

				if (i_ReturnValue >= 0) {

			/*********************************/
					/* Test if set channel ON        */
		    /*********************************/
					if (data[0]) {
		    /*********************************/
						/* Test if output memory enabled */
		    /*********************************/

						if (devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_DigitalIOInfo.
							b_OutputMemoryEnabled ==
							1) {
							dw_WriteValue =
								devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_DigitalIOInfo.
								dw_OutputMemory
								| (1 <<
								b_OutputChannel);

							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_DigitalIOInfo.
								dw_OutputMemory
								= dw_WriteValue;
						} else {
							dw_WriteValue =
								1 <<
								b_OutputChannel;
						}
					}	// set channel off
					else {
						if (devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_DigitalIOInfo.
							b_OutputMemoryEnabled ==
							1) {
							dw_WriteValue =
								devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_DigitalIOInfo.
								dw_OutputMemory
								& (0xFFFFFFFFUL
								-
								(1 << b_OutputChannel));

							devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_DigitalIOInfo.
								dw_OutputMemory
								= dw_WriteValue;
						} else {
		       /*****************************/
							/* Digital Output Memory OFF */
		       /*****************************/
							// +Use previously the function "i_APCI1710_SetDigitalIOMemoryOn"
							i_ReturnValue = -8;
						}

					}
		    /*******************/
					/* Write the value */
		    /*******************/

					//OUTPDW (ps_APCI1710Variable->
					//    s_Board [b_BoardHandle].
					//   s_BoardInfos.
					//   ui_Address + (64 * b_ModulNbr),
					//   dw_WriteValue);
					outl(dw_WriteValue,
						devpriv->s_BoardInfos.
						ui_Address + (64 * b_ModulNbr));
				}
			} else {
		 /*******************************/
				/* Digital I/O not initialised */
		 /*******************************/

				i_ReturnValue = -5;
			}
		} else {
	      /******************************************/
			/* The module is not a digital I/O module */
	      /******************************************/

			i_ReturnValue = -3;
		}
	} else {
	   /***********************/
		/* Module number error */
	   /***********************/

		i_ReturnValue = -2;
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+

|INT i_APCI1710_InsnBitsDigitalIOPortOnOff(struct comedi_device *dev,comedi_subdevice
	*s,	struct comedi_insn *insn,unsigned int *data)
+----------------------------------------------------------------------------+
| Task              : write:
					  Sets or resets one or several outputs from port.                 |
|                     Setting an output means setting an output high.        |
|                     If you have switched OFF the digital output memory     |
|                     (OFF), all the other output are set to "0".

|                      read:
					  Read the status from digital input port                |
|                     from selected digital I/O module (b_ModulNbr)
+----------------------------------------------------------------------------+
| Input Parameters  :
	unsigned char_ b_BoardHandle   : Handle of board APCI-1710      |
|   unsigned char_ b_ModulNbr  CR_AREF(aref)    : Selected module number (0 to 3)|
|   unsigned char_ b_PortValue CR_CHAN(chanspec) : Output Value ( 0 To 7 )
|                       data[0]           read or write port
                        data[1]            if write then indicate ON or OFF

                        if read : data[1] will return port status.
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :

                INPUT :

					  0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: The module parameter is wrong                       |
|                    -3: The module is not a digital I/O module              |
|                    -4: Digital I/O not initialised

				OUTPUT:	  0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: The module parameter is wrong                       |
|                    -3: The module is not a digital I/O module              |
|                    -4: Output value wrong                                  |
|                    -5: digital I/O not initialised see function            |
|                        " i_APCI1710_InitDigitalIO"                         |
|                    -6: The digital channel A is used for input             |
|                    -7: The digital channel B is used for input
					-8: Digital Output Memory OFF.                          |
|                        Use previously the function                         |
|                        "i_APCI1710_SetDigitalIOMemoryOn".               |
+----------------------------------------------------------------------------+
*/

//_INT_   i_APCI1710_SetDigitalIOPortOn   (unsigned char_ b_BoardHandle,
//                                       unsigned char_ b_ModulNbr,
//                                       unsigned char_ b_PortValue)
int i_APCI1710_InsnBitsDigitalIOPortOnOff(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	int i_ReturnValue = 0;
	unsigned int dw_WriteValue = 0;
	unsigned int dw_StatusReg;
	unsigned char b_ModulNbr, b_PortValue;
	unsigned char b_PortOperation, b_PortOnOFF;

	unsigned char * pb_PortValue;

	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_PortOperation = (unsigned char) data[0];	// Input or output
	b_PortOnOFF = (unsigned char) data[1];	// if output then On or Off
	b_PortValue = (unsigned char) data[2];	// if out put then Value
	i_ReturnValue = insn->n;
	pb_PortValue = (unsigned char *) & data[0];
// if input then read value

	switch (b_PortOperation) {
	case APCI1710_INPUT:
	/**************************/
		/* Test the module number */
	/**************************/

		if (b_ModulNbr < 4) {
	   /*******************************/
			/* Test if digital I/O counter */
	   /*******************************/

			if ((devpriv->s_BoardInfos.
					dw_MolduleConfiguration[b_ModulNbr] &
					0xFFFF0000UL) == APCI1710_DIGITAL_IO) {
	      /**********************************************/
				/* Test if the digital I/O module initialised */
	      /**********************************************/

				if (devpriv->s_ModuleInfo[b_ModulNbr].
					s_DigitalIOInfo.b_DigitalInit == 1) {
		 /**************************/
					/* Read all digital input */
		 /**************************/

					//INPDW (ps_APCI1710Variable->
					//      s_Board [b_BoardHandle].
					//      s_BoardInfos.
					//      ui_Address + (64 * b_ModulNbr),
					//      &dw_StatusReg);

					dw_StatusReg =
						inl(devpriv->s_BoardInfos.
						ui_Address + (64 * b_ModulNbr));
					*pb_PortValue =
						(unsigned char) (dw_StatusReg ^ 0x1C);

				} else {
		 /*******************************/
					/* Digital I/O not initialised */
		 /*******************************/

					i_ReturnValue = -4;
				}
			} else {
	      /******************************************/
				/* The module is not a digital I/O module */
	      /******************************************/

				i_ReturnValue = -3;
			}
		} else {
	   /***********************/
			/* Module number error */
	   /***********************/

			i_ReturnValue = -2;
		}

		break;

	case APCI1710_OUTPUT:
	/**************************/
		/* Test the module number */
	/**************************/

		if (b_ModulNbr < 4) {
	   /*******************************/
			/* Test if digital I/O counter */
	   /*******************************/

			if ((devpriv->s_BoardInfos.
					dw_MolduleConfiguration[b_ModulNbr] &
					0xFFFF0000UL) == APCI1710_DIGITAL_IO) {
	      /**********************************************/
				/* Test if the digital I/O module initialised */
	      /**********************************************/

				if (devpriv->s_ModuleInfo[b_ModulNbr].
					s_DigitalIOInfo.b_DigitalInit == 1) {
		 /***********************/
					/* Test the port value */
		 /***********************/

					if (b_PortValue <= 7) {
		    /***********************************/
						/* Test the digital output channel */
		    /***********************************/

		    /**************************/
						/* Test if channel A used */
		    /**************************/

						if ((b_PortValue & 2) == 2) {
							if (devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_DigitalIOInfo.
								b_ChannelAMode
								!= 1) {
			  /*******************************************/
								/* The digital channel A is used for input */
			  /*******************************************/

								i_ReturnValue =
									-6;
							}
						}	// if ((b_PortValue & 2) == 2)

		    /**************************/
						/* Test if channel B used */
		    /**************************/

						if ((b_PortValue & 4) == 4) {
							if (devpriv->
								s_ModuleInfo
								[b_ModulNbr].
								s_DigitalIOInfo.
								b_ChannelBMode
								!= 1) {
			  /*******************************************/
								/* The digital channel B is used for input */
			  /*******************************************/

								i_ReturnValue =
									-7;
							}
						}	// if ((b_PortValue & 4) == 4)

		    /***********************/
						/* Test if error occur */
		    /***********************/

						if (i_ReturnValue >= 0) {

							//if(data[1])
							//{
							switch (b_PortOnOFF) {
			   /*********************************/
								/* Test if set Port ON                   */
		       /*********************************/

							case APCI1710_ON:

		       /*********************************/
								/* Test if output memory enabled */
		       /*********************************/

								if (devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_DigitalIOInfo.
									b_OutputMemoryEnabled
									== 1) {
									dw_WriteValue
										=
										devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_DigitalIOInfo.
										dw_OutputMemory
										|
										b_PortValue;

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_DigitalIOInfo.
										dw_OutputMemory
										=
										dw_WriteValue;
								} else {
									dw_WriteValue
										=
										b_PortValue;
								}
								break;

								// If Set PORT  OFF
							case APCI1710_OFF:

			   /*********************************/
								/* Test if output memory enabled */
		       /*********************************/

								if (devpriv->
									s_ModuleInfo
									[b_ModulNbr].
									s_DigitalIOInfo.
									b_OutputMemoryEnabled
									== 1) {
									dw_WriteValue
										=
										devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_DigitalIOInfo.
										dw_OutputMemory
										&
										(0xFFFFFFFFUL
										-
										b_PortValue);

									devpriv->
										s_ModuleInfo
										[b_ModulNbr].
										s_DigitalIOInfo.
										dw_OutputMemory
										=
										dw_WriteValue;
								} else {
			  /*****************************/
									/* Digital Output Memory OFF */
			  /*****************************/

									i_ReturnValue
										=
										-8;
								}
							}	// switch

		       /*******************/
							/* Write the value */
		       /*******************/

							//  OUTPDW (ps_APCI1710Variable->
							//      s_Board [b_BoardHandle].
							//      s_BoardInfos.
							//      ui_Address + (64 * b_ModulNbr),
							//      dw_WriteValue);
							outl(dw_WriteValue,
								devpriv->
								s_BoardInfos.
								ui_Address +
								(64 * b_ModulNbr));
						}
					} else {
		    /**********************/
						/* Output value wrong */
		    /**********************/

						i_ReturnValue = -4;
					}
				} else {
		 /*******************************/
					/* Digital I/O not initialised */
		 /*******************************/

					i_ReturnValue = -5;
				}
			} else {
	      /******************************************/
				/* The module is not a digital I/O module */
	      /******************************************/

				i_ReturnValue = -3;
			}
		} else {
	   /***********************/
			/* Module number error */
	   /***********************/

			i_ReturnValue = -2;
		}
		break;

	default:
		i_ReturnValue = -9;
		DPRINTK("NO INPUT/OUTPUT specified\n");
	}			//switch INPUT / OUTPUT
	return (i_ReturnValue);
}
