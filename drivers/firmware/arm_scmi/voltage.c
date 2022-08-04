// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Voltage Protocol
 *
 * Copyright (C) 2020 ARM Ltd.
 */

#include <linux/scmi_protocol.h>

#include "common.h"

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
};

#define NUM_VOLTAGE_DOMAINS(x)	((u16)(FIELD_GET(VOLTAGE_DOMS_NUM_MASK, (x))))

struct scmi_msg_resp_domain_attributes {
	__le32 attr;
	u8 name[SCMI_MAX_STR_SIZE];
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

struct voltage_info {
	unsigned int version;
	unsigned int num_domains;
	struct scmi_voltage_info *domains;
};

static int scmi_protocol_attributes_get(const struct scmi_handle *handle,
					struct voltage_info *vinfo)
{
	int ret;
	struct scmi_xfer *t;

	ret = scmi_xfer_get_init(handle, PROTOCOL_ATTRIBUTES,
				 SCMI_PROTOCOL_VOLTAGE, 0, sizeof(__le32), &t);
	if (ret)
		return ret;

	ret = scmi_do_xfer(handle, t);
	if (!ret)
		vinfo->num_domains =
			NUM_VOLTAGE_DOMAINS(get_unaligned_le32(t->rx.buf));

	scmi_xfer_put(handle, t);
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

static int scmi_voltage_descriptors_get(const struct scmi_handle *handle,
					struct voltage_info *vinfo)
{
	int ret, dom;
	struct scmi_xfer *td, *tl;
	struct device *dev = handle->dev;
	struct scmi_msg_resp_domain_attributes *resp_dom;
	struct scmi_msg_resp_describe_levels *resp_levels;

	ret = scmi_xfer_get_init(handle, VOLTAGE_DOMAIN_ATTRIBUTES,
				 SCMI_PROTOCOL_VOLTAGE, sizeof(__le32),
				 sizeof(*resp_dom), &td);
	if (ret)
		return ret;
	resp_dom = td->rx.buf;

	ret = scmi_xfer_get_init(handle, VOLTAGE_DESCRIBE_LEVELS,
				 SCMI_PROTOCOL_VOLTAGE, sizeof(__le64), 0, &tl);
	if (ret)
		goto outd;
	resp_levels = tl->rx.buf;

	for (dom = 0; dom < vinfo->num_domains; dom++) {
		u32 desc_index = 0;
		u16 num_returned = 0, num_remaining = 0;
		struct scmi_msg_cmd_describe_levels *cmd;
		struct scmi_voltage_info *v;

		/* Retrieve domain attributes at first ... */
		put_unaligned_le32(dom, td->tx.buf);
		ret = scmi_do_xfer(handle, td);
		/* Skip domain on comms error */
		if (ret)
			continue;

		v = vinfo->domains + dom;
		v->id = dom;
		v->attributes = le32_to_cpu(resp_dom->attr);
		strlcpy(v->name, resp_dom->name, SCMI_MAX_STR_SIZE);

		cmd = tl->tx.buf;
		/* ...then retrieve domain levels descriptions */
		do {
			u32 flags;
			int cnt;

			cmd->domain_id = cpu_to_le32(v->id);
			cmd->level_index = desc_index;
			ret = scmi_do_xfer(handle, tl);
			if (ret)
				break;

			flags = le32_to_cpu(resp_levels->flags);
			num_returned = NUM_RETURNED_LEVELS(flags);
			num_remaining = NUM_REMAINING_LEVELS(flags);

			/* Allocate space for num_levels if not already done */
			if (!v->num_levels) {
				ret = scmi_init_voltage_levels(dev, v,
							       num_returned,
							       num_remaining,
					      SUPPORTS_SEGMENTED_LEVELS(flags));
				if (ret)
					break;
			}

			if (desc_index + num_returned > v->num_levels) {
				dev_err(handle->dev,
					"No. of voltage levels can't exceed %d\n",
					v->num_levels);
				ret = -EINVAL;
				break;
			}

			for (cnt = 0; cnt < num_returned; cnt++) {
				s32 val;

				val =
				    (s32)le32_to_cpu(resp_levels->voltage[cnt]);
				v->levels_uv[desc_index + cnt] = val;
				if (val < 0)
					v->negative_volts_allowed = true;
			}

			desc_index += num_returned;

			scmi_reset_rx_to_maxsz(handle, tl);
			/* check both to avoid infinite loop due to buggy fw */
		} while (num_returned && num_remaining);

		if (ret) {
			v->num_levels = 0;
			devm_kfree(dev, v->levels_uv);
		}

		scmi_reset_rx_to_maxsz(handle, td);
	}

	scmi_xfer_put(handle, tl);
outd:
	scmi_xfer_put(handle, td);

	return ret;
}

static int __scmi_voltage_get_u32(const struct scmi_handle *handle,
				  u8 cmd_id, u32 domain_id, u32 *value)
{
	int ret;
	struct scmi_xfer *t;
	struct voltage_info *vinfo = handle->voltage_priv;

	if (domain_id >= vinfo->num_domains)
		return -EINVAL;

	ret = scmi_xfer_get_init(handle, cmd_id,
				 SCMI_PROTOCOL_VOLTAGE,
				 sizeof(__le32), 0, &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain_id, t->tx.buf);
	ret = scmi_do_xfer(handle, t);
	if (!ret)
		*value = get_unaligned_le32(t->rx.buf);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_voltage_config_set(const struct scmi_handle *handle,
				   u32 domain_id, u32 config)
{
	int ret;
	struct scmi_xfer *t;
	struct voltage_info *vinfo = handle->voltage_priv;
	struct scmi_msg_cmd_config_set *cmd;

	if (domain_id >= vinfo->num_domains)
		return -EINVAL;

	ret = scmi_xfer_get_init(handle, VOLTAGE_CONFIG_SET,
				 SCMI_PROTOCOL_VOLTAGE,
				 sizeof(*cmd), 0, &t);
	if (ret)
		return ret;

	cmd = t->tx.buf;
	cmd->domain_id = cpu_to_le32(domain_id);
	cmd->config = cpu_to_le32(config & GENMASK(3, 0));

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_voltage_config_get(const struct scmi_handle *handle,
				   u32 domain_id, u32 *config)
{
	return __scmi_voltage_get_u32(handle, VOLTAGE_CONFIG_GET,
				      domain_id, config);
}

static int scmi_voltage_level_set(const struct scmi_handle *handle,
				  u32 domain_id, u32 flags, s32 volt_uV)
{
	int ret;
	struct scmi_xfer *t;
	struct voltage_info *vinfo = handle->voltage_priv;
	struct scmi_msg_cmd_level_set *cmd;

	if (domain_id >= vinfo->num_domains)
		return -EINVAL;

	ret = scmi_xfer_get_init(handle, VOLTAGE_LEVEL_SET,
				 SCMI_PROTOCOL_VOLTAGE,
				 sizeof(*cmd), 0, &t);
	if (ret)
		return ret;

	cmd = t->tx.buf;
	cmd->domain_id = cpu_to_le32(domain_id);
	cmd->flags = cpu_to_le32(flags);
	cmd->voltage_level = cpu_to_le32(volt_uV);

	ret = scmi_do_xfer(handle, t);

	scmi_xfer_put(handle, t);
	return ret;
}

static int scmi_voltage_level_get(const struct scmi_handle *handle,
				  u32 domain_id, s32 *volt_uV)
{
	return __scmi_voltage_get_u32(handle, VOLTAGE_LEVEL_GET,
				      domain_id, (u32 *)volt_uV);
}

static const struct scmi_voltage_info * __must_check
scmi_voltage_info_get(const struct scmi_handle *handle, u32 domain_id)
{
	struct voltage_info *vinfo = handle->voltage_priv;

	if (domain_id >= vinfo->num_domains ||
	    !vinfo->domains[domain_id].num_levels)
		return NULL;

	return vinfo->domains + domain_id;
}

static int scmi_voltage_domains_num_get(const struct scmi_handle *handle)
{
	struct voltage_info *vinfo = handle->voltage_priv;

	return vinfo->num_domains;
}

static struct scmi_voltage_ops voltage_ops = {
	.num_domains_get = scmi_voltage_domains_num_get,
	.info_get = scmi_voltage_info_get,
	.config_set = scmi_voltage_config_set,
	.config_get = scmi_voltage_config_get,
	.level_set = scmi_voltage_level_set,
	.level_get = scmi_voltage_level_get,
};

static int scmi_voltage_protocol_init(struct scmi_handle *handle)
{
	int ret;
	u32 version;
	struct voltage_info *vinfo;

	ret = scmi_version_get(handle, SCMI_PROTOCOL_VOLTAGE, &version);
	if (ret)
		return ret;

	dev_dbg(handle->dev, "Voltage Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	vinfo = devm_kzalloc(handle->dev, sizeof(*vinfo), GFP_KERNEL);
	if (!vinfo)
		return -ENOMEM;
	vinfo->version = version;

	ret = scmi_protocol_attributes_get(handle, vinfo);
	if (ret)
		return ret;

	if (vinfo->num_domains) {
		vinfo->domains = devm_kcalloc(handle->dev, vinfo->num_domains,
					      sizeof(*vinfo->domains),
					      GFP_KERNEL);
		if (!vinfo->domains)
			return -ENOMEM;
		ret = scmi_voltage_descriptors_get(handle, vinfo);
		if (ret)
			return ret;
	} else {
		dev_warn(handle->dev, "No Voltage domains found.\n");
	}

	handle->voltage_ops = &voltage_ops;
	handle->voltage_priv = vinfo;

	return 0;
}

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(SCMI_PROTOCOL_VOLTAGE, voltage)
