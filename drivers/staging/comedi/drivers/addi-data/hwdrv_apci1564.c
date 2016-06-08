static int apci1564_counter_insn_config(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn,
					unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned long iobase = devpriv->counters + APCI1564_COUNTER(chan);
	unsigned int ctrl;

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
