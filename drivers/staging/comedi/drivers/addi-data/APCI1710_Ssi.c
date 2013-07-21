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
  | Module name : SSI.C           | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date     :  02/12/2002                |
  +-----------------------------------------------------------------------+
  | Description :   APCI-1710 SSI counter module                          |
  +-----------------------------------------------------------------------+
  | several changes done by S. Weber in 1998 and C. Guinot in 2000        |
  +-----------------------------------------------------------------------+
*/

#define APCI1710_30MHZ			30
#define APCI1710_33MHZ			33
#define APCI1710_40MHZ			40

#define APCI1710_BINARY_MODE		0x1
#define APCI1710_GRAY_MODE		0x0

#define APCI1710_SSI_READ1VALUE		1
#define APCI1710_SSI_READALLVALUE	2

#define APCI1710_SSI_SET_CHANNELON	0
#define APCI1710_SSI_SET_CHANNELOFF	1
#define APCI1710_SSI_READ_1CHANNEL	2
#define APCI1710_SSI_READ_ALLCHANNEL	3

/*
+----------------------------------------------------------------------------+
| Function Name     : _INT_ i_APCI1710_InitSSI                               |
|                               (unsigned char_    b_BoardHandle,                     |
|                                unsigned char_    b_ModulNbr,                        |
|                                unsigned char_    b_SSIProfile,                      |
|                                unsigned char_    b_PositionTurnLength,              |
|                                unsigned char_    b_TurnCptLength,                   |
|                                unsigned char_    b_PCIInputClock,                   |
|                                ULONG_  ul_SSIOutputClock,                  |
|                                unsigned char_    b_SSICountingMode)                 |
+----------------------------------------------------------------------------+
| Task              : Configure the SSI operating mode from selected module  |
|                     (b_ModulNbr). You must calling this function be for you|
|                     call any other function witch access of SSI.           |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle         : Handle of board APCI-1710|
|                     unsigned char_ b_ModulNbr            : Module number to         |
|                                                   configure (0 to 3)       |
|                     unsigned char_  b_SSIProfile         : Selection from SSI       |
|                                                   profile length (2 to 32).|
|                     unsigned char_  b_PositionTurnLength : Selection from SSI       |
|                                                   position data length     |
|                                                   (1 to 31).               |
|                     unsigned char_  b_TurnCptLength      : Selection from SSI turn  |
|                                                   counter data length      |
|                                                   (1 to 31).               |
|                     unsigned char   b_PCIInputClock      : Selection from PCI bus   |
|                                                   clock                    |
|                                                 - APCI1710_30MHZ :         |
|                                                   The PC have a PCI bus    |
|                                                   clock from 30 MHz        |
|                                                 - APCI1710_33MHZ :         |
|                                                   The PC have a PCI bus    |
|                                                   clock from 33 MHz        |
|                     ULONG_ ul_SSIOutputClock    : Selection from SSI output|
|                                                   clock.                   |
|                                                   From  229 to 5 000 000 Hz|
|                                                   for 30 MHz selection.    |
|                                                   From  252 to 5 000 000 Hz|
|                                                   for 33 MHz selection.    |
|                     unsigned char   b_SSICountingMode    : SSI counting mode        |
|                                                   selection                |
|                                                 - APCI1710_BINARY_MODE :   |
|                                                    Binary counting mode.   |
|                                                 - APCI1710_GRAY_MODE :     |
|                                                    Gray counting mode.

	b_ModulNbr			= CR_AREF(insn->chanspec);
	b_SSIProfile		= (unsigned char) data[0];
	b_PositionTurnLength= (unsigned char) data[1];
	b_TurnCptLength		= (unsigned char) data[2];
	b_PCIInputClock		= (unsigned char) data[3];
	ul_SSIOutputClock	= (unsigned int) data[4];
	b_SSICountingMode	= (unsigned char)  data[5];     |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: The module parameter is wrong                       |
|                    -3: The module is not a SSI module                      |
|                    -4: The selected SSI profile length is wrong            |
|                    -5: The selected SSI position data length is wrong      |
|                    -6: The selected SSI turn counter data length is wrong  |
|                    -7: The selected PCI input clock is wrong               |
|                    -8: The selected SSI output clock is wrong              |
|                    -9: The selected SSI counting mode parameter is wrong   |
+----------------------------------------------------------------------------+
*/

static int i_APCI1710_InsnConfigInitSSI(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn,
					unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	int i_ReturnValue = 0;
	unsigned int ui_TimerValue;
	unsigned char b_ModulNbr, b_SSIProfile, b_PositionTurnLength, b_TurnCptLength,
		b_PCIInputClock, b_SSICountingMode;
	unsigned int ul_SSIOutputClock;

	b_ModulNbr = CR_AREF(insn->chanspec);
	b_SSIProfile = (unsigned char) data[0];
	b_PositionTurnLength = (unsigned char) data[1];
	b_TurnCptLength = (unsigned char) data[2];
	b_PCIInputClock = (unsigned char) data[3];
	ul_SSIOutputClock = (unsigned int) data[4];
	b_SSICountingMode = (unsigned char) data[5];

	i_ReturnValue = insn->n;
	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if SSI counter */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_SSI_COUNTER) {
	      /*******************************/
			/* Test the SSI profile length */
	      /*******************************/

			/*  CG 22/03/00 b_SSIProfile >= 2 anstatt b_SSIProfile > 2 */
			if (b_SSIProfile >= 2 && b_SSIProfile < 33) {
		 /*************************************/
				/* Test the SSI position data length */
		 /*************************************/

				if (b_PositionTurnLength > 0
					&& b_PositionTurnLength < 32) {
		    /*****************************************/
					/* Test the SSI turn counter data length */
		    /*****************************************/

					if (b_TurnCptLength > 0
						&& b_TurnCptLength < 32) {
		       /***************************/
						/* Test the profile length */
		       /***************************/

						if ((b_TurnCptLength +
								b_PositionTurnLength)
							<= b_SSIProfile) {
			  /****************************/
							/* Test the PCI input clock */
			  /****************************/

							if (b_PCIInputClock ==
								APCI1710_30MHZ
								||
								b_PCIInputClock
								==
								APCI1710_33MHZ)
							{
			     /*************************/
								/* Test the output clock */
			     /*************************/

								if ((b_PCIInputClock == APCI1710_30MHZ && (ul_SSIOutputClock > 228 && ul_SSIOutputClock <= 5000000UL)) || (b_PCIInputClock == APCI1710_33MHZ && (ul_SSIOutputClock > 251 && ul_SSIOutputClock <= 5000000UL))) {
									if (b_SSICountingMode == APCI1710_BINARY_MODE || b_SSICountingMode == APCI1710_GRAY_MODE) {
				   /**********************/
										/* Save configuration */
				   /**********************/
										devpriv->
											s_ModuleInfo
											[b_ModulNbr].
											s_SSICounterInfo.
											b_SSIProfile
											=
											b_SSIProfile;

										devpriv->
											s_ModuleInfo
											[b_ModulNbr].
											s_SSICounterInfo.
											b_PositionTurnLength
											=
											b_PositionTurnLength;

										devpriv->
											s_ModuleInfo
											[b_ModulNbr].
											s_SSICounterInfo.
											b_TurnCptLength
											=
											b_TurnCptLength;

				   /*********************************/
										/* Initialise the profile length */
				   /*********************************/

										if (b_SSICountingMode == APCI1710_BINARY_MODE) {

											outl(b_SSIProfile + 1, devpriv->s_BoardInfos.ui_Address + 4 + (64 * b_ModulNbr));
										} else {

											outl(b_SSIProfile, devpriv->s_BoardInfos.ui_Address + 4 + (64 * b_ModulNbr));
										}

				   /******************************/
										/* Calculate the output clock */
				   /******************************/

										ui_TimerValue
											=
											(unsigned int)
											(
											((unsigned int) (b_PCIInputClock) * 500000UL) / ul_SSIOutputClock);

				   /************************/
										/* Initialise the timer */
				   /************************/

										outl(ui_TimerValue, devpriv->s_BoardInfos.ui_Address + (64 * b_ModulNbr));

				   /********************************/
										/* Initialise the counting mode */
				   /********************************/

										outl(7 * b_SSICountingMode, devpriv->s_BoardInfos.ui_Address + 12 + (64 * b_ModulNbr));

										devpriv->
											s_ModuleInfo
											[b_ModulNbr].
											s_SSICounterInfo.
											b_SSIInit
											=
											1;
									} else {
				   /*****************************************************/
										/* The selected SSI counting mode parameter is wrong */
				   /*****************************************************/

										DPRINTK("The selected SSI counting mode parameter is wrong\n");
										i_ReturnValue
											=
											-9;
									}
								} else {
				/******************************************/
									/* The selected SSI output clock is wrong */
				/******************************************/

									DPRINTK("The selected SSI output clock is wrong\n");
									i_ReturnValue
										=
										-8;
								}
							} else {
			     /*****************************************/
								/* The selected PCI input clock is wrong */
			     /*****************************************/

								DPRINTK("The selected PCI input clock is wrong\n");
								i_ReturnValue =
									-7;
							}
						} else {
			  /********************************************/
							/* The selected SSI profile length is wrong */
			  /********************************************/

							DPRINTK("The selected SSI profile length is wrong\n");
							i_ReturnValue = -4;
						}
					} else {
		       /******************************************************/
						/* The selected SSI turn counter data length is wrong */
		       /******************************************************/

						DPRINTK("The selected SSI turn counter data length is wrong\n");
						i_ReturnValue = -6;
					}
				} else {
		    /**************************************************/
					/* The selected SSI position data length is wrong */
		    /**************************************************/

					DPRINTK("The selected SSI position data length is wrong\n");
					i_ReturnValue = -5;
				}
			} else {
		 /********************************************/
				/* The selected SSI profile length is wrong */
		 /********************************************/

				DPRINTK("The selected SSI profile length is wrong\n");
				i_ReturnValue = -4;
			}
		} else {
	      /**********************************/
			/* The module is not a SSI module */
	      /**********************************/

			DPRINTK("The module is not a SSI module\n");
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
| Function Name     : _INT_  i_APCI1710_Read1SSIValue                        |
|                               (unsigned char_     b_BoardHandle,                    |
|                                unsigned char_     b_ModulNbr,                       |
|                                unsigned char_     b_SelectedSSI,                    |
|                                PULONG_ pul_Position,                       |
|                                PULONG_ pul_TurnCpt)
 int i_APCI1710_ReadSSIValue(struct comedi_device *dev,struct comedi_subdevice *s,
	struct comedi_insn *insn,unsigned int *data)                       |
+----------------------------------------------------------------------------+
| Task              :


						Read the selected SSI counter (b_SelectedSSI) from     |
|                     selected module (b_ModulNbr).
						or Read all SSI counter (b_SelectedSSI) from              |
|                     selected module (b_ModulNbr).                            |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle         : Handle of board APCI-1710|
|                     unsigned char_ b_ModulNbr            : Module number to         |
|                                                   configure (0 to 3)       |
|                     unsigned char_ b_SelectedSSI         : Selection from SSI       |
|                                                   counter (0 to 2)

    b_ModulNbr		=   (unsigned char) CR_AREF(insn->chanspec);
	b_SelectedSSI	=	(unsigned char) CR_CHAN(insn->chanspec); (in case of single ssi)
	b_ReadType		=	(unsigned char) CR_RANGE(insn->chanspec);
|
+----------------------------------------------------------------------------+
| Output Parameters : PULONG_  pul_Position       : SSI position in the turn |
|                     PULONG_  pul_TurnCpt        : Number of turns

pul_Position	=	(unsigned int *) &data[0];
	pul_TurnCpt		=	(unsigned int *) &data[1];         |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: The module parameter is wrong                       |
|                    -3: The module is not a SSI module                      |
|                    -4: SSI not initialised see function                    |
|                        "i_APCI1710_InitSSI"                                |
|                    -5: The selected SSI is wrong                           |
+----------------------------------------------------------------------------+
*/

static int i_APCI1710_InsnReadSSIValue(struct comedi_device *dev,
				       struct comedi_subdevice *s,
				       struct comedi_insn *insn,
				       unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	int i_ReturnValue = 0;
	unsigned char b_Cpt;
	unsigned char b_Length;
	unsigned char b_Schift;
	unsigned char b_SSICpt;
	unsigned int dw_And;
	unsigned int dw_And1;
	unsigned int dw_And2;
	unsigned int dw_StatusReg;
	unsigned int dw_CounterValue;
	unsigned char b_ModulNbr;
	unsigned char b_SelectedSSI;
	unsigned char b_ReadType;
	unsigned int *pul_Position;
	unsigned int *pul_TurnCpt;
	unsigned int *pul_Position1;
	unsigned int *pul_TurnCpt1;

	i_ReturnValue = insn->n;
	pul_Position1 = (unsigned int *) &data[0];
/* For Read1 */
	pul_TurnCpt1 = (unsigned int *) &data[1];
/* For Read all */
	pul_Position = (unsigned int *) &data[0];	/* 0-2 */
	pul_TurnCpt = (unsigned int *) &data[3];	/* 3-5 */
	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_SelectedSSI = (unsigned char) CR_CHAN(insn->chanspec);
	b_ReadType = (unsigned char) CR_RANGE(insn->chanspec);

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if SSI counter */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_SSI_COUNTER) {
	      /***************************/
			/* Test if SSI initialised */
	      /***************************/

			if (devpriv->s_ModuleInfo[b_ModulNbr].
				s_SSICounterInfo.b_SSIInit == 1) {

				switch (b_ReadType) {

				case APCI1710_SSI_READ1VALUE:
		 /****************************************/
					/* Test the selected SSI counter number */
		 /****************************************/

					if (b_SelectedSSI < 3) {
		    /************************/
						/* Start the conversion */
		    /************************/

						outl(0, devpriv->s_BoardInfos.
							ui_Address + 8 +
							(64 * b_ModulNbr));

						do {
		       /*******************/
							/* Read the status */
		       /*******************/

							dw_StatusReg =
								inl(devpriv->
								s_BoardInfos.
								ui_Address +
								(64 * b_ModulNbr));
						} while ((dw_StatusReg & 0x1)
							 != 0);

		    /******************************/
						/* Read the SSI counter value */
		    /******************************/

						dw_CounterValue =
							inl(devpriv->
							s_BoardInfos.
							ui_Address + 4 +
							(b_SelectedSSI * 4) +
							(64 * b_ModulNbr));

						b_Length =
							devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_SSICounterInfo.
							b_SSIProfile / 2;

						if ((b_Length * 2) !=
							devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_SSICounterInfo.
							b_SSIProfile) {
							b_Length++;
						}

						b_Schift =
							b_Length -
							devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_SSICounterInfo.
							b_PositionTurnLength;

						*pul_Position1 =
							dw_CounterValue >>
							b_Schift;

						dw_And = 1;

						for (b_Cpt = 0;
							b_Cpt <
							devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_SSICounterInfo.
							b_PositionTurnLength;
							b_Cpt++) {
							dw_And = dw_And * 2;
						}

						*pul_Position1 =
							*pul_Position1 &
							((dw_And) - 1);

						*pul_TurnCpt1 =
							dw_CounterValue >>
							b_Length;

						dw_And = 1;

						for (b_Cpt = 0;
							b_Cpt <
							devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_SSICounterInfo.
							b_TurnCptLength;
							b_Cpt++) {
							dw_And = dw_And * 2;
						}

						*pul_TurnCpt1 =
							*pul_TurnCpt1 &
							((dw_And) - 1);
					} else {
		    /*****************************/
						/* The selected SSI is wrong */
		    /*****************************/

						DPRINTK("The selected SSI is wrong\n");
						i_ReturnValue = -5;
					}
					break;

				case APCI1710_SSI_READALLVALUE:
					dw_And1 = 1;

					for (b_Cpt = 0;
						b_Cpt <
						devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_SSICounterInfo.
						b_PositionTurnLength; b_Cpt++) {
						dw_And1 = dw_And1 * 2;
					}

					dw_And2 = 1;

					for (b_Cpt = 0;
						b_Cpt <
						devpriv->
						s_ModuleInfo[b_ModulNbr].
						s_SSICounterInfo.
						b_TurnCptLength; b_Cpt++) {
						dw_And2 = dw_And2 * 2;
					}

		 /************************/
					/* Start the conversion */
		 /************************/

					outl(0, devpriv->s_BoardInfos.
						ui_Address + 8 +
						(64 * b_ModulNbr));

					do {
		    /*******************/
						/* Read the status */
		    /*******************/

						dw_StatusReg =
							inl(devpriv->
							s_BoardInfos.
							ui_Address +
							(64 * b_ModulNbr));
					} while ((dw_StatusReg & 0x1) != 0);

					for (b_SSICpt = 0; b_SSICpt < 3;
						b_SSICpt++) {
		    /******************************/
						/* Read the SSI counter value */
		    /******************************/

						dw_CounterValue =
							inl(devpriv->
							s_BoardInfos.
							ui_Address + 4 +
							(b_SSICpt * 4) +
							(64 * b_ModulNbr));

						b_Length =
							devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_SSICounterInfo.
							b_SSIProfile / 2;

						if ((b_Length * 2) !=
							devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_SSICounterInfo.
							b_SSIProfile) {
							b_Length++;
						}

						b_Schift =
							b_Length -
							devpriv->
							s_ModuleInfo
							[b_ModulNbr].
							s_SSICounterInfo.
							b_PositionTurnLength;

						pul_Position[b_SSICpt] =
							dw_CounterValue >>
							b_Schift;
						pul_Position[b_SSICpt] =
							pul_Position[b_SSICpt] &
							((dw_And1) - 1);

						pul_TurnCpt[b_SSICpt] =
							dw_CounterValue >>
							b_Length;
						pul_TurnCpt[b_SSICpt] =
							pul_TurnCpt[b_SSICpt] &
							((dw_And2) - 1);
					}
					break;

				default:
					printk("Read Type Inputs Wrong\n");

				}	/*  switch  ending */

			} else {
		 /***********************/
				/* SSI not initialised */
		 /***********************/

				DPRINTK("SSI not initialised\n");
				i_ReturnValue = -4;
			}
		} else {
	      /**********************************/
			/* The module is not a SSI module */
	      /**********************************/

			DPRINTK("The module is not a SSI module\n");
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
| Function Name     : _INT_   i_APCI1710_ReadSSI1DigitalInput                |
|                                       (unsigned char_     b_BoardHandle,            |
|                                        unsigned char_     b_ModulNbr,               |
|                                        unsigned char_     b_InputChannel,           |
|                                        unsigned char *_   pb_ChannelStatus)          |
+----------------------------------------------------------------------------+
| Task              :
					(0) Set the digital output from selected SSI module         |
|                     (b_ModuleNbr) ON
                    (1) Set the digital output from selected SSI module         |
|                     (b_ModuleNbr) OFF
					(2)Read the status from selected SSI digital input        |
|                     (b_InputChannel)
                    (3)Read the status from all SSI digital inputs from       |
|                     selected SSI module (b_ModulNbr)                   |
+----------------------------------------------------------------------------+
| Input Parameters  : unsigned char_ b_BoardHandle         : Handle of board APCI-1710|
|                     unsigned char_ b_ModulNbr    CR_AREF        : Module number to         |
|                                                   configure (0 to 3)       |
|                     unsigned char_ b_InputChannel CR_CHAN       : Selection from digital   |
|                        data[0] which IOTYPE                           input ( 0 to 2)          |
+----------------------------------------------------------------------------+
| Output Parameters : unsigned char *_  pb_ChannelStatus    : Digital input channel    |
|                                 data[0]                  status                   |
|                                                   0 : Channle is not active|
|                                                   1 : Channle is active    |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -1: The handle parameter of the board is wrong          |
|                    -2: The module parameter is wrong                       |
|                    -3: The module is not a SSI module                      |
|                    -4: The selected SSI digital input is wrong             |
+----------------------------------------------------------------------------+
*/

static int i_APCI1710_InsnBitsSSIDigitalIO(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   struct comedi_insn *insn,
					   unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	int i_ReturnValue = 0;
	unsigned int dw_StatusReg;
	unsigned char b_ModulNbr;
	unsigned char b_InputChannel;
	unsigned char *pb_ChannelStatus;
	unsigned char *pb_InputStatus;
	unsigned char b_IOType;

	i_ReturnValue = insn->n;
	b_ModulNbr = (unsigned char) CR_AREF(insn->chanspec);
	b_IOType = (unsigned char) data[0];

	/**************************/
	/* Test the module number */
	/**************************/

	if (b_ModulNbr < 4) {
	   /***********************/
		/* Test if SSI counter */
	   /***********************/

		if ((devpriv->s_BoardInfos.
				dw_MolduleConfiguration[b_ModulNbr] &
				0xFFFF0000UL) == APCI1710_SSI_COUNTER) {
			switch (b_IOType) {
			case APCI1710_SSI_SET_CHANNELON:
					/*****************************/
				/* Set the digital output ON */
					/*****************************/

				outl(1, devpriv->s_BoardInfos.ui_Address + 16 +
					(64 * b_ModulNbr));
				break;

			case APCI1710_SSI_SET_CHANNELOFF:
					/******************************/
				/* Set the digital output OFF */
					/******************************/

				outl(0, devpriv->s_BoardInfos.ui_Address + 16 +
					(64 * b_ModulNbr));
				break;

			case APCI1710_SSI_READ_1CHANNEL:
				   /******************************************/
				/* Test the digital imnput channel number */
				   /******************************************/

				b_InputChannel = (unsigned char) CR_CHAN(insn->chanspec);
				pb_ChannelStatus = (unsigned char *) &data[0];

				if (b_InputChannel <= 2) {
					/**************************/
					/* Read all digital input */
					/**************************/

					dw_StatusReg =
						inl(devpriv->s_BoardInfos.
						ui_Address + (64 * b_ModulNbr));
					*pb_ChannelStatus =
						(unsigned char) (((~dw_StatusReg) >> (4 +
								b_InputChannel))
						& 1);
				} else {
					/********************************/
					/* Selected digital input error */
					/********************************/

					DPRINTK("Selected digital input error\n");
					i_ReturnValue = -4;
				}
				break;

			case APCI1710_SSI_READ_ALLCHANNEL:
					/**************************/
				/* Read all digital input */
					/**************************/
				pb_InputStatus = (unsigned char *) &data[0];

				dw_StatusReg =
					inl(devpriv->s_BoardInfos.ui_Address +
					(64 * b_ModulNbr));
				*pb_InputStatus =
					(unsigned char) (((~dw_StatusReg) >> 4) & 7);
				break;

			default:
				printk("IO type wrong\n");

			}	/* switch end */
		} else {
	      /**********************************/
			/* The module is not a SSI module */
	      /**********************************/

			DPRINTK("The module is not a SSI module\n");
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
