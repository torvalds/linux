#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"
#include "addi_watchdog.h"

struct apci1564_private {
	unsigned int amcc_iobase;	/* base of AMCC I/O registers */
	unsigned int mode1;		/* riding-edge/high level channels */
	unsigned int mode2;		/* falling-edge/low level channels */
	unsigned int ctrl;		/* interrupt mode OR (edge) . AND (level) */
	unsigned char timer_select_mode;
	unsigned char mode_select_register;
	struct task_struct *tsk_current;
};

#include "addi-data/hwdrv_apci1564.c"

static int apci1564_reset(struct comedi_device *dev)
{
	struct apci1564_private *devpriv = dev->private;

	/* Disable the input interrupts and reset status register */
	outl(0x0, devpriv->amcc_iobase + APCI1564_DI_IRQ_REG);
	inl(devpriv->amcc_iobase + APCI1564_DI_INT_STATUS_REG);
	outl(0x0, devpriv->amcc_iobase + APCI1564_DI_INT_MODE1_REG);
	outl(0x0, devpriv->amcc_iobase + APCI1564_DI_INT_MODE2_REG);

	/* Reset the output channels and disable interrupts */
	outl(0x0, devpriv->amcc_iobase + APCI1564_DO_REG);
	outl(0x0, devpriv->amcc_iobase + APCI1564_DO_INT_CTRL_REG);

	/* Reset the watchdog registers */
	addi_watchdog_reset(devpriv->amcc_iobase + APCI1564_WDOG_REG);

	/* Reset the timer registers */
	outl(0x0, devpriv->amcc_iobase + APCI1564_TIMER_CTRL_REG);
	outl(0x0, devpriv->amcc_iobase + APCI1564_TIMER_RELOAD_REG);

	/* Reset the counter registers */
	outl(0x0, dev->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER1));
	outl(0x0, dev->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER2));
	outl(0x0, dev->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER3));
	outl(0x0, dev->iobase + APCI1564_TCW_CTRL_REG(APCI1564_COUNTER4));

	return 0;
}

static irqreturn_t apci1564_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci1564_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int status;
	unsigned int ctrl;
	unsigned int chan;

	/* check interrupt is from this device */
	if ((inl(devpriv->amcc_iobase + AMCC_OP_REG_INTCSR) &
	     INTCSR_INTR_ASSERTED) == 0)
		return IRQ_NONE;

	status = inl(devpriv->amcc_iobase + APCI1564_DI_IRQ_REG);
	if (status & APCI1564_DI_INT_ENABLE) {
		/* disable the interrupt */
		outl(status & APCI1564_DI_INT_DISABLE,
		     devpriv->amcc_iobase + APCI1564_DI_IRQ_REG);

		s->state = inl(dev->iobase + APCI1564_DI_INT_STATUS_REG)
			       & 0xffff;
		comedi_buf_put(s, s->state);
		s->async->events |= COMEDI_CB_BLOCK | COMEDI_CB_EOS;
		comedi_event(dev, s);

		/* enable the interrupt */
		outl(status, devpriv->amcc_iobase + APCI1564_DI_IRQ_REG);
	}

	status = inl(devpriv->amcc_iobase + APCI1564_TIMER_IRQ_REG);
	if (status & 0x01) {
		/*  Disable Timer Interrupt */
		ctrl = inl(devpriv->amcc_iobase + APCI1564_TIMER_CTRL_REG);
		outl(0x0, devpriv->amcc_iobase + APCI1564_TIMER_CTRL_REG);

		/* Send a signal to from kernel to user space */
		send_sig(SIGIO, devpriv->tsk_current, 0);

		/*  Enable Timer Interrupt */
		outl(ctrl, devpriv->amcc_iobase + APCI1564_TIMER_CTRL_REG);
	}

	for (chan = 0; chan < 4; chan++) {
		status = inl(dev->iobase + APCI1564_TCW_IRQ_REG(chan));
		if (status & 0x01) {
			/*  Disable Counter Interrupt */
			ctrl = inl(dev->iobase + APCI1564_TCW_CTRL_REG(chan));
			outl(0x0, dev->iobase + APCI1564_TCW_CTRL_REG(chan));

			/* Send a signal to from kernel to user space */
			send_sig(SIGIO, devpriv->tsk_current, 0);

			/*  Enable Counter Interrupt */
			outl(ctrl, dev->iobase + APCI1564_TCW_CTRL_REG(chan));
		}
	}

	return IRQ_HANDLED;
}

static int apci1564_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;

	data[1] = inl(devpriv->amcc_iobase + APCI1564_DI_REG);

	return insn->n;
}

static int apci1564_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;

	s->state = inl(devpriv->amcc_iobase + APCI1564_DO_REG);

	if (comedi_dio_update_state(s, data))
		outl(s->state, devpriv->amcc_iobase + APCI1564_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static int apci1564_diag_insn_bits(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;

	data[1] = inl(devpriv->amcc_iobase + APCI1564_DO_INT_STATUS_REG) & 3;

	return insn->n;
}

/*
 * Change-Of-State (COS) interrupt configuration
 *
 * Channels 0 to 15 are interruptible. These channels can be configured
 * to generate interrupts based on AND/OR logic for the desired channels.
 *
 *	OR logic
 *		- reacts to rising or falling edges
 *		- interrupt is generated when any enabled channel
 *		  meet the desired interrupt condition
 *
 *	AND logic
 *		- reacts to changes in level of the selected inputs
 *		- interrupt is generated when all enabled channels
 *		  meet the desired interrupt condition
 *		- after an interrupt, a change in level must occur on
 *		  the selected inputs to release the IRQ logic
 *
 * The COS interrupt must be configured before it can be enabled.
 *
 *	data[0] : INSN_CONFIG_DIGITAL_TRIG
 *	data[1] : trigger number (= 0)
 *	data[2] : configuration operation:
 *	          COMEDI_DIGITAL_TRIG_DISABLE = no interrupts
 *	          COMEDI_DIGITAL_TRIG_ENABLE_EDGES = OR (edge) interrupts
 *	          COMEDI_DIGITAL_TRIG_ENABLE_LEVELS = AND (level) interrupts
 *	data[3] : left-shift for data[4] and data[5]
 *	data[4] : rising-edge/high level channels
 *	data[5] : falling-edge/low level channels
 */
static int apci1564_cos_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct apci1564_private *devpriv = dev->private;
	unsigned int shift, oldmask;

	switch (data[0]) {
	case INSN_CONFIG_DIGITAL_TRIG:
		if (data[1] != 0)
			return -EINVAL;
		shift = data[3];
		oldmask = (1U << shift) - 1;
		switch (data[2]) {
		case COMEDI_DIGITAL_TRIG_DISABLE:
			devpriv->ctrl = 0;
			devpriv->mode1 = 0;
			devpriv->mode2 = 0;
			outl(0x0, devpriv->amcc_iobase + APCI1564_DI_IRQ_REG);
			inl(devpriv->amcc_iobase + APCI1564_DI_INT_STATUS_REG);
			outl(0x0, devpriv->amcc_iobase + APCI1564_DI_INT_MODE1_REG);
			outl(0x0, devpriv->amcc_iobase + APCI1564_DI_INT_MODE2_REG);
			break;
		case COMEDI_DIGITAL_TRIG_ENABLE_EDGES:
			if (devpriv->ctrl != (APCI1564_DI_INT_ENABLE |
					      APCI1564_DI_INT_OR)) {
				/* switching to 'OR' mode */
				devpriv->ctrl = APCI1564_DI_INT_ENABLE |
						APCI1564_DI_INT_OR;
				/* wipe old channels */
				devpriv->mode1 = 0;
				devpriv->mode2 = 0;
			} else {
				/* preserve unspecified channels */
				devpriv->mode1 &= oldmask;
				devpriv->mode2 &= oldmask;
			}
			/* configure specified channels */
			devpriv->mode1 |= data[4] << shift;
			devpriv->mode2 |= data[5] << shift;
			break;
		case COMEDI_DIGITAL_TRIG_ENABLE_LEVELS:
			if (devpriv->ctrl != (APCI1564_DI_INT_ENABLE |
					      APCI1564_DI_INT_AND)) {
				/* switching to 'AND' mode */
				devpriv->ctrl = APCI1564_DI_INT_ENABLE |
						APCI1564_DI_INT_AND;
				/* wipe old channels */
				devpriv->mode1 = 0;
				devpriv->mode2 = 0;
			} else {
				/* preserve unspecified channels */
				devpriv->mode1 &= oldmask;
				devpriv->mode2 &= oldmask;
			}
			/* configure specified channels */
			devpriv->mode1 |= data[4] << shift;
			devpriv->mode2 |= data[5] << shift;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
	return insn->n;
}

static int apci1564_cos_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	data[1] = s->state;

	return 0;
}

static int apci1564_cos_cmdtest(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);
	err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: ignored */

	if (err)
		return 4;

	return 0;
}

/*
 * Change-Of-State (COS) 'do_cmd' operation
 *
 * Enable the COS interrupt as configured by apci1564_cos_insn_config().
 */
static int apci1564_cos_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct apci1564_private *devpriv = dev->private;

	if (!devpriv->ctrl) {
		dev_warn(dev->class_dev,
			"Interrupts disabled due to mode configuration!\n");
		return -EINVAL;
	}

	outl(devpriv->mode1, devpriv->amcc_iobase + APCI1564_DI_INT_MODE1_REG);
	outl(devpriv->mode2, devpriv->amcc_iobase + APCI1564_DI_INT_MODE2_REG);
	outl(devpriv->ctrl, devpriv->amcc_iobase + APCI1564_DI_IRQ_REG);

	return 0;
}

static int apci1564_cos_cancel(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct apci1564_private *devpriv = dev->private;

	outl(0x0, devpriv->amcc_iobase + APCI1564_DI_IRQ_REG);
	inl(devpriv->amcc_iobase + APCI1564_DI_INT_STATUS_REG);
	outl(0x0, devpriv->amcc_iobase + APCI1564_DI_INT_MODE1_REG);
	outl(0x0, devpriv->amcc_iobase + APCI1564_DI_INT_MODE2_REG);

	return 0;
}

static int apci1564_auto_attach(struct comedi_device *dev,
				      unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct apci1564_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->amcc_iobase = pci_resource_start(pcidev, 0);

	apci1564_reset(dev);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci1564_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 6);
	if (ret)
		return ret;

	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 32;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = apci1564_di_insn_bits;

	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITEABLE;
	s->n_chan = 32;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_config = apci1564_do_config;
	s->insn_bits = apci1564_do_insn_bits;

	/* Change-Of-State (COS) interrupt subdevice */
	s = &dev->subdevices[2];
	if (dev->irq) {
		dev->read_subdev = s;
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE | SDF_CMD_READ;
		s->n_chan = 1;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->len_chanlist = 1;
		s->insn_config = apci1564_cos_insn_config;
		s->insn_bits = apci1564_cos_insn_bits;
		s->do_cmdtest = apci1564_cos_cmdtest;
		s->do_cmd = apci1564_cos_cmd;
		s->cancel = apci1564_cos_cancel;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;
	s->insn_write = apci1564_timer_write;
	s->insn_read = apci1564_timer_read;
	s->insn_config = apci1564_timer_config;

	/* Initialize the watchdog subdevice */
	s = &dev->subdevices[4];
	ret = addi_watchdog_init(s, devpriv->amcc_iobase + APCI1564_WDOG_REG);
	if (ret)
		return ret;

	/* Initialize the diagnostic status subdevice */
	s = &dev->subdevices[5];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 2;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = apci1564_diag_insn_bits;

	return 0;
}

static void apci1564_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		apci1564_reset(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);
	comedi_pci_disable(dev);
}

static struct comedi_driver apci1564_driver = {
	.driver_name	= "addi_apci_1564",
	.module		= THIS_MODULE,
	.auto_attach	= apci1564_auto_attach,
	.detach		= apci1564_detach,
};

static int apci1564_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci1564_driver, id->driver_data);
}

static const struct pci_device_id apci1564_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1006) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1564_pci_table);

static struct pci_driver apci1564_pci_driver = {
	.name		= "addi_apci_1564",
	.id_table	= apci1564_pci_table,
	.probe		= apci1564_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci1564_driver, apci1564_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("ADDI-DATA APCI-1564, 32 channel DI / 32 channel DO boards");
MODULE_LICENSE("GPL");
