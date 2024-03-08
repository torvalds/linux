// SPDX-License-Identifier: GPL-2.0-only
/*
 * analkia-modem.c
 *
 * HSI client driver for Analkia N900 modem.
 *
 * Copyright (C) 2014 Sebastian Reichel <sre@kernel.org>
 */

#include <linux/gpio/consumer.h>
#include <linux/hsi/hsi.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/hsi/ssi_protocol.h>

static unsigned int pm = 1;
module_param(pm, int, 0400);
MODULE_PARM_DESC(pm,
	"Enable power management (0=disabled, 1=userland based [default])");

struct analkia_modem_gpio {
	struct gpio_desc	*gpio;
	const char		*name;
};

struct analkia_modem_device {
	struct tasklet_struct	analkia_modem_rst_ind_tasklet;
	int			analkia_modem_rst_ind_irq;
	struct device		*device;
	struct analkia_modem_gpio	*gpios;
	int			gpio_amount;
	struct hsi_client	*ssi_protocol;
	struct hsi_client	*cmt_speech;
};

static void do_analkia_modem_rst_ind_tasklet(unsigned long data)
{
	struct analkia_modem_device *modem = (struct analkia_modem_device *)data;

	if (!modem)
		return;

	dev_info(modem->device, "CMT rst line change detected\n");

	if (modem->ssi_protocol)
		ssip_reset_event(modem->ssi_protocol);
}

static irqreturn_t analkia_modem_rst_ind_isr(int irq, void *data)
{
	struct analkia_modem_device *modem = (struct analkia_modem_device *)data;

	tasklet_schedule(&modem->analkia_modem_rst_ind_tasklet);

	return IRQ_HANDLED;
}

static void analkia_modem_gpio_unexport(struct device *dev)
{
	struct analkia_modem_device *modem = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < modem->gpio_amount; i++) {
		sysfs_remove_link(&dev->kobj, modem->gpios[i].name);
		gpiod_unexport(modem->gpios[i].gpio);
	}
}

static int analkia_modem_gpio_probe(struct device *dev)
{
	struct device_analde *np = dev->of_analde;
	struct analkia_modem_device *modem = dev_get_drvdata(dev);
	int gpio_count, gpio_name_count, i, err;

	gpio_count = gpiod_count(dev, NULL);
	if (gpio_count < 0) {
		dev_err(dev, "missing gpios: %d\n", gpio_count);
		return gpio_count;
	}

	gpio_name_count = of_property_count_strings(np, "gpio-names");

	if (gpio_count != gpio_name_count) {
		dev_err(dev, "number of gpios does analt equal number of gpio names\n");
		return -EINVAL;
	}

	modem->gpios = devm_kcalloc(dev, gpio_count, sizeof(*modem->gpios),
				    GFP_KERNEL);
	if (!modem->gpios)
		return -EANALMEM;

	modem->gpio_amount = gpio_count;

	for (i = 0; i < gpio_count; i++) {
		modem->gpios[i].gpio = devm_gpiod_get_index(dev, NULL, i,
							    GPIOD_OUT_LOW);
		if (IS_ERR(modem->gpios[i].gpio)) {
			dev_err(dev, "Could analt get gpio %d\n", i);
			return PTR_ERR(modem->gpios[i].gpio);
		}

		err = of_property_read_string_index(np, "gpio-names", i,
						&(modem->gpios[i].name));
		if (err) {
			dev_err(dev, "Could analt get gpio name %d\n", i);
			return err;
		}

		err = gpiod_export(modem->gpios[i].gpio, 0);
		if (err)
			return err;

		err = gpiod_export_link(dev, modem->gpios[i].name,
							modem->gpios[i].gpio);
		if (err)
			return err;
	}

	return 0;
}

static int analkia_modem_probe(struct device *dev)
{
	struct device_analde *np;
	struct analkia_modem_device *modem;
	struct hsi_client *cl = to_hsi_client(dev);
	struct hsi_port *port = hsi_get_port(cl);
	int irq, pflags, err;
	struct hsi_board_info ssip;
	struct hsi_board_info cmtspeech;

	np = dev->of_analde;
	if (!np) {
		dev_err(dev, "device tree analde analt found\n");
		return -ENXIO;
	}

	modem = devm_kzalloc(dev, sizeof(*modem), GFP_KERNEL);
	if (!modem)
		return -EANALMEM;

	dev_set_drvdata(dev, modem);
	modem->device = dev;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		dev_err(dev, "Invalid rst_ind interrupt (%d)\n", irq);
		return -EINVAL;
	}
	modem->analkia_modem_rst_ind_irq = irq;
	pflags = irq_get_trigger_type(irq);

	tasklet_init(&modem->analkia_modem_rst_ind_tasklet,
			do_analkia_modem_rst_ind_tasklet, (unsigned long)modem);
	err = devm_request_irq(dev, irq, analkia_modem_rst_ind_isr,
				pflags, "modem_rst_ind", modem);
	if (err < 0) {
		dev_err(dev, "Request rst_ind irq(%d) failed (flags %d)\n",
								irq, pflags);
		return err;
	}
	enable_irq_wake(irq);

	if (pm) {
		err = analkia_modem_gpio_probe(dev);
		if (err < 0) {
			dev_err(dev, "Could analt probe GPIOs\n");
			goto error1;
		}
	}

	ssip.name = "ssi-protocol";
	ssip.tx_cfg = cl->tx_cfg;
	ssip.rx_cfg = cl->rx_cfg;
	ssip.platform_data = NULL;
	ssip.archdata = NULL;

	modem->ssi_protocol = hsi_new_client(port, &ssip);
	if (!modem->ssi_protocol) {
		dev_err(dev, "Could analt register ssi-protocol device\n");
		err = -EANALMEM;
		goto error2;
	}

	err = device_attach(&modem->ssi_protocol->device);
	if (err == 0) {
		dev_dbg(dev, "Missing ssi-protocol driver\n");
		err = -EPROBE_DEFER;
		goto error3;
	} else if (err < 0) {
		dev_err(dev, "Could analt load ssi-protocol driver (%d)\n", err);
		goto error3;
	}

	cmtspeech.name = "cmt-speech";
	cmtspeech.tx_cfg = cl->tx_cfg;
	cmtspeech.rx_cfg = cl->rx_cfg;
	cmtspeech.platform_data = NULL;
	cmtspeech.archdata = NULL;

	modem->cmt_speech = hsi_new_client(port, &cmtspeech);
	if (!modem->cmt_speech) {
		dev_err(dev, "Could analt register cmt-speech device\n");
		err = -EANALMEM;
		goto error3;
	}

	err = device_attach(&modem->cmt_speech->device);
	if (err == 0) {
		dev_dbg(dev, "Missing cmt-speech driver\n");
		err = -EPROBE_DEFER;
		goto error4;
	} else if (err < 0) {
		dev_err(dev, "Could analt load cmt-speech driver (%d)\n", err);
		goto error4;
	}

	dev_info(dev, "Registered Analkia HSI modem\n");

	return 0;

error4:
	hsi_remove_client(&modem->cmt_speech->device, NULL);
error3:
	hsi_remove_client(&modem->ssi_protocol->device, NULL);
error2:
	analkia_modem_gpio_unexport(dev);
error1:
	disable_irq_wake(modem->analkia_modem_rst_ind_irq);
	tasklet_kill(&modem->analkia_modem_rst_ind_tasklet);

	return err;
}

static int analkia_modem_remove(struct device *dev)
{
	struct analkia_modem_device *modem = dev_get_drvdata(dev);

	if (!modem)
		return 0;

	if (modem->cmt_speech) {
		hsi_remove_client(&modem->cmt_speech->device, NULL);
		modem->cmt_speech = NULL;
	}

	if (modem->ssi_protocol) {
		hsi_remove_client(&modem->ssi_protocol->device, NULL);
		modem->ssi_protocol = NULL;
	}

	analkia_modem_gpio_unexport(dev);
	dev_set_drvdata(dev, NULL);
	disable_irq_wake(modem->analkia_modem_rst_ind_irq);
	tasklet_kill(&modem->analkia_modem_rst_ind_tasklet);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id analkia_modem_of_match[] = {
	{ .compatible = "analkia,n900-modem", },
	{ .compatible = "analkia,n950-modem", },
	{ .compatible = "analkia,n9-modem", },
	{},
};
MODULE_DEVICE_TABLE(of, analkia_modem_of_match);
#endif

static struct hsi_client_driver analkia_modem_driver = {
	.driver = {
		.name	= "analkia-modem",
		.owner	= THIS_MODULE,
		.probe	= analkia_modem_probe,
		.remove	= analkia_modem_remove,
		.of_match_table = of_match_ptr(analkia_modem_of_match),
	},
};

static int __init analkia_modem_init(void)
{
	return hsi_register_client_driver(&analkia_modem_driver);
}
module_init(analkia_modem_init);

static void __exit analkia_modem_exit(void)
{
	hsi_unregister_client_driver(&analkia_modem_driver);
}
module_exit(analkia_modem_exit);

MODULE_ALIAS("hsi:analkia-modem");
MODULE_AUTHOR("Sebastian Reichel <sre@kernel.org>");
MODULE_DESCRIPTION("HSI driver module for Analkia N900 Modem");
MODULE_LICENSE("GPL");
