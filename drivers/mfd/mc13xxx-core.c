/*
 * Copyright 2009-2010 Pengutronix
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
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mc13xxx.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include "mc13xxx.h"

#define MC13XXX_IRQSTAT0	0
#define MC13XXX_IRQSTAT0_ADCDONEI	(1 << 0)
#define MC13XXX_IRQSTAT0_ADCBISDONEI	(1 << 1)
#define MC13XXX_IRQSTAT0_TSI		(1 << 2)
#define MC13783_IRQSTAT0_WHIGHI		(1 << 3)
#define MC13783_IRQSTAT0_WLOWI		(1 << 4)
#define MC13XXX_IRQSTAT0_CHGDETI	(1 << 6)
#define MC13783_IRQSTAT0_CHGOVI		(1 << 7)
#define MC13XXX_IRQSTAT0_CHGREVI	(1 << 8)
#define MC13XXX_IRQSTAT0_CHGSHORTI	(1 << 9)
#define MC13XXX_IRQSTAT0_CCCVI		(1 << 10)
#define MC13XXX_IRQSTAT0_CHGCURRI	(1 << 11)
#define MC13XXX_IRQSTAT0_BPONI		(1 << 12)
#define MC13XXX_IRQSTAT0_LOBATLI	(1 << 13)
#define MC13XXX_IRQSTAT0_LOBATHI	(1 << 14)
#define MC13783_IRQSTAT0_UDPI		(1 << 15)
#define MC13783_IRQSTAT0_USBI		(1 << 16)
#define MC13783_IRQSTAT0_IDI		(1 << 19)
#define MC13783_IRQSTAT0_SE1I		(1 << 21)
#define MC13783_IRQSTAT0_CKDETI		(1 << 22)
#define MC13783_IRQSTAT0_UDMI		(1 << 23)

#define MC13XXX_IRQMASK0	1
#define MC13XXX_IRQMASK0_ADCDONEM	MC13XXX_IRQSTAT0_ADCDONEI
#define MC13XXX_IRQMASK0_ADCBISDONEM	MC13XXX_IRQSTAT0_ADCBISDONEI
#define MC13XXX_IRQMASK0_TSM		MC13XXX_IRQSTAT0_TSI
#define MC13783_IRQMASK0_WHIGHM		MC13783_IRQSTAT0_WHIGHI
#define MC13783_IRQMASK0_WLOWM		MC13783_IRQSTAT0_WLOWI
#define MC13XXX_IRQMASK0_CHGDETM	MC13XXX_IRQSTAT0_CHGDETI
#define MC13783_IRQMASK0_CHGOVM		MC13783_IRQSTAT0_CHGOVI
#define MC13XXX_IRQMASK0_CHGREVM	MC13XXX_IRQSTAT0_CHGREVI
#define MC13XXX_IRQMASK0_CHGSHORTM	MC13XXX_IRQSTAT0_CHGSHORTI
#define MC13XXX_IRQMASK0_CCCVM		MC13XXX_IRQSTAT0_CCCVI
#define MC13XXX_IRQMASK0_CHGCURRM	MC13XXX_IRQSTAT0_CHGCURRI
#define MC13XXX_IRQMASK0_BPONM		MC13XXX_IRQSTAT0_BPONI
#define MC13XXX_IRQMASK0_LOBATLM	MC13XXX_IRQSTAT0_LOBATLI
#define MC13XXX_IRQMASK0_LOBATHM	MC13XXX_IRQSTAT0_LOBATHI
#define MC13783_IRQMASK0_UDPM		MC13783_IRQSTAT0_UDPI
#define MC13783_IRQMASK0_USBM		MC13783_IRQSTAT0_USBI
#define MC13783_IRQMASK0_IDM		MC13783_IRQSTAT0_IDI
#define MC13783_IRQMASK0_SE1M		MC13783_IRQSTAT0_SE1I
#define MC13783_IRQMASK0_CKDETM		MC13783_IRQSTAT0_CKDETI
#define MC13783_IRQMASK0_UDMM		MC13783_IRQSTAT0_UDMI

#define MC13XXX_IRQSTAT1	3
#define MC13XXX_IRQSTAT1_1HZI		(1 << 0)
#define MC13XXX_IRQSTAT1_TODAI		(1 << 1)
#define MC13783_IRQSTAT1_ONOFD1I	(1 << 3)
#define MC13783_IRQSTAT1_ONOFD2I	(1 << 4)
#define MC13783_IRQSTAT1_ONOFD3I	(1 << 5)
#define MC13XXX_IRQSTAT1_SYSRSTI	(1 << 6)
#define MC13XXX_IRQSTAT1_RTCRSTI	(1 << 7)
#define MC13XXX_IRQSTAT1_PCI		(1 << 8)
#define MC13XXX_IRQSTAT1_WARMI		(1 << 9)
#define MC13XXX_IRQSTAT1_MEMHLDI	(1 << 10)
#define MC13783_IRQSTAT1_PWRRDYI	(1 << 11)
#define MC13XXX_IRQSTAT1_THWARNLI	(1 << 12)
#define MC13XXX_IRQSTAT1_THWARNHI	(1 << 13)
#define MC13XXX_IRQSTAT1_CLKI		(1 << 14)
#define MC13783_IRQSTAT1_SEMAFI		(1 << 15)
#define MC13783_IRQSTAT1_MC2BI		(1 << 17)
#define MC13783_IRQSTAT1_HSDETI		(1 << 18)
#define MC13783_IRQSTAT1_HSLI		(1 << 19)
#define MC13783_IRQSTAT1_ALSPTHI	(1 << 20)
#define MC13783_IRQSTAT1_AHSSHORTI	(1 << 21)

#define MC13XXX_IRQMASK1	4
#define MC13XXX_IRQMASK1_1HZM		MC13XXX_IRQSTAT1_1HZI
#define MC13XXX_IRQMASK1_TODAM		MC13XXX_IRQSTAT1_TODAI
#define MC13783_IRQMASK1_ONOFD1M	MC13783_IRQSTAT1_ONOFD1I
#define MC13783_IRQMASK1_ONOFD2M	MC13783_IRQSTAT1_ONOFD2I
#define MC13783_IRQMASK1_ONOFD3M	MC13783_IRQSTAT1_ONOFD3I
#define MC13XXX_IRQMASK1_SYSRSTM	MC13XXX_IRQSTAT1_SYSRSTI
#define MC13XXX_IRQMASK1_RTCRSTM	MC13XXX_IRQSTAT1_RTCRSTI
#define MC13XXX_IRQMASK1_PCM		MC13XXX_IRQSTAT1_PCI
#define MC13XXX_IRQMASK1_WARMM		MC13XXX_IRQSTAT1_WARMI
#define MC13XXX_IRQMASK1_MEMHLDM	MC13XXX_IRQSTAT1_MEMHLDI
#define MC13783_IRQMASK1_PWRRDYM	MC13783_IRQSTAT1_PWRRDYI
#define MC13XXX_IRQMASK1_THWARNLM	MC13XXX_IRQSTAT1_THWARNLI
#define MC13XXX_IRQMASK1_THWARNHM	MC13XXX_IRQSTAT1_THWARNHI
#define MC13XXX_IRQMASK1_CLKM		MC13XXX_IRQSTAT1_CLKI
#define MC13783_IRQMASK1_SEMAFM		MC13783_IRQSTAT1_SEMAFI
#define MC13783_IRQMASK1_MC2BM		MC13783_IRQSTAT1_MC2BI
#define MC13783_IRQMASK1_HSDETM		MC13783_IRQSTAT1_HSDETI
#define MC13783_IRQMASK1_HSLM		MC13783_IRQSTAT1_HSLI
#define MC13783_IRQMASK1_ALSPTHM	MC13783_IRQSTAT1_ALSPTHI
#define MC13783_IRQMASK1_AHSSHORTM	MC13783_IRQSTAT1_AHSSHORTI

#define MC13XXX_REVISION	7
#define MC13XXX_REVISION_REVMETAL	(0x07 <<  0)
#define MC13XXX_REVISION_REVFULL	(0x03 <<  3)
#define MC13XXX_REVISION_ICID		(0x07 <<  6)
#define MC13XXX_REVISION_FIN		(0x03 <<  9)
#define MC13XXX_REVISION_FAB		(0x03 << 11)
#define MC13XXX_REVISION_ICIDCODE	(0x3f << 13)

#define MC13XXX_ADC1		44
#define MC13XXX_ADC1_ADEN		(1 << 0)
#define MC13XXX_ADC1_RAND		(1 << 1)
#define MC13XXX_ADC1_ADSEL		(1 << 3)
#define MC13XXX_ADC1_ASC		(1 << 20)
#define MC13XXX_ADC1_ADTRIGIGN		(1 << 21)

#define MC13XXX_ADC2		45

void mc13xxx_lock(struct mc13xxx *mc13xxx)
{
	if (!mutex_trylock(&mc13xxx->lock)) {
		dev_dbg(mc13xxx->dev, "wait for %s from %pf\n",
				__func__, __builtin_return_address(0));

		mutex_lock(&mc13xxx->lock);
	}
	dev_dbg(mc13xxx->dev, "%s from %pf\n",
			__func__, __builtin_return_address(0));
}
EXPORT_SYMBOL(mc13xxx_lock);

void mc13xxx_unlock(struct mc13xxx *mc13xxx)
{
	dev_dbg(mc13xxx->dev, "%s from %pf\n",
			__func__, __builtin_return_address(0));
	mutex_unlock(&mc13xxx->lock);
}
EXPORT_SYMBOL(mc13xxx_unlock);

int mc13xxx_reg_read(struct mc13xxx *mc13xxx, unsigned int offset, u32 *val)
{
	int ret;

	BUG_ON(!mutex_is_locked(&mc13xxx->lock));

	if (offset > MC13XXX_NUMREGS)
		return -EINVAL;

	ret = regmap_read(mc13xxx->regmap, offset, val);
	dev_vdbg(mc13xxx->dev, "[0x%02x] -> 0x%06x\n", offset, *val);

	return ret;
}
EXPORT_SYMBOL(mc13xxx_reg_read);

int mc13xxx_reg_write(struct mc13xxx *mc13xxx, unsigned int offset, u32 val)
{
	BUG_ON(!mutex_is_locked(&mc13xxx->lock));

	dev_vdbg(mc13xxx->dev, "[0x%02x] <- 0x%06x\n", offset, val);

	if (offset > MC13XXX_NUMREGS || val > 0xffffff)
		return -EINVAL;

	return regmap_write(mc13xxx->regmap, offset, val);
}
EXPORT_SYMBOL(mc13xxx_reg_write);

int mc13xxx_reg_rmw(struct mc13xxx *mc13xxx, unsigned int offset,
		u32 mask, u32 val)
{
	BUG_ON(!mutex_is_locked(&mc13xxx->lock));
	BUG_ON(val & ~mask);
	dev_vdbg(mc13xxx->dev, "[0x%02x] <- 0x%06x (mask: 0x%06x)\n",
			offset, val, mask);

	return regmap_update_bits(mc13xxx->regmap, offset, mask, val);
}
EXPORT_SYMBOL(mc13xxx_reg_rmw);

int mc13xxx_irq_mask(struct mc13xxx *mc13xxx, int irq)
{
	int ret;
	unsigned int offmask = irq < 24 ? MC13XXX_IRQMASK0 : MC13XXX_IRQMASK1;
	u32 irqbit = 1 << (irq < 24 ? irq : irq - 24);
	u32 mask;

	if (irq < 0 || irq >= MC13XXX_NUM_IRQ)
		return -EINVAL;

	ret = mc13xxx_reg_read(mc13xxx, offmask, &mask);
	if (ret)
		return ret;

	if (mask & irqbit)
		/* already masked */
		return 0;

	return mc13xxx_reg_write(mc13xxx, offmask, mask | irqbit);
}
EXPORT_SYMBOL(mc13xxx_irq_mask);

int mc13xxx_irq_unmask(struct mc13xxx *mc13xxx, int irq)
{
	int ret;
	unsigned int offmask = irq < 24 ? MC13XXX_IRQMASK0 : MC13XXX_IRQMASK1;
	u32 irqbit = 1 << (irq < 24 ? irq : irq - 24);
	u32 mask;

	if (irq < 0 || irq >= MC13XXX_NUM_IRQ)
		return -EINVAL;

	ret = mc13xxx_reg_read(mc13xxx, offmask, &mask);
	if (ret)
		return ret;

	if (!(mask & irqbit))
		/* already unmasked */
		return 0;

	return mc13xxx_reg_write(mc13xxx, offmask, mask & ~irqbit);
}
EXPORT_SYMBOL(mc13xxx_irq_unmask);

int mc13xxx_irq_status(struct mc13xxx *mc13xxx, int irq,
		int *enabled, int *pending)
{
	int ret;
	unsigned int offmask = irq < 24 ? MC13XXX_IRQMASK0 : MC13XXX_IRQMASK1;
	unsigned int offstat = irq < 24 ? MC13XXX_IRQSTAT0 : MC13XXX_IRQSTAT1;
	u32 irqbit = 1 << (irq < 24 ? irq : irq - 24);

	if (irq < 0 || irq >= MC13XXX_NUM_IRQ)
		return -EINVAL;

	if (enabled) {
		u32 mask;

		ret = mc13xxx_reg_read(mc13xxx, offmask, &mask);
		if (ret)
			return ret;

		*enabled = mask & irqbit;
	}

	if (pending) {
		u32 stat;

		ret = mc13xxx_reg_read(mc13xxx, offstat, &stat);
		if (ret)
			return ret;

		*pending = stat & irqbit;
	}

	return 0;
}
EXPORT_SYMBOL(mc13xxx_irq_status);

int mc13xxx_irq_ack(struct mc13xxx *mc13xxx, int irq)
{
	unsigned int offstat = irq < 24 ? MC13XXX_IRQSTAT0 : MC13XXX_IRQSTAT1;
	unsigned int val = 1 << (irq < 24 ? irq : irq - 24);

	BUG_ON(irq < 0 || irq >= MC13XXX_NUM_IRQ);

	return mc13xxx_reg_write(mc13xxx, offstat, val);
}
EXPORT_SYMBOL(mc13xxx_irq_ack);

int mc13xxx_irq_request_nounmask(struct mc13xxx *mc13xxx, int irq,
		irq_handler_t handler, const char *name, void *dev)
{
	BUG_ON(!mutex_is_locked(&mc13xxx->lock));
	BUG_ON(!handler);

	if (irq < 0 || irq >= MC13XXX_NUM_IRQ)
		return -EINVAL;

	if (mc13xxx->irqhandler[irq])
		return -EBUSY;

	mc13xxx->irqhandler[irq] = handler;
	mc13xxx->irqdata[irq] = dev;

	return 0;
}
EXPORT_SYMBOL(mc13xxx_irq_request_nounmask);

int mc13xxx_irq_request(struct mc13xxx *mc13xxx, int irq,
		irq_handler_t handler, const char *name, void *dev)
{
	int ret;

	ret = mc13xxx_irq_request_nounmask(mc13xxx, irq, handler, name, dev);
	if (ret)
		return ret;

	ret = mc13xxx_irq_unmask(mc13xxx, irq);
	if (ret) {
		mc13xxx->irqhandler[irq] = NULL;
		mc13xxx->irqdata[irq] = NULL;
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(mc13xxx_irq_request);

int mc13xxx_irq_free(struct mc13xxx *mc13xxx, int irq, void *dev)
{
	int ret;
	BUG_ON(!mutex_is_locked(&mc13xxx->lock));

	if (irq < 0 || irq >= MC13XXX_NUM_IRQ || !mc13xxx->irqhandler[irq] ||
			mc13xxx->irqdata[irq] != dev)
		return -EINVAL;

	ret = mc13xxx_irq_mask(mc13xxx, irq);
	if (ret)
		return ret;

	mc13xxx->irqhandler[irq] = NULL;
	mc13xxx->irqdata[irq] = NULL;

	return 0;
}
EXPORT_SYMBOL(mc13xxx_irq_free);

static inline irqreturn_t mc13xxx_irqhandler(struct mc13xxx *mc13xxx, int irq)
{
	return mc13xxx->irqhandler[irq](irq, mc13xxx->irqdata[irq]);
}

/*
 * returns: number of handled irqs or negative error
 * locking: holds mc13xxx->lock
 */
static int mc13xxx_irq_handle(struct mc13xxx *mc13xxx,
		unsigned int offstat, unsigned int offmask, int baseirq)
{
	u32 stat, mask;
	int ret = mc13xxx_reg_read(mc13xxx, offstat, &stat);
	int num_handled = 0;

	if (ret)
		return ret;

	ret = mc13xxx_reg_read(mc13xxx, offmask, &mask);
	if (ret)
		return ret;

	while (stat & ~mask) {
		int irq = __ffs(stat & ~mask);

		stat &= ~(1 << irq);

		if (likely(mc13xxx->irqhandler[baseirq + irq])) {
			irqreturn_t handled;

			handled = mc13xxx_irqhandler(mc13xxx, baseirq + irq);
			if (handled == IRQ_HANDLED)
				num_handled++;
		} else {
			dev_err(mc13xxx->dev,
					"BUG: irq %u but no handler\n",
					baseirq + irq);

			mask |= 1 << irq;

			ret = mc13xxx_reg_write(mc13xxx, offmask, mask);
		}
	}

	return num_handled;
}

static irqreturn_t mc13xxx_irq_thread(int irq, void *data)
{
	struct mc13xxx *mc13xxx = data;
	irqreturn_t ret;
	int handled = 0;

	mc13xxx_lock(mc13xxx);

	ret = mc13xxx_irq_handle(mc13xxx, MC13XXX_IRQSTAT0,
			MC13XXX_IRQMASK0, 0);
	if (ret > 0)
		handled = 1;

	ret = mc13xxx_irq_handle(mc13xxx, MC13XXX_IRQSTAT1,
			MC13XXX_IRQMASK1, 24);
	if (ret > 0)
		handled = 1;

	mc13xxx_unlock(mc13xxx);

	return IRQ_RETVAL(handled);
}

static const char *mc13xxx_chipname[] = {
	[MC13XXX_ID_MC13783] = "mc13783",
	[MC13XXX_ID_MC13892] = "mc13892",
};

#define maskval(reg, mask)	(((reg) & (mask)) >> __ffs(mask))
static int mc13xxx_identify(struct mc13xxx *mc13xxx)
{
	u32 icid;
	u32 revision;
	int ret;

	/*
	 * Get the generation ID from register 46, as apparently some older
	 * IC revisions only have this info at this location. Newer ICs seem to
	 * have both.
	 */
	ret = mc13xxx_reg_read(mc13xxx, 46, &icid);
	if (ret)
		return ret;

	icid = (icid >> 6) & 0x7;

	switch (icid) {
	case 2:
		mc13xxx->ictype = MC13XXX_ID_MC13783;
		break;
	case 7:
		mc13xxx->ictype = MC13XXX_ID_MC13892;
		break;
	default:
		mc13xxx->ictype = MC13XXX_ID_INVALID;
		break;
	}

	if (mc13xxx->ictype == MC13XXX_ID_MC13783 ||
			mc13xxx->ictype == MC13XXX_ID_MC13892) {
		ret = mc13xxx_reg_read(mc13xxx, MC13XXX_REVISION, &revision);

		dev_info(mc13xxx->dev, "%s: rev: %d.%d, "
				"fin: %d, fab: %d, icid: %d/%d\n",
				mc13xxx_chipname[mc13xxx->ictype],
				maskval(revision, MC13XXX_REVISION_REVFULL),
				maskval(revision, MC13XXX_REVISION_REVMETAL),
				maskval(revision, MC13XXX_REVISION_FIN),
				maskval(revision, MC13XXX_REVISION_FAB),
				maskval(revision, MC13XXX_REVISION_ICID),
				maskval(revision, MC13XXX_REVISION_ICIDCODE));
	}

	return (mc13xxx->ictype == MC13XXX_ID_INVALID) ? -ENODEV : 0;
}

static const char *mc13xxx_get_chipname(struct mc13xxx *mc13xxx)
{
	return mc13xxx_chipname[mc13xxx->ictype];
}

int mc13xxx_get_flags(struct mc13xxx *mc13xxx)
{
	return mc13xxx->flags;
}
EXPORT_SYMBOL(mc13xxx_get_flags);

#define MC13XXX_ADC1_CHAN0_SHIFT	5
#define MC13XXX_ADC1_CHAN1_SHIFT	8
#define MC13783_ADC1_ATO_SHIFT		11
#define MC13783_ADC1_ATOX		(1 << 19)

struct mc13xxx_adcdone_data {
	struct mc13xxx *mc13xxx;
	struct completion done;
};

static irqreturn_t mc13xxx_handler_adcdone(int irq, void *data)
{
	struct mc13xxx_adcdone_data *adcdone_data = data;

	mc13xxx_irq_ack(adcdone_data->mc13xxx, irq);

	complete_all(&adcdone_data->done);

	return IRQ_HANDLED;
}

#define MC13XXX_ADC_WORKING (1 << 0)

int mc13xxx_adc_do_conversion(struct mc13xxx *mc13xxx, unsigned int mode,
		unsigned int channel, u8 ato, bool atox,
		unsigned int *sample)
{
	u32 adc0, adc1, old_adc0;
	int i, ret;
	struct mc13xxx_adcdone_data adcdone_data = {
		.mc13xxx = mc13xxx,
	};
	init_completion(&adcdone_data.done);

	dev_dbg(mc13xxx->dev, "%s\n", __func__);

	mc13xxx_lock(mc13xxx);

	if (mc13xxx->adcflags & MC13XXX_ADC_WORKING) {
		ret = -EBUSY;
		goto out;
	}

	mc13xxx->adcflags |= MC13XXX_ADC_WORKING;

	mc13xxx_reg_read(mc13xxx, MC13XXX_ADC0, &old_adc0);

	adc0 = MC13XXX_ADC0_ADINC1 | MC13XXX_ADC0_ADINC2;
	adc1 = MC13XXX_ADC1_ADEN | MC13XXX_ADC1_ADTRIGIGN | MC13XXX_ADC1_ASC;

	if (channel > 7)
		adc1 |= MC13XXX_ADC1_ADSEL;

	switch (mode) {
	case MC13XXX_ADC_MODE_TS:
		adc0 |= MC13XXX_ADC0_ADREFEN | MC13XXX_ADC0_TSMOD0 |
			MC13XXX_ADC0_TSMOD1;
		adc1 |= 4 << MC13XXX_ADC1_CHAN1_SHIFT;
		break;

	case MC13XXX_ADC_MODE_SINGLE_CHAN:
		adc0 |= old_adc0 & MC13XXX_ADC0_CONFIG_MASK;
		adc1 |= (channel & 0x7) << MC13XXX_ADC1_CHAN0_SHIFT;
		adc1 |= MC13XXX_ADC1_RAND;
		break;

	case MC13XXX_ADC_MODE_MULT_CHAN:
		adc0 |= old_adc0 & MC13XXX_ADC0_CONFIG_MASK;
		adc1 |= 4 << MC13XXX_ADC1_CHAN1_SHIFT;
		break;

	default:
		mc13xxx_unlock(mc13xxx);
		return -EINVAL;
	}

	adc1 |= ato << MC13783_ADC1_ATO_SHIFT;
	if (atox)
		adc1 |= MC13783_ADC1_ATOX;

	dev_dbg(mc13xxx->dev, "%s: request irq\n", __func__);
	mc13xxx_irq_request(mc13xxx, MC13XXX_IRQ_ADCDONE,
			mc13xxx_handler_adcdone, __func__, &adcdone_data);
	mc13xxx_irq_ack(mc13xxx, MC13XXX_IRQ_ADCDONE);

	mc13xxx_reg_write(mc13xxx, MC13XXX_ADC0, adc0);
	mc13xxx_reg_write(mc13xxx, MC13XXX_ADC1, adc1);

	mc13xxx_unlock(mc13xxx);

	ret = wait_for_completion_interruptible_timeout(&adcdone_data.done, HZ);

	if (!ret)
		ret = -ETIMEDOUT;

	mc13xxx_lock(mc13xxx);

	mc13xxx_irq_free(mc13xxx, MC13XXX_IRQ_ADCDONE, &adcdone_data);

	if (ret > 0)
		for (i = 0; i < 4; ++i) {
			ret = mc13xxx_reg_read(mc13xxx,
					MC13XXX_ADC2, &sample[i]);
			if (ret)
				break;
		}

	if (mode == MC13XXX_ADC_MODE_TS)
		/* restore TSMOD */
		mc13xxx_reg_write(mc13xxx, MC13XXX_ADC0, old_adc0);

	mc13xxx->adcflags &= ~MC13XXX_ADC_WORKING;
out:
	mc13xxx_unlock(mc13xxx);

	return ret;
}
EXPORT_SYMBOL_GPL(mc13xxx_adc_do_conversion);

static int mc13xxx_add_subdevice_pdata(struct mc13xxx *mc13xxx,
		const char *format, void *pdata, size_t pdata_size)
{
	char buf[30];
	const char *name = mc13xxx_get_chipname(mc13xxx);

	struct mfd_cell cell = {
		.platform_data = pdata,
		.pdata_size = pdata_size,
	};

	/* there is no asnprintf in the kernel :-( */
	if (snprintf(buf, sizeof(buf), format, name) > sizeof(buf))
		return -E2BIG;

	cell.name = kmemdup(buf, strlen(buf) + 1, GFP_KERNEL);
	if (!cell.name)
		return -ENOMEM;

	return mfd_add_devices(mc13xxx->dev, -1, &cell, 1, NULL, 0);
}

static int mc13xxx_add_subdevice(struct mc13xxx *mc13xxx, const char *format)
{
	return mc13xxx_add_subdevice_pdata(mc13xxx, format, NULL, 0);
}

#ifdef CONFIG_OF
static int mc13xxx_probe_flags_dt(struct mc13xxx *mc13xxx)
{
	struct device_node *np = mc13xxx->dev->of_node;

	if (!np)
		return -ENODEV;

	if (of_get_property(np, "fsl,mc13xxx-uses-adc", NULL))
		mc13xxx->flags |= MC13XXX_USE_ADC;

	if (of_get_property(np, "fsl,mc13xxx-uses-codec", NULL))
		mc13xxx->flags |= MC13XXX_USE_CODEC;

	if (of_get_property(np, "fsl,mc13xxx-uses-rtc", NULL))
		mc13xxx->flags |= MC13XXX_USE_RTC;

	if (of_get_property(np, "fsl,mc13xxx-uses-touch", NULL))
		mc13xxx->flags |= MC13XXX_USE_TOUCHSCREEN;

	return 0;
}
#else
static inline int mc13xxx_probe_flags_dt(struct mc13xxx *mc13xxx)
{
	return -ENODEV;
}
#endif

int mc13xxx_common_init(struct mc13xxx *mc13xxx,
		struct mc13xxx_platform_data *pdata, int irq)
{
	int ret;

	mc13xxx_lock(mc13xxx);

	ret = mc13xxx_identify(mc13xxx);
	if (ret)
		goto err_revision;

	/* mask all irqs */
	ret = mc13xxx_reg_write(mc13xxx, MC13XXX_IRQMASK0, 0x00ffffff);
	if (ret)
		goto err_mask;

	ret = mc13xxx_reg_write(mc13xxx, MC13XXX_IRQMASK1, 0x00ffffff);
	if (ret)
		goto err_mask;

	ret = request_threaded_irq(irq, NULL, mc13xxx_irq_thread,
			IRQF_ONESHOT | IRQF_TRIGGER_HIGH, "mc13xxx", mc13xxx);

	if (ret) {
err_mask:
err_revision:
		mc13xxx_unlock(mc13xxx);
		kfree(mc13xxx);
		return ret;
	}

	mc13xxx->irq = irq;

	mc13xxx_unlock(mc13xxx);

	if (mc13xxx_probe_flags_dt(mc13xxx) < 0 && pdata)
		mc13xxx->flags = pdata->flags;

	if (mc13xxx->flags & MC13XXX_USE_ADC)
		mc13xxx_add_subdevice(mc13xxx, "%s-adc");

	if (mc13xxx->flags & MC13XXX_USE_CODEC)
		mc13xxx_add_subdevice_pdata(mc13xxx, "%s-codec",
					pdata->codec, sizeof(*pdata->codec));

	if (mc13xxx->flags & MC13XXX_USE_RTC)
		mc13xxx_add_subdevice(mc13xxx, "%s-rtc");

	if (mc13xxx->flags & MC13XXX_USE_TOUCHSCREEN)
		mc13xxx_add_subdevice_pdata(mc13xxx, "%s-ts",
				&pdata->touch, sizeof(pdata->touch));

	if (pdata) {
		mc13xxx_add_subdevice_pdata(mc13xxx, "%s-regulator",
			&pdata->regulators, sizeof(pdata->regulators));
		mc13xxx_add_subdevice_pdata(mc13xxx, "%s-led",
				pdata->leds, sizeof(*pdata->leds));
		mc13xxx_add_subdevice_pdata(mc13xxx, "%s-pwrbutton",
				pdata->buttons, sizeof(*pdata->buttons));
	} else {
		mc13xxx_add_subdevice(mc13xxx, "%s-regulator");
		mc13xxx_add_subdevice(mc13xxx, "%s-led");
		mc13xxx_add_subdevice(mc13xxx, "%s-pwrbutton");
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mc13xxx_common_init);

void mc13xxx_common_cleanup(struct mc13xxx *mc13xxx)
{
	free_irq(mc13xxx->irq, mc13xxx);

	mfd_remove_devices(mc13xxx->dev);

	regmap_exit(mc13xxx->regmap);

	kfree(mc13xxx);
}
EXPORT_SYMBOL_GPL(mc13xxx_common_cleanup);

MODULE_DESCRIPTION("Core driver for Freescale MC13XXX PMIC");
MODULE_AUTHOR("Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>");
MODULE_LICENSE("GPL v2");
