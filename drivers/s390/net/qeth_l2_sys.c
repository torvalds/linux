// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2013
 *    Author(s): Eugene Crosser <eugene.crosser@ru.ibm.com>
 */

#include <linux/slab.h>
#include <asm/ebcdic.h>
#include "qeth_core.h"
#include "qeth_l2.h"

static ssize_t qeth_bridge_port_role_state_show(struct device *dev,
				struct device_attribute *attr, char *buf,
				int show_state)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	enum qeth_sbp_states state = QETH_SBP_STATE_INACTIVE;
	int rc = 0;
	char *word;

	if (!qeth_bridgeport_allowed(card))
		return sysfs_emit(buf, "n/a (VNIC characteristics)\n");

	mutex_lock(&card->sbp_lock);
	if (qeth_card_hw_is_reachable(card) &&
					card->options.sbp.supported_funcs)
		rc = qeth_bridgeport_query_ports(card,
			&card->options.sbp.role, &state);
	if (!rc) {
		if (show_state)
			switch (state) {
			case QETH_SBP_STATE_INACTIVE:
				word = "inactive"; break;
			case QETH_SBP_STATE_STANDBY:
				word = "standby"; break;
			case QETH_SBP_STATE_ACTIVE:
				word = "active"; break;
			default:
				rc = -EIO;
			}
		else
			switch (card->options.sbp.role) {
			case QETH_SBP_ROLE_NONE:
				word = "none"; break;
			case QETH_SBP_ROLE_PRIMARY:
				word = "primary"; break;
			case QETH_SBP_ROLE_SECONDARY:
				word = "secondary"; break;
			default:
				rc = -EIO;
			}
		if (rc)
			QETH_CARD_TEXT_(card, 2, "SBP%02x:%02x",
				card->options.sbp.role, state);
		else
			rc = sysfs_emit(buf, "%s\n", word);
	}
	mutex_unlock(&card->sbp_lock);

	return rc;
}

static ssize_t qeth_bridge_port_role_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!qeth_bridgeport_allowed(card))
		return sysfs_emit(buf, "n/a (VNIC characteristics)\n");

	return qeth_bridge_port_role_state_show(dev, attr, buf, 0);
}

static ssize_t qeth_bridge_port_role_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;
	enum qeth_sbp_roles role;

	if (sysfs_streq(buf, "primary"))
		role = QETH_SBP_ROLE_PRIMARY;
	else if (sysfs_streq(buf, "secondary"))
		role = QETH_SBP_ROLE_SECONDARY;
	else if (sysfs_streq(buf, "none"))
		role = QETH_SBP_ROLE_NONE;
	else
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	mutex_lock(&card->sbp_lock);

	if (!qeth_bridgeport_allowed(card))
		rc = -EBUSY;
	else if (card->options.sbp.reflect_promisc)
		/* Forbid direct manipulation */
		rc = -EPERM;
	else if (qeth_card_hw_is_reachable(card)) {
		rc = qeth_bridgeport_setrole(card, role);
		if (!rc)
			card->options.sbp.role = role;
	} else
		card->options.sbp.role = role;

	mutex_unlock(&card->sbp_lock);
	mutex_unlock(&card->conf_mutex);

	return rc ? rc : count;
}

static DEVICE_ATTR(bridge_role, 0644, qeth_bridge_port_role_show,
		   qeth_bridge_port_role_store);

static ssize_t qeth_bridge_port_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!qeth_bridgeport_allowed(card))
		return sysfs_emit(buf, "n/a (VNIC characteristics)\n");

	return qeth_bridge_port_role_state_show(dev, attr, buf, 1);
}

static DEVICE_ATTR(bridge_state, 0444, qeth_bridge_port_state_show,
		   NULL);

static ssize_t qeth_bridgeport_hostnotification_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int enabled;

	if (!qeth_bridgeport_allowed(card))
		return sysfs_emit(buf, "n/a (VNIC characteristics)\n");

	enabled = card->options.sbp.hostnotification;

	return sysfs_emit(buf, "%d\n", enabled);
}

static ssize_t qeth_bridgeport_hostnotification_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	bool enable;
	int rc;

	rc = kstrtobool(buf, &enable);
	if (rc)
		return rc;

	mutex_lock(&card->conf_mutex);
	mutex_lock(&card->sbp_lock);

	if (!qeth_bridgeport_allowed(card))
		rc = -EBUSY;
	else if (qeth_card_hw_is_reachable(card)) {
		rc = qeth_bridgeport_an_set(card, enable);
		/* sbp_lock ensures ordering vs notifications-stopped events */
		if (!rc)
			card->options.sbp.hostnotification = enable;
	} else
		card->options.sbp.hostnotification = enable;

	mutex_unlock(&card->sbp_lock);
	mutex_unlock(&card->conf_mutex);

	return rc ? rc : count;
}

static DEVICE_ATTR(bridge_hostnotify, 0644,
			qeth_bridgeport_hostnotification_show,
			qeth_bridgeport_hostnotification_store);

static ssize_t qeth_bridgeport_reflect_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *state;

	if (!qeth_bridgeport_allowed(card))
		return sysfs_emit(buf, "n/a (VNIC characteristics)\n");

	if (card->options.sbp.reflect_promisc) {
		if (card->options.sbp.reflect_promisc_primary)
			state = "primary";
		else
			state = "secondary";
	} else
		state = "none";

	return sysfs_emit(buf, "%s\n", state);
}

static ssize_t qeth_bridgeport_reflect_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int enable, primary;
	int rc = 0;

	if (sysfs_streq(buf, "none")) {
		enable = 0;
		primary = 0;
	} else if (sysfs_streq(buf, "primary")) {
		enable = 1;
		primary = 1;
	} else if (sysfs_streq(buf, "secondary")) {
		enable = 1;
		primary = 0;
	} else
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	mutex_lock(&card->sbp_lock);

	if (!qeth_bridgeport_allowed(card))
		rc = -EBUSY;
	else if (card->options.sbp.role != QETH_SBP_ROLE_NONE)
		rc = -EPERM;
	else {
		card->options.sbp.reflect_promisc = enable;
		card->options.sbp.reflect_promisc_primary = primary;
		rc = 0;
	}

	mutex_unlock(&card->sbp_lock);
	mutex_unlock(&card->conf_mutex);

	return rc ? rc : count;
}

static DEVICE_ATTR(bridge_reflect_promisc, 0644,
			qeth_bridgeport_reflect_show,
			qeth_bridgeport_reflect_store);

static struct attribute *qeth_l2_bridgeport_attrs[] = {
	&dev_attr_bridge_role.attr,
	&dev_attr_bridge_state.attr,
	&dev_attr_bridge_hostnotify.attr,
	&dev_attr_bridge_reflect_promisc.attr,
	NULL,
};

static struct attribute_group qeth_l2_bridgeport_attr_group = {
	.attrs = qeth_l2_bridgeport_attrs,
};

/* VNIC CHARS support */

/* convert sysfs attr name to VNIC characteristic */
static u32 qeth_l2_vnicc_sysfs_attr_to_char(const char *attr_name)
{
	if (sysfs_streq(attr_name, "flooding"))
		return QETH_VNICC_FLOODING;
	else if (sysfs_streq(attr_name, "mcast_flooding"))
		return QETH_VNICC_MCAST_FLOODING;
	else if (sysfs_streq(attr_name, "learning"))
		return QETH_VNICC_LEARNING;
	else if (sysfs_streq(attr_name, "takeover_setvmac"))
		return QETH_VNICC_TAKEOVER_SETVMAC;
	else if (sysfs_streq(attr_name, "takeover_learning"))
		return QETH_VNICC_TAKEOVER_LEARNING;
	else if (sysfs_streq(attr_name, "bridge_invisible"))
		return QETH_VNICC_BRIDGE_INVISIBLE;
	else if (sysfs_streq(attr_name, "rx_bcast"))
		return QETH_VNICC_RX_BCAST;

	return 0;
}

/* get current timeout setting */
static ssize_t qeth_vnicc_timeout_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	u32 timeout;
	int rc;

	rc = qeth_l2_vnicc_get_timeout(card, &timeout);
	if (rc == -EBUSY)
		return sysfs_emit(buf, "n/a (BridgePort)\n");
	if (rc == -EOPNOTSUPP)
		return sysfs_emit(buf, "n/a\n");
	return rc ? rc : sysfs_emit(buf, "%d\n", timeout);
}

/* change timeout setting */
static ssize_t qeth_vnicc_timeout_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	u32 timeout;
	int rc;

	rc = kstrtou32(buf, 10, &timeout);
	if (rc)
		return rc;

	mutex_lock(&card->conf_mutex);
	rc = qeth_l2_vnicc_set_timeout(card, timeout);
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

/* get current setting of characteristic */
static ssize_t qeth_vnicc_char_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	bool state;
	u32 vnicc;
	int rc;

	vnicc = qeth_l2_vnicc_sysfs_attr_to_char(attr->attr.name);
	rc = qeth_l2_vnicc_get_state(card, vnicc, &state);

	if (rc == -EBUSY)
		return sysfs_emit(buf, "n/a (BridgePort)\n");
	if (rc == -EOPNOTSUPP)
		return sysfs_emit(buf, "n/a\n");
	return rc ? rc : sysfs_emit(buf, "%d\n", state);
}

/* change setting of characteristic */
static ssize_t qeth_vnicc_char_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	bool state;
	u32 vnicc;
	int rc;

	if (kstrtobool(buf, &state))
		return -EINVAL;

	vnicc = qeth_l2_vnicc_sysfs_attr_to_char(attr->attr.name);
	mutex_lock(&card->conf_mutex);
	rc = qeth_l2_vnicc_set_state(card, vnicc, state);
	mutex_unlock(&card->conf_mutex);

	return rc ? rc : count;
}

static DEVICE_ATTR(flooding, 0644, qeth_vnicc_char_show, qeth_vnicc_char_store);
static DEVICE_ATTR(mcast_flooding, 0644, qeth_vnicc_char_show,
		   qeth_vnicc_char_store);
static DEVICE_ATTR(learning, 0644, qeth_vnicc_char_show, qeth_vnicc_char_store);
static DEVICE_ATTR(learning_timeout, 0644, qeth_vnicc_timeout_show,
		   qeth_vnicc_timeout_store);
static DEVICE_ATTR(takeover_setvmac, 0644, qeth_vnicc_char_show,
		   qeth_vnicc_char_store);
static DEVICE_ATTR(takeover_learning, 0644, qeth_vnicc_char_show,
		   qeth_vnicc_char_store);
static DEVICE_ATTR(bridge_invisible, 0644, qeth_vnicc_char_show,
		   qeth_vnicc_char_store);
static DEVICE_ATTR(rx_bcast, 0644, qeth_vnicc_char_show, qeth_vnicc_char_store);

static struct attribute *qeth_l2_vnicc_attrs[] = {
	&dev_attr_flooding.attr,
	&dev_attr_mcast_flooding.attr,
	&dev_attr_learning.attr,
	&dev_attr_learning_timeout.attr,
	&dev_attr_takeover_setvmac.attr,
	&dev_attr_takeover_learning.attr,
	&dev_attr_bridge_invisible.attr,
	&dev_attr_rx_bcast.attr,
	NULL,
};

static struct attribute_group qeth_l2_vnicc_attr_group = {
	.attrs = qeth_l2_vnicc_attrs,
	.name = "vnicc",
};

const struct attribute_group *qeth_l2_attr_groups[] = {
	&qeth_l2_bridgeport_attr_group,
	&qeth_l2_vnicc_attr_group,
	NULL,
};
