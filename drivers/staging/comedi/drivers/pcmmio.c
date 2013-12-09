/*
    comedi/drivers/pcmmio.c
    Driver for Winsystems PC-104 based multifunction IO board.

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2007 Calin A. Culianu <calin@ajvar.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/
/*
Driver: pcmmio
Description: A driver for the PCM-MIO multifunction board
Devices: [Winsystems] PCM-MIO (pcmmio)
Author: Calin Culianu <calin@ajvar.org>
Updated: Wed, May 16 2007 16:21:10 -0500
Status: works

A driver for the relatively new PCM-MIO multifunction board from
Winsystems.  This board is a PC-104 based I/O board.  It contains
four subdevices:
  subdevice 0 - 16 channels of 16-bit AI
  subdevice 1 - 8 channels of 16-bit AO
  subdevice 2 - first 24 channels of the 48 channel of DIO
	(with edge-triggered interrupt support)
  subdevice 3 - last 24 channels of the 48 channel DIO
	(no interrupt support for this bank of channels)

  Some notes:

  Synchronous reads and writes are the only things implemented for AI and AO,
  even though the hardware itself can do streaming acquisition, etc.  Anyone
  want to add asynchronous I/O for AI/AO as a feature?  Be my guest...

  Asynchronous I/O for the DIO subdevices *is* implemented, however!  They are
  basically edge-triggered interrupts for any configuration of the first
  24 DIO-lines.

  Also note that this interrupt support is untested.

  A few words about edge-detection IRQ support (commands on DIO):

  * To use edge-detection IRQ support for the DIO subdevice, pass the IRQ
    of the board to the comedi_config command.  The board IRQ is not jumpered
    but rather configured through software, so any IRQ from 1-15 is OK.

  * Due to the genericity of the comedi API, you need to create a special
    comedi_command in order to use edge-triggered interrupts for DIO.

  * Use comedi_commands with TRIG_NOW.  Your callback will be called each
    time an edge is detected on the specified DIO line(s), and the data
    values will be two sample_t's, which should be concatenated to form
    one 32-bit unsigned int.  This value is the mask of channels that had
    edges detected from your channel list.  Note that the bits positions
    in the mask correspond to positions in your chanlist when you
    specified the command and *not* channel id's!

 *  To set the polarity of the edge-detection interrupts pass a nonzero value
    for either CR_RANGE or CR_AREF for edge-up polarity, or a zero
    value for both CR_RANGE and CR_AREF if you want edge-down polarity.

Configuration Options:
  [0] - I/O port base address
  [1] - IRQ (optional -- for edge-detect interrupt support only,
	leave out if you don't need this feature)
*/

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include "../comedidev.h"

#include "comedi_fc.h"

/*
 * Register I/O map
 */
#define PCMMIO_AI_LSB_REG			0x00
#define PCMMIO_AI_MSB_REG			0x01
#define PCMMIO_AI_CMD_REG			0x02
#define PCMMIO_AI_CMD_SE			(1 << 7)
#define PCMMIO_AI_CMD_ODD_CHAN			(1 << 6)
#define PCMMIO_AI_CMD_CHAN_SEL(x)		(((x) & 0x3) << 4)
#define PCMMIO_AI_CMD_RANGE(x)			(((x) & 0x3) << 2)
#define PCMMIO_AI_STATUS_REG			0x03
#define PCMMIO_AI_STATUS_DATA_READY		(1 << 7)
#define PCMMIO_AI_STATUS_DATA_DMA_PEND		(1 << 6)
#define PCMMIO_AI_STATUS_CMD_DMA_PEND		(1 << 5)
#define PCMMIO_AI_STATUS_IRQ_PEND		(1 << 4)
#define PCMMIO_AI_STATUS_DATA_DRQ_ENA		(1 << 2)
#define PCMMIO_AI_STATUS_REG_SEL		(1 << 3)
#define PCMMIO_AI_STATUS_CMD_DRQ_ENA		(1 << 1)
#define PCMMIO_AI_STATUS_IRQ_ENA		(1 << 0)
#define PCMMIO_AI_RESOURCE_REG			0x03
#define PCMMIO_AI_2ND_ADC_OFFSET		0x04

#define PCMMIO_AO_LSB_REG			0x08
#define PCMMIO_AO_LSB_SPAN(x)			(((x) & 0xf) << 0)
#define PCMMIO_AO_MSB_REG			0x09
#define PCMMIO_AO_CMD_REG			0x0a
#define PCMMIO_AO_CMD_WR_SPAN			(0x2 << 4)
#define PCMMIO_AO_CMD_WR_CODE			(0x3 << 4)
#define PCMMIO_AO_CMD_UPDATE			(0x4 << 4)
#define PCMMIO_AO_CMD_UPDATE_ALL		(0x5 << 4)
#define PCMMIO_AO_CMD_WR_SPAN_UPDATE		(0x6 << 4)
#define PCMMIO_AO_CMD_WR_CODE_UPDATE		(0x7 << 4)
#define PCMMIO_AO_CMD_WR_SPAN_UPDATE_ALL	(0x8 << 4)
#define PCMMIO_AO_CMD_WR_CODE_UPDATE_ALL	(0x9 << 4)
#define PCMMIO_AO_CMD_RD_B1_SPAN		(0xa << 4)
#define PCMMIO_AO_CMD_RD_B1_CODE		(0xb << 4)
#define PCMMIO_AO_CMD_RD_B2_SPAN		(0xc << 4)
#define PCMMIO_AO_CMD_RD_B2_CODE		(0xd << 4)
#define PCMMIO_AO_CMD_NOP			(0xf << 4)
#define PCMMIO_AO_CMD_CHAN_SEL(x)		(((x) & 0x03) << 1)
#define PCMMIO_AO_CMD_CHAN_SEL_ALL		(0x0f << 0)
#define PCMMIO_AO_STATUS_REG			0x0b
#define PCMMIO_AO_STATUS_DATA_READY		(1 << 7)
#define PCMMIO_AO_STATUS_DATA_DMA_PEND		(1 << 6)
#define PCMMIO_AO_STATUS_CMD_DMA_PEND		(1 << 5)
#define PCMMIO_AO_STATUS_IRQ_PEND		(1 << 4)
#define PCMMIO_AO_STATUS_DATA_DRQ_ENA		(1 << 2)
#define PCMMIO_AO_STATUS_REG_SEL		(1 << 3)
#define PCMMIO_AO_STATUS_CMD_DRQ_ENA		(1 << 1)
#define PCMMIO_AO_STATUS_IRQ_ENA		(1 << 0)
#define PCMMIO_AO_RESOURCE_ENA_REG		0x0b
#define PCMMIO_AO_2ND_DAC_OFFSET		0x04

/*
 * WinSystems WS16C48
 *
 * Offset    Page 0       Page 1       Page 2       Page 3
 * ------  -----------  -----------  -----------  -----------
 *  0x10   Port 0 I/O   Port 0 I/O   Port 0 I/O   Port 0 I/O
 *  0x11   Port 1 I/O   Port 1 I/O   Port 1 I/O   Port 1 I/O
 *  0x12   Port 2 I/O   Port 2 I/O   Port 2 I/O   Port 2 I/O
 *  0x13   Port 3 I/O   Port 3 I/O   Port 3 I/O   Port 3 I/O
 *  0x14   Port 4 I/O   Port 4 I/O   Port 4 I/O   Port 4 I/O
 *  0x15   Port 5 I/O   Port 5 I/O   Port 5 I/O   Port 5 I/O
 *  0x16   INT_PENDING  INT_PENDING  INT_PENDING  INT_PENDING
 *  0x17    Page/Lock    Page/Lock    Page/Lock    Page/Lock
 *  0x18       N/A         POL_0       ENAB_0       INT_ID0
 *  0x19       N/A         POL_1       ENAB_1       INT_ID1
 *  0x1a       N/A         POL_2       ENAB_2       INT_ID2
 */
#define PCMMIO_PORT_REG(x)			(0x10 + (x))
#define PCMMIO_INT_PENDING_REG			0x16
#define PCMMIO_PAGE_LOCK_REG			0x17
#define PCMMIO_LOCK_PORT(x)			((1 << (x)) & 0x3f)
#define PCMMIO_PAGE(x)				(((x) & 0x3) << 6)
#define PCMMIO_PAGE_MASK			PCMUIO_PAGE(3)
#define PCMMIO_PAGE_POL				1
#define PCMMIO_PAGE_ENAB			2
#define PCMMIO_PAGE_INT_ID			3
#define PCMMIO_PAGE_REG(x)			(0x18 + (x))

/* This stuff is all from pcmuio.c -- it refers to the DIO subdevices only */
#define CHANS_PER_PORT   8
#define PORTS_PER_ASIC   6
#define INTR_PORTS_PER_ASIC   3
#define MAX_CHANS_PER_SUBDEV 24	/* number of channels per comedi subdevice */
#define PORTS_PER_SUBDEV (MAX_CHANS_PER_SUBDEV/CHANS_PER_PORT)
#define CHANS_PER_ASIC (CHANS_PER_PORT*PORTS_PER_ASIC)
#define INTR_CHANS_PER_ASIC 24
#define INTR_PORTS_PER_SUBDEV (INTR_CHANS_PER_ASIC/CHANS_PER_PORT)
#define MAX_DIO_CHANS   (PORTS_PER_ASIC*1*CHANS_PER_PORT)
#define MAX_ASICS       (MAX_DIO_CHANS/CHANS_PER_ASIC)
#define CALC_N_DIO_SUBDEVS(nchans) ((nchans)/MAX_CHANS_PER_SUBDEV + (!!((nchans)%MAX_CHANS_PER_SUBDEV)) /*+ (nchans > INTR_CHANS_PER_ASIC ? 2 : 1)*/)
/* IO Memory sizes */
#define ASIC_IOSIZE (0x0B)
#define PCMMIO48_IOSIZE ASIC_IOSIZE

#define NUM_PAGED_REGS 3
#define NUM_PAGES 4
#define FIRST_PAGED_REG 0x8

static const struct comedi_lrange pcmmio_ai_ranges = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(10)
	}
};

static const struct comedi_lrange pcmmio_ao_ranges = {
	6, {
		UNI_RANGE(5),
		UNI_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(10),
		BIP_RANGE(2.5),
		RANGE(-2.5, 7.5)
	}
};

/* this structure is for data unique to this subdevice.  */
struct pcmmio_subdev_private {

	union {
		struct {

			/* The below is only used for intr subdevices */
			struct {
				/*
				 * if non-negative, this subdev has an
				 * interrupt asic
				 */
				int asic;
				/*
				 * if nonnegative, the first channel id for
				 * interrupts.
				 */
				int first_chan;
				/*
				 * the number of asic channels in this subdev
				 * that have interrutps
				 */
				int num_asic_chans;
				/*
				 * if nonnegative, the first channel id with
				 * respect to the asic that has interrupts
				 */
				int asic_chan;
				/*
				 * subdev-relative channel mask for channels
				 * we are interested in
				 */
				int enabled_mask;
				int active;
				int stop_count;
				int continuous;
				spinlock_t spinlock;
			} intr;
		} dio;
	};
};

/*
 * this structure is for data unique to this hardware driver.  If
 * several hardware drivers keep similar information in this structure,
 * feel free to suggest moving the variable to the struct comedi_device struct.
 */
struct pcmmio_private {
	spinlock_t pagelock;	/* protects the page registers */

	struct pcmmio_subdev_private *sprivs;
	unsigned int ao_readback[8];
};

static void pcmmio_dio_write(struct comedi_device *dev, unsigned int val,
			     int page, int port)
{
	struct pcmmio_private *devpriv = dev->private;
	unsigned long iobase = dev->iobase;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->pagelock, flags);
	if (page == 0) {
		/* Port registers are valid for any page */
		outb(val & 0xff, iobase + PCMMIO_PORT_REG(port + 0));
		outb((val >> 8) & 0xff, iobase + PCMMIO_PORT_REG(port + 1));
		outb((val >> 16) & 0xff, iobase + PCMMIO_PORT_REG(port + 2));
	} else {
		outb(PCMMIO_PAGE(page), iobase + PCMMIO_PAGE_LOCK_REG);
		outb(val & 0xff, iobase + PCMMIO_PAGE_REG(0));
		outb((val >> 8) & 0xff, iobase + PCMMIO_PAGE_REG(1));
		outb((val >> 16) & 0xff, iobase + PCMMIO_PAGE_REG(2));
	}
	spin_unlock_irqrestore(&devpriv->pagelock, flags);
}

static unsigned int pcmmio_dio_read(struct comedi_device *dev,
				    int page, int port)
{
	struct pcmmio_private *devpriv = dev->private;
	unsigned long iobase = dev->iobase;
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&devpriv->pagelock, flags);
	if (page == 0) {
		/* Port registers are valid for any page */
		val = inb(iobase + PCMMIO_PORT_REG(port + 0));
		val |= (inb(iobase + PCMMIO_PORT_REG(port + 1)) << 8);
		val |= (inb(iobase + PCMMIO_PORT_REG(port + 2)) << 16);
	} else {
		outb(PCMMIO_PAGE(page), iobase + PCMMIO_PAGE_LOCK_REG);
		val = inb(iobase + PCMMIO_PAGE_REG(0));
		val |= (inb(iobase + PCMMIO_PAGE_REG(1)) << 8);
		val |= (inb(iobase + PCMMIO_PAGE_REG(2)) << 16);
	}
	spin_unlock_irqrestore(&devpriv->pagelock, flags);

	return val;
}

/*
 * Each channel can be individually programmed for input or output.
 * Writing a '0' to a channel causes the corresponding output pin
 * to go to a high-z state (pulled high by an external 10K resistor).
 * This allows it to be used as an input. When used in the input mode,
 * a read reflects the inverted state of the I/O pin, such that a
 * high on the pin will read as a '0' in the register. Writing a '1'
 * to a bit position causes the pin to sink current (up to 12mA),
 * effectively pulling it low.
 */
static int pcmmio_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	/* subdevice 2 uses ports 0-2, subdevice 3 uses ports 3-5 */
	int port = s->index == 2 ? 0 : 3;
	unsigned int chanmask = (1 << s->n_chan) - 1;
	unsigned int mask;
	unsigned int val;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		/*
		 * Outputs are inverted, invert the state and
		 * update the channels.
		 *
		 * The s->io_bits mask makes sure the input channels
		 * are '0' so that the outputs pins stay in a high
		 * z-state.
		 */
		val = ~s->state & chanmask;
		val &= s->io_bits;
		pcmmio_dio_write(dev, val, 0, port);
	}

	/* get inverted state of the channels from the port */
	val = pcmmio_dio_read(dev, 0, port);

	/* return the true state of the channels */
	data[1] = ~val & chanmask;

	return insn->n;
}

static int pcmmio_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	/* subdevice 2 uses ports 0-2, subdevice 3 uses ports 3-5 */
	int port = s->index == 2 ? 0 : 3;
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	if (data[0] == INSN_CONFIG_DIO_INPUT)
		pcmmio_dio_write(dev, s->io_bits, 0, port);

	return insn->n;
}

static void switch_page(struct comedi_device *dev, int page)
{
	outb(PCMMIO_PAGE(page), dev->iobase + PCMMIO_PAGE_LOCK_REG);
}

static void pcmmio_reset(struct comedi_device *dev)
{
	/* Clear all the DIO port bits */
	pcmmio_dio_write(dev, 0, 0, 0);
	pcmmio_dio_write(dev, 0, 0, 3);

	/* Clear all the paged registers */
	pcmmio_dio_write(dev, 0, PCMMIO_PAGE_POL, 0);
	pcmmio_dio_write(dev, 0, PCMMIO_PAGE_ENAB, 0);
	pcmmio_dio_write(dev, 0, PCMMIO_PAGE_INT_ID, 0);
}

static void pcmmio_stop_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pcmmio_subdev_private *subpriv = s->private;

	subpriv->dio.intr.enabled_mask = 0;
	subpriv->dio.intr.active = 0;
	s->async->inttrig = NULL;

	/* disable all dio interrupts */
	pcmmio_dio_write(dev, 0, PCMMIO_PAGE_ENAB, 0);
}

static irqreturn_t interrupt_pcmmio(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pcmmio_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct pcmmio_subdev_private *subpriv = s->private;
	int asic, got1 = 0;

	for (asic = 0; asic < MAX_ASICS; ++asic) {
		if (irq == dev->irq) {
			unsigned long flags;
			unsigned triggered = 0;
			/* it is an interrupt for ASIC #asic */
			unsigned char int_pend;

			spin_lock_irqsave(&devpriv->pagelock, flags);

			int_pend = inb(dev->iobase + PCMMIO_INT_PENDING_REG);
			int_pend &= 0x07;

			if (int_pend) {
				int port;
				for (port = 0; port < INTR_PORTS_PER_ASIC;
				     ++port) {
					if (int_pend & (0x1 << port)) {
						unsigned char
						    io_lines_with_edges = 0;
						switch_page(dev, PCMMIO_PAGE_INT_ID);
						io_lines_with_edges =
						    inb(dev->iobase +
							PCMMIO_PAGE_REG(port));

						if (io_lines_with_edges)
							/*
							 * clear pending
							 * interrupt
							 */
							outb(0, dev->iobase +
							     PCMMIO_PAGE_REG(port));

						triggered |=
						    io_lines_with_edges <<
						    port * 8;
					}
				}

				++got1;
			}

			spin_unlock_irqrestore(&devpriv->pagelock, flags);

			if (triggered) {
				/*
				 * TODO here: dispatch io lines to subdevs
				 * with commands..
				 */
					/*
					 * this is an interrupt subdev,
					 * and it matches this asic!
					 */
					if (subpriv->dio.intr.asic == asic) {
						unsigned long flags;
						unsigned oldevents;

						spin_lock_irqsave(&subpriv->dio.
								  intr.spinlock,
								  flags);

						oldevents = s->async->events;

						if (subpriv->dio.intr.active) {
							unsigned mytrig =
							    ((triggered >>
							      subpriv->dio.intr.asic_chan)
							     &
							     ((0x1 << subpriv->
							       dio.intr.
							       num_asic_chans) -
							      1)) << subpriv->
							    dio.intr.first_chan;
							if (mytrig &
							    subpriv->dio.
							    intr.enabled_mask) {
								unsigned int val
								    = 0;
								unsigned int n,
								    ch, len;

								len =
								    s->
								    async->cmd.chanlist_len;
								for (n = 0;
								     n < len;
								     n++) {
									ch = CR_CHAN(s->async->cmd.chanlist[n]);
									if (mytrig & (1U << ch))
										val |= (1U << n);
								}
								/* Write the scan to the buffer. */
								if (comedi_buf_put(s->async, val)
								    &&
								    comedi_buf_put
								    (s->async,
								     val >> 16)) {
									s->async->events |= (COMEDI_CB_BLOCK | COMEDI_CB_EOS);
								} else {
									/* Overflow! Stop acquisition!! */
									/* TODO: STOP_ACQUISITION_CALL_HERE!! */
									pcmmio_stop_intr
									    (dev,
									     s);
								}

								/* Check for end of acquisition. */
								if (!subpriv->dio.intr.continuous) {
									/* stop_src == TRIG_COUNT */
									if (subpriv->dio.intr.stop_count > 0) {
										subpriv->dio.intr.stop_count--;
										if (subpriv->dio.intr.stop_count == 0) {
											s->async->events |= COMEDI_CB_EOA;
											/* TODO: STOP_ACQUISITION_CALL_HERE!! */
											pcmmio_stop_intr
											    (dev,
											     s);
										}
									}
								}
							}
						}

						spin_unlock_irqrestore
						    (&subpriv->dio.intr.
						     spinlock, flags);

						if (oldevents !=
						    s->async->events) {
							comedi_event(dev, s);
						}

					}

			}

		}
	}
	if (!got1)
		return IRQ_NONE;	/* interrupt from other source */
	return IRQ_HANDLED;
}

static int pcmmio_start_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pcmmio_subdev_private *subpriv = s->private;

	if (!subpriv->dio.intr.continuous && subpriv->dio.intr.stop_count == 0) {
		/* An empty acquisition! */
		s->async->events |= COMEDI_CB_EOA;
		subpriv->dio.intr.active = 0;
		return 1;
	} else {
		unsigned bits = 0, pol_bits = 0, n;
		int nports, firstport, asic, port;
		struct comedi_cmd *cmd = &s->async->cmd;

		asic = subpriv->dio.intr.asic;
		if (asic < 0)
			return 1;	/* not an interrupt
					   subdev */
		subpriv->dio.intr.enabled_mask = 0;
		subpriv->dio.intr.active = 1;
		nports = subpriv->dio.intr.num_asic_chans / CHANS_PER_PORT;
		firstport = subpriv->dio.intr.asic_chan / CHANS_PER_PORT;
		if (cmd->chanlist) {
			for (n = 0; n < cmd->chanlist_len; n++) {
				bits |= (1U << CR_CHAN(cmd->chanlist[n]));
				pol_bits |= (CR_AREF(cmd->chanlist[n])
					     || CR_RANGE(cmd->
							 chanlist[n]) ? 1U : 0U)
				    << CR_CHAN(cmd->chanlist[n]);
			}
		}
		bits &= ((0x1 << subpriv->dio.intr.num_asic_chans) -
			 1) << subpriv->dio.intr.first_chan;
		subpriv->dio.intr.enabled_mask = bits;

		{
			/*
			 * the below code configures the board
			 * to use a specific IRQ from 0-15.
			 */
			unsigned char b;
			/*
			 * set resource enable register
			 * to enable IRQ operation
			 */
			outb(1 << 4, dev->iobase + 3);
			/* set bits 0-3 of b to the irq number from 0-15 */
			b = dev->irq & ((1 << 4) - 1);
			outb(b, dev->iobase + 2);
			/* done, we told the board what irq to use */
		}

		switch_page(dev, PCMMIO_PAGE_ENAB);
		for (port = firstport; port < firstport + nports; ++port) {
			unsigned enab =
			    bits >> (subpriv->dio.intr.first_chan + (port -
								     firstport)
				     * 8) & 0xff, pol =
			    pol_bits >> (subpriv->dio.intr.first_chan +
					 (port - firstport) * 8) & 0xff;
			/* set enab intrs for this subdev.. */
			outb(enab, dev->iobase + PCMMIO_PAGE_REG(port));
			switch_page(dev, PCMMIO_PAGE_POL);
			outb(pol, dev->iobase + PCMMIO_PAGE_REG(port));
		}
	}
	return 0;
}

static int pcmmio_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcmmio_subdev_private *subpriv = s->private;
	unsigned long flags;

	spin_lock_irqsave(&subpriv->dio.intr.spinlock, flags);
	if (subpriv->dio.intr.active)
		pcmmio_stop_intr(dev, s);
	spin_unlock_irqrestore(&subpriv->dio.intr.spinlock, flags);

	return 0;
}

/*
 * Internal trigger function to start acquisition for an 'INTERRUPT' subdevice.
 */
static int
pcmmio_inttrig_start_intr(struct comedi_device *dev, struct comedi_subdevice *s,
			  unsigned int trignum)
{
	struct pcmmio_subdev_private *subpriv = s->private;
	unsigned long flags;
	int event = 0;

	if (trignum != 0)
		return -EINVAL;

	spin_lock_irqsave(&subpriv->dio.intr.spinlock, flags);
	s->async->inttrig = NULL;
	if (subpriv->dio.intr.active)
		event = pcmmio_start_intr(dev, s);
	spin_unlock_irqrestore(&subpriv->dio.intr.spinlock, flags);

	if (event)
		comedi_event(dev, s);

	return 1;
}

/*
 * 'do_cmd' function for an 'INTERRUPT' subdevice.
 */
static int pcmmio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcmmio_subdev_private *subpriv = s->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned long flags;
	int event = 0;

	spin_lock_irqsave(&subpriv->dio.intr.spinlock, flags);
	subpriv->dio.intr.active = 1;

	/* Set up end of acquisition. */
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		subpriv->dio.intr.continuous = 0;
		subpriv->dio.intr.stop_count = cmd->stop_arg;
		break;
	default:
		/* TRIG_NONE */
		subpriv->dio.intr.continuous = 1;
		subpriv->dio.intr.stop_count = 0;
		break;
	}

	/* Set up start of acquisition. */
	switch (cmd->start_src) {
	case TRIG_INT:
		s->async->inttrig = pcmmio_inttrig_start_intr;
		break;
	default:
		/* TRIG_NOW */
		event = pcmmio_start_intr(dev, s);
		break;
	}
	spin_unlock_irqrestore(&subpriv->dio.intr.spinlock, flags);

	if (event)
		comedi_event(dev, s);

	return 0;
}

static int pcmmio_cmdtest(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		/* any count allowed */
		break;
	case TRIG_NONE:
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	/* if (err) return 4; */

	return 0;
}

static int pcmmio_ai_wait_for_eoc(unsigned long iobase, unsigned int timeout)
{
	unsigned char status;

	while (timeout--) {
		status = inb(iobase + PCMMIO_AI_STATUS_REG);
		if (status & PCMMIO_AI_STATUS_DATA_READY)
			return 0;
	}
	return -ETIME;
}

static int pcmmio_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned long iobase = dev->iobase;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int aref = CR_AREF(insn->chanspec);
	unsigned char cmd = 0;
	unsigned int val;
	int ret;
	int i;

	/*
	 * The PCM-MIO uses two Linear Tech LTC1859CG 8-channel A/D converters.
	 * The devices use a full duplex serial interface which transmits and
	 * receives data simultaneously. An 8-bit command is shifted into the
	 * ADC interface to configure it for the next conversion. At the same
	 * time, the data from the previous conversion is shifted out of the
	 * device. Consequently, the conversion result is delayed by one
	 * conversion from the command word.
	 *
	 * Setup the cmd for the conversions then do a dummy conversion to
	 * flush the junk data. Then do each conversion requested by the
	 * comedi_insn. Note that the last conversion will leave junk data
	 * in ADC which will get flushed on the next comedi_insn.
	 */

	if (chan > 7) {
		chan -= 8;
		iobase += PCMMIO_AI_2ND_ADC_OFFSET;
	}

	if (aref == AREF_GROUND)
		cmd |= PCMMIO_AI_CMD_SE;
	if (chan % 2)
		cmd |= PCMMIO_AI_CMD_ODD_CHAN;
	cmd |= PCMMIO_AI_CMD_CHAN_SEL(chan / 2);
	cmd |= PCMMIO_AI_CMD_RANGE(range);

	outb(cmd, iobase + PCMMIO_AI_CMD_REG);
	ret = pcmmio_ai_wait_for_eoc(iobase, 100000);
	if (ret)
		return ret;

	val = inb(iobase + PCMMIO_AI_LSB_REG);
	val |= inb(iobase + PCMMIO_AI_MSB_REG) << 8;

	for (i = 0; i < insn->n; i++) {
		outb(cmd, iobase + PCMMIO_AI_CMD_REG);
		ret = pcmmio_ai_wait_for_eoc(iobase, 100000);
		if (ret)
			return ret;

		val = inb(iobase + PCMMIO_AI_LSB_REG);
		val |= inb(iobase + PCMMIO_AI_MSB_REG) << 8;

		/* bipolar data is two's complement */
		if (comedi_range_is_bipolar(s, range))
			val = comedi_offset_munge(s, val);

		data[i] = val;
	}

	return insn->n;
}

static int pcmmio_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct pcmmio_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return insn->n;
}

static int pcmmio_ao_wait_for_eoc(unsigned long iobase, unsigned int timeout)
{
	unsigned char status;

	while (timeout--) {
		status = inb(iobase + PCMMIO_AO_STATUS_REG);
		if (status & PCMMIO_AO_STATUS_DATA_READY)
			return 0;
	}
	return -ETIME;
}

static int pcmmio_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct pcmmio_private *devpriv = dev->private;
	unsigned long iobase = dev->iobase;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val = devpriv->ao_readback[chan];
	unsigned char cmd = 0;
	int ret;
	int i;

	/*
	 * The PCM-MIO has two Linear Tech LTC2704 DAC devices. Each device
	 * is a 4-channel converter with software-selectable output range.
	 */

	if (chan > 3) {
		cmd |= PCMMIO_AO_CMD_CHAN_SEL(chan - 4);
		iobase += PCMMIO_AO_2ND_DAC_OFFSET;
	} else {
		cmd |= PCMMIO_AO_CMD_CHAN_SEL(chan);
	}

	/* set the range for the channel */
	outb(PCMMIO_AO_LSB_SPAN(range), iobase + PCMMIO_AO_LSB_REG);
	outb(0, iobase + PCMMIO_AO_MSB_REG);
	outb(cmd | PCMMIO_AO_CMD_WR_SPAN_UPDATE, iobase + PCMMIO_AO_CMD_REG);
	ret = pcmmio_ao_wait_for_eoc(iobase, 100000);
	if (ret)
		return ret;

	for (i = 0; i < insn->n; i++) {
		val = data[i];

		/* write the data to the channel */
		outb(val & 0xff, iobase + PCMMIO_AO_LSB_REG);
		outb((val >> 8) & 0xff, iobase + PCMMIO_AO_MSB_REG);
		outb(cmd | PCMMIO_AO_CMD_WR_CODE_UPDATE,
		     iobase + PCMMIO_AO_CMD_REG);
		ret = pcmmio_ao_wait_for_eoc(iobase, 100000);
		if (ret)
			return ret;

		devpriv->ao_readback[chan] = val;
	}

	return insn->n;
}

static int pcmmio_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct pcmmio_private *devpriv;
	struct pcmmio_subdev_private *subpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 32);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	spin_lock_init(&devpriv->pagelock);

	devpriv->sprivs = kcalloc(4, sizeof(struct pcmmio_subdev_private),
				  GFP_KERNEL);
	if (!devpriv->sprivs)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan	= 16;
	s->maxdata	= 0xffff;
	s->range_table	= &pcmmio_ai_ranges;
	s->insn_read	= pcmmio_ai_insn_read;

	/* initialize the resource enable register by clearing it */
	outb(0, dev->iobase + PCMMIO_AI_RESOURCE_REG);
	outb(0,
	     dev->iobase + PCMMIO_AI_2ND_ADC_OFFSET + PCMMIO_AI_RESOURCE_REG);

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 8;
	s->maxdata	= 0xffff;
	s->range_table	= &pcmmio_ao_ranges;
	s->insn_read	= pcmmio_ao_insn_read;
	s->insn_write	= pcmmio_ao_insn_write;

	/* initialize the resource enable register by clearing it */
	outb(0, dev->iobase + PCMMIO_AO_RESOURCE_ENA_REG);
	outb(0, dev->iobase + PCMMIO_AO_2ND_DAC_OFFSET +
		PCMMIO_AO_RESOURCE_ENA_REG);

	/* Digital I/O subdevice with interrupt support */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 24;
	s->maxdata	= 1;
	s->len_chanlist	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pcmmio_dio_insn_bits;
	s->insn_config	= pcmmio_dio_insn_config;

	dev->read_subdev = s;
	s->subdev_flags	|= SDF_CMD_READ;
	s->len_chanlist	= s->n_chan;
	s->cancel	= pcmmio_cancel;
	s->do_cmd	= pcmmio_cmd;
	s->do_cmdtest	= pcmmio_cmdtest;

	s->private = &devpriv->sprivs[2];
	subpriv = s->private;
	subpriv->dio.intr.asic = 0;
	subpriv->dio.intr.active = 0;
	subpriv->dio.intr.stop_count = 0;
	subpriv->dio.intr.first_chan = 0;
	subpriv->dio.intr.asic_chan = 0;
	subpriv->dio.intr.num_asic_chans = 24;

	spin_lock_init(&subpriv->dio.intr.spinlock);

	/* Digital I/O subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 24;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pcmmio_dio_insn_bits;
	s->insn_config	= pcmmio_dio_insn_config;

	pcmmio_reset(dev);

	if (it->options[1]) {
		ret = request_irq(it->options[1], interrupt_pcmmio, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	return 1;
}

static void pcmmio_detach(struct comedi_device *dev)
{
	struct pcmmio_private *devpriv = dev->private;

	if (devpriv)
		kfree(devpriv->sprivs);
	comedi_legacy_detach(dev);
}

static struct comedi_driver pcmmio_driver = {
	.driver_name	= "pcmmio",
	.module		= THIS_MODULE,
	.attach		= pcmmio_attach,
	.detach		= pcmmio_detach,
};
module_comedi_driver(pcmmio_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
