// SPDX-License-Identifier: GPL-2.0+
/*
 * Comedi driver for Keithley DAS-1700/DAS-1800 series boards
 * Copyright (C) 2000 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: das1800
 * Description: Keithley Metrabyte DAS1800 (& compatibles)
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Devices: [Keithley Metrabyte] DAS-1701ST (das-1701st),
 *   DAS-1701ST-DA (das-1701st-da), DAS-1701/AO (das-1701ao),
 *   DAS-1702ST (das-1702st), DAS-1702ST-DA (das-1702st-da),
 *   DAS-1702HR (das-1702hr), DAS-1702HR-DA (das-1702hr-da),
 *   DAS-1702/AO (das-1702ao), DAS-1801ST (das-1801st),
 *   DAS-1801ST-DA (das-1801st-da), DAS-1801HC (das-1801hc),
 *   DAS-1801AO (das-1801ao), DAS-1802ST (das-1802st),
 *   DAS-1802ST-DA (das-1802st-da), DAS-1802HR (das-1802hr),
 *   DAS-1802HR-DA (das-1802hr-da), DAS-1802HC (das-1802hc),
 *   DAS-1802AO (das-1802ao)
 * Status: works
 *
 * Configuration options:
 *   [0] - I/O port base address
 *   [1] - IRQ (optional, required for analog input cmd support)
 *   [2] - DMA0 (optional, requires irq)
 *   [3] - DMA1 (optional, requires irq and dma0)
 *
 * analog input cmd triggers supported:
 *
 *   start_src		TRIG_NOW	command starts immediately
 *			TRIG_EXT	command starts on external pin TGIN
 *
 *   scan_begin_src	TRIG_FOLLOW	paced/external scans start immediately
 *			TRIG_TIMER	burst scans start periodically
 *			TRIG_EXT	burst scans start on external pin XPCLK
 *
 *   scan_end_src	TRIG_COUNT	scan ends after last channel
 *
 *   convert_src	TRIG_TIMER	paced/burst conversions are timed
 *			TRIG_EXT	conversions on external pin XPCLK
 *					(requires scan_begin_src == TRIG_FOLLOW)
 *
 *   stop_src		TRIG_COUNT	command stops after stop_arg scans
 *			TRIG_EXT	command stops on external pin TGIN
 *			TRIG_NONE	command runs until canceled
 *
 * If TRIG_EXT is used for both the start_src and stop_src, the first TGIN
 * trigger starts the command, and the second trigger will stop it. If only
 * one is TRIG_EXT, the first trigger will either stop or start the command.
 * The external pin TGIN is normally set for negative edge triggering. It
 * can be set to positive edge with the CR_INVERT flag. If TRIG_EXT is used
 * for both the start_src and stop_src they must have the same polarity.
 *
 * Minimum conversion speed is limited to 64 microseconds (convert_arg <= 64000)
 * for 'burst' scans. This limitation does not apply for 'paced' scans. The
 * maximum conversion speed is limited by the board (convert_arg >= ai_speed).
 * Maximum conversion speeds are not always achievable depending on the
 * board setup (see user manual).
 *
 * NOTES:
 * Only the DAS-1801ST has been tested by me.
 * Unipolar and bipolar ranges cannot be mixed in the channel/gain list.
 *
 * The waveform analog output on the 'ao' cards is not supported.
 * If you need it, send me (Frank Hess) an email.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/comedi/comedidev.h>
#include <linux/comedi/comedi_8254.h>
#include <linux/comedi/comedi_isadma.h>

/* misc. defines */
#define DAS1800_SIZE           16	/* uses 16 io addresses */
#define FIFO_SIZE              1024	/*  1024 sample fifo */
#define DMA_BUF_SIZE           0x1ff00	/*  size in bytes of dma buffers */

/* Registers for the das1800 */
#define DAS1800_FIFO            0x0
#define DAS1800_QRAM            0x0
#define DAS1800_DAC             0x0
#define DAS1800_SELECT          0x2
#define   ADC                     0x0
#define   QRAM                    0x1
#define   DAC(a)                  (0x2 + a)
#define DAS1800_DIGITAL         0x3
#define DAS1800_CONTROL_A       0x4
#define   FFEN                    0x1
#define   CGEN                    0x4
#define   CGSL                    0x8
#define   TGEN                    0x10
#define   TGSL                    0x20
#define   TGPL                    0x40
#define   ATEN                    0x80
#define DAS1800_CONTROL_B       0x5
#define   DMA_CH5                 0x1
#define   DMA_CH6                 0x2
#define   DMA_CH7                 0x3
#define   DMA_CH5_CH6             0x5
#define   DMA_CH6_CH7             0x6
#define   DMA_CH7_CH5             0x7
#define   DMA_ENABLED             0x3
#define   DMA_DUAL                0x4
#define   IRQ3                    0x8
#define   IRQ5                    0x10
#define   IRQ7                    0x18
#define   IRQ10                   0x28
#define   IRQ11                   0x30
#define   IRQ15                   0x38
#define   FIMD                    0x40
#define DAS1800_CONTROL_C       0X6
#define   IPCLK                   0x1
#define   XPCLK                   0x3
#define   BMDE                    0x4
#define   CMEN                    0x8
#define   UQEN                    0x10
#define   SD                      0x40
#define   UB                      0x80
#define DAS1800_STATUS          0x7
#define   INT                     0x1
#define   DMATC                   0x2
#define   CT0TC                   0x8
#define   OVF                     0x10
#define   FHF                     0x20
#define   FNE                     0x40
#define   CVEN                    0x80
#define   CVEN_MASK               0x40
#define   CLEAR_INTR_MASK         (CVEN_MASK | 0x1f)
#define DAS1800_BURST_LENGTH    0x8
#define DAS1800_BURST_RATE      0x9
#define DAS1800_QRAM_ADDRESS    0xa
#define DAS1800_COUNTER         0xc

#define IOBASE2                   0x400

static const struct comedi_lrange das1801_ai_range = {
	8, {
		BIP_RANGE(5),		/* bipolar gain = 1 */
		BIP_RANGE(1),		/* bipolar gain = 10 */
		BIP_RANGE(0.1),		/* bipolar gain = 50 */
		BIP_RANGE(0.02),	/* bipolar gain = 250 */
		UNI_RANGE(5),		/* unipolar gain = 1 */
		UNI_RANGE(1),		/* unipolar gain = 10 */
		UNI_RANGE(0.1),		/* unipolar gain = 50 */
		UNI_RANGE(0.02)		/* unipolar gain = 250 */
	}
};

static const struct comedi_lrange das1802_ai_range = {
	8, {
		BIP_RANGE(10),		/* bipolar gain = 1 */
		BIP_RANGE(5),		/* bipolar gain = 2 */
		BIP_RANGE(2.5),		/* bipolar gain = 4 */
		BIP_RANGE(1.25),	/* bipolar gain = 8 */
		UNI_RANGE(10),		/* unipolar gain = 1 */
		UNI_RANGE(5),		/* unipolar gain = 2 */
		UNI_RANGE(2.5),		/* unipolar gain = 4 */
		UNI_RANGE(1.25)		/* unipolar gain = 8 */
	}
};

/*
 * The waveform analog outputs on the 'ao' boards are not currently
 * supported. They have a comedi_lrange of:
 * { 2, { BIP_RANGE(10), BIP_RANGE(5) } }
 */

enum das1800_boardid {
	BOARD_DAS1701ST,
	BOARD_DAS1701ST_DA,
	BOARD_DAS1702ST,
	BOARD_DAS1702ST_DA,
	BOARD_DAS1702HR,
	BOARD_DAS1702HR_DA,
	BOARD_DAS1701AO,
	BOARD_DAS1702AO,
	BOARD_DAS1801ST,
	BOARD_DAS1801ST_DA,
	BOARD_DAS1802ST,
	BOARD_DAS1802ST_DA,
	BOARD_DAS1802HR,
	BOARD_DAS1802HR_DA,
	BOARD_DAS1801HC,
	BOARD_DAS1802HC,
	BOARD_DAS1801AO,
	BOARD_DAS1802AO
};

/* board probe id values (hi byte of the digital input register) */
#define DAS1800_ID_ST_DA		0x3
#define DAS1800_ID_HR_DA		0x4
#define DAS1800_ID_AO			0x5
#define DAS1800_ID_HR			0x6
#define DAS1800_ID_ST			0x7
#define DAS1800_ID_HC			0x8

struct das1800_board {
	const char *name;
	unsigned char id;
	unsigned int ai_speed;
	unsigned int is_01_series:1;
};

static const struct das1800_board das1800_boards[] = {
	[BOARD_DAS1701ST] = {
		.name		= "das-1701st",
		.id		= DAS1800_ID_ST,
		.ai_speed	= 6250,
		.is_01_series	= 1,
	},
	[BOARD_DAS1701ST_DA] = {
		.name		= "das-1701st-da",
		.id		= DAS1800_ID_ST_DA,
		.ai_speed	= 6250,
		.is_01_series	= 1,
	},
	[BOARD_DAS1702ST] = {
		.name		= "das-1702st",
		.id		= DAS1800_ID_ST,
		.ai_speed	= 6250,
	},
	[BOARD_DAS1702ST_DA] = {
		.name		= "das-1702st-da",
		.id		= DAS1800_ID_ST_DA,
		.ai_speed	= 6250,
	},
	[BOARD_DAS1702HR] = {
		.name		= "das-1702hr",
		.id		= DAS1800_ID_HR,
		.ai_speed	= 20000,
	},
	[BOARD_DAS1702HR_DA] = {
		.name		= "das-1702hr-da",
		.id		= DAS1800_ID_HR_DA,
		.ai_speed	= 20000,
	},
	[BOARD_DAS1701AO] = {
		.name		= "das-1701ao",
		.id		= DAS1800_ID_AO,
		.ai_speed	= 6250,
		.is_01_series	= 1,
	},
	[BOARD_DAS1702AO] = {
		.name		= "das-1702ao",
		.id		= DAS1800_ID_AO,
		.ai_speed	= 6250,
	},
	[BOARD_DAS1801ST] = {
		.name		= "das-1801st",
		.id		= DAS1800_ID_ST,
		.ai_speed	= 3000,
		.is_01_series	= 1,
	},
	[BOARD_DAS1801ST_DA] = {
		.name		= "das-1801st-da",
		.id		= DAS1800_ID_ST_DA,
		.ai_speed	= 3000,
		.is_01_series	= 1,
	},
	[BOARD_DAS1802ST] = {
		.name		= "das-1802st",
		.id		= DAS1800_ID_ST,
		.ai_speed	= 3000,
	},
	[BOARD_DAS1802ST_DA] = {
		.name		= "das-1802st-da",
		.id		= DAS1800_ID_ST_DA,
		.ai_speed	= 3000,
	},
	[BOARD_DAS1802HR] = {
		.name		= "das-1802hr",
		.id		= DAS1800_ID_HR,
		.ai_speed	= 10000,
	},
	[BOARD_DAS1802HR_DA] = {
		.name		= "das-1802hr-da",
		.id		= DAS1800_ID_HR_DA,
		.ai_speed	= 10000,
	},
	[BOARD_DAS1801HC] = {
		.name		= "das-1801hc",
		.id		= DAS1800_ID_HC,
		.ai_speed	= 3000,
		.is_01_series	= 1,
	},
	[BOARD_DAS1802HC] = {
		.name		= "das-1802hc",
		.id		= DAS1800_ID_HC,
		.ai_speed	= 3000,
	},
	[BOARD_DAS1801AO] = {
		.name		= "das-1801ao",
		.id		= DAS1800_ID_AO,
		.ai_speed	= 3000,
		.is_01_series	= 1,
	},
	[BOARD_DAS1802AO] = {
		.name		= "das-1802ao",
		.id		= DAS1800_ID_AO,
		.ai_speed	= 3000,
	},
};

struct das1800_private {
	struct comedi_isadma *dma;
	int irq_dma_bits;
	int dma_bits;
	unsigned short *fifo_buf;
	unsigned long iobase2;
	bool ai_is_unipolar;
};

static void das1800_ai_munge(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     void *data, unsigned int num_bytes,
			     unsigned int start_chan_index)
{
	struct das1800_private *devpriv = dev->private;
	unsigned short *array = data;
	unsigned int num_samples = comedi_bytes_to_samples(s, num_bytes);
	unsigned int i;

	if (devpriv->ai_is_unipolar)
		return;

	for (i = 0; i < num_samples; i++)
		array[i] = comedi_offset_munge(s, array[i]);
}

static void das1800_handle_fifo_half_full(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;
	unsigned int nsamples = comedi_nsamples_left(s, FIFO_SIZE / 2);

	insw(dev->iobase + DAS1800_FIFO, devpriv->fifo_buf, nsamples);
	comedi_buf_write_samples(s, devpriv->fifo_buf, nsamples);
}

static void das1800_handle_fifo_not_empty(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned short dpnt;

	while (inb(dev->iobase + DAS1800_STATUS) & FNE) {
		dpnt = inw(dev->iobase + DAS1800_FIFO);
		comedi_buf_write_samples(s, &dpnt, 1);

		if (cmd->stop_src == TRIG_COUNT &&
		    s->async->scans_done >= cmd->stop_arg)
			break;
	}
}

static void das1800_flush_dma_channel(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_isadma_desc *desc)
{
	unsigned int residue = comedi_isadma_disable(desc->chan);
	unsigned int nbytes = desc->size - residue;
	unsigned int nsamples;

	/*  figure out how many points to read */
	nsamples = comedi_bytes_to_samples(s, nbytes);
	nsamples = comedi_nsamples_left(s, nsamples);

	comedi_buf_write_samples(s, desc->virt_addr, nsamples);
}

static void das1800_flush_dma(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[dma->cur_dma];
	const int dual_dma = devpriv->irq_dma_bits & DMA_DUAL;

	das1800_flush_dma_channel(dev, s, desc);

	if (dual_dma) {
		/*  switch to other channel and flush it */
		dma->cur_dma = 1 - dma->cur_dma;
		desc = &dma->desc[dma->cur_dma];
		das1800_flush_dma_channel(dev, s, desc);
	}

	/*  get any remaining samples in fifo */
	das1800_handle_fifo_not_empty(dev, s);
}

static void das1800_handle_dma(struct comedi_device *dev,
			       struct comedi_subdevice *s, unsigned int status)
{
	struct das1800_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[dma->cur_dma];
	const int dual_dma = devpriv->irq_dma_bits & DMA_DUAL;

	das1800_flush_dma_channel(dev, s, desc);

	/* re-enable dma channel */
	comedi_isadma_program(desc);

	if (status & DMATC) {
		/*  clear DMATC interrupt bit */
		outb(CLEAR_INTR_MASK & ~DMATC, dev->iobase + DAS1800_STATUS);
		/*  switch dma channels for next time, if appropriate */
		if (dual_dma)
			dma->cur_dma = 1 - dma->cur_dma;
	}
}

static int das1800_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc;
	int i;

	/* disable and stop conversions */
	outb(0x0, dev->iobase + DAS1800_STATUS);
	outb(0x0, dev->iobase + DAS1800_CONTROL_B);
	outb(0x0, dev->iobase + DAS1800_CONTROL_A);

	if (dma) {
		for (i = 0; i < 2; i++) {
			desc = &dma->desc[i];
			if (desc->chan)
				comedi_isadma_disable(desc->chan);
		}
	}

	return 0;
}

static void das1800_ai_handler(struct comedi_device *dev)
{
	struct das1800_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int status = inb(dev->iobase + DAS1800_STATUS);

	/* select adc register (spinlock is already held) */
	outb(ADC, dev->iobase + DAS1800_SELECT);

	/* get samples with dma, fifo, or polled as necessary */
	if (devpriv->irq_dma_bits & DMA_ENABLED)
		das1800_handle_dma(dev, s, status);
	else if (status & FHF)
		das1800_handle_fifo_half_full(dev, s);
	else if (status & FNE)
		das1800_handle_fifo_not_empty(dev, s);

	/* if the card's fifo has overflowed */
	if (status & OVF) {
		/*  clear OVF interrupt bit */
		outb(CLEAR_INTR_MASK & ~OVF, dev->iobase + DAS1800_STATUS);
		dev_err(dev->class_dev, "FIFO overflow\n");
		async->events |= COMEDI_CB_ERROR;
		comedi_handle_events(dev, s);
		return;
	}
	/*  stop taking data if appropriate */
	/* stop_src TRIG_EXT */
	if (status & CT0TC) {
		/*  clear CT0TC interrupt bit */
		outb(CLEAR_INTR_MASK & ~CT0TC, dev->iobase + DAS1800_STATUS);
		/* get all remaining samples before quitting */
		if (devpriv->irq_dma_bits & DMA_ENABLED)
			das1800_flush_dma(dev, s);
		else
			das1800_handle_fifo_not_empty(dev, s);
		async->events |= COMEDI_CB_EOA;
	} else if (cmd->stop_src == TRIG_COUNT &&
		   async->scans_done >= cmd->stop_arg) {
		async->events |= COMEDI_CB_EOA;
	}

	comedi_handle_events(dev, s);
}

static int das1800_ai_poll(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	unsigned long flags;

	/*
	 * Protects the indirect addressing selected by DAS1800_SELECT
	 * in das1800_ai_handler() also prevents race with das1800_interrupt().
	 */
	spin_lock_irqsave(&dev->spinlock, flags);

	das1800_ai_handler(dev);

	spin_unlock_irqrestore(&dev->spinlock, flags);

	return comedi_buf_n_bytes_ready(s);
}

static irqreturn_t das1800_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	unsigned int status;

	if (!dev->attached) {
		dev_err(dev->class_dev, "premature interrupt\n");
		return IRQ_HANDLED;
	}

	/*
	 * Protects the indirect addressing selected by DAS1800_SELECT
	 * in das1800_ai_handler() also prevents race with das1800_ai_poll().
	 */
	spin_lock(&dev->spinlock);

	status = inb(dev->iobase + DAS1800_STATUS);

	/* if interrupt was not caused by das-1800 */
	if (!(status & INT)) {
		spin_unlock(&dev->spinlock);
		return IRQ_NONE;
	}
	/* clear the interrupt status bit INT */
	outb(CLEAR_INTR_MASK & ~INT, dev->iobase + DAS1800_STATUS);
	/*  handle interrupt */
	das1800_ai_handler(dev);

	spin_unlock(&dev->spinlock);
	return IRQ_HANDLED;
}

static int das1800_ai_fixup_paced_timing(struct comedi_device *dev,
					 struct comedi_cmd *cmd)
{
	unsigned int arg = cmd->convert_arg;

	/*
	 * Paced mode:
	 *	scan_begin_src is TRIG_FOLLOW
	 *	convert_src is TRIG_TIMER
	 *
	 * The convert_arg sets the pacer sample acquisition time.
	 * The max acquisition speed is limited to the boards
	 * 'ai_speed' (this was already verified). The min speed is
	 * limited by the cascaded 8254 timer.
	 */
	comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
	return comedi_check_trigger_arg_is(&cmd->convert_arg, arg);
}

static int das1800_ai_fixup_burst_timing(struct comedi_device *dev,
					 struct comedi_cmd *cmd)
{
	unsigned int arg = cmd->convert_arg;
	int err = 0;

	/*
	 * Burst mode:
	 *	scan_begin_src is TRIG_TIMER or TRIG_EXT
	 *	convert_src is TRIG_TIMER
	 *
	 * The convert_arg sets burst sample acquisition time.
	 * The max acquisition speed is limited to the boards
	 * 'ai_speed' (this was already verified). The min speed is
	 * limiited to 64 microseconds,
	 */
	err |= comedi_check_trigger_arg_max(&arg, 64000);

	/* round to microseconds then verify */
	switch (cmd->flags & CMDF_ROUND_MASK) {
	case CMDF_ROUND_NEAREST:
	default:
		arg = DIV_ROUND_CLOSEST(arg, 1000);
		break;
	case CMDF_ROUND_DOWN:
		arg = arg / 1000;
		break;
	case CMDF_ROUND_UP:
		arg = DIV_ROUND_UP(arg, 1000);
		break;
	}
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg * 1000);

	/*
	 * The pacer can be used to set the scan sample rate. The max scan
	 * speed is limited by the conversion speed and the number of channels
	 * to convert. The min speed is limited by the cascaded 8254 timer.
	 */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->convert_arg * cmd->chanlist_len;
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg, arg);

		arg = cmd->scan_begin_arg;
		comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	return err;
}

static int das1800_ai_check_chanlist(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_cmd *cmd)
{
	unsigned int range = CR_RANGE(cmd->chanlist[0]);
	bool unipolar0 = comedi_range_is_unipolar(s, range);
	int i;

	for (i = 1; i < cmd->chanlist_len; i++) {
		range = CR_RANGE(cmd->chanlist[i]);

		if (unipolar0 != comedi_range_is_unipolar(s, range)) {
			dev_dbg(dev->class_dev,
				"unipolar and bipolar ranges cannot be mixed in the chanlist\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int das1800_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	const struct das1800_board *board = dev->board_ptr;
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_FOLLOW | TRIG_TIMER | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src,
					TRIG_COUNT | TRIG_EXT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	/* burst scans must use timed conversions */
	if (cmd->scan_begin_src != TRIG_FOLLOW &&
	    cmd->convert_src != TRIG_TIMER)
		err |= -EINVAL;

	/* the external pin TGIN must use the same polarity */
	if (cmd->start_src == TRIG_EXT && cmd->stop_src == TRIG_EXT)
		err |= comedi_check_trigger_arg_is(&cmd->start_arg,
						   cmd->stop_arg);

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	if (cmd->start_arg == TRIG_NOW)
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->convert_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
						    board->ai_speed);
	}

	err |= comedi_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
		break;
	case TRIG_NONE:
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
	default:
		break;
	}

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->scan_begin_src == TRIG_FOLLOW)
			err |= das1800_ai_fixup_paced_timing(dev, cmd);
		else /* TRIG_TIMER or TRIG_EXT */
			err |= das1800_ai_fixup_burst_timing(dev, cmd);
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= das1800_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static unsigned char das1800_ai_chanspec_bits(struct comedi_subdevice *s,
					      unsigned int chanspec)
{
	unsigned int range = CR_RANGE(chanspec);
	unsigned int aref = CR_AREF(chanspec);
	unsigned char bits;

	bits = UQEN;
	if (aref != AREF_DIFF)
		bits |= SD;
	if (aref == AREF_COMMON)
		bits |= CMEN;
	if (comedi_range_is_unipolar(s, range))
		bits |= UB;

	return bits;
}

static unsigned int das1800_ai_transfer_size(struct comedi_device *dev,
					     struct comedi_subdevice *s,
					     unsigned int maxbytes,
					     unsigned int ns)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int max_samples = comedi_bytes_to_samples(s, maxbytes);
	unsigned int samples;

	samples = max_samples;

	/* for timed modes, make dma buffer fill in 'ns' time */
	switch (cmd->scan_begin_src) {
	case TRIG_FOLLOW:	/* not in burst mode */
		if (cmd->convert_src == TRIG_TIMER)
			samples = ns / cmd->convert_arg;
		break;
	case TRIG_TIMER:
		samples = ns / (cmd->scan_begin_arg * cmd->chanlist_len);
		break;
	}

	/* limit samples to what is remaining in the command */
	samples = comedi_nsamples_left(s, samples);

	if (samples > max_samples)
		samples = max_samples;
	if (samples < 1)
		samples = 1;

	return comedi_samples_to_bytes(s, samples);
}

static void das1800_ai_setup_dma(struct comedi_device *dev,
				 struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc;
	unsigned int bytes;

	if ((devpriv->irq_dma_bits & DMA_ENABLED) == 0)
		return;

	dma->cur_dma = 0;
	desc = &dma->desc[0];

	/* determine a dma transfer size to fill buffer in 0.3 sec */
	bytes = das1800_ai_transfer_size(dev, s, desc->maxsize, 300000000);

	desc->size = bytes;
	comedi_isadma_program(desc);

	/* set up dual dma if appropriate */
	if (devpriv->irq_dma_bits & DMA_DUAL) {
		desc = &dma->desc[1];
		desc->size = bytes;
		comedi_isadma_program(desc);
	}
}

static void das1800_ai_set_chanlist(struct comedi_device *dev,
				    unsigned int *chanlist, unsigned int len)
{
	unsigned long flags;
	unsigned int i;

	/* protects the indirect addressing selected by DAS1800_SELECT */
	spin_lock_irqsave(&dev->spinlock, flags);

	/* select QRAM register and set start address */
	outb(QRAM, dev->iobase + DAS1800_SELECT);
	outb(len - 1, dev->iobase + DAS1800_QRAM_ADDRESS);

	/* make channel / gain list */
	for (i = 0; i < len; i++) {
		unsigned int chan = CR_CHAN(chanlist[i]);
		unsigned int range = CR_RANGE(chanlist[i]);
		unsigned short val;

		val = chan | ((range & 0x3) << 8);
		outw(val, dev->iobase + DAS1800_QRAM);
	}

	/* finish write to QRAM */
	outb(len - 1, dev->iobase + DAS1800_QRAM_ADDRESS);

	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static int das1800_ai_cmd(struct comedi_device *dev,
			  struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;
	int control_a, control_c;
	struct comedi_async *async = s->async;
	const struct comedi_cmd *cmd = &async->cmd;
	unsigned int range0 = CR_RANGE(cmd->chanlist[0]);

	/*
	 * Disable dma on CMDF_WAKE_EOS, or CMDF_PRIORITY (because dma in
	 * handler is unsafe at hard real-time priority).
	 */
	if (cmd->flags & (CMDF_WAKE_EOS | CMDF_PRIORITY))
		devpriv->irq_dma_bits &= ~DMA_ENABLED;
	else
		devpriv->irq_dma_bits |= devpriv->dma_bits;
	/*  interrupt on end of conversion for CMDF_WAKE_EOS */
	if (cmd->flags & CMDF_WAKE_EOS) {
		/*  interrupt fifo not empty */
		devpriv->irq_dma_bits &= ~FIMD;
	} else {
		/*  interrupt fifo half full */
		devpriv->irq_dma_bits |= FIMD;
	}

	das1800_ai_cancel(dev, s);

	devpriv->ai_is_unipolar = comedi_range_is_unipolar(s, range0);

	control_a = FFEN;
	if (cmd->stop_src == TRIG_EXT)
		control_a |= ATEN;
	if (cmd->start_src == TRIG_EXT)
		control_a |= TGEN | CGSL;
	else /* TRIG_NOW */
		control_a |= CGEN;
	if (control_a & (ATEN | TGEN)) {
		if ((cmd->start_arg & CR_INVERT) || (cmd->stop_arg & CR_INVERT))
			control_a |= TGPL;
	}

	control_c = das1800_ai_chanspec_bits(s, cmd->chanlist[0]);
	/* set clock source to internal or external */
	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		/* not in burst mode */
		if (cmd->convert_src == TRIG_TIMER) {
			/* trig on cascaded counters */
			control_c |= IPCLK;
		} else { /* TRIG_EXT */
			/* trig on falling edge of external trigger */
			control_c |= XPCLK;
		}
	} else if (cmd->scan_begin_src == TRIG_TIMER) {
		/* burst mode with internal pacer clock */
		control_c |= BMDE | IPCLK;
	} else { /* TRIG_EXT */
		/* burst mode with external trigger */
		control_c |= BMDE | XPCLK;
	}

	das1800_ai_set_chanlist(dev, cmd->chanlist, cmd->chanlist_len);

	/* setup cascaded counters for conversion/scan frequency */
	if ((cmd->scan_begin_src == TRIG_FOLLOW ||
	     cmd->scan_begin_src == TRIG_TIMER) &&
	    cmd->convert_src == TRIG_TIMER) {
		comedi_8254_update_divisors(dev->pacer);
		comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
	}

	/* setup counter 0 for 'about triggering' */
	if (cmd->stop_src == TRIG_EXT)
		comedi_8254_load(dev->pacer, 0, 1, I8254_MODE0 | I8254_BINARY);

	das1800_ai_setup_dma(dev, s);
	outb(control_c, dev->iobase + DAS1800_CONTROL_C);
	/*  set conversion rate and length for burst mode */
	if (control_c & BMDE) {
		outb(cmd->convert_arg / 1000 - 1,	/* microseconds - 1 */
		     dev->iobase + DAS1800_BURST_RATE);
		outb(cmd->chanlist_len - 1, dev->iobase + DAS1800_BURST_LENGTH);
	}

	/* enable and start conversions */
	outb(devpriv->irq_dma_bits, dev->iobase + DAS1800_CONTROL_B);
	outb(control_a, dev->iobase + DAS1800_CONTROL_A);
	outb(CVEN, dev->iobase + DAS1800_STATUS);

	return 0;
}

static int das1800_ai_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned char status;

	status = inb(dev->iobase + DAS1800_STATUS);
	if (status & FNE)
		return 0;
	return -EBUSY;
}

static int das1800_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int range = CR_RANGE(insn->chanspec);
	bool is_unipolar = comedi_range_is_unipolar(s, range);
	int ret = 0;
	int n;
	unsigned short dpnt;
	unsigned long flags;

	outb(das1800_ai_chanspec_bits(s, insn->chanspec),
	     dev->iobase + DAS1800_CONTROL_C);		/* software pacer */
	outb(CVEN, dev->iobase + DAS1800_STATUS);	/* enable conversions */
	outb(0x0, dev->iobase + DAS1800_CONTROL_A);	/* reset fifo */
	outb(FFEN, dev->iobase + DAS1800_CONTROL_A);

	das1800_ai_set_chanlist(dev, &insn->chanspec, 1);

	/* protects the indirect addressing selected by DAS1800_SELECT */
	spin_lock_irqsave(&dev->spinlock, flags);

	/* select ai fifo register */
	outb(ADC, dev->iobase + DAS1800_SELECT);

	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		outb(0, dev->iobase + DAS1800_FIFO);

		ret = comedi_timeout(dev, s, insn, das1800_ai_eoc, 0);
		if (ret)
			break;

		dpnt = inw(dev->iobase + DAS1800_FIFO);
		if (!is_unipolar)
			dpnt = comedi_offset_munge(s, dpnt);
		data[n] = dpnt;
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return ret ? ret : insn->n;
}

static int das1800_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int update_chan = s->n_chan - 1;
	unsigned long flags;
	int i;

	/* protects the indirect addressing selected by DAS1800_SELECT */
	spin_lock_irqsave(&dev->spinlock, flags);

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		s->readback[chan] = val;

		val = comedi_offset_munge(s, val);

		/* load this channel (and update if it's the last channel) */
		outb(DAC(chan), dev->iobase + DAS1800_SELECT);
		outw(val, dev->iobase + DAS1800_DAC);

		/* update all channels */
		if (chan != update_chan) {
			val = comedi_offset_munge(s, s->readback[update_chan]);

			outb(DAC(update_chan), dev->iobase + DAS1800_SELECT);
			outw(val, dev->iobase + DAS1800_DAC);
		}
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return insn->n;
}

static int das1800_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	data[1] = inb(dev->iobase + DAS1800_DIGITAL) & 0xf;
	data[0] = 0;

	return insn->n;
}

static int das1800_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DAS1800_DIGITAL);

	data[1] = s->state;

	return insn->n;
}

static void das1800_init_dma(struct comedi_device *dev,
			     struct comedi_devconfig *it)
{
	struct das1800_private *devpriv = dev->private;
	unsigned int *dma_chan;

	/*
	 * it->options[2] is DMA channel 0
	 * it->options[3] is DMA channel 1
	 *
	 * Encode the DMA channels into 2 digit hexadecimal for switch.
	 */
	dma_chan = &it->options[2];

	switch ((dma_chan[0] & 0x7) | (dma_chan[1] << 4)) {
	case 0x5:	/*  dma0 == 5 */
		devpriv->dma_bits = DMA_CH5;
		break;
	case 0x6:	/*  dma0 == 6 */
		devpriv->dma_bits = DMA_CH6;
		break;
	case 0x7:	/*  dma0 == 7 */
		devpriv->dma_bits = DMA_CH7;
		break;
	case 0x65:	/*  dma0 == 5, dma1 == 6 */
		devpriv->dma_bits = DMA_CH5_CH6;
		break;
	case 0x76:	/*  dma0 == 6, dma1 == 7 */
		devpriv->dma_bits = DMA_CH6_CH7;
		break;
	case 0x57:	/*  dma0 == 7, dma1 == 5 */
		devpriv->dma_bits = DMA_CH7_CH5;
		break;
	default:
		return;
	}

	/* DMA can use 1 or 2 buffers, each with a separate channel */
	devpriv->dma = comedi_isadma_alloc(dev, dma_chan[1] ? 2 : 1,
					   dma_chan[0], dma_chan[1],
					   DMA_BUF_SIZE, COMEDI_ISADMA_READ);
	if (!devpriv->dma)
		devpriv->dma_bits = 0;
}

static void das1800_free_dma(struct comedi_device *dev)
{
	struct das1800_private *devpriv = dev->private;

	if (devpriv)
		comedi_isadma_free(devpriv->dma);
}

static int das1800_probe(struct comedi_device *dev)
{
	const struct das1800_board *board = dev->board_ptr;
	unsigned char id;

	id = (inb(dev->iobase + DAS1800_DIGITAL) >> 4) & 0xf;

	/*
	 * The dev->board_ptr will be set by comedi_device_attach() if the
	 * board name provided by the user matches a board->name in this
	 * driver. If so, this function sanity checks the id to verify that
	 * the board is correct.
	 */
	if (board) {
		if (board->id == id)
			return 0;
		dev_err(dev->class_dev,
			"probed id does not match board id (0x%x != 0x%x)\n",
			id, board->id);
		return -ENODEV;
	}

	 /*
	  * If the dev->board_ptr is not set, the user is trying to attach
	  * an unspecified board to this driver. In this case the id is used
	  * to 'probe' for the dev->board_ptr.
	  */
	switch (id) {
	case DAS1800_ID_ST_DA:
		/* das-1701st-da, das-1702st-da, das-1801st-da, das-1802st-da */
		board = &das1800_boards[BOARD_DAS1801ST_DA];
		break;
	case DAS1800_ID_HR_DA:
		/* das-1702hr-da, das-1802hr-da */
		board = &das1800_boards[BOARD_DAS1802HR_DA];
		break;
	case DAS1800_ID_AO:
		/* das-1701ao, das-1702ao, das-1801ao, das-1802ao */
		board = &das1800_boards[BOARD_DAS1801AO];
		break;
	case DAS1800_ID_HR:
		/*  das-1702hr, das-1802hr */
		board = &das1800_boards[BOARD_DAS1802HR];
		break;
	case DAS1800_ID_ST:
		/* das-1701st, das-1702st, das-1801st, das-1802st */
		board = &das1800_boards[BOARD_DAS1801ST];
		break;
	case DAS1800_ID_HC:
		/* das-1801hc, das-1802hc */
		board = &das1800_boards[BOARD_DAS1801HC];
		break;
	default:
		dev_err(dev->class_dev, "invalid probe id 0x%x\n", id);
		return -ENODEV;
	}
	dev->board_ptr = board;
	dev->board_name = board->name;
	dev_warn(dev->class_dev,
		 "probed id 0x%0x: %s series (not recommended)\n",
		 id, board->name);
	return 0;
}

static int das1800_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	const struct das1800_board *board;
	struct das1800_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int irq = it->options[1];
	bool is_16bit;
	int ret;
	int i;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], DAS1800_SIZE);
	if (ret)
		return ret;

	ret = das1800_probe(dev);
	if (ret)
		return ret;
	board = dev->board_ptr;

	is_16bit = board->id == DAS1800_ID_HR || board->id == DAS1800_ID_HR_DA;

	/* waveform 'ao' boards have additional io ports */
	if (board->id == DAS1800_ID_AO) {
		unsigned long iobase2 = dev->iobase + IOBASE2;

		ret = __comedi_request_region(dev, iobase2, DAS1800_SIZE);
		if (ret)
			return ret;
		devpriv->iobase2 = iobase2;
	}

	if (irq == 3 || irq == 5 || irq == 7 || irq == 10 || irq == 11 ||
	    irq == 15) {
		ret = request_irq(irq, das1800_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0) {
			dev->irq = irq;

			switch (irq) {
			case 3:
				devpriv->irq_dma_bits |= 0x8;
				break;
			case 5:
				devpriv->irq_dma_bits |= 0x10;
				break;
			case 7:
				devpriv->irq_dma_bits |= 0x18;
				break;
			case 10:
				devpriv->irq_dma_bits |= 0x28;
				break;
			case 11:
				devpriv->irq_dma_bits |= 0x30;
				break;
			case 15:
				devpriv->irq_dma_bits |= 0x38;
				break;
			}
		}
	}

	/* an irq and one dma channel is required to use dma */
	if (dev->irq & it->options[2])
		das1800_init_dma(dev, it);

	devpriv->fifo_buf = kmalloc_array(FIFO_SIZE,
					  sizeof(*devpriv->fifo_buf),
					  GFP_KERNEL);
	if (!devpriv->fifo_buf)
		return -ENOMEM;

	dev->pacer = comedi_8254_io_alloc(dev->iobase + DAS1800_COUNTER,
					  I8254_OSC_BASE_5MHZ, I8254_IO8, 0);
	if (IS_ERR(dev->pacer))
		return PTR_ERR(dev->pacer);

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/*
	 * Analog Input subdevice
	 *
	 * The "hc" type boards have 64 analog input channels and a 64
	 * entry QRAM fifo.
	 *
	 * All the other board types have 16 on-board channels. Each channel
	 * can be expanded to 16 channels with the addition of an EXP-1800
	 * expansion board for a total of 256 channels. The QRAM fifo on
	 * these boards has 256 entries.
	 *
	 * From the datasheets it's not clear what the comedi channel to
	 * actual physical channel mapping is when EXP-1800 boards are used.
	 */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_DIFF | SDF_GROUND;
	if (board->id != DAS1800_ID_HC)
		s->subdev_flags	|= SDF_COMMON;
	s->n_chan	= (board->id == DAS1800_ID_HC) ? 64 : 256;
	s->maxdata	= is_16bit ? 0xffff : 0x0fff;
	s->range_table	= board->is_01_series ? &das1801_ai_range
					      : &das1802_ai_range;
	s->insn_read	= das1800_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= s->n_chan;
		s->do_cmd	= das1800_ai_cmd;
		s->do_cmdtest	= das1800_ai_cmdtest;
		s->poll		= das1800_ai_poll;
		s->cancel	= das1800_ai_cancel;
		s->munge	= das1800_ai_munge;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	if (board->id == DAS1800_ID_ST_DA || board->id == DAS1800_ID_HR_DA) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= (board->id == DAS1800_ID_ST_DA) ? 4 : 2;
		s->maxdata	= is_16bit ? 0xffff : 0x0fff;
		s->range_table	= &range_bipolar10;
		s->insn_write	= das1800_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;

		/* initialize all channels to 0V */
		for (i = 0; i < s->n_chan; i++) {
			/* spinlock is not necessary during the attach */
			outb(DAC(i), dev->iobase + DAS1800_SELECT);
			outw(0, dev->iobase + DAS1800_DAC);
		}
	} else if (board->id == DAS1800_ID_AO) {
		/*
		 * 'ao' boards have waveform analog outputs that are not
		 * currently supported.
		 */
		s->type		= COMEDI_SUBD_UNUSED;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das1800_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= (board->id == DAS1800_ID_HC) ? 8 : 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das1800_do_insn_bits;

	das1800_ai_cancel(dev, dev->read_subdev);

	/*  initialize digital out channels */
	outb(0, dev->iobase + DAS1800_DIGITAL);

	return 0;
};

static void das1800_detach(struct comedi_device *dev)
{
	struct das1800_private *devpriv = dev->private;

	das1800_free_dma(dev);
	if (devpriv) {
		kfree(devpriv->fifo_buf);
		if (devpriv->iobase2)
			release_region(devpriv->iobase2, DAS1800_SIZE);
	}
	comedi_legacy_detach(dev);
}

static struct comedi_driver das1800_driver = {
	.driver_name	= "das1800",
	.module		= THIS_MODULE,
	.attach		= das1800_attach,
	.detach		= das1800_detach,
	.num_names	= ARRAY_SIZE(das1800_boards),
	.board_name	= &das1800_boards[0].name,
	.offset		= sizeof(struct das1800_board),
};
module_comedi_driver(das1800_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for DAS1800 compatible ISA boards");
MODULE_LICENSE("GPL");
