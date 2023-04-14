// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2017 Chelsio Communications.  All rights reserved.
 *
 *  Written by: Ganesh Goudar (ganeshgr@chelsio.com)
 */

#include "cxgb4.h"

#define CXGB4_NUM_TRIPS 1

static int cxgb4_thermal_get_temp(struct thermal_zone_device *tzdev,
				  int *temp)
{
	struct adapter *adap = thermal_zone_device_priv(tzdev);
	u32 param, val;
	int ret;

	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_DIAG) |
		 FW_PARAMS_PARAM_Y_V(FW_PARAM_DEV_DIAG_TMP));

	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 1,
			      &param, &val);
	if (ret < 0 || val == 0)
		return -1;

	*temp = val * 1000;
	return 0;
}

static struct thermal_zone_device_ops cxgb4_thermal_ops = {
	.get_temp = cxgb4_thermal_get_temp,
};

static struct thermal_trip trip = { .type = THERMAL_TRIP_CRITICAL } ;

int cxgb4_thermal_init(struct adapter *adap)
{
	struct ch_thermal *ch_thermal = &adap->ch_thermal;
	char ch_tz_name[THERMAL_NAME_LENGTH];
	int num_trip = CXGB4_NUM_TRIPS;
	u32 param, val;
	int ret;

	/* on older firmwares we may not get the trip temperature,
	 * set the num of trips to 0.
	 */
	param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) |
		 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_DIAG) |
		 FW_PARAMS_PARAM_Y_V(FW_PARAM_DEV_DIAG_MAXTMPTHRESH));

	ret = t4_query_params(adap, adap->mbox, adap->pf, 0, 1,
			      &param, &val);
	if (ret < 0) {
		num_trip = 0; /* could not get trip temperature */
	} else {
		trip.temperature = val * 1000;
	}

	snprintf(ch_tz_name, sizeof(ch_tz_name), "cxgb4_%s", adap->name);
	ch_thermal->tzdev = thermal_zone_device_register_with_trips(ch_tz_name, &trip, num_trip,
								    0, adap,
								    &cxgb4_thermal_ops,
								    NULL, 0, 0);
	if (IS_ERR(ch_thermal->tzdev)) {
		ret = PTR_ERR(ch_thermal->tzdev);
		dev_err(adap->pdev_dev, "Failed to register thermal zone\n");
		ch_thermal->tzdev = NULL;
		return ret;
	}

	ret = thermal_zone_device_enable(ch_thermal->tzdev);
	if (ret) {
		dev_err(adap->pdev_dev, "Failed to enable thermal zone\n");
		thermal_zone_device_unregister(adap->ch_thermal.tzdev);
		return ret;
	}

	return 0;
}

int cxgb4_thermal_remove(struct adapter *adap)
{
	if (adap->ch_thermal.tzdev) {
		thermal_zone_device_unregister(adap->ch_thermal.tzdev);
		adap->ch_thermal.tzdev = NULL;
	}
	return 0;
}
