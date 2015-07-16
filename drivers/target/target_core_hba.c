/*******************************************************************************
 * Filename:  target_core_hba.c
 *
 * This file contains the TCM HBA Transport related functions.
 *
 * (c) Copyright 2003-2013 Datera, Inc.
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
#include <linux/in.h>
#include <linux/module.h>
#include <net/sock.h>
#include <net/tcp.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"

static LIST_HEAD(backend_list);
static DEFINE_MUTEX(backend_mutex);

static u32 hba_id_counter;

static DEFINE_SPINLOCK(hba_lock);
static LIST_HEAD(hba_list);


int transport_backend_register(const struct target_backend_ops *ops)
{
	struct target_backend *tb, *old;

	tb = kzalloc(sizeof(*tb), GFP_KERNEL);
	if (!tb)
		return -ENOMEM;
	tb->ops = ops;

	mutex_lock(&backend_mutex);
	list_for_each_entry(old, &backend_list, list) {
		if (!strcmp(old->ops->name, ops->name)) {
			pr_err("backend %s already registered.\n", ops->name);
			mutex_unlock(&backend_mutex);
			kfree(tb);
			return -EEXIST;
		}
	}
	target_setup_backend_cits(tb);
	list_add_tail(&tb->list, &backend_list);
	mutex_unlock(&backend_mutex);

	pr_debug("TCM: Registered subsystem plugin: %s struct module: %p\n",
			ops->name, ops->owner);
	return 0;
}
EXPORT_SYMBOL(transport_backend_register);

void target_backend_unregister(const struct target_backend_ops *ops)
{
	struct target_backend *tb;

	mutex_lock(&backend_mutex);
	list_for_each_entry(tb, &backend_list, list) {
		if (tb->ops == ops) {
			list_del(&tb->list);
			kfree(tb);
			break;
		}
	}
	mutex_unlock(&backend_mutex);
}
EXPORT_SYMBOL(target_backend_unregister);

static struct target_backend *core_get_backend(const char *name)
{
	struct target_backend *tb;

	mutex_lock(&backend_mutex);
	list_for_each_entry(tb, &backend_list, list) {
		if (!strcmp(tb->ops->name, name))
			goto found;
	}
	mutex_unlock(&backend_mutex);
	return NULL;
found:
	if (tb->ops->owner && !try_module_get(tb->ops->owner))
		tb = NULL;
	mutex_unlock(&backend_mutex);
	return tb;
}

struct se_hba *
core_alloc_hba(const char *plugin_name, u32 plugin_dep_id, u32 hba_flags)
{
	struct se_hba *hba;
	int ret = 0;

	hba = kzalloc(sizeof(*hba), GFP_KERNEL);
	if (!hba) {
		pr_err("Unable to allocate struct se_hba\n");
		return ERR_PTR(-ENOMEM);
	}

	spin_lock_init(&hba->device_lock);
	mutex_init(&hba->hba_access_mutex);

	hba->hba_index = scsi_get_new_index(SCSI_INST_INDEX);
	hba->hba_flags |= hba_flags;

	hba->backend = core_get_backend(plugin_name);
	if (!hba->backend) {
		ret = -EINVAL;
		goto out_free_hba;
	}

	ret = hba->backend->ops->attach_hba(hba, plugin_dep_id);
	if (ret < 0)
		goto out_module_put;

	spin_lock(&hba_lock);
	hba->hba_id = hba_id_counter++;
	list_add_tail(&hba->hba_node, &hba_list);
	spin_unlock(&hba_lock);

	pr_debug("CORE_HBA[%d] - Attached HBA to Generic Target"
			" Core\n", hba->hba_id);

	return hba;

out_module_put:
	module_put(hba->backend->ops->owner);
	hba->backend = NULL;
out_free_hba:
	kfree(hba);
	return ERR_PTR(ret);
}

int
core_delete_hba(struct se_hba *hba)
{
	WARN_ON(hba->dev_count);

	hba->backend->ops->detach_hba(hba);

	spin_lock(&hba_lock);
	list_del(&hba->hba_node);
	spin_unlock(&hba_lock);

	pr_debug("CORE_HBA[%d] - Detached HBA from Generic Target"
			" Core\n", hba->hba_id);

	module_put(hba->backend->ops->owner);

	hba->backend = NULL;
	kfree(hba);
	return 0;
}

bool target_sense_desc_format(struct se_device *dev)
{
	return dev->transport->get_blocks(dev) > U32_MAX;
}
