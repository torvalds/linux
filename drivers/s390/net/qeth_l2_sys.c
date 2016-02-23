/*
 *    Copyright IBM Corp. 2013
 *    Author(s): Eugene Crosser <eugene.crosser@ru.ibm.com>
 */

#include <linux/slab.h>
#include <asm/ebcdic.h>
#include "qeth_core.h"
#include "qeth_l2.h"

#define QETH_DEVICE_ATTR(_id, _name, _mode, _show, _store) \
struct device_attribute dev_attr_##_id = __ATTR(_name, _mode, _show, _store)

static ssize_t qeth_bridge_port_role_state_show(struct device *dev,
				struct device_attribute *attr, char *buf,
				int show_state)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	enum qeth_sbp_states state = QETH_SBP_STATE_INACTIVE;
	int rc = 0;
	char *word;

	if (!card)
		return -EINVAL;

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
			rc = sprintf(buf, "%s\n", word);
	}

	return rc;
}

static ssize_t qeth_bridge_port_role_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return qeth_bridge_port_role_state_show(dev, attr, buf, 0);
}

static ssize_t qeth_bridge_port_role_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;
	enum qeth_sbp_roles role;

	if (!card)
		return -EINVAL;
	if (sysfs_streq(buf, "primary"))
		role = QETH_SBP_ROLE_PRIMARY;
	else if (sysfs_streq(buf, "secondary"))
		role = QETH_SBP_ROLE_SECONDARY;
	else if (sysfs_streq(buf, "none"))
		role = QETH_SBP_ROLE_NONE;
	else
		return -EINVAL;

	mutex_lock(&card->conf_mutex);

	if (card->options.sbp.reflect_promisc) /* Forbid direct manipulation */
		rc = -EPERM;
	else if (qeth_card_hw_is_reachable(card)) {
		rc = qeth_bridgeport_setrole(card, role);
		if (!rc)
			card->options.sbp.role = role;
	} else
		card->options.sbp.role = role;

	mutex_unlock(&card->conf_mutex);

	return rc ? rc : count;
}

static DEVICE_ATTR(bridge_role, 0644, qeth_bridge_port_role_show,
		   qeth_bridge_port_role_store);

static ssize_t qeth_bridge_port_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return qeth_bridge_port_role_state_show(dev, attr, buf, 1);
}

static DEVICE_ATTR(bridge_state, 0444, qeth_bridge_port_state_show,
		   NULL);

static ssize_t qeth_bridgeport_hostnotification_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int enabled;

	if (!card)
		return -EINVAL;

	enabled = card->options.sbp.hostnotification;

	return sprintf(buf, "%d\n", enabled);
}

static ssize_t qeth_bridgeport_hostnotification_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;
	int enable;

	if (!card)
		return -EINVAL;

	if (sysfs_streq(buf, "0"))
		enable = 0;
	else if (sysfs_streq(buf, "1"))
		enable = 1;
	else
		return -EINVAL;

	mutex_lock(&card->conf_mutex);

	if (qeth_card_hw_is_reachable(card)) {
		rc = qeth_bridgeport_an_set(card, enable);
		if (!rc)
			card->options.sbp.hostnotification = enable;
	} else
		card->options.sbp.hostnotification = enable;

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

	if (!card)
		return -EINVAL;

	if (card->options.sbp.reflect_promisc) {
		if (card->options.sbp.reflect_promisc_primary)
			state = "primary";
		else
			state = "secondary";
	} else
		state = "none";

	return sprintf(buf, "%s\n", state);
}

static ssize_t qeth_bridgeport_reflect_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int enable, primary;
	int rc = 0;

	if (!card)
		return -EINVAL;

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

	if (card->options.sbp.role != QETH_SBP_ROLE_NONE)
		rc = -EPERM;
	else {
		card->options.sbp.reflect_promisc = enable;
		card->options.sbp.reflect_promisc_primary = primary;
		rc = 0;
	}

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

int qeth_l2_create_device_attributes(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &qeth_l2_bridgeport_attr_group);
}

void qeth_l2_remove_device_attributes(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &qeth_l2_bridgeport_attr_group);
}

/**
 * qeth_l2_setup_bridgeport_attrs() - set/restore attrs when turning online.
 * @card:			      qeth_card structure pointer
 *
 * Note: this function is called with conf_mutex held by the caller
 */
void qeth_l2_setup_bridgeport_attrs(struct qeth_card *card)
{
	int rc;

	if (!card)
		return;
	if (!card->options.sbp.supported_funcs)
		return;
	if (card->options.sbp.role != QETH_SBP_ROLE_NONE) {
		/* Conditional to avoid spurious error messages */
		qeth_bridgeport_setrole(card, card->options.sbp.role);
		/* Let the callback function refresh the stored role value. */
		qeth_bridgeport_query_ports(card,
			&card->options.sbp.role, NULL);
	}
	if (card->options.sbp.hostnotification) {
		rc = qeth_bridgeport_an_set(card, 1);
		if (rc)
			card->options.sbp.hostnotification = 0;
	} else
		qeth_bridgeport_an_set(card, 0);
}
