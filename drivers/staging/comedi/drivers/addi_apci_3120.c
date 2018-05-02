// SPDX-License-Identifier: GPL-2.0+
/*
 * addi_apci_3120.c
 * Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data.com
 *	info@addi-data.com
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"
#include "amcc_s5933.h"

/*
 * PCI BAR 0 register map (devpriv->amcc)
 * see amcc_s5933.h for register and bit defines
 */
#define APCI3120_FIFO_ADVANCE_ON_BYTE_2		BIT(29)

/*
 * PCI BAR 1 register map (dev->iobase)
 */
#define APCI3120_AI_FIFO_REG			0x00
#define APCI3120_CTRL_REG			0x00
#define APCI3120_CTRL_EXT_TRIG			BIT(15)
#define APCI3120_CTRL_GATE(x)			BIT(12 + (x))
#define APCI3120_CTRL_PR(x)			(((x) & 0xf) << 8)
#define APCI3120_CTRL_PA(x)			(((x) & 0xf) << 0)
#define APCI3120_AI_SOFTTRIG_REG		0x02
#define APCI3120_STATUS_REG			0x02
#define APCI3120_STATUS_EOC_INT			BIT(15)
#define APCI3120_STATUS_AMCC_INT		BIT(14)
#define APCI3120_STATUS_EOS_INT			BIT(13)
#define APCI3120_STATUS_TIMER2_INT		BIT(12)
#define APCI3120_STATUS_INT_MASK		(0xf << 12)
#define APCI3120_STATUS_TO_DI_BITS(x)		(((x) >> 8) & 0xf)
#define APCI3120_STATUS_TO_VERSION(x)		(((x) >> 4) & 0xf)
#define APCI3120_STATUS_FIFO_FULL		BIT(2)
#define APCI3120_STATUS_FIFO_EMPTY		BIT(1)
#define APCI3120_STATUS_DA_READY		BIT(0)
#define APCI3120_TIMER_REG			0x04
#define APCI3120_CHANLIST_REG			0x06
#define APCI3120_CHANLIST_INDEX(x)		(((x) & 0xf) << 8)
#define APCI3120_CHANLIST_UNIPOLAR		BIT(7)
#define APCI3120_CHANLIST_GAIN(x)		(((x) & 0x3) << 4)
#define APCI3120_CHANLIST_MUX(x)		(((x) & 0xf) << 0)
#define APCI3120_AO_REG(x)			(0x08 + (((x) / 4) * 2))
#define APCI3120_AO_MUX(x)			(((x) & 0x3) << 14)
#define APCI3120_AO_DATA(x)			((x) << 0)
#define APCI3120_TIMER_MODE_REG			0x0c
#define APCI3120_TIMER_MODE(_t, _m)		((_m) << ((_t) * 2))
#define APCI3120_TIMER_MODE0			0  /* I8254_MODE0 */
#define APCI3120_TIMER_MODE2			1  /* I8254_MODE2 */
#define APCI3120_TIMER_MODE4			2  /* I8254_MODE4 */
#define APCI3120_TIMER_MODE5			3  /* I8254_MODE5 */
#define APCI3120_TIMER_MODE_MASK(_t)		(3 << ((_t) * 2))
#define APCI3120_CTR0_REG			0x0d
#define APCI3120_CTR0_DO_BITS(x)		((x) << 4)
#define APCI3120_CTR0_TIMER_SEL(x)		((x) << 0)
#define APCI3120_MODE_REG			0x0e
#define APCI3120_MODE_TIMER2_CLK(x)		(((x) & 0x3) << 6)
#define APCI3120_MODE_TIMER2_CLK_OSC		APCI3120_MODE_TIMER2_CLK(0)
#define APCI3120_MODE_TIMER2_CLK_OUT1		APCI3120_MODE_TIMER2_CLK(1)
#define APCI3120_MODE_TIMER2_CLK_EOC		APCI3120_MODE_TIMER2_CLK(2)
#define APCI3120_MODE_TIMER2_CLK_EOS		APCI3120_MODE_TIMER2_CLK(3)
#define APCI3120_MODE_TIMER2_CLK_MASK		APCI3120_MODE_TIMER2_CLK(3)
#define APCI3120_MODE_TIMER2_AS(x)		(((x) & 0x3) << 4)
#define APCI3120_MODE_TIMER2_AS_TIMER		APCI3120_MODE_TIMER2_AS(0)
#define APCI3120_MODE_TIMER2_AS_COUNTER		APCI3120_MODE_TIMER2_AS(1)
#define APCI3120_MODE_TIMER2_AS_WDOG		APCI3120_MODE_TIMER2_AS(2)
#define APCI3120_MODE_TIMER2_AS_MASK		APCI3120_MODE_TIMER2_AS(3)
#define APCI3120_MODE_SCAN_ENA			BIT(3)
#define APCI3120_MODE_TIMER2_IRQ_ENA		BIT(2)
#define APCI3120_MODE_EOS_IRQ_ENA		BIT(1)
#define APCI3120_MODE_EOC_IRQ_ENA		BIT(0)

/*
 * PCI BAR 2 register map (devpriv->addon)
 */
#define APCI3120_ADDON_ADDR_REG			0x00
#define APCI3120_ADDON_DATA_REG			0x02
#define APCI3120_ADDON_CTRL_REG			0x04
#define APCI3120_ADDON_CTRL_AMWEN_ENA		BIT(1)
#define APCI3120_ADDON_CTRL_A2P_FIFO_ENA	BIT(0)

/*
 * Board revisions
 */
#define APCI3120_REVA				0xa
#define APCI3120_REVB				0xb
#define APCI3120_REVA_OSC_BASE			70	/* 70ns = 14.29MHz */
#define APCI3120_REVB_OSC_BASE			50	/* 50ns = 20MHz */

static const struct comedi_lrange apci3120_ai_range = {
	8, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2),
		BIP_RANGE(1),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1)
	}
};

enum apci3120_boardid {
	BOARD_APCI3120,
	BOARD_APCI3001,
};

struct apci3120_board {
	const char *name;
	unsigned int ai_is_16bit:1;
	unsigned int has_ao:1;
};

static const struct apci3120_board apci3120_boardtypes[] = {
	[BOARD_APCI3120] = {
		.name		= "apci3120",
		.ai_is_16bit	= 1,
		.has_ao		= 1,
	},
	[BOARD_APCI3001] = {
		.name		= "apci3001",
	},
};

struct apci3120_dmabuf {
	unsigned short *virt;
	dma_addr_t hw;
	unsigned int size;
	unsigned int use_size;
};

struct apci3120_private {
	unsigned long amcc;
	unsigned long addon;
	unsigned int osc_base;
	unsigned int use_dma:1;
	unsigned int use_double_buffer:1;
	unsigned int cur_dmabuf:1;
	struct apci3120_dmabuf dmabuf[2];
	unsigned char do_bits;
	unsigned char timer_mode;
	unsigned char mode;
	unsigned short ctrl;
};

static void apci3120_addon_write(struct comedi_device *dev,
				 unsigned int val, unsigned int reg)
{
	struct apci3120_private *devpriv = dev->private;

	/* 16-bit interface for AMCC add-on registers */

	outw(reg, devpriv->addon + APCI3120_ADDON_ADDR_REG);
	outw(val & 0xffff, devpriv->addon + APCI3120_ADDON_DATA_REG);

	outw(reg + 2, devpriv->addon + APCI3120_ADDON_ADDR_REG);
	outw((val >> 16) & 0xffff, devpriv->addon + APCI3120_ADDON_DATA_REG);
}

static void apci3120_init_dma(struct comedi_device *dev,
			      struct apci3120_dmabuf *dmabuf)
{
	struct apci3120_private *devpriv = dev->private;

	/* AMCC - enable transfer count and reset A2P FIFO */
	outl(AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
	     devpriv->amcc + AMCC_OP_REG_AGCSTS);

	/* Add-On - enable transfer count and reset A2P FIFO */
	apci3120_addon_write(dev, AGCSTS_TC_ENABLE | AGCSTS_RESET_A2P_FIFO,
			     AMCC_OP_REG_AGCSTS);

	/* AMCC - enable transfers and reset A2P flags */
	outl(RESET_A2P_FLAGS | EN_A2P_TRANSFERS,
	     devpriv->amcc + AMCC_OP_REG_MCSR);

	/* Add-On - DMA start address */
	apci3120_addon_write(dev, dmabuf->hw, AMCC_OP_REG_AMWAR);

	/* Add-On - Number of acquisitions */
	apci3120_addon_write(dev, dmabuf->use_size, AMCC_OP_REG_AMWTC);

	/* AMCC - enable write complete (DMA) and set FIFO advance */
	outl(APCI3120_FIFO_ADVANCE_ON_BYTE_2 | AINT_WRITE_COMPL,
	     devpriv->amcc + AMCC_OP_REG_INTCSR);

	/* Add-On - enable DMA */
	outw(APCI3120_ADDON_CTRL_AMWEN_ENA | APCI3120_ADDON_CTRL_A2P_FIFO_ENA,
	     devpriv->addon + APCI3120_ADDON_CTRL_REG);
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

	apci3120_init_dma(dev, dmabuf0);
}

/*
 * There are three timers on the board. They all use the same base
 * clock with a fixed prescaler for each timer. The base clock used
 * depends on the board version and type.
 *
 * APCI-3120 Rev A boards OSC = 14.29MHz base clock (~70ns)
 * APCI-3120 Rev B boards OSC = 20MHz base clock (50ns)
 * APCI-3001 boards OSC = 20MHz base clock (50ns)
 *
 * The prescalers for each timer are:
 * Timer 0 CLK = OSC/10
 * Timer 1 CLK = OSC/1000
 * Timer 2 CLK = OSC/1000
 */
static unsigned int apci3120_ns_to_timer(struct comedi_device *dev,
					 unsigned int timer,
					 unsigned int ns,
					 unsigned int flags)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int prescale = (timer == 0) ? 10 : 1000;
	unsigned int timer_base = devpriv->osc_base * prescale;
	unsigned int divisor;

	switch (flags & CMDF_ROUND_MASK) {
	case CMDF_ROUND_UP:
		divisor = DIV_ROUND_UP(ns, timer_base);
		break;
	case CMDF_ROUND_DOWN:
		divisor = ns / timer_base;
		break;
	case CMDF_ROUND_NEAREST:
	default:
		divisor = DIV_ROUND_CLOSEST(ns, timer_base);
		break;
	}

	if (timer == 2) {
		/* timer 2 is 24-bits */
		if (divisor > 0x00ffffff)
			divisor = 0x00ffffff;
	} else {
		/* timers 0 and 1 are 16-bits */
		if (divisor > 0xffff)
			divisor = 0xffff;
	}
	/* the timers require a minimum divisor of 2 */
	if (divisor < 2)
		divisor = 2;

	return divisor;
}

static void apci3120_clr_timer2_interrupt(struct comedi_device *dev)
{
	/* a dummy read of APCI3120_CTR0_REG clears the timer 2 interrupt */
	inb(dev->iobase + APCI3120_CTR0_REG);
}

static void apci3120_timer_write(struct comedi_device *dev,
				 unsigned int timer, unsigned int val)
{
	struct apci3120_private *devpriv = dev->private;

	/* write 16-bit value to timer (lower 16-bits of timer 2) */
	outb(APCI3120_CTR0_DO_BITS(devpriv->do_bits) |
	     APCI3120_CTR0_TIMER_SEL(timer),
	     dev->iobase + APCI3120_CTR0_REG);
	outw(val & 0xffff, dev->iobase + APCI3120_TIMER_REG);

	if (timer == 2) {
		/* write upper 16-bits to timer 2 */
		outb(APCI3120_CTR0_DO_BITS(devpriv->do_bits) |
		     APCI3120_CTR0_TIMER_SEL(timer + 1),
		     dev->iobase + APCI3120_CTR0_REG);
		outw((val >> 16) & 0xffff, dev->iobase + APCI3120_TIMER_REG);
	}
}

static unsigned int apci3120_timer_read(struct comedi_device *dev,
					unsigned int timer)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int val;

	/* read 16-bit value from timer (lower 16-bits of timer 2) */
	outb(APCI3120_CTR0_DO_BITS(devpriv->do_bits) |
	     APCI3120_CTR0_TIMER_SEL(timer),
	     dev->iobase + APCI3120_CTR0_REG);
	val = inw(dev->iobase + APCI3120_TIMER_REG);

	if (timer == 2) {
		/* read upper 16-bits from timer 2 */
		outb(APCI3120_CTR0_DO_BITS(devpriv->do_bits) |
		     APCI3120_CTR0_TIMER_SEL(timer + 1),
		     dev->iobase + APCI3120_CTR0_REG);
		val |= (inw(dev->iobase + APCI3120_TIMER_REG) << 16);
	}

	return val;
}

static void apci3120_timer_set_mode(struct comedi_device *dev,
				    unsigned int timer, unsigned int mode)
{
	struct apci3120_private *devpriv = dev->private;

	devpriv->timer_mode &= ~APCI3120_TIMER_MODE_MASK(timer);
	devpriv->timer_mode |= APCI3120_TIMER_MODE(timer, mode);
	outb(devpriv->timer_mode, dev->iobase + APCI3120_TIMER_MODE_REG);
}

static void apci3120_timer_enable(struct comedi_device *dev,
				  unsigned int timer, bool enable)
{
	struct apci3120_private *devpriv = dev->private;

	if (enable)
		devpriv->ctrl |= APCI3120_CTRL_GATE(timer);
	else
		devpriv->ctrl &= ~APCI3120_CTRL_GATE(timer);
	outw(devpriv->ctrl, dev->iobase + APCI3120_CTRL_REG);
}

static void apci3120_exttrig_enable(struct comedi_device *dev, bool enable)
{
	struct apci3120_private *devpriv = dev->private;

	if (enable)
		devpriv->ctrl |= APCI3120_CTRL_EXT_TRIG;
	else
		devpriv->ctrl &= ~APCI3120_CTRL_EXT_TRIG;
	outw(devpriv->ctrl, dev->iobase + APCI3120_CTRL_REG);
}

static void apci3120_set_chanlist(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  int n_chan, unsigned int *chanlist)
{
	struct apci3120_private *devpriv = dev->private;
	int i;

	/* set chanlist for scan */
	for (i = 0; i < n_chan; i++) {
		unsigned int chan = CR_CHAN(chanlist[i]);
		unsigned int range = CR_RANGE(chanlist[i]);
		unsigned int val;

		val = APCI3120_CHANLIST_MUX(chan) |
		      APCI3120_CHANLIST_GAIN(range) |
		      APCI3120_CHANLIST_INDEX(i);

		if (comedi_range_is_unipolar(s, range))
			val |= APCI3120_CHANLIST_UNIPOLAR;

		outw(val, dev->iobase + APCI3120_CHANLIST_REG);
	}

	/* a dummy read of APCI3120_TIMER_MODE_REG resets the ai FIFO */
	inw(dev->iobase + APCI3120_TIMER_MODE_REG);

	/* set scan length (PR) and scan start (PA) */
	devpriv->ctrl = APCI3120_CTRL_PR(n_chan - 1) | APCI3120_CTRL_PA(0);
	outw(devpriv->ctrl, dev->iobase + APCI3120_CTRL_REG);

	/* enable chanlist scanning if necessary */
	if (n_chan > 1)
		devpriv->mode |= APCI3120_MODE_SCAN_ENA;
}

static void apci3120_interrupt_dma(struct comedi_device *dev,
				   struct comedi_subdevice *s)
{
	struct apci3120_private *devpriv = dev->private;
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
	if (nsamples) {
		comedi_buf_write_samples(s, dmabuf->virt, nsamples);

		if (!(cmd->flags & CMDF_WAKE_EOS))
			async->events |= COMEDI_CB_EOS;
	}

	if ((async->events & COMEDI_CB_CANCEL_MASK) ||
	    (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg))
		return;

	if (devpriv->use_double_buffer) {
		/* switch DMA buffers for next interrupt */
		devpriv->cur_dmabuf = !devpriv->cur_dmabuf;
		dmabuf = &devpriv->dmabuf[devpriv->cur_dmabuf];
		apci3120_init_dma(dev, dmabuf);
	} else {
		/* restart DMA if not using double buffering */
		apci3120_init_dma(dev, dmabuf);
	}
}

static irqreturn_t apci3120_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci3120_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
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

	if (int_amcc & MASTER_ABORT_INT)
		dev_err(dev->class_dev, "AMCC IRQ - MASTER DMA ABORT!\n");
	if (int_amcc & TARGET_ABORT_INT)
		dev_err(dev->class_dev, "AMCC IRQ - TARGET DMA ABORT!\n");

	if ((status & APCI3120_STATUS_EOS_INT) &&
	    (devpriv->mode & APCI3120_MODE_EOS_IRQ_ENA)) {
		unsigned short val;
		int i;

		for (i = 0; i < cmd->chanlist_len; i++) {
			val = inw(dev->iobase + APCI3120_AI_FIFO_REG);
			comedi_buf_write_samples(s, &val, 1);
		}

		devpriv->mode |= APCI3120_MODE_EOS_IRQ_ENA;
		outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);
	}

	if (status & APCI3120_STATUS_TIMER2_INT) {
		/*
		 * for safety...
		 * timer2 interrupts are not enabled in the driver
		 */
		apci3120_clr_timer2_interrupt(dev);
	}

	if (status & APCI3120_STATUS_AMCC_INT) {
		/* AMCC- Clear write complete interrupt (DMA) */
		outl(AINT_WT_COMPLETE, devpriv->amcc + AMCC_OP_REG_INTCSR);

		/* do some data transfer */
		apci3120_interrupt_dma(dev, s);
	}

	if (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg)
		async->events |= COMEDI_CB_EOA;

	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
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

	devpriv->cur_dmabuf = 0;

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

	if (devpriv->use_dma)
		apci3120_setup_dma(dev, s);
	else
		devpriv->mode |= APCI3120_MODE_EOS_IRQ_ENA;

	/* set mode to enable acquisition */
	outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);

	if (cmd->scan_begin_src == TRIG_TIMER)
		apci3120_timer_enable(dev, 1, true);
	apci3120_timer_enable(dev, 0, true);

	return 0;
}

static int apci3120_ai_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	unsigned int arg;
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_TIMER) {	/* Test Delay timing */
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
						    100000);
	}

	/* minimum conversion time per sample is 10us */
	err |= comedi_check_trigger_arg_min(&cmd->convert_arg, 10000);

	err |= comedi_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/*  TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		/* scan begin must be larger than the scan time */
		arg = cmd->convert_arg * cmd->scan_end_arg;
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg, arg);
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */

	return 0;
}

static int apci3120_cancel(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct apci3120_private *devpriv = dev->private;

	/* Add-On - disable DMA */
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
	devpriv->cur_dmabuf = 0;

	return 0;
}

static int apci3120_ai_eoc(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + APCI3120_STATUS_REG);
	if ((status & APCI3120_STATUS_EOC_INT) == 0)
		return 0;
	return -EBUSY;
}

static int apci3120_ai_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int divisor;
	int ret;
	int i;

	/* set mode for A/D conversions by software trigger with timer 0 */
	devpriv->mode = APCI3120_MODE_TIMER2_CLK_OSC |
			APCI3120_MODE_TIMER2_AS_TIMER;
	outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);

	/* load chanlist for single channel scan */
	apci3120_set_chanlist(dev, s, 1, &insn->chanspec);

	/*
	 * Timer 0 is used in MODE4 (software triggered strobe) to set the
	 * conversion time for each acquisition. Each conversion is triggered
	 * when the divisor is written to the timer, The conversion is done
	 * when the EOC bit in the status register is '0'.
	 */
	apci3120_timer_set_mode(dev, 0, APCI3120_TIMER_MODE4);
	apci3120_timer_enable(dev, 0, true);

	/* fixed conversion time of 10 us */
	divisor = apci3120_ns_to_timer(dev, 0, 10000, CMDF_ROUND_NEAREST);

	for (i = 0; i < insn->n; i++) {
		/* trigger conversion */
		apci3120_timer_write(dev, 0, divisor);

		ret = comedi_timeout(dev, s, insn, apci3120_ai_eoc, 0);
		if (ret)
			return ret;

		data[i] = inw(dev->iobase + APCI3120_AI_FIFO_REG);
	}

	return insn->n;
}

static int apci3120_ao_ready(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + APCI3120_STATUS_REG);
	if (status & APCI3120_STATUS_DA_READY)
		return 0;
	return -EBUSY;
}

static int apci3120_ao_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];
		int ret;

		ret = comedi_timeout(dev, s, insn, apci3120_ao_ready, 0);
		if (ret)
			return ret;

		outw(APCI3120_AO_MUX(chan) | APCI3120_AO_DATA(val),
		     dev->iobase + APCI3120_AO_REG(chan));

		s->readback[chan] = val;
	}

	return insn->n;
}

static int apci3120_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int status;

	status = inw(dev->iobase + APCI3120_STATUS_REG);
	data[1] = APCI3120_STATUS_TO_DI_BITS(status);

	return insn->n;
}

static int apci3120_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;

	if (comedi_dio_update_state(s, data)) {
		devpriv->do_bits = s->state;
		outb(APCI3120_CTR0_DO_BITS(devpriv->do_bits),
		     dev->iobase + APCI3120_CTR0_REG);
	}

	data[1] = s->state;

	return insn->n;
}

static int apci3120_timer_insn_config(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	struct apci3120_private *devpriv = dev->private;
	unsigned int divisor;
	unsigned int status;
	unsigned int mode;
	unsigned int timer_mode;

	switch (data[0]) {
	case INSN_CONFIG_ARM:
		apci3120_clr_timer2_interrupt(dev);
		divisor = apci3120_ns_to_timer(dev, 2, data[1],
					       CMDF_ROUND_DOWN);
		apci3120_timer_write(dev, 2, divisor);
		apci3120_timer_enable(dev, 2, true);
		break;

	case INSN_CONFIG_DISARM:
		apci3120_timer_enable(dev, 2, false);
		apci3120_clr_timer2_interrupt(dev);
		break;

	case INSN_CONFIG_GET_COUNTER_STATUS:
		data[1] = 0;
		data[2] = COMEDI_COUNTER_ARMED | COMEDI_COUNTER_COUNTING |
			  COMEDI_COUNTER_TERMINAL_COUNT;

		if (devpriv->ctrl & APCI3120_CTRL_GATE(2)) {
			data[1] |= COMEDI_COUNTER_ARMED;
			data[1] |= COMEDI_COUNTER_COUNTING;
		}
		status = inw(dev->iobase + APCI3120_STATUS_REG);
		if (status & APCI3120_STATUS_TIMER2_INT) {
			data[1] &= ~COMEDI_COUNTER_COUNTING;
			data[1] |= COMEDI_COUNTER_TERMINAL_COUNT;
		}
		break;

	case INSN_CONFIG_SET_COUNTER_MODE:
		switch (data[1]) {
		case I8254_MODE0:
			mode = APCI3120_MODE_TIMER2_AS_COUNTER;
			timer_mode = APCI3120_TIMER_MODE0;
			break;
		case I8254_MODE2:
			mode = APCI3120_MODE_TIMER2_AS_TIMER;
			timer_mode = APCI3120_TIMER_MODE2;
			break;
		case I8254_MODE4:
			mode = APCI3120_MODE_TIMER2_AS_TIMER;
			timer_mode = APCI3120_TIMER_MODE4;
			break;
		case I8254_MODE5:
			mode = APCI3120_MODE_TIMER2_AS_WDOG;
			timer_mode = APCI3120_TIMER_MODE5;
			break;
		default:
			return -EINVAL;
		}
		apci3120_timer_enable(dev, 2, false);
		apci3120_clr_timer2_interrupt(dev);
		apci3120_timer_set_mode(dev, 2, timer_mode);
		devpriv->mode &= ~APCI3120_MODE_TIMER2_AS_MASK;
		devpriv->mode |= mode;
		outb(devpriv->mode, dev->iobase + APCI3120_MODE_REG);
		break;

	default:
		return -EINVAL;
	}

	return insn->n;
}

static int apci3120_timer_insn_read(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = apci3120_timer_read(dev, 2);

	return insn->n;
}

static void apci3120_dma_alloc(struct comedi_device *dev)
{
	struct apci3120_private *devpriv = dev->private;
	struct apci3120_dmabuf *dmabuf;
	int order;
	int i;

	for (i = 0; i < 2; i++) {
		dmabuf = &devpriv->dmabuf[i];
		for (order = 2; order >= 0; order--) {
			dmabuf->virt = dma_alloc_coherent(dev->hw_dev,
							  PAGE_SIZE << order,
							  &dmabuf->hw,
							  GFP_KERNEL);
			if (dmabuf->virt)
				break;
		}
		if (!dmabuf->virt)
			break;
		dmabuf->size = PAGE_SIZE << order;

		if (i == 0)
			devpriv->use_dma = 1;
		if (i == 1)
			devpriv->use_double_buffer = 1;
	}
}

static void apci3120_dma_free(struct comedi_device *dev)
{
	struct apci3120_private *devpriv = dev->private;
	struct apci3120_dmabuf *dmabuf;
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

static void apci3120_reset(struct comedi_device *dev)
{
	/* disable all interrupt sources */
	outb(0, dev->iobase + APCI3120_MODE_REG);

	/* disable all counters, ext trigger, and reset scan */
	outw(0, dev->iobase + APCI3120_CTRL_REG);

	/* clear interrupt status */
	inw(dev->iobase + APCI3120_STATUS_REG);
}

static int apci3120_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct apci3120_board *board = NULL;
	struct apci3120_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int status;
	int ret;

	if (context < ARRAY_SIZE(apci3120_boardtypes))
		board = &apci3120_boardtypes[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	pci_set_master(pcidev);

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->amcc = pci_resource_start(pcidev, 0);
	devpriv->addon = pci_resource_start(pcidev, 2);

	apci3120_reset(dev);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci3120_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0) {
			dev->irq = pcidev->irq;

			apci3120_dma_alloc(dev);
		}
	}

	status = inw(dev->iobase + APCI3120_STATUS_REG);
	if (APCI3120_STATUS_TO_VERSION(status) == APCI3120_REVB ||
	    context == BOARD_APCI3001)
		devpriv->osc_base = APCI3120_REVB_OSC_BASE;
	else
		devpriv->osc_base = APCI3120_REVA_OSC_BASE;

	ret = comedi_alloc_subdevices(dev, 5);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_COMMON | SDF_GROUND | SDF_DIFF;
	s->n_chan	= 16;
	s->maxdata	= board->ai_is_16bit ? 0xffff : 0x0fff;
	s->range_table	= &apci3120_ai_range;
	s->insn_read	= apci3120_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= s->n_chan;
		s->do_cmdtest	= apci3120_ai_cmdtest;
		s->do_cmd	= apci3120_ai_cmd;
		s->cancel	= apci3120_cancel;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	if (board->has_ao) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan	= 8;
		s->maxdata	= 0x3fff;
		s->range_table	= &range_bipolar10;
		s->insn_write	= apci3120_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;
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
	s->insn_bits	= apci3120_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci3120_do_insn_bits;

	/* Timer subdevice */
	s = &dev->subdevices[4];
	s->type		= COMEDI_SUBD_TIMER;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 1;
	s->maxdata	= 0x00ffffff;
	s->insn_config	= apci3120_timer_insn_config;
	s->insn_read	= apci3120_timer_insn_read;

	return 0;
}

static void apci3120_detach(struct comedi_device *dev)
{
	comedi_pci_detach(dev);
	apci3120_dma_free(dev);
}

static struct comedi_driver apci3120_driver = {
	.driver_name	= "addi_apci_3120",
	.module		= THIS_MODULE,
	.auto_attach	= apci3120_auto_attach,
	.detach		= apci3120_detach,
};

static int apci3120_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci3120_driver, id->driver_data);
}

static const struct pci_device_id apci3120_pci_table[] = {
	{ PCI_VDEVICE(AMCC, 0x818d), BOARD_APCI3120 },
	{ PCI_VDEVICE(AMCC, 0x828d), BOARD_APCI3001 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3120_pci_table);

static struct pci_driver apci3120_pci_driver = {
	.name		= "addi_apci_3120",
	.id_table	= apci3120_pci_table,
	.probe		= apci3120_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci3120_driver, apci3120_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("ADDI-DATA APCI-3120, Analog input board");
MODULE_LICENSE("GPL");
