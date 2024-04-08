// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * v4l2-spi - SPI helpers for Video4Linux2
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>

void v4l2_spi_subdev_unregister(struct v4l2_subdev *sd)
{
	struct spi_device *spi = v4l2_get_subdevdata(sd);

	if (spi && !spi->dev.of_node && !spi->dev.fwnode)
		spi_unregister_device(spi);
}

void v4l2_spi_subdev_init(struct v4l2_subdev *sd, struct spi_device *spi,
			  const struct v4l2_subdev_ops *ops)
{
	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_IS_SPI;
	/* the owner is the same as the spi_device's driver owner */
	sd->owner = spi->dev.driver->owner;
	sd->dev = &spi->dev;
	/* spi_device and v4l2_subdev point to one another */
	v4l2_set_subdevdata(sd, spi);
	spi_set_drvdata(spi, sd);
	/* initialize name */
	snprintf(sd->name, sizeof(sd->name), "%s %s",
		 spi->dev.driver->name, dev_name(&spi->dev));
}
EXPORT_SYMBOL_GPL(v4l2_spi_subdev_init);

struct v4l2_subdev *v4l2_spi_new_subdev(struct v4l2_device *v4l2_dev,
					struct spi_controller *ctlr,
					struct spi_board_info *info)
{
	struct v4l2_subdev *sd = NULL;
	struct spi_device *spi = NULL;

	if (!v4l2_dev)
		return NULL;
	if (info->modalias[0])
		request_module(info->modalias);

	spi = spi_new_device(ctlr, info);

	if (!spi || !spi->dev.driver)
		goto error;

	if (!try_module_get(spi->dev.driver->owner))
		goto error;

	sd = spi_get_drvdata(spi);

	/*
	 * Register with the v4l2_device which increases the module's
	 * use count as well.
	 */
	if (v4l2_device_register_subdev(v4l2_dev, sd))
		sd = NULL;

	/* Decrease the module use count to match the first try_module_get. */
	module_put(spi->dev.driver->owner);

error:
	/*
	 * If we have a client but no subdev, then something went wrong and
	 * we must unregister the client.
	 */
	if (!sd)
		spi_unregister_device(spi);

	return sd;
}
EXPORT_SYMBOL_GPL(v4l2_spi_new_subdev);
