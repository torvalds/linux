// SPDX-License-Identifier: GPL-2.0
/*
 * SGI IOC3 PS/2 controller driver for linux
 *
 * Copyright (C) 2019 Thomas Bogendoerfer <tbogendoerfer@suse.de>
 *
 * Based on code Copyright (C) 2005 Stanislaw Skowronek <skylark@unaligned.org>
 *               Copyright (C) 2009 Johannes Dickgreber <tanzy@gmx.de>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/serio.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/sn/ioc3.h>

struct ioc3kbd_data {
	struct ioc3_serioregs __iomem *regs;
	struct serio *kbd, *aux;
	bool kbd_exists, aux_exists;
	int irq;
};

static int ioc3kbd_wait(struct ioc3_serioregs __iomem *regs, u32 mask)
{
	unsigned long timeout = 0;

	while ((readl(&regs->km_csr) & mask) && (timeout < 250)) {
		udelay(50);
		timeout++;
	}
	return (timeout >= 250) ? -ETIMEDOUT : 0;
}

static int ioc3kbd_write(struct serio *dev, u8 val)
{
	struct ioc3kbd_data *d = dev->port_data;
	int ret;

	ret = ioc3kbd_wait(d->regs, KM_CSR_K_WRT_PEND);
	if (ret)
		return ret;

	writel(val, &d->regs->k_wd);

	return 0;
}

static int ioc3kbd_start(struct serio *dev)
{
	struct ioc3kbd_data *d = dev->port_data;

	d->kbd_exists = true;
	return 0;
}

static void ioc3kbd_stop(struct serio *dev)
{
	struct ioc3kbd_data *d = dev->port_data;

	d->kbd_exists = false;
}

static int ioc3aux_write(struct serio *dev, u8 val)
{
	struct ioc3kbd_data *d = dev->port_data;
	int ret;

	ret = ioc3kbd_wait(d->regs, KM_CSR_M_WRT_PEND);
	if (ret)
		return ret;

	writel(val, &d->regs->m_wd);

	return 0;
}

static int ioc3aux_start(struct serio *dev)
{
	struct ioc3kbd_data *d = dev->port_data;

	d->aux_exists = true;
	return 0;
}

static void ioc3aux_stop(struct serio *dev)
{
	struct ioc3kbd_data *d = dev->port_data;

	d->aux_exists = false;
}

static void ioc3kbd_process_data(struct serio *dev, u32 data)
{
	if (data & KM_RD_VALID_0)
		serio_interrupt(dev, (data >> KM_RD_DATA_0_SHIFT) & 0xff, 0);
	if (data & KM_RD_VALID_1)
		serio_interrupt(dev, (data >> KM_RD_DATA_1_SHIFT) & 0xff, 0);
	if (data & KM_RD_VALID_2)
		serio_interrupt(dev, (data >> KM_RD_DATA_2_SHIFT) & 0xff, 0);
}

static irqreturn_t ioc3kbd_intr(int itq, void *dev_id)
{
	struct ioc3kbd_data *d = dev_id;
	u32 data_k, data_m;

	data_k = readl(&d->regs->k_rd);
	if (d->kbd_exists)
		ioc3kbd_process_data(d->kbd, data_k);

	data_m = readl(&d->regs->m_rd);
	if (d->aux_exists)
		ioc3kbd_process_data(d->aux, data_m);

	return IRQ_HANDLED;
}

static int ioc3kbd_probe(struct platform_device *pdev)
{
	struct ioc3_serioregs __iomem *regs;
	struct device *dev = &pdev->dev;
	struct ioc3kbd_data *d;
	struct serio *sk, *sa;
	int irq, ret;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENXIO;

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	sk = kzalloc(sizeof(*sk), GFP_KERNEL);
	if (!sk)
		return -ENOMEM;

	sa = kzalloc(sizeof(*sa), GFP_KERNEL);
	if (!sa) {
		kfree(sk);
		return -ENOMEM;
	}

	sk->id.type = SERIO_8042;
	sk->write = ioc3kbd_write;
	sk->start = ioc3kbd_start;
	sk->stop = ioc3kbd_stop;
	snprintf(sk->name, sizeof(sk->name), "IOC3 keyboard %d", pdev->id);
	snprintf(sk->phys, sizeof(sk->phys), "ioc3/serio%dkbd", pdev->id);
	sk->port_data = d;
	sk->dev.parent = dev;

	sa->id.type = SERIO_8042;
	sa->write = ioc3aux_write;
	sa->start = ioc3aux_start;
	sa->stop = ioc3aux_stop;
	snprintf(sa->name, sizeof(sa->name), "IOC3 auxiliary %d", pdev->id);
	snprintf(sa->phys, sizeof(sa->phys), "ioc3/serio%daux", pdev->id);
	sa->port_data = d;
	sa->dev.parent = dev;

	d->regs = regs;
	d->kbd = sk;
	d->aux = sa;
	d->irq = irq;

	platform_set_drvdata(pdev, d);
	serio_register_port(d->kbd);
	serio_register_port(d->aux);

	ret = request_irq(irq, ioc3kbd_intr, IRQF_SHARED, "ioc3-kbd", d);
	if (ret) {
		dev_err(dev, "could not request IRQ %d\n", irq);
		serio_unregister_port(d->kbd);
		serio_unregister_port(d->aux);
		return ret;
	}

	/* enable ports */
	writel(KM_CSR_K_CLAMP_3 | KM_CSR_M_CLAMP_3, &regs->km_csr);

	return 0;
}

static void ioc3kbd_remove(struct platform_device *pdev)
{
	struct ioc3kbd_data *d = platform_get_drvdata(pdev);

	free_irq(d->irq, d);

	serio_unregister_port(d->kbd);
	serio_unregister_port(d->aux);
}

static const struct platform_device_id ioc3kbd_id_table[] = {
	{ "ioc3-kbd", },
	{ }
};
MODULE_DEVICE_TABLE(platform, ioc3kbd_id_table);

static struct platform_driver ioc3kbd_driver = {
	.probe          = ioc3kbd_probe,
	.remove         = ioc3kbd_remove,
	.id_table	= ioc3kbd_id_table,
	.driver = {
		.name = "ioc3-kbd",
	},
};
module_platform_driver(ioc3kbd_driver);

MODULE_AUTHOR("Thomas Bogendoerfer <tbogendoerfer@suse.de>");
MODULE_DESCRIPTION("SGI IOC3 serio driver");
MODULE_LICENSE("GPL");
