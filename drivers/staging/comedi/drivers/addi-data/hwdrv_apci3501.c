/* Watchdog Related Defines */

#define ADDIDATA_TIMER			0
#define ADDIDATA_WATCHDOG		2

/*
 * (*insn_config) for the timer subdevice
 *
 * Configures The Timer, Counter or Watchdog
 * Data Pointer contains configuration parameters as below
 *	data[0] : 0 Configure As Timer
 *		  1 Configure As Counter
 *		  2 Configure As Watchdog
 *	data[1] : 1 Enable  Interrupt
 *		  0 Disable Interrupt
 *	data[2] : Time Unit
 *	data[3] : Reload Value
 */
static int apci3501_config_insn_timer(struct comedi_device *dev,
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
		outl(0x0, dev->iobase + APCI3501_TIMER_CTRL_REG);

		if (data[1] == 1) {
			/* Enable TIMER int & DISABLE ALL THE OTHER int SOURCES */
			outl(0x02, dev->iobase + APCI3501_TIMER_CTRL_REG);
		} else {
			/* disable Timer interrupt */
			outl(0x0, dev->iobase + APCI3501_TIMER_CTRL_REG);
		}

		outl(data[2], dev->iobase + APCI3501_TIMER_TIMEBASE_REG);
		outl(data[3], dev->iobase + APCI3501_TIMER_RELOAD_REG);

		/* Set the mode (e2->e0) */
		ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG) | 0xFFF819E0UL;
		outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);
	}

	else if (data[0] == ADDIDATA_TIMER) {
		/* First Stop The Timer */
		ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
		ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
		outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);
		devpriv->b_TimerSelectMode = ADDIDATA_TIMER;
		if (data[1] == 1) {
			/* Enable TIMER int & DISABLE ALL THE OTHER int SOURCES */
			outl(0x02, dev->iobase + APCI3501_TIMER_CTRL_REG);
		} else {
			/* disable Timer interrupt */
			outl(0x0, dev->iobase + APCI3501_TIMER_CTRL_REG);
		}

		outl(data[2], dev->iobase + APCI3501_TIMER_TIMEBASE_REG);
		outl(data[3], dev->iobase + APCI3501_TIMER_RELOAD_REG);

		/* mode 2 */
		ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
		ul_Command1 =
			(ul_Command1 & 0xFFF719E2UL) | 2UL << 13UL | 0x10UL;
		outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);
	}

	return insn->n;
}

/*
 * (*insn_write) for the timer subdevice
 *
 * Start / Stop The Selected Timer , Counter or Watchdog
 * Data Pointer contains configuration parameters as below
 *	data[0] : 0 Timer
 *		  1 Counter
 *		  2 Watchdog
 *	data[1] : 1 Start
 *		  0 Stop
 *		  2 Trigger
 */
static int apci3501_write_insn_timer(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct apci3501_private *devpriv = dev->private;
	unsigned int ul_Command1 = 0;
	int i_Temp;

	if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {

		if (data[1] == 1) {
			ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;
			/* Enable the Watchdog */
			outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);
		}

		else if (data[1] == 0)	/* Stop The Watchdog */
		{
			/* Stop The Watchdog */
			ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
			ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
			outl(0x0, dev->iobase + APCI3501_TIMER_CTRL_REG);
		} else if (data[1] == 2) {
			ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x200UL;
			outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);
		}
	}

	if (devpriv->b_TimerSelectMode == ADDIDATA_TIMER) {
		if (data[1] == 1) {

			ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x1UL;
			/* Enable the Timer */
			outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);
		} else if (data[1] == 0) {
			/* Stop The Timer */
			ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
			ul_Command1 = ul_Command1 & 0xFFFFF9FEUL;
			outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);
		}

		else if (data[1] == 2) {
			/* Trigger the Timer */
			ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
			ul_Command1 = (ul_Command1 & 0xFFFFF9FFUL) | 0x200UL;
			outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);
		}
	}

	i_Temp = inl(dev->iobase + APCI3501_TIMER_STATUS_REG) & 0x1;
	return insn->n;
}

/*
 * (*insn_read) for the timer subdevice
 *
 * Read The Selected Timer, Counter or Watchdog
 * Data Pointer contains configuration parameters as below
 *	data[0] : 0 Timer
 *		  1 Counter
 *		  2 Watchdog
 *	data[1] : Timer Counter Watchdog Number
 */
static int apci3501_read_insn_timer(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct apci3501_private *devpriv = dev->private;

	if (devpriv->b_TimerSelectMode == ADDIDATA_WATCHDOG) {
		data[0] = inl(dev->iobase + APCI3501_TIMER_STATUS_REG) & 0x1;
		data[1] = inl(dev->iobase + APCI3501_TIMER_SYNC_REG);
	}

	else if (devpriv->b_TimerSelectMode == ADDIDATA_TIMER) {
		data[0] = inl(dev->iobase + APCI3501_TIMER_STATUS_REG) & 0x1;
		data[1] = inl(dev->iobase + APCI3501_TIMER_SYNC_REG);
	}

	else if ((devpriv->b_TimerSelectMode != ADDIDATA_TIMER)
		&& (devpriv->b_TimerSelectMode != ADDIDATA_WATCHDOG)) {
		printk("\nIn ReadTimerCounterWatchdog :: Invalid Subdevice \n");
	}
	return insn->n;
}
