/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include "main.h"
#include "device.h"
#include "send.h"
#include "types.h"
#include "hash.h"

#include "compat.h"

static struct class *batman_class;

static int Major;	/* Major number assigned to our device driver */

static const struct file_operations fops = {
	.open = bat_device_open,
	.release = bat_device_release,
	.read = bat_device_read,
	.write = bat_device_write,
	.poll = bat_device_poll,
};

static struct device_client *device_client_hash[256];

void bat_device_init(void)
{
	int i;

	for (i = 0; i < 256; i++)
		device_client_hash[i] = NULL;
}

int bat_device_setup(void)
{
	int tmp_major;

	if (Major)
		return 1;

	/* register our device - kernel assigns a free major number */
	tmp_major = register_chrdev(0, DRIVER_DEVICE, &fops);
	if (tmp_major < 0) {
		printk(KERN_ERR "batman-adv:Registering the character device failed with %d\n",
			  tmp_major);
		return 0;
	}

	batman_class = class_create(THIS_MODULE, "batman-adv");

	if (IS_ERR(batman_class)) {
		printk(KERN_ERR "batman-adv:Could not register class 'batman-adv' \n");
		return 0;
	}

	device_create(batman_class, NULL, MKDEV(tmp_major, 0), NULL,
		      "batman-adv");

	Major = tmp_major;
	return 1;
}

void bat_device_destroy(void)
{
	if (!Major)
		return;

	device_destroy(batman_class, MKDEV(Major, 0));
	class_destroy(batman_class);

	/* Unregister the device */
	unregister_chrdev(Major, DRIVER_DEVICE);

	Major = 0;
}

int bat_device_open(struct inode *inode, struct file *file)
{
	unsigned int i;
	struct device_client *device_client;

	device_client = kmalloc(sizeof(struct device_client), GFP_KERNEL);

	if (!device_client)
		return -ENOMEM;

	for (i = 0; i < 256; i++) {
		if (!device_client_hash[i]) {
			device_client_hash[i] = device_client;
			break;
		}
	}

	if (device_client_hash[i] != device_client) {
		printk(KERN_ERR "batman-adv:Error - can't add another packet client: maximum number of clients reached \n");
		kfree(device_client);
		return -EXFULL;
	}

	INIT_LIST_HEAD(&device_client->queue_list);
	device_client->queue_len = 0;
	device_client->index = i;
	device_client->lock = __SPIN_LOCK_UNLOCKED(device_client->lock);
	init_waitqueue_head(&device_client->queue_wait);

	file->private_data = device_client;

	inc_module_count();
	return 0;
}

int bat_device_release(struct inode *inode, struct file *file)
{
	struct device_client *device_client =
		(struct device_client *)file->private_data;
	struct device_packet *device_packet;
	struct list_head *list_pos, *list_pos_tmp;

	spin_lock(&device_client->lock);

	/* for all packets in the queue ... */
	list_for_each_safe(list_pos, list_pos_tmp, &device_client->queue_list) {
		device_packet = list_entry(list_pos,
					   struct device_packet, list);

		list_del(list_pos);
		kfree(device_packet);
	}

	device_client_hash[device_client->index] = NULL;
	spin_unlock(&device_client->lock);

	kfree(device_client);
	dec_module_count();

	return 0;
}

ssize_t bat_device_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	struct device_client *device_client =
		(struct device_client *)file->private_data;
	struct device_packet *device_packet;
	int error;

	if ((file->f_flags & O_NONBLOCK) && (device_client->queue_len == 0))
		return -EAGAIN;

	if ((!buf) || (count < sizeof(struct icmp_packet)))
		return -EINVAL;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	error = wait_event_interruptible(device_client->queue_wait,
					 device_client->queue_len);

	if (error)
		return error;

	spin_lock(&device_client->lock);

	device_packet = list_first_entry(&device_client->queue_list,
					 struct device_packet, list);
	list_del(&device_packet->list);
	device_client->queue_len--;

	spin_unlock(&device_client->lock);

	error = __copy_to_user(buf, &device_packet->icmp_packet,
			       sizeof(struct icmp_packet));

	kfree(device_packet);

	if (error)
		return error;

	return sizeof(struct icmp_packet);
}

ssize_t bat_device_write(struct file *file, const char __user *buff,
			 size_t len, loff_t *off)
{
	struct device_client *device_client =
		(struct device_client *)file->private_data;
	struct icmp_packet icmp_packet;
	struct orig_node *orig_node;
	struct batman_if *batman_if;

	if (len < sizeof(struct icmp_packet)) {
		printk(KERN_DEBUG "batman-adv:Error - can't send packet from char device: invalid packet size\n");
		return -EINVAL;
	}

	if (!access_ok(VERIFY_READ, buff, sizeof(struct icmp_packet)))
		return -EFAULT;

	if (__copy_from_user(&icmp_packet, buff, sizeof(icmp_packet)))
		return -EFAULT;

	if (icmp_packet.packet_type != BAT_ICMP) {
		printk(KERN_DEBUG "batman-adv:Error - can't send packet from char device: got bogus packet type (expected: BAT_ICMP)\n");
		return -EINVAL;
	}

	if (icmp_packet.msg_type != ECHO_REQUEST) {
		printk(KERN_DEBUG "batman-adv:Error - can't send packet from char device: got bogus message type (expected: ECHO_REQUEST)\n");
		return -EINVAL;
	}

	icmp_packet.uid = device_client->index;

	if (icmp_packet.version != COMPAT_VERSION) {
		icmp_packet.msg_type = PARAMETER_PROBLEM;
		icmp_packet.ttl = COMPAT_VERSION;
		bat_device_add_packet(device_client, &icmp_packet);
		goto out;
	}

	if (atomic_read(&module_state) != MODULE_ACTIVE)
		goto dst_unreach;

	spin_lock(&orig_hash_lock);
	orig_node = ((struct orig_node *)hash_find(orig_hash, icmp_packet.dst));

	if (!orig_node)
		goto unlock;

	if (!orig_node->router)
		goto unlock;

	batman_if = orig_node->batman_if;

	if (!batman_if)
		goto unlock;

	memcpy(icmp_packet.orig,
	       batman_if->net_dev->dev_addr,
	       ETH_ALEN);

	send_raw_packet((unsigned char *)&icmp_packet,
			sizeof(struct icmp_packet),
			batman_if, orig_node->router->addr);

	spin_unlock(&orig_hash_lock);
	goto out;

unlock:
	spin_unlock(&orig_hash_lock);
dst_unreach:
	icmp_packet.msg_type = DESTINATION_UNREACHABLE;
	bat_device_add_packet(device_client, &icmp_packet);
out:
	return len;
}

unsigned int bat_device_poll(struct file *file, poll_table *wait)
{
	struct device_client *device_client =
		(struct device_client *)file->private_data;

	poll_wait(file, &device_client->queue_wait, wait);

	if (device_client->queue_len > 0)
		return POLLIN | POLLRDNORM;

	return 0;
}

void bat_device_add_packet(struct device_client *device_client,
			   struct icmp_packet *icmp_packet)
{
	struct device_packet *device_packet;

	device_packet = kmalloc(sizeof(struct device_packet), GFP_KERNEL);

	if (!device_packet)
		return;

	INIT_LIST_HEAD(&device_packet->list);
	memcpy(&device_packet->icmp_packet, icmp_packet,
	       sizeof(struct icmp_packet));

	spin_lock(&device_client->lock);

	/* while waiting for the lock the device_client could have been
	 * deleted */
	if (!device_client_hash[icmp_packet->uid]) {
		spin_unlock(&device_client->lock);
		kfree(device_packet);
		return;
	}

	list_add_tail(&device_packet->list, &device_client->queue_list);
	device_client->queue_len++;

	if (device_client->queue_len > 100) {
		device_packet = list_first_entry(&device_client->queue_list,
						 struct device_packet, list);

		list_del(&device_packet->list);
		kfree(device_packet);
		device_client->queue_len--;
	}

	spin_unlock(&device_client->lock);

	wake_up(&device_client->queue_wait);
}

void bat_device_receive_packet(struct icmp_packet *icmp_packet)
{
	struct device_client *hash = device_client_hash[icmp_packet->uid];

	if (hash)
		bat_device_add_packet(hash, icmp_packet);
}
