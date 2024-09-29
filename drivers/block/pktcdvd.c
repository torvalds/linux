/*
 * Copyright (C) 2000 Jens Axboe <axboe@suse.de>
 * Copyright (C) 2001-2004 Peter Osterlund <petero2@telia.com>
 * Copyright (C) 2006 Thomas Maier <balagi@justmail.de>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Packet writing layer for ATAPI and SCSI CD-RW, DVD+RW, DVD-RW and
 * DVD-RAM devices.
 *
 * Theory of operation:
 *
 * At the lowest level, there is the standard driver for the CD/DVD device,
 * such as drivers/scsi/sr.c. This driver can handle read and write requests,
 * but it doesn't know anything about the special restrictions that apply to
 * packet writing. One restriction is that write requests must be aligned to
 * packet boundaries on the physical media, and the size of a write request
 * must be equal to the packet size. Another restriction is that a
 * GPCMD_FLUSH_CACHE command has to be issued to the drive before a read
 * command, if the previous command was a write.
 *
 * The purpose of the packet writing driver is to hide these restrictions from
 * higher layers, such as file systems, and present a block device that can be
 * randomly read and written using 2kB-sized blocks.
 *
 * The lowest layer in the packet writing driver is the packet I/O scheduler.
 * Its data is defined by the struct packet_iosched and includes two bio
 * queues with pending read and write requests. These queues are processed
 * by the pkt_iosched_process_queue() function. The write requests in this
 * queue are already properly aligned and sized. This layer is responsible for
 * issuing the flush cache commands and scheduling the I/O in a good order.
 *
 * The next layer transforms unaligned write requests to aligned writes. This
 * transformation requires reading missing pieces of data from the underlying
 * block device, assembling the pieces to full packets and queuing them to the
 * packet I/O scheduler.
 *
 * At the top layer there is a custom ->submit_bio function that forwards
 * read requests directly to the iosched queue and puts write requests in the
 * unaligned write queue. A kernel thread performs the necessary read
 * gathering to convert the unaligned writes to aligned writes and then feeds
 * them to the packet I/O scheduler.
 *
 *************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/backing-dev.h>
#include <linux/compat.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nospec.h>
#include <linux/pktcdvd.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_ioctl.h>

#include <asm/unaligned.h>

#define DRIVER_NAME	"pktcdvd"

#define MAX_SPEED 0xffff

static DEFINE_MUTEX(pktcdvd_mutex);
static struct pktcdvd_device *pkt_devs[MAX_WRITERS];
static struct proc_dir_entry *pkt_proc;
static int pktdev_major;
static int write_congestion_on  = PKT_WRITE_CONGESTION_ON;
static int write_congestion_off = PKT_WRITE_CONGESTION_OFF;
static struct mutex ctl_mutex;	/* Serialize open/close/setup/teardown */
static mempool_t psd_pool;
static struct bio_set pkt_bio_set;

/* /sys/class/pktcdvd */
static struct class	class_pktcdvd;
static struct dentry	*pkt_debugfs_root = NULL; /* /sys/kernel/debug/pktcdvd */

/* forward declaration */
static int pkt_setup_dev(dev_t dev, dev_t* pkt_dev);
static int pkt_remove_dev(dev_t pkt_dev);

static sector_t get_zone(sector_t sector, struct pktcdvd_device *pd)
{
	return (sector + pd->offset) & ~(sector_t)(pd->settings.size - 1);
}

/**********************************************************
 * sysfs interface for pktcdvd
 * by (C) 2006  Thomas Maier <balagi@justmail.de>
 
  /sys/class/pktcdvd/pktcdvd[0-7]/
                     stat/reset
                     stat/packets_started
                     stat/packets_finished
                     stat/kb_written
                     stat/kb_read
                     stat/kb_read_gather
                     write_queue/size
                     write_queue/congestion_off
                     write_queue/congestion_on
 **********************************************************/

static ssize_t packets_started_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", pd->stats.pkt_started);
}
static DEVICE_ATTR_RO(packets_started);

static ssize_t packets_finished_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", pd->stats.pkt_ended);
}
static DEVICE_ATTR_RO(packets_finished);

static ssize_t kb_written_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", pd->stats.secs_w >> 1);
}
static DEVICE_ATTR_RO(kb_written);

static ssize_t kb_read_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", pd->stats.secs_r >> 1);
}
static DEVICE_ATTR_RO(kb_read);

static ssize_t kb_read_gather_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", pd->stats.secs_rg >> 1);
}
static DEVICE_ATTR_RO(kb_read_gather);

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t len)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);

	if (len > 0) {
		pd->stats.pkt_started = 0;
		pd->stats.pkt_ended = 0;
		pd->stats.secs_w = 0;
		pd->stats.secs_rg = 0;
		pd->stats.secs_r = 0;
	}
	return len;
}
static DEVICE_ATTR_WO(reset);

static struct attribute *pkt_stat_attrs[] = {
	&dev_attr_packets_finished.attr,
	&dev_attr_packets_started.attr,
	&dev_attr_kb_read.attr,
	&dev_attr_kb_written.attr,
	&dev_attr_kb_read_gather.attr,
	&dev_attr_reset.attr,
	NULL,
};

static const struct attribute_group pkt_stat_group = {
	.name = "stat",
	.attrs = pkt_stat_attrs,
};

static ssize_t size_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);
	int n;

	spin_lock(&pd->lock);
	n = sysfs_emit(buf, "%d\n", pd->bio_queue_size);
	spin_unlock(&pd->lock);
	return n;
}
static DEVICE_ATTR_RO(size);

static void init_write_congestion_marks(int* lo, int* hi)
{
	if (*hi > 0) {
		*hi = max(*hi, 500);
		*hi = min(*hi, 1000000);
		if (*lo <= 0)
			*lo = *hi - 100;
		else {
			*lo = min(*lo, *hi - 100);
			*lo = max(*lo, 100);
		}
	} else {
		*hi = -1;
		*lo = -1;
	}
}

static ssize_t congestion_off_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);
	int n;

	spin_lock(&pd->lock);
	n = sysfs_emit(buf, "%d\n", pd->write_congestion_off);
	spin_unlock(&pd->lock);
	return n;
}

static ssize_t congestion_off_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	spin_lock(&pd->lock);
	pd->write_congestion_off = val;
	init_write_congestion_marks(&pd->write_congestion_off, &pd->write_congestion_on);
	spin_unlock(&pd->lock);
	return len;
}
static DEVICE_ATTR_RW(congestion_off);

static ssize_t congestion_on_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);
	int n;

	spin_lock(&pd->lock);
	n = sysfs_emit(buf, "%d\n", pd->write_congestion_on);
	spin_unlock(&pd->lock);
	return n;
}

static ssize_t congestion_on_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct pktcdvd_device *pd = dev_get_drvdata(dev);
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	spin_lock(&pd->lock);
	pd->write_congestion_on = val;
	init_write_congestion_marks(&pd->write_congestion_off, &pd->write_congestion_on);
	spin_unlock(&pd->lock);
	return len;
}
static DEVICE_ATTR_RW(congestion_on);

static struct attribute *pkt_wq_attrs[] = {
	&dev_attr_congestion_on.attr,
	&dev_attr_congestion_off.attr,
	&dev_attr_size.attr,
	NULL,
};

static const struct attribute_group pkt_wq_group = {
	.name = "write_queue",
	.attrs = pkt_wq_attrs,
};

static const struct attribute_group *pkt_groups[] = {
	&pkt_stat_group,
	&pkt_wq_group,
	NULL,
};

static void pkt_sysfs_dev_new(struct pktcdvd_device *pd)
{
	if (class_is_registered(&class_pktcdvd)) {
		pd->dev = device_create_with_groups(&class_pktcdvd, NULL,
						    MKDEV(0, 0), pd, pkt_groups,
						    "%s", pd->disk->disk_name);
		if (IS_ERR(pd->dev))
			pd->dev = NULL;
	}
}

static void pkt_sysfs_dev_remove(struct pktcdvd_device *pd)
{
	if (class_is_registered(&class_pktcdvd))
		device_unregister(pd->dev);
}


/********************************************************************
  /sys/class/pktcdvd/
                     add            map block device
                     remove         unmap packet dev
                     device_map     show mappings
 *******************************************************************/

static ssize_t device_map_show(const struct class *c, const struct class_attribute *attr,
			       char *data)
{
	int n = 0;
	int idx;
	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
	for (idx = 0; idx < MAX_WRITERS; idx++) {
		struct pktcdvd_device *pd = pkt_devs[idx];
		if (!pd)
			continue;
		n += sysfs_emit_at(data, n, "%s %u:%u %u:%u\n",
			pd->disk->disk_name,
			MAJOR(pd->pkt_dev), MINOR(pd->pkt_dev),
			MAJOR(file_bdev(pd->bdev_file)->bd_dev),
			MINOR(file_bdev(pd->bdev_file)->bd_dev));
	}
	mutex_unlock(&ctl_mutex);
	return n;
}
static CLASS_ATTR_RO(device_map);

static ssize_t add_store(const struct class *c, const struct class_attribute *attr,
			 const char *buf, size_t count)
{
	unsigned int major, minor;

	if (sscanf(buf, "%u:%u", &major, &minor) == 2) {
		/* pkt_setup_dev() expects caller to hold reference to self */
		if (!try_module_get(THIS_MODULE))
			return -ENODEV;

		pkt_setup_dev(MKDEV(major, minor), NULL);

		module_put(THIS_MODULE);

		return count;
	}

	return -EINVAL;
}
static CLASS_ATTR_WO(add);

static ssize_t remove_store(const struct class *c, const struct class_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned int major, minor;
	if (sscanf(buf, "%u:%u", &major, &minor) == 2) {
		pkt_remove_dev(MKDEV(major, minor));
		return count;
	}
	return -EINVAL;
}
static CLASS_ATTR_WO(remove);

static struct attribute *class_pktcdvd_attrs[] = {
	&class_attr_add.attr,
	&class_attr_remove.attr,
	&class_attr_device_map.attr,
	NULL,
};
ATTRIBUTE_GROUPS(class_pktcdvd);

static struct class class_pktcdvd = {
	.name		= DRIVER_NAME,
	.class_groups	= class_pktcdvd_groups,
};

static int pkt_sysfs_init(void)
{
	/*
	 * create control files in sysfs
	 * /sys/class/pktcdvd/...
	 */
	return class_register(&class_pktcdvd);
}

static void pkt_sysfs_cleanup(void)
{
	class_unregister(&class_pktcdvd);
}

/********************************************************************
  entries in debugfs

  /sys/kernel/debug/pktcdvd[0-7]/
			info

 *******************************************************************/

static void pkt_count_states(struct pktcdvd_device *pd, int *states)
{
	struct packet_data *pkt;
	int i;

	for (i = 0; i < PACKET_NUM_STATES; i++)
		states[i] = 0;

	spin_lock(&pd->cdrw.active_list_lock);
	list_for_each_entry(pkt, &pd->cdrw.pkt_active_list, list) {
		states[pkt->state]++;
	}
	spin_unlock(&pd->cdrw.active_list_lock);
}

static int pkt_seq_show(struct seq_file *m, void *p)
{
	struct pktcdvd_device *pd = m->private;
	char *msg;
	int states[PACKET_NUM_STATES];

	seq_printf(m, "Writer %s mapped to %pg:\n", pd->disk->disk_name,
		   file_bdev(pd->bdev_file));

	seq_printf(m, "\nSettings:\n");
	seq_printf(m, "\tpacket size:\t\t%dkB\n", pd->settings.size / 2);

	if (pd->settings.write_type == 0)
		msg = "Packet";
	else
		msg = "Unknown";
	seq_printf(m, "\twrite type:\t\t%s\n", msg);

	seq_printf(m, "\tpacket type:\t\t%s\n", pd->settings.fp ? "Fixed" : "Variable");
	seq_printf(m, "\tlink loss:\t\t%d\n", pd->settings.link_loss);

	seq_printf(m, "\ttrack mode:\t\t%d\n", pd->settings.track_mode);

	if (pd->settings.block_mode == PACKET_BLOCK_MODE1)
		msg = "Mode 1";
	else if (pd->settings.block_mode == PACKET_BLOCK_MODE2)
		msg = "Mode 2";
	else
		msg = "Unknown";
	seq_printf(m, "\tblock mode:\t\t%s\n", msg);

	seq_printf(m, "\nStatistics:\n");
	seq_printf(m, "\tpackets started:\t%lu\n", pd->stats.pkt_started);
	seq_printf(m, "\tpackets ended:\t\t%lu\n", pd->stats.pkt_ended);
	seq_printf(m, "\twritten:\t\t%lukB\n", pd->stats.secs_w >> 1);
	seq_printf(m, "\tread gather:\t\t%lukB\n", pd->stats.secs_rg >> 1);
	seq_printf(m, "\tread:\t\t\t%lukB\n", pd->stats.secs_r >> 1);

	seq_printf(m, "\nMisc:\n");
	seq_printf(m, "\treference count:\t%d\n", pd->refcnt);
	seq_printf(m, "\tflags:\t\t\t0x%lx\n", pd->flags);
	seq_printf(m, "\tread speed:\t\t%ukB/s\n", pd->read_speed);
	seq_printf(m, "\twrite speed:\t\t%ukB/s\n", pd->write_speed);
	seq_printf(m, "\tstart offset:\t\t%lu\n", pd->offset);
	seq_printf(m, "\tmode page offset:\t%u\n", pd->mode_offset);

	seq_printf(m, "\nQueue state:\n");
	seq_printf(m, "\tbios queued:\t\t%d\n", pd->bio_queue_size);
	seq_printf(m, "\tbios pending:\t\t%d\n", atomic_read(&pd->cdrw.pending_bios));
	seq_printf(m, "\tcurrent sector:\t\t0x%llx\n", pd->current_sector);

	pkt_count_states(pd, states);
	seq_printf(m, "\tstate:\t\t\ti:%d ow:%d rw:%d ww:%d rec:%d fin:%d\n",
		   states[0], states[1], states[2], states[3], states[4], states[5]);

	seq_printf(m, "\twrite congestion marks:\toff=%d on=%d\n",
			pd->write_congestion_off,
			pd->write_congestion_on);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pkt_seq);

static void pkt_debugfs_dev_new(struct pktcdvd_device *pd)
{
	if (!pkt_debugfs_root)
		return;
	pd->dfs_d_root = debugfs_create_dir(pd->disk->disk_name, pkt_debugfs_root);

	pd->dfs_f_info = debugfs_create_file("info", 0444, pd->dfs_d_root,
					     pd, &pkt_seq_fops);
}

static void pkt_debugfs_dev_remove(struct pktcdvd_device *pd)
{
	if (!pkt_debugfs_root)
		return;
	debugfs_remove(pd->dfs_f_info);
	debugfs_remove(pd->dfs_d_root);
	pd->dfs_f_info = NULL;
	pd->dfs_d_root = NULL;
}

static void pkt_debugfs_init(void)
{
	pkt_debugfs_root = debugfs_create_dir(DRIVER_NAME, NULL);
}

static void pkt_debugfs_cleanup(void)
{
	debugfs_remove(pkt_debugfs_root);
	pkt_debugfs_root = NULL;
}

/* ----------------------------------------------------------*/


static void pkt_bio_finished(struct pktcdvd_device *pd)
{
	struct device *ddev = disk_to_dev(pd->disk);

	BUG_ON(atomic_read(&pd->cdrw.pending_bios) <= 0);
	if (atomic_dec_and_test(&pd->cdrw.pending_bios)) {
		dev_dbg(ddev, "queue empty\n");
		atomic_set(&pd->iosched.attention, 1);
		wake_up(&pd->wqueue);
	}
}

/*
 * Allocate a packet_data struct
 */
static struct packet_data *pkt_alloc_packet_data(int frames)
{
	int i;
	struct packet_data *pkt;

	pkt = kzalloc(sizeof(struct packet_data), GFP_KERNEL);
	if (!pkt)
		goto no_pkt;

	pkt->frames = frames;
	pkt->w_bio = bio_kmalloc(frames, GFP_KERNEL);
	if (!pkt->w_bio)
		goto no_bio;

	for (i = 0; i < frames / FRAMES_PER_PAGE; i++) {
		pkt->pages[i] = alloc_page(GFP_KERNEL|__GFP_ZERO);
		if (!pkt->pages[i])
			goto no_page;
	}

	spin_lock_init(&pkt->lock);
	bio_list_init(&pkt->orig_bios);

	for (i = 0; i < frames; i++) {
		pkt->r_bios[i] = bio_kmalloc(1, GFP_KERNEL);
		if (!pkt->r_bios[i])
			goto no_rd_bio;
	}

	return pkt;

no_rd_bio:
	for (i = 0; i < frames; i++)
		kfree(pkt->r_bios[i]);
no_page:
	for (i = 0; i < frames / FRAMES_PER_PAGE; i++)
		if (pkt->pages[i])
			__free_page(pkt->pages[i]);
	kfree(pkt->w_bio);
no_bio:
	kfree(pkt);
no_pkt:
	return NULL;
}

/*
 * Free a packet_data struct
 */
static void pkt_free_packet_data(struct packet_data *pkt)
{
	int i;

	for (i = 0; i < pkt->frames; i++)
		kfree(pkt->r_bios[i]);
	for (i = 0; i < pkt->frames / FRAMES_PER_PAGE; i++)
		__free_page(pkt->pages[i]);
	kfree(pkt->w_bio);
	kfree(pkt);
}

static void pkt_shrink_pktlist(struct pktcdvd_device *pd)
{
	struct packet_data *pkt, *next;

	BUG_ON(!list_empty(&pd->cdrw.pkt_active_list));

	list_for_each_entry_safe(pkt, next, &pd->cdrw.pkt_free_list, list) {
		pkt_free_packet_data(pkt);
	}
	INIT_LIST_HEAD(&pd->cdrw.pkt_free_list);
}

static int pkt_grow_pktlist(struct pktcdvd_device *pd, int nr_packets)
{
	struct packet_data *pkt;

	BUG_ON(!list_empty(&pd->cdrw.pkt_free_list));

	while (nr_packets > 0) {
		pkt = pkt_alloc_packet_data(pd->settings.size >> 2);
		if (!pkt) {
			pkt_shrink_pktlist(pd);
			return 0;
		}
		pkt->id = nr_packets;
		pkt->pd = pd;
		list_add(&pkt->list, &pd->cdrw.pkt_free_list);
		nr_packets--;
	}
	return 1;
}

static inline struct pkt_rb_node *pkt_rbtree_next(struct pkt_rb_node *node)
{
	struct rb_node *n = rb_next(&node->rb_node);
	if (!n)
		return NULL;
	return rb_entry(n, struct pkt_rb_node, rb_node);
}

static void pkt_rbtree_erase(struct pktcdvd_device *pd, struct pkt_rb_node *node)
{
	rb_erase(&node->rb_node, &pd->bio_queue);
	mempool_free(node, &pd->rb_pool);
	pd->bio_queue_size--;
	BUG_ON(pd->bio_queue_size < 0);
}

/*
 * Find the first node in the pd->bio_queue rb tree with a starting sector >= s.
 */
static struct pkt_rb_node *pkt_rbtree_find(struct pktcdvd_device *pd, sector_t s)
{
	struct rb_node *n = pd->bio_queue.rb_node;
	struct rb_node *next;
	struct pkt_rb_node *tmp;

	if (!n) {
		BUG_ON(pd->bio_queue_size > 0);
		return NULL;
	}

	for (;;) {
		tmp = rb_entry(n, struct pkt_rb_node, rb_node);
		if (s <= tmp->bio->bi_iter.bi_sector)
			next = n->rb_left;
		else
			next = n->rb_right;
		if (!next)
			break;
		n = next;
	}

	if (s > tmp->bio->bi_iter.bi_sector) {
		tmp = pkt_rbtree_next(tmp);
		if (!tmp)
			return NULL;
	}
	BUG_ON(s > tmp->bio->bi_iter.bi_sector);
	return tmp;
}

/*
 * Insert a node into the pd->bio_queue rb tree.
 */
static void pkt_rbtree_insert(struct pktcdvd_device *pd, struct pkt_rb_node *node)
{
	struct rb_node **p = &pd->bio_queue.rb_node;
	struct rb_node *parent = NULL;
	sector_t s = node->bio->bi_iter.bi_sector;
	struct pkt_rb_node *tmp;

	while (*p) {
		parent = *p;
		tmp = rb_entry(parent, struct pkt_rb_node, rb_node);
		if (s < tmp->bio->bi_iter.bi_sector)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&node->rb_node, parent, p);
	rb_insert_color(&node->rb_node, &pd->bio_queue);
	pd->bio_queue_size++;
}

/*
 * Send a packet_command to the underlying block device and
 * wait for completion.
 */
static int pkt_generic_packet(struct pktcdvd_device *pd, struct packet_command *cgc)
{
	struct request_queue *q = bdev_get_queue(file_bdev(pd->bdev_file));
	struct scsi_cmnd *scmd;
	struct request *rq;
	int ret = 0;

	rq = scsi_alloc_request(q, (cgc->data_direction == CGC_DATA_WRITE) ?
			     REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(rq))
		return PTR_ERR(rq);
	scmd = blk_mq_rq_to_pdu(rq);

	if (cgc->buflen) {
		ret = blk_rq_map_kern(q, rq, cgc->buffer, cgc->buflen,
				      GFP_NOIO);
		if (ret)
			goto out;
	}

	scmd->cmd_len = COMMAND_SIZE(cgc->cmd[0]);
	memcpy(scmd->cmnd, cgc->cmd, CDROM_PACKET_SIZE);

	rq->timeout = 60*HZ;
	if (cgc->quiet)
		rq->rq_flags |= RQF_QUIET;

	blk_execute_rq(rq, false);
	if (scmd->result)
		ret = -EIO;
out:
	blk_mq_free_request(rq);
	return ret;
}

static const char *sense_key_string(__u8 index)
{
	static const char * const info[] = {
		"No sense", "Recovered error", "Not ready",
		"Medium error", "Hardware error", "Illegal request",
		"Unit attention", "Data protect", "Blank check",
	};

	return index < ARRAY_SIZE(info) ? info[index] : "INVALID";
}

/*
 * A generic sense dump / resolve mechanism should be implemented across
 * all ATAPI + SCSI devices.
 */
static void pkt_dump_sense(struct pktcdvd_device *pd,
			   struct packet_command *cgc)
{
	struct device *ddev = disk_to_dev(pd->disk);
	struct scsi_sense_hdr *sshdr = cgc->sshdr;

	if (sshdr)
		dev_err(ddev, "%*ph - sense %02x.%02x.%02x (%s)\n",
			CDROM_PACKET_SIZE, cgc->cmd,
			sshdr->sense_key, sshdr->asc, sshdr->ascq,
			sense_key_string(sshdr->sense_key));
	else
		dev_err(ddev, "%*ph - no sense\n", CDROM_PACKET_SIZE, cgc->cmd);
}

/*
 * flush the drive cache to media
 */
static int pkt_flush_cache(struct pktcdvd_device *pd)
{
	struct packet_command cgc;

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_FLUSH_CACHE;
	cgc.quiet = 1;

	/*
	 * the IMMED bit -- we default to not setting it, although that
	 * would allow a much faster close, this is safer
	 */
#if 0
	cgc.cmd[1] = 1 << 1;
#endif
	return pkt_generic_packet(pd, &cgc);
}

/*
 * speed is given as the normal factor, e.g. 4 for 4x
 */
static noinline_for_stack int pkt_set_speed(struct pktcdvd_device *pd,
				unsigned write_speed, unsigned read_speed)
{
	struct packet_command cgc;
	struct scsi_sense_hdr sshdr;
	int ret;

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.sshdr = &sshdr;
	cgc.cmd[0] = GPCMD_SET_SPEED;
	put_unaligned_be16(read_speed, &cgc.cmd[2]);
	put_unaligned_be16(write_speed, &cgc.cmd[4]);

	ret = pkt_generic_packet(pd, &cgc);
	if (ret)
		pkt_dump_sense(pd, &cgc);

	return ret;
}

/*
 * Queue a bio for processing by the low-level CD device. Must be called
 * from process context.
 */
static void pkt_queue_bio(struct pktcdvd_device *pd, struct bio *bio)
{
	/*
	 * Some CDRW drives can not handle writes larger than one packet,
	 * even if the size is a multiple of the packet size.
	 */
	bio->bi_opf |= REQ_NOMERGE;

	spin_lock(&pd->iosched.lock);
	if (bio_data_dir(bio) == READ)
		bio_list_add(&pd->iosched.read_queue, bio);
	else
		bio_list_add(&pd->iosched.write_queue, bio);
	spin_unlock(&pd->iosched.lock);

	atomic_set(&pd->iosched.attention, 1);
	wake_up(&pd->wqueue);
}

/*
 * Process the queued read/write requests. This function handles special
 * requirements for CDRW drives:
 * - A cache flush command must be inserted before a read request if the
 *   previous request was a write.
 * - Switching between reading and writing is slow, so don't do it more often
 *   than necessary.
 * - Optimize for throughput at the expense of latency. This means that streaming
 *   writes will never be interrupted by a read, but if the drive has to seek
 *   before the next write, switch to reading instead if there are any pending
 *   read requests.
 * - Set the read speed according to current usage pattern. When only reading
 *   from the device, it's best to use the highest possible read speed, but
 *   when switching often between reading and writing, it's better to have the
 *   same read and write speeds.
 */
static void pkt_iosched_process_queue(struct pktcdvd_device *pd)
{
	struct device *ddev = disk_to_dev(pd->disk);

	if (atomic_read(&pd->iosched.attention) == 0)
		return;
	atomic_set(&pd->iosched.attention, 0);

	for (;;) {
		struct bio *bio;
		int reads_queued, writes_queued;

		spin_lock(&pd->iosched.lock);
		reads_queued = !bio_list_empty(&pd->iosched.read_queue);
		writes_queued = !bio_list_empty(&pd->iosched.write_queue);
		spin_unlock(&pd->iosched.lock);

		if (!reads_queued && !writes_queued)
			break;

		if (pd->iosched.writing) {
			int need_write_seek = 1;
			spin_lock(&pd->iosched.lock);
			bio = bio_list_peek(&pd->iosched.write_queue);
			spin_unlock(&pd->iosched.lock);
			if (bio && (bio->bi_iter.bi_sector ==
				    pd->iosched.last_write))
				need_write_seek = 0;
			if (need_write_seek && reads_queued) {
				if (atomic_read(&pd->cdrw.pending_bios) > 0) {
					dev_dbg(ddev, "write, waiting\n");
					break;
				}
				pkt_flush_cache(pd);
				pd->iosched.writing = 0;
			}
		} else {
			if (!reads_queued && writes_queued) {
				if (atomic_read(&pd->cdrw.pending_bios) > 0) {
					dev_dbg(ddev, "read, waiting\n");
					break;
				}
				pd->iosched.writing = 1;
			}
		}

		spin_lock(&pd->iosched.lock);
		if (pd->iosched.writing)
			bio = bio_list_pop(&pd->iosched.write_queue);
		else
			bio = bio_list_pop(&pd->iosched.read_queue);
		spin_unlock(&pd->iosched.lock);

		if (!bio)
			continue;

		if (bio_data_dir(bio) == READ)
			pd->iosched.successive_reads +=
				bio->bi_iter.bi_size >> 10;
		else {
			pd->iosched.successive_reads = 0;
			pd->iosched.last_write = bio_end_sector(bio);
		}
		if (pd->iosched.successive_reads >= HI_SPEED_SWITCH) {
			if (pd->read_speed == pd->write_speed) {
				pd->read_speed = MAX_SPEED;
				pkt_set_speed(pd, pd->write_speed, pd->read_speed);
			}
		} else {
			if (pd->read_speed != pd->write_speed) {
				pd->read_speed = pd->write_speed;
				pkt_set_speed(pd, pd->write_speed, pd->read_speed);
			}
		}

		atomic_inc(&pd->cdrw.pending_bios);
		submit_bio_noacct(bio);
	}
}

/*
 * Special care is needed if the underlying block device has a small
 * max_phys_segments value.
 */
static int pkt_set_segment_merging(struct pktcdvd_device *pd, struct request_queue *q)
{
	struct device *ddev = disk_to_dev(pd->disk);

	if ((pd->settings.size << 9) / CD_FRAMESIZE <= queue_max_segments(q)) {
		/*
		 * The cdrom device can handle one segment/frame
		 */
		clear_bit(PACKET_MERGE_SEGS, &pd->flags);
		return 0;
	}

	if ((pd->settings.size << 9) / PAGE_SIZE <= queue_max_segments(q)) {
		/*
		 * We can handle this case at the expense of some extra memory
		 * copies during write operations
		 */
		set_bit(PACKET_MERGE_SEGS, &pd->flags);
		return 0;
	}

	dev_err(ddev, "cdrom max_phys_segments too small\n");
	return -EIO;
}

static void pkt_end_io_read(struct bio *bio)
{
	struct packet_data *pkt = bio->bi_private;
	struct pktcdvd_device *pd = pkt->pd;
	BUG_ON(!pd);

	dev_dbg(disk_to_dev(pd->disk), "bio=%p sec0=%llx sec=%llx err=%d\n",
		bio, pkt->sector, bio->bi_iter.bi_sector, bio->bi_status);

	if (bio->bi_status)
		atomic_inc(&pkt->io_errors);
	bio_uninit(bio);
	if (atomic_dec_and_test(&pkt->io_wait)) {
		atomic_inc(&pkt->run_sm);
		wake_up(&pd->wqueue);
	}
	pkt_bio_finished(pd);
}

static void pkt_end_io_packet_write(struct bio *bio)
{
	struct packet_data *pkt = bio->bi_private;
	struct pktcdvd_device *pd = pkt->pd;
	BUG_ON(!pd);

	dev_dbg(disk_to_dev(pd->disk), "id=%d, err=%d\n", pkt->id, bio->bi_status);

	pd->stats.pkt_ended++;

	bio_uninit(bio);
	pkt_bio_finished(pd);
	atomic_dec(&pkt->io_wait);
	atomic_inc(&pkt->run_sm);
	wake_up(&pd->wqueue);
}

/*
 * Schedule reads for the holes in a packet
 */
static void pkt_gather_data(struct pktcdvd_device *pd, struct packet_data *pkt)
{
	struct device *ddev = disk_to_dev(pd->disk);
	int frames_read = 0;
	struct bio *bio;
	int f;
	char written[PACKET_MAX_SIZE];

	BUG_ON(bio_list_empty(&pkt->orig_bios));

	atomic_set(&pkt->io_wait, 0);
	atomic_set(&pkt->io_errors, 0);

	/*
	 * Figure out which frames we need to read before we can write.
	 */
	memset(written, 0, sizeof(written));
	spin_lock(&pkt->lock);
	bio_list_for_each(bio, &pkt->orig_bios) {
		int first_frame = (bio->bi_iter.bi_sector - pkt->sector) /
			(CD_FRAMESIZE >> 9);
		int num_frames = bio->bi_iter.bi_size / CD_FRAMESIZE;
		pd->stats.secs_w += num_frames * (CD_FRAMESIZE >> 9);
		BUG_ON(first_frame < 0);
		BUG_ON(first_frame + num_frames > pkt->frames);
		for (f = first_frame; f < first_frame + num_frames; f++)
			written[f] = 1;
	}
	spin_unlock(&pkt->lock);

	if (pkt->cache_valid) {
		dev_dbg(ddev, "zone %llx cached\n", pkt->sector);
		goto out_account;
	}

	/*
	 * Schedule reads for missing parts of the packet.
	 */
	for (f = 0; f < pkt->frames; f++) {
		int p, offset;

		if (written[f])
			continue;

		bio = pkt->r_bios[f];
		bio_init(bio, file_bdev(pd->bdev_file), bio->bi_inline_vecs, 1,
			 REQ_OP_READ);
		bio->bi_iter.bi_sector = pkt->sector + f * (CD_FRAMESIZE >> 9);
		bio->bi_end_io = pkt_end_io_read;
		bio->bi_private = pkt;

		p = (f * CD_FRAMESIZE) / PAGE_SIZE;
		offset = (f * CD_FRAMESIZE) % PAGE_SIZE;
		dev_dbg(ddev, "Adding frame %d, page:%p offs:%d\n", f,
			pkt->pages[p], offset);
		if (!bio_add_page(bio, pkt->pages[p], CD_FRAMESIZE, offset))
			BUG();

		atomic_inc(&pkt->io_wait);
		pkt_queue_bio(pd, bio);
		frames_read++;
	}

out_account:
	dev_dbg(ddev, "need %d frames for zone %llx\n", frames_read, pkt->sector);
	pd->stats.pkt_started++;
	pd->stats.secs_rg += frames_read * (CD_FRAMESIZE >> 9);
}

/*
 * Find a packet matching zone, or the least recently used packet if
 * there is no match.
 */
static struct packet_data *pkt_get_packet_data(struct pktcdvd_device *pd, int zone)
{
	struct packet_data *pkt;

	list_for_each_entry(pkt, &pd->cdrw.pkt_free_list, list) {
		if (pkt->sector == zone || pkt->list.next == &pd->cdrw.pkt_free_list) {
			list_del_init(&pkt->list);
			if (pkt->sector != zone)
				pkt->cache_valid = 0;
			return pkt;
		}
	}
	BUG();
	return NULL;
}

static void pkt_put_packet_data(struct pktcdvd_device *pd, struct packet_data *pkt)
{
	if (pkt->cache_valid) {
		list_add(&pkt->list, &pd->cdrw.pkt_free_list);
	} else {
		list_add_tail(&pkt->list, &pd->cdrw.pkt_free_list);
	}
}

static inline void pkt_set_state(struct device *ddev, struct packet_data *pkt,
				 enum packet_data_state state)
{
	static const char *state_name[] = {
		"IDLE", "WAITING", "READ_WAIT", "WRITE_WAIT", "RECOVERY", "FINISHED"
	};
	enum packet_data_state old_state = pkt->state;

	dev_dbg(ddev, "pkt %2d : s=%6llx %s -> %s\n",
		pkt->id, pkt->sector, state_name[old_state], state_name[state]);

	pkt->state = state;
}

/*
 * Scan the work queue to see if we can start a new packet.
 * returns non-zero if any work was done.
 */
static int pkt_handle_queue(struct pktcdvd_device *pd)
{
	struct device *ddev = disk_to_dev(pd->disk);
	struct packet_data *pkt, *p;
	struct bio *bio = NULL;
	sector_t zone = 0; /* Suppress gcc warning */
	struct pkt_rb_node *node, *first_node;
	struct rb_node *n;

	atomic_set(&pd->scan_queue, 0);

	if (list_empty(&pd->cdrw.pkt_free_list)) {
		dev_dbg(ddev, "no pkt\n");
		return 0;
	}

	/*
	 * Try to find a zone we are not already working on.
	 */
	spin_lock(&pd->lock);
	first_node = pkt_rbtree_find(pd, pd->current_sector);
	if (!first_node) {
		n = rb_first(&pd->bio_queue);
		if (n)
			first_node = rb_entry(n, struct pkt_rb_node, rb_node);
	}
	node = first_node;
	while (node) {
		bio = node->bio;
		zone = get_zone(bio->bi_iter.bi_sector, pd);
		list_for_each_entry(p, &pd->cdrw.pkt_active_list, list) {
			if (p->sector == zone) {
				bio = NULL;
				goto try_next_bio;
			}
		}
		break;
try_next_bio:
		node = pkt_rbtree_next(node);
		if (!node) {
			n = rb_first(&pd->bio_queue);
			if (n)
				node = rb_entry(n, struct pkt_rb_node, rb_node);
		}
		if (node == first_node)
			node = NULL;
	}
	spin_unlock(&pd->lock);
	if (!bio) {
		dev_dbg(ddev, "no bio\n");
		return 0;
	}

	pkt = pkt_get_packet_data(pd, zone);

	pd->current_sector = zone + pd->settings.size;
	pkt->sector = zone;
	BUG_ON(pkt->frames != pd->settings.size >> 2);
	pkt->write_size = 0;

	/*
	 * Scan work queue for bios in the same zone and link them
	 * to this packet.
	 */
	spin_lock(&pd->lock);
	dev_dbg(ddev, "looking for zone %llx\n", zone);
	while ((node = pkt_rbtree_find(pd, zone)) != NULL) {
		sector_t tmp = get_zone(node->bio->bi_iter.bi_sector, pd);

		bio = node->bio;
		dev_dbg(ddev, "found zone=%llx\n", tmp);
		if (tmp != zone)
			break;
		pkt_rbtree_erase(pd, node);
		spin_lock(&pkt->lock);
		bio_list_add(&pkt->orig_bios, bio);
		pkt->write_size += bio->bi_iter.bi_size / CD_FRAMESIZE;
		spin_unlock(&pkt->lock);
	}
	/* check write congestion marks, and if bio_queue_size is
	 * below, wake up any waiters
	 */
	if (pd->congested &&
	    pd->bio_queue_size <= pd->write_congestion_off) {
		pd->congested = false;
		wake_up_var(&pd->congested);
	}
	spin_unlock(&pd->lock);

	pkt->sleep_time = max(PACKET_WAIT_TIME, 1);
	pkt_set_state(ddev, pkt, PACKET_WAITING_STATE);
	atomic_set(&pkt->run_sm, 1);

	spin_lock(&pd->cdrw.active_list_lock);
	list_add(&pkt->list, &pd->cdrw.pkt_active_list);
	spin_unlock(&pd->cdrw.active_list_lock);

	return 1;
}

/**
 * bio_list_copy_data - copy contents of data buffers from one chain of bios to
 * another
 * @src: source bio list
 * @dst: destination bio list
 *
 * Stops when it reaches the end of either the @src list or @dst list - that is,
 * copies min(src->bi_size, dst->bi_size) bytes (or the equivalent for lists of
 * bios).
 */
static void bio_list_copy_data(struct bio *dst, struct bio *src)
{
	struct bvec_iter src_iter = src->bi_iter;
	struct bvec_iter dst_iter = dst->bi_iter;

	while (1) {
		if (!src_iter.bi_size) {
			src = src->bi_next;
			if (!src)
				break;

			src_iter = src->bi_iter;
		}

		if (!dst_iter.bi_size) {
			dst = dst->bi_next;
			if (!dst)
				break;

			dst_iter = dst->bi_iter;
		}

		bio_copy_data_iter(dst, &dst_iter, src, &src_iter);
	}
}

/*
 * Assemble a bio to write one packet and queue the bio for processing
 * by the underlying block device.
 */
static void pkt_start_write(struct pktcdvd_device *pd, struct packet_data *pkt)
{
	struct device *ddev = disk_to_dev(pd->disk);
	int f;

	bio_init(pkt->w_bio, file_bdev(pd->bdev_file), pkt->w_bio->bi_inline_vecs,
		 pkt->frames, REQ_OP_WRITE);
	pkt->w_bio->bi_iter.bi_sector = pkt->sector;
	pkt->w_bio->bi_end_io = pkt_end_io_packet_write;
	pkt->w_bio->bi_private = pkt;

	/* XXX: locking? */
	for (f = 0; f < pkt->frames; f++) {
		struct page *page = pkt->pages[(f * CD_FRAMESIZE) / PAGE_SIZE];
		unsigned offset = (f * CD_FRAMESIZE) % PAGE_SIZE;

		if (!bio_add_page(pkt->w_bio, page, CD_FRAMESIZE, offset))
			BUG();
	}
	dev_dbg(ddev, "vcnt=%d\n", pkt->w_bio->bi_vcnt);

	/*
	 * Fill-in bvec with data from orig_bios.
	 */
	spin_lock(&pkt->lock);
	bio_list_copy_data(pkt->w_bio, pkt->orig_bios.head);

	pkt_set_state(ddev, pkt, PACKET_WRITE_WAIT_STATE);
	spin_unlock(&pkt->lock);

	dev_dbg(ddev, "Writing %d frames for zone %llx\n", pkt->write_size, pkt->sector);

	if (test_bit(PACKET_MERGE_SEGS, &pd->flags) || (pkt->write_size < pkt->frames))
		pkt->cache_valid = 1;
	else
		pkt->cache_valid = 0;

	/* Start the write request */
	atomic_set(&pkt->io_wait, 1);
	pkt_queue_bio(pd, pkt->w_bio);
}

static void pkt_finish_packet(struct packet_data *pkt, blk_status_t status)
{
	struct bio *bio;

	if (status)
		pkt->cache_valid = 0;

	/* Finish all bios corresponding to this packet */
	while ((bio = bio_list_pop(&pkt->orig_bios))) {
		bio->bi_status = status;
		bio_endio(bio);
	}
}

static void pkt_run_state_machine(struct pktcdvd_device *pd, struct packet_data *pkt)
{
	struct device *ddev = disk_to_dev(pd->disk);

	dev_dbg(ddev, "pkt %d\n", pkt->id);

	for (;;) {
		switch (pkt->state) {
		case PACKET_WAITING_STATE:
			if ((pkt->write_size < pkt->frames) && (pkt->sleep_time > 0))
				return;

			pkt->sleep_time = 0;
			pkt_gather_data(pd, pkt);
			pkt_set_state(ddev, pkt, PACKET_READ_WAIT_STATE);
			break;

		case PACKET_READ_WAIT_STATE:
			if (atomic_read(&pkt->io_wait) > 0)
				return;

			if (atomic_read(&pkt->io_errors) > 0) {
				pkt_set_state(ddev, pkt, PACKET_RECOVERY_STATE);
			} else {
				pkt_start_write(pd, pkt);
			}
			break;

		case PACKET_WRITE_WAIT_STATE:
			if (atomic_read(&pkt->io_wait) > 0)
				return;

			if (!pkt->w_bio->bi_status) {
				pkt_set_state(ddev, pkt, PACKET_FINISHED_STATE);
			} else {
				pkt_set_state(ddev, pkt, PACKET_RECOVERY_STATE);
			}
			break;

		case PACKET_RECOVERY_STATE:
			dev_dbg(ddev, "No recovery possible\n");
			pkt_set_state(ddev, pkt, PACKET_FINISHED_STATE);
			break;

		case PACKET_FINISHED_STATE:
			pkt_finish_packet(pkt, pkt->w_bio->bi_status);
			return;

		default:
			BUG();
			break;
		}
	}
}

static void pkt_handle_packets(struct pktcdvd_device *pd)
{
	struct device *ddev = disk_to_dev(pd->disk);
	struct packet_data *pkt, *next;

	/*
	 * Run state machine for active packets
	 */
	list_for_each_entry(pkt, &pd->cdrw.pkt_active_list, list) {
		if (atomic_read(&pkt->run_sm) > 0) {
			atomic_set(&pkt->run_sm, 0);
			pkt_run_state_machine(pd, pkt);
		}
	}

	/*
	 * Move no longer active packets to the free list
	 */
	spin_lock(&pd->cdrw.active_list_lock);
	list_for_each_entry_safe(pkt, next, &pd->cdrw.pkt_active_list, list) {
		if (pkt->state == PACKET_FINISHED_STATE) {
			list_del(&pkt->list);
			pkt_put_packet_data(pd, pkt);
			pkt_set_state(ddev, pkt, PACKET_IDLE_STATE);
			atomic_set(&pd->scan_queue, 1);
		}
	}
	spin_unlock(&pd->cdrw.active_list_lock);
}

/*
 * kcdrwd is woken up when writes have been queued for one of our
 * registered devices
 */
static int kcdrwd(void *foobar)
{
	struct pktcdvd_device *pd = foobar;
	struct device *ddev = disk_to_dev(pd->disk);
	struct packet_data *pkt;
	int states[PACKET_NUM_STATES];
	long min_sleep_time, residue;

	set_user_nice(current, MIN_NICE);
	set_freezable();

	for (;;) {
		DECLARE_WAITQUEUE(wait, current);

		/*
		 * Wait until there is something to do
		 */
		add_wait_queue(&pd->wqueue, &wait);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);

			/* Check if we need to run pkt_handle_queue */
			if (atomic_read(&pd->scan_queue) > 0)
				goto work_to_do;

			/* Check if we need to run the state machine for some packet */
			list_for_each_entry(pkt, &pd->cdrw.pkt_active_list, list) {
				if (atomic_read(&pkt->run_sm) > 0)
					goto work_to_do;
			}

			/* Check if we need to process the iosched queues */
			if (atomic_read(&pd->iosched.attention) != 0)
				goto work_to_do;

			/* Otherwise, go to sleep */
			pkt_count_states(pd, states);
			dev_dbg(ddev, "i:%d ow:%d rw:%d ww:%d rec:%d fin:%d\n",
				states[0], states[1], states[2], states[3], states[4], states[5]);

			min_sleep_time = MAX_SCHEDULE_TIMEOUT;
			list_for_each_entry(pkt, &pd->cdrw.pkt_active_list, list) {
				if (pkt->sleep_time && pkt->sleep_time < min_sleep_time)
					min_sleep_time = pkt->sleep_time;
			}

			dev_dbg(ddev, "sleeping\n");
			residue = schedule_timeout(min_sleep_time);
			dev_dbg(ddev, "wake up\n");

			/* make swsusp happy with our thread */
			try_to_freeze();

			list_for_each_entry(pkt, &pd->cdrw.pkt_active_list, list) {
				if (!pkt->sleep_time)
					continue;
				pkt->sleep_time -= min_sleep_time - residue;
				if (pkt->sleep_time <= 0) {
					pkt->sleep_time = 0;
					atomic_inc(&pkt->run_sm);
				}
			}

			if (kthread_should_stop())
				break;
		}
work_to_do:
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&pd->wqueue, &wait);

		if (kthread_should_stop())
			break;

		/*
		 * if pkt_handle_queue returns true, we can queue
		 * another request.
		 */
		while (pkt_handle_queue(pd))
			;

		/*
		 * Handle packet state machine
		 */
		pkt_handle_packets(pd);

		/*
		 * Handle iosched queues
		 */
		pkt_iosched_process_queue(pd);
	}

	return 0;
}

static void pkt_print_settings(struct pktcdvd_device *pd)
{
	dev_info(disk_to_dev(pd->disk), "%s packets, %u blocks, Mode-%c disc\n",
		 pd->settings.fp ? "Fixed" : "Variable",
		 pd->settings.size >> 2,
		 pd->settings.block_mode == 8 ? '1' : '2');
}

static int pkt_mode_sense(struct pktcdvd_device *pd, struct packet_command *cgc, int page_code, int page_control)
{
	memset(cgc->cmd, 0, sizeof(cgc->cmd));

	cgc->cmd[0] = GPCMD_MODE_SENSE_10;
	cgc->cmd[2] = page_code | (page_control << 6);
	put_unaligned_be16(cgc->buflen, &cgc->cmd[7]);
	cgc->data_direction = CGC_DATA_READ;
	return pkt_generic_packet(pd, cgc);
}

static int pkt_mode_select(struct pktcdvd_device *pd, struct packet_command *cgc)
{
	memset(cgc->cmd, 0, sizeof(cgc->cmd));
	memset(cgc->buffer, 0, 2);
	cgc->cmd[0] = GPCMD_MODE_SELECT_10;
	cgc->cmd[1] = 0x10;		/* PF */
	put_unaligned_be16(cgc->buflen, &cgc->cmd[7]);
	cgc->data_direction = CGC_DATA_WRITE;
	return pkt_generic_packet(pd, cgc);
}

static int pkt_get_disc_info(struct pktcdvd_device *pd, disc_information *di)
{
	struct packet_command cgc;
	int ret;

	/* set up command and get the disc info */
	init_cdrom_command(&cgc, di, sizeof(*di), CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_DISC_INFO;
	cgc.cmd[8] = cgc.buflen = 2;
	cgc.quiet = 1;

	ret = pkt_generic_packet(pd, &cgc);
	if (ret)
		return ret;

	/* not all drives have the same disc_info length, so requeue
	 * packet with the length the drive tells us it can supply
	 */
	cgc.buflen = be16_to_cpu(di->disc_information_length) +
		     sizeof(di->disc_information_length);

	if (cgc.buflen > sizeof(disc_information))
		cgc.buflen = sizeof(disc_information);

	cgc.cmd[8] = cgc.buflen;
	return pkt_generic_packet(pd, &cgc);
}

static int pkt_get_track_info(struct pktcdvd_device *pd, __u16 track, __u8 type, track_information *ti)
{
	struct packet_command cgc;
	int ret;

	init_cdrom_command(&cgc, ti, 8, CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_READ_TRACK_RZONE_INFO;
	cgc.cmd[1] = type & 3;
	put_unaligned_be16(track, &cgc.cmd[4]);
	cgc.cmd[8] = 8;
	cgc.quiet = 1;

	ret = pkt_generic_packet(pd, &cgc);
	if (ret)
		return ret;

	cgc.buflen = be16_to_cpu(ti->track_information_length) +
		     sizeof(ti->track_information_length);

	if (cgc.buflen > sizeof(track_information))
		cgc.buflen = sizeof(track_information);

	cgc.cmd[8] = cgc.buflen;
	return pkt_generic_packet(pd, &cgc);
}

static noinline_for_stack int pkt_get_last_written(struct pktcdvd_device *pd,
						long *last_written)
{
	disc_information di;
	track_information ti;
	__u32 last_track;
	int ret;

	ret = pkt_get_disc_info(pd, &di);
	if (ret)
		return ret;

	last_track = (di.last_track_msb << 8) | di.last_track_lsb;
	ret = pkt_get_track_info(pd, last_track, 1, &ti);
	if (ret)
		return ret;

	/* if this track is blank, try the previous. */
	if (ti.blank) {
		last_track--;
		ret = pkt_get_track_info(pd, last_track, 1, &ti);
		if (ret)
			return ret;
	}

	/* if last recorded field is valid, return it. */
	if (ti.lra_v) {
		*last_written = be32_to_cpu(ti.last_rec_address);
	} else {
		/* make it up instead */
		*last_written = be32_to_cpu(ti.track_start) +
				be32_to_cpu(ti.track_size);
		if (ti.free_blocks)
			*last_written -= (be32_to_cpu(ti.free_blocks) + 7);
	}
	return 0;
}

/*
 * write mode select package based on pd->settings
 */
static noinline_for_stack int pkt_set_write_settings(struct pktcdvd_device *pd)
{
	struct device *ddev = disk_to_dev(pd->disk);
	struct packet_command cgc;
	struct scsi_sense_hdr sshdr;
	write_param_page *wp;
	char buffer[128];
	int ret, size;

	/* doesn't apply to DVD+RW or DVD-RAM */
	if ((pd->mmc3_profile == 0x1a) || (pd->mmc3_profile == 0x12))
		return 0;

	memset(buffer, 0, sizeof(buffer));
	init_cdrom_command(&cgc, buffer, sizeof(*wp), CGC_DATA_READ);
	cgc.sshdr = &sshdr;
	ret = pkt_mode_sense(pd, &cgc, GPMODE_WRITE_PARMS_PAGE, 0);
	if (ret) {
		pkt_dump_sense(pd, &cgc);
		return ret;
	}

	size = 2 + get_unaligned_be16(&buffer[0]);
	pd->mode_offset = get_unaligned_be16(&buffer[6]);
	if (size > sizeof(buffer))
		size = sizeof(buffer);

	/*
	 * now get it all
	 */
	init_cdrom_command(&cgc, buffer, size, CGC_DATA_READ);
	cgc.sshdr = &sshdr;
	ret = pkt_mode_sense(pd, &cgc, GPMODE_WRITE_PARMS_PAGE, 0);
	if (ret) {
		pkt_dump_sense(pd, &cgc);
		return ret;
	}

	/*
	 * write page is offset header + block descriptor length
	 */
	wp = (write_param_page *) &buffer[sizeof(struct mode_page_header) + pd->mode_offset];

	wp->fp = pd->settings.fp;
	wp->track_mode = pd->settings.track_mode;
	wp->write_type = pd->settings.write_type;
	wp->data_block_type = pd->settings.block_mode;

	wp->multi_session = 0;

#ifdef PACKET_USE_LS
	wp->link_size = 7;
	wp->ls_v = 1;
#endif

	if (wp->data_block_type == PACKET_BLOCK_MODE1) {
		wp->session_format = 0;
		wp->subhdr2 = 0x20;
	} else if (wp->data_block_type == PACKET_BLOCK_MODE2) {
		wp->session_format = 0x20;
		wp->subhdr2 = 8;
#if 0
		wp->mcn[0] = 0x80;
		memcpy(&wp->mcn[1], PACKET_MCN, sizeof(wp->mcn) - 1);
#endif
	} else {
		/*
		 * paranoia
		 */
		dev_err(ddev, "write mode wrong %d\n", wp->data_block_type);
		return 1;
	}
	wp->packet_size = cpu_to_be32(pd->settings.size >> 2);

	cgc.buflen = cgc.cmd[8] = size;
	ret = pkt_mode_select(pd, &cgc);
	if (ret) {
		pkt_dump_sense(pd, &cgc);
		return ret;
	}

	pkt_print_settings(pd);
	return 0;
}

/*
 * 1 -- we can write to this track, 0 -- we can't
 */
static int pkt_writable_track(struct pktcdvd_device *pd, track_information *ti)
{
	struct device *ddev = disk_to_dev(pd->disk);

	switch (pd->mmc3_profile) {
		case 0x1a: /* DVD+RW */
		case 0x12: /* DVD-RAM */
			/* The track is always writable on DVD+RW/DVD-RAM */
			return 1;
		default:
			break;
	}

	if (!ti->packet || !ti->fp)
		return 0;

	/*
	 * "good" settings as per Mt Fuji.
	 */
	if (ti->rt == 0 && ti->blank == 0)
		return 1;

	if (ti->rt == 0 && ti->blank == 1)
		return 1;

	if (ti->rt == 1 && ti->blank == 0)
		return 1;

	dev_err(ddev, "bad state %d-%d-%d\n", ti->rt, ti->blank, ti->packet);
	return 0;
}

/*
 * 1 -- we can write to this disc, 0 -- we can't
 */
static int pkt_writable_disc(struct pktcdvd_device *pd, disc_information *di)
{
	struct device *ddev = disk_to_dev(pd->disk);

	switch (pd->mmc3_profile) {
		case 0x0a: /* CD-RW */
		case 0xffff: /* MMC3 not supported */
			break;
		case 0x1a: /* DVD+RW */
		case 0x13: /* DVD-RW */
		case 0x12: /* DVD-RAM */
			return 1;
		default:
			dev_dbg(ddev, "Wrong disc profile (%x)\n", pd->mmc3_profile);
			return 0;
	}

	/*
	 * for disc type 0xff we should probably reserve a new track.
	 * but i'm not sure, should we leave this to user apps? probably.
	 */
	if (di->disc_type == 0xff) {
		dev_notice(ddev, "unknown disc - no track?\n");
		return 0;
	}

	if (di->disc_type != 0x20 && di->disc_type != 0) {
		dev_err(ddev, "wrong disc type (%x)\n", di->disc_type);
		return 0;
	}

	if (di->erasable == 0) {
		dev_err(ddev, "disc not erasable\n");
		return 0;
	}

	if (di->border_status == PACKET_SESSION_RESERVED) {
		dev_err(ddev, "can't write to last track (reserved)\n");
		return 0;
	}

	return 1;
}

static noinline_for_stack int pkt_probe_settings(struct pktcdvd_device *pd)
{
	struct device *ddev = disk_to_dev(pd->disk);
	struct packet_command cgc;
	unsigned char buf[12];
	disc_information di;
	track_information ti;
	int ret, track;

	init_cdrom_command(&cgc, buf, sizeof(buf), CGC_DATA_READ);
	cgc.cmd[0] = GPCMD_GET_CONFIGURATION;
	cgc.cmd[8] = 8;
	ret = pkt_generic_packet(pd, &cgc);
	pd->mmc3_profile = ret ? 0xffff : get_unaligned_be16(&buf[6]);

	memset(&di, 0, sizeof(disc_information));
	memset(&ti, 0, sizeof(track_information));

	ret = pkt_get_disc_info(pd, &di);
	if (ret) {
		dev_err(ddev, "failed get_disc\n");
		return ret;
	}

	if (!pkt_writable_disc(pd, &di))
		return -EROFS;

	pd->type = di.erasable ? PACKET_CDRW : PACKET_CDR;

	track = 1; /* (di.last_track_msb << 8) | di.last_track_lsb; */
	ret = pkt_get_track_info(pd, track, 1, &ti);
	if (ret) {
		dev_err(ddev, "failed get_track\n");
		return ret;
	}

	if (!pkt_writable_track(pd, &ti)) {
		dev_err(ddev, "can't write to this track\n");
		return -EROFS;
	}

	/*
	 * we keep packet size in 512 byte units, makes it easier to
	 * deal with request calculations.
	 */
	pd->settings.size = be32_to_cpu(ti.fixed_packet_size) << 2;
	if (pd->settings.size == 0) {
		dev_notice(ddev, "detected zero packet size!\n");
		return -ENXIO;
	}
	if (pd->settings.size > PACKET_MAX_SECTORS) {
		dev_err(ddev, "packet size is too big\n");
		return -EROFS;
	}
	pd->settings.fp = ti.fp;
	pd->offset = (be32_to_cpu(ti.track_start) << 2) & (pd->settings.size - 1);

	if (ti.nwa_v) {
		pd->nwa = be32_to_cpu(ti.next_writable);
		set_bit(PACKET_NWA_VALID, &pd->flags);
	}

	/*
	 * in theory we could use lra on -RW media as well and just zero
	 * blocks that haven't been written yet, but in practice that
	 * is just a no-go. we'll use that for -R, naturally.
	 */
	if (ti.lra_v) {
		pd->lra = be32_to_cpu(ti.last_rec_address);
		set_bit(PACKET_LRA_VALID, &pd->flags);
	} else {
		pd->lra = 0xffffffff;
		set_bit(PACKET_LRA_VALID, &pd->flags);
	}

	/*
	 * fine for now
	 */
	pd->settings.link_loss = 7;
	pd->settings.write_type = 0;	/* packet */
	pd->settings.track_mode = ti.track_mode;

	/*
	 * mode1 or mode2 disc
	 */
	switch (ti.data_mode) {
		case PACKET_MODE1:
			pd->settings.block_mode = PACKET_BLOCK_MODE1;
			break;
		case PACKET_MODE2:
			pd->settings.block_mode = PACKET_BLOCK_MODE2;
			break;
		default:
			dev_err(ddev, "unknown data mode\n");
			return -EROFS;
	}
	return 0;
}

/*
 * enable/disable write caching on drive
 */
static noinline_for_stack int pkt_write_caching(struct pktcdvd_device *pd)
{
	struct device *ddev = disk_to_dev(pd->disk);
	struct packet_command cgc;
	struct scsi_sense_hdr sshdr;
	unsigned char buf[64];
	bool set = IS_ENABLED(CONFIG_CDROM_PKTCDVD_WCACHE);
	int ret;

	init_cdrom_command(&cgc, buf, sizeof(buf), CGC_DATA_READ);
	cgc.sshdr = &sshdr;
	cgc.buflen = pd->mode_offset + 12;

	/*
	 * caching mode page might not be there, so quiet this command
	 */
	cgc.quiet = 1;

	ret = pkt_mode_sense(pd, &cgc, GPMODE_WCACHING_PAGE, 0);
	if (ret)
		return ret;

	/*
	 * use drive write caching -- we need deferred error handling to be
	 * able to successfully recover with this option (drive will return good
	 * status as soon as the cdb is validated).
	 */
	buf[pd->mode_offset + 10] |= (set << 2);

	cgc.buflen = cgc.cmd[8] = 2 + get_unaligned_be16(&buf[0]);
	ret = pkt_mode_select(pd, &cgc);
	if (ret) {
		dev_err(ddev, "write caching control failed\n");
		pkt_dump_sense(pd, &cgc);
	} else if (!ret && set)
		dev_notice(ddev, "enabled write caching\n");
	return ret;
}

static int pkt_lock_door(struct pktcdvd_device *pd, int lockflag)
{
	struct packet_command cgc;

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL;
	cgc.cmd[4] = lockflag ? 1 : 0;
	return pkt_generic_packet(pd, &cgc);
}

/*
 * Returns drive maximum write speed
 */
static noinline_for_stack int pkt_get_max_speed(struct pktcdvd_device *pd,
						unsigned *write_speed)
{
	struct packet_command cgc;
	struct scsi_sense_hdr sshdr;
	unsigned char buf[256+18];
	unsigned char *cap_buf;
	int ret, offset;

	cap_buf = &buf[sizeof(struct mode_page_header) + pd->mode_offset];
	init_cdrom_command(&cgc, buf, sizeof(buf), CGC_DATA_UNKNOWN);
	cgc.sshdr = &sshdr;

	ret = pkt_mode_sense(pd, &cgc, GPMODE_CAPABILITIES_PAGE, 0);
	if (ret) {
		cgc.buflen = pd->mode_offset + cap_buf[1] + 2 +
			     sizeof(struct mode_page_header);
		ret = pkt_mode_sense(pd, &cgc, GPMODE_CAPABILITIES_PAGE, 0);
		if (ret) {
			pkt_dump_sense(pd, &cgc);
			return ret;
		}
	}

	offset = 20;			    /* Obsoleted field, used by older drives */
	if (cap_buf[1] >= 28)
		offset = 28;		    /* Current write speed selected */
	if (cap_buf[1] >= 30) {
		/* If the drive reports at least one "Logical Unit Write
		 * Speed Performance Descriptor Block", use the information
		 * in the first block. (contains the highest speed)
		 */
		int num_spdb = get_unaligned_be16(&cap_buf[30]);
		if (num_spdb > 0)
			offset = 34;
	}

	*write_speed = get_unaligned_be16(&cap_buf[offset]);
	return 0;
}

/* These tables from cdrecord - I don't have orange book */
/* standard speed CD-RW (1-4x) */
static char clv_to_speed[16] = {
	/* 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 */
	   0, 2, 4, 6, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
/* high speed CD-RW (-10x) */
static char hs_clv_to_speed[16] = {
	/* 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 */
	   0, 2, 4, 6, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
/* ultra high speed CD-RW */
static char us_clv_to_speed[16] = {
	/* 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 */
	   0, 2, 4, 8, 0, 0,16, 0,24,32,40,48, 0, 0, 0, 0
};

/*
 * reads the maximum media speed from ATIP
 */
static noinline_for_stack int pkt_media_speed(struct pktcdvd_device *pd,
						unsigned *speed)
{
	struct device *ddev = disk_to_dev(pd->disk);
	struct packet_command cgc;
	struct scsi_sense_hdr sshdr;
	unsigned char buf[64];
	unsigned int size, st, sp;
	int ret;

	init_cdrom_command(&cgc, buf, 2, CGC_DATA_READ);
	cgc.sshdr = &sshdr;
	cgc.cmd[0] = GPCMD_READ_TOC_PMA_ATIP;
	cgc.cmd[1] = 2;
	cgc.cmd[2] = 4; /* READ ATIP */
	cgc.cmd[8] = 2;
	ret = pkt_generic_packet(pd, &cgc);
	if (ret) {
		pkt_dump_sense(pd, &cgc);
		return ret;
	}
	size = 2 + get_unaligned_be16(&buf[0]);
	if (size > sizeof(buf))
		size = sizeof(buf);

	init_cdrom_command(&cgc, buf, size, CGC_DATA_READ);
	cgc.sshdr = &sshdr;
	cgc.cmd[0] = GPCMD_READ_TOC_PMA_ATIP;
	cgc.cmd[1] = 2;
	cgc.cmd[2] = 4;
	cgc.cmd[8] = size;
	ret = pkt_generic_packet(pd, &cgc);
	if (ret) {
		pkt_dump_sense(pd, &cgc);
		return ret;
	}

	if (!(buf[6] & 0x40)) {
		dev_notice(ddev, "disc type is not CD-RW\n");
		return 1;
	}
	if (!(buf[6] & 0x4)) {
		dev_notice(ddev, "A1 values on media are not valid, maybe not CDRW?\n");
		return 1;
	}

	st = (buf[6] >> 3) & 0x7; /* disc sub-type */

	sp = buf[16] & 0xf; /* max speed from ATIP A1 field */

	/* Info from cdrecord */
	switch (st) {
		case 0: /* standard speed */
			*speed = clv_to_speed[sp];
			break;
		case 1: /* high speed */
			*speed = hs_clv_to_speed[sp];
			break;
		case 2: /* ultra high speed */
			*speed = us_clv_to_speed[sp];
			break;
		default:
			dev_notice(ddev, "unknown disc sub-type %d\n", st);
			return 1;
	}
	if (*speed) {
		dev_info(ddev, "maximum media speed: %d\n", *speed);
		return 0;
	} else {
		dev_notice(ddev, "unknown speed %d for sub-type %d\n", sp, st);
		return 1;
	}
}

static noinline_for_stack int pkt_perform_opc(struct pktcdvd_device *pd)
{
	struct device *ddev = disk_to_dev(pd->disk);
	struct packet_command cgc;
	struct scsi_sense_hdr sshdr;
	int ret;

	dev_dbg(ddev, "Performing OPC\n");

	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.sshdr = &sshdr;
	cgc.timeout = 60*HZ;
	cgc.cmd[0] = GPCMD_SEND_OPC;
	cgc.cmd[1] = 1;
	ret = pkt_generic_packet(pd, &cgc);
	if (ret)
		pkt_dump_sense(pd, &cgc);
	return ret;
}

static int pkt_open_write(struct pktcdvd_device *pd)
{
	struct device *ddev = disk_to_dev(pd->disk);
	int ret;
	unsigned int write_speed, media_write_speed, read_speed;

	ret = pkt_probe_settings(pd);
	if (ret) {
		dev_dbg(ddev, "failed probe\n");
		return ret;
	}

	ret = pkt_set_write_settings(pd);
	if (ret) {
		dev_notice(ddev, "failed saving write settings\n");
		return -EIO;
	}

	pkt_write_caching(pd);

	ret = pkt_get_max_speed(pd, &write_speed);
	if (ret)
		write_speed = 16 * 177;
	switch (pd->mmc3_profile) {
		case 0x13: /* DVD-RW */
		case 0x1a: /* DVD+RW */
		case 0x12: /* DVD-RAM */
			dev_notice(ddev, "write speed %ukB/s\n", write_speed);
			break;
		default:
			ret = pkt_media_speed(pd, &media_write_speed);
			if (ret)
				media_write_speed = 16;
			write_speed = min(write_speed, media_write_speed * 177);
			dev_notice(ddev, "write speed %ux\n", write_speed / 176);
			break;
	}
	read_speed = write_speed;

	ret = pkt_set_speed(pd, write_speed, read_speed);
	if (ret) {
		dev_notice(ddev, "couldn't set write speed\n");
		return -EIO;
	}
	pd->write_speed = write_speed;
	pd->read_speed = read_speed;

	ret = pkt_perform_opc(pd);
	if (ret)
		dev_notice(ddev, "Optimum Power Calibration failed\n");

	return 0;
}

/*
 * called at open time.
 */
static int pkt_open_dev(struct pktcdvd_device *pd, bool write)
{
	struct device *ddev = disk_to_dev(pd->disk);
	int ret;
	long lba;
	struct request_queue *q;
	struct file *bdev_file;

	/*
	 * We need to re-open the cdrom device without O_NONBLOCK to be able
	 * to read/write from/to it. It is already opened in O_NONBLOCK mode
	 * so open should not fail.
	 */
	bdev_file = bdev_file_open_by_dev(file_bdev(pd->bdev_file)->bd_dev,
				       BLK_OPEN_READ, pd, NULL);
	if (IS_ERR(bdev_file)) {
		ret = PTR_ERR(bdev_file);
		goto out;
	}
	pd->f_open_bdev = bdev_file;

	ret = pkt_get_last_written(pd, &lba);
	if (ret) {
		dev_err(ddev, "pkt_get_last_written failed\n");
		goto out_putdev;
	}

	set_capacity(pd->disk, lba << 2);
	set_capacity_and_notify(file_bdev(pd->bdev_file)->bd_disk, lba << 2);

	q = bdev_get_queue(file_bdev(pd->bdev_file));
	if (write) {
		ret = pkt_open_write(pd);
		if (ret)
			goto out_putdev;
		set_bit(PACKET_WRITABLE, &pd->flags);
	} else {
		pkt_set_speed(pd, MAX_SPEED, MAX_SPEED);
		clear_bit(PACKET_WRITABLE, &pd->flags);
	}

	ret = pkt_set_segment_merging(pd, q);
	if (ret)
		goto out_putdev;

	if (write) {
		if (!pkt_grow_pktlist(pd, CONFIG_CDROM_PKTCDVD_BUFFERS)) {
			dev_err(ddev, "not enough memory for buffers\n");
			ret = -ENOMEM;
			goto out_putdev;
		}
		dev_info(ddev, "%lukB available on disc\n", lba << 1);
	}
	set_blocksize(bdev_file, CD_FRAMESIZE);

	return 0;

out_putdev:
	fput(bdev_file);
out:
	return ret;
}

/*
 * called when the device is closed. makes sure that the device flushes
 * the internal cache before we close.
 */
static void pkt_release_dev(struct pktcdvd_device *pd, int flush)
{
	struct device *ddev = disk_to_dev(pd->disk);

	if (flush && pkt_flush_cache(pd))
		dev_notice(ddev, "not flushing cache\n");

	pkt_lock_door(pd, 0);

	pkt_set_speed(pd, MAX_SPEED, MAX_SPEED);
	fput(pd->f_open_bdev);
	pd->f_open_bdev = NULL;

	pkt_shrink_pktlist(pd);
}

static struct pktcdvd_device *pkt_find_dev_from_minor(unsigned int dev_minor)
{
	if (dev_minor >= MAX_WRITERS)
		return NULL;

	dev_minor = array_index_nospec(dev_minor, MAX_WRITERS);
	return pkt_devs[dev_minor];
}

static int pkt_open(struct gendisk *disk, blk_mode_t mode)
{
	struct pktcdvd_device *pd = NULL;
	int ret;

	mutex_lock(&pktcdvd_mutex);
	mutex_lock(&ctl_mutex);
	pd = pkt_find_dev_from_minor(disk->first_minor);
	if (!pd) {
		ret = -ENODEV;
		goto out;
	}
	BUG_ON(pd->refcnt < 0);

	pd->refcnt++;
	if (pd->refcnt > 1) {
		if ((mode & BLK_OPEN_WRITE) &&
		    !test_bit(PACKET_WRITABLE, &pd->flags)) {
			ret = -EBUSY;
			goto out_dec;
		}
	} else {
		ret = pkt_open_dev(pd, mode & BLK_OPEN_WRITE);
		if (ret)
			goto out_dec;
	}
	mutex_unlock(&ctl_mutex);
	mutex_unlock(&pktcdvd_mutex);
	return 0;

out_dec:
	pd->refcnt--;
out:
	mutex_unlock(&ctl_mutex);
	mutex_unlock(&pktcdvd_mutex);
	return ret;
}

static void pkt_release(struct gendisk *disk)
{
	struct pktcdvd_device *pd = disk->private_data;

	mutex_lock(&pktcdvd_mutex);
	mutex_lock(&ctl_mutex);
	pd->refcnt--;
	BUG_ON(pd->refcnt < 0);
	if (pd->refcnt == 0) {
		int flush = test_bit(PACKET_WRITABLE, &pd->flags);
		pkt_release_dev(pd, flush);
	}
	mutex_unlock(&ctl_mutex);
	mutex_unlock(&pktcdvd_mutex);
}


static void pkt_end_io_read_cloned(struct bio *bio)
{
	struct packet_stacked_data *psd = bio->bi_private;
	struct pktcdvd_device *pd = psd->pd;

	psd->bio->bi_status = bio->bi_status;
	bio_put(bio);
	bio_endio(psd->bio);
	mempool_free(psd, &psd_pool);
	pkt_bio_finished(pd);
}

static void pkt_make_request_read(struct pktcdvd_device *pd, struct bio *bio)
{
	struct bio *cloned_bio = bio_alloc_clone(file_bdev(pd->bdev_file), bio,
		GFP_NOIO, &pkt_bio_set);
	struct packet_stacked_data *psd = mempool_alloc(&psd_pool, GFP_NOIO);

	psd->pd = pd;
	psd->bio = bio;
	cloned_bio->bi_private = psd;
	cloned_bio->bi_end_io = pkt_end_io_read_cloned;
	pd->stats.secs_r += bio_sectors(bio);
	pkt_queue_bio(pd, cloned_bio);
}

static void pkt_make_request_write(struct bio *bio)
{
	struct pktcdvd_device *pd = bio->bi_bdev->bd_disk->private_data;
	sector_t zone;
	struct packet_data *pkt;
	int was_empty, blocked_bio;
	struct pkt_rb_node *node;

	zone = get_zone(bio->bi_iter.bi_sector, pd);

	/*
	 * If we find a matching packet in state WAITING or READ_WAIT, we can
	 * just append this bio to that packet.
	 */
	spin_lock(&pd->cdrw.active_list_lock);
	blocked_bio = 0;
	list_for_each_entry(pkt, &pd->cdrw.pkt_active_list, list) {
		if (pkt->sector == zone) {
			spin_lock(&pkt->lock);
			if ((pkt->state == PACKET_WAITING_STATE) ||
			    (pkt->state == PACKET_READ_WAIT_STATE)) {
				bio_list_add(&pkt->orig_bios, bio);
				pkt->write_size +=
					bio->bi_iter.bi_size / CD_FRAMESIZE;
				if ((pkt->write_size >= pkt->frames) &&
				    (pkt->state == PACKET_WAITING_STATE)) {
					atomic_inc(&pkt->run_sm);
					wake_up(&pd->wqueue);
				}
				spin_unlock(&pkt->lock);
				spin_unlock(&pd->cdrw.active_list_lock);
				return;
			} else {
				blocked_bio = 1;
			}
			spin_unlock(&pkt->lock);
		}
	}
	spin_unlock(&pd->cdrw.active_list_lock);

	/*
	 * Test if there is enough room left in the bio work queue
	 * (queue size >= congestion on mark).
	 * If not, wait till the work queue size is below the congestion off mark.
	 */
	spin_lock(&pd->lock);
	if (pd->write_congestion_on > 0
	    && pd->bio_queue_size >= pd->write_congestion_on) {
		struct wait_bit_queue_entry wqe;

		init_wait_var_entry(&wqe, &pd->congested, 0);
		for (;;) {
			prepare_to_wait_event(__var_waitqueue(&pd->congested),
					      &wqe.wq_entry,
					      TASK_UNINTERRUPTIBLE);
			if (pd->bio_queue_size <= pd->write_congestion_off)
				break;
			pd->congested = true;
			spin_unlock(&pd->lock);
			schedule();
			spin_lock(&pd->lock);
		}
	}
	spin_unlock(&pd->lock);

	/*
	 * No matching packet found. Store the bio in the work queue.
	 */
	node = mempool_alloc(&pd->rb_pool, GFP_NOIO);
	node->bio = bio;
	spin_lock(&pd->lock);
	BUG_ON(pd->bio_queue_size < 0);
	was_empty = (pd->bio_queue_size == 0);
	pkt_rbtree_insert(pd, node);
	spin_unlock(&pd->lock);

	/*
	 * Wake up the worker thread.
	 */
	atomic_set(&pd->scan_queue, 1);
	if (was_empty) {
		/* This wake_up is required for correct operation */
		wake_up(&pd->wqueue);
	} else if (!list_empty(&pd->cdrw.pkt_free_list) && !blocked_bio) {
		/*
		 * This wake up is not required for correct operation,
		 * but improves performance in some cases.
		 */
		wake_up(&pd->wqueue);
	}
}

static void pkt_submit_bio(struct bio *bio)
{
	struct pktcdvd_device *pd = bio->bi_bdev->bd_disk->private_data;
	struct device *ddev = disk_to_dev(pd->disk);
	struct bio *split;

	bio = bio_split_to_limits(bio);
	if (!bio)
		return;

	dev_dbg(ddev, "start = %6llx stop = %6llx\n",
		bio->bi_iter.bi_sector, bio_end_sector(bio));

	/*
	 * Clone READ bios so we can have our own bi_end_io callback.
	 */
	if (bio_data_dir(bio) == READ) {
		pkt_make_request_read(pd, bio);
		return;
	}

	if (!test_bit(PACKET_WRITABLE, &pd->flags)) {
		dev_notice(ddev, "WRITE for ro device (%llu)\n", bio->bi_iter.bi_sector);
		goto end_io;
	}

	if (!bio->bi_iter.bi_size || (bio->bi_iter.bi_size % CD_FRAMESIZE)) {
		dev_err(ddev, "wrong bio size\n");
		goto end_io;
	}

	do {
		sector_t zone = get_zone(bio->bi_iter.bi_sector, pd);
		sector_t last_zone = get_zone(bio_end_sector(bio) - 1, pd);

		if (last_zone != zone) {
			BUG_ON(last_zone != zone + pd->settings.size);

			split = bio_split(bio, last_zone -
					  bio->bi_iter.bi_sector,
					  GFP_NOIO, &pkt_bio_set);
			bio_chain(split, bio);
		} else {
			split = bio;
		}

		pkt_make_request_write(split);
	} while (split != bio);

	return;
end_io:
	bio_io_error(bio);
}

static int pkt_new_dev(struct pktcdvd_device *pd, dev_t dev)
{
	struct device *ddev = disk_to_dev(pd->disk);
	int i;
	struct file *bdev_file;
	struct scsi_device *sdev;

	if (pd->pkt_dev == dev) {
		dev_err(ddev, "recursive setup not allowed\n");
		return -EBUSY;
	}
	for (i = 0; i < MAX_WRITERS; i++) {
		struct pktcdvd_device *pd2 = pkt_devs[i];
		if (!pd2)
			continue;
		if (file_bdev(pd2->bdev_file)->bd_dev == dev) {
			dev_err(ddev, "%pg already setup\n",
				file_bdev(pd2->bdev_file));
			return -EBUSY;
		}
		if (pd2->pkt_dev == dev) {
			dev_err(ddev, "can't chain pktcdvd devices\n");
			return -EBUSY;
		}
	}

	bdev_file = bdev_file_open_by_dev(dev, BLK_OPEN_READ | BLK_OPEN_NDELAY,
				       NULL, NULL);
	if (IS_ERR(bdev_file))
		return PTR_ERR(bdev_file);
	sdev = scsi_device_from_queue(file_bdev(bdev_file)->bd_disk->queue);
	if (!sdev) {
		fput(bdev_file);
		return -EINVAL;
	}
	put_device(&sdev->sdev_gendev);

	/* This is safe, since we have a reference from open(). */
	__module_get(THIS_MODULE);

	pd->bdev_file = bdev_file;

	atomic_set(&pd->cdrw.pending_bios, 0);
	pd->cdrw.thread = kthread_run(kcdrwd, pd, "%s", pd->disk->disk_name);
	if (IS_ERR(pd->cdrw.thread)) {
		dev_err(ddev, "can't start kernel thread\n");
		goto out_mem;
	}

	proc_create_single_data(pd->disk->disk_name, 0, pkt_proc, pkt_seq_show, pd);
	dev_notice(ddev, "writer mapped to %pg\n", file_bdev(bdev_file));
	return 0;

out_mem:
	fput(bdev_file);
	/* This is safe: open() is still holding a reference. */
	module_put(THIS_MODULE);
	return -ENOMEM;
}

static int pkt_ioctl(struct block_device *bdev, blk_mode_t mode,
		unsigned int cmd, unsigned long arg)
{
	struct pktcdvd_device *pd = bdev->bd_disk->private_data;
	struct device *ddev = disk_to_dev(pd->disk);
	int ret;

	dev_dbg(ddev, "cmd %x, dev %d:%d\n", cmd, MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));

	mutex_lock(&pktcdvd_mutex);
	switch (cmd) {
	case CDROMEJECT:
		/*
		 * The door gets locked when the device is opened, so we
		 * have to unlock it or else the eject command fails.
		 */
		if (pd->refcnt == 1)
			pkt_lock_door(pd, 0);
		fallthrough;
	/*
	 * forward selected CDROM ioctls to CD-ROM, for UDF
	 */
	case CDROMMULTISESSION:
	case CDROMREADTOCENTRY:
	case CDROM_LAST_WRITTEN:
	case CDROM_SEND_PACKET:
	case SCSI_IOCTL_SEND_COMMAND:
		if (!bdev->bd_disk->fops->ioctl)
			ret = -ENOTTY;
		else
			ret = bdev->bd_disk->fops->ioctl(bdev, mode, cmd, arg);
		break;
	default:
		dev_dbg(ddev, "Unknown ioctl (%x)\n", cmd);
		ret = -ENOTTY;
	}
	mutex_unlock(&pktcdvd_mutex);

	return ret;
}

static unsigned int pkt_check_events(struct gendisk *disk,
				     unsigned int clearing)
{
	struct pktcdvd_device *pd = disk->private_data;
	struct gendisk *attached_disk;

	if (!pd)
		return 0;
	if (!pd->bdev_file)
		return 0;
	attached_disk = file_bdev(pd->bdev_file)->bd_disk;
	if (!attached_disk || !attached_disk->fops->check_events)
		return 0;
	return attached_disk->fops->check_events(attached_disk, clearing);
}

static char *pkt_devnode(struct gendisk *disk, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "pktcdvd/%s", disk->disk_name);
}

static const struct block_device_operations pktcdvd_ops = {
	.owner =		THIS_MODULE,
	.submit_bio =		pkt_submit_bio,
	.open =			pkt_open,
	.release =		pkt_release,
	.ioctl =		pkt_ioctl,
	.compat_ioctl =		blkdev_compat_ptr_ioctl,
	.check_events =		pkt_check_events,
	.devnode =		pkt_devnode,
};

/*
 * Set up mapping from pktcdvd device to CD-ROM device.
 */
static int pkt_setup_dev(dev_t dev, dev_t* pkt_dev)
{
	struct queue_limits lim = {
		.max_hw_sectors		= PACKET_MAX_SECTORS,
		.logical_block_size	= CD_FRAMESIZE,
		.features		= BLK_FEAT_ROTATIONAL,
	};
	int idx;
	int ret = -ENOMEM;
	struct pktcdvd_device *pd;
	struct gendisk *disk;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	for (idx = 0; idx < MAX_WRITERS; idx++)
		if (!pkt_devs[idx])
			break;
	if (idx == MAX_WRITERS) {
		pr_err("max %d writers supported\n", MAX_WRITERS);
		ret = -EBUSY;
		goto out_mutex;
	}

	pd = kzalloc(sizeof(struct pktcdvd_device), GFP_KERNEL);
	if (!pd)
		goto out_mutex;

	ret = mempool_init_kmalloc_pool(&pd->rb_pool, PKT_RB_POOL_SIZE,
					sizeof(struct pkt_rb_node));
	if (ret)
		goto out_mem;

	INIT_LIST_HEAD(&pd->cdrw.pkt_free_list);
	INIT_LIST_HEAD(&pd->cdrw.pkt_active_list);
	spin_lock_init(&pd->cdrw.active_list_lock);

	spin_lock_init(&pd->lock);
	spin_lock_init(&pd->iosched.lock);
	bio_list_init(&pd->iosched.read_queue);
	bio_list_init(&pd->iosched.write_queue);
	init_waitqueue_head(&pd->wqueue);
	pd->bio_queue = RB_ROOT;

	pd->write_congestion_on  = write_congestion_on;
	pd->write_congestion_off = write_congestion_off;

	disk = blk_alloc_disk(&lim, NUMA_NO_NODE);
	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		goto out_mem;
	}
	pd->disk = disk;
	disk->major = pktdev_major;
	disk->first_minor = idx;
	disk->minors = 1;
	disk->fops = &pktcdvd_ops;
	disk->flags = GENHD_FL_REMOVABLE | GENHD_FL_NO_PART;
	snprintf(disk->disk_name, sizeof(disk->disk_name), DRIVER_NAME"%d", idx);
	disk->private_data = pd;

	pd->pkt_dev = MKDEV(pktdev_major, idx);
	ret = pkt_new_dev(pd, dev);
	if (ret)
		goto out_mem2;

	/* inherit events of the host device */
	disk->events = file_bdev(pd->bdev_file)->bd_disk->events;

	ret = add_disk(disk);
	if (ret)
		goto out_mem2;

	pkt_sysfs_dev_new(pd);
	pkt_debugfs_dev_new(pd);

	pkt_devs[idx] = pd;
	if (pkt_dev)
		*pkt_dev = pd->pkt_dev;

	mutex_unlock(&ctl_mutex);
	return 0;

out_mem2:
	put_disk(disk);
out_mem:
	mempool_exit(&pd->rb_pool);
	kfree(pd);
out_mutex:
	mutex_unlock(&ctl_mutex);
	pr_err("setup of pktcdvd device failed\n");
	return ret;
}

/*
 * Tear down mapping from pktcdvd device to CD-ROM device.
 */
static int pkt_remove_dev(dev_t pkt_dev)
{
	struct pktcdvd_device *pd;
	struct device *ddev;
	int idx;
	int ret = 0;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	for (idx = 0; idx < MAX_WRITERS; idx++) {
		pd = pkt_devs[idx];
		if (pd && (pd->pkt_dev == pkt_dev))
			break;
	}
	if (idx == MAX_WRITERS) {
		pr_debug("dev not setup\n");
		ret = -ENXIO;
		goto out;
	}

	if (pd->refcnt > 0) {
		ret = -EBUSY;
		goto out;
	}

	ddev = disk_to_dev(pd->disk);

	if (!IS_ERR(pd->cdrw.thread))
		kthread_stop(pd->cdrw.thread);

	pkt_devs[idx] = NULL;

	pkt_debugfs_dev_remove(pd);
	pkt_sysfs_dev_remove(pd);

	fput(pd->bdev_file);

	remove_proc_entry(pd->disk->disk_name, pkt_proc);
	dev_notice(ddev, "writer unmapped\n");

	del_gendisk(pd->disk);
	put_disk(pd->disk);

	mempool_exit(&pd->rb_pool);
	kfree(pd);

	/* This is safe: open() is still holding a reference. */
	module_put(THIS_MODULE);

out:
	mutex_unlock(&ctl_mutex);
	return ret;
}

static void pkt_get_status(struct pkt_ctrl_command *ctrl_cmd)
{
	struct pktcdvd_device *pd;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	pd = pkt_find_dev_from_minor(ctrl_cmd->dev_index);
	if (pd) {
		ctrl_cmd->dev = new_encode_dev(file_bdev(pd->bdev_file)->bd_dev);
		ctrl_cmd->pkt_dev = new_encode_dev(pd->pkt_dev);
	} else {
		ctrl_cmd->dev = 0;
		ctrl_cmd->pkt_dev = 0;
	}
	ctrl_cmd->num_devices = MAX_WRITERS;

	mutex_unlock(&ctl_mutex);
}

static long pkt_ctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct pkt_ctrl_command ctrl_cmd;
	int ret = 0;
	dev_t pkt_dev = 0;

	if (cmd != PACKET_CTRL_CMD)
		return -ENOTTY;

	if (copy_from_user(&ctrl_cmd, argp, sizeof(struct pkt_ctrl_command)))
		return -EFAULT;

	switch (ctrl_cmd.command) {
	case PKT_CTRL_CMD_SETUP:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		ret = pkt_setup_dev(new_decode_dev(ctrl_cmd.dev), &pkt_dev);
		ctrl_cmd.pkt_dev = new_encode_dev(pkt_dev);
		break;
	case PKT_CTRL_CMD_TEARDOWN:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		ret = pkt_remove_dev(new_decode_dev(ctrl_cmd.pkt_dev));
		break;
	case PKT_CTRL_CMD_STATUS:
		pkt_get_status(&ctrl_cmd);
		break;
	default:
		return -ENOTTY;
	}

	if (copy_to_user(argp, &ctrl_cmd, sizeof(struct pkt_ctrl_command)))
		return -EFAULT;
	return ret;
}

#ifdef CONFIG_COMPAT
static long pkt_ctl_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return pkt_ctl_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations pkt_ctl_fops = {
	.open		= nonseekable_open,
	.unlocked_ioctl	= pkt_ctl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= pkt_ctl_compat_ioctl,
#endif
	.owner		= THIS_MODULE,
};

static struct miscdevice pkt_misc = {
	.minor 		= MISC_DYNAMIC_MINOR,
	.name  		= DRIVER_NAME,
	.nodename	= "pktcdvd/control",
	.fops  		= &pkt_ctl_fops
};

static int __init pkt_init(void)
{
	int ret;

	mutex_init(&ctl_mutex);

	ret = mempool_init_kmalloc_pool(&psd_pool, PSD_POOL_SIZE,
				    sizeof(struct packet_stacked_data));
	if (ret)
		return ret;
	ret = bioset_init(&pkt_bio_set, BIO_POOL_SIZE, 0, 0);
	if (ret) {
		mempool_exit(&psd_pool);
		return ret;
	}

	ret = register_blkdev(pktdev_major, DRIVER_NAME);
	if (ret < 0) {
		pr_err("unable to register block device\n");
		goto out2;
	}
	if (!pktdev_major)
		pktdev_major = ret;

	ret = pkt_sysfs_init();
	if (ret)
		goto out;

	pkt_debugfs_init();

	ret = misc_register(&pkt_misc);
	if (ret) {
		pr_err("unable to register misc device\n");
		goto out_misc;
	}

	pkt_proc = proc_mkdir("driver/"DRIVER_NAME, NULL);

	return 0;

out_misc:
	pkt_debugfs_cleanup();
	pkt_sysfs_cleanup();
out:
	unregister_blkdev(pktdev_major, DRIVER_NAME);
out2:
	mempool_exit(&psd_pool);
	bioset_exit(&pkt_bio_set);
	return ret;
}

static void __exit pkt_exit(void)
{
	remove_proc_entry("driver/"DRIVER_NAME, NULL);
	misc_deregister(&pkt_misc);

	pkt_debugfs_cleanup();
	pkt_sysfs_cleanup();

	unregister_blkdev(pktdev_major, DRIVER_NAME);
	mempool_exit(&psd_pool);
	bioset_exit(&pkt_bio_set);
}

MODULE_DESCRIPTION("Packet writing layer for CD/DVD drives");
MODULE_AUTHOR("Jens Axboe <axboe@suse.de>");
MODULE_LICENSE("GPL");

module_init(pkt_init);
module_exit(pkt_exit);
