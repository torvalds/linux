// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007
 *    Author(s): Utz Bacher <utz.bacher@de.ibm.com>,
 *		 Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

#define KMSG_COMPONENT "qeth"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/list.h>
#include <linux/rwsem.h>
#include <asm/ebcdic.h>

#include "qeth_core.h"

static ssize_t qeth_dev_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	if (!card)
		return -EINVAL;

	switch (card->state) {
	case CARD_STATE_DOWN:
		return sprintf(buf, "DOWN\n");
	case CARD_STATE_HARDSETUP:
		return sprintf(buf, "HARDSETUP\n");
	case CARD_STATE_SOFTSETUP:
		if (card->dev->flags & IFF_UP)
			return sprintf(buf, "UP (LAN %s)\n",
				       netif_carrier_ok(card->dev) ? "ONLINE" :
								     "OFFLINE");
		return sprintf(buf, "SOFTSETUP\n");
	default:
		return sprintf(buf, "UNKNOWN\n");
	}
}

static DEVICE_ATTR(state, 0444, qeth_dev_state_show, NULL);

static ssize_t qeth_dev_chpid_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	if (!card)
		return -EINVAL;

	return sprintf(buf, "%02X\n", card->info.chpid);
}

static DEVICE_ATTR(chpid, 0444, qeth_dev_chpid_show, NULL);

static ssize_t qeth_dev_if_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	if (!card)
		return -EINVAL;
	return sprintf(buf, "%s\n", QETH_CARD_IFNAME(card));
}

static DEVICE_ATTR(if_name, 0444, qeth_dev_if_name_show, NULL);

static ssize_t qeth_dev_card_type_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	if (!card)
		return -EINVAL;

	return sprintf(buf, "%s\n", qeth_get_cardname_short(card));
}

static DEVICE_ATTR(card_type, 0444, qeth_dev_card_type_show, NULL);

static const char *qeth_get_bufsize_str(struct qeth_card *card)
{
	if (card->qdio.in_buf_size == 16384)
		return "16k";
	else if (card->qdio.in_buf_size == 24576)
		return "24k";
	else if (card->qdio.in_buf_size == 32768)
		return "32k";
	else if (card->qdio.in_buf_size == 40960)
		return "40k";
	else
		return "64k";
}

static ssize_t qeth_dev_inbuf_size_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	if (!card)
		return -EINVAL;

	return sprintf(buf, "%s\n", qeth_get_bufsize_str(card));
}

static DEVICE_ATTR(inbuf_size, 0444, qeth_dev_inbuf_size_show, NULL);

static ssize_t qeth_dev_portno_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->dev->dev_port);
}

static ssize_t qeth_dev_portno_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;
	unsigned int portno, limit;
	int rc = 0;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}

	portno = simple_strtoul(buf, &tmp, 16);
	if (portno > QETH_MAX_PORTNO) {
		rc = -EINVAL;
		goto out;
	}
	limit = (card->ssqd.pcnt ? card->ssqd.pcnt - 1 : card->ssqd.pcnt);
	if (portno > limit) {
		rc = -EINVAL;
		goto out;
	}
	card->dev->dev_port = portno;
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static DEVICE_ATTR(portno, 0644, qeth_dev_portno_show, qeth_dev_portno_store);

static ssize_t qeth_dev_portname_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "no portname required\n");
}

static ssize_t qeth_dev_portname_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	dev_warn_once(&card->gdev->dev,
		      "portname is deprecated and is ignored\n");
	return count;
}

static DEVICE_ATTR(portname, 0644, qeth_dev_portname_show,
		qeth_dev_portname_store);

static ssize_t qeth_dev_prioqing_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	switch (card->qdio.do_prio_queueing) {
	case QETH_PRIO_Q_ING_PREC:
		return sprintf(buf, "%s\n", "by precedence");
	case QETH_PRIO_Q_ING_TOS:
		return sprintf(buf, "%s\n", "by type of service");
	case QETH_PRIO_Q_ING_SKB:
		return sprintf(buf, "%s\n", "by skb-priority");
	case QETH_PRIO_Q_ING_VLAN:
		return sprintf(buf, "%s\n", "by VLAN headers");
	default:
		return sprintf(buf, "always queue %i\n",
			       card->qdio.default_out_queue);
	}
}

static ssize_t qeth_dev_prioqing_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;

	if (!card)
		return -EINVAL;

	if (IS_IQD(card))
		return -EOPNOTSUPP;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}

	/* check if 1920 devices are supported ,
	 * if though we have to permit priority queueing
	 */
	if (card->qdio.no_out_queues == 1) {
		card->qdio.do_prio_queueing = QETH_PRIOQ_DEFAULT;
		rc = -EPERM;
		goto out;
	}

	if (sysfs_streq(buf, "prio_queueing_prec")) {
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_PREC;
		card->qdio.default_out_queue = QETH_DEFAULT_QUEUE;
	} else if (sysfs_streq(buf, "prio_queueing_skb")) {
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_SKB;
		card->qdio.default_out_queue = QETH_DEFAULT_QUEUE;
	} else if (sysfs_streq(buf, "prio_queueing_tos")) {
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_TOS;
		card->qdio.default_out_queue = QETH_DEFAULT_QUEUE;
	} else if (sysfs_streq(buf, "prio_queueing_vlan")) {
		if (IS_LAYER3(card)) {
			rc = -ENOTSUPP;
			goto out;
		}
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_VLAN;
		card->qdio.default_out_queue = QETH_DEFAULT_QUEUE;
	} else if (sysfs_streq(buf, "no_prio_queueing:0")) {
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = 0;
	} else if (sysfs_streq(buf, "no_prio_queueing:1")) {
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = 1;
	} else if (sysfs_streq(buf, "no_prio_queueing:2")) {
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = 2;
	} else if (sysfs_streq(buf, "no_prio_queueing:3")) {
		if (card->info.type == QETH_CARD_TYPE_IQD) {
			rc = -EPERM;
			goto out;
		}
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = 3;
	} else if (sysfs_streq(buf, "no_prio_queueing")) {
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = QETH_DEFAULT_QUEUE;
	} else
		rc = -EINVAL;
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static DEVICE_ATTR(priority_queueing, 0644, qeth_dev_prioqing_show,
		qeth_dev_prioqing_store);

static ssize_t qeth_dev_bufcnt_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->qdio.in_buf_pool.buf_count);
}

static ssize_t qeth_dev_bufcnt_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;
	int cnt, old_cnt;
	int rc = 0;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}

	old_cnt = card->qdio.in_buf_pool.buf_count;
	cnt = simple_strtoul(buf, &tmp, 10);
	cnt = (cnt < QETH_IN_BUF_COUNT_MIN) ? QETH_IN_BUF_COUNT_MIN :
		((cnt > QETH_IN_BUF_COUNT_MAX) ? QETH_IN_BUF_COUNT_MAX : cnt);
	if (old_cnt != cnt) {
		rc = qeth_realloc_buffer_pool(card, cnt);
	}
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static DEVICE_ATTR(buffer_count, 0644, qeth_dev_bufcnt_show,
		qeth_dev_bufcnt_store);

static ssize_t qeth_dev_recover_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;

	if (!qeth_card_hw_is_reachable(card))
		return -EPERM;

	i = simple_strtoul(buf, &tmp, 16);
	if (i == 1)
		qeth_schedule_recovery(card);

	return count;
}

static DEVICE_ATTR(recover, 0200, NULL, qeth_dev_recover_store);

static ssize_t qeth_dev_performance_stats_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return sprintf(buf, "1\n");
}

static ssize_t qeth_dev_performance_stats_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	struct qeth_qdio_out_q *queue;
	unsigned int i;
	bool reset;
	int rc;

	if (!card)
		return -EINVAL;

	rc = kstrtobool(buf, &reset);
	if (rc)
		return rc;

	if (reset) {
		memset(&card->stats, 0, sizeof(card->stats));
		for (i = 0; i < card->qdio.no_out_queues; i++) {
			queue = card->qdio.out_qs[i];
			if (!queue)
				break;
			memset(&queue->stats, 0, sizeof(queue->stats));
		}
	}

	return count;
}

static DEVICE_ATTR(performance_stats, 0644, qeth_dev_performance_stats_show,
		   qeth_dev_performance_stats_store);

static ssize_t qeth_dev_layer2_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->options.layer);
}

static ssize_t qeth_dev_layer2_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	struct net_device *ndev;
	char *tmp;
	int i, rc = 0;
	enum qeth_discipline_id newdis;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->discipline_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}

	i = simple_strtoul(buf, &tmp, 16);
	switch (i) {
	case 0:
		newdis = QETH_DISCIPLINE_LAYER3;
		break;
	case 1:
		newdis = QETH_DISCIPLINE_LAYER2;
		break;
	default:
		rc = -EINVAL;
		goto out;
	}

	if (card->options.layer == newdis)
		goto out;
	if (card->info.layer_enforced) {
		/* fixed layer, can't switch */
		rc = -EOPNOTSUPP;
		goto out;
	}

	if (card->discipline) {
		/* start with a new, pristine netdevice: */
		ndev = qeth_clone_netdev(card->dev);
		if (!ndev) {
			rc = -ENOMEM;
			goto out;
		}

		card->discipline->remove(card->gdev);
		qeth_core_free_discipline(card);
		free_netdev(card->dev);
		card->dev = ndev;
	}

	rc = qeth_core_load_discipline(card, newdis);
	if (rc)
		goto out;

	rc = card->discipline->setup(card->gdev);
	if (rc)
		qeth_core_free_discipline(card);
out:
	mutex_unlock(&card->discipline_mutex);
	return rc ? rc : count;
}

static DEVICE_ATTR(layer2, 0644, qeth_dev_layer2_show,
		   qeth_dev_layer2_store);

#define ATTR_QETH_ISOLATION_NONE	("none")
#define ATTR_QETH_ISOLATION_FWD		("forward")
#define ATTR_QETH_ISOLATION_DROP	("drop")

static ssize_t qeth_dev_isolation_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	switch (card->options.isolation) {
	case ISOLATION_MODE_NONE:
		return snprintf(buf, 6, "%s\n", ATTR_QETH_ISOLATION_NONE);
	case ISOLATION_MODE_FWD:
		return snprintf(buf, 9, "%s\n", ATTR_QETH_ISOLATION_FWD);
	case ISOLATION_MODE_DROP:
		return snprintf(buf, 6, "%s\n", ATTR_QETH_ISOLATION_DROP);
	default:
		return snprintf(buf, 5, "%s\n", "N/A");
	}
}

static ssize_t qeth_dev_isolation_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	enum qeth_ipa_isolation_modes isolation;
	int rc = 0;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if (card->info.type != QETH_CARD_TYPE_OSD &&
	    card->info.type != QETH_CARD_TYPE_OSX) {
		rc = -EOPNOTSUPP;
		dev_err(&card->gdev->dev, "Adapter does not "
			"support QDIO data connection isolation\n");
		goto out;
	}

	/* parse input into isolation mode */
	if (sysfs_streq(buf, ATTR_QETH_ISOLATION_NONE)) {
		isolation = ISOLATION_MODE_NONE;
	} else if (sysfs_streq(buf, ATTR_QETH_ISOLATION_FWD)) {
		isolation = ISOLATION_MODE_FWD;
	} else if (sysfs_streq(buf, ATTR_QETH_ISOLATION_DROP)) {
		isolation = ISOLATION_MODE_DROP;
	} else {
		rc = -EINVAL;
		goto out;
	}
	rc = count;

	/* defer IP assist if device is offline (until discipline->set_online)*/
	card->options.prev_isolation = card->options.isolation;
	card->options.isolation = isolation;
	if (qeth_card_hw_is_reachable(card)) {
		int ipa_rc = qeth_set_access_ctrl_online(card, 1);
		if (ipa_rc != 0)
			rc = ipa_rc;
	}
out:
	mutex_unlock(&card->conf_mutex);
	return rc;
}

static DEVICE_ATTR(isolation, 0644, qeth_dev_isolation_show,
			qeth_dev_isolation_store);

static ssize_t qeth_dev_switch_attrs_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	struct qeth_switch_info sw_info;
	int	rc = 0;

	if (!card)
		return -EINVAL;

	if (!qeth_card_hw_is_reachable(card))
		return sprintf(buf, "n/a\n");

	rc = qeth_query_switch_attributes(card, &sw_info);
	if (rc)
		return rc;

	if (!sw_info.capabilities)
		rc = sprintf(buf, "unknown");

	if (sw_info.capabilities & QETH_SWITCH_FORW_802_1)
		rc = sprintf(buf, (sw_info.settings & QETH_SWITCH_FORW_802_1 ?
							"[802.1]" : "802.1"));
	if (sw_info.capabilities & QETH_SWITCH_FORW_REFL_RELAY)
		rc += sprintf(buf + rc,
			(sw_info.settings & QETH_SWITCH_FORW_REFL_RELAY ?
							" [rr]" : " rr"));
	rc += sprintf(buf + rc, "\n");

	return rc;
}

static DEVICE_ATTR(switch_attrs, 0444,
		   qeth_dev_switch_attrs_show, NULL);

static ssize_t qeth_hw_trap_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;
	if (card->info.hwtrap)
		return snprintf(buf, 5, "arm\n");
	else
		return snprintf(buf, 8, "disarm\n");
}

static ssize_t qeth_hw_trap_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;
	int state = 0;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if (qeth_card_hw_is_reachable(card))
		state = 1;

	if (sysfs_streq(buf, "arm") && !card->info.hwtrap) {
		if (state) {
			if (qeth_is_diagass_supported(card,
			    QETH_DIAGS_CMD_TRAP)) {
				rc = qeth_hw_trap(card, QETH_DIAGS_TRAP_ARM);
				if (!rc)
					card->info.hwtrap = 1;
			} else
				rc = -EINVAL;
		} else
			card->info.hwtrap = 1;
	} else if (sysfs_streq(buf, "disarm") && card->info.hwtrap) {
		if (state) {
			rc = qeth_hw_trap(card, QETH_DIAGS_TRAP_DISARM);
			if (!rc)
				card->info.hwtrap = 0;
		} else
			card->info.hwtrap = 0;
	} else if (sysfs_streq(buf, "trap") && state && card->info.hwtrap)
		rc = qeth_hw_trap(card, QETH_DIAGS_TRAP_CAPTURE);
	else
		rc = -EINVAL;

	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static DEVICE_ATTR(hw_trap, 0644, qeth_hw_trap_show,
		   qeth_hw_trap_store);

static ssize_t qeth_dev_blkt_show(char *buf, struct qeth_card *card, int value)
{

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", value);
}

static ssize_t qeth_dev_blkt_store(struct qeth_card *card,
		const char *buf, size_t count, int *value, int max_value)
{
	char *tmp;
	int i, rc = 0;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}
	i = simple_strtoul(buf, &tmp, 10);
	if (i <= max_value)
		*value = i;
	else
		rc = -EINVAL;
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static ssize_t qeth_dev_blkt_total_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_dev_blkt_show(buf, card, card->info.blkt.time_total);
}

static ssize_t qeth_dev_blkt_total_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_dev_blkt_store(card, buf, count,
				   &card->info.blkt.time_total, 5000);
}



static DEVICE_ATTR(total, 0644, qeth_dev_blkt_total_show,
		   qeth_dev_blkt_total_store);

static ssize_t qeth_dev_blkt_inter_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_dev_blkt_show(buf, card, card->info.blkt.inter_packet);
}

static ssize_t qeth_dev_blkt_inter_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_dev_blkt_store(card, buf, count,
				   &card->info.blkt.inter_packet, 1000);
}

static DEVICE_ATTR(inter, 0644, qeth_dev_blkt_inter_show,
		   qeth_dev_blkt_inter_store);

static ssize_t qeth_dev_blkt_inter_jumbo_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_dev_blkt_show(buf, card,
				  card->info.blkt.inter_packet_jumbo);
}

static ssize_t qeth_dev_blkt_inter_jumbo_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_dev_blkt_store(card, buf, count,
				   &card->info.blkt.inter_packet_jumbo, 1000);
}

static DEVICE_ATTR(inter_jumbo, 0644, qeth_dev_blkt_inter_jumbo_show,
		   qeth_dev_blkt_inter_jumbo_store);

static struct attribute *qeth_blkt_device_attrs[] = {
	&dev_attr_total.attr,
	&dev_attr_inter.attr,
	&dev_attr_inter_jumbo.attr,
	NULL,
};
const struct attribute_group qeth_device_blkt_group = {
	.name = "blkt",
	.attrs = qeth_blkt_device_attrs,
};
EXPORT_SYMBOL_GPL(qeth_device_blkt_group);

static struct attribute *qeth_device_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_chpid.attr,
	&dev_attr_if_name.attr,
	&dev_attr_card_type.attr,
	&dev_attr_inbuf_size.attr,
	&dev_attr_portno.attr,
	&dev_attr_portname.attr,
	&dev_attr_priority_queueing.attr,
	&dev_attr_buffer_count.attr,
	&dev_attr_recover.attr,
	&dev_attr_performance_stats.attr,
	&dev_attr_layer2.attr,
	&dev_attr_isolation.attr,
	&dev_attr_hw_trap.attr,
	&dev_attr_switch_attrs.attr,
	NULL,
};
const struct attribute_group qeth_device_attr_group = {
	.attrs = qeth_device_attrs,
};
EXPORT_SYMBOL_GPL(qeth_device_attr_group);

const struct attribute_group *qeth_generic_attr_groups[] = {
	&qeth_device_attr_group,
	&qeth_device_blkt_group,
	NULL,
};

static struct attribute *qeth_osn_device_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_chpid.attr,
	&dev_attr_if_name.attr,
	&dev_attr_card_type.attr,
	&dev_attr_buffer_count.attr,
	&dev_attr_recover.attr,
	NULL,
};
static struct attribute_group qeth_osn_device_attr_group = {
	.attrs = qeth_osn_device_attrs,
};
const struct attribute_group *qeth_osn_attr_groups[] = {
	&qeth_osn_device_attr_group,
	NULL,
};
