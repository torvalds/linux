/*
 * dt282x.c
 * Comedi driver for Data Translation DT2821 series
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver: dt282x
 * Description: Data Translation DT2821 series (including DT-EZ)
 * Author: ds
 * Devices: (Data Translation) DT2821 [dt2821]
 *	    (Data Translation) DT2821-F-16SE [dt2821-f]
 *	    (Data Translation) DT2821-F-8DI [dt2821-f]
 *	    (Data Translation) DT2821-G-16SE [dt2821-g]
 *	    (Data Translation) DT2821-G-8DI [dt2821-g]
 *	    (Data Translation) DT2823 [dt2823]
 *	    (Data Translation) DT2824-PGH [dt2824-pgh]
 *	    (Data Translation) DT2824-PGL [dt2824-pgl]
 *	    (Data Translation) DT2825 [dt2825]
 *	    (Data Translation) DT2827 [dt2827]
 *	    (Data Translation) DT2828 [dt2828]
 *	    (Data Translation) DT2928 [dt2829]
 *	    (Data Translation) DT21-EZ [dt21-ez]
 *	    (Data Translation) DT23-EZ [dt23-ez]
 *	    (Data Translation) DT24-EZ [dt24-ez]
 *	    (Data Translation) DT24-EZ-PGL [dt24-ez-pgl]
 * Status: complete
 * Updated: Wed, 22 Aug 2001 17:11:34 -0700
 *
 * Configuration options:
 *   [0] - I/O port base address
 *   [1] - IRQ (optional, required for async command support)
 *   [2] - DMA 1 (optional, required for async command support)
 *   [3] - DMA 2 (optional, required for async command support)
 *   [4] - AI jumpered for 0=single ended, 1=differential
 *   [5] - AI jumpered for 0=straight binary, 1=2's complement
 *   [6] - AO 0 data format (deprecated, see below)
 *   [7] - AO 1 data format (deprecated, see below)
 *   [8] - AI jumpered for 0=[-10,10]V, 1=[0,10], 2=[-5,5], 3=[0,5]
 *   [9] - AO channel 0 range (deprecated, see below)
 *   [10]- AO channel 1 range (deprecated, see below)
 *
 * Notes:
 *   - AO commands might be broken.
 *   - If you try to run a command on both the AI and AO subdevices
 *     simultaneously, bad things will happen.  The driver needs to
 *     be fixed to check for this situation and return an error.
 *   - AO range is not programmable. The AO subdevice has a range_table
 *     containing all the possible analog output ranges. Use the range
 *     that matches your board configuration to convert between data
 *     values and physical units. The format of the data written to the
 *     board is handled automatically based on the unipolar/bipolar
 *     range that is selected.
 */

#include <linux/module.h>
#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <asm/dma.h>

#include "comedi_fc.h"

/*
 * Register map
 */
#define DT2821_ADCSR_REG		0x00
#define DT2821_ADCSR_ADERR		(1 << 15)
#define DT2821_ADCSR_ADCLK		(1 << 9)
#define DT2821_ADCSR_MUXBUSY		(1 << 8)
#define DT2821_ADCSR_ADDONE		(1 << 7)
#define DT2821_ADCSR_IADDONE		(1 << 6)
#define DT2821_ADCSR_GS(x)		(((x) & 0x3) << 4)
#define DT2821_ADCSR_CHAN(x)		(((x) & 0xf) << 0)
#define DT2821_CHANCSR_REG		0x02
#define DT2821_CHANCSR_LLE		(1 << 15)
#define DT2821_CHANCSR_PRESLA(x)	(((x) & 0xf) >> 8)
#define DT2821_CHANCSR_NUMB(x)		((((x) - 1) & 0xf) << 0)
#define DT2821_ADDAT_REG		0x04
#define DT2821_DACSR_REG		0x06
#define DT2821_DACSR_DAERR		(1 << 15)
#define DT2821_DACSR_YSEL(x)		((x) << 9)
#define DT2821_DACSR_SSEL		(1 << 8)
#define DT2821_DACSR_DACRDY		(1 << 7)
#define DT2821_DACSR_IDARDY		(1 << 6)
#define DT2821_DACSR_DACLK		(1 << 5)
#define DT2821_DACSR_HBOE		(1 << 1)
#define DT2821_DACSR_LBOE		(1 << 0)
#define DT2821_DADAT_REG		0x08
#define DT2821_DIODAT_REG		0x0a
#define DT2821_SUPCSR_REG		0x0c
#define DT2821_SUPCSR_DMAD		(1 << 15)
#define DT2821_SUPCSR_ERRINTEN		(1 << 14)
#define DT2821_SUPCSR_CLRDMADNE		(1 << 13)
#define DT2821_SUPCSR_DDMA		(1 << 12)
#define DT2821_SUPCSR_DS_PIO		(0 << 10)
#define DT2821_SUPCSR_DS_AD_CLK		(1 << 10)
#define DT2821_SUPCSR_DS_DA_CLK		(2 << 10)
#define DT2821_SUPCSR_DS_AD_TRIG	(3 << 10)
#define DT2821_SUPCSR_BUFFB		(1 << 9)
#define DT2821_SUPCSR_SCDN		(1 << 8)
#define DT2821_SUPCSR_DACON		(1 << 7)
#define DT2821_SUPCSR_ADCINIT		(1 << 6)
#define DT2821_SUPCSR_DACINIT		(1 << 5)
#define DT2821_SUPCSR_PRLD		(1 << 4)
#define DT2821_SUPCSR_STRIG		(1 << 3)
#define DT2821_SUPCSR_XTRIG		(1 << 2)
#define DT2821_SUPCSR_XCLK		(1 << 1)
#define DT2821_SUPCSR_BDINIT		(1 << 0)
#define DT2821_TMRCTR_REG		0x0e

static const struct comedi_lrange range_dt282x_ai_lo_bipolar = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25)
	}
};

static const struct comedi_lrange range_dt282x_ai_lo_unipolar = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

static const struct comedi_lrange range_dt282x_ai_5_bipolar = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625)
	}
};

static const struct comedi_lrange range_dt282x_ai_5_unipolar = {
	4, {
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25),
		UNI_RANGE(0.625)
	}
};

static const struct comedi_lrange range_dt282x_ai_hi_bipolar = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(1),
		BIP_RANGE(0.1),
		BIP_RANGE(0.02)
	}
};

static const struct comedi_lrange range_dt282x_ai_hi_unipolar = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(1),
		UNI_RANGE(0.1),
		UNI_RANGE(0.02)
	}
};

/*
 * The Analog Output range is set per-channel using jumpers on the board.
 * All of these ranges may not be available on some DT2821 series boards.
 * The default jumper setting has both channels set for +/-10V output.
 */
static const struct comedi_lrange dt282x_ao_range = {
	5, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		UNI_RANGE(10),
		UNI_RANGE(5),
	}
};

struct dt282x_board {
	const char *name;
	unsigned int ai_maxdata;
	int adchan_se;
	int adchan_di;
	int ai_speed;
	int ispgl;
	int dachan;
	unsigned int ao_maxdata;
};

static const struct dt282x_board boardtypes[] = {
	{
		.name		= "dt2821",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 20000,
		.dachan		= 2,
		.ao_maxdata	= 0x0fff,
	}, {
		.name		= "dt2821-f",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 6500,
		.dachan		= 2,
		.ao_maxdata	= 0x0fff,
	}, {
		.name		= "dt2821-g",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 4000,
		.dachan		= 2,
		.ao_maxdata	= 0x0fff,
	}, {
		.name		= "dt2823",
		.ai_maxdata	= 0xffff,
		.adchan_di	= 4,
		.ai_speed	= 10000,
		.dachan		= 2,
		.ao_maxdata	= 0xffff,
	}, {
		.name		= "dt2824-pgh",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 20000,
	}, {
		.name		= "dt2824-pgl",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 20000,
		.ispgl		= 1,
	}, {
		.name		= "dt2825",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 20000,
		.ispgl		= 1,
		.dachan		= 2,
		.ao_maxdata	= 0x0fff,
	}, {
		.name		= "dt2827",
		.ai_maxdata	= 0xffff,
		.adchan_di	= 4,
		.ai_speed	= 10000,
		.dachan		= 2,
		.ao_maxdata	= 0x0fff,
	}, {
		.name		= "dt2828",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 4,
		.ai_speed	= 10000,
		.dachan		= 2,
		.ao_maxdata	= 0x0fff,
	}, {
		.name		= "dt2829",
		.ai_maxdata	= 0xffff,
		.adchan_se	= 8,
		.ai_speed	= 33250,
		.dachan		= 2,
		.ao_maxdata	= 0xffff,
	}, {
		.name		= "dt21-ez",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 10000,
		.dachan		= 2,
		.ao_maxdata	= 0x0fff,
	}, {
		.name		= "dt23-ez",
		.ai_maxdata	= 0xffff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 10000,
	}, {
		.name		= "dt24-ez",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 10000,
	}, {
		.name		= "dt24-ez-pgl",
		.ai_maxdata	= 0x0fff,
		.adchan_se	= 16,
		.adchan_di	= 8,
		.ai_speed	= 10000,
		.ispgl		= 1,
	},
};

struct dt282x_private {
	unsigned int ad_2scomp:1;

	unsigned int divisor;

	unsigned short ao_readback[2];

	int dacsr;	/* software copies of registers */
	int adcsr;
	int supcsr;

	int ntrig;
	int nread;

	struct {
		int chan;
		unsigned short *buf;	/* DMA buffer */
		int size;	/* size of current transfer */
	} dma[2];
	int dma_maxsize;	/* max size of DMA transfer (in bytes) */
	int current_dma_index;
	int dma_dir;
};

static int dt282x_prep_ai_dma(struct comedi_device *dev, int dma_index, int n)
{
	struct dt282x_private *devpriv = dev->private;
	int dma_chan;
	unsigned long dma_ptr;
	unsigned long flags;

	if (!devpriv->ntrig)
		return 0;

	if (n == 0)
		n = devpriv->dma_maxsize;
	if (n > devpriv->ntrig * 2)
		n = devpriv->ntrig * 2;
	devpriv->ntrig -= n / 2;

	devpriv->dma[dma_index].size = n;
	dma_chan = devpriv->dma[dma_index].chan;
	dma_ptr = virt_to_bus(devpriv->dma[dma_index].buf);

	set_dma_mode(dma_chan, DMA_MODE_READ);
	flags = claim_dma_lock();
	clear_dma_ff(dma_chan);
	set_dma_addr(dma_chan, dma_ptr);
	set_dma_count(dma_chan, n);
	release_dma_lock(flags);

	enable_dma(dma_chan);

	return n;
}

static int dt282x_prep_ao_dma(struct comedi_device *dev, int dma_index, int n)
{
	struct dt282x_private *devpriv = dev->private;
	int dma_chan;
	unsigned long dma_ptr;
	unsigned long flags;

	devpriv->dma[dma_index].size = n;
	dma_chan = devpriv->dma[dma_index].chan;
	dma_ptr = virt_to_bus(devpriv->dma[dma_index].buf);

	set_dma_mode(dma_chan, DMA_MODE_WRITE);
	flags = claim_dma_lock();
	clear_dma_ff(dma_chan);
	set_dma_addr(dma_chan, dma_ptr);
	set_dma_count(dma_chan, n);
	release_dma_lock(flags);

	enable_dma(dma_chan);

	return n;
}

static void dt282x_disable_dma(struct comedi_device *dev)
{
	struct dt282x_private *devpriv = dev->private;

	disable_dma(devpriv->dma[0].chan);
	disable_dma(devpriv->dma[1].chan);
}

static unsigned int dt282x_ns_to_timer(unsigned int *ns, unsigned int flags)
{
	unsigned int prescale, base, divider;

	for (prescale = 0; prescale < 16; prescale++) {
		if (prescale == 1)
			continue;
		base = 250 * (1 << prescale);
		switch (flags & TRIG_ROUND_MASK) {
		case TRIG_ROUND_NEAREST:
		default:
			divider = (*ns + base / 2) / base;
			break;
		case TRIG_ROUND_DOWN:
			divider = (*ns) / base;
			break;
		case TRIG_ROUND_UP:
			divider = (*ns + base - 1) / base;
			break;
		}
		if (divider < 256) {
			*ns = divider * base;
			return (prescale << 8) | (255 - divider);
		}
	}
	base = 250 * (1 << 15);
	divider = 255;
	*ns = divider * base;
	return (15 << 8) | (255 - divider);
}

static void dt282x_munge(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 unsigned short *buf,
			 unsigned int nbytes)
{
	struct dt282x_private *devpriv = dev->private;
	unsigned int val;
	int i;

	if (nbytes % 2)
		comedi_error(dev, "bug! odd number of bytes from dma xfer");

	for (i = 0; i < nbytes / 2; i++) {
		val = buf[i];
		val &= s->maxdata;
		if (devpriv->ad_2scomp)
			val = comedi_offset_munge(s, val);

		buf[i] = val;
	}
}

static void dt282x_ao_dma_interrupt(struct comedi_device *dev,
				    struct comedi_subdevice *s)
{
	struct dt282x_private *devpriv = dev->private;
	int cur_dma = devpriv->current_dma_index;
	void *ptr = devpriv->dma[cur_dma].buf;
	int size;

	outw(devpriv->supcsr | DT2821_SUPCSR_CLRDMADNE,
	     dev->iobase + DT2821_SUPCSR_REG);

	disable_dma(devpriv->dma[cur_dma].chan);

	devpriv->current_dma_index = 1 - cur_dma;

	size = cfc_read_array_from_buffer(s, ptr, devpriv->dma_maxsize);
	if (size == 0) {
		dev_err(dev->class_dev, "AO underrun\n");
		s->async->events |= COMEDI_CB_OVERFLOW;
	} else {
		dt282x_prep_ao_dma(dev, cur_dma, size);
	}
}

static void dt282x_ai_dma_interrupt(struct comedi_device *dev,
				    struct comedi_subdevice *s)
{
	struct dt282x_private *devpriv = dev->private;
	int cur_dma = devpriv->current_dma_index;
	void *ptr = devpriv->dma[cur_dma].buf;
	int size = devpriv->dma[cur_dma].size;
	int ret;

	outw(devpriv->supcsr | DT2821_SUPCSR_CLRDMADNE,
	     dev->iobase + DT2821_SUPCSR_REG);

	disable_dma(devpriv->dma[cur_dma].chan);

	devpriv->current_dma_index = 1 - cur_dma;

	dt282x_munge(dev, s, ptr, size);
	ret = cfc_write_array_to_buffer(s, ptr, size);
	if (ret != size) {
		s->async->events |= COMEDI_CB_OVERFLOW;
		return;
	}

	devpriv->nread -= size / 2;
	if (devpriv->nread < 0) {
		dev_info(dev->class_dev, "nread off by one\n");
		devpriv->nread = 0;
	}
	if (!devpriv->nread) {
		s->async->events |= COMEDI_CB_EOA;
		return;
	}
#if 0
	/* clear the dual dma flag, making this the last dma segment */
	/* XXX probably wrong */
	if (!devpriv->ntrig) {
		devpriv->supcsr &= ~DT2821_SUPCSR_DDMA;
		outw(devpriv->supcsr, dev->iobase + DT2821_SUPCSR_REG);
	}
#endif
	/* restart the channel */
	dt282x_prep_ai_dma(dev, cur_dma, 0);
}

static irqreturn_t dt282x_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct dt282x_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_subdevice *s_ao = dev->write_subdev;
	unsigned int supcsr, adcsr, dacsr;
	int handled = 0;

	if (!dev->attached) {
		comedi_error(dev, "spurious interrupt");
		return IRQ_HANDLED;
	}

	adcsr = inw(dev->iobase + DT2821_ADCSR_REG);
	dacsr = inw(dev->iobase + DT2821_DACSR_REG);
	supcsr = inw(dev->iobase + DT2821_SUPCSR_REG);
	if (supcsr & DT2821_SUPCSR_DMAD) {
		if (devpriv->dma_dir == DMA_MODE_READ)
			dt282x_ai_dma_interrupt(dev, s);
		else
			dt282x_ao_dma_interrupt(dev, s_ao);
		handled = 1;
	}
	if (adcsr & DT2821_ADCSR_ADERR) {
		if (devpriv->nread != 0) {
			comedi_error(dev, "A/D error");
			s->async->events |= COMEDI_CB_ERROR;
		}
		handled = 1;
	}
	if (dacsr & DT2821_DACSR_DAERR) {
		comedi_error(dev, "D/A error");
		s_ao->async->events |= COMEDI_CB_ERROR;
		handled = 1;
	}
#if 0
	if (adcsr & DT2821_ADCSR_ADDONE) {
		int ret;
		unsigned short data;

		data = inw(dev->iobase + DT2821_ADDAT_REG);
		data &= s->maxdata;
		if (devpriv->ad_2scomp)
			data = comedi_offset_munge(s, data);

		ret = comedi_buf_put(s, data);

		if (ret == 0)
			s->async->events |= COMEDI_CB_OVERFLOW;

		devpriv->nread--;
		if (!devpriv->nread) {
			s->async->events |= COMEDI_CB_EOA;
		} else {
			if (supcsr & DT2821_SUPCSR_SCDN)
				outw(devpriv->supcsr | DT2821_SUPCSR_STRIG,
				     dev->iobase + DT2821_SUPCSR_REG);
		}
		handled = 1;
	}
#endif
	cfc_handle_events(dev, s);
	cfc_handle_events(dev, s_ao);

	return IRQ_RETVAL(handled);
}

static void dt282x_load_changain(struct comedi_device *dev, int n,
				 unsigned int *chanlist)
{
	struct dt282x_private *devpriv = dev->private;
	int i;

	outw(DT2821_CHANCSR_LLE | DT2821_CHANCSR_NUMB(n),
	     dev->iobase + DT2821_CHANCSR_REG);
	for (i = 0; i < n; i++) {
		unsigned int chan = CR_CHAN(chanlist[i]);
		unsigned int range = CR_RANGE(chanlist[i]);

		outw(devpriv->adcsr |
		     DT2821_ADCSR_GS(range) |
		     DT2821_ADCSR_CHAN(chan),
		     dev->iobase + DT2821_ADCSR_REG);
	}
	outw(DT2821_CHANCSR_NUMB(n), dev->iobase + DT2821_CHANCSR_REG);
}

static int dt282x_ai_timeout(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + DT2821_ADCSR_REG);
	switch (context) {
	case DT2821_ADCSR_MUXBUSY:
		if ((status & DT2821_ADCSR_MUXBUSY) == 0)
			return 0;
		break;
	case DT2821_ADCSR_ADDONE:
		if (status & DT2821_ADCSR_ADDONE)
			return 0;
		break;
	default:
		return -EINVAL;
	}
	return -EBUSY;
}

/*
 *    Performs a single A/D conversion.
 *      - Put channel/gain into channel-gain list
 *      - preload multiplexer
 *      - trigger conversion and wait for it to finish
 */
static int dt282x_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct dt282x_private *devpriv = dev->private;
	unsigned int val;
	int ret;
	int i;

	/* XXX should we really be enabling the ad clock here? */
	devpriv->adcsr = DT2821_ADCSR_ADCLK;
	outw(devpriv->adcsr, dev->iobase + DT2821_ADCSR_REG);

	dt282x_load_changain(dev, 1, &insn->chanspec);

	outw(devpriv->supcsr | DT2821_SUPCSR_PRLD,
	     dev->iobase + DT2821_SUPCSR_REG);
	ret = comedi_timeout(dev, s, insn,
			     dt282x_ai_timeout, DT2821_ADCSR_MUXBUSY);
	if (ret)
		return ret;

	for (i = 0; i < insn->n; i++) {
		outw(devpriv->supcsr | DT2821_SUPCSR_STRIG,
		     dev->iobase + DT2821_SUPCSR_REG);

		ret = comedi_timeout(dev, s, insn,
				     dt282x_ai_timeout, DT2821_ADCSR_ADDONE);
		if (ret)
			return ret;

		val = inw(dev->iobase + DT2821_ADDAT_REG);
		val &= s->maxdata;
		if (devpriv->ad_2scomp)
			val = comedi_offset_munge(s, val);

		data[i] = val;
	}

	return i;
}

static int dt282x_ai_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_cmd *cmd)
{
	const struct dt282x_board *board = comedi_board(dev);
	struct dt282x_private *devpriv = dev->private;
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_FOLLOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		/* internal trigger */
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	} else {
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	}

	err |= cfc_check_trigger_arg_min(&cmd->convert_arg, 4000);

#define SLOWEST_TIMER	(250*(1<<15)*255)
	err |= cfc_check_trigger_arg_max(&cmd->convert_arg, SLOWEST_TIMER);
	err |= cfc_check_trigger_arg_min(&cmd->convert_arg, board->ai_speed);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT) {
		/* any count is allowed */
	} else {	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	arg = cmd->convert_arg;
	devpriv->divisor = dt282x_ns_to_timer(&arg, cmd->flags);
	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, arg);

	if (err)
		return 4;

	return 0;
}

static int dt282x_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct dt282x_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int ret;

	dt282x_disable_dma(dev);

	outw(devpriv->divisor, dev->iobase + DT2821_TMRCTR_REG);

	devpriv->supcsr = DT2821_SUPCSR_ERRINTEN;
	if (cmd->scan_begin_src == TRIG_FOLLOW)
		devpriv->supcsr = DT2821_SUPCSR_DS_AD_CLK;
	else
		devpriv->supcsr = DT2821_SUPCSR_DS_AD_TRIG;
	outw(devpriv->supcsr |
	     DT2821_SUPCSR_CLRDMADNE |
	     DT2821_SUPCSR_BUFFB |
	     DT2821_SUPCSR_ADCINIT,
	     dev->iobase + DT2821_SUPCSR_REG);

	devpriv->ntrig = cmd->stop_arg * cmd->scan_end_arg;
	devpriv->nread = devpriv->ntrig;

	devpriv->dma_dir = DMA_MODE_READ;
	devpriv->current_dma_index = 0;
	dt282x_prep_ai_dma(dev, 0, 0);
	if (devpriv->ntrig) {
		dt282x_prep_ai_dma(dev, 1, 0);
		devpriv->supcsr |= DT2821_SUPCSR_DDMA;
		outw(devpriv->supcsr, dev->iobase + DT2821_SUPCSR_REG);
	}

	devpriv->adcsr = 0;

	dt282x_load_changain(dev, cmd->chanlist_len, cmd->chanlist);

	devpriv->adcsr = DT2821_ADCSR_ADCLK | DT2821_ADCSR_IADDONE;
	outw(devpriv->adcsr, dev->iobase + DT2821_ADCSR_REG);

	outw(devpriv->supcsr | DT2821_SUPCSR_PRLD,
	     dev->iobase + DT2821_SUPCSR_REG);
	ret = comedi_timeout(dev, s, NULL,
			     dt282x_ai_timeout, DT2821_ADCSR_MUXBUSY);
	if (ret)
		return ret;

	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		outw(devpriv->supcsr | DT2821_SUPCSR_STRIG,
			dev->iobase + DT2821_SUPCSR_REG);
	} else {
		devpriv->supcsr |= DT2821_SUPCSR_XTRIG;
		outw(devpriv->supcsr, dev->iobase + DT2821_SUPCSR_REG);
	}

	return 0;
}

static int dt282x_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct dt282x_private *devpriv = dev->private;

	dt282x_disable_dma(dev);

	devpriv->adcsr = 0;
	outw(devpriv->adcsr, dev->iobase + DT2821_ADCSR_REG);

	devpriv->supcsr = 0;
	outw(devpriv->supcsr | DT2821_SUPCSR_ADCINIT,
	     dev->iobase + DT2821_SUPCSR_REG);

	return 0;
}

static int dt282x_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct dt282x_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return insn->n;
}

static int dt282x_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct dt282x_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	int i;

	devpriv->dacsr |= DT2821_DACSR_SSEL | DT2821_DACSR_YSEL(chan);

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		devpriv->ao_readback[chan] = val;

		if (comedi_range_is_bipolar(s, range))
			val = comedi_offset_munge(s, val);

		outw(devpriv->dacsr, dev->iobase + DT2821_DACSR_REG);

		outw(val, dev->iobase + DT2821_DADAT_REG);

		outw(devpriv->supcsr | DT2821_SUPCSR_DACON,
		     dev->iobase + DT2821_SUPCSR_REG);
	}

	return insn->n;
}

static int dt282x_ao_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_cmd *cmd)
{
	struct dt282x_private *devpriv = dev->private;
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_INT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_TIMER);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg, 5000);
	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT) {
		/* any count is allowed */
	} else {	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	arg = cmd->scan_begin_arg;
	devpriv->divisor = dt282x_ns_to_timer(&arg, cmd->flags);
	err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, arg);

	if (err)
		return 4;

	return 0;

}

static int dt282x_ao_inttrig(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     unsigned int trig_num)
{
	struct dt282x_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int size;

	if (trig_num != cmd->start_src)
		return -EINVAL;

	size = cfc_read_array_from_buffer(s, devpriv->dma[0].buf,
					  devpriv->dma_maxsize);
	if (size == 0) {
		dev_err(dev->class_dev, "AO underrun\n");
		return -EPIPE;
	}
	dt282x_prep_ao_dma(dev, 0, size);

	size = cfc_read_array_from_buffer(s, devpriv->dma[1].buf,
					  devpriv->dma_maxsize);
	if (size == 0) {
		dev_err(dev->class_dev, "AO underrun\n");
		return -EPIPE;
	}
	dt282x_prep_ao_dma(dev, 1, size);

	outw(devpriv->supcsr | DT2821_SUPCSR_STRIG,
	     dev->iobase + DT2821_SUPCSR_REG);
	s->async->inttrig = NULL;

	return 1;
}

static int dt282x_ao_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct dt282x_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	dt282x_disable_dma(dev);

	devpriv->supcsr = DT2821_SUPCSR_ERRINTEN |
			  DT2821_SUPCSR_DS_DA_CLK |
			  DT2821_SUPCSR_DDMA;
	outw(devpriv->supcsr |
	     DT2821_SUPCSR_CLRDMADNE |
	     DT2821_SUPCSR_BUFFB |
	     DT2821_SUPCSR_DACINIT,
	     dev->iobase + DT2821_SUPCSR_REG);

	devpriv->ntrig = cmd->stop_arg * cmd->chanlist_len;
	devpriv->nread = devpriv->ntrig;

	devpriv->dma_dir = DMA_MODE_WRITE;
	devpriv->current_dma_index = 0;

	outw(devpriv->divisor, dev->iobase + DT2821_TMRCTR_REG);

	/* clear all bits but the DIO direction bits */
	devpriv->dacsr &= (DT2821_DACSR_LBOE | DT2821_DACSR_HBOE);

	devpriv->dacsr |= (DT2821_DACSR_SSEL |
			   DT2821_DACSR_DACLK |
			   DT2821_DACSR_IDARDY);
	outw(devpriv->dacsr, dev->iobase + DT2821_DACSR_REG);

	s->async->inttrig = dt282x_ao_inttrig;

	return 0;
}

static int dt282x_ao_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct dt282x_private *devpriv = dev->private;

	dt282x_disable_dma(dev);

	/* clear all bits but the DIO direction bits */
	devpriv->dacsr &= (DT2821_DACSR_LBOE | DT2821_DACSR_HBOE);

	outw(devpriv->dacsr, dev->iobase + DT2821_DACSR_REG);

	devpriv->supcsr = 0;
	outw(devpriv->supcsr | DT2821_SUPCSR_DACINIT,
	     dev->iobase + DT2821_SUPCSR_REG);

	return 0;
}

static int dt282x_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + DT2821_DIODAT_REG);

	data[1] = inw(dev->iobase + DT2821_DIODAT_REG);

	return insn->n;
}

static int dt282x_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct dt282x_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 8)
		mask = 0x00ff;
	else
		mask = 0xff00;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	devpriv->dacsr &= ~(DT2821_DACSR_LBOE | DT2821_DACSR_HBOE);
	if (s->io_bits & 0x00ff)
		devpriv->dacsr |= DT2821_DACSR_LBOE;
	if (s->io_bits & 0xff00)
		devpriv->dacsr |= DT2821_DACSR_HBOE;

	outw(devpriv->dacsr, dev->iobase + DT2821_DACSR_REG);

	return insn->n;
}

static const struct comedi_lrange *const ai_range_table[] = {
	&range_dt282x_ai_lo_bipolar,
	&range_dt282x_ai_lo_unipolar,
	&range_dt282x_ai_5_bipolar,
	&range_dt282x_ai_5_unipolar
};

static const struct comedi_lrange *const ai_range_pgl_table[] = {
	&range_dt282x_ai_hi_bipolar,
	&range_dt282x_ai_hi_unipolar
};

static const struct comedi_lrange *opt_ai_range_lkup(int ispgl, int x)
{
	if (ispgl) {
		if (x < 0 || x >= 2)
			x = 0;
		return ai_range_pgl_table[x];
	} else {
		if (x < 0 || x >= 4)
			x = 0;
		return ai_range_table[x];
	}
}

static int dt282x_grab_dma(struct comedi_device *dev, int dma1, int dma2)
{
	struct dt282x_private *devpriv = dev->private;
	int ret;

	ret = request_dma(dma1, "dt282x A");
	if (ret)
		return -EBUSY;
	devpriv->dma[0].chan = dma1;

	ret = request_dma(dma2, "dt282x B");
	if (ret)
		return -EBUSY;
	devpriv->dma[1].chan = dma2;

	devpriv->dma_maxsize = PAGE_SIZE;
	devpriv->dma[0].buf = (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
	devpriv->dma[1].buf = (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
	if (!devpriv->dma[0].buf || !devpriv->dma[1].buf)
		return -ENOMEM;

	return 0;
}

static void dt282x_free_dma(struct comedi_device *dev)
{
	struct dt282x_private *devpriv = dev->private;
	int i;

	if (!devpriv)
		return;

	for (i = 0; i < 2; i++) {
		if (devpriv->dma[i].chan)
			free_dma(devpriv->dma[i].chan);
		if (devpriv->dma[i].buf)
			free_page((unsigned long)devpriv->dma[i].buf);
		devpriv->dma[i].chan = 0;
		devpriv->dma[i].buf = NULL;
	}
}

static int dt282x_initialize(struct comedi_device *dev)
{
	/* Initialize board */
	outw(DT2821_SUPCSR_BDINIT, dev->iobase + DT2821_SUPCSR_REG);
	inw(dev->iobase + DT2821_ADCSR_REG);

	/*
	 * At power up, some registers are in a well-known state.
	 * Check them to see if a DT2821 series board is present.
	 */
	if (((inw(dev->iobase + DT2821_ADCSR_REG) & 0xfff0) != 0x7c00) ||
	    ((inw(dev->iobase + DT2821_CHANCSR_REG) & 0xf0f0) != 0x70f0) ||
	    ((inw(dev->iobase + DT2821_DACSR_REG) & 0x7c93) != 0x7c90) ||
	    ((inw(dev->iobase + DT2821_SUPCSR_REG) & 0xf8ff) != 0x0000) ||
	    ((inw(dev->iobase + DT2821_TMRCTR_REG) & 0xff00) != 0xf000)) {
		dev_err(dev->class_dev, "board not found\n");
		return -EIO;
	}
	return 0;
}

/*
   options:
   0	i/o base
   1	irq
   2	dma1
   3	dma2
   4	0=single ended, 1=differential
   5	ai 0=straight binary, 1=2's comp
   6	ao0 0=straight binary, 1=2's comp
   7	ao1 0=straight binary, 1=2's comp
   8	ai 0=±10 V, 1=0-10 V, 2=±5 V, 3=0-5 V
   9	ao0 0=±10 V, 1=0-10 V, 2=±5 V, 3=0-5 V, 4=±2.5 V
   10	ao1 0=±10 V, 1=0-10 V, 2=±5 V, 3=0-5 V, 4=±2.5 V
 */
static int dt282x_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct dt282x_board *board = comedi_board(dev);
	struct dt282x_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	ret = dt282x_initialize(dev);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	/* an IRQ and 2 DMA channels are required for async command support */
	if (it->options[1] && it->options[2] && it->options[3]) {
		unsigned int irq = it->options[1];
		unsigned int dma1 = it->options[2];
		unsigned int dma2 = it->options[3];

		if (dma2 < dma1) {
			unsigned int swap;

			swap = dma1;
			dma1 = dma2;
			dma2 = swap;
		}

		if (dma1 != dma2 &&
		    dma1 >= 5 && dma1 <= 7 &&
		    dma2 >= 5 && dma2 <= 7) {
			ret = request_irq(irq, dt282x_interrupt, 0,
					  dev->board_name, dev);
			if (ret == 0) {
				dev->irq = irq;

				ret = dt282x_grab_dma(dev, dma1, dma2);
				if (ret < 0) {
					dt282x_free_dma(dev);
					free_irq(dev->irq, dev);
					dev->irq = 0;
				}
			}
		}
	}

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE;
	if ((it->options[4] && board->adchan_di) || board->adchan_se == 0) {
		s->subdev_flags	|= SDF_DIFF;
		s->n_chan	= board->adchan_di;
	} else {
		s->subdev_flags	|= SDF_COMMON;
		s->n_chan	= board->adchan_se;
	}
	s->maxdata	= board->ai_maxdata;

	s->range_table = opt_ai_range_lkup(board->ispgl, it->options[8]);
	devpriv->ad_2scomp = it->options[5] ? 1 : 0;

	s->insn_read	= dt282x_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= s->n_chan;
		s->do_cmdtest	= dt282x_ai_cmdtest;
		s->do_cmd	= dt282x_ai_cmd;
		s->cancel	= dt282x_ai_cancel;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	if (board->dachan) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= board->dachan;
		s->maxdata	= board->ao_maxdata;

		/* ranges are per-channel, set by jumpers on the board */
		s->range_table	= &dt282x_ao_range;

		s->insn_read	= dt282x_ao_insn_read;
		s->insn_write	= dt282x_ao_insn_write;
		if (dev->irq) {
			dev->write_subdev = s;
			s->subdev_flags	|= SDF_CMD_WRITE;
			s->len_chanlist	= s->n_chan;
			s->do_cmdtest	= dt282x_ao_cmdtest;
			s->do_cmd	= dt282x_ao_cmd;
			s->cancel	= dt282x_ao_cancel;
		}
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Digital I/O subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= dt282x_dio_insn_bits;
	s->insn_config	= dt282x_dio_insn_config;

	return 0;
}

static void dt282x_detach(struct comedi_device *dev)
{
	dt282x_free_dma(dev);
	comedi_legacy_detach(dev);
}

static struct comedi_driver dt282x_driver = {
	.driver_name	= "dt282x",
	.module		= THIS_MODULE,
	.attach		= dt282x_attach,
	.detach		= dt282x_detach,
	.board_name	= &boardtypes[0].name,
	.num_names	= ARRAY_SIZE(boardtypes),
	.offset		= sizeof(struct dt282x_board),
};
module_comedi_driver(dt282x_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Data Translation DT2821 series");
MODULE_LICENSE("GPL");
