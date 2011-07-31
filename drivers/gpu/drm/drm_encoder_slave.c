/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drm_encoder_slave.h"

/**
 * drm_i2c_encoder_init - Initialize an I2C slave encoder
 * @dev:	DRM device.
 * @encoder:    Encoder to be attached to the I2C device. You aren't
 *		required to have called drm_encoder_init() before.
 * @adap:	I2C adapter that will be used to communicate with
 *		the device.
 * @info:	Information that will be used to create the I2C device.
 *		Required fields are @addr and @type.
 *
 * Create an I2C device on the specified bus (the module containing its
 * driver is transparently loaded) and attach it to the specified
 * &drm_encoder_slave. The @slave_funcs field will be initialized with
 * the hooks provided by the slave driver.
 *
 * If @info->platform_data is non-NULL it will be used as the initial
 * slave config.
 *
 * Returns 0 on success or a negative errno on failure, in particular,
 * -ENODEV is returned when no matching driver is found.
 */
int drm_i2c_encoder_init(struct drm_device *dev,
			 struct drm_encoder_slave *encoder,
			 struct i2c_adapter *adap,
			 const struct i2c_board_info *info)
{
	char modalias[sizeof(I2C_MODULE_PREFIX)
		      + I2C_NAME_SIZE];
	struct module *module = NULL;
	struct i2c_client *client;
	struct drm_i2c_encoder_driver *encoder_drv;
	int err = 0;

	snprintf(modalias, sizeof(modalias),
		 "%s%s", I2C_MODULE_PREFIX, info->type);
	request_module(modalias);

	client = i2c_new_device(adap, info);
	if (!client) {
		err = -ENOMEM;
		goto fail;
	}

	if (!client->driver) {
		err = -ENODEV;
		goto fail_unregister;
	}

	module = client->driver->driver.owner;
	if (!try_module_get(module)) {
		err = -ENODEV;
		goto fail_unregister;
	}

	encoder->bus_priv = client;

	encoder_drv = to_drm_i2c_encoder_driver(client->driver);

	err = encoder_drv->encoder_init(client, dev, encoder);
	if (err)
		goto fail_unregister;

	if (info->platform_data)
		encoder->slave_funcs->set_config(&encoder->base,
						 info->platform_data);

	return 0;

fail_unregister:
	i2c_unregister_device(client);
	module_put(module);
fail:
	return err;
}
EXPORT_SYMBOL(drm_i2c_encoder_init);

/**
 * drm_i2c_encoder_destroy - Unregister the I2C device backing an encoder
 * @drm_encoder:	Encoder to be unregistered.
 *
 * This should be called from the @destroy method of an I2C slave
 * encoder driver once I2C access is no longer needed.
 */
void drm_i2c_encoder_destroy(struct drm_encoder *drm_encoder)
{
	struct drm_encoder_slave *encoder = to_encoder_slave(drm_encoder);
	struct i2c_client *client = drm_i2c_encoder_get_client(drm_encoder);
	struct module *module = client->driver->driver.owner;

	i2c_unregister_device(client);
	encoder->bus_priv = NULL;

	module_put(module);
}
EXPORT_SYMBOL(drm_i2c_encoder_destroy);
