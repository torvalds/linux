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

		devpriv->timer_mode = ADDIDATA_WATCHDOG;
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
		devpriv->timer_mode = ADDIDATA_TIMER;
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
	unsigned int ctrl;

	if (devpriv->timer_mode == ADDIDATA_WATCHDOG ||
	    devpriv->timer_mode == ADDIDATA_TIMER) {
		ctrl = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
		ctrl &= 0xfffff9ff;

		if (data[1] == 1) {		/* enable */
			ctrl |= 0x1;
		} else if (data[1] == 0) {	/* stop */
			if (devpriv->timer_mode == ADDIDATA_WATCHDOG)
				ctrl = 0;
			else
				ctrl &= ~0x1;
		} else if (data[1] == 2) {	/* trigger */
			ctrl |= 0x200;
		}
		outl(ctrl, dev->iobase + APCI3501_TIMER_CTRL_REG);
	}

	inl(dev->iobase + APCI3501_TIMER_STATUS_REG);
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

	if (devpriv->timer_mode != ADDIDATA_TIMER &&
	    devpriv->timer_mode != ADDIDATA_WATCHDOG)
		return -EINVAL;

	data[0] = inl(dev->iobase + APCI3501_TIMER_STATUS_REG) & 0x1;
	data[1] = inl(dev->iobase + APCI3501_TIMER_SYNC_REG);

	return insn->n;
}
