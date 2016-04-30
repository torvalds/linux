/*
 * SPI Link Layer for ST NCI based Driver
 * Copyright (C) 2014-2015 STMicroelectronics SAS. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/nfc.h>
#include <net/nfc/nci.h>
#include <linux/platform_data/st-nci.h>

#include "st-nci.h"

#define DRIVER_DESC "NCI NFC driver for ST_NCI"

/* ndlc header */
#define ST_NCI_FRAME_HEADROOM	1
#define ST_NCI_FRAME_TAILROOM	0

#define ST_NCI_SPI_MIN_SIZE 4   /* PCB(1) + NCI Packet header(3) */
#define ST_NCI_SPI_MAX_SIZE 250 /* req 4.2.1 */

#define ST_NCI_SPI_DRIVER_NAME "st_nci_spi"

#define ST_NCI_GPIO_NAME_RESET "reset"

struct st_nci_spi_phy {
	struct spi_device *spi_dev;
	struct llt_ndlc *ndlc;

	bool irq_active;

	unsigned int gpio_reset;
	unsigned int irq_polarity;

	struct st_nci_se_status se_status;
};

static int st_nci_spi_enable(void *phy_id)
{
	struct st_nci_spi_phy *phy = phy_id;

	gpio_set_value(phy->gpio_reset, 0);
	usleep_range(10000, 15000);
	gpio_set_value(phy->gpio_reset, 1);
	usleep_range(80000, 85000);

	if (phy->ndlc->powered == 0 && phy->irq_active == 0) {
		enable_irq(phy->spi_dev->irq);
		phy->irq_active = true;
	}

	return 0;
}

static void st_nci_spi_disable(void *phy_id)
{
	struct st_nci_spi_phy *phy = phy_id;

	disable_irq_nosync(phy->spi_dev->irq);
	phy->irq_active = false;
}

/*
 * Writing a frame must not return the number of written bytes.
 * It must return either zero for success, or <0 for error.
 * In addition, it must not alter the skb
 */
static int st_nci_spi_write(void *phy_id, struct sk_buff *skb)
{
	int r;
	struct st_nci_spi_phy *phy = phy_id;
	struct spi_device *dev = phy->spi_dev;
	struct sk_buff *skb_rx;
	u8 buf[ST_NCI_SPI_MAX_SIZE + NCI_DATA_HDR_SIZE +
	       ST_NCI_FRAME_HEADROOM + ST_NCI_FRAME_TAILROOM];
	struct spi_transfer spi_xfer = {
		.tx_buf = skb->data,
		.rx_buf = buf,
		.len = skb->len,
	};

	if (phy->ndlc->hard_fault != 0)
		return phy->ndlc->hard_fault;

	r = spi_sync_transfer(dev, &spi_xfer, 1);
	/*
	 * We may have received some valuable data on miso line.
	 * Send them back in the ndlc state machine.
	 */
	if (!r) {
		skb_rx = alloc_skb(skb->len, GFP_KERNEL);
		if (!skb_rx) {
			r = -ENOMEM;
			goto exit;
		}

		skb_put(skb_rx, skb->len);
		memcpy(skb_rx->data, buf, skb->len);
		ndlc_recv(phy->ndlc, skb_rx);
	}

exit:
	return r;
}

/*
 * Reads an ndlc frame and returns it in a newly allocated sk_buff.
 * returns:
 * 0 : if received frame is complete
 * -EREMOTEIO : i2c read error (fatal)
 * -EBADMSG : frame was incorrect and discarded
 * -ENOMEM : cannot allocate skb, frame dropped
 */
static int st_nci_spi_read(struct st_nci_spi_phy *phy,
			struct sk_buff **skb)
{
	int r;
	u8 len;
	u8 buf[ST_NCI_SPI_MAX_SIZE];
	struct spi_device *dev = phy->spi_dev;
	struct spi_transfer spi_xfer = {
		.rx_buf = buf,
		.len = ST_NCI_SPI_MIN_SIZE,
	};

	r = spi_sync_transfer(dev, &spi_xfer, 1);
	if (r < 0)
		return -EREMOTEIO;

	len = be16_to_cpu(*(__be16 *) (buf + 2));
	if (len > ST_NCI_SPI_MAX_SIZE) {
		nfc_err(&dev->dev, "invalid frame len\n");
		phy->ndlc->hard_fault = 1;
		return -EBADMSG;
	}

	*skb = alloc_skb(ST_NCI_SPI_MIN_SIZE + len, GFP_KERNEL);
	if (*skb == NULL)
		return -ENOMEM;

	skb_reserve(*skb, ST_NCI_SPI_MIN_SIZE);
	skb_put(*skb, ST_NCI_SPI_MIN_SIZE);
	memcpy((*skb)->data, buf, ST_NCI_SPI_MIN_SIZE);

	if (!len)
		return 0;

	spi_xfer.len = len;
	r = spi_sync_transfer(dev, &spi_xfer, 1);
	if (r < 0) {
		kfree_skb(*skb);
		return -EREMOTEIO;
	}

	skb_put(*skb, len);
	memcpy((*skb)->data + ST_NCI_SPI_MIN_SIZE, buf, len);

	return 0;
}

/*
 * Reads an ndlc frame from the chip.
 *
 * On ST21NFCB, IRQ goes in idle state when read starts.
 */
static irqreturn_t st_nci_irq_thread_fn(int irq, void *phy_id)
{
	struct st_nci_spi_phy *phy = phy_id;
	struct spi_device *dev;
	struct sk_buff *skb = NULL;
	int r;

	if (!phy || !phy->ndlc || irq != phy->spi_dev->irq) {
		WARN_ON_ONCE(1);
		return IRQ_NONE;
	}

	dev = phy->spi_dev;
	dev_dbg(&dev->dev, "IRQ\n");

	if (phy->ndlc->hard_fault)
		return IRQ_HANDLED;

	if (!phy->ndlc->powered) {
		st_nci_spi_disable(phy);
		return IRQ_HANDLED;
	}

	r = st_nci_spi_read(phy, &skb);
	if (r == -EREMOTEIO || r == -ENOMEM || r == -EBADMSG)
		return IRQ_HANDLED;

	ndlc_recv(phy->ndlc, skb);

	return IRQ_HANDLED;
}

static struct nfc_phy_ops spi_phy_ops = {
	.write = st_nci_spi_write,
	.enable = st_nci_spi_enable,
	.disable = st_nci_spi_disable,
};

static int st_nci_spi_acpi_request_resources(struct spi_device *spi_dev)
{
	struct st_nci_spi_phy *phy = spi_get_drvdata(spi_dev);
	const struct acpi_device_id *id;
	struct gpio_desc *gpiod_reset;
	struct device *dev;

	if (!spi_dev)
		return -EINVAL;

	dev = &spi_dev->dev;

	/* Match the struct device against a given list of ACPI IDs */
	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return -ENODEV;

	/* Get RESET GPIO from ACPI */
	gpiod_reset = devm_gpiod_get_index(dev, ST_NCI_GPIO_NAME_RESET, 1,
					   GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod_reset)) {
		nfc_err(dev, "Unable to get RESET GPIO\n");
		return -ENODEV;
	}

	phy->gpio_reset = desc_to_gpio(gpiod_reset);

	phy->irq_polarity = irq_get_trigger_type(spi_dev->irq);

	phy->se_status.is_ese_present =
				device_property_present(dev, "ese-present");
	phy->se_status.is_uicc_present =
				device_property_present(dev, "uicc-present");

	return 0;
}

static int st_nci_spi_of_request_resources(struct spi_device *dev)
{
	struct st_nci_spi_phy *phy = spi_get_drvdata(dev);
	struct device_node *pp;
	int gpio;
	int r;

	pp = dev->dev.of_node;
	if (!pp)
		return -ENODEV;

	/* Get GPIO from device tree */
	gpio = of_get_named_gpio(pp, "reset-gpios", 0);
	if (gpio < 0) {
		nfc_err(&dev->dev,
			"Failed to retrieve reset-gpios from device tree\n");
		return gpio;
	}

	/* GPIO request and configuration */
	r = devm_gpio_request_one(&dev->dev, gpio,
				GPIOF_OUT_INIT_HIGH, ST_NCI_GPIO_NAME_RESET);
	if (r) {
		nfc_err(&dev->dev, "Failed to request reset pin\n");
		return r;
	}
	phy->gpio_reset = gpio;

	phy->irq_polarity = irq_get_trigger_type(dev->irq);

	phy->se_status.is_ese_present =
				of_property_read_bool(pp, "ese-present");
	phy->se_status.is_uicc_present =
				of_property_read_bool(pp, "uicc-present");

	return 0;
}

static int st_nci_spi_request_resources(struct spi_device *dev)
{
	struct st_nci_nfc_platform_data *pdata;
	struct st_nci_spi_phy *phy = spi_get_drvdata(dev);
	int r;

	pdata = dev->dev.platform_data;
	if (pdata == NULL) {
		nfc_err(&dev->dev, "No platform data\n");
		return -EINVAL;
	}

	/* store for later use */
	phy->gpio_reset = pdata->gpio_reset;
	phy->irq_polarity = pdata->irq_polarity;

	r = devm_gpio_request_one(&dev->dev,
			phy->gpio_reset, GPIOF_OUT_INIT_HIGH,
			ST_NCI_GPIO_NAME_RESET);
	if (r) {
		pr_err("%s : reset gpio_request failed\n", __FILE__);
		return r;
	}

	phy->se_status.is_ese_present = pdata->is_ese_present;
	phy->se_status.is_uicc_present = pdata->is_uicc_present;

	return 0;
}

static int st_nci_spi_probe(struct spi_device *dev)
{
	struct st_nci_spi_phy *phy;
	struct st_nci_nfc_platform_data *pdata;
	int r;

	dev_dbg(&dev->dev, "%s\n", __func__);
	dev_dbg(&dev->dev, "IRQ: %d\n", dev->irq);

	/* Check SPI platform functionnalities */
	if (!dev) {
		pr_debug("%s: dev is NULL. Device is not accessible.\n",
			__func__);
		return -ENODEV;
	}

	phy = devm_kzalloc(&dev->dev, sizeof(struct st_nci_spi_phy),
			   GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->spi_dev = dev;

	spi_set_drvdata(dev, phy);

	pdata = dev->dev.platform_data;
	if (!pdata && dev->dev.of_node) {
		r = st_nci_spi_of_request_resources(dev);
		if (r) {
			nfc_err(&dev->dev, "No platform data\n");
			return r;
		}
	} else if (pdata) {
		r = st_nci_spi_request_resources(dev);
		if (r) {
			nfc_err(&dev->dev,
				"Cannot get platform resources\n");
			return r;
		}
	} else if (ACPI_HANDLE(&dev->dev)) {
		r = st_nci_spi_acpi_request_resources(dev);
		if (r) {
			nfc_err(&dev->dev, "Cannot get ACPI data\n");
			return r;
		}
	} else {
		nfc_err(&dev->dev,
			"st_nci platform resources not available\n");
		return -ENODEV;
	}

	r = ndlc_probe(phy, &spi_phy_ops, &dev->dev,
			ST_NCI_FRAME_HEADROOM, ST_NCI_FRAME_TAILROOM,
			&phy->ndlc, &phy->se_status);
	if (r < 0) {
		nfc_err(&dev->dev, "Unable to register ndlc layer\n");
		return r;
	}

	phy->irq_active = true;
	r = devm_request_threaded_irq(&dev->dev, dev->irq, NULL,
				st_nci_irq_thread_fn,
				phy->irq_polarity | IRQF_ONESHOT,
				ST_NCI_SPI_DRIVER_NAME, phy);
	if (r < 0)
		nfc_err(&dev->dev, "Unable to register IRQ handler\n");

	return r;
}

static int st_nci_spi_remove(struct spi_device *dev)
{
	struct st_nci_spi_phy *phy = spi_get_drvdata(dev);

	dev_dbg(&dev->dev, "%s\n", __func__);

	ndlc_remove(phy->ndlc);

	return 0;
}

static struct spi_device_id st_nci_spi_id_table[] = {
	{ST_NCI_SPI_DRIVER_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(spi, st_nci_spi_id_table);

static const struct acpi_device_id st_nci_spi_acpi_match[] = {
	{"SMO2101", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, st_nci_spi_acpi_match);

static const struct of_device_id of_st_nci_spi_match[] = {
	{ .compatible = "st,st21nfcb-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, of_st_nci_spi_match);

static struct spi_driver st_nci_spi_driver = {
	.driver = {
		.name = ST_NCI_SPI_DRIVER_NAME,
		.of_match_table = of_match_ptr(of_st_nci_spi_match),
		.acpi_match_table = ACPI_PTR(st_nci_spi_acpi_match),
	},
	.probe = st_nci_spi_probe,
	.id_table = st_nci_spi_id_table,
	.remove = st_nci_spi_remove,
};
module_spi_driver(st_nci_spi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
