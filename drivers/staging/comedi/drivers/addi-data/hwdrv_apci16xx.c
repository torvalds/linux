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
  | Project     : API APCI1648    | Compiler : gcc                        |
  | Module name : TTL.C           | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Project manager: S. Weber     | Date     :  25/05/2005                |
  +-----------------------------------------------------------------------+
  | Description :   APCI-16XX TTL I/O module                              |
  |                                                                       |
  |                                                                       |
  +-----------------------------------------------------------------------+
  |                             UPDATES                                   |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |25.05.2005| S.Weber   | Creation                                       |
  |          |           |                                                |
  +-----------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
|                               Included files                               |
+----------------------------------------------------------------------------+
*/

#include "hwdrv_apci16xx.h"

/*
+----------------------------------------------------------------------------+
| Function Name     : INT   i_APCI16XX_InsnConfigInitTTLIO                   |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task           APCI16XX_TTL_INIT (using defaults)   :                      |
|                Configure the TTL I/O operating mode from all ports         |
|                You must calling this function be                           |
|                for you call any other function witch access of TTL.        |
|                APCI16XX_TTL_INITDIRECTION(user inputs for direction)       |
+----------------------------------------------------------------------------+
| Input Parameters  : b_InitType    = (BYTE) data[0];                        |
|                     b_Port0Mode   = (BYTE) data[1];                        |
|                     b_Port1Mode   = (BYTE) data[2];                        |
|                     b_Port2Mode   = (BYTE) data[3];                        |
|                     b_Port3Mode   = (BYTE) data[4];                        |
|                     ........                                               |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :>0: No error                                            |
|                    -1: Port 0 mode selection is wrong                      |
|                    -2: Port 1 mode selection is wrong                      |
|                    -3: Port 2 mode selection is wrong                      |
|                    -4: Port 3 mode selection is wrong                      |
|                    -X: Port X-1 mode selection is wrong                    |
|                    ....                                                    |
|                    -100 : Config command error                             |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI16XX_InsnConfigInitTTLIO(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	INT i_ReturnValue = insn->n;
	BYTE b_Command = 0;
	BYTE b_Cpt = 0;
	BYTE b_NumberOfPort =
		(BYTE) (devpriv->ps_BoardInfo->i_NbrTTLChannel / 8);

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
	   /*******************/
		/* Get the command */
		/* **************** */

		b_Command = (BYTE) data[0];

	   /********************/
		/* Test the command */
	   /********************/

		if ((b_Command == APCI16XX_TTL_INIT) ||
			(b_Command == APCI16XX_TTL_INITDIRECTION) ||
			(b_Command == APCI16XX_TTL_OUTPUTMEMORY)) {
	      /***************************************/
			/* Test the initialisation buffer size */
	      /***************************************/

			if ((b_Command == APCI16XX_TTL_INITDIRECTION)
				&& ((BYTE) (insn->n - 1) != b_NumberOfPort)) {
		 /*******************/
				/* Data size error */
		 /*******************/

				printk("\nBuffer size error");
				i_ReturnValue = -101;
			}

			if ((b_Command == APCI16XX_TTL_OUTPUTMEMORY)
				&& ((BYTE) (insn->n) != 2)) {
		 /*******************/
				/* Data size error */
		 /*******************/

				printk("\nBuffer size error");
				i_ReturnValue = -101;
			}
		} else {
	      /************************/
			/* Config command error */
	      /************************/

			printk("\nCommand selection error");
			i_ReturnValue = -100;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("\nBuffer size error");
		i_ReturnValue = -101;
	}

	/**************************************************************************/
	/* Test if no error occur and APCI16XX_TTL_INITDIRECTION command selected */
	/**************************************************************************/

	if ((i_ReturnValue >= 0) && (b_Command == APCI16XX_TTL_INITDIRECTION)) {
		memset(devpriv->ul_TTLPortConfiguration, 0,
			sizeof(devpriv->ul_TTLPortConfiguration));

	   /*************************************/
		/* Test the port direction selection */
	   /*************************************/

		for (b_Cpt = 1;
			(b_Cpt <= b_NumberOfPort) && (i_ReturnValue >= 0);
			b_Cpt++) {
	      /**********************/
			/* Test the direction */
	      /**********************/

			if ((data[b_Cpt] != 0) && (data[b_Cpt] != 0xFF)) {
		 /************************/
				/* Port direction error */
		 /************************/

				printk("\nPort %d direction selection error",
					(INT) b_Cpt);
				i_ReturnValue = -(INT) b_Cpt;
			}

	      /**************************/
			/* Save the configuration */
	      /**************************/

			devpriv->ul_TTLPortConfiguration[(b_Cpt - 1) / 4] =
				devpriv->ul_TTLPortConfiguration[(b_Cpt -
					1) / 4] | (data[b_Cpt] << (8 * ((b_Cpt -
							1) % 4)));
		}
	}

	/**************************/
	/* Test if no error occur */
	/**************************/

	if (i_ReturnValue >= 0) {
	   /***********************************/
		/* Test if TTL port initilaisation */
	   /***********************************/

		if ((b_Command == APCI16XX_TTL_INIT)
			|| (b_Command == APCI16XX_TTL_INITDIRECTION)) {
	      /******************************/
			/* Set all port configuration */
	      /******************************/

			for (b_Cpt = 0; b_Cpt <= b_NumberOfPort; b_Cpt++) {
				if ((b_Cpt % 4) == 0) {
		    /*************************/
					/* Set the configuration */
		    /*************************/

					outl(devpriv->
						ul_TTLPortConfiguration[b_Cpt /
							4],
						devpriv->iobase + 32 + b_Cpt);
				}
			}
		}
	}

	/************************************************/
	/* Test if output memory initialisation command */
	/************************************************/

	if (b_Command == APCI16XX_TTL_OUTPUTMEMORY) {
		if (data[1]) {
			devpriv->b_OutputMemoryStatus = ADDIDATA_ENABLE;
		} else {
			devpriv->b_OutputMemoryStatus = ADDIDATA_DISABLE;
		}
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
|                            INPUT FUNCTIONS                                 |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : INT     i_APCI16XX_InsnBitsReadTTLIO                   |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task              : Read the status from selected TTL digital input        |
|                     (b_InputChannel)                                       |
+----------------------------------------------------------------------------+
| Task              : Read the status from digital input port                |
|                     (b_SelectedPort)                                       |
+----------------------------------------------------------------------------+
| Input Parameters  :                                                        |
|              APCI16XX_TTL_READCHANNEL                                      |
|                    b_SelectedPort= CR_RANGE(insn->chanspec);               |
|                    b_InputChannel= CR_CHAN(insn->chanspec);                |
|                    b_ReadType	  = (BYTE) data[0];                          |
|                                                                            |
|              APCI16XX_TTL_READPORT                                         |
|                    b_SelectedPort= CR_RANGE(insn->chanspec);               |
|                    b_ReadType	  = (BYTE) data[0];                          |
+----------------------------------------------------------------------------+
| Output Parameters : data[0]    0 : Channle is not active                   |
|                                1 : Channle is active                       |
+----------------------------------------------------------------------------+
| Return Value      : >0  : No error                                         |
|                    -100 : Config command error                             |
|                    -101 : Data size error                                  |
|                    -102 : The selected TTL input port is wrong             |
|                    -103 : The selected TTL digital input is wrong          |
+----------------------------------------------------------------------------+
*/

int i_APCI16XX_InsnBitsReadTTLIO(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	INT i_ReturnValue = insn->n;
	BYTE b_Command = 0;
	BYTE b_NumberOfPort =
		(BYTE) (devpriv->ps_BoardInfo->i_NbrTTLChannel / 8);
	BYTE b_SelectedPort = CR_RANGE(insn->chanspec);
	BYTE b_InputChannel = CR_CHAN(insn->chanspec);
	BYTE *pb_Status;
	DWORD dw_Status;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
	   /*******************/
		/* Get the command */
		/* **************** */

		b_Command = (BYTE) data[0];

	   /********************/
		/* Test the command */
	   /********************/

		if ((b_Command == APCI16XX_TTL_READCHANNEL)
			|| (b_Command == APCI16XX_TTL_READPORT)) {
	      /**************************/
			/* Test the selected port */
	      /**************************/

			if (b_SelectedPort < b_NumberOfPort) {
		 /**********************/
				/* Test if input port */
		 /**********************/

				if (((devpriv->ul_TTLPortConfiguration
							[b_SelectedPort /
								4] >> (8 *
								(b_SelectedPort
									%
									4))) &
						0xFF) == 0) {
		    /***************************/
					/* Test the channel number */
		    /***************************/

					if ((b_Command ==
							APCI16XX_TTL_READCHANNEL)
						&& (b_InputChannel > 7)) {
		       /*******************************************/
						/* The selected TTL digital input is wrong */
		       /*******************************************/

						printk("\nChannel selection error");
						i_ReturnValue = -103;
					}
				} else {
		    /****************************************/
					/* The selected TTL input port is wrong */
		    /****************************************/

					printk("\nPort selection error");
					i_ReturnValue = -102;
				}
			} else {
		 /****************************************/
				/* The selected TTL input port is wrong */
		 /****************************************/

				printk("\nPort selection error");
				i_ReturnValue = -102;
			}
		} else {
	      /************************/
			/* Config command error */
	      /************************/

			printk("\nCommand selection error");
			i_ReturnValue = -100;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("\nBuffer size error");
		i_ReturnValue = -101;
	}

	/**************************/
	/* Test if no error occur */
	/**************************/

	if (i_ReturnValue >= 0) {
		pb_Status = (PBYTE) & data[0];

	   /*******************************/
		/* Get the digital inpu status */
	   /*******************************/

		dw_Status =
			inl(devpriv->iobase + 8 + ((b_SelectedPort / 4) * 4));
		dw_Status = (dw_Status >> (8 * (b_SelectedPort % 4))) & 0xFF;

	   /***********************/
		/* Save the port value */
	   /***********************/

		*pb_Status = (BYTE) dw_Status;

	   /***************************************/
		/* Test if read channel status command */
	   /***************************************/

		if (b_Command == APCI16XX_TTL_READCHANNEL) {
			*pb_Status = (*pb_Status >> b_InputChannel) & 1;
		}
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function Name     : INT i_APCI16XX_InsnReadTTLIOAllPortValue               |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task              : Read the status from all digital input ports           |
+----------------------------------------------------------------------------+
| Input Parameters  : -                                                      |
+----------------------------------------------------------------------------+
| Output Parameters : data[0] : Port 0 to 3 data                             |
|                     data[1] : Port 4 to 7 data                             |
|                     ....                                                   |
+----------------------------------------------------------------------------+
| Return Value      : 0: No error                                            |
|                    -100 : Read command error                               |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI16XX_InsnReadTTLIOAllPortValue(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	BYTE b_Command = (BYTE) CR_AREF(insn->chanspec);
	INT i_ReturnValue = insn->n;
	BYTE b_Cpt = 0;
	BYTE b_NumberOfPort = 0;
	unsigned int *pls_ReadData = data;

	/********************/
	/* Test the command */
	/********************/

	if ((b_Command == APCI16XX_TTL_READ_ALL_INPUTS)
		|| (b_Command == APCI16XX_TTL_READ_ALL_OUTPUTS)) {
	   /**********************************/
		/* Get the number of 32-Bit ports */
	   /**********************************/

		b_NumberOfPort =
			(BYTE) (devpriv->ps_BoardInfo->i_NbrTTLChannel / 32);
		if ((b_NumberOfPort * 32) <
			devpriv->ps_BoardInfo->i_NbrTTLChannel) {
			b_NumberOfPort = b_NumberOfPort + 1;
		}

	   /************************/
		/* Test the buffer size */
	   /************************/

		if (insn->n >= b_NumberOfPort) {
			if (b_Command == APCI16XX_TTL_READ_ALL_INPUTS) {
		 /**************************/
				/* Read all digital input */
		 /**************************/

				for (b_Cpt = 0; b_Cpt < b_NumberOfPort; b_Cpt++) {
		    /************************/
					/* Read the 32-Bit port */
		    /************************/

					pls_ReadData[b_Cpt] =
						inl(devpriv->iobase + 8 +
						(b_Cpt * 4));

		    /**************************************/
					/* Mask all channels used als outputs */
		    /**************************************/

					pls_ReadData[b_Cpt] =
						pls_ReadData[b_Cpt] &
						(~devpriv->
						ul_TTLPortConfiguration[b_Cpt]);
				}
			} else {
		 /****************************/
				/* Read all digital outputs */
		 /****************************/

				for (b_Cpt = 0; b_Cpt < b_NumberOfPort; b_Cpt++) {
		    /************************/
					/* Read the 32-Bit port */
		    /************************/

					pls_ReadData[b_Cpt] =
						inl(devpriv->iobase + 20 +
						(b_Cpt * 4));

		    /**************************************/
					/* Mask all channels used als outputs */
		    /**************************************/

					pls_ReadData[b_Cpt] =
						pls_ReadData[b_Cpt] & devpriv->
						ul_TTLPortConfiguration[b_Cpt];
				}
			}
		} else {
	      /*******************/
			/* Data size error */
	      /*******************/

			printk("\nBuffer size error");
			i_ReturnValue = -101;
		}
	} else {
	   /*****************/
		/* Command error */
	   /*****************/

		printk("\nCommand selection error");
		i_ReturnValue = -100;
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
| Function Name     : INT     i_APCI16XX_InsnBitsWriteTTLIO                  |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task              : Set the state from selected TTL digital output         |
|                     (b_OutputChannel)                                      |
+----------------------------------------------------------------------------+
| Task              : Set the state from digital output port                 |
|                     (b_SelectedPort)                                       |
+----------------------------------------------------------------------------+
| Input Parameters  :                                                        |
|              APCI16XX_TTL_WRITECHANNEL_ON | APCI16XX_TTL_WRITECHANNEL_OFF  |
|                    b_SelectedPort = CR_RANGE(insn->chanspec);              |
|                    b_OutputChannel= CR_CHAN(insn->chanspec);               |
|                    b_Command      = (BYTE) data[0];                        |
|                                                                            |
|              APCI16XX_TTL_WRITEPORT_ON | APCI16XX_TTL_WRITEPORT_OFF        |
|                    b_SelectedPort = CR_RANGE(insn->chanspec);              |
|                    b_Command      = (BYTE) data[0];                        |
+----------------------------------------------------------------------------+
| Output Parameters : data[0] : TTL output port 0 to 3 data                  |
|                     data[1] : TTL output port 4 to 7 data                  |
|                     ....                                                   |
+----------------------------------------------------------------------------+
| Return Value      : >0  : No error                                         |
|                    -100 : Command error                                    |
|                    -101 : Data size error                                  |
|                    -102 : The selected TTL output port is wrong            |
|                    -103 : The selected TTL digital output is wrong         |
|                    -104 : Output memory disabled                           |
+----------------------------------------------------------------------------+
*/

int i_APCI16XX_InsnBitsWriteTTLIO(struct comedi_device * dev,
	struct comedi_subdevice * s, struct comedi_insn * insn, unsigned int * data)
{
	INT i_ReturnValue = insn->n;
	BYTE b_Command = 0;
	BYTE b_NumberOfPort =
		(BYTE) (devpriv->ps_BoardInfo->i_NbrTTLChannel / 8);
	BYTE b_SelectedPort = CR_RANGE(insn->chanspec);
	BYTE b_OutputChannel = CR_CHAN(insn->chanspec);
	DWORD dw_Status = 0;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
	   /*******************/
		/* Get the command */
		/* **************** */

		b_Command = (BYTE) data[0];

	   /********************/
		/* Test the command */
	   /********************/

		if ((b_Command == APCI16XX_TTL_WRITECHANNEL_ON) ||
			(b_Command == APCI16XX_TTL_WRITEPORT_ON) ||
			(b_Command == APCI16XX_TTL_WRITECHANNEL_OFF) ||
			(b_Command == APCI16XX_TTL_WRITEPORT_OFF)) {
	      /**************************/
			/* Test the selected port */
	      /**************************/

			if (b_SelectedPort < b_NumberOfPort) {
		 /***********************/
				/* Test if output port */
		 /***********************/

				if (((devpriv->ul_TTLPortConfiguration
							[b_SelectedPort /
								4] >> (8 *
								(b_SelectedPort
									%
									4))) &
						0xFF) == 0xFF) {
		    /***************************/
					/* Test the channel number */
		    /***************************/

					if (((b_Command == APCI16XX_TTL_WRITECHANNEL_ON) || (b_Command == APCI16XX_TTL_WRITECHANNEL_OFF)) && (b_OutputChannel > 7)) {
		       /********************************************/
						/* The selected TTL digital output is wrong */
		       /********************************************/

						printk("\nChannel selection error");
						i_ReturnValue = -103;
					}

					if (((b_Command == APCI16XX_TTL_WRITECHANNEL_OFF) || (b_Command == APCI16XX_TTL_WRITEPORT_OFF)) && (devpriv->b_OutputMemoryStatus == ADDIDATA_DISABLE)) {
		       /********************************************/
						/* The selected TTL digital output is wrong */
		       /********************************************/

						printk("\nOutput memory disabled");
						i_ReturnValue = -104;
					}

		    /************************/
					/* Test the buffer size */
		    /************************/

					if (((b_Command == APCI16XX_TTL_WRITEPORT_ON) || (b_Command == APCI16XX_TTL_WRITEPORT_OFF)) && (insn->n < 2)) {
		       /*******************/
						/* Data size error */
		       /*******************/

						printk("\nBuffer size error");
						i_ReturnValue = -101;
					}
				} else {
		    /*****************************************/
					/* The selected TTL output port is wrong */
		    /*****************************************/

					printk("\nPort selection error %lX",
						(unsigned long)devpriv->
						ul_TTLPortConfiguration[0]);
					i_ReturnValue = -102;
				}
			} else {
		 /****************************************/
				/* The selected TTL output port is wrong */
		 /****************************************/

				printk("\nPort selection error %d %d",
					b_SelectedPort, b_NumberOfPort);
				i_ReturnValue = -102;
			}
		} else {
	      /************************/
			/* Config command error */
	      /************************/

			printk("\nCommand selection error");
			i_ReturnValue = -100;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("\nBuffer size error");
		i_ReturnValue = -101;
	}

	/**************************/
	/* Test if no error occur */
	/**************************/

	if (i_ReturnValue >= 0) {
	   /********************************/
		/* Get the digital output state */
	   /********************************/

		dw_Status =
			inl(devpriv->iobase + 20 + ((b_SelectedPort / 4) * 4));

	   /**********************************/
		/* Test if output memory not used */
	   /**********************************/

		if (devpriv->b_OutputMemoryStatus == ADDIDATA_DISABLE) {
	      /*********************************/
			/* Clear the selected port value */
	      /*********************************/

			dw_Status =
				dw_Status & (0xFFFFFFFFUL -
				(0xFFUL << (8 * (b_SelectedPort % 4))));
		}

	   /******************************/
		/* Test if setting channel ON */
	   /******************************/

		if (b_Command == APCI16XX_TTL_WRITECHANNEL_ON) {
			dw_Status =
				dw_Status | (1UL << ((8 * (b_SelectedPort %
							4)) + b_OutputChannel));
		}

	   /***************************/
		/* Test if setting port ON */
	   /***************************/

		if (b_Command == APCI16XX_TTL_WRITEPORT_ON) {
			dw_Status =
				dw_Status | ((data[1] & 0xFF) << (8 *
					(b_SelectedPort % 4)));
		}

	   /*******************************/
		/* Test if setting channel OFF */
	   /*******************************/

		if (b_Command == APCI16XX_TTL_WRITECHANNEL_OFF) {
			dw_Status =
				dw_Status & (0xFFFFFFFFUL -
				(1UL << ((8 * (b_SelectedPort % 4)) +
						b_OutputChannel)));
		}

	   /****************************/
		/* Test if setting port OFF */
	   /****************************/

		if (b_Command == APCI16XX_TTL_WRITEPORT_OFF) {
			dw_Status =
				dw_Status & (0xFFFFFFFFUL -
				((data[1] & 0xFF) << (8 * (b_SelectedPort %
							4))));
		}

		outl(dw_Status,
			devpriv->iobase + 20 + ((b_SelectedPort / 4) * 4));
	}

	return (i_ReturnValue);
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI2200_Reset(struct comedi_device *dev)               |                                                         +----------------------------------------------------------------------------+
| Task              :resets all the registers                                |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev                                     |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : -                                                      |
+----------------------------------------------------------------------------+
*/

int i_APCI16XX_Reset(struct comedi_device * dev)
{
	return 0;
}
