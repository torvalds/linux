static int apci3120_cancel(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct apci3120_private *devpriv = dev->private;

	/* Add-On - disable DMA */
	outw(0, devpriv->addon + 4);

	/* Add-On - disable bus master */
	apci3120_addon_write(dev, 0, AMCC_OP_REG_AGCSTS);

	/* AMCC - disable bus master */
	outl(0, devpriv->amcc + AMCC_OP_REG_MCSR);

	/* disable all counters, ext trigger, and reset scan */
	devpriv->ctrl = 0;
	outw(devpriv->ctrl, dev->iobase + APCI3120_CTRL_REG);

	/* DISABLE_ALL_INTERRUPT */
	devpriv->mode = 0;
	outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);

	inw(dev->iobase + APCI3120_STATUS_REG);
	devpriv->cur_dmabuf = 0;

	return 0;
}

/*
 * This is a handler for the DMA interrupt.
 * This function copies the data to Comedi Buffer.
 * For continuous DMA it reinitializes the DMA operation.
 * For single mode DMA it stop the acquisition.
 */
static void apci3120_interrupt_dma(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci3120_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	struct apci3120_dmabuf *dmabuf;
	unsigned int samplesinbuf;

	dmabuf = &devpriv->dmabuf[devpriv->cur_dmabuf];

	samplesinbuf = dmabuf->use_size - inl(devpriv->amcc + AMCC_OP_REG_MWTC);

	if (samplesinbuf < dmabuf->use_size)
		dev_err(dev->class_dev, "Interrupted DMA transfer!\n");
	if (samplesinbuf & 1) {
		dev_err(dev->class_dev, "Odd count of bytes in DMA ring!\n");
		apci3120_cancel(dev, s);
		return;
	}
	samplesinbuf = samplesinbuf >> 1;	/*  number of received samples */

	if (devpriv->use_double_buffer) {
		struct apci3120_dmabuf *next_dmabuf;

		next_dmabuf = &devpriv->dmabuf[!devpriv->cur_dmabuf];

		/* start DMA on next buffer */
		apci3120_init_dma(dev, next_dmabuf);
	}

	if (samplesinbuf) {
		comedi_buf_write_samples(s, dmabuf->virt, samplesinbuf);

		if (!(cmd->flags & CMDF_WAKE_EOS))
			async->events |= COMEDI_CB_EOS;
	}

	if ((async->events & COMEDI_CB_CANCEL_MASK) ||
	    (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg))
		return;

	if (devpriv->use_double_buffer) {
		/* switch dma buffers for next interrupt */
		devpriv->cur_dmabuf = !devpriv->cur_dmabuf;
	} else {
		/* restart DMA if is not using double buffering */
		apci3120_init_dma(dev, dmabuf);
	}
}

static irqreturn_t apci3120_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci3120_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int status;
	unsigned int int_amcc;

	status = inw(dev->iobase + APCI3120_STATUS_REG);
	int_amcc = inl(devpriv->amcc + AMCC_OP_REG_INTCSR);

	if (!(status & APCI3120_STATUS_INT_MASK) &&
	    !(int_amcc & ANY_S593X_INT)) {
		dev_err(dev->class_dev, "IRQ from unknown source\n");
		return IRQ_NONE;
	}

	outl(int_amcc | AINT_INT_MASK, devpriv->amcc + AMCC_OP_REG_INTCSR);

	if (devpriv->ctrl & APCI3120_CTRL_EXT_TRIG)
		apci3120_exttrig_enable(dev, false);

	if (int_amcc & MASTER_ABORT_INT)
		dev_err(dev->class_dev, "AMCC IRQ - MASTER DMA ABORT!\n");
	if (int_amcc & TARGET_ABORT_INT)
		dev_err(dev->class_dev, "AMCC IRQ - TARGET DMA ABORT!\n");

	if ((status & APCI3120_STATUS_EOC_INT) == 0 &&
	    (devpriv->mode & APCI3120_MODE_EOC_IRQ_ENA)) {
		/* nothing to do... EOC mode is not currently used */
	}

	if ((status & APCI3120_STATUS_EOS_INT) &&
	    (devpriv->mode & APCI3120_MODE_EOS_IRQ_ENA)) {
		unsigned short val;
		int i;

		for (i = 0; i < cmd->chanlist_len; i++) {
			val = inw(dev->iobase + APCI3120_AI_FIFO_REG);
			comedi_buf_write_samples(s, &val, 1);
		}

		devpriv->mode |= APCI3120_MODE_EOS_IRQ_ENA;
		outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);
	}

	if (status & APCI3120_STATUS_TIMER2_INT) {
		/*
		 * for safety...
		 * timer2 interrupts are not enabled in the driver
		 */
		apci3120_clr_timer2_interrupt(dev);
	}

	if (status & APCI3120_STATUS_AMCC_INT) {
		/* AMCC- Clear write complete interrupt (DMA) */
		outl(AINT_WT_COMPLETE, devpriv->amcc + AMCC_OP_REG_INTCSR);

		/* do some data transfer */
		apci3120_interrupt_dma(irq, d);
	}

	if (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg)
		async->events |= COMEDI_CB_EOA;

	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}
