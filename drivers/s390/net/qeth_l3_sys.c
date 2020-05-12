// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007
 *    Author(s): Utz Bacher <utz.bacher@de.ibm.com>,
 *		 Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

#include <linux/slab.h>
#include <asm/ebcdic.h>
#include <linux/hashtable.h>
#include <linux/inet.h>
#include "qeth_l3.h"

#define QETH_DEVICE_ATTR(_id, _name, _mode, _show, _store) \
struct device_attribute dev_attr_##_id = __ATTR(_name, _mode, _show, _store)

static int qeth_l3_string_to_ipaddr(const char *buf,
				    enum qeth_prot_versions proto, u8 *addr)
{
	const char *end;

	if ((proto == QETH_PROT_IPV4 && !in4_pton(buf, -1, addr, -1, &end)) ||
	    (proto == QETH_PROT_IPV6 && !in6_pton(buf, -1, addr, -1, &end)))
		return -EINVAL;
	return 0;
}

static ssize_t qeth_l3_dev_route_show(struct qeth_card *card,
			struct qeth_routing_info *route, char *buf)
{
	switch (route->type) {
	case PRIMARY_ROUTER:
		return sprintf(buf, "%s\n", "primary router");
	case SECONDARY_ROUTER:
		return sprintf(buf, "%s\n", "secondary router");
	case MULTICAST_ROUTER:
		if (card->info.broadcast_capable == QETH_BROADCAST_WITHOUT_ECHO)
			return sprintf(buf, "%s\n", "multicast router+");
		else
			return sprintf(buf, "%s\n", "multicast router");
	case PRIMARY_CONNECTOR:
		if (card->info.broadcast_capable == QETH_BROADCAST_WITHOUT_ECHO)
			return sprintf(buf, "%s\n", "primary connector+");
		else
			return sprintf(buf, "%s\n", "primary connector");
	case SECONDARY_CONNECTOR:
		if (card->info.broadcast_capable == QETH_BROADCAST_WITHOUT_ECHO)
			return sprintf(buf, "%s\n", "secondary connector+");
		else
			return sprintf(buf, "%s\n", "secondary connector");
	default:
		return sprintf(buf, "%s\n", "no");
	}
}

static ssize_t qeth_l3_dev_route4_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_route_show(card, &card->options.route4, buf);
}

static ssize_t qeth_l3_dev_route_store(struct qeth_card *card,
		struct qeth_routing_info *route, enum qeth_prot_versions prot,
		const char *buf, size_t count)
{
	enum qeth_routing_types old_route_type = route->type;
	int rc = 0;

	mutex_lock(&card->conf_mutex);
	if (sysfs_streq(buf, "no_router")) {
		route->type = NO_ROUTER;
	} else if (sysfs_streq(buf, "primary_connector")) {
		route->type = PRIMARY_CONNECTOR;
	} else if (sysfs_streq(buf, "secondary_connector")) {
		route->type = SECONDARY_CONNECTOR;
	} else if (sysfs_streq(buf, "primary_router")) {
		route->type = PRIMARY_ROUTER;
	} else if (sysfs_streq(buf, "secondary_router")) {
		route->type = SECONDARY_ROUTER;
	} else if (sysfs_streq(buf, "multicast_router")) {
		route->type = MULTICAST_ROUTER;
	} else {
		rc = -EINVAL;
		goto out;
	}
	if (qeth_card_hw_is_reachable(card) &&
	    (old_route_type != route->type)) {
		if (prot == QETH_PROT_IPV4)
			rc = qeth_l3_setrouting_v4(card);
		else if (prot == QETH_PROT_IPV6)
			rc = qeth_l3_setrouting_v6(card);
	}
out:
	if (rc)
		route->type = old_route_type;
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_route4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_route_store(card, &card->options.route4,
				QETH_PROT_IPV4, buf, count);
}

static DEVICE_ATTR(route4, 0644, qeth_l3_dev_route4_show,
			qeth_l3_dev_route4_store);

static ssize_t qeth_l3_dev_route6_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_route_show(card, &card->options.route6, buf);
}

static ssize_t qeth_l3_dev_route6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_route_store(card, &card->options.route6,
				QETH_PROT_IPV6, buf, count);
}

static DEVICE_ATTR(route6, 0644, qeth_l3_dev_route6_show,
			qeth_l3_dev_route6_store);

static ssize_t qeth_l3_dev_fake_broadcast_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sprintf(buf, "%i\n", card->options.fake_broadcast? 1:0);
}

static ssize_t qeth_l3_dev_fake_broadcast_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;
	int i, rc = 0;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}

	i = simple_strtoul(buf, &tmp, 16);
	if ((i == 0) || (i == 1))
		card->options.fake_broadcast = i;
	else
		rc = -EINVAL;
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static DEVICE_ATTR(fake_broadcast, 0644, qeth_l3_dev_fake_broadcast_show,
		   qeth_l3_dev_fake_broadcast_store);

static ssize_t qeth_l3_dev_sniffer_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sprintf(buf, "%i\n", card->options.sniffer ? 1 : 0);
}

static ssize_t qeth_l3_dev_sniffer_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;
	unsigned long i;

	if (!IS_IQD(card))
		return -EPERM;
	if (card->options.cq == QETH_CQ_ENABLED)
		return -EPERM;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}

	rc = kstrtoul(buf, 16, &i);
	if (rc) {
		rc = -EINVAL;
		goto out;
	}
	switch (i) {
	case 0:
		card->options.sniffer = i;
		break;
	case 1:
		qdio_get_ssqd_desc(CARD_DDEV(card), &card->ssqd);
		if (card->ssqd.qdioac2 & CHSC_AC2_SNIFFER_AVAILABLE) {
			card->options.sniffer = i;
			qeth_resize_buffer_pool(card, QETH_IN_BUF_COUNT_MAX);
		} else {
			rc = -EPERM;
		}

		break;
	default:
		rc = -EINVAL;
	}
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static DEVICE_ATTR(sniffer, 0644, qeth_l3_dev_sniffer_show,
		qeth_l3_dev_sniffer_store);

static ssize_t qeth_l3_dev_hsuid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char tmp_hsuid[9];

	if (!IS_IQD(card))
		return -EPERM;

	memcpy(tmp_hsuid, card->options.hsuid, sizeof(tmp_hsuid));
	EBCASC(tmp_hsuid, 8);
	return sprintf(buf, "%s\n", tmp_hsuid);
}

static ssize_t qeth_l3_dev_hsuid_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;
	char *tmp;

	if (!IS_IQD(card))
		return -EPERM;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}

	if (card->options.sniffer) {
		rc = -EPERM;
		goto out;
	}

	if (card->options.cq == QETH_CQ_NOTAVAILABLE) {
		rc = -EPERM;
		goto out;
	}

	tmp = strsep((char **)&buf, "\n");
	if (strlen(tmp) > 8) {
		rc = -EINVAL;
		goto out;
	}

	if (card->options.hsuid[0])
		/* delete old ip address */
		qeth_l3_modify_hsuid(card, false);

	if (strlen(tmp) == 0) {
		/* delete ip address only */
		card->options.hsuid[0] = '\0';
		memcpy(card->dev->perm_addr, card->options.hsuid, 9);
		qeth_configure_cq(card, QETH_CQ_DISABLED);
		goto out;
	}

	if (qeth_configure_cq(card, QETH_CQ_ENABLED)) {
		rc = -EPERM;
		goto out;
	}

	snprintf(card->options.hsuid, sizeof(card->options.hsuid),
		 "%-8s", tmp);
	ASCEBC(card->options.hsuid, 8);
	memcpy(card->dev->perm_addr, card->options.hsuid, 9);

	rc = qeth_l3_modify_hsuid(card, true);

out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static DEVICE_ATTR(hsuid, 0644, qeth_l3_dev_hsuid_show,
		   qeth_l3_dev_hsuid_store);


static struct attribute *qeth_l3_device_attrs[] = {
	&dev_attr_route4.attr,
	&dev_attr_route6.attr,
	&dev_attr_fake_broadcast.attr,
	&dev_attr_sniffer.attr,
	&dev_attr_hsuid.attr,
	NULL,
};

static const struct attribute_group qeth_l3_device_attr_group = {
	.attrs = qeth_l3_device_attrs,
};

static ssize_t qeth_l3_dev_ipato_enable_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sprintf(buf, "%i\n", card->ipato.enabled? 1:0);
}

static ssize_t qeth_l3_dev_ipato_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	bool enable;
	int rc = 0;

	mutex_lock(&card->conf_mutex);
	if (card->state != CARD_STATE_DOWN) {
		rc = -EPERM;
		goto out;
	}

	if (sysfs_streq(buf, "toggle")) {
		enable = !card->ipato.enabled;
	} else if (kstrtobool(buf, &enable)) {
		rc = -EINVAL;
		goto out;
	}

	if (card->ipato.enabled != enable) {
		card->ipato.enabled = enable;
		mutex_lock(&card->ip_lock);
		qeth_l3_update_ipato(card);
		mutex_unlock(&card->ip_lock);
	}
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static QETH_DEVICE_ATTR(ipato_enable, enable, 0644,
			qeth_l3_dev_ipato_enable_show,
			qeth_l3_dev_ipato_enable_store);

static ssize_t qeth_l3_dev_ipato_invert4_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sprintf(buf, "%i\n", card->ipato.invert4? 1:0);
}

static ssize_t qeth_l3_dev_ipato_invert4_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	bool invert;
	int rc = 0;

	mutex_lock(&card->conf_mutex);
	if (sysfs_streq(buf, "toggle")) {
		invert = !card->ipato.invert4;
	} else if (kstrtobool(buf, &invert)) {
		rc = -EINVAL;
		goto out;
	}

	if (card->ipato.invert4 != invert) {
		card->ipato.invert4 = invert;
		mutex_lock(&card->ip_lock);
		qeth_l3_update_ipato(card);
		mutex_unlock(&card->ip_lock);
	}
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static QETH_DEVICE_ATTR(ipato_invert4, invert4, 0644,
			qeth_l3_dev_ipato_invert4_show,
			qeth_l3_dev_ipato_invert4_store);

static ssize_t qeth_l3_dev_ipato_add_show(char *buf, struct qeth_card *card,
			enum qeth_prot_versions proto)
{
	struct qeth_ipato_entry *ipatoe;
	int str_len = 0;

	mutex_lock(&card->ip_lock);
	list_for_each_entry(ipatoe, &card->ipato.entries, entry) {
		char addr_str[40];
		int entry_len;

		if (ipatoe->proto != proto)
			continue;

		entry_len = qeth_l3_ipaddr_to_string(proto, ipatoe->addr,
						     addr_str);
		if (entry_len < 0)
			continue;

		/* Append /%mask to the entry: */
		entry_len += 1 + ((proto == QETH_PROT_IPV4) ? 2 : 3);
		/* Enough room to format %entry\n into null terminated page? */
		if (entry_len + 1 > PAGE_SIZE - str_len - 1)
			break;

		entry_len = scnprintf(buf, PAGE_SIZE - str_len,
				      "%s/%i\n", addr_str, ipatoe->mask_bits);
		str_len += entry_len;
		buf += entry_len;
	}
	mutex_unlock(&card->ip_lock);

	return str_len ? str_len : scnprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t qeth_l3_dev_ipato_add4_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_ipato_add_show(buf, card, QETH_PROT_IPV4);
}

static int qeth_l3_parse_ipatoe(const char *buf, enum qeth_prot_versions proto,
		  u8 *addr, int *mask_bits)
{
	const char *start, *end;
	char *tmp;
	char buffer[40] = {0, };

	start = buf;
	/* get address string */
	end = strchr(start, '/');
	if (!end || (end - start >= 40)) {
		return -EINVAL;
	}
	strncpy(buffer, start, end - start);
	if (qeth_l3_string_to_ipaddr(buffer, proto, addr)) {
		return -EINVAL;
	}
	start = end + 1;
	*mask_bits = simple_strtoul(start, &tmp, 10);
	if (!strlen(start) ||
	    (tmp == start) ||
	    (*mask_bits > ((proto == QETH_PROT_IPV4) ? 32 : 128))) {
		return -EINVAL;
	}
	return 0;
}

static ssize_t qeth_l3_dev_ipato_add_store(const char *buf, size_t count,
			 struct qeth_card *card, enum qeth_prot_versions proto)
{
	struct qeth_ipato_entry *ipatoe;
	u8 addr[16];
	int mask_bits;
	int rc = 0;

	rc = qeth_l3_parse_ipatoe(buf, proto, addr, &mask_bits);
	if (rc)
		return rc;

	ipatoe = kzalloc(sizeof(struct qeth_ipato_entry), GFP_KERNEL);
	if (!ipatoe)
		return -ENOMEM;

	ipatoe->proto = proto;
	memcpy(ipatoe->addr, addr, (proto == QETH_PROT_IPV4)? 4:16);
	ipatoe->mask_bits = mask_bits;

	rc = qeth_l3_add_ipato_entry(card, ipatoe);
	if (rc)
		kfree(ipatoe);

	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_ipato_add4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_ipato_add_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(ipato_add4, add4, 0644,
			qeth_l3_dev_ipato_add4_show,
			qeth_l3_dev_ipato_add4_store);

static ssize_t qeth_l3_dev_ipato_del_store(const char *buf, size_t count,
			 struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16];
	int mask_bits;
	int rc = 0;

	rc = qeth_l3_parse_ipatoe(buf, proto, addr, &mask_bits);
	if (!rc)
		rc = qeth_l3_del_ipato_entry(card, proto, addr, mask_bits);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_ipato_del4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_ipato_del_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(ipato_del4, del4, 0200, NULL,
			qeth_l3_dev_ipato_del4_store);

static ssize_t qeth_l3_dev_ipato_invert6_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return sprintf(buf, "%i\n", card->ipato.invert6? 1:0);
}

static ssize_t qeth_l3_dev_ipato_invert6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	bool invert;
	int rc = 0;

	mutex_lock(&card->conf_mutex);
	if (sysfs_streq(buf, "toggle")) {
		invert = !card->ipato.invert6;
	} else if (kstrtobool(buf, &invert)) {
		rc = -EINVAL;
		goto out;
	}

	if (card->ipato.invert6 != invert) {
		card->ipato.invert6 = invert;
		mutex_lock(&card->ip_lock);
		qeth_l3_update_ipato(card);
		mutex_unlock(&card->ip_lock);
	}
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static QETH_DEVICE_ATTR(ipato_invert6, invert6, 0644,
			qeth_l3_dev_ipato_invert6_show,
			qeth_l3_dev_ipato_invert6_store);


static ssize_t qeth_l3_dev_ipato_add6_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_ipato_add_show(buf, card, QETH_PROT_IPV6);
}

static ssize_t qeth_l3_dev_ipato_add6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_ipato_add_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(ipato_add6, add6, 0644,
			qeth_l3_dev_ipato_add6_show,
			qeth_l3_dev_ipato_add6_store);

static ssize_t qeth_l3_dev_ipato_del6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	return qeth_l3_dev_ipato_del_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(ipato_del6, del6, 0200, NULL,
			qeth_l3_dev_ipato_del6_store);

static struct attribute *qeth_ipato_device_attrs[] = {
	&dev_attr_ipato_enable.attr,
	&dev_attr_ipato_invert4.attr,
	&dev_attr_ipato_add4.attr,
	&dev_attr_ipato_del4.attr,
	&dev_attr_ipato_invert6.attr,
	&dev_attr_ipato_add6.attr,
	&dev_attr_ipato_del6.attr,
	NULL,
};

static const struct attribute_group qeth_device_ipato_group = {
	.name = "ipa_takeover",
	.attrs = qeth_ipato_device_attrs,
};

static ssize_t qeth_l3_dev_ip_add_show(struct device *dev, char *buf,
				       enum qeth_prot_versions proto,
				       enum qeth_ip_types type)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	struct qeth_ipaddr *ipaddr;
	int str_len = 0;
	int i;

	mutex_lock(&card->ip_lock);
	hash_for_each(card->ip_htable, i, ipaddr, hnode) {
		char addr_str[40];
		int entry_len;

		if (ipaddr->proto != proto || ipaddr->type != type)
			continue;

		entry_len = qeth_l3_ipaddr_to_string(proto, (u8 *)&ipaddr->u,
						     addr_str);
		if (entry_len < 0)
			continue;

		/* Enough room to format %addr\n into null terminated page? */
		if (entry_len + 1 > PAGE_SIZE - str_len - 1)
			break;

		entry_len = scnprintf(buf, PAGE_SIZE - str_len, "%s\n",
				      addr_str);
		str_len += entry_len;
		buf += entry_len;
	}
	mutex_unlock(&card->ip_lock);

	return str_len ? str_len : scnprintf(buf, PAGE_SIZE, "\n");
}

static ssize_t qeth_l3_dev_vipa_add4_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return qeth_l3_dev_ip_add_show(dev, buf, QETH_PROT_IPV4,
				       QETH_IP_TYPE_VIPA);
}

static ssize_t qeth_l3_vipa_store(struct device *dev, const char *buf, bool add,
				  size_t count, enum qeth_prot_versions proto)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	u8 addr[16] = {0, };
	int rc;

	rc = qeth_l3_string_to_ipaddr(buf, proto, addr);
	if (!rc)
		rc = qeth_l3_modify_rxip_vipa(card, add, addr,
					      QETH_IP_TYPE_VIPA, proto);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_vipa_add4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return qeth_l3_vipa_store(dev, buf, true, count, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(vipa_add4, add4, 0644,
			qeth_l3_dev_vipa_add4_show,
			qeth_l3_dev_vipa_add4_store);

static ssize_t qeth_l3_dev_vipa_del4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return qeth_l3_vipa_store(dev, buf, true, count, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(vipa_del4, del4, 0200, NULL,
			qeth_l3_dev_vipa_del4_store);

static ssize_t qeth_l3_dev_vipa_add6_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return qeth_l3_dev_ip_add_show(dev, buf, QETH_PROT_IPV6,
				       QETH_IP_TYPE_VIPA);
}

static ssize_t qeth_l3_dev_vipa_add6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return qeth_l3_vipa_store(dev, buf, true, count, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(vipa_add6, add6, 0644,
			qeth_l3_dev_vipa_add6_show,
			qeth_l3_dev_vipa_add6_store);

static ssize_t qeth_l3_dev_vipa_del6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return qeth_l3_vipa_store(dev, buf, false, count, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(vipa_del6, del6, 0200, NULL,
			qeth_l3_dev_vipa_del6_store);

static struct attribute *qeth_vipa_device_attrs[] = {
	&dev_attr_vipa_add4.attr,
	&dev_attr_vipa_del4.attr,
	&dev_attr_vipa_add6.attr,
	&dev_attr_vipa_del6.attr,
	NULL,
};

static const struct attribute_group qeth_device_vipa_group = {
	.name = "vipa",
	.attrs = qeth_vipa_device_attrs,
};

static ssize_t qeth_l3_dev_rxip_add4_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return qeth_l3_dev_ip_add_show(dev, buf, QETH_PROT_IPV4,
				       QETH_IP_TYPE_RXIP);
}

static int qeth_l3_parse_rxipe(const char *buf, enum qeth_prot_versions proto,
		 u8 *addr)
{
	__be32 ipv4_addr;
	struct in6_addr ipv6_addr;

	if (qeth_l3_string_to_ipaddr(buf, proto, addr)) {
		return -EINVAL;
	}
	if (proto == QETH_PROT_IPV4) {
		memcpy(&ipv4_addr, addr, sizeof(ipv4_addr));
		if (ipv4_is_multicast(ipv4_addr)) {
			QETH_DBF_MESSAGE(2, "multicast rxip not supported.\n");
			return -EINVAL;
		}
	} else if (proto == QETH_PROT_IPV6) {
		memcpy(&ipv6_addr, addr, sizeof(ipv6_addr));
		if (ipv6_addr_is_multicast(&ipv6_addr)) {
			QETH_DBF_MESSAGE(2, "multicast rxip not supported.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static ssize_t qeth_l3_rxip_store(struct device *dev, const char *buf, bool add,
				  size_t count, enum qeth_prot_versions proto)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	u8 addr[16] = {0, };
	int rc;

	rc = qeth_l3_parse_rxipe(buf, proto, addr);
	if (!rc)
		rc = qeth_l3_modify_rxip_vipa(card, add, addr,
					      QETH_IP_TYPE_RXIP, proto);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_rxip_add4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return qeth_l3_rxip_store(dev, buf, true, count, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(rxip_add4, add4, 0644,
			qeth_l3_dev_rxip_add4_show,
			qeth_l3_dev_rxip_add4_store);

static ssize_t qeth_l3_dev_rxip_del4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return qeth_l3_rxip_store(dev, buf, false, count, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(rxip_del4, del4, 0200, NULL,
			qeth_l3_dev_rxip_del4_store);

static ssize_t qeth_l3_dev_rxip_add6_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return qeth_l3_dev_ip_add_show(dev, buf, QETH_PROT_IPV6,
				       QETH_IP_TYPE_RXIP);
}

static ssize_t qeth_l3_dev_rxip_add6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return qeth_l3_rxip_store(dev, buf, true, count, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(rxip_add6, add6, 0644,
			qeth_l3_dev_rxip_add6_show,
			qeth_l3_dev_rxip_add6_store);

static ssize_t qeth_l3_dev_rxip_del6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return qeth_l3_rxip_store(dev, buf, false, count, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(rxip_del6, del6, 0200, NULL,
			qeth_l3_dev_rxip_del6_store);

static struct attribute *qeth_rxip_device_attrs[] = {
	&dev_attr_rxip_add4.attr,
	&dev_attr_rxip_del4.attr,
	&dev_attr_rxip_add6.attr,
	&dev_attr_rxip_del6.attr,
	NULL,
};

static const struct attribute_group qeth_device_rxip_group = {
	.name = "rxip",
	.attrs = qeth_rxip_device_attrs,
};

static const struct attribute_group *qeth_l3_only_attr_groups[] = {
	&qeth_l3_device_attr_group,
	&qeth_device_ipato_group,
	&qeth_device_vipa_group,
	&qeth_device_rxip_group,
	NULL,
};

int qeth_l3_create_device_attributes(struct device *dev)
{
	return sysfs_create_groups(&dev->kobj, qeth_l3_only_attr_groups);
}

void qeth_l3_remove_device_attributes(struct device *dev)
{
	sysfs_remove_groups(&dev->kobj, qeth_l3_only_attr_groups);
}

const struct attribute_group *qeth_l3_attr_groups[] = {
	&qeth_device_attr_group,
	&qeth_device_blkt_group,
	/* l3 specific, see qeth_l3_only_attr_groups: */
	&qeth_l3_device_attr_group,
	&qeth_device_ipato_group,
	&qeth_device_vipa_group,
	&qeth_device_rxip_group,
	NULL,
};
