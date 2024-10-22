// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * Generic netlink for thermal management framework
 */
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/kernel.h>
#include <net/sock.h>
#include <net/genetlink.h>
#include <uapi/linux/thermal.h>

#include "thermal_core.h"

static const struct genl_multicast_group thermal_genl_mcgrps[] = {
	[THERMAL_GENL_SAMPLING_GROUP] = { .name = THERMAL_GENL_SAMPLING_GROUP_NAME, },
	[THERMAL_GENL_EVENT_GROUP]  = { .name = THERMAL_GENL_EVENT_GROUP_NAME,  },
};

static const struct nla_policy thermal_genl_policy[THERMAL_GENL_ATTR_MAX + 1] = {
	/* Thermal zone */
	[THERMAL_GENL_ATTR_TZ]			= { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_TZ_ID]		= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TEMP]		= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TRIP]		= { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_TZ_TRIP_ID]		= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TRIP_TEMP]	= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TRIP_TYPE]	= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TRIP_HYST]	= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_MODE]		= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_CDEV_WEIGHT]	= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_NAME]		= { .type = NLA_STRING,
						    .len = THERMAL_NAME_LENGTH },
	/* Governor(s) */
	[THERMAL_GENL_ATTR_TZ_GOV]		= { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_TZ_GOV_NAME]		= { .type = NLA_STRING,
						    .len = THERMAL_NAME_LENGTH },
	/* Cooling devices */
	[THERMAL_GENL_ATTR_CDEV]		= { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_CDEV_ID]		= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_CDEV_CUR_STATE]	= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_CDEV_MAX_STATE]	= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_CDEV_NAME]		= { .type = NLA_STRING,
						    .len = THERMAL_NAME_LENGTH },
	/* CPU capabilities */
	[THERMAL_GENL_ATTR_CPU_CAPABILITY]		= { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_CPU_CAPABILITY_ID]		= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_CPU_CAPABILITY_PERFORMANCE]	= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_CPU_CAPABILITY_EFFICIENCY]	= { .type = NLA_U32 },

	/* Thresholds */
	[THERMAL_GENL_ATTR_THRESHOLD]		= { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_THRESHOLD_TEMP]	= { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_THRESHOLD_DIRECTION]	= { .type = NLA_U32 },
};

struct param {
	struct nlattr **attrs;
	struct sk_buff *msg;
	const char *name;
	int tz_id;
	int cdev_id;
	int trip_id;
	int trip_temp;
	int trip_type;
	int trip_hyst;
	int temp;
	int prev_temp;
	int direction;
	int cdev_state;
	int cdev_max_state;
	struct thermal_genl_cpu_caps *cpu_capabilities;
	int cpu_capabilities_count;
};

typedef int (*cb_t)(struct param *);

static struct genl_family thermal_genl_family;
static BLOCKING_NOTIFIER_HEAD(thermal_genl_chain);

static int thermal_group_has_listeners(enum thermal_genl_multicast_groups group)
{
	return genl_has_listeners(&thermal_genl_family, &init_net, group);
}

/************************** Sampling encoding *******************************/

int thermal_genl_sampling_temp(int id, int temp)
{
	struct sk_buff *skb;
	void *hdr;

	if (!thermal_group_has_listeners(THERMAL_GENL_SAMPLING_GROUP))
		return 0;

	skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_put(skb, 0, 0, &thermal_genl_family, 0,
			  THERMAL_GENL_SAMPLING_TEMP);
	if (!hdr)
		goto out_free;

	if (nla_put_u32(skb, THERMAL_GENL_ATTR_TZ_ID, id))
		goto out_cancel;

	if (nla_put_u32(skb, THERMAL_GENL_ATTR_TZ_TEMP, temp))
		goto out_cancel;

	genlmsg_end(skb, hdr);

	genlmsg_multicast(&thermal_genl_family, skb, 0, THERMAL_GENL_SAMPLING_GROUP, GFP_KERNEL);

	return 0;
out_cancel:
	genlmsg_cancel(skb, hdr);
out_free:
	nlmsg_free(skb);

	return -EMSGSIZE;
}

/**************************** Event encoding *********************************/

static int thermal_genl_event_tz_create(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id) ||
	    nla_put_string(p->msg, THERMAL_GENL_ATTR_TZ_NAME, p->name))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_tz(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_tz_trip_up(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_ID, p->trip_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TEMP, p->temp))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_tz_trip_change(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_ID, p->trip_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_TYPE, p->trip_type) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_TEMP, p->trip_temp) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_HYST, p->trip_hyst))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_cdev_add(struct param *p)
{
	if (nla_put_string(p->msg, THERMAL_GENL_ATTR_CDEV_NAME,
			   p->name) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_CDEV_ID,
			p->cdev_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_CDEV_MAX_STATE,
			p->cdev_max_state))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_cdev_delete(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_CDEV_ID, p->cdev_id))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_cdev_state_update(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_CDEV_ID,
			p->cdev_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_CDEV_CUR_STATE,
			p->cdev_state))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_gov_change(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id) ||
	    nla_put_string(p->msg, THERMAL_GENL_ATTR_GOV_NAME, p->name))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_cpu_capability_change(struct param *p)
{
	struct thermal_genl_cpu_caps *cpu_cap = p->cpu_capabilities;
	struct sk_buff *msg = p->msg;
	struct nlattr *start_cap;
	int i;

	start_cap = nla_nest_start(msg, THERMAL_GENL_ATTR_CPU_CAPABILITY);
	if (!start_cap)
		return -EMSGSIZE;

	for (i = 0; i < p->cpu_capabilities_count; ++i) {
		if (nla_put_u32(msg, THERMAL_GENL_ATTR_CPU_CAPABILITY_ID,
				cpu_cap->cpu))
			goto out_cancel_nest;

		if (nla_put_u32(msg, THERMAL_GENL_ATTR_CPU_CAPABILITY_PERFORMANCE,
				cpu_cap->performance))
			goto out_cancel_nest;

		if (nla_put_u32(msg, THERMAL_GENL_ATTR_CPU_CAPABILITY_EFFICIENCY,
				cpu_cap->efficiency))
			goto out_cancel_nest;

		++cpu_cap;
	}

	nla_nest_end(msg, start_cap);

	return 0;
out_cancel_nest:
	nla_nest_cancel(msg, start_cap);

	return -EMSGSIZE;
}

static int thermal_genl_event_threshold_add(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_THRESHOLD_TEMP, p->temp) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_THRESHOLD_DIRECTION, p->direction))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_threshold_flush(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_threshold_up(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_PREV_TEMP, p->prev_temp) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TEMP, p->temp))
		return -EMSGSIZE;

	return 0;
}

int thermal_genl_event_tz_delete(struct param *p)
	__attribute__((alias("thermal_genl_event_tz")));

int thermal_genl_event_tz_enable(struct param *p)
	__attribute__((alias("thermal_genl_event_tz")));

int thermal_genl_event_tz_disable(struct param *p)
	__attribute__((alias("thermal_genl_event_tz")));

int thermal_genl_event_tz_trip_down(struct param *p)
	__attribute__((alias("thermal_genl_event_tz_trip_up")));

int thermal_genl_event_threshold_delete(struct param *p)
	__attribute__((alias("thermal_genl_event_threshold_add")));

int thermal_genl_event_threshold_down(struct param *p)
	__attribute__((alias("thermal_genl_event_threshold_up")));

static cb_t event_cb[] = {
	[THERMAL_GENL_EVENT_TZ_CREATE]		= thermal_genl_event_tz_create,
	[THERMAL_GENL_EVENT_TZ_DELETE]		= thermal_genl_event_tz_delete,
	[THERMAL_GENL_EVENT_TZ_ENABLE]		= thermal_genl_event_tz_enable,
	[THERMAL_GENL_EVENT_TZ_DISABLE]		= thermal_genl_event_tz_disable,
	[THERMAL_GENL_EVENT_TZ_TRIP_UP]		= thermal_genl_event_tz_trip_up,
	[THERMAL_GENL_EVENT_TZ_TRIP_DOWN]	= thermal_genl_event_tz_trip_down,
	[THERMAL_GENL_EVENT_TZ_TRIP_CHANGE]	= thermal_genl_event_tz_trip_change,
	[THERMAL_GENL_EVENT_CDEV_ADD]		= thermal_genl_event_cdev_add,
	[THERMAL_GENL_EVENT_CDEV_DELETE]	= thermal_genl_event_cdev_delete,
	[THERMAL_GENL_EVENT_CDEV_STATE_UPDATE]	= thermal_genl_event_cdev_state_update,
	[THERMAL_GENL_EVENT_TZ_GOV_CHANGE]	= thermal_genl_event_gov_change,
	[THERMAL_GENL_EVENT_CPU_CAPABILITY_CHANGE] = thermal_genl_event_cpu_capability_change,
	[THERMAL_GENL_EVENT_THRESHOLD_ADD]	= thermal_genl_event_threshold_add,
	[THERMAL_GENL_EVENT_THRESHOLD_DELETE]	= thermal_genl_event_threshold_delete,
	[THERMAL_GENL_EVENT_THRESHOLD_FLUSH]	= thermal_genl_event_threshold_flush,
	[THERMAL_GENL_EVENT_THRESHOLD_DOWN]	= thermal_genl_event_threshold_down,
	[THERMAL_GENL_EVENT_THRESHOLD_UP]	= thermal_genl_event_threshold_up,
};

/*
 * Generic netlink event encoding
 */
static int thermal_genl_send_event(enum thermal_genl_event event,
				   struct param *p)
{
	struct sk_buff *msg;
	int ret = -EMSGSIZE;
	void *hdr;

	if (!thermal_group_has_listeners(THERMAL_GENL_EVENT_GROUP))
		return 0;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	p->msg = msg;

	hdr = genlmsg_put(msg, 0, 0, &thermal_genl_family, 0, event);
	if (!hdr)
		goto out_free_msg;

	ret = event_cb[event](p);
	if (ret)
		goto out_cancel_msg;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&thermal_genl_family, msg, 0, THERMAL_GENL_EVENT_GROUP, GFP_KERNEL);

	return 0;

out_cancel_msg:
	genlmsg_cancel(msg, hdr);
out_free_msg:
	nlmsg_free(msg);

	return ret;
}

int thermal_notify_tz_create(const struct thermal_zone_device *tz)
{
	struct param p = { .tz_id = tz->id, .name = tz->type };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_CREATE, &p);
}

int thermal_notify_tz_delete(const struct thermal_zone_device *tz)
{
	struct param p = { .tz_id = tz->id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_DELETE, &p);
}

int thermal_notify_tz_enable(const struct thermal_zone_device *tz)
{
	struct param p = { .tz_id = tz->id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_ENABLE, &p);
}

int thermal_notify_tz_disable(const struct thermal_zone_device *tz)
{
	struct param p = { .tz_id = tz->id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_DISABLE, &p);
}

int thermal_notify_tz_trip_down(const struct thermal_zone_device *tz,
				const struct thermal_trip *trip)
{
	struct param p = { .tz_id = tz->id,
			   .trip_id = thermal_zone_trip_id(tz, trip),
			   .temp = tz->temperature };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_TRIP_DOWN, &p);
}

int thermal_notify_tz_trip_up(const struct thermal_zone_device *tz,
			      const struct thermal_trip *trip)
{
	struct param p = { .tz_id = tz->id,
			   .trip_id = thermal_zone_trip_id(tz, trip),
			   .temp = tz->temperature };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_TRIP_UP, &p);
}

int thermal_notify_tz_trip_change(const struct thermal_zone_device *tz,
				  const struct thermal_trip *trip)
{
	struct param p = { .tz_id = tz->id,
			   .trip_id = thermal_zone_trip_id(tz, trip),
			   .trip_type = trip->type,
			   .trip_temp = trip->temperature,
			   .trip_hyst = trip->hysteresis };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_TRIP_CHANGE, &p);
}

int thermal_notify_cdev_state_update(const struct thermal_cooling_device *cdev,
				     int state)
{
	struct param p = { .cdev_id = cdev->id, .cdev_state = state };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_CDEV_STATE_UPDATE, &p);
}

int thermal_notify_cdev_add(const struct thermal_cooling_device *cdev)
{
	struct param p = { .cdev_id = cdev->id, .name = cdev->type,
			   .cdev_max_state = cdev->max_state };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_CDEV_ADD, &p);
}

int thermal_notify_cdev_delete(const struct thermal_cooling_device *cdev)
{
	struct param p = { .cdev_id = cdev->id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_CDEV_DELETE, &p);
}

int thermal_notify_tz_gov_change(const struct thermal_zone_device *tz,
				 const char *name)
{
	struct param p = { .tz_id = tz->id, .name = name };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_GOV_CHANGE, &p);
}

int thermal_genl_cpu_capability_event(int count,
				      struct thermal_genl_cpu_caps *caps)
{
	struct param p = { .cpu_capabilities_count = count, .cpu_capabilities = caps };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_CPU_CAPABILITY_CHANGE, &p);
}
EXPORT_SYMBOL_GPL(thermal_genl_cpu_capability_event);

int thermal_notify_threshold_add(const struct thermal_zone_device *tz,
				 int temperature, int direction)
{
	struct param p = { .tz_id = tz->id, .temp = temperature, .direction = direction };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_THRESHOLD_ADD, &p);
}

int thermal_notify_threshold_delete(const struct thermal_zone_device *tz,
				    int temperature, int direction)
{
	struct param p = { .tz_id = tz->id, .temp = temperature, .direction = direction };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_THRESHOLD_DELETE, &p);
}

int thermal_notify_threshold_flush(const struct thermal_zone_device *tz)
{
	struct param p = { .tz_id = tz->id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_THRESHOLD_FLUSH, &p);
}

int thermal_notify_threshold_down(const struct thermal_zone_device *tz)
{
	struct param p = { .tz_id = tz->id, .temp = tz->temperature, .prev_temp = tz->last_temperature };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_THRESHOLD_DOWN, &p);
}

int thermal_notify_threshold_up(const struct thermal_zone_device *tz)
{
	struct param p = { .tz_id = tz->id, .temp = tz->temperature, .prev_temp = tz->last_temperature };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_THRESHOLD_UP, &p);
}

/*************************** Command encoding ********************************/

static int __thermal_genl_cmd_tz_get_id(struct thermal_zone_device *tz,
					void *data)
{
	struct sk_buff *msg = data;

	if (nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_ID, tz->id) ||
	    nla_put_string(msg, THERMAL_GENL_ATTR_TZ_NAME, tz->type))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_cmd_tz_get_id(struct param *p)
{
	struct sk_buff *msg = p->msg;
	struct nlattr *start_tz;
	int ret;

	start_tz = nla_nest_start(msg, THERMAL_GENL_ATTR_TZ);
	if (!start_tz)
		return -EMSGSIZE;

	ret = for_each_thermal_zone(__thermal_genl_cmd_tz_get_id, msg);
	if (ret)
		goto out_cancel_nest;

	nla_nest_end(msg, start_tz);

	return 0;

out_cancel_nest:
	nla_nest_cancel(msg, start_tz);

	return ret;
}

static int thermal_genl_cmd_tz_get_trip(struct param *p)
{
	struct sk_buff *msg = p->msg;
	const struct thermal_trip_desc *td;
	struct nlattr *start_trip;
	int id;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	CLASS(thermal_zone_get_by_id, tz)(id);
	if (!tz)
		return -EINVAL;

	start_trip = nla_nest_start(msg, THERMAL_GENL_ATTR_TZ_TRIP);
	if (!start_trip)
		return -EMSGSIZE;

	guard(thermal_zone)(tz);

	for_each_trip_desc(tz, td) {
		const struct thermal_trip *trip = &td->trip;

		if (nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_TRIP_ID,
				thermal_zone_trip_id(tz, trip)) ||
		    nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_TRIP_TYPE, trip->type) ||
		    nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_TRIP_TEMP, trip->temperature) ||
		    nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_TRIP_HYST, trip->hysteresis))
			return -EMSGSIZE;
	}

	nla_nest_end(msg, start_trip);

	return 0;
}

static int thermal_genl_cmd_tz_get_temp(struct param *p)
{
	struct sk_buff *msg = p->msg;
	int temp, ret, id;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	CLASS(thermal_zone_get_by_id, tz)(id);
	if (!tz)
		return -EINVAL;

	ret = thermal_zone_get_temp(tz, &temp);
	if (ret)
		return ret;

	if (nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_ID, id) ||
	    nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_TEMP, temp))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_cmd_tz_get_gov(struct param *p)
{
	struct sk_buff *msg = p->msg;
	int id;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	CLASS(thermal_zone_get_by_id, tz)(id);
	if (!tz)
		return -EINVAL;

	guard(thermal_zone)(tz);

	if (nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_ID, id) ||
	    nla_put_string(msg, THERMAL_GENL_ATTR_TZ_GOV_NAME,
			   tz->governor->name))
		return -EMSGSIZE;

	return 0;
}

static int __thermal_genl_cmd_cdev_get(struct thermal_cooling_device *cdev,
				       void *data)
{
	struct sk_buff *msg = data;

	if (nla_put_u32(msg, THERMAL_GENL_ATTR_CDEV_ID, cdev->id))
		return -EMSGSIZE;

	if (nla_put_string(msg, THERMAL_GENL_ATTR_CDEV_NAME, cdev->type))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_cmd_cdev_get(struct param *p)
{
	struct sk_buff *msg = p->msg;
	struct nlattr *start_cdev;
	int ret;

	start_cdev = nla_nest_start(msg, THERMAL_GENL_ATTR_CDEV);
	if (!start_cdev)
		return -EMSGSIZE;

	ret = for_each_thermal_cooling_device(__thermal_genl_cmd_cdev_get, msg);
	if (ret)
		goto out_cancel_nest;

	nla_nest_end(msg, start_cdev);

	return 0;
out_cancel_nest:
	nla_nest_cancel(msg, start_cdev);

	return ret;
}

static int __thermal_genl_cmd_threshold_get(struct user_threshold *threshold, void *arg)
{
	struct sk_buff *msg = arg;

	if (nla_put_u32(msg, THERMAL_GENL_ATTR_THRESHOLD_TEMP, threshold->temperature) ||
	    nla_put_u32(msg, THERMAL_GENL_ATTR_THRESHOLD_DIRECTION, threshold->direction))
		return -1;

	return 0;
}

static int thermal_genl_cmd_threshold_get(struct param *p)
{
	struct sk_buff *msg = p->msg;
	struct nlattr *start_trip;
	int id, ret;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	CLASS(thermal_zone_get_by_id, tz)(id);
	if (!tz)
		return -EINVAL;

	start_trip = nla_nest_start(msg, THERMAL_GENL_ATTR_THRESHOLD);
	if (!start_trip)
		return -EMSGSIZE;

	ret = thermal_thresholds_for_each(tz, __thermal_genl_cmd_threshold_get, msg);
	if (ret)
		return -EMSGSIZE;

	nla_nest_end(msg, start_trip);

	return 0;
}

static int thermal_genl_cmd_threshold_add(struct param *p)
{
	int id, temp, direction;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID] ||
	    !p->attrs[THERMAL_GENL_ATTR_THRESHOLD_TEMP] ||
	    !p->attrs[THERMAL_GENL_ATTR_THRESHOLD_DIRECTION])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);
	temp = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_THRESHOLD_TEMP]);
	direction = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_THRESHOLD_DIRECTION]);

	CLASS(thermal_zone_get_by_id, tz)(id);
	if (!tz)
		return -EINVAL;

	guard(thermal_zone)(tz);

	return thermal_thresholds_add(tz, temp, direction);
}

static int thermal_genl_cmd_threshold_delete(struct param *p)
{
	int id, temp, direction;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID] ||
	    !p->attrs[THERMAL_GENL_ATTR_THRESHOLD_TEMP] ||
	    !p->attrs[THERMAL_GENL_ATTR_THRESHOLD_DIRECTION])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);
	temp = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_THRESHOLD_TEMP]);
	direction = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_THRESHOLD_DIRECTION]);

	CLASS(thermal_zone_get_by_id, tz)(id);
	if (!tz)
		return -EINVAL;

	guard(thermal_zone)(tz);

	return thermal_thresholds_delete(tz, temp, direction);
}

static int thermal_genl_cmd_threshold_flush(struct param *p)
{
	int id;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	CLASS(thermal_zone_get_by_id, tz)(id);
	if (!tz)
		return -EINVAL;

	guard(thermal_zone)(tz);

	thermal_thresholds_flush(tz);

	return 0;
}

static cb_t cmd_cb[] = {
	[THERMAL_GENL_CMD_TZ_GET_ID]		= thermal_genl_cmd_tz_get_id,
	[THERMAL_GENL_CMD_TZ_GET_TRIP]		= thermal_genl_cmd_tz_get_trip,
	[THERMAL_GENL_CMD_TZ_GET_TEMP]		= thermal_genl_cmd_tz_get_temp,
	[THERMAL_GENL_CMD_TZ_GET_GOV]		= thermal_genl_cmd_tz_get_gov,
	[THERMAL_GENL_CMD_CDEV_GET]		= thermal_genl_cmd_cdev_get,
	[THERMAL_GENL_CMD_THRESHOLD_GET]	= thermal_genl_cmd_threshold_get,
	[THERMAL_GENL_CMD_THRESHOLD_ADD]	= thermal_genl_cmd_threshold_add,
	[THERMAL_GENL_CMD_THRESHOLD_DELETE]	= thermal_genl_cmd_threshold_delete,
	[THERMAL_GENL_CMD_THRESHOLD_FLUSH]	= thermal_genl_cmd_threshold_flush,
};

static int thermal_genl_cmd_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct param p = { .msg = skb };
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	int cmd = info->op.cmd;
	int ret;
	void *hdr;

	hdr = genlmsg_put(skb, 0, 0, &thermal_genl_family, 0, cmd);
	if (!hdr)
		return -EMSGSIZE;

	ret = cmd_cb[cmd](&p);
	if (ret)
		goto out_cancel_msg;

	genlmsg_end(skb, hdr);

	return 0;

out_cancel_msg:
	genlmsg_cancel(skb, hdr);

	return ret;
}

static int thermal_genl_cmd_doit(struct sk_buff *skb,
				 struct genl_info *info)
{
	struct param p = { .attrs = info->attrs };
	struct sk_buff *msg;
	void *hdr;
	int cmd = info->genlhdr->cmd;
	int ret = -EMSGSIZE;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	p.msg = msg;

	hdr = genlmsg_put_reply(msg, info, &thermal_genl_family, 0, cmd);
	if (!hdr)
		goto out_free_msg;

	ret = cmd_cb[cmd](&p);
	if (ret)
		goto out_cancel_msg;

	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);

out_cancel_msg:
	genlmsg_cancel(msg, hdr);
out_free_msg:
	nlmsg_free(msg);

	return ret;
}

static int thermal_genl_bind(int mcgrp)
{
	struct thermal_genl_notify n = { .mcgrp = mcgrp };

	if (WARN_ON_ONCE(mcgrp > THERMAL_GENL_MAX_GROUP))
		return -EINVAL;

	blocking_notifier_call_chain(&thermal_genl_chain, THERMAL_NOTIFY_BIND, &n);
	return 0;
}

static void thermal_genl_unbind(int mcgrp)
{
	struct thermal_genl_notify n = { .mcgrp = mcgrp };

	if (WARN_ON_ONCE(mcgrp > THERMAL_GENL_MAX_GROUP))
		return;

	blocking_notifier_call_chain(&thermal_genl_chain, THERMAL_NOTIFY_UNBIND, &n);
}

static const struct genl_small_ops thermal_genl_ops[] = {
	{
		.cmd = THERMAL_GENL_CMD_TZ_GET_ID,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit = thermal_genl_cmd_dumpit,
	},
	{
		.cmd = THERMAL_GENL_CMD_TZ_GET_TRIP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = thermal_genl_cmd_doit,
	},
	{
		.cmd = THERMAL_GENL_CMD_TZ_GET_TEMP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = thermal_genl_cmd_doit,
	},
	{
		.cmd = THERMAL_GENL_CMD_TZ_GET_GOV,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = thermal_genl_cmd_doit,
	},
	{
		.cmd = THERMAL_GENL_CMD_CDEV_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit = thermal_genl_cmd_dumpit,
	},
	{
		.cmd = THERMAL_GENL_CMD_THRESHOLD_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = thermal_genl_cmd_doit,
	},
	{
		.cmd = THERMAL_GENL_CMD_THRESHOLD_ADD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = thermal_genl_cmd_doit,
	},
	{
		.cmd = THERMAL_GENL_CMD_THRESHOLD_DELETE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = thermal_genl_cmd_doit,
	},
	{
		.cmd = THERMAL_GENL_CMD_THRESHOLD_FLUSH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = thermal_genl_cmd_doit,
	},
};

static struct genl_family thermal_genl_family __ro_after_init = {
	.hdrsize	= 0,
	.name		= THERMAL_GENL_FAMILY_NAME,
	.version	= THERMAL_GENL_VERSION,
	.maxattr	= THERMAL_GENL_ATTR_MAX,
	.policy		= thermal_genl_policy,
	.bind		= thermal_genl_bind,
	.unbind		= thermal_genl_unbind,
	.small_ops	= thermal_genl_ops,
	.n_small_ops	= ARRAY_SIZE(thermal_genl_ops),
	.resv_start_op	= __THERMAL_GENL_CMD_MAX,
	.mcgrps		= thermal_genl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(thermal_genl_mcgrps),
};

int thermal_genl_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&thermal_genl_chain, nb);
}

int thermal_genl_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&thermal_genl_chain, nb);
}

int __init thermal_netlink_init(void)
{
	return genl_register_family(&thermal_genl_family);
}

void __init thermal_netlink_exit(void)
{
	genl_unregister_family(&thermal_genl_family);
}
