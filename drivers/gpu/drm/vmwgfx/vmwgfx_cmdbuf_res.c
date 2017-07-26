/**************************************************************************
 *
 * Copyright © 2014-2015 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"

#define VMW_CMDBUF_RES_MAN_HT_ORDER 12

/**
 * struct vmw_cmdbuf_res - Command buffer managed resource entry.
 *
 * @res: Refcounted pointer to a struct vmw_resource.
 * @hash: Hash entry for the manager hash table.
 * @head: List head used either by the staging list or the manager list
 * of commited resources.
 * @state: Staging state of this resource entry.
 * @man: Pointer to a resource manager for this entry.
 */
struct vmw_cmdbuf_res {
	struct vmw_resource *res;
	struct drm_hash_item hash;
	struct list_head head;
	enum vmw_cmdbuf_res_state state;
	struct vmw_cmdbuf_res_manager *man;
};

/**
 * struct vmw_cmdbuf_res_manager - Command buffer resource manager.
 *
 * @resources: Hash table containing staged and commited command buffer
 * resources
 * @list: List of commited command buffer resources.
 * @dev_priv: Pointer to a device private structure.
 *
 * @resources and @list are protected by the cmdbuf mutex for now.
 */
struct vmw_cmdbuf_res_manager {
	struct drm_open_hash resources;
	struct list_head list;
	struct vmw_private *dev_priv;
};


/**
 * vmw_cmdbuf_res_lookup - Look up a command buffer resource
 *
 * @man: Pointer to the command buffer resource manager
 * @resource_type: The resource type, that combined with the user key
 * identifies the resource.
 * @user_key: The user key.
 *
 * Returns a valid refcounted struct vmw_resource pointer on success,
 * an error pointer on failure.
 */
struct vmw_resource *
vmw_cmdbuf_res_lookup(struct vmw_cmdbuf_res_manager *man,
		      enum vmw_cmdbuf_res_type res_type,
		      u32 user_key)
{
	struct drm_hash_item *hash;
	int ret;
	unsigned long key = user_key | (res_type << 24);

	ret = drm_ht_find_item(&man->resources, key, &hash);
	if (unlikely(ret != 0))
		return ERR_PTR(ret);

	return vmw_resource_reference
		(drm_hash_entry(hash, struct vmw_cmdbuf_res, hash)->res);
}

/**
 * vmw_cmdbuf_res_free - Free a command buffer resource.
 *
 * @man: Pointer to the command buffer resource manager
 * @entry: Pointer to a struct vmw_cmdbuf_res.
 *
 * Frees a struct vmw_cmdbuf_res entry and drops its reference to the
 * struct vmw_resource.
 */
static void vmw_cmdbuf_res_free(struct vmw_cmdbuf_res_manager *man,
				struct vmw_cmdbuf_res *entry)
{
	list_del(&entry->head);
	WARN_ON(drm_ht_remove_item(&man->resources, &entry->hash));
	vmw_resource_unreference(&entry->res);
	kfree(entry);
}

/**
 * vmw_cmdbuf_res_commit - Commit a list of command buffer resource actions
 *
 * @list: Caller's list of command buffer resource actions.
 *
 * This function commits a list of command buffer resource
 * additions or removals.
 * It is typically called when the execbuf ioctl call triggering these
 * actions has commited the fifo contents to the device.
 */
void vmw_cmdbuf_res_commit(struct list_head *list)
{
	struct vmw_cmdbuf_res *entry, *next;

	list_for_each_entry_safe(entry, next, list, head) {
		list_del(&entry->head);
		if (entry->res->func->commit_notify)
			entry->res->func->commit_notify(entry->res,
							entry->state);
		switch (entry->state) {
		case VMW_CMDBUF_RES_ADD:
			entry->state = VMW_CMDBUF_RES_COMMITTED;
			list_add_tail(&entry->head, &entry->man->list);
			break;
		case VMW_CMDBUF_RES_DEL:
			vmw_resource_unreference(&entry->res);
			kfree(entry);
			break;
		default:
			BUG();
			break;
		}
	}
}

/**
 * vmw_cmdbuf_res_revert - Revert a list of command buffer resource actions
 *
 * @man: Pointer to the command buffer resource manager
 * @list: Caller's list of command buffer resource action
 *
 * This function reverts a list of command buffer resource
 * additions or removals.
 * It is typically called when the execbuf ioctl call triggering these
 * actions failed for some reason, and the command stream was never
 * submitted.
 */
void vmw_cmdbuf_res_revert(struct list_head *list)
{
	struct vmw_cmdbuf_res *entry, *next;
	int ret;

	list_for_each_entry_safe(entry, next, list, head) {
		switch (entry->state) {
		case VMW_CMDBUF_RES_ADD:
			vmw_cmdbuf_res_free(entry->man, entry);
			break;
		case VMW_CMDBUF_RES_DEL:
			ret = drm_ht_insert_item(&entry->man->resources,
						 &entry->hash);
			list_del(&entry->head);
			list_add_tail(&entry->head, &entry->man->list);
			entry->state = VMW_CMDBUF_RES_COMMITTED;
			break;
		default:
			BUG();
			break;
		}
	}
}

/**
 * vmw_cmdbuf_res_add - Stage a command buffer managed resource for addition.
 *
 * @man: Pointer to the command buffer resource manager.
 * @res_type: The resource type.
 * @user_key: The user-space id of the resource.
 * @res: Valid (refcount != 0) pointer to a struct vmw_resource.
 * @list: The staging list.
 *
 * This function allocates a struct vmw_cmdbuf_res entry and adds the
 * resource to the hash table of the manager identified by @man. The
 * entry is then put on the staging list identified by @list.
 */
int vmw_cmdbuf_res_add(struct vmw_cmdbuf_res_manager *man,
		       enum vmw_cmdbuf_res_type res_type,
		       u32 user_key,
		       struct vmw_resource *res,
		       struct list_head *list)
{
	struct vmw_cmdbuf_res *cres;
	int ret;

	cres = kzalloc(sizeof(*cres), GFP_KERNEL);
	if (unlikely(cres == NULL))
		return -ENOMEM;

	cres->hash.key = user_key | (res_type << 24);
	ret = drm_ht_insert_item(&man->resources, &cres->hash);
	if (unlikely(ret != 0))
		goto out_invalid_key;

	cres->state = VMW_CMDBUF_RES_ADD;
	cres->res = vmw_resource_reference(res);
	cres->man = man;
	list_add_tail(&cres->head, list);

out_invalid_key:
	return ret;
}

/**
 * vmw_cmdbuf_res_remove - Stage a command buffer managed resource for removal.
 *
 * @man: Pointer to the command buffer resource manager.
 * @res_type: The resource type.
 * @user_key: The user-space id of the resource.
 * @list: The staging list.
 * @res_p: If the resource is in an already committed state, points to the
 * struct vmw_resource on successful return. The pointer will be
 * non ref-counted.
 *
 * This function looks up the struct vmw_cmdbuf_res entry from the manager
 * hash table and, if it exists, removes it. Depending on its current staging
 * state it then either removes the entry from the staging list or adds it
 * to it with a staging state of removal.
 */
int vmw_cmdbuf_res_remove(struct vmw_cmdbuf_res_manager *man,
			  enum vmw_cmdbuf_res_type res_type,
			  u32 user_key,
			  struct list_head *list,
			  struct vmw_resource **res_p)
{
	struct vmw_cmdbuf_res *entry;
	struct drm_hash_item *hash;
	int ret;

	ret = drm_ht_find_item(&man->resources, user_key | (res_type << 24),
			       &hash);
	if (likely(ret != 0))
		return -EINVAL;

	entry = drm_hash_entry(hash, struct vmw_cmdbuf_res, hash);

	switch (entry->state) {
	case VMW_CMDBUF_RES_ADD:
		vmw_cmdbuf_res_free(man, entry);
		*res_p = NULL;
		break;
	case VMW_CMDBUF_RES_COMMITTED:
		(void) drm_ht_remove_item(&man->resources, &entry->hash);
		list_del(&entry->head);
		entry->state = VMW_CMDBUF_RES_DEL;
		list_add_tail(&entry->head, list);
		*res_p = entry->res;
		break;
	default:
		BUG();
		break;
	}

	return 0;
}

/**
 * vmw_cmdbuf_res_man_create - Allocate a command buffer managed resource
 * manager.
 *
 * @dev_priv: Pointer to a struct vmw_private
 *
 * Allocates and initializes a command buffer managed resource manager. Returns
 * an error pointer on failure.
 */
struct vmw_cmdbuf_res_manager *
vmw_cmdbuf_res_man_create(struct vmw_private *dev_priv)
{
	struct vmw_cmdbuf_res_manager *man;
	int ret;

	man = kzalloc(sizeof(*man), GFP_KERNEL);
	if (man == NULL)
		return ERR_PTR(-ENOMEM);

	man->dev_priv = dev_priv;
	INIT_LIST_HEAD(&man->list);
	ret = drm_ht_create(&man->resources, VMW_CMDBUF_RES_MAN_HT_ORDER);
	if (ret == 0)
		return man;

	kfree(man);
	return ERR_PTR(ret);
}

/**
 * vmw_cmdbuf_res_man_destroy - Destroy a command buffer managed resource
 * manager.
 *
 * @man: Pointer to the  manager to destroy.
 *
 * This function destroys a command buffer managed resource manager and
 * unreferences / frees all command buffer managed resources and -entries
 * associated with it.
 */
void vmw_cmdbuf_res_man_destroy(struct vmw_cmdbuf_res_manager *man)
{
	struct vmw_cmdbuf_res *entry, *next;

	list_for_each_entry_safe(entry, next, &man->list, head)
		vmw_cmdbuf_res_free(man, entry);

	drm_ht_remove(&man->resources);
	kfree(man);
}

/**
 *
 * vmw_cmdbuf_res_man_size - Return the size of a command buffer managed
 * resource manager
 *
 * Returns the approximate allocation size of a command buffer managed
 * resource manager.
 */
size_t vmw_cmdbuf_res_man_size(void)
{
	static size_t res_man_size;

	if (unlikely(res_man_size == 0))
		res_man_size =
			ttm_round_pot(sizeof(struct vmw_cmdbuf_res_manager)) +
			ttm_round_pot(sizeof(struct hlist_head) <<
				      VMW_CMDBUF_RES_MAN_HT_ORDER);

	return res_man_size;
}
