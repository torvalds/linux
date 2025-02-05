// SPDX-License-Identifier: GPL-2.0
/*
 * Power Supply driver for a Greybus module.
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/greybus.h>

#define PROP_MAX 32

struct gb_power_supply_prop {
	enum power_supply_property	prop;
	u8				gb_prop;
	int				val;
	int				previous_val;
	bool				is_writeable;
};

struct gb_power_supply {
	u8				id;
	bool				registered;
	struct power_supply		*psy;
	struct power_supply_desc	desc;
	char				name[64];
	struct gb_power_supplies	*supplies;
	struct delayed_work		work;
	char				*manufacturer;
	char				*model_name;
	char				*serial_number;
	u8				type;
	u8				properties_count;
	u8				properties_count_str;
	unsigned long			last_update;
	u8				cache_invalid;
	unsigned int			update_interval;
	bool				changed;
	struct gb_power_supply_prop	*props;
	enum power_supply_property	*props_raw;
	bool				pm_acquired;
	struct mutex			supply_lock;
};

struct gb_power_supplies {
	struct gb_connection	*connection;
	u8			supplies_count;
	struct gb_power_supply	*supply;
	struct mutex		supplies_lock;
};

#define to_gb_power_supply(x) power_supply_get_drvdata(x)

/*
 * General power supply properties that could be absent from various reasons,
 * like kernel versions or vendor specific versions
 */
#ifndef POWER_SUPPLY_PROP_VOLTAGE_BOOT
	#define POWER_SUPPLY_PROP_VOLTAGE_BOOT	-1
#endif
#ifndef POWER_SUPPLY_PROP_CURRENT_BOOT
	#define POWER_SUPPLY_PROP_CURRENT_BOOT	-1
#endif
#ifndef POWER_SUPPLY_PROP_CALIBRATE
	#define POWER_SUPPLY_PROP_CALIBRATE	-1
#endif

/* cache time in milliseconds, if cache_time is set to 0 cache is disable */
static unsigned int cache_time = 1000;
/*
 * update interval initial and maximum value, between the two will
 * back-off exponential
 */
static unsigned int update_interval_init = 1 * HZ;
static unsigned int update_interval_max = 30 * HZ;

struct gb_power_supply_changes {
	enum power_supply_property	prop;
	u32				tolerance_change;
	void (*prop_changed)(struct gb_power_supply *gbpsy,
			     struct gb_power_supply_prop *prop);
};

static void gb_power_supply_state_change(struct gb_power_supply *gbpsy,
					 struct gb_power_supply_prop *prop);

static const struct gb_power_supply_changes psy_props_changes[] = {
	{	.prop			= GB_POWER_SUPPLY_PROP_STATUS,
		.tolerance_change	= 0,
		.prop_changed		= gb_power_supply_state_change,
	},
	{	.prop			= GB_POWER_SUPPLY_PROP_TEMP,
		.tolerance_change	= 500,
		.prop_changed		= NULL,
	},
	{	.prop			= GB_POWER_SUPPLY_PROP_ONLINE,
		.tolerance_change	= 0,
		.prop_changed		= NULL,
	},
};

static int get_psp_from_gb_prop(int gb_prop, enum power_supply_property *psp)
{
	int prop;

	switch (gb_prop) {
	case GB_POWER_SUPPLY_PROP_STATUS:
		prop = POWER_SUPPLY_PROP_STATUS;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_TYPE:
		prop = POWER_SUPPLY_PROP_CHARGE_TYPE;
		break;
	case GB_POWER_SUPPLY_PROP_HEALTH:
		prop = POWER_SUPPLY_PROP_HEALTH;
		break;
	case GB_POWER_SUPPLY_PROP_PRESENT:
		prop = POWER_SUPPLY_PROP_PRESENT;
		break;
	case GB_POWER_SUPPLY_PROP_ONLINE:
		prop = POWER_SUPPLY_PROP_ONLINE;
		break;
	case GB_POWER_SUPPLY_PROP_AUTHENTIC:
		prop = POWER_SUPPLY_PROP_AUTHENTIC;
		break;
	case GB_POWER_SUPPLY_PROP_TECHNOLOGY:
		prop = POWER_SUPPLY_PROP_TECHNOLOGY;
		break;
	case GB_POWER_SUPPLY_PROP_CYCLE_COUNT:
		prop = POWER_SUPPLY_PROP_CYCLE_COUNT;
		break;
	case GB_POWER_SUPPLY_PROP_VOLTAGE_MAX:
		prop = POWER_SUPPLY_PROP_VOLTAGE_MAX;
		break;
	case GB_POWER_SUPPLY_PROP_VOLTAGE_MIN:
		prop = POWER_SUPPLY_PROP_VOLTAGE_MIN;
		break;
	case GB_POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		prop = POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN;
		break;
	case GB_POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		prop = POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN;
		break;
	case GB_POWER_SUPPLY_PROP_VOLTAGE_NOW:
		prop = POWER_SUPPLY_PROP_VOLTAGE_NOW;
		break;
	case GB_POWER_SUPPLY_PROP_VOLTAGE_AVG:
		prop = POWER_SUPPLY_PROP_VOLTAGE_AVG;
		break;
	case GB_POWER_SUPPLY_PROP_VOLTAGE_OCV:
		prop = POWER_SUPPLY_PROP_VOLTAGE_OCV;
		break;
	case GB_POWER_SUPPLY_PROP_VOLTAGE_BOOT:
		prop = POWER_SUPPLY_PROP_VOLTAGE_BOOT;
		break;
	case GB_POWER_SUPPLY_PROP_CURRENT_MAX:
		prop = POWER_SUPPLY_PROP_CURRENT_MAX;
		break;
	case GB_POWER_SUPPLY_PROP_CURRENT_NOW:
		prop = POWER_SUPPLY_PROP_CURRENT_NOW;
		break;
	case GB_POWER_SUPPLY_PROP_CURRENT_AVG:
		prop = POWER_SUPPLY_PROP_CURRENT_AVG;
		break;
	case GB_POWER_SUPPLY_PROP_CURRENT_BOOT:
		prop = POWER_SUPPLY_PROP_CURRENT_BOOT;
		break;
	case GB_POWER_SUPPLY_PROP_POWER_NOW:
		prop = POWER_SUPPLY_PROP_POWER_NOW;
		break;
	case GB_POWER_SUPPLY_PROP_POWER_AVG:
		prop = POWER_SUPPLY_PROP_POWER_AVG;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		prop = POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN:
		prop = POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_FULL:
		prop = POWER_SUPPLY_PROP_CHARGE_FULL;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_EMPTY:
		prop = POWER_SUPPLY_PROP_CHARGE_EMPTY;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_NOW:
		prop = POWER_SUPPLY_PROP_CHARGE_NOW;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_AVG:
		prop = POWER_SUPPLY_PROP_CHARGE_AVG;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_COUNTER:
		prop = POWER_SUPPLY_PROP_CHARGE_COUNTER;
		break;
	case GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		prop = POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT;
		break;
	case GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		prop = POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX;
		break;
	case GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		prop = POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE;
		break;
	case GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		prop = POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		prop = POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		prop = POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX;
		break;
	case GB_POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		prop = POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT;
		break;
	case GB_POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		prop = POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN;
		break;
	case GB_POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN:
		prop = POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN;
		break;
	case GB_POWER_SUPPLY_PROP_ENERGY_FULL:
		prop = POWER_SUPPLY_PROP_ENERGY_FULL;
		break;
	case GB_POWER_SUPPLY_PROP_ENERGY_EMPTY:
		prop = POWER_SUPPLY_PROP_ENERGY_EMPTY;
		break;
	case GB_POWER_SUPPLY_PROP_ENERGY_NOW:
		prop = POWER_SUPPLY_PROP_ENERGY_NOW;
		break;
	case GB_POWER_SUPPLY_PROP_ENERGY_AVG:
		prop = POWER_SUPPLY_PROP_ENERGY_AVG;
		break;
	case GB_POWER_SUPPLY_PROP_CAPACITY:
		prop = POWER_SUPPLY_PROP_CAPACITY;
		break;
	case GB_POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN:
		prop = POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN;
		break;
	case GB_POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX:
		prop = POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX;
		break;
	case GB_POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		prop = POWER_SUPPLY_PROP_CAPACITY_LEVEL;
		break;
	case GB_POWER_SUPPLY_PROP_TEMP:
		prop = POWER_SUPPLY_PROP_TEMP;
		break;
	case GB_POWER_SUPPLY_PROP_TEMP_MAX:
		prop = POWER_SUPPLY_PROP_TEMP_MAX;
		break;
	case GB_POWER_SUPPLY_PROP_TEMP_MIN:
		prop = POWER_SUPPLY_PROP_TEMP_MIN;
		break;
	case GB_POWER_SUPPLY_PROP_TEMP_ALERT_MIN:
		prop = POWER_SUPPLY_PROP_TEMP_ALERT_MIN;
		break;
	case GB_POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		prop = POWER_SUPPLY_PROP_TEMP_ALERT_MAX;
		break;
	case GB_POWER_SUPPLY_PROP_TEMP_AMBIENT:
		prop = POWER_SUPPLY_PROP_TEMP_AMBIENT;
		break;
	case GB_POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MIN:
		prop = POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MIN;
		break;
	case GB_POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MAX:
		prop = POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MAX;
		break;
	case GB_POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		prop = POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW;
		break;
	case GB_POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		prop = POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG;
		break;
	case GB_POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		prop = POWER_SUPPLY_PROP_TIME_TO_FULL_NOW;
		break;
	case GB_POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		prop = POWER_SUPPLY_PROP_TIME_TO_FULL_AVG;
		break;
	case GB_POWER_SUPPLY_PROP_TYPE:
		prop = POWER_SUPPLY_PROP_TYPE;
		break;
	case GB_POWER_SUPPLY_PROP_SCOPE:
		prop = POWER_SUPPLY_PROP_SCOPE;
		break;
	case GB_POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		prop = POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT;
		break;
	case GB_POWER_SUPPLY_PROP_CALIBRATE:
		prop = POWER_SUPPLY_PROP_CALIBRATE;
		break;
	default:
		prop = -1;
		break;
	}

	if (prop < 0)
		return prop;

	*psp = (enum power_supply_property)prop;

	return 0;
}

static struct gb_connection *get_conn_from_psy(struct gb_power_supply *gbpsy)
{
	return gbpsy->supplies->connection;
}

static struct gb_power_supply_prop *get_psy_prop(struct gb_power_supply *gbpsy,
						 enum power_supply_property psp)
{
	int i;

	for (i = 0; i < gbpsy->properties_count; i++)
		if (gbpsy->props[i].prop == psp)
			return &gbpsy->props[i];
	return NULL;
}

static int is_psy_prop_writeable(struct gb_power_supply *gbpsy,
				     enum power_supply_property psp)
{
	struct gb_power_supply_prop *prop;

	prop = get_psy_prop(gbpsy, psp);
	if (!prop)
		return -ENOENT;
	return prop->is_writeable ? 1 : 0;
}

static int is_prop_valint(enum power_supply_property psp)
{
	return ((psp < POWER_SUPPLY_PROP_MODEL_NAME) ? 1 : 0);
}

static void next_interval(struct gb_power_supply *gbpsy)
{
	if (gbpsy->update_interval == update_interval_max)
		return;

	/* do some exponential back-off in the update interval */
	gbpsy->update_interval *= 2;
	if (gbpsy->update_interval > update_interval_max)
		gbpsy->update_interval = update_interval_max;
}

static void __gb_power_supply_changed(struct gb_power_supply *gbpsy)
{
	power_supply_changed(gbpsy->psy);
}

static void gb_power_supply_state_change(struct gb_power_supply *gbpsy,
					 struct gb_power_supply_prop *prop)
{
	struct gb_connection *connection = get_conn_from_psy(gbpsy);
	int ret;

	/*
	 * Check gbpsy->pm_acquired to make sure only one pair of 'get_sync'
	 * and 'put_autosuspend' runtime pm call for state property change.
	 */
	mutex_lock(&gbpsy->supply_lock);

	if ((prop->val == GB_POWER_SUPPLY_STATUS_CHARGING) &&
	    !gbpsy->pm_acquired) {
		ret = gb_pm_runtime_get_sync(connection->bundle);
		if (ret)
			dev_err(&connection->bundle->dev,
				"Fail to set wake lock for charging state\n");
		else
			gbpsy->pm_acquired = true;
	} else {
		if (gbpsy->pm_acquired) {
			ret = gb_pm_runtime_put_autosuspend(connection->bundle);
			if (ret)
				dev_err(&connection->bundle->dev,
					"Fail to set wake unlock for none charging\n");
			else
				gbpsy->pm_acquired = false;
		}
	}

	mutex_unlock(&gbpsy->supply_lock);
}

static void check_changed(struct gb_power_supply *gbpsy,
			  struct gb_power_supply_prop *prop)
{
	const struct gb_power_supply_changes *psyc;
	int val = prop->val;
	int prev_val = prop->previous_val;
	bool changed = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(psy_props_changes); i++) {
		psyc = &psy_props_changes[i];
		if (prop->prop == psyc->prop) {
			if (!psyc->tolerance_change)
				changed = true;
			else if (val < prev_val &&
				 prev_val - val > psyc->tolerance_change)
				changed = true;
			else if (val > prev_val &&
				 val - prev_val > psyc->tolerance_change)
				changed = true;

			if (changed && psyc->prop_changed)
				psyc->prop_changed(gbpsy, prop);

			if (changed)
				gbpsy->changed = true;
			break;
		}
	}
}

static int total_props(struct gb_power_supply *gbpsy)
{
	/* this return the intval plus the strval properties */
	return (gbpsy->properties_count + gbpsy->properties_count_str);
}

static void prop_append(struct gb_power_supply *gbpsy,
			enum power_supply_property prop)
{
	enum power_supply_property *new_props_raw;

	gbpsy->properties_count_str++;
	new_props_raw = krealloc(gbpsy->props_raw, total_props(gbpsy) *
				 sizeof(enum power_supply_property),
				 GFP_KERNEL);
	if (!new_props_raw)
		return;
	gbpsy->props_raw = new_props_raw;
	gbpsy->props_raw[total_props(gbpsy) - 1] = prop;
}

static int __gb_power_supply_set_name(char *init_name, char *name, size_t len)
{
	unsigned int i = 0;
	int ret = 0;
	struct power_supply *psy;

	if (!strlen(init_name))
		init_name = "gb_power_supply";
	strscpy(name, init_name, len);

	while ((ret < len) && (psy = power_supply_get_by_name(name))) {
		power_supply_put(psy);

		ret = snprintf(name, len, "%s_%u", init_name, ++i);
	}
	if (ret >= len)
		return -ENOMEM;
	return i;
}

static void _gb_power_supply_append_props(struct gb_power_supply *gbpsy)
{
	if (strlen(gbpsy->manufacturer))
		prop_append(gbpsy, POWER_SUPPLY_PROP_MANUFACTURER);
	if (strlen(gbpsy->model_name))
		prop_append(gbpsy, POWER_SUPPLY_PROP_MODEL_NAME);
	if (strlen(gbpsy->serial_number))
		prop_append(gbpsy, POWER_SUPPLY_PROP_SERIAL_NUMBER);
}

static int gb_power_supply_description_get(struct gb_power_supply *gbpsy)
{
	struct gb_connection *connection = get_conn_from_psy(gbpsy);
	struct gb_power_supply_get_description_request req;
	struct gb_power_supply_get_description_response resp;
	int ret;

	req.psy_id = gbpsy->id;

	ret = gb_operation_sync(connection,
				GB_POWER_SUPPLY_TYPE_GET_DESCRIPTION,
				&req, sizeof(req), &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	gbpsy->manufacturer = kstrndup(resp.manufacturer, PROP_MAX, GFP_KERNEL);
	if (!gbpsy->manufacturer)
		return -ENOMEM;
	gbpsy->model_name = kstrndup(resp.model, PROP_MAX, GFP_KERNEL);
	if (!gbpsy->model_name)
		return -ENOMEM;
	gbpsy->serial_number = kstrndup(resp.serial_number, PROP_MAX,
				       GFP_KERNEL);
	if (!gbpsy->serial_number)
		return -ENOMEM;

	gbpsy->type = le16_to_cpu(resp.type);
	gbpsy->properties_count = resp.properties_count;

	return 0;
}

static int gb_power_supply_prop_descriptors_get(struct gb_power_supply *gbpsy)
{
	struct gb_connection *connection = get_conn_from_psy(gbpsy);
	struct gb_power_supply_get_property_descriptors_request *req;
	struct gb_power_supply_get_property_descriptors_response *resp;
	struct gb_operation *op;
	u8 props_count = gbpsy->properties_count;
	enum power_supply_property psp;
	int ret;
	int i, r = 0;

	if (props_count == 0)
		return 0;

	op = gb_operation_create(connection,
				 GB_POWER_SUPPLY_TYPE_GET_PROP_DESCRIPTORS,
				 sizeof(*req),
				 struct_size(resp, props, props_count),
				 GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	req = op->request->payload;
	req->psy_id = gbpsy->id;

	ret = gb_operation_request_send_sync(op);
	if (ret < 0)
		goto out_put_operation;

	resp = op->response->payload;

	/* validate received properties */
	for (i = 0; i < props_count; i++) {
		ret = get_psp_from_gb_prop(resp->props[i].property, &psp);
		if (ret < 0) {
			dev_warn(&connection->bundle->dev,
				 "greybus property %u it is not supported by this kernel, dropped\n",
				 resp->props[i].property);
			gbpsy->properties_count--;
		}
	}

	gbpsy->props = kcalloc(gbpsy->properties_count, sizeof(*gbpsy->props),
			      GFP_KERNEL);
	if (!gbpsy->props) {
		ret = -ENOMEM;
		goto out_put_operation;
	}

	gbpsy->props_raw = kcalloc(gbpsy->properties_count,
				   sizeof(*gbpsy->props_raw), GFP_KERNEL);
	if (!gbpsy->props_raw) {
		ret = -ENOMEM;
		goto out_put_operation;
	}

	/* Store available properties, skip the ones we do not support */
	for (i = 0; i < props_count; i++) {
		ret = get_psp_from_gb_prop(resp->props[i].property, &psp);
		if (ret < 0) {
			r++;
			continue;
		}
		gbpsy->props[i - r].prop = psp;
		gbpsy->props[i - r].gb_prop = resp->props[i].property;
		gbpsy->props_raw[i - r] = psp;
		if (resp->props[i].is_writeable)
			gbpsy->props[i - r].is_writeable = true;
	}

	/*
	 * now append the properties that we already got information in the
	 * get_description operation. (char * ones)
	 */
	_gb_power_supply_append_props(gbpsy);

	ret = 0;
out_put_operation:
	gb_operation_put(op);

	return ret;
}

static int __gb_power_supply_property_update(struct gb_power_supply *gbpsy,
					     enum power_supply_property psp)
{
	struct gb_connection *connection = get_conn_from_psy(gbpsy);
	struct gb_power_supply_prop *prop;
	struct gb_power_supply_get_property_request req;
	struct gb_power_supply_get_property_response resp;
	int val;
	int ret;

	prop = get_psy_prop(gbpsy, psp);
	if (!prop)
		return -EINVAL;
	req.psy_id = gbpsy->id;
	req.property = prop->gb_prop;

	ret = gb_operation_sync(connection, GB_POWER_SUPPLY_TYPE_GET_PROPERTY,
				&req, sizeof(req), &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	val = le32_to_cpu(resp.prop_val);
	if (val == prop->val)
		return 0;

	prop->previous_val = prop->val;
	prop->val = val;

	check_changed(gbpsy, prop);

	return 0;
}

static int __gb_power_supply_property_get(struct gb_power_supply *gbpsy,
					  enum power_supply_property psp,
					  union power_supply_propval *val)
{
	struct gb_power_supply_prop *prop;

	prop = get_psy_prop(gbpsy, psp);
	if (!prop)
		return -EINVAL;

	val->intval = prop->val;
	return 0;
}

static int __gb_power_supply_property_strval_get(struct gb_power_supply *gbpsy,
						enum power_supply_property psp,
						union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = gbpsy->model_name;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = gbpsy->manufacturer;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = gbpsy->serial_number;
		break;
	default:
		break;
	}

	return 0;
}

static int _gb_power_supply_property_get(struct gb_power_supply *gbpsy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct gb_connection *connection = get_conn_from_psy(gbpsy);
	int ret;

	/*
	 * Properties of type const char *, were already fetched on
	 * get_description operation and should be cached in gb
	 */
	if (is_prop_valint(psp))
		ret = __gb_power_supply_property_get(gbpsy, psp, val);
	else
		ret = __gb_power_supply_property_strval_get(gbpsy, psp, val);

	if (ret < 0)
		dev_err(&connection->bundle->dev, "get property %u\n", psp);

	return 0;
}

static int is_cache_valid(struct gb_power_supply *gbpsy)
{
	/* check if cache is good enough or it has expired */
	if (gbpsy->cache_invalid) {
		gbpsy->cache_invalid = 0;
		return 0;
	}

	if (gbpsy->last_update &&
	    time_is_after_jiffies(gbpsy->last_update +
				  msecs_to_jiffies(cache_time)))
		return 1;

	return 0;
}

static int gb_power_supply_status_get(struct gb_power_supply *gbpsy)
{
	struct gb_connection *connection = get_conn_from_psy(gbpsy);
	int ret = 0;
	int i;

	if (is_cache_valid(gbpsy))
		return 0;

	ret = gb_pm_runtime_get_sync(connection->bundle);
	if (ret)
		return ret;

	for (i = 0; i < gbpsy->properties_count; i++) {
		ret = __gb_power_supply_property_update(gbpsy,
							gbpsy->props[i].prop);
		if (ret < 0)
			break;
	}

	if (ret == 0)
		gbpsy->last_update = jiffies;

	gb_pm_runtime_put_autosuspend(connection->bundle);
	return ret;
}

static void gb_power_supply_status_update(struct gb_power_supply *gbpsy)
{
	/* check if there a change that need to be reported */
	gb_power_supply_status_get(gbpsy);

	if (!gbpsy->changed)
		return;

	gbpsy->update_interval = update_interval_init;
	__gb_power_supply_changed(gbpsy);
	gbpsy->changed = false;
}

static void gb_power_supply_work(struct work_struct *work)
{
	struct gb_power_supply *gbpsy = container_of(work,
						     struct gb_power_supply,
						     work.work);

	/*
	 * if the poll interval is not set, disable polling, this is helpful
	 * specially at unregister time.
	 */
	if (!gbpsy->update_interval)
		return;

	gb_power_supply_status_update(gbpsy);
	next_interval(gbpsy);
	schedule_delayed_work(&gbpsy->work, gbpsy->update_interval);
}

static int get_property(struct power_supply *b,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct gb_power_supply *gbpsy = to_gb_power_supply(b);

	gb_power_supply_status_get(gbpsy);

	return _gb_power_supply_property_get(gbpsy, psp, val);
}

static int gb_power_supply_property_set(struct gb_power_supply *gbpsy,
					enum power_supply_property psp,
					int val)
{
	struct gb_connection *connection = get_conn_from_psy(gbpsy);
	struct gb_power_supply_prop *prop;
	struct gb_power_supply_set_property_request req;
	int ret;

	ret = gb_pm_runtime_get_sync(connection->bundle);
	if (ret)
		return ret;

	prop = get_psy_prop(gbpsy, psp);
	if (!prop) {
		ret = -EINVAL;
		goto out;
	}

	req.psy_id = gbpsy->id;
	req.property = prop->gb_prop;
	req.prop_val = cpu_to_le32((s32)val);

	ret = gb_operation_sync(connection, GB_POWER_SUPPLY_TYPE_SET_PROPERTY,
				&req, sizeof(req), NULL, 0);
	if (ret < 0)
		goto out;

	/* cache immediately the new value */
	prop->val = val;

out:
	gb_pm_runtime_put_autosuspend(connection->bundle);
	return ret;
}

static int set_property(struct power_supply *b,
			enum power_supply_property psp,
			const union power_supply_propval *val)
{
	struct gb_power_supply *gbpsy = to_gb_power_supply(b);

	return gb_power_supply_property_set(gbpsy, psp, val->intval);
}

static int property_is_writeable(struct power_supply *b,
				 enum power_supply_property psp)
{
	struct gb_power_supply *gbpsy = to_gb_power_supply(b);

	return is_psy_prop_writeable(gbpsy, psp);
}

static int gb_power_supply_register(struct gb_power_supply *gbpsy)
{
	struct gb_connection *connection = get_conn_from_psy(gbpsy);
	struct power_supply_config cfg = {};

	cfg.drv_data = gbpsy;

	gbpsy->desc.name		= gbpsy->name;
	gbpsy->desc.type		= gbpsy->type;
	gbpsy->desc.properties		= gbpsy->props_raw;
	gbpsy->desc.num_properties	= total_props(gbpsy);
	gbpsy->desc.get_property	= get_property;
	gbpsy->desc.set_property	= set_property;
	gbpsy->desc.property_is_writeable = property_is_writeable;

	gbpsy->psy = power_supply_register(&connection->bundle->dev,
					   &gbpsy->desc, &cfg);
	return PTR_ERR_OR_ZERO(gbpsy->psy);
}

static void _gb_power_supply_free(struct gb_power_supply *gbpsy)
{
	kfree(gbpsy->serial_number);
	kfree(gbpsy->model_name);
	kfree(gbpsy->manufacturer);
	kfree(gbpsy->props_raw);
	kfree(gbpsy->props);
}

static void _gb_power_supply_release(struct gb_power_supply *gbpsy)
{
	gbpsy->update_interval = 0;

	cancel_delayed_work_sync(&gbpsy->work);

	if (gbpsy->registered)
		power_supply_unregister(gbpsy->psy);

	_gb_power_supply_free(gbpsy);
}

static void _gb_power_supplies_release(struct gb_power_supplies *supplies)
{
	int i;

	if (!supplies->supply)
		return;

	mutex_lock(&supplies->supplies_lock);
	for (i = 0; i < supplies->supplies_count; i++)
		_gb_power_supply_release(&supplies->supply[i]);
	kfree(supplies->supply);
	mutex_unlock(&supplies->supplies_lock);
	kfree(supplies);
}

static int gb_power_supplies_get_count(struct gb_power_supplies *supplies)
{
	struct gb_power_supply_get_supplies_response resp;
	int ret;

	ret = gb_operation_sync(supplies->connection,
				GB_POWER_SUPPLY_TYPE_GET_SUPPLIES,
				NULL, 0, &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	if  (!resp.supplies_count)
		return -EINVAL;

	supplies->supplies_count = resp.supplies_count;

	return ret;
}

static int gb_power_supply_config(struct gb_power_supplies *supplies, int id)
{
	struct gb_power_supply *gbpsy = &supplies->supply[id];
	int ret;

	gbpsy->supplies = supplies;
	gbpsy->id = id;

	ret = gb_power_supply_description_get(gbpsy);
	if (ret < 0)
		return ret;

	return gb_power_supply_prop_descriptors_get(gbpsy);
}

static int gb_power_supply_enable(struct gb_power_supply *gbpsy)
{
	int ret;

	/* guarantee that we have an unique name, before register */
	ret =  __gb_power_supply_set_name(gbpsy->model_name, gbpsy->name,
					  sizeof(gbpsy->name));
	if (ret < 0)
		return ret;

	mutex_init(&gbpsy->supply_lock);

	ret = gb_power_supply_register(gbpsy);
	if (ret < 0)
		return ret;

	gbpsy->update_interval = update_interval_init;
	INIT_DELAYED_WORK(&gbpsy->work, gb_power_supply_work);
	schedule_delayed_work(&gbpsy->work, 0);

	/* everything went fine, mark it for release code to know */
	gbpsy->registered = true;

	return 0;
}

static int gb_power_supplies_setup(struct gb_power_supplies *supplies)
{
	struct gb_connection *connection = supplies->connection;
	int ret;
	int i;

	mutex_lock(&supplies->supplies_lock);

	ret = gb_power_supplies_get_count(supplies);
	if (ret < 0)
		goto out;

	supplies->supply = kcalloc(supplies->supplies_count,
				     sizeof(struct gb_power_supply),
				     GFP_KERNEL);

	if (!supplies->supply) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < supplies->supplies_count; i++) {
		ret = gb_power_supply_config(supplies, i);
		if (ret < 0) {
			dev_err(&connection->bundle->dev,
				"Fail to configure supplies devices\n");
			goto out;
		}
	}
out:
	mutex_unlock(&supplies->supplies_lock);
	return ret;
}

static int gb_power_supplies_register(struct gb_power_supplies *supplies)
{
	struct gb_connection *connection = supplies->connection;
	int ret = 0;
	int i;

	mutex_lock(&supplies->supplies_lock);

	for (i = 0; i < supplies->supplies_count; i++) {
		ret = gb_power_supply_enable(&supplies->supply[i]);
		if (ret < 0) {
			dev_err(&connection->bundle->dev,
				"Fail to enable supplies devices\n");
			break;
		}
	}

	mutex_unlock(&supplies->supplies_lock);
	return ret;
}

static int gb_supplies_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_power_supplies *supplies = gb_connection_get_data(connection);
	struct gb_power_supply *gbpsy;
	struct gb_message *request;
	struct gb_power_supply_event_request *payload;
	u8 psy_id;
	u8 event;
	int ret = 0;

	if (op->type != GB_POWER_SUPPLY_TYPE_EVENT) {
		dev_err(&connection->bundle->dev,
			"Unsupported unsolicited event: %u\n", op->type);
		return -EINVAL;
	}

	request = op->request;

	if (request->payload_size < sizeof(*payload)) {
		dev_err(&connection->bundle->dev,
			"Wrong event size received (%zu < %zu)\n",
			request->payload_size, sizeof(*payload));
		return -EINVAL;
	}

	payload = request->payload;
	psy_id = payload->psy_id;
	mutex_lock(&supplies->supplies_lock);
	if (psy_id >= supplies->supplies_count ||
	    !supplies->supply[psy_id].registered) {
		dev_err(&connection->bundle->dev,
			"Event received for unconfigured power_supply id: %d\n",
			psy_id);
		ret = -EINVAL;
		goto out_unlock;
	}

	event = payload->event;
	/*
	 * we will only handle events after setup is done and before release is
	 * running. For that just check update_interval.
	 */
	gbpsy = &supplies->supply[psy_id];
	if (!gbpsy->update_interval) {
		ret = -ESHUTDOWN;
		goto out_unlock;
	}

	if (event & GB_POWER_SUPPLY_UPDATE) {
		/*
		 * we need to make sure we invalidate cache, if not no new
		 * values for the properties will be fetch and the all propose
		 * of this event is missed
		 */
		gbpsy->cache_invalid = 1;
		gb_power_supply_status_update(gbpsy);
	}

out_unlock:
	mutex_unlock(&supplies->supplies_lock);
	return ret;
}

static int gb_power_supply_probe(struct gb_bundle *bundle,
				 const struct greybus_bundle_id *id)
{
	struct greybus_descriptor_cport *cport_desc;
	struct gb_connection *connection;
	struct gb_power_supplies *supplies;
	int ret;

	if (bundle->num_cports != 1)
		return -ENODEV;

	cport_desc = &bundle->cport_desc[0];
	if (cport_desc->protocol_id != GREYBUS_PROTOCOL_POWER_SUPPLY)
		return -ENODEV;

	supplies = kzalloc(sizeof(*supplies), GFP_KERNEL);
	if (!supplies)
		return -ENOMEM;

	connection = gb_connection_create(bundle, le16_to_cpu(cport_desc->id),
					  gb_supplies_request_handler);
	if (IS_ERR(connection)) {
		ret = PTR_ERR(connection);
		goto out;
	}

	supplies->connection = connection;
	gb_connection_set_data(connection, supplies);

	mutex_init(&supplies->supplies_lock);

	greybus_set_drvdata(bundle, supplies);

	/* We aren't ready to receive an incoming request yet */
	ret = gb_connection_enable_tx(connection);
	if (ret)
		goto error_connection_destroy;

	ret = gb_power_supplies_setup(supplies);
	if (ret < 0)
		goto error_connection_disable;

	/* We are ready to receive an incoming request now, enable RX as well */
	ret = gb_connection_enable(connection);
	if (ret)
		goto error_connection_disable;

	ret = gb_power_supplies_register(supplies);
	if (ret < 0)
		goto error_connection_disable;

	gb_pm_runtime_put_autosuspend(bundle);
	return 0;

error_connection_disable:
	gb_connection_disable(connection);
error_connection_destroy:
	gb_connection_destroy(connection);
out:
	_gb_power_supplies_release(supplies);
	return ret;
}

static void gb_power_supply_disconnect(struct gb_bundle *bundle)
{
	struct gb_power_supplies *supplies = greybus_get_drvdata(bundle);

	gb_connection_disable(supplies->connection);
	gb_connection_destroy(supplies->connection);

	_gb_power_supplies_release(supplies);
}

static const struct greybus_bundle_id gb_power_supply_id_table[] = {
	{ GREYBUS_DEVICE_CLASS(GREYBUS_CLASS_POWER_SUPPLY) },
	{ }
};
MODULE_DEVICE_TABLE(greybus, gb_power_supply_id_table);

static struct greybus_driver gb_power_supply_driver = {
	.name		= "power_supply",
	.probe		= gb_power_supply_probe,
	.disconnect	= gb_power_supply_disconnect,
	.id_table	= gb_power_supply_id_table,
};
module_greybus_driver(gb_power_supply_driver);

MODULE_DESCRIPTION("Power Supply driver for a Greybus module");
MODULE_LICENSE("GPL v2");
