/*
 * Copyright 2009 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This code is in parts based on wm8350-core.c and pcf50633-core.c
 *
 * Initial development of this code was funded by
 * Phytec Messtechnik GmbH, http://www.phytec.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/mfd/mc13783-private.h>
#include <linux/platform_device.h>
#include <linux/mfd/mc13783.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/irq.h>

#define MC13783_MAX_REG_NUM	0x3f
#define MC13783_FRAME_MASK	0x00ffffff
#define MC13783_MAX_REG_NUM	0x3f
#define MC13783_REG_NUM_SHIFT	0x19
#define MC13783_WRITE_BIT_SHIFT	31

static inline int spi_rw(struct spi_device *spi, u8 * buf, size_t len)
{
	struct spi_transfer t = {
		.tx_buf = (const void *)buf,
		.rx_buf = buf,
		.len = len,
		.cs_change = 0,
		.delay_usecs = 0,
	};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	if (spi_sync(spi, &m) != 0 || m.status != 0)
		return -EINVAL;
	return len - m.actual_length;
}

static int mc13783_read(struct mc13783 *mc13783, int reg_num, u32 *reg_val)
{
	unsigned int frame = 0;
	int ret = 0;

	if (reg_num > MC13783_MAX_REG_NUM)
		return -EINVAL;

	frame |= reg_num << MC13783_REG_NUM_SHIFT;

	ret = spi_rw(mc13783->spi_device, (u8 *)&frame, 4);

	*reg_val = frame & MC13783_FRAME_MASK;

	return ret;
}

static int mc13783_write(struct mc13783 *mc13783, int reg_num, u32 reg_val)
{
	unsigned int frame = 0;

	if (reg_num > MC13783_MAX_REG_NUM)
		return -EINVAL;

	frame |= (1 << MC13783_WRITE_BIT_SHIFT);
	frame |= reg_num << MC13783_REG_NUM_SHIFT;
	frame |= reg_val & MC13783_FRAME_MASK;

	return spi_rw(mc13783->spi_device, (u8 *)&frame, 4);
}

int mc13783_reg_read(struct mc13783 *mc13783, int reg_num, u32 *reg_val)
{
	int ret;

	mutex_lock(&mc13783->io_lock);
	ret = mc13783_read(mc13783, reg_num, reg_val);
	mutex_unlock(&mc13783->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(mc13783_reg_read);

int mc13783_reg_write(struct mc13783 *mc13783, int reg_num, u32 reg_val)
{
	int ret;

	mutex_lock(&mc13783->io_lock);
	ret = mc13783_write(mc13783, reg_num, reg_val);
	mutex_unlock(&mc13783->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(mc13783_reg_write);

/**
 * mc13783_set_bits - Bitmask write
 *
 * @mc13783: Pointer to mc13783 control structure
 * @reg:    Register to access
 * @mask:   Mask of bits to change
 * @val:    Value to set for masked bits
 */
int mc13783_set_bits(struct mc13783 *mc13783, int reg, u32 mask, u32 val)
{
	u32 tmp;
	int ret;

	mutex_lock(&mc13783->io_lock);

	ret = mc13783_read(mc13783, reg, &tmp);
	tmp = (tmp & ~mask) | val;
	if (ret == 0)
		ret = mc13783_write(mc13783, reg, tmp);

	mutex_unlock(&mc13783->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(mc13783_set_bits);

int mc13783_register_irq(struct mc13783 *mc13783, int irq,
		void (*handler) (int, void *), void *data)
{
	if (irq < 0 || irq > MC13783_NUM_IRQ || !handler)
		return -EINVAL;

	if (WARN_ON(mc13783->irq_handler[irq].handler))
		return -EBUSY;

	mutex_lock(&mc13783->io_lock);
	mc13783->irq_handler[irq].handler = handler;
	mc13783->irq_handler[irq].data = data;
	mutex_unlock(&mc13783->io_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mc13783_register_irq);

int mc13783_free_irq(struct mc13783 *mc13783, int irq)
{
	if (irq < 0 || irq > MC13783_NUM_IRQ)
		return -EINVAL;

	mutex_lock(&mc13783->io_lock);
	mc13783->irq_handler[irq].handler = NULL;
	mutex_unlock(&mc13783->io_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mc13783_free_irq);

static void mc13783_irq_work(struct work_struct *work)
{
	struct mc13783 *mc13783 = container_of(work, struct mc13783, work);
	int i;
	unsigned int adc_sts;

	/* check if the adc has finished any completion */
	mc13783_reg_read(mc13783, MC13783_REG_INTERRUPT_STATUS_0, &adc_sts);
	mc13783_reg_write(mc13783, MC13783_REG_INTERRUPT_STATUS_0,
			adc_sts & MC13783_INT_STAT_ADCDONEI);

	if (adc_sts & MC13783_INT_STAT_ADCDONEI)
		complete_all(&mc13783->adc_done);

	for (i = 0; i < MC13783_NUM_IRQ; i++)
		if (mc13783->irq_handler[i].handler)
			mc13783->irq_handler[i].handler(i,
					mc13783->irq_handler[i].data);
	enable_irq(mc13783->irq);
}

static irqreturn_t mc13783_interrupt(int irq, void *dev_id)
{
	struct mc13783 *mc13783 = dev_id;

	disable_irq_nosync(irq);

	schedule_work(&mc13783->work);
	return IRQ_HANDLED;
}

/* set adc to ts interrupt mode, which generates touchscreen wakeup interrupt */
static inline void mc13783_adc_set_ts_irq_mode(struct mc13783 *mc13783)
{
	unsigned int reg_adc0, reg_adc1;

	reg_adc0 = MC13783_ADC0_ADREFEN | MC13783_ADC0_ADREFMODE
			| MC13783_ADC0_TSMOD0;
	reg_adc1 = MC13783_ADC1_ADEN | MC13783_ADC1_ADTRIGIGN;

	mc13783_reg_write(mc13783, MC13783_REG_ADC_0, reg_adc0);
	mc13783_reg_write(mc13783, MC13783_REG_ADC_1, reg_adc1);
}

int mc13783_adc_do_conversion(struct mc13783 *mc13783, unsigned int mode,
		unsigned int channel, unsigned int *sample)
{
	unsigned int reg_adc0, reg_adc1;
	int i;

	mutex_lock(&mc13783->adc_conv_lock);

	/* set up auto incrementing anyway to make quick read */
	reg_adc0 =  MC13783_ADC0_ADINC1 | MC13783_ADC0_ADINC2;
	/* enable the adc, ignore external triggering and set ASC to trigger
	 * conversion */
	reg_adc1 =  MC13783_ADC1_ADEN | MC13783_ADC1_ADTRIGIGN
		| MC13783_ADC1_ASC;

	/* setup channel number */
	if (channel > 7)
		reg_adc1 |= MC13783_ADC1_ADSEL;

	switch (mode) {
	case MC13783_ADC_MODE_TS:
		/* enables touch screen reference mode and set touchscreen mode
		 * to position mode */
		reg_adc0 |= MC13783_ADC0_ADREFEN | MC13783_ADC0_ADREFMODE
			| MC13783_ADC0_TSMOD0 | MC13783_ADC0_TSMOD1;
		reg_adc1 |= 4 << MC13783_ADC1_CHAN1_SHIFT;
		break;
	case MC13783_ADC_MODE_SINGLE_CHAN:
		reg_adc1 |= (channel & 0x7) << MC13783_ADC1_CHAN0_SHIFT;
		reg_adc1 |= MC13783_ADC1_RAND;
		break;
	case MC13783_ADC_MODE_MULT_CHAN:
		reg_adc1 |= 4 << MC13783_ADC1_CHAN1_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	mc13783_reg_write(mc13783, MC13783_REG_ADC_0, reg_adc0);
	mc13783_reg_write(mc13783, MC13783_REG_ADC_1, reg_adc1);

	wait_for_completion_interruptible(&mc13783->adc_done);

	for (i = 0; i < 4; i++)
		mc13783_reg_read(mc13783, MC13783_REG_ADC_2, &sample[i]);

	if (mc13783->ts_active)
		mc13783_adc_set_ts_irq_mode(mc13783);

	mutex_unlock(&mc13783->adc_conv_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mc13783_adc_do_conversion);

void mc13783_adc_set_ts_status(struct mc13783 *mc13783, unsigned int status)
{
	mc13783->ts_active = status;
}
EXPORT_SYMBOL_GPL(mc13783_adc_set_ts_status);

static int mc13783_check_revision(struct mc13783 *mc13783)
{
	u32 rev_id, rev1, rev2, finid, icid;

	mc13783_read(mc13783, MC13783_REG_REVISION, &rev_id);

	rev1 = (rev_id & 0x018) >> 3;
	rev2 = (rev_id & 0x007);
	icid = (rev_id & 0x01C0) >> 6;
	finid = (rev_id & 0x01E00) >> 9;

	/* Ver 0.2 is actually 3.2a.  Report as 3.2 */
	if ((rev1 == 0) && (rev2 == 2))
		rev1 = 3;

	if (rev1 == 0 || icid != 2) {
		dev_err(mc13783->dev, "No MC13783 detected.\n");
		return -ENODEV;
	}

	mc13783->revision = ((rev1 * 10) + rev2);
	dev_info(mc13783->dev, "MC13783 Rev %d.%d FinVer %x detected\n", rev1,
	       rev2, finid);

	return 0;
}

/*
 * Register a client device.  This is non-fatal since there is no need to
 * fail the entire device init due to a single platform device failing.
 */
static void mc13783_client_dev_register(struct mc13783 *mc13783,
				       const char *name)
{
	struct mfd_cell cell = {};

	cell.name = name;

	mfd_add_devices(mc13783->dev, -1, &cell, 1, NULL, 0);
}

static int __devinit mc13783_probe(struct spi_device *spi)
{
	struct mc13783 *mc13783;
	struct mc13783_platform_data *pdata = spi->dev.platform_data;
	int ret;

	mc13783 = kzalloc(sizeof(struct mc13783), GFP_KERNEL);
	if (!mc13783)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, mc13783);
	spi->mode = SPI_MODE_0 | SPI_CS_HIGH;
	spi->bits_per_word = 32;
	spi_setup(spi);

	mc13783->spi_device = spi;
	mc13783->dev = &spi->dev;
	mc13783->irq = spi->irq;

	INIT_WORK(&mc13783->work, mc13783_irq_work);
	mutex_init(&mc13783->io_lock);
	mutex_init(&mc13783->adc_conv_lock);
	init_completion(&mc13783->adc_done);

	if (pdata) {
		mc13783->flags = pdata->flags;
		mc13783->regulators = pdata->regulators;
		mc13783->num_regulators = pdata->num_regulators;
	}

	if (mc13783_check_revision(mc13783)) {
		ret = -ENODEV;
		goto err_out;
	}

	/* clear and mask all interrupts */
	mc13783_reg_write(mc13783, MC13783_REG_INTERRUPT_STATUS_0, 0x00ffffff);
	mc13783_reg_write(mc13783, MC13783_REG_INTERRUPT_MASK_0, 0x00ffffff);
	mc13783_reg_write(mc13783, MC13783_REG_INTERRUPT_STATUS_1, 0x00ffffff);
	mc13783_reg_write(mc13783, MC13783_REG_INTERRUPT_MASK_1, 0x00ffffff);

	/* unmask adcdone interrupts */
	mc13783_set_bits(mc13783, MC13783_REG_INTERRUPT_MASK_0,
			MC13783_INT_MASK_ADCDONEM, 0);

	ret = request_irq(mc13783->irq, mc13783_interrupt,
			IRQF_DISABLED | IRQF_TRIGGER_HIGH, "mc13783",
			mc13783);
	if (ret)
		goto err_out;

	if (mc13783->flags & MC13783_USE_CODEC)
		mc13783_client_dev_register(mc13783, "mc13783-codec");
	if (mc13783->flags & MC13783_USE_ADC)
		mc13783_client_dev_register(mc13783, "mc13783-adc");
	if (mc13783->flags & MC13783_USE_RTC)
		mc13783_client_dev_register(mc13783, "mc13783-rtc");
	if (mc13783->flags & MC13783_USE_REGULATOR)
		mc13783_client_dev_register(mc13783, "mc13783-regulator");
	if (mc13783->flags & MC13783_USE_TOUCHSCREEN)
		mc13783_client_dev_register(mc13783, "mc13783-ts");

	return 0;

err_out:
	kfree(mc13783);
	return ret;
}

static int __devexit mc13783_remove(struct spi_device *spi)
{
	struct mc13783 *mc13783;

	mc13783 = dev_get_drvdata(&spi->dev);

	free_irq(mc13783->irq, mc13783);

	mfd_remove_devices(&spi->dev);

	return 0;
}

static struct spi_driver pmic_driver = {
	.driver = {
		   .name = "mc13783",
		   .bus = &spi_bus_type,
		   .owner = THIS_MODULE,
	},
	.probe = mc13783_probe,
	.remove = __devexit_p(mc13783_remove),
};

static int __init pmic_init(void)
{
	return spi_register_driver(&pmic_driver);
}
subsys_initcall(pmic_init);

static void __exit pmic_exit(void)
{
	spi_unregister_driver(&pmic_driver);
}
module_exit(pmic_exit);

MODULE_DESCRIPTION("Core/Protocol driver for Freescale MC13783 PMIC");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_LICENSE("GPL");

