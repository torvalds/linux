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
  | (C) ADDI-DATA GmbH          Dieselstrasse 3      D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-----------------------------------------------------------------------+
  | Project     : APCI-3XXX       | Compiler   : GCC                      |
  | Module name : hwdrv_apci3xxx.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: S. Weber     | Date       :  15/09/2005              |
  +-----------------------------------------------------------------------+
  | Description :APCI3XXX Module.  Hardware abstraction Layer for APCI3XXX|
  +-----------------------------------------------------------------------+
  |                             UPDATE'S                                  |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |          | 		 | 						  |
  |          |           |						  |
  +----------+-----------+------------------------------------------------+
*/

#include "hwdrv_apci3xxx.h"

/*
+----------------------------------------------------------------------------+
|                         ANALOG INPUT FUNCTIONS                             |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : int   i_APCI3XXX_TestConversionStarted                 |
|                          (struct comedi_device    *dev)                           |
+----------------------------------------------------------------------------+
| Task                Test if any conversion started                         |
+----------------------------------------------------------------------------+
| Input Parameters  : -                                                      |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0 : Conversion not started                             |
|                     1 : Conversion started                                 |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_TestConversionStarted(struct comedi_device *dev)
{
	if ((readl((void *)(devpriv->dw_AiBase + 8)) & 0x80000UL) == 0x80000UL)
		return 1;
	else
		return 0;

}

/*
+----------------------------------------------------------------------------+
| Function Name     : int   i_APCI3XXX_AnalogInputConfigOperatingMode        |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task           Converting mode and convert time selection                  |
+----------------------------------------------------------------------------+
| Input Parameters  : b_SingleDiff  = (unsigned char)  data[1];                       |
|                     b_TimeBase    = (unsigned char)  data[2]; (0: ns, 1:micros 2:ms)|
|                    dw_ReloadValue = (unsigned int) data[3];                       |
|                     ........                                               |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :>0 : No error                                           |
|                    -1 : Single/Diff selection error                        |
|                    -2 : Convert time base unity selection error            |
|                    -3 : Convert time value selection error                 |
|                    -10: Any conversion started                             |
|                    ....                                                    |
|                    -100 : Config command error                             |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_AnalogInputConfigOperatingMode(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned char b_TimeBase = 0;
	unsigned char b_SingleDiff = 0;
	unsigned int dw_ReloadValue = 0;
	unsigned int dw_TestReloadValue = 0;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n == 4) {
	   /****************************/
		/* Get the Singel/Diff flag */
	   /****************************/

		b_SingleDiff = (unsigned char) data[1];

	   /****************************/
		/* Get the time base unitiy */
	   /****************************/

		b_TimeBase = (unsigned char) data[2];

	   /*************************************/
		/* Get the convert time reload value */
	   /*************************************/

		dw_ReloadValue = (unsigned int) data[3];

	   /**********************/
		/* Test the time base */
	   /**********************/

		if ((devpriv->ps_BoardInfo->
				b_AvailableConvertUnit & (1 << b_TimeBase)) !=
			0) {
	      /*******************************/
			/* Test the convert time value */
	      /*******************************/

			if ((dw_ReloadValue >= 0) && (dw_ReloadValue <= 65535)) {
				dw_TestReloadValue = dw_ReloadValue;

				if (b_TimeBase == 1) {
					dw_TestReloadValue =
						dw_TestReloadValue * 1000UL;
				}
				if (b_TimeBase == 2) {
					dw_TestReloadValue =
						dw_TestReloadValue * 1000000UL;
				}

		 /*******************************/
				/* Test the convert time value */
		 /*******************************/

				if (dw_TestReloadValue >=
					devpriv->ps_BoardInfo->
					ui_MinAcquisitiontimeNs) {
					if ((b_SingleDiff == APCI3XXX_SINGLE)
						|| (b_SingleDiff ==
							APCI3XXX_DIFF)) {
						if (((b_SingleDiff == APCI3XXX_SINGLE) && (devpriv->ps_BoardInfo->i_NbrAiChannel == 0)) || ((b_SingleDiff == APCI3XXX_DIFF) && (devpriv->ps_BoardInfo->i_NbrAiChannelDiff == 0))) {
			   /*******************************/
							/* Single/Diff selection error */
			   /*******************************/

							printk("Single/Diff selection error\n");
							i_ReturnValue = -1;
						} else {
			   /**********************************/
							/* Test if conversion not started */
			   /**********************************/

							if (i_APCI3XXX_TestConversionStarted(dev) == 0) {
								devpriv->
									ui_EocEosConversionTime
									=
									(unsigned int)
									dw_ReloadValue;
								devpriv->
									b_EocEosConversionTimeBase
									=
									b_TimeBase;
								devpriv->
									b_SingelDiff
									=
									b_SingleDiff;
								devpriv->
									b_AiInitialisation
									= 1;

			      /*******************************/
								/* Set the convert timing unit */
			      /*******************************/

								writel((unsigned int)
									b_TimeBase,
									(void *)
									(devpriv->
										dw_AiBase
										+
										36));

			      /**************************/
								/* Set the convert timing */
			      /*************************/

								writel(dw_ReloadValue, (void *)(devpriv->dw_AiBase + 32));
							} else {
			      /**************************/
								/* Any conversion started */
			      /**************************/

								printk("Any conversion started\n");
								i_ReturnValue =
									-10;
							}
						}
					} else {
		       /*******************************/
						/* Single/Diff selection error */
		       /*******************************/

						printk("Single/Diff selection error\n");
						i_ReturnValue = -1;
					}
				} else {
		    /************************/
					/* Time selection error */
		    /************************/

					printk("Convert time value selection error\n");
					i_ReturnValue = -3;
				}
			} else {
		 /************************/
				/* Time selection error */
		 /************************/

				printk("Convert time value selection error\n");
				i_ReturnValue = -3;
			}
		} else {
	      /*****************************/
			/* Time base selection error */
	      /*****************************/

			printk("Convert time base unity selection error\n");
			i_ReturnValue = -2;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : int   i_APCI3XXX_InsnConfigAnalogInput                 |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task           Converting mode and convert time selection                  |
+----------------------------------------------------------------------------+
| Input Parameters  : b_ConvertMode = (unsigned char)  data[0];                       |
|                     b_TimeBase    = (unsigned char)  data[1]; (0: ns, 1:micros 2:ms)|
|                    dw_ReloadValue = (unsigned int) data[2];                       |
|                     ........                                               |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :>0: No error                                            |
|                    ....                                                    |
|                    -100 : Config command error                             |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnConfigAnalogInput(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
		switch ((unsigned char) data[0]) {
		case APCI3XXX_CONFIGURATION:
			i_ReturnValue =
				i_APCI3XXX_AnalogInputConfigOperatingMode(dev,
				s, insn, data);
			break;

		default:
			i_ReturnValue = -100;
			printk("Config command error %d\n", data[0]);
			break;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : int   i_APCI3XXX_InsnReadAnalogInput                   |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task                Read 1 analog input                                    |
+----------------------------------------------------------------------------+
| Input Parameters  : b_Range             = CR_RANGE(insn->chanspec);        |
|                     b_Channel           = CR_CHAN(insn->chanspec);         |
|                     dw_NbrOfAcquisition = insn->n;                         |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :>0: No error                                            |
|                    -3 : Channel selection error                            |
|                    -4 : Configuration selelection error                    |
|                    -10: Any conversion started                             |
|                    ....                                                    |
|                    -100 : Config command error                             |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnReadAnalogInput(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned char b_Configuration = (unsigned char) CR_RANGE(insn->chanspec);
	unsigned char b_Channel = (unsigned char) CR_CHAN(insn->chanspec);
	unsigned int dw_Temp = 0;
	unsigned int dw_Configuration = 0;
	unsigned int dw_AcquisitionCpt = 0;
	unsigned char b_Interrupt = 0;

	/*************************************/
	/* Test if operating mode configured */
	/*************************************/

	if (devpriv->b_AiInitialisation) {
	   /***************************/
		/* Test the channel number */
	   /***************************/

		if (((b_Channel < devpriv->ps_BoardInfo->i_NbrAiChannel)
				&& (devpriv->b_SingelDiff == APCI3XXX_SINGLE))
			|| ((b_Channel < devpriv->ps_BoardInfo->
					i_NbrAiChannelDiff)
				&& (devpriv->b_SingelDiff == APCI3XXX_DIFF))) {
	      /**********************************/
			/* Test the channel configuration */
	      /**********************************/

			if (b_Configuration > 7) {
		 /***************************/
				/* Channel not initialised */
		 /***************************/

				i_ReturnValue = -4;
				printk("Channel %d range %d selection error\n",
					b_Channel, b_Configuration);
			}
		} else {
	      /***************************/
			/* Channel selection error */
	      /***************************/

			i_ReturnValue = -3;
			printk("Channel %d selection error\n", b_Channel);
		}

	   /**************************/
		/* Test if no error occur */
	   /**************************/

		if (i_ReturnValue >= 0) {
	      /************************/
			/* Test the buffer size */
	      /************************/

			if ((b_Interrupt != 0) || ((b_Interrupt == 0)
					&& (insn->n >= 1))) {
		 /**********************************/
				/* Test if conversion not started */
		 /**********************************/

				if (i_APCI3XXX_TestConversionStarted(dev) == 0) {
		    /******************/
					/* Clear the FIFO */
		    /******************/

					writel(0x10000UL,
						(void *)(devpriv->dw_AiBase +
							12));

		    /*******************************/
					/* Get and save the delay mode */
		    /*******************************/

					dw_Temp =
						readl((void *)(devpriv->
							dw_AiBase + 4));
					dw_Temp = dw_Temp & 0xFFFFFEF0UL;

		    /***********************************/
					/* Channel configuration selection */
		    /***********************************/

					writel(dw_Temp,
						(void *)(devpriv->dw_AiBase +
							4));

		    /**************************/
					/* Make the configuration */
		    /**************************/

					dw_Configuration =
						(b_Configuration & 3) |
						((unsigned int) (b_Configuration >> 2)
						<< 6) | ((unsigned int) devpriv->
						b_SingelDiff << 7);

		    /***************************/
					/* Write the configuration */
		    /***************************/

					writel(dw_Configuration,
						(void *)(devpriv->dw_AiBase +
							0));

		    /*********************/
					/* Channel selection */
		    /*********************/

					writel(dw_Temp | 0x100UL,
						(void *)(devpriv->dw_AiBase +
							4));
					writel((unsigned int) b_Channel,
						(void *)(devpriv->dw_AiBase +
							0));

		    /***********************/
					/* Restaure delay mode */
		    /***********************/

					writel(dw_Temp,
						(void *)(devpriv->dw_AiBase +
							4));

		    /***********************************/
					/* Set the number of sequence to 1 */
		    /***********************************/

					writel(1,
						(void *)(devpriv->dw_AiBase +
							48));

		    /***************************/
					/* Save the interrupt flag */
		    /***************************/

					devpriv->b_EocEosInterrupt =
						b_Interrupt;

		    /*******************************/
					/* Save the number of channels */
		    /*******************************/

					devpriv->ui_AiNbrofChannels = 1;

		    /******************************/
					/* Test if interrupt not used */
		    /******************************/

					if (b_Interrupt == 0) {
						for (dw_AcquisitionCpt = 0;
							dw_AcquisitionCpt <
							insn->n;
							dw_AcquisitionCpt++) {
			  /************************/
							/* Start the conversion */
			  /************************/

							writel(0x80000UL,
								(void *)
								(devpriv->
									dw_AiBase
									+ 8));

			  /****************/
							/* Wait the EOS */
			  /****************/

							do {
								dw_Temp =
									readl(
									(void *)
									(devpriv->
										dw_AiBase
										+
										20));
								dw_Temp =
									dw_Temp
									& 1;
							}
							while (dw_Temp != 1);

			  /*************************/
							/* Read the analog value */
			  /*************************/

							data[dw_AcquisitionCpt]
								=
								(unsigned int)
								readl((void
									*)
								(devpriv->
									dw_AiBase
									+ 28));
						}
					} else {
		       /************************/
						/* Start the conversion */
		       /************************/

						writel(0x180000UL,
							(void *)(devpriv->
								dw_AiBase + 8));
					}
				} else {
		    /**************************/
					/* Any conversion started */
		    /**************************/

					printk("Any conversion started\n");
					i_ReturnValue = -10;
				}
			} else {
		 /*******************/
				/* Data size error */
		 /*******************/

				printk("Buffer size error\n");
				i_ReturnValue = -101;
			}
		}
	} else {
	   /***************************/
		/* Channel selection error */
	   /***************************/

		printk("Operating mode not configured\n");
		i_ReturnValue = -1;
	}
	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function name     : void v_APCI3XXX_Interrupt (int            irq,         |
|                                                void           *d)       |
+----------------------------------------------------------------------------+
| Task              :Interrupt handler for APCI3XXX                          |
|                    When interrupt occurs this gets called.                 |
|                    First it finds which interrupt has been generated and   |
|                    handles  corresponding interrupt                        |
+----------------------------------------------------------------------------+
| Input Parameters  : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : -                                                      |
+----------------------------------------------------------------------------+
*/

void v_APCI3XXX_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	unsigned char b_CopyCpt = 0;
	unsigned int dw_Status = 0;

	/***************************/
	/* Test if interrupt occur */
	/***************************/

	dw_Status = readl((void *)(devpriv->dw_AiBase + 16));
	if ( (dw_Status & 0x2UL) == 0x2UL) {
	   /***********************/
		/* Reset the interrupt */
	   /***********************/

		writel(dw_Status, (void *)(devpriv->dw_AiBase + 16));

	   /*****************************/
		/* Test if interrupt enabled */
	   /*****************************/

		if (devpriv->b_EocEosInterrupt == 1) {
	      /********************************/
			/* Read all analog inputs value */
	      /********************************/

			for (b_CopyCpt = 0;
				b_CopyCpt < devpriv->ui_AiNbrofChannels;
				b_CopyCpt++) {
				devpriv->ui_AiReadData[b_CopyCpt] =
					(unsigned int) readl((void *)(devpriv->
						dw_AiBase + 28));
			}

	      /**************************/
			/* Set the interrupt flag */
	      /**************************/

			devpriv->b_EocEosInterrupt = 2;

	      /**********************************************/
			/* Send a signal to from kernel to user space */
	      /**********************************************/

			send_sig(SIGIO, devpriv->tsk_Current, 0);
		}
	}
}

/*
+----------------------------------------------------------------------------+
|                            ANALOG OUTPUT SUBDEVICE                         |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : int   i_APCI3XXX_InsnWriteAnalogOutput                 |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task                Read 1 analog input                                    |
+----------------------------------------------------------------------------+
| Input Parameters  : b_Range    = CR_RANGE(insn->chanspec);                 |
|                     b_Channel  = CR_CHAN(insn->chanspec);                  |
|                     data[0]    = analog value;                             |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :>0: No error                                            |
|                    -3 : Channel selection error                            |
|                    -4 : Configuration selelection error                    |
|                    ....                                                    |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnWriteAnalogOutput(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	unsigned char b_Range = (unsigned char) CR_RANGE(insn->chanspec);
	unsigned char b_Channel = (unsigned char) CR_CHAN(insn->chanspec);
	unsigned int dw_Status = 0;
	int i_ReturnValue = insn->n;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
	   /***************************/
		/* Test the channel number */
	   /***************************/

		if (b_Channel < devpriv->ps_BoardInfo->i_NbrAoChannel) {
	      /**********************************/
			/* Test the channel configuration */
	      /**********************************/

			if (b_Range < 2) {
		 /***************************/
				/* Set the range selection */
		 /***************************/

				writel(b_Range,
					(void *)(devpriv->dw_AiBase + 96));

		 /**************************************************/
				/* Write the analog value to the selected channel */
		 /**************************************************/

				writel((data[0] << 8) | b_Channel,
					(void *)(devpriv->dw_AiBase + 100));

		 /****************************/
				/* Wait the end of transfer */
		 /****************************/

				do {
					dw_Status =
						readl((void *)(devpriv->
							dw_AiBase + 96));
				}
				while ((dw_Status & 0x100) != 0x100);
			} else {
		 /***************************/
				/* Channel not initialised */
		 /***************************/

				i_ReturnValue = -4;
				printk("Channel %d range %d selection error\n",
					b_Channel, b_Range);
			}
		} else {
	      /***************************/
			/* Channel selection error */
	      /***************************/

			i_ReturnValue = -3;
			printk("Channel %d selection error\n", b_Channel);
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
|                              TTL FUNCTIONS                                 |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : int   i_APCI3XXX_InsnConfigInitTTLIO                   |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task           You must calling this function be                           |
|                for you call any other function witch access of TTL.        |
|                APCI3XXX_TTL_INIT_DIRECTION_PORT2(user inputs for direction)|
+----------------------------------------------------------------------------+
| Input Parameters  : b_InitType    = (unsigned char) data[0];                        |
|                     b_Port2Mode   = (unsigned char) data[1];                        |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      :>0: No error                                            |
|                    -1: Port 2 mode selection is wrong                      |
|                    ....                                                    |
|                    -100 : Config command error                             |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnConfigInitTTLIO(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned char b_Command = 0;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
	   /*******************/
		/* Get the command */
		/* **************** */

		b_Command = (unsigned char) data[0];

	   /********************/
		/* Test the command */
	   /********************/

		if (b_Command == APCI3XXX_TTL_INIT_DIRECTION_PORT2) {
	      /***************************************/
			/* Test the initialisation buffer size */
	      /***************************************/

			if ((b_Command == APCI3XXX_TTL_INIT_DIRECTION_PORT2)
				&& (insn->n != 2)) {
		 /*******************/
				/* Data size error */
		 /*******************/

				printk("Buffer size error\n");
				i_ReturnValue = -101;
			}
		} else {
	      /************************/
			/* Config command error */
	      /************************/

			printk("Command selection error\n");
			i_ReturnValue = -100;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	/*********************************************************************************/
	/* Test if no error occur and APCI3XXX_TTL_INIT_DIRECTION_PORT2 command selected */
	/*********************************************************************************/

	if ((i_ReturnValue >= 0)
		&& (b_Command == APCI3XXX_TTL_INIT_DIRECTION_PORT2)) {
	   /**********************/
		/* Test the direction */
	   /**********************/

		if ((data[1] == 0) || (data[1] == 0xFF)) {
	      /**************************/
			/* Save the configuration */
	      /**************************/

			devpriv->ul_TTLPortConfiguration[0] =
				devpriv->ul_TTLPortConfiguration[0] | data[1];
		} else {
	      /************************/
			/* Port direction error */
	      /************************/

			printk("Port 2 direction selection error\n");
			i_ReturnValue = -1;
		}
	}

	/**************************/
	/* Test if no error occur */
	/**************************/

	if (i_ReturnValue >= 0) {
	   /***********************************/
		/* Test if TTL port initilaisation */
	   /***********************************/

		if (b_Command == APCI3XXX_TTL_INIT_DIRECTION_PORT2) {
	      /*************************/
			/* Set the configuration */
	      /*************************/

			outl(data[1], devpriv->iobase + 224);
		}
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
|                        TTL INPUT FUNCTIONS                                 |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : int     i_APCI3XXX_InsnBitsTTLIO                       |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task              : Write the selected output mask and read the status from|
|                     all TTL channles                                       |
+----------------------------------------------------------------------------+
| Input Parameters  : dw_ChannelMask = data [0];                             |
|                     dw_BitMask     = data [1];                             |
+----------------------------------------------------------------------------+
| Output Parameters : data[1] : All TTL channles states                      |
+----------------------------------------------------------------------------+
| Return Value      : >0  : No error                                         |
|                    -4   : Channel mask error                               |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnBitsTTLIO(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned char b_ChannelCpt = 0;
	unsigned int dw_ChannelMask = 0;
	unsigned int dw_BitMask = 0;
	unsigned int dw_Status = 0;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 2) {
	   /*******************************/
		/* Get the channe and bit mask */
	   /*******************************/

		dw_ChannelMask = data[0];
		dw_BitMask = data[1];

	   /*************************/
		/* Test the channel mask */
	   /*************************/

		if (((dw_ChannelMask & 0XFF00FF00) == 0) &&
			(((devpriv->ul_TTLPortConfiguration[0] & 0xFF) == 0xFF)
				|| (((devpriv->ul_TTLPortConfiguration[0] &
							0xFF) == 0)
					&& ((dw_ChannelMask & 0XFF0000) ==
						0)))) {
	      /*********************************/
			/* Test if set/reset any channel */
	      /*********************************/

			if (dw_ChannelMask) {
		 /****************************************/
				/* Test if set/rest any port 0 channels */
		 /****************************************/

				if (dw_ChannelMask & 0xFF) {
		    /*******************************************/
					/* Read port 0 (first digital output port) */
		    /*******************************************/

					dw_Status = inl(devpriv->iobase + 80);

					for (b_ChannelCpt = 0; b_ChannelCpt < 8;
						b_ChannelCpt++) {
						if ((dw_ChannelMask >>
								b_ChannelCpt) &
							1) {
							dw_Status =
								(dw_Status &
								(0xFF - (1 << b_ChannelCpt))) | (dw_BitMask & (1 << b_ChannelCpt));
						}
					}

					outl(dw_Status, devpriv->iobase + 80);
				}

		 /****************************************/
				/* Test if set/rest any port 2 channels */
		 /****************************************/

				if (dw_ChannelMask & 0xFF0000) {
					dw_BitMask = dw_BitMask >> 16;
					dw_ChannelMask = dw_ChannelMask >> 16;

		    /********************************************/
					/* Read port 2 (second digital output port) */
		    /********************************************/

					dw_Status = inl(devpriv->iobase + 112);

					for (b_ChannelCpt = 0; b_ChannelCpt < 8;
						b_ChannelCpt++) {
						if ((dw_ChannelMask >>
								b_ChannelCpt) &
							1) {
							dw_Status =
								(dw_Status &
								(0xFF - (1 << b_ChannelCpt))) | (dw_BitMask & (1 << b_ChannelCpt));
						}
					}

					outl(dw_Status, devpriv->iobase + 112);
				}
			}

	      /*******************************************/
			/* Read port 0 (first digital output port) */
	      /*******************************************/

			data[1] = inl(devpriv->iobase + 80);

	      /******************************************/
			/* Read port 1 (first digital input port) */
	      /******************************************/

			data[1] = data[1] | (inl(devpriv->iobase + 64) << 8);

	      /************************/
			/* Test if port 2 input */
	      /************************/

			if ((devpriv->ul_TTLPortConfiguration[0] & 0xFF) == 0) {
				data[1] =
					data[1] | (inl(devpriv->iobase +
						96) << 16);
			} else {
				data[1] =
					data[1] | (inl(devpriv->iobase +
						112) << 16);
			}
		} else {
	      /************************/
			/* Config command error */
	      /************************/

			printk("Channel mask error\n");
			i_ReturnValue = -4;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function Name     : int i_APCI3XXX_InsnReadTTLIO                           |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task              : Read the status from selected channel                  |
+----------------------------------------------------------------------------+
| Input Parameters  : b_Channel = CR_CHAN(insn->chanspec)                    |
+----------------------------------------------------------------------------+
| Output Parameters : data[0] : Selected TTL channel state                   |
+----------------------------------------------------------------------------+
| Return Value      : 0   : No error                                         |
|                    -3   : Channel selection error                          |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnReadTTLIO(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	unsigned char b_Channel = (unsigned char) CR_CHAN(insn->chanspec);
	int i_ReturnValue = insn->n;
	unsigned int *pls_ReadData = data;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
	   /***********************/
		/* Test if read port 0 */
	   /***********************/

		if (b_Channel < 8) {
	      /*******************************************/
			/* Read port 0 (first digital output port) */
	      /*******************************************/

			pls_ReadData[0] = inl(devpriv->iobase + 80);
			pls_ReadData[0] = (pls_ReadData[0] >> b_Channel) & 1;
		} else {
	      /***********************/
			/* Test if read port 1 */
	      /***********************/

			if ((b_Channel > 7) && (b_Channel < 16)) {
		 /******************************************/
				/* Read port 1 (first digital input port) */
		 /******************************************/

				pls_ReadData[0] = inl(devpriv->iobase + 64);
				pls_ReadData[0] =
					(pls_ReadData[0] >> (b_Channel -
						8)) & 1;
			} else {
		 /***********************/
				/* Test if read port 2 */
		 /***********************/

				if ((b_Channel > 15) && (b_Channel < 24)) {
		    /************************/
					/* Test if port 2 input */
		    /************************/

					if ((devpriv->ul_TTLPortConfiguration[0]
							& 0xFF) == 0) {
						pls_ReadData[0] =
							inl(devpriv->iobase +
							96);
						pls_ReadData[0] =
							(pls_ReadData[0] >>
							(b_Channel - 16)) & 1;
					} else {
						pls_ReadData[0] =
							inl(devpriv->iobase +
							112);
						pls_ReadData[0] =
							(pls_ReadData[0] >>
							(b_Channel - 16)) & 1;
					}
				} else {
		    /***************************/
					/* Channel selection error */
		    /***************************/

					i_ReturnValue = -3;
					printk("Channel %d selection error\n",
						b_Channel);
				}
			}
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
|                        TTL OUTPUT FUNCTIONS                                |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : int     i_APCI3XXX_InsnWriteTTLIO                      |
|                          (struct comedi_device    *dev,                           |
|                           struct comedi_subdevice *s,                             |
|                           struct comedi_insn      *insn,                          |
|                           unsigned int         *data)                          |
+----------------------------------------------------------------------------+
| Task              : Set the state from TTL output channel                  |
+----------------------------------------------------------------------------+
| Input Parameters  : b_Channel = CR_CHAN(insn->chanspec)                    |
|                     b_State   = data [0]                                   |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0   : No error                                         |
|                    -3   : Channel selection error                          |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnWriteTTLIO(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned char b_Channel = (unsigned char) CR_CHAN(insn->chanspec);
	unsigned char b_State = 0;
	unsigned int dw_Status = 0;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
		b_State = (unsigned char) data[0];

	   /***********************/
		/* Test if read port 0 */
	   /***********************/

		if (b_Channel < 8) {
	      /*****************************************************************************/
			/* Read port 0 (first digital output port) and set/reset the selcted channel */
	      /*****************************************************************************/

			dw_Status = inl(devpriv->iobase + 80);
			dw_Status =
				(dw_Status & (0xFF -
					(1 << b_Channel))) | ((b_State & 1) <<
				b_Channel);
			outl(dw_Status, devpriv->iobase + 80);
		} else {
	      /***********************/
			/* Test if read port 2 */
	      /***********************/

			if ((b_Channel > 15) && (b_Channel < 24)) {
		 /*************************/
				/* Test if port 2 output */
		 /*************************/

				if ((devpriv->ul_TTLPortConfiguration[0] & 0xFF)
					== 0xFF) {
		    /*****************************************************************************/
					/* Read port 2 (first digital output port) and set/reset the selcted channel */
		    /*****************************************************************************/

					dw_Status = inl(devpriv->iobase + 112);
					dw_Status =
						(dw_Status & (0xFF -
							(1 << (b_Channel -
									16)))) |
						((b_State & 1) << (b_Channel -
							16));
					outl(dw_Status, devpriv->iobase + 112);
				} else {
		    /***************************/
					/* Channel selection error */
		    /***************************/

					i_ReturnValue = -3;
					printk("Channel %d selection error\n",
						b_Channel);
				}
			} else {
		 /***************************/
				/* Channel selection error */
		 /***************************/

				i_ReturnValue = -3;
				printk("Channel %d selection error\n",
					b_Channel);
			}
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
|                           DIGITAL INPUT SUBDEVICE                          |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3XXX_InsnReadDigitalInput                     |
|                                          (struct comedi_device *dev,              |
|                                           struct comedi_subdevice *s,             |
|                                           struct comedi_insn *insn,               |
|                                           unsigned int *data)                  |
+----------------------------------------------------------------------------+
| Task              : Reads the value of the specified Digital input channel |
+----------------------------------------------------------------------------+
| Input Parameters  : b_Channel = CR_CHAN(insn->chanspec) (0 to 3)           |
+----------------------------------------------------------------------------+
| Output Parameters : data[0] : Channel value                                |
+----------------------------------------------------------------------------+
| Return Value      : 0   : No error                                         |
|                    -3   : Channel selection error                          |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnReadDigitalInput(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned char b_Channel = (unsigned char) CR_CHAN(insn->chanspec);
	unsigned int dw_Temp = 0;

	/***************************/
	/* Test the channel number */
	/***************************/

	if (b_Channel <= devpriv->ps_BoardInfo->i_NbrDiChannel) {
	   /************************/
		/* Test the buffer size */
	   /************************/

		if (insn->n >= 1) {
			dw_Temp = inl(devpriv->iobase + 32);
			*data = (dw_Temp >> b_Channel) & 1;
		} else {
	      /*******************/
			/* Data size error */
	      /*******************/

			printk("Buffer size error\n");
			i_ReturnValue = -101;
		}
	} else {
	   /***************************/
		/* Channel selection error */
	   /***************************/

		printk("Channel selection error\n");
		i_ReturnValue = -3;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3XXX_InsnBitsDigitalInput                     |
|                                          (struct comedi_device *dev,              |
|                                           struct comedi_subdevice *s,             |
|                                           struct comedi_insn *insn,               |
|                                           unsigned int *data)                  |
+----------------------------------------------------------------------------+
| Task              : Reads the value of the Digital input Port i.e.4channels|
+----------------------------------------------------------------------------+
| Input Parameters  : -                                                      |
+----------------------------------------------------------------------------+
| Output Parameters : data[0] : Port value                                   |
+----------------------------------------------------------------------------+
| Return Value      :>0: No error                                            |
|                    ....                                                    |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/
int i_APCI3XXX_InsnBitsDigitalInput(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned int dw_Temp = 0;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
		dw_Temp = inl(devpriv->iobase + 32);
		*data = dw_Temp & 0xf;
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
|                           DIGITAL OUTPUT SUBDEVICE                         |
+----------------------------------------------------------------------------+

*/

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3XXX_InsnBitsDigitalOutput                    |
|                                          (struct comedi_device *dev,              |
|                                           struct comedi_subdevice *s,             |
|                                           struct comedi_insn *insn,               |
|                                           unsigned int *data)                  |
+----------------------------------------------------------------------------+
| Task              : Write the selected output mask and read the status from|
|                     all digital output channles                            |
+----------------------------------------------------------------------------+
| Input Parameters  : dw_ChannelMask = data [0];                             |
|                     dw_BitMask     = data [1];                             |
+----------------------------------------------------------------------------+
| Output Parameters : data[1] : All digital output channles states           |
+----------------------------------------------------------------------------+
| Return Value      : >0  : No error                                         |
|                    -4   : Channel mask error                               |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/
int i_APCI3XXX_InsnBitsDigitalOutput(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned char b_ChannelCpt = 0;
	unsigned int dw_ChannelMask = 0;
	unsigned int dw_BitMask = 0;
	unsigned int dw_Status = 0;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 2) {
	   /*******************************/
		/* Get the channe and bit mask */
	   /*******************************/

		dw_ChannelMask = data[0];
		dw_BitMask = data[1];

	   /*************************/
		/* Test the channel mask */
	   /*************************/

		if ((dw_ChannelMask & 0XFFFFFFF0) == 0) {
	      /*********************************/
			/* Test if set/reset any channel */
	      /*********************************/

			if (dw_ChannelMask & 0xF) {
		 /********************************/
				/* Read the digital output port */
		 /********************************/

				dw_Status = inl(devpriv->iobase + 48);

				for (b_ChannelCpt = 0; b_ChannelCpt < 4;
					b_ChannelCpt++) {
					if ((dw_ChannelMask >> b_ChannelCpt) &
						1) {
						dw_Status =
							(dw_Status & (0xF -
								(1 << b_ChannelCpt))) | (dw_BitMask & (1 << b_ChannelCpt));
					}
				}

				outl(dw_Status, devpriv->iobase + 48);
			}

	      /********************************/
			/* Read the digital output port */
	      /********************************/

			data[1] = inl(devpriv->iobase + 48);
		} else {
	      /************************/
			/* Config command error */
	      /************************/

			printk("Channel mask error\n");
			i_ReturnValue = -4;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3XXX_InsnWriteDigitalOutput                   |
|                                          (struct comedi_device *dev,              |
|                                           struct comedi_subdevice *s,             |
|                                           struct comedi_insn *insn,               |
|                                           unsigned int *data)                  |
+----------------------------------------------------------------------------+
| Task              : Set the state from digital output channel              |
+----------------------------------------------------------------------------+
| Input Parameters  : b_Channel = CR_CHAN(insn->chanspec)                    |
|                     b_State   = data [0]                                   |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : >0  : No error                                         |
|                    -3   : Channel selection error                          |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnWriteDigitalOutput(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned char b_Channel = CR_CHAN(insn->chanspec);
	unsigned char b_State = 0;
	unsigned int dw_Status = 0;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
	   /***************************/
		/* Test the channel number */
	   /***************************/

		if (b_Channel < devpriv->ps_BoardInfo->i_NbrDoChannel) {
	      /*******************/
			/* Get the command */
	      /*******************/

			b_State = (unsigned char) data[0];

	      /********************************/
			/* Read the digital output port */
	      /********************************/

			dw_Status = inl(devpriv->iobase + 48);

			dw_Status =
				(dw_Status & (0xF -
					(1 << b_Channel))) | ((b_State & 1) <<
				b_Channel);
			outl(dw_Status, devpriv->iobase + 48);
		} else {
	      /***************************/
			/* Channel selection error */
	      /***************************/

			printk("Channel selection error\n");
			i_ReturnValue = -3;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function name     :int i_APCI3XXX_InsnReadDigitalOutput                    |
|                                          (struct comedi_device *dev,              |
|                                           struct comedi_subdevice *s,             |
|                                           struct comedi_insn *insn,               |
|                                           unsigned int *data)                  |
+----------------------------------------------------------------------------+
| Task              : Read the state from digital output channel             |
+----------------------------------------------------------------------------+
| Input Parameters  : b_Channel = CR_CHAN(insn->chanspec)                    |
+----------------------------------------------------------------------------+
| Output Parameters : b_State   = data [0]                                   |
+----------------------------------------------------------------------------+
| Return Value      : >0  : No error                                         |
|                    -3   : Channel selection error                          |
|                    -101 : Data size error                                  |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_InsnReadDigitalOutput(struct comedi_device *dev,
	struct comedi_subdevice *s, struct comedi_insn *insn, unsigned int *data)
{
	int i_ReturnValue = insn->n;
	unsigned char b_Channel = CR_CHAN(insn->chanspec);
	unsigned int dw_Status = 0;

	/************************/
	/* Test the buffer size */
	/************************/

	if (insn->n >= 1) {
	   /***************************/
		/* Test the channel number */
	   /***************************/

		if (b_Channel < devpriv->ps_BoardInfo->i_NbrDoChannel) {
	      /********************************/
			/* Read the digital output port */
	      /********************************/

			dw_Status = inl(devpriv->iobase + 48);

			dw_Status = (dw_Status >> b_Channel) & 1;
			*data = dw_Status;
		} else {
	      /***************************/
			/* Channel selection error */
	      /***************************/

			printk("Channel selection error\n");
			i_ReturnValue = -3;
		}
	} else {
	   /*******************/
		/* Data size error */
	   /*******************/

		printk("Buffer size error\n");
		i_ReturnValue = -101;
	}

	return i_ReturnValue;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI3XXX_Reset(struct comedi_device *dev)               |                                                         +----------------------------------------------------------------------------+
| Task              :resets all the registers                                |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev                                     |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : -                                                      |
+----------------------------------------------------------------------------+
*/

int i_APCI3XXX_Reset(struct comedi_device *dev)
{
	unsigned char b_Cpt = 0;

	/*************************/
	/* Disable the interrupt */
	/*************************/

	disable_irq(dev->irq);

	/****************************/
	/* Reset the interrupt flag */
	/****************************/

	devpriv->b_EocEosInterrupt = 0;

	/***************************/
	/* Clear the start command */
	/***************************/

	writel(0, (void *)(devpriv->dw_AiBase + 8));

	/*****************************/
	/* Reset the interrupt flags */
	/*****************************/

	writel(readl((void *)(devpriv->dw_AiBase + 16)),
		(void *)(devpriv->dw_AiBase + 16));

	/*****************/
	/* clear the EOS */
	/*****************/

	readl((void *)(devpriv->dw_AiBase + 20));

	/******************/
	/* Clear the FIFO */
	/******************/

	for (b_Cpt = 0; b_Cpt < 16; b_Cpt++) {
		readl((void *)(devpriv->dw_AiBase + 28));
	}

	/************************/
	/* Enable the interrupt */
	/************************/

	enable_irq(dev->irq);

	return 0;
}
