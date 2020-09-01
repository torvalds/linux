// SPDX-License-Identifier: GPL-2.0+
/*
 * s526.c
 * Sensoray s526 Comedi driver
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: s526
 * Description: Sensoray 526 driver
 * Devices: [Sensoray] 526 (s526)
 * Author: Richie
 *	   Everett Wang <everett.wang@everteq.com>
 * Updated: Thu, 14 Sep. 2006
 * Status: experimental
 *
 * Encoder works
 * Analog input works
 * Analog output works
 * PWM output works
 * Commands are not supported yet.
 *
 * Configuration Options:
 *   [0] - I/O port base address
 */

#include <linux/module.h>
#include "../comedidev.h"

/*
 * Register I/O map
 */
#define S526_TIMER_REG		0x00
#define S526_TIMER_LOAD(x)	(((x) & 0xff) << 8)
#define S526_TIMER_MODE		((x) << 1)
#define S526_TIMER_MANUAL	S526_TIMER_MODE(0)
#define S526_TIMER_AUTO		S526_TIMER_MODE(1)
#define S526_TIMER_RESTART	BIT(0)
#define S526_WDOG_REG		0x02
#define S526_WDOG_INVERTED	BIT(4)
#define S526_WDOG_ENA		BIT(3)
#define S526_WDOG_INTERVAL(x)	(((x) & 0x7) << 0)
#define S526_AO_CTRL_REG	0x04
#define S526_AO_CTRL_RESET	BIT(3)
#define S526_AO_CTRL_CHAN(x)	(((x) & 0x3) << 1)
#define S526_AO_CTRL_START	BIT(0)
#define S526_AI_CTRL_REG	0x06
#define S526_AI_CTRL_DELAY	BIT(15)
#define S526_AI_CTRL_CONV(x)	(1 << (5 + ((x) & 0x9)))
#define S526_AI_CTRL_READ(x)	(((x) & 0xf) << 1)
#define S526_AI_CTRL_START	BIT(0)
#define S526_AO_REG		0x08
#define S526_AI_REG		0x08
#define S526_DIO_CTRL_REG	0x0a
#define S526_DIO_CTRL_DIO3_NEG	BIT(15)	/* irq on DIO3 neg/pos edge */
#define S526_DIO_CTRL_DIO2_NEG	BIT(14)	/* irq on DIO2 neg/pos edge */
#define S526_DIO_CTRL_DIO1_NEG	BIT(13)	/* irq on DIO1 neg/pos edge */
#define S526_DIO_CTRL_DIO0_NEG	BIT(12)	/* irq on DIO0 neg/pos edge */
#define S526_DIO_CTRL_GRP2_OUT	BIT(11)
#define S526_DIO_CTRL_GRP1_OUT	BIT(10)
#define S526_DIO_CTRL_GRP2_NEG	BIT(8)	/* irq on DIO[4-7] neg/pos edge */
#define S526_INT_ENA_REG	0x0c
#define S526_INT_STATUS_REG	0x0e
#define S526_INT_DIO(x)		BIT(8 + ((x) & 0x7))
#define S526_INT_EEPROM		BIT(7)	/* status only */
#define S526_INT_CNTR(x)	BIT(3 + (3 - ((x) & 0x3)))
#define S526_INT_AI		BIT(2)
#define S526_INT_AO		BIT(1)
#define S526_INT_TIMER		BIT(0)
#define S526_MISC_REG		0x10
#define S526_MISC_LED_OFF	BIT(0)
#define S526_GPCT_LSB_REG(x)	(0x12 + ((x) * 8))
#define S526_GPCT_MSB_REG(x)	(0x14 + ((x) * 8))
#define S526_GPCT_MODE_REG(x)	(0x16 + ((x) * 8))
#define S526_GPCT_MODE_COUT_SRC(x)	((x) << 0)
#define S526_GPCT_MODE_COUT_SRC_MASK	S526_GPCT_MODE_COUT_SRC(0x1)
#define S526_GPCT_MODE_COUT_SRC_RCAP	S526_GPCT_MODE_COUT_SRC(0)
#define S526_GPCT_MODE_COUT_SRC_RTGL	S526_GPCT_MODE_COUT_SRC(1)
#define S526_GPCT_MODE_COUT_POL(x)	((x) << 1)
#define S526_GPCT_MODE_COUT_POL_MASK	S526_GPCT_MODE_COUT_POL(0x1)
#define S526_GPCT_MODE_COUT_POL_NORM	S526_GPCT_MODE_COUT_POL(0)
#define S526_GPCT_MODE_COUT_POL_INV	S526_GPCT_MODE_COUT_POL(1)
#define S526_GPCT_MODE_AUTOLOAD(x)	((x) << 2)
#define S526_GPCT_MODE_AUTOLOAD_MASK	S526_GPCT_MODE_AUTOLOAD(0x7)
#define S526_GPCT_MODE_AUTOLOAD_NONE	S526_GPCT_MODE_AUTOLOAD(0)
/* these 3 bits can be OR'ed */
#define S526_GPCT_MODE_AUTOLOAD_RO	S526_GPCT_MODE_AUTOLOAD(0x1)
#define S526_GPCT_MODE_AUTOLOAD_IXFALL	S526_GPCT_MODE_AUTOLOAD(0x2)
#define S526_GPCT_MODE_AUTOLOAD_IXRISE	S526_GPCT_MODE_AUTOLOAD(0x4)
#define S526_GPCT_MODE_HWCTEN_SRC(x)	((x) << 5)
#define S526_GPCT_MODE_HWCTEN_SRC_MASK	S526_GPCT_MODE_HWCTEN_SRC(0x3)
#define S526_GPCT_MODE_HWCTEN_SRC_CEN	S526_GPCT_MODE_HWCTEN_SRC(0)
#define S526_GPCT_MODE_HWCTEN_SRC_IX	S526_GPCT_MODE_HWCTEN_SRC(1)
#define S526_GPCT_MODE_HWCTEN_SRC_IXRF	S526_GPCT_MODE_HWCTEN_SRC(2)
#define S526_GPCT_MODE_HWCTEN_SRC_NRCAP	S526_GPCT_MODE_HWCTEN_SRC(3)
#define S526_GPCT_MODE_CTEN_CTRL(x)	((x) << 7)
#define S526_GPCT_MODE_CTEN_CTRL_MASK	S526_GPCT_MODE_CTEN_CTRL(0x3)
#define S526_GPCT_MODE_CTEN_CTRL_DIS	S526_GPCT_MODE_CTEN_CTRL(0)
#define S526_GPCT_MODE_CTEN_CTRL_ENA	S526_GPCT_MODE_CTEN_CTRL(1)
#define S526_GPCT_MODE_CTEN_CTRL_HW	S526_GPCT_MODE_CTEN_CTRL(2)
#define S526_GPCT_MODE_CTEN_CTRL_INVHW	S526_GPCT_MODE_CTEN_CTRL(3)
#define S526_GPCT_MODE_CLK_SRC(x)	((x) << 9)
#define S526_GPCT_MODE_CLK_SRC_MASK	S526_GPCT_MODE_CLK_SRC(0x3)
/* if count direction control set to quadrature */
#define S526_GPCT_MODE_CLK_SRC_QUADX1	S526_GPCT_MODE_CLK_SRC(0)
#define S526_GPCT_MODE_CLK_SRC_QUADX2	S526_GPCT_MODE_CLK_SRC(1)
#define S526_GPCT_MODE_CLK_SRC_QUADX4	S526_GPCT_MODE_CLK_SRC(2)
#define S526_GPCT_MODE_CLK_SRC_QUADX4_	S526_GPCT_MODE_CLK_SRC(3)
/* if count direction control set to software control */
#define S526_GPCT_MODE_CLK_SRC_ARISE	S526_GPCT_MODE_CLK_SRC(0)
#define S526_GPCT_MODE_CLK_SRC_AFALL	S526_GPCT_MODE_CLK_SRC(1)
#define S526_GPCT_MODE_CLK_SRC_INT	S526_GPCT_MODE_CLK_SRC(2)
#define S526_GPCT_MODE_CLK_SRC_INTHALF	S526_GPCT_MODE_CLK_SRC(3)
#define S526_GPCT_MODE_CT_DIR(x)	((x) << 11)
#define S526_GPCT_MODE_CT_DIR_MASK	S526_GPCT_MODE_CT_DIR(0x1)
/* if count direction control set to software control */
#define S526_GPCT_MODE_CT_DIR_UP	S526_GPCT_MODE_CT_DIR(0)
#define S526_GPCT_MODE_CT_DIR_DOWN	S526_GPCT_MODE_CT_DIR(1)
#define S526_GPCT_MODE_CTDIR_CTRL(x)	((x) << 12)
#define S526_GPCT_MODE_CTDIR_CTRL_MASK	S526_GPCT_MODE_CTDIR_CTRL(0x1)
#define S526_GPCT_MODE_CTDIR_CTRL_QUAD	S526_GPCT_MODE_CTDIR_CTRL(0)
#define S526_GPCT_MODE_CTDIR_CTRL_SOFT	S526_GPCT_MODE_CTDIR_CTRL(1)
#define S526_GPCT_MODE_LATCH_CTRL(x)	((x) << 13)
#define S526_GPCT_MODE_LATCH_CTRL_MASK	S526_GPCT_MODE_LATCH_CTRL(0x1)
#define S526_GPCT_MODE_LATCH_CTRL_READ	S526_GPCT_MODE_LATCH_CTRL(0)
#define S526_GPCT_MODE_LATCH_CTRL_EVENT	S526_GPCT_MODE_LATCH_CTRL(1)
#define S526_GPCT_MODE_PR_SELECT(x)	((x) << 14)
#define S526_GPCT_MODE_PR_SELECT_MASK	S526_GPCT_MODE_PR_SELECT(0x1)
#define S526_GPCT_MODE_PR_SELECT_PR0	S526_GPCT_MODE_PR_SELECT(0)
#define S526_GPCT_MODE_PR_SELECT_PR1	S526_GPCT_MODE_PR_SELECT(1)
/* Control/Status - R = readable, W = writeable, C = write 1 to clear */
#define S526_GPCT_CTRL_REG(x)	(0x18 + ((x) * 8))
#define S526_GPCT_CTRL_EV_STATUS(x)	((x) << 0)		/* RC */
#define S526_GPCT_CTRL_EV_STATUS_MASK	S526_GPCT_EV_STATUS(0xf)
#define S526_GPCT_CTRL_EV_STATUS_NONE	S526_GPCT_EV_STATUS(0)
/* these 4 bits can be OR'ed */
#define S526_GPCT_CTRL_EV_STATUS_ECAP	S526_GPCT_EV_STATUS(0x1)
#define S526_GPCT_CTRL_EV_STATUS_ICAPN	S526_GPCT_EV_STATUS(0x2)
#define S526_GPCT_CTRL_EV_STATUS_ICAPP	S526_GPCT_EV_STATUS(0x4)
#define S526_GPCT_CTRL_EV_STATUS_RCAP	S526_GPCT_EV_STATUS(0x8)
#define S526_GPCT_CTRL_COUT_STATUS	BIT(4)			/* R */
#define S526_GPCT_CTRL_INDEX_STATUS	BIT(5)			/* R */
#define S525_GPCT_CTRL_INTEN(x)		((x) << 6)		/* W */
#define S525_GPCT_CTRL_INTEN_MASK	S526_GPCT_CTRL_INTEN(0xf)
#define S525_GPCT_CTRL_INTEN_NONE	S526_GPCT_CTRL_INTEN(0)
/* these 4 bits can be OR'ed */
#define S525_GPCT_CTRL_INTEN_ERROR	S526_GPCT_CTRL_INTEN(0x1)
#define S525_GPCT_CTRL_INTEN_IXFALL	S526_GPCT_CTRL_INTEN(0x2)
#define S525_GPCT_CTRL_INTEN_IXRISE	S526_GPCT_CTRL_INTEN(0x4)
#define S525_GPCT_CTRL_INTEN_RO		S526_GPCT_CTRL_INTEN(0x8)
#define S525_GPCT_CTRL_LATCH_SEL(x)	((x) << 10)		/* W */
#define S525_GPCT_CTRL_LATCH_SEL_MASK	S526_GPCT_CTRL_LATCH_SEL(0x7)
#define S525_GPCT_CTRL_LATCH_SEL_NONE	S526_GPCT_CTRL_LATCH_SEL(0)
/* these 3 bits can be OR'ed */
#define S525_GPCT_CTRL_LATCH_SEL_IXFALL	S526_GPCT_CTRL_LATCH_SEL(0x1)
#define S525_GPCT_CTRL_LATCH_SEL_IXRISE	S526_GPCT_CTRL_LATCH_SEL(0x2)
#define S525_GPCT_CTRL_LATCH_SEL_ITIMER	S526_GPCT_CTRL_LATCH_SEL(0x4)
#define S525_GPCT_CTRL_CT_ARM		BIT(13)			/* W */
#define S525_GPCT_CTRL_CT_LOAD		BIT(14)			/* W */
#define S526_GPCT_CTRL_CT_RESET		BIT(15)			/* W */
#define S526_EEPROM_DATA_REG	0x32
#define S526_EEPROM_CTRL_REG	0x34
#define S526_EEPROM_CTRL_ADDR(x) (((x) & 0x3f) << 3)
#define S526_EEPROM_CTRL(x)	(((x) & 0x3) << 1)
#define S526_EEPROM_CTRL_READ	S526_EEPROM_CTRL(2)
#define S526_EEPROM_CTRL_START	BIT(0)

struct s526_private {
	unsigned int gpct_config[4];
	unsigned short ai_ctrl;
};

static void s526_gpct_write(struct comedi_device *dev,
			    unsigned int chan, unsigned int val)
{
	/* write high word then low word */
	outw((val >> 16) & 0xffff, dev->iobase + S526_GPCT_MSB_REG(chan));
	outw(val & 0xffff, dev->iobase + S526_GPCT_LSB_REG(chan));
}

static unsigned int s526_gpct_read(struct comedi_device *dev,
				   unsigned int chan)
{
	unsigned int val;

	/* read the low word then high word */
	val = inw(dev->iobase + S526_GPCT_LSB_REG(chan)) & 0xffff;
	val |= (inw(dev->iobase + S526_GPCT_MSB_REG(chan)) & 0xff) << 16;

	return val;
}

static int s526_gpct_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = s526_gpct_read(dev, chan);

	return insn->n;
}

static int s526_gpct_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct s526_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val;

	/*
	 * Check what type of Counter the user requested
	 * data[0] contains the Application type
	 */
	switch (data[0]) {
	case INSN_CONFIG_GPCT_QUADRATURE_ENCODER:
		/*
		 * data[0]: Application Type
		 * data[1]: Counter Mode Register Value
		 * data[2]: Pre-load Register Value
		 * data[3]: Conter Control Register
		 */
		devpriv->gpct_config[chan] = data[0];

#if 1
		/*  Set Counter Mode Register */
		val = data[1] & 0xffff;
		outw(val, dev->iobase + S526_GPCT_MODE_REG(chan));

		/*  Reset the counter if it is software preload */
		if ((val & S526_GPCT_MODE_AUTOLOAD_MASK) ==
		    S526_GPCT_MODE_AUTOLOAD_NONE) {
			/*  Reset the counter */
			outw(S526_GPCT_CTRL_CT_RESET,
			     dev->iobase + S526_GPCT_CTRL_REG(chan));
			/*
			 * Load the counter from PR0
			 * outw(S526_GPCT_CTRL_CT_LOAD,
			 *      dev->iobase + S526_GPCT_CTRL_REG(chan));
			 */
		}
#else
		val = S526_GPCT_MODE_CTDIR_CTRL_QUAD;

		/*  data[1] contains GPCT_X1, GPCT_X2 or GPCT_X4 */
		if (data[1] == GPCT_X2)
			val |= S526_GPCT_MODE_CLK_SRC_QUADX2;
		else if (data[1] == GPCT_X4)
			val |= S526_GPCT_MODE_CLK_SRC_QUADX4;
		else
			val |= S526_GPCT_MODE_CLK_SRC_QUADX1;

		/*  When to take into account the indexpulse: */
		/*
		 * if (data[2] == GPCT_IndexPhaseLowLow) {
		 * } else if (data[2] == GPCT_IndexPhaseLowHigh) {
		 * } else if (data[2] == GPCT_IndexPhaseHighLow) {
		 * } else if (data[2] == GPCT_IndexPhaseHighHigh) {
		 * }
		 */
		/*  Take into account the index pulse? */
		if (data[3] == GPCT_RESET_COUNTER_ON_INDEX) {
			/*  Auto load with INDEX^ */
			val |= S526_GPCT_MODE_AUTOLOAD_IXRISE;
		}

		/*  Set Counter Mode Register */
		val = data[1] & 0xffff;
		outw(val, dev->iobase + S526_GPCT_MODE_REG(chan));

		/*  Load the pre-load register */
		s526_gpct_write(dev, chan, data[2]);

		/*  Write the Counter Control Register */
		if (data[3])
			outw(data[3] & 0xffff,
			     dev->iobase + S526_GPCT_CTRL_REG(chan));

		/*  Reset the counter if it is software preload */
		if ((val & S526_GPCT_MODE_AUTOLOAD_MASK) ==
		    S526_GPCT_MODE_AUTOLOAD_NONE) {
			/*  Reset the counter */
			outw(S526_GPCT_CTRL_CT_RESET,
			     dev->iobase + S526_GPCT_CTRL_REG(chan));
			/*  Load the counter from PR0 */
			outw(S526_GPCT_CTRL_CT_LOAD,
			     dev->iobase + S526_GPCT_CTRL_REG(chan));
		}
#endif
		break;

	case INSN_CONFIG_GPCT_SINGLE_PULSE_GENERATOR:
		/*
		 * data[0]: Application Type
		 * data[1]: Counter Mode Register Value
		 * data[2]: Pre-load Register 0 Value
		 * data[3]: Pre-load Register 1 Value
		 * data[4]: Conter Control Register
		 */
		devpriv->gpct_config[chan] = data[0];

		/*  Set Counter Mode Register */
		val = data[1] & 0xffff;
		/* Select PR0 */
		val &= ~S526_GPCT_MODE_PR_SELECT_MASK;
		val |= S526_GPCT_MODE_PR_SELECT_PR0;
		outw(val, dev->iobase + S526_GPCT_MODE_REG(chan));

		/* Load the pre-load register 0 */
		s526_gpct_write(dev, chan, data[2]);

		/*  Set Counter Mode Register */
		val = data[1] & 0xffff;
		/* Select PR1 */
		val &= ~S526_GPCT_MODE_PR_SELECT_MASK;
		val |= S526_GPCT_MODE_PR_SELECT_PR1;
		outw(val, dev->iobase + S526_GPCT_MODE_REG(chan));

		/* Load the pre-load register 1 */
		s526_gpct_write(dev, chan, data[3]);

		/*  Write the Counter Control Register */
		if (data[4]) {
			val = data[4] & 0xffff;
			outw(val, dev->iobase + S526_GPCT_CTRL_REG(chan));
		}
		break;

	case INSN_CONFIG_GPCT_PULSE_TRAIN_GENERATOR:
		/*
		 * data[0]: Application Type
		 * data[1]: Counter Mode Register Value
		 * data[2]: Pre-load Register 0 Value
		 * data[3]: Pre-load Register 1 Value
		 * data[4]: Conter Control Register
		 */
		devpriv->gpct_config[chan] = data[0];

		/*  Set Counter Mode Register */
		val = data[1] & 0xffff;
		/* Select PR0 */
		val &= ~S526_GPCT_MODE_PR_SELECT_MASK;
		val |= S526_GPCT_MODE_PR_SELECT_PR0;
		outw(val, dev->iobase + S526_GPCT_MODE_REG(chan));

		/* Load the pre-load register 0 */
		s526_gpct_write(dev, chan, data[2]);

		/*  Set Counter Mode Register */
		val = data[1] & 0xffff;
		/* Select PR1 */
		val &= ~S526_GPCT_MODE_PR_SELECT_MASK;
		val |= S526_GPCT_MODE_PR_SELECT_PR1;
		outw(val, dev->iobase + S526_GPCT_MODE_REG(chan));

		/* Load the pre-load register 1 */
		s526_gpct_write(dev, chan, data[3]);

		/*  Write the Counter Control Register */
		if (data[4]) {
			val = data[4] & 0xffff;
			outw(val, dev->iobase + S526_GPCT_CTRL_REG(chan));
		}
		break;

	default:
		return -EINVAL;
	}

	return insn->n;
}

static int s526_gpct_winsn(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned int *data)
{
	struct s526_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);

	inw(dev->iobase + S526_GPCT_MODE_REG(chan));	/* Is this required? */

	/*  Check what Application of Counter this channel is configured for */
	switch (devpriv->gpct_config[chan]) {
	case INSN_CONFIG_GPCT_PULSE_TRAIN_GENERATOR:
		/*
		 * data[0] contains the PULSE_WIDTH
		 * data[1] contains the PULSE_PERIOD
		 * @pre PULSE_PERIOD > PULSE_WIDTH > 0
		 * The above periods must be expressed as a multiple of the
		 * pulse frequency on the selected source
		 */
		if ((data[1] <= data[0]) || !data[0])
			return -EINVAL;
		/* to write the PULSE_WIDTH */
		fallthrough;
	case INSN_CONFIG_GPCT_QUADRATURE_ENCODER:
	case INSN_CONFIG_GPCT_SINGLE_PULSE_GENERATOR:
		s526_gpct_write(dev, chan, data[0]);
		break;

	default:
		return -EINVAL;
	}

	return insn->n;
}

static int s526_eoc(struct comedi_device *dev,
		    struct comedi_subdevice *s,
		    struct comedi_insn *insn,
		    unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + S526_INT_STATUS_REG);
	if (status & context) {
		/* we got our eoc event, clear it */
		outw(context, dev->iobase + S526_INT_STATUS_REG);
		return 0;
	}
	return -EBUSY;
}

static int s526_ai_insn_read(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	struct s526_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int ctrl;
	unsigned int val;
	int ret;
	int i;

	ctrl = S526_AI_CTRL_CONV(chan) | S526_AI_CTRL_READ(chan) |
	       S526_AI_CTRL_START;
	if (ctrl != devpriv->ai_ctrl) {
		/*
		 * The multiplexor needs to change, enable the 15us
		 * delay for the first sample.
		 */
		devpriv->ai_ctrl = ctrl;
		ctrl |= S526_AI_CTRL_DELAY;
	}

	for (i = 0; i < insn->n; i++) {
		/* trigger conversion */
		outw(ctrl, dev->iobase + S526_AI_CTRL_REG);
		ctrl &= ~S526_AI_CTRL_DELAY;

		/* wait for conversion to end */
		ret = comedi_timeout(dev, s, insn, s526_eoc, S526_INT_AI);
		if (ret)
			return ret;

		val = inw(dev->iobase + S526_AI_REG);
		data[i] = comedi_offset_munge(s, val);
	}

	return insn->n;
}

static int s526_ao_insn_write(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int ctrl = S526_AO_CTRL_CHAN(chan);
	unsigned int val = s->readback[chan];
	int ret;
	int i;

	outw(ctrl, dev->iobase + S526_AO_CTRL_REG);
	ctrl |= S526_AO_CTRL_START;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outw(val, dev->iobase + S526_AO_REG);
		outw(ctrl, dev->iobase + S526_AO_CTRL_REG);

		/* wait for conversion to end */
		ret = comedi_timeout(dev, s, insn, s526_eoc, S526_INT_AO);
		if (ret)
			return ret;
	}
	s->readback[chan] = val;

	return insn->n;
}

static int s526_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + S526_DIO_CTRL_REG);

	data[1] = inw(dev->iobase + S526_DIO_CTRL_REG) & 0xff;

	return insn->n;
}

static int s526_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	/*
	 * Digital I/O can be configured as inputs or outputs in
	 * groups of 4; DIO group 1 (DIO0-3) and DIO group 2 (DIO4-7).
	 */
	if (chan < 4)
		mask = 0x0f;
	else
		mask = 0xf0;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	if (s->io_bits & 0x0f)
		s->state |= S526_DIO_CTRL_GRP1_OUT;
	else
		s->state &= ~S526_DIO_CTRL_GRP1_OUT;
	if (s->io_bits & 0xf0)
		s->state |= S526_DIO_CTRL_GRP2_OUT;
	else
		s->state &= ~S526_DIO_CTRL_GRP2_OUT;

	outw(s->state, dev->iobase + S526_DIO_CTRL_REG);

	return insn->n;
}

static int s526_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct s526_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x40);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* General-Purpose Counter/Timer (GPCT) */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_LSAMPL;
	s->n_chan	= 4;
	s->maxdata	= 0x00ffffff;
	s->insn_read	= s526_gpct_rinsn;
	s->insn_config	= s526_gpct_insn_config;
	s->insn_write	= s526_gpct_winsn;

	/*
	 * Analog Input subdevice
	 * channels 0 to 7 are the regular differential inputs
	 * channel 8 is "reference 0" (+10V)
	 * channel 9 is "reference 1" (0V)
	 */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_DIFF;
	s->n_chan	= 10;
	s->maxdata	= 0xffff;
	s->range_table	= &range_bipolar10;
	s->len_chanlist	= 16;
	s->insn_read	= s526_ai_insn_read;

	/* Analog Output subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 0xffff;
	s->range_table	= &range_bipolar10;
	s->insn_write	= s526_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital I/O subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= s526_dio_insn_bits;
	s->insn_config	= s526_dio_insn_config;

	return 0;
}

static struct comedi_driver s526_driver = {
	.driver_name	= "s526",
	.module		= THIS_MODULE,
	.attach		= s526_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(s526_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
