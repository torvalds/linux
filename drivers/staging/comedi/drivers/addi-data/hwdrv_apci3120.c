static void apci3120_addon_write(struct comedi_device *dev,
				 unsigned int val, unsigned int reg)
{
	struct apci3120_private *devpriv = dev->private;

	/* 16-bit interface for AMCC add-on registers */

	outw(reg, devpriv->addon + APCI3120_ADDON_ADDR_REG);
	outw(val & 0xffff, devpriv->addon + APCI3120_ADDON_DATA_REG);

	outw(reg + 2, devpriv->addon + APCI3120_ADDON_ADDR_REG);
	outw((val >> 16) & 0xffff, devpriv->addon + APCI3120_ADDON_DATA_REG);
}

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

static void apci3120_init_dma(struct comedi_device *dev,
			      struct apci3120_dmabuf *dmabuf)
{
	struct apci3120_private *devpriv = dev->private;

	/* AMCC - enable transfer count and reset A2P FIFO */
	outl(AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
	     devpriv->amcc + AMCC_OP_REG_AGCSTS);

	/* Add-On - enable transfer count and reset A2P FIFO */
	apci3120_addon_write(dev, AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
			     AMCC_OP_REG_AGCSTS);

	/* AMCC - enable transfers and reset A2P flags */
	outl(RESET_A2P_FLAGS | EN_A2P_TRANSFERS,
	     devpriv->amcc + AMCC_OP_REG_MCSR);

	/* Add-On - DMA start address */
	apci3120_addon_write(dev, dmabuf->hw, AMCC_OP_REG_AMWAR);

	/* Add-On - Number of acquisitions */
	apci3120_addon_write(dev, dmabuf->use_size, AMCC_OP_REG_AMWTC);

	/* AMCC - enable write complete (DMA) and set FIFO advance */
	outl(APCI3120_FIFO_ADVANCE_ON_BYTE_2 | AINT_WRITE_COMPL,
	     devpriv->amcc + AMCC_OP_REG_INTCSR);

	/* Add-On - enable DMA */
	outw(APCI3120_ADDON_CTRL_AMWEN_ENA | APCI3120_ADDON_CTRL_A2P_FIFO_ENA,
	     devpriv->addon + APCI3120_ADDON_CTRL_REG);
}

static void apci3120_setup_dma(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct apci3120_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	struct apci3120_dmabuf *dmabuf0 = &devpriv->dmabuf[0];
	struct apci3120_dmabuf *dmabuf1 = &devpriv->dmabuf[1];
	unsigned int dmalen0 = dmabuf0->size;
	unsigned int dmalen1 = dmabuf1->size;
	unsigned int scan_bytes;

	scan_bytes = comedi_samples_to_bytes(s, cmd->scan_end_arg);

	if (cmd->stop_src == TRIG_COUNT) {
		/*
		 * Must we fill full first buffer? And must we fill
		 * full second buffer when first is once filled?
		 */
		if (dmalen0 > (cmd->stop_arg * scan_bytes))
			dmalen0 = cmd->stop_arg * scan_bytes;
		else if (dmalen1 > (cmd->stop_arg * scan_bytes - dmalen0))
			dmalen1 = cmd->stop_arg * scan_bytes - dmalen0;
	}

	if (cmd->flags & CMDF_WAKE_EOS) {
		/* don't we want wake up every scan? */
		if (dmalen0 > scan_bytes) {
			dmalen0 = scan_bytes;
			if (cmd->scan_end_arg & 1)
				dmalen0 += 2;
		}
		if (dmalen1 > scan_bytes) {
			dmalen1 = scan_bytes;
			if (cmd->scan_end_arg & 1)
				dmalen1 -= 2;
			if (dmalen1 < 4)
				dmalen1 = 4;
		}
	} else {
		/* isn't output buff smaller that our DMA buff? */
		if (dmalen0 > s->async->prealloc_bufsz)
			dmalen0 = s->async->prealloc_bufsz;
		if (dmalen1 > s->async->prealloc_bufsz)
			dmalen1 = s->async->prealloc_bufsz;
	}
	dmabuf0->use_size = dmalen0;
	dmabuf1->use_size = dmalen1;

	apci3120_init_dma(dev, dmabuf0);
}

static int apci3120_ai_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct apci3120_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int divisor;

	/* set default mode bits */
	devpriv->mode = APCI3120_MODE_TIMER2_CLK_OSC |
			APCI3120_MODE_TIMER2_AS_TIMER;

	/* AMCC- Clear write complete interrupt (DMA) */
	outl(AINT_WT_COMPLETE, devpriv->amcc + AMCC_OP_REG_INTCSR);

	devpriv->cur_dmabuf = 0;

	/* load chanlist for command scan */
	apci3120_set_chanlist(dev, s, cmd->chanlist_len, cmd->chanlist);

	if (cmd->start_src == TRIG_EXT)
		apci3120_exttrig_enable(dev, true);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		/*
		 * Timer 1 is used in MODE2 (rate generator) to set the
		 * start time for each scan.
		 */
		divisor = apci3120_ns_to_timer(dev, 1, cmd->scan_begin_arg,
					       cmd->flags);
		apci3120_timer_set_mode(dev, 1, APCI3120_TIMER_MODE2);
		apci3120_timer_write(dev, 1, divisor);
	}

	/*
	 * Timer 0 is used in MODE2 (rate generator) to set the conversion
	 * time for each acquisition.
	 */
	divisor = apci3120_ns_to_timer(dev, 0, cmd->convert_arg, cmd->flags);
	apci3120_timer_set_mode(dev, 0, APCI3120_TIMER_MODE2);
	apci3120_timer_write(dev, 0, divisor);

	if (devpriv->use_dma)
		apci3120_setup_dma(dev, s);
	else
		devpriv->mode |= APCI3120_MODE_EOS_IRQ_ENA;

	/* set mode to enable acquisition */
	outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);

	if (cmd->scan_begin_src == TRIG_TIMER)
		apci3120_timer_enable(dev, 1, true);
	apci3120_timer_enable(dev, 0, true);

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
