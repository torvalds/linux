/*
 * PowerNV sensor code
 *
 * Copyright (C) 2013 IBM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/delay.h>
#include <linux/of_platform.h>
#include <asm/opal.h>
#include <asm/machdep.h>

/*
 * This will return sensor information to driver based on the requested sensor
 * handle. A handle is an opaque id for the powernv, read by the driver from the
 * device tree..
 */
int opal_get_sensor_data(u32 sensor_hndl, u32 *sensor_data)
{
	int ret, token;
	struct opal_msg msg;
	__be32 data;

	token = opal_async_get_token_interruptible();
	if (token < 0)
		return token;

	ret = opal_sensor_read(sensor_hndl, token, &data);
	switch (ret) {
	case OPAL_ASYNC_COMPLETION:
		ret = opal_async_wait_response(token, &msg);
		if (ret) {
			pr_err("%s: Failed to wait for the async response, %d\n",
			       __func__, ret);
			goto out;
		}

		ret = opal_error_code(opal_get_async_rc(msg));
		*sensor_data = be32_to_cpu(data);
		break;

	case OPAL_SUCCESS:
		ret = 0;
		*sensor_data = be32_to_cpu(data);
		break;

	case OPAL_WRONG_STATE:
		ret = -EIO;
		break;

	default:
		ret = opal_error_code(ret);
		break;
	}

out:
	opal_async_release_token(token);
	return ret;
}
EXPORT_SYMBOL_GPL(opal_get_sensor_data);

int opal_get_sensor_data_u64(u32 sensor_hndl, u64 *sensor_data)
{
	int ret, token;
	struct opal_msg msg;
	__be64 data;

	if (!opal_check_token(OPAL_SENSOR_READ_U64)) {
		u32 sdata;

		ret = opal_get_sensor_data(sensor_hndl, &sdata);
		if (!ret)
			*sensor_data = sdata;
		return ret;
	}

	token = opal_async_get_token_interruptible();
	if (token < 0)
		return token;

	ret = opal_sensor_read_u64(sensor_hndl, token, &data);
	switch (ret) {
	case OPAL_ASYNC_COMPLETION:
		ret = opal_async_wait_response(token, &msg);
		if (ret) {
			pr_err("%s: Failed to wait for the async response, %d\n",
			       __func__, ret);
			goto out_token;
		}

		ret = opal_error_code(opal_get_async_rc(msg));
		*sensor_data = be64_to_cpu(data);
		break;

	case OPAL_SUCCESS:
		ret = 0;
		*sensor_data = be64_to_cpu(data);
		break;

	case OPAL_WRONG_STATE:
		ret = -EIO;
		break;

	default:
		ret = opal_error_code(ret);
		break;
	}

out_token:
	opal_async_release_token(token);
	return ret;
}
EXPORT_SYMBOL_GPL(opal_get_sensor_data_u64);

int __init opal_sensor_init(void)
{
	struct platform_device *pdev;
	struct device_node *sensor;

	sensor = of_find_node_by_path("/ibm,opal/sensors");
	if (!sensor) {
		pr_err("Opal node 'sensors' not found\n");
		return -ENODEV;
	}

	pdev = of_platform_device_create(sensor, "opal-sensor", NULL);
	of_node_put(sensor);

	return PTR_ERR_OR_ZERO(pdev);
}
