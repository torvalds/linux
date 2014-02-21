/*
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#define DRV_NAME			"altera_ilc"
#define	CTRL_REG			0x80
#define FREQ_REG			0x84
#define	STP_REG				0x88
#define VLD_REG				0x8C
#define	ILC_MAX_PORTS		32
#define ILC_FIFO_DEFAULT	32
#define ILC_ENABLE			0x01
#define	CHAR_SIZE			10
#define POLL_INTERVAL		1
#define GET_PORT_COUNT(_val)		((_val & 0x7C) >> 2)
#define GET_VLD_BIT(_val, _offset)	(((_val) >> _offset) & 0x1)

struct altera_ilc {
	struct platform_device	*pdev;
	void __iomem			*regs;
	unsigned int			port_count;
	unsigned int			irq;
	unsigned int			channel_offset;
	unsigned int			interrupt_channels[ILC_MAX_PORTS];
	struct kfifo			kfifos[ILC_MAX_PORTS];
	struct device_attribute dev_attr[ILC_MAX_PORTS];
	struct delayed_work     ilc_work;
	char					sysfs[ILC_MAX_PORTS][CHAR_SIZE];
	u32						fifo_depth;
};

static int ilc_irq_lookup(struct altera_ilc *ilc, int irq)
{
	int i;
	for (i = 0; i < ilc->port_count; i++) {
		if (irq == platform_get_irq(ilc->pdev, i))
			return i;
	}
	return -EPERM;
}

static ssize_t ilc_show_counter(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret, i, id, fifo_len;
	unsigned int fifo_buf[ILC_MAX_PORTS];
	char	temp[10];
	struct  altera_ilc *ilc = dev_get_drvdata(dev);

	fifo_len = 0;
	ret = kstrtouint(attr->attr.name, 0, &id);

	for (i = 0; i < ilc->port_count; i++) {
		if (id == (ilc->interrupt_channels[i])) {
			/*Check for kfifo length*/
			fifo_len = kfifo_len(&ilc->kfifos[i])
				/sizeof(unsigned int);
			if (fifo_len <= 0) {
				dev_info(&ilc->pdev->dev, "Fifo for interrupt %s is empty\n",
					attr->attr.name);
			return 0;
			}
			/*Read from kfifo*/
			ret = kfifo_out(&ilc->kfifos[i], &fifo_buf,
				kfifo_len(&ilc->kfifos[i]));
		}
	}

	for (i = 0; i < fifo_len; i++) {
		sprintf(temp, "%u\n", fifo_buf[i]);
		strcat(buf, temp);
	}

	strcat(buf, "\0");

	return strlen(buf);
}

static struct attribute *altera_ilc_attrs[ILC_MAX_PORTS];

struct attribute_group altera_ilc_attr_group = {
	.name = "ilc_data",
	.attrs = altera_ilc_attrs,
};

static void ilc_work(struct work_struct *work)
{
	unsigned int ilc_value, ret, offset, stp_reg;
	struct altera_ilc *ilc =
		container_of(work, struct altera_ilc, ilc_work.work);

	offset = ilc_irq_lookup(ilc, ilc->irq);
	if (offset < 0) {
		dev_err(&ilc->pdev->dev, "Unable to lookup irq number\n");
		return;
	}

	if (GET_VLD_BIT(readl(ilc->regs + VLD_REG), offset)) {
		/*Read counter register*/
		ilc_value = readl(ilc->regs + (offset) * 4);

		/*Putting value into kfifo*/
		ret = kfifo_in((&ilc->kfifos[offset]),
			(unsigned int *)&ilc_value, sizeof(ilc_value));

		/*Clearing stop register*/
		stp_reg = readl(ilc->regs + STP_REG);
		writel((!(0x1 << offset))&stp_reg, ilc->regs + STP_REG);

		return;
	}

	/*Start workqueue to poll data valid*/
	schedule_delayed_work(&ilc->ilc_work, msecs_to_jiffies(POLL_INTERVAL));
}

static irqreturn_t ilc_interrupt_handler(int irq, void *p)
{
	unsigned int offset, stp_reg;

	struct altera_ilc *ilc = (struct altera_ilc *)p;

	/*Update ILC struct*/
	ilc->irq = irq;

	dev_dbg(&ilc->pdev->dev, "Interrupt %u triggered\n",
		ilc->irq);

	offset = ilc_irq_lookup(ilc, irq);
	if (offset < 0) {
		dev_err(&ilc->pdev->dev, "Unable to lookup irq number\n");
		return IRQ_RETVAL(IRQ_NONE);
	}

	/*Setting stop register*/
	stp_reg = readl(ilc->regs + STP_REG);
	writel((0x1 << offset)|stp_reg, ilc->regs + STP_REG);

	/*Start workqueue to poll data valid*/
	schedule_delayed_work(&ilc->ilc_work, 0);

	return IRQ_RETVAL(IRQ_NONE);
}

static int altera_ilc_probe(struct platform_device *pdev)
{
	struct	altera_ilc	*ilc;
	struct	resource	*regs;
	struct	device_node *np = pdev->dev.of_node;
	int		ret, i;

	ilc = devm_kzalloc(&pdev->dev, sizeof(struct altera_ilc),
		GFP_KERNEL);
	if (!ilc)
		return -ENOMEM;

	ilc->pdev = pdev;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;

	ilc->regs = devm_request_and_ioremap(&pdev->dev, regs);
	if (!ilc->regs)
		return -EADDRNOTAVAIL;

	ilc->port_count = GET_PORT_COUNT(readl(ilc->regs + CTRL_REG));
	if (ilc->port_count <= 0) {
		dev_warn(&pdev->dev, "No interrupt connected to ILC\n");
		return -EPERM;
	}

	/*Check for fifo depth*/
	ret = of_property_read_u32(np, "altr,sw-fifo-depth",
		&(ilc->fifo_depth));
	if (ret) {
		dev_warn(&pdev->dev, "Fifo depth undefined\n");
		dev_warn(&pdev->dev, "Setting fifo depth to default value (32)\n");
		ilc->fifo_depth = ILC_FIFO_DEFAULT;
	}

	/*Initialize Kfifo*/
	for (i = 0; i < ilc->port_count; i++) {
		ret = kfifo_alloc(&ilc->kfifos[i], (ilc->fifo_depth *
			sizeof(unsigned int)), GFP_KERNEL);
		if (ret) {
			dev_err(&pdev->dev, "Kfifo failed to initialize\n");
			return ret;
		}
	}

	/*Register each of the IRQs*/
	for (i = 0; i < ilc->port_count; i++) {
		ilc->interrupt_channels[i] = platform_get_irq(pdev, i);

		ret = devm_request_irq(&pdev->dev, (ilc->interrupt_channels[i]),
			ilc_interrupt_handler, IRQF_SHARED, "ilc_0",
			(void *)(ilc));

		if (ret < 0)
			dev_warn(&pdev->dev, "Failed to register interrupt handler");
	}

	/*Setup sysfs interface*/
	for (i = 0; (i < ilc->port_count); i++) {
		sprintf(ilc->sysfs[i], "%d", (ilc->interrupt_channels[i]));
		ilc->dev_attr[i].attr.name = ilc->sysfs[i];
		ilc->dev_attr[i].attr.mode = S_IRUGO;
		ilc->dev_attr[i].show = ilc_show_counter;
		altera_ilc_attrs[i] = &ilc->dev_attr[i].attr;
		altera_ilc_attrs[i+1] = NULL;
	}
	ret = sysfs_create_group(&pdev->dev.kobj, &altera_ilc_attr_group);

	/*Initialize workqueue*/
	INIT_DELAYED_WORK(&ilc->ilc_work, ilc_work);

	/*Global enable ILC softIP*/
	writel(ILC_ENABLE, ilc->regs + CTRL_REG);

	platform_set_drvdata(pdev, ilc);

	dev_info(&pdev->dev, "Driver successfully loaded\n");

	return 0;
}

static int altera_ilc_remove(struct platform_device *pdev)
{
	int i;
	struct altera_ilc *ilc = platform_get_drvdata(pdev);

	/*Remove sysfs interface*/
	sysfs_remove_group(&pdev->dev.kobj, &altera_ilc_attr_group);

	/*Free up kfifo memory*/
	for (i = 0; i < ilc->port_count; i++)
		kfifo_free(&ilc->kfifos[i]);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id altera_ilc_match[] = {
	{ .compatible = "altr,ilc-1.0" },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, altera_ilc_match);

static struct platform_driver altera_ilc_platform_driver = {
	.driver = {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(altera_ilc_match),
	},
	.remove			= altera_ilc_remove,
};

static int __init altera_ilc_init(void)
{
	return platform_driver_probe(&altera_ilc_platform_driver,
		altera_ilc_probe);
}

static void __exit altera_ilc_exit(void)
{
	platform_driver_unregister(&altera_ilc_platform_driver);
}

module_init(altera_ilc_init);
module_exit(altera_ilc_exit);

MODULE_AUTHOR("Chee Nouk Phoon <cnphoon@altera.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Altera Interrupt Latency Counter Driver");
MODULE_ALIAS("platform:" DRV_NAME);
