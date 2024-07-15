// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Pinctrl Protocol
 *
 * Copyright (C) 2024 EPAM
 * Copyright 2024 NXP
 */

#include <asm/byteorder.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/scmi_protocol.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "common.h"
#include "protocols.h"

/* Updated only after ALL the mandatory features for that version are merged */
#define SCMI_PROTOCOL_SUPPORTED_VERSION		0x10000

#define GET_GROUPS_NR(x)	le32_get_bits((x), GENMASK(31, 16))
#define GET_PINS_NR(x)		le32_get_bits((x), GENMASK(15, 0))
#define GET_FUNCTIONS_NR(x)	le32_get_bits((x), GENMASK(15, 0))

#define EXT_NAME_FLAG(x)	le32_get_bits((x), BIT(31))
#define NUM_ELEMS(x)		le32_get_bits((x), GENMASK(15, 0))

#define REMAINING(x)		le32_get_bits((x), GENMASK(31, 16))
#define RETURNED(x)		le32_get_bits((x), GENMASK(11, 0))

#define CONFIG_FLAG_MASK	GENMASK(19, 18)
#define SELECTOR_MASK		GENMASK(17, 16)
#define SKIP_CONFIGS_MASK	GENMASK(15, 8)
#define CONFIG_TYPE_MASK	GENMASK(7, 0)

enum scmi_pinctrl_protocol_cmd {
	PINCTRL_ATTRIBUTES = 0x3,
	PINCTRL_LIST_ASSOCIATIONS = 0x4,
	PINCTRL_SETTINGS_GET = 0x5,
	PINCTRL_SETTINGS_CONFIGURE = 0x6,
	PINCTRL_REQUEST = 0x7,
	PINCTRL_RELEASE = 0x8,
	PINCTRL_NAME_GET = 0x9,
	PINCTRL_SET_PERMISSIONS = 0xa,
};

struct scmi_msg_settings_conf {
	__le32 identifier;
	__le32 function_id;
	__le32 attributes;
	__le32 configs[];
};

struct scmi_msg_settings_get {
	__le32 identifier;
	__le32 attributes;
};

struct scmi_resp_settings_get {
	__le32 function_selected;
	__le32 num_configs;
	__le32 configs[];
};

struct scmi_msg_pinctrl_protocol_attributes {
	__le32 attributes_low;
	__le32 attributes_high;
};

struct scmi_msg_pinctrl_attributes {
	__le32 identifier;
	__le32 flags;
};

struct scmi_resp_pinctrl_attributes {
	__le32 attributes;
	u8 name[SCMI_SHORT_NAME_MAX_SIZE];
};

struct scmi_msg_pinctrl_list_assoc {
	__le32 identifier;
	__le32 flags;
	__le32 index;
};

struct scmi_resp_pinctrl_list_assoc {
	__le32 flags;
	__le16 array[];
};

struct scmi_msg_request {
	__le32 identifier;
	__le32 flags;
};

struct scmi_group_info {
	char name[SCMI_MAX_STR_SIZE];
	bool present;
	u32 *group_pins;
	u32 nr_pins;
};

struct scmi_function_info {
	char name[SCMI_MAX_STR_SIZE];
	bool present;
	u32 *groups;
	u32 nr_groups;
};

struct scmi_pin_info {
	char name[SCMI_MAX_STR_SIZE];
	bool present;
};

struct scmi_pinctrl_info {
	u32 version;
	int nr_groups;
	int nr_functions;
	int nr_pins;
	struct scmi_group_info *groups;
	struct scmi_function_info *functions;
	struct scmi_pin_info *pins;
};

static int scmi_pinctrl_attributes_get(const struct scmi_protocol_handle *ph,
				       struct scmi_pinctrl_info *pi)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_pinctrl_protocol_attributes *attr;

	ret = ph->xops->xfer_get_init(ph, PROTOCOL_ATTRIBUTES, 0, sizeof(*attr), &t);
	if (ret)
		return ret;

	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		pi->nr_functions = GET_FUNCTIONS_NR(attr->attributes_high);
		pi->nr_groups = GET_GROUPS_NR(attr->attributes_low);
		pi->nr_pins = GET_PINS_NR(attr->attributes_low);
		if (pi->nr_pins == 0) {
			dev_warn(ph->dev, "returned zero pins\n");
			ret = -EINVAL;
		}
	}

	ph->xops->xfer_put(ph, t);
	return ret;
}

static int scmi_pinctrl_count_get(const struct scmi_protocol_handle *ph,
				  enum scmi_pinctrl_selector_type type)
{
	struct scmi_pinctrl_info *pi = ph->get_priv(ph);

	switch (type) {
	case PIN_TYPE:
		return pi->nr_pins;
	case GROUP_TYPE:
		return pi->nr_groups;
	case FUNCTION_TYPE:
		return pi->nr_functions;
	default:
		return -EINVAL;
	}
}

static int scmi_pinctrl_validate_id(const struct scmi_protocol_handle *ph,
				    u32 selector,
				    enum scmi_pinctrl_selector_type type)
{
	int value;

	value = scmi_pinctrl_count_get(ph, type);
	if (value < 0)
		return value;

	if (selector >= value || value == 0)
		return -EINVAL;

	return 0;
}

static int scmi_pinctrl_attributes(const struct scmi_protocol_handle *ph,
				   enum scmi_pinctrl_selector_type type,
				   u32 selector, char *name,
				   u32 *n_elems)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_pinctrl_attributes *tx;
	struct scmi_resp_pinctrl_attributes *rx;
	bool ext_name_flag;

	if (!name)
		return -EINVAL;

	ret = scmi_pinctrl_validate_id(ph, selector, type);
	if (ret)
		return ret;

	ret = ph->xops->xfer_get_init(ph, PINCTRL_ATTRIBUTES, sizeof(*tx),
				      sizeof(*rx), &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	rx = t->rx.buf;
	tx->identifier = cpu_to_le32(selector);
	tx->flags = cpu_to_le32(type);

	ret = ph->xops->do_xfer(ph, t);
	if (!ret) {
		if (n_elems)
			*n_elems = NUM_ELEMS(rx->attributes);

		strscpy(name, rx->name, SCMI_SHORT_NAME_MAX_SIZE);

		ext_name_flag = !!EXT_NAME_FLAG(rx->attributes);
	}

	ph->xops->xfer_put(ph, t);

	if (ret)
		return ret;
	/*
	 * If supported overwrite short name with the extended one;
	 * on error just carry on and use already provided short name.
	 */
	if (ext_name_flag)
		ret = ph->hops->extended_name_get(ph, PINCTRL_NAME_GET,
						  selector, (u32 *)&type, name,
						  SCMI_MAX_STR_SIZE);
	return ret;
}

struct scmi_pinctrl_ipriv {
	u32 selector;
	enum scmi_pinctrl_selector_type type;
	u32 *array;
};

static void iter_pinctrl_assoc_prepare_message(void *message,
					       u32 desc_index,
					       const void *priv)
{
	struct scmi_msg_pinctrl_list_assoc *msg = message;
	const struct scmi_pinctrl_ipriv *p = priv;

	msg->identifier = cpu_to_le32(p->selector);
	msg->flags = cpu_to_le32(p->type);
	msg->index = cpu_to_le32(desc_index);
}

static int iter_pinctrl_assoc_update_state(struct scmi_iterator_state *st,
					   const void *response, void *priv)
{
	const struct scmi_resp_pinctrl_list_assoc *r = response;

	st->num_returned = RETURNED(r->flags);
	st->num_remaining = REMAINING(r->flags);

	return 0;
}

static int
iter_pinctrl_assoc_process_response(const struct scmi_protocol_handle *ph,
				    const void *response,
				    struct scmi_iterator_state *st, void *priv)
{
	const struct scmi_resp_pinctrl_list_assoc *r = response;
	struct scmi_pinctrl_ipriv *p = priv;

	p->array[st->desc_index + st->loop_idx] =
		le16_to_cpu(r->array[st->loop_idx]);

	return 0;
}

static int scmi_pinctrl_list_associations(const struct scmi_protocol_handle *ph,
					  u32 selector,
					  enum scmi_pinctrl_selector_type type,
					  u16 size, u32 *array)
{
	int ret;
	void *iter;
	struct scmi_iterator_ops ops = {
		.prepare_message = iter_pinctrl_assoc_prepare_message,
		.update_state = iter_pinctrl_assoc_update_state,
		.process_response = iter_pinctrl_assoc_process_response,
	};
	struct scmi_pinctrl_ipriv ipriv = {
		.selector = selector,
		.type = type,
		.array = array,
	};

	if (!array || !size || type == PIN_TYPE)
		return -EINVAL;

	ret = scmi_pinctrl_validate_id(ph, selector, type);
	if (ret)
		return ret;

	iter = ph->hops->iter_response_init(ph, &ops, size,
					    PINCTRL_LIST_ASSOCIATIONS,
					    sizeof(struct scmi_msg_pinctrl_list_assoc),
					    &ipriv);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	return ph->hops->iter_response_run(iter);
}

struct scmi_settings_get_ipriv {
	u32 selector;
	enum scmi_pinctrl_selector_type type;
	bool get_all;
	unsigned int *nr_configs;
	enum scmi_pinctrl_conf_type *config_types;
	u32 *config_values;
};

static void
iter_pinctrl_settings_get_prepare_message(void *message, u32 desc_index,
					  const void *priv)
{
	struct scmi_msg_settings_get *msg = message;
	const struct scmi_settings_get_ipriv *p = priv;
	u32 attributes;

	attributes = FIELD_PREP(SELECTOR_MASK, p->type);

	if (p->get_all) {
		attributes |= FIELD_PREP(CONFIG_FLAG_MASK, 1) |
			FIELD_PREP(SKIP_CONFIGS_MASK, desc_index);
	} else {
		attributes |= FIELD_PREP(CONFIG_TYPE_MASK, p->config_types[0]);
	}

	msg->attributes = cpu_to_le32(attributes);
	msg->identifier = cpu_to_le32(p->selector);
}

static int
iter_pinctrl_settings_get_update_state(struct scmi_iterator_state *st,
				       const void *response, void *priv)
{
	const struct scmi_resp_settings_get *r = response;
	struct scmi_settings_get_ipriv *p = priv;

	if (p->get_all) {
		st->num_returned = le32_get_bits(r->num_configs, GENMASK(7, 0));
		st->num_remaining = le32_get_bits(r->num_configs, GENMASK(31, 24));
	} else {
		st->num_returned = 1;
		st->num_remaining = 0;
	}

	return 0;
}

static int
iter_pinctrl_settings_get_process_response(const struct scmi_protocol_handle *ph,
					   const void *response,
					   struct scmi_iterator_state *st,
					   void *priv)
{
	const struct scmi_resp_settings_get *r = response;
	struct scmi_settings_get_ipriv *p = priv;
	u32 type = le32_get_bits(r->configs[st->loop_idx * 2], GENMASK(7, 0));
	u32 val = le32_to_cpu(r->configs[st->loop_idx * 2 + 1]);

	if (p->get_all) {
		p->config_types[st->desc_index + st->loop_idx] = type;
	} else {
		if (p->config_types[0] != type)
			return -EINVAL;
	}

	p->config_values[st->desc_index + st->loop_idx] = val;
	++*p->nr_configs;

	return 0;
}

static int
scmi_pinctrl_settings_get(const struct scmi_protocol_handle *ph, u32 selector,
			  enum scmi_pinctrl_selector_type type,
			  unsigned int *nr_configs,
			  enum scmi_pinctrl_conf_type *config_types,
			  u32 *config_values)
{
	int ret;
	void *iter;
	unsigned int max_configs = *nr_configs;
	struct scmi_iterator_ops ops = {
		.prepare_message = iter_pinctrl_settings_get_prepare_message,
		.update_state = iter_pinctrl_settings_get_update_state,
		.process_response = iter_pinctrl_settings_get_process_response,
	};
	struct scmi_settings_get_ipriv ipriv = {
		.selector = selector,
		.type = type,
		.get_all = (max_configs > 1),
		.nr_configs = nr_configs,
		.config_types = config_types,
		.config_values = config_values,
	};

	if (!config_types || !config_values || type == FUNCTION_TYPE)
		return -EINVAL;

	ret = scmi_pinctrl_validate_id(ph, selector, type);
	if (ret)
		return ret;

	/* Prepare to count returned configs */
	*nr_configs = 0;
	iter = ph->hops->iter_response_init(ph, &ops, max_configs,
					    PINCTRL_SETTINGS_GET,
					    sizeof(struct scmi_msg_settings_get),
					    &ipriv);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	return ph->hops->iter_response_run(iter);
}

static int scmi_pinctrl_settings_get_one(const struct scmi_protocol_handle *ph,
					 u32 selector,
					 enum scmi_pinctrl_selector_type type,
					 enum scmi_pinctrl_conf_type config_type,
					 u32 *config_value)
{
	unsigned int nr_configs = 1;

	return scmi_pinctrl_settings_get(ph, selector, type, &nr_configs,
					 &config_type, config_value);
}

static int scmi_pinctrl_settings_get_all(const struct scmi_protocol_handle *ph,
					 u32 selector,
					 enum scmi_pinctrl_selector_type type,
					 unsigned int *nr_configs,
					 enum scmi_pinctrl_conf_type *config_types,
					 u32 *config_values)
{
	if (!nr_configs || *nr_configs == 0)
		return -EINVAL;

	return scmi_pinctrl_settings_get(ph, selector, type, nr_configs,
					 config_types, config_values);
}

static int
scmi_pinctrl_settings_conf(const struct scmi_protocol_handle *ph,
			   u32 selector,
			   enum scmi_pinctrl_selector_type type,
			   u32 nr_configs,
			   enum scmi_pinctrl_conf_type *config_type,
			   u32 *config_value)
{
	struct scmi_xfer *t;
	struct scmi_msg_settings_conf *tx;
	u32 attributes;
	int ret, i;
	u32 configs_in_chunk, conf_num = 0;
	u32 chunk;
	int max_msg_size = ph->hops->get_max_msg_size(ph);

	if (!config_type || !config_value || type == FUNCTION_TYPE)
		return -EINVAL;

	ret = scmi_pinctrl_validate_id(ph, selector, type);
	if (ret)
		return ret;

	configs_in_chunk = (max_msg_size - sizeof(*tx)) / (sizeof(__le32) * 2);
	while (conf_num < nr_configs) {
		chunk = (nr_configs - conf_num > configs_in_chunk) ?
			configs_in_chunk : nr_configs - conf_num;

		ret = ph->xops->xfer_get_init(ph, PINCTRL_SETTINGS_CONFIGURE,
					      sizeof(*tx) +
					      chunk * 2 * sizeof(__le32), 0, &t);
		if (ret)
			break;

		tx = t->tx.buf;
		tx->identifier = cpu_to_le32(selector);
		tx->function_id = cpu_to_le32(0xFFFFFFFF);
		attributes = FIELD_PREP(GENMASK(1, 0), type) |
			FIELD_PREP(GENMASK(9, 2), chunk);
		tx->attributes = cpu_to_le32(attributes);

		for (i = 0; i < chunk; i++) {
			tx->configs[i * 2] =
				cpu_to_le32(config_type[conf_num + i]);
			tx->configs[i * 2 + 1] =
				cpu_to_le32(config_value[conf_num + i]);
		}

		ret = ph->xops->do_xfer(ph, t);

		ph->xops->xfer_put(ph, t);

		if (ret)
			break;

		conf_num += chunk;
	}

	return ret;
}

static int scmi_pinctrl_function_select(const struct scmi_protocol_handle *ph,
					u32 group,
					enum scmi_pinctrl_selector_type type,
					u32 function_id)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_settings_conf *tx;
	u32 attributes;

	ret = scmi_pinctrl_validate_id(ph, group, type);
	if (ret)
		return ret;

	ret = ph->xops->xfer_get_init(ph, PINCTRL_SETTINGS_CONFIGURE,
				      sizeof(*tx), 0, &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	tx->identifier = cpu_to_le32(group);
	tx->function_id = cpu_to_le32(function_id);
	attributes = FIELD_PREP(GENMASK(1, 0), type) | BIT(10);
	tx->attributes = cpu_to_le32(attributes);

	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_pinctrl_request_free(const struct scmi_protocol_handle *ph,
				     u32 identifier,
				     enum scmi_pinctrl_selector_type type,
				     enum scmi_pinctrl_protocol_cmd cmd)
{
	int ret;
	struct scmi_xfer *t;
	struct scmi_msg_request *tx;

	if (type == FUNCTION_TYPE)
		return -EINVAL;

	if (cmd != PINCTRL_REQUEST && cmd != PINCTRL_RELEASE)
		return -EINVAL;

	ret = scmi_pinctrl_validate_id(ph, identifier, type);
	if (ret)
		return ret;

	ret = ph->xops->xfer_get_init(ph, cmd, sizeof(*tx), 0, &t);
	if (ret)
		return ret;

	tx = t->tx.buf;
	tx->identifier = cpu_to_le32(identifier);
	tx->flags = cpu_to_le32(type);

	ret = ph->xops->do_xfer(ph, t);
	ph->xops->xfer_put(ph, t);

	return ret;
}

static int scmi_pinctrl_pin_request(const struct scmi_protocol_handle *ph,
				    u32 pin)
{
	return scmi_pinctrl_request_free(ph, pin, PIN_TYPE, PINCTRL_REQUEST);
}

static int scmi_pinctrl_pin_free(const struct scmi_protocol_handle *ph, u32 pin)
{
	return scmi_pinctrl_request_free(ph, pin, PIN_TYPE, PINCTRL_RELEASE);
}

static int scmi_pinctrl_get_group_info(const struct scmi_protocol_handle *ph,
				       u32 selector,
				       struct scmi_group_info *group)
{
	int ret;

	ret = scmi_pinctrl_attributes(ph, GROUP_TYPE, selector, group->name,
				      &group->nr_pins);
	if (ret)
		return ret;

	if (!group->nr_pins) {
		dev_err(ph->dev, "Group %d has 0 elements", selector);
		return -ENODATA;
	}

	group->group_pins = kmalloc_array(group->nr_pins,
					  sizeof(*group->group_pins),
					  GFP_KERNEL);
	if (!group->group_pins)
		return -ENOMEM;

	ret = scmi_pinctrl_list_associations(ph, selector, GROUP_TYPE,
					     group->nr_pins, group->group_pins);
	if (ret) {
		kfree(group->group_pins);
		return ret;
	}

	group->present = true;
	return 0;
}

static int scmi_pinctrl_get_group_name(const struct scmi_protocol_handle *ph,
				       u32 selector, const char **name)
{
	struct scmi_pinctrl_info *pi = ph->get_priv(ph);

	if (!name)
		return -EINVAL;

	if (selector >= pi->nr_groups || pi->nr_groups == 0)
		return -EINVAL;

	if (!pi->groups[selector].present) {
		int ret;

		ret = scmi_pinctrl_get_group_info(ph, selector,
						  &pi->groups[selector]);
		if (ret)
			return ret;
	}

	*name = pi->groups[selector].name;

	return 0;
}

static int scmi_pinctrl_group_pins_get(const struct scmi_protocol_handle *ph,
				       u32 selector, const u32 **pins,
				       u32 *nr_pins)
{
	struct scmi_pinctrl_info *pi = ph->get_priv(ph);

	if (!pins || !nr_pins)
		return -EINVAL;

	if (selector >= pi->nr_groups || pi->nr_groups == 0)
		return -EINVAL;

	if (!pi->groups[selector].present) {
		int ret;

		ret = scmi_pinctrl_get_group_info(ph, selector,
						  &pi->groups[selector]);
		if (ret)
			return ret;
	}

	*pins = pi->groups[selector].group_pins;
	*nr_pins = pi->groups[selector].nr_pins;

	return 0;
}

static int scmi_pinctrl_get_function_info(const struct scmi_protocol_handle *ph,
					  u32 selector,
					  struct scmi_function_info *func)
{
	int ret;

	ret = scmi_pinctrl_attributes(ph, FUNCTION_TYPE, selector, func->name,
				      &func->nr_groups);
	if (ret)
		return ret;

	if (!func->nr_groups) {
		dev_err(ph->dev, "Function %d has 0 elements", selector);
		return -ENODATA;
	}

	func->groups = kmalloc_array(func->nr_groups, sizeof(*func->groups),
				     GFP_KERNEL);
	if (!func->groups)
		return -ENOMEM;

	ret = scmi_pinctrl_list_associations(ph, selector, FUNCTION_TYPE,
					     func->nr_groups, func->groups);
	if (ret) {
		kfree(func->groups);
		return ret;
	}

	func->present = true;
	return 0;
}

static int scmi_pinctrl_get_function_name(const struct scmi_protocol_handle *ph,
					  u32 selector, const char **name)
{
	struct scmi_pinctrl_info *pi = ph->get_priv(ph);

	if (!name)
		return -EINVAL;

	if (selector >= pi->nr_functions || pi->nr_functions == 0)
		return -EINVAL;

	if (!pi->functions[selector].present) {
		int ret;

		ret = scmi_pinctrl_get_function_info(ph, selector,
						     &pi->functions[selector]);
		if (ret)
			return ret;
	}

	*name = pi->functions[selector].name;
	return 0;
}

static int
scmi_pinctrl_function_groups_get(const struct scmi_protocol_handle *ph,
				 u32 selector, u32 *nr_groups,
				 const u32 **groups)
{
	struct scmi_pinctrl_info *pi = ph->get_priv(ph);

	if (!groups || !nr_groups)
		return -EINVAL;

	if (selector >= pi->nr_functions || pi->nr_functions == 0)
		return -EINVAL;

	if (!pi->functions[selector].present) {
		int ret;

		ret = scmi_pinctrl_get_function_info(ph, selector,
						     &pi->functions[selector]);
		if (ret)
			return ret;
	}

	*groups = pi->functions[selector].groups;
	*nr_groups = pi->functions[selector].nr_groups;

	return 0;
}

static int scmi_pinctrl_mux_set(const struct scmi_protocol_handle *ph,
				u32 selector, u32 group)
{
	return scmi_pinctrl_function_select(ph, group, GROUP_TYPE, selector);
}

static int scmi_pinctrl_get_pin_info(const struct scmi_protocol_handle *ph,
				     u32 selector, struct scmi_pin_info *pin)
{
	int ret;

	if (!pin)
		return -EINVAL;

	ret = scmi_pinctrl_attributes(ph, PIN_TYPE, selector, pin->name, NULL);
	if (ret)
		return ret;

	pin->present = true;
	return 0;
}

static int scmi_pinctrl_get_pin_name(const struct scmi_protocol_handle *ph,
				     u32 selector, const char **name)
{
	struct scmi_pinctrl_info *pi = ph->get_priv(ph);

	if (!name)
		return -EINVAL;

	if (selector >= pi->nr_pins)
		return -EINVAL;

	if (!pi->pins[selector].present) {
		int ret;

		ret = scmi_pinctrl_get_pin_info(ph, selector, &pi->pins[selector]);
		if (ret)
			return ret;
	}

	*name = pi->pins[selector].name;

	return 0;
}

static int scmi_pinctrl_name_get(const struct scmi_protocol_handle *ph,
				 u32 selector,
				 enum scmi_pinctrl_selector_type type,
				 const char **name)
{
	switch (type) {
	case PIN_TYPE:
		return scmi_pinctrl_get_pin_name(ph, selector, name);
	case GROUP_TYPE:
		return scmi_pinctrl_get_group_name(ph, selector, name);
	case FUNCTION_TYPE:
		return scmi_pinctrl_get_function_name(ph, selector, name);
	default:
		return -EINVAL;
	}
}

static const struct scmi_pinctrl_proto_ops pinctrl_proto_ops = {
	.count_get = scmi_pinctrl_count_get,
	.name_get = scmi_pinctrl_name_get,
	.group_pins_get = scmi_pinctrl_group_pins_get,
	.function_groups_get = scmi_pinctrl_function_groups_get,
	.mux_set = scmi_pinctrl_mux_set,
	.settings_get_one = scmi_pinctrl_settings_get_one,
	.settings_get_all = scmi_pinctrl_settings_get_all,
	.settings_conf = scmi_pinctrl_settings_conf,
	.pin_request = scmi_pinctrl_pin_request,
	.pin_free = scmi_pinctrl_pin_free,
};

static int scmi_pinctrl_protocol_init(const struct scmi_protocol_handle *ph)
{
	int ret;
	u32 version;
	struct scmi_pinctrl_info *pinfo;

	ret = ph->xops->version_get(ph, &version);
	if (ret)
		return ret;

	dev_dbg(ph->dev, "Pinctrl Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(ph->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	ret = scmi_pinctrl_attributes_get(ph, pinfo);
	if (ret)
		return ret;

	pinfo->pins = devm_kcalloc(ph->dev, pinfo->nr_pins,
				   sizeof(*pinfo->pins), GFP_KERNEL);
	if (!pinfo->pins)
		return -ENOMEM;

	pinfo->groups = devm_kcalloc(ph->dev, pinfo->nr_groups,
				     sizeof(*pinfo->groups), GFP_KERNEL);
	if (!pinfo->groups)
		return -ENOMEM;

	pinfo->functions = devm_kcalloc(ph->dev, pinfo->nr_functions,
					sizeof(*pinfo->functions), GFP_KERNEL);
	if (!pinfo->functions)
		return -ENOMEM;

	pinfo->version = version;

	return ph->set_priv(ph, pinfo, version);
}

static int scmi_pinctrl_protocol_deinit(const struct scmi_protocol_handle *ph)
{
	int i;
	struct scmi_pinctrl_info *pi = ph->get_priv(ph);

	/* Free groups_pins allocated in scmi_pinctrl_get_group_info */
	for (i = 0; i < pi->nr_groups; i++) {
		if (pi->groups[i].present) {
			kfree(pi->groups[i].group_pins);
			pi->groups[i].present = false;
		}
	}

	/* Free groups allocated in scmi_pinctrl_get_function_info */
	for (i = 0; i < pi->nr_functions; i++) {
		if (pi->functions[i].present) {
			kfree(pi->functions[i].groups);
			pi->functions[i].present = false;
		}
	}

	return 0;
}

static const struct scmi_protocol scmi_pinctrl = {
	.id = SCMI_PROTOCOL_PINCTRL,
	.owner = THIS_MODULE,
	.instance_init = &scmi_pinctrl_protocol_init,
	.instance_deinit = &scmi_pinctrl_protocol_deinit,
	.ops = &pinctrl_proto_ops,
	.supported_version = SCMI_PROTOCOL_SUPPORTED_VERSION,
};
DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(pinctrl, scmi_pinctrl)
