/*
 *
 * linux/drivers/s390/net/qeth_fs.c ($Revision: 1.13 $)
 *
 * Linux on zSeries OSA Express and HiperSockets support
 * This file contains code related to procfs.
 *
 * Copyright 2000,2003 IBM Corporation
 *
 * Author(s): Thomas Spatzier <tspat@de.ibm.com>
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/rwsem.h>

#include "qeth.h"
#include "qeth_mpc.h"
#include "qeth_fs.h"

const char *VERSION_QETH_PROC_C = "$Revision: 1.13 $";

/***** /proc/qeth *****/
#define QETH_PROCFILE_NAME "qeth"
static struct proc_dir_entry *qeth_procfile;

static int
qeth_procfile_seq_match(struct device *dev, void *data)
{
	return 1;
}

static void *
qeth_procfile_seq_start(struct seq_file *s, loff_t *offset)
{
	struct device *dev;
	loff_t nr;

	down_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);

	nr = *offset;
	if (nr == 0)
		return SEQ_START_TOKEN;

	dev = driver_find_device(&qeth_ccwgroup_driver.driver, NULL,
				 NULL, qeth_procfile_seq_match);

	/* get card at pos *offset */
	nr = *offset;
	while (nr-- > 1 && dev)
		dev = driver_find_device(&qeth_ccwgroup_driver.driver, dev,
					 NULL, qeth_procfile_seq_match);
	return (void *) dev;
}

static void
qeth_procfile_seq_stop(struct seq_file *s, void* it)
{
	up_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);
}

static void *
qeth_procfile_seq_next(struct seq_file *s, void *it, loff_t *offset)
{
	struct device *prev, *next;

	if (it == SEQ_START_TOKEN) {
		next = driver_find_device(&qeth_ccwgroup_driver.driver,
					  NULL, NULL, qeth_procfile_seq_match);
		if (next)
			(*offset)++;
		return (void *) next;
	}
	prev = (struct device *) it;
	next = driver_find_device(&qeth_ccwgroup_driver.driver,
				  prev, NULL, qeth_procfile_seq_match);
	if (next)
		(*offset)++;
	return (void *) next;
}

static inline const char *
qeth_get_router_str(struct qeth_card *card, int ipv)
{
	int routing_type = 0;

	if (ipv == 4){
		routing_type = card->options.route4.type;
	} else {
#ifdef CONFIG_QETH_IPV6
		routing_type = card->options.route6.type;
#else
		return "n/a";
#endif /* CONFIG_QETH_IPV6 */
	}

	if (routing_type == PRIMARY_ROUTER)
		return "pri";
	else if (routing_type == SECONDARY_ROUTER)
		return "sec";
	else if (routing_type == MULTICAST_ROUTER) {
		if (card->info.broadcast_capable == QETH_BROADCAST_WITHOUT_ECHO)
			return "mc+";
		return "mc";
	} else if (routing_type == PRIMARY_CONNECTOR) {
		if (card->info.broadcast_capable == QETH_BROADCAST_WITHOUT_ECHO)
			return "p+c";
		return "p.c";
	} else if (routing_type == SECONDARY_CONNECTOR) {
		if (card->info.broadcast_capable == QETH_BROADCAST_WITHOUT_ECHO)
			return "s+c";
		return "s.c";
	} else if (routing_type == NO_ROUTER)
		return "no";
	else
		return "unk";
}

static int
qeth_procfile_seq_show(struct seq_file *s, void *it)
{
	struct device *device;
	struct qeth_card *card;
	char tmp[12]; /* for qeth_get_prioq_str */

	if (it == SEQ_START_TOKEN){
		seq_printf(s, "devices                    CHPID interface  "
		              "cardtype       port chksum prio-q'ing rtr4 "
			      "rtr6 fsz   cnt\n");
		seq_printf(s, "-------------------------- ----- ---------- "
			      "-------------- ---- ------ ---------- ---- "
			      "---- ----- -----\n");
	} else {
		device = (struct device *) it;
		card = device->driver_data;
		seq_printf(s, "%s/%s/%s x%02X   %-10s %-14s %-4i ",
				CARD_RDEV_ID(card),
				CARD_WDEV_ID(card),
				CARD_DDEV_ID(card),
				card->info.chpid,
				QETH_CARD_IFNAME(card),
				qeth_get_cardname_short(card),
				card->info.portno);
		if (card->lan_online)
			seq_printf(s, "%-6s %-10s %-4s %-4s %-5s %-5i\n",
					qeth_get_checksum_str(card),
					qeth_get_prioq_str(card, tmp),
					qeth_get_router_str(card, 4),
					qeth_get_router_str(card, 6),
					qeth_get_bufsize_str(card),
					card->qdio.in_buf_pool.buf_count);
		else
			seq_printf(s, "  +++ LAN OFFLINE +++\n");
	}
	return 0;
}

static struct seq_operations qeth_procfile_seq_ops = {
	.start = qeth_procfile_seq_start,
	.stop  = qeth_procfile_seq_stop,
	.next  = qeth_procfile_seq_next,
	.show  = qeth_procfile_seq_show,
};

static int
qeth_procfile_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &qeth_procfile_seq_ops);
}

static struct file_operations qeth_procfile_fops = {
	.owner   = THIS_MODULE,
	.open    = qeth_procfile_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/***** /proc/qeth_perf *****/
#define QETH_PERF_PROCFILE_NAME "qeth_perf"
static struct proc_dir_entry *qeth_perf_procfile;

#ifdef CONFIG_QETH_PERF_STATS

static void *
qeth_perf_procfile_seq_start(struct seq_file *s, loff_t *offset)
{
	struct device *dev = NULL;
	int nr;

	down_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);
	/* get card at pos *offset */
	dev = driver_find_device(&qeth_ccwgroup_driver.driver, NULL, NULL,
				 qeth_procfile_seq_match);

	/* get card at pos *offset */
	nr = *offset;
	while (nr-- > 1 && dev)
		dev = driver_find_device(&qeth_ccwgroup_driver.driver, dev,
					 NULL, qeth_procfile_seq_match);
	return (void *) dev;
}

static void
qeth_perf_procfile_seq_stop(struct seq_file *s, void* it)
{
	up_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);
}

static void *
qeth_perf_procfile_seq_next(struct seq_file *s, void *it, loff_t *offset)
{
	struct device *prev, *next;

	prev = (struct device *) it;
	next = driver_find_device(&qeth_ccwgroup_driver.driver, prev,
				  NULL, qeth_procfile_seq_match);
	if (next)
		(*offset)++;
	return (void *) next;
}

static int
qeth_perf_procfile_seq_show(struct seq_file *s, void *it)
{
	struct device *device;
	struct qeth_card *card;

	device = (struct device *) it;
	card = device->driver_data;
	seq_printf(s, "For card with devnos %s/%s/%s (%s):\n",
			CARD_RDEV_ID(card),
			CARD_WDEV_ID(card),
			CARD_DDEV_ID(card),
			QETH_CARD_IFNAME(card)
		  );
	seq_printf(s, "  Skb's/buffers received                 : %li/%i\n"
		      "  Skb's/buffers sent                     : %li/%i\n\n",
		        card->stats.rx_packets, card->perf_stats.bufs_rec,
		        card->stats.tx_packets, card->perf_stats.bufs_sent
		  );
	seq_printf(s, "  Skb's/buffers sent without packing     : %li/%i\n"
		      "  Skb's/buffers sent with packing        : %i/%i\n\n",
		   card->stats.tx_packets - card->perf_stats.skbs_sent_pack,
		   card->perf_stats.bufs_sent - card->perf_stats.bufs_sent_pack,
		   card->perf_stats.skbs_sent_pack,
		   card->perf_stats.bufs_sent_pack
		  );
	seq_printf(s, "  Skbs sent in SG mode                   : %i\n"
		      "  Skb fragments sent in SG mode          : %i\n\n",
		      card->perf_stats.sg_skbs_sent,
		      card->perf_stats.sg_frags_sent);
	seq_printf(s, "  large_send tx (in Kbytes)              : %i\n"
		      "  large_send count                       : %i\n\n",
		      card->perf_stats.large_send_bytes >> 10,
		      card->perf_stats.large_send_cnt);
	seq_printf(s, "  Packing state changes no pkg.->packing : %i/%i\n"
		      "  Watermarks L/H                         : %i/%i\n"
		      "  Current buffer usage (outbound q's)    : "
		      "%i/%i/%i/%i\n\n",
		        card->perf_stats.sc_dp_p, card->perf_stats.sc_p_dp,
			QETH_LOW_WATERMARK_PACK, QETH_HIGH_WATERMARK_PACK,
			atomic_read(&card->qdio.out_qs[0]->used_buffers),
			(card->qdio.no_out_queues > 1)?
				atomic_read(&card->qdio.out_qs[1]->used_buffers)
				: 0,
			(card->qdio.no_out_queues > 2)?
				atomic_read(&card->qdio.out_qs[2]->used_buffers)
				: 0,
			(card->qdio.no_out_queues > 3)?
				atomic_read(&card->qdio.out_qs[3]->used_buffers)
				: 0
		  );
	seq_printf(s, "  Inbound handler time (in us)           : %i\n"
		      "  Inbound handler count                  : %i\n"
		      "  Inbound do_QDIO time (in us)           : %i\n"
		      "  Inbound do_QDIO count                  : %i\n\n"
		      "  Outbound handler time (in us)          : %i\n"
		      "  Outbound handler count                 : %i\n\n"
		      "  Outbound time (in us, incl QDIO)       : %i\n"
		      "  Outbound count                         : %i\n"
		      "  Outbound do_QDIO time (in us)          : %i\n"
		      "  Outbound do_QDIO count                 : %i\n\n",
		        card->perf_stats.inbound_time,
			card->perf_stats.inbound_cnt,
		        card->perf_stats.inbound_do_qdio_time,
			card->perf_stats.inbound_do_qdio_cnt,
			card->perf_stats.outbound_handler_time,
			card->perf_stats.outbound_handler_cnt,
			card->perf_stats.outbound_time,
			card->perf_stats.outbound_cnt,
		        card->perf_stats.outbound_do_qdio_time,
			card->perf_stats.outbound_do_qdio_cnt
		  );
	return 0;
}

static struct seq_operations qeth_perf_procfile_seq_ops = {
	.start = qeth_perf_procfile_seq_start,
	.stop  = qeth_perf_procfile_seq_stop,
	.next  = qeth_perf_procfile_seq_next,
	.show  = qeth_perf_procfile_seq_show,
};

static int
qeth_perf_procfile_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &qeth_perf_procfile_seq_ops);
}

static struct file_operations qeth_perf_procfile_fops = {
	.owner   = THIS_MODULE,
	.open    = qeth_perf_procfile_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

#define qeth_perf_procfile_created qeth_perf_procfile
#else
#define qeth_perf_procfile_created 1
#endif /* CONFIG_QETH_PERF_STATS */

/***** /proc/qeth_ipa_takeover *****/
#define QETH_IPATO_PROCFILE_NAME "qeth_ipa_takeover"
static struct proc_dir_entry *qeth_ipato_procfile;

static void *
qeth_ipato_procfile_seq_start(struct seq_file *s, loff_t *offset)
{
	struct device *dev;
	loff_t nr;

	down_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);
	/* TODO: finish this */
	/*
	 * maybe SEQ_SATRT_TOKEN can be returned for offset 0
	 * output driver settings then;
	 * else output setting for respective card
	 */

	dev = driver_find_device(&qeth_ccwgroup_driver.driver, NULL, NULL,
				 qeth_procfile_seq_match);

	/* get card at pos *offset */
	nr = *offset;
	while (nr-- > 1 && dev)
		dev = driver_find_device(&qeth_ccwgroup_driver.driver, dev,
					 NULL, qeth_procfile_seq_match);
	return (void *) dev;
}

static void
qeth_ipato_procfile_seq_stop(struct seq_file *s, void* it)
{
	up_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);
}

static void *
qeth_ipato_procfile_seq_next(struct seq_file *s, void *it, loff_t *offset)
{
	struct device *prev, *next;

	prev = (struct device *) it;
	next = driver_find_device(&qeth_ccwgroup_driver.driver, prev,
				  NULL, qeth_procfile_seq_match);
	if (next)
		(*offset)++;
	return (void *) next;
}

static int
qeth_ipato_procfile_seq_show(struct seq_file *s, void *it)
{
	struct device *device;
	struct qeth_card *card;

	/* TODO: finish this */
	/*
	 * maybe SEQ_SATRT_TOKEN can be returned for offset 0
	 * output driver settings then;
	 * else output setting for respective card
	 */
	device = (struct device *) it;
	card = device->driver_data;

	return 0;
}

static struct seq_operations qeth_ipato_procfile_seq_ops = {
	.start = qeth_ipato_procfile_seq_start,
	.stop  = qeth_ipato_procfile_seq_stop,
	.next  = qeth_ipato_procfile_seq_next,
	.show  = qeth_ipato_procfile_seq_show,
};

static int
qeth_ipato_procfile_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &qeth_ipato_procfile_seq_ops);
}

static struct file_operations qeth_ipato_procfile_fops = {
	.owner   = THIS_MODULE,
	.open    = qeth_ipato_procfile_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

int __init
qeth_create_procfs_entries(void)
{
	qeth_procfile = create_proc_entry(QETH_PROCFILE_NAME,
					   S_IFREG | 0444, NULL);
	if (qeth_procfile)
		qeth_procfile->proc_fops = &qeth_procfile_fops;

#ifdef CONFIG_QETH_PERF_STATS
	qeth_perf_procfile = create_proc_entry(QETH_PERF_PROCFILE_NAME,
					   S_IFREG | 0444, NULL);
	if (qeth_perf_procfile)
		qeth_perf_procfile->proc_fops = &qeth_perf_procfile_fops;
#endif /* CONFIG_QETH_PERF_STATS */

	qeth_ipato_procfile = create_proc_entry(QETH_IPATO_PROCFILE_NAME,
					   S_IFREG | 0444, NULL);
	if (qeth_ipato_procfile)
		qeth_ipato_procfile->proc_fops = &qeth_ipato_procfile_fops;

	if (qeth_procfile &&
	    qeth_ipato_procfile &&
	    qeth_perf_procfile_created)
		return 0;
	else
		return -ENOMEM;
}

void __exit
qeth_remove_procfs_entries(void)
{
	if (qeth_procfile)
		remove_proc_entry(QETH_PROCFILE_NAME, NULL);
	if (qeth_perf_procfile)
		remove_proc_entry(QETH_PERF_PROCFILE_NAME, NULL);
	if (qeth_ipato_procfile)
		remove_proc_entry(QETH_IPATO_PROCFILE_NAME, NULL);
}


/* ONLY FOR DEVELOPMENT! -> make it as module */
/*
static void
qeth_create_sysfs_entries(void)
{
	struct device *dev;

	down_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);

	list_for_each_entry(dev, &qeth_ccwgroup_driver.driver.devices,
			driver_list)
		qeth_create_device_attributes(dev);

	up_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);
}

static void
qeth_remove_sysfs_entries(void)
{
	struct device *dev;

	down_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);

	list_for_each_entry(dev, &qeth_ccwgroup_driver.driver.devices,
			driver_list)
		qeth_remove_device_attributes(dev);

	up_read(&qeth_ccwgroup_driver.driver.bus->subsys.rwsem);
}

static int __init
qeth_fs_init(void)
{
	printk(KERN_INFO "qeth_fs_init\n");
	qeth_create_procfs_entries();
	qeth_create_sysfs_entries();

	return 0;
}

static void __exit
qeth_fs_exit(void)
{
	printk(KERN_INFO "qeth_fs_exit\n");
	qeth_remove_procfs_entries();
	qeth_remove_sysfs_entries();
}


module_init(qeth_fs_init);
module_exit(qeth_fs_exit);

MODULE_LICENSE("GPL");
*/
