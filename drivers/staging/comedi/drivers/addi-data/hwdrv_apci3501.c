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
	unsigned int ctrl;

	if (data[0] != ADDIDATA_WATCHDOG &&
	    data[0] != ADDIDATA_TIMER)
		return -EINVAL;

	devpriv->tsk_Current = current;

	devpriv->timer_mode = data[0];

	/* first, disable the watchdog or stop the timer */
	if (devpriv->timer_mode == ADDIDATA_WATCHDOG) {
		ctrl = 0;
	} else {
		ctrl = inl(devpriv->tcw + ADDI_TCW_CTRL_REG);
		ctrl &= ~(ADDI_TCW_CTRL_GATE | ADDI_TCW_CTRL_TRIG |
			  ADDI_TCW_CTRL_ENA);
	}
	outl(ctrl, devpriv->tcw + ADDI_TCW_CTRL_REG);

	/* enable/disable the timer interrupt */
	ctrl = (data[1] == 1) ? ADDI_TCW_CTRL_IRQ_ENA : 0;
	outl(ctrl, devpriv->tcw + ADDI_TCW_CTRL_REG);

	outl(data[2], devpriv->tcw + ADDI_TCW_TIMEBASE_REG);
	outl(data[3], devpriv->tcw + ADDI_TCW_RELOAD_REG);

	ctrl = inl(devpriv->tcw + ADDI_TCW_CTRL_REG);
	if (devpriv->timer_mode == ADDIDATA_WATCHDOG) {
		/* Set the mode (e2->e0) NOTE: this doesn't look correct */
		ctrl |= ~(ADDI_TCW_CTRL_CNT_UP | ADDI_TCW_CTRL_EXT_CLK_MASK |
			  ADDI_TCW_CTRL_MODE_MASK | ADDI_TCW_CTRL_GATE |
			  ADDI_TCW_CTRL_TRIG | ADDI_TCW_CTRL_TIMER_ENA |
			  ADDI_TCW_CTRL_RESET_ENA | ADDI_TCW_CTRL_WARN_ENA |
			  ADDI_TCW_CTRL_IRQ_ENA | ADDI_TCW_CTRL_ENA);
	} else {
		/* mode 2 */
		ctrl &= ~(ADDI_TCW_CTRL_CNTR_ENA | ADDI_TCW_CTRL_MODE_MASK |
			  ADDI_TCW_CTRL_GATE | ADDI_TCW_CTRL_TRIG |
			  ADDI_TCW_CTRL_TIMER_ENA | ADDI_TCW_CTRL_RESET_ENA |
			  ADDI_TCW_CTRL_WARN_ENA | ADDI_TCW_CTRL_ENA);
		ctrl |= ADDI_TCW_CTRL_MODE(2) | ADDI_TCW_CTRL_TIMER_ENA;
	}
	outl(ctrl, devpriv->tcw + ADDI_TCW_CTRL_REG);

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
		ctrl = inl(devpriv->tcw + ADDI_TCW_CTRL_REG);
		ctrl &= ~(ADDI_TCW_CTRL_GATE | ADDI_TCW_CTRL_TRIG);

		if (data[1] == 1) {		/* enable */
			ctrl |= ADDI_TCW_CTRL_ENA;
		} else if (data[1] == 0) {	/* stop */
			if (devpriv->timer_mode == ADDIDATA_WATCHDOG)
				ctrl = 0;
			else
				ctrl &= ~ADDI_TCW_CTRL_ENA;
		} else if (data[1] == 2) {	/* trigger */
			ctrl |= ADDI_TCW_CTRL_TRIG;
		}
		outl(ctrl, devpriv->tcw + ADDI_TCW_CTRL_REG);
	}

	inl(devpriv->tcw + ADDI_TCW_STATUS_REG);
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

	data[0] = inl(devpriv->tcw + ADDI_TCW_STATUS_REG) &
		  ADDI_TCW_STATUS_OVERFLOW;
	data[1] = inl(devpriv->tcw + ADDI_TCW_VAL_REG);

	return insn->n;
}
