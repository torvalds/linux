#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

/*
 * PCI BAR 0 register map (devpriv->amcc)
 * see amcc_s5933.h for register and bit defines
 */
#define APCI3120_FIFO_ADVANCE_ON_BYTE_2		(1 << 29)

/*
 * PCI BAR 1 register map (dev->iobase)
 */
#define APCI3120_CTRL_REG			0x00
#define APCI3120_CTRL_EXT_TRIG			(1 << 15)
#define APCI3120_CTRL_GATE(x)			(1 << (12 + (x)))
#define APCI3120_CTRL_PR(x)			(((x) & 0xf) << 8)
#define APCI3120_CTRL_PA(x)			(((x) & 0xf) << 0)
#define APCI3120_STATUS_REG			0x02
#define APCI3120_STATUS_EOC_INT			(1 << 15)
#define APCI3120_STATUS_AMCC_INT		(1 << 14)
#define APCI3120_STATUS_EOS_INT			(1 << 13)
#define APCI3120_STATUS_TIMER2_INT		(1 << 12)
#define APCI3120_STATUS_INT_MASK		(0xf << 12)
#define APCI3120_STATUS_TO_DI_BITS(x)		(((x) >> 8) & 0xf)
#define APCI3120_STATUS_TO_VERSION(x)		(((x) >> 4) & 0xf)
#define APCI3120_STATUS_FIFO_FULL		(1 << 2)
#define APCI3120_STATUS_FIFO_EMPTY		(1 << 1)
#define APCI3120_STATUS_DA_READY		(1 << 0)
#define APCI3120_TIMER_REG			0x04
#define APCI3120_CHANLIST_REG			0x06
#define APCI3120_CHANLIST_INDEX(x)		(((x) & 0xf) << 8)
#define APCI3120_CHANLIST_UNIPOLAR		(1 << 7)
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
#define APCI3120_MODE_TIMER2_CLK_OSC		(0 << 6)
#define APCI3120_MODE_TIMER2_CLK_OUT1		(1 << 6)
#define APCI3120_MODE_TIMER2_CLK_EOC		(2 << 6)
#define APCI3120_MODE_TIMER2_CLK_EOS		(3 << 6)
#define APCI3120_MODE_TIMER2_CLK_MASK		(3 << 6)
#define APCI3120_MODE_TIMER2_AS_TIMER		(0 << 4)
#define APCI3120_MODE_TIMER2_AS_COUNTER		(1 << 4)
#define APCI3120_MODE_TIMER2_AS_WDOG		(2 << 4)
#define APCI3120_MODE_TIMER2_AS_MASK		(3 << 4)  /* sets AS_TIMER */
#define APCI3120_MODE_SCAN_ENA			(1 << 3)
#define APCI3120_MODE_TIMER2_IRQ_ENA		(1 << 2)
#define APCI3120_MODE_EOS_IRQ_ENA		(1 << 1)
#define APCI3120_MODE_EOC_IRQ_ENA		(1 << 0)

/*
 * PCI BAR 2 register map (devpriv->addon)
 */
#define APCI3120_ADDON_ADDR_REG			0x00
#define APCI3120_ADDON_DATA_REG			0x02
#define APCI3120_ADDON_CTRL_REG			0x04
#define APCI3120_ADDON_CTRL_AMWEN_ENA		(1 << 1)
#define APCI3120_ADDON_CTRL_A2P_FIFO_ENA	(1 << 0)

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
	unsigned short us_UseDma;
	unsigned char b_DmaDoubleBuffer;
	unsigned int ui_DmaActualBuffer;
	struct apci3120_dmabuf dmabuf[2];
	unsigned char do_bits;
	unsigned char timer_mode;
	unsigned char mode;
	unsigned short ctrl;
	unsigned char b_Timer2Mode;
	unsigned char b_Timer2Interrupt;
	struct task_struct *tsk_Current;
};

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

static void apci3120_ai_reset_fifo(struct comedi_device *dev)
{
	/* a dummy read of APCI3120_TIMER_MODE_REG resets the ai FIFO */
	inw(dev->iobase + APCI3120_TIMER_MODE_REG);
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

	apci3120_ai_reset_fifo(dev);

	/* set scan length (PR) and scan start (PA) */
	devpriv->ctrl = APCI3120_CTRL_PR(n_chan - 1) | APCI3120_CTRL_PA(0);
	outw(devpriv->ctrl, dev->iobase + APCI3120_CTRL_REG);

	/* enable chanlist scanning if necessary */
	if (n_chan > 1)
		devpriv->mode |= APCI3120_MODE_SCAN_ENA;
}

#include "addi-data/hwdrv_apci3120.c"

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

		data[i] = inw(dev->iobase + 0);
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
			devpriv->us_UseDma = 1;
		if (i == 1)
			devpriv->b_DmaDoubleBuffer = 1;
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

static int apci3120_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct apci3120_board *this_board = NULL;
	struct apci3120_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int status;
	int ret;

	if (context < ARRAY_SIZE(apci3120_boardtypes))
		this_board = &apci3120_boardtypes[context];
	if (!this_board)
		return -ENODEV;
	dev->board_ptr = this_board;
	dev->board_name = this_board->name;

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
	s->maxdata	= this_board->ai_is_16bit ? 0xffff : 0x0fff;
	s->range_table	= &apci3120_ai_range;
	s->insn_read	= apci3120_ai_insn_read;
	if (0 /* dev->irq */) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= s->n_chan;
		s->do_cmdtest	= apci3120_ai_cmdtest;
		s->do_cmd	= apci3120_ai_cmd;
		s->cancel	= apci3120_cancel;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	if (this_board->has_ao) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan	= 8;
		s->maxdata	= 0x3fff;
		s->range_table	= &range_bipolar10;
		s->insn_write	= apci3120_ao_insn_write;
		s->insn_read	= comedi_readback_insn_read;

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
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE;
	s->n_chan	= 1;
	s->maxdata	= 0x00ffffff;
	s->insn_write	= apci3120_write_insn_timer;
	s->insn_read	= apci3120_read_insn_timer;
	s->insn_config	= apci3120_config_insn_timer;

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
