#define APCI3XXX_SINGLE				0
#define APCI3XXX_DIFF				1
#define APCI3XXX_CONFIGURATION			0

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

static int apci3xxx_ai_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci3xxx_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned char use_interrupt = 0;	/* FIXME: use interrupts */
	unsigned int delay_mode;
	unsigned int val;
	int i;

	if (!use_interrupt && insn->n == 0)
		return insn->n;

	if (i_APCI3XXX_TestConversionStarted(dev))
		return -EBUSY;

	/* Clear the FIFO */
	writel(0x10000, devpriv->dw_AiBase + 12);

	/* Get and save the delay mode */
	delay_mode = readl(devpriv->dw_AiBase + 4);
	delay_mode &= 0xfffffef0;

	/* Channel configuration selection */
	writel(delay_mode, devpriv->dw_AiBase + 4);

	/* Make the configuration */
	val = (range & 3) | ((range >> 2) << 6) |
	      (devpriv->b_SingelDiff << 7);
	writel(val, devpriv->dw_AiBase + 0);

	/* Channel selection */
	writel(delay_mode | 0x100, devpriv->dw_AiBase + 4);
	writel(chan, devpriv->dw_AiBase + 0);

	/* Restore delay mode */
	writel(delay_mode, devpriv->dw_AiBase + 4);

	/* Set the number of sequence to 1 */
	writel(1, devpriv->dw_AiBase + 48);

	/* Save the interrupt flag */
	devpriv->b_EocEosInterrupt = use_interrupt;

	/* Save the number of channels */
	devpriv->ui_AiNbrofChannels = 1;

	/* Test if interrupt not used */
	if (!use_interrupt) {
		for (i = 0; i < insn->n; i++) {
			/* Start the conversion */
			writel(0x80000, devpriv->dw_AiBase + 8);

			/* Wait the EOS */
			do {
				val = readl(devpriv->dw_AiBase + 20);
				val &= 0x1;
			} while (!val);

			/* Read the analog value */
			data[i] = readl(devpriv->dw_AiBase + 28);
		}
	} else {
		/* Start the conversion */
		writel(0x180000, devpriv->dw_AiBase + 8);
	}

	return insn->n;
}
