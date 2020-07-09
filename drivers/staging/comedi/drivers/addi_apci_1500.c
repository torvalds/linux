// SPDX-License-Identifier: GPL-2.0+
/*
 * addi_apci_1500.c
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
#include "z8536.h"

/*
 * PCI Bar 0 Register map (devpriv->amcc)
 * see amcc_s5933.h for register and bit defines
 */

/*
 * PCI Bar 1 Register map (dev->iobase)
 * see z8536.h for Z8536 internal registers and bit defines
 */
#define APCI1500_Z8536_PORTC_REG	0x00
#define APCI1500_Z8536_PORTB_REG	0x01
#define APCI1500_Z8536_PORTA_REG	0x02
#define APCI1500_Z8536_CTRL_REG		0x03

/*
 * PCI Bar 2 Register map (devpriv->addon)
 */
#define APCI1500_CLK_SEL_REG		0x00
#define APCI1500_DI_REG			0x00
#define APCI1500_DO_REG			0x02

struct apci1500_private {
	unsigned long amcc;
	unsigned long addon;

	unsigned int clk_src;

	/* Digital trigger configuration [0]=AND [1]=OR */
	unsigned int pm[2];	/* Pattern Mask */
	unsigned int pt[2];	/* Pattern Transition */
	unsigned int pp[2];	/* Pattern Polarity */
};

static unsigned int z8536_read(struct comedi_device *dev, unsigned int reg)
{
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&dev->spinlock, flags);
	outb(reg, dev->iobase + APCI1500_Z8536_CTRL_REG);
	val = inb(dev->iobase + APCI1500_Z8536_CTRL_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return val;
}

static void z8536_write(struct comedi_device *dev,
			unsigned int val, unsigned int reg)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);
	outb(reg, dev->iobase + APCI1500_Z8536_CTRL_REG);
	outb(val, dev->iobase + APCI1500_Z8536_CTRL_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);
}

static void z8536_reset(struct comedi_device *dev)
{
	unsigned long flags;

	/*
	 * Even if the state of the Z8536 is not known, the following
	 * sequence will reset it and put it in State 0.
	 */
	spin_lock_irqsave(&dev->spinlock, flags);
	inb(dev->iobase + APCI1500_Z8536_CTRL_REG);
	outb(0, dev->iobase + APCI1500_Z8536_CTRL_REG);
	inb(dev->iobase + APCI1500_Z8536_CTRL_REG);
	outb(0, dev->iobase + APCI1500_Z8536_CTRL_REG);
	outb(1, dev->iobase + APCI1500_Z8536_CTRL_REG);
	outb(0, dev->iobase + APCI1500_Z8536_CTRL_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/* Disable all Ports and Counter/Timers */
	z8536_write(dev, 0x00, Z8536_CFG_CTRL_REG);

	/*
	 * Port A is connected to Ditial Input channels 0-7.
	 * Configure the port to allow interrupt detection.
	 */
	z8536_write(dev, Z8536_PAB_MODE_PTS_BIT |
			 Z8536_PAB_MODE_SB |
			 Z8536_PAB_MODE_PMS_DISABLE,
		    Z8536_PA_MODE_REG);
	z8536_write(dev, 0xff, Z8536_PB_DPP_REG);
	z8536_write(dev, 0xff, Z8536_PA_DD_REG);

	/*
	 * Port B is connected to Ditial Input channels 8-13.
	 * Configure the port to allow interrupt detection.
	 *
	 * NOTE: Bits 7 and 6 of Port B are connected to internal
	 * diagnostic signals and bit 7 is inverted.
	 */
	z8536_write(dev, Z8536_PAB_MODE_PTS_BIT |
			 Z8536_PAB_MODE_SB |
			 Z8536_PAB_MODE_PMS_DISABLE,
		    Z8536_PB_MODE_REG);
	z8536_write(dev, 0x7f, Z8536_PB_DPP_REG);
	z8536_write(dev, 0xff, Z8536_PB_DD_REG);

	/*
	 * Not sure what Port C is connected to...
	 */
	z8536_write(dev, 0x09, Z8536_PC_DPP_REG);
	z8536_write(dev, 0x0e, Z8536_PC_DD_REG);

	/*
	 * Clear and disable all interrupt sources.
	 *
	 * Just in case, the reset of the Z8536 should have already
	 * done this.
	 */
	z8536_write(dev, Z8536_CMD_CLR_IP_IUS, Z8536_PA_CMDSTAT_REG);
	z8536_write(dev, Z8536_CMD_CLR_IE, Z8536_PA_CMDSTAT_REG);

	z8536_write(dev, Z8536_CMD_CLR_IP_IUS, Z8536_PB_CMDSTAT_REG);
	z8536_write(dev, Z8536_CMD_CLR_IE, Z8536_PB_CMDSTAT_REG);

	z8536_write(dev, Z8536_CMD_CLR_IP_IUS, Z8536_CT_CMDSTAT_REG(0));
	z8536_write(dev, Z8536_CMD_CLR_IE, Z8536_CT_CMDSTAT_REG(0));

	z8536_write(dev, Z8536_CMD_CLR_IP_IUS, Z8536_CT_CMDSTAT_REG(1));
	z8536_write(dev, Z8536_CMD_CLR_IE, Z8536_CT_CMDSTAT_REG(1));

	z8536_write(dev, Z8536_CMD_CLR_IP_IUS, Z8536_CT_CMDSTAT_REG(2));
	z8536_write(dev, Z8536_CMD_CLR_IE, Z8536_CT_CMDSTAT_REG(2));

	/* Disable all interrupts */
	z8536_write(dev, 0x00, Z8536_INT_CTRL_REG);
}

static void apci1500_port_enable(struct comedi_device *dev, bool enable)
{
	unsigned int cfg;

	cfg = z8536_read(dev, Z8536_CFG_CTRL_REG);
	if (enable)
		cfg |= (Z8536_CFG_CTRL_PAE | Z8536_CFG_CTRL_PBE);
	else
		cfg &= ~(Z8536_CFG_CTRL_PAE | Z8536_CFG_CTRL_PBE);
	z8536_write(dev, cfg, Z8536_CFG_CTRL_REG);
}

static void apci1500_timer_enable(struct comedi_device *dev,
				  unsigned int chan, bool enable)
{
	unsigned int bit;
	unsigned int cfg;

	if (chan == 0)
		bit = Z8536_CFG_CTRL_CT1E;
	else if (chan == 1)
		bit = Z8536_CFG_CTRL_CT2E;
	else
		bit = Z8536_CFG_CTRL_PCE_CT3E;

	cfg = z8536_read(dev, Z8536_CFG_CTRL_REG);
	if (enable) {
		cfg |= bit;
	} else {
		cfg &= ~bit;
		z8536_write(dev, 0x00, Z8536_CT_CMDSTAT_REG(chan));
	}
	z8536_write(dev, cfg, Z8536_CFG_CTRL_REG);
}

static bool apci1500_ack_irq(struct comedi_device *dev,
			     unsigned int reg)
{
	unsigned int val;

	val = z8536_read(dev, reg);
	if ((val & Z8536_STAT_IE_IP) == Z8536_STAT_IE_IP) {
		val &= 0x0f;			/* preserve any write bits */
		val |= Z8536_CMD_CLR_IP_IUS;
		z8536_write(dev, val, reg);

		return true;
	}
	return false;
}

static irqreturn_t apci1500_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci1500_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int status = 0;
	unsigned int val;

	val = inl(devpriv->amcc + AMCC_OP_REG_INTCSR);
	if (!(val & INTCSR_INTR_ASSERTED))
		return IRQ_NONE;

	if (apci1500_ack_irq(dev, Z8536_PA_CMDSTAT_REG))
		status |= 0x01;	/* port a event (inputs 0-7) */

	if (apci1500_ack_irq(dev, Z8536_PB_CMDSTAT_REG)) {
		/* Tests if this is an external error */
		val = inb(dev->iobase + APCI1500_Z8536_PORTB_REG);
		val &= 0xc0;
		if (val) {
			if (val & 0x80)	/* voltage error */
				status |= 0x40;
			if (val & 0x40)	/* short circuit error */
				status |= 0x80;
		} else {
			status |= 0x02;	/* port b event (inputs 8-13) */
		}
	}

	/*
	 * NOTE: The 'status' returned by the sample matches the
	 * interrupt mask information from the APCI-1500 Users Manual.
	 *
	 *    Mask     Meaning
	 * ----------  ------------------------------------------
	 * 0x00000001  Event 1 has occurred
	 * 0x00000010  Event 2 has occurred
	 * 0x00000100  Counter/timer 1 has run down (not implemented)
	 * 0x00001000  Counter/timer 2 has run down (not implemented)
	 * 0x00010000  Counter 3 has run down (not implemented)
	 * 0x00100000  Watchdog has run down (not implemented)
	 * 0x01000000  Voltage error
	 * 0x10000000  Short-circuit error
	 */
	comedi_buf_write_samples(s, &status, 1);
	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static int apci1500_di_cancel(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	/* Disables the main interrupt on the board */
	z8536_write(dev, 0x00, Z8536_INT_CTRL_REG);

	/* Disable Ports A & B */
	apci1500_port_enable(dev, false);

	/* Ack any pending interrupts */
	apci1500_ack_irq(dev, Z8536_PA_CMDSTAT_REG);
	apci1500_ack_irq(dev, Z8536_PB_CMDSTAT_REG);

	/* Disable pattern interrupts */
	z8536_write(dev, Z8536_CMD_CLR_IE, Z8536_PA_CMDSTAT_REG);
	z8536_write(dev, Z8536_CMD_CLR_IE, Z8536_PB_CMDSTAT_REG);

	/* Enable Ports A & B */
	apci1500_port_enable(dev, true);

	return 0;
}

static int apci1500_di_inttrig_start(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     unsigned int trig_num)
{
	struct apci1500_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int pa_mode = Z8536_PAB_MODE_PMS_DISABLE;
	unsigned int pb_mode = Z8536_PAB_MODE_PMS_DISABLE;
	unsigned int pa_trig = trig_num & 0x01;
	unsigned int pb_trig = (trig_num >> 1) & 0x01;
	bool valid_trig = false;
	unsigned int val;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	/* Disable Ports A & B */
	apci1500_port_enable(dev, false);

	/* Set Port A for selected trigger pattern */
	z8536_write(dev, devpriv->pm[pa_trig] & 0xff, Z8536_PA_PM_REG);
	z8536_write(dev, devpriv->pt[pa_trig] & 0xff, Z8536_PA_PT_REG);
	z8536_write(dev, devpriv->pp[pa_trig] & 0xff, Z8536_PA_PP_REG);

	/* Set Port B for selected trigger pattern */
	z8536_write(dev, (devpriv->pm[pb_trig] >> 8) & 0xff, Z8536_PB_PM_REG);
	z8536_write(dev, (devpriv->pt[pb_trig] >> 8) & 0xff, Z8536_PB_PT_REG);
	z8536_write(dev, (devpriv->pp[pb_trig] >> 8) & 0xff, Z8536_PB_PP_REG);

	/* Set Port A trigger mode (if enabled) and enable interrupt */
	if (devpriv->pm[pa_trig] & 0xff) {
		pa_mode = pa_trig ? Z8536_PAB_MODE_PMS_AND
				  : Z8536_PAB_MODE_PMS_OR;

		val = z8536_read(dev, Z8536_PA_MODE_REG);
		val &= ~Z8536_PAB_MODE_PMS_MASK;
		val |= (pa_mode | Z8536_PAB_MODE_IMO);
		z8536_write(dev, val, Z8536_PA_MODE_REG);

		z8536_write(dev, Z8536_CMD_SET_IE, Z8536_PA_CMDSTAT_REG);

		valid_trig = true;

		dev_dbg(dev->class_dev,
			"Port A configured for %s mode pattern detection\n",
			pa_trig ? "AND" : "OR");
	}

	/* Set Port B trigger mode (if enabled) and enable interrupt */
	if (devpriv->pm[pb_trig] & 0xff00) {
		pb_mode = pb_trig ? Z8536_PAB_MODE_PMS_AND
				  : Z8536_PAB_MODE_PMS_OR;

		val = z8536_read(dev, Z8536_PB_MODE_REG);
		val &= ~Z8536_PAB_MODE_PMS_MASK;
		val |= (pb_mode | Z8536_PAB_MODE_IMO);
		z8536_write(dev, val, Z8536_PB_MODE_REG);

		z8536_write(dev, Z8536_CMD_SET_IE, Z8536_PB_CMDSTAT_REG);

		valid_trig = true;

		dev_dbg(dev->class_dev,
			"Port B configured for %s mode pattern detection\n",
			pb_trig ? "AND" : "OR");
	}

	/* Enable Ports A & B */
	apci1500_port_enable(dev, true);

	if (!valid_trig) {
		dev_dbg(dev->class_dev,
			"digital trigger %d is not configured\n", trig_num);
		return -EINVAL;
	}

	/* Authorizes the main interrupt on the board */
	z8536_write(dev, Z8536_INT_CTRL_MIE | Z8536_INT_CTRL_DLC,
		    Z8536_INT_CTRL_REG);

	return 0;
}

static int apci1500_di_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	s->async->inttrig = apci1500_di_inttrig_start;

	return 0;
}

static int apci1500_di_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_INT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	/* Step 3: check if arguments are trivially valid */

	/*
	 * Internal start source triggers:
	 *
	 *   0	AND mode for Port A (digital inputs 0-7)
	 *	AND mode for Port B (digital inputs 8-13 and internal signals)
	 *
	 *   1	OR mode for Port A (digital inputs 0-7)
	 *	AND mode for Port B (digital inputs 8-13 and internal signals)
	 *
	 *   2	AND mode for Port A (digital inputs 0-7)
	 *	OR mode for Port B (digital inputs 8-13 and internal signals)
	 *
	 *   3	OR mode for Port A (digital inputs 0-7)
	 *	OR mode for Port B (digital inputs 8-13 and internal signals)
	 */
	err |= comedi_check_trigger_arg_max(&cmd->start_arg, 3);

	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);
	err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

	/* Step 5: check channel list if it exists */

	return 0;
}

/*
 * The pattern-recognition logic must be configured before the digital
 * input async command is started.
 *
 * Digital input channels 0 to 13 can generate interrupts. Channels 14
 * and 15 are connected to internal board status/diagnostic signals.
 *
 * Channel 14 - Voltage error (the external supply is < 5V)
 * Channel 15 - Short-circuit/overtemperature error
 *
 *	data[0] : INSN_CONFIG_DIGITAL_TRIG
 *	data[1] : trigger number
 *		  0 = AND mode
 *		  1 = OR mode
 *	data[2] : configuration operation:
 *	          COMEDI_DIGITAL_TRIG_DISABLE = no interrupts
 *	          COMEDI_DIGITAL_TRIG_ENABLE_EDGES = edge interrupts
 *	          COMEDI_DIGITAL_TRIG_ENABLE_LEVELS = level interrupts
 *	data[3] : left-shift for data[4] and data[5]
 *	data[4] : rising-edge/high level channels
 *	data[5] : falling-edge/low level channels
 */
static int apci1500_di_cfg_trig(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct apci1500_private *devpriv = dev->private;
	unsigned int trig = data[1];
	unsigned int shift = data[3];
	unsigned int hi_mask = data[4] << shift;
	unsigned int lo_mask = data[5] << shift;
	unsigned int chan_mask = hi_mask | lo_mask;
	unsigned int old_mask = (1 << shift) - 1;
	unsigned int pm;
	unsigned int pt;
	unsigned int pp;

	if (trig > 1) {
		dev_dbg(dev->class_dev,
			"invalid digital trigger number (0=AND, 1=OR)\n");
		return -EINVAL;
	}

	if (chan_mask > 0xffff) {
		dev_dbg(dev->class_dev, "invalid digital trigger channel\n");
		return -EINVAL;
	}

	pm = devpriv->pm[trig] & old_mask;
	pt = devpriv->pt[trig] & old_mask;
	pp = devpriv->pp[trig] & old_mask;

	switch (data[2]) {
	case COMEDI_DIGITAL_TRIG_DISABLE:
		/* clear trigger configuration */
		pm = 0;
		pt = 0;
		pp = 0;
		break;
	case COMEDI_DIGITAL_TRIG_ENABLE_EDGES:
		pm |= chan_mask;	/* enable channels */
		pt |= chan_mask;	/* enable edge detection */
		pp |= hi_mask;		/* rising-edge channels */
		pp &= ~lo_mask;		/* falling-edge channels */
		break;
	case COMEDI_DIGITAL_TRIG_ENABLE_LEVELS:
		pm |= chan_mask;	/* enable channels */
		pt &= ~chan_mask;	/* enable level detection */
		pp |= hi_mask;		/* high level channels */
		pp &= ~lo_mask;		/* low level channels */
		break;
	default:
		return -EINVAL;
	}

	/*
	 * The AND mode trigger can only have one channel (max) enabled
	 * for edge detection.
	 */
	if (trig == 0) {
		int ret = 0;
		unsigned int src;

		src = pt & 0xff;
		if (src)
			ret |= comedi_check_trigger_is_unique(src);

		src = (pt >> 8) & 0xff;
		if (src)
			ret |= comedi_check_trigger_is_unique(src);

		if (ret) {
			dev_dbg(dev->class_dev,
				"invalid AND trigger configuration\n");
			return ret;
		}
	}

	/* save the trigger configuration */
	devpriv->pm[trig] = pm;
	devpriv->pt[trig] = pt;
	devpriv->pp[trig] = pp;

	return insn->n;
}

static int apci1500_di_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	switch (data[0]) {
	case INSN_CONFIG_DIGITAL_TRIG:
		return apci1500_di_cfg_trig(dev, s, insn, data);
	default:
		return -EINVAL;
	}
}

static int apci1500_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci1500_private *devpriv = dev->private;

	data[1] = inw(devpriv->addon + APCI1500_DI_REG);

	return insn->n;
}

static int apci1500_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci1500_private *devpriv = dev->private;

	if (comedi_dio_update_state(s, data))
		outw(s->state, devpriv->addon + APCI1500_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static int apci1500_timer_insn_config(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	struct apci1500_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val;

	switch (data[0]) {
	case INSN_CONFIG_ARM:
		val = data[1] & s->maxdata;
		z8536_write(dev, val & 0xff, Z8536_CT_RELOAD_LSB_REG(chan));
		z8536_write(dev, (val >> 8) & 0xff,
			    Z8536_CT_RELOAD_MSB_REG(chan));

		apci1500_timer_enable(dev, chan, true);
		z8536_write(dev, Z8536_CT_CMDSTAT_GCB,
			    Z8536_CT_CMDSTAT_REG(chan));
		break;
	case INSN_CONFIG_DISARM:
		apci1500_timer_enable(dev, chan, false);
		break;

	case INSN_CONFIG_GET_COUNTER_STATUS:
		data[1] = 0;
		val = z8536_read(dev, Z8536_CT_CMDSTAT_REG(chan));
		if (val & Z8536_CT_STAT_CIP)
			data[1] |= COMEDI_COUNTER_COUNTING;
		if (val & Z8536_CT_CMDSTAT_GCB)
			data[1] |= COMEDI_COUNTER_ARMED;
		if (val & Z8536_STAT_IP) {
			data[1] |= COMEDI_COUNTER_TERMINAL_COUNT;
			apci1500_ack_irq(dev, Z8536_CT_CMDSTAT_REG(chan));
		}
		data[2] = COMEDI_COUNTER_ARMED | COMEDI_COUNTER_COUNTING |
			  COMEDI_COUNTER_TERMINAL_COUNT;
		break;

	case INSN_CONFIG_SET_COUNTER_MODE:
		/* Simulate the 8254 timer modes */
		switch (data[1]) {
		case I8254_MODE0:
			/* Interrupt on Terminal Count */
			val = Z8536_CT_MODE_ECE |
			      Z8536_CT_MODE_DCS_ONESHOT;
			break;
		case I8254_MODE1:
			/* Hardware Retriggerable One-Shot */
			val = Z8536_CT_MODE_ETE |
			      Z8536_CT_MODE_DCS_ONESHOT;
			break;
		case I8254_MODE2:
			/* Rate Generator */
			val = Z8536_CT_MODE_CSC |
			      Z8536_CT_MODE_DCS_PULSE;
			break;
		case I8254_MODE3:
			/* Square Wave Mode */
			val = Z8536_CT_MODE_CSC |
			      Z8536_CT_MODE_DCS_SQRWAVE;
			break;
		case I8254_MODE4:
			/* Software Triggered Strobe */
			val = Z8536_CT_MODE_REB |
			      Z8536_CT_MODE_DCS_PULSE;
			break;
		case I8254_MODE5:
			/* Hardware Triggered Strobe (watchdog) */
			val = Z8536_CT_MODE_EOE |
			      Z8536_CT_MODE_ETE |
			      Z8536_CT_MODE_REB |
			      Z8536_CT_MODE_DCS_PULSE;
			break;
		default:
			return -EINVAL;
		}
		apci1500_timer_enable(dev, chan, false);
		z8536_write(dev, val, Z8536_CT_MODE_REG(chan));
		break;

	case INSN_CONFIG_SET_CLOCK_SRC:
		if (data[1] > 2)
			return -EINVAL;
		devpriv->clk_src = data[1];
		if (devpriv->clk_src == 2)
			devpriv->clk_src = 3;
		outw(devpriv->clk_src, devpriv->addon + APCI1500_CLK_SEL_REG);
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		switch (devpriv->clk_src) {
		case 0:
			data[1] = 0;		/* 111.86 kHz / 2 */
			data[2] = 17879;	/* 17879 ns (approx) */
			break;
		case 1:
			data[1] = 1;		/* 3.49 kHz / 2 */
			data[2] = 573066;	/* 573066 ns (approx) */
			break;
		case 3:
			data[1] = 2;		/* 1.747 kHz / 2 */
			data[2] = 1164822;	/* 1164822 ns (approx) */
			break;
		default:
			return -EINVAL;
		}
		break;

	case INSN_CONFIG_SET_GATE_SRC:
		if (chan == 0)
			return -EINVAL;

		val = z8536_read(dev, Z8536_CT_MODE_REG(chan));
		val &= Z8536_CT_MODE_EGE;
		if (data[1] == 1)
			val |= Z8536_CT_MODE_EGE;
		else if (data[1] > 1)
			return -EINVAL;
		z8536_write(dev, val, Z8536_CT_MODE_REG(chan));
		break;
	case INSN_CONFIG_GET_GATE_SRC:
		if (chan == 0)
			return -EINVAL;
		break;

	default:
		return -EINVAL;
	}
	return insn->n;
}

static int apci1500_timer_insn_write(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int cmd;

	cmd = z8536_read(dev, Z8536_CT_CMDSTAT_REG(chan));
	cmd &= Z8536_CT_CMDSTAT_GCB;	/* preserve gate */
	cmd |= Z8536_CT_CMD_TCB;	/* set trigger */

	/* software trigger a timer, it only makes sense to do one write */
	if (insn->n)
		z8536_write(dev, cmd, Z8536_CT_CMDSTAT_REG(chan));

	return insn->n;
}

static int apci1500_timer_insn_read(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int cmd;
	unsigned int val;
	int i;

	cmd = z8536_read(dev, Z8536_CT_CMDSTAT_REG(chan));
	cmd &= Z8536_CT_CMDSTAT_GCB;	/* preserve gate */
	cmd |= Z8536_CT_CMD_RCC;	/* set RCC */

	for (i = 0; i < insn->n; i++) {
		z8536_write(dev, cmd, Z8536_CT_CMDSTAT_REG(chan));

		val = z8536_read(dev, Z8536_CT_VAL_MSB_REG(chan)) << 8;
		val |= z8536_read(dev, Z8536_CT_VAL_LSB_REG(chan));

		data[i] = val;
	}

	return insn->n;
}

static int apci1500_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct apci1500_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->amcc = pci_resource_start(pcidev, 0);
	devpriv->addon = pci_resource_start(pcidev, 2);

	z8536_reset(dev);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci1500_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	/* Digital Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci1500_di_insn_bits;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags |= SDF_CMD_READ;
		s->len_chanlist	= 1;
		s->insn_config	= apci1500_di_insn_config;
		s->do_cmdtest	= apci1500_di_cmdtest;
		s->do_cmd	= apci1500_di_cmd;
		s->cancel	= apci1500_di_cancel;
	}

	/* Digital Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci1500_do_insn_bits;

	/* reset all the digital outputs */
	outw(0x0, devpriv->addon + APCI1500_DO_REG);

	/* Counter/Timer(Watchdog) subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_TIMER;
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE;
	s->n_chan	= 3;
	s->maxdata	= 0xffff;
	s->range_table	= &range_unknown;
	s->insn_config	= apci1500_timer_insn_config;
	s->insn_write	= apci1500_timer_insn_write;
	s->insn_read	= apci1500_timer_insn_read;

	/* Enable the PCI interrupt */
	if (dev->irq) {
		outl(0x2000 | INTCSR_INBOX_FULL_INT,
		     devpriv->amcc + AMCC_OP_REG_INTCSR);
		inl(devpriv->amcc + AMCC_OP_REG_IMB1);
		inl(devpriv->amcc + AMCC_OP_REG_INTCSR);
		outl(INTCSR_INBOX_INTR_STATUS | 0x2000 | INTCSR_INBOX_FULL_INT,
		     devpriv->amcc + AMCC_OP_REG_INTCSR);
	}

	return 0;
}

static void apci1500_detach(struct comedi_device *dev)
{
	struct apci1500_private *devpriv = dev->private;

	if (devpriv->amcc)
		outl(0x0, devpriv->amcc + AMCC_OP_REG_INTCSR);
	comedi_pci_detach(dev);
}

static struct comedi_driver apci1500_driver = {
	.driver_name	= "addi_apci_1500",
	.module		= THIS_MODULE,
	.auto_attach	= apci1500_auto_attach,
	.detach		= apci1500_detach,
};

static int apci1500_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci1500_driver, id->driver_data);
}

static const struct pci_device_id apci1500_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMCC, 0x80fc) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1500_pci_table);

static struct pci_driver apci1500_pci_driver = {
	.name		= "addi_apci_1500",
	.id_table	= apci1500_pci_table,
	.probe		= apci1500_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci1500_driver, apci1500_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("ADDI-DATA APCI-1500, 16 channel DI / 16 channel DO boards");
MODULE_LICENSE("GPL");
