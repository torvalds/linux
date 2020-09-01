// SPDX-License-Identifier: GPL-2.0
/*
 * comedi/drivers/pcl812.c
 *
 * Author: Michal Dobes <dobes@tesnet.cz>
 *
 * hardware driver for Advantech cards
 *  card:   PCL-812, PCL-812PG, PCL-813, PCL-813B
 *  driver: pcl812,  pcl812pg,  pcl813,  pcl813b
 * and for ADlink cards
 *  card:   ACL-8112DG, ACL-8112HG, ACL-8112PG, ACL-8113, ACL-8216
 *  driver: acl8112dg,  acl8112hg,  acl8112pg,  acl8113,  acl8216
 * and for ICP DAS cards
 *  card:   ISO-813, A-821PGH, A-821PGL, A-821PGL-NDA, A-822PGH, A-822PGL,
 *  driver: iso813,  a821pgh,  a-821pgl, a-821pglnda,  a822pgh,  a822pgl,
 *  card:   A-823PGH, A-823PGL, A-826PG
 * driver:  a823pgh,  a823pgl,  a826pg
 */

/*
 * Driver: pcl812
 * Description: Advantech PCL-812/PG, PCL-813/B,
 *	     ADLink ACL-8112DG/HG/PG, ACL-8113, ACL-8216,
 *	     ICP DAS A-821PGH/PGL/PGL-NDA, A-822PGH/PGL, A-823PGH/PGL, A-826PG,
 *	     ICP DAS ISO-813
 * Author: Michal Dobes <dobes@tesnet.cz>
 * Devices: [Advantech] PCL-812 (pcl812), PCL-812PG (pcl812pg),
 *	PCL-813 (pcl813), PCL-813B (pcl813b), [ADLink] ACL-8112DG (acl8112dg),
 *	ACL-8112HG (acl8112hg), ACL-8113 (acl-8113), ACL-8216 (acl8216),
 *	[ICP] ISO-813 (iso813), A-821PGH (a821pgh), A-821PGL (a821pgl),
 *	A-821PGL-NDA (a821pclnda), A-822PGH (a822pgh), A-822PGL (a822pgl),
 *	A-823PGH (a823pgh), A-823PGL (a823pgl), A-826PG (a826pg)
 * Updated: Mon, 06 Aug 2007 12:03:15 +0100
 * Status: works (I hope. My board fire up under my hands
 *	       and I cann't test all features.)
 *
 * This driver supports insn and cmd interfaces. Some boards support only insn
 * because their hardware don't allow more (PCL-813/B, ACL-8113, ISO-813).
 * Data transfer over DMA is supported only when you measure only one
 * channel, this is too hardware limitation of these boards.
 *
 * Options for PCL-812:
 *   [0] - IO Base
 *   [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7; 10, 11, 12, 14, 15)
 *   [2] - DMA  (0=disable, 1, 3)
 *   [3] - 0=trigger source is internal 8253 with 2MHz clock
 *         1=trigger source is external
 *   [4] - 0=A/D input range is +/-10V
 *	   1=A/D input range is +/-5V
 *	   2=A/D input range is +/-2.5V
 *	   3=A/D input range is +/-1.25V
 *	   4=A/D input range is +/-0.625V
 *	   5=A/D input range is +/-0.3125V
 *   [5] - 0=D/A outputs 0-5V  (internal reference -5V)
 *	   1=D/A outputs 0-10V (internal reference -10V)
 *	   2=D/A outputs unknown (external reference)
 *
 * Options for PCL-812PG, ACL-8112PG:
 *   [0] - IO Base
 *   [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7; 10, 11, 12, 14, 15)
 *   [2] - DMA  (0=disable, 1, 3)
 *   [3] - 0=trigger source is internal 8253 with 2MHz clock
 *	   1=trigger source is external
 *   [4] - 0=A/D have max +/-5V input
 *	   1=A/D have max +/-10V input
 *   [5] - 0=D/A outputs 0-5V  (internal reference -5V)
 *	   1=D/A outputs 0-10V (internal reference -10V)
 *	   2=D/A outputs unknown (external reference)
 *
 * Options for ACL-8112DG/HG, A-822PGL/PGH, A-823PGL/PGH, ACL-8216, A-826PG:
 *   [0] - IO Base
 *   [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7; 10, 11, 12, 14, 15)
 *   [2] - DMA  (0=disable, 1, 3)
 *   [3] - 0=trigger source is internal 8253 with 2MHz clock
 *	   1=trigger source is external
 *   [4] - 0=A/D channels are S.E.
 *	   1=A/D channels are DIFF
 *   [5] - 0=D/A outputs 0-5V  (internal reference -5V)
 *	   1=D/A outputs 0-10V (internal reference -10V)
 *	   2=D/A outputs unknown (external reference)
 *
 * Options for A-821PGL/PGH:
 *   [0] - IO Base
 *   [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7)
 *   [2] - 0=A/D channels are S.E.
 *	   1=A/D channels are DIFF
 *   [3] - 0=D/A output 0-5V  (internal reference -5V)
 *	   1=D/A output 0-10V (internal reference -10V)
 *
 * Options for A-821PGL-NDA:
 *   [0] - IO Base
 *   [1] - IRQ  (0=disable, 2, 3, 4, 5, 6, 7)
 *   [2] - 0=A/D channels are S.E.
 *	   1=A/D channels are DIFF
 *
 * Options for PCL-813:
 *   [0] - IO Base
 *
 * Options for PCL-813B:
 *   [0] - IO Base
 *   [1] - 0= bipolar inputs
 *	   1= unipolar inputs
 *
 * Options for ACL-8113, ISO-813:
 *   [0] - IO Base
 *   [1] - 0= 10V bipolar inputs
 *	   1= 10V unipolar inputs
 *	   2= 20V bipolar inputs
 *	   3= 20V unipolar inputs
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "../comedidev.h"

#include "comedi_isadma.h"
#include "comedi_8254.h"

/*
 * Register I/O map
 */
#define PCL812_TIMER_BASE			0x00
#define PCL812_AI_LSB_REG			0x04
#define PCL812_AI_MSB_REG			0x05
#define PCL812_AI_MSB_DRDY			BIT(4)
#define PCL812_AO_LSB_REG(x)			(0x04 + ((x) * 2))
#define PCL812_AO_MSB_REG(x)			(0x05 + ((x) * 2))
#define PCL812_DI_LSB_REG			0x06
#define PCL812_DI_MSB_REG			0x07
#define PCL812_STATUS_REG			0x08
#define PCL812_STATUS_DRDY			BIT(5)
#define PCL812_RANGE_REG			0x09
#define PCL812_MUX_REG				0x0a
#define PCL812_MUX_CHAN(x)			((x) << 0)
#define PCL812_MUX_CS0				BIT(4)
#define PCL812_MUX_CS1				BIT(5)
#define PCL812_CTRL_REG				0x0b
#define PCL812_CTRL_TRIG(x)			(((x) & 0x7) << 0)
#define PCL812_CTRL_DISABLE_TRIG		PCL812_CTRL_TRIG(0)
#define PCL812_CTRL_SOFT_TRIG			PCL812_CTRL_TRIG(1)
#define PCL812_CTRL_PACER_DMA_TRIG		PCL812_CTRL_TRIG(2)
#define PCL812_CTRL_PACER_EOC_TRIG		PCL812_CTRL_TRIG(6)
#define PCL812_SOFTTRIG_REG			0x0c
#define PCL812_DO_LSB_REG			0x0d
#define PCL812_DO_MSB_REG			0x0e

#define MAX_CHANLIST_LEN    256	/* length of scan list */

static const struct comedi_lrange range_pcl812pg_ai = {
	5, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		BIP_RANGE(0.3125)
	}
};

static const struct comedi_lrange range_pcl812pg2_ai = {
	5, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625)
	}
};

static const struct comedi_lrange range812_bipolar1_25 = {
	1, {
		BIP_RANGE(1.25)
	}
};

static const struct comedi_lrange range812_bipolar0_625 = {
	1, {
		BIP_RANGE(0.625)
	}
};

static const struct comedi_lrange range812_bipolar0_3125 = {
	1, {
		BIP_RANGE(0.3125)
	}
};

static const struct comedi_lrange range_pcl813b_ai = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625)
	}
};

static const struct comedi_lrange range_pcl813b2_ai = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

static const struct comedi_lrange range_iso813_1_ai = {
	5, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		BIP_RANGE(0.3125)
	}
};

static const struct comedi_lrange range_iso813_1_2_ai = {
	5, {
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25),
		UNI_RANGE(0.625)
	}
};

static const struct comedi_lrange range_iso813_2_ai = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625)
	}
};

static const struct comedi_lrange range_iso813_2_2_ai = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

static const struct comedi_lrange range_acl8113_1_ai = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625)
	}
};

static const struct comedi_lrange range_acl8113_1_2_ai = {
	4, {
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

static const struct comedi_lrange range_acl8113_2_ai = {
	3, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25)
	}
};

static const struct comedi_lrange range_acl8113_2_2_ai = {
	3, {
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5)
	}
};

static const struct comedi_lrange range_acl8112dg_ai = {
	9, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25),
		BIP_RANGE(10)
	}
};

static const struct comedi_lrange range_acl8112hg_ai = {
	12, {
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05),
		BIP_RANGE(0.005),
		UNI_RANGE(10),
		UNI_RANGE(1),
		UNI_RANGE(0.1),
		UNI_RANGE(0.01),
		BIP_RANGE(10),
		BIP_RANGE(1),
		BIP_RANGE(0.1),
		BIP_RANGE(0.01)
	}
};

static const struct comedi_lrange range_a821pgh_ai = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05),
		BIP_RANGE(0.005)
	}
};

enum pcl812_boardtype {
	BOARD_PCL812PG	= 0,	/* and ACL-8112PG */
	BOARD_PCL813B	= 1,
	BOARD_PCL812	= 2,
	BOARD_PCL813	= 3,
	BOARD_ISO813	= 5,
	BOARD_ACL8113	= 6,
	BOARD_ACL8112	= 7,	/* ACL-8112DG/HG, A-822PGL/PGH, A-823PGL/PGH */
	BOARD_ACL8216	= 8,	/* and ICP DAS A-826PG */
	BOARD_A821	= 9,	/* PGH, PGL, PGL/NDA versions */
};

struct pcl812_board {
	const char *name;
	enum pcl812_boardtype board_type;
	int n_aichan;
	int n_aochan;
	unsigned int ai_ns_min;
	const struct comedi_lrange *rangelist_ai;
	unsigned int irq_bits;
	unsigned int has_dma:1;
	unsigned int has_16bit_ai:1;
	unsigned int has_mpc508_mux:1;
	unsigned int has_dio:1;
};

static const struct pcl812_board boardtypes[] = {
	{
		.name		= "pcl812",
		.board_type	= BOARD_PCL812,
		.n_aichan	= 16,
		.n_aochan	= 2,
		.ai_ns_min	= 33000,
		.rangelist_ai	= &range_bipolar10,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_dio	= 1,
	}, {
		.name		= "pcl812pg",
		.board_type	= BOARD_PCL812PG,
		.n_aichan	= 16,
		.n_aochan	= 2,
		.ai_ns_min	= 33000,
		.rangelist_ai	= &range_pcl812pg_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_dio	= 1,
	}, {
		.name		= "acl8112pg",
		.board_type	= BOARD_PCL812PG,
		.n_aichan	= 16,
		.n_aochan	= 2,
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_pcl812pg_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_dio	= 1,
	}, {
		.name		= "acl8112dg",
		.board_type	= BOARD_ACL8112,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 2,
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_acl8112dg_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_mpc508_mux	= 1,
		.has_dio	= 1,
	}, {
		.name		= "acl8112hg",
		.board_type	= BOARD_ACL8112,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 2,
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_acl8112hg_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_mpc508_mux	= 1,
		.has_dio	= 1,
	}, {
		.name		= "a821pgl",
		.board_type	= BOARD_A821,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 1,
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_pcl813b_ai,
		.irq_bits	= 0x000c,
		.has_dio	= 1,
	}, {
		.name		= "a821pglnda",
		.board_type	= BOARD_A821,
		.n_aichan	= 16,	/* 8 differential */
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_pcl813b_ai,
		.irq_bits	= 0x000c,
	}, {
		.name		= "a821pgh",
		.board_type	= BOARD_A821,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 1,
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_a821pgh_ai,
		.irq_bits	= 0x000c,
		.has_dio	= 1,
	}, {
		.name		= "a822pgl",
		.board_type	= BOARD_ACL8112,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 2,
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_acl8112dg_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_dio	= 1,
	}, {
		.name		= "a822pgh",
		.board_type	= BOARD_ACL8112,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 2,
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_acl8112hg_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_dio	= 1,
	}, {
		.name		= "a823pgl",
		.board_type	= BOARD_ACL8112,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 2,
		.ai_ns_min	= 8000,
		.rangelist_ai	= &range_acl8112dg_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_dio	= 1,
	}, {
		.name		= "a823pgh",
		.board_type	= BOARD_ACL8112,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 2,
		.ai_ns_min	= 8000,
		.rangelist_ai	= &range_acl8112hg_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_dio	= 1,
	}, {
		.name		= "pcl813",
		.board_type	= BOARD_PCL813,
		.n_aichan	= 32,
		.rangelist_ai	= &range_pcl813b_ai,
	}, {
		.name		= "pcl813b",
		.board_type	= BOARD_PCL813B,
		.n_aichan	= 32,
		.rangelist_ai	= &range_pcl813b_ai,
	}, {
		.name		= "acl8113",
		.board_type	= BOARD_ACL8113,
		.n_aichan	= 32,
		.rangelist_ai	= &range_acl8113_1_ai,
	}, {
		.name		= "iso813",
		.board_type	= BOARD_ISO813,
		.n_aichan	= 32,
		.rangelist_ai	= &range_iso813_1_ai,
	}, {
		.name		= "acl8216",
		.board_type	= BOARD_ACL8216,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 2,
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_pcl813b2_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_16bit_ai	= 1,
		.has_mpc508_mux	= 1,
		.has_dio	= 1,
	}, {
		.name		= "a826pg",
		.board_type	= BOARD_ACL8216,
		.n_aichan	= 16,	/* 8 differential */
		.n_aochan	= 2,
		.ai_ns_min	= 10000,
		.rangelist_ai	= &range_pcl813b2_ai,
		.irq_bits	= 0xdcfc,
		.has_dma	= 1,
		.has_16bit_ai	= 1,
		.has_dio	= 1,
	},
};

struct pcl812_private {
	struct comedi_isadma *dma;
	unsigned char range_correction;	/* =1 we must add 1 to range number */
	unsigned int last_ai_chanspec;
	unsigned char mode_reg_int; /* stored INT number for some cards */
	unsigned int ai_poll_ptr; /* how many samples transfer poll */
	unsigned int max_812_ai_mode0_rangewait; /* settling time for gain */
	unsigned int use_diff:1;
	unsigned int use_mpc508:1;
	unsigned int use_ext_trg:1;
	unsigned int ai_dma:1;
	unsigned int ai_eos:1;
};

static void pcl812_ai_setup_dma(struct comedi_device *dev,
				struct comedi_subdevice *s,
				unsigned int unread_samples)
{
	struct pcl812_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[dma->cur_dma];
	unsigned int bytes;
	unsigned int max_samples;
	unsigned int nsamples;

	comedi_isadma_disable(dma->chan);

	/* if using EOS, adapt DMA buffer to one scan */
	bytes = devpriv->ai_eos ? comedi_bytes_per_scan(s) : desc->maxsize;
	max_samples = comedi_bytes_to_samples(s, bytes);

	/*
	 * Determine dma size based on the buffer size plus the number of
	 * unread samples and the number of samples remaining in the command.
	 */
	nsamples = comedi_nsamples_left(s, max_samples + unread_samples);
	if (nsamples > unread_samples) {
		nsamples -= unread_samples;
		desc->size = comedi_samples_to_bytes(s, nsamples);
		comedi_isadma_program(desc);
	}
}

static void pcl812_ai_set_chan_range(struct comedi_device *dev,
				     unsigned int chanspec, char wait)
{
	struct pcl812_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(chanspec);
	unsigned int range = CR_RANGE(chanspec);
	unsigned int mux = 0;

	if (chanspec == devpriv->last_ai_chanspec)
		return;

	devpriv->last_ai_chanspec = chanspec;

	if (devpriv->use_mpc508) {
		if (devpriv->use_diff) {
			mux |= PCL812_MUX_CS0 | PCL812_MUX_CS1;
		} else {
			if (chan < 8)
				mux |= PCL812_MUX_CS0;
			else
				mux |= PCL812_MUX_CS1;
		}
	}

	outb(mux | PCL812_MUX_CHAN(chan), dev->iobase + PCL812_MUX_REG);
	outb(range + devpriv->range_correction, dev->iobase + PCL812_RANGE_REG);

	if (wait)
		/*
		 * XXX this depends on selected range and can be very long for
		 * some high gain ranges!
		 */
		udelay(devpriv->max_812_ai_mode0_rangewait);
}

static void pcl812_ai_clear_eoc(struct comedi_device *dev)
{
	/* writing any value clears the interrupt request */
	outb(0, dev->iobase + PCL812_STATUS_REG);
}

static void pcl812_ai_soft_trig(struct comedi_device *dev)
{
	/* writing any value triggers a software conversion */
	outb(255, dev->iobase + PCL812_SOFTTRIG_REG);
}

static unsigned int pcl812_ai_get_sample(struct comedi_device *dev,
					 struct comedi_subdevice *s)
{
	unsigned int val;

	val = inb(dev->iobase + PCL812_AI_MSB_REG) << 8;
	val |= inb(dev->iobase + PCL812_AI_LSB_REG);

	return val & s->maxdata;
}

static int pcl812_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	if (s->maxdata > 0x0fff) {
		status = inb(dev->iobase + PCL812_STATUS_REG);
		if ((status & PCL812_STATUS_DRDY) == 0)
			return 0;
	} else {
		status = inb(dev->iobase + PCL812_AI_MSB_REG);
		if ((status & PCL812_AI_MSB_DRDY) == 0)
			return 0;
	}
	return -EBUSY;
}

static int pcl812_ai_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	const struct pcl812_board *board = dev->board_ptr;
	struct pcl812_private *devpriv = dev->private;
	int err = 0;
	unsigned int flags;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_FOLLOW);

	if (devpriv->use_ext_trg)
		flags = TRIG_EXT;
	else
		flags = TRIG_TIMER;
	err |= comedi_check_trigger_src(&cmd->convert_src, flags);

	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	if (cmd->convert_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
						    board->ai_ns_min);
	} else {	/* TRIG_EXT */
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	}

	err |= comedi_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		unsigned int arg = cmd->convert_arg;

		comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);
	}

	if (err)
		return 4;

	return 0;
}

static int pcl812_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcl812_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int ctrl = 0;
	unsigned int i;

	pcl812_ai_set_chan_range(dev, cmd->chanlist[0], 1);

	if (dma) {	/*  check if we can use DMA transfer */
		devpriv->ai_dma = 1;
		for (i = 1; i < cmd->chanlist_len; i++)
			if (cmd->chanlist[0] != cmd->chanlist[i]) {
				/*  we cann't use DMA :-( */
				devpriv->ai_dma = 0;
				break;
			}
	} else {
		devpriv->ai_dma = 0;
	}

	devpriv->ai_poll_ptr = 0;

	/*  don't we want wake up every scan? */
	if (cmd->flags & CMDF_WAKE_EOS) {
		devpriv->ai_eos = 1;

		/*  DMA is useless for this situation */
		if (cmd->chanlist_len == 1)
			devpriv->ai_dma = 0;
	}

	if (devpriv->ai_dma) {
		/* setup and enable dma for the first buffer */
		dma->cur_dma = 0;
		pcl812_ai_setup_dma(dev, s, 0);
	}

	switch (cmd->convert_src) {
	case TRIG_TIMER:
		comedi_8254_update_divisors(dev->pacer);
		comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
		break;
	}

	if (devpriv->ai_dma)
		ctrl |= PCL812_CTRL_PACER_DMA_TRIG;
	else
		ctrl |= PCL812_CTRL_PACER_EOC_TRIG;
	outb(devpriv->mode_reg_int | ctrl, dev->iobase + PCL812_CTRL_REG);

	return 0;
}

static bool pcl812_ai_next_chan(struct comedi_device *dev,
				struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;

	if (cmd->stop_src == TRIG_COUNT &&
	    s->async->scans_done >= cmd->stop_arg) {
		s->async->events |= COMEDI_CB_EOA;
		return false;
	}

	return true;
}

static void pcl812_handle_eoc(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int chan = s->async->cur_chan;
	unsigned int next_chan;
	unsigned short val;

	if (pcl812_ai_eoc(dev, s, NULL, 0)) {
		dev_dbg(dev->class_dev, "A/D cmd IRQ without DRDY!\n");
		s->async->events |= COMEDI_CB_ERROR;
		return;
	}

	val = pcl812_ai_get_sample(dev, s);
	comedi_buf_write_samples(s, &val, 1);

	/* Set up next channel. Added by abbotti 2010-01-20, but untested. */
	next_chan = s->async->cur_chan;
	if (cmd->chanlist[chan] != cmd->chanlist[next_chan])
		pcl812_ai_set_chan_range(dev, cmd->chanlist[next_chan], 0);

	pcl812_ai_next_chan(dev, s);
}

static void transfer_from_dma_buf(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned short *ptr,
				  unsigned int bufptr, unsigned int len)
{
	unsigned int i;
	unsigned short val;

	for (i = len; i; i--) {
		val = ptr[bufptr++];
		comedi_buf_write_samples(s, &val, 1);

		if (!pcl812_ai_next_chan(dev, s))
			break;
	}
}

static void pcl812_handle_dma(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct pcl812_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[dma->cur_dma];
	unsigned int nsamples;
	int bufptr;

	nsamples = comedi_bytes_to_samples(s, desc->size) -
		   devpriv->ai_poll_ptr;
	bufptr = devpriv->ai_poll_ptr;
	devpriv->ai_poll_ptr = 0;

	/* restart dma with the next buffer */
	dma->cur_dma = 1 - dma->cur_dma;
	pcl812_ai_setup_dma(dev, s, nsamples);

	transfer_from_dma_buf(dev, s, desc->virt_addr, bufptr, nsamples);
}

static irqreturn_t pcl812_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	struct pcl812_private *devpriv = dev->private;

	if (!dev->attached) {
		pcl812_ai_clear_eoc(dev);
		return IRQ_HANDLED;
	}

	if (devpriv->ai_dma)
		pcl812_handle_dma(dev, s);
	else
		pcl812_handle_eoc(dev, s);

	pcl812_ai_clear_eoc(dev);

	comedi_handle_events(dev, s);
	return IRQ_HANDLED;
}

static int pcl812_ai_poll(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcl812_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc;
	unsigned long flags;
	unsigned int poll;
	int ret;

	/* poll is valid only for DMA transfer */
	if (!devpriv->ai_dma)
		return 0;

	spin_lock_irqsave(&dev->spinlock, flags);

	poll = comedi_isadma_poll(dma);
	poll = comedi_bytes_to_samples(s, poll);
	if (poll > devpriv->ai_poll_ptr) {
		desc = &dma->desc[dma->cur_dma];
		transfer_from_dma_buf(dev, s, desc->virt_addr,
				      devpriv->ai_poll_ptr,
				      poll - devpriv->ai_poll_ptr);
		/* new buffer position */
		devpriv->ai_poll_ptr = poll;

		ret = comedi_buf_n_bytes_ready(s);
	} else {
		/* no new samples */
		ret = 0;
	}

	spin_unlock_irqrestore(&dev->spinlock, flags);

	return ret;
}

static int pcl812_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct pcl812_private *devpriv = dev->private;

	if (devpriv->ai_dma)
		comedi_isadma_disable(devpriv->dma->chan);

	outb(devpriv->mode_reg_int | PCL812_CTRL_DISABLE_TRIG,
	     dev->iobase + PCL812_CTRL_REG);
	comedi_8254_pacer_enable(dev->pacer, 1, 2, false);
	pcl812_ai_clear_eoc(dev);
	return 0;
}

static int pcl812_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct pcl812_private *devpriv = dev->private;
	int ret = 0;
	int i;

	outb(devpriv->mode_reg_int | PCL812_CTRL_SOFT_TRIG,
	     dev->iobase + PCL812_CTRL_REG);

	pcl812_ai_set_chan_range(dev, insn->chanspec, 1);

	for (i = 0; i < insn->n; i++) {
		pcl812_ai_clear_eoc(dev);
		pcl812_ai_soft_trig(dev);

		ret = comedi_timeout(dev, s, insn, pcl812_ai_eoc, 0);
		if (ret)
			break;

		data[i] = pcl812_ai_get_sample(dev, s);
	}
	outb(devpriv->mode_reg_int | PCL812_CTRL_DISABLE_TRIG,
	     dev->iobase + PCL812_CTRL_REG);
	pcl812_ai_clear_eoc(dev);

	return ret ? ret : insn->n;
}

static int pcl812_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outb(val & 0xff, dev->iobase + PCL812_AO_LSB_REG(chan));
		outb((val >> 8) & 0x0f, dev->iobase + PCL812_AO_MSB_REG(chan));
	}
	s->readback[chan] = val;

	return insn->n;
}

static int pcl812_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	data[1] = inb(dev->iobase + PCL812_DI_LSB_REG) |
		  (inb(dev->iobase + PCL812_DI_MSB_REG) << 8);

	return insn->n;
}

static int pcl812_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	if (comedi_dio_update_state(s, data)) {
		outb(s->state & 0xff, dev->iobase + PCL812_DO_LSB_REG);
		outb((s->state >> 8), dev->iobase + PCL812_DO_MSB_REG);
	}

	data[1] = s->state;

	return insn->n;
}

static void pcl812_reset(struct comedi_device *dev)
{
	const struct pcl812_board *board = dev->board_ptr;
	struct pcl812_private *devpriv = dev->private;
	unsigned int chan;

	/* disable analog input trigger */
	outb(devpriv->mode_reg_int | PCL812_CTRL_DISABLE_TRIG,
	     dev->iobase + PCL812_CTRL_REG);
	pcl812_ai_clear_eoc(dev);

	/*
	 * Invalidate last_ai_chanspec then set analog input to
	 * known channel/range.
	 */
	devpriv->last_ai_chanspec = CR_PACK(16, 0, 0);
	pcl812_ai_set_chan_range(dev, CR_PACK(0, 0, 0), 0);

	/* set analog output channels to 0V */
	for (chan = 0; chan < board->n_aochan; chan++) {
		outb(0, dev->iobase + PCL812_AO_LSB_REG(chan));
		outb(0, dev->iobase + PCL812_AO_MSB_REG(chan));
	}

	/* set all digital outputs low */
	if (board->has_dio) {
		outb(0, dev->iobase + PCL812_DO_MSB_REG);
		outb(0, dev->iobase + PCL812_DO_LSB_REG);
	}
}

static void pcl812_set_ai_range_table(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_devconfig *it)
{
	const struct pcl812_board *board = dev->board_ptr;
	struct pcl812_private *devpriv = dev->private;

	switch (board->board_type) {
	case BOARD_PCL812PG:
		if (it->options[4] == 1)
			s->range_table = &range_pcl812pg2_ai;
		else
			s->range_table = board->rangelist_ai;
		break;
	case BOARD_PCL812:
		switch (it->options[4]) {
		case 0:
			s->range_table = &range_bipolar10;
			break;
		case 1:
			s->range_table = &range_bipolar5;
			break;
		case 2:
			s->range_table = &range_bipolar2_5;
			break;
		case 3:
			s->range_table = &range812_bipolar1_25;
			break;
		case 4:
			s->range_table = &range812_bipolar0_625;
			break;
		case 5:
			s->range_table = &range812_bipolar0_3125;
			break;
		default:
			s->range_table = &range_bipolar10;
			break;
		}
		break;
	case BOARD_PCL813B:
		if (it->options[1] == 1)
			s->range_table = &range_pcl813b2_ai;
		else
			s->range_table = board->rangelist_ai;
		break;
	case BOARD_ISO813:
		switch (it->options[1]) {
		case 0:
			s->range_table = &range_iso813_1_ai;
			break;
		case 1:
			s->range_table = &range_iso813_1_2_ai;
			break;
		case 2:
			s->range_table = &range_iso813_2_ai;
			devpriv->range_correction = 1;
			break;
		case 3:
			s->range_table = &range_iso813_2_2_ai;
			devpriv->range_correction = 1;
			break;
		default:
			s->range_table = &range_iso813_1_ai;
			break;
		}
		break;
	case BOARD_ACL8113:
		switch (it->options[1]) {
		case 0:
			s->range_table = &range_acl8113_1_ai;
			break;
		case 1:
			s->range_table = &range_acl8113_1_2_ai;
			break;
		case 2:
			s->range_table = &range_acl8113_2_ai;
			devpriv->range_correction = 1;
			break;
		case 3:
			s->range_table = &range_acl8113_2_2_ai;
			devpriv->range_correction = 1;
			break;
		default:
			s->range_table = &range_acl8113_1_ai;
			break;
		}
		break;
	default:
		s->range_table = board->rangelist_ai;
		break;
	}
}

static void pcl812_alloc_dma(struct comedi_device *dev, unsigned int dma_chan)
{
	struct pcl812_private *devpriv = dev->private;

	/* only DMA channels 3 and 1 are valid */
	if (!(dma_chan == 3 || dma_chan == 1))
		return;

	/* DMA uses two 8K buffers */
	devpriv->dma = comedi_isadma_alloc(dev, 2, dma_chan, dma_chan,
					   PAGE_SIZE * 2, COMEDI_ISADMA_READ);
}

static void pcl812_free_dma(struct comedi_device *dev)
{
	struct pcl812_private *devpriv = dev->private;

	if (devpriv)
		comedi_isadma_free(devpriv->dma);
}

static int pcl812_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcl812_board *board = dev->board_ptr;
	struct pcl812_private *devpriv;
	struct comedi_subdevice *s;
	int n_subdevices;
	int subdev;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	if (board->irq_bits) {
		dev->pacer = comedi_8254_init(dev->iobase + PCL812_TIMER_BASE,
					      I8254_OSC_BASE_2MHZ,
					      I8254_IO8, 0);
		if (!dev->pacer)
			return -ENOMEM;

		if ((1 << it->options[1]) & board->irq_bits) {
			ret = request_irq(it->options[1], pcl812_interrupt, 0,
					  dev->board_name, dev);
			if (ret == 0)
				dev->irq = it->options[1];
		}
	}

	/* we need an IRQ to do DMA on channel 3 or 1 */
	if (dev->irq && board->has_dma)
		pcl812_alloc_dma(dev, it->options[2]);

	/* differential analog inputs? */
	switch (board->board_type) {
	case BOARD_A821:
		if (it->options[2] == 1)
			devpriv->use_diff = 1;
		break;
	case BOARD_ACL8112:
	case BOARD_ACL8216:
		if (it->options[4] == 1)
			devpriv->use_diff = 1;
		break;
	default:
		break;
	}

	n_subdevices = 1;		/* all boardtypes have analog inputs */
	if (board->n_aochan > 0)
		n_subdevices++;
	if (board->has_dio)
		n_subdevices += 2;

	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	subdev = 0;

	/* Analog Input subdevice */
	s = &dev->subdevices[subdev];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE;
	if (devpriv->use_diff) {
		s->subdev_flags	|= SDF_DIFF;
		s->n_chan	= board->n_aichan / 2;
	} else {
		s->subdev_flags	|= SDF_GROUND;
		s->n_chan	= board->n_aichan;
	}
	s->maxdata	= board->has_16bit_ai ? 0xffff : 0x0fff;

	pcl812_set_ai_range_table(dev, s, it);

	s->insn_read	= pcl812_ai_insn_read;

	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= MAX_CHANLIST_LEN;
		s->do_cmdtest	= pcl812_ai_cmdtest;
		s->do_cmd	= pcl812_ai_cmd;
		s->poll		= pcl812_ai_poll;
		s->cancel	= pcl812_ai_cancel;
	}

	devpriv->use_mpc508 = board->has_mpc508_mux;

	subdev++;

	/* analog output */
	if (board->n_aochan > 0) {
		s = &dev->subdevices[subdev];
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_GROUND;
		s->n_chan	= board->n_aochan;
		s->maxdata	= 0xfff;
		switch (board->board_type) {
		case BOARD_A821:
			if (it->options[3] == 1)
				s->range_table = &range_unipolar10;
			else
				s->range_table = &range_unipolar5;
			break;
		case BOARD_PCL812:
		case BOARD_ACL8112:
		case BOARD_PCL812PG:
		case BOARD_ACL8216:
			switch (it->options[5]) {
			case 1:
				s->range_table = &range_unipolar10;
				break;
			case 2:
				s->range_table = &range_unknown;
				break;
			default:
				s->range_table = &range_unipolar5;
				break;
			}
			break;
		default:
			s->range_table = &range_unipolar5;
			break;
		}
		s->insn_write	= pcl812_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;

		subdev++;
	}

	if (board->has_dio) {
		/* Digital Input subdevice */
		s = &dev->subdevices[subdev];
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= 16;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pcl812_di_insn_bits;
		subdev++;

		/* Digital Output subdevice */
		s = &dev->subdevices[subdev];
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 16;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pcl812_do_insn_bits;
		subdev++;
	}

	switch (board->board_type) {
	case BOARD_ACL8216:
	case BOARD_PCL812PG:
	case BOARD_PCL812:
	case BOARD_ACL8112:
		devpriv->max_812_ai_mode0_rangewait = 1;
		if (it->options[3] > 0)
						/*  we use external trigger */
			devpriv->use_ext_trg = 1;
		break;
	case BOARD_A821:
		devpriv->max_812_ai_mode0_rangewait = 1;
		devpriv->mode_reg_int = (dev->irq << 4) & 0xf0;
		break;
	case BOARD_PCL813B:
	case BOARD_PCL813:
	case BOARD_ISO813:
	case BOARD_ACL8113:
		/* maybe there must by greatest timeout */
		devpriv->max_812_ai_mode0_rangewait = 5;
		break;
	}

	pcl812_reset(dev);

	return 0;
}

static void pcl812_detach(struct comedi_device *dev)
{
	pcl812_free_dma(dev);
	comedi_legacy_detach(dev);
}

static struct comedi_driver pcl812_driver = {
	.driver_name	= "pcl812",
	.module		= THIS_MODULE,
	.attach		= pcl812_attach,
	.detach		= pcl812_detach,
	.board_name	= &boardtypes[0].name,
	.num_names	= ARRAY_SIZE(boardtypes),
	.offset		= sizeof(struct pcl812_board),
};
module_comedi_driver(pcl812_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
