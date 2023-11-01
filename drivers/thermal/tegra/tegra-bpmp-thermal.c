// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Mikko Perttunen <mperttunen@nvidia.com>
 *	Aapo Vienamo	<avienamo@nvidia.com>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

struct tegra_bpmp_thermal_zone {
	struct tegra_bpmp_thermal *tegra;
	struct thermal_zone_device *tzd;
	struct work_struct tz_device_update_work;
	unsigned int idx;
};

struct tegra_bpmp_thermal {
	struct device *dev;
	struct tegra_bpmp *bpmp;
	unsigned int num_zones;
	struct tegra_bpmp_thermal_zone **zones;
};

static int __tegra_bpmp_thermal_get_temp(struct tegra_bpmp_thermal_zone *zone,
					 int *out_temp)
{
	struct mrq_thermal_host_to_bpmp_request req;
	union mrq_thermal_bpmp_to_host_response reply;
	struct tegra_bpmp_message msg;
	int err;

	memset(&req, 0, sizeof(req));
	req.type = CMD_THERMAL_GET_TEMP;
	req.get_temp.zone = zone->idx;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_THERMAL;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);
	msg.rx.data = &reply;
	msg.rx.size = sizeof(reply);

	err = tegra_bpmp_transfer(zone->tegra->bpmp, &msg);
	if (err)
		return err;
	if (msg.rx.ret == -BPMP_EFAULT)
		return -EAGAIN;
	if (msg.rx.ret)
		return -EINVAL;

	*out_temp = reply.get_temp.temp;

	return 0;
}

static int tegra_bpmp_thermal_get_temp(struct thermal_zone_device *tz, int *out_temp)
{
	struct tegra_bpmp_thermal_zone *zone = thermal_zone_device_priv(tz);

	return __tegra_bpmp_thermal_get_temp(zone, out_temp);
}

static int tegra_bpmp_thermal_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct tegra_bpmp_thermal_zone *zone = thermal_zone_device_priv(tz);
	struct mrq_thermal_host_to_bpmp_request req;
	struct tegra_bpmp_message msg;
	int err;

	memset(&req, 0, sizeof(req));
	req.type = CMD_THERMAL_SET_TRIP;
	req.set_trip.zone = zone->idx;
	req.set_trip.enabled = true;
	req.set_trip.low = low;
	req.set_trip.high = high;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_THERMAL;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);

	err = tegra_bpmp_transfer(zone->tegra->bpmp, &msg);
	if (err)
		return err;
	if (msg.rx.ret)
		return -EINVAL;

	return 0;
}

static void tz_device_update_work_fn(struct work_struct *work)
{
	struct tegra_bpmp_thermal_zone *zone;

	zone = container_of(work, struct tegra_bpmp_thermal_zone,
			    tz_device_update_work);

	thermal_zone_device_update(zone->tzd, THERMAL_TRIP_VIOLATED);
}

static void bpmp_mrq_thermal(unsigned int mrq, struct tegra_bpmp_channel *ch,
			     void *data)
{
	struct mrq_thermal_bpmp_to_host_request req;
	struct tegra_bpmp_thermal *tegra = data;
	size_t offset;
	int i;

	offset = offsetof(struct tegra_bpmp_mb_data, data);
	iosys_map_memcpy_from(&req, &ch->ib, offset, sizeof(req));

	if (req.type != CMD_THERMAL_HOST_TRIP_REACHED) {
		dev_err(tegra->dev, "%s: invalid request type: %d\n", __func__, req.type);
		tegra_bpmp_mrq_return(ch, -EINVAL, NULL, 0);
		return;
	}

	for (i = 0; i < tegra->num_zones; ++i) {
		if (tegra->zones[i]->idx != req.host_trip_reached.zone)
			continue;

		schedule_work(&tegra->zones[i]->tz_device_update_work);
		tegra_bpmp_mrq_return(ch, 0, NULL, 0);
		return;
	}

	dev_err(tegra->dev, "%s: invalid thermal zone: %d\n", __func__,
		req.host_trip_reached.zone);
	tegra_bpmp_mrq_return(ch, -EINVAL, NULL, 0);
}

static int tegra_bpmp_thermal_get_num_zones(struct tegra_bpmp *bpmp,
					    int *num_zones)
{
	struct mrq_thermal_host_to_bpmp_request req;
	union mrq_thermal_bpmp_to_host_response reply;
	struct tegra_bpmp_message msg;
	int err;

	memset(&req, 0, sizeof(req));
	req.type = CMD_THERMAL_GET_NUM_ZONES;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_THERMAL;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);
	msg.rx.data = &reply;
	msg.rx.size = sizeof(reply);

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err)
		return err;
	if (msg.rx.ret)
		return -EINVAL;

	*num_zones = reply.get_num_zones.num;

	return 0;
}

static int tegra_bpmp_thermal_trips_supported(struct tegra_bpmp *bpmp, bool *supported)
{
	struct mrq_thermal_host_to_bpmp_request req;
	union mrq_thermal_bpmp_to_host_response reply;
	struct tegra_bpmp_message msg;
	int err;

	memset(&req, 0, sizeof(req));
	req.type = CMD_THERMAL_QUERY_ABI;
	req.query_abi.type = CMD_THERMAL_SET_TRIP;

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_THERMAL;
	msg.tx.data = &req;
	msg.tx.size = sizeof(req);
	msg.rx.data = &reply;
	msg.rx.size = sizeof(reply);

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err)
		return err;

	if (msg.rx.ret == 0) {
		*supported = true;
		return 0;
	} else if (msg.rx.ret == -BPMP_ENODEV) {
		*supported = false;
		return 0;
	} else {
		return -EINVAL;
	}
}

static const struct thermal_zone_device_ops tegra_bpmp_of_thermal_ops = {
	.get_temp = tegra_bpmp_thermal_get_temp,
	.set_trips = tegra_bpmp_thermal_set_trips,
};

static const struct thermal_zone_device_ops tegra_bpmp_of_thermal_ops_notrips = {
	.get_temp = tegra_bpmp_thermal_get_temp,
};

static int tegra_bpmp_thermal_probe(struct platform_device *pdev)
{
	struct tegra_bpmp *bpmp = dev_get_drvdata(pdev->dev.parent);
	const struct thermal_zone_device_ops *thermal_ops;
	struct tegra_bpmp_thermal *tegra;
	struct thermal_zone_device *tzd;
	unsigned int i, max_num_zones;
	bool supported;
	int err;

	err = tegra_bpmp_thermal_trips_supported(bpmp, &supported);
	if (err) {
		dev_err(&pdev->dev, "failed to determine if trip points are supported\n");
		return err;
	}

	if (supported)
		thermal_ops = &tegra_bpmp_of_thermal_ops;
	else
		thermal_ops = &tegra_bpmp_of_thermal_ops_notrips;

	tegra = devm_kzalloc(&pdev->dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	tegra->dev = &pdev->dev;
	tegra->bpmp = bpmp;

	err = tegra_bpmp_thermal_get_num_zones(bpmp, &max_num_zones);
	if (err) {
		dev_err(&pdev->dev, "failed to get the number of zones: %d\n",
			err);
		return err;
	}

	tegra->zones = devm_kcalloc(&pdev->dev, max_num_zones,
				    sizeof(*tegra->zones), GFP_KERNEL);
	if (!tegra->zones)
		return -ENOMEM;

	for (i = 0; i < max_num_zones; ++i) {
		struct tegra_bpmp_thermal_zone *zone;
		int temp;

		zone = devm_kzalloc(&pdev->dev, sizeof(*zone), GFP_KERNEL);
		if (!zone)
			return -ENOMEM;

		zone->idx = i;
		zone->tegra = tegra;

		err = __tegra_bpmp_thermal_get_temp(zone, &temp);

		/*
		 * Sensors in powergated domains may temporarily fail to be read
		 * (-EAGAIN), but will become accessible when the domain is powered on.
		 */
		if (err < 0 && err != -EAGAIN) {
			devm_kfree(&pdev->dev, zone);
			continue;
		}

		tzd = devm_thermal_of_zone_register(
			&pdev->dev, i, zone, thermal_ops);
		if (IS_ERR(tzd)) {
			if (PTR_ERR(tzd) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			devm_kfree(&pdev->dev, zone);
			continue;
		}

		zone->tzd = tzd;
		INIT_WORK(&zone->tz_device_update_work,
			  tz_device_update_work_fn);

		tegra->zones[tegra->num_zones++] = zone;
	}

	err = tegra_bpmp_request_mrq(bpmp, MRQ_THERMAL, bpmp_mrq_thermal,
				     tegra);
	if (err) {
		dev_err(&pdev->dev, "failed to register mrq handler: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, tegra);

	return 0;
}

static void tegra_bpmp_thermal_remove(struct platform_device *pdev)
{
	struct tegra_bpmp_thermal *tegra = platform_get_drvdata(pdev);

	tegra_bpmp_free_mrq(tegra->bpmp, MRQ_THERMAL, tegra);
}

static const struct of_device_id tegra_bpmp_thermal_of_match[] = {
	{ .compatible = "nvidia,tegra186-bpmp-thermal" },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_bpmp_thermal_of_match);

static struct platform_driver tegra_bpmp_thermal_driver = {
	.probe = tegra_bpmp_thermal_probe,
	.remove_new = tegra_bpmp_thermal_remove,
	.driver = {
		.name = "tegra-bpmp-thermal",
		.of_match_table = tegra_bpmp_thermal_of_match,
	},
};
module_platform_driver(tegra_bpmp_thermal_driver);

MODULE_AUTHOR("Mikko Perttunen <mperttunen@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra BPMP thermal sensor driver");
MODULE_LICENSE("GPL v2");
