/*
   comedi/drivers/ni_atmio16d.c
   Hardware driver for National Instruments AT-MIO16D board
   Copyright (C) 2000 Chris R. Baugher <baugher@enteract.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 */
/*
Driver: ni_atmio16d
Description: National Instruments AT-MIO-16D
Author: Chris R. Baugher <baugher@enteract.com>
Status: unknown
Devices: [National Instruments] AT-MIO-16 (atmio16), AT-MIO-16D (atmio16d)
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

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/ioport.h>

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
/* Other miscellaneous defines */
#define ATMIO16D_SIZE	32	/* bus address range */
#define devpriv ((struct atmio16d_private *)dev->private)
#define ATMIO16D_TIMEOUT 10

struct atmio16_board_t {

	const char *name;
	int has_8255;
};

/* range structs */
static const struct comedi_lrange range_atmio16d_ai_10_bipolar = { 4, {
								       BIP_RANGE
								       (10),
								       BIP_RANGE
								       (1),
								       BIP_RANGE
								       (0.1),
								       BIP_RANGE
								       (0.02)
								       }
};

static const struct comedi_lrange range_atmio16d_ai_5_bipolar = { 4, {
								      BIP_RANGE
								      (5),
								      BIP_RANGE
								      (0.5),
								      BIP_RANGE
								      (0.05),
								      BIP_RANGE
								      (0.01)
								      }
};

static const struct comedi_lrange range_atmio16d_ai_unipolar = { 4, {
								     UNI_RANGE
								     (10),
								     UNI_RANGE
								     (1),
								     UNI_RANGE
								     (0.1),
								     UNI_RANGE
								     (0.02)
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
	unsigned int ao_readback[2];
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
	struct comedi_subdevice *s = &dev->subdevices[0];

	comedi_buf_put(s->async, inw(dev->iobase + AD_FIFO_REG));

	comedi_event(dev, s);
	return IRQ_HANDLED;
}

static int atmio16d_ai_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0, tmp;

	/* make sure triggers are valid */
	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_FOLLOW | TRIG_TIMER;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique & mutually compatible */
	/* note that mutual compatibility is not an issue here */
	if (cmd->scan_begin_src != TRIG_FOLLOW &&
	    cmd->scan_begin_src != TRIG_EXT &&
	    cmd->scan_begin_src != TRIG_TIMER)
		err++;
	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		/* internal trigger */
		if (cmd->scan_begin_arg != 0) {
			cmd->scan_begin_arg = 0;
			err++;
		}
	} else {
#if 0
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		if (cmd->scan_begin_arg != 0) {
			cmd->scan_begin_arg = 0;
			err++;
		}
#endif
	}

	if (cmd->convert_arg < 10000) {
		cmd->convert_arg = 10000;
		err++;
	}
#if 0
	if (cmd->convert_arg > SLOWEST_TIMER) {
		cmd->convert_arg = SLOWEST_TIMER;
		err++;
	}
#endif
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		/* any count is allowed */
	} else {
		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	if (err)
		return 3;

	return 0;
}

static int atmio16d_ai_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int timer, base_clock;
	unsigned int sample_count, tmp, chan, gain;
	int i;

	/* This is slowly becoming a working command interface. *
	 * It is still uber-experimental */

	reset_counters(dev);
	s->async->cur_chan = 0;

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

	/* Now program the sample interval timer */
	/* Figure out which clock to use then get an
	 * appropriate timer value */
	if (cmd->convert_arg < 65536000) {
		base_clock = CLOCK_1_MHZ;
		timer = cmd->convert_arg / 1000;
	} else if (cmd->convert_arg < 655360000) {
		base_clock = CLOCK_100_KHZ;
		timer = cmd->convert_arg / 10000;
	} else if (cmd->convert_arg <= 0xffffffff /* 6553600000 */) {
		base_clock = CLOCK_10_KHZ;
		timer = cmd->convert_arg / 100000;
	} else if (cmd->convert_arg <= 0xffffffff /* 65536000000 */) {
		base_clock = CLOCK_1_KHZ;
		timer = cmd->convert_arg / 1000000;
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

	/* Program the scan interval timer ONLY IF SCANNING IS ENABLED */
	/* Figure out which clock to use then get an
	 * appropriate timer value */
	if (cmd->chanlist_len > 1) {
		if (cmd->scan_begin_arg < 65536000) {
			base_clock = CLOCK_1_MHZ;
			timer = cmd->scan_begin_arg / 1000;
		} else if (cmd->scan_begin_arg < 655360000) {
			base_clock = CLOCK_100_KHZ;
			timer = cmd->scan_begin_arg / 10000;
		} else if (cmd->scan_begin_arg < 0xffffffff /* 6553600000 */) {
			base_clock = CLOCK_10_KHZ;
			timer = cmd->scan_begin_arg / 100000;
		} else if (cmd->scan_begin_arg < 0xffffffff /* 65536000000 */) {
			base_clock = CLOCK_1_KHZ;
			timer = cmd->scan_begin_arg / 1000000;
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

/* Mode 0 is used to get a single conversion on demand */
static int atmio16d_ai_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	int i, t;
	int chan;
	int gain;
	int status;

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
		for (t = 0; t < ATMIO16D_TIMEOUT; t++) {
			/* check conversion status */
			status = inw(dev->iobase + STAT_REG);
			if (status & STAT_AD_CONVAVAIL) {
				/* read the data now */
				data[i] = inw(dev->iobase + AD_FIFO_REG);
				/* change to two's complement if need be */
				if (devpriv->adc_coding == adc_2comp)
					data[i] ^= 0x800;
				break;
			}
			if (status & STAT_AD_OVERFLOW) {
				printk(KERN_INFO "atmio16d: a/d FIFO overflow\n");
				outw(0, dev->iobase + AD_CLEAR_REG);

				return -ETIME;
			}
		}
		/* end waiting, now check if it timed out */
		if (t == ATMIO16D_TIMEOUT) {
			printk(KERN_INFO "atmio16d: timeout\n");

			return -ETIME;
		}
	}

	return i;
}

static int atmio16d_ao_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[CR_CHAN(insn->chanspec)];
	return i;
}

static int atmio16d_ao_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan;
	int d;

	chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++) {
		d = data[i];
		switch (chan) {
		case 0:
			if (devpriv->dac0_coding == dac_2comp)
				d ^= 0x800;
			outw(d, dev->iobase + DAC0_REG);
			break;
		case 1:
			if (devpriv->dac1_coding == dac_2comp)
				d ^= 0x800;
			outw(d, dev->iobase + DAC1_REG);
			break;
		default:
			return -EINVAL;
		}
		devpriv->ao_readback[chan] = data[i];
	}
	return i;
}

static int atmio16d_dio_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] | data[1]);
		outw(s->state, dev->iobase + MIO_16_DIG_OUT_REG);
	}
	data[1] = inw(dev->iobase + MIO_16_DIG_IN_REG);

	return insn->n;
}

static int atmio16d_dio_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	int i;
	int mask;

	for (i = 0; i < insn->n; i++) {
		mask = (CR_CHAN(insn->chanspec) < 4) ? 0x0f : 0xf0;
		s->io_bits &= ~mask;
		if (data[i])
			s->io_bits |= mask;
	}
	devpriv->com_reg_2_state &= ~(COMREG2_DOUTEN0 | COMREG2_DOUTEN1);
	if (s->io_bits & 0x0f)
		devpriv->com_reg_2_state |= COMREG2_DOUTEN0;
	if (s->io_bits & 0xf0)
		devpriv->com_reg_2_state |= COMREG2_DOUTEN1;
	outw(devpriv->com_reg_2_state, dev->iobase + COM_REG_2);

	return i;
}

/*
   options[0] - I/O port
   options[1] - MIO irq
		0 == no irq
		N == irq N {3,4,5,6,7,9,10,11,12,14,15}
   options[2] - DIO irq
		0 == no irq
		N == irq N {3,4,5,6,7,9}
   options[3] - DMA1 channel
		0 == no DMA
		N == DMA N {5,6,7}
   options[4] - DMA2 channel
		0 == no DMA
		N == DMA N {5,6,7}

   options[5] - a/d mux
	0=differential, 1=single
   options[6] - a/d range
	0=bipolar10, 1=bipolar5, 2=unipolar10

   options[7] - dac0 range
	0=bipolar, 1=unipolar
   options[8] - dac0 reference
	0=internal, 1=external
   options[9] - dac0 coding
	0=2's comp, 1=straight binary

   options[10] - dac1 range
   options[11] - dac1 reference
   options[12] - dac1 coding
 */

static int atmio16d_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	const struct atmio16_board_t *board = comedi_board(dev);
	unsigned int irq;
	unsigned long iobase;
	int ret;

	struct comedi_subdevice *s;

	/* make sure the address range is free and allocate it */
	iobase = it->options[0];
	printk(KERN_INFO "comedi%d: atmio16d: 0x%04lx ", dev->minor, iobase);
	if (!request_region(iobase, ATMIO16D_SIZE, "ni_atmio16d")) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;

	dev->board_name = board->name;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	ret = alloc_private(dev, sizeof(struct atmio16d_private));
	if (ret < 0)
		return ret;

	/* reset the atmio16d hardware */
	reset_atmio16d(dev);

	/* check if our interrupt is available and get it */
	irq = it->options[1];
	if (irq) {

		ret = request_irq(irq, atmio16d_interrupt, 0, "atmio16d", dev);
		if (ret < 0) {
			printk(KERN_INFO "failed to allocate irq %u\n", irq);
			return ret;
		}
		dev->irq = irq;
		printk(KERN_INFO "( irq = %u )\n", irq);
	} else {
		printk(KERN_INFO "( no irq )");
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
	dev->read_subdev = s;
	/* ai subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_CMD_READ;
	s->n_chan = (devpriv->adc_mux ? 16 : 8);
	s->len_chanlist = 16;
	s->insn_read = atmio16d_ai_insn_read;
	s->do_cmdtest = atmio16d_ai_cmdtest;
	s->do_cmd = atmio16d_ai_cmd;
	s->cancel = atmio16d_ai_cancel;
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

	/* ao subdevice */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 2;
	s->insn_read = atmio16d_ao_insn_read;
	s->insn_write = atmio16d_ao_insn_write;
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
	if (board->has_8255)
		subdev_8255_init(dev, s, NULL, dev->iobase);
	else
		s->type = COMEDI_SUBD_UNUSED;

/* don't yet know how to deal with counter/timers */
#if 0
	s = &dev->subdevices[4];
	/* do */
	s->type = COMEDI_SUBD_TIMER;
	s->n_chan = 0;
	s->maxdata = 0
#endif
	    printk("\n");

	return 0;
}

static void atmio16d_detach(struct comedi_device *dev)
{
	const struct atmio16_board_t *board = comedi_board(dev);
	struct comedi_subdevice *s;

	if (dev->subdevices && board->has_8255) {
		s = &dev->subdevices[3];
		subdev_8255_cleanup(dev, s);
	}
	if (dev->irq)
		free_irq(dev->irq, dev);
	reset_atmio16d(dev);
	if (dev->iobase)
		release_region(dev->iobase, ATMIO16D_SIZE);
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
