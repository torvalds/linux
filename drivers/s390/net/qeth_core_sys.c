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

	switch (card->state) {
	case CARD_STATE_DOWN:
		return sysfs_emit(buf, "DOWN\n");
	case CARD_STATE_SOFTSETUP:
		if (card->dev->flags & IFF_UP)
			return sysfs_emit(buf, "UP (LAN %s)\n",
					  netif_carrier_ok(card->dev) ?
					  "ONLINE" : "OFFLINE");
		return sysfs_emit(buf, "SOFTSETUP\n");
	default:
		return sysfs_emit(buf, "UNKNOWN\n");
	}
}

static DEVICE_ATTR(state, 0444, qeth_dev_state_show, NULL);

static ssize_t qeth_dev_chpid_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%02X\n", card->info.chpid);
}

static DEVICE_ATTR(chpid, 0444, qeth_dev_chpid_show, NULL);

static ssize_t qeth_dev_if_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", netdev_name(card->dev));
}

static DEVICE_ATTR(if_name, 0444, qeth_dev_if_name_show, NULL);

static ssize_t qeth_dev_card_type_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", qeth_get_cardname_short(card));
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

	return sysfs_emit(buf, "%s\n", qeth_get_bufsize_str(card));
}

static DEVICE_ATTR(inbuf_size, 0444, qeth_dev_inbuf_size_show, NULL);

static ssize_t qeth_dev_portno_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%i\n", card->dev->dev_port);
}

static ssize_t qeth_dev_portno_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	unsigned int portno, limit;
	int rc = 0;

	rc = kstrtouint(buf, 16, &portno);
	if (rc)
		return rc;
	if (portno > QETH_MAX_PORTNO)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
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
	return sysfs_emit(buf, "no portname required\n");
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

	switch (card->qdio.do_prio_queueing) {
	case QETH_PRIO_Q_ING_PREC:
		return sysfs_emit(buf, "%s\n", "by precedence");
	case QETH_PRIO_Q_ING_TOS:
		return sysfs_emit(buf, "%s\n", "by type of service");
	case QETH_PRIO_Q_ING_SKB:
		return sysfs_emit(buf, "%s\n", "by skb-priority");
	case QETH_PRIO_Q_ING_VLAN:
		return sysfs_emit(buf, "%s\n", "by VLAN headers");
	case QETH_PRIO_Q_ING_FIXED:
		return sysfs_emit(buf, "always queue %i\n",
			       card->qdio.default_out_queue);
	default:
		return sysfs_emit(buf, "disabled\n");
	}
}

static ssize_t qeth_dev_prioqing_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;

	if (IS_IQD(card) || IS_VM_NIC(card))
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
			rc = -EOPNOTSUPP;
			goto out;
		}
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_VLAN;
		card->qdio.default_out_queue = QETH_DEFAULT_QUEUE;
	} else if (sysfs_streq(buf, "no_prio_queueing:0")) {
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_FIXED;
		card->qdio.default_out_queue = 0;
	} else if (sysfs_streq(buf, "no_prio_queueing:1")) {
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_FIXED;
		card->qdio.default_out_queue = 1;
	} else if (sysfs_streq(buf, "no_prio_queueing:2")) {
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_FIXED;
		card->qdio.default_out_queue = 2;
	} else if (sysfs_streq(buf, "no_prio_queueing:3")) {
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_FIXED;
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

	return sysfs_emit(buf, "%i\n", card->qdio.in_buf_pool.buf_count);
}

static ssize_t qeth_dev_bufcnt_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	unsigned int cnt;
	int rc = 0;

	rc = kstrtouint(buf, 10, &cnt);
	if (rc)
		return rc;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}

	cnt = clamp(cnt, QETH_IN_BUF_COUNT_MIN, QETH_IN_BUF_COUNT_MAX);
	rc = qeth_resize_buffer_pool(card, cnt);

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
	bool reset;
	int rc;

	rc = kstrtobool(buf, &reset);
	if (rc)
		return rc;

	if (!qeth_card_hw_is_reachable(card))
		return -EPERM;

	if (reset)
		rc = qeth_schedule_recovery(card);

	return rc ? rc : count;
}

static DEVICE_ATTR(recover, 0200, NULL, qeth_dev_recover_store);

static ssize_t qeth_dev_performance_stats_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "1\n");
}

static ssize_t qeth_dev_performance_stats_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	struct qeth_qdio_out_q *queue;
	unsigned int i;
	bool reset;
	int rc;

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

	return sysfs_emit(buf, "%i\n", card->options.layer);
}

static ssize_t qeth_dev_layer2_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	struct net_device *ndev;
	enum qeth_discipline_id newdis;
	unsigned int input;
	int rc;

	rc = kstrtouint(buf, 16, &input);
	if (rc)
		return rc;

	switch (input) {
	case 0:
		newdis = QETH_DISCIPLINE_LAYER3;
		break;
	case 1:
		newdis = QETH_DISCIPLINE_LAYER2;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&card->discipline_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
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

		qeth_remove_discipline(card);
		free_netdev(card->dev);
		card->dev = ndev;
	}

	rc = qeth_setup_discipline(card, newdis);

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

	switch (card->options.isolation) {
	case ISOLATION_MODE_NONE:
		return sysfs_emit(buf, "%s\n", ATTR_QETH_ISOLATION_NONE);
	case ISOLATION_MODE_FWD:
		return sysfs_emit(buf, "%s\n", ATTR_QETH_ISOLATION_FWD);
	case ISOLATION_MODE_DROP:
		return sysfs_emit(buf, "%s\n", ATTR_QETH_ISOLATION_DROP);
	default:
		return sysfs_emit(buf, "%s\n", "N/A");
	}
}

static ssize_t qeth_dev_isolation_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	enum qeth_ipa_isolation_modes isolation;
	int rc = 0;

	mutex_lock(&card->conf_mutex);
	if (!IS_OSD(card) && !IS_OSX(card)) {
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

	if (qeth_card_hw_is_reachable(card))
		rc = qeth_setadpparms_set_access_ctrl(card, isolation);

	if (!rc)
		WRITE_ONCE(card->options.isolation, isolation);

out:
	mutex_unlock(&card->conf_mutex);

	return rc ? rc : count;
}

static DEVICE_ATTR(isolation, 0644, qeth_dev_isolation_show,
			qeth_dev_isolation_store);

static ssize_t qeth_dev_switch_attrs_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	struct qeth_switch_info sw_info;
	int	rc = 0;

	if (!qeth_card_hw_is_reachable(card))
		return sysfs_emit(buf, "n/a\n");

	rc = qeth_query_switch_attributes(card, &sw_info);
	if (rc)
		return rc;

	if (!sw_info.capabilities)
		rc = sysfs_emit(buf, "unknown");

	if (sw_info.capabilities & QETH_SWITCH_FORW_802_1)
		rc = sysfs_emit(buf,
				(sw_info.settings & QETH_SWITCH_FORW_802_1 ?
				"[802.1]" : "802.1"));
	if (sw_info.capabilities & QETH_SWITCH_FORW_REFL_RELAY)
		rc += sysfs_emit_at(buf, rc,
				    (sw_info.settings &
				    QETH_SWITCH_FORW_REFL_RELAY ?
				    " [rr]" : " rr"));
	rc += sysfs_emit_at(buf, rc, "\n");

	return rc;
}

static DEVICE_ATTR(switch_attrs, 0444,
		   qeth_dev_switch_attrs_show, NULL);

static ssize_t qeth_hw_trap_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (card->info.hwtrap)
		return sysfs_emit(buf, "arm\n");
	else
		return sysfs_emit(buf, "disarm\n");
}

static ssize_t qeth_hw_trap_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;
	int state = 0;

	mutex_lock(&card->conf_mutex);
	if (qeth_card_hw_is_reachable(card))
		state = 1;

	if (sysfs_streq(buf, "arm")) {
		if (state && !card->info.hwtrap) {
			if (qeth_is_diagass_supported(card,
			    QETH_DIAGS_CMD_TRAP)) {
				rc = qeth_hw_trap(card, QETH_DIAGS_TRAP_ARM);
				if (!rc)
					card->info.hwtrap = 1;
			} else {
				rc = -EINVAL;
			}
		} else {
			card->info.hwtrap = 1;
		}
	} else if (sysfs_streq(buf, "disarm")) {
		if (state && card->info.hwtrap) {
			rc = qeth_hw_trap(card, QETH_DIAGS_TRAP_DISARM);
			if (!rc)
				card->info.hwtrap = 0;
		} else {
			card->info.hwtrap = 0;
		}
	} else if (sysfs_streq(buf, "trap") && state && card->info.hwtrap) {
		rc = qeth_hw_trap(card, QETH_DIAGS_TRAP_CAPTURE);
	} else {
		rc = -EINVAL;
	}

	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static DEVICE_ATTR(hw_trap, 0644, qeth_hw_trap_show,
		   qeth_hw_trap_store);

static ssize_t qeth_dev_blkt_store(struct qeth_card *card,
		const char *buf, size_t count, int *value, int max_value)
{
	unsigned int input;
	int rc;

	rc = kstrtouint(buf, 10, &input);
	if (rc)
		return rc;

	if (input > max_value)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN)
		rc = -EPERM;
	else
		*value = input;
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static ssize_t qeth_dev_blkt_total_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%i\n", card->info.blkt.time_total);
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

	return sysfs_emit(buf, "%i\n", card->info.blkt.inter_packet);
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

	return sysfs_emit(buf, "%i\n", card->info.blkt.inter_packet_jumbo);
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

static const struct attribute_group qeth_dev_blkt_group = {
	.name = "blkt",
	.attrs = qeth_blkt_device_attrs,
};

static struct attribute *qeth_dev_extended_attrs[] = {
	&dev_attr_inbuf_size.attr,
	&dev_attr_portno.attr,
	&dev_attr_portname.attr,
	&dev_attr_priority_queueing.attr,
	&dev_attr_performance_stats.attr,
	&dev_attr_layer2.attr,
	&dev_attr_isolation.attr,
	&dev_attr_hw_trap.attr,
	&dev_attr_switch_attrs.attr,
	NULL,
};

static const struct attribute_group qeth_dev_extended_group = {
	.attrs = qeth_dev_extended_attrs,
};

static struct attribute *qeth_dev_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_chpid.attr,
	&dev_attr_if_name.attr,
	&dev_attr_card_type.attr,
	&dev_attr_buffer_count.attr,
	&dev_attr_recover.attr,
	NULL,
};

static const struct attribute_group qeth_dev_group = {
	.attrs = qeth_dev_attrs,
};

const struct attribute_group *qeth_dev_groups[] = {
	&qeth_dev_group,
	&qeth_dev_extended_group,
	&qeth_dev_blkt_group,
	NULL,
};
