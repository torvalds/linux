/*
 * Greybus legacy-protocol driver
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"
#include "legacy.h"

static int legacy_probe(struct gb_bundle *bundle,
			const struct greybus_bundle_id *id)
{
	struct gb_connection *connection;
	int ret;

	dev_dbg(&bundle->dev, "%s - bundle class = 0x%02x\n", __func__,
			bundle->class);

	list_for_each_entry(connection, &bundle->connections, bundle_links) {
		dev_dbg(&bundle->dev, "enabling connection %s\n",
				connection->name);

		ret = gb_connection_legacy_init(connection);
		if (ret)
			goto err_connections_disable;
	}

	return 0;

err_connections_disable:
	list_for_each_entry_reverse(connection, &bundle->connections,
							bundle_links) {
		gb_connection_legacy_exit(connection);
	}

	return ret;
}

static void legacy_disconnect(struct gb_bundle *bundle)
{
	struct gb_connection *connection;

	dev_dbg(&bundle->dev, "%s - bundle class = 0x%02x\n", __func__,
			bundle->class);

	list_for_each_entry_reverse(connection, &bundle->connections,
							bundle_links) {
		gb_connection_legacy_exit(connection);
	}
}

static const struct greybus_bundle_id legacy_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_GPIO) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_I2C) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_UART) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_HID) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_USB) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_SDIO) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_POWER_SUPPLY) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_PWM) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_SPI) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_DISPLAY) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_CAMERA) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_SENSOR) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_LIGHTS) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_VIBRATOR) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_LOOPBACK) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_AUDIO_MGMT) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_AUDIO_DATA) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_SVC) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_FIRMWARE) },
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_RAW) },
	{ }
};
MODULE_DEVICE_TABLE(greybus, legacy_id_table);

static struct greybus_driver legacy_driver = {
	.name		= "legacy",
	.probe		= legacy_probe,
	.disconnect	= legacy_disconnect,
	.id_table	= legacy_id_table,
};

int gb_legacy_init(void)
{
	return greybus_register(&legacy_driver);
}

void gb_legacy_exit(void)
{
	greybus_deregister(&legacy_driver);
}
