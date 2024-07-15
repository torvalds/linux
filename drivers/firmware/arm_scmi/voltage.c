// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Voltage Protocol
 *
 * Copyright (C) 2020-2022 ARM Ltd.
 */

#include <linux/module.h>
#include <linux/scmi_protocol.h>

#include "protocols.h"

/* Updated only after ALL the mandatory features for that version are merged */
#define SCMI_PROTOCOL_SUPPORTED_VERSION		0x20000

#define VOLTAGE_DOMS_NUM_MASK		GENMASK(15, 0)
#define REMAINING_LEVELS_MASK		GENMASK(31, 16)
#define RETURNED_LEVELS_MASK		GENMASK(11, 0)

enum scmi_voltage_protocol_cmd {
	VOLTAGE_DOMAIN_ATTRIBUTES = 0x3,
	VOLTAGE_DESCRIBE_LEVELS = 0x4,
	VOLTAGE_CONFIG_SET = 0x5,
	VOLTAGE_CONFIG_GET = 0x6,
	VOLTAGE_LEVEL_SET = 0x7,
	VOLTAGE_LEVEL_GET = 0x8,
	VOLTAGE_DOMAIN_NAME_GET = 0x09,
};

#define NUM_VOLTAGE_DOMAINS(x)	((u16)(FIELD_GET(VOLTAGE_DOMS_NUM_MASK, (x))))

struct scmi_msg_resp_domain_attributes {
	__le32 attr;
#define SUPPORTS_ASYNC_LEVEL_SET(x)	((x) & BIT(31))
#define SUPPORTS_EXTENDED_NAMES(x)	((x) & BIT(30))
	u8 name[SCMI_SHORT_NAME_MAX_SIZE];
};

struct scmi_msg_cmd_describe_levels {
	__le32 domain_id;
	__le32 level_index;
};

struct scmi_msg_resp_describe_levels {
	__le32 flags;
#define NUM_REMAINING_LEVELS(f)	((u16)(FIELD_GET(REMAINING_LEVELS_MASK, (f))))
#define NUM_RETURNED_LEVELS(f)	((u16)(FIELD_GET(RETURNED_LEVELS_MASK, (f))))
#define SUPPORTS_SEGMENTED_LEVELS(f)	((f) & BIT(12))
	__le32 voltage[];
};

struct scmi_msg_cmd_config_set {
	__le32 domain_id;
	__le32 config;
};

struct scmi_msg_cmd_level_set {
	__le32 domain_id;
	__le32 flags;
	__le32 voltage_level;
};

struct scmi_resp_voltage_level_set_complete {
	__le32 domain_id;
	__le32 voltage_level;
};

struct voltage_info {
	unsigned int version;
	unsigned int num_domains;
	struct scmi_voltage_info *domains;
};

static int scmi_protocol_attributes_get(const struct scmi_protocol_handle *ph,
					struct voltage_info *vinfo)
{
	int ret;
	struct scmi_xfer *t;

	ret = ph->xops->xfer_get_init(ph, PROTOCOL_ATTRIBUTES, 0,
				      sizeof(__le32), &t);
	if (ret)
		return ret;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		vinfo->num_domains =
			NUM_VOLTAGE_DOMAINS(get_unaligned_le32(t->rx.buf));

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_init_voltage_levels(struct device *dev,
				    struct scmi_voltage_info *v,
				    u32 num_returned, u32 num_remaining,
				    bool segmented)
{
	u32 num_levels;

	num_levels = num_returned + num_remaining;
	/*
	 * segmented levels entries are represented by a single triplet
	 * returned all in one go.
	 */
	if (!num_levels ||
	    (segmented && (num_remaining || num_returned != 3))) {
		dev_err(dev,
			"Invalid level descriptor(%d/%d/%d) for voltage dom %d\n",
			num_levels, num_returned, num_remaining, v->id);
		return -EINVAL;
	}

	v->levels_uv = devm_kcalloc(dev, num_levels, sizeof(u32), GFP_KERNEL);
	if (!v->levels_uv)
		return -ENOMEM;

	v->num_levels = num_levels;
	v->segmented = segmented;

	return 0;
}

struct scmi_volt_ipriv {
	struct device *dev;
	struct scmi_voltage_info *v;
};

static void iter_volt_levels_prepare_message(void *message,
					     unsigned int desc_index,
					     const void *priv)
{
	struct scmi_msg_cmd_describe_levels *msg = message;
	const struct scmi_volt_ipriv *p = priv;

	msg->domain_id = cpu_to_le32(p->v->id);
	msg->level_index = cpu_to_le32(desc_index);
}

static int iter_volt_levels_update_state(struct scmi_iterator_state *st,
					 const void *response, void *priv)
{
	int ret = 0;
	u32 flags;
	const struct scmi_msg_resp_describe_levels *r = response;
	struct scmi_volt_ipriv *p = priv;

	flags = le32_to_cpu(r->flags);
	st->num_returned = NUM_RETURNED_LEVELS(flags);
	st->num_remaining = NUM_REMAINING_LEVELS(flags);

	/* Allocate space for num_levels if not already done */
	if (!p->v->num_levels) {
		ret = scmi_init_voltage_levels(p->dev, p->v, st->num_returned,
					       st->num_remaining,
					      SUPPORTS_SEGMENTED_LEVELS(flags));
		if (!ret)
			st->max_resources = p->v->num_levels;
	}

	return ret;
}

static int
iter_volt_levels_process_response(const struct scmi_protocol_handle *ph,
				  const void *response,
				  struct scmi_iterator_state *st, void *priv)
{
	s32 val;
	const struct scmi_msg_resp_describe_levels *r = response;
	struct scmi_volt_ipriv *p = priv;

	val = (s32)le32_to_cpu(r->voltage[st->loop_idx]);
	p->v->levels_uv[st->desc_index + st->loop_idx] = val;
	if (val < 0)
		p->v->negative_volts_allowed = true;

	return 0;
}

static int scmi_voltage_levels_get(const struct scmi_protocol_handle *ph,
				   struct scmi_voltage_info *v)
{
	int ret;
	void *iter;
	struct scmi_iterator_ops ops = {
		.prepare_message = iter_volt_levels_prepare_message,
		.update_state = iter_volt_levels_update_state,
		.process_response = iter_volt_levels_process_response,
	};
	struct scmi_volt_ipriv vpriv = {
		.dev = ph->dev,
		.v = v,
	};

	iter = ph->hops->iter_response_init(ph, &ops, v->num_levels,
					    VOLTAGE_DESCRIBE_LEVELS,
					    sizeof(struct scmi_msg_cmd_describe_levels),
					    &vpriv);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	ret = ph->hops->iter_response_run(iter);
	if (ret) {
		v->num_levels = 0;
		devm_kfree(ph->dev, v->levels_uv);
	}

	return ret;
}

static int scmi_voltage_descriptors_get(const struct scmi_protocol_handle *ph,
					struct voltage_info *vinfo)
{
	int ret, dom;
	struct scmi_xfer *td;
	struct scmi_msg_resp_domain_attributes *resp_dom;

	ret = ph->xops->xfer_get_init(ph, VOLTAGE_DOMAIN_ATTRIBUTES,
				      sizeof(__le32), sizeof(*resp_dom), &td);
	if (ret)
		return ret;
	resp_dom = td->rx.buf;

	for (dom = 0; dom < vinfo->num_domains; dom++) {
		u32 attributes;
		struct scmi_voltage_info *v;

		/* Retrieve domain attributes at first ... */
		put_unaligned_le32(dom, td->tx.buf);
		/* Skip domain on comms error */
		if (ph->xops->do_xfer(ph, td))
			continue;

		v = vinfo->domains + dom;
		v->id = dom;
		attributes = le32_to_cpu(resp_dom->attr);
		strscpy(v->name, resp_dom->name, SCMI_SHORT_NAME_MAX_SIZE);

		/*
		 * If supported overwrite short name with the extended one;
		 * on error just carry on and use already provided short name.
		 */
		if (PROTOCOL_REV_MAJOR(vinfo->version) >= 0x2) {
			if (SUPPORTS_EXTENDED_NAMES(attributes))
				ph->hops->extended_name_get(ph,
							VOLTAGE_DOMAIN_NAME_GET,
							v->id, NULL, v->name,
							SCMI_MAX_STR_SIZE);
			if (SUPPORTS_ASYNC_LEVEL_SET(attributes))
				v->async_level_set = true;
		}

		/* Skip invalid voltage descriptors */
		scmi_voltage_levels_get(ph, v);
	}

	ph->xops->xfer_put(ph, td);

	return ret;
}

static int __scmi_voltage_get_u32(const struct scmi_protocol_handle *ph,
				  u8 cmd_id, u32 domain_id, u32 *value)
{
	int ret;
	struct scmi_xfer *t;
	struct voltage_info *vinfo = ph->get_priv(ph);

	if (domain_id >= vinfo->num_domains)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, cmd_id, sizeof(__le32), 0, &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain_id, t->tx.buf);
	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		*value = get_unaligned_le32(t->rx.buf);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_voltage_config_set(const struct scmi_protocol_handle *ph,
				   u32 domain_id, u32 config)
{
	int ret;
	struct scmi_xfer *t;
	struct voltage_info *vinfo = ph->get_priv(ph);
	struct scmi_msg_cmd_config_set *cmd;

	if (domain_id >= vinfo->num_domains)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, VOLTAGE_CONFIG_SET,
				     sizeof(*cmd), 0, &t);
	if (ret)
		return ret;

	cmd = t->tx.buf;
	cmd->domain_id = cpu_to_le32(domain_id);
	cmd->config = cpu_to_le32(config & GENMASK(3, 0));

	ret = ph->xops->do_xfer(ph, t);

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_voltage_config_get(const struct scmi_protocol_handle *ph,
				   u32 domain_id, u32 *config)
{
	return __scmi_voltage_get_u32(ph, VOLTAGE_CONFIG_GET,
				      domain_id, config);
}

static int scmi_voltage_level_set(const struct scmi_protocol_handle *ph,
				  u32 domain_id,
				  enum scmi_voltage_level_mode mode,
				  s32 volt_uV)
{
	int ret;
	struct scmi_xfer *t;
	struct voltage_info *vinfo = ph->get_priv(ph);
	struct scmi_msg_cmd_level_set *cmd;
	struct scmi_voltage_info *v;

	if (domain_id >= vinfo->num_domains)
		return -EINVAL;

	ret = ph->xops->xfer_get_init(ph, VOLTAGE_LEVEL_SET,
				      sizeof(*cmd), 0, &t);
	if (ret)
		return ret;

	v = vinfo->domains + domain_id;

	cmd = t->tx.buf;
	cmd->domain_id = cpu_to_le32(domain_id);
	cmd->voltage_level = cpu_to_le32(volt_uV);

	if (!v->async_level_set || mode != SCMI_VOLTAGE_LEVEL_SET_AUTO) {
		cmd->flags = cpu_to_le32(0x0);
		ret = ph->xops->do_xfer(ph, t);
	} else {
		cmd->flags = cpu_to_le32(0x1);
		ret = ph->xops->do_xfer_with_response(ph, t);
		if (!ret) {
			struct scmi_resp_voltage_level_set_complete *resp;

			resp = t->rx.buf;
			if (le32_to_cpu(resp->domain_id) == domain_id)
				dev_dbg(ph->dev,
					"Voltage domain %d set async to %d\n",
					v->id,
					le32_to_cpu(resp->voltage_level));
			else
				ret = -EPROTO;
		}
	}

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_voltage_level_get(const struct scmi_protocol_handle *ph,
				  u32 domain_id, s32 *volt_uV)
{
	return __scmi_voltage_get_u32(ph, VOLTAGE_LEVEL_GET,
				      domain_id, (u32 *)volt_uV);
}

static const struct scmi_voltage_info * __must_check
scmi_voltage_info_get(const struct scmi_protocol_handle *ph, u32 domain_id)
{
	struct voltage_info *vinfo = ph->get_priv(ph);

	if (domain_id >= vinfo->num_domains ||
	    !vinfo->domains[domain_id].num_levels)
		return NULL;

	return vinfo->domains + domain_id;
}

static int scmi_voltage_domains_num_get(const struct scmi_protocol_handle *ph)
{
	struct voltage_info *vinfo = ph->get_priv(ph);

	return vinfo->num_domains;
}

static struct scmi_voltage_proto_ops voltage_proto_ops = {
	.num_domains_get = scmi_voltage_domains_num_get,
	.info_get = scmi_voltage_info_get,
	.config_set = scmi_voltage_config_set,
	.config_get = scmi_voltage_config_get,
	.level_set = scmi_voltage_level_set,
	.level_get = scmi_voltage_level_get,
};

static int scmi_voltage_protocol_init(const struct scmi_protocol_handle *ph)
{
	int ret;
	u32 version;
	struct voltage_info *vinfo;

	ret = ph->xops->version_get(ph, &version);
	if (ret)
		return ret;

	dev_dbg(ph->dev, "Voltage Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	vinfo = devm_kzalloc(ph->dev, sizeof(*vinfo), GFP_KERNEL);
	if (!vinfo)
		return -ENOMEM;
	vinfo->version = version;

	ret = scmi_protocol_attributes_get(ph, vinfo);
	if (ret)
		return ret;

	if (vinfo->num_domains) {
		vinfo->domains = devm_kcalloc(ph->dev, vinfo->num_domains,
					      sizeof(*vinfo->domains),
					      GFP_KERNEL);
		if (!vinfo->domains)
			return -ENOMEM;
		ret = scmi_voltage_descriptors_get(ph, vinfo);
		if (ret)
			return ret;
	} else {
		dev_warn(ph->dev, "No Voltage domains found.\n");
	}

	return ph->set_priv(ph, vinfo, version);
}

static const struct scmi_protocol scmi_voltage = {
	.id = SCMI_PROTOCOL_VOLTAGE,
	.owner = THIS_MODULE,
	.instance_init = &scmi_voltage_protocol_init,
	.ops = &voltage_proto_ops,
	.supported_version = SCMI_PROTOCOL_SUPPORTED_VERSION,
};

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(voltage, scmi_voltage)
