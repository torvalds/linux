/*
 * System Trace Module (STM) master/channel allocation policy management
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * A master/channel allocation policy allows mapping string identifiers to
 * master and channel ranges, where allocation can be done.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/configfs.h>
#include <linux/slab.h>
#include <linux/stm.h>
#include "stm.h"

/*
 * STP Master/Channel allocation policy configfs layout.
 */

struct stp_policy {
	struct config_group	group;
	struct stm_device	*stm;
};

struct stp_policy_node {
	struct config_group	group;
	struct stp_policy	*policy;
	unsigned int		first_master;
	unsigned int		last_master;
	unsigned int		first_channel;
	unsigned int		last_channel;
};

static struct configfs_subsystem stp_policy_subsys;

void stp_policy_node_get_ranges(struct stp_policy_node *policy_node,
				unsigned int *mstart, unsigned int *mend,
				unsigned int *cstart, unsigned int *cend)
{
	*mstart	= policy_node->first_master;
	*mend	= policy_node->last_master;
	*cstart	= policy_node->first_channel;
	*cend	= policy_node->last_channel;
}

static inline char *stp_policy_node_name(struct stp_policy_node *policy_node)
{
	return policy_node->group.cg_item.ci_name ? : "<none>";
}

static inline struct stp_policy *to_stp_policy(struct config_item *item)
{
	return item ?
		container_of(to_config_group(item), struct stp_policy, group) :
		NULL;
}

static inline struct stp_policy_node *
to_stp_policy_node(struct config_item *item)
{
	return item ?
		container_of(to_config_group(item), struct stp_policy_node,
			     group) :
		NULL;
}

static ssize_t stp_policy_node_masters_show(struct stp_policy_node *policy_node,
					    char *page)
{
	ssize_t count;

	count = sprintf(page, "%u %u\n", policy_node->first_master,
			policy_node->last_master);

	return count;
}

static ssize_t
stp_policy_node_masters_store(struct stp_policy_node *policy_node,
			      const char *page, size_t count)
{
	unsigned int first, last;
	struct stm_device *stm;
	char *p = (char *)page;
	ssize_t ret = -ENODEV;

	if (sscanf(p, "%u %u", &first, &last) != 2)
		return -EINVAL;

	mutex_lock(&stp_policy_subsys.su_mutex);
	stm = policy_node->policy->stm;
	if (!stm)
		goto unlock;

	/* must be within [sw_start..sw_end], which is an inclusive range */
	if (first > INT_MAX || last > INT_MAX || first > last ||
	    first < stm->data->sw_start ||
	    last > stm->data->sw_end) {
		ret = -ERANGE;
		goto unlock;
	}

	ret = count;
	policy_node->first_master = first;
	policy_node->last_master = last;

unlock:
	mutex_unlock(&stp_policy_subsys.su_mutex);

	return ret;
}

static ssize_t
stp_policy_node_channels_show(struct stp_policy_node *policy_node, char *page)
{
	ssize_t count;

	count = sprintf(page, "%u %u\n", policy_node->first_channel,
			policy_node->last_channel);

	return count;
}

static ssize_t
stp_policy_node_channels_store(struct stp_policy_node *policy_node,
			       const char *page, size_t count)
{
	unsigned int first, last;
	struct stm_device *stm;
	char *p = (char *)page;
	ssize_t ret = -ENODEV;

	if (sscanf(p, "%u %u", &first, &last) != 2)
		return -EINVAL;

	mutex_lock(&stp_policy_subsys.su_mutex);
	stm = policy_node->policy->stm;
	if (!stm)
		goto unlock;

	if (first > INT_MAX || last > INT_MAX || first > last ||
	    last >= stm->data->sw_nchannels) {
		ret = -ERANGE;
		goto unlock;
	}

	ret = count;
	policy_node->first_channel = first;
	policy_node->last_channel = last;

unlock:
	mutex_unlock(&stp_policy_subsys.su_mutex);

	return ret;
}

static void stp_policy_node_release(struct config_item *item)
{
	kfree(to_stp_policy_node(item));
}

struct stp_policy_node_attribute {
	struct configfs_attribute	attr;
	ssize_t (*show)(struct stp_policy_node *, char *);
	ssize_t (*store)(struct stp_policy_node *, const char *, size_t);
};

static ssize_t stp_policy_node_attr_show(struct config_item *item,
					 struct configfs_attribute *attr,
					 char *page)
{
	struct stp_policy_node *policy_node = to_stp_policy_node(item);
	struct stp_policy_node_attribute *pn_attr =
		container_of(attr, struct stp_policy_node_attribute, attr);
	ssize_t count = 0;

	if (pn_attr->show)
		count = pn_attr->show(policy_node, page);

	return count;
}

static ssize_t stp_policy_node_attr_store(struct config_item *item,
					  struct configfs_attribute *attr,
					  const char *page, size_t len)
{
	struct stp_policy_node *policy_node = to_stp_policy_node(item);
	struct stp_policy_node_attribute *pn_attr =
		container_of(attr, struct stp_policy_node_attribute, attr);
	ssize_t count = -EINVAL;

	if (pn_attr->store)
		count = pn_attr->store(policy_node, page, len);

	return count;
}

static struct configfs_item_operations stp_policy_node_item_ops = {
	.release		= stp_policy_node_release,
	.show_attribute		= stp_policy_node_attr_show,
	.store_attribute	= stp_policy_node_attr_store,
};

static struct stp_policy_node_attribute stp_policy_node_attr_range = {
	.attr	= {
		.ca_owner = THIS_MODULE,
		.ca_name = "masters",
		.ca_mode = S_IRUGO | S_IWUSR,
	},
	.show	= stp_policy_node_masters_show,
	.store	= stp_policy_node_masters_store,
};

static struct stp_policy_node_attribute stp_policy_node_attr_channels = {
	.attr	= {
		.ca_owner = THIS_MODULE,
		.ca_name = "channels",
		.ca_mode = S_IRUGO | S_IWUSR,
	},
	.show	= stp_policy_node_channels_show,
	.store	= stp_policy_node_channels_store,
};

static struct configfs_attribute *stp_policy_node_attrs[] = {
	&stp_policy_node_attr_range.attr,
	&stp_policy_node_attr_channels.attr,
	NULL,
};

static struct config_item_type stp_policy_type;
static struct config_item_type stp_policy_node_type;

static struct config_group *
stp_policy_node_make(struct config_group *group, const char *name)
{
	struct stp_policy_node *policy_node, *parent_node;
	struct stp_policy *policy;

	if (group->cg_item.ci_type == &stp_policy_type) {
		policy = container_of(group, struct stp_policy, group);
	} else {
		parent_node = container_of(group, struct stp_policy_node,
					   group);
		policy = parent_node->policy;
	}

	if (!policy->stm)
		return ERR_PTR(-ENODEV);

	policy_node = kzalloc(sizeof(struct stp_policy_node), GFP_KERNEL);
	if (!policy_node)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&policy_node->group, name,
				    &stp_policy_node_type);

	policy_node->policy = policy;

	/* default values for the attributes */
	policy_node->first_master = policy->stm->data->sw_start;
	policy_node->last_master = policy->stm->data->sw_end;
	policy_node->first_channel = 0;
	policy_node->last_channel = policy->stm->data->sw_nchannels - 1;

	return &policy_node->group;
}

static void
stp_policy_node_drop(struct config_group *group, struct config_item *item)
{
	config_item_put(item);
}

static struct configfs_group_operations stp_policy_node_group_ops = {
	.make_group	= stp_policy_node_make,
	.drop_item	= stp_policy_node_drop,
};

static struct config_item_type stp_policy_node_type = {
	.ct_item_ops	= &stp_policy_node_item_ops,
	.ct_group_ops	= &stp_policy_node_group_ops,
	.ct_attrs	= stp_policy_node_attrs,
	.ct_owner	= THIS_MODULE,
};

/*
 * Root group: policies.
 */
static struct configfs_attribute stp_policy_attr_device = {
	.ca_owner = THIS_MODULE,
	.ca_name = "device",
	.ca_mode = S_IRUGO,
};

static struct configfs_attribute *stp_policy_attrs[] = {
	&stp_policy_attr_device,
	NULL,
};

static ssize_t stp_policy_attr_show(struct config_item *item,
				    struct configfs_attribute *attr,
				    char *page)
{
	struct stp_policy *policy = to_stp_policy(item);
	ssize_t count;

	count = sprintf(page, "%s\n",
			(policy && policy->stm) ?
			policy->stm->data->name :
			"<none>");

	return count;
}

void stp_policy_unbind(struct stp_policy *policy)
{
	struct stm_device *stm = policy->stm;

	if (WARN_ON_ONCE(!policy->stm))
		return;

	mutex_lock(&stm->policy_mutex);
	stm->policy = NULL;
	mutex_unlock(&stm->policy_mutex);

	policy->stm = NULL;

	stm_put_device(stm);
}

static void stp_policy_release(struct config_item *item)
{
	struct stp_policy *policy = to_stp_policy(item);

	stp_policy_unbind(policy);
	kfree(policy);
}

static struct configfs_item_operations stp_policy_item_ops = {
	.release		= stp_policy_release,
	.show_attribute		= stp_policy_attr_show,
};

static struct configfs_group_operations stp_policy_group_ops = {
	.make_group	= stp_policy_node_make,
};

static struct config_item_type stp_policy_type = {
	.ct_item_ops	= &stp_policy_item_ops,
	.ct_group_ops	= &stp_policy_group_ops,
	.ct_attrs	= stp_policy_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *
stp_policies_make(struct config_group *group, const char *name)
{
	struct config_group *ret;
	struct stm_device *stm;
	char *devname, *p;

	devname = kasprintf(GFP_KERNEL, "%s", name);
	if (!devname)
		return ERR_PTR(-ENOMEM);

	/*
	 * node must look like <device_name>.<policy_name>, where
	 * <device_name> is the name of an existing stm device and
	 * <policy_name> is an arbitrary string
	 */
	p = strchr(devname, '.');
	if (!p) {
		kfree(devname);
		return ERR_PTR(-EINVAL);
	}

	*p++ = '\0';

	stm = stm_find_device(devname);
	kfree(devname);

	if (!stm)
		return ERR_PTR(-ENODEV);

	mutex_lock(&stm->policy_mutex);
	if (stm->policy) {
		ret = ERR_PTR(-EBUSY);
		goto unlock_policy;
	}

	stm->policy = kzalloc(sizeof(*stm->policy), GFP_KERNEL);
	if (!stm->policy) {
		ret = ERR_PTR(-ENOMEM);
		goto unlock_policy;
	}

	config_group_init_type_name(&stm->policy->group, name,
				    &stp_policy_type);
	stm->policy->stm = stm;

	ret = &stm->policy->group;

unlock_policy:
	mutex_unlock(&stm->policy_mutex);

	if (IS_ERR(ret))
		stm_put_device(stm);

	return ret;
}

static struct configfs_group_operations stp_policies_group_ops = {
	.make_group	= stp_policies_make,
};

static struct config_item_type stp_policies_type = {
	.ct_group_ops	= &stp_policies_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem stp_policy_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf	= "stp-policy",
			.ci_type	= &stp_policies_type,
		},
	},
};

/*
 * Lock the policy mutex from the outside
 */
static struct stp_policy_node *
__stp_policy_node_lookup(struct stp_policy *policy, char *s)
{
	struct stp_policy_node *policy_node, *ret;
	struct list_head *head = &policy->group.cg_children;
	struct config_item *item;
	char *start, *end = s;

	if (list_empty(head))
		return NULL;

	/* return the first entry if everything else fails */
	item = list_entry(head->next, struct config_item, ci_entry);
	ret = to_stp_policy_node(item);

next:
	for (;;) {
		start = strsep(&end, "/");
		if (!start)
			break;

		if (!*start)
			continue;

		list_for_each_entry(item, head, ci_entry) {
			policy_node = to_stp_policy_node(item);

			if (!strcmp(start,
				    policy_node->group.cg_item.ci_name)) {
				ret = policy_node;

				if (!end)
					goto out;

				head = &policy_node->group.cg_children;
				goto next;
			}
		}
		break;
	}

out:
	return ret;
}


struct stp_policy_node *
stp_policy_node_lookup(struct stm_device *stm, char *s)
{
	struct stp_policy_node *policy_node = NULL;

	mutex_lock(&stp_policy_subsys.su_mutex);

	mutex_lock(&stm->policy_mutex);
	if (stm->policy)
		policy_node = __stp_policy_node_lookup(stm->policy, s);
	mutex_unlock(&stm->policy_mutex);

	if (policy_node)
		config_item_get(&policy_node->group.cg_item);
	mutex_unlock(&stp_policy_subsys.su_mutex);

	return policy_node;
}

void stp_policy_node_put(struct stp_policy_node *policy_node)
{
	config_item_put(&policy_node->group.cg_item);
}

int __init stp_configfs_init(void)
{
	int err;

	config_group_init(&stp_policy_subsys.su_group);
	mutex_init(&stp_policy_subsys.su_mutex);
	err = configfs_register_subsystem(&stp_policy_subsys);

	return err;
}

void __exit stp_configfs_exit(void)
{
	configfs_unregister_subsystem(&stp_policy_subsys);
}
