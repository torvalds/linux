/*
 * SPI bridge PHY driver.
 *
 * Copyright 2014-2016 Google Inc.
 * Copyright 2014-2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/module.h>

#include "greybus.h"
#include "gpbridge.h"
#include "spilib.h"

static int gb_spi_probe(struct gpbridge_device *gpbdev,
			const struct gpbridge_device_id *id)
{
	struct gb_connection *connection;
	int ret;

	connection = gb_connection_create(gpbdev->bundle,
					  le16_to_cpu(gpbdev->cport_desc->id),
					  NULL);
	if (IS_ERR(connection))
		return PTR_ERR(connection);

	ret = gb_connection_enable(connection);
	if (ret)
		goto exit_connection_destroy;

	ret = gb_gpbridge_get_version(connection);
	if (ret)
		goto exit_connection_disable;

	ret = gb_spilib_master_init(connection, &gpbdev->dev);
	if (ret)
		goto exit_connection_disable;

	gb_gpbridge_set_data(gpbdev, connection);

	return 0;

exit_connection_disable:
	gb_connection_disable(connection);
exit_connection_destroy:
	gb_connection_destroy(connection);

	return ret;
}

static void gb_spi_remove(struct gpbridge_device *gpbdev)
{
	struct gb_connection *connection = gb_gpbridge_get_data(gpbdev);

	gb_spilib_master_exit(connection);
	gb_connection_disable(connection);
	gb_connection_destroy(connection);
}

static const struct gpbridge_device_id gb_spi_id_table[] = {
	{ GPBRIDGE_PROTOCOL(GREYBUS_PROTOCOL_SPI) },
	{ },
};
MODULE_DEVICE_TABLE(gpbridge, gb_spi_id_table);

static struct gpbridge_driver spi_driver = {
	.name		= "spi",
	.probe		= gb_spi_probe,
	.remove		= gb_spi_remove,
	.id_table	= gb_spi_id_table,
};

module_gpbridge_driver(spi_driver);
MODULE_LICENSE("GPL v2");
