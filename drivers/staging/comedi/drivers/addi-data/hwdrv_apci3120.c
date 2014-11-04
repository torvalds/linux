/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

	ADDI-DATA GmbH
	Dieselstrasse 3
	D-77833 Ottersweier
	Tel: +19(0)7223/9493-0
	Fax: +49(0)7223/9493-92
	http://www.addi-data.com
	info@addi-data.com

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

@endverbatim
*/
/*
  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstrasse 3      D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-----------------------------------------------------------------------+
  | Project     : APCI-3120       | Compiler   : GCC                      |
  | Module name : hwdrv_apci3120.c| Version    : 2.96                     |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date       :  02/12/2002              |
  +-----------------------------------------------------------------------+
  | Description :APCI3120 Module.  Hardware abstraction Layer for APCI3120|
  +-----------------------------------------------------------------------+
  |                             UPDATE'S                                  |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |          | 		 | 						  |
  |          |           |						  |
  +----------+-----------+------------------------------------------------+
*/

/*
 * ADDON RELATED ADDITIONS
 */
/* Constant */
#define APCI3120_AMWEN_ENABLE				0x02
#define APCI3120_A2P_FIFO_WRITE_ENABLE			0x01
#define APCI3120_FIFO_ADVANCE_ON_BYTE_2			0x20000000L
#define APCI3120_DISABLE_AMWEN_AND_A2P_FIFO_WRITE	0x0
#define APCI3120_DISABLE_BUS_MASTER_ADD_ON		0x0
#define APCI3120_DISABLE_BUS_MASTER_PCI			0x0

#define APCI3120_START			1
#define APCI3120_STOP			0

#define APCI3120_RD_FIFO		0x00

/* software trigger dummy register */
#define APCI3120_START_CONVERSION	0x02

/* TIMER DEFINE */
#define APCI3120_QUARTZ_A		70
#define APCI3120_QUARTZ_B		50
#define APCI3120_TIMER			1
#define APCI3120_WATCHDOG		2
#define APCI3120_TIMER_DISABLE		0
#define APCI3120_TIMER_ENABLE		1

#define APCI3120_COUNTER		3

static void apci3120_addon_write(struct comedi_device *dev,
				 unsigned int val, unsigned int reg)
{
	struct apci3120_private *devpriv = dev->private;

	/* 16-bit interface for AMCC add-on registers */

	outw(reg, devpriv->addon + 0);
	outw(val & 0xffff, devpriv->addon + 2);

	outw(reg + 2, devpriv->addon + 0);
	outw((val >> 16) & 0xffff, devpriv->addon + 2);
}

static int apci3120_reset(struct comedi_device *dev)
{
	struct apci3120_private *devpriv = dev->private;

	/*  variables used in timer subdevice */
	devpriv->b_Timer2Mode = 0;
	devpriv->b_Timer2Interrupt = 0;

	/* Disable all interrupts, watchdog for the anolog output */
	devpriv->mode = 0;
	outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);

	/* disable all counters, ext trigger, and reset scan */
	devpriv->ctrl = 0;
	outw(devpriv->ctrl, dev->iobase + APCI3120_CTRL_REG);

	inw(dev->iobase + APCI3120_STATUS_REG);

	return 0;
}

static int apci3120_cancel(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct apci3120_private *devpriv = dev->private;

	/*  Disable A2P Fifo write and AMWEN signal */
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
	devpriv->ui_DmaActualBuffer = 0;

	return 0;
}

static int apci3120_ai_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_TIMER)	/* Test Delay timing */
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg, 100000);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->convert_arg)
			err |= cfc_check_trigger_arg_min(&cmd->convert_arg,
							 10000);
	} else {
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg, 10000);
	}

	err |= cfc_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/*  TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/*  step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER &&
	    cmd->scan_begin_arg < cmd->convert_arg * cmd->scan_end_arg) {
		cmd->scan_begin_arg = cmd->convert_arg * cmd->scan_end_arg;
		err |= -EINVAL;
	}

	if (err)
		return 4;

	return 0;
}

static void apci3120_init_dma(struct comedi_device *dev,
			      struct apci3120_dmabuf *dmabuf)
{
	/* Add-On - DMA start address */
	apci3120_addon_write(dev, dmabuf->hw, AMCC_OP_REG_AMWAR);

	/* Add-On - Number of acquisitions */
	apci3120_addon_write(dev, dmabuf->use_size, AMCC_OP_REG_AMWTC);
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

	/* Initialize DMA */

	/* AMCC- enable transfer count and reset A2P FIFO */
	outl(AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
	     devpriv->amcc + AMCC_OP_REG_AGCSTS);

	/* Add-On - enable transfer count and reset A2P FIFO */
	apci3120_addon_write(dev, AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
			     AMCC_OP_REG_AGCSTS);

	/* AMCC - enable transfers and reset A2P flags */
	outl(RESET_A2P_FLAGS | EN_A2P_TRANSFERS,
	     devpriv->amcc + AMCC_OP_REG_MCSR);

	apci3120_init_dma(dev, dmabuf0);

	/* AMCC- reset A2P flags */
	outl(RESET_A2P_FLAGS, devpriv->amcc + AMCC_OP_REG_MCSR);

	/* AMCC - enable write complete (DMA) and set FIFO advance */
	outl(APCI3120_FIFO_ADVANCE_ON_BYTE_2 | AINT_WRITE_COMPL,
	     devpriv->amcc + AMCC_OP_REG_INTCSR);

	/* ENABLE A2P FIFO WRITE AND ENABLE AMWEN */
	outw(3, devpriv->addon + 4);

	/* AMCC- reset A2P flags */
	outl(RESET_A2P_FLAGS, devpriv->amcc + AMCC_OP_REG_MCSR);
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

	devpriv->ui_DmaActualBuffer = 0;

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

	if (devpriv->us_UseDma) {
		apci3120_setup_dma(dev, s);
	} else {
		devpriv->mode |= APCI3120_MODE_EOS_IRQ_ENA;

		if (cmd->stop_src == TRIG_COUNT) {
			/*
			 * Timer 2 is used in MODE0 (hardware retriggerable
			 * one-shot) to count the number of scans.
			 *
			 * NOTE: not sure about the -2 value
			 */
			apci3120_timer_set_mode(dev, 2, APCI3120_TIMER_MODE0);
			apci3120_timer_write(dev, 2, cmd->stop_arg - 2);

			apci3120_clr_timer2_interrupt(dev);

			apci3120_timer_enable(dev, 2, true);

			/* configure Timer 2 For counting EOS */
			devpriv->mode |= APCI3120_MODE_TIMER2_AS_COUNTER |
					 APCI3120_MODE_TIMER2_CLK_EOS |
					 APCI3120_MODE_TIMER2_IRQ_ENA;

			devpriv->b_Timer2Mode = APCI3120_COUNTER;
			devpriv->b_Timer2Interrupt = 1;
		}
	}

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
	struct comedi_cmd *cmd = &s->async->cmd;
	struct apci3120_dmabuf *dmabuf;
	unsigned int samplesinbuf;

	dmabuf = &devpriv->dmabuf[devpriv->ui_DmaActualBuffer];

	samplesinbuf = dmabuf->use_size - inl(devpriv->amcc + AMCC_OP_REG_MWTC);

	if (samplesinbuf < dmabuf->use_size)
		dev_err(dev->class_dev, "Interrupted DMA transfer!\n");
	if (samplesinbuf & 1) {
		dev_err(dev->class_dev, "Odd count of bytes in DMA ring!\n");
		apci3120_cancel(dev, s);
		return;
	}
	samplesinbuf = samplesinbuf >> 1;	/*  number of received samples */
	if (devpriv->b_DmaDoubleBuffer) {
		/*  switch DMA buffers if is used double buffering */
		struct apci3120_dmabuf *next_dmabuf;

		next_dmabuf = &devpriv->dmabuf[1 - devpriv->ui_DmaActualBuffer];

		/* AMCC - enable transfer count and reset A2P FIFO */
		outl(AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
		     devpriv->amcc + AMCC_OP_REG_AGCSTS);

		/* Add-On - enable transfer count and reset A2P FIFO */
		apci3120_addon_write(dev,
				     AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
				     AMCC_OP_REG_AGCSTS);

		apci3120_init_dma(dev, next_dmabuf);

		/*
		 * To configure A2P FIFO
		 * ENABLE A2P FIFO WRITE AND ENABLE AMWEN
		 * AMWEN_ENABLE | A2P_FIFO_WRITE_ENABLE (0x01|0x02)=0x03
		 */
		outw(3, devpriv->addon + 4);

		/* AMCC - enable write complete (DMA) and set FIFO advance */
		outl(APCI3120_FIFO_ADVANCE_ON_BYTE_2 | AINT_WRITE_COMPL,
		     devpriv->amcc + AMCC_OP_REG_INTCSR);

	}
	if (samplesinbuf) {
		comedi_buf_write_samples(s, dmabuf->virt, samplesinbuf);

		if (!(cmd->flags & CMDF_WAKE_EOS))
			s->async->events |= COMEDI_CB_EOS;
	}
	if (cmd->stop_src == TRIG_COUNT &&
	    s->async->scans_done >= cmd->stop_arg) {
		s->async->events |= COMEDI_CB_EOA;
		return;
	}

	if (devpriv->b_DmaDoubleBuffer) {	/*  switch dma buffers */
		devpriv->ui_DmaActualBuffer = 1 - devpriv->ui_DmaActualBuffer;
	} else {
		/* restart DMA if is not using double buffering */

		/* AMCC - enable transfer count and reset A2P FIFO */
		outl(AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
		     devpriv->amcc + AMCC_OP_REG_AGCSTS);

		/* Add-On - enable transfer count and reset A2P FIFO */
		apci3120_addon_write(dev,
				     AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
				     AMCC_OP_REG_AGCSTS);

		/* AMCC - enable transfers and reset A2P flags */
		outl(RESET_A2P_FLAGS | EN_A2P_TRANSFERS,
		     devpriv->amcc + AMCC_OP_REG_MCSR);

		apci3120_init_dma(dev, dmabuf);

		/*
		 * To configure A2P FIFO
		 * ENABLE A2P FIFO WRITE AND ENABLE AMWEN
		 * AMWEN_ENABLE | A2P_FIFO_WRITE_ENABLE (0x01|0x02)=0x03
		 */
		outw(3, devpriv->addon + 4);

		/* AMCC - enable write complete (DMA) and set FIFO advance */
		outl(APCI3120_FIFO_ADVANCE_ON_BYTE_2 | AINT_WRITE_COMPL,
		     devpriv->amcc + AMCC_OP_REG_INTCSR);
	}
}

static irqreturn_t apci3120_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci3120_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_cmd *cmd = &s->async->cmd;
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

	apci3120_clr_timer2_interrupt(dev);

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
			val = inw(dev->iobase + 0);
			comedi_buf_write_samples(s, &val, 1);
		}

		devpriv->mode |= APCI3120_MODE_EOS_IRQ_ENA;
		outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);
	}

	if (status & APCI3120_STATUS_TIMER2_INT) {
		switch (devpriv->b_Timer2Mode) {
		case APCI3120_COUNTER:
			devpriv->mode &= ~APCI3120_MODE_EOS_IRQ_ENA;
			outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);

			s->async->events |= COMEDI_CB_EOA;
			break;

		case APCI3120_TIMER:

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);
			break;

		case APCI3120_WATCHDOG:

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);
			break;

		default:
			/*  disable Timer Interrupt */
			devpriv->mode &= ~APCI3120_MODE_TIMER2_IRQ_ENA;
			outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);
		}

		apci3120_clr_timer2_interrupt(dev);
	}

	if (status & APCI3120_STATUS_AMCC_INT) {
		/* AMCC- Clear write complete interrupt (DMA) */
		outl(AINT_WT_COMPLETE, devpriv->amcc + AMCC_OP_REG_INTCSR);

		apci3120_clr_timer2_interrupt(dev);

		/* do some data transfer */
		apci3120_interrupt_dma(irq, d);
	}
	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

/*
 * Configure Timer 2
 *
 * data[0] = TIMER configure as timer
 *	   = WATCHDOG configure as watchdog
 * data[1] = Timer constant
 * data[2] = Timer2 interrupt (1)enable or(0) disable
 */
static int apci3120_config_insn_timer(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int divisor;

	if (!data[1])
		dev_err(dev->class_dev, "No timer constant!\n");

	devpriv->b_Timer2Interrupt = (unsigned char) data[2];	/*  save info whether to enable or disable interrupt */

	divisor = apci3120_ns_to_timer(dev, 2, data[1], CMDF_ROUND_DOWN);

	apci3120_timer_enable(dev, 2, false);

	/* disable timer 2 interrupt and reset operation mode (timer) */
	devpriv->mode &= ~APCI3120_MODE_TIMER2_IRQ_ENA &
			 ~APCI3120_MODE_TIMER2_AS_MASK;

	/*  Disable Eoc and Eos Interrupts */
	devpriv->mode &= ~APCI3120_MODE_EOC_IRQ_ENA &
			 ~APCI3120_MODE_EOS_IRQ_ENA;
	outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);

	if (data[0] == APCI3120_TIMER) {	/* initialize timer */
		/* Set the Timer 2 in mode 2(Timer) */
		apci3120_timer_set_mode(dev, 2, APCI3120_TIMER_MODE2);

		/* Set timer 2 delay */
		apci3120_timer_write(dev, 2, divisor);

		/*  timer2 in Timer mode enabled */
		devpriv->b_Timer2Mode = APCI3120_TIMER;
	} else {			/*  Initialize Watch dog */
		/* Set the Timer 2 in mode 5(Watchdog) */
		apci3120_timer_set_mode(dev, 2, APCI3120_TIMER_MODE5);

		/* Set timer 2 delay */
		apci3120_timer_write(dev, 2, divisor);

		/* watchdog enabled */
		devpriv->b_Timer2Mode = APCI3120_WATCHDOG;

	}

	return insn->n;

}

/*
 * To start and stop the timer
 *
 * data[0] = 1 (start)
 *	   = 0 (stop)
 *	   = 2 (write new value)
 * data[1] = new value
 *
 * devpriv->b_Timer2Mode = 0 DISABLE
 *			 = 1 Timer
 *			 = 2 Watch dog
 */
static int apci3120_write_insn_timer(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int divisor;

	if ((devpriv->b_Timer2Mode != APCI3120_WATCHDOG)
		&& (devpriv->b_Timer2Mode != APCI3120_TIMER)) {
		dev_err(dev->class_dev, "timer2 not configured\n");
		return -EINVAL;
	}

	if (data[0] == 2) {	/*  write new value */
		if (devpriv->b_Timer2Mode != APCI3120_TIMER) {
			dev_err(dev->class_dev,
				"timer2 not configured in TIMER MODE\n");
			return -EINVAL;
		}
	}

	switch (data[0]) {
	case APCI3120_START:
		apci3120_clr_timer2_interrupt(dev);

		if (devpriv->b_Timer2Mode == APCI3120_TIMER) {	/* start timer */
			/* Enable Timer */
			devpriv->mode &= 0x0b;
			devpriv->mode |= APCI3120_MODE_TIMER2_AS_TIMER;
		} else {		/* start watch dog */
			/* Enable WatchDog */
			devpriv->mode &= 0x0b;
			devpriv->mode |= APCI3120_MODE_TIMER2_AS_WDOG;
		}

		/* enable disable interrupt */
		if (devpriv->b_Timer2Interrupt) {
			devpriv->mode |= APCI3120_MODE_TIMER2_IRQ_ENA;

			/*  save the task structure to pass info to user */
			devpriv->tsk_Current = current;
		} else {
			devpriv->mode &= ~APCI3120_MODE_TIMER2_IRQ_ENA;
		}
		outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);

		/* start timer */
		if (devpriv->b_Timer2Mode == APCI3120_TIMER)
			apci3120_timer_enable(dev, 2, true);
		break;

	case APCI3120_STOP:
		/* disable timer 2 interrupt and reset operation mode (timer) */
		devpriv->mode &= ~APCI3120_MODE_TIMER2_IRQ_ENA &
				 ~APCI3120_MODE_TIMER2_AS_MASK;
		outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);

		apci3120_timer_enable(dev, 2, false);

		apci3120_clr_timer2_interrupt(dev);
		break;

	case 2:		/* write new value to Timer */
		if (devpriv->b_Timer2Mode != APCI3120_TIMER) {
			dev_err(dev->class_dev,
				"timer2 not configured in TIMER MODE\n");
			return -EINVAL;
		}

		divisor = apci3120_ns_to_timer(dev, 2, data[1],
					       CMDF_ROUND_DOWN);

		/* Set timer 2 delay */
		apci3120_timer_write(dev, 2, divisor);
		break;
	default:
		return -EINVAL;	/*  Not a valid input */
	}

	return insn->n;
}

/*
 * Read the Timer value
 *
 * for Timer: data[0]= Timer constant
 *
 * for watchdog: data[0] = 0 (still running)
 *			 = 1 (run down)
 */
static int apci3120_read_insn_timer(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int status;

	if ((devpriv->b_Timer2Mode != APCI3120_WATCHDOG)
		&& (devpriv->b_Timer2Mode != APCI3120_TIMER)) {
		dev_err(dev->class_dev, "timer2 not configured\n");
	}
	if (devpriv->b_Timer2Mode == APCI3120_TIMER) {
		data[0] = apci3120_timer_read(dev, 2);
	} else {
		/* Read watch dog status */
		status = inw(dev->iobase + APCI3120_STATUS_REG);
		if (status & APCI3120_STATUS_TIMER2_INT) {
			apci3120_clr_timer2_interrupt(dev);
			data[0] = 1;
		} else {
			data[0] = 0;
		}
	}
	return insn->n;
}
