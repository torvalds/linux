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
#include <linux/slab.h>

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

	memset(&t, 0, sizeof(t));
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

static void pcap_mask_irq(struct irq_data *d)
{
	struct pcap_chip *pcap = irq_data_get_irq_chip_data(d);

	pcap->msr |= 1 << irq_to_pcap(pcap, d->irq);
	queue_work(pcap->workqueue, &pcap->msr_work);
}

static void pcap_unmask_irq(struct irq_data *d)
{
	struct pcap_chip *pcap = irq_data_get_irq_chip_data(d);

	pcap->msr &= ~(1 << irq_to_pcap(pcap, d->irq));
	queue_work(pcap->workqueue, &pcap->msr_work);
}

static struct irq_chip pcap_irq_chip = {
	.name		= "pcap",
	.irq_disable	= pcap_mask_irq,
	.irq_mask	= pcap_mask_irq,
	.irq_unmask	= pcap_unmask_irq,
};

static void pcap_msr_work(struct work_struct *work)
{
	struct pcap_chip *pcap = container_of(work, struct pcap_chip, msr_work);

	ezx_pcap_write(pcap, PCAP_REG_MSR, pcap->msr);
}

static void pcap_isr_work(struct work_struct *work)
{
	struct pcap_chip *pcap = container_of(work, struct pcap_chip, isr_work);
	struct pcap_platform_data *pdata = dev_get_platdata(&pcap->spi->dev);
	u32 msr, isr, int_sel, service;
	int irq;

	do {
		ezx_pcap_read(pcap, PCAP_REG_MSR, &msr);
		ezx_pcap_read(pcap, PCAP_REG_ISR, &isr);

		/* We can't service/ack irqs that are assigned to port 2 */
		if (!(pdata->config & PCAP_SECOND_PORT)) {
			ezx_pcap_read(pcap, PCAP_REG_INT_SEL, &int_sel);
			isr &= ~int_sel;
		}

		ezx_pcap_write(pcap, PCAP_REG_MSR, isr | msr);
		ezx_pcap_write(pcap, PCAP_REG_ISR, isr);

		local_irq_disable();
		service = isr & ~msr;
		for (irq = pcap->irq_base; service; service >>= 1, irq++) {
			if (service & 1)
				generic_handle_irq(irq);
		}
		local_irq_enable();
		ezx_pcap_write(pcap, PCAP_REG_MSR, pcap->msr);
	} while (gpio_get_value(pdata->gpio));
}

static void pcap_irq_handler(struct irq_desc *desc)
{
	struct pcap_chip *pcap = irq_desc_get_handler_data(desc);

	desc->irq_data.chip->irq_ack(&desc->irq_data);
	queue_work(pcap->workqueue, &pcap->isr_work);
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

	if (WARN(!req, "adc irq without pending request\n")) {
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

static int pcap_add_subdev(struct pcap_chip *pcap,
						struct pcap_subdev *subdev)
{
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_alloc(subdev->name, subdev->id);
	if (!pdev)
		return -ENOMEM;

	pdev->dev.parent = &pcap->spi->dev;
	pdev->dev.platform_data = subdev->platform_data;

	ret = platform_device_add(pdev);
	if (ret)
		platform_device_put(pdev);

	return ret;
}

static int ezx_pcap_remove(struct spi_device *spi)
{
	struct pcap_chip *pcap = spi_get_drvdata(spi);
	int i;

	/* remove all registered subdevs */
	device_for_each_child(&spi->dev, NULL, pcap_remove_subdev);

	/* cleanup ADC */
	mutex_lock(&pcap->adc_mutex);
	for (i = 0; i < PCAP_ADC_MAXQ; i++)
		kfree(pcap->adc_queue[i]);
	mutex_unlock(&pcap->adc_mutex);

	/* cleanup irqchip */
	for (i = pcap->irq_base; i < (pcap->irq_base + PCAP_NIRQS); i++)
		irq_set_chip_and_handler(i, NULL, NULL);

	destroy_workqueue(pcap->workqueue);

	return 0;
}

static int ezx_pcap_probe(struct spi_device *spi)
{
	struct pcap_platform_data *pdata = dev_get_platdata(&spi->dev);
	struct pcap_chip *pcap;
	int i, adc_irq;
	int ret = -ENODEV;

	/* platform data is required */
	if (!pdata)
		goto ret;

	pcap = devm_kzalloc(&spi->dev, sizeof(*pcap), GFP_KERNEL);
	if (!pcap) {
		ret = -ENOMEM;
		goto ret;
	}

	mutex_init(&pcap->io_mutex);
	mutex_init(&pcap->adc_mutex);
	INIT_WORK(&pcap->isr_work, pcap_isr_work);
	INIT_WORK(&pcap->msr_work, pcap_msr_work);
	spi_set_drvdata(spi, pcap);

	/* setup spi */
	spi->bits_per_word = 32;
	spi->mode = SPI_MODE_0 | (pdata->config & PCAP_CS_AH ? SPI_CS_HIGH : 0);
	ret = spi_setup(spi);
	if (ret)
		goto ret;

	pcap->spi = spi;

	/* setup irq */
	pcap->irq_base = pdata->irq_base;
	pcap->workqueue = create_singlethread_workqueue("pcapd");
	if (!pcap->workqueue) {
		ret = -ENOMEM;
		dev_err(&spi->dev, "can't create pcap thread\n");
		goto ret;
	}

	/* redirect interrupts to AP, except adcdone2 */
	if (!(pdata->config & PCAP_SECOND_PORT))
		ezx_pcap_write(pcap, PCAP_REG_INT_SEL,
					(1 << PCAP_IRQ_ADCDONE2));

	/* setup irq chip */
	for (i = pcap->irq_base; i < (pcap->irq_base + PCAP_NIRQS); i++) {
		irq_set_chip_and_handler(i, &pcap_irq_chip, handle_simple_irq);
		irq_set_chip_data(i, pcap);
		irq_clear_status_flags(i, IRQ_NOREQUEST | IRQ_NOPROBE);
	}

	/* mask/ack all PCAP interrupts */
	ezx_pcap_write(pcap, PCAP_REG_MSR, PCAP_MASK_ALL_INTERRUPT);
	ezx_pcap_write(pcap, PCAP_REG_ISR, PCAP_CLEAR_INTERRUPT_REGISTER);
	pcap->msr = PCAP_MASK_ALL_INTERRUPT;

	irq_set_irq_type(spi->irq, IRQ_TYPE_EDGE_RISING);
	irq_set_chained_handler_and_data(spi->irq, pcap_irq_handler, pcap);
	irq_set_irq_wake(spi->irq, 1);

	/* ADC */
	adc_irq = pcap_to_irq(pcap, (pdata->config & PCAP_SECOND_PORT) ?
					PCAP_IRQ_ADCDONE2 : PCAP_IRQ_ADCDONE);

	ret = devm_request_irq(&spi->dev, adc_irq, pcap_adc_irq, 0, "ADC",
				pcap);
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
free_irqchip:
	for (i = pcap->irq_base; i < (pcap->irq_base + PCAP_NIRQS); i++)
		irq_set_chip_and_handler(i, NULL, NULL);
/* destroy_workqueue: */
	destroy_workqueue(pcap->workqueue);
ret:
	return ret;
}

static struct spi_driver ezxpcap_driver = {
	.probe	= ezx_pcap_probe,
	.remove = ezx_pcap_remove,
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
