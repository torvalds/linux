static int apci1564_timer_insn_config(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int ctrl;

	devpriv->tsk_current = current;

	/* Stop the timer */
	ctrl = inl(devpriv->timer + ADDI_TCW_CTRL_REG);
	ctrl &= ~(ADDI_TCW_CTRL_GATE | ADDI_TCW_CTRL_TRIG |
		  ADDI_TCW_CTRL_ENA);
	outl(ctrl, devpriv->timer + ADDI_TCW_CTRL_REG);

	if (data[1] == 1) {
		/* Enable timer int & disable all the other int sources */
		outl(ADDI_TCW_CTRL_IRQ_ENA,
		     devpriv->timer + ADDI_TCW_CTRL_REG);
		outl(0x0, dev->iobase + APCI1564_DI_IRQ_REG);
		outl(0x0, dev->iobase + APCI1564_DO_IRQ_REG);
		outl(0x0, dev->iobase + APCI1564_WDOG_IRQ_REG);
		if (devpriv->counters) {
			unsigned long iobase;

			iobase = devpriv->counters + ADDI_TCW_IRQ_REG;
			outl(0x0, iobase + APCI1564_COUNTER(0));
			outl(0x0, iobase + APCI1564_COUNTER(1));
			outl(0x0, iobase + APCI1564_COUNTER(2));
		}
	} else {
		/* disable Timer interrupt */
		outl(0x0, devpriv->timer + ADDI_TCW_CTRL_REG);
	}

	/* Loading Timebase */
	outl(data[2], devpriv->timer + ADDI_TCW_TIMEBASE_REG);

	/* Loading the Reload value */
	outl(data[3], devpriv->timer + ADDI_TCW_RELOAD_REG);

	ctrl = inl(devpriv->timer + ADDI_TCW_CTRL_REG);
	ctrl &= ~(ADDI_TCW_CTRL_CNTR_ENA | ADDI_TCW_CTRL_MODE_MASK |
		  ADDI_TCW_CTRL_GATE | ADDI_TCW_CTRL_TRIG |
		  ADDI_TCW_CTRL_TIMER_ENA | ADDI_TCW_CTRL_RESET_ENA |
		  ADDI_TCW_CTRL_WARN_ENA | ADDI_TCW_CTRL_ENA);
	ctrl |= ADDI_TCW_CTRL_MODE(2) | ADDI_TCW_CTRL_TIMER_ENA;
	outl(ctrl, devpriv->timer + ADDI_TCW_CTRL_REG);

	return insn->n;
}

static int apci1564_timer_insn_write(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int ctrl;

	ctrl = inl(devpriv->timer + ADDI_TCW_CTRL_REG);
	ctrl &= ~(ADDI_TCW_CTRL_GATE | ADDI_TCW_CTRL_TRIG);
	switch (data[1]) {
	case 0:	/* Stop The Timer */
		ctrl &= ~ADDI_TCW_CTRL_ENA;
		break;
	case 1:	/* Enable the Timer */
		ctrl |= ADDI_TCW_CTRL_ENA;
		break;
	}
	outl(ctrl, devpriv->timer + ADDI_TCW_CTRL_REG);

	return insn->n;
}

static int apci1564_timer_insn_read(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;

	/* Stores the status of the Timer */
	data[0] = inl(devpriv->timer + ADDI_TCW_STATUS_REG) &
		  ADDI_TCW_STATUS_OVERFLOW;

	/* Stores the Actual value of the Timer */
	data[1] = inl(devpriv->timer + ADDI_TCW_VAL_REG);

	return insn->n;
}

static int apci1564_counter_insn_config(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn,
					unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned long iobase = devpriv->counters + APCI1564_COUNTER(chan);
	unsigned int ctrl;

	devpriv->tsk_current = current;

	/* Stop The Timer */
	ctrl = inl(iobase + ADDI_TCW_CTRL_REG);
	ctrl &= ~(ADDI_TCW_CTRL_GATE | ADDI_TCW_CTRL_TRIG |
		  ADDI_TCW_CTRL_ENA);
	outl(ctrl, iobase + ADDI_TCW_CTRL_REG);

	/* Set the reload value */
	outl(data[3], iobase + ADDI_TCW_RELOAD_REG);

	/* Set the mode */
	ctrl &= ~(ADDI_TCW_CTRL_EXT_CLK_MASK | ADDI_TCW_CTRL_MODE_MASK |
		  ADDI_TCW_CTRL_TIMER_ENA | ADDI_TCW_CTRL_RESET_ENA |
		  ADDI_TCW_CTRL_WARN_ENA);
	ctrl |= ADDI_TCW_CTRL_CNTR_ENA | ADDI_TCW_CTRL_MODE(data[4]);
	outl(ctrl, iobase + ADDI_TCW_CTRL_REG);

	/* Enable or Disable Interrupt */
	if (data[1])
		ctrl |= ADDI_TCW_CTRL_IRQ_ENA;
	else
		ctrl &= ~ADDI_TCW_CTRL_IRQ_ENA;
	outl(ctrl, iobase + ADDI_TCW_CTRL_REG);

	/* Set the Up/Down selection */
	if (data[6])
		ctrl |= ADDI_TCW_CTRL_CNT_UP;
	else
		ctrl &= ~ADDI_TCW_CTRL_CNT_UP;
	outl(ctrl, iobase + ADDI_TCW_CTRL_REG);

	return insn->n;
}

static int apci1564_counter_insn_write(struct comedi_device *dev,
				       struct comedi_subdevice *s,
				       struct comedi_insn *insn,
				       unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned long iobase = devpriv->counters + APCI1564_COUNTER(chan);
	unsigned int ctrl;

	ctrl = inl(iobase + ADDI_TCW_CTRL_REG);
	ctrl &= ~(ADDI_TCW_CTRL_GATE | ADDI_TCW_CTRL_TRIG);
	switch (data[1]) {
	case 0:	/* Stops the Counter subdevice */
		ctrl = 0;
		break;
	case 1:	/* Start the Counter subdevice */
		ctrl |= ADDI_TCW_CTRL_ENA;
		break;
	case 2:	/* Clears the Counter subdevice */
		ctrl |= ADDI_TCW_CTRL_GATE;
		break;
	}
	outl(ctrl, iobase + ADDI_TCW_CTRL_REG);

	return insn->n;
}

static int apci1564_counter_insn_read(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned long iobase = devpriv->counters + APCI1564_COUNTER(chan);
	unsigned int status;

	/* Read the Counter Actual Value. */
	data[0] = inl(iobase + ADDI_TCW_VAL_REG);

	status = inl(iobase + ADDI_TCW_STATUS_REG);
	data[1] = (status & ADDI_TCW_STATUS_SOFT_TRIG) ? 1 : 0;
	data[2] = (status & ADDI_TCW_STATUS_HARDWARE_TRIG) ? 1 : 0;
	data[3] = (status & ADDI_TCW_STATUS_SOFT_CLR) ? 1 : 0;
	data[4] = (status & ADDI_TCW_STATUS_OVERFLOW) ? 1 : 0;

	return insn->n;
}
