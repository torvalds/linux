/*
 * Motorola CPCAP PMIC USB PHY driver
 * Copyright (C) 2017 Tony Lindgren <tony@atomide.com>
 *
 * Some parts based on earlier Motorola Linux kernel tree code in
 * board-mapphone-usb.c and cpcap-usb-det.c:
 * Copyright (C) 2007 - 2011 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/gpio/consumer.h>
#include <linux/mfd/motorola-cpcap.h>
#include <linux/phy/omap_usb.h>
#include <linux/phy/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/musb.h>

/* CPCAP_REG_USBC1 register bits */
#define CPCAP_BIT_IDPULSE		BIT(15)
#define CPCAP_BIT_ID100KPU		BIT(14)
#define CPCAP_BIT_IDPUCNTRL		BIT(13)
#define CPCAP_BIT_IDPU			BIT(12)
#define CPCAP_BIT_IDPD			BIT(11)
#define CPCAP_BIT_VBUSCHRGTMR3		BIT(10)
#define CPCAP_BIT_VBUSCHRGTMR2		BIT(9)
#define CPCAP_BIT_VBUSCHRGTMR1		BIT(8)
#define CPCAP_BIT_VBUSCHRGTMR0		BIT(7)
#define CPCAP_BIT_VBUSPU		BIT(6)
#define CPCAP_BIT_VBUSPD		BIT(5)
#define CPCAP_BIT_DMPD			BIT(4)
#define CPCAP_BIT_DPPD			BIT(3)
#define CPCAP_BIT_DM1K5PU		BIT(2)
#define CPCAP_BIT_DP1K5PU		BIT(1)
#define CPCAP_BIT_DP150KPU		BIT(0)

/* CPCAP_REG_USBC2 register bits */
#define CPCAP_BIT_ZHSDRV1		BIT(15)
#define CPCAP_BIT_ZHSDRV0		BIT(14)
#define CPCAP_BIT_DPLLCLKREQ		BIT(13)
#define CPCAP_BIT_SE0CONN		BIT(12)
#define CPCAP_BIT_UARTTXTRI		BIT(11)
#define CPCAP_BIT_UARTSWAP		BIT(10)
#define CPCAP_BIT_UARTMUX1		BIT(9)
#define CPCAP_BIT_UARTMUX0		BIT(8)
#define CPCAP_BIT_ULPISTPLOW		BIT(7)
#define CPCAP_BIT_TXENPOL		BIT(6)
#define CPCAP_BIT_USBXCVREN		BIT(5)
#define CPCAP_BIT_USBCNTRL		BIT(4)
#define CPCAP_BIT_USBSUSPEND		BIT(3)
#define CPCAP_BIT_EMUMODE2		BIT(2)
#define CPCAP_BIT_EMUMODE1		BIT(1)
#define CPCAP_BIT_EMUMODE0		BIT(0)

/* CPCAP_REG_USBC3 register bits */
#define CPCAP_BIT_SPARE_898_15		BIT(15)
#define CPCAP_BIT_IHSTX03		BIT(14)
#define CPCAP_BIT_IHSTX02		BIT(13)
#define CPCAP_BIT_IHSTX01		BIT(12)
#define CPCAP_BIT_IHSTX0		BIT(11)
#define CPCAP_BIT_IDPU_SPI		BIT(10)
#define CPCAP_BIT_UNUSED_898_9		BIT(9)
#define CPCAP_BIT_VBUSSTBY_EN		BIT(8)
#define CPCAP_BIT_VBUSEN_SPI		BIT(7)
#define CPCAP_BIT_VBUSPU_SPI		BIT(6)
#define CPCAP_BIT_VBUSPD_SPI		BIT(5)
#define CPCAP_BIT_DMPD_SPI		BIT(4)
#define CPCAP_BIT_DPPD_SPI		BIT(3)
#define CPCAP_BIT_SUSPEND_SPI		BIT(2)
#define CPCAP_BIT_PU_SPI		BIT(1)
#define CPCAP_BIT_ULPI_SPI_SEL		BIT(0)

struct cpcap_usb_ints_state {
	bool id_ground;
	bool id_float;
	bool chrg_det;
	bool rvrs_chrg;
	bool vbusov;

	bool chrg_se1b;
	bool se0conn;
	bool rvrs_mode;
	bool chrgcurr1;
	bool vbusvld;
	bool sessvld;
	bool sessend;
	bool se1;

	bool battdetb;
	bool dm;
	bool dp;
};

enum cpcap_gpio_mode {
	CPCAP_DM_DP,
	CPCAP_MDM_RX_TX,
	CPCAP_UNKNOWN_DISABLED,	/* Seems to disable USB lines */
	CPCAP_OTG_DM_DP,
};

struct cpcap_phy_ddata {
	struct regmap *reg;
	struct device *dev;
	struct clk *refclk;
	struct usb_phy phy;
	struct delayed_work detect_work;
	struct pinctrl *pins;
	struct pinctrl_state *pins_ulpi;
	struct pinctrl_state *pins_utmi;
	struct pinctrl_state *pins_uart;
	struct gpio_desc *gpio[2];
	struct iio_channel *vbus;
	struct iio_channel *id;
	struct regulator *vusb;
	atomic_t active;
	unsigned int vbus_provider:1;
	unsigned int docked:1;
};

static bool cpcap_usb_vbus_valid(struct cpcap_phy_ddata *ddata)
{
	int error, value = 0;

	error = iio_read_channel_processed(ddata->vbus, &value);
	if (error >= 0)
		return value > 3900 ? true : false;

	dev_err(ddata->dev, "error reading VBUS: %i\n", error);

	return false;
}

static int cpcap_usb_phy_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	otg->host = host;
	if (!host)
		otg->state = OTG_STATE_UNDEFINED;

	return 0;
}

static int cpcap_usb_phy_set_peripheral(struct usb_otg *otg,
					struct usb_gadget *gadget)
{
	otg->gadget = gadget;
	if (!gadget)
		otg->state = OTG_STATE_UNDEFINED;

	return 0;
}

static const struct phy_ops ops = {
	.owner		= THIS_MODULE,
};

static int cpcap_phy_get_ints_state(struct cpcap_phy_ddata *ddata,
				    struct cpcap_usb_ints_state *s)
{
	int val, error;

	error = regmap_read(ddata->reg, CPCAP_REG_INTS1, &val);
	if (error)
		return error;

	s->id_ground = val & BIT(15);
	s->id_float = val & BIT(14);
	s->vbusov = val & BIT(11);

	error = regmap_read(ddata->reg, CPCAP_REG_INTS2, &val);
	if (error)
		return error;

	s->vbusvld = val & BIT(3);
	s->sessvld = val & BIT(2);
	s->sessend = val & BIT(1);
	s->se1 = val & BIT(0);

	error = regmap_read(ddata->reg, CPCAP_REG_INTS4, &val);
	if (error)
		return error;

	s->dm = val & BIT(1);
	s->dp = val & BIT(0);

	return 0;
}

static int cpcap_usb_set_uart_mode(struct cpcap_phy_ddata *ddata);
static int cpcap_usb_set_usb_mode(struct cpcap_phy_ddata *ddata);

static void cpcap_usb_try_musb_mailbox(struct cpcap_phy_ddata *ddata,
				       enum musb_vbus_id_status status)
{
	int error;

	error = musb_mailbox(status);
	if (!error)
		return;

	dev_dbg(ddata->dev, "%s: musb_mailbox failed: %i\n",
		__func__, error);
}

static void cpcap_usb_detect(struct work_struct *work)
{
	struct cpcap_phy_ddata *ddata;
	struct cpcap_usb_ints_state s;
	bool vbus = false;
	int error;

	ddata = container_of(work, struct cpcap_phy_ddata, detect_work.work);

	error = cpcap_phy_get_ints_state(ddata, &s);
	if (error)
		return;

	vbus = cpcap_usb_vbus_valid(ddata);

	/* We need to kick the VBUS as USB A-host */
	if (s.id_ground && ddata->vbus_provider) {
		dev_dbg(ddata->dev, "still in USB A-host mode, kicking VBUS\n");

		cpcap_usb_try_musb_mailbox(ddata, MUSB_ID_GROUND);

		error = regmap_update_bits(ddata->reg, CPCAP_REG_USBC3,
					   CPCAP_BIT_VBUSSTBY_EN |
					   CPCAP_BIT_VBUSEN_SPI,
					   CPCAP_BIT_VBUSEN_SPI);
		if (error)
			goto out_err;

		return;
	}

	if (vbus && s.id_ground && ddata->docked) {
		dev_dbg(ddata->dev, "still docked as A-host, signal ID down\n");

		cpcap_usb_try_musb_mailbox(ddata, MUSB_ID_GROUND);

		return;
	}

	/* No VBUS needed with docks */
	if (vbus && s.id_ground && !ddata->vbus_provider) {
		dev_dbg(ddata->dev, "connected to a dock\n");

		ddata->docked = true;

		error = cpcap_usb_set_usb_mode(ddata);
		if (error)
			goto out_err;

		cpcap_usb_try_musb_mailbox(ddata, MUSB_ID_GROUND);

		/*
		 * Force check state again after musb has reoriented,
		 * otherwise devices won't enumerate after loading PHY
		 * driver.
		 */
		schedule_delayed_work(&ddata->detect_work,
				      msecs_to_jiffies(1000));

		return;
	}

	if (s.id_ground && !ddata->docked) {
		dev_dbg(ddata->dev, "id ground, USB host mode\n");

		ddata->vbus_provider = true;

		error = cpcap_usb_set_usb_mode(ddata);
		if (error)
			goto out_err;

		cpcap_usb_try_musb_mailbox(ddata, MUSB_ID_GROUND);

		error = regmap_update_bits(ddata->reg, CPCAP_REG_USBC3,
					   CPCAP_BIT_VBUSSTBY_EN |
					   CPCAP_BIT_VBUSEN_SPI,
					   CPCAP_BIT_VBUSEN_SPI);
		if (error)
			goto out_err;

		return;
	}

	error = regmap_update_bits(ddata->reg, CPCAP_REG_USBC3,
				   CPCAP_BIT_VBUSSTBY_EN |
				   CPCAP_BIT_VBUSEN_SPI, 0);
	if (error)
		goto out_err;

	vbus = cpcap_usb_vbus_valid(ddata);

	/* Otherwise assume we're connected to a USB host */
	if (vbus) {
		dev_dbg(ddata->dev, "connected to USB host\n");
		error = cpcap_usb_set_usb_mode(ddata);
		if (error)
			goto out_err;
		cpcap_usb_try_musb_mailbox(ddata, MUSB_VBUS_VALID);

		return;
	}

	ddata->vbus_provider = false;
	ddata->docked = false;
	cpcap_usb_try_musb_mailbox(ddata, MUSB_VBUS_OFF);

	/* Default to debug UART mode */
	error = cpcap_usb_set_uart_mode(ddata);
	if (error)
		goto out_err;

	dev_dbg(ddata->dev, "set UART mode\n");

	return;

out_err:
	dev_err(ddata->dev, "error setting cable state: %i\n", error);
}

static irqreturn_t cpcap_phy_irq_thread(int irq, void *data)
{
	struct cpcap_phy_ddata *ddata = data;

	if (!atomic_read(&ddata->active))
		return IRQ_NONE;

	schedule_delayed_work(&ddata->detect_work, msecs_to_jiffies(1));

	return IRQ_HANDLED;
}

static int cpcap_usb_init_irq(struct platform_device *pdev,
			      struct cpcap_phy_ddata *ddata,
			      const char *name)
{
	int irq, error;

	irq = platform_get_irq_byname(pdev, name);
	if (irq < 0)
		return -ENODEV;

	error = devm_request_threaded_irq(ddata->dev, irq, NULL,
					  cpcap_phy_irq_thread,
					  IRQF_SHARED,
					  name, ddata);
	if (error) {
		dev_err(ddata->dev, "could not get irq %s: %i\n",
			name, error);

		return error;
	}

	return 0;
}

static const char * const cpcap_phy_irqs[] = {
	/* REG_INT_0 */
	"id_ground", "id_float",

	/* REG_INT1 */
	"se0conn", "vbusvld", "sessvld", "sessend", "se1",

	/* REG_INT_3 */
	"dm", "dp",
};

static int cpcap_usb_init_interrupts(struct platform_device *pdev,
				     struct cpcap_phy_ddata *ddata)
{
	int i, error;

	for (i = 0; i < ARRAY_SIZE(cpcap_phy_irqs); i++) {
		error = cpcap_usb_init_irq(pdev, ddata, cpcap_phy_irqs[i]);
		if (error)
			return error;
	}

	return 0;
}

/*
 * Optional pins and modes. At least Motorola mapphone devices
 * are using two GPIOs and dynamic pinctrl to multiplex PHY pins
 * to UART, ULPI or UTMI mode.
 */

static int cpcap_usb_gpio_set_mode(struct cpcap_phy_ddata *ddata,
				   enum cpcap_gpio_mode mode)
{
	if (!ddata->gpio[0] || !ddata->gpio[1])
		return 0;

	gpiod_set_value(ddata->gpio[0], mode & 1);
	gpiod_set_value(ddata->gpio[1], mode >> 1);

	return 0;
}

static int cpcap_usb_set_uart_mode(struct cpcap_phy_ddata *ddata)
{
	int error;

	/* Disable lines to prevent glitches from waking up mdm6600 */
	error = cpcap_usb_gpio_set_mode(ddata, CPCAP_UNKNOWN_DISABLED);
	if (error)
		goto out_err;

	if (ddata->pins_uart) {
		error = pinctrl_select_state(ddata->pins, ddata->pins_uart);
		if (error)
			goto out_err;
	}

	error = regmap_update_bits(ddata->reg, CPCAP_REG_USBC1,
				   CPCAP_BIT_VBUSPD,
				   CPCAP_BIT_VBUSPD);
	if (error)
		goto out_err;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_USBC2,
				   0xffff, CPCAP_BIT_UARTMUX0 |
				   CPCAP_BIT_EMUMODE0);
	if (error)
		goto out_err;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_USBC3, 0x7fff,
				   CPCAP_BIT_IDPU_SPI);
	if (error)
		goto out_err;

	/* Enable UART mode */
	error = cpcap_usb_gpio_set_mode(ddata, CPCAP_DM_DP);
	if (error)
		goto out_err;

	return 0;

out_err:
	dev_err(ddata->dev, "%s failed with %i\n", __func__, error);

	return error;
}

static int cpcap_usb_set_usb_mode(struct cpcap_phy_ddata *ddata)
{
	int error;

	/* Disable lines to prevent glitches from waking up mdm6600 */
	error = cpcap_usb_gpio_set_mode(ddata, CPCAP_UNKNOWN_DISABLED);
	if (error)
		return error;

	if (ddata->pins_utmi) {
		error = pinctrl_select_state(ddata->pins, ddata->pins_utmi);
		if (error) {
			dev_err(ddata->dev, "could not set usb mode: %i\n",
				error);

			return error;
		}
	}

	error = regmap_update_bits(ddata->reg, CPCAP_REG_USBC1,
				   CPCAP_BIT_VBUSPD, 0);
	if (error)
		goto out_err;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_USBC3,
				   CPCAP_BIT_PU_SPI |
				   CPCAP_BIT_DMPD_SPI |
				   CPCAP_BIT_DPPD_SPI |
				   CPCAP_BIT_SUSPEND_SPI |
				   CPCAP_BIT_ULPI_SPI_SEL, 0);
	if (error)
		goto out_err;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_USBC2,
				   CPCAP_BIT_USBXCVREN,
				   CPCAP_BIT_USBXCVREN);
	if (error)
		goto out_err;

	/* Enable USB mode */
	error = cpcap_usb_gpio_set_mode(ddata, CPCAP_OTG_DM_DP);
	if (error)
		goto out_err;

	return 0;

out_err:
	dev_err(ddata->dev, "%s failed with %i\n", __func__, error);

	return error;
}

static int cpcap_usb_init_optional_pins(struct cpcap_phy_ddata *ddata)
{
	ddata->pins = devm_pinctrl_get(ddata->dev);
	if (IS_ERR(ddata->pins)) {
		dev_info(ddata->dev, "default pins not configured: %ld\n",
			 PTR_ERR(ddata->pins));
		ddata->pins = NULL;

		return 0;
	}

	ddata->pins_ulpi = pinctrl_lookup_state(ddata->pins, "ulpi");
	if (IS_ERR(ddata->pins_ulpi)) {
		dev_info(ddata->dev, "ulpi pins not configured\n");
		ddata->pins_ulpi = NULL;
	}

	ddata->pins_utmi = pinctrl_lookup_state(ddata->pins, "utmi");
	if (IS_ERR(ddata->pins_utmi)) {
		dev_info(ddata->dev, "utmi pins not configured\n");
		ddata->pins_utmi = NULL;
	}

	ddata->pins_uart = pinctrl_lookup_state(ddata->pins, "uart");
	if (IS_ERR(ddata->pins_uart)) {
		dev_info(ddata->dev, "uart pins not configured\n");
		ddata->pins_uart = NULL;
	}

	if (ddata->pins_uart)
		return pinctrl_select_state(ddata->pins, ddata->pins_uart);

	return 0;
}

static void cpcap_usb_init_optional_gpios(struct cpcap_phy_ddata *ddata)
{
	int i;

	for (i = 0; i < 2; i++) {
		ddata->gpio[i] = devm_gpiod_get_index(ddata->dev, "mode",
						      i, GPIOD_OUT_HIGH);
		if (IS_ERR(ddata->gpio[i])) {
			dev_info(ddata->dev, "no mode change GPIO%i: %li\n",
				 i, PTR_ERR(ddata->gpio[i]));
			ddata->gpio[i] = NULL;
		}
	}
}

static int cpcap_usb_init_iio(struct cpcap_phy_ddata *ddata)
{
	enum iio_chan_type type;
	int error;

	ddata->vbus = devm_iio_channel_get(ddata->dev, "vbus");
	if (IS_ERR(ddata->vbus)) {
		error = PTR_ERR(ddata->vbus);
		goto out_err;
	}

	if (!ddata->vbus->indio_dev) {
		error = -ENXIO;
		goto out_err;
	}

	error = iio_get_channel_type(ddata->vbus, &type);
	if (error < 0)
		goto out_err;

	if (type != IIO_VOLTAGE) {
		error = -EINVAL;
		goto out_err;
	}

	return 0;

out_err:
	dev_err(ddata->dev, "could not initialize VBUS or ID IIO: %i\n",
		error);

	return error;
}

#ifdef CONFIG_OF
static const struct of_device_id cpcap_usb_phy_id_table[] = {
	{
		.compatible = "motorola,cpcap-usb-phy",
	},
	{
		.compatible = "motorola,mapphone-cpcap-usb-phy",
	},
	{},
};
MODULE_DEVICE_TABLE(of, cpcap_usb_phy_id_table);
#endif

static int cpcap_usb_phy_probe(struct platform_device *pdev)
{
	struct cpcap_phy_ddata *ddata;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct usb_otg *otg;
	const struct of_device_id *of_id;
	int error;

	of_id = of_match_device(of_match_ptr(cpcap_usb_phy_id_table),
				&pdev->dev);
	if (!of_id)
		return -EINVAL;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->reg = dev_get_regmap(pdev->dev.parent, NULL);
	if (!ddata->reg)
		return -ENODEV;

	otg = devm_kzalloc(&pdev->dev, sizeof(*otg), GFP_KERNEL);
	if (!otg)
		return -ENOMEM;

	ddata->dev = &pdev->dev;
	ddata->phy.dev = ddata->dev;
	ddata->phy.label = "cpcap_usb_phy";
	ddata->phy.otg = otg;
	ddata->phy.type = USB_PHY_TYPE_USB2;
	otg->set_host = cpcap_usb_phy_set_host;
	otg->set_peripheral = cpcap_usb_phy_set_peripheral;
	otg->usb_phy = &ddata->phy;
	INIT_DELAYED_WORK(&ddata->detect_work, cpcap_usb_detect);
	platform_set_drvdata(pdev, ddata);

	ddata->vusb = devm_regulator_get(&pdev->dev, "vusb");
	if (IS_ERR(ddata->vusb))
		return PTR_ERR(ddata->vusb);

	error = regulator_enable(ddata->vusb);
	if (error)
		return error;

	generic_phy = devm_phy_create(ddata->dev, NULL, &ops);
	if (IS_ERR(generic_phy)) {
		error = PTR_ERR(generic_phy);
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, ddata);

	phy_provider = devm_of_phy_provider_register(ddata->dev,
						     of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	error = cpcap_usb_init_optional_pins(ddata);
	if (error)
		return error;

	cpcap_usb_init_optional_gpios(ddata);

	error = cpcap_usb_init_iio(ddata);
	if (error)
		return error;

	error = cpcap_usb_init_interrupts(pdev, ddata);
	if (error)
		return error;

	usb_add_phy_dev(&ddata->phy);
	atomic_set(&ddata->active, 1);
	schedule_delayed_work(&ddata->detect_work, msecs_to_jiffies(1));

	return 0;
}

static int cpcap_usb_phy_remove(struct platform_device *pdev)
{
	struct cpcap_phy_ddata *ddata = platform_get_drvdata(pdev);
	int error;

	atomic_set(&ddata->active, 0);
	error = cpcap_usb_set_uart_mode(ddata);
	if (error)
		dev_err(ddata->dev, "could not set UART mode\n");

	cpcap_usb_try_musb_mailbox(ddata, MUSB_VBUS_OFF);

	usb_remove_phy(&ddata->phy);
	cancel_delayed_work_sync(&ddata->detect_work);
	clk_unprepare(ddata->refclk);
	regulator_disable(ddata->vusb);

	return 0;
}

static struct platform_driver cpcap_usb_phy_driver = {
	.probe		= cpcap_usb_phy_probe,
	.remove		= cpcap_usb_phy_remove,
	.driver		= {
		.name	= "cpcap-usb-phy",
		.of_match_table = of_match_ptr(cpcap_usb_phy_id_table),
	},
};

module_platform_driver(cpcap_usb_phy_driver);

MODULE_ALIAS("platform:cpcap_usb");
MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("CPCAP usb phy driver");
MODULE_LICENSE("GPL v2");
