/*
 * Copyright 2009 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * loosely based on an earlier driver that has
 * Copyright 2009 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mc13783-private.h>

#define MC13783_IRQSTAT0	0
#define MC13783_IRQSTAT0_ADCDONEI	(1 << 0)
#define MC13783_IRQSTAT0_ADCBISDONEI	(1 << 1)
#define MC13783_IRQSTAT0_TSI		(1 << 2)
#define MC13783_IRQSTAT0_WHIGHI		(1 << 3)
#define MC13783_IRQSTAT0_WLOWI		(1 << 4)
#define MC13783_IRQSTAT0_CHGDETI	(1 << 6)
#define MC13783_IRQSTAT0_CHGOVI		(1 << 7)
#define MC13783_IRQSTAT0_CHGREVI	(1 << 8)
#define MC13783_IRQSTAT0_CHGSHORTI	(1 << 9)
#define MC13783_IRQSTAT0_CCCVI		(1 << 10)
#define MC13783_IRQSTAT0_CHGCURRI	(1 << 11)
#define MC13783_IRQSTAT0_BPONI		(1 << 12)
#define MC13783_IRQSTAT0_LOBATLI	(1 << 13)
#define MC13783_IRQSTAT0_LOBATHI	(1 << 14)
#define MC13783_IRQSTAT0_UDPI		(1 << 15)
#define MC13783_IRQSTAT0_USBI		(1 << 16)
#define MC13783_IRQSTAT0_IDI		(1 << 19)
#define MC13783_IRQSTAT0_SE1I		(1 << 21)
#define MC13783_IRQSTAT0_CKDETI		(1 << 22)
#define MC13783_IRQSTAT0_UDMI		(1 << 23)

#define MC13783_IRQMASK0	1
#define MC13783_IRQMASK0_ADCDONEM	MC13783_IRQSTAT0_ADCDONEI
#define MC13783_IRQMASK0_ADCBISDONEM	MC13783_IRQSTAT0_ADCBISDONEI
#define MC13783_IRQMASK0_TSM		MC13783_IRQSTAT0_TSI
#define MC13783_IRQMASK0_WHIGHM		MC13783_IRQSTAT0_WHIGHI
#define MC13783_IRQMASK0_WLOWM		MC13783_IRQSTAT0_WLOWI
#define MC13783_IRQMASK0_CHGDETM	MC13783_IRQSTAT0_CHGDETI
#define MC13783_IRQMASK0_CHGOVM		MC13783_IRQSTAT0_CHGOVI
#define MC13783_IRQMASK0_CHGREVM	MC13783_IRQSTAT0_CHGREVI
#define MC13783_IRQMASK0_CHGSHORTM	MC13783_IRQSTAT0_CHGSHORTI
#define MC13783_IRQMASK0_CCCVM		MC13783_IRQSTAT0_CCCVI
#define MC13783_IRQMASK0_CHGCURRM	MC13783_IRQSTAT0_CHGCURRI
#define MC13783_IRQMASK0_BPONM		MC13783_IRQSTAT0_BPONI
#define MC13783_IRQMASK0_LOBATLM	MC13783_IRQSTAT0_LOBATLI
#define MC13783_IRQMASK0_LOBATHM	MC13783_IRQSTAT0_LOBATHI
#define MC13783_IRQMASK0_UDPM		MC13783_IRQSTAT0_UDPI
#define MC13783_IRQMASK0_USBM		MC13783_IRQSTAT0_USBI
#define MC13783_IRQMASK0_IDM		MC13783_IRQSTAT0_IDI
#define MC13783_IRQMASK0_SE1M		MC13783_IRQSTAT0_SE1I
#define MC13783_IRQMASK0_CKDETM		MC13783_IRQSTAT0_CKDETI
#define MC13783_IRQMASK0_UDMM		MC13783_IRQSTAT0_UDMI

#define MC13783_IRQSTAT1	3
#define MC13783_IRQSTAT1_1HZI		(1 << 0)
#define MC13783_IRQSTAT1_TODAI		(1 << 1)
#define MC13783_IRQSTAT1_ONOFD1I	(1 << 3)
#define MC13783_IRQSTAT1_ONOFD2I	(1 << 4)
#define MC13783_IRQSTAT1_ONOFD3I	(1 << 5)
#define MC13783_IRQSTAT1_SYSRSTI	(1 << 6)
#define MC13783_IRQSTAT1_RTCRSTI	(1 << 7)
#define MC13783_IRQSTAT1_PCI		(1 << 8)
#define MC13783_IRQSTAT1_WARMI		(1 << 9)
#define MC13783_IRQSTAT1_MEMHLDI	(1 << 10)
#define MC13783_IRQSTAT1_PWRRDYI	(1 << 11)
#define MC13783_IRQSTAT1_THWARNLI	(1 << 12)
#define MC13783_IRQSTAT1_THWARNHI	(1 << 13)
#define MC13783_IRQSTAT1_CLKI		(1 << 14)
#define MC13783_IRQSTAT1_SEMAFI		(1 << 15)
#define MC13783_IRQSTAT1_MC2BI		(1 << 17)
#define MC13783_IRQSTAT1_HSDETI		(1 << 18)
#define MC13783_IRQSTAT1_HSLI		(1 << 19)
#define MC13783_IRQSTAT1_ALSPTHI	(1 << 20)
#define MC13783_IRQSTAT1_AHSSHORTI	(1 << 21)

#define MC13783_IRQMASK1	4
#define MC13783_IRQMASK1_1HZM		MC13783_IRQSTAT1_1HZI
#define MC13783_IRQMASK1_TODAM		MC13783_IRQSTAT1_TODAI
#define MC13783_IRQMASK1_ONOFD1M	MC13783_IRQSTAT1_ONOFD1I
#define MC13783_IRQMASK1_ONOFD2M	MC13783_IRQSTAT1_ONOFD2I
#define MC13783_IRQMASK1_ONOFD3M	MC13783_IRQSTAT1_ONOFD3I
#define MC13783_IRQMASK1_SYSRSTM	MC13783_IRQSTAT1_SYSRSTI
#define MC13783_IRQMASK1_RTCRSTM	MC13783_IRQSTAT1_RTCRSTI
#define MC13783_IRQMASK1_PCM		MC13783_IRQSTAT1_PCI
#define MC13783_IRQMASK1_WARMM		MC13783_IRQSTAT1_WARMI
#define MC13783_IRQMASK1_MEMHLDM	MC13783_IRQSTAT1_MEMHLDI
#define MC13783_IRQMASK1_PWRRDYM	MC13783_IRQSTAT1_PWRRDYI
#define MC13783_IRQMASK1_THWARNLM	MC13783_IRQSTAT1_THWARNLI
#define MC13783_IRQMASK1_THWARNHM	MC13783_IRQSTAT1_THWARNHI
#define MC13783_IRQMASK1_CLKM		MC13783_IRQSTAT1_CLKI
#define MC13783_IRQMASK1_SEMAFM		MC13783_IRQSTAT1_SEMAFI
#define MC13783_IRQMASK1_MC2BM		MC13783_IRQSTAT1_MC2BI
#define MC13783_IRQMASK1_HSDETM		MC13783_IRQSTAT1_HSDETI
#define MC13783_IRQMASK1_HSLM		MC13783_IRQSTAT1_HSLI
#define MC13783_IRQMASK1_ALSPTHM	MC13783_IRQSTAT1_ALSPTHI
#define MC13783_IRQMASK1_AHSSHORTM	MC13783_IRQSTAT1_AHSSHORTI

#define MC13783_ADC1		44
#define MC13783_ADC1_ADEN		(1 << 0)
#define MC13783_ADC1_RAND		(1 << 1)
#define MC13783_ADC1_ADSEL		(1 << 3)
#define MC13783_ADC1_ASC		(1 << 20)
#define MC13783_ADC1_ADTRIGIGN		(1 << 21)

#define MC13783_NUMREGS 0x3f

void mc13783_lock(struct mc13783 *mc13783)
{
	if (!mutex_trylock(&mc13783->lock)) {
		dev_dbg(&mc13783->spidev->dev, "wait for %s from %pf\n",
				__func__, __builtin_return_address(0));

		mutex_lock(&mc13783->lock);
	}
	dev_dbg(&mc13783->spidev->dev, "%s from %pf\n",
			__func__, __builtin_return_address(0));
}
EXPORT_SYMBOL(mc13783_lock);

void mc13783_unlock(struct mc13783 *mc13783)
{
	dev_dbg(&mc13783->spidev->dev, "%s from %pf\n",
			__func__, __builtin_return_address(0));
	mutex_unlock(&mc13783->lock);
}
EXPORT_SYMBOL(mc13783_unlock);

#define MC13783_REGOFFSET_SHIFT 25
int mc13783_reg_read(struct mc13783 *mc13783, unsigned int offset, u32 *val)
{
	struct spi_transfer t;
	struct spi_message m;
	int ret;

	BUG_ON(!mutex_is_locked(&mc13783->lock));

	if (offset > MC13783_NUMREGS)
		return -EINVAL;

	*val = offset << MC13783_REGOFFSET_SHIFT;

	memset(&t, 0, sizeof(t));

	t.tx_buf = val;
	t.rx_buf = val;
	t.len = sizeof(u32);

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	ret = spi_sync(mc13783->spidev, &m);

	/* error in message.status implies error return from spi_sync */
	BUG_ON(!ret && m.status);

	if (ret)
		return ret;

	*val &= 0xffffff;

	dev_vdbg(&mc13783->spidev->dev, "[0x%02x] -> 0x%06x\n", offset, *val);

	return 0;
}
EXPORT_SYMBOL(mc13783_reg_read);

int mc13783_reg_write(struct mc13783 *mc13783, unsigned int offset, u32 val)
{
	u32 buf;
	struct spi_transfer t;
	struct spi_message m;
	int ret;

	BUG_ON(!mutex_is_locked(&mc13783->lock));

	dev_vdbg(&mc13783->spidev->dev, "[0x%02x] <- 0x%06x\n", offset, val);

	if (offset > MC13783_NUMREGS || val > 0xffffff)
		return -EINVAL;

	buf = 1 << 31 | offset << MC13783_REGOFFSET_SHIFT | val;

	memset(&t, 0, sizeof(t));

	t.tx_buf = &buf;
	t.rx_buf = &buf;
	t.len = sizeof(u32);

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	ret = spi_sync(mc13783->spidev, &m);

	BUG_ON(!ret && m.status);

	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(mc13783_reg_write);

int mc13783_reg_rmw(struct mc13783 *mc13783, unsigned int offset,
		u32 mask, u32 val)
{
	int ret;
	u32 valread;

	BUG_ON(val & ~mask);

	ret = mc13783_reg_read(mc13783, offset, &valread);
	if (ret)
		return ret;

	valread = (valread & ~mask) | val;

	return mc13783_reg_write(mc13783, offset, valread);
}
EXPORT_SYMBOL(mc13783_reg_rmw);

int mc13783_irq_mask(struct mc13783 *mc13783, int irq)
{
	int ret;
	unsigned int offmask = irq < 24 ? MC13783_IRQMASK0 : MC13783_IRQMASK1;
	u32 irqbit = 1 << (irq < 24 ? irq : irq - 24);
	u32 mask;

	if (irq < 0 || irq >= MC13783_NUM_IRQ)
		return -EINVAL;

	ret = mc13783_reg_read(mc13783, offmask, &mask);
	if (ret)
		return ret;

	if (mask & irqbit)
		/* already masked */
		return 0;

	return mc13783_reg_write(mc13783, offmask, mask | irqbit);
}
EXPORT_SYMBOL(mc13783_irq_mask);

int mc13783_irq_unmask(struct mc13783 *mc13783, int irq)
{
	int ret;
	unsigned int offmask = irq < 24 ? MC13783_IRQMASK0 : MC13783_IRQMASK1;
	u32 irqbit = 1 << (irq < 24 ? irq : irq - 24);
	u32 mask;

	if (irq < 0 || irq >= MC13783_NUM_IRQ)
		return -EINVAL;

	ret = mc13783_reg_read(mc13783, offmask, &mask);
	if (ret)
		return ret;

	if (!(mask & irqbit))
		/* already unmasked */
		return 0;

	return mc13783_reg_write(mc13783, offmask, mask & ~irqbit);
}
EXPORT_SYMBOL(mc13783_irq_unmask);

int mc13783_irq_status(struct mc13783 *mc13783, int irq,
		int *enabled, int *pending)
{
	int ret;
	unsigned int offmask = irq < 24 ? MC13783_IRQMASK0 : MC13783_IRQMASK1;
	unsigned int offstat = irq < 24 ? MC13783_IRQSTAT0 : MC13783_IRQSTAT1;
	u32 irqbit = 1 << (irq < 24 ? irq : irq - 24);

	if (irq < 0 || irq >= MC13783_NUM_IRQ)
		return -EINVAL;

	if (enabled) {
		u32 mask;

		ret = mc13783_reg_read(mc13783, offmask, &mask);
		if (ret)
			return ret;

		*enabled = mask & irqbit;
	}

	if (pending) {
		u32 stat;

		ret = mc13783_reg_read(mc13783, offstat, &stat);
		if (ret)
			return ret;

		*pending = stat & irqbit;
	}

	return 0;
}
EXPORT_SYMBOL(mc13783_irq_status);

int mc13783_irq_ack(struct mc13783 *mc13783, int irq)
{
	unsigned int offstat = irq < 24 ? MC13783_IRQSTAT0 : MC13783_IRQSTAT1;
	unsigned int val = 1 << (irq < 24 ? irq : irq - 24);

	BUG_ON(irq < 0 || irq >= MC13783_NUM_IRQ);

	return mc13783_reg_write(mc13783, offstat, val);
}
EXPORT_SYMBOL(mc13783_irq_ack);

int mc13783_irq_request_nounmask(struct mc13783 *mc13783, int irq,
		irq_handler_t handler, const char *name, void *dev)
{
	BUG_ON(!mutex_is_locked(&mc13783->lock));
	BUG_ON(!handler);

	if (irq < 0 || irq >= MC13783_NUM_IRQ)
		return -EINVAL;

	if (mc13783->irqhandler[irq])
		return -EBUSY;

	mc13783->irqhandler[irq] = handler;
	mc13783->irqdata[irq] = dev;

	return 0;
}
EXPORT_SYMBOL(mc13783_irq_request_nounmask);

int mc13783_irq_request(struct mc13783 *mc13783, int irq,
		irq_handler_t handler, const char *name, void *dev)
{
	int ret;

	ret = mc13783_irq_request_nounmask(mc13783, irq, handler, name, dev);
	if (ret)
		return ret;

	ret = mc13783_irq_unmask(mc13783, irq);
	if (ret) {
		mc13783->irqhandler[irq] = NULL;
		mc13783->irqdata[irq] = NULL;
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(mc13783_irq_request);

int mc13783_irq_free(struct mc13783 *mc13783, int irq, void *dev)
{
	int ret;
	BUG_ON(!mutex_is_locked(&mc13783->lock));

	if (irq < 0 || irq >= MC13783_NUM_IRQ || !mc13783->irqhandler[irq] ||
			mc13783->irqdata[irq] != dev)
		return -EINVAL;

	ret = mc13783_irq_mask(mc13783, irq);
	if (ret)
		return ret;

	mc13783->irqhandler[irq] = NULL;
	mc13783->irqdata[irq] = NULL;

	return 0;
}
EXPORT_SYMBOL(mc13783_irq_free);

static inline irqreturn_t mc13783_irqhandler(struct mc13783 *mc13783, int irq)
{
	return mc13783->irqhandler[irq](irq, mc13783->irqdata[irq]);
}

/*
 * returns: number of handled irqs or negative error
 * locking: holds mc13783->lock
 */
static int mc13783_irq_handle(struct mc13783 *mc13783,
		unsigned int offstat, unsigned int offmask, int baseirq)
{
	u32 stat, mask;
	int ret = mc13783_reg_read(mc13783, offstat, &stat);
	int num_handled = 0;

	if (ret)
		return ret;

	ret = mc13783_reg_read(mc13783, offmask, &mask);
	if (ret)
		return ret;

	while (stat & ~mask) {
		int irq = __ffs(stat & ~mask);

		stat &= ~(1 << irq);

		if (likely(mc13783->irqhandler[baseirq + irq])) {
			irqreturn_t handled;

			handled = mc13783_irqhandler(mc13783, baseirq + irq);
			if (handled == IRQ_HANDLED)
				num_handled++;
		} else {
			dev_err(&mc13783->spidev->dev,
					"BUG: irq %u but no handler\n",
					baseirq + irq);

			mask |= 1 << irq;

			ret = mc13783_reg_write(mc13783, offmask, mask);
		}
	}

	return num_handled;
}

static irqreturn_t mc13783_irq_thread(int irq, void *data)
{
	struct mc13783 *mc13783 = data;
	irqreturn_t ret;
	int handled = 0;

	mc13783_lock(mc13783);

	ret = mc13783_irq_handle(mc13783, MC13783_IRQSTAT0,
			MC13783_IRQMASK0, MC13783_IRQ_ADCDONE);
	if (ret > 0)
		handled = 1;

	ret = mc13783_irq_handle(mc13783, MC13783_IRQSTAT1,
			MC13783_IRQMASK1, MC13783_IRQ_1HZ);
	if (ret > 0)
		handled = 1;

	mc13783_unlock(mc13783);

	return IRQ_RETVAL(handled);
}

#define MC13783_ADC1_CHAN0_SHIFT	5
#define MC13783_ADC1_CHAN1_SHIFT	8

struct mc13783_adcdone_data {
	struct mc13783 *mc13783;
	struct completion done;
};

static irqreturn_t mc13783_handler_adcdone(int irq, void *data)
{
	struct mc13783_adcdone_data *adcdone_data = data;

	mc13783_irq_ack(adcdone_data->mc13783, irq);

	complete_all(&adcdone_data->done);

	return IRQ_HANDLED;
}

#define MC13783_ADC_WORKING (1 << 16)

int mc13783_adc_do_conversion(struct mc13783 *mc13783, unsigned int mode,
		unsigned int channel, unsigned int *sample)
{
	u32 adc0, adc1, old_adc0;
	int i, ret;
	struct mc13783_adcdone_data adcdone_data = {
		.mc13783 = mc13783,
	};
	init_completion(&adcdone_data.done);

	dev_dbg(&mc13783->spidev->dev, "%s\n", __func__);

	mc13783_lock(mc13783);

	if (mc13783->flags & MC13783_ADC_WORKING) {
		ret = -EBUSY;
		goto out;
	}

	mc13783->flags |= MC13783_ADC_WORKING;

	mc13783_reg_read(mc13783, MC13783_ADC0, &old_adc0);

	adc0 = MC13783_ADC0_ADINC1 | MC13783_ADC0_ADINC2;
	adc1 = MC13783_ADC1_ADEN | MC13783_ADC1_ADTRIGIGN | MC13783_ADC1_ASC;

	if (channel > 7)
		adc1 |= MC13783_ADC1_ADSEL;

	switch (mode) {
	case MC13783_ADC_MODE_TS:
		adc0 |= MC13783_ADC0_ADREFEN | MC13783_ADC0_TSMOD0 |
			MC13783_ADC0_TSMOD1;
		adc1 |= 4 << MC13783_ADC1_CHAN1_SHIFT;
		break;

	case MC13783_ADC_MODE_SINGLE_CHAN:
		adc0 |= old_adc0 & MC13783_ADC0_TSMOD_MASK;
		adc1 |= (channel & 0x7) << MC13783_ADC1_CHAN0_SHIFT;
		adc1 |= MC13783_ADC1_RAND;
		break;

	case MC13783_ADC_MODE_MULT_CHAN:
		adc0 |= old_adc0 & MC13783_ADC0_TSMOD_MASK;
		adc1 |= 4 << MC13783_ADC1_CHAN1_SHIFT;
		break;

	default:
		mc13783_unlock(mc13783);
		return -EINVAL;
	}

	dev_dbg(&mc13783->spidev->dev, "%s: request irq\n", __func__);
	mc13783_irq_request(mc13783, MC13783_IRQ_ADCDONE,
			mc13783_handler_adcdone, __func__, &adcdone_data);
	mc13783_irq_ack(mc13783, MC13783_IRQ_ADCDONE);

	mc13783_reg_write(mc13783, MC13783_REG_ADC_0, adc0);
	mc13783_reg_write(mc13783, MC13783_REG_ADC_1, adc1);

	mc13783_unlock(mc13783);

	ret = wait_for_completion_interruptible_timeout(&adcdone_data.done, HZ);

	if (!ret)
		ret = -ETIMEDOUT;

	mc13783_lock(mc13783);

	mc13783_irq_free(mc13783, MC13783_IRQ_ADCDONE, &adcdone_data);

	if (ret > 0)
		for (i = 0; i < 4; ++i) {
			ret = mc13783_reg_read(mc13783,
					MC13783_REG_ADC_2, &sample[i]);
			if (ret)
				break;
		}

	if (mode == MC13783_ADC_MODE_TS)
		/* restore TSMOD */
		mc13783_reg_write(mc13783, MC13783_REG_ADC_0, old_adc0);

	mc13783->flags &= ~MC13783_ADC_WORKING;
out:
	mc13783_unlock(mc13783);

	return ret;
}
EXPORT_SYMBOL_GPL(mc13783_adc_do_conversion);

static int mc13783_add_subdevice_pdata(struct mc13783 *mc13783,
		const char *name, void *pdata, size_t pdata_size)
{
	struct mfd_cell cell = {
		.name = name,
		.platform_data = pdata,
		.data_size = pdata_size,
	};

	return mfd_add_devices(&mc13783->spidev->dev, -1, &cell, 1, NULL, 0);
}

static int mc13783_add_subdevice(struct mc13783 *mc13783, const char *name)
{
	return mc13783_add_subdevice_pdata(mc13783, name, NULL, 0);
}

static int mc13783_check_revision(struct mc13783 *mc13783)
{
	u32 rev_id, rev1, rev2, finid, icid;

	mc13783_reg_read(mc13783, MC13783_REG_REVISION, &rev_id);

	rev1 = (rev_id & 0x018) >> 3;
	rev2 = (rev_id & 0x007);
	icid = (rev_id & 0x01C0) >> 6;
	finid = (rev_id & 0x01E00) >> 9;

	/* Ver 0.2 is actually 3.2a.  Report as 3.2 */
	if ((rev1 == 0) && (rev2 == 2))
		rev1 = 3;

	if (rev1 == 0 || icid != 2) {
		dev_err(&mc13783->spidev->dev, "No MC13783 detected.\n");
		return -ENODEV;
	}

	dev_info(&mc13783->spidev->dev,
			"MC13783 Rev %d.%d FinVer %x detected\n",
			rev1, rev2, finid);

	return 0;
}

static int mc13783_probe(struct spi_device *spi)
{
	struct mc13783 *mc13783;
	struct mc13783_platform_data *pdata = dev_get_platdata(&spi->dev);
	int ret;

	mc13783 = kzalloc(sizeof(*mc13783), GFP_KERNEL);
	if (!mc13783)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, mc13783);
	spi->mode = SPI_MODE_0 | SPI_CS_HIGH;
	spi->bits_per_word = 32;
	spi_setup(spi);

	mc13783->spidev = spi;

	mutex_init(&mc13783->lock);
	mc13783_lock(mc13783);

	ret = mc13783_check_revision(mc13783);
	if (ret)
		goto err_revision;

	/* mask all irqs */
	ret = mc13783_reg_write(mc13783, MC13783_IRQMASK0, 0x00ffffff);
	if (ret)
		goto err_mask;

	ret = mc13783_reg_write(mc13783, MC13783_IRQMASK1, 0x00ffffff);
	if (ret)
		goto err_mask;

	ret = request_threaded_irq(spi->irq, NULL, mc13783_irq_thread,
			IRQF_ONESHOT | IRQF_TRIGGER_HIGH, "mc13783", mc13783);

	if (ret) {
err_mask:
err_revision:
		mutex_unlock(&mc13783->lock);
		dev_set_drvdata(&spi->dev, NULL);
		kfree(mc13783);
		return ret;
	}

	/* This should go away (BEGIN) */
	if (pdata) {
		mc13783->flags = pdata->flags;
		mc13783->regulators = pdata->regulators;
		mc13783->num_regulators = pdata->num_regulators;
	}
	/* This should go away (END) */

	mc13783_unlock(mc13783);

	if (pdata->flags & MC13783_USE_ADC)
		mc13783_add_subdevice(mc13783, "mc13783-adc");

	if (pdata->flags & MC13783_USE_CODEC)
		mc13783_add_subdevice(mc13783, "mc13783-codec");

	if (pdata->flags & MC13783_USE_REGULATOR) {
		struct mc13783_regulator_platform_data regulator_pdata = {
			.num_regulators = pdata->num_regulators,
			.regulators = pdata->regulators,
		};

		mc13783_add_subdevice_pdata(mc13783, "mc13783-regulator",
				&regulator_pdata, sizeof(regulator_pdata));
	}

	if (pdata->flags & MC13783_USE_RTC)
		mc13783_add_subdevice(mc13783, "mc13783-rtc");

	if (pdata->flags & MC13783_USE_TOUCHSCREEN)
		mc13783_add_subdevice(mc13783, "mc13783-ts");

	if (pdata->flags & MC13783_USE_LED)
		mc13783_add_subdevice_pdata(mc13783, "mc13783-led",
					pdata->leds, sizeof(*pdata->leds));

	return 0;
}

static int __devexit mc13783_remove(struct spi_device *spi)
{
	struct mc13783 *mc13783 = dev_get_drvdata(&spi->dev);

	free_irq(mc13783->spidev->irq, mc13783);

	mfd_remove_devices(&spi->dev);

	return 0;
}

static struct spi_driver mc13783_driver = {
	.driver = {
		.name = "mc13783",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
	},
	.probe = mc13783_probe,
	.remove = __devexit_p(mc13783_remove),
};

static int __init mc13783_init(void)
{
	return spi_register_driver(&mc13783_driver);
}
subsys_initcall(mc13783_init);

static void __exit mc13783_exit(void)
{
	spi_unregister_driver(&mc13783_driver);
}
module_exit(mc13783_exit);

MODULE_DESCRIPTION("Core driver for Freescale MC13783 PMIC");
MODULE_AUTHOR("Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>");
MODULE_LICENSE("GPL v2");
