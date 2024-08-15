// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Microchip USB251xB USB 2.0 Hi-Speed Hub Controller
 * Configuration via SMBus.
 *
 * Copyright (c) 2017 SKIDATA AG
 *
 * This work is based on the USB3503 driver by Dongjin Kim and
 * a not-accepted patch by Fabien Lahoudere, see:
 * https://patchwork.kernel.org/patch/9257715/
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/nls.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

/* Internal Register Set Addresses & Default Values acc. to DS00001692C */
#define USB251XB_ADDR_VENDOR_ID_LSB	0x00
#define USB251XB_ADDR_VENDOR_ID_MSB	0x01
#define USB251XB_DEF_VENDOR_ID		0x0424

#define USB251XB_ADDR_PRODUCT_ID_LSB	0x02
#define USB251XB_ADDR_PRODUCT_ID_MSB	0x03

#define USB251XB_ADDR_DEVICE_ID_LSB	0x04
#define USB251XB_ADDR_DEVICE_ID_MSB	0x05
#define USB251XB_DEF_DEVICE_ID		0x0BB3

#define USB251XB_ADDR_CONFIG_DATA_1	0x06
#define USB251XB_DEF_CONFIG_DATA_1	0x9B
#define USB251XB_ADDR_CONFIG_DATA_2	0x07
#define USB251XB_DEF_CONFIG_DATA_2	0x20
#define USB251XB_ADDR_CONFIG_DATA_3	0x08
#define USB251XB_DEF_CONFIG_DATA_3	0x02

#define USB251XB_ADDR_NON_REMOVABLE_DEVICES	0x09
#define USB251XB_DEF_NON_REMOVABLE_DEVICES	0x00

#define USB251XB_ADDR_PORT_DISABLE_SELF	0x0A
#define USB251XB_DEF_PORT_DISABLE_SELF	0x00
#define USB251XB_ADDR_PORT_DISABLE_BUS	0x0B
#define USB251XB_DEF_PORT_DISABLE_BUS	0x00

#define USB251XB_ADDR_MAX_POWER_SELF	0x0C
#define USB251XB_DEF_MAX_POWER_SELF	0x01
#define USB251XB_ADDR_MAX_POWER_BUS	0x0D
#define USB251XB_DEF_MAX_POWER_BUS	0x32

#define USB251XB_ADDR_MAX_CURRENT_SELF	0x0E
#define USB251XB_DEF_MAX_CURRENT_SELF	0x01
#define USB251XB_ADDR_MAX_CURRENT_BUS	0x0F
#define USB251XB_DEF_MAX_CURRENT_BUS	0x32

#define USB251XB_ADDR_POWER_ON_TIME	0x10
#define USB251XB_DEF_POWER_ON_TIME	0x32

#define USB251XB_ADDR_LANGUAGE_ID_HIGH	0x11
#define USB251XB_ADDR_LANGUAGE_ID_LOW	0x12
#define USB251XB_DEF_LANGUAGE_ID	0x0000

#define USB251XB_STRING_BUFSIZE			62
#define USB251XB_ADDR_MANUFACTURER_STRING_LEN	0x13
#define USB251XB_ADDR_MANUFACTURER_STRING	0x16
#define USB251XB_DEF_MANUFACTURER_STRING	"Microchip"

#define USB251XB_ADDR_PRODUCT_STRING_LEN	0x14
#define USB251XB_ADDR_PRODUCT_STRING		0x54

#define USB251XB_ADDR_SERIAL_STRING_LEN		0x15
#define USB251XB_ADDR_SERIAL_STRING		0x92
#define USB251XB_DEF_SERIAL_STRING		""

#define USB251XB_ADDR_BATTERY_CHARGING_ENABLE	0xD0
#define USB251XB_DEF_BATTERY_CHARGING_ENABLE	0x00

#define USB251XB_ADDR_BOOST_UP	0xF6
#define USB251XB_DEF_BOOST_UP	0x00
#define USB251XB_ADDR_BOOST_57	0xF7
#define USB251XB_DEF_BOOST_57	0x00
#define USB251XB_ADDR_BOOST_14	0xF8
#define USB251XB_DEF_BOOST_14	0x00

#define USB251XB_ADDR_PORT_SWAP	0xFA
#define USB251XB_DEF_PORT_SWAP	0x00

#define USB251XB_ADDR_PORT_MAP_12	0xFB
#define USB251XB_DEF_PORT_MAP_12	0x00
#define USB251XB_ADDR_PORT_MAP_34	0xFC
#define USB251XB_DEF_PORT_MAP_34	0x00 /* USB251{3B/i,4B/i,7/i} only */
#define USB251XB_ADDR_PORT_MAP_56	0xFD
#define USB251XB_DEF_PORT_MAP_56	0x00 /* USB2517/i only */
#define USB251XB_ADDR_PORT_MAP_7	0xFE
#define USB251XB_DEF_PORT_MAP_7		0x00 /* USB2517/i only */

#define USB251XB_ADDR_STATUS_COMMAND		0xFF
#define USB251XB_STATUS_COMMAND_SMBUS_DOWN	0x04
#define USB251XB_STATUS_COMMAND_RESET		0x02
#define USB251XB_STATUS_COMMAND_ATTACH		0x01

#define USB251XB_I2C_REG_SZ	0x100
#define USB251XB_I2C_WRITE_SZ	0x10

#define DRIVER_NAME	"usb251xb"
#define DRIVER_DESC	"Microchip USB 2.0 Hi-Speed Hub Controller"

struct usb251xb {
	struct device *dev;
	struct i2c_client *i2c;
	struct regulator *vdd;
	u8 skip_config;
	struct gpio_desc *gpio_reset;
	u16 vendor_id;
	u16 product_id;
	u16 device_id;
	u8  conf_data1;
	u8  conf_data2;
	u8  conf_data3;
	u8  non_rem_dev;
	u8  port_disable_sp;
	u8  port_disable_bp;
	u8  max_power_sp;
	u8  max_power_bp;
	u8  max_current_sp;
	u8  max_current_bp;
	u8  power_on_time;
	u16 lang_id;
	u8 manufacturer_len;
	u8 product_len;
	u8 serial_len;
	char manufacturer[USB251XB_STRING_BUFSIZE];
	char product[USB251XB_STRING_BUFSIZE];
	char serial[USB251XB_STRING_BUFSIZE];
	u8  bat_charge_en;
	u8  boost_up;
	u8  boost_57;
	u8  boost_14;
	u8  port_swap;
	u8  port_map12;
	u8  port_map34;
	u8  port_map56;
	u8  port_map7;
	u8  status;
};

struct usb251xb_data {
	u16 product_id;
	u8 port_cnt;
	bool led_support;
	bool bat_support;
	char product_str[USB251XB_STRING_BUFSIZE / 2]; /* ASCII string */
};

static const struct usb251xb_data usb2422_data = {
	.product_id = 0x2422,
	.port_cnt = 2,
	.led_support = false,
	.bat_support = true,
	.product_str = "USB2422",
};

static const struct usb251xb_data usb2512b_data = {
	.product_id = 0x2512,
	.port_cnt = 2,
	.led_support = false,
	.bat_support = true,
	.product_str = "USB2512B",
};

static const struct usb251xb_data usb2512bi_data = {
	.product_id = 0x2512,
	.port_cnt = 2,
	.led_support = false,
	.bat_support = true,
	.product_str = "USB2512Bi",
};

static const struct usb251xb_data usb2513b_data = {
	.product_id = 0x2513,
	.port_cnt = 3,
	.led_support = false,
	.bat_support = true,
	.product_str = "USB2513B",
};

static const struct usb251xb_data usb2513bi_data = {
	.product_id = 0x2513,
	.port_cnt = 3,
	.led_support = false,
	.bat_support = true,
	.product_str = "USB2513Bi",
};

static const struct usb251xb_data usb2514b_data = {
	.product_id = 0x2514,
	.port_cnt = 4,
	.led_support = false,
	.bat_support = true,
	.product_str = "USB2514B",
};

static const struct usb251xb_data usb2514bi_data = {
	.product_id = 0x2514,
	.port_cnt = 4,
	.led_support = false,
	.bat_support = true,
	.product_str = "USB2514Bi",
};

static const struct usb251xb_data usb2517_data = {
	.product_id = 0x2517,
	.port_cnt = 7,
	.led_support = true,
	.bat_support = false,
	.product_str = "USB2517",
};

static const struct usb251xb_data usb2517i_data = {
	.product_id = 0x2517,
	.port_cnt = 7,
	.led_support = true,
	.bat_support = false,
	.product_str = "USB2517i",
};

#ifdef CONFIG_GPIOLIB
static int usb251xb_check_dev_children(struct device *dev, void *child)
{
	if (dev->type == &i2c_adapter_type) {
		return device_for_each_child(dev, child,
					     usb251xb_check_dev_children);
	}

	return (dev == child);
}

static int usb251x_check_gpio_chip(struct usb251xb *hub)
{
	struct gpio_chip *gc = gpiod_to_chip(hub->gpio_reset);
	struct i2c_adapter *adap = hub->i2c->adapter;
	int ret;

	if (!hub->gpio_reset)
		return 0;

	if (!gc)
		return -EINVAL;

	ret = usb251xb_check_dev_children(&adap->dev, gc->parent);
	if (ret) {
		dev_err(hub->dev, "Reset GPIO chip is at the same i2c-bus\n");
		return -EINVAL;
	}

	return 0;
}
#else
static int usb251x_check_gpio_chip(struct usb251xb *hub)
{
	return 0;
}
#endif

static void usb251xb_reset(struct usb251xb *hub)
{
	if (!hub->gpio_reset)
		return;

	i2c_lock_bus(hub->i2c->adapter, I2C_LOCK_SEGMENT);

	gpiod_set_value_cansleep(hub->gpio_reset, 1);
	usleep_range(1, 10);	/* >=1us RESET_N asserted */
	gpiod_set_value_cansleep(hub->gpio_reset, 0);

	/* wait for hub recovery/stabilization */
	usleep_range(500, 750);	/* >=500us after RESET_N deasserted */

	i2c_unlock_bus(hub->i2c->adapter, I2C_LOCK_SEGMENT);
}

static int usb251xb_connect(struct usb251xb *hub)
{
	struct device *dev = hub->dev;
	int err, i;
	char i2c_wb[USB251XB_I2C_REG_SZ];

	memset(i2c_wb, 0, USB251XB_I2C_REG_SZ);

	if (hub->skip_config) {
		dev_info(dev, "Skip hub configuration, only attach.\n");
		i2c_wb[0] = 0x01;
		i2c_wb[1] = USB251XB_STATUS_COMMAND_ATTACH;

		usb251xb_reset(hub);

		err = i2c_smbus_write_i2c_block_data(hub->i2c,
				USB251XB_ADDR_STATUS_COMMAND, 2, i2c_wb);
		if (err) {
			dev_err(dev, "attaching hub failed: %d\n", err);
			return err;
		}
		return 0;
	}

	i2c_wb[USB251XB_ADDR_VENDOR_ID_MSB]     = (hub->vendor_id >> 8) & 0xFF;
	i2c_wb[USB251XB_ADDR_VENDOR_ID_LSB]     = hub->vendor_id & 0xFF;
	i2c_wb[USB251XB_ADDR_PRODUCT_ID_MSB]    = (hub->product_id >> 8) & 0xFF;
	i2c_wb[USB251XB_ADDR_PRODUCT_ID_LSB]    = hub->product_id & 0xFF;
	i2c_wb[USB251XB_ADDR_DEVICE_ID_MSB]     = (hub->device_id >> 8) & 0xFF;
	i2c_wb[USB251XB_ADDR_DEVICE_ID_LSB]     = hub->device_id & 0xFF;
	i2c_wb[USB251XB_ADDR_CONFIG_DATA_1]     = hub->conf_data1;
	i2c_wb[USB251XB_ADDR_CONFIG_DATA_2]     = hub->conf_data2;
	i2c_wb[USB251XB_ADDR_CONFIG_DATA_3]     = hub->conf_data3;
	i2c_wb[USB251XB_ADDR_NON_REMOVABLE_DEVICES] = hub->non_rem_dev;
	i2c_wb[USB251XB_ADDR_PORT_DISABLE_SELF] = hub->port_disable_sp;
	i2c_wb[USB251XB_ADDR_PORT_DISABLE_BUS]  = hub->port_disable_bp;
	i2c_wb[USB251XB_ADDR_MAX_POWER_SELF]    = hub->max_power_sp;
	i2c_wb[USB251XB_ADDR_MAX_POWER_BUS]     = hub->max_power_bp;
	i2c_wb[USB251XB_ADDR_MAX_CURRENT_SELF]  = hub->max_current_sp;
	i2c_wb[USB251XB_ADDR_MAX_CURRENT_BUS]   = hub->max_current_bp;
	i2c_wb[USB251XB_ADDR_POWER_ON_TIME]     = hub->power_on_time;
	i2c_wb[USB251XB_ADDR_LANGUAGE_ID_HIGH]  = (hub->lang_id >> 8) & 0xFF;
	i2c_wb[USB251XB_ADDR_LANGUAGE_ID_LOW]   = hub->lang_id & 0xFF;
	i2c_wb[USB251XB_ADDR_MANUFACTURER_STRING_LEN] = hub->manufacturer_len;
	i2c_wb[USB251XB_ADDR_PRODUCT_STRING_LEN]      = hub->product_len;
	i2c_wb[USB251XB_ADDR_SERIAL_STRING_LEN]       = hub->serial_len;
	memcpy(&i2c_wb[USB251XB_ADDR_MANUFACTURER_STRING], hub->manufacturer,
	       USB251XB_STRING_BUFSIZE);
	memcpy(&i2c_wb[USB251XB_ADDR_SERIAL_STRING], hub->serial,
	       USB251XB_STRING_BUFSIZE);
	memcpy(&i2c_wb[USB251XB_ADDR_PRODUCT_STRING], hub->product,
	       USB251XB_STRING_BUFSIZE);
	i2c_wb[USB251XB_ADDR_BATTERY_CHARGING_ENABLE] = hub->bat_charge_en;
	i2c_wb[USB251XB_ADDR_BOOST_UP]          = hub->boost_up;
	i2c_wb[USB251XB_ADDR_BOOST_57]          = hub->boost_57;
	i2c_wb[USB251XB_ADDR_BOOST_14]          = hub->boost_14;
	i2c_wb[USB251XB_ADDR_PORT_SWAP]         = hub->port_swap;
	i2c_wb[USB251XB_ADDR_PORT_MAP_12]       = hub->port_map12;
	i2c_wb[USB251XB_ADDR_PORT_MAP_34]       = hub->port_map34;
	i2c_wb[USB251XB_ADDR_PORT_MAP_56]       = hub->port_map56;
	i2c_wb[USB251XB_ADDR_PORT_MAP_7]        = hub->port_map7;
	i2c_wb[USB251XB_ADDR_STATUS_COMMAND] = USB251XB_STATUS_COMMAND_ATTACH;

	usb251xb_reset(hub);

	/* write registers */
	for (i = 0; i < (USB251XB_I2C_REG_SZ / USB251XB_I2C_WRITE_SZ); i++) {
		int offset = i * USB251XB_I2C_WRITE_SZ;
		char wbuf[USB251XB_I2C_WRITE_SZ + 1];

		/* The first data byte transferred tells the hub how many data
		 * bytes will follow (byte count).
		 */
		wbuf[0] = USB251XB_I2C_WRITE_SZ;
		memcpy(&wbuf[1], &i2c_wb[offset], USB251XB_I2C_WRITE_SZ);

		dev_dbg(dev, "writing %d byte block %d to 0x%02X\n",
			USB251XB_I2C_WRITE_SZ, i, offset);

		err = i2c_smbus_write_i2c_block_data(hub->i2c, offset,
						     USB251XB_I2C_WRITE_SZ + 1,
						     wbuf);
		if (err)
			goto out_err;
	}

	dev_info(dev, "Hub configuration was successful.\n");
	return 0;

out_err:
	dev_err(dev, "configuring block %d failed: %d\n", i, err);
	return err;
}

#ifdef CONFIG_OF
static void usb251xb_get_ports_field(struct usb251xb *hub,
				    const char *prop_name, u8 port_cnt,
				    bool ds_only, u8 *fld)
{
	struct device *dev = hub->dev;
	struct property *prop;
	const __be32 *p;
	u32 port;

	of_property_for_each_u32(dev->of_node, prop_name, prop, p, port) {
		if ((port >= ds_only ? 1 : 0) && (port <= port_cnt))
			*fld |= BIT(port);
		else
			dev_warn(dev, "port %u doesn't exist\n", port);
	}
}

static int usb251xb_get_ofdata(struct usb251xb *hub,
			       const struct usb251xb_data *data)
{
	struct device *dev = hub->dev;
	struct device_node *np = dev->of_node;
	int len;
	u32 property_u32 = 0;
	const char *cproperty_char;
	char str[USB251XB_STRING_BUFSIZE / 2];

	if (!np) {
		dev_err(dev, "failed to get ofdata\n");
		return -ENODEV;
	}

	if (of_get_property(np, "skip-config", NULL))
		hub->skip_config = 1;
	else
		hub->skip_config = 0;

	hub->gpio_reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(hub->gpio_reset))
		return dev_err_probe(dev, PTR_ERR(hub->gpio_reset),
				     "unable to request GPIO reset pin\n");

	if (of_property_read_u16_array(np, "vendor-id", &hub->vendor_id, 1))
		hub->vendor_id = USB251XB_DEF_VENDOR_ID;

	if (of_property_read_u16_array(np, "product-id",
				       &hub->product_id, 1))
		hub->product_id = data->product_id;

	if (of_property_read_u16_array(np, "device-id", &hub->device_id, 1))
		hub->device_id = USB251XB_DEF_DEVICE_ID;

	hub->conf_data1 = USB251XB_DEF_CONFIG_DATA_1;
	if (of_get_property(np, "self-powered", NULL)) {
		hub->conf_data1 |= BIT(7);

		/* Configure Over-Current sens when self-powered */
		hub->conf_data1 &= ~BIT(2);
		if (of_get_property(np, "ganged-sensing", NULL))
			hub->conf_data1 &= ~BIT(1);
		else if (of_get_property(np, "individual-sensing", NULL))
			hub->conf_data1 |= BIT(1);
	} else if (of_get_property(np, "bus-powered", NULL)) {
		hub->conf_data1 &= ~BIT(7);

		/* Disable Over-Current sense when bus-powered */
		hub->conf_data1 |= BIT(2);
	}

	if (of_get_property(np, "disable-hi-speed", NULL))
		hub->conf_data1 |= BIT(5);

	if (of_get_property(np, "multi-tt", NULL))
		hub->conf_data1 |= BIT(4);
	else if (of_get_property(np, "single-tt", NULL))
		hub->conf_data1 &= ~BIT(4);

	if (of_get_property(np, "disable-eop", NULL))
		hub->conf_data1 |= BIT(3);

	if (of_get_property(np, "individual-port-switching", NULL))
		hub->conf_data1 |= BIT(0);
	else if (of_get_property(np, "ganged-port-switching", NULL))
		hub->conf_data1 &= ~BIT(0);

	hub->conf_data2 = USB251XB_DEF_CONFIG_DATA_2;
	if (of_get_property(np, "dynamic-power-switching", NULL))
		hub->conf_data2 |= BIT(7);

	if (!of_property_read_u32(np, "oc-delay-us", &property_u32)) {
		if (property_u32 == 100) {
			/* 100 us*/
			hub->conf_data2 &= ~BIT(5);
			hub->conf_data2 &= ~BIT(4);
		} else if (property_u32 == 4000) {
			/* 4 ms */
			hub->conf_data2 &= ~BIT(5);
			hub->conf_data2 |= BIT(4);
		} else if (property_u32 == 16000) {
			/* 16 ms */
			hub->conf_data2 |= BIT(5);
			hub->conf_data2 |= BIT(4);
		} else {
			/* 8 ms (DEFAULT) */
			hub->conf_data2 |= BIT(5);
			hub->conf_data2 &= ~BIT(4);
		}
	}

	if (of_get_property(np, "compound-device", NULL))
		hub->conf_data2 |= BIT(3);

	hub->conf_data3 = USB251XB_DEF_CONFIG_DATA_3;
	if (of_get_property(np, "port-mapping-mode", NULL))
		hub->conf_data3 |= BIT(3);

	if (data->led_support && of_get_property(np, "led-usb-mode", NULL))
		hub->conf_data3 &= ~BIT(1);

	if (of_get_property(np, "string-support", NULL))
		hub->conf_data3 |= BIT(0);

	hub->non_rem_dev = USB251XB_DEF_NON_REMOVABLE_DEVICES;
	usb251xb_get_ports_field(hub, "non-removable-ports", data->port_cnt,
				 true, &hub->non_rem_dev);

	hub->port_disable_sp = USB251XB_DEF_PORT_DISABLE_SELF;
	usb251xb_get_ports_field(hub, "sp-disabled-ports", data->port_cnt,
				 true, &hub->port_disable_sp);

	hub->port_disable_bp = USB251XB_DEF_PORT_DISABLE_BUS;
	usb251xb_get_ports_field(hub, "bp-disabled-ports", data->port_cnt,
				 true, &hub->port_disable_bp);

	hub->max_power_sp = USB251XB_DEF_MAX_POWER_SELF;
	if (!of_property_read_u32(np, "sp-max-total-current-microamp",
	    &property_u32))
		hub->max_power_sp = min_t(u8, property_u32 / 2000, 50);

	hub->max_power_bp = USB251XB_DEF_MAX_POWER_BUS;
	if (!of_property_read_u32(np, "bp-max-total-current-microamp",
	    &property_u32))
		hub->max_power_bp = min_t(u8, property_u32 / 2000, 255);

	hub->max_current_sp = USB251XB_DEF_MAX_CURRENT_SELF;
	if (!of_property_read_u32(np, "sp-max-removable-current-microamp",
	    &property_u32))
		hub->max_current_sp = min_t(u8, property_u32 / 2000, 50);

	hub->max_current_bp = USB251XB_DEF_MAX_CURRENT_BUS;
	if (!of_property_read_u32(np, "bp-max-removable-current-microamp",
	    &property_u32))
		hub->max_current_bp = min_t(u8, property_u32 / 2000, 255);

	hub->power_on_time = USB251XB_DEF_POWER_ON_TIME;
	if (!of_property_read_u32(np, "power-on-time-ms", &property_u32))
		hub->power_on_time = min_t(u8, property_u32 / 2, 255);

	if (of_property_read_u16_array(np, "language-id", &hub->lang_id, 1))
		hub->lang_id = USB251XB_DEF_LANGUAGE_ID;

	if (of_property_read_u8(np, "boost-up", &hub->boost_up))
		hub->boost_up = USB251XB_DEF_BOOST_UP;

	cproperty_char = of_get_property(np, "manufacturer", NULL);
	strscpy(str, cproperty_char ? : USB251XB_DEF_MANUFACTURER_STRING,
		sizeof(str));
	hub->manufacturer_len = strlen(str) & 0xFF;
	memset(hub->manufacturer, 0, USB251XB_STRING_BUFSIZE);
	len = min_t(size_t, USB251XB_STRING_BUFSIZE / 2, strlen(str));
	len = utf8s_to_utf16s(str, len, UTF16_LITTLE_ENDIAN,
			      (wchar_t *)hub->manufacturer,
			      USB251XB_STRING_BUFSIZE);

	cproperty_char = of_get_property(np, "product", NULL);
	strscpy(str, cproperty_char ? : data->product_str, sizeof(str));
	hub->product_len = strlen(str) & 0xFF;
	memset(hub->product, 0, USB251XB_STRING_BUFSIZE);
	len = min_t(size_t, USB251XB_STRING_BUFSIZE / 2, strlen(str));
	len = utf8s_to_utf16s(str, len, UTF16_LITTLE_ENDIAN,
			      (wchar_t *)hub->product,
			      USB251XB_STRING_BUFSIZE);

	cproperty_char = of_get_property(np, "serial", NULL);
	strscpy(str, cproperty_char ? : USB251XB_DEF_SERIAL_STRING,
		sizeof(str));
	hub->serial_len = strlen(str) & 0xFF;
	memset(hub->serial, 0, USB251XB_STRING_BUFSIZE);
	len = min_t(size_t, USB251XB_STRING_BUFSIZE / 2, strlen(str));
	len = utf8s_to_utf16s(str, len, UTF16_LITTLE_ENDIAN,
			      (wchar_t *)hub->serial,
			      USB251XB_STRING_BUFSIZE);

	/*
	 * The datasheet documents the register as 'Port Swap' but in real the
	 * register controls the USB DP/DM signal swapping for each port.
	 */
	hub->port_swap = USB251XB_DEF_PORT_SWAP;
	usb251xb_get_ports_field(hub, "swap-dx-lanes", data->port_cnt,
				 false, &hub->port_swap);

	/* The following parameters are currently not exposed to devicetree, but
	 * may be as soon as needed.
	 */
	hub->bat_charge_en = USB251XB_DEF_BATTERY_CHARGING_ENABLE;
	hub->boost_57 = USB251XB_DEF_BOOST_57;
	hub->boost_14 = USB251XB_DEF_BOOST_14;
	hub->port_map12 = USB251XB_DEF_PORT_MAP_12;
	hub->port_map34 = USB251XB_DEF_PORT_MAP_34;
	hub->port_map56 = USB251XB_DEF_PORT_MAP_56;
	hub->port_map7  = USB251XB_DEF_PORT_MAP_7;

	return 0;
}

static const struct of_device_id usb251xb_of_match[] = {
	{
		.compatible = "microchip,usb2422",
		.data = &usb2422_data,
	}, {
		.compatible = "microchip,usb2512b",
		.data = &usb2512b_data,
	}, {
		.compatible = "microchip,usb2512bi",
		.data = &usb2512bi_data,
	}, {
		.compatible = "microchip,usb2513b",
		.data = &usb2513b_data,
	}, {
		.compatible = "microchip,usb2513bi",
		.data = &usb2513bi_data,
	}, {
		.compatible = "microchip,usb2514b",
		.data = &usb2514b_data,
	}, {
		.compatible = "microchip,usb2514bi",
		.data = &usb2514bi_data,
	}, {
		.compatible = "microchip,usb2517",
		.data = &usb2517_data,
	}, {
		.compatible = "microchip,usb2517i",
		.data = &usb2517i_data,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, usb251xb_of_match);
#else /* CONFIG_OF */
static int usb251xb_get_ofdata(struct usb251xb *hub,
			       const struct usb251xb_data *data)
{
	return 0;
}
#endif /* CONFIG_OF */

static void usb251xb_regulator_disable_action(void *data)
{
	struct usb251xb *hub = data;

	regulator_disable(hub->vdd);
}

static int usb251xb_probe(struct usb251xb *hub)
{
	struct device *dev = hub->dev;
	struct device_node *np = dev->of_node;
	const struct usb251xb_data *usb_data = of_device_get_match_data(dev);
	int err;

	if (np && usb_data) {
		err = usb251xb_get_ofdata(hub, usb_data);
		if (err) {
			dev_err(dev, "failed to get ofdata: %d\n", err);
			return err;
		}
	}

	/*
	 * usb251x SMBus-slave SCL lane is muxed with CFG_SEL0 pin. So if anyone
	 * tries to work with the bus at the moment the hub reset is released,
	 * it may cause an invalid config being latched by usb251x. Particularly
	 * one of the config modes makes the hub loading a default registers
	 * value without SMBus-slave interface activation. If the hub
	 * accidentally gets this mode, this will cause the driver SMBus-
	 * functions failure. Normally we could just lock the SMBus-segment the
	 * hub i2c-interface resides for the device-specific reset timing. But
	 * the GPIO controller, which is used to handle the hub reset, might be
	 * placed at the same i2c-bus segment. In this case an error should be
	 * returned since we can't safely use the GPIO controller to clear the
	 * reset state (it may affect the hub configuration) and we can't lock
	 * the i2c-bus segment (it will cause a deadlock).
	 */
	err = usb251x_check_gpio_chip(hub);
	if (err)
		return err;

	hub->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(hub->vdd))
		return PTR_ERR(hub->vdd);

	err = regulator_enable(hub->vdd);
	if (err)
		return err;

	err = devm_add_action_or_reset(dev,
				       usb251xb_regulator_disable_action, hub);
	if (err)
		return err;

	err = usb251xb_connect(hub);
	if (err) {
		dev_err(dev, "Failed to connect hub (%d)\n", err);
		return err;
	}

	dev_info(dev, "Hub probed successfully\n");

	return 0;
}

static int usb251xb_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct usb251xb *hub;

	hub = devm_kzalloc(&i2c->dev, sizeof(struct usb251xb), GFP_KERNEL);
	if (!hub)
		return -ENOMEM;

	i2c_set_clientdata(i2c, hub);
	hub->dev = &i2c->dev;
	hub->i2c = i2c;

	return usb251xb_probe(hub);
}

static int __maybe_unused usb251xb_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct usb251xb *hub = i2c_get_clientdata(client);

	return regulator_disable(hub->vdd);
}

static int __maybe_unused usb251xb_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct usb251xb *hub = i2c_get_clientdata(client);
	int err;

	err = regulator_enable(hub->vdd);
	if (err)
		return err;

	return usb251xb_connect(hub);
}

static SIMPLE_DEV_PM_OPS(usb251xb_pm_ops, usb251xb_suspend, usb251xb_resume);

static const struct i2c_device_id usb251xb_id[] = {
	{ "usb2422", 0 },
	{ "usb2512b", 0 },
	{ "usb2512bi", 0 },
	{ "usb2513b", 0 },
	{ "usb2513bi", 0 },
	{ "usb2514b", 0 },
	{ "usb2514bi", 0 },
	{ "usb2517", 0 },
	{ "usb2517i", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, usb251xb_id);

static struct i2c_driver usb251xb_i2c_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(usb251xb_of_match),
		.pm = &usb251xb_pm_ops,
	},
	.probe    = usb251xb_i2c_probe,
	.id_table = usb251xb_id,
};

module_i2c_driver(usb251xb_i2c_driver);

MODULE_AUTHOR("Richard Leitner <richard.leitner@skidata.com>");
MODULE_DESCRIPTION("USB251x/xBi USB 2.0 Hub Controller Driver");
MODULE_LICENSE("GPL");
