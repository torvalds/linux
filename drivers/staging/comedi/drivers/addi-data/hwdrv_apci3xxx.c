#define APCI3XXX_SINGLE				0
#define APCI3XXX_DIFF				1
#define APCI3XXX_CONFIGURATION			0

/*
+----------------------------------------------------------------------------+
|                         ANALOG INPUT FUNCTIONS                             |
+----------------------------------------------------------------------------+
*/

/*
+----------------------------------------------------------------------------+
| Function Name     : int   i_APCI3XXX_TestConversionStarted                 |
|                          (struct comedi_device    *dev)                           |
+----------------------------------------------------------------------------+
| Task                Test if any conversion started                         |
+----------------------------------------------------------------------------+
| Input Parameters  : -                                                      |
+----------------------------------------------------------------------------+
| Output Parameters : -                                                      |
+----------------------------------------------------------------------------+
| Return Value      : 0 : Conversion not started                             |
|                     1 : Conversion started                                 |
+----------------------------------------------------------------------------+
*/
static int i_APCI3XXX_TestConversionStarted(struct comedi_device *dev)
{
	struct apci3xxx_private *devpriv = dev->private;

	if ((readl(devpriv->mmio + 8) & 0x80000) == 0x80000)
		return 1;
	else
		return 0;

}

static int apci3xxx_ai_configure(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	const struct apci3xxx_boardinfo *board = comedi_board(dev);
	struct apci3xxx_private *devpriv = dev->private;
	unsigned int aref_mode = data[1];
	unsigned int time_base = data[2];
	unsigned int reload_time = data[3];
	unsigned int acq_ns;

	if (aref_mode != 0 && aref_mode != 1)
		return -EINVAL;

	if (!(board->b_AvailableConvertUnit & (1 << time_base)))
		return -EINVAL;

	if (reload_time > 0xffff)
		return -EINVAL;

	switch (time_base) {
	case 0:
		acq_ns = reload_time;		/* ns */
	case 1:
		acq_ns = reload_time * 1000;	/* us */
	case 2:
		acq_ns = reload_time * 1000000;	/* ms */
	default:
		return -EINVAL;
	}

	/* Test the convert time value */
	if (acq_ns < board->ui_MinAcquisitiontimeNs)
		return -EINVAL;

	/* Test if conversion not started */
	if (i_APCI3XXX_TestConversionStarted(dev))
		return -EBUSY;

	devpriv->ui_EocEosConversionTime = reload_time;
	devpriv->b_EocEosConversionTimeBase = time_base;
	devpriv->b_SingelDiff = aref_mode;

	/* Set the convert timing unit */
	writel(time_base, devpriv->mmio + 36);

	/* Set the convert timing */
	writel(reload_time, devpriv->mmio + 32);

	return insn->n;
}

static int apci3xxx_ai_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	switch (data[0]) {
	case APCI3XXX_CONFIGURATION:
		if (insn->n == 4)
			return apci3xxx_ai_configure(dev, s, insn, data);
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
}

static int apci3xxx_ai_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct apci3xxx_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned char use_interrupt = 0;	/* FIXME: use interrupts */
	unsigned int delay_mode;
	unsigned int val;
	int i;

	if (!use_interrupt && insn->n == 0)
		return insn->n;

	if (i_APCI3XXX_TestConversionStarted(dev))
		return -EBUSY;

	/* Clear the FIFO */
	writel(0x10000, devpriv->mmio + 12);

	/* Get and save the delay mode */
	delay_mode = readl(devpriv->mmio + 4);
	delay_mode &= 0xfffffef0;

	/* Channel configuration selection */
	writel(delay_mode, devpriv->mmio + 4);

	/* Make the configuration */
	val = (range & 3) | ((range >> 2) << 6) |
	      (devpriv->b_SingelDiff << 7);
	writel(val, devpriv->mmio + 0);

	/* Channel selection */
	writel(delay_mode | 0x100, devpriv->mmio + 4);
	writel(chan, devpriv->mmio + 0);

	/* Restore delay mode */
	writel(delay_mode, devpriv->mmio + 4);

	/* Set the number of sequence to 1 */
	writel(1, devpriv->mmio + 48);

	/* Save the interrupt flag */
	devpriv->b_EocEosInterrupt = use_interrupt;

	/* Save the number of channels */
	devpriv->ui_AiNbrofChannels = 1;

	/* Test if interrupt not used */
	if (!use_interrupt) {
		for (i = 0; i < insn->n; i++) {
			/* Start the conversion */
			writel(0x80000, devpriv->mmio + 8);

			/* Wait the EOS */
			do {
				val = readl(devpriv->mmio + 20);
				val &= 0x1;
			} while (!val);

			/* Read the analog value */
			data[i] = readl(devpriv->mmio + 28);
		}
	} else {
		/* Start the conversion */
		writel(0x180000, devpriv->mmio + 8);
	}

	return insn->n;
}
