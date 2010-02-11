/*
 * Driver for Motorola PCAP2 as present in EZX phones
 *
 * Copyright (C) 2006 Harald Welte <laforge@openezx.org>
 * Copyright (C) 2009 Daniel Ribeiro <drwyrm@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/ezx-pcap.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>

#define PCAP_ADC_MAXQ		8
struct pcap_adc_request {
	u8 bank;
	u8 ch[2];
	u32 flags;
	void (*callback)(void *, u16[]);
	void *data;
};

struct pcap_adc_sync_request {
	u16 res[2];
	struct completion completion;
};

struct pcap_chip {
	struct spi_device *spi;

	/* IO */
	u32 buf;
	struct mutex io_mutex;

	/* IRQ */
	unsigned int irq_base;
	u32 msr;
	struct work_struct isr_work;
	struct work_struct msr_work;
	struct workqueue_struct *workqueue;

	/* ADC */
	struct pcap_adc_request *adc_queue[PCAP_ADC_MAXQ];
	u8 adc_head;
	u8 adc_tail;
	struct mutex adc_mutex;
};

/* IO */
static int ezx_pcap_putget(struct pcap_chip *pcap, u32 *data)
{
	struct spi_transfer t;
	struct spi_message m;
	int status;

	memset(&t, 0, sizeof t);
	spi_message_init(&m);
	t.len = sizeof(u32);
	spi_message_add_tail(&t, &m);

	pcap->buf = *data;
	t.tx_buf = (u8 *) &pcap->buf;
	t.rx_buf = (u8 *) &pcap->buf;
	status = spi_sync(pcap->spi, &m);

	if (status == 0)
		*data = pcap->buf;

	return status;
}

int ezx_pcap_write(struct pcap_chip *pcap, u8 reg_num, u32 value)
{
	int ret;

	mutex_lock(&pcap->io_mutex);
	value &= PCAP_REGISTER_VALUE_MASK;
	value |= PCAP_REGISTER_WRITE_OP_BIT
		| (reg_num << PCAP_REGISTER_ADDRESS_SHIFT);
	ret = ezx_pcap_putget(pcap, &value);
	mutex_unlock(&pcap->io_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(ezx_pcap_write);

int ezx_pcap_read(struct pcap_chip *pcap, u8 reg_num, u32 *value)
{
	int ret;

	mutex_lock(&pcap->io_mutex);
	*value = PCAP_REGISTER_READ_OP_BIT
		| (reg_num << PCAP_REGISTER_ADDRESS_SHIFT);

	ret = ezx_pcap_putget(pcap, value);
	mutex_unlock(&pcap->io_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(ezx_pcap_read);

int ezx_pcap_set_bits(struct pcap_chip *pcap, u8 reg_num, u32 mask, u32 val)
{
	int ret;
	u32 tmp = PCAP_REGISTER_READ_OP_BIT |
		(reg_num << PCAP_REGISTER_ADDRESS_SHIFT);

	mutex_lock(&pcap->io_mutex);
	ret = ezx_pcap_putget(pcap, &tmp);
	if (ret)
		goto out_unlock;

	tmp &= (PCAP_REGISTER_VALUE_MASK & ~mask);
	tmp |= (val & mask) | PCAP_REGISTER_WRITE_OP_BIT |
		(reg_num << PCAP_REGISTER_ADDRESS_SHIFT);

	ret = ezx_pcap_putget(pcap, &tmp);
out_unlock:
	mutex_unlock(&pcap->io_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(ezx_pcap_set_bits);

/* IRQ */
int irq_to_pcap(struct pcap_chip *pcap, int irq)
{
	return irq - pcap->irq_base;
}
EXPORT_SYMBOL_GPL(irq_to_pcap);

int pcap_to_irq(struct pcap_chip *pcap, int irq)
{
	return pcap->irq_base + irq;
}
EXPORT_SYMBOL_GPL(pcap_to_irq);

static void pcap_mask_irq(unsigned int irq)
{
	struct pcap_chip *pcap = get_irq_chip_data(irq);

	pcap->msr |= 1 << irq_to_pcap(pcap, irq);
	queue_work(pcap->workqueue, &pcap->msr_work);
}

static void pcap_unmask_irq(unsigned int irq)
{
	struct pcap_chip *pcap = get_irq_chip_data(irq);

	pcap->msr &= ~(1 << irq_to_pcap(pcap, irq));
	queue_work(pcap->workqueue, &pcap->msr_work);
}

static struct irq_chip pcap_irq_chip = {
	.name	= "pcap",
	.mask	= pcap_mask_irq,
	.unmask	= pcap_unmask_irq,
};

static void pcap_msr_work(struct work_struct *work)
{
	struct pcap_chip *pcap = container_of(work, struct pcap_chip, msr_work);

	ezx_pcap_write(pcap, PCAP_REG_MSR, pcap->msr);
}

static void pcap_isr_work(struct work_struct *work)
{
	struct pcap_chip *pcap = container_of(work, struct pcap_chip, isr_work);
	struct pcap_platform_data *pdata = pcap->spi->dev.platform_data;
	u32 msr, isr, int_sel, service;
	int irq;

	do {
		ezx_pcap_read(pcap, PCAP_REG_MSR, &msr);
		ezx_pcap_read(pcap, PCAP_REG_ISR, &isr);

		/* We cant service/ack irqs that are assigned to port 2 */
		if (!(pdata->config & PCAP_SECOND_PORT)) {
			ezx_pcap_read(pcap, PCAP_REG_INT_SEL, &int_sel);
			isr &= ~int_sel;
		}

		ezx_pcap_write(pcap, PCAP_REG_MSR, isr | msr);
		ezx_pcap_write(pcap, PCAP_REG_ISR, isr);

		local_irq_disable();
		service = isr & ~msr;
		for (irq = pcap->irq_base; service; service >>= 1, irq++) {
			if (service & 1) {
				struct irq_desc *desc = irq_to_desc(irq);

				if (WARN(!desc, KERN_WARNING
						"Invalid PCAP IRQ %d\n", irq))
					break;

				if (desc->status & IRQ_DISABLED)
					note_interrupt(irq, desc, IRQ_NONE);
				else
					desc->handle_irq(irq, desc);
			}
		}
		local_irq_enable();
		ezx_pcap_write(pcap, PCAP_REG_MSR, pcap->msr);
	} while (gpio_get_value(irq_to_gpio(pcap->spi->irq)));
}

static void pcap_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct pcap_chip *pcap = get_irq_data(irq);

	desc->chip->ack(irq);
	queue_work(pcap->workqueue, &pcap->isr_work);
	return;
}

/* ADC */
void pcap_set_ts_bits(struct pcap_chip *pcap, u32 bits)
{
	u32 tmp;

	mutex_lock(&pcap->adc_mutex);
	ezx_pcap_read(pcap, PCAP_REG_ADC, &tmp);
	tmp &= ~(PCAP_ADC_TS_M_MASK | PCAP_ADC_TS_REF_LOWPWR);
	tmp |= bits & (PCAP_ADC_TS_M_MASK | PCAP_ADC_TS_REF_LOWPWR);
	ezx_pcap_write(pcap, PCAP_REG_ADC, tmp);
	mutex_unlock(&pcap->adc_mutex);
}
EXPORT_SYMBOL_GPL(pcap_set_ts_bits);

static void pcap_disable_adc(struct pcap_chip *pcap)
{
	u32 tmp;

	ezx_pcap_read(pcap, PCAP_REG_ADC, &tmp);
	tmp &= ~(PCAP_ADC_ADEN|PCAP_ADC_BATT_I_ADC|PCAP_ADC_BATT_I_POLARITY);
	ezx_pcap_write(pcap, PCAP_REG_ADC, tmp);
}

static void pcap_adc_trigger(struct pcap_chip *pcap)
{
	u32 tmp;
	u8 head;

	mutex_lock(&pcap->adc_mutex);
	head = pcap->adc_head;
	if (!pcap->adc_queue[head]) {
		/* queue is empty, save power */
		pcap_disable_adc(pcap);
		mutex_unlock(&pcap->adc_mutex);
		return;
	}
	/* start conversion on requested bank, save TS_M bits */
	ezx_pcap_read(pcap, PCAP_REG_ADC, &tmp);
	tmp &= (PCAP_ADC_TS_M_MASK | PCAP_ADC_TS_REF_LOWPWR);
	tmp |= pcap->adc_queue[head]->flags | PCAP_ADC_ADEN;

	if (pcap->adc_queue[head]->bank == PCAP_ADC_BANK_1)
		tmp |= PCAP_ADC_AD_SEL1;

	ezx_pcap_write(pcap, PCAP_REG_ADC, tmp);
	mutex_unlock(&pcap->adc_mutex);
	ezx_pcap_write(pcap, PCAP_REG_ADR, PCAP_ADR_ASC);
}

static irqreturn_t pcap_adc_irq(int irq, void *_pcap)
{
	struct pcap_chip *pcap = _pcap;
	struct pcap_adc_request *req;
	u16 res[2];
	u32 tmp;

	mutex_lock(&pcap->adc_mutex);
	req = pcap->adc_queue[pcap->adc_head];

	if (WARN(!req, KERN_WARNING "adc irq without pending request\n")) {
		mutex_unlock(&pcap->adc_mutex);
		return IRQ_HANDLED;
	}

	/* read requested channels results */
	ezx_pcap_read(pcap, PCAP_REG_ADC, &tmp);
	tmp &= ~(PCAP_ADC_ADA1_MASK | PCAP_ADC_ADA2_MASK);
	tmp |= (req->ch[0] << PCAP_ADC_ADA1_SHIFT);
	tmp |= (req->ch[1] << PCAP_ADC_ADA2_SHIFT);
	ezx_pcap_write(pcap, PCAP_REG_ADC, tmp);
	ezx_pcap_read(pcap, PCAP_REG_ADR, &tmp);
	res[0] = (tmp & PCAP_ADR_ADD1_MASK) >> PCAP_ADR_ADD1_SHIFT;
	res[1] = (tmp & PCAP_ADR_ADD2_MASK) >> PCAP_ADR_ADD2_SHIFT;

	pcap->adc_queue[pcap->adc_head] = NULL;
	pcap->adc_head = (pcap->adc_head + 1) & (PCAP_ADC_MAXQ - 1);
	mutex_unlock(&pcap->adc_mutex);

	/* pass the results and release memory */
	req->callback(req->data, res);
	kfree(req);

	/* trigger next conversion (if any) on queue */
	pcap_adc_trigger(pcap);

	return IRQ_HANDLED;
}

int pcap_adc_async(struct pcap_chip *pcap, u8 bank, u32 flags, u8 ch[],
						void *callback, void *data)
{
	struct pcap_adc_request *req;

	/* This will be freed after we have a result */
	req = kmalloc(sizeof(struct pcap_adc_request), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->bank = bank;
	req->flags = flags;
	req->ch[0] = ch[0];
	req->ch[1] = ch[1];
	req->callback = callback;
	req->data = data;

	mutex_lock(&pcap->adc_mutex);
	if (pcap->adc_queue[pcap->adc_tail]) {
		mutex_unlock(&pcap->adc_mutex);
		kfree(req);
		return -EBUSY;
	}
	pcap->adc_queue[pcap->adc_tail] = req;
	pcap->adc_tail = (pcap->adc_tail + 1) & (PCAP_ADC_MAXQ - 1);
	mutex_unlock(&pcap->adc_mutex);

	/* start conversion */
	pcap_adc_trigger(pcap);

	return 0;
}
EXPORT_SYMBOL_GPL(pcap_adc_async);

static void pcap_adc_sync_cb(void *param, u16 res[])
{
	struct pcap_adc_sync_request *req = param;

	req->res[0] = res[0];
	req->res[1] = res[1];
	complete(&req->completion);
}

int pcap_adc_sync(struct pcap_chip *pcap, u8 bank, u32 flags, u8 ch[],
								u16 res[])
{
	struct pcap_adc_sync_request sync_data;
	int ret;

	init_completion(&sync_data.completion);
	ret = pcap_adc_async(pcap, bank, flags, ch, pcap_adc_sync_cb,
								&sync_data);
	if (ret)
		return ret;
	wait_for_completion(&sync_data.completion);
	res[0] = sync_data.res[0];
	res[1] = sync_data.res[1];

	return 0;
}
EXPORT_SYMBOL_GPL(pcap_adc_sync);

/* subdevs */
static int pcap_remove_subdev(struct device *dev, void *unused)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int __devinit pcap_add_subdev(struct pcap_chip *pcap,
						struct pcap_subdev *subdev)
{
	struct platform_device *pdev;

	pdev = platform_device_alloc(subdev->name, subdev->id);
	pdev->dev.parent = &pcap->spi->dev;
	pdev->dev.platform_data = subdev->platform_data;

	return platform_device_add(pdev);
}

static int __devexit ezx_pcap_remove(struct spi_device *spi)
{
	struct pcap_chip *pcap = dev_get_drvdata(&spi->dev);
	struct pcap_platform_data *pdata = spi->dev.platform_data;
	int i, adc_irq;

	/* remove all registered subdevs */
	device_for_each_child(&spi->dev, NULL, pcap_remove_subdev);

	/* cleanup ADC */
	adc_irq = pcap_to_irq(pcap, (pdata->config & PCAP_SECOND_PORT) ?
				PCAP_IRQ_ADCDONE2 : PCAP_IRQ_ADCDONE);
	free_irq(adc_irq, pcap);
	mutex_lock(&pcap->adc_mutex);
	for (i = 0; i < PCAP_ADC_MAXQ; i++)
		kfree(pcap->adc_queue[i]);
	mutex_unlock(&pcap->adc_mutex);

	/* cleanup irqchip */
	for (i = pcap->irq_base; i < (pcap->irq_base + PCAP_NIRQS); i++)
		set_irq_chip_and_handler(i, NULL, NULL);

	destroy_workqueue(pcap->workqueue);

	kfree(pcap);

	return 0;
}

static int __devinit ezx_pcap_probe(struct spi_device *spi)
{
	struct pcap_platform_data *pdata = spi->dev.platform_data;
	struct pcap_chip *pcap;
	int i, adc_irq;
	int ret = -ENODEV;

	/* platform data is required */
	if (!pdata)
		goto ret;

	pcap = kzalloc(sizeof(*pcap), GFP_KERNEL);
	if (!pcap) {
		ret = -ENOMEM;
		goto ret;
	}

	mutex_init(&pcap->io_mutex);
	mutex_init(&pcap->adc_mutex);
	INIT_WORK(&pcap->isr_work, pcap_isr_work);
	INIT_WORK(&pcap->msr_work, pcap_msr_work);
	dev_set_drvdata(&spi->dev, pcap);

	/* setup spi */
	spi->bits_per_word = 32;
	spi->mode = SPI_MODE_0 | (pdata->config & PCAP_CS_AH ? SPI_CS_HIGH : 0);
	ret = spi_setup(spi);
	if (ret)
		goto free_pcap;

	pcap->spi = spi;

	/* setup irq */
	pcap->irq_base = pdata->irq_base;
	pcap->workqueue = create_singlethread_workqueue("pcapd");
	if (!pcap->workqueue) {
		dev_err(&spi->dev, "cant create pcap thread\n");
		goto free_pcap;
	}

	/* redirect interrupts to AP, except adcdone2 */
	if (!(pdata->config & PCAP_SECOND_PORT))
		ezx_pcap_write(pcap, PCAP_REG_INT_SEL,
					(1 << PCAP_IRQ_ADCDONE2));

	/* setup irq chip */
	for (i = pcap->irq_base; i < (pcap->irq_base + PCAP_NIRQS); i++) {
		set_irq_chip_and_handler(i, &pcap_irq_chip, handle_simple_irq);
		set_irq_chip_data(i, pcap);
#ifdef CONFIG_ARM
		set_irq_flags(i, IRQF_VALID);
#else
		set_irq_noprobe(i);
#endif
	}

	/* mask/ack all PCAP interrupts */
	ezx_pcap_write(pcap, PCAP_REG_MSR, PCAP_MASK_ALL_INTERRUPT);
	ezx_pcap_write(pcap, PCAP_REG_ISR, PCAP_CLEAR_INTERRUPT_REGISTER);
	pcap->msr = PCAP_MASK_ALL_INTERRUPT;

	set_irq_type(spi->irq, IRQ_TYPE_EDGE_RISING);
	set_irq_data(spi->irq, pcap);
	set_irq_chained_handler(spi->irq, pcap_irq_handler);
	set_irq_wake(spi->irq, 1);

	/* ADC */
	adc_irq = pcap_to_irq(pcap, (pdata->config & PCAP_SECOND_PORT) ?
					PCAP_IRQ_ADCDONE2 : PCAP_IRQ_ADCDONE);

	ret = request_irq(adc_irq, pcap_adc_irq, 0, "ADC", pcap);
	if (ret)
		goto free_irqchip;

	/* setup subdevs */
	for (i = 0; i < pdata->num_subdevs; i++) {
		ret = pcap_add_subdev(pcap, &pdata->subdevs[i]);
		if (ret)
			goto remove_subdevs;
	}

	/* board specific quirks */
	if (pdata->init)
		pdata->init(pcap);

	return 0;

remove_subdevs:
	device_for_each_child(&spi->dev, NULL, pcap_remove_subdev);
/* free_adc: */
	free_irq(adc_irq, pcap);
free_irqchip:
	for (i = pcap->irq_base; i < (pcap->irq_base + PCAP_NIRQS); i++)
		set_irq_chip_and_handler(i, NULL, NULL);
/* destroy_workqueue: */
	destroy_workqueue(pcap->workqueue);
free_pcap:
	kfree(pcap);
ret:
	return ret;
}

static struct spi_driver ezxpcap_driver = {
	.probe	= ezx_pcap_probe,
	.remove = __devexit_p(ezx_pcap_remove),
	.driver = {
		.name	= "ezx-pcap",
		.owner	= THIS_MODULE,
	},
};

static int __init ezx_pcap_init(void)
{
	return spi_register_driver(&ezxpcap_driver);
}

static void __exit ezx_pcap_exit(void)
{
	spi_unregister_driver(&ezxpcap_driver);
}

subsys_initcall(ezx_pcap_init);
module_exit(ezx_pcap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Ribeiro / Harald Welte");
MODULE_DESCRIPTION("Motorola PCAP2 ASIC Driver");
MODULE_ALIAS("spi:ezx-pcap");
