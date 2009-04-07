/*
 *  drivers/s390/net/qeth_core_sys.c
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Utz Bacher <utz.bacher@de.ibm.com>,
 *		 Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

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
		return sprintf(buf, "SOFTSETUP\n");
	case CARD_STATE_UP:
		if (card->lan_online)
		return sprintf(buf, "UP (LAN ONLINE)\n");
		else
			return sprintf(buf, "UP (LAN OFFLINE)\n");
	case CARD_STATE_RECOVER:
		return sprintf(buf, "RECOVER\n");
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

static inline const char *qeth_get_bufsize_str(struct qeth_card *card)
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

	return sprintf(buf, "%i\n", card->info.portno);
}

static ssize_t qeth_dev_portno_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;
	unsigned int portno;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	portno = simple_strtoul(buf, &tmp, 16);
	if (portno > QETH_MAX_PORTNO) {
		return -EINVAL;
	}

	card->info.portno = portno;
	return count;
}

static DEVICE_ATTR(portno, 0644, qeth_dev_portno_show, qeth_dev_portno_store);

static ssize_t qeth_dev_portname_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char portname[9] = {0, };

	if (!card)
		return -EINVAL;

	if (card->info.portname_required) {
		memcpy(portname, card->info.portname + 1, 8);
		EBCASC(portname, 8);
		return sprintf(buf, "%s\n", portname);
	} else
		return sprintf(buf, "no portname required\n");
}

static ssize_t qeth_dev_portname_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	tmp = strsep((char **) &buf, "\n");
	if ((strlen(tmp) > 8) || (strlen(tmp) == 0))
		return -EINVAL;

	card->info.portname[0] = strlen(tmp);
	/* for beauty reasons */
	for (i = 1; i < 9; i++)
		card->info.portname[i] = ' ';
	strcpy(card->info.portname + 1, tmp);
	ASCEBC(card->info.portname + 1, 8);

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
	default:
		return sprintf(buf, "always queue %i\n",
			       card->qdio.default_out_queue);
	}
}

static ssize_t qeth_dev_prioqing_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	/* check if 1920 devices are supported ,
	 * if though we have to permit priority queueing
	 */
	if (card->qdio.no_out_queues == 1) {
		card->qdio.do_prio_queueing = QETH_PRIOQ_DEFAULT;
		return -EPERM;
	}

	tmp = strsep((char **) &buf, "\n");
	if (!strcmp(tmp, "prio_queueing_prec"))
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_PREC;
	else if (!strcmp(tmp, "prio_queueing_tos"))
		card->qdio.do_prio_queueing = QETH_PRIO_Q_ING_TOS;
	else if (!strcmp(tmp, "no_prio_queueing:0")) {
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = 0;
	} else if (!strcmp(tmp, "no_prio_queueing:1")) {
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = 1;
	} else if (!strcmp(tmp, "no_prio_queueing:2")) {
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = 2;
	} else if (!strcmp(tmp, "no_prio_queueing:3")) {
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = 3;
	} else if (!strcmp(tmp, "no_prio_queueing")) {
		card->qdio.do_prio_queueing = QETH_NO_PRIO_QUEUEING;
		card->qdio.default_out_queue = QETH_DEFAULT_QUEUE;
	} else {
		return -EINVAL;
	}
	return count;
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
	int rc;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	old_cnt = card->qdio.in_buf_pool.buf_count;
	cnt = simple_strtoul(buf, &tmp, 10);
	cnt = (cnt < QETH_IN_BUF_COUNT_MIN) ? QETH_IN_BUF_COUNT_MIN :
		((cnt > QETH_IN_BUF_COUNT_MAX) ? QETH_IN_BUF_COUNT_MAX : cnt);
	if (old_cnt != cnt) {
		rc = qeth_realloc_buffer_pool(card, cnt);
	}
	return count;
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

	if (card->state != CARD_STATE_UP)
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

	return sprintf(buf, "%i\n", card->options.performance_stats ? 1:0);
}

static ssize_t qeth_dev_performance_stats_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;

	i = simple_strtoul(buf, &tmp, 16);
	if ((i == 0) || (i == 1)) {
		if (i == card->options.performance_stats)
			return count;
		card->options.performance_stats = i;
		if (i == 0)
			memset(&card->perf_stats, 0,
				sizeof(struct qeth_perf_stats));
		card->perf_stats.initial_rx_packets = card->stats.rx_packets;
		card->perf_stats.initial_tx_packets = card->stats.tx_packets;
	} else {
		return -EINVAL;
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

	return sprintf(buf, "%i\n", card->options.layer2 ? 1:0);
}

static ssize_t qeth_dev_layer2_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;
	int i, rc;
	enum qeth_discipline_id newdis;

	if (!card)
		return -EINVAL;

	if (((card->state != CARD_STATE_DOWN) &&
	     (card->state != CARD_STATE_RECOVER)))
		return -EPERM;

	i = simple_strtoul(buf, &tmp, 16);
	switch (i) {
	case 0:
		newdis = QETH_DISCIPLINE_LAYER3;
		break;
	case 1:
		newdis = QETH_DISCIPLINE_LAYER2;
		break;
	default:
		return -EINVAL;
	}

	if (card->options.layer2 == newdis) {
		return count;
	} else {
		if (card->discipline.ccwgdriver) {
			card->discipline.ccwgdriver->remove(card->gdev);
			qeth_core_free_discipline(card);
		}
	}

	rc = qeth_core_load_discipline(card, newdis);
	if (rc)
		return rc;

	rc = card->discipline.ccwgdriver->probe(card->gdev);
	if (rc)
		return rc;
	return count;
}

static DEVICE_ATTR(layer2, 0644, qeth_dev_layer2_show,
		   qeth_dev_layer2_store);

static ssize_t qeth_dev_large_send_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	switch (card->options.large_send) {
	case QETH_LARGE_SEND_NO:
		return sprintf(buf, "%s\n", "no");
	case QETH_LARGE_SEND_TSO:
		return sprintf(buf, "%s\n", "TSO");
	default:
		return sprintf(buf, "%s\n", "N/A");
	}
}

static ssize_t qeth_dev_large_send_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	enum qeth_large_send_types type;
	int rc = 0;
	char *tmp;

	if (!card)
		return -EINVAL;
	tmp = strsep((char **) &buf, "\n");
	if (!strcmp(tmp, "no")) {
		type = QETH_LARGE_SEND_NO;
	} else if (!strcmp(tmp, "TSO")) {
		type = QETH_LARGE_SEND_TSO;
	} else {
		return -EINVAL;
	}
	if (card->options.large_send == type)
		return count;
	rc = qeth_set_large_send(card, type);
	if (rc)
		return rc;
	return count;
}

static DEVICE_ATTR(large_send, 0644, qeth_dev_large_send_show,
		   qeth_dev_large_send_store);

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
	int i;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	i = simple_strtoul(buf, &tmp, 10);
	if (i <= max_value) {
		*value = i;
	} else {
		return -EINVAL;
	}
	return count;
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
				   &card->info.blkt.time_total, 1000);
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
				   &card->info.blkt.inter_packet, 100);
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
				   &card->info.blkt.inter_packet_jumbo, 100);
}

static DEVICE_ATTR(inter_jumbo, 0644, qeth_dev_blkt_inter_jumbo_show,
		   qeth_dev_blkt_inter_jumbo_store);

static struct attribute *qeth_blkt_device_attrs[] = {
	&dev_attr_total.attr,
	&dev_attr_inter.attr,
	&dev_attr_inter_jumbo.attr,
	NULL,
};

static struct attribute_group qeth_device_blkt_group = {
	.name = "blkt",
	.attrs = qeth_blkt_device_attrs,
};

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
	&dev_attr_large_send.attr,
	NULL,
};

static struct attribute_group qeth_device_attr_group = {
	.attrs = qeth_device_attrs,
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

int qeth_core_create_device_attributes(struct device *dev)
{
	int ret;
	ret = sysfs_create_group(&dev->kobj, &qeth_device_attr_group);
	if (ret)
		return ret;
	ret = sysfs_create_group(&dev->kobj, &qeth_device_blkt_group);
	if (ret)
		sysfs_remove_group(&dev->kobj, &qeth_device_attr_group);

	return 0;
}

void qeth_core_remove_device_attributes(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &qeth_device_attr_group);
	sysfs_remove_group(&dev->kobj, &qeth_device_blkt_group);
}

int qeth_core_create_osn_attributes(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &qeth_osn_device_attr_group);
}

void qeth_core_remove_osn_attributes(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &qeth_osn_device_attr_group);
	return;
}
