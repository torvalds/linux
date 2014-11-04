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

/*
 * PCI BAR 1 register map (dev->iobase)
 */
#define APCI3120_STATUS_TO_VERSION(x)		(((x) >> 4) & 0xf)
#define APCI3120_TIMER_REG			0x04
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

/*
 * PCI BAR 2 register map (devpriv->addon)
 */

/*
 * Board revisions
 */
#define APCI3120_REVA				0xa
#define APCI3120_REVB				0xb
#define APCI3120_REVA_OSC_BASE			70	/* 70ns = 14.29MHz */
#define APCI3120_REVB_OSC_BASE			50	/* 50ns = 20MHz */

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
	unsigned int ui_AiNbrofChannels;
	unsigned int ui_AiChannelList[32];
	unsigned int ui_AiReadData[32];
	unsigned short us_UseDma;
	unsigned char b_DmaDoubleBuffer;
	unsigned int ui_DmaActualBuffer;
	struct apci3120_dmabuf dmabuf[2];
	unsigned char do_bits;
	unsigned char timer_mode;
	unsigned char b_ModeSelectRegister;
	unsigned short us_OutputRegister;
	unsigned char b_Timer2Mode;
	unsigned char b_Timer2Interrupt;
	unsigned int ai_running:1;
	unsigned char b_InterruptMode;
	unsigned char b_EocEosInterrupt;
	unsigned int ui_EocEosConversionTime;
	unsigned char b_ExttrigEnable;
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

#include "addi-data/hwdrv_apci3120.c"

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

	status = inw(dev->iobase + APCI3120_RD_STATUS);
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
	s->range_table	= &range_apci3120_ai;
	s->insn_config	= apci3120_ai_insn_config;
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
