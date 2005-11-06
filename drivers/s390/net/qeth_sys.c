/*
 *
 * linux/drivers/s390/net/qeth_sys.c ($Revision: 1.55 $)
 *
 * Linux on zSeries OSA Express and HiperSockets support
 * This file contains code related to sysfs.
 *
 * Copyright 2000,2003 IBM Corporation
 *
 * Author(s): Thomas Spatzier <tspat@de.ibm.com>
 * 	      Frank Pavlic <pavlic@de.ibm.com>
 *
 */
#include <linux/list.h>
#include <linux/rwsem.h>

#include <asm/ebcdic.h>

#include "qeth.h"
#include "qeth_mpc.h"
#include "qeth_fs.h"

const char *VERSION_QETH_SYS_C = "$Revision: 1.55 $";

/*****************************************************************************/
/*                                                                           */
/*          /sys-fs stuff UNDER DEVELOPMENT !!!                              */
/*                                                                           */
/*****************************************************************************/
//low/high watermark

static ssize_t
qeth_dev_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;
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

static ssize_t
qeth_dev_chpid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;
	if (!card)
		return -EINVAL;

	return sprintf(buf, "%02X\n", card->info.chpid);
}

static DEVICE_ATTR(chpid, 0444, qeth_dev_chpid_show, NULL);

static ssize_t
qeth_dev_if_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;
	if (!card)
		return -EINVAL;
	return sprintf(buf, "%s\n", QETH_CARD_IFNAME(card));
}

static DEVICE_ATTR(if_name, 0444, qeth_dev_if_name_show, NULL);

static ssize_t
qeth_dev_card_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;
	if (!card)
		return -EINVAL;

	return sprintf(buf, "%s\n", qeth_get_cardname_short(card));
}

static DEVICE_ATTR(card_type, 0444, qeth_dev_card_type_show, NULL);

static ssize_t
qeth_dev_portno_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;
	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->info.portno);
}

static ssize_t
qeth_dev_portno_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;
	unsigned int portno;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	portno = simple_strtoul(buf, &tmp, 16);
	if ((portno < 0) || (portno > MAX_PORTNO)){
		PRINT_WARN("portno 0x%X is out of range\n", portno);
		return -EINVAL;
	}

	card->info.portno = portno;
	return count;
}

static DEVICE_ATTR(portno, 0644, qeth_dev_portno_show, qeth_dev_portno_store);

static ssize_t
qeth_dev_portname_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;
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

static ssize_t
qeth_dev_portname_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	tmp = strsep((char **) &buf, "\n");
	if ((strlen(tmp) > 8) || (strlen(tmp) < 2))
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

static ssize_t
qeth_dev_checksum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%s checksumming\n", qeth_get_checksum_str(card));
}

static ssize_t
qeth_dev_checksum_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	tmp = strsep((char **) &buf, "\n");
	if (!strcmp(tmp, "sw_checksumming"))
		card->options.checksum_type = SW_CHECKSUMMING;
	else if (!strcmp(tmp, "hw_checksumming"))
		card->options.checksum_type = HW_CHECKSUMMING;
	else if (!strcmp(tmp, "no_checksumming"))
		card->options.checksum_type = NO_CHECKSUMMING;
	else {
		PRINT_WARN("Unknown checksumming type '%s'\n", tmp);
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(checksumming, 0644, qeth_dev_checksum_show,
		qeth_dev_checksum_store);

static ssize_t
qeth_dev_prioqing_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

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

static ssize_t
qeth_dev_prioqing_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
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
		PRINT_WARN("Priority queueing disabled due "
			   "to hardware limitations!\n");
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
		PRINT_WARN("Unknown queueing type '%s'\n", tmp);
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(priority_queueing, 0644, qeth_dev_prioqing_show,
		qeth_dev_prioqing_store);

static ssize_t
qeth_dev_bufcnt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->qdio.in_buf_pool.buf_count);
}

static ssize_t
qeth_dev_bufcnt_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
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
		if ((rc = qeth_realloc_buffer_pool(card, cnt)))
			PRINT_WARN("Error (%d) while setting "
				   "buffer count.\n", rc);
	}
	return count;
}

static DEVICE_ATTR(buffer_count, 0644, qeth_dev_bufcnt_show,
		qeth_dev_bufcnt_store);

static inline ssize_t
qeth_dev_route_show(struct qeth_card *card, struct qeth_routing_info *route,
		    char *buf)
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

static ssize_t
qeth_dev_route4_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_route_show(card, &card->options.route4, buf);
}

static inline ssize_t
qeth_dev_route_store(struct qeth_card *card, struct qeth_routing_info *route,
		enum qeth_prot_versions prot, const char *buf, size_t count)
{
	enum qeth_routing_types old_route_type = route->type;
	char *tmp;
	int rc;

	tmp = strsep((char **) &buf, "\n");

	if (!strcmp(tmp, "no_router")){
		route->type = NO_ROUTER;
	} else if (!strcmp(tmp, "primary_connector")) {
		route->type = PRIMARY_CONNECTOR;
	} else if (!strcmp(tmp, "secondary_connector")) {
		route->type = SECONDARY_CONNECTOR;
	} else if (!strcmp(tmp, "multicast_router")) {
		route->type = MULTICAST_ROUTER;
	} else if (!strcmp(tmp, "primary_router")) {
		route->type = PRIMARY_ROUTER;
	} else if (!strcmp(tmp, "secondary_router")) {
		route->type = SECONDARY_ROUTER;
	} else if (!strcmp(tmp, "multicast_router")) {
		route->type = MULTICAST_ROUTER;
	} else {
		PRINT_WARN("Invalid routing type '%s'.\n", tmp);
		return -EINVAL;
	}
	if (((card->state == CARD_STATE_SOFTSETUP) ||
	     (card->state == CARD_STATE_UP)) &&
	    (old_route_type != route->type)){
		if (prot == QETH_PROT_IPV4)
			rc = qeth_setrouting_v4(card);
		else if (prot == QETH_PROT_IPV6)
			rc = qeth_setrouting_v6(card);
	}
	return count;
}

static ssize_t
qeth_dev_route4_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_route_store(card, &card->options.route4,
			            QETH_PROT_IPV4, buf, count);
}

static DEVICE_ATTR(route4, 0644, qeth_dev_route4_show, qeth_dev_route4_store);

#ifdef CONFIG_QETH_IPV6
static ssize_t
qeth_dev_route6_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	if (!qeth_is_supported(card, IPA_IPV6))
		return sprintf(buf, "%s\n", "n/a");

	return qeth_dev_route_show(card, &card->options.route6, buf);
}

static ssize_t
qeth_dev_route6_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	if (!qeth_is_supported(card, IPA_IPV6)){
		PRINT_WARN("IPv6 not supported for interface %s.\n"
			   "Routing status no changed.\n",
			   QETH_CARD_IFNAME(card));
		return -ENOTSUPP;
	}

	return qeth_dev_route_store(card, &card->options.route6,
			            QETH_PROT_IPV6, buf, count);
}

static DEVICE_ATTR(route6, 0644, qeth_dev_route6_show, qeth_dev_route6_store);
#endif

static ssize_t
qeth_dev_add_hhlen_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->options.add_hhlen);
}

static ssize_t
qeth_dev_add_hhlen_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	i = simple_strtoul(buf, &tmp, 10);
	if ((i < 0) || (i > MAX_ADD_HHLEN)) {
		PRINT_WARN("add_hhlen out of range\n");
		return -EINVAL;
	}
	card->options.add_hhlen = i;

	return count;
}

static DEVICE_ATTR(add_hhlen, 0644, qeth_dev_add_hhlen_show,
		   qeth_dev_add_hhlen_store);

static ssize_t
qeth_dev_fake_ll_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->options.fake_ll? 1:0);
}

static ssize_t
qeth_dev_fake_ll_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	i = simple_strtoul(buf, &tmp, 16);
	if ((i != 0) && (i != 1)) {
		PRINT_WARN("fake_ll: write 0 or 1 to this file!\n");
		return -EINVAL;
	}
	card->options.fake_ll = i;
	return count;
}

static DEVICE_ATTR(fake_ll, 0644, qeth_dev_fake_ll_show,
		   qeth_dev_fake_ll_store);

static ssize_t
qeth_dev_fake_broadcast_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->options.fake_broadcast? 1:0);
}

static ssize_t
qeth_dev_fake_broadcast_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	i = simple_strtoul(buf, &tmp, 16);
	if ((i == 0) || (i == 1))
		card->options.fake_broadcast = i;
	else {
		PRINT_WARN("fake_broadcast: write 0 or 1 to this file!\n");
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(fake_broadcast, 0644, qeth_dev_fake_broadcast_show,
		   qeth_dev_fake_broadcast_store);

static ssize_t
qeth_dev_recover_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
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

static ssize_t
qeth_dev_broadcast_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	if (!((card->info.link_type == QETH_LINK_TYPE_HSTR) ||
	      (card->info.link_type == QETH_LINK_TYPE_LANE_TR)))
		return sprintf(buf, "n/a\n");

	return sprintf(buf, "%s\n", (card->options.broadcast_mode ==
				     QETH_TR_BROADCAST_ALLRINGS)?
		       "all rings":"local");
}

static ssize_t
qeth_dev_broadcast_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	if (!((card->info.link_type == QETH_LINK_TYPE_HSTR) ||
	      (card->info.link_type == QETH_LINK_TYPE_LANE_TR))){
		PRINT_WARN("Device is not a tokenring device!\n");
		return -EINVAL;
	}

	tmp = strsep((char **) &buf, "\n");

	if (!strcmp(tmp, "local")){
		card->options.broadcast_mode = QETH_TR_BROADCAST_LOCAL;
		return count;
	} else if (!strcmp(tmp, "all_rings")) {
		card->options.broadcast_mode = QETH_TR_BROADCAST_ALLRINGS;
		return count;
	} else {
		PRINT_WARN("broadcast_mode: invalid mode %s!\n",
			   tmp);
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(broadcast_mode, 0644, qeth_dev_broadcast_mode_show,
		   qeth_dev_broadcast_mode_store);

static ssize_t
qeth_dev_canonical_macaddr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	if (!((card->info.link_type == QETH_LINK_TYPE_HSTR) ||
	      (card->info.link_type == QETH_LINK_TYPE_LANE_TR)))
		return sprintf(buf, "n/a\n");

	return sprintf(buf, "%i\n", (card->options.macaddr_mode ==
				     QETH_TR_MACADDR_CANONICAL)? 1:0);
}

static ssize_t
qeth_dev_canonical_macaddr_store(struct device *dev, struct device_attribute *attr, const char *buf,
				  size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	if (!((card->info.link_type == QETH_LINK_TYPE_HSTR) ||
	      (card->info.link_type == QETH_LINK_TYPE_LANE_TR))){
		PRINT_WARN("Device is not a tokenring device!\n");
		return -EINVAL;
	}

	i = simple_strtoul(buf, &tmp, 16);
	if ((i == 0) || (i == 1))
		card->options.macaddr_mode = i?
			QETH_TR_MACADDR_CANONICAL :
			QETH_TR_MACADDR_NONCANONICAL;
	else {
		PRINT_WARN("canonical_macaddr: write 0 or 1 to this file!\n");
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(canonical_macaddr, 0644, qeth_dev_canonical_macaddr_show,
		   qeth_dev_canonical_macaddr_store);

static ssize_t
qeth_dev_layer2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", card->options.layer2 ? 1:0);
}

static ssize_t
qeth_dev_layer2_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;
	int i;

	if (!card)
		return -EINVAL;
	if (card->info.type == QETH_CARD_TYPE_IQD) {
                PRINT_WARN("Layer2 on Hipersockets is not supported! \n");
                return -EPERM;
        }

	if (((card->state != CARD_STATE_DOWN) &&
	     (card->state != CARD_STATE_RECOVER)))
		return -EPERM;

	i = simple_strtoul(buf, &tmp, 16);
	if ((i == 0) || (i == 1))
		card->options.layer2 = i;
	else {
		PRINT_WARN("layer2: write 0 or 1 to this file!\n");
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(layer2, 0644, qeth_dev_layer2_show,
		   qeth_dev_layer2_store);

static ssize_t
qeth_dev_large_send_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	switch (card->options.large_send) {
	case QETH_LARGE_SEND_NO:
		return sprintf(buf, "%s\n", "no");
	case QETH_LARGE_SEND_EDDP:
		return sprintf(buf, "%s\n", "EDDP");
	case QETH_LARGE_SEND_TSO:
		return sprintf(buf, "%s\n", "TSO");
	default:
		return sprintf(buf, "%s\n", "N/A");
	}
}

static ssize_t
qeth_dev_large_send_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	enum qeth_large_send_types type;
	int rc = 0;
	char *tmp;

	if (!card)
		return -EINVAL;
	tmp = strsep((char **) &buf, "\n");
	if (!strcmp(tmp, "no")){
		type = QETH_LARGE_SEND_NO;
	} else if (!strcmp(tmp, "EDDP")) {
		type = QETH_LARGE_SEND_EDDP;
	} else if (!strcmp(tmp, "TSO")) {
		type = QETH_LARGE_SEND_TSO;
	} else {
		PRINT_WARN("large_send: invalid mode %s!\n", tmp);
		return -EINVAL;
	}
	if (card->options.large_send == type)
		return count;
	if ((rc = qeth_set_large_send(card, type)))	
		return rc;
	return count;
}

static DEVICE_ATTR(large_send, 0644, qeth_dev_large_send_show,
		   qeth_dev_large_send_store);

static ssize_t
qeth_dev_blkt_show(char *buf, struct qeth_card *card, int value )
{

	if (!card)
		return -EINVAL;

	return sprintf(buf, "%i\n", value);
}

static ssize_t
qeth_dev_blkt_store(struct qeth_card *card, const char *buf, size_t count,
			  int *value, int max_value)
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
		PRINT_WARN("blkt total time: write values between"
			   " 0 and %d to this file!\n", max_value);
		return -EINVAL;
	}
	return count;
}

static ssize_t
qeth_dev_blkt_total_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	return qeth_dev_blkt_show(buf, card, card->info.blkt.time_total);
}


static ssize_t
qeth_dev_blkt_total_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	return qeth_dev_blkt_store(card, buf, count,
				   &card->info.blkt.time_total,1000);
}



static DEVICE_ATTR(total, 0644, qeth_dev_blkt_total_show,
		   qeth_dev_blkt_total_store);

static ssize_t
qeth_dev_blkt_inter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	return qeth_dev_blkt_show(buf, card, card->info.blkt.inter_packet);
}


static ssize_t
qeth_dev_blkt_inter_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	return qeth_dev_blkt_store(card, buf, count,
				   &card->info.blkt.inter_packet,100);
}

static DEVICE_ATTR(inter, 0644, qeth_dev_blkt_inter_show,
		   qeth_dev_blkt_inter_store);

static ssize_t
qeth_dev_blkt_inter_jumbo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	return qeth_dev_blkt_show(buf, card,
				  card->info.blkt.inter_packet_jumbo);
}


static ssize_t
qeth_dev_blkt_inter_jumbo_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	return qeth_dev_blkt_store(card, buf, count,
				   &card->info.blkt.inter_packet_jumbo,100);
}

static DEVICE_ATTR(inter_jumbo, 0644, qeth_dev_blkt_inter_jumbo_show,
		   qeth_dev_blkt_inter_jumbo_store);

static struct device_attribute * qeth_blkt_device_attrs[] = {
	&dev_attr_total,
	&dev_attr_inter,
	&dev_attr_inter_jumbo,
	NULL,
};

static struct attribute_group qeth_device_blkt_group = {
	.name = "blkt",
	.attrs = (struct attribute **)qeth_blkt_device_attrs,
};

static struct device_attribute * qeth_device_attrs[] = {
	&dev_attr_state,
	&dev_attr_chpid,
	&dev_attr_if_name,
	&dev_attr_card_type,
	&dev_attr_portno,
	&dev_attr_portname,
	&dev_attr_checksumming,
	&dev_attr_priority_queueing,
	&dev_attr_buffer_count,
	&dev_attr_route4,
#ifdef CONFIG_QETH_IPV6
	&dev_attr_route6,
#endif
	&dev_attr_add_hhlen,
	&dev_attr_fake_ll,
	&dev_attr_fake_broadcast,
	&dev_attr_recover,
	&dev_attr_broadcast_mode,
	&dev_attr_canonical_macaddr,
	&dev_attr_layer2,
	&dev_attr_large_send,
	NULL,
};

static struct attribute_group qeth_device_attr_group = {
	.attrs = (struct attribute **)qeth_device_attrs,
};

static struct device_attribute * qeth_osn_device_attrs[] = {
	&dev_attr_state,
	&dev_attr_chpid,
	&dev_attr_if_name,
	&dev_attr_card_type,
	&dev_attr_buffer_count,
	&dev_attr_recover,
	NULL,
};

static struct attribute_group qeth_osn_device_attr_group = {
	.attrs = (struct attribute **)qeth_osn_device_attrs,
};

#define QETH_DEVICE_ATTR(_id,_name,_mode,_show,_store)			     \
struct device_attribute dev_attr_##_id = {				     \
	.attr = {.name=__stringify(_name), .mode=_mode, .owner=THIS_MODULE },\
	.show	= _show,						     \
	.store	= _store,						     \
};

int
qeth_check_layer2(struct qeth_card *card)
{
	if (card->options.layer2)
		return -EPERM;
	return 0;
}


static ssize_t
qeth_dev_ipato_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	if (qeth_check_layer2(card))
		return -EPERM;
	return sprintf(buf, "%i\n", card->ipato.enabled? 1:0);
}

static ssize_t
qeth_dev_ipato_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;

	if (!card)
		return -EINVAL;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	if (qeth_check_layer2(card))
		return -EPERM;

	tmp = strsep((char **) &buf, "\n");
	if (!strcmp(tmp, "toggle")){
		card->ipato.enabled = (card->ipato.enabled)? 0 : 1;
	} else if (!strcmp(tmp, "1")){
		card->ipato.enabled = 1;
	} else if (!strcmp(tmp, "0")){
		card->ipato.enabled = 0;
	} else {
		PRINT_WARN("ipato_enable: write 0, 1 or 'toggle' to "
			   "this file\n");
		return -EINVAL;
	}
	return count;
}

static QETH_DEVICE_ATTR(ipato_enable, enable, 0644,
			qeth_dev_ipato_enable_show,
			qeth_dev_ipato_enable_store);

static ssize_t
qeth_dev_ipato_invert4_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	if (qeth_check_layer2(card))
		return -EPERM;

	return sprintf(buf, "%i\n", card->ipato.invert4? 1:0);
}

static ssize_t
qeth_dev_ipato_invert4_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;

	if (!card)
		return -EINVAL;

	if (qeth_check_layer2(card))
		return -EPERM;

	tmp = strsep((char **) &buf, "\n");
	if (!strcmp(tmp, "toggle")){
		card->ipato.invert4 = (card->ipato.invert4)? 0 : 1;
	} else if (!strcmp(tmp, "1")){
		card->ipato.invert4 = 1;
	} else if (!strcmp(tmp, "0")){
		card->ipato.invert4 = 0;
	} else {
		PRINT_WARN("ipato_invert4: write 0, 1 or 'toggle' to "
			   "this file\n");
		return -EINVAL;
	}
	return count;
}

static QETH_DEVICE_ATTR(ipato_invert4, invert4, 0644,
			qeth_dev_ipato_invert4_show,
			qeth_dev_ipato_invert4_store);

static inline ssize_t
qeth_dev_ipato_add_show(char *buf, struct qeth_card *card,
			enum qeth_prot_versions proto)
{
	struct qeth_ipato_entry *ipatoe;
	unsigned long flags;
	char addr_str[40];
	int entry_len; /* length of 1 entry string, differs between v4 and v6 */
	int i = 0;

	if (qeth_check_layer2(card))
		return -EPERM;

	entry_len = (proto == QETH_PROT_IPV4)? 12 : 40;
	/* add strlen for "/<mask>\n" */
	entry_len += (proto == QETH_PROT_IPV4)? 5 : 6;
	spin_lock_irqsave(&card->ip_lock, flags);
	list_for_each_entry(ipatoe, &card->ipato.entries, entry){
		if (ipatoe->proto != proto)
			continue;
		/* String must not be longer than PAGE_SIZE. So we check if
		 * string length gets near PAGE_SIZE. Then we can savely display
		 * the next IPv6 address (worst case, compared to IPv4) */
		if ((PAGE_SIZE - i) <= entry_len)
			break;
		qeth_ipaddr_to_string(proto, ipatoe->addr, addr_str);
		i += snprintf(buf + i, PAGE_SIZE - i,
			      "%s/%i\n", addr_str, ipatoe->mask_bits);
	}
	spin_unlock_irqrestore(&card->ip_lock, flags);
	i += snprintf(buf + i, PAGE_SIZE - i, "\n");

	return i;
}

static ssize_t
qeth_dev_ipato_add4_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_ipato_add_show(buf, card, QETH_PROT_IPV4);
}

static inline int
qeth_parse_ipatoe(const char* buf, enum qeth_prot_versions proto,
		  u8 *addr, int *mask_bits)
{
	const char *start, *end;
	char *tmp;
	char buffer[49] = {0, };

	start = buf;
	/* get address string */
	end = strchr(start, '/');
	if (!end){
		PRINT_WARN("Invalid format for ipato_addx/delx. "
			   "Use <ip addr>/<mask bits>\n");
		return -EINVAL;
	}
	strncpy(buffer, start, end - start);
	if (qeth_string_to_ipaddr(buffer, proto, addr)){
		PRINT_WARN("Invalid IP address format!\n");
		return -EINVAL;
	}
	start = end + 1;
	*mask_bits = simple_strtoul(start, &tmp, 10);

	return 0;
}

static inline ssize_t
qeth_dev_ipato_add_store(const char *buf, size_t count,
			 struct qeth_card *card, enum qeth_prot_versions proto)
{
	struct qeth_ipato_entry *ipatoe;
	u8 addr[16];
	int mask_bits;
	int rc;

	if (qeth_check_layer2(card))
		return -EPERM;
	if ((rc = qeth_parse_ipatoe(buf, proto, addr, &mask_bits)))
		return rc;

	if (!(ipatoe = kmalloc(sizeof(struct qeth_ipato_entry), GFP_KERNEL))){
		PRINT_WARN("No memory to allocate ipato entry\n");
		return -ENOMEM;
	}
	memset(ipatoe, 0, sizeof(struct qeth_ipato_entry));
	ipatoe->proto = proto;
	memcpy(ipatoe->addr, addr, (proto == QETH_PROT_IPV4)? 4:16);
	ipatoe->mask_bits = mask_bits;

	if ((rc = qeth_add_ipato_entry(card, ipatoe))){
		kfree(ipatoe);
		return rc;
	}

	return count;
}

static ssize_t
qeth_dev_ipato_add4_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_ipato_add_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(ipato_add4, add4, 0644,
			qeth_dev_ipato_add4_show,
			qeth_dev_ipato_add4_store);

static inline ssize_t
qeth_dev_ipato_del_store(const char *buf, size_t count,
			 struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16];
	int mask_bits;
	int rc;

	if (qeth_check_layer2(card))
		return -EPERM;
	if ((rc = qeth_parse_ipatoe(buf, proto, addr, &mask_bits)))
		return rc;

	qeth_del_ipato_entry(card, proto, addr, mask_bits);

	return count;
}

static ssize_t
qeth_dev_ipato_del4_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_ipato_del_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(ipato_del4, del4, 0200, NULL,
			qeth_dev_ipato_del4_store);

#ifdef CONFIG_QETH_IPV6
static ssize_t
qeth_dev_ipato_invert6_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	if (qeth_check_layer2(card))
		return -EPERM;

	return sprintf(buf, "%i\n", card->ipato.invert6? 1:0);
}

static ssize_t
qeth_dev_ipato_invert6_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;
	char *tmp;

	if (!card)
		return -EINVAL;

	if (qeth_check_layer2(card))
		return -EPERM;

	tmp = strsep((char **) &buf, "\n");
	if (!strcmp(tmp, "toggle")){
		card->ipato.invert6 = (card->ipato.invert6)? 0 : 1;
	} else if (!strcmp(tmp, "1")){
		card->ipato.invert6 = 1;
	} else if (!strcmp(tmp, "0")){
		card->ipato.invert6 = 0;
	} else {
		PRINT_WARN("ipato_invert6: write 0, 1 or 'toggle' to "
			   "this file\n");
		return -EINVAL;
	}
	return count;
}

static QETH_DEVICE_ATTR(ipato_invert6, invert6, 0644,
			qeth_dev_ipato_invert6_show,
			qeth_dev_ipato_invert6_store);


static ssize_t
qeth_dev_ipato_add6_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_ipato_add_show(buf, card, QETH_PROT_IPV6);
}

static ssize_t
qeth_dev_ipato_add6_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_ipato_add_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(ipato_add6, add6, 0644,
			qeth_dev_ipato_add6_show,
			qeth_dev_ipato_add6_store);

static ssize_t
qeth_dev_ipato_del6_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_ipato_del_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(ipato_del6, del6, 0200, NULL,
			qeth_dev_ipato_del6_store);
#endif /* CONFIG_QETH_IPV6 */

static struct device_attribute * qeth_ipato_device_attrs[] = {
	&dev_attr_ipato_enable,
	&dev_attr_ipato_invert4,
	&dev_attr_ipato_add4,
	&dev_attr_ipato_del4,
#ifdef CONFIG_QETH_IPV6
	&dev_attr_ipato_invert6,
	&dev_attr_ipato_add6,
	&dev_attr_ipato_del6,
#endif
	NULL,
};

static struct attribute_group qeth_device_ipato_group = {
	.name = "ipa_takeover",
	.attrs = (struct attribute **)qeth_ipato_device_attrs,
};

static inline ssize_t
qeth_dev_vipa_add_show(char *buf, struct qeth_card *card,
			enum qeth_prot_versions proto)
{
	struct qeth_ipaddr *ipaddr;
	char addr_str[40];
	int entry_len; /* length of 1 entry string, differs between v4 and v6 */
	unsigned long flags;
	int i = 0;

	if (qeth_check_layer2(card))
		return -EPERM;

	entry_len = (proto == QETH_PROT_IPV4)? 12 : 40;
	entry_len += 2; /* \n + terminator */
	spin_lock_irqsave(&card->ip_lock, flags);
	list_for_each_entry(ipaddr, &card->ip_list, entry){
		if (ipaddr->proto != proto)
			continue;
		if (ipaddr->type != QETH_IP_TYPE_VIPA)
			continue;
		/* String must not be longer than PAGE_SIZE. So we check if
		 * string length gets near PAGE_SIZE. Then we can savely display
		 * the next IPv6 address (worst case, compared to IPv4) */
		if ((PAGE_SIZE - i) <= entry_len)
			break;
		qeth_ipaddr_to_string(proto, (const u8 *)&ipaddr->u, addr_str);
		i += snprintf(buf + i, PAGE_SIZE - i, "%s\n", addr_str);
	}
	spin_unlock_irqrestore(&card->ip_lock, flags);
	i += snprintf(buf + i, PAGE_SIZE - i, "\n");

	return i;
}

static ssize_t
qeth_dev_vipa_add4_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_vipa_add_show(buf, card, QETH_PROT_IPV4);
}

static inline int
qeth_parse_vipae(const char* buf, enum qeth_prot_versions proto,
		 u8 *addr)
{
	if (qeth_string_to_ipaddr(buf, proto, addr)){
		PRINT_WARN("Invalid IP address format!\n");
		return -EINVAL;
	}
	return 0;
}

static inline ssize_t
qeth_dev_vipa_add_store(const char *buf, size_t count,
			 struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16] = {0, };
	int rc;

	if (qeth_check_layer2(card))
		return -EPERM;
	if ((rc = qeth_parse_vipae(buf, proto, addr)))
		return rc;

	if ((rc = qeth_add_vipa(card, proto, addr)))
		return rc;

	return count;
}

static ssize_t
qeth_dev_vipa_add4_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_vipa_add_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(vipa_add4, add4, 0644,
			qeth_dev_vipa_add4_show,
			qeth_dev_vipa_add4_store);

static inline ssize_t
qeth_dev_vipa_del_store(const char *buf, size_t count,
			 struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16];
	int rc;

	if (qeth_check_layer2(card))
		return -EPERM;
	if ((rc = qeth_parse_vipae(buf, proto, addr)))
		return rc;

	qeth_del_vipa(card, proto, addr);

	return count;
}

static ssize_t
qeth_dev_vipa_del4_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_vipa_del_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(vipa_del4, del4, 0200, NULL,
			qeth_dev_vipa_del4_store);

#ifdef CONFIG_QETH_IPV6
static ssize_t
qeth_dev_vipa_add6_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_vipa_add_show(buf, card, QETH_PROT_IPV6);
}

static ssize_t
qeth_dev_vipa_add6_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_vipa_add_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(vipa_add6, add6, 0644,
			qeth_dev_vipa_add6_show,
			qeth_dev_vipa_add6_store);

static ssize_t
qeth_dev_vipa_del6_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	if (qeth_check_layer2(card))
		return -EPERM;

	return qeth_dev_vipa_del_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(vipa_del6, del6, 0200, NULL,
			qeth_dev_vipa_del6_store);
#endif /* CONFIG_QETH_IPV6 */

static struct device_attribute * qeth_vipa_device_attrs[] = {
	&dev_attr_vipa_add4,
	&dev_attr_vipa_del4,
#ifdef CONFIG_QETH_IPV6
	&dev_attr_vipa_add6,
	&dev_attr_vipa_del6,
#endif
	NULL,
};

static struct attribute_group qeth_device_vipa_group = {
	.name = "vipa",
	.attrs = (struct attribute **)qeth_vipa_device_attrs,
};

static inline ssize_t
qeth_dev_rxip_add_show(char *buf, struct qeth_card *card,
		       enum qeth_prot_versions proto)
{
	struct qeth_ipaddr *ipaddr;
	char addr_str[40];
	int entry_len; /* length of 1 entry string, differs between v4 and v6 */
	unsigned long flags;
	int i = 0;

	if (qeth_check_layer2(card))
		return -EPERM;

	entry_len = (proto == QETH_PROT_IPV4)? 12 : 40;
	entry_len += 2; /* \n + terminator */
	spin_lock_irqsave(&card->ip_lock, flags);
	list_for_each_entry(ipaddr, &card->ip_list, entry){
		if (ipaddr->proto != proto)
			continue;
		if (ipaddr->type != QETH_IP_TYPE_RXIP)
			continue;
		/* String must not be longer than PAGE_SIZE. So we check if
		 * string length gets near PAGE_SIZE. Then we can savely display
		 * the next IPv6 address (worst case, compared to IPv4) */
		if ((PAGE_SIZE - i) <= entry_len)
			break;
		qeth_ipaddr_to_string(proto, (const u8 *)&ipaddr->u, addr_str);
		i += snprintf(buf + i, PAGE_SIZE - i, "%s\n", addr_str);
	}
	spin_unlock_irqrestore(&card->ip_lock, flags);
	i += snprintf(buf + i, PAGE_SIZE - i, "\n");

	return i;
}

static ssize_t
qeth_dev_rxip_add4_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_rxip_add_show(buf, card, QETH_PROT_IPV4);
}

static inline int
qeth_parse_rxipe(const char* buf, enum qeth_prot_versions proto,
		 u8 *addr)
{
	if (qeth_string_to_ipaddr(buf, proto, addr)){
		PRINT_WARN("Invalid IP address format!\n");
		return -EINVAL;
	}
	return 0;
}

static inline ssize_t
qeth_dev_rxip_add_store(const char *buf, size_t count,
			struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16] = {0, };
	int rc;

	if (qeth_check_layer2(card))
		return -EPERM;
	if ((rc = qeth_parse_rxipe(buf, proto, addr)))
		return rc;

	if ((rc = qeth_add_rxip(card, proto, addr)))
		return rc;

	return count;
}

static ssize_t
qeth_dev_rxip_add4_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_rxip_add_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(rxip_add4, add4, 0644,
			qeth_dev_rxip_add4_show,
			qeth_dev_rxip_add4_store);

static inline ssize_t
qeth_dev_rxip_del_store(const char *buf, size_t count,
			struct qeth_card *card, enum qeth_prot_versions proto)
{
	u8 addr[16];
	int rc;

	if (qeth_check_layer2(card))
		return -EPERM;
	if ((rc = qeth_parse_rxipe(buf, proto, addr)))
		return rc;

	qeth_del_rxip(card, proto, addr);

	return count;
}

static ssize_t
qeth_dev_rxip_del4_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_rxip_del_store(buf, count, card, QETH_PROT_IPV4);
}

static QETH_DEVICE_ATTR(rxip_del4, del4, 0200, NULL,
			qeth_dev_rxip_del4_store);

#ifdef CONFIG_QETH_IPV6
static ssize_t
qeth_dev_rxip_add6_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_rxip_add_show(buf, card, QETH_PROT_IPV6);
}

static ssize_t
qeth_dev_rxip_add6_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_rxip_add_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(rxip_add6, add6, 0644,
			qeth_dev_rxip_add6_show,
			qeth_dev_rxip_add6_store);

static ssize_t
qeth_dev_rxip_del6_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct qeth_card *card = dev->driver_data;

	if (!card)
		return -EINVAL;

	return qeth_dev_rxip_del_store(buf, count, card, QETH_PROT_IPV6);
}

static QETH_DEVICE_ATTR(rxip_del6, del6, 0200, NULL,
			qeth_dev_rxip_del6_store);
#endif /* CONFIG_QETH_IPV6 */

static struct device_attribute * qeth_rxip_device_attrs[] = {
	&dev_attr_rxip_add4,
	&dev_attr_rxip_del4,
#ifdef CONFIG_QETH_IPV6
	&dev_attr_rxip_add6,
	&dev_attr_rxip_del6,
#endif
	NULL,
};

static struct attribute_group qeth_device_rxip_group = {
	.name = "rxip",
	.attrs = (struct attribute **)qeth_rxip_device_attrs,
};

int
qeth_create_device_attributes(struct device *dev)
{
	int ret;
	struct qeth_card *card = dev->driver_data;

	if (card->info.type == QETH_CARD_TYPE_OSN)
		return sysfs_create_group(&dev->kobj,
					  &qeth_osn_device_attr_group);
   	
	if ((ret = sysfs_create_group(&dev->kobj, &qeth_device_attr_group)))
		return ret;
	if ((ret = sysfs_create_group(&dev->kobj, &qeth_device_ipato_group))){
		sysfs_remove_group(&dev->kobj, &qeth_device_attr_group);
		return ret;
	}
	if ((ret = sysfs_create_group(&dev->kobj, &qeth_device_vipa_group))){
		sysfs_remove_group(&dev->kobj, &qeth_device_attr_group);
		sysfs_remove_group(&dev->kobj, &qeth_device_ipato_group);
		return ret;
	}
	if ((ret = sysfs_create_group(&dev->kobj, &qeth_device_rxip_group))){
		sysfs_remove_group(&dev->kobj, &qeth_device_attr_group);
		sysfs_remove_group(&dev->kobj, &qeth_device_ipato_group);
		sysfs_remove_group(&dev->kobj, &qeth_device_vipa_group);
	}
	if ((ret = sysfs_create_group(&dev->kobj, &qeth_device_blkt_group)))
		return ret;

	return ret;
}

void
qeth_remove_device_attributes(struct device *dev)
{
	struct qeth_card *card = dev->driver_data;

	if (card->info.type == QETH_CARD_TYPE_OSN)
		return sysfs_remove_group(&dev->kobj,
					  &qeth_osn_device_attr_group);
		      
	sysfs_remove_group(&dev->kobj, &qeth_device_attr_group);
	sysfs_remove_group(&dev->kobj, &qeth_device_ipato_group);
	sysfs_remove_group(&dev->kobj, &qeth_device_vipa_group);
	sysfs_remove_group(&dev->kobj, &qeth_device_rxip_group);
	sysfs_remove_group(&dev->kobj, &qeth_device_blkt_group);
}

/**********************/
/* DRIVER ATTRIBUTES  */
/**********************/
static ssize_t
qeth_driver_group_store(struct device_driver *ddrv, const char *buf,
			size_t count)
{
	const char *start, *end;
	char bus_ids[3][BUS_ID_SIZE], *argv[3];
	int i;
	int err;

	start = buf;
	for (i = 0; i < 3; i++) {
		static const char delim[] = { ',', ',', '\n' };
		int len;

		if (!(end = strchr(start, delim[i])))
			return -EINVAL;
		len = min_t(ptrdiff_t, BUS_ID_SIZE, end - start);
		strncpy(bus_ids[i], start, len);
		bus_ids[i][len] = '\0';
		start = end + 1;
		argv[i] = bus_ids[i];
	}
	err = ccwgroup_create(qeth_root_dev, qeth_ccwgroup_driver.driver_id,
			&qeth_ccw_driver, 3, argv);
	if (err)
		return err;
	else
		return count;
}


static DRIVER_ATTR(group, 0200, 0, qeth_driver_group_store);

static ssize_t
qeth_driver_notifier_register_store(struct device_driver *ddrv, const char *buf,
				size_t count)
{
	int rc;
	int signum;
	char *tmp, *tmp2;

	tmp = strsep((char **) &buf, "\n");
	if (!strncmp(tmp, "unregister", 10)){
		if ((rc = qeth_notifier_unregister(current)))
			return rc;
		return count;
	}

	signum = simple_strtoul(tmp, &tmp2, 10);
	if ((signum < 0) || (signum > 32)){
		PRINT_WARN("Signal number %d is out of range\n", signum);
		return -EINVAL;
	}
	if ((rc = qeth_notifier_register(current, signum)))
		return rc;

	return count;
}

static DRIVER_ATTR(notifier_register, 0200, 0,
		   qeth_driver_notifier_register_store);

int
qeth_create_driver_attributes(void)
{
	int rc;

	if ((rc = driver_create_file(&qeth_ccwgroup_driver.driver,
				     &driver_attr_group)))
		return rc;
	return driver_create_file(&qeth_ccwgroup_driver.driver,
				  &driver_attr_notifier_register);
}

void
qeth_remove_driver_attributes(void)
{
	driver_remove_file(&qeth_ccwgroup_driver.driver,
			&driver_attr_group);
	driver_remove_file(&qeth_ccwgroup_driver.driver,
			&driver_attr_notifier_register);
}
