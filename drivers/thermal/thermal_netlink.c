// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * Generic netlink for thermal management framework
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <net/genetlink.h>
#include <trace/hooks/thermal.h>
#include <uapi/linux/thermal.h>

#include "thermal_core.h"

static const struct genl_multicast_group thermal_genl_mcgrps[] = {
	{ .name = THERMAL_GENL_SAMPLING_GROUP_NAME, },
	{ .name = THERMAL_GENL_EVENT_GROUP_NAME,  },
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
	int cdev_state;
	int cdev_max_state;
};

typedef int (*cb_t)(struct param *);

static struct genl_family thermal_gnl_family;

/************************** Sampling encoding *******************************/

int thermal_genl_sampling_temp(int id, int temp)
{
	struct sk_buff *skb;
	void *hdr;

	skb = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = genlmsg_put(skb, 0, 0, &thermal_gnl_family, 0,
			  THERMAL_GENL_SAMPLING_TEMP);
	if (!hdr)
		goto out_free;

	if (nla_put_u32(skb, THERMAL_GENL_ATTR_TZ_ID, id))
		goto out_cancel;

	if (nla_put_u32(skb, THERMAL_GENL_ATTR_TZ_TEMP, temp))
		goto out_cancel;

	genlmsg_end(skb, hdr);

	genlmsg_multicast(&thermal_gnl_family, skb, 0, 0, GFP_KERNEL);

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
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_ID, p->trip_id))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_tz_trip_add(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_ID, p->trip_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_TYPE, p->trip_type) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_TEMP, p->trip_temp) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_HYST, p->trip_hyst))
		return -EMSGSIZE;

	return 0;
}

static int thermal_genl_event_tz_trip_delete(struct param *p)
{
	if (nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id) ||
	    nla_put_u32(p->msg, THERMAL_GENL_ATTR_TZ_TRIP_ID, p->trip_id))
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

int thermal_genl_event_tz_delete(struct param *p)
	__attribute__((alias("thermal_genl_event_tz")));

int thermal_genl_event_tz_enable(struct param *p)
	__attribute__((alias("thermal_genl_event_tz")));

int thermal_genl_event_tz_disable(struct param *p)
	__attribute__((alias("thermal_genl_event_tz")));

int thermal_genl_event_tz_trip_down(struct param *p)
	__attribute__((alias("thermal_genl_event_tz_trip_up")));

int thermal_genl_event_tz_trip_change(struct param *p)
	__attribute__((alias("thermal_genl_event_tz_trip_add")));

static cb_t event_cb[] = {
	[THERMAL_GENL_EVENT_TZ_CREATE]		= thermal_genl_event_tz_create,
	[THERMAL_GENL_EVENT_TZ_DELETE]		= thermal_genl_event_tz_delete,
	[THERMAL_GENL_EVENT_TZ_ENABLE]		= thermal_genl_event_tz_enable,
	[THERMAL_GENL_EVENT_TZ_DISABLE]		= thermal_genl_event_tz_disable,
	[THERMAL_GENL_EVENT_TZ_TRIP_UP]		= thermal_genl_event_tz_trip_up,
	[THERMAL_GENL_EVENT_TZ_TRIP_DOWN]	= thermal_genl_event_tz_trip_down,
	[THERMAL_GENL_EVENT_TZ_TRIP_CHANGE]	= thermal_genl_event_tz_trip_change,
	[THERMAL_GENL_EVENT_TZ_TRIP_ADD]	= thermal_genl_event_tz_trip_add,
	[THERMAL_GENL_EVENT_TZ_TRIP_DELETE]	= thermal_genl_event_tz_trip_delete,
	[THERMAL_GENL_EVENT_CDEV_ADD]		= thermal_genl_event_cdev_add,
	[THERMAL_GENL_EVENT_CDEV_DELETE]	= thermal_genl_event_cdev_delete,
	[THERMAL_GENL_EVENT_CDEV_STATE_UPDATE]	= thermal_genl_event_cdev_state_update,
	[THERMAL_GENL_EVENT_TZ_GOV_CHANGE]	= thermal_genl_event_gov_change,
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
	int enable_thermal_genl = 1;

	trace_android_vh_enable_thermal_genl_check(event, p->tz_id, &enable_thermal_genl);
	if (!enable_thermal_genl)
		return 0;

	msg = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	p->msg = msg;

	hdr = genlmsg_put(msg, 0, 0, &thermal_gnl_family, 0, event);
	if (!hdr)
		goto out_free_msg;

	ret = event_cb[event](p);
	if (ret)
		goto out_cancel_msg;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&thermal_gnl_family, msg, 0, 1, GFP_KERNEL);

	return 0;

out_cancel_msg:
	genlmsg_cancel(msg, hdr);
out_free_msg:
	nlmsg_free(msg);

	return ret;
}

int thermal_notify_tz_create(int tz_id, const char *name)
{
	struct param p = { .tz_id = tz_id, .name = name };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_CREATE, &p);
}

int thermal_notify_tz_delete(int tz_id)
{
	struct param p = { .tz_id = tz_id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_DELETE, &p);
}

int thermal_notify_tz_enable(int tz_id)
{
	struct param p = { .tz_id = tz_id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_ENABLE, &p);
}

int thermal_notify_tz_disable(int tz_id)
{
	struct param p = { .tz_id = tz_id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_DISABLE, &p);
}

int thermal_notify_tz_trip_down(int tz_id, int trip_id)
{
	struct param p = { .tz_id = tz_id, .trip_id = trip_id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_TRIP_DOWN, &p);
}

int thermal_notify_tz_trip_up(int tz_id, int trip_id)
{
	struct param p = { .tz_id = tz_id, .trip_id = trip_id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_TRIP_UP, &p);
}

int thermal_notify_tz_trip_add(int tz_id, int trip_id, int trip_type,
			       int trip_temp, int trip_hyst)
{
	struct param p = { .tz_id = tz_id, .trip_id = trip_id,
			   .trip_type = trip_type, .trip_temp = trip_temp,
			   .trip_hyst = trip_hyst };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_TRIP_ADD, &p);
}

int thermal_notify_tz_trip_delete(int tz_id, int trip_id)
{
	struct param p = { .tz_id = tz_id, .trip_id = trip_id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_TRIP_DELETE, &p);
}

int thermal_notify_tz_trip_change(int tz_id, int trip_id, int trip_type,
				  int trip_temp, int trip_hyst)
{
	struct param p = { .tz_id = tz_id, .trip_id = trip_id,
			   .trip_type = trip_type, .trip_temp = trip_temp,
			   .trip_hyst = trip_hyst };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_TRIP_CHANGE, &p);
}

int thermal_notify_cdev_state_update(int cdev_id, int cdev_state)
{
	struct param p = { .cdev_id = cdev_id, .cdev_state = cdev_state };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_CDEV_STATE_UPDATE, &p);
}

int thermal_notify_cdev_add(int cdev_id, const char *name, int cdev_max_state)
{
	struct param p = { .cdev_id = cdev_id, .name = name,
			   .cdev_max_state = cdev_max_state };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_CDEV_ADD, &p);
}

int thermal_notify_cdev_delete(int cdev_id)
{
	struct param p = { .cdev_id = cdev_id };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_CDEV_DELETE, &p);
}

int thermal_notify_tz_gov_change(int tz_id, const char *name)
{
	struct param p = { .tz_id = tz_id, .name = name };

	return thermal_genl_send_event(THERMAL_GENL_EVENT_TZ_GOV_CHANGE, &p);
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
	struct thermal_zone_device *tz;
	struct nlattr *start_trip;
	int i, id;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	tz = thermal_zone_get_by_id(id);
	if (!tz)
		return -EINVAL;

	start_trip = nla_nest_start(msg, THERMAL_GENL_ATTR_TZ_TRIP);
	if (!start_trip)
		return -EMSGSIZE;

	mutex_lock(&tz->lock);

	for (i = 0; i < tz->trips; i++) {

		enum thermal_trip_type type;
		int temp, hyst;

		tz->ops->get_trip_type(tz, i, &type);
		tz->ops->get_trip_temp(tz, i, &temp);
		tz->ops->get_trip_hyst(tz, i, &hyst);

		if (nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_TRIP_ID, i) ||
		    nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_TRIP_TYPE, type) ||
		    nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_TRIP_TEMP, temp) ||
		    nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_TRIP_HYST, hyst))
			goto out_cancel_nest;
	}

	mutex_unlock(&tz->lock);

	nla_nest_end(msg, start_trip);

	return 0;

out_cancel_nest:
	mutex_unlock(&tz->lock);

	return -EMSGSIZE;
}

static int thermal_genl_cmd_tz_get_temp(struct param *p)
{
	struct sk_buff *msg = p->msg;
	struct thermal_zone_device *tz;
	int temp, ret, id;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	tz = thermal_zone_get_by_id(id);
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
	struct thermal_zone_device *tz;
	int id, ret = 0;

	if (!p->attrs[THERMAL_GENL_ATTR_TZ_ID])
		return -EINVAL;

	id = nla_get_u32(p->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	tz = thermal_zone_get_by_id(id);
	if (!tz)
		return -EINVAL;

	mutex_lock(&tz->lock);

	if (nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_ID, id) ||
	    nla_put_string(msg, THERMAL_GENL_ATTR_TZ_GOV_NAME,
			   tz->governor->name))
		ret = -EMSGSIZE;

	mutex_unlock(&tz->lock);

	return ret;
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

static cb_t cmd_cb[] = {
	[THERMAL_GENL_CMD_TZ_GET_ID]	= thermal_genl_cmd_tz_get_id,
	[THERMAL_GENL_CMD_TZ_GET_TRIP]	= thermal_genl_cmd_tz_get_trip,
	[THERMAL_GENL_CMD_TZ_GET_TEMP]	= thermal_genl_cmd_tz_get_temp,
	[THERMAL_GENL_CMD_TZ_GET_GOV]	= thermal_genl_cmd_tz_get_gov,
	[THERMAL_GENL_CMD_CDEV_GET]	= thermal_genl_cmd_cdev_get,
};

static int thermal_genl_cmd_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct param p = { .msg = skb };
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	int cmd = info->op.cmd;
	int ret;
	void *hdr;

	hdr = genlmsg_put(skb, 0, 0, &thermal_gnl_family, 0, cmd);
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

	hdr = genlmsg_put_reply(msg, info, &thermal_gnl_family, 0, cmd);
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
};

static struct genl_family thermal_gnl_family __ro_after_init = {
	.hdrsize	= 0,
	.name		= THERMAL_GENL_FAMILY_NAME,
	.version	= THERMAL_GENL_VERSION,
	.maxattr	= THERMAL_GENL_ATTR_MAX,
	.policy		= thermal_genl_policy,
	.small_ops	= thermal_genl_ops,
	.n_small_ops	= ARRAY_SIZE(thermal_genl_ops),
	.mcgrps		= thermal_genl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(thermal_genl_mcgrps),
};

int __init thermal_netlink_init(void)
{
	return genl_register_family(&thermal_gnl_family);
}
