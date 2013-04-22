/*
 * comedi/drivers/ni_labpc.c
 * Driver for National Instruments Lab-PC series boards and compatibles
 * Copyright (C) 2001-2003 Frank Mori Hess <fmhess@users.sourceforge.net>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Driver: ni_labpc
 * Description: National Instruments Lab-PC (& compatibles)
 * Devices: (National Instruments) Lab-PC-1200 [lab-pc-1200]
 *	    (National Instruments) Lab-PC-1200AI [lab-pc-1200ai]
 *	    (National Instruments) Lab-PC+ [lab-pc+]
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Status: works
 *
 * Configuration options - ISA boards:
 *   [0] - I/O port base address
 *   [1] - IRQ (optional, required for timed or externally triggered
 *		conversions)
 *   [2] - DMA channel (optional)
 *
 * Tested with lab-pc-1200.  For the older Lab-PC+, not all input
 * ranges and analog references will work, the available ranges/arefs
 * will depend on how you have configured the jumpers on your board
 * (see your owner's manual).
 *
 * Kernel-level ISA plug-and-play support for the lab-pc-1200 boards
 * has not yet been added to the driver, mainly due to the fact that
 * I don't know the device id numbers. If you have one of these boards,
 * please file a bug report at http://comedi.org/ so I can get the
 * necessary information from you.
 *
 * The 1200 series boards have onboard calibration dacs for correcting
 * analog input/output offsets and gains. The proper settings for these
 * caldacs are stored on the board's eeprom. To read the caldac values
 * from the eeprom and store them into a file that can be then be used
 * by comedilib, use the comedi_calibrate program.
 *
 * The Lab-pc+ has quirky chanlist requirements when scanning multiple
 * channels. Multiple channel scan sequence must start at highest channel,
 * then decrement down to channel 0. The rest of the cards can scan down
 * like lab-pc+ or scan up from channel zero. Chanlists consisting of all
 * one channel are also legal, and allow you to pace conversions in bursts.
 *
 * NI manuals:
 * 341309a (labpc-1200 register manual)
 * 320502b (lab-pc+)
 */

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "../comedidev.h"

#include <asm/dma.h>

#include "8253.h"
#include "8255.h"
#include "comedi_fc.h"
#include "ni_labpc.h"

/*
 * Register map (all registers are 8-bit)
 */
#define STAT1_REG		0x00	/* R: Status 1 reg */
#define STAT1_DAVAIL		(1 << 0)
#define STAT1_OVERRUN		(1 << 1)
#define STAT1_OVERFLOW		(1 << 2)
#define STAT1_CNTINT		(1 << 3)
#define STAT1_GATA0		(1 << 5)
#define STAT1_EXTGATA0		(1 << 6)
#define CMD1_REG		0x00	/* W: Command 1 reg */
#define CMD1_MA(x)		(((x) & 0x7) << 0)
#define CMD1_TWOSCMP		(1 << 3)
#define CMD1_GAIN_MASK		(7 << 4)
#define CMD1_SCANEN		(1 << 7)
#define CMD2_REG		0x01	/* W: Command 2 reg */
#define CMD2_PRETRIG		(1 << 0)
#define CMD2_HWTRIG		(1 << 1)
#define CMD2_SWTRIG		(1 << 2)
#define CMD2_TBSEL		(1 << 3)
#define CMD2_2SDAC0		(1 << 4)
#define CMD2_2SDAC1		(1 << 5)
#define CMD2_LDAC(x)		(1 << (6 + (x)))
#define CMD3_REG		0x02	/* W: Command 3 reg */
#define CMD3_DMAEN		(1 << 0)
#define CMD3_DIOINTEN		(1 << 1)
#define CMD3_DMATCINTEN		(1 << 2)
#define CMD3_CNTINTEN		(1 << 3)
#define CMD3_ERRINTEN		(1 << 4)
#define CMD3_FIFOINTEN		(1 << 5)
#define ADC_START_CONVERT_REG	0x03	/* W: Start Convert reg */
#define DAC_LSB_REG(x)		(0x04 + 2 * (x)) /* W: DAC0/1 LSB reg */
#define DAC_MSB_REG(x)		(0x05 + 2 * (x)) /* W: DAC0/1 MSB reg */
#define ADC_FIFO_CLEAR_REG	0x08	/* W: A/D FIFO Clear reg */
#define ADC_FIFO_REG		0x0a	/* R: A/D FIFO reg */
#define DMATC_CLEAR_REG		0x0a	/* W: DMA Interrupt Clear reg */
#define TIMER_CLEAR_REG		0x0c	/* W: Timer Interrupt Clear reg */
#define CMD6_REG		0x0e	/* W: Command 6 reg */
#define CMD6_NRSE		(1 << 0)
#define CMD6_ADCUNI		(1 << 1)
#define CMD6_DACUNI(x)		(1 << (2 + (x)))
#define CMD6_HFINTEN		(1 << 5)
#define CMD6_DQINTEN		(1 << 6)
#define CMD6_SCANUP		(1 << 7)
#define CMD4_REG		0x0f	/* W: Command 3 reg */
#define CMD4_INTSCAN		(1 << 0)
#define CMD4_EOIRCV		(1 << 1)
#define CMD4_ECLKDRV		(1 << 2)
#define CMD4_SEDIFF		(1 << 3)
#define CMD4_ECLKRCV		(1 << 4)
#define DIO_BASE_REG		0x10	/* R/W: 8255 DIO base reg */
#define COUNTER_A_BASE_REG	0x14	/* R/W: 8253 Counter A base reg */
#define COUNTER_B_BASE_REG	0x18	/* R/W: 8253 Counter B base reg */
#define CMD5_REG		0x1c	/* W: Command 5 reg */
#define CMD5_WRTPRT		(1 << 2)
#define CMD5_DITHEREN		(1 << 3)
#define CMD5_CALDACLD		(1 << 4)
#define CMD5_SCLK		(1 << 5)
#define CMD5_SDATA		(1 << 6)
#define CMD5_EEPROMCS		(1 << 7)
#define STAT2_REG		0x1d	/* R: Status 2 reg */
#define STAT2_PROMOUT		(1 << 0)
#define STAT2_OUTA1		(1 << 1)
#define STAT2_FIFONHF		(1 << 2)
#define INTERVAL_COUNT_REG	0x1e	/* W: Interval Counter Data reg */
#define INTERVAL_STROBE_REG	0x1f	/* W: Interval Counter Strobe reg */

#define LABPC_SIZE		0x20	/* size of ISA io region */
#define LABPC_TIMER_BASE	500	/* 2 MHz master clock */
#define LABPC_ADC_TIMEOUT	1000

enum scan_mode {
	MODE_SINGLE_CHAN,
	MODE_SINGLE_CHAN_INTERVAL,
	MODE_MULT_CHAN_UP,
	MODE_MULT_CHAN_DOWN,
};

static const int labpc_plus_ai_gain_bits[] = {
	0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
	0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
};

static const struct comedi_lrange range_labpc_plus_ai = {
	16, {
		BIP_RANGE(5),
		BIP_RANGE(4),
		BIP_RANGE(2.5),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.25),
		BIP_RANGE(0.1),
		BIP_RANGE(0.05),
		UNI_RANGE(10),
		UNI_RANGE(8),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1),
		UNI_RANGE(0.5),
		UNI_RANGE(0.2),
		UNI_RANGE(0.1)
	}
};

const int labpc_1200_ai_gain_bits[] = {
	0x00, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
	0x00, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
};
EXPORT_SYMBOL_GPL(labpc_1200_ai_gain_bits);

const struct comedi_lrange range_labpc_1200_ai = {
	14, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.25),
		BIP_RANGE(0.1),
		BIP_RANGE(0.05),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1),
		UNI_RANGE(0.5),
		UNI_RANGE(0.2),
		UNI_RANGE(0.1)
	}
};
EXPORT_SYMBOL_GPL(range_labpc_1200_ai);

static const struct comedi_lrange range_labpc_ao = {
	2, {
		BIP_RANGE(5),
		UNI_RANGE(10)
	}
};

/* functions that do inb/outb and readb/writeb so we can use
 * function pointers to decide which to use */
static inline unsigned int labpc_inb(unsigned long address)
{
	return inb(address);
}

static inline void labpc_outb(unsigned int byte, unsigned long address)
{
	outb(byte, address);
}

static inline unsigned int labpc_readb(unsigned long address)
{
	return readb((void __iomem *)address);
}

static inline void labpc_writeb(unsigned int byte, unsigned long address)
{
	writeb(byte, (void __iomem *)address);
}

#ifdef CONFIG_COMEDI_NI_LABPC_ISA
static const struct labpc_boardinfo labpc_boards[] = {
	{
		.name			= "lab-pc-1200",
		.ai_speed		= 10000,
		.bustype		= isa_bustype,
		.register_layout	= labpc_1200_layout,
		.has_ao			= 1,
		.ai_range_table		= &range_labpc_1200_ai,
		.ai_range_code		= labpc_1200_ai_gain_bits,
		.ai_scan_up		= 1,
	}, {
		.name			= "lab-pc-1200ai",
		.ai_speed		= 10000,
		.bustype		= isa_bustype,
		.register_layout	= labpc_1200_layout,
		.ai_range_table		= &range_labpc_1200_ai,
		.ai_range_code		= labpc_1200_ai_gain_bits,
		.ai_scan_up		= 1,
	}, {
		.name			= "lab-pc+",
		.ai_speed		= 12000,
		.bustype		= isa_bustype,
		.register_layout	= labpc_plus_layout,
		.has_ao			= 1,
		.ai_range_table		= &range_labpc_plus_ai,
		.ai_range_code		= labpc_plus_ai_gain_bits,
	},
};
#endif

/* size in bytes of dma buffer */
static const int dma_buffer_size = 0xff00;
/* 2 bytes per sample */
static const int sample_size = 2;

static int labpc_counter_load(struct comedi_device *dev,
			      unsigned long base_address,
			      unsigned int counter_number,
			      unsigned int count, unsigned int mode)
{
	const struct labpc_boardinfo *board = comedi_board(dev);

	if (board->has_mmio)
		return i8254_mm_load((void __iomem *)base_address, 0,
				     counter_number, count, mode);
	else
		return i8254_load(base_address, 0, counter_number, count, mode);
}

static int labpc_counter_set_mode(struct comedi_device *dev,
				  unsigned long base_address,
				  unsigned int counter_number,
				  unsigned int mode)
{
	const struct labpc_boardinfo *board = comedi_board(dev);

	if (board->has_mmio)
		return i8254_mm_set_mode((void __iomem *)base_address, 0,
					 counter_number, mode);
	else
		return i8254_set_mode(base_address, 0, counter_number, mode);
}

static bool labpc_range_is_unipolar(struct comedi_subdevice *s,
				    unsigned int range)
{
	return s->range_table->range[range].min >= 0;
}

static int labpc_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct labpc_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->cmd2 &= ~(CMD2_SWTRIG | CMD2_HWTRIG | CMD2_PRETRIG);
	devpriv->write_byte(devpriv->cmd2, dev->iobase + CMD2_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	devpriv->cmd3 = 0;
	devpriv->write_byte(devpriv->cmd3, dev->iobase + CMD3_REG);

	return 0;
}

static void labpc_ai_set_chan_and_gain(struct comedi_device *dev,
				       enum scan_mode mode,
				       unsigned int chan,
				       unsigned int range,
				       unsigned int aref)
{
	const struct labpc_boardinfo *board = comedi_board(dev);
	struct labpc_private *devpriv = dev->private;

	/* munge channel bits for differential/scan disabled mode */
	if ((mode == MODE_SINGLE_CHAN || mode == MODE_SINGLE_CHAN_INTERVAL) &&
	    aref == AREF_DIFF)
		chan *= 2;
	devpriv->cmd1 = CMD1_MA(chan);
	devpriv->cmd1 |= board->ai_range_code[range];

	devpriv->write_byte(devpriv->cmd1, dev->iobase + CMD1_REG);
}

static void labpc_setup_cmd6_reg(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 enum scan_mode mode,
				 enum transfer_type xfer,
				 unsigned int range,
				 unsigned int aref,
				 bool ena_intr)
{
	const struct labpc_boardinfo *board = comedi_board(dev);
	struct labpc_private *devpriv = dev->private;

	if (board->register_layout != labpc_1200_layout)
		return;

	/* reference inputs to ground or common? */
	if (aref != AREF_GROUND)
		devpriv->cmd6 |= CMD6_NRSE;
	else
		devpriv->cmd6 &= ~CMD6_NRSE;

	/* bipolar or unipolar range? */
	if (labpc_range_is_unipolar(s, range))
		devpriv->cmd6 |= CMD6_ADCUNI;
	else
		devpriv->cmd6 &= ~CMD6_ADCUNI;

	/*  interrupt on fifo half full? */
	if (xfer == fifo_half_full_transfer)
		devpriv->cmd6 |= CMD6_HFINTEN;
	else
		devpriv->cmd6 &= ~CMD6_HFINTEN;

	/* enable interrupt on counter a1 terminal count? */
	if (ena_intr)
		devpriv->cmd6 |= CMD6_DQINTEN;
	else
		devpriv->cmd6 &= ~CMD6_DQINTEN;

	/* are we scanning up or down through channels? */
	if (mode == MODE_MULT_CHAN_UP)
		devpriv->cmd6 |= CMD6_SCANUP;
	else
		devpriv->cmd6 &= ~CMD6_SCANUP;

	devpriv->write_byte(devpriv->cmd6, dev->iobase + CMD6_REG);
}

static unsigned int labpc_read_adc_fifo(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int lsb = devpriv->read_byte(dev->iobase + ADC_FIFO_REG);
	unsigned int msb = devpriv->read_byte(dev->iobase + ADC_FIFO_REG);

	return (msb << 8) | lsb;
}

static void labpc_clear_adc_fifo(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;

	devpriv->write_byte(0x1, dev->iobase + ADC_FIFO_CLEAR_REG);
	labpc_read_adc_fifo(dev);
}

static int labpc_ai_wait_for_data(struct comedi_device *dev,
				  int timeout)
{
	struct labpc_private *devpriv = dev->private;
	int i;

	for (i = 0; i < timeout; i++) {
		devpriv->stat1 = devpriv->read_byte(dev->iobase + STAT1_REG);
		if (devpriv->stat1 & STAT1_DAVAIL)
			return 0;
		udelay(1);
	}
	return -ETIME;
}

static int labpc_ai_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int aref = CR_AREF(insn->chanspec);
	int ret;
	int i;

	/* disable timed conversions, interrupt generation and dma */
	labpc_cancel(dev, s);

	labpc_ai_set_chan_and_gain(dev, MODE_SINGLE_CHAN, chan, range, aref);

	labpc_setup_cmd6_reg(dev, s, MODE_SINGLE_CHAN, fifo_not_empty_transfer,
			     range, aref, false);

	/* setup cmd4 register */
	devpriv->cmd4 = 0;
	devpriv->cmd4 |= CMD4_ECLKRCV;
	/* single-ended/differential */
	if (aref == AREF_DIFF)
		devpriv->cmd4 |= CMD4_SEDIFF;
	devpriv->write_byte(devpriv->cmd4, dev->iobase + CMD4_REG);

	/* initialize pacer counter to prevent any problems */
	ret = labpc_counter_set_mode(dev, dev->iobase + COUNTER_A_BASE_REG,
				     0, I8254_MODE2);
	if (ret)
		return ret;

	labpc_clear_adc_fifo(dev);

	for (i = 0; i < insn->n; i++) {
		/* trigger conversion */
		devpriv->write_byte(0x1, dev->iobase + ADC_START_CONVERT_REG);

		ret = labpc_ai_wait_for_data(dev, LABPC_ADC_TIMEOUT);
		if (ret)
			return ret;

		data[i] = labpc_read_adc_fifo(dev);
	}

	return insn->n;
}

#ifdef CONFIG_ISA_DMA_API
/* utility function that suggests a dma transfer size in bytes */
static unsigned int labpc_suggest_transfer_size(const struct comedi_cmd *cmd)
{
	unsigned int size;
	unsigned int freq;

	if (cmd->convert_src == TRIG_TIMER)
		freq = 1000000000 / cmd->convert_arg;
	/* return some default value */
	else
		freq = 0xffffffff;

	/* make buffer fill in no more than 1/3 second */
	size = (freq / 3) * sample_size;

	/* set a minimum and maximum size allowed */
	if (size > dma_buffer_size)
		size = dma_buffer_size - dma_buffer_size % sample_size;
	else if (size < sample_size)
		size = sample_size;

	return size;
}
#endif

static bool labpc_use_continuous_mode(const struct comedi_cmd *cmd,
				      enum scan_mode mode)
{
	if (mode == MODE_SINGLE_CHAN || cmd->scan_begin_src == TRIG_FOLLOW)
		return true;

	return false;
}

static unsigned int labpc_ai_convert_period(const struct comedi_cmd *cmd,
					    enum scan_mode mode)
{
	if (cmd->convert_src != TRIG_TIMER)
		return 0;

	if (mode == MODE_SINGLE_CHAN && cmd->scan_begin_src == TRIG_TIMER)
		return cmd->scan_begin_arg;

	return cmd->convert_arg;
}

static void labpc_set_ai_convert_period(struct comedi_cmd *cmd,
					enum scan_mode mode, unsigned int ns)
{
	if (cmd->convert_src != TRIG_TIMER)
		return;

	if (mode == MODE_SINGLE_CHAN &&
	    cmd->scan_begin_src == TRIG_TIMER) {
		cmd->scan_begin_arg = ns;
		if (cmd->convert_arg > cmd->scan_begin_arg)
			cmd->convert_arg = cmd->scan_begin_arg;
	} else
		cmd->convert_arg = ns;
}

static unsigned int labpc_ai_scan_period(const struct comedi_cmd *cmd,
					enum scan_mode mode)
{
	if (cmd->scan_begin_src != TRIG_TIMER)
		return 0;

	if (mode == MODE_SINGLE_CHAN && cmd->convert_src == TRIG_TIMER)
		return 0;

	return cmd->scan_begin_arg;
}

static void labpc_set_ai_scan_period(struct comedi_cmd *cmd,
				     enum scan_mode mode, unsigned int ns)
{
	if (cmd->scan_begin_src != TRIG_TIMER)
		return;

	if (mode == MODE_SINGLE_CHAN && cmd->convert_src == TRIG_TIMER)
		return;

	cmd->scan_begin_arg = ns;
}

/* figures out what counter values to use based on command */
static void labpc_adc_timing(struct comedi_device *dev, struct comedi_cmd *cmd,
			     enum scan_mode mode)
{
	struct labpc_private *devpriv = dev->private;
	/* max value for 16 bit counter in mode 2 */
	const int max_counter_value = 0x10000;
	/* min value for 16 bit counter in mode 2 */
	const int min_counter_value = 2;
	unsigned int base_period;
	unsigned int scan_period;
	unsigned int convert_period;

	/*
	 * if both convert and scan triggers are TRIG_TIMER, then they
	 * both rely on counter b0
	 */
	convert_period = labpc_ai_convert_period(cmd, mode);
	scan_period = labpc_ai_scan_period(cmd, mode);
	if (convert_period && scan_period) {
		/*
		 * pick the lowest b0 divisor value we can (for maximum input
		 * clock speed on convert and scan counters)
		 */
		devpriv->divisor_b0 = (scan_period - 1) /
		    (LABPC_TIMER_BASE * max_counter_value) + 1;
		if (devpriv->divisor_b0 < min_counter_value)
			devpriv->divisor_b0 = min_counter_value;
		if (devpriv->divisor_b0 > max_counter_value)
			devpriv->divisor_b0 = max_counter_value;

		base_period = LABPC_TIMER_BASE * devpriv->divisor_b0;

		/*  set a0 for conversion frequency and b1 for scan frequency */
		switch (cmd->flags & TRIG_ROUND_MASK) {
		default:
		case TRIG_ROUND_NEAREST:
			devpriv->divisor_a0 =
			    (convert_period + (base_period / 2)) / base_period;
			devpriv->divisor_b1 =
			    (scan_period + (base_period / 2)) / base_period;
			break;
		case TRIG_ROUND_UP:
			devpriv->divisor_a0 =
			    (convert_period + (base_period - 1)) / base_period;
			devpriv->divisor_b1 =
			    (scan_period + (base_period - 1)) / base_period;
			break;
		case TRIG_ROUND_DOWN:
			devpriv->divisor_a0 = convert_period / base_period;
			devpriv->divisor_b1 = scan_period / base_period;
			break;
		}
		/*  make sure a0 and b1 values are acceptable */
		if (devpriv->divisor_a0 < min_counter_value)
			devpriv->divisor_a0 = min_counter_value;
		if (devpriv->divisor_a0 > max_counter_value)
			devpriv->divisor_a0 = max_counter_value;
		if (devpriv->divisor_b1 < min_counter_value)
			devpriv->divisor_b1 = min_counter_value;
		if (devpriv->divisor_b1 > max_counter_value)
			devpriv->divisor_b1 = max_counter_value;
		/*  write corrected timings to command */
		labpc_set_ai_convert_period(cmd, mode,
					    base_period * devpriv->divisor_a0);
		labpc_set_ai_scan_period(cmd, mode,
					 base_period * devpriv->divisor_b1);
		/*
		 * if only one TRIG_TIMER is used, we can employ the generic
		 * cascaded timing functions
		 */
	} else if (scan_period) {
		/*
		 * calculate cascaded counter values
		 * that give desired scan timing
		 */
		i8253_cascade_ns_to_timer_2div(LABPC_TIMER_BASE,
					       &(devpriv->divisor_b1),
					       &(devpriv->divisor_b0),
					       &scan_period,
					       cmd->flags & TRIG_ROUND_MASK);
		labpc_set_ai_scan_period(cmd, mode, scan_period);
	} else if (convert_period) {
		/*
		 * calculate cascaded counter values
		 * that give desired conversion timing
		 */
		i8253_cascade_ns_to_timer_2div(LABPC_TIMER_BASE,
					       &(devpriv->divisor_a0),
					       &(devpriv->divisor_b0),
					       &convert_period,
					       cmd->flags & TRIG_ROUND_MASK);
		labpc_set_ai_convert_period(cmd, mode, convert_period);
	}
}

static enum scan_mode labpc_ai_scan_mode(const struct comedi_cmd *cmd)
{
	if (cmd->chanlist_len == 1)
		return MODE_SINGLE_CHAN;

	/* chanlist may be NULL during cmdtest. */
	if (cmd->chanlist == NULL)
		return MODE_MULT_CHAN_UP;

	if (CR_CHAN(cmd->chanlist[0]) == CR_CHAN(cmd->chanlist[1]))
		return MODE_SINGLE_CHAN_INTERVAL;

	if (CR_CHAN(cmd->chanlist[0]) < CR_CHAN(cmd->chanlist[1]))
		return MODE_MULT_CHAN_UP;

	if (CR_CHAN(cmd->chanlist[0]) > CR_CHAN(cmd->chanlist[1]))
		return MODE_MULT_CHAN_DOWN;

	pr_err("ni_labpc: bug! cannot determine AI scan mode\n");
	return 0;
}

static int labpc_ai_chanlist_invalid(const struct comedi_device *dev,
				     const struct comedi_cmd *cmd,
				     enum scan_mode mode)
{
	int channel, range, aref, i;

	if (cmd->chanlist == NULL)
		return 0;

	if (mode == MODE_SINGLE_CHAN)
		return 0;

	if (mode == MODE_SINGLE_CHAN_INTERVAL) {
		if (cmd->chanlist_len > 0xff) {
			comedi_error(dev,
				     "ni_labpc: chanlist too long for single channel interval mode\n");
			return 1;
		}
	}

	channel = CR_CHAN(cmd->chanlist[0]);
	range = CR_RANGE(cmd->chanlist[0]);
	aref = CR_AREF(cmd->chanlist[0]);

	for (i = 0; i < cmd->chanlist_len; i++) {

		switch (mode) {
		case MODE_SINGLE_CHAN_INTERVAL:
			if (CR_CHAN(cmd->chanlist[i]) != channel) {
				comedi_error(dev,
					     "channel scanning order specified in chanlist is not supported by hardware.\n");
				return 1;
			}
			break;
		case MODE_MULT_CHAN_UP:
			if (CR_CHAN(cmd->chanlist[i]) != i) {
				comedi_error(dev,
					     "channel scanning order specified in chanlist is not supported by hardware.\n");
				return 1;
			}
			break;
		case MODE_MULT_CHAN_DOWN:
			if (CR_CHAN(cmd->chanlist[i]) !=
			    cmd->chanlist_len - i - 1) {
				comedi_error(dev,
					     "channel scanning order specified in chanlist is not supported by hardware.\n");
				return 1;
			}
			break;
		default:
			dev_err(dev->class_dev,
				"ni_labpc: bug! in chanlist check\n");
			return 1;
			break;
		}

		if (CR_RANGE(cmd->chanlist[i]) != range) {
			comedi_error(dev,
				     "entries in chanlist must all have the same range\n");
			return 1;
		}

		if (CR_AREF(cmd->chanlist[i]) != aref) {
			comedi_error(dev,
				     "entries in chanlist must all have the same reference\n");
			return 1;
		}
	}

	return 0;
}

static int labpc_ai_cmdtest(struct comedi_device *dev,
			    struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	const struct labpc_boardinfo *board = comedi_board(dev);
	int err = 0;
	int tmp, tmp2;
	unsigned int stop_mask;
	enum scan_mode mode;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);

	stop_mask = TRIG_COUNT | TRIG_NONE;
	if (board->register_layout == labpc_1200_layout)
		stop_mask |= TRIG_EXT;
	err |= cfc_check_trigger_src(&cmd->stop_src, stop_mask);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	/* can't have external stop and start triggers at once */
	if (cmd->start_src == TRIG_EXT && cmd->stop_src == TRIG_EXT)
		err++;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	if (cmd->start_arg == TRIG_NOW)
		err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (!cmd->chanlist_len)
		err |= -EINVAL;
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->convert_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg,
						 board->ai_speed);

	/* make sure scan timing is not too fast */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->convert_src == TRIG_TIMER)
			err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
					cmd->convert_arg * cmd->chanlist_len);
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
				board->ai_speed * cmd->chanlist_len);
	}

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
		break;
	case TRIG_NONE:
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
		/*
		 * TRIG_EXT doesn't care since it doesn't
		 * trigger off a numbered channel
		 */
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	tmp = cmd->convert_arg;
	tmp2 = cmd->scan_begin_arg;
	mode = labpc_ai_scan_mode(cmd);
	labpc_adc_timing(dev, cmd, mode);
	if (tmp != cmd->convert_arg || tmp2 != cmd->scan_begin_arg)
		err++;

	if (err)
		return 4;

	if (labpc_ai_chanlist_invalid(dev, cmd, mode))
		return 5;

	return 0;
}

static int labpc_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct labpc_boardinfo *board = comedi_board(dev);
	struct labpc_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	enum scan_mode mode = labpc_ai_scan_mode(cmd);
	unsigned int chanspec = (mode == MODE_MULT_CHAN_UP)
				? cmd->chanlist[cmd->chanlist_len - 1]
				: cmd->chanlist[0];
	unsigned int chan = CR_CHAN(chanspec);
	unsigned int range = CR_RANGE(chanspec);
	unsigned int aref = CR_AREF(chanspec);
	enum transfer_type xfer;
	unsigned long flags;
	int ret;

	/* make sure board is disabled before setting up acquisition */
	labpc_cancel(dev, s);

	/*  initialize software conversion count */
	if (cmd->stop_src == TRIG_COUNT)
		devpriv->count = cmd->stop_arg * cmd->chanlist_len;

	/*  setup hardware conversion counter */
	if (cmd->stop_src == TRIG_EXT) {
		/*
		 * load counter a1 with count of 3
		 * (pc+ manual says this is minimum allowed) using mode 0
		 */
		ret = labpc_counter_load(dev, dev->iobase + COUNTER_A_BASE_REG,
					 1, 3, I8254_MODE0);
	} else	{
		/* just put counter a1 in mode 0 to set its output low */
		ret = labpc_counter_set_mode(dev,
					     dev->iobase + COUNTER_A_BASE_REG,
					     1, I8254_MODE0);
	}
	if (ret) {
		comedi_error(dev, "error loading counter a1");
		return ret;
	}

#ifdef CONFIG_ISA_DMA_API
	/*  figure out what method we will use to transfer data */
	if (devpriv->dma_chan &&	/*  need a dma channel allocated */
		/*
		 * dma unsafe at RT priority,
		 * and too much setup time for TRIG_WAKE_EOS for
		 */
	    (cmd->flags & (TRIG_WAKE_EOS | TRIG_RT)) == 0) {
		xfer = isa_dma_transfer;
		/* pc-plus has no fifo-half full interrupt */
	} else
#endif
	if (board->register_layout == labpc_1200_layout &&
		   /*  wake-end-of-scan should interrupt on fifo not empty */
		   (cmd->flags & TRIG_WAKE_EOS) == 0 &&
		   /*  make sure we are taking more than just a few points */
		   (cmd->stop_src != TRIG_COUNT || devpriv->count > 256)) {
		xfer = fifo_half_full_transfer;
	} else
		xfer = fifo_not_empty_transfer;
	devpriv->current_transfer = xfer;

	labpc_ai_set_chan_and_gain(dev, mode, chan, range, aref);

	labpc_setup_cmd6_reg(dev, s, mode, xfer, range, aref,
			     (cmd->stop_src == TRIG_EXT));

	/* manual says to set scan enable bit on second pass */
	if (mode == MODE_MULT_CHAN_UP || mode == MODE_MULT_CHAN_DOWN) {
		devpriv->cmd1 |= CMD1_SCANEN;
		/* need a brief delay before enabling scan, or scan
		 * list will get screwed when you switch
		 * between scan up to scan down mode - dunno why */
		udelay(1);
		devpriv->write_byte(devpriv->cmd1, dev->iobase + CMD1_REG);
	}

	devpriv->write_byte(cmd->chanlist_len,
			    dev->iobase + INTERVAL_COUNT_REG);
	/*  load count */
	devpriv->write_byte(0x1, dev->iobase + INTERVAL_STROBE_REG);

	if (cmd->convert_src == TRIG_TIMER ||
	    cmd->scan_begin_src == TRIG_TIMER) {
		/*  set up pacing */
		labpc_adc_timing(dev, cmd, mode);
		/*  load counter b0 in mode 3 */
		ret = labpc_counter_load(dev, dev->iobase + COUNTER_B_BASE_REG,
					 0, devpriv->divisor_b0, I8254_MODE3);
		if (ret < 0) {
			comedi_error(dev, "error loading counter b0");
			return -1;
		}
	}
	/*  set up conversion pacing */
	if (labpc_ai_convert_period(cmd, mode)) {
		/*  load counter a0 in mode 2 */
		ret = labpc_counter_load(dev, dev->iobase + COUNTER_A_BASE_REG,
					 0, devpriv->divisor_a0, I8254_MODE2);
	} else {
		/* initialize pacer counter to prevent any problems */
		ret = labpc_counter_set_mode(dev,
					     dev->iobase + COUNTER_A_BASE_REG,
					     0, I8254_MODE2);
	}
	if (ret) {
		comedi_error(dev, "error loading counter a0");
		return ret;
	}

	/*  set up scan pacing */
	if (labpc_ai_scan_period(cmd, mode)) {
		/*  load counter b1 in mode 2 */
		ret = labpc_counter_load(dev, dev->iobase + COUNTER_B_BASE_REG,
					 1, devpriv->divisor_b1, I8254_MODE2);
		if (ret < 0) {
			comedi_error(dev, "error loading counter b1");
			return -1;
		}
	}

	labpc_clear_adc_fifo(dev);

#ifdef CONFIG_ISA_DMA_API
	/*  set up dma transfer */
	if (xfer == isa_dma_transfer) {
		unsigned long irq_flags;

		irq_flags = claim_dma_lock();
		disable_dma(devpriv->dma_chan);
		/* clear flip-flop to make sure 2-byte registers for
		 * count and address get set correctly */
		clear_dma_ff(devpriv->dma_chan);
		set_dma_addr(devpriv->dma_chan,
			     virt_to_bus(devpriv->dma_buffer));
		/*  set appropriate size of transfer */
		devpriv->dma_transfer_size = labpc_suggest_transfer_size(cmd);
		if (cmd->stop_src == TRIG_COUNT &&
		    devpriv->count * sample_size < devpriv->dma_transfer_size) {
			devpriv->dma_transfer_size =
			    devpriv->count * sample_size;
		}
		set_dma_count(devpriv->dma_chan, devpriv->dma_transfer_size);
		enable_dma(devpriv->dma_chan);
		release_dma_lock(irq_flags);
		/*  enable board's dma */
		devpriv->cmd3 |= (CMD3_DMAEN | CMD3_DMATCINTEN);
	} else
		devpriv->cmd3 &= ~(CMD3_DMAEN | CMD3_DMATCINTEN);
#endif

	/*  enable error interrupts */
	devpriv->cmd3 |= CMD3_ERRINTEN;
	/*  enable fifo not empty interrupt? */
	if (xfer == fifo_not_empty_transfer)
		devpriv->cmd3 |= CMD3_FIFOINTEN;
	else
		devpriv->cmd3 &= ~CMD3_FIFOINTEN;
	devpriv->write_byte(devpriv->cmd3, dev->iobase + CMD3_REG);

	/*  setup any external triggering/pacing (cmd4 register) */
	devpriv->cmd4 = 0;
	if (cmd->convert_src != TRIG_EXT)
		devpriv->cmd4 |= CMD4_ECLKRCV;
	/* XXX should discard first scan when using interval scanning
	 * since manual says it is not synced with scan clock */
	if (!labpc_use_continuous_mode(cmd, mode)) {
		devpriv->cmd4 |= CMD4_INTSCAN;
		if (cmd->scan_begin_src == TRIG_EXT)
			devpriv->cmd4 |= CMD4_EOIRCV;
	}
	/*  single-ended/differential */
	if (aref == AREF_DIFF)
		devpriv->cmd4 |= CMD4_SEDIFF;
	devpriv->write_byte(devpriv->cmd4, dev->iobase + CMD4_REG);

	/*  startup acquisition */

	spin_lock_irqsave(&dev->spinlock, flags);

	/* use 2 cascaded counters for pacing */
	devpriv->cmd2 |= CMD2_TBSEL;

	devpriv->cmd2 &= ~(CMD2_SWTRIG | CMD2_HWTRIG | CMD2_PRETRIG);
	if (cmd->start_src == TRIG_EXT)
		devpriv->cmd2 |= CMD2_HWTRIG;
	else
		devpriv->cmd2 |= CMD2_SWTRIG;
	if (cmd->stop_src == TRIG_EXT)
		devpriv->cmd2 |= (CMD2_HWTRIG | CMD2_PRETRIG);

	devpriv->write_byte(devpriv->cmd2, dev->iobase + CMD2_REG);

	spin_unlock_irqrestore(&dev->spinlock, flags);

	return 0;
}

#ifdef CONFIG_ISA_DMA_API
static void labpc_drain_dma(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	int status;
	unsigned long flags;
	unsigned int max_points, num_points, residue, leftover;
	int i;

	status = devpriv->stat1;

	flags = claim_dma_lock();
	disable_dma(devpriv->dma_chan);
	/* clear flip-flop to make sure 2-byte registers for
	 * count and address get set correctly */
	clear_dma_ff(devpriv->dma_chan);

	/*  figure out how many points to read */
	max_points = devpriv->dma_transfer_size / sample_size;
	/* residue is the number of points left to be done on the dma
	 * transfer.  It should always be zero at this point unless
	 * the stop_src is set to external triggering.
	 */
	residue = get_dma_residue(devpriv->dma_chan) / sample_size;
	num_points = max_points - residue;
	if (devpriv->count < num_points && async->cmd.stop_src == TRIG_COUNT)
		num_points = devpriv->count;

	/*  figure out how many points will be stored next time */
	leftover = 0;
	if (async->cmd.stop_src != TRIG_COUNT) {
		leftover = devpriv->dma_transfer_size / sample_size;
	} else if (devpriv->count > num_points) {
		leftover = devpriv->count - num_points;
		if (leftover > max_points)
			leftover = max_points;
	}

	/* write data to comedi buffer */
	for (i = 0; i < num_points; i++)
		cfc_write_to_buffer(s, devpriv->dma_buffer[i]);

	if (async->cmd.stop_src == TRIG_COUNT)
		devpriv->count -= num_points;

	/*  set address and count for next transfer */
	set_dma_addr(devpriv->dma_chan, virt_to_bus(devpriv->dma_buffer));
	set_dma_count(devpriv->dma_chan, leftover * sample_size);
	release_dma_lock(flags);

	async->events |= COMEDI_CB_BLOCK;
}

static void handle_isa_dma(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;

	labpc_drain_dma(dev);

	enable_dma(devpriv->dma_chan);

	/*  clear dma tc interrupt */
	devpriv->write_byte(0x1, dev->iobase + DMATC_CLEAR_REG);
}
#endif

/* read all available samples from ai fifo */
static int labpc_drain_fifo(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	short data;
	struct comedi_async *async = dev->read_subdev->async;
	const int timeout = 10000;
	unsigned int i;

	devpriv->stat1 = devpriv->read_byte(dev->iobase + STAT1_REG);

	for (i = 0; (devpriv->stat1 & STAT1_DAVAIL) && i < timeout;
	     i++) {
		/*  quit if we have all the data we want */
		if (async->cmd.stop_src == TRIG_COUNT) {
			if (devpriv->count == 0)
				break;
			devpriv->count--;
		}
		data = labpc_read_adc_fifo(dev);
		cfc_write_to_buffer(dev->read_subdev, data);
		devpriv->stat1 = devpriv->read_byte(dev->iobase + STAT1_REG);
	}
	if (i == timeout) {
		comedi_error(dev, "ai timeout, fifo never empties");
		async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		return -1;
	}

	return 0;
}

/* makes sure all data acquired by board is transferred to comedi (used
 * when acquisition is terminated by stop_src == TRIG_EXT). */
static void labpc_drain_dregs(struct comedi_device *dev)
{
#ifdef CONFIG_ISA_DMA_API
	struct labpc_private *devpriv = dev->private;

	if (devpriv->current_transfer == isa_dma_transfer)
		labpc_drain_dma(dev);
#endif

	labpc_drain_fifo(dev);
}

/* interrupt service routine */
static irqreturn_t labpc_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	const struct labpc_boardinfo *board = comedi_board(dev);
	struct labpc_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async;
	struct comedi_cmd *cmd;

	if (!dev->attached) {
		comedi_error(dev, "premature interrupt");
		return IRQ_HANDLED;
	}

	async = s->async;
	cmd = &async->cmd;
	async->events = 0;

	/* read board status */
	devpriv->stat1 = devpriv->read_byte(dev->iobase + STAT1_REG);
	if (board->register_layout == labpc_1200_layout)
		devpriv->stat2 = devpriv->read_byte(dev->iobase + STAT2_REG);

	if ((devpriv->stat1 & (STAT1_GATA0 | STAT1_CNTINT | STAT1_OVERFLOW |
			       STAT1_OVERRUN | STAT1_DAVAIL)) == 0
	    && (devpriv->stat2 & STAT2_OUTA1) == 0
	    && (devpriv->stat2 & STAT2_FIFONHF)) {
		return IRQ_NONE;
	}

	if (devpriv->stat1 & STAT1_OVERRUN) {
		/* clear error interrupt */
		devpriv->write_byte(0x1, dev->iobase + ADC_FIFO_CLEAR_REG);
		async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		comedi_event(dev, s);
		comedi_error(dev, "overrun");
		return IRQ_HANDLED;
	}

#ifdef CONFIG_ISA_DMA_API
	if (devpriv->current_transfer == isa_dma_transfer) {
		/*
		 * if a dma terminal count of external stop trigger
		 * has occurred
		 */
		if (devpriv->stat1 & STAT1_GATA0 ||
		    (board->register_layout == labpc_1200_layout
		     && devpriv->stat2 & STAT2_OUTA1)) {
			handle_isa_dma(dev);
		}
	} else
#endif
		labpc_drain_fifo(dev);

	if (devpriv->stat1 & STAT1_CNTINT) {
		comedi_error(dev, "handled timer interrupt?");
		/*  clear it */
		devpriv->write_byte(0x1, dev->iobase + TIMER_CLEAR_REG);
	}

	if (devpriv->stat1 & STAT1_OVERFLOW) {
		/*  clear error interrupt */
		devpriv->write_byte(0x1, dev->iobase + ADC_FIFO_CLEAR_REG);
		async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		comedi_event(dev, s);
		comedi_error(dev, "overflow");
		return IRQ_HANDLED;
	}
	/*  handle external stop trigger */
	if (cmd->stop_src == TRIG_EXT) {
		if (devpriv->stat2 & STAT2_OUTA1) {
			labpc_drain_dregs(dev);
			labpc_cancel(dev, s);
			async->events |= COMEDI_CB_EOA;
		}
	}

	/* TRIG_COUNT end of acquisition */
	if (cmd->stop_src == TRIG_COUNT) {
		if (devpriv->count == 0) {
			labpc_cancel(dev, s);
			async->events |= COMEDI_CB_EOA;
		}
	}

	comedi_event(dev, s);
	return IRQ_HANDLED;
}

static int labpc_ao_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	const struct labpc_boardinfo *board = comedi_board(dev);
	struct labpc_private *devpriv = dev->private;
	int channel, range;
	unsigned long flags;
	int lsb, msb;

	channel = CR_CHAN(insn->chanspec);

	/* turn off pacing of analog output channel */
	/* note: hardware bug in daqcard-1200 means pacing cannot
	 * be independently enabled/disabled for its the two channels */
	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->cmd2 &= ~CMD2_LDAC(channel);
	devpriv->write_byte(devpriv->cmd2, dev->iobase + CMD2_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/* set range */
	if (board->register_layout == labpc_1200_layout) {
		range = CR_RANGE(insn->chanspec);
		if (labpc_range_is_unipolar(s, range))
			devpriv->cmd6 |= CMD6_DACUNI(channel);
		else
			devpriv->cmd6 &= ~CMD6_DACUNI(channel);
		/*  write to register */
		devpriv->write_byte(devpriv->cmd6, dev->iobase + CMD6_REG);
	}
	/* send data */
	lsb = data[0] & 0xff;
	msb = (data[0] >> 8) & 0xff;
	devpriv->write_byte(lsb, dev->iobase + DAC_LSB_REG(channel));
	devpriv->write_byte(msb, dev->iobase + DAC_MSB_REG(channel));

	/* remember value for readback */
	devpriv->ao_value[channel] = data[0];

	return 1;
}

static int labpc_ao_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct labpc_private *devpriv = dev->private;

	data[0] = devpriv->ao_value[CR_CHAN(insn->chanspec)];

	return 1;
}

static int labpc_8255_mmio(int dir, int port, int data, unsigned long iobase)
{
	if (dir) {
		writeb(data, (void __iomem *)(iobase + port));
		return 0;
	} else {
		return readb((void __iomem *)(iobase + port));
	}
}

/* lowlevel write to eeprom/dac */
static void labpc_serial_out(struct comedi_device *dev, unsigned int value,
			     unsigned int value_width)
{
	struct labpc_private *devpriv = dev->private;
	int i;

	for (i = 1; i <= value_width; i++) {
		/*  clear serial clock */
		devpriv->cmd5 &= ~CMD5_SCLK;
		/*  send bits most significant bit first */
		if (value & (1 << (value_width - i)))
			devpriv->cmd5 |= CMD5_SDATA;
		else
			devpriv->cmd5 &= ~CMD5_SDATA;
		udelay(1);
		devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
		/*  set clock to load bit */
		devpriv->cmd5 |= CMD5_SCLK;
		udelay(1);
		devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
	}
}

/* lowlevel read from eeprom */
static unsigned int labpc_serial_in(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int value = 0;
	int i;
	const int value_width = 8;	/*  number of bits wide values are */

	for (i = 1; i <= value_width; i++) {
		/*  set serial clock */
		devpriv->cmd5 |= CMD5_SCLK;
		udelay(1);
		devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
		/*  clear clock bit */
		devpriv->cmd5 &= ~CMD5_SCLK;
		udelay(1);
		devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
		/*  read bits most significant bit first */
		udelay(1);
		devpriv->stat2 = devpriv->read_byte(dev->iobase + STAT2_REG);
		if (devpriv->stat2 & STAT2_PROMOUT)
			value |= 1 << (value_width - i);
	}

	return value;
}

static unsigned int labpc_eeprom_read(struct comedi_device *dev,
				      unsigned int address)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int value;
	/*  bits to tell eeprom to expect a read */
	const int read_instruction = 0x3;
	/*  8 bit write lengths to eeprom */
	const int write_length = 8;

	/*  enable read/write to eeprom */
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
	devpriv->cmd5 |= (CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);

	/*  send read instruction */
	labpc_serial_out(dev, read_instruction, write_length);
	/*  send 8 bit address to read from */
	labpc_serial_out(dev, address, write_length);
	/*  read result */
	value = labpc_serial_in(dev);

	/*  disable read/write to eeprom */
	devpriv->cmd5 &= ~(CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);

	return value;
}

static unsigned int labpc_eeprom_read_status(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int value;
	const int read_status_instruction = 0x5;
	const int write_length = 8;	/*  8 bit write lengths to eeprom */

	/*  enable read/write to eeprom */
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
	devpriv->cmd5 |= (CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);

	/*  send read status instruction */
	labpc_serial_out(dev, read_status_instruction, write_length);
	/*  read result */
	value = labpc_serial_in(dev);

	/*  disable read/write to eeprom */
	devpriv->cmd5 &= ~(CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);

	return value;
}

static int labpc_eeprom_write(struct comedi_device *dev,
				unsigned int address, unsigned int value)
{
	struct labpc_private *devpriv = dev->private;
	const int write_enable_instruction = 0x6;
	const int write_instruction = 0x2;
	const int write_length = 8;	/*  8 bit write lengths to eeprom */
	const int write_in_progress_bit = 0x1;
	const int timeout = 10000;
	int i;

	/*  make sure there isn't already a write in progress */
	for (i = 0; i < timeout; i++) {
		if ((labpc_eeprom_read_status(dev) & write_in_progress_bit) ==
		    0)
			break;
	}
	if (i == timeout) {
		comedi_error(dev, "eeprom write timed out");
		return -ETIME;
	}
	/*  update software copy of eeprom */
	devpriv->eeprom_data[address] = value;

	/*  enable read/write to eeprom */
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
	devpriv->cmd5 |= (CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);

	/*  send write_enable instruction */
	labpc_serial_out(dev, write_enable_instruction, write_length);
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);

	/*  send write instruction */
	devpriv->cmd5 |= CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
	labpc_serial_out(dev, write_instruction, write_length);
	/*  send 8 bit address to write to */
	labpc_serial_out(dev, address, write_length);
	/*  write value */
	labpc_serial_out(dev, value, write_length);
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);

	/*  disable read/write to eeprom */
	devpriv->cmd5 &= ~(CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);

	return 0;
}

/* writes to 8 bit calibration dacs */
static void write_caldac(struct comedi_device *dev, unsigned int channel,
			 unsigned int value)
{
	struct labpc_private *devpriv = dev->private;

	if (value == devpriv->caldac[channel])
		return;
	devpriv->caldac[channel] = value;

	/*  clear caldac load bit and make sure we don't write to eeprom */
	devpriv->cmd5 &= ~(CMD5_CALDACLD | CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);

	/*  write 4 bit channel */
	labpc_serial_out(dev, channel, 4);
	/*  write 8 bit caldac value */
	labpc_serial_out(dev, value, 8);

	/*  set and clear caldac bit to load caldac value */
	devpriv->cmd5 |= CMD5_CALDACLD;
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
	devpriv->cmd5 &= ~CMD5_CALDACLD;
	udelay(1);
	devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
}

static int labpc_calib_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);

	/*
	 * Only write the last data value to the caldac. Preceding
	 * data would be overwritten anyway.
	 */
	if (insn->n > 0)
		write_caldac(dev, chan, data[insn->n - 1]);

	return insn->n;
}

static int labpc_calib_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->caldac[chan];

	return insn->n;
}

static int labpc_eeprom_insn_write(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int ret;

	/* only allow writes to user area of eeprom */
	if (chan < 16 || chan > 127)
		return -EINVAL;

	/*
	 * Only write the last data value to the eeprom. Preceding
	 * data would be overwritten anyway.
	 */
	if (insn->n > 0) {
		ret = labpc_eeprom_write(dev, chan, data[insn->n - 1]);
		if (ret)
			return ret;
	}

	return insn->n;
}

static int labpc_eeprom_insn_read(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->eeprom_data[chan];

	return insn->n;
}

int labpc_common_attach(struct comedi_device *dev,
			unsigned int irq, unsigned long isr_flags)
{
	const struct labpc_boardinfo *board = comedi_board(dev);
	struct labpc_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	int ret;
	int i;

	if (board->has_mmio) {
		devpriv->read_byte = labpc_readb;
		devpriv->write_byte = labpc_writeb;
	} else {
		devpriv->read_byte = labpc_inb;
		devpriv->write_byte = labpc_outb;
	}

	/* initialize board's command registers */
	devpriv->write_byte(devpriv->cmd1, dev->iobase + CMD1_REG);
	devpriv->write_byte(devpriv->cmd2, dev->iobase + CMD2_REG);
	devpriv->write_byte(devpriv->cmd3, dev->iobase + CMD3_REG);
	devpriv->write_byte(devpriv->cmd4, dev->iobase + CMD4_REG);
	if (board->register_layout == labpc_1200_layout) {
		devpriv->write_byte(devpriv->cmd5, dev->iobase + CMD5_REG);
		devpriv->write_byte(devpriv->cmd6, dev->iobase + CMD6_REG);
	}

	if (irq) {
		ret = request_irq(irq, labpc_interrupt, isr_flags,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = irq;
	}

	ret = comedi_alloc_subdevices(dev, 5);
	if (ret)
		return ret;

	/* analog input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_COMMON | SDF_DIFF;
	s->n_chan	= 8;
	s->len_chanlist	= 8;
	s->maxdata	= 0x0fff;
	s->range_table	= board->ai_range_table;
	s->insn_read	= labpc_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->do_cmd	= labpc_ai_cmd;
		s->do_cmdtest	= labpc_ai_cmdtest;
		s->cancel	= labpc_cancel;
	}

	/* analog output */
	s = &dev->subdevices[1];
	if (board->has_ao) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_GROUND;
		s->n_chan	= NUM_AO_CHAN;
		s->maxdata	= 0x0fff;
		s->range_table	= &range_labpc_ao;
		s->insn_read	= labpc_ao_insn_read;
		s->insn_write	= labpc_ao_insn_write;

		/* initialize analog outputs to a known value */
		for (i = 0; i < s->n_chan; i++) {
			short lsb, msb;

			devpriv->ao_value[i] = s->maxdata / 2;
			lsb = devpriv->ao_value[i] & 0xff;
			msb = (devpriv->ao_value[i] >> 8) & 0xff;
			devpriv->write_byte(lsb, dev->iobase + DAC_LSB_REG(i));
			devpriv->write_byte(msb, dev->iobase + DAC_MSB_REG(i));
		}
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* 8255 dio */
	s = &dev->subdevices[2];
	ret = subdev_8255_init(dev, s,
			       (board->has_mmio) ? labpc_8255_mmio : NULL,
			       dev->iobase + DIO_BASE_REG);
	if (ret)
		return ret;

	/*  calibration subdevices for boards that have one */
	s = &dev->subdevices[3];
	if (board->register_layout == labpc_1200_layout) {
		s->type		= COMEDI_SUBD_CALIB;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
		s->n_chan	= 16;
		s->maxdata	= 0xff;
		s->insn_read	= labpc_calib_insn_read;
		s->insn_write	= labpc_calib_insn_write;

		for (i = 0; i < s->n_chan; i++)
			write_caldac(dev, i, s->maxdata / 2);
	} else
		s->type		= COMEDI_SUBD_UNUSED;

	/* EEPROM */
	s = &dev->subdevices[4];
	if (board->register_layout == labpc_1200_layout) {
		s->type		= COMEDI_SUBD_MEMORY;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
		s->n_chan	= EEPROM_SIZE;
		s->maxdata	= 0xff;
		s->insn_read	= labpc_eeprom_insn_read;
		s->insn_write	= labpc_eeprom_insn_write;

		for (i = 0; i < s->n_chan; i++)
			devpriv->eeprom_data[i] = labpc_eeprom_read(dev, i);
	} else
		s->type		= COMEDI_SUBD_UNUSED;

	return 0;
}
EXPORT_SYMBOL_GPL(labpc_common_attach);

void labpc_common_detach(struct comedi_device *dev)
{
	comedi_spriv_free(dev, 2);
}
EXPORT_SYMBOL_GPL(labpc_common_detach);

#ifdef CONFIG_COMEDI_NI_LABPC_ISA
static int labpc_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct labpc_boardinfo *board = comedi_board(dev);
	struct labpc_private *devpriv;
	unsigned int irq = it->options[1];
	unsigned int dma_chan = it->options[2];
	int ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_request_region(dev, it->options[0], LABPC_SIZE);
	if (ret)
		return ret;

	ret = labpc_common_attach(dev, irq, 0);
	if (ret)
		return ret;

#ifdef CONFIG_ISA_DMA_API
	if (dev->irq && (dma_chan == 1 || dma_chan == 3)) {
		devpriv->dma_buffer = kmalloc(dma_buffer_size,
					      GFP_KERNEL | GFP_DMA);
		if (devpriv->dma_buffer) {
			ret = request_dma(dma_chan, dev->board_name);
			if (ret == 0) {
				unsigned long dma_flags;

				devpriv->dma_chan = dma_chan;
				dma_flags = claim_dma_lock();
				disable_dma(devpriv->dma_chan);
				set_dma_mode(devpriv->dma_chan, DMA_MODE_READ);
				release_dma_lock(dma_flags);
			} else {
				kfree(devpriv->dma_buffer);
			}
		}
	}
#endif

	return 0;
}

void labpc_detach(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;

	labpc_common_detach(dev);

	if (devpriv) {
		kfree(devpriv->dma_buffer);
		if (devpriv->dma_chan)
			free_dma(devpriv->dma_chan);
	}
	comedi_legacy_detach(dev);
}

static struct comedi_driver labpc_driver = {
	.driver_name	= "ni_labpc",
	.module		= THIS_MODULE,
	.attach		= labpc_attach,
	.detach		= labpc_detach,
	.num_names	= ARRAY_SIZE(labpc_boards),
	.board_name	= &labpc_boards[0].name,
	.offset		= sizeof(struct labpc_boardinfo),
};
module_comedi_driver(labpc_driver);
#else
static int __init labpc_common_init(void)
{
	return 0;
}
module_init(labpc_common_init);

static void __exit labpc_common_exit(void)
{
}
module_exit(labpc_common_exit);
#endif

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
