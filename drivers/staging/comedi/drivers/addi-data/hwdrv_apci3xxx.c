#define APCI3XXX_SINGLE				0
#define APCI3XXX_DIFF				1
#define APCI3XXX_CONFIGURATION			0

#define APCI3XXX_TTL_INIT_DIRECTION_PORT2	0

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
static int i_APCI3XXX_TestConversionStarted(struct comedi_device *dev)
{
	struct apci3xxx_private *devpriv = dev->private;

	if ((readl(devpriv->dw_AiBase + 8) & 0x80000UL) == 0x80000UL)
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
static int i_APCI3XXX_AnalogInputConfigOperatingMode(struct comedi_device *dev,
						     struct comedi_subdevice *s,
						     struct comedi_insn *insn,
						     unsigned int *data)
{
	const struct apci3xxx_boardinfo *this_board = comedi_board(dev);
	struct apci3xxx_private *devpriv = dev->private;
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

		if ((this_board->b_AvailableConvertUnit & (1 << b_TimeBase)) !=
			0) {
	      /*******************************/
			/* Test the convert time value */
	      /*******************************/

			if (dw_ReloadValue <= 65535) {
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
				    this_board->ui_MinAcquisitiontimeNs) {
					if ((b_SingleDiff == APCI3XXX_SINGLE)
						|| (b_SingleDiff ==
							APCI3XXX_DIFF)) {
						if (((b_SingleDiff == APCI3XXX_SINGLE)
						        && (this_board->i_NbrAiChannel == 0))
						    || ((b_SingleDiff == APCI3XXX_DIFF)
							&& (this_board->i_NbrAiChannelDiff == 0))
						    ) {
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

								writel((unsigned int)b_TimeBase,
									devpriv->dw_AiBase + 36);

			      /**************************/
								/* Set the convert timing */
			      /*************************/

								writel(dw_ReloadValue, devpriv->dw_AiBase + 32);
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
static int i_APCI3XXX_InsnConfigAnalogInput(struct comedi_device *dev,
					    struct comedi_subdevice *s,
					    struct comedi_insn *insn,
					    unsigned int *data)
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
static int i_APCI3XXX_InsnReadAnalogInput(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	const struct apci3xxx_boardinfo *this_board = comedi_board(dev);
	struct apci3xxx_private *devpriv = dev->private;
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

		if (((b_Channel < this_board->i_NbrAiChannel)
				&& (devpriv->b_SingelDiff == APCI3XXX_SINGLE))
			|| ((b_Channel < this_board->i_NbrAiChannelDiff)
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

					writel(0x10000UL, devpriv->dw_AiBase + 12);

		    /*******************************/
					/* Get and save the delay mode */
		    /*******************************/

					dw_Temp = readl(devpriv->dw_AiBase + 4);
					dw_Temp = dw_Temp & 0xFFFFFEF0UL;

		    /***********************************/
					/* Channel configuration selection */
		    /***********************************/

					writel(dw_Temp, devpriv->dw_AiBase + 4);

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
					       devpriv->dw_AiBase + 0);

		    /*********************/
					/* Channel selection */
		    /*********************/

					writel(dw_Temp | 0x100UL,
					       devpriv->dw_AiBase + 4);
					writel((unsigned int) b_Channel,
					       devpriv->dw_AiBase + 0);

		    /***********************/
					/* Restaure delay mode */
		    /***********************/

					writel(dw_Temp, devpriv->dw_AiBase + 4);

		    /***********************************/
					/* Set the number of sequence to 1 */
		    /***********************************/

					writel(1, devpriv->dw_AiBase + 48);

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

							writel(0x80000UL, devpriv->dw_AiBase + 8);

			  /****************/
							/* Wait the EOS */
			  /****************/

							do {
								dw_Temp = readl(devpriv->dw_AiBase + 20);
								dw_Temp = dw_Temp & 1;
							} while (dw_Temp != 1);

			  /*************************/
							/* Read the analog value */
			  /*************************/

							data[dw_AcquisitionCpt] = (unsigned int)readl(devpriv->dw_AiBase + 28);
						}
					} else {
		       /************************/
						/* Start the conversion */
		       /************************/

						writel(0x180000UL, devpriv->dw_AiBase + 8);
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
