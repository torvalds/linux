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
#include "qeth_l3.h"

#define QETH_DEVICE_ATTR(_id, _name, _mode, _show, _store) \
struct device_attribute dev_attr_##_id = __ATTR(_name, _mode, _show, _store)

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

	if (!card)
		return -EINVAL;

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

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_route_store(card, &card->options.route4,
				QETH_PROT_IPV4, buf, count);
}

static DEVICE_ATTR(route4, 0644, qeth_l3_dev_route4_show,
			qeth_l3_dev_route4_store);

static ssize_t qeth_l3_dev_route6_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_route_show(card, &card->options.route6, buf);
}

static ssize_t qeth_l3_dev_route6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_route_store(card, &card->options.route6,
				QETH_PROT_IPV6, buf, count);
}

static DEVICE_ATTR(route6, 0644, qeth_l3_dev_route6_show,
			qeth_l3_dev_route6_store);

static ssize_t qeth_l3_dev_fake_broadcast_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->options.fake_broadcast? 1:0);
}

static ssize_t qeth_l3_dev_fake_broadcast_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	char *tmp;
	int i, rc = 0;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER)) {
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

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->options.sniffer ? 1 : 0);
}

static ssize_t qeth_l3_dev_sniffer_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;
	unsigned long i;

	if (!card)
		return -EINVAL;

	if (card->info.type != QETH_CARD_TYPE_IQD)
		return -EPERM;
	if (card->options.cq == QETH_CQ_ENABLED)
		return -EPERM;

	mutex_lock(&card->conf_mutex);
	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER)) {
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
		if (card->ssqd.qdioac2 & QETH_SNIFF_AVAIL) {
			card->options.sniffer = i;
			if (card->qdio.init_pool.buf_count !=
					QETH_IN_BUF_COUNT_MAX)
				qeth_realloc_buffer_pool(card,
					QETH_IN_BUF_COUNT_MAX);
		} else
			rc = -EPERM;
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

	if (!card)
		return -EINVAL;

	if (card->info.type != QETH_CARD_TYPE_IQD)
		return -EPERM;

	if (card->state == CARD_STATE_DOWN)
		return -EPERM;

	memcpy(tmp_hsuid, card->options.hsuid, sizeof(tmp_hsuid));
	EBCASC(tmp_hsuid, 8);
	return sprintf(buf, "%s\n", tmp_hsuid);
}

static ssize_t qeth_l3_dev_hsuid_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	struct qeth_ipaddr *addr;
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;

	if (card->info.type != QETH_CARD_TYPE_IQD)
		return -EPERM;
	if (card->state != CARD_STATE_DOWN &&
	    card->state != CARD_STATE_RECOVER)
		return -EPERM;
	if (card->options.sniffer)
		return -EPERM;
	if (card->options.cq == QETH_CQ_NOTAVAILABLE)
		return -EPERM;

	tmp = strsep((char **)&buf, "\n");
	if (strlen(tmp) > 8)
		return -EINVAL;

	if (card->options.hsuid[0]) {
		/* delete old ip address */
		addr = qeth_l3_get_addr_buffer(QETH_PROT_IPV6);
		if (!addr)
			return -ENOMEM;

		addr->u.a6.addr.s6_addr32[0] = 0xfe800000;
		addr->u.a6.addr.s6_addr32[1] = 0x00000000;
		for (i = 8; i < 16; i++)
			addr->u.a6.addr.s6_addr[i] =
				card->options.hsuid[i - 8];
		addr->u.a6.pfxlen = 0;
		addr->type = QETH_IP_TYPE_NORMAL;

		spin_lock_bh(&card->ip_lock);
		qeth_l3_delete_ip(card, addr);
		spin_unlock_bh(&card->ip_lock);
		kfree(addr);
	}

	if (strlen(tmp) == 0) {
		/* delete ip address only */
		card->options.hsuid[0] = '\0';
		if (card->dev)
			memcpy(card->dev->perm_addr, card->options.hsuid, 9);
		qeth_configure_cq(card, QETH_CQ_DISABLED);
		return count;
	}

	if (qeth_configure_cq(card, QETH_CQ_ENABLED))
		return -EPERM;

	snprintf(card->options.hsuid, sizeof(card->options.hsuid),
		 "%-8s", tmp);
	ASCEBC(card->options.hsuid, 8);
	if (card->dev)
		memcpy(card->dev->perm_addr, card->options.hsuid, 9);

	addr = qeth_l3_get_addr_buffer(QETH_PROT_IPV6);
	if (addr != NULL) {
		addr->u.a6.addr.s6_addr32[0] = 0xfe800000;
		addr->u.a6.addr.s6_addr32[1] = 0x00000000;
		for (i = 8; i < 16; i++)
			addr->u.a6.addr.s6_addr[i] = card->options.hsuid[i - 8];
		addr->u.a6.pfxlen = 0;
		addr->type = QETH_IP_TYPE_NORMAL;
	} else
		return -ENOMEM;

	spin_lock_bh(&card->ip_lock);
	qeth_l3_add_ip(card, addr);
	spin_unlock_bh(&card->ip_lock);
	kfree(addr);

	return count;
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

static struct attribute_group qeth_l3_device_attr_group = {
	.attrs = qeth_l3_device_attrs,
};

static ssize_t qeth_l3_dev_ipato_enable_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->ipato.enabled? 1:0);
}

static ssize_t qeth_l3_dev_ipato_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	struct qeth_ipaddr *addr;
	int i, rc = 0;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER)) {
		rc = -EPERM;
		goto out;
	}

	if (sysfs_streq(buf, "toggle")) {
		card->ipato.enabled = (card->ipato.enabled)? 0 : 1;
	} else if (sysfs_streq(buf, "1")) {
		card->ipato.enabled = 1;
		hash_for_each(card->ip_htable, i, addr, hnode) {
				if ((addr->type == QETH_IP_TYPE_NORMAL) &&
				qeth_l3_is_addr_covered_by_ipato(card, addr))
					addr->set_flags |=
					QETH_IPA_SETIP_TAKEOVER_FLAG;
			}
	} else if (sysfs_streq(buf, "0")) {
		card->ipato.enabled = 0;
		hash_for_each(card->ip_htable, i, addr, hnode) {
			if (addr->set_flags &
			QETH_IPA_SETIP_TAKEOVER_FLAG)
				addr->set_flags &=
				~QETH_IPA_SETIP_TAKEOVER_FLAG;
			}
	} else
		rc = -EINVAL;
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

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->ipato.invert4? 1:0);
}

static ssize_t qeth_l3_dev_ipato_invert4_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if (sysfs_streq(buf, "toggle"))
		card->ipato.invert4 = (card->ipato.invert4)? 0 : 1;
	else if (sysfs_streq(buf, "1"))
		card->ipato.invert4 = 1;
	else if (sysfs_streq(buf, "0"))
		card->ipato.invert4 = 0;
	else
		rc = -EINVAL;
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
	char addr_str[40];
	int entry_len; /* length of 1 entry string, differs between v4 and v6 */
	int i = 0;

	entry_len = (proto == QETH_PROT_IPV4)? 12 : 40;
	/* add strlen for "/<mask>\n" */
	entry_len += (proto == QETH_PROT_IPV4)? 5 : 6;
	spin_lock_bh(&card->ip_lock);
	list_for_each_entry(ipatoe, &card->ipato.entries, entry) {
		if (ipatoe->proto != proto)
			continue;
		/* String must not be longer than PAGE_SIZE. So we check if
		 * string length gets near PAGE_SIZE. Then we can savely display
		 * the next IPv6 address (worst case, compared to IPv4) */
		if ((PAGE_SIZE - i) <= entry_len)
			break;
		qeth_l3_ipaddr_to_string(proto, ipatoe->addr, addr_str);
		i += snprintf(buf + i, PAGE_SIZE - i,
			      "%s/%i\n", addr_str, ipatoe->mask_bits);
	}
	spin_unlock_bh(&card->ip_lock);
	i += snprintf(buf + i, PAGE_SIZE - i, "\n");

	return i;
}

static ssize_t qeth_l3_dev_ipato_add4_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

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

	mutex_lock(&card->conf_mutex);
	rc = qeth_l3_parse_ipatoe(buf, proto, addr, &mask_bits);
	if (rc)
		goto out;

	ipatoe = kzalloc(sizeof(struct qeth_ipato_entry), GFP_KERNEL);
	if (!ipatoe) {
		rc = -ENOMEM;
		goto out;
	}
	ipatoe->proto = proto;
	memcpy(ipatoe->addr, addr, (proto == QETH_PROT_IPV4)? 4:16);
	ipatoe->mask_bits = mask_bits;

	rc = qeth_l3_add_ipato_entry(card, ipatoe);
	if (rc)
		kfree(ipatoe);
out:
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_ipato_add4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

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

	mutex_lock(&card->conf_mutex);
	rc = qeth_l3_parse_ipatoe(buf, proto, addr, &mask_bits);
	if (!rc)
		qeth_l3_del_ipato_entry(card, proto, addr, mask_bits);
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_ipato_del4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_ipato_del_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(ipato_del4, del4, 0200, NULL,
			qeth_l3_dev_ipato_del4_store);

static ssize_t qeth_l3_dev_ipato_invert6_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->ipato.invert6? 1:0);
}

static ssize_t qeth_l3_dev_ipato_invert6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);
	int rc = 0;

	if (!card)
		return -EINVAL;

	mutex_lock(&card->conf_mutex);
	if (sysfs_streq(buf, "toggle"))
		card->ipato.invert6 = (card->ipato.invert6)? 0 : 1;
	else if (sysfs_streq(buf, "1"))
		card->ipato.invert6 = 1;
	else if (sysfs_streq(buf, "0"))
		card->ipato.invert6 = 0;
	else
		rc = -EINVAL;
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

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_ipato_add_show(buf, card, QETH_PROT_IPV6);
}

static ssize_t qeth_l3_dev_ipato_add6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_ipato_add_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(ipato_add6, add6, 0644,
			qeth_l3_dev_ipato_add6_show,
			qeth_l3_dev_ipato_add6_store);

static ssize_t qeth_l3_dev_ipato_del6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

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

static struct attribute_group qeth_device_ipato_group = {
	.name = "ipa_takeover",
	.attrs = qeth_ipato_device_attrs,
};

static ssize_t qeth_l3_dev_vipa_add_show(char *buf, struct qeth_card *card,
			enum qeth_prot_versions proto)
{
	struct qeth_ipaddr *ipaddr;
	struct hlist_node  *tmp;
	char addr_str[40];
	int entry_len; /* length of 1 entry string, differs between v4 and v6 */
	int i = 0;

	entry_len = (proto == QETH_PROT_IPV4)? 12 : 40;
	entry_len += 2; /* \n + terminator */
	spin_lock_bh(&card->ip_lock);
	hash_for_each_safe(card->ip_htable, i, tmp, ipaddr, hnode) {
		if (ipaddr->proto != proto)
			continue;
		if (ipaddr->type != QETH_IP_TYPE_VIPA)
			continue;
		/* String must not be longer than PAGE_SIZE. So we check if
		 * string length gets near PAGE_SIZE. Then we can savely display
		 * the next IPv6 address (worst case, compared to IPv4) */
		if ((PAGE_SIZE - i) <= entry_len)
			break;
		qeth_l3_ipaddr_to_string(proto, (const u8 *)&ipaddr->u,
			addr_str);
		i += snprintf(buf + i, PAGE_SIZE - i, "%s\n", addr_str);
	}
	spin_unlock_bh(&card->ip_lock);
	i += snprintf(buf + i, PAGE_SIZE - i, "\n");

	return i;
}

static ssize_t qeth_l3_dev_vipa_add4_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_vipa_add_show(buf, card, QETH_PROT_IPV4);
}

static int qeth_l3_parse_vipae(const char *buf, enum qeth_prot_versions proto,
		 u8 *addr)
{
	if (qeth_l3_string_to_ipaddr(buf, proto, addr)) {
		return -EINVAL;
	}
	return 0;
}

static ssize_t qeth_l3_dev_vipa_add_store(const char *buf, size_t count,
			struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16] = {0, };
	int rc;

	mutex_lock(&card->conf_mutex);
	rc = qeth_l3_parse_vipae(buf, proto, addr);
	if (!rc)
		rc = qeth_l3_add_vipa(card, proto, addr);
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_vipa_add4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_vipa_add_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(vipa_add4, add4, 0644,
			qeth_l3_dev_vipa_add4_show,
			qeth_l3_dev_vipa_add4_store);

static ssize_t qeth_l3_dev_vipa_del_store(const char *buf, size_t count,
			 struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16];
	int rc;

	mutex_lock(&card->conf_mutex);
	rc = qeth_l3_parse_vipae(buf, proto, addr);
	if (!rc)
		qeth_l3_del_vipa(card, proto, addr);
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_vipa_del4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_vipa_del_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(vipa_del4, del4, 0200, NULL,
			qeth_l3_dev_vipa_del4_store);

static ssize_t qeth_l3_dev_vipa_add6_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_vipa_add_show(buf, card, QETH_PROT_IPV6);
}

static ssize_t qeth_l3_dev_vipa_add6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_vipa_add_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(vipa_add6, add6, 0644,
			qeth_l3_dev_vipa_add6_show,
			qeth_l3_dev_vipa_add6_store);

static ssize_t qeth_l3_dev_vipa_del6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_vipa_del_store(buf, count, card, QETH_PROT_IPV6);
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

static struct attribute_group qeth_device_vipa_group = {
	.name = "vipa",
	.attrs = qeth_vipa_device_attrs,
};

static ssize_t qeth_l3_dev_rxip_add_show(char *buf, struct qeth_card *card,
		       enum qeth_prot_versions proto)
{
	struct qeth_ipaddr *ipaddr;
	struct hlist_node *tmp;
	char addr_str[40];
	int entry_len; /* length of 1 entry string, differs between v4 and v6 */
	int i = 0;

	entry_len = (proto == QETH_PROT_IPV4)? 12 : 40;
	entry_len += 2; /* \n + terminator */
	spin_lock_bh(&card->ip_lock);
	hash_for_each_safe(card->ip_htable, i, tmp, ipaddr, hnode) {
		if (ipaddr->proto != proto)
			continue;
		if (ipaddr->type != QETH_IP_TYPE_RXIP)
			continue;
		/* String must not be longer than PAGE_SIZE. So we check if
		 * string length gets near PAGE_SIZE. Then we can savely display
		 * the next IPv6 address (worst case, compared to IPv4) */
		if ((PAGE_SIZE - i) <= entry_len)
			break;
		qeth_l3_ipaddr_to_string(proto, (const u8 *)&ipaddr->u,
			addr_str);
		i += snprintf(buf + i, PAGE_SIZE - i, "%s\n", addr_str);
	}
	spin_unlock_bh(&card->ip_lock);
	i += snprintf(buf + i, PAGE_SIZE - i, "\n");

	return i;
}

static ssize_t qeth_l3_dev_rxip_add4_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_rxip_add_show(buf, card, QETH_PROT_IPV4);
}

static int qeth_l3_parse_rxipe(const char *buf, enum qeth_prot_versions proto,
		 u8 *addr)
{
	if (qeth_l3_string_to_ipaddr(buf, proto, addr)) {
		return -EINVAL;
	}
	return 0;
}

static ssize_t qeth_l3_dev_rxip_add_store(const char *buf, size_t count,
			struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16] = {0, };
	int rc;

	mutex_lock(&card->conf_mutex);
	rc = qeth_l3_parse_rxipe(buf, proto, addr);
	if (!rc)
		rc = qeth_l3_add_rxip(card, proto, addr);
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_rxip_add4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_rxip_add_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(rxip_add4, add4, 0644,
			qeth_l3_dev_rxip_add4_show,
			qeth_l3_dev_rxip_add4_store);

static ssize_t qeth_l3_dev_rxip_del_store(const char *buf, size_t count,
			struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16];
	int rc;

	mutex_lock(&card->conf_mutex);
	rc = qeth_l3_parse_rxipe(buf, proto, addr);
	if (!rc)
		qeth_l3_del_rxip(card, proto, addr);
	mutex_unlock(&card->conf_mutex);
	return rc ? rc : count;
}

static ssize_t qeth_l3_dev_rxip_del4_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_rxip_del_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(rxip_del4, del4, 0200, NULL,
			qeth_l3_dev_rxip_del4_store);

static ssize_t qeth_l3_dev_rxip_add6_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_rxip_add_show(buf, card, QETH_PROT_IPV6);
}

static ssize_t qeth_l3_dev_rxip_add6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_rxip_add_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(rxip_add6, add6, 0644,
			qeth_l3_dev_rxip_add6_show,
			qeth_l3_dev_rxip_add6_store);

static ssize_t qeth_l3_dev_rxip_del6_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev_get_drvdata(dev);

	if (!card)
		return -EINVAL;

	return qeth_l3_dev_rxip_del_store(buf, count, card, QETH_PROT_IPV6);
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

static struct attribute_group qeth_device_rxip_group = {
	.name = "rxip",
	.attrs = qeth_rxip_device_attrs,
};

int qeth_l3_create_device_attributes(struct device *dev)
{
	int ret;

	ret = sysfs_create_group(&dev->kobj, &qeth_l3_device_attr_group);
	if (ret)
		return ret;

	ret = sysfs_create_group(&dev->kobj, &qeth_device_ipato_group);
	if (ret) {
		sysfs_remove_group(&dev->kobj, &qeth_l3_device_attr_group);
		return ret;
	}

	ret = sysfs_create_group(&dev->kobj, &qeth_device_vipa_group);
	if (ret) {
		sysfs_remove_group(&dev->kobj, &qeth_l3_device_attr_group);
		sysfs_remove_group(&dev->kobj, &qeth_device_ipato_group);
		return ret;
	}

	ret = sysfs_create_group(&dev->kobj, &qeth_device_rxip_group);
	if (ret) {
		sysfs_remove_group(&dev->kobj, &qeth_l3_device_attr_group);
		sysfs_remove_group(&dev->kobj, &qeth_device_ipato_group);
		sysfs_remove_group(&dev->kobj, &qeth_device_vipa_group);
		return ret;
	}
	return 0;
}

void qeth_l3_remove_device_attributes(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &qeth_l3_device_attr_group);
	sysfs_remove_group(&dev->kobj, &qeth_device_ipato_group);
	sysfs_remove_group(&dev->kobj, &qeth_device_vipa_group);
	sysfs_remove_group(&dev->kobj, &qeth_device_rxip_group);
}
