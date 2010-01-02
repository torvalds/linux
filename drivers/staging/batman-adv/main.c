/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
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
#include "proc.h"
#include "routing.h"
#include "send.h"
#include "originator.h"
#include "soft-interface.h"
#include "device.h"
#include "translation-table.h"
#include "hard-interface.h"
#include "types.h"
#include "vis.h"
#include "hash.h"
#include "compat.h"

struct list_head if_list;
struct hlist_head forw_bat_list;
struct hlist_head forw_bcast_list;
struct hashtable_t *orig_hash;

DEFINE_SPINLOCK(orig_hash_lock);
DEFINE_SPINLOCK(forw_bat_list_lock);
DEFINE_SPINLOCK(forw_bcast_list_lock);

atomic_t originator_interval;
atomic_t vis_interval;
atomic_t aggregation_enabled;
int16_t num_hna;
int16_t num_ifs;

struct net_device *soft_device;

unsigned char broadcastAddr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
atomic_t module_state;

static struct packet_type batman_adv_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_BATMAN),
	.func = batman_skb_recv,
};

struct workqueue_struct *bat_event_workqueue;

#ifdef CONFIG_BATMAN_ADV_DEBUG
int debug;

module_param(debug, int, 0644);

int bat_debug_type(int type)
{
	return debug & type;
}
#endif

int init_module(void)
{
	int retval;

	INIT_LIST_HEAD(&if_list);
	INIT_HLIST_HEAD(&forw_bat_list);
	INIT_HLIST_HEAD(&forw_bcast_list);

	atomic_set(&module_state, MODULE_INACTIVE);

	atomic_set(&originator_interval, 1000);
	atomic_set(&vis_interval, 1000);/* TODO: raise this later, this is only
					 * for debugging now. */
	atomic_set(&aggregation_enabled, 1);

	/* the name should not be longer than 10 chars - see
	 * http://lwn.net/Articles/23634/ */
	bat_event_workqueue = create_singlethread_workqueue("bat_events");

	if (!bat_event_workqueue)
		return -ENOMEM;

	retval = setup_procfs();
	if (retval < 0)
		return retval;

	bat_device_init();

	/* initialize layer 2 interface */
	soft_device = alloc_netdev(sizeof(struct bat_priv) , "bat%d",
				   interface_setup);

	if (!soft_device) {
		printk(KERN_ERR "batman-adv:Unable to allocate the batman interface\n");
		goto end;
	}

	retval = register_netdev(soft_device);

	if (retval < 0) {
		printk(KERN_ERR "batman-adv:Unable to register the batman interface: %i\n", retval);
		goto free_soft_device;
	}

	register_netdevice_notifier(&hard_if_notifier);
	dev_add_pack(&batman_adv_packet_type);

	printk(KERN_INFO "batman-adv:B.A.T.M.A.N. advanced %s%s (compatibility version %i) loaded \n",
		  SOURCE_VERSION, REVISION_VERSION_STR, COMPAT_VERSION);

	return 0;

free_soft_device:
	free_netdev(soft_device);
	soft_device = NULL;
end:
	return -ENOMEM;
}

void cleanup_module(void)
{
	shutdown_module();

	if (soft_device) {
		unregister_netdev(soft_device);
		soft_device = NULL;
	}

	dev_remove_pack(&batman_adv_packet_type);

	unregister_netdevice_notifier(&hard_if_notifier);
	cleanup_procfs();

	destroy_workqueue(bat_event_workqueue);
	bat_event_workqueue = NULL;
}

/* activates the module, creates bat device, starts timer ... */
void activate_module(void)
{
	if (originator_init() < 1)
		goto err;

	if (hna_local_init() < 1)
		goto err;

	if (hna_global_init() < 1)
		goto err;

	hna_local_add(soft_device->dev_addr);

	if (bat_device_setup() < 1)
		goto end;

	if (vis_init() < 1)
		goto err;

	update_min_mtu();
	atomic_set(&module_state, MODULE_ACTIVE);
	goto end;

err:
	printk(KERN_ERR "batman-adv:Unable to allocate memory for mesh information structures: out of mem ?\n");
	shutdown_module();
end:
	return;
}

/* shuts down the whole module.*/
void shutdown_module(void)
{
	atomic_set(&module_state, MODULE_DEACTIVATING);

	purge_outstanding_packets();
	flush_workqueue(bat_event_workqueue);

	vis_quit();

	/* TODO: unregister BATMAN pack */

	originator_free();

	hna_local_free();
	hna_global_free();

	synchronize_net();
	bat_device_destroy();

	hardif_remove_interfaces();
	synchronize_rcu();
	atomic_set(&module_state, MODULE_INACTIVE);
}

void inc_module_count(void)
{
	try_module_get(THIS_MODULE);
}

void dec_module_count(void)
{
	module_put(THIS_MODULE);
}

int addr_to_string(char *buff, uint8_t *addr)
{
	return sprintf(buff, "%02x:%02x:%02x:%02x:%02x:%02x",
		       addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

/* returns 1 if they are the same originator */

int compare_orig(void *data1, void *data2)
{
	return (memcmp(data1, data2, ETH_ALEN) == 0 ? 1 : 0);
}

/* hashfunction to choose an entry in a hash table of given size */
/* hash algorithm from http://en.wikipedia.org/wiki/Hash_table */
int choose_orig(void *data, int32_t size)
{
	unsigned char *key = data;
	uint32_t hash = 0;
	size_t i;

	for (i = 0; i < 6; i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash % size;
}

int is_my_mac(uint8_t *addr)
{
	struct batman_if *batman_if;
	rcu_read_lock();
	list_for_each_entry_rcu(batman_if, &if_list, list) {
		if ((batman_if->net_dev) &&
		    (compare_orig(batman_if->net_dev->dev_addr, addr))) {
			rcu_read_unlock();
			return 1;
		}
	}
	rcu_read_unlock();
	return 0;

}

int is_bcast(uint8_t *addr)
{
	return (addr[0] == (uint8_t)0xff) && (addr[1] == (uint8_t)0xff);
}

int is_mcast(uint8_t *addr)
{
	return *addr & 0x01;
}

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
#ifdef REVISION_VERSION
MODULE_VERSION(SOURCE_VERSION "-" REVISION_VERSION);
#else
MODULE_VERSION(SOURCE_VERSION);
#endif
