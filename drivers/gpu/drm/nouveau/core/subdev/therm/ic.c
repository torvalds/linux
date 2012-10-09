/*
 * Copyright 2012 Nouveau community
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Martin Peres
 */

#include "priv.h"

#include <subdev/i2c.h>
#include <subdev/bios/extdev.h>

static bool
probe_monitoring_device(struct nouveau_i2c_port *i2c,
			struct i2c_board_info *info)
{
	struct nouveau_therm_priv *priv = (void *)nouveau_therm(i2c->i2c);
	struct i2c_client *client;

	request_module("%s%s", I2C_MODULE_PREFIX, info->type);

	client = i2c_new_device(&i2c->adapter, info);
	if (!client)
		return false;

	if (!client->driver || client->driver->detect(client, info)) {
		i2c_unregister_device(client);
		return false;
	}

	nv_info(priv,
		"Found an %s at address 0x%x (controlled by lm_sensors)\n",
		info->type, info->addr);
	priv->ic = client;

	return true;
}

void
nouveau_therm_ic_ctor(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_bios *bios = nouveau_bios(therm);
	struct nouveau_i2c *i2c = nouveau_i2c(therm);
	struct nvbios_extdev_func extdev_entry;
	struct i2c_board_info info[] = {
		{ I2C_BOARD_INFO("w83l785ts", 0x2d) },
		{ I2C_BOARD_INFO("w83781d", 0x2d) },
		{ I2C_BOARD_INFO("adt7473", 0x2e) },
		{ I2C_BOARD_INFO("adt7473", 0x2d) },
		{ I2C_BOARD_INFO("adt7473", 0x2c) },
		{ I2C_BOARD_INFO("f75375", 0x2e) },
		{ I2C_BOARD_INFO("lm99", 0x4c) },
		{ I2C_BOARD_INFO("lm90", 0x4c) },
		{ I2C_BOARD_INFO("lm90", 0x4d) },
		{ I2C_BOARD_INFO("adm1021", 0x18) },
		{ I2C_BOARD_INFO("adm1021", 0x19) },
		{ I2C_BOARD_INFO("adm1021", 0x1a) },
		{ I2C_BOARD_INFO("adm1021", 0x29) },
		{ I2C_BOARD_INFO("adm1021", 0x2a) },
		{ I2C_BOARD_INFO("adm1021", 0x2b) },
		{ I2C_BOARD_INFO("adm1021", 0x4c) },
		{ I2C_BOARD_INFO("adm1021", 0x4d) },
		{ I2C_BOARD_INFO("adm1021", 0x4e) },
		{ I2C_BOARD_INFO("lm63", 0x18) },
		{ I2C_BOARD_INFO("lm63", 0x4e) },
		{ }
	};

	if (!nvbios_extdev_find(bios, NVBIOS_EXTDEV_LM89, &extdev_entry)) {
		struct i2c_board_info board[] = {
			{ I2C_BOARD_INFO("lm90", extdev_entry.addr >> 1) },
			{ }
		};

		i2c->identify(i2c, NV_I2C_DEFAULT(0), "monitoring device",
				  board, probe_monitoring_device);
		if (priv->ic)
			return;
	}

	if (!nvbios_extdev_find(bios, NVBIOS_EXTDEV_ADT7473, &extdev_entry)) {
		struct i2c_board_info board[] = {
			{ I2C_BOARD_INFO("adt7473", extdev_entry.addr >> 1) },
			{ }
		};

		i2c->identify(i2c, NV_I2C_DEFAULT(0), "monitoring device",
				  board, probe_monitoring_device);
		if (priv->ic)
			return;
	}

	/* The vbios doesn't provide the address of an exisiting monitoring
	   device. Let's try our static list.
	 */
	i2c->identify(i2c, NV_I2C_DEFAULT(0), "monitoring device", info,
		      probe_monitoring_device);
}
