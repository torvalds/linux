// SPDX-License-Identifier: GPL-2.0
/*
 *  comedi/drivers/adl_pci9118.c
 *
 *  hardware driver for ADLink cards:
 *   card:   PCI-9118DG, PCI-9118HG, PCI-9118HR
 *   driver: pci9118dg,  pci9118hg,  pci9118hr
 *
 * Author: Michal Dobes <dobes@tesnet.cz>
 *
 */

/*
 * Driver: adl_pci9118
 * Description: Adlink PCI-9118DG, PCI-9118HG, PCI-9118HR
 * Author: Michal Dobes <dobes@tesnet.cz>
 * Devices: [ADLink] PCI-9118DG (pci9118dg), PCI-9118HG (pci9118hg),
 * PCI-9118HR (pci9118hr)
 * Status: works
 *
 * This driver supports AI, AO, DI and DO subdevices.
 * AI subdevice supports cmd and insn interface,
 * other subdevices support only insn interface.
 * For AI:
 * - If cmd->scan_begin_src=TRIG_EXT then trigger input is TGIN (pin 46).
 * - If cmd->convert_src=TRIG_EXT then trigger input is EXTTRG (pin 44).
 * - If cmd->start_src/stop_src=TRIG_EXT then trigger input is TGIN (pin 46).
 * - It is not necessary to have cmd.scan_end_arg=cmd.chanlist_len but
 * cmd.scan_end_arg modulo cmd.chanlist_len must by 0.
 * - If return value of cmdtest is 5 then you've bad channel list
 * (it isn't possible mixture S.E. and DIFF inputs or bipolar and unipolar
 * ranges).
 *
 * There are some hardware limitations:
 * a) You cann't use mixture of unipolar/bipoar ranges or differencial/single
 *  ended inputs.
 * b) DMA transfers must have the length aligned to two samples (32 bit),
 *  so there is some problems if cmd->chanlist_len is odd. This driver tries
 *  bypass this with adding one sample to the end of the every scan and discard
 *  it on output but this can't be used if cmd->scan_begin_src=TRIG_FOLLOW
 *  and is used flag CMDF_WAKE_EOS, then driver switch to interrupt driven mode
 *  with interrupt after every sample.
 * c) If isn't used DMA then you can use only mode where
 *  cmd->scan_begin_src=TRIG_FOLLOW.
 *
 * Configuration options:
 * [0] - PCI bus of device (optional)
 * [1] - PCI slot of device (optional)
 *	 If bus/slot is not specified, then first available PCI
 *	 card will be used.
 * [2] - 0= standard 8 DIFF/16 SE channels configuration
 *	 n = external multiplexer connected, 1 <= n <= 256
 * [3] - ignored
 * [4] - sample&hold signal - card can generate signal for external S&H board
 *	 0 = use SSHO(pin 45) signal is generated in onboard hardware S&H logic
 *	 0 != use ADCHN7(pin 23) signal is generated from driver, number say how
 *		long delay is requested in ns and sign polarity of the hold
 *		(in this case external multiplexor can serve only 128 channels)
 * [5] - ignored
 */

/*
 * FIXME
 *
 * All the supported boards have the same PCI vendor and device IDs, so
 * auto-attachment of PCI devices will always find the first board type.
 *
 * Perhaps the boards have different subdevice IDs that we could use to
 * distinguish them?
 *
 * Need some device attributes so the board type can be corrected after
 * attachment if necessary, and possibly to set other options supported by
 * manual attachment.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include "../comedi_pci.h"

#include "amcc_s5933.h"
#include "comedi_8254.h"

/*
 * PCI BAR2 Register map (dev->iobase)
 */
#define PCI9118_TIMER_BASE		0x00
#define PCI9118_AI_FIFO_REG		0x10
#define PCI9118_AO_REG(x)		(0x10 + ((x) * 4))
#define PCI9118_AI_STATUS_REG		0x18
#define PCI9118_AI_STATUS_NFULL		BIT(8)	/* 0=FIFO full (fatal) */
#define PCI9118_AI_STATUS_NHFULL	BIT(7)	/* 0=FIFO half full */
#define PCI9118_AI_STATUS_NEPTY		BIT(6)	/* 0=FIFO empty */
#define PCI9118_AI_STATUS_ACMP		BIT(5)	/* 1=about trigger complete */
#define PCI9118_AI_STATUS_DTH		BIT(4)	/* 1=ext. digital trigger */
#define PCI9118_AI_STATUS_BOVER		BIT(3)	/* 1=burst overrun (fatal) */
#define PCI9118_AI_STATUS_ADOS		BIT(2)	/* 1=A/D over speed (warn) */
#define PCI9118_AI_STATUS_ADOR		BIT(1)	/* 1=A/D overrun (fatal) */
#define PCI9118_AI_STATUS_ADRDY		BIT(0)	/* 1=A/D ready */
#define PCI9118_AI_CTRL_REG		0x18
#define PCI9118_AI_CTRL_UNIP		BIT(7)	/* 1=unipolar */
#define PCI9118_AI_CTRL_DIFF		BIT(6)	/* 1=differential inputs */
#define PCI9118_AI_CTRL_SOFTG		BIT(5)	/* 1=8254 software gate */
#define PCI9118_AI_CTRL_EXTG		BIT(4)	/* 1=8254 TGIN(pin 46) gate */
#define PCI9118_AI_CTRL_EXTM		BIT(3)	/* 1=ext. trigger (pin 44) */
#define PCI9118_AI_CTRL_TMRTR		BIT(2)	/* 1=8254 is trigger source */
#define PCI9118_AI_CTRL_INT		BIT(1)	/* 1=enable interrupt */
#define PCI9118_AI_CTRL_DMA		BIT(0)	/* 1=enable DMA */
#define PCI9118_DIO_REG			0x1c
#define PCI9118_SOFTTRG_REG		0x20
#define PCI9118_AI_CHANLIST_REG		0x24
#define PCI9118_AI_CHANLIST_RANGE(x)	(((x) & 0x3) << 8)
#define PCI9118_AI_CHANLIST_CHAN(x)	((x) << 0)
#define PCI9118_AI_BURST_NUM_REG	0x28
#define PCI9118_AI_AUTOSCAN_MODE_REG	0x2c
#define PCI9118_AI_CFG_REG		0x30
#define PCI9118_AI_CFG_PDTRG		BIT(7)	/* 1=positive trigger */
#define PCI9118_AI_CFG_PETRG		BIT(6)	/* 1=positive ext. trigger */
#define PCI9118_AI_CFG_BSSH		BIT(5)	/* 1=with sample & hold */
#define PCI9118_AI_CFG_BM		BIT(4)	/* 1=burst mode */
#define PCI9118_AI_CFG_BS		BIT(3)	/* 1=burst mode start */
#define PCI9118_AI_CFG_PM		BIT(2)	/* 1=post trigger */
#define PCI9118_AI_CFG_AM		BIT(1)	/* 1=about trigger */
#define PCI9118_AI_CFG_START		BIT(0)	/* 1=trigger start */
#define PCI9118_FIFO_RESET_REG		0x34
#define PCI9118_INT_CTRL_REG		0x38
#define PCI9118_INT_CTRL_TIMER		BIT(3)	/* timer interrupt */
#define PCI9118_INT_CTRL_ABOUT		BIT(2)	/* about trigger complete */
#define PCI9118_INT_CTRL_HFULL		BIT(1)	/* A/D FIFO half full */
#define PCI9118_INT_CTRL_DTRG		BIT(0)	/* ext. digital trigger */

#define START_AI_EXT	0x01	/* start measure on external trigger */
#define STOP_AI_EXT	0x02	/* stop measure on external trigger */
#define STOP_AI_INT	0x08	/* stop measure on internal trigger */

static const struct comedi_lrange pci9118_ai_range = {
	8, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

static const struct comedi_lrange pci9118hg_ai_range = {
	8, {
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05),
		BIP_RANGE(0.005),
		UNI_RANGE(10),
		UNI_RANGE(1),
		UNI_RANGE(0.1),
		UNI_RANGE(0.01)
	}
};

enum pci9118_boardid {
	BOARD_PCI9118DG,
	BOARD_PCI9118HG,
	BOARD_PCI9118HR,
};

struct pci9118_boardinfo {
	const char *name;
	unsigned int ai_is_16bit:1;
	unsigned int is_hg:1;
};

static const struct pci9118_boardinfo pci9118_boards[] = {
	[BOARD_PCI9118DG] = {
		.name		= "pci9118dg",
	},
	[BOARD_PCI9118HG] = {
		.name		= "pci9118hg",
		.is_hg		= 1,
	},
	[BOARD_PCI9118HR] = {
		.name		= "pci9118hr",
		.ai_is_16bit	= 1,
	},
};

struct pci9118_dmabuf {
	unsigned short *virt;	/* virtual address of buffer */
	dma_addr_t hw;		/* hardware (bus) address of buffer */
	unsigned int size;	/* size of dma buffer in bytes */
	unsigned int use_size;	/* which size we may now use for transfer */
};

struct pci9118_private {
	unsigned long iobase_a;	/* base+size for AMCC chip */
	unsigned int master:1;
	unsigned int dma_doublebuf:1;
	unsigned int ai_neverending:1;
	unsigned int usedma:1;
	unsigned int usemux:1;
	unsigned char ai_ctrl;
	unsigned char int_ctrl;
	unsigned char ai_cfg;
	unsigned int ai_do;		/* what do AI? 0=nothing, 1 to 4 mode */
	unsigned int ai_n_realscanlen;	/*
					 * what we must transfer for one
					 * outgoing scan include front/back adds
					 */
	unsigned int ai_act_dmapos;	/* position in actual real stream */
	unsigned int ai_add_front;	/*
					 * how many channels we must add
					 * before scan to satisfy S&H?
					 */
	unsigned int ai_add_back;	/*
					 * how many channels we must add
					 * before scan to satisfy DMA?
					 */
	unsigned int ai_flags;
	char ai12_startstop;		/*
					 * measure can start/stop
					 * on external trigger
					 */
	unsigned int dma_actbuf;		/* which buffer is used now */
	struct pci9118_dmabuf dmabuf[2];
	int softsshdelay;		/*
					 * >0 use software S&H,
					 * numer is requested delay in ns
					 */
	unsigned char softsshsample;	/*
					 * polarity of S&H signal
					 * in sample state
					 */
	unsigned char softsshhold;	/*
					 * polarity of S&H signal
					 * in hold state
					 */
	unsigned int ai_ns_min;
};

static void pci9118_amcc_setup_dma(struct comedi_device *dev, unsigned int buf)
{
	struct pci9118_private *devpriv = dev->private;
	struct pci9118_dmabuf *dmabuf = &devpriv->dmabuf[buf];

	/* set the master write address and transfer count */
	outl(dmabuf->hw, devpriv->iobase_a + AMCC_OP_REG_MWAR);
	outl(dmabuf->use_size, devpriv->iobase_a + AMCC_OP_REG_MWTC);
}

static void pci9118_amcc_dma_ena(struct comedi_device *dev, bool enable)
{
	struct pci9118_private *devpriv = dev->private;
	unsigned int mcsr;

	mcsr = inl(devpriv->iobase_a + AMCC_OP_REG_MCSR);
	if (enable)
		mcsr |= RESET_A2P_FLAGS | A2P_HI_PRIORITY | EN_A2P_TRANSFERS;
	else
		mcsr &= ~EN_A2P_TRANSFERS;
	outl(mcsr, devpriv->iobase_a + AMCC_OP_REG_MCSR);
}

static void pci9118_amcc_int_ena(struct comedi_device *dev, bool enable)
{
	struct pci9118_private *devpriv = dev->private;
	unsigned int intcsr;

	/* enable/disable interrupt for AMCC Incoming Mailbox 4 (32-bit) */
	intcsr = inl(devpriv->iobase_a + AMCC_OP_REG_INTCSR);
	if (enable)
		intcsr |= 0x1f00;
	else
		intcsr &= ~0x1f00;
	outl(intcsr, devpriv->iobase_a + AMCC_OP_REG_INTCSR);
}

static void pci9118_ai_reset_fifo(struct comedi_device *dev)
{
	/* writing any value resets the A/D FIFO */
	outl(0, dev->iobase + PCI9118_FIFO_RESET_REG);
}

static int pci9118_ai_check_chanlist(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_cmd *cmd)
{
	struct pci9118_private *devpriv = dev->private;
	unsigned int range0 = CR_RANGE(cmd->chanlist[0]);
	unsigned int aref0 = CR_AREF(cmd->chanlist[0]);
	int i;

	/* single channel scans are always ok */
	if (cmd->chanlist_len == 1)
		return 0;

	for (i = 1; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);
		unsigned int range = CR_RANGE(cmd->chanlist[i]);
		unsigned int aref = CR_AREF(cmd->chanlist[i]);

		if (aref != aref0) {
			dev_err(dev->class_dev,
				"Differential and single ended inputs can't be mixed!\n");
			return -EINVAL;
		}
		if (comedi_range_is_bipolar(s, range) !=
		    comedi_range_is_bipolar(s, range0)) {
			dev_err(dev->class_dev,
				"Bipolar and unipolar ranges can't be mixed!\n");
			return -EINVAL;
		}
		if (!devpriv->usemux && aref == AREF_DIFF &&
		    (chan >= (s->n_chan / 2))) {
			dev_err(dev->class_dev,
				"AREF_DIFF is only available for the first 8 channels!\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void pci9118_set_chanlist(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 int n_chan, unsigned int *chanlist,
				 int frontadd, int backadd)
{
	struct pci9118_private *devpriv = dev->private;
	unsigned int chan0 = CR_CHAN(chanlist[0]);
	unsigned int range0 = CR_RANGE(chanlist[0]);
	unsigned int aref0 = CR_AREF(chanlist[0]);
	unsigned int ssh = 0x00;
	unsigned int val;
	int i;

	/*
	 * Configure analog input based on the first chanlist entry.
	 * All entries are either unipolar or bipolar and single-ended
	 * or differential.
	 */
	devpriv->ai_ctrl = 0;
	if (comedi_range_is_unipolar(s, range0))
		devpriv->ai_ctrl |= PCI9118_AI_CTRL_UNIP;
	if (aref0 == AREF_DIFF)
		devpriv->ai_ctrl |= PCI9118_AI_CTRL_DIFF;
	outl(devpriv->ai_ctrl, dev->iobase + PCI9118_AI_CTRL_REG);

	/* gods know why this sequence! */
	outl(2, dev->iobase + PCI9118_AI_AUTOSCAN_MODE_REG);
	outl(0, dev->iobase + PCI9118_AI_AUTOSCAN_MODE_REG);
	outl(1, dev->iobase + PCI9118_AI_AUTOSCAN_MODE_REG);

	/* insert channels for S&H */
	if (frontadd) {
		val = PCI9118_AI_CHANLIST_CHAN(chan0) |
		      PCI9118_AI_CHANLIST_RANGE(range0);
		ssh = devpriv->softsshsample;
		for (i = 0; i < frontadd; i++) {
			outl(val | ssh, dev->iobase + PCI9118_AI_CHANLIST_REG);
			ssh = devpriv->softsshhold;
		}
	}

	/* store chanlist */
	for (i = 0; i < n_chan; i++) {
		unsigned int chan = CR_CHAN(chanlist[i]);
		unsigned int range = CR_RANGE(chanlist[i]);

		val = PCI9118_AI_CHANLIST_CHAN(chan) |
		      PCI9118_AI_CHANLIST_RANGE(range);
		outl(val | ssh, dev->iobase + PCI9118_AI_CHANLIST_REG);
	}

	/* insert channels to fit onto 32bit DMA */
	if (backadd) {
		val = PCI9118_AI_CHANLIST_CHAN(chan0) |
		      PCI9118_AI_CHANLIST_RANGE(range0);
		for (i = 0; i < backadd; i++)
			outl(val | ssh, dev->iobase + PCI9118_AI_CHANLIST_REG);
	}
	/* close scan queue */
	outl(0, dev->iobase + PCI9118_AI_AUTOSCAN_MODE_REG);
	/* udelay(100); important delay, or first sample will be crippled */
}

static void pci9118_ai_mode4_switch(struct comedi_device *dev,
				    unsigned int next_buf)
{
	struct pci9118_private *devpriv = dev->private;
	struct pci9118_dmabuf *dmabuf = &devpriv->dmabuf[next_buf];

	devpriv->ai_cfg = PCI9118_AI_CFG_PDTRG | PCI9118_AI_CFG_PETRG |
			  PCI9118_AI_CFG_AM;
	outl(devpriv->ai_cfg, dev->iobase + PCI9118_AI_CFG_REG);
	comedi_8254_load(dev->pacer, 0, dmabuf->hw >> 1,
			 I8254_MODE0 | I8254_BINARY);
	devpriv->ai_cfg |= PCI9118_AI_CFG_START;
	outl(devpriv->ai_cfg, dev->iobase + PCI9118_AI_CFG_REG);
}

static unsigned int pci9118_ai_samples_ready(struct comedi_device *dev,
					     struct comedi_subdevice *s,
					     unsigned int n_raw_samples)
{
	struct pci9118_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int start_pos = devpriv->ai_add_front;
	unsigned int stop_pos = start_pos + cmd->chanlist_len;
	unsigned int span_len = stop_pos + devpriv->ai_add_back;
	unsigned int dma_pos = devpriv->ai_act_dmapos;
	unsigned int whole_spans, n_samples, x;

	if (span_len == cmd->chanlist_len)
		return n_raw_samples;	/* use all samples */

	/*
	 * Not all samples are to be used.  Buffer contents consist of a
	 * possibly non-whole number of spans and a region of each span
	 * is to be used.
	 *
	 * Account for samples in whole number of spans.
	 */
	whole_spans = n_raw_samples / span_len;
	n_samples = whole_spans * cmd->chanlist_len;
	n_raw_samples -= whole_spans * span_len;

	/*
	 * Deal with remaining samples which could overlap up to two spans.
	 */
	while (n_raw_samples) {
		if (dma_pos < start_pos) {
			/* Skip samples before start position. */
			x = start_pos - dma_pos;
			if (x > n_raw_samples)
				x = n_raw_samples;
			dma_pos += x;
			n_raw_samples -= x;
			if (!n_raw_samples)
				break;
		}
		if (dma_pos < stop_pos) {
			/* Include samples before stop position. */
			x = stop_pos - dma_pos;
			if (x > n_raw_samples)
				x = n_raw_samples;
			n_samples += x;
			dma_pos += x;
			n_raw_samples -= x;
		}
		/* Advance to next span. */
		start_pos += span_len;
		stop_pos += span_len;
	}
	return n_samples;
}

static void pci9118_ai_dma_xfer(struct comedi_device *dev,
				struct comedi_subdevice *s,
				unsigned short *dma_buffer,
				unsigned int n_raw_samples)
{
	struct pci9118_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int start_pos = devpriv->ai_add_front;
	unsigned int stop_pos = start_pos + cmd->chanlist_len;
	unsigned int span_len = stop_pos + devpriv->ai_add_back;
	unsigned int dma_pos = devpriv->ai_act_dmapos;
	unsigned int x;

	if (span_len == cmd->chanlist_len) {
		/* All samples are to be copied. */
		comedi_buf_write_samples(s, dma_buffer, n_raw_samples);
		dma_pos += n_raw_samples;
	} else {
		/*
		 * Not all samples are to be copied.  Buffer contents consist
		 * of a possibly non-whole number of spans and a region of
		 * each span is to be copied.
		 */
		while (n_raw_samples) {
			if (dma_pos < start_pos) {
				/* Skip samples before start position. */
				x = start_pos - dma_pos;
				if (x > n_raw_samples)
					x = n_raw_samples;
				dma_pos += x;
				n_raw_samples -= x;
				if (!n_raw_samples)
					break;
			}
			if (dma_pos < stop_pos) {
				/* Copy samples before stop position. */
				x = stop_pos - dma_pos;
				if (x > n_raw_samples)
					x = n_raw_samples;
				comedi_buf_write_samples(s, dma_buffer, x);
				dma_pos += x;
				n_raw_samples -= x;
			}
			/* Advance to next span. */
			start_pos += span_len;
			stop_pos += span_len;
		}
	}
	/* Update position in span for next time. */
	devpriv->ai_act_dmapos = dma_pos % span_len;
}

static void pci9118_exttrg_enable(struct comedi_device *dev, bool enable)
{
	struct pci9118_private *devpriv = dev->private;

	if (enable)
		devpriv->int_ctrl |= PCI9118_INT_CTRL_DTRG;
	else
		devpriv->int_ctrl &= ~PCI9118_INT_CTRL_DTRG;
	outl(devpriv->int_ctrl, dev->iobase + PCI9118_INT_CTRL_REG);

	if (devpriv->int_ctrl)
		pci9118_amcc_int_ena(dev, true);
	else
		pci9118_amcc_int_ena(dev, false);
}

static void pci9118_calc_divisors(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned int *tim1, unsigned int *tim2,
				  unsigned int flags, int chans,
				  unsigned int *div1, unsigned int *div2,
				  unsigned int chnsshfront)
{
	struct comedi_8254 *pacer = dev->pacer;
	struct comedi_cmd *cmd = &s->async->cmd;

	*div1 = *tim2 / pacer->osc_base;	/* convert timer (burst) */
	*div2 = *tim1 / pacer->osc_base;	/* scan timer */
	*div2 = *div2 / *div1;			/* major timer is c1*c2 */
	if (*div2 < chans)
		*div2 = chans;

	*tim2 = *div1 * pacer->osc_base;	/* real convert timer */

	if (cmd->convert_src == TRIG_NOW && !chnsshfront) {
		/* use BSSH signal */
		if (*div2 < (chans + 2))
			*div2 = chans + 2;
	}

	*tim1 = *div1 * *div2 * pacer->osc_base;
}

static void pci9118_start_pacer(struct comedi_device *dev, int mode)
{
	if (mode == 1 || mode == 2 || mode == 4)
		comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
}

static int pci9118_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pci9118_private *devpriv = dev->private;

	if (devpriv->usedma)
		pci9118_amcc_dma_ena(dev, false);
	pci9118_exttrg_enable(dev, false);
	comedi_8254_pacer_enable(dev->pacer, 1, 2, false);
	/* set default config (disable burst and triggers) */
	devpriv->ai_cfg = PCI9118_AI_CFG_PDTRG | PCI9118_AI_CFG_PETRG;
	outl(devpriv->ai_cfg, dev->iobase + PCI9118_AI_CFG_REG);
	/* reset acquisition control */
	devpriv->ai_ctrl = 0;
	outl(devpriv->ai_ctrl, dev->iobase + PCI9118_AI_CTRL_REG);
	outl(0, dev->iobase + PCI9118_AI_BURST_NUM_REG);
	/* reset scan queue */
	outl(1, dev->iobase + PCI9118_AI_AUTOSCAN_MODE_REG);
	outl(2, dev->iobase + PCI9118_AI_AUTOSCAN_MODE_REG);
	pci9118_ai_reset_fifo(dev);

	devpriv->int_ctrl = 0;
	outl(devpriv->int_ctrl, dev->iobase + PCI9118_INT_CTRL_REG);
	pci9118_amcc_int_ena(dev, false);

	devpriv->ai_do = 0;
	devpriv->usedma = 0;

	devpriv->ai_act_dmapos = 0;
	s->async->inttrig = NULL;
	devpriv->ai_neverending = 0;
	devpriv->dma_actbuf = 0;

	return 0;
}

static void pci9118_ai_munge(struct comedi_device *dev,
			     struct comedi_subdevice *s, void *data,
			     unsigned int num_bytes,
			     unsigned int start_chan_index)
{
	struct pci9118_private *devpriv = dev->private;
	unsigned short *array = data;
	unsigned int num_samples = comedi_bytes_to_samples(s, num_bytes);
	unsigned int i;
	__be16 *barray = data;

	for (i = 0; i < num_samples; i++) {
		if (devpriv->usedma)
			array[i] = be16_to_cpu(barray[i]);
		if (s->maxdata == 0xffff)
			array[i] ^= 0x8000;
		else
			array[i] = (array[i] >> 4) & 0x0fff;
	}
}

static void pci9118_ai_get_onesample(struct comedi_device *dev,
				     struct comedi_subdevice *s)
{
	struct pci9118_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned short sampl;

	sampl = inl(dev->iobase + PCI9118_AI_FIFO_REG);

	comedi_buf_write_samples(s, &sampl, 1);

	if (!devpriv->ai_neverending) {
		if (s->async->scans_done >= cmd->stop_arg)
			s->async->events |= COMEDI_CB_EOA;
	}
}

static void pci9118_ai_get_dma(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct pci9118_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	struct pci9118_dmabuf *dmabuf = &devpriv->dmabuf[devpriv->dma_actbuf];
	unsigned int n_all = comedi_bytes_to_samples(s, dmabuf->use_size);
	unsigned int n_valid;
	bool more_dma;

	/* determine whether more DMA buffers to do after this one */
	n_valid = pci9118_ai_samples_ready(dev, s, n_all);
	more_dma = n_valid < comedi_nsamples_left(s, n_valid + 1);

	/* switch DMA buffers and restart DMA if double buffering */
	if (more_dma && devpriv->dma_doublebuf) {
		devpriv->dma_actbuf = 1 - devpriv->dma_actbuf;
		pci9118_amcc_setup_dma(dev, devpriv->dma_actbuf);
		if (devpriv->ai_do == 4)
			pci9118_ai_mode4_switch(dev, devpriv->dma_actbuf);
	}

	if (n_all)
		pci9118_ai_dma_xfer(dev, s, dmabuf->virt, n_all);

	if (!devpriv->ai_neverending) {
		if (s->async->scans_done >= cmd->stop_arg)
			s->async->events |= COMEDI_CB_EOA;
	}

	if (s->async->events & COMEDI_CB_CANCEL_MASK)
		more_dma = false;

	/* restart DMA if not double buffering */
	if (more_dma && !devpriv->dma_doublebuf) {
		pci9118_amcc_setup_dma(dev, 0);
		if (devpriv->ai_do == 4)
			pci9118_ai_mode4_switch(dev, 0);
	}
}

static irqreturn_t pci9118_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	struct pci9118_private *devpriv = dev->private;
	unsigned int intsrc;	/* IRQ reasons from card */
	unsigned int intcsr;	/* INT register from AMCC chip */
	unsigned int adstat;	/* STATUS register */

	if (!dev->attached)
		return IRQ_NONE;

	intsrc = inl(dev->iobase + PCI9118_INT_CTRL_REG) & 0xf;
	intcsr = inl(devpriv->iobase_a + AMCC_OP_REG_INTCSR);

	if (!intsrc && !(intcsr & ANY_S593X_INT))
		return IRQ_NONE;

	outl(intcsr | 0x00ff0000, devpriv->iobase_a + AMCC_OP_REG_INTCSR);

	if (intcsr & MASTER_ABORT_INT) {
		dev_err(dev->class_dev, "AMCC IRQ - MASTER DMA ABORT!\n");
		s->async->events |= COMEDI_CB_ERROR;
		goto interrupt_exit;
	}

	if (intcsr & TARGET_ABORT_INT) {
		dev_err(dev->class_dev, "AMCC IRQ - TARGET DMA ABORT!\n");
		s->async->events |= COMEDI_CB_ERROR;
		goto interrupt_exit;
	}

	adstat = inl(dev->iobase + PCI9118_AI_STATUS_REG);
	if ((adstat & PCI9118_AI_STATUS_NFULL) == 0) {
		dev_err(dev->class_dev,
			"A/D FIFO Full status (Fatal Error!)\n");
		s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_OVERFLOW;
		goto interrupt_exit;
	}
	if (adstat & PCI9118_AI_STATUS_BOVER) {
		dev_err(dev->class_dev,
			"A/D Burst Mode Overrun Status (Fatal Error!)\n");
		s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_OVERFLOW;
		goto interrupt_exit;
	}
	if (adstat & PCI9118_AI_STATUS_ADOS) {
		dev_err(dev->class_dev, "A/D Over Speed Status (Warning!)\n");
		s->async->events |= COMEDI_CB_ERROR;
		goto interrupt_exit;
	}
	if (adstat & PCI9118_AI_STATUS_ADOR) {
		dev_err(dev->class_dev, "A/D Overrun Status (Fatal Error!)\n");
		s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_OVERFLOW;
		goto interrupt_exit;
	}

	if (!devpriv->ai_do)
		return IRQ_HANDLED;

	if (devpriv->ai12_startstop) {
		if ((adstat & PCI9118_AI_STATUS_DTH) &&
		    (intsrc & PCI9118_INT_CTRL_DTRG)) {
			/* start/stop of measure */
			if (devpriv->ai12_startstop & START_AI_EXT) {
				/* deactivate EXT trigger */
				devpriv->ai12_startstop &= ~START_AI_EXT;
				if (!(devpriv->ai12_startstop & STOP_AI_EXT))
					pci9118_exttrg_enable(dev, false);

				/* start pacer */
				pci9118_start_pacer(dev, devpriv->ai_do);
				outl(devpriv->ai_ctrl,
				     dev->iobase + PCI9118_AI_CTRL_REG);
			} else if (devpriv->ai12_startstop & STOP_AI_EXT) {
				/* deactivate EXT trigger */
				devpriv->ai12_startstop &= ~STOP_AI_EXT;
				pci9118_exttrg_enable(dev, false);

				/* on next interrupt measure will stop */
				devpriv->ai_neverending = 0;
			}
		}
	}

	if (devpriv->usedma)
		pci9118_ai_get_dma(dev, s);
	else
		pci9118_ai_get_onesample(dev, s);

interrupt_exit:
	comedi_handle_events(dev, s);
	return IRQ_HANDLED;
}

static void pci9118_ai_cmd_start(struct comedi_device *dev)
{
	struct pci9118_private *devpriv = dev->private;

	outl(devpriv->int_ctrl, dev->iobase + PCI9118_INT_CTRL_REG);
	outl(devpriv->ai_cfg, dev->iobase + PCI9118_AI_CFG_REG);
	if (devpriv->ai_do != 3) {
		pci9118_start_pacer(dev, devpriv->ai_do);
		devpriv->ai_ctrl |= PCI9118_AI_CTRL_SOFTG;
	}
	outl(devpriv->ai_ctrl, dev->iobase + PCI9118_AI_CTRL_REG);
}

static int pci9118_ai_inttrig(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      unsigned int trig_num)
{
	struct comedi_cmd *cmd = &s->async->cmd;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	s->async->inttrig = NULL;
	pci9118_ai_cmd_start(dev);

	return 1;
}

static int pci9118_ai_setup_dma(struct comedi_device *dev,
				struct comedi_subdevice *s)
{
	struct pci9118_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	struct pci9118_dmabuf *dmabuf0 = &devpriv->dmabuf[0];
	struct pci9118_dmabuf *dmabuf1 = &devpriv->dmabuf[1];
	unsigned int dmalen0 = dmabuf0->size;
	unsigned int dmalen1 = dmabuf1->size;
	unsigned int scan_bytes = devpriv->ai_n_realscanlen *
				  comedi_bytes_per_sample(s);

	/* isn't output buff smaller that our DMA buff? */
	if (dmalen0 > s->async->prealloc_bufsz) {
		/* align to 32bit down */
		dmalen0 = s->async->prealloc_bufsz & ~3L;
	}
	if (dmalen1 > s->async->prealloc_bufsz) {
		/* align to 32bit down */
		dmalen1 = s->async->prealloc_bufsz & ~3L;
	}

	/* we want wake up every scan? */
	if (devpriv->ai_flags & CMDF_WAKE_EOS) {
		if (dmalen0 < scan_bytes) {
			/* uff, too short DMA buffer, disable EOS support! */
			devpriv->ai_flags &= (~CMDF_WAKE_EOS);
			dev_info(dev->class_dev,
				 "WAR: DMA0 buf too short, can't support CMDF_WAKE_EOS (%d<%d)\n",
				  dmalen0, scan_bytes);
		} else {
			/* short first DMA buffer to one scan */
			dmalen0 = scan_bytes;
			if (dmalen0 < 4) {
				dev_info(dev->class_dev,
					 "ERR: DMA0 buf len bug? (%d<4)\n",
					 dmalen0);
				dmalen0 = 4;
			}
		}
	}
	if (devpriv->ai_flags & CMDF_WAKE_EOS) {
		if (dmalen1 < scan_bytes) {
			/* uff, too short DMA buffer, disable EOS support! */
			devpriv->ai_flags &= (~CMDF_WAKE_EOS);
			dev_info(dev->class_dev,
				 "WAR: DMA1 buf too short, can't support CMDF_WAKE_EOS (%d<%d)\n",
				 dmalen1, scan_bytes);
		} else {
			/* short second DMA buffer to one scan */
			dmalen1 = scan_bytes;
			if (dmalen1 < 4) {
				dev_info(dev->class_dev,
					 "ERR: DMA1 buf len bug? (%d<4)\n",
					 dmalen1);
				dmalen1 = 4;
			}
		}
	}

	/* transfer without CMDF_WAKE_EOS */
	if (!(devpriv->ai_flags & CMDF_WAKE_EOS)) {
		unsigned int tmp;

		/* if it's possible then align DMA buffers to length of scan */
		tmp = dmalen0;
		dmalen0 = (dmalen0 / scan_bytes) * scan_bytes;
		dmalen0 &= ~3L;
		if (!dmalen0)
			dmalen0 = tmp;	/* uff. very long scan? */
		tmp = dmalen1;
		dmalen1 = (dmalen1 / scan_bytes) * scan_bytes;
		dmalen1 &= ~3L;
		if (!dmalen1)
			dmalen1 = tmp;	/* uff. very long scan? */
		/*
		 * if measure isn't neverending then test, if it fits whole
		 * into one or two DMA buffers
		 */
		if (!devpriv->ai_neverending) {
			unsigned long long scanlen;

			scanlen = (unsigned long long)scan_bytes *
				  cmd->stop_arg;

			/* fits whole measure into one DMA buffer? */
			if (dmalen0 > scanlen) {
				dmalen0 = scanlen;
				dmalen0 &= ~3L;
			} else {
				/* fits whole measure into two DMA buffer? */
				if (dmalen1 > (scanlen - dmalen0)) {
					dmalen1 = scanlen - dmalen0;
					dmalen1 &= ~3L;
				}
			}
		}
	}

	/* these DMA buffer size will be used */
	devpriv->dma_actbuf = 0;
	dmabuf0->use_size = dmalen0;
	dmabuf1->use_size = dmalen1;

	pci9118_amcc_dma_ena(dev, false);
	pci9118_amcc_setup_dma(dev, 0);
	/* init DMA transfer */
	outl(0x00000000 | AINT_WRITE_COMPL,
	     devpriv->iobase_a + AMCC_OP_REG_INTCSR);
/* outl(0x02000000|AINT_WRITE_COMPL, devpriv->iobase_a+AMCC_OP_REG_INTCSR); */
	pci9118_amcc_dma_ena(dev, true);
	outl(inl(devpriv->iobase_a + AMCC_OP_REG_INTCSR) | EN_A2P_TRANSFERS,
	     devpriv->iobase_a + AMCC_OP_REG_INTCSR);
						/* allow bus mastering */

	return 0;
}

static int pci9118_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pci9118_private *devpriv = dev->private;
	struct comedi_8254 *pacer = dev->pacer;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int addchans = 0;
	unsigned int scanlen;

	devpriv->ai12_startstop = 0;
	devpriv->ai_flags = cmd->flags;
	devpriv->ai_add_front = 0;
	devpriv->ai_add_back = 0;

	/* prepare for start/stop conditions */
	if (cmd->start_src == TRIG_EXT)
		devpriv->ai12_startstop |= START_AI_EXT;
	if (cmd->stop_src == TRIG_EXT) {
		devpriv->ai_neverending = 1;
		devpriv->ai12_startstop |= STOP_AI_EXT;
	}
	if (cmd->stop_src == TRIG_NONE)
		devpriv->ai_neverending = 1;
	if (cmd->stop_src == TRIG_COUNT)
		devpriv->ai_neverending = 0;

	/*
	 * use additional sample at end of every scan
	 * to satisty DMA 32 bit transfer?
	 */
	devpriv->ai_add_front = 0;
	devpriv->ai_add_back = 0;
	if (devpriv->master) {
		devpriv->usedma = 1;
		if ((cmd->flags & CMDF_WAKE_EOS) &&
		    (cmd->scan_end_arg == 1)) {
			if (cmd->convert_src == TRIG_NOW)
				devpriv->ai_add_back = 1;
			if (cmd->convert_src == TRIG_TIMER) {
				devpriv->usedma = 0;
					/*
					 * use INT transfer if scanlist
					 * have only one channel
					 */
			}
		}
		if ((cmd->flags & CMDF_WAKE_EOS) &&
		    (cmd->scan_end_arg & 1) &&
		    (cmd->scan_end_arg > 1)) {
			if (cmd->scan_begin_src == TRIG_FOLLOW) {
				devpriv->usedma = 0;
				/*
				 * XXX maybe can be corrected to use 16 bit DMA
				 */
			} else {	/*
					 * well, we must insert one sample
					 * to end of EOS to meet 32 bit transfer
					 */
				devpriv->ai_add_back = 1;
			}
		}
	} else {	/* interrupt transfer don't need any correction */
		devpriv->usedma = 0;
	}

	/*
	 * we need software S&H signal?
	 * It adds two samples before every scan as minimum
	 */
	if (cmd->convert_src == TRIG_NOW && devpriv->softsshdelay) {
		devpriv->ai_add_front = 2;
		if ((devpriv->usedma == 1) && (devpriv->ai_add_back == 1)) {
							/* move it to front */
			devpriv->ai_add_front++;
			devpriv->ai_add_back = 0;
		}
		if (cmd->convert_arg < devpriv->ai_ns_min)
			cmd->convert_arg = devpriv->ai_ns_min;
		addchans = devpriv->softsshdelay / cmd->convert_arg;
		if (devpriv->softsshdelay % cmd->convert_arg)
			addchans++;
		if (addchans > (devpriv->ai_add_front - 1)) {
							/* uff, still short */
			devpriv->ai_add_front = addchans + 1;
			if (devpriv->usedma == 1)
				if ((devpriv->ai_add_front +
				     cmd->chanlist_len +
				     devpriv->ai_add_back) & 1)
					devpriv->ai_add_front++;
							/* round up to 32 bit */
		}
	}
	/* well, we now know what must be all added */
	scanlen = devpriv->ai_add_front + cmd->chanlist_len +
		  devpriv->ai_add_back;
	/*
	 * what we must take from card in real to have cmd->scan_end_arg
	 * on output?
	 */
	devpriv->ai_n_realscanlen = scanlen *
				    (cmd->scan_end_arg / cmd->chanlist_len);

	if (scanlen > s->len_chanlist) {
		dev_err(dev->class_dev,
			"range/channel list is too long for actual configuration!\n");
		return -EINVAL;
	}

	/*
	 * Configure analog input and load the chanlist.
	 * The acquisition control bits are enabled later.
	 */
	pci9118_set_chanlist(dev, s, cmd->chanlist_len, cmd->chanlist,
			     devpriv->ai_add_front, devpriv->ai_add_back);

	/* Determine acquisition mode and calculate timing */
	devpriv->ai_do = 0;
	if (cmd->scan_begin_src != TRIG_TIMER &&
	    cmd->convert_src == TRIG_TIMER) {
		/* cascaded timers 1 and 2 are used for convert timing */
		if (cmd->scan_begin_src == TRIG_EXT)
			devpriv->ai_do = 4;
		else
			devpriv->ai_do = 1;

		comedi_8254_cascade_ns_to_timer(pacer, &cmd->convert_arg,
						devpriv->ai_flags &
						CMDF_ROUND_NEAREST);
		comedi_8254_update_divisors(pacer);

		devpriv->ai_ctrl |= PCI9118_AI_CTRL_TMRTR;

		if (!devpriv->usedma) {
			devpriv->ai_ctrl |= PCI9118_AI_CTRL_INT;
			devpriv->int_ctrl |= PCI9118_INT_CTRL_TIMER;
		}

		if (cmd->scan_begin_src == TRIG_EXT) {
			struct pci9118_dmabuf *dmabuf = &devpriv->dmabuf[0];

			devpriv->ai_cfg |= PCI9118_AI_CFG_AM;
			outl(devpriv->ai_cfg, dev->iobase + PCI9118_AI_CFG_REG);
			comedi_8254_load(pacer, 0, dmabuf->hw >> 1,
					 I8254_MODE0 | I8254_BINARY);
			devpriv->ai_cfg |= PCI9118_AI_CFG_START;
		}
	}

	if (cmd->scan_begin_src == TRIG_TIMER &&
	    cmd->convert_src != TRIG_EXT) {
		if (!devpriv->usedma) {
			dev_err(dev->class_dev,
				"cmd->scan_begin_src=TRIG_TIMER works only with bus mastering!\n");
			return -EIO;
		}

		/* double timed action */
		devpriv->ai_do = 2;

		pci9118_calc_divisors(dev, s,
				      &cmd->scan_begin_arg, &cmd->convert_arg,
				      devpriv->ai_flags,
				      devpriv->ai_n_realscanlen,
				      &pacer->divisor1,
				      &pacer->divisor2,
				      devpriv->ai_add_front);

		devpriv->ai_ctrl |= PCI9118_AI_CTRL_TMRTR;
		devpriv->ai_cfg |= PCI9118_AI_CFG_BM | PCI9118_AI_CFG_BS;
		if (cmd->convert_src == TRIG_NOW && !devpriv->softsshdelay)
			devpriv->ai_cfg |= PCI9118_AI_CFG_BSSH;
		outl(devpriv->ai_n_realscanlen,
		     dev->iobase + PCI9118_AI_BURST_NUM_REG);
	}

	if (cmd->scan_begin_src == TRIG_FOLLOW &&
	    cmd->convert_src == TRIG_EXT) {
		/* external trigger conversion */
		devpriv->ai_do = 3;

		devpriv->ai_ctrl |= PCI9118_AI_CTRL_EXTM;
	}

	if (devpriv->ai_do == 0) {
		dev_err(dev->class_dev,
			"Unable to determine acquisition mode! BUG in (*do_cmdtest)?\n");
		return -EINVAL;
	}

	if (devpriv->usedma)
		devpriv->ai_ctrl |= PCI9118_AI_CTRL_DMA;

	/* set default config (disable burst and triggers) */
	devpriv->ai_cfg = PCI9118_AI_CFG_PDTRG | PCI9118_AI_CFG_PETRG;
	outl(devpriv->ai_cfg, dev->iobase + PCI9118_AI_CFG_REG);
	udelay(1);
	pci9118_ai_reset_fifo(dev);

	/* clear A/D and INT status registers */
	inl(dev->iobase + PCI9118_AI_STATUS_REG);
	inl(dev->iobase + PCI9118_INT_CTRL_REG);

	devpriv->ai_act_dmapos = 0;

	if (devpriv->usedma) {
		pci9118_ai_setup_dma(dev, s);

		outl(0x02000000 | AINT_WRITE_COMPL,
		     devpriv->iobase_a + AMCC_OP_REG_INTCSR);
	} else {
		pci9118_amcc_int_ena(dev, true);
	}

	/* start async command now or wait for internal trigger */
	if (cmd->start_src == TRIG_NOW)
		pci9118_ai_cmd_start(dev);
	else if (cmd->start_src == TRIG_INT)
		s->async->inttrig = pci9118_ai_inttrig;

	/* enable external trigger for command start/stop */
	if (cmd->start_src == TRIG_EXT || cmd->stop_src == TRIG_EXT)
		pci9118_exttrg_enable(dev, true);

	return 0;
}

static int pci9118_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	struct pci9118_private *devpriv = dev->private;
	int err = 0;
	unsigned int flags;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src,
					TRIG_NOW | TRIG_EXT | TRIG_INT);

	flags = TRIG_FOLLOW;
	if (devpriv->master)
		flags |= TRIG_TIMER | TRIG_EXT;
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, flags);

	flags = TRIG_TIMER | TRIG_EXT;
	if (devpriv->master)
		flags |= TRIG_NOW;
	err |= comedi_check_trigger_src(&cmd->convert_src, flags);

	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src,
					TRIG_COUNT | TRIG_NONE | TRIG_EXT);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (cmd->start_src == TRIG_EXT && cmd->scan_begin_src == TRIG_EXT)
		err |= -EINVAL;

	if ((cmd->scan_begin_src & (TRIG_TIMER | TRIG_EXT)) &&
	    (!(cmd->convert_src & (TRIG_TIMER | TRIG_NOW))))
		err |= -EINVAL;

	if ((cmd->scan_begin_src == TRIG_FOLLOW) &&
	    (!(cmd->convert_src & (TRIG_TIMER | TRIG_EXT))))
		err |= -EINVAL;

	if (cmd->stop_src == TRIG_EXT && cmd->scan_begin_src == TRIG_EXT)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	switch (cmd->start_src) {
	case TRIG_NOW:
	case TRIG_EXT:
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_INT:
		/* start_arg is the internal trigger (any value) */
		break;
	}

	if (cmd->scan_begin_src & (TRIG_FOLLOW | TRIG_EXT))
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	if ((cmd->scan_begin_src == TRIG_TIMER) &&
	    (cmd->convert_src == TRIG_TIMER) && (cmd->scan_end_arg == 1)) {
		cmd->scan_begin_src = TRIG_FOLLOW;
		cmd->convert_arg = cmd->scan_begin_arg;
		cmd->scan_begin_arg = 0;
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
						    devpriv->ai_ns_min);
	}

	if (cmd->scan_begin_src == TRIG_EXT) {
		if (cmd->scan_begin_arg) {
			cmd->scan_begin_arg = 0;
			err |= -EINVAL;
			err |= comedi_check_trigger_arg_max(&cmd->scan_end_arg,
							    65535);
		}
	}

	if (cmd->convert_src & (TRIG_TIMER | TRIG_NOW)) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
						    devpriv->ai_ns_min);
	}

	if (cmd->convert_src == TRIG_EXT)
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	err |= comedi_check_trigger_arg_min(&cmd->chanlist_len, 1);

	err |= comedi_check_trigger_arg_min(&cmd->scan_end_arg,
					    cmd->chanlist_len);

	if ((cmd->scan_end_arg % cmd->chanlist_len)) {
		cmd->scan_end_arg =
		    cmd->chanlist_len * (cmd->scan_end_arg / cmd->chanlist_len);
		err |= -EINVAL;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (cmd->convert_src & (TRIG_TIMER | TRIG_NOW)) {
		arg = cmd->convert_arg;
		comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);

		if (cmd->scan_begin_src == TRIG_TIMER &&
		    cmd->convert_src == TRIG_NOW) {
			if (cmd->convert_arg == 0) {
				arg = devpriv->ai_ns_min *
				      (cmd->scan_end_arg + 2);
			} else {
				arg = cmd->convert_arg * cmd->chanlist_len;
			}
			err |= comedi_check_trigger_arg_min(
				&cmd->scan_begin_arg, arg);
		}
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */

	if (cmd->chanlist)
		err |= pci9118_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int pci9118_ai_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned int status;

	status = inl(dev->iobase + PCI9118_AI_STATUS_REG);
	if (status & PCI9118_AI_STATUS_ADRDY)
		return 0;
	return -EBUSY;
}

static void pci9118_ai_start_conv(struct comedi_device *dev)
{
	/* writing any value triggers an A/D conversion */
	outl(0, dev->iobase + PCI9118_SOFTTRG_REG);
}

static int pci9118_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct pci9118_private *devpriv = dev->private;
	unsigned int val;
	int ret;
	int i;

       /*
	* Configure analog input based on the chanspec.
	* Acqusition is software controlled without interrupts.
	*/
	pci9118_set_chanlist(dev, s, 1, &insn->chanspec, 0, 0);

	/* set default config (disable burst and triggers) */
	devpriv->ai_cfg = PCI9118_AI_CFG_PDTRG | PCI9118_AI_CFG_PETRG;
	outl(devpriv->ai_cfg, dev->iobase + PCI9118_AI_CFG_REG);

	pci9118_ai_reset_fifo(dev);

	for (i = 0; i < insn->n; i++) {
		pci9118_ai_start_conv(dev);

		ret = comedi_timeout(dev, s, insn, pci9118_ai_eoc, 0);
		if (ret)
			return ret;

		val = inl(dev->iobase + PCI9118_AI_FIFO_REG);
		if (s->maxdata == 0xffff)
			data[i] = (val & 0xffff) ^ 0x8000;
		else
			data[i] = (val >> 4) & 0xfff;
	}

	return insn->n;
}

static int pci9118_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outl(val, dev->iobase + PCI9118_AO_REG(chan));
	}
	s->readback[chan] = val;

	return insn->n;
}

static int pci9118_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	/*
	 * The digital inputs and outputs share the read register.
	 * bits [7:4] are the digital outputs
	 * bits [3:0] are the digital inputs
	 */
	data[1] = inl(dev->iobase + PCI9118_DIO_REG) & 0xf;

	return insn->n;
}

static int pci9118_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	/*
	 * The digital outputs are set with the same register that
	 * the digital inputs and outputs are read from. But the
	 * outputs are set with bits [3:0] so we can simply write
	 * the s->state to set them.
	 */
	if (comedi_dio_update_state(s, data))
		outl(s->state, dev->iobase + PCI9118_DIO_REG);

	data[1] = s->state;

	return insn->n;
}

static void pci9118_reset(struct comedi_device *dev)
{
	/* reset analog input subsystem */
	outl(0, dev->iobase + PCI9118_INT_CTRL_REG);
	outl(0, dev->iobase + PCI9118_AI_CTRL_REG);
	outl(0, dev->iobase + PCI9118_AI_CFG_REG);
	pci9118_ai_reset_fifo(dev);

	/* clear any pending interrupts and status */
	inl(dev->iobase + PCI9118_INT_CTRL_REG);
	inl(dev->iobase + PCI9118_AI_STATUS_REG);

	/* reset DMA and scan queue */
	outl(0, dev->iobase + PCI9118_AI_BURST_NUM_REG);
	outl(1, dev->iobase + PCI9118_AI_AUTOSCAN_MODE_REG);
	outl(2, dev->iobase + PCI9118_AI_AUTOSCAN_MODE_REG);

	/* reset analog outputs to 0V */
	outl(2047, dev->iobase + PCI9118_AO_REG(0));
	outl(2047, dev->iobase + PCI9118_AO_REG(1));
}

static struct pci_dev *pci9118_find_pci(struct comedi_device *dev,
					struct comedi_devconfig *it)
{
	struct pci_dev *pcidev = NULL;
	int bus = it->options[0];
	int slot = it->options[1];

	for_each_pci_dev(pcidev) {
		if (pcidev->vendor != PCI_VENDOR_ID_AMCC)
			continue;
		if (pcidev->device != 0x80d9)
			continue;
		if (bus || slot) {
			/* requested particular bus/slot */
			if (pcidev->bus->number != bus ||
			    PCI_SLOT(pcidev->devfn) != slot)
				continue;
		}
		return pcidev;
	}
	dev_err(dev->class_dev,
		"no supported board found! (req. bus/slot : %d/%d)\n",
		bus, slot);
	return NULL;
}

static void pci9118_alloc_dma(struct comedi_device *dev)
{
	struct pci9118_private *devpriv = dev->private;
	struct pci9118_dmabuf *dmabuf;
	int order;
	int i;

	for (i = 0; i < 2; i++) {
		dmabuf = &devpriv->dmabuf[i];
		for (order = 2; order >= 0; order--) {
			dmabuf->virt =
			    dma_alloc_coherent(dev->hw_dev, PAGE_SIZE << order,
					       &dmabuf->hw, GFP_KERNEL);
			if (dmabuf->virt)
				break;
		}
		if (!dmabuf->virt)
			break;
		dmabuf->size = PAGE_SIZE << order;

		if (i == 0)
			devpriv->master = 1;
		if (i == 1)
			devpriv->dma_doublebuf = 1;
	}
}

static void pci9118_free_dma(struct comedi_device *dev)
{
	struct pci9118_private *devpriv = dev->private;
	struct pci9118_dmabuf *dmabuf;
	int i;

	if (!devpriv)
		return;

	for (i = 0; i < 2; i++) {
		dmabuf = &devpriv->dmabuf[i];
		if (dmabuf->virt) {
			dma_free_coherent(dev->hw_dev, dmabuf->size,
					  dmabuf->virt, dmabuf->hw);
		}
	}
}

static int pci9118_common_attach(struct comedi_device *dev,
				 int ext_mux, int softsshdelay)
{
	const struct pci9118_boardinfo *board = dev->board_ptr;
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct pci9118_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	int i;
	u16 u16w;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	pci_set_master(pcidev);

	devpriv->iobase_a = pci_resource_start(pcidev, 0);
	dev->iobase = pci_resource_start(pcidev, 2);

	dev->pacer = comedi_8254_init(dev->iobase + PCI9118_TIMER_BASE,
				      I8254_OSC_BASE_4MHZ, I8254_IO32, 0);
	if (!dev->pacer)
		return -ENOMEM;

	pci9118_reset(dev);

	if (pcidev->irq) {
		ret = request_irq(pcidev->irq, pci9118_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0) {
			dev->irq = pcidev->irq;

			pci9118_alloc_dma(dev);
		}
	}

	if (ext_mux > 0) {
		if (ext_mux > 256)
			ext_mux = 256;	/* max 256 channels! */
		if (softsshdelay > 0)
			if (ext_mux > 128)
				ext_mux = 128;
		devpriv->usemux = 1;
	} else {
		devpriv->usemux = 0;
	}

	if (softsshdelay < 0) {
		/* select sample&hold signal polarity */
		devpriv->softsshdelay = -softsshdelay;
		devpriv->softsshsample = 0x80;
		devpriv->softsshhold = 0x00;
	} else {
		devpriv->softsshdelay = softsshdelay;
		devpriv->softsshsample = 0x00;
		devpriv->softsshhold = 0x80;
	}

	pci_read_config_word(pcidev, PCI_COMMAND, &u16w);
	pci_write_config_word(pcidev, PCI_COMMAND, u16w | 64);
				/* Enable parity check for parity error */

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_COMMON | SDF_GROUND | SDF_DIFF;
	s->n_chan	= (devpriv->usemux) ? ext_mux : 16;
	s->maxdata	= board->ai_is_16bit ? 0xffff : 0x0fff;
	s->range_table	= board->is_hg ? &pci9118hg_ai_range
				       : &pci9118_ai_range;
	s->insn_read	= pci9118_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= 255;
		s->do_cmdtest	= pci9118_ai_cmdtest;
		s->do_cmd	= pci9118_ai_cmd;
		s->cancel	= pci9118_ai_cancel;
		s->munge	= pci9118_ai_munge;
	}

	if (s->maxdata == 0xffff) {
		/*
		 * 16-bit samples are from an ADS7805 A/D converter.
		 * Minimum sampling rate is 10us.
		 */
		devpriv->ai_ns_min = 10000;
	} else {
		/*
		 * 12-bit samples are from an ADS7800 A/D converter.
		 * Minimum sampling rate is 3us.
		 */
		devpriv->ai_ns_min = 3000;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan	= 2;
	s->maxdata	= 0x0fff;
	s->range_table	= &range_bipolar10;
	s->insn_write	= pci9118_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* the analog outputs were reset to 0V, make the readback match */
	for (i = 0; i < s->n_chan; i++)
		s->readback[i] = 2047;

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci9118_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci9118_do_insn_bits;

	/* get the current state of the digital outputs */
	s->state = inl(dev->iobase + PCI9118_DIO_REG) >> 4;

	return 0;
}

static int pci9118_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct pci_dev *pcidev;
	int ext_mux, softsshdelay;

	ext_mux = it->options[2];
	softsshdelay = it->options[4];

	pcidev = pci9118_find_pci(dev, it);
	if (!pcidev)
		return -EIO;
	comedi_set_hw_dev(dev, &pcidev->dev);

	return pci9118_common_attach(dev, ext_mux, softsshdelay);
}

static int pci9118_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct pci9118_boardinfo *board = NULL;

	if (context < ARRAY_SIZE(pci9118_boards))
		board = &pci9118_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	/*
	 * Need to 'get' the PCI device to match the 'put' in pci9118_detach().
	 * (The 'put' also matches the implicit 'get' by pci9118_find_pci().)
	 */
	pci_dev_get(pcidev);
	/* no external mux, no sample-hold delay */
	return pci9118_common_attach(dev, 0, 0);
}

static void pci9118_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->iobase)
		pci9118_reset(dev);
	comedi_pci_detach(dev);
	pci9118_free_dma(dev);
	pci_dev_put(pcidev);
}

static struct comedi_driver adl_pci9118_driver = {
	.driver_name	= "adl_pci9118",
	.module		= THIS_MODULE,
	.attach		= pci9118_attach,
	.auto_attach	= pci9118_auto_attach,
	.detach		= pci9118_detach,
	.num_names	= ARRAY_SIZE(pci9118_boards),
	.board_name	= &pci9118_boards[0].name,
	.offset		= sizeof(struct pci9118_boardinfo),
};

static int adl_pci9118_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &adl_pci9118_driver,
				      id->driver_data);
}

/* FIXME: All the supported board types have the same device ID! */
static const struct pci_device_id adl_pci9118_pci_table[] = {
	{ PCI_VDEVICE(AMCC, 0x80d9), BOARD_PCI9118DG },
/*	{ PCI_VDEVICE(AMCC, 0x80d9), BOARD_PCI9118HG }, */
/*	{ PCI_VDEVICE(AMCC, 0x80d9), BOARD_PCI9118HR }, */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adl_pci9118_pci_table);

static struct pci_driver adl_pci9118_pci_driver = {
	.name		= "adl_pci9118",
	.id_table	= adl_pci9118_pci_table,
	.probe		= adl_pci9118_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(adl_pci9118_driver, adl_pci9118_pci_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
