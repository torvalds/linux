/*
 * 2007+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/connector.h>
#include <linux/dst.h>
#include <linux/device.h>
#include <linux/jhash.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/socket.h>

#include <linux/in.h>
#include <linux/in6.h>

#include <net/sock.h>

static int dst_major;

static DEFINE_MUTEX(dst_hash_lock);
static struct list_head *dst_hashtable;
static unsigned int dst_hashtable_size = 128;
module_param(dst_hashtable_size, uint, 0644);

static char dst_name[] = "Dementianting goldfish";

static DEFINE_IDR(dst_index_idr);
static struct cb_id cn_dst_id = { CN_DST_IDX, CN_DST_VAL };

/*
 * DST sysfs tree for device called 'storage':
 *
 * /sys/bus/dst/devices/storage/
 * /sys/bus/dst/devices/storage/type : 192.168.4.80:1025
 * /sys/bus/dst/devices/storage/size : 800
 * /sys/bus/dst/devices/storage/name : storage
 */

static int dst_dev_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static struct bus_type dst_dev_bus_type = {
	.name 		= "dst",
	.match 		= &dst_dev_match,
};

static void dst_node_release(struct device *dev)
{
	struct dst_info *info = container_of(dev, struct dst_info, device);

	kfree(info);
}

static struct device dst_node_dev = {
	.bus 		= &dst_dev_bus_type,
	.release 	= &dst_node_release
};

/*
 * Setting size of the node after it was changed.
 */
static void dst_node_set_size(struct dst_node *n)
{
	struct block_device *bdev;

	set_capacity(n->disk, n->size >> 9);

	bdev = bdget_disk(n->disk, 0);
	if (bdev) {
		mutex_lock(&bdev->bd_inode->i_mutex);
		i_size_write(bdev->bd_inode, n->size);
		mutex_unlock(&bdev->bd_inode->i_mutex);
		bdput(bdev);
	}
}

/*
 * Distributed storage request processing function.
 */
static int dst_request(struct request_queue *q, struct bio *bio)
{
	struct dst_node *n = q->queuedata;
	int err = -EIO;

	if (bio_empty_barrier(bio) && !blk_queue_discard(q)) {
		/*
		 * This is a dirty^Wnice hack, but if we complete this
		 * operation with -EOPNOTSUPP like intended, XFS
		 * will stuck and freeze the machine. This may be
		 * not particulary XFS problem though, but it is the
		 * only FS which sends empty barrier at umount time
		 * I worked with.
		 *
		 * Empty barriers are not allowed anyway, see 51fd77bd9f512
		 * for example, although later it was changed to
		 * bio_rw_flagged(bio, BIO_RW_DISCARD) only, which does not
		 * work in this case.
		 */
		//err = -EOPNOTSUPP;
		err = 0;
		goto end_io;
	}

	bio_get(bio);

	return dst_process_bio(n, bio);

end_io:
	bio_endio(bio, err);
	return err;
}

/*
 * Open/close callbacks for appropriate block device.
 */
static int dst_bdev_open(struct block_device *bdev, fmode_t mode)
{
	struct dst_node *n = bdev->bd_disk->private_data;

	dst_node_get(n);
	return 0;
}

static int dst_bdev_release(struct gendisk *disk, fmode_t mode)
{
	struct dst_node *n = disk->private_data;

	dst_node_put(n);
	return 0;
}

static struct block_device_operations dst_blk_ops = {
	.open		= dst_bdev_open,
	.release	= dst_bdev_release,
	.owner		= THIS_MODULE,
};

/*
 * Block layer binding - disk is created when array is fully configured
 * by userspace request.
 */
static int dst_node_create_disk(struct dst_node *n)
{
	int err = -ENOMEM;
	u32 index = 0;

	n->queue = blk_init_queue(NULL, NULL);
	if (!n->queue)
		goto err_out_exit;

	n->queue->queuedata = n;
	blk_queue_make_request(n->queue, dst_request);
	blk_queue_max_phys_segments(n->queue, n->max_pages);
	blk_queue_max_hw_segments(n->queue, n->max_pages);

	err = -ENOMEM;
	n->disk = alloc_disk(1);
	if (!n->disk)
		goto err_out_free_queue;

	if (!(n->state->permissions & DST_PERM_WRITE)) {
		printk(KERN_INFO "DST node %s attached read-only.\n", n->name);
		set_disk_ro(n->disk, 1);
	}

	if (!idr_pre_get(&dst_index_idr, GFP_KERNEL))
		goto err_out_put;

	mutex_lock(&dst_hash_lock);
	err = idr_get_new(&dst_index_idr, NULL, &index);
	mutex_unlock(&dst_hash_lock);
	if (err)
		goto err_out_put;

	n->disk->major = dst_major;
	n->disk->first_minor = index;
	n->disk->fops = &dst_blk_ops;
	n->disk->queue = n->queue;
	n->disk->private_data = n;
	snprintf(n->disk->disk_name, sizeof(n->disk->disk_name), "dst-%s", n->name);

	return 0;

err_out_put:
	put_disk(n->disk);
err_out_free_queue:
	blk_cleanup_queue(n->queue);
err_out_exit:
	return err;
}

/*
 * Sysfs machinery: show device's size.
 */
static ssize_t dst_show_size(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dst_info *info = container_of(dev, struct dst_info, device);

	return sprintf(buf, "%llu\n", info->size);
}

/*
 * Show local exported device.
 */
static ssize_t dst_show_local(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dst_info *info = container_of(dev, struct dst_info, device);

	return sprintf(buf, "%s\n", info->local);
}

/*
 * Shows type of the remote node - device major/minor number
 * for local nodes and address (af_inet ipv4/ipv6 only) for remote nodes.
 */
static ssize_t dst_show_type(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dst_info *info = container_of(dev, struct dst_info, device);
	int family = info->net.addr.sa_family;

	if (family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&info->net.addr;
		return sprintf(buf, "%u.%u.%u.%u:%d\n",
			NIPQUAD(sin->sin_addr.s_addr), ntohs(sin->sin_port));
	} else if (family == AF_INET6) {
		struct sockaddr_in6 *sin = (struct sockaddr_in6 *)&info->net.addr;
		return sprintf(buf,
			"%pi6:%d\n",
			&sin->sin6_addr, ntohs(sin->sin6_port));
	} else {
		int i, sz = PAGE_SIZE - 2; /* 0 symbol and '\n' below */
		int size, addrlen = info->net.addr.sa_data_len;
		unsigned char *a = (unsigned char *)&info->net.addr.sa_data;
		char *buf_orig = buf;

		size = snprintf(buf, sz, "family: %d, addrlen: %u, addr: ",
				family, addrlen);
		sz -= size;
		buf += size;

		for (i=0; i<addrlen; ++i) {
			if (sz < 3)
				break;

			size = snprintf(buf, sz, "%02x ", a[i]);
			sz -= size;
			buf += size;
		}
		buf += sprintf(buf, "\n");

		return buf - buf_orig;
	}
	return 0;
}

static struct device_attribute dst_node_attrs[] = {
	__ATTR(size, 0444, dst_show_size, NULL),
	__ATTR(type, 0444, dst_show_type, NULL),
	__ATTR(local, 0444, dst_show_local, NULL),
};

static int dst_create_node_attributes(struct dst_node *n)
{
	int err, i;

	for (i=0; i<ARRAY_SIZE(dst_node_attrs); ++i) {
		err = device_create_file(&n->info->device,
				&dst_node_attrs[i]);
		if (err)
			goto err_out_remove_all;
	}
	return 0;

err_out_remove_all:
	while (--i >= 0)
		device_remove_file(&n->info->device,
				&dst_node_attrs[i]);

	return err;
}

static void dst_remove_node_attributes(struct dst_node *n)
{
	int i;

	for (i=0; i<ARRAY_SIZE(dst_node_attrs); ++i)
		device_remove_file(&n->info->device,
				&dst_node_attrs[i]);
}

/*
 * Sysfs cleanup and initialization.
 * Shows number of useful parameters.
 */
static void dst_node_sysfs_exit(struct dst_node *n)
{
	if (n->info) {
		dst_remove_node_attributes(n);
		device_unregister(&n->info->device);
		n->info = NULL;
	}
}

static int dst_node_sysfs_init(struct dst_node *n)
{
	int err;

	n->info = kzalloc(sizeof(struct dst_info), GFP_KERNEL);
	if (!n->info)
		return -ENOMEM;

	memcpy(&n->info->device, &dst_node_dev, sizeof(struct device));
	n->info->size = n->size;

	dev_set_name(&n->info->device, "dst-%s", n->name);
	err = device_register(&n->info->device);
	if (err) {
		dprintk(KERN_ERR "Failed to register node '%s', err: %d.\n",
				n->name, err);
		goto err_out_exit;
	}

	dst_create_node_attributes(n);

	return 0;

err_out_exit:
	kfree(n->info);
	n->info = NULL;
	return err;
}

/*
 * DST node hash tables machinery.
 */
static inline unsigned int dst_hash(char *str, unsigned int size)
{
	return (jhash(str, size, 0) % dst_hashtable_size);
}

static void dst_node_remove(struct dst_node *n)
{
	mutex_lock(&dst_hash_lock);
	list_del_init(&n->node_entry);
	mutex_unlock(&dst_hash_lock);
}

static void dst_node_add(struct dst_node *n)
{
	unsigned hash = dst_hash(n->name, sizeof(n->name));

	mutex_lock(&dst_hash_lock);
	list_add_tail(&n->node_entry, &dst_hashtable[hash]);
	mutex_unlock(&dst_hash_lock);
}

/*
 * Cleaning node when it is about to be freed.
 * There are still users of the socket though,
 * so connection cleanup should be protected.
 */
static void dst_node_cleanup(struct dst_node *n)
{
	struct dst_state *st = n->state;

	if (!st)
		return;

	if (n->queue) {
		blk_cleanup_queue(n->queue);

		mutex_lock(&dst_hash_lock);
		idr_remove(&dst_index_idr, n->disk->first_minor);
		mutex_unlock(&dst_hash_lock);

		put_disk(n->disk);
	}

	if (n->bdev) {
		sync_blockdev(n->bdev);
		blkdev_put(n->bdev, FMODE_READ|FMODE_WRITE);
	}

	dst_state_lock(st);
	st->need_exit = 1;
	dst_state_exit_connected(st);
	dst_state_unlock(st);

	wake_up(&st->thread_wait);

	dst_state_put(st);
	n->state = NULL;
}

/*
 * Free security attributes attached to given node.
 */
static void dst_security_exit(struct dst_node *n)
{
	struct dst_secure *s, *tmp;

	list_for_each_entry_safe(s, tmp, &n->security_list, sec_entry) {
		list_del(&s->sec_entry);
		kfree(s);
	}
}

/*
 * Free node when there are no more users.
 * Actually node has to be freed on behalf od userspace process,
 * since there are number of threads, which are embedded in the
 * node, so they can not exit and free node from there, that is
 * why there is a wakeup if reference counter is not equal to zero.
 */
void dst_node_put(struct dst_node *n)
{
	if (unlikely(!n))
		return;

	dprintk("%s: n: %p, refcnt: %d.\n",
			__func__, n, atomic_read(&n->refcnt));

	if (atomic_dec_and_test(&n->refcnt)) {
		dst_node_remove(n);
		n->trans_scan_timeout = 0;
		dst_node_cleanup(n);
		thread_pool_destroy(n->pool);
		dst_node_sysfs_exit(n);
		dst_node_crypto_exit(n);
		dst_security_exit(n);
		dst_node_trans_exit(n);

		kfree(n);

		dprintk("%s: freed n: %p.\n", __func__, n);
	} else {
		wake_up(&n->wait);
	}
}

/*
 * This function finds devices major/minor numbers for given pathname.
 */
static int dst_lookup_device(const char *path, dev_t *dev)
{
	int err;
	struct nameidata nd;
	struct inode *inode;

	err = path_lookup(path, LOOKUP_FOLLOW, &nd);
	if (err)
		return err;

	inode = nd.path.dentry->d_inode;
	if (!inode) {
		err = -ENOENT;
		goto out;
	}

	if (!S_ISBLK(inode->i_mode)) {
		err = -ENOTBLK;
		goto out;
	}

	*dev = inode->i_rdev;

out:
	path_put(&nd.path);
	return err;
}

/*
 * Setting up export device: lookup by the name, get its size
 * and setup listening socket, which will accept clients, which
 * will submit IO for given storage.
 */
static int dst_setup_export(struct dst_node *n, struct dst_ctl *ctl,
		struct dst_export_ctl *le)
{
	int err;
	dev_t dev = 0; /* gcc likes to scream here */

	snprintf(n->info->local, sizeof(n->info->local), "%s", le->device);

	err = dst_lookup_device(le->device, &dev);
	if (err)
		return err;

	n->bdev = open_by_devnum(dev, FMODE_READ|FMODE_WRITE);
	if (!n->bdev)
		return -ENODEV;

	if (n->size != 0)
		n->size = min_t(loff_t, n->bdev->bd_inode->i_size, n->size);
	else
		n->size = n->bdev->bd_inode->i_size;

	n->info->size = n->size;
	err = dst_node_init_listened(n, le);
	if (err)
		goto err_out_cleanup;

	return 0;

err_out_cleanup:
	blkdev_put(n->bdev, FMODE_READ|FMODE_WRITE);
	n->bdev = NULL;

	return err;
}

/* Empty thread pool callbacks for the network processing threads. */
static inline void *dst_thread_network_init(void *data)
{
	dprintk("%s: data: %p.\n", __func__, data);
	return data;
}

static inline void dst_thread_network_cleanup(void *data)
{
	dprintk("%s: data: %p.\n", __func__, data);
}

/*
 * Allocate DST node and initialize some of its parameters.
 */
static struct dst_node *dst_alloc_node(struct dst_ctl *ctl,
		int (*start)(struct dst_node *),
		int num)
{
	struct dst_node *n;
	int err;

	n = kzalloc(sizeof(struct dst_node), GFP_KERNEL);
	if (!n)
		return NULL;

	INIT_LIST_HEAD(&n->node_entry);

	INIT_LIST_HEAD(&n->security_list);
	mutex_init(&n->security_lock);

	init_waitqueue_head(&n->wait);

	n->trans_scan_timeout = msecs_to_jiffies(ctl->trans_scan_timeout);
	if (!n->trans_scan_timeout)
		n->trans_scan_timeout = HZ;

	n->trans_max_retries = ctl->trans_max_retries;
	if (!n->trans_max_retries)
		n->trans_max_retries = 10;

	/*
	 * Pretty much arbitrary default numbers.
	 * 32 matches maximum number of pages in bio originated from ext3 (31).
	 */
	n->max_pages = ctl->max_pages;
	if (!n->max_pages)
		n->max_pages = 32;

	if (n->max_pages > 1024)
		n->max_pages = 1024;

	n->start = start;
	n->size = ctl->size;

	atomic_set(&n->refcnt, 1);
	atomic_long_set(&n->gen, 0);
	snprintf(n->name, sizeof(n->name), "%s", ctl->name);

	err = dst_node_sysfs_init(n);
	if (err)
		goto err_out_free;

	n->pool = thread_pool_create(num, n->name, dst_thread_network_init,
			dst_thread_network_cleanup, n);
	if (IS_ERR(n->pool)) {
		err = PTR_ERR(n->pool);
		goto err_out_sysfs_exit;
	}

	dprintk("%s: n: %p, name: %s.\n", __func__, n, n->name);

	return n;

err_out_sysfs_exit:
	dst_node_sysfs_exit(n);
err_out_free:
	kfree(n);
	return NULL;
}

/*
 * Starting a node, connected to the remote server:
 * register block device and initialize transaction mechanism.
 * In revers order though.
 *
 * It will autonegotiate some parameters with the remote node
 * and update local if needed.
 *
 * Transaction initialization should be the last thing before
 * starting the node, since transaction should include not only
 * block IO, but also crypto related data (if any), which are
 * initialized separately.
 */
static int dst_start_remote(struct dst_node *n)
{
	int err;

	err = dst_node_trans_init(n, sizeof(struct dst_trans));
	if (err)
		return err;

	err = dst_node_create_disk(n);
	if (err)
		return err;

	dst_node_set_size(n);
	add_disk(n->disk);

	dprintk("DST: started remote node '%s', minor: %d.\n", n->name, n->disk->first_minor);

	return 0;
}

/*
 * Adding remote node and initialize connection.
 */
static int dst_add_remote(struct dst_node *n, struct dst_ctl *ctl,
		void *data, unsigned int size)
{
	int err;
	struct dst_network_ctl *rctl = data;

	if (n)
		return -EEXIST;

	if (size != sizeof(struct dst_network_ctl))
		return -EINVAL;

	n = dst_alloc_node(ctl, dst_start_remote, 1);
	if (!n)
		return -ENOMEM;

	memcpy(&n->info->net, rctl, sizeof(struct dst_network_ctl));
	err = dst_node_init_connected(n, rctl);
	if (err)
		goto err_out_free;

	dst_node_add(n);

	return 0;

err_out_free:
	dst_node_put(n);
	return err;
}

/*
 * Adding export node: initializing block device and listening socket.
 */
static int dst_add_export(struct dst_node *n, struct dst_ctl *ctl,
		void *data, unsigned int size)
{
	int err;
	struct dst_export_ctl *le = data;

	if (n)
		return -EEXIST;

	if (size != sizeof(struct dst_export_ctl))
		return -EINVAL;

	n = dst_alloc_node(ctl, dst_start_export, 2);
	if (!n)
		return -EINVAL;

	err = dst_setup_export(n, ctl, le);
	if (err)
		goto err_out_free;

	dst_node_add(n);

	return 0;

err_out_free:
	dst_node_put(n);
	return err;
}

static int dst_node_remove_unload(struct dst_node *n)
{
	printk(KERN_INFO "STOPPED name: '%s', size: %llu.\n",
			n->name, n->size);

	if (n->disk)
		del_gendisk(n->disk);

	dst_node_remove(n);
	dst_node_sysfs_exit(n);

	/*
	 * This is not a hack. Really.
	 * Node's reference counter allows to implement fine grained
	 * node freeing, but since all transactions (which hold node's
	 * reference counter) are processed in the dedicated thread,
	 * it is possible that reference will hit zero in that thread,
	 * so we will not be able to exit thread and cleanup the node.
	 *
	 * So, we remove disk, so no new activity is possible, and
	 * wait until all pending transaction are completed (either
	 * in receiving thread or by timeout in workqueue), in this
	 * case reference counter will be less or equal to 2 (once set in
	 * dst_alloc_node() and then in connector message parser;
	 * or when we force module unloading, and connector message
	 * parser does not hold a reference, in this case reference
	 * counter will be equal to 1),
	 * and subsequent dst_node_put() calls will free the node.
	 */
	dprintk("%s: going to sleep with %d refcnt.\n", __func__, atomic_read(&n->refcnt));
	wait_event(n->wait, atomic_read(&n->refcnt) <= 2);

	dst_node_put(n);
	return 0;
}

/*
 * Remove node from the hash table.
 */
static int dst_del_node(struct dst_node *n, struct dst_ctl *ctl,
		void *data, unsigned int size)
{
	if (!n)
		return -ENODEV;

	return dst_node_remove_unload(n);
}

/*
 * Initialize crypto processing for given node.
 */
static int dst_crypto_init(struct dst_node *n, struct dst_ctl *ctl,
		void *data, unsigned int size)
{
	struct dst_crypto_ctl *crypto = data;

	if (!n)
		return -ENODEV;

	if (size != sizeof(struct dst_crypto_ctl) + crypto->hash_keysize +
			crypto->cipher_keysize)
		return -EINVAL;

	if (n->trans_cache)
		return -EEXIST;

	return dst_node_crypto_init(n, crypto);
}

/*
 * Security attributes for given node.
 */
static int dst_security_init(struct dst_node *n, struct dst_ctl *ctl,
		void *data, unsigned int size)
{
	struct dst_secure *s;

	if (!n)
		return -ENODEV;

	if (size != sizeof(struct dst_secure_user))
		return -EINVAL;

	s = kmalloc(sizeof(struct dst_secure), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	memcpy(&s->sec, data, size);

	mutex_lock(&n->security_lock);
	list_add_tail(&s->sec_entry, &n->security_list);
	mutex_unlock(&n->security_lock);

	return 0;
}

/*
 * Kill'em all!
 */
static int dst_start_node(struct dst_node *n, struct dst_ctl *ctl,
		void *data, unsigned int size)
{
	int err;

	if (!n)
		return -ENODEV;

	if (n->trans_cache)
		return 0;

	err = n->start(n);
	if (err)
		return err;

	printk(KERN_INFO "STARTED name: '%s', size: %llu.\n", n->name, n->size);
	return 0;
}

typedef int (*dst_command_func)(struct dst_node *n, struct dst_ctl *ctl,
		void *data, unsigned int size);

/*
 * List of userspace commands.
 */
static dst_command_func dst_commands[] = {
	[DST_ADD_REMOTE] = &dst_add_remote,
	[DST_ADD_EXPORT] = &dst_add_export,
	[DST_DEL_NODE] = &dst_del_node,
	[DST_CRYPTO] = &dst_crypto_init,
	[DST_SECURITY] = &dst_security_init,
	[DST_START] = &dst_start_node,
};

/*
 * Configuration parser.
 */
static void cn_dst_callback(struct cn_msg *msg, struct netlink_skb_parms *nsp)
{
	struct dst_ctl *ctl;
	int err;
	struct dst_ctl_ack ack;
	struct dst_node *n = NULL, *tmp;
	unsigned int hash;

	if (!cap_raised(nsp->eff_cap, CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto out;
	}

	if (msg->len < sizeof(struct dst_ctl)) {
		err = -EBADMSG;
		goto out;
	}

	ctl = (struct dst_ctl *)msg->data;

	if (ctl->cmd >= DST_CMD_MAX) {
		err = -EINVAL;
		goto out;
	}
	hash = dst_hash(ctl->name, sizeof(ctl->name));

	mutex_lock(&dst_hash_lock);
	list_for_each_entry(tmp, &dst_hashtable[hash], node_entry) {
		if (!memcmp(tmp->name, ctl->name, sizeof(tmp->name))) {
			n = tmp;
			dst_node_get(n);
			break;
		}
	}
	mutex_unlock(&dst_hash_lock);

	err = dst_commands[ctl->cmd](n, ctl, msg->data + sizeof(struct dst_ctl),
			msg->len - sizeof(struct dst_ctl));

	dst_node_put(n);
out:
	memcpy(&ack.msg, msg, sizeof(struct cn_msg));

	ack.msg.ack = msg->ack + 1;
	ack.msg.len = sizeof(struct dst_ctl_ack) - sizeof(struct cn_msg);

	ack.error = err;

	cn_netlink_send(&ack.msg, 0, GFP_KERNEL);
}

/*
 * Global initialization: sysfs, hash table, block device registration,
 * connector and various caches.
 */
static int __init dst_sysfs_init(void)
{
	return bus_register(&dst_dev_bus_type);
}

static void dst_sysfs_exit(void)
{
	bus_unregister(&dst_dev_bus_type);
}

static int __init dst_hashtable_init(void)
{
	unsigned int i;

	dst_hashtable = kcalloc(dst_hashtable_size, sizeof(struct list_head),
			GFP_KERNEL);
	if (!dst_hashtable)
		return -ENOMEM;

	for (i=0; i<dst_hashtable_size; ++i)
		INIT_LIST_HEAD(&dst_hashtable[i]);

	return 0;
}

static void dst_hashtable_exit(void)
{
	unsigned int i;
	struct dst_node *n, *tmp;

	for (i=0; i<dst_hashtable_size; ++i) {
		list_for_each_entry_safe(n, tmp, &dst_hashtable[i], node_entry) {
			dst_node_remove_unload(n);
		}
	}

	kfree(dst_hashtable);
}

static int __init dst_sys_init(void)
{
	int err = -ENOMEM;

	err = dst_hashtable_init();
	if (err)
		goto err_out_exit;

	err = dst_export_init();
	if (err)
		goto err_out_hashtable_exit;

	err = register_blkdev(dst_major, DST_NAME);
	if (err < 0)
		goto err_out_export_exit;
	if (err)
		dst_major = err;

	err = dst_sysfs_init();
	if (err)
		goto err_out_unregister;

	err = cn_add_callback(&cn_dst_id, "DST", cn_dst_callback);
	if (err)
		goto err_out_sysfs_exit;

	printk(KERN_INFO "Distributed storage, '%s' release.\n", dst_name);

	return 0;

err_out_sysfs_exit:
	dst_sysfs_exit();
err_out_unregister:
	unregister_blkdev(dst_major, DST_NAME);
err_out_export_exit:
	dst_export_exit();
err_out_hashtable_exit:
	dst_hashtable_exit();
err_out_exit:
	return err;
}

static void __exit dst_sys_exit(void)
{
	cn_del_callback(&cn_dst_id);
	unregister_blkdev(dst_major, DST_NAME);
	dst_hashtable_exit();
	dst_sysfs_exit();
	dst_export_exit();
}

module_init(dst_sys_init);
module_exit(dst_sys_exit);

MODULE_DESCRIPTION("Distributed storage");
MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_LICENSE("GPL");
