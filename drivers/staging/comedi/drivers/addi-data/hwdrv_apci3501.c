/* Watchdog Related Defines */

#define APCI3501_WATCHDOG		0x20
#define APCI3501_TCW_SYNC_ENABLEDISABLE	0
#define APCI3501_TCW_RELOAD_VALUE	4
#define APCI3501_TCW_TIMEBASE		8
#define APCI3501_TCW_PROG		12
#define APCI3501_TCW_TRIG_STATUS	16
#define APCI3501_TCW_IRQ		20
#define APCI3501_TCW_WARN_TIMEVAL	24
#define APCI3501_TCW_WARN_TIMEBASE	28
#define ADDIDATA_TIMER			0
#define ADDIDATA_WATCHDOG		2

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI3501_ConfigTimerCounterWatchdog              |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Configures The Timer , Counter or Watchdog             |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|					  data[0]            : 0 Configure As Timer      |
|										   1 Configure As Counter    |
|										   2 Configure As Watchdog   |
|					  data[1]            : 1 Enable  Interrupt       |
|										   0 Disable Interrupt 	     |
|					  data[2]            : Time Unit                 |
|					  data[3]			 : Reload Value			     |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/
static int i_APCI3501_ConfigTimerCounterWatchdog(struct comedi_device *dev,
						 struct comedi_subdevice *s,
						 struct comedi_insn *insn,
						 unsigned int *data)
{
	struct apci3501_private *devpriv = dev->private;
	unsigned int ul_Command1 = 0;

	devpriv->tsk_Current = current;
	if (data[0] == ADDIDATA_WATCHDOG) {

		devpriv->b_TimerSelectMode = ADDIDATA_WATCHDOG;
		/* Disable the watchdog */
		outl(0x0, dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG);	/* disable Wa */

		if (data[1] == 1) {
			/* Enable TIMER int & DISABLE ALL THE OTHER int SOURCES */
			outl(0x02,
				dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
		} else {
			outl(0x0, dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG);	/* disable Timer interrupt */
		}

		/* Loading the Timebase value */
		outl(data[2],
			dev->iobase + APCI3501_WATCHDOG +
			APCI3501_TCW_TIMEBASE);

		/* Loading the Reload value */
		outl(data[3],
			dev->iobase + APCI3501_WATCHDOG +
			APCI3501_TCW_RELOAD_VALUE);
		/* Set the mode */
		ul_Command1 = inl(dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG) | 0xFFF819E0UL;	/* e2->e0 */
		outl(ul_Command1,
			dev->iobase + APCI3501_WATCHDOG +
			APCI3501_TCW_PROG);
	}			/* end if(data[0]==ADDIDATA_WATCHDOG) */

	else if (data[0] == ADDIDATA_TIMER) {
		/* First Stop The Timer */
		ul_Command1 =
			inl(dev->iobase + APCI3501_WATCHDOG +
			APCI3501_TCW_PROG);
		ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
		outl(ul_Command1, dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG);	/* Stop The Timer */
		devpriv->b_TimerSelectMode = ADDIDATA_TIMER;
		if (data[1] == 1) {
			/* Enable TIMER int & DISABLE ALL THE OTHER int SOURCES */
			outl(0x02,
				dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
		} else {
			outl(0x0, dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG);	/* disable Timer interrupt */
		}

		/*  Loading Timebase */
		outl(data[2],
			dev->iobase + APCI3501_WATCHDOG +
			APCI3501_TCW_TIMEBASE);

		/* Loading the Reload value */
		outl(data[3],
			dev->iobase + APCI3501_WATCHDOG +
			APCI3501_TCW_RELOAD_VALUE);

		/*  printk ("\nTimer Address :: %x\n", (dev->iobase + APCI3501_WATCHDOG)); */
		ul_Command1 =
			inl(dev->iobase + APCI3501_WATCHDOG +
			APCI3501_TCW_PROG);
		ul_Command1 =
			(ul_Command1 & 0xFFF719E2UL) | 2UL << 13UL | 0x10UL;
		outl(ul_Command1, dev->iobase + APCI3501_WATCHDOG + APCI3501_TCW_PROG);	/* mode 2 */

	}			/* end if(data[0]==ADDIDATA_TIMER) */

	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI3501_StartStopWriteTimerCounterWatchdog      |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Start / Stop The Selected Timer , Counter or Watchdog  |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|					  data[0]            : 0 Timer                   |
|										   1 Counter                 |
|										   2 Watchdog          		 |                             |            				 data[1]            : 1 Start                   |
|										   0 Stop      				 |									                                              2 Trigger                 |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

static int i_APCI3501_StartStopWriteTimerCounterWatchdog(struct comedi_device *dev,
							 struct comedi_subdevice *s,
							 struct comedi_insn *insn,
							 unsigned int *data)
{
	struct apci3501_private *devpriv = dev->private;
	unsigned int ul_Command1 = 0;
	int i_Temp;

	if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {

		if (data[1] == 1) {
			ul_Command1 =
				inl(dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;
			/* Enable the Watchdog */
			outl(ul_Command1,
				dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
		}

		else if (data[1] == 0)	/* Stop The Watchdog */
		{
			/* Stop The Watchdog */
			ul_Command1 =
				inl(dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
			ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
			outl(0x0,
				dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
		} else if (data[1] == 2) {
			ul_Command1 =
				inl(dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x200UL;
			outl(ul_Command1,
				dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
		}		/* if(data[1]==2) */
	}			/*  end if (devpriv->b_TimerSelectMode==ADDIDATA_WATCHDOG) */

	if (devpriv->b_TimerSelectMode == ADDIDATA_TIMER) {
		if (data[1] == 1) {

			ul_Command1 =
				inl(dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;
			/* Enable the Timer */
			outl(ul_Command1,
				dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
		} else if (data[1] == 0) {
			/* Stop The Timer */
			ul_Command1 =
				inl(dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
			ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
			outl(ul_Command1,
				dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
		}

		else if (data[1] == 2) {
			/* Trigger the Timer */
			ul_Command1 =
				inl(dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x200UL;
			outl(ul_Command1,
				dev->iobase + APCI3501_WATCHDOG +
				APCI3501_TCW_PROG);
		}

	}			/*  end if (devpriv->b_TimerSelectMode==ADDIDATA_TIMER) */
	i_Temp = inl(dev->iobase + APCI3501_WATCHDOG +
		APCI3501_TCW_TRIG_STATUS) & 0x1;
	return insn->n;
}

/*
+----------------------------------------------------------------------------+
| Function   Name   : int i_APCI3501_ReadTimerCounterWatchdog                |
|			  (struct comedi_device *dev,struct comedi_subdevice *s,               |
|                      struct comedi_insn *insn,unsigned int *data)                     |
+----------------------------------------------------------------------------+
| Task              : Read The Selected Timer , Counter or Watchdog          |
+----------------------------------------------------------------------------+
| Input Parameters  : struct comedi_device *dev : Driver handle                     |
|                     unsigned int *data         : Data Pointer contains             |
|                                          configuration parameters as below |
|                                                                            |
|					  data[0]            : 0 Timer                   |
|										   1 Counter                 |
|										   2 Watchdog                |                             |					  data[1]             : Timer Counter Watchdog Number   |
+----------------------------------------------------------------------------+
| Output Parameters :	--													 |
+----------------------------------------------------------------------------+
| Return Value      : TRUE  : No error occur                                 |
|		            : FALSE : Error occur. Return the error          |
|			                                                         |
+----------------------------------------------------------------------------+
*/

static int i_APCI3501_ReadTimerCounterWatchdog(struct comedi_device *dev,
					       struct comedi_subdevice *s,
					       struct comedi_insn *insn,
					       unsigned int *data)
{
	struct apci3501_private *devpriv = dev->private;

	if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {
		data[0] =
			inl(dev->iobase + APCI3501_WATCHDOG +
			APCI3501_TCW_TRIG_STATUS) & 0x1;
		data[1] = inl(dev->iobase + APCI3501_WATCHDOG);
	}			/*  end if  (devpriv->b_TimerSelectMode==ADDIDATA_WATCHDOG) */

	else if (devpriv->b_TimerSelectMode == ADDIDATA_TIMER) {
		data[0] =
			inl(dev->iobase + APCI3501_WATCHDOG +
			APCI3501_TCW_TRIG_STATUS) & 0x1;
		data[1] = inl(dev->iobase + APCI3501_WATCHDOG);
	}			/*  end if  (devpriv->b_TimerSelectMode==ADDIDATA_TIMER) */

	else if ((devpriv->b_TimerSelectMode != ADDIDATA_TIMER)
		&& (devpriv->b_TimerSelectMode != ADDIDATA_WATCHDOG)) {
		printk("\nIn ReadTimerCounterWatchdog :: Invalid Subdevice \n");
	}
	return insn->n;
}
