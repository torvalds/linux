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
	unsigned int nbytes;
	unsigned int nsamples;

	dmabuf = &devpriv->dmabuf[devpriv->cur_dmabuf];

	nbytes = dmabuf->use_size - inl(devpriv->amcc + AMCC_OP_REG_MWTC);

	if (nbytes < dmabuf->use_size)
		dev_err(dev->class_dev, "Interrupted DMA transfer!\n");
	if (nbytes & 1) {
		dev_err(dev->class_dev, "Odd count of bytes in DMA ring!\n");
		async->events |= COMEDI_CB_ERROR;
		return;
	}
	nsamples = comedi_bytes_to_samples(s, nbytes);

	if (devpriv->use_double_buffer) {
		struct apci3120_dmabuf *next_dmabuf;

		next_dmabuf = &devpriv->dmabuf[!devpriv->cur_dmabuf];

		/* start DMA on next buffer */
		apci3120_init_dma(dev, next_dmabuf);
	}

	if (nsamples) {
		comedi_buf_write_samples(s, dmabuf->virt, nsamples);

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
