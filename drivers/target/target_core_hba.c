/*******************************************************************************
 * Filename:  target_core_hba.c
 *
 * This file copntains the iSCSI HBA Transport related functions.
 *
 * Copyright (c) 2003, 2004, 2005 PyX Technologies, Inc.
 * Copyright (c) 2005, 2006, 2007 SBE, Inc.
 * Copyright (c) 2007-2010 Rising Tide Systems
 * Copyright (c) 2008-2010 Linux-iSCSI.org
 *
 * Nicholas A. Bellinger <nab@kernel.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/net.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/in.h>
#include <net/sock.h>
#include <net/tcp.h>

#include <target/target_core_base.h>
#include <target/target_core_device.h>
#include <target/target_core_device.h>
#include <target/target_core_tpg.h>
#include <target/target_core_transport.h>

#include "target_core_hba.h"

static LIST_HEAD(subsystem_list);
static DEFINE_MUTEX(subsystem_mutex);

int transport_subsystem_register(struct se_subsystem_api *sub_api)
{
	struct se_subsystem_api *s;

	INIT_LIST_HEAD(&sub_api->sub_api_list);

	mutex_lock(&subsystem_mutex);
	list_for_each_entry(s, &subsystem_list, sub_api_list) {
		if (!(strcmp(s->name, sub_api->name))) {
			printk(KERN_ERR "%p is already registered with"
				" duplicate name %s, unable to process"
				" request\n", s, s->name);
			mutex_unlock(&subsystem_mutex);
			return -EEXIST;
		}
	}
	list_add_tail(&sub_api->sub_api_list, &subsystem_list);
	mutex_unlock(&subsystem_mutex);

	printk(KERN_INFO "TCM: Registered subsystem plugin: %s struct module:"
			" %p\n", sub_api->name, sub_api->owner);
	return 0;
}
EXPORT_SYMBOL(transport_subsystem_register);

void transport_subsystem_release(struct se_subsystem_api *sub_api)
{
	mutex_lock(&subsystem_mutex);
	list_del(&sub_api->sub_api_list);
	mutex_unlock(&subsystem_mutex);
}
EXPORT_SYMBOL(transport_subsystem_release);

static struct se_subsystem_api *core_get_backend(const char *sub_name)
{
	struct se_subsystem_api *s;

	mutex_lock(&subsystem_mutex);
	list_for_each_entry(s, &subsystem_list, sub_api_list) {
		if (!strcmp(s->name, sub_name))
			goto found;
	}
	mutex_unlock(&subsystem_mutex);
	return NULL;
found:
	if (s->owner && !try_module_get(s->owner))
		s = NULL;
	mutex_unlock(&subsystem_mutex);
	return s;
}

struct se_hba *
core_alloc_hba(const char *plugin_name, u32 plugin_dep_id, u32 hba_flags)
{
	struct se_hba *hba;
	int ret = 0;

	hba = kzalloc(sizeof(*hba), GFP_KERNEL);
	if (!hba) {
		printk(KERN_ERR "Unable to allocate struct se_hba\n");
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&hba->hba_dev_list);
	spin_lock_init(&hba->device_lock);
	spin_lock_init(&hba->hba_queue_lock);
	mutex_init(&hba->hba_access_mutex);

	hba->hba_index = scsi_get_new_index(SCSI_INST_INDEX);
	hba->hba_flags |= hba_flags;

	atomic_set(&hba->max_queue_depth, 0);
	atomic_set(&hba->left_queue_depth, 0);

	hba->transport = core_get_backend(plugin_name);
	if (!hba->transport) {
		ret = -EINVAL;
		goto out_free_hba;
	}

	ret = hba->transport->attach_hba(hba, plugin_dep_id);
	if (ret < 0)
		goto out_module_put;

	spin_lock(&se_global->hba_lock);
	hba->hba_id = se_global->g_hba_id_counter++;
	list_add_tail(&hba->hba_list, &se_global->g_hba_list);
	spin_unlock(&se_global->hba_lock);

	printk(KERN_INFO "CORE_HBA[%d] - Attached HBA to Generic Target"
			" Core\n", hba->hba_id);

	return hba;

out_module_put:
	if (hba->transport->owner)
		module_put(hba->transport->owner);
	hba->transport = NULL;
out_free_hba:
	kfree(hba);
	return ERR_PTR(ret);
}

int
core_delete_hba(struct se_hba *hba)
{
	struct se_device *dev, *dev_tmp;

	spin_lock(&hba->device_lock);
	list_for_each_entry_safe(dev, dev_tmp, &hba->hba_dev_list, dev_list) {

		se_clear_dev_ports(dev);
		spin_unlock(&hba->device_lock);

		se_release_device_for_hba(dev);

		spin_lock(&hba->device_lock);
	}
	spin_unlock(&hba->device_lock);

	hba->transport->detach_hba(hba);

	spin_lock(&se_global->hba_lock);
	list_del(&hba->hba_list);
	spin_unlock(&se_global->hba_lock);

	printk(KERN_INFO "CORE_HBA[%d] - Detached HBA from Generic Target"
			" Core\n", hba->hba_id);

	if (hba->transport->owner)
		module_put(hba->transport->owner);

	hba->transport = NULL;
	kfree(hba);
	return 0;
}
