// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2018 Mellanox Technologies */

#include <linux/mlx5/vport.h>
#include <linux/list.h>
#include "lib/devcom.h"
#include "mlx5_core.h"

static LIST_HEAD(devcom_dev_list);
static LIST_HEAD(devcom_comp_list);
/* protect device list */
static DEFINE_MUTEX(dev_list_lock);
/* protect component list */
static DEFINE_MUTEX(comp_list_lock);

#define devcom_for_each_component(iter) \
	list_for_each_entry(iter, &devcom_comp_list, comp_list)

struct mlx5_devcom_dev {
	struct list_head list;
	struct mlx5_core_dev *dev;
	struct kref ref;
};

struct mlx5_devcom_comp {
	struct list_head comp_list;
	enum mlx5_devcom_component id;
	u64 key;
	struct list_head comp_dev_list_head;
	mlx5_devcom_event_handler_t handler;
	struct kref ref;
	bool ready;
	struct rw_semaphore sem;
	struct lock_class_key lock_key;
};

struct mlx5_devcom_comp_dev {
	struct list_head list;
	struct mlx5_devcom_comp *comp;
	struct mlx5_devcom_dev *devc;
	void __rcu *data;
};

static bool devcom_dev_exists(struct mlx5_core_dev *dev)
{
	struct mlx5_devcom_dev *iter;

	list_for_each_entry(iter, &devcom_dev_list, list)
		if (iter->dev == dev)
			return true;

	return false;
}

static struct mlx5_devcom_dev *
mlx5_devcom_dev_alloc(struct mlx5_core_dev *dev)
{
	struct mlx5_devcom_dev *devc;

	devc = kzalloc(sizeof(*devc), GFP_KERNEL);
	if (!devc)
		return NULL;

	devc->dev = dev;
	kref_init(&devc->ref);
	return devc;
}

struct mlx5_devcom_dev *
mlx5_devcom_register_device(struct mlx5_core_dev *dev)
{
	struct mlx5_devcom_dev *devc;

	mutex_lock(&dev_list_lock);

	if (devcom_dev_exists(dev)) {
		devc = ERR_PTR(-EEXIST);
		goto out;
	}

	devc = mlx5_devcom_dev_alloc(dev);
	if (!devc) {
		devc = ERR_PTR(-ENOMEM);
		goto out;
	}

	list_add_tail(&devc->list, &devcom_dev_list);
out:
	mutex_unlock(&dev_list_lock);
	return devc;
}

static void
mlx5_devcom_dev_release(struct kref *ref)
{
	struct mlx5_devcom_dev *devc = container_of(ref, struct mlx5_devcom_dev, ref);

	mutex_lock(&dev_list_lock);
	list_del(&devc->list);
	mutex_unlock(&dev_list_lock);
	kfree(devc);
}

void mlx5_devcom_unregister_device(struct mlx5_devcom_dev *devc)
{
	if (!IS_ERR_OR_NULL(devc))
		kref_put(&devc->ref, mlx5_devcom_dev_release);
}

static struct mlx5_devcom_comp *
mlx5_devcom_comp_alloc(u64 id, u64 key, mlx5_devcom_event_handler_t handler)
{
	struct mlx5_devcom_comp *comp;

	comp = kzalloc(sizeof(*comp), GFP_KERNEL);
	if (!comp)
		return ERR_PTR(-ENOMEM);

	comp->id = id;
	comp->key = key;
	comp->handler = handler;
	init_rwsem(&comp->sem);
	lockdep_register_key(&comp->lock_key);
	lockdep_set_class(&comp->sem, &comp->lock_key);
	kref_init(&comp->ref);
	INIT_LIST_HEAD(&comp->comp_dev_list_head);

	return comp;
}

static void
mlx5_devcom_comp_release(struct kref *ref)
{
	struct mlx5_devcom_comp *comp = container_of(ref, struct mlx5_devcom_comp, ref);

	mutex_lock(&comp_list_lock);
	list_del(&comp->comp_list);
	mutex_unlock(&comp_list_lock);
	lockdep_unregister_key(&comp->lock_key);
	kfree(comp);
}

static struct mlx5_devcom_comp_dev *
devcom_alloc_comp_dev(struct mlx5_devcom_dev *devc,
		      struct mlx5_devcom_comp *comp,
		      void *data)
{
	struct mlx5_devcom_comp_dev *devcom;

	devcom = kzalloc(sizeof(*devcom), GFP_KERNEL);
	if (!devcom)
		return ERR_PTR(-ENOMEM);

	kref_get(&devc->ref);
	devcom->devc = devc;
	devcom->comp = comp;
	rcu_assign_pointer(devcom->data, data);

	down_write(&comp->sem);
	list_add_tail(&devcom->list, &comp->comp_dev_list_head);
	up_write(&comp->sem);

	return devcom;
}

static void
devcom_free_comp_dev(struct mlx5_devcom_comp_dev *devcom)
{
	struct mlx5_devcom_comp *comp = devcom->comp;

	down_write(&comp->sem);
	list_del(&devcom->list);
	up_write(&comp->sem);

	kref_put(&devcom->devc->ref, mlx5_devcom_dev_release);
	kfree(devcom);
	kref_put(&comp->ref, mlx5_devcom_comp_release);
}

static bool
devcom_component_equal(struct mlx5_devcom_comp *devcom,
		       enum mlx5_devcom_component id,
		       u64 key)
{
	return devcom->id == id && devcom->key == key;
}

static struct mlx5_devcom_comp *
devcom_component_get(struct mlx5_devcom_dev *devc,
		     enum mlx5_devcom_component id,
		     u64 key,
		     mlx5_devcom_event_handler_t handler)
{
	struct mlx5_devcom_comp *comp;

	devcom_for_each_component(comp) {
		if (devcom_component_equal(comp, id, key)) {
			if (handler == comp->handler) {
				kref_get(&comp->ref);
				return comp;
			}

			mlx5_core_err(devc->dev,
				      "Cannot register existing devcom component with different handler\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return NULL;
}

struct mlx5_devcom_comp_dev *
mlx5_devcom_register_component(struct mlx5_devcom_dev *devc,
			       enum mlx5_devcom_component id,
			       u64 key,
			       mlx5_devcom_event_handler_t handler,
			       void *data)
{
	struct mlx5_devcom_comp_dev *devcom;
	struct mlx5_devcom_comp *comp;

	if (IS_ERR_OR_NULL(devc))
		return NULL;

	mutex_lock(&comp_list_lock);
	comp = devcom_component_get(devc, id, key, handler);
	if (IS_ERR(comp)) {
		devcom = ERR_PTR(-EINVAL);
		goto out_unlock;
	}

	if (!comp) {
		comp = mlx5_devcom_comp_alloc(id, key, handler);
		if (IS_ERR(comp)) {
			devcom = ERR_CAST(comp);
			goto out_unlock;
		}
		list_add_tail(&comp->comp_list, &devcom_comp_list);
	}
	mutex_unlock(&comp_list_lock);

	devcom = devcom_alloc_comp_dev(devc, comp, data);
	if (IS_ERR(devcom))
		kref_put(&comp->ref, mlx5_devcom_comp_release);

	return devcom;

out_unlock:
	mutex_unlock(&comp_list_lock);
	return devcom;
}

void mlx5_devcom_unregister_component(struct mlx5_devcom_comp_dev *devcom)
{
	if (!IS_ERR_OR_NULL(devcom))
		devcom_free_comp_dev(devcom);
}

int mlx5_devcom_comp_get_size(struct mlx5_devcom_comp_dev *devcom)
{
	struct mlx5_devcom_comp *comp = devcom->comp;

	return kref_read(&comp->ref);
}

int mlx5_devcom_send_event(struct mlx5_devcom_comp_dev *devcom,
			   int event, int rollback_event,
			   void *event_data)
{
	struct mlx5_devcom_comp_dev *pos;
	struct mlx5_devcom_comp *comp;
	int err = 0;
	void *data;

	if (IS_ERR_OR_NULL(devcom))
		return -ENODEV;

	comp = devcom->comp;
	down_write(&comp->sem);
	list_for_each_entry(pos, &comp->comp_dev_list_head, list) {
		data = rcu_dereference_protected(pos->data, lockdep_is_held(&comp->sem));

		if (pos != devcom && data) {
			err = comp->handler(event, data, event_data);
			if (err)
				goto rollback;
		}
	}

	up_write(&comp->sem);
	return 0;

rollback:
	if (list_entry_is_head(pos, &comp->comp_dev_list_head, list))
		goto out;
	pos = list_prev_entry(pos, list);
	list_for_each_entry_from_reverse(pos, &comp->comp_dev_list_head, list) {
		data = rcu_dereference_protected(pos->data, lockdep_is_held(&comp->sem));

		if (pos != devcom && data)
			comp->handler(rollback_event, data, event_data);
	}
out:
	up_write(&comp->sem);
	return err;
}

void mlx5_devcom_comp_set_ready(struct mlx5_devcom_comp_dev *devcom, bool ready)
{
	WARN_ON(!rwsem_is_locked(&devcom->comp->sem));

	WRITE_ONCE(devcom->comp->ready, ready);
}

bool mlx5_devcom_comp_is_ready(struct mlx5_devcom_comp_dev *devcom)
{
	if (IS_ERR_OR_NULL(devcom))
		return false;

	return READ_ONCE(devcom->comp->ready);
}

bool mlx5_devcom_for_each_peer_begin(struct mlx5_devcom_comp_dev *devcom)
{
	struct mlx5_devcom_comp *comp;

	if (IS_ERR_OR_NULL(devcom))
		return false;

	comp = devcom->comp;
	down_read(&comp->sem);
	if (!READ_ONCE(comp->ready)) {
		up_read(&comp->sem);
		return false;
	}

	return true;
}

void mlx5_devcom_for_each_peer_end(struct mlx5_devcom_comp_dev *devcom)
{
	up_read(&devcom->comp->sem);
}

void *mlx5_devcom_get_next_peer_data(struct mlx5_devcom_comp_dev *devcom,
				     struct mlx5_devcom_comp_dev **pos)
{
	struct mlx5_devcom_comp *comp = devcom->comp;
	struct mlx5_devcom_comp_dev *tmp;
	void *data;

	tmp = list_prepare_entry(*pos, &comp->comp_dev_list_head, list);

	list_for_each_entry_continue(tmp, &comp->comp_dev_list_head, list) {
		if (tmp != devcom) {
			data = rcu_dereference_protected(tmp->data, lockdep_is_held(&comp->sem));
			if (data)
				break;
		}
	}

	if (list_entry_is_head(tmp, &comp->comp_dev_list_head, list))
		return NULL;

	*pos = tmp;
	return data;
}

void *mlx5_devcom_get_next_peer_data_rcu(struct mlx5_devcom_comp_dev *devcom,
					 struct mlx5_devcom_comp_dev **pos)
{
	struct mlx5_devcom_comp *comp = devcom->comp;
	struct mlx5_devcom_comp_dev *tmp;
	void *data;

	tmp = list_prepare_entry(*pos, &comp->comp_dev_list_head, list);

	list_for_each_entry_continue(tmp, &comp->comp_dev_list_head, list) {
		if (tmp != devcom) {
			/* This can change concurrently, however 'data' pointer will remain
			 * valid for the duration of RCU read section.
			 */
			if (!READ_ONCE(comp->ready))
				return NULL;
			data = rcu_dereference(tmp->data);
			if (data)
				break;
		}
	}

	if (list_entry_is_head(tmp, &comp->comp_dev_list_head, list))
		return NULL;

	*pos = tmp;
	return data;
}

void mlx5_devcom_comp_lock(struct mlx5_devcom_comp_dev *devcom)
{
	if (IS_ERR_OR_NULL(devcom))
		return;
	down_write(&devcom->comp->sem);
}

void mlx5_devcom_comp_unlock(struct mlx5_devcom_comp_dev *devcom)
{
	if (IS_ERR_OR_NULL(devcom))
		return;
	up_write(&devcom->comp->sem);
}

int mlx5_devcom_comp_trylock(struct mlx5_devcom_comp_dev *devcom)
{
	if (IS_ERR_OR_NULL(devcom))
		return 0;
	return down_write_trylock(&devcom->comp->sem);
}
