/*
 *
 * linux/drivers/s390/net/qeth_fs.c
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

/***** /proc/qeth *****/
#define QETH_PROCFILE_NAME "qeth"
static struct proc_dir_entry *qeth_procfile;

static int
qeth_procfile_seq_match(struct device *dev, void *data)
{
	return(dev ? 1 : 0);
}

static void *
qeth_procfile_seq_start(struct seq_file *s, loff_t *offset)
{
	struct device *dev = NULL;
	loff_t nr = 0;

	if (*offset == 0)
		return SEQ_START_TOKEN;
	while (1) {
		dev = driver_find_device(&qeth_ccwgroup_driver.driver, dev,
					 NULL, qeth_procfile_seq_match);
		if (++nr == *offset)
			break;
		put_device(dev);
	}
	return dev;
}

static void
qeth_procfile_seq_stop(struct seq_file *s, void* it)
{
}

static void *
qeth_procfile_seq_next(struct seq_file *s, void *it, loff_t *offset)
{
	struct device *prev, *next;

	if (it == SEQ_START_TOKEN)
		prev = NULL;
	else
		prev = (struct device *) it;
	next = driver_find_device(&qeth_ccwgroup_driver.driver,
				  prev, NULL, qeth_procfile_seq_match);
	(*offset)++;
	return (void *) next;
}

static inline const char *
qeth_get_router_str(struct qeth_card *card, int ipv)
{
	enum qeth_routing_types routing_type = NO_ROUTER;

	if (ipv == 4) {
		routing_type = card->options.route4.type;
	} else {
#ifdef CONFIG_QETH_IPV6
		routing_type = card->options.route6.type;
#else
		return "n/a";
#endif /* CONFIG_QETH_IPV6 */
	}

	switch (routing_type){
	case PRIMARY_ROUTER:
		return "pri";
	case SECONDARY_ROUTER:
		return "sec";
	case MULTICAST_ROUTER:
		if (card->info.broadcast_capable == QETH_BROADCAST_WITHOUT_ECHO)
			return "mc+";
		return "mc";
	case PRIMARY_CONNECTOR:
		if (card->info.broadcast_capable == QETH_BROADCAST_WITHOUT_ECHO)
			return "p+c";
		return "p.c";
	case SECONDARY_CONNECTOR:
		if (card->info.broadcast_capable == QETH_BROADCAST_WITHOUT_ECHO)
			return "s+c";
		return "s.c";
	default:   /* NO_ROUTER */
		return "no";
	}
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
		put_device(device);
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

static const struct file_operations qeth_procfile_fops = {
	.owner   = THIS_MODULE,
	.open    = qeth_procfile_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/***** /proc/qeth_perf *****/
#define QETH_PERF_PROCFILE_NAME "qeth_perf"
static struct proc_dir_entry *qeth_perf_procfile;

static int
qeth_perf_procfile_seq_show(struct seq_file *s, void *it)
{
	struct device *device;
	struct qeth_card *card;


	if (it == SEQ_START_TOKEN)
		return 0;

	device = (struct device *) it;
	card = device->driver_data;
	seq_printf(s, "For card with devnos %s/%s/%s (%s):\n",
			CARD_RDEV_ID(card),
			CARD_WDEV_ID(card),
			CARD_DDEV_ID(card),
			QETH_CARD_IFNAME(card)
		  );
	if (!card->options.performance_stats)
		seq_printf(s, "Performance statistics are deactivated.\n");
	seq_printf(s, "  Skb's/buffers received                 : %lu/%u\n"
		      "  Skb's/buffers sent                     : %lu/%u\n\n",
		        card->stats.rx_packets -
				card->perf_stats.initial_rx_packets,
			card->perf_stats.bufs_rec,
		        card->stats.tx_packets -
				card->perf_stats.initial_tx_packets,
			card->perf_stats.bufs_sent
		  );
	seq_printf(s, "  Skb's/buffers sent without packing     : %lu/%u\n"
		      "  Skb's/buffers sent with packing        : %u/%u\n\n",
		   card->stats.tx_packets - card->perf_stats.initial_tx_packets
					  - card->perf_stats.skbs_sent_pack,
		   card->perf_stats.bufs_sent - card->perf_stats.bufs_sent_pack,
		   card->perf_stats.skbs_sent_pack,
		   card->perf_stats.bufs_sent_pack
		  );
	seq_printf(s, "  Skbs sent in SG mode                   : %u\n"
		      "  Skb fragments sent in SG mode          : %u\n\n",
		      card->perf_stats.sg_skbs_sent,
		      card->perf_stats.sg_frags_sent);
	seq_printf(s, "  Skbs received in SG mode               : %u\n"
		      "  Skb fragments received in SG mode      : %u\n"
		      "  Page allocations for rx SG mode        : %u\n\n",
		      card->perf_stats.sg_skbs_rx,
		      card->perf_stats.sg_frags_rx,
		      card->perf_stats.sg_alloc_page_rx);
	seq_printf(s, "  large_send tx (in Kbytes)              : %u\n"
		      "  large_send count                       : %u\n\n",
		      card->perf_stats.large_send_bytes >> 10,
		      card->perf_stats.large_send_cnt);
	seq_printf(s, "  Packing state changes no pkg.->packing : %u/%u\n"
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
	seq_printf(s, "  Inbound handler time (in us)           : %u\n"
		      "  Inbound handler count                  : %u\n"
		      "  Inbound do_QDIO time (in us)           : %u\n"
		      "  Inbound do_QDIO count                  : %u\n\n"
		      "  Outbound handler time (in us)          : %u\n"
		      "  Outbound handler count                 : %u\n\n"
		      "  Outbound time (in us, incl QDIO)       : %u\n"
		      "  Outbound count                         : %u\n"
		      "  Outbound do_QDIO time (in us)          : %u\n"
		      "  Outbound do_QDIO count                 : %u\n\n",
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
	put_device(device);
	return 0;
}

static struct seq_operations qeth_perf_procfile_seq_ops = {
	.start = qeth_procfile_seq_start,
	.stop  = qeth_procfile_seq_stop,
	.next  = qeth_procfile_seq_next,
	.show  = qeth_perf_procfile_seq_show,
};

static int
qeth_perf_procfile_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &qeth_perf_procfile_seq_ops);
}

static const struct file_operations qeth_perf_procfile_fops = {
	.owner   = THIS_MODULE,
	.open    = qeth_perf_procfile_open,
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

	qeth_perf_procfile = create_proc_entry(QETH_PERF_PROCFILE_NAME,
					   S_IFREG | 0444, NULL);
	if (qeth_perf_procfile)
		qeth_perf_procfile->proc_fops = &qeth_perf_procfile_fops;

	if (qeth_procfile &&
	    qeth_perf_procfile)
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
}

