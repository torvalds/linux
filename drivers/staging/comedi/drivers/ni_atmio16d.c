/*
 * Comedi driver for National Instruments AT-MIO16D board
 * Copyright (C) 2000 Chris R. Baugher <baugher@enteract.com>
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
 * Driver: ni_atmio16d
 * Description: National Instruments AT-MIO-16D
 * Author: Chris R. Baugher <baugher@enteract.com>
 * Status: unknown
 * Devices: [National Instruments] AT-MIO-16 (atmio16), AT-MIO-16D (atmio16d)
 *
 * Configuration options:
 *   [0] - I/O port
 *   [1] - MIO irq (0 == no irq; or 3,4,5,6,7,9,10,11,12,14,15)
 *   [2] - DIO irq (0 == no irq; or 3,4,5,6,7,9)
 *   [3] - DMA1 channel (0 == no DMA; or 5,6,7)
 *   [4] - DMA2 channel (0 == no DMA; or 5,6,7)
 *   [5] - a/d mux (0=differential; 1=single)
 *   [6] - a/d range (0=bipolar10; 1=bipolar5; 2=unipolar10)
 *   [7] - dac0 range (0=bipolar; 1=unipolar)
 *   [8] - dac0 reference (0=internal; 1=external)
 *   [9] - dac0 coding (0=2's comp; 1=straight binary)
 *   [10] - dac1 range (same as dac0 options)
 *   [11] - dac1 reference (same as dac0 options)
 *   [12] - dac1 coding (same as dac0 options)
 */

/*
 * I must give credit here to Michal Dobes <dobes@tesnet.cz> who
 * wrote the driver for Advantec's pcl812 boards. I used the interrupt
 * handling code from his driver as an example for this one.
 *
 * Chris Baugher
 * 5/1/2000
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include "../comedidev.h"

#include "8255.h"

/* Configuration and Status Registers */
#define COM_REG_1	0x00	/* wo 16 */
#define STAT_REG	0x00	/* ro 16 */
#define COM_REG_2	0x02	/* wo 16 */
/* Event Strobe Registers */
#define START_CONVERT_REG	0x08	/* wo 16 */
#define START_DAQ_REG		0x0A	/* wo 16 */
#define AD_CLEAR_REG		0x0C	/* wo 16 */
#define EXT_STROBE_REG		0x0E	/* wo 16 */
/* Analog Output Registers */
#define DAC0_REG		0x10	/* wo 16 */
#define DAC1_REG		0x12	/* wo 16 */
#define INT2CLR_REG		0x14	/* wo 16 */
/* Analog Input Registers */
#define MUX_CNTR_REG		0x04	/* wo 16 */
#define MUX_GAIN_REG		0x06	/* wo 16 */
#define AD_FIFO_REG		0x16	/* ro 16 */
#define DMA_TC_INT_CLR_REG	0x16	/* wo 16 */
/* AM9513A Counter/Timer Registers */
#define AM9513A_DATA_REG	0x18	/* rw 16 */
#define AM9513A_COM_REG		0x1A	/* wo 16 */
#define AM9513A_STAT_REG	0x1A	/* ro 16 */
/* MIO-16 Digital I/O Registers */
#define MIO_16_DIG_IN_REG	0x1C	/* ro 16 */
#define MIO_16_DIG_OUT_REG	0x1C	/* wo 16 */
/* RTSI Switch Registers */
#define RTSI_SW_SHIFT_REG	0x1E	/* wo 8 */
#define RTSI_SW_STROBE_REG	0x1F	/* wo 8 */
/* DIO-24 Registers */
#define DIO_24_PORTA_REG	0x00	/* rw 8 */
#define DIO_24_PORTB_REG	0x01	/* rw 8 */
#define DIO_24_PORTC_REG	0x02	/* rw 8 */
#define DIO_24_CNFG_REG		0x03	/* wo 8 */

/* Command Register bits */
#define COMREG1_2SCADC		0x0001
#define COMREG1_1632CNT		0x0002
#define COMREG1_SCANEN		0x0008
#define COMREG1_DAQEN		0x0010
#define COMREG1_DMAEN		0x0020
#define COMREG1_CONVINTEN	0x0080
#define COMREG2_SCN2		0x0010
#define COMREG2_INTEN		0x0080
#define COMREG2_DOUTEN0		0x0100
#define COMREG2_DOUTEN1		0x0200
/* Status Register bits */
#define STAT_AD_OVERRUN		0x0100
#define STAT_AD_OVERFLOW	0x0200
#define STAT_AD_DAQPROG		0x0800
#define STAT_AD_CONVAVAIL	0x2000
#define STAT_AD_DAQSTOPINT	0x4000
/* AM9513A Counter/Timer defines */
#define CLOCK_1_MHZ		0x8B25
#define CLOCK_100_KHZ	0x8C25
#define CLOCK_10_KHZ	0x8D25
#define CLOCK_1_KHZ		0x8E25
#define CLOCK_100_HZ	0x8F25

struct atmio16_board_t {
	const char *name;
	int has_8255;
};

/* range structs */
static const struct comedi_lrange range_atmio16d_ai_10_bipolar = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(1),
		BIP_RANGE(0.1),
		BIP_RANGE(0.02)
	}
};

static const struct comedi_lrange range_atmio16d_ai_5_bipolar = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05),
		BIP_RANGE(0.01)
	}
};

static const struct comedi_lrange range_atmio16d_ai_unipolar = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(1),
		UNI_RANGE(0.1),
		UNI_RANGE(0.02)
	}
};

/* private data struct */
struct atmio16d_private {
	enum { adc_diff, adc_singleended } adc_mux;
	enum { adc_bipolar10, adc_bipolar5, adc_unipolar10 } adc_range;
	enum { adc_2comp, adc_straight } adc_coding;
	enum { dac_bipolar, dac_unipolar } dac0_range, dac1_range;
	enum { dac_internal, dac_external } dac0_reference, dac1_reference;
	enum { dac_2comp, dac_straight } dac0_coding, dac1_coding;
	const struct comedi_lrange *ao_range_type_list[2];
	unsigned int com_reg_1_state; /* current state of command register 1 */
	unsigned int com_reg_2_state; /* current state of command register 2 */
};

static void reset_counters(struct comedi_device *dev)
{
	/* Counter 2 */
	outw(0xFFC2, dev->iobase + AM9513A_COM_REG);
	outw(0xFF02, dev->iobase + AM9513A_COM_REG);
	outw(0x4, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF0A, dev->iobase + AM9513A_COM_REG);
	outw(0x3, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF42, dev->iobase + AM9513A_COM_REG);
	outw(0xFF42, dev->iobase + AM9513A_COM_REG);
	/* Counter 3 */
	outw(0xFFC4, dev->iobase + AM9513A_COM_REG);
	outw(0xFF03, dev->iobase + AM9513A_COM_REG);
	outw(0x4, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF0B, dev->iobase + AM9513A_COM_REG);
	outw(0x3, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF44, dev->iobase + AM9513A_COM_REG);
	outw(0xFF44, dev->iobase + AM9513A_COM_REG);
	/* Counter 4 */
	outw(0xFFC8, dev->iobase + AM9513A_COM_REG);
	outw(0xFF04, dev->iobase + AM9513A_COM_REG);
	outw(0x4, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF0C, dev->iobase + AM9513A_COM_REG);
	outw(0x3, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF48, dev->iobase + AM9513A_COM_REG);
	outw(0xFF48, dev->iobase + AM9513A_COM_REG);
	/* Counter 5 */
	outw(0xFFD0, dev->iobase + AM9513A_COM_REG);
	outw(0xFF05, dev->iobase + AM9513A_COM_REG);
	outw(0x4, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF0D, dev->iobase + AM9513A_COM_REG);
	outw(0x3, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF50, dev->iobase + AM9513A_COM_REG);
	outw(0xFF50, dev->iobase + AM9513A_COM_REG);

	outw(0, dev->iobase + AD_CLEAR_REG);
}

static void reset_atmio16d(struct comedi_device *dev)
{
	struct atmio16d_private *devpriv = dev->private;
	int i;

	/* now we need to initialize the board */
	outw(0, dev->iobase + COM_REG_1);
	outw(0, dev->iobase + COM_REG_2);
	outw(0, dev->iobase + MUX_GAIN_REG);
	/* init AM9513A timer */
	outw(0xFFFF, dev->iobase + AM9513A_COM_REG);
	outw(0xFFEF, dev->iobase + AM9513A_COM_REG);
	outw(0xFF17, dev->iobase + AM9513A_COM_REG);
	outw(0xF000, dev->iobase + AM9513A_DATA_REG);
	for (i = 1; i <= 5; ++i) {
		outw(0xFF00 + i, dev->iobase + AM9513A_COM_REG);
		outw(0x0004, dev->iobase + AM9513A_DATA_REG);
		outw(0xFF08 + i, dev->iobase + AM9513A_COM_REG);
		outw(0x3, dev->iobase + AM9513A_DATA_REG);
	}
	outw(0xFF5F, dev->iobase + AM9513A_COM_REG);
	/* timer init done */
	outw(0, dev->iobase + AD_CLEAR_REG);
	outw(0, dev->iobase + INT2CLR_REG);
	/* select straight binary mode for Analog Input */
	devpriv->com_reg_1_state |= 1;
	outw(devpriv->com_reg_1_state, dev->iobase + COM_REG_1);
	devpriv->adc_coding = adc_straight;
	/* zero the analog outputs */
	outw(2048, dev->iobase + DAC0_REG);
	outw(2048, dev->iobase + DAC1_REG);
}

static irqreturn_t atmio16d_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned short val;

	val = inw(dev->iobase + AD_FIFO_REG);
	comedi_buf_write_samples(s, &val, 1);
	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static int atmio16d_ai_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_FOLLOW | TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		/* internal trigger */
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	} else {
#if 0
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
#endif
	}

	err |= comedi_check_trigger_arg_min(&cmd->convert_arg, 10000);
#if 0
	err |= comedi_check_trigger_arg_max(&cmd->convert_arg, SLOWEST_TIMER);
#endif

	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	return 0;
}

static int atmio16d_ai_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct atmio16d_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int timer, base_clock;
	unsigned int sample_count, tmp, chan, gain;
	int i;

	/*
	 * This is slowly becoming a working command interface.
	 * It is still uber-experimental
	 */

	reset_counters(dev);

	/* check if scanning multiple channels */
	if (cmd->chanlist_len < 2) {
		devpriv->com_reg_1_state &= ~COMREG1_SCANEN;
		outw(devpriv->com_reg_1_state, dev->iobase + COM_REG_1);
	} else {
		devpriv->com_reg_1_state |= COMREG1_SCANEN;
		devpriv->com_reg_2_state |= COMREG2_SCN2;
		outw(devpriv->com_reg_1_state, dev->iobase + COM_REG_1);
		outw(devpriv->com_reg_2_state, dev->iobase + COM_REG_2);
	}

	/* Setup the Mux-Gain Counter */
	for (i = 0; i < cmd->chanlist_len; ++i) {
		chan = CR_CHAN(cmd->chanlist[i]);
		gain = CR_RANGE(cmd->chanlist[i]);
		outw(i, dev->iobase + MUX_CNTR_REG);
		tmp = chan | (gain << 6);
		if (i == cmd->scan_end_arg - 1)
			tmp |= 0x0010;	/* set LASTONE bit */
		outw(tmp, dev->iobase + MUX_GAIN_REG);
	}

	/*
	 * Now program the sample interval timer.
	 * Figure out which clock to use then get an appropriate timer value.
	 */
	if (cmd->convert_arg < 65536000) {
		base_clock = CLOCK_1_MHZ;
		timer = cmd->convert_arg / 1000;
	} else if (cmd->convert_arg < 655360000) {
		base_clock = CLOCK_100_KHZ;
		timer = cmd->convert_arg / 10000;
	} else /* cmd->convert_arg < 6553600000 */ {
		base_clock = CLOCK_10_KHZ;
		timer = cmd->convert_arg / 100000;
	}
	outw(0xFF03, dev->iobase + AM9513A_COM_REG);
	outw(base_clock, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF0B, dev->iobase + AM9513A_COM_REG);
	outw(0x2, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF44, dev->iobase + AM9513A_COM_REG);
	outw(0xFFF3, dev->iobase + AM9513A_COM_REG);
	outw(timer, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF24, dev->iobase + AM9513A_COM_REG);

	/* Now figure out how many samples to get */
	/* and program the sample counter */
	sample_count = cmd->stop_arg * cmd->scan_end_arg;
	outw(0xFF04, dev->iobase + AM9513A_COM_REG);
	outw(0x1025, dev->iobase + AM9513A_DATA_REG);
	outw(0xFF0C, dev->iobase + AM9513A_COM_REG);
	if (sample_count < 65536) {
		/* use only Counter 4 */
		outw(sample_count, dev->iobase + AM9513A_DATA_REG);
		outw(0xFF48, dev->iobase + AM9513A_COM_REG);
		outw(0xFFF4, dev->iobase + AM9513A_COM_REG);
		outw(0xFF28, dev->iobase + AM9513A_COM_REG);
		devpriv->com_reg_1_state &= ~COMREG1_1632CNT;
		outw(devpriv->com_reg_1_state, dev->iobase + COM_REG_1);
	} else {
		/* Counter 4 and 5 are needed */

		tmp = sample_count & 0xFFFF;
		if (tmp)
			outw(tmp - 1, dev->iobase + AM9513A_DATA_REG);
		else
			outw(0xFFFF, dev->iobase + AM9513A_DATA_REG);

		outw(0xFF48, dev->iobase + AM9513A_COM_REG);
		outw(0, dev->iobase + AM9513A_DATA_REG);
		outw(0xFF28, dev->iobase + AM9513A_COM_REG);
		outw(0xFF05, dev->iobase + AM9513A_COM_REG);
		outw(0x25, dev->iobase + AM9513A_DATA_REG);
		outw(0xFF0D, dev->iobase + AM9513A_COM_REG);
		tmp = sample_count & 0xFFFF;
		if ((tmp == 0) || (tmp == 1)) {
			outw((sample_count >> 16) & 0xFFFF,
			     dev->iobase + AM9513A_DATA_REG);
		} else {
			outw(((sample_count >> 16) & 0xFFFF) + 1,
			     dev->iobase + AM9513A_DATA_REG);
		}
		outw(0xFF70, dev->iobase + AM9513A_COM_REG);
		devpriv->com_reg_1_state |= COMREG1_1632CNT;
		outw(devpriv->com_reg_1_state, dev->iobase + COM_REG_1);
	}

	/*
	 * Program the scan interval timer ONLY IF SCANNING IS ENABLED.
	 * Figure out which clock to use then get an appropriate timer value.
	 */
	if (cmd->chanlist_len > 1) {
		if (cmd->scan_begin_arg < 65536000) {
			base_clock = CLOCK_1_MHZ;
			timer = cmd->scan_begin_arg / 1000;
		} else if (cmd->scan_begin_arg < 655360000) {
			base_clock = CLOCK_100_KHZ;
			timer = cmd->scan_begin_arg / 10000;
		} else /* cmd->scan_begin_arg < 6553600000 */ {
			base_clock = CLOCK_10_KHZ;
			timer = cmd->scan_begin_arg / 100000;
		}
		outw(0xFF02, dev->iobase + AM9513A_COM_REG);
		outw(base_clock, dev->iobase + AM9513A_DATA_REG);
		outw(0xFF0A, dev->iobase + AM9513A_COM_REG);
		outw(0x2, dev->iobase + AM9513A_DATA_REG);
		outw(0xFF42, dev->iobase + AM9513A_COM_REG);
		outw(0xFFF2, dev->iobase + AM9513A_COM_REG);
		outw(timer, dev->iobase + AM9513A_DATA_REG);
		outw(0xFF22, dev->iobase + AM9513A_COM_REG);
	}

	/* Clear the A/D FIFO and reset the MUX counter */
	outw(0, dev->iobase + AD_CLEAR_REG);
	outw(0, dev->iobase + MUX_CNTR_REG);
	outw(0, dev->iobase + INT2CLR_REG);
	/* enable this acquisition operation */
	devpriv->com_reg_1_state |= COMREG1_DAQEN;
	outw(devpriv->com_reg_1_state, dev->iobase + COM_REG_1);
	/* enable interrupts for conversion completion */
	devpriv->com_reg_1_state |= COMREG1_CONVINTEN;
	devpriv->com_reg_2_state |= COMREG2_INTEN;
	outw(devpriv->com_reg_1_state, dev->iobase + COM_REG_1);
	outw(devpriv->com_reg_2_state, dev->iobase + COM_REG_2);
	/* apply a trigger. this starts the counters! */
	outw(0, dev->iobase + START_DAQ_REG);

	return 0;
}

/* This will cancel a running acquisition operation */
static int atmio16d_ai_cancel(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	reset_atmio16d(dev);

	return 0;
}

static int atmio16d_ai_eoc(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + STAT_REG);
	if (status & STAT_AD_CONVAVAIL)
		return 0;
	if (status & STAT_AD_OVERFLOW) {
		outw(0, dev->iobase + AD_CLEAR_REG);
		return -EOVERFLOW;
	}
	return -EBUSY;
}

static int atmio16d_ai_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	struct atmio16d_private *devpriv = dev->private;
	int i;
	int chan;
	int gain;
	int ret;

	chan = CR_CHAN(insn->chanspec);
	gain = CR_RANGE(insn->chanspec);

	/* reset the Analog input circuitry */
	/* outw( 0, dev->iobase+AD_CLEAR_REG ); */
	/* reset the Analog Input MUX Counter to 0 */
	/* outw( 0, dev->iobase+MUX_CNTR_REG ); */

	/* set the Input MUX gain */
	outw(chan | (gain << 6), dev->iobase + MUX_GAIN_REG);

	for (i = 0; i < insn->n; i++) {
		/* start the conversion */
		outw(0, dev->iobase + START_CONVERT_REG);

		/* wait for it to finish */
		ret = comedi_timeout(dev, s, insn, atmio16d_ai_eoc, 0);
		if (ret)
			return ret;

		/* read the data now */
		data[i] = inw(dev->iobase + AD_FIFO_REG);
		/* change to two's complement if need be */
		if (devpriv->adc_coding == adc_2comp)
			data[i] ^= 0x800;
	}

	return i;
}

static int atmio16d_ao_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct atmio16d_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int reg = (chan) ? DAC1_REG : DAC0_REG;
	bool munge = false;
	int i;

	if (chan == 0 && devpriv->dac0_coding == dac_2comp)
		munge = true;
	if (chan == 1 && devpriv->dac1_coding == dac_2comp)
		munge = true;

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		s->readback[chan] = val;

		if (munge)
			val ^= 0x800;

		outw(val, dev->iobase + reg);
	}

	return insn->n;
}

static int atmio16d_dio_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + MIO_16_DIG_OUT_REG);

	data[1] = inw(dev->iobase + MIO_16_DIG_IN_REG);

	return insn->n;
}

static int atmio16d_dio_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct atmio16d_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 4)
		mask = 0x0f;
	else
		mask = 0xf0;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	devpriv->com_reg_2_state &= ~(COMREG2_DOUTEN0 | COMREG2_DOUTEN1);
	if (s->io_bits & 0x0f)
		devpriv->com_reg_2_state |= COMREG2_DOUTEN0;
	if (s->io_bits & 0xf0)
		devpriv->com_reg_2_state |= COMREG2_DOUTEN1;
	outw(devpriv->com_reg_2_state, dev->iobase + COM_REG_2);

	return insn->n;
}

static int atmio16d_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	const struct atmio16_board_t *board = dev->board_ptr;
	struct atmio16d_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x20);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	/* reset the atmio16d hardware */
	reset_atmio16d(dev);

	if (it->options[1]) {
		ret = request_irq(it->options[1], atmio16d_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	/* set device options */
	devpriv->adc_mux = it->options[5];
	devpriv->adc_range = it->options[6];

	devpriv->dac0_range = it->options[7];
	devpriv->dac0_reference = it->options[8];
	devpriv->dac0_coding = it->options[9];
	devpriv->dac1_range = it->options[10];
	devpriv->dac1_reference = it->options[11];
	devpriv->dac1_coding = it->options[12];

	/* setup sub-devices */
	s = &dev->subdevices[0];
	/* ai subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = (devpriv->adc_mux ? 16 : 8);
	s->insn_read = atmio16d_ai_insn_read;
	s->maxdata = 0xfff;	/* 4095 decimal */
	switch (devpriv->adc_range) {
	case adc_bipolar10:
		s->range_table = &range_atmio16d_ai_10_bipolar;
		break;
	case adc_bipolar5:
		s->range_table = &range_atmio16d_ai_5_bipolar;
		break;
	case adc_unipolar10:
		s->range_table = &range_atmio16d_ai_unipolar;
		break;
	}
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags |= SDF_CMD_READ;
		s->len_chanlist = 16;
		s->do_cmdtest = atmio16d_ai_cmdtest;
		s->do_cmd = atmio16d_ai_cmd;
		s->cancel = atmio16d_ai_cancel;
	}

	/* ao subdevice */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 2;
	s->maxdata = 0xfff;	/* 4095 decimal */
	s->range_table_list = devpriv->ao_range_type_list;
	switch (devpriv->dac0_range) {
	case dac_bipolar:
		devpriv->ao_range_type_list[0] = &range_bipolar10;
		break;
	case dac_unipolar:
		devpriv->ao_range_type_list[0] = &range_unipolar10;
		break;
	}
	switch (devpriv->dac1_range) {
	case dac_bipolar:
		devpriv->ao_range_type_list[1] = &range_bipolar10;
		break;
	case dac_unipolar:
		devpriv->ao_range_type_list[1] = &range_unipolar10;
		break;
	}
	s->insn_write = atmio16d_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital I/O */
	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 8;
	s->insn_bits = atmio16d_dio_insn_bits;
	s->insn_config = atmio16d_dio_insn_config;
	s->maxdata = 1;
	s->range_table = &range_digital;

	/* 8255 subdevice */
	s = &dev->subdevices[3];
	if (board->has_8255) {
		ret = subdev_8255_init(dev, s, NULL, 0x00);
		if (ret)
			return ret;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

/* don't yet know how to deal with counter/timers */
#if 0
	s = &dev->subdevices[4];
	/* do */
	s->type = COMEDI_SUBD_TIMER;
	s->n_chan = 0;
	s->maxdata = 0
#endif

	return 0;
}

static void atmio16d_detach(struct comedi_device *dev)
{
	reset_atmio16d(dev);
	comedi_legacy_detach(dev);
}

static const struct atmio16_board_t atmio16_boards[] = {
	{
		.name		= "atmio16",
		.has_8255	= 0,
	}, {
		.name		= "atmio16d",
		.has_8255	= 1,
	},
};

static struct comedi_driver atmio16d_driver = {
	.driver_name	= "atmio16",
	.module		= THIS_MODULE,
	.attach		= atmio16d_attach,
	.detach		= atmio16d_detach,
	.board_name	= &atmio16_boards[0].name,
	.num_names	= ARRAY_SIZE(atmio16_boards),
	.offset		= sizeof(struct atmio16_board_t),
};
module_comedi_driver(atmio16d_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
